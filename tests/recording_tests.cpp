#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWindow>
#include "backend.h"
#include "regionselector.h"
#include "videorecorder.h"
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

class RecordingTests : public QObject {
    Q_OBJECT
private slots:
    void recordingArguments();
    void indicatorPlacement();
    void videoSelectorIsSingleRegion();
    void desktopRecording();
    void trimKeepsOriginalOnDismissAndInvalidRange();
    void trimHeadAndTailAccurately();
    void trimThumbnailsFollowWindow();
    void trimCancellationPreservesOriginal();
    void trimReplacementFailurePreservesOriginal();
#ifdef Q_OS_WIN
    void trimWindowsThumbnailsResumeAfterCancelAndFailure();
#endif
#ifdef Q_OS_MACOS
    void nativeTrimProgressAndReset();
    void cancellationTimeoutDiscards();
    void interactiveCancellationTimeout();
#endif
};

static QString makeFourColorClip(const QString &directory) {
    const QString path = QDir(directory).filePath("source.mp4");
    const QString ffmpeg = recording::toolPath("ffmpeg");
    if (ffmpeg.isEmpty()) return {};
    QProcess process;
    QStringList args {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "color=c=black:s=160x90:r=30:d=4", "-vf",
        "drawbox=c=red:t=fill:enable='lt(t,1)',"
        "drawbox=c=green:t=fill:enable='gte(t,1)*lt(t,2)',"
        "drawbox=c=blue:t=fill:enable='gte(t,2)*lt(t,3)',"
        "drawbox=c=yellow:t=fill:enable='gte(t,3)'",
        "-c:v", "libx264", "-g", "30", "-pix_fmt", "yuv420p", "-movflags", "+faststart", path};
    process.start(ffmpeg, args);
    if (!process.waitForFinished(15000) || process.exitCode() != 0) return {};
    return path;
}

static QByteArray clipPixels(const QString &path) {
    QProcess decoder;
    decoder.start(recording::toolPath("ffmpeg"), {"-hide_banner", "-loglevel", "error",
        "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    if (!decoder.waitForFinished(15000) || decoder.exitCode() != 0) return {};
    return decoder.readAllStandardOutput();
}

void RecordingTests::trimKeepsOriginalOnDismissAndInvalidRange() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY2(!path.isEmpty(), "Could not make the four-color recording fixture");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    file.close();
    TrimSession trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    QVERIFY(trim.duration() > 3000);
    trim.exportRange(-1, 2000);
    QVERIFY(!trim.problem().isEmpty());
    QVERIFY(!trim.busy());
    trim.keepOriginal();
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(finalized.first().first().toString(), path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256), original);
    file.close();
    QVERIFY(QDir(directory.path()).entryList({".xshot-trim-*.mp4"}, QDir::Files).isEmpty());
    finalized.clear();
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    trim.exportRange(0, trim.duration());
    QCOMPARE(finalized.size(), 1);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256), original);
    file.close();
}

void RecordingTests::trimHeadAndTailAccurately() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY2(!path.isEmpty(), "Could not make the four-color recording fixture");
    TrimSession trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    QVERIFY(trim.duration() > 3000);
    trim.exportRange(1100, 2100);
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy() || !trim.problem().isEmpty(), 60000);
    QVERIFY2(trim.problem().isEmpty(), qPrintable(trim.problem()));
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(finalized.first().first().toString(), path);
    const QByteArray decoded = clipPixels(path);
    const qsizetype frameSize = 160 * 90 * 3;
    QVERIFY(decoded.size() >= frameSize * 25);
    QVERIFY(decoded.size() <= frameSize * 32);
    const auto pixel = [&](qsizetype offset) {
        const qsizetype middle = offset + (45 * 160 + 80) * 3;
        return QColor(uchar(decoded[middle]), uchar(decoded[middle + 1]), uchar(decoded[middle + 2]));
    };
    const QColor first = pixel(0);
    const QColor last = pixel(decoded.size() - frameSize);
    QVERIFY2(first.green() > first.red() * 1.5 && first.green() > first.blue() * 1.5,
             qPrintable(QStringLiteral("First frame %1 was not green").arg(first.name())));
    QVERIFY2(last.blue() > last.red() * 1.5 && last.blue() > last.green() * 1.5,
             qPrintable(QStringLiteral("Last frame %1 was not blue").arg(last.name())));
    QVERIFY(QDir(directory.path()).entryList({".xshot-trim-*.mp4"}, QDir::Files).isEmpty());
}

