#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QFileInfo>
#include <QScopeGuard>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QDir>
#include <QStandardPaths>
#include "backend.h"
#include "regionselector.h"
#include "globalhotkey.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include "imagedocument.h"
#include "editorcanvas.h"

class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void cutsJoinExactPixels();
    void invalidCutsDoNotAlterHistory();
    void undoRedoAndBranch();
    void annotationsAndText();
    void scaledGesturesAndClipboard();
    void failedLoadPreservesImage();
    void qmlLoadsAndPlacesText();
    void qmlToolbarPasteReplacesTextDraft();
    void qmlWheelSizes();
    void qmlSaveAndClose();
    void qmlKeyboardCommands();
    void qmlDismissal();
    void qmlRecordingControls();
    void qmlRecordingHotkeyStop();
    void regionSelectionScalesAndCancels();
    void multipleRegionSelection();
    void captureToolbarInteraction();
    void gridArrangementPreservesPixels();
    void desktopMultipleCapture();
    void windowsHotkeyRegistration();
    void windowsDesktopCapture();
    void windowsCaptureLatency();
#ifdef Q_OS_WIN
    void windowsRecordingOverlayExclusionFailure();
#endif
    void makePreviewFixture();
};

static QImage pattern() {
    QImage image(80, 60, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x * 3, y * 4, 60, 200));
    return image;
}

static QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (QQuickItem *child : parent->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

void EditorTests::cutsJoinExactPixels() {
    const QImage original = pattern();
    for (bool vertical : {true, false}) {
        for (auto range : {QPair<int, int>{12, 28}, {28, 12}, {0, 10}, {50, 60}}) {
            ImageDocument doc;
            doc.reset(original);
            QVERIFY(doc.cut(vertical, range.first, range.second));
            const int start = qMin(range.first, range.second);
            const int removed = qAbs(range.first - range.second);
            QCOMPARE(doc.image().size(), QSize(80 - (vertical ? removed : 0), 60 - (vertical ? 0 : removed)));
            for (int y = 0; y < doc.image().height(); ++y)
                for (int x = 0; x < doc.image().width(); ++x)
                    QCOMPARE(doc.image().pixel(x, y), original.pixel(x + (vertical && x >= start ? removed : 0),
                                                                   y + (!vertical && y >= start ? removed : 0)));
        }
    }
}

void EditorTests::invalidCutsDoNotAlterHistory() {
    ImageDocument doc;
    doc.reset(pattern());
    QVERIFY(!doc.cut(true, 20, 20));
    QVERIFY(!doc.cut(false, -20, 100));
    QVERIFY(!doc.canUndo());
    QCOMPARE(doc.image(), pattern());
}

void EditorTests::undoRedoAndBranch() {
    ImageDocument doc;
    doc.reset(pattern());
    doc.cut(true, 10, 20);
    const QImage cut = doc.image();
    doc.annotate("rect", {4, 4}, {50, 40}, Qt::red);
    const QImage marked = doc.image();
    doc.undo(); QCOMPARE(doc.image(), cut);
    doc.undo(); QCOMPARE(doc.image(), pattern());
    doc.redo(); QCOMPARE(doc.image(), cut);
    doc.redo(); QCOMPARE(doc.image(), marked);
    doc.undo(); doc.cut(false, 20, 30);
    QVERIFY(!doc.canRedo());
    QCOMPARE(doc.image().size(), QSize(70, 50));
}

void EditorTests::annotationsAndText() {
    QImage white(320, 240, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    ImageDocument doc;
    doc.reset(white);
    QVERIFY(doc.annotate("rect", {20, 20}, {150, 100}, QColor("#22c55e")));
    QCOMPARE(doc.image().pixelColor(60, 20), QColor("#22c55e"));
    QCOMPARE(doc.image().pixelColor(60, 60), QColor(Qt::white));
    QVERIFY(doc.annotate("arrow", {180, 40}, {260, 140}, QColor("#ef4444")));
    QCOMPARE(doc.image().pixelColor(220, 90), QColor("#ef4444"));
    const QImage beforeText = doc.image();
    QVERIFY(doc.text({20, 150, 280, 80}, "Review this", Qt::red));
    QVERIFY(doc.image() != beforeText);
    doc.undo(); QCOMPARE(doc.image(), beforeText);
    QVERIFY(!doc.text({20, 20, 100, 100}, "  ", Qt::red));
}

void EditorTests::scaledGesturesAndClipboard() {
    QTemporaryDir dir;
    const QString path = dir.filePath("test.png");
    QVERIFY(pattern().save(path));
    EditorCanvas canvas;
    canvas.setWidth(432); canvas.setHeight(332);
    QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
    QCOMPARE(canvas.imageScale(), 5.0);
    canvas.setTool("cut");
    canvas.begin(66, 66); canvas.end(116, 66); // source columns 10..20
    QCOMPARE(canvas.imageWidth(), 70);
    QCOMPARE(canvas.imageHeight(), 60);
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(70, 60));
    canvas.undo(); QCOMPARE(canvas.imageWidth(), 80);
    canvas.begin(66, 66); canvas.move(116, 66); canvas.cancel(); canvas.end(116, 66);
    QCOMPARE(canvas.imageWidth(), 80);
    canvas.begin(0, 0); canvas.end(116, 66); // outside image
    QCOMPARE(canvas.imageWidth(), 80);
    canvas.redo(); QCOMPARE(canvas.imageWidth(), 70);
    QVERIFY(canvas.paste());
    QCOMPARE(canvas.imageWidth(), 70);
    QVERIFY(!canvas.canUndo());
}

void EditorTests::failedLoadPreservesImage() {
    QGuiApplication::clipboard()->setImage(pattern());
    EditorCanvas canvas;
    QVERIFY(canvas.paste());
    QSignalSpy errors(&canvas, &EditorCanvas::error);
    QVERIFY(!canvas.load(QUrl::fromLocalFile("/does/not/exist.png")));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(canvas.imageWidth(), 80);
}

void EditorTests::makePreviewFixture() {
    QImage image(1200, 760, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#f5f7fa"));
    QPainter p(&image);
    p.fillRect(0, 0, 1200, 90, QColor("#ffffff"));
    p.setPen(QColor("#172b40"));
    QFont font = QGuiApplication::font(); font.setPixelSize(28); font.setBold(true); p.setFont(font);
    p.drawText(48, 57, "Release checklist");
    font.setPixelSize(16); font.setBold(false); p.setFont(font);
    p.setPen(QColor("#617187"));
    p.drawText(48, 138, "A small example image for checking annotations.");
    const QStringList labels = {"Capture a region", "Remove the empty space", "Point to what matters", "Copy and get back to work"};
    for (int i = 0; i < labels.size(); ++i) {
        const int y = 190 + i * 125;
        p.fillRect(48, y, 1104, 86, Qt::white);
        p.setPen(QColor("#26394f"));
        font.setPixelSize(22); p.setFont(font);
        p.drawText(78, y + 52, labels[i]);
        p.setPen(QColor("#758398"));
        p.drawText(1010, y + 52, "Ready");
    }
    p.end();
    QVERIFY(image.save("preview.png"));
    if (qEnvironmentVariableIsEmpty("XSHOT_RENDER_PREVIEW")) return;
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", true);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QVariantList regions;
    for (int i = 0; i < 4; ++i) regions.append(image.copy(48, 190 + i * 125, 640, 86));
    QVERIFY(canvas->loadRegions(regions));
    QTRY_VERIFY(window->isVisible());
    QImage preview;
    QTRY_VERIFY(!(preview = window->grabWindow()).isNull());
    QVERIFY(preview.save("arrangement-preview.png"));
    canvas->annotate();
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("annotation-preview.png"));
    window->resize(860, 560);
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("annotation-preview-small.png"));
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().topLeft() + QPointF(80, 110) * canvas->imageScale();
    canvas->begin(textPoint.x(), textPoint.y());
    auto *activeText = window->findChild<QObject *>("annotationText");
    QVERIFY(activeText);
    activeText->setProperty("text", "Entry baseline\nSecond line");
    QTRY_COMPARE(activeText->property("topPadding").toReal(), 0.0);
    auto *activeFrame = window->findChild<QObject *>("annotationTextFrame");
    QVERIFY(activeFrame);
    QTRY_VERIFY(activeFrame->property("height").toReal() >= activeText->property("contentHeight").toReal());
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("text-entry-preview.png"));
    QVERIFY(QMetaObject::invokeMethod(window, "commitText"));
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("text-committed-preview.png"));
    canvas->adjustToolSize(120 * 48);
    const QPointF largeTextPoint = canvas->imageRect().topLeft() + QPointF(450, 20) * canvas->imageScale();
    canvas->begin(largeTextPoint.x(), largeTextPoint.y());
    QVERIFY(window->property("editingText").toBool());
    activeText->setProperty("text", "Large note");
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("text-entry-large-preview.png"));
    QVERIFY(QMetaObject::invokeMethod(window, "commitText"));
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("text-committed-large-preview.png"));
    canvas->arrange();
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save("arrangement-preview-small.png"));
    canvas->annotate();
    canvas->addText(30, 30, 300, 60, "Example annotation");
    QVERIFY(QMetaObject::invokeMethod(window, "arrangeRegions"));
    QTest::qWait(250);
    QVERIFY(window->grabWindow().save("rearrange-dialog-preview.png"));
    RegionSelector selector(image, QRect(0, 0, 860, 560));
    selector.setSelections(true, {{0, QRectF(34, 140, 320, 60)}}, 1);
    QVERIFY(selector.grab().save("selection-preview-small.png"));
}

