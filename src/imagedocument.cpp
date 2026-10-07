#include "imagedocument.h"
#include "annotationfont.h"
#include <QPainter>
#include <QRegion>
#include <algorithm>
#include <cmath>

namespace {
QRect selectedPixels(QRectF area, const QSize &size) {
    if (!std::isfinite(area.x()) || !std::isfinite(area.y())
        || !std::isfinite(area.width()) || !std::isfinite(area.height())) return {};
    return area.normalized().intersected(QRectF(QPointF(0, 0), size)).toAlignedRect();
}

void fillWithSample(QImage &image, const QRect &area, QRgb sampledPixel) {
    for (int y = area.top(); y <= area.bottom(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        std::fill(row + area.left(), row + area.right() + 1, sampledPixel);
    }
}

QRgb averageBlock(const QImage &source, const QRect &block) {
    quint64 red = 0, green = 0, blue = 0, alpha = 0;
    for (int y = block.top(); y <= block.bottom(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = block.left(); x <= block.right(); ++x) {
            const QRgb pixel = row[x]; // Source images use premultiplied ARGB.
            red += qRed(pixel); green += qGreen(pixel); blue += qBlue(pixel);
            alpha += qAlpha(pixel);
        }
    }
    const quint64 count = quint64(block.width()) * block.height();
    if (!alpha) return 0;
    return qRgba(int(qMin<quint64>(255, (red * 255 + alpha / 2) / alpha)),
                 int(qMin<quint64>(255, (green * 255 + alpha / 2) / alpha)),
                 int(qMin<quint64>(255, (blue * 255 + alpha / 2) / alpha)),
                 int((alpha + count / 2) / count));
}

QImage samplePixelGrid(const QImage &source, const QRect &area, int blockSize) {
    const QRect selected = area.intersected(source.rect());
    if (selected.isEmpty()) return {};
    const int firstX = selected.left() / blockSize;
    const int firstY = selected.top() / blockSize;
    const int columns = selected.right() / blockSize - firstX + 1;
    const int rows = selected.bottom() / blockSize - firstY + 1;
    QImage grid(columns, rows, QImage::Format_ARGB32);
    if (grid.isNull()) return {};
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
            grid.setPixel(column, row, averageBlock(source,
                QRect((firstX + column) * blockSize, (firstY + row) * blockSize,
                      blockSize, blockSize).intersected(source.rect())));
    return grid;
}

void paintPixelGrid(QPainter &painter, const QRect &area, int blockSize, const QImage &grid) {
    if (grid.isNull()) return;
    const int firstX = area.left() / blockSize;
    const int firstY = area.top() / blockSize;
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int row = 0; row < grid.height(); ++row)
        for (int column = 0; column < grid.width(); ++column)
            painter.fillRect(QRect((firstX + column) * blockSize, (firstY + row) * blockSize,
                                   blockSize, blockSize).intersected(area),
                             QColor::fromRgba(grid.pixel(column, row)));
    painter.restore();
}
}

const QImage &ImageRenderer::image() const { return m_image; }

