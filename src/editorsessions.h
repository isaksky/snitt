#pragma once
#include <QObject>
#include <QList>
#include <QPointer>
#include <QQmlComponent>
#include <QWindow>

class AppSettings;
class Backend;
class QQmlEngine;
class QQuickWindow;

// Owns independent editor documents while serializing desktop capture/recording.
class EditorSessions : public QObject {
    Q_OBJECT
public:
    EditorSessions(QQmlEngine *engine, AppSettings *settings, QObject *parent = nullptr);
    ~EditorSessions() override;
    bool isReady() const;
    QList<QQuickWindow *> windows() const;
    Q_INVOKABLE void capture(bool multiple = false, bool video = false);
    Q_INVOKABLE void hotkeyCapture();
    Q_INVOKABLE void showEditor();
    Q_INVOKABLE void openImage(const QUrl &file);
    Q_INVOKABLE void closeEditor(QObject *window);
signals:
    void error(const QString &message);
protected:
    virtual Backend *createBackend(QObject *owner);
private:
    struct Session;
    struct HiddenEditor {
        QPointer<QQuickWindow> window;
        QWindow::Visibility visibility;
    };
    Session *createSession(const QString &label = {});
    void completeCapture(Session *session);
    void removeSession(Session *session);
    void activate(Session *session);
    QQmlEngine *m_engine;
    AppSettings *m_settings;
    QQmlComponent m_component;
    QList<Session *> m_sessions;
    QPointer<Session> m_capture;
    QPointer<Session> m_lastActive;
    QPointer<QQuickWindow> m_restoreFocus;
    QList<HiddenEditor> m_hiddenEditors;
    int m_nextNumber = 1;
};