void EditorTests::qmlLoadsAndPlacesText() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    // The offscreen platform cannot raise native windows; QML warnings still fail.
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(path));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    QObject *window = engine.rootObjects().first();
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->hasImage());
    QCOMPARE(canvas->imageWidth(), 80);
    canvas->setTool("text");
    const QPointF point = canvas->imageRect().topLeft() + QPointF(5, 5) * canvas->imageScale();
    canvas->begin(point.x(), point.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", "A\nB");
    QTRY_COMPARE(text->property("topPadding").toReal(), 0.0);
    auto *textFrame = window->findChild<QObject *>("annotationTextFrame");
    QVERIFY(textFrame);
    QTRY_VERIFY(textFrame->property("height").toReal() >= text->property("contentHeight").toReal());
    QVERIFY(QMetaObject::invokeMethod(window, "commitText"));
    QVERIFY(!window->property("editingText").toBool());
    QVERIFY(canvas->canUndo());
    QVERIFY(canvas->copy());
    QVERIFY(QGuiApplication::clipboard()->image() != pattern());
    const QImage copied = QGuiApplication::clipboard()->image();
    QVERIFY(QMetaObject::invokeMethod(window, "finish"));
    QVERIFY(!window->property("visible").toBool());
    QVERIFY(!canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->image(), copied);
    QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
    QVERIFY(window->property("visible").toBool());
    emit backend.regionsCaptured({pattern(), pattern(), pattern()});
    QVERIFY(canvas->arranging());
    QCOMPARE(canvas->regionCount(), 3);
    QCOMPARE(canvas->columns(), 2);
    canvas->setColumns(1);
    QCOMPARE(canvas->columns(), 1);
    canvas->annotate();
    QVERIFY(!canvas->arranging());
    QVERIFY(QMetaObject::invokeMethod(window, "finish"));
    QVERIFY(!canvas->hasImage());
    QCOMPARE(canvas->regionCount(), 0);
    const QString captureError = QStringLiteral("Allow screen recording in System Settings.");
    emit backend.error(captureError);
    emit backend.captureFinished(false);
    auto *dialog = window->findChild<QObject *>("captureErrorDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(dialog->property("message").toString(), captureError);
    QVERIFY(window->property("visible").toBool());
    QVERIFY(!window->property("shortcutsOn").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
}

void EditorTests::qmlToolbarPasteReplacesTextDraft() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QImage large(800, 600, QImage::Format_ARGB32_Premultiplied);
    large.fill(Qt::white);
    const QString sourcePath = temporary.filePath("large.png");
    QVERIFY(large.save(sourcePath));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(sourcePath));
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    auto *text = window->findChild<QObject *>("annotationText");
    auto *frame = window->findChild<QObject *>("annotationTextFrame");
    auto *paste = visualItem(window->contentItem(), "pasteImageButton");
    QVERIFY(canvas); QVERIFY(text); QVERIFY(frame); QVERIFY(paste);
    QTRY_COMPARE(canvas->imageWidth(), 800);
    canvas->setTool("text");
    const QPointF draftPoint = canvas->imageRect().topLeft() + QPointF(400, 400) * canvas->imageScale();
    canvas->begin(draftPoint.x(), draftPoint.y());
    QVERIFY(window->property("editingText").toBool());
    text->setProperty("text", "OLD DRAFT");
    const qreal oldY = window->property("textY").toReal();
    QVERIFY(oldY > 80);

    QGuiApplication::clipboard()->setText("no image");
    QVERIFY(QMetaObject::invokeMethod(paste, "clicked"));
    QCOMPARE(canvas->imageWidth(), 800);
    QVERIFY(window->property("editingText").toBool());
    QCOMPARE(text->property("text").toString(), QString("OLD DRAFT"));
    QCOMPARE(window->property("textY").toReal(), oldY);
    QVERIFY(!canvas->canUndo());

    QImage small(80, 60, QImage::Format_ARGB32_Premultiplied);
    small.fill(QColor("#51a3ce"));
    QGuiApplication::clipboard()->setImage(small);
    QVERIFY(QMetaObject::invokeMethod(paste, "clicked"));
    QCOMPARE(canvas->imageWidth(), 80);
    QCOMPARE(canvas->imageHeight(), 60);
    QVERIFY(!window->property("editingText").toBool());
    QCOMPARE(text->property("text").toString(), QString());
    QCOMPARE(window->property("textX").toReal(), 0.0);
    QCOMPARE(window->property("textY").toReal(), 0.0);
    QCOMPARE(window->property("textWidth").toReal(), 0.0);
    QVERIFY(frame->property("height").toReal() >= 0);
    QVERIFY(window->property("shortcutsOn").toBool());
    QVERIFY(canvas->hasActiveFocus());
    QVERIFY(!canvas->canUndo());
    QVERIFY(canvas->copy());
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(small.format()), small);
    const QString savedPath = canvas->saveTo(temporary.path());
    QVERIFY(!savedPath.isEmpty());
    QCOMPARE(QImage(savedPath).convertToFormat(small.format()), small);

    QVERIFY(canvas->loadRegions({small, small}));
    QVERIFY(canvas->arranging());
    QGuiApplication::clipboard()->setText("still no image");
    QVERIFY(QMetaObject::invokeMethod(paste, "clicked"));
    QVERIFY(canvas->arranging());
    QCOMPARE(canvas->regionCount(), 2);
    QGuiApplication::clipboard()->setImage(small);
    QVERIFY(QMetaObject::invokeMethod(paste, "clicked"));
    QVERIFY(!canvas->arranging());
    QCOMPARE(canvas->regionCount(), 0);
    QCOMPARE(canvas->imageWidth(), 80);
}

