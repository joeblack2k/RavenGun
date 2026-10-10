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

void GameData::load() {
    // $RAVENGUN_GAME_OPTIONS: ander game-options.json (ontwikkelaars en schermafdrukken).
    const QString opts = qEnvironmentVariable("RAVENGUN_GAME_OPTIONS");
    setRouteSources(AppPaths::configDir() + QStringLiteral("/io-routes.json"), AppPaths::packagingFile(QStringLiteral("hid-boards.json")),
                    AppPaths::packagingFile(QStringLiteral("jvs-boards.json")), AppPaths::packagingFile(QStringLiteral("aagames-games.json")));
    loadFrom(AppPaths::packagingFile(QStringLiteral("io-routes.json")), AppPaths::packagingFile(QStringLiteral("games-notes.json")),
             opts.isEmpty() ? AppPaths::packagingFile(QStringLiteral("game-options.json")) : opts);
}

void GameData::setRouteSources(const QString& userRoutesFile, const QString& hidBoards, const QString& jvsBoards, const QString& aagames) {
    userRoutes_ = userRoutesFile;
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

// Velden van één game uit JSON over `i` heen (alleen wat er staat).
void readRouteFields(const QJsonObject& r, RouteInfo& i) {
    if (r.contains(QStringLiteral("route"))) i.route = r.value(QStringLiteral("route")).toString();
    if (r.contains(QStringLiteral("layout"))) i.layout = r.value(QStringLiteral("layout")).toString();
    if (r.contains(QStringLiteral("board"))) i.board = r.value(QStringLiteral("board")).toString();
    if (r.contains(QStringLiteral("boards"))) {
        i.boards.clear();
        for (const auto& b : r.value(QStringLiteral("boards")).toArray()) i.boards << b.toString();
    }
    if (r.contains(QStringLiteral("note"))) i.note = r.value(QStringLiteral("note")).toString();
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

void GameData::loadFrom(const QString& ioRoutes, const QString& notes, const QString& gameOptions) {
    baseRoutes_.clear();
    notes_.clear();
    notesEn_.clear();
    state_.clear();
    error_.clear();
    baseDefault_ = QStringLiteral("evdev");
    if (!ioRoutes.isEmpty()) {
        const QJsonObject o = readJson(ioRoutes).object();
        baseDefault_ = o.value(QStringLiteral("default")).toString(QStringLiteral("evdev"));
        const QJsonObject g = o.value(QStringLiteral("games")).toObject();
        for (auto it = g.begin(); it != g.end(); ++it) {
            RouteInfo i;
            i.route = baseDefault_;
            readRouteFields(it->toObject(), i);
            i.explicitRoute = true;
            i.baseRoute = i.route;
            baseRoutes_.insert(it.key(), i);
        }
    }
    if (!notes.isEmpty()) {
        const QJsonObject root = readJson(notes).object();
        const QJsonObject g = root.value(QStringLiteral("games")).toObject();
        for (auto it = g.begin(); it != g.end(); ++it) notes_.insert(it.key(), it->toString());
        const QJsonObject en = root.value(QStringLiteral("games_en")).toObject();
        for (auto it = en.begin(); it != en.end(); ++it) notesEn_.insert(it.key(), it->toString());
        const QJsonObject st = root.value(QStringLiteral("status")).toObject();
        for (auto it = st.begin(); it != st.end(); ++it) state_.insert(it.key(), it->toString());
    }
    loadUserRoutes();
    optionsPath_ = gameOptions;
    options_ = GameOptionsDb();
    if (!gameOptions.isEmpty()) {
        QString err;
        if (!options_.load(gameOptions, &err)) error_ = err;
    }
}

void GameData::loadUserRoutes() {
    routes_ = baseRoutes_;
    defaultRoute_ = baseDefault_;
    userRoutesError_.clear();
    if (userRoutes_.isEmpty() || !QFileInfo::exists(userRoutes_)) return;
    QFile f(userRoutes_);
    if (!f.open(QIODevice::ReadOnly)) {
        userRoutesError_ = f.errorString();
        return;
    }
    const QByteArray text = f.readAll();
    if (text.trimmed().isEmpty()) return;
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(text, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        userRoutesError_ = QStringLiteral("%1 is not valid JSON and is ignored").arg(userRoutes_);
        return;
    }
    // Alles of niets, net als de engine.
    QHash<QString, RouteInfo> merged = baseRoutes_;
    QString def = baseDefault_;
    const QJsonObject o = doc.object();
    if (o.contains(QStringLiteral("default"))) {
        def = o.value(QStringLiteral("default")).toString();
        if (!allRoutes().contains(def)) {
            userRoutesError_ = QStringLiteral("%1: invalid default route; the file is ignored").arg(userRoutes_);
            return;
        }
    }
    const QJsonObject g = o.value(QStringLiteral("games")).toObject();
    for (auto it = g.begin(); it != g.end(); ++it) {
        RouteInfo i = merged.value(it.key());
        if (!merged.contains(it.key())) {
            i.route = def;
            i.baseRoute = baseDefault_;
        }
        const QJsonObject fields = it->toObject();
        readRouteFields(fields, i);
        const QString err = validateRoute(it.key(), i);
        if (!err.isEmpty()) {
            userRoutesError_ = QStringLiteral("%1: %2; the file is ignored").arg(userRoutes_, err);
            return;
        }
        i.explicitRoute = true;
        i.userChoice = fields.contains(QStringLiteral("route")) && i.route != i.baseRoute;
        merged.insert(it.key(), i);
    }
    routes_ = merged;
    defaultRoute_ = def;
}

RouteInfo GameData::route(const QString& profile) const {
    auto it = routes_.find(profile);
    if (it != routes_.end()) return *it;
    RouteInfo i;
    i.route = defaultRoute_;
    i.baseRoute = baseDefault_;
    return i;
}

QStringList GameData::availableRoutes(const QString& profile) const {
    // Unity-games starten altijd via hun plugin (play-halo.sh gaat naar play-unity.sh): geen andere keuze.
    if (unityGames_.contains(profile)) return QStringList{QStringLiteral("unity-tcp")};
    const RouteInfo base = baseRoutes_.value(profile);
    const QString recommended = baseRoutes_.contains(profile) ? base.route : baseDefault_;
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
    if (userRoutes_.isEmpty()) {
        if (err) *err = QStringLiteral("No configuration folder.");
        return false;
    }
    if (!route.isEmpty() && !availableRoutes(profile).contains(route)) {
        if (err) *err = QStringLiteral("The route \"%1\" is not available for this game.").arg(route);
        return false;
    }
    // Keuzes van andere games blijven staan (ook velden die de app niet kent).
    QJsonObject root = QFileInfo::exists(userRoutes_) ? readJson(userRoutes_).object() : QJsonObject();
    if (!root.contains(QStringLiteral("schema"))) root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("comment"),
                QStringLiteral("Invoerroute per game, gekozen in de RavenGun-app (Properties > General). Gaat voor packaging/io-routes.json; "
                               "gelezen door de engine (bij elke start) en scripts/io-route.py (play-halo.sh)."));
    QJsonObject games = root.value(QStringLiteral("games")).toObject();
    const QString recommended = baseRoutes_.contains(profile) ? baseRoutes_.value(profile).route : baseDefault_;
    QJsonObject g = games.value(profile).toObject();
    if (route.isEmpty() || route == recommended) g.remove(QStringLiteral("route"));
    else g.insert(QStringLiteral("route"), route);
    if (g.isEmpty()) games.remove(profile);
    else games.insert(profile, g);
    root.insert(QStringLiteral("games"), games);
    QDir().mkpath(QFileInfo(userRoutes_).path());
    if (!writeJsonAtomic(userRoutes_, root, err)) return false;
    loadUserRoutes();
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
