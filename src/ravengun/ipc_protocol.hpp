#pragma once
// Beheerprotocol tussen ravengund en clients (ravengunctl, later de webinterface).
// Transport: Unix-stream-socket, één JSON-object per regel (UTF-8, afgesloten met '\n').
// Specificatie: docs/architecture.md, sectie "IPC".
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ravengun::ipc {

inline constexpr int kProtocolVersion = 1;
inline constexpr size_t kMaxLineBytes = 64 * 1024;
inline constexpr double kWatchRateMin = 1.0;
inline constexpr double kWatchRateMax = 250.0;
inline constexpr int kMouseCountsMin = 100, kMouseCountsMax = 20000;  // set_mouse_gun: counts_per_screen

enum class Cmd { Status, Sample, Recenter, Watch, SetFilter, SetAim, Camera,
                 Arm, Disarm, Output, TestPattern, OutputButton, SetOutput,
                 Record, CalibMag,
                 SetLed, SetCamera, SetWeb, Info, Settings, Diagnostics,
                 SetButtons, CalibrateUi, SetFusion, ClearTrim, SetClock, SetRumble, RumbleTest,
                 SetGunButtons, Games, GameStart, GameStop,
                 Controllers, SetController, Blink, Unpair, Pair,
                 Aagames, OutputKey, Iobridge, Mice, SetMouseGun };
const char* to_string(Cmd c);

struct Request {
    Cmd cmd = Cmd::Status;
    // Meerdere spelers: voor welke speler of controller dit verzoek geldt (per-controller-opdrachten). `player` 1..4, of
    // `controller` = Bluetooth-adres, controller_key (16 hex) of "default" (de standaard voor elke controller). Zonder beide:
    // speler 1 (en, als speler 1 een Move heeft, diens instellingen). Nooit allebei.
    std::optional<int> player;
    std::string controller;
    std::string id_json;   // id van de client als JSON-tekst (getal of string), leeg = geen id
    bool full_ids = false; // status/sample/watch: controller-ID ongeredigeerd
    double rate_hz = 30;   // watch
    std::optional<bool> filter_enabled;
    std::optional<double> min_cutoff_hz, beta, d_cutoff_hz;
    std::optional<double> h_edge_deg, v_edge_deg;
    std::optional<double> predict_ms;                       // set_aim: voorspelhorizon (ms, 0 = uit)
    std::optional<double> clock_hz, extrapolate_ms;         // set_clock: uitvoerklok (Hz, 0 = uit) en extrapolatielimiet (ms)
    // set_rumble: trillen bij schieten (alle velden optioneel, minstens een); rumble_test: een testpuls van `rumble_ms` ms.
    std::optional<bool> rumble_enabled;
    std::optional<double> rumble_strength, rumble_pulse_ms, rumble_repeat_hz, rumble_pump_strength, rumble_ms;
    // Uitvoer (arm/disarm/output/test_pattern/output_button/set_output).
    std::string pattern;   // test_pattern: center|corners|grid|sweep|point|stop
    std::optional<double> dwell_ms, duration_s, margin, x, y, rate_hz_pattern;
    std::string button;    // output_button: trigger|reload|grenade|aux1|aux2
    std::string action;    // output_button: down|up|tap
    std::optional<bool> invert_x, invert_y;
    std::optional<double> scale_x, scale_y, offset_x, offset_y;
    std::optional<double> vp_x0, vp_y0, vp_x1, vp_y1;          // set_output: viewport (aim-ruimte)
    std::optional<double> screen_aspect, content_aspect;      // set_output: letterbox, beide of geen
    // record (start|stop|status|mark), calib_mag (start|stop|abort|status)
    std::string tag;     // record start: [a-z0-9_-]{1,40}
    std::string label;   // record mark: max. 80 tekens, geen stuurtekens
    std::string holder;  // calib_mag start: houder, bijv. "sharp_shooter" of "los"
    std::optional<double> max_mb;  // duration_s/action: gedeeld met uitvoer
    // set_led: een preset (cyaan, magenta, rood, groen, blauw, wit) OF alle drie r,g,b (gehele getallen 0..255).
    std::string led_preset;
    std::optional<double> led_r, led_g, led_b;
    // set_camera: geheel getal exposure 0..255, gain 0..63; auto-vlaggen; modus "BxH@FPS" uit de toegestane lijst.
    std::optional<double> exposure, gain;
    std::optional<bool> auto_exposure, auto_gain, auto_awb;
    std::string mode;     // set_camera: cameramodus; set_fusion: mahony | mahony_mag | vqf | vqf_mag
    // set_web: poort voor de volgende start.
    std::optional<double> web_port;
    // set_buttons: rol (zie config::sharp_shooter_roles()) -> Move-knop of "" (wissen).
    std::map<std::string, std::string> button_map;
    // set_gun_buttons: Move-knop (config::gun_map_buttons()) -> actie (canonieke vorm, zie config::check_gun_action()).
    std::map<std::string, std::string> gun_map;
    // game_start: TeknoParrot-profielnaam ([A-Za-z0-9_-]{1,64}, een bestaand bestand in UserProfiles; de daemon controleert dat).
    std::string profile;
    // aagames (Unity-route zonder TeknoParrot): action = start | stop | status, game = sleutel uit packaging/aagames-games.json.
    // output_key: een toets op het virtuele toetsenbord (key = naam uit output/key_table.hpp, action down|up|tap, player 1..4).
    std::string game, key_name;
    // iobridge: action = start | stop | status | route, game = TeknoParrot-profielnaam, route = evdev | tp-shm | unity-tcp (optioneel).
    std::string route;
    // set_fusion: yaw-trim leren uit centreerdrukken aan/uit (live).
    std::optional<bool> recenter_learning;
    // calibrate_ui: open | close | status (kiosk-browser op de tv, zie src/ravengun/kiosk/kiosk.hpp), bij open
    // optioneel step="practice" (opent direct de oefenstap).
    std::string step;
    // Gedeeld veld: 'action' bij record/calib_mag, en bij pair: start | status | confirm | cancel.
    // set_controller: nieuwe naam (1..32 tekens), spelernummer (1..4) en/of alle afwijkingen wissen.
    std::string name;
    std::optional<int> new_player;
    bool reset_settings = false;
    // unpair: de instellingen bewaren voor als de controller terugkomt (standaard waar).
    bool keep_settings = true;
    // blink: duur in ms (200..10000, standaard 3000).
    std::optional<double> blink_ms;
    // set_mouse_gun: `player` (1..4, verplicht; geen `controller`), `mouse` = een id uit `mice` of "" (loskoppelen) en
    // optioneel counts_per_screen (100..20000).
    std::optional<std::string> mouse;
    std::optional<double> counts_per_screen;
};
// Commando's met een spelerkeuze (player/controller).
bool takes_selector(Cmd c);
// Commando's met een verplicht `player` (1..4) als gewoon veld, zonder `controller`: set_mouse_gun.
bool needs_player(Cmd c);

struct Error {
    std::string code;     // parse_error, bad_request, unsupported_version, unknown_command,
                          // invalid_argument, line_too_long, unavailable, internal
    std::string message;  // Nederlands, voor mensen
    std::string id_json;  // indien uit het verzoek te halen
};

std::variant<Request, Error> parse_request(std::string_view line);

// Antwoorden (zonder afsluitende '\n').
std::string ok_response(const std::string& id_json, std::string_view result_json);
std::string error_response(const Error& e);
// Event-regel voor watch: {"v":1,"event":"<name>","data":<data_json>}
std::string event_line(std::string_view name, std::string_view data_json);

// Verzoek opbouwen (client-kant).
std::string make_request(const Request& r);

}  // namespace ravengun::ipc
