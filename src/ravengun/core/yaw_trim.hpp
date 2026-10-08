#pragma once
// Lerende yaw-trim uit centreerdrukken (alleen header, geen threads, geen wandklok).
//
// Idee: tussen twee gebruikers-centreerdrukken (PS of een knop met actie recenter) richt de speler beide keren op het
// schermmidden. De echte netto draai is dan ongeveer nul (richtfout 1 tot 2 graden); de gemeten verandering van de
// wereld-yaw gedeeld door de tussentijd is dus de resterende yaw-snelheidsfout, wat de oorzaak ook is (restbias,
// schaalfout, verloren rapporten). De trim is die fout als draaisnelheid om de wereld-verticaal (rad/s, Z omhoog) en
// wordt, net als de verticale bias van de magnetometer, van de gyro afgetrokken.
//
// Teken: yaw = atan2(x, y) neemt AF bij een positieve draai om +Z. Een gemeten yaw-stijging van e graden/s betekent
// dus een extra draai van -e graden/s om +Z; de trim (om +Z) wordt met +(-e) bijgewerkt en afgetrokken.
//
// Het leren is robuust en begrensd. Elke druk wordt (zonder de al toegepaste trim) onthouden als punt (tijd, ruwe yaw);
// de ruwe yaw is de gemeten yaw min wat de trim er zelf aan heeft bijgedragen. Uit de laatste punten volgt de ruwe
// yaw-snelheid als gewogen mediaan van de paarhellingen (gewicht = tussentijd^2: de richtfout van 1 tot 2 graden telt bij
// een korte tussentijd zwaarder, en een enkele rare druk komt in de minderheid van de paren); de doelwaarde van de trim
// is het tegengestelde daarvan. De trim loopt daar per druk een stuk naartoe: gewicht gain * T^2 / (T^2 + short_s^2),
// de stap begrensd op step_max en de trim zelf op limit. Een rare druk (20 graden ernaast) kan de trim zo hooguit
// step_max verschuiven; de paren met een latere druk trekken dat weer recht.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

#include "ravengun/core/math.hpp"

namespace ravengun::core {

struct YawTrimConfig {
    bool enabled = true;            // leren aan (toepassen van een aanwezige trim gebeurt altijd)
    double gain = 0.5;              // maximaal aandeel van de gemeten fout per druk
    double short_s = 10.0;          // bij deze tussentijd is het gewicht de helft van gain
    double step_max_dps = 0.08;     // grootste verandering van de trim per druk (graden/s)
    double limit_dps = 0.6;         // |trim| blijft hieronder (graden/s)
    double min_interval_s = 5.0;    // kortere tussentijd telt niet (richtfout weegt te zwaar)
    double max_interval_s = 180.0;  // langere ook niet (de speler kan ondertussen alles gedaan hebben)
    double max_drift_deg = 25.0;    // een grotere yaw-verandering is geen drift maar een verkeerde druk
};

// Uitkomst van een druk, voor tests en logging.
struct YawTrimUpdate {
    bool learned = false;
    const char* why = "";           // reden als niet geleerd
    double interval_s = 0;
    double drift_deg = 0;           // gemeten yaw-verandering sinds de vorige druk (gewikkeld)
    double step_dps = 0;            // toegepaste verandering van de trim
};

class YawTrim {
public:
    explicit YawTrim(YawTrimConfig cfg = {}) : cfg_(cfg) {}

    const YawTrimConfig& config() const { return cfg_; }
    void set_enabled(bool on) { cfg_.enabled = on; }
    // Trim om de wereld-verticaal (graden/s en rad/s): de waarde die van de gyro wordt afgetrokken.
    double rate_dps() const { return trim_dps_; }
    double rate_rad() const { return trim_dps_ * kRadPerDeg; }
    uint32_t presses() const { return presses_; }       // gebruikersdrukken gezien sinds de start (ook zonder leren)
    uint32_t learned() const { return learned_; }       // drukken die de trim hebben bijgewerkt
    uint32_t skipped() const { return skipped_; }
    bool has_reference() const { return !hist_.empty(); }

    // Beginwaarde (uit het bewaarde bestand of de analyse); begrensd. learned = eerder geleerde drukken.
    void set(double dps, uint32_t learned = 0) {
        trim_dps_ = std::isfinite(dps) ? std::clamp(dps, -cfg_.limit_dps, cfg_.limit_dps) : 0.0;
        learned_ = learned;
        hist_.clear();
    }
    void clear() { trim_dps_ = 0; learned_ = 0; presses_ = 0; skipped_ = 0; hist_.clear(); applied_deg_ = 0; }
    // De yaw-referentie is onderbroken (verbinding weg, gat, magnetometer corrigeert, modus gewisseld): de volgende
    // druk is weer een eerste druk.
    void invalidate() { hist_.clear(); }

