#include "ravengun/ipc_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "ravengun/config/settings.hpp"
#include "ravengun/games/games.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::ipc {

const char* to_string(Cmd c) {
    switch (c) {
        case Cmd::Status: return "status";
        case Cmd::Sample: return "sample";
        case Cmd::Recenter: return "recenter";
        case Cmd::Watch: return "watch";
        case Cmd::SetFilter: return "set_filter";
        case Cmd::SetAim: return "set_aim";
        case Cmd::Camera: return "camera";
        case Cmd::Arm: return "arm";
        case Cmd::Disarm: return "disarm";
        case Cmd::Output: return "output";
        case Cmd::TestPattern: return "test_pattern";
        case Cmd::OutputButton: return "output_button";
        case Cmd::SetOutput: return "set_output";
        case Cmd::Record: return "record";
        case Cmd::CalibMag: return "calib_mag";
        case Cmd::SetLed: return "set_led";
        case Cmd::SetCamera: return "set_camera";
        case Cmd::SetWeb: return "set_web";
        case Cmd::Info: return "info";
        case Cmd::Settings: return "settings";
        case Cmd::Diagnostics: return "diagnostics";
        case Cmd::SetButtons: return "set_buttons";
        case Cmd::CalibrateUi: return "calibrate_ui";
        case Cmd::SetFusion: return "set_fusion";
        case Cmd::ClearTrim: return "clear_trim";
        case Cmd::SetClock: return "set_clock";
        case Cmd::SetRumble: return "set_rumble";
        case Cmd::RumbleTest: return "rumble_test";
        case Cmd::SetGunButtons: return "set_gun_buttons";
        case Cmd::Games: return "games";
        case Cmd::GameStart: return "game_start";
        case Cmd::GameStop: return "game_stop";
        case Cmd::Controllers: return "controllers";
        case Cmd::SetController: return "set_controller";
        case Cmd::Blink: return "blink";
        case Cmd::Unpair: return "unpair";
        case Cmd::Pair: return "pair";
        case Cmd::Aagames: return "aagames";
        case Cmd::Iobridge: return "iobridge";
        case Cmd::OutputKey: return "output_key";
    }
    return "?";
}

bool takes_selector(Cmd c) {
    switch (c) {
        case Cmd::Status: case Cmd::Sample: case Cmd::Recenter: case Cmd::ClearTrim: case Cmd::Watch: case Cmd::SetFilter:
        case Cmd::SetAim: case Cmd::SetRumble: case Cmd::RumbleTest: case Cmd::SetLed: case Cmd::SetFusion: case Cmd::SetGunButtons:
        case Cmd::Record: case Cmd::CalibMag: case Cmd::Blink: case Cmd::OutputKey:
            return true;
        default: return false;
    }
}

namespace {

Error err(const char* code, std::string msg, const std::string& id = {}) { return Error{code, std::move(msg), id}; }

bool allowed(const json::Value& o, std::initializer_list<const char*> extra, std::string& bad) {
    for (const auto& kv : o.obj) {
        if (kv.first == "v" || kv.first == "cmd" || kv.first == "id") continue;
        if (kv.first == "player" || kv.first == "controller") continue;  // spelerkeuze: apart gecontroleerd (takes_selector)
        bool ok = false;
        for (const char* e : extra) if (kv.first == e) ok = true;
        if (!ok) { bad = kv.first; return false; }
    }
    return true;
}

// Haalt een optioneel getal binnen [lo, hi]; false bij een fout (err gevuld).
bool opt_number(const json::Value& o, const char* key, double lo, double hi, std::optional<double>& out, Error& e,
                const std::string& id) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (!v->is_number()) { e = err("invalid_argument", std::string("'") + key + "' moet een getal zijn", id); return false; }
    if (!(v->num >= lo && v->num <= hi)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "'%s' moet tussen %g en %g liggen", key, lo, hi);
        e = err("invalid_argument", buf, id);
        return false;
    }
    out = v->num;
    return true;
}

// Optioneel geheel getal binnen [lo, hi].
bool opt_int(const json::Value& o, const char* key, int lo, int hi, std::optional<double>& out, Error& e, const std::string& id) {
    const json::Value* v = o.find(key);
    if (v && v->is_number() && std::floor(v->num) != v->num) {
        e = err("invalid_argument", std::string("'") + key + "' moet een geheel getal zijn", id);
        return false;
    }
    return opt_number(o, key, lo, hi, out, e, id);
}

bool opt_bool(const json::Value& o, const char* key, std::optional<bool>& out, Error& e, const std::string& id) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (!v->is_bool()) { e = err("invalid_argument", std::string("'") + key + "' moet true of false zijn", id); return false; }
    out = v->b;
    return true;
}

bool opt_string(const json::Value& o, const char* key, std::string& out, Error& e, const std::string& id) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (!v->is_string() || v->str.size() > 32) { e = err("invalid_argument", std::string("'") + key + "' moet een korte string zijn", id); return false; }
    out = v->str;
    return true;
}

}  // namespace

