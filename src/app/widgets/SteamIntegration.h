// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// "Add to Steam" (A8): de app als niet-Steam-game ("RavenGun", met --fullscreen) in de Steam-bibliotheek van de gebruiker.
// Het eigenlijke werk doet rg::SteamShortcuts (core); hier de vragen aan de gebruiker: welke Steam-gebruiker, Steam sluiten
// (nooit zonder bevestiging, en dan netjes met `steam -shutdown`), en het resultaat.
#include <QString>

#include "core/SteamShortcuts.h"

class QWidget;

namespace rg::ui {

struct SteamAddOutcome {
    bool ok = false;
    QString message;  // Engels, voor de gebruiker
};

// De gegevens van de snelkoppeling voor deze installatie (pad van de app, startmap, icoon in ~/.local/share/ravengun).
rg::ShortcutSpec RavenGunShortcutSpec(bool writeIcon = true);

// Zonder vragen (ook voor `ravengun --add-to-steam`): weigert als Steam draait. account leeg = de laatst ingelogde gebruiker.
SteamAddOutcome AddToSteamQuiet(const QString& account = {});

// Met vragen (Tools > Add to Steam en de wizard).
SteamAddOutcome AddToSteamInteractive(QWidget* parent);

// Staat RavenGun al in de bibliotheek van de laatst ingelogde gebruiker? (alleen lezen)
bool IsInSteam(QString* where = nullptr);

}  // namespace rg::ui
