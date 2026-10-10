// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameOptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "core/AppPaths.h"
#include "core/FileSafety.h"
#include "core/TextConfig.h"
#include "core/TpProfile.h"

namespace rg {

namespace {

QString firstString(const QVariantMap& m, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const auto it = m.find(QLatin1String(k));
        if (it != m.end() && !it->isNull() && !it->toString().isEmpty()) return it->toString();
    }
    return {};
}

QVariant firstValue(const QVariantMap& m, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        const auto it = m.find(QLatin1String(k));
        if (it != m.end() && it->isValid() && !it->isNull()) return *it;
    }
    return {};
}

QString normType(QString t) {
    t = t.trimmed().toLower();
    if (t == QStringLiteral("boolean") || t == QStringLiteral("checkbox")) return QStringLiteral("bool");
    if (t == QStringLiteral("integer") || t == QStringLiteral("number") || t == QStringLiteral("slider") ||
        t == QStringLiteral("spin") || t == QStringLiteral("range"))
        return QStringLiteral("int");
    if (t == QStringLiteral("dropdown") || t == QStringLiteral("choice") || t == QStringLiteral("select") ||
        t == QStringLiteral("combo"))
        return QStringLiteral("enum");
    if (t == QStringLiteral("text") || t == QStringLiteral("str")) return QStringLiteral("string");
    return t;
}

QString valueToText(const QVariant& v, const GameOption& o) {
    if (v.typeId() == QMetaType::Bool) return v.toBool() ? o.trueValue : o.falseValue;
    if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong) {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<long long>(d))) return QString::number(static_cast<long long>(d));
        return QString::number(d);
    }
    return v.toString();
}

}  // namespace

QString OptionTarget::describe() const {
    switch (kind) {
        case Kind::TpField:
            return category.isEmpty() ? QStringLiteral("launch profile field \"%1\"").arg(field)
                                      : QStringLiteral("launch profile field \"%1 / %2\"").arg(category, field);
        case Kind::TpTag: return QStringLiteral("launch profile setting <%1>").arg(field);
        case Kind::Env: return QStringLiteral("environment variable %1 (UserProfiles/<profile>.env)").arg(field);
        case Kind::Ini:
            return section.isEmpty() ? QStringLiteral("%1: %2").arg(file, field) : QStringLiteral("%1: [%2] %3").arg(file, section, field);
        case Kind::KeyValue: return QStringLiteral("%1: %2").arg(file, field);
        case Kind::Unsupported: break;
    }
    return QStringLiteral("not editable here: %1").arg(raw);
}

QStringList GameOptionsDb::expandBraces(const QString& pattern) {
    static const QRegularExpression re(QStringLiteral("\\{([^{}]*,[^{}]*)\\}"));
    const auto m = re.match(pattern);
    if (!m.hasMatch()) return {pattern};
    QStringList out;
    for (const QString& alt : m.captured(1).split(QLatin1Char(','))) {
        QString p = pattern;
        p.replace(m.capturedStart(), m.capturedLength(), alt);
        out << expandBraces(p);
    }
    return out;
}

