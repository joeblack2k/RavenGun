// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameOptions.h"

#include <QCryptographicHash>
#include <QDateTime>
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
        case Kind::AdjVal:
            return section == QStringLiteral("native_layer") ? QStringLiteral("game setting \"%1\" (%2, in the game's local write layer)").arg(field, file)
                                                            : QStringLiteral("game setting \"%1\" (%2)").arg(field, file);
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
        else if (format == QStringLiteral("ini_onoff") || format == QStringLiteral("ini_yesno")) {
            t.kind = OptionTarget::Kind::Ini;
            const bool onoff = format == QStringLiteral("ini_onoff");
            t.trueWord = onoff ? QStringLiteral("ON") : QStringLiteral("YES");
            t.falseWord = onoff ? QStringLiteral("OFF") : QStringLiteral("NO");
        } else if (format == QStringLiteral("keyvalue") || format == QStringLiteral("cfg")) t.kind = OptionTarget::Kind::KeyValue;
        else if (format == QStringLiteral("tab_ini") || format == QStringLiteral("space_ini") || format == QStringLiteral("colon")) {
            t.kind = OptionTarget::Kind::KeyValue;
            t.sep = format == QStringLiteral("tab_ini") ? QLatin1Char('\t') : format == QStringLiteral("space_ini") ? QLatin1Char(' ') : QLatin1Char(':');
        } else if (format == QStringLiteral("rtdt_adjval")) t.kind = OptionTarget::Kind::AdjVal;
        else t.readOnlyReason = QStringLiteral("file format \"%1\" is not editable here").arg(format);
        if (t.kind == OptionTarget::Kind::AdjVal && base == QStringLiteral("native_layer")) t.section = base;
        else if (!base.isEmpty() && base != QStringLiteral("game_dir")) {
            if (base == QStringLiteral("view_dir") || base == QStringLiteral("tp_dir") || base == QStringLiteral("home")) t.file = QLatin1Char('{') + base + QStringLiteral("}/") + t.file;
            else t.readOnlyReason = QStringLiteral("location \"%1\" is not supported yet").arg(base);
        }
        const QVariant outside = m.value(QStringLiteral("edit_outside_game"));
        if (outside.isValid() && !outside.toBool()) t.readOnlyReason = QStringLiteral("change it in the game's own test menu");
        if (!t.readOnlyReason.isEmpty()) t.kind = OptionTarget::Kind::Unsupported;
    }
    if (t.field.isEmpty() && t.kind != OptionTarget::Kind::Unsupported) t.kind = OptionTarget::Kind::Unsupported;
    if ((t.kind == OptionTarget::Kind::Ini || t.kind == OptionTarget::Kind::KeyValue || t.kind == OptionTarget::Kind::AdjVal) && t.file.isEmpty())
        t.kind = OptionTarget::Kind::Unsupported;
    return t;
}

bool GameOptionsDb::loadGame(const QString& game, const QString& file, QString* err) {
    removeGame(game);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("%1: %2").arg(file, f.errorString());
        return false;
    }
    return loadGameData(game, f.readAll(), err, file);
}

bool GameOptionsDb::loadGameData(const QString& game, const QByteArray& json, QString* err, const QString& file) {
    removeGame(game);
    const QString name = file.isEmpty() ? game + QStringLiteral("/options.json") : file;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QStringLiteral("%1: %2").arg(name, pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not a JSON object"));
        return false;
    }
    const QVariantMap m = doc.object().toVariantMap();
    const QString id = game;
    GameOptionsEntry e;
    e.profile = game;
    e.file = file;
    e.title = firstString(m, {"title", "name_en", "name"});
    e.nasFolder = firstString(m, {"nas_folder", "folder"});
    e.platform = firstString(m, {"platform"});
    e.exe = firstString(m, {"exe"});
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
        if (o.type == QStringLiteral("bool") && !o.target.trueWord.isEmpty() && !om.contains(QStringLiteral("true_value"))) {
            o.trueValue = o.target.trueWord;
            o.falseValue = o.target.falseWord;
            if (def.isValid()) o.defaultValue = valueToText(def, o);
        }
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
    return true;
}

