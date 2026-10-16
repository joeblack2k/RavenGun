// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameLibrary.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include "core/AppPaths.h"
#include "core/FileSafety.h"
#include "core/TpProfile.h"

namespace rg {

QString kindLabel(GameKind k) {
    switch (k) {
        case GameKind::TeknoParrot: return QStringLiteral("Arcade");
        case GameKind::Unity: return QStringLiteral("Unity route");
        case GameKind::Detected: return QStringLiteral("Not set up");
        case GameKind::Unconfigured: return QStringLiteral("Unconfigured");
    }
    return {};
}

QString platformLabel(const QString& emulator) {
    // EmulatorType uit het profiel naar een neutrale platformnaam voor de lijst (de ruwe waarde staat in Info).
    static const QHash<QString, QString> names{
        {QStringLiteral("TeknoParrot"), QStringLiteral("Windows PC")},   {QStringLiteral("OpenParrot"), QStringLiteral("Windows PC")},
        {QStringLiteral("ElfLdr2"), QStringLiteral("Linux PC")},          {QStringLiteral("Lindbergh"), QStringLiteral("Sega Lindbergh")},
        {QStringLiteral("RPCS3"), QStringLiteral("PlayStation 3 based")}, {QStringLiteral("pcsx2x6"), QStringLiteral("PlayStation 2 based")},
        {QStringLiteral("Dolphin"), QStringLiteral("Triforce")},          {QStringLiteral("cxbxr"), QStringLiteral("Xbox based")},
        {QStringLiteral("Play"), QStringLiteral("PlayStation 2 based")},  {QStringLiteral("SegaTools"), QStringLiteral("Sega ALLS/Nu")},
    };
    if (names.contains(emulator)) return names.value(emulator);
    if (emulator.startsWith(QStringLiteral("Tekno"))) return emulator.mid(5);
    return emulator;
}

namespace {

QString kindKey(GameKind k) {
    switch (k) {
        case GameKind::TeknoParrot: return QStringLiteral("teknoparrot");
        case GameKind::Unity: return QStringLiteral("unity");
        case GameKind::Detected: return QStringLiteral("detected");
        case GameKind::Unconfigured: return QStringLiteral("unconfigured");
    }
    return {};
}

GameKind kindFromKey(const QString& s) {
    if (s == QStringLiteral("teknoparrot")) return GameKind::TeknoParrot;
    if (s == QStringLiteral("unity")) return GameKind::Unity;
    if (s == QStringLiteral("detected")) return GameKind::Detected;
    return GameKind::Unconfigured;
}

const QRegularExpression& profileNameRe() {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return re;
}

QJsonObject readMetadata(const QString& tpDir, const QString& profile) {
    if (tpDir.isEmpty()) return {};
    return readJson(tpDir + QStringLiteral("/Metadata/") + profile + QStringLiteral(".json")).object();
}

QString iconIn(const QString& tpDir, QString iconName) {
    if (tpDir.isEmpty() || iconName.isEmpty()) return {};
    iconName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    iconName = iconName.section(QLatin1Char('/'), -1);
    const QString p = tpDir + QStringLiteral("/Icons/") + iconName;
    return QFileInfo(p).isFile() ? p : QString();
}

// Een icoon in de gamemap zelf (icon.png, <naam>.png, een .ico op het hoogste niveau).
QString iconInFolder(const QString& folder) {
    if (folder.isEmpty()) return {};
    const QDir d(folder);
    for (const QString& n : {QStringLiteral("icon.png"), QStringLiteral("Icon.png"), QStringLiteral("icon.ico"), QStringLiteral("cover.png"),
                             QStringLiteral("banner.png")})
        if (d.exists(n)) return d.filePath(n);
    const QStringList icos = d.entryList({QStringLiteral("*.ico"), QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    return icos.isEmpty() ? QString() : d.filePath(icos.first());
}

QStringList words(const QString& s) {
    QString t = s.toLower();
    // CamelCase en cijfers scheiden: "TombRaider2" -> "tomb raider 2"
    t = LibraryScanner::humanize(s).toLower();
    QStringList out;
    for (const QString& w : t.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts))
        if (w != QStringLiteral("the") && w != QStringLiteral("of") && w != QStringLiteral("arcade") && w != QStringLiteral("ver"))
            out << w;
    return out;
}

QString canon(const QString& p) {
    const QString c = QFileInfo(p).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(p) : c;
}

bool inside(const QString& path, const QString& dir) {
    return !path.isEmpty() && !dir.isEmpty() && (path == dir || path.startsWith(dir + QLatin1Char('/')));
}

GameEntry tpEntry(const QString& tpDir, const QString& profile, const TpProfile& p) {
    GameEntry g;
    g.id = QStringLiteral("tp:") + profile;
    g.kind = GameKind::TeknoParrot;
    g.profile = profile;
    const QJsonObject meta = readMetadata(tpDir, profile);
    g.name = meta.value(QStringLiteral("game_name")).toString();
    if (g.name.isEmpty()) g.name = p.tag(QStringLiteral("GameNameInternal"));
    if (g.name.isEmpty()) g.name = LibraryScanner::humanize(profile);
    g.platform = meta.value(QStringLiteral("platform")).toString();
    g.genre = meta.value(QStringLiteral("game_genre")).toString();
    g.emulator = p.tag(QStringLiteral("EmulatorType"));
    if (g.platform.isEmpty()) g.platform = platformLabel(g.emulator);
    g.gunGame = p.tag(QStringLiteral("GunGame")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    g.exePath = p.tag(QStringLiteral("GamePath"));
    g.folder = g.exePath.isEmpty() ? QString() : QFileInfo(g.exePath).path();
    g.iconPath = iconIn(tpDir, p.tag(QStringLiteral("IconName")));
    if (g.iconPath.isEmpty()) g.iconPath = iconIn(tpDir, meta.value(QStringLiteral("icon_name")).toString());
    if (g.iconPath.isEmpty()) g.iconPath = iconIn(tpDir, profile + QStringLiteral(".png"));
    return g;
}

}  // namespace

QJsonObject GameEntry::toJson() const {
    QJsonObject o{{QStringLiteral("id"), id},
                  {QStringLiteral("kind"), kindKey(kind)},
                  {QStringLiteral("name"), name},
                  {QStringLiteral("profile"), profile},
                  {QStringLiteral("template"), templateProfile},
                  {QStringLiteral("folder"), folder},
                  {QStringLiteral("exe"), exePath},
                  {QStringLiteral("icon"), iconPath},
                  {QStringLiteral("platform"), platform},
                  {QStringLiteral("emulator"), emulator},
                  {QStringLiteral("genre"), genre},
                  {QStringLiteral("gun_game"), gunGame},
                  {QStringLiteral("exe_exists"), exeExists},
                  {QStringLiteral("source"), sourceFolder}};
    if (!candidates.isEmpty()) o.insert(QStringLiteral("candidates"), QJsonArray::fromStringList(candidates));
    return o;
}

GameEntry GameEntry::fromJson(const QJsonObject& o) {
    GameEntry g;
    g.id = o.value(QStringLiteral("id")).toString();
    g.kind = kindFromKey(o.value(QStringLiteral("kind")).toString());
    g.name = o.value(QStringLiteral("name")).toString();
    g.profile = o.value(QStringLiteral("profile")).toString();
    g.templateProfile = o.value(QStringLiteral("template")).toString();
    g.folder = o.value(QStringLiteral("folder")).toString();
    g.exePath = o.value(QStringLiteral("exe")).toString();
    g.iconPath = o.value(QStringLiteral("icon")).toString();
    g.platform = o.value(QStringLiteral("platform")).toString();
    g.emulator = o.value(QStringLiteral("emulator")).toString();
    g.genre = o.value(QStringLiteral("genre")).toString();
    g.gunGame = o.value(QStringLiteral("gun_game")).toBool();
    g.exeExists = o.value(QStringLiteral("exe_exists")).toBool(true);
    g.sourceFolder = o.value(QStringLiteral("source")).toString();
    for (const auto& v : o.value(QStringLiteral("candidates")).toArray()) g.candidates << v.toString();
    return g;
}

bool LibraryData::load(const QString& path, QString* err) {
    QString e;
    const QJsonDocument d = readJson(path, &e);
    if (!d.isObject()) {
        if (err) *err = e;
        return false;
    }
    const QJsonObject o = d.object();
    schema = o.value(QStringLiteral("schema")).toInt(1);
    folders.clear();
    for (const auto& v : o.value(QStringLiteral("folders")).toArray()) {
        LibraryFolder f;
        if (v.isString()) f.path = v.toString();
        else {
            f.path = v.toObject().value(QStringLiteral("path")).toString();
            f.recursive = v.toObject().value(QStringLiteral("recursive")).toBool(true);
        }
        if (!f.path.isEmpty()) folders << f;
    }
    tpDir = o.value(QStringLiteral("tp_dir")).toString();
    defaultGame = o.value(QStringLiteral("default_game")).toString();
    launcher = o.value(QStringLiteral("launcher")).toString();
    overrides.clear();
    const QJsonObject ov = o.value(QStringLiteral("overrides")).toObject();
    for (auto it = ov.begin(); it != ov.end(); ++it) {
        const QJsonObject x = it->toObject();
        GameOverride g;
        g.name = x.value(QStringLiteral("name")).toString();
        g.note = x.value(QStringLiteral("note")).toString();
        g.templateProfile = x.value(QStringLiteral("template")).toString();
        g.hidden = x.value(QStringLiteral("hidden")).toBool();
        g.options = x.value(QStringLiteral("options")).toObject().toVariantMap();
        g.tpFields = x.value(QStringLiteral("profile_fields")).toObject().toVariantMap();
        overrides.insert(it.key(), g);
    }
    games.clear();
    for (const auto& v : o.value(QStringLiteral("games")).toArray()) {
        GameEntry g = GameEntry::fromJson(v.toObject());
        if (!g.id.isEmpty()) games << g;
    }
    scanned = QDateTime::fromString(o.value(QStringLiteral("scanned")).toString(), Qt::ISODate);
    return true;
}

bool LibraryData::save(const QString& path, QString* err) const {
    QJsonArray fa;
    for (const auto& f : folders) fa.append(QJsonObject{{QStringLiteral("path"), f.path}, {QStringLiteral("recursive"), f.recursive}});
    QJsonObject ov;
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        QJsonObject x;
        if (!it->name.isEmpty()) x.insert(QStringLiteral("name"), it->name);
        if (!it->note.isEmpty()) x.insert(QStringLiteral("note"), it->note);
        if (!it->templateProfile.isEmpty()) x.insert(QStringLiteral("template"), it->templateProfile);
        if (it->hidden) x.insert(QStringLiteral("hidden"), true);
        if (!it->options.isEmpty()) x.insert(QStringLiteral("options"), QJsonObject::fromVariantMap(it->options));
        if (!it->tpFields.isEmpty()) x.insert(QStringLiteral("profile_fields"), QJsonObject::fromVariantMap(it->tpFields));
        if (!x.isEmpty()) ov.insert(it.key(), x);
    }
    QJsonArray ga;
    for (const auto& g : games) ga.append(g.toJson());
    QJsonObject o{{QStringLiteral("schema"), schema},
                  {QStringLiteral("comment"), QStringLiteral("RavenGun game library (written by the ravengun app)")},
                  {QStringLiteral("folders"), fa},
                  {QStringLiteral("tp_dir"), tpDir},
                  {QStringLiteral("default_game"), defaultGame},
                  {QStringLiteral("launcher"), launcher},
                  {QStringLiteral("overrides"), ov},
                  {QStringLiteral("games"), ga},
                  {QStringLiteral("scanned"), scanned.toString(Qt::ISODate)}};
    return writeJsonAtomic(path, o, err);
}

QString LibraryData::effectiveTpDir() const { return tpDir.isEmpty() ? AppPaths::defaultTpDir() : AppPaths::expandHome(tpDir); }

QString LibraryScanner::humanize(const QString& id) {
    QString out;
    for (int i = 0; i < id.size(); ++i) {
        const QChar c = id[i];
        if (c == QLatin1Char('_') || c == QLatin1Char('-')) { out += QLatin1Char(' '); continue; }
        if (i > 0) {
            const QChar p = id[i - 1];
            if ((c.isUpper() && p.isLower()) || (c.isDigit() && p.isLetter() && !out.endsWith(QLatin1Char(' '))) ) out += QLatin1Char(' ');
        }
        out += c;
    }
    return out.simplified();
}

double LibraryScanner::nameSimilarity(const QString& a, const QString& b) {
    const QStringList wa = words(a), wb = words(b);
    if (wa.isEmpty() || wb.isEmpty()) return 0;
    const QSet<QString> sa(wa.begin(), wa.end()), sb(wb.begin(), wb.end());
    const int common = static_cast<int>((sa & sb).size());
    const double j = static_cast<double>(common) / static_cast<double>((sa | sb).size());
    // Samengeplakt vergelijken ("TC5" ~ "tc 5", "Gashaaaan" ~ "Gashaaaan!!!!").
    const QString ca = wa.join(QString()), cb = wb.join(QString());
    double c = 0;
    if (ca == cb) c = 1.0;
    else if (ca.size() >= 4 && cb.size() >= 4 && (ca.contains(cb) || cb.contains(ca))) c = 0.7;
    return std::max(j, c);
}

bool LibraryScanner::isTeknoParrotDir(const QString& dir) {
    const QDir d(dir);
    return (d.exists(QStringLiteral("GameProfiles")) || d.exists(QStringLiteral("UserProfiles"))) &&
           (d.exists(QStringLiteral("TeknoParrotUi")) || d.exists(QStringLiteral("TeknoParrotUi.exe")) || d.exists(QStringLiteral("ParrotData.xml")));
}

TemplateInfo LibraryScanner::templateInfo(const QString& tpDir, const QString& profile) {
    TemplateInfo t;
    t.profile = profile;
    TpProfile p;
    if (!p.load(tpDir + QStringLiteral("/GameProfiles/") + profile + QStringLiteral(".xml"))) return t;
    const QJsonObject meta = readMetadata(tpDir, profile);
    t.name = meta.value(QStringLiteral("game_name")).toString();
    if (t.name.isEmpty()) t.name = p.tag(QStringLiteral("GameNameInternal"));
    if (t.name.isEmpty()) t.name = humanize(profile);
    t.platform = meta.value(QStringLiteral("platform")).toString();
    t.genre = meta.value(QStringLiteral("game_genre")).toString();
    t.iconName = meta.value(QStringLiteral("icon_name")).toString();
    t.emulator = p.tag(QStringLiteral("EmulatorType"));
    t.exeNames = p.executableNames();
    t.gunGame = p.tag(QStringLiteral("GunGame")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    return t;
}

QList<TemplateInfo> LibraryScanner::loadTemplates(const QString& tpDir, const std::atomic<bool>* cancel) {
    QList<TemplateInfo> out;
    const QDir d(tpDir + QStringLiteral("/GameProfiles"));
    for (const QString& f : d.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name)) {
        if (cancel && cancel->load()) break;
        const QString prof = f.left(f.size() - 4);
        if (!profileNameRe().match(prof).hasMatch()) continue;
        out << templateInfo(tpDir, prof);
    }
    return out;
}

ScanResult LibraryScanner::scan(const ScanInput& in, const std::atomic<bool>* cancel, const Progress& progress) {
    ScanResult r;
    auto cancelled = [&] { return cancel && cancel->load(); };
    const QString tpDir = in.tpDir;

    // 1. Gebruikersprofielen.
    QList<GameEntry> tp;
    QHash<QString, QString> exeToProfile;  // canoniek GamePath -> profiel
    // [Launch] StackReserve maakt de exe in de view een lokale kopie; de koppeling naar het origineel blijft als <exe>.ravengun-orig
    // ernaast staan. Ook die telt, anders is de map op de NAS voor de scan van niemand en komt de game er een tweede keer in.
    auto addExe = [&exeToProfile](const QString& exe, const QString& prof) {
        exeToProfile.insert(canon(exe), prof);
        const QString orig = exe + QStringLiteral(".ravengun-orig");
        if (QFileInfo(orig).isSymLink()) exeToProfile.insert(canon(orig), prof);
    };
    if (!tpDir.isEmpty()) {
        const QDir ud(tpDir + QStringLiteral("/UserProfiles"));
        for (const QString& f : ud.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name)) {
            const QString prof = f.left(f.size() - 4);
            if (!profileNameRe().match(prof).hasMatch()) continue;
            TpProfile p;
            if (!p.load(ud.filePath(f))) continue;
            GameEntry g = tpEntry(tpDir, prof, p);
            g.exeExists = g.exePath.isEmpty() || QFileInfo::exists(g.exePath);
            if (!g.exePath.isEmpty()) addExe(g.exePath, prof);
            tp << g;
        }
        r.log << QStringLiteral("TeknoParrot user profiles: %1 (%2)").arg(tp.size()).arg(tpDir);
    }
    // 1b. Games met een eigen profiel in game.ini (patches/<Game>/ en de gebruikerslaag): het pad wordt niet opgeslagen maar afgeleid, uit de
    // lokale view van de game of uit de gamemappen aan [Identify] (zoals Dolphin games herkent op hun ID). Een game die er niet staat, is niet
    // geinstalleerd en komt niet in de lijst; een profiel dat ook als UserProfiles/<P>.xml bestaat, komt hier uit game.ini.
    if (!in.ownGames.isEmpty()) {
        std::vector<std::string> folderPaths;
        for (const LibraryFolder& lf : in.folders) folderPaths.push_back(QDir::cleanPath(AppPaths::expandHome(lf.path)).toStdString());
        const ravengun::gamesettings::HashFn hash = [](const std::string& path) {
            QFile f(QString::fromStdString(path));
            if (!f.open(QIODevice::ReadOnly)) return std::string();
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            return QString::fromLatin1(h.result().toHex()).toStdString();
        };
        QHash<QString, int> byProfile;
        for (int i = 0; i < tp.size(); ++i) byProfile.insert(tp[i].profile, i);
        int located = 0;
        for (const OwnGame& og : in.ownGames) {
            if (cancelled()) break;
            if (!profileNameRe().match(og.profile).hasMatch()) continue;
            if (progress) progress(QStringLiteral("Looking for %1").arg(og.profile));
            std::string how;
            const std::string exe = ravengun::gamesettings::locate_game(og.profile.toStdString(), og.settings, folderPaths, in.viewsDir.toStdString(),
                                                                         AppPaths::homeDir().toStdString(), &how, hash);
            if (exe.empty() || !QFileInfo::exists(QString::fromStdString(exe))) continue;
            TpProfile tpl;
            if (!tpDir.isEmpty()) tpl.load(tpDir + QStringLiteral("/GameProfiles/") + og.profile + QStringLiteral(".xml"));
            GameEntry g = tpEntry(tpDir, og.profile, tpl);
            if (!og.name.isEmpty()) g.name = og.name;
            if (g.emulator.isEmpty()) g.emulator = QString::fromStdString(og.settings.get("Game", "Type"));
            if (g.platform.isEmpty() || g.platform == g.emulator) g.platform = platformLabel(g.emulator);
            g.exePath = QString::fromStdString(exe);
            const QString rel = QString::fromStdString(ravengun::gamesettings::identify_of(og.settings).path);
            g.folder = !rel.isEmpty() && g.exePath.endsWith(QLatin1Char('/') + rel) ? g.exePath.left(g.exePath.size() - rel.size() - 1) : QFileInfo(g.exePath).path();
            g.exeExists = true;
            addExe(g.exePath, og.profile);
            if (byProfile.contains(og.profile)) tp[byProfile.value(og.profile)] = g;
            else {
                byProfile.insert(og.profile, static_cast<int>(tp.size()));
                tp << g;
            }
            ++located;
            r.log << QStringLiteral("%1: %2 (from %3)").arg(og.profile, g.exePath, QString::fromStdString(how));
        }
        r.log << QStringLiteral("Games with their own profile: %1 of %2 found").arg(located).arg(in.ownGames.size());
    }

