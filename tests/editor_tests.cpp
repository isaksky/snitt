#include <QtTest>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QApplication>
#include <QAccessible>
#include <QScreen>
#include <QTimer>
#include <QFileInfo>
#include <QDate>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QWheelEvent>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QSet>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include "backend.h"
#include "regionselector.h"
#include "globalhotkey.h"
#include "appsettings.h"
#include "annotationfont.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "imagedocument.h"
#include "editorcanvas.h"
#include "playbackclock.h"

class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qmlRegisterType<PlaybackClock>("Snitt", 1, 0, "PlaybackClock"); }
    void cutsJoinExactPixels();
    void invalidCutsDoNotAlterHistory();
    void undoRedoAndBranch();
    void annotationsAndText();
    void scaledGesturesAndClipboard();
    void failedLoadPreservesImage();
    void qmlLoadsAndPlacesText();
    void qmlAnnotationToolbarResponsiveLayout();
    void qmlPasteReplacesTextDraft();
    void qmlResizeTextDraft();
    void qmlWheelSizes();
    void qmlSaveAndClose();
    void qmlKeyboardCommands();
    void qmlDismissal();
    void qmlRecordingControls();
    void qmlRecordingHotkeyStop_data();
    void qmlRecordingHotkeyStop();
    void qmlRecordingReview();
    void qmlTrimBarSeeking();
    void regionSelectionScalesAndCancels();
    void multipleRegionSelection();
    void captureToolbarInteraction();
    void settingsRegionToolbarHintsLive();
    void gridArrangementPreservesPixels();
    void desktopMultipleCapture();
    void windowsHotkeyRegistration();
    void windowsDesktopCapture();
    void windowsCaptureLatency();
    void settingsCreateDefaultsAndPreserveEdits();
    void settingsReloadsAtomicEditsAndRecovers();
    void settingsRejectConflictsAndRollbackNativeHotkey();
    void settingsUpdateEditorShortcutsHintsAndColorsLive();
    void settingsShortcutAlternativesAndDisable();
    void settingsRejectMalformedAndNonScalarValues();
    void settingsPathsRoundTripSpecialCharacters();
    void macSettingsUseCommandActions();
    void macSettingsNativeShortcutAgreement();
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

static QString accessibleName(QQuickItem *item) {
    auto *accessible = QAccessible::queryAccessibleInterface(item);
    return accessible ? accessible->text(QAccessible::Name) : QString();
}

static AppSettings &isolatedSettings() {
    static QTemporaryDir *directory = new QTemporaryDir;
    static AppSettings *settings = [] {
        AppSettings::Paths paths;
        paths.settingsFile = directory->filePath("config/settings.ini");
        paths.guideFile = directory->filePath("config/settings-format.md");
        paths.defaultPicturesRoot = directory->filePath("Pictures/snitt");
        paths.defaultVideosRoot = directory->filePath("Videos/snitt");
        return new AppSettings(paths);
    }();
    Q_ASSERT(directory->isValid());
    return *settings;
}

static AppSettings::Paths settingsPaths(const QString &directory) {
    AppSettings::Paths paths;
    paths.settingsFile = QDir(directory).filePath("config/settings.ini");
    paths.guideFile = QDir(directory).filePath("config/settings-format.md");
    paths.defaultPicturesRoot = QDir(directory).filePath("Pictures/snitt");
    paths.defaultVideosRoot = QDir(directory).filePath("Videos/snitt");
    return paths;
}

static QByteArray readBytes(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

static bool atomicWrite(const QString &path, const QByteArray &bytes) {
    QElapsedTimer timer;
    timer.start();
    QString error;
    do {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            qInfo().noquote() << "Settings fixture write failed:" << file.errorString();
            return false;
        }
        if (file.commit()) return true;
        error = file.errorString();
#ifdef Q_OS_WIN
        // Windows may briefly retain a handle after a watched file is replaced.
        // Retry only staging the fixture; every settings assertion still runs.
        QTest::qWait(20);
#else
        break;
#endif
    } while (timer.elapsed() < 1000);
    qInfo().noquote() << "Settings fixture replacement failed:" << error;
    return false;
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
    if (qEnvironmentVariableIsEmpty("SNITT_RENDER_PREVIEW")) return;
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    canvas->adjustToolSize(120 * 24);
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
    RegionSelector selector(image, QRect(0, 0, 860, 560), &isolatedSettings());
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
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    QCOMPARE(canvas->annotationFontFamily(), annotationfont::family());
    canvas->setTool("text");
    const QPointF point = canvas->imageRect().topLeft() + QPointF(5, 5) * canvas->imageScale();
    canvas->begin(point.x(), point.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    QCOMPARE(QQmlProperty(text, QStringLiteral("font.family")).read().toString(), annotationfont::family());
    text->setProperty("text", "Impact\nwraps");
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
    QCOMPARE(copied.size(), pattern().size() * 3);
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

void EditorTests::qmlAnnotationToolbarResponsiveLayout() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support (?:raise|propagateSizeHints)\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir source;
    const QString imagePath = source.filePath("input.png");
    QVERIFY(pattern().save(imagePath));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(imagePath));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    QVERIFY(canvas);
    QTRY_VERIFY(canvas->hasImage());
    emit backend.regionsCaptured({pattern(), pattern(), pattern()});
    QTRY_VERIFY(canvas->arranging());
    QCOMPARE(canvas->regionCount(), 3);
    canvas->setColumns(2);
    canvas->annotate();
    canvas->addText(4, 4, 60, 30, "Combined", 14);
    const bool virtualDisplay = QGuiApplication::platformName() == "offscreen";
    const QRect desktop = QGuiApplication::primaryScreen()->availableGeometry();
    if (!virtualDisplay)
        window->resize(qMin(1100, desktop.width() - 32), qMin(760, desktop.height() - 64));
    window->show();
    QTRY_VERIFY(window->isVisible());

    auto *toolbar = visualItem(window->contentItem(), "annotationToolbar");
    auto *tools = visualItem(window->contentItem(), "annotationToolCluster");
    auto *modeGroup = visualItem(window->contentItem(), "modeGroup");
    auto *finishGroup = visualItem(window->contentItem(), "finishGroup");
    auto *modeHeading = visualItem(window->contentItem(), "modeHeading");
    auto *finishHeading = visualItem(window->contentItem(), "finishHeading");
    auto *modeButtons = visualItem(window->contentItem(), "modeSegmentedControl");
    auto *finishButtons = visualItem(window->contentItem(), "finishButtons");
    auto *hint = visualItem(window->contentItem(), "annotationHint");
    auto *firstTool = visualItem(window->contentItem(), "tool_cut");
    auto *undoButton = visualItem(window->contentItem(), "undoButton");
    auto *redoButton = visualItem(window->contentItem(), "redoButton");
    auto *saveButton = visualItem(window->contentItem(), "saveButton");
    auto *copyButton = visualItem(window->contentItem(), "copyButton");
    QVERIFY(toolbar && tools && modeGroup && finishGroup && modeHeading && finishHeading);
    QVERIFY(modeButtons && finishButtons && hint && firstTool && undoButton && redoButton);
    QVERIFY(saveButton && copyButton);
    QVERIFY(!visualItem(window->contentItem(), "pasteImageButton"));
    const auto centerX = [toolbar](QQuickItem *item) {
        return item->mapToItem(toolbar, QPointF(item->width() / 2, item->height() / 2)).x();
    };
    const auto leftX = [toolbar](QQuickItem *item) {
        return item->mapToItem(toolbar, QPointF(0, 0)).x();
    };
    const auto rightX = [toolbar](QQuickItem *item) {
        return item->mapToItem(toolbar, QPointF(item->width(), 0)).x();
    };
    const auto topY = [toolbar](QQuickItem *item) {
        return item->mapToItem(toolbar, QPointF(0, 0)).y();
    };
    const auto bottomY = [toolbar](QQuickItem *item) {
        return item->mapToItem(toolbar, QPointF(0, item->height())).y();
    };
    const auto checkToolbar = [&](int width, bool singleLine, const QString &previewName) {
        // Native windows can apply a changed minimum size asynchronously.
        QTRY_VERIFY(([&] {
            window->resize(width, window->height());
            return toolbar->width() == qreal(width - 32);
        })());
        QTRY_COMPARE(toolbar->property("singleLine").toBool(), singleLine);
        // Wrapping the hint and resizing the header settle on the next frame.
        QTRY_VERIFY(bottomY(hint) <= toolbar->height());
        QCoreApplication::processEvents();
        QVERIFY(qAbs(centerX(modeHeading) - centerX(modeButtons)) <= 1.0);
        QVERIFY(qAbs(centerX(finishHeading) - centerX(finishButtons)) <= 1.0);
        QVERIFY(topY(modeHeading) > bottomY(modeButtons));
        QVERIFY(topY(finishHeading) > bottomY(finishButtons));
        QCOMPARE(saveButton->height(), 40.0);
        QCOMPARE(copyButton->height(), saveButton->height());
        QCOMPARE(topY(copyButton), topY(saveButton));
        QCOMPARE(topY(undoButton), topY(firstTool));
        QCOMPARE(topY(redoButton), topY(firstTool));
        QVERIFY(leftX(tools) >= 0);
        QVERIFY(rightX(tools) <= toolbar->width());
        QVERIFY(leftX(modeGroup) >= 0);
        QVERIFY(rightX(modeGroup) <= toolbar->width());
        QVERIFY(leftX(finishGroup) >= 0);
        QVERIFY(rightX(finishGroup) <= toolbar->width());
        QVERIFY(rightX(modeGroup) < leftX(hint));
        QVERIFY(rightX(hint) < leftX(finishGroup));
        QVERIFY(hint->width() >= 180);
        if (singleLine) {
            QVERIFY(rightX(tools) <= leftX(modeGroup));
            QVERIFY(qAbs(bottomY(firstTool) - bottomY(modeButtons)) <= 1.0);
            QVERIFY(qAbs(bottomY(firstTool) - bottomY(finishButtons)) <= 1.0);
        } else {
            QVERIFY(tools->mapToItem(toolbar, QPointF(0, tools->height())).y() <
                    modeButtons->mapToItem(toolbar, QPointF(0, 0)).y());
        }
        const QString previewDir = qEnvironmentVariable("SNITT_TOOLBAR_PREVIEW_DIR",
                                                         QDir::current().filePath("toolbar-layout-previews"));
        QVERIFY(QDir().mkpath(previewDir));
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(QDir(previewDir).filePath(previewName)));
    };

    const QStringList toolNames{"tool_cut", "tool_rect", "tool_highlight", "tool_text",
                                "tool_arrow", "tool_blur", "tool_erase"};
    qreal previousRight = -1;
    for (const QString &name : toolNames) {
        auto *tool = visualItem(window->contentItem(), name);
        QVERIFY2(tool, qPrintable("Missing icon tool " + name));
        QVERIFY(tool->property("text").toString().isEmpty());
        QVERIFY(!accessibleName(tool).isEmpty());
        QCOMPARE(tool->property("focusPolicy").toInt(), int(Qt::TabFocus));
        auto *shortcut = visualItem(window->contentItem(), "shortcut_" + name.mid(5));
        QVERIFY(shortcut);
        QVERIFY(!shortcut->property("text").toString().isEmpty());
        QVERIFY(topY(shortcut) > bottomY(tool));
        QVERIFY(qAbs(centerX(shortcut) - centerX(tool)) <= 1.0);
        const qreal x = leftX(tool);
        if (previousRight >= 0) QVERIFY(x >= previousRight);
        previousRight = rightX(tool);
    }
    QCOMPARE(saveButton->property("text").toString(), QString("Save"));
    QCOMPARE(copyButton->property("text").toString(), QString("Copy"));
    QCOMPARE(modeHeading->property("text").toString(), QString("Color Mode"));
    QCOMPARE(finishHeading->property("text").toString(), QString("Finish (will close)"));
    QCOMPARE(window->minimumWidth(), 860);

    if (virtualDisplay || desktop.width() >= 1132)
        checkToolbar(1100, true, "annotation-combined-1100.png");
    checkToolbar(860, false, "annotation-combined-860.png");
    window->setMinimumWidth(600); // Exercise the fallback without changing the supported minimum.
    checkToolbar(600, false, "annotation-combined-wrapped.png");
    QCOMPARE(hint->property("text").toString(), window->property("hint").toString());
    window->setProperty("notice", "Could not save the screenshot. Choose another folder.");
    QTRY_COMPARE(hint->property("text").toString(), window->property("notice").toString());
    window->setProperty("notice", "");

    QVERIFY(undoButton->isEnabled());
    QVERIFY(!redoButton->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(undoButton, "clicked"));
    QVERIFY(!canvas->canUndo());
    QVERIFY(redoButton->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(redoButton, "clicked"));
    QVERIFY(canvas->canUndo());

    auto *goodButton = visualItem(window->contentItem(), "ink_good");
    auto *badButton = visualItem(window->contentItem(), "ink_bad");
    QVERIFY(goodButton && badButton);
    QCOMPARE(accessibleName(goodButton), QString("Good mode"));
    QCOMPARE(accessibleName(badButton), QString("Bad mode"));
    QCOMPARE(goodButton->property("focusPolicy").toInt(), int(Qt::TabFocus));
    QCOMPARE(badButton->property("focusPolicy").toInt(), int(Qt::TabFocus));
    QCOMPARE(saveButton->property("focusPolicy").toInt(), int(Qt::TabFocus));
    QCOMPARE(copyButton->property("focusPolicy").toInt(), int(Qt::TabFocus));
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().topLeft() + QPointF(12, 12) * canvas->imageScale();
    canvas->begin(textPoint.x(), textPoint.y());
    QVERIFY(window->property("editingText").toBool());
    QVERIFY(!undoButton->isEnabled());
    QVERIFY(!redoButton->isEnabled());
    auto *draft = window->findChild<QObject *>("annotationText");
    QVERIFY(draft);
    draft->setProperty("text", "Mode draft");
    QVERIFY(QMetaObject::invokeMethod(badButton, "clicked"));
    QTRY_VERIFY(!window->property("editingText").toBool());
    QCOMPARE(canvas->tool(), QString("text"));
    QCOMPARE(canvas->colorMode(), QString("bad"));
    QVERIFY(badButton->property("checked").toBool());
    QVERIFY(!goodButton->property("checked").toBool());
    QVERIFY(QMetaObject::invokeMethod(goodButton, "clicked"));
    QCOMPARE(canvas->colorMode(), QString("good"));
    QCOMPARE(canvas->tool(), QString("text"));
    QVERIFY(goodButton->property("checked").toBool());
    QVERIFY(!badButton->property("checked").toBool());
}

