#include "videorecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#endif

QString recording::toolPath(const QString &tool) {
    const QString found = QStandardPaths::findExecutable(tool);
    if (!found.isEmpty()) return found;
    // Login agents and GUI launches often have a smaller PATH than a terminal.
    QStringList paths;
#ifdef Q_OS_MACOS
    paths = {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")};
#elif defined(Q_OS_WIN)
    paths = {QDir::homePath() + "/scoop/shims",
             QDir::homePath() + "/scoop/apps/ffmpeg/current/bin",
             QCoreApplication::applicationDirPath()};
    const QString scoop = qEnvironmentVariable("SCOOP");
    if (!scoop.isEmpty()) paths.prepend(scoop + "/shims");
#endif
    return QStandardPaths::findExecutable(tool, paths);
}

bool recording::revealSavedFile(const QString &path) {
    if (!QFileInfo(path).isFile()) return false;
#ifdef Q_OS_MACOS
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WIN)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE file = nullptr;
    // QStandardPaths returns forward slashes on Windows; the shell parser
    // requires a native path for SHParseDisplayName.
    const QString nativePath = QDir::toNativeSeparators(path);
    const HRESULT parsed = SHParseDisplayName(reinterpret_cast<LPCWSTR>(nativePath.utf16()), nullptr, &file, 0, nullptr);
    bool revealed = false;
    if (SUCCEEDED(parsed) && file) {
        PIDLIST_ABSOLUTE folder = ILCloneFull(file);
        if (folder && ILRemoveLastID(folder)) {
            LPCITEMIDLIST child = ILFindLastID(file);
            revealed = SUCCEEDED(SHOpenFolderAndSelectItems(folder, 1, &child, 0));
        }
        ILFree(folder);
    }
    ILFree(file);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return revealed;
#else
    return false;
#endif
}

QStringList recording::arguments(const Source &source, const QString &output) {
    QStringList args {"-hide_banner", "-loglevel", "warning", "-n", "-nostats",
                      "-progress", "pipe:1", "-stats_period", "0.05"};
    QString filter;
    if (source.platform == Platform::Windows) {
        args << "-f" << "gdigrab" << "-framerate" << "30" << "-draw_mouse" << "1"
             << "-offset_x" << QString::number(source.pixelRegion.x())
             << "-offset_y" << QString::number(source.pixelRegion.y())
             << "-video_size" << QStringLiteral("%1x%2").arg(source.pixelRegion.width()).arg(source.pixelRegion.height())
             << "-probesize" << "32" << "-analyzeduration" << "0"
             << "-i" << "desktop";
    } else {
        args << "-f" << "avfoundation" << "-framerate" << "30" << "-capture_cursor" << "1"
             << "-pixel_format" << "bgr0"
             << "-probesize" << "32" << "-analyzeduration" << "0"
             << "-i" << QStringLiteral("Capture screen %1:none").arg(source.screenIndex);
        const auto number = [](qreal n) { return QString::number(n, 'f', 10); };
        const QRectF r = source.relativeRegion;
        // Fractional coordinates remain correct when AVFoundation and the
        // screenshot picker use different Retina backing resolutions.
        filter = QStringLiteral("crop=round(iw*%1):round(ih*%2):round(iw*%3):round(ih*%4):exact=1,")
            .arg(number(r.width()), number(r.height()), number(r.x()), number(r.y()));
    }
    // Pad odd dimensions instead of trimming pixels off the selected region.
    filter += "pad=ceil(iw/2)*2:ceil(ih/2)*2";
    args << "-an" << "-vf" << filter << "-r" << "30" << "-c:v" << "libx264" << "-preset" << "veryfast"
         << "-crf" << "18" << "-pix_fmt" << "yuv420p"
         << "-movflags" << "+frag_keyframe+empty_moov+default_base_moof" << "-g" << "30" << output;
    return args;
}

VideoRecorder::VideoRecorder(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        const QString message = m_finishing ? QStringLiteral("FFmpeg did not finish saving the recording.")
                                           : QStringLiteral("FFmpeg did not receive a screen frame. Check screen-recording permission, then try again.");
        m_process.kill();
        fail(message);
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError();
        if (m_stderr.size() > 16384) m_stderr = m_stderr.right(16384);
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &VideoRecorder::consumeProgress);
    connect(&m_process, &QProcess::started, this, &VideoRecorder::processStarted);
    connect(&m_process, &QProcess::finished, this, &VideoRecorder::complete);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError reason) {
        if (reason == QProcess::FailedToStart && m_active)
            fail(QStringLiteral("Could not start FFmpeg: %1").arg(m_process.errorString()));
    });
}

VideoRecorder::~VideoRecorder() {
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        // Preserve an in-progress clip if the resident app is quit or updated.
        m_process.write("q\n");
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
}

