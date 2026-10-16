// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gegevens over games: instellingen en patches per game uit patches/<Game>/game.ini en patches.ini (meegeleverd, met de
// gebruikerslaag in ~/.local/share/ravengun/patches eroverheen; src/ravengun/gamesettings, docs/per-game-patches.md) en opties en
// memory card uit patches/<Game>/options.json (gebruikerslaag eerst; een kapot bestand geeft geen opties).
//
// Invoerroute: [Input] in game.ini. De aanbevolen route is die van de meegeleverde laag; de keuze van de gebruiker (Properties >
// General > Input route) is een sleutel in het gebruikers-game.ini. De engine en scripts/gamesettings.py (play-halo.sh) lezen
// dezelfde bestanden met dezelfde regels.
//
// Status en notities: [Status] Status (working | playable | parked | untested; working = gecontroleerd op onze eigen loader en rechtstreeks
// vanuit RavenGun gestart en gespeeld, de app toont dat groen als "Working"; de fullscreen-modus toont standaard working en playable), Note
// (Nederlands, voor de webpagina en engine) en NoteEN (Engels, voor de app).
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/GameLibrary.h"
#include "core/GameOptions.h"
#include "core/TpProfile.h"
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
    QString category;  // graphics | input | timing | audio | paths | crash | network | loader
    QString version;   // gameversie waarvoor de patch bedoeld is (leeg = niet gezegd)
    QString source;    // herkomst (bv. de openbare bron van een geheugenpatch)
    bool appliedByCore = false;   // AppliedBy = core: de OpenParrot-kern past dit al toe (alleen documentatie)
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
    void load();  // uit AppPaths::dataRoot() (patches/, met de gebruikerslaag) en AppPaths::packagingFile
    // Voor tests: eigen mappen met instellingen (dirs.user leeg = geen gebruikerslaag). Per game komen game.ini, patches.ini en
    // options.json uit die mappen; options.json van de gebruikerslaag vervangt het meegeleverde bestand als geheel.
    void loadFrom(const ravengun::gamesettings::Dirs& dirs);
    // Extra bronnen voor de routekeuze (voor tests; load() zet ze zelf). Leeg = niet gebruiken.
    void setRouteSources(const QString& hidBoards, const QString& jvsBoards);

    RouteInfo route(const QString& profile) const;
    // Routes die voor deze game kunnen (er is een indeling of bord voor), de aanbevolen route eerst.
    QStringList availableRoutes(const QString& profile) const;
    // Zet de keuze van de gebruiker (leeg of gelijk aan de aanbevolen route = keuze weghalen) in het gebruikers-game.ini.
    bool setUserRoute(const QString& profile, const QString& route, QString* err = nullptr);
    QString userRoutesError() const { return userRoutesError_; }  // een ongeldige gebruikerslaag (dan genegeerd)

    QString note(const QString& profile) const;  // Engels: [Status] NoteEN, dan de Nederlandse Note (game.ini)
    QString playState(const QString& profile) const;  // working | playable | parked | untested
    const GameOptionsDb& options() const { return options_; }
    // Het options.json dat voor deze game geldt (gebruikerslaag eerst, anders het meegeleverde; leeg als er geen is), en de melding
    // als dat bestand kapot is (dan heeft de game geen opties; het meegeleverde bestand telt dan niet).
    QString optionsFile(const QString& profile) const;
    QString optionsError(const QString& profile) const { return optionsErrors_.value(profile); }

    // Patches en instellingen van een game (Properties > Patches).
    QList<PatchInfo> patches(const QString& profile) const;
    // Afgewezen patches (ontbrekende of onbekende Category, geen Kind) als leesbare meldingen.
    QStringList patchErrors(const QString& profile) const;
    // Zet Enabled in het gebruikers-patches.ini; gelijk aan de meegeleverde stand = de keuze weghalen.
    bool setPatchEnabled(const QString& profile, const QString& patch, bool enabled, QString* err = nullptr);
    QList<SettingEntry> settings(const QString& profile) const;
    // Mappen van de game in beide lagen (de map hoeft niet te bestaan; de gebruikersmap wordt bij "Open" gemaakt).
    QString shippedGameDir(const QString& profile) const;
    QString userGameDir(const QString& profile) const;
    const ravengun::gamesettings::Dirs& dirs() const { return dirs_; }

    // Eigen profiel in game.ini (docs/per-game-patches.md, "Het profiel van een game"): de velden, de profielvelden en de games die de
    // bibliotheek in de gamemappen herkent. Zonder eigen profiel: hasOwnProfile = false en lege lijsten (dan geldt UserProfiles/<P>.xml).
    bool hasOwnProfile(const QString& profile) const;
    // De ConfigValues van het profiel met de waarden die nu gelden (alle lagen); hint, bereik en dergelijke uit het sjabloon in tpDir.
    QList<TpField> profileFields(const QString& profile, const QString& tpDir) const;
    // Een profielveld van het eigen profiel zoals het in het XML-profiel heet (EmulationProfile, Is64Bit, ...); leeg als het niet gezet is.
    QString profileValue(const QString& profile, const QString& xmlName) const;
    QList<OwnGame> ownGames() const;   // alle games met een eigen profiel (voor de bibliotheek)
    void reloadGame(const QString& profile) { loadGame(profile); }   // na een schrijfactie in de gebruikerslaag

    static QString routeLabel(const QString& route);  // Engels, voor de lijst
    static QString routeDescription(const QString& route);  // Engels, één zin voor de keuze
    // Context (mappen) van een game voor opties en memory card. De gamemap (rootDir) volgt uit `exe` in options.json
    // (het uitvoerbare bestand relatief aan de gamemap), anders de gevonden map.
    GameContext context(const GameEntry& g, const QString& tpDir) const;
    // Gegevens uit options.json voor deze game (op profiel, dan op mapnaam); nullptr als er niets is.
    const GameOptionsEntry* entryFor(const GameEntry& g) const;
    QList<KnownGame> knownGames() const;  // voor de herkenning bij het scannen
    // De Unity-games (patches/<Game>/unity.json, gebruikerslaag eerst; een kapot bestand in de gebruikerslaag blijft staan met `error`).
    QList<UnityGameInfo> unityGames() const { return unity_.values(); }

private:
    void loadGame(const QString& profile);   // (her)leest één game uit beide lagen
    void loadUnity();   // unity.json van alle games: gebruikerslaag eerst, geen terugval als dat kapot is
    void loadOptions(const QString& profile);   // options.json: gebruikerslaag eerst, geen terugval als dat kapot is
    ravengun::gamesettings::Dirs dirs_;
    QHash<QString, RouteInfo> baseRoutes_, routes_;
    QHash<QString, QString> notes_, notesEn_, state_;
    QHash<QString, ravengun::gamesettings::Game> games_, shippedGames_;   // samengevoegd, en zonder de gebruikerslaag
    QHash<QString, QString> routeErrors_;
    QSet<QString> hidBoards_, jvsBoards_;
    QHash<QString, UnityGameInfo> unity_;
    QString userRoutesError_;
    GameOptionsDb options_;
    QHash<QString, QString> optionsFiles_, optionsErrors_;
};

}  // namespace rg
