#include <QtTest>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimeZone>
#include "imagedocument.h"
#include "editorcanvas.h"
#include "annotationfont.h"
#include "screenshotsave.h"

class ImageToolTests : public QObject {
    Q_OBJECT
private slots:
    void pixelationUsesSourceColors();
    void pixelateWheelPreviewAndExport();
    void selectionBoundsAndHistory();
    void eraseUsesExactDragStartPixel();
    void scaledGesturesAndClipboard();
    void highlightsPreviewAndHistory();
    void logicalGoodBadColorsFollowPalette();
    void annotationSizingAndPreview();
    void annotationFontSelectionAndTextReplay();
    void replayedAnnotationsAndExportPolicy();
    void replayedEditsKeepOperationOrder();
    void fractionalCutsKeepPrivacyMasksAligned();
    void eraseReplayKeepsExactSample();
    void annotationPreviewDoesNotSoftenOnRelease();
    void annotationPreviewAfterLargeCuts();
    void earlierAnnotationsStaySharpThroughRasterEdits();
    void fractionalCutsKeepEarlierAnnotationsInsideMasks();
    void failedSavePreservesSession();
    void savePngUsesUniqueNamesAndKeepsSource();
    void saveUsesConfiguredMediaRoot();
    void savePngUsesLocalYearAndMonthFolders();
};

static QImage sourceImage() {
    QImage image(80, 60, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x * 3, y * 4, (x + y) % 256, 80 + (x + y) % 176));
    return image;
}

void ImageToolTests::pixelationUsesSourceColors() {
    const QRect area(0, 0, 24, 12);
    QImage original(48, 36, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::white);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 24; ++x)
            original.setPixelColor(x, y, x < 6 ? Qt::red : x < 12 ? Qt::blue : Qt::green);
    QImage changedSource = original;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 6; ++x) changedSource.setPixelColor(x, y, Qt::yellow);
    ImageDocument first, second;
    first.reset(original); second.reset(changedSource);
    QVERIFY(first.blur(area)); QVERIFY(second.blur(area));
    const QImage pixelated = first.image();
    QCOMPARE(pixelated.pixelColor(0, 0), QColor(128, 0, 128)); // Red + blue averaged.
    QCOMPARE(pixelated.pixelColor(12, 0), QColor(Qt::green));
    QVERIFY(pixelated.pixel(0, 0) != second.image().pixel(0, 0));
    QCOMPARE(pixelated.pixel(12, 0), second.image().pixel(12, 0));
    for (int y = 0; y < original.height(); ++y) {
        for (int x = 0; x < original.width(); ++x) {
            if (area.contains(x, y)) {
                QCOMPARE(pixelated.pixel(x, y), pixelated.pixel((x / 12) * 12, 0));
            } else {
                QCOMPARE(pixelated.pixel(x, y), original.pixel(x, y));
            }
        }
    }
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(first.image().save(&buffer, "PNG"));
    const QImage exported = QImage::fromData(png, "PNG").convertToFormat(first.image().format());
    QCOMPARE(exported, first.image());
    first.undo(); QCOMPARE(first.image(), original);
    first.redo(); QCOMPARE(first.image(), pixelated);
}

void ImageToolTests::pixelateWheelPreviewAndExport() {
    const QImage original = sourceImage();
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("source.png");
    QVERIFY(original.save(sourcePath));
    EditorCanvas canvas;
    canvas.setWidth(272); canvas.setHeight(212); // 3 preview pixels per image pixel.
    QVERIFY(canvas.load(QUrl::fromLocalFile(sourcePath)));
    canvas.setTool("blur");
    QCOMPARE(canvas.pixelBlockSize(), 12);
    QCOMPARE(canvas.maxPixelBlockSize(), 30);
    canvas.adjustToolSize(120);
    QCOMPARE(canvas.pixelBlockSize(), 16);
    canvas.adjustToolSize(120 * 100);
    QCOMPARE(canvas.pixelBlockSize(), 30);
    canvas.adjustToolSize(-120 * 100);
    QCOMPARE(canvas.pixelBlockSize(), 4);
    canvas.adjustToolSize(120 * 2);
    QCOMPARE(canvas.pixelBlockSize(), 12);
    const auto point = [&canvas](QPointF imagePoint) {
        return canvas.imageRect().topLeft() + imagePoint * canvas.imageScale();
    };
    const auto snapshot = [&]() {
        QImage frame(272, 212, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        QPainter painter(&frame); canvas.paint(&painter);
        return frame;
    };
    const QRgb before = snapshot().pixel(point({18, 18}).toPoint());
    const QPointF start = point({8, 8}), end = point({58, 45});
    canvas.begin(start.x(), start.y()); canvas.move(end.x(), end.y());
    const QRgb live = snapshot().pixel(point({18, 18}).toPoint());
    canvas.end(end.x(), end.y());
    if (qEnvironmentVariableIsSet("XSHOT_RENDER_PREVIEW"))
        QVERIFY(snapshot().save(QDir::current().filePath("pixelate-preview.png")));
    QCOMPARE(snapshot().pixel(point({18, 18}).toPoint()), live);
    QVERIFY(canvas.copy());
    ImageDocument expected;
    expected.reset(original);
    QVERIFY(expected.blur(QRectF(QPointF(8, 8), QPointF(58, 45)), 12));
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(original.format()), expected.image());
    const QString savedPath = canvas.saveTo(temporary.path());
    QVERIFY(!savedPath.isEmpty());
    QCOMPARE(QImage(savedPath).convertToFormat(original.format()), expected.image());
    canvas.undo();
    QCOMPARE(snapshot().pixel(point({18, 18}).toPoint()), before);
    canvas.redo();
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(original.format()), expected.image());

    // A later wheel change only affects the next operation, including replay.
    canvas.adjustToolSize(60 * 3);
    QCOMPARE(canvas.pixelBlockSize(), 18);
    const QPointF secondStart = point({40, 20}), secondEnd = point({70, 50});
    canvas.begin(secondStart.x(), secondStart.y()); canvas.end(secondEnd.x(), secondEnd.y());
    QVERIFY(expected.blur(QRectF(QPointF(40, 20), QPointF(70, 50)), 18));
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(original.format()), expected.image());
}

void ImageToolTests::selectionBoundsAndHistory() {
    const QImage original = sourceImage();
    ImageDocument doc;
    doc.reset(original);
    QVERIFY(!doc.blur({20, 20, 0, 20}));
    QVERIFY(!doc.erase({90, 70, 20, 20}, {5, 5}));
    QVERIFY(!doc.canUndo());
    QVERIFY(doc.blur({20.9, 30.1, -30, -40}));
    const QRect expected(0, 0, 21, 31);
    for (int y = 0; y < original.height(); ++y)
        for (int x = 0; x < original.width(); ++x)
            if (!expected.contains(x, y)) QCOMPARE(doc.image().pixel(x, y), original.pixel(x, y));
            else QCOMPARE(doc.image().pixel(x, y), doc.image().pixel((x / 12) * 12, (y / 12) * 12));
    QVERIFY(doc.image().pixel(0, 0) != original.pixel(0, 0));
    doc.undo(); QCOMPARE(doc.image(), original);
    QVERIFY(doc.erase({-5, -5, 12, 12}, {-10, -10}));
    QCOMPARE(doc.image().pixel(6, 6), original.pixel(0, 0));
    QCOMPARE(doc.image().pixel(7, 7), original.pixel(7, 7));
    QVERIFY(!doc.canRedo());
}

