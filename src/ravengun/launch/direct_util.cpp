// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10): tekst-, pad- en XML-hulpen van launch-direct.py (posixpath, shlex, ...), de kleine publieke hulpen uit direct.hpp
// (wine_path, shlex_split, win_split, ...) en de joystick-id-hulpen van de hid-board-route. Zie direct.hpp en direct_internal.hpp.
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/direct_internal.hpp"

extern char** environ;

namespace ravengun::launch {

namespace direct_detail {

// --- tekst --------------------------------------------------------------------------------------------------------------------------
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return out;
        a = b + 1;
    }
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}
// launch-direct.py truthy(): alleen "true" (anders dan gamesettings.truthy).
bool ld_truthy(const std::string& v) { return lower(trim(v)) == "true"; }
bool gs_falsy(const std::string& v) {
    const std::string l = lower(trim(v));
    return l == "false" || l == "0" || l == "no" || l == "off";
}

// --- paden (posixpath) ---------------------------------------------------------------------------------------------------------------
std::string normpath(const std::string& p) {
    if (p.empty()) return ".";
    size_t lead = 0;
    while (lead < p.size() && p[lead] == '/') ++lead;
    const std::string initial = lead == 0 ? "" : (lead == 2 ? "//" : "/");
    std::vector<std::string> comps;
    for (const std::string& c : split(p, '/')) {
        if (c.empty() || c == ".") continue;
        if (c != ".." || (initial.empty() && comps.empty()) || (!comps.empty() && comps.back() == "..")) comps.push_back(c);
        else if (!comps.empty()) comps.pop_back();
    }
    const std::string out = initial + join(comps, "/");
    return out.empty() ? "." : out;
}
namespace {
std::string cwd_now() {
    char b[4096];
    return ::getcwd(b, sizeof b) ? std::string(b) : std::string("/");
}
}  // namespace

std::string abspath(const std::string& p) { return normpath(!p.empty() && p[0] == '/' ? p : cwd_now() + "/" + p); }
std::string pjoin(const std::string& a, const std::string& b) {
    if (!b.empty() && b[0] == '/') return b;
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
}
std::string dirname(const std::string& p) {
    const size_t i = p.rfind('/');
    if (i == std::string::npos) return "";
    std::string head = p.substr(0, i + 1);
    if (head.find_first_not_of('/') != std::string::npos)
        while (!head.empty() && head.back() == '/') head.pop_back();
    return head;
}
std::string basename(const std::string& p) {
    const size_t i = p.rfind('/');
    return i == std::string::npos ? p : p.substr(i + 1);
}
std::string realpath_(const std::string& p) {
    std::error_code ec;
    const fs::path r = fs::weakly_canonical(fs::path(abspath(p)), ec);
    if (ec) return abspath(p);
    std::string s = r.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}
