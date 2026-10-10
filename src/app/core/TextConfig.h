// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Kleine, opmaakbewarende bewerkers voor tekstconfiguratie: INI ([sectie] sleutel=waarde) en sleutel=waarde-regels
// (zoals UserProfiles/<P>.env). Alleen de betreffende regel verandert; commentaar en volgorde blijven staan.
#include <QString>

namespace rg {

namespace ini {
// Waarde van sleutel in sectie (hoofdletterongevoelig; sectie leeg = vóór de eerste sectie). found=false als hij ontbreekt.
QString get(const QString& text, const QString& section, const QString& key, bool* found = nullptr);
// Zet of voegt toe (sectie wordt zo nodig aan het eind gemaakt). Geeft de nieuwe tekst.
QString set(const QString& text, const QString& section, const QString& key, const QString& value);
}  // namespace ini

namespace keyvalue {
// Regels "KEY<sep>WAARDE" ('#' en ';' aan het begin = commentaar).
QString get(const QString& text, const QString& key, bool* found = nullptr, QChar sep = QLatin1Char('='));
QString set(const QString& text, const QString& key, const QString& value, QChar sep = QLatin1Char('='));
QString remove(const QString& text, const QString& key, QChar sep = QLatin1Char('='));
}  // namespace keyvalue

}  // namespace rg