#ifdef Q_OS_WIN
void EditorTests::windowsRecordingOverlayExclusionFailure() {
    QGuiApplication::setFont(QFont("Segoe UI"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    for (const auto mode : {Backend::ExclusionTestMode::ForceLegacyVersion,
                            Backend::ExclusionTestMode::ForceFailure}) {
        Backend backend;
        backend.m_exclusionTestMode = mode;
        backend.m_pendingRecording = true;
        backend.m_recordingRegion = QRect(100, 100, 140, 100);
        backend.m_recordingIndicatorGeometry = QRect(30, 30, 300, 300);
        backend.m_pendingSource.platform = recording::Platform::Windows;
        backend.m_pendingSource.pixelRegion = backend.m_recordingRegion;
        QSignalSpy errors(&backend, &Backend::error);
        QSignalSpy started(&backend, &Backend::recordingProcessStarted);
        QSignalSpy ready(&backend, &Backend::recordingReady);
        QSignalSpy saved(&backend, &Backend::recordingSaved);
        QGuiApplication::clipboard()->setText("keep clipboard on exclusion failure");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("initialImage", QUrl());
        engine.rootContext()->setContextProperty("startInBackground", true);
        engine.rootContext()->setContextProperty("showOnStart", false);
        engine.load(QUrl("qrc:/Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *indicator = window->findChild<QQuickWindow *>("recordingStartupIndicator");
        auto *controls = window->findChild<QQuickWindow *>("recordingWindow");
        auto *dialog = window->findChild<QObject *>("captureErrorDialog");
        QVERIFY(indicator); QVERIFY(controls); QVERIFY(dialog);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 3000);
        const QString message = errors.first().first().toString();
        QVERIFY(message.contains(mode == Backend::ExclusionTestMode::ForceLegacyVersion
                                 ? "version 2004" : "exclude xshot"));
        QVERIFY(!backend.recording());
        QVERIFY(!backend.startingRecording());
        QVERIFY(!backend.recordingProtectionPending());
        QTRY_VERIFY(!indicator->isVisible() && !controls->isVisible());
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(started.size(), 0);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(saved.size(), 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), "keep clipboard on exclusion failure");
    }
    Backend canceled;
    canceled.m_pendingRecording = true;
    QSignalSpy canceledSignal(&canceled, &Backend::recordingCanceled);
    canceled.cancelRecording();
    QCOMPARE(canceledSignal.size(), 1);
    QVERIFY(!canceled.recording());
    QVERIFY(!canceled.beginProtectedRecording(nullptr, nullptr));
}
#endif

void EditorTests::qmlWheelSizes() {
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QImage image(480, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QTemporaryDir dir;
    const QString path = dir.filePath("wheel.png");
    QVERIFY(image.save(path));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(path));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->hasImage());
    window->show();
    QTRY_VERIFY(window->isVisible());
    const auto wheelAt = [window](QPointF local, int delta) {
        QWheelEvent event(local, window->mapToGlobal(local.toPoint()), {}, {0, delta},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event);
        QCoreApplication::processEvents();
    };
    const auto point = [canvas](QPointF source) {
        return canvas->imageRect().topLeft() + source * canvas->imageScale();
    };
    canvas->setTool("rect");
    wheelAt(point({100, 250}), 120);
    QCOMPARE(canvas->strokeWidth(), 5);
    canvas->setTool("arrow");
    wheelAt(point({100, 250}), -120);
    QCOMPARE(canvas->strokeWidth(), 4);
    canvas->setTool("text");
    wheelAt(point({100, 250}), 120);
    QCOMPARE(canvas->textSize(), 25);
    canvas->begin(point({180, 180}).x(), point({180, 180}).y());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", QString(30, 'A').replace("A", "A\n"));
    auto *textFrame = window->findChild<QObject *>("annotationTextFrame");
    QVERIFY(textFrame);
    QTRY_VERIFY(text->property("contentHeight").toReal() > textFrame->property("height").toReal());
    const QPointF inside(textFrame->property("x").toReal() + 20,
                         textFrame->property("y").toReal() + textFrame->property("height").toReal() / 2);
    auto *flickable = textFrame->property("contentItem").value<QObject *>();
    QVERIFY(flickable);
    const qreal scrollStart = flickable->property("contentY").toReal();
    wheelAt(inside, scrollStart > 0 ? 120 : -120);
    QCOMPARE(canvas->textSize(), 25);
    QTRY_VERIFY(flickable->property("contentY").toReal() != scrollStart);
    wheelAt(inside, scrollStart > 0 ? -120 : 120);
    QCOMPARE(canvas->textSize(), 25);
    wheelAt(inside, 12000);
    wheelAt(inside, 120);
    wheelAt(inside, -12000);
    wheelAt(inside, -120);
    QCOMPARE(canvas->textSize(), 25); // Scroll limits never resize the note.
    wheelAt(point({20, 300}), 120);
    QCOMPARE(canvas->textSize(), 26);
    QCOMPARE(text->property("text").toString(), QString(30, 'A').replace("A", "A\n"));
    QVERIFY(text->property("activeFocus").toBool());
}

void EditorTests::qmlSaveAndClose() {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "offscreen")
        QSKIP("File-manager reveal requires an interactive Windows desktop");
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir source;
    const QString inputPath = source.filePath("input.png");
    QVERIFY(pattern().save(inputPath));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(inputPath));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->hasImage());
    window->show();
    QTRY_VERIFY(window->isVisible());
    auto *saveButton = window->findChild<QObject *>("saveButton");
    auto *copyButton = window->findChild<QQuickItem *>("copyButton");
    auto *arrangeButton = window->findChild<QObject *>("saveArrangementButton");
    auto *arrangeCopyButton = window->findChild<QQuickItem *>("copyArrangementButton");
    QVERIFY(saveButton && copyButton && arrangeButton && arrangeCopyButton);
    QVERIFY(saveButton->property("visible").toBool());
    QVERIFY(!arrangeButton->property("visible").toBool());
    QCOMPARE(saveButton->property("text").toString(), QString("Save and close"));
    QCOMPARE(copyButton->property("text").toString(), QString("Copy and close"));
    QCoreApplication::processEvents();
    const qreal saveRight = saveButton->property("x").toReal() + saveButton->property("width").toReal();
    QVERIFY(copyButton->x() >= saveRight && copyButton->x() - saveRight <= 13);

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QVERIFY(!pictures.isEmpty());
    QDir savedDir(QDir(pictures).filePath("xshot"));
    QStringList known = savedDir.entryList({"xshot-*.png"}, QDir::Files);
    const auto newSavedFile = [&]() {
        for (const QString &name : savedDir.entryList({"xshot-*.png"}, QDir::Files)) {
            if (!known.contains(name)) { known.append(name); return savedDir.filePath(name); }
        }
        return QString();
    };
    QStringList created;
    QGuiApplication::clipboard()->setText("keep screenshot clipboard");
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().topLeft() + QPointF(5, 5) * canvas->imageScale();
    canvas->begin(textPoint.x(), textPoint.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", "A");
    QTest::keyClick(window, Qt::Key_S);
    QTRY_VERIFY(text->property("text").toString().contains('s'));
    QCOMPARE(text->property("text").toString().size(), 2);
    QVERIFY(canvas->hasImage());
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    const QString fromButton = newSavedFile();
    QVERIFY(!fromButton.isEmpty()); created.append(fromButton);
    QCOMPARE(QImage(fromButton).size(), pattern().size() * 3);
    QVERIFY(QImage(fromButton).convertToFormat(pattern().format()) != pattern());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep screenshot clipboard"));

    QVERIFY(canvas->load(QUrl::fromLocalFile(inputPath)));
    window->show();
    canvas->addText(4, 4, 70, 40, "Sharp", 16);
    QVERIFY(canvas->copy());
    const QImage annotatedCopy = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("keep screenshot clipboard");
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    const QString matchedFile = newSavedFile();
    QVERIFY(!matchedFile.isEmpty()); created.append(matchedFile);
    QCOMPARE(QImage(matchedFile).convertToFormat(annotatedCopy.format()), annotatedCopy);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep screenshot clipboard"));

    QVERIFY(canvas->load(QUrl::fromLocalFile(inputPath)));
    QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
    canvas->forceActiveFocus();
    QTRY_VERIFY(window->isActive());
    QTest::keyClick(window, Qt::Key_S);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    const QString fromShortcut = newSavedFile();
    QVERIFY(!fromShortcut.isEmpty()); created.append(fromShortcut);
    QCOMPARE(QImage(fromShortcut).convertToFormat(pattern().format()), pattern());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep screenshot clipboard"));

    emit backend.regionsCaptured({pattern(), pattern()});
    QVERIFY(canvas->arranging());
    window->show();
    QVERIFY(arrangeButton->property("visible").toBool());
    QVERIFY(!saveButton->property("visible").toBool());
    QCOMPARE(arrangeButton->property("text").toString(), QString("Save and close"));
    QCOMPARE(arrangeCopyButton->property("text").toString(), QString("Copy and close"));
    QCoreApplication::processEvents();
    const qreal arrangeSaveRight = arrangeButton->property("x").toReal() + arrangeButton->property("width").toReal();
    QVERIFY(arrangeCopyButton->x() >= arrangeSaveRight && arrangeCopyButton->x() - arrangeSaveRight <= 13);
    QVERIFY(canvas->copy());
    const QImage arranged = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("keep screenshot clipboard");
    QVERIFY(QMetaObject::invokeMethod(arrangeButton, "clicked"));
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    const QString fromArrange = newSavedFile();
    QVERIFY(!fromArrange.isEmpty()); created.append(fromArrange);
    QCOMPARE(QImage(fromArrange).convertToFormat(arranged.format()), arranged);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep screenshot clipboard"));
    QTest::qWait(250);
    for (const QString &path : created) QVERIFY(QFile::remove(path));
}

