#include "appsettings.h"

#include "globalhotkey.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

namespace {
struct ShortcutDefinition {
    const char *name;
    const char *otherDefault;
    const char *macDefault;
    const char *context;
};

const ShortcutDefinition shortcutDefinitions[] = {
    {"globalCapture", "Ctrl+Print", "Ctrl+Print", "global"},
    {"capture", "Ctrl+N", "Meta+N", "editorBase"},
    {"captureMultiple", "M", "M", "editorBase"},
    {"captureVideo", "V", "V", "editorBase"},
    {"openImage", "Ctrl+O", "Meta+O", "editorBase"},
    {"pasteImage", "Ctrl+V", "Meta+V", "editorBase"},
    {"copyClose", "C", "C", "editorBase"},
    {"saveClose", "S", "S", "editorBase"},
    {"dismiss", "Escape", "Escape", "editorBase"},
    {"undo", "Ctrl+Z", "Meta+Z", "editorAnnotate"},
    {"redo", "Ctrl+Y", "Meta+Shift+Z", "editorAnnotate"},
    {"toolCut", "X", "X", "editorAnnotate"},
    {"toolRectangle", "R", "R", "editorAnnotate"},
    {"toolHighlight", "H", "H", "editorAnnotate"},
    {"toolText", "T", "T", "editorAnnotate"},
    {"toolArrow", "A", "A", "editorAnnotate"},
    {"toolPixelate", "P", "P", "editorAnnotate"},
    {"toolErase", "E", "E", "editorAnnotate"},
    {"goodMode", "G", "G", "editorAnnotate"},
    {"badMode", "B", "B", "editorAnnotate"},
    {"arrange", "Return|Enter", "Return|Enter", "editorArrange"},
    {"columnsMore", "Shift+=|+|=", "Shift+=|+|=", "editorArrange"},
    {"columnsLess", "-", "-", "editorArrange"},
    {"removeRegion", "Backspace|Delete", "Backspace|Delete", "editorArrange"},
    {"moveRegionLeft", "Left", "Left", "editorArrange"},
    {"moveRegionRight", "Right", "Right", "editorArrange"},
    {"regionCancel", "Escape", "Escape", "region"},
    {"regionSingle", "R", "R", "region"},
    {"regionMultiple", "M", "M", "region"},
    {"regionVideo", "V", "V", "region"},
    {"regionArrange", "Return|Enter", "Return|Enter", "region"},
    {"regionRemoveLast", "Backspace|Delete", "Backspace|Delete", "region"},
    {"reviewPlayPause", "Space", "Space", "review"},
    {"reviewSeekBackward", "Left", "Left", "review"},
    {"reviewSeekForward", "Right", "Right", "review"},
    {"reviewSeekBackwardFar", "Shift+Left", "Shift+Left", "review"},
    {"reviewSeekForwardFar", "Shift+Right", "Shift+Right", "review"},
    {"reviewMarkStart", "Ctrl+Space", "Ctrl+Space", "review"},
    {"reviewMarkEnd", "Alt+Space", "Alt+Space", "review"},
    {"reviewCancel", "Escape", "Escape", "review"},
};

QString defaultSequence(const ShortcutDefinition &definition) {
#ifdef Q_OS_MACOS
    return QString::fromLatin1(definition.macDefault);
#else
    return QString::fromLatin1(definition.otherDefault);
#endif
}

QStringList splitAlternatives(const QString &value) {
    if (value.trimmed().isEmpty()) return {};
    const QStringList pieces = value.split(QLatin1Char('|'), Qt::KeepEmptyParts);
    QStringList result;
    for (const QString &piece : pieces) result.append(piece.trimmed());
    return result;
}

// INI names describe physical modifiers. Qt normally swaps Control/Meta on
// macOS, so translate only at the settings boundary; every consumer then uses
// ordinary Qt sequences, including native hotkey registration and hint text.
QKeySequence qtSequence(const QString &configured) {
    QKeySequence sequence = QKeySequence::fromString(configured, QKeySequence::PortableText);
#ifdef Q_OS_MACOS
    if (sequence.count() == 1) {
        const auto combination = sequence[0];
        const auto configuredModifiers = combination.keyboardModifiers();
        auto qtModifiers = configuredModifiers & ~(Qt::ControlModifier | Qt::MetaModifier);
        if (configuredModifiers.testFlag(Qt::ControlModifier)) qtModifiers |= Qt::MetaModifier;
        if (configuredModifiers.testFlag(Qt::MetaModifier)) qtModifiers |= Qt::ControlModifier;
        sequence = QKeySequence(QKeyCombination(qtModifiers, combination.key()));
    }
#endif
    return sequence;
}

QString iniString(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString displaySequence(const QKeySequence &sequence) {
    QString text = sequence.toString(QKeySequence::NativeText);
#ifdef Q_OS_MACOS
    if (sequence.count() == 1 && sequence[0].key() == Qt::Key_Print)
        text.replace(QStringLiteral("Print"), QStringLiteral("F13 / Print Screen"));
#endif
    return text;
}

QString normalizedRoot(QString value, const QString &fallback, QString *error, const QString &key) {
    value = value.trimmed();
    if (value.isEmpty()) value = fallback;
    if (value == QStringLiteral("~")) value = QDir::homePath();
    else if (value.startsWith(QStringLiteral("~/"))) value = QDir::homePath() + value.mid(1);
    value = QDir::cleanPath(QDir::fromNativeSeparators(value));
    if (value.isEmpty() || value == QStringLiteral("." ) || !QDir::isAbsolutePath(value)) {
        if (error) *error = QStringLiteral("Save/%1 must be empty (platform default) or an absolute folder path.").arg(key);
        return {};
    }
    return value;
}

bool sameSequence(const QString &left, const QString &right) {
    return QKeySequence::fromString(left, QKeySequence::PortableText)
        == QKeySequence::fromString(right, QKeySequence::PortableText);
}
}

AppSettings::Paths AppSettings::standardPaths() {
    Paths paths;
    QString config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (config.isEmpty()) config = QDir(QDir::homePath()).filePath(QStringLiteral(".config/snitt"));
    paths.settingsFile = QDir(config).filePath(QStringLiteral("settings.ini"));
    paths.guideFile = QDir(config).filePath(QStringLiteral("settings-format.md"));

    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) pictures = QDir(QDir::homePath()).filePath(QStringLiteral("Pictures"));
    paths.defaultPicturesRoot = QDir(pictures).filePath(QStringLiteral("snitt"));
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) videos = QDir::homePath();
    paths.defaultVideosRoot = QDir(videos).filePath(QStringLiteral("snitt"));
    return paths;
}

