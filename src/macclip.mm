#include "macclip.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#include <QFileInfo>
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

MacClipExport::MacClipExport() = default;
MacClipExport::~MacClipExport() {
    if (m_poll) { m_poll->stop(); m_poll->deleteLater(); m_poll = nullptr; }
    cancel();
}

void MacClipExport::start(const QString &input, const QString &output, qint64 startMs, qint64 endMs,
                          QObject *receiver, std::function<void(double)> progress,
                          std::function<void(bool, const QString &)> done) {
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
    QObject::connect(m_poll, &QTimer::timeout, receiver, [this, session, guard, progress, done] {
        const auto status = session.status;
        if (status != AVAssetExportSessionStatusCompleted && status != AVAssetExportSessionStatusFailed
            && status != AVAssetExportSessionStatusCancelled) {
            // AVFoundation's native fraction is the only determinate value we
            // report. Its initial zero is not evidence of actual progress.
            const double fraction = session.progress;
            if (guard && std::isfinite(fraction) && fraction >= 0.01 && fraction < 1.0)
                progress(fraction);
            return;
        }
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
