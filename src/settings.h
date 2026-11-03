// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QRegularExpression>
#include <QString>
#include <QVector>

namespace Fluid {
// Krohnkite res/config.xml at 177040c6: KDE integration defaults.
inline const QString defaultIgnoredApplications=QStringLiteral(
    "krunner\nyakuake\nspectacle\nkded5\nxwaylandvideobridge\nplasmashell\nksplashqml\n"
    "org.kde.plasmashell\norg.kde.polkit-kde-authentication-agent-1\norg.kde.kruler\n"
    "kruler\nkwin_wayland\nksmserver-logout-greeter");
inline const QString defaultIgnoredRoles=QStringLiteral("quake");
// Window identifiers and titles are not paths: '*' also matches slashes.
// Whole-field, case-insensitive globs, one rule per line; no regex syntax.
class IgnorePatterns {
public:
    void setText(const QString &text) {
        m_patterns.clear();
        for (const auto &line : text.split('\n')) {
            const auto pattern=line.trimmed();
            if (pattern.isEmpty()) continue;
            m_patterns.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(
                pattern,QRegularExpression::NonPathWildcardConversion),QRegularExpression::CaseInsensitiveOption));
        }
    }
    bool matches(const QString &value) const {
        if (value.isEmpty()) return false;
        for (const auto &pattern : m_patterns) if (pattern.match(value).hasMatch()) return true;
        return false;
    }
    bool empty() const { return m_patterns.isEmpty(); }
private:
    QVector<QRegularExpression> m_patterns;
};
}
