#pragma once
// Games starten en stoppen vanuit de webinterface (TeknoParrot-profielen).
//
// - list_games(): de profielen uit <tp_dir>/UserProfiles/*.xml. Alleen bestandsnamen die voldoen aan
//   [A-Za-z0-9_-]{1,64}.xml tellen mee; de naam komt uit <GameNameInternal>, het icoon uit <IconName>.
// - read_icon(): serveert uitsluitend bestanden uit <tp_dir>/Icons (naam [A-Za-z0-9_-]+.png|jpg|jpeg|gif|webp,
//   realpath binnen de Icons-map, gewoon bestand, hoogstens 3 MiB). Nooit een pad van buiten.
// - GameService: start/stopt via `systemd-run --user` met een vaste unitnaam het script scripts/play-halo.sh. De
//   profielnaam is altijd een bestaand bestand uit UserProfiles en komt als los argument (geen shell, geen
//   interpolatie) op de opdrachtregel. De toestand (wacht op Move, start, draait, mislukt) wordt afgeleid uit
//   /proc (BudgieLoader, het script) en het logbestand van de laatste start; er is geen wachtende thread.
// Ontwerp: docs/web-ui.md, tab Games.
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ravengun::games {

inline constexpr const char* kPlayUnit = "ravengun-play";
inline constexpr const char* kStopUnit = "ravengun-play-stop";
inline constexpr size_t kMaxIconBytes = 3 * 1024 * 1024;

// [A-Za-z0-9_-]{1,64}
bool valid_profile_name(std::string_view s);
// [A-Za-z0-9_-]{1,64}.(png|jpg|jpeg|gif|webp), alleen kleine letters in de extensie.
bool valid_icon_file(std::string_view s);

struct Config {
    std::string tp_dir;        // TeknoParrot-map met UserProfiles/ en Icons/
    std::string play_script;   // absoluut pad naar scripts/play-halo.sh
    std::string log_path;      // uitvoer van de laatste start (leeg = geen)
    std::string notes_json;    // inhoud van packaging/games-notes.json (leeg = geen notities)
    std::string systemd_run = "systemd-run";
    std::string systemctl = "systemctl";
    std::string proc_root = "/proc";
    // cgroup.procs van de transient unit ravengun-tpui.service (TeknoParrotUi, Wine en de game leven daarin; het startscript
    // maakt hem aan). Niet leeg = er draait iets van een test. Leeg pad = niet gebruiken.
    std::string tpui_procs;
    int start_grace_ms = 6000; // zolang na het starten is "start" de toestand, ook als het script nog niet zichtbaar is
    std::function<bool()> move_connected;  // leeg = onbekend (geen "wacht op Move")
};

struct Game {
    std::string profile;    // bestandsnaam zonder .xml
    std::string name;       // weergavenaam
    std::string icon_file;  // leeg = geen (bestaand) icoon
    std::string note;       // opmerking uit games-notes.json (leeg = geen)
};

std::vector<Game> list_games(const Config& c);
bool profile_exists(const Config& c, const std::string& profile);

// Leest een icoon. Leeg resultaat in `err` = gelukt; anders "invalid" (naam niet toegestaan) of "not_found".
std::string read_icon(const Config& c, const std::string& file, std::string& content_type, std::string& data);

// Notities uit games-notes.json: {"schema":1,"games":{"<profiel>":"tekst"}}. Onleesbaar = leeg.
std::vector<std::pair<std::string, std::string>> parse_notes(const std::string& json_text);

// Procesinspectie (proc_root is /proc, of een nepmap in tests).
bool game_process_running(const std::string& proc_root);
// Draait er iets in de cgroup van de TeknoParrot-unit? (cgroup.procs niet leeg)
bool unit_alive(const std::string& cgroup_procs);
// Het standaardpad van cgroup.procs van ravengun-tpui.service voor deze gebruiker (user@UID.service/app.slice).
std::string default_tpui_procs(unsigned uid);
// 0 = geen, 1 = het startscript draait, 2 = het script draait met --stop
int script_state(const std::string& proc_root, const std::string& script);

// Argumenten (zonder shell) voor het starten/stoppen. Alleen aanroepen met een gevalideerde profielnaam.
std::vector<std::string> start_argv(const Config& c, const std::string& profile);
std::vector<std::string> stop_unit_argv(const Config& c);   // systemctl --user stop ravengun-play.service
std::vector<std::string> stop_script_argv(const Config& c); // systemd-run ... play-halo.sh --stop

// Voert een proces uit en wacht tot het klaar is (hoogstens 10 s). Geeft de exitcode, of -1 met `err`.
int run_process(const std::vector<std::string>& argv, std::string& err);

struct Result {
    bool ok = true;
    std::string code;     // unavailable | invalid_argument
    std::string message;  // Nederlands
};

class GameService {
public:
    using Spawner = std::function<int(const std::vector<std::string>& argv, std::string& err)>;
    explicit GameService(Config c, Spawner s = run_process);
    ~GameService();
    GameService(const GameService&) = delete;
    GameService& operator=(const GameService&) = delete;

    Result start(const std::string& profile);
    Result stop();
    std::string status_json();  // {"state":...,"text":...,"profile":...,"message":...}
    std::string list_json();    // {"available":..,"games":[...],"status":{...}}
    const Config& config() const { return c_; }

private:
    void join_worker();
    Config c_;
    Spawner spawn_;
    std::mutex m_;
    std::thread worker_;
    std::atomic<bool> busy_{false};       // de worker draait
    bool busy_stop_ = false;              // ... en stopt
    std::string profile_;                 // laatst gestarte profiel
    bool launched_ = false;               // systemd-run slaagde en het resultaat is nog niet afgehandeld
    int64_t launched_ms_ = 0;
    bool failed_ = false;
    std::string failed_msg_, failed_profile_;
};

}  // namespace ravengun::games
