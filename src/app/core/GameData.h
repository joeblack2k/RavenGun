// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gegevens over games uit packaging/: invoerroutes (io-routes.json), notities (games-notes.json) en opties en memory card
// (game-options.json). Bestanden uit de repo gaan voor de ingebouwde kopie (AppPaths::packagingFile).
//
// Invoerroute: packaging/io-routes.json is de basis; de keuze van de gebruiker staat in ~/.config/ravengun/io-routes.json
// (alleen de afwijkingen, {"schema":1,"games":{"<profiel>":{"route":"..."}}}) en gaat voor. De engine
// (apply_io_routes_override) en scripts/io-route.py (play-halo.sh) lezen hetzelfde bestand met dezelfde regels.
//
// Notities (games-notes.json): "games" is de Nederlandse tekst (webpagina en engine), "games_en" de Engelse (de app) en
// "status" per profiel playable | parked | untested (de fullscreen-modus toont standaard alleen playable).
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/GameLibrary.h"
#include "core/GameOptions.h"

namespace rg {

struct RouteInfo {
    QString route;   // evdev | tp-shm | unity-tcp | hid-board | jvs
    QString layout;  // tp-shm-profiel (I/O-indeling)
    QString board;   // hid-board / jvs
    QStringList boards;  // jvs (keten)
    QString note;
    bool explicitRoute = false;  // staat in io-routes.json of in het gebruikersbestand (anders de standaard)
    bool userChoice = false;     // de route komt uit ~/.config/ravengun/io-routes.json
    QString baseRoute;           // de route volgens packaging/io-routes.json (de aanbevolen route)
};

class GameData {
public:
    void load();  // uit AppPaths::packagingFile en AppPaths::configDir()
    void loadFrom(const QString& ioRoutes, const QString& notes, const QString& gameOptions);
    // Extra bronnen voor de routekeuze (voor tests; load() zet ze zelf). Leeg = niet gebruiken.
    void setRouteSources(const QString& userRoutesFile, const QString& hidBoards, const QString& jvsBoards, const QString& aagames);

    RouteInfo route(const QString& profile) const;
    // Routes die voor deze game kunnen (er is een indeling of bord voor), de aanbevolen route eerst.
    QStringList availableRoutes(const QString& profile) const;
    // Zet de keuze van de gebruiker (leeg of gelijk aan de aanbevolen route = keuze weghalen) en schrijft het gebruikersbestand.
    bool setUserRoute(const QString& profile, const QString& route, QString* err = nullptr);
    QString userRoutesFile() const { return userRoutes_; }
    QString userRoutesError() const { return userRoutesError_; }  // ongeldig gebruikersbestand (dan genegeerd)

    QString note(const QString& profile) const;  // Engels: games_en, dan game-options.json, dan de Nederlandse tekst
    QString playState(const QString& profile) const;  // playable | parked | untested
    const GameOptionsDb& options() const { return options_; }
    QString gameOptionsPath() const { return optionsPath_; }
    QString loadError() const { return error_; }

    static QString routeLabel(const QString& route);  // Engels, voor de lijst
    static QString routeDescription(const QString& route);  // Engels, één zin voor de keuze
    // Context (mappen) van een game voor opties en memory card. De gamemap (rootDir) volgt uit `exe` in game-options.json
    // (het uitvoerbare bestand relatief aan de gamemap), anders de gevonden map.
    GameContext context(const GameEntry& g, const QString& tpDir) const;
    // Gegevens uit game-options.json voor deze game (op profiel, dan op mapnaam); nullptr als er niets is.
    const GameOptionsEntry* entryFor(const GameEntry& g) const;
    QList<KnownGame> knownGames() const;  // voor de herkenning bij het scannen

private:
    void loadUserRoutes();
    QString defaultRoute_ = QStringLiteral("evdev"), baseDefault_ = QStringLiteral("evdev");
    QHash<QString, RouteInfo> baseRoutes_, routes_;
    QHash<QString, QString> notes_, notesEn_, state_;
    QSet<QString> hidBoards_, jvsBoards_, unityGames_;
    QString userRoutes_, userRoutesError_;
    GameOptionsDb options_;
    QString optionsPath_, error_;
};

}  // namespace rg
