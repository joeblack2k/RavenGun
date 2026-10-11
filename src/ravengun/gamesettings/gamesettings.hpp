// SPDX-License-Identifier: GPL-3.0-or-later
// Instellingen en patches per game (docs/per-game-patches.md): patches/<Game>/game.ini en patches.ini, met drie lagen die per sleutel
// worden samengevoegd (een latere laag wint, een lege waarde haalt de sleutel weg):
//   <meegeleverd>/_defaults/<Type>.ini   standaard per soort emulator
//   <meegeleverd>/<Game>/game.ini        meegeleverd (repo: patches/, installatie: <datadir>/patches/)
//   <gebruiker>/<Game>/game.ini          $XDG_DATA_HOME/ravengun/patches (standaard ~/.local/share/ravengun/patches)
// Patches (patches.ini, secties "[Patch: <naam>]") volgen dezelfde lagen, met onderaan de gedeelde definities in
// <meegeleverd>/_defaults/<Type>.patches.ini.
//
// Alleen de standaardbibliotheek, zodat engine en app (Qt) hetzelfde lezen. Het INI-formaat is gelijk aan dat van
// scripts/gamesettings.py (Python, configparser): sectienamen en sleutels hoofdlettergevoelig; commentaar op een regel die met # of ;
// begint, of een ';' na witruimte achter een waarde; geen waarden over meerdere regels; voorloop-witruimte wordt genegeerd.
// tests/data/gamesettings-cases.ini wordt door beide gelezen en vergeleken.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ravengun::gamesettings {

struct Section {
    std::string name;
    std::vector<std::pair<std::string, std::string>> kv;   // in volgorde van voorkomen
};

class Ini {
public:
    std::vector<Section> sections;
    const Section* section(const std::string& name) const;
    const std::string* find(const std::string& sec, const std::string& key) const;   // nullptr als hij ontbreekt
    std::string get(const std::string& sec, const std::string& key, const std::string& def = {}) const;
    bool has(const std::string& sec, const std::string& key) const { return find(sec, key) != nullptr; }
    void set(const std::string& sec, const std::string& key, const std::string& value);
    void erase(const std::string& sec, const std::string& key);
};

// Leest INI-tekst. Regels die niet te begrijpen zijn (sleutel zonder '=', sleutel buiten een sectie) vervallen met een waarschuwing.
Ini parse_ini(const std::string& text, std::vector<std::string>* warnings = nullptr);
// Voegt `over` per sleutel in `base` samen; een lege waarde haalt de sleutel weg.
void merge_into(Ini& base, const Ini& over);

bool truthy(const std::string& v);   // true, 1, yes, on (hoofdletters maken niet uit)

// [Dongle] (docs/per-game-patches.md, "Dongle per game"): waar de dongle vandaan komt. Dezelfde regels als Game.dongle() in
// scripts/gamesettings.py (tests/data/dongle-cases.json). Standaard uit en usb-passthrough; een Enabled die geen true of false is geldt als aan
// met een fout; een onbekende Source is een fout (alleen van belang als Enabled aan staat). Twee bronnen: usb-passthrough (de fysieke dongle van
// de eigenaar) en drop-in (de drop-in uit patches/<Game>/; welke soort het is staat in dongle.json, docs/dropins.md).
extern const char* const kDongleSources[];   // usb-passthrough, drop-in
struct Dongle {
    bool enabled = false;
    std::string source;   // kleine letters
    std::string error;    // leeg = in orde
};
Dongle dongle_of(const Ini& settings);

// Categorieen (docs/per-game-patches.md, "Categorie en beleid"): elke patch heeft er precies een, uit deze vaste lijst. De laatste vijf
// horen bij de afhandeling per game in de map van de game zelf (dongle, regio, keychip, integriteit, activering; docs/dropins.md).
extern const char* const kPatchCategories[];   // graphics, input, timing, audio, paths, crash, network, loader, dongle, region, keychip, integrity, activation
std::string patch_categories_text();            // "graphics, input, ..." voor meldingen

struct Patch {
    std::string name;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string get(const std::string& key, const std::string& def = {}) const;
    bool enabled() const { return truthy(get("Enabled", "false")); }
    std::string kind() const;     // file | registry | memory (kleine letters), leeg als niet gezet
    std::string reason() const { return get("Reason"); }
    std::string category() const;    // kleine letters
    std::string version() const;     // gameversie waarvoor de patch bedoeld is (optioneel)
    std::string applied_by() const;  // "core" = de OpenParrot-kern past dit zelf al toe (alleen documentatie)
    // "" als de patch geldig is, anders een duidelijke melding (Nederlands). Gaat over de samengevoegde patch: elke laag telt mee.
    std::string validate() const;
};

struct PatchError {
    std::string name;
    std::string message;
    bool enabled = false;
};

struct Dirs {
    std::string shipped;   // <datamap>/patches
    std::string user;      // gebruikerslaag; leeg = geen
};

// $RAVENGUN_PATCHES_DIR of <data_dir>/patches; $RAVENGUN_USER_PATCHES_DIR of $XDG_DATA_HOME/ravengun/patches (anders
// $HOME/.local/share/ravengun/patches).
Dirs default_dirs(const std::string& data_dir);