OptionTarget GameOptionsDb::parseTarget(const QVariant& v) {
    OptionTarget t;
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().trimmed();
        t.raw = s;
        const int colon = s.indexOf(QLatin1Char(':'));
        const QString kind = colon > 0 ? s.left(colon).toLower() : QString();
        const QString rest = colon > 0 ? s.mid(colon + 1) : s;
        if (kind == QStringLiteral("tp") || kind == QStringLiteral("profile") || kind == QStringLiteral("teknoparrot")) {
            t.kind = OptionTarget::Kind::TpField;
            const int slash = rest.indexOf(QLatin1Char('/'));
            if (slash > 0) { t.category = rest.left(slash); t.field = rest.mid(slash + 1); }
            else t.field = rest;
        } else if (kind == QStringLiteral("env")) {
            t.kind = OptionTarget::Kind::Env;
            t.field = rest;
        } else if (kind == QStringLiteral("ini") || kind == QStringLiteral("cfg") || kind == QStringLiteral("keyvalue")) {
            const int hash = rest.lastIndexOf(QLatin1Char('#'));
            if (hash <= 0) return t;
            t.file = rest.left(hash);
            const QString k = rest.mid(hash + 1);
            if (kind == QStringLiteral("ini")) {
                t.kind = OptionTarget::Kind::Ini;
                const int slash = k.lastIndexOf(QLatin1Char('/'));
                if (slash >= 0) { t.section = k.left(slash); t.field = k.mid(slash + 1); }
                else t.field = k;
            } else {
                t.kind = OptionTarget::Kind::KeyValue;
                t.field = k;
            }
        } else if (kind.isEmpty() && !rest.isEmpty()) {
            t.kind = OptionTarget::Kind::TpField;  // alleen een veldnaam
            t.field = rest;
        }
        if (t.field.isEmpty()) t.kind = OptionTarget::Kind::Unsupported;
        return t;
    }
    const QVariantMap m = v.toMap();
    if (m.isEmpty()) return t;
    t.raw = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact));
    const QString kind = firstString(m, {"kind", "type", "target"}).toLower();
    t.category = firstString(m, {"category", "category_name"});
    t.file = firstString(m, {"file", "path"});
    t.section = firstString(m, {"section"});
    const QString base = firstString(m, {"base"});
    if (kind == QStringLiteral("tp_profile_field") || kind == QStringLiteral("tp_field") || kind == QStringLiteral("tp") ||
        kind == QStringLiteral("profile") || kind == QStringLiteral("teknoparrot") || kind == QStringLiteral("tp_profile")) {
        t.kind = OptionTarget::Kind::TpField;
        t.field = firstString(m, {"field", "field_name", "name", "key"});
    } else if (kind == QStringLiteral("tp_profile_attr") || kind == QStringLiteral("tp_tag")) {
        t.kind = OptionTarget::Kind::TpTag;
        t.field = firstString(m, {"element", "tag", "name"});
    } else if (kind == QStringLiteral("launch_env") || kind == QStringLiteral("env") || kind == QStringLiteral("environment")) {
        t.kind = OptionTarget::Kind::Env;
        t.field = firstString(m, {"name", "var", "variable", "key"});
        t.companion = firstString(m, {"companion"});
        t.file.clear();  // altijd UserProfiles/<profiel>.env
    } else if (kind == QStringLiteral("game_config_file") || kind == QStringLiteral("ini") || kind == QStringLiteral("teknoparrot.ini") ||
               kind == QStringLiteral("teknoparrot_ini") || kind == QStringLiteral("tp_ini") || kind == QStringLiteral("keyvalue") ||
               kind == QStringLiteral("cfg") || kind == QStringLiteral("config")) {
        QString format = firstString(m, {"format"}).toLower();
        if (format.isEmpty()) format = (kind == QStringLiteral("keyvalue") || kind == QStringLiteral("cfg") || kind == QStringLiteral("config"))
                                           ? QStringLiteral("keyvalue")
                                           : QStringLiteral("ini");
        if (t.file.isEmpty() && kind.contains(QStringLiteral("teknoparrot")) ) t.file = QStringLiteral("teknoparrot.ini");
        if (t.file.isEmpty() && kind == QStringLiteral("tp_ini")) t.file = QStringLiteral("teknoparrot.ini");
        t.field = firstString(m, {"key", "name", "field"});
        if (format == QStringLiteral("ini")) t.kind = OptionTarget::Kind::Ini;
        else if (format == QStringLiteral("keyvalue") || format == QStringLiteral("cfg")) t.kind = OptionTarget::Kind::KeyValue;
        else t.readOnlyReason = QStringLiteral("file format \"%1\" is not editable here").arg(format);
        if (!base.isEmpty() && base != QStringLiteral("game_dir")) {
            if (base == QStringLiteral("view_dir") || base == QStringLiteral("tp_dir") || base == QStringLiteral("home")) t.file = QLatin1Char('{') + base + QStringLiteral("}/") + t.file;
            else t.readOnlyReason = QStringLiteral("location \"%1\" is not supported yet").arg(base);
        }
        const QVariant outside = m.value(QStringLiteral("edit_outside_game"));
        if (outside.isValid() && !outside.toBool()) t.readOnlyReason = QStringLiteral("change it in the game's own test menu");
        if (!t.readOnlyReason.isEmpty()) t.kind = OptionTarget::Kind::Unsupported;
    }
    if (t.field.isEmpty() && t.kind != OptionTarget::Kind::Unsupported) t.kind = OptionTarget::Kind::Unsupported;
    if ((t.kind == OptionTarget::Kind::Ini || t.kind == OptionTarget::Kind::KeyValue) && t.file.isEmpty()) t.kind = OptionTarget::Kind::Unsupported;
    return t;
}

