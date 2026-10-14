// SPDX-License-Identifier: GPL-3.0-or-later
// Leesbare (Engelse) namen voor de gamepad-indeling (docs/gamepad.md): wat een bediening doet en waar de keuze vandaan komt. Voor
// het venster Controller Settings (Gamepads) en Properties (General > Gamepad).
#pragma once
#include <QCoreApplication>
#include <QString>

namespace rg {

class GamepadText {
    Q_DECLARE_TR_FUNCTIONS(GamepadText)
public:
    // "wheel" -> "Steering wheel", "btn3" -> "Cabinet button 3 (btn3)", "key:f2" -> "Key F2", ...
    static QString target(const QString& t) {
        if (t == QLatin1String("wheel")) return tr("Steering wheel (wheel)");
        if (t == QLatin1String("gas")) return tr("Gas pedal (gas)");
        if (t == QLatin1String("brake")) return tr("Brake pedal (brake)");
        if (t == QLatin1String("trigger")) return tr("Trigger");
        if (t == QLatin1String("reload")) return tr("Reload");
        if (t == QLatin1String("grenade")) return tr("Grenade");
        if (t == QLatin1String("aux1")) return tr("Extra gun button 1 (aux1)");
        if (t == QLatin1String("aux2")) return tr("Extra gun button 2 (aux2)");
        if (t == QLatin1String("start")) return tr("Start (key 1 for player 1, 2 for player 2, ...)");
        if (t == QLatin1String("coin")) return tr("Coin (key 5)");
        if (t == QLatin1String("none")) return tr("Nothing (none)");
        if (t.startsWith(QLatin1String("btn"))) return tr("Cabinet button %1 (%2)").arg(t.mid(3), t);
        if (t.startsWith(QLatin1String("key:"))) return tr("Key %1").arg(t.mid(4).toUpper());
        return t;
    }
    // "default" | "shipped" | "user"
    static QString source(const QString& s) {
        if (s == QLatin1String("user")) return tr("Yours (user game.ini)");
        if (s == QLatin1String("shipped")) return tr("Game (shipped game.ini)");
        return tr("Default");
    }
};

}  // namespace rg
