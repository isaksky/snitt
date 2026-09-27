#include "globalhotkey.h"

#include <QCoreApplication>
#include <QAtomicInteger>
#include <QHash>
#include <QKeyCombination>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#endif

namespace {
QAtomicInteger<quint32> nextRegistrationId{0x5853};

quint32 allocateRegistrationId() {
    quint32 id = nextRegistrationId.fetchAndAddRelaxed(1);
    if (id < 0x5853 || id == 0) {
        nextRegistrationId.storeRelaxed(0x5854);
        id = 0x5853;
    }
    return id;
}

#ifdef Q_OS_WIN
UINT windowsKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return UINT('A' + key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return UINT('0' + key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return UINT(VK_F1 + key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    default: return 0;
    }
}

UINT windowsModifiers(Qt::KeyboardModifiers modifiers) {
    UINT native = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier)) native |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier)) native |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier)) native |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier)) native |= MOD_WIN;
    return native;
}
#elif defined(Q_OS_MACOS)
UInt32 macKey(int key) {
    static const QHash<int, UInt32> letters {
        {Qt::Key_A, 0x00}, {Qt::Key_S, 0x01}, {Qt::Key_D, 0x02}, {Qt::Key_F, 0x03},
        {Qt::Key_H, 0x04}, {Qt::Key_G, 0x05}, {Qt::Key_Z, 0x06}, {Qt::Key_X, 0x07},
        {Qt::Key_C, 0x08}, {Qt::Key_V, 0x09}, {Qt::Key_B, 0x0b}, {Qt::Key_Q, 0x0c},
        {Qt::Key_W, 0x0d}, {Qt::Key_E, 0x0e}, {Qt::Key_R, 0x0f}, {Qt::Key_Y, 0x10},
        {Qt::Key_T, 0x11}, {Qt::Key_O, 0x1f}, {Qt::Key_U, 0x20}, {Qt::Key_I, 0x22},
        {Qt::Key_P, 0x23}, {Qt::Key_L, 0x25}, {Qt::Key_J, 0x26}, {Qt::Key_K, 0x28},
        {Qt::Key_N, 0x2d}, {Qt::Key_M, 0x2e}
    };
    if (letters.contains(key)) return letters.value(key);
    static const UInt32 digitCodes[] = {0x1d, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1a, 0x1c, 0x19};
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return digitCodes[key - Qt::Key_0];
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        static const UInt32 functionCodes[] = {0x7a, 0x78, 0x63, 0x76, 0x60, 0x61,
                                               0x62, 0x64, 0x65, 0x6d, 0x67, 0x6f};
        return functionCodes[key - Qt::Key_F1];
    }
    if (key >= Qt::Key_F13 && key <= Qt::Key_F20) {
        static const UInt32 functionCodes[] = {0x69, 0x6b, 0x71, 0x6a, 0x40, 0x4f, 0x50, 0x5a};
        return functionCodes[key - Qt::Key_F13];
    }
    switch (key) {
    case Qt::Key_Print: return 0x69; // Print Screen is F13 on Apple keyboards.
    case Qt::Key_Space: return kVK_Space;
    case Qt::Key_Return: case Qt::Key_Enter: return kVK_Return;
    case Qt::Key_Escape: return kVK_Escape;
    case Qt::Key_Tab: return kVK_Tab;
    case Qt::Key_Backspace: return kVK_Delete;
    case Qt::Key_Delete: return kVK_ForwardDelete;
    case Qt::Key_Home: return kVK_Home;
    case Qt::Key_End: return kVK_End;
    case Qt::Key_PageUp: return kVK_PageUp;
    case Qt::Key_PageDown: return kVK_PageDown;
    case Qt::Key_Left: return kVK_LeftArrow;
    case Qt::Key_Right: return kVK_RightArrow;
    case Qt::Key_Up: return kVK_UpArrow;
    case Qt::Key_Down: return kVK_DownArrow;
    default: return UINT32_MAX;
    }
}

UInt32 macModifiers(Qt::KeyboardModifiers modifiers) {
    UInt32 native = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) native |= controlKey;
    if (modifiers.testFlag(Qt::AltModifier)) native |= optionKey;
    if (modifiers.testFlag(Qt::ShiftModifier)) native |= shiftKey;
    if (modifiers.testFlag(Qt::MetaModifier)) native |= cmdKey;
    return native;
}
#endif
}

struct GlobalHotkey::State {
    QKeySequence sequence;
    bool primary = false;
    unsigned long error = 0;
    quint32 registrationId = 0;
#ifdef Q_OS_MACOS
    EventHotKeyRef hotkey = nullptr;
    EventHandlerRef handler = nullptr;
#endif
};

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent), m_state(new State) {
#ifdef Q_OS_WIN
    QCoreApplication::instance()->installNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
    const auto callback = [](EventHandlerCallRef, EventRef event, void *data) -> OSStatus {
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                              nullptr, sizeof(id), nullptr, &id) != noErr || id.signature != 0x58534854)
            return eventNotHandledErr;
        auto *owner = static_cast<GlobalHotkey *>(data);
        if (!owner->m_state->primary || id.id != owner->m_state->registrationId)
            return eventNotHandledErr;
        emit owner->activated();
        return noErr;
    };
    if (InstallEventHandler(GetApplicationEventTarget(), callback, 1, &type, this, &m_state->handler) != noErr)
        m_state->error = static_cast<unsigned long>(-1);
