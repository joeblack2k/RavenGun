#include "ravengun/launch/unity.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>

#include "ravengun/json_parse.hpp"
#include "ravengun/launch/native.hpp"
#include "ravengun/strutil.hpp"

namespace fs = std::filesystem;

namespace ravengun::launch {
namespace {

using json::Value;

bool lexists(const std::string& p) { struct stat st{}; return ::lstat(p.c_str(), &st) == 0; }
bool is_link(const std::string& p) { struct stat st{}; return ::lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode); }
bool is_dir(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool is_file(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
long long size_of(const std::string& p) { struct stat st{}; return ::stat(p.c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : -1; }
std::string join(const std::string& a, const std::string& b) { return a + "/" + b; }

// De vermeldingen van een map, gesorteerd (zoals sorted(os.listdir()) in het script); leeg bij een fout.
std::vector<std::string> list_sorted(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool mkdirs(const std::string& p, std::string& err) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec && !is_dir(p)) { err = "map maken mislukt: " + p + ": " + ec.message(); return false; }
    return true;
}

// Kopieert een bestand. Staat er op `d` een symlink, dan gaat eerst alleen die link weg: nooit schrijven via een symlink.
// keep_mode: de rechten van de bron mee (shutil.copy2/copymode); anders alleen de inhoud (shutil.copyfile: een nieuw bestand
// krijgt de gewone rechten, een bestaand houdt de zijne).
bool copy_one(const std::string& s, const std::string& d, std::string& err, bool keep_mode = false) {
    if (is_link(d)) ::unlink(d.c_str());
    if (keep_mode) {
        std::error_code ec;
        fs::copy_file(s, d, fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "kopieren mislukt: " + s + " -> " + d + ": " + ec.message(); return false; }
        return true;
    }
    std::ifstream in(s, std::ios::binary);
    if (!in) { err = "kopieren mislukt: " + s + " is niet te lezen"; return false; }
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());   // ook een leeg bestand
    std::ofstream out(d, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) { err = "kopieren mislukt: " + s + " -> " + d; return false; }
    return true;
}

// Een hele map (recursief) kopieren, bestaande bestanden overschrijven (shutil.copytree(..., dirs_exist_ok=True)).
bool copy_tree(const std::string& s, const std::string& d, std::string& err) {
    if (!mkdirs(d, err)) return false;
    for (const std::string& e : list_sorted(s)) {
        const std::string ss = join(s, e), dd = join(d, e);
        if (is_dir(ss)) { if (!copy_tree(ss, dd, err)) return false; }
        else if (!copy_one(ss, dd, err, true)) return false;
    }
    return true;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string t = ss.str();
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB &&
        static_cast<unsigned char>(t[2]) == 0xBF)
        t.erase(0, 3);  // utf-8-sig
    return t;
}
bool write_file(const std::string& path, const std::string& text, std::string& err) {
    if (is_link(path)) ::unlink(path.c_str());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) { err = "schrijven mislukt: " + path; return false; }
    return true;
}

std::vector<std::string> split_lines(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : t) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
std::string join_lines(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? "\n" : "") + v[i];
    return out;
}

// Map `dst` als ECHTE map met echte kopieen van `src` (recursief, eenmalig); bestaande lokale bestanden blijven staan, een
// symlink naar de NAS op die plek wordt vervangen. Komt `dst` toch op de NAS uit, dan een fout (kopieer_lokaal in het script).
bool copy_local(const std::string& src, const std::string& dst, const std::string& nas, std::string& err) {
    if (is_link(dst)) ::unlink(dst.c_str());   // alleen de link zelf, nooit wat erachter staat
    if (!mkdirs(dst, err)) return false;
    if (path_under(dst, nas)) { err = dst + " komt op de NAS uit; daar wordt nooit geschreven"; return false; }
    for (const std::string& e : list_sorted(src)) {
        const std::string s = join(src, e), d = join(dst, e);
        if (is_link(d) && path_under(d, nas)) ::unlink(d.c_str());
        if (is_dir(s)) { if (!copy_local(s, d, nas, err)) return false; }
        else if (!lexists(d)) { if (!copy_one(s, d, err, true)) return false; }
    }
    return true;
}

