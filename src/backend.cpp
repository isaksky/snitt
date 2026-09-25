#include "backend.h"
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QPixmap>
#include "regionselector.h"
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

Backend::Backend(QObject *parent) : QObject(parent) {
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

void Backend::capture(bool multiple) {
    if (m_capturing) return;
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
    m_multiple = multiple;
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
        connect(selector, &RegionSelector::multipleRequested, this, [this] {
            m_multiple = true; updateSelections();
        });
        connect(selector, &RegionSelector::regionAdded, this, [this, selector](const QRectF &area) {
            qint64 bytes = 0;
            for (const auto &selection : m_selections) bytes += selection.image.sizeInBytes();
            const QImage image = selector->crop(area);
            if (m_selections.size() >= 24 || image.isNull() || bytes + image.sizeInBytes() > 256 * 1024 * 1024) {
                selector->setNotice(QStringLiteral("Selection limit reached. Remove a region, or press Enter to arrange."));
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
    }
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