void RecordingTests::trimThumbnailsFollowWindow() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the color fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY(!path.isEmpty());
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256);
    source.close();
    TrimSession trim;
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    const auto thumbnailsReady = [&] {
        if (trim.thumbnails().size() != 12) return false;
        for (const QString &url : trim.thumbnails()) if (url.isEmpty()) return false;
        return true;
    };
    const auto colorAt = [&](int index) {
        const QImage image(QUrl(trim.thumbnails().at(index)).toLocalFile());
        return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
    };
    const auto isRed = [](QColor c) { return c.red() > 90 && c.red() > c.green() * 2 && c.red() > c.blue() * 2; };
    const auto isGreen = [](QColor c) { return c.green() > 60 && c.green() > c.red() * 2 && c.green() > c.blue() * 2; };
    const auto isBlue = [](QColor c) { return c.blue() > 90 && c.blue() > c.red() * 2 && c.blue() > c.green() * 2; };
    const auto isYellow = [](QColor c) { return c.red() > 90 && c.green() > 90 && c.blue() < 60; };
    QTRY_VERIFY_WITH_TIMEOUT(thumbnailsReady(), 45000);
    QVERIFY2(isRed(colorAt(0)), qPrintable(colorAt(0).name()));
    QVERIFY2(isYellow(colorAt(11)), qPrintable(colorAt(11).name()));

    trim.setThumbnailWindow(1375, 2625);
    QTRY_VERIFY_WITH_TIMEOUT(thumbnailsReady(), 45000);
    QVERIFY2(isGreen(colorAt(0)), qPrintable(colorAt(0).name()));
    QVERIFY2(isBlue(colorAt(11)), qPrintable(colorAt(11).name()));

    // Switch twice before older asynchronous results finish; only the latest
    // full-width strip may be published.
    trim.setThumbnailWindow(1625, 2375);
    trim.setThumbnailWindow(0, 4000);
    QTRY_VERIFY_WITH_TIMEOUT(thumbnailsReady(), 45000);
    QTest::qWait(500);
    QVERIFY2(isRed(colorAt(0)), qPrintable(colorAt(0).name()));
    QVERIFY2(isYellow(colorAt(11)), qPrintable(colorAt(11).name()));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
}

void RecordingTests::trimCancellationPreservesOriginal() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY(!path.isEmpty());
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256);
    source.close();
    TrimSession trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    trim.exportRange(500, 2500);
    QVERIFY(trim.busy());
    trim.cancelExport();
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy(), 15000);
    QVERIFY(finalized.isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
    source.close();
    QVERIFY(QDir(directory.path()).entryList({".xshot-trim-*.mp4"}, QDir::Files).isEmpty());
    trim.keepOriginal();
    QCOMPARE(finalized.size(), 1);
}

void RecordingTests::trimReplacementFailurePreservesOriginal() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    class FailingReplacement : public TrimSession {
    protected:
        bool replaceFile(const QString &, const QString &, QString *problem) override {
            *problem = QStringLiteral("Simulated replacement failure; original preserved.");
            return false;
        }
    };
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY(!path.isEmpty());
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256);
    source.close();
    FailingReplacement trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    if (trim.duration() == 0) trim.setDuration(4000);
    trim.exportRange(1100, 2100);
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy() || !trim.problem().isEmpty(), 60000);
    QVERIFY(!trim.problem().isEmpty());
    QCOMPARE(trim.progress(), 0.0);
    QVERIFY(finalized.isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
    source.close();
    QVERIFY(QDir(directory.path()).entryList({".xshot-trim-*.mp4"}, QDir::Files).isEmpty());
}