void EditorTests::qmlKeyboardCommands() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(path));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(window->isVisible() && canvas->hasImage());
    window->requestActivate();
    canvas->forceActiveFocus();
    QTRY_VERIFY(window->isActive());

    // The displayed accelerator and actual keyboard command stay together.
    const QList<QPair<QString, Qt::Key>> tools{
        {"cut", Qt::Key_X}, {"rect", Qt::Key_R}, {"highlight", Qt::Key_H}, {"text", Qt::Key_T},
        {"arrow", Qt::Key_A}, {"blur", Qt::Key_B}, {"erase", Qt::Key_E}};
    for (const auto &tool : tools) {
        auto *button = visualItem(window->contentItem(), "tool_" + tool.first);
        QVERIFY2(button, qPrintable("Missing tool button: " + tool.first));
        QCOMPARE(button->property("text").toString(), tool.first == "cut" ? QString("Cut") : QString());
        QTest::keyClick(window, tool.second);
        QTRY_COMPARE(canvas->tool(), tool.first);
        QVERIFY(button->property("checked").toBool());
    }
    QTest::keyClick(window, Qt::Key_G);
    QTRY_COMPARE(canvas->ink(), QColor("#22c55e"));
    QTest::keyClick(window, Qt::Key_B);
    QTRY_COMPARE(canvas->tool(), QString("blur"));
    QCOMPARE(canvas->ink(), QColor("#22c55e"));
    QTest::keyClick(window, Qt::Key_D);
    QTRY_COMPARE(canvas->ink(), QColor("#ef4444"));
    QCOMPARE(canvas->tool(), QString("blur"));
    auto *redButton = visualItem(window->contentItem(), "ink_D");
    QVERIFY(redButton);
    QVERIFY(redButton->property("checked").toBool());

    canvas->setTool("rect");
    const QPointF start = canvas->imageRect().topLeft() + QPointF(10, 10) * canvas->imageScale();
    const QPointF end = canvas->imageRect().topLeft() + QPointF(50, 40) * canvas->imageScale();
    canvas->begin(start.x(), start.y()); canvas->end(end.x(), end.y());
    QVERIFY(canvas->canUndo());
    QVERIFY(canvas->copy());
    const QImage expectedImage = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("not submitted");
    QTest::keyClick(window, Qt::Key_Return);
    QCoreApplication::processEvents();
    QVERIFY(window->isVisible()); QVERIFY(canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("not submitted"));
    auto *copyButton = visualItem(window->contentItem(), "copyButton");
    QVERIFY(copyButton);
    QCOMPARE(copyButton->property("text").toString(), QString("Copy and close"));
    QTest::keySequence(window, QKeySequence(QKeySequence::Copy));
    QCoreApplication::processEvents();
    QVERIFY(window->isVisible()); QVERIFY(canvas->hasImage());
    QTest::keyClick(window, Qt::Key_C);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->image(), expectedImage);

    QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
    QVERIFY(canvas->loadRegions({pattern(), pattern(), pattern(), pattern(), pattern(), pattern(), pattern()}));
    canvas->forceActiveFocus();
    QTRY_VERIFY(window->isActive());
    QCOMPARE(canvas->columns(), 2);
    QTest::keyClick(window, Qt::Key_Plus);
    QTRY_COMPARE(canvas->columns(), 3);
    QTest::keyClick(window, Qt::Key_Equal);
    QTRY_COMPARE(canvas->columns(), 4);
    for (int i = 0; i < 5; ++i) QTest::keyClick(window, Qt::Key_Plus);
    QTRY_COMPARE(canvas->columns(), 6);
    auto *moreColumns = visualItem(window->contentItem(), "moreColumnsButton");
    auto *fewerColumns = visualItem(window->contentItem(), "fewerColumnsButton");
    QVERIFY(moreColumns); QVERIFY(fewerColumns);
    QVERIFY(!moreColumns->isEnabled()); QVERIFY(fewerColumns->isEnabled());
    for (int i = 0; i < 8; ++i) QTest::keyClick(window, Qt::Key_Minus);
    QTRY_COMPARE(canvas->columns(), 1);
    QVERIFY(moreColumns->isEnabled()); QVERIFY(!fewerColumns->isEnabled());
    const QPoint plusCenter = moreColumns->mapToScene(QPointF(moreColumns->width() / 2, moreColumns->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, plusCenter);
    QTRY_COMPARE(canvas->columns(), 2);

    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!canvas->arranging());
    QVERIFY(canvas->hasImage()); QVERIFY(window->isVisible());
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().center();
    canvas->begin(textPoint.x(), textPoint.y());
    QVERIFY(window->property("editingText").toBool());
    QTest::keyClick(window, Qt::Key_C);
    QTest::keyClick(window, Qt::Key_H);
    auto *annotationText = window->findChild<QObject *>("annotationText");
    QVERIFY(annotationText);
    QTRY_COMPARE(annotationText->property("text").toString(), QString("ch"));
    QVERIFY(window->isVisible()); QVERIFY(canvas->hasImage());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("editingText").toBool());
    QTest::keyClick(window, Qt::Key_Plus);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->columns(), 2);
    QTest::keyClick(window, Qt::Key_Minus);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->columns(), 2); // Layout keys do not change an annotated image.

    // Returning to the layout must preserve edits until discard is explicitly chosen.
    canvas->addText(30, 30, 160, 40, "Keep this note");
    QVERIFY(canvas->canUndo());
    QVERIFY(canvas->copy());
    const QImage annotatedGrid = QGuiApplication::clipboard()->image();
    auto *backButton = visualItem(window->contentItem(), "backToArrangeButton");
    QVERIFY(backButton);
    QVERIFY(QMetaObject::invokeMethod(backButton, "clicked"));
    auto *dialog = window->findChild<QObject *>("rearrangeDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(!canvas->arranging());
    auto *keepButton = visualItem(window->contentItem(), "keepEditingButton");
    QVERIFY(keepButton);
    QVERIFY(QMetaObject::invokeMethod(keepButton, "clicked"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!canvas->arranging());
    QVERIFY(canvas->canUndo());
    QVERIFY(canvas->copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), annotatedGrid);
    QVERIFY(QMetaObject::invokeMethod(backButton, "clicked"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto *discardButton = visualItem(window->contentItem(), "discardEditsButton");
    QVERIFY(discardButton);
    QVERIFY(QMetaObject::invokeMethod(discardButton, "clicked"));
    QTRY_VERIFY(canvas->arranging());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!canvas->canUndo());
    QCOMPARE(canvas->regionCount(), 7);
    QVERIFY(canvas->copy());
    const QImage expectedGrid = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("not submitted");
    canvas->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_C);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(canvas->regionCount(), 0);
    QCOMPARE(QGuiApplication::clipboard()->image(), expectedGrid);
}