AppSettings::AppSettings(QObject *parent) : AppSettings(standardPaths(), parent) {}

AppSettings::AppSettings(const Paths &paths, QObject *parent) : QObject(parent), m_paths(paths) {
    const Paths standard = standardPaths();
    if (m_paths.settingsFile.isEmpty()) m_paths.settingsFile = standard.settingsFile;
    if (m_paths.guideFile.isEmpty()) m_paths.guideFile = standard.guideFile;
    if (m_paths.defaultPicturesRoot.isEmpty()) m_paths.defaultPicturesRoot = standard.defaultPicturesRoot;
    if (m_paths.defaultVideosRoot.isEmpty()) m_paths.defaultVideosRoot = standard.defaultVideosRoot;
    m_paths.settingsFile = QFileInfo(m_paths.settingsFile).absoluteFilePath();
    m_paths.guideFile = QFileInfo(m_paths.guideFile).absoluteFilePath();

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(220);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] { reloadNow(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReload(); });
    m_filePollTimer.setInterval(300);
    connect(&m_filePollTimer, &QTimer::timeout, this, [this] { pollSettingsFile(); });
    initialize();
    m_filePollTimer.start();
}

AppSettings::Candidate AppSettings::defaults() const {
    Candidate candidate;
    for (const auto &definition : shortcutDefinitions) {
        const QStringList alternatives = splitAlternatives(defaultSequence(definition));
        candidate.configuredShortcuts.insert(QString::fromLatin1(definition.name), alternatives);
    }
    candidate.goodColor = QColor(QStringLiteral("#22c55e"));
    candidate.badColor = QColor(QStringLiteral("#ef4444"));
    candidate.picturesRoot = QDir::cleanPath(m_paths.defaultPicturesRoot);
    candidate.videosRoot = QDir::cleanPath(m_paths.defaultVideosRoot);
    return candidate;
}

