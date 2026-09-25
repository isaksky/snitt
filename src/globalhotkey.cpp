#include "globalhotkey.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#endif

struct GlobalHotkey::State {
    bool primary = false;
    bool alternate = false;
    unsigned long error = 0;
#ifdef Q_OS_MACOS
    EventHotKeyRef hotkey = nullptr;
    EventHotKeyRef fallback = nullptr;
    EventHandlerRef handler = nullptr;
#endif
};

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent), m_state(new State) {
#ifdef Q_OS_WIN
    m_state->primary = RegisterHotKey(nullptr, 0x5853, MOD_CONTROL | MOD_NOREPEAT, VK_SNAPSHOT);
    if (!m_state->primary) m_state->error = GetLastError();
    QCoreApplication::instance()->installNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
    const auto callback = [](EventHandlerCallRef, EventRef event, void *data) -> OSStatus {
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                              nullptr, sizeof(id), nullptr, &id) != noErr || id.signature != 0x58534854)
            return eventNotHandledErr;
        emit static_cast<GlobalHotkey *>(data)->activated();
        return noErr;
    };
    if (InstallEventHandler(GetApplicationEventTarget(), callback, 1, &type, this, &m_state->handler) == noErr) {
        // PC keyboards report Print Screen as F13 on macOS. Laptop fallback too.
        m_state->primary = RegisterEventHotKey(kVK_F13, controlKey, {0x58534854, 1},
            GetApplicationEventTarget(), 0, &m_state->hotkey) == noErr;
        m_state->alternate = RegisterEventHotKey(kVK_ANSI_X, controlKey | shiftKey, {0x58534854, 2},
            GetApplicationEventTarget(), 0, &m_state->fallback) == noErr;
    }
#endif
}

GlobalHotkey::~GlobalHotkey() {
#ifdef Q_OS_WIN
    if (m_state->primary) UnregisterHotKey(nullptr, 0x5853);
    QCoreApplication::instance()->removeNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    if (m_state->hotkey) UnregisterEventHotKey(m_state->hotkey);
    if (m_state->fallback) UnregisterEventHotKey(m_state->fallback);
    if (m_state->handler) RemoveEventHandler(m_state->handler);
#endif
}

bool GlobalHotkey::registered() const { return m_state->primary; }

QString GlobalHotkey::description() const {
    QString text = m_state->primary ? QStringLiteral("Ctrl+Print Screen")
                                   : QStringLiteral("Ctrl+Print Screen unavailable (already in use?)");
#ifdef Q_OS_WIN
    if (!m_state->primary) text += QStringLiteral(" [Windows error %1]").arg(m_state->error);
#endif
#ifdef Q_OS_MACOS
    if (m_state->alternate) text += QStringLiteral(" · Ctrl+Shift+X");
#endif
    return text;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    const auto *msg = static_cast<MSG *>(message);
    if (m_state->primary && msg->message == WM_HOTKEY && msg->wParam == 0x5853) {
        emit activated();
        // Thread-level WM_HOTKEY dispatch may not provide a result pointer.
        if (result) *result = 0;
        return true;
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}
