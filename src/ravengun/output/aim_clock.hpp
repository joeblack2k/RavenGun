#pragma once
// Uitvoerklok (fase 6): tussen twee HID-rapporten en tijdens gaten (verloren rapporten, ~13 % via Bluetooth)
// extrapoleert de uitvoer de aim met de laatst bekende snelheid, begrensd in tijd, op een vaste klok
// (standaard 250 Hz). Pure logica zonder threads of apparaat: de daemon roept `on_sample` aan vanuit de
// HID-thread (via OutputService, onder diens slot) en `tick` vanuit de klokthread; de analyse
// (`ravengun-analyze latency`) en de tests gebruiken dezelfde klasse met gesimuleerde tijden.
//
// Regels:
//  - Het anker is de laatste geldige aim van een rapport (positie in aim-ruimte, ongeclampt, plus de snelheid
//    van de gefilterde aim uit de Move-tijd). Een ongeldige aim (stale, lost, disconnect, geen midden) wist het
//    anker: de cursor blijft dan staan, er wordt niets geëxtrapoleerd.
//  - Positie op tijd t = anker + v * min(t - t_anker, max_extrap): daarna wordt vastgehouden (een weggevallen
//    verbinding laat de cursor niet wegschieten). taper > 0 laat de snelheid lineair afnemen naar 0 op de
//    limiet (taper = 1: volledig), voor een zachtere stop.
//  - Het anker ligt op t_rx - age_ms (age_comp): een rapport dat 15 ms later binnenkomt dan het snelste transport, wordt
//    15 ms verder geëxtrapoleerd, zodat de getoonde positie niet met de aankomstjitter meebeweegt.
//  - hz = 0: klok uit (huidig gedrag: de uitvoer volgt alleen de rapporten).
//  - Tikken liggen op een vast raster (t0 + k * periode). Is de klokthread te laat, dan worden gemiste tikken
//    overgeslagen (geen salvo van inhaaltikken).
#include <cstdint>
#include <optional>

#include "ravengun/core/input_model.hpp"

namespace ravengun::output {

struct OutputClockConfig {
    double hz = 250.0;            // 0 = uit (per rapport)
    double max_extrap_ms = 40.0;  // daarna vasthouden
    double taper = 0.0;           // 0..1, zie hierboven
    // Het anker op t_rx - age_ms zetten (de extra Bluetooth-vertraging van dit rapport, zie InputSample::age_ms): de
    // jitter van de aankomsttijd wordt dan weggeëxtrapoleerd. Alleen met klok aan.
    bool age_comp = true;
    bool enabled() const { return hz > 0; }
};

struct AimAnchor {
    bool valid = false;
    int64_t t_ns = 0;
    double x = 0.5, y = 0.5;   // aim-ruimte, ongeclampt
    double vx = 0, vy = 0;     // per seconde
};

struct AimPos {
    double x = 0.5, y = 0.5;
    bool extrapolating = false;  // dt > 0 en binnen de limiet
    bool held = false;           // limiet bereikt: vastgehouden
    double dt_s = 0;             // tijd sinds het anker
};

class AimClock {
public:
    explicit AimClock(OutputClockConfig c = {}) : cfg_(c) {}
    void set_config(const OutputClockConfig& c);
    const OutputClockConfig& config() const { return cfg_; }

    // Nieuw rapport. Een ongeldige aim wist het anker. t_ns = ontvangsttijd (zelfde klok als tick).
    void on_sample(const core::InputSample& s);
    // Hetzelfde met losse waarden (positie en snelheid in aim-ruimte, leeftijd in ms).
    void on_report(int64_t t_ns, double x, double y, double vx, double vy, double age_ms);
    void clear() { anchor_.valid = false; }
    const AimAnchor& anchor() const { return anchor_; }

    // Positie op tijd t uit het anker (zonder het rooster te verschuiven); leeg zonder geldig anker.
    std::optional<AimPos> at(int64_t t_ns) const;

    // Rooster: het eerstvolgende tikmoment strikt na `now_ns`, op t0 + k * periode (t0 = eerste aanroep).
    int64_t next_tick_after(int64_t now_ns);
    // Tik op `now_ns` (door de klokthread, rond een tikmoment): de positie, of leeg als er geen anker is of de
    // klok uit staat.
    std::optional<AimPos> tick(int64_t now_ns) const { return cfg_.enabled() ? at(now_ns) : std::nullopt; }
    int64_t period_ns() const;

private:
    OutputClockConfig cfg_;
    AimAnchor anchor_;
    bool have_t0_ = false;
    int64_t t0_ns_ = 0;
};

}  // namespace ravengun::output