bool ImageRenderer::hasAnnotations() const {
    if (image().isNull()) return false;
    const auto &ops = m_operations;
    int lastAnnotation = -1;
    int lastDestructiveEdit = -1;
    for (int i = 0; i < ops.size(); ++i) {
        if (ops[i].kind == Operation::Annotation || ops[i].kind == Operation::Text) lastAnnotation = i;
        else lastDestructiveEdit = i;
    }
    if (lastAnnotation < 0) return false;
    if (lastAnnotation > lastDestructiveEdit) return true;
    // A later cut, mask, or erase may have removed every visible annotation.
    // Replay an alpha-only layer so that export size reflects current content.
    QImage mask(m_source.size(), QImage::Format_Alpha8);
    if (mask.isNull()) return true;
    mask.fill(Qt::transparent);
    for (const auto &op : ops) {
        if (op.kind == Operation::Cut) {
            QImage next(mask.width() - (op.vertical ? op.end - op.start : 0),
                        mask.height() - (op.vertical ? 0 : op.end - op.start), mask.format());
            if (next.isNull()) return true;
            next.fill(Qt::transparent);
            QPainter p(&next);
            if (op.vertical) {
                p.drawImage(QPoint(0, 0), mask, QRect(0, 0, op.start, mask.height()));
                p.drawImage(QPoint(op.start, 0), mask, QRect(op.end, 0, mask.width() - op.end, mask.height()));
            } else {
                p.drawImage(QPoint(0, 0), mask, QRect(0, 0, mask.width(), op.start));
                p.drawImage(QPoint(0, op.start), mask, QRect(0, op.end, mask.width(), mask.height() - op.end));
            }
            p.end();
            mask = std::move(next);
        } else if (op.kind == Operation::Blur || op.kind == Operation::Erase) {
            QPainter p(&mask);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillRect(selectedPixels(op.area, mask.size()), Qt::white);
        } else if (op.kind == Operation::Annotation) {
            QPainter p(&mask);
            drawAnnotation(p, op.tool, op.from, op.to, Qt::white, op.strokeWidth);
        } else {
            QPainter p(&mask);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setFont(annotationfont::make(op.fontSize));
            p.setPen(Qt::white);
            p.drawText(op.area, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, op.text);
        }
    }
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
            if (row[x]) return true;
    }
    return false;
}

qreal ImageRenderer::exportScale() const {
    if (!hasAnnotations()) return 1;
    const QSize size = image().size();
    const int shorter = qMin(size.width(), size.height());
    if (shorter >= 720) return 1;
    qreal scale = qMin(3.0, 720.0 / qMax(1, shorter));
    // Replay starts with the pre-cut source; budget that intermediate too.
    scale = qMin(scale, 16384.0 / qMax(m_source.width(), m_source.height()));
    scale = qMin(scale, std::sqrt(32.0 * 1024 * 1024 /
                                      qMax(1.0, double(m_source.width()) * m_source.height())));
    return qMax(1.0, scale);
}

void ImageRenderer::drawText(QPainter &p, const Operation &op) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(annotationfont::make(op.fontSize));
    p.setPen(op.color);
    p.drawText(op.area, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, op.text);
}

QImage ImageRenderer::render(qreal scale) const {
    return renderThrough(scale, m_operations.size());
}

void ImageRenderer::paint(QPainter &p, qreal scale) const {
    if (image().isNull()) return;
    const auto &ops = m_operations;
    int firstTrailing = ops.size();
    while (firstTrailing > 0 && (ops[firstTrailing - 1].kind == Operation::Annotation
                                 || ops[firstTrailing - 1].kind == Operation::Text))
        --firstTrailing;
    const QImage base = renderThrough(scale, firstTrailing);
    if (base.isNull()) {
        p.drawImage(QRectF(QPointF(0, 0), image().size()), image());
        return;
    }
    p.drawImage(QRectF(QPointF(0, 0), image().size()), base);
    for (int i = firstTrailing; i < ops.size(); ++i) {
        const auto &op = ops[i];
        p.save();
        if (op.kind == Operation::Annotation)
            drawAnnotation(p, op.tool, op.from, op.to, op.color, op.strokeWidth, op.goodMode);
        else
            drawText(p, op);
        p.restore();
    }
}

