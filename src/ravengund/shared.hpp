#pragma once
// Gedeelde toestand tussen de HID-thread (eigenaar van hardware en pijplijn) en de IPC-thread.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <time.h>

#include "ravengun/core/input_model.hpp"

namespace ravengun::config { class SettingsStore; }
namespace ravengun::games { class GameService; }

namespace ravengund {

class CameraService;  // gedefinieerd in camera.hpp
class HidContext;     // gedefinieerd in hid_thread.hpp
class OutputService;  // gedefinieerd in output_service.hpp
class ControllerService;  // gedefinieerd in controllers.hpp
class AagamesService;     // gedefinieerd in aagames_service.hpp
class IoBridgeService;    // gedefinieerd in iobridge_service.hpp
class MouseService;       // gedefinieerd in mouse_service.hpp
class GamepadService;     // gedefinieerd in gamepad_service.hpp

inline int64_t mono_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

// Log naar stderr met monotone tijd; Bluetooth-adressen horen al geredigeerd te zijn.
template <typename... A>
void logf(const char* level, const char* fmt, A... a) {
    double t = static_cast<double>(mono_ns()) / 1e9;
    std::fprintf(stderr, "[%.3f] %s: ", t, level);
    if constexpr (sizeof...(A) == 0) std::fputs(fmt, stderr);
    else std::fprintf(stderr, fmt, a...);
    std::fputc('\n', stderr);
}

// Laatste sample plus cumulatieve flanktellers per knopbit. Iedere afnemer onthoudt zelf de
// tellerstand van zijn vorige lezing, zodat geen enkele flank verloren gaat, ook niet bij
// meerdere of trage afnemers.
struct Snapshot {
    ravengun::core::InputSample sample;
    std::array<uint64_t, 32> press_total{};
    std::array<uint64_t, 32> release_total{};
    uint64_t optical_updates = 0;  // telt elke gepubliceerde optische meting
    // Voortgang van opname en magnetometerkalibratie (JSON-object of leeg), voor de SSE-stroom.
    std::string progress;
    uint64_t progress_seq = 0;
};

inline constexpr int kPlayers = 4;

// Korte samenvatting van wat er in de pijplijn van een speler zit (voor de controllerlijst en het overzicht), door de
// HID-thread bijgewerkt bij verbinden, verbreken en een andere batterijstand.
struct SlotInfo {
    bool connected = false;
    std::string address;        // volledig adres (kleine letters); leeg = geen Move
    std::string model;          // ZCM1 / ZCM2
    int battery_raw = -1;       // PSMove_Battery_Level
    int64_t connected_at_ns = 0;
    bool led_enabled = false;
    int led[3] = {0, 0, 0};
};

class SharedState {
public:
    static int clamp_player(int p) { return p < 1 ? 1 : p > kPlayers ? kPlayers : p; }
    // Sample van speler 1 (zoals voorheen) of van speler `player` (1..4).
    void publish(const ravengun::core::InputSample& s) { publish(1, s); }
    void publish(int player, const ravengun::core::InputSample& s) {
        const size_t i = static_cast<size_t>(clamp_player(player) - 1);
        std::lock_guard<std::mutex> g(m_);
        Snapshot& sn = snap_[i];
        sn.sample = s;
        sn.sample.optical = optical_[i];  // optische meting komt uit de tracker-thread
        for (int b = 0; b < 32; ++b) {
            if (s.pressed & (1u << b)) ++sn.press_total[b];
            if (s.released & (1u << b)) ++sn.release_total[b];
        }
    }
    // Vanuit de tracker-thread: laatste optische meting van de bol van speler `player`. Komt in elk volgend sample en in de
    // huidige momentopname (ook als er geen Move verbonden is).
    void publish_optical(const ravengun::core::OpticalSample& o) { publish_optical(1, o); }
    void publish_optical(int player, const ravengun::core::OpticalSample& o) {
        const int p = clamp_player(player);
        const size_t i = static_cast<size_t>(p - 1);
        {
            std::lock_guard<std::mutex> g(m_);
            optical_[i] = o;
            snap_[i].sample.optical = o;
            ++snap_[i].optical_updates;
        }
        if (optical_hook) optical_hook(p, o);  // bijv. de recorder (niet-blokkerend)
    }
    // Vanuit de HID-thread: de laatste optische meting van speler `player` als die nieuw is sinds `seen`.
    bool optical_if_new(uint64_t& seen, ravengun::core::OpticalSample& out) const { return optical_if_new(1, seen, out); }
    bool optical_if_new(int player, uint64_t& seen, ravengun::core::OpticalSample& out) const {
        const size_t i = static_cast<size_t>(clamp_player(player) - 1);
        std::lock_guard<std::mutex> g(m_);
        if (snap_[i].optical_updates == seen) return false;
        seen = snap_[i].optical_updates;
        out = optical_[i];
        return true;
    }
    // Vanuit de HID-thread: voortgang van opname/kalibratie (zie Snapshot::progress); geldt voor alle spelers.
    void set_progress(std::string json) {
        std::lock_guard<std::mutex> g(m_);
        progress_ = std::move(json);
        ++progress_seq_;
    }
    Snapshot get(int player = 1) const {
        const size_t i = static_cast<size_t>(clamp_player(player) - 1);
        std::lock_guard<std::mutex> g(m_);
        Snapshot s = snap_[i];
        s.progress = progress_;
        s.progress_seq = progress_seq_;
        return s;
    }
    void set_slot_info(int player, const SlotInfo& si) {
        std::lock_guard<std::mutex> g(m_);
        slots_[static_cast<size_t>(clamp_player(player) - 1)] = si;
    }
    std::array<SlotInfo, kPlayers> slot_infos() const {
        std::lock_guard<std::mutex> g(m_);
        return slots_;
    }

