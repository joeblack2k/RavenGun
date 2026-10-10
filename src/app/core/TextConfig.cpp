// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TextConfig.h"

#include <QStringList>

namespace rg {

namespace {

QStringList splitLines(const QString& text, QString* eol) {
    *eol = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QStringList lines = text.split(*eol);
    return lines;
}

bool isComment(const QString& t) { return t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1Char(';')); }

bool sectionHeader(const QString& t, QString* name) {
    if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
        *name = t.mid(1, t.size() - 2).trimmed();
        return true;
    }
    return false;
}

bool keyLine(const QString& t, const QString& key, QChar sep, QString* value) {
    if (t.isEmpty() || isComment(t)) return false;
    const int at = t.indexOf(sep);
    if (at <= 0) return false;
    if (t.left(at).trimmed().compare(key, Qt::CaseInsensitive) != 0) return false;
    if (value) *value = t.mid(at + 1).trimmed();
    return true;
}

}  // namespace

namespace ini {

QString get(const QString& text, const QString& section, const QString& key, bool* found) {
    QString eol;
    const QStringList lines = splitLines(text, &eol);
    QString cur;
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        QString name;
        if (sectionHeader(t, &name)) { cur = name; continue; }
        QString v;
        if (cur.compare(section, Qt::CaseInsensitive) == 0 && keyLine(t, key, QLatin1Char('='), &v)) {
            if (found) *found = true;
            return v;
        }
    }
    if (found) *found = false;
    return {};
}

QString set(const QString& text, const QString& section, const QString& key, const QString& value) {
    QString eol;
    QStringList lines = splitLines(text, &eol);
    QString cur;
    int sectionEnd = section.isEmpty() ? 0 : -1;  // index na de laatste regel van de juiste sectie
    bool inSection = section.isEmpty();
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        QString name;
        if (sectionHeader(t, &name)) {
            cur = name;
            inSection = cur.compare(section, Qt::CaseInsensitive) == 0;
            if (inSection) sectionEnd = i + 1;
            continue;
        }
        if (!inSection) continue;
        if (keyLine(t, key, QLatin1Char('='), nullptr)) {
            const int at = lines[i].indexOf(QLatin1Char('='));
            lines[i] = lines[i].left(at + 1) + value;
            return lines.join(eol);
        }
        if (!t.isEmpty()) sectionEnd = i + 1;
    }
    const QString line = key + QLatin1Char('=') + value;
    if (sectionEnd >= 0) {
        lines.insert(sectionEnd, line);
    } else {
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
        if (!lines.isEmpty()) lines << QString();
        lines << QStringLiteral("[%1]").arg(section) << line << QString();
    }
    return lines.join(eol);
}

}  // namespace ini

namespace keyvalue {

QString get(const QString& text, const QString& key, bool* found, QChar sep) {
    QString eol;
    for (const QString& l : splitLines(text, &eol)) {
        QString v;
        if (keyLine(l.trimmed(), key, sep, &v)) {
            if (found) *found = true;
            return v;
        }
    }
    if (found) *found = false;
    return {};
}

QString set(const QString& text, const QString& key, const QString& value, QChar sep) {
    QString eol;
    QStringList lines = splitLines(text, &eol);
    for (QString& l : lines) {
        if (keyLine(l.trimmed(), key, sep, nullptr)) {
            l = key + sep + value;
            return lines.join(eol);
        }
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    lines << key + sep + value << QString();
    return lines.join(eol);
}

QString remove(const QString& text, const QString& key, QChar sep) {
    QString eol;
    QStringList lines = splitLines(text, &eol), out;
    for (const QString& l : lines)
        if (!keyLine(l.trimmed(), key, sep, nullptr)) out << l;
    return out.join(eol);
}

}  // namespace keyvalue

}  // namespace rg
