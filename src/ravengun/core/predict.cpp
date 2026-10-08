#include "ravengun/core/predict.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::core {

void RatePredictor::update(Vec3 w, double dt) {
    if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) return;
    if (!have_) { have_ = true; w_ = w; a_ = {}; return; }
    if (!(dt > 0) || !std::isfinite(dt)) return;
    const double aw = 1.0 - std::exp(-dt * 1e3 / std::max(0.1, cfg_.rate_tau_ms));
    const Vec3 w_old = w_;
    w_ = w_ + (w - w_) * aw;
    const Vec3 da = (w_ - w_old) * (1.0 / dt);
    const double aa = 1.0 - std::exp(-dt * 1e3 / std::max(0.1, cfg_.accel_tau_ms));
    a_ = a_ + (da - a_) * aa;
}

double RatePredictor::gain() const {
    const double s = norm(w_) * kDegPerRad;
    const double lo = cfg_.speed_lo_dps, hi = std::max(cfg_.speed_hi_dps, lo + 1e-6);
    if (s <= lo) return 0;
    if (s >= hi) return 1;
    const double x = (s - lo) / (hi - lo);
    return x * x * (3 - 2 * x);
}

Vec3 RatePredictor::rotation(double horizon_s) const {
    if (!cfg_.enabled() || !have_ || !(horizon_s > 0)) return {};
    const double g = gain();
    if (g <= 0) return {};
    Vec3 w = w_;
    Vec3 a{};
    if (cfg_.accel_gain > 0) {
        // Het snelheidsfilter loopt bij een versnelling ongeveer rate_tau * a achter: daarvoor corrigeren. Draait de
        // gecorrigeerde snelheid om (de beweging is volgens het model al gestopt), dan is de snelheid nul: het filter
        // en de versnellingsschatting lopen na een abrupte stop achter, en dat mag nooit een zwiep terug geven.
        a = a_ * cfg_.accel_gain;
        Vec3 wn = w + a * (cfg_.rate_tau_ms * 1e-3);
        w = dot(wn, w_) > 0 ? wn : Vec3{};
    }
    const double s = norm(w);
    if (s < 1e-9) return {};
    const Vec3 u = w * (1.0 / s);
    const double a_par = dot(a, u);
    const Vec3 a_perp = a - u * a_par;
    double t = horizon_s;
    // Afremmend: de beweging komt volgens het model na s/|a_par| tot stilstand; daarna niet verder draaien.
    if (a_par < 0 && t * (-a_par) > s) t = s / (-a_par);
    Vec3 phi = u * (s * t + 0.5 * a_par * t * t) + a_perp * (0.5 * t * t);
    const double n = norm(phi);
    const double cap = cfg_.max_angle_deg * kRadPerDeg;
    if (n > cap && n > 0) phi = phi * (cap / n);
    return phi * g;
}

Quat RatePredictor::apply(Quat q, double horizon_s) const {
    const Vec3 phi = rotation(horizon_s);
    const double n = norm(phi);
    if (n <= 0) return q;
    return normalized(from_axis_angle(phi, n) * q);
}

}  // namespace ravengun::core
