#pragma once

#include <QObject>
#include <QString>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString greeting READ greeting NOTIFY greetingChanged)

public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}

    QString greeting() const {
        return m_count == 0
            ? QStringLiteral("Hello, world!")
            : QStringLiteral("Hello from C++! (%1)").arg(m_count);
    }

    Q_INVOKABLE void sayHello() {
        ++m_count;
        emit greetingChanged();
    }

signals:
    void greetingChanged();

private:
    int m_count = 0;
};