    // Extra draaisnelheid in het controllerstelsel (rad/s) die van de gyro moet worden afgetrokken.
    Vec3 body_rate(Quat q_wb) const {
        if (trim_dps_ == 0) return {};
        return rotate(conj(q_wb), Vec3{0, 0, 1}) * rate_rad();
    }
    // De pijplijn meldt per integratiestap hoeveel tijd de trim is toegepast (voor de ruwe yaw).
    void advance(double dt_s) { if (dt_s > 0) applied_deg_ += trim_dps_ * dt_s; }

    // Een gebruikers-centreerdruk op tijd t_s (willekeurige oorsprong, seconden) bij wereld-yaw yaw_rad (rad).
    // valid=false: tussendoor was de yaw-referentie onderbroken; deze druk wordt alleen de nieuwe referentie.
    YawTrimUpdate press(double t_s, double yaw_rad, bool valid = true) {
        YawTrimUpdate u;
        ++presses_;
        if (!valid) hist_.clear();
        // Ruwe yaw (graden, uitgerold): de toegepaste trim verhoogt de yaw met trim*dt, dat halen we er weer af.
        double raw;
        if (hist_.empty()) {
            raw = 0;
        } else {
            const double d = wrap_pi(yaw_rad - last_yaw_) * kDegPerRad;
            if (std::fabs(d) >= cfg_.max_drift_deg) {   // geen drift maar een sprong: opnieuw beginnen
                hist_.clear();
                raw = 0;
            } else {
                raw = hist_.back().raw + d - (applied_deg_ - hist_.back().applied);
            }
        }
        const bool had = !hist_.empty();
        if (had) { u.interval_s = t_s - hist_.back().t; u.drift_deg = wrap_pi(yaw_rad - last_yaw_) * kDegPerRad; }
        hist_.push_back({t_s, raw, applied_deg_});
        last_yaw_ = yaw_rad;
        while (hist_.size() > kHist || (hist_.size() > 1 && t_s - hist_.front().t > 2 * cfg_.max_interval_s)) hist_.pop_front();
        if (!had) { u.why = "eerste druk"; return u; }
        if (!cfg_.enabled) { u.why = "leren uit"; return u; }
        // Paarhellingen van de nieuwste druk naar alle eerdere punten binnen het toegestane bereik, plus de eerdere paren.
        struct Slope { double v, w, dt; };
        Slope sl[kHist * kHist / 2 + 1];
        size_t n = 0;
        for (size_t i = 0; i + 1 < hist_.size(); ++i) {
            for (size_t j = i + 1; j < hist_.size(); ++j) {
                const double dt = hist_[j].t - hist_[i].t;
                if (dt < cfg_.min_interval_s || dt > cfg_.max_interval_s) continue;
                const double dr = hist_[j].raw - hist_[i].raw;
                if (std::fabs(dr) >= cfg_.max_drift_deg + std::fabs(hist_[j].applied - hist_[i].applied)) continue;
                sl[n++] = {dr / dt, dt * dt, dt};
            }
        }
        if (n == 0) { ++skipped_; u.why = u.interval_s < cfg_.min_interval_s ? "te kort" : "te lang"; return u; }
        // Gewogen mediaan van de ruwe yaw-snelheid (graden/s).
        std::sort(sl, sl + n, [](const Slope& a, const Slope& b) { return a.v < b.v; });
        double wsum = 0, tsum = 0;
        for (size_t k = 0; k < n; ++k) { wsum += sl[k].w; tsum += sl[k].w * sl[k].dt; }
        double acc = 0, rho = sl[n - 1].v;
        for (size_t k = 0; k < n; ++k) { acc += sl[k].w; if (acc >= wsum / 2) { rho = sl[k].v; break; } }
        const double T = tsum / wsum;                     // gewogen gemiddelde tussentijd
        const double target = std::clamp(-rho, -cfg_.limit_dps, cfg_.limit_dps);
        const double w = cfg_.gain * T * T / (T * T + cfg_.short_s * cfg_.short_s);
        double step = std::clamp(w * (target - trim_dps_), -cfg_.step_max_dps, cfg_.step_max_dps);
        const double next = std::clamp(trim_dps_ + step, -cfg_.limit_dps, cfg_.limit_dps);
        step = next - trim_dps_;
        trim_dps_ = next;
        ++learned_;
        u.learned = true;
        u.step_dps = step;
        return u;
    }

private:
    static constexpr size_t kHist = 8;
    struct Pt { double t, raw, applied; };
    YawTrimConfig cfg_;
    double trim_dps_ = 0;
    uint32_t presses_ = 0, learned_ = 0, skipped_ = 0;
    std::deque<Pt> hist_;
    double last_yaw_ = 0;
    double applied_deg_ = 0;   // som van trim*dt (graden) sinds de start
};

}  // namespace ravengun::core
