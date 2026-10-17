// SPDX-License-Identifier: GPL-3.0-or-later
// De lus van de OLED-bescherming (`ravengun --saver-loop`, ExecStart van ravengun-oled-saver.service) en het dichtdoen van
// het zwarte scherm (`ravengun --saver-stop-view`, ExecStopPost). Vervangt scripts/oled-saver.sh --loop/--stop-view:
// geen shellscript in het product, alles uit de app.
//
// Staat er langer dan idleSeconds geen game op de tv, dan start de lus `ravengun --saver` als transient unit
// ravengun-oled-view (systemd-run); zodra er een game MET BEELD draait, wordt die unit gestopt. De tv toont dus altijd een
// game of het saver-venster -- nooit een browser (een balk daarvan brandt in). Draait er nog een oude Brave-kiosk van het
// saver-profiel, dan wordt die bij het stoppen opgeruimd.
//
// Alleen de standaardbibliotheek en POSIX (geen Qt): de lus draait zonder QApplication en dus ook zonder scherm.
// De beslislogica (classificatie, idle-stap, bestandsinhoud, opdrachtregels) is puur en wordt getest in tests/app/test_app_core.cpp.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace rg::saver {

enum class GameState { Geen, Zichtbaar, Onzichtbaar };
// De woorden in saver-game.txt (door SaverWindow::LoadGame gelezen): geen | zichtbaar | onzichtbaar.
const char* GameStateName(GameState s);

struct Proc {
    int pid = 0;
    std::string args;  // de opdrachtregel zoals `ps -o args=`: argumenten met een spatie ertussen
};

// Wat classificeren nodig heeft van de omgeving; functies zodat de test ze kan vervangen.
struct GameProbe {
    bool screenTaken = false;                 // een houder van tools/dev/rg-tv-env.sh met een levend proces
    std::function<bool(int pid)> hasDisplay;  // heeft dit proces DISPLAY of WAYLAND_DISPLAY in zijn omgeving
    std::function<bool()> tpuiActive;         // is ravengun-tpui.service actief (alleen gevraagd als de rest niets beslist)
};

// geen | zichtbaar | onzichtbaar, in dezelfde volgorde als het script:
//   0. een houder met een levend proces: zichtbaar (gaat voor alles);
//   1. een gamescope beslist: gewone = zichtbaar, `--backend headless` = onzichtbaar (zichtbaar gaat voor onzichtbaar);
//   2. de eenheid ravengun-tpui is actief: zichtbaar;
//   3. rg-direct --run / BudgieLoader / Game.exe: zichtbaar met scherm in de omgeving, anders onzichtbaar;
//   4. anders geen. De lus zelf (`--saver-loop`, het oude oled-saver.sh) en rg-saver-status tellen niet mee.
GameState ClassifyGameState(const std::vector<Proc>& procs, const GameProbe& probe);

enum class ViewAction { None, Start, Stop };
struct Step {
    int idle = 0;
    ViewAction action = ViewAction::None;
};
inline constexpr int kLoopSeconds = 3;  // pauze tussen twee rondes; idle telt hiermee op
// Eén ronde van de lus: zichtbaar = idle 0 en het zwarte scherm dicht; onzichtbaar = meteen idle=idleSeconds en
// het scherm open; geen = idle + 3 en open zodra idle >= idleSeconds.
Step NextStep(int idle, int idleSeconds, GameState state, bool viewOn);

// Inhoud van saver-game.txt: het woord plus een regeleinde.
std::string GameStateFileText(GameState s);

// De omgeving waarmee de zwarte-schermunit wordt gestart (van de unit zelf, met dezelfde standaardwaarden als het script).
struct ViewEnv {
    std::string runtimeDir;      // XDG_RUNTIME_DIR, standaard /run/user/<uid>
    std::string waylandDisplay;  // WAYLAND_DISPLAY, standaard wayland-0
    std::string dbusAddress;     // DBUS_SESSION_BUS_ADDRESS, standaard unix:path=<runtimeDir>/bus
};
ViewEnv ViewEnvFrom(const std::function<const char*(const char*)>& getenv, unsigned uid);
// De argumenten (zonder programmanaam) van `systemd-run` die `<bin> --saver` als ravengun-oled-view starten.
std::vector<std::string> ViewStartArgs(const ViewEnv& env, const std::string& bin);
// Argumenten van `systemctl` om de zwarte-schermunit te stoppen resp. te bevragen.
std::vector<std::string> ViewStopArgs();
std::vector<std::string> ViewActiveArgs();
// Het argument dat de oude Brave-kiosk van het saver-profiel herkent.
std::string LegacyBrowserProfileArg(const std::string& home);

// Veld 22 (starttijd) van een /proc/<pid>/stat; (comm) kan spaties bevatten. Leeg als de regel niet te lezen is.
std::string ProcStartTime(const std::string& statLine);
// Heeft een run het scherm genomen? holdDir bevat per run een bestand <pid> met de starttijd van dat proces op regel 1
// (leeg of 0 = niet controleren). Een houder van een verdwenen proces (of een ander proces met hetzelfde pid) wordt verwijderd.
bool ScreenTaken(const std::string& holdDir, const std::string& procRoot);
// De processen van deze gebruiker uit procRoot (/proc of een namaak-map); processen zonder opdrachtregel tellen niet.
std::vector<Proc> ListProcesses(const std::string& procRoot, unsigned uid);
// Heeft het proces een scherm in zijn omgeving? Niet te lezen = geen scherm.
bool ProcHasDisplay(const std::string& procRoot, int pid);

// De programma's. Omgevingsvariabelen (voor tests): RG_SAVER_IDLE_S, RG_SAVER_PROC, RG_SAVER_HOLD_DIR, RAVENGUN_SYSTEMCTL,
// RAVENGUN_SYSTEMD_RUN, RG_SAVER_VIEW_BIN.
int RunSaverLoop();  // tot SIGTERM/SIGINT; sluit het zwarte scherm bij het stoppen; exitcode 0
int RunStopView();   // de unit en een eventuele oude browser-kiosk stoppen; exitcode 0

}  // namespace rg::saver
