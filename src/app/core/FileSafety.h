// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Veilig schrijven: de app schrijft alleen naar lokale bestanden, nooit naar de NAS (docs/decisions.md, "Qt-app").
//
// - isOnNetwork(): ligt een pad (na het volgen van symlinks) op een netwerkbestandssysteem (CIFS/SMB/NFS), of onder
//   een van de NAS-voorvoegsels (standaard ~/mnt; extra via $RAVENGUN_NAS_PREFIXES, met ':' gescheiden, voor tests)?
// - writableTarget(): het pad waarnaar de app een bestand mag schrijven. Een gewoon lokaal bestand: zichzelf. Een symlink
//   in een lokale view-map die naar de NAS wijst: eerst een lokale kopie (overlay) die de symlink vervangt, met
//   `allowOverlay`. Ligt het bestand zelf op de NAS: geweigerd.
// - readJson()/writeJsonAtomic(): JSON lezen en atomisch schrijven (QSaveFile), met een eenmalige back-up.
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace rg {

bool isOnNetwork(const QString& path);

struct WriteCheck {
    bool ok = false;
    bool needsOverlay = false;  // het doel is een symlink naar de NAS in een lokale map: eerst een lokale kopie
    QString path;               // het werkelijke pad dat geschreven wordt
    QString reason;             // Engels, voor de gebruiker (bij !ok)
};
WriteCheck checkWritable(const QString& path);
// Maakt van een symlink naar de NAS een lokale kopie (zelfde inhoud), zodat schrijven de NAS niet raakt.
bool makeLocalOverlay(const QString& linkPath, QString* err);

QJsonDocument readJson(const QString& path, QString* err = nullptr);
// Schrijft atomisch; `backupSuffix` niet leeg = vóór de eerste wijziging een kopie <pad><suffix> (alleen als die nog niet bestaat).
bool writeJsonAtomic(const QString& path, const QJsonObject& obj, QString* err = nullptr, const QString& backupSuffix = {});
bool writeTextAtomic(const QString& path, const QByteArray& data, QString* err = nullptr, const QString& backupSuffix = {});

}  // namespace rg
