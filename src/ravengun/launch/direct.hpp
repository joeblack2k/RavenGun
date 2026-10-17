#pragma once
// De directe route (A10) zonder scripts: het plan (profiel uit game.ini, Wine en prefix, lader en kern, eigen build, teknoparrot.ini,
// drop-ins, omgeving, gamescope, de JVS-brug) en de invoerroute; tot 2026-10-16 deden scripts/launch-direct.py en play-halo.sh dat,
// tests/golden/direct/ bewaart hun uitkomst. De daemon voert het plan uit (GameService): drop-ins en bestanden plaatsen, dan
// `systemd-run --user --unit=ravengun-tpui -- bwrap ... -- rg-direct --run <plan.json>` (de schrijflaag: games::write_layer).
// rg-direct (tools/rg-direct) is het C++-hoofdproces van de unit: gedeeld geheugen, de JVS-pipe (TCP-kant), pipehelper/rg-jvsbridge in
// de prefix, de lader, het spel volgen tot het weg is, opruimen. Geen shell en geen Python in de keten.
//
// tests/golden/direct/ bewaart het plan per game uit patches/ (bij invoering gelijk aan dat van launch-direct.py). Wat de engine
// (nog) niet doet, start niet (Verdict::Fallback, met de reden): een profiel zonder eigen profiel in game.ini (alleen een
// UserProfiles-XML), een ingeschakelde patch van een soort die de engine niet toepast, ontbrekende GStreamer-plug-ins
// (fetch-gst-wmv.sh), en de invoerroutes unity-tcp en jvs over een COM-poort. Een instelling of patch die geweigerd wordt, of een
// drop-in die niet te plaatsen is, geeft Verdict::Refused. In beide gevallen start er niets.
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
    Fallback,   // de engine kan deze game (nog) niet direct starten: er start niets (de reden staat in why)
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

// [Launch] Drives: een schijfletter in de prefix voor de duur van de run (rg-direct zet hem en haalt hem weg als hij er niet al stond).
struct DirectDrive {
    std::string letter, target, link;   // "W", de lokale map, <prefix>/dosdevices/w:
    bool present = false;               // stond er al (en wees naar dezelfde map): dan laat rg-direct hem staan
};

