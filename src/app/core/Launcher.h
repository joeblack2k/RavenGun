// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Starten van games via verwisselbare backends (besluit 2026-10-10: RavenGun vervangt de TeknoParrot-UI voor de gebruiker;
// docs/decisions.md, "Qt-app vervangt TeknoParrotUi").
//
// Alle backends starten via de engine (IPC game_start); de engine start elke game zelf in de unit ravengun-tpui (geen startscript,
// geen TeknoParrotUi). Ze verschillen alleen in de startwijze die ze meegeven (A10, docs/decisions.md "A10: games direct starten"):
// - "auto" (standaard): de route uit het profiel (native, Unity of [Launch] Mode = direct in patches/<profiel>/game.ini).
// - "direct": de directe route afdwingen (rg-direct start de lader BudgieLoader/OpenParrotLoader zelf; de invoer komt van de engine).
// Een game zonder eigen route start niet; de engine zegt waarom.
// Het profiel van een game staat in game.ini (patches/<Game>/ met de gebruikerslaag; docs/per-game-patches.md). ProfileSync schrijft de
// instellingen uit onze bibliotheek (library.json, options.json) daarheen, in de gebruikerslaag en alleen als ze van de meegeleverde
// waarde afwijken. Het gebruikersprofiel van TeknoParrotUi (UserProfiles/<P>.xml en <P>.env) is een uitvoerformaat voor een start via
// TeknoParrotUi (scripts/play-halo.sh maakt het uit game.ini) en geldt alleen nog als terugval voor een game zonder eigen profiel: ProfileSync
// schrijft dan, zoals voorheen, in dat bestand.
//
// De TeknoParrot-runtime (laders en cores) blijft een afhankelijkheid; RuntimeCheck controleert de gekozen map.
// De rest van de app praat alleen met GameLauncher en neemt nergens aan dat TeknoParrotUi draait.
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

#include "core/GameLibrary.h"

namespace rg {

class EngineClient;
class GameData;

struct RuntimeCheck {
    bool ok = false;        // genoeg voor de gekozen backend
    bool hasUi = false;     // TeknoParrotUi aanwezig (alleen ter informatie: RavenGun start er niets meer mee)
    QString dir;
    QStringList found, missing;
    QString summary;        // Engels, één regel
    static RuntimeCheck check(const QString& dir);
};

// Schrijft de instellingen van een game uit de bibliotheek naar de uitvoerbestanden van de backend (profiel, .env, ini).
class ProfileSync {
public:
    // Alle opgeslagen waarden (options.json-opties en losse profielvelden) van deze game naar hun doel. errors: Engels.
    static bool apply(const GameEntry& g, const GameLibrary& lib, const GameData& data, QStringList* errors = nullptr);
};

class LaunchBackend {
public:
    using Done = std::function<void(bool ok, const QString& error)>;
    virtual ~LaunchBackend() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;  // alleen voor Settings > Paths
    virtual bool available(QString* why = nullptr) const = 0;
    // Bereidt de start voor (gegenereerde bestanden bijwerken). false + err = niet starten.
    virtual bool prepare(const GameEntry& g, QString* err) = 0;
    virtual void start(const GameEntry& g, Done done) = 0;
    virtual void stop(Done done) = 0;
};

// Gemeenschappelijk deel: starten en stoppen via de engine, met een vaste startwijze ("" = auto).
class EngineBackend : public LaunchBackend {
public:
    EngineBackend(EngineClient* engine, GameLibrary* library, GameData* data, QString launch);
    bool available(QString* why) const override;
    bool prepare(const GameEntry& g, QString* err) override;
    void start(const GameEntry& g, Done done) override;
    void stop(Done done) override;
    QString launchMode() const { return launch_; }  // wat game_start als "launch" meekrijgt (leeg = niets)

protected:
    EngineClient* engine_;
    GameLibrary* library_;
    GameData* data_;
    QString launch_;
};

class AutoBackend final : public EngineBackend {
public:
    AutoBackend(EngineClient* engine, GameLibrary* library, GameData* data) : EngineBackend(engine, library, data, QString()) {}
    QString id() const override { return QStringLiteral("auto"); }
    QString displayName() const override { return QStringLiteral("Automatic (the route from the game's profile)"); }
};

class DirectBackend final : public EngineBackend {
public:
    DirectBackend(EngineClient* engine, GameLibrary* library, GameData* data)
        : EngineBackend(engine, library, data, QStringLiteral("direct")) {}
    QString id() const override { return QStringLiteral("direct"); }
    QString displayName() const override { return QStringLiteral("Direct (RavenGun starts the TeknoParrot loader itself)"); }
};

std::unique_ptr<LaunchBackend> makeBackend(const QString& id, EngineClient* engine, GameLibrary* library, GameData* data);
QStringList backendIds();

}  // namespace rg
