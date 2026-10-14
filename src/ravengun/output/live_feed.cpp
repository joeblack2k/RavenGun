#include "ravengun/output/live_feed.hpp"

#include <cmath>

#include "ravengun/core/buttons.hpp"
#include "ravengun/core/navcon.hpp"
#include "ravengun/output/key_table.hpp"

namespace ravengun::output {

uint64_t move_button_bit(const std::string& name) {
    for (const auto& b : core::kButtonNames)
        if (name == b.name) return b.bit;
    if (uint32_t n = core::nav_button_bit(name)) return static_cast<uint64_t>(n) << kNavBitShift;
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
        uint64_t bit = move_button_bit(b);
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

bool OverrideLayer::live_key(int code, bool live) {
    if (live) live_keys_[code] = true; else live_keys_.erase(code);
    return live || held_key(code);
}

bool OverrideLayer::want_key(int code) const {
    auto it = live_keys_.find(code);
    return (it != live_keys_.end() && it->second) || held_key(code);
}

bool OverrideLayer::held_key(int code) const {
    auto it = keys_.find(code);
    return it != keys_.end() && (it->second.down || it->second.tap != 0);
}

void OverrideLayer::end_tap_key(int code, uint64_t token) {
    auto it = keys_.find(code);
    if (it == keys_.end() || it->second.tap != token) return;
    it->second.tap = 0;
    if (!it->second.down) keys_.erase(it);
}

bool OverrideLayer::any_synthetic() const {
    for (const auto& h : btn_) if (h.down || h.tap != 0) return true;
    for (const auto& kv : keys_) if (kv.second.down || kv.second.tap != 0) return true;
    return false;
}

void OverrideLayer::clear() {
    btn_.fill({});
    live_btn_.fill(false);
    for (auto& s : live_src_) s.fill(false);
    keys_.clear();
    live_keys_.clear();
}

LiveAction LiveGunFeed::update(const core::InputSample& s, uint32_t nav) {
    LiveAction a;
    if (s.link == core::LinkState::Lost || s.link == core::LinkState::Disconnected) {
        trigger_down_ = false;
        prev_nav_ = nav;
        a.release_all = true;
        return a;
    }
    const uint64_t all = static_cast<uint64_t>(s.buttons) | (static_cast<uint64_t>(nav) << kNavBitShift);
    const uint64_t pressed = static_cast<uint64_t>(s.pressed) | (static_cast<uint64_t>(nav & ~prev_nav_) << kNavBitShift);
    prev_nav_ = nav;
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
    a.reload = (all & map_.gun[static_cast<int>(GunButton::Reload)]) != 0;
    a.grenade = (all & map_.gun[static_cast<int>(GunButton::Grenade)]) != 0;
    a.aux1 = (all & map_.gun[static_cast<int>(GunButton::Aux1)]) != 0;
    a.aux2 = (all & map_.gun[static_cast<int>(GunButton::Aux2)]) != 0;
    for (const auto& k : map_.keys) a.keys.emplace_back(k.code, (all & k.bits) != 0);
    a.recenter_ps = (s.pressed & core::kBtnPS) != 0;
    a.recenter = a.recenter_ps || (pressed & map_.recenter) != 0;
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