bool GameOptionsDb::load(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    return loadData(f.readAll(), err);
}

bool GameOptionsDb::loadData(const QByteArray& json, QString* err) {
    entries_.clear();
    warnings_.clear();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QStringLiteral("game-options.json: %1").arg(pe.errorString());
        return false;
    }
    const QVariantMap root = doc.object().toVariantMap();
    QList<QPair<QString, QVariantMap>> games;
    const QVariant g = root.value(QStringLiteral("games"));
    if (g.typeId() == QMetaType::QVariantMap) {
        const QVariantMap gm = g.toMap();
        for (auto it = gm.begin(); it != gm.end(); ++it) games.append({it.key(), it->toMap()});
    } else if (g.typeId() == QMetaType::QVariantList) {
        for (const QVariant& e : g.toList()) {
            const QVariantMap m = e.toMap();
            games.append({firstString(m, {"id", "key", "profile", "tp_profile", "game"}), m});
        }
    }
    for (const auto& [id, m] : games) {
        if (id.isEmpty() || id.startsWith(QLatin1Char('_'))) continue;
        GameOptionsEntry e;
        e.id = id;
        // tp_profile kan expliciet leeg (null) zijn: de game heeft nog geen profiel.
        e.profile = m.contains(QStringLiteral("tp_profile")) ? m.value(QStringLiteral("tp_profile")).toString()
                                                             : (m.contains(QStringLiteral("profile")) ? m.value(QStringLiteral("profile")).toString() : id);
        e.title = firstString(m, {"title", "name_en", "name"});
        e.nasFolder = firstString(m, {"nas_folder", "folder"});
        e.platform = firstString(m, {"platform"});
        e.exe = firstString(m, {"exe"});
        e.route = firstString(m, {"recommended_route", "route"});
        e.note = firstString(m, {"note_en", "note", "status_en", "notes_en", "status_note", "notes"});
        for (const QVariant& ov : firstValue(m, {"options"}).toList()) {
            const QVariantMap om = ov.toMap();
            GameOption o;
            o.key = firstString(om, {"key", "id"});
            o.name = firstString(om, {"name_en", "name", "label", "title"});
            if (o.name.isEmpty()) o.name = o.key;
            o.type = normType(firstString(om, {"type"}));
            o.description = firstString(om, {"description_en", "description", "hint", "help", "tooltip"});
            const QString note = firstString(om, {"note_en", "note"});
            if (!note.isEmpty()) o.description += (o.description.isEmpty() ? QString() : QStringLiteral(" ")) + note;
            o.category = firstString(om, {"category", "group", "section_name"});
            if (om.contains(QStringLiteral("true_value"))) o.trueValue = om.value(QStringLiteral("true_value")).toString();
            if (om.contains(QStringLiteral("false_value"))) o.falseValue = om.value(QStringLiteral("false_value")).toString();
            for (const QVariant& vv : firstValue(om, {"values", "options", "choices"}).toList()) {
                if (vv.typeId() == QMetaType::QVariantMap) {
                    const QVariantMap vm = vv.toMap();
                    const QString val = valueToText(firstValue(vm, {"value", "id", "key"}), o);
                    o.values << val;
                    const QString lab = firstString(vm, {"label_en", "label", "name", "title"});
                    o.labels << (lab.isEmpty() ? val : lab);
                } else {
                    o.values << valueToText(vv, o);
                    o.labels << valueToText(vv, o);
                }
            }
            const QVariant range = firstValue(om, {"range"});
            if (range.typeId() == QMetaType::QVariantList && range.toList().size() >= 2) {
                o.hasRange = true;
                o.min = range.toList()[0].toDouble();
                o.max = range.toList()[1].toDouble();
                if (range.toList().size() >= 3) o.step = range.toList()[2].toDouble();
            } else if (range.typeId() == QMetaType::QVariantMap) {
                const QVariantMap rm = range.toMap();
                o.hasRange = rm.contains(QStringLiteral("min")) && rm.contains(QStringLiteral("max"));
                o.min = rm.value(QStringLiteral("min")).toDouble();
                o.max = rm.value(QStringLiteral("max")).toDouble();
                o.step = rm.value(QStringLiteral("step"), 1).toDouble();
            } else if (om.contains(QStringLiteral("min")) && om.contains(QStringLiteral("max"))) {
                o.hasRange = true;
                o.min = om.value(QStringLiteral("min")).toDouble();
                o.max = om.value(QStringLiteral("max")).toDouble();
                o.step = om.value(QStringLiteral("step"), 1).toDouble();
            }
            if (o.type.isEmpty()) o.type = !o.values.isEmpty() ? QStringLiteral("enum") : o.hasRange ? QStringLiteral("int") : QStringLiteral("string");
            const QVariant def = firstValue(om, {"default", "default_value"});
            if (def.isValid()) o.defaultValue = valueToText(def, o);
            o.target = parseTarget(firstValue(om, {"target", "where", "destination"}));
            if (o.key.isEmpty()) {
                warnings_ << QStringLiteral("%1: option without key skipped").arg(id);
                continue;
            }
            if (o.type != QStringLiteral("bool") && o.type != QStringLiteral("int") && o.type != QStringLiteral("enum") &&
                o.type != QStringLiteral("string")) {
                warnings_ << QStringLiteral("%1/%2: unknown type '%3', shown as text").arg(id, o.key, o.type);
                o.type = QStringLiteral("string");
            }
            if (o.type == QStringLiteral("enum") && o.values.isEmpty()) {
                warnings_ << QStringLiteral("%1/%2: enum without values, shown as text").arg(id, o.key);
                o.type = QStringLiteral("string");
            }
            if (o.target.kind == OptionTarget::Kind::Unsupported && o.target.readOnlyReason.isEmpty())
                warnings_ << QStringLiteral("%1/%2: unsupported target (%3)").arg(id, o.key, o.target.raw);
            e.options << o;
        }
        QVariant mc = firstValue(m, {"memory_card", "memory_cards", "memory-card", "memorycard", "saves", "save_files"});
        if (mc.typeId() == QMetaType::QVariantMap) {
            e.memoryNotes = firstString(mc.toMap(), {"notes_en", "notes", "note"});
            mc = mc.toMap().value(QStringLiteral("items"));
        }
        for (const QVariant& mv : mc.toList()) {
            MemoryCardSpec s;
            if (mv.typeId() == QMetaType::QString) {
                s.path = mv.toString();
            } else {
                const QVariantMap mm = mv.toMap();
                s.id = firstString(mm, {"id"});
                s.path = firstString(mm, {"path", "glob", "file", "dir"});
                s.base = firstString(mm, {"base"});
                s.label = firstString(mm, {"description_en", "label", "name", "description"});
                s.kind = firstString(mm, {"kind", "type"});
                s.eraseEffect = firstString(mm, {"erase_effect_en", "erase_effect"});
                const QVariant safe = mm.value(QStringLiteral("safe_to_erase"));
                if (safe.typeId() == QMetaType::Bool) s.safeToErase = safe.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
                else if (safe.isValid() && !safe.isNull()) s.safeToErase = safe.toString().toLower();
                if (s.kind == QStringLiteral("protected_data")) s.safeToErase = QStringLiteral("no");
                s.local = mm.value(QStringLiteral("local")).toBool();
            }
            if (!s.path.isEmpty()) e.memoryCard << s;
        }
        entries_ << e;
    }
    return true;
}

