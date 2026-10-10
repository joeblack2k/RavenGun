#include "commands.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include "aagames_service.hpp"
#include "iobridge_service.hpp"
#include "mouse_service.hpp"
#include "build_info.hpp"
#include "camera.hpp"
#include "controllers.hpp"
#include "hid_thread.hpp"
#include "output_service.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/games/games.hpp"
#include "ravengun/output/key_table.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/sysinfo/navcon.hpp"
#include "ravengun/json.hpp"
#include "ravengun/kiosk/kiosk.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/parse.hpp"

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

// Voor welke speler en welke instellingen een per-controller-opdracht geldt.
//  - controller = adres of sleutel: die controller (een Move); live bij zijn speler (als hij er een heeft).
//  - controller = "default": de standaard voor elke controller; live bij elke speler zonder eigen afwijking.
//  - player = N of niets (speler 1): live bij die speler; bewaard bij de Move van die speler, of in de standaard als die speler
//    (nog) geen Move heeft (zo gedraagt een daemon zonder bekende controllers zich als voorheen).
struct Target {
    int player = 1;           // 0 = geen live pijplijn
    std::string address;      // leeg = de standaard
    bool all_defaults = false;
};

bool resolve_target(SharedState& sh, const ipc::Request& r, Target& t, ipc::Error& e) {
    const config::Settings s = sh.settings ? sh.settings->get() : config::Settings{};
    if (r.controller == "default") { t.player = 0; t.address.clear(); t.all_defaults = true; return true; }
    if (!r.controller.empty()) {
        std::string a = config::normalize_address(r.controller);
        if (a.empty()) for (const auto& kv : s.controllers) if (core::controller_key(kv.first) == r.controller) a = kv.first;
        auto it = s.controllers.find(a);
        if (a.empty() || it == s.controllers.end()) { e = {"invalid_argument", "onbekende controller", r.id_json}; return false; }
        if (it->second.type != "move") { e = {"invalid_argument", "dit is geen Move (een Navigation Controller heeft geen eigen pijplijn)", r.id_json}; return false; }
        t.address = a;
        t.player = it->second.removed ? 0 : it->second.player;
        return true;
    }
    t.player = r.player.value_or(1);
    t.address = config::move_for_player(s, t.player);
    return true;
}

// De spelers waar een wijziging live heen gaat: de speler van het doel, of bij "default" elke speler zonder eigen afwijking
// voor deze sleutels.
std::vector<int> live_players(SharedState& sh, const Target& t, const std::vector<std::string>& keys) {
    if (!t.all_defaults) return t.player > 0 ? std::vector<int>{t.player} : std::vector<int>{};
    std::vector<int> out;
    const config::Settings s = sh.settings ? sh.settings->get() : config::Settings{};
    for (int p = 1; p <= kPlayers; ++p) {
        const std::string a = config::move_for_player(s, p);
        bool own = false;
        if (!a.empty())
            for (const auto& k : keys) own = own || s.controllers.at(a).keys.count(k);
        if (!own) out.push_back(p);
    }
    return out;
}

// Bewaart de wijziging in config.json: bij een controller als afwijking (alleen `keys`), anders in de standaard.
config::SaveResult persist(SharedState& sh, const Target& t, const std::vector<std::string>& keys, const std::function<void(config::Settings&)>& fn) {
    if (!sh.settings) return {false, "geen instellingenopslag"};
    return sh.settings->update([&](config::Settings& s) { config::modify_for(s, t.address, keys, fn); });
}

// Een job voor speler p: eerst de speler kiezen.
std::string run_for(SharedState& sh, int player, SharedState::Job j, std::string& err) {
    return run_job(sh, [player, j](HidContext& h) {
        if (!h.select_player(player)) return std::string("!invalid_argument|ongeldige speler");
        return j(h);
    }, err);
}

