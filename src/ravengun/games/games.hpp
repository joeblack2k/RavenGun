#pragma once
// Games starten en stoppen vanuit de webinterface (TeknoParrot-profielen).
//
// - list_games(): de profielen uit <tp_dir>/UserProfiles/*.xml. Alleen bestandsnamen die voldoen aan
//   [A-Za-z0-9_-]{1,64}.xml tellen mee; de naam komt uit <GameNameInternal>, het icoon uit <IconName>.
// - read_icon(): serveert uitsluitend bestanden uit <tp_dir>/Icons (naam [A-Za-z0-9_-]+.png|jpg|jpeg|gif|webp,
//   realpath binnen de Icons-map, gewoon bestand, hoogstens 3 MiB). Nooit een pad van buiten.
// - GameService: een game met een native profiel dat aan staat (packaging/native-games.json) start de daemon ZELF, zonder
//   shellscripts: wachten op een lightgun (Move of muis), uitvoer wapenen, de IO-brug, de schrijflaag, het logboek, en dan
//   `systemd-run --user --unit=ravengun-tpui -- env -i ... gamescope ... -- bwrap ... game` (src/ravengun/launch/native.*). Een
//   Unity-game uit packaging/aagames-games.json (Tomb Raider, Rabbids Hollywood) start hij ook zelf: de view klaarzetten, dan
//   `systemd-run ... -- proton run Game.exe -popupwindow`, en de TCP-klant van de plugin (src/ravengun/launch/unity.*). Een
//   game met [Launch] Mode = direct start hij ook zelf (src/ravengun/launch/direct.*): dezelfde voorbereiding als play-halo.sh
//   (lightgun, wapenen, de invoerroute tp-shm/hid-board/jvs over de pipe), de drop-ins en teknoparrot.ini in de view, en dan
//   `systemd-run --user --unit=ravengun-tpui -- rg-direct --run <plan.json>` (tools/rg-direct, op de plaats van launch-direct.py).
//   Wat de engine daar nog niet kan (direct.hpp), en alle andere games, start/stopt hij via `systemd-run --user` met een vaste
//   unitnaam en het script scripts/play-halo.sh. De
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

#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/native.hpp"
#include "ravengun/launch/unity.hpp"

namespace ravengun::games {

inline constexpr const char* kPlayUnit = "ravengun-play";
inline constexpr const char* kStopUnit = "ravengun-play-stop";
inline constexpr size_t kMaxIconBytes = 3 * 1024 * 1024;
inline constexpr size_t kMaxNoteChars = 300;   // tekens (UTF-8), geen bytes

// [A-Za-z0-9_-]{1,64}
bool valid_profile_name(std::string_view s);
// Startwijze van game_start: "direct", "teknoparrot-ui" of "auto" (leeg = auto).
bool valid_launch(std::string_view s);
// [A-Za-z0-9_-]{1,64}.(png|jpg|jpeg|gif|webp), alleen kleine letters in de extensie.
bool valid_icon_file(std::string_view s);

struct Config {
    std::string tp_dir;        // TeknoParrot-map met UserProfiles/ en Icons/
    std::string play_script;   // absoluut pad naar scripts/play-halo.sh
    std::string log_path;      // uitvoer van de laatste start (leeg = geen)
    ravengun::gamesettings::Dirs dirs;   // patches/ (meegeleverd en gebruiker): [Status] Note per game (leeg = geen notities)
    std::string library_file;  // library.json van de app (de gamemappen); leeg = games met een eigen profiel worden niet in de gamemappen gezocht
    std::string views_dir;     // runtime/views (lokale views van games); leeg = geen
    std::string home;          // thuismap (voor ~ in [Game] GamePath)
    std::string systemd_run = "systemd-run";
    std::string systemctl = "systemctl";
    std::string proc_root = "/proc";
    // cgroup.procs van de transient unit ravengun-tpui.service (TeknoParrotUi, Wine en de game leven daarin; het startscript
    // maakt hem aan). Niet leeg = er draait iets van een test. Leeg pad = niet gebruiken.
    std::string tpui_procs;
    int start_grace_ms = 6000; // zolang na het starten is "start" de toestand, ook als het script nog niet zichtbaar is
    std::function<bool()> move_connected;  // leeg = onbekend (geen "wacht op Move")

