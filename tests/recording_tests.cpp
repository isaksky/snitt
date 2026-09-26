#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QStandardPaths>
#include "backend.h"
#include "regionselector.h"
#include "videorecorder.h"

class RecordingTests : public QObject {
    Q_OBJECT
private slots:
    void recordingArguments();
    void indicatorPlacement();
    void videoSelectorIsSingleRegion();
    void desktopRecording();
};

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

QTEST_MAIN(RecordingTests)
#include "recording_tests.moc"