std::string relpath(const std::string& path, const std::string& start) {
    auto comps = [](const std::string& x) {
        std::vector<std::string> v;
        for (const std::string& c : split(abspath(x), '/'))
            if (!c.empty()) v.push_back(c);
        return v;
    };
    const auto a = comps(path), b = comps(start);
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    std::vector<std::string> out(b.size() - i, "..");
    out.insert(out.end(), a.begin() + static_cast<long>(i), a.end());
    return out.empty() ? "." : join(out, "/");
}
bool binnen(const std::string& path, const std::string& base) {
    const std::string p = abspath(path), b = abspath(base);
    return p == b || starts_with(p, b == "/" ? "/" : b + "/");
}
bool isfile(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool isdir(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool lexists(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::lstat(p.c_str(), &st) == 0;
}
bool islink(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
std::string strip_bom(std::string s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
}
std::string expand_home(const std::string& p, const std::string& home) {
    if (p == "~") return home;
    if (starts_with(p, "~/")) return pjoin(home, p.substr(2));
    return p;
}
std::string shrink_home(const std::string& p, const std::string& home) {
    std::string h = home;
    while (h.size() > 1 && h.back() == '/') h.pop_back();
    if (p == h) return "~";
    return starts_with(p, h + "/") ? "~/" + p.substr(h.size() + 1) : p;
}
std::string getenv_or(const EnvMap& env, const std::string& k, const std::string& def) {
    auto it = env.find(k);
    return it == env.end() ? def : it->second;
}

// launch-direct.py on_nas(): ~/mnt of een netwerkbestandssysteem (langste koppelpunt in /proc/self/mounts).
bool on_nas(const std::string& path, const DirectPaths& p) {
    const std::string real = realpath_(path);
    if (starts_with(real, pjoin(p.home, "mnt") + "/")) return true;
    std::ifstream f(p.mounts);
    if (!f) return false;
    std::string line, best, fstype;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string dev, mp, type;
        if (!(is >> dev >> mp >> type)) continue;
        for (size_t at = mp.find("\\040"); at != std::string::npos; at = mp.find("\\040", at + 1)) mp.replace(at, 4, " ");
        std::string mps = mp;
        while (!mps.empty() && mps.back() == '/') mps.pop_back();
        if ((real == mp || starts_with(real, mps + "/")) && mp.size() > best.size()) { best = mp; fstype = type; }
    }
    return fstype == "cifs" || fstype == "smb3" || fstype == "smbfs" || fstype == "nfs" || fstype == "nfs4" || fstype == "fuse.sshfs";
}

// --- XML (alleen de platte velden die launch-direct.py leest) ----------------------------------------------------------------------
namespace {
std::string xml_unescape(const std::string& s) {
    static const std::pair<const char*, const char*> ent[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool done = false;
        if (s[i] == '&')
            for (const auto& e : ent)
                if (s.compare(i, std::strlen(e.first), e.first) == 0) { out += e.second; i += std::strlen(e.first); done = true; break; }
        if (!done) out += s[i++];
    }
    return out;
}
}  // namespace

// De tekst van het eerste <tag>...</tag> of <tag/> (found = false als de tag er niet is).
std::string xml_tag(const std::string& xml, const std::string& tag, bool* found) {
    if (found) *found = false;
    for (size_t at = xml.find("<" + tag); at != std::string::npos; at = xml.find("<" + tag, at + 1)) {
        size_t e = at + 1 + tag.size();
        if (e >= xml.size()) break;
        while (e < xml.size() && std::isspace(static_cast<unsigned char>(xml[e]))) ++e;
        if (e < xml.size() && xml[e] == '/') { if (found) *found = true; return {}; }
        if (xml[at + 1 + tag.size()] != '>') continue;
        const size_t b = xml.find("</" + tag + ">", at);
        if (b == std::string::npos) return {};
        if (found) *found = true;
        return xml_unescape(xml.substr(at + tag.size() + 2, b - at - tag.size() - 2));
    }
    return {};
}

// --- kleine hulpen die meer dan een bestand nodig heeft ----------------------------------------------------------------------------
std::string kv_get(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k, const std::string& def) {
    for (const auto& p : kv) if (p.first == k) return p.second;
    return def;
}

bool py_int(const std::string& s, long long& out) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    size_t i = 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    if (i >= t.size()) return false;
    for (size_t j = i; j < t.size(); ++j)
        if (!std::isdigit(static_cast<unsigned char>(t[j])) && t[j] != '_') return false;
    std::string d = t;
    d.erase(std::remove(d.begin(), d.end(), '_'), d.end());
    try { out = std::stoll(d); } catch (...) { return false; }
    return true;
}

// float() van Python voor [Launch] SecondWait: decimaal of met exponent, inf en nan; geen hex. false = geen getal.
bool py_float(const std::string& s, double& out) {
    const std::string t = trim(s);
    if (t.empty() || lower(t).find('x') != std::string::npos) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
}

bool samefile(const std::string& a, const std::string& b) {
    struct stat sa{}, sb{};
    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

bool is_pe(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char b[2] = {0, 0};
    return f.read(b, 2) && b[0] == 'M' && b[1] == 'Z';
}

std::vector<std::string> nonempty_split(const std::string& s, char sep) {
    std::vector<std::string> out;
    for (const std::string& x : split(s, sep)) if (!x.empty()) out.push_back(x);
    return out;
}

}  // namespace direct_detail

using namespace direct_detail;

// -----------------------------------------------------------------------------------------------------------------------------
// Publieke hulpen

std::string wine_path(const std::string& p) {
    std::string out = "Z:" + p;
    std::replace(out.begin(), out.end(), '/', '\\');
    return out;
}

std::vector<std::string> exe_comms(const std::vector<std::string>& names) {
    std::vector<std::string> out;
    for (const std::string& naam : names) {
        size_t a = 0;
        for (;;) {   // re.split(r"[;|]", naam)
            const size_t b = naam.find_first_of(";|", a);
            std::string part = naam.substr(a, b == std::string::npos ? std::string::npos : b - a);
            std::replace(part.begin(), part.end(), '\\', '/');
            std::string n = lower(trim(basename(part)));
            if (n.size() > 15) n.resize(15);
            if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
            if (b == std::string::npos) break;
            a = b + 1;
        }
    }
    return out;
}

std::vector<std::string> plan_comms(const DirectPlan& pl) {
    std::vector<std::string> out = pl.game_comms;
    for (const DirectStart& s : pl.starts)
        for (const std::string& c : s.comms)
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    return out;
}

std::string new_uuid4() {
    unsigned char b[16] = {0};
    std::ifstream f("/dev/urandom", std::ios::binary);
    f.read(reinterpret_cast<char*>(b), 16);
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    char out[37];
    std::snprintf(out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                  b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return out;
}

EnvMap environ_map() {
    EnvMap out;
    for (char** e = environ; e && *e; ++e) {
        const char* eq = std::strchr(*e, '=');
        if (eq) out.emplace(std::string(*e, static_cast<size_t>(eq - *e)), std::string(eq + 1));
    }
    return out;
}

std::vector<std::string> shlex_split(const std::string& s, bool* ok) {
    std::vector<std::string> out;
    std::string cur;
    bool in_tok = false;
    if (ok) *ok = true;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\'') {
            in_tok = true;
            const size_t e = s.find('\'', i + 1);
            if (e == std::string::npos) { if (ok) *ok = false; return out; }
            cur += s.substr(i + 1, e - i - 1);
            i = e;
        } else if (c == '"') {
            in_tok = true;
            size_t j = i + 1;
            for (; j < s.size() && s[j] != '"'; ++j) {
                if (s[j] == '\\' && j + 1 < s.size() && (s[j + 1] == '"' || s[j + 1] == '\\')) { cur += s[++j]; continue; }
                cur += s[j];
            }
            if (j >= s.size()) { if (ok) *ok = false; return out; }
            i = j;
        } else if (c == '\\') {
            in_tok = true;
            if (i + 1 >= s.size()) { if (ok) *ok = false; return out; }
            cur += s[++i];
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (in_tok) { out.push_back(cur); cur.clear(); in_tok = false; }
        } else {
            in_tok = true;
            cur += c;
        }
    }
    if (in_tok) out.push_back(cur);
    return out;
}

