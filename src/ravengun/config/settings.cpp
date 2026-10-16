#include "ravengun/config/settings.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/config/led_color.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/magnetometer.hpp"  // controller_key
#include "ravengun/parse.hpp"               // redact_mac
#include "ravengun/json.hpp"
#include "ravengun/output/key_table.hpp"
#include "ravengun/sysinfo/mice.hpp"   // mouse_id_valid (ook voor gamepad-ids)

namespace ravengun::config {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxFileBytes = 64 * 1024;

std::string fmt(const char* f, const char* a, double lo, double hi) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, lo, hi);
    return buf;
}

// Leest typevaste velden uit een object; houdt de eerste fout vast.
struct Section {
    const json::Value& o;
    std::string name;
    std::string& err;
    std::vector<std::string>* warnings;

    std::string full(const char* key) const { return name + "." + key; }
    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }

    bool number(const char* key, double lo, double hi, double& out, bool integer) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_number() || !std::isfinite(v->num) || (integer && std::floor(v->num) != v->num))
            return fail(full(key) + " moet " + (integer ? "een geheel getal" : "een getal") + " zijn");
        if (!(v->num >= lo && v->num <= hi)) return fail(fmt("%s moet tussen %g en %g liggen", full(key).c_str(), lo, hi));
        out = v->num;
        return true;
    }
    bool integer(const char* key, int lo, int hi, int& out) {
        double d = out;
        if (!number(key, lo, hi, d, true)) return false;
        out = static_cast<int>(d);
        return true;
    }
    bool boolean(const char* key, bool& out) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_bool()) return fail(full(key) + " moet true of false zijn");
        out = v->b;
        return true;
    }
    bool string(const char* key, std::string& out) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_string() || v->str.size() > 32) return fail(full(key) + " moet een korte tekst zijn");
        out = v->str;
        return true;
    }
    void unknown(std::initializer_list<const char*> known) {
        if (!warnings) return;
        for (const auto& kv : o.obj) {
            bool k = false;
            for (const char* n : known) if (kv.first == n) k = true;
            if (!k) warnings->push_back("onbekende sleutel '" + name + "." + kv.first + "' genegeerd");
        }
    }
};

bool read_file(const std::string& path, std::string& out, std::string& err, bool& missing) {
    missing = false;
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) { missing = true; err = "bestaat niet"; return false; }
        err = std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); err = "geen gewoon bestand"; return false; }
    if (st.st_uid != ::getuid()) { ::close(fd); err = "niet van de eigen gebruiker"; return false; }
    if (st.st_size > static_cast<off_t>(kMaxFileBytes)) { ::close(fd); err = "groter dan 64 KiB"; return false; }
    out.clear();
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; err = std::strerror(errno); ::close(fd); return false; }
        if (n == 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxFileBytes) { ::close(fd); err = "groter dan 64 KiB"; return false; }
    }
    ::close(fd);
    return true;
}

}  // namespace

const std::vector<CameraMode>& camera_modes() {
    static const std::vector<CameraMode> m = {{640, 480, 60}, {320, 240, 125}, {320, 240, 187}};
    return m;
}
bool camera_mode_valid(int w, int h, int fps) {
    for (const auto& m : camera_modes()) if (m.width == w && m.height == h && m.fps == fps) return true;
    return false;
}
std::string camera_mode_string(int w, int h, int fps) {
    return std::to_string(w) + "x" + std::to_string(h) + "@" + std::to_string(fps);
}
bool parse_camera_mode(const std::string& s, CameraMode& out) {
    int w = 0, h = 0, f = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%dx%d@%d%c", &w, &h, &f, &tail) != 3) return false;
    if (!camera_mode_valid(w, h, f)) return false;
    out = {w, h, f};
    return true;
}

const std::vector<std::string>& sharp_shooter_roles() {
    static const std::vector<std::string> r{"trekker", "pomp", "driehoek", "cirkel", "kruis", "vierkant", "start", "select", "move", "ps"};
    return r;
}

bool move_button_name_valid(const std::string& name) {
    for (const auto& b : core::kButtonNames)
        if (name == b.name) return true;
    return false;
}

const std::vector<std::string>& gun_map_buttons() {
    static const std::vector<std::string> b{"triangle", "circle", "cross", "square", "select", "start", "move", "pump", "ss_rl", "ss_fire", "sel1", "sel2", "sel3",
                                            "nav_cross", "nav_circle", "nav_l1", "nav_l2", "nav_l3", "nav_ps", "nav_up", "nav_down", "nav_left", "nav_right",
                                            "nav_stick_up", "nav_stick_down", "nav_stick_left", "nav_stick_right"};
    return b;
}

std::string check_gun_action(const std::string& action, std::string& canon) {
    if (action.empty() || action == "none") { canon = "none"; return {}; }
    for (const char* a : {"reload", "grenade", "aux1", "aux2", "recenter"})
        if (action == a) { canon = action; return {}; }
    if (action.compare(0, 4, "key:") == 0) {
        int code = output::key_code(action.substr(4));
        if (code < 0) return "onbekende toets '" + action.substr(4) + "' (zie de toetsnamen in docs/architecture.md)";
        canon = std::string("key:") + output::key_name(code);
        return {};
    }
    return "onbekende actie '" + action + "' (reload, grenade, aux1, aux2, recenter, key:<naam> of none)";
}

std::string validate_controllers(const Settings& s);

// ---------------- Per-controller-instellingen ----------------

