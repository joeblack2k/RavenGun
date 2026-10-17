#pragma once
// Muisservice van ravengund: een gewone systeemmuis (evdev) als bron voor de virtuele lightgun van een speler (1..4).
// Eigen thread; per speler hoogstens één toegewezen muis. Het eventN-apparaat wordt O_RDONLY|O_NONBLOCK|O_CLOEXEC geopend
// en NIET gegrabd (EVIOCGRAB): de desktopcursor blijft gewoon werken. De events gaan door een MouseGun (zuivere
// toestandsmachine, ravengun/output/mouse_gun.hpp) naar OutputService::feed_live(speler, actie).
//
// Veiligheid: de daemon opent alleen een apparaat dat in zijn eigen opsomming (sysinfo::list_mice) staat; een client geeft
// een id en nooit een pad. Verlies van het apparaat, afmelden en afsluiten geven alle knoppen vrij (release_all). Bij een
// fout (apparaat weg, EACCES) staat er een duidelijke last_error in de status, blijft de toewijzing staan en probeert de
// thread elke 2 s opnieuw. Toewijzingen zijn alleen runtime (niet in config.json): de app past ze opnieuw toe bij het
// verbinden.
#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/output/mouse_gun.hpp"
#include "ravengun/sysinfo/mice.hpp"

namespace ravengund {

class OutputService;

struct MouseServiceConfig {
    // Levert de beschikbare muizen; leeg = ravengun::sysinfo::list_system_mice (echt systeem). Tests geven een nepmap.
    std::function<std::vector<ravengun::sysinfo::Mouse>()> lister;
    std::string dev_dir = "/dev/input";  // map met de eventN-apparaten (tests: een map met een FIFO)
    int retry_ms = 2000;                 // opnieuw proberen na een fout
};

class MouseService {
public:
    static constexpr int kPlayers = 4;
    explicit MouseService(OutputService& out) : MouseService(out, MouseServiceConfig{}) {}
    MouseService(OutputService& out, MouseServiceConfig cfg);
    ~MouseService();  // stop()
    MouseService(const MouseService&) = delete;
    MouseService& operator=(const MouseService&) = delete;

    void start();
    // Stopt de thread; geeft alle knoppen van alle muisspelers vrij. Idempotent.
    void stop();

    // Wijst de muis met dit id (uit mice_json) toe aan speler 1..4, of ontkoppelt bij id "". `counts` = gevoeligheid
    // (counts per schermbreedte, 100..20000); zonder waarde blijft de huidige gelden. true = gelukt; anders is `code` een
    // IPC-foutcode (invalid_argument, unavailable) en `msg` de Nederlandse melding. Wacht hoogstens ~0,4 s op de eerste
    // openpoging, zodat het antwoord meteen een foutmelding (EACCES, ...) kan bevatten.
    bool assign(int player, const std::string& id, std::optional<int> counts, std::string& code, std::string& msg);

    // {"mice":[{"id","name","absolute","event"}...],"assignments":[...]}
    std::string mice_json() const;
    // {"player":N,"mouse":"<id>"|null,"connected":bool,"last_error":"..."|null,"counts_per_screen":N}
    std::string assignment_json(int player) const;
    // De vier toewijzingen als JSON-array (voor het blok "mouse_guns" in de status).
    std::string status_json() const;
    // Is er een muis als lightgun toegewezen en geopend (verbonden)? Zo ja, dan hoeft een start niet op een Move te wachten.
    bool any_connected() const;
    // Speler 1..4: een muis toegewezen (assigned) en geopend (connected). Voor de poort vóór een start (games/readiness.hpp).
    void player_state(int player, bool& assigned, bool& connected) const;

private:
    struct Slot {
        // Gedeeld (onder m_)
        std::string mouse;
        int counts = static_cast<int>(ravengun::output::kMouseCountsDefault);
        bool connected = false;
        std::string last_error;
        uint64_t gen = 0;       // wordt opgehoogd bij elke toewijzing
        uint64_t attempts = 0;  // aantal afgeronde openpogingen (voor assign)
        // Alleen de werkerthread
        int fd = -1;
        uint64_t applied_gen = 0;
        ravengun::output::MouseGun gun;
        std::string carry;      // een half input_event tussen twee reads
        int64_t next_retry_ms = 0;
    };

    void run();
    std::vector<ravengun::sysinfo::Mouse> mice() const;
    void try_open(size_t i);
    void drain(size_t i);          // leest en verwerkt events; sluit en meldt bij verlies
    void lose(size_t i, const std::string& why);
    void close_and_release(size_t i);
    void wake();
    unsigned mask_locked() const;
    std::string assignment_locked(int player) const;

    OutputService& out_;
    MouseServiceConfig cfg_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::array<Slot, kPlayers> slots_;
    int wake_fd_ = -1;
    bool stop_ = false;
    bool running_ = false;
    std::thread th_;
};

}  // namespace ravengund