std::vector<std::string> win_split(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false, bezig = false;
    size_t i = 0;
    const size_t n = t.size();
    while (i < n) {
        const char c = t[i];
        if (c == '\\') {
            size_t j = i;
            while (j < n && t[j] == '\\') ++j;
            const size_t aantal = j - i;
            if (j < n && t[j] == '"') {
                cur += std::string(aantal / 2, '\\');
                if (aantal % 2) { cur += '"'; ++j; }
            } else {
                cur += std::string(aantal, '\\');
            }
            bezig = true;
            i = j;
            continue;
        }
        if (c == '"') {
            if (in_q && i + 1 < n && t[i + 1] == '"') { cur += '"'; i += 2; continue; }
            in_q = !in_q;
            bezig = true;
        } else if ((c == ' ' || c == '\t') && !in_q) {
            if (bezig) { out.push_back(cur); cur.clear(); bezig = false; }
        } else {
            cur += c;
            bezig = true;
        }
        ++i;
    }
    if (bezig) out.push_back(cur);
    return out;
}

const char* verdict_name(Verdict v) { return v == Verdict::Ok ? "ok" : v == Verdict::Fallback ? "fallback" : "refused"; }

DirectPaths default_direct_paths(const EnvMap& env) {
    DirectPaths p;
    p.home = getenv_or(env, "HOME");
    if (p.home.empty())   // zonder HOME: de thuismap van deze gebruiker volgens het systeem
        if (const struct passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir) p.home = pw->pw_dir;
    const std::string xd = getenv_or(env, "XDG_DATA_HOME"), xs = getenv_or(env, "XDG_STATE_HOME"), xc = getenv_or(env, "XDG_CONFIG_HOME");
    p.data = (starts_with(xd, "/") ? xd : p.home + "/.local/share") + "/ravengun";
    p.state = (xs.empty() ? p.home + "/.local/state" : xs) + "/ravengun";
    p.tp = getenv_or(env, "RAVENGUN_TP_DIR");
    if (p.tp.empty()) p.tp = p.home + "/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64";
    p.library_file = getenv_or(env, "RAVENGUN_LIBRARY_FILE");
    if (p.library_file.empty()) p.library_file = (starts_with(xc, "/") ? xc : p.home + "/.config") + "/ravengun/library.json";
    return p;
}

