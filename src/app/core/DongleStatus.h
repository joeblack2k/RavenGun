// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Status van de echte, fysieke dongle (USB pass-through) voor Properties > Options > Dongle. De detectie zelf doet scripts/dongle.py
// (sysfs: alleen vendor/product-ID en de leesbare namen, nooit het serienummer; plus de controle of de officiële runtime van de fabrikant
// draait); de app roept dat aan met `python3 -I scripts/dongle.py --json` en toont de uitkomst. Er is hier niets dat een dongle nabootst.
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace rg {

struct DongleInfo {
    QString vendorId, productId;     // 4 hexcijfers, kleine letters
    QString manufacturer, product;   // zoals het apparaat zich noemt (kan leeg zijn)
    QString vendorName;              // uit packaging/dongle-vendors.json
    QString runtimeName;             // "Sentinel LDK Runtime (Thales)"; leeg als de runtime van deze fabrikant onbekend is
    QString runtimeStatus;           // "running" | "not running" | "not installed" | "unknown"
    QString installUrl;              // officiële installatie van de fabrikant (voor een ontbrekende runtime)
    QString label() const;           // "<fabrikant> <product>" (of de naam uit het gegevensbestand)
    QString line() const;            // "<label> (vvvv:pppp) — runtime: running"
};

struct DongleStatus {
    bool valid = false;        // de uitvoer van dongle.py kon gelezen worden
    bool ok = false;           // dongle gevonden en runtime in orde (of onbekend)
    QString error;             // reden als !valid (Engels)
    QString message;           // de melding van dongle.py (Engels), bij een probleem met een verwijzing naar de officiële installatie
    QList<DongleInfo> dongles;
    // De statusregel: "Dongle detected: <fabrikant> <product> (vvvv:pppp) — runtime: running" of "No dongle found".
    QString text() const;
    // Uitleg voor de tooltip (de melding van dongle.py).
    QString tooltip() const;
};

// De bronnen die "Dongle source" aanbiedt: (waarde in game.ini, tekst in de app). Uitbreidbaar; nu alleen USB pass-through.
struct DongleSource {
    QString value, label;
};
const QList<DongleSource>& dongleSources();
QString dongleSourceLabel(const QString& value);   // de waarde zelf als hij onbekend is

DongleStatus parseDongleJson(const QByteArray& output);   // output mag ruis vóór de eerste '{' bevatten

// Draait scripts/dongle.py (python3 -I) en roept done in de GUI-thread aan.
void detectDongle(QObject* context, std::function<void(const DongleStatus&)> done);

}  // namespace rg
