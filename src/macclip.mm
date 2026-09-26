#include "macclip.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <cmath>

bool macProbeClip(const QString &path, qint64 *durationMs, int *width, int *height) {
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
    const CMTime time = asset.duration;
    const double seconds = CMTimeGetSeconds(time);
    NSArray<AVAssetTrack *> *tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    if (!std::isfinite(seconds) || seconds <= 0 || tracks.count == 0) return false;
    const CGSize size = tracks.firstObject.naturalSize;
    if (size.width < 1 || size.height < 1) return false;
    if (durationMs) *durationMs = qRound64(seconds * 1000);
    if (width) *width = qRound(size.width);
    if (height) *height = qRound(size.height);
    return true;
}

void macGenerateClipThumbnails(const QString &path, const QString &directory, qint64 durationMs,
                               QObject *receiver, quint64 generation,
                               std::function<void(int, const QString &, quint64)> ready) {
    QPointer<QObject> guard(receiver);
    const QString clipPath = path;
    const QString targetDirectory = directory;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:clipPath.toNSString()] options:nil];
            AVAssetImageGenerator *generator = [[AVAssetImageGenerator alloc] initWithAsset:asset];
            generator.appliesPreferredTrackTransform = YES;
            generator.maximumSize = CGSizeMake(180, 110);
            // Filmstrip samples may use the nearest decoded frame. Exact edge
            // positions are checked on export, while zero tolerance here can
            // force a long full decode for every thumbnail.
            generator.requestedTimeToleranceBefore = CMTimeMake(1, 5);
            generator.requestedTimeToleranceAfter = CMTimeMake(1, 5);
            for (int index = 0; index < 12; ++index) {
                const double seconds = double(durationMs) * (index + 0.5) / 12000.0;
                NSError *error = nil;
                CGImageRef frame = [generator copyCGImageAtTime:CMTimeMakeWithSeconds(seconds, 600)
                                                        actualTime:nil error:&error];
                QString filename;
                if (frame) {
                    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithCGImage:frame];
                    NSData *jpeg = [bitmap representationUsingType:NSBitmapImageFileTypeJPEG
                                                        properties:@{NSImageCompressionFactor: @0.8}];
                    filename = QStringLiteral("%1/%2.jpg").arg(targetDirectory).arg(index);
                    if (![jpeg writeToFile:filename.toNSString() atomically:YES]) filename.clear();
                    CGImageRelease(frame);
                }
                Q_UNUSED(error);
                QMetaObject::invokeMethod(QCoreApplication::instance(), [=] {
                    if (guard) ready(index, filename, generation);
                }, Qt::QueuedConnection);
            }
        }
    });
}

MacClipExport::MacClipExport() = default;
MacClipExport::~MacClipExport() {
    if (m_poll) { m_poll->stop(); m_poll->deleteLater(); m_poll = nullptr; }
    cancel();
}

void MacClipExport::start(const QString &input, const QString &output, qint64 startMs, qint64 endMs,
                          QObject *receiver, std::function<void(bool, const QString &)> done) {
    cancel();
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:input.toNSString()] options:nil];
    AVAssetExportSession *session = [[AVAssetExportSession alloc] initWithAsset:asset
                                                                    presetName:AVAssetExportPresetHighestQuality];
    if (!session || ![session.supportedFileTypes containsObject:AVFileTypeMPEG4]) {
        done(false, QStringLiteral("This recording cannot be exported as MP4."));
        return;
    }
    session.outputURL = [NSURL fileURLWithPath:output.toNSString()];
    session.outputFileType = AVFileTypeMPEG4;
    session.shouldOptimizeForNetworkUse = YES;
    session.timeRange = CMTimeRangeMake(CMTimeMake(startMs, 1000), CMTimeMake(endMs - startMs, 1000));
    m_session = (__bridge_retained void *)session;
    QPointer<QObject> guard(receiver);
    m_poll = new QTimer(receiver);
    m_poll->setInterval(100);
    QObject::connect(m_poll, &QTimer::timeout, receiver, [this, session, guard, done] {
        const auto status = session.status;
        if (status != AVAssetExportSessionStatusCompleted && status != AVAssetExportSessionStatusFailed
            && status != AVAssetExportSessionStatusCancelled) return;
        m_poll->stop();
        if (guard) done(status == AVAssetExportSessionStatusCompleted,
                        session.error ? QString::fromNSString(session.error.localizedDescription) : QString());
    });
    m_poll->start();
    [session exportAsynchronouslyWithCompletionHandler:^{}];
}

void MacClipExport::cancel() {
    if (!m_session) return;
    AVAssetExportSession *session = (__bridge_transfer AVAssetExportSession *)m_session;
    m_session = nullptr;
    [session cancelExport];
}