// Symlink elke vermelding van `src` in `dst` (bestaande links worden vervangen, echte bestanden blijven). Mappen in
// `local_dirs` (kleine letters) worden lokaal gekopieerd; met `copy_small` worden lege bestanden en kleine configuratie
// (extensie uit `copy_ext`, minder dan 64 KiB) gekopieerd en .log-bestanden overgeslagen (link_all in het script).
bool link_all(const std::string& src, const std::string& dst, const std::set<std::string>& skip, bool copy_small,
              const std::set<std::string>& local_dirs, const std::vector<std::string>& copy_ext, std::string& err) {
    if (!mkdirs(dst, err)) return false;
    for (const std::string& e : list_sorted(dst))
        if (is_link(join(dst, e))) ::unlink(join(dst, e).c_str());
    for (const std::string& e : list_sorted(src)) {
        const std::string le = lower(e);
        if (skip.count(le)) continue;
        const std::string s = join(src, e), d = join(dst, e);
        if (local_dirs.count(le) && is_dir(s)) {
            if (!copy_local(s, d, src, err)) return false;
            continue;
        }
        if (lexists(d)) continue;
        if (copy_small && le.ends_with(".log")) continue;   // logbestanden van de game: nieuwe ontstaan lokaal
        bool small = false;
        if (copy_small && is_file(s)) {
            const long long sz = size_of(s);
            bool ext = false;
            for (const std::string& x : copy_ext) ext = ext || le.ends_with(x);
            small = sz == 0 || (ext && sz < 64 * 1024);
        }
        if (small) {
            if (!copy_one(s, d, err)) return false;   // lege vlagbestanden en kleine configuratie: lokaal, de game schrijft erin
        } else if (::symlink(s.c_str(), d.c_str()) != 0) {
            err = "symlink maken mislukt: " + d;
            return false;
        }
    }
    return true;
}

// BepInEx schrijft standaard geen logbestand: aanzetten (BepInEx/LogOutput.log).
bool enable_disk_log(const std::string& view, std::string& err) {
    const std::string cfg = view + "/BepInEx/config/BepInEx.cfg";
    if (!mkdirs(view + "/BepInEx/config", err)) return false;
    if (!is_file(cfg)) return write_file(cfg, "[Logging.Disk]\n\nEnabled = true\n", err);
    std::vector<std::string> lines = split_lines(read_file(cfg));
    bool in_section = false, changed = false;
    for (std::string& l : lines) {
        if (!l.empty() && l[0] == '[') { in_section = l.rfind("[Logging.Disk]", 0) == 0; continue; }
        if (!in_section || l.rfind("Enabled", 0) != 0) continue;
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string v = l.substr(eq + 1);
        v.erase(0, v.find_first_not_of(" \t"));
        if (v.rfind("false", 0) == 0) { l = l.substr(0, l.size() - v.size()) + "true" + v.substr(5); changed = true; }
        break;   // alleen de eerste Enabled van de sectie
    }
    return changed ? write_file(cfg, join_lines(lines), err) : true;
}

// Het ini-sjabloon van de DemulShooter-plugin met de resolutie uit het JSON-bestand (plugin_ini in het script).
std::string plugin_ini(const std::string& tmpl, const UnityGame& g) {
    std::vector<std::pair<std::string, std::string>> keys = {
        {"FORCE_RESOLUTION", "1"}, {"WIDTH", std::to_string(g.width)}, {"HEIGHT", std::to_string(g.height)}, {"FULLSCREEN", "1"}};
    if (!g.language.empty()) keys.push_back({"LANGUAGE", g.language});
    std::vector<std::string> lines = split_lines(tmpl);
    for (std::string& l : lines) {
        std::string body = l, cr;
        if (!body.empty() && body.back() == '\r') { body.pop_back(); cr = "\r"; }
        for (const auto& [k, v] : keys)
            if (body.rfind(k + "=", 0) == 0) { l = k + "=" + v + cr; break; }
    }
    return join_lines(lines);
}

// Nieuwste wijzigingstijd van een .cs-bestand onder `dir` (0 = geen).
long long newest_cs(const std::string& dir) {
    long long best = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".cs") continue;
        struct stat st{};
        if (::stat(it->path().c_str(), &st) == 0) best = std::max<long long>(best, st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec);
    }
    return best;
}

bool remove_plugins(const std::string& dir, const std::string& part, bool prefix) {
    for (const std::string& e : list_sorted(dir)) {
        const bool hit = prefix ? e.rfind(part, 0) == 0 : e.find(part) != std::string::npos;
        if (hit && !is_dir(join(dir, e))) ::unlink(join(dir, e).c_str());
    }
    return true;
}

