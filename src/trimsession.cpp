#include "trimsession.h"

#include "videorecorder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QUuid>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_MACOS
#include "macclip.h"
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr int thumbnailCount = 12;
#ifdef Q_OS_WIN
QString timestamp(qint64 ms) { return QString::number(double(ms) / 1000.0, 'f', 6); }
#endif

struct ClipInfo { qint64 duration = 0; int width = 0; int height = 0; };

ClipInfo parseProbe(const QByteArray &json) {
    const auto root = QJsonDocument::fromJson(json).object();
    const auto streams = root.value("streams").toArray();
    if (streams.isEmpty()) return {};
    const auto stream = streams.first().toObject();
    QString duration = stream.value("duration").toString();
    if (duration.isEmpty()) duration = root.value("format").toObject().value("duration").toString();
    return {qRound64(duration.toDouble() * 1000), stream.value("width").toInt(), stream.value("height").toInt()};
}

bool validClip(const ClipInfo &info) { return info.duration > 0 && info.width > 0 && info.height > 0; }
}

TrimSession::TrimSession(QObject *parent) : QObject(parent) {
    connect(&m_thumbProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const int index = m_nextThumb++;
        if (!m_thumbDir || m_path.isEmpty() || index < 0 || index >= m_thumbnails.size()) return;
        const QString file = m_thumbDir->filePath(QString::number(index) + ".jpg");
        if (status == QProcess::NormalExit && code == 0 && QFileInfo(file).size() > 0)
            m_thumbnails[index] = QUrl::fromLocalFile(file).toString();
        emit changed();
        nextWindowsThumbnail();
    });
    connect(&m_exportProcess, &QProcess::readyReadStandardOutput, this, [this] {
        m_progressBuffer += m_exportProcess.readAllStandardOutput();
        int newline;
        while ((newline = m_progressBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_progressBuffer.left(newline).trimmed();
            m_progressBuffer.remove(0, newline + 1);
            if (line.startsWith("out_time_us=")) {
                const qint64 elapsed = line.mid(12).toLongLong() / 1000;
                m_progress = qBound(0.0, double(elapsed) / qMax<qint64>(1, m_endMs - m_startMs), 1.0);
                emit changed();
            }
        }
    });
    connect(&m_exportProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_busy) return;
        if (m_canceling) {
            removeTemporaryOutput();
            m_busy = m_canceling = false;
            m_progress = 0;
            emit changed();
            resumeThumbnails();
            return;
        }
        exportFinished(status == QProcess::NormalExit && code == 0,
                       QString::fromUtf8(m_exportProcess.readAllStandardError()).trimmed());
    });
    connect(&m_exportProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_busy)
            exportFinished(false, m_exportProcess.errorString());
    });
    connect(&m_probeProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QByteArray result = m_probeProcess.readAllStandardOutput();
        const ClipInfo info = parseProbe(result);
        if (status != QProcess::NormalExit || code != 0 || !validClip(info)) {
            fail(QStringLiteral("The trimmed MP4 could not be verified. Your original recording is unchanged."));
            return;
        }
        if (info.duration < m_endMs - m_startMs - 100 || info.duration > m_endMs - m_startMs + 150) {
            fail(QStringLiteral("The trimmed MP4 duration did not match the selected range. Your original recording is unchanged."));
            return;
        }
        replaceOutput();
    });
    connect(&m_probeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_busy)
            fail(QStringLiteral("Could not verify the trimmed MP4. Your original recording is unchanged."));
    });
}

TrimSession::~TrimSession() {
    ++m_generation;
#ifdef Q_OS_MACOS
    m_macExport.reset();
#endif
    for (QProcess *process : {&m_exportProcess, &m_probeProcess, &m_thumbProcess}) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(3000);
        }
    }
    removeTemporaryOutput();
}

