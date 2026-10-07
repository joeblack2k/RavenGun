// HID-inname via PSMoveAPI. Dit is de enige plek in RavenGun die PSMoveAPI aanroept.
#include "hid_thread.hpp"

#include <algorithm>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <vector>

#include "hid_aux.hpp"
#include "output_service.hpp"

#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"

#ifndef RAVENGUN_NO_PSMOVEAPI
#include <psmoveapi/psmove.h>
#endif

namespace ravengund {

using namespace ravengun;
using namespace ravengun::core;

namespace {

uint64_t led_updates = 0, led_failures = 0;  // alleen in de HID-thread geschreven en gelezen

struct ControllerInfo {
    bool open = false;
    std::string id;
    std::string model;
    std::string connection;
    bool has_calibration = false;
    int64_t connected_at_ns = 0;
    uint64_t connects = 0, disconnects = 0;
};

class ProcStats {
public:
    void add(double us) {
        if (v_.size() < 4096) v_.push_back(us);
        else { v_[pos_] = us; pos_ = (pos_ + 1) % 4096; }
    }
    double pct(double p) const {
        if (v_.empty()) return 0;
        auto c = v_;
        size_t k = static_cast<size_t>(p * static_cast<double>(c.size() - 1) + 0.5);
        std::nth_element(c.begin(), c.begin() + static_cast<long>(k), c.end());
        return c[k];
    }
    double max() const { return v_.empty() ? 0 : *std::max_element(v_.begin(), v_.end()); }

private:
    std::vector<double> v_;
    size_t pos_ = 0;
};

std::string status_json(const Pipeline& p, const ControllerInfo& ci, const ProcStats& ps, const HidOptions& opt,
                        int64_t start_ns, bool full_ids, const OpticalSample& optical) {
    int64_t now = mono_ns();
    const ReportStats& st = p.stats();
    InputSample s = p.last();
    s.optical = optical;  // laatste optische meting (uit de tracker-thread)
    json::Writer w;
    w.begin_object();
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
    w.key("magnetometer").value(ci.model == "ZCM1" ? (p.fusion_mode() == FusionMode::MahonyMag && p.mag().calibrated() ? "aanwezig, in de fusie (yaw-correctie, zie magnetometer)" : "aanwezig, niet in de fusie gebruikt (zie magnetometer)") : "afwezig of onbekend");
    w.key("connected_s").value(ci.open ? static_cast<double>(now - ci.connected_at_ns) / 1e9 : 0.0);
    w.key("connects").value(static_cast<unsigned long long>(ci.connects));
    w.key("led").begin_object();
    w.key("enabled").value(opt.led_enabled);
    w.key("rgb").begin_array().value(static_cast<int>(opt.led_r)).value(static_cast<int>(opt.led_g)).value(static_cast<int>(opt.led_b)).end_array();
    w.key("refresh_ms").value(opt.led_refresh_ms);
    w.key("updates").value(static_cast<unsigned long long>(led_updates));
    w.key("failures").value(static_cast<unsigned long long>(led_failures));
    w.end_object();
    w.key("disconnects").value(static_cast<unsigned long long>(ci.disconnects));
    BatteryInfo b = battery_info(s.battery_raw);
    w.key("battery").begin_object();
    if (s.battery_raw >= 0) w.key("raw").value(s.battery_raw); else w.key("raw").null();
    w.key("code").value(b.code).key("text").value(b.text).key("charging").value(b.charging);
    w.key("resolution").value("6 niveaus (<20, >=20, >=40, >=60, >=80 %, vol) plus laden/vol aan lader");
    w.end_object();
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
    w.key("p50").value(ps.pct(0.5)).key("p95").value(ps.pct(0.95)).key("max").value(ps.max());
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
    w.end_object();
    w.key("fusion").begin_object();
    w.key("algorithm").value(p.fusion_mode() == FusionMode::MahonyMag ? "mahony_mag (Mahony + yaw-Kalman met magnetometer)" : "mahony_p_imu");
    w.key("mode").value(to_string(p.fusion_mode()));
    w.key("kp").value(c.fusion.kp).key("accel_gate_g").value(c.fusion.accel_gate).key("max_dt_s").value(c.fusion.max_dt_s);
    w.key("magnetometer").value(p.fusion_mode() == FusionMode::MahonyMag && p.mag().calibrated());
    w.key("gap_interpolation").value(c.interpolate_gaps);
    w.key("gaps_interpolated").value(static_cast<unsigned long long>(st.gap_interpolated));
    w.end_object();
    w.key("bias").begin_object();
    w.key("window_s").value(c.bias.window_s).key("gyro_std_max_rad_s").value(c.bias.gyro_std_max);
    w.key("gyro_mean_max_rad_s").value(c.bias.gyro_mean_max).key("accel_norm_tol_g").value(c.bias.accel_norm_tol);
    w.key("tau_s").value(c.bias.tau_s);
    w.key("still_time_total_s").value(p.bias().still_time_total());
    w.end_object();
    w.key("stale_ms").value(static_cast<double>(c.stale_ns) / 1e6);
    w.key("release_ms").value(static_cast<double>(c.release_ns) / 1e6);
    w.key("disconnect_ms").value(static_cast<double>(opt.disconnect_ns) / 1e6);
    w.end_object();

    w.key("centers").begin_object();
    w.key("auto_initial").value(static_cast<unsigned long long>(p.auto_center_count()));
    w.key("user").value(static_cast<unsigned long long>(p.user_center_count()));
    w.end_object();

    w.key("sample");
    write_sample_json(w, s, full_ids);
    w.end_object();
    return w.str();
}

#ifndef RAVENGUN_NO_PSMOVEAPI
std::string serial_of(PSMove* m) {
    char* s = psmove_get_serial(m);
    std::string r = s ? s : "";
    if (s) psmove_free_mem(s);
    for (auto& ch : r) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return r;
}

// Zoekt een via Bluetooth verbonden Move (USB levert geen inputrapporten).
PSMove* try_connect(const std::string& preferred, ControllerInfo& ci) {
    int n = psmove_count_connected();
    for (int i = 0; i < n; ++i) {
        PSMove* m = psmove_connect_by_id(i);
        if (!m) continue;
        if (psmove_connection_type(m) != Conn_Bluetooth) { psmove_disconnect(m); continue; }
        std::string id = serial_of(m);
        if (!preferred.empty() && id != preferred) { psmove_disconnect(m); continue; }
        ci.open = true;
        ci.id = id;
        ci.model = psmove_get_model(m) == Model_ZCM1 ? "ZCM1" : psmove_get_model(m) == Model_ZCM2 ? "ZCM2" : "onbekend";
        ci.connection = "bluetooth";
        ci.has_calibration = psmove_has_calibration(m);
        return m;
    }
    return nullptr;
}

RawReport read_report(PSMove* m, int poll_ret, bool zcm2, int64_t t, HidrawTap& tap) {
    RawReport r;
    r.t_rx_ns = t;
    r.hid_seq = poll_ret - 1;  // psmove_poll geeft 1 + volgnummer
    r.subsamples = zcm2 ? 1 : 2;
    float x, y, z;
    for (int i = 0; i < r.subsamples; ++i) {
        // ZCM1: Frame_FirstHalf is het oudere subsample, Frame_SecondHalf het nieuwere (psmove.h).
        // ZCM2: één subsample; PSMoveAPI negeert daar de frame-parameter.
        PSMove_Frame f = i == 0 ? Frame_FirstHalf : Frame_SecondHalf;
        psmove_get_accelerometer_frame(m, f, &x, &y, &z);
        r.accel[i] = {x, y, z};
        psmove_get_gyroscope_frame(m, f, &x, &y, &z);
        r.gyro[i] = {x, y, z};
    }
    psmove_get_magnetometer(m, &r.mag_raw[0], &r.mag_raw[1], &r.mag_raw[2]);
    r.buttons = psmove_get_buttons(m);
    r.trigger = psmove_get_trigger(m);
    r.battery_raw = static_cast<int>(psmove_get_battery(m));
    r.temperature_raw = psmove_get_temperature(m);
    if (tap.is_open()) {
        // Hetzelfde rapport via de eigen hidraw-descriptor: interne stempel en ruwe waarden.
        int avg[3];
        psmove_get_accelerometer(m, &avg[0], &avg[1], &avg[2]);
        if (auto in = tap.take(r.hid_seq, avg)) {
            r.has_stamp = true;
            r.stamp = in->stamp;
            r.temperature_raw = in->temperature_raw;
            r.has_raw = true;
            std::memcpy(r.accel_raw, in->accel_raw, sizeof r.accel_raw);
            std::memcpy(r.gyro_raw, in->gyro_raw, sizeof r.gyro_raw);
        }
    }
    return r;
}
#endif

void sleep_us(int us) {
    timespec ts{0, static_cast<long>(us) * 1000};
    nanosleep(&ts, nullptr);
}

}  // namespace

void hid_thread_main(SharedState& shared, HidOptions opt) {
    Pipeline pipe(opt.pipeline);
    ControllerInfo ci;
    ProcStats ps;
    const int64_t start = mono_ns();
    HidAux aux(shared, opt.recorder, opt.pipeline.use_move_clock);
    aux.attach(&pipe);

    // Opdrachten uit de IPC-thread draaien hier, zodat de pijplijn één eigenaar heeft.
    struct Ctx final : HidContext {
        Pipeline& p; ControllerInfo& ci; ProcStats& ps; HidOptions& opt; int64_t start; SharedState& sh; HidAux& aux;
        bool led_dirty = false;
        Ctx(Pipeline& p_, ControllerInfo& c_, ProcStats& s_, HidOptions& o_, int64_t t_, SharedState& h_, HidAux& a_)
            : p(p_), ci(c_), ps(s_), opt(o_), start(t_), sh(h_), aux(a_) {}
        std::string status_json(bool full) override {
            std::string s = ravengund::status_json(p, ci, ps, opt, start, full, sh.get().sample.optical);
            if (!s.empty() && s.back() == '}') { s.pop_back(); s += aux.status_blocks(p, mono_ns()) + "}"; }
            return s;
        }
        std::string record(const ravengun::ipc::Request& r) override {
            int led[3] = {opt.led_r, opt.led_g, opt.led_b};
            return aux.record(r, opt.led_enabled, led, mono_ns());
        }
        std::string calib_mag(const ravengun::ipc::Request& r) override { return aux.calib_mag(r, mono_ns()); }
        bool recenter() override {
            bool ok = p.recenter();
            if (ok) { sh.publish(p.last()); logf("info", "recenter door gebruiker"); }
            return ok;
        }
        OneEuroConfig filter() const override { return p.config().filter; }
        void set_filter(const OneEuroConfig& c) override { p.set_filter(c); opt.pipeline.filter = c; }
        AimConfig aim() const override { return p.config().aim; }
        void set_aim(const AimConfig& c) override { p.set_aim(c); opt.pipeline.aim = c; }
        void set_led(bool en, uint8_t r, uint8_t g, uint8_t b) override { opt.led_enabled = en; opt.led_r = r; opt.led_g = g; opt.led_b = b; led_dirty = true; }
        FusionMode fusion() const override { return p.fusion_mode(); }
        void set_fusion(FusionMode m) override {
            if (m == p.fusion_mode()) return;
            p.set_fusion_mode(m);
            opt.pipeline.fusion_mode = m;
            logf("info", "oriëntatieschatter: %s", to_string(m));
        }
    } ctx(pipe, ci, ps, opt, start, shared, aux);
    auto handle_jobs = [&] { shared.run_jobs(ctx); };
    // Fase 4: elk gepubliceerd sample gaat ook naar de virtuele lightgun (alleen als die gewapend is).
    ravengun::output::LiveGunFeed live(opt.gun_buttons);
    auto emit = [&](const InputSample& s) {
        shared.publish(s);
        if (!shared.output) return;
        ravengun::output::LiveAction a = live.update(s);
        if (a.recenter && shared.output->armed()) {
            if (pipe.recenter()) {
                shared.publish(pipe.last());
                logf("info", "recenter met de Move-knop");
                a = live.update(pipe.last());
                a.recenter = false;
            }
        }
        shared.output->feed_live(a);
    };

#ifndef RAVENGUN_NO_PSMOVEAPI
    PSMove* move = nullptr;
    int64_t next_scan = 0;
    int64_t next_led = 0;
    if (opt.enabled) {
        psmove_set_remote_config(PSMove_OnlyLocal);  // geen netwerk (moved) gebruiken
        if (!psmove_init(PSMOVE_CURRENT_VERSION)) {
            logf("fout", "psmove_init() faalde (verkeerde PSMoveAPI-versie?)");
            opt.enabled = false;
        }
    }
#else
    opt.enabled = false;
#endif

    while (shared.running.load()) {
        handle_jobs();
#ifndef RAVENGUN_NO_PSMOVEAPI
        if (opt.enabled) {
            int64_t now = mono_ns();
            if (!move) {
                if (now >= next_scan) {
                    move = try_connect(opt.preferred_controller, ci);
                    next_scan = now + 1'000'000'000;
                    if (move) {
                        next_led = 0;
                        ci.connected_at_ns = mono_ns();
                        ++ci.connects;
                        pipe.on_connect(ci.id, ci.connected_at_ns);
                        aux.on_connect(ci.id, ci.model, ci.connected_at_ns);
                        shared.publish(pipe.last());
                        logf("info", "Move verbonden: %s, model %s, kalibratie %s", redact_mac(ci.id).c_str(),
                             ci.model.c_str(), ci.has_calibration ? "ja" : "NEE (waarden ongekalibreerd)");
                    }
                }
                aux.periodic(now);
                sleep_us(20000);
                continue;
            }
            if (ctx.led_dirty) { next_led = 0; ctx.led_dirty = false; }
            if (opt.led_enabled && now >= next_led) {
                // Alleen hier (HID-thread) wordt de LED aangestuurd. set_leds markeert de waarde als
                // gewijzigd, zodat update_leds hem echt verstuurt (PSMoveAPI zou anders pas na 4 s
                // herhalen, en dat is te krap voor de time-out van de Move).
                psmove_set_leds(move, opt.led_r, opt.led_g, opt.led_b);
                if (psmove_update_leds(move) == Update_Failed) ++led_failures;
                else { ++led_updates; aux.on_led_sent(now, opt.led_r, opt.led_g, opt.led_b); }
                next_led = now + static_cast<int64_t>(opt.led_refresh_ms) * 1'000'000;
            }
            bool zcm2 = ci.model == "ZCM2";
            bool got = false;
            aux.tap.drain();
            for (int k = 0; k < 64; ++k) {  // wachtrij leegmaken; elk rapport apart verwerken
                int ret = psmove_poll(move);
                if (ret <= 0) break;
                int64_t t = mono_ns();
                RawReport r = read_report(move, ret, zcm2, t, aux.tap);
                InputSample s = pipe.on_report(r);
                emit(s);
                ps.add(static_cast<double>(mono_ns() - t) / 1e3);
                aux.on_report(r, pipe, static_cast<double>(psmove_get_temperature_in_celsius(move)));
                got = true;
            }
            now = mono_ns();
            if (auto s = pipe.tick(now)) {
                emit(*s);
                if (s->link == LinkState::Lost) logf("waarschuwing", "geen rapporten sinds %.0f ms: knoppen vrijgegeven", static_cast<double>(opt.pipeline.release_ns) / 1e6);
            }
            const InputSample& last = pipe.last();
            int64_t last_rx = last.t_rx_ns ? last.t_rx_ns : ci.connected_at_ns;
            if (now - last_rx > opt.disconnect_ns) {
                logf("waarschuwing", "Move %s: %.1f s geen rapporten, verbinding gesloten; opnieuw zoeken",
                     redact_mac(ci.id).c_str(), static_cast<double>(now - last_rx) / 1e9);
                emit(pipe.on_disconnect(now));
                aux.on_disconnect(now);
                psmove_disconnect(move);
                move = nullptr;
                ci.open = false;
                ++ci.disconnects;
                next_scan = now + 500'000'000;
                continue;
            }
            aux.periodic(now);
            if (!got) {
                // Wachten op het volgende rapport: met de hidraw-tap via poll() (wordt direct wakker
                // bij een nieuw rapport), anders de oude slaap van idle_sleep_us.
                if (aux.tap.is_open()) {
                    pollfd pf{aux.tap.fd(), POLLIN, 0};
                    if (::poll(&pf, 1, 2) > 0 && (pf.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                        aux.tap.close();
                        logf("waarschuwing", "hidraw-tap gesloten (apparaat weg?); terug naar polling");
                    }
                } else {
                    sleep_us(opt.idle_sleep_us);
                }
            }
            continue;
        }
#endif
        aux.periodic(mono_ns());
        sleep_us(5000);
    }

#ifndef RAVENGUN_NO_PSMOVEAPI
    if (move) {
        emit(pipe.on_disconnect(mono_ns()));  // alle knoppen vrij (ook op de virtuele lightgun)
        if (opt.led_enabled) {
            psmove_set_leds(move, 0, 0, 0);
            psmove_update_leds(move);
        }
        psmove_disconnect(move);
        move = nullptr;
    }
#endif
    aux.on_disconnect(mono_ns());
    if (opt.recorder) opt.recorder->stop("shutdown");
    handle_jobs();
    logf("info", "HID-thread gestopt");
}

}  // namespace ravengund