void ImageToolTests::eraseUsesExactDragStartPixel() {
    const QImage original = sourceImage();
    const QList<QPointF> starts{{10.2, 8.6}, {60.8, 8.6}, {10.2, 40.8}, {60.8, 40.8}};
    for (const QPointF &start : starts) {
        const QPointF end(start.x() < 20 ? 60.8 : 10.2, start.y() < 20 ? 40.8 : 8.6);
        const QRect area = QRectF(start, end).normalized().toAlignedRect();
        const QRgb sample = original.pixel(int(start.x()), int(start.y()));
        ImageDocument doc; doc.reset(original);
        QVERIFY(doc.erase(QRectF(start, end), start));
        for (int y = 0; y < original.height(); ++y)
            for (int x = 0; x < original.width(); ++x)
                QCOMPARE(doc.image().pixel(x, y), area.contains(x, y) ? sample : original.pixel(x, y));
        const QImage erased = doc.image();
        doc.undo(); QCOMPARE(doc.image(), original);
        doc.redo(); QCOMPARE(doc.image(), erased);
    }
}

void ImageToolTests::scaledGesturesAndClipboard() {
    const QImage original = sourceImage();
    QTemporaryDir temporary;
    const QString path = temporary.filePath("source.png");
    QVERIFY(original.save(path));
    EditorCanvas canvas;
    canvas.setWidth(432); canvas.setHeight(332);
    QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
    QCOMPARE(canvas.imageScale(), 5.0);
    const auto viewPoint = [&canvas](QPointF source) {
        return canvas.imageRect().topLeft() + source * canvas.imageScale();
    };
    const QPointF sourceStart(60.8, 40.8), sourceEnd(20.1, 10.1);
    const QPointF start = viewPoint(sourceStart), end = viewPoint(sourceEnd);
    canvas.setTool("erase");
    canvas.begin(start.x(), start.y()); canvas.end(end.x(), end.y());
    QVERIFY(canvas.copy());
    ImageDocument expected; expected.reset(original);
    QVERIFY(expected.erase(QRectF(sourceStart, sourceEnd), sourceStart));
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(original.format()), expected.image());
    canvas.undo();
    canvas.setTool("blur");
    canvas.begin(start.x(), start.y()); canvas.move(end.x(), end.y()); canvas.cancel();
    QVERIFY(!canvas.canUndo());
    canvas.begin(start.x(), start.y()); canvas.end(end.x(), end.y());
    QVERIFY(canvas.copy());
    expected.reset(original); QVERIFY(expected.blur(QRectF(sourceStart, sourceEnd)));
    QCOMPARE(QGuiApplication::clipboard()->image().convertToFormat(original.format()), expected.image());
}

void ImageToolTests::highlightsPreviewAndHistory() {
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    QTemporaryDir temporary;
    const QString path = temporary.filePath("white.png");
    QVERIFY(white.save(path));
    EditorCanvas canvas;
    canvas.setWidth(432); canvas.setHeight(332); // 5x fit preview.
    QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
    canvas.setTool("highlight");
    canvas.setGoodColor(QColor("#386ab3"));
    canvas.setInkMode("good");
    const auto point = [&canvas](QPointF source) {
        return canvas.imageRect().topLeft() + source * canvas.imageScale();
    };
    const QPointF start = point({60, 40}), end = point({10, 10});
    canvas.begin(start.x(), start.y());
    canvas.move(end.x(), end.y());
    QImage preview(432, 332, QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    { QPainter painter(&preview); canvas.paint(&painter); }
    canvas.end(end.x(), end.y());
    QVERIFY(canvas.copy());
    const QImage one = QGuiApplication::clipboard()->image();
    QCOMPARE(one.size(), QSize(240, 180));
    const auto pixelAt = [](const QImage &image, int x, int y) {
        return image.pixelColor(x * image.width() / 80, y * image.height() / 60);
    };
    QVERIFY(pixelAt(one, 30, 25).blue() >= 177 && pixelAt(one, 30, 25).blue() <= 179);
    QCOMPARE(pixelAt(one, 9, 25), QColor(Qt::white));
    QCOMPARE(pixelAt(one, 61, 25), QColor(Qt::white));
    QCOMPARE(preview.pixelColor(point({30, 25}).toPoint()), pixelAt(one, 30, 25));

    canvas.begin(point({30, 20}).x(), point({30, 20}).y());
    canvas.end(point({70, 50}).x(), point({70, 50}).y());
    QVERIFY(canvas.copy());
    const QImage overlap = QGuiApplication::clipboard()->image();
    QVERIFY(pixelAt(overlap, 40, 30).blue() < pixelAt(one, 40, 30).blue());
    canvas.setBadColor(QColor("#d94c63"));
    canvas.setInkMode("bad");
    canvas.begin(point({5, 5}).x(), point({5, 5}).y());
    canvas.end(point({15, 15}).x(), point({15, 15}).y());
    QVERIFY(canvas.copy());
    const QImage bothModes = QGuiApplication::clipboard()->image();
    QVERIFY(pixelAt(bothModes, 7, 7).green() < pixelAt(bothModes, 7, 7).red());
    QCOMPARE(pixelAt(bothModes, 20, 20), pixelAt(overlap, 20, 20));
    canvas.undo(); canvas.undo();
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), one);
    canvas.redo(); canvas.redo();
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), bothModes);
}

void ImageToolTests::logicalGoodBadColorsFollowPalette() {
    EditorCanvas canvas;
    canvas.setGoodColor(QColor("#386ab3"));
    canvas.setBadColor(QColor("#d94c63"));
    canvas.setInkMode("good");
    QCOMPARE(canvas.colorMode(), QString("good"));
    QCOMPARE(canvas.ink(), QColor("#386ab3"));
    canvas.setGoodColor(QColor("#36b37e"));
    QCOMPARE(canvas.colorMode(), QString("good"));
    QCOMPARE(canvas.ink(), QColor("#36b37e"));
    canvas.setInkMode("bad");
    QCOMPARE(canvas.colorMode(), QString("bad"));
    QCOMPARE(canvas.ink(), QColor("#d94c63"));
    canvas.setBadColor(QColor("#a7475e"));
    QCOMPARE(canvas.colorMode(), QString("bad"));
    QCOMPARE(canvas.ink(), QColor("#a7475e"));
}

