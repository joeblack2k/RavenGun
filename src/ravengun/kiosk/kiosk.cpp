#include "ravengun/kiosk/kiosk.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace ravengun::kiosk {

namespace fs = std::filesystem;

std::string profile_dir() {
    const char* x = std::getenv("XDG_STATE_HOME");
    const char* h = std::getenv("HOME");
    std::string base = x && x[0] == '/' ? x : (h && h[0] == '/' ? std::string(h) + "/.local/state" : std::string());
    return base.empty() ? std::string() : base + "/ravengun/kiosk";
}

std::vector<std::string> flatpak_args(const std::string& url, const std::string& profile) {
    return {"run",
            "--filesystem=" + profile,
            kBrowserAppId,
            "--user-data-dir=" + profile,
            "--kiosk",
            "--app=" + url,
            "--lang=nl",
            "--no-first-run",
            "--no-default-browser-check",
            "--noerrdialogs",
            "--disable-infobars",
            "--disable-session-crashed-bubble",
            "--disable-features=Translate,InfiniteSessionRestore",
            "--password-store=basic",
            "--window-position=0,0"};
}

namespace {
std::vector<std::string> split_nul(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\0', pos);
        if (e == std::string::npos) e = s.size();
        out.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    return out;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

std::vector<int> scan(const std::string& profile) {
    std::vector<int> pids;
    DIR* d = ::opendir("/proc");
    if (!d) return pids;
    int self = static_cast<int>(::getpid());
    while (dirent* e = ::readdir(d)) {
        char* end = nullptr;
        long pid = std::strtol(e->d_name, &end, 10);
        if (!end || *end || pid <= 0 || pid == self) continue;
        std::string base = std::string("/proc/") + e->d_name;
        struct stat st{};
        if (::stat(base.c_str(), &st) != 0 || st.st_uid != ::getuid()) continue;
        if (cmdline_is_kiosk(read_file(base + "/cmdline"), profile)) pids.push_back(static_cast<int>(pid));
    }
    ::closedir(d);
    return pids;
}

std::string find_xauthority() {
    DIR* d = ::opendir("/proc");
    if (!d) return {};
    std::string found;
    while (dirent* e = ::readdir(d)) {
        char* end = nullptr;
        long pid = std::strtol(e->d_name, &end, 10);
        if (!end || *end || pid <= 0) continue;
        std::string base = std::string("/proc/") + e->d_name;
        std::string comm = read_file(base + "/comm");
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
        if (comm != "kwin_wayland") continue;
        struct stat st{};
        if (::stat(base.c_str(), &st) != 0 || st.st_uid != ::getuid()) continue;
        found = xauthority_from_cmdline(read_file(base + "/cmdline"));
        if (!found.empty()) break;
    }
    ::closedir(d);
    return found;
}

// Alleen bij een nieuw profiel: Brave-meldingsbalken (analyse, vertalen) staan uit, zodat de pagina
// de hele tv vult. Een bestaand bestand wordt nooit overschreven.
void seed_profile(const std::string& prof) {
    auto put = [](const std::string& path, const char* text) {
        if (fs::exists(path)) return;
        std::ofstream f(path, std::ios::binary);
        f << text;
    };
    std::error_code ec;
    fs::create_directories(prof + "/Default", ec);
    put(prof + "/Local State",
        "{\"brave\":{\"p3a\":{\"enabled\":false,\"notice_acknowledged\":true},\"stats\":{\"reporting_enabled\":false}}}");
    put(prof + "/Default/Preferences",
        "{\"translate\":{\"enabled\":false},\"intl\":{\"accept_languages\":\"nl,en\"},"
        "\"translate_blocked_languages\":[\"nl\",\"en\"],\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true}}");
}

void pause_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

std::string xauthority_from_cmdline(const std::string& nul_separated) {
    auto a = split_nul(nul_separated);
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == "--xwayland-xauthority") return a[i + 1];
    return {};
}

bool cmdline_is_kiosk(const std::string& nul_separated, const std::string& profile) {
    if (profile.empty()) return false;
    const std::string needle = "--user-data-dir=" + profile;
    for (const auto& a : split_nul(nul_separated))
        if (a == needle) return true;
    return false;
}

Result status() {
    Result r;
    std::string prof = profile_dir();
    if (prof.empty()) { r.message = "geen HOME of XDG_STATE_HOME bekend"; return r; }
    r.processes = static_cast<int>(scan(prof).size());
    r.ok = true;
    r.running = r.processes > 0;
    r.message = r.running ? "kalibratiescherm is open op de tv" : "kalibratiescherm is niet open";
    return r;
}

Result open(int port) {
    Result r;
    std::string prof = profile_dir();
    if (prof.empty()) { r.message = "geen HOME of XDG_STATE_HOME bekend"; return r; }
    if (port < 1 || port > 65535) { r.message = "ongeldige webpoort"; return r; }
    int n = static_cast<int>(scan(prof).size());
    if (n > 0) { r.ok = true; r.running = true; r.processes = n; r.message = "kalibratiescherm was al open op de tv"; return r; }

    std::error_code ec;
    fs::create_directories(prof, ec);
    if (ec) { r.message = "kan " + prof + " niet maken: " + ec.message(); return r; }
    ::chmod(prof.c_str(), 0700);
    seed_profile(prof);

    // Sessievariabelen: de daemon draait niet per se in de grafische sessie.
    std::string uid = std::to_string(::getuid());
    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        for (const char* k : {"WAYLAND_DISPLAY=", "XDG_RUNTIME_DIR=", "DBUS_SESSION_BUS_ADDRESS=", "XAUTHORITY="})
            if (kv.rfind(k, 0) == 0) { kv.clear(); break; }
        if (!kv.empty()) env.push_back(kv);
    }
    env.push_back("WAYLAND_DISPLAY=wayland-0");
    env.push_back("XDG_RUNTIME_DIR=/run/user/" + uid);
    env.push_back("DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + uid + "/bus");
    bool has_display = false;
    for (const auto& kv : env) if (kv.rfind("DISPLAY=", 0) == 0) has_display = true;
    if (!has_display) env.push_back("DISPLAY=:0");
    std::string xa = find_xauthority();
    if (!xa.empty()) env.push_back("XAUTHORITY=" + xa);

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/calibrate";
    std::vector<std::string> args{"flatpak"};
    for (auto& a : flatpak_args(url, prof)) args.push_back(a);
    std::vector<char*> argv, envp;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    for (auto& a : env) envp.push_back(a.data());
    envp.push_back(nullptr);
    std::string logpath = prof + "/kiosk.log";

    pid_t p1 = ::fork();
    if (p1 < 0) { r.message = std::string("fork mislukt: ") + std::strerror(errno); return r; }
    if (p1 == 0) {
        // Dubbele fork: de browser raakt zijn ouder kwijt en hoeft door niemand opgehaald te worden.
        if (::fork() != 0) _exit(0);
        ::setsid();
        int in = ::open("/dev/null", O_RDONLY);
        int out = ::open(logpath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (in >= 0) ::dup2(in, 0);
        if (out >= 0) { ::dup2(out, 1); ::dup2(out, 2); }
        for (int fd = 3; fd < 256; ++fd) ::close(fd);  // geen sockets van de daemon doorgeven
        ::execvpe("flatpak", argv.data(), envp.data());
        _exit(127);
    }
    int st = 0;
    ::waitpid(p1, &st, 0);
    for (int i = 0; i < 20 && scan(prof).empty(); ++i) pause_ms(100);  // zodat 'status' direct klopt
    r.processes = static_cast<int>(scan(prof).size());
    r.running = r.processes > 0;
    r.ok = r.running;
    r.message = r.running ? "kalibratiescherm geopend op de tv" : "browser lijkt niet gestart: zie " + logpath;
    return r;
}

Result close() {
    Result r;
    std::string prof = profile_dir();
    if (prof.empty()) { r.message = "geen HOME of XDG_STATE_HOME bekend"; return r; }
    auto pids = scan(prof);
    if (pids.empty()) { r.ok = true; r.message = "kalibratiescherm was niet open"; return r; }
    for (int p : pids) ::kill(p, SIGTERM);
    for (int i = 0; i < 25 && !scan(prof).empty(); ++i) pause_ms(100);
    auto left = scan(prof);
    for (int p : left) ::kill(p, SIGKILL);
    if (!left.empty())
        for (int i = 0; i < 10 && !scan(prof).empty(); ++i) pause_ms(100);
    r.processes = static_cast<int>(scan(prof).size());
    r.running = r.processes > 0;
    r.ok = !r.running;
    r.message = r.ok ? "kalibratiescherm gesloten" : "kon het kalibratiescherm niet sluiten";
    return r;
}

}  // namespace ravengun::kiosk
