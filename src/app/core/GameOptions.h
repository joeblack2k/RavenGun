// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// packaging/game-options.json: per game instelbare opties en "memory card"-bestanden. Schema en uitleg:
// docs/architecture.md, "Qt-app", en het voorbeeld tests/app/data/game-options.json.
//
// Verwachte vorm (schema 1, zoals gegenereerd voor de app):
//   {"schema":1, "games": {"<id>": {"title", "tp_profile", "nas_folder", "platform", "exe", "recommended_route",
//     "status_en", "notes_en", "options": [{"key", "name_en", "type": bool|int|enum|string, "values", "min", "max",
//     "default", "description_en", "note_en", "target": {...}}], "memory_card": {"items": [{"id", "path", "base", "kind",
//     "description_en", "safe_to_erase": yes|no|unknown, "erase_effect_en"}], "notes_en"}}}}
// Doelen: tp_profile_field (category, field), tp_profile_attr (element), launch_env (name, companion "K=V"),
// game_config_file (path, base game_dir, format ini|ini_onoff|ini_yesno|tab_ini|space_ini|colon, section, key,
// edit_outside_game; andere formaten zoals rtdt_adjval, csv, xml en shell_arg zijn alleen-lezen). Ook toegestaan (ruimer, voor
// handwerk en tests): games als lijst, name/label/description, memory_card als lijst, en doelen als korte tekst
// ("tp:Categorie/Veld", "env:VAR", "ini:BESTAND#Sectie/Sleutel", "cfg:BESTAND#Sleutel"). Onbekende velden worden genegeerd.
//
// Lezen en schrijven van een waarde gaat via OptionStore, alleen naar lokale bestanden (rg::checkWritable): een bestand op
// de NAS is "read-only (on NAS)"; een symlink in een lokale view-map naar de NAS wordt eerst een lokale kopie (overlay).
// Bron van waarheid is de bibliotheek (library.json); deze bestanden zijn uitvoer (rg::ProfileSync).
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "ravengun/gamesettings/gamesettings.hpp"

namespace rg {

struct OptionTarget {
    enum class Kind { TpField, TpTag, Env, Ini, KeyValue, Unsupported };
    Kind kind = Kind::Unsupported;
    QString category;  // TpField (optioneel)
    QString field;     // TpField: FieldName; TpTag: element; Env: variabele; Ini/KeyValue: sleutel
    QString file;      // Ini/KeyValue: bestand (relatief aan de gamemap, of ~/... of absoluut; {placeholders})
    QString section;   // Ini
    QString companion; // Env: extra "K=V" dat meegaat als de waarde aan staat (bool)
    QChar sep = QLatin1Char('=');  // KeyValue: scheidingsteken (tab_ini: tab, space_ini: spatie, colon: dubbele punt)
    QString trueWord, falseWord;   // ini_onoff / ini_yesno: woorden voor een bool (ON/OFF, YES/NO)
    QString readOnlyReason;  // niet leeg = alleen tonen (bv. "change it in the game's own test menu")
    QString raw;       // oorspronkelijke tekst, voor meldingen
    QString describe() const;  // Engels, voor de tooltip
};

struct GameOption {
    QString key, name, type;  // type: bool | int | enum | string
    QString description, category;
    QStringList values;       // enum: opgeslagen waarden
    QStringList labels;       // enum: weergave (zelfde lengte als values)
    bool hasRange = false;
    double min = 0, max = 0, step = 1;
    QString defaultValue;     // als tekst in de opslagvorm (bool: trueValue/falseValue)
    QString trueValue = QStringLiteral("1"), falseValue = QStringLiteral("0");
    OptionTarget target;
};

struct MemoryCardSpec {
    QString id;
    QString path;   // bestand, map of glob (* en ?), met ~, {placeholders} en {a,b}-reeksen
    QString base;   // game_dir (standaard) | view_dir | tp_dir | home | wine_prefix (niet ondersteund)
    QString label;  // beschrijving
    QString kind;   // savegame | nvram | highscores | bookkeeping | credits | operator_settings | state | log | protected_data
    QString safeToErase = QStringLiteral("yes");  // yes | no | unknown
    QString eraseEffect;
    bool local = false;  // expliciet lokaal gemarkeerd (ruimer schema)
};

struct GameOptionsEntry {
    QString id;        // sleutel in games
    QString profile;   // tp_profile (kan leeg zijn: nog geen profiel)
    QString title, nasFolder, platform, exe, route;
    QString note;      // status_en / notes_en / note
    QString memoryNotes;
    QList<GameOption> options;
    QList<MemoryCardSpec> memoryCard;
};

class GameOptionsDb {
public:
    bool load(const QString& path, QString* err = nullptr);
    bool loadData(const QByteArray& json, QString* err = nullptr);
    const GameOptionsEntry* find(const QString& profile) const;          // op tp_profile
    const GameOptionsEntry* findByFolder(const QString& folderName) const;  // op nas_folder (mapnaam)
    const QList<GameOptionsEntry>& entries() const { return entries_; }
    const QStringList& warnings() const { return warnings_; }
    bool isEmpty() const { return entries_.isEmpty(); }

