#include "ravengun/games/games.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/launch/gamefind.hpp"

extern char** environ;

namespace ravengun::games {

namespace fs = std::filesystem;

namespace {

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string read_head(const std::string& path, size_t max_bytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string s(max_bytes, '\0');
    f.read(s.data(), static_cast<std::streamsize>(max_bytes));
    s.resize(static_cast<size_t>(f.gcount()));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '\0')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string xml_unescape(const std::string& s) {
    static const std::pair<const char*, const char*> ent[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool done = false;
        if (s[i] == '&')
            for (const auto& e : ent) {
                size_t n = std::strlen(e.first);
                if (s.compare(i, n, e.first) == 0) { out += e.second; i += n; done = true; break; }
            }
        if (!done) out += s[i++];
    }
    return out;
}

// Tekst tussen <tag> en </tag>; leeg als de tag ontbreekt of leeg is.
std::string xml_tag(const std::string& xml, const std::string& tag) {
    size_t a = xml.find("<" + tag + ">");
    if (a == std::string::npos) return {};
    a += tag.size() + 2;
    size_t b = xml.find("</" + tag + ">", a);
    if (b == std::string::npos) return {};
    std::string v = xml_unescape(trim(xml.substr(a, b - a)));
    std::string clean;
    for (unsigned char c : v) if (c >= 0x20 && c != 0x7f) clean += static_cast<char>(c);
    if (clean.size() > 80) clean.resize(80);
    return clean;
}

// "JurassicPark" -> "Jurassic Park", "tomb_raider" -> "tomb raider".
std::string humanize(const std::string& id) {
    std::string out;
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        if (c == '_' || c == '-') { out += ' '; continue; }
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(id[i - 1]))) out += ' ';
        out += c;
    }
    return out;
}

std::string icon_content_type(const std::string& file) {
    std::string ext = file.substr(file.rfind('.') + 1);
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    return "image/webp";
}

std::string last_line(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamoff size = f.tellg();
    std::streamoff from = size > 4096 ? size - 4096 : 0;
    f.seekg(from);
    std::string tail(static_cast<size_t>(size - from), '\0');
    f.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    tail.resize(static_cast<size_t>(f.gcount()));
    std::istringstream is(tail);
    std::string line, last;
    while (std::getline(is, line)) {
        line = trim(line);
        if (!line.empty()) last = line;
    }
    std::string clean;
    for (unsigned char c : last) if (c >= 0x20 && c != 0x7f) clean += static_cast<char>(c);
    if (clean.size() > 300) clean.resize(300);
    return clean;
}

bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

constexpr const char* kSuccessMarker = "lightgun klaar.";  // laatste regel van scripts/play-halo.sh bij succes

}  // namespace

bool valid_profile_name(std::string_view s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) if (!ident_char(c)) return false;
    return true;
}

bool valid_icon_file(std::string_view s) {
    size_t dot = s.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return false;
    if (!valid_profile_name(s.substr(0, dot))) return false;
    std::string_view ext = s.substr(dot + 1);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp";
}

std::string clip_note(const std::string& note) {
    size_t chars = 0, cut = 0;   // cut = de byte waar het laatste teken dat nog past begint; daar komt de "…"
    for (size_t i = 0; i < note.size(); ++i) {
        if ((static_cast<unsigned char>(note[i]) & 0xC0) == 0x80) continue;   // vervolgbyte van een UTF-8-teken
        if (chars == kMaxNoteChars - 1) cut = i;
        ++chars;
    }
    if (chars <= kMaxNoteChars) return note;
    std::string out = note.substr(0, cut);
    const size_t space = out.find_last_of(' ');
    if (space != std::string::npos && out.size() - space <= 24) out.resize(space);   // liever een heel woord minder dan een half woord
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "\xE2\x80\xA6";   // U+2026
}

std::vector<std::pair<std::string, std::string>> load_notes(const ravengun::gamesettings::Dirs& dirs) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& name : ravengun::gamesettings::list_games(dirs)) {
        if (!valid_profile_name(name)) continue;
        const std::string note = ravengun::gamesettings::load_game(dirs, name).get("Status", "Note");
        if (!note.empty()) out.emplace_back(name, clip_note(note));
    }
    return out;
}

std::vector<std::string> library_folders(const std::string& library_file, const std::string& home) {
    std::vector<std::string> out;
    if (library_file.empty()) return out;
    std::ifstream f(library_file, std::ios::binary);
    if (!f) return out;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto r = ravengun::json::parse(text);
    if (!r.ok || !r.value.is_object()) return out;
    const auto* folders = r.value.find("folders");
    if (!folders || !folders->is_array()) return out;
    for (const auto& v : folders->arr) {
        std::string p = v.is_string() ? v.str : (v.is_object() && v.find("path") && v.find("path")->is_string() ? v.find("path")->str : std::string());
        if (p.empty()) continue;
        if (!home.empty() && (p == "~" || p.rfind("~/", 0) == 0)) p = home + p.substr(1);
        out.push_back(p);
    }
    return out;
}

namespace {

// Games met een eigen profiel in game.ini die op deze machine staan (view of gamemap).
bool own_profile_installed(const Config& c, const std::string& profile, const std::vector<std::string>& folders, ravengun::gamesettings::Game* out) {
    if (c.dirs.shipped.empty() && c.dirs.user.empty()) return false;
    ravengun::gamesettings::Game g = ravengun::gamesettings::load_game(c.dirs, profile);
    if (!ravengun::gamesettings::has_own_profile(g.settings)) return false;
    // Zonder hash (alleen stat-aanroepen), met library.json als hint: de lijst doorzoekt de NAS niet voor een game die de app al kent.
    const std::vector<std::string> hints =
        ravengun::launch::library_roots(c.library_file, profile, ravengun::gamesettings::identify_of(g.settings).path, c.home);
    if (ravengun::gamesettings::locate_game(profile, g.settings, folders, c.views_dir, c.home, nullptr, {}, hints).empty()) return false;
    if (out) *out = std::move(g);
    return true;
}

}  // namespace