std::variant<Request, Error> parse_request(std::string_view line) {
    if (line.size() > kMaxLineBytes) return err("line_too_long", "verzoek groter dan 64 KiB");
    auto pr = json::parse(line);
    if (!pr.ok) return err("parse_error", "ongeldige JSON: " + pr.error + " (positie " + std::to_string(pr.offset) + ")");
    const json::Value& o = pr.value;
    if (!o.is_object()) return err("bad_request", "verzoek moet een JSON-object zijn");

    Request r;
    if (const json::Value* id = o.find("id")) {
        if (id->is_number() && std::floor(id->num) == id->num && std::fabs(id->num) < 9e15) {
            r.id_json = std::to_string(static_cast<long long>(id->num));
        } else if (id->is_string() && id->str.size() <= 128) {
            r.id_json = json::quote(id->str);
        } else {
            return err("bad_request", "'id' moet een geheel getal of een string (max 128 tekens) zijn");
        }
    }
    const json::Value* v = o.find("v");
    if (!v) return err("bad_request", "'v' (protocolversie) ontbreekt", r.id_json);
    if (!v->is_number() || v->num != kProtocolVersion)
        return err("unsupported_version", "alleen protocolversie 1 wordt ondersteund", r.id_json);
    const json::Value* c = o.find("cmd");
    if (!c || !c->is_string()) return err("bad_request", "'cmd' (string) ontbreekt", r.id_json);

    std::string bad;
    Error e;
    const std::string& cmd = c->str;
    if (cmd == "status" || cmd == "sample") {
        r.cmd = cmd == "status" ? Cmd::Status : Cmd::Sample;
        if (!allowed(o, {"full_ids"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<bool> f;
        if (!opt_bool(o, "full_ids", f, e, r.id_json)) return e;
        r.full_ids = f.value_or(false);
    } else if (cmd == "camera") {
        r.cmd = Cmd::Camera;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "clear_trim") {
        r.cmd = Cmd::ClearTrim;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "recenter") {
        r.cmd = Cmd::Recenter;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "watch") {
        r.cmd = Cmd::Watch;
        if (!allowed(o, {"rate_hz", "full_ids"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<double> rate;
        if (!opt_number(o, "rate_hz", kWatchRateMin, kWatchRateMax, rate, e, r.id_json)) return e;
        r.rate_hz = rate.value_or(30.0);
        std::optional<bool> f;
        if (!opt_bool(o, "full_ids", f, e, r.id_json)) return e;
        r.full_ids = f.value_or(false);
    } else if (cmd == "set_filter") {
        r.cmd = Cmd::SetFilter;
        if (!allowed(o, {"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_bool(o, "enabled", r.filter_enabled, e, r.id_json)) return e;
        if (!opt_number(o, "min_cutoff_hz", 0.001, 100, r.min_cutoff_hz, e, r.id_json)) return e;
        if (!opt_number(o, "beta", 0, 1000, r.beta, e, r.id_json)) return e;
        if (!opt_number(o, "d_cutoff_hz", 0.001, 100, r.d_cutoff_hz, e, r.id_json)) return e;
        if (!r.filter_enabled && !r.min_cutoff_hz && !r.beta && !r.d_cutoff_hz)
            return err("invalid_argument", "set_filter zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_aim") {
        r.cmd = Cmd::SetAim;
        if (!allowed(o, {"h_edge_deg", "v_edge_deg", "predict_ms"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_number(o, "h_edge_deg", 1, 89, r.h_edge_deg, e, r.id_json)) return e;
        if (!opt_number(o, "v_edge_deg", 1, 89, r.v_edge_deg, e, r.id_json)) return e;
        if (!opt_number(o, "predict_ms", 0, 200, r.predict_ms, e, r.id_json)) return e;
        if (!r.h_edge_deg && !r.v_edge_deg && !r.predict_ms) return err("invalid_argument", "set_aim zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_clock") {
        r.cmd = Cmd::SetClock;
        if (!allowed(o, {"clock_hz", "extrapolate_ms"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_number(o, "clock_hz", 0, 1000, r.clock_hz, e, r.id_json)) return e;
        if (!opt_number(o, "extrapolate_ms", 0, 100, r.extrapolate_ms, e, r.id_json)) return e;
        if (!r.clock_hz && !r.extrapolate_ms) return err("invalid_argument", "set_clock zonder te wijzigen velden", r.id_json);
        if (r.clock_hz && *r.clock_hz != 0 && *r.clock_hz < 20) return err("invalid_argument", "'clock_hz' moet 0 (uit) zijn of tussen 20 en 1000 liggen", r.id_json);
    } else if (cmd == "set_rumble") {
        r.cmd = Cmd::SetRumble;
        if (!allowed(o, {"enabled", "strength", "pulse_ms", "repeat_hz", "pump_strength"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_bool(o, "enabled", r.rumble_enabled, e, r.id_json)) return e;
        if (!opt_int(o, "strength", 0, 255, r.rumble_strength, e, r.id_json)) return e;
        if (!opt_int(o, "pulse_ms", 10, 500, r.rumble_pulse_ms, e, r.id_json)) return e;
        if (!opt_number(o, "repeat_hz", 0, 30, r.rumble_repeat_hz, e, r.id_json)) return e;
        if (!opt_int(o, "pump_strength", 0, 255, r.rumble_pump_strength, e, r.id_json)) return e;
        if (!r.rumble_enabled && !r.rumble_strength && !r.rumble_pulse_ms && !r.rumble_repeat_hz && !r.rumble_pump_strength)
            return err("invalid_argument", "set_rumble zonder te wijzigen velden", r.id_json);
        if (r.rumble_repeat_hz && *r.rumble_repeat_hz != 0 && *r.rumble_repeat_hz < 1)
            return err("invalid_argument", "'repeat_hz' moet 0 (geen herhaling) zijn of tussen 1 en 30 liggen", r.id_json);
    } else if (cmd == "rumble_test") {
        r.cmd = Cmd::RumbleTest;
        if (!allowed(o, {"ms"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_int(o, "ms", 10, 1000, r.rumble_ms, e, r.id_json)) return e;
    } else if (cmd == "arm" || cmd == "disarm" || cmd == "output") {
        r.cmd = cmd == "arm" ? Cmd::Arm : cmd == "disarm" ? Cmd::Disarm : Cmd::Output;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "test_pattern") {
        r.cmd = Cmd::TestPattern;
        if (!allowed(o, {"pattern", "dwell_ms", "duration_s", "margin", "x", "y", "rate_hz"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "pattern", r.pattern, e, r.id_json)) return e;
        if (r.pattern.empty()) return err("invalid_argument", "'pattern' ontbreekt (center, corners, grid, sweep, point of stop)", r.id_json);
        if (!opt_number(o, "dwell_ms", 50, 60000, r.dwell_ms, e, r.id_json)) return e;
        if (!opt_number(o, "duration_s", 0.5, 600, r.duration_s, e, r.id_json)) return e;
        if (!opt_number(o, "margin", 0, 0.499, r.margin, e, r.id_json)) return e;
        if (!opt_number(o, "x", 0, 1, r.x, e, r.id_json)) return e;
        if (!opt_number(o, "y", 0, 1, r.y, e, r.id_json)) return e;
        if (!opt_number(o, "rate_hz", 1, 1000, r.rate_hz_pattern, e, r.id_json)) return e;
    } else if (cmd == "output_button") {
        r.cmd = Cmd::OutputButton;
        if (!allowed(o, {"button", "action"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "button", r.button, e, r.id_json)) return e;
        if (!opt_string(o, "action", r.action, e, r.id_json)) return e;
        if (r.button.empty()) return err("invalid_argument", "'button' ontbreekt", r.id_json);
        if (r.action.empty()) r.action = "tap";
        if (r.action != "down" && r.action != "up" && r.action != "tap")
            return err("invalid_argument", "'action' moet down, up of tap zijn", r.id_json);
    } else if (cmd == "set_output") {
        r.cmd = Cmd::SetOutput;
        if (!allowed(o, {"invert_x", "invert_y", "scale_x", "scale_y", "offset_x", "offset_y", "viewport_x0", "viewport_y0",
                         "viewport_x1", "viewport_y1", "screen_aspect", "content_aspect"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_bool(o, "invert_x", r.invert_x, e, r.id_json)) return e;
        if (!opt_bool(o, "invert_y", r.invert_y, e, r.id_json)) return e;
        if (!opt_number(o, "scale_x", 0.05, 20, r.scale_x, e, r.id_json)) return e;
        if (!opt_number(o, "scale_y", 0.05, 20, r.scale_y, e, r.id_json)) return e;
        if (!opt_number(o, "offset_x", -1, 1, r.offset_x, e, r.id_json)) return e;
        if (!opt_number(o, "offset_y", -1, 1, r.offset_y, e, r.id_json)) return e;
        if (!opt_number(o, "viewport_x0", 0, 1, r.vp_x0, e, r.id_json)) return e;
        if (!opt_number(o, "viewport_y0", 0, 1, r.vp_y0, e, r.id_json)) return e;
        if (!opt_number(o, "viewport_x1", 0, 1, r.vp_x1, e, r.id_json)) return e;
        if (!opt_number(o, "viewport_y1", 0, 1, r.vp_y1, e, r.id_json)) return e;
        if (!opt_number(o, "screen_aspect", 0.1, 10, r.screen_aspect, e, r.id_json)) return e;
        if (!opt_number(o, "content_aspect", 0.1, 10, r.content_aspect, e, r.id_json)) return e;
        if (r.screen_aspect.has_value() != r.content_aspect.has_value())
            return err("invalid_argument", "screen_aspect en content_aspect horen samen", r.id_json);
        bool any = r.invert_x || r.invert_y || r.scale_x || r.scale_y || r.offset_x || r.offset_y || r.vp_x0 || r.vp_y0 || r.vp_x1 ||
                   r.vp_y1 || r.screen_aspect;
        if (!any) return err("invalid_argument", "set_output zonder te wijzigen velden", r.id_json);
    } else if (cmd == "record" || cmd == "calib_mag") {
        bool recd = cmd == "record";
        r.cmd = recd ? Cmd::Record : Cmd::CalibMag;
        if (!allowed(o, recd ? std::initializer_list<const char*>{"action", "tag", "label", "max_mb", "duration_s"}
                             : std::initializer_list<const char*>{"action", "holder"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        const json::Value* a = o.find("action");
        if (!a || !a->is_string()) return err("invalid_argument", "'action' (string) ontbreekt", r.id_json);
        r.action = a->str;
        bool ok_action = recd ? (r.action == "start" || r.action == "stop" || r.action == "status" || r.action == "mark")
                              : (r.action == "start" || r.action == "stop" || r.action == "abort" || r.action == "status");
        if (!ok_action) return err("invalid_argument", "onbekende action '" + r.action + "'", r.id_json);
        auto ident = [](const std::string& t) {
            if (t.empty() || t.size() > 40) return false;
            for (char ch : t)
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
            return true;
        };
        if (const json::Value* t = o.find(recd ? "tag" : "holder")) {
            if (!t->is_string() || !ident(t->str))
                return err("invalid_argument", std::string("'") + (recd ? "tag" : "holder") + "' mag alleen a-z, 0-9, _ en - bevatten (max. 40)", r.id_json);
            (recd ? r.tag : r.holder) = t->str;
        }
        if (const json::Value* l = o.find("label")) {
            if (!l->is_string() || l->str.empty() || l->str.size() > 80)
                return err("invalid_argument", "'label' moet een string van 1..80 tekens zijn", r.id_json);
            for (unsigned char ch : l->str)
                if (ch < 0x20 || ch == 0x7f) return err("invalid_argument", "'label' bevat stuurtekens", r.id_json);
            r.label = l->str;
        }
        if (recd) {
            if (!opt_number(o, "max_mb", 1, 1024, r.max_mb, e, r.id_json)) return e;
            if (!opt_number(o, "duration_s", 1, 7200, r.duration_s, e, r.id_json)) return e;
            if (r.action == "mark" && r.label.empty()) return err("invalid_argument", "mark vereist 'label'", r.id_json);
        }
    } else if (cmd == "set_led") {
        r.cmd = Cmd::SetLed;
        if (!allowed(o, {"preset", "r", "g", "b"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "preset", r.led_preset, e, r.id_json)) return e;
        if (!opt_int(o, "r", 0, 255, r.led_r, e, r.id_json)) return e;
        if (!opt_int(o, "g", 0, 255, r.led_g, e, r.id_json)) return e;
        if (!opt_int(o, "b", 0, 255, r.led_b, e, r.id_json)) return e;
        bool any_rgb = r.led_r || r.led_g || r.led_b, all_rgb = r.led_r && r.led_g && r.led_b;
        if (!r.led_preset.empty() && any_rgb) return err("invalid_argument", "geef een 'preset' of r,g,b, niet allebei", r.id_json);
        if (r.led_preset.empty() && !all_rgb)
            return err("invalid_argument", "geef een 'preset' (cyaan, magenta, rood, groen, blauw, wit) of alle drie r, g en b (0..255)", r.id_json);
    } else if (cmd == "set_camera") {
        r.cmd = Cmd::SetCamera;
        if (!allowed(o, {"exposure", "gain", "auto_exposure", "auto_gain", "auto_awb", "mode"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_int(o, "exposure", 0, 255, r.exposure, e, r.id_json)) return e;
        if (!opt_int(o, "gain", 0, 63, r.gain, e, r.id_json)) return e;
        if (!opt_bool(o, "auto_exposure", r.auto_exposure, e, r.id_json)) return e;
        if (!opt_bool(o, "auto_gain", r.auto_gain, e, r.id_json)) return e;
        if (!opt_bool(o, "auto_awb", r.auto_awb, e, r.id_json)) return e;
        if (!opt_string(o, "mode", r.mode, e, r.id_json)) return e;
        if (!r.mode.empty()) {
            config::CameraMode m{};
            if (!config::parse_camera_mode(r.mode, m))
                return err("invalid_argument", "'mode' moet 640x480@60, 320x240@125 of 320x240@187 zijn", r.id_json);
        }
        if (!r.exposure && !r.gain && !r.auto_exposure && !r.auto_gain && !r.auto_awb && r.mode.empty())
            return err("invalid_argument", "set_camera zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_web") {
        r.cmd = Cmd::SetWeb;
        if (!allowed(o, {"port"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_int(o, "port", 1, 65535, r.web_port, e, r.id_json)) return e;
        if (!r.web_port) return err("invalid_argument", "set_web zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_buttons") {
        r.cmd = Cmd::SetButtons;
        for (const auto& kv : o.obj) {
            if (kv.first == "v" || kv.first == "cmd" || kv.first == "id") continue;
            const auto& roles = config::sharp_shooter_roles();
            if (std::find(roles.begin(), roles.end(), kv.first) == roles.end())
                return err("bad_request", "onbekend veld '" + kv.first + "'", r.id_json);
            if (!kv.second.is_string())
                return err("invalid_argument", "'" + kv.first + "' moet een tekst zijn (Move-knop of leeg)", r.id_json);
            if (!kv.second.str.empty() && !config::move_button_name_valid(kv.second.str))
                return err("invalid_argument", "'" + kv.first + "': onbekende Move-knop '" + kv.second.str + "' (triangle, circle, cross, square, select, start, ps, move, t)", r.id_json);
            r.button_map[kv.first] = kv.second.str;
        }
        if (r.button_map.empty()) return err("invalid_argument", "set_buttons zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_fusion") {
        r.cmd = Cmd::SetFusion;
        if (!allowed(o, {"mode", "recenter_learning"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "mode", r.mode, e, r.id_json)) return e;
        if (!opt_bool(o, "recenter_learning", r.recenter_learning, e, r.id_json)) return e;
        if (r.mode.empty() && !r.recenter_learning) return err("invalid_argument", "set_fusion zonder te wijzigen velden", r.id_json);
        if (!r.mode.empty() && r.mode != "mahony" && r.mode != "mahony_mag" && r.mode != "vqf" && r.mode != "vqf_mag")
            return err("invalid_argument", "'mode' moet mahony, mahony_mag, vqf of vqf_mag zijn", r.id_json);
    } else if (cmd == "set_gun_buttons") {
        r.cmd = Cmd::SetGunButtons;
        if (!allowed(o, {"map"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        const json::Value* m = o.find("map");
        if (!m || !m->is_object() || m->obj.empty()) return err("invalid_argument", "'map' (object met Move-knop -> actie) ontbreekt", r.id_json);
        const auto& buttons = config::gun_map_buttons();
        for (const auto& kv : m->obj) {
            if (kv.first == "ps") return err("invalid_argument", "de PS-knop is altijd centreren en kan niet worden ingedeeld", r.id_json);
            if (kv.first == "t") return err("invalid_argument", "de trekker (T) kan niet worden ingedeeld", r.id_json);
            if (std::find(buttons.begin(), buttons.end(), kv.first) == buttons.end())
                return err("invalid_argument", "'" + kv.first + "' is geen indeelbare knop", r.id_json);
            if (!kv.second.is_string() || kv.second.str.size() > 40)
                return err("invalid_argument", "de actie van '" + kv.first + "' moet een korte tekst zijn", r.id_json);
            std::string canon;
            std::string msg = config::check_gun_action(kv.second.str, canon);
            if (!msg.empty()) return err("invalid_argument", kv.first + ": " + msg, r.id_json);
            r.gun_map[kv.first] = canon;
        }
    } else if (cmd == "games" || cmd == "game_stop") {
        r.cmd = cmd == "games" ? Cmd::Games : Cmd::GameStop;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "game_start") {
        r.cmd = Cmd::GameStart;
        if (!allowed(o, {"profile"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        const json::Value* p = o.find("profile");
        if (!p || !p->is_string()) return err("invalid_argument", "'profile' (profielnaam) ontbreekt", r.id_json);
        if (!games::valid_profile_name(p->str))
            return err("invalid_argument", "ongeldige profielnaam (alleen letters, cijfers, _ en -, hoogstens 64 tekens)", r.id_json);
        r.profile = p->str;
    } else if (cmd == "calibrate_ui") {
        r.cmd = Cmd::CalibrateUi;
        if (!allowed(o, {"action", "step"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        const json::Value* a = o.find("action");
        if (!a || !a->is_string() || (a->str != "open" && a->str != "close" && a->str != "status"))
            return err("invalid_argument", "'action' moet open, close of status zijn", r.id_json);
        r.action = a->str;
        if (const json::Value* st = o.find("step")) {
            if (!st->is_string() || st->str != "practice" || r.action != "open")
                return err("invalid_argument", "'step' mag alleen \"practice\" zijn, bij open", r.id_json);
            r.step = st->str;
        }
    } else if (cmd == "controllers") {
        r.cmd = Cmd::Controllers;
        if (!allowed(o, {"full_ids"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<bool> f;
        if (!opt_bool(o, "full_ids", f, e, r.id_json)) return e;
        r.full_ids = f.value_or(false);
    } else if (cmd == "set_controller") {
        r.cmd = Cmd::SetController;
        if (!allowed(o, {"name", "new_player", "reset_settings"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (const json::Value* n = o.find("name")) {
            if (!n->is_string() || !config::controller_name_valid(n->str))
                return err("invalid_argument", "'name' moet 1 tot 32 gewone tekens zijn", r.id_json);
            r.name = n->str;
        }
        std::optional<double> np;
        if (!opt_int(o, "new_player", 1, 4, np, e, r.id_json)) return e;
        if (np) r.new_player = static_cast<int>(*np);
        std::optional<bool> rs;
        if (!opt_bool(o, "reset_settings", rs, e, r.id_json)) return e;
        r.reset_settings = rs.value_or(false);
        if (r.name.empty() && !r.new_player && !r.reset_settings) return err("invalid_argument", "set_controller zonder te wijzigen velden", r.id_json);
    } else if (cmd == "blink") {
        r.cmd = Cmd::Blink;
        if (!allowed(o, {"ms"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_int(o, "ms", 200, 10000, r.blink_ms, e, r.id_json)) return e;
    } else if (cmd == "unpair") {
        r.cmd = Cmd::Unpair;
        if (!allowed(o, {"keep_settings"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<bool> k;
        if (!opt_bool(o, "keep_settings", k, e, r.id_json)) return e;
        r.keep_settings = k.value_or(true);
    } else if (cmd == "pair") {
        r.cmd = Cmd::Pair;
        if (!allowed(o, {"action"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        const json::Value* a = o.find("action");
        if (!a || !a->is_string() || (a->str != "start" && a->str != "status" && a->str != "confirm" && a->str != "cancel"))
            return err("invalid_argument", "'action' moet start, status, confirm of cancel zijn", r.id_json);
        r.action = a->str;
    } else if (cmd == "aagames") {
        r.cmd = Cmd::Aagames;
        if (!allowed(o, {"action", "game"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "action", r.action, e, r.id_json)) return e;
        if (!opt_string(o, "game", r.game, e, r.id_json)) return e;
        if (r.action.empty()) r.action = "status";
        if (r.action != "start" && r.action != "stop" && r.action != "status")
            return err("invalid_argument", "'action' moet start, stop of status zijn", r.id_json);
        if (r.action == "start" && !games::valid_profile_name(r.game))
            return err("invalid_argument", "'game' ontbreekt of is ongeldig (letters, cijfers, _ en -)", r.id_json);
    } else if (cmd == "iobridge") {
        r.cmd = Cmd::Iobridge;
        if (!allowed(o, {"action", "game", "route"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "action", r.action, e, r.id_json)) return e;
        if (!opt_string(o, "game", r.game, e, r.id_json)) return e;
        if (!opt_string(o, "route", r.route, e, r.id_json)) return e;
        if (r.action.empty()) r.action = "status";
        if (r.action != "start" && r.action != "stop" && r.action != "status" && r.action != "route")
            return err("invalid_argument", "'action' moet start, stop, status of route zijn", r.id_json);
        if ((r.action == "start" || r.action == "route") && !games::valid_profile_name(r.game))
            return err("invalid_argument", "'game' ontbreekt of is ongeldig (letters, cijfers, _ en -)", r.id_json);
        if (!r.route.empty() && r.route != "evdev" && r.route != "tp-shm" && r.route != "unity-tcp")
            return err("invalid_argument", "'route' moet evdev, tp-shm of unity-tcp zijn", r.id_json);
    } else if (cmd == "output_key") {
        r.cmd = Cmd::OutputKey;
        if (!allowed(o, {"key", "action"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_string(o, "key", r.key_name, e, r.id_json)) return e;
        if (!opt_string(o, "action", r.action, e, r.id_json)) return e;
        if (r.key_name.empty()) return err("invalid_argument", "'key' ontbreekt", r.id_json);
        if (r.action.empty()) r.action = "tap";
        if (r.action != "down" && r.action != "up" && r.action != "tap")
            return err("invalid_argument", "'action' moet down, up of tap zijn", r.id_json);
    } else if (cmd == "info" || cmd == "settings" || cmd == "diagnostics") {
        r.cmd = cmd == "info" ? Cmd::Info : cmd == "settings" ? Cmd::Settings : Cmd::Diagnostics;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else {
        return err("unknown_command", "onbekend commando '" + cmd + "'", r.id_json);
    }
    // Spelerkeuze: alleen bij per-controller-opdrachten; set_controller en unpair hebben een controller nodig.
    const json::Value* pv = o.find("player");
    const json::Value* cv = o.find("controller");
    const bool needs_controller = r.cmd == Cmd::SetController || r.cmd == Cmd::Unpair;
    if ((pv || cv) && !takes_selector(r.cmd) && !needs_controller)
        return err("bad_request", "onbekend veld '" + std::string(pv ? "player" : "controller") + "'", r.id_json);
    if (pv && needs_controller) return err("bad_request", "onbekend veld 'player' (gebruik 'controller')", r.id_json);
    if (pv && cv) return err("invalid_argument", "geef 'player' of 'controller', niet allebei", r.id_json);
    if (pv) {
        if (!pv->is_number() || std::floor(pv->num) != pv->num || pv->num < 1 || pv->num > 4)
            return err("invalid_argument", "'player' moet een geheel getal van 1 tot 4 zijn", r.id_json);
        r.player = static_cast<int>(pv->num);
    }
    if (cv) {
        bool hex16 = cv->is_string() && cv->str.size() == 16;
        if (hex16) for (char ch : cv->str) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) hex16 = false;
        const bool ok = cv->is_string() && (config::address_valid(cv->str) || hex16 || (cv->str == "default" && !needs_controller));
        if (!ok) return err("invalid_argument", "'controller' moet een Bluetooth-adres (AA:BB:CC:DD:EE:FF), een controller-sleutel of 'default' zijn", r.id_json);
        r.controller = cv->str;
    }
    if (needs_controller && r.controller.empty()) return err("invalid_argument", "'controller' (adres of sleutel) ontbreekt", r.id_json);
    return r;
}

std::string ok_response(const std::string& id_json, std::string_view result_json) {
    std::string s = "{\"v\":1,\"ok\":true";
    if (!id_json.empty()) s += ",\"id\":" + id_json;
    s += ",\"result\":";
    s += result_json;
    s += '}';
    return s;
}

std::string error_response(const Error& e) {
    json::Writer w;
    w.begin_object().key("v").value(kProtocolVersion).key("ok").value(false);
    if (!e.id_json.empty()) w.key("id").raw(e.id_json);
    w.key("error").begin_object().key("code").value(e.code).key("message").value(e.message).end_object();
    w.end_object();
    return w.str();
}

std::string event_line(std::string_view name, std::string_view data_json) {
    std::string s = "{\"v\":1,\"event\":" + json::quote(name) + ",\"data\":";
    s += data_json;
    s += '}';
    return s;
}

std::string make_request(const Request& r) {
    json::Writer w;
    w.begin_object().key("v").value(kProtocolVersion).key("cmd").value(to_string(r.cmd));
    if (!r.id_json.empty()) w.key("id").raw(r.id_json);
    if (r.player) w.key("player").value(*r.player);
    if (!r.controller.empty()) w.key("controller").value(r.controller);
    switch (r.cmd) {
        case Cmd::Status:
        case Cmd::Sample:
            if (r.full_ids) w.key("full_ids").value(true);
            break;
        case Cmd::Watch:
            w.key("rate_hz").value(r.rate_hz);
            if (r.full_ids) w.key("full_ids").value(true);
            break;
        case Cmd::SetFilter:
            if (r.filter_enabled) w.key("enabled").value(*r.filter_enabled);
            if (r.min_cutoff_hz) w.key("min_cutoff_hz").value(*r.min_cutoff_hz);
            if (r.beta) w.key("beta").value(*r.beta);
            if (r.d_cutoff_hz) w.key("d_cutoff_hz").value(*r.d_cutoff_hz);
            break;
        case Cmd::SetAim:
            if (r.h_edge_deg) w.key("h_edge_deg").value(*r.h_edge_deg);
            if (r.v_edge_deg) w.key("v_edge_deg").value(*r.v_edge_deg);
            if (r.predict_ms) w.key("predict_ms").value(*r.predict_ms);
            break;
        case Cmd::SetClock:
            if (r.clock_hz) w.key("clock_hz").value(*r.clock_hz);
            if (r.extrapolate_ms) w.key("extrapolate_ms").value(*r.extrapolate_ms);
            break;
        case Cmd::SetRumble:
            if (r.rumble_enabled) w.key("enabled").value(*r.rumble_enabled);
            if (r.rumble_strength) w.key("strength").value(*r.rumble_strength);
            if (r.rumble_pulse_ms) w.key("pulse_ms").value(*r.rumble_pulse_ms);
            if (r.rumble_repeat_hz) w.key("repeat_hz").value(*r.rumble_repeat_hz);
            if (r.rumble_pump_strength) w.key("pump_strength").value(*r.rumble_pump_strength);
            break;
        case Cmd::RumbleTest:
            if (r.rumble_ms) w.key("ms").value(*r.rumble_ms);
            break;
        case Cmd::TestPattern:
            w.key("pattern").value(r.pattern);
            if (r.dwell_ms) w.key("dwell_ms").value(*r.dwell_ms);
            if (r.duration_s) w.key("duration_s").value(*r.duration_s);
            if (r.margin) w.key("margin").value(*r.margin);
            if (r.x) w.key("x").value(*r.x);
            if (r.y) w.key("y").value(*r.y);
            if (r.rate_hz_pattern) w.key("rate_hz").value(*r.rate_hz_pattern);
            break;
        case Cmd::OutputButton:
            w.key("button").value(r.button).key("action").value(r.action.empty() ? "tap" : r.action);
            break;
        case Cmd::SetOutput:
            if (r.invert_x) w.key("invert_x").value(*r.invert_x);
            if (r.invert_y) w.key("invert_y").value(*r.invert_y);
            if (r.scale_x) w.key("scale_x").value(*r.scale_x);
            if (r.scale_y) w.key("scale_y").value(*r.scale_y);
            if (r.offset_x) w.key("offset_x").value(*r.offset_x);
            if (r.offset_y) w.key("offset_y").value(*r.offset_y);
            if (r.vp_x0) w.key("viewport_x0").value(*r.vp_x0);
            if (r.vp_y0) w.key("viewport_y0").value(*r.vp_y0);
            if (r.vp_x1) w.key("viewport_x1").value(*r.vp_x1);
            if (r.vp_y1) w.key("viewport_y1").value(*r.vp_y1);
            if (r.screen_aspect) w.key("screen_aspect").value(*r.screen_aspect);
            if (r.content_aspect) w.key("content_aspect").value(*r.content_aspect);
            break;
        case Cmd::Record:
        case Cmd::CalibMag:
            w.key("action").value(r.action);
            if (!r.tag.empty()) w.key("tag").value(r.tag);
            if (!r.label.empty()) w.key("label").value(r.label);
            if (!r.holder.empty()) w.key("holder").value(r.holder);
            if (r.max_mb) w.key("max_mb").value(*r.max_mb);
            if (r.duration_s) w.key("duration_s").value(*r.duration_s);
            break;
        case Cmd::SetLed:
            if (!r.led_preset.empty()) w.key("preset").value(r.led_preset);
            if (r.led_r) w.key("r").value(*r.led_r);
            if (r.led_g) w.key("g").value(*r.led_g);
            if (r.led_b) w.key("b").value(*r.led_b);
            break;
        case Cmd::SetCamera:
            if (r.exposure) w.key("exposure").value(*r.exposure);
            if (r.gain) w.key("gain").value(*r.gain);
            if (r.auto_exposure) w.key("auto_exposure").value(*r.auto_exposure);
            if (r.auto_gain) w.key("auto_gain").value(*r.auto_gain);
            if (r.auto_awb) w.key("auto_awb").value(*r.auto_awb);
            if (!r.mode.empty()) w.key("mode").value(r.mode);
            break;
        case Cmd::SetWeb:
            if (r.web_port) w.key("port").value(*r.web_port);
            break;
        case Cmd::SetButtons:
            for (const auto& kv : r.button_map) w.key(kv.first).value(kv.second);
            break;
        case Cmd::CalibrateUi:
            w.key("action").value(r.action);
            if (!r.step.empty()) w.key("step").value(r.step);
            break;
        case Cmd::SetFusion:
            if (!r.mode.empty()) w.key("mode").value(r.mode);
            if (r.recenter_learning) w.key("recenter_learning").value(*r.recenter_learning);
            break;
        case Cmd::SetGunButtons:
            w.key("map").begin_object();
            for (const auto& kv : r.gun_map) w.key(kv.first).value(kv.second);
            w.end_object();
            break;
        case Cmd::GameStart:
            w.key("profile").value(r.profile);
            break;
        case Cmd::Games:
        case Cmd::GameStop:
            break;
        case Cmd::Controllers:
            if (r.full_ids) w.key("full_ids").value(true);
            break;
        case Cmd::SetController:
            if (!r.name.empty()) w.key("name").value(r.name);
            if (r.new_player) w.key("new_player").value(*r.new_player);
            if (r.reset_settings) w.key("reset_settings").value(true);
            break;
        case Cmd::Blink:
            if (r.blink_ms) w.key("ms").value(*r.blink_ms);
            break;
        case Cmd::Unpair:
            w.key("keep_settings").value(r.keep_settings);
            break;
        case Cmd::Pair:
            w.key("action").value(r.action);
            break;
        case Cmd::Aagames:
            w.key("action").value(r.action.empty() ? "status" : r.action);
            if (!r.game.empty()) w.key("game").value(r.game);
            break;
        case Cmd::Iobridge:
            w.key("action").value(r.action.empty() ? "status" : r.action);
            if (!r.game.empty()) w.key("game").value(r.game);
            if (!r.route.empty()) w.key("route").value(r.route);
            break;
        case Cmd::OutputKey:
            w.key("key").value(r.key_name).key("action").value(r.action.empty() ? "tap" : r.action);
            break;
        case Cmd::Info:
        case Cmd::Settings:
        case Cmd::Diagnostics:
        case Cmd::Recenter:
        case Cmd::ClearTrim:
        case Cmd::Camera:
        case Cmd::Arm:
        case Cmd::Disarm:
        case Cmd::Output: break;
    }
    w.end_object();
    return w.str();
}

}  // namespace ravengun::ipc
