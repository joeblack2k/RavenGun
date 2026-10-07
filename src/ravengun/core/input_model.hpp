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

inline constexpr int kInputSchemaVersion = 1;

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
    YawReference yaw_ref = YawReference::None;

    // Positie: nog geen camera; altijd ongeldig.
    bool position_valid = false;

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
};

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