void GameOptionsDb::removeGame(const QString& game) {
    entries_.removeIf([&](const GameOptionsEntry& e) { return e.profile == game; });
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

QString GameContext::userGameIni() const {
    if (layers.user.empty() || profile.isEmpty()) return {};
    return QString::fromStdString(layers.user) + QLatin1Char('/') + profile + QStringLiteral("/game.ini");
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
    // De lokale schrijflaag van de native route (scripts/play-native.sh: de overlay van de eerste mount, meestal de gamemap).
    if (s.contains(QStringLiteral("{native_layer}")))
        s.replace(QStringLiteral("{native_layer}"), profile.isEmpty() ? QString() : AppPaths::dataDir() + QStringLiteral("/native/") + profile + QStringLiteral("/0/upper"));
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

namespace {

namespace gs = ravengun::gamesettings;

bool isProfileKind(OptionTarget::Kind k) {
    return k == OptionTarget::Kind::TpField || k == OptionTarget::Kind::TpTag || k == OptionTarget::Kind::Env;
}

// Waar een doel van een optie staat in game.ini van een game met een eigen profiel: sectie, sleutel en hoe de waarde omgezet wordt.
struct SettingRef {
    std::string section, key;
    QString kind;   // "" | bool (profielveld true/false) | bool1 / boolT (omgevingsvariabele 1 of true) | runner
    bool ok = false;
};

SettingRef settingRefFor(const OptionTarget& t, const gs::Ini& settings) {
    SettingRef r;
    if (t.kind == OptionTarget::Kind::TpField) {
        r.key = gs::find_config_key(settings, t.field.toStdString(), t.category.toStdString());
        r.section = "Config";
        r.ok = !r.key.empty();
    } else if (t.kind == OptionTarget::Kind::TpTag) {
        if (const gs::ProfileField* f = gs::profile_field_for_xml(t.field.toStdString())) {
            r.section = f->section;
            r.key = f->key;
            r.kind = QString::fromLatin1(f->kind);
            r.ok = true;
        }
    } else if (t.kind == OptionTarget::Kind::Env) {
        if (const gs::EnvMapEntry* e = gs::env_for_var(t.field.toStdString())) {
            r.section = e->section;
            r.key = e->key;
            r.kind = QString::fromLatin1(e->kind);
        } else {
            r.section = "Env";
            r.key = t.field.toStdString();
        }
        r.ok = true;
    }
    return r;
}

QString runnerToIni(const QString& path) {
    if (path.isEmpty()) return {};
    const QString clean = QDir::cleanPath(path);
    if (clean == QDir::cleanPath(AppPaths::dataDir() + QStringLiteral("/wrappers/wine-prefix"))) return QStringLiteral("wrapper");
    if (clean == QDir::cleanPath(AppPaths::dataDir() + QStringLiteral("/wine/proton-11.0-2/bin/wine"))) return {};
    const QString home = AppPaths::homeDir();
    return !home.isEmpty() && clean.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + clean.mid(home.size()) : clean;
}

QString runnerFromIni(const QString& v) {
    if (v.isEmpty()) return {};
    return v == QStringLiteral("wrapper") ? AppPaths::dataDir() + QStringLiteral("/wrappers/wine-prefix") : AppPaths::expandHome(v);
}

bool truthyText(const QString& v) {
    const QString l = v.trimmed().toLower();
    return l == QStringLiteral("true") || l == QStringLiteral("1") || l == QStringLiteral("yes") || l == QStringLiteral("on");
}

// Waarde in game.ini -> opslagvorm van de optie.
QString iniToOption(const SettingRef& r, const QString& v, const GameOption& o) {
    if (r.kind == QStringLiteral("bool1")) return truthyText(v) ? o.trueValue : o.falseValue;
    if (r.kind == QStringLiteral("boolT")) return truthyText(v) ? QStringLiteral("true") : QStringLiteral("false");
    if (r.kind == QStringLiteral("runner")) return runnerFromIni(v);
    return v;
}

// Opslagvorm van de optie -> waarde in game.ini. Een bool-omgevingsvariabele die uit staat is "niet gezet" (bool1: de lege waarde heft ook een
// meegeleverde "true" op) of false (boolT).
QString optionToIni(const SettingRef& r, const QString& v, const GameOption& o) {
    if (r.kind == QStringLiteral("bool1")) return v == o.trueValue ? QStringLiteral("true") : QString();
    if (r.kind == QStringLiteral("boolT")) return v == QStringLiteral("true") || v == o.trueValue ? QStringLiteral("true") : QStringLiteral("false");
    if (r.kind == QStringLiteral("runner")) return runnerToIni(v);
    return v;
}

gs::Game mergedOf(const GameContext& c) { return gs::load_game(c.layers, c.profile.toStdString()); }

}  // namespace

QString OptionStore::readSetting(const GameContext& c, const QString& section, const QString& key, bool* present) {
    const gs::Game g = mergedOf(c);
    const std::string* v = g.settings.find(section.toStdString(), key.toStdString());
    if (present) *present = v != nullptr;
    return v ? QString::fromStdString(*v) : QString();
}

bool OptionStore::writeSetting(const GameContext& c, const QString& section, const QString& key, const QString& value, QString* err,
                              const QString& implicitDefault) {
    if (c.layers.user.empty() || c.profile.isEmpty()) {
        if (err) *err = QStringLiteral("No user folder for game settings.");
        return false;
    }
    // Alleen verschillen met de meegeleverde laag: gelijk aan de meegeleverde waarde = de sleutel uit de gebruikerslaag weghalen.
    const gs::Game shipped = gs::load_game(gs::Dirs{c.layers.shipped, {}}, c.profile.toStdString());
    const std::string* base = shipped.settings.find(section.toStdString(), key.toStdString());
    const std::string baseValue = base ? *base : implicitDefault.toStdString();
    std::string e;
    const bool ok = value.toStdString() == baseValue
                        ? gs::erase_user_value(c.layers, c.profile.toStdString(), section.toStdString(), key.toStdString(), &e)
                        : gs::set_user_value(c.layers, c.profile.toStdString(), section.toStdString(), key.toStdString(), value.toStdString(), &e);
    if (!ok && err) *err = QString::fromStdString(e);
    return ok;
}

QString OptionStore::targetFile(const OptionTarget& t, const GameContext& c) {
    if (c.ownProfile && isProfileKind(t.kind)) return c.userGameIni();
    switch (t.kind) {
        case OptionTarget::Kind::TpField:
        case OptionTarget::Kind::TpTag: return c.userProfilePath();
        case OptionTarget::Kind::Env: return c.envPath();
        case OptionTarget::Kind::Ini:
        case OptionTarget::Kind::KeyValue: return c.resolve(t.file);
        case OptionTarget::Kind::AdjVal: {
            const QStringList f = GameOptionsDb::expandBraces(t.file);
            if (f.isEmpty()) return {};
            return t.section == QStringLiteral("native_layer") ? QDir::cleanPath(c.expand(QStringLiteral("{native_layer}/") + f.first())) : c.resolve(f.first());
        }
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

// --- AdjVal (rtdt_adjval): [u32 lengte][SHA-1 van de tekst][u32 lengte][tekst, met een NUL erachter], little-endian. Gemeten op
// Jurassic Park (pmuser/adj/save/playerCost/freeplay.0.txt): de 20 bytes zijn sha1 over de tekst inclusief de NUL.
bool adjNative(const OptionTarget& t) { return t.section == QStringLiteral("native_layer"); }
QStringList adjFiles(const OptionTarget& t, const GameContext& c, bool layer) {
    QStringList out;
    for (const QString& f : GameOptionsDb::expandBraces(t.file)) {
        const QString p = layer ? c.expand(QStringLiteral("{native_layer}/") + f) : c.resolve(f);
        if (!p.isEmpty() && !p.startsWith(QStringLiteral("{native_layer}"))) out << QDir::cleanPath(p);
    }
    return out;
}
quint32 adjU32(const QByteArray& d, int at) {
    return quint32(uchar(d[at])) | quint32(uchar(d[at + 1])) << 8 | quint32(uchar(d[at + 2])) << 16 | quint32(uchar(d[at + 3])) << 24;
}
// De tekst van een kopie, alleen als de controlesom klopt (een kapotte kopie slaat de game ook over).
bool adjParse(const QByteArray& d, QString* text) {
    if (d.size() < 8) return false;
    const quint32 hl = adjU32(d, 0);
    if (hl == 0 || hl > 64 || d.size() < qsizetype(8 + hl)) return false;
    const quint32 tl = adjU32(d, int(4 + hl));
    if (d.size() < qsizetype(8 + hl + tl)) return false;
    const QByteArray t = d.mid(qsizetype(8 + hl), qsizetype(tl));
    if (QCryptographicHash::hash(t, QCryptographicHash::Sha1) != d.mid(4, qsizetype(hl))) return false;
    *text = QString::fromLatin1(t).remove(QChar(0));
    return true;
}
QString adjField(const QString& text, const QString& key, bool* found = nullptr) {
    const QRegularExpression re(QStringLiteral("^\\s*%1\\s*=\\s*(.*?)\\s*$").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption);
    const auto m = re.match(text);
    if (found) *found = m.hasMatch();
    return m.hasMatch() ? m.captured(1) : QString();
}
// Lezen: de eerste kopie die klopt; de schrijflaag gaat voor de gamemap.
bool adjRead(const OptionTarget& t, const GameContext& c, QString* text) {
    QStringList kand;
    if (adjNative(t)) kand << adjFiles(t, c, true);
    kand << adjFiles(t, c, false);
    for (const QString& p : kand) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly) && adjParse(f.readAll(), text)) return true;
    }
    return false;
}
QByteArray adjBuild(const QString& name, const QString& version, const QString& value) {
    const QByteArray t = QStringLiteral("# RTDT Data Type AdjValStore\r\nname = %1\r\nversion = %2\r\ntime_set = %3\r\nval = %4\r\n")
                             .arg(name, version, QString::number(QDateTime::currentSecsSinceEpoch()), value)
                             .toLatin1() +
                         QByteArray(1, '\0');
    const QByteArray h = QCryptographicHash::hash(t, QCryptographicHash::Sha1);
    auto u32 = [](quint32 v) { QByteArray b(4, '\0'); for (int i = 0; i < 4; ++i) b[i] = char((v >> (8 * i)) & 0xff); return b; };
    return u32(quint32(h.size())) + h + u32(quint32(t.size())) + t;
}
}  // namespace