QImage ImageRenderer::renderThrough(qreal scale, int operationCount) const {
    if (image().isNull() || !std::isfinite(scale) || scale <= 0) return {};
    if (operationCount == m_operations.size() && qFuzzyCompare(scale, 1.0)) return image();
    const auto scaledSize = [&](QSize size) {
        return QSize(qMax(1, qRound(size.width() * scale)), qMax(1, qRound(size.height() * scale)));
    };
    const auto &ops = m_operations;
    int lastRasterEdit = -1;
    bool hasPrivacyEdit = false;
    QSize outputSize = m_source.size();
    for (int i = 0; i < operationCount; ++i) {
        if (ops[i].kind == Operation::Cut || ops[i].kind == Operation::Blur
            || ops[i].kind == Operation::Erase) lastRasterEdit = i;
        if (ops[i].kind == Operation::Blur || ops[i].kind == Operation::Erase) hasPrivacyEdit = true;
        if (ops[i].kind == Operation::Cut) {
            const int removed = ops[i].end - ops[i].start;
            if (ops[i].vertical) outputSize.rwidth() -= removed;
            else outputSize.rheight() -= removed;
        }
    }
    // A cut only relocates intact pixels, so keep the original smooth scaling.
    // Privacy edits retain nearest-neighbor scaling for hard block boundaries
    // and exact erase samples, including edits followed by cuts.
    const auto transformation = hasPrivacyEdit ? Qt::FastTransformation : Qt::SmoothTransformation;
    // Cuts replay at native resolution; only their remainder is enlarged. Using
    // the original capture here incorrectly rejects a small, zoomed-in preview
    // and makes paint() fall back to flattened, low-resolution annotations.
    if (scale > 1 && (outputSize.width() * scale > 16384 || outputSize.height() * scale > 16384
                      || double(outputSize.width()) * outputSize.height() * scale * scale > 32.0 * 1024 * 1024))
        return {};

    bool annotationBeforeRasterEdit = false;
    for (int i = 0; i < lastRasterEdit; ++i)
        if (ops[i].kind == Operation::Annotation || ops[i].kind == Operation::Text)
            annotationBeforeRasterEdit = true;

    if (annotationBeforeRasterEdit) {
        // Keep destructive edits at native resolution so fractional cuts cannot
        // expose source pixels at a rounded boundary. Retain each annotation's
        // visible region in document coordinates. Repeated cuts then change its
        // integral translation, not a rounded high-resolution bitmap offset.
        // This keeps later masks aligned at every fractional replay scale.
        ImageRenderer raster;
        raster.reset(m_source);
        struct Fragment {
            const Operation *annotation;
            QRegion visible;
            QPoint offset;
        };
        QVector<Fragment> fragments;
        for (int i = 0; i < operationCount; ++i) {
            const auto &op = ops[i];
            switch (op.kind) {
            case Operation::Annotation:
            case Operation::Text:
                fragments.append({&op, QRegion(QRect(QPoint(0, 0), raster.image().size())), {0, 0}});
                break;
            case Operation::Blur:
            case Operation::Erase: {
                const QRect pixels = selectedPixels(op.area, raster.image().size());
                if (op.kind == Operation::Blur) {
                    QImage frame = raster.image().copy();
                    if (frame.isNull()) return {};
                    QPainter p(&frame);
                    paintPixelGrid(p, pixels, op.blockSize, op.pixelGrid);
                    p.end();
                    raster.commit(std::move(frame), &op);
                }
                else {
                    QImage frame = raster.image().copy();
                    if (frame.isNull()) return {};
                    fillWithSample(frame, pixels, op.sampledPixel);
                    raster.commit(std::move(frame), &op);
                }
                // Remove only earlier annotation content. An annotation added
                // after this edit is appended with its full visible region.
                for (auto &fragment : fragments)
                    fragment.visible = fragment.visible.subtracted(
                        QRegion(pixels.translated(-fragment.offset)));
                break;
            }
            case Operation::Cut: {
                const QSize oldSize = raster.image().size();
                const int removed = op.end - op.start;
                const QRect before = op.vertical ? QRect(0, 0, op.start, oldSize.height())
                                                 : QRect(0, 0, oldSize.width(), op.start);
                const QRect after = op.vertical ? QRect(op.end, 0, oldSize.width() - op.end, oldSize.height())
                                                : QRect(0, op.end, oldSize.width(), oldSize.height() - op.end);
                QVector<Fragment> next;
                next.reserve(fragments.size() * 2);
                for (const auto &fragment : fragments) {
                    const QRegion current = fragment.visible.translated(fragment.offset);
                    const QRegion prefix = current.intersected(before);
                    if (!prefix.isEmpty())
                        next.append({fragment.annotation, prefix.translated(-fragment.offset), fragment.offset});
                    const QRegion suffix = current.intersected(after);
                    if (!suffix.isEmpty()) {
                        QPoint shifted = fragment.offset;
                        if (op.vertical) shifted.rx() -= removed;
                        else shifted.ry() -= removed;
                        next.append({fragment.annotation, suffix.translated(-fragment.offset), shifted});
                    }
                }
                fragments = std::move(next);
                if (!raster.cut(op.vertical, op.start, op.end)) return {};
                break;
            }
            }
        }
        QImage result = raster.image().scaled(scaledSize(raster.image().size()),
                                              Qt::IgnoreAspectRatio, transformation);
        if (result.isNull()) return {};
        QPainter p(&result);
        p.scale(scale, scale);
        for (const auto &fragment : fragments) {
            p.save();
            p.translate(fragment.offset);
            p.setClipRegion(fragment.visible);
            const auto &op = *fragment.annotation;
            if (op.kind == Operation::Annotation)
                drawAnnotation(p, op.tool, op.from, op.to, op.color, op.strokeWidth, op.goodMode);
            else drawText(p, op);
            p.restore();
        }
        return result;
    }

    // Cuts have integer boundaries in the document, but fractional replay scales
    // do not. Cropping an already scaled image rounds away a different number of
    // pixels than the document cut, shifting every later privacy edit. Start
    // from the exact native frame after the last raster edit instead: no removed
    // or masked source pixel can survive a later resampling boundary.
    QImage base = m_source;
    if (lastRasterEdit >= 0) {
        const int prefixCount = lastRasterEdit + 1;
        {
            // Explicit batches replay without consulting undo history.
            ImageRenderer replay;
            replay.reset(m_source);
            for (int i = 0; i < prefixCount; ++i) {
                const auto &op = ops[i];
                switch (op.kind) {
                case Operation::Cut:
                    if (!replay.cut(op.vertical, op.start, op.end)) return {};
                    break;
                case Operation::Blur: {
                    QImage frame = replay.image().copy();
                    if (frame.isNull()) return {};
                    QPainter p(&frame);
                    paintPixelGrid(p, selectedPixels(op.area, frame.size()), op.blockSize, op.pixelGrid);
                    p.end();
                    replay.commit(std::move(frame), &op);
                    break;
                }
                case Operation::Erase: {
                    QImage frame = replay.image().copy();
                    if (frame.isNull()) return {};
                    fillWithSample(frame, selectedPixels(op.area, frame.size()), op.sampledPixel);
                    replay.commit(std::move(frame), &op);
                    break;
                }
                case Operation::Annotation:
                    replay.annotate(op.tool, op.from, op.to, op.color, op.strokeWidth, op.goodMode); break;
                case Operation::Text: replay.text(op.area, op.text, op.color, op.fontSize); break;
                }
            }
            base = replay.image();
        }
    }
    QImage result = base.scaled(scaledSize(base.size()), Qt::IgnoreAspectRatio, transformation);
    if (result.isNull()) return {};
    for (int i = lastRasterEdit + 1; i < operationCount; ++i) {
        const auto &op = ops[i];
        QPainter p(&result);
        p.scale(scale, scale);
        if (op.kind == Operation::Annotation)
            drawAnnotation(p, op.tool, op.from, op.to, op.color, op.strokeWidth, op.goodMode);
        else if (op.kind == Operation::Text) drawText(p, op);
    }
    return result;
}

