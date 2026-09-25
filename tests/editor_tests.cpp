#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QApplication>
#include <QScreen>
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
    void regionSelectionScalesAndCancels();
    void windowsHotkeyRegistration();
    void windowsDesktopCapture();
    void makePreviewFixture();
};

static QImage pattern() {
    QImage image(80, 60, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x * 3, y * 4, 60, 200));
    return image;
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
    const QPointF point = canvas->imageRect().center();
    canvas->begin(point.x(), point.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", "Review");
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

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
