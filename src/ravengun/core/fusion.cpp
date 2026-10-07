#include "ravengun/core/fusion.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::core {

const char* to_string(BiasState s) {
    switch (s) {
        case BiasState::None: return "none";
        case BiasState::Estimating: return "estimating";
        case BiasState::Estimated: return "estimated";
    }
    return "none";
}

void GyroBiasEstimator::reset() {
    win_.clear();
    win_span_ = 0;
    bias_ = {};
    still_ = false;
    still_total_ = 0;
}

BiasState GyroBiasEstimator::state() const {
    if (still_total_ <= 0) return BiasState::None;
    if (still_total_ < cfg_.settled_after_s) return BiasState::Estimating;
    return BiasState::Estimated;
}

bool GyroBiasEstimator::update(Vec3 gyro, Vec3 accel, double dt) {
    if (!(dt > 0) || !std::isfinite(dt)) return still_;
    // Venster bijhouden op basis van echte dt (leeftijd per sample).
    for (auto& s : win_) s.age += dt;
    win_.push_back({0.0, gyro, norm(accel)});
    while (!win_.empty() && win_.front().age > cfg_.window_s) win_.pop_front();
    win_span_ = win_.empty() ? 0 : win_.front().age;

    still_ = false;
    if (win_.size() >= 8 && win_span_ >= cfg_.window_s * 0.8) {
        Vec3 mean{};
        bool acc_ok = true;
        for (const auto& s : win_) {
            mean = mean + s.g;
            if (std::fabs(s.an - 1.0) > cfg_.accel_norm_tol) acc_ok = false;
        }
        mean = mean * (1.0 / static_cast<double>(win_.size()));
        Vec3 var{};
        for (const auto& s : win_) {
            Vec3 d = s.g - mean;
            var = var + Vec3{d.x * d.x, d.y * d.y, d.z * d.z};
        }
        var = var * (1.0 / static_cast<double>(win_.size()));
        double smax = std::sqrt(std::max({var.x, var.y, var.z}));
        Vec3 off = mean - bias_;
        // Voor de eerste schatting is de bias nog 0: dan geldt dezelfde grens op |mean|.
        if (acc_ok && smax <= cfg_.gyro_std_max && norm(off) <= cfg_.gyro_mean_max) {
            still_ = true;
            still_total_ += dt;
            double a = dt / std::min(still_total_, cfg_.tau_s);
            bias_ = bias_ + (mean - bias_) * std::min(1.0, a);
        }
    }
    return still_;
}

void OrientationFilter::init_from_accel(Vec3 accel) {
    double n = norm(accel);
    if (n <= 1e-6) { q_ = Quat{}; init_ = true; return; }
    Vec3 a = accel * (1.0 / n);
    Vec3 up{0, 0, 1};
    // Kortste rotatie die de gemeten "omhoog" (controllerstelsel) naar wereld-Z draait.
    double c = dot(a, up);
    if (c < -0.999999) {
        q_ = from_axis_angle({1, 0, 0}, kPi);
    } else {
        Vec3 ax = cross(a, up);
        q_ = normalized(Quat{1 + c, ax.x, ax.y, ax.z});
    }
    init_ = true;
}

bool OrientationFilter::update(Vec3 g, Vec3 accel, double dt) {
    last_accel_used_ = false;
    if (!init_) init_from_accel(accel);
    if (!(dt > 0) || !std::isfinite(dt) || dt > cfg_.max_dt_s) return false;
    double an = norm(accel);
    if (an > 1e-6 && std::fabs(an - 1.0) <= cfg_.accel_gate && cfg_.kp > 0) {
        Vec3 a = accel * (1.0 / an);
        // Verwachte "omhoog" in het controllerstelsel volgens de huidige schatting.
        Vec3 v = rotate(conj(q_), Vec3{0, 0, 1});
        Vec3 e = cross(a, v);
        g = g + e * cfg_.kp;
        last_accel_used_ = true;
    }
    // Exacte integratie van een constante hoeksnelheid over dt (body-frame, rechtsvermenigvuldiging).
    double w = norm(g);
    Quat dq = w > 1e-12 ? from_axis_angle(g, w * dt) : Quat{};
    q_ = normalized(q_ * dq);
    return true;
}

}  // namespace ravengun::core