    // De native route zonder script. Leeg native.json = elke game via het script.
    ravengun::launch::NativePaths native;
    std::string native_log_dir;    // logs/native-<profiel>-<tijd>.log (leeg = geen logboek)
    std::string tpui_unit = "ravengun-tpui";   // de unit van de game (stoppen, OLED-bescherming en de IO-brug kijken daarnaar)
    std::function<bool()> lightgun_ready;      // Move verbonden of een muis als lightgun (leeg = niet wachten)
    std::function<std::string()> arm;          // uitvoer wapenen; leeg resultaat = gelukt
    std::function<std::string(const std::string& game, const std::string& route)> start_route;   // IO-brug; leeg = gelukt
    std::function<void()> stop_inputs;         // IO-brug stoppen en ontwapenen (bij stoppen of een mislukte start)
    // De Unity-route zonder script. Leeg unity.json = Unity-games via het script.
    ravengun::launch::UnityPaths unity;
    int lightgun_wait_ms = 120000;             // zo lang wachten op een Move of muis

    // De directe route zonder script (A10). Leeg direct.tp of leeg direct_runner = elke directe start via het script.
    ravengun::launch::DirectPaths direct;
    std::string direct_runner;                 // rg-direct (tools/rg-direct): het hoofdproces van de unit
    std::string jvs_link;                      // de pty-koppeling van de IO-brug (jvs over de pipe: RG_JVS_PTY)
    std::string input_by_id;                   // na het wapenen wachten op de virtuele lightgun en het toetsenbord hier (leeg = niet)
    std::function<std::string(const std::string& board)> hid_vidpid;   // VVVV:PPPP van een hid-bord (leeg = geen joystick-id zetten)
    std::function<bool(const std::string& profile)> unity_game;          // staat in aagames-games.json (de Unity-route van play-halo.sh)
    std::function<ravengun::launch::EnvMap()> session_env;               // omgeving van de game-unit (leeg = direct_session_env())
    ravengun::launch::CaptureRunner capture = ravengun::launch::run_capture;   // wine reg (joystick-id bij hid-board)
    int game_wait_ms = 120000;                 // zo lang wachten tot het spelproces er is
};

struct Game {
    std::string profile;    // bestandsnaam zonder .xml
    std::string name;       // weergavenaam
    std::string icon_file;  // leeg = geen (bestaand) icoon
    std::string note;       // opmerking ([Status] Note in game.ini, Nederlands, hoogstens 300 tekens: langer is afgekapt met "…"; leeg = geen)
};

// De games van de UI: de profielen uit <tp_dir>/UserProfiles/*.xml, en de games met een eigen profiel in game.ini (patches/<Game>/ met de
// gebruikerslaag) die in een gamemap of een lokale view staan (docs/per-game-patches.md, "Het profiel van een game").
std::vector<Game> list_games(const Config& c);
// Een bestaand profiel: UserProfiles/<profile>.xml, of een eigen profiel in game.ini (dan is de XML niet nodig).
bool profile_exists(const Config& c, const std::string& profile);
// De gamemappen uit library.json (leeg bij een ontbrekend of kapot bestand).
std::vector<std::string> library_folders(const std::string& library_file, const std::string& home);

// Leest een icoon. Leeg resultaat in `err` = gelukt; anders "invalid" (naam niet toegestaan) of "not_found".
std::string read_icon(const Config& c, const std::string& file, std::string& content_type, std::string& data);

// Een notitie van hoogstens kMaxNoteChars tekens. Een langere wordt afgekapt (op een woordgrens als die dichtbij is, nooit midden in een
// UTF-8-teken) en eindigt dan op "…", zodat te zien is dat er meer stond. Weglaten doen we niet: dan is de notitie nergens te zien.
std::string clip_note(const std::string& note);
// Nederlandse notities van alle games met een [Status] Note (hoogstens 300 tekens, zie clip_note; profielnaam [A-Za-z0-9_-]{1,64}).
std::vector<std::pair<std::string, std::string>> load_notes(const ravengun::gamesettings::Dirs& dirs);

// Procesinspectie (proc_root is /proc, of een nepmap in tests).
bool game_process_running(const std::string& proc_root);
// Draait er iets in de cgroup van de TeknoParrot-unit? (cgroup.procs niet leeg)
bool unit_alive(const std::string& cgroup_procs);
// Het standaardpad van cgroup.procs van ravengun-tpui.service voor deze gebruiker (user@UID.service/app.slice).
std::string default_tpui_procs(unsigned uid);
// 0 = geen, 1 = het startscript draait, 2 = het script draait met --stop
int script_state(const std::string& proc_root, const std::string& script);

// Argumenten (zonder shell) voor het starten/stoppen. Alleen aanroepen met een gevalideerde profielnaam.
std::vector<std::string> start_argv(const Config& c, const std::string& profile, const std::string& launch = "");
std::vector<std::string> stop_unit_argv(const Config& c);   // systemctl --user stop ravengun-play.service
std::vector<std::string> stop_script_argv(const Config& c); // systemd-run ... play-halo.sh --stop

// Gaat dit profiel via de native route van de daemon? (native-games.json leesbaar, profiel enabled, geen camera in de game)
bool native_route(const Config& c, const std::string& profile);
// Het commando waarmee de daemon een native game in de unit start (systemd-run, geen shell); `log` = uitvoer van de game.
std::vector<std::string> native_unit_argv(const Config& c, const std::vector<std::string>& game_argv, const std::string& log);

// Gaat dit profiel via de Unity-route van de daemon? (in aagames-games.json, niet via TeknoParrotUi gevraagd, geen tp-shm in game.ini)
bool unity_route(const Config& c, const std::string& profile, const std::string& launch = "");
// Het commando waarmee de daemon een Unity-game in de unit start: werkmap = de view, de omgeving van het plan, `log` = uitvoer.
std::vector<std::string> unity_unit_argv(const Config& c, const ravengun::launch::UnityPlan& plan, const std::string& log);
// Gaat deze start via de directe route van de daemon? Ok: de engine; Fallback: het script (why = reden, voor het logboek);
// Refused: niet starten (why = de melding, zoals launch-direct.py --check met exit 5). launch zoals bij start().
ravengun::launch::Verdict direct_route(const Config& c, const std::string& profile, const std::string& launch, std::string& why,
                                       ravengun::launch::DirectInput* input = nullptr);
// De omgeving waarin play-halo.sh een directe start doet: die van de daemon zonder de variabelen van zijn eigen unit, met
// XDG_RUNTIME_DIR, WAYLAND_DISPLAY, DISPLAY, DBUS_SESSION_BUS_ADDRESS en XAUTHORITY (van kwin_wayland) zoals het script ze aanvult.
ravengun::launch::EnvMap direct_session_env(const std::string& proc_root = "/proc");
std::vector<std::string> direct_unit_argv(const Config& c, const std::string& plan_file, const std::string& log);

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

    // launch: "" (play-halo.sh kiest per profiel), "direct" (--direct) of "teknoparrot-ui" (--tpui).
    Result start(const std::string& profile, const std::string& launch = "");
    Result stop();
    std::string status_json();  // {"state":...,"text":...,"profile":...,"message":...}
    std::string list_json();    // {"available":..,"games":[...],"status":{...}}
    const Config& config() const { return c_; }

private:
    void join_worker();
    std::string run_native(const std::string& profile);   // de native start in de worker; leeg = gelukt, anders de melding
    std::string run_unity(const std::string& profile);    // idem voor de Unity-route
    std::string run_direct(const std::string& profile, const ravengun::launch::DirectInput& in);   // idem, de directe route
    std::string wait_lightgun();                          // wacht op een Move of muis; leeg = er is er een
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
    bool native_ = false;                 // de daemon startte de laatste game zelf: native, Unity of direct (stoppen ruimt dan zelf op)
    std::atomic<bool> waiting_{false};    // de eigen start wacht op een lightgun
    std::atomic<bool> cancel_{false};     // stoppen tijdens een eigen start
};

}  // namespace ravengun::games
