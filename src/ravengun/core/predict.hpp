#pragma once
// Voorspelling van de oriëntatie (latentiecompensatie): de richtas wordt een instelbare horizon
// vooruit gedraaid met de bias-gecorrigeerde draaisnelheid, vóór de omrekening naar de aim.
//
// Ontwerp (onderbouwing: docs/aim-fusion-design.md, fase 6, en de metingen met `ravengun-analyze latency`):
//  - De draaisnelheid (wereldstelsel, rad/s) wordt per rapport met een exponentieel filter (tijdconstante
//    rate_tau_ms) gladgestreken. Het filter gebruikt de echte dt van het rapport, dus verloren rapporten
//    (gaten tot ~50 ms) vertekenen het niet.
//  - Een hoekversnellingsterm is optioneel (accel_gain, standaard 0 = uit). Als hij aanstaat, compenseert hij
//    de vertraging van het snelheidsfilter in een versnelling, en een afremmende versnelling stopt de
//    voorspelling op het punt waar de beweging (volgens het model) tot stilstand komt: geen terugdraaien.
//  - Rustig houden: de voorspelling krijgt een gewicht dat van 0 (onder speed_lo_dps) vloeiend (smoothstep) naar 1
//    (boven speed_hi_dps) gaat. Handtrilling en sensorruis in stilstand worden zo niet vooruit geëxtrapoleerd.
//  - De voorspelde draaihoek is begrensd (max_angle_deg), zodat een uitschieter in de gyro nooit een grote
//    sprong geeft.
#include "ravengun/core/math.hpp"

namespace ravengun::core {

struct PredictConfig {
    double horizon_ms = 0;         // 0 = uit
    double rate_tau_ms = 10.0;     // tijdconstante van het snelheidsfilter
    double accel_gain = 1.0;       // 0 = geen versnellingsterm; 1 = vol
    double accel_tau_ms = 10.0;    // tijdconstante van het versnellingsfilter
    double speed_lo_dps = 3.0;     // onder deze (gefilterde) snelheid geen voorspelling
    double speed_hi_dps = 15.0;    // boven deze snelheid volle voorspelling
    double max_angle_deg = 8.0;    // bovengrens van de voorspelde draaihoek
    bool enabled() const { return horizon_ms > 0; }
};

class RatePredictor {
public:
    explicit RatePredictor(PredictConfig c = {}) : cfg_(c) {}
    void set_config(const PredictConfig& c) { cfg_ = c; }
    const PredictConfig& config() const { return cfg_; }
    void reset() { have_ = false; w_ = a_ = {}; }
    // Nieuwe meting: draaisnelheid in het wereldstelsel (rad/s), dt = tijd sinds de vorige meting (s).
    void update(Vec3 omega_world, double dt_s);
    bool primed() const { return have_; }
    // De voorspelde rotatievector (wereldstelsel, rad) voor de gegeven horizon (s); nul als uit of niet gevuld.
    Vec3 rotation(double horizon_s) const;
    // q met de voorspelde rotatie erop (links vermenigvuldigd: wereldstelsel).
    Quat apply(Quat q, double horizon_s) const;
    Vec3 rate() const { return w_; }
    Vec3 accel() const { return a_; }
    // Gewicht 0..1 bij de huidige snelheid.
    double gain() const;

private:
    PredictConfig cfg_;
    bool have_ = false;
    Vec3 w_{};  // gefilterde draaisnelheid (rad/s)
    Vec3 a_{};  // gefilterde hoekversnelling (rad/s^2)
};

}  // namespace ravengun::core
