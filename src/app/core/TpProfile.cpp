// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TpProfile.h"

#include <QFile>
#include <QRegularExpression>

#include "core/FileSafety.h"

namespace rg {

namespace {

const QRegularExpression& blockRe() {
    static const QRegularExpression re(QStringLiteral("<FieldInformation>(.*?)</FieldInformation>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    return re;
}

QString inner(const QString& block, const QString& tag) {
    const QRegularExpression re(QStringLiteral("<%1>(.*?)</%1>").arg(QRegularExpression::escape(tag)),
                                QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(block);
    return m.hasMatch() ? TpProfile::xmlUnescape(m.captured(1).trimmed()) : QString();
}

}  // namespace

QString TpProfile::xmlEscape(const QString& s) {
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    o.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    o.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return o;
}

QString TpProfile::xmlUnescape(const QString& s) {
    QString o = s;
    o.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    o.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    o.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    o.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    o.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return o;
}

bool TpProfile::load(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    if (f.size() > 8 * 1024 * 1024) {
        if (err) *err = QStringLiteral("profile too large");
        return false;
    }
    QByteArray b = f.readAll();
    if (b.startsWith("\xEF\xBB\xBF")) b.remove(0, 3);  // BOM (utf-8-sig)
    path_ = path;
    text_ = QString::fromUtf8(b);
    return text_.contains(QStringLiteral("<GameProfile"));
}

bool TpProfile::loadText(const QString& text) {
    text_ = text;
    return text_.contains(QStringLiteral("<GameProfile"));
}

bool TpProfile::save(QString* err) { return saveAs(path_, err); }

bool TpProfile::saveAs(const QString& path, QString* err) {
    if (path.isEmpty()) {
        if (err) *err = QStringLiteral("no path");
        return false;
    }
    if (!writeTextAtomic(path, text_.toUtf8(), err, QStringLiteral(".ravengun-bak"))) return false;
    path_ = path;
    return true;
}

QString TpProfile::tag(const QString& name) const { return inner(text_, name); }

QList<TpField> TpProfile::fields() const {
    QList<TpField> out;
    auto it = blockRe().globalMatch(text_);
    while (it.hasNext()) {
        const QString b = it.next().captured(1);
        TpField f;
        f.category = inner(b, QStringLiteral("CategoryName"));
        f.name = inner(b, QStringLiteral("FieldName"));
        f.value = inner(b, QStringLiteral("FieldValue"));
        f.type = inner(b, QStringLiteral("FieldType"));
        f.hint = inner(b, QStringLiteral("Hint"));
        const QString mn = inner(b, QStringLiteral("FieldMin")), mx = inner(b, QStringLiteral("FieldMax"));
        f.min = mn.toInt(&f.hasMin);
        f.max = mx.toInt(&f.hasMax);
        static const QRegularExpression optsRe(QStringLiteral("<FieldOptions>(.*?)</FieldOptions>"),
                                               QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression optRe(QStringLiteral("<string>(.*?)</string>"), QRegularExpression::DotMatchesEverythingOption);
        const auto om = optsRe.match(b);
        if (om.hasMatch()) {
            auto oi = optRe.globalMatch(om.captured(1));
            while (oi.hasNext()) f.options << xmlUnescape(oi.next().captured(1).trimmed());
        }
        if (!f.name.isEmpty()) out << f;
    }
    return out;
}

QString TpProfile::fieldValue(const QString& name, const QString& category, bool* found) const {
    for (const TpField& f : fields()) {
        if (f.name == name && (category.isEmpty() || f.category == category)) {
            if (found) *found = true;
            return f.value;
        }
    }
    if (found) *found = false;
    return {};
}

bool TpProfile::hasField(const QString& name, const QString& category) const {
    bool found = false;
    fieldValue(name, category, &found);
    return found;
}

bool TpProfile::setFieldValue(const QString& name, const QString& value, const QString& category) {
    auto it = blockRe().globalMatch(text_);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString b = m.captured(1);
        if (inner(b, QStringLiteral("FieldName")) != name) continue;
        if (!category.isEmpty() && inner(b, QStringLiteral("CategoryName")) != category) continue;
        QString nb = b;
        const QString esc = xmlEscape(value);
        static const QRegularExpression valRe(QStringLiteral("<FieldValue>.*?</FieldValue>|<FieldValue\\s*/>"),
                                              QRegularExpression::DotMatchesEverythingOption);
        const auto vm = valRe.match(nb);
        if (vm.hasMatch()) {
            nb.replace(vm.capturedStart(), vm.capturedLength(), QStringLiteral("<FieldValue>%1</FieldValue>").arg(esc));
        } else {
            const int at = nb.indexOf(QStringLiteral("</FieldName>"));
            if (at < 0) return false;
            nb.insert(at + 12, QStringLiteral("<FieldValue>%1</FieldValue>").arg(esc));
        }
        text_.replace(m.capturedStart(1), m.capturedLength(1), nb);
        return true;
    }
    return false;
}

QStringList TpProfile::executableNames() const {
    QStringList out;
    for (const QString& tagName : {QStringLiteral("ExecutableName"), QStringLiteral("ExecutableName2")}) {
        const QString v = tag(tagName);
        for (QString part : v.split(QRegularExpression(QStringLiteral("[|;]")), Qt::SkipEmptyParts)) {
            part.replace(QLatin1Char('\\'), QLatin1Char('/'));
            part = part.section(QLatin1Char('/'), -1).trimmed().toLower();
            if (!part.isEmpty() && !out.contains(part)) out << part;
        }
    }
    return out;
}

}  // namespace rg
