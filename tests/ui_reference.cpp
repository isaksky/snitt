// Render the immutable reference QML using the same fixtures as snitt.ui-parity-test.
// The runner links this driver against the archived reference sources/resources.
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QtTest>
#include "appsettings.h"
#include "backend.h"
#include "editorcanvas.h"
#include "playbackclock.h"
#include "trimsession.h"
#include "regionselector.h"

// Deterministic media metadata fixture. The reference QML remains unchanged;
// actual decoder/playback/export are covered separately with real recordings.
class ReviewFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 duration MEMBER duration NOTIFY changed)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QUrl source READ source CONSTANT)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(double progress MEMBER progress NOTIFY changed)
    Q_PROPERTY(QString problem MEMBER problem NOTIFY changed)
    Q_PROPERTY(QStringList thumbnails MEMBER thumbnails NOTIFY changed)
public:
    qint64 duration = 0;
    bool busy = false;
    double progress = 0;
    QString problem;
    QStringList thumbnails;
    QString path() const { return {}; }
    QUrl source() const { return {}; }
    Q_INVOKABLE void setDuration(qint64 value) { duration = value; emit changed(); }
    Q_INVOKABLE void keepOriginal() {}
    Q_INVOKABLE void cancelExport() {}
signals:
    void changed();
};
class UiBackend : public Backend {
    Q_OBJECT
    Q_PROPERTY(QObject *trim READ fixture CONSTANT)
    Q_PROPERTY(bool recording MEMBER recording NOTIFY recordingFixtureChanged)
    Q_PROPERTY(bool startingRecording MEMBER startingRecording NOTIFY recordingFixtureChanged)
    Q_PROPERTY(bool finishingRecording MEMBER finishingRecording NOTIFY recordingFixtureChanged)
    Q_PROPERTY(int recordingElapsed MEMBER recordingElapsed NOTIFY recordingFixtureChanged)
public:
    using Backend::Backend;
    ReviewFixture review;
    bool recording = false, startingRecording = false, finishingRecording = false;
    int recordingElapsed = 0;
signals:
    void recordingFixtureChanged();
public:
    QObject *fixture() { return &review; }
};

static QQuickItem *visualItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

static QJsonObject properties(QQuickItem *item) {
    const QPointF scene = item->mapToScene(QPointF());
    QJsonObject result{{"x", scene.x()}, {"y", scene.y()}, {"width", item->width()},
                       {"height", item->height()}, {"visible", item->isVisible()}};
    for (const auto *name : {"text", "checked", "enabled", "opacity", "hovered", "hoverEnabled", "pressed", "visualFocus", "font.pixelSize", "font.family", "font.weight", "icon.width", "icon.height"}) {
        const QVariant value = QQmlProperty(item, QString::fromLatin1(name)).read();
        if (value.isValid()) result.insert(name, QJsonValue::fromVariant(value));
    }
    return result;
}

