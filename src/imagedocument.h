#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

// All edits use source-image pixels; the preview and clipboard share a renderer.
class ImageDocument {
public:
    const QImage &image() const;
    QImage render(qreal scale = 1) const;
    bool hasAnnotations() const;
    qreal exportScale() const;
    void reset(QImage image);
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_history.size(); }
    void undo();
    void redo();
    bool cut(bool vertical, int start, int end);
    bool blur(QRectF area);
    bool erase(QRectF area, QPointF samplePosition);
    bool annotate(const QString &tool, QPointF start, QPointF end, QColor color, qreal strokeWidth = 4);
    bool text(QRectF box, const QString &text, QColor color, int fontSize = 24);
    static void drawAnnotation(QPainter &painter, const QString &tool,
                               QPointF start, QPointF end, QColor color, qreal strokeWidth = 4);
    static void drawPrivacyMask(QPainter &painter, const QRect &area);
private:
    struct Operation {
        enum Kind { Cut, Blur, Erase, Annotation, Text } kind;
        bool vertical = false;
        int start = 0, end = 0;
        QRectF area;
        QPointF samplePosition;
        QString tool, text;
        QPointF from, to;
        QColor color;
        qreal strokeWidth = 0;
        int fontSize = 0;
    };
    void commit(QImage image, const Operation *operation = nullptr);
    QImage m_source;
    QVector<QImage> m_history;
    QVector<QVector<Operation>> m_operationHistory;
    int m_index = -1;
};
