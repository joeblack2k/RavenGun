// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SaverLoop.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

extern char** environ;

namespace rg::saver {

namespace {
const char kView[] = "ravengun-oled-view";
const char kTpui[] = "ravengun-tpui.service";

bool Contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool IsNumber(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}
std::string EnvOr(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : def;
}
std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Start een programma en wacht erop; 0 = gelukt, anders de exitcode (-1 als het niet te starten was). Uitvoer gaat naar /dev/null.
int RunProgram(const std::string& program, const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, program.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string Systemctl() { return EnvOr("RAVENGUN_SYSTEMCTL", "systemctl"); }
std::string SystemdRun() { return EnvOr("RAVENGUN_SYSTEMD_RUN", "systemd-run"); }

std::string StateDir() {
    const char* x = std::getenv("XDG_STATE_HOME");
    if (x && *x) return std::string(x) + "/ravengun";
    return EnvOr("HOME", "/") + "/.local/state/ravengun";
}
std::string RuntimeDir() { return EnvOr("XDG_RUNTIME_DIR", "/run/user/" + std::to_string(getuid())); }

bool ViewOn() { return RunProgram(Systemctl(), ViewActiveArgs()) == 0; }

// Atomisch schrijven, zodat het bord nooit een half bestand leest.
void WriteGameState(GameState s) {
    const std::string dir = StateDir();
    mkdir(dir.c_str(), 0755);  // de ouders bestaan in de praktijk (~/.local/state)
    const std::string path = dir + "/saver-game.txt", tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << GameStateFileText(s);
    }
    std::rename(tmp.c_str(), path.c_str());
}

std::string ViewBinary() {
    const char* o = std::getenv("RG_SAVER_VIEW_BIN");
    if (o && *o) return o;
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);  // dit is `ravengun` zelf
    std::string bin = n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
    // Na een installatie draait deze lus nog op het vervangen bestand: /proc/self/exe wijst dan naar de oude map met " (deleted)"
    // erachter en het bord kwam nooit meer (2026-10-18 12:45-13:21: anderhalf uur de app stil op de oled). Dan de geinstalleerde
    // `ravengun` van nu nemen: de map van de installatie ($RAVENGUN_PREFIX/bin, anders ~/.local/opt/ravengun/bin).
    static const std::string kDeleted = " (deleted)";
    if (bin.empty() || access(bin.c_str(), X_OK) != 0 ||
        (bin.size() > kDeleted.size() && bin.compare(bin.size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0)) {
        const std::string prefix = EnvOr("RAVENGUN_PREFIX", EnvOr("HOME", "/") + "/.local/opt/ravengun");
        bin = prefix + "/bin/ravengun";
    }
    return bin;
}

void ViewStart() {
    const std::string bin = ViewBinary();
    if (bin.empty() || access(bin.c_str(), X_OK) != 0) {
        std::fprintf(stderr, "ravengun-app niet gevonden\n");
        return;
    }
    RunProgram(SystemdRun(), ViewStartArgs(ViewEnvFrom([](const char* n) { return std::getenv(n); }, getuid()), bin));
}

void ViewStop() {
    RunProgram(Systemctl(), ViewStopArgs());
    // Oude installaties: de Brave-kiosk (flatpak start Brave buiten de cgroup van de unit). Alleen processen met het oude
    // saver-profiel; andere Brave-vensters van de gebruiker blijven staan.
    const std::string prof = LegacyBrowserProfileArg(EnvOr("HOME", "/"));
    for (const Proc& p : ListProcesses(EnvOr("RG_SAVER_PROC", "/proc"), getuid()))
        if (p.pid != getpid() && Contains(p.args, prof.c_str())) kill(p.pid, SIGTERM);
}

std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop = true; }

}  // namespace

const char* GameStateName(GameState s) {
    switch (s) {
        case GameState::Zichtbaar: return "zichtbaar";
        case GameState::Onzichtbaar: return "onzichtbaar";
        default: return "geen";
    }
}