void EditorTests::qmlDismissal() {
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(path));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(window->isVisible() && canvas->hasImage());
    const auto reopen = [&] {
        QVERIFY(canvas->load(QUrl::fromLocalFile(path)));
        QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
        canvas->forceActiveFocus();
        QTRY_VERIFY(window->isActive());
    };
    QGuiApplication::clipboard()->setText("keep clipboard");
    canvas->forceActiveFocus();
    QTRY_VERIFY(window->isActive());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep clipboard"));

    QVERIFY(canvas->loadRegions({pattern(), pattern()}));
    QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
    canvas->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(canvas->regionCount(), 0);

    reopen();
    canvas->setTool("text");
    const QPointF center = canvas->imageRect().center();
    canvas->begin(center.x(), center.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", "draft");
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("editingText").toBool());
    QVERIFY(window->isVisible() && canvas->hasImage());
    QVERIFY(!canvas->canUndo());
    QCOMPARE(text->property("text").toString(), QString());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());

    reopen();
    canvas->begin(center.x(), center.y()); // Empty text drafts have the same two-Escape behavior.
    QVERIFY(window->property("editingText").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(window->isVisible() && canvas->hasImage());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());

    reopen();
    canvas->setTool("rect");
    canvas->begin(center.x(), center.y());
    canvas->move(center.x() + 30, center.y() + 30);
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(window->isVisible() && canvas->hasImage());
    QVERIFY(!canvas->canUndo());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());

    reopen();
    auto *dialog = window->findChild<QObject *>("captureErrorDialog");
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(window->isVisible() && canvas->hasImage());
    auto *arrangeDialog = window->findChild<QObject *>("rearrangeDialog");
    QVERIFY(arrangeDialog);
    QVERIFY(QMetaObject::invokeMethod(arrangeDialog, "open"));
    QTRY_VERIFY(arrangeDialog->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!arrangeDialog->property("visible").toBool());
    QVERIFY(window->isVisible() && canvas->hasImage());
    canvas->begin(center.x(), center.y());
    window->close();
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep clipboard"));
    reopen(); // The next screenshot session still opens normally.
    window->close();
    QVERIFY(QMetaObject::invokeMethod(window, "showEditor"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
}

void EditorTests::qmlRecordingControls() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Recording controls require FFmpeg and an unlocked desktop");
    QVERIFY2(!recording::toolPath("ffmpeg").isEmpty(), "Interactive recording tests require FFmpeg");
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression(".*"));
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend;
    QString error;
    connect(&backend, &Backend::error, this, [&error](const QString &message) { error = message; });
    QSignalSpy saved(&backend, &Backend::recordingSaved);
    const auto cleanup = qScopeGuard([&backend] {
        const QString ownClip = backend.recordingPath();
        if (backend.recording()) {
            backend.cancelRecording();
            QElapsedTimer timeout; timeout.start();
            while (backend.recording() && timeout.elapsed() < 5000) QTest::qWait(20);
        }
        if (!ownClip.isEmpty() && !backend.recording()) QFile::remove(ownClip);
    });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *controls = window->findChild<QQuickWindow *>("recordingWindow");
    auto *indicator = window->findChild<QQuickWindow *>("recordingStartupIndicator");
    QVERIFY(controls);
    QVERIFY(indicator);
    QVERIFY(!window->isVisible()); QVERIFY(!controls->isVisible());

    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    const QRect screen = QGuiApplication::primaryScreen()->geometry();
    marker.setGeometry(screen.x() + 80, screen.y() + 80, 420, 320);
    marker.setAutoFillBackground(true);
    QPalette palette; palette.setColor(QPalette::Window, QColor("#123456")); marker.setPalette(palette);
#ifdef Q_OS_MACOS
    QProcess markerProcess;
    const auto markerCleanup = qScopeGuard([&markerProcess] {
        if (markerProcess.state() != QProcess::NotRunning) {
            markerProcess.kill();
            markerProcess.waitForFinished(5000);
        }
    });
    const QString markerTool = QDir(QCoreApplication::applicationDirPath())
        .filePath("recording_marker/recording_marker");
    QVERIFY2(QFileInfo::exists(markerTool), "Run bin/test to build the external recording marker");
    markerProcess.start(markerTool, {QString::number(marker.x()), QString::number(marker.y()),
                                     QString::number(marker.width()), QString::number(marker.height())});
    QVERIFY(markerProcess.waitForReadyRead(5000));
    QVERIFY(markerProcess.readAllStandardOutput().contains("ready"));
#else
    marker.show(); marker.raise(); marker.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