#ifdef Q_OS_WIN
void RecordingTests::trimWindowsThumbnailsResumeAfterCancelAndFailure() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = makeFourColorClip(directory.path());
    QVERIFY(!path.isEmpty());
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256);
    source.close();
    const auto allReady = [](const TrimSession &trim) {
        if (trim.thumbnails().size() != 12) return false;
        for (const QString &url : trim.thumbnails()) if (url.isEmpty()) return false;
        return true;
    };
    const auto noTemporaryOutput = [&] {
        return QDir(directory.path()).entryList({".xshot-trim-*.mp4", ".xshot-original-*.mp4"}, QDir::Files).isEmpty();
    };
    {
        TrimSession trim;
        trim.open(path);
        trim.setDuration(4000);
        QVERIFY(!allReady(trim)); // Export starts before the sequential filmstrip finishes.
        trim.exportRange(500, 2500);
        QVERIFY(trim.busy());
        trim.cancelExport();
        QTRY_VERIFY_WITH_TIMEOUT(!trim.busy(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(allReady(trim), 45000);
        QCOMPARE(trim.path(), path);
        QVERIFY(noTemporaryOutput());
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
        source.close();
    }
    class FailOnce : public TrimSession {
    public:
        bool failNext = true;
    protected:
        bool replaceFile(const QString &temporary, const QString &originalPath, QString *problem) override {
            if (failNext) {
                failNext = false;
                *problem = QStringLiteral("Simulated replacement failure; original preserved.");
                return false;
            }
            return TrimSession::replaceFile(temporary, originalPath, problem);
        }
    } trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    trim.setDuration(4000);
    QVERIFY(!allReady(trim));
    trim.exportRange(1100, 2100);
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy() || !trim.problem().isEmpty(), 60000);
    QVERIFY(!trim.problem().isEmpty());
    QVERIFY(finalized.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(allReady(trim), 45000);
    QVERIFY(noTemporaryOutput());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
    source.close();
    trim.exportRange(1100, 2100);
    QTRY_VERIFY_WITH_TIMEOUT(finalized.size() == 1 || !trim.problem().isEmpty(), 60000);
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(finalized.first().first().toString(), path);
    QVERIFY(trim.path().isEmpty());
    QVERIFY(noTemporaryOutput());
    QTemporaryDir nextDirectory;
    QVERIFY(nextDirectory.isValid());
    const QString nextPath = makeFourColorClip(nextDirectory.path());
    QVERIFY(!nextPath.isEmpty());
    trim.open(nextPath);
    trim.setDuration(4000);
    QTRY_VERIFY_WITH_TIMEOUT(allReady(trim), 45000);
    const QString thumbnailDirectory = QFileInfo(QUrl(trim.thumbnails().first()).toLocalFile()).absolutePath();
    for (const QString &url : trim.thumbnails())
        QCOMPARE(QFileInfo(QUrl(url).toLocalFile()).absolutePath(), thumbnailDirectory);
}
#endif

#ifdef Q_OS_MACOS
void RecordingTests::nativeTrimProgressAndReset() {
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed for the video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("native-progress.mp4");
    QProcess fixture;
    fixture.start(recording::toolPath("ffmpeg"), {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=2560x1440:rate=30:duration=6",
        "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", path});
    QVERIFY(fixture.waitForFinished(60000));
    QCOMPARE(fixture.exitCode(), 0);

    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256);
    source.close();
    TrimSession trim;
    QSignalSpy finalized(&trim, &TrimSession::finalized);
    trim.open(path);
    QVERIFY(trim.duration() >= 5900);
    const qint64 start = 500, end = trim.duration() - 500;

    trim.exportRange(start, end);
    QVERIFY(trim.busy());
    QCOMPARE(trim.progress(), -1.0); // No invented percentage before AVFoundation reports one.
    trim.cancelExport();
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy(), 15000);
    QCOMPARE(trim.progress(), 0.0);
    QVERIFY(finalized.isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256), original);
    source.close();

    QVector<double> observed;
    connect(&trim, &TrimSession::changed, &trim, [&] {
        if (trim.busy()) observed.append(trim.progress());
    });
    trim.exportRange(start, end);
    QVERIFY(trim.busy());
    QCOMPARE(trim.progress(), -1.0); // A retry starts with unknown progress again.
    QTRY_VERIFY_WITH_TIMEOUT(!trim.busy() || !trim.problem().isEmpty(), 90000);
    QVERIFY2(trim.problem().isEmpty(), qPrintable(trim.problem()));
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(trim.progress(), 0.0);
    QVERIFY(!observed.isEmpty());
    bool sawNativeFraction = false;
    for (const double progress : observed) {
        QVERIFY2(progress == -1.0 || (progress >= 0.01 && progress <= 0.99),
                 qPrintable(QStringLiteral("Invalid native progress %1").arg(progress)));
        sawNativeFraction |= progress > 0;
    }
    QVERIFY2(sawNativeFraction, "The long native export did not report a determinate fraction while busy");
    trim.open(path);
    QCOMPARE(trim.progress(), 0.0); // The next recording starts without the last export's fraction.
    trim.keepOriginal();
}
#endif

