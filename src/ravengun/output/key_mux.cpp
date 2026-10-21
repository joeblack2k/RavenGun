#include "ravengun/output/key_mux.hpp"

#include <algorithm>

namespace ravengun::output {

bool KeyMux::press(int code) {
    int& n = count_[code];
    if (n++ > 0) return true;
    if (real_.write_key(code, true)) return true;
    n = 0;  // het echte apparaat heeft hem niet: niet als ingedrukt tellen
    return false;
}

bool KeyMux::unpress(int code) {
    auto it = count_.find(code);
    if (it == count_.end() || it->second <= 0) return true;
    if (--it->second > 0) return true;
    count_.erase(it);
    return real_.write_key(code, false);
}

void KeyMux::shutdown() {
    for (auto& p : ports_) p.destroy();
    count_.clear();
    real_.destroy();
}

bool KeyMux::Port::create(std::string& err) {
    if (!mux_->real_.created() && !mux_->real_.create(err)) return false;
    created_ = true;
    return true;
}

int64_t KeyMux::now() const {
    if (clock_) return clock_();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void KeyMux::poll(int64_t t) {
    for (auto& p : ports_) p.poll(t);
}

int64_t KeyMux::next_due() const {
    int64_t d = kNever;
    for (const auto& p : ports_) d = std::min(d, p.next_due());
    return d;
}

void KeyMux::Port::release() {
    sched_.clear();   // a release is a safety action (disarm, fault, source loss): no delayed events survive it
    for (int c : held_) mux_->unpress(c);
    held_.clear();
}

void KeyMux::Port::destroy() {
    release();
    created_ = false;
}

bool KeyMux::Port::emit(int code, bool down) {
    if (down) {
        if (held_.count(code)) return true;
        if (!mux_->press(code)) return false;
        held_.insert(code);
        return true;
    }
    if (!held_.count(code)) return true;
    held_.erase(code);
    return mux_->unpress(code);
}

bool KeyMux::Port::write_key(int code, bool down) {
    if (!created()) return false;
    const KeyTiming& tm = mux_->timing_;
    if (tm.min_hold_ns <= 0 && tm.min_gap_ns <= 0 && sched_.empty()) return emit(code, down);   // timing off: as before
    Sched& s = sched_[code];
    if (s.want_down == down) return true;   // nothing new for the caller
    const int64_t now = mux_->now();
    // Earliest moment this event may reach the device: a release waits for min_hold after the press, a press waits for
    // min_gap after the release (both measured against the previous event of this key, even if that one is still queued).
    const int64_t due = std::max(now, down ? s.last_up + tm.min_gap_ns : s.last_down + tm.min_hold_ns);
    if (s.q.empty() && due <= now) {
        if (!emit(code, down)) return false;
        s.want_down = down;
        (down ? s.last_down : s.last_up) = now;
        return true;
    }
    s.want_down = down;
    (down ? s.last_down : s.last_up) = due;
    s.q.emplace_back(down, due);
    if (mux_->wake_) mux_->wake_();
    return true;
}

void KeyMux::Port::poll(int64_t now) {
    for (auto& kv : sched_) {
        Sched& s = kv.second;
        while (!s.q.empty() && s.q.front().second <= now) {
            const bool down = s.q.front().first;
            s.q.pop_front();
            emit(kv.first, down);   // a failed write leaves the key as the device has it; nothing to retry
        }
    }
}

int64_t KeyMux::Port::next_due() const {
    int64_t d = kNever;
    for (const auto& kv : sched_)
        if (!kv.second.q.empty()) d = std::min(d, kv.second.q.front().second);
    return d;
}

}  // namespace ravengun::output
