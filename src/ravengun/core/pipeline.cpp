#include "ravengun/core/pipeline.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::core {

namespace {
constexpr size_t kIntervalRing = 16384;
constexpr size_t kTimeRing = 4096;
constexpr double kLongDtS = 0.05;
}  // namespace

// ---------------- ReportStats ----------------

void ReportStats::reset() { *this = ReportStats{}; }

void ReportStats::add_interval(double ms) {
    ++intervals;
    size_t b = 0;
    while (b + 1 < kBucketUpperMs.size() && ms >= kBucketUpperMs[b]) ++b;
    ++buckets[b];
    if (ms > max_ms) max_ms = ms;
    if (ring_.size() < kIntervalRing) ring_.push_back(ms);
    else { ring_[ring_pos_] = ms; ring_pos_ = (ring_pos_ + 1) % kIntervalRing; }
}

void ReportStats::add_report(int64_t t_ns) {
    ++reports;
    if (first_t_ns == 0) first_t_ns = t_ns;
    last_t_ns = t_ns;
    if (times_.size() < kTimeRing) times_.push_back(t_ns);
    else { times_[times_pos_] = t_ns; times_pos_ = (times_pos_ + 1) % kTimeRing; }
}

double ReportStats::percentile(double p) const {
    if (ring_.empty()) return 0;
    std::vector<double> v = ring_;
    size_t k = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

double ReportStats::rate(int64_t now_ns, double window_s) const {
    if (reports < 2 || first_t_ns == 0) return 0;
    double span = std::min(window_s, static_cast<double>(now_ns - first_t_ns) / 1e9);
    if (span <= 0) return 0;
    int64_t from = now_ns - static_cast<int64_t>(span * 1e9);
    size_t n = 0;
    for (int64_t t : times_) if (t > from && t <= now_ns) ++n;
    return static_cast<double>(n) / span;
}

// ---------------- Pipeline ----------------

Pipeline::Pipeline(PipelineConfig cfg)
    : cfg_(cfg), bias_(cfg.bias), fusion_(cfg.fusion), mag_(cfg.mag), aim_(cfg.aim), fx_(cfg.filter), fy_(cfg.filter), clock_(cfg.clock) {}

void Pipeline::on_connect(const std::string& id, int64_t now_ns) {
    if (!controller_id_.empty() && id != controller_id_) {
        // Andere controller: geen enkele toestand van de vorige hergebruiken.
        bias_.reset();
        fusion_.reset();
        aim_.clear_center();
        yaw_ref_ = YawReference::None;
        auto_centered_once_ = false;
        mag_.clear_calibration();  // de kalibratie hoort bij de controller; de daemon zet de nieuwe
    } else {
        mag_.on_interrupt();  // zelfde controller: de yaw-integratie was onderbroken
    }
    controller_id_ = id;
    stats_.reset();
    have_rx_ = false;
    last_hid_seq_ = -1;
    reset_filter_ = true;
    link_ = LinkState::Stale;  // verbonden, maar nog geen rapport
    last_rx_ns_ = now_ns;
    clock_.reset();
    clock_res_.reset();
    last_clock_ = {};
    prev_had_stamp_ = false;
    dt_move_ = dt_rx_ = 0;
    have_prev_gyro_ = false;
}

InputSample Pipeline::make_sample(int64_t t_eval) {
    InputSample s;
    s.seq = ++seq_;
    s.t_eval_ns = t_eval;
    s.t_rx_ns = have_rx_ ? last_rx_ns_ : 0;
    s.controller_id = controller_id_;
    s.link = link_;
    s.mode = TrackingMode::Orientation;
    s.orientation_valid = fusion_.initialized() && link_ == LinkState::Fresh;
    s.q_wb = fusion_.q();
    s.bias_state = bias_.state();
    s.still = bias_.still();
    s.gyro_bias = bias_.bias();
    s.yaw_ref = yaw_ref_;
    const MagYawStatus& ms = mag_.status();
    s.fusion_mode = to_string(cfg_.fusion_mode);
    s.mag_state = mag_.calibrated() ? to_string(ms.state) : "none";
    s.mag_locked = ms.locked;
    bool mag_active = cfg_.fusion_mode == FusionMode::MahonyMag && mag_.calibrated() && ms.locked &&
                      ms.state == MagState::Ok && ms.unusable_s < 1.0;
    s.yaw_source = mag_active ? "magnetometer" : "gyro";
    s.yaw_pending_deg = ms.pending_deg;
    s.yaw_corr_rate_deg_s = ms.rate_dps;
    s.yaw_corr_limit_deg_s = ms.rate_limit_dps;
    s.yaw_corr_total_deg = ms.total_deg;
    s.yaw_vertical_bias_deg_s = ms.vertical_bias_dps;
    s.mag_unusable_s = ms.unusable_s;
    s.center_source = aim_.center_source();
    s.filter_enabled = fx_.config().enabled;
    s.buttons = edges_.state();
    s.trigger = last_.trigger;
    s.accel_g = last_.accel_g;
    s.gyro_rad_s = last_.gyro_rad_s;
    s.battery_raw = last_.battery_raw;
    s.hid_seq = last_.hid_seq;
    if (link_ == LinkState::Disconnected || link_ == LinkState::Lost) s.trigger = 0;
    return s;
}

void Pipeline::fill_aim(InputSample& s, double dt) {
    s.aim_valid = false;
    if (!s.orientation_valid || !aim_.has_center()) return;
    AimAngles rel = aim_.relative(fusion_.q());
    s.yaw_rel_deg = rel.yaw_rad * kDegPerRad;
    s.pitch_rel_deg = rel.pitch_rad * kDegPerRad;
    AimPoint raw = aim_.map(rel.yaw_rad, rel.pitch_rad);
    s.x_raw = raw.x;
    s.y_raw = raw.y;
    if (reset_filter_) { fx_.reset(); fy_.reset(); reset_filter_ = false; }
    double x = fx_.filter(raw.x, dt), y = fy_.filter(raw.y, dt);
    s.aim = AimMapper::clamp(x, y);
    s.aim_valid = true;
}

InputSample Pipeline::on_report(const RawReport& r) {
    double dt = 0;
    if (have_rx_) {
        if (r.t_rx_ns < last_rx_ns_) {
            ++stats_.non_monotonic;
        } else {
            dt = static_cast<double>(r.t_rx_ns - last_rx_ns_) / 1e9;
            stats_.add_interval(dt * 1e3);
        }
    }
    stats_.add_report(r.t_rx_ns);
    if (last_hid_seq_ >= 0 && r.hid_seq >= 0) {
        int d = (r.hid_seq - last_hid_seq_ + 16) % 16;
        if (d > 1) stats_.dropped_seq += static_cast<uint64_t>(d - 1);
    }
    if (r.hid_seq >= 0) last_hid_seq_ = r.hid_seq;

    // Integratie-dt: uit de Move-stempel als dit en het vorige rapport er een hebben (fase 1),
    // anders de ontvangst-dt. report_dt_s in het sample blijft de ontvangst-dt.
    double dt_int = dt;
    bool used_move = false;
    if (r.has_stamp) {
        last_clock_ = clock_.add(r.stamp, r.t_rx_ns);
        if (!last_clock_.first) clock_res_.add(last_clock_.residual_ms);
        if (cfg_.use_move_clock && have_rx_ && prev_had_stamp_ && !last_clock_.first) {
            dt_int = last_clock_.dt_s;
            used_move = true;
        }
    }
    if (have_rx_) { if (used_move) ++dt_move_; else ++dt_rx_; }
    prev_had_stamp_ = r.has_stamp;
    if (r.temperature_raw >= 0) last_temp_ = r.temperature_raw;

    int n = std::clamp(r.subsamples, 1, 2);
    double sub_dt = dt_int / n;
    if (dt_int > cfg_.fusion.max_dt_s) {
        // Te lang gat: rotatie in het gat is onbekend. Niet integreren, wel zichtbaar maken.
        ++stats_.gap_not_integrated;
        if (yaw_ref_ == YawReference::Continuous) yaw_ref_ = YawReference::Interrupted;
        reset_filter_ = true;
        sub_dt = 0;
        have_prev_gyro_ = false;
        mag_.on_interrupt();
    } else if (dt_int > kLongDtS) {
        ++stats_.long_dt;
    }

    if (!fusion_.initialized()) fusion_.init_from_accel(r.accel[0]);
    const bool mag_apply_now = cfg_.fusion_mode == FusionMode::MahonyMag && mag_.calibrated();
    // Integratietijd per subsample; bij een gat (verloren rapporten) eerst het ontbrekende deel met de
    // geïnterpoleerde hoeksnelheid (zie PipelineConfig::interpolate_gaps).
    double fuse_dt = sub_dt, gap_dt = 0;
    if (cfg_.interpolate_gaps && n == 2 && have_prev_gyro_ && sub_dt > 0 && dt_int > 1.5 * cfg_.nominal_report_s) {
        gap_dt = dt_int - cfg_.nominal_report_s;
        fuse_dt = cfg_.nominal_report_s / 2;
        ++stats_.gap_interpolated;
    }
    Vec3 g_sum{}, a_sum{};
    for (int i = 0; i < n; ++i) {
        if (bias_.update(r.gyro[i], r.accel[i], sub_dt)) ++stats_.still_samples;
        Vec3 extra = mag_apply_now ? mag_.bias_body(fusion_.q()) : Vec3{};
        if (i == 0 && gap_dt > 0) {
            Vec3 gm = (prev_gyro_ + r.gyro[0]) * 0.5 - bias_.bias() - extra;
            fusion_.update(gm, r.accel[0], gap_dt);
        }
        Vec3 g = r.gyro[i] - bias_.bias() - extra;
        fusion_.update(g, r.accel[i], fuse_dt);
        g_sum = g_sum + g;
        a_sum = a_sum + r.accel[i];
    }
    prev_gyro_ = r.gyro[n - 1];
    have_prev_gyro_ = true;
    if (mag_.calibrated()) {
        // Eén magnetometersample per rapport, na de integratie (zie mag_yaw.hpp).
        Quat q = fusion_.q();
        mag_.update(q, r.mag_raw, a_sum * (1.0 / n), g_sum * (1.0 / n), sub_dt * n, mag_apply_now,
                    bias_.state() == BiasState::Estimated);
        if (mag_apply_now) fusion_.set_q(q);
    }

    ButtonEdges e = edges_.update(r.buttons);
    link_ = LinkState::Fresh;
    have_rx_ = true;
    last_rx_ns_ = r.t_rx_ns;

    if (cfg_.auto_center_on_start && !auto_centered_once_ && !aim_.has_center()) {
        aim_.recenter(fusion_.q(), CenterSource::AutoInitial);
        auto_centered_once_ = true;
        ++auto_centers_;
        yaw_ref_ = YawReference::Continuous;
        reset_filter_ = true;
    }

    last_.trigger = r.trigger;
    last_.accel_g = r.accel[n - 1];
    last_.gyro_rad_s = r.gyro[n - 1];
    last_.battery_raw = r.battery_raw;
    last_.hid_seq = r.hid_seq;

    InputSample s = make_sample(r.t_rx_ns);
    s.report_dt_s = dt;
    s.pressed = e.pressed;
    s.released = e.released;
    fill_aim(s, dt);
    last_ = s;
    return s;
}

std::optional<InputSample> Pipeline::tick(int64_t now_ns) {
    if (link_ == LinkState::Disconnected || link_ == LinkState::Lost) return std::nullopt;
    int64_t age = now_ns - last_rx_ns_;
    LinkState next = link_;
    if (age > cfg_.release_ns) next = LinkState::Lost;
    else if (age > cfg_.stale_ns) next = LinkState::Stale;
    if (next == link_) return std::nullopt;
    link_ = next;
    ButtonEdges e{};
    if (link_ == LinkState::Lost) {
        e = edges_.release_all();
        reset_filter_ = true;
    }
    InputSample s = make_sample(now_ns);
    s.released = e.released;
    fill_aim(s, 0);
    last_ = s;
    return s;
}

InputSample Pipeline::on_disconnect(int64_t now_ns) {
    ButtonEdges e = edges_.release_all();
    link_ = LinkState::Disconnected;
    if (yaw_ref_ == YawReference::Continuous) yaw_ref_ = YawReference::Interrupted;
    have_rx_ = false;
    last_hid_seq_ = -1;
    reset_filter_ = true;
    InputSample s = make_sample(now_ns);
    s.released = e.released;
    s.trigger = 0;
    fill_aim(s, 0);
    last_ = s;
    return s;
}

bool Pipeline::recenter() {
    if (!fusion_.initialized() || link_ != LinkState::Fresh) return false;
    aim_.recenter(fusion_.q(), CenterSource::User);
    yaw_ref_ = YawReference::Continuous;
    reset_filter_ = true;
    ++user_centers_;
    // Nieuw sample met het nieuwe midden, zodat afnemers het direct zien.
    InputSample n = make_sample(last_rx_ns_);
    fill_aim(n, 0);
    last_ = n;
    return true;
}

void Pipeline::set_filter(const OneEuroConfig& c) {
    cfg_.filter = c;
    fx_.set_config(c);
    fy_.set_config(c);
    reset_filter_ = true;
}

void Pipeline::set_fusion_mode(FusionMode m) {
    if (m == cfg_.fusion_mode) return;
    cfg_.fusion_mode = m;
    // Inschakelen: de huidige stand wordt de referentie (de drift uit de schaduwperiode wordt niet
    // alsnog teruggedraaid). Uitschakelen: de geleerde verticale bias vervalt via een nieuwe
    // vergrendeling bij een volgende inschakeling; q blijft staan (geen sprong).
    mag_.relock();
}

void Pipeline::set_aim(const AimConfig& c) {
    cfg_.aim = c;
    aim_.set_config(c);
    reset_filter_ = true;
}

}  // namespace ravengun::core
