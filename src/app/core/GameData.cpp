// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/GameData.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "core/AppPaths.h"
#include "core/FileSafety.h"

namespace rg {

void GameData::load() {
    // $RAVENGUN_GAME_OPTIONS: ander game-options.json (ontwikkelaars en schermafdrukken).
    const QString opts = qEnvironmentVariable("RAVENGUN_GAME_OPTIONS");
    loadFrom(AppPaths::packagingFile(QStringLiteral("io-routes.json")), AppPaths::packagingFile(QStringLiteral("games-notes.json")),
             opts.isEmpty() ? AppPaths::packagingFile(QStringLiteral("game-options.json")) : opts);
}

void GameData::loadFrom(const QString& ioRoutes, const QString& notes, const QString& gameOptions) {
    routes_.clear();
    notes_.clear();
    error_.clear();
    if (!ioRoutes.isEmpty()) {
        const QJsonObject o = readJson(ioRoutes).object();
        defaultRoute_ = o.value(QStringLiteral("default")).toString(QStringLiteral("evdev"));
        const QJsonObject g = o.value(QStringLiteral("games")).toObject();
        for (auto it = g.begin(); it != g.end(); ++it) {
            const QJsonObject r = it->toObject();
            RouteInfo i;
            i.route = r.value(QStringLiteral("route")).toString(defaultRoute_);
            i.layout = r.value(QStringLiteral("layout")).toString();
            i.board = r.value(QStringLiteral("board")).toString();
            i.note = r.value(QStringLiteral("note")).toString();
            i.explicitRoute = true;
            routes_.insert(it.key(), i);
        }
    }
    if (!notes.isEmpty()) {
        const QJsonObject g = readJson(notes).object().value(QStringLiteral("games")).toObject();
        for (auto it = g.begin(); it != g.end(); ++it) notes_.insert(it.key(), it->toString());
    }
    optionsPath_ = gameOptions;
    options_ = GameOptionsDb();
    if (!gameOptions.isEmpty()) {
        QString err;
        if (!options_.load(gameOptions, &err)) error_ = err;
    }
}

RouteInfo GameData::route(const QString& profile) const {
    auto it = routes_.find(profile);
    if (it != routes_.end()) return *it;
    RouteInfo i;
    i.route = defaultRoute_;
    return i;
}

QString GameData::note(const QString& profile) const {
    if (const GameOptionsEntry* e = options_.find(profile); e && !e->note.isEmpty()) return e->note;
    return notes_.value(profile);
}

QString GameData::routeLabel(const QString& route) {
    if (route == QStringLiteral("evdev")) return QStringLiteral("Virtual lightgun (evdev)");
    if (route == QStringLiteral("tp-shm")) return QStringLiteral("I/O bridge (shared memory)");
    if (route == QStringLiteral("unity-tcp")) return QStringLiteral("Unity plugin (TCP)");
    if (route == QStringLiteral("hid-board")) return QStringLiteral("Emulated gun board (HID)");
    return route;
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
