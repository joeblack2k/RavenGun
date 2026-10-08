#pragma once
// Outputservice van ravengund: eigenaar van het virtuele lightgun-apparaat (uinput), de
// arm/disarm-toestand en de testpatronen. Standaard disarmed: er worden geen events verstuurd.
// De daemon leest zijn eigen apparaat nooit (geen feedbackloop).
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/output/aim_clock.hpp"
#include "ravengun/output/gun_output.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/output/test_pattern.hpp"
#include "ravengun/output/uinput_gun.hpp"
#include "ravengun/output/uinput_kbd.hpp"

namespace ravengund {

struct OutputResult {
    bool ok = true;
    std::string code;     // IPC-foutcode: unavailable, invalid_argument, internal
    std::string message;  // Nederlands
};

class OutputService {
public:
    OutputService();
    ~OutputService();  // shutdown()

    OutputResult arm();
    OutputResult disarm();                  // stopt een lopend patroon, geeft alle knoppen vrij
    OutputResult start_pattern(const ravengun::output::PatternParams& p);  // vereist armed
    OutputResult stop_pattern();
    OutputResult button(ravengun::output::GunButton b, const std::string& action);  // down|up|tap (vereist armed)
    OutputResult set_transform(const ravengun::output::GunTransform& t);

    // Geeft alle knoppen vrij (bronuitval) zonder te disarmen.
    void release_all();
    // Fase 4: de live Move (HID-thread, per sample). Alleen als de uitvoer gewapend is en er geen testpatroon
    // loopt; release_all werkt altijd. Zie ravengun/output/live_feed.hpp.
    void feed_live(const ravengun::output::LiveAction& a);
    bool armed() const;
    // Uitvoerklok (fase 6): live instelbaar; hz = 0 zet hem uit (positie alleen per rapport).
    ravengun::output::OutputClockConfig clock_config() const;
    void set_clock(const ravengun::output::OutputClockConfig& c);
    // Actieve knoppenindeling (voor status/startlog); de live koppeling zelf zit in de HID-thread.
    void set_layout(const std::vector<std::pair<std::string, std::string>>& layout);
    // Stopt patroon, geeft knoppen vrij en verwijdert het apparaat. Idempotent.
    void shutdown();

    ravengun::output::GunTransform transform() const;
    std::string status_json() const;

    // Startwaarden voor de transformatie (uit de opdrachtregel van de daemon).
    void set_initial_transform(const ravengun::output::GunTransform& t);

private:
    void pattern_main(std::vector<ravengun::output::PatternStep> steps, ravengun::output::PatternParams p, uint64_t run_id);
    void stop_pattern_locked(std::unique_lock<std::mutex>& lk);
    void clock_main();

    mutable std::mutex m_;
    std::condition_variable cv_;
    ravengun::output::UinputGun dev_;
    ravengun::output::UinputKeyboard kbd_;
    std::vector<std::pair<std::string, std::string>> layout_;
    ravengun::output::GunOutput out_;
    // Uitvoerklok: eigen thread, schrijft onder m_ de geëxtrapoleerde positie tussen de rapporten (zie aim_clock.hpp).
    ravengun::output::AimClock clock_;
    std::thread clock_thread_;
    std::condition_variable clock_cv_;
    bool clock_changed_ = false;
    bool clock_idle_ = false;   // vastgehouden na de extrapolatielimiet: de klokthread slaapt tot het volgende rapport
    uint64_t clock_ticks_ = 0, clock_extrapolated_ = 0, clock_held_ = 0, clock_events_ = 0;

    std::thread pattern_thread_;
    bool pattern_running_ = false;
    bool pattern_stop_ = false;
    uint64_t pattern_run_ = 0;
    ravengun::output::PatternParams pattern_params_;
    size_t pattern_steps_total_ = 0;
    size_t pattern_step_ = 0;
    double pattern_length_s_ = 0;
    std::string pattern_state_ = "none";  // none, running, done, stopped
    bool shut_ = false;
    // Live-bron (fase 4)
    uint64_t live_samples_ = 0, live_positions_ = 0, live_invalid_ = 0, live_releases_ = 0, live_suspended_ = 0;
    bool live_aim_valid_ = false;
};

}  // namespace ravengund
