#include "appservice.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

QString AppService::serverName() {
    const QByteArray user = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    QString name = QStringLiteral("snitt-") + QString::fromLatin1(user);
#ifdef Q_OS_WIN
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    name += QStringLiteral("-%1").arg(session);
#endif
    return name;
}

bool AppService::sendCommand(const QStringList &arguments) {
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(500)) return false;
    socket.write(QJsonDocument(QJsonArray::fromStringList(arguments)).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    return socket.bytesToWrite() == 0 || socket.waitForBytesWritten(1000);
}

AppService::AppService(QLocalServer *server, QObject *window, AppSettings *settings, QObject *parent)
    : QObject(parent), m_window(window), m_settings(settings) {
    auto capture = [this] { QMetaObject::invokeMethod(m_window, "capture"); };
    auto hotkeyCapture = [this] { QMetaObject::invokeMethod(m_window, "hotkeyCapture"); };
    auto show = [this] { QMetaObject::invokeMethod(m_window, "showEditor"); };
    connect(&m_hotkey, &GlobalHotkey::activated, this, hotkeyCapture);
    m_menu.addAction(QStringLiteral("Capture region"), this, capture);
    m_menu.addAction(QStringLiteral("Open editor"), this, show);
    m_menu.addSeparator();
    m_hotkeyAction = m_menu.addAction(QString());
    m_hotkeyAction->setEnabled(false);
    m_menu.addSeparator();
    m_menu.addAction(QStringLiteral("Quit Snitt"), qApp, &QCoreApplication::quit);
    QPixmap pixmap(32, 32); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
#ifdef Q_OS_MACOS
    p.setPen(QPen(Qt::black, 3));
#else
    p.setPen(QPen(QColor("#91bff0"), 3));
#endif
    p.drawRoundedRect(QRectF(3, 7, 26, 20), 4, 4);
    p.drawEllipse(QPointF(16, 17), 5, 5);
    p.drawLine(QPointF(10, 4), QPointF(21, 4)); p.end();
    QIcon icon(pixmap);
#ifdef Q_OS_MACOS
    icon.setIsMask(true);
#endif
    m_tray.setIcon(icon);
    const auto refreshShortcutText = [this] {
        m_hotkeyAction->setText(m_hotkey.description());
        m_tray.setToolTip(QStringLiteral("Snitt · ") + m_hotkey.description());
    };
    connect(m_settings, &AppSettings::settingsChanged, this, refreshShortcutText);
    connect(m_settings, &AppSettings::reloadFailed, this, [this](const QString &message) {
        m_tray.showMessage(QStringLiteral("Snitt settings not applied"), message, QSystemTrayIcon::Warning);
    });
    m_settings->attachGlobalHotkey(&m_hotkey);
    refreshShortcutText();
    m_tray.setContextMenu(&m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this, [capture](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) capture();
    });
    m_tray.show();
    if (!m_hotkey.registered()) {
        QTimer::singleShot(500, this, [this] {
            m_tray.showMessage(QStringLiteral("Snitt shortcut unavailable"),
                              m_hotkey.description() + QStringLiteral(". Use Capture region in this menu."), QSystemTrayIcon::Warning);
        });
    }
    if (!m_settings->lastError().isEmpty())
        QTimer::singleShot(750, this, [this] {
            m_tray.showMessage(QStringLiteral("Snitt settings not applied"), m_settings->lastError(), QSystemTrayIcon::Warning);
        });
    connect(server, &QLocalServer::newConnection, this, [this, server] {
        while (auto *socket = server->nextPendingConnection()) {
            socket->setParent(this);
            socket->setReadBufferSize(64 * 1024);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            const auto read = [this, socket] {
                if (!socket->canReadLine()) return;
                const auto message = QJsonDocument::fromJson(socket->readLine(64 * 1024)).array();
                QStringList args;
                for (const auto &value : message) args.append(value.toString());
                dispatch(args);
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, read);
            QTimer::singleShot(2000, socket, [socket] { socket->disconnectFromServer(); });
            read();
        }
    });
}

void AppService::dispatch(const QStringList &arguments) {
    if (arguments.contains("--quit")) { qApp->quit(); return; }
    if (arguments.contains("--background")) return;
    if (arguments.contains("--show")) { QMetaObject::invokeMethod(m_window, "showEditor"); return; }
    for (const auto &arg : arguments) {
        if (!arg.startsWith('-')) {
            QMetaObject::invokeMethod(m_window, "openImage", Q_ARG(QVariant, QUrl::fromLocalFile(QFileInfo(arg).absoluteFilePath())));
            return;
        }
    }
    QMetaObject::invokeMethod(m_window, "capture");
}
