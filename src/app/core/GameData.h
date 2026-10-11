// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gegevens over games: instellingen en patches per game uit patches/<Game>/game.ini en patches.ini (meegeleverd, met de
// gebruikerslaag in ~/.local/share/ravengun/patches eroverheen; src/ravengun/gamesettings, docs/per-game-patches.md) en opties en
// memory card uit packaging/game-options.json (bestanden uit de repo gaan voor de ingebouwde kopie, AppPaths::packagingFile).
//
// Invoerroute: [Input] in game.ini. De aanbevolen route is die van de meegeleverde laag; de keuze van de gebruiker (Properties >
// General > Input route) is een sleutel in het gebruikers-game.ini. De engine en scripts/gamesettings.py (play-halo.sh) lezen
// dezelfde bestanden met dezelfde regels.
//
// Status en notities: [Status] Status (playable | parked | untested; de fullscreen-modus toont standaard alleen playable), Note
// (Nederlands, voor de webpagina en engine) en NoteEN (Engels, voor de app).
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/GameLibrary.h"
#include "core/GameOptions.h"
#include "ravengun/gamesettings/gamesettings.hpp"

namespace rg {

struct RouteInfo {
    QString route;   // evdev | tp-shm | unity-tcp | hid-board | jvs
    QString layout;  // tp-shm-profiel (I/O-indeling)
    QString board;   // hid-board / jvs
    QStringList boards;  // jvs (keten)
    QString note;
    bool explicitRoute = false;  // staat in game.ini (anders de standaard)
    bool userChoice = false;     // de route komt uit de gebruikerslaag en wijkt af van de aanbevolen route
    QString baseRoute;           // de route volgens de meegeleverde laag (de aanbevolen route)
};

// Een patch uit patches.ini, na samenvoegen van de lagen (Properties > Patches).
struct PatchInfo {
    QString name;
    QString kind;      // file | registry | memory
    QString reason;    // uitleg (tooltip)
    QString target;
    bool enabled = false;         // effectief
    bool shippedEnabled = false;  // zonder de gebruikerslaag
    bool userSet = false;         // de gebruiker zet Enabled zelf (anders geldt de meegeleverde stand)
    bool userDefined = false;     // de patch bestaat alleen in de gebruikerslaag
    QList<QPair<QString, QString>> fields;   // alle velden (Find, Replace, Count, ...)
};

// Een instelling zoals ze voor een game gelden, met de laag waar ze vandaan komt.
struct SettingEntry {
    QString section, key, value;
    QString layer;   // default (_defaults/<Type>.ini) | shipped | user
};

class GameData {
public:
    void load();  // uit AppPaths::dataRoot() (patches/) en AppPaths::packagingFile
    // Voor tests: eigen mappen met instellingen (dirs.user leeg = geen gebruikerslaag) en een eigen game-options.json.
    void loadFrom(const ravengun::gamesettings::Dirs& dirs, const QString& gameOptions);
    // Extra bronnen voor de routekeuze (voor tests; load() zet ze zelf). Leeg = niet gebruiken.
    void setRouteSources(const QString& hidBoards, const QString& jvsBoards, const QString& aagames);

    RouteInfo route(const QString& profile) const;
    // Routes die voor deze game kunnen (er is een indeling of bord voor), de aanbevolen route eerst.
    QStringList availableRoutes(const QString& profile) const;
    // Zet de keuze van de gebruiker (leeg of gelijk aan de aanbevolen route = keuze weghalen) in het gebruikers-game.ini.
    bool setUserRoute(const QString& profile, const QString& route, QString* err = nullptr);
    QString userRoutesError() const { return userRoutesError_; }  // een ongeldige gebruikerslaag (dan genegeerd)

    QString note(const QString& profile) const;  // Engels: NoteEN, dan game-options.json, dan de Nederlandse tekst
    QString playState(const QString& profile) const;  // playable | parked | untested
    const GameOptionsDb& options() const { return options_; }
    QString gameOptionsPath() const { return optionsPath_; }
    QString loadError() const { return error_; }

    // Patches en instellingen van een game (Properties > Patches).
    QList<PatchInfo> patches(const QString& profile) const;
    // Zet Enabled in het gebruikers-patches.ini; gelijk aan de meegeleverde stand = de keuze weghalen.
    bool setPatchEnabled(const QString& profile, const QString& patch, bool enabled, QString* err = nullptr);
    QList<SettingEntry> settings(const QString& profile) const;
    // Mappen van de game in beide lagen (de map hoeft niet te bestaan; de gebruikersmap wordt bij "Open" gemaakt).
    QString shippedGameDir(const QString& profile) const;
    QString userGameDir(const QString& profile) const;
    const ravengun::gamesettings::Dirs& dirs() const { return dirs_; }

    static QString routeLabel(const QString& route);  // Engels, voor de lijst
    static QString routeDescription(const QString& route);  // Engels, één zin voor de keuze
    // Context (mappen) van een game voor opties en memory card. De gamemap (rootDir) volgt uit `exe` in game-options.json
    // (het uitvoerbare bestand relatief aan de gamemap), anders de gevonden map.
    GameContext context(const GameEntry& g, const QString& tpDir) const;
    // Gegevens uit game-options.json voor deze game (op profiel, dan op mapnaam); nullptr als er niets is.
    const GameOptionsEntry* entryFor(const GameEntry& g) const;
    QList<KnownGame> knownGames() const;  // voor de herkenning bij het scannen

private:
    void loadGame(const QString& profile);   // (her)leest één game uit beide lagen
    ravengun::gamesettings::Dirs dirs_;
    QHash<QString, RouteInfo> baseRoutes_, routes_;
    QHash<QString, QString> notes_, notesEn_, state_;
    QHash<QString, ravengun::gamesettings::Game> games_, shippedGames_;   // samengevoegd, en zonder de gebruikerslaag
    QHash<QString, QString> routeErrors_;
    QSet<QString> hidBoards_, jvsBoards_, unityGames_;
    QString userRoutesError_;
    GameOptionsDb options_;
    QString optionsPath_, error_;
};

}  // namespace rg