void ImageToolTests::annotationSizingAndPreview() {
    QImage white(1200, 800, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    QTemporaryDir temporary;
    const QString path = temporary.filePath("large.png");
    QVERIFY(white.save(path));
    EditorCanvas canvas;
    canvas.setWidth(1232); canvas.setHeight(832); // 1 source pixel per preview pixel.
    QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
    QCOMPARE(canvas.maxStrokeWidth(), 64);
    QCOMPARE(canvas.maxTextSize(), 144);
    canvas.setTool("rect");
    canvas.adjustToolSize(120 * 100);
    QCOMPARE(canvas.strokeWidth(), 64);
    canvas.adjustToolSize(-120 * 200);
    QCOMPARE(canvas.strokeWidth(), 1);
    canvas.adjustToolSize(60 * 19);
    QCOMPARE(canvas.strokeWidth(), 20);
    const auto point = [&canvas](QPointF source) {
        return canvas.imageRect().topLeft() + source * canvas.imageScale();
    };
    const QPointF start = point({100, 100}), end = point({300, 300});
    canvas.begin(start.x(), start.y()); canvas.move(end.x(), end.y());
    canvas.adjustToolSize(120);
    QCOMPARE(canvas.strokeWidth(), 22);
    canvas.adjustToolSize(-120);
    QCOMPARE(canvas.strokeWidth(), 20);
    QImage preview(1232, 832, QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    { QPainter painter(&preview); canvas.paint(&painter); }
    canvas.end(end.x(), end.y());
    QVERIFY(canvas.copy());
    const QImage rectangle = QGuiApplication::clipboard()->image();
    QCOMPARE(preview.pixelColor(point({100, 200}).toPoint()), rectangle.pixelColor(100, 200));
    QCOMPARE(rectangle.pixelColor(95, 200), QColor("#ef4444"));
    QCOMPARE(rectangle.pixelColor(88, 200), QColor(Qt::white));
    canvas.setTool("arrow");
    canvas.adjustToolSize(120 * 100);
    const QPointF arrowStart = point({400, 100}), arrowEnd = point({450, 100});
    canvas.begin(arrowStart.x(), arrowStart.y()); canvas.end(arrowEnd.x(), arrowEnd.y());
    QVERIFY(canvas.copy());
    const QImage arrow = QGuiApplication::clipboard()->image();
    QCOMPARE(arrow.pixelColor(420, 143), QColor("#ef4444")); // Thick head exceeds shaft radius.
    canvas.undo(); QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), rectangle);
    canvas.redo(); QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), arrow);
    canvas.setTool("text");
    canvas.adjustToolSize(-120 * 100);
    QCOMPARE(canvas.textSize(), 8);
    canvas.adjustToolSize(120 * 32);
    QCOMPARE(canvas.textSize(), 72);
    canvas.addText(500, 200, 500, 300, "Whole draft", canvas.textSize());
    QVERIFY(canvas.copy());
    const QImage largeText = QGuiApplication::clipboard()->image();
    canvas.adjustToolSize(-120 * 32);
    canvas.addText(500, 400, 500, 300, "Whole draft", canvas.textSize());
    QVERIFY(canvas.copy());
    const QImage bothText = QGuiApplication::clipboard()->image();
    QCOMPARE(largeText.pixelColor(520, 230), bothText.pixelColor(520, 230));
    QVERIFY(largeText != bothText);
    QImage small(80, 60, QImage::Format_ARGB32_Premultiplied);
    small.fill(Qt::white);
    const QString smallPath = temporary.filePath("small.png");
    QVERIFY(small.save(smallPath));
    QVERIFY(canvas.load(QUrl::fromLocalFile(smallPath)));
    QCOMPARE(canvas.maxStrokeWidth(), 7);
    QCOMPARE(canvas.maxTextSize(), 20);
    canvas.setTool("arrow"); canvas.adjustToolSize(120 * 100);
    QCOMPARE(canvas.strokeWidth(), 7);
    canvas.setTool("text"); canvas.adjustToolSize(120 * 100);
    QCOMPARE(canvas.textSize(), 20);
}

void ImageToolTests::replayedAnnotationsAndExportPolicy() {
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    ImageDocument doc;
    doc.reset(white);
    QVERIFY(!doc.hasAnnotations());
    QCOMPARE(doc.exportScale(), 1.0);
    QCOMPARE(doc.render(doc.exportScale()).size(), white.size());
    QVERIFY(doc.blur(QRectF(1, 1, 5, 5)));
    QCOMPARE(doc.exportScale(), 1.0); // Raster-only edits stay at natural size.
    QVERIFY(doc.annotate("arrow", {8, 30}, {75, 30}, Qt::red, 2));
    QVERIFY(doc.hasAnnotations());
    QCOMPARE(doc.exportScale(), 3.0);
    QCOMPARE(doc.render(doc.exportScale()).size(), QSize(240, 180));
    const QImage enlarged = doc.render(3);
    QVERIFY(enlarged.pixelColor(60, 90).red() > enlarged.pixelColor(60, 90).green());
    doc.undo();
    QVERIFY(!doc.hasAnnotations());
    QCOMPARE(doc.exportScale(), 1.0);
    doc.redo();
    QCOMPARE(doc.render(3), enlarged);

    QImage adequate(1200, 800, QImage::Format_ARGB32_Premultiplied);
    adequate.fill(Qt::white);
    doc.reset(adequate);
    QVERIFY(doc.text(QRectF(10, 10, 300, 100), "Sharp text", Qt::red, 24));
    QCOMPARE(doc.exportScale(), 1.0);
    QCOMPARE(doc.render(doc.exportScale()).size(), adequate.size());
}

void ImageToolTests::annotationFontSelectionAndTextReplay() {
    const QString system = QStringLiteral("System Sans");
    QCOMPARE(annotationfont::selectFamily({QStringLiteral("Impact"), QStringLiteral("Segoe UI")},
                                          annotationfont::Platform::Windows, system), QStringLiteral("Impact"));
    QCOMPARE(annotationfont::selectFamily({QStringLiteral("Impact"), QStringLiteral("Helvetica")},
                                          annotationfont::Platform::MacOS, system), QStringLiteral("Impact"));
    QCOMPARE(annotationfont::selectFamily({QStringLiteral("Segoe UI"), system},
                                          annotationfont::Platform::Windows, system), QStringLiteral("Segoe UI"));
    QCOMPARE(annotationfont::selectFamily({QStringLiteral("Helvetica"), system},
                                          annotationfont::Platform::MacOS, system), QStringLiteral("Helvetica"));
    QCOMPARE(annotationfont::selectFamily({system}, annotationfont::Platform::Windows, system), system);
    QCOMPARE(annotationfont::selectFamily({system}, annotationfont::Platform::MacOS, system), system);

    const QFont font = annotationfont::make(17);
    QCOMPARE(font.family(), annotationfont::family());
    QCOMPARE(font.pixelSize(), 17);
    QCOMPARE(font.weight(), QFont::DemiBold);

    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    ImageDocument document;
    document.reset(white);
    QVERIFY(document.text(QRectF(3, 3, 70, 18), QStringLiteral("Single line"), Qt::red, 12));
    const QImage singleLine = document.image();
    QVERIFY(singleLine != white);
    QVERIFY(document.text(QRectF(3, 24, 44, 33), QStringLiteral("Wrapped\nannotation"), Qt::blue, 12));
    QVERIFY(document.image() != singleLine);
    QVERIFY(document.hasAnnotations());
    QCOMPARE(document.exportScale(), 3.0);
    const QImage enlarged = document.render(document.exportScale());
    QCOMPARE(enlarged.size(), QSize(240, 180));
    QVERIFY(enlarged != document.image().scaled(enlarged.size(), Qt::IgnoreAspectRatio,
                                                 Qt::FastTransformation));
    document.undo();
    const QImage singleLineExport = document.render(3.0);
    QVERIFY(singleLineExport != enlarged);
    document.redo();
    QCOMPARE(document.render(3.0), enlarged);
}

