// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameData.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/AppPaths.h"
#include "core/FileSafety.h"

namespace rg {

namespace gs = ravengun::gamesettings;

void GameData::load() {
    // $RAVENGUN_GAME_OPTIONS: ander game-options.json (ontwikkelaars en schermafdrukken).
    const QString opts = qEnvironmentVariable("RAVENGUN_GAME_OPTIONS");
    setRouteSources(AppPaths::packagingFile(QStringLiteral("hid-boards.json")), AppPaths::packagingFile(QStringLiteral("jvs-boards.json")),
                    AppPaths::packagingFile(QStringLiteral("aagames-games.json")));
    loadFrom(gs::default_dirs(AppPaths::dataRoot().toStdString()),
             opts.isEmpty() ? AppPaths::packagingFile(QStringLiteral("game-options.json")) : opts);
}

void GameData::setRouteSources(const QString& hidBoards, const QString& jvsBoards, const QString& aagames) {
    hidBoards_.clear();
    jvsBoards_.clear();
    unityGames_.clear();
    auto keys = [](const QString& file, const char* member, QSet<QString>& out) {
        if (file.isEmpty()) return;
        const QJsonObject o = readJson(file).object().value(QLatin1String(member)).toObject();
        for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key());
    };
    keys(hidBoards, "boards", hidBoards_);
    keys(jvsBoards, "boards", jvsBoards_);
    keys(aagames, "games", unityGames_);
}

namespace {

const QStringList& allRoutes() {
    static const QStringList r{QStringLiteral("evdev"), QStringLiteral("tp-shm"), QStringLiteral("unity-tcp"), QStringLiteral("hid-board"),
                               QStringLiteral("jvs")};
    return r;
}

QStringList splitList(const std::string& s) {
    QStringList out;
    for (const QString& p : QString::fromStdString(s).split(QLatin1Char(','), Qt::SkipEmptyParts)) out << p.trimmed();
    return out;
}

bool hasInput(const gs::Game& g) {
    for (const char* k : {"Route", "Layout", "Board", "Boards"})
        if (g.settings.has("Input", k)) return true;
    return false;
}

RouteInfo routeFrom(const gs::Game& g) {
    RouteInfo i;
    const auto get = [&g](const char* k) { return QString::fromStdString(g.get("Input", k)); };
    i.route = get("Route").isEmpty() ? QStringLiteral("evdev") : get("Route");
    i.layout = get("Layout");
    i.board = get("Board");
    i.boards = splitList(g.get("Input", "Boards"));
    i.note = get("Note");
    i.explicitRoute = hasInput(g);
    return i;
}

// Dezelfde regels als de engine (src/ravengun/iobridge/routes.cpp, validate_route).
QString validateRoute(const QString& name, const RouteInfo& i) {
    if (!allRoutes().contains(i.route)) return QStringLiteral("%1: unknown route \"%2\"").arg(name, i.route);
    if (i.route == QStringLiteral("tp-shm") && i.layout.isEmpty()) return QStringLiteral("%1: route tp-shm needs a layout").arg(name);
    if (i.route == QStringLiteral("hid-board") && i.board.isEmpty()) return QStringLiteral("%1: route hid-board needs a board").arg(name);
    if (i.route == QStringLiteral("jvs") && i.board.isEmpty() && i.boards.isEmpty()) return QStringLiteral("%1: route jvs needs a board").arg(name);
    return {};
}

}  // namespace

void GameData::loadFrom(const gs::Dirs& dirs, const QString& gameOptions) {
    dirs_ = dirs;
    baseRoutes_.clear();
    routes_.clear();
    notes_.clear();
    notesEn_.clear();
    state_.clear();
    games_.clear();
    shippedGames_.clear();
    routeErrors_.clear();
    userRoutesError_.clear();
    error_.clear();
    for (const std::string& name : gs::list_games(dirs_)) loadGame(QString::fromStdString(name));
    optionsPath_ = gameOptions;
    options_ = GameOptionsDb();
    if (!gameOptions.isEmpty()) {
        QString err;
        if (!options_.load(gameOptions, &err)) error_ = err;
    }
}