bool install_ravengun(const std::string& view, const UnityGame& g, const UnityPaths& p, std::vector<std::string>& log, std::string& err) {
    const std::string bep = p.data + "/bepinex/" + g.bepinex_version + "/x64";
    if (!is_file(bep + "/BepInEx/core/BepInEx.dll")) {
        err = "BepInEx " + g.bepinex_version + " ontbreekt in " + bep + ": zie plugins/unity/README.md (download van github.com/BepInEx)";
        return false;
    }
    const std::string name = "RavenGun." + g.key + ".dll";
    std::string dll;
    std::string searched;
    for (const std::string& d : p.plugin_dirs) {
        if (d.empty()) continue;
        searched += (searched.empty() ? "" : ", ") + d;
        if (is_file(d + "/" + g.key + "/" + name)) { dll = d + "/" + g.key + "/" + name; break; }
    }
    if (dll.empty()) {
        err = "de plugin " + name + " ontbreekt (gezocht in " + searched + "); bouw hem met scripts/build-unity-plugin.sh " + g.key;
        return false;
    }
    if (!p.plugin_src.empty() && is_dir(p.plugin_src)) {
        struct stat st{};
        ::stat(dll.c_str(), &st);
        if (newest_cs(p.plugin_src) > st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec) {
            err = dll + " is verouderd (een bronbestand in " + p.plugin_src + " is nieuwer); bouw hem opnieuw met scripts/build-unity-plugin.sh " + g.key;
            return false;
        }
    }
    // BepInEx zelf: core en doorstop uit de officiele release.
    if (!copy_tree(bep + "/BepInEx/core", view + "/BepInEx/core", err)) return false;
    for (const char* f : {"winhttp.dll", "doorstop_config.ini"})
        if (!copy_one(bep + "/" + f, view + "/" + f, err)) return false;
    for (const char* d : {"plugins", "config", "patchers", "cache"})
        if (!mkdirs(view + "/BepInEx/" + d, err)) return false;
    // DemulShooter eruit (route 2 kiest uitsluitend onze plugin) en onze plugin erin.
    remove_plugins(view + "/BepInEx/plugins", "DemulShooter", false);
    remove_plugins(view + "/BepInEx/plugins", "RavenGun.", true);
    if (!copy_one(dll, view + "/BepInEx/plugins/" + name, err)) return false;
    log.push_back("plugin: " + dll);
    if (!enable_disk_log(view, err)) return false;
    const std::string cfgf = view + "/BepInEx/config/nl.studiodamon.ravengun." + lower(g.key) + ".cfg";
    if (!is_file(cfgf)) {
        const std::string text = "[Kanaal]\n\nPoort = " + std::to_string(g.port) + "\n\n[Scherm]\n\nBreedte = " + std::to_string(g.width) +
                                 "\nHoogte = " + std::to_string(g.height) + "\nBeeldvullend = " + (g.fullscreen ? "true" : "false") +
                                 "\nResolutieAfdwingen = true\n";
        if (!write_file(cfgf, text, err)) return false;
        log.push_back("plugin-configuratie geschreven: " + cfgf);
    }
    return true;
}

bool install_demulshooter(const std::string& view, const UnityGame& g, const UnityPaths& p, std::vector<std::string>& log, std::string& err) {
    const std::string pl = p.data + "/demulshooter/" + g.demulshooter_version + "/Unity/" + g.plugin_dir;
    if (!is_file(pl + "/winhttp.dll")) {
        err = "plugin-bestanden ontbreken in " + pl + ": download DemulShooter " + g.demulshooter_version + " (zie PROVENANCE.txt)";
        return false;
    }
    remove_plugins(view + "/BepInEx/plugins", "RavenGun.", true);   // route 1 kiest uitsluitend DemulShooter
    for (const std::string& e : list_sorted(pl)) {
        const std::string s = join(pl, e), d = join(view, e);
        if (is_dir(s)) { if (!copy_tree(s, d, err)) return false; }
        else if (!copy_one(s, d, err)) return false;
    }
    if (!enable_disk_log(view, err)) return false;
    for (const std::string& f : list_sorted(pl + "/BepInEx/plugins"))
        if (f.ends_with(".ini") && !write_file(view + "/BepInEx/plugins/" + f, plugin_ini(read_file(pl + "/BepInEx/plugins/" + f), g), err))
            return false;
    log.push_back("plugin: DemulShooter " + g.demulshooter_version + " (" + pl + ")");
    return true;
}