void EditorTests::qmlPasteReplacesTextDraft() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QImage large(800, 600, QImage::Format_ARGB32_Premultiplied);
    large.fill(Qt::white);
    const QString sourcePath = temporary.filePath("large.png");
    QVERIFY(large.save(sourcePath));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    QVERIFY(canvas); QVERIFY(text); QVERIFY(frame);
    QTRY_COMPARE(canvas->imageWidth(), 800);
    canvas->setTool("text");
    const QPointF draftPoint = canvas->imageRect().topLeft() + QPointF(400, 400) * canvas->imageScale();
    canvas->begin(draftPoint.x(), draftPoint.y());
    QVERIFY(window->property("editingText").toBool());
    text->setProperty("text", "OLD DRAFT");
    const qreal oldY = window->property("textY").toReal();
    QVERIFY(oldY > 80);

    QGuiApplication::clipboard()->setText("no image");
    QVERIFY(QMetaObject::invokeMethod(window, "pasteImage"));
    QCOMPARE(canvas->imageWidth(), 800);
    QVERIFY(window->property("editingText").toBool());
    QCOMPARE(text->property("text").toString(), QString("OLD DRAFT"));
    QCOMPARE(window->property("textY").toReal(), oldY);
    QVERIFY(!canvas->canUndo());

    QImage small(80, 60, QImage::Format_ARGB32_Premultiplied);
    small.fill(QColor("#51a3ce"));
    QGuiApplication::clipboard()->setImage(small);
    QVERIFY(QMetaObject::invokeMethod(window, "pasteImage"));
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
    QVERIFY(QMetaObject::invokeMethod(window, "pasteImage"));
    QVERIFY(canvas->arranging());
    QCOMPARE(canvas->regionCount(), 2);
    QGuiApplication::clipboard()->setImage(small);
    QVERIFY(QMetaObject::invokeMethod(window, "pasteImage"));
    QVERIFY(!canvas->arranging());
    QCOMPARE(canvas->regionCount(), 0);
    QCOMPARE(canvas->imageWidth(), 80);
}

#ifdef Q_OS_WIN
void EditorTests::windowsRecordingOverlayExclusionFailure() {
    QGuiApplication::setFont(QFont("Segoe UI"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    for (const auto mode : {Backend::ExclusionTestMode::ForceLegacyVersion,
                            Backend::ExclusionTestMode::ForceFailure}) {
        Backend backend(&isolatedSettings());
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
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
                                 ? "version 2004" : "exclude Snitt"));
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
    Backend canceled(&isolatedSettings());
    canceled.m_pendingRecording = true;
    QSignalSpy canceledSignal(&canceled, &Backend::recordingCanceled);
    canceled.cancelRecording();
    QCOMPARE(canceledSignal.size(), 1);
    QVERIFY(!canceled.recording());
    QVERIFY(!canceled.beginProtectedRecording(nullptr, nullptr));
}
#endif

void EditorTests::qmlResizeTextDraft() {
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QImage original(800, 600, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::white);
    QTemporaryDir directory;
    const QString path = directory.filePath("text-resize.png");
    QVERIFY(original.save(path));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(path));
    engine.rootContext()->setContextProperty("startInBackground", false);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *canvas = window->findChild<EditorCanvas *>("canvas");
    auto *text = window->findChild<QQuickItem *>("annotationText");
    auto *handle = window->findChild<QQuickItem *>("annotationTextResizeHandle");
    QVERIFY(canvas && text && handle);
    QTRY_VERIFY(canvas->hasImage());
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    canvas->setTool("text");
    const QPointF origin = canvas->imageRect().topLeft() + QPointF(60, 40) * canvas->imageScale();
    canvas->begin(origin.x(), origin.y());
    const QString draft = "One two three four five six seven eight nine ten";
    text->setProperty("text", draft);
    QTRY_VERIFY(handle->isVisible());
    const int originalLines = text->property("lineCount").toInt();
    const auto resizeTo = [&](qreal width) {
        const QPoint from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const QPoint to = from + QPoint(qRound((width - window->property("textWidth").toReal())
                                              * canvas->imageScale()), 0);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    };
    resizeTo(180);
    QVERIFY(qAbs(window->property("textWidth").toReal() - 180) < 2);
    QTRY_VERIFY(text->property("lineCount").toInt() > originalLines);
    QCOMPARE(text->property("text").toString(), draft);
    QVERIFY(window->property("editingText").toBool());
    QVERIFY(text->hasActiveFocus());
    QVERIFY(!canvas->canUndo());
    QCOMPARE(canvas->textSize(), 24);
    window->resize(860, 560);
    QTest::qWait(50);
    resizeTo(300);
    QVERIFY(qAbs(window->property("textWidth").toReal() - 300) < 2);
    resizeTo(180);
    const QRectF box(window->property("textX").toReal(), window->property("textY").toReal(),
        window->property("textWidth").toReal(), canvas->imageHeight() - window->property("textY").toReal());
    ImageDocument expected;
    expected.reset(original);
    QVERIFY(expected.text(box, draft, canvas->ink(), canvas->textSize()));
    QVERIFY(QMetaObject::invokeMethod(window, "commitText"));
    QVERIFY(!handle->isVisible());
    QVERIFY(canvas->copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), expected.render(expected.exportScale()));
}

void EditorTests::qmlWheelSizes() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QImage image(480, 360, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QTemporaryDir dir;
    const QString path = dir.filePath("wheel.png");
    QVERIFY(image.save(path));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    const auto wheelAt = [window, canvas](QPointF local, int delta) {
        const QPointF scene = canvas->mapToScene(local);
        QWheelEvent event(scene, window->mapToGlobal(scene.toPoint()), {}, {0, delta},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event);
        QCoreApplication::processEvents();
    };
    const auto point = [canvas](QPointF source) {
        return canvas->imageRect().topLeft() + source * canvas->imageScale();
    };
    canvas->setTool("rect");
    wheelAt(point({100, 250}), 120);
    QCOMPARE(canvas->strokeWidth(), 6);
    canvas->setTool("arrow");
    wheelAt(point({100, 250}), -120);
    QCOMPARE(canvas->strokeWidth(), 4);
    canvas->setTool("text");
    wheelAt(point({100, 250}), 120);
    QCOMPARE(canvas->textSize(), 26);
    canvas->setTool("blur");
    wheelAt(point({100, 250}), 120);
    QCOMPARE(canvas->pixelBlockSize(), 16);
    QCOMPARE(window->property("hint").toString(),
             QString("16 px · Mouse wheel to adjust"));
    QCOMPARE(canvas->strokeWidth(), 4);
    QCOMPARE(canvas->textSize(), 26);
    canvas->setTool("text");
    canvas->begin(point({180, 180}).x(), point({180, 180}).y());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    text->setProperty("text", QString(30, 'A').replace("A", "A\n"));
    auto *textFrame = window->findChild<QObject *>("annotationTextFrame");
    QVERIFY(textFrame);
    QTRY_VERIFY(text->property("contentHeight").toReal() > textFrame->property("height").toReal());
    const QPointF inside(textFrame->property("x").toReal() + 20,
                         textFrame->property("y").toReal() + textFrame->property("height").toReal() / 2);
    // Wheel input over an overflowing text editor still resizes the whole draft.
    wheelAt(inside, 120);
    QCOMPARE(canvas->textSize(), 28);
    wheelAt(inside, -120);
    QCOMPARE(canvas->textSize(), 26);
    wheelAt(inside, 12000);
    QCOMPARE(canvas->textSize(), canvas->maxTextSize());
    wheelAt(inside, 120);
    QCOMPARE(canvas->textSize(), canvas->maxTextSize());
    wheelAt(inside, -12000);
    QCOMPARE(canvas->textSize(), 8);
    wheelAt(inside, -120);
    QCOMPARE(canvas->textSize(), 8);
    wheelAt(inside, 120 * 9);
    QCOMPARE(canvas->textSize(), 26);
    wheelAt(point({20, 300}), 120);
    QCOMPARE(canvas->textSize(), 28);
    QCOMPARE(text->property("text").toString(), QString(30, 'A').replace("A", "A\n"));
    QVERIFY(text->property("activeFocus").toBool());
}

