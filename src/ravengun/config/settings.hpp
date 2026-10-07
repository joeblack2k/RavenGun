#pragma once
// Persistente instellingen van ravengund: ~/.config/ravengun/config.json.
//
// - Schemaversie met migratiehaak; onbekende (nieuwere) versies worden geweigerd.
// - Strikte validatie bij het laden. Een ongeldig bestand wordt nooit half toegepast: de daemon valt
//   terug op de laatst-goede kopie (config.json.bak) of op de standaardwaarden, en meldt dat.
// - Atomisch schrijven: tijdelijk bestand (0600) in dezelfde map, fsync, rename.
// - De opdrachtregel van de daemon heeft voorrang op dit bestand (zie docs/web-ui.md).
#include <functional>
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

struct Settings {
    LedSettings led;
    TrackerSettings tracker;
    CameraSettings camera;
    FilterSettings filter;
    AimSettings aim;
    WebSettings web;
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
