#include "ravengun/output/aim_clock.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::output {

void AimClock::set_config(const OutputClockConfig& c) {
    cfg_ = c;
    cfg_.hz = std::isfinite(c.hz) && c.hz > 0 ? c.hz : 0.0;
    cfg_.max_extrap_ms = std::isfinite(c.max_extrap_ms) ? std::max(0.0, c.max_extrap_ms) : 0.0;
    cfg_.taper = std::isfinite(c.taper) ? std::clamp(c.taper, 0.0, 1.0) : 0.0;
    have_t0_ = false;  // nieuw rooster
}

int64_t AimClock::period_ns() const {
    return cfg_.hz > 0 ? static_cast<int64_t>(std::llround(1e9 / cfg_.hz)) : 0;
}

void AimClock::on_sample(const core::InputSample& s) {
    if (!s.aim_valid) { anchor_.valid = false; return; }
    on_report(s.t_eval_ns, s.aim.x, s.aim.y, s.aim_vx, s.aim_vy, s.age_ms);
}

void AimClock::on_report(int64_t t_ns, double x, double y, double vx, double vy, double age_ms) {
    if (!std::isfinite(x) || !std::isfinite(y)) { anchor_.valid = false; return; }
    const double age = cfg_.age_comp && std::isfinite(age_ms) ? std::clamp(age_ms, 0.0, 40.0) : 0.0;
    anchor_.valid = true;
    anchor_.t_ns = t_ns - static_cast<int64_t>(age * 1e6);
    anchor_.x = x;
    anchor_.y = y;
    anchor_.vx = std::isfinite(vx) ? vx : 0.0;
    anchor_.vy = std::isfinite(vy) ? vy : 0.0;
}

std::optional<AimPos> AimClock::at(int64_t t_ns) const {
    if (!anchor_.valid) return std::nullopt;
    AimPos p;
    p.x = anchor_.x;
    p.y = anchor_.y;
    double dt = static_cast<double>(t_ns - anchor_.t_ns) / 1e9;
    if (dt <= 0) return p;
    const double lim = cfg_.max_extrap_ms * 1e-3;
    p.dt_s = dt;
    p.held = dt > lim;
    const double d = std::min(dt, lim);
    // Verplaatsing: v * (d - taper * d^2 / (2 * lim)); bij taper = 1 loopt de snelheid lineair naar 0 op de limiet.
    const double eff = lim > 0 ? d - cfg_.taper * d * d / (2.0 * lim) : 0.0;
    p.x += anchor_.vx * eff;
    p.y += anchor_.vy * eff;
    p.extrapolating = !p.held && eff > 0;
    return p;
}

int64_t AimClock::next_tick_after(int64_t now_ns) {
    const int64_t per = period_ns();
    if (per <= 0) return now_ns + 100'000'000;  // klok uit: af en toe kijken of hij aan is gezet
    if (!have_t0_) { have_t0_ = true; t0_ns_ = now_ns; }
    if (now_ns < t0_ns_) return t0_ns_;
    const int64_t k = (now_ns - t0_ns_) / per + 1;
    return t0_ns_ + k * per;
}

}  // namespace ravengun::output
