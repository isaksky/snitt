#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QDebug>

#include "trimsession.h"
#include "videorecorder.h"

// Run once per process to measure a cold thumbnail load, or pass --repeat to
// observe a warm reopen. Use the same completed MP4 on every implementation.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2 || !QFileInfo::exists(args.at(1))) {
        qCritical() << "Usage: trim_thumbnail_benchmark clip.mp4 [--repeat]";
        return 2;
    }
    const QString path = QFileInfo(args.at(1)).absoluteFilePath();
    const int runs = args.contains("--repeat") ? 2 : 1;
    for (int run = 0; run < runs; ++run) {
        TrimSession trim;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(45000);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QElapsedTimer timer;
        qint64 firstMs = -1, allMs = -1;
        QObject::connect(&trim, &TrimSession::changed, &loop, [&] {
            int ready = 0;
            for (const QString &url : trim.thumbnails()) {
                if (!url.isEmpty() && !QImage(QUrl(url).toLocalFile()).isNull()) ++ready;
            }
            if (ready > 0 && firstMs < 0) firstMs = timer.elapsed();
            if (ready == 12) { allMs = timer.elapsed(); loop.quit(); }
        });
        timer.start();
        trim.open(path);
        const qint64 openMs = timer.elapsed();
        if (allMs < 0) { timeout.start(); loop.exec(); }
        qInfo().noquote() << "clip=" + path << "run=" + QString::number(run)
                          << "open_ms=" + QString::number(openMs)
                          << "first_thumb_ms=" + QString::number(firstMs)
                          << "all_thumbs_ms=" + QString::number(allMs);
        if (firstMs < 0 || allMs < 0) return 1;
        trim.keepOriginal();
    }
    return 0;
}