void ImageRenderer::reset(QImage image) {
    image.setDevicePixelRatio(1);
    m_source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_image = m_source;
    m_operations.clear();
}

void ImageRenderer::commit(QImage image, const Operation *operation) {
    if (operation) m_operations.append(*operation);
    m_image = std::move(image);
}

void ImageDocument::reset(QImage image) {
    ImageRenderer::reset(std::move(image));
    m_history.clear(); m_index = -1; m_undo = m_redo = false;
    ++m_resetSerial;
    if (!m_external && !this->image().isNull()) {
        m_history.append(*this); m_index = 0;
    }
}

void ImageDocument::external() {
    m_external = m_readOnly = true;
    m_history.clear(); m_index = -1;
}

void ImageDocument::projectExternal(const ImageRenderer &projection, bool undo, bool redo) {
    if (!m_external) return;
    project(projection.source(), projection.image(), projection.operations());
    m_undo = undo; m_redo = redo;
}

void ImageDocument::commit(QImage image, const Operation *operation) {
    ImageRenderer::commit(std::move(image), operation);
    m_history.resize(m_index + 1);
    m_history.append(*this);
    ++m_index;
    qint64 bytes = 0;
    for (const auto &frame : m_history) bytes += frame.image().sizeInBytes();
    while (m_history.size() > 1 && (bytes > 256LL * 1024 * 1024 || m_history.size() > 50)) {
        bytes -= m_history.front().image().sizeInBytes();
        m_history.removeFirst(); --m_index;
    }
}