void RecordingTests::indicatorPlacement() {
    const QRect primary(0, 0, 1920, 1080);
    const QRect workArea(0, 24, 1920, 1056);
    QCOMPARE(Backend::indicatorGeometry(QRect(4, 4, 12, 12), primary, workArea),
             QRect(0, 24, 360, 360));
    const QRect secondary(-1280, -240, 1280, 800);
    const QRect secondaryWork(-1280, -240, 1280, 760);
    const QRect centered = Backend::indicatorGeometry(QRect(-900, 80, 40, 30), secondary, secondaryWork);
    QCOMPARE(centered.size(), QSize(267, 267));
    QVERIFY(secondaryWork.contains(centered));
    QVERIFY(qAbs(centered.center().x() - QRect(-900, 80, 40, 30).center().x()) <= 1);
    QVERIFY(qAbs(centered.center().y() - QRect(-900, 80, 40, 30).center().y()) <= 1);
    // The input is already in logical coordinates; a Retina backing scale
    // must not double the visual diameter or move it to pixel coordinates.
    QCOMPARE(Backend::indicatorGeometry(QRect(800, 500, 20, 20), QRect(0, 0, 1728, 1117),
                                        QRect(0, 25, 1728, 1092)).size(), QSize(372, 372));
    QCOMPARE(Backend::controlsGeometry(workArea), QRect(790, 36, 340, 58));
    QCOMPARE(Backend::controlsGeometry(secondaryWork), QRect(-810, -228, 340, 58));
    QCOMPARE(Backend::controlsGeometry(QRect(-40, 20, 180, 40)), QRect(-40, 20, 180, 40));
}