void GameData::loadGame(const QString& profile) {
    const std::string name = profile.toStdString();
    const gs::Game merged = gs::load_game(dirs_, name);
    const gs::Game shipped = dirs_.user.empty() ? merged : gs::load_game(gs::Dirs{dirs_.shipped, {}}, name);
    games_.insert(profile, merged);
    shippedGames_.insert(profile, shipped);
    routeErrors_.remove(profile);
    const auto str = [](const std::string& s) { return QString::fromStdString(s); };
    // Status en notities: de gebruikerslaag mag ze overschrijven.
    notes_.insert(profile, str(merged.get("Status", "Note")));
    notesEn_.insert(profile, str(merged.get("Status", "NoteEN")));
    state_.insert(profile, str(merged.get("Status", "Status")));
    // Route: de aanbevolen is die van de meegeleverde laag; klopt de samenvoeging niet, dan telt de gebruikerslaag niet mee.
    RouteInfo base = routeFrom(shipped);
    base.baseRoute = base.route;
    if (hasInput(shipped)) baseRoutes_.insert(profile, base);
    else baseRoutes_.remove(profile);
    RouteInfo cur = routeFrom(merged);
    cur.baseRoute = base.route;
    const QString err = hasInput(merged) ? validateRoute(profile, cur) : QString();
    if (!err.isEmpty()) {
        routeErrors_.insert(profile, QStringLiteral("%1; your settings for this game are ignored").arg(err));
        cur = base;
    }
    cur.userChoice = err.isEmpty() && gs::read_layer(dirs_.user, name).has("Input", "Route") && cur.route != base.route;
    if (hasInput(merged) || hasInput(shipped)) routes_.insert(profile, cur);
    else routes_.remove(profile);
    userRoutesError_ = routeErrors_.isEmpty() ? QString() : routeErrors_.begin().value();
}

RouteInfo GameData::route(const QString& profile) const {
    auto it = routes_.find(profile);
    if (it != routes_.end()) return *it;
    RouteInfo i;
    i.route = QStringLiteral("evdev");
    i.baseRoute = i.route;
    return i;
}

QStringList GameData::availableRoutes(const QString& profile) const {
    // Unity-games starten altijd via hun plugin (play-halo.sh gaat naar play-unity.sh): geen andere keuze.
    if (unityGames_.contains(profile)) return QStringList{QStringLiteral("unity-tcp")};
    const RouteInfo base = baseRoutes_.value(profile);
    const QString recommended = baseRoutes_.contains(profile) ? base.route : QStringLiteral("evdev");
    QStringList out{recommended};
    auto add = [&out](const QString& r) {
        if (!out.contains(r)) out << r;
    };
    add(QStringLiteral("evdev"));
    if (!base.layout.isEmpty()) add(QStringLiteral("tp-shm"));
    if (!base.board.isEmpty() && hidBoards_.contains(base.board)) add(QStringLiteral("hid-board"));
    if ((!base.board.isEmpty() && jvsBoards_.contains(base.board)) || !base.boards.isEmpty()) add(QStringLiteral("jvs"));
    return out;
}

bool GameData::setUserRoute(const QString& profile, const QString& route, QString* err) {
    if (profile.isEmpty()) {
        if (err) *err = QStringLiteral("This game has no profile yet.");
        return false;
    }
    if (dirs_.user.empty()) {
        if (err) *err = QStringLiteral("No user folder for game settings.");
        return false;
    }
    if (!route.isEmpty() && !availableRoutes(profile).contains(route)) {
        if (err) *err = QStringLiteral("The route \"%1\" is not available for this game.").arg(route);
        return false;
    }
    const QString recommended = baseRoutes_.contains(profile) ? baseRoutes_.value(profile).route : QStringLiteral("evdev");
    std::string e;
    const bool ok = (route.isEmpty() || route == recommended)
                        ? gs::erase_user_value(dirs_, profile.toStdString(), "Input", "Route", &e)
                        : gs::set_user_value(dirs_, profile.toStdString(), "Input", "Route", route.toStdString(), &e);
    if (!ok) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    loadGame(profile);
    return true;
}

QString GameData::note(const QString& profile) const {
    if (const QString en = notesEn_.value(profile); !en.isEmpty()) return en;
    if (const GameOptionsEntry* e = options_.find(profile); e && !e->note.isEmpty()) return e->note;
    return notes_.value(profile);
}

