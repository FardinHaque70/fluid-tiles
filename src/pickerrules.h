// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace Fluid {
// Picked properties are literal values; hand-written rules may still use globs.
inline QString literalWindowRule(const QString &value) {
    if (value.trimmed().isEmpty() || value.contains('\n') || value.contains('\r')) return {};
    QString rule;
    for (const auto ch : value.trimmed()) {
        if (ch==u'*') rule+="[*]";
        else if (ch==u'?') rule+="[?]";
        else if (ch==u'[') rule+="[[]";
        else if (ch==u']') rule+="[]]";
        else rule+=ch;
    }
    return rule;
}
inline QString appendWindowRule(const QString &text,const QString &value) {
    const auto rule=literalWindowRule(value);
    if (rule.isEmpty()) return text;
    for (const auto &line : text.split('\n'))
        if (line.trimmed().compare(rule,Qt::CaseInsensitive)==0) return text;
    return text+(text.isEmpty() || text.endsWith('\n') ? QString{} : QStringLiteral("\n"))+rule;
}
}
