#pragma once
// Unity-route in de daemon: als een game van dit type actief is (IPC `aagames start <game>`), verbindt een klant met de
// BepInEx-plugin in de game (127.0.0.1:33610) en stuurt de aim en knoppen van de spelers als invoerpakket. De uitvoer van de
// plugin (Recoil) wordt een rumble-puls van die speler; ammo, leven en credits staan in de status. Zie
// ravengun/games/aagames.hpp voor het protocol.
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ravengun/games/aagames.hpp"
#include "shared.hpp"

namespace ravengund {

class AagamesService {
public:
    AagamesService(SharedState& sh, ravengun::games::AaConfig cfg);
    ~AagamesService();
    AagamesService(const AagamesService&) = delete;
    AagamesService& operator=(const AagamesService&) = delete;

    // Leeg = gelukt, anders de Nederlandse foutmelding (onbekende game).
    std::string start(const std::string& game_key);
    void stop();
    bool active() const;
    // {"active":false,"games":[...]} of de status van de klant met "active":true.
    std::string status_json() const;
    const ravengun::games::AaConfig& config() const { return cfg_; }
    // De route stopt vanzelf als de game weg is (de verbinding was er en is langer dan grace_ms weg) of als de game nooit
    // verbond binnen never_ms. Zo blijft de rumble niet aan de game gebonden nadat hij met Esc is afgesloten.
    void set_timeouts(int grace_ms, int never_ms) { std::lock_guard<std::mutex> lk(m_); grace_ms_ = grace_ms; never_ms_ = never_ms; }

private:
    void stop_locked();
    void watch_main();
    SharedState& sh_;
    ravengun::games::AaConfig cfg_;
    mutable std::mutex m_;
    std::unique_ptr<ravengun::games::AaClient> client_;
    int grace_ms_ = 8000, never_ms_ = 15 * 60 * 1000;
    bool quit_ = false;
    std::condition_variable cv_;
    std::thread watch_;
};

}  // namespace ravengund
