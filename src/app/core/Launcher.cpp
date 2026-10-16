// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Launcher.h"

#include <QDir>
#include <QFileInfo>

#include "core/EngineClient.h"
#include "core/GameData.h"
#include "core/GameOptions.h"
#include "core/FileSafety.h"
#include "core/TextConfig.h"
#include "core/TpProfile.h"

#include <QFile>
#include <QJsonObject>
#include <utility>

namespace rg {

RuntimeCheck RuntimeCheck::check(const QString& dir) {
    RuntimeCheck c;
    c.dir = dir;
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        c.missing << QStringLiteral("folder");
        c.summary = QStringLiteral("Runtime folder not found: %1").arg(dir.isEmpty() ? QStringLiteral("(not set)") : dir);
        return c;
    }
    // Laders en cores; minstens een lader moet er zijn.
    const QList<QPair<QString, QString>> loaders{
        {QStringLiteral("TeknoParrot/BudgieLoader.exe"), QStringLiteral("TeknoParrot loader")},
        {QStringLiteral("ElfLdr2/BudgieLoader.exe"), QStringLiteral("ElfLdr2 loader (Linux games)")},
        {QStringLiteral("OpenParrotx64/OpenParrotLoader64.exe"), QStringLiteral("OpenParrot loader (64-bit)")},
        {QStringLiteral("OpenParrotWin32/OpenParrotLoader.exe"), QStringLiteral("OpenParrot loader (32-bit)")}};
    int loaderCount = 0;
    for (const auto& [rel, name] : loaders) {
        if (QFileInfo::exists(dir + QLatin1Char('/') + rel)) {
            c.found << name;
            ++loaderCount;
        } else {
            c.missing << name;
        }
    }
    const bool templates = QFileInfo(dir + QStringLiteral("/GameProfiles")).isDir();
    if (templates) c.found << QStringLiteral("game profiles");
    else c.missing << QStringLiteral("game profiles (GameProfiles/)");
    // TeknoParrotUi zelf start RavenGun niet meer (sinds 2026-10-16): alleen ter informatie, geen eis en niet in found/missing.
    c.hasUi = QFileInfo(dir + QStringLiteral("/TeknoParrotUi")).isFile();
    c.ok = loaderCount > 0 && templates;
    if (!c.ok) c.summary = QStringLiteral("Runtime incomplete: no loader or game profiles found in %1").arg(dir);
    else c.summary = QStringLiteral("Runtime found: %1 loader(s)").arg(loaderCount);
    return c;
}