const GameOptionsEntry* GameOptionsDb::find(const QString& profile) const {
    if (profile.isEmpty()) return nullptr;
    for (const auto& e : entries_)
        if (e.profile == profile) return &e;
    for (const auto& e : entries_)
        if (!e.profile.isEmpty() && e.profile.compare(profile, Qt::CaseInsensitive) == 0) return &e;
    return nullptr;
}

const GameOptionsEntry* GameOptionsDb::findByFolder(const QString& folderName) const {
    if (folderName.isEmpty()) return nullptr;
    for (const auto& e : entries_)
        if (!e.nasFolder.isEmpty() && e.nasFolder == folderName) return &e;
    for (const auto& e : entries_)
        if (!e.nasFolder.isEmpty() && e.nasFolder.trimmed().compare(folderName.trimmed(), Qt::CaseInsensitive) == 0) return &e;
    return nullptr;
}

QString GameContext::userProfilePath() const {
    if (tpDir.isEmpty() || profile.isEmpty()) return {};
    return tpDir + QStringLiteral("/UserProfiles/") + profile + QStringLiteral(".xml");
}

QString GameContext::envPath() const {
    if (tpDir.isEmpty() || profile.isEmpty()) return {};
    return tpDir + QStringLiteral("/UserProfiles/") + profile + QStringLiteral(".env");
}