void RecordingTests::recordingArguments() {
    recording::Source windows;
    windows.platform = recording::Platform::Windows;
    windows.pixelRegion = QRect(-1801, 271, 321, 199);
    const QString output = "C:/Clips with spaces/recording.mp4";
    const QStringList args = recording::arguments(windows, output);
    auto value = [&args](const QString &flag) { return args.value(args.indexOf(flag) + 1); };
    QCOMPARE(value("-f"), "gdigrab");
    QCOMPARE(value("-offset_x"), "-1801");
    QCOMPARE(value("-offset_y"), "271");
    QCOMPARE(value("-video_size"), "321x199");
    QCOMPARE(value("-vf"), "pad=ceil(iw/2)*2:ceil(ih/2)*2");
    QCOMPARE(value("-i"), "desktop");
    QCOMPARE(args.last(), output);
    QVERIFY(args.contains("-an"));
    QVERIFY(args.contains("-n"));
    QVERIFY(!args.contains("-y"));
    QVERIFY(!args.contains("-nostdin"));
    QCOMPARE(value("-probesize"), "32");
    QCOMPARE(value("-analyzeduration"), "0");
    QVERIFY(!recording::revealSavedFile("/path/that/does/not/exist.mp4"));

    recording::Source mac;
    mac.platform = recording::Platform::Mac;
    mac.screenIndex = 2;
    mac.relativeRegion = QRectF(0.25, 0.125, 0.5, 0.25);
    const QStringList macArgs = recording::arguments(mac, "/Users/test/Movies/test.mp4");
    QCOMPARE(macArgs.value(macArgs.indexOf("-i") + 1), "Capture screen 2:none");
    const QString filter = macArgs.value(macArgs.indexOf("-vf") + 1);
    QVERIFY(filter.startsWith("crop=round(iw*0.5000000000):round(ih*0.2500000000):round(iw*0.2500000000):round(ih*0.1250000000):exact=1,"));
    QCOMPARE(macArgs.value(macArgs.indexOf("-c:v") + 1), "libx264");
}

void RecordingTests::videoSelectorIsSingleRegion() {
    QImage image(800, 600, QImage::Format_RGB32); image.fill(Qt::blue);
    RegionSelector selector(image, QRect(0, 0, 400, 300));
    QSignalSpy videoRequest(&selector, &RegionSelector::videoRequested);
    QSignalSpy multiple(&selector, &RegionSelector::multipleRequested);
    QSignalSpy screenshot(&selector, &RegionSelector::selected);
    QSignalSpy video(&selector, &RegionSelector::videoSelected);
    QSignalSpy accepted(&selector, &RegionSelector::accepted);
    selector.show();
    QTest::keyClick(&selector, Qt::Key_V);
    QCOMPARE(videoRequest.size(), 1);
    selector.setVideo(true);
    QTest::keyClick(&selector, Qt::Key_M);
    QCOMPARE(multiple.size(), 0);
    QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(30, 40));
    QTest::mouseMove(&selector, QPoint(150, 140));
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(150, 140));
    QCOMPARE(video.size(), 1);
    QCOMPARE(video.first().first().toRectF(), QRectF(30, 40, 120, 100));
    QCOMPARE(screenshot.size(), 0);
    QCOMPARE(accepted.size(), 0);
    selector.setVideo(false);
    QTest::keyClick(&selector, Qt::Key_M);
    QCOMPARE(multiple.size(), 1);
}