GameState ClassifyGameState(const std::vector<Proc>& procs, const GameProbe& probe) {
    if (probe.screenTaken) return GameState::Zichtbaar;
    bool gsTv = false, gsHeadless = false, metScherm = false, zonderScherm = false;
    for (const Proc& p : procs) {
        if (p.pid <= 0 || p.args.empty()) continue;
        const std::string& a = p.args;
        if (Contains(a, "oled-saver.sh") || Contains(a, "--saver-loop") || Contains(a, "--saver-stop-view") || Contains(a, "rg-saver-status"))
            continue;  // wijzelf
        if (StartsWith(a, "gamescope ") || Contains(a, "/gamescope ")) {
            if (Contains(a, "--backend headless")) gsHeadless = true; else gsTv = true;
        } else if (Contains(a, "rg-direct --run") || Contains(a, "BudgieLoader") || Contains(a, "Game.exe")) {
            if (probe.hasDisplay && probe.hasDisplay(p.pid)) metScherm = true; else zonderScherm = true;
        }
    }
    if (gsTv) return GameState::Zichtbaar;
    if (gsHeadless) return GameState::Onzichtbaar;
    if (probe.tpuiActive && probe.tpuiActive()) return GameState::Zichtbaar;
    if (metScherm) return GameState::Zichtbaar;
    if (zonderScherm) return GameState::Onzichtbaar;
    return GameState::Geen;
}

Step NextStep(int idle, int idleSeconds, GameState state, bool viewOn) {
    Step s{idle, ViewAction::None};
    switch (state) {
        case GameState::Zichtbaar:
            s.idle = 0;
            if (viewOn) s.action = ViewAction::Stop;
            break;
        case GameState::Onzichtbaar:
            s.idle = idleSeconds;
            if (!viewOn) s.action = ViewAction::Start;
            break;
        default:
            s.idle = idle + kLoopSeconds;
            if (s.idle >= idleSeconds && !viewOn) s.action = ViewAction::Start;
            break;
    }
    return s;
}

std::string GameStateFileText(GameState s) { return std::string(GameStateName(s)) + "\n"; }

