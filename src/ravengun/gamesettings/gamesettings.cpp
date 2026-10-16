// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/strutil.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ravengun::gamesettings {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

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

const char* const kDongleSources[] = {"usb-passthrough", "drop-in", nullptr};

Dongle dongle_of(const Ini& settings) {
    Dongle d;
    const std::string raw = trim(settings.get("Dongle", "Enabled"));
    const std::string l = lower(raw);
    d.enabled = truthy(raw);
    if (!raw.empty() && !d.enabled && !(l == "false" || l == "0" || l == "no" || l == "off")) {
        d.enabled = true;   // geen true of false: aan met een fout (de launcher weigert dan met die melding)
        d.error = "[Dongle] Enabled = " + raw + " (use true or false)";
    }
    const std::string src_raw = trim(settings.get("Dongle", "Source"));
    d.source = src_raw.empty() ? std::string(kDongleSources[0]) : lower(src_raw);
    bool known = false;
    std::string list;
    for (const char* const* c = kDongleSources; *c; ++c) {
        known = known || d.source == *c;
        list += (list.empty() ? "" : ", ") + std::string(*c);
    }
    if (!known && d.error.empty()) d.error = "[Dongle] Source = " + src_raw + " (known: " + list + ")";
    return d;
}

std::string Patch::get(const std::string& key, const std::string& def) const {
    for (const auto& kv : fields)
        if (kv.first == key) return kv.second;
    return def;
}

std::string Patch::kind() const { return lower(trim(get("Kind"))); }
std::string Patch::category() const { return lower(trim(get("Category"))); }
std::string Patch::version() const { return trim(get("Version")); }
std::string Patch::applied_by() const { return lower(trim(get("AppliedBy"))); }

const char* const kPatchCategories[] = {"graphics", "input", "timing", "audio", "paths", "crash", "network", "loader", "dongle", "region", "keychip", "integrity", "activation", nullptr};

std::string patch_categories_text() {
    std::string out;
    for (const char* const* c = kPatchCategories; *c; ++c) out += (out.empty() ? "" : ", ") + std::string(*c);
    return out;
}

std::string Patch::validate() const {
    const std::string k = kind();
    if (k.empty()) return "patch '" + name + "' heeft geen definitie (Kind ontbreekt; definieer hem in patches.ini of in _defaults/<Type>.patches.ini)";
    if (k != "file" && k != "registry" && k != "memory") return "patch '" + name + "': onbekend Kind '" + k + "' (toegestaan: file, registry, memory)";
    const std::string c = category();
    if (c.empty())
        return "patch '" + name + "': Category ontbreekt (toegestaan: " + patch_categories_text() + ")";
    for (const char* const* it = kPatchCategories; *it; ++it)
        if (c == *it) return {};
    return "patch '" + name + "': onbekende Category '" + trim(get("Category")) + "' (toegestaan: " + patch_categories_text() + ")";
}

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

Game load_game(const Dirs& dirs, const std::string& profile, const std::string& type_hint, const Ini* legacy) {
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
    std::string type = legacy ? legacy->get("Game", "Type") : std::string();
    if (legacy && !legacy->sections.empty()) g.exists = true;
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
    if (legacy) merge_into(g.settings, *legacy);
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
    for (const auto& name : order) {
        if (merged[name].empty()) continue;
        Patch patch{name, merged[name]};
        const std::string err = patch.validate();
        if (err.empty()) {
            g.patches.push_back(std::move(patch));
        } else {
            g.patch_errors.push_back({name, err, patch.enabled()});
            g.warnings.push_back(err);
        }
    }
    return g;
}

