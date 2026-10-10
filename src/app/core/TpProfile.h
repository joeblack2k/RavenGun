// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Een TeknoParrot-profiel (UserProfiles/<P>.xml of een sjabloon in GameProfiles/) lezen en velden wijzigen.
//
// Het bestand wordt als tekst bewerkt (zoals scripts/tp-add-game.py doet): alleen de inhoud van <FieldValue> in het juiste
// <FieldInformation>-blok verandert, de rest van het bestand (opmaak, bindingen) blijft byte voor byte gelijk. Schrijven gaat
// via rg::writeTextAtomic (nooit naar de NAS) met een eenmalige back-up "<P>.xml.ravengun-bak".
#include <QList>
#include <QString>
#include <QStringList>

namespace rg {

struct TpField {
    QString category;   // <CategoryName> (kan leeg zijn)
    QString name;       // <FieldName>
    QString value;      // <FieldValue> (leeg als het ontbreekt)
    QString type;       // Bool, Text, Dropdown, Slider, ...
    QStringList options;  // <FieldOptions><string>...
    QString hint;
    bool hasMin = false, hasMax = false;
    int min = 0, max = 0;
};

class TpProfile {
public:
    bool load(const QString& path, QString* err = nullptr);
    bool loadText(const QString& text);  // voor tests
    bool save(QString* err = nullptr);   // naar path()
    bool saveAs(const QString& path, QString* err = nullptr);

    const QString& path() const { return path_; }
    const QString& text() const { return text_; }

    // Eerste <tag>waarde</tag> (XML-entiteiten opgelost, getrimd); leeg als het ontbreekt.
    QString tag(const QString& name) const;
    QList<TpField> fields() const;
    // Waarde van een veld; category leeg = eerste veld met die naam. nullptr-achtig: found=false.
    QString fieldValue(const QString& name, const QString& category = {}, bool* found = nullptr) const;
    bool hasField(const QString& name, const QString& category = {}) const;
    // Zet de waarde; false als het veld niet bestaat.
    bool setFieldValue(const QString& name, const QString& value, const QString& category = {});

    // Uitvoerbare namen uit <ExecutableName> en <ExecutableName2> (gesplitst op | en ;), alleen bestandsnamen, kleine letters.
    QStringList executableNames() const;

    static QString xmlEscape(const QString& s);
    static QString xmlUnescape(const QString& s);

private:
    QString path_;
    QString text_;
};

}  // namespace rg
