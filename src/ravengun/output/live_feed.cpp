#include "ravengun/output/live_feed.hpp"

#include <cmath>

#include "ravengun/core/buttons.hpp"
#include "ravengun/output/key_table.hpp"

namespace ravengun::output {

uint32_t move_button_bit(const std::string& name) {
    for (const auto& b : core::kButtonNames)
        if (name == b.name) return b.bit;
    return 0;
}

LiveButtonMap build_live_map(const config::GunButtonSettings& g) {
    LiveButtonMap m;
    m.trigger_on = g.trigger_on;
    m.trigger_off = g.trigger_off;
    for (const auto& b : config::gun_map_buttons()) {
        auto it = g.map.find(b);
        std::string canon = "none";
        if (it != g.map.end()) config::check_gun_action(it->second, canon);
        uint32_t bit = move_button_bit(b);
        m.layout.emplace_back(b, canon);
        if (canon == "none" || !bit) continue;
        if (canon == "recenter") { m.recenter |= bit; continue; }
        if (canon.compare(0, 4, "key:") == 0) {
            int code = key_code(canon.substr(4));
            if (code < 0) continue;
            KeyBinding* kb = nullptr;
            for (auto& k : m.keys) if (k.code == code) kb = &k;
            if (!kb) { m.keys.push_back({code, key_name(code), 0}); kb = &m.keys.back(); }
            kb->bits |= bit;
            continue;
        }
        GunButton gb;
        if (parse_button(canon, gb) && gb != GunButton::Trigger) m.gun[static_cast<int>(gb)] |= bit;
    }
    m.layout.emplace_back("ps", "recenter (vast)");
    return m;
}

std::string describe_layout(const LiveButtonMap& m) {
    std::string s;
    for (const auto& kv : m.layout) {
        if (!s.empty()) s += ", ";
        s += kv.first + "=" + kv.second;
    }
    return s;
}

LiveAction LiveGunFeed::update(const core::InputSample& s) {
    LiveAction a;
    if (s.link == core::LinkState::Lost || s.link == core::LinkState::Disconnected) {
        trigger_down_ = false;
        a.release_all = true;
        return a;
    }
    if (s.ext_active) {
        // Sharp Shooter: de trekker is het fire-bit van de uitbreiding (valt in de opname altijd samen met T, zonder
        // extra rapport vertraging); T zonder fire is de pomp (knop "pump", afgeleid in de pijplijn) en geeft geen schot.
        trigger_down_ = (s.buttons & core::kBtnSsFire) != 0;
    } else {
        // Zonder uitbreiding: de analoge T met hysterese.
        if (!trigger_down_ && s.trigger >= map_.trigger_on) trigger_down_ = true;
        else if (trigger_down_ && s.trigger <= map_.trigger_off) trigger_down_ = false;
    }
    a.buttons = true;
    a.trigger = trigger_down_;
    a.reload = s.buttons & map_.gun[static_cast<int>(GunButton::Reload)];
    a.grenade = s.buttons & map_.gun[static_cast<int>(GunButton::Grenade)];
    a.aux1 = s.buttons & map_.gun[static_cast<int>(GunButton::Aux1)];
    a.aux2 = s.buttons & map_.gun[static_cast<int>(GunButton::Aux2)];
    for (const auto& k : map_.keys) a.keys.emplace_back(k.code, (s.buttons & k.bits) != 0);
    a.recenter_ps = (s.pressed & core::kBtnPS) != 0;
    a.recenter = a.recenter_ps || (s.pressed & map_.recenter) != 0;
    if (s.aim_valid && std::isfinite(s.aim.x) && std::isfinite(s.aim.y)) {
        a.move = true;
        a.x = s.aim.x;
        a.y = s.aim.y;
        a.vx = s.aim_vx;
        a.vy = s.aim_vy;
        a.age_ms = s.age_ms;
        a.t_ns = s.t_eval_ns;
    }
    return a;
}

}  // namespace ravengun::output