void EditorTests::qmlSaveAndClose() {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "offscreen")
        QSKIP("File-manager reveal requires an interactive Windows desktop");
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir source;
    const QString inputPath = source.filePath("input.png");
    QVERIFY(pattern().save(inputPath));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    QCOMPARE(saveButton->property("text").toString(), QString("Save"));
    QCOMPARE(copyButton->property("text").toString(), QString("Copy"));
    QCOMPARE(accessibleName(qobject_cast<QQuickItem *>(saveButton)), QString("Save image and close"));
    QCOMPARE(accessibleName(copyButton), QString("Copy image and close"));
    QCoreApplication::processEvents();
    const qreal saveRight = saveButton->property("x").toReal() + saveButton->property("width").toReal();
    QVERIFY(copyButton->x() >= saveRight && copyButton->x() - saveRight <= 13);

    const QString savedRoot = isolatedSettings().picturesRoot();
    const auto savedFiles = [&]() {
        QSet<QString> files;
        QDirIterator iterator(savedRoot, {"snitt-*.png"}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) files.insert(iterator.next());
        return files;
    };
    QSet<QString> known = savedFiles();
    const auto newSavedFile = [&]() {
        for (const QString &path : savedFiles()) {
            if (!known.contains(path)) { known.insert(path); return path; }
        }
        return QString();
    };
    QStringList created;
    const auto cleanupSavedFiles = qScopeGuard([&created] {
        for (const QString &path : created) QFile::remove(path);
    });
    QGuiApplication::clipboard()->setText("keep screenshot clipboard");
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().topLeft() + QPointF(5, 5) * canvas->imageScale();
    canvas->begin(textPoint.x(), textPoint.y());
    QVERIFY(window->property("editingText").toBool());
    auto *text = window->findChild<QObject *>("annotationText");
    QVERIFY(text);
    QCOMPARE(QQmlProperty(text, QStringLiteral("font.family")).read().toString(), annotationfont::family());
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
    QTRY_VERIFY(([&] {
        const qreal saveRight = arrangeButton->property("x").toReal()
            + arrangeButton->property("width").toReal();
        return arrangeCopyButton->x() >= saveRight && arrangeCopyButton->x() - saveRight <= 13;
    })());
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
    for (const QString &path : created) QVERIFY(QFileInfo::exists(path));
}

void EditorTests::qmlKeyboardCommands() {
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression("^(?!This plugin does not support raise\\(\\)).*"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
        {"arrow", Qt::Key_A}, {"blur", Qt::Key_P}, {"erase", Qt::Key_E}};
    for (const auto &tool : tools) {
        auto *button = visualItem(window->contentItem(), "tool_" + tool.first);
        QVERIFY2(button, qPrintable("Missing tool button: " + tool.first));
        QCOMPARE(button->property("text").toString(), QString());
        QVERIFY(!accessibleName(button).isEmpty());
        QTest::keyClick(window, tool.second);
        QTRY_COMPARE(canvas->tool(), tool.first);
        QVERIFY(button->property("checked").toBool());
    }
    QTest::keyClick(window, Qt::Key_G);
    QTRY_COMPARE(canvas->ink(), QColor("#22c55e"));
    QTest::keyClick(window, Qt::Key_P);
    QTRY_COMPARE(canvas->tool(), QString("blur"));
    QCOMPARE(window->property("hint").toString(), QString("12 px · Mouse wheel to adjust"));
    canvas->adjustToolSize(120);
    QCOMPARE(window->property("hint").toString(), QString("16 px · Mouse wheel to adjust"));
    QCOMPARE(canvas->ink(), QColor("#22c55e"));
    QTest::keyClick(window, Qt::Key_B);
    QTRY_COMPARE(canvas->ink(), QColor("#ef4444"));
    QCOMPARE(canvas->tool(), QString("blur"));
    auto *redButton = visualItem(window->contentItem(), "ink_bad");
    QVERIFY(redButton);
    QVERIFY(redButton->property("checked").toBool());
    QTest::keyClick(window, Qt::Key_G);
    QTRY_COMPARE(canvas->ink(), QColor("#22c55e"));
    QTest::keyClick(window, Qt::Key_D);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->ink(), QColor("#22c55e"));
    QCOMPARE(canvas->tool(), QString("blur"));
    QVERIFY(!visualItem(window->contentItem(), "ink_D"));
    QTest::keyClick(window, Qt::Key_B);
    QTRY_COMPARE(canvas->ink(), QColor("#ef4444"));

    canvas->setTool("rect");
    const QPointF start = canvas->imageRect().topLeft() + QPointF(10, 10) * canvas->imageScale();
    const QPointF end = canvas->imageRect().topLeft() + QPointF(50, 40) * canvas->imageScale();
    canvas->begin(start.x(), start.y()); canvas->end(end.x(), end.y());
    QVERIFY(canvas->canUndo());
    // Removed input actions must not interrupt or replace an annotation session.
    const QStringList inputActions{"capture", "captureMultiple", "captureVideo", "openImage", "pasteImage"};
    QImage replacement(24, 18, QImage::Format_RGB32);
    replacement.fill(Qt::yellow);
    QGuiApplication::clipboard()->setImage(replacement);
    QSignalSpy captureChanges(&backend, &Backend::capturingChanged);
    for (const QString &action : inputActions) {
        auto *shortcut = window->findChild<QObject *>(action + "Shortcut");
        QVERIFY(shortcut);
        QVERIFY(!shortcut->property("enabled").toBool());
        for (const QString &key : isolatedSettings().shortcuts().value(action).toStringList())
            QTest::keySequence(window, QKeySequence(key));
    }
    QCoreApplication::processEvents();
    QCOMPARE(captureChanges.size(), 0);
    QVERIFY(window->isVisible());
    QVERIFY(canvas->canUndo());
    QCOMPARE(canvas->imageWidth(), pattern().width());
    QCOMPARE(canvas->imageHeight(), pattern().height());
    QVERIFY(canvas->copy());
    const QImage expectedImage = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("not submitted");
    QTest::keyClick(window, Qt::Key_Return);
    QCoreApplication::processEvents();
    QVERIFY(window->isVisible()); QVERIFY(canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("not submitted"));
    auto *copyButton = visualItem(window->contentItem(), "copyButton");
    QVERIFY(copyButton);
    QCOMPARE(copyButton->property("text").toString(), QString("Copy"));
    QTest::keySequence(window, QKeySequence(QKeySequence::Copy));
    QCoreApplication::processEvents();
    QVERIFY(window->isVisible()); QVERIFY(canvas->hasImage());
    QTest::keyClick(window, Qt::Key_C);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
    QCOMPARE(QGuiApplication::clipboard()->image(), expectedImage);
    for (const QString &action : inputActions)
        QVERIFY(window->findChild<QObject *>(action + "Shortcut")->property("enabled").toBool());

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
    const QString arrangeTool = canvas->tool();
    const QColor arrangeInk = canvas->ink();
    QTest::keyClick(window, Qt::Key_P);
    QTest::keyClick(window, Qt::Key_B);
    QTest::keyClick(window, Qt::Key_D);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->tool(), arrangeTool);
    QCOMPARE(canvas->ink(), arrangeInk);

    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!canvas->arranging());
    QVERIFY(canvas->hasImage()); QVERIFY(window->isVisible());
    canvas->setTool("text");
    const QPointF textPoint = canvas->imageRect().center();
    canvas->begin(textPoint.x(), textPoint.y());
    QVERIFY(window->property("editingText").toBool());
    const QColor textInk = canvas->ink();
    QTest::keyClick(window, Qt::Key_C);
    QTest::keyClick(window, Qt::Key_H);
    QTest::keyClick(window, Qt::Key_P);
    QTest::keyClick(window, Qt::Key_B);
    QTest::keyClick(window, Qt::Key_D);
    auto *annotationText = window->findChild<QObject *>("annotationText");
    QVERIFY(annotationText);
    QTRY_COMPARE(annotationText->property("text").toString(), QString("chpbd"));
    QCOMPARE(canvas->tool(), QString("text"));
    QCOMPARE(canvas->ink(), textInk);
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
    // Material sets visible before its enter transition finishes. Wait for the
    // opening transition before sending a synthetic button click.
    QTRY_VERIFY(dialog->property("opened").toBool());
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
    QTRY_VERIFY(dialog->property("opened").toBool());
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
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QTemporaryDir dir;
    const QString path = dir.filePath("input.png");
    QVERIFY(pattern().save(path));
    Backend backend(&isolatedSettings());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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
    // Native activation is asynchronous; window shortcuts need it before Escape.
    QTRY_VERIFY(window->isActive());
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
    QTRY_VERIFY(window->isActive());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible() && !canvas->hasImage());
}

void EditorTests::qmlTrimBarSeeking() {
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl("qrc:/TrimBar.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> bar(component.create());
    QVERIFY(bar);
    bar->setProperty("width", 1000);
    bar->setProperty("height", 100);
    bar->setProperty("durationSec", 4.0);
    bar->setProperty("startSec", 1.5);
    bar->setProperty("endSec", 2.5);
    QSignalSpy scrubs(bar.get(), SIGNAL(scrub(double)));
    auto *slider = bar->findChild<QObject *>("recordingSeekSlider");
    QVERIFY(slider);
    const auto seek = [&](double seconds) {
        return QMetaObject::invokeMethod(bar.get(), "seekTo", Q_ARG(QVariant, seconds));
    };
    QVERIFY(seek(2.0));
    QCOMPARE(bar->property("playheadSec").toDouble(), 2.0);
    QCOMPARE(slider->property("value").toDouble(), 2.0);
    QVERIFY(seek(0.0));
    QCOMPARE(bar->property("playheadSec").toDouble(), 1.5);
    QVERIFY(seek(4.0));
    QCOMPARE(bar->property("playheadSec").toDouble(), 2.5);
    QCOMPARE(scrubs.size(), 3);
    QCOMPARE(bar->property("startSec").toDouble(), 1.5);
    QCOMPARE(bar->property("endSec").toDouble(), 2.5);
    // Exercise the actual pointer gesture, including its grab/release state.
    QQuickWindow window;
    window.resize(1000, 140);
    auto *barItem = qobject_cast<QQuickItem *>(bar.get());
    auto *sliderItem = qobject_cast<QQuickItem *>(slider);
    QVERIFY(barItem && sliderItem);
    barItem->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const auto sliderPoint = [&](double fraction) {
        return sliderItem->mapToScene(QPointF(9 + fraction * (sliderItem->width() - 18), 12)).toPoint();
    };
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, sliderPoint(0.5));
    QVERIFY(bar->property("interacting").toBool());
    QTest::mouseMove(&window, sliderPoint(0.75));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, sliderPoint(0.75));
    QVERIFY(!bar->property("interacting").toBool());
    QVERIFY(qAbs(bar->property("playheadSec").toDouble() - 2.25) < 0.01);
    QCOMPARE(bar->property("startSec").toDouble(), 1.5);
    QCOMPARE(bar->property("endSec").toDouble(), 2.5);
    barItem->setParentItem(nullptr);
}

