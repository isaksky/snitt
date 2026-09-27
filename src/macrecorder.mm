#include "macrecorder.h"
#include "savepaths.h"

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUuid>
#include <cmath>
#include <unistd.h>

@interface XShotCaptureSession : NSObject <SCStreamDelegate, SCStreamOutput, SCRecordingOutputDelegate>
@property(nonatomic, assign) MacRecorder *owner;
@property(nonatomic, assign) quint64 generation;
@property(nonatomic, strong) SCStream *stream;
@property(nonatomic, strong) SCRecordingOutput *recordingOutput;
@property(nonatomic, assign) BOOL stopping;
@property(nonatomic, assign) BOOL frameReported;
@property(nonatomic, copy) NSString *discardPath;
- (instancetype)initWithOwner:(MacRecorder *)owner generation:(quint64)generation;
- (void)requestStop;
- (void)discardFile;
@end

@implementation XShotCaptureSession
- (instancetype)initWithOwner:(MacRecorder *)owner generation:(quint64)generation {
    if ((self = [super init])) { _owner = owner; _generation = generation; }
    return self;
}

- (void)post:(void (^)(MacRecorder *))action {
    dispatch_async(dispatch_get_main_queue(), ^{
        MacRecorder *owner = self.owner;
        if (owner) action(owner);
    });
}

- (void)requestStop {
    if (_stopping) return;
    _stopping = YES;
    if (!_stream) {
        [self post:^(MacRecorder *owner) { owner->nativeStopped(self.generation, {}); }];
        return;
    }
    [_stream stopCaptureWithCompletionHandler:^(NSError *error) {
        [self discardFile];
        const QString detail = error ? QString::fromUtf8(error.localizedDescription.UTF8String) : QString();
        [self post:^(MacRecorder *owner) { owner->nativeStopped(self.generation, detail); }];
    }];
}

- (void)discardFile {
    if (!_discardPath) return;
    NSError *error = nil;
    if ([[NSFileManager defaultManager] fileExistsAtPath:_discardPath]
        && ![[NSFileManager defaultManager] removeItemAtPath:_discardPath error:&error])
        qWarning() << "Could not discard canceled recording at" << QString::fromNSString(_discardPath)
                   << QString::fromUtf8(error.localizedDescription.UTF8String);
}

- (void)dealloc { [self discardFile]; }

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        ofType:(SCStreamOutputType)type {
    Q_UNUSED(stream);
    if (_frameReported || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer)) return;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (!attachments || CFArrayGetCount(attachments) == 0) return;
    NSDictionary *frame = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = frame[SCStreamFrameInfoStatus];
    if (status.integerValue != SCFrameStatusComplete || !CMSampleBufferGetImageBuffer(sampleBuffer)) return;
    _frameReported = YES;
    [self post:^(MacRecorder *owner) { owner->nativeFrame(self.generation); }];
}

- (void)recordingOutputDidStartRecording:(SCRecordingOutput *)output {
    Q_UNUSED(output);
    [self post:^(MacRecorder *owner) { owner->nativeRecordingStarted(self.generation); }];
}

- (void)recordingOutputDidFinishRecording:(SCRecordingOutput *)output {
    Q_UNUSED(output);
    [self discardFile];
    [self post:^(MacRecorder *owner) { owner->nativeRecordingFinished(self.generation); }];
}

- (void)recordingOutput:(SCRecordingOutput *)output didFailWithError:(NSError *)error {
    Q_UNUSED(output);
    [self discardFile];
    const QString detail = QString::fromUtf8(error.localizedDescription.UTF8String);
    [self post:^(MacRecorder *owner) { owner->nativeFailed(self.generation, detail); }];
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    Q_UNUSED(stream);
    if ([error.domain isEqualToString:SCStreamErrorDomain] && error.code == SCStreamErrorUserStopped) {
        [self discardFile];
        [self post:^(MacRecorder *owner) { owner->nativeUserStopped(self.generation); }];
        return;
    }
    [self discardFile];
    const QString detail = QString::fromUtf8(error.localizedDescription.UTF8String);
    [self post:^(MacRecorder *owner) { owner->nativeFailed(self.generation, detail); }];
}
@end

static XShotCaptureSession *sessionFor(void *pointer) {
    return (__bridge XShotCaptureSession *)pointer;
}

MacRecorder::MacRecorder(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(m_finishing ? QStringLiteral("ScreenCaptureKit did not finish saving the recording.")
                         : QStringLiteral("ScreenCaptureKit did not provide a recorded screen frame. Check screen-recording permission, then try again."));
    });
    m_clock.setInterval(200);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (!m_ready || m_finishing) return;
        const int seconds = int(m_elapsedClock.elapsed() / 1000);
        if (seconds != m_elapsed) { m_elapsed = seconds; emit elapsedChanged(); }
    });
}