void RecordingTests::desktopRecording() {
    if (!qEnvironmentVariableIsSet("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Recording requires an interactive desktop and FFmpeg");
    QVERIFY2(!recording::toolPath("ffmpeg").isEmpty(), "FFmpeg must be installed for recording integration tests");
    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    marker.setStyleSheet("background: #123456;");
    const QRect display = QApplication::primaryScreen()->geometry();
    marker.setGeometry(display.x() + 140, display.y() + 180, 360, 260);
#ifdef Q_OS_MACOS
    QProcess markerProcess;
    const auto markerCleanup = qScopeGuard([&markerProcess] {
        if (markerProcess.state() != QProcess::NotRunning) {
            markerProcess.kill();
            markerProcess.waitForFinished(5000);
        }
    });
    const QString markerTool = QDir(QCoreApplication::applicationDirPath())
        .filePath("../recording_marker/recording_marker");
    QVERIFY2(QFileInfo::exists(markerTool), "Run bin/test to build the external recording marker");
    markerProcess.start(markerTool, {QString::number(marker.x()), QString::number(marker.y()),
                                     QString::number(marker.width()), QString::number(marker.height())});
    QVERIFY(markerProcess.waitForReadyRead(5000));
    QVERIFY(markerProcess.readAllStandardOutput().contains("ready"));
#else
    marker.show(); marker.raise();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
#endif
    QTest::qWait(100);

    Backend backend;
#ifdef Q_OS_WIN
    QWindow startupOverlay, controlsOverlay;
    startupOverlay.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    controlsOverlay.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    startupOverlay.setGeometry(display.x() + 160, display.y() + 190, 120, 120);
    controlsOverlay.setGeometry(display.x() + 320, display.y() + 190, 120, 120);
    startupOverlay.setOpacity(0);
    controlsOverlay.setOpacity(0);
    startupOverlay.show(); controlsOverlay.show();
    QVERIFY(QTest::qWaitForWindowExposed(&startupOverlay));
    QVERIFY(QTest::qWaitForWindowExposed(&controlsOverlay));
#endif
    QElapsedTimer startup;
    qint64 processMs = -1, readyMs = -1;
    connect(&backend, &Backend::recordingProcessStarted, this, [&] { processMs = startup.elapsed(); });
    connect(&backend, &Backend::recordingReady, this, [&] { readyMs = startup.elapsed(); });
    QString error;
    connect(&backend, &Backend::error, this, [&error](const QString &message) { error = message; });
    QSignalSpy saved(&backend, &Backend::recordingSaved);
    QApplication::clipboard()->setText("keep clipboard on save");
    backend.capture();
    RegionSelector *selector = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<RegionSelector *>(widget);
            if (candidate && candidate->isVisible() && candidate->geometry().contains(marker.geometry().center())) {
                selector = candidate;
                return true;
            }
        }
        return false;
    })(), 5000);
    QTest::keyClick(selector, Qt::Key_V);
    QTest::keyClick(selector, Qt::Key_M); // Must not re-enable multi-region recording.
    const QPoint start = marker.geometry().topLeft() + QPoint(20, 20) - selector->geometry().topLeft();
    const QPoint end = start + QPoint(241, 159);
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(selector, end);
    startup.start();
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY2(backend.recording(), qPrintable(error));
#ifdef Q_OS_WIN
    QVERIFY(backend.recordingProtectionPending());
    QVERIFY2(backend.beginProtectedRecording(&startupOverlay, &controlsOverlay), qPrintable(error));