void EditorTests::qmlRecordingReview() {
    if (QGuiApplication::platformName() == "offscreen")
        QSKIP("Qt Multimedia playback requires an interactive display");
    if (recording::toolPath("ffmpeg").isEmpty()) QSKIP("FFmpeg is needed to build a video fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString clip = directory.filePath("review.mp4");
    const QString sparseClip = directory.filePath("held-frame.mp4");
    QProcess fixture;
    fixture.start(recording::toolPath("ffmpeg"), {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=2560x1440:rate=30:duration=3",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", clip});
    QVERIFY(fixture.waitForFinished(15000));
    QCOMPARE(fixture.exitCode(), 0);
    fixture.start(recording::toolPath("ffmpeg"), {"-hide_banner", "-loglevel", "error", "-y",
        "-i", clip, "-vf", "select='lt(t,1)+gte(t,2)'", "-fps_mode", "vfr",
        "-c:v", "libx264", sparseClip});
    QVERIFY(fixture.waitForFinished(15000));
    QCOMPARE(fixture.exitCode(), 0);
    Backend backend(&isolatedSettings());
    auto *trim = qobject_cast<TrimSession *>(backend.trim());
    QVERIFY(trim);
    QElapsedTimer loadTimer;
    loadTimer.start();
    qint64 firstThumbMs = -1, allThumbsMs = -1;
    connect(trim, &TrimSession::changed, trim, [&] {
        int ready = 0;
        for (const auto &url : trim->thumbnails()) if (!url.isEmpty()) ++ready;
        if (ready && firstThumbMs < 0) firstThumbMs = loadTimer.elapsed();
        if (ready == 12 && allThumbsMs < 0) allThumbsMs = loadTimer.elapsed();
    });
    trim->open(clip);
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *root = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(root);
    auto *review = root->findChild<QQuickWindow *>("recordingReviewWindow");
    QVERIFY(review);
    QTRY_VERIFY(review->isVisible());
    auto *bar = review->findChild<QObject *>("recordingTrimBar");
    auto *player = review->findChild<QObject *>("recordingReviewPlayer");
    QVERIFY(bar && player);
    auto *save = review->findChild<QObject *>("recordingSaveTrimButton");
    auto *keep = review->findChild<QObject *>("recordingKeepOriginalButton");
    QVERIFY(save && keep);
    auto *playbackControls = review->findChild<QQuickItem *>("recordingPlaybackControls");
    auto *playbackTime = review->findChild<QQuickItem *>("recordingPlaybackTime");
    auto *playbackClock = review->findChild<QObject *>("recordingPlaybackClock");
    auto *playButton = review->findChild<QQuickItem *>("recordingReviewPlayButton");
    QVERIFY(playbackControls && playbackTime && playbackClock && playButton);
    QVERIFY(!review->findChild<QObject *>("recordingReviewZoomButton"));
    const auto timeCentered = [&] {
        const QPointF center = playbackTime->mapToItem(playbackControls,
            QPointF(playbackTime->width() / 2, playbackTime->height() / 2));
        return qAbs(center.x() - playbackControls->width() / 2) <= 0.5; // Anchors snap to logical pixels.
    };
    QTRY_VERIFY2(timeCentered(), qPrintable(QString("time x=%1 width=%2; controls width=%3")
        .arg(playbackTime->x()).arg(playbackTime->width()).arg(playbackControls->width())));
    QTRY_VERIFY(playButton->y() >= playbackTime->y() + playbackTime->height());
    QVERIFY(qAbs(playButton->x() + playButton->width() / 2 - playbackControls->width() / 2) <= 0.5);
    auto *timeSeparator = review->findChild<QQuickItem *>("recordingTimeSeparator");
    QVERIFY(timeSeparator);
    const qreal timeWidth = playbackTime->width();
    const qreal separatorX = timeSeparator->mapToItem(playbackControls, QPointF()).x();
    for (double seconds : {0.0, 1.11, 2.88}) {
        bar->setProperty("playheadSec", seconds);
        QCOMPARE(playbackTime->width(), timeWidth);
        QCOMPARE(timeSeparator->mapToItem(playbackControls, QPointF()).x(), separatorX);
    }
    bar->setProperty("playheadSec", 0.0);
    auto *videoOutput = qvariant_cast<QObject *>(player->property("videoOutput"));
    QVERIFY(videoOutput);
    auto *videoSink = qvariant_cast<QVideoSink *>(videoOutput->property("videoSink"));
    QVERIFY(videoSink);
    QSignalSpy firstFrames(videoSink, &QVideoSink::videoFrameChanged);
    QTRY_VERIFY_WITH_TIMEOUT(bar->property("endSec").toDouble() > 2.0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("duration").toLongLong() > 2000, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto &signal : firstFrames)
            if (qvariant_cast<QVideoFrame>(signal.first()).isValid()) return true;
        return false;
    })(), 45000);
    const qint64 firstFrameMs = loadTimer.elapsed();
    QTRY_VERIFY(!player->property("priming").toBool());
    QCOMPARE(save->property("text").toString(), QString("Keep original"));
    QVERIFY(save->property("enabled").toBool());
    QVERIFY(!keep->property("visible").toBool());
    QCOMPARE(player->property("playbackState").toInt(), int(QMediaPlayer::PausedState));
    QVERIFY(qAbs(player->property("position").toLongLong()) < 150);
    QVERIFY(qAbs(bar->property("playheadSec").toDouble()) < 0.15);
    const QImage primedPreview = review->grabWindow();
    QVERIFY(!primedPreview.isNull());
    const QColor previewCenter = primedPreview.pixelColor(primedPreview.width() / 2,
        primedPreview.height() / 3);
    QVERIFY2(previewCenter.red() > 40 || previewCenter.green() > 40 || previewCenter.blue() > 40,
        "The paused preview should show video pixels before Play");
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QTRY_VERIFY_WITH_TIMEOUT(player->property("position").toLongLong() > 500, 15000);
    // Sparse backend notifications must not freeze the visible playtime.
    const double before = bar->property("playheadSec").toDouble();
    {
        QSignalBlocker sparseNotifications(player);
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("playheadSec").toDouble() > before + 0.15, 2000);
    }
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QVERIFY(!playbackClock->property("running").toBool());
    QVERIFY(qAbs(bar->property("playheadSec").toDouble()
        - player->property("position").toLongLong() / 1000.0) < 0.1);
    auto *seekSlider = review->findChild<QQuickItem *>("recordingSeekSlider");
    QVERIFY(seekSlider);
    const auto seekPoint = [&](double fraction) {
        return seekSlider->mapToScene(QPointF(9 + fraction * (seekSlider->width() - 18), 12)).toPoint();
    };
    QTest::mousePress(review, Qt::LeftButton, Qt::NoModifier, seekPoint(0.25));
    QTest::mouseMove(review, seekPoint(0.5));
    QTest::mouseRelease(review, Qt::LeftButton, Qt::NoModifier, seekPoint(0.5));
    QCOMPARE(player->property("playbackState").toInt(), int(QMediaPlayer::PausedState));
    QVERIFY(qAbs(player->property("position").toLongLong() - 1500) < 50);
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QTest::mousePress(review, Qt::LeftButton, Qt::NoModifier, seekPoint(0.3));
    QCOMPARE(player->property("playbackState").toInt(), int(QMediaPlayer::PausedState));
    QTest::mouseMove(review, seekPoint(0.4));
    QTest::mouseRelease(review, Qt::LeftButton, Qt::NoModifier, seekPoint(0.4));
    QCOMPARE(player->property("playbackState").toInt(), int(QMediaPlayer::PlayingState));
    QCOMPARE(bar->property("startSec").toDouble(), 0.0);
    QCOMPARE(bar->property("endSec").toDouble(), trim->duration() / 1000.0);
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto &url : trim->thumbnails()) if (!url.isEmpty()) return true;
        return false;
    })(), 45000);
    QVERIFY(review->grabWindow().save("trim-review-normal.png"));
    review->resize(680, 520);
    QTest::qWait(150);
    QTRY_VERIFY(timeCentered());
    QVERIFY(review->grabWindow().save("trim-review-minimum.png"));
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto &url : trim->thumbnails()) if (url.isEmpty()) return false;
        return true;
    })(), 45000);
    qInfo("review load: first frame=%lld ms, first thumbnail=%lld ms, all thumbnails=%lld ms",
          firstFrameMs, firstThumbMs, allThumbsMs);
    if (qEnvironmentVariableIsSet("SNITT_CHECK_LOAD_TIMES")) {
        QVERIFY(firstFrameMs < 2000);
        QVERIFY(firstThumbMs < 2000);
        QVERIFY(allThumbsMs < 5000);
    }
    bar->setProperty("startSec", 0.5);
    bar->setProperty("endSec", 2.0);
    QCOMPARE(save->property("text").toString(), QString("Save trim"));
    QVERIFY(keep->property("visible").toBool());
    QFile originalClip(clip);
    QVERIFY(originalClip.open(QIODevice::ReadOnly));
    const QByteArray originalHash = QCryptographicHash::hash(originalClip.readAll(), QCryptographicHash::Sha256);
    originalClip.close();
    auto *cancel = review->findChild<QObject *>("recordingCancelExportButton");
    auto *escape = review->findChild<QObject *>("recordingReviewEscapeShortcut");
    QVERIFY(cancel);
    QVERIFY(escape);
    for (const bool useEscape : {false, true}) {
        bar->setProperty("startSec", 0.5);
        bar->setProperty("endSec", 2.0);
        QVERIFY(QMetaObject::invokeMethod(review, "prepareExport"));
        QVERIFY(review->property("preparingExport").toBool());
        if (useEscape) {
            QVERIFY(QMetaObject::invokeMethod(escape, "activated"));
        } else QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY(!review->property("preparingExport").toBool());
        QVERIFY(!trim->busy());
        QTRY_VERIFY_WITH_TIMEOUT(player->property("duration").toLongLong() > 2000, 30000);
        QCOMPARE(bar->property("startSec").toDouble(), 0.5);
        QCOMPARE(bar->property("endSec").toDouble(), 2.0);
        QTest::qWait(250); // The canceled release timer must not start an export.
        QVERIFY(review->isVisible());
        QVERIFY(!trim->busy());
        QVERIFY(QDir(directory.path()).entryList({".snitt-trim-*.mp4"}, QDir::Files).isEmpty());
        QVERIFY(originalClip.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(originalClip.readAll(), QCryptographicHash::Sha256), originalHash);
        originalClip.close();
        QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
        QTRY_VERIFY_WITH_TIMEOUT(player->property("position").toLongLong() > 700, 15000);
        QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
        bar->setProperty("playheadSec", 0.75);
        QVERIFY(QMetaObject::invokeMethod(review, "seek", Q_ARG(QVariant, QVariant(0.25))));
        QTRY_VERIFY(qAbs(player->property("position").toLongLong() - 1000) < 150);
    }
    QSignalSpy finalized(trim, &TrimSession::finalized);
    bar->setProperty("startSec", 0.0);
    bar->setProperty("endSec", trim->duration() / 1000.0);
    QCOMPARE(save->property("text").toString(), QString("Keep original"));
    QVERIFY(!keep->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(finalized.first().first().toString(), clip);
    QTRY_VERIFY(!review->isVisible());
    QVERIFY(QFileInfo(clip).size() > 0);
    finalized.clear();
    firstFrames.clear();
    trim->open(clip);
    QTRY_VERIFY(review->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(trim->duration() > 2000 && review->property("canEdit").toBool(), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto &signal : firstFrames)
            if (qvariant_cast<QVideoFrame>(signal.first()).isValid()) return true;
        return false;
    })(), 45000);
    QTRY_VERIFY(!player->property("priming").toBool());
    QCOMPARE(player->property("playbackState").toInt(), int(QMediaPlayer::PausedState));
    bar->setProperty("startSec", 0.5);
    bar->setProperty("endSec", 2.0);
    QTRY_VERIFY(save->property("enabled").toBool());
    QVERIFY(review->grabWindow().save("trim-review-selected.png"));
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(!finalized.isEmpty() || !trim->problem().isEmpty(), 60000);
    QVERIFY2(trim->problem().isEmpty(), qPrintable(trim->problem()));
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(finalized.first().first().toString(), clip);
    QTRY_VERIFY(!review->isVisible());
    // Reopen the copied MP4 through the real player: edit-list preroll must not
    // leave a black preview or delay playback of a cut between keyframes.
    firstFrames.clear();
    QElapsedTimer copiedLoad;
    copiedLoad.start();
    trim->open(clip);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        for (const auto &signal : firstFrames)
            if (qvariant_cast<QVideoFrame>(signal.first()).isValid()) return true;
        return false;
    })(), 10000);
    qInfo("trimmed review first frame: %lld ms", copiedLoad.elapsed());
    if (qEnvironmentVariableIsSet("SNITT_CHECK_LOAD_TIMES"))
        QVERIFY(copiedLoad.elapsed() < 2000);
    QTRY_VERIFY(!player->property("priming").toBool());
    QVERIFY(player->property("duration").toLongLong() >= 1400);
    QVERIFY(player->property("duration").toLongLong() <= 1750);
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QTRY_VERIFY_WITH_TIMEOUT(player->property("position").toLongLong() > 500, 5000);
    trim->keepOriginal();

    // This separate fixture holds a single frame from 1 to 2 seconds. Test the
    // actual frame timestamp as well as the counter so a timer that merely
    // polls the decoder's stale position cannot pass this regression.
    trim->open(sparseClip);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("duration").toLongLong() > 2000
        && player->property("primed").toBool() && !player->property("priming").toBool(), 10000);
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    QTRY_VERIFY_WITH_TIMEOUT(bar->property("playheadSec").toDouble() >= 1.1, 3000);
    const qint64 heldFrameTime = videoSink->videoFrame().startTime();
    QVERIFY(heldFrameTime >= 900000 && heldFrameTime < 1000000);
    double previousPlayhead = bar->property("playheadSec").toDouble();
    QElapsedTimer cadenceTimer;
    cadenceTimer.start();
    for (int sample = 0; sample < 5; ++sample) {
        QTest::qWait(100);
        const double currentPlayhead = bar->property("playheadSec").toDouble();
        qInfo("held-frame sample: elapsed=%lld ms, playhead=%.3f->%.3f, backend=%lld ms, clock=%d, mediaStatus=%d",
            cadenceTimer.restart(), previousPlayhead, currentPlayhead,
            player->property("position").toLongLong(), playbackClock->property("running").toBool(),
            player->property("mediaStatus").toInt());
        QVERIFY(currentPlayhead > previousPlayhead + 0.04);
        QVERIFY(currentPlayhead < previousPlayhead + 0.25);
        QCOMPARE(videoSink->videoFrame().startTime(), heldFrameTime);
        previousPlayhead = currentPlayhead;
    }
    QVERIFY(QMetaObject::invokeMethod(review, "togglePlay"));
    const double pausedPlayhead = bar->property("playheadSec").toDouble();
    QTest::qWait(150);
    QVERIFY(qAbs(bar->property("playheadSec").toDouble() - pausedPlayhead) < 0.01);
    trim->keepOriginal();
}