MacRecorder::~MacRecorder() {
    if (m_session) {
        XShotCaptureSession *session = sessionFor(m_session);
        session.owner = nullptr;
        [session requestStop];
        CFRelease(m_session);
    }
}

void MacRecorder::start(const recording::Source &source, const QString &saveRoot) {
    if (m_active) return;
    if (saveRoot.isEmpty()) {
        emit error(QStringLiteral("The configured video save folder is empty. Correct Save/videosRoot in xshot's settings file."));
        return;
    }
    const QDateTime startedAt = QDateTime::currentDateTime();
    const QString directory = savepaths::datedMediaDirectory(saveRoot, startedAt);
    if (!QDir().mkpath(directory)) {
        emit error(QStringLiteral("Could not create the recordings folder: %1").arg(directory));
        return;
    }
    m_output = QDir(directory).filePath(QStringLiteral("xshot-%1-%2.mp4")
        .arg(startedAt.toString("yyyyMMdd-HHmmss"),
             QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    m_source = source;
    m_active = true;
    m_ready = m_finishing = m_canceling = false;
    m_recordingStarted = m_firstFrame = m_recordingFinished = m_stopped = false;
    m_elapsed = 0;
    ++m_generation;
    auto *session = [[XShotCaptureSession alloc] initWithOwner:this generation:m_generation];
    m_session = (__bridge_retained void *)session;
    emit elapsedChanged();
    emit changed();
    m_timeout.start(15000);
    // Current process matching also works for the unbundled interactive tests.
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:YES
        completionHandler:^(SCShareableContent *content, NSError *error) {
            const QString detail = error ? QString::fromUtf8(error.localizedDescription.UTF8String) : QString();
            [session post:^(MacRecorder *owner) {
                owner->nativePrepared(session.generation, (__bridge void *)content, detail);
            }];
        }];
}

void MacRecorder::nativePrepared(quint64 generation, void *contentPointer, const QString &problem) {
    if (!current(generation)) return;
    if (!problem.isEmpty()) { fail(QStringLiteral("Could not prepare screen capture: %1").arg(problem)); return; }
    SCShareableContent *content = (__bridge SCShareableContent *)contentPointer;
    SCDisplay *display = nil;
    SCRunningApplication *ownApp = nil;
    for (SCDisplay *candidate in content.displays)
        if (candidate.displayID == m_source.displayId) { display = candidate; break; }
    for (SCRunningApplication *candidate in content.applications)
        if (candidate.processID == getpid()) { ownApp = candidate; break; }
    if (!display || !ownApp) {
        fail(QStringLiteral("Could not identify the selected display or exclude xshot's windows from the recording."));
        return;
    }
    const QRectF relative = m_source.relativeRegion;
    const CGRect sourceRect = CGRectMake(relative.x() * display.width, relative.y() * display.height,
                                        relative.width() * display.width, relative.height() * display.height);
    if (sourceRect.size.width < 1 || sourceRect.size.height < 1) {
        fail(QStringLiteral("The selected recording region is too small."));
        return;
    }
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
        excludingApplications:@[ownApp] exceptingWindows:@[]];
    SCStreamConfiguration *configuration = [SCStreamConfiguration new];
    configuration.sourceRect = sourceRect;
    const double scale = filter.pointPixelScale;
    configuration.width = qMax(2, int(std::ceil(sourceRect.size.width * scale / 2.0) * 2));
    configuration.height = qMax(2, int(std::ceil(sourceRect.size.height * scale / 2.0) * 2));
    configuration.minimumFrameInterval = CMTimeMake(1, 30);
    configuration.queueDepth = 3;
    configuration.showsCursor = YES;
    configuration.capturesAudio = NO;

    XShotCaptureSession *session = sessionFor(m_session);
    session.stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:session];
    SCRecordingOutputConfiguration *recordingConfig = [SCRecordingOutputConfiguration new];
    recordingConfig.outputURL = [NSURL fileURLWithPath:m_output.toNSString()];
    recordingConfig.videoCodecType = AVVideoCodecTypeH264;
    recordingConfig.outputFileType = AVFileTypeMPEG4;
    session.recordingOutput = [[SCRecordingOutput alloc] initWithConfiguration:recordingConfig delegate:session];
    NSError *nativeError = nil;
    // Apple guarantees the first captured sample is written when this is
    // attached before startCapture. No post-hoc trimming can offer that.
    if (![session.stream addRecordingOutput:session.recordingOutput error:&nativeError]
        || ![session.stream addStreamOutput:session type:SCStreamOutputTypeScreen
                      sampleHandlerQueue:dispatch_get_main_queue() error:&nativeError]) {
        fail(QStringLiteral("Could not configure screen recording: %1")
             .arg(QString::fromUtf8(nativeError.localizedDescription.UTF8String)));
        return;
    }
    emit processStarted();
    [session.stream startCaptureWithCompletionHandler:^(NSError *error) {
        const QString detail = error ? QString::fromUtf8(error.localizedDescription.UTF8String) : QString();
        [session post:^(MacRecorder *owner) { owner->nativeCaptureStarted(session.generation, detail); }];
    }];
}