void ImageToolTests::replayedEditsKeepOperationOrder() {
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    ImageDocument doc;
    doc.reset(white);
    QVERIFY(doc.annotate("rect", {10, 10}, {70, 50}, Qt::red, 2));
    const QImage beforeCut = doc.render(3);
    QVERIFY(beforeCut.pixelColor(45, 30).red() > beforeCut.pixelColor(45, 30).green());
    QVERIFY(doc.cut(true, 20, 40));
    const QImage afterCut = doc.render(3);
    QCOMPARE(afterCut.size(), QSize(180, 180));
    QVERIFY(afterCut.pixelColor(45, 30).red() > afterCut.pixelColor(45, 30).green());
    QVERIFY(afterCut.pixelColor(120, 30).red() > afterCut.pixelColor(120, 30).green());
    QVERIFY(doc.blur(QRectF(0, 8, 25, 8)));
    const QImage masked = doc.render(3);
    QVERIFY(masked.pixel(45, 30) != afterCut.pixel(45, 30));
    QVERIFY(masked.pixelColor(120, 30).red() > masked.pixelColor(120, 30).green());
    doc.undo();
    QCOMPARE(doc.render(3), afterCut);
    doc.redo();
    QCOMPARE(doc.render(3), masked);
    QVERIFY(doc.erase(QRectF(35, 8, 15, 8), {35, 8}));
    const QImage erased = doc.render(3);
    QVERIFY(erased.pixelColor(120, 30).red() <= erased.pixelColor(120, 30).green() + 20);
    QVERIFY(doc.annotate("highlight", {35, 8}, {50, 16}, Qt::green, 2, true));
    const QImage lateAnnotation = doc.render(3);
    QVERIFY(lateAnnotation != erased); // A later annotation remains above the erase.

    doc.reset(white);
    QVERIFY(doc.annotate("rect", {10, 10}, {18, 18}, Qt::red, 2));
    QVERIFY(doc.cut(true, 8, 22));
    QVERIFY(!doc.hasAnnotations());
    QCOMPARE(doc.exportScale(), 1.0);
    doc.undo();
    QVERIFY(doc.hasAnnotations());
    QVERIFY(doc.blur(QRectF(0, 0, 25, 25)));
    QVERIFY(!doc.hasAnnotations());
    doc.undo();
    QVERIFY(doc.erase(QRectF(0, 0, 25, 25), {40, 40}));
    QVERIFY(!doc.hasAnnotations());

    QImage secret = white;
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x)
            secret.setPixelColor(x, y, Qt::black);
    ImageDocument clean, changed;
    clean.reset(white); changed.reset(secret);
    QVERIFY(clean.annotate("arrow", {40, 40}, {70, 40}, Qt::red, 2));
    QVERIFY(changed.annotate("arrow", {40, 40}, {70, 40}, Qt::red, 2));
    QVERIFY(clean.blur(QRectF(10, 10, 20, 20)));
    QVERIFY(changed.blur(QRectF(10, 10, 20, 20)));
    QVERIFY(clean.render(3) != changed.render(3)); // Coarse source colors remain visible.
    QCOMPARE(clean.render(3).pixel(150, 120), changed.render(3).pixel(150, 120));

    QImage cutSecret = white;
    for (int y = 0; y < cutSecret.height(); ++y)
        for (int x = 10; x < 20; ++x)
            cutSecret.setPixelColor(x, y, Qt::black);
    clean.reset(white); changed.reset(cutSecret);
    QVERIFY(clean.cut(true, 10, 20));
    QVERIFY(changed.cut(true, 10, 20));
    QVERIFY(clean.annotate("arrow", {30, 40}, {60, 40}, Qt::red, 2));
    QVERIFY(changed.annotate("arrow", {30, 40}, {60, 40}, Qt::red, 2));
    QCOMPARE(clean.render(1.5), changed.render(1.5));

    clean.reset(white); changed.reset(cutSecret);
    QVERIFY(clean.cut(true, 1, 2));
    QVERIFY(changed.cut(true, 1, 2));
    QVERIFY(clean.cut(true, 9, 19));
    QVERIFY(changed.cut(true, 9, 19));
    QVERIFY(clean.annotate("arrow", {25, 40}, {55, 40}, Qt::red, 2));
    QVERIFY(changed.annotate("arrow", {25, 40}, {55, 40}, Qt::red, 2));
    QCOMPARE(clean.render(1.5), changed.render(1.5));
}