// Alleen een gewone game-naam: hij komt in een pad.
bool plain_name(const std::string& game) {
    if (game.empty() || game.size() > 64) return false;
    for (char ch : game)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    return true;
}

}  // namespace

bool path_under(const std::string& path, const std::string& root) {
    std::error_code ec;
    const std::string p = fs::weakly_canonical(path, ec).string();
    const std::string w = fs::weakly_canonical(root, ec).string();
    return p == w || (p.size() > w.size() && p.compare(0, w.size(), w) == 0 && p[w.size()] == '/');
}

std::vector<std::string> nas_write_errors(const std::string& view, const std::string& nas) {
    std::vector<std::string> errors;
    auto check = [&](const std::string& p) {
        if (is_link(p) && path_under(p, nas)) {
            std::error_code ec;
            errors.push_back(p + " -> " + fs::weakly_canonical(p, ec).string());
        } else if (path_under(p, nas)) {
            errors.push_back(p + " ligt op de NAS");
        }
    };
    std::function<void(const std::string&)> tree = [&](const std::string& p) {
        check(p);
        if (is_dir(p) && !is_link(p))
            for (const std::string& e : list_sorted(p)) tree(join(p, e));
    };
    if (path_under(view, nas)) return {"de view " + view + " ligt op de NAS"};
    for (const char* rel : {"BepInEx", "Game_Data"})
        if (is_link(view + "/" + rel)) errors.push_back(view + "/" + rel + " is een symlink");
    tree(view + "/BepInEx");
    const std::string gd = view + "/Game_Data";
    if (is_dir(gd))
        for (const std::string& e : list_sorted(gd))
            if (lower(e) == "plugins") tree(join(gd, e));
    return errors;
}

bool parse_unity_route(const std::string& json_text, UnityRoute& out, std::string& err) {
    out = UnityRoute{};
    err.clear();
    if (json_text.empty()) return true;
    const auto r = json::parse(json_text);
    if (!r.ok || !r.value.is_object()) { err = "unity-route.json: " + (r.ok ? std::string("geen JSON-object") : r.error); return false; }
    if (const Value* v = r.value.find("port")) {
        if (!v->is_number() || v->num < 1 || v->num > 65535) { err = "unity-route.json: ongeldige poort"; return false; }
        out.port = static_cast<int>(v->num);
    }
    for (auto [key, dst] : std::initializer_list<std::pair<const char*, std::string*>>{
             {"bepinex_version", &out.bepinex_version}, {"demulshooter_version", &out.demulshooter_version}}) {
        const Value* v = r.value.find(key);
        if (!v) continue;
        if (!v->is_string() || v->str.find('/') != std::string::npos || v->str == "..") { err = std::string("unity-route.json: ongeldige waarde voor ") + key; return false; }
        *dst = v->str;
    }
    return true;
}

bool load_unity_route(const UnityPaths& p, UnityRoute& out, std::string& err) {
    if (p.route_json.empty() || !is_file(p.route_json)) { out = UnityRoute{}; err.clear(); return true; }
    return parse_unity_route(read_file(p.route_json), out, err);
}

std::string unity_file(const UnityPaths& p, const std::string& game) {
    if (!plain_name(game)) return {};
    for (const std::string& dir : {p.user_patches, p.patches}) {
        if (dir.empty()) continue;
        const std::string f = dir + "/" + game + "/unity.json";
        if (is_file(f)) return f;
    }
    return {};
}

std::vector<std::string> unity_games(const UnityPaths& p) {
    std::set<std::string> keys;
    for (const std::string& dir : {p.user_patches, p.patches}) {
        if (dir.empty()) continue;
        for (const std::string& e : list_sorted(dir))
            if (plain_name(e) && is_file(dir + "/" + e + "/unity.json")) keys.insert(e);
    }
    return {keys.begin(), keys.end()};
}

bool unity_game_known(const UnityPaths& p, const std::string& game) { return !unity_file(p, game).empty(); }

std::string unity_text(const UnityPaths& p, const std::string& game) {
    const std::string f = unity_file(p, game);
    return f.empty() ? std::string() : read_file(f);
}

std::string resolve_unity_dir(const std::string& folder, const std::vector<std::string>& game_folders) {
    return find_game_folder(folder, game_folders);   // dezelfde regel als "@nas/<Map>" van de native route
}

