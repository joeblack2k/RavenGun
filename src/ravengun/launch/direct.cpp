// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10) zonder scripts; zie direct.hpp. Elke functie hier heeft een tegenhanger in scripts/launch-direct.py,
// scripts/gamesettings.py of scripts/ownprofile.py met dezelfde naam of een vertaling ervan (in het commentaar genoemd), zodat
// een verschil tussen beide in tests/direct_golden.py naar een plek te herleiden is.
#include "ravengun/launch/direct.hpp"

#include <fcntl.h>
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
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "ravengun/iobridge/control_pipe.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/launch/native.hpp"

extern char** environ;

namespace ravengun::launch {

namespace fs = std::filesystem;
namespace gsn = ravengun::gamesettings;
namespace iob = ravengun::iobridge;

namespace {

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
std::string cwd_now() {
    char b[4096];
    return ::getcwd(b, sizeof b) ? std::string(b) : std::string("/");
}
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
std::string getenv_or(const EnvMap& env, const std::string& k, const std::string& def = {}) {
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
// De tekst van het eerste <tag>...</tag> of <tag/> (found = false als de tag er niet is).
std::string xml_tag(const std::string& xml, const std::string& tag, bool* found = nullptr) {
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

// --- gamesettings.py: het oude .env, to_env, env_to_settings -----------------------------------------------------------------------
std::string prefixes_dir(const DirectPaths& p) { return p.home + "/.local/share/ravengun/prefixes"; }
std::string shared_prefix(const DirectPaths& p) { return prefixes_dir(p) + "/shared/wine"; }
std::string prefix_path(std::string v, const DirectPaths& p) {
    v = trim(v);
    if (v == "shared") return shared_prefix(p);
    if (starts_with(v, "/") || starts_with(v, "~")) return expand_home(v, p.home);
    return pjoin(prefixes_dir(p), v);
}
std::string prefix_value(const std::string& path, const DirectPaths& p) {
    if (path == shared_prefix(p)) return "shared";
    const std::string pre = prefixes_dir(p) + "/";
    if (starts_with(path, pre)) {
        const std::string rest = path.substr(pre.size());
        if (rest.find('/') == std::string::npos && rest != "shared") return rest;
    }
    return shrink_home(path, p.home);
}

bool is_drive_path(const std::string& v) {
    return v.size() >= 3 && std::isalpha(static_cast<unsigned char>(v[0])) && v[1] == ':' && (v[2] == '\\' || v[2] == '/');
}

EnvMap to_env(const gsn::Ini& s, const std::string& gamedir, const DirectPaths& p) {
    EnvMap env;
    for (size_t i = 0; i < gsn::kEnvMapCount; ++i) {
        const auto& m = gsn::kEnvMap[i];
        const std::string* raw = s.find(m.section, m.key);
        if (!raw || raw->empty()) continue;
        std::string v = *raw;
        const std::string kind = m.kind;
        if (kind == "bool1" || kind == "boolT") {
            const bool one = kind == "bool1";
            v = gsn::truthy(v) ? (one ? "1" : "true") : gs_falsy(v) ? (one ? "0" : "false") : v;
        } else if (kind == "path") {
            v = expand_home(v, p.home);
        } else if (kind == "prefix") {
            v = prefix_path(v, p);
        } else if (kind == "workdir") {
            v = expand_home(v, p.home);
            if (!(is_drive_path(v) || starts_with(v, "/"))) {
                if (gamedir.empty()) continue;
                v = wine_path(normpath(pjoin(gamedir, v)));
            } else if (starts_with(v, "/")) {
                v = wine_path(v);
            }
        }
        env[m.var] = v;
    }
    if (const gsn::Section* e = s.section("Env"))
        for (const auto& kv : e->kv) env[kv.first] = kv.second;
    return env;
}

bool is_secret_name(const std::string& name) {
    const std::string u = upper(name);
    for (const char* w : {"TOKEN", "SECRET", "PASSWORD", "PASSWD", "APIKEY", "API_KEY", "SERIAL", "LICENSE", "LICENCE", "PATREON"})
        if (u.find(w) != std::string::npos) return true;
    return false;
}

// read_env_file(): K=V per regel, # is commentaar (ook achter een waarde). Volgorde blijft.
std::vector<std::pair<std::string, std::string>> read_env_file(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> out;
    std::ifstream f(path);
    std::string line;
    while (f && std::getline(f, line)) {
        line = trim(line.substr(0, line.find('#')));
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        bool done = false;
        for (auto& kv : out)
            if (kv.first == k) { kv.second = v; done = true; }
        if (!done) out.emplace_back(k, v);
    }
    return out;
}

gsn::Ini env_to_settings(const std::vector<std::pair<std::string, std::string>>& env, const std::string& gamedir, const DirectPaths& p,
                         bool* fpfix) {
    gsn::Ini s;
    for (const auto& [var, val] : env) {
        if (var == "RG_LOADER_FPFIX") {
            if (gsn::truthy(val) && fpfix) *fpfix = true;
            continue;
        }
        if (is_secret_name(var)) continue;
        const gsn::EnvMapEntry* m = gsn::env_for_var(var);
        if (!m) { s.set("Env", var, val); continue; }
        const std::string kind = m->kind;
        std::string out = val;
        if (kind == "bool1") out = val == "1" ? "true" : val == "0" ? "false" : val;
        else if (kind == "path") out = shrink_home(val, p.home);
        else if (kind == "prefix") out = prefix_value(val, p);
        else if (kind == "workdir") {
            if (!gamedir.empty() && starts_with(val, "Z:")) {
                std::string posix = val.substr(2);
                std::replace(posix.begin(), posix.end(), '\\', '/');
                std::string gd = gamedir;
                while (gd.size() > 1 && gd.back() == '/') gd.pop_back();
                if (posix == gamedir || starts_with(posix, gd + "/")) {
                    const std::string rel = relpath(posix, gamedir);
                    gsn::Ini probe;
                    probe.set("Launch", "WorkingDirectory", rel);
                    if (getenv_or(to_env(probe, gamedir, p), var) == val) out = rel;
                }
            }
            if (out == val) out = starts_with(val, "/") ? shrink_home(val, p.home) : val;
        }
        s.set(m->section, m->key, out);
    }
    return s;
}

// De gamemappen uit library.json (ownprofile.library_folders: ~ uitgeklapt en genormaliseerd).
std::vector<std::string> library_folders(const DirectPaths& p) {
    std::vector<std::string> out;
    std::string text;
    if (p.library_file.empty() || !read_file(p.library_file, text)) return out;
    const auto r = ravengun::json::parse(text);
    if (!r.ok || !r.value.is_object()) return out;
    const auto* folders = r.value.find("folders");
    if (!folders || !folders->is_array()) return out;
    for (const auto& v : folders->arr) {
        std::string s = v.is_string() ? v.str : (v.is_object() && v.find("path") && v.find("path")->is_string() ? v.find("path")->str : std::string());
        if (!s.empty()) out.push_back(normpath(expand_home(s, p.home)));
    }
    return out;
}

std::string locate(const std::string& profile, const gsn::Ini& settings, const DirectPaths& p, std::string* how) {
    std::string ov = trim(settings.get("Game", "GamePath"));
    if (!ov.empty()) {
        if (how) *how = "override";
        return expand_home(ov, p.home);
    }
    return gsn::locate_game(profile, settings, library_folders(p), p.views(), p.home, how, [](const std::string& f) { return sha256_file(f); });
}

std::string user_xml(const DirectPaths& p, const std::string& profile) { return p.tp + "/UserProfiles/" + profile + ".xml"; }

// gamesettings.profile_gamedir(): de map van de game volgens het eigen profiel (zonder het oude .env), anders de XML.
std::string profile_gamedir(const std::string& profile, const DirectPaths& p) {
    const gsn::Game g = gsn::load_game(p.dirs, profile);
    if (gsn::has_own_profile(g.settings)) {
        const std::string own = locate(profile, g.settings, p, nullptr);
        if (!own.empty()) return dirname(own);
    }
    std::string xml;
    if (!read_file(user_xml(p, profile), xml)) return {};
    bool found = false;
    const std::string gp = trim(xml_tag(strip_bom(xml), "GamePath", &found));
    return found ? dirname(gp) : std::string();
}

// gamesettings.load_for_profile(): de lagen met het oude UserProfiles/<P>.env eronder.
gsn::Game load_for_profile(const std::string& profile, const DirectPaths& p, std::vector<std::string>* notes, bool* legacy_fpfix,
                           const gsn::Dirs* dirs_override = nullptr) {
    const gsn::Dirs& dirs = dirs_override ? *dirs_override : p.dirs;
    std::string hint, xml;
    if (read_file(user_xml(p, profile), xml)) hint = trim(xml_tag(strip_bom(xml), "EmulatorType"));
    gsn::Ini legacy;
    bool have_legacy = false;
    const char* nolegacy = getenv("RAVENGUN_NO_LEGACY_ENV");
    if (!p.tp.empty() && !(nolegacy && std::string(nolegacy) == "1")) {
        const std::string path = p.tp + "/UserProfiles/" + profile + ".env";
        const auto env = read_env_file(path);
        if (!env.empty()) {
            legacy = env_to_settings(env, profile_gamedir(profile, p), p, legacy_fpfix);
            have_legacy = true;
            if (notes) notes->push_back("legacy .env gelezen: " + path + " (game.ini wint; migreer met scripts/migrate-game-settings.py)");
        }
    }
    return gsn::load_game(dirs, profile, hint, have_legacy ? &legacy : nullptr);
}

// --- het profiel (ownprofile.profile_from_settings) ---------------------------------------------------------------------------------
struct Field { std::string category, name, value, type; std::vector<std::string> options; };
struct Profile {
    std::map<std::string, std::string> s;   // de scalaire velden met hun XML-naam
    std::vector<Field> fields;
    std::string get(const std::string& k, const std::string& def = {}) const { auto it = s.find(k); return it == s.end() ? def : it->second; }
    Field* field(const std::string& name) { for (auto& f : fields) if (f.name == name) return &f; return nullptr; }
    const Field* field(const std::string& name) const { for (const auto& f : fields) if (f.name == name) return &f; return nullptr; }
};

Profile profile_from_settings(const std::string& name, const gsn::Ini& settings, const std::string& game, const DirectPaths& p) {
    Profile pr;
    pr.s["ProfileName"] = name;
    pr.s["GamePath"] = game;
    for (size_t i = 0; i < gsn::kProfileFieldCount; ++i) {
        const auto& f = gsn::kProfileFields[i];
        const std::string v = settings.get(f.section, f.key);
        if (v.empty()) continue;
        pr.s[f.xml] = std::string(f.kind) == "runner" ? (v == "wrapper" ? p.data + "/wrappers/wine-prefix" : expand_home(v, p.home)) : v;
    }
    for (const auto& c : gsn::config_fields(settings)) pr.fields.push_back({c.category, c.name, c.value, c.type, c.options});
    return pr;
}

// --- teknoparrot.ini (build_ini) en de geweer-ini van Taito (taito_gun_ini) -------------------------------------------------------
std::string build_ini(const Profile& pr, const std::map<std::string, std::string>& pd, bool windowed_override) {
    auto pdg = [&](const char* k, const char* def) { auto it = pd.find(k); return it == pd.end() ? std::string(def) : it->second; };
    std::string out;
    if (!pr.get("GameVersion").empty()) out += "[GameInfo]\nGameVersion=" + pr.get("GameVersion") + "\n";
    out += "[GlobalHotkeys]\nExitKey=" + pdg("ExitGameKey", "0x1B") + "\nPauseKey=" + pdg("PauseGameKey", "0x13") + "\n";
    if (std::any_of(pr.fields.begin(), pr.fields.end(), [](const Field& f) { return f.name == "Enable Submission" && f.value == "1"; }))
        out += "[GlobalScore]\nSubmission ID=" + pdg("ScoreSubmissionID", "") + "\nCollapseGUIKey=" + pdg("ScoreCollapseGUIKey", "") + "\n";
    std::vector<std::string> cats;
    for (const auto& f : pr.fields)
        if (std::find(cats.begin(), cats.end(), f.category) == cats.end()) cats.push_back(f.category);
    for (const auto& c : cats) {
        out += "[" + c + "]\n";
        for (const auto& f : pr.fields) {
            if (f.category != c) continue;
            std::string v = f.value;
            if (windowed_override && f.name == "Windowed") v = "1";
            if (f.type == "DropdownIndex") {
                auto it = std::find(f.options.begin(), f.options.end(), v);
                v = it == f.options.end() ? "-1" : std::to_string(it - f.options.begin());
            }
            out += f.name + "=" + v + "\n";
        }
    }
    return out;
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

bool taito_gun_ini(const Profile& pr, std::string& name, std::string& text) {
    static const std::map<std::string, std::pair<std::string, std::string>> taito = {
        {"GaiaAttack4", {"MINIGUN.INI", "GA4"}}, {"HauntedMuseum", {"MUSEUM.INI", "HM"}}, {"HauntedMuseum2", {"HAUNTED2.INI", "HM2"}}};
    auto it = taito.find(pr.get("EmulationProfile"));
    if (it == taito.end()) return false;
    name = it->second.first;
    auto num = [&](const char* n, long long d) {
        const Field* f = pr.field(n);
        long long v = 0;
        return f ? (py_int(f->value, v) ? v : d) : d;
    };
    const long long w = num("ResolutionWidth", 1280), h = num("ResolutionHeight", 720);
    const Field* rf = pr.field("Region");
    const std::string region = rf ? rf->value : "";
    const std::string W = std::to_string(w), H = std::to_string(h);
    text = "REGION\t\t" + region + "\r\nCNFNAME\t\t.\\OpenParrot\\" + it->second.second + "\r\nRANKFILE\t.\\OpenParrot\\\r\nPRJENABLE   \t1\r\n"
           "SCREEN_WIDTH\t" + W + "\r\nSCREEN_HEIGHT\t" + H + "\r\nRENDER_WIDTH\t" + W + "\r\nRENDER_HEIGHT\t" + H + "\r\n"
           "RENDER_WIDTH3D\t" + W + "\r\nRENDER_HEIGHT3D\t" + H + "\r\n";
    return true;
}

// --- drop-ins (dropin_specs, dropin_toets, dropin_plan) ---------------------------------------------------------------------------
struct DropinSpec {
    std::string name, to, dll, ini, src;
    std::vector<std::pair<std::string, std::string>> env;
    bool preload = false, audit = true;
};

// configparser.RawConfigParser (optionxform = str): secties, "sleutel = waarde" of "sleutel: waarde", commentaar op een eigen regel
// (# of ;), een ingesprongen regel hoort bij de vorige waarde. Een dubbele sectie of sleutel, een regel zonder scheidingsteken of een
// sleutel voor de eerste sectie maakt het hele bestand onleesbaar (zoals configparser met strict = True).
bool parse_dropin_ini(const std::string& text_in, std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>& out,
                      std::string& err) {
    std::string text = strip_bom(text_in);
    std::vector<std::string> lines;
    {
        std::string cur;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') ++i; lines.push_back(cur); cur.clear(); }
            else if (text[i] == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += text[i];
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    std::vector<std::pair<std::string, std::string>> defaults;
    std::vector<std::pair<std::string, std::string>>* cursect = nullptr;
    std::string* curval = nullptr;
    size_t indent_level = 0;
    std::set<std::string> seen_sections;
    std::set<std::string> seen_opts;
    int lineno = 0;
    for (const std::string& line : lines) {
        ++lineno;
        const std::string t = trim(line);
        if (!t.empty() && (t[0] == '#' || t[0] == ';')) continue;
        if (t.empty()) {
            if (curval) *curval += "\n";
            continue;
        }
        size_t ind = 0;
        while (ind < line.size() && std::isspace(static_cast<unsigned char>(line[ind]))) ++ind;
        if (cursect && curval && ind > indent_level) {
            *curval += "\n" + t;
            continue;
        }
        indent_level = ind;
        if (t[0] == '[' && t.size() >= 3 && t.back() == ']') {
            const std::string name = t.substr(1, t.size() - 2);
            if (seen_sections.count(name)) { err = "regel " + std::to_string(lineno) + ": sectie '" + name + "' staat er twee keer"; return false; }
            seen_sections.insert(name);
            seen_opts.clear();
            curval = nullptr;
            if (name == "DEFAULT") { cursect = &defaults; continue; }
            out.emplace_back(name, std::vector<std::pair<std::string, std::string>>{});
            cursect = &out.back().second;
            continue;
        }
        if (!cursect) { err = "regel " + std::to_string(lineno) + ": geen sectiekop"; return false; }
        const size_t d = t.find_first_of("=:");
        if (d == std::string::npos) { err = "regel " + std::to_string(lineno) + ": geen sleutel = waarde"; return false; }
        std::string key = t.substr(0, d);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        if (key.empty()) { err = "regel " + std::to_string(lineno) + ": lege sleutel"; return false; }
        if (seen_opts.count(key)) { err = "regel " + std::to_string(lineno) + ": sleutel '" + key + "' staat er twee keer"; return false; }
        seen_opts.insert(key);
        cursect->emplace_back(key, trim(t.substr(d + 1)));
        curval = &cursect->back().second;
    }
    // Waarden over meer regels: aan het eind de lege regels eraf (configparser: '\n'.join(val).rstrip()).
    auto fix = [](std::vector<std::pair<std::string, std::string>>& kv) {
        for (auto& p : kv) while (!p.second.empty() && std::isspace(static_cast<unsigned char>(p.second.back()))) p.second.pop_back();
    };
    fix(defaults);
    for (auto& s : out) {
        fix(s.second);
        for (const auto& d : defaults)
            if (std::none_of(s.second.begin(), s.second.end(), [&](const auto& kv) { return kv.first == d.first; })) s.second.push_back(d);
    }
    return true;
}

std::string kv_get(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k, const std::string& def = {}) {
    for (const auto& p : kv) if (p.first == k) return p.second;
    return def;
}

std::vector<DropinSpec> dropin_specs(const std::string& game, const DirectPaths& p, std::vector<std::string>& fouten) {
    std::vector<DropinSpec> specs;
    for (const std::string& root : {p.dirs.user, p.dirs.shipped}) {   // eerste treffer wint: gebruiker, dan meegeleverd
        if (root.empty() || !isdir(root + "/" + game)) continue;
        const std::string pad = root + "/" + game + "/dropin/dropin.ini";
        if (!isfile(pad)) continue;
        std::string text, perr;
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> secs;
        if (!read_file(pad, text) || !parse_dropin_ini(text, secs, perr)) {
            fouten.push_back("kan " + pad + " niet lezen: " + (perr.empty() ? std::string("niet te openen") : perr));
            continue;
        }
        const std::string dir = dirname(pad);
        for (const auto& [naam, sectie] : secs) {
            if (std::any_of(specs.begin(), specs.end(), [&](const DropinSpec& s) { return s.name == naam; })) continue;
            std::string to = trim(kv_get(sectie, "To"));
            if (to.empty()) to = ".";
            std::string tos = to;
            while (!tos.empty() && tos.back() == '/') tos.pop_back();
            if (to != "." && starts_with(naam, tos + "/")) {
                fouten.push_back(pad + " [" + naam + "]: de sectienaam begint ook met To = " + to +
                                 ", dus het bestand komt dubbel in de view te staan; laat To weg of zet To = . (docs/dropins.md)");
                continue;
            }
            DropinSpec s;
            s.name = naam;
            s.to = to;
            bool bad = false;
            for (std::string stuk : split(kv_get(sectie, "Env"), ';')) {
                stuk = trim(stuk);
                if (stuk.empty()) continue;
                const size_t eq = stuk.find('=');
                if (eq == std::string::npos) {
                    fouten.push_back(pad + " [" + naam + "]: Env-regel '" + stuk + "' is geen SLEUTEL=WAARDE");
                    continue;
                }
                const std::string k = trim(stuk.substr(0, eq)), v = trim(stuk.substr(eq + 1));
                bool done = false;
                for (auto& kv : s.env) if (kv.first == k) { kv.second = v; done = true; }
                if (!done) s.env.emplace_back(k, v);
            }
            s.dll = lower(trim(kv_get(sectie, "DllOverride")));
            if (!s.dll.empty() && s.dll != "native" && s.dll != "builtin,native" && s.dll != "builtin") {
                fouten.push_back(pad + " [" + naam + "]: DllOverride = '" + s.dll + "' (kies native of builtin,native)");
                bad = true;
            }
            if (bad) continue;
            s.preload = ld_truthy(kv_get(sectie, "Preload"));
            s.audit = ld_truthy(kv_get(sectie, "Audit", "true"));
            s.ini = pad;
            s.src = pjoin(dir, naam);
            specs.push_back(std::move(s));
        }
    }
    return specs;
}

std::string view_wortel(const std::string& base, const DirectPaths& p) {
    const std::string views = abspath(p.views()), b = abspath(base);
    if (b != views && binnen(b, views)) return pjoin(views, split(relpath(b, views), '/')[0]);
    return b;
}

// --- twee exe's (in_dezelfde_view, second_start) -------------------------------------------------------------------------------------
bool samefile(const std::string& a, const std::string& b) {
    struct stat sa{}, sb{};
    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

// Het pad van de tweede exe BINNEN de view van de eerste; leeg + reden als hij erbuiten ligt (zie launch-direct.py in_dezelfde_view).
std::string in_dezelfde_view(const std::string& game, const std::string& second, const DirectPaths& p, std::string& waarom) {
    const std::string wortel = view_wortel(dirname(game), p);
    if (wortel == abspath(dirname(game))) return second;   // de eerste staat niet in een view: er valt niets gelijk te trekken
    if (binnen(second, wortel)) return second;
    const std::string rel = relpath(abspath(game), wortel);
    const std::string echt = realpath_(game);
    if (ends_with(echt, "/" + rel)) {   // waar de view naar wijst: de map van het spel zelf
        std::string bron = echt.substr(0, echt.size() - rel.size());
        while (!bron.empty() && bron.back() == '/') bron.pop_back();
        const std::string echt2 = realpath_(second);
        if (binnen(echt2, bron)) {
            const std::string kandidaat = pjoin(wortel, relpath(echt2, bron));
            if (isfile(kandidaat) && samefile(kandidaat, echt2)) return kandidaat;
        }
    }
    waarom = "de tweede exe " + second + " ligt buiten de view " + wortel + " van de eerste; beide moeten in dezelfde view draaien "
             "(maak de view opnieuw of zet [Game] GamePath2)";
    return {};
}

// float() van Python voor [Launch] SecondWait: decimaal of met exponent, inf en nan; geen hex. false = geen getal.
bool py_float(const std::string& s, double& out) {
    const std::string t = trim(s);
    if (t.empty() || lower(t).find('x') != std::string::npos) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0';
}

struct SecondStart { std::string path, profile_args, own_args; bool first = false; double wait = 5.0; };
constexpr double kDefaultSecondWait = 5.0;   // DEFAULT_SECOND_WAIT: seconden tussen twee starts als game.ini niets zegt

// second_start(): 0 = een gewone game (een exe), 1 = `out` gevuld, -1 = niet direct te starten, -2 = instelling geweigerd (melding in `m`).
int second_start(const Profile& pr, const gsn::Ini& settings, const std::string& game, const DirectPaths& p, SecondStart& out, std::string& m) {
    if (!ld_truthy(pr.get("HasTwoExecutables", "false"))) return 0;
    std::string pad = expand_home(trim(settings.get("Game", "GamePath2")), p.home);
    if (pad.empty()) pad = trim(pr.get("GamePath2"));
    if (pad.empty()) {
        const std::string path = gsn::identify_of(settings).path;
        std::string path2 = trim(settings.get("Identify", "Path2"));
        std::replace(path2.begin(), path2.end(), '\\', '/');
        while (!path2.empty() && path2.front() == '/') path2.erase(path2.begin());
        while (!path2.empty() && path2.back() == '/') path2.pop_back();
        if (!path2.empty()) {
            std::string root;
            if (!path.empty() && ends_with(game, "/" + path)) {
                root = game.substr(0, game.size() - path.size());
                while (!root.empty() && root.back() == '/') root.pop_back();
            } else {
                root = dirname(game);
            }
            pad = pjoin(root, path2);
        }
    }
    if (pad.empty() || !isfile(pad)) {
        m = "het profiel heeft twee exe's (HasTwoExecutables) maar de tweede is niet gevonden: '" + pad +
            "' (zet GamePath2 in het profiel, of [Identify] Path2 / [Game] GamePath2 in game.ini)";
        return -1;
    }
    std::string waarom;
    pad = in_dezelfde_view(game, pad, p, waarom);
    if (pad.empty()) { m = waarom; return -1; }
    std::string ruw = trim(settings.get("Launch", "SecondWait"));
    std::replace(ruw.begin(), ruw.end(), ',', '.');
    double wacht = kDefaultSecondWait;
    if (!ruw.empty() && !py_float(ruw, wacht)) wacht = -1.0;
    if (!(wacht >= 0 && wacht <= 600)) {
        m = "[Launch] SecondWait = " + ruw + " (een aantal seconden van 0 tot en met 600)";
        return -2;
    }
    // [Launch] SecondArguments (onze sleutel, letterlijk) gaat voor op SecondExecutableArguments van het profiel.
    const bool eigen = settings.find("Launch", "SecondArguments") != nullptr;
    out.path = pad;
    out.own_args = eigen ? settings.get("Launch", "SecondArguments") : std::string();
    out.profile_args = eigen ? std::string() : pr.get("SecondExecutableArguments");
    out.first = ld_truthy(pr.get("LaunchSecondExecutableFirst", "false"));
    out.wait = wacht;
    return 1;
}

// spel_argumenten(): de OpenParrot-lader krijgt alles als EEN parameter (profiel met spaties, dan de eigen tekst letterlijk); andere
// laders de delen los (profiel, dan de eigen tekst opgedeeld zoals Windows dat doet).
std::vector<std::string> spel_argumenten(const std::string& loader, const std::vector<std::string>& profiel_delen, const std::string& eigen_ruw) {
    const std::string eigen = trim(eigen_ruw);
    const std::string ll = lower(basename(loader));
    std::vector<std::string> out;
    if (ll == "openparrotloader.exe" || ll == "openparrotloader64.exe") {
        std::vector<std::string> parts;
        if (!join(profiel_delen, " ").empty()) parts.push_back(join(profiel_delen, " "));
        if (!eigen.empty()) parts.push_back(eigen);
        const std::string param = join(parts, " ");
        if (!param.empty()) out.push_back(param);
        return out;
    }
    out = profiel_delen;
    const auto w = win_split(eigen);
    out.insert(out.end(), w.begin(), w.end());
    return out;
}

// (_dropin_keten) wortel en de padonderdelen tot de map van dest; false + melding bij een fout.
bool dropin_keten(const std::string& base_in, const std::string& dest_in, const std::string& wortel_in, std::string& wortel,
                  std::vector<std::string>& delen, std::string& err) {
    const std::string base = abspath(base_in), dest = abspath(dest_in);
    wortel = wortel_in.empty() ? base : abspath(wortel_in);
    if (!binnen(dest, base)) { err = "het doel " + dest + " ligt buiten de view " + base; return false; }
    if (!binnen(base, wortel)) { err = "de gamemap " + base + " ligt buiten de view " + wortel; return false; }
    if (wortel != base && islink(wortel)) {
        err = "de view " + wortel + " is zelf een symlink (naar " + realpath_(wortel) + "); maak de view opnieuw";
        return false;
    }
    const std::string rel = relpath(dirname(dest), wortel);
    delen.clear();
    if (rel != ".") delen = split(rel, '/');
    return true;
}

bool dropin_toets(const std::string& base, const std::string& dest, const std::string& wortel_in, std::string& err, std::vector<std::string>& lokaal) {
    std::string wortel;
    std::vector<std::string> delen;
    if (!dropin_keten(base, dest, wortel_in, wortel, delen, err)) return false;
    const std::string rel = relpath(dirname(abspath(dest)), wortel);
    if (wortel == abspath(base) && !binnen(pjoin(realpath_(wortel), rel), wortel)) {
        err = "de gamemap " + wortel + " loopt via een symlink naar " + realpath_(wortel) +
              " en er is geen view omheen om lokaal te maken; het bestand zou buiten de gamemap terechtkomen (maak een view: "
              "scripts/tp-add-game.py <Spel> <pad> --view)";
        return false;
    }
    lokaal.clear();
    std::string cur = wortel, werkelijk = wortel;
    bool gevolgd = false;
    for (const std::string& deel : delen) {
        cur = pjoin(cur, deel);
        const std::string kandidaat = pjoin(werkelijk, deel);
        if (!lexists(kandidaat)) break;
        if (islink(kandidaat) || gevolgd) {
            const std::string doel = realpath_(kandidaat);
            if (!isdir(doel)) { err = cur + " wijst naar " + doel + " en dat is geen map"; return false; }
            lokaal.push_back(cur);
            werkelijk = doel;
            gevolgd = true;
        } else if (isdir(kandidaat)) {
            werkelijk = kandidaat;
        } else {
            err = cur + " is een bestand, geen map";
            return false;
        }
    }
    return true;
}

bool is_pe(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char b[2] = {0, 0};
    return f.read(b, 2) && b[0] == 'M' && b[1] == 'Z';
}

std::string lader_naam(const DirectPlan& pl) {
    auto it = std::find(pl.argv.begin(), pl.argv.end(), pl.wine);
    if (!pl.wine.empty() && it != pl.argv.end() && it + 1 != pl.argv.end()) return basename(*(it + 1));
    return {};
}

std::vector<std::string> nonempty_split(const std::string& s, char sep) {
    std::vector<std::string> out;
    for (const std::string& x : split(s, sep)) if (!x.empty()) out.push_back(x);
    return out;
}

struct DropinResult {
    EnvMap env;
    std::vector<std::string> overrides, preload, audit, geen_audit, errors;
    struct Kanaal { std::string file, var, value, by; bool loads = false; };
    std::vector<Kanaal> kanalen;
};

DropinResult dropin_plan(DirectPlan& pl, const DirectPaths& p) {
    DropinResult r;
    const std::vector<DropinSpec> specs = dropin_specs(pl.profile, p, r.errors);
    const std::string base = dirname(pl.game), wortel = view_wortel(base, p);
    pl.dropin_base = base;
    pl.dropin_root = wortel;
    for (const DropinSpec& s : specs) {
        if (!isfile(s.src)) { r.errors.push_back(s.ini + " [" + s.name + "]: " + s.name + " ontbreekt in dropin/"); continue; }
        const std::string dest = normpath(pjoin(pjoin(base, s.to), s.name));
        if (on_nas(base, p) && !binnen(base, p.views())) {
            r.errors.push_back(s.ini + " [" + s.name + "]: deze game draait rechtstreeks van de NAS, dus er is geen view om het bestand in te zetten; "
                               "maak eerst een lokale view (scripts/tp-add-game.py " + pl.profile + " <pad> --view)");
            continue;
        }
        if (!binnen(dest, base)) { r.errors.push_back(s.ini + " [" + s.name + "]: het doel " + dest + " ligt buiten de view " + base); continue; }
        std::string waarom;
        std::vector<std::string> lokaal;
        if (!dropin_toets(base, dest, wortel, waarom, lokaal)) {
            r.errors.push_back(s.ini + " [" + s.name + "]: niet te plaatsen: " + waarom);
            continue;
        }
        for (const auto& m : lokaal)
            if (std::find(pl.dropin_materialise.begin(), pl.dropin_materialise.end(), m) == pl.dropin_materialise.end()) pl.dropin_materialise.push_back(m);
        pl.dropin_files.push_back({s.src, dest, s.name});
        if (!s.dll.empty()) {
            const std::string dllnaam = ends_with(lower(s.name), ".dll") ? s.name.substr(0, s.name.size() - 4) : s.name;
            r.overrides.push_back(dllnaam + "=" + (s.dll == "native" ? "n" : "n,b"));
        }
        if (s.preload) {
            r.preload.push_back(dest);
            (s.audit ? r.audit : r.geen_audit).push_back(dest);
        }
        for (const auto& kv : s.env) r.env[kv.first] = kv.second;
    }
    if (!r.overrides.empty()) {
        auto delen = nonempty_split(getenv_or(pl.env, "WINEDLLOVERRIDES"), ';');
        for (const auto& o : r.overrides) if (std::find(delen.begin(), delen.end(), o) == delen.end()) delen.push_back(o);
        r.env["WINEDLLOVERRIDES"] = join(delen, ";");
    }
    if (!r.preload.empty()) {
        const bool wine = !pl.wine.empty();
        const bool pe = is_pe(pl.game);
        const std::string var = wine && !pe ? "LD_AUDIT" : !wine ? "LD_PRELOAD" : "";
        if (wine && pe) {
            const std::string lader = lader_naam(pl);
            const std::string al = trim(getenv_or(pl.env, "TP_PRELOAD_DLL"));
            const std::string ll = lower(lader);
            std::vector<std::string> names;
            for (const auto& x : r.preload) names.push_back(basename(x));
            if (ll != "openparrotloader.exe" && ll != "openparrotloader64.exe") {
                r.errors.push_back("Preload = true (" + join(names, ", ") + "): de lader " + (lader.empty() ? "(onbekend)" : lader) +
                                   " leest geen TP_PRELOAD_DLL, dus op deze route is er geen kanaal om een DLL in het spelproces te laden");
            } else if (r.preload.size() > 1) {
                r.errors.push_back("Preload = true staat bij " + std::to_string(r.preload.size()) + " bestanden (" + join(names, ", ") +
                                   "), maar de lader laadt er via TP_PRELOAD_DLL precies een; laat de ene DLL de andere laden");
            } else if (!al.empty() && al != wine_path(r.preload[0])) {
                r.errors.push_back("Preload = true (" + basename(r.preload[0]) + "): TP_PRELOAD_DLL staat al op " + al + " en de lader laadt er precies een");
            } else {
                r.env["TP_PRELOAD_DLL"] = wine_path(r.preload[0]);
            }
        }
        if (!var.empty()) {
            const auto& bron = var == "LD_AUDIT" ? r.audit : r.preload;
            auto delen = nonempty_split(getenv_or(pl.env, var), ':');
            for (const auto& x : bron) if (std::find(delen.begin(), delen.end(), x) == delen.end()) delen.push_back(x);
            const std::string waarde = join(delen, ":");
            if (!waarde.empty()) r.env[var] = waarde;
        }
        auto delen = nonempty_split(getenv_or(pl.env, "RG_DROPIN_PRELOAD"), ':');
        for (const auto& x : r.preload) if (std::find(delen.begin(), delen.end(), x) == delen.end()) delen.push_back(x);
        r.env["RG_DROPIN_PRELOAD"] = join(delen, ":");
    }
    for (const auto& pad : r.preload) {
        DropinResult::Kanaal k;
        k.file = pad;
        const auto audit = nonempty_split(getenv_or(r.env, "LD_AUDIT"), ':'), pre = nonempty_split(getenv_or(r.env, "LD_PRELOAD"), ':');
        if (getenv_or(r.env, "TP_PRELOAD_DLL") == wine_path(pad) && r.env.count("TP_PRELOAD_DLL")) {
            k = {pad, "TP_PRELOAD_DLL", r.env["TP_PRELOAD_DLL"], "de OpenParrot-lader (" + lader_naam(pl) + ") laadt deze DLL na de kern in het spelproces", true};
        } else if (std::find(audit.begin(), audit.end(), pad) != audit.end()) {
            k = {pad, "LD_AUDIT", pad, "glibc laadt dit in het proces van de Cygwin-lader, waar de game-ELF in geladen wordt", true};
        } else if (std::find(pre.begin(), pre.end(), pad) != pre.end()) {
            k = {pad, "LD_PRELOAD", pad, "de dynamische linker van de native start", true};
        } else {
            k = {pad, "RG_DROPIN_PRELOAD", pad, "NIEMAND op deze route: alleen de native route en de eigen ELF-lader lezen deze variabele", false};
        }
        r.kanalen.push_back(k);
    }
    // dropin_log_lines()
    for (const auto& e : r.errors) pl.dropin_lines.push_back("FOUT: " + e);
    for (const auto& m : pl.dropin_materialise) pl.dropin_lines.push_back(m + " loopt via een symlink; wordt eerst een echte map met symlinks");
    for (const auto& f : pl.dropin_files) pl.dropin_lines.push_back(f.name + " -> " + f.dest);
    if (!r.overrides.empty()) pl.dropin_lines.push_back("WINEDLLOVERRIDES += " + join(r.overrides, ", "));
    std::set<std::string> geladen;
    for (const auto& k : r.kanalen) {
        if (k.loads) {
            geladen.insert(k.file);
            pl.dropin_lines.push_back("preload: " + basename(k.file) + " -> " + k.var + "=" + k.value + " (" + k.by + ")");
        } else {
            pl.dropin_lines.push_back("preload: " + basename(k.file) + " -> alleen " + k.var + "=" + k.value + "; WORDT NIET GELADEN door " + k.by);
        }
    }
    std::vector<std::string> niet;
    for (const auto& x : r.geen_audit) if (!geladen.count(x)) niet.push_back(x);
    if (!niet.empty())
        pl.dropin_lines.push_back("niet in LD_AUDIT (Audit = false): " + join(niet, ", ") +
                                  " -- deze bibliotheek hoort in het spelproces en niet in het Cygwin-proces van de lader");
    return r;
}

// merge_env(): LD_PRELOAD en WINEDLLOVERRIDES samenvoegen, de rest overschrijven; in env en launch_env.
void merge_env(DirectPlan& pl, const EnvMap& extra) {
    for (const auto& [k, v] : extra)
        for (EnvMap* doel : {&pl.env, &pl.launch_env}) {
            auto it = doel->find(k);
            if (k == "LD_PRELOAD" && it != doel->end() && !it->second.empty()) {
                auto delen = nonempty_split(it->second, ':');
                if (std::find(delen.begin(), delen.end(), v) == delen.end()) delen.push_back(v);
                it->second = join(delen, ":");
            } else if (k == "WINEDLLOVERRIDES" && it != doel->end() && !it->second.empty()) {
                auto delen = nonempty_split(it->second, ';');
                for (const auto& stuk : split(v, ';'))
                    if (!stuk.empty() && std::find(delen.begin(), delen.end(), stuk) == delen.end()) delen.push_back(stuk);
                it->second = join(delen, ";");
            } else {
                (*doel)[k] = v;
            }
        }
}

// --- weergave van het scherm (display_size) ----------------------------------------------------------------------------------------
std::pair<int, int> xrandr_size(const EnvMap& env) {
    EnvMap e = environ_map();
    e["DISPLAY"] = getenv_or(env, "DISPLAY", ":0");
    std::string out;
    if (run_capture({"xrandr", "--current"}, e, &out) < 0) return {1920, 1080};
    static const std::regex rx(R"( (\d+)x(\d+)\+\d+\+\d+)");
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line)) {
        if (line.find(" connected primary ") == std::string::npos && line.find(" connected ") == std::string::npos) continue;
        std::smatch m;
        if (std::regex_search(line, m, rx)) return {std::stoi(m[1]), std::stoi(m[2])};
    }
    return {1920, 1080};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
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
    p.home = getenv_or(env, "HOME", "/home/deck");
    const std::string xd = getenv_or(env, "XDG_DATA_HOME"), xs = getenv_or(env, "XDG_STATE_HOME"), xc = getenv_or(env, "XDG_CONFIG_HOME");
    p.data = (starts_with(xd, "/") ? xd : p.home + "/.local/share") + "/ravengun";
    p.state = (xs.empty() ? p.home + "/.local/state" : xs) + "/ravengun";
    p.tp = getenv_or(env, "RAVENGUN_TP_DIR");
    if (p.tp.empty()) p.tp = p.home + "/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64";
    p.library_file = getenv_or(env, "RAVENGUN_LIBRARY_FILE");
    if (p.library_file.empty()) p.library_file = (starts_with(xc, "/") ? xc : p.home + "/.config") + "/ravengun/library.json";
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Het plan (build_plan)

Verdict plan_direct(const std::string& name, const DirectPaths& p, const DirectOptions& o, DirectPlan& pl, std::string& why) {
    pl = DirectPlan{};
    pl.profile = name;
    pl.tp = p.tp;
    pl.state = p.state;
    pl.log_full = o.log_full;
    const std::string& tp = p.tp;
    auto unsupported = [&](const std::string& m) { why = "Niet direct te starten: " + m; return Verdict::Fallback; };
    auto engine = [&](const std::string& m) { why = "nog niet in de engine: " + m; return Verdict::Fallback; };
    auto setting_refused = [&](const std::string& m) { why = "Instelling geweigerd: " + m; return Verdict::Refused; };
    auto patch_refused = [&](const std::string& m) { why = "Patch geweigerd: " + m; return Verdict::Refused; };

    // 1. Instellingen (met het oude .env eronder) en het eigen profiel.
    bool legacy_fpfix_file = false;
    const gsn::Game settings = load_for_profile(name, p, &pl.notes, &legacy_fpfix_file);
    for (const auto& w : settings.warnings) pl.notes.push_back("instellingen: " + w);
    if (!gsn::has_own_profile(settings.settings))
        return engine("geen eigen profiel in patches/" + name + "/game.ini (alleen UserProfiles/" + name + ".xml)");
    {   // load_own_profile(): de revisie van het sjabloon in de runtime
        std::string stock;
        if (read_file(tp + "/GameProfiles/" + name + ".xml", stock)) {
            const std::string want = xml_tag(strip_bom(stock), "GameProfileRevision"), have = settings.get("Game", "ProfileRevision");
            if (want != have)
                return unsupported("profielrevisie " + (have.empty() ? std::string("(geen)") : have) + " in game.ini wijkt af van het sjabloon (" + want +
                                   "); importeer het profiel opnieuw: scripts/import-tp-profiles.py " + name);
        }
    }
    std::string how;
    const std::string game = locate(name, settings.settings, p, &how);
    Profile pr = profile_from_settings(name, settings.settings, game, p);
    pl.game_source = how;
    std::map<std::string, std::string> pd;
    {
        std::string x;
        if (read_file(tp + "/ParrotData.xml", x)) {
            x = strip_bom(x);
            for (const char* k : {"ExitGameKey", "PauseGameKey", "ScoreSubmissionID", "ScoreCollapseGUIKey", "Elfldr2LogToFile", "Elfldr2NetworkAdapterName"}) {
                bool found = false;
                const std::string v = xml_tag(x, k, &found);
                if (found) pd[k] = v;
            }
        }
    }
    const std::string emu = pr.get("EmulationProfile");
    static const std::set<std::string> unsupported_emu = {"AfterBurnerClimax", "TaitoTypeXBattleGear", "NamcoMachStorm", "NamcoPokken", "GuiltyGearRE2",
                                                          "GuiltyGearAPM3", "SiN", "ALLSSWDC", "ALLSSCHRONO", "NamcoMkdx", "EXVS2", "EXVS2XB",
                                                          "SegaToolsIDZ", "APM3Direct", "Vf5Lindbergh", "Vf5cLindbergh"};
    if (unsupported_emu.count(emu)) return unsupported("EmulationProfile " + emu + " heeft eigen startargumenten in TeknoParrotUi (niet nagebouwd)");
    if (game.empty() || !isfile(game))
        return unsupported("game niet gevonden: '" + game + "' (voeg de map met de game toe als gamemap in de app, of zet [Game] GamePath in je eigen game.ini)");
    // Twee exe's (HasTwoExecutables): de tweede start, uit het profiel en game.ini (second_start).
    SecondStart second;
    {
        std::string m;
        const int r = second_start(pr, settings.settings, game, p, second, m);
        if (r == -1) return unsupported(m);
        if (r == -2) return setting_refused(m);
        if (r == 0) second.path.clear();
    }
    const bool have_second = !second.path.empty();
    // resolve_loader()
    const std::string et = pr.get("EmulatorType");
    const bool is64 = ld_truthy(pr.get("Is64Bit", "false"));
    std::string exe_rel = is64 ? "OpenParrotx64/OpenParrotLoader64.exe" : "OpenParrotWin32/OpenParrotLoader.exe", dll_rel;
    if (et == "ElfLdr2") exe_rel = is64 ? "ElfLdr2/x64/BudgieLoader_x64.exe" : "ElfLdr2/BudgieLoader.exe";
    else if (et == "Lindbergh") exe_rel = "TeknoParrot/BudgieLoader.exe";
    else if (et == "N2") exe_rel = "N2/BudgieLoader.exe";
    else if (et == "TeknoMacaw") exe_rel = is64 ? "TeknoParrot/TeknoMacaw64.exe" : "TeknoParrot/TeknoMacaw.exe";
    else if (et == "OpenParrot") dll_rel = is64 ? "OpenParrotx64/OpenParrot64" : "OpenParrotWin32/OpenParrot";
    else if (et == "OpenParrotKonami") exe_rel = "OpenParrotWin32/OpenParrotKonamiLoader.exe";
    else if (et == "TeknoParrot") dll_rel = is64 ? "TeknoParrot/TeknoParrot64" : "TeknoParrot/TeknoParrot";
    else return unsupported("EmulatorType " + et + " wordt niet direct gestart (externe emulator of eigen voorbereiding)");
    const std::string gamedir = dirname(game);
    const bool elf_like = et == "ElfLdr2" || et == "N2" || et == "Lindbergh";
    const std::string cwd = elf_like ? gamedir : tp;
    std::string loader = pjoin(tp, exe_rel);
    if (elf_like && !(et == "ElfLdr2" && is64) && isfile(pjoin(gamedir, exe_rel))) loader = pjoin(gamedir, exe_rel);
    if (!isfile(loader)) return unsupported("lader ontbreekt: " + loader);
    if (!dll_rel.empty() && !isfile(pjoin(tp, dll_rel + ".dll"))) return unsupported("kern ontbreekt: " + pjoin(tp, dll_rel) + ".dll");

    // 2. Omgeving, keuzes van het profiel, eigen build, patches.
    EnvMap env = o.base_env;
    for (const char* key : {"DongleRegion", "PcbRegion"}) {   // apply_profile_choices()
        const std::string want = trim(settings.get("Game", key));
        if (want.empty()) continue;
        Field* f = pr.field(key);
        if (!f) return setting_refused("game.ini zet " + std::string(key) + " = " + want + ", maar het profiel " + name + " heeft die optie niet");
        if (std::find(f->options.begin(), f->options.end(), want) == f->options.end())
            return setting_refused("game.ini zet " + std::string(key) + " = " + want + "; het profiel " + name + " biedt alleen: " +
                                   (f->options.empty() ? std::string("(geen keuzelijst)") : join(f->options, ", ")));
        f->value = want;
    }
    const EnvMap prof_env = to_env(settings.settings, gamedir, p);
    for (const auto& kv : prof_env) env[kv.first] = kv.second;
    // Welke patches staan aan (legacy RG_LOADER_FPFIX zet de gedeelde lader-patch aan; de eigen lader zet hem weer uit).
    static const std::string kFpfix = "OpenParrotLoader zonder CONTEXT_FLOATING_POINT";
    std::map<std::string, bool> enabled;
    for (const auto& pt : settings.patches) enabled[pt.name] = pt.enabled();
    if ((legacy_fpfix_file || gsn::truthy(getenv_or(env, "RG_LOADER_FPFIX", "0"))) && enabled.count(kFpfix)) enabled[kFpfix] = true;
    // own_build(): [Loader] Build/Core/Path
    std::string core_dir = tp;
    {
        const gsn::Section* ls = settings.settings.section("Loader");
        auto lget = [&](const char* k) { return ls ? kv_get(ls->kv, k) : std::string(); };
        std::string build = lower(trim(lget("Build"))), core = lower(trim(lget("Core")));
        for (const auto& [key, val] : {std::pair<std::string, std::string>{"Build", build}, {"Core", core}})
            if (!val.empty() && val != "teknoparrot" && val != "ravengun") return setting_refused("[Loader] " + key + " = " + val + " (kies teknoparrot of ravengun)");
        if (core.empty() && build == "ravengun" && et == "OpenParrot") core = "ravengun";
        if (build == "ravengun" || core == "ravengun") {
            const std::string pth = trim(lget("Path"));
            const std::string base = expand_home(pth.empty() ? std::string("~/.local/share/ravengun/openparrot/current") : pth, p.home);
            std::string own_loader, own_core_rel;
            const bool op_loader = starts_with(lower(basename(exe_rel)), "openparrotloader");
            if (build == "ravengun") {
                if (!op_loader) return setting_refused("[Loader] Build = ravengun bestaat alleen voor OpenParrotLoader(64).exe, niet voor " + exe_rel);
                own_loader = pjoin(base, exe_rel);
            }
            if (core == "ravengun") {
                if ((et != "OpenParrot" && et != "TeknoParrot") || dll_rel.empty())
                    return setting_refused("een eigen kern bestaat alleen voor EmulatorType OpenParrot of TeknoParrot (dit is " + et + ")");
                if (!op_loader) return setting_refused("[Loader] Core = ravengun past alleen bij OpenParrotLoader(64).exe, niet bij " + exe_rel);
                own_core_rel = et == "OpenParrot" ? dll_rel : (is64 ? "OpenParrotx64/OpenParrot64" : "OpenParrotWin32/OpenParrot");
            }
            for (const std::string& f : {own_loader, own_core_rel.empty() ? std::string() : pjoin(base, own_core_rel + ".dll")})
                if (!f.empty() && !isfile(f)) return setting_refused("eigen OpenParrot-build ontbreekt: " + f + " (bouwen: scripts/build-openparrot.sh)");
            if (!own_loader.empty()) {
                loader = own_loader;
                if (enabled.count(kFpfix)) enabled[kFpfix] = false;   // de Wine-fix zit in de eigen lader zelf
            }
            if (!own_core_rel.empty()) {
                core_dir = base;
                dll_rel = own_core_rel;
            }
        }
    }
    if (!dll_rel.empty()) {
        pl.core_dll = pjoin(core_dir, dll_rel + ".dll");
        pl.core_source = core_dir == tp ? "runtime" : "ravengun";
    }
    // plan_file_patches(): een afgewezen patch die aan staat weigert; een ingeschakelde patch past de engine (nog) niet toe.
    for (const auto& e : settings.patch_errors)
        if (e.enabled) return patch_refused(e.message);
    for (const auto& e : settings.patch_errors) pl.patch_lines.push_back("overgeslagen '" + e.name + "' @- [afgewezen: " + e.message + "]");
    for (const auto& pt : settings.patches) {
        if (enabled[pt.name]) return engine("patch '" + pt.name + "' staat aan (de patchmotor draait alleen in launch-direct.py)");
        const std::string plek = pt.kind() == "memory" ? (pt.get("Module").empty() ? "?" : pt.get("Module")) + "+" + (pt.get("Offset").empty() ? "?" : pt.get("Offset"))
                                                       : (pt.get("Target").empty() ? "-" : pt.get("Target"));
        pl.patch_lines.push_back("overgeslagen '" + pt.name + "' @" + plek + " [uit]");
    }
    if (!trim(settings.get("Game", "ExeSha256")).empty()) return engine("[Game] ExeSha256 (de versiecontrole van de patchmotor)");

    // 3. Wine en de prefix.
    std::string wine = pr.get("WineRunnerPath");
    if (!(!wine.empty() && isfile(wine))) wine = isfile(getenv_or(env, "TP_WINE")) ? getenv_or(env, "TP_WINE") : p.home + "/.local/share/ravengun/wine/proton-11.0-2/bin/wine";
    {
        const std::string bindir = dirname(wine);
        if (basename(dirname(bindir)) == "files" && isfile(pjoin(dirname(dirname(bindir)), "proton")))
            return unsupported("Proton-runner (" + wine + ") wordt niet direct gestart; gebruik plain Wine");
    }
    std::string prefix;
    bool set_prefix = true;
    if (basename(wine) == "wine-prefix") {
        prefix = getenv_or(env, "RG_WINEPREFIX");
        if (prefix.empty()) return unsupported("WineRunnerPath is de wrapper wine-prefix maar RG_WINEPREFIX ontbreekt in " + name + ".env");
        set_prefix = false;
    } else if (!getenv_or(env, "TP_WINEPREFIX").empty()) {
        prefix = getenv_or(env, "TP_WINEPREFIX");
    } else {
        const std::string mode = pr.get("WinePrefixMode"), legacy = pjoin(prefixes_dir(p), name);
        prefix = mode == "Isolated" || (mode.empty() && isfile(pjoin(legacy, "system.reg"))) ? legacy : shared_prefix(p);
    }

    // 4. Wat de launcher zelf in de omgeving zet.
    const bool windowed = std::any_of(pr.fields.begin(), pr.fields.end(), [](const Field& f) {
        return (f.name == "Windowed" && f.value == "1") || (f.name == "DisplayMode" && f.value == "Windowed");
    });
    EnvMap launch;
    if (ld_truthy(pr.get("UseRemoteThread", "false"))) launch["TP_REMOTETHREAD"] = "1";
    {
        long long ms = 0;
        const std::string mt = pr.get("msysType", "0");
        if (py_int(mt.empty() ? "0" : mt, ms) && ms > 0) launch["tp_msysType"] = pr.get("msysType");
    }
    auto has_field_value = [&](const char* n, const char* v) {
        return std::any_of(pr.fields.begin(), pr.fields.end(), [&](const Field& f) { return f.name == n && f.value == v; });
    };
    if (et == "N2" || et == "ElfLdr2") {
        launch["tp_windowed"] = windowed ? "1" : "0";
        launch["TP_LOGTOFILE"] = (o.log_full || ld_truthy(pd.count("Elfldr2LogToFile") ? pd["Elfldr2LogToFile"] : "false")) ? "1" : "0";
        if (!pd["Elfldr2NetworkAdapterName"].empty()) launch["TP_ETH"] = pd["Elfldr2NetworkAdapterName"];
        if (const Field* m = pr.field("MSAA Level")) launch["TP_MSAA"] = m->value;
        if (emu == "Vt3Lindbergh") launch["TEA_DIR"] = wine_path(dirname(gamedir)) + "\\";
        else if (emu == "SegaJvsLetsGoJungle") launch["TEA_DIR"] = wine_path(gamedir) + "\\";
    } else if (et == "Lindbergh") {
        static const std::set<std::string> tea = {"SegaInitialDLindbergh", "Vf5Lindbergh", "Vf5cLindbergh", "SegaRtv", "SegaJvsLetsGoJungle", "Rambo",
                                                  "TooSpicy", "SegaRTuned", "GSEVO", "HummerExtreme"};
        if (windowed) launch["tp_windowed"] = "1";
        if (tea.count(emu)) launch["TEA_DIR"] = wine_path(gamedir) + "\\";
        else if (emu == "Vt3Lindbergh") launch["TEA_DIR"] = wine_path(dirname(gamedir)) + "\\";
        if (has_field_value("EnableAmdFix", "1")) {
            launch["tp_AMDCGGL"] = "1";
            if (emu == "SegaInitialDLindbergh") launch["tp_D4AMDFix"] = "1";
        }
        launch["REGAL_LOAD_GL"] = "opengl32.dll";
    }
    static const std::set<std::string> openssl = {"ALLSSWDC", "IDZ", "ALLSSCHRONO", "NxL2", "RawThrillsFNF", "ALLSHOTDSD", "ALLSFGO", "TimeCrisis5",
                                                  "JojoLastSurvivor", "DenshaDeGo", "ALLSIDTA", "SegaOlympic2020"};
    if (openssl.count(emu)) launch["OPENSSL_ia32cap"] = ":~0x20000000";
    if (set_prefix) launch["WINEPREFIX"] = prefix;
    if (!launch.count("TP_REMOTETHREAD")) launch["TP_REMOTETHREAD"] = getenv_or(env, "TP_REMOTETHREAD", "1");
    {
        const std::string rt = lower(launch["TP_REMOTETHREAD"]);
        if (rt == "off" || rt == "0" || rt == "no" || rt == "false" || rt.empty()) {
            launch.erase("TP_REMOTETHREAD");
            env.erase("TP_REMOTETHREAD");
        }
    }
    if (o.log_full) launch["WINEDEBUG"] = getenv_or(env, "RG_WINEDEBUG_FULL", "-all,+debugstr,err+module");
    else if (!env.count("WINEDEBUG")) launch["WINEDEBUG"] = "-all";
    launch["TP_LAUNCH_SESSION_ID"] = o.session_id.empty() ? new_uuid4() : o.session_id;
    for (const auto& kv : launch) env[kv.first] = kv.second;

    // 5. De opdrachtregel.
    const gsn::Section* launch_ini = settings.settings.section("Launch");
    auto lini = [&](const char* k) { return launch_ini ? kv_get(launch_ini->kv, k) : std::string(); };
    if (!trim(lini("Drives")).empty()) return engine("[Launch] Drives (schijfletters voor de run)");
    if (!trim(lini("Exe")).empty()) return engine("[Launch] Exe (het spel vanaf een ander pad)");
    const std::string spel_win = wine_path(game);
    std::vector<std::string> args{wine, loader};
    if (!dll_rel.empty()) args.push_back(wine_path(pjoin(core_dir, dll_rel)));
    args.push_back(spel_win);
    std::vector<std::string> profiel_delen;
    for (const char* veld : {"CustomArguments", "ExtraParameters"}) {
        const std::string extra = pr.get(veld);
        if (trim(extra).empty()) continue;
        std::string dbl;
        for (char c : extra) { dbl += c; if (c == '\\') dbl += '\\'; }
        bool ok = true;
        const auto delen = shlex_split(dbl, &ok);
        if (!ok) return engine(std::string(veld) + " is niet op te delen (geen sluitend aanhalingsteken)");
        profiel_delen.insert(profiel_delen.end(), delen.begin(), delen.end());
    }
    const std::vector<std::string> basis(args.begin(), args.end() - 1);   // wine, lader, kern: de tweede exe krijgt dezelfde
    {
        const auto spel_args = spel_argumenten(loader, profiel_delen, lini("Arguments"));
        args.insert(args.end(), spel_args.begin(), spel_args.end());
    }
    // De tweede exe krijgt dezelfde wine, dezelfde lader en dezelfde kern als de eerste: anders haakt de kern er niet aan.
    std::vector<std::string> args2;
    if (have_second) {
        std::vector<std::string> delen2;
        if (!trim(second.profile_args).empty()) {
            std::string dbl;
            for (char c : second.profile_args) { dbl += c; if (c == '\\') dbl += '\\'; }
            bool ok = true;
            delen2 = shlex_split(dbl, &ok);
            if (!ok) return engine("SecondExecutableArguments is niet op te delen (geen sluitend aanhalingsteken)");
        }
        args2 = basis;
        args2.push_back(wine_path(second.path));
        const auto a2 = spel_argumenten(loader, delen2, second.own_args);
        args2.insert(args2.end(), a2.begin(), a2.end());
    }
    std::vector<std::string> gamescope;
    if (getenv_or(env, "TP_GAMESCOPE") == "1" && getenv_or(env, "TP_NO_GAMESCOPE") != "1") {
        std::pair<int, int> wh{1920, 1080};
        static const std::regex res(R"(^(\d+)x(\d+)$)");
        std::smatch m;
        const std::string rv = getenv_or(env, "RG_GAMESCOPE_RES");
        if (std::regex_match(rv, m, res)) wh = {std::stoi(m[1]), std::stoi(m[2])};
        else wh = o.display_size ? o.display_size(env) : xrandr_size(env);
        std::string gs_bin = getenv_or(env, "TP_GAMESCOPE_PATH");
        if (gs_bin.empty()) gs_bin = "/usr/bin/gamescope";
        std::string backend = getenv_or(env, "TP_GAMESCOPE_BACKEND");
        if (backend.empty()) backend = getenv_or(env, "WAYLAND_DISPLAY").empty() ? "" : "wayland";
        gamescope = {gs_bin, "-W", std::to_string(wh.first), "-H", std::to_string(wh.second), "-S", "fit", "-f"};
        if (!backend.empty()) gamescope.insert(gamescope.end(), {"--backend", backend});
        gamescope.push_back("--");
    }
    const Field* wf = pr.field("Windowed");
    pl.windowed_override = !gamescope.empty() && pr.get("GamescopeGameWindowCompatibility") == "RequireWindowed" && wf && wf->value == "0";

    // 6. Bestanden naast de game.
    const std::string ini = build_ini(pr, pd, pl.windowed_override);
    pl.files.emplace_back(pjoin(gamedir, "teknoparrot.ini"), ini);
    if (et == "TeknoMacaw" && basename(gamedir) == "modules") pl.files.emplace_back(pjoin(dirname(gamedir), "teknoparrot.ini"), ini);
    {
        std::string tn, tt;
        if (taito_gun_ini(pr, tn, tt)) pl.files.emplace_back(pjoin(gamedir, tn), tt);
    }
    // De kern leest teknoparrot.ini naast de exe waarin hij geladen is, dus ook naast de tweede (TeknoParrotUi schrijft daar hetzelfde).
    if (have_second && dirname(second.path) != gamedir) pl.files.emplace_back(pjoin(dirname(second.path), "teknoparrot.ini"), ini);

    // 7. De JVS-brug en de procesnamen.
    static const std::set<std::string> no_pipe = {"EuropaRFordRacing", "EuropaRSegaRally3", "Theatrhythm", "FastIo", "GunslingerStratos3"};
    pl.pipe_mode = no_pipe.count(emu) ? "shm" : "pipe";
    pl.shm_source = o.shm_source.empty() ? "/dev/shm/TeknoParrot_JvsState" : o.shm_source;
    pl.pipehelper = p.home + "/.local/share/ravengun/jvsbridge/rg-jvsbridge.exe";
    if (!isfile(pl.pipehelper)) pl.pipehelper = pjoin(tp, "pipehelper.exe");
    if (!isfile(pl.pipehelper))
        return unsupported("JVS-brug ontbreekt: bouw rg-jvsbridge (scripts/build-jvsbridge.sh) of zorg voor pipehelper.exe in de runtime-map");
    // De besturingspipe ([Input] ControlPipe op route tp-shm): de daemon schrijft het rapport, rg-direct stroomt het naar de pipe.
    {
        DirectInput din;
        std::string iwhy;
        if (direct_input(name, p, din, iwhy) == Verdict::Ok && din.route == "tp-shm" && !din.control_pipe.empty()) {
            std::string perr;
            const iob::ControlPipeProfiles cps = iob::builtin_control_pipe_profiles(perr);
            const iob::ControlPipeProfile* cp = cps.find(din.control_pipe);
            if (!cp) return setting_refused("[Input] ControlPipe = " + din.control_pipe + " staat niet in packaging/control-pipe-profiles.json");
            pl.control_pipe = cp->name;
            pl.control_pipe_name = cp->pipe;
            pl.control_source = o.control_source.empty() ? std::string(iob::kDefaultControlPipePath) : o.control_source;
            pl.control_size = cp->size;
            pl.control_interval_ms = cp->interval_ms;
        }
    }
    std::set<std::string> comms = {"budgieloader.ex", "budgieloader_x6", "openparrotloade", "teknomacaw.exe", "teknomacaw64.ex"};
    for (const char* key : {"ExecutableName", "GamePath"})
        for (std::string part : split(pr.get(key), ';')) {
            std::replace(part.begin(), part.end(), '\\', '/');
            std::string b = lower(basename(part));
            if (b.size() > 15) b.resize(15);
            if (!b.empty()) comms.insert(b);
        }
    pl.game_comms.assign(comms.begin(), comms.end());
    // De starts in de orde waarin ze gebeuren. De tweede exe draait buiten gamescope (hij heeft geen venster), in dezelfde werkmap,
    // prefix en omgeving als de eerste.
    {
        DirectStart hoofd;
        hoofd.name = basename(game);
        hoofd.role = "main";
        hoofd.exe = game;
        hoofd.argv = gamescope;
        hoofd.argv.insert(hoofd.argv.end(), args.begin(), args.end());
        hoofd.cwd = cwd;
        hoofd.gamescope = !gamescope.empty();
        hoofd.comms = exe_comms({pr.get("ExecutableName"), game});
        if (!have_second) {
            pl.starts = {hoofd};
        } else {
            DirectStart tweede;
            tweede.name = basename(second.path);
            tweede.role = "second";
            tweede.exe = second.path;
            tweede.argv = args2;
            tweede.cwd = cwd;
            tweede.comms = exe_comms({pr.get("ExecutableName2"), second.path});
            pl.starts = second.first ? std::vector<DirectStart>{tweede, hoofd} : std::vector<DirectStart>{hoofd, tweede};
            pl.starts[1].wait_before = second.wait;
        }
    }

    pl.emulator_type = et;
    pl.emulation_profile = emu;
    pl.is64 = is64;
    pl.game = game;
    pl.cwd = cwd;
    pl.wine = wine;
    pl.prefix = prefix;
    pl.loader = loader;
    pl.gamescope = !gamescope.empty();
    pl.windows = is_pe(game);
    pl.argv = gamescope;
    pl.argv.insert(pl.argv.end(), args.begin(), args.end());
    pl.env = env;
    pl.launch_env = launch;

    // 8. Drop-ins: een fout is een weigering (launch-direct.py dropin_weiger, exit 5).
    const DropinResult dr = dropin_plan(pl, p);
    if (!dr.errors.empty()) {
        std::string m;
        for (const auto& e : dr.errors) m += (m.empty() ? "" : "\n") + std::string("Drop-in geweigerd: ") + e;
        why = m;
        return Verdict::Refused;
    }
    merge_env(pl, dr.env);
    return Verdict::Ok;
}

bool needs_gst_fetch(const DirectPlan& pl) {
    const std::string gst = getenv_or(pl.env, "GST_PLUGIN_PATH");
    return !gst.empty() && !isfile(pjoin(gst, "libgstasf.so"));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Plaatsen (run(): dropin_materialiseer, place_binary, place_file)

namespace {

bool maak_map_lokaal(const std::string& pad, std::string& err) {
    if (!islink(pad)) return true;
    const std::string doel = realpath_(pad), tmp = pad + ".ravengun-tmp";
    std::error_code ec;
    if (lexists(tmp)) fs::remove_all(tmp, ec);
    if (::mkdir(tmp.c_str(), 0777) != 0) { err = tmp + ": " + std::strerror(errno); return false; }
    for (fs::directory_iterator it(doel, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (::symlink(pjoin(doel, n).c_str(), pjoin(tmp, n).c_str()) != 0) { err = pjoin(tmp, n) + ": " + std::strerror(errno); return false; }
    }
    if (ec) { err = doel + ": " + ec.message(); return false; }
    if (::unlink(pad.c_str()) != 0 || ::rename(tmp.c_str(), pad.c_str()) != 0) { err = pad + ": " + std::strerror(errno); return false; }
    return true;
}

bool dropin_materialiseer(const std::string& base, const std::string& dest_in, const std::string& wortel_in, std::vector<std::string>& log, std::string& msg) {
    const std::string dest = abspath(dest_in);
    std::string wortel;
    std::vector<std::string> delen;
    if (!dropin_keten(base, dest, wortel_in, wortel, delen, msg)) return false;
    std::string cur = wortel;
    for (const std::string& deel : delen) {
        cur = pjoin(cur, deel);
        if (islink(cur)) {
            const std::string doel = realpath_(cur);
            if (!isdir(doel)) { msg = cur + " is een symlink naar iets dat geen map is"; return false; }
            std::string e;
            if (!maak_map_lokaal(cur, e)) { msg = e; return false; }
            log.push_back(relpath(cur, wortel) + " was een symlink naar de NAS; vervangen door een echte map met symlinks");
        } else if (lexists(cur) && !isdir(cur)) {
            msg = cur + " is een bestand, geen map";
            return false;
        } else if (!isdir(cur)) {
            std::error_code ec;
            fs::create_directories(cur, ec);
            if (ec) { msg = cur + ": " + ec.message(); return false; }
        }
    }
    if (islink(dest)) ::unlink(dest.c_str());
    const std::string anker = wortel != abspath(base) ? pjoin(realpath_(dirname(wortel)), basename(wortel)) : wortel;
    if (!binnen(realpath_(dirname(dest)), anker)) { msg = "de map van het doel " + dest + " komt na het materialiseren nog buiten de view uit"; return false; }
    msg = "de map van " + relpath(dest, wortel) + " is lokaal";
    return true;
}

bool write_atomic(const std::string& path, const std::string& data, std::string& err) {
    const std::string tmp = path + ".ravengun-tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << data) || !f.flush()) { err = tmp + ": niet te schrijven"; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { err = path + ": " + std::strerror(errno); return false; }
    return true;
}

}  // namespace

bool apply_direct(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    for (const std::string& l : pl.dropin_lines) log.push_back("drop-in: " + l);
    for (const DropinFile& f : pl.dropin_files) {
        std::string msg;
        if (!dropin_materialiseer(pl.dropin_base, f.dest, pl.dropin_root, log, msg)) {
            log.push_back("drop-in: FOUT: " + msg + " -- de game wordt niet gestart");
            err = "drop-in " + f.name + ": " + msg;
            return false;
        }
        // place_binary()
        std::string data;
        if (!read_file(f.src, data)) {
            log.push_back("drop-in: " + f.src + " is niet te lezen");
            err = "drop-in " + f.name + " is niet geplaatst";
            return false;
        }
        if (on_nas(f.dest, p)) {
            log.push_back("drop-in: " + f.dest + " ligt op de NAS; niet geschreven. Maak eerst een lokale view (scripts/tp-add-game.py <Game> <pad> --view)");
            err = "drop-in " + f.name + " is niet geplaatst (NAS)";
            return false;
        }
        std::string cur;
        if (isfile(f.dest) && read_file(f.dest, cur) && cur == data) {
            log.push_back("drop-in: " + f.dest + " is al actueel");
            continue;
        }
        std::error_code ec;
        fs::create_directories(dirname(f.dest), ec);
        if (islink(f.dest)) ::unlink(f.dest.c_str());
        std::string werr;
        if (!write_atomic(f.dest, data, werr)) {
            log.push_back("drop-in: " + f.dest + " niet te schrijven: " + werr);
            err = "drop-in " + f.name + " is niet geplaatst";
            return false;
        }
        ::chmod(f.dest.c_str(), 0755);
        log.push_back("drop-in: " + f.dest + " geschreven (" + std::to_string(data.size()) + " bytes)");
    }
    for (const auto& [path, text] : pl.files) {   // place_file()
        std::string cur;
        const bool have = read_file(path, cur);
        if (have && cur == text) { log.push_back("bestand: " + path + " is al actueel"); continue; }
        if (on_nas(path, p)) {
            log.push_back("bestand: " + path + " ligt op de NAS en " + (have ? "wijkt af" : "ontbreekt") + "; niet geschreven (de game gebruikt het bestaande bestand)");
            continue;
        }
        if (islink(path)) ::unlink(path.c_str());
        std::string werr;
        if (!write_atomic(path, text, werr)) { err = "bestand " + path + ": " + werr; return false; }
        log.push_back("bestand: " + path + " geschreven");
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// JSON voor rg-direct

std::string plan_to_json(const DirectPlan& pl) {
    json::Writer w;
    auto strs = [&](const char* k, const std::vector<std::string>& v) {
        w.key(k).begin_array();
        for (const auto& s : v) w.value(s);
        w.end_array();
    };
    auto map = [&](const char* k, const EnvMap& m) {
        w.key(k).begin_object();
        for (const auto& [a, b] : m) w.key(a).value(b);
        w.end_object();
    };
    w.begin_object();
    for (const auto& [k, v] : std::initializer_list<std::pair<const char*, const std::string*>>{
             {"profile", &pl.profile}, {"tp", &pl.tp}, {"emulator_type", &pl.emulator_type}, {"emulation_profile", &pl.emulation_profile},
             {"game", &pl.game}, {"game_source", &pl.game_source}, {"cwd", &pl.cwd}, {"wine", &pl.wine}, {"prefix", &pl.prefix},
             {"loader", &pl.loader}, {"pipe_mode", &pl.pipe_mode}, {"shm_source", &pl.shm_source}, {"pipehelper", &pl.pipehelper},
             {"core_dll", &pl.core_dll}, {"core_source", &pl.core_source}, {"state", &pl.state}, {"dropin_base", &pl.dropin_base},
             {"dropin_root", &pl.dropin_root}, {"control_pipe", &pl.control_pipe}, {"control_pipe_name", &pl.control_pipe_name},
             {"control_source", &pl.control_source}})
        w.key(k).value(*v);
    w.key("control_size").value(pl.control_size).key("control_interval_ms").value(pl.control_interval_ms);
    w.key("is64").value(pl.is64).key("gamescope").value(pl.gamescope).key("windowed_override").value(pl.windowed_override);
    w.key("log_full").value(pl.log_full).key("windows").value(pl.windows);
    strs("argv", pl.argv);
    map("env", pl.env);
    map("launch_env", pl.launch_env);
    w.key("files").begin_array();
    for (const auto& [a, b] : pl.files) w.begin_array().value(a).value(b).end_array();
    w.end_array();
    w.key("dropin_files").begin_array();
    for (const auto& f : pl.dropin_files) w.begin_object().key("src").value(f.src).key("dest").value(f.dest).key("name").value(f.name).end_object();
    w.end_array();
    strs("dropin_materialise", pl.dropin_materialise);
    strs("dropin_lines", pl.dropin_lines);
    strs("game_comms", pl.game_comms);
    strs("patch_lines", pl.patch_lines);
    strs("notes", pl.notes);
    w.key("starts").begin_array();
    for (const DirectStart& s : pl.starts) {
        w.begin_object().key("name").value(s.name).key("role").value(s.role).key("exe").value(s.exe).key("cwd").value(s.cwd);
        strs("argv", s.argv);
        strs("comms", s.comms);
        w.key("gamescope").value(s.gamescope).key("wait_before").value(s.wait_before).end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

bool plan_from_json(const std::string& text, DirectPlan& pl, std::string& err) {
    const auto r = json::parse(text);
    if (!r.ok || !r.value.is_object()) { err = "plan: geen JSON-object (" + r.error + ")"; return false; }
    const json::Value& v = r.value;
    pl = DirectPlan{};
    auto s = [&](const char* k, std::string& out) { const auto* x = v.find(k); if (x && x->is_string()) out = x->str; };
    auto b = [&](const char* k, bool& out) { const auto* x = v.find(k); if (x && x->is_bool()) out = x->b; };
    auto sv = [&](const char* k, std::vector<std::string>& out) {
        const auto* x = v.find(k);
        if (x && x->is_array()) for (const auto& e : x->arr) if (e.is_string()) out.push_back(e.str);
    };
    auto mp = [&](const char* k, EnvMap& out) {
        const auto* x = v.find(k);
        if (x && x->is_object()) for (const auto& [a, e] : x->obj) if (e.is_string()) out[a] = e.str;
    };
    s("profile", pl.profile); s("tp", pl.tp); s("emulator_type", pl.emulator_type); s("emulation_profile", pl.emulation_profile);
    s("game", pl.game); s("game_source", pl.game_source); s("cwd", pl.cwd); s("wine", pl.wine); s("prefix", pl.prefix); s("loader", pl.loader);
    s("pipe_mode", pl.pipe_mode); s("shm_source", pl.shm_source); s("pipehelper", pl.pipehelper); s("core_dll", pl.core_dll);
    s("core_source", pl.core_source); s("state", pl.state); s("dropin_base", pl.dropin_base); s("dropin_root", pl.dropin_root);
    s("control_pipe", pl.control_pipe); s("control_pipe_name", pl.control_pipe_name); s("control_source", pl.control_source);
    auto n = [&](const char* k, int& out) { const auto* x = v.find(k); if (x && x->is_number()) out = static_cast<int>(x->num); };
    n("control_size", pl.control_size); n("control_interval_ms", pl.control_interval_ms);
    b("is64", pl.is64); b("gamescope", pl.gamescope); b("windowed_override", pl.windowed_override); b("log_full", pl.log_full); b("windows", pl.windows);
    sv("argv", pl.argv); sv("dropin_materialise", pl.dropin_materialise); sv("dropin_lines", pl.dropin_lines); sv("game_comms", pl.game_comms);
    sv("patch_lines", pl.patch_lines); sv("notes", pl.notes);
    mp("env", pl.env); mp("launch_env", pl.launch_env);
    if (const auto* f = v.find("files"); f && f->is_array())
        for (const auto& e : f->arr)
            if (e.is_array() && e.arr.size() == 2 && e.arr[0].is_string() && e.arr[1].is_string()) pl.files.emplace_back(e.arr[0].str, e.arr[1].str);
    if (const auto* f = v.find("dropin_files"); f && f->is_array())
        for (const auto& e : f->arr) {
            DropinFile d;
            if (const auto* x = e.find("src"); x && x->is_string()) d.src = x->str;
            if (const auto* x = e.find("dest"); x && x->is_string()) d.dest = x->str;
            if (const auto* x = e.find("name"); x && x->is_string()) d.name = x->str;
            pl.dropin_files.push_back(d);
        }
    if (const auto* f = v.find("starts"); f && f->is_array())
        for (const auto& e : f->arr) {
            if (!e.is_object()) continue;
            DirectStart d;
            auto es = [&](const char* k, std::string& out) { const auto* x = e.find(k); if (x && x->is_string()) out = x->str; };
            auto ev = [&](const char* k, std::vector<std::string>& out) {
                const auto* x = e.find(k);
                if (x && x->is_array()) for (const auto& y : x->arr) if (y.is_string()) out.push_back(y.str);
            };
            es("name", d.name); es("role", d.role); es("exe", d.exe); es("cwd", d.cwd);
            ev("argv", d.argv); ev("comms", d.comms);
            if (const auto* x = e.find("gamescope"); x && x->is_bool()) d.gamescope = x->b;
            if (const auto* x = e.find("wait_before"); x && x->is_number()) d.wait_before = x->num;
            if (d.argv.empty() || d.cwd.empty()) { err = "plan: een start zonder argv of cwd"; return false; }
            pl.starts.push_back(std::move(d));
        }
    if (pl.argv.empty() || pl.cwd.empty() || pl.wine.empty() || pl.pipehelper.empty()) { err = "plan: argv, cwd, wine of pipehelper ontbreekt"; return false; }
    if (!pl.control_pipe.empty() && (pl.control_pipe_name.empty() || pl.control_source.empty() || pl.control_size < 1 || pl.control_size > 4096 ||
                                     pl.control_interval_ms < 1 || pl.control_interval_ms > 1000)) {
        err = "plan: besturingspipe zonder naam, bron, geldige grootte of tempo";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// De invoer (play-halo.sh) en de omgeving van de game (gamesettings.py env)

std::string launch_mode(const std::string& profile, const DirectPaths& p) {
    const gsn::Game g = load_for_profile(profile, p, nullptr, nullptr);
    const std::string m = lower(trim(g.get("Launch", "Mode")));
    if (m == "direct") return "direct";
    if (m == "tpui" || m == "teknoparrot-ui") return "teknoparrot-ui";
    return {};
}

EnvMap profile_env(const std::string& profile, const DirectPaths& p) {
    const gsn::Game g = load_for_profile(profile, p, nullptr, nullptr);
    return to_env(g.settings, profile_gamedir(profile, p), p);
}

Verdict direct_input(const std::string& profile, const DirectPaths& p, DirectInput& out, std::string& why) {
    out = DirectInput{};
    auto check = [](const gsn::Ini& s, std::string& err) {
        const std::string r = s.get("Input", "Route", "evdev").empty() ? "evdev" : s.get("Input", "Route", "evdev");
        if (r != "evdev" && r != "tp-shm" && r != "unity-tcp" && r != "hid-board" && r != "jvs") { err = "ongeldige Route '" + r + "'"; return false; }
        if (r == "tp-shm" && s.get("Input", "Layout").empty()) { err = "route tp-shm vraagt een Layout"; return false; }
        if (!s.get("Input", "ControlPipe").empty() && r != "tp-shm" && r != "evdev") { err = "ControlPipe hoort bij route tp-shm"; return false; }
        if (r == "hid-board" && s.get("Input", "Board").empty()) { err = "route hid-board vraagt een Board (bordnaam)"; return false; }
        if (r == "jvs" && s.get("Input", "Board").empty() && s.get("Input", "Boards").empty()) { err = "route jvs vraagt een Board of Boards"; return false; }
        return true;
    };
    gsn::Game g = load_for_profile(profile, p, nullptr, nullptr);
    std::string err;
    gsn::Ini input;
    if (check(g.settings, err)) {
        input = g.settings;
    } else {
        gsn::Dirs shipped_only{p.dirs.shipped, ""};
        g = load_for_profile(profile, p, nullptr, nullptr, &shipped_only);
        if (check(g.settings, err)) input = g.settings;
        else input.set("Input", "Route", "evdev");
    }
    out.route = input.get("Input", "Route", "evdev");
    if (out.route.empty()) out.route = "evdev";
    out.layout = input.get("Input", "Layout");
    out.board = input.get("Input", "Board");
    if (out.board.empty()) out.board = trim(split(input.get("Input", "Boards"), ',')[0]);
    out.control_pipe = input.get("Input", "ControlPipe");
    out.com_port = input.get("Input", "ComPort");
    if (out.com_port.empty()) out.com_port = "COM4";
    out.transport = getenv_or(profile_env(profile, p), "RG_JVS_TRANSPORT");
    if (out.route == "evdev") {
        if (out.layout.empty()) { why = "route evdev zonder tp-shm-indeling"; return Verdict::Fallback; }
        out.route = "tp-shm";
        out.note = "Directe start: invoer via tp-shm (zonder TeknoParrotUi leest niemand evdev).";
    }
    if (out.route == "unity-tcp") { why = "nog niet in de engine: invoerroute unity-tcp bij een directe start"; return Verdict::Fallback; }
    if (out.route == "jvs" && out.transport != "pipe") {
        why = "nog niet in de engine: jvs over een COM-poort (wineboot, dosdevices en de LD_PRELOAD-hulp)";
        return Verdict::Fallback;
    }
    return Verdict::Ok;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// hid-board: joystick-id 0 (scripts/wine-joyid.sh)

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
