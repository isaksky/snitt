#include <QtTest>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
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
    void replayedAnnotationsAndExportPolicy();
    void replayedEditsKeepOperationOrder();
    void annotationPreviewDoesNotSoftenOnRelease();
    void failedSavePreservesSession();
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
    canvas.setInk(QColor("#ef4444"));
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
    QVERIFY(masked.pixelColor(45, 30).red() < 180);
    QVERIFY(masked.pixelColor(120, 30).red() > masked.pixelColor(120, 30).green());
    doc.undo();
    QCOMPARE(doc.render(3), afterCut);
    doc.redo();
    QCOMPARE(doc.render(3), masked);
    QVERIFY(doc.erase(QRectF(35, 8, 15, 8), {35, 8}));
    const QImage erased = doc.render(3);
    QVERIFY(erased.pixelColor(120, 30).red() <= erased.pixelColor(120, 30).green() + 20);
    QVERIFY(doc.annotate("highlight", {35, 8}, {50, 16}, Qt::green, 2));
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
    QCOMPARE(clean.render(3), changed.render(3)); // Hidden source pixels cannot bleed past the mask.

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
            if (tool != "highlight") canvas.adjustToolSize(-120); // Thin 3 px stroke.
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
    QVERIFY(!QFileInfo::exists(QDir(blocked).filePath("xshot")));
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
