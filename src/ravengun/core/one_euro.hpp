#pragma once
// 1€-filter (Casiez, Roussel, Vogel, CHI 2012), met variabele dt.
// Eén filterlaag op de aim; uitschakelbaar (dan is de uitvoer gelijk aan de invoer).
#include <cmath>

namespace ravengun::core {

struct OneEuroConfig {
    bool enabled = true;
    double min_cutoff_hz = 1.0;  // afsnijfrequentie bij stilstand (lager = minder jitter, meer vertraging)
    double beta = 0.5;           // toename van de afsnijfrequentie per eenheid snelheid (eenheden/s)
    double d_cutoff_hz = 1.0;    // afsnijfrequentie voor de snelheidsschatting
};

class OneEuro {
public:
    explicit OneEuro(OneEuroConfig cfg = {}) : cfg_(cfg) {}
    // dt in seconden; dt <= 0 of niet-eindig: geen tijdstap, vorige uitvoer (of invoer bij eerste sample).
    double filter(double x, double dt);
    void reset() { have_ = false; }
    void set_config(const OneEuroConfig& c) { cfg_ = c; }
    const OneEuroConfig& config() const { return cfg_; }
    static double alpha(double cutoff_hz, double dt);

private:
    OneEuroConfig cfg_;
    bool have_ = false;
    double x_prev_ = 0, dx_prev_ = 0;
};

inline double OneEuro::alpha(double cutoff_hz, double dt) {
    double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff_hz);
    return 1.0 / (1.0 + tau / dt);
}

inline double OneEuro::filter(double x, double dt) {
    if (!cfg_.enabled) { have_ = false; return x; }
    if (!have_) { have_ = true; x_prev_ = x; dx_prev_ = 0; return x; }
    if (!(dt > 0) || !std::isfinite(dt)) return x_prev_;
    double dx = (x - x_prev_) / dt;
    double ad = alpha(cfg_.d_cutoff_hz, dt);
    double edx = ad * dx + (1 - ad) * dx_prev_;
    double cutoff = cfg_.min_cutoff_hz + cfg_.beta * std::fabs(edx);
    double a = alpha(cutoff, dt);
    double y = a * x + (1 - a) * x_prev_;
    x_prev_ = y;
    dx_prev_ = edx;
    return y;
}

}  // namespace ravengun::core
