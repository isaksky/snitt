#pragma once
#include <QObject>
#include <QLocalServer>
#include <QSystemTrayIcon>
#include <QMenu>
#include "globalhotkey.h"
#include "appsettings.h"
class EditorSessions;

class AppService : public QObject {
    Q_OBJECT
public:
    explicit AppService(QLocalServer *server, EditorSessions *editors, AppSettings *settings, QObject *parent = nullptr);
    static QString serverName();
    static bool sendCommand(const QStringList &arguments);
private:
    void dispatch(const QStringList &arguments);
    EditorSessions *m_editors;
    AppSettings *m_settings;
    GlobalHotkey m_hotkey;
    QMenu m_menu;
    QSystemTrayIcon m_tray;
    QAction *m_hotkeyAction = nullptr;
};