struct DirectPlan {
    // tp: de TeknoParrot-map, alleen als het plan er iets uit gebruikt (tp_files); leeg bij een eigen lader en kern of zonder lader.
    // cwd: de werkmap van de lader; de TeknoParrot-map alleen als de lader of kern daar vandaan komt, anders de map van de game.
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
    // [Launch] StackReserve (0 = niet): de exe van de game (stack_dest, in de view) wordt een lokale kopie van stack_src met
    // SizeOfStackReserve = stack_reserve (pe_stack.hpp). stack_src is het origineel: de koppeling <exe>.ravengun-orig als die er al
    // is, anders waar de koppeling van de view naar wijst, anders de exe zelf.
    long long stack_reserve = 0;
    // [Launch] PeFillPages = true: in dezelfde lokale kopie lopen de secties tot hun bladzijde, zoals Windows ze mapt (pe_stack.hpp,
    // pe_fill_section_pages). Samen met StackReserve één kopie.
    bool pe_fill_pages = false;
    std::string stack_src, stack_dest;
    // [Launch] ViewLinks: per paar (link, doel), beide absolute paden in de view. apply_direct() maakt van de link een symlink naar
    // het bestand waar het doel naar wijst (realpath, meestal op de NAS); de mappen op de weg worden eerst lokaal. Alleen de view.
    std::vector<std::pair<std::string, std::string>> view_links;
    // [Launch] Dxvk = true: de map met de DXVK-DLL's van de bitbreedte van de game (d3d8, d3d9, d3d10core, d3d11, dxgi) en waar die
    // vandaan komt (docs/dxvk.md). dropin_plan() zet ze als drop-in in de view met WINEDLLOVERRIDES <naam>=n. Leeg = geen DXVK.
    std::string dxvk_dir, dxvk_from;
    // [Launch] BepInEx = <versie>: de map met de officiële release (winhttp.dll, doorstop_config.ini, BepInEx/core) van de bitbreedte van
    // de game. dropin_plan() zet die naast de exe in de view, winhttp=n,b. Leeg = geen BepInEx.
    std::string bepinex_dir;
    // [Launch] NativeDlls: per DLL (bron in <data>/redist/<x86|x64>/, doel in system32/syswow64 van de prefix). apply_direct() zet de
    // native DLL op het doel (de ingebouwde van Wine eenmalig bewaard als <naam>.wine-bewaard) en WINEDLLOVERRIDES krijgt <naam>=n.
    // Voor COM-DLL's (XACT) die Wine altijd uit system32 laadt; zonder override blijft Wine zijn ingebouwde gebruiken (docs/redist.md).
    std::vector<std::pair<std::string, std::string>> native_dlls;
    std::vector<DirectDrive> drives;                       // [Launch] Drives
    std::string exe_override;                              // [Launch] Exe: het Windows-pad dat de lader voor het spel krijgt
    std::vector<std::string> view_copies;                  // [Launch] ViewCopies: links in de view die een lokale kopie worden
    std::vector<std::string> view_ttc_fonts;               // [Launch] ViewTtcAsTtf: links naar een .ttc die face 0 als losse font worden (ttc.hpp)
    // [Launch] Mod = <naam> ([Mod <naam>] in game.ini, scripts/fetch-mods.py): per regel van de lijst van de mod het doel (pad in de
    // gamemap), het bestand van de mod en de offset (-1 = vervangen). apply_direct() bouwt elk doel met een offset een keer in
    // mod_built (een kopie van het origineel uit mod_game_root met de bestanden op hun offset) en linkt het doel in de view daarheen.
    // mod_root (<data>/mods/<Game>) staat er ook zonder mod: links in de view die nog naar een mod wijzen gaan terug naar het origineel.
    struct ModEntry { std::string target, src; long long offset = -1; };
    std::string mod_name, mod_root, mod_built, mod_game_root, mod_view_root, mod_tree, mod_exe, mod_exe_sha256;
    std::vector<ModEntry> mod_entries;
    std::vector<std::string> install_cmd;                 // [Launch] Install (+ InstallArgs): installatieprogramma uit de spelmap, eenmalig per prefix
    std::string install_mark;                             // markering in de prefix (naam + grootte): bestaat hij, dan is het al gedaan
    std::vector<std::string> reg_files;                   // [Launch] Registry: .reg-bestanden die rg-direct in de prefix inleest
    // [Launch] AmDaemon = true: rg-direct is voor de duur van de run de AM-daemon van de Sega Ring-games (amdaemon.hpp) op 127.0.0.1:
    // am_ports, met de antwoorden in am_values (de standaardwaarden van amd::default_values met [AmDaemon] van game.ini erover).
    bool am_daemon = false;
    std::vector<std::string> am_ports;
    EnvMap am_values;
    std::string pipe_mode, shm_source, pipehelper;         // de JVS-brug: "pipe" of "shm", bronbestand, rg-jvsbridge.exe of pipehelper.exe
    // De besturingspipe ([Input] ControlPipe op route tp-shm; leeg = geen): de indeling (control-pipe-profiles.json), de naam van de
    // pipe in de prefix (TeknoParrotPipe), het bestand dat de daemon schrijft, de grootte van een rapport en het tempo. rg-direct
    // luistert dan op een TCP-poort, stroomt het bestand elke control_interval_ms naar een tweede rg-jvsbridge in de prefix
    // ("<pipe> 127.0.0.1 <poort>"), die de pipe voor de game maakt; zoals TeknoParrotUi alleen schrijven, geen handdruk.
    std::string control_pipe, control_pipe_name, control_source;
    int control_size = 0, control_interval_ms = 0;
    std::vector<std::string> game_comms;                   // procesnamen (comm, kleine letters, 15 tekens) die "de game draait" zijn
    std::vector<DirectStart> starts;                       // de starts in hun orde (een gewone game: een)
    std::string core_dll, core_source;                     // de kern (leeg = geen) en waar hij vandaan komt (runtime | ravengun)
    // De bestanden uit de TeknoParrot-map (DirectPaths::tp) die dit plan gebruikt of gelezen heeft: lader, kern, pipehelper.exe,
    // ParrotData.xml, GameProfiles/<P>.xml, UserProfiles/<P>.xml en .env, ElfLdr2/. Leeg = het plan heeft de map niet nodig; dan is
    // `tp` ook leeg en staat er nergens in het plan een pad uit die map (docs/loskoppelen-per-game.md, "Centrale afhankelijkheden").
    std::vector<std::string> tp_files;
    std::vector<std::string> patch_lines;                  // een regel per patch (ook uitgeschakelde), zoals launch-direct.py
    std::vector<std::string> notes;                        // extra regels voor het logboek (instellingen, legacy .env)
    std::string state;                                     // $XDG_STATE_HOME/ravengun (A11)
};

