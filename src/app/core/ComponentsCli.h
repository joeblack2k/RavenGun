// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Onderdelen tonen en installeren zonder venster (`ravengun --components`, `--install-component`): dezelfde ComponentInstaller als de
// Components-pagina, met tekstuitvoer. Geen shellscripts.
#include <QList>
#include <QString>
#include <QStringList>
#include <cstdio>

#include "core/Components.h"

namespace rg {

class ComponentsCli {
public:
    // Tekst van een stand: installed | missing | outdated | not available.
    static QString stateName(ComponentState s);
    // Eén regel per onderdeel: id, naam, versie, grootte, stand (tab-gescheiden). Geeft 0.
    static int list(const QList<Component>& all, const ComponentEnv& env, std::FILE* out);
    // "a,b" en herhaalde opties tot één lijst (leeg weggelaten, dubbelen eruit).
    static QStringList splitIds(const QStringList& values);
    // "all" wordt: alles wat ontbreekt of verouderd is. Onbekend of niet beschikbaar id: false met melding in err.
    static bool resolveIds(const QList<Component>& all, const ComponentEnv& env, const QStringList& ids, QStringList* out, QString* err);
    // Installeert (met afhankelijkheden), toont voortgang (hoogstens elke 5%) en draait zelf een gebeurtenislus. 0 = gelukt, 1 = mislukt.
    static int install(const QList<Component>& all, const ComponentEnv& env, const QStringList& ids, std::FILE* out, std::FILE* err);
};

}  // namespace rg