void ImageDocument::undo() {
    if (!m_external && canUndo()) {
        const auto &frame = m_history[--m_index];
        project(frame.source(), frame.image(), frame.operations());
    }
}
void ImageDocument::redo() {
    if (!m_external && canRedo()) {
        const auto &frame = m_history[++m_index];
        project(frame.source(), frame.image(), frame.operations());
    }
}

bool ImageRenderer::cut(bool vertical, int start, int end) {
    if (m_readOnly || image().isNull()) return false;
    const int length = vertical ? image().width() : image().height();
    if (start > end) std::swap(start, end);
    start = qBound(0, start, length);
    end = qBound(0, end, length);
    const int removed = end - start;
    if (removed <= 0 || removed >= length) return false;
    QImage result(image().width() - (vertical ? removed : 0),
                  image().height() - (vertical ? 0 : removed), image().format());
    if (result.isNull()) return false;
    result.fill(Qt::transparent);
    QPainter p(&result);
    if (vertical) {
        p.drawImage(QRect(0, 0, start, result.height()), image(), QRect(0, 0, start, image().height()));
        p.drawImage(QRect(start, 0, length - end, result.height()), image(), QRect(end, 0, length - end, image().height()));
    } else {
        p.drawImage(QRect(0, 0, result.width(), start), image(), QRect(0, 0, image().width(), start));
        p.drawImage(QRect(0, start, result.width(), length - end), image(), QRect(0, end, image().width(), length - end));
    }
    p.end();
    Operation op; op.kind = Operation::Cut; op.vertical = vertical; op.start = start; op.end = end;
    commit(std::move(result), &op);
    return true;
}

void ImageRenderer::drawPixelation(QPainter &painter, const QImage &source,
                                  const QRect &area, int blockSize) {
    if (source.isNull()) return;
    const QRect selected = area.intersected(source.rect());
    if (selected.isEmpty()) return;
    blockSize = qBound(4, blockSize, 64);
    // Anchor cells to the document, not the drag corner. Edge cells sample the
    // full available source block, but only paint inside the selection.
    paintPixelGrid(painter, selected, blockSize, samplePixelGrid(source, selected, blockSize));
}

bool ImageRenderer::blur(QRectF area, int blockSize) {
    if (m_readOnly || image().isNull()) return false;
    const QRect pixels = selectedPixels(area, image().size());
    if (pixels.isEmpty()) return false;
    blockSize = qBound(4, blockSize, 64);
    const QImage grid = samplePixelGrid(image(), pixels, blockSize);
    if (grid.isNull()) return false;
    QImage result = image().copy();
    if (result.isNull()) return false;
    QPainter painter(&result);
    paintPixelGrid(painter, pixels, blockSize, grid);
    painter.end();
    Operation op; op.kind = Operation::Blur; op.area = pixels; op.blockSize = blockSize;
    op.pixelGrid = grid;
    commit(std::move(result), &op);
    return true;
}

