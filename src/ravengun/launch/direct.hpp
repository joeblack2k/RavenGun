#pragma once
// De directe route (A10) zonder scripts: wat scripts/launch-direct.py --plan uitrekent (profiel uit game.ini, Wine en prefix, lader en
// kern, eigen build, teknoparrot.ini, drop-ins, omgeving, gamescope, de JVS-brug) en wat play-halo.sh voor zo'n game voorbereidt (de
// invoerroute). De daemon voert het plan uit (GameService): drop-ins en bestanden plaatsen, dan
// `systemd-run --user --unit=ravengun-tpui -- rg-direct --run <plan.json>`. rg-direct (tools/rg-direct) is het C++-hoofdproces van de
// unit op de plaats van launch-direct.py --run: gedeeld geheugen, de JVS-pipe (TCP-kant), pipehelper/rg-jvsbridge in de prefix, de lader,
// het spel volgen tot het weg is, opruimen. Geen shell en geen Python in de keten.
//
// Gelijk aan launch-direct.py: dezelfde argv, dezelfde omgeving, dezelfde geplaatste bestanden (tests/direct_golden.py vergelijkt het
// plan per game uit patches/ met dat van het script). Wat de engine (nog) niet doet, laat hij aan het script over (Verdict::Fallback,
// met de reden): een profiel zonder eigen profiel in game.ini (alleen een UserProfiles-XML), een
// ingeschakelde patch (de patchmotor), ExeSha256, [Launch] Exe en Drives, ontbrekende GStreamer-plug-ins (fetch-gst-wmv.sh), en de
// invoerroutes unity-tcp en jvs over een COM-poort. Wat het script zou weigeren (exit 5: instelling of patch geweigerd, drop-in niet te
// plaatsen) weigert de engine ook (Verdict::Refused): dan start er niets, ook geen TeknoParrotUi.
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/gamesettings/gamesettings.hpp"

