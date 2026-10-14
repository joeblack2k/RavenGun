#include "ravengun/sysinfo/gamepads.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "ravengun/sysinfo/mice.hpp"

namespace ravengun::sysinfo {

namespace {

constexpr unsigned kOwnVendor = 0x1209;   // pid.codes-testbereik: de virtuele apparaten van RavenGun (en libvirtualhid)

bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool contains(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

bool ends_with(const std::string& s, const char* suf) {
    const std::string x(suf);
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

}  // namespace

std::string gamepad_fallback_id(const std::string& name, const std::string& uniq, const std::string& phys) {
    std::string n;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        n += ok ? c : '_';
    }
    if (n.empty()) n = "gamepad";
    if (n.size() > 80) n.resize(80);
    std::string id = "pad-" + n;
    const std::string& src = !uniq.empty() ? uniq : phys;
    if (!src.empty()) {
        char b[16];
        std::snprintf(b, sizeof b, "-%08x", fnv1a(src));
        id += b;
    }
    return id;
}

std::vector<Gamepad> list_gamepads(std::string_view proc_text, const std::string& byid_dir, std::vector<GamepadVerdict>* verdicts) {
    auto byid = byid_links(byid_dir);
    for (auto& kv : byid)
        // Liefst de naam die op een joystick/gamepad wijst ("...-event-joystick").
        std::stable_partition(kv.second.begin(), kv.second.end(), [](const std::string& n) { return ends_with(n, "-event-joystick"); });
    std::vector<Gamepad> out;
    std::set<std::string> used;
    for (const auto& d : parse_input_devices(proc_text)) {
        GamepadVerdict v;
        v.name = d.name;
        v.event = d.event_handler();
        v.vendor = d.vendor;
        v.product = d.product;
        if (starts_with(d.name, "libvirtualhid")) v.why = "libvirtualhid (virtueel apparaat)";
        else if (starts_with(d.name, "RavenGun")) v.why = "eigen virtueel apparaat (naam RavenGun...)";
        else if (d.vendor == kOwnVendor) v.why = "eigen virtueel apparaat (vendor 1209)";
        else if (contains(d.name, "Motion Controller")) v.why = "PS Move (Motion Controller)";
        else if (v.event.empty()) v.why = "geen eventN-handler";
        else if (!d.has_key(BTN_SOUTH)) v.why = d.key_bits.empty() ? "geen knoppen (EV_KEY)" : "geen BTN_SOUTH/BTN_GAMEPAD";
        else if (!d.has_abs(ABS_X) || !d.has_abs(ABS_Y)) v.why = "geen ABS_X/ABS_Y";
        else {
            v.gamepad = true;
            v.why = "gamepad";
        }
        if (v.gamepad) {
            Gamepad g;
            g.name = d.name;
            g.event = v.event;
            g.phys = d.phys;
            g.uniq = d.uniq;
            g.bus = d.bus;
            g.vendor = d.vendor;
            g.product = d.product;
            auto it = byid.find(g.event);
            if (it != byid.end())
                for (const auto& n : it->second)
                    if (!used.count(n)) { g.id = n; g.by_id = true; break; }
            if (g.id.empty()) {
                g.id = gamepad_fallback_id(d.name, d.uniq, d.phys);
                // Twee gelijke pads zonder uniq en phys: volgnummer erachter (dan niet stabiel tussen herstarts, wel uniek).
                const std::string base = g.id;
                for (int k = 2; used.count(g.id); ++k) g.id = base + "-" + std::to_string(k);
            }
            used.insert(g.id);
            v.why += g.by_id ? " (id uit by-id)" : " (id uit naam en uniq/phys)";
            out.push_back(std::move(g));
        }
        if (verdicts) verdicts->push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](const Gamepad& a, const Gamepad& b) {
        return a.event.size() != b.event.size() ? a.event.size() < b.event.size() : a.event < b.event;
    });
    return out;
}

std::vector<Gamepad> list_system_gamepads(std::vector<GamepadVerdict>* verdicts) {
    std::ifstream f("/proc/bus/input/devices", std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return list_gamepads(ss.str(), "/dev/input/by-id", verdicts);
}

}  // namespace ravengun::sysinfo