QString GameContext::expand(const QString& pattern) const {
    QString s = pattern;
    s.replace(QStringLiteral("{profile}"), profile);
    s.replace(QStringLiteral("{tp_dir}"), tpDir);
    s.replace(QStringLiteral("{game_dir}"), rootDir.isEmpty() ? gameDir : rootDir);
    s.replace(QStringLiteral("{exe_dir}"), gameDir);
    s.replace(QStringLiteral("{real_game_dir}"), realGameDir.isEmpty() ? gameDir : realGameDir);
    s.replace(QStringLiteral("{view_dir}"), viewDir);
    s.replace(QStringLiteral("{home}"), AppPaths::homeDir());
    return AppPaths::expandHome(s);
}

QString GameContext::resolve(const QString& path, const QString& base) const {
    QString p = expand(path);
    if (QDir::isAbsolutePath(p)) return QDir::cleanPath(p);
    QString dir;
    if (base.isEmpty() || base == QStringLiteral("game_dir")) dir = rootDir.isEmpty() ? gameDir : rootDir;
    else if (base == QStringLiteral("view_dir")) dir = viewDir;
    else if (base == QStringLiteral("tp_dir")) dir = tpDir;
    else if (base == QStringLiteral("home")) dir = AppPaths::homeDir();
    else return {};  // wine_prefix e.d.: (nog) niet op te lossen
    // teknoparrot.ini staat naast het uitvoerbare bestand.
    if (p.compare(QStringLiteral("teknoparrot.ini"), Qt::CaseInsensitive) == 0 && !gameDir.isEmpty()) dir = gameDir;
    if (dir.isEmpty()) return {};
    return QDir::cleanPath(dir + QLatin1Char('/') + p);
}

QString OptionStore::targetFile(const OptionTarget& t, const GameContext& c) {
    switch (t.kind) {
        case OptionTarget::Kind::TpField:
        case OptionTarget::Kind::TpTag: return c.userProfilePath();
        case OptionTarget::Kind::Env: return c.envPath();
        case OptionTarget::Kind::Ini:
        case OptionTarget::Kind::KeyValue: return c.resolve(t.file);
        case OptionTarget::Kind::Unsupported: break;
    }
    return {};
}

namespace {
QString readText(const QString& path, bool* ok) {
    QFile f(path);
    *ok = f.open(QIODevice::ReadOnly);
    return *ok ? QString::fromUtf8(f.readAll()) : QString();
}
QString setTag(QString text, const QString& tag, const QString& value) {
    const QRegularExpression re(QStringLiteral("<%1>.*?</%1>|<%1\\s*/>").arg(QRegularExpression::escape(tag)),
                                QRegularExpression::DotMatchesEverythingOption);
    const QString repl = value.isEmpty() ? QString() : QStringLiteral("<%1>%2</%1>").arg(tag, TpProfile::xmlEscape(value));
    const auto m = re.match(text);
    if (m.hasMatch()) {
        text.replace(m.capturedStart(), m.capturedLength(), repl);
        return text;
    }
    if (value.isEmpty()) return text;
    const int end = text.lastIndexOf(QStringLiteral("</GameProfile>"));
    if (end < 0) return text;
    text.insert(end, QStringLiteral("  ") + repl + QLatin1Char('\n'));
    return text;
}
}  // namespace

