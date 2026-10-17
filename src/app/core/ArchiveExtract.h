// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Uitpakken van tar.gz, tar.xz, zip, pkg.tar.zst, cab en deb (Debian/Ubuntu-pakketten: het data.tar-lid; ook .deb's in een tar.gz) met libarchive, zonder tar of unzip aan te roepen (geen shell). Veilig tegen
// paden buiten de doelmap: ".." en absolute paden worden geweigerd, symlinks mogen niet naar buiten wijzen.
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace rg {

struct ExtractOptions {
    int strip = 0;                 // aantal voorste padonderdelen dat wegvalt (zoals tar --strip-components)
    QString subdir;                // alleen wat onder deze map staat (na strip), met die map eraf
    QStringList include;           // alleen paden die hiermee beginnen (na strip/subdir); zonder "/" aan het eind moet het pad exact passen
    QList<QPair<QString, QString>> map;   // exact pad in het archief -> doelpad (relatief aan de doelmap); werkt naast include
    bool nestedDebs = false;       // het archief (tar.gz) bevat .deb-pakketten: pak die uit en pas include/map op hun inhoud toe (PhysX van NVIDIA)
    bool flattenLower = false;     // alleen bestanden, zonder mappen, bestandsnaam in kleine letters (cab-bestanden met DLL's)
    QString nameFilterSuffix;      // met flattenLower: alleen namen met dit achtervoegsel (bv. ".dll")
    const std::atomic_bool* cancel = nullptr;
    std::function<void(qint64 done, qint64 total)> progress;   // gelezen (gecomprimeerde) bytes van het archief
};

// Pakt `archive` uit naar `destDir` (wordt aangemaakt). Geeft false met een Engelse melding in err bij een fout of bij annuleren.
bool extractArchive(const QString& archive, const QString& destDir, const ExtractOptions& opt, QString* err);

// Namen van de bestanden in een archief; leeg bij een fout.
QStringList listArchive(const QString& archive, QString* err = nullptr);

}  // namespace rg
