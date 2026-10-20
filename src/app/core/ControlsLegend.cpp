// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ControlsLegend.h"

#include <QFile>
#include <QJsonDocument>

#include "core/AppPaths.h"
#include "core/ControlsWalk.h"

namespace rg::controls {

namespace gs = ravengun::gamesettings;

std::map<std::string, std::string> defaultLayout() {
    return {{"move", "grenade"}, {"cross", "reload"},  {"triangle", "key:5"}, {"square", "key:1"}, {"select", "key:5"},
            {"start", "key:1"},  {"circle", "none"},   {"pump", "reload"},    {"ss_rl", "reload"}};
}

namespace {
void overlay(std::map<std::string, std::string>& out, const QJsonObject& map) {
    for (auto it = map.begin(); it != map.end(); ++it)
        if (it.value().isString()) out[it.key().toStdString()] = it.value().toString().toStdString();
}
}  // namespace

std::map<std::string, std::string> baseLayout(const QJsonObject& engineStatus) {
    std::map<std::string, std::string> out = defaultLayout();
    const QJsonObject fromEngine = engineStatus.value(QStringLiteral("gun_buttons")).toObject().value(QStringLiteral("map")).toObject();
    if (!fromEngine.isEmpty()) {
        overlay(out, fromEngine);
        return out;
    }
    QFile f(rg::AppPaths::engineConfigFile());
    if (f.open(QIODevice::ReadOnly))
        overlay(out, QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("gun_buttons")).toObject().value(QStringLiteral("map")).toObject());
    return out;
}

std::map<std::string, std::string> effectiveLayout(const std::map<std::string, std::string>& base, const gs::Ini& merged, int player) {
    std::map<std::string, std::string> out = base;
    for (const auto& kv : gs::controls_overrides(merged, player)) out[kv.first] = kv.second;
    return out;
}

QString legendButtonName(const std::string& b) {
    if (b == "move") return QStringLiteral("MOVE");
    if (b == "ss_rl") return QStringLiteral("RL button");
    if (b == "start") return QStringLiteral("Start");
    return QString::fromStdString(rg::ControlsWalk::buttonName(b));
}

namespace {

// The buttons (in the fixed order of ControlsWalk::allButtons) whose action is `action`, as "A / B".
QString buttonsFor(const std::map<std::string, std::string>& layout, const std::string& action) {
    QStringList names;
    for (const auto& b : rg::ControlsWalk::allButtons()) {
        auto it = layout.find(b);
        if (it != layout.end() && it->second == action) names << legendButtonName(b);
    }
    return names.join(QStringLiteral(" / "));
}

QString joined(const QString& a, const QString& b) {
    if (a.isEmpty()) return b;
    if (b.isEmpty()) return a;
    return a + QStringLiteral(" / ") + b;
}

}  // namespace

QList<LegendRow> legend(const gs::Ini& merged, const std::map<std::string, std::string>& layout) {
    const gs::ControlsInfo info = gs::controls_info(merged);
    auto declared = [&](const char* signal) -> const gs::ControlSignal* {
        for (const auto& e : info.entries) if (e.signal == signal) return &e;
        return nullptr;
    };
    auto label = [](const gs::ControlSignal* s, const QString& fallback) {
        return s && !s->label.empty() ? QString::fromStdString(s->label) : fallback;
    };
    QList<LegendRow> rows;
    auto add = [&rows](const QString& buttons, const QString& text) {
        if (!buttons.isEmpty()) rows.push_back({buttons, text});
    };

    // The trigger always shoots; with a Sharp Shooter the same word covers its fire button (the engine decides which bit).
    add(QStringLiteral("Trigger"), label(declared("trigger"), QStringLiteral("Shoot")));

    // Reload. The game's off-screen signal ("aim away and pull the trigger") reloads too: when the game declares no separate reload it
    // shares the line with the buttons that reload (the pump); when it declares both, they are two lines.
    const gs::ControlSignal* reload = declared("reload");
    const gs::ControlSignal* off = declared("offscreen");
    const QString reloadButtons = buttonsFor(layout, gs::control_action("reload", 1));
    if (off && !reload) {
        add(joined(QStringLiteral("Aim off-screen + Trigger"), reloadButtons), label(off, QStringLiteral("Reload")));
    } else {
        if (off) add(QStringLiteral("Aim off-screen + Trigger"), label(off, QStringLiteral("Reload")));
        if (reload) add(reloadButtons, label(reload, QStringLiteral("Reload")));
    }
    for (const char* sig : {"grenade", "aux1", "aux2"}) {
        const gs::ControlSignal* s = declared(sig);
        if (!s) continue;
        const QString name = !s->label.empty() ? QString::fromStdString(s->label)
                           : std::string(sig) == "grenade" ? QStringLiteral("Grenade")
                           : QStringLiteral("Extra button");
        add(buttonsFor(layout, gs::control_action(sig, 1)), name);
    }

    // The arcade buttons every game has, from what the layout gives them.
    add(buttonsFor(layout, gs::control_action("start", 1)), QStringLiteral("Start"));
    add(buttonsFor(layout, gs::control_action("coin", 1)), QStringLiteral("Coin"));
    add(buttonsFor(layout, "key:f2"), QStringLiteral("TEST"));
    add(buttonsFor(layout, "key:f3"), QStringLiteral("Service"));
    // The PS button always recenters (hard-wired in the engine); the layout may give more buttons the same job.
    add(joined(buttonsFor(layout, "recenter"), QStringLiteral("PS")), QStringLiteral("Recenter the aim"));
    return rows;
}

}  // namespace rg::controls
