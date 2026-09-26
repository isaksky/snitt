#include "backend.h"
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QPixmap>
#include <QClipboard>
#include <QDir>
#include <QWindow>
#include "regionselector.h"
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_recorder, &VideoRecorder::changed, this, &Backend::recordingChanged);
    connect(&m_recorder, &VideoRecorder::elapsedChanged, this, &Backend::recordingElapsedChanged);
    connect(&m_recorder, &VideoRecorder::canceled, this, &Backend::recordingCanceled);
    connect(&m_recorder, &VideoRecorder::error, this, &Backend::error);
    connect(&m_recorder, &VideoRecorder::saved, this, [this](const QString &path) {
        const QString nativePath = QDir::toNativeSeparators(path);
        QGuiApplication::clipboard()->setText(nativePath);
        emit recordingSaved(nativePath);
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
        if (!m_capturing) return;
        const bool success = state == QProcess::NormalExit && code == 0
            && m_screenIndex < m_screens.size() && m_screens[m_screenIndex].image.load(m_output);
        QFile::remove(m_output);
        if (!success) {
            emit error(QStringLiteral("Could not capture the screen. Check System Settings → Privacy & Security → Screen & System Audio Recording, then try again."));
            finish();
            return;
        }
        ++m_screenIndex;
        captureNextScreen();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit this->error(QStringLiteral("Could not start the macOS screenshot tool."));
            finish();
        }
    });
}

Backend::~Backend() {
    for (auto selector : m_selectors) delete selector;
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

bool Backend::protectRecordingControls(QObject *object) {
#ifdef Q_OS_WIN
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) return false;
    // Windows 10 2004+ omits this window from desktop capture. On older
    // releases the same value falls back to the protected black rectangle.
    return SetWindowDisplayAffinity(reinterpret_cast<HWND>(window->winId()), 0x00000011) != FALSE;
#else
    Q_UNUSED(object);
    return false;
#endif
}

void Backend::capture(bool multiple, bool video) {
    if (m_capturing || recording()) return;
#ifdef Q_OS_MACOS
    // Without permission macOS can return a successful wallpaper-only image.
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        emit error(QStringLiteral("Allow xshot in System Settings → Privacy & Security → Screen & System Audio Recording, then quit and reopen xshot."));
        emit captureFinished(false);
        return;
    }
#endif
    if (!m_temp.isValid()) {
        emit error(QStringLiteral("Could not create a temporary capture file."));
        emit captureFinished(false);
        return;
    }
    m_capturing = true;
    emit capturingChanged();
    m_output = m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    m_video = video;
    m_multiple = multiple && !video;
    m_screens.clear();
    m_screenIndex = 0;
    for (QScreen *screen : QGuiApplication::screens()) m_screens.append({screen->geometry(), {}});
    QTimer::singleShot(0, this, [this] {
#ifdef Q_OS_MACOS
        // The non-interactive system tool snapshots each display; xshot owns
        // selection so M can switch to multiple regions on that frozen desktop.
        captureNextScreen();
#else
        DwmFlush();
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
            m_screens[i].image = screens[i]->grabWindow(0).toImage();
        showSelectors();
#endif
    });
#else
    emit error(QStringLiteral("Screen capture supports macOS and Windows. Open or paste an image to edit it here."));
    finish();
#endif
}

void Backend::captureNextScreen() {
    if (!m_capturing) return;
    if (m_screenIndex >= m_screens.size()) { showSelectors(); return; }
    const QRect area = m_screens[m_screenIndex].geometry;
    const QString bounds = QStringLiteral("%1,%2,%3,%4")
        .arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
    m_process.start(QStringLiteral("/usr/sbin/screencapture"),
                    {"-x", "-t", "png", "-R", bounds, m_output});
}

