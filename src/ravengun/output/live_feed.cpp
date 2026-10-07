#include "ravengun/output/live_feed.hpp"

#include <cmath>

#include "ravengun/core/buttons.hpp"

namespace ravengun::output {

uint32_t move_button_bit(const std::string& name) {
    for (const auto& b : core::kButtonNames)
        if (name == b.name) return b.bit;
    return 0;
}

LiveAction LiveGunFeed::update(const core::InputSample& s) {
    LiveAction a;
    if (s.link == core::LinkState::Lost || s.link == core::LinkState::Disconnected) {
        trigger_down_ = false;
        a.release_all = true;
        return a;
    }
    // Trekker met hysterese op de analoge waarde.
    if (!trigger_down_ && s.trigger >= map_.trigger_on) trigger_down_ = true;
    else if (trigger_down_ && s.trigger <= map_.trigger_off) trigger_down_ = false;
    a.buttons = true;
    a.trigger = trigger_down_;
    a.reload = map_.reload && (s.buttons & map_.reload);
    a.grenade = map_.grenade && (s.buttons & map_.grenade);
    a.recenter = map_.recenter && (s.pressed & map_.recenter);
    if (s.aim_valid && std::isfinite(s.aim.x) && std::isfinite(s.aim.y)) {
        a.move = true;
        a.x = s.aim.x;
        a.y = s.aim.y;
    }
    return a;
}

}  // namespace ravengun::output