    // Opdrachten die in de HID-thread moeten draaien (pijplijn heeft één eigenaar).
    using Job = std::function<std::string(HidContext&)>;
    std::future<std::string> post(Job j) {
        std::packaged_task<std::string(HidContext&)> t(std::move(j));
        auto f = t.get_future();
        std::lock_guard<std::mutex> g(jm_);
        jobs_.push_back(std::move(t));
        return f;
    }
    void run_jobs(HidContext& ctx) {
        std::deque<std::packaged_task<std::string(HidContext&)>> js;
        {
            std::lock_guard<std::mutex> g(jm_);
            js.swap(jobs_);
        }
        for (auto& j : js) j(ctx);
    }

    std::atomic<bool> running{true};
    // De speler wiens Move wordt opgenomen (een opname is per controller); door de HID-thread gezet bij record start.
    std::atomic<int> recording_player{1};
    // Levert de camerastatus als JSON-object (thread-safe); leeg = geen camera-service.
    std::function<std::string()> camera_status;
    // De camera voor een spel (CameraService::set_game_feed: het beeld van de daemon in /dev/shm voor het spelproces), aan/uit,
    // de huidige stand, het apparaat van de daemon en of de camera aan staat. Leeg = geen camera-service (tests).
    std::function<void(bool on, const std::string& why)> set_game_camera;
    std::function<bool()> game_camera_on;
    // [Camera] Exposure = photo: zolang het spel de camera heeft, automatische belichting, gain en witbalans (een foto in het spel);
    // false = terug naar de eigen instellingen (het volgen van de Move). Leeg = geen camera-service.
    std::function<void(bool photo)> set_game_camera_photo;
    std::function<std::string()> camera_device;
    std::function<bool()> camera_enabled;
    // Uitvoer (virtueel lightgun-apparaat); niet-eigenaar, geset door main vóór de threads starten.
    OutputService* output = nullptr;
    // Wordt na elke optische meting aangeroepen (in de tracker-thread) met het spelernummer; vóór het starten van de
    // threads gezet en daarna niet meer gewijzigd.
    std::function<void(int, const ravengun::core::OpticalSample&)> optical_hook;
    // Instellingenopslag (config.json) en camera-service (voor set_camera/set_led); niet-eigenaar, geset
    // door main vóór de threads starten. nullptr = niet beschikbaar (bijv. in tests).
    ravengun::config::SettingsStore* settings = nullptr;
    CameraService* camera = nullptr;
    // Games starten/stoppen (TeknoParrot-profielen); nullptr = niet beschikbaar.
    ravengun::games::GameService* games = nullptr;
    const int64_t start_ns = mono_ns();  // start van de daemon (uptime)
    int web_port = 0;                    // 0 = webinterface uit; door main gezet
    bool web_lan = false;

    // Controllers (koppelen, verwijderen, Navigation Controller, BlueZ); nullptr = niet beschikbaar (tests).
    class ControllerService* controllers = nullptr;

    // Unity-route (Tomb Raider / Rabbids Hollywood zonder TeknoParrot); nullptr = niet beschikbaar.
    AagamesService* aagames = nullptr;
    // IO-brug (dialecten tp-shm en unity-tcp, routes per game); nullptr = niet beschikbaar (tests).
    IoBridgeService* iobridge = nullptr;
    // Gewone muis als bron voor de gun van een speler (runtime-toewijzingen); nullptr = niet beschikbaar.
    MouseService* mouse = nullptr;
    // Gamepad naast de lightgun (bewaarde toewijzingen, [Gamepad] per game); nullptr = niet beschikbaar.
    GamepadService* gamepad = nullptr;
    // Bit p-1 gezet: de rumble van speler p komt uit de game (Recoil-vlag van de plugin) in plaats van uit de trekker.
    std::atomic<unsigned> game_rumble_mask{0};
    // Per-game knopkeuzes ([Controls] in game.ini) per speler: (Move-knop, actie) over de globale indeling heen. Leeg = de globale
    // indeling. Gezet bij de start van een game en gewist bij stoppen; MoveSlot::configure legt ze ook op een nieuw toegewezen Move.
    std::vector<std::pair<std::string, std::string>> game_controls(int player) const {
        std::lock_guard<std::mutex> g(controls_m_);
        return controls_[static_cast<size_t>(clamp_player(player) - 1)];
    }
    void set_game_controls(int player, std::vector<std::pair<std::string, std::string>> v) {
        std::lock_guard<std::mutex> g(controls_m_);
        controls_[static_cast<size_t>(clamp_player(player) - 1)] = std::move(v);
    }
    // Aantal Recoil-vlaggen per speler (door de klantthread opgehoogd, door de HID-thread gelezen).
    std::array<std::atomic<uint64_t>, kPlayers> game_recoil{};

private:
    mutable std::mutex controls_m_;
    std::array<std::vector<std::pair<std::string, std::string>>, kPlayers> controls_{};
    mutable std::mutex m_;
    std::array<Snapshot, kPlayers> snap_{};
    std::array<ravengun::core::OpticalSample, kPlayers> optical_{};
    std::array<SlotInfo, kPlayers> slots_{};
    std::string progress_;
    uint64_t progress_seq_ = 0;
    std::mutex jm_;
    std::deque<std::packaged_task<std::string(HidContext&)>> jobs_;
};

}  // namespace ravengund