void TrimSession::open(const QString &path) {
    if (m_busy || !m_path.isEmpty()) return;
    if (!QFileInfo(path).isFile()) return;
    m_path = path;
    m_problem.clear();
    m_duration = 0;
    m_progress = 0;
    m_thumbnailStartMs = 0;
    m_thumbnailEndMs = 0;
    m_thumbnails = QStringList(thumbnailCount, QString());
    m_thumbDir = std::make_unique<QTemporaryDir>();
    ++m_generation;
#ifdef Q_OS_MACOS
    qint64 nativeDuration = 0;
    if (macProbeClip(path, &nativeDuration, nullptr, nullptr)) m_duration = nativeDuration;
#endif
    emit changed();
    if (m_duration > 0) generateThumbnails();
}

void TrimSession::setDuration(qint64 milliseconds) {
    if (m_path.isEmpty() || milliseconds <= 0) return;
    if (m_duration > 0) return;
    m_duration = milliseconds;
    emit changed();
    generateThumbnails();
}

void TrimSession::setThumbnailWindow(qint64 startMs, qint64 endMs) {
    if (m_path.isEmpty() || m_duration <= 0 || m_busy) return;
    const qint64 start = qBound<qint64>(0, startMs, m_duration - 1);
    const qint64 end = qBound(start + 1, endMs, m_duration);
    const qint64 currentEnd = m_thumbnailEndMs > 0 ? m_thumbnailEndMs : m_duration;
    if (start == m_thumbnailStartMs && end == currentEnd) return;
    ++m_generation;
    if (!stopThumbnails()) {
        m_thumbnails = QStringList(thumbnailCount, QString());
        m_problem = QStringLiteral("Could not refresh the filmstrip preview.");
        emit changed();
        return;
    }
    m_thumbnailStartMs = start;
    m_thumbnailEndMs = end;
    m_thumbnails = QStringList(thumbnailCount, QString());
    m_thumbDir = std::make_unique<QTemporaryDir>();
    emit changed();
    generateThumbnails();
}

void TrimSession::generateThumbnails() {
    if (!m_thumbDir || !m_thumbDir->isValid() || m_duration <= 0) return;
#ifdef Q_OS_MACOS
    macGenerateClipThumbnails(m_path, m_thumbDir->path(), m_thumbnailStartMs,
        m_thumbnailEndMs > 0 ? m_thumbnailEndMs : m_duration, this, m_generation,
        [this](int index, const QString &file, quint64 generation) {
            if (generation != m_generation || index < 0 || index >= m_thumbnails.size()) return;
            if (!file.isEmpty()) m_thumbnails[index] = QUrl::fromLocalFile(file).toString();
            emit changed();
        });
#else
    m_nextThumb = 0;
    nextWindowsThumbnail();
#endif
}

void TrimSession::nextWindowsThumbnail() {
#ifdef Q_OS_WIN
    while (m_nextThumb < m_thumbnails.size() && !m_thumbnails.at(m_nextThumb).isEmpty())
        ++m_nextThumb;
    if (m_path.isEmpty() || m_busy || m_nextThumb >= thumbnailCount || !m_thumbDir) return;
    const QString ffmpeg = recording::toolPath("ffmpeg");
    if (ffmpeg.isEmpty()) return;
    const qint64 windowEnd = m_thumbnailEndMs > 0 ? m_thumbnailEndMs : m_duration;
    const qint64 time = m_thumbnailStartMs
        + qRound64(double(windowEnd - m_thumbnailStartMs) * (m_nextThumb + 0.5) / thumbnailCount);
    m_thumbProcess.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y",
        "-ss", timestamp(time), "-i", m_path, "-frames:v", "1", "-vf", "scale=-1:90",
        "-vcodec", "mjpeg", m_thumbDir->filePath(QString::number(m_nextThumb) + ".jpg")});
#endif
}

bool TrimSession::stopThumbnails() {
#ifdef Q_OS_WIN
    m_nextThumb = thumbnailCount;
    // The finished callback normally launches the next frame. Block it while
    // stopping this worker so it cannot reopen the source during replacement.
    const QSignalBlocker blocked(&m_thumbProcess);
    if (m_thumbProcess.state() == QProcess::NotRunning) return true;
    m_thumbProcess.kill();
    return m_thumbProcess.waitForFinished(5000)
        || m_thumbProcess.state() == QProcess::NotRunning;
#else
    return true;
#endif
}

