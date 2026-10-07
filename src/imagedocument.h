#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

// All edits use source-image pixels; the preview and clipboard share a renderer.
class ImageRenderer {
public:
    virtual ~ImageRenderer() = default;
    const QImage &image() const;
    QImage render(qreal scale = 1) const;
    void paint(QPainter &painter, qreal scale) const;
    bool hasAnnotations() const;
    qreal exportScale() const;
    void reset(QImage image);
    bool cut(bool vertical, int start, int end);
    bool blur(QRectF area, int blockSize = 12);
    bool erase(QRectF area, QPointF samplePosition);
    bool annotate(const QString &tool, QPointF start, QPointF end, QColor color, qreal strokeWidth = 4,
                  bool goodMode = false);
    bool text(QRectF box, const QString &text, QColor color, int fontSize = 24);
    static void drawAnnotation(QPainter &painter, const QString &tool,
                               QPointF start, QPointF end, QColor color, qreal strokeWidth = 4,
                               bool goodMode = false);
    static void drawPixelation(QPainter &painter, const QImage &source,
                              const QRect &area, int blockSize = 12);
    // Prepared commands contain immutable native pixel samples, never history.
    struct Operation {
        enum Kind { Cut, Blur, Erase, Annotation, Text } kind;
        bool vertical = false;
        int start = 0, end = 0;
        QRectF area;
        int blockSize = 12;
        QImage pixelGrid;
        QRgb sampledPixel = 0;
        QString tool, text;
        QPointF from, to;
        QColor color;
        bool goodMode = false;
        qreal strokeWidth = 0;
        int fontSize = 0;
    };
    const QImage &source() const { return m_source; }
    const QVector<Operation> &operations() const { return m_operations; }
    void project(QImage source, QImage frame, QVector<Operation> operations) {
        m_source = std::move(source); m_image = std::move(frame);
        m_operations = std::move(operations);
    }
protected:
    virtual void commit(QImage image, const Operation *operation = nullptr);
    bool m_readOnly = false;
private:
    QImage renderThrough(qreal scale, int operationCount) const;
    static void drawText(QPainter &painter, const Operation &operation);
    QImage m_source, m_image;
    QVector<Operation> m_operations;
};

// The retained Qt reference application owns its own history. Jolt canvases
// disable this wrapper and submit render projections from their document model.
class ImageDocument : public ImageRenderer {
public:
    void reset(QImage image);
    bool canUndo() const { return m_external ? m_undo : m_index > 0; }
    bool canRedo() const { return m_external ? m_redo : m_index + 1 < m_history.size(); }
    void undo();
    void redo();
    void external();
    void projectExternal(const ImageRenderer &projection, bool undo, bool redo);
    quint64 resetSerial() const { return m_resetSerial; }
protected:
    void commit(QImage image, const Operation *operation = nullptr) override;
private:
    QVector<ImageRenderer> m_history;
    int m_index = -1;
    quint64 m_resetSerial = 0;
    bool m_external = false, m_undo = false, m_redo = false;
};
