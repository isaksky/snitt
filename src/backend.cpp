#include "backend.h"
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

Backend::Backend(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
        if (!m_capturing) return;
        if (state == QProcess::NormalExit && code == 0 && QFileInfo(m_output).size() > 0)
            emit captured(QUrl::fromLocalFile(m_output));
        else if (!m_process.readAllStandardError().trimmed().isEmpty())
            emit error(QStringLiteral("Capture did not complete. Check System Settings → Privacy & Security → Screen & System Audio Recording, then try again."));
        finish();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit this->error(QStringLiteral("Could not start the macOS screenshot tool."));
            finish();
        }
    });
}

void Backend::capture() {
    if (m_capturing) return;
#ifdef Q_OS_MACOS
    if (!m_temp.isValid()) {
        emit error(QStringLiteral("Could not create a temporary capture file."));
        emit captureFinished();
        return;
    }
    m_capturing = true;
    emit capturingChanged();
    m_output = m_temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
    // Hide the editor before the system snapshots the desktop.
    QTimer::singleShot(250, this, [this] {
        m_process.start(QStringLiteral("/usr/sbin/screencapture"),
                        {"-i", "-s", "-x", "-t", "png", m_output});
    });
#else
    emit error(QStringLiteral("Screen capture currently supports macOS. Open or paste an image to edit it here."));
    emit captureFinished();
#endif
}

void Backend::finish() {
    QFile::remove(m_output);
    m_capturing = false;
    emit capturingChanged();
    emit captureFinished();
}