    // 2. Unity-route: de mapnaam uit unity.json in de gamemappen, met dezelfde regel als de daemon (<gamemap>/<folder>, dan een niveau
    //    dieper; de naam moet exact kloppen), of het eigen pad van de gebruiker (nasDir).
    struct Unity { QString key, name, dir, exe; bool matched = false; QString root; };
    QList<Unity> unity;
    for (const UnityGameInfo& info : in.unityGames) {
        if (!info.error.isEmpty()) {
            r.log << QStringLiteral("Unity game %1: unity.json is broken (%2)").arg(info.key, info.error);
            continue;
        }
        Unity u{info.key, info.name, {}, info.exe, false, {}};
        if (u.name.isEmpty()) u.name = humanize(u.key);
        if (!info.nasDir.isEmpty()) {
            u.dir = QDir::cleanPath(AppPaths::expandHome(info.nasDir));
        } else if (!info.folder.isEmpty()) {
            for (const LibraryFolder& lf : in.folders) {
                const QString root = QDir::cleanPath(AppPaths::expandHome(lf.path));
                if (QFileInfo(root + QLatin1Char('/') + info.folder).isDir()) { u.dir = root + QLatin1Char('/') + info.folder; u.root = lf.path; break; }
            }
            if (u.dir.isEmpty())
                for (const LibraryFolder& lf : in.folders) {
                    const QString root = QDir::cleanPath(AppPaths::expandHome(lf.path));
                    for (const QFileInfo& sub : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                        if (QFileInfo(sub.absoluteFilePath() + QLatin1Char('/') + info.folder).isDir()) {
                            u.dir = sub.absoluteFilePath() + QLatin1Char('/') + info.folder;
                            u.root = lf.path;
                            break;
                        }
                    if (!u.dir.isEmpty()) break;
                }
        }
        if (u.dir.isEmpty()) {
            r.log << QStringLiteral("Unity game %1: folder \"%2\" not found in the game folders").arg(info.key, info.folder);
            continue;
        }
        unity << u;
    }
    QSet<QString> tpProfiles;
    for (const auto& g : tp) tpProfiles.insert(g.profile);
    QHash<QString, int> tpIndex;
    for (int i = 0; i < tp.size(); ++i) tpIndex.insert(tp[i].profile, i);
    for (const Unity& u : unity) {
        if (tpProfiles.contains(u.key)) {
            GameEntry& g = tp[tpIndex.value(u.key)];
            if (g.platform.isEmpty() || g.platform == g.emulator) g.platform = QStringLiteral("Unity (PC)");
        }
    }

    // 3. Sjablonen (alleen als er gamemappen zijn).
    QList<TemplateInfo> templates;
    QHash<QString, QList<int>> exeIndex;
    if (!in.folders.isEmpty() && !tpDir.isEmpty()) {
        if (progress) progress(QStringLiteral("Reading TeknoParrot game templates..."));
        templates = loadTemplates(tpDir, cancel);
        for (int i = 0; i < templates.size(); ++i)
            for (const QString& e : templates[i].exeNames) exeIndex[e].append(i);
        r.templates = static_cast<int>(templates.size());
        r.log << QStringLiteral("Templates: %1").arg(templates.size());
    }

    QList<GameEntry> found;
    QSet<QString> seenDirs;
    for (const LibraryFolder& lf : in.folders) {
        if (cancelled()) break;
        const QString root = QDir::cleanPath(AppPaths::expandHome(lf.path));
        if (!QFileInfo(root).isDir()) {
            r.log << QStringLiteral("Folder not found: %1").arg(root);
            continue;
        }
        // Kandidaten: de submappen; de map zelf als hij een game is (een uitvoerbaar bestand van een sjabloon bevat).
        QStringList candidatesDirs;
        const QFileInfoList subs = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        bool rootIsGame = false;
        for (const QString& f : QDir(root).entryList(QDir::Files))
            if (exeIndex.contains(f.toLower())) rootIsGame = true;
        if (rootIsGame) candidatesDirs << root;
        else
            for (const QFileInfo& s : subs) candidatesDirs << s.absoluteFilePath();

        for (const QString& dir : candidatesDirs) {
            if (cancelled()) break;
            const QString cdir = canon(dir);
            if (seenDirs.contains(cdir)) continue;
            seenDirs.insert(cdir);
            const QString base = QFileInfo(dir).fileName();
            // Verborgen mappen en werk-/archiefmappen met een liggend streepje ervoor (_ia_work, _meta-quarantaine-...) zijn geen games;
            // anders stonden ze als "Unconfigured" in de lijst (2026-10-15).
            if (base.startsWith(QLatin1Char('.')) || base.startsWith(QLatin1Char('_'))) continue;
            if (isTeknoParrotDir(dir)) {
                if (r.detectedTpDir.isEmpty()) r.detectedTpDir = dir;
                r.log << QStringLiteral("TeknoParrot installation (not a game): %1").arg(dir);
                continue;
            }
            if (progress) progress(QStringLiteral("Scanning %1").arg(base));
            // a. Hoort bij een gebruikersprofiel?
            QString owner;
            for (auto it = exeToProfile.begin(); it != exeToProfile.end(); ++it)
                if (inside(it.key(), cdir)) { owner = it.value(); break; }
            if (!owner.isEmpty()) {
                GameEntry& g = tp[tpIndex.value(owner)];
                g.folder = dir;
                g.sourceFolder = lf.path;
                continue;
            }
            // b. Unity-route?
            bool isUnity = false;
            for (Unity& u : unity) {
                if (canon(u.dir) == cdir) {
                    isUnity = true;
                    u.matched = true;
                    if (tpProfiles.contains(u.key)) {
                        tp[tpIndex.value(u.key)].folder = dir;
                        tp[tpIndex.value(u.key)].sourceFolder = lf.path;
                        break;
                    }
                    GameEntry g;
                    g.id = QStringLiteral("unity:") + u.key;
                    g.kind = GameKind::Unity;
                    g.profile = u.key;
                    g.name = u.name;
                    g.folder = dir;
                    g.exePath = dir + QLatin1Char('/') + u.exe;
                    g.platform = QStringLiteral("Unity (PC)");
                    g.gunGame = true;
                    g.sourceFolder = lf.path;
                    g.iconPath = iconIn(tpDir, u.key + QStringLiteral(".png"));
                    found << g;
                    break;
                }
            }
            if (isUnity) continue;
            // c. Bekend uit options.json (mapnaam)?
            const KnownGame* known = nullptr;
            for (const KnownGame& k : in.known)
                if (k.folder == base || k.folder.trimmed().compare(base.trimmed(), Qt::CaseInsensitive) == 0) known = &k;
            if (known && !known->profile.isEmpty() && tpProfiles.contains(known->profile)) {
                GameEntry& t = tp[tpIndex.value(known->profile)];
                if (t.folder.isEmpty() || !inside(canon(t.exePath), cdir)) t.folder = dir;
                t.sourceFolder = lf.path;
                continue;
            }
            // d. Bestanden vergelijken met de sjablonen.
            QHash<int, QString> hits;  // sjabloon -> gevonden bestand
            QList<QPair<QString, int>> todo{{dir, 0}};
            int seen = 0;
            const int maxDepth = lf.recursive ? in.maxDepth : 1;
            while (!todo.isEmpty() && seen < in.maxEntriesPerGame && !cancelled()) {
                const auto [d, depth] = todo.takeFirst();
                const QFileInfoList es = QDir(d).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const QFileInfo& e : es) {
                    if (++seen > in.maxEntriesPerGame) break;
                    if (e.isDir()) {
                        if (depth < maxDepth && !e.isSymLink()) todo.append({e.absoluteFilePath(), depth + 1});
                        continue;
                    }
                    const auto it = exeIndex.find(e.fileName().toLower());
                    if (it == exeIndex.end()) continue;
                    for (int t : *it)
                        if (!hits.contains(t)) hits.insert(t, e.absoluteFilePath());
                }
            }
            GameEntry g;
            g.id = QStringLiteral("dir:") + dir;
            g.folder = dir;
            g.sourceFolder = lf.path;
            g.name = base.trimmed();
            g.iconPath = iconInFolder(dir);
            if (!hits.isEmpty()) {
                // Beste sjabloon: naamgelijkenis met de mapnaam, gun-games licht voor, zeldzame uitvoerbare namen voor.
                QList<QPair<double, int>> scored;
                for (auto it = hits.begin(); it != hits.end(); ++it) {
                    const TemplateInfo& t = templates[it.key()];
                    double s = std::max(nameSimilarity(base, t.name), nameSimilarity(base, t.profile));
                    if (t.gunGame) s += 0.05;
                    const QString exe = QFileInfo(it.value()).fileName().toLower();
                    s += 0.1 / static_cast<double>(exeIndex.value(exe).size());
                    scored.append({s, it.key()});
                }
                std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                const bool unique = scored.size() == 1;
                const bool confident = unique || scored.first().first >= 0.45;
                for (int i = 0; i < scored.size() && i < 8; ++i) g.candidates << templates[scored[i].second].profile;
                if (confident) {
                    const TemplateInfo& t = templates[scored.first().second];
                    g.kind = GameKind::Detected;
                    g.templateProfile = t.profile;
                    g.exePath = hits.value(scored.first().second);
                    g.platform = t.platform.isEmpty() ? platformLabel(t.emulator) : t.platform;
                    g.emulator = t.emulator;
                    g.genre = t.genre;
                    g.gunGame = t.gunGame;
                    if (g.iconPath.isEmpty()) g.iconPath = iconIn(tpDir, t.iconName);
                    if (nameSimilarity(base, t.name) >= 0.45 || base.isEmpty()) g.name = t.name;
                } else {
                    g.kind = GameKind::Unconfigured;
                }
            } else {
                g.kind = GameKind::Unconfigured;
            }
            if (known) {
                // options.json kent deze map: titel en platform daarvandaan, profiel als dat er is.
                if (!known->title.isEmpty()) g.name = known->title;
                if (!known->platform.isEmpty()) g.platform = known->platform;
                if (!known->profile.isEmpty()) {
                    g.kind = GameKind::Detected;
                    g.templateProfile = known->profile;
                    if (!g.candidates.contains(known->profile)) g.candidates.prepend(known->profile);
                    QString rel = known->exe.section(QStringLiteral(" ("), 0, 0).trimmed();
                    if (!rel.isEmpty() && QFileInfo(dir + QLatin1Char('/') + rel).isFile()) g.exePath = dir + QLatin1Char('/') + rel;
                }
            }
            found << g;
        }
    }
    // Een Unity-game die niet rechtstreeks in een gamemap staat (een niveau dieper, of een eigen pad) en dus geen kandidaat was.
    for (const Unity& u : unity) {
        if (u.matched || cancelled() || !QFileInfo(u.dir).isDir()) continue;
        if (tpProfiles.contains(u.key)) {
            tp[tpIndex.value(u.key)].folder = u.dir;
            tp[tpIndex.value(u.key)].sourceFolder = u.root;
            continue;
        }
        GameEntry g;
        g.id = QStringLiteral("unity:") + u.key;
        g.kind = GameKind::Unity;
        g.profile = u.key;
        g.name = u.name;
        g.folder = u.dir;
        g.exePath = u.dir + QLatin1Char('/') + u.exe;
        g.platform = QStringLiteral("Unity (PC)");
        g.gunGame = true;
        g.sourceFolder = u.root;
        g.iconPath = iconIn(tpDir, u.key + QStringLiteral(".png"));
        found << g;
    }
    r.games = tp + found;
    r.log << QStringLiteral("Games: %1 (%2 from TeknoParrot, %3 from game folders)").arg(r.games.size()).arg(tp.size()).arg(found.size());
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------

GameLibrary::GameLibrary(QString file, QObject* parent) : QObject(parent), file_(file.isEmpty() ? AppPaths::libraryFile() : std::move(file)) {}

GameLibrary::~GameLibrary() { stopScan(); }

void GameLibrary::stopScan() {
    if (cancel_) cancel_->store(true);
    if (worker_.joinable()) worker_.join();
    scanning_ = false;
}

void GameLibrary::load() {
    if (QFileInfo::exists(file_)) data_.load(file_);
    emit foldersChanged();
    emit gamesChanged();
}

bool GameLibrary::save(QString* err) { return data_.save(file_, err); }

QList<GameEntry> GameLibrary::visibleGames() const {
    QList<GameEntry> out;
    for (GameEntry g : data_.games) {
        const GameOverride o = data_.overrides.value(g.id);
        if (o.hidden) continue;
        g.userName = o.name;
        g.userNote = o.note;
        if (!o.templateProfile.isEmpty() && (g.kind == GameKind::Unconfigured || g.kind == GameKind::Detected)) {
            g.templateProfile = o.templateProfile;
            g.kind = GameKind::Detected;
        }
        out << g;
    }
    return out;
}

const GameEntry* GameLibrary::find(const QString& id) const {
    for (const auto& g : data_.games)
        if (g.id == id) return &g;
    return nullptr;
}

void GameLibrary::addFolder(const QString& path, bool recursive) {
    const QString p = QDir::cleanPath(path);
    for (auto& f : data_.folders)
        if (f.path == p) return;
    data_.folders << LibraryFolder{p, recursive};
    save();
    emit foldersChanged();
    rescan();
}

void GameLibrary::removeFolder(const QString& path) {
    for (int i = 0; i < data_.folders.size(); ++i)
        if (data_.folders[i].path == path) data_.folders.removeAt(i--);
    save();
    emit foldersChanged();
    rescan();
}

void GameLibrary::setFolderRecursive(const QString& path, bool recursive) {
    for (auto& f : data_.folders)
        if (f.path == path) f.recursive = recursive;
    save();
    emit foldersChanged();
}

void GameLibrary::setTpDir(const QString& dir) {
    data_.tpDir = dir;
    save();
    rescan();
}

void GameLibrary::setDefaultGame(const QString& id) {
    data_.defaultGame = id;
    save();
    emit gamesChanged();
}

void GameLibrary::setLauncher(const QString& backend) {
    data_.launcher = backend;
    save();
}

void GameLibrary::setOverride(const QString& id, const GameOverride& o) {
    data_.overrides.insert(id, o);
    save();
    emit gamesChanged();
}

void GameLibrary::hideGame(const QString& id) {
    GameOverride o = data_.overrides.value(id);
    o.hidden = true;
    setOverride(id, o);
}

void GameLibrary::unhideAll() {
    for (auto it = data_.overrides.begin(); it != data_.overrides.end(); ++it) it->hidden = false;
    save();
    emit gamesChanged();
}

void GameLibrary::rescan() {
    stopScan();
    ScanInput in;
    in.folders = data_.folders;
    in.known = known_;
    in.tpDir = data_.effectiveTpDir();
    in.ownGames = own_;
    in.viewsDir = AppPaths::viewsDir();
    in.unityGames = unity_;
    const quint64 gen = ++gen_;
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = cancel_;
    scanning_ = true;
    emit scanStarted();
    worker_ = std::thread([this, in, cancel, gen] {
        ScanResult r = LibraryScanner::scan(in, cancel.get(), [this, cancel](const QString& what) {
            if (!cancel->load()) QMetaObject::invokeMethod(this, [this, what] { emit scanProgress(what); }, Qt::QueuedConnection);
        });
        if (!cancel->load()) QMetaObject::invokeMethod(this, [this, r, gen] { applyResult(r, gen); }, Qt::QueuedConnection);
    });
}

void GameLibrary::applyResult(const ScanResult& r, quint64 gen) {
    if (gen != gen_) return;
    if (worker_.joinable()) worker_.join();
    scanning_ = false;
    data_.games = r.games;
    data_.scanned = QDateTime::currentDateTime();
    log_ = r.log;
    save();
    emit gamesChanged();
    emit scanFinished(static_cast<int>(r.games.size()));
    if (!r.detectedTpDir.isEmpty() && !QFileInfo(data_.effectiveTpDir() + QStringLiteral("/UserProfiles")).isDir())
        emit tpDirDetected(r.detectedTpDir);
}

}  // namespace rg
