#pragma once
// Persistente instellingen van ravengund: ~/.config/ravengun/config.json.
//
// - Schemaversie met migratiehaak; onbekende (nieuwere) versies worden geweigerd.
// - Strikte validatie bij het laden. Een ongeldig bestand wordt nooit half toegepast: de daemon valt
//   terug op de laatst-goede kopie (config.json.bak) of op de standaardwaarden, en meldt dat.
// - Atomisch schrijven: tijdelijk bestand (0600) in dezelfde map, fsync, rename.
// - De opdrachtregel van de daemon heeft voorrang op dit bestand (zie docs/web-ui.md).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ravengun/json_parse.hpp"
#include "ravengun/output/rumble.hpp"

namespace ravengun::config {

// Schema 2 (2026-10-09): per-controller-afwijkingen onder "controllers" (zie ControllerEntry). Schema 1 migreert verliesvrij:
// alle waarden worden de standaard (en gelden zo ook voor de Move die al gekoppeld was).
inline constexpr int kSchemaVersion = 2;
inline constexpr int kMaxPlayers = 4;

struct CameraMode { int width, height, fps; };
// De modi die de UI en set_camera toestaan (gemeten in docs/decisions.md).
const std::vector<CameraMode>& camera_modes();
bool camera_mode_valid(int w, int h, int fps);
std::string camera_mode_string(int w, int h, int fps);          // "640x480@60"
bool parse_camera_mode(const std::string& s, CameraMode& out);  // alleen toegestane modi

struct LedSettings {
    bool enabled = false;          // true zodra de gebruiker een kleur gekozen heeft
    std::string preset = "cyaan";  // cyaan, magenta, rood, groen, blauw, wit of custom
    int r = 0, g = 255, b = 255;
};
struct TrackerSettings {
    double hue_deg = -75;
    double hue_tol_deg = 30;
    int min_chroma = 28;
};
struct CameraSettings {
    int width = 640, height = 480, fps = 60;
    bool auto_exposure = false, auto_gain = false, auto_awb = false;
    int exposure = 50;  // 0..255
    int gain = 0;       // 0..63
};
struct FilterSettings {
    // Standaard uit de latentiemeting (docs/aim-fusion-design.md, fase 6): zelfde jitter in stilstand als 1 Hz / 10 / 1 Hz,
    // maar ongeveer 40 % minder vertraging door de hogere afsnijfrequentie van de snelheidsschatting.
    bool enabled = true;
    double min_cutoff_hz = 0.3, beta = 10.0, d_cutoff_hz = 20.0;
};
struct AimSettings {
    double h_edge_deg = 20.0, v_edge_deg = 11.25;
    // Voorspelling van de oriëntatie (ms vooruit, 0 = uit); zie core/predict.hpp. Live instelbaar.
    double predict_ms = 40.0;
};
// Uitvoerklok (fase 6): tussen rapporten en tijdens gaten extrapoleert de uitvoer de aim (output/aim_clock.hpp).
struct OutputSettings {
    double clock_hz = 250.0;       // 0 = uit: de uitvoer volgt alleen de rapporten
    double extrapolate_ms = 40.0;  // daarna wordt de laatste positie vastgehouden
};
struct WebSettings {
    int port = 8765;  // gebruikt bij de volgende start
    // Bereikbaar vanaf het thuisnetwerk (telefoon, tablet); anders alleen vanaf deze computer. Bij de volgende start.
    bool lan = false;
    // Alleen met lan: apparaten op het netwerk moeten eenmalig het token meegeven (/?token=...). Standaard uit.
    bool require_token = false;
};

// Welke Move-knop een fysieke knop van de Sharp Shooter oplevert (gemeten door de kalibratie-UI op de tv).
// Sleutels: sharp_shooter_roles(). Waarde: naam van een Move-knop (triangle, circle, cross, square, select,
// start, ps, move, t) of leeg = niet gemeten of niet aanwezig.
const std::vector<std::string>& sharp_shooter_roles();
bool move_button_name_valid(const std::string& name);  // false voor een lege naam
struct SharpShooterSettings {
    std::map<std::string, std::string> buttons;  // rol -> Move-knop
    // Uitbreidingsconnector van de Move: "auto" (een verbonden EXT-apparaat is de Sharp Shooter; het device-id
    // wordt nog niet gelezen), "sharp_shooter" (idem, expliciet) of "none" (EXT-bytes negeren). Gelezen bij
    // het starten van de daemon. Nog te bevestigen op echte hardware aan de hand van de ruwe bytes in de opname.
    std::string extension = "auto";
};

// Fase 4: oriëntatieschatter. "mahony" (IMU-only, terugval) of "mahony_mag" (met yaw-correctie van de
// magnetometer; zonder geldige kalibratie gedraagt die zich als mahony), of "vqf" / "vqf_mag" (VQF 6D / 9D).
struct FusionSettings {
    std::string mode = "mahony";  // opt-in: de replay toont winst op 1 van 2 handsporen (zie aim-fusion-design.md, fase 4)
    // Alleen voor vqf en vqf_mag: welke gyro-bias VQF gebruikt. "external" (standaard): die van onze schatter;
    // "own": VQF schat zelf (en onze wordt dan niet toegepast). Gelezen bij het starten van de daemon.
    std::string vqf_bias = "external";
    // Lerende yaw-trim uit centreerdrukken van de speler (yaw_trim.hpp); gelezen bij het starten van de daemon.
    bool recenter_learning = true;
};

// Knoppenindeling van de virtuele lightgun (live koppeling): `map` koppelt een Move-knopnaam aan een actie.
// Acties: "reload", "grenade", "aux1", "aux2" (gun-knoppen), "recenter", "key:<naam>" (toets op het
// virtuele toetsenbord, namen uit output/key_table.hpp) of "none". Meerdere Move-knoppen mogen dezelfde
// actie hebben (OR). Knoppen: triangle, circle, cross, square, select, start, move en (alleen als EXT verbonden is) de Sharp Shooter-knoppen
// pump (de afgeleide pomp: T zonder fire-bit; standaard reload), ss_rl (het RL-bit 0x80, in de praktijk nooit gezien; standaard
// reload), ss_fire, sel1, sel2, sel3 (standaard none). Zonder uitbreiding is T de analoge trekker (drempel met hysterese);
// met uitbreiding is de trekker het fire-bit. T zelf en PS staan niet in de map (PS is altijd recenter, hard bedraad).
// De mapping uit sharp_shooter.buttons (kalibratie-UI) wordt hier bewust NIET gebruikt.
struct GunButtonSettings {
    int trigger_on = 140;     // T-waarde (0..255) waarbij de trekker ingedrukt wordt
    int trigger_off = 100;    // en waaronder hij weer loslaat (hysterese)
    // Move-knopnaam -> actie; altijd alle indelbare knoppen aanwezig ("none" = niets). Standaard geschikt voor Halo.
    std::map<std::string, std::string> map = default_gun_map();
    static std::map<std::string, std::string> default_gun_map() {
        return {{"move", "grenade"}, {"cross", "reload"}, {"triangle", "key:5"}, {"square", "key:1"},
                {"select", "key:5"}, {"start", "key:1"}, {"circle", "none"}, {"pump", "reload"},
                {"ss_rl", "reload"}, {"ss_fire", "none"}, {"sel1", "none"}, {"sel2", "none"}, {"sel3", "none"},
                // Navigation Controller van dezelfde speler (alleen als hij aan deze speler is toegewezen).
                {"nav_cross", "none"}, {"nav_circle", "none"}, {"nav_l1", "reload"}, {"nav_l2", "grenade"}, {"nav_l3", "none"},
                {"nav_ps", "none"}, {"nav_up", "none"}, {"nav_down", "none"}, {"nav_left", "none"}, {"nav_right", "none"},
                {"nav_stick_up", "none"}, {"nav_stick_down", "none"}, {"nav_stick_left", "none"}, {"nav_stick_right", "none"}};
    }
};

// Indelbare Move-knoppen (in vaste volgorde) en de toegestane vaste acties.
const std::vector<std::string>& gun_map_buttons();
// Leeg = geldige actie, anders een Nederlandse foutmelding. Geeft de canonieke vorm terug in 'canon'.
std::string check_gun_action(const std::string& action, std::string& canon);
// De knoppenindeling met per-game keuzes (Move-knop, actie) eroverheen ([Controls] in game.ini, gamesettings/controls_settings.hpp).
// Een onbekende knop of ongeldige actie wordt overgeslagen. Zonder keuzes: gelijk aan `g`.
GunButtonSettings with_gun_overrides(const GunButtonSettings& g, const std::vector<std::pair<std::string, std::string>>& overrides);

// De instellingen die per controller kunnen afwijken (LED en trackertint, richten, filter, voorspelling, fusie, trillen,
// knoppenindeling). De globale waarden in Settings zijn de standaard voor elke controller.
struct ControllerValues {
    LedSettings led;
    TrackerSettings tracker;
    FilterSettings filter;
    AimSettings aim;
    FusionSettings fusion;
    GunButtonSettings gun_buttons;
    // Trillen bij schieten (output/rumble.hpp); live instelbaar.
    output::RumbleConfig rumble;
};

// Een bekende controller (Move of Navigation Controller), sleutel = Bluetooth-adres in kleine letters.
// `keys` noemt de velden die afwijken van de standaard ("filter.beta", "led.r", "gun_buttons.map.cross", ...); de
// waarden staan in `values`. Alles wat niet in `keys` staat, volgt de globale standaard.
struct ControllerEntry {
    std::string type = "move";   // move | navigation
    std::string model;           // ZCM1, ZCM2 of leeg (onbekend)
    std::string name;            // door de gebruiker te kiezen, max. 32 tekens
    int player = 0;              // 1..4 (een Move: zijn gun; een Navigation Controller: de speler die hij aanvult); 0 = geen
    bool removed = false;        // ontkoppeld, maar de instellingen zijn bewaard voor als hij terugkomt
    int64_t added_unix = 0, last_seen_unix = 0;
    std::set<std::string> keys;
    ControllerValues values;
};

struct Settings : ControllerValues {
    CameraSettings camera;
    OutputSettings output;
    WebSettings web;
    SharpShooterSettings sharp_shooter;
    std::map<std::string, ControllerEntry> controllers;  // adres (kleine letters) -> controller
    // Gamepad per speler (id uit `pads`, sysinfo/gamepads.hpp; leeg = geen). Bewaard, zodat een herstart van de daemon de keuze
    // niet wist (anders dan de muis, die alleen runtime is). Zie docs/gamepad.md.
    std::array<std::string, kMaxPlayers> gamepads{};
};

// ---- Per-controller-instellingen ----
bool controller_key_valid(const std::string& key);
// Kopieert het veld `key` van src naar dst. false bij een onbekende sleutel.
bool copy_controller_value(const std::string& key, ControllerValues& dst, const ControllerValues& src);
// De instellingen zoals ze voor deze controller gelden (standaard plus afwijkingen). Onbekend adres = de standaard.
Settings effective(const Settings& s, const std::string& address);
// Past fn toe op de instellingen van `address` (leeg = de standaard). Bij een controller worden daarna precies de sleutels in
// `keys` als afwijking bewaard (met de nieuwe waarde). Geeft false als het adres onbekend is.
bool modify_for(Settings& s, const std::string& address, const std::vector<std::string>& keys, const std::function<void(Settings&)>& fn);
// Wist afwijkingen van een controller (alle, of alleen die met een van de voorvoegsels, bv. "filter.").
void clear_overrides(ControllerEntry& e, const std::vector<std::string>& prefixes = {});

// Adressen: AA:BB:CC:DD:EE:FF (hoofd- of kleine letters); normalize geeft kleine letters of leeg bij een ongeldig adres.
bool address_valid(const std::string& a);
std::string normalize_address(const std::string& a);
// Naam: 1..32 tekens, geen stuurtekens.
bool controller_name_valid(const std::string& n);
// Standaardkleur van een speler (1 cyaan, 2 magenta, 3 groen, 4 geel; zie docs/decisions.md) en zijn Start-toets.
LedSettings player_led(int player);
std::string player_start_key(int player);  // "1".."4"
// Zet de spelergebonden standaarden (LED-kleur, Start-toets) van een controller over van speler `from` naar `to`, maar alleen
// waar de controller nog de standaard van `from` gebruikte. Een eigen keuze van de gebruiker blijft staan.
void apply_player_defaults(Settings& s, const std::string& address, int from, int to);
// Laagste vrije spelernummer voor een Move (1..4), 0 als alles bezet is.
int free_player(const Settings& s, const std::string& except_address = {});
// Het adres van de (niet-verwijderde) Move met dit spelernummer, leeg als er geen is.
std::string move_for_player(const Settings& s, int player);
// Neemt een controller op (eerste keer gezien via Bluetooth, BlueZ of de koppelwizard) of haalt een verwijderde terug.
// Een Move krijgt het laagste vrije spelernummer en de spelergebonden standaarden (kleur, Start-toets); een Navigation
// Controller vult standaard speler 1 aan. Een bekende controller verandert niet (alleen model en last_seen). Geeft het
// spelernummer (0 = geen vrije plek of een Navigation Controller zonder speler); `added` = nieuw of teruggehaald.
int register_controller(Settings& s, const std::string& address, const std::string& type, const std::string& model, int64_t now_unix,
                        bool* added = nullptr);
// Aantal eigen keuzes van een controller: afwijkingen die niet alleen de spelergebonden standaarden (kleur, Start-toets) zijn.
int own_overrides(const Settings& s, const std::string& address);
// Standaardnaam: "Move 2", "Navigation Controller".
std::string default_controller_name(const Settings& s, const std::string& type);

// Controleert alle bereiken; leeg = geldig, anders een Nederlandse melding.
std::string validate(const Settings& s);

// Serialisering (met schemaversie). Leesbaar opgemaakt: een sectie per regel.
// pretty=false: compact op een regel (voor de IPC, die regelgebaseerd is). redact=true: de controller-adressen afgeschermd
// (sleutel = controller_key, adres als xx:xx:xx:xx:xx:ac), voor de web-API en de diagnose-export.
std::string to_json(const Settings& s, bool pretty = true, bool redact = false);

struct MigrationStep {
    int from;  // zet schema 'from' om naar 'from + 1'
    std::function<bool(json::Value&, std::string&)> apply;
};
// De ingebouwde migraties: 1 -> 2 (controllers erbij; alle bestaande waarden blijven de standaard).
const std::vector<MigrationStep>& builtin_migrations();

// Parseert en valideert een configuratietekst. Bij ok=false blijft 'out' ongemoeid.
// 'warnings' krijgt o.a. onbekende sleutels (die worden genegeerd).
bool parse_settings(const std::string& text, Settings& out, std::string& err, std::vector<std::string>* warnings = nullptr,
                    const std::vector<MigrationStep>* migrations = nullptr);

// Schrijft atomisch (tijdelijk bestand 0600 + fsync + rename). Maakt de map (0700) aan.
bool write_file_atomic(const std::string& path, const std::string& content, std::string& err);

struct LoadInfo {
    enum class State {
        NotLoaded,
        Missing,    // geen bestand: standaardwaarden
        Loaded,     // bestand geldig
        Recovered,  // bestand ongeldig, laatst-goede kopie gebruikt
        Invalid,    // bestand ongeldig en geen kopie: standaardwaarden
    };
    State state = State::NotLoaded;
    std::string message;  // leeg bij Missing/Loaded
    std::vector<std::string> warnings;
};
const char* to_string(LoadInfo::State s);

struct SaveResult {
    bool ok = true;
    std::string message;
};

class SettingsStore {
public:
    explicit SettingsStore(std::string path = default_path());
    static std::string default_path();  // $XDG_CONFIG_HOME of $HOME/.config + /ravengun/config.json; leeg als onbekend

    // Leest het bestand (of de kopie). Verandert het bestand niet.
    LoadInfo load();
    Settings get() const;
    // Past 'fn' toe op een kopie, valideert, neemt over in het geheugen en schrijft het bestand.
    // Een ongeldige uitkomst wordt niet overgenomen (ok=false). Een schrijffout laat de wijziging in
    // het geheugen staan (de daemon past hem toe) en meldt de fout.
    SaveResult update(const std::function<void(Settings&)>& fn);

    const std::string& path() const { return path_; }
    LoadInfo info() const;
    std::string last_save_error() const;
    bool file_present() const;

private:
    std::string path_;
    mutable std::mutex m_;
    Settings cur_;
    LoadInfo info_;
    std::string last_save_error_;
};

}  // namespace ravengun::config