// Status (HID-thread) plus camera en uitvoer als extra sleutels. Leeg resultaat bij een fout (err gevuld).
std::string status_result(SharedState& sh, bool full_ids, std::string& err, int player = 1) {
    std::string res = run_for(sh, player, [full_ids](HidContext& h) { return h.status_json(full_ids); }, err);
    if (!err.empty()) return {};
    if (sh.camera_status && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"camera\":" + sh.camera_status() + "}";
    }
    if (sh.aagames && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"aagames\":" + sh.aagames->status_json() + "}";
    }
    if (sh.iobridge && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"iobridge\":" + sh.iobridge->status_json() + "}";
    }
    if (sh.output && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"output\":" + sh.output->status_json() + "}";
    }
    if (sh.mouse && !res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"mouse_guns\":" + sh.mouse->status_json() + "}";
    }
    if (!res.empty() && res.back() == '}') {
        res.pop_back();
        res += ",\"navigation\":" + sysinfo::navcon_json(sysinfo::read_navcon()) + "}";
    }
    // Welke controller deze speler heeft (naam, sleutel, afgeschermd adres) en hoeveel spelers er zijn.
    if (!res.empty() && res.back() == '}') {
        const config::Settings s = sh.settings ? sh.settings->get() : config::Settings{};
        const std::string a = config::move_for_player(s, player);
        json::Writer w;
        w.begin_object().key("player").value(player);
        if (a.empty()) w.key("controller").null();
        else {
            const auto& e = s.controllers.at(a);
            w.key("controller").begin_object().key("key").value(core::controller_key(a)).key("address").value(full_ids ? a : redact_mac(a));
            w.key("name").value(e.name).key("overrides").value(config::own_overrides(s, a)).end_object();
        }
        w.key("players").begin_array();
        for (int p = 1; p <= kPlayers; ++p) {
            const std::string pa = config::move_for_player(s, p);
            w.begin_object().key("player").value(p);
            if (pa.empty()) w.key("controller").null(); else w.key("controller").value(s.controllers.at(pa).name);
            w.end_object();
        }
        w.end_array().end_object();
        res.pop_back();
        res += ",\"assignment\":" + w.str() + "}";
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
    Target tg;
    ipc::Error te;
    if (!resolve_target(sh, r, tg, te)) return ipc::error_response(te);
    config::LedChoice c;
    if (!r.led_preset.empty()) {
        if (!config::resolve_led_preset(r.led_preset, c))
            return ipc::error_response({"invalid_argument", "onbekende preset '" + r.led_preset + "' (cyaan, magenta, rood, groen, blauw, wit)", r.id_json});
    } else {
        c = config::resolve_led_rgb(static_cast<int>(*r.led_r), static_cast<int>(*r.led_g), static_cast<int>(*r.led_b));
    }
    std::string err;
    uint8_t cr = static_cast<uint8_t>(c.r), cg = static_cast<uint8_t>(c.g), cb = static_cast<uint8_t>(c.b);
    std::vector<std::string> keys{"led.enabled", "led.preset", "led.r", "led.g", "led.b"};
    if (c.hue_known) { keys.push_back("tracker.hue_deg"); keys.push_back("tracker.hue_tol_deg"); }
    const std::vector<int> players = live_players(sh, tg, keys);
    for (int p : players) {
        run_for(sh, p, [=](HidContext& h) { h.set_led(true, cr, cg, cb); return std::string("1"); }, err);
        if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
    }

    // De tracker zoekt op tint: de tint (en tolerantie) volgt de LED-kleur, ook zonder camera (voor de volgende start).
    // Elke speler heeft een eigen tracker (een bol per kleur).
    double hue = -75, tol = 30;
    bool live = false;
    if (sh.settings) { auto st = config::effective(sh.settings->get(), tg.address); hue = st.tracker.hue_deg; tol = st.tracker.hue_tol_deg; }
    if (sh.camera && sh.camera->enabled()) {
        const int cam_player = players.empty() ? 0 : players.front();
        if (cam_player > 0) { auto tc = sh.camera->tracker(cam_player); hue = tc.hue_deg; tol = tc.hue_tol_deg; }
        if (c.hue_known) {
            for (int p : players) {
                auto tc = sh.camera->tracker(p);
                tc.hue_deg = c.hue_deg;
                tc.hue_tol_deg = c.hue_tol_deg;
                sh.camera->set_tracker(p, tc);
                live = true;
            }
        }
    }
    if (c.hue_known) { hue = c.hue_deg; tol = c.hue_tol_deg; }

    config::SaveResult sr{true, {}};
    if (sh.settings) {
        sr = persist(sh, tg, keys, [&](config::Settings& s) {
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

// Knoppenindeling live wijzigen en bewaren. De gevraagde knoppen worden over de huidige indeling heen gelegd.
std::string handle_set_gun_buttons(SharedState& sh, const ipc::Request& r) {
    Target tg;
    ipc::Error te;
    if (!resolve_target(sh, r, tg, te)) return ipc::error_response(te);
    config::GunButtonSettings g;
    if (sh.settings) g = config::effective(sh.settings->get(), tg.address).gun_buttons;
    for (const auto& kv : r.gun_map) g.map[kv.first] = kv.second;
    // Dezelfde validatie als bij het laden van config.json, vóór er iets live verandert.
    config::Settings probe;
    probe.gun_buttons = g;
    if (std::string bad = config::validate(probe); !bad.empty()) return ipc::error_response({"invalid_argument", bad, r.id_json});
    output::LiveButtonMap m = output::build_live_map(g);
    std::string err;
    std::vector<std::string> keys;
    for (const auto& kv : r.gun_map) keys.push_back("gun_buttons.map." + kv.first);
    const std::vector<int> players = live_players(sh, tg, keys);
    for (int p : players) {
        // Bij "default" krijgt elke speler zijn eigen indeling (met zijn eigen afwijkingen) plus de nieuwe standaard.
        output::LiveButtonMap pm = m;
        if (tg.all_defaults && sh.settings) {
            config::Settings s2 = sh.settings->get();
            config::modify_for(s2, "", {}, [&](config::Settings& s) { for (const auto& kv : r.gun_map) s.gun_buttons.map[kv.first] = kv.second; });
            pm = output::build_live_map(config::effective(s2, config::move_for_player(s2, p)).gun_buttons);
        }
        run_for(sh, p, [pm](HidContext& h) { h.set_gun_buttons(pm); return std::string("1"); }, err);
        if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
        if (sh.output) sh.output->set_layout(p, pm.layout);
    }
    if (sh.output) sh.output->release_all();  // een toets die net een andere functie kreeg mag niet blijven hangen
    config::SaveResult sr{true, {}};
    if (sh.settings) sr = persist(sh, tg, keys, [&](config::Settings& s) { for (const auto& kv : r.gun_map) s.gun_buttons.map[kv.first] = kv.second; });
    json::Writer w;
    w.begin_object().key("map").begin_object();
    for (const auto& kv : m.layout) w.key(kv.first).value(kv.second);
    w.end_object().key("saved").value(sr.ok && sh.settings != nullptr);
    if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
    w.end_object();
    return ipc::ok_response(r.id_json, w.str());
}

std::string game_error(const ipc::Request& r, const games::Result& res) {
    return ipc::error_response({res.code.empty() ? "internal" : res.code, res.message, r.id_json});
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
    if (sh.settings) w.key("values").raw(config::to_json(sh.settings->get(), false, true)); else w.key("values").null();
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
    // Fabrieksinstellingen, voor "standaard herstellen" in de app (zelfde bron als de daemon, niet in de JS nagebootst).
    w.key("defaults").raw(config::to_json(config::Settings{}, false));
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
    if (sh.settings) w.key("settings").raw(config::to_json(sh.settings->get(), false, true)); else w.key("settings").null();
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
std::vector<std::string> filter_keys(const ipc::Request& r) {
    std::vector<std::string> k;
    if (r.filter_enabled) k.push_back("filter.enabled");
    if (r.min_cutoff_hz) k.push_back("filter.min_cutoff_hz");
    if (r.beta) k.push_back("filter.beta");
    if (r.d_cutoff_hz) k.push_back("filter.d_cutoff_hz");
    return k;
}
std::vector<std::string> aim_keys(const ipc::Request& r) {
    std::vector<std::string> k;
    if (r.h_edge_deg) k.push_back("aim.h_edge_deg");
    if (r.v_edge_deg) k.push_back("aim.v_edge_deg");
    if (r.predict_ms) k.push_back("aim.predict_ms");
    return k;
}
std::vector<std::string> rumble_keys(const ipc::Request& r) {
    std::vector<std::string> k;
    if (r.rumble_enabled) k.push_back("rumble.enabled");
    if (r.rumble_strength) k.push_back("rumble.strength");
    if (r.rumble_pulse_ms) k.push_back("rumble.pulse_ms");
    if (r.rumble_repeat_hz) k.push_back("rumble.repeat_hz");
    if (r.rumble_pump_strength) k.push_back("rumble.pump_strength");
    return k;
}
void persist_filter(SharedState& sh, const Target& t, const ipc::Request& r) {
    if (!sh.settings) return;
    persist(sh, t, filter_keys(r), [&](config::Settings& s) {
        if (r.filter_enabled) s.filter.enabled = *r.filter_enabled;
        if (r.min_cutoff_hz) s.filter.min_cutoff_hz = *r.min_cutoff_hz;
        if (r.beta) s.filter.beta = *r.beta;
        if (r.d_cutoff_hz) s.filter.d_cutoff_hz = *r.d_cutoff_hz;
    });
}
void persist_aim(SharedState& sh, const Target& t, const ipc::Request& r) {
    if (!sh.settings) return;
    persist(sh, t, aim_keys(r), [&](config::Settings& s) {
        if (r.h_edge_deg) s.aim.h_edge_deg = *r.h_edge_deg;
        if (r.v_edge_deg) s.aim.v_edge_deg = *r.v_edge_deg;
        if (r.predict_ms) s.aim.predict_ms = *r.predict_ms;
    });
}

std::string context_reply(const ipc::Request& r, const std::string& res, const std::string& err);

json::Writer& write_rumble(json::Writer& w, const output::RumbleConfig& c) {
    return w.key("enabled").value(c.enabled).key("strength").value(c.strength).key("pulse_ms").value(c.pulse_ms)
        .key("repeat_hz").value(c.repeat_hz).key("pump_strength").value(c.pump_strength);
}

std::string handle_set_rumble(SharedState& sh, const ipc::Request& r) {
    Target tg;
    ipc::Error te;
    if (!resolve_target(sh, r, tg, te)) return ipc::error_response(te);
    std::string err;
    ipc::Request rq = r;
    // Zonder live speler (alleen bewaren): de gevraagde waarden op de huidige instelling gecontroleerd.
    const std::vector<int> players = live_players(sh, tg, rumble_keys(r));
    std::string res;
    if (players.empty()) {
        output::RumbleConfig c = sh.settings ? config::effective(sh.settings->get(), tg.address).rumble : output::RumbleConfig{};
        if (rq.rumble_enabled) c.enabled = *rq.rumble_enabled;
        if (rq.rumble_strength) c.strength = static_cast<int>(*rq.rumble_strength);
        if (rq.rumble_pulse_ms) c.pulse_ms = static_cast<int>(*rq.rumble_pulse_ms);
        if (rq.rumble_repeat_hz) c.repeat_hz = *rq.rumble_repeat_hz;
        if (rq.rumble_pump_strength) c.pump_strength = static_cast<int>(*rq.rumble_pump_strength);
        std::string bad = output::validate_rumble(c);
        if (!bad.empty()) return ipc::error_response({"invalid_argument", bad, r.id_json});
        json::Writer w;
        w.begin_object();
        write_rumble(w, c);
        w.end_object();
        res = w.str();
    }
    for (int p : players) {
      std::string one = run_for(sh, p, [rq](HidContext& h) {
        output::RumbleConfig c = h.rumble();
        if (rq.rumble_enabled) c.enabled = *rq.rumble_enabled;
        if (rq.rumble_strength) c.strength = static_cast<int>(*rq.rumble_strength);
        if (rq.rumble_pulse_ms) c.pulse_ms = static_cast<int>(*rq.rumble_pulse_ms);
        if (rq.rumble_repeat_hz) c.repeat_hz = *rq.rumble_repeat_hz;
        if (rq.rumble_pump_strength) c.pump_strength = static_cast<int>(*rq.rumble_pump_strength);
        std::string bad = output::validate_rumble(c);
        if (!bad.empty()) return "!invalid_argument|" + bad;
        h.set_rumble(c);
        json::Writer w;
        w.begin_object();
        write_rumble(w, c);
        w.end_object();
        return w.str();
      }, err);
      if (!err.empty() || (!one.empty() && one[0] == '!')) return context_reply(r, one, err);
      if (res.empty()) res = one;
    }
    config::SaveResult sr{true, {}};
    if (sh.settings) {
        sr = persist(sh, tg, rumble_keys(r), [&](config::Settings& s) {
            if (r.rumble_enabled) s.rumble.enabled = *r.rumble_enabled;
            if (r.rumble_strength) s.rumble.strength = static_cast<int>(*r.rumble_strength);
            if (r.rumble_pulse_ms) s.rumble.pulse_ms = static_cast<int>(*r.rumble_pulse_ms);
            if (r.rumble_repeat_hz) s.rumble.repeat_hz = *r.rumble_repeat_hz;
            if (r.rumble_pump_strength) s.rumble.pump_strength = static_cast<int>(*r.rumble_pump_strength);
        });
    }
    // Het resultaat: de actieve instelling plus of hij bewaard is.
    std::string out = res;
    if (!out.empty() && out.back() == '}') {
        out.pop_back();
        out += std::string(",\"saved\":") + (sr.ok && sh.settings ? "true" : "false") + "}";
    }
    return ipc::ok_response(r.id_json, out);
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

// Naam, spelernummer of het wissen van de afwijkingen van een controller. Een spelernummer dat al bezet is, wordt geruild.
std::string handle_set_controller(SharedState& sh, const ipc::Request& r) {
    if (!sh.settings) return ipc::error_response({"unavailable", "geen instellingenopslag", r.id_json});
    const config::Settings cur = sh.settings->get();
    std::string a = config::normalize_address(r.controller);
    if (a.empty()) for (const auto& kv : cur.controllers) if (core::controller_key(kv.first) == r.controller) a = kv.first;
    auto it = cur.controllers.find(a);
    if (a.empty() || it == cur.controllers.end()) return ipc::error_response({"invalid_argument", "onbekende controller", r.id_json});
    if (it->second.removed) return ipc::error_response({"unavailable", "deze controller is verwijderd; koppel hem eerst opnieuw", r.id_json});
    std::string swapped;
    config::SaveResult sr = sh.settings->update([&](config::Settings& s) {
        config::ControllerEntry& e = s.controllers.at(a);
        if (!r.name.empty()) e.name = r.name;
        if (r.reset_settings) {
            config::clear_overrides(e);
            config::apply_player_defaults(s, a, 1, e.player);  // de spelerkleur en Start-toets horen bij de speler
        }
        if (r.new_player && *r.new_player != e.player) {
            const int old = e.player, to = *r.new_player;
            if (e.type == "move") {
                const std::string other = config::move_for_player(s, to);
                e.player = to;
                if (!other.empty()) {
                    s.controllers.at(other).player = old;
                    swapped = other;
                }
                config::apply_player_defaults(s, a, old > 0 ? old : 1, to);
                if (!other.empty()) config::apply_player_defaults(s, other, to, old > 0 ? old : 1);
            } else {
                e.player = to;
            }
        }
    });
    if (!sr.ok && sr.message.rfind("ongeldige instelling", 0) == 0) return ipc::error_response({"invalid_argument", sr.message, r.id_json});
    // De HID-thread leest de nieuwe toewijzing en instellingen (een Move die van speler wisselt, verbindt even opnieuw).
    std::string err;
    run_job(sh, [](HidContext& h) { h.reconfigure(); return std::string("1"); }, err);
    const config::Settings now = sh.settings->get();
    const auto& e = now.controllers.at(a);
    json::Writer w;
    w.begin_object().key("key").value(core::controller_key(a)).key("name").value(e.name).key("player").value(e.player);
    w.key("overrides").value(config::own_overrides(now, a));
    if (swapped.empty()) w.key("swapped_with").null();
    else w.key("swapped_with").value(now.controllers.at(swapped).name);
    w.key("saved").value(sr.ok);
    if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
    w.end_object();
    return ipc::ok_response(r.id_json, w.str());
}

}  // namespace

std::string execute_command(SharedState& sh, const ipc::Request& r) {
    std::string err;
    // Per-controller-opdrachten: voor welke speler/controller (zie Target).
    Target tg;
    if (ipc::takes_selector(r.cmd)) {
        ipc::Error te;
        if (!resolve_target(sh, r, tg, te)) return ipc::error_response(te);
    }
    const int pl = tg.player > 0 ? tg.player : 1;
    auto no_player = [&]() {
        return ipc::error_response({"unavailable", "deze controller heeft geen speler (verwijderd of geen vrije plek)", r.id_json});
    };
    switch (r.cmd) {
        case ipc::Cmd::Status: {
            if (tg.player == 0 && !tg.all_defaults) return no_player();
            std::string res = status_result(sh, r.full_ids, err, pl);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Camera: {
            if (!sh.camera_status) return ipc::error_response({"unavailable", "geen camera-service", r.id_json});
            return ipc::ok_response(r.id_json, sh.camera_status());
        }
        case ipc::Cmd::Sample: {
            Snapshot s = sh.get(pl);
            json::Writer w;
            core::write_sample_json(w, s.sample, r.full_ids);
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Recenter: {
            if (tg.player == 0) return no_player();
            std::string res = run_for(sh, pl, [](HidContext& h) { return std::string(h.recenter() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1")
                return ipc::error_response({"unavailable", "geen geldige, actuele oriëntatie: is de Move verbonden?", r.id_json});
            Snapshot s = sh.get(pl);
            json::Writer w;
            w.begin_object().key("recentered").value(true).key("player").value(pl).key("sample");
            core::write_sample_json(w, s.sample, false);
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::ClearTrim: {
            if (tg.player == 0) return no_player();
            std::string res = run_for(sh, pl, [](HidContext& h) { return std::string(h.clear_trim() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1") return ipc::error_response({"unavailable", "yaw-trim wissen niet beschikbaar", r.id_json});
            return ipc::ok_response(r.id_json, "{\"trim_cleared\":true}");
        }
        case ipc::Cmd::Watch:
            return ipc::error_response({"bad_request", "watch hoort bij een IPC-verbinding", r.id_json});
        case ipc::Cmd::SetFilter: {
            ipc::Request rq = r;
            std::string res;
            for (int p : live_players(sh, tg, filter_keys(r))) {
                std::string one = run_for(sh, p, [rq](HidContext& h) {
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
                if (res.empty()) res = one;
            }
            persist_filter(sh, tg, r);
            if (res.empty()) {  // geen live speler: de bewaarde waarden
                const auto f = sh.settings ? config::effective(sh.settings->get(), tg.address).filter : config::FilterSettings{};
                json::Writer w;
                w.begin_object().key("enabled").value(f.enabled).key("min_cutoff_hz").value(f.min_cutoff_hz)
                    .key("beta").value(f.beta).key("d_cutoff_hz").value(f.d_cutoff_hz).end_object();
                res = w.str();
            }
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::SetAim: {
            ipc::Request rq = r;
            std::string res;
            for (int p : live_players(sh, tg, aim_keys(r))) {
                std::string one = run_for(sh, p, [rq](HidContext& h) {
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
                if (res.empty()) res = one;
            }
            persist_aim(sh, tg, r);
            if (res.empty()) {
                const auto a = sh.settings ? config::effective(sh.settings->get(), tg.address).aim : config::AimSettings{};
                json::Writer w;
                w.begin_object().key("h_edge_deg").value(a.h_edge_deg).key("v_edge_deg").value(a.v_edge_deg).key("predict_ms").value(a.predict_ms).end_object();
                res = w.str();
            }
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
        case ipc::Cmd::SetRumble: return handle_set_rumble(sh, r);
        case ipc::Cmd::RumbleTest: {
            if (tg.player == 0) return no_player();
            const int ms = r.rumble_ms ? static_cast<int>(*r.rumble_ms) : 300;
            std::string res = run_for(sh, pl, [ms](HidContext& h) { return h.rumble_test(ms); }, err);
            if (!err.empty() || (!res.empty() && res[0] == '!')) return context_reply(r, res, err);
            return ipc::ok_response(r.id_json, "{\"pulse_ms\":" + std::to_string(ms) + "}");
        }
        case ipc::Cmd::Blink: {
            if (tg.player == 0) return no_player();
            const int ms = r.blink_ms ? static_cast<int>(*r.blink_ms) : 3000;
            std::string res = run_for(sh, pl, [ms](HidContext& h) { return h.blink(ms); }, err);
            if (!err.empty() || (!res.empty() && res[0] == '!')) return context_reply(r, res, err);
            return ipc::ok_response(r.id_json, "{\"blinking_ms\":" + std::to_string(ms) + ",\"player\":" + std::to_string(pl) + "}");
        }
        case ipc::Cmd::SetLed: return handle_set_led(sh, r);
        case ipc::Cmd::SetCamera: return handle_set_camera(sh, r);
        case ipc::Cmd::SetWeb: return handle_set_web(sh, r);
        case ipc::Cmd::SetButtons: return handle_set_buttons(sh, r);
        case ipc::Cmd::CalibrateUi: return handle_calibrate_ui(sh, r);
        case ipc::Cmd::SetFusion: {
            core::FusionMode m = core::FusionMode::Mahony;
            if (!r.mode.empty()) core::parse_fusion_mode(r.mode.c_str(), m);
            const bool set_mode = !r.mode.empty();
            const std::optional<bool> learn = r.recenter_learning;
            std::vector<std::string> keys;
            if (set_mode) keys.push_back("fusion.mode");
            if (learn) keys.push_back("fusion.recenter_learning");
            std::string res;
            for (int p : live_players(sh, tg, keys)) {
                std::string one = run_for(sh, p, [m, set_mode, learn](HidContext& h) {
                    if (set_mode) h.set_fusion(m);
                    if (learn) h.set_recenter_learning(*learn);
                    return std::string(core::to_string(h.fusion())) + (h.recenter_learning() ? "|1" : "|0");
                }, err);
                if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
                if (res.empty()) res = one;
            }
            config::SaveResult sr{true, {}};
            if (sh.settings) sr = persist(sh, tg, keys, [&](config::Settings& s) {
                if (set_mode) s.fusion.mode = r.mode;
                if (learn) s.fusion.recenter_learning = *learn;
            });
            if (res.empty()) {
                const auto f = sh.settings ? config::effective(sh.settings->get(), tg.address).fusion : config::FusionSettings{};
                res = f.mode + (f.recenter_learning ? "|1" : "|0");
            }
            const size_t bar = res.find('|');
            const std::string mode_now = res.substr(0, bar);
            const bool learn_now = bar != std::string::npos && res.substr(bar + 1) == "1";
            json::Writer w;
            w.begin_object().key("mode").value(mode_now).key("recenter_learning").value(learn_now)
                .key("saved").value(sr.ok && sh.settings != nullptr);
            if (sr.ok) w.key("save_error").null(); else w.key("save_error").value(tilde(sr.message));
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::SetGunButtons: return handle_set_gun_buttons(sh, r);
        case ipc::Cmd::Games: {
            if (!sh.games) return ipc::error_response({"unavailable", "games zijn niet beschikbaar in deze daemon", r.id_json});
            return ipc::ok_response(r.id_json, sh.games->list_json());
        }
        case ipc::Cmd::GameStart: {
            if (!sh.games) return ipc::error_response({"unavailable", "games zijn niet beschikbaar in deze daemon", r.id_json});
            games::Result res = sh.games->start(r.profile, r.launch);
            if (!res.ok) return game_error(r, res);
            return ipc::ok_response(r.id_json, sh.games->status_json());
        }
        case ipc::Cmd::GameStop: {
            if (!sh.games) return ipc::error_response({"unavailable", "games zijn niet beschikbaar in deze daemon", r.id_json});
            games::Result res = sh.games->stop();
            if (!res.ok) return game_error(r, res);
            return ipc::ok_response(r.id_json, sh.games->status_json());
        }
        case ipc::Cmd::Aagames: {
            if (!sh.aagames) return ipc::error_response({"unavailable", "de Unity-route is niet beschikbaar in deze daemon", r.id_json});
            if (r.action == "start") {
                if (!sh.output) return ipc::error_response({"unavailable", "geen uitvoerservice", r.id_json});
                const std::string msg = sh.aagames->start(r.game);
                if (!msg.empty()) return ipc::error_response({"invalid_argument", msg, r.id_json});
            } else if (r.action == "stop") {
                sh.aagames->stop();
            }
            return ipc::ok_response(r.id_json, sh.aagames->status_json());
        }
        case ipc::Cmd::Iobridge: {
            if (!sh.iobridge) return ipc::error_response({"unavailable", "de IO-brug is niet beschikbaar in deze daemon", r.id_json});
            if (r.action == "route") {
                ravengun::json::Writer w;
                w.begin_object().key("game").value(r.game).key("route").value(sh.iobridge->route_for(r.game)).end_object();
                return ipc::ok_response(r.id_json, w.str());
            }
            if (r.action == "start") {
                if (!sh.output) return ipc::error_response({"unavailable", "geen uitvoerservice", r.id_json});
                const std::string msg = sh.iobridge->start(r.game, r.route);
                if (!msg.empty()) return ipc::error_response({"invalid_argument", msg, r.id_json});
            } else if (r.action == "stop") {
                sh.iobridge->stop();
                if (sh.aagames) sh.aagames->stop();
            }
            return ipc::ok_response(r.id_json, sh.iobridge->status_json());
        }
        case ipc::Cmd::OutputKey: {
            if (!sh.output) return ipc::error_response({"unavailable", "geen uitvoerservice", r.id_json});
            const int code = output::key_code(r.key_name);
            if (code < 0) return ipc::error_response({"invalid_argument", "onbekende toets '" + r.key_name + "'", r.id_json});
            return output_reply(r, sh.output->key(r.player.value_or(1), code, r.action), sh);
        }
        case ipc::Cmd::Mice: {
            if (!sh.mouse) return ipc::error_response({"unavailable", "muisservice is niet beschikbaar in deze daemon", r.id_json});
            return ipc::ok_response(r.id_json, sh.mouse->mice_json());
        }
        case ipc::Cmd::SetMouseGun: {
            if (!sh.mouse) return ipc::error_response({"unavailable", "muisservice is niet beschikbaar in deze daemon", r.id_json});
            std::string code, msg;
            std::optional<int> counts;
            if (r.counts_per_screen) counts = static_cast<int>(*r.counts_per_screen);
            if (!sh.mouse->assign(r.player.value_or(1), r.mouse.value_or(""), counts, code, msg))
                return ipc::error_response({code, msg, r.id_json});
            return ipc::ok_response(r.id_json, sh.mouse->assignment_json(r.player.value_or(1)));
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
            // Een opname is per controller: start neemt de Move van deze speler op; stop/status/mark gelden voor de lopende opname.
            if (r.action == "start" && tg.player == 0) return no_player();
            ipc::Request rq = r;
            std::string res = run_for(sh, pl, [rq](HidContext& h) { return h.record(rq); }, err);
            if (rq.action == "start" && err.empty() && !res.empty() && res[0] != '!') sh.recording_player.store(pl);
            return context_reply(r, res, err);
        }
        case ipc::Cmd::CalibMag: {
            if (tg.player == 0) return no_player();
            ipc::Request rq = r;
            std::string res = run_for(sh, pl, [rq](HidContext& h) { return h.calib_mag(rq); }, err);
            return context_reply(r, res, err);
        }
        case ipc::Cmd::Controllers: {
            if (!sh.controllers) return ipc::error_response({"unavailable", "controllerbeheer is niet beschikbaar in deze daemon", r.id_json});
            return ipc::ok_response(r.id_json, sh.controllers->list_json(r.full_ids));
        }
        case ipc::Cmd::SetController: return handle_set_controller(sh, r);
        case ipc::Cmd::Unpair: {
            if (!sh.controllers) return ipc::error_response({"unavailable", "controllerbeheer is niet beschikbaar in deze daemon", r.id_json});
            std::string code, msg;
            std::string res = sh.controllers->unpair(r.controller, r.keep_settings, code, msg);
            if (!code.empty()) return ipc::error_response({code, msg, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Pair: {
            if (!sh.controllers) return ipc::error_response({"unavailable", "controllerbeheer is niet beschikbaar in deze daemon", r.id_json});
            std::string code, msg;
            std::string res = sh.controllers->pair(r.action, code, msg);
            if (!code.empty()) return ipc::error_response({code, msg, r.id_json});
            return ipc::ok_response(r.id_json, res);
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