#endif
    QTRY_VERIFY_WITH_TIMEOUT(!backend.startingRecording() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    qInfo("recording startup: process=%lld ms, first frame/ready=%lld ms", processMs, readyMs);
    QTRY_VERIFY_WITH_TIMEOUT(backend.recordingElapsed() >= 1 || !error.isEmpty(), 6000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    backend.finishRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(saved.size(), 1);
    QVERIFY(!backend.recording());
    const QString file = saved.first().first().toString();
    QVERIFY(QFileInfo(file).size() > 1000);
    QCOMPARE(QFileInfo(file).absolutePath(), QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("xshot"));
    QCOMPARE(QApplication::clipboard()->text(), "keep clipboard on save");

    QProcess probe;
    probe.start(recording::toolPath("ffprobe"), {"-v", "error", "-show_streams", "-show_format", "-of", "json", file});
    QVERIFY(probe.waitForFinished(5000));
    QCOMPARE(probe.exitCode(), 0);
    const auto metadata = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const auto streams = metadata.value("streams").toArray();
    QCOMPARE(streams.size(), 1);
    const auto stream = streams.first().toObject();
    QCOMPARE(stream.value("codec_name").toString(), "h264");
    QVERIFY(stream.value("width").toInt() >= 241);
    QVERIFY(stream.value("height").toInt() >= 159);
    QCOMPARE(stream.value("width").toInt() % 2, 0);
    QCOMPARE(stream.value("height").toInt() % 2, 0);
    QVERIFY(metadata.value("format").toObject().value("duration").toString().toDouble() >= 1.0);

    QProcess frame;
    frame.start(recording::toolPath("ffmpeg"), {"-v", "error", "-i", file,
                                               "-frames:v", "5", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    QVERIFY(frame.waitForFinished(5000));
    QCOMPARE(frame.exitCode(), 0);
    const QByteArray pixels = frame.readAllStandardOutput();
    const int width = stream.value("width").toInt(), height = stream.value("height").toInt();
    const qsizetype bytesPerFrame = qsizetype(width) * height * 3;
    QVERIFY(bytesPerFrame > 0);
    QVERIFY2(pixels.size() >= bytesPerFrame, "No decoded first frame");
    const QColor expected("#123456");
    // Screen color conversion and lossy 4:2:0 video encoding can change RGB
    // values. Inspect the first frame and all early frames that were decoded.
    for (qsizetype offset = 0; offset + bytesPerFrame <= pixels.size(); offset += bytesPerFrame) {
        const qsizetype center = offset + (qsizetype(height / 2) * width + width / 2) * 3;
        const QColor actual{uchar(pixels[center]), uchar(pixels[center + 1]), uchar(pixels[center + 2])};
        QVERIFY2(qAbs(actual.red() - expected.red()) <= 16, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.green() - expected.green()) <= 16, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.blue() - expected.blue()) <= 16, qPrintable(actual.name()));
    }
    QVERIFY(QMetaObject::invokeMethod(backend.trim(), "keepOriginal"));
    QVERIFY(backend.trim()->property("path").toString().isEmpty());
    QVERIFY(QFile::remove(file));

    QSignalSpy canceled(&backend, &Backend::recordingCanceled);
    QApplication::clipboard()->setText("keep clipboard on cancel");
    backend.capture(false, true);
    selector = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<RegionSelector *>(widget);
            if (candidate && candidate->isVisible() && candidate->geometry().contains(marker.geometry().center())) {
                selector = candidate;
                return true;
            }
        }
        return false;
    })(), 5000);
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(selector, end);
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, end);
    QVERIFY2(backend.recording(), qPrintable(error));
#ifdef Q_OS_WIN
    QVERIFY(backend.recordingProtectionPending());
    QVERIFY2(backend.beginProtectedRecording(&startupOverlay, &controlsOverlay), qPrintable(error));
#endif
    QTRY_VERIFY_WITH_TIMEOUT(!backend.startingRecording() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString canceledFile = backend.recordingPath();
    backend.cancelRecording();
    QTRY_COMPARE_WITH_TIMEOUT(canceled.size(), 1, 5000);
    QVERIFY(!QFileInfo::exists(canceledFile));
    QVERIFY(!backend.recording());
    QCOMPARE(saved.size(), 1);
    QCOMPARE(QApplication::clipboard()->text(), "keep clipboard on cancel");
}

