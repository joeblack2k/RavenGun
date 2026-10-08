#pragma once
// Intern, versieerbaar inputmodel van RavenGun (schema-versie 1).
// Betekenis van ieder veld: docs/architecture.md, sectie "Inputmodel".
#include <cstdint>
#include <string>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/fusion.hpp"
#include "ravengun/core/math.hpp"

namespace ravengun::json { class Writer; }

namespace ravengun::core {

inline constexpr int kInputSchemaVersion = 2;  // 2: position = optische 2D-beeldpositie

// Welke bron de aim bepaalt. Alleen Orientation is geïmplementeerd.
enum class TrackingMode { None, Orientation, OpticalDiagnostic, Hybrid, GeometricRay };
const char* to_string(TrackingMode m);

// Verbindingstoestand van de controller zoals de pijplijn die ziet.
enum class LinkState {
    Disconnected,  // geen controller geopend
    Fresh,         // laatste rapport jonger dan stale_ms
    Stale,         // geen rapport sinds stale_ms: aim en oriëntatie ongeldig, knoppen nog vast
    Lost,          // geen rapport sinds release_ms: alle knoppen vrijgegeven
};
const char* to_string(LinkState s);

// Of de yaw-integratie sinds het laatste midden ononderbroken is.
enum class YawReference { None, Continuous, Interrupted };
const char* to_string(YawReference r);

// Eén HID-rapport, al door PSMoveAPI gekalibreerd (g en rad/s). Index 0 = Frame_FirstHalf
// (ouder), index 1 = Frame_SecondHalf (nieuwer). ZCM2 heeft één subsample (subsamples = 1).
struct RawReport {
    int64_t t_rx_ns = 0;      // CLOCK_MONOTONIC direct nadat psmove_poll() dit rapport opleverde
    int hid_seq = -1;         // 4-bit volgnummer uit het rapport (0..15), -1 = onbekend
    int subsamples = 2;
    Vec3 accel[2]{};          // g
    Vec3 gyro[2]{};           // rad/s
    int mag_raw[3]{0, 0, 0};  // ruwe magnetometer (-2048..2047), niet gebruikt in de fusie
    uint32_t buttons = 0;     // PSMoveAPI-bitveld
    uint8_t trigger = 0;      // 0..255
    int battery_raw = -1;     // PSMove_Battery_Level
    // Uit het ruwe hidraw-rapport (eigen parsing, zie move_report.hpp). has_stamp = false als het
    // rapport niet aan een hidraw-rapport gekoppeld kon worden (dan geldt de ontvangsttijd).
    bool has_stamp = false;
    uint16_t stamp = 0;       // interne 16-bit tijdstempel van de Move (10 us per tick, gemeten)
    int temperature_raw = -1; // 12-bit ruwe temperatuur (psmove_get_temperature), -1 = onbekend
    bool has_raw = false;     // accel_raw/gyro_raw gevuld (ruwe sensorwaarden, voor opnamen)
    int accel_raw[2][3]{};
    int gyro_raw[2][3]{};
    // Uitbreidingsconnector (EXT) van de Move, zie move_report.hpp. has_ext = bekend (uit de PSMoveAPI of de
    // hidraw-tap); oude opnames hebben dit niet.
    bool has_ext = false;
    bool ext_connected = false;
    uint8_t ext[5]{0, 0, 0, 0, 0};
};

// Optische meting van de bol: 2D-positie in het camerabeeld. Geen diepte en geen 3D: daarvoor
// is een gekalibreerd cameramodel nodig, dat er (nog) niet is.
struct OpticalSample {
    bool camera_active = false;    // camera levert frames
    const char* camera_state = "disabled";  // disabled, opening, streaming, error
    bool valid = false;
    const char* reason = "no_camera";       // "none" als geldig, zie BallTracker (vision::Reason)
    const char* track_state = "searching";  // searching, tracking
    uint64_t frame_seq = 0;        // V4L2-volgnummer van het verwerkte frame
    uint64_t processed = 0;        // aantal verwerkte frames in deze daemon-run
    int64_t t_frame_ns = 0;        // V4L2-timestamp (CLOCK_MONOTONIC, einde van het frame)
    int64_t t_processed_ns = 0;    // einde van de verwerking
    int width = 0, height = 0;
    double x_px = 0, y_px = 0, x_norm = 0, y_norm = 0, radius_px = 0;
    int candidates = 0;
};

struct InputSample {
    int schema_version = kInputSchemaVersion;
    uint64_t seq = 0;            // oplopend per gepubliceerd sample in deze daemon-run
    int64_t t_rx_ns = 0;         // ontvangsttijd van het rapport (CLOCK_MONOTONIC), zie architectuur
    int64_t t_eval_ns = 0;       // tijdstip waarop de toestand is geëvalueerd (= t_rx bij een rapport)
    double report_dt_s = 0;      // t_rx - t_rx(vorig rapport); 0 bij het eerste rapport
    int hid_seq = -1;
    std::string controller_id;   // stabiel: Bluetooth-adres (in uitvoer standaard geredigeerd)
    TrackingMode mode = TrackingMode::None;
    LinkState link = LinkState::Disconnected;