namespace ravengun::launch {

using EnvMap = std::map<std::string, std::string>;

struct DirectPaths {
    std::string tp;              // runtime-map van TeknoParrot (RAVENGUN_TP_DIR of ~/.local/share/ravengun/runtime/TeknoParrotUi-...)
    std::string home;            // HOME (~ uitklappen, ~/mnt is de NAS, de vaste paden van launch-direct.py)
    std::string data;            // $XDG_DATA_HOME/ravengun (wrapper wine-prefix, views)
    std::string state;           // $XDG_STATE_HOME/ravengun (A11-samenvatting)
    ravengun::gamesettings::Dirs dirs;   // patches/ (meegeleverd) en de gebruikerslaag
    std::string library_file;    // library.json van de app (de gamemappen)
    std::string mounts = "/proc/self/mounts";
    std::string views() const { return data + "/runtime/views"; }
};

// Standaardpaden zoals launch-direct.py ze uit de omgeving haalt (HOME, XDG_DATA_HOME, XDG_STATE_HOME, XDG_CONFIG_HOME,
// RAVENGUN_TP_DIR, RAVENGUN_LIBRARY_FILE). dirs blijft leeg.
DirectPaths default_direct_paths(const EnvMap& env);

enum class Verdict {
    Ok,         // de engine start de game zelf
    Fallback,   // via scripts/play-halo.sh (het script valt zelf terug op TeknoParrotUi als direct niet kan)
    Refused,    // niet starten: wat launch-direct.py met exit 5 weigert
};
const char* verdict_name(Verdict v);   // ok | fallback | refused

struct DropinFile { std::string src, dest, name; };

// Een start van het plan (launch-direct.py "starts"). Een gewone game heeft er een (role "main", argv gelijk aan DirectPlan::argv); een
// profiel met HasTwoExecutables twee, in de orde waarin ze gebeuren (LaunchSecondExecutableFirst), met de wachttijd voor de tweede
// ([Launch] SecondWait, standaard 5 s). Beide krijgen dezelfde wine, lader, kern, werkmap, prefix en omgeving.
struct DirectStart {
    std::string name, role, exe, cwd;                      // role: main | second
    std::vector<std::string> argv;
    std::vector<std::string> comms;                        // procesnamen van deze start (exe_comms)
    bool gamescope = false;
    double wait_before = 0.0;                              // seconden wachten voor deze start (de eerste: 0)
};

struct DirectPlan {
    std::string profile, tp, emulator_type, emulation_profile, game, game_source, cwd, wine, prefix, loader;
    bool is64 = false, gamescope = false, windowed_override = false, log_full = false, windows = false;
    std::vector<std::string> argv;                         // het hele commando van de start ([gamescope ... --] wine lader [kern] spel [args])
    EnvMap env;                                            // de VOLLEDIGE omgeving van de lader en de helper (met de drop-ins)
    EnvMap launch_env;                                     // wat de launcher zelf zet (voor het logboek)
    std::vector<std::pair<std::string, std::string>> files;   // gegenereerd naast de game: pad, tekst (teknoparrot.ini, ...)
    // Drop-ins (docs/dropins.md): de bestanden, de mappen die eerst lokaal worden, en de regels voor het logboek.
    std::string dropin_base, dropin_root;
    std::vector<DropinFile> dropin_files;
    std::vector<std::string> dropin_materialise, dropin_lines;
    std::string pipe_mode, shm_source, pipehelper;         // de JVS-brug: "pipe" of "shm", bronbestand, rg-jvsbridge.exe of pipehelper.exe
    std::vector<std::string> game_comms;                   // procesnamen (comm, kleine letters, 15 tekens) die "de game draait" zijn
    std::vector<DirectStart> starts;                       // de starts in hun orde (een gewone game: een)
    std::string core_dll, core_source;                     // de kern (leeg = geen) en waar hij vandaan komt (runtime | ravengun)
    std::vector<std::string> patch_lines;                  // een regel per patch (ook uitgeschakelde), zoals launch-direct.py
    std::vector<std::string> notes;                        // extra regels voor het logboek (instellingen, legacy .env)
    std::string state;                                     // $XDG_STATE_HOME/ravengun (A11)
};

struct DirectOptions {
    EnvMap base_env;            // os.environ van launch-direct.py: de omgeving van de unit (plus de extra variabelen van play-halo.sh)
    std::string shm_source;     // --shm-source (leeg = /dev/shm/TeknoParrot_JvsState)
    bool log_full = false;      // --log-full
    std::string session_id;     // TP_LAUNCH_SESSION_ID (leeg = een nieuwe uuid4)
    // Breedte en hoogte van het scherm voor gamescope zonder RG_GAMESCOPE_RES (leeg = xrandr --current, anders 1920x1080).
    std::function<std::pair<int, int>(const EnvMap& env)> display_size;
};

// Het plan. Ok: `out` is volledig. Fallback/Refused: `why` zegt waarom (Nederlands, de melding van launch-direct.py waar die er is).
Verdict plan_direct(const std::string& profile, const DirectPaths& p, const DirectOptions& o, DirectPlan& out, std::string& why);

// De drop-ins en de gegenereerde bestanden in de view zetten (wat launch-direct.py in run() doet voordat er iets start). Nooit op de NAS.
// false + err = de game hoort niet te starten. `log` krijgt een regel per stap.
bool apply_direct(const DirectPlan& plan, const DirectPaths& p, std::vector<std::string>& log, std::string& err);

// Ontbreken de extra GStreamer-plug-ins (GST_PLUGIN_PATH zonder libgstasf.so)? Dan haalt het script ze eerst op (fetch-gst-wmv.sh).
bool needs_gst_fetch(const DirectPlan& plan);

// Alle procesnamen van het plan: game_comms plus die van elke start (bij twee exe's ook de tweede, bv. amdaemon.exe).
std::vector<std::string> plan_comms(const DirectPlan& plan);

// Het plan als JSON voor rg-direct --run, en terug.
std::string plan_to_json(const DirectPlan& plan);
bool plan_from_json(const std::string& text, DirectPlan& out, std::string& err);

// --- De invoer, zoals play-halo.sh die voor een directe start kiest ---------------------------------------------------------------
struct DirectInput {
    std::string route;          // tp-shm | hid-board | jvs (na de regel "evdev met een indeling wordt tp-shm")
    std::string layout, board, transport, com_port;
    std::string note;           // "Directe start: invoer via tp-shm ..." als evdev is omgezet
    // Schrijft de daemon op deze route het gedeelde geheugen (/dev/shm/ravengun_JvsState), zodat de pipehelper dat bestand naar de
    // game spiegelt? tp-shm altijd; jvs als [Input] ook een Layout heeft (Let's Go Safari: het stuur komt alleen uit het gedeelde
    // geheugen, de game vraagt nooit JVS 0x22). Zelfde regel als play-halo.sh (--shm-source).
    bool writes_tp_shm() const { return route == "tp-shm" || (route == "jvs" && !layout.empty()); }
};
// [Input] van de game, gecontroleerd zoals gamesettings.resolve_input (klopt de gebruikerslaag niet, dan de meegeleverde, anders evdev).
// Fallback: een route die de engine niet zelf voorbereidt (evdev zonder indeling, unity-tcp, jvs over een COM-poort).
Verdict direct_input(const std::string& profile, const DirectPaths& p, DirectInput& out, std::string& why);
// [Launch] Mode van de game (direct | teknoparrot-ui | leeg), met de gebruikerslaag eroverheen.
std::string launch_mode(const std::string& profile, const DirectPaths& p);
// `gamesettings.py env PROFIEL`: de omgeving uit game.ini (met het oude .env als laag), zoals play-halo.sh hem leest.
EnvMap profile_env(const std::string& profile, const DirectPaths& p);

// --- hid-board: het bord krijgt joystick-id 0 in de prefix (scripts/wine-joyid.sh) -----------------------------------------------
struct JoyId { std::string key; int id = 0; };
// De regels van `wine reg query <DirectInput> /s`: per Calibration-sleutel het "Joystick Id".
std::vector<JoyId> parse_joy_ids(const std::string& reg_query_output);
// Welke sleutels een nieuw id krijgen (sleutel onder DirectInput\, id), zodat VID:PID id 0 heeft en een ander apparaat met 0 opschuift.
std::vector<JoyId> joy_id_changes(const std::vector<JoyId>& ids, const std::string& vidpid);
// Uitvoeren (wine reg query/add, wineserver -w). `run` voert een programma uit met een omgeving en geeft de exitcode (out = stdout).
using CaptureRunner = std::function<int(const std::vector<std::string>& argv, const EnvMap& env, std::string* out)>;
int run_capture(const std::vector<std::string>& argv, const EnvMap& env, std::string* out);
void wine_joystick_zero(const std::string& wine, const std::string& prefix, const std::string& vidpid, const EnvMap& base_env,
                        const CaptureRunner& run, std::vector<std::string>& log);

// --- kleine hulpen (ook voor rg-direct en de tests) ---------------------------------------------------------------------------------
std::vector<std::string> shlex_split(const std::string& s, bool* ok = nullptr);   // shlex.split (POSIX)
std::vector<std::string> win_split(const std::string& s);                          // de regels van de C-runtime van Windows
std::string wine_path(const std::string& p);                                       // Z:\pad\naar
std::vector<std::string> exe_comms(const std::vector<std::string>& names);         // procesnamen van exe-namen ('a.exe;b.exe' of een pad)
std::string new_uuid4();
EnvMap environ_map();                                                              // de omgeving van dit proces

}  // namespace ravengun::launch
