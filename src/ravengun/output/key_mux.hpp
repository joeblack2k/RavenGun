#pragma once
// Eén virtueel toetsenbord voor alle spelers (1209:7201 blijft één apparaat). Elke speler krijgt een eigen KeyPort, het
// KeyDevice van zijn GunOutput. Een toets is ingedrukt zolang minstens één speler hem vasthoudt (bijvoorbeeld Coin = 5 bij
// twee spelers tegelijk): pas als de laatste hem loslaat gaat er een up-event naar het echte apparaat. Een port vrijgeven
// (disarm, fout, bronuitval) laat alleen de toetsen van die speler los. Niet thread-safe: de eigenaar (OutputService)
// serialiseert met zijn mutex. Getest in tests/test_output.cpp.
//
// Minimum hold time: some games only count a coin or Start from a key that stays down for a few frames (Pull The Trigger
// needs ~300 ms of held key 5 in its own polling; Unity routes sample once per frame), but a PS Move button press is
// often well under 200 ms. With a non-zero KeyTiming every key press of a port is stretched to at least `min_hold_ns`:
// the up event is delayed until `down time + min_hold_ns`. A press that follows the previous release by less than
// `min_gap_ns` is delayed as well, so two quick presses stay two separate presses (two coins) instead of merging into one
// long one. Events of one key keep their order. Everything the mux reports (down(), held_by(), holders()) is the stretched
// state, so the I/O-bridge snapshot and the virtual keyboard agree. A press longer than the minimum, or a press after a
// long pause, passes through at once. Delayed events are emitted by poll(), driven by the owner from next_due().
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "ravengun/output/uinput_kbd.hpp"

namespace ravengun::output {

struct KeyTiming {
    int64_t min_hold_ns = 0;   // 0 = off (every event passes straight through)
    int64_t min_gap_ns = 0;    // minimum time between a release and the next press of the same key
};

class KeyMux {
public:
    using Clock = std::function<int64_t()>;   // monotonic ns
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();
    static constexpr int kPorts = 4;
    explicit KeyMux(KeyDevice& real) : real_(real) {
        for (int i = 0; i < kPorts; ++i) ports_[static_cast<size_t>(i)].bind(this);
    }
    KeyMux(const KeyMux&) = delete;
    KeyMux& operator=(const KeyMux&) = delete;

    // Port van speler 1..4.
    KeyDevice& port(int player) { return ports_[static_cast<size_t>(clamp(player) - 1)]; }
    bool down(int code) const { auto it = count_.find(code); return it != count_.end() && it->second > 0; }
    int holders(int code) const { auto it = count_.find(code); return it == count_.end() ? 0 : it->second; }
    // Houdt speler 1..4 deze toets nu vast?
    bool held_by(int player, int code) const { return ports_[static_cast<size_t>(clamp(player) - 1)].holds(code); }
    // Minimum hold timing (see above). Applies to later events only; call it before the first press.
    void set_timing(const KeyTiming& t) { timing_ = t; }
    const KeyTiming& timing() const { return timing_; }
    // Test hook: replace the monotonic clock (default steady_clock).
    void set_clock(Clock c) { clock_ = std::move(c); }
    int64_t now() const;
    // Called when an event gets delayed (so the owner can wake its timer thread). Runs under the owner's mutex.
    void set_wake(std::function<void()> f) { wake_ = std::move(f); }
    // Emit all delayed events that are due at `now`.
    void poll(int64_t now);
    // Due time (same clock as now()) of the earliest delayed event, kNever if none.
    int64_t next_due() const;
    // Alle ports los en het echte apparaat weg (afsluiten).
    void shutdown();

private:
    static int clamp(int p) { return p < 1 ? 1 : p > kPorts ? kPorts : p; }
    class Port final : public KeyDevice {
    public:
        void bind(KeyMux* m) { mux_ = m; }
        bool create(std::string& err) override;
        void destroy() override;
        bool created() const override { return created_ && mux_->real_.created(); }
        bool write_key(int code, bool down) override;
        void release();   // immediate: drops delayed events and lets go of every held key
        bool holds(int code) const { return held_.count(code) != 0; }
        void poll(int64_t now);
        int64_t next_due() const;
    private:
        // Per key: the state the caller asked for (which may be ahead of the emitted one) with the times it was/will be
        // emitted, and the delayed events in order.
        struct Sched {
            bool want_down = false;
            int64_t last_down = -kNever / 2, last_up = -kNever / 2;
            std::deque<std::pair<bool, int64_t>> q;   // (down, due)
        };
        bool emit(int code, bool down);
        KeyMux* mux_ = nullptr;
        bool created_ = false;
        std::set<int> held_;   // keys emitted to the mux (the stretched state)
        std::map<int, Sched> sched_;
    };
    bool press(int code);
    bool unpress(int code);

    KeyDevice& real_;
    KeyTiming timing_;
    Clock clock_;
    std::function<void()> wake_;
    std::array<Port, kPorts> ports_;
    std::map<int, int> count_;
};

}  // namespace ravengun::output
