// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Starten van games via verwisselbare backends (besluit 2026-10-10: RavenGun vervangt de TeknoParrot-UI voor de gebruiker;
// docs/decisions.md, "Qt-app vervangt TeknoParrotUi").
//
// - "teknoparrot-ui" (nu): de engine start scripts/play-halo.sh, dat TeknoParrotUi --profile=<P>.xml headless gebruikt als
//   lader. Het gebruikersprofiel (UserProfiles/<P>.xml en <P>.env) is daarbij alleen een uitvoerformaat: vóór het starten
//   schrijft ProfileSync de instellingen uit onze bibliotheek (library.json, game-options.json) erin.
// - "direct" (later): de engine start de lader (BudgieLoader/OpenParrotLoader) zelf met de juiste argumenten en omgeving en
//   levert zelf JVS-pipe en gedeeld geheugen. Nog niet gebouwd; available() zegt dat.
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
    bool hasUi = false;     // TeknoParrotUi aanwezig (nodig voor de backend teknoparrot-ui)
    QString dir;
    QStringList found, missing;
    QString summary;        // Engels, één regel
    static RuntimeCheck check(const QString& dir);
};

// Schrijft de instellingen van een game uit de bibliotheek naar de uitvoerbestanden van de backend (profiel, .env, ini).
class ProfileSync {
public:
    // Alle opgeslagen waarden (game-options.json-opties en losse profielvelden) van deze game naar hun doel. errors: Engels.
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

class TeknoParrotUiBackend final : public LaunchBackend {
public:
    TeknoParrotUiBackend(EngineClient* engine, GameLibrary* library, GameData* data);
    QString id() const override { return QStringLiteral("teknoparrot-ui"); }
    QString displayName() const override { return QStringLiteral("TeknoParrot runtime via TeknoParrotUi (headless)"); }
    bool available(QString* why) const override;
    bool prepare(const GameEntry& g, QString* err) override;
    void start(const GameEntry& g, Done done) override;
    void stop(Done done) override;

private:
    EngineClient* engine_;
    GameLibrary* library_;
    GameData* data_;
};

class DirectBackend final : public LaunchBackend {
public:
    QString id() const override { return QStringLiteral("direct"); }
    QString displayName() const override { return QStringLiteral("Direct (engine starts the loader; not available yet)"); }
    bool available(QString* why) const override;
    bool prepare(const GameEntry&, QString* err) override;
    void start(const GameEntry&, Done done) override;
    void stop(Done done) override;
};

std::unique_ptr<LaunchBackend> makeBackend(const QString& id, EngineClient* engine, GameLibrary* library, GameData* data);
QStringList backendIds();

}  // namespace rg