QString GameData::playState(const QString& profile) const {
    const QString s = state_.value(profile);
    return s.isEmpty() ? QStringLiteral("untested") : s;
}

QString GameData::shippedGameDir(const QString& profile) const {
    return dirs_.shipped.empty() ? QString() : QString::fromStdString(gs::game_dir(dirs_.shipped, profile.toStdString()));
}

QString GameData::userGameDir(const QString& profile) const {
    return dirs_.user.empty() ? QString() : QString::fromStdString(gs::game_dir(dirs_.user, profile.toStdString()));
}

QList<PatchInfo> GameData::patches(const QString& profile) const {
    QList<PatchInfo> out;
    const std::string name = profile.toStdString();
    const gs::Game merged = games_.contains(profile) ? games_.value(profile) : gs::load_game(dirs_, name);
    const gs::Game shipped = shippedGames_.contains(profile) ? shippedGames_.value(profile) : gs::load_game(gs::Dirs{dirs_.shipped, {}}, name);
    const gs::Ini userPatches = gs::read_layer(dirs_.user, name, "patches.ini");
    for (const gs::Patch& p : merged.patches) {
        PatchInfo i;
        i.name = QString::fromStdString(p.name);
        i.kind = QString::fromStdString(p.kind());
        i.reason = QString::fromStdString(p.reason());
        i.target = QString::fromStdString(p.get("Target"));
        if (i.target.isEmpty()) i.target = QString::fromStdString(p.get("Module") + (p.get("Offset").empty() ? "" : "+" + p.get("Offset")));
        i.category = QString::fromStdString(p.category());
        i.version = QString::fromStdString(p.version());
        i.source = QString::fromStdString(p.get("Source"));
        i.appliedByCore = p.applied_by() == "core";
        i.enabled = p.enabled();
        const gs::Patch* sp = shipped.patch(p.name);
        i.shippedEnabled = sp && sp->enabled();
        i.userDefined = sp == nullptr;
        const std::string section = "Patch: " + p.name;
        i.userSet = userPatches.has(section, "Enabled") && !userPatches.get(section, "Enabled").empty();
        for (const auto& kv : p.fields) i.fields << qMakePair(QString::fromStdString(kv.first), QString::fromStdString(kv.second));
        out << i;
    }
    return out;
}

QStringList GameData::patchErrors(const QString& profile) const {
    QStringList out;
    const gs::Game g = games_.contains(profile) ? games_.value(profile) : gs::load_game(dirs_, profile.toStdString());
    for (const auto& e : g.patch_errors) out << QString::fromStdString(e.message);
    return out;
}

bool GameData::setPatchEnabled(const QString& profile, const QString& patch, bool enabled, QString* err) {
    if (dirs_.user.empty()) {
        if (err) *err = QStringLiteral("No user folder for game settings.");
        return false;
    }
    const std::string name = profile.toStdString();
    const gs::Game shipped = shippedGames_.contains(profile) ? shippedGames_.value(profile) : gs::load_game(gs::Dirs{dirs_.shipped, {}}, name);
    const gs::Patch* sp = shipped.patch(patch.toStdString());
    std::string e;
    // Gelijk aan de meegeleverde stand: de keuze van de gebruiker weghalen (de meegeleverde stand blijft dan gelden).
    const bool ok = (sp && sp->enabled() == enabled) ? gs::erase_user_patch_enabled(dirs_, name, patch.toStdString(), &e)
                                                     : gs::set_user_patch_enabled(dirs_, name, patch.toStdString(), enabled, &e);
    if (!ok) {
        if (err) *err = QString::fromStdString(e);
        return false;
    }
    loadGame(profile);
    return true;
}

QList<SettingEntry> GameData::settings(const QString& profile) const {
    QList<SettingEntry> out;
    const std::string name = profile.toStdString();
    const gs::Game merged = games_.contains(profile) ? games_.value(profile) : gs::load_game(dirs_, name);
    const gs::Ini shippedIni = gs::read_layer(dirs_.shipped, name), userIni = gs::read_layer(dirs_.user, name);
    for (const gs::Section& s : merged.settings.sections)
        for (const auto& kv : s.kv) {
            SettingEntry e;
            e.section = QString::fromStdString(s.name);
            e.key = QString::fromStdString(kv.first);
            e.value = QString::fromStdString(kv.second);
            e.layer = userIni.has(s.name, kv.first) ? QStringLiteral("user") : shippedIni.has(s.name, kv.first) ? QStringLiteral("shipped") : QStringLiteral("default");
            out << e;
        }
    return out;
}