QString OptionStore::read(const GameOption& o, const GameContext& c, bool* present, QString* err) {
    if (present) *present = false;
    const QString file = targetFile(o.target, c);
    if (file.isEmpty()) {
        if (err) *err = QStringLiteral("target unknown");
        return o.defaultValue;
    }
    bool found = false;
    QString v;
    if (o.target.kind == OptionTarget::Kind::TpField || o.target.kind == OptionTarget::Kind::TpTag) {
        TpProfile p;
        if (!p.load(file, err)) return o.defaultValue;
        if (o.target.kind == OptionTarget::Kind::TpField) {
            v = p.fieldValue(o.target.field, o.target.category, &found);
        } else {
            v = p.tag(o.target.field);
            found = p.text().contains(QLatin1Char('<') + o.target.field);
        }
    } else {
        bool ok = false;
        const QString text = readText(file, &ok);
        if (!ok) return o.defaultValue;
        if (o.target.kind == OptionTarget::Kind::Ini) v = ini::get(text, o.target.section, o.target.field, &found);
        else v = keyvalue::get(text, o.target.field, &found);
        // Een bool in .env: afwezig = uit.
        if (o.target.kind == OptionTarget::Kind::Env && o.type == QStringLiteral("bool") && !found) {
            found = true;
            v = o.falseValue;
        }
    }
    if (present) *present = found;
    return found ? v : o.defaultValue;
}

bool OptionStore::writable(const GameOption& o, const GameContext& c, QString* reason) {
    if (o.target.kind == OptionTarget::Kind::Unsupported) {
        if (reason) *reason = o.target.readOnlyReason.isEmpty() ? QStringLiteral("not editable here") : o.target.readOnlyReason;
        return false;
    }
    const QString file = targetFile(o.target, c);
    if (file.isEmpty()) {
        if (reason) *reason = QStringLiteral("game folder unknown");
        return false;
    }
    if ((o.target.kind == OptionTarget::Kind::TpField || o.target.kind == OptionTarget::Kind::TpTag) && !QFileInfo::exists(file)) {
        if (reason) *reason = QStringLiteral("set up the game first");
        return false;
    }
    const WriteCheck w = checkWritable(file);
    if (!w.ok && reason) *reason = QStringLiteral("read-only (on NAS)");
    return w.ok;
}

bool OptionStore::write(const GameOption& o, const GameContext& c, const QString& value, QString* err) {
    QString reason;
    if (!writable(o, c, &reason)) {
        if (err) *err = reason;
        return false;
    }
    const QString file = targetFile(o.target, c);
    if (o.target.kind == OptionTarget::Kind::TpField || o.target.kind == OptionTarget::Kind::TpTag) {
        TpProfile p;
        if (!p.load(file, err)) return false;
        if (o.target.kind == OptionTarget::Kind::TpTag) {
            if (!p.loadText(setTag(p.text(), o.target.field, value))) return false;
        } else if (!p.setFieldValue(o.target.field, value, o.target.category)) {
            if (err) *err = QStringLiteral("field \"%1\" not found in %2").arg(o.target.field, QFileInfo(file).fileName());
            return false;
        }
        return p.save(err);
    }
    bool ok = false;
    const QString text = readText(file, &ok);  // ontbreekt = nieuw bestand
    QString out;
    if (o.target.kind == OptionTarget::Kind::Ini) {
        out = ini::set(text, o.target.section, o.target.field, value);
    } else if (o.target.kind == OptionTarget::Kind::Env && o.type == QStringLiteral("bool")) {
        // Bool in .env: aan = VAR=1 (plus companion), uit = weg.
        const bool on = value == o.trueValue;
        const int eq = o.target.companion.indexOf(QLatin1Char('='));
        const QString compKey = eq > 0 ? o.target.companion.left(eq) : QString();
        if (on) {
            out = keyvalue::set(text, o.target.field, value);
            if (!compKey.isEmpty()) out = keyvalue::set(out, compKey, o.target.companion.mid(eq + 1));
        } else {
            out = keyvalue::remove(text, o.target.field);
            if (!compKey.isEmpty()) out = keyvalue::remove(out, compKey);
        }
    } else {
        out = keyvalue::set(text, o.target.field, value);
    }
    if (ok && out == text) return true;
    return writeTextAtomic(file, out.toUtf8(), err, ok ? QStringLiteral(".ravengun-bak") : QString());
}

}  // namespace rg