namespace {

using Copier = void (*)(ControllerValues&, const ControllerValues&);
struct KeyCopier { const char* key; Copier fn; };

#define RG_FIELD(sec, f) {#sec "." #f, [](ControllerValues& d, const ControllerValues& s) { d.sec.f = s.sec.f; }}
const std::vector<KeyCopier>& copiers() {
    static const std::vector<KeyCopier> c = {
        RG_FIELD(led, enabled), RG_FIELD(led, preset), RG_FIELD(led, r), RG_FIELD(led, g), RG_FIELD(led, b),
        RG_FIELD(tracker, hue_deg), RG_FIELD(tracker, hue_tol_deg), RG_FIELD(tracker, min_chroma),
        RG_FIELD(filter, enabled), RG_FIELD(filter, min_cutoff_hz), RG_FIELD(filter, beta), RG_FIELD(filter, d_cutoff_hz),
        RG_FIELD(aim, h_edge_deg), RG_FIELD(aim, v_edge_deg), RG_FIELD(aim, predict_ms),
        RG_FIELD(fusion, mode), RG_FIELD(fusion, vqf_bias), RG_FIELD(fusion, recenter_learning),
        RG_FIELD(rumble, enabled), RG_FIELD(rumble, strength), RG_FIELD(rumble, pulse_ms), RG_FIELD(rumble, repeat_hz),
        RG_FIELD(rumble, pump_strength),
        RG_FIELD(gun_buttons, trigger_on), RG_FIELD(gun_buttons, trigger_off),
    };
    return c;
}
#undef RG_FIELD

const char* const kMapPrefix = "gun_buttons.map.";

bool is_map_key(const std::string& k, std::string& button) {
    if (k.rfind(kMapPrefix, 0) != 0) return false;
    button = k.substr(std::strlen(kMapPrefix));
    const auto& bs = gun_map_buttons();
    return std::find(bs.begin(), bs.end(), button) != bs.end();
}

bool same_led(const LedSettings& a, const LedSettings& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Tint en tolerantie bij een spelerkleur (gemeten tinten; geel nominaal met een smalle sector).
void tracker_for_led(const LedSettings& l, TrackerSettings& t) {
    if (l.preset == "custom" && l.r == 255 && l.g == 255 && l.b == 0) { t.hue_deg = 171; t.hue_tol_deg = 20; return; }
    LedChoice c;
    if (resolve_led_preset(l.preset, c) && c.hue_known) { t.hue_deg = c.hue_deg; t.hue_tol_deg = c.hue_tol_deg; }
}

}  // namespace

bool controller_key_valid(const std::string& key) {
    std::string b;
    if (is_map_key(key, b)) return true;
    for (const auto& c : copiers()) if (key == c.key) return true;
    return false;
}

bool copy_controller_value(const std::string& key, ControllerValues& dst, const ControllerValues& src) {
    std::string b;
    if (is_map_key(key, b)) {
        auto it = src.gun_buttons.map.find(b);
        dst.gun_buttons.map[b] = it == src.gun_buttons.map.end() ? std::string("none") : it->second;
        return true;
    }
    for (const auto& c : copiers())
        if (key == c.key) { c.fn(dst, src); return true; }
    return false;
}

Settings effective(const Settings& s, const std::string& address) {
    Settings e = s;
    auto it = s.controllers.find(normalize_address(address));
    if (it == s.controllers.end()) return e;
    for (const auto& k : it->second.keys) copy_controller_value(k, e, it->second.values);
    return e;
}

bool modify_for(Settings& s, const std::string& address, const std::vector<std::string>& keys, const std::function<void(Settings&)>& fn) {
    if (address.empty()) { fn(s); return true; }
    const std::string a = normalize_address(address);
    auto it = s.controllers.find(a);
    if (it == s.controllers.end()) return false;
    Settings e = effective(s, a);
    fn(e);
    for (const auto& k : keys) {
        if (!controller_key_valid(k)) continue;
        copy_controller_value(k, it->second.values, e);
        it->second.keys.insert(k);
    }
    return true;
}

void clear_overrides(ControllerEntry& e, const std::vector<std::string>& prefixes) {
    if (prefixes.empty()) { e.keys.clear(); e.values = ControllerValues{}; return; }
    for (auto it = e.keys.begin(); it != e.keys.end();) {
        bool hit = false;
        for (const auto& p : prefixes) if (it->rfind(p, 0) == 0) hit = true;
        it = hit ? e.keys.erase(it) : std::next(it);
    }
}

bool address_valid(const std::string& a) {
    if (a.size() != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        char c = a[i];
        if (i % 3 == 2) { if (c != ':') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    std::string l = a;
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l != "00:00:00:00:00:00" && l != "ff:ff:ff:ff:ff:ff";
}

std::string normalize_address(const std::string& a) {
    if (!address_valid(a)) return {};
    std::string r = a;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool controller_name_valid(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    for (unsigned char c : n) if (c < 0x20 || c == 0x7f) return false;
    return true;
}

LedSettings player_led(int player) {
    LedSettings l;
    l.enabled = true;
    switch (player) {
        case 2: l.preset = "magenta"; l.r = 255; l.g = 0; l.b = 255; break;
        case 3: l.preset = "groen"; l.r = 0; l.g = 255; l.b = 0; break;
        case 4: l.preset = "custom"; l.r = 255; l.g = 255; l.b = 0; break;  // geel: niet gemeten, zie docs/decisions.md
        default: l.preset = "cyaan"; l.r = 0; l.g = 255; l.b = 255; break;
    }
    return l;
}

std::string player_start_key(int player) { return std::to_string(player >= 1 && player <= kMaxPlayers ? player : 1); }

void apply_player_defaults(Settings& s, const std::string& address, int from, int to) {
    const std::string a = normalize_address(address);
    auto it = s.controllers.find(a);
    if (it == s.controllers.end() || from == to) return;
    ControllerEntry& e = it->second;
    if (e.type != "move") return;
    const Settings eff = effective(s, a);
    // LED: alleen als hij nog de kleur van `from` had (speler 1 of geen speler: de globale standaard).
    const LedSettings from_led = from >= 2 ? player_led(from) : s.led;
    if (same_led(eff.led, from_led)) {
        clear_overrides(e, {"led.", "tracker.hue_deg", "tracker.hue_tol_deg"});
        if (to >= 2) {
            e.values.led = player_led(to);
            tracker_for_led(e.values.led, e.values.tracker);
            for (const char* k : {"led.enabled", "led.preset", "led.r", "led.g", "led.b", "tracker.hue_deg", "tracker.hue_tol_deg"}) e.keys.insert(k);
        }
    }
    // Start-toets: knoppen die de Start-toets van `from` gaven, geven nu die van `to` (Coin blijft gelijk).
    const std::string old_key = "key:" + player_start_key(from), new_key = "key:" + player_start_key(to);
    if (old_key == new_key) return;
    for (const auto& b : gun_map_buttons()) {
        auto m = eff.gun_buttons.map.find(b);
        if (m == eff.gun_buttons.map.end() || m->second != old_key) continue;
        const std::string k = std::string(kMapPrefix) + b;
        auto d = s.gun_buttons.map.find(b);
        if (d != s.gun_buttons.map.end() && d->second == new_key) { e.keys.erase(k); continue; }
        e.values.gun_buttons.map[b] = new_key;
        e.keys.insert(k);
    }
}

int free_player(const Settings& s, const std::string& except) {
    const std::string ex = normalize_address(except);
    for (int p = 1; p <= kMaxPlayers; ++p) {
        bool used = false;
        for (const auto& kv : s.controllers)
            if (kv.first != ex && kv.second.type == "move" && !kv.second.removed && kv.second.player == p) used = true;
        if (!used) return p;
    }
    return 0;
}

std::string move_for_player(const Settings& s, int player) {
    if (player < 1) return {};
    for (const auto& kv : s.controllers)
        if (kv.second.type == "move" && !kv.second.removed && kv.second.player == player) return kv.first;
    return {};
}

int own_overrides(const Settings& s, const std::string& address) {
    const std::string a = normalize_address(address);
    auto it = s.controllers.find(a);
    if (it == s.controllers.end()) return 0;
    // Wat de controller zou hebben met alleen de spelergebonden standaarden.
    Settings base = s;
    ControllerEntry& be = base.controllers.at(a);
    clear_overrides(be);
    if (be.type == "move" && be.player > 1) apply_player_defaults(base, a, 1, be.player);
    const Settings mine = effective(s, a), plain = effective(base, a);
    int n = 0;
    for (const auto& k : it->second.keys) {
        Settings x, y;
        copy_controller_value(k, x, mine);
        copy_controller_value(k, y, plain);
        x.controllers.clear();
        y.controllers.clear();
        if (to_json(x, false) != to_json(y, false)) ++n;
    }
    return n;
}

std::string default_controller_name(const Settings& s, const std::string& type) {
    if (type == "navigation") {
        int n = 0;
        for (const auto& kv : s.controllers) if (kv.second.type == "navigation" && !kv.second.removed) ++n;
        return n ? "Navigation Controller " + std::to_string(n + 1) : "Navigation Controller";
    }
    for (int i = 1; i < 100; ++i) {
        const std::string name = "Move " + std::to_string(i);
        bool used = false;
        for (const auto& kv : s.controllers) if (kv.second.name == name) used = true;
        if (!used) return name;
    }
    return "Move";
}

int register_controller(Settings& s, const std::string& address, const std::string& type, const std::string& model, int64_t now_unix,
                        bool* added) {
    if (added) *added = false;
    const std::string a = normalize_address(address);
    if (a.empty() || (type != "move" && type != "navigation")) return 0;
    auto it = s.controllers.find(a);
    if (it != s.controllers.end() && !it->second.removed) {
        if (!model.empty()) it->second.model = model;
        if (now_unix) it->second.last_seen_unix = now_unix;
        return it->second.player;
    }
    if (it == s.controllers.end()) {
        ControllerEntry e;
        e.type = type;
        e.model = model;
        e.added_unix = now_unix;
        e.last_seen_unix = now_unix;
        e.name = default_controller_name(s, type);
        it = s.controllers.emplace(a, std::move(e)).first;
    } else {  // teruggekomen na verwijderen met bewaarde instellingen
        it->second.removed = false;
        it->second.type = type;
        if (!model.empty()) it->second.model = model;
        it->second.last_seen_unix = now_unix;
    }
    if (added) *added = true;
    if (type == "navigation") {
        if (it->second.player == 0) it->second.player = 1;
        return it->second.player;
    }
    const int old = it->second.player;
    it->second.player = 0;
    const int p = free_player(s, a);
    it->second.player = p;
    // Bewaarde instellingen van een eerdere speler: de spelergebonden standaarden meeverhuizen.
    if (p > 0) apply_player_defaults(s, a, old > 0 ? old : 1, p);
    return p;
}

std::string validate_controllers(const Settings& s) {
    std::set<int> players;
    for (const auto& kv : s.controllers) {
        const std::string where = "controllers." + redact_mac(kv.first);
        if (normalize_address(kv.first) != kv.first) return where + ": ongeldig adres (verwacht aa:bb:cc:dd:ee:ff in kleine letters)";
        const ControllerEntry& e = kv.second;
        if (e.type != "move" && e.type != "navigation") return where + ".type moet move of navigation zijn";
        if (!e.name.empty() && !controller_name_valid(e.name)) return where + ".name moet 1 tot 32 gewone tekens zijn";
        if (e.player < 0 || e.player > kMaxPlayers) return where + ".player moet tussen 0 en 4 liggen";
        if (e.type == "move" && !e.removed && e.player > 0) {
            if (players.count(e.player)) return where + ".player: speler " + std::to_string(e.player) + " heeft al een Move";
            players.insert(e.player);
        }
        for (const auto& k : e.keys)
            if (!controller_key_valid(k)) return where + ": onbekende instelling '" + k + "'";
        Settings eff = effective(s, kv.first);
        eff.controllers.clear();
        if (std::string m = validate(eff); !m.empty()) return where + ": " + m;
    }
    return {};
}


std::string validate(const Settings& s) {
    auto in = [](double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    static const char* kPresets[] = {"cyaan", "magenta", "rood", "groen", "blauw", "wit", "custom"};
    bool pr = false;
    for (const char* p : kPresets) if (s.led.preset == p) pr = true;
    if (!pr) return "led.preset is onbekend (cyaan, magenta, rood, groen, blauw, wit of custom)";
    for (int c : {s.led.r, s.led.g, s.led.b}) if (c < 0 || c > 255) return "led.r/g/b moeten tussen 0 en 255 liggen";
    if (!in(s.tracker.hue_deg, -180, 180)) return "tracker.hue_deg moet tussen -180 en 180 liggen";
    if (!in(s.tracker.hue_tol_deg, 1, 90)) return "tracker.hue_tol_deg moet tussen 1 en 90 liggen";
    if (s.tracker.min_chroma < 1 || s.tracker.min_chroma > 180) return "tracker.min_chroma moet tussen 1 en 180 liggen";
    if (!camera_mode_valid(s.camera.width, s.camera.height, s.camera.fps)) return "camera.mode is geen toegestane modus (640x480@60, 320x240@125, 320x240@187)";
    if (s.camera.exposure < 0 || s.camera.exposure > 255) return "camera.exposure moet tussen 0 en 255 liggen";
    if (s.camera.gain < 0 || s.camera.gain > 63) return "camera.gain moet tussen 0 en 63 liggen";
    if (!in(s.filter.min_cutoff_hz, 0.001, 100)) return "filter.min_cutoff_hz moet tussen 0,001 en 100 liggen";
    if (!in(s.filter.beta, 0, 1000)) return "filter.beta moet tussen 0 en 1000 liggen";
    if (!in(s.filter.d_cutoff_hz, 0.001, 100)) return "filter.d_cutoff_hz moet tussen 0,001 en 100 liggen";
    if (!in(s.aim.h_edge_deg, 1, 89) || !in(s.aim.v_edge_deg, 1, 89)) return "aim.h_edge_deg en aim.v_edge_deg moeten tussen 1 en 89 liggen";
    if (!in(s.aim.predict_ms, 0, 200)) return "aim.predict_ms moet tussen 0 en 200 liggen";
    if (!(s.output.clock_hz == 0 || in(s.output.clock_hz, 20, 1000))) return "output.clock_hz moet 0 (uit) zijn of tussen 20 en 1000 liggen";
    if (!in(s.output.extrapolate_ms, 0, 100)) return "output.extrapolate_ms moet tussen 0 en 100 liggen";
    if (s.web.port < 1 || s.web.port > 65535) return "web.port moet tussen 1 en 65535 liggen";
    for (const auto& kv : s.sharp_shooter.buttons) {
        const auto& roles = sharp_shooter_roles();
        if (std::find(roles.begin(), roles.end(), kv.first) == roles.end()) return "sharp_shooter.buttons." + kv.first + " is een onbekende knop";
        if (!kv.second.empty() && !move_button_name_valid(kv.second))
            return "sharp_shooter.buttons." + kv.first + " is geen Move-knop (triangle, circle, cross, square, select, start, ps, move of t)";
    }
    if (s.fusion.mode != "mahony" && s.fusion.mode != "mahony_mag" && s.fusion.mode != "vqf" && s.fusion.mode != "vqf_mag")
        return "fusion.mode moet mahony, mahony_mag, vqf of vqf_mag zijn";
    if (s.fusion.vqf_bias != "external" && s.fusion.vqf_bias != "own") return "fusion.vqf_bias moet external of own zijn";
    if (s.sharp_shooter.extension != "auto" && s.sharp_shooter.extension != "sharp_shooter" && s.sharp_shooter.extension != "none")
        return "sharp_shooter.extension moet auto, sharp_shooter of none zijn";
    if (std::string m = output::validate_rumble(s.rumble); !m.empty()) return m;
    const auto& g = s.gun_buttons;
    if (g.trigger_on < 1 || g.trigger_on > 255 || g.trigger_off < 0 || g.trigger_off >= g.trigger_on)
        return "gun_buttons: 0 <= trigger_off < trigger_on <= 255";
    for (const auto& kv : g.map) {
        if (kv.first == "ps") return "gun_buttons.map: 'ps' kan niet worden ingedeeld: de PS-knop is altijd recenter (vast)";
        if (kv.first == "t") return "gun_buttons.map: 't' kan niet worden ingedeeld: T is altijd de analoge trekker";
        const auto& bs = gun_map_buttons();
        if (std::find(bs.begin(), bs.end(), kv.first) == bs.end())
            return "gun_buttons.map: '" + kv.first + "' is geen indeelbare knop (triangle, circle, cross, square, select, start, move, pump, ss_rl, ss_fire, sel1, sel2, sel3, nav_...)";
        std::string canon;
        std::string m = check_gun_action(kv.second, canon);
        if (!m.empty()) return "gun_buttons.map." + kv.first + ": " + m;
    }
    for (int i = 0; i < kMaxPlayers; ++i) {
        const std::string& gp = s.gamepads[static_cast<size_t>(i)];
        if (!gp.empty() && !sysinfo::mouse_id_valid(gp)) return "gamepads." + std::to_string(i + 1) + " is geen geldig gamepad-id";
    }
    return validate_controllers(s);
}

namespace {

// Schrijft de afwijkingen van een controller als geneste secties ({"filter":{"beta":4},"gun_buttons":{"map":{...}}}).
void write_overrides(json::Writer& w, const ControllerEntry& e) {
    const ControllerValues& v = e.values;
    auto has = [&](const char* k) { return e.keys.count(k) != 0; };
    auto any = [&](const char* prefix) {
        for (const auto& k : e.keys) if (k.rfind(prefix, 0) == 0) return true;
        return false;
    };
    w.begin_object();
    if (any("led.")) {
        w.key("led").begin_object();
        if (has("led.enabled")) w.key("enabled").value(v.led.enabled);
        if (has("led.preset")) w.key("preset").value(v.led.preset);
        if (has("led.r")) w.key("r").value(v.led.r);
        if (has("led.g")) w.key("g").value(v.led.g);
        if (has("led.b")) w.key("b").value(v.led.b);
        w.end_object();
    }
    if (any("tracker.")) {
        w.key("tracker").begin_object();
        if (has("tracker.hue_deg")) w.key("hue_deg").value(v.tracker.hue_deg);
        if (has("tracker.hue_tol_deg")) w.key("hue_tol_deg").value(v.tracker.hue_tol_deg);
        if (has("tracker.min_chroma")) w.key("min_chroma").value(v.tracker.min_chroma);
        w.end_object();
    }
    if (any("filter.")) {
        w.key("filter").begin_object();
        if (has("filter.enabled")) w.key("enabled").value(v.filter.enabled);
        if (has("filter.min_cutoff_hz")) w.key("min_cutoff_hz").value(v.filter.min_cutoff_hz);
        if (has("filter.beta")) w.key("beta").value(v.filter.beta);
        if (has("filter.d_cutoff_hz")) w.key("d_cutoff_hz").value(v.filter.d_cutoff_hz);
        w.end_object();
    }
    if (any("aim.")) {
        w.key("aim").begin_object();
        if (has("aim.h_edge_deg")) w.key("h_edge_deg").value(v.aim.h_edge_deg);
        if (has("aim.v_edge_deg")) w.key("v_edge_deg").value(v.aim.v_edge_deg);
        if (has("aim.predict_ms")) w.key("predict_ms").value(v.aim.predict_ms);
        w.end_object();
    }
    if (any("fusion.")) {
        w.key("fusion").begin_object();
        if (has("fusion.mode")) w.key("mode").value(v.fusion.mode);
        if (has("fusion.vqf_bias")) w.key("vqf_bias").value(v.fusion.vqf_bias);
        if (has("fusion.recenter_learning")) w.key("recenter_learning").value(v.fusion.recenter_learning);
        w.end_object();
    }
    if (any("rumble.")) {
        w.key("rumble").begin_object();
        if (has("rumble.enabled")) w.key("enabled").value(v.rumble.enabled);
        if (has("rumble.strength")) w.key("strength").value(v.rumble.strength);
        if (has("rumble.pulse_ms")) w.key("pulse_ms").value(v.rumble.pulse_ms);
        if (has("rumble.repeat_hz")) w.key("repeat_hz").value(v.rumble.repeat_hz);
        if (has("rumble.pump_strength")) w.key("pump_strength").value(v.rumble.pump_strength);
        w.end_object();
    }
    if (any("gun_buttons.")) {
        w.key("gun_buttons").begin_object();
        if (has("gun_buttons.trigger_on")) w.key("trigger_on").value(v.gun_buttons.trigger_on);
        if (has("gun_buttons.trigger_off")) w.key("trigger_off").value(v.gun_buttons.trigger_off);
        if (any(kMapPrefix)) {
            w.key("map").begin_object();
            for (const auto& b : gun_map_buttons()) {
                if (!e.keys.count(std::string(kMapPrefix) + b)) continue;
                auto it = v.gun_buttons.map.find(b);
                std::string canon = "none";
                if (it != v.gun_buttons.map.end()) check_gun_action(it->second, canon);
                w.key(b).value(canon);
            }
            w.end_object();
        }
        w.end_object();
    }
    w.end_object();
}

}  // namespace

std::string to_json(const Settings& s, bool pretty, bool redact) {
    const char* nl = pretty ? "\n" : "";
    const char* ind = pretty ? "  " : "";
    auto section = [&](const char* name, auto&& fill) {
        json::Writer w;
        w.begin_object();
        fill(w);
        w.end_object();
        return std::string(ind) + "\"" + name + "\":" + (pretty ? " " : "") + w.str();
    };
    std::string out = std::string("{") + nl + ind + "\"schema\":" + (pretty ? " " : "") + std::to_string(kSchemaVersion) + "," + nl;
    out += section("led", [&](json::Writer& w) {
        w.key("enabled").value(s.led.enabled).key("preset").value(s.led.preset);
        w.key("r").value(s.led.r).key("g").value(s.led.g).key("b").value(s.led.b);
    }) + "," + nl;
    out += section("tracker", [&](json::Writer& w) {
        w.key("hue_deg").value(s.tracker.hue_deg).key("hue_tol_deg").value(s.tracker.hue_tol_deg).key("min_chroma").value(s.tracker.min_chroma);
    }) + "," + nl;
    out += section("camera", [&](json::Writer& w) {
        w.key("mode").value(camera_mode_string(s.camera.width, s.camera.height, s.camera.fps));
        w.key("auto_exposure").value(s.camera.auto_exposure).key("auto_gain").value(s.camera.auto_gain).key("auto_awb").value(s.camera.auto_awb);
        w.key("exposure").value(s.camera.exposure).key("gain").value(s.camera.gain);
    }) + "," + nl;
    out += section("filter", [&](json::Writer& w) {
        w.key("enabled").value(s.filter.enabled).key("min_cutoff_hz").value(s.filter.min_cutoff_hz);
        w.key("beta").value(s.filter.beta).key("d_cutoff_hz").value(s.filter.d_cutoff_hz);
    }) + "," + nl;
    out += section("aim", [&](json::Writer& w) {
        w.key("h_edge_deg").value(s.aim.h_edge_deg).key("v_edge_deg").value(s.aim.v_edge_deg).key("predict_ms").value(s.aim.predict_ms);
    }) + "," + nl;
    out += section("output", [&](json::Writer& w) {
        w.key("clock_hz").value(s.output.clock_hz).key("extrapolate_ms").value(s.output.extrapolate_ms);
    }) + "," + nl;
    out += section("web", [&](json::Writer& w) { w.key("port").value(s.web.port).key("lan").value(s.web.lan).key("require_token").value(s.web.require_token); }) + "," + nl;
    out += section("sharp_shooter", [&](json::Writer& w) {
        w.key("buttons").begin_object();
        for (const auto& kv : s.sharp_shooter.buttons) w.key(kv.first).value(kv.second);
        w.end_object();
        w.key("extension").value(s.sharp_shooter.extension);
    }) + "," + nl;
    out += section("fusion", [&](json::Writer& w) {
        w.key("mode").value(s.fusion.mode).key("vqf_bias").value(s.fusion.vqf_bias).key("recenter_learning").value(s.fusion.recenter_learning);
    }) + "," + nl;
    out += section("rumble", [&](json::Writer& w) {
        w.key("enabled").value(s.rumble.enabled).key("strength").value(s.rumble.strength).key("pulse_ms").value(s.rumble.pulse_ms);
        w.key("repeat_hz").value(s.rumble.repeat_hz).key("pump_strength").value(s.rumble.pump_strength);
    }) + "," + nl;
    out += section("gun_buttons", [&](json::Writer& w) {
        w.key("trigger_on").value(s.gun_buttons.trigger_on).key("trigger_off").value(s.gun_buttons.trigger_off);
        w.key("map").begin_object();
        for (const auto& b : gun_map_buttons()) {
            auto it = s.gun_buttons.map.find(b);
            std::string canon = "none";
            if (it != s.gun_buttons.map.end()) check_gun_action(it->second, canon);
            w.key(b).value(canon);
        }
        w.end_object();
    }) + "," + nl;
    out += section("gamepads", [&](json::Writer& w) {
        for (int i = 0; i < kMaxPlayers; ++i) w.key(std::to_string(i + 1)).value(s.gamepads[static_cast<size_t>(i)]);
    }) + "," + nl;
    // Controllers: een regel per controller (leesbaar en met de hand te herstellen).
    out += std::string(ind) + "\"controllers\":" + (pretty ? " " : "") + "{";
    bool first = true;
    for (const auto& kv : s.controllers) {
        const ControllerEntry& e = kv.second;
        json::Writer w;
        w.begin_object();
        if (redact) w.key("address").value(redact_mac(kv.first));
        w.key("type").value(e.type).key("model").value(e.model).key("name").value(e.name).key("player").value(e.player);
        w.key("removed").value(e.removed);
        w.key("added_unix").value(static_cast<long long>(e.added_unix)).key("last_seen_unix").value(static_cast<long long>(e.last_seen_unix));
        w.key("settings");
        write_overrides(w, e);
        w.end_object();
        out += std::string(first ? "" : ",") + nl + ind + ind + json::quote(redact ? core::controller_key(kv.first) : kv.first) + ":" +
               (pretty ? " " : "") + w.str();
        first = false;
    }
    out += std::string(first ? "" : std::string(nl) + ind) + "}" + nl + "}" + nl;
    return out;
}

const std::vector<MigrationStep>& builtin_migrations() {
    // 1 -> 2: er komt alleen de sectie "controllers" bij (leeg). Alle bestaande waarden blijven staan en zijn vanaf nu de
    // standaard; een Move zonder eigen afwijkingen gebruikt precies die waarden, dus er gaat niets verloren. De Move die al
    // gekoppeld was, komt bij het eerste verbinden (of uit de BlueZ-lijst) als speler 1 in "controllers".
    static const std::vector<MigrationStep> steps = {
        {1, [](json::Value& root, std::string& err) {
             if (root.find("controllers")) { err = "schema 1 hoort geen 'controllers' te hebben"; return false; }
             return true;
         }},
    };
    return steps;
}

bool parse_settings(const std::string& text, Settings& out, std::string& err, std::vector<std::string>* warnings,
                    const std::vector<MigrationStep>* migrations) {
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok) { err = "ongeldige JSON: " + pr.error + " (positie " + std::to_string(pr.offset) + ")"; return false; }
    json::Value root = std::move(pr.value);
    if (!root.is_object()) { err = "het bestand moet een JSON-object zijn"; return false; }
    const json::Value* sv = root.find("schema");
    if (!sv || !sv->is_number() || std::floor(sv->num) != sv->num) { err = "schema (geheel getal) ontbreekt"; return false; }
    int ver = static_cast<int>(sv->num);
    if (ver > kSchemaVersion) {
        err = "schema " + std::to_string(ver) + " is nieuwer dan deze daemon kent (" + std::to_string(kSchemaVersion) + ")";
        return false;
    }
    if (ver < 0) { err = "schema " + std::to_string(ver) + " is ongeldig"; return false; }
    const auto& steps = migrations ? *migrations : builtin_migrations();
    while (ver < kSchemaVersion) {
        const MigrationStep* st = nullptr;
        for (const auto& m : steps) if (m.from == ver) st = &m;
        if (!st) for (const auto& m : builtin_migrations()) if (m.from == ver) st = &m;  // eigen lijst eerst, dan de ingebouwde
        if (!st) { err = "geen migratie van schema " + std::to_string(ver); return false; }
        std::string merr;
        if (!st->apply(root, merr)) { err = "migratie van schema " + std::to_string(ver) + " mislukt: " + merr; return false; }
        ++ver;
    }
    if (warnings) {
        for (const auto& kv : root.obj) {
            static const char* known[] = {"schema", "led", "tracker", "camera", "filter", "aim", "output", "web", "sharp_shooter", "fusion", "gun_buttons", "rumble", "controllers", "gamepads"};
            bool k = false;
            for (const char* n : known) if (kv.first == n) k = true;
            if (!k) warnings->push_back("onbekende sleutel '" + kv.first + "' genegeerd");
        }
    }

    Settings s;
    std::string e;
    auto sec = [&](const char* name) -> const json::Value* {
        const json::Value* v = root.find(name);
        if (v && !v->is_object()) { if (e.empty()) e = std::string(name) + " moet een object zijn"; return nullptr; }
        return v;
    };
    if (const json::Value* o = sec("led")) {
        Section c{*o, "led", e, warnings};
        c.boolean("enabled", s.led.enabled); c.string("preset", s.led.preset);
        c.integer("r", 0, 255, s.led.r); c.integer("g", 0, 255, s.led.g); c.integer("b", 0, 255, s.led.b);
        c.unknown({"enabled", "preset", "r", "g", "b"});
    }
    if (const json::Value* o = sec("tracker")) {
        Section c{*o, "tracker", e, warnings};
        c.number("hue_deg", -180, 180, s.tracker.hue_deg, false);
        c.number("hue_tol_deg", 1, 90, s.tracker.hue_tol_deg, false);
        c.integer("min_chroma", 1, 180, s.tracker.min_chroma);
        c.unknown({"hue_deg", "hue_tol_deg", "min_chroma"});
    }
    if (const json::Value* o = sec("camera")) {
        Section c{*o, "camera", e, warnings};
        std::string mode;
        if (c.string("mode", mode) && !mode.empty()) {
            CameraMode m{};
            if (!parse_camera_mode(mode, m)) c.fail("camera.mode '" + mode + "' is geen toegestane modus (640x480@60, 320x240@125, 320x240@187)");
            else { s.camera.width = m.width; s.camera.height = m.height; s.camera.fps = m.fps; }
        }
        c.boolean("auto_exposure", s.camera.auto_exposure); c.boolean("auto_gain", s.camera.auto_gain); c.boolean("auto_awb", s.camera.auto_awb);
        c.integer("exposure", 0, 255, s.camera.exposure); c.integer("gain", 0, 63, s.camera.gain);
        c.unknown({"mode", "auto_exposure", "auto_gain", "auto_awb", "exposure", "gain"});
    }
    if (const json::Value* o = sec("filter")) {
        Section c{*o, "filter", e, warnings};
        c.boolean("enabled", s.filter.enabled);
        c.number("min_cutoff_hz", 0.001, 100, s.filter.min_cutoff_hz, false);
        c.number("beta", 0, 1000, s.filter.beta, false);
        c.number("d_cutoff_hz", 0.001, 100, s.filter.d_cutoff_hz, false);
        c.unknown({"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"});
    }
    if (const json::Value* o = sec("aim")) {
        Section c{*o, "aim", e, warnings};
        c.number("h_edge_deg", 1, 89, s.aim.h_edge_deg, false);
        c.number("v_edge_deg", 1, 89, s.aim.v_edge_deg, false);
        c.number("predict_ms", 0, 200, s.aim.predict_ms, false);
        c.unknown({"h_edge_deg", "v_edge_deg", "predict_ms"});
    }
    if (const json::Value* o = sec("output")) {
        Section c{*o, "output", e, warnings};
        c.number("clock_hz", 0, 1000, s.output.clock_hz, false);
        c.number("extrapolate_ms", 0, 100, s.output.extrapolate_ms, false);
        c.unknown({"clock_hz", "extrapolate_ms"});
    }
    if (const json::Value* o = sec("web")) {
        Section c{*o, "web", e, warnings};
        c.integer("port", 1, 65535, s.web.port);
        c.boolean("lan", s.web.lan);
        c.boolean("require_token", s.web.require_token);
        c.unknown({"port", "lan", "require_token"});
    }
    if (const json::Value* o = sec("sharp_shooter")) {
        Section c{*o, "sharp_shooter", e, warnings};
        c.string("extension", s.sharp_shooter.extension);
        c.unknown({"buttons", "extension"});
        if (const json::Value* b = o->find("buttons")) {
            if (!b->is_object()) c.fail("sharp_shooter.buttons moet een object zijn");
            else
                for (const auto& kv : b->obj) {
                    if (!kv.second.is_string() || kv.second.str.size() > 32) { c.fail("sharp_shooter.buttons." + kv.first + " moet een korte tekst zijn"); break; }
                    s.sharp_shooter.buttons[kv.first] = kv.second.str;
                }
        }
    }
    if (const json::Value* o = sec("fusion")) {
        Section c{*o, "fusion", e, warnings};
        c.string("mode", s.fusion.mode);
        c.string("vqf_bias", s.fusion.vqf_bias);
        c.boolean("recenter_learning", s.fusion.recenter_learning);
        c.unknown({"mode", "vqf_bias", "recenter_learning"});
    }
    if (const json::Value* o = sec("rumble")) {
        Section c{*o, "rumble", e, warnings};
        c.boolean("enabled", s.rumble.enabled);
        c.integer("strength", 0, 255, s.rumble.strength);
        c.integer("pulse_ms", 10, 500, s.rumble.pulse_ms);
        c.number("repeat_hz", 0, 30, s.rumble.repeat_hz, false);
        c.integer("pump_strength", 0, 255, s.rumble.pump_strength);
        c.unknown({"enabled", "strength", "pulse_ms", "repeat_hz", "pump_strength"});
    }
    if (const json::Value* o = sec("gun_buttons")) {
        Section c{*o, "gun_buttons", e, warnings};
        c.integer("trigger_on", 1, 255, s.gun_buttons.trigger_on);
        c.integer("trigger_off", 0, 254, s.gun_buttons.trigger_off);
        c.unknown({"trigger_on", "trigger_off", "map", "reload", "grenade", "recenter"});
        // Oud formaat (reload/grenade/recenter = Move-knopnaam): vertaald naar map-items die de defaults voor die knop overschrijven.
        for (const char* old : {"reload", "grenade", "recenter"}) {
            const json::Value* v = o->find(old);
            if (!v) continue;
            if (!v->is_string() || v->str.size() > 32) { c.fail(std::string("gun_buttons.") + old + " moet een korte tekst zijn"); break; }
            if (v->str.empty()) continue;
            if (v->str == "ps" && std::string(old) == "recenter") {
                if (warnings) warnings->push_back("gun_buttons.recenter 'ps' genegeerd: de PS-knop is altijd recenter");
                continue;
            }
            if (v->str == "ps" || v->str == "t") { c.fail("gun_buttons." + std::string(old) + ": '" + v->str + "' kan niet worden ingedeeld (PS is altijd recenter, T is de trekker)"); break; }
            if (!move_button_name_valid(v->str)) { c.fail("gun_buttons." + std::string(old) + ": '" + v->str + "' is geen Move-knop"); break; }
            s.gun_buttons.map[v->str] = old;
        }
        if (const json::Value* m = o->find("map")) {
            if (!m->is_object()) c.fail("gun_buttons.map moet een object zijn");
            else
                for (const auto& kv : m->obj) {
                    if (!kv.second.is_string() || kv.second.str.size() > 32) { c.fail("gun_buttons.map." + kv.first + " moet een korte tekst zijn"); break; }
                    s.gun_buttons.map[kv.first] = kv.second.str;  // validate() controleert knop en actie
                }
        }
    }
    if (const json::Value* o = sec("gamepads")) {
        Section c{*o, "gamepads", e, warnings};
        c.unknown({"1", "2", "3", "4"});
        for (int i = 0; i < kMaxPlayers && e.empty(); ++i) {
            const json::Value* v = o->find(std::to_string(i + 1));
            if (!v) continue;
            if (!v->is_string() || v->str.size() > sysinfo::kMouseIdMax) { c.fail("gamepads." + std::to_string(i + 1) + " moet een gamepad-id zijn"); break; }
            s.gamepads[static_cast<size_t>(i)] = v->str;   // validate() controleert de tekens
        }
    }
    if (const json::Value* cs = sec("controllers"); cs && e.empty()) {
        for (const auto& kv : cs->obj) {
            const std::string where = "controllers." + redact_mac(kv.first);
            const std::string addr = normalize_address(kv.first);
            if (addr.empty() || addr != kv.first) { e = where + ": ongeldig adres (verwacht aa:bb:cc:dd:ee:ff in kleine letters)"; break; }
            if (!kv.second.is_object()) { e = where + " moet een object zijn"; break; }
            const json::Value& o = kv.second;
            ControllerEntry ce;
            Section c{o, where, e, warnings};
            c.string("type", ce.type);
            c.string("model", ce.model);
            c.string("name", ce.name);
            c.integer("player", 0, kMaxPlayers, ce.player);
            c.boolean("removed", ce.removed);
            double au = 0, ls = 0;
            c.number("added_unix", 0, 1e11, au, true);
            c.number("last_seen_unix", 0, 1e11, ls, true);
            ce.added_unix = static_cast<int64_t>(au);
            ce.last_seen_unix = static_cast<int64_t>(ls);
            c.unknown({"type", "model", "name", "player", "removed", "added_unix", "last_seen_unix", "settings"});
            ce.values = static_cast<const ControllerValues&>(s);  // basis; alleen de genoemde sleutels tellen
            if (const json::Value* st = o.find("settings")) {
                if (!st->is_object()) { c.fail(where + ".settings moet een object zijn"); break; }
                ControllerValues& v = ce.values;
                auto read = [&](const char* name, auto&& fn) {
                    const json::Value* x = st->find(name);
                    if (!x) return;
                    if (!x->is_object()) { c.fail(where + ".settings." + name + " moet een object zijn"); return; }
                    Section d{*x, where + ".settings." + name, e, warnings};
                    fn(d, *x, std::string(name) + ".");
                };
                auto mark = [&](const json::Value& x, const std::string& pre, std::initializer_list<const char*> ks) {
                    for (const char* k : ks) if (x.find(k)) ce.keys.insert(pre + k);
                };
                read("led", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.boolean("enabled", v.led.enabled); d.string("preset", v.led.preset);
                    d.integer("r", 0, 255, v.led.r); d.integer("g", 0, 255, v.led.g); d.integer("b", 0, 255, v.led.b);
                    d.unknown({"enabled", "preset", "r", "g", "b"});
                    mark(x, pre, {"enabled", "preset", "r", "g", "b"});
                });
                read("tracker", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.number("hue_deg", -180, 180, v.tracker.hue_deg, false);
                    d.number("hue_tol_deg", 1, 90, v.tracker.hue_tol_deg, false);
                    d.integer("min_chroma", 1, 180, v.tracker.min_chroma);
                    d.unknown({"hue_deg", "hue_tol_deg", "min_chroma"});
                    mark(x, pre, {"hue_deg", "hue_tol_deg", "min_chroma"});
                });
                read("filter", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.boolean("enabled", v.filter.enabled);
                    d.number("min_cutoff_hz", 0.001, 100, v.filter.min_cutoff_hz, false);
                    d.number("beta", 0, 1000, v.filter.beta, false);
                    d.number("d_cutoff_hz", 0.001, 100, v.filter.d_cutoff_hz, false);
                    d.unknown({"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"});
                    mark(x, pre, {"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"});
                });
                read("aim", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.number("h_edge_deg", 1, 89, v.aim.h_edge_deg, false);
                    d.number("v_edge_deg", 1, 89, v.aim.v_edge_deg, false);
                    d.number("predict_ms", 0, 200, v.aim.predict_ms, false);
                    d.unknown({"h_edge_deg", "v_edge_deg", "predict_ms"});
                    mark(x, pre, {"h_edge_deg", "v_edge_deg", "predict_ms"});
                });
                read("fusion", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.string("mode", v.fusion.mode); d.string("vqf_bias", v.fusion.vqf_bias);
                    d.boolean("recenter_learning", v.fusion.recenter_learning);
                    d.unknown({"mode", "vqf_bias", "recenter_learning"});
                    mark(x, pre, {"mode", "vqf_bias", "recenter_learning"});
                });
                read("rumble", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.boolean("enabled", v.rumble.enabled);
                    d.integer("strength", 0, 255, v.rumble.strength);
                    d.integer("pulse_ms", 10, 500, v.rumble.pulse_ms);
                    d.number("repeat_hz", 0, 30, v.rumble.repeat_hz, false);
                    d.integer("pump_strength", 0, 255, v.rumble.pump_strength);
                    d.unknown({"enabled", "strength", "pulse_ms", "repeat_hz", "pump_strength"});
                    mark(x, pre, {"enabled", "strength", "pulse_ms", "repeat_hz", "pump_strength"});
                });
                read("gun_buttons", [&](Section& d, const json::Value& x, const std::string& pre) {
                    d.integer("trigger_on", 1, 255, v.gun_buttons.trigger_on);
                    d.integer("trigger_off", 0, 254, v.gun_buttons.trigger_off);
                    d.unknown({"trigger_on", "trigger_off", "map"});
                    mark(x, pre, {"trigger_on", "trigger_off"});
                    if (const json::Value* m = x.find("map")) {
                        if (!m->is_object()) { d.fail(where + ".settings.gun_buttons.map moet een object zijn"); return; }
                        for (const auto& mk : m->obj) {
                            if (!mk.second.is_string() || mk.second.str.size() > 32) { d.fail(where + ".settings.gun_buttons.map." + mk.first + " moet een korte tekst zijn"); return; }
                            const auto& bs = gun_map_buttons();
                            if (std::find(bs.begin(), bs.end(), mk.first) == bs.end()) { d.fail(where + ".settings.gun_buttons.map: '" + mk.first + "' is geen indeelbare knop"); return; }
                            v.gun_buttons.map[mk.first] = mk.second.str;
                            ce.keys.insert(pre + "map." + mk.first);
                        }
                    }
                });
                for (const auto& sk : st->obj) {
                    static const char* known[] = {"led", "tracker", "filter", "aim", "fusion", "rumble", "gun_buttons"};
                    bool k = false;
                    for (const char* n : known) if (sk.first == n) k = true;
                    if (!k && warnings) warnings->push_back("onbekende sleutel '" + where + ".settings." + sk.first + "' genegeerd");
                }
            }
            if (!e.empty()) break;
            s.controllers[addr] = std::move(ce);
        }
    }
    if (e.empty()) e = validate(s);  // o.a. de presetnaam
    if (!e.empty()) { err = e; return false; }
    out = s;
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& content, std::string& err) {
    if (path.empty()) { err = "geen configuratiepad bekend (HOME/XDG_CONFIG_HOME)"; return false; }
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    if (!dir.empty()) {
        fs::create_directories(dir, ec);
        if (ec) { err = "kan " + dir.string() + " niet maken: " + ec.message(); return false; }
        ::chmod(dir.c_str(), 0700);
    }
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0 && errno == EEXIST) {  // restant van een eerdere crash met hetzelfde pid
        ::unlink(tmp.c_str());
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    }
    if (fd < 0) { err = "kan " + tmp + " niet maken: " + std::strerror(errno); return false; }
    size_t off = 0;
    while (off < content.size()) {
        ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = std::string("schrijven mislukt: ") + std::strerror(errno);
            ::close(fd); ::unlink(tmp.c_str());
            return false;
        }
        off += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) { err = std::string("fsync mislukt: ") + std::strerror(errno); ::close(fd); ::unlink(tmp.c_str()); return false; }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "hernoemen naar " + path + " mislukt: " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    if (!dir.empty()) {
        int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
    }
    return true;
}