bool parse_unity_game(const std::string& json_text, const std::string& source, const std::string& game, const UnityRoute& route, const std::string& home,
                      const std::vector<std::string>& game_folders, const std::string& plugin_override, UnityGame& out, std::string& err) {
    const auto r = json::parse(json_text);
    const std::string file = source.empty() ? std::string("unity.json") : source;
    if (!r.ok || !r.value.is_object()) { err = file + ": " + (r.ok ? std::string("geen JSON-object") : r.error); return false; }
    const Value* g = &r.value;
    out = UnityGame{};
    out.key = game;
    out.source = source;
    for (auto [key, dst] : std::initializer_list<std::pair<const char*, std::string*>>{
             {"name", &out.name}, {"folder", &out.folder}, {"nas_dir", &out.nas_dir}, {"exe", &out.exe}, {"plugin", &out.plugin},
             {"plugin_dir", &out.plugin_dir}, {"language", &out.language}}) {
        const Value* v = g->find(key);
        if (v && !v->is_string()) { err = file + ": '" + key + "' is geen tekst"; return false; }
        if (v) *dst = v->str;
    }
    for (auto [key, dst] : std::initializer_list<std::pair<const char*, int*>>{{"width", &out.width}, {"height", &out.height}}) {
        const Value* v = g->find(key);
        if (v && (!v->is_number() || v->num < 1 || v->num > 16384)) { err = file + ": '" + key + "' is geen geldige maat"; return false; }
        if (v) *dst = static_cast<int>(v->num);
    }
    if (const Value* v = g->find("fullscreen"); v && v->is_bool()) out.fullscreen = v->b;
    out.port = route.port;
    out.bepinex_version = route.bepinex_version;
    out.demulshooter_version = route.demulshooter_version;
    if (out.exe.empty()) out.exe = "Game.exe";
    if (out.exe.find('/') != std::string::npos || out.exe == "." || out.exe == "..") { err = file + ": ongeldige exe " + out.exe; return false; }
    for (const std::string* s : {&out.bepinex_version, &out.demulshooter_version, &out.plugin_dir})
        if (s->find('/') != std::string::npos || *s == "..") { err = file + ": ongeldige waarde " + *s; return false; }
    if (!out.folder.empty() && (out.folder.find('/') != std::string::npos || out.folder == "." || out.folder == "..")) {
        err = file + ": 'folder' is een mapnaam binnen een gamemap, geen pad (een eigen pad is 'nas_dir' in de gebruikerslaag)";
        return false;
    }
    // De map van de game: een eigen pad van de gebruiker gaat voor, anders de mapnaam in de gamemappen van library.json.
    if (!out.nas_dir.empty()) {
        if (out.nas_dir.rfind("~/", 0) == 0) out.nas_dir = home + out.nas_dir.substr(1);
        if (out.nas_dir[0] != '/') { err = file + ": 'nas_dir' moet een absoluut pad zijn"; return false; }
    } else if (out.folder.empty()) {
        err = file + ": 'folder' ontbreekt (de mapnaam van de game in een gamemap)";
        return false;
    } else {
        out.nas_dir = resolve_unity_dir(out.folder, game_folders);
        if (out.nas_dir.empty()) {
            std::string where;
            for (const std::string& f : game_folders) where += (where.empty() ? "" : ", ") + f;
            err = "de map '" + out.folder + "' van " + game + " staat in geen gamemap (" + (where.empty() ? std::string("geen gamemappen in library.json") : where) +
                  "); NAS gekoppeld, en de map toegevoegd in de app (Add Folder)?";
            return false;
        }
    }
    if (!plugin_override.empty()) out.plugin = plugin_override;
    if (out.plugin.empty()) out.plugin = "ravengun";
    if (out.plugin != "ravengun" && out.plugin != "demulshooter") {
        err = "onbekende plugin '" + out.plugin + "'; kies ravengun of demulshooter";
        return false;
    }
    if (out.name.empty()) out.name = game;
    return true;
}

bool load_unity_game(const UnityPaths& p, const std::vector<std::string>& game_folders, const std::string& game, const std::string& plugin_override,
                     UnityGame& out, std::string& err) {
    const std::string file = unity_file(p, game);
    if (file.empty()) { err = "onbekende Unity-game: " + game + " (geen patches/" + game + "/unity.json)"; return false; }
    UnityRoute route;
    if (!load_unity_route(p, route, err)) return false;
    return parse_unity_game(read_file(file), file, game, route, p.home, game_folders, plugin_override, out, err);
}