void TrimSession::resumeThumbnails() {
#ifdef Q_OS_WIN
    if (m_path.isEmpty() || m_busy || !m_thumbDir || !m_thumbDir->isValid()
        || m_thumbProcess.state() != QProcess::NotRunning) return;
    for (int index = 0; index < m_thumbnails.size(); ++index) {
        if (m_thumbnails.at(index).isEmpty()) {
            m_nextThumb = index;
            nextWindowsThumbnail();
            return;
        }
    }
#endif
}

void TrimSession::keepOriginal() {
    if (m_busy) { cancelExport(); return; }
    if (m_path.isEmpty()) return;
    const QString path = m_path;
    clear();
    emit finalized(path);
}

void TrimSession::exportRange(qint64 startMs, qint64 endMs) {
    if (m_path.isEmpty() || m_busy) return;
    if (m_duration <= 0 || startMs < 0 || endMs <= startMs || endMs > m_duration + 50
        || endMs - startMs < 100) {
        m_problem = QStringLiteral("Choose a trim range of at least 0.1 seconds.");
        emit changed();
        return;
    }
    if (startMs == 0 && endMs >= m_duration - 1) { keepOriginal(); return; }
    if (!stopThumbnails()) {
        m_problem = QStringLiteral("Could not stop the preview worker. Your original recording is unchanged.");
        emit changed();
        return;
    }
    m_startMs = startMs;
    m_endMs = endMs;
    m_problem.clear();
#ifdef Q_OS_MACOS
    m_progress = -1; // Native progress is unknown until AVFoundation reports a positive fraction.
#else
    m_progress = 0;
#endif
    m_canceling = false;
    m_busy = true;
    m_tempOutput = QFileInfo(m_path).dir().filePath(
        QStringLiteral(".xshot-trim-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    emit changed();
#ifdef Q_OS_MACOS
    m_macExport = std::make_unique<MacClipExport>();
    const quint64 generation = m_generation;
    m_macExport->start(m_path, m_tempOutput, startMs, endMs, this,
        [this, generation](double fraction) {
            if (generation != m_generation || !m_busy || m_canceling) return;
            const double bounded = qBound(0.01, fraction, 0.99);
            if (bounded <= m_progress) return;
            m_progress = bounded;
            emit changed();
        },
        [this, generation](bool success, const QString &detail) {
            if (generation != m_generation || !m_busy) return;
            if (m_canceling) {
                removeTemporaryOutput();
                m_macExport.reset();
                m_busy = m_canceling = false;
                m_progress = 0;
                emit changed();
                return;
            }
            m_macExport.reset();
            exportFinished(success, detail);
        });
#else
    const QString ffmpeg = recording::toolPath("ffmpeg");
    if (ffmpeg.isEmpty()) {
        fail(QStringLiteral("FFmpeg is unavailable for trimming. Your original recording is unchanged."));
        return;
    }
    m_progressBuffer.clear();
    m_exportProcess.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y",
        "-progress", "pipe:1", "-ss", timestamp(startMs), "-i", m_path,
        "-t", timestamp(endMs - startMs), "-c:v", "libx264", "-preset", "veryfast",
        "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "aac",
        "-movflags", "+faststart", m_tempOutput});
#endif
}

void TrimSession::cancelExport() {
    if (!m_busy || m_canceling) return;
    m_canceling = true;
#ifdef Q_OS_MACOS
    if (m_macExport) m_macExport->cancel();
#else
    if (m_exportProcess.state() != QProcess::NotRunning) m_exportProcess.kill();
    else if (m_probeProcess.state() != QProcess::NotRunning) {
        const QSignalBlocker blocked(&m_probeProcess);
        m_probeProcess.kill();
        m_probeProcess.waitForFinished(3000);
        removeTemporaryOutput();
        m_busy = m_canceling = false;
        m_progress = 0;
    }
    else {
        removeTemporaryOutput();
        m_busy = m_canceling = false;
        m_progress = 0;
    }
#endif
    emit changed();
    resumeThumbnails();
}

void TrimSession::exportFinished(bool success, const QString &detail) {
    if (!success || QFileInfo(m_tempOutput).size() < 1024) {
        fail(QStringLiteral("Could not export the selected range%1. Your original recording is unchanged.")
             .arg(detail.isEmpty() ? QString() : QStringLiteral(": %1").arg(detail)));
        return;
    }
    validateOutput();
}

