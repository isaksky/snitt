#pragma once
#include <QObject>
#include <QLocalServer>
#include <QSystemTrayIcon>
#include <QMenu>
#include "globalhotkey.h"

class AppService : public QObject {
    Q_OBJECT
public:
    explicit AppService(QLocalServer *server, QObject *window, QObject *parent = nullptr);
    static QString serverName();
    static bool sendCommand(const QStringList &arguments);
private:
    void dispatch(const QStringList &arguments);
    QObject *m_window;
    GlobalHotkey m_hotkey;
    QMenu m_menu;
    QSystemTrayIcon m_tray;
};
