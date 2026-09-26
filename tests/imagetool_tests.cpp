#include <QtTest>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include "imagedocument.h"
#include "editorcanvas.h"
#include "screenshotsave.h"

class ImageToolTests : public QObject {
    Q_OBJECT
private slots:
    void privacyMaskDiscardsSourcePixels();
    void selectionBoundsAndHistory();
    void eraseUsesExactDragStartPixel();
    void scaledGesturesAndClipboard();
    void highlightsPreviewAndHistory();
    void annotationSizingAndPreview();
    void savePngUsesUniqueNamesAndKeepsSource();
};

static QImage sourceImage() {
    QImage image(80, 60, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x * 3, y * 4, (x + y) % 256, 80 + (x + y) % 176));
    return image;
}

void ImageToolTests::privacyMaskDiscardsSourcePixels() {
    const QRect area(12, 8, 49, 37);
    const QImage original = sourceImage();
    QImage changedSecret = original;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            changedSecret.setPixelColor(x, y, QColor(255 - x, 255 - y, 0, (x + y) % 2 ? 255 : 0));
    ImageDocument first, second;
    first.reset(original); second.reset(changedSecret);
    QVERIFY(first.blur(area)); QVERIFY(second.blur(area));
    QCOMPARE(first.image(), second.image());
    bool varied = false;
    const QRgb firstMaskPixel = first.image().pixel(area.topLeft());
    for (int y = 0; y < original.height(); ++y) {
        for (int x = 0; x < original.width(); ++x) {
            if (area.contains(x, y)) {
                QCOMPARE(qAlpha(first.image().pixel(x, y)), 255);
                varied |= first.image().pixel(x, y) != firstMaskPixel;
            } else {
                QCOMPARE(first.image().pixel(x, y), original.pixel(x, y));
            }
        }
    }
    QVERIFY(varied);
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(first.image().save(&buffer, "PNG"));
    const QImage exported = QImage::fromData(png, "PNG").convertToFormat(first.image().format());
    QCOMPARE(exported, first.image());
    first.undo(); QCOMPARE(first.image(), original);
    first.redo(); QCOMPARE(first.image(), second.image());
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
            else QCOMPARE(qAlpha(doc.image().pixel(x, y)), 255);
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
    canvas.setInk(QColor("#22c55e"));
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
    QVERIFY(one.pixelColor(30, 25).blue() >= 177 && one.pixelColor(30, 25).blue() <= 179);
    QCOMPARE(one.pixelColor(9, 25), QColor(Qt::white));
    QCOMPARE(one.pixelColor(61, 25), QColor(Qt::white));
    QCOMPARE(preview.pixelColor(point({30, 25}).toPoint()), one.pixelColor(30, 25));

    canvas.begin(point({30, 20}).x(), point({30, 20}).y());
    canvas.end(point({70, 50}).x(), point({70, 50}).y());
    QVERIFY(canvas.copy());
    const QImage overlap = QGuiApplication::clipboard()->image();
    QVERIFY(overlap.pixelColor(40, 30).blue() < one.pixelColor(40, 30).blue());
    canvas.setInk(QColor("#ef4444"));
    canvas.begin(point({5, 5}).x(), point({5, 5}).y());
    canvas.end(point({15, 15}).x(), point({15, 15}).y());
    QVERIFY(canvas.copy());
    const QImage bothModes = QGuiApplication::clipboard()->image();
    QVERIFY(bothModes.pixelColor(7, 7).green() < bothModes.pixelColor(7, 7).red());
    QCOMPARE(bothModes.pixelColor(20, 20), overlap.pixelColor(20, 20));
    canvas.undo(); canvas.undo();
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), one);
    canvas.redo(); canvas.redo();
    QVERIFY(canvas.copy());
    QCOMPARE(QGuiApplication::clipboard()->image(), bothModes);
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
    canvas.adjustToolSize(120 * 19);
    QCOMPARE(canvas.strokeWidth(), 20);
    const auto point = [&canvas](QPointF source) {
        return canvas.imageRect().topLeft() + source * canvas.imageScale();
    };
    const QPointF start = point({100, 100}), end = point({300, 300});
    canvas.begin(start.x(), start.y()); canvas.move(end.x(), end.y());
    canvas.adjustToolSize(120);
    QCOMPARE(canvas.strokeWidth(), 21);
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
    canvas.adjustToolSize(120 * 64);
    QCOMPARE(canvas.textSize(), 72);
    canvas.addText(500, 200, 500, 300, "Whole draft", canvas.textSize());
    QVERIFY(canvas.copy());
    const QImage largeText = QGuiApplication::clipboard()->image();
    canvas.adjustToolSize(-120 * 64);
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

void ImageToolTests::savePngUsesUniqueNamesAndKeepsSource() {
    QTemporaryDir pictures;
    QVERIFY(pictures.isValid());
    const QImage original = sourceImage();
    QString error;
    const QString first = screenshots::savePng(original, pictures.path(), &error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    const QString second = screenshots::savePng(original, pictures.path(), &error);
    QVERIFY2(!second.isEmpty(), qPrintable(error));
    QVERIFY(first != second);
    QVERIFY(first.endsWith(".png") && second.endsWith(".png"));
    QCOMPARE(QFileInfo(first).absolutePath(), pictures.filePath("xshot"));
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

QTEST_MAIN(ImageToolTests)
#include "imagetool_tests.moc"
