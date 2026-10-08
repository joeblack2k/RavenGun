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

enum class Cmd { Status, Sample, Recenter, Watch, SetFilter, SetAim, Camera,
                 Arm, Disarm, Output, TestPattern, OutputButton, SetOutput,
                 Record, CalibMag,
                 SetLed, SetCamera, SetWeb, Info, Settings, Diagnostics,
                 SetButtons, CalibrateUi, SetFusion, ClearTrim, SetClock };
const char* to_string(Cmd c);

struct Request {
    Cmd cmd = Cmd::Status;
    std::string id_json;   // id van de client als JSON-tekst (getal of string), leeg = geen id
    bool full_ids = false; // status/sample/watch: controller-ID ongeredigeerd
    double rate_hz = 30;   // watch
    std::optional<bool> filter_enabled;
    std::optional<double> min_cutoff_hz, beta, d_cutoff_hz;
    std::optional<double> h_edge_deg, v_edge_deg;
    std::optional<double> predict_ms;                       // set_aim: voorspelhorizon (ms, 0 = uit)
    std::optional<double> clock_hz, extrapolate_ms;         // set_clock: uitvoerklok (Hz, 0 = uit) en extrapolatielimiet (ms)
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
    // calibrate_ui: open | close | status (kiosk-browser op de tv, zie src/ravengun/kiosk/kiosk.hpp), bij open
    // optioneel step="practice" (opent direct de oefenstap).
    std::string step;
    // Gedeeld veld: 'action' bij record/calib_mag.
};

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