ViewEnv ViewEnvFrom(const std::function<const char*(const char*)>& getenv, unsigned uid) {
    auto get = [&](const char* n, const std::string& def) {
        const char* v = getenv(n);
        return (v && *v) ? std::string(v) : def;
    };
    ViewEnv e;
    e.runtimeDir = get("XDG_RUNTIME_DIR", "/run/user/" + std::to_string(uid));
    e.waylandDisplay = get("WAYLAND_DISPLAY", "wayland-0");
    e.dbusAddress = get("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + e.runtimeDir + "/bus");
    return e;
}

std::vector<std::string> ViewStartArgs(const ViewEnv& env, const std::string& bin) {
    return {"--user",
            std::string("--unit=") + kView,
            "--collect",
            "--quiet",
            "-E", "XDG_RUNTIME_DIR=" + env.runtimeDir,
            "-E", "WAYLAND_DISPLAY=" + env.waylandDisplay,
            "-E", "DBUS_SESSION_BUS_ADDRESS=" + env.dbusAddress,
            bin,
            "--saver"};
}
std::vector<std::string> ViewStopArgs() { return {"--user", "stop", std::string(kView) + ".service"}; }
std::vector<std::string> ViewActiveArgs() { return {"--user", "is-active", "--quiet", std::string(kView) + ".service"}; }
std::string LegacyBrowserProfileArg(const std::string& home) { return "--user-data-dir=" + home + "/.local/state/ravengun/saver-browser"; }

std::string ProcStartTime(const std::string& stat) {
    const size_t close = stat.rfind(')');
    if (close == std::string::npos) return {};
    std::istringstream in(stat.substr(close + 1));
    std::string tok;
    for (int field = 3; in >> tok; ++field)  // na (comm) begint veld 3 (state)
        if (field == 22) return tok;
    return {};
}

bool ScreenTaken(const std::string& holdDir, const std::string& procRoot) {
    DIR* d = opendir(holdDir.c_str());
    if (!d) return false;
    std::vector<std::string> names;
    while (dirent* e = readdir(d))
        if (e->d_name[0] != '.') names.push_back(e->d_name);
    closedir(d);
    bool taken = false;
    for (const std::string& pid : names) {
        const std::string f = holdDir + "/" + pid;
        struct stat st {};
        if (stat(f.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        std::string start = ReadFile(f);
        start = start.substr(0, start.find('\n'));
        std::string now;
        if (IsNumber(pid)) now = ProcStartTime(ReadFile(procRoot + "/" + pid + "/stat"));
        struct stat ps {};
        const bool alive = IsNumber(pid) && stat((procRoot + "/" + pid).c_str(), &ps) == 0 && S_ISDIR(ps.st_mode);
        if (alive && (start.empty() || start == "0" || now.empty() || now == start)) taken = true;
        else unlink(f.c_str());
    }
    return taken;
}

std::vector<Proc> ListProcesses(const std::string& procRoot, unsigned uid) {
    std::vector<Proc> out;
    DIR* d = opendir(procRoot.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (!IsNumber(name)) continue;
        struct stat st {};
        if (stat((procRoot + "/" + name).c_str(), &st) != 0 || st.st_uid != uid) continue;
        std::string cmd = ReadFile(procRoot + "/" + name + "/cmdline");
        while (!cmd.empty() && cmd.back() == '\0') cmd.pop_back();
        if (cmd.empty()) continue;
        for (char& c : cmd)
            if (c == '\0') c = ' ';
        out.push_back({std::atoi(name.c_str()), cmd});
    }
    closedir(d);
    return out;
}

bool ProcHasDisplay(const std::string& procRoot, int pid) {
    const std::string env = ReadFile(procRoot + "/" + std::to_string(pid) + "/environ");
    size_t pos = 0;
    while (pos < env.size()) {
        size_t end = env.find('\0', pos);
        if (end == std::string::npos) end = env.size();
        const std::string kv = env.substr(pos, end - pos);
        if ((StartsWith(kv, "DISPLAY=") && kv.size() > 8) || (StartsWith(kv, "WAYLAND_DISPLAY=") && kv.size() > 16)) return true;
        pos = end + 1;
    }
    return false;
}

int RunStopView() {
    ViewStop();
    return 0;
}

int RunSaverLoop() {
    int idleSeconds = 600;
    if (const char* v = std::getenv("RG_SAVER_IDLE_S")) {
        const int n = std::atoi(v);
        if (n > 0) idleSeconds = n;
    }
    const std::string procRoot = EnvOr("RG_SAVER_PROC", "/proc");
    const std::string holdDir = EnvOr("RG_SAVER_HOLD_DIR", RuntimeDir() + "/ravengun/tv-hold");
    // Zonder SA_RESTART, zodat de slaap meteen wordt onderbroken.
    struct sigaction sa {};
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    int idle = 0;
    while (!g_stop) {
        GameProbe probe;
        probe.screenTaken = ScreenTaken(holdDir, procRoot);
        probe.hasDisplay = [&](int pid) { return ProcHasDisplay(procRoot, pid); };
        probe.tpuiActive = [] { return RunProgram(Systemctl(), {"--user", "is-active", "--quiet", kTpui}) == 0; };
        const GameState gs = ClassifyGameState(ListProcesses(procRoot, getuid()), probe);
        WriteGameState(gs);
        const Step step = NextStep(idle, idleSeconds, gs, ViewOn());
        idle = step.idle;
        if (step.action == ViewAction::Start) ViewStart();
        else if (step.action == ViewAction::Stop) ViewStop();
        for (int i = 0; i < kLoopSeconds * 10 && !g_stop; ++i) {
            timespec ts{0, 100 * 1000 * 1000};
            nanosleep(&ts, nullptr);
        }
    }
    ViewStop();  // zoals de EXIT-trap van het script
    return 0;
}

}  // namespace rg::saver
