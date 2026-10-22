#include "ravengun/output/rumble.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::output {

std::string validate_rumble(const RumbleConfig& c) {
    if (c.strength < 0 || c.strength > 255) return "rumble.strength moet tussen 0 en 255 liggen";
    if (c.pulse_ms < 10 || c.pulse_ms > 500) return "rumble.pulse_ms moet tussen 10 en 500 liggen";
    if (!(c.repeat_hz == 0 || (c.repeat_hz >= 1 && c.repeat_hz <= 30))) return "rumble.repeat_hz moet 0 (geen herhaling) zijn of tussen 1 en 30 liggen";
    if (c.pump_strength < 0 || c.pump_strength > 255) return "rumble.pump_strength moet tussen 0 en 255 liggen";
    return {};
}

void RumbleEngine::set_config(const RumbleConfig& c) {
    cfg_ = c;
    if (!cfg_.enabled) { shot_until_ = pump_until_ = test_until_ = 0; value_ = 0; }
}

void RumbleEngine::reset() {
    prev_trigger_ = prev_pump_ = false;
    shot_until_ = pump_until_ = test_until_ = next_repeat_ = 0;
    repeating_ = false;
    value_ = 0;
}

void RumbleEngine::start_shot(int64_t now_ns) {
    shot_until_ = now_ns + static_cast<int64_t>(cfg_.pulse_ms) * 1'000'000;
    ++pulses_;
}

uint8_t RumbleEngine::update(int64_t now_ns, const RumbleInput& in) {
    const bool trig_edge = in.trigger && !prev_trigger_;
    const bool pump_edge = in.pump && !prev_pump_;
    prev_trigger_ = in.trigger;
    prev_pump_ = in.pump;

    if (!cfg_.enabled || !in.connected) {
        shot_until_ = pump_until_ = test_until_ = 0;
        repeating_ = false;
        return value_ = 0;
    }
    if (!in.armed) {
        shot_until_ = pump_until_ = 0;  // een testpuls mag ook ongewapend uitlopen
        repeating_ = false;
    } else {
        if (!in.trigger) repeating_ = false;
        const int64_t period = cfg_.repeat_hz > 0 ? static_cast<int64_t>(1e9 / cfg_.repeat_hz) : 0;
        if (trig_edge) {
            start_shot(now_ns);
            next_repeat_ = now_ns + period;
            repeating_ = true;
        } else if (in.trigger && repeating_ && period > 0 && now_ns >= next_repeat_) {
            start_shot(now_ns);
            next_repeat_ += period;
            if (next_repeat_ <= now_ns) next_repeat_ = now_ns + period;  // lang niet aangeroepen: geen inhaalsalvo
        }
        if (pump_edge && cfg_.pump_strength > 0) {
            pump_until_ = now_ns + static_cast<int64_t>(std::max(20, cfg_.pulse_ms / 2)) * 1'000'000;
            ++pulses_;
        }
    }
    int v = 0;
    if (now_ns < shot_until_) v = std::max(v, cfg_.strength);
    if (now_ns < pump_until_) v = std::max(v, cfg_.pump_strength);
    if (now_ns < test_until_) v = std::max(v, static_cast<int>(test_value_));
    if (in.armed && in.hold > 0) v = std::max(v, static_cast<int>(std::lround(std::min(in.hold, 1.0) * cfg_.strength)));
    return value_ = static_cast<uint8_t>(std::clamp(v, 0, 255));
}

bool RumbleEngine::test(int64_t now_ns, int ms) {
    if (!cfg_.enabled) return false;
    ms = std::clamp(ms, 10, 1000);
    test_value_ = static_cast<uint8_t>(cfg_.strength > 0 ? cfg_.strength : 180);
    test_until_ = now_ns + static_cast<int64_t>(ms) * 1'000'000;
    ++pulses_;
    return true;
}

bool ReportGate::due(int64_t now_ns, uint8_t want, bool other_due) const {
    if (now_ns - last_attempt_ < min_gap_ns_) return false;
    return want != last_sent_ || other_due;
}

void ReportGate::sent(int64_t now_ns, uint8_t value, bool ok) {
    last_attempt_ = now_ns;
    if (!ok) return;
    ++reports_;
    if (value != last_sent_) ++rumble_reports_;
    last_sent_ = value;
}

}  // namespace ravengun::output
