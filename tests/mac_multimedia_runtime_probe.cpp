#include <QAudioOutput>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QDebug>

// A fresh-process probe for the bundled Mac playback runtime. Run against a
// real MP4 and optionally require its AAC track with --audio.
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2 || !QFileInfo::exists(QString::fromLocal8Bit(argv[1]))) {
        qCritical("Usage: mac_multimedia_runtime_probe <video.mp4> [--audio]");
        return 2;
    }
    const bool expectAudio = app.arguments().contains("--audio");
    QMediaPlayer player;
    QVideoSink sink;
    QAudioOutput audio;
    audio.setMuted(true);
    player.setVideoSink(&sink);
    player.setAudioOutput(&audio);
    QElapsedTimer clock;
    bool firstFrame = false;
    bool seeking = false;
    qint64 seekTarget = 0;

    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
                     [&](QMediaPlayer::Error, const QString &message) {
        qCritical() << "Playback error:" << message;
        app.exit(3);
    });
    QObject::connect(&player, &QMediaPlayer::durationChanged, &app,
                     [&](qint64 duration) {
        if (duration <= 0 || firstFrame) return;
        if (expectAudio && player.audioTracks().isEmpty()) {
            qCritical("Expected audio track is missing");
            app.exit(4);
            return;
        }
        qInfo() << "media_ready_ms" << clock.elapsed() << "duration_ms" << duration
                << "audio_tracks" << player.audioTracks().size();
        player.play();
    });
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &app,
                     [&](const QVideoFrame &frame) {
        if (!frame.isValid()) return;
        if (!firstFrame) {
            firstFrame = true;
            qInfo() << "first_frame_ms" << clock.elapsed() << "size" << frame.size();
            if (!player.isSeekable() || player.duration() < 1000) {
                qCritical("Media is not seekable");
                app.exit(5);
                return;
            }
            seekTarget = player.duration() / 2;
            seeking = true;
            QTimer::singleShot(0, &app, [&] { player.setPosition(seekTarget); });
        } else if (seeking && frame.startTime() >= (seekTarget - 200) * 1000) {
            qInfo() << "seek_frame_ms" << clock.elapsed() << "frame_pts_us"
                    << frame.startTime() << "target_ms" << seekTarget;
            player.pause();
            app.exit(0);
        }
    });
    QTimer::singleShot(15000, &app, [&] {
        qCritical("Playback/seek timeout");
        app.exit(6);
    });
    clock.start();
    player.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    return app.exec();
}