struct Game {
    std::string profile;
    std::string type;                   // [Game] Type (de gebruikerslaag gaat voor), anders de hint
    Ini settings;                       // samengevoegd (standaard + meegeleverd + gebruiker)
    std::vector<Patch> patches;         // in volgorde van voorkomen (alleen geldige patches)
    std::vector<PatchError> patch_errors;   // afgewezen patches: geen Kind, ontbrekende of onbekende Category
    std::vector<std::string> sources;   // gelezen bestanden
    std::vector<std::string> warnings;
    bool exists = false;                // er is een game.ini in een van beide lagen
    std::string get(const std::string& sec, const std::string& key, const std::string& def = {}) const { return settings.get(sec, key, def); }
    const Patch* patch(const std::string& name) const;
};

Game load_game(const Dirs& dirs, const std::string& profile, const std::string& type_hint = {});
// Eén laag zonder samenvoegen: game.ini of patches.ini van een game in een map (leeg als het bestand ontbreekt). Voor weergave
// ("deze waarde komt uit de gebruikerslaag") en om te zien of de gebruiker een sleutel zelf zet.
Ini read_layer(const std::string& layer_dir, const std::string& profile, const std::string& file = "game.ini");
// Namen van de games met een map in een van beide lagen (zonder _-mappen), gesorteerd zonder hoofdletters.
std::vector<std::string> list_games(const Dirs& dirs);

// Tekst van een INI-bestand met één sleutel gezet (value) of weggehaald (erase); de rest (commentaar, volgorde) blijft staan.
std::string edit_ini_text(const std::string& text, const std::string& section, const std::string& key, const std::string& value, bool erase);

// Gebruikerslaag schrijven (de app). Maken de map aan; geven false met een Engelse melding in err bij een fout.
bool set_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key,
                    const std::string& value, std::string* err = nullptr);
bool erase_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key,
                      std::string* err = nullptr);
// Zet Enabled van een patch in het gebruikers-patches.ini (sectie "Patch: <naam>").
bool set_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, bool enabled, std::string* err = nullptr);
// Haalt de keuze van de gebruiker voor een patch weg (de meegeleverde stand geldt weer).
bool erase_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, std::string* err = nullptr);

// Paden van de bestanden van een game in een laag (voor "Open map" en weergave).
std::string game_dir(const std::string& layer_dir, const std::string& profile);

// ---------------------------------------------------------------------------------------------------------------------
// Het profiel van een game in game.ini (docs/per-game-patches.md, "Het profiel van een game"). Dezelfde regels als scripts/ownprofile.py;
// tests/test_ownprofile.py controleert dat de tabellen hieronder gelijk zijn aan PROFILE_FIELDS en ENV_MAP in Python.

// (sectie, sleutel in game.ini, veld in het TeknoParrot-profiel, soort). Soort: "" tekst, "bool", "runner".
struct ProfileField {
    const char* section;
    const char* key;
    const char* xml;
    const char* kind;
};
extern const ProfileField kProfileFields[];
extern const size_t kProfileFieldCount;
const ProfileField* profile_field_for_xml(const std::string& xml_name);

// (sectie, sleutel, omgevingsvariabele, soort) zoals ENV_MAP in scripts/gamesettings.py. Soort: "" letterlijk, "bool1" (true = 1), "boolT"
// (true = true), "path", "prefix", "workdir".
struct EnvMapEntry {
    const char* section;
    const char* key;
    const char* var;
    const char* kind;
};
extern const EnvMapEntry kEnvMap[];
extern const size_t kEnvMapCount;
const EnvMapEntry* env_for_var(const std::string& var);

// De ConfigValues: [ConfigType] "Categorie/Veld = Type" (bepaalt de volgorde), [Config] "Categorie/Veld = waarde" (leeg of afwezig = leeg),
// [ConfigOptions] "Categorie/Veld = a|b|c". De sleutel wordt bij de eerste '/' gesplitst.
struct ConfigField {
    std::string category, name, value, type;
    std::vector<std::string> options;
    std::string key() const { return category + "/" + name; }
};
std::vector<ConfigField> config_fields(const Ini& settings);
// De sleutel van het eerste veld met deze naam (en categorie als die niet leeg is); leeg als het profiel dat veld niet heeft.
std::string find_config_key(const Ini& settings, const std::string& name, const std::string& category = {});

// Heeft game.ini een eigen profiel (UserProfiles-XML niet nodig)? [Game] Executable of ProfileRevision.
bool has_own_profile(const Ini& settings);

// [Identify]: hoe de game in een gamemap wordt herkend. path = relatief pad van het hoofdbestand (anders de eerste naam uit Executable),
// size (-1 = niet gezet), sha256 (leeg = niet gezet), folder (optioneel: de mapnaam, alleen voor games met een identiek hoofdbestand).
struct Identify {
    std::string path;
    long long size = -1;
    std::string sha256, folder;
};
Identify identify_of(const Ini& settings);
using HashFn = std::function<std::string(const std::string& path)>;   // sha256 (kleine hex) van een bestand; leeg = niet te lezen

// Het pad van de game, afgeleid en niet opgeslagen: een override van de gebruiker ([Game] GamePath), een lokale view (views_dir/<profile>/)
// en dan de gamemappen (elke submap van een gamemap, en de gamemap zelf). how: override | view | folders ("" = niet gevonden). Zonder
// `hash` (de engine heeft er geen) wordt een sha256 in [Identify] niet gecontroleerd: pad, grootte en mapnaam gelden dan.
std::string locate_game(const std::string& profile, const Ini& settings, const std::vector<std::string>& folders, const std::string& views_dir,
                        const std::string& home, std::string* how = nullptr, const HashFn& hash = {});

}  // namespace ravengun::gamesettings
