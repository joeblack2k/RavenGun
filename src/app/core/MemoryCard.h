// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// "Memory card" van een game: spelstanden, NVRAM en highscores.
//
// Bronnen: (1) de items uit game-options.json (memory_card), en (2) de view-map van het profiel
// (~/.local/share/ravengun/runtime/views/<profiel>): alles wat daar een echt bestand is en geen symlink, heeft de game
// zelf lokaal gemaakt (scripts/tp-add-game.py --view: "nieuwe bestanden die de game in de view maakt blijven lokaal").
// Wissen kan alleen bij lokale bestanden die als wisbaar zijn gemarkeerd (safe_to_erase yes of unknown); een bestand op de
// NAS wordt alleen getoond, ook als game-options.json het als lokaal markeert (de app schrijft nooit naar de NAS).
#include <QDateTime>
#include <QList>
#include <QString>

#include "core/GameOptions.h"

namespace rg {

struct MemoryCardFile {
    QString path;
    QString label;   // uit game-options.json of "Created by the game (local view)"
    QString kind;
    QString safeToErase = QStringLiteral("yes");  // yes | no | unknown
    QString eraseEffect;
    qint64 size = 0;
    QDateTime modified;
    bool onNas = false;
    bool deletable = false;
    QString reason;  // waarom niet wisbaar (Engels)
};

class MemoryCard {
public:
    // Hoogstens `maxFiles` bestanden; mappen worden recursief (diepte 4) doorlopen.
    static QList<MemoryCardFile> list(const GameContext& c, const QList<MemoryCardSpec>& specs, int maxFiles = 500);
    // Wist een bestand uit de lijst (alleen als deletable; opnieuw gecontroleerd). err in het Engels.
    static bool remove(const MemoryCardFile& f, QString* err = nullptr);
};

}  // namespace rg
