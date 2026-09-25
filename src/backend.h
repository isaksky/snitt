#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QPointer>
#include <QList>
#include <QImage>
#include <QRect>
#include <QVariantList>
class RegionSelector;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;
    bool capturing() const { return m_capturing; }
    Q_INVOKABLE void capture(bool multiple = false);
signals:
    void capturingChanged();
    void captured(const QUrl &file);
    void regionsCaptured(const QVariantList &images);
    void captureFinished(bool captured);
    void error(const QString &message);
private:
    void finish(bool captured = false);
    void showSelectors();
    void captureNextScreen();
    void updateSelections();
    void removeSelection(int index);
    struct ScreenImage { QRect geometry; QImage image; };
    struct Selection { RegionSelector *owner; QRectF area; QImage image; };
    QList<ScreenImage> m_screens;
    QList<Selection> m_selections;
    int m_screenIndex = 0;
    bool m_multiple = false;
    QTemporaryDir m_temp;
    QProcess m_process;
    bool m_capturing = false;
    QString m_output;
    QList<QPointer<RegionSelector>> m_selectors;
};