#endif
    QTest::qWait(100);
    backend.capture(false, true);
    RegionSelector *selector = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<RegionSelector *>(widget);
            if (candidate && candidate->isVisible() && candidate->geometry().contains(marker.geometry().center())) {
                selector = candidate; return true;
            }
        }
        return false;
    })() || !error.isEmpty(), 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(selector);
    const QPoint start = marker.geometry().topLeft() + QPoint(20, 20) - selector->geometry().topLeft();
    const QPoint end = start + QPoint(240, 160);
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(selector, end);
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_VERIFY_WITH_TIMEOUT(indicator->isVisible() || !backend.startingRecording(), 1000);
    if (indicator->isVisible()) {
        const QRect bounds = QGuiApplication::primaryScreen()->geometry();
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        const QRect selected = backend.recordingRegion();
        const QRect expected = Backend::indicatorGeometry(selected, bounds, available);
        QCOMPARE(indicator->size(), expected.size());
        QVERIFY(bounds.contains(indicator->geometry()));
        QVERIFY(qAbs(indicator->x() - expected.x()) <= 1);
        QVERIFY(qAbs(indicator->y() - expected.y()) <= 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(controls->isVisible() || !error.isEmpty(), 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(backend.recording());
    QVERIFY(!window->isVisible());
    const QRect overlayExpected = Backend::controlsGeometry(QGuiApplication::primaryScreen()->availableGeometry());
    QCOMPARE(controls->size(), overlayExpected.size());
    QVERIFY(qAbs(controls->x() - overlayExpected.x()) <= 1);
    QVERIFY(qAbs(controls->y() - overlayExpected.y()) <= 1);
    QVERIFY(controls->flags() & Qt::FramelessWindowHint);
    auto *stop = controls->findChild<QObject *>("recordingStopButton");
    QVERIFY(stop);
    QCOMPARE(stop->property("text").toString(), QString("Stop"));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.startingRecording() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!indicator->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(backend.recordingElapsed() >= 1 || !error.isEmpty(), 6000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QImage preview;
    QTRY_VERIFY(!(preview = controls->grabWindow()).isNull());
    QVERIFY(preview.save("recording-controls.png"));
    controls->requestActivate();
    QTRY_VERIFY(controls->isActive());
    QGuiApplication::clipboard()->setText("waiting for recording");
    QTest::keySequence(controls, QKeySequence(QKeySequence::Copy));
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(saved.size(), 1);
    const QString path = saved.first().first().toString();
    const QFileInfo clip(path);
    QVERIFY(clip.isAbsolute()); QCOMPARE(clip.suffix(), QString("mp4"));
    QVERIFY(clip.exists() && clip.size() > 0);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("waiting for recording"));
    QProcess probe;
    probe.start(recording::toolPath("ffprobe"), {"-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", path});
    QVERIFY(probe.waitForFinished(5000));
    QCOMPARE(probe.exitCode(), 0);
    const auto dimensions = QString::fromUtf8(probe.readAllStandardOutput()).trimmed().split('x');
    QCOMPARE(dimensions.size(), 2);
    const int width = dimensions[0].toInt(), height = dimensions[1].toInt();
    QProcess decoder;
    decoder.start(recording::toolPath("ffmpeg"), {"-v", "error", "-i", path, "-frames:v", "5",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"});
    QVERIFY(decoder.waitForFinished(5000));
    QCOMPARE(decoder.exitCode(), 0);
    const QByteArray pixels = decoder.readAllStandardOutput();
    const qsizetype stride = qsizetype(width) * height * 3;
    QVERIFY(pixels.size() >= stride && stride > 0);
    for (qsizetype offset = 0; offset + stride <= pixels.size(); offset += stride) {
        const qsizetype center = offset + (qsizetype(height / 2) * width + width / 2) * 3;
        const QColor actual{uchar(pixels[center]), uchar(pixels[center + 1]), uchar(pixels[center + 2])};
        QVERIFY2(qAbs(actual.red() - 0x12) <= 16, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.green() - 0x34) <= 16, qPrintable(actual.name()));
        QVERIFY2(qAbs(actual.blue() - 0x56) <= 16, qPrintable(actual.name()));
    }
    QVERIFY(!backend.recording());
    QTRY_VERIFY(!controls->isVisible());
    QVERIFY(!window->isVisible());
    QTest::qWait(250); // Let Finder/Explorer select the completed file before cleanup.
    QVERIFY(QFile::remove(path));
#else
    QSKIP("Screen recording requires macOS or Windows");
#endif
}

void EditorTests::qmlRecordingHotkeyStop() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Recording hotkey requires an interactive desktop");
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend;
    QString error;
    connect(&backend, &Backend::error, this, [&error](const QString &message) { error = message; });
    QSignalSpy saved(&backend, &Backend::recordingSaved);
    const auto cleanup = qScopeGuard([&backend] {
        const QString clip = backend.recordingPath();
        if (backend.recording()) {
            backend.cancelRecording();
            QElapsedTimer timeout; timeout.start();
            while (backend.recording() && timeout.elapsed() < 5000) QTest::qWait(20);
        }
        if (!clip.isEmpty() && !backend.recording()) QFile::remove(clip);
    });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *controls = window->findChild<QQuickWindow *>("recordingWindow");
    QVERIFY(controls);
    QGuiApplication::clipboard()->setText("keep clipboard during hotkey stop");
    backend.capture(false, true);
    RegionSelector *selector = nullptr;
    const QRect display = QGuiApplication::primaryScreen()->geometry();
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<RegionSelector *>(widget);
            if (candidate && candidate->isVisible() && candidate->geometry().contains(display.center())) {
                selector = candidate; return true;
            }
        }
        return false;
    })() || !error.isEmpty(), 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(selector);
    const QPoint origin = display.center() - selector->geometry().topLeft();
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, origin);
    QTest::mouseMove(selector, origin + QPoint(240, 160));
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, origin + QPoint(240, 160));
    QVERIFY(backend.recording());
    QVERIFY(backend.startingRecording());
    QVERIFY(QMetaObject::invokeMethod(window, "hotkeyCapture"));
    QVERIFY(QMetaObject::invokeMethod(window, "hotkeyCapture")); // Repeated press is idempotent.
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !error.isEmpty(), 20000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep clipboard during hotkey stop"));
    QVERIFY(!backend.recording());
    QTRY_VERIFY(!controls->isVisible());
    QVERIFY(!window->isVisible());
    for (QWidget *widget : QApplication::topLevelWidgets())
        QVERIFY(!qobject_cast<RegionSelector *>(widget) || !widget->isVisible());
    const QString path = saved.first().first().toString();
    QVERIFY(QFileInfo(path).size() > 0);
    QTest::qWait(250);
    QVERIFY(QFile::remove(path));
#else
    QSKIP("Screen recording requires macOS or Windows");
#endif
}

void EditorTests::multipleRegionSelection() {
    RegionSelector selector(pattern(), QRect(0, 0, 400, 300));
    QSignalSpy single(&selector, &RegionSelector::selected);
    QSignalSpy multiple(&selector, &RegionSelector::multipleRequested);
    QSignalSpy added(&selector, &RegionSelector::regionAdded);
    QSignalSpy removed(&selector, &RegionSelector::regionRemoved);
    QSignalSpy accepted(&selector, &RegionSelector::accepted);
    QSignalSpy canceled(&selector, &RegionSelector::canceled);
    QTest::keyClick(&selector, Qt::Key_M);
    QCOMPARE(multiple.size(), 1);
    selector.setSelections(true, {}, 0);
    QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(150, 200));
    QCOMPARE(added.size(), 1);
    QCOMPARE(single.size(), 0);
    const QRectF area = added.first().first().toRectF();
    QCOMPARE(selector.crop(area), pattern().copy(10, 10, 20, 30));
    selector.setSelections(true, {{2, area}}, 3);
    QTest::mouseClick(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(75, 75));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.first().first().toInt(), 2); // Global order across monitors.
    QCOMPARE(added.size(), 1);
    QTest::keyClick(&selector, Qt::Key_C);
    QTest::keySequence(&selector, QKeySequence(QKeySequence::Copy));
    QCOMPARE(accepted.size(), 0);
    QTest::keyClick(&selector, Qt::Key_Return);
    QCOMPARE(accepted.size(), 1);
    QTest::keyClick(&selector, Qt::Key_Enter);
    QCOMPARE(accepted.size(), 2);
    QTest::keyClick(&selector, Qt::Key_Escape);
    QCOMPARE(canceled.size(), 1);
}

