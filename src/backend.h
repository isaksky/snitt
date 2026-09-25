#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QPointer>
#include <QList>
class RegionSelector;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;
    bool capturing() const { return m_capturing; }
    Q_INVOKABLE void capture();
signals:
    void capturingChanged();
    void captured(const QUrl &file);
    void captureFinished(bool captured);
    void error(const QString &message);
private:
    void finish(bool captured = false);
    QTemporaryDir m_temp;
    QProcess m_process;
    bool m_capturing = false;
    QString m_output;
    QList<QPointer<RegionSelector>> m_selectors;
};