void EditorTests::qmlRecordingControls() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
        QSKIP("Recording controls require FFmpeg and an unlocked desktop");
    QVERIFY2(!recording::toolPath("ffmpeg").isEmpty(), "Interactive recording tests require FFmpeg");
#ifdef Q_OS_WIN
    QGuiApplication::setFont(QFont("Segoe UI"));
#else
    QGuiApplication::setFont(QFont("Helvetica"));
#endif
    QTest::failOnWarning(QRegularExpression(".*"));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&isolatedSettings());
    QString error;
    connect(&backend, &Backend::error, this, [&error](const QString &message) { error = message; });
    QSignalSpy saved(&backend, &Backend::recordingSaved);
    QSignalSpy canceled(&backend, &Backend::recordingCanceled);
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
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *controls = window->findChild<QQuickWindow *>("recordingWindow");
    auto *indicator = window->findChild<QQuickWindow *>("recordingStartupIndicator");
    auto *review = window->findChild<QQuickWindow *>("recordingReviewWindow");
    QVERIFY(controls);
    QVERIFY(indicator);
    auto *outline = window->findChild<QQuickWindow *>("recordingOutline");
    QVERIFY(outline);
    QVERIFY(review);
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
    QTRY_VERIFY(outline->isVisible());
    QCOMPARE(outline->geometry(), backend.recordingRegion().adjusted(-3, -3, 3, 3));
    QVERIFY(outline->flags() & Qt::WindowTransparentForInput);
    QVERIFY(outline->flags() & Qt::WindowDoesNotAcceptFocus);
#ifdef Q_OS_WIN
    DWORD affinity = 0;
    QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(outline->winId()), &affinity));
    QCOMPARE(affinity, DWORD(0x00000011));
#endif
    QImage outlinePreview;
    QTRY_VERIFY(!(outlinePreview = outline->grabWindow()).isNull());
    QVERIFY(outlinePreview.save("recording-outline.png"));
    QCOMPARE(outlinePreview.pixelColor(1, 1), QColor("#ef4444"));
    QCOMPARE(outlinePreview.pixelColor(outlinePreview.width() / 2, outlinePreview.height() / 2).alpha(), 0);
    QTest::qWait(200); // Let the window server retire the startup indicator before the desktop preview.
    const QRect previewArea = backend.recordingRegion().adjusted(-6, -6, 6, 6);
    const QPixmap regionPreview = QGuiApplication::primaryScreen()->grabWindow(0,
        previewArea.x(), previewArea.y(), previewArea.width(), previewArea.height());
    QVERIFY(regionPreview.save("recording-region-preview.png"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.recordingElapsed() >= 1 || !error.isEmpty(), 6000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QImage preview;
    QTRY_VERIFY(!(preview = controls->grabWindow()).isNull());
    QVERIFY(preview.save("recording-controls.png"));
    controls->requestActivate();
    QTRY_VERIFY(controls->isActive());
    QGuiApplication::clipboard()->setText("waiting for recording");
    QTest::keySequence(controls, QKeySequence(QKeySequence::Copy));
    QTest::keyClick(controls, Qt::Key_Escape);
    QTest::qWait(100);
    QVERIFY(backend.recording());
    QVERIFY(!backend.finishingRecording());
    QVERIFY(controls->isVisible());
    QVERIFY(canceled.isEmpty());
    QVERIFY(saved.isEmpty());
    backend.finishRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !error.isEmpty(), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(saved.size(), 1);
    const QString path = saved.first().first().toString();
    const QFileInfo clip(path);
    QVERIFY(clip.isAbsolute()); QCOMPARE(clip.suffix(), QString("mp4"));
    QVERIFY(clip.exists() && clip.size() > 0);
    const QDate allocatedDate = QDate::fromString(clip.completeBaseName().mid(6, 8), "yyyyMMdd");
    QVERIFY(allocatedDate.isValid());
    const QString expectedDirectory = QDir(isolatedSettings().videosRoot()).filePath(QStringLiteral("%1/%2")
        .arg(QString::number(allocatedDate.year()).rightJustified(4, QLatin1Char('0')))
        .arg(allocatedDate.month()));
    QCOMPARE(clip.absolutePath(), expectedDirectory);
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
        // Check the capture edges as well as its center: the outside border must not leak into the video.
        for (const QPoint point : {QPoint(0, 0), QPoint(width - 1, 0), QPoint(0, height - 1),
                                   QPoint(width - 1, height - 1), QPoint(width / 2, height / 2)}) {
            const qsizetype sample = offset + (qsizetype(point.y()) * width + point.x()) * 3;
            const QColor actual{uchar(pixels[sample]), uchar(pixels[sample + 1]), uchar(pixels[sample + 2])};
            QVERIFY2(qAbs(actual.red() - 0x12) <= 16, qPrintable(actual.name()));
            QVERIFY2(qAbs(actual.green() - 0x34) <= 16, qPrintable(actual.name()));
            QVERIFY2(qAbs(actual.blue() - 0x56) <= 16, qPrintable(actual.name()));
        }
    }
    QVERIFY(!backend.recording());
    QTRY_VERIFY(!controls->isVisible());
    QTRY_VERIFY(!outline->isVisible());
    QVERIFY(!window->isVisible());
    QTRY_VERIFY(review->isVisible());
    auto *trimSession = qobject_cast<TrimSession *>(backend.trim());
    QVERIFY(trimSession);
    QSignalSpy finalized(trimSession, &TrimSession::finalized);
    QVERIFY(QMetaObject::invokeMethod(backend.trim(), "keepOriginal"));
    QCOMPARE(finalized.size(), 1);
    QCOMPARE(QDir::fromNativeSeparators(finalized.first().first().toString()),
             QDir::fromNativeSeparators(path));
    QTRY_VERIFY(!review->isVisible());
    QTest::qWait(250); // Let Finder/Explorer select the completed file before cleanup.
    QVERIFY(QFile::remove(path));
#else
    QSKIP("Screen recording requires macOS or Windows");
#endif
}

void EditorTests::qmlRecordingHotkeyStop_data() {
    QTest::addColumn<bool>("duringStartup");
    QTest::newRow("startup") << true;
#ifdef Q_OS_MACOS
    QTest::newRow("native-dispatch") << false;
#else
    QTest::newRow("native-keypress") << false;
#endif
}

