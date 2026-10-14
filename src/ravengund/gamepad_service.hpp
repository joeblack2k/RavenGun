#pragma once
// Gamepadservice van ravengund: een gamepad (evdev) als extra bediening naast de lightgun van een speler (1..4): stuur, gas en
// rem en extra kastknoppen (docs/gamepad.md). Gemodelleerd naar MouseService: eigen thread, het eventN-apparaat wordt
// O_RDONLY|O_NONBLOCK|O_CLOEXEC geopend en NIET gegrabd; de daemon opent alleen een apparaat uit zijn eigen opsomming
// (sysinfo::list_gamepads), een client geeft een id en nooit een pad.
//
// Toewijzing: hoogstens één gamepad per speler, en anders dan bij de muis BEWAARD in config.json (Settings::gamepads), zodat een
// herstart van de daemon niets wist. Automatisch: heeft speler 1 geen gamepad en is er precies één gamepad aangesloten (die niet
// aan een andere speler hangt), dan is dat de gamepad van speler 1. Alleen de gamepads die zo bij een speler horen worden geopend.
//
// Uitvoer per speler: bedieningen die op gun-knoppen (trigger/reload/grenade/aux1/aux2) of toetsen (start = toets N van speler N,
// coin = toets 5, key:<naam>) vallen gaan als live bron 2 door OutputService::feed_live (OR met Move en muis, zonder aim); stuur,
// gas, rem en btn1..8 gaan via OutputService::set_pad_input naar de IO-brug. De indeling komt uit [Gamepad] van de game die de
// IO-brug het laatst startte (set_game), anders de standaardindeling.
//
// Verlies van een apparaat, een andere toewijzing of indeling en afsluiten laten alles van die speler los. Bij een fout (apparaat
// weg, EACCES) staat er een last_error in de status en probeert de thread het bij de volgende opsomming (elke 2 s) opnieuw.
#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/output/gamepad.hpp"
#include "ravengun/sysinfo/gamepads.hpp"

namespace ravengund {

class OutputService;

struct GamepadServiceConfig {
    static constexpr int kPlayers = 4;
    using Assignments = std::array<std::string, kPlayers>;
    // Levert de gamepads (en per invoerapparaat waarom wel of niet); leeg = sysinfo::list_system_gamepads. Tests geven een nepmap.
    std::function<std::vector<ravengun::sysinfo::Gamepad>(std::vector<ravengun::sysinfo::GamepadVerdict>*)> lister;
    std::string dev_dir = "/dev/input";   // map met de eventN-apparaten (tests: een map met een FIFO)
    int scan_ms = 2000;                   // opnieuw opsommen (nieuwe pads, opnieuw proberen na een fout)
    ravengun::gamesettings::Dirs dirs;    // [Gamepad] per game
    // Bewaarde toewijzingen lezen en schrijven (config.json); leeg = alleen in het geheugen. save geeft "" of een melding.
    std::function<Assignments()> load;
    std::function<std::string(const Assignments&)> save;
    bool open_devices = true;             // false (--no-hid): opsommen en toewijzen wel, nooit een apparaat openen
};

class GamepadService {
public:
    static constexpr int kPlayers = GamepadServiceConfig::kPlayers;
    GamepadService(OutputService& out, GamepadServiceConfig cfg);
    ~GamepadService();   // stop()
    GamepadService(const GamepadService&) = delete;
    GamepadService& operator=(const GamepadService&) = delete;

    void start();
    // Stopt de thread; laat alles van alle gamepadspelers los. Idempotent.
    void stop();

    // Wijst de gamepad met dit id (uit pads_json) toe aan speler 1..4, of ontkoppelt bij id "" (dan geldt voor speler 1 weer de
    // automatische regel). Bewaard via cfg.save. true = gelukt; anders is `code` een IPC-foutcode (invalid_argument, unavailable)
    // en `msg` de Nederlandse melding.
    bool assign(int player, const std::string& id, std::string& code, std::string& msg);
    // De IO-brug start de route van een game: vanaf nu geldt [Gamepad] van die game (leeg = de standaardindeling).
    void set_game(const std::string& game);

    // {"pads":[{"id","name","event","connected","last_error","player","live":{...}}],"assignments":[...],"game":..,"mapping":[..],
    //  "devices":[{"name","event","vendor","product","gamepad","why"}]}
    std::string pads_json() const;
    // {"player":N,"pad":"<id>"|null,"auto":bool,"connected":bool,"last_error":..}
    std::string assignment_json(int player) const;
    // De vier toewijzingen als JSON-array (blok "gamepads" in de status).
    std::string status_json() const;
    // Is er een speler met een toegewezen (of automatisch gekozen) gamepad die nu verbonden is? Een start wacht dan niet op een Move.
    bool any_player_connected() const;
    // Voor tests: wacht tot de werkerthread een volledige ronde heeft gedaan.
    bool wait_round(int timeout_ms = 2000);

private:
    struct Dev {
        int fd = -1;
        std::string event;
        ravengun::output::PadReader reader;
        std::string carry;
    };
    struct Live {   // onder m_, voor pads_json
        bool connected = false;
        std::string last_error;
        ravengun::output::PadState state;
    };
    struct Sent {   // alleen de werkerthread: wat een speler het laatst kreeg
        std::set<int> keys;
        bool any = false;
    };

    void run();
    std::vector<ravengun::sysinfo::Gamepad> enumerate(std::vector<ravengun::sysinfo::GamepadVerdict>* v) const;
    void rescan();
    void sync_devices();
    bool try_open(const ravengun::sysinfo::Gamepad& g, Dev& d, std::string& err);
    void close_dev(const std::string& id, const std::string& why);
    void drain(const std::string& id);
    void push_all();
    void push_player(int player);
    void wake();
    GamepadServiceConfig::Assignments effective_locked(std::array<bool, kPlayers>* is_auto = nullptr) const;
    std::string assignment_locked(int player) const;
    std::string assignments_locked() const;

    OutputService& out_;
    GamepadServiceConfig cfg_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    // Gedeeld (onder m_)
    GamepadServiceConfig::Assignments assigned_{};
    std::vector<ravengun::sysinfo::Gamepad> pads_;
    std::vector<ravengun::sysinfo::GamepadVerdict> verdicts_;
    std::map<std::string, Live> live_;
    ravengun::output::GamepadMap map_ = ravengun::output::default_gamepad_map();
    std::string game_;
    std::vector<std::string> map_warnings_;
    uint64_t gen_ = 1, rounds_ = 0;
    bool rescan_now_ = true;
    bool stop_ = false, running_ = false;
    // Alleen de werkerthread
    std::map<std::string, Dev> devs_;
    std::array<Sent, kPlayers> sent_{};
    uint64_t applied_gen_ = 0;
    std::string verdict_sig_;
    int64_t verdict_log_ms_ = 0;     // laatste volledige lijst in het logboek
    int verdict_changes_ = 0;        // wijzigingen sindsdien die niet als lijst gelogd zijn
    int wake_fd_ = -1;
    std::thread th_;
};

}  // namespace ravengund