QString AppSettings::initialIni() const {
    const Candidate initial = defaults();
    QString contents = QStringLiteral(
        "; Snitt application settings (UTF-8 INI format).\n"
        "; AI agents: read settings-format.md in this same directory before editing.\n"
        "; Preserve unrelated keys and comments. Unknown keys are ignored by Snitt.\n\n"
        "[Shortcuts]\n");
    for (const auto &definition : shortcutDefinitions) {
        const QString key = QString::fromLatin1(definition.name);
        contents += key + QLatin1Char('=') + initial.configuredShortcuts.value(key).toStringList().join(QLatin1Char('|')) + QLatin1Char('\n');
    }
    contents += QStringLiteral("\n[Colors]\ngood=#22c55e\nbad=#ef4444\n\n[Save]\npicturesRoot=");
    contents += iniString(QDir::fromNativeSeparators(initial.picturesRoot));
    contents += QStringLiteral("\nvideosRoot=");
    contents += iniString(QDir::fromNativeSeparators(initial.videosRoot));
    contents += QLatin1Char('\n');
    return contents;
}

bool AppSettings::createGuideIfMissing(QString *error) const {
    if (QFileInfo::exists(m_paths.guideFile)) return true;
    const QFileInfo target(m_paths.guideFile);
    if (!QDir().mkpath(target.absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the settings guide folder %1.").arg(target.absolutePath());
        return false;
    }
    QFile source(QStringLiteral(":/settings-format.md"));
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The installed settings format guide is missing from the application resources.");
        return false;
    }
    QFile output(m_paths.guideFile);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (QFileInfo::exists(m_paths.guideFile)) return true;
        if (error) *error = QStringLiteral("Could not create the settings guide at %1: %2")
            .arg(m_paths.guideFile, output.errorString());
        return false;
    }
    const QByteArray contents = source.readAll();
    if (output.write(contents) != contents.size() || !output.flush()) {
        if (error) *error = QStringLiteral("Could not finish writing the settings guide at %1.").arg(m_paths.guideFile);
        return false;
    }
    return true;
}

bool AppSettings::createDefaultIniIfMissing(QString *error) const {
    if (QFileInfo::exists(m_paths.settingsFile)) return true;
    const QFileInfo target(m_paths.settingsFile);
    if (!QDir().mkpath(target.absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the settings folder %1.").arg(target.absolutePath());
        return false;
    }
    QFile output(m_paths.settingsFile);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (QFileInfo::exists(m_paths.settingsFile)) return true;
        if (error) *error = QStringLiteral("Could not create settings at %1: %2")
            .arg(m_paths.settingsFile, output.errorString());
        return false;
    }
    const QByteArray contents = initialIni().toUtf8();
    if (output.write(contents) != contents.size() || !output.flush()) {
        if (error) *error = QStringLiteral("Could not finish writing settings at %1.").arg(m_paths.settingsFile);
        return false;
    }
    return true;
}

void AppSettings::initialize() {
    Candidate initial = defaults();
    publish(initial);
    QString error;
    const bool guideReady = createGuideIfMissing(&error);
    if (!guideReady) setError(error);
    if (guideReady && !createDefaultIniIfMissing(&error)) setError(error);
    pollSettingsFile();
    if (QFileInfo::exists(m_paths.settingsFile)) reloadNow();
    watchFiles();
}