struct DirectOptions {
    EnvMap base_env;            // de omgeving van de unit (direct_session_env): die van de daemon, aangevuld met die van de sessie
    std::string shm_source;     // --shm-source (leeg = /dev/shm/TeknoParrot_JvsState)
    std::string control_source; // --control-source: het rapport van de besturingspipe (leeg = /dev/shm/ravengun_ControlPipe)
    bool log_full = false;      // --log-full
    std::string session_id;     // TP_LAUNCH_SESSION_ID (leeg = een nieuwe uuid4)
    // Breedte en hoogte van het scherm voor gamescope zonder RG_GAMESCOPE_RES (leeg = xrandr --current, anders 1920x1080).
    std::function<std::pair<int, int>(const EnvMap& env)> display_size;
    // De view ([Launch] View, plan_view): maken of verversen (een start; launch-direct.py --check en --run) of alleen melden (--plan).
    bool make_view = false;
};

// Het plan. Ok: `out` is volledig. Fallback/Refused: `why` zegt waarom (Nederlands, de melding van launch-direct.py waar die er is).
Verdict plan_direct(const std::string& profile, const DirectPaths& p, const DirectOptions& o, DirectPlan& out, std::string& why);

// [Launch] ViewLinks = "<link> -> <doel>[, ...]": paden ten opzichte van `base` (de map van de exe), relatief en zonder "..".
// Een doel "origineel:<pad>" is het bestand in de map van de game zelf (`orig_base`, op de NAS), ook als de view op die plaats een
// drop-in heeft (Lethal Enforcers 3: D22/iob.dll -> origineel:iob.dll voor de bestandscontrole, terwijl de game onze iob.dll laadt).
// out krijgt per paar de absolute paden. false + err (Nederlands) bij een onleesbare waarde.
bool parse_view_links(const std::string& text, const std::string& base, std::vector<std::pair<std::string, std::string>>& out,
                      std::string& err, const std::string& orig_base = std::string());

// De drop-ins, de gegenereerde bestanden, de links van [Launch] ViewLinks en de exe met de grotere stapel ([Launch] StackReserve) in de view zetten (wat
// launch-direct.py in run() doet voordat er iets start). Nooit op de NAS.
// false + err = de game hoort niet te starten. `log` krijgt een regel per stap.
bool apply_direct(const DirectPlan& plan, const DirectPaths& p, std::vector<std::string>& log, std::string& err);

// Ontbreken de extra GStreamer-plug-ins (GST_PLUGIN_PATH zonder libgstasf.so)? Dan haalt het script ze eerst op (fetch-gst-wmv.sh).
bool needs_gst_fetch(const DirectPlan& plan);

// Alle procesnamen van het plan: game_comms plus die van elke start (bij twee exe's ook de tweede, bv. amdaemon.exe).
std::vector<std::string> plan_comms(const DirectPlan& plan);