std::vector<Game> own_profile_games(const Config& c) {
    std::vector<Game> out;
    if (c.library_file.empty() && c.views_dir.empty()) return out;
    const std::vector<std::string> folders = library_folders(c.library_file, c.home);
    for (const std::string& name : ravengun::gamesettings::list_games(c.dirs)) {
        if (!valid_profile_name(name)) continue;
        std::error_code e2;
        if (!c.tp_dir.empty() && fs::is_regular_file(fs::path(c.tp_dir) / "UserProfiles" / (name + ".xml"), e2)) continue;   // staat al in de lijst
        ravengun::gamesettings::Game own;
        if (!own_profile_installed(c, name, folders, &own)) continue;
        Game g;
        g.profile = name;
        g.name = own.get("Game", "Name");
        if (g.name.empty()) g.name = humanize(name);
        std::error_code e3;
        if (fs::is_regular_file(fs::path(c.tp_dir) / "Icons" / (name + ".png"), e3) && !e3) g.icon_file = name + ".png";
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<Game> list_games(const Config& c, const std::vector<Game>* own_games) {
    std::vector<Game> out;
    if (c.tp_dir.empty()) return out;
    std::error_code ec;
    fs::path profiles = fs::path(c.tp_dir) / "UserProfiles";
    auto notes = load_notes(c.dirs);
    for (fs::directory_iterator it(profiles, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p.extension() != ".xml") continue;
        std::string stem = p.stem().string();
        if (!valid_profile_name(stem)) continue;
        std::error_code e2;
        if (!fs::is_regular_file(p, e2) || e2) continue;
        auto sz = fs::file_size(p, e2);
        if (e2 || sz > 4u * 1024 * 1024) continue;
        std::string head = read_head(p.string(), 64 * 1024);
        Game g;
        g.profile = stem;
        g.name = xml_tag(head, "GameNameInternal");
        if (g.name.empty()) g.name = humanize(stem);
        std::string icon = xml_tag(head, "IconName");
        size_t slash = icon.find_last_of("/\\");
        if (slash != std::string::npos) icon = icon.substr(slash + 1);
        if (valid_icon_file(icon)) {
            std::error_code e3;
            if (fs::is_regular_file(fs::path(c.tp_dir) / "Icons" / icon, e3) && !e3) g.icon_file = icon;
        }
        for (const auto& n : notes) if (n.first == stem) g.note = n.second;
        out.push_back(std::move(g));
    }
    // Games met een eigen profiel in game.ini die niet ook als UserProfiles-XML bestaan (dat zoekt in de gamemappen: de NAS).
    const std::vector<Game> own = own_games ? std::vector<Game>() : own_profile_games(c);
    for (Game g : own_games ? *own_games : own) {
        bool have = false;
        for (const Game& x : out) have = have || x.profile == g.profile;
        if (have) continue;
        for (const auto& n : notes) if (n.first == g.profile) g.note = n.second;
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const Game& a, const Game& b) {
        std::string x = a.name, y = b.name;
        for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (auto& ch : y) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return x != y ? x < y : a.profile < b.profile;
    });
    return out;
}

bool profile_exists(const Config& c, const std::string& profile) {
    if (!valid_profile_name(profile)) return false;
    std::error_code ec;
    if (!c.tp_dir.empty()) {
        fs::path p = fs::path(c.tp_dir) / "UserProfiles" / (profile + ".xml");
        if (fs::is_regular_file(p, ec) && !ec) return true;
    }
    // Een eigen profiel in game.ini: de XML is dan niet nodig (het startscript haalt het pad uit de gamemappen).
    if (c.dirs.shipped.empty() && c.dirs.user.empty()) return false;
    return ravengun::gamesettings::has_own_profile(ravengun::gamesettings::load_game(c.dirs, profile).settings);
}

std::string read_icon(const Config& c, const std::string& file, std::string& content_type, std::string& data) {
    if (!valid_icon_file(file) || c.tp_dir.empty()) return "invalid";
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(c.tp_dir) / "Icons", ec);
    if (ec) return "not_found";
    fs::path full = fs::weakly_canonical(dir / file, ec);
    if (ec) return "not_found";
    // Het opgeloste pad (symlinks gevolgd) moet rechtstreeks in de Icons-map liggen.
    if (full.parent_path() != dir) return "invalid";
    struct stat st{};
    if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return "not_found";
    if (static_cast<size_t>(st.st_size) > kMaxIconBytes) return "invalid";
    std::ifstream f(full, std::ios::binary);
    if (!f) return "not_found";
    std::ostringstream ss;
    ss << f.rdbuf();
    data = ss.str();
    content_type = icon_content_type(file);
    return {};
}

bool game_process_running(const std::string& proc_root) {
    std::error_code ec;
    for (fs::directory_iterator it(proc_root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) continue;
        std::ifstream f(it->path() / "comm");
        std::string comm;
        if (f && std::getline(f, comm) && (comm == "BudgieLoader_x6" || comm == "BudgieLoader.ex")) return true;
    }
    return false;
}

bool unit_alive(const std::string& cgroup_procs) {
    if (cgroup_procs.empty()) return false;
    std::ifstream f(cgroup_procs);
    std::string line;
    while (f && std::getline(f, line)) if (!trim(line).empty()) return true;
    return false;
}

std::string default_tpui_procs(unsigned uid) {
    const std::string u = std::to_string(uid);
    return "/sys/fs/cgroup/user.slice/user-" + u + ".slice/user@" + u + ".service/app.slice/ravengun-tpui.service/cgroup.procs";
}

int script_state(const std::string& proc_root, const std::string& script) {
    int found = 0;
    std::error_code ec;
    for (fs::directory_iterator it(proc_root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) continue;
        std::string raw = read_head((it->path() / "cmdline").string(), 8192);
        if (raw.empty()) continue;
        std::vector<std::string> args;
        std::string cur;
        for (char ch : raw) { if (ch == '\0') { args.push_back(cur); cur.clear(); } else cur += ch; }
        if (!cur.empty()) args.push_back(cur);
        size_t at = args.size();
        // Het script zelf, of een shell die het uitvoert (geen editor of ander programma met het pad als argument).
        std::string prog = args.empty() ? std::string() : args[0].substr(args[0].rfind('/') == std::string::npos ? 0 : args[0].rfind('/') + 1);
        if (!args.empty() && args[0] == script) at = 0;
        else if (args.size() > 1 && args[1] == script && (prog == "bash" || prog == "sh" || prog == "dash" || prog == "env")) at = 1;
        if (at == args.size()) continue;
        bool stop = false;
        for (size_t i = at + 1; i < args.size(); ++i) if (args[i] == "--stop") stop = true;
        if (stop) return 2;
        found = 1;
    }
    return found;
}

bool valid_launch(std::string_view s) { return s.empty() || s == "auto" || s == "direct" || s == "teknoparrot-ui"; }

std::vector<std::string> start_argv(const Config& c, const std::string& profile, const std::string& launch) {
    std::vector<std::string> a{c.systemd_run, "--user", std::string("--unit=") + kPlayUnit, "--collect", "--quiet",
                               "--description=RavenGun: game starten"};
    bool plain = !c.log_path.empty();
    for (unsigned char ch : c.log_path) if (ch <= 0x20 || ch == 0x7f || ch == '"' || ch == '\'' || ch == '\\') plain = false;
    if (plain) a.push_back("--property=StandardOutput=file:" + c.log_path);
    a.push_back("--");
    a.push_back(c.play_script);
    a.push_back("--profile");
    a.push_back(profile);
    if (launch == "direct") a.push_back("--direct");
    else if (launch == "teknoparrot-ui") a.push_back("--tpui");
    return a;
}

std::vector<std::string> stop_unit_argv(const Config& c) {
    return {c.systemctl, "--user", "stop", std::string(kPlayUnit) + ".service"};
}

std::vector<std::string> stop_script_argv(const Config& c) {
    return {c.systemd_run, "--user", std::string("--unit=") + kStopUnit, "--collect", "--quiet",
            "--description=RavenGun: game stoppen", "--", c.play_script, "--stop"};
}

