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

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
        if (!m_capturing) return;
        const bool success = state == QProcess::NormalExit && code == 0 && QFileInfo(m_output).size() > 0;
        if (success)
            emit captured(QUrl::fromLocalFile(m_output));
        else if (!m_process.readAllStandardError().trimmed().isEmpty())
            emit error(QStringLiteral("Capture did not complete. Check System Settings → Privacy & Security → Screen & System Audio Recording, then try again."));
        finish(success);
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

void Backend::capture() {
    if (m_capturing) return;
    if (!m_temp.isValid()) {
        emit error(QStringLiteral("Could not create a temporary capture file."));
        emit captureFinished(false);
        return;
    }
    m_capturing = true;
    emit capturingChanged();
    m_output = m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
#ifdef Q_OS_MACOS
    // Hide the editor before the system snapshots the desktop.
    QTimer::singleShot(250, this, [this] {
        m_process.start(QStringLiteral("/usr/sbin/screencapture"),
                        {"-i", "-s", "-x", "-t", "png", m_output});
    });
#elif defined(Q_OS_WIN)
    QTimer::singleShot(200, this, [this] {
        // Freeze every monitor before showing any overlay, so none is captured.
        QPointer<RegionSelector> active;
        for (QScreen *screen : QGuiApplication::screens()) {
            const QImage image = screen->grabWindow(0).toImage();
            if (image.isNull()) continue;
            auto *selector = new RegionSelector(image, screen->geometry());
            m_selectors.append(selector);
            if (screen->geometry().contains(QCursor::pos())) active = selector;
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
        }
        if (m_selectors.isEmpty()) {
            emit error(QStringLiteral("Could not capture a screen. Is the Windows desktop unlocked?"));
            finish();
            return;
        }
        for (auto selector : m_selectors) selector->show();
        if (!active) active = m_selectors.first();
        active->raise();
        active->activateWindow();
        active->setFocus();
    });
#else
    emit error(QStringLiteral("Screen capture supports macOS and Windows. Open or paste an image to edit it here."));
    finish();
#endif
}

void Backend::finish(bool captured) {
    for (auto selector : m_selectors) {
        if (!selector) continue;
        selector->disconnect(this);
        selector->hide();
        selector->deleteLater();
    }
    m_selectors.clear();
    QFile::remove(m_output);
    m_capturing = false;
    emit capturingChanged();
    emit captureFinished(captured);
}
