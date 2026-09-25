#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
public:
    explicit Backend(QObject *parent = nullptr);
    bool capturing() const { return m_capturing; }
    Q_INVOKABLE void capture();
signals:
    void capturingChanged();
    void captured(const QUrl &file);
    void captureFinished();
    void error(const QString &message);
private:
    void finish();
    QTemporaryDir m_temp;
    QProcess m_process;
    bool m_capturing = false;
    QString m_output;
};