bool AppSettings::parseCandidate(Candidate *candidate, QString *error) const {
    *candidate = defaults();
    if (!QFileInfo::exists(m_paths.settingsFile)) {
        if (error) *error = QStringLiteral("The settings file is missing. Restore it at %1; Snitt is keeping the last valid settings.")
            .arg(QDir::toNativeSeparators(m_paths.settingsFile));
        return false;
    }
    // Parse a stable byte snapshot at a unique path. QSettings caches parsed
    // INI files using filesystem metadata; on Windows, a same-size in-place
    // edit can retain the same timestamp and otherwise return stale values.
    QFile settingsFile(m_paths.settingsFile);
    if (!settingsFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not parse or read %1. Correct the INI syntax; Snitt is keeping the last valid settings.")
            .arg(QDir::toNativeSeparators(m_paths.settingsFile));
        return false;
    }
    const QByteArray bytes = settingsFile.readAll();
    if (settingsFile.error() != QFileDevice::NoError) {
        if (error) *error = QStringLiteral("Could not parse or read %1. Correct the INI syntax; Snitt is keeping the last valid settings.")
            .arg(QDir::toNativeSeparators(m_paths.settingsFile));
        return false;
    }
    QTemporaryFile snapshot;
    if (!snapshot.open() || snapshot.write(bytes) != bytes.size() || !snapshot.flush()) {
        if (error) *error = QStringLiteral("Could not create a temporary settings snapshot while reading %1.")
            .arg(QDir::toNativeSeparators(m_paths.settingsFile));
        return false;
    }
    const QString snapshotPath = snapshot.fileName();
    snapshot.close();
    QSettings input(snapshotPath, QSettings::IniFormat);
    input.setFallbacksEnabled(false);
    input.sync();
    // QSettings parses sections lazily. Read every section before checking
    // status so malformed unused sections cannot slip through a reload.
    input.allKeys();
    if (input.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("Could not parse or read %1. Correct the INI syntax; Snitt is keeping the last valid settings.")
            .arg(QDir::toNativeSeparators(m_paths.settingsFile));
        return false;
    }

    QStringList knownKeys{QStringLiteral("Colors/good"), QStringLiteral("Colors/bad"),
                          QStringLiteral("Save/picturesRoot"), QStringLiteral("Save/videosRoot")};
    for (const auto &definition : shortcutDefinitions)
        knownKeys.append(QStringLiteral("Shortcuts/") + QString::fromLatin1(definition.name));
    for (const QString &key : knownKeys) {
        if (input.contains(key) && input.value(key).metaType() != QMetaType::fromType<QString>()) {
            if (error) *error = QStringLiteral("%1 must be one text value. Quote values containing commas or semicolons with double quotes; use | between shortcut alternatives.").arg(key);
            return false;
        }
    }

    for (const auto &definition : shortcutDefinitions) {
        const QString key = QString::fromLatin1(definition.name);
        const QString settingsKey = QStringLiteral("Shortcuts/") + key;
        if (!input.contains(settingsKey)) continue;
        const QString raw = input.value(settingsKey).toString();
        if (raw.trimmed().isEmpty() && key != QStringLiteral("globalCapture")) {
            candidate->configuredShortcuts.insert(key, QStringList{});
            continue;
        }
        const QStringList alternatives = splitAlternatives(raw);
        if (alternatives.isEmpty()) {
            if (error) *error = QStringLiteral("Shortcuts/%1 cannot be empty because it controls global capture and recording stop.").arg(key);
            return false;
        }
        candidate->configuredShortcuts.insert(key, alternatives);
    }

    const QString good = input.contains(QStringLiteral("Colors/good"))
        ? input.value(QStringLiteral("Colors/good")).toString().trimmed() : candidate->goodColor.name();
    const QString bad = input.contains(QStringLiteral("Colors/bad"))
        ? input.value(QStringLiteral("Colors/bad")).toString().trimmed() : candidate->badColor.name();
    static const QRegularExpression hexColor(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!hexColor.match(good).hasMatch()) {
        if (error) *error = QStringLiteral("Colors/good must use #RRGGBB syntax (for example #22c55e).");
        return false;
    }
    if (!hexColor.match(bad).hasMatch()) {
        if (error) *error = QStringLiteral("Colors/bad must use #RRGGBB syntax (for example #ef4444).");
        return false;
    }
    candidate->goodColor = QColor(good);
    candidate->badColor = QColor(bad);

    candidate->picturesRoot = normalizedRoot(
        input.value(QStringLiteral("Save/picturesRoot")).toString(), m_paths.defaultPicturesRoot, error, QStringLiteral("picturesRoot"));
    if (candidate->picturesRoot.isEmpty()) return false;
    candidate->videosRoot = normalizedRoot(
        input.value(QStringLiteral("Save/videosRoot")).toString(), m_paths.defaultVideosRoot, error, QStringLiteral("videosRoot"));
    if (candidate->videosRoot.isEmpty()) return false;
    return validateShortcuts(candidate->configuredShortcuts, error);
}

