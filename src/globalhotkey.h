#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <memory>

// Native registration rather than a keyboard hook: no keystrokes are recorded.
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;
    bool registered() const;
    quint32 nativeRegistrationId() const;
    // Uses Qt modifier semantics, just like QShortcut and QKeySequence::NativeText.
    bool setShortcut(const QKeySequence &sequence, QString *error = nullptr);
    QKeySequence shortcut() const;
    QString description() const;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
signals:
    void activated();
private:
    struct State;
    std::unique_ptr<State> m_state;
};
