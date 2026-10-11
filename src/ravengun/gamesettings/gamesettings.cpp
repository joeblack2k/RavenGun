// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/gamesettings/gamesettings.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace ravengun::gamesettings {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back(cur);
            cur.clear();
        } else if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() || (!text.empty() && text.back() != '\n' && text.back() != '\r')) out.push_back(cur);
    return out;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

bool is_file(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool is_dir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool mkdirs(const std::string& path) {
    if (path.empty()) return false;
    if (is_dir(path)) return true;
    size_t sl = path.find_last_of('/');
    if (sl != std::string::npos && sl > 0 && !mkdirs(path.substr(0, sl))) return false;
    return ::mkdir(path.c_str(), 0755) == 0 || is_dir(path);
}

bool write_file_atomic(const std::string& path, const std::string& text, std::string* err) {
    const size_t sl = path.find_last_of('/');
    if (sl != std::string::npos && !mkdirs(path.substr(0, sl))) {
        if (err) *err = "Cannot create the folder " + path.substr(0, sl);
        return false;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "Cannot write " + tmp;
            return false;
        }
        f << text;
        if (!f) {
            if (err) *err = "Cannot write " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        if (err) *err = "Cannot replace " + path;
        return false;
    }
    return true;
}

// Ini + waarschuwingen uit een bestand; ontbreekt het, dan is het resultaat leeg.
Ini read_ini_file(const std::string& path, std::vector<std::string>* warnings, bool* existed) {
    std::string text;
    const bool ok = read_file(path, text);
    if (existed) *existed = ok;
    if (!ok) return {};
    std::vector<std::string> w;
    Ini ini = parse_ini(text, &w);
    if (warnings)
        for (const auto& x : w) warnings->push_back(path + ": " + x);
    return ini;
}

constexpr const char* kPatchPrefix = "Patch:";

// {patchnaam -> velden (in volgorde)} uit de secties "Patch: <naam>" van een Ini.
struct PatchLayer {
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> items;
};

PatchLayer patches_from(const Ini& ini) {
    PatchLayer out;
    const std::string prefix = kPatchPrefix;
    for (const auto& s : ini.sections)
        if (s.name.compare(0, prefix.size(), prefix) == 0) out.items.emplace_back(trim(s.name.substr(prefix.size())), s.kv);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Ini

const Section* Ini::section(const std::string& name) const {
    for (const auto& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

const std::string* Ini::find(const std::string& sec, const std::string& key) const {
    const Section* s = section(sec);
    if (!s) return nullptr;
    for (const auto& kv : s->kv)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string Ini::get(const std::string& sec, const std::string& key, const std::string& def) const {
    const std::string* v = find(sec, key);
    return v ? *v : def;
}

void Ini::set(const std::string& sec, const std::string& key, const std::string& value) {
    for (auto& s : sections) {
        if (s.name != sec) continue;
        for (auto& kv : s.kv)
            if (kv.first == key) {
                kv.second = value;
                return;
            }
        s.kv.emplace_back(key, value);
        return;
    }
    sections.push_back({sec, {{key, value}}});
}

void Ini::erase(const std::string& sec, const std::string& key) {
    for (auto& s : sections) {
        if (s.name != sec) continue;
        s.kv.erase(std::remove_if(s.kv.begin(), s.kv.end(), [&](const auto& kv) { return kv.first == key; }), s.kv.end());
    }
}

Ini parse_ini(const std::string& text, std::vector<std::string>* warnings) {
    Ini ini;
    Section* cur = nullptr;
    int n = 0;
    for (const std::string& raw : split_lines(text)) {
        ++n;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        // Commentaar achter de waarde: een ';' na witruimte (zoals configparser met inline_comment_prefixes=(';',)).
        for (size_t i = 1; i < line.size(); ++i)
            if (line[i] == ';' && is_space(line[i - 1])) {
                line = trim(line.substr(0, i));
                break;
            }
        if (line.empty()) continue;
        if (line[0] == '[') {
            const size_t close = line.rfind(']');
            if (close == std::string::npos || close < 2) {
                if (warnings) warnings->push_back("line " + std::to_string(n) + ": section header without ']'");
                cur = nullptr;
                continue;
            }
            const std::string name = line.substr(1, close - 1);
            Section* found = nullptr;
            for (auto& s : ini.sections)
                if (s.name == name) found = &s;
            if (!found) {
                ini.sections.push_back({name, {}});
                found = &ini.sections.back();
            }
            cur = found;
            continue;
        }
        if (!cur) {
            if (warnings) warnings->push_back("line " + std::to_string(n) + ": key outside a section");
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            if (warnings) warnings->push_back("line " + std::to_string(n) + ": no 'key = value'");
            continue;
        }
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key.empty()) continue;
        bool done = false;
        for (auto& kv : cur->kv)
            if (kv.first == key) {
                kv.second = value;
                done = true;
            }
        if (!done) cur->kv.emplace_back(key, value);
    }
    return ini;
}

void merge_into(Ini& base, const Ini& over) {
    for (const auto& s : over.sections)
        for (const auto& kv : s.kv) {
            if (kv.second.empty()) base.erase(s.name, kv.first);
            else base.set(s.name, kv.first, kv.second);
        }
}

bool truthy(const std::string& v) {
    const std::string l = lower(trim(v));
    return l == "true" || l == "1" || l == "yes" || l == "on";
}

std::string Patch::get(const std::string& key, const std::string& def) const {
    for (const auto& kv : fields)
        if (kv.first == key) return kv.second;
    return def;
}

std::string Patch::kind() const { return lower(trim(get("Kind"))); }

const Patch* Game::patch(const std::string& name) const {
    for (const auto& p : patches)
        if (p.name == name) return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lagen

Dirs default_dirs(const std::string& data_dir) {
    Dirs d;
    const char* p = std::getenv("RAVENGUN_PATCHES_DIR");
    if (p && *p) d.shipped = p;
    else if (!data_dir.empty()) d.shipped = data_dir + "/patches";
    const char* u = std::getenv("RAVENGUN_USER_PATCHES_DIR");
    if (u && *u) {
        d.user = u;
        return d;
    }
    const char* x = std::getenv("XDG_DATA_HOME");
    const char* h = std::getenv("HOME");
    if (x && *x == '/') d.user = std::string(x) + "/ravengun/patches";
    else if (h && *h == '/') d.user = std::string(h) + "/.local/share/ravengun/patches";
    return d;
}

std::string game_dir(const std::string& layer_dir, const std::string& profile) { return layer_dir.empty() ? std::string() : layer_dir + "/" + profile; }

Game load_game(const Dirs& dirs, const std::string& profile, const std::string& type_hint) {
    Game g;
    g.profile = profile;
    std::vector<Ini> layers;          // game.ini: meegeleverd, gebruiker
    std::vector<PatchLayer> pl;       // patches.ini: idem
    for (const std::string& d : {dirs.shipped, dirs.user}) {
        Ini gi, pi;
        if (!d.empty()) {
            const std::string gp = d + "/" + profile + "/game.ini", pp = d + "/" + profile + "/patches.ini";
            bool existed = false;
            gi = read_ini_file(gp, &g.warnings, &existed);
            if (existed) {
                g.exists = true;
                g.sources.push_back(gp);
            }
            pi = read_ini_file(pp, &g.warnings, &existed);
            if (existed) g.sources.push_back(pp);
        }
        layers.push_back(std::move(gi));
        pl.push_back(patches_from(pi));
    }
    std::string type;
    for (const auto& l : layers) {
        const std::string t = l.get("Game", "Type");
        if (!t.empty()) type = t;
    }
    if (type.empty()) type = type_hint;
    g.type = type;
    PatchLayer shared;
    if (!type.empty() && !dirs.shipped.empty()) {
        const std::string dp = dirs.shipped + "/_defaults/" + type + ".ini", dpp = dirs.shipped + "/_defaults/" + type + ".patches.ini";
        bool existed = false;
        Ini d = read_ini_file(dp, &g.warnings, &existed);
        if (existed) g.sources.push_back(dp);
        merge_into(g.settings, d);
        existed = false;
        Ini dpi = read_ini_file(dpp, &g.warnings, &existed);
        if (existed) g.sources.push_back(dpp);
        shared = patches_from(dpi);
    }
    for (const auto& l : layers) merge_into(g.settings, l);
    // Lege secties vallen weg.
    g.settings.sections.erase(std::remove_if(g.settings.sections.begin(), g.settings.sections.end(), [](const Section& s) { return s.kv.empty(); }),
                              g.settings.sections.end());
    // Patches: de definities uit de gedeelde laag, dan de lagen van de game; per sleutel samengevoegd.
    std::vector<std::string> order;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> merged;
    auto apply = [&](const PatchLayer& layer) {
        for (const auto& it : layer.items) {
            if (!merged.count(it.first)) order.push_back(it.first);
            auto& fields = merged[it.first];
            for (const auto& kv : it.second) {
                auto pos = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.first == kv.first; });
                if (kv.second.empty()) {
                    if (pos != fields.end()) fields.erase(pos);
                } else if (pos != fields.end()) {
                    pos->second = kv.second;
                } else {
                    fields.emplace_back(kv.first, kv.second);
                }
            }
        }
    };
    apply(shared);
    for (const auto& l : pl) apply(l);
    for (const auto& name : order)
        if (!merged[name].empty()) g.patches.push_back({name, merged[name]});
    return g;
}

std::vector<std::string> list_games(const Dirs& dirs) {
    std::vector<std::string> out;
    for (const std::string& d : {dirs.shipped, dirs.user}) {
        if (d.empty()) continue;
        DIR* dir = ::opendir(d.c_str());
        if (!dir) continue;
        while (const dirent* e = ::readdir(dir)) {
            const std::string name = e->d_name;
            if (name.empty() || name[0] == '.' || name[0] == '_') continue;
            if (is_dir(d + "/" + name) && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
        ::closedir(dir);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        const std::string x = lower(a), y = lower(b);
        return x != y ? x < y : a < b;
    });
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Schrijven

std::string edit_ini_text(const std::string& text, const std::string& section, const std::string& key, const std::string& value, bool erase) {
    std::vector<std::string> lines = split_lines(text);
    int in_target = 0;                  // 1 = binnen de doelsectie
    long header = -1, last_kv = -1, key_line = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[') {
            const size_t close = t.rfind(']');
            in_target = close != std::string::npos && close >= 2 && t.substr(1, close - 1) == section;
            if (in_target && header < 0) header = static_cast<long>(i);
            continue;
        }
        if (!in_target) continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        last_kv = static_cast<long>(i);
        if (key_line < 0 && trim(t.substr(0, eq)) == key) key_line = static_cast<long>(i);
    }
    const std::string newline = value.empty() ? key + " =" : key + " = " + value;
    if (key_line >= 0) {
        if (erase) lines.erase(lines.begin() + key_line);
        else lines[static_cast<size_t>(key_line)] = newline;
    } else if (!erase) {
        if (header >= 0) lines.insert(lines.begin() + (last_kv >= 0 ? last_kv : header) + 1, newline);
        else {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + section + "]");
            lines.push_back(newline);
        }
    }
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

namespace {

bool edit_user_file(const Dirs& dirs, const std::string& profile, const char* file, const std::string& section, const std::string& key,
                    const std::string& value, bool erase, std::string* err) {
    if (dirs.user.empty()) {
        if (err) *err = "No user folder for game settings.";
        return false;
    }
    if (profile.empty() || profile.find('/') != std::string::npos || profile[0] == '.' || profile[0] == '_') {
        if (err) *err = "Invalid game name.";
        return false;
    }
    const std::string path = dirs.user + "/" + profile + "/" + file;
    std::string text;
    const bool had = read_file(path, text);
    if (!had && erase) return true;
    const std::string out = edit_ini_text(text, section, key, value, erase);
    if (had && out == text) return true;
    return write_file_atomic(path, out, err);
}

}  // namespace

bool set_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key, const std::string& value,
                    std::string* err) {
    return edit_user_file(dirs, profile, "game.ini", section, key, value, false, err);
}

bool erase_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key, std::string* err) {
    return edit_user_file(dirs, profile, "game.ini", section, key, {}, true, err);
}

bool set_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, bool enabled, std::string* err) {
    return edit_user_file(dirs, profile, "patches.ini", std::string(kPatchPrefix) + " " + patch, "Enabled", enabled ? "true" : "false", false, err);
}

bool erase_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, std::string* err) {
    return edit_user_file(dirs, profile, "patches.ini", std::string(kPatchPrefix) + " " + patch, "Enabled", {}, true, err);
}

}  // namespace ravengun::gamesettings