// Het plan als JSON voor rg-direct --run, en terug.
std::string plan_to_json(const DirectPlan& plan);
bool plan_from_json(const std::string& text, DirectPlan& out, std::string& err);

// De map waarin rg-direct (systemd-run --working-directory), de JVS-brug en `wine reg import` draaien: de TeknoParrot-map als het plan
// die gebruikt (zoals altijd), anders de werkmap van de lader. Nooit een pad uit de TeknoParrot-map bij een plan zonder tp_files.
std::string plan_run_dir(const DirectPlan& plan);

// Alle verwijzingen naar de TeknoParrot-map `tp` in het plan: tp_files plus elke tekst in het JSON-plan (argv, omgeving, bestanden,
// ook in de Wine-vorm Z:\...) die in die map ligt. Relatief aan `tp`, gesorteerd, zonder dubbele. Leeg = los van TeknoParrot.
std::vector<std::string> plan_tp_refs(const DirectPlan& plan, const std::string& tp);

// Heeft deze game volgens zijn instellingen (game.ini met de gebruikerslaag, zonder het oude .env) de TeknoParrot-map niet nodig?
// Een eigen profiel met [Loader] Core = none, of een eigen lader en eigen kern: Build = ravengun en Core = ravengun (bij
// Type = OpenParrot volgt de eigen kern uit Build = ravengun). Dan leest het plan niets uit de TeknoParrot-map: geen
// GameProfiles/UserProfiles, geen ParrotData.xml, geen oud UserProfiles/<P>.env.
bool tp_free_settings(const ravengun::gamesettings::Ini& settings);

// --- De invoer, zoals play-halo.sh die voor een directe start kiest ---------------------------------------------------------------
struct DirectInput {
    std::string route;          // tp-shm | hid-board | jvs (na de regel "evdev met een indeling wordt tp-shm")
    std::string layout, board, transport, com_port;
    std::string control_pipe;   // [Input] ControlPipe (alleen bij tp-shm, ook na de omzetting van evdev): het rapport voor TeknoParrotPipe
    std::string note;           // "Directe start: invoer via tp-shm ..." als evdev is omgezet
    // Schrijft de daemon op deze route het gedeelde geheugen (/dev/shm/ravengun_JvsState), zodat de pipehelper dat bestand naar de
    // game spiegelt? tp-shm altijd; jvs als [Input] ook een Layout heeft (Let's Go Safari: het stuur komt alleen uit het gedeelde
    // geheugen, de game vraagt nooit JVS 0x22). (Zo deed play-halo.sh het met --shm-source.)
    // Alleen tp-shm: bij jvs met een Layout schrijft de daemon zijn bytes in het bestand van rg-direct zelf (iobridge_service.cpp,
    // start_jvs), dus de spiegel blijft dat bestand lezen.
    bool writes_tp_shm() const { return route == "tp-shm"; }
};
// [Input] van de game, gecontroleerd zoals gamesettings.resolve_input (klopt de gebruikerslaag niet, dan de meegeleverde, anders evdev).
// Fallback: een route die de engine niet zelf voorbereidt (evdev zonder indeling, unity-tcp, jvs over een COM-poort). ControlPipe hoort
// bij tp-shm (of evdev, dat hier tp-shm wordt); bij een andere route is [Input] ongeldig (dan de meegeleverde laag, anders evdev).
Verdict direct_input(const std::string& profile, const DirectPaths& p, DirectInput& out, std::string& why);
// [Launch] Mode van de game (direct | teknoparrot-ui | leeg), met de gebruikerslaag eroverheen.
std::string launch_mode(const std::string& profile, const DirectPaths& p);
// De omgeving uit game.ini (met het oude .env als laag); gelijk aan `gamesettings.py env PROFIEL`.
EnvMap profile_env(const std::string& profile, const DirectPaths& p);

// --- hid-board: het bord krijgt joystick-id 0 in de prefix (tools/dev/wine-joyid.sh) -----------------------------------------------
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
