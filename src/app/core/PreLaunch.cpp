// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PreLaunch.h"

#include <QJsonArray>
#include <QSettings>

#include "core/AppPaths.h"

namespace rg::prelaunch {

namespace {
QList<int> ints(const QJsonValue& v) {
    QList<int> out;
    for (const auto& x : v.toArray())
        if (x.isDouble() && x.toInt() >= 1 && x.toInt() <= 4) out << x.toInt();
    return out;
}

QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}
}  // namespace

Plan parsePlan(const QJsonObject& o) {
    Plan p;
    if (!o.contains(QStringLiteral("lightgun")) || !o.contains(QStringLiteral("ready"))) return p;
    p.known = true;
    p.lightgun = o.value(QStringLiteral("lightgun")).toBool(true);
    p.gunPlayers = o.value(QStringLiteral("gun_players")).toInt(4);
    p.ready = o.value(QStringLiteral("ready")).toBool(true);
    p.waiting = ints(o.value(QStringLiteral("waiting")));
    p.waitingAny = o.value(QStringLiteral("waiting_any")).toBool();
    p.optionalOff = ints(o.value(QStringLiteral("optional_off")));
    p.calibrate = ints(o.value(QStringLiteral("calibrate")));
    return p;
}

QString waitingText(const QList<int>& players, bool any) {
    if (any || players == QList<int>{1}) return QStringLiteral("Please turn on your PS Move controller");
    if (players.isEmpty()) return {};
    QStringList who;
    for (int p : players) who << QString::number(p);
    return QStringLiteral("Player %1: please turn on your PS Move controller").arg(who.join(QStringLiteral(" and ")));
}

QString waitingTextFromStatus(const QJsonObject& st) {
    const QList<int> players = ints(st.value(QStringLiteral("waiting_players")));
    return waitingText(players, players.isEmpty());
}

QColor playerColor(int player) {
    switch (player) {
        case 2: return QColor(235, 40, 40);
        case 3: return QColor(40, 200, 60);
        case 4: return QColor(235, 215, 30);
        default: return QColor(40, 110, 255);
    }
}

QString ledPreset(int player) {
    switch (player) {
        case 2: return QStringLiteral("rood");
        case 3: return QStringLiteral("groen");
        case 4: return {};   // geel: geen preset, rgb 255,255,0
        default: return QStringLiteral("blauw");
    }
}

bool calibrateEnabled() { return settings().value(QStringLiteral("prelaunch/calibrate"), true).toBool(); }

void setCalibrateEnabled(bool on) {
    settings().setValue(QStringLiteral("prelaunch/calibrate"), on);
    settings().sync();
}

bool needsScreen(const Plan& p, bool calibrate, bool controlsSetup) {
    if (!p.known || !p.lightgun) return false;
    if (!p.ready || controlsSetup) return true;
    return calibrate && !p.calibrate.isEmpty();
}

}  // namespace rg::prelaunch
