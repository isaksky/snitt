#pragma once
#include <QQuickPaintedItem>
#include <QUrl>
#include "imagedocument.h"

class EditorCanvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY imageChanged)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY imageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY imageChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY imageChanged)
    Q_PROPERTY(QRectF imageRect READ imageRect NOTIFY imageRectChanged)
    Q_PROPERTY(qreal imageScale READ imageScale NOTIFY imageRectChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor ink READ ink WRITE setInk NOTIFY inkChanged)
public:
    explicit EditorCanvas(QQuickItem *parent = nullptr);
    bool hasImage() const { return !m_document.image().isNull(); }
    int imageWidth() const { return m_document.image().width(); }
    int imageHeight() const { return m_document.image().height(); }
    bool canUndo() const { return m_document.canUndo(); }
    bool canRedo() const { return m_document.canRedo(); }
    QRectF imageRect() const;
    qreal imageScale() const;
    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    QColor ink() const { return m_ink; }
    void setInk(QColor ink);
    void paint(QPainter *painter) override;
    Q_INVOKABLE bool load(const QUrl &url);
    Q_INVOKABLE bool paste();
    Q_INVOKABLE bool copy();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void begin(qreal x, qreal y);
    Q_INVOKABLE void move(qreal x, qreal y);
    Q_INVOKABLE void end(qreal x, qreal y);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void addText(qreal x, qreal y, qreal width, qreal height, const QString &text);
signals:
    void imageChanged();
    void imageRectChanged();
    void toolChanged();
    void inkChanged();
    void error(const QString &message);
    void textRequested(qreal x, qreal y);
protected:
    void geometryChange(const QRectF &next, const QRectF &previous) override;
private:
    QPointF imagePoint(qreal x, qreal y) const;
    void changed();
    ImageDocument m_document;
    QString m_tool = QStringLiteral("rect");
    QColor m_ink = QColor("#ef4444");
    bool m_dragging = false;
    QPointF m_start, m_end;
};
