#include "ravengun/rec/drift.hpp"

#include <algorithm>
#include <cmath>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/rec/replay.hpp"

namespace ravengun::rec {

using namespace ravengun::core;

double pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = std::min(x.size(), y.size());
    if (n < 3) return NAN;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxx = 0, syy = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    if (!(sxx > 1e-18) || !(syy > 1e-18)) return NAN;
    return sxy / std::sqrt(sxx * syy);
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return NAN;
    size_t k = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

namespace {

struct Acc {
    uint64_t reports = 0, lost = 0;
    double rotated = 0, max_rate = 0, longest_gap_ms = 0;
    int64_t saturated = -1;
    int temp_begin = -1, temp_end = -1;
};

}  // namespace

DriftResult compute_drift(const std::vector<Record>& recs, const DriftOptions& opt) {
    DriftResult res;
    if (recs.empty() || recs[0].kind != Kind::Header) { res.error = "geen opname"; return res; }
    const int64_t t0 = recs[0].header.t0_ns;

    ReplayOptions ro;
    ro.pipeline.fusion_mode = opt.mode;
    ro.pipeline.bias = opt.bias;
    ro.have_bias_init = opt.have_bias_init;
    ro.bias_init = opt.bias_init;
    ro.bias_init_tc = opt.bias_init_tc;
    if (opt.have_cal) { ro.have_mag_cal = true; ro.mag_cal = opt.cal; }
    ro.pipeline.trim = opt.trim;
    ro.trim_marker_mask = opt.marker_mask ? opt.marker_mask : static_cast<uint32_t>(kBtnPS);
    ro.have_trim_init = opt.have_trim_init;
    ro.trim_init_dps = opt.trim_init_dps;
    double cur_trim = opt.have_trim_init ? opt.trim_init_dps : 0.0;
    ro.on_marker = [&](const Pipeline& p, const InputSample&) { cur_trim = p.trim().rate_dps(); };

    bool have_ref = false;
    double ref_yaw = 0, ref_pitch = 0, ref_t = 0;
    Quat prev_q{};
    bool have_prev_q = false;
    int prev_seq = -1;
    int64_t prev_rx = 0;
    bool have_prev_rx = false;
    Acc acc;
    uint64_t reports_seen = 0;
    int last_tc = -1;
    double cum_yaw = 0, cum_first_t = 0, cum_last_t = 0, cum_prev_yaw = 0;
    size_t cum_n = 0;

    ro.on_sample = [&](const InputSample& s, const RawReport* r) {
        if (!r) return;
        ++reports_seen;
        const double t = static_cast<double>(r->t_rx_ns - t0) / 1e9;
        // Verloren rapporten en gaten.
        ++acc.reports;
        if (prev_seq >= 0 && r->hid_seq >= 0) {
            const int d = (r->hid_seq - prev_seq) & 15;  // 4-bit volgnummer: een volledige ronde (16) is niet te zien
            acc.lost += d ? static_cast<uint64_t>(d - 1) : 0;
        }
        if (r->hid_seq >= 0) prev_seq = r->hid_seq;
        if (have_prev_rx) acc.longest_gap_ms = std::max(acc.longest_gap_ms, static_cast<double>(r->t_rx_ns - prev_rx) / 1e6);
        prev_rx = r->t_rx_ns;
        have_prev_rx = true;
        // Snelheid en gedraaide hoek.
        Vec3 g = (r->gyro[0] + r->gyro[1]) * 0.5 - s.gyro_bias;
        acc.max_rate = std::max(acc.max_rate, norm(g) * kDegPerRad);
        if (s.orientation_valid) {
            if (have_prev_q) acc.rotated += angle_between(prev_q, s.q_wb) * kDegPerRad;
            prev_q = s.q_wb;
            have_prev_q = true;
        } else {
            have_prev_q = false;
        }
        if (r->has_raw) {
            if (acc.saturated < 0) acc.saturated = 0;
            for (int f = 0; f < r->subsamples && f < 2; ++f)
                for (int a = 0; a < 3; ++a)
                    if (std::abs(r->gyro_raw[f][a]) >= opt.saturation_raw) { ++acc.saturated; break; }
        }
        if (r->temperature_raw >= 0) {
            last_tc = r->temperature_raw;
            if (acc.temp_begin < 0) acc.temp_begin = r->temperature_raw;
            acc.temp_end = r->temperature_raw;
        }

        if (!(s.pressed & (opt.marker_mask ? opt.marker_mask : static_cast<uint32_t>(kBtnPS)))) return;
        ++res.presses;
        if (!s.orientation_valid) return;  // geen oriëntatie: dit is geen bruikbaar meetpunt
        AimAngles wa = world_angles(s.q_wb);
        const double yaw = wa.yaw_rad * kDegPerRad, pitch = wa.pitch_rad * kDegPerRad;
        if (cum_n == 0) cum_first_t = t;
        else cum_yaw += wrap_pi((yaw - cum_prev_yaw) * kRadPerDeg) * kDegPerRad;
        cum_prev_yaw = yaw;
        cum_last_t = t;
        ++cum_n;
        if (have_ref) {
            const double dur = t - ref_t;
            if (dur < opt.min_interval_s) {
                ++res.skipped_short;
            } else {
                DriftInterval d;
                d.t0_s = ref_t; d.t1_s = t; d.duration_s = dur;
                d.yaw_deg = wrap_pi((yaw - ref_yaw) * kRadPerDeg) * kDegPerRad;
                d.pitch_deg = pitch - ref_pitch;
                d.yaw_per_min = d.yaw_deg / dur * 60;
                d.pitch_per_min = d.pitch_deg / dur * 60;
                d.rotated_deg = acc.rotated;
                d.max_rate_dps = acc.max_rate;
                d.reports = acc.reports;
                d.lost = acc.lost;
                d.lost_pct = acc.reports + acc.lost ? 100.0 * static_cast<double>(acc.lost) / static_cast<double>(acc.reports + acc.lost) : 0;
                d.longest_gap_ms = acc.longest_gap_ms;
                d.saturated = acc.saturated;
                d.temp_begin = acc.temp_begin;
                d.temp_end = acc.temp_end;
                d.trim_dps = cur_trim;
                res.intervals.push_back(d);
            }
        }
        have_ref = true;
        ref_yaw = yaw; ref_pitch = pitch; ref_t = t;
        acc = Acc{};  // volgende interval; seq en rx-tijd lopen door
        acc.temp_begin = r->temperature_raw >= 0 ? r->temperature_raw : -1;
        acc.temp_end = acc.temp_begin;
    };
    ReplayResult rr = replay_records(recs, ro);
    if (!rr.ok) { res.error = rr.error.empty() ? "replay mislukt" : rr.error; return res; }
    res.ok = true;
    res.final_bias = rr.last.gyro_bias;
    res.final_state = rr.last.bias_state;
    res.final_source = rr.last.bias_source;
    res.final_tc = last_tc;
    res.final_trim_dps = rr.trim_dps;
    res.trim_learned = rr.trim_learned;
    res.optical_s = rr.optical_s;
    res.optical_windows = rr.optical_windows;
    res.optical_still = rr.optical_still;
    res.optical_rejected = rr.optical_rejected;

    // Samenvatting.
    DriftSummary& sm = res.summary;
    sm.intervals = res.intervals.size();
    std::vector<double> absall, absyaw, yawabs, rot, rate, lost, dur;
    for (const auto& d : res.intervals) {
        absall.push_back(std::hypot(d.yaw_per_min, d.pitch_per_min));
        absyaw.push_back(std::fabs(d.yaw_per_min));
        yawabs.push_back(std::fabs(d.yaw_deg));
        rot.push_back(d.rotated_deg);
        rate.push_back(d.max_rate_dps);
        lost.push_back(static_cast<double>(d.lost));
        dur.push_back(d.duration_s);
    }
    if (cum_n >= 2 && cum_last_t > cum_first_t) {
        sm.total_yaw_deg = cum_yaw;
        sm.total_s = cum_last_t - cum_first_t;
        sm.total_yaw_per_min = cum_yaw / sm.total_s * 60;
    }
    sm.median_abs_per_min = percentile(absall, 0.5);
    sm.p90_abs_per_min = percentile(absall, 0.9);
    sm.median_yaw_abs_per_min = percentile(absyaw, 0.5);
    sm.p90_yaw_abs_per_min = percentile(absyaw, 0.9);
    sm.r_rotated = pearson(yawabs, rot);
    sm.r_max_rate = pearson(yawabs, rate);
    sm.r_lost = pearson(yawabs, lost);
    sm.r_duration = pearson(yawabs, dur);
    return res;
}

}  // namespace ravengun::rec