bool build_view(const UnityGame& g, const UnityPaths& p, std::string& view, std::vector<std::string>& log, std::string& err) {
    const std::string& nas = g.nas_dir;
    if (!is_file(nas + "/" + g.exe)) { err = g.exe + " niet gevonden in " + nas + " (NAS gekoppeld?)"; return false; }
    view = p.data + "/runtime/views-aagames/" + g.key;
    if (path_under(view, nas)) { err = "de view " + view + " ligt op de NAS " + nas + "; de view moet altijd een lokale map zijn"; return false; }
    static const std::set<std::string> kSkip = {"bepinex", "winhttp.dll", "doorstop_config.ini", "teknoparrot.ini", "doorstop.dll",
                                                "0harmony.dll", ".doorstop_version", "game_data"};
    static const std::vector<std::string> kCopyExt = {".json", ".conf", ".cfg"};
    static const std::vector<std::string> kGameDataExt = {".json", ".conf", ".cfg", ".config", ".info", ".txt", ".ini"};
    // Game_Data: Plugins is lokaal omdat onze drop-ins daar komen; StreamingAssets is een eigen map van de game, lokaal uit gemak.
    static const std::set<std::string> kLocalDirs = {"plugins", "streamingassets"};
    if (!link_all(nas, view, kSkip, true, {}, kCopyExt, err)) return false;
    if (!link_all(nas + "/Game_Data", view + "/Game_Data", {}, true, kLocalDirs, kGameDataExt, err)) return false;
    if (!(g.plugin == "demulshooter" ? install_demulshooter(view, g, p, log, err) : install_ravengun(view, g, p, log, err))) return false;
    const std::vector<std::string> bad = nas_write_errors(view, nas);
    if (!bad.empty()) {
        err = "schrijfpaden die op de NAS uitkomen (dat mag nooit):";
        for (const std::string& b : bad) err += "\n  " + b;
        return false;
    }
    log.push_back("view: " + view + " (plugin: " + g.plugin + ")");
    return true;
}

bool plan_unity(const UnityGame& g, const UnityPaths& p, const std::string& view, const UnityOptions& o, UnityPlan& out, std::string& err) {
    out = UnityPlan{};
    const std::string proton = p.proton + "/proton";
    if (::access(proton.c_str(), X_OK) != 0) { err = "GE-Proton ontbreekt: " + proton; return false; }
    out.prefix = p.data + "/prefixes/unity-aagames";
    out.cwd = view;
    out.comm = g.exe;
    out.argv = {proton, "run", g.exe};
    if (o.popup) out.argv.push_back("-popupwindow");
    out.env = {{"WINEDLLOVERRIDES", "winhttp=n,b"},   // de plugin laadt via doorstop: winhttp.dll uit de gamemap eerst
               {"WINEDEBUG", "-all"},
               {"STEAM_COMPAT_DATA_PATH", out.prefix},
               {"STEAM_COMPAT_CLIENT_INSTALL_PATH", p.home + "/.local/share/Steam"},
               {"XDG_RUNTIME_DIR", o.runtime_dir},
               {"WAYLAND_DISPLAY", o.wayland_display.empty() ? std::string("wayland-0") : o.wayland_display},
               {"DISPLAY", o.display.empty() ? std::string(":0") : o.display},
               {"DBUS_SESSION_BUS_ADDRESS", o.dbus.empty() ? "unix:path=" + o.runtime_dir + "/bus" : o.dbus}};
    if (!o.xauthority.empty()) out.env.push_back({"XAUTHORITY", o.xauthority});
    if (!is_dir(out.prefix + "/pfx/drive_c")) out.notes.push_back("prefix " + out.prefix + " is nieuw: Proton maakt hem bij deze start (duurt even)");
    return true;
}

std::string find_xwayland_xauthority(const std::string& proc_root) {
    for (const std::string& pid : list_sorted(proc_root)) {
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        std::ifstream c(proc_root + "/" + pid + "/comm");
        std::string comm;
        if (!c || !std::getline(c, comm) || comm != "kwin_wayland") continue;
        const std::string cmd = read_file(proc_root + "/" + pid + "/cmdline");
        std::vector<std::string> args;
        std::string cur;
        for (char ch : cmd) {
            if (ch == '\0') { args.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        if (!cur.empty()) args.push_back(cur);
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--xwayland-xauthority") return args[i + 1];
    }
    return {};
}

}  // namespace ravengun::launch