void MacRecorder::nativeCaptureStarted(quint64 generation, const QString &problem) {
    if (!current(generation)) return;
    if (!problem.isEmpty()) fail(QStringLiteral("Could not start screen recording: %1").arg(problem));
}

void MacRecorder::nativeRecordingStarted(quint64 generation) {
    if (!current(generation)) return;
    m_recordingStarted = true;
    maybeReady();
}

void MacRecorder::nativeFrame(quint64 generation) {
    if (!current(generation)) return;
    m_firstFrame = true;
    maybeReady();
}

void MacRecorder::maybeReady() {
    if (m_ready || m_finishing || !m_recordingStarted || !m_firstFrame) return;
    m_ready = true;
    m_timeout.stop();
    m_elapsedClock.start();
    m_clock.start();
    emit changed();
    emit ready();
}

void MacRecorder::finish() {
    if (!m_active || m_finishing || !m_ready) return;
    m_finishing = true;
    emit changed();
    m_timeout.start(15000);
    [sessionFor(m_session) requestStop];
}

void MacRecorder::cancel() {
    if (!m_active || m_canceling) return;
    m_canceling = true;
    m_finishing = true;
    m_recordingFinished = true; // Cancellation discards output, even if encoder never started.
    emit changed();
    m_timeout.start(3000);
    XShotCaptureSession *session = sessionFor(m_session);
    session.discardPath = m_output.toNSString();
    if (session.stream) [session requestStop];
    else { m_stopped = true; maybeComplete(); }
}

void MacRecorder::nativeRecordingFinished(quint64 generation) {
    if (!current(generation)) return;
    m_recordingFinished = true;
    maybeComplete();
}

void MacRecorder::nativeStopped(quint64 generation, const QString &problem) {
    if (!current(generation)) return;
    m_stopped = true;
    if (!problem.isEmpty() && !m_canceling) {
        fail(QStringLiteral("Could not stop screen recording: %1").arg(problem));
        return;
    }
    maybeComplete();
}

void MacRecorder::nativeFailed(quint64 generation, const QString &problem) {
    if (!current(generation)) return;
    if (m_canceling) { m_recordingFinished = true; maybeComplete(); return; }
    fail(QStringLiteral("Screen recording failed: %1").arg(problem));
}

void MacRecorder::nativeUserStopped(quint64 generation) {
    if (current(generation)) cancel();
}

bool MacRecorder::discardOutput(const QString &path) {
    return !QFileInfo::exists(path) || QFile::remove(path);
}

void MacRecorder::maybeComplete() {
    if (!m_finishing || !m_stopped || !m_recordingFinished) return;
    if (m_canceling) {
        const QString path = m_output;
        reset();
        if (discardOutput(path)) emit canceled();
        else emit error(QStringLiteral("Could not discard canceled recording at %1")
                            .arg(QDir::toNativeSeparators(path)));
    } else if (m_firstFrame && QFileInfo(m_output).size() > 0) {
        const QString path = m_output;
        reset();
        emit saved(path);
    } else fail(QStringLiteral("ScreenCaptureKit did not save a video frame."));
}

void MacRecorder::fail(const QString &message) {
    if (!m_active) return;
    const QString path = m_output;
    const bool wasCanceling = m_canceling;
    const bool keepPartial = !wasCanceling && m_firstFrame && QFileInfo(path).size() > 0;
    XShotCaptureSession *session = sessionFor(m_session);
    if (session && session.stream) [session requestStop];
    reset();
    if (!keepPartial && !discardOutput(path)) {
        emit error(QStringLiteral("Could not discard recording at %1. %2")
                       .arg(QDir::toNativeSeparators(path), message));
        return;
    }
    if (wasCanceling) { emit canceled(); return; }
    emit error(keepPartial ? message + QStringLiteral("\n\nThe partial recording was kept at %1")
                                    .arg(QDir::toNativeSeparators(path)) : message);
}

void MacRecorder::reset() {
    m_timeout.stop();
    m_clock.stop();
    m_active = false;
    m_ready = false;
    m_finishing = false;
    if (m_session) {
        XShotCaptureSession *session = sessionFor(m_session);
        session.owner = nullptr;
        CFRelease(m_session);
        m_session = nullptr;
    }
    emit changed();
}