    static OptionTarget parseTarget(const QVariant& v);
    // "a/{0,1,2}.txt" -> a/0.txt, a/1.txt, a/2.txt (meerdere reeksen: alle combinaties).
    static QStringList expandBraces(const QString& pattern);

private:
    QList<GameOptionsEntry> entries_;
    QStringList warnings_;
};

// Waar een game staat, om doelen en placeholders op te lossen.
struct GameContext {
    QString profile;      // profielnaam
    QString tpDir;        // runtime-map (UserProfiles/)
    QString gameDir;      // map van het uitvoerbare bestand zoals de game draait (een view-map als die er is)
    QString rootDir;      // de gamemap ("game_dir" in game-options.json); zonder gegevens = gameDir
    QString realGameDir;  // de werkelijke map van het uitvoerbare bestand (op de NAS)
    QString viewDir;      // ~/.local/share/ravengun/runtime/views/<profiel> (kan ontbreken)
    // Heeft de game een eigen profiel in game.ini (docs/per-game-patches.md)? Dan gaan de opties van het profiel (velden, profielvelden en
    // omgevingsvariabelen) naar de gebruikerslaag van game.ini, en alleen als ze afwijken van de meegeleverde waarde; UserProfiles/<P>.xml en
    // <P>.env worden niet meer geschreven. Zonder eigen profiel (ownProfile = false) blijft het oude gedrag.
    bool ownProfile = false;
    ravengun::gamesettings::Dirs layers;   // meegeleverde en gebruikerslaag
    QString userGameIni() const;           // <gebruiker>/<profiel>/game.ini (leeg zonder gebruikerslaag)
    QString userProfilePath() const;
    QString envPath() const;
    QString expand(const QString& pattern) const;  // {profile} {tp_dir} {game_dir} {real_game_dir} {view_dir} {home} en ~
    QString resolve(const QString& path, const QString& base = {}) const;  // relatief pad naar absoluut (leeg = onbekend)
};

class OptionStore {
public:
    // Het bestand waar dit doel naartoe gaat (leeg als onbekend).
    static QString targetFile(const OptionTarget& t, const GameContext& c);
    // Huidige waarde (opslagvorm); present=false als het doel (nog) niets heeft: dan geldt de standaard.
    static QString read(const GameOption& o, const GameContext& c, bool* present = nullptr, QString* err = nullptr);
    // Mag dit doel geschreven worden? reason (Engels) bij nee, bv. "read-only (on NAS)".
    static bool writable(const GameOption& o, const GameContext& c, QString* reason = nullptr);
    static bool write(const GameOption& o, const GameContext& c, const QString& value, QString* err = nullptr);
    // Eigen profiel: zet [sectie] sleutel in de gebruikerslaag, en haalt de sleutel weg als de waarde gelijk is aan de meegeleverde (de
    // gebruikerslaag bevat alleen verschillen, zoals Dolphin's User/GameSettings). Een lege waarde heft een meegeleverde waarde op.
    // implicitDefault: de waarde die geldt als de meegeleverde laag de sleutel niet zet (bv. "false" voor [Dongle] Enabled); gelijk eraan = weghalen.
    static bool writeSetting(const GameContext& c, const QString& section, const QString& key, const QString& value, QString* err = nullptr,
                             const QString& implicitDefault = {});
    // De waarde van [sectie] sleutel zoals ze nu geldt (alle lagen); present=false als de sleutel nergens staat.
    static QString readSetting(const GameContext& c, const QString& section, const QString& key, bool* present = nullptr);
};

}  // namespace rg