void ImageToolTests::fractionalCutsKeepPrivacyMasksAligned() {
    QImage white(800, 480, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    QImage secret = white;
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x) secret.setPixelColor(x, y, Qt::black);

    for (const bool vertical : {true, false}) {
        for (const bool repeated : {false, true}) {
            for (const bool erase : {false, true}) {
                ImageDocument clean, changed;
                clean.reset(white); changed.reset(secret);
                auto editBoth = [&](auto edit) { QVERIFY(edit(clean)); QVERIFY(edit(changed)); };
                editBoth([&](ImageDocument &doc) { return doc.cut(vertical, 1, 2); });
                if (repeated)
                    editBoth([&](ImageDocument &doc) { return doc.cut(vertical, 3, 4); });
                const int origin = repeated ? 8 : 9;
                const QRectF area = vertical ? QRectF(origin, 10, 20, 20)
                                             : QRectF(10, origin, 20, 20);
                if (erase)
                    editBoth([&](ImageDocument &doc) { return doc.erase(area, {0, 0}); });
                else
                    editBoth([&](ImageDocument &doc) { return doc.blur(area, 18); });
                editBoth([&](ImageDocument &doc) {
                    return doc.annotate("arrow", {300, 300}, {350, 300}, Qt::red, 2);
                });
                if (erase) QCOMPARE(clean.image(), changed.image());
                else {
                    QVERIFY(clean.image() != changed.image()); // Source colors affect the coarse blocks.
                    for (int y = 0; y < clean.image().height(); ++y)
                        for (int x = 0; x < clean.image().width(); ++x)
                            if (!area.contains(QPointF(x, y)))
                                QCOMPARE(clean.image().pixel(x, y), changed.image().pixel(x, y));
                }
                for (const qreal scale : {1.25, 1.5, 2.5}) {
                    const QImage first = clean.render(scale), second = changed.render(scale);
                    if (erase) QCOMPARE(first, second);
                    else {
                        QVERIFY(first != second);
                        QCOMPARE(first.pixel(qRound(100 * scale), qRound(100 * scale)),
                                 second.pixel(qRound(100 * scale), qRound(100 * scale)));
                    }
                    QCOMPARE(first.size(), QSize(qRound(clean.image().width() * scale),
                                                 qRound(clean.image().height() * scale)));
                    QImage previewA(first.size(), first.format()), previewB(first.size(), first.format());
                    previewA.fill(Qt::transparent); previewB.fill(Qt::transparent);
                    QPainter a(&previewA), b(&previewB);
                    a.scale(scale, scale); b.scale(scale, scale);
                    clean.paint(a, scale); changed.paint(b, scale);
                    a.end(); b.end();
                    if (erase) QCOMPARE(previewA, previewB);
                    else {
                        QVERIFY(previewA != previewB);
                        QCOMPARE(previewA.pixel(qRound(100 * scale), qRound(100 * scale)),
                                 previewB.pixel(qRound(100 * scale), qRound(100 * scale)));
                    }
                }
                clean.undo(); changed.undo();
                if (erase) QCOMPARE(clean.render(1.5), changed.render(1.5));
                else QVERIFY(clean.render(1.5) != changed.render(1.5));
                clean.redo(); changed.redo();
                if (erase) QCOMPARE(clean.render(clean.exportScale()), changed.render(changed.exportScale()));
                else QVERIFY(clean.render(clean.exportScale()) != changed.render(changed.exportScale()));
            }
        }
    }

    // Exercise the actual Copy and Save export paths, not only the document API.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString whitePath = temporary.filePath("white.png");
    const QString secretPath = temporary.filePath("secret.png");
    QVERIFY(white.save(whitePath)); QVERIFY(secret.save(secretPath));
    const auto exported = [&](const QString &source, QImage &copy, QImage &saved) {
        EditorCanvas canvas;
        canvas.setWidth(832); canvas.setHeight(512); // Natural-size gesture mapping.
        QVERIFY(canvas.load(QUrl::fromLocalFile(source)));
        const auto drag = [&](const QString &tool, QPointF from, QPointF to) {
            canvas.setTool(tool);
            const auto point = [&](QPointF imagePoint) {
                return canvas.imageRect().topLeft() + imagePoint * canvas.imageScale();
            };
            const QPointF start = point(from), end = point(to);
            canvas.begin(start.x(), start.y()); canvas.end(end.x(), end.y());
        };
        drag("cut", {1, 40}, {2, 40});
        drag("blur", {9, 10}, {29, 30});
        drag("arrow", {300, 300}, {350, 300});
        QVERIFY(canvas.copy());
        copy = QGuiApplication::clipboard()->image();
        const QString path = canvas.saveTo(temporary.path());
        QVERIFY(!path.isEmpty());
        saved = QImage(path);
        QVERIFY(!saved.isNull());
    };
    QImage cleanCopy, cleanSaved, secretCopy, secretSaved;
    exported(whitePath, cleanCopy, cleanSaved);
    exported(secretPath, secretCopy, secretSaved);
    QCOMPARE(cleanCopy.convertToFormat(QImage::Format_ARGB32_Premultiplied),
             cleanSaved.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    QCOMPARE(secretCopy.convertToFormat(QImage::Format_ARGB32_Premultiplied),
             secretSaved.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    QVERIFY(cleanCopy != secretCopy);
    QCOMPARE(cleanCopy.pixel(300, 300), secretCopy.pixel(300, 300));
    QCOMPARE(cleanCopy.size(), QSize(1199, 720));
}

void ImageToolTests::eraseReplayKeepsExactSample() {
    for (const bool transparent : {false, true}) {
        QImage source(80, 60, QImage::Format_ARGB32_Premultiplied);
        source.fill(transparent ? Qt::transparent : Qt::white);
        ImageDocument doc;
        doc.reset(source);
        QVERIFY(doc.annotate("rect", {10.25, 10}, {30.25, 40},
                             transparent ? QColor(255, 0, 0, 127) : Qt::red, 1));
        const QRgb sample = doc.image().pixel(10, 20);
        QVERIFY(sample != source.pixel(10, 20));
        QVERIFY(doc.erase(QRectF(10.2, 20, 49.8, 20), {10.2, 20}));
        QCOMPARE(doc.image().pixel(50, 30), sample);
        for (const qreal scale : {1.0, 1.25, 1.5, 3.0}) {
            const QImage rendered = doc.render(scale);
            QCOMPARE(rendered.pixel(qRound(50 * scale), qRound(30 * scale)), sample);
        }
        const QImage erased = doc.render(3);
        doc.undo(); doc.redo();
        QCOMPARE(doc.render(3), erased);
        QVERIFY(doc.annotate("arrow", {2, 50}, {70, 50}, Qt::blue, 2));
        QCOMPARE(doc.render(3).pixel(150, 90), sample);
        QCOMPARE(doc.render(doc.exportScale()).pixel(150, 90), sample);

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString sourcePath = temporary.filePath("source.png");
        QVERIFY(source.save(sourcePath));
        EditorCanvas canvas;
        canvas.setWidth(272); canvas.setHeight(212); // Three display pixels per image pixel.
        QVERIFY(canvas.load(QUrl::fromLocalFile(sourcePath)));
        const auto point = [&](QPointF imagePoint) {
            return canvas.imageRect().topLeft() + imagePoint * canvas.imageScale();
        };
        canvas.setTool("rect");
        canvas.setInk(transparent ? QColor(255, 0, 0, 127) : QColor(Qt::red));
        canvas.adjustToolSize(-360);
        const QPointF rectFrom = point({10.25, 10}), rectTo = point({30.25, 40});
        canvas.begin(rectFrom.x(), rectFrom.y()); canvas.end(rectTo.x(), rectTo.y());
        canvas.setTool("erase");
        const QPointF eraseFrom = point({10.2, 20}), eraseTo = point({60, 40});
        canvas.begin(eraseFrom.x(), eraseFrom.y()); canvas.move(eraseTo.x(), eraseTo.y());
        const auto snapshot = [&]() {
            QImage frame(272, 212, QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            QPainter painter(&frame); canvas.paint(&painter);
            return frame;
        };
        const QRgb live = snapshot().pixel(point({50, 30}).toPoint());
        canvas.end(eraseTo.x(), eraseTo.y());
        const QRgb committed = snapshot().pixel(point({50, 30}).toPoint());
        QCOMPARE(live, committed);
        QVERIFY(canvas.copy());
        const QImage copied = QGuiApplication::clipboard()->image();
        QCOMPARE(copied.pixel(150, 90), sample);
        const QString savedPath = canvas.saveTo(temporary.path());
        QVERIFY(!savedPath.isEmpty());
        QCOMPARE(QImage(savedPath).convertToFormat(QImage::Format_ARGB32_Premultiplied)
                     .pixel(150, 90), sample);
    }
}

void ImageToolTests::annotationPreviewDoesNotSoftenOnRelease() {
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    QTemporaryDir temporary;
    const QString path = temporary.filePath("small.png");
    QVERIFY(white.save(path));
    for (const QString &tool : {QStringLiteral("arrow"), QStringLiteral("rect"), QStringLiteral("highlight")}) {
      for (const qreal zoom : {2.5, 3.0}) {
       for (const bool cutFirst : {false, true}) {
        for (const int displayScale : {1, 2}) {
            EditorCanvas canvas;
            canvas.setWidth(80 * zoom + 32); canvas.setHeight(60 * zoom + 32);
            QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
            if (cutFirst) {
                canvas.setTool("cut");
                const auto cut = [&](int from, int to) {
                    const QPointF origin = canvas.imageRect().topLeft();
                    const qreal scale = canvas.imageScale();
                    canvas.begin(origin.x() + from * scale, origin.y() + 20 * scale);
                    canvas.end(origin.x() + to * scale, origin.y() + 20 * scale);
                };
                cut(3, 5); cut(10, 13);
            }
            canvas.setTool(tool);
            if (tool != "highlight") canvas.adjustToolSize(-60); // Thin 3 px stroke.
            const auto point = [&](QPointF source) {
                return canvas.imageRect().topLeft() + source * canvas.imageScale();
            };
            const QPointF start = point({5, 15}), end = point({72, 42});
            canvas.begin(start.x(), start.y()); canvas.move(end.x(), end.y());
            const QSize viewport(qRound(canvas.width() * displayScale), qRound(canvas.height() * displayScale));
            const auto snapshot = [&]() {
                QImage frame(viewport, QImage::Format_ARGB32_Premultiplied);
                frame.fill(Qt::transparent);
                QPainter painter(&frame);
                painter.scale(displayScale, displayScale);
                canvas.paint(&painter);
                return frame;
            };
            const QImage live = snapshot();
            canvas.end(end.x(), end.y());
            const QImage committed = snapshot();
            QVERIFY2(committed == live,
                     qPrintable(QStringLiteral("%1 zoom %2 cut %3 dpr %4")
                         .arg(tool).arg(zoom).arg(cutFirst).arg(displayScale)));
        }
       }
      }
    }

    ImageDocument text;
    text.reset(white);
    QVERIFY(text.text(QRectF(3, 3, 74, 40), "Sharp", Qt::red, 14));
    const QImage fresh = text.render(3);
    const QImage stretched = text.image().scaled(fresh.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVERIFY(fresh != stretched); // Export re-renders glyphs, not the flattened bitmap.
}

void ImageToolTests::annotationPreviewAfterLargeCuts() {
    // A tiny remainder of a large capture can have a much larger preview zoom
    // than its export scale. Budget the rendered remainder, not the old capture.
    QImage original(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::white);
    ImageDocument fullSize;
    fullSize.reset(original);
    QVERIFY(fullSize.render(3).isNull()); // The limit still protects genuinely large output.
    QTemporaryDir temporary;
    const QString path = temporary.filePath("large.png");
    QVERIFY(original.save(path));
    EditorCanvas canvas;
    canvas.setWidth(832); canvas.setHeight(632);
    QVERIFY(canvas.load(QUrl::fromLocalFile(path)));
    const auto point = [&](QPointF source) {
        return canvas.imageRect().topLeft() + source * canvas.imageScale();
    };
    canvas.setTool("cut");
    QPointF from = point({80, 30}), to = point({3840, 30});
    canvas.begin(from.x(), from.y()); canvas.end(to.x(), to.y());
    from = point({30, 60}); to = point({30, 2160});
    canvas.begin(from.x(), from.y()); canvas.end(to.x(), to.y());
    QCOMPARE(canvas.imageWidth(), 80);
    QCOMPARE(canvas.imageHeight(), 60);

    for (const int dpr : {1, 2}) {
        const auto snapshot = [&]() {
            QImage frame(832 * dpr, 632 * dpr, QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            QPainter painter(&frame);
            painter.scale(dpr, dpr);
            canvas.paint(&painter);
            return frame;
        };
        canvas.setTool("arrow");
        from = point({5, 15}); to = point({72, 42});
        canvas.begin(from.x(), from.y()); canvas.move(to.x(), to.y());
        const QImage live = snapshot();
        canvas.end(to.x(), to.y());
        QCOMPARE(snapshot(), live);
        canvas.undo();

        canvas.addText(3, 3, 74, 40, "Sharp", 14);
        ImageDocument reference;
        reference.reset(original.copy(0, 0, 80, 60));
        QVERIFY(reference.text(QRectF(3, 3, 74, 40), "Sharp", canvas.ink(), 14));
        QImage expected(832 * dpr, 632 * dpr, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::transparent);
        {
            QPainter painter(&expected);
            painter.scale(dpr, dpr);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.translate(canvas.imageRect().topLeft());
            painter.scale(canvas.imageScale(), canvas.imageScale());
            painter.setClipRect(QRectF(0, 0, 80, 60));
            reference.paint(painter, canvas.imageScale() * dpr);
        }
        QCOMPARE(snapshot(), expected);
        canvas.undo();
    }
}

void ImageToolTests::earlierAnnotationsStaySharpThroughRasterEdits() {
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    const auto addShape = [](ImageDocument &doc, const QString &shape) {
        if (shape == "text") return doc.text(QRectF(10, 12, 65, 30), "Sharp", Qt::red, 13);
        return doc.annotate(shape, {10.25, 10.25}, {70.25, 50.25}, Qt::red, 1);
    };
    for (const QString &shape : {QStringLiteral("arrow"), QStringLiteral("rect"),
                                 QStringLiteral("text")}) {
        for (const qreal scale : {1.25, 1.5, 3.0}) {
            ImageDocument doc;
            doc.reset(white);
            QVERIFY(addShape(doc, shape));
            const QImage before = doc.render(scale);
            QVERIFY(doc.blur(QRectF(0, 0, 5, 5)));
            const QImage blurred = doc.render(scale);
            const QRect untouched(qRound(8 * scale), qRound(8 * scale),
                                  qRound(65 * scale), qRound(45 * scale));
            QCOMPARE(blurred.copy(untouched), before.copy(untouched));
            doc.undo(); QCOMPARE(doc.render(scale), before);
            doc.redo(); QCOMPARE(doc.render(scale), blurred);
            QVERIFY(doc.erase(QRectF(0, 0, 5, 5), {6, 6}));
            QCOMPARE(doc.render(scale).copy(untouched), before.copy(untouched));

            doc.reset(white);
            QVERIFY(addShape(doc, shape));
            const QImage beforeCut = doc.render(scale);
            QVERIFY(doc.cut(true, 2, 5));
            const QImage afterCut = doc.render(scale);
            ImageDocument repositioned;
            repositioned.reset(white);
            QVERIFY(repositioned.cut(true, 2, 5));
            if (shape == "text")
                QVERIFY(repositioned.text(QRectF(7, 12, 65, 30), "Sharp", Qt::red, 13));
            else
                QVERIFY(repositioned.annotate(shape, {7.25, 10.25}, {67.25, 50.25}, Qt::red, 1));
            QCOMPARE(afterCut, repositioned.render(scale));
            // Check the resulting edges against an enlarged native frame too.
            const QImage flattened = doc.image().scaled(afterCut.size(), Qt::IgnoreAspectRatio,
                                                        Qt::FastTransformation);
            QVERIFY(afterCut != flattened);
            QVERIFY(beforeCut != afterCut);
        }
    }

    ImageDocument doc;
    doc.reset(white);
    QVERIFY(doc.annotate("arrow", {10.25, 20.25}, {70.25, 20.25}, Qt::red, 1));
    const QImage beforeCut = doc.render(3);
    QVERIFY(doc.cut(true, 30, 40)); // Remove the middle of the existing arrow.
    const QImage afterCut = doc.render(3);
    QCOMPARE(afterCut.copy(QRect(0, 0, 90, 180)), beforeCut.copy(QRect(0, 0, 90, 180)));
    QCOMPARE(afterCut.copy(QRect(90, 0, 120, 180)), beforeCut.copy(QRect(120, 0, 120, 180)));
    QVERIFY(doc.blur(QRectF(10, 18, 8, 5)));
    const QImage masked = doc.render(3);
    QVERIFY(masked.pixel(39, 60) != afterCut.pixel(39, 60));
    QVERIFY(masked.pixelColor(150, 60).red() > masked.pixelColor(150, 60).green());
    QVERIFY(doc.annotate("arrow", {10, 20}, {20, 20}, Qt::blue, 1));
    QVERIFY(doc.render(3) != masked); // Later annotation remains above the mask.

    QImage secret = white;
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x) secret.setPixelColor(x, y, Qt::black);
    ImageDocument clean, changed;
    clean.reset(white); changed.reset(secret);
    for (ImageDocument *document : {&clean, &changed}) {
        QVERIFY(document->annotate("arrow", {50.25, 35.25}, {70.25, 35.25}, Qt::red, 1));
        QVERIFY(document->cut(true, 1, 2));
        QVERIFY(document->blur(QRectF(9, 10, 20, 20)));
    }
    for (const qreal scale : {1.25, 1.5, 3.0}) {
        QVERIFY(clean.render(scale) != changed.render(scale));
        QCOMPARE(clean.render(scale).pixel(qRound(50 * scale), qRound(50 * scale)),
                 changed.render(scale).pixel(qRound(50 * scale), qRound(50 * scale)));
    }

    ImageDocument plainMask, coveredAnnotation;
    plainMask.reset(white); coveredAnnotation.reset(white);
    QVERIFY(coveredAnnotation.annotate("rect", {5, 5}, {35, 35}, Qt::red, 2));
    QVERIFY(plainMask.blur(QRectF(10, 10, 20, 20)));
    QVERIFY(coveredAnnotation.blur(QRectF(10, 10, 20, 20)));
    for (const qreal scale : {1.25, 1.5, 3.0}) {
        const QImage expected = plainMask.render(scale);
        const QImage actual = coveredAnnotation.render(scale);
        QVERIFY(actual != expected); // Earlier red annotation contributes to block averages.
        QCOMPARE(actual.pixel(qRound(50 * scale), qRound(50 * scale)),
                 expected.pixel(qRound(50 * scale), qRound(50 * scale)));
    }
}

void ImageToolTests::fractionalCutsKeepEarlierAnnotationsInsideMasks() {
    QImage white(800, 480, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    for (const bool vertical : {true, false}) {
        for (const bool mixed : {false, true}) {
            for (const bool erase : {false, true}) {
                for (const bool text : {false, true}) {
                    ImageDocument clean, covered;
                    clean.reset(white); covered.reset(white);
                    for (ImageDocument *doc : {&clean, &covered})
                        QVERIFY(doc->annotate("arrow", {300, 300}, {350, 300}, Qt::blue, 2));
                    if (text)
                        QVERIFY(covered.text(QRectF(40, 20, 40, 40), "Secret", Qt::red, 18));
                    else
                        QVERIFY(covered.annotate("rect", {40, 20}, {80, 60}, Qt::red, 2));
                    const QList<QPair<int, int>> cuts = mixed
                        ? QList<QPair<int, int>>{{1, 2}, {3, 5}, {2, 3}, {0, 1}}
                        : QList<QPair<int, int>>{{1, 2}, {1, 2}, {1, 2},
                                                 {1, 2}, {1, 2}, {1, 2}};
                    int removed = 0;
                    for (const auto &cut : cuts) {
                        for (ImageDocument *doc : {&clean, &covered})
                            QVERIFY(doc->cut(vertical, cut.first, cut.second));
                        removed += cut.second - cut.first;
                    }
                    const QRectF area = vertical ? QRectF(37 - removed, 17, 46, 46)
                                                 : QRectF(37, 17 - removed, 46, 46);
                    for (ImageDocument *doc : {&clean, &covered}) {
                        if (erase) QVERIFY(doc->erase(area, {200, 100}));
                        else QVERIFY(doc->blur(area, 18));
                    }
                    if (erase) QCOMPARE(clean.image(), covered.image());
                    else {
                        QVERIFY(clean.image() != covered.image());
                        QCOMPARE(clean.image().pixel(100, 100), covered.image().pixel(100, 100));
                    }
                    QCOMPARE(clean.exportScale(), covered.exportScale());
                    for (const qreal scale : {1.25, 1.5, 2.5}) {
                        if (erase) QCOMPARE(clean.render(scale), covered.render(scale));
                        else {
                            QVERIFY(clean.render(scale) != covered.render(scale));
                            QCOMPARE(clean.render(scale).pixel(qRound(100 * scale), qRound(100 * scale)),
                                     covered.render(scale).pixel(qRound(100 * scale), qRound(100 * scale)));
                        }
                        const QSize frameSize(qRound(clean.image().width() * scale),
                                              qRound(clean.image().height() * scale));
                        const auto preview = [&](ImageDocument &doc) {
                            QImage frame(frameSize, QImage::Format_ARGB32_Premultiplied);
                            frame.fill(Qt::transparent);
                            QPainter painter(&frame);
                            painter.scale(scale, scale);
                            doc.paint(painter, scale);
                            painter.end();
                            return frame;
                        };
                        if (erase) QCOMPARE(preview(clean), preview(covered));
                        else {
                            QVERIFY(preview(clean) != preview(covered));
                            QCOMPARE(preview(clean).pixel(qRound(100 * scale), qRound(100 * scale)),
                                     preview(covered).pixel(qRound(100 * scale), qRound(100 * scale)));
                        }
                    }
                    if (erase) QCOMPARE(clean.render(clean.exportScale()),
                                        covered.render(covered.exportScale()));
                    else QVERIFY(clean.render(clean.exportScale()) != covered.render(covered.exportScale()));
                    const QImage exported = covered.render(1.5);
                    covered.undo(); covered.redo();
                    QCOMPARE(covered.render(1.5), exported);
                }
            }
        }
    }
}

void ImageToolTests::failedSavePreservesSession() {
    QTemporaryDir temporary;
    QImage white(80, 60, QImage::Format_ARGB32_Premultiplied);
    white.fill(Qt::white);
    const QString imagePath = temporary.filePath("source.png");
    QVERIFY(white.save(imagePath));
    EditorCanvas canvas;
    canvas.setWidth(272); canvas.setHeight(212);
    QVERIFY(canvas.load(QUrl::fromLocalFile(imagePath)));
    canvas.setTool("rect");
    const QPointF start = canvas.imageRect().topLeft() + QPointF(10, 10) * canvas.imageScale();
    const QPointF end = canvas.imageRect().topLeft() + QPointF(70, 50) * canvas.imageScale();
    canvas.begin(start.x(), start.y()); canvas.end(end.x(), end.y());
    QVERIFY(canvas.canUndo());
    QVERIFY(canvas.copy());
    const QImage before = QGuiApplication::clipboard()->image();
    QGuiApplication::clipboard()->setText("leave this clipboard alone");
    const QString blocked = temporary.filePath("not-a-folder");
    QFile file(blocked);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QSignalSpy error(&canvas, &EditorCanvas::error);
    QVERIFY(canvas.saveTo(blocked).isEmpty());
    QCOMPARE(error.size(), 1);
    QVERIFY(error.first().first().toString().contains("folder"));
    QVERIFY(canvas.hasImage());
    QVERIFY(canvas.canUndo());
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), before);
    QGuiApplication::clipboard()->setText("leave this clipboard alone");
    QVERIFY(!QFileInfo::exists(QDir(blocked).filePath(QDate::currentDate().toString("yyyy"))));
}

void ImageToolTests::savePngUsesUniqueNamesAndKeepsSource() {
    QTemporaryDir pictures;
    QVERIFY(pictures.isValid());
    const QImage original = sourceImage();
    QString error;
    const QDateTime saveTime(QDate(2026, 9, 18), QTime(14, 30), QTimeZone::systemTimeZone());
    const QString first = screenshots::savePng(original, pictures.path(), &error, saveTime);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    const QString second = screenshots::savePng(original, pictures.path(), &error, saveTime);
    QVERIFY2(!second.isEmpty(), qPrintable(error));
    QVERIFY(first != second);
    QVERIFY(first.endsWith(".png") && second.endsWith(".png"));
    QCOMPARE(QFileInfo(first).absolutePath(), pictures.filePath("2026/9"));
    QCOMPARE(QImage(first).convertToFormat(original.format()), original);
    QCOMPARE(QImage(second).convertToFormat(original.format()), original);
    QVERIFY(QFileInfo(first).size() > 0);

    const QString blocked = pictures.filePath("blocked");
    QFile obstruction(blocked);
    QVERIFY(obstruction.open(QIODevice::WriteOnly));
    obstruction.close();
    error.clear();
    QVERIFY(screenshots::savePng(original, blocked, &error).isEmpty());
    QVERIFY(error.contains("Could not create the screenshots folder"));
    QCOMPARE(QImage(first).convertToFormat(original.format()), original);
}

void ImageToolTests::saveUsesConfiguredMediaRoot() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir mediaDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(mediaDirectory.isValid());
    const QString source = sourceDirectory.filePath("source.png");
    QVERIFY(sourceImage().save(source));
    EditorCanvas canvas;
    canvas.setSaveRoot(mediaDirectory.filePath("custom captures"));
    QVERIFY(canvas.load(QUrl::fromLocalFile(source)));
    const QString saved = canvas.save();
    QVERIFY2(!saved.isEmpty(), "The canvas did not save to its configured media root");
    QCOMPARE(QFileInfo(saved).absolutePath(), QDir(canvas.saveRoot()).filePath(
        QStringLiteral("%1/%2").arg(QString::number(QDate::currentDate().year()).rightJustified(4, QLatin1Char('0')))
            .arg(QDate::currentDate().month())));
    QVERIFY(QFileInfo(saved).isAbsolute());
    QCOMPARE(QImage(saved).convertToFormat(sourceImage().format()), sourceImage());
}

void ImageToolTests::savePngUsesLocalYearAndMonthFolders() {
    QTemporaryDir pictures;
    QVERIFY(pictures.isValid());
    const QString legacyDirectory = pictures.path();
    QVERIFY(QDir().mkpath(legacyDirectory));
    const QString legacyPath = QDir(legacyDirectory).filePath("existing.png");
    const QByteArray legacyContents("leave existing files in place");
    QFile legacyFile(legacyPath);
    QVERIFY(legacyFile.open(QIODevice::WriteOnly));
    QCOMPARE(legacyFile.write(legacyContents), qint64(legacyContents.size()));
    legacyFile.close();

    const QImage original = sourceImage();
    const QList<QPair<QDateTime, QString>> cases{
        {QDateTime(QDate(2026, 9, 30), QTime(23, 59), QTimeZone::systemTimeZone()), "2026/9"},
        {QDateTime(QDate(2026, 10, 1), QTime(0, 1), QTimeZone::systemTimeZone()), "2026/10"},
        {QDateTime(QDate(2026, 12, 31), QTime(23, 59), QTimeZone::systemTimeZone()), "2026/12"},
        {QDateTime(QDate(2027, 1, 1), QTime(0, 1), QTimeZone::systemTimeZone()), "2027/1"}};
    QString error;
    for (const auto &entry : cases) {
        const QString saved = screenshots::savePng(original, pictures.path(), &error, entry.first);
        QVERIFY2(!saved.isEmpty(), qPrintable(error));
        QCOMPARE(QFileInfo(saved).absolutePath(), pictures.filePath(entry.second));
        QVERIFY(QFileInfo(saved).fileName().startsWith(
            QStringLiteral("xshot-%1-").arg(entry.first.toLocalTime().toString("yyyyMMdd-HHmmss-zzz"))));
        QCOMPARE(QImage(saved).convertToFormat(original.format()), original);
    }
    QVERIFY(QFileInfo::exists(legacyPath));
    QVERIFY(legacyFile.open(QIODevice::ReadOnly));
    QCOMPARE(legacyFile.readAll(), legacyContents);
}

QTEST_MAIN(ImageToolTests)
#include "imagetool_tests.moc"