void EditorTests::qmlRecordingHotkeyStop() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
        QSKIP("Recording controls require an interactive desktop");
    QFETCH(bool, duringStartup);
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&isolatedSettings());
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
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
    engine.rootContext()->setContextProperty("initialImage", QUrl());
    engine.rootContext()->setContextProperty("startInBackground", true);
    engine.rootContext()->setContextProperty("showOnStart", false);
    engine.load(QUrl("qrc:/Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *controls = window->findChild<QQuickWindow *>("recordingWindow");
    auto *review = window->findChild<QQuickWindow *>("recordingReviewWindow");
    QVERIFY(controls);
    QVERIFY(review);
    GlobalHotkey hotkey;
    QVERIFY2(hotkey.registered(), qPrintable(hotkey.description()));
    QSignalSpy activated(&hotkey, &GlobalHotkey::activated);
    connect(&hotkey, &GlobalHotkey::activated, window, [window] {
        QMetaObject::invokeMethod(window, "hotkeyCapture");
    });
    QGuiApplication::clipboard()->setText("keep clipboard during recording");
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
    if (duringStartup) {
        QVERIFY(QMetaObject::invokeMethod(window, "hotkeyCapture"));
        QVERIFY(QMetaObject::invokeMethod(window, "hotkeyCapture"));
    } else {
        QTRY_VERIFY_WITH_TIMEOUT(!backend.startingRecording() || !error.isEmpty(), 15000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(backend.recordingElapsed() >= 1, 6000);
#ifdef Q_OS_WIN
        INPUT input[4]{};
        for (auto &key : input) key.type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_CONTROL;
        input[1].ki.wVk = VK_SNAPSHOT;
        input[2].ki.wVk = VK_SNAPSHOT; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
        input[3].ki.wVk = VK_CONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        QCOMPARE(SendInput(4, input, sizeof(INPUT)), UINT(4));
#else
        // Quartz-synthesized keys do not trigger Carbon hotkeys on this macOS VM.
        // Exercise the registered native handler through the Carbon event queue.
        EventRef event = nullptr;
        QCOMPARE(CreateEvent(nullptr, kEventClassKeyboard, kEventHotKeyPressed,
                             GetCurrentEventTime(), kEventAttributeNone, &event), OSStatus(noErr));
        const EventHotKeyID id{0x58534854, hotkey.nativeRegistrationId()};
        QVERIFY(id.id != 0);
        QCOMPARE(SetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                                   sizeof(id), &id), OSStatus(noErr));
        QCOMPARE(PostEventToQueue(GetMainEventQueue(), event, kEventPriorityStandard), OSStatus(noErr));
        ReleaseEvent(event);
#endif
        QTRY_COMPARE_WITH_TIMEOUT(activated.size(), 1, 3000);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !error.isEmpty(), 20000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep clipboard during recording"));
    QVERIFY(!backend.recording());
    QTRY_VERIFY(!controls->isVisible());
    QVERIFY(!window->isVisible());
    QTRY_VERIFY(review->isVisible());
    QVERIFY(QMetaObject::invokeMethod(backend.trim(), "keepOriginal"));
    QTRY_VERIFY(!review->isVisible());
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
    RegionSelector selector(pattern(), QRect(0, 0, 400, 300), &isolatedSettings());
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
    RegionSelector selector(pattern().convertToFormat(QImage::Format_RGB32), QRect(0, 0, 860, 560), &isolatedSettings());
    auto *toolbar = selector.findChild<QWidget *>("captureToolbar");
    auto *single = selector.findChild<QPushButton *>("singleCaptureButton");
    auto *multiple = selector.findChild<QPushButton *>("multipleCaptureButton");
    auto *video = selector.findChild<QPushButton *>("videoCaptureButton");
    auto *cancel = selector.findChild<QPushButton *>("cancelCaptureButton");
    auto *arrange = selector.findChild<QPushButton *>("arrangeCaptureButton");
    auto *instruction = selector.findChild<QLabel *>("captureInstruction");
    auto *count = selector.findChild<QLabel *>("captureCount");
    QVERIFY(toolbar && single && multiple && video && cancel && arrange && instruction && count);
    QCOMPARE(single->text(), QString("&Region"));
    QCOMPARE(multiple->text(), QString("&Multi"));
    QCOMPARE(video->text(), QString("&Video"));
    QCOMPARE(cancel->text(), QString("Cancel (%1)").arg(QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)));
    QCOMPARE(arrange->text(), QString("Arrange (%1)")
        .arg(QKeySequence(Qt::Key_Return).toString(QKeySequence::NativeText)));
    QCOMPARE(arrange->toolTip(), QString("Continue to Arrange (%1 / %2)")
        .arg(QKeySequence(Qt::Key_Return).toString(QKeySequence::NativeText),
             QKeySequence(Qt::Key_Enter).toString(QKeySequence::NativeText)));
    connect(&selector, &RegionSelector::multipleRequested, &selector, [&] { selector.setSelections(true, {}, 0); });
    connect(&selector, &RegionSelector::singleRequested, &selector, [&] { selector.setSelections(false, {}, 0); selector.setVideo(false); });
    connect(&selector, &RegionSelector::videoRequested, &selector, [&] { selector.setSelections(false, {}, 0); selector.setVideo(true); });
    QSignalSpy added(&selector, &RegionSelector::regionAdded);
    QSignalSpy accepted(&selector, &RegionSelector::accepted);
    QSignalSpy removed(&selector, &RegionSelector::regionRemoved);
    selector.show();
    QCoreApplication::processEvents();
    const QRect toolbarBounds = toolbar->geometry();
    const QRect modeBounds = multiple->geometry();
    QCOMPARE(toolbar->height(), 60);
    QCOMPARE(instruction->geometry().center().y(), single->geometry().center().y());
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
    QVERIFY(arrange->isHidden());
    QVERIFY(!arrange->isEnabled());
    QVERIFY(count->text().isEmpty());
    QCOMPARE(toolbar->geometry(), toolbarBounds);
    QCOMPARE(multiple->geometry(), modeBounds);
    QCOMPARE(instruction->geometry().center().y(), single->geometry().center().y());
    QVERIFY(instruction->isVisible());
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
    QCOMPARE(instruction->geometry().center().y(), single->geometry().center().y());
    QVERIFY(instruction->isVisible());
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
    QTest::keyClick(&selector, Qt::Key_V);
    QVERIFY(video->isChecked());
    QVERIFY(!multiple->isEnabled());
    QTest::keyClick(&selector, Qt::Key_V); // Selecting the current mode does not toggle it off.
    QTest::keyClick(&selector, Qt::Key_M); // The disabled mode's key follows its button.
    QVERIFY(video->isChecked());
    QCOMPARE(toolbar->geometry(), toolbarBounds);
    selector.resize(400, 300);
    QCoreApplication::processEvents();
    QCOMPARE(cancel->text(), QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText));
    QCOMPARE(cancel->accessibleName(), QString("Cancel selection (%1)")
        .arg(QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)));
    QVERIFY(selector.grab().save("capture-toolbar-compact.png"));
    for (auto *control : toolbar->findChildren<QPushButton *>())
        if (!control->isHidden()) QVERIFY(toolbar->rect().contains(control->geometry()));
    QTest::keyClick(&selector, Qt::Key_R);
    QVERIFY(single->isChecked());
    QVERIFY(multiple->isEnabled());
    QTest::keyClick(&selector, Qt::Key_M);
    QVERIFY(multiple->isChecked());
    QCoreApplication::processEvents();
    QVERIFY(instruction->isHidden());
    QCOMPARE(toolbar->toolTip(), instruction->text());
    selector.setSelections(true, {{0, QRectF(20, 180, 80, 60)}}, 1);
    QCoreApplication::processEvents();
    QVERIFY(arrange->isVisible());
    QVERIFY(selector.grab().save("capture-toolbar-compact-multiple.png"));
    for (auto *control : toolbar->findChildren<QPushButton *>())
        QVERIFY2(control->width() >= control->fontMetrics().horizontalAdvance(QString(control->text()).remove('&')) + 20,
                 qPrintable(control->objectName() + ": " + control->text()));
}

void EditorTests::settingsRegionToolbarHintsLive() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    const QByteArray defaults = readBytes(paths.settingsFile);
    RegionSelector selector(pattern(), QRect(0, 0, 860, 560), &settings);
    selector.setSelections(true, {}, 1);
    selector.show();
    QCoreApplication::processEvents();
    auto *toolbar = selector.findChild<QWidget *>("captureToolbar");
    QVERIFY(toolbar);
    struct Action {
        const char *key;
        const char *objectName;
        const char *label;
        const char *description;
        const char *defaultKeys;
        const char *customKeys;
    };
    const Action actions[] = {
        {"regionSingle", "singleCaptureButton", "Region", "Select a region", "R", "F4|F5"},
        {"regionMultiple", "multipleCaptureButton", "Multi", "Select multiple regions", "M", "F6|F7"},
        {"regionVideo", "videoCaptureButton", "Video", "Record a region", "V", "F8|F9"},
        {"regionCancel", "cancelCaptureButton", "Cancel", "Cancel selection", "Escape", "F10|F11"},
        {"regionArrange", "arrangeCaptureButton", "Arrange", "Continue to Arrange", "Return|Enter", "F12|F13"}
    };
    const QRect originalBounds = toolbar->geometry();
    QByteArray changed = defaults;
    for (const auto &action : actions)
        changed.replace(QByteArray(action.key) + '=' + action.defaultKeys + '\n',
                        QByteArray(action.key) + '=' + action.customKeys + '\n');
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY2(settings.reloadNow(), qPrintable(settings.lastError()));
    QCoreApplication::processEvents();
    QCOMPARE(toolbar->geometry(), originalBounds);
    for (const auto &action : actions) {
        auto *control = selector.findChild<QPushButton *>(action.objectName);
        QVERIFY(control);
        const QString hints = settings.shortcutHints().value(action.key).toString();
        const bool mode = control->isCheckable();
        QCOMPARE(control->text(), mode ? QString(action.label)
            : QString("%1 (%2)").arg(action.label, hints.section(" / ", 0, 0)));
        QCOMPARE(control->toolTip(), QString("%1 (%2)").arg(action.description, hints));
        QCOMPARE(control->accessibleName(), control->toolTip());
    }

    // Even a single configured alternative can be wider than a compact button.
    for (const auto &action : actions) {
        const QByteArray longKeys = "Ctrl+Alt+Shift+" + QByteArray(action.customKeys).replace("|", "|Ctrl+Alt+Shift+");
        changed.replace(QByteArray(action.key) + '=' + action.customKeys + '\n',
                        QByteArray(action.key) + '=' + longKeys + '\n');
    }
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY2(settings.reloadNow(), qPrintable(settings.lastError()));
    selector.resize(400, 300);
    QCoreApplication::processEvents();
    QCOMPARE(toolbar->width(), 376);
    for (auto *control : toolbar->findChildren<QPushButton *>()) {
        QVERIFY(toolbar->rect().contains(control->geometry()));
        QVERIFY2(control->width() >= control->minimumSizeHint().width(),
                 qPrintable(QString("%1: width %2, minimum %3").arg(control->objectName()).arg(control->width()).arg(control->minimumSizeHint().width())));
        QVERIFY2(control->width() >= control->fontMetrics().horizontalAdvance(control->text()) + 20,
                 qPrintable(control->objectName() + ": " + control->text()));
    }
    for (const auto &action : actions) {
        auto *control = selector.findChild<QPushButton *>(action.objectName);
        const QString hints = settings.shortcutHints().value(action.key).toString();
        QCOMPARE(control->toolTip(), QString("%1 (%2)").arg(action.description, hints));
        QCOMPARE(control->accessibleName(), control->toolTip());
    }

    changed = defaults;
    for (const auto &action : actions)
        changed.replace(QByteArray(action.key) + '=' + action.defaultKeys + '\n', QByteArray(action.key) + "=\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY2(settings.reloadNow(), qPrintable(settings.lastError()));
    QCoreApplication::processEvents();
    for (const auto &action : actions) {
        auto *control = selector.findChild<QPushButton *>(action.objectName);
        if (!control->isCheckable() || !control->text().isEmpty())
            QCOMPARE(control->text(), QString(action.label));
        QCOMPARE(control->toolTip(), QString(action.description));
        QCOMPARE(control->accessibleName(), control->toolTip());
    }
    QSignalSpy canceled(&selector, &RegionSelector::canceled);
    QSignalSpy accepted(&selector, &RegionSelector::accepted);
    QSignalSpy singleRequested(&selector, &RegionSelector::singleRequested);
    QSignalSpy multipleRequested(&selector, &RegionSelector::multipleRequested);
    QSignalSpy videoRequested(&selector, &RegionSelector::videoRequested);
    for (Qt::Key key : {Qt::Key_R, Qt::Key_M, Qt::Key_V}) QTest::keyClick(&selector, key);
    QTest::keyClick(&selector, Qt::Key_Escape);
    QTest::keyClick(&selector, Qt::Key_Return);
    QCOMPARE(canceled.size(), 0);
    QCOMPARE(accepted.size(), 0);
    QCOMPARE(singleRequested.size(), 0);
    QCOMPARE(multipleRequested.size(), 0);
    QCOMPARE(videoRequested.size(), 0);
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
    RegionSelector selector(pattern(), QRect(0, 0, 400, 300), &isolatedSettings());
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
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
        QSKIP("Native hotkeys require an interactive Windows desktop; set SNITT_INTERACTIVE_TESTS=1");
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
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
        QSKIP("Multiple-region capture requires a real desktop");
    QWidget marker;
    marker.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    marker.setGeometry(QGuiApplication::primaryScreen()->geometry().adjusted(80, 80, -80, -80));
    marker.setAutoFillBackground(true);
    QPalette palette; palette.setColor(QPalette::Window, QColor("#123456")); marker.setPalette(palette);
    marker.show(); marker.raise(); marker.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&marker));
    QTest::qWait(150);
    Backend backend(&isolatedSettings());
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
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
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
    Backend backend(&isolatedSettings());
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
    if (qEnvironmentVariableIsEmpty("SNITT_INTERACTIVE_TESTS"))
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
    Backend backend(&isolatedSettings());
    // Include the real QML capture/hide path used by the resident app.
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &isolatedSettings());
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

void EditorTests::settingsCreateDefaultsAndPreserveEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    QVERIFY(QFileInfo::exists(paths.settingsFile));
    QVERIFY(QFileInfo::exists(paths.guideFile));
    QVERIFY(settings.settingsFile() == QFileInfo(paths.settingsFile).absoluteFilePath());
    QVERIFY(settings.guideFile() == QFileInfo(paths.guideFile).absoluteFilePath());
    const QByteArray ini = readBytes(paths.settingsFile);
    QVERIFY(ini.startsWith("; Snitt application settings"));
    QVERIFY(ini.contains("AI agents: read settings-format.md"));
    QFile guide(paths.guideFile);
    QVERIFY(guide.open(QIODevice::ReadOnly));
    const QByteArray guideBytes = guide.readAll();
    QVERIFY(guideBytes.contains("# Snitt settings file"));
    QVERIFY(guideBytes.contains("## Shortcuts"));
    QVERIFY(guideBytes.contains("## Save roots"));

    QSettings parsed(paths.settingsFile, QSettings::IniFormat);
    QCOMPARE(parsed.value("Shortcuts/globalCapture").toString(), QString("Ctrl+Print"));
    QCOMPARE(parsed.value("Colors/good").toString(), QString("#22c55e"));
    QCOMPARE(settings.picturesRoot(), paths.defaultPicturesRoot);
    QCOMPARE(settings.videosRoot(), paths.defaultVideosRoot);

    QFile append(paths.settingsFile);
    QVERIFY(append.open(QIODevice::Append));
    QVERIFY(append.write("\n; user's note\n[Agent]\nunknownSetting=keep-me\n") > 0);
    append.close();
    const QByteArray withUnknown = readBytes(paths.settingsFile);
    QVERIFY(settings.reloadNow());
    QCOMPARE(readBytes(paths.settingsFile), withUnknown);
}

