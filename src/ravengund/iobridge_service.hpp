#pragma once
// IO-brug in de daemon: een toestandsmodel (ravengun/iobridge/io_state.hpp) dat uit de bestaande pijplijn komt (aim na
// transformatie, voorspelling en uitvoerklok, knoppen en toetsen van de virtuele apparaten), en dialecten die het naar
// het formaat van een game vertalen: tp-shm (gedeeld geheugen van de TeknoParrot-kern) en unity-tcp (klant van de plugin
// in Unity-games, via AagamesService). Per game staat de route in packaging/io-routes.json. Zie docs/architecture.md,
// "IO-brug", en docs/decisions.md, "IO-brug: tp-shm".
//
// Tik: een eigen thread van 250 Hz (zelfde klok als de uitvoer) leest OutputService::io_snapshot en schrijft de
// toestand naar het actieve dialect. Herstart van de daemon: de route staat in een toestandsbestand in
// $XDG_RUNTIME_DIR; bij het opstarten pakt de brug hem weer op als de game nog draait (unit ravengun-tpui), en het
// bestand met de invoer blijft staan, zodat de game niets merkt behalve hooguit een paar milliseconden neutrale invoer.
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ravengun/iobridge/dialect.hpp"
#include "ravengun/iobridge/routes.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "shared.hpp"

namespace ravengund {

class IoBridgeService {
public:
    struct Config {
        ravengun::iobridge::IoRoutes routes;
        ravengun::iobridge::TpShmProfiles profiles;
        std::string state_path;       // toestandsbestand voor herstart (leeg = geen)
        std::string tpui_procs;       // cgroup.procs van ravengun-tpui.service (leeg = geen bewaking)
        std::string shm_path = ravengun::iobridge::kDefaultTpShmPath;
        std::string tpui_shm_path = ravengun::iobridge::kTpuiShmPath;
        int tick_hz = 250;
        int grace_ms = 8000;          // zo lang na het verdwijnen van de game stopt de route
        int never_ms = 10 * 60 * 1000; // zo lang wachten op een game die niet verschijnt
    };

    IoBridgeService(SharedState& sh, Config cfg);
    ~IoBridgeService() { shutdown(); }   // zet de invoer neutraal en stopt de tikthread; de route blijft in het toestandsbestand staan
    IoBridgeService(const IoBridgeService&) = delete;
    IoBridgeService& operator=(const IoBridgeService&) = delete;

    // Start de route van `game` (of `route_override`: evdev | tp-shm | unity-tcp). Leeg = gelukt, anders de melding.
    // evdev stopt een actieve brug. unity-tcp start de Unity-route via AagamesService.
    std::string start(const std::string& game, const std::string& route_override = {});
    // Stopt de tikthread en zet de invoer neutraal; de route blijft in het toestandsbestand (herstart). Idempotent.
    void shutdown();
    // Stopt de route: neutraal, dialect dicht, toestandsbestand weg. Idempotent.
    void stop();
    bool active() const;
    std::string active_route() const;
    std::string route_for(const std::string& game) const { return cfg_.routes.route_for(game); }
    std::string status_json() const;
    // Na een herstart van de daemon: pakt een eerdere tp-shm-route weer op als de game nog draait.
    bool resume();

    // De Unity-route (AagamesService) meldt zich hier voor de status (route en game; de details staan onder "aagames");
    // de brug bezit dat dialect niet.
    void set_external(const std::string& route, const std::string& game);
    void clear_external(const std::string& route);
    // De toestand zoals de brug hem nu uitleest (voor het Unity-dialect en tests).
    bool snapshot(ravengun::iobridge::IoState& s) const;
    // Voor tests: één tik zonder thread.
    void tick_once();
    const Config& config() const { return cfg_; }

private:
    void main_loop();
    void write_state_file(const std::string& route, const std::string& game);
    void stop_locked();
    SharedState& sh_;
    Config cfg_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::unique_ptr<ravengun::iobridge::IoDialect> dialect_;
    std::string game_, route_;
    bool quit_ = false;
    std::thread th_;
    uint64_t ticks_ = 0, resumes_ = 0;
    bool seen_alive_ = false;
    int64_t started_ms_ = 0, gone_since_ms_ = 0;
    // extern (unity-tcp)
    std::string ext_route_, ext_game_;
};

}  // namespace ravengund
