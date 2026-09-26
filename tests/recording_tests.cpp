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
    void videoSelectorIsSingleRegion();
    void desktopRecording();
};

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
    marker.show(); marker.raise();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
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
    frame.start(recording::toolPath("ffmpeg"), {"-v", "error", "-ss", "0.4", "-i", file,
                                               "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "pipe:1"});
    QVERIFY(frame.waitForFinished(5000));
    QCOMPARE(frame.exitCode(), 0);
    QImage image = QImage::fromData(frame.readAllStandardOutput(), "PNG");
    QVERIFY(!image.isNull());
    const QColor actual = image.pixelColor(image.width() / 2, image.height() / 2);
    const QColor expected("#123456");
    // Screen color conversion and lossy 4:2:0 video encoding can change RGB
    // values. Verify the source region without requiring pixel identity.
    QVERIFY2(qAbs(actual.red() - expected.red()) <= 16, qPrintable(actual.name()));
    QVERIFY2(qAbs(actual.green() - expected.green()) <= 16, qPrintable(actual.name()));
    QVERIFY2(qAbs(actual.blue() - expected.blue()) <= 16, qPrintable(actual.name()));
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