bool AppSettings::validateShortcuts(const QVariantMap &shortcuts, QString *error) const {
    struct BoundSequence { QString action; QString value; QString context; };
    QList<BoundSequence> bindings;
    for (const auto &definition : shortcutDefinitions) {
        const QString action = QString::fromLatin1(definition.name);
        const QStringList values = shortcuts.value(action).toStringList();
        QSet<QString> seen;
        for (const QString &value : values) {
            const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
            if (value.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown
                || sequence.toString(QKeySequence::PortableText).isEmpty()) {
                if (error) *error = QStringLiteral("Shortcuts/%1 has unsupported key syntax: '%2'. Use one key with optional modifiers.")
                    .arg(action, value);
                return false;
            }
            const QString normalized = sequence.toString(QKeySequence::PortableText);
            if (seen.contains(normalized)) {
                if (error) *error = QStringLiteral("Shortcuts/%1 repeats the same key binding '%2'.").arg(action, value);
                return false;
            }
            seen.insert(normalized);
            bindings.append({action, normalized, QString::fromLatin1(definition.context)});
        }
        if (action == QStringLiteral("globalCapture") && values.size() != 1) {
            if (error) *error = QStringLiteral("Shortcuts/globalCapture needs exactly one native system-wide key binding.");
            return false;
        }
    }

    const auto scopes = [](const QString &context) {
        if (context == QStringLiteral("editorBase"))
            return QStringList{QStringLiteral("editorAnnotate"), QStringLiteral("editorArrange")};
        if (context == QStringLiteral("global"))
            return QStringList{QStringLiteral("global")};
        return QStringList{context};
    };
    for (int i = 0; i < bindings.size(); ++i) {
        for (int j = i + 1; j < bindings.size(); ++j) {
            if (bindings[i].action == bindings[j].action || !sameSequence(bindings[i].value, bindings[j].value)) continue;
            const QStringList left = scopes(bindings[i].context);
            const QStringList right = scopes(bindings[j].context);
            bool overlaps = left.contains(QStringLiteral("global")) || right.contains(QStringLiteral("global"));
            for (const QString &context : left)
                if (right.contains(context)) overlaps = true;
            if (overlaps) {
                if (error) *error = QStringLiteral("Shortcuts/%1 conflicts with Shortcuts/%2 at %3 in an overlapping action context.")
                    .arg(bindings[i].action, bindings[j].action, bindings[i].value);
                return false;
            }
        }
    }
    return true;
}

void AppSettings::publish(const Candidate &candidate) {
    m_shortcuts.clear();
    m_shortcutHints.clear();
    for (auto it = candidate.configuredShortcuts.cbegin(); it != candidate.configuredShortcuts.cend(); ++it) {
        QStringList sequences;
        QStringList labels;
        for (const QString &configured : it.value().toStringList()) {
            const QKeySequence sequence = qtSequence(configured);
            sequences.append(sequence.toString(QKeySequence::PortableText));
            labels.append(displaySequence(sequence));
        }
        m_shortcuts.insert(it.key(), sequences);
        m_shortcutHints.insert(it.key(), labels.join(QStringLiteral(" / ")));
    }
    m_goodColor = candidate.goodColor;
    m_badColor = candidate.badColor;
    m_picturesRoot = candidate.picturesRoot;
    m_videosRoot = candidate.videosRoot;
    emit settingsChanged();
}