const char* to_string(LoadInfo::State s) {
    switch (s) {
        case LoadInfo::State::NotLoaded: return "not_loaded";
        case LoadInfo::State::Missing: return "missing";
        case LoadInfo::State::Loaded: return "loaded";
        case LoadInfo::State::Recovered: return "recovered";
        case LoadInfo::State::Invalid: return "invalid";
    }
    return "?";
}

std::string SettingsStore::default_path() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const char* h = std::getenv("HOME");
    std::string base = x && *x ? x : (h && *h ? std::string(h) + "/.config" : std::string());
    if (base.empty()) return {};
    return base + "/ravengun/config.json";
}

SettingsStore::SettingsStore(std::string path) : path_(std::move(path)) {}

LoadInfo SettingsStore::load() {
    LoadInfo li;
    Settings s;
    std::string text, rerr, perr;
    bool missing = false;
    if (path_.empty()) {
        li.state = LoadInfo::State::Missing;
        li.message = "geen configuratiepad bekend (HOME/XDG_CONFIG_HOME); instellingen worden niet bewaard";
    } else if (!read_file(path_, text, rerr, missing)) {
        if (missing) {
            li.state = LoadInfo::State::Missing;
        } else {
            rerr = path_ + ": " + rerr;
            std::string btext, berr, bperr;
            bool bmissing = false;
            Settings bs;
            if (read_file(path_ + ".bak", btext, berr, bmissing) && parse_settings(btext, bs, bperr, &li.warnings)) {
                s = bs;
                li.state = LoadInfo::State::Recovered;
                li.message = rerr + "; laatst-goede kopie (config.json.bak) gebruikt";
            } else {
                li.state = LoadInfo::State::Invalid;
                li.message = rerr + "; standaardwaarden gebruikt";
            }
        }
    } else if (parse_settings(text, s, perr, &li.warnings)) {
        li.state = LoadInfo::State::Loaded;
    } else {
        s = Settings{};
        li.warnings.clear();
        std::string btext, berr, bperr;
        bool bmissing = false;
        Settings bs;
        std::string base = "config.json is ongeldig (" + perr + ")";
        if (read_file(path_ + ".bak", btext, berr, bmissing) && parse_settings(btext, bs, bperr, &li.warnings)) {
            s = bs;
            li.state = LoadInfo::State::Recovered;
            li.message = base + "; laatst-goede kopie (config.json.bak) gebruikt";
        } else {
            li.state = LoadInfo::State::Invalid;
            li.message = base + "; standaardwaarden gebruikt. Het bestand blijft staan tot de eerstvolgende wijziging en wordt dan hernoemd naar config.json.corrupt.";
        }
    }
    std::lock_guard<std::mutex> g(m_);
    cur_ = s;
    info_ = li;
    return li;
}

