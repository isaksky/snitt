#include "backend.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTimer>
#include <QUuid>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QPixmap>
#include <QDir>
#include <QWindow>
#include <algorithm>
#include "regionselector.h"
#include "appsettings.h"
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

Backend::Backend(AppSettings *settings, QObject *parent) : QObject(parent), m_settings(settings) {
    connect(&m_recorder, &Recorder::changed, this, &Backend::recordingChanged);
    connect(&m_recorder, &Recorder::processStarted, this, &Backend::recordingProcessStarted);
    connect(&m_recorder, &Recorder::ready, this, &Backend::recordingReady);
    connect(&m_recorder, &Recorder::ready, this, [this] {
        if (!m_stopWhenReady) return;
        m_stopWhenReady = false;
        m_recorder.finish();
    });
    connect(&m_recorder, &Recorder::elapsedChanged, this, &Backend::recordingElapsedChanged);
    connect(&m_recorder, &Recorder::canceled, this, &Backend::recordingCanceled);
    connect(&m_recorder, &Recorder::error, this, &Backend::error);
    connect(&m_recorder, &Recorder::saved, this, [this](const QString &path) {
        const QString nativePath = QDir::toNativeSeparators(path);
        m_trim.open(path);
        emit recordingSaved(nativePath);
    });
    connect(&m_trim, &TrimSession::finalized, this, [this](const QString &path) {
        if (!recording::revealSavedFile(path))
            emit error(QStringLiteral("Recording saved at %1, but could not reveal it in the file manager.")
                       .arg(QDir::toNativeSeparators(path)));
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
        if (m_externalCaptureRouting)
            emit captureScreenReady(m_screenIndex, m_screens.size());
        else
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
    if (!windowsExclusionSupported() || m_exclusionTestMode == ExclusionTestMode::ForceFailure)
        return false;
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) return false;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    constexpr DWORD excludeFromCapture = 0x00000011;
    DWORD affinity = 0;
    return SetWindowDisplayAffinity(handle, excludeFromCapture) != FALSE
        && GetWindowDisplayAffinity(handle, &affinity) != FALSE
        && affinity == excludeFromCapture;
#else
    Q_UNUSED(object);
    return false;
#endif
}

bool Backend::windowsExclusionSupported() const {
#ifdef Q_OS_WIN
    if (m_exclusionTestMode == ExclusionTestMode::ForceLegacyVersion) return false;
    using VersionFunction = LONG (WINAPI *)(OSVERSIONINFOW *);
    auto *versionFunction = reinterpret_cast<VersionFunction>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (!versionFunction) return false;
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    return versionFunction(&version) == 0
        && (version.dwMajorVersion > 10
            || (version.dwMajorVersion == 10 && version.dwBuildNumber >= 19041));
#else
    return false;
#endif
}

bool Backend::beginProtectedRecording(QObject *indicator, QObject *controls, QObject *outline) {
#ifdef Q_OS_WIN
    if (!m_pendingRecording) return false;
    QString problem;
    if (!windowsExclusionSupported())
        problem = QStringLiteral("Video recording needs Windows 10 version 2004 or later so Snitt's controls can be excluded from the captured screen.");
    else if (!protectRecordingControls(indicator) || !protectRecordingControls(controls)
             || (outline && !protectRecordingControls(outline)))
        problem = QStringLiteral("Could not exclude Snitt's recording controls from the captured screen. Recording was canceled; try again after updating Windows or your display driver.");
    if (!problem.isEmpty()) {
        m_pendingRecording = false;
        emit recordingChanged();
        emit error(problem);
        return false;
    }
    const recording::Source source = m_pendingSource;
    m_pendingRecording = false;
    m_recorder.start(source, m_settings ? m_settings->videosRoot() : QString());
    emit recordingChanged();
    return m_recorder.active();
#else
    Q_UNUSED(indicator);
    Q_UNUSED(controls);
    Q_UNUSED(outline);
    return false;
#endif
}

void Backend::cancelRecording() {
    m_stopWhenReady = false;
    if (!m_pendingRecording) { m_recorder.cancel(); return; }
    m_pendingRecording = false;
    emit recordingChanged();
    emit recordingCanceled();
}

void Backend::stopRecordingFromHotkey() {
    if (!recording() || finishingRecording()) return;
    if (startingRecording()) { m_stopWhenReady = true; return; }
    m_recorder.finish();
}

QRect Backend::controlsGeometry(const QRect &available) {
    const int width = qMin(340, qMax(1, available.width()));
    const int height = qMin(58, qMax(1, available.height()));
    const int x = available.x() + (available.width() - width) / 2;
    const int y = available.y() + qMin(12, available.height() - height);
    return {x, y, width, height};
}

QRect Backend::indicatorGeometry(const QRect &region, const QRect &screen, const QRect &available) {
    const int nominal = qRound(qMin(screen.width(), screen.height()) / 3.0);
    const int diameter = qMax(1, qMin(nominal, qMin(available.width(), available.height())));
    const int x = qBound(available.left(), qRound(region.center().x() - diameter / 2.0),
                         available.right() - diameter + 1);
    const int y = qBound(available.top(), qRound(region.center().y() - diameter / 2.0),
                         available.bottom() - diameter + 1);
    QRect indicator(x, y, diameter, diameter);
    const QRect controls = controlsGeometry(available);
    if (indicator.intersects(controls)) {
        const int belowControls = controls.bottom() + 9;
        if (belowControls + diameter - 1 <= available.bottom())
            indicator.moveTop(belowControls);
        else if (controls.left() - diameter - 8 >= available.left())
            indicator.moveLeft(controls.left() - diameter - 8);
        else if (controls.right() + diameter + 9 <= available.right())
            indicator.moveLeft(controls.right() + 9);
    }
    return indicator;
}

void Backend::capture(bool multiple, bool video) {
    if (m_capturing || recording() || !m_trim.path().isEmpty()) return;
#ifdef Q_OS_MACOS
    // Without permission macOS can return a successful wallpaper-only image.
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        emit error(QStringLiteral("Allow Snitt in System Settings → Privacy & Security → Screen & System Audio Recording, then quit and reopen Snitt."));
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
        // The non-interactive system tool snapshots each display; Snitt owns
        // selection so M can switch to multiple regions on that frozen desktop.
        if (m_externalCaptureRouting)
            emit captureScreenReady(0, m_screens.size());
        else
            captureNextScreen();
#else
        DwmFlush();
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
            m_screens[i].image = screens[i]->grabWindow(0).toImage();
        if (m_externalCaptureRouting) {
            m_screenIndex = m_screens.size();
            emit captureScreenReady(m_screens.size(), m_screens.size());
        } else
            showSelectors();
#endif
    });
