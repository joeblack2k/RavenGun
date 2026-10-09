#pragma once
// Eén virtueel toetsenbord voor alle spelers (1209:7201 blijft één apparaat). Elke speler krijgt een eigen KeyPort, het
// KeyDevice van zijn GunOutput. Een toets is ingedrukt zolang minstens één speler hem vasthoudt (bijvoorbeeld Coin = 5 bij
// twee spelers tegelijk): pas als de laatste hem loslaat gaat er een up-event naar het echte apparaat. Een port vrijgeven
// (disarm, fout, bronuitval) laat alleen de toetsen van die speler los. Niet thread-safe: de eigenaar (OutputService)
// serialiseert met zijn mutex. Getest in tests/test_output.cpp.
#include <array>
#include <map>
#include <set>
#include <string>

#include "ravengun/output/uinput_kbd.hpp"

namespace ravengun::output {

class KeyMux {
public:
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
        void release();
    private:
        KeyMux* mux_ = nullptr;
        bool created_ = false;
        std::set<int> held_;
    };
    bool press(int code);
    bool unpress(int code);

    KeyDevice& real_;
    std::array<Port, kPorts> ports_;
    std::map<int, int> count_;
};

}  // namespace ravengun::output