#endif
    QString ignored;
    setShortcut(QKeySequence::fromString(QStringLiteral("Ctrl+Print"), QKeySequence::PortableText), &ignored);
}

GlobalHotkey::~GlobalHotkey() {
#ifdef Q_OS_WIN
    if (m_state->primary) UnregisterHotKey(nullptr, m_state->registrationId);
    QCoreApplication::instance()->removeNativeEventFilter(this);
#elif defined(Q_OS_MACOS)
    if (m_state->hotkey) UnregisterEventHotKey(m_state->hotkey);
    if (m_state->handler) RemoveEventHandler(m_state->handler);
#endif
}

bool GlobalHotkey::registered() const { return m_state->primary; }
QKeySequence GlobalHotkey::shortcut() const { return m_state->sequence; }

bool GlobalHotkey::setShortcut(const QKeySequence &sequence, QString *error) {
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown) {
        if (error) *error = QStringLiteral("Choose one supported key with at least one modifier.");
        return false;
    }
    if (m_state->sequence == sequence && m_state->primary) return true;
    const Qt::KeyboardModifiers modifiers = sequence[0].keyboardModifiers();
    if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier))
        || (modifiers & (Qt::KeypadModifier | Qt::GroupSwitchModifier))) {
        if (error) *error = QStringLiteral("A system-wide shortcut needs a modifier and cannot use keypad or group-switch modifiers.");
        return false;
    }

#ifdef Q_OS_WIN
    const UINT key = windowsKey(sequence[0].key());
    if (!key) {
        if (error) *error = QStringLiteral("That key is not supported for a Windows system-wide shortcut.");
        return false;
    }
    const quint32 newId = allocateRegistrationId();
    if (!RegisterHotKey(nullptr, newId, windowsModifiers(modifiers), key)) {
        m_state->error = GetLastError();
        if (error) *error = QStringLiteral("Windows could not register the shortcut (error %1). It may already be in use.").arg(m_state->error);
        return false;
    }
    const quint32 previousId = m_state->registrationId;
    const bool hadPrevious = m_state->primary;
    m_state->sequence = sequence;
    m_state->registrationId = newId;
    m_state->primary = true;
    m_state->error = 0;
    if (hadPrevious) UnregisterHotKey(nullptr, previousId);
    return true;
#elif defined(Q_OS_MACOS)
    const UInt32 key = macKey(sequence[0].key());
    if (key == UINT32_MAX) {
        if (error) *error = QStringLiteral("That key is not supported for a macOS system-wide shortcut.");
        return false;
    }
    if (!m_state->handler) {
        if (error) *error = QStringLiteral("Could not install the macOS keyboard event handler.");
        return false;
    }
    const quint32 newId = allocateRegistrationId();
    EventHotKeyRef next = nullptr;
    const OSStatus status = RegisterEventHotKey(key, macModifiers(modifiers), {0x58534854, newId},
                                                GetApplicationEventTarget(), 0, &next);
    if (status != noErr) {
        m_state->error = static_cast<unsigned long>(status);
        if (error) *error = QStringLiteral("macOS could not register the shortcut (status %1). It may already be in use.").arg(status);
        return false;
    }
    EventHotKeyRef previous = m_state->hotkey;
    m_state->hotkey = next;
    m_state->sequence = sequence;
    m_state->registrationId = newId;
    m_state->primary = true;
    m_state->error = 0;
    if (previous) UnregisterEventHotKey(previous);
    return true;
#else
    Q_UNUSED(modifiers)
    if (error) *error = QStringLiteral("System-wide shortcuts are supported on macOS and Windows only.");
    return false;
#endif
}

QString GlobalHotkey::description() const {
    QString text = m_state->sequence.toString(QKeySequence::NativeText);
#ifdef Q_OS_MACOS
    if (m_state->sequence.count() == 1 && m_state->sequence[0].key() == Qt::Key_Print)
        text.replace(QStringLiteral("Print"), QStringLiteral("F13 / Print Screen"));
#endif
    if (text.isEmpty()) text = QStringLiteral("Capture shortcut");
    if (!m_state->primary) {
        text += QStringLiteral(" unavailable (already in use?)");
#ifdef Q_OS_WIN
        if (m_state->error) text += QStringLiteral(" [Windows error %1]").arg(m_state->error);
#elif defined(Q_OS_MACOS)
        if (m_state->error) text += QStringLiteral(" [macOS status %1]").arg(m_state->error);
#endif
    }
    return text;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    const auto *msg = static_cast<MSG *>(message);
    if (m_state->primary && msg->message == WM_HOTKEY && msg->wParam == m_state->registrationId) {
        emit activated();
        if (result) *result = 0;
        return true;
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}