static void collect(QQuickItem *item, QJsonObject &result) {
    if (!item->objectName().isEmpty()) result.insert(item->objectName(), properties(item));
    else if (!item->property("text").toString().isEmpty())
        result.insert("text:" + item->property("text").toString(), properties(item));
    for (auto *child : item->childItems()) collect(child, result);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("snitt-ui-reference");
    QQuickStyle::setStyle("Material");
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    qmlRegisterType<PlaybackClock>("Snitt", 1, 0, "PlaybackClock");
    AppSettings settings;
    UiBackend backend(&settings);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().size() != 1) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    if (!window || !canvas) return 2;
    const QDir output(qEnvironmentVariable("SNITT_UI_OUTPUT"));
    if (!QDir().mkpath(output.absolutePath())) return 3;
    window->show();
    QTest::qWait(300);
    const auto snapshot = [&](const QString &name, QSize size, bool resetPointer = true) {
        window->resize(size);
        if (resetPointer) QTest::mouseMove(window, QPoint(size.width() - 5, size.height() - 40));
        QTest::qWait(name.endsWith("hover") || name.endsWith("pressed") || name.endsWith("focus") ? 600 : 300);
        if (!window->grabWindow().save(output.filePath(name + ".png"))) qFatal("Snapshot failed");
        QJsonObject geometry;
        collect(window->contentItem(), geometry);
        QFile file(output.filePath(name + ".json"));
        if (!file.open(QIODevice::WriteOnly)) qFatal("Geometry write failed");
        file.write(QJsonDocument(geometry).toJson());
    };
    snapshot("empty-wide", {1100, 760});
    snapshot("empty-small", {860, 560});
    if (!canvas->load(QUrl::fromLocalFile(qEnvironmentVariable("SNITT_UI_IMAGE")))) return 4;
    snapshot("populated-wide", {1100, 760});
    snapshot("populated-small", {860, 560});
    snapshot("populated-large", {1400, 900});
    window->resize(1100, 760); QTest::qWait(150);
    auto *rectButton = visualItem(window->contentItem(), "tool_rect");
    const QPoint toolPoint = rectButton->mapToScene(QPointF(20,20)).toPoint();
    QTest::mouseMove(window, toolPoint); snapshot("tool-hover", {1100,760}, false);
    QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, toolPoint);
    snapshot("tool-pressed", {1100,760}, false);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier, {1095,720});
    auto *copyButton = visualItem(window->contentItem(), "copyButton");
    const QPoint copyPoint = copyButton->mapToScene(QPointF(copyButton->width()/2,20)).toPoint();
    QTest::mouseMove(window, copyPoint); snapshot("copy-hover", {1100,760}, false);
    QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, copyPoint);
    snapshot("copy-pressed", {1100,760}, false);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier, {1095,720});
    visualItem(window->contentItem(), "ink_good")->forceActiveFocus(Qt::TabFocusReason);
    snapshot("ink-focus", {1100,760});
    canvas->forceActiveFocus();
    canvas->setTool("text");
    window->resize(1100, 760);
    QTest::qWait(150);
    const QPointF start = canvas->imageRect().topLeft() + QPointF(200, 180) * canvas->imageScale();
    canvas->begin(start.x(), start.y());
    auto *text = window->findChild<QObject *>("annotationText");
    text->setProperty("text", "Entry baseline\nSecond line");
    snapshot("text-draft", {1100, 760});
    QMetaObject::invokeMethod(window, "commitText");
    snapshot("text-committed", {1100, 760});
    QVariantList regions;
    const QImage regionSource(qEnvironmentVariable("SNITT_UI_IMAGE"));
    const QList<QSize> regionSizes{{200, 100}, {120, 80}, {160, 60}};
    for (int i = 0; i < regionSizes.size(); ++i) {
        const QImage region = regionSource.copy(QRect(QPoint(), regionSizes[i]));
        region.save(output.absoluteFilePath("../region-" + QString::number(i) + ".png"));
        regions.append(region);
    }
    canvas->loadRegions(regions);
    snapshot("arrangement-wide", {1100, 760});
    snapshot("arrangement-small", {860, 560});
    canvas->annotate();
    snapshot("arrangement-annotated", {1100, 760});
    auto *rearrangeDialog = window->findChild<QObject *>("rearrangeDialog");
    QMetaObject::invokeMethod(rearrangeDialog, "open"); QTest::qWait(250);
    snapshot("dialog-rearrange", {1100, 760});
    QMetaObject::invokeMethod(rearrangeDialog, "reject"); QTest::qWait(250);
    auto *captureDialog = window->findChild<QObject *>("captureErrorDialog");
    captureDialog->setProperty("message", "Screenshot saved at /tmp/sn itt.png, but could not reveal it in the file manager.");
    QMetaObject::invokeMethod(captureDialog, "open"); QTest::qWait(250);
    snapshot("dialog-reveal", {1100, 760});
    QMetaObject::invokeMethod(captureDialog, "close"); QTest::qWait(250);
    canvas->clear();
    captureDialog->setProperty("message", "Allow Snitt in System Settings → Privacy & Security → Screen & System Audio Recording, then quit and reopen Snitt.");
    QMetaObject::invokeMethod(captureDialog, "open"); QTest::qWait(250);
    snapshot("dialog-capture-permission", {1100, 760});
    QMetaObject::invokeMethod(captureDialog, "close"); QTest::qWait(250);
    captureDialog->setProperty("message", "Could not capture a screen. Check screen recording permission and that the desktop is unlocked.");
    QMetaObject::invokeMethod(captureDialog, "open"); QTest::qWait(250);
    snapshot("dialog-capture-error", {1100, 760});
    QMetaObject::invokeMethod(captureDialog, "close"); QTest::qWait(250);
    auto *editor = window;
    window = editor->findChild<QQuickWindow *>("recordingReviewWindow");
    window->show();
    snapshot("review-empty", {960, 690});
    backend.review.setDuration(12000);
    auto *timeline = window->findChild<QObject *>("recordingTrimBar");
    timeline->setProperty("endSec", 12.0);
    snapshot("review-loaded", {960, 690});
    timeline->setProperty("startSec", 2.0);
    timeline->setProperty("endSec", 10.0);
    timeline->setProperty("playheadSec", 6.0);
    snapshot("review-trimmed", {960, 690});
    snapshot("review-small", {680, 520});
    backend.review.busy = true; backend.review.progress = 0.35; emit backend.review.changed();
    snapshot("review-exporting", {960, 690});
    backend.review.busy = false; backend.review.problem = "Could not save the trimmed recording."; emit backend.review.changed();
    snapshot("review-error", {960, 690});
    window->hide();
    backend.recording = true; backend.startingRecording = true; emit backend.recordingFixtureChanged();
    window = editor->findChild<QQuickWindow *>("recordingWindow");
    snapshot("recording-starting", {340, 58});
    backend.startingRecording = false; backend.recordingElapsed = 65; emit backend.recordingFixtureChanged();
    snapshot("recording-running", {340, 58});
    backend.finishingRecording = true; emit backend.recordingFixtureChanged();
    snapshot("recording-finishing", {340, 58});
    backend.recording = false; emit backend.recordingFixtureChanged();
    window = editor->findChild<QQuickWindow *>("recordingStartupIndicator");
    window->setProperty("diameter", 180); window->setProperty("completing", true);
    window->resize(180, 180); window->show();
    // Match the presentation angle after the startup animator stops. A full
    // circle is rotationally symmetric, but transformed antialiasing is not.
    QTest::qWait(30);
    for (auto *item : window->findChildren<QQuickItem *>())
        if (QByteArray(item->metaObject()->className()).contains("Canvas")) item->setRotation(0);
    snapshot("recording-ready", {180, 180});
    window->hide();
    RegionSelector selector(QImage(qEnvironmentVariable("SNITT_UI_IMAGE")), {0, 0, 1100, 760}, &settings);
    selector.show();
    const auto selectorSnapshot = [&](const QString &name, QSize size) {
        selector.resize(size); QTest::qWait(150);
        if (name != "selector-drag") QTest::mouseMove(&selector, {size.width() - 5, size.height() - 5});
        selector.grab().save(output.filePath(name + ".png"));
        QJsonObject geometry;
        for (auto *child : selector.findChildren<QWidget *>()) {
            if (child->objectName().isEmpty()) continue;
            auto point = child->mapTo(&selector, QPoint());
            geometry.insert(child->objectName(), QJsonObject{{"x",point.x()}, {"y",point.y()},
                {"width",child->width()}, {"height",child->height()}, {"font",child->font().family()}});
        }
        QFile file(output.filePath(name + ".json")); file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(geometry).toJson());
    };
    selectorSnapshot("selector-single", {1100, 760});
    selector.setSelections(true, {{0, {100,180,200,120}}, {1, {450,380,20,20}}}, 2);
    selectorSnapshot("selector-multiple", {1100, 760});
    selectorSnapshot("selector-small", {440, 500});
    selector.setSelections(false, {}, 0); selector.setVideo(true);
    selectorSnapshot("selector-video", {1100, 760});
    selector.setVideo(false); selector.setNotice("Selection could not be captured. Try again.");
    QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, {100,180});
    QTest::mouseMove(&selector, {500,740});
    // Drain the platform cursor warp before supplying the final drag event.
    // Cocoa can otherwise deliver the previous snapshot's cursor move late.
    QTest::qWait(200);
    QMouseEvent finalDrag(QEvent::MouseMove, QPointF(500,740), selector.mapToGlobal(QPoint(500,740)),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&selector, &finalDrag);
    selectorSnapshot("selector-drag", {1100, 760});
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, {500,740});
    selector.setNotice("");
    selector.setSelections(true, {{0, {100.5,180.5,200.5,120.5}}, {1, {450.5,380.5,20.5,20.5}}}, 2);
    selectorSnapshot("selector-fractional", {1100,760});
    qInfo("PASS: reference editor and media snapshots");
    return 0;
}

#include "ui_reference.moc"