void TrimSession::validateOutput() {
#ifdef Q_OS_MACOS
    qint64 duration = 0;
    int width = 0, height = 0;
    if (!macProbeClip(m_tempOutput, &duration, &width, &height) || width < 1 || height < 1
        || duration < m_endMs - m_startMs - 100 || duration > m_endMs - m_startMs + 150) {
        fail(QStringLiteral("The trimmed MP4 could not be verified. Your original recording is unchanged."));
        return;
    }
    replaceOutput();
#else
    const QString ffprobe = recording::toolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        fail(QStringLiteral("FFprobe is unavailable. Your original recording is unchanged."));
        return;
    }
    m_probeProcess.start(ffprobe, {"-v", "error", "-print_format", "json", "-show_format",
        "-show_streams", "-select_streams", "v:0", m_tempOutput});
#endif
}

void TrimSession::replaceOutput() {
    if (!stopThumbnails()) {
        fail(QStringLiteral("Could not stop the preview worker. Your original recording is unchanged."));
        return;
    }
    QString replacementProblem;
    if (!replaceFile(m_tempOutput, m_path, &replacementProblem)) {
        fail(QStringLiteral("Could not replace the original recording. %1")
             .arg(replacementProblem.isEmpty()
                  ? QStringLiteral("The full recording remains unchanged.") : replacementProblem));
        return;
    }
    const QString path = m_path;
    clear();
    emit finalized(path);
}

bool TrimSession::replaceFile(const QString &temporary, const QString &original, QString *problem) {
#ifdef Q_OS_WIN
    const QString backup = QFileInfo(original).dir().filePath(
        QStringLiteral(".xshot-original-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const bool replaced = ReplaceFileW(reinterpret_cast<LPCWSTR>(original.utf16()),
                                       reinterpret_cast<LPCWSTR>(temporary.utf16()),
                                       reinterpret_cast<LPCWSTR>(backup.utf16()), 0, nullptr, nullptr) != FALSE;
    if (replaced) { QFile::remove(backup); return true; }
    if (QFileInfo(backup).isFile()) {
        // ReplaceFileW may move the original to backup before reporting failure.
        const bool restored = MoveFileExW(reinterpret_cast<LPCWSTR>(backup.utf16()),
                                          reinterpret_cast<LPCWSTR>(original.utf16()),
                                          MOVEFILE_REPLACE_EXISTING) != FALSE;
        if (!restored && CopyFileW(reinterpret_cast<LPCWSTR>(backup.utf16()),
                                   reinterpret_cast<LPCWSTR>(original.utf16()), FALSE)) QFile::remove(backup);
        if (!QFileInfo(original).isFile() && problem)
            *problem = QStringLiteral("The full recording is safe in backup file %1, but Windows could not restore its original path.")
                           .arg(QDir::toNativeSeparators(backup));
    }
    return false;
#else
    const QByteArray source = QFile::encodeName(temporary);
    const QByteArray destination = QFile::encodeName(original);
    if (std::rename(source.constData(), destination.constData()) == 0) return true;
    if (problem) *problem = QStringLiteral("The full recording remains unchanged (%1).")
                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
#endif
}

void TrimSession::fail(const QString &message) {
    removeTemporaryOutput();
    m_busy = m_canceling = false;
    m_progress = 0;
    m_problem = message;
    emit changed();
    resumeThumbnails();
}

void TrimSession::removeTemporaryOutput() {
    if (!m_tempOutput.isEmpty()) QFile::remove(m_tempOutput);
    m_tempOutput.clear();
}

void TrimSession::clear() {
    ++m_generation;
    stopThumbnails();
    removeTemporaryOutput();
    m_thumbDir.reset();
    m_path.clear();
    m_thumbnails.clear();
    m_duration = 0;
    m_thumbnailStartMs = 0;
    m_thumbnailEndMs = 0;
    m_busy = false;
    m_canceling = false;
    m_problem.clear();
    m_progress = 0;
    emit changed();
}