QString OptionStore::read(const GameOption& o, const GameContext& c, bool* present, QString* err) {
    if (present) *present = false;
    if (c.ownProfile && isProfileKind(o.target.kind)) {
        const gs::Game g = mergedOf(c);
        const SettingRef r = settingRefFor(o.target, g.settings);
        if (!r.ok) {
            if (err) *err = QStringLiteral("field \"%1\" is not in the profile of this game").arg(o.target.field);
            return o.defaultValue;
        }
        const std::string* v = g.settings.find(r.section, r.key);
        // Een profielveld dat het profiel kent is altijd aanwezig (leeg = lege waarde); een bool-omgevingsvariabele zonder waarde is uit.
        const bool isBoolEnv = r.kind == QStringLiteral("bool1") || r.kind == QStringLiteral("boolT");
        const bool found = v != nullptr || r.section == "Config" || isBoolEnv;
        if (present) *present = found;
        if (!found) return o.defaultValue;
        return iniToOption(r, v ? QString::fromStdString(*v) : QString(), o);
    }
    if (o.target.kind == OptionTarget::Kind::AdjVal) {
        QString text;
        bool found = false;
        const QString v = adjRead(o.target, c, &text) ? adjField(text, QStringLiteral("val"), &found) : QString();
        if (present) *present = found;
        return found ? v : o.defaultValue;
    }
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
        else v = keyvalue::get(text, o.target.field, &found, o.target.sep);
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
    if (c.ownProfile && isProfileKind(o.target.kind)) {
        const QString ini = c.userGameIni();
        if (ini.isEmpty()) {
            if (reason) *reason = QStringLiteral("no user folder for game settings");
            return false;
        }
        if (o.target.kind == OptionTarget::Kind::TpField && !settingRefFor(o.target, mergedOf(c).settings).ok) {
            if (reason) *reason = QStringLiteral("not in the profile of this game");
            return false;
        }
        const WriteCheck w = checkWritable(ini);
        if (!w.ok && reason) *reason = w.reason.isEmpty() ? QStringLiteral("read-only") : w.reason;
        return w.ok;
    }
    if (o.target.kind == OptionTarget::Kind::AdjVal) {
        const QStringList kopie = adjFiles(o.target, c, adjNative(o.target));
        if (kopie.isEmpty()) {
            if (reason) *reason = QStringLiteral("game folder unknown");
            return false;
        }
        for (const QString& p : kopie) {
            const WriteCheck w = checkWritable(p);
            if (!w.ok || w.needsOverlay) {
                if (reason) *reason = QStringLiteral("read-only (on NAS)");
                return false;
            }
        }
        return true;
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
    if (c.ownProfile && isProfileKind(o.target.kind)) {
        const gs::Game g = mergedOf(c);
        const SettingRef r = settingRefFor(o.target, g.settings);
        if (!r.ok) {
            if (err) *err = QStringLiteral("field \"%1\" is not in the profile of this game").arg(o.target.field);
            return false;
        }
        bool done = writeSetting(c, QString::fromStdString(r.section), QString::fromStdString(r.key), optionToIni(r, value, o), err);
        // Een bool-omgevingsvariabele met een tweede variabele erbij (TP_GAMESCOPE met TP_GAMESCOPE_PATH): die gaat mee of weg.
        const int eq = o.target.companion.indexOf(QLatin1Char('='));
        if (done && o.target.kind == OptionTarget::Kind::Env && eq > 0) {
            OptionTarget ct;
            ct.kind = OptionTarget::Kind::Env;
            ct.field = o.target.companion.left(eq);
            const SettingRef cr = settingRefFor(ct, g.settings);
            done = writeSetting(c, QString::fromStdString(cr.section), QString::fromStdString(cr.key),
                                value == o.trueValue ? o.target.companion.mid(eq + 1) : QString(), err);
        }
        return done;
    }
    if (o.target.kind == OptionTarget::Kind::AdjVal) {
        // Alle kopieen dezelfde nieuwe inhoud; naam en versie uit de bestaande instelling (schrijflaag of gamemap), anders de sleutel.
        QString oud;
        const bool had = adjRead(o.target, c, &oud);
        bool f = false;
        const QString name = had ? adjField(oud, QStringLiteral("name"), &f) : QString();
        const QString version = had ? adjField(oud, QStringLiteral("version")) : QString();
        const QByteArray data = adjBuild(f && !name.isEmpty() ? name : o.target.field, version.isEmpty() ? QStringLiteral("0") : version, value.trimmed());
        for (const QString& p : adjFiles(o.target, c, adjNative(o.target))) {
            if (!QDir().mkpath(QFileInfo(p).path())) {
                if (err) *err = QStringLiteral("cannot create %1").arg(QFileInfo(p).path());
                return false;
            }
            if (!writeTextAtomic(p, data, err)) return false;
        }
        return true;
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
        out = keyvalue::set(text, o.target.field, value, o.target.sep);
    }
    if (ok && out == text) return true;
    return writeTextAtomic(file, out.toUtf8(), err, ok ? QStringLiteral(".ravengun-bak") : QString());
}

}  // namespace rg
