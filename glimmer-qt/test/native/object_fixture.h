#pragma once
#include <QObject>
#include <QPointer>
#include <QMetaMethod>
#include <QUrl>
#include <QVariant>
#include <QThread>
#include <thread>
#include <vector>
#include <stdexcept>
#include <atomic>
#include "../../native/extensions.h"

class ObjectFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY changed)
    Q_PROPERTY(QObject* relay MEMBER relay)
    Q_PROPERTY(QObject* child READ child CONSTANT)
    Q_PROPERTY(QObject* reference READ reference WRITE setReference NOTIFY changed)
    Q_PROPERTY(int connects READ connects)
    Q_PROPERTY(int disconnects READ disconnects)
public:
    static inline std::atomic<bool> awaitingGc{false}, collected{false};
    ObjectFixture() : m_child(new QObject(this)) { m_child->setObjectName("native child"); }
    ~ObjectFixture() override { for (auto &thread : workers) thread.join(); }
    int count() const { return m_count; }
    QObject *relay = nullptr;
    void setCount(int n) {
        if (n == m_count) return;
        m_count = n;
        emit changed();
        if (relay) QMetaObject::invokeMethod(relay, "dispatch", Qt::DirectConnection,
            Q_ARG(QObject *, this), Q_ARG(QString, "on-change"), Q_ARG(QString, QString::number(n)));
    }
    QObject *child() const { return m_child; }
    QObject *reference() const { return m_reference; }
    void setReference(QObject *value) { m_reference = value; emit changed(); }
    int connects() const { return m_connects; }
    int disconnects() const { return m_disconnects; }
    Q_INVOKABLE void exposeChild() { gqt_expose_object(m_child); }
    Q_INVOKABLE void removeChild() { delete m_child; m_child = nullptr; }
    Q_INVOKABLE void change(int n) { setCount(n); }
    Q_INVOKABLE int overloaded(int n) { return n + 1; }
    Q_INVOKABLE QString overloaded(const QString &s) { return s + "!"; }
    Q_INVOKABLE qint64 duration(qint64 n) { return n; }
    Q_INVOKABLE bool enabled(bool b) { return !b; }
    Q_INVOKABLE double scale(double a, double b) { return a * b; }
    Q_INVOKABLE QUrl url(const QUrl &url) { return url; }
    Q_INVOKABLE QVariantList list(const QVariantList &list) { return list; }
    Q_INVOKABLE QStringList strings(const QStringList &list) { return list; }
    Q_INVOKABLE QVariantMap map(const QVariantMap &map) { return map; }
    Q_INVOKABLE QVariant variant(const QVariant &v) { return v; }
    Q_INVOKABLE QObject *object(QObject *o) { return o; }
    Q_INVOKABLE ObjectFixture *typedObject(ObjectFixture *o) { return o; }
    Q_INVOKABLE void throws() { throw std::runtime_error("fixture method failure"); }
    Q_INVOKABLE bool awaitGc() {
        collected = false;
        awaitingGc = true;
        for (int n = 0; n < 2000 && !collected; ++n) QThread::msleep(1);
        awaitingGc = false;
        return collected;
    }
    Q_INVOKABLE QByteArray unsupported() { ++m_count; return "unsupported"; }
    Q_INVOKABLE void bytes(QByteArray) { ++m_count; }
    Q_INVOKABLE void fire(int n) { emit payload(n, {"日本語", qint64(9007199254740993LL)}, QUrl("file:///tmp/a%20b"), m_child); }
    Q_INVOKABLE void fireObject(QObject *o) { emit payload(9, {}, QUrl("file:///tmp/queued"), o); }
    Q_INVOKABLE void fireWorker(int n) {
        auto *child = m_child.data();
        workers.emplace_back([this, n, child] {
            emit payload(n, {"worker", qint64(9007199254740993LL)}, QUrl("file:///tmp/worker%20value"), child);
        });
    }
signals:
    void changed();
    void payload(int n, const QVariantList &list, const QUrl &url, QObject *object);
    void bytesSignal(QByteArray bytes);
protected:
    void connectNotify(const QMetaMethod &signal) override {
        if (signal.name() == "changed" || signal.name() == "payload") ++m_connects;
    }
    void disconnectNotify(const QMetaMethod &signal) override {
        if (signal.name() == "changed" || signal.name() == "payload") ++m_disconnects;
    }
private:
    int m_count = 0, m_connects = 0, m_disconnects = 0;
    QPointer<QObject> m_child, m_reference;
    std::vector<std::thread> workers;
};
