#include "imagedocument.h"
#include <QFont>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
QRect selectedPixels(QRectF area, const QSize &size) {
    if (!std::isfinite(area.x()) || !std::isfinite(area.y())
        || !std::isfinite(area.width()) || !std::isfinite(area.height())) return {};
    return area.normalized().intersected(QRectF(QPointF(0, 0), size)).toAlignedRect();
}

const QImage &privacyTexture() {
    // This opaque pattern never uses screenshot pixels. Blurring the source
    // would retain information about the contents the user meant to hide.
    static const QImage texture = [] {
        constexpr int side = 96;
        constexpr int block = 12;
        constexpr int radius = 2;
        QImage pixels(side, side, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < side; ++y) {
            auto *row = reinterpret_cast<QRgb *>(pixels.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const quint32 seed = quint32(x / block) * 0x9e3779b9U
                                   ^ quint32(y / block) * 0x85ebca6bU;
                const int shade = int((seed ^ (seed >> 13)) % 25);
                row[x] = qRgb(125 + shade, 133 + shade, 144 + shade);
            }
        }
        QImage horizontal(side, side, pixels.format());
        QImage softened(side, side, pixels.format());
        for (int pass = 0; pass < 2; ++pass) {
            const QImage &input = pass == 0 ? pixels : horizontal;
            QImage &output = pass == 0 ? horizontal : softened;
            for (int y = 0; y < side; ++y) {
                auto *row = reinterpret_cast<QRgb *>(output.scanLine(y));
                for (int x = 0; x < side; ++x) {
                    int red = 0, green = 0, blue = 0;
                    for (int offset = -radius; offset <= radius; ++offset) {
                        const QRgb value = input.pixel(
                            pass == 0 ? (x + offset + side) % side : x,
                            pass == 1 ? (y + offset + side) % side : y);
                        red += qRed(value); green += qGreen(value); blue += qBlue(value);
                    }
                    row[x] = qRgb(red / 5, green / 5, blue / 5);
                }
            }
        }
        return softened;
    }();
    return texture;
}
}

const QImage &ImageDocument::image() const {
    static const QImage empty;
    return m_index < 0 ? empty : m_history[m_index];
}

void ImageDocument::reset(QImage image) {
    m_history.clear();
    m_index = -1;
    if (!image.isNull()) {
        image.setDevicePixelRatio(1);
        commit(image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
}

void ImageDocument::commit(QImage image) {
    m_history.resize(m_index + 1);
    m_history.append(std::move(image));
    ++m_index;
    qint64 bytes = 0;
    for (const auto &frame : m_history) bytes += frame.sizeInBytes();
    while (m_history.size() > 1 && (bytes > 256LL * 1024 * 1024 || m_history.size() > 50)) {
        bytes -= m_history.front().sizeInBytes();
        m_history.removeFirst();
        --m_index;
    }
}

void ImageDocument::undo() { if (canUndo()) --m_index; }
void ImageDocument::redo() { if (canRedo()) ++m_index; }

bool ImageDocument::cut(bool vertical, int start, int end) {
    if (image().isNull()) return false;
    const int length = vertical ? image().width() : image().height();
    if (start > end) std::swap(start, end);
    start = qBound(0, start, length);
    end = qBound(0, end, length);
    const int removed = end - start;
    if (removed <= 0 || removed >= length) return false;
    QImage result(image().width() - (vertical ? removed : 0),
                  image().height() - (vertical ? 0 : removed), image().format());
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
    commit(std::move(result));
    return true;
}

void ImageDocument::drawPrivacyMask(QPainter &painter, const QRect &area) {
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(area, QBrush(privacyTexture()));
    painter.restore();
}

bool ImageDocument::blur(QRectF area) {
    if (image().isNull()) return false;
    const QRect pixels = selectedPixels(area, image().size());
    if (pixels.isEmpty()) return false;
    QImage result = image().copy();
    QPainter painter(&result);
    drawPrivacyMask(painter, pixels);
    painter.end();
    commit(std::move(result));
    return true;
}

bool ImageDocument::erase(QRectF area, QPointF samplePosition) {
    if (image().isNull() || !std::isfinite(samplePosition.x())
        || !std::isfinite(samplePosition.y())) return false;
    const QRect pixels = selectedPixels(area, image().size());
    if (pixels.isEmpty()) return false;
    const int sampleX = int(qBound(0.0, std::floor(samplePosition.x()), double(image().width() - 1)));
    const int sampleY = int(qBound(0.0, std::floor(samplePosition.y()), double(image().height() - 1)));
    const QRgb sampledPixel = image().pixel(sampleX, sampleY);
    QImage result = image().copy();
    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        // Copy the stored pixel exactly, including its alpha and premultiplication.
        std::fill(row + pixels.left(), row + pixels.right() + 1, sampledPixel);
    }
    commit(std::move(result));
    return true;
}

void ImageDocument::drawAnnotation(QPainter &p, const QString &tool,
                                   QPointF start, QPointF end, QColor color) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (tool == "rect") {
        p.drawRect(QRectF(start, end).normalized());
    } else if (tool == "arrow") {
        const QLineF line(start, end);
        if (line.length() < 1) return;
        const QPointF unit = (end - start) / line.length();
        const QPointF side(-unit.y(), unit.x());
        const qreal head = qMin(18.0, line.length() * 0.45);
        const QPointF base = end - unit * head;
        p.drawLine(start, base);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF{end, base + side * head * 0.48, base - side * head * 0.48});
    }
}

bool ImageDocument::annotate(const QString &tool, QPointF start, QPointF end, QColor color) {
    if (image().isNull() || (tool != "rect" && tool != "arrow") || QLineF(start, end).length() < 3)
        return false;
    if (tool == "rect" && (qAbs(start.x() - end.x()) < 2 || qAbs(start.y() - end.y()) < 2))
        return false;
    QImage result = image().copy();
    QPainter p(&result);
    drawAnnotation(p, tool, start, end, color);
    p.end();
    commit(std::move(result));
    return true;
}

bool ImageDocument::text(QRectF box, const QString &text, QColor color) {
    if (image().isNull() || text.trimmed().isEmpty() || box.width() < 1 || box.height() < 1)
        return false;
    QImage result = image().copy();
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
#ifdef Q_OS_WIN
    QFont font(QStringLiteral("Segoe UI"));
#else
    QFont font(QStringLiteral("Helvetica"));
#endif
    font.setPixelSize(24);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(color);
    p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    p.end();
    commit(std::move(result));
    return true;
}