bool ImageRenderer::erase(QRectF area, QPointF samplePosition) {
    if (m_readOnly || image().isNull() || !std::isfinite(samplePosition.x())
        || !std::isfinite(samplePosition.y())) return false;
    const QRect pixels = selectedPixels(area, image().size());
    if (pixels.isEmpty()) return false;
    const int sampleX = int(qBound(0.0, std::floor(samplePosition.x()), double(image().width() - 1)));
    const int sampleY = int(qBound(0.0, std::floor(samplePosition.y()), double(image().height() - 1)));
    const QRgb sampledPixel = image().pixel(sampleX, sampleY);
    QImage result = image().copy();
    if (result.isNull()) return false;
    // Copy the stored pixel exactly, including its alpha and premultiplication.
    fillWithSample(result, pixels, sampledPixel);
    Operation op; op.kind = Operation::Erase; op.area = pixels;
    op.sampledPixel = sampledPixel;
    commit(std::move(result), &op);
    return true;
}

void ImageRenderer::drawAnnotation(QPainter &p, const QString &tool,
                                   QPointF start, QPointF end, QColor color, qreal strokeWidth, bool goodMode) {
    p.setRenderHint(QPainter::Antialiasing);
    if (tool == "highlight") {
        // Source-over composition makes overlapping highlights accumulate.
        QColor fill = goodMode ? QColor("#ffff00") : QColor("#ff6b6b");
        fill.setAlphaF(0.3);
        p.fillRect(QRectF(start, end).normalized(), fill);
        return;
    }
    p.setPen(QPen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (tool == "rect") {
        p.drawRect(QRectF(start, end).normalized());
    } else if (tool == "arrow") {
        const QLineF line(start, end);
        if (line.length() < 1) return;
        const QPointF unit = (end - start) / line.length();
        const QPointF side(-unit.y(), unit.x());
        const qreal head = qMin(qMax(18.0, strokeWidth * 4), line.length() * 0.65);
        const QPointF base = end - unit * head;
        p.drawLine(start, base);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        const qreal halfWidth = qMax(strokeWidth * 0.9, head * 0.48);
        p.drawPolygon(QPolygonF{end, base + side * halfWidth, base - side * halfWidth});
    }
}

bool ImageRenderer::annotate(const QString &tool, QPointF start, QPointF end, QColor color, qreal strokeWidth,
                             bool goodMode) {
    if (!std::isfinite(start.x()) || !std::isfinite(start.y())
        || !std::isfinite(end.x()) || !std::isfinite(end.y())
        || !std::isfinite(strokeWidth) || strokeWidth <= 0 || !color.isValid()) return false;
    if (m_readOnly || image().isNull() || (tool != "rect" && tool != "arrow" && tool != "highlight")
        || QLineF(start, end).length() < 3)
        return false;
    if ((tool == "rect" || tool == "highlight")
        && (qAbs(start.x() - end.x()) < 2 || qAbs(start.y() - end.y()) < 2))
        return false;
    QImage result = image().copy();
    if (result.isNull()) return false;
    QPainter p(&result);
    drawAnnotation(p, tool, start, end, color, strokeWidth, goodMode);
    p.end();
    Operation op; op.kind = Operation::Annotation; op.tool = tool; op.from = start; op.to = end;
    op.color = color; op.goodMode = goodMode; op.strokeWidth = strokeWidth;
    commit(std::move(result), &op);
    return true;
}

bool ImageRenderer::text(QRectF box, const QString &text, QColor color, int fontSize) {
    if (!std::isfinite(box.x()) || !std::isfinite(box.y())
        || !std::isfinite(box.width()) || !std::isfinite(box.height())
        || !color.isValid() || fontSize <= 0) return false;
    if (m_readOnly || image().isNull() || text.trimmed().isEmpty() || box.width() < 1 || box.height() < 1)
        return false;
    QImage result = image().copy();
    if (result.isNull()) return false;
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(annotationfont::make(fontSize));
    p.setPen(color);
    p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    p.end();
    Operation op; op.kind = Operation::Text; op.area = box; op.text = text; op.color = color; op.fontSize = fontSize;
    commit(std::move(result), &op);
    return true;
}
