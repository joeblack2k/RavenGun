#include "ravengun/core/mag_yaw.hpp"

#include "ravengun/core/aim.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ravengun::core {

const char* to_string(FusionMode m) {
    switch (m) {
        case FusionMode::Mahony: return "mahony";
        case FusionMode::MahonyMag: return "mahony_mag";
    }
    return "mahony";
}

bool parse_fusion_mode(const char* s, FusionMode& out) {
    if (!s) return false;
    if (!std::strcmp(s, "mahony")) { out = FusionMode::Mahony; return true; }
    if (!std::strcmp(s, "mahony_mag") || !std::strcmp(s, "mahony-mag")) { out = FusionMode::MahonyMag; return true; }
    return false;
}

void MagYawCorrector::set_calibration(const MagCalibration& c) {
    cal_ = c;
    have_cal_ = true;
    det_ = MagDisturbanceDetector(cfg_.detector);
    det_.set_reference_dip(c.dip_ref_deg);
    double b = b_v_;
    reset();
    b_v_ = b;  // de geleerde bias hoort bij de gyro, niet bij de kalibratie
    st_.vertical_bias_dps = b_v_ * kDegPerRad;
}

void MagYawCorrector::clear_calibration() {
    have_cal_ = false;
    reset();
}

void MagYawCorrector::reset() {
    double total = st_.total_deg;
    det_.reset();
    st_ = MagYawStatus{};
    st_.calibrated = have_cal_;
    st_.total_deg = total;
    age_ = 0;
    lock_c_ = lock_s_ = lock_t_ = 0;
    phi_ref_ = 0;
    f_ = d_ = 0;
    P00_ = P01_ = P11_ = 0;
    used_t_ = 0;
    b_v_ = 0;
    reacq_t_ = 0;
}

void MagYawCorrector::relock() {
    st_.locked = false;
    st_.reacquire = false;
    lock_c_ = lock_s_ = lock_t_ = 0;
    f_ = d_ = 0;
    P00_ = P01_ = P11_ = 0;
    used_t_ = 0;
    st_.pending_deg = st_.drift_dps = st_.sigma_deg = 0;
}

void MagYawCorrector::on_interrupt() {
    det_.reset();  // de gyro-consistentie over het gat is onbekend
    if (!st_.locked) return;
    double s = cfg_.reacquire_sigma_deg * kRadPerDeg;
    P00_ += s * s;
    st_.reacquire = true;
    reacq_t_ = 0;
}

Vec3 MagYawCorrector::bias_body(Quat q) const {
    if (b_v_ == 0) return {};
    return rotate(conj(q), Vec3{0, 0, 1}) * b_v_;
}