void EditorTests::captureToolbarInteraction() {
    RegionSelector selector(pattern().convertToFormat(QImage::Format_RGB32), QRect(0, 0, 860, 560));
    auto *toolbar = selector.findChild<QWidget *>("captureToolbar");
    auto *single = selector.findChild<QPushButton *>("singleCaptureButton");
    auto *multiple = selector.findChild<QPushButton *>("multipleCaptureButton");
    auto *video = selector.findChild<QPushButton *>("videoCaptureButton");
    auto *cancel = selector.findChild<QPushButton *>("cancelCaptureButton");
    auto *arrange = selector.findChild<QPushButton *>("arrangeCaptureButton");
    auto *instruction = selector.findChild<QLabel *>("captureInstruction");
    auto *count = selector.findChild<QLabel *>("captureCount");
    QVERIFY(toolbar && single && multiple && video && cancel && arrange && instruction && count);
    QCOMPARE(single->text(), QString("Region")); // No selector-local shortcut is bound.
    QCOMPARE(multiple->text(), QString("Multiple (M)"));
    QCOMPARE(video->text(), QString("Video (V)"));
    QCOMPARE(cancel->text(), QString("Cancel (Esc)"));
    QCOMPARE(arrange->text(), QString("Arrange (Enter)"));
    connect(&selector, &RegionSelector::multipleRequested, &selector, [&] { selector.setSelections(true, {}, 0); });
    connect(&selector, &RegionSelector::singleRequested, &selector, [&] { selector.setSelections(false, {}, 0); selector.setVideo(false); });
    QSignalSpy added(&selector, &RegionSelector::regionAdded);
    QSignalSpy accepted(&selector, &RegionSelector::accepted);
    QSignalSpy removed(&selector, &RegionSelector::regionRemoved);
    selector.show();
    QCoreApplication::processEvents();
    const QRect toolbarBounds = toolbar->geometry();
    const QRect modeBounds = multiple->geometry();
    const QRect instructionBounds = instruction->geometry();
    QVERIFY(single->isChecked());
    QVERIFY(arrange->isHidden());
    QTest::mouseClick(toolbar, Qt::LeftButton, Qt::NoModifier, QPoint(400, 50));
    QSignalSpy captured(&selector, &RegionSelector::selected);
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(500, 300));
    QCOMPARE(captured.size(), 0); // The toolbar background must not begin a drag either.
    QVERIFY(selector.grab().save("capture-toolbar-single.png"));
    QTest::mouseClick(multiple, Qt::LeftButton);
    QCoreApplication::processEvents();
    QVERIFY(multiple->isChecked());
    QVERIFY(!arrange->isHidden());
    QVERIFY(!arrange->isEnabled());
    QCOMPARE(toolbar->geometry(), toolbarBounds);
    QCOMPARE(multiple->geometry(), modeBounds);
    QCOMPARE(instruction->geometry(), instructionBounds);
    QTest::keyClick(&selector, Qt::Key_Return);
    QTest::mouseClick(arrange, Qt::LeftButton);
    QCOMPARE(accepted.size(), 0);
    QVERIFY(selector.grab().save("capture-toolbar-multiple-empty.png"));
    QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(80, 180));
    QTest::mouseMove(&selector, QPoint(360, 300));
    QCOMPARE(instruction->text(), QString("Drag to add regions."));
    QVERIFY(selector.grab().save("capture-toolbar-drag.png"));
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(360, 300));
    QCOMPARE(added.size(), 1);
    selector.setSelections(true, {{7, QRectF(80, 180, 280, 120)}}, 10);
    QCoreApplication::processEvents();
    QVERIFY(arrange->isEnabled());
    QCOMPARE(count->text(), QString("10 regions"));
    QCOMPARE(toolbar->geometry(), toolbarBounds);
    QCOMPARE(multiple->geometry(), modeBounds);
    QCOMPARE(instruction->geometry(), instructionBounds);
    QVERIFY(selector.grab().save("capture-toolbar-multiple.png"));
    auto *remove = selector.findChild<QToolButton *>();
    QVERIFY(remove && remove->isVisible());
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.first().first().toInt(), 7);
    QCOMPARE(added.size(), 1); // Controls never start a new capture.
    QTest::mouseClick(arrange, Qt::LeftButton);
    QCOMPARE(accepted.size(), 1);
    QTest::mouseClick(single, Qt::LeftButton);
    QVERIFY(single->isChecked());
    QVERIFY(arrange->isHidden());
    QVERIFY(remove->isHidden());
    selector.setVideo(true);
    QVERIFY(video->isChecked());
    QVERIFY(!multiple->isEnabled());
    QCOMPARE(toolbar->geometry(), toolbarBounds);
    selector.resize(400, 300);
    QCoreApplication::processEvents();
    QCOMPARE(cancel->text(), QString("Esc"));
    QCOMPARE(cancel->accessibleName(), QString("Cancel selection (Esc)"));
    QVERIFY(selector.grab().save("capture-toolbar-compact.png"));
    for (auto *control : toolbar->findChildren<QPushButton *>())
        QVERIFY(toolbar->rect().contains(control->geometry()));
    selector.setVideo(false);
    selector.setSelections(true, {}, 0);
    QCoreApplication::processEvents();
    QVERIFY(instruction->width() >= instruction->fontMetrics().horizontalAdvance(instruction->text()));
    for (auto *control : toolbar->findChildren<QPushButton *>())
        QVERIFY(control->width() >= control->fontMetrics().horizontalAdvance(control->text()) + 20);
}

void EditorTests::gridArrangementPreservesPixels() {
    QImage first = pattern();
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x) {
            QColor color = first.pixelColor(x, y); color.setAlpha(255); first.setPixelColor(x, y, color);
        }
    QImage second(32, 90, QImage::Format_RGB32); second.fill(Qt::red);
    QImage third(50, 20, QImage::Format_RGB32); third.fill(Qt::green);
    EditorCanvas canvas;
    canvas.setWidth(800); canvas.setHeight(600);
    QVERIFY(canvas.loadRegions({first, second, third}));
    QVERIFY(canvas.arranging());
    QCOMPARE(canvas.regionCount(), 3);
    QCOMPARE(canvas.columns(), 2);
    QVERIFY(canvas.copy());
    QImage grid = QGuiApplication::clipboard()->image();
    QCOMPARE(grid.size(), QSize(184, 182));
    QCOMPARE(grid.copy(24, 24, 80, 60), first);
    QCOMPARE(grid.pixelColor(128, 24), QColor(Qt::red));
    QCOMPARE(grid.pixelColor(24, 138), QColor(Qt::green));
    QCOMPARE(grid.pixelColor(0, 0), QColor("#f5f7fa"));
    canvas.setColumns(1);
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(128, 266));
    canvas.setColumns(2);
    const QPointF origin = canvas.imageRect().topLeft();
    const QPointF from = origin + QPointF(64, 54) * canvas.imageScale();
    const QPointF to = origin + QPointF(49, 148) * canvas.imageScale();
    canvas.begin(from.x(), from.y()); canvas.move(to.x(), to.y()); canvas.end(to.x(), to.y());
    QCOMPARE(canvas.selectedRegion(), 2);
    QVERIFY(canvas.copy());
    grid = QGuiApplication::clipboard()->image();
    QCOMPARE(grid.copy(24, 138, 80, 60), first);
    QCOMPARE(grid.pixelColor(24, 24), QColor(Qt::red));
    canvas.annotate();
    QVERIFY(!canvas.arranging());
    canvas.addText(24, 24, 100, 40, "Test");
    QVERIFY(canvas.canUndo());
    canvas.arrange();
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), grid);
    QVERIFY(!canvas.loadRegions({}));
    QCOMPARE(canvas.regionCount(), 3);
    canvas.removeRegion(2); canvas.removeRegion(1); canvas.removeRegion(0);
    QVERIFY(!canvas.hasImage());
    QVERIFY(!canvas.arranging());
    QCOMPARE(canvas.regionCount(), 0);
}

void EditorTests::regionSelectionScalesAndCancels() {
    QCOMPARE(RegionSelector::pixelRect(QRectF(100, 50, 200, 100), QSizeF(800, 600), QSize(1600, 1200)),
             QRect(200, 100, 400, 200));
    QCOMPARE(RegionSelector::pixelRect(QRectF(300, 150, -200, -100), QSizeF(800, 600), QSize(1600, 1200)),
             QRect(200, 100, 400, 200));
    QCOMPARE(RegionSelector::pixelRect(QRectF(-10, -10, 50, 50), QSizeF(800, 600), QSize(1000, 750)),
             QRect(0, 0, 50, 50));
    RegionSelector selector(pattern(), QRect(0, 0, 400, 300));
    QSignalSpy selected(&selector, &RegionSelector::selected);
    QSignalSpy canceled(&selector, &RegionSelector::canceled);
    QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(150, 200));
    QCOMPARE(selected.size(), 1);
    QCOMPARE(qvariant_cast<QImage>(selected.first().first()), pattern().copy(10, 10, 20, 30));
    QTest::keyClick(&selector, Qt::Key_Escape);
    QCOMPARE(canceled.size(), 1);
}

