#include "hid_aux.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"

namespace ravengund {

using namespace ravengun;
using namespace ravengun::core;

namespace {
constexpr int64_t kProgressPeriodNs = 200'000'000;  // 5 Hz
constexpr double kMagTargetCoverage = 0.80;
constexpr int kMagTargetSamples = 300;
constexpr double kMagMinSeconds = 40.0;  // minimaal zo lang draaien, ook als de dekking eerder lijkt te kloppen

std::string fail(const char* code, const std::string& msg) { return std::string("!") + code + "|" + msg; }
}  // namespace

void HidAux::on_connect(const std::string& id, const std::string& model, int64_t now) {
    if (collecting_ && id != id_) { collecting_ = false; last_fit_json_ = "{\"aborted\":\"andere controller\"}"; }
    connected_ = true;
    id_ = id;
    model_ = model;
    last_rx_ = 0;
    ema_init_ = false;
    tap_err_.clear();
    if (model == "ZCM1") {
        if (tap.open_for(id, tap_err_))
            logf("info", "hidraw-tap open (%s): interne stempel en temperatuur beschikbaar", tap.node().c_str());
        else
            logf("waarschuwing", "hidraw-tap niet beschikbaar (%s): ontvangsttijd als tijdbasis", tap_err_.c_str());
    } else {
        tap_err_ = "alleen ZCM1 wordt geparseerd";
    }
    mag_dir_ = default_mag_calibration_dir();
    cal_loaded_ = false;
    if (model != "ZCM1") {
        cal_err_ = "geen magnetometer";
    } else if (load_mag_calibration(mag_dir_, controller_key(id), cal_, cal_err_)) {
        cal_loaded_ = true;
        cal_err_.clear();
        logf("info", "magnetometerkalibratie geladen (%s, houder %s, restfout %.2f %%)", to_string(cal_.kind),
             cal_.holder.c_str(), cal_.rms_norm_err * 100);
    }
    if (pipe_) {
        // Dezelfde kalibratie bij een herverbinding niet opnieuw zetten: dan blijft de vergrendeling van de
        // yaw-referentie staan (de pijplijn doet zelf een gladde terugkeer na de onderbreking).
        if (!cal_loaded_) pipe_->clear_mag_calibration();
        else if (!pipe_->mag().calibrated() || pipe_->mag().calibration().created_unix != cal_.created_unix ||
                 pipe_->mag().calibration().controller_key != cal_.controller_key)
            pipe_->set_mag_calibration(cal_);
    }
    // Bewaarde gyro-bias als beginwaarde (alleen als de pijplijn nog niets gemeten heeft, bv. bij een herverbinding).
    if (pipe_) {
        std::string info;
        bias_store_.set_restore_trim(false);  // yaw-trim begint elke sessie op nul (zie bias_store.hpp)
        bias_store_.on_connect(*pipe_, id, static_cast<double>(now) / 1e9, static_cast<int64_t>(std::time(nullptr)), &info);
        if (!info.empty()) logf(info.find("niet gebruikt") != std::string::npos ? "waarschuwing" : "info", "%s", info.c_str());
    }
    progress_dirty_ = true;
}

void HidAux::on_disconnect(int64_t) {
    if (pipe_ && connected_) {
        std::string info;
        bias_store_.on_disconnect(*pipe_, static_cast<double>(mono_ns()) / 1e9, static_cast<int64_t>(std::time(nullptr)), &info);
        if (!info.empty()) logf(info.find("mislukt") != std::string::npos ? "waarschuwing" : "info", "%s", info.c_str());
    }
    tap.close();
    connected_ = false;
    if (collecting_) {
        collecting_ = false;
        last_fit_json_ = "{\"aborted\":\"verbinding verbroken\"}";
        logf("waarschuwing", "magnetometerkalibratie afgebroken: verbinding verbroken");
    }
    progress_dirty_ = true;
}

void HidAux::on_report(const RawReport& r, const Pipeline& p, double temp_c) {
    if (rec_ && rec_feed_) rec_->push_report(r);
    if (bias_store_.enabled()) {
        std::string info;
        if (bias_store_.on_report(p, static_cast<double>(r.t_rx_ns) / 1e9, static_cast<int64_t>(std::time(nullptr)), &info) && !info.empty())
            logf(info.find("mislukt") != std::string::npos ? "waarschuwing" : "info", "%s", info.c_str());
    }
    temp_c_ = temp_c;
    if (model_ != "ZCM1") return;
    int n = std::clamp(r.subsamples, 1, 2);
    Vec3 accel{}, gyro{};
    for (int i = 0; i < n; ++i) { accel = accel + r.accel[i]; gyro = gyro + r.gyro[i]; }
    accel = accel * (1.0 / n);
    gyro = gyro * (1.0 / n) - p.bias().bias();
    gyro_dps_ += 0.2 * (norm(gyro) * kDegPerRad - gyro_dps_);  // gladgemaakte rotatiesnelheid (graden/s)
    last_rx_ = r.t_rx_ns;
    if (cal_loaded_ && p.mag().calibrated()) {
        // De detector draait in de pijplijn (fase 4); hier alleen de getoonde gemiddelden.
        last_check_ = p.mag().status().last;
        double a = ema_init_ ? 0.1 : 1.0;
        norm_ema_ += a * (last_check_.norm - norm_ema_);
        if (!std::isnan(last_check_.dip_deg)) dip_ema_ += (ema_init_ ? 0.1 : 1.0) * (last_check_.dip_deg - dip_ema_);
        ema_init_ = true;
    }
    if (collecting_) {
        coll_.add(r.mag_raw, accel, static_cast<double>(r.t_rx_ns) / 1e9, norm(gyro) * kDegPerRad);
        if (r.temperature_raw >= 0) collect_temp_ = r.temperature_raw;
        progress_dirty_ = true;
    }
}

void HidAux::periodic(int64_t now) {
    if (rec_ && rec_feed_) {
        std::string why = rec_->check_limits(now);
        if (!why.empty()) {
            logf("info", "opname gestopt: %s", why.c_str());
            progress_dirty_ = true;
        }
        if (rec_->active()) progress_dirty_ = true;
    }
    if (progress_owner_ && progress_dirty_ && now >= next_progress_) {
        sh_.set_progress(progress_json(now));
        next_progress_ = now + kProgressPeriodNs;
        progress_dirty_ = false;
    }
}

void HidAux::on_led_sent(int64_t t, int r, int g, int b) {
    if (r == last_led_[0] && g == last_led_[1] && b == last_led_[2]) return;
    last_led_[0] = r; last_led_[1] = g; last_led_[2] = b;
    if (rec_ && rec_feed_) rec_->push_led(t, r, g, b);
}

std::string HidAux::record(const ipc::Request& rq, bool led_enabled, const int led[3], int64_t now) {
    if (!rec_) return fail("unavailable", "geen recorder");
    if (rq.action == "status") return rec_->status_json(now);
    if (rq.action == "stop") {
        std::string s = rec_->stop("stop", now);
        if (s.empty()) return fail("unavailable", "er loopt geen opname");
        logf("info", "opname gestopt door gebruiker");
        progress_dirty_ = true;
        return s;
    }
    if (rq.action == "mark") {
        if (!rec_->active()) return fail("unavailable", "er loopt geen opname");
        rec_->push_mark(now, rq.label);
        return rec_->status_json(now);
    }
    // start
    rec::Header h;
    h.tag = rq.tag.empty() ? "opname" : rq.tag;
    h.controller = connected_ ? redact_mac(id_) : std::string();
    h.model = connected_ ? model_ : std::string();
    h.led_enabled = led_enabled;
    for (int i = 0; i < 3; ++i) h.led[i] = led[i];
    h.dt_source = move_clock_ ? "move" : "rx";
    uint64_t max_bytes = static_cast<uint64_t>(rq.max_mb.value_or(64) * 1024 * 1024);
    double dur = rq.duration_s.value_or(600);
    std::string err = rec_->start(h, max_bytes, dur, now);
    if (!err.empty()) return fail("unavailable", err);
    // Huidige LED-kleur als eerste gebeurtenis, zodat elk segment een bekende kleur heeft.
    if (led_enabled) {
        rec_->push_led(now, led[0], led[1], led[2]);
        last_led_[0] = led[0]; last_led_[1] = led[1]; last_led_[2] = led[2];
    }
    logf("info", "opname gestart (tag %s, max %.0f MiB, max %.0f s)", h.tag.c_str(), static_cast<double>(max_bytes) / 1048576.0, dur);
    progress_dirty_ = true;
    return rec_->status_json(now);
}

std::string HidAux::calib_mag(const ipc::Request& rq, int64_t now) {
    if (rq.action == "status") return mag_json(now);
    if (rq.action == "abort") {
        if (!collecting_) return fail("unavailable", "er loopt geen magnetometerkalibratie");
        collecting_ = false;
        last_fit_json_ = "{\"aborted\":\"door gebruiker\"}";
        progress_dirty_ = true;
        return "{\"aborted\":true}";
    }
    if (rq.action == "start") {
        if (!connected_ || model_ != "ZCM1") return fail("unavailable", "geen ZCM1 (met magnetometer) verbonden");
        coll_.reset();
        collecting_ = true;
        holder_ = rq.holder.empty() ? "onbekend" : rq.holder;
        collect_start_ns_ = now;
        collect_temp_ = -1;
        last_fit_json_.clear();
        progress_dirty_ = true;
        logf("info", "magnetometerkalibratie gestart (houder %s)", holder_.c_str());
        return progress_json(now);
    }
    // stop: fitten, en alleen bij voldoende kwaliteit opslaan en activeren.
    if (!collecting_) return fail("unavailable", "er loopt geen magnetometerkalibratie");
    collecting_ = false;
    MagFitResult res = fit_magnetometer(coll_.samples());
    MagCalibration c = res.cal;
    c.controller = redact_mac(id_);
    c.controller_key = controller_key(id_);
    c.holder = holder_;
    c.created_unix = static_cast<int64_t>(std::time(nullptr));
    c.temperature_raw = collect_temp_;
    bool saved = false;
    std::string save_err;
    if (res.ok || res.marginal) {
        saved = save_mag_calibration(mag_dir_, c, save_err);
        if (saved) {
            cal_ = c;
            cal_loaded_ = true;
            cal_err_.clear();
            if (pipe_) pipe_->set_mag_calibration(cal_);
            ema_init_ = false;
        }
    }
    json::Writer w;
    w.begin_object();
    w.key("fitted").value(res.fitted);
    w.key("ok").value(res.ok);
    w.key("marginal").value(res.marginal);
    w.key("saved").value(saved);
    if (!save_err.empty()) w.key("save_error").value(save_err);
    if (saved) w.key("file").value(mag_dir_ + "/" + c.controller_key + ".json");
    w.key("problems").begin_array();
    for (const auto& p : res.problems) w.value(p);
    w.end_array();
    w.key("kind").value(to_string(c.kind));
    w.key("samples").value(c.samples);
    w.key("samples_seen").value(coll_.seen());
    w.key("duration_s").value(static_cast<double>(now - collect_start_ns_) / 1e9);
    w.key("coverage").value(c.coverage);
    w.key("rms_norm_err").value(c.rms_norm_err);
    w.key("max_norm_err").value(res.max_norm_err);
    w.key("field_counts").value(c.field_counts);
    w.key("dip_mean_deg").value(res.dip_mean_deg);
    w.key("dip_std_deg").value(c.dip_std_deg);
    w.key("dip_std_noflip_deg").value(res.dip_std_noflip_deg);
    w.key("dip_samples").value(res.dip_samples);
    w.key("offset").begin_array().value(c.offset.x).value(c.offset.y).value(c.offset.z).end_array();
    w.key("holder").value(c.holder);
    w.end_object();
    last_fit_json_ = w.str();
    progress_dirty_ = true;
    logf("info", "magnetometerkalibratie: %s (%d samples, dekking %.0f %%, restfout %.2f %%, dip-spreiding %.2f graden)%s",
         res.ok ? "geslaagd" : "NIET geslaagd", c.samples, c.coverage * 100, c.rms_norm_err * 100, c.dip_std_deg,
         saved ? ", opgeslagen" : "");
    return last_fit_json_;
}

std::string HidAux::progress_json(int64_t now) const {
    json::Writer w;
    w.begin_object();
    w.key("recording");
    if (rec_) w.raw(rec_->status_json(now)); else w.null();
    w.key("mag_calibration").begin_object();
    w.key("active").value(collecting_);
    if (collecting_) {
        MagCoverageDetail cd = coll_.detail();
        double cov = cd.coverage;
        int n = static_cast<int>(coll_.samples().size());
        w.key("holder").value(holder_);
        w.key("samples").value(n);
        w.key("samples_seen").value(coll_.seen());
        w.key("coverage_pct").value(cov * 100);
        w.key("elapsed_s").value(static_cast<double>(now - collect_start_ns_) / 1e9);
        w.key("target_coverage_pct").value(kMagTargetCoverage * 100);
        w.key("target_samples").value(kMagTargetSamples);
        double el = static_cast<double>(now - collect_start_ns_) / 1e9;
        w.key("min_seconds").value(kMagMinSeconds);
        w.key("ready").value(cov >= kMagTargetCoverage && n >= kMagTargetSamples && el >= kMagMinSeconds);
        // Dekking per richting (20 vlakken, SphereCoverage::directions()) voor de kalibratie-UI op de tv:
        // een vlak telt mee vanaf cell_min_count samples. 'dir' is de laatste meetrichting.
        w.key("cell_min_count").value(3);
        w.key("cells").begin_array();
        for (int c : cd.counts) w.value(c);
        w.end_array();
        w.key("spread").value(cd.spread);
        if (cd.has_dir) w.key("dir").begin_array().value(cd.dir.x).value(cd.dir.y).value(cd.dir.z).end_array();
        w.key("gyro_dps").value(gyro_dps_);
    }
    if (!last_fit_json_.empty()) w.key("last_result").raw(last_fit_json_);
    w.end_object();
    w.end_object();
    return w.str();
}

std::string HidAux::mag_json(int64_t now) const {
    json::Writer w;
    w.begin_object();
    w.key("present").value(model_ == "ZCM1");
    const bool fused = pipe_ && (pipe_->fusion_mode() == FusionMode::MahonyMag || pipe_->fusion_mode() == FusionMode::VqfMag) && pipe_->mag().calibrated();
    w.key("used_in_fusion").value(fused);
    w.key("calibrated").value(cal_loaded_);
    if (!cal_loaded_) w.key("calibration_error").value(cal_err_);
    if (cal_loaded_) {
        w.key("calibration").begin_object();
        w.key("kind").value(to_string(cal_.kind));
        w.key("holder").value(cal_.holder);
        w.key("created_unix").value(static_cast<long long>(cal_.created_unix));
        w.key("field_counts").value(cal_.field_counts);
        w.key("dip_ref_deg").value(cal_.dip_ref_deg);
        w.key("rms_norm_err").value(cal_.rms_norm_err);
        w.key("dip_std_deg").value(cal_.dip_std_deg);
        w.key("coverage").value(cal_.coverage);
        w.key("samples").value(cal_.samples);
        w.key("y_flip").value(cal_.y_flip);
        w.key("b_map").value(cal_.negate ? "diag(-1,1,-1)" : "diag(1,-1,1)");
        w.end_object();
        w.key("live").begin_object();
        w.key("norm").value(last_check_.norm);
        w.key("norm_ema").value(norm_ema_);
        if (std::isnan(last_check_.dip_deg)) w.key("dip_deg").null(); else w.key("dip_deg").value(last_check_.dip_deg);
        w.key("dip_ema_deg").value(dip_ema_);
        if (std::isnan(last_check_.rate_err_deg_s)) w.key("rate_err_deg_s").null(); else w.key("rate_err_deg_s").value(last_check_.rate_err_deg_s);
        w.key("accepted").value(last_check_.accepted);
        w.key("reasons").value(reject_names(last_check_.reasons));
        static const MagDisturbanceDetector kNone;
        const MagDisturbanceDetector& det = pipe_ ? pipe_->mag().detector() : kNone;
        w.key("state").value(to_string(det.state()));
        w.end_object();
        w.key("detector").begin_object();
        w.key("samples").value(static_cast<unsigned long long>(det.total()));
        w.key("accepted").value(static_cast<unsigned long long>(det.accepted()));
        w.key("rejected_norm").value(static_cast<unsigned long long>(det.rejected(kRejectNorm)));
        w.key("rejected_dip").value(static_cast<unsigned long long>(det.rejected(kRejectDip)));
        w.key("rejected_rate").value(static_cast<unsigned long long>(det.rejected(kRejectRate)));
        w.key("disturbed_episodes").value(static_cast<unsigned long long>(det.disturbed_episodes()));
        w.key("thresholds").value("norm 5 %, dip 2 graden (beide na 0,15 s glad), gyro-consistentie 2 graden/s (helling over 1,5 s); verstoord na 1 s, herstel na 2 s");
        w.end_object();
        if (pipe_) {
            const MagYawStatus& ms = pipe_->mag().status();
            w.key("yaw_correction").begin_object();
            w.key("mode").value(to_string(pipe_->fusion_mode()));
            w.key("applied").value(fused);
            w.key("locked").value(ms.locked);
            w.key("reacquire").value(ms.reacquire);
            w.key("pending_deg").value(ms.pending_deg);
            w.key("sigma_deg").value(ms.sigma_deg);
            w.key("drift_deg_s").value(ms.drift_dps);
            w.key("vertical_bias_deg_s").value(ms.vertical_bias_dps);
            w.key("rate_deg_s").value(ms.rate_dps);
            w.key("rate_limit_deg_s").value(ms.rate_limit_dps);
            w.key("total_deg").value(ms.total_deg);
            w.key("max_step_deg").value(ms.max_step_deg);
            w.key("unusable_s").value(ms.unusable_s);
            w.key("samples_used").value(static_cast<unsigned long long>(ms.used_samples));
            w.key("innovation_rejected").value(static_cast<unsigned long long>(ms.innov_rejected));
            w.key("accel_skipped").value(static_cast<unsigned long long>(ms.accel_skipped));
            w.key("fast_skipped").value(static_cast<unsigned long long>(ms.fast_skipped));
            w.key("meaning").value("pending = openstaande yaw-fout volgens de magnetometer; de hoek wordt hooguit met rate_limit gecorrigeerd "
                                   "(0,02 graden/s stil, 2 % van de draaisnelheid in beweging, max 0,3); de drift via de verticale bias");
            w.end_object();
        }
    }
    w.key("session").raw(progress_json(now));
    w.end_object();
    return w.str();
}

std::string HidAux::status_blocks(const Pipeline& p, int64_t now) const {
    json::Writer w;
    w.begin_object();
    const MoveClock& c = p.clock();
    const ResidualStats& rs = p.clock_residuals();
    w.key("timebase").begin_object();
    w.key("integration_dt").value(move_clock_ ? "move_stamp (terugval: ontvangsttijd)" : "receive_time");
    w.key("move_ticks_per_s").value(kMoveTicksPerSecond);
    w.key("stamp_wrap_s").value(kMoveStampModulo / kMoveTicksPerSecond);
    w.key("stamps").value(static_cast<unsigned long long>(c.samples()));
    w.key("coupled").value(c.coupled());
    w.key("skew_ppm").value(c.skew_ppm());
    w.key("wrap_corrections").value(static_cast<unsigned long long>(c.wrap_corrections()));
    w.key("residual_ms").begin_object();
    w.key("p50").value(rs.percentile(0.5)).key("p95").value(rs.percentile(0.95));
    w.key("p99").value(rs.percentile(0.99)).key("max").value(rs.max());
    w.key("meaning").value("t_rx - gekoppelde stempeltijd (extra transportvertraging boven het minimum)");
    w.end_object();
    w.key("dt_from_move_stamp").value(static_cast<unsigned long long>(p.dt_from_move_clock()));
    w.key("dt_from_receive_time").value(static_cast<unsigned long long>(p.dt_from_rx()));
    w.key("temperature_raw").value(p.last_temperature_raw());
    if (std::isnan(temp_c_)) w.key("temperature_c").null(); else w.key("temperature_c").value(temp_c_);
    w.end_object();
    w.key("hidraw").begin_object();
    w.key("open").value(tap.is_open());
    w.key("node").value(tap.node());
    if (!tap_err_.empty()) w.key("error").value(tap_err_);
    w.key("reads").value(static_cast<unsigned long long>(tap.reads));
    w.key("matched").value(static_cast<unsigned long long>(tap.matched));
    w.key("unmatched").value(static_cast<unsigned long long>(tap.unmatched));
    w.key("discarded").value(static_cast<unsigned long long>(tap.discarded));
    w.key("parse_errors").value(static_cast<unsigned long long>(tap.parse_errors));
    w.key("overflow").value(static_cast<unsigned long long>(tap.overflow));
    w.end_object();
    w.key("gyro_bias").begin_object();
    w.key("mode").value(to_string(p.config().bias.mode));
    w.key("state").value(to_string(p.bias().state()));
    w.key("source").value(to_string(p.bias().source()));
    {
        Vec3 b = p.bias().bias();
        w.key("bias_deg_s").begin_array().value(b.x * kDegPerRad).value(b.y * kDegPerRad).value(b.z * kDegPerRad).end_array();
        Vec3 sl = p.bias().temp_slope();
        w.key("temp_slope_deg_s_per_count").begin_array().value(sl.x * kDegPerRad).value(sl.y * kDegPerRad).value(sl.z * kDegPerRad).end_array();
    }
    w.key("still_s").value(p.bias().still_time_total());
    w.key("quasi_s").value(p.bias().quasi_time_total());
    w.key("quasi_windows_used").value(static_cast<unsigned long long>(p.bias().quasi_accepted()));
    w.key("quasi_windows_rejected").value(static_cast<unsigned long long>(p.bias().quasi_rejected()));
    w.key("quasi_enabled").value(!p.mag_yaw_active());
    w.key("optical_enabled").value(p.config().bias.optical && p.config().bias.mode == BiasMode::New);
    w.key("optical_s").value(p.bias().optical_time_total());
    w.key("optical_windows_used").value(static_cast<unsigned long long>(p.bias().optical_accepted()));
    w.key("optical_windows_rejected").value(static_cast<unsigned long long>(p.bias().optical_rejected()));
    w.key("optical_still_windows").value(static_cast<unsigned long long>(p.bias().optical_still_windows()));
    w.key("temperature_raw").value(p.last_temperature_raw());
    w.key("store").begin_object();
    w.key("enabled").value(bias_store_.enabled());
    w.key("dir").value(bias_store_.dir());
    w.key("loaded").value(bias_store_.loaded());
    if (bias_store_.loaded()) {
        const SavedBias& lv = bias_store_.loaded_value();
        w.key("loaded_deg_s").begin_array().value(lv.bias.x * kDegPerRad).value(lv.bias.y * kDegPerRad).value(lv.bias.z * kDegPerRad).end_array();
        w.key("loaded_tc").value(lv.tc);
        w.key("loaded_unix_s").value(static_cast<long long>(lv.unix_s));
    }
    w.key("saves").value(static_cast<unsigned long long>(bias_store_.saves()));
    w.key("last_saved_unix_s").value(static_cast<long long>(bias_store_.last_saved_unix()));
    if (!bias_store_.error().empty()) w.key("error").value(bias_store_.error());
    w.end_object();
    w.end_object();
    w.key("yaw_trim").begin_object();
    w.key("learning").value(p.config().trim.enabled && !p.mag_yaw_active());
    w.key("trim_deg_s").value(p.trim().rate_dps());
    w.key("presses").value(static_cast<unsigned long long>(p.trim().presses()));
    w.key("learned").value(static_cast<unsigned long long>(p.trim().learned()));
    w.key("skipped").value(static_cast<unsigned long long>(p.trim().skipped()));
    w.key("limit_deg_s").value(p.config().trim.limit_dps);
    w.end_object();
    w.key("magnetometer").raw(mag_json(now));
    w.key("recording");
    if (rec_) w.raw(rec_->status_json(now)); else w.null();
    w.end_object();
    std::string s = w.str();
    // Zonder de buitenste accolades, met een komma ervoor, om in een bestaand object te plakken.
    return "," + s.substr(1, s.size() - 2);
}

}  // namespace ravengund
