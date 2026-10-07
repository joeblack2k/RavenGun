#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

const char* to_string(GunButton b) {
    switch (b) {
        case GunButton::Trigger: return "trigger";
        case GunButton::Reload: return "reload";
        case GunButton::Grenade: return "grenade";
        case GunButton::Aux1: return "aux1";
        case GunButton::Aux2: return "aux2";
    }
    return "?";
}

bool parse_button(const std::string& name, GunButton& b) {
    for (int i = 0; i < kGunButtonCount; ++i) {
        if (name == to_string(static_cast<GunButton>(i))) {
            b = static_cast<GunButton>(i);
            return true;
        }
    }
    return false;
}

bool GunOutput::arm(std::string& err) {
    if (armed_) return true;
    if (!dev_.created()) {
        std::string e;
        if (!dev_.create(e)) {
            last_error_ = e.empty() ? "apparaat aanmaken mislukt" : e;
            err = last_error_;
            return false;
        }
    }
    kbd_error_.clear();
    if (kbd_ && !kbd_->created()) {
        // Het toetsenbord hoort te bestaan zodra de gun bestaat (TeknoParrot opent evdev-apparaten bij de gamestart).
        std::string e;
        if (!kbd_->create(e)) kbd_error_ = e.empty() ? "toetsenbord aanmaken mislukt" : e;
    }
    down_.fill(false);
    keys_down_.clear();
    have_last_ = false;  // het apparaat kent nog geen positie; de eerste positie wordt altijd verstuurd
    armed_ = true;
    ++stats_.arms;
    last_error_.clear();
    return true;
}

void GunOutput::release_all() {
    bool any = false;
    for (int code : keys_down_) {
        any = true;
        if (!kbd_ || !kbd_->write_key(code, false)) ++stats_.write_errors;
    }
    keys_down_.clear();  // intern altijd los, ook als de write faalt
    if (!dev_.created()) { down_.fill(false); if (any) ++stats_.releases; return; }
    for (int i = 0; i < kGunButtonCount; ++i) {
        if (!down_[static_cast<size_t>(i)]) continue;
        any = true;
        down_[static_cast<size_t>(i)] = false;  // intern altijd los, ook als de write faalt
        if (!dev_.write_button(static_cast<GunButton>(i), false)) ++stats_.write_errors;
    }
    if (any) ++stats_.releases;
}

void GunOutput::disarm() {
    release_all();
    armed_ = false;
}

void GunOutput::shutdown() {
    disarm();
    if (kbd_) kbd_->destroy();
    dev_.destroy();  // ook zonder events: de kernel geeft eventuele resterende toestand vrij
    have_last_ = false;
}

void GunOutput::fail(const char* what) {
    ++stats_.write_errors;
    last_error_ = what;
    release_all();   // best effort; schrijft naar een mogelijk kapot apparaat
    armed_ = false;
    if (kbd_) kbd_->destroy();
    dev_.destroy();  // verwijderen geeft alle toestand vrij; een nieuwe arm maakt het opnieuw aan
    have_last_ = false;
}

bool GunOutput::set_position(double nx, double ny) {
    if (!armed_) { ++stats_.rejected; return false; }
    auto p = to_abs(t_, nx, ny);
    if (!p) { ++stats_.rejected; return false; }
    last_nx_ = nx;
    last_ny_ = ny;
    last_valid_ = true;
    if (have_last_ && p->x == last_x_ && p->y == last_y_) return true;  // niets te melden
    if (!dev_.write_position(p->x, p->y)) { fail("schrijven van positie mislukt"); return false; }
    last_x_ = p->x;
    last_y_ = p->y;
    have_last_ = true;
    ++stats_.position_events;
    return true;
}

bool GunOutput::set_button(GunButton b, bool down) {
    if (!armed_) { ++stats_.rejected; return false; }
    size_t i = static_cast<size_t>(b);
    if (down_[i] == down) return true;
    if (!dev_.write_button(b, down)) { fail("schrijven van knop mislukt"); return false; }
    down_[i] = down;
    ++stats_.button_events;
    return true;
}

bool GunOutput::set_key(int code, bool down) {
    if (!armed_ || !kbd_) { ++stats_.rejected; return false; }
    if ((keys_down_.count(code) != 0) == down) return true;
    if (!kbd_->write_key(code, down)) { fail("schrijven van toets mislukt"); return false; }
    if (down) keys_down_.insert(code); else keys_down_.erase(code);
    ++stats_.button_events;
    return true;
}

}  // namespace ravengun::output