QString GameData::routeLabel(const QString& route) {
    if (route == QStringLiteral("evdev")) return QStringLiteral("Virtual lightgun (evdev)");
    if (route == QStringLiteral("tp-shm")) return QStringLiteral("I/O bridge (shared memory)");
    if (route == QStringLiteral("unity-tcp")) return QStringLiteral("Unity plugin (TCP)");
    if (route == QStringLiteral("hid-board")) return QStringLiteral("Emulated gun board (HID)");
    if (route == QStringLiteral("jvs")) return QStringLiteral("Emulated JVS board (serial)");
    return route;
}

QString GameData::routeDescription(const QString& route) {
    if (route == QStringLiteral("evdev")) return QStringLiteral("The game reads the virtual lightgun and keyboard of RavenGun.");
    if (route == QStringLiteral("tp-shm"))
        return QStringLiteral("The engine writes the input straight into the shared memory of the game runtime.");
    if (route == QStringLiteral("unity-tcp")) return QStringLiteral("The engine talks to a plugin inside the game.");
    if (route == QStringLiteral("hid-board")) return QStringLiteral("The engine acts as the gun board of the game (a virtual joystick).");
    if (route == QStringLiteral("jvs")) return QStringLiteral("The engine acts as the arcade I/O board on a serial port.");
    return {};
}

QList<KnownGame> GameData::knownGames() const {
    QList<KnownGame> out;
    for (const GameOptionsEntry& e : options_.entries())
        if (!e.nasFolder.isEmpty()) out << KnownGame{e.nasFolder, e.title, e.profile, e.platform, e.exe};
    return out;
}

const GameOptionsEntry* GameData::entryFor(const GameEntry& g) const {
    if (const GameOptionsEntry* e = options_.find(g.profile.isEmpty() ? g.templateProfile : g.profile)) return e;
    const QString folder = QFileInfo(g.folder).fileName();
    return options_.findByFolder(folder);
}

GameContext GameData::context(const GameEntry& g, const QString& tpDir) const {
    GameContext c;
    c.profile = g.profile.isEmpty() ? g.templateProfile : g.profile;
    c.tpDir = tpDir;
    if (!g.exePath.isEmpty()) c.gameDir = QFileInfo(g.exePath).path();
    else c.gameDir = g.folder;
    // Werkelijke map: de map van het uitvoerbare bestand na het volgen van de view-symlink.
    const QString canonExe = g.exePath.isEmpty() ? QString() : QFileInfo(g.exePath).canonicalFilePath();
    c.realGameDir = canonExe.isEmpty() ? c.gameDir : QFileInfo(canonExe).path();
    const QString view = AppPaths::viewsDir() + QLatin1Char('/') + c.profile;
    if (!c.profile.isEmpty() && QFileInfo(view).isDir()) c.viewDir = view;
    // Gamemap: GamePath min het relatieve `exe` uit game-options.json ("TC5/Binaries/Win64/x.exe" -> de map erboven).
    c.rootDir = g.folder.isEmpty() ? c.gameDir : g.folder;
    if (const GameOptionsEntry* e = entryFor(g); e && !e->exe.isEmpty() && !g.exePath.isEmpty()) {
        QString rel = e->exe.section(QStringLiteral(" ("), 0, 0).trimmed();
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString exe = QDir::cleanPath(g.exePath);
        if (exe.endsWith(QLatin1Char('/') + rel)) c.rootDir = exe.left(exe.size() - rel.size() - 1);
        else if (QFileInfo(exe).fileName() == QFileInfo(rel).fileName()) {
            // Zelfde bestandsnaam, ander pad (een view van de exe-map): zoveel mappen omhoog als `rel` diep is.
            QString root = QFileInfo(exe).path();
            for (int i = 0; i < rel.count(QLatin1Char('/')); ++i) root = QFileInfo(root).path();
            c.rootDir = root;
        }
    }
    return c;
}

}  // namespace rg