bool AppSettings::reloadNow() {
    Candidate candidate;
    QString error;
    if (!parseCandidate(&candidate, &error)) {
        setError(error);
        watchFiles();
        emit reloadFailed(error);
        return false;
    }
    if (m_hotkey) {
        const QString shortcut = candidate.configuredShortcuts.value(QStringLiteral("globalCapture")).toStringList().value(0);
        if (!m_hotkey->setShortcut(qtSequence(shortcut), &error)) {
            const QString message = QStringLiteral("Could not activate Shortcuts/globalCapture (%1): %2. The previous capture shortcut and all other settings remain active.")
                .arg(shortcut, error);
            setError(message);
            watchFiles();
            emit reloadFailed(message);
            return false;
        }
    }
    publish(candidate);
    setError(QString());
    watchFiles();
    emit reloaded();
    return true;
}

void AppSettings::attachGlobalHotkey(GlobalHotkey *hotkey) {
    m_hotkey = hotkey;
    if (!m_hotkey) return;
    const QString wanted = m_shortcuts.value(QStringLiteral("globalCapture")).toStringList().value(0);
    const QString wantedHint = m_shortcutHints.value(QStringLiteral("globalCapture")).toString();
    QString error;
    if (m_hotkey->setShortcut(QKeySequence::fromString(wanted, QKeySequence::PortableText), &error)) return;
    Candidate fallback = defaults();
    QString fallbackError;
    const QString safe = fallback.configuredShortcuts.value(QStringLiteral("globalCapture")).toStringList().value(0);
    m_hotkey->setShortcut(qtSequence(safe), &fallbackError);
    publish(fallback);
    const QString message = QStringLiteral("The configured capture shortcut (%1) could not be registered: %2. Snitt restored its default shortcut; correct Shortcuts/globalCapture in %3.")
        .arg(wantedHint, error, QDir::toNativeSeparators(m_paths.settingsFile));
    setError(message);
    emit reloadFailed(message);
}

void AppSettings::watchFiles() {
    const QStringList files = m_watcher.files();
    const QStringList directories = m_watcher.directories();
    if (!files.isEmpty()) m_watcher.removePaths(files);
    if (!directories.isEmpty()) m_watcher.removePaths(directories);
    const QFileInfo file(m_paths.settingsFile);
    if (file.exists()) m_watcher.addPath(file.absoluteFilePath());
    // Atomic replacement drops the file watch; also watch its containing
    // directory and one ancestor so deletion/recreation is detectable. Keep
    // this scoped to the settings subtree to avoid noisy notifications from
    // broad roots such as the temporary or home directory.
    QDir directory = file.absoluteDir();
    int watches = 0;
    while (watches < 2) {
        if (directory.exists()) {
            m_watcher.addPath(directory.absolutePath());
            ++watches;
        }
        const QString parent = directory.absolutePath();
        if (!directory.cdUp() || directory.absolutePath() == parent) break;
    }
}

void AppSettings::pollSettingsFile() {
    const bool exists = QFileInfo::exists(m_paths.settingsFile);
    QByteArray contents;
    if (exists) {
        QFile file(m_paths.settingsFile);
        if (!file.open(QIODevice::ReadOnly)) return;
        contents = file.readAll();
        if (file.error() != QFileDevice::NoError) return;
    }

    const bool changed = m_hasObservedSettingsFile
        && (exists != m_observedSettingsFile || contents != m_observedSettingsContents);
    m_hasObservedSettingsFile = true;
    m_observedSettingsFile = exists;
    m_observedSettingsContents = contents;
    // QFileSystemWatcher implementations can miss in-place changes on some
    // platforms. The settings file is small, so byte comparison provides a
    // portable fallback for edits as well as delete/recreate cycles.
    if (changed) scheduleReload();
}

void AppSettings::scheduleReload() { m_reloadTimer.start(); }

void AppSettings::setError(const QString &message) {
    if (m_lastError == message) return;
    m_lastError = message;
    emit errorChanged();
}