Settings SettingsStore::get() const {
    std::lock_guard<std::mutex> g(m_);
    return cur_;
}

LoadInfo SettingsStore::info() const {
    std::lock_guard<std::mutex> g(m_);
    return info_;
}

std::string SettingsStore::last_save_error() const {
    std::lock_guard<std::mutex> g(m_);
    return last_save_error_;
}

bool SettingsStore::file_present() const {
    std::error_code ec;
    return !path_.empty() && fs::exists(path_, ec);
}

SaveResult SettingsStore::update(const std::function<void(Settings&)>& fn) {
    std::lock_guard<std::mutex> g(m_);
    Settings n = cur_;
    fn(n);
    std::string v = validate(n);
    if (!v.empty()) return {false, "ongeldige instelling: " + v};
    cur_ = n;
    if (path_.empty()) {
        last_save_error_ = "niet bewaard: geen instellingenbestand (--no-config, of HOME en XDG_CONFIG_HOME ontbreken)";
        return {false, last_save_error_};
    }
    // Voor het overschrijven: een geldig bestaand bestand wordt de laatst-goede kopie; een ongeldig
    // bestand wordt opzij gezet (nooit stil weggegooid).
    std::string old, rerr, perr;
    bool missing = false;
    if (read_file(path_, old, rerr, missing)) {
        Settings tmp;
        if (parse_settings(old, tmp, perr)) {
            std::string werr;
            if (!write_file_atomic(path_ + ".bak", old, werr)) { /* kopie is best effort */ }
        } else {
            ::rename(path_.c_str(), (path_ + ".corrupt").c_str());
        }
    }
    std::string werr;
    if (!write_file_atomic(path_, to_json(n), werr)) {
        last_save_error_ = werr;
        return {false, "niet bewaard: " + werr};
    }
    last_save_error_.clear();
    if (info_.state == LoadInfo::State::Missing || info_.state == LoadInfo::State::Invalid || info_.state == LoadInfo::State::Recovered)
        info_.state = LoadInfo::State::Loaded, info_.message.clear();
    return {true, {}};
}

}  // namespace ravengun::config
