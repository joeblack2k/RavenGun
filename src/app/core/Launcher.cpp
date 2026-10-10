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
    c.hasUi = QFileInfo(dir + QStringLiteral("/TeknoParrotUi")).isFile();
    if (c.hasUi) c.found << QStringLiteral("TeknoParrotUi (used headless as loader)");
    else c.missing << QStringLiteral("TeknoParrotUi");
    c.ok = loaderCount > 0 && templates;
    if (!c.ok) c.summary = QStringLiteral("Runtime incomplete: no loader or game profiles found in %1").arg(dir);
    else c.summary = QStringLiteral("Runtime found: %1 loader(s)%2").arg(loaderCount).arg(c.hasUi ? QString() : QStringLiteral(", but TeknoParrotUi is missing"));
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

bool TeknoParrotUiBackend::available(QString* why) const {
    const RuntimeCheck c = RuntimeCheck::check(library_->tpDir());
    if (!c.ok || !c.hasUi) {
        if (why) *why = c.ok ? QStringLiteral("The game runtime is incomplete (TeknoParrotUi is missing). See Settings > Paths.")
                             : QStringLiteral("The game runtime was not found. Choose it in Settings > Paths.");
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
    engine_->request(QStringLiteral("game_start"), f, [done](const EngineReply& r) { done(r.ok, r.errorMessage); });
}

void EngineBackend::stop(Done done) {
    engine_->request(QStringLiteral("game_stop"), {}, [done](const EngineReply& r) { done(r.ok, r.errorMessage); });
}

QStringList backendIds() { return {QStringLiteral("auto"), QStringLiteral("direct"), QStringLiteral("teknoparrot-ui")}; }

std::unique_ptr<LaunchBackend> makeBackend(const QString& id, EngineClient* engine, GameLibrary* library, GameData* data) {
    if (id == QStringLiteral("direct")) return std::make_unique<DirectBackend>(engine, library, data);
    if (id == QStringLiteral("teknoparrot-ui")) return std::make_unique<TeknoParrotUiBackend>(engine, library, data);
    return std::make_unique<AutoBackend>(engine, library, data);  // "auto", leeg of onbekend
}

}  // namespace rg
