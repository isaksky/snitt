#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// Keep the review counter independent of both decoded frames and redraws.
class PlaybackClock : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
public:
    explicit PlaybackClock(QObject *parent = nullptr) : QObject(parent) {
        m_timer.setInterval(33);
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const qreal seconds = m_elapsed.nsecsElapsed() / 1000000000.0;
            m_elapsed.restart();
            emit advanced(seconds);
        });
    }

    bool running() const { return m_timer.isActive(); }
    void setRunning(bool running) {
        if (running == this->running()) return;
        if (running) {
            m_elapsed.start();
            m_timer.start();
        } else {
            m_timer.stop();
            m_elapsed.invalidate();
        }
        emit runningChanged();
    }

signals:
    void runningChanged();
    void advanced(qreal seconds);

private:
    QTimer m_timer;
    QElapsedTimer m_elapsed;
};
