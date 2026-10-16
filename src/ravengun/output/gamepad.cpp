#include "ravengun/output/gamepad.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "ravengun/output/key_table.hpp"
#include "ravengun/strutil.hpp"

namespace ravengun::output {

namespace {

constexpr const char* kNames[kPadControls] = {"A",     "B",    "X",     "Y",  "LB", "RB",     "LT",       "RT",       "Start",     "Back",      "Guide",
                                              "LS",    "RS",   "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "LeftX", "LeftY", "RightX", "RightY"};

size_t ix(PadControl c) { return static_cast<size_t>(c); }

// Zelfde namen als to_string(GunButton), hier los zodat de app dit bestand zonder gun_output.cpp kan gebruiken.
constexpr const char* kGunNames[kGunButtonCount] = {"trigger", "reload", "grenade", "aux1", "aux2"};

// evdev-knop -> bediening (namen zoals op een Xbox-controller; xpad en hid-steam melden X als BTN_X en Y als BTN_Y).
bool button_control(uint16_t code, PadControl& c) {
    switch (code) {
        case BTN_SOUTH: c = PadControl::A; return true;
        case BTN_EAST: c = PadControl::B; return true;
        case BTN_X: c = PadControl::X; return true;
        case BTN_Y: c = PadControl::Y; return true;
        case BTN_TL: c = PadControl::LB; return true;
        case BTN_TR: c = PadControl::RB; return true;
        case BTN_TL2: c = PadControl::LT; return true;
        case BTN_TR2: c = PadControl::RT; return true;
        case BTN_SELECT: c = PadControl::Back; return true;
        case BTN_START: c = PadControl::Start; return true;
        case BTN_MODE: c = PadControl::Guide; return true;
        case BTN_THUMBL: c = PadControl::LS; return true;
        case BTN_THUMBR: c = PadControl::RS; return true;
        case BTN_DPAD_UP: c = PadControl::DPadUp; return true;
        case BTN_DPAD_DOWN: c = PadControl::DPadDown; return true;
        case BTN_DPAD_LEFT: c = PadControl::DPadLeft; return true;
        case BTN_DPAD_RIGHT: c = PadControl::DPadRight; return true;
        default: return false;
    }
}

bool is_stick(int code) { return code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY; }
bool is_trigger(int code) { return code == ABS_Z || code == ABS_RZ || code == ABS_GAS || code == ABS_BRAKE; }
bool is_hat(int code) { return code == ABS_HAT0X || code == ABS_HAT0Y; }

// Stickuitslag -1..1 na de dode zone, als 0..1 met het midden op 0,5.
double stick_value(double d) {
    const double a = std::fabs(d);
    if (a < kPadDeadZone) return 0.5;
    const double r = std::min(1.0, (a - kPadDeadZone) / (1.0 - kPadDeadZone));
    return 0.5 + (d < 0 ? -r : r) / 2.0;
}

}  // namespace

const char* pad_control_name(PadControl c) {
    const int i = static_cast<int>(c);
    return i >= 0 && i < kPadControls ? kNames[i] : "?";
}

bool pad_control_from_name(const std::string& name, PadControl& out) {
    const std::string n = lower(trim(name));
    for (int i = 0; i < kPadControls; ++i)
        if (n == lower(kNames[i])) { out = static_cast<PadControl>(i); return true; }
    return false;
}

PadKind pad_control_kind(PadControl c) {
    switch (c) {
        case PadControl::LT: case PadControl::RT: return PadKind::Trigger;
        case PadControl::LeftX: case PadControl::LeftY: case PadControl::RightX: case PadControl::RightY: return PadKind::Stick;
        default: return PadKind::Button;
    }
}

void PadState::reset() {
    for (int i = 0; i < kPadControls; ++i) v[static_cast<size_t>(i)] = pad_control_kind(static_cast<PadControl>(i)) == PadKind::Stick ? 0.5 : 0.0;
}

bool PadState::pressed(PadControl c) const {
    const double x = value(c);
    switch (pad_control_kind(c)) {
        case PadKind::Button: return x >= 0.5;
        case PadKind::Trigger: return x >= kPadButtonThreshold;
        case PadKind::Stick: return std::fabs(x - 0.5) * 2.0 >= kPadStickThreshold;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// PadReader

PadReader::PadReader() { reset(); }

void PadReader::reset() {
    abs_.fill(0.0);
    btn_.fill(false);
    state_.reset();
    dropped_ = false;
}

void PadReader::set_range(int code, PadAxisRange r) {
    if (code >= 0 && code < static_cast<int>(range_.size())) range_[static_cast<size_t>(code)] = r;
}

double PadReader::norm(int code, int32_t value) const {
    PadAxisRange r = range_[static_cast<size_t>(code)];
    if (!r.valid()) {
        if (is_stick(code)) r = {-32768, 32767};
        else if (is_hat(code)) r = {-1, 1};
        else r = {0, 255};
    }
    const double t = std::clamp((static_cast<double>(value) - r.min) / (static_cast<double>(r.max) - r.min), 0.0, 1.0);
    if (is_trigger(code)) return t;
    if (is_hat(code)) return value < (r.min + r.max) / 2.0 - 0.25 ? -1.0 : value > (r.min + r.max) / 2.0 + 0.25 ? 1.0 : 0.0;
    return t * 2.0 - 1.0;   // stick: -1..1
}

void PadReader::compose() {
    PadState s;
    for (int i = 0; i < kPadControls; ++i)
        if (pad_control_kind(static_cast<PadControl>(i)) == PadKind::Button) s.v[static_cast<size_t>(i)] = btn_[static_cast<size_t>(i)] ? 1.0 : 0.0;
    s.v[ix(PadControl::LeftX)] = stick_value(abs_[ABS_X]);
    s.v[ix(PadControl::LeftY)] = stick_value(abs_[ABS_Y]);
    s.v[ix(PadControl::RightX)] = stick_value(abs_[ABS_RX]);
    s.v[ix(PadControl::RightY)] = stick_value(abs_[ABS_RY]);
    // Triggers: analoog (ABS_Z/ABS_RZ bij xpad en hid-steam, ABS_BRAKE/ABS_GAS bij sommige Bluetooth-pads) of digitaal (BTN_TL2/TR2).
    s.v[ix(PadControl::LT)] = std::max({abs_[ABS_Z], abs_[ABS_BRAKE], btn_[ix(PadControl::LT)] ? 1.0 : 0.0});
    s.v[ix(PadControl::RT)] = std::max({abs_[ABS_RZ], abs_[ABS_GAS], btn_[ix(PadControl::RT)] ? 1.0 : 0.0});
    // D-pad: als hat (de meeste pads) of als BTN_DPAD_*.
    auto dp = [&](PadControl c, bool hat) { s.v[ix(c)] = (btn_[ix(c)] || hat) ? 1.0 : 0.0; };
    dp(PadControl::DPadUp, abs_[ABS_HAT0Y] < 0);
    dp(PadControl::DPadDown, abs_[ABS_HAT0Y] > 0);
    dp(PadControl::DPadLeft, abs_[ABS_HAT0X] < 0);
    dp(PadControl::DPadRight, abs_[ABS_HAT0X] > 0);
    state_ = s;
}

bool PadReader::event(uint16_t type, uint16_t code, int32_t value) {
    if (type == EV_SYN) {
        if (code == SYN_DROPPED) {
            dropped_ = true;
            abs_.fill(0.0);
            btn_.fill(false);
            return false;
        }
        if (code != SYN_REPORT) return false;
        if (dropped_) {
            // Tot hier niet te vertrouwen: alles los en in het midden.
            dropped_ = false;
            abs_.fill(0.0);
            btn_.fill(false);
        }
        const PadState before = state_;
        compose();
        return state_ != before;
    }
    if (dropped_) return false;
    if (type == EV_KEY) {
        PadControl c;
        if (button_control(code, c)) btn_[ix(c)] = value != 0;   // 2 = herhaling: blijft ingedrukt
    } else if (type == EV_ABS && code < abs_.size() && (is_stick(code) || is_trigger(code) || is_hat(code))) {
        abs_[code] = norm(code, value);
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Indeling

bool parse_pad_target(const std::string& text, PadTarget& out, std::string& err) {
    const std::string t = lower(trim(text));
    out = {};
    if (t == "none" || t.empty()) return true;
    if (t == "wheel") { out.kind = PadTargetKind::Wheel; return true; }
    if (t == "gas") { out.kind = PadTargetKind::Gas; return true; }
    if (t == "brake") { out.kind = PadTargetKind::Brake; return true; }
    if (t == "start") { out.kind = PadTargetKind::Start; return true; }
    if (t == "coin") { out.kind = PadTargetKind::Coin; return true; }
    for (int i = 0; i < kGunButtonCount; ++i)
        if (t == kGunNames[i]) { out.kind = PadTargetKind::Gun; out.index = i; return true; }
    if (t.size() == 4 && t.compare(0, 3, "btn") == 0 && t[3] >= '1' && t[3] < '1' + iobridge::kIoPadButtons) {
        out.kind = PadTargetKind::Btn;
        out.index = t[3] - '1';
        return true;
    }
    if (t.compare(0, 4, "key:") == 0) {
        const int code = key_code(t.substr(4));
        if (code < 0) { err = "onbekende toets '" + trim(text).substr(4) + "'"; return false; }
        out.kind = PadTargetKind::Key;
        out.index = code;
        return true;
    }
    err = "onbekend doel '" + trim(text) + "' (wheel, gas, brake, trigger, reload, grenade, aux1, aux2, start, coin, btn1..btn8, key:<toets> of none)";
    return false;
}

std::string pad_target_name(const PadTarget& t) {
    switch (t.kind) {
        case PadTargetKind::None: return "none";
        case PadTargetKind::Wheel: return "wheel";
        case PadTargetKind::Gas: return "gas";
        case PadTargetKind::Brake: return "brake";
        case PadTargetKind::Start: return "start";
        case PadTargetKind::Coin: return "coin";
        case PadTargetKind::Gun: return kGunNames[t.index];
        case PadTargetKind::Btn: return "btn" + std::to_string(t.index + 1);
        case PadTargetKind::Key: return std::string("key:") + key_name(t.index);
    }
    return "none";
}

bool pad_target_allowed(PadControl c, const PadTarget& t) {
    return !(t.kind == PadTargetKind::Wheel && pad_control_kind(c) == PadKind::Button);
}

GamepadMap default_gamepad_map() {
    GamepadMap m;
    auto set = [&](PadControl c, PadTargetKind k, int index = 0) { m.to[ix(c)] = PadTarget{k, index}; };
    set(PadControl::LeftX, PadTargetKind::Wheel);
    set(PadControl::RT, PadTargetKind::Gas);
    set(PadControl::LT, PadTargetKind::Brake);
    set(PadControl::A, PadTargetKind::Gun, static_cast<int>(GunButton::Trigger));
    set(PadControl::B, PadTargetKind::Gun, static_cast<int>(GunButton::Reload));
    set(PadControl::X, PadTargetKind::Gun, static_cast<int>(GunButton::Grenade));
    set(PadControl::Y, PadTargetKind::Gun, static_cast<int>(GunButton::Aux1));
    set(PadControl::LB, PadTargetKind::Btn, 0);
    set(PadControl::RB, PadTargetKind::Btn, 1);
    set(PadControl::Start, PadTargetKind::Start);
    set(PadControl::Back, PadTargetKind::Coin);
    return m;
}

GamepadMap gamepad_map_from(const std::vector<std::pair<std::string, std::string>>& kv, std::vector<std::string>* warnings, const GamepadMap& base) {
    GamepadMap m = base;
    for (const auto& e : kv) {
        PadControl c;
        if (!pad_control_from_name(e.first, c)) {
            if (warnings) warnings->push_back("[Gamepad] " + e.first + ": onbekende bediening (A, B, X, Y, LB, RB, LT, RT, Start, Back, Guide, LS, RS, DPadUp/Down/Left/Right, LeftX, LeftY, RightX, RightY)");
            continue;
        }
        PadTarget t;
        std::string err;
        if (!parse_pad_target(e.second, t, err)) {
            if (warnings) warnings->push_back("[Gamepad] " + e.first + ": " + err);
            continue;
        }
        if (!pad_target_allowed(c, t)) {
            if (warnings) warnings->push_back("[Gamepad] " + e.first + " = " + e.second + ": een knop kan geen stuur zijn (gebruik een stick of trigger)");
            continue;
        }
        m.to[ix(c)] = t;
    }
    return m;
}

std::vector<std::pair<std::string, std::string>> describe_gamepad_map(const GamepadMap& m) {
    std::vector<std::pair<std::string, std::string>> out;
    for (int i = 0; i < kPadControls; ++i) {
        const PadTarget& t = m.to[static_cast<size_t>(i)];
        if (t.kind != PadTargetKind::None) out.emplace_back(kNames[i], pad_target_name(t));
    }
    return out;
}

PadOutput apply_gamepad_map(const GamepadMap& m, const PadState& s) {
    PadOutput o;
    double wheel_dev = -1;
    for (int i = 0; i < kPadControls; ++i) {
        const PadControl c = static_cast<PadControl>(i);
        const PadTarget& t = m.to[static_cast<size_t>(i)];
        const double v = s.value(c);
        const PadKind k = pad_control_kind(c);
        // Analoog als 0..1: een stick naar gas of rem geeft zijn uitslag.
        const double amount = k == PadKind::Stick ? std::fabs(v - 0.5) * 2.0 : v;
        const bool on = s.pressed(c);
        switch (t.kind) {
            case PadTargetKind::None: break;
            case PadTargetKind::Wheel:
                if (k == PadKind::Button) break;
                o.wheel_mapped = true;
                if (std::fabs(v - 0.5) > wheel_dev) { wheel_dev = std::fabs(v - 0.5); o.wheel = v; }
                break;
            case PadTargetKind::Gas: o.gas_mapped = true; o.gas = std::max(o.gas, amount); break;
            case PadTargetKind::Brake: o.brake_mapped = true; o.brake = std::max(o.brake, amount); break;
            case PadTargetKind::Gun: o.gun[static_cast<size_t>(t.index)] = o.gun[static_cast<size_t>(t.index)] || on; break;
            case PadTargetKind::Start: o.start = o.start || on; break;
            case PadTargetKind::Coin: o.coin = o.coin || on; break;
            case PadTargetKind::Btn: o.btn[static_cast<size_t>(t.index)] = o.btn[static_cast<size_t>(t.index)] || on; break;
            case PadTargetKind::Key: {
                auto it = std::find_if(o.keys.begin(), o.keys.end(), [&](const auto& kv) { return kv.first == t.index; });
                if (it == o.keys.end()) o.keys.emplace_back(t.index, on);
                else it->second = it->second || on;
                break;
            }
        }
    }
    return o;
}

}  // namespace ravengun::output