void EditorTests::settingsReloadsAtomicEditsAndRecovers() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    QByteArray valid = readBytes(paths.settingsFile);
    QVERIFY(valid.contains("toolPixelate=P\n"));
    QByteArray inPlace = valid;
    inPlace.replace("good=#22c55e", "good=#335577");
    QFile edit(paths.settingsFile);
    QVERIFY(edit.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(edit.write(inPlace), qint64(inPlace.size()));
    edit.close();
    QTRY_COMPARE_WITH_TIMEOUT(settings.goodColor(), QColor("#335577"), 3000);
    valid = readBytes(paths.settingsFile);
    valid.replace("good=#335577", "good=#386ab3");
    valid.replace("toolPixelate=P\n", "toolPixelate=Shift+P\n");
    valid.replace("picturesRoot=\"" + QDir::fromNativeSeparators(paths.defaultPicturesRoot).toUtf8() + "\"",
                  "picturesRoot=\"" + QDir(directory.filePath("Pictures/custom")).absolutePath().toUtf8() + "\"");
    valid.replace("videosRoot=\"" + QDir::fromNativeSeparators(paths.defaultVideosRoot).toUtf8() + "\"",
                  "videosRoot=\"" + QDir(directory.filePath("Clips/custom")).absolutePath().toUtf8() + "\"");
    QVERIFY(atomicWrite(paths.settingsFile, valid));
    QTRY_COMPARE_WITH_TIMEOUT(settings.goodColor(), QColor("#386ab3"), 3000);
    QCOMPARE(settings.shortcuts().value("toolPixelate").toStringList(), QStringList{"Shift+P"});
    QCOMPARE(settings.picturesRoot(), QDir(directory.filePath("Pictures/custom")).absolutePath());
    QCOMPARE(settings.videosRoot(), QDir(directory.filePath("Clips/custom")).absolutePath());

    valid.replace("good=#386ab3", "good=#247050");
    QVERIFY(atomicWrite(paths.settingsFile, valid)); // A second atomic replacement must be watched too.
    QTRY_COMPARE_WITH_TIMEOUT(settings.goodColor(), QColor("#247050"), 3000);

    QByteArray invalid = valid;
    invalid.replace("bad=#ef4444", "bad=not-a-color");
    QVERIFY(atomicWrite(paths.settingsFile, invalid));
    QTRY_VERIFY_WITH_TIMEOUT(!settings.lastError().isEmpty(), 3000);
    QCOMPARE(settings.goodColor(), QColor("#247050"));
    QCOMPARE(settings.badColor(), QColor("#ef4444"));
    QCOMPARE(readBytes(paths.settingsFile), invalid); // Never rewrite or discard the invalid candidate.

    QByteArray repaired = invalid;
    repaired.replace("bad=not-a-color", "bad=#a7475e");
    QVERIFY(atomicWrite(paths.settingsFile, repaired));
    QTRY_COMPARE_WITH_TIMEOUT(settings.badColor(), QColor("#a7475e"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(settings.lastError().isEmpty(), 3000);

    QVERIFY(QFile::remove(paths.settingsFile));
    QTRY_VERIFY_WITH_TIMEOUT(!settings.lastError().isEmpty(), 3000);
    QCOMPARE(settings.badColor(), QColor("#a7475e"));
    QVERIFY(QDir(QFileInfo(paths.settingsFile).absolutePath()).removeRecursively());
    QVERIFY(QDir().mkpath(QFileInfo(paths.settingsFile).absolutePath()));
    QVERIFY(atomicWrite(paths.settingsFile, repaired));
    QTRY_VERIFY_WITH_TIMEOUT(settings.lastError().isEmpty(), 3000);
    QCOMPARE(settings.badColor(), QColor("#a7475e"));
}

void EditorTests::settingsRejectConflictsAndRollbackNativeHotkey() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    QByteArray invalid = readBytes(paths.settingsFile);
    QVERIFY(invalid.contains("toolRectangle=R\n"));
    QVERIFY(invalid.contains("toolPixelate=P\n"));
    invalid.replace("toolPixelate=P\n", "toolPixelate=R\n");
    QVERIFY(atomicWrite(paths.settingsFile, invalid));
    QVERIFY(invalid.contains("toolPixelate=R\n"));
    QCOMPARE(readBytes(paths.settingsFile), invalid);
    QSettings parsed(paths.settingsFile, QSettings::IniFormat);
    QCOMPARE(parsed.value("Shortcuts/toolRectangle").toString(), QString("R"));
    QCOMPARE(parsed.value("Shortcuts/toolPixelate").toString(), QString("R"));
    const bool acceptedCollision = settings.reloadNow();
    QVERIFY(!acceptedCollision);
    QVERIFY(settings.lastError().contains("toolPixelate"));
    QVERIFY(settings.lastError().contains("toolRectangle"));
    QCOMPARE(readBytes(paths.settingsFile), invalid);
    QCOMPARE(settings.shortcuts().value("toolPixelate").toStringList(), QStringList{"P"});

    GlobalHotkey active;
    QKeySequence oldSequence;
    for (int functionKey = 1; functionKey <= 24 && oldSequence.isEmpty(); ++functionKey) {
        const QKeySequence candidate(QStringLiteral("Ctrl+Alt+F%1").arg(functionKey));
        if (active.setShortcut(candidate)) oldSequence = candidate;
    }
    if (oldSequence.isEmpty()) QSKIP("No free native modifier+function-key shortcut for rollback test");
    QByteArray nativeCandidate = readBytes(paths.settingsFile);
    nativeCandidate.replace("toolPixelate=R\n", "toolPixelate=P\n");
    const auto iniSequence = [](const QKeySequence &sequence) {
        QString portable = sequence.toString(QKeySequence::PortableText);
#ifdef Q_OS_MACOS
        portable.replace(QStringLiteral("Ctrl+"), QStringLiteral("Meta+"));
#endif
        return portable.toUtf8();
    };
    nativeCandidate.replace("globalCapture=Ctrl+Print", "globalCapture=" + iniSequence(oldSequence));
    QVERIFY(atomicWrite(paths.settingsFile, nativeCandidate));
    QVERIFY(settings.reloadNow());
    settings.attachGlobalHotkey(&active);
    QCOMPARE(active.shortcut(), oldSequence);

    GlobalHotkey occupied;
    QKeySequence occupiedSequence;
    for (int functionKey = 1; functionKey <= 24 && occupiedSequence.isEmpty(); ++functionKey) {
        const QKeySequence candidate(QStringLiteral("Ctrl+Alt+F%1").arg(functionKey));
        if (candidate != oldSequence && occupied.setShortcut(candidate)) occupiedSequence = candidate;
    }
    if (occupiedSequence.isEmpty()) QSKIP("No second free native modifier+function-key shortcut for rollback test");
    const QByteArray conflictingCandidate = nativeCandidate;
    QByteArray blocked = conflictingCandidate;
    blocked.replace("globalCapture=" + iniSequence(oldSequence),
                    "globalCapture=" + iniSequence(occupiedSequence));
    QVERIFY(atomicWrite(paths.settingsFile, blocked));
    QVERIFY(!settings.reloadNow());
    QCOMPARE(active.shortcut(), oldSequence);
    QCOMPARE(settings.shortcuts().value("globalCapture").toStringList(),
             QStringList{oldSequence.toString(QKeySequence::PortableText)});
    QCOMPARE(readBytes(paths.settingsFile), blocked);
}

void EditorTests::settingsUpdateEditorShortcutsHintsAndColorsLive() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    QTemporaryDir imageDirectory;
    const QString imagePath = imageDirectory.filePath("input.png");
    QVERIFY(pattern().save(imagePath));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&settings);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(imagePath));
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
    window->requestActivate();
    canvas->forceActiveFocus();
    QTRY_VERIFY(window->isActive());
    QTest::keyClick(window, Qt::Key_G);
    QTRY_COMPARE(canvas->colorMode(), QString("good"));

    QList<QObject *> finishButtons;
    for (const char *name : {"saveButton", "copyButton", "saveArrangementButton", "copyArrangementButton"}) {
        auto *button = window->findChild<QObject *>(name);
        QVERIFY(button);
        QVERIFY(button->property("underlineShortcut").toBool());
        finishButtons.append(button);
    }

    QByteArray changed = readBytes(paths.settingsFile);
    changed.replace("toolPixelate=P\n", "toolPixelate=Shift+Alt+P\n");
    changed.replace("saveClose=S\n", "saveClose=Ctrl+S\n");
    changed.replace("copyClose=C\n", "copyClose=Ctrl+C\n");
    changed.replace("good=#22c55e", "good=#317ab5");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QTRY_COMPARE_WITH_TIMEOUT(canvas->goodColor(), QColor("#317ab5"), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(canvas->ink(), QColor("#317ab5"), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(settings.shortcutHints().value("toolPixelate").toString(),
        QKeySequence::fromString(QStringLiteral("Shift+Alt+P"), QKeySequence::PortableText)
            .toString(QKeySequence::NativeText), 3000);

    canvas->setTool("rect");
    QTest::keyClick(window, Qt::Key_P);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->tool(), QString("rect"));
    QTest::keySequence(window, QKeySequence(QStringLiteral("Shift+Alt+P")));
    QTRY_COMPARE(canvas->tool(), QString("blur"));
    for (auto *button : finishButtons)
        QTRY_VERIFY(!button->property("underlineShortcut").toBool());

    // A mnemonic returns when its bare letter is one of the configured
    // alternatives; disabling a shortcut must not leave a misleading underline.
    changed.replace("saveClose=Ctrl+S\n", "saveClose=F6|S\n");
    changed.replace("copyClose=Ctrl+C\n", "copyClose=\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    for (auto *button : finishButtons)
        QTRY_COMPARE(button->property("underlineShortcut").toBool(), button->objectName().startsWith("save"));
}

void EditorTests::settingsShortcutAlternativesAndDisable() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    const QByteArray defaults = readBytes(paths.settingsFile);
    QByteArray changed = defaults;
    changed.replace("toolPixelate=P\n", "toolPixelate=F6|F7\n");
    changed.replace("reviewCancel=Escape\n", "reviewCancel=F8|F9\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY2(settings.reloadNow(), qPrintable(settings.lastError()));
    const QString imagePath = directory.filePath("input.png");
    QVERIFY(pattern().save(imagePath));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&settings);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(imagePath));
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
    for (Qt::Key key : {Qt::Key_F6, Qt::Key_F7}) {
        canvas->setTool("rect");
        QTest::keyClick(window, key);
        QTRY_COMPARE_WITH_TIMEOUT(canvas->tool(), QString("blur"), 1000);
    }

    changed.replace("toolPixelate=F6|F7\n", "toolPixelate=\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    canvas->setTool("rect");
    for (Qt::Key key : {Qt::Key_P, Qt::Key_F6, Qt::Key_F7}) QTest::keyClick(window, key);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->tool(), QString("rect"));
    QVERIFY(settings.shortcutHints().value("toolPixelate").toString().isEmpty());
    changed.replace("toolPixelate=\n", "toolPixelate=F10|F11\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    for (Qt::Key key : {Qt::Key_F10, Qt::Key_F11}) {
        canvas->setTool("rect");
        QTest::keyClick(window, key);
        QTRY_COMPARE_WITH_TIMEOUT(canvas->tool(), QString("blur"), 1000);
    }

    // Exercise the review window's real shortcut delivery without a movie or export.
    auto *review = window->findChild<QQuickWindow *>("recordingReviewWindow");
    QVERIFY(review);
    review->show();
    review->requestActivate();
    QTRY_VERIFY(review->isActive());
    for (Qt::Key key : {Qt::Key_F8, Qt::Key_F9}) {
        review->setProperty("preparingExport", true);
        QTest::keyClick(review, key);
        QTRY_VERIFY_WITH_TIMEOUT(!review->property("preparingExport").toBool(), 1000);
    }
    changed.replace("reviewCancel=F8|F9\n", "reviewCancel=\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    review->setProperty("preparingExport", true);
    for (Qt::Key key : {Qt::Key_Escape, Qt::Key_F8, Qt::Key_F9}) QTest::keyClick(review, key);
    QCoreApplication::processEvents();
    QVERIFY(review->property("preparingExport").toBool());
    changed.replace("reviewCancel=\n", "reviewCancel=F10|F11\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    for (Qt::Key key : {Qt::Key_F10, Qt::Key_F11}) {
        review->setProperty("preparingExport", true);
        QTest::keyClick(review, key);
        QTRY_VERIFY_WITH_TIMEOUT(!review->property("preparingExport").toBool(), 1000);
    }
    review->hide();
}

