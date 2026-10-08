#pragma once
// Persistente instellingen van ravengund: ~/.config/ravengun/config.json.
//
// - Schemaversie met migratiehaak; onbekende (nieuwere) versies worden geweigerd.
// - Strikte validatie bij het laden. Een ongeldig bestand wordt nooit half toegepast: de daemon valt
//   terug op de laatst-goede kopie (config.json.bak) of op de standaardwaarden, en meldt dat.
// - Atomisch schrijven: tijdelijk bestand (0600) in dezelfde map, fsync, rename.
// - De opdrachtregel van de daemon heeft voorrang op dit bestand (zie docs/web-ui.md).
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ravengun/json_parse.hpp"

namespace ravengun::config {

inline constexpr int kSchemaVersion = 1;

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
    bool enabled = true;
    double min_cutoff_hz = 1.0, beta = 0.5, d_cutoff_hz = 1.0;
};
struct AimSettings {
    double h_edge_deg = 20.0, v_edge_deg = 11.25;
};
struct WebSettings {
    int port = 8765;  // gebruikt bij de volgende start
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
// magnetometer; zonder geldige kalibratie gedraagt die zich als mahony).
struct FusionSettings {
    std::string mode = "mahony";  // opt-in: de replay toont winst op 1 van 2 handsporen (zie aim-fusion-design.md, fase 4)
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
                {"ss_rl", "reload"}, {"ss_fire", "none"}, {"sel1", "none"}, {"sel2", "none"}, {"sel3", "none"}};
    }
};

// Indelbare Move-knoppen (in vaste volgorde) en de toegestane vaste acties.
const std::vector<std::string>& gun_map_buttons();
// Leeg = geldige actie, anders een Nederlandse foutmelding. Geeft de canonieke vorm terug in 'canon'.
std::string check_gun_action(const std::string& action, std::string& canon);

struct Settings {
    LedSettings led;
    TrackerSettings tracker;
    CameraSettings camera;
    FilterSettings filter;
    AimSettings aim;
    WebSettings web;
    SharpShooterSettings sharp_shooter;
    FusionSettings fusion;
    GunButtonSettings gun_buttons;
};

// Controleert alle bereiken; leeg = geldig, anders een Nederlandse melding.
std::string validate(const Settings& s);

// Serialisering (met schemaversie). Leesbaar opgemaakt: een sectie per regel.
// pretty=false: compact op een regel (voor de IPC, die regelgebaseerd is).
std::string to_json(const Settings& s, bool pretty = true);

struct MigrationStep {
    int from;  // zet schema 'from' om naar 'from + 1'
    std::function<bool(json::Value&, std::string&)> apply;
};
// De ingebouwde migraties (nu geen: schema 1 is de eerste versie).
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
