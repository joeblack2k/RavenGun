// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10): de instellingen en het profiel (gamesettings.py to_env/env_to_settings/load_for_profile, ownprofile.py
// profile_from_settings), teknoparrot.ini en de geweer-ini van Taito, en de invoerroute. Zie direct.hpp.
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/direct_internal.hpp"
#include "ravengun/launch/gamefind.hpp"
#include "ravengun/launch/gameview.hpp"

namespace ravengun::launch {

namespace direct_detail {

// --- gamesettings.py: het oude .env, to_env, env_to_settings -----------------------------------------------------------------------
std::string prefixes_dir(const DirectPaths& p) { return p.data + "/prefixes"; }
std::string shared_prefix(const DirectPaths& p) { return prefixes_dir(p) + "/shared/wine"; }

namespace {

std::string prefix_path(std::string v, const DirectPaths& p) {
    v = trim(v);
    if (v == "shared") return shared_prefix(p);
    if (v.starts_with("/") || v.starts_with("~")) return expand_home(v, p.home);
    return pjoin(prefixes_dir(p), v);
}
std::string prefix_value(const std::string& path, const DirectPaths& p) {
    if (path == shared_prefix(p)) return "shared";
    const std::string pre = prefixes_dir(p) + "/";
    if (path.starts_with(pre)) {
        const std::string rest = path.substr(pre.size());
        if (rest.find('/') == std::string::npos && rest != "shared") return rest;
    }
    return shrink_home(path, p.home);
}

bool is_drive_path(const std::string& v) {
    return v.size() >= 3 && std::isalpha(static_cast<unsigned char>(v[0])) && v[1] == ':' && (v[2] == '\\' || v[2] == '/');
}

}  // namespace

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
            if (!(is_drive_path(v) || v.starts_with("/"))) {
                if (gamedir.empty()) continue;
                v = wine_path(normpath(pjoin(gamedir, v)));
            } else if (v.starts_with("/")) {
                v = wine_path(v);
            }
        }
        env[m.var] = v;
    }
    if (const gsn::Section* e = s.section("Env"))
        for (const auto& kv : e->kv) env[kv.first] = kv.second;
    return env;
}

namespace {

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
            if (!gamedir.empty() && val.starts_with("Z:")) {
                std::string posix = val.substr(2);
                std::replace(posix.begin(), posix.end(), '\\', '/');
                std::string gd = gamedir;
                while (gd.size() > 1 && gd.back() == '/') gd.pop_back();
                if (posix == gamedir || posix.starts_with(gd + "/")) {
                    const std::string rel = relpath(posix, gamedir);
                    gsn::Ini probe;
                    probe.set("Launch", "WorkingDirectory", rel);
                    if (getenv_or(to_env(probe, gamedir, p), var) == val) out = rel;
                }
            }
            if (out == val) out = val.starts_with("/") ? shrink_home(val, p.home) : val;
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

}  // namespace

std::string locate(const std::string& profile, const gsn::Ini& settings, const DirectPaths& p, std::string* how) {
    std::string ov = trim(settings.get("Game", "GamePath"));
    if (!ov.empty()) {
        if (how) *how = "override";
        return expand_home(ov, p.home);
    }
    // [Launch] View = off: nooit een view, ook niet een die er al staat (Dream Raiders: vanuit de view "SOUND FUNCTION NOT FOUND",
    // vanuit de gamemap niet; gemeten 2026-10-16). Een ongeldige waarde weigert plan_view later met een melding.
    ViewSetting vs;
    std::string verr;
    const bool no_view = parse_view_setting(settings.get("Launch", "View"), settings.get("Launch", "ViewEmptyDirs"), vs, verr) &&
                         vs.mode == ViewSetting::Mode::Off;
    // De sha256 uit de cache (per pad, grootte en mtime), library.json als hint voor het zoeken: een start hasht de NAS niet opnieuw.
    return gsn::locate_game(profile, settings, library_folders(p), no_view ? std::string() : p.views(), p.home, how,
                            [](const std::string& f) { return default_hash_cache().sha256(f); },
                            library_roots(p.library_file, profile, gsn::identify_of(settings).path, p.home));
}

namespace {

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

}  // namespace

// gamesettings.load_for_profile(): de lagen met het oude UserProfiles/<P>.env eronder.
// Een game die de TeknoParrot-map niet nodig heeft (tp_free_settings: eigen lader en kern, of geen lader) leest niets uit die map:
// geen UserProfiles/<P>.xml (alleen een hint voor [Game] Type, dat zo'n game.ini zelf heeft) en geen oud .env. Gemeten 2026-10-17
// met rg-direct --plan: voor Gaia Attack 4, Haunted Museum I en II, Music GunGun 2 en Luigi's Mansion (de eigen-kern-games met
// een .env in de runtime) is het plan met en zonder dat .env gelijk op de logregel na; game.ini had alles al.
gsn::Game load_for_profile(const std::string& profile, const DirectPaths& p, std::vector<std::string>* notes, bool* legacy_fpfix,
                           const gsn::Dirs* dirs_override) {
    const gsn::Dirs& dirs = dirs_override ? *dirs_override : p.dirs;
    if (gsn::Game eigen = gsn::load_game(dirs, profile); tp_free_settings(eigen.settings)) return eigen;
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
            if (notes) notes->push_back("legacy .env gelezen: " + path + " (game.ini wint; migreer met tools/dev/migrate-game-settings.py)");
        }
    }
    return gsn::load_game(dirs, profile, hint, have_legacy ? &legacy : nullptr);
}

// --- het profiel (ownprofile.profile_from_settings) ---------------------------------------------------------------------------------
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

}  // namespace direct_detail

using namespace direct_detail;

bool tp_free_settings(const gsn::Ini& s) {
    if (!gsn::has_own_profile(s)) return false;
    const std::string build = lower(trim(s.get("Loader", "Build"))), core = lower(trim(s.get("Loader", "Core"))), type = trim(s.get("Game", "Type"));
    if (core == "none") return true;   // Wine start de exe zelf
    // Een eigen lader als drop-in ([Loader] Exe) bij een lader zonder kern-DLL (TeknoMacaw: Silent Scope: Bone Eater, rg-macaw.exe).
    if (!trim(s.get("Loader", "Exe")).empty() && type == "TeknoMacaw") return true;
    if (build != "ravengun" || (type != "OpenParrot" && type != "TeknoParrot")) return false;   // de lader van TeknoParrot
    return core == "ravengun" || (core.empty() && type == "OpenParrot");                     // eigen kern (OpenParrot: volgt uit Build)
}

// ---------------------------------------------------------------------------------------------------------------------------------
// De invoer en de omgeving van de game (gelijk aan gamesettings.py env)

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

}  // namespace ravengun::launch