// -----------------------------------------------------------------------------------------------------------------------------
// hid-board: joystick-id 0 (tools/dev/wine-joyid.sh)

std::vector<JoyId> parse_joy_ids(const std::string& output) {
    std::vector<JoyId> out;
    static const std::regex rx(R"(Joystick Id.*REG_BINARY[ \t\r\n\f\v]+([0-9A-Fa-f]{2}))");
    std::istringstream is(output);
    std::string line, key;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (starts_with(line, "HKEY_CURRENT_USER") && line.find("Calibration") != std::string::npos) {
            const size_t at = line.rfind("DirectInput\\");
            key = at == std::string::npos ? line : line.substr(at + 12);
        }
        std::smatch m;
        if (!key.empty() && std::regex_search(line, m, rx)) {
            const int id = std::stoi(m[1].str(), nullptr, 16);
            bool done = false;
            for (auto& j : out) if (j.key == key) { j.id = id; done = true; }
            if (!done) out.push_back({key, id});
            key.clear();
        }
    }
    return out;
}

std::vector<JoyId> joy_id_changes(const std::vector<JoyId>& ids_in, const std::string& vidpid) {
    std::vector<JoyId> ids = ids_in, out;
    std::sort(ids.begin(), ids.end(), [](const JoyId& a, const JoyId& b) { return a.key < b.key; });
    const size_t colon = vidpid.find(':');
    const std::string ours = "VID_" + upper(vidpid.substr(0, colon)) + "&PID_" + upper(colon == std::string::npos ? vidpid : vidpid.substr(colon + 1));
    std::set<int> used;
    for (const auto& j : ids) if (!starts_with(j.key, ours)) used.insert(j.id);
    for (const auto& j : ids)
        if (!starts_with(j.key, ours) && j.id == 0) {
            int n = 1;
            while (used.count(n)) ++n;
            used.insert(n);
            out.push_back({j.key, n});
        }
    bool have = false;
    for (const auto& j : ids)
        if (starts_with(j.key, ours)) {
            have = true;
            if (j.id != 0) out.push_back({j.key, 0});
        }
    if (!have) out.push_back({ours + "\\Calibration\\0", 0});
    return out;
}

int run_capture(const std::vector<std::string>& argv, const EnvMap& env, std::string* out) {
    if (argv.empty()) return -1;
    int pfd[2];
    if (::pipe2(pfd, O_CLOEXEC) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::vector<std::string> envs;
    for (const auto& [k, v] : env) envs.push_back(k + "=" + v);
    std::vector<char*> av, ev;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    for (auto& s : envs) ev.push_back(s.data());
    ev.push_back(nullptr);
    // PATH van de daemon om het programma te vinden (posix_spawnp zoekt in de PATH van de aanroeper).
    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), ev.data());
    posix_spawn_file_actions_destroy(&fa);
    ::close(pfd[1]);
    if (rc != 0) { ::close(pfd[0]); return -1; }
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pfd[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
        if (n > 0 && out && out->size() < 4 * 1024 * 1024) out->append(buf, static_cast<size_t>(n));
    ::close(pfd[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void wine_joystick_zero(const std::string& wine, const std::string& prefix, const std::string& vidpid, const EnvMap& base_env, const CaptureRunner& run,
                        std::vector<std::string>& log) {
    if (!isdir(prefix)) { log.push_back("joystick-id: prefix ontbreekt: " + prefix); return; }
    EnvMap env = base_env;
    env["WINEPREFIX"] = prefix;
    env["WINEDEBUG"] = "-all";
    const std::string base = "HKCU\\System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput";
    std::string out;
    run({wine, "reg", "query", base, "/s"}, env, &out);
    const auto changes = joy_id_changes(parse_joy_ids(out), vidpid);
    for (const auto& c : changes) {
        char d[16];
        std::snprintf(d, sizeof d, "%02x000000", c.id);
        run({wine, "reg", "add", base + "\\" + c.key, "/v", "Joystick Id", "/t", "REG_BINARY", "/d", d, "/f"}, env, nullptr);
        log.push_back("joystick-id van " + c.key + ": " + std::to_string(c.id));
    }
    if (!changes.empty()) run({pjoin(dirname(wine), "wineserver"), "-w"}, env, nullptr);
}

}  // namespace ravengun::launch