#ifdef Q_OS_MACOS
void RecordingTests::cancellationTimeoutDiscards() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    MacRecorder recorder;
    QSignalSpy canceled(&recorder, &MacRecorder::canceled);
    QSignalSpy saved(&recorder, &MacRecorder::saved);
    QSignalSpy error(&recorder, &MacRecorder::error);
    QApplication::clipboard()->setText("keep clipboard on cancel");

    // Cancel while shareable content is still being prepared. Its late callback
    // must have no owner to restart the old capture.
    recording::Source source;
    source.platform = recording::Platform::Mac;
    source.displayId = 0;
    source.relativeRegion = QRectF(0, 0, 0.25, 0.25);
    recorder.start(source);
    QVERIFY(recorder.starting());
    recorder.cancel();
    QCOMPARE(canceled.size(), 1);
    QVERIFY(!recorder.active());
    QTest::qWait(100);
    QCOMPARE(canceled.size(), 1);
    QCOMPARE(saved.size(), 0);
    QCOMPARE(error.size(), 0);

    // Inject the same timeout branch reached when native stop completion is
    // delayed or absent, both before and after the first recorded frame.
    for (const bool firstFrame : {false, true}) {
        const QString path = temporary.filePath(firstFrame ? "after-frame.mp4" : "before-ready.mp4");
        QFile partial(path);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        QVERIFY(partial.write("partial video") > 0);
        partial.close();
        recorder.m_output = path;
        recorder.m_active = true;
        recorder.m_ready = firstFrame;
        recorder.m_firstFrame = firstFrame;
        recorder.m_finishing = true;
        recorder.m_canceling = true;
        recorder.m_recordingFinished = true;
        recorder.m_stopped = false;
        ++recorder.m_generation;
        recorder.fail("injected stop timeout");
        QVERIFY(!recorder.active());
        QVERIFY(!recorder.finishing());
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(canceled.size(), firstFrame ? 3 : 2);
        QCOMPARE(saved.size(), 0);
        QCOMPARE(error.size(), 0);
        QCOMPARE(QApplication::clipboard()->text(), "keep clipboard on cancel");
    }

    const quint64 oldGeneration = recorder.m_generation;
    recorder.m_active = true;
    recorder.m_generation = oldGeneration + 1;
    recorder.m_output = temporary.filePath("new-attempt.mp4");
    recorder.nativeStopped(oldGeneration, {});
    recorder.nativeRecordingFinished(oldGeneration);
    recorder.nativeFailed(oldGeneration, "late failure");
    recorder.nativeUserStopped(oldGeneration);
    QVERIFY(recorder.active());
    QCOMPARE(canceled.size(), 3);
    QCOMPARE(saved.size(), 0);
    QCOMPARE(error.size(), 0);
    recorder.reset();
}

void RecordingTests::interactiveCancellationTimeout() {
    if (!qEnvironmentVariableIsSet("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Requires an interactive desktop and ScreenCaptureKit permission");
    QWidget marker;
    marker.setGeometry(QRect(QApplication::primaryScreen()->geometry().topLeft() + QPoint(80, 80),
                             QSize(160, 120)));
    marker.show();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
    MacRecorder recorder;
    QSignalSpy canceled(&recorder, &MacRecorder::canceled);
    QSignalSpy saved(&recorder, &MacRecorder::saved);
    QSignalSpy error(&recorder, &MacRecorder::error);
    QApplication::clipboard()->setText("keep clipboard on timeout");
    recording::Source source;
    source.platform = recording::Platform::Mac;
    source.displayId = CGMainDisplayID();
    source.relativeRegion = QRectF(0.1, 0.1, 0.2, 0.2);
    recorder.start(source);
    QTRY_VERIFY_WITH_TIMEOUT(recorder.m_ready || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error.isEmpty() ? QString() : error.first().first().toString()));
    QTest::qWait(500);
    const QString canceledPath = recorder.path();
    recorder.cancel();
    recorder.fail("injected stop timeout");
    QCOMPARE(canceled.size(), 1);
    QCOMPARE(saved.size(), 0);
    QCOMPARE(error.size(), 0);
    QVERIFY(!recorder.active());
    QVERIFY(!QFileInfo::exists(canceledPath));

    // A late native stop or recording-finished callback belongs to the old
    // session and must neither recreate its file nor cancel this new attempt.
    recorder.start(source);
    QTRY_VERIFY_WITH_TIMEOUT(recorder.m_ready || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error.isEmpty() ? QString() : error.first().first().toString()));
    QVERIFY(recorder.active());
    QTest::qWait(1000);
    QVERIFY(!QFileInfo::exists(canceledPath));
    QVERIFY(recorder.active());
    recorder.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(canceled.size(), 2, 5000);
    QCOMPARE(saved.size(), 0);
    QCOMPARE(error.size(), 0);
    QCOMPARE(QApplication::clipboard()->text(), "keep clipboard on timeout");
}
#endif

QTEST_MAIN(RecordingTests)
#include "recording_tests.moc"
