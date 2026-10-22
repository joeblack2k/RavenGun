#include "move_slot.hpp"

#include <cmath>
#include <unistd.h>

#include "output_service.hpp"
#include "ravengun/games/games.hpp"
#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"

namespace ravengund {

namespace {
// Zo lang de PS-knop vasthouden stopt de game (een korte druk blijft centreren).
constexpr int64_t kPsHoldStopNs = 3'000'000'000;
}  // namespace

using namespace ravengun;
using namespace ravengun::core;

void apply_settings_to_options(const config::Settings& eff, const std::set<std::string>& keep, int player, HidOptions& o) {
    auto use = [&](const char* k) { return keep.count(k) == 0; };
    if (use("filter.enabled")) o.pipeline.filter.enabled = eff.filter.enabled;
    if (use("filter.min_cutoff_hz")) o.pipeline.filter.min_cutoff_hz = eff.filter.min_cutoff_hz;
    if (use("filter.beta")) o.pipeline.filter.beta = eff.filter.beta;
    if (use("filter.d_cutoff_hz")) o.pipeline.filter.d_cutoff_hz = eff.filter.d_cutoff_hz;
    if (use("aim.h_edge_deg")) o.pipeline.aim.h_edge_deg = eff.aim.h_edge_deg;
    if (use("aim.v_edge_deg")) o.pipeline.aim.v_edge_deg = eff.aim.v_edge_deg;
    if (use("aim.predict_ms")) o.pipeline.predict.horizon_ms = eff.aim.predict_ms;
    if (use("fusion.mode")) parse_fusion_mode(eff.fusion.mode.c_str(), o.pipeline.fusion_mode);
    if (use("fusion.vqf_bias")) parse_vqf_bias(eff.fusion.vqf_bias.c_str(), o.pipeline.vqf.bias);
    if (use("fusion.recenter_learning")) o.pipeline.trim.enabled = eff.fusion.recenter_learning;
    o.rumble = eff.rumble;
    parse_ext_mode(eff.sharp_shooter.extension.c_str(), o.pipeline.ext_mode);
    o.pipeline.pump.on = eff.gun_buttons.trigger_on;
    o.pipeline.pump.off = eff.gun_buttons.trigger_off;
    o.gun_buttons = output::build_live_map(eff.gun_buttons);
    // LED: de gekozen kleur; zonder keuze houdt speler 1 de standaard van de daemon (cyaan met camera) en krijgt speler 2..4
    // zijn spelerkleur, zodat twee bollen nooit dezelfde kleur hebben.
    if (use("led.r")) {
        if (eff.led.enabled) {
            o.led_enabled = true;
            o.led_r = static_cast<uint8_t>(eff.led.r); o.led_g = static_cast<uint8_t>(eff.led.g); o.led_b = static_cast<uint8_t>(eff.led.b);
        } else if (player >= 2) {
            config::LedSettings l = config::player_led(player);
            o.led_enabled = true;
            o.led_r = static_cast<uint8_t>(l.r); o.led_g = static_cast<uint8_t>(l.g); o.led_b = static_cast<uint8_t>(l.b);
        }
    }
}

MoveSlot::MoveSlot(int player, SharedState& sh, const HidOptions& base)
    : player_(player), sh_(sh), opt_(base), pipe_(base.pipeline), aux_(sh, base.recorder, base.pipeline.use_move_clock),
      live_(base.gun_buttons) {
    aux_.attach(&pipe_);
    aux_.set_bias_dir(opt_.bias_dir);
    aux_.set_recording_owner(player == 1);
    aux_.set_progress_owner(player == 1);
    rumble_.set_config(opt_.rumble);
    publish_info();
}

void MoveSlot::configure(const std::string& address, const config::Settings& eff, const std::set<std::string>& cli_keys) {
    HidOptions o = opt_;
    apply_settings_to_options(eff, cli_keys, player_, o);
    // Per-game knoppen ([Controls]) gelden ook voor een Move die tijdens de game wordt toegewezen.
    if (const auto ov = sh_.game_controls(player_); !ov.empty()) o.gun_buttons = output::build_live_map(config::with_gun_overrides(eff.gun_buttons, ov));
    assigned_ = address;
    const auto& f = o.pipeline.filter;
    const auto& cf = pipe_.config().filter;
    if (f.enabled != cf.enabled || f.min_cutoff_hz != cf.min_cutoff_hz || f.beta != cf.beta || f.d_cutoff_hz != cf.d_cutoff_hz) set_filter(f);
    const auto& ca = pipe_.config().aim;
    if (o.pipeline.aim.h_edge_deg != ca.h_edge_deg || o.pipeline.aim.v_edge_deg != ca.v_edge_deg) set_aim(o.pipeline.aim);
    if (o.pipeline.predict.horizon_ms != pipe_.config().predict.horizon_ms) {
        PredictConfig pc = pipe_.config().predict;
        pc.horizon_ms = o.pipeline.predict.horizon_ms;
        set_predict(pc);
    }
    if (o.pipeline.fusion_mode != pipe_.fusion_mode()) set_fusion(o.pipeline.fusion_mode);
    if (o.pipeline.trim.enabled != pipe_.trim().config().enabled) set_recenter_learning(o.pipeline.trim.enabled);
    if (o.led_enabled != opt_.led_enabled || o.led_r != opt_.led_r || o.led_g != opt_.led_g || o.led_b != opt_.led_b)
        set_led(o.led_enabled, o.led_r, o.led_g, o.led_b);
    set_rumble(o.rumble);
    opt_.gun_buttons = o.gun_buttons;
    live_.set_map(o.gun_buttons);
    if (sh_.output) sh_.output->set_layout(player_, o.gun_buttons.layout);
}

void MoveSlot::publish_info() {
    SlotInfo si;
    si.connected = ci_.open;
    si.address = ci_.open ? ci_.id : std::string();
    si.model = ci_.open ? ci_.model : std::string();
    si.battery_raw = ci_.open ? pipe_.last().battery_raw : -1;
    si.connected_at_ns = ci_.connected_at_ns;
    si.led_enabled = opt_.led_enabled;
    si.led[0] = opt_.led_r; si.led[1] = opt_.led_g; si.led[2] = opt_.led_b;
    last_battery_ = si.battery_raw;
    sh_.set_slot_info(player_, si);
}

void MoveSlot::rumble_step(int64_t now) {
    if (now - armed_checked_ >= 20'000'000 || armed_checked_ == 0) {
        rin_.armed = sh_.output && sh_.output->armed();
        armed_checked_ = now;
    }
    // Unity route and [Input] Rumble = game (game-out file: game_recoil counts the solenoid pulses, game_rumble_hold is the motor).
    // Unity-route: de game bepaalt wanneer er geschoten wordt (Recoil-vlag van de plugin). De trekker-gedreven rumble gaat dan
    // uit, zodat een lege trekker niet trilt en schoten niet dubbel trillen; elk schot geeft een puls van pulse_ms.
    if ((sh_.game_rumble_mask.load(std::memory_order_relaxed) >> (player_ - 1)) & 1u) {
        const uint64_t rc = sh_.game_recoil[static_cast<size_t>(player_ - 1)].load(std::memory_order_relaxed);
        if (rc != recoil_seen_) {
            recoil_seen_ = rc;
            if (rin_.connected && rin_.armed) rumble_.test(now, opt_.rumble.pulse_ms);
        }
        output::RumbleInput gated = rin_;
        gated.trigger = false;
        gated.pump = false;
        gated.hold = sh_.game_rumble_hold[static_cast<size_t>(player_ - 1)].load(std::memory_order_relaxed) / 255.0;
        rumble_.update(now, gated);
        return;
    }
    recoil_seen_ = sh_.game_recoil[static_cast<size_t>(player_ - 1)].load(std::memory_order_relaxed);
    rumble_.update(now, rin_);
}

void MoveSlot::emit(const InputSample& s) {
    sh_.publish(player_, s);
    if (!sh_.output) return;
    output::LiveAction a = live_.update(s, nav_);
    if (a.recenter) {  // PS-knop altijd (ook ongewapend), anders een knop uit de indeling
        if (pipe_.recenter(true)) {  // van de controller: de speler richt op het midden, telt mee voor de yaw-trim
            sh_.publish(player_, pipe_.last());
            logf("info", a.recenter_ps ? "speler %d: recenter met de PS-knop" : "speler %d: recenter met een knop uit de indeling", player_);
            a = live_.update(pipe_.last(), nav_);
            a.recenter = false;
        }
    }
    sh_.output->feed_live(player_, a);
    ps_hold_step(s);
    rin_.connected = s.link != LinkState::Lost && s.link != LinkState::Disconnected;
    rin_.trigger = rin_.connected && live_.trigger_down();
    rin_.pump = rin_.connected && (s.buttons & kBtnPump) != 0;
    rumble_step(mono_ns());
}

bool MoveSlot::ps_hold_elapsed(bool held, int64_t now) {
    if (!held) {              // losgelaten: opnieuw beginnen bij de volgende druk
        ps_down_since_ = 0;
        ps_hold_fired_ = false;
        return false;
    }
    if (ps_down_since_ == 0) ps_down_since_ = now;
    if (ps_hold_fired_ || now - ps_down_since_ < kPsHoldStopNs) return false;
    ps_hold_fired_ = true;    // precies één keer per keer vasthouden
    return true;
}

void MoveSlot::ps_hold_step(const InputSample& s) {
    const bool held = (s.buttons & kBtnPS) != 0 && s.link != LinkState::Lost && s.link != LinkState::Disconnected;
    if (!ps_hold_elapsed(held, mono_ns()) || !sh_.games) return;
    // stop() start het stoppen op een eigen thread; hier alleen de korte controle of er iets draait.
    const games::Result r = sh_.games->stop();
    logf("info", "speler %d: PS-knop 3 s vastgehouden: game stoppen (%s)", player_, r.ok ? (r.message.empty() ? "gestart" : r.message.c_str()) : r.message.c_str());
}

void MoveSlot::connect(const ControllerInfo& ci, int64_t now) {
    const uint64_t connects = ci_.connects + 1, disconnects = ci_.disconnects;  // tellers blijven per speler doorlopen
    ci_ = ci;
    ci_.open = true;
    ci_.connected_at_ns = now;
    ci_.connects = connects;
    ci_.disconnects = disconnects;
    next_led_ = 0;
    rumble_.reset();
    gate_.reset();
    rumble_sent_ = 0;
    nav_ = 0;
    live_.reset();
    pipe_.on_connect(ci_.id, now);
    aux_.on_connect(ci_.id, ci_.model, now);
    sh_.publish(player_, pipe_.last());
    publish_info();
    logf("info", "speler %d: Move verbonden: %s, model %s, kalibratie %s", player_, redact_mac(ci_.id).c_str(), ci_.model.c_str(),
         ci_.has_calibration ? "ja" : "NEE (waarden ongekalibreerd)");
}

void MoveSlot::on_report(const RawReport& r, double temp_c, int64_t) {
    {   // nieuwe cameraframes eerst: de bol-stilstand is bewijs voor de gyro-bias
        OpticalSample o;
        if (sh_.optical_if_new(player_, optical_seen_, o)) pipe_.on_optical(o);
    }
    InputSample s = pipe_.on_report(r);
    emit(s);
    ps_.add(static_cast<double>(mono_ns() - r.t_rx_ns) / 1e3);
    aux_.on_report(r, pipe_, temp_c);
    if (s.battery_raw != last_battery_) publish_info();
}

bool MoveSlot::tick(int64_t now) {
    if (!ci_.open) return false;
    rumble_step(now);
    if (auto s = pipe_.tick(now)) {
        emit(*s);
        if (s->link == LinkState::Lost)
            logf("waarschuwing", "speler %d: geen rapporten sinds %.0f ms: knoppen vrijgegeven", player_, static_cast<double>(opt_.pipeline.release_ns) / 1e6);
    }
    const InputSample& last = pipe_.last();
    const int64_t last_rx = last.t_rx_ns ? last.t_rx_ns : ci_.connected_at_ns;
    if (now - last_rx > opt_.disconnect_ns) {
        logf("waarschuwing", "speler %d: Move %s: %.1f s geen rapporten, verbinding gesloten; opnieuw zoeken", player_,
             redact_mac(ci_.id).c_str(), static_cast<double>(now - last_rx) / 1e9);
        return true;
    }
    return false;
}

void MoveSlot::disconnect(int64_t now) {
    if (!ci_.open) return;
    emit(pipe_.on_disconnect(now));  // alle knoppen vrij (ook op de virtuele gun)
    rin_.connected = false;
    rumble_step(now);
    aux_.on_disconnect(now);
    ci_.open = false;
    ++ci_.disconnects;
    nav_ = 0;
    blink_until_ = 0;
    publish_info();
}

void MoveSlot::set_nav(uint32_t buttons, int64_t now) {
    if (buttons == nav_) return;
    nav_ = buttons;
    // Meteen doorgeven (niet wachten op het volgende Move-rapport), met de laatste toestand van de Move.
    if (ci_.open && sh_.output) {
        output::LiveAction a = live_.update(pipe_.last(), nav_);
        if (a.recenter && pipe_.recenter(true)) {
            sh_.publish(player_, pipe_.last());
            logf("info", "speler %d: recenter met de Navigation Controller", player_);
            a = live_.update(pipe_.last(), nav_);
        }
        a.move = false;  // de positie komt alleen uit Move-rapporten
        sh_.output->feed_live(player_, a);
        rin_.trigger = live_.trigger_down();
        rumble_step(now);
    }
}

void MoveSlot::set_led(bool en, uint8_t r, uint8_t g, uint8_t b) {
    opt_.led_enabled = en; opt_.led_r = r; opt_.led_g = g; opt_.led_b = b;
    led_dirty_ = true;
    publish_info();
}

void MoveSlot::blink(int64_t now, int ms) {
    blink_start_ = now;
    blink_until_ = now + static_cast<int64_t>(ms) * 1'000'000;
    if (ci_.open) rumble_.test(now, 150);
}

std::optional<MoveOutputReport> MoveSlot::due_output(int64_t now) {
    if (!ci_.open) return std::nullopt;
    if (led_dirty_) { next_led_ = 0; led_dirty_ = false; }
    MoveOutputReport rep;
    rep.rumble = rumble_.value();
    if (blink_until_ && now < blink_until_) {
        // Knipperen: wit en uit om de 250 ms; elke wissel gaat meteen weg.
        const bool on = ((now - blink_start_) / 250'000'000) % 2 == 0;
        const uint8_t v = on ? 255 : 0;
        rep.r = rep.g = rep.b = v;
        rep.led_due = v != blink_last_ || now >= next_led_;
        if (!gate_.due(now, rep.rumble, rep.led_due)) return std::nullopt;
        blink_last_ = v;
        return rep;
    }
    if (blink_until_) { blink_until_ = 0; next_led_ = 0; }  // klaar: de eigen kleur meteen terug
    rep.led_due = opt_.led_enabled && now >= next_led_;
    if (!gate_.due(now, rep.rumble, rep.led_due)) return std::nullopt;
    if (opt_.led_enabled) { rep.r = opt_.led_r; rep.g = opt_.led_g; rep.b = opt_.led_b; }
    return rep;
}

void MoveSlot::output_sent(int64_t now, const MoveOutputReport& rep, bool ok, bool ignored) {
    const bool changed = rep.rumble != gate_.last_sent();
    gate_.sent(now, rep.rumble, ok);
    if (ignored) {
    } else if (!ok) { ++led_failures_; if (changed) ++rumble_failures_; }
    else {
        rumble_sent_ = rep.rumble;
        if (changed) ++rumble_reports_;
        if (opt_.led_enabled && !blink_until_) { ++led_updates_; aux_.on_led_sent(now, opt_.led_r, opt_.led_g, opt_.led_b); }
    }
    if (rep.led_due) next_led_ = now + static_cast<int64_t>(opt_.led_refresh_ms) * 1'000'000;
}

std::optional<MoveOutputReport> MoveSlot::final_output() const {
    if (!ci_.open) return std::nullopt;
    if (!opt_.led_enabled && gate_.last_sent() == 0) return std::nullopt;
    return MoveOutputReport{};  // alles 0: LED uit, rumble 0
}

std::string MoveSlot::record(const ipc::Request& r) {
    int led[3] = {opt_.led_r, opt_.led_g, opt_.led_b};
    return aux_.record(r, opt_.led_enabled, led, mono_ns());
}

bool MoveSlot::recenter() {
    bool ok = pipe_.recenter();
    if (ok) { sh_.publish(player_, pipe_.last()); logf("info", "speler %d: recenter door gebruiker", player_); }
    return ok;
}

bool MoveSlot::clear_trim() {
    pipe_.clear_trim();
    aux_.note_trim_cleared();
    logf("info", "speler %d: yaw-trim gewist door gebruiker", player_);
    return true;
}

std::string MoveSlot::rumble_test(int ms) {
    if (!opt_.rumble.enabled) return "!unavailable|rumble staat uit (ravengunctl rumble on)";
    if (!ci_.open) return "!unavailable|geen Move verbonden";
    return rumble_.test(mono_ns(), ms) ? "1" : "!unavailable|rumble staat uit";
}

void MoveSlot::set_fusion(FusionMode m) {
    if (m == pipe_.fusion_mode()) return;
    pipe_.set_fusion_mode(m);
    opt_.pipeline.fusion_mode = m;
    logf("info", "speler %d: oriëntatieschatter: %s", player_, to_string(m));
}

void MoveSlot::set_recenter_learning(bool on) {
    pipe_.set_trim_learning(on);
    opt_.pipeline.trim.enabled = on;
    logf("info", "speler %d: leren van centreren: %s", player_, on ? "aan" : "uit");
}

void MoveSlot::set_gun_buttons(const output::LiveButtonMap& m) {
    opt_.gun_buttons = m;
    live_.set_map(m);
    if (sh_.output) sh_.output->set_layout(player_, m.layout);
    logf("info", "speler %d: knoppenindeling gewijzigd: %s", player_, output::describe_layout(m).c_str());
}

std::string MoveSlot::status_json(bool full_ids, int64_t start_ns) {
    const Pipeline& p = pipe_;
    const ControllerInfo& ci = ci_;
    const HidOptions& opt = opt_;
    int64_t now = mono_ns();
    const ReportStats& st = p.stats();
    InputSample s = p.last();
    s.optical = sh_.get(player_).sample.optical;  // laatste optische meting (uit de tracker-thread)
    json::Writer w;
    w.begin_object();
    w.key("player").value(player_);
    w.key("daemon").begin_object();
    w.key("pid").value(static_cast<long long>(getpid()));
    w.key("uptime_s").value(static_cast<double>(now - start_ns) / 1e9);
    w.key("hid_enabled").value(opt.enabled);
    w.key("input_schema_version").value(kInputSchemaVersion);
    w.end_object();

    w.key("controller").begin_object();
    w.key("connected").value(ci.open);
    w.key("id").value(full_ids ? ci.id : redact_mac(ci.id));
    w.key("model").value(ci.model);
    w.key("connection").value(ci.connection);
    w.key("has_calibration").value(ci.has_calibration);
    w.key("magnetometer").value(ci.model == "ZCM1" ? ((p.fusion_mode() == FusionMode::MahonyMag || p.fusion_mode() == FusionMode::VqfMag) && p.mag().calibrated() ? "aanwezig, in de fusie (yaw-correctie, zie magnetometer)" : "aanwezig, niet in de fusie gebruikt (zie magnetometer)") : "afwezig of onbekend");
    w.key("connected_s").value(ci.open ? static_cast<double>(now - ci.connected_at_ns) / 1e9 : 0.0);
    w.key("connects").value(static_cast<unsigned long long>(ci.connects));
    w.key("led").begin_object();
    w.key("enabled").value(opt.led_enabled);
    w.key("rgb").begin_array().value(static_cast<int>(opt.led_r)).value(static_cast<int>(opt.led_g)).value(static_cast<int>(opt.led_b)).end_array();
    w.key("refresh_ms").value(opt.led_refresh_ms);
    w.key("updates").value(static_cast<unsigned long long>(led_updates_));
    w.key("failures").value(static_cast<unsigned long long>(led_failures_));
    w.key("blinking").value(blink_until_ != 0 && now < blink_until_);
    w.end_object();
    w.key("rumble").begin_object();
    w.key("enabled").value(opt.rumble.enabled).key("strength").value(opt.rumble.strength).key("pulse_ms").value(opt.rumble.pulse_ms);
    w.key("repeat_hz").value(opt.rumble.repeat_hz).key("pump_strength").value(opt.rumble.pump_strength);
    w.key("current").value(static_cast<int>(rumble_sent_));
    w.key("pulses").value(static_cast<unsigned long long>(rumble_.pulses()));
    w.key("reports").value(static_cast<unsigned long long>(rumble_reports_));
    w.key("failures").value(static_cast<unsigned long long>(rumble_failures_));
    w.end_object();
    w.key("disconnects").value(static_cast<unsigned long long>(ci.disconnects));
    BatteryInfo b = battery_info(s.battery_raw);
    w.key("battery").begin_object();
    if (s.battery_raw >= 0) w.key("raw").value(s.battery_raw); else w.key("raw").null();
    w.key("code").value(b.code).key("text").value(b.text).key("charging").value(b.charging);
    w.key("resolution").value("6 niveaus (<20, >=20, >=40, >=60, >=80 %, vol) plus laden/vol aan lader");
    w.end_object();
    w.key("navigation_buttons").value(static_cast<unsigned long long>(nav_));
    w.end_object();

    w.key("reports").begin_object();
    w.key("total").value(static_cast<unsigned long long>(st.reports));
    w.key("rate_hz_5s").value(ci.open ? st.rate(now, 5.0) : 0.0);
    double span = st.last_t_ns > st.first_t_ns ? static_cast<double>(st.last_t_ns - st.first_t_ns) / 1e9 : 0;
    w.key("rate_hz_since_connect").value(span > 0 ? static_cast<double>(st.intervals) / span : 0.0);
    w.key("imu_subsamples_per_report").value(ci.model == "ZCM2" ? 1 : 2);
    w.key("interval_ms").begin_object();
    w.key("p50").value(st.percentile(0.50)).key("p95").value(st.percentile(0.95));
    w.key("p99").value(st.percentile(0.99)).key("max").value(st.max_ms);
    w.end_object();
    w.key("interval_histogram").begin_array();
    double lo = 0;
    for (size_t i = 0; i < st.buckets.size(); ++i) {
        w.begin_object().key("from_ms").value(lo);
        if (i + 1 < st.buckets.size()) w.key("to_ms").value(ReportStats::kBucketUpperMs[i]);
        else w.key("to_ms").null();
        w.key("count").value(static_cast<unsigned long long>(st.buckets[i])).end_object();
        lo = ReportStats::kBucketUpperMs[i];
    }
    w.end_array();
    w.key("dropped_by_seq").value(static_cast<unsigned long long>(st.dropped_seq));
    w.key("non_monotonic").value(static_cast<unsigned long long>(st.non_monotonic));
    w.key("long_dt_50_250ms").value(static_cast<unsigned long long>(st.long_dt));
    w.key("gaps_not_integrated").value(static_cast<unsigned long long>(st.gap_not_integrated));
    w.end_object();

    w.key("processing_us").begin_object();
    w.key("p50").value(ps_.pct(0.5)).key("p95").value(ps_.pct(0.95)).key("max").value(ps_.max());
    w.key("meaning").value("t_publish - t_rx per rapport (fusie, aim, filter, publiceren)");
    w.end_object();

    const PipelineConfig& c = p.config();
    w.key("config").begin_object();
    w.key("filter").begin_object();
    w.key("enabled").value(c.filter.enabled).key("min_cutoff_hz").value(c.filter.min_cutoff_hz);
    w.key("beta").value(c.filter.beta).key("d_cutoff_hz").value(c.filter.d_cutoff_hz);
    w.end_object();
    w.key("aim").begin_object();
    w.key("mode").value("orientation (voorlopig, geen aim-ray)");
    w.key("h_edge_deg").value(c.aim.h_edge_deg).key("v_edge_deg").value(c.aim.v_edge_deg);
    w.key("predict_ms").value(c.predict.horizon_ms);
    w.end_object();
    w.key("predict").begin_object();
    w.key("horizon_ms").value(c.predict.horizon_ms).key("rate_tau_ms").value(c.predict.rate_tau_ms);
    w.key("accel_gain").value(c.predict.accel_gain).key("speed_lo_dps").value(c.predict.speed_lo_dps).key("speed_hi_dps").value(c.predict.speed_hi_dps);
    w.key("max_angle_deg").value(c.predict.max_angle_deg);
    w.key("rate_dps").value(norm(p.predictor().rate()) * kDegPerRad).key("gain").value(p.predictor().gain());
    w.key("angle_deg").value(s.pred_deg).key("report_age_ms").value(s.age_ms);
    w.end_object();
    w.key("fusion").begin_object();
    w.key("algorithm").value(p.fusion_mode() == FusionMode::MahonyMag ? "mahony_mag (Mahony + yaw-Kalman met magnetometer)" : is_vqf(p.fusion_mode()) ? (p.fusion_mode() == FusionMode::VqfMag ? "vqf_mag (VQF 9D)" : "vqf (VQF 6D)") : "mahony_p_imu");
    w.key("mode").value(to_string(p.fusion_mode()));
    w.key("kp").value(c.fusion.kp).key("accel_gate_g").value(c.fusion.accel_gate).key("max_dt_s").value(c.fusion.max_dt_s);
    w.key("magnetometer").value((p.fusion_mode() == FusionMode::MahonyMag || p.fusion_mode() == FusionMode::VqfMag) && p.mag().calibrated());
    w.key("gap_interpolation").value(c.interpolate_gaps);
    w.key("gaps_interpolated").value(static_cast<unsigned long long>(st.gap_interpolated));
    w.end_object();
    w.key("bias").begin_object();
    w.key("mode").value(to_string(c.bias.mode));
    w.key("window_s").value(c.bias.window_s).key("gyro_std_max_rad_s").value(c.bias.gyro_std_max);
    w.key("gyro_mean_max_rad_s").value(c.bias.gyro_mean_max).key("accel_norm_tol_g").value(c.bias.accel_norm_tol);
    w.key("tau_s").value(c.bias.tau_s);
    w.key("still_time_total_s").value(p.bias().still_time_total());
    w.end_object();
    w.key("stale_ms").value(static_cast<double>(c.stale_ns) / 1e6);
    w.key("release_ms").value(static_cast<double>(c.release_ns) / 1e6);
    w.key("disconnect_ms").value(static_cast<double>(opt.disconnect_ns) / 1e6);
    w.end_object();

    w.key("gun_buttons").begin_object();
    w.key("trigger_on").value(opt.gun_buttons.trigger_on).key("trigger_off").value(opt.gun_buttons.trigger_off);
    w.key("map").begin_object();
    for (const auto& kv : opt.gun_buttons.layout) w.key(kv.first).value(kv.second);
    w.end_object();
    w.end_object();

    w.key("centers").begin_object();
    w.key("auto_initial").value(static_cast<unsigned long long>(p.auto_center_count()));
    w.key("user").value(static_cast<unsigned long long>(p.user_center_count()));
    w.end_object();

    w.key("sample");
    write_sample_json(w, s, full_ids);
    w.end_object();
    std::string out = w.str();
    if (!out.empty() && out.back() == '}') { out.pop_back(); out += aux_.status_blocks(p, now) + "}"; }
    return out;
}

}  // namespace ravengund