void EditorTests::macSettingsUseCommandActions() {
#ifndef Q_OS_MACOS
    QSKIP("macOS maps Qt Control to the physical Command modifier");
#else
    QVERIFY(!QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    const QList<QPair<QString, QKeySequence>> standards{
        {"capture", QKeySequence(QKeySequence::New)}, {"openImage", QKeySequence(QKeySequence::Open)},
        {"pasteImage", QKeySequence(QKeySequence::Paste)}, {"undo", QKeySequence(QKeySequence::Undo)},
        // The offscreen QPA has generic StandardKey defaults, so spell macOS Redo explicitly.
        {"redo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}};
    for (const auto &standard : standards) {
        const QKeySequence actual(settings.shortcuts().value(standard.first).toStringList().first());
        QCOMPARE(actual, QKeySequence(standard.second));
        QCOMPARE(settings.shortcutHints().value(standard.first).toString(),
                 actual.toString(QKeySequence::NativeText));
    }
    QVERIFY(readBytes(paths.settingsFile).contains("pasteImage=Meta+V\n"));
    const QString imagePath = directory.filePath("input.png");
    QVERIFY(pattern().save(imagePath));
    qmlRegisterType<EditorCanvas>("Snitt", 1, 0, "EditorCanvas");
    if (QQuickStyle::name() != "Material") QQuickStyle::setStyle("Material");
    Backend backend(&settings);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("appSettings", &settings);
    engine.rootContext()->setContextProperty("initialImage", QUrl::fromLocalFile(imagePath));
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
    QImage replacement(24, 18, QImage::Format_RGB32);
    replacement.fill(Qt::yellow);
    QGuiApplication::clipboard()->setImage(replacement);
    QTest::keySequence(window, QKeySequence(QKeySequence::Paste));
    QCoreApplication::processEvents();
    QCOMPARE(canvas->imageWidth(), pattern().width()); // Image paste is disabled during annotation.
    canvas->clear();
    QTest::keySequence(window, QKeySequence(QKeySequence::Paste));
    QTRY_COMPARE(canvas->imageWidth(), 24);
    canvas->addText(2, 2, 20, 15, "Undo");
    QVERIFY(canvas->canUndo());
    QTest::keySequence(window, QKeySequence(QKeySequence::Undo));
    QTRY_VERIFY(!canvas->canUndo());
    QTest::keySequence(window, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    QTRY_VERIFY(canvas->canUndo());

    // Qt's normal text editing also continues to use Command.
    canvas->setTool("text");
    const QPointF point = canvas->imageRect().center();
    canvas->begin(point.x(), point.y());
    QVERIFY(window->property("editingText").toBool());
    auto *annotation = window->findChild<QObject *>("annotationText");
    QVERIFY(annotation);
    for (const char key : QByteArray("original")) QTest::keyClick(window, key);
    QTest::keySequence(window, QKeySequence(QKeySequence::SelectAll));
    QGuiApplication::clipboard()->setText("replacement");
    QTest::keySequence(window, QKeySequence(QKeySequence::Paste));
    QTRY_COMPARE(annotation->property("text").toString(), QString("replacement"));
    QTest::keyClick(window, Qt::Key_Escape);

    QByteArray changed = readBytes(paths.settingsFile);
    changed.replace("regionMultiple=M\n", "regionMultiple=Meta+M|Ctrl+M\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    RegionSelector selector(pattern(), QRect(0, 0, 800, 600), &settings);
    QSignalSpy multiple(&selector, &RegionSelector::multipleRequested);
    QTest::keyClick(&selector, Qt::Key_M, Qt::ControlModifier);
    QCOMPARE(multiple.size(), 1);
    QTest::keyClick(&selector, Qt::Key_M, Qt::MetaModifier);
    QCOMPARE(multiple.size(), 2);
    changed.replace("regionMultiple=Meta+M|Ctrl+M\n", "regionMultiple=Meta+M\n");
    QVERIFY(atomicWrite(paths.settingsFile, changed));
    QVERIFY(settings.reloadNow());
    QTest::keyClick(&selector, Qt::Key_M, Qt::MetaModifier);
    QCOMPARE(multiple.size(), 2);
    QTest::keyClick(&selector, Qt::Key_M, Qt::ControlModifier);
    QCOMPARE(multiple.size(), 3);
#endif
}

void EditorTests::macSettingsNativeShortcutAgreement() {
#ifndef Q_OS_MACOS
    QSKIP("macOS native registration uses Carbon modifiers");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    const QByteArray defaults = readBytes(paths.settingsFile);
    QCOMPARE(settings.shortcuts().value("globalCapture").toStringList(), QStringList{"Meta+Print"});
    const QString defaultHint = settings.shortcutHints().value("globalCapture").toString();
    QVERIFY(defaultHint.contains(QChar(0x2303))); // Physical Control, not Command.
    QVERIFY(!defaultHint.contains(QChar(0x2318)));
    GlobalHotkey hotkey;
    const QList<QPair<QString, UInt32>> modifiers{
        {"Ctrl", controlKey}, {"Meta", cmdKey}, {"Ctrl+Meta", controlKey | cmdKey}};
    for (const auto &modifier : modifiers) {
        QByteArray changed = defaults;
        const QByteArray binding = modifier.first.toUtf8() + "+Alt+Shift+F19";
        changed.replace("globalCapture=Ctrl+Print", "globalCapture=" + binding);
        QVERIFY(atomicWrite(paths.settingsFile, changed));
        QVERIFY(settings.reloadNow());
        const QKeySequence sequence(settings.shortcuts().value("globalCapture").toStringList().first());
        EventHotKeyRef reserved = nullptr;
        const UInt32 nativeModifiers = modifier.second | optionKey | shiftKey;
        const OSStatus status = RegisterEventHotKey(kVK_F19, nativeModifiers, {0x54535458, 1},
            GetApplicationEventTarget(), 0, &reserved);
        const auto cleanup = qScopeGuard([&] { if (reserved) UnregisterEventHotKey(reserved); });
        if (status != noErr) QSKIP("A free native F19 combination is required");
        QString error;
        QVERIFY2(!hotkey.setShortcut(sequence, &error), "Qt binding must target the same physical modifier as Carbon");
        UnregisterEventHotKey(reserved);
        reserved = nullptr;
        QVERIFY2(hotkey.setShortcut(sequence, &error), qPrintable(error));
        QCOMPARE(hotkey.description(), settings.shortcutHints().value("globalCapture").toString());
        // Conflict validation uses the same physical meaning as registration and hints.
        changed.replace("regionMultiple=M\n", "regionMultiple=" + binding + "\n");
        QVERIFY(atomicWrite(paths.settingsFile, changed));
        QVERIFY(!settings.reloadNow());
        QVERIFY(settings.lastError().contains("globalCapture"));
        QVERIFY(settings.lastError().contains("regionMultiple"));
    }
#endif
}

void EditorTests::settingsRejectMalformedAndNonScalarValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto paths = settingsPaths(directory.path());
    AppSettings settings(paths);
    QByteArray valid = readBytes(paths.settingsFile);
    valid.replace("good=#22c55e", "good=#123456");
    valid.replace("toolPixelate=P\n", "toolPixelate=Alt+P\n");
    QVERIFY(atomicWrite(paths.settingsFile, valid));
    QVERIFY(settings.reloadNow());
    const QVariantMap shortcuts = settings.shortcuts();
    const QVariantMap hints = settings.shortcutHints();
    const QList<QByteArray> invalidFiles{
        "[Colors]\ngood=#112233\nmissing-equals\n",
        "[UnusedSection]\nmissing-equals\n[Colors]\ngood=#112233\n",
        "[Save]\npicturesRoot=/tmp/a,b\n",
        "[Save]\nvideosRoot=@ByteArray(/tmp/video)\n",
        "[Shortcuts]\ntoolPixelate=P, X\n",
        "[Colors]\ngood=#112233,#445566\n"};
    QSignalSpy published(&settings, &AppSettings::settingsChanged);
    for (const QByteArray &invalid : invalidFiles) {
        QVERIFY(atomicWrite(paths.settingsFile, invalid));
        QVERIFY2(!settings.reloadNow(), invalid.constData());
        QVERIFY(!settings.lastError().isEmpty());
        QCOMPARE(settings.goodColor(), QColor("#123456"));
        QCOMPARE(settings.shortcuts(), shortcuts);
        QCOMPARE(settings.shortcutHints(), hints);
        QCOMPARE(settings.picturesRoot(), paths.defaultPicturesRoot);
        QCOMPARE(settings.videosRoot(), paths.defaultVideosRoot);
        QCOMPARE(published.size(), 0);
        QCOMPARE(readBytes(paths.settingsFile), invalid);
    }
    // Quoted comma paths are one literal INI value and must be honored.
    const QString pictures = directory.filePath("pictures, with spaces");
    const QString videos = directory.filePath("videos; with #symbols");
    const QByteArray quoted = "[Save]\npicturesRoot=\"" + pictures.toUtf8()
        + "\"\nvideosRoot=\"" + videos.toUtf8() + "\"\n";
    QVERIFY(atomicWrite(paths.settingsFile, quoted));
    QVERIFY2(settings.reloadNow(), qPrintable(settings.lastError()));
    QCOMPARE(settings.picturesRoot(), pictures);
    QCOMPARE(settings.videosRoot(), videos);
}

void EditorTests::settingsPathsRoundTripSpecialCharacters() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto paths = settingsPaths(directory.path());
    paths.defaultPicturesRoot = directory.filePath("pictures, with spaces; #and=punctuation");
    paths.defaultVideosRoot = directory.filePath("videos with \"quotes\" and \\backslash");
    AppSettings settings(paths);
    QVERIFY2(settings.lastError().isEmpty(), qPrintable(settings.lastError()));
    QSettings parsed(paths.settingsFile, QSettings::IniFormat);
    QCOMPARE(parsed.value("Save/picturesRoot").metaType(), QMetaType::fromType<QString>());
    QCOMPARE(parsed.value("Save/picturesRoot").toString(), paths.defaultPicturesRoot);
    QCOMPARE(parsed.value("Save/videosRoot").toString(), QDir::fromNativeSeparators(paths.defaultVideosRoot));
    QCOMPARE(settings.picturesRoot(), QDir::fromNativeSeparators(paths.defaultPicturesRoot));
    QCOMPARE(settings.videosRoot(), QDir::fromNativeSeparators(paths.defaultVideosRoot));
    QVERIFY(settings.reloadNow());
    QCOMPARE(settings.picturesRoot(), QDir::fromNativeSeparators(paths.defaultPicturesRoot));
    QCOMPARE(settings.videosRoot(), QDir::fromNativeSeparators(paths.defaultVideosRoot));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