namespace {
std::string read_text(const std::string& path, size_t max_bytes = 1024 * 1024) {
    std::ifstream f(path, std::ios::binary);
    std::string s;
    if (!f) return s;
    std::ostringstream ss;
    ss << f.rdbuf();
    s = ss.str();
    if (s.size() > max_bytes) s.clear();
    return s;
}
// Draait er in de cgroup van de unit een proces met deze naam (/proc/<pid>/comm)?
bool same_name(const std::string& a, const std::string& b, bool icase) {
    if (!icase) return a == b;
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
// Draait er in de unit een proces met deze naam? icase: hoofdletters tellen niet (Wine: "Game.exe" of "game.exe").
bool unit_has_comm(const std::string& cgroup_procs, const std::string& proc_root, const std::string& comm, bool icase = false) {
    if (cgroup_procs.empty() || comm.empty()) return false;
    std::ifstream f(cgroup_procs);
    std::string pid;
    while (f && std::getline(f, pid)) {
        pid = trim(pid);
        if (pid.empty()) continue;
        std::ifstream c(proc_root + "/" + pid + "/comm");
        std::string name;
        if (c && std::getline(c, name) && same_name(name, comm, icase)) return true;
    }
    return false;
}
// Idem voor een lijst namen in kleine letters (de procesnamen van een directe start, zoals launch-direct.py ze vergelijkt).
bool unit_has_any_comm(const std::string& cgroup_procs, const std::string& proc_root, const std::vector<std::string>& lower_comms) {
    if (cgroup_procs.empty() || lower_comms.empty()) return false;
    std::ifstream f(cgroup_procs);
    std::string pid;
    while (f && std::getline(f, pid)) {
        pid = trim(pid);
        if (pid.empty()) continue;
        std::ifstream c(proc_root + "/" + pid + "/comm");
        std::string name;
        if (!(c && std::getline(c, name))) continue;
        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (std::find(lower_comms.begin(), lower_comms.end(), name) != lower_comms.end()) return true;
    }
    return false;
}
std::string stamp(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[64];
    std::strftime(b, sizeof b, fmt, &tm);
    return b;
}
}  // namespace

bool native_route(const Config& c, const std::string& profile) {
    if (c.native.json.empty()) return false;
    const std::string text = read_text(c.native.json);
    if (text.empty() || !ravengun::launch::profile_enabled(text, profile)) return false;
    // Een game die de camera IN het spel gebruikt, gaat nog via het script (dat kiest het apparaat met scripts/camera.py).
    const ravengun::gamesettings::Game g = ravengun::gamesettings::load_game(c.dirs, profile);
    const std::string* cam = g.settings.find("Camera", "Use");
    return !(cam && trim(*cam) == "game");
}

std::vector<std::string> native_unit_argv(const Config& c, const std::vector<std::string>& game_argv, const std::string& log) {
    std::vector<std::string> a{c.systemd_run, "--user", "--unit=" + c.tpui_unit, "--collect", "--quiet",
                               "--description=RavenGun: game (native, gestart door de daemon)", "--property=TimeoutStopSec=10"};
    bool plain = !log.empty();
    for (unsigned char ch : log) if (ch <= 0x20 || ch == 0x7f || ch == '"' || ch == '\'' || ch == '\\') plain = false;
    if (plain) {
        a.push_back("--property=StandardOutput=append:" + log);
        a.push_back("--property=StandardError=append:" + log);
    }
    a.push_back("--");
    a.insert(a.end(), game_argv.begin(), game_argv.end());
    return a;
}

bool unity_route(const Config& c, const std::string& profile, const std::string& launch) {
    if (c.unity.json.empty() || launch == "teknoparrot-ui") return false;
    const std::string text = read_text(c.unity.json);
    if (text.empty() || !ravengun::launch::unity_game_known(text, profile)) return false;
    // Zoals play-halo.sh: een game die in game.ini om tp-shm vraagt gaat niet over de Unity-route.
    const ravengun::gamesettings::Game g = ravengun::gamesettings::load_game(c.dirs, profile);
    const std::string* route = g.settings.find("Input", "Route");
    return !(route && trim(*route) == "tp-shm");
}

std::vector<std::string> unity_unit_argv(const Config& c, const ravengun::launch::UnityPlan& plan, const std::string& log) {
    std::vector<std::string> a{c.systemd_run, "--user", "--unit=" + c.tpui_unit, "--collect", "--quiet",
                               "--description=RavenGun: game (Unity, gestart door de daemon)", "--property=TimeoutStopSec=10",
                               "--working-directory=" + plan.cwd};
    for (const auto& [k, v] : plan.env) a.push_back("--setenv=" + k + "=" + v);
    bool plain = !log.empty();
    for (unsigned char ch : log) if (ch <= 0x20 || ch == 0x7f || ch == '"' || ch == '\'' || ch == '\\') plain = false;
    if (plain) {
        a.push_back("--property=StandardOutput=append:" + log);
        a.push_back("--property=StandardError=append:" + log);
    }
    a.push_back("--");
    a.insert(a.end(), plan.argv.begin(), plan.argv.end());
    return a;
}

ravengun::launch::Verdict direct_route(const Config& c, const std::string& profile, const std::string& launch, std::string& why,
                                       ravengun::launch::DirectInput* input) {
    namespace L = ravengun::launch;
    if (c.direct.tp.empty() || c.direct_runner.empty() || ::access(c.direct_runner.c_str(), X_OK) != 0) {
        why = "rg-direct ontbreekt (" + c.direct_runner + ")";
        return L::Verdict::Fallback;
    }
    if (launch == "teknoparrot-ui") { why = "TeknoParrotUi gevraagd"; return L::Verdict::Fallback; }
    const std::string mode = launch == "direct" ? std::string("direct") : L::launch_mode(profile, c.direct);
    if (mode != "direct") { why = "[Launch] Mode is niet direct"; return L::Verdict::Fallback; }
    L::DirectInput in;
    L::Verdict v = L::direct_input(profile, c.direct, in, why);
    if (v != L::Verdict::Ok) return v;
    // play-halo.sh: een game uit aagames-games.json met een andere route dan tp-shm gaat via de Unity-route (play-unity.sh).
    if (c.unity_game && c.unity_game(profile) && (in.route != "tp-shm" || !in.note.empty())) { why = "Unity-route (aagames-games.json)"; return L::Verdict::Fallback; }
    L::DirectOptions o;
    o.base_env = c.session_env ? c.session_env() : direct_session_env(c.proc_root);
    if (in.route == "jvs") o.base_env["RG_JVS_PTY"] = c.jvs_link;
    if (in.writes_tp_shm()) o.shm_source = ravengun::iobridge::kDefaultTpShmPath;
    o.make_view = true;   // dit is de start: een game op de NAS krijgt hier zijn view ([Launch] View; zoals launch-direct.py --check)
    L::DirectPlan pl;
    v = L::plan_direct(profile, c.direct, o, pl, why);
    if (v == L::Verdict::Ok && L::needs_gst_fetch(pl)) {
        why = "nog niet in de engine: de GStreamer-plug-ins ophalen (scripts/fetch-gst-wmv.sh)";
        return L::Verdict::Fallback;
    }
    if (input) *input = in;
    return v;
}

ravengun::launch::EnvMap direct_session_env(const std::string& proc_root) {
    ravengun::launch::EnvMap e = ravengun::launch::environ_map();
    // De variabelen van de unit van de daemon zelf horen niet bij de game (systemd zet in de game-unit zijn eigen).
    for (const char* k : {"INVOCATION_ID", "JOURNAL_STREAM", "SYSTEMD_EXEC_PID", "MANAGERPID", "NOTIFY_SOCKET", "LISTEN_PID", "LISTEN_FDS",
                          "LISTEN_FDNAMES", "WATCHDOG_PID", "WATCHDOG_USEC"})
        e.erase(k);
    auto dflt = [&](const char* k, const std::string& v) { if (e[k].empty()) e[k] = v; };
    dflt("XDG_RUNTIME_DIR", "/run/user/" + std::to_string(::getuid()));
    dflt("WAYLAND_DISPLAY", "wayland-0");
    dflt("DISPLAY", ":0");
    dflt("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + e["XDG_RUNTIME_DIR"] + "/bus");
    if (e["XAUTHORITY"].empty()) {
        e.erase("XAUTHORITY");
        // Zoals play-halo.sh: het xauth-bestand van Xwayland uit de opdrachtregel van kwin_wayland (pgrep -x kwin_wayland | head -1).
        long best = -1;
        std::string auth;
        std::error_code ec;
        for (fs::directory_iterator it(proc_root, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.empty() || !std::all_of(name.begin(), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) continue;
            std::ifstream cf(it->path() / "comm");
            std::string comm;
            if (!(cf && std::getline(cf, comm)) || comm != "kwin_wayland") continue;
            const long pid = std::stol(name);
            if (best >= 0 && pid > best) continue;
            const std::string raw = read_head((it->path() / "cmdline").string(), 65536);
            std::vector<std::string> args;
            std::string cur;
            for (char ch : raw) { if (ch == '\0') { args.push_back(cur); cur.clear(); } else cur += ch; }
            std::string found;
            for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--xwayland-xauthority") found = args[i + 1];
            best = pid;
            auth = found;
        }
        if (!auth.empty()) e["XAUTHORITY"] = auth;
    }
    return e;
}

std::vector<std::string> direct_unit_argv(const Config& c, const std::string& plan_file, const std::string& log) {
    std::vector<std::string> a{c.systemd_run, "--user", "--unit=" + c.tpui_unit, "--collect", "--quiet",
                               "--description=RavenGun: game (direct, gestart door de daemon)", "--property=TimeoutStopSec=10",
                               "--working-directory=" + c.direct.tp};
    bool plain = !log.empty();
    for (unsigned char ch : log) if (ch <= 0x20 || ch == 0x7f || ch == '"' || ch == '\'' || ch == '\\') plain = false;
    if (plain) {
        a.push_back("--property=StandardOutput=append:" + log);
        a.push_back("--property=StandardError=append:" + log);
    }
    a.insert(a.end(), {"--", c.direct_runner, "--run", plan_file});
    return a;
}

int run_process(const std::vector<std::string>& argv, std::string& err) {
    if (argv.empty()) { err = "geen programma"; return -1; }
    std::vector<char*> av;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = ::posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) { err = argv[0] + ": " + std::strerror(rc); return -1; }
    for (int i = 0; i < 500; ++i) {  // hoogstens 10 s
        int status = 0;
        pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (w < 0) { err = "waitpid mislukt"; return -1; }
        ::usleep(20000);
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
    err = argv[0] + " reageerde niet binnen 10 s";
    return -1;
}

GameService::GameService(Config c, Spawner s) : c_(std::move(c)), spawn_(std::move(s)) {}

GameService::~GameService() {
    join_worker();
    wait_own_games();
}

void GameService::wait_own_games() {
    std::thread t;
    {
        std::lock_guard<std::mutex> g(own_m_);
        t = std::move(own_thread_);
    }
    if (t.joinable()) t.join();
}

void GameService::join_worker() {
    if (worker_.joinable()) worker_.join();
}

Result GameService::start(const std::string& profile, const std::string& launch) {
    // Alleen goedkope controles in het IPC-verzoek (geen NAS): de route kiezen en plannen gebeurt in de worker, zodat de daemon tijdens
    // een start op alles blijft antwoorden. Een weigering of fout eindigt daar in de toestand "failed" met de melding.
    if (!valid_profile_name(profile)) return {false, "invalid_argument", "ongeldige profielnaam (alleen letters, cijfers, _ en -)"};
    if (!valid_launch(launch)) return {false, "invalid_argument", "ongeldige startwijze '" + launch + "'"};
    if (!profile_exists(c_, profile)) return {false, "invalid_argument", "onbekend profiel '" + profile + "': er is geen eigen profiel in patches/" + profile + "/game.ini en geen bestand " + profile + ".xml in UserProfiles"};
    std::error_code ec;
    // Toestand nu bepalen: er mag niets lopen.
    const bool game_run = game_process_running(c_.proc_root) || unit_alive(c_.tpui_procs);
    const int sc = script_state(c_.proc_root, c_.play_script);
    std::lock_guard<std::mutex> g(m_);
    if (busy_.load() || sc != 0 || (launched_ && now_ms() - launched_ms_ < c_.start_grace_ms))
        return {false, "unavailable", "er wordt al een game gestart of gestopt; wacht even of druk op Stoppen"};
    if (game_run) return {false, "unavailable", "er draait al een game; stop die eerst"};
    join_worker();
    if (!c_.log_path.empty()) {
        fs::create_directories(fs::path(c_.log_path).parent_path(), ec);
        std::ofstream trunc(c_.log_path, std::ios::trunc);  // oude melding niet voor een nieuwe aanzien
    }
    profile_ = profile;
    launched_ = false;
    failed_ = false;
    failed_msg_.clear();
    failed_profile_.clear();
    busy_.store(true);
    busy_stop_ = false;
    native_ = true;   // tijdens het plannen is het een eigen start: stoppen breekt hem af (cancel_)
    cancel_.store(false);
    worker_ = std::thread([this, profile, launch] { run_start(profile, launch); });
    return {};
}

// De worker van een start: de route kiezen (native, Unity, direct of het script) en die uitvoeren.
void GameService::run_start(const std::string& profile, const std::string& launch) {
    namespace L = ravengun::launch;
    const bool native = native_route(c_, profile);
    const bool unity = !native && unity_route(c_, profile, launch);
    // De directe route (A10): de engine zelf, of het script (ook als dat zelf naar TeknoParrotUi terugvalt). Wat launch-direct.py
    // --check zou weigeren (exit 5), start niet: play-halo.sh stopt dan ook, zonder TeknoParrotUi.
    L::DirectInput din;
    std::string dwhy;
    const L::Verdict dv = native || unity ? L::Verdict::Fallback : direct_route(c_, profile, launch, dwhy, &din);
    const bool direct = dv == L::Verdict::Ok;
    std::string msg;
    bool own = false;   // een eigen route liep (die kan invoer of een unit achterlaten; stoppen ruimt dat dan op)
    if (dv == L::Verdict::Refused) msg = profile + ": " + dwhy;
    else if (cancel_.load()) msg = "de start is afgebroken";
    else if (native || unity || direct) {
        own = true;
        msg = unity ? run_unity(profile) : direct ? run_direct(profile, din) : run_native(profile);
    }
    else if (c_.play_script.empty() || ::access(c_.play_script.c_str(), X_OK) != 0) msg = "het startscript ontbreekt of is niet uitvoerbaar";
    else {
        // Waarom de eigen route niet liep, in het startlogboek: anders is achteraf niet te zien dat het script (en daarmee
        // TeknoParrotUi) het overnam (2026-10-15: Otomedius en Infinity Blade FX zonder [Launch] Mode = direct).
        if (!c_.log_path.empty() && !dwhy.empty()) {
            std::ofstream lg(c_.log_path, std::ios::app);
            lg << "route: geen eigen route voor " << profile << " (" << dwhy << "); het startscript neemt over\n";
        }
        // Het script: vanaf hier stopt stoppen het script (zoals altijd), niet meer de eigen start.
        {
            std::lock_guard<std::mutex> lk(m_);
            if (cancel_.load()) msg = "de start is afgebroken";
            else native_ = false;
        }
        if (msg.empty()) {
            std::string err;
            const int rc = spawn_(start_argv(c_, profile, launch), err);
            std::lock_guard<std::mutex> lk(m_);
            if (rc == 0) {
                launched_ = true;
                launched_ms_ = now_ms();
            } else {
                failed_ = true;
                failed_profile_ = profile;
                failed_msg_ = rc > 0 ? "het starten via systemd-run mislukte (code " + std::to_string(rc) + "); draait er al een start?"
                                     : "het starten mislukte: " + err;
            }
            busy_.store(false);
            return;
        }
    }
    std::lock_guard<std::mutex> lk(m_);
    if (!msg.empty()) {
        failed_ = true;
        failed_profile_ = profile;
        failed_msg_ = msg;
        if (!own) native_ = false;   // er is niets van de daemon zelf op te ruimen
    }
    waiting_.store(false);
    busy_.store(false);
}

Result GameService::stop() {
    const bool game_run = game_process_running(c_.proc_root) || unit_alive(c_.tpui_procs);
    const int sc = script_state(c_.proc_root, c_.play_script);
    std::lock_guard<std::mutex> g(m_);
    if (native_ && busy_.load() && !busy_stop_) {   // een native start die nog wacht of loopt: afbreken
        cancel_.store(true);
        return {true, "", "de start wordt afgebroken"};
    }
    if (busy_.load()) return {false, "unavailable", "er wordt al een game gestart of gestopt; wacht even"};
    if (!game_run && sc == 0 && !launched_ && !native_) return {true, "", "er draait geen game"};
    if (native_) {
        join_worker();
        launched_ = false;
        failed_ = false;
        busy_.store(true);
        busy_stop_ = true;
        std::vector<std::string> a1 = stop_unit_argv(c_);
        a1.back() = c_.tpui_unit + ".service";
        worker_ = std::thread([this, a1] {
            std::string err;
            spawn_(a1, err);                      // de game (gamescope, bwrap, het spel) in een keer
            if (c_.stop_inputs) c_.stop_inputs(); // IO-brug neutraal en dicht, uitvoer ontwapend
            std::lock_guard<std::mutex> lk(m_);
            native_ = false;
            busy_.store(false);
        });
        return {};
    }
    if (c_.play_script.empty() || ::access(c_.play_script.c_str(), X_OK) != 0)
        return {false, "unavailable", "het startscript ontbreekt of is niet uitvoerbaar"};
    join_worker();
    launched_ = false;
    failed_ = false;
    busy_.store(true);
    busy_stop_ = true;
    std::vector<std::string> a1 = stop_unit_argv(c_), a2 = stop_script_argv(c_);
    worker_ = std::thread([this, a1, a2] {
        std::string err;
        spawn_(a1, err);  // een start die nog op de Move wacht afbreken; geen unit is geen fout
        err.clear();
        int rc = spawn_(a2, err);
        std::lock_guard<std::mutex> lk(m_);
        if (rc != 0) {
            failed_ = true;
            failed_profile_.clear();
            failed_msg_ = "het stoppen mislukte: " + (rc > 0 ? "code " + std::to_string(rc) : err);
        }
        busy_.store(false);
    });
    return {};
}

// Een lightgun: een verbonden Move of een muis die als lightgun is toegewezen (zoals het script, maar zonder het script).
std::string GameService::wait_lightgun() {
    if (!c_.lightgun_ready || c_.lightgun_ready()) return {};
    waiting_.store(true);
    const int64_t until = now_ms() + c_.lightgun_wait_ms;
    while (!c_.lightgun_ready()) {
        if (cancel_.load()) { waiting_.store(false); return "de start is afgebroken"; }
        if (now_ms() > until) { waiting_.store(false); return "Geen Move gevonden binnen 2 minuten. Druk op PS en start opnieuw."; }
        ::usleep(200000);
    }
    waiting_.store(false);
    return {};
}

// [Display] AimAspect = 4:3 | 5:3 | 1024x768 | 1.333: het beeld van het spel op de tv. 0 = niet gezet of onleesbaar.
static double aim_aspect_of(const ravengun::gamesettings::Dirs& dirs, const std::string& profile) {
    const std::string v = ravengun::gamesettings::load_game(dirs, profile).get("Display", "AimAspect");
    if (v.empty()) return 0;
    double a = 0, b = 0;
    char sep = 0;
    if (std::sscanf(v.c_str(), " %lf %c %lf", &a, &sep, &b) == 3 && (sep == ':' || sep == 'x' || sep == 'X' || sep == '/') && a > 0 && b > 0)
        return a / b;
    if (std::sscanf(v.c_str(), " %lf", &a) == 1 && a > 0.2 && a < 5) return a;
    return 0;
}

// [Audio] Speakers uit game.ini ("STEREO" of "LPCM 5.1", zoals de opties van de app hem schrijven) als waarde voor de FMOD-wrapper:
// "stereo" of "5.1". Leeg = niets gekozen (de wrapper gebruikt dan zijn standaard, 5.1).
static std::string speakers_of(const ravengun::gamesettings::Dirs& dirs, const std::string& profile) {
    std::string v = ravengun::gamesettings::load_game(dirs, profile).get("Audio", "Speakers");
    for (char& ch : v) ch = (char)std::tolower((unsigned char)ch);
    if (v.find("stereo") != std::string::npos) return "stereo";
    if (v.find("5.1") != std::string::npos) return "5.1";
    if (v.find("7.1") != std::string::npos) return "7.1";
    return {};
}

std::string GameService::run_direct(const std::string& profile, const ravengun::launch::DirectInput& in) {
    namespace L = ravengun::launch;
    // 1. Een lightgun, dan wapenen; de virtuele lightgun en het toetsenbord moeten er zijn voordat de game zijn invoer opent.
    if (std::string e = wait_lightgun(); !e.empty()) return e;
    if (c_.arm) { const std::string e = c_.arm(); if (!e.empty()) return "uitvoer wapenen: " + e; }
    if (c_.set_aim_aspect) c_.set_aim_aspect(aim_aspect_of(c_.direct.dirs, profile));
    auto fail = [&](const std::string& m) { if (c_.stop_inputs) c_.stop_inputs(); return m; };
    if (!c_.input_by_id.empty())
        for (const char* dev : {"usb-RavenGun_Virtual_Lightgun-event-mouse", "usb-RavenGun_Virtual_Keyboard-event-kbd"}) {
            const std::string pth = c_.input_by_id + "/" + dev;
            for (int i = 0; i < 20 && ::access(pth.c_str(), F_OK) != 0; ++i) ::usleep(250000);
            if (::access(pth.c_str(), F_OK) != 0) return fail(std::string("Invoerapparaat ontbreekt: ") + dev);
        }
    // 2. De invoerroute, zoals play-halo.sh hem voor een directe start voorbereidt.
    std::vector<std::string> prep;
    if (!in.note.empty()) prep.push_back(in.note);
    L::EnvMap base = c_.session_env ? c_.session_env() : direct_session_env(c_.proc_root);
    std::string shm;
    if (c_.start_route) {
        const std::string e = c_.start_route(profile, in.route);
        if (!e.empty()) return fail("IO-brug (" + in.route + ") starten mislukt voor " + profile + ": " + e);
    }
    if (in.route == "tp-shm") {
        shm = ravengun::iobridge::kDefaultTpShmPath;
        prep.push_back("Invoerroute: tp-shm (de daemon schrijft de invoer).");
        if (!in.control_pipe.empty())
            prep.push_back("Plus besturingspipe " + in.control_pipe + " (de daemon schrijft het rapport, rg-direct stroomt het naar de pipe in de prefix).");
    } else if (in.route == "hid-board") {
        // Het bord krijgt joystick-id 0 in de prefix van het profiel (scripts/wine-joyid.sh), anders opent de game de Move zelf.
        const std::string pfx = L::profile_env(profile, c_.direct)["RG_WINEPREFIX"];
        const std::string vidpid = c_.hid_vidpid ? c_.hid_vidpid(in.board) : std::string();
        if (!pfx.empty() && !vidpid.empty()) {
            std::string wine = base.count("RG_WINE") ? base["RG_WINE"] : std::string();
            if (wine.empty()) wine = c_.direct.home + "/.local/share/ravengun/wine/proton-11.0-2/bin/wine";
            L::wine_joystick_zero(wine, pfx, vidpid, base, c_.capture, prep);
        }
        prep.push_back("Invoerroute: hid-board (de daemon is het invoerbord van de game).");
    } else if (in.route == "jvs") {
        struct stat st{};
        if (c_.jvs_link.empty() || ::lstat(c_.jvs_link.c_str(), &st) != 0 || !S_ISLNK(st.st_mode))
            return fail("De daemon maakte geen pty-symlink (" + c_.jvs_link + ").");
        base["RG_JVS_PTY"] = c_.jvs_link;
        prep.push_back("Invoerroute: jvs over de JVS-pipe (de daemon is het JVS-bord achter TeknoParrot_JVS).");
        if (in.writes_tp_shm()) {
            // Layout in [Input]: de daemon schrijft ook de tp-shm-indeling; de pipehelper spiegelt dat bestand naar de game.
            shm = ravengun::iobridge::kDefaultTpShmPath;
            prep.push_back("Plus tp-shm-indeling " + in.layout + " (de daemon schrijft het gedeelde geheugen, bv. het stuur).");
        }
    }
    if (cancel_.load()) return fail("de start is afgebroken");
    // 3. Het plan (opnieuw, nu met de omgeving van de route), de drop-ins en teknoparrot.ini in de view.
    L::DirectOptions o;
    o.base_env = base;
    o.shm_source = shm;
    o.make_view = true;
    L::DirectPlan pl;
    std::string why;
    if (L::plan_direct(profile, c_.direct, o, pl, why) != L::Verdict::Ok) return fail(profile + ": " + why);
    std::vector<std::string> placed;
    std::string err;
    if (!L::apply_direct(pl, c_.direct, placed, err)) return fail(profile + ": " + err);
    // 4. Het logboek en het plan voor rg-direct.
    std::string log, plan_file;
    {
        std::string dir = c_.native_log_dir;
        if (dir.empty()) dir = c_.direct.state + "/logs";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string st = stamp("%Y%m%d-%H%M%S");
        log = dir + "/direct-" + profile + "-" + st + ".log";
        plan_file = dir + "/direct-" + profile + "-" + st + ".plan.json";
        {
            std::ofstream pf(plan_file, std::ios::trunc);
            pf << L::plan_to_json(pl) << "\n";
            if (!pf) return fail("het plan " + plan_file + " is niet te schrijven");
        }
        ::chmod(plan_file.c_str(), 0600);   // de hele omgeving van de sessie staat erin
        std::ofstream f(log, std::ios::app);
        f << "== direct " << profile << " " << st << " (gestart door ravengund, zonder script)\n";
        for (const std::string& l : pl.notes) f << l << "\n";
        for (const std::string& l : prep) f << l << "\n";
        f << "plan: " << plan_file << "\n";
        f << "spel: " << pl.game << " (" << pl.game_source << ")\n";
        for (const std::string& l : placed) f << l << "\n";
        f << "cmd:";
        for (const std::string& a : direct_unit_argv(c_, plan_file, log)) f << " " << a;
        f << "\n";
    }
    if (cancel_.load()) return fail("de start is afgebroken");
    // 5. Starten in de unit van de game: rg-direct is het hoofdproces, op de plaats van launch-direct.py.
    std::string serr;
    const int rc = spawn_(direct_unit_argv(c_, plan_file, log), serr);
    if (rc != 0) return fail(rc > 0 ? "het starten via systemd-run mislukte (code " + std::to_string(rc) + ")" : "het starten mislukte: " + serr);
    // 6. Wachten tot de lader of de game draait (of de unit al weer weg is).
    if (c_.tpui_procs.empty()) return {};
    const int64_t until = now_ms() + c_.game_wait_ms;
    ::usleep(500000);
    // Bij twee exe's telt ook de tweede (bv. amdaemon.exe, die eerst start) als "draait".
    const std::vector<std::string> comms = L::plan_comms(pl);
    while (!unit_has_any_comm(c_.tpui_procs, c_.proc_root, comms)) {
        if (cancel_.load()) return "de start is afgebroken";
        if (!unit_alive(c_.tpui_procs)) {
            const std::string last = last_line(log);
            return fail(profile + " is niet gestart" + (last.empty() ? std::string() : ": " + last) + " (logboek: " + log + ")");
        }
        if (now_ms() > until) return profile + " is niet gestart; zie " + log;
        ::usleep(250000);
    }
    return {};
}

std::string GameService::run_native(const std::string& profile) {
    namespace L = ravengun::launch;
    // 1. Een lightgun.
    if (const std::string e = wait_lightgun(); !e.empty()) return e;
    // 2. Het profiel en het commando.
    const std::string text = read_text(c_.native.json);
    L::NativeProfile pr;
    L::NativePlan pl;
    std::string err;
    if (!L::load_profile(text, profile, c_.native, pr, err)) return err;
    L::PlanOptions po;
    if (const char* w = std::getenv("WAYLAND_DISPLAY"); w && *w) po.wayland_display = w;
    if (const char* d = std::getenv("DISPLAY"); d && *d) po.host_display = d;
    if (const char* x = std::getenv("XAUTHORITY"); x && *x) po.host_xauthority = x;
    // Geluid (FMOD-wrapper, tools/fmod-bridge): de keuze uit de opties van de game ([Audio] Speakers in game.ini, de gebruikerslaag gaat voor).
    if (const std::string spk = speakers_of(c_.direct.dirs, profile); !spk.empty()) po.env.emplace_back("RG_FMOD_SPEAKERS", spk);
    if (!L::plan(pr, c_.native, po, pl, err)) return err;
    // 3. Invoer: uitvoer wapenen, dan de route van het profiel (tp-shm: de daemon schrijft het bordbeeld; evdev: de brug dicht).
    if (c_.arm) { const std::string e = c_.arm(); if (!e.empty()) return "uitvoer wapenen: " + e; }
    if (c_.set_aim_aspect) c_.set_aim_aspect(aim_aspect_of(c_.direct.dirs, profile));
    if (c_.start_route) {
        const std::string e = c_.start_route(profile, pr.route.empty() ? std::string("evdev") : pr.route);
        if (!e.empty()) { if (c_.stop_inputs) c_.stop_inputs(); return "IO-brug: " + e; }
    }
    // Lindbergh met JVS-doorgifte: de pty bestaat pas nu (de route jvs maakte hem); het doel gaat als apparaat de sandbox in.
    if (pl.needs_jvs_pty && !L::resolve_jvs(pl, c_.jvs_link, err)) { if (c_.stop_inputs) c_.stop_inputs(); return err; }
    if (cancel_.load()) { if (c_.stop_inputs) c_.stop_inputs(); return "de start is afgebroken"; }
    // 4. De schrijflaag en het logboek.
    std::vector<std::string> layer_log;
    if (!L::apply_layer(pl, layer_log, err)) { if (c_.stop_inputs) c_.stop_inputs(); return err; }
    std::string log;
    if (!c_.native_log_dir.empty()) {
        std::error_code ec;
        fs::create_directories(c_.native_log_dir, ec);
        log = c_.native_log_dir + "/native-" + profile + "-" + stamp("%Y%m%d-%H%M%S") + ".log";
        std::ofstream f(log, std::ios::app);
        f << "== native " << profile << " " << stamp("%Y%m%d-%H%M%S") << " (gestart door ravengund, zonder script)\n";
        for (const std::string& l : L::identity_lines(pl)) f << l << "\n";
        for (const std::string& l : pl.notes) f << l << "\n";
        f << "invoerroute: " << (pr.route.empty() ? "evdev" : pr.route) << "; res " << pl.res << "; proces " << pl.comm << "\n";
        for (const std::string& l : layer_log) f << l << "\n";
        f << "cmd:";
        for (const std::string& a : pl.argv) f << " " << a;
        f << "\n";
    }
    // 5. Starten in de unit van de game.
    std::string serr;
    const int rc = spawn_(native_unit_argv(c_, pl.argv, log), serr);
    if (rc != 0) {
        if (c_.stop_inputs) c_.stop_inputs();
        return rc > 0 ? "het starten via systemd-run mislukte (code " + std::to_string(rc) + ")" : "het starten mislukte: " + serr;
    }
    // 6. Wachten tot het spelproces er is (of de unit al weer weg is).
    if (c_.tpui_procs.empty()) return {};   // tests zonder cgroup: niets te volgen
    const int64_t until = now_ms() + c_.game_wait_ms;
    ::usleep(500000);
    while (!unit_has_comm(c_.tpui_procs, c_.proc_root, pl.comm)) {
        if (cancel_.load()) return "de start is afgebroken";
        if (!unit_alive(c_.tpui_procs)) {
            if (c_.stop_inputs) c_.stop_inputs();
            const std::string last = log.empty() ? std::string() : last_line(log);
            return "de game stopte meteen" + (last.empty() ? std::string() : ": " + last) + (log.empty() ? "" : " (logboek: " + log + ")");
        }
        if (now_ms() > until) return pr.title.empty() ? profile + " is niet gestart" : pr.title + " is niet gestart" + (log.empty() ? "" : " (logboek: " + log + ")");
        ::usleep(250000);
    }
    return {};
}

std::string GameService::run_unity(const std::string& profile) {
    namespace L = ravengun::launch;
    // 1. Een lightgun.
    if (const std::string e = wait_lightgun(); !e.empty()) return e;
    // 2. De game, de view (symlinks naar de NAS, BepInEx en onze plugin lokaal) en het commando.
    const std::string text = read_text(c_.unity.json);
    const char* po = std::getenv("RAVENGUN_UNITY_PLUGIN");
    L::UnityGame g;
    std::string err, view;
    std::vector<std::string> view_log;
    if (!L::load_unity_game(text, profile, c_.unity.home, po ? po : "", g, err)) return err;
    if (!L::build_view(g, c_.unity, view, view_log, err)) return err;
    L::UnityOptions o;
    auto env = [](const char* k) { const char* v = std::getenv(k); return std::string(v && *v ? v : ""); };
    o.runtime_dir = !c_.native.runtime_dir.empty() ? c_.native.runtime_dir : env("XDG_RUNTIME_DIR");
    if (!env("WAYLAND_DISPLAY").empty()) o.wayland_display = env("WAYLAND_DISPLAY");
    if (!env("DISPLAY").empty()) o.display = env("DISPLAY");
    o.xauthority = !env("XAUTHORITY").empty() ? env("XAUTHORITY") : L::find_xwayland_xauthority(c_.proc_root);
    o.dbus = env("DBUS_SESSION_BUS_ADDRESS");
    L::UnityPlan pl;
    if (!L::plan_unity(g, c_.unity, view, o, pl, err)) return err;
    // 3. Invoer: uitvoer wapenen, dan de TCP-klant van de plugin (hij verbindt zodra de game luistert).
    if (c_.arm) { const std::string e = c_.arm(); if (!e.empty()) return "uitvoer wapenen: " + e; }
    if (c_.start_route) {
        const std::string e = c_.start_route(profile, "unity-tcp");
        if (!e.empty()) { if (c_.stop_inputs) c_.stop_inputs(); return "Unity-route: " + e; }
    }
    if (cancel_.load()) { if (c_.stop_inputs) c_.stop_inputs(); return "de start is afgebroken"; }
    // 4. Het logboek.
    std::string log;
    if (!c_.native_log_dir.empty()) {
        std::error_code ec;
        fs::create_directories(c_.native_log_dir, ec);
        log = c_.native_log_dir + "/unity-" + profile + "-" + stamp("%Y%m%d-%H%M%S") + ".log";
        std::ofstream f(log, std::ios::app);
        f << "== unity " << profile << " " << stamp("%Y%m%d-%H%M%S") << " (gestart door ravengund, zonder script)\n";
        f << "game: " << g.name << " (" << g.nas_dir << "/" << g.exe << "); plugin " << g.plugin << "; poort " << g.port << "\n";
        for (const std::string& l : view_log) f << l << "\n";
        for (const std::string& l : pl.notes) f << l << "\n";
        f << "cmd:";
        for (const std::string& a : pl.argv) f << " " << a;
        f << "\n";
    }
    // 5. Starten in de unit van de game.
    std::string serr;
    const int rc = spawn_(unity_unit_argv(c_, pl, log), serr);
    if (rc != 0) {
        if (c_.stop_inputs) c_.stop_inputs();
        return rc > 0 ? "het starten via systemd-run mislukte (code " + std::to_string(rc) + ")" : "het starten mislukte: " + serr;
    }
    // 6. Wachten tot het spelproces er is (Proton start eerst zijn eigen hulpprogramma's).
    if (c_.tpui_procs.empty()) return {};
    const int64_t until = now_ms() + c_.game_wait_ms;
    ::usleep(500000);
    while (!unit_has_comm(c_.tpui_procs, c_.proc_root, pl.comm, true)) {
        if (cancel_.load()) return "de start is afgebroken";
        if (!unit_alive(c_.tpui_procs)) {
            if (c_.stop_inputs) c_.stop_inputs();
            const std::string last = log.empty() ? std::string() : last_line(log);
            return "de game stopte meteen" + (last.empty() ? std::string() : ": " + last) + (log.empty() ? "" : " (logboek: " + log + ")");
        }
        if (now_ms() > until) return g.name + " is niet gestart" + (log.empty() ? "" : " (logboek: " + log + ")");
        ::usleep(250000);
    }
    return {};
}

std::string GameService::status_json() {
    const bool game_run = game_process_running(c_.proc_root) || unit_alive(c_.tpui_procs);
    const int sc = script_state(c_.proc_root, c_.play_script);
    const bool move = c_.move_connected ? c_.move_connected() : true;
    std::string state, text, profile, message;
    bool have_profile = false;
    {
        std::lock_guard<std::mutex> g(m_);
        if (busy_.load() && !busy_stop_ && waiting_.load()) {
            state = "waiting_move";
            text = "Wacht op de Move: druk op de PS-knop.";
            have_profile = true;
            profile = profile_;
        } else if (busy_.load()) {
            state = busy_stop_ ? "stopping" : "starting";
            text = busy_stop_ ? "De game wordt gestopt..." : "De game wordt gestart...";
            have_profile = !busy_stop_;
            profile = profile_;
        } else if (sc == 2) {
            state = "stopping";
            text = "De game wordt gestopt...";
        } else if (sc == 1 || (launched_ && now_ms() - launched_ms_ < c_.start_grace_ms)) {
            // Het script draait nog (het wacht op de Move, op de game of controleert iets): de game telt nog niet als draaiend.
            state = move ? "starting" : "waiting_move";
            text = move ? "De game wordt gestart..." : "Wacht op de Move: druk op de PS-knop.";
            have_profile = true;
            profile = profile_;
        } else {
            if (launched_) {  // het script is klaar: de laatste regel van het script beslist of de start gelukt is
                launched_ = false;
                std::string last = c_.log_path.empty() ? std::string() : last_line(c_.log_path);
                if (!ends_with(last, kSuccessMarker) && !(game_run && c_.log_path.empty())) {
                    failed_ = true;
                    failed_profile_ = profile_;
                    failed_msg_ = last.empty() ? "het startscript stopte zonder melding" : last;
                }
            }
            if (failed_) {
                state = "failed";
                text = "Starten is mislukt.";
                message = failed_msg_;
                have_profile = !failed_profile_.empty();
                profile = failed_profile_;
            } else if (game_run) {
                state = "running";
                have_profile = !profile_.empty();
                profile = profile_;
                text = "De game draait.";
            } else {
                state = "idle";
                text = "Er draait geen game.";
            }
        }
    }
    json::Writer w;
    w.begin_object().key("state").value(state).key("text").value(text);
    w.key("running").value(state == "running");
    w.key("busy").value(state == "starting" || state == "waiting_move" || state == "stopping");
    w.key("stoppable").value(game_run || state == "starting" || state == "waiting_move");
    if (have_profile && !profile.empty()) w.key("profile").value(profile); else w.key("profile").null();
    if (message.empty()) w.key("message").null(); else w.key("message").value(message);
    w.end_object();
    return w.str();
}

std::string GameService::list_json() {
    std::error_code ec;
    const bool avail = !c_.tp_dir.empty() && fs::is_directory(fs::path(c_.tp_dir) / "UserProfiles", ec);
    json::Writer w;
    w.begin_object().key("available").value(avail);
    if (!avail) w.key("message").value("De TeknoParrot-profielen zijn niet gevonden. Controleer of TeknoParrot is geinstalleerd.");
    // De games met een eigen profiel uit de cache; is die oud of leeg, dan zoekt een achtergrondthread opnieuw (het verzoek wacht niet).
    std::vector<Game> own;
    {
        std::lock_guard<std::mutex> g(own_m_);
        own = own_games_;
        if (!own_busy_ && (own_ms_ < 0 || now_ms() - own_ms_ > kOwnGamesTtlMs)) {
            own_busy_ = true;
            if (own_thread_.joinable()) own_thread_.join();   // de vorige is klaar (own_busy_ was false)
            own_thread_ = std::thread([this] {
                std::vector<Game> found = own_profile_games(c_);
                std::lock_guard<std::mutex> lk(own_m_);
                own_games_ = std::move(found);
                own_ms_ = now_ms();
                own_busy_ = false;
            });
        }
    }
    w.key("games").begin_array();
    for (const auto& g : list_games(c_, &own)) {
        w.begin_object().key("profile").value(g.profile).key("name").value(g.name);
        if (g.icon_file.empty()) w.key("icon").null(); else w.key("icon").value("/api/game-icon/" + g.icon_file);
        if (g.note.empty()) w.key("note").null(); else w.key("note").value(g.note);
        w.end_object();
    }
    w.end_array();
    w.key("status").raw(status_json());
    w.end_object();
    return w.str();
}

}  // namespace ravengun::games