void Backend::showSelectors() {
    QPointer<RegionSelector> active;
    for (const auto &screen : m_screens) {
        if (screen.image.isNull()) continue;
        auto *selector = new RegionSelector(screen.image, screen.geometry);
        m_selectors.append(selector);
        if (screen.geometry.contains(QCursor::pos())) active = selector;
        connect(selector, &RegionSelector::canceled, this, [this] { finish(); });
        connect(selector, &RegionSelector::selected, this, [this](const QImage &image) {
            if (!m_capturing) return;
            if (!image.save(m_output, "PNG")) {
                emit error(QStringLiteral("Could not save the captured region."));
                finish();
                return;
            }
            emit captured(QUrl::fromLocalFile(m_output));
            finish(true);
        });
        connect(selector, &RegionSelector::singleRequested, this, [this] {
            m_video = false;
            m_multiple = false;
            m_selections.clear();
            updateSelections();
        });
        connect(selector, &RegionSelector::multipleRequested, this, [this] {
            if (!m_video) { m_multiple = true; updateSelections(); }
        });
        connect(selector, &RegionSelector::videoRequested, this, [this] {
            m_video = !m_video;
            m_multiple = false;
            m_selections.clear();
            updateSelections();
        });
        connect(selector, &RegionSelector::videoSelected, this, [this, selector](const QRectF &area) {
            startRecording(selector, area);
        });
        connect(selector, &RegionSelector::regionAdded, this, [this, selector](const QRectF &area) {
            qint64 bytes = 0;
            for (const auto &selection : m_selections) bytes += selection.image.sizeInBytes();
            const QImage image = selector->crop(area);
            if (m_selections.size() >= 24 || image.isNull() || bytes + image.sizeInBytes() > 256 * 1024 * 1024) {
                selector->setNotice(QStringLiteral("Selection limit reached. Remove a region or continue to Arrange."));
                return;
            }
            m_selections.append({selector, area, image});
            updateSelections();
        });
        connect(selector, &RegionSelector::regionRemoved, this, &Backend::removeSelection);
        connect(selector, &RegionSelector::removeLastRequested, this, [this] { removeSelection(m_selections.size() - 1); });
        connect(selector, &RegionSelector::accepted, this, [this] {
            if (m_selections.isEmpty()) return;
            QVariantList images;
            for (const auto &selection : m_selections) images.append(selection.image);
            emit regionsCaptured(images);
            finish(true);
        });
    }
    m_screens.clear();
    if (m_selectors.isEmpty()) {
        emit error(QStringLiteral("Could not capture a screen. Check screen recording permission and that the desktop is unlocked."));
        finish();
        return;
    }
    updateSelections();
    for (auto selector : m_selectors) selector->show();
    if (!active) active = m_selectors.first();
    active->raise();
    active->activateWindow();
    active->setFocus();
}

void Backend::updateSelections() {
    for (auto selector : m_selectors) {
        QList<QPair<int, QRectF>> areas;
        for (int i = 0; i < m_selections.size(); ++i)
            if (m_selections[i].owner == selector) areas.append({i, m_selections[i].area});
        selector->setSelections(m_multiple, areas, m_selections.size());
        selector->setVideo(m_video);
    }
}

void Backend::startRecording(RegionSelector *selector, const QRectF &area) {
    if (!m_capturing || !m_video) return;
    recording::Source source;
    m_recordingRegion = area.toAlignedRect().translated(selector->geometry().topLeft());
#ifdef Q_OS_MACOS
    source.platform = recording::Platform::Mac;
    source.relativeRegion = QRectF(area.x() / selector->width(), area.y() / selector->height(),
                                  area.width() / selector->width(), area.height() / selector->height());
    // AVFoundation's Capture screen N follows CGGetActiveDisplayList order,
    // which need not match Qt's screen list order.
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    QList<CGDirectDisplayID> displays(count);
    CGGetActiveDisplayList(count, displays.data(), &count);
    source.screenIndex = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const CGRect bounds = CGDisplayBounds(displays[i]);
        const QRect geometry(qRound(bounds.origin.x), qRound(bounds.origin.y),
                             qRound(bounds.size.width), qRound(bounds.size.height));
        if (geometry == selector->geometry()) { source.screenIndex = int(i); break; }
    }
    if (source.screenIndex < 0) {
        emit error(QStringLiteral("The selected display changed. Select the region again."));
        finish();
        return;
    }
#elif defined(Q_OS_WIN)
    source.platform = recording::Platform::Windows;
    const HMONITOR monitor = MonitorFromWindow(reinterpret_cast<HWND>(selector->winId()), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        emit error(QStringLiteral("Could not determine the selected display's recording coordinates."));
        finish();
        return;
    }
    const QRect monitorPixels(info.rcMonitor.left, info.rcMonitor.top,
                              info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
    source.pixelRegion = RegionSelector::pixelRect(area, selector->size(), monitorPixels.size()).translated(monitorPixels.topLeft());
#else
    emit error(QStringLiteral("Screen recording supports macOS and Windows."));
    finish();
    return;
#endif
    // Hide all frozen overlays before FFmpeg starts sampling the live desktop.
    for (auto overlay : m_selectors) if (overlay) overlay->hide();
#ifdef Q_OS_WIN
    DwmFlush();
#endif
    m_recorder.start(source);
    finish(false);
}

void Backend::removeSelection(int index) {
    if (index < 0 || index >= m_selections.size()) return;
    m_selections.removeAt(index);
    updateSelections();
}

void Backend::finish(bool captured) {
    for (auto selector : m_selectors) {
        if (!selector) continue;
        selector->disconnect(this);
        selector->hide();
        selector->deleteLater();
    }
    m_selectors.clear();
    m_selections.clear();
    m_screens.clear();
    QFile::remove(m_output);
    m_capturing = false;
    emit capturingChanged();
    emit captureFinished(captured);
}