    // Oriëntatie
    bool orientation_valid = false;
    Quat q_wb{};                 // controller -> wereld (Z omhoog), Hamilton (w,x,y,z)
    BiasState bias_state = BiasState::None;
    bool still = false;
    Vec3 gyro_bias{};            // rad/s
    BiasSource bias_source = BiasSource::None;   // laatste bron van de bias: restored, still, quasi-still
    double bias_evidence_s = 0;  // seconden stilstand + quasi-stil bewijs waarop de bias rust
    YawReference yaw_ref = YawReference::None;
    // Fusie (fase 4): welke schatter, waar de yaw vandaan komt en wat de magnetometer doet.
    const char* fusion_mode = "mahony";   // mahony | mahony_mag
    const char* yaw_source = "gyro";      // gyro | magnetometer (mahony_mag, vergrendeld en gebruikt < 1 s geleden)
    const char* mag_state = "none";       // none (geen kalibratie) | unknown | ok | disturbed
    bool mag_locked = false;              // referentie vastgelegd
    double yaw_pending_deg = 0;           // openstaand verschil volgens de magnetometer (bij mahony: drift sinds vergrendeling)
    double yaw_corr_rate_deg_s = 0;       // toegepaste correctiesnelheid in dit rapport
    double yaw_corr_limit_deg_s = 0;      // de grens die daarbij gold
    double yaw_corr_total_deg = 0;        // som van alle toegepaste yaw-correcties
    double yaw_vertical_bias_deg_s = 0;   // door de magnetometer geleerde extra bias om de verticaal
    double mag_unusable_s = 0;            // sinds het laatst gebruikte magnetometersample

    // Positie: laatste optische meting van de camera (2D-beeldpositie, geen 3D). Wordt door de
    // daemon bij het publiceren ingevuld vanuit de tracker-thread; de pijplijn raakt dit niet.
    OpticalSample optical{};

    // Aim (genormaliseerd, zie aim.hpp)
    bool aim_valid = false;
    CenterSource center_source = CenterSource::None;
    double yaw_rel_deg = 0, pitch_rel_deg = 0;   // t.o.v. het midden
    double x_raw = 0.5, y_raw = 0.5;             // ongefilterd, unclamped
    AimPoint aim{};                               // na het (optionele) 1€-filter; unclamped + clamped
    bool filter_enabled = false;

    // Knoppen en trigger
    uint8_t trigger = 0;
    uint32_t buttons = 0;        // status na dit sample
    uint32_t pressed = 0;        // down-flanken in dit sample
    uint32_t released = 0;       // up-flanken in dit sample

    // Bron (laatste subsample, na PSMoveAPI-kalibratie, vóór biascorrectie)
    Vec3 accel_g{};
    Vec3 gyro_rad_s{};
    int battery_raw = -1;

    // Uitbreidingsconnector: vlag en 5 ruwe bytes (Sharp Shooter: byte 0, zie buttons.hpp). De afgeleide
    // knoppen (pump, ss_fire, sel1..3) zitten in `buttons` zolang ext_active.
    bool ext_known = false;
    bool ext_connected = false;
    bool ext_active = false;       // verbonden én modus niet "none"
    uint8_t ext[5]{0, 0, 0, 0, 0};
};

// Schrijft "position": {...} (sleutel plus object) voor een optische meting.
void write_optical_json(json::Writer& w, const OpticalSample& o);

// Schrijft een sample als JSON-object. full_ids=false redigeert het controller-ID.
void write_sample_json(json::Writer& w, const InputSample& s, bool full_ids);

// Batterij volgens de echte resolutie van PSMoveAPI (6 niveaus + 2 laadtoestanden).
struct BatteryInfo {
    const char* code;     // "min", "ge20", ..., "max", "charging", "charged", "unknown"
    const char* text;     // leesbaar, Nederlands
    bool charging;
};
BatteryInfo battery_info(int raw);

std::string buttons_to_string(uint32_t mask);

}  // namespace ravengun::core
