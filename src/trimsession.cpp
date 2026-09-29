#include "trimsession.h"

#include "mp4duration.h"
#include "videorecorder.h"
#include <QCoreApplication>
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

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr int thumbnailCount = 12;
QString timestamp(qint64 ms) { return QString::number(double(ms) / 1000.0, 'f', 6); }

QString mediaToolPath(const QString &tool) {
    // Prefer the bundled tools; developer builds and Windows use PATH/Scoop.
    const QString bundled = QCoreApplication::applicationDirPath() + "/" + tool + "-media";
    if (QFileInfo(bundled).isExecutable()) return bundled;
    return recording::toolPath(tool);
}

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
    connect(&m_sourceProbe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const ClipInfo info = parseProbe(m_sourceProbe.readAllStandardOutput());
        if (!m_busy && status == QProcess::NormalExit && code == 0 && validClip(info))
            setDuration(info.duration);
    });
    connect(&m_thumbProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const int index = m_nextThumb;
        if (!m_thumbDir || m_path.isEmpty() || index < 0 || index >= m_thumbnails.size()) return;
        const QString file = m_thumbDir->filePath(QString::number(index) + ".jpg");
        const bool decoded = status == QProcess::NormalExit && code == 0 && QFileInfo(file).size() > 0;
        if (!decoded && m_thumbnailLookbackMs == 0 && status == QProcess::NormalExit
            && QFileInfo(file).size() == 0) {
            const QByteArray error = m_thumbProcess.readAllStandardError();
            // FFmpeg reports this empty-tail condition as 234 on macOS and
            // -22 on Windows; its diagnostic is the stable distinction from
            // an unrelated decode or output failure.
            m_thumbnailReachedEof = error.contains("before EOF")
                && error.contains("Nothing was written into output file");
        }
        if (decoded) {
            m_thumbnails[index] = QUrl::fromLocalFile(file).toString();
            if (m_thumbnailReachedEof) {
                // A direct seek found no frame from this sample to EOF. The
                // recovered final frame covers every later sample in this
                // generation, so publishing its same file is exact and avoids
                // redundant decode attempts on short recordings.
                for (int later = index + 1; later < m_thumbnails.size(); ++later)
                    m_thumbnails[later] = m_thumbnails[index];
            }
        } else if (retryThumbnailBeforeEof())
            return;
        else if (m_problem.isEmpty())
            m_problem = QStringLiteral("Some filmstrip frames could not be decoded.");
        ++m_nextThumb;
        emit changed();
        nextThumbnail();
    });
    connect(&m_thumbProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_path.isEmpty() || m_busy) return;
        m_nextThumb = thumbnailCount;
        m_problem = QStringLiteral("Could not start the thumbnail decoder.");
        emit changed();
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
        // The edit list bounds presentation even when stream copy retains
        // reference frames beyond the end. Reject missing media or bad timing.
        if (info.duration < m_endMs - m_startMs - 100 || info.duration > m_endMs - m_startMs + 2) {
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
    for (QProcess *process : {&m_exportProcess, &m_probeProcess, &m_sourceProbe, &m_thumbProcess}) {
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
    emit changed();
    // Read metadata asynchronously so filmstrip startup doesn't wait for the player.
    const QString probe = mediaToolPath("ffprobe");
    if (!probe.isEmpty())
        m_sourceProbe.start(probe, {"-v", "error", "-print_format", "json", "-show_format",
            "-show_streams", "-select_streams", "v:0", m_path});
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
    if (!stopThumbnails()) {
        m_thumbnails = QStringList(thumbnailCount, QString());
        m_problem = QStringLiteral("Could not refresh the filmstrip preview.");
        emit changed();
        return;
    }
    m_thumbnailStartMs = start;
    m_thumbnailEndMs = end;
    m_problem.clear();
    m_thumbnails = QStringList(thumbnailCount, QString());
    m_thumbDir = std::make_unique<QTemporaryDir>();
    emit changed();
    generateThumbnails();
}

void TrimSession::generateThumbnails() {
    if (!m_thumbDir || !m_thumbDir->isValid() || m_duration <= 0) return;
    m_nextThumb = 0;
    m_thumbnailLookbackMs = 0;
    m_thumbnailReachedEof = false;
    nextThumbnail();
}

void TrimSession::nextThumbnail() {
    while (m_nextThumb < m_thumbnails.size() && !m_thumbnails.at(m_nextThumb).isEmpty())
        ++m_nextThumb;
    if (m_path.isEmpty() || m_busy || m_nextThumb >= thumbnailCount || !m_thumbDir) return;
    const QString ffmpeg = mediaToolPath("ffmpeg");
    if (ffmpeg.isEmpty()) {
        m_problem = QStringLiteral("The thumbnail decoder is unavailable.");
        emit changed();
        return;
    }
    m_thumbnailLookbackMs = 0;
    m_thumbnailReachedEof = false;
    startThumbnailAttempt();
}

void TrimSession::startThumbnailAttempt() {
    const qint64 windowEnd = m_thumbnailEndMs > 0 ? m_thumbnailEndMs : m_duration;
    const qint64 time = m_thumbnailStartMs
        + qRound64(double(windowEnd - m_thumbnailStartMs) * (m_nextThumb + 0.5) / thumbnailCount);
    QStringList args {"-hide_banner", "-loglevel", "error", "-y"};
    if (m_thumbnailLookbackMs > 0) {
        // Accurate input seeking normally returns the first frame at or after
        // time. At EOF that can be empty even while the final frame is still
        // displayed. Decode a bounded preceding interval in reverse to choose
        // the latest frame at or before time, widening only when necessary for
        // sparse/VFR clips. The ordinary fast path remains one direct seek.
        const qint64 start = qMax<qint64>(0, time - m_thumbnailLookbackMs);
        args << "-ss" << timestamp(start) << "-t" << timestamp(qMax<qint64>(1, time - start));
    } else {
        args << "-ss" << timestamp(time);
    }
    args << "-i" << m_path << "-frames:v" << "1" << "-vf"
         << (m_thumbnailLookbackMs > 0 ? "reverse,scale=-1:90" : "scale=-1:90")
         << "-vcodec" << "mjpeg"
         << m_thumbDir->filePath(QString::number(m_nextThumb) + ".jpg");
    m_thumbProcess.start(mediaToolPath("ffmpeg"), args);
}

bool TrimSession::retryThumbnailBeforeEof() {
    const qint64 windowEnd = m_thumbnailEndMs > 0 ? m_thumbnailEndMs : m_duration;
    // A verified empty tail means no frame exists at or after this sample.
    // The preceding frame can cover it even when the zoom ends before the
    // container duration; the window boundary says nothing about frame PTS.
    if (!m_thumbnailReachedEof || m_nextThumb < 0 || m_nextThumb >= thumbnailCount)
        return false;
    const qint64 time = m_thumbnailStartMs
        + qRound64(double(windowEnd - m_thumbnailStartMs) * (m_nextThumb + 0.5) / thumbnailCount);
    if (m_thumbnailLookbackMs > 0 && m_thumbnailLookbackMs >= time) return false;
    m_thumbnailLookbackMs = m_thumbnailLookbackMs == 0 ? qMin<qint64>(2000, qMax<qint64>(1, time))
        : qMin<qint64>(time, m_thumbnailLookbackMs * 2);
    QFile::remove(m_thumbDir->filePath(QString::number(m_nextThumb) + ".jpg"));
    startThumbnailAttempt();
    return true;
}

bool TrimSession::stopThumbnails() {
    // Both preview workers must release the source before replacement/closing.
    const QSignalBlocker sourceBlocked(&m_sourceProbe);
    if (m_sourceProbe.state() != QProcess::NotRunning) {
        m_sourceProbe.kill();
        if (!m_sourceProbe.waitForFinished(3000) && m_sourceProbe.state() != QProcess::NotRunning)
            return false;
    }
    m_nextThumb = thumbnailCount;
    // The finished callback normally launches the next frame. Block it while
    // stopping this worker so it cannot reopen the source during replacement.
    const QSignalBlocker blocked(&m_thumbProcess);
    if (m_thumbProcess.state() == QProcess::NotRunning) return true;
    m_thumbProcess.kill();
    return m_thumbProcess.waitForFinished(5000)
        || m_thumbProcess.state() == QProcess::NotRunning;
}

void TrimSession::resumeThumbnails() {
    if (m_path.isEmpty() || m_busy || !m_thumbDir || !m_thumbDir->isValid()
        || m_thumbProcess.state() != QProcess::NotRunning) return;
    for (int index = 0; index < m_thumbnails.size(); ++index) {
        if (m_thumbnails.at(index).isEmpty()) {
            m_nextThumb = index;
            nextThumbnail();
            return;
        }
    }
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
    m_progress = 0;
    m_canceling = false;
    m_busy = true;
    m_tempOutput = QFileInfo(m_path).dir().filePath(
        QStringLiteral(".snitt-trim-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    emit changed();
    const QString ffmpeg = mediaToolPath("ffmpeg");
    if (ffmpeg.isEmpty()) {
        fail(QStringLiteral("FFmpeg is unavailable for trimming. Your original recording is unchanged."));
        return;
    }
    m_progressBuffer.clear();
    // Input seeking retains required keyframe preroll. MP4 edit lists hide it
    // during playback. Bound the tail's edit duration after muxing, since the
    // decode-order cutoff can retain much later presentation frames on VFR video.
    m_exportProcess.start(ffmpeg, {"-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-progress", "pipe:1", "-ss", timestamp(startMs), "-i", m_path,
        "-t", timestamp(endMs - startMs), "-map", "0:v:0", "-map", "0:a?",
        "-c", "copy",
        "-use_editlist", "1", "-movflags", "+faststart", m_tempOutput});
}

void TrimSession::cancelExport() {
    if (!m_busy || m_canceling) return;
    m_canceling = true;
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
    emit changed();
    resumeThumbnails();
}

void TrimSession::exportFinished(bool success, const QString &detail) {
    if (!success || QFileInfo(m_tempOutput).size() < 1024) {
        fail(QStringLiteral("Could not export the selected range%1. Your original recording is unchanged.")
             .arg(detail.isEmpty() ? QString() : QStringLiteral(": %1").arg(detail)));
        return;
    }
    if (!limitMp4Duration(m_tempOutput, m_endMs - m_startMs)) {
        fail(QStringLiteral("Could not set the trimmed MP4 playback range. Your original recording is unchanged."));
        return;
    }
    validateOutput();
}

void TrimSession::validateOutput() {
    const QString ffprobe = mediaToolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        fail(QStringLiteral("FFprobe is unavailable. Your original recording is unchanged."));
        return;
    }
    m_probeProcess.start(ffprobe, {"-v", "error", "-print_format", "json", "-show_format",
        "-show_streams", "-select_streams", "v:0", m_tempOutput});
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
        QStringLiteral(".snitt-original-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
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