#else
    emit error(QStringLiteral("Screen capture supports macOS and Windows. Open or paste an image to edit it here."));
    finish();
#endif
}

void Backend::advanceCapture(int nextScreen) {
    if (m_capturing && m_externalCaptureRouting && nextScreen == m_screenIndex
        && m_process.state() == QProcess::NotRunning)
        captureNextScreen();
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
    if (m_externalRegionPicker) {
        if (m_screens.isEmpty() || std::none_of(m_screens.cbegin(), m_screens.cend(),
                                                [](const ScreenImage &screen) { return !screen.image.isNull(); })) {
            emit error(QStringLiteral("Could not capture a screen. Check screen recording permission and that the desktop is unlocked."));
            finish();
        } else {
            emit pickerReady();
        }
        return;
    }
    QPointer<RegionSelector> active;
    for (const auto &screen : m_screens) {
        if (screen.image.isNull()) continue;
        auto *selector = new RegionSelector(screen.image, screen.geometry, m_settings);
        m_selectors.append(selector);
        if (screen.geometry.contains(QCursor::pos())) active = selector;
        connect(selector, &RegionSelector::canceled, this, [this] { finish(); });
        connect(selector, &RegionSelector::selected, this, [this, selector](const QImage &image) {
            if (!m_capturing) return;
            if (!image.save(m_output, "PNG")) {
                emit error(QStringLiteral("Could not save the captured region."));
                finish();
                return;
            }
            m_captureScreenGeometry = selector->geometry();
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
            m_captureScreenGeometry = m_selections.last().owner->geometry();
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

QVariantList Backend::pickerScreens() const {
    QVariantList result;
    if (!m_capturing || !m_externalRegionPicker) return result;
    for (int i = 0; i < m_screens.size(); ++i) {
        const auto &screen = m_screens[i];
        if (screen.image.isNull()) continue;
        result.append(QVariantMap{{QStringLiteral("index"), i},
                                  {QStringLiteral("x"), screen.geometry.x()},
                                  {QStringLiteral("y"), screen.geometry.y()},
                                  {QStringLiteral("width"), screen.geometry.width()},
                                  {QStringLiteral("height"), screen.geometry.height()},
                                  {QStringLiteral("pixelWidth"), screen.image.width()},
                                  {QStringLiteral("pixelHeight"), screen.image.height()}});
    }
    return result;
}

QImage Backend::pickerImage(int screen) const {
    return m_capturing && m_externalRegionPicker && screen >= 0 && screen < m_screens.size()
        ? m_screens[screen].image : QImage();
}

bool Backend::pickerFixture(const QStringList &paths, const QStringList &geometries,
                            bool multiple, bool video) {
    if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") != QStringLiteral("1")
        || m_capturing || paths.isEmpty() || paths.size() != geometries.size()) return false;
    QList<ScreenImage> screens;
    for (int i = 0; i < paths.size(); ++i) {
        const QStringList parts = geometries[i].split(',');
        if (parts.size() != 4) return false;
        bool valid = true;
        int coords[4]{};
        for (int j = 0; j < 4; ++j) {
            bool parsed = false;
            coords[j] = parts[j].toInt(&parsed);
            valid &= parsed;
        }
        QImageReader reader(paths[i]);
        const QImage image = reader.read();
        if (!valid || coords[2] <= 0 || coords[3] <= 0 || image.isNull()) return false;
        screens.append({QRect(coords[0], coords[1], coords[2], coords[3]), image});
    }
    m_screens = screens;
    m_multiple = multiple && !video;
    m_video = video;
    m_capturing = true;
    m_output = m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    emit capturingChanged();
    showSelectors();
    return true;
}

bool Backend::pickerSingle(int screen, const QRectF &area) {
    if (!m_capturing || !m_externalRegionPicker || screen < 0 || screen >= m_screens.size()) return false;
    const auto &source = m_screens[screen];
    const QRect pixels = RegionSelector::pixelRect(area, source.geometry.size(), source.image.size());
    if (pixels.width() < 2 || pixels.height() < 2) return false;
    const QImage image = source.image.copy(pixels);
    if (image.isNull() || !image.save(m_output, "PNG")) {
        emit error(QStringLiteral("Could not save the captured region."));
        finish();
        return false;
    }
    m_captureScreenGeometry = source.geometry;
    emit captured(QUrl::fromLocalFile(m_output));
    finish(true);
    return true;
}

int Backend::pickerAdd(int screen, const QRectF &area) {
    if (!m_capturing || !m_externalRegionPicker || screen < 0 || screen >= m_screens.size()) return -1;
    const auto &source = m_screens[screen];
    const QRect pixels = RegionSelector::pixelRect(area, source.geometry.size(), source.image.size());
    if (pixels.width() < 2 || pixels.height() < 2) return -1;
    const QImage image = source.image.copy(pixels);
    qint64 bytes = image.sizeInBytes();
    int count = 0;
    for (const auto &region : m_pickerRegions) if (!region.isNull()) { ++count; bytes += region.sizeInBytes(); }
    if (count >= 24 || image.isNull() || bytes > 256 * 1024 * 1024) {
        emit pickerNotice(QStringLiteral("Selection limit reached. Remove a region or continue to Arrange."));
        return -1;
    }
    m_pickerRegions.append(image);
    m_pickerRegionScreens.append(screen);
    return m_pickerRegions.size() - 1;
}

void Backend::pickerRemove(int token) {
    if (m_capturing && token >= 0 && token < m_pickerRegions.size()) m_pickerRegions[token] = QImage();
}

bool Backend::pickerAccept(const QStringList &tokens) {
    if (!m_capturing || !m_externalRegionPicker || tokens.isEmpty()) return false;
    QVariantList images;
    for (const QString &text : tokens) {
        bool parsed = false;
        const int token = text.toInt(&parsed);
        if (!parsed || token < 0 || token >= m_pickerRegions.size() || m_pickerRegions[token].isNull())
            return false;
        images.append(m_pickerRegions[token]);
        m_captureScreenGeometry = m_screens[m_pickerRegionScreens[token]].geometry;
    }
    emit regionsCaptured(images);
    finish(true);
    return true;
}

bool Backend::pickerVideo(int screen, const QRectF &area) {
    if (!m_capturing || !m_externalRegionPicker || !m_video || screen < 0 || screen >= m_screens.size()) return false;
    const auto &source = m_screens[screen];
    const QRect pixels = RegionSelector::pixelRect(area, source.geometry.size(), source.image.size());
    if (pixels.width() < 2 || pixels.height() < 2) return false;
    startRecording(source.geometry, source.geometry.size(), area);
    return true;
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

QRect Backend::editorGeometry(const QRect &available, const QSize &size) {
    // Leave room for the native title bar and window borders. Coordinates are
    // logical desktop coordinates, including monitors above/left of primary.
    const QRect bounds = available.adjusted(16, 32, -16, -16);
    const QSize fitted = size.boundedTo(bounds.size());
    return QRect(bounds.x() + (bounds.width() - fitted.width()) / 2,
                 bounds.y() + (bounds.height() - fitted.height()) / 2,
                 fitted.width(), fitted.height());
}

void Backend::positionEditorForCapture(QObject *editor) {
    auto *window = qobject_cast<QWindow *>(editor);
    if (!window || !m_captureScreenGeometry.isValid()) return;
    QScreen *screen = QGuiApplication::screenAt(m_captureScreenGeometry.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QSize size = window->size();
    window->setScreen(screen);
    window->setGeometry(editorGeometry(screen->availableGeometry(), size));
}

void Backend::startRecording(RegionSelector *selector, const QRectF &area) {
    startRecording(selector->geometry(), selector->size(), area, selector);
}

void Backend::startRecording(const QRect &screenGeometry, const QSize &viewSize, const QRectF &area,
                             RegionSelector *selector) {
    if (!m_capturing || !m_video) return;
    m_stopWhenReady = false;
    recording::Source source;
    m_recordingRegion = area.toAlignedRect().translated(screenGeometry.topLeft());
    if (auto *screen = QGuiApplication::screenAt(m_recordingRegion.center())) {
        m_recordingIndicatorGeometry = indicatorGeometry(m_recordingRegion, screen->geometry(),
                                                          screen->availableGeometry());
        m_recordingControlsGeometry = controlsGeometry(screen->availableGeometry());
    } else {
        m_recordingIndicatorGeometry = indicatorGeometry(m_recordingRegion, screenGeometry,
                                                         screenGeometry);
        m_recordingControlsGeometry = controlsGeometry(screenGeometry);
    }
#ifdef Q_OS_MACOS
    Q_UNUSED(selector);
    source.platform = recording::Platform::Mac;
    source.relativeRegion = QRectF(area.x() / viewSize.width(), area.y() / viewSize.height(),
                                  area.width() / viewSize.width(), area.height() / viewSize.height());
    // Match the native display ID, rather than assuming Qt and the capture
    // framework enumerate secondary displays in the same order.
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    QList<CGDirectDisplayID> displays(count);
    CGGetActiveDisplayList(count, displays.data(), &count);
    source.screenIndex = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const CGRect bounds = CGDisplayBounds(displays[i]);
        const QRect geometry(qRound(bounds.origin.x), qRound(bounds.origin.y),
                             qRound(bounds.size.width), qRound(bounds.size.height));
        if (geometry == screenGeometry) {
            source.screenIndex = int(i);
            source.displayId = displays[i];
            break;
        }
    }
    if (source.screenIndex < 0) {
        emit error(QStringLiteral("The selected display changed. Select the region again."));
        finish();
        return;
    }
#elif defined(Q_OS_WIN)
    source.platform = recording::Platform::Windows;
    const RECT monitorArea{m_recordingRegion.left(), m_recordingRegion.top(),
                           m_recordingRegion.right() + 1, m_recordingRegion.bottom() + 1};
    const HMONITOR monitor = selector
        ? MonitorFromWindow(reinterpret_cast<HWND>(selector->winId()), MONITOR_DEFAULTTONEAREST)
        : MonitorFromRect(&monitorArea, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        emit error(QStringLiteral("Could not determine the selected display's recording coordinates."));
        finish();
        return;
    }
    const QRect monitorPixels(info.rcMonitor.left, info.rcMonitor.top,
                              info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
    source.pixelRegion = RegionSelector::pixelRect(area, viewSize, monitorPixels.size()).translated(monitorPixels.topLeft());
#else
    emit error(QStringLiteral("Screen recording supports macOS and Windows."));
    finish();
    return;
#endif
    // The native macOS recorder excludes every Snitt window. The selector is
    // also hidden promptly so the live desktop is visible to the user.
    for (auto overlay : m_selectors) if (overlay) overlay->hide();
#ifdef Q_OS_WIN
    DwmFlush();
#endif
#ifdef Q_OS_WIN
    // The indicator and controls must both exist and be excluded before FFmpeg
    // samples a frame. QML makes their windows transparent until this succeeds.
    m_pendingSource = source;
    m_pendingRecording = true;
    emit recordingChanged();
#else
    m_recorder.start(source, m_settings ? m_settings->videosRoot() : QString());
#endif
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
    m_pickerRegions.clear();
    m_pickerRegionScreens.clear();
    m_screens.clear();
    QFile::remove(m_output);
    m_capturing = false;
    emit capturingChanged();
    emit captureFinished(captured);
}
