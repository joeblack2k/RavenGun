// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gegevens over games uit packaging/: invoerroutes (io-routes.json), notities (games-notes.json) en opties en memory card
// (game-options.json). Bestanden uit de repo gaan voor de ingebouwde kopie (AppPaths::packagingFile).
#include <QHash>
#include <QString>

#include "core/GameLibrary.h"
#include "core/GameOptions.h"

namespace rg {

struct RouteInfo {
    QString route;   // evdev | tp-shm | unity-tcp | hid-board
    QString layout;  // tp-shm-profiel (I/O-indeling)
    QString board;   // hid-board
    QString note;
    bool explicitRoute = false;  // staat in io-routes.json (anders de standaard)
};

class GameData {
public:
    void load();  // uit AppPaths::packagingFile
    void loadFrom(const QString& ioRoutes, const QString& notes, const QString& gameOptions);

    RouteInfo route(const QString& profile) const;
    QString note(const QString& profile) const;  // game-options.json (Engels) gaat voor games-notes.json
    const GameOptionsDb& options() const { return options_; }
    QString gameOptionsPath() const { return optionsPath_; }
    QString loadError() const { return error_; }

    static QString routeLabel(const QString& route);  // Engels, voor de lijst
    // Context (mappen) van een game voor opties en memory card. De gamemap (rootDir) volgt uit `exe` in game-options.json
    // (het uitvoerbare bestand relatief aan de gamemap), anders de gevonden map.
    GameContext context(const GameEntry& g, const QString& tpDir) const;
    // Gegevens uit game-options.json voor deze game (op profiel, dan op mapnaam); nullptr als er niets is.
    const GameOptionsEntry* entryFor(const GameEntry& g) const;
    QList<KnownGame> knownGames() const;  // voor de herkenning bij het scannen

private:
    QString defaultRoute_ = QStringLiteral("evdev");
    QHash<QString, RouteInfo> routes_;
    QHash<QString, QString> notes_;
    GameOptionsDb options_;
    QString optionsPath_, error_;
};

}  // namespace rg
