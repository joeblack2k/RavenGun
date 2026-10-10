#include "ravengun/output/mouse_gun.hpp"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>

namespace ravengun::output {

namespace {

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Welke gun-knop hoort bij een evdev-toetscode; -1 = geen.
int button_index(uint16_t code) {
    switch (code) {
        case BTN_LEFT: return static_cast<int>(GunButton::Trigger);
        case BTN_RIGHT: return static_cast<int>(GunButton::Reload);
        case BTN_MIDDLE: return static_cast<int>(GunButton::Grenade);
        case BTN_SIDE: return static_cast<int>(GunButton::Aux1);
        case BTN_EXTRA: return static_cast<int>(GunButton::Aux2);
        default: return -1;
    }
}

}  // namespace

void MouseGun::clamp_cfg() {
    if (!std::isfinite(cfg_.counts_per_screen)) cfg_.counts_per_screen = kMouseCountsDefault;
    cfg_.counts_per_screen = std::clamp(cfg_.counts_per_screen, kMouseCountsMin, kMouseCountsMax);
    if (!std::isfinite(cfg_.screen_aspect) || cfg_.screen_aspect < 0.1 || cfg_.screen_aspect > 10) cfg_.screen_aspect = 16.0 / 9.0;
}

LiveAction MouseGun::make() const {
    LiveAction a;
    a.move = true;
    a.x = x_;
    a.y = y_;
    a.buttons = true;
    a.trigger = down_[static_cast<int>(GunButton::Trigger)];
    a.reload = down_[static_cast<int>(GunButton::Reload)];
    a.grenade = down_[static_cast<int>(GunButton::Grenade)];
    a.aux1 = down_[static_cast<int>(GunButton::Aux1)];
    a.aux2 = down_[static_cast<int>(GunButton::Aux2)];
    return a;
}

LiveAction MouseGun::snapshot() const { return make(); }

LiveAction MouseGun::release() {
    for (bool& b : down_) b = false;
    dirty_ = false;
    dropped_ = false;
    LiveAction a;
    a.release_all = true;
    return a;
}

std::optional<LiveAction> MouseGun::event(uint16_t type, uint16_t code, int32_t value) {
    switch (type) {
        case EV_SYN:
            if (code == SYN_DROPPED) { dropped_ = true; return std::nullopt; }
            if (code != SYN_REPORT) return std::nullopt;
            if (dropped_) {
                dropped_ = false;
                for (bool& b : down_) b = false;
                dirty_ = true;
            }
            if (!dirty_) return std::nullopt;
            dirty_ = false;
            return make();
        case EV_REL: {
            if (dropped_) return std::nullopt;
            const double counts = cfg_.counts_per_screen;
            if (code == REL_X) { x_ = clamp01(x_ + value / counts); dirty_ = true; }
            else if (code == REL_Y) { y_ = clamp01(y_ + value * cfg_.screen_aspect / counts); dirty_ = true; }
            return std::nullopt;
        }
        case EV_ABS: {
            if (dropped_) return std::nullopt;
            const AbsAxis& ax = code == ABS_X ? cfg_.abs_x : cfg_.abs_y;
            if ((code != ABS_X && code != ABS_Y) || !ax.valid()) return std::nullopt;
            const double n = clamp01(static_cast<double>(value - ax.min) / (static_cast<double>(ax.max) - ax.min));
            (code == ABS_X ? x_ : y_) = n;
            dirty_ = true;
            return std::nullopt;
        }
        case EV_KEY: {
            if (dropped_) return std::nullopt;
            const int i = button_index(code);
            if (i < 0) return std::nullopt;
            const bool d = value != 0;  // 2 = autorepeat: blijft ingedrukt
            if (down_[i] != d) { down_[i] = d; dirty_ = true; }
            return std::nullopt;
        }
        default: return std::nullopt;
    }
}

}  // namespace ravengun::output