void EditorTests::windowsHotkeyRegistration() {
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Native hotkeys require an interactive Windows desktop; set XSHOT_INTERACTIVE_TESTS=1");
    {
        GlobalHotkey hotkey;
        QVERIFY2(hotkey.registered(), qPrintable(hotkey.description()));
        QSignalSpy activated(&hotkey, &GlobalHotkey::activated);
        GlobalHotkey conflict;
        QVERIFY(!conflict.registered());
        INPUT input[4]{};
        for (auto &key : input) key.type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_CONTROL;
        input[1].ki.wVk = VK_SNAPSHOT;
        input[2].ki.wVk = VK_SNAPSHOT; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
        input[3].ki.wVk = VK_CONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        QCOMPARE(SendInput(4, input, sizeof(INPUT)), UINT(4));
        QTRY_COMPARE(activated.size(), 1);
    }
    GlobalHotkey released;
    QVERIFY(released.registered());
#else
    QSKIP("Windows native hotkey test");
#endif
}

void EditorTests::desktopMultipleCapture() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Multiple-region capture requires a real desktop");
    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    marker.setGeometry(QGuiApplication::primaryScreen()->geometry().adjusted(80, 80, -80, -80));
    marker.setAutoFillBackground(true);
    QPalette palette; palette.setColor(QPalette::Window, QColor("#123456")); marker.setPalette(palette);
    marker.show(); marker.raise(); marker.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
    QTest::qWait(150);
    Backend backend;
    QSignalSpy batch(&backend, &Backend::regionsCaptured);
    QSignalSpy finished(&backend, &Backend::captureFinished);
    backend.capture();
    RegionSelector *selector = nullptr;
    const auto findSelector = [&selector] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *region = qobject_cast<RegionSelector *>(widget); region && region->isVisible()) { selector = region; return true; }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findSelector(), 10000);
    QTest::keyClick(selector, Qt::Key_M);
    const QPoint center = selector->rect().center();
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, center - QPoint(80, 60));
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, center - QPoint(20, 20));
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, center + QPoint(20, 20));
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, center + QPoint(80, 60));
    QCOMPARE(batch.size(), 0);
    QTest::keySequence(selector, QKeySequence(QKeySequence::Copy));
    QCOMPARE(batch.size(), 0);
    QTest::keyClick(selector, Qt::Key_Return);
    QTRY_COMPARE(batch.size(), 1);
    QCOMPARE(finished.size(), 1);
    QVERIFY(finished.first().first().toBool());
    const QVariantList images = batch.first().first().toList();
    QCOMPARE(images.size(), 2);
    for (const auto &value : images) {
        const QImage image = qvariant_cast<QImage>(value);
        QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor("#123456"));
    }
    QVERIFY(!backend.capturing());
    backend.capture(true);
    selector = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(findSelector(), 10000);
    QTest::keyClick(selector, Qt::Key_Return); // Empty multi-selection stays open.
    QVERIFY(backend.capturing());
    QTest::keyClick(selector, Qt::Key_Escape);
    QCOMPARE(finished.size(), 2);
    QVERIFY(!finished.last().first().toBool());
#else
    QSKIP("Screen capture requires macOS or Windows");
#endif
}

void EditorTests::windowsDesktopCapture() {
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Real screen capture requires an interactive Windows desktop");
    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    marker.setGeometry(QGuiApplication::primaryScreen()->geometry().adjusted(80, 80, -80, -80));
    marker.setAutoFillBackground(true);
    QPalette palette; palette.setColor(QPalette::Window, QColor("#123456"));
    marker.setPalette(palette);
    marker.show(); marker.raise(); marker.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
    QTest::qWait(300);
    Backend backend;
    QSignalSpy finished(&backend, &Backend::captureFinished);
    QImage captured;
    connect(&backend, &Backend::captured, this, [&captured](const QUrl &url) { captured.load(url.toLocalFile()); });
    // Cover the marker, then hide immediately before capture, like the editor.
    // Removing the fixed delay must not capture this stale window.
    QWidget cover;
    cover.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    cover.setGeometry(marker.geometry());
    cover.setAutoFillBackground(true);
    QPalette coverPalette; coverPalette.setColor(QPalette::Window, Qt::red);
    cover.setPalette(coverPalette);
    cover.show(); cover.raise();
    QVERIFY(QTest::qWaitForWindowExposed(&cover));
    QTest::qWait(100);
    cover.hide();
    backend.capture();
    RegionSelector *selector = nullptr;
    const auto findSelector = [&selector] {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *region = qobject_cast<RegionSelector *>(widget); region && region->isVisible()) { selector = region; return true; }
        return false;
    };
    QTRY_VERIFY(findSelector());
    const QPoint center = selector->rect().center();
    QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, center - QPoint(40, 30));
    QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 30));
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished.first().first().toBool());
    QVERIFY(!captured.isNull());
    QCOMPARE(captured.pixelColor(captured.width() / 2, captured.height() / 2), QColor("#123456"));
    QVERIFY(captured.save("native-capture.png"));
    QVERIFY(!backend.capturing());
    backend.capture();
    selector = nullptr;
    QTRY_VERIFY(findSelector());
    QTest::keyClick(selector, Qt::Key_Escape);
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!finished.last().first().toBool());
#else
    QSKIP("Windows desktop capture test");
#endif
}

void EditorTests::windowsCaptureLatency() {
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("XSHOT_INTERACTIVE_TESTS"))
        QSKIP("Capture latency requires an interactive Windows desktop");
    class PaintProbe : public QObject {
    public:
        QElapsedTimer timer;
        qint64 firstPaintMs = -1;
        QPointer<RegionSelector> selector;
        bool eventFilter(QObject *object, QEvent *event) override {
            if (firstPaintMs < 0 && event->type() == QEvent::Paint) {
                if (auto *region = qobject_cast<RegionSelector *>(object)) {
                    selector = region;
                    // Measure after painting returns, when input can be handled.
                    QTimer::singleShot(0, this, [this] {
                        if (firstPaintMs < 0) firstPaintMs = timer.elapsed();
                    });
                }
            }
            return false;
        }
    } probe;
    Backend backend;
    // Include the real QML capture/hide path used by the resident app.
    qmlRegisterType<EditorCanvas>("XShot", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    QObject *window = engine.rootObjects().first();
    GlobalHotkey hotkey;
    QVERIFY2(hotkey.registered(), qPrintable(hotkey.description()));
    connect(&hotkey, &GlobalHotkey::activated, window, [window] {
        QMetaObject::invokeMethod(window, "capture");
    });
    qApp->installEventFilter(&probe);
    for (int attempt = 0; attempt < 5; ++attempt) {
        probe.firstPaintMs = -1;
        probe.selector.clear();
        probe.timer.start();
        INPUT input[4]{};
        for (auto &key : input) key.type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_CONTROL;
        input[1].ki.wVk = VK_SNAPSHOT;
        input[2].ki.wVk = VK_SNAPSHOT; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
        input[3].ki.wVk = VK_CONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        QCOMPARE(SendInput(4, input, sizeof(INPUT)), UINT(4));
        QTRY_VERIFY_WITH_TIMEOUT(probe.firstPaintMs >= 0, 5000);
        qInfo("Hotkey to painted selector, attempt %d: %lld ms", attempt + 1, probe.firstPaintMs);
        QVERIFY(probe.selector);
        QTest::keyClick(probe.selector, Qt::Key_Escape);
        QVERIFY(!backend.capturing());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(100);
    }
    qApp->removeEventFilter(&probe);
    for (QScreen *screen : QGuiApplication::screens()) {
        QElapsedTimer timer; timer.start();
        const QImage image = screen->grabWindow(0).toImage();
        QVERIFY(!image.isNull());
        qInfo("Raw screen grab %dx%d: %lld ms", image.width(), image.height(), timer.elapsed());
    }
#else
    QSKIP("Windows capture latency test");
#endif
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