void VideoRecorder::start(const recording::Source &source) {
    if (m_active || m_process.state() != QProcess::NotRunning) return;
    const QString executable = recording::toolPath("ffmpeg");
    if (executable.isEmpty()) {
#ifdef Q_OS_WIN
        emit error(QStringLiteral("Recording needs FFmpeg. Install it with ‘scoop install ffmpeg’, then try again."));
#else
        emit error(QStringLiteral("Recording needs FFmpeg. Install it with ‘brew install ffmpeg’, then try again."));
#endif
        return;
    }
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString directory = QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath("xshot");
    if (!QDir().mkpath(directory)) {
        emit error(QStringLiteral("Could not create the recordings folder: %1").arg(directory));
        return;
    }
    m_output = QDir(directory).filePath(QStringLiteral("xshot-%1-%2.mp4")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
             QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    m_stderr.clear();
    m_progress.clear();
    m_firstFrame = false;
    m_finishing = false;
    m_canceling = false;
    m_elapsed = 0;
    m_active = true;
    emit elapsedChanged();
    emit changed();
    m_timeout.start(15000);
    m_process.start(executable, recording::arguments(source, m_output));
}

void VideoRecorder::finish() {
    if (!m_active || m_finishing) return;
    m_finishing = true;
    emit changed();
    // q lets FFmpeg drain the encoder and finish MP4 metadata.
    m_process.write("q\n");
    m_timeout.start(15000);
}

void VideoRecorder::cancel() {
    if (!m_active || m_canceling) return;
    m_canceling = true;
    m_finishing = true;
    emit changed();
    m_process.kill();
    m_timeout.start(3000);
}

void VideoRecorder::consumeProgress() {
    m_progress += m_process.readAllStandardOutput();
    int newline;
    while ((newline = m_progress.indexOf('\n')) >= 0) {
        const QByteArray line = m_progress.left(newline).trimmed();
        m_progress.remove(0, newline + 1);
        if (line.startsWith("frame=")) {
            const bool hasFrame = line.mid(6).trimmed().toLongLong() > 0;
            if (hasFrame && !m_firstFrame) {
                m_firstFrame = true;
                if (!m_finishing) m_timeout.stop();
                emit changed();
                emit ready();
            }
        } else if (line.startsWith("out_time_us=")) {
            const int seconds = qMax<qint64>(0, line.mid(12).toLongLong() / 1000000);
            if (seconds != m_elapsed) { m_elapsed = seconds; emit elapsedChanged(); }
        }
    }
}

void VideoRecorder::complete(int code, QProcess::ExitStatus status) {
    if (!m_active || m_discarding) return;
    consumeProgress();
    m_stderr += m_process.readAllStandardError();
    if (m_canceling) {
        m_timeout.stop();
        m_discarding = true;
        m_discardAttempts = 0;
        discardCanceledOutput();
    } else if (status == QProcess::NormalExit && code == 0 && m_firstFrame && QFileInfo(m_output).size() > 0) {
        const QString output = m_output;
        reset();
        emit saved(output);
    } else {
        QString message = QStringLiteral("FFmpeg could not save the recording.");
        const QString detail = QString::fromUtf8(m_stderr).trimmed();
        if (!detail.isEmpty()) message += "\n\n" + detail.right(2000);
        fail(message);
    }
}

void VideoRecorder::fail(const QString &message) {
    if (m_discarding) return;
    QString report = message;
    if (m_canceling) {
        m_timeout.stop();
        m_discarding = true;
        m_discardAttempts = 0;
        discardCanceledOutput();
        return;
    }
    if (m_firstFrame && QFileInfo(m_output).size() > 0)
        report += QStringLiteral("\n\nThe partial recording was kept at %1").arg(QDir::toNativeSeparators(m_output));
    else QFile::remove(m_output);
    reset();
    emit error(report);
}

void VideoRecorder::discardCanceledOutput() {
    if (!m_discarding) return;
    if (!QFileInfo::exists(m_output) || QFile::remove(m_output)) {
        m_discarding = false;
        reset();
        emit canceled();
    } else if (++m_discardAttempts < 60) {
        // Windows may still hold FFmpeg's output briefly after QProcess has
        // reported exit. Do not claim cancellation until the file is gone.
        QTimer::singleShot(50, this, &VideoRecorder::discardCanceledOutput);
    } else {
        const QString path = m_output;
        m_discarding = false;
        reset();
        emit error(QStringLiteral("Could not discard canceled recording at %1")
                       .arg(QDir::toNativeSeparators(path)));
    }
}

void VideoRecorder::reset() {
    m_timeout.stop();
    m_active = false;
    m_finishing = false;
    emit changed();
}
