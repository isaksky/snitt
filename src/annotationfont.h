#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

namespace annotationfont {
enum class Platform { Windows, MacOS, Other };

inline constexpr Platform currentPlatform() {
#ifdef Q_OS_WIN
    return Platform::Windows;
#elif defined(Q_OS_MACOS)
    return Platform::MacOS;
#else
    return Platform::Other;
#endif
}

inline QString installedFamily(const QStringList &families, const QString &requested) {
    for (const QString &family : families)
        if (family.compare(requested, Qt::CaseInsensitive) == 0) return family;
    return {};
}

inline QString selectFamily(const QStringList &families, Platform platform,
                            const QString &systemFallback) {
    const QString impact = installedFamily(families, QStringLiteral("Impact"));
    if (!impact.isEmpty()) return impact;

    const QString platformFallback = platform == Platform::Windows ? QStringLiteral("Segoe UI")
        : platform == Platform::MacOS ? QStringLiteral("Helvetica") : QString();
    if (!platformFallback.isEmpty()) {
        const QString installedPlatformFallback = installedFamily(families, platformFallback);
        if (!installedPlatformFallback.isEmpty()) return installedPlatformFallback;
    }

    const QString installedSystemFallback = installedFamily(families, systemFallback);
    if (!installedSystemFallback.isEmpty()) return installedSystemFallback;
    if (!systemFallback.trimmed().isEmpty()) return systemFallback.trimmed();
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

inline QString family() {
    static const QString selected = [] {
        const QString systemFallback = QGuiApplication::font().family();
        return selectFamily(QFontDatabase::families(), currentPlatform(), systemFallback);
    }();
    return selected;
}

inline QFont make(int pixelSize) {
    QFont font(family());
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::DemiBold);
    return font;
}
}
