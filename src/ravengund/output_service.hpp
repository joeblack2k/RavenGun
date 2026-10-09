#pragma once
// Outputservice van ravengund: eigenaar van de virtuele lightguns (uinput, een per speler), het gedeelde virtuele
// toetsenbord, de arm/disarm-toestand en de testpatronen. Standaard disarmed: er worden geen events verstuurd.
// De daemon leest zijn eigen apparaten nooit (geen feedbackloop).
//
// Spelers: de gun van speler 1 bestaat altijd na `arm` (identiteit 1209:7202, ongewijzigd). De gun van speler 2..4 bestaat
// alleen als die speler een Move heeft (set_active_players): dan wordt hij bij `arm` gemaakt, of meteen als de uitvoer al
// gewapend is (een game opent zijn apparaten bij de start). Met één Move verandert er dus niets aan de apparaten.
// Testpatronen en output_button werken op speler 1. De transformatie en de uitvoerklok gelden voor alle spelers.
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/output/aim_clock.hpp"
#include "ravengun/output/gun_output.hpp"
#include "ravengun/output/key_mux.hpp"
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
    static constexpr int kPlayers = ravengun::output::kMaxGunPlayers;
    // Maakt het apparaat van een speler (1..4). Leeg = de echte uinput-gun met gun_identity(player). Tests geven een nep-apparaat.
    using GunFactory = std::function<std::unique_ptr<ravengun::output::GunDevice>(int player)>;

    explicit OutputService(GunFactory guns = {}, std::unique_ptr<ravengun::output::KeyDevice> keyboard = {});
    ~OutputService();  // shutdown()

    OutputResult arm();
    OutputResult disarm();                  // stopt een lopend patroon, geeft alle knoppen vrij
    OutputResult start_pattern(const ravengun::output::PatternParams& p);  // vereist armed (speler 1)
    OutputResult stop_pattern();
    OutputResult button(ravengun::output::GunButton b, const std::string& action);  // down|up|tap (vereist armed; speler 1)
    OutputResult set_transform(const ravengun::output::GunTransform& t);

    // Geeft alle knoppen en toetsen van alle spelers vrij (bronuitval) zonder te disarmen.
    void release_all();
    // De live Move van speler 1 (feed_live(a)) of speler `player` (HID-thread, per sample). Alleen als de uitvoer gewapend is
    // en er geen testpatroon loopt; release_all werkt altijd. Zie ravengun/output/live_feed.hpp.
    void feed_live(const ravengun::output::LiveAction& a) { feed_live(1, a); }
    void feed_live(int player, const ravengun::output::LiveAction& a);
    bool armed() const;
    // Momentopname van de aim en knoppen van een speler, zoals de virtuele gun ze nu heeft (na transformatie, voorspelling en
    // uitvoerklok): voor de Unity-route (aagames_service), die dezelfde aim als pixels naar de game stuurt. u/v zijn de
    // absolute assen van de gun als fractie (0,0 = linksboven). false = speler bestaat niet of de uitvoer is niet gewapend.
    struct AimSnapshot {
        double u = 0.5, v = 0.5;
        bool have_position = false, trigger = false, reload = false;
    };
    bool aim_snapshot(int player, AimSnapshot& s) const;
    // Drukt een toets op het gedeelde virtuele toetsenbord (down|up|tap) voor speler `player`; vereist gewapend. Voor munt,
    // start en test vanuit ravengunctl/de webinterface.
    OutputResult key(int player, int code, const std::string& action);
    // Welke spelers 2..4 een Move hebben (bit p-1 voor speler p; speler 1 telt altijd). Een gewapende uitvoer maakt de gun van
    // een nieuwe speler meteen; een speler die wegvalt houdt zijn apparaat tot het afsluiten (een lopende game blijft het zien).
    void set_active_players(unsigned mask);
    unsigned active_players() const;
    // Uitvoerklok (fase 6): live instelbaar; hz = 0 zet hem uit (positie alleen per rapport).
    ravengun::output::OutputClockConfig clock_config() const;
    void set_clock(const ravengun::output::OutputClockConfig& c);
    // Actieve knoppenindeling (voor status/startlog) van speler 1 of `player`; de live koppeling zelf zit in de HID-thread.
    void set_layout(const std::vector<std::pair<std::string, std::string>>& layout) { set_layout(1, layout); }
    void set_layout(int player, const std::vector<std::pair<std::string, std::string>>& layout);
    // Stopt patroon, geeft knoppen vrij en verwijdert de apparaten. Idempotent.
    void shutdown();

    ravengun::output::GunTransform transform() const;
    std::string status_json() const;

    // Startwaarden voor de transformatie (uit de opdrachtregel van de daemon).
    void set_initial_transform(const ravengun::output::GunTransform& t);

private:
    struct Player {
        int number = 1;
        ravengun::output::GunIdentity id;
        std::unique_ptr<ravengun::output::GunDevice> dev;
        std::unique_ptr<ravengun::output::GunOutput> out;
        ravengun::output::AimClock clock;
        bool clock_idle = false;  // vastgehouden na de extrapolatielimiet: niet tikken tot het volgende rapport
        std::vector<std::pair<std::string, std::string>> layout;
        uint64_t clock_ticks = 0, clock_extrapolated = 0, clock_held = 0, clock_events = 0;
        uint64_t live_samples = 0, live_positions = 0, live_invalid = 0, live_releases = 0, live_suspended = 0;
        bool live_aim_valid = false;
    };
    Player& p1() { return players_[0]; }
    const Player& p1() const { return players_[0]; }
    bool should_exist(int player) const { return player == 1 || (active_ & (1u << (player - 1))) != 0; }
    void arm_player_locked(Player& p);
    void pattern_main(std::vector<ravengun::output::PatternStep> steps, ravengun::output::PatternParams p, uint64_t run_id);
    void stop_pattern_locked(std::unique_lock<std::mutex>& lk);
    void clock_main();
    std::string event_node(const Player& p) const;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::unique_ptr<ravengun::output::KeyDevice> kbd_;
    ravengun::output::KeyMux mux_;
    std::array<Player, kPlayers> players_;
    unsigned active_ = 1;
    bool armed_ = false;
    // Uitvoerklok: eigen thread, schrijft onder m_ de geëxtrapoleerde positie tussen de rapporten (zie aim_clock.hpp).
    ravengun::output::OutputClockConfig clock_cfg_;
    std::thread clock_thread_;
    std::condition_variable clock_cv_;
    bool clock_changed_ = false;

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
};

}  // namespace ravengund
