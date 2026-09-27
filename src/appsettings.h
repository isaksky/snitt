#pragma once

#include <QByteArray>
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class GlobalHotkey;

class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap shortcutHints READ shortcutHints NOTIFY settingsChanged)
    Q_PROPERTY(QColor goodColor READ goodColor NOTIFY settingsChanged)
    Q_PROPERTY(QColor badColor READ badColor NOTIFY settingsChanged)
    Q_PROPERTY(QString picturesRoot READ picturesRoot NOTIFY settingsChanged)
    Q_PROPERTY(QString videosRoot READ videosRoot NOTIFY settingsChanged)
    Q_PROPERTY(QString settingsFile READ settingsFile CONSTANT)
    Q_PROPERTY(QString guideFile READ guideFile CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    struct Paths {
        QString settingsFile;
        QString guideFile;
        QString defaultPicturesRoot;
        QString defaultVideosRoot;
    };

    explicit AppSettings(QObject *parent = nullptr);
    explicit AppSettings(const Paths &paths, QObject *parent = nullptr);

    static Paths standardPaths();
    QVariantMap shortcuts() const { return m_shortcuts; }
    QVariantMap shortcutHints() const { return m_shortcutHints; }
    QColor goodColor() const { return m_goodColor; }
    QColor badColor() const { return m_badColor; }
    QString picturesRoot() const { return m_picturesRoot; }
    QString videosRoot() const { return m_videosRoot; }
    QString settingsFile() const { return m_paths.settingsFile; }
    QString guideFile() const { return m_paths.guideFile; }
    QString lastError() const { return m_lastError; }
    bool reloadNow();
    void attachGlobalHotkey(GlobalHotkey *hotkey);
signals:
    void settingsChanged();
    void errorChanged();
    void reloadFailed(const QString &message);
    void reloaded();
private:
    struct Candidate {
        // Keep INI modifier names here; publish converts them to Qt semantics.
        QVariantMap configuredShortcuts;
        QColor goodColor;
        QColor badColor;
        QString picturesRoot;
        QString videosRoot;
    };
    void initialize();
    Candidate defaults() const;
    bool parseCandidate(Candidate *candidate, QString *error) const;
    bool validateShortcuts(const QVariantMap &shortcuts, QString *error) const;
    void publish(const Candidate &candidate);
    void watchFiles();
    void pollSettingsFile();
    void scheduleReload();
    void setError(const QString &message);
    bool createGuideIfMissing(QString *error) const;
    bool createDefaultIniIfMissing(QString *error) const;
    QString initialIni() const;

    Paths m_paths;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QTimer m_filePollTimer;
    QByteArray m_observedSettingsContents;
    bool m_observedSettingsFile = false;
    bool m_hasObservedSettingsFile = false;
    GlobalHotkey *m_hotkey = nullptr;
    QVariantMap m_shortcuts;
    QVariantMap m_shortcutHints;
    QColor m_goodColor = QColor(QStringLiteral("#22c55e"));
    QColor m_badColor = QColor(QStringLiteral("#ef4444"));
    QString m_picturesRoot;
    QString m_videosRoot;
    QString m_lastError;
};