Ini read_layer(const std::string& layer_dir, const std::string& profile, const std::string& file) {
    if (layer_dir.empty()) return {};
    return read_ini_file(layer_dir + "/" + profile + "/" + file, nullptr, nullptr);
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

// ---------------------------------------------------------------------------------------------------------------------
// Het profiel van een game

const ProfileField kProfileFields[] = {
    {"Game", "Executable", "ExecutableName", ""},
    {"Game", "Executable2", "ExecutableName2", ""},
    {"Game", "Type", "EmulatorType", ""},
    {"Game", "EmulationProfile", "EmulationProfile", ""},
    {"Game", "Is64Bit", "Is64Bit", "bool"},
    {"Game", "ProfileRevision", "GameProfileRevision", ""},
    {"Game", "Patreon", "Patreon", "bool"},
    {"Game", "MsysType", "msysType", ""},
    {"Game", "GameVersion", "GameVersion", ""},
    {"Game", "HasTwoExecutables", "HasTwoExecutables", "bool"},
    {"Game", "LaunchSecondExecutableFirst", "LaunchSecondExecutableFirst", "bool"},
    {"Launch", "UseRemoteThread", "UseRemoteThread", "bool"},
    {"Launch", "ExtraParameters", "ExtraParameters", ""},
    {"Launch", "CustomArguments", "CustomArguments", ""},
    {"Display", "GamescopeCompatibility", "GamescopeGameWindowCompatibility", ""},
    {"Wine", "PrefixMode", "WinePrefixMode", ""},
    {"Wine", "Runner", "WineRunnerPath", "runner"},
};
const size_t kProfileFieldCount = sizeof(kProfileFields) / sizeof(kProfileFields[0]);

const ProfileField* profile_field_for_xml(const std::string& xml_name) {
    for (size_t i = 0; i < kProfileFieldCount; ++i)
        if (xml_name == kProfileFields[i].xml) return &kProfileFields[i];
    return nullptr;
}

const EnvMapEntry kEnvMap[] = {
    {"Loader", "RemoteThread", "TP_REMOTETHREAD", ""},
    {"Loader", "PostStartMs", "TP_POSTSTART_REMOTETHREAD_MS", ""},
    {"Loader", "EntryPointMs", "TP_ENTRYPOINT_REMOTETHREAD_MS", ""},
    {"Launch", "WorkingDirectory", "TP_GAME_WORKING_DIRECTORY", "workdir"},
    {"Wine", "Prefix", "RG_WINEPREFIX", "prefix"},
    {"Wine", "PrefixDirect", "TP_WINEPREFIX", "prefix"},
    {"Wine", "Binary", "TP_WINE", "path"},
    {"Wine", "DllOverrides", "WINEDLLOVERRIDES", ""},
    {"Wine", "Debug", "WINEDEBUG", ""},
    {"Wine", "GStreamerPlugins", "GST_PLUGIN_PATH", "path"},
    {"Display", "Gamescope", "TP_GAMESCOPE", "bool1"},
    {"Display", "GamescopePath", "TP_GAMESCOPE_PATH", "path"},
    {"Display", "GamescopeBackend", "TP_GAMESCOPE_BACKEND", ""},
    {"Display", "GamescopeResolution", "RG_GAMESCOPE_RES", ""},
    {"Display", "GamescopeScale", "RG_GAMESCOPE_SCALE", ""},
    {"Display", "GamescopeGameResolution", "RG_GAMESCOPE_GAME_RES", ""},   // interne resolutie van gamescope (-w/-h), bv. 1360x768
    {"Input", "JvsTransport", "RG_JVS_TRANSPORT", ""},
    {"Input", "JvsPipeBoard", "RG_JVS_BOARD", "boolT"},
    {"Input", "JvsF1Once", "RG_JVS_F1_ONCE", "boolT"},
    {"Input", "JvsDebug", "RG_JVS_DEBUG", "boolT"},
    {"Input", "JvsIdent", "RG_JVS_IDENT", ""},
    {"Input", "JvsFeatures", "RG_JVS_FEATURES", ""},
    {"Audio", "Speakers", "RG_FMOD_SPEAKERS", ""},   // FMOD-wrapper (tools/fmod-bridge): STEREO of LPCM 5.1
};
const size_t kEnvMapCount = sizeof(kEnvMap) / sizeof(kEnvMap[0]);

const EnvMapEntry* env_for_var(const std::string& var) {
    for (size_t i = 0; i < kEnvMapCount; ++i)
        if (var == kEnvMap[i].var) return &kEnvMap[i];
    return nullptr;
}

std::vector<ConfigField> config_fields(const Ini& settings) {
    std::vector<ConfigField> out;
    const Section* typ = settings.section("ConfigType");
    if (!typ) return out;
    for (const auto& kv : typ->kv) {
        ConfigField f;
        const size_t slash = kv.first.find('/');
        f.category = slash == std::string::npos ? std::string() : kv.first.substr(0, slash);
        f.name = slash == std::string::npos ? kv.first : kv.first.substr(slash + 1);
        f.type = kv.second;
        f.value = settings.get("Config", kv.first);
        const std::string opts = settings.get("ConfigOptions", kv.first);
        if (!opts.empty()) {
            size_t pos = 0;
            for (;;) {
                const size_t bar = opts.find('|', pos);
                f.options.push_back(opts.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos));
                if (bar == std::string::npos) break;
                pos = bar + 1;
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}

std::string find_config_key(const Ini& settings, const std::string& name, const std::string& category) {
    const Section* typ = settings.section("ConfigType");
    if (!typ) return {};
    for (const auto& kv : typ->kv) {
        const size_t slash = kv.first.find('/');
        const std::string cat = slash == std::string::npos ? std::string() : kv.first.substr(0, slash);
        const std::string nm = slash == std::string::npos ? kv.first : kv.first.substr(slash + 1);
        if (nm == name && (category.empty() || cat == category)) return kv.first;
    }
    return {};
}

bool has_own_profile(const Ini& settings) {
    return !trim(settings.get("Game", "Executable")).empty() || !trim(settings.get("Game", "ProfileRevision")).empty();
}

Identify identify_of(const Ini& settings) {
    Identify id;
    std::string path = trim(settings.get("Identify", "Path"));
    if (path.empty()) {
        const std::string exe = settings.get("Game", "Executable");
        path = trim(exe.substr(0, exe.find_first_of(";|")));
    }
    for (auto& c : path)
        if (c == '\\') c = '/';
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    while (!path.empty() && path.back() == '/') path.pop_back();
    id.path = path;
    const std::string size = trim(settings.get("Identify", "Size"));
    if (!size.empty() && std::all_of(size.begin(), size.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) id.size = std::stoll(size);
    id.sha256 = lower(trim(settings.get("Identify", "Sha256")));
    id.folder = trim(settings.get("Identify", "Folder"));
    return id;
}

namespace {

namespace fs = std::filesystem;

bool identify_matches(const std::string& root, const Identify& id, bool check_folder, const HashFn& hash) {
    std::error_code ec;
    if (check_folder && !id.folder.empty()) {
        const fs::path rp(root);
        std::string base = rp.filename().string();
        if (base.empty()) base = rp.parent_path().filename().string();   // "dir/" met een slash aan het eind
        if (lower(trim(base)) != lower(id.folder)) return false;
    }
    const fs::path full = fs::path(root) / id.path;
    if (!fs::is_regular_file(full, ec)) return false;
    if (id.size >= 0 && static_cast<long long>(fs::file_size(full, ec)) != id.size) return false;
    if (!id.sha256.empty()) {
        if (!hash) return true;   // zonder hashfunctie (de engine) gelden pad, grootte en mapnaam
        return lower(hash(full.string())) == id.sha256;
    }
    return true;
}

}  // namespace

std::string locate_game(const std::string& profile, const Ini& settings, const std::vector<std::string>& folders, const std::string& views_dir,
                        const std::string& home, std::string* how, const HashFn& hash, const std::vector<std::string>& hints) {
    namespace fs = std::filesystem;
    auto result = [&](const std::string& p, const char* h) {
        if (how) *how = p.empty() ? "" : h;
        return p;
    };
    std::string ov = trim(settings.get("Game", "GamePath"));
    if (!ov.empty()) {
        if (!home.empty() && (ov == "~" || ov.rfind("~/", 0) == 0)) ov = home + ov.substr(1);
        return result(ov, "override");
    }
    const Identify id = identify_of(settings);
    if (id.path.empty()) return result("", "");
    std::error_code ec;
    if (!views_dir.empty()) {
        const std::string view = views_dir + "/" + profile;
        if (fs::is_directory(view, ec) && identify_matches(view, id, false, hash)) return result((fs::path(view) / id.path).string(), "view");
    }
    // Goedkoop voor het zoeken: de map uit [Identify] Folder in elke gamemap, dan wat library.json bij dit profiel heeft.
    if (!id.folder.empty() && id.folder.find('/') == std::string::npos && id.folder != "." && id.folder != "..")
        for (const std::string& f : folders) {
            const std::string root = (fs::path(f) / id.folder).string();
            if (fs::is_directory(root, ec) && identify_matches(root, id, true, hash)) return result((fs::path(root) / id.path).string(), "folders");
        }
    for (const std::string& root : hints)
        if (!root.empty() && fs::is_directory(root, ec) && identify_matches(root, id, true, hash))
            return result((fs::path(root) / id.path).string(), "folders");
    for (const std::string& f : folders) {
        std::vector<std::string> subs;
        for (fs::directory_iterator it(f, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (!name.empty() && name[0] != '.' && it->is_directory(ec)) subs.push_back(it->path().string());
        }
        std::sort(subs.begin(), subs.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        subs.push_back(f);
        for (const std::string& root : subs)
            if (identify_matches(root, id, true, hash)) return result((fs::path(root) / id.path).string(), "folders");
    }
    return result("", "");
}

}  // namespace ravengun::gamesettings