bool ProfileSync::apply(const GameEntry& g, const GameLibrary& lib, const GameData& data, QStringList* errors) {
    const GameOverride ov = lib.data().overrides.value(g.id);
    const GameContext ctx = data.context(g, lib.tpDir());
    bool ok = true;
    if (const GameOptionsEntry* e = data.entryFor(g)) {
        for (const GameOption& o : e->options) {
            if (!ov.options.contains(o.key)) continue;
            if (!OptionStore::writable(o, ctx)) continue;  // alleen-lezen (NAS): de waarde blijft in de bibliotheek staan
            const QString want = ov.options.value(o.key).toString();
            bool present = false;
            if (OptionStore::read(o, ctx, &present) == want && present) continue;
            QString err;
            if (!OptionStore::write(o, ctx, want, &err)) {
                ok = false;
                if (errors) *errors << QStringLiteral("%1: %2").arg(o.name, err);
            }
        }
    }
    // Een game met een eigen profiel in game.ini (docs/per-game-patches.md): alles gaat naar de gebruikerslaag van game.ini, en alleen als het van
    // de meegeleverde waarde afwijkt (Dolphin: User/GameSettings). UserProfiles/<P>.xml en <P>.env worden niet geschreven; de XML voor een start via
    // TeknoParrotUi maakt scripts/play-halo.sh uit game.ini.
    if (ctx.ownProfile) {
        if (ov.options.contains(QStringLiteral("display.gamescope"))) {
            const bool on = ov.options.value(QStringLiteral("display.gamescope")).toString() == QStringLiteral("1");
            QString err;
            // "Gamescope =" (leeg) heft ook een meegeleverde "true" op; uit en gelijk aan de meegeleverde stand haalt de sleutel weg.
            if (!OptionStore::writeSetting(ctx, QStringLiteral("Display"), QStringLiteral("Gamescope"), on ? QStringLiteral("true") : QString(), &err) ||
                !OptionStore::writeSetting(ctx, QStringLiteral("Display"), QStringLiteral("GamescopePath"),
                                           on ? QStringLiteral("/usr/bin/gamescope") : QString(), &err)) {
                ok = false;
                if (errors) *errors << err;
            }
        }
        if (!ov.tpFields.isEmpty()) {
            const ravengun::gamesettings::Game g = ravengun::gamesettings::load_game(ctx.layers, ctx.profile.toStdString());
            for (auto it = ov.tpFields.begin(); it != ov.tpFields.end(); ++it) {
                const QString key = it.key();   // "Categorie/Veld" (de sleutel in [Config])
                const int slash = key.indexOf(QLatin1Char('/'));
                const std::string cfgKey = ravengun::gamesettings::find_config_key(g.settings, key.mid(slash + 1).toStdString(),
                                                                                  slash >= 0 ? key.left(slash).toStdString() : std::string());
                if (cfgKey.empty()) continue;   // het profiel kent dit veld niet (meer)
                QString err;
                if (!OptionStore::writeSetting(ctx, QStringLiteral("Config"), QString::fromStdString(cfgKey), it->toString(), &err)) {
                    ok = false;
                    if (errors) *errors << err;
                }
            }
        }
        return ok;
    }
    // Gamescope (Display): UserProfiles/<P>.env, zoals scripts/play-halo.sh en tp-add-game.py --gamescope het lezen.
    if (ov.options.contains(QStringLiteral("display.gamescope")) && QFileInfo::exists(ctx.userProfilePath())) {
        const bool on = ov.options.value(QStringLiteral("display.gamescope")).toString() == QStringLiteral("1");
        QFile f(ctx.envPath());
        QString text;
        const bool existed = f.open(QIODevice::ReadOnly);
        if (existed) text = QString::fromUtf8(f.readAll());
        f.close();
        QString out = text;
        if (on) {
            out = keyvalue::set(out, QStringLiteral("TP_GAMESCOPE"), QStringLiteral("1"));
            out = keyvalue::set(out, QStringLiteral("TP_GAMESCOPE_PATH"), QStringLiteral("/usr/bin/gamescope"));
        } else {
            out = keyvalue::remove(out, QStringLiteral("TP_GAMESCOPE"));
            out = keyvalue::remove(out, QStringLiteral("TP_GAMESCOPE_PATH"));
        }
        QString err;
        if (out != text && !writeTextAtomic(ctx.envPath(), out.toUtf8(), &err, existed ? QStringLiteral(".ravengun-bak") : QString())) {
            ok = false;
            if (errors) *errors << err;
        }
        // Ook in de gebruikerslaag van game.ini ([Display] Gamescope): die gaat voor het meegeleverde game.ini en voor het oude .env,
        // dus een keuze in de app geldt ook als de meegeleverde instellingen gamescope aan hebben. Uit = de sleutel terugzetten
        // ("Gamescope =" heft ook een meegeleverde waarde op), zodat de variabele ontbreekt zoals vroeger.
        if (!data.dirs().user.empty() && !ctx.profile.isEmpty()) {
            const std::string prof = ctx.profile.toStdString();
            std::string e;
            bool wrote = ravengun::gamesettings::set_user_value(data.dirs(), prof, "Display", "Gamescope", on ? "true" : "", &e);
            if (wrote && on) wrote = ravengun::gamesettings::set_user_value(data.dirs(), prof, "Display", "GamescopePath", "/usr/bin/gamescope", &e);
            else if (wrote) wrote = ravengun::gamesettings::set_user_value(data.dirs(), prof, "Display", "GamescopePath", "", &e);
            if (!wrote) {
                ok = false;
                if (errors) *errors << QString::fromStdString(e);
            }
        }
    }
    if (!ov.tpFields.isEmpty() && QFileInfo::exists(ctx.userProfilePath())) {
        TpProfile p;
        QString err;
        if (!p.load(ctx.userProfilePath(), &err)) {
            if (errors) *errors << err;
            return false;
        }
        bool changed = false;
        for (auto it = ov.tpFields.begin(); it != ov.tpFields.end(); ++it) {
            const QString key = it.key();
            const int slash = key.indexOf(QLatin1Char('/'));
            const QString cat = slash >= 0 ? key.left(slash) : QString();
            const QString field = slash >= 0 ? key.mid(slash + 1) : key;
            bool found = false;
            if (p.fieldValue(field, cat, &found) == it->toString() || !found) continue;
            changed = p.setFieldValue(field, it->toString(), cat) || changed;
        }
        if (changed && !p.save(&err)) {
            ok = false;
            if (errors) *errors << err;
        }
    }
    return ok;
}

EngineBackend::EngineBackend(EngineClient* engine, GameLibrary* library, GameData* data, QString launch)
    : engine_(engine), library_(library), data_(data), launch_(std::move(launch)) {}

bool EngineBackend::available(QString* why) const {
    const RuntimeCheck c = RuntimeCheck::check(library_->tpDir());
    if (!c.ok) {
        if (why) *why = QStringLiteral("The game runtime was not found. Choose it in Settings > Paths.");
        return false;
    }
    return true;
}

bool EngineBackend::prepare(const GameEntry& g, QString* err) {
    QStringList errors;
    if (!ProfileSync::apply(g, *library_, *data_, &errors)) {
        if (err) *err = QStringLiteral("Could not write the game settings: %1").arg(errors.join(QStringLiteral("; ")));
        return false;
    }
    return true;
}

void EngineBackend::start(const GameEntry& g, Done done) {
    QJsonObject f{{QStringLiteral("profile"), g.profile}};
    if (!launch_.isEmpty()) f.insert(QStringLiteral("launch"), launch_);
    engine_->request(QStringLiteral("game_start"), f, [done](const EngineReply& r) {
        // Geen antwoord op tijd is geen mislukte start: de engine kan nog bezig zijn (een oudere engine plande in het verzoek zelf). De
        // toestand uit de polling (starting, running of failed met de melding) laat zien hoe het afloopt.
        if (r.errorCode == QStringLiteral("timeout")) return done(true, {});
        done(r.ok, r.errorMessage);
    });
}

void EngineBackend::stop(Done done) {
    engine_->request(QStringLiteral("game_stop"), {}, [done](const EngineReply& r) { done(r.ok, r.errorMessage); });
}

QStringList backendIds() { return {QStringLiteral("auto"), QStringLiteral("direct")}; }

std::unique_ptr<LaunchBackend> makeBackend(const QString& id, EngineClient* engine, GameLibrary* library, GameData* data) {
    if (id == QStringLiteral("direct")) return std::make_unique<DirectBackend>(engine, library, data);
    return std::make_unique<AutoBackend>(engine, library, data);  // "auto", leeg of onbekend (ook het oude "teknoparrot-ui")
}

}  // namespace rg
