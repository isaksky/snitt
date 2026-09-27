#pragma once
#include <QQuickPaintedItem>
#include <QUrl>
#include <QVariantList>
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
    Q_PROPERTY(QString colorMode READ colorMode NOTIFY inkChanged)
    Q_PROPERTY(QColor goodColor READ goodColor WRITE setGoodColor NOTIFY inkChanged)
    Q_PROPERTY(QColor badColor READ badColor WRITE setBadColor NOTIFY inkChanged)
    Q_PROPERTY(QString saveRoot READ saveRoot WRITE setSaveRoot)
    Q_PROPERTY(int strokeWidth READ strokeWidth NOTIFY sizeChanged)
    Q_PROPERTY(int textSize READ textSize NOTIFY sizeChanged)
    Q_PROPERTY(QString annotationFontFamily READ annotationFontFamily CONSTANT)
    Q_PROPERTY(int pixelBlockSize READ pixelBlockSize NOTIFY sizeChanged)
    Q_PROPERTY(int maxPixelBlockSize READ maxPixelBlockSize NOTIFY sizeChanged)
    Q_PROPERTY(int maxStrokeWidth READ maxStrokeWidth NOTIFY sizeChanged)
    Q_PROPERTY(int maxTextSize READ maxTextSize NOTIFY sizeChanged)
    Q_PROPERTY(bool arranging READ arranging NOTIFY arrangementChanged)
    Q_PROPERTY(int regionCount READ regionCount NOTIFY arrangementChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY arrangementChanged)
    Q_PROPERTY(int selectedRegion READ selectedRegion NOTIFY arrangementChanged)
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
    QString colorMode() const { return m_colorMode; }
    QColor goodColor() const { return m_goodColor; }
    QColor badColor() const { return m_badColor; }
    void setGoodColor(const QColor &color);
    void setBadColor(const QColor &color);
    QString saveRoot() const { return m_saveRoot; }
    void setSaveRoot(const QString &root) { m_saveRoot = root; }
    Q_INVOKABLE void setInkMode(const QString &mode);
    int strokeWidth() const { return m_strokeWidth; }
    int textSize() const { return m_textSize; }
    QString annotationFontFamily() const;
    int pixelBlockSize() const { return m_pixelBlockSize; }
    int maxPixelBlockSize() const;
    int maxStrokeWidth() const;
    int maxTextSize() const;
    void paint(QPainter *painter) override;
    bool arranging() const { return m_arranging; }
    int regionCount() const { return m_regions.size(); }
    int columns() const { return m_columns; }
    int selectedRegion() const { return m_selectedRegion; }
    void setColumns(int columns);
    Q_INVOKABLE bool loadRegions(const QVariantList &images);
    Q_INVOKABLE void arrange();
    Q_INVOKABLE void annotate();
    Q_INVOKABLE void moveRegion(int from, int to);
    Q_INVOKABLE void removeRegion(int index);
    Q_INVOKABLE bool load(const QUrl &url);
    Q_INVOKABLE bool paste();
    Q_INVOKABLE bool copy();
    Q_INVOKABLE QString save();
    QString saveTo(const QString &picturesDirectory);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void begin(qreal x, qreal y);
    Q_INVOKABLE void move(qreal x, qreal y);
    Q_INVOKABLE void end(qreal x, qreal y);
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE void adjustToolSize(qreal wheelDelta);
    Q_INVOKABLE void addText(qreal x, qreal y, qreal width, qreal height, const QString &text, int fontSize = 24);
signals:
    void imageChanged();
    void imageRectChanged();
    void toolChanged();
    void inkChanged();
    void sizeChanged();
    void error(const QString &message);
    void textRequested(qreal x, qreal y);
    void arrangementChanged();
protected:
    void geometryChange(const QRectF &next, const QRectF &previous) override;
private:
    QImage exportImage() const;
    QPointF imagePoint(qreal x, qreal y) const;
    void changed();
    bool composeRegions();
    void clearRegions();
    int regionAt(const QPointF &point) const;
    ImageDocument m_document;
    QString m_tool = QStringLiteral("rect");
    QColor m_ink = QColor("#ef4444");
    QColor m_goodColor = QColor("#22c55e");
    QColor m_badColor = QColor("#ef4444");
    QString m_colorMode = QStringLiteral("bad");
    QString m_saveRoot;
    int m_strokeWidth = 4;
    int m_textSize = 24;
    int m_pixelBlockSize = 12;
    qreal m_wheelRemainder = 0;
    bool m_dragging = false;
    QPointF m_start, m_end;
    QList<QImage> m_regions;
    QList<QRect> m_regionRects;
    int m_columns = 2;
    int m_selectedRegion = -1;
    int m_dropRegion = -1;
    bool m_arranging = false;
};