void MagYawCorrector::update(Quat& q, const int mag_raw[3], Vec3 accel, Vec3 gyro, double dt, bool apply, bool bias_settled) {
    st_.applying = false;
    st_.used = false;
    st_.rate_dps = 0;
    if (!(dt >= 0) || !std::isfinite(dt)) dt = 0;
    if (!have_cal_) return;
    age_ += dt;
    ++st_.samples;
    st_.unusable_s += dt;

    Vec3 m = mag_apply(cal_, mag_raw);
    MagCheck c = det_.update(m, accel, gyro, dt);
    st_.last = c;
    st_.state = det_.state();
    double w_dps = norm(gyro) * kDegPerRad;

    bool usable = c.accepted && det_.state() == MagState::Ok;
    double phi = 0;
    if (usable) {
        // Richting van het veld in de wereld, via de kompasrichting van de richtas (TRIAD met de
        // accelerometer als "omhoog") en de yaw van de schatting: φ = yaw_q - H. Niet via de tilt van q:
        // een tiltfout van Mahony (resterende X/Y-bias / kp) werkt bij een dip van 60° met tan(dip) = 1,7
        // keer door in φ en is een langzame fout die het filter niet wegmiddelt (gemeten op tafel met
        // bevroren bias: 1,4° wegloop). De accelerometer heeft ruis maar geen langzame fout.
        double an = norm(accel);
        if (!(an > 0) || std::fabs(an - 1) > cfg_.accel_tol_g) { usable = false; ++st_.accel_skipped; }
        else {
            Vec3 up = accel * (1 / an);
            Vec3 nh = m - up * dot(m, up);
            Vec3 d = kAimAxisBody - up * dot(kAimAxisBody, up);
            double n = norm(m);
            if (!(n > 0) || norm(nh) < cfg_.min_horizontal * n || norm(d) < 0.3) { usable = false; ++st_.weak_horizontal; }
            else {
                nh = nh * (1 / norm(nh));
                double H = std::atan2(dot(d, cross(nh, up)), dot(d, nh));
                phi = wrap_pi(world_angles(q).yaw_rad - H);
            }
        }
    }
    if (usable && w_dps > cfg_.max_gyro_dps) { usable = false; ++st_.fast_skipped; }

    // Vergrendelen (pas als de stilstandsbias een schatting heeft, anders drijft de referentie mee).
    if (!st_.locked) {
        if (!bias_settled && age_ < cfg_.lock_wait_bias_s) return;
        if (!usable) return;
        lock_c_ += std::cos(phi);
        lock_s_ += std::sin(phi);
        lock_t_ += dt;
        st_.unusable_s = 0;
        if (lock_t_ >= cfg_.lock_s) {
            phi_ref_ = std::atan2(lock_s_, lock_c_);
            st_.locked = true;
            f_ = d_ = 0;
            double n_eff = std::max(1.0, lock_t_ * 77.0);
            double s0 = cfg_.meas_sigma_deg * kRadPerDeg / std::sqrt(n_eff), s1 = cfg_.init_drift_sigma_dps * kRadPerDeg;
            P00_ = s0 * s0;
            P01_ = 0;
            P11_ = s1 * s1;
            used_t_ = 0;
        }
        return;
    }

    // Kalman: voorspellen.
    const double qf = std::pow(cfg_.yaw_rw_deg_rs * kRadPerDeg, 2), qd = std::pow(cfg_.drift_rw_dps_rs * kRadPerDeg, 2);
    f_ += d_ * dt;
    P00_ += 2 * dt * P01_ + dt * dt * P11_ + qf * dt;
    P01_ += dt * P11_;
    P11_ += qd * dt;

    // Meten.
    if (usable) {
        double R = std::pow(cfg_.meas_sigma_deg * kRadPerDeg, 2);
        double y = wrap_pi(wrap_pi(phi - phi_ref_) - f_);
        double S = P00_ + R;
        if (y * y / S > cfg_.nis_gate) {
            ++st_.innov_rejected;
            usable = false;
            // Aanhoudend buiten de poort (bijv. een echte verschuiving na een lange verstoring): onzekerheid
            // laten groeien zodat het filter de meting na een paar seconden weer toelaat.
            P00_ += 0.02 * y * y * std::min(1.0, dt / 0.25);
        } else {
            double K0 = P00_ / S, K1 = P01_ / S;
            f_ += K0 * y;
            d_ += K1 * y;
            double p00 = P00_, p01 = P01_;
            P00_ = p00 - K0 * p00;
            P01_ = p01 - K0 * p01;
            P11_ = P11_ - K1 * p01;
            st_.used = true;
            ++st_.used_samples;
            st_.unusable_s = 0;
            used_t_ += dt;
        }
    }

    if (apply) {
        // Drift wegregelen via b_v (begrensde veranderingssnelheid). Een bias-stap δ verhoogt de helling
        // van e met δ (zie mag_yaw.hpp), dus δ = -d.
        if (used_t_ >= cfg_.bias_min_time_s) {
            double slew = cfg_.bias_slew_dps2 * kRadPerDeg * dt, lim = cfg_.bias_max_dps * kRadPerDeg;
            double nb = std::clamp(b_v_ + std::clamp(-d_, -slew, slew), -lim, lim);
            double delta = nb - b_v_;
            b_v_ = nb;
            d_ += delta;
        }
        // Hoekfout begrensd wegwerken.
        if (st_.reacquire) {
            reacq_t_ += dt;
            if (st_.used && P00_ < std::pow(2 * cfg_.meas_sigma_deg * kRadPerDeg, 2) && std::fabs(f_) < 0.5 * kRadPerDeg) st_.reacquire = false;
            if (reacq_t_ > cfg_.reacquire_max_s) st_.reacquire = false;
        }
        double lim;
        if (st_.reacquire) lim = cfg_.reacquire_rate_dps;
        else if (w_dps < cfg_.still_gyro_dps) lim = cfg_.rate_still_dps;
        else lim = std::min(cfg_.rate_moving_dps, cfg_.rate_still_dps + cfg_.rate_per_motion * w_dps);
        double tau = st_.reacquire ? 0.5 : cfg_.tau_corr_s;
        double max = lim * kRadPerDeg * dt;
        double dlt = std::clamp(f_ / tau * dt, -max, max);
        if (dlt != 0) {
            q = normalized(from_axis_angle({0, 0, 1}, dlt) * q);
            f_ -= dlt;
            st_.total_deg += dlt * kDegPerRad;
            st_.max_step_deg = std::max(st_.max_step_deg, std::fabs(dlt) * kDegPerRad);
        }
        st_.applying = true;
        st_.rate_dps = dt > 0 ? dlt / dt * kDegPerRad : 0;
        st_.rate_limit_dps = lim;
    }
    st_.pending_deg = f_ * kDegPerRad;
    st_.drift_dps = d_ * kDegPerRad;
    st_.sigma_deg = std::sqrt(std::max(0.0, P00_)) * kDegPerRad;
    st_.vertical_bias_dps = b_v_ * kDegPerRad;
}

}  // namespace ravengun::core
