#include "commands.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include "build_info.hpp"
#include "camera.hpp"
#include "hid_thread.hpp"
#include "output_service.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/json.hpp"
#include "ravengun/kiosk/kiosk.hpp"

namespace ravengund {

using namespace ravengun;

namespace {

constexpr auto kJobTimeout = std::chrono::milliseconds(500);

std::string run_job(SharedState& sh, SharedState::Job j, std::string& err) {
    auto f = sh.post(std::move(j));
    if (f.wait_for(kJobTimeout) != std::future_status::ready) { err = "HID-thread reageert niet binnen 500 ms"; return {}; }
    try {
        return f.get();
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

// Status (HID-thread) plus camera en uitvoer als extra sleutels. Leeg resultaat bij een fout (err gevuld).
std::string status_result(SharedState& sh, bool full_ids, std::string& err) {
    std::string res = run_job(sh, [full_ids](HidContext& h) { return h.status_json(full_ids); }, err);
    if (!err.empty()) return {};
    if (sh.camera_status && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"camera\":" + sh.camera_status() + "}";
    }
    if (sh.output && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"output\":" + sh.output->status_json() + "}";
    }
    return res;
}

// Vervangt de thuismap door "~" (geen gebruikersnaam in exports).
std::string tilde(std::string path) {
    const char* h = std::getenv("HOME");
    if (h && *h && path.rfind(h, 0) == 0) path.replace(0, std::string(h).size(), "~");
    return path;
}

void write_strings(json::Writer& w, const std::vector<std::string>& v) {
    w.begin_array();
    for (const auto& s : v) w.value(s);
    w.end_array();
}

std::string settings_info_json(SharedState& sh) {
    json::Writer w;
    w.begin_object();
    if (!sh.settings) {
        w.key("available").value(false).end_object();
        return w.str();
    }
    config::LoadInfo li = sh.settings->info();
    w.key("available").value(true);
    w.key("path").value(tilde(sh.settings->path()));
    w.key("file_present").value(sh.settings->file_present());
    w.key("state").value(config::to_string(li.state));
    w.key("message").value(li.message);
    w.key("warnings");
    write_strings(w, li.warnings);
    std::string se = sh.settings->last_save_error();
    if (se.empty()) w.key("last_save_error").null(); else w.key("last_save_error").value(tilde(se));
    w.key("schema").value(config::kSchemaVersion);
    w.end_object();
    return w.str();
}

std::string info_json(SharedState& sh) {
    json::Writer w;
    w.begin_object();
    w.key("version").begin_object();
    w.key("daemon").value(RAVENGUN_VERSION).key("git_commit").value(RAVENGUN_GIT_COMMIT);
    w.key("psmoveapi_pin").value(RAVENGUN_PSMOVEAPI_PIN);
#ifdef RAVENGUN_NO_PSMOVEAPI
    w.key("psmoveapi_linked").value(false);
#else
    w.key("psmoveapi_linked").value(true);
#endif
    w.key("ipc_protocol").value(ipc::kProtocolVersion).key("input_schema").value(core::kInputSchemaVersion);
    w.end_object();
    w.key("daemon").begin_object();
    w.key("pid").value(static_cast<long long>(::getpid()));
    w.key("uptime_s").value(static_cast<double>(mono_ns() - sh.start_ns) / 1e9);
    w.key("web_port").value(sh.web_port).key("web_lan").value(sh.web_lan);
    w.end_object();
    w.key("settings").raw(settings_info_json(sh));
    w.end_object();
    return w.str();
}

void write_led_tracker(json::Writer& w, const config::LedChoice& c, double hue, double tol, bool changed, bool live) {
    w.key("led").begin_object().key("preset").value(c.preset);
    w.key("rgb").begin_array().value(c.r).value(c.g).value(c.b).end_array();
    w.key("custom").value(c.preset == "custom").end_object();
    w.key("tracker").begin_object().key("hue_deg").value(hue).key("hue_tol_deg").value(tol);
    w.key("source").value(c.hue_source).key("changed").value(changed).key("applied_live").value(live).end_object();
}

std::string handle_set_led(SharedState& sh, const ipc::Request& r) {
    config::LedChoice c;
    if (!r.led_preset.empty()) {
        if (!config::resolve_led_preset(r.led_preset, c))
            return ipc::error_response({"invalid_argument", "onbekende preset '" + r.led_preset + "' (cyaan, magenta, rood, groen, blauw, wit)", r.id_json});
    } else {
        c = config::resolve_led_rgb(static_cast<int>(*r.led_r), static_cast<int>(*r.led_g), static_cast<int>(*r.led_b));
    }
    std::string err;
    uint8_t cr = static_cast<uint8_t>(c.r), cg = static_cast<uint8_t>(c.g), cb = static_cast<uint8_t>(c.b);
    run_job(sh, [=](HidContext& h) { h.set_led(true, cr, cg, cb); return std::string("1"); }, err);
    if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});

    // De tracker zoekt op tint: de tint (en tolerantie) volgt de LED-kleur, ook zonder camera (voor de volgende start).
    double hue = -75, tol = 30;
    bool live = false;
    if (sh.settings) { auto st = sh.settings->get(); hue = st.tracker.hue_deg; tol = st.tracker.hue_tol_deg; }
    if (sh.camera && sh.camera->enabled()) {
        auto tc = sh.camera->tracker();
        hue = tc.hue_deg; tol = tc.hue_tol_deg;
        if (c.hue_known) {
            tc.hue_deg = c.hue_deg;
            tc.hue_tol_deg = c.hue_tol_deg;
            sh.camera->set_tracker(tc);
            live = true;
        }
    }
    if (c.hue_known) { hue = c.hue_deg; tol = c.hue_tol_deg; }

    config::SaveResult sr{true, {}};
    if (sh.settings) {
        sr = sh.settings->update([&](config::Settings& s) {
            s.led.enabled = true; s.led.preset = c.preset; s.led.r = c.r; s.led.g = c.g; s.led.b = c.b;
            if (c.hue_known) { s.tracker.hue_deg = c.hue_deg; s.tracker.hue_tol_deg = c.hue_tol_deg; }
        });
    }
    json::Writer w;
    w.begin_object();
    write_led_tracker(w, c, hue, tol, c.hue_known, live);
    w.key("warnings");
    write_strings(w, c.warnings);
    w.key("saved").value(sr.ok && sh.settings != nullptr);
    if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
    w.end_object();
    return ipc::ok_response(r.id_json, w.str());
}

std::string handle_set_camera(SharedState& sh, const ipc::Request& r) {
    if (!sh.camera) return ipc::error_response({"unavailable", "geen camera-service", r.id_json});
    CameraChange ch;
    if (r.exposure) ch.exposure = static_cast<int>(*r.exposure);
    if (r.gain) ch.gain = static_cast<int>(*r.gain);
    ch.auto_exposure = r.auto_exposure;
    ch.auto_gain = r.auto_gain;
    ch.auto_awb = r.auto_awb;
    if (!r.mode.empty()) {
        config::CameraMode m{};
        if (!config::parse_camera_mode(r.mode, m))
            return ipc::error_response({"invalid_argument", "'mode' moet 640x480@60, 320x240@125 of 320x240@187 zijn", r.id_json});
        ch.width = m.width; ch.height = m.height; ch.fps = m.fps;
    }
    CameraApplyResult res = sh.camera->apply(ch);
    if (!res.ok) return ipc::error_response({res.code.empty() ? "internal" : res.code, res.message, r.id_json});

    config::SaveResult sr{true, {}};
    if (sh.settings) {
        sr = sh.settings->update([&](config::Settings& s) {
            if (ch.width) { s.camera.width = *ch.width; s.camera.height = *ch.height; s.camera.fps = *ch.fps; }
            if (ch.exposure) s.camera.exposure = *ch.exposure;
            if (ch.gain) s.camera.gain = *ch.gain;
            if (res.controls.readable) {
                s.camera.auto_exposure = res.controls.auto_exposure;
                s.camera.auto_gain = res.controls.auto_gain;
                s.camera.auto_awb = res.controls.auto_awb;
            } else {
                if (ch.auto_exposure) s.camera.auto_exposure = *ch.auto_exposure;
                if (ch.auto_gain) s.camera.auto_gain = *ch.auto_gain;
                if (ch.auto_awb) s.camera.auto_awb = *ch.auto_awb;
            }
        });
    }
    json::Writer w;
    w.begin_object();
    w.key("mode").begin_object().key("width").value(res.width).key("height").value(res.height).key("fps").value(res.fps);
    w.key("driver_fps").value(res.driver_fps).end_object();
    w.key("restarted").value(res.restarted);
    w.key("controls").begin_object().key("readable").value(res.controls.readable);
    w.key("exposure").value(res.controls.exposure).key("gain").value(res.controls.gain);
    w.key("auto_exposure").value(res.controls.auto_exposure).key("auto_gain").value(res.controls.auto_gain);
    w.key("auto_awb").value(res.controls.auto_awb).end_object();
    w.key("warnings");
    write_strings(w, res.warnings);
    w.key("saved").value(sr.ok && sh.settings != nullptr);
    if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
    w.end_object();
    return ipc::ok_response(r.id_json, w.str());
}

std::string handle_set_web(SharedState& sh, const ipc::Request& r) {
    if (!sh.settings) return ipc::error_response({"unavailable", "geen instellingenopslag", r.id_json});
    int port = static_cast<int>(*r.web_port);
    config::SaveResult sr = sh.settings->update([&](config::Settings& s) { s.web.port = port; });
    if (!sr.ok) return ipc::error_response({"internal", tilde(sr.message), r.id_json});
    json::Writer w;
    w.begin_object().key("port").value(port).key("active_port").value(sh.web_port);
    w.key("applies").value("bij de volgende start van ravengund (een --web-port op de opdrachtregel gaat voor)").key("saved").value(true).end_object();
    return ipc::ok_response(r.id_json, w.str());
}

// Legt vast welke Move-knop elke fysieke Sharp Shooter-knop oplevert (kalibratie-UI, stap Knoppen-test).
std::string handle_set_buttons(SharedState& sh, const ipc::Request& r) {
    if (!sh.settings) return ipc::error_response({"unavailable", "geen instellingenopslag", r.id_json});
    config::SaveResult sr = sh.settings->update([&](config::Settings& s) {
        for (const auto& kv : r.button_map) {
            if (kv.second.empty()) s.sharp_shooter.buttons.erase(kv.first);
            else s.sharp_shooter.buttons[kv.first] = kv.second;
        }
    });
    if (!sr.ok) return ipc::error_response({"internal", tilde(sr.message), r.id_json});
    json::Writer w;
    w.begin_object().key("saved").value(true).key("buttons").begin_object();
    for (const auto& kv : sh.settings->get().sharp_shooter.buttons) w.key(kv.first).value(kv.second);
    w.end_object().end_object();
    return ipc::ok_response(r.id_json, w.str());
}

// Kiosk-browser op de tv (zie src/ravengun/kiosk/kiosk.hpp). Alleen een vaste opdracht, nooit een pad of URL van buiten.
std::string handle_calibrate_ui(SharedState& sh, const ipc::Request& r) {
    kiosk::Result res;
    if (r.action == "open") {
        if (sh.web_port <= 0) return ipc::error_response({"unavailable", "de webinterface staat uit: het kalibratiescherm heeft die nodig", r.id_json});
        // Een gevraagde stap op een al open scherm: eerst sluiten, anders blijft de browser op de oude pagina.
        if (!r.step.empty() && kiosk::status().running) kiosk::close();
        res = kiosk::open(sh.web_port, r.step);
    } else if (r.action == "close") {
        res = kiosk::close();
    } else {
        res = kiosk::status();
    }
    if (!res.ok) return ipc::error_response({"unavailable", res.message, r.id_json});
    json::Writer w;
    w.begin_object().key("running").value(res.running).key("processes").value(res.processes).key("message").value(res.message).end_object();
    return ipc::ok_response(r.id_json, w.str());
}

std::string handle_settings(SharedState& sh, const ipc::Request& r) {
    json::Writer w;
    w.begin_object();
    w.key("info").raw(settings_info_json(sh));
    if (sh.settings) w.key("values").raw(config::to_json(sh.settings->get(), false)); else w.key("values").null();
    w.key("presets").begin_array();
    for (const auto& p : config::led_presets()) {
        config::LedChoice c;
        config::resolve_led_preset(p.name, c);
        w.begin_object().key("name").value(p.name).key("rgb").begin_array().value(p.r).value(p.g).value(p.b).end_array();
        if (c.hue_known) w.key("hue_deg").value(c.hue_deg); else w.key("hue_deg").null();
        w.key("hue_tol_deg").value(c.hue_tol_deg).key("source").value(c.hue_source).key("warnings");
        write_strings(w, c.warnings);
        w.end_object();
    }
    w.end_array();
    w.key("camera_modes").begin_array();
    for (const auto& m : config::camera_modes()) w.value(config::camera_mode_string(m.width, m.height, m.fps));
    w.end_array();
    w.end_object();
    return ipc::ok_response(r.id_json, w.str());
}

std::string handle_diagnostics(SharedState& sh, const ipc::Request& r) {
    std::string err;
    std::string st = status_result(sh, false, err);  // controller-ID geredigeerd
    if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
    json::Writer w;
    w.begin_object();
    w.key("kind").value("ravengun-diagnostics").key("generated_unix").value(static_cast<long long>(std::time(nullptr)));
    w.key("redaction").value("controller-ID geredigeerd, thuismap als ~, geen camerabeelden");
    w.key("info").raw(info_json(sh));
    w.key("status").raw(st);
    if (sh.settings) w.key("settings").raw(config::to_json(sh.settings->get(), false)); else w.key("settings").null();
    w.end_object();
    // Thuismap overal vervangen (ook paden in de status: opnamen, kalibratiebestanden).
    std::string out = w.str();
    if (const char* h = std::getenv("HOME"); h && h[0] == '/' && h[1]) {
        std::string home(h);
        for (size_t pos = 0; (pos = out.find(home, pos)) != std::string::npos;) { out.replace(pos, home.size(), "~"); pos += 1; }
    }
    return ipc::ok_response(r.id_json, out);
}

// Bewaart gewijzigde filter/aim-waarden (na een geslaagd commando) in config.json.
void persist_filter(SharedState& sh, const ipc::Request& r) {
    if (!sh.settings) return;
    sh.settings->update([&](config::Settings& s) {
        if (r.filter_enabled) s.filter.enabled = *r.filter_enabled;
        if (r.min_cutoff_hz) s.filter.min_cutoff_hz = *r.min_cutoff_hz;
        if (r.beta) s.filter.beta = *r.beta;
        if (r.d_cutoff_hz) s.filter.d_cutoff_hz = *r.d_cutoff_hz;
    });
}
void persist_aim(SharedState& sh, const ipc::Request& r) {
    if (!sh.settings) return;
    sh.settings->update([&](config::Settings& s) {
        if (r.h_edge_deg) s.aim.h_edge_deg = *r.h_edge_deg;
        if (r.v_edge_deg) s.aim.v_edge_deg = *r.v_edge_deg;
        if (r.predict_ms) s.aim.predict_ms = *r.predict_ms;
    });
}

std::string output_reply(const ipc::Request& r, const OutputResult& res, SharedState& sh) {
    if (!res.ok) return ipc::error_response({res.code.empty() ? "internal" : res.code, res.message, r.id_json});
    return ipc::ok_response(r.id_json, sh.output->status_json());
}

std::string handle_output(SharedState& sh, const ipc::Request& r) {
    if (!sh.output) return ipc::error_response({"unavailable", "geen uitvoer-service", r.id_json});
    OutputService& o = *sh.output;
    switch (r.cmd) {
        case ipc::Cmd::Arm: return output_reply(r, o.arm(), sh);
        case ipc::Cmd::Disarm: return output_reply(r, o.disarm(), sh);
        case ipc::Cmd::Output: return ipc::ok_response(r.id_json, o.status_json());
        case ipc::Cmd::TestPattern: {
            if (r.pattern == "stop") return output_reply(r, o.stop_pattern(), sh);
            output::PatternParams p;
            if (!output::parse_pattern(r.pattern, p.kind))
                return ipc::error_response({"invalid_argument", "onbekend patroon '" + r.pattern + "' (center, corners, grid, sweep, point, stop)", r.id_json});
            if (r.dwell_ms) p.dwell_s = *r.dwell_ms / 1000.0;
            if (r.duration_s) p.duration_s = *r.duration_s;
            if (r.margin) p.margin = *r.margin;
            if (r.x) p.x = *r.x;
            if (r.y) p.y = *r.y;
            if (r.rate_hz_pattern) p.rate_hz = *r.rate_hz_pattern;
            return output_reply(r, o.start_pattern(p), sh);
        }
        case ipc::Cmd::OutputButton: {
            output::GunButton b;
            if (!output::parse_button(r.button, b))
                return ipc::error_response({"invalid_argument", "onbekende knop '" + r.button + "' (trigger, reload, grenade, aux1, aux2)", r.id_json});
            return output_reply(r, o.button(b, r.action), sh);
        }
        case ipc::Cmd::SetOutput: {
            output::GunTransform t = o.transform();
            if (r.invert_x) t.invert_x = *r.invert_x;
            if (r.invert_y) t.invert_y = *r.invert_y;
            if (r.scale_x) t.scale_x = *r.scale_x;
            if (r.scale_y) t.scale_y = *r.scale_y;
            if (r.offset_x) t.offset_x = *r.offset_x;
            if (r.offset_y) t.offset_y = *r.offset_y;
            if (r.screen_aspect && r.content_aspect) t.viewport = output::letterbox_viewport(*r.screen_aspect, *r.content_aspect);
            if (r.vp_x0) t.viewport.x0 = *r.vp_x0;
            if (r.vp_y0) t.viewport.y0 = *r.vp_y0;
            if (r.vp_x1) t.viewport.x1 = *r.vp_x1;
            if (r.vp_y1) t.viewport.y1 = *r.vp_y1;
            return output_reply(r, o.set_transform(t), sh);
        }
        default: break;
    }
    return ipc::error_response({"internal", "onbekend uitvoercommando", r.id_json});
}

// Resultaat van een HidContext-opdracht uit fase 0/1/3: JSON, of "!code|melding".
std::string context_reply(const ipc::Request& r, const std::string& res, const std::string& err) {
    if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
    if (!res.empty() && res[0] == '!') {
        size_t bar = res.find('|');
        std::string code = res.substr(1, bar == std::string::npos ? std::string::npos : bar - 1);
        std::string msg = bar == std::string::npos ? std::string() : res.substr(bar + 1);
        return ipc::error_response({code, msg, r.id_json});
    }
    return ipc::ok_response(r.id_json, res);
}

}  // namespace

std::string execute_command(SharedState& sh, const ipc::Request& r) {
    std::string err;
    switch (r.cmd) {
        case ipc::Cmd::Status: {
            std::string res = status_result(sh, r.full_ids, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Camera: {
            if (!sh.camera_status) return ipc::error_response({"unavailable", "geen camera-service", r.id_json});
            return ipc::ok_response(r.id_json, sh.camera_status());
        }
        case ipc::Cmd::Sample: {
            Snapshot s = sh.get();
            json::Writer w;
            core::write_sample_json(w, s.sample, r.full_ids);
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Recenter: {
            std::string res = run_job(sh, [](HidContext& h) { return std::string(h.recenter() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1")
                return ipc::error_response({"unavailable", "geen geldige, actuele oriëntatie: is de Move verbonden?", r.id_json});
            Snapshot s = sh.get();
            json::Writer w;
            w.begin_object().key("recentered").value(true).key("sample");
            core::write_sample_json(w, s.sample, false);
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::ClearTrim: {
            std::string res = run_job(sh, [](HidContext& h) { return std::string(h.clear_trim() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1") return ipc::error_response({"unavailable", "yaw-trim wissen niet beschikbaar", r.id_json});
            return ipc::ok_response(r.id_json, "{\"trim_cleared\":true}");
        }
        case ipc::Cmd::Watch:
            return ipc::error_response({"bad_request", "watch hoort bij een IPC-verbinding", r.id_json});
        case ipc::Cmd::SetFilter: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::OneEuroConfig f = h.filter();
                if (rq.filter_enabled) f.enabled = *rq.filter_enabled;
                if (rq.min_cutoff_hz) f.min_cutoff_hz = *rq.min_cutoff_hz;
                if (rq.beta) f.beta = *rq.beta;
                if (rq.d_cutoff_hz) f.d_cutoff_hz = *rq.d_cutoff_hz;
                h.set_filter(f);
                json::Writer w;
                w.begin_object().key("enabled").value(f.enabled).key("min_cutoff_hz").value(f.min_cutoff_hz)
                    .key("beta").value(f.beta).key("d_cutoff_hz").value(f.d_cutoff_hz).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            persist_filter(sh, r);
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::SetAim: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::AimConfig a = h.aim();
                // Alleen de randen herstellen het filter; de voorspelhorizon verandert live zonder reset.
                if (rq.h_edge_deg || rq.v_edge_deg) {
                    if (rq.h_edge_deg) a.h_edge_deg = *rq.h_edge_deg;
                    if (rq.v_edge_deg) a.v_edge_deg = *rq.v_edge_deg;
                    h.set_aim(a);
                }
                core::PredictConfig pc = h.predict();
                if (rq.predict_ms) { pc.horizon_ms = *rq.predict_ms; h.set_predict(pc); }
                json::Writer w;
                w.begin_object().key("h_edge_deg").value(a.h_edge_deg).key("v_edge_deg").value(a.v_edge_deg)
                    .key("predict_ms").value(pc.horizon_ms).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            persist_aim(sh, r);
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::SetClock: {
            if (!sh.output) return ipc::error_response({"unavailable", "geen uitvoerservice", r.id_json});
            ravengun::output::OutputClockConfig c = sh.output->clock_config();
            if (r.clock_hz) c.hz = *r.clock_hz;
            if (r.extrapolate_ms) c.max_extrap_ms = *r.extrapolate_ms;
            sh.output->set_clock(c);
            if (sh.settings) sh.settings->update([&](config::Settings& s) { s.output.clock_hz = c.hz; s.output.extrapolate_ms = c.max_extrap_ms; });
            json::Writer w;
            w.begin_object().key("clock_hz").value(c.hz).key("extrapolate_ms").value(c.max_extrap_ms).end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::SetLed: return handle_set_led(sh, r);
        case ipc::Cmd::SetCamera: return handle_set_camera(sh, r);
        case ipc::Cmd::SetWeb: return handle_set_web(sh, r);
        case ipc::Cmd::SetButtons: return handle_set_buttons(sh, r);
        case ipc::Cmd::CalibrateUi: return handle_calibrate_ui(sh, r);
        case ipc::Cmd::SetFusion: {
            core::FusionMode m = core::FusionMode::Mahony;
            core::parse_fusion_mode(r.mode.c_str(), m);
            std::string res = run_job(sh, [m](HidContext& h) { h.set_fusion(m); return std::string(core::to_string(h.fusion())); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            config::SaveResult sr{true, {}};
            if (sh.settings) sr = sh.settings->update([&](config::Settings& s) { s.fusion.mode = r.mode; });
            json::Writer w;
            w.begin_object().key("mode").value(res).key("saved").value(sr.ok && sh.settings != nullptr);
            if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Info: return ipc::ok_response(r.id_json, info_json(sh));
        case ipc::Cmd::Settings: return handle_settings(sh, r);
        case ipc::Cmd::Diagnostics: return handle_diagnostics(sh, r);
        case ipc::Cmd::Arm:
        case ipc::Cmd::Disarm:
        case ipc::Cmd::Output:
        case ipc::Cmd::TestPattern:
        case ipc::Cmd::OutputButton:
        case ipc::Cmd::SetOutput:
            return handle_output(sh, r);
        case ipc::Cmd::Record: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) { return h.record(rq); }, err);
            return context_reply(r, res, err);
        }
        case ipc::Cmd::CalibMag: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) { return h.calib_mag(rq); }, err);
            return context_reply(r, res, err);
        }
    }
    return ipc::error_response({"internal", "onbekende toestand", r.id_json});
}

std::string execute_line(SharedState& sh, const std::string& line) {
    auto pr = ipc::parse_request(line);
    if (auto* e = std::get_if<ipc::Error>(&pr)) return ipc::error_response(*e);
    return execute_command(sh, std::get<ipc::Request>(pr));
}

}  // namespace ravengund
