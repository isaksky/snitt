#include <QtTest>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QTemporaryDir>
#include "imagedocument.h"
#include "editorcanvas.h"

class ImageToolTests : public QObject {
    Q_OBJECT
private slots:
    void privacyMaskDiscardsSourcePixels();
    void selectionBoundsAndHistory();
    void eraseUsesExactDragStartPixel();
    void scaledGesturesAndClipboard();
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

QTEST_MAIN(ImageToolTests)
#include "imagetool_tests.moc"
