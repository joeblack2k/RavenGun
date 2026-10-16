#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Alleen voor de bestanden van de directe route (direct*.cpp): de helpers die ze delen. Niet voor andere code; de publieke kant is
// direct.hpp. Alles staat in ravengun::launch::direct_detail; de bestanden halen het binnen met `using namespace direct_detail;`.
// Wat maar een bestand gebruikt staat daar in een anonieme namespace.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/launch/direct.hpp"
#include "ravengun/strutil.hpp"

namespace ravengun::launch {

namespace fs = std::filesystem;
namespace gsn = ravengun::gamesettings;

namespace direct_detail {

// [Launch] StackReserve: de koppeling van de view naar de oorspronkelijke exe blijft onder deze naam naast de lokale kopie staan.
inline constexpr const char* kStackOrigSuffix = ".ravengun-orig";

// --- direct_util.cpp: tekst, paden (posixpath), XML ------------------------------------------------------------------------------------
std::string upper(std::string s);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& v, const std::string& sep);
std::vector<std::string> nonempty_split(const std::string& s, char sep);
bool ld_truthy(const std::string& v);   // launch-direct.py truthy(): alleen "true"
bool gs_falsy(const std::string& v);

std::string normpath(const std::string& p);
std::string abspath(const std::string& p);
std::string pjoin(const std::string& a, const std::string& b);
std::string dirname(const std::string& p);
std::string basename(const std::string& p);
std::string realpath_(const std::string& p);
std::string relpath(const std::string& path, const std::string& start);
bool binnen(const std::string& path, const std::string& base);
bool isfile(const std::string& p);
bool isdir(const std::string& p);
bool lexists(const std::string& p);
bool islink(const std::string& p);
bool samefile(const std::string& a, const std::string& b);
bool is_pe(const std::string& path);
bool read_file(const std::string& path, std::string& out);
std::string strip_bom(std::string s);
std::string expand_home(const std::string& p, const std::string& home);
std::string shrink_home(const std::string& p, const std::string& home);
std::string getenv_or(const EnvMap& env, const std::string& k, const std::string& def = {});
bool on_nas(const std::string& path, const DirectPaths& p);   // launch-direct.py on_nas()

std::string xml_tag(const std::string& xml, const std::string& tag, bool* found = nullptr);   // de tekst van het eerste <tag>
std::string kv_get(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k, const std::string& def = {});
bool py_int(const std::string& s, long long& out);
bool py_float(const std::string& s, double& out);

// --- direct_profile.cpp: instellingen en profiel ---------------------------------------------------------------------------------------
std::string prefixes_dir(const DirectPaths& p);
std::string shared_prefix(const DirectPaths& p);
EnvMap to_env(const gsn::Ini& s, const std::string& gamedir, const DirectPaths& p);
std::string locate(const std::string& profile, const gsn::Ini& settings, const DirectPaths& p, std::string* how);
gsn::Game load_for_profile(const std::string& profile, const DirectPaths& p, std::vector<std::string>* notes, bool* legacy_fpfix,
                           const gsn::Dirs* dirs_override = nullptr);

// ownprofile.profile_from_settings
struct Field { std::string category, name, value, type; std::vector<std::string> options; };
struct Profile {
    std::map<std::string, std::string> s;   // de scalaire velden met hun XML-naam
    std::vector<Field> fields;
    std::string get(const std::string& k, const std::string& def = {}) const { auto it = s.find(k); return it == s.end() ? def : it->second; }
    Field* field(const std::string& name) { for (auto& f : fields) if (f.name == name) return &f; return nullptr; }
    const Field* field(const std::string& name) const { for (const auto& f : fields) if (f.name == name) return &f; return nullptr; }
};
Profile profile_from_settings(const std::string& name, const gsn::Ini& settings, const std::string& game, const DirectPaths& p);

// teknoparrot.ini (build_ini) en de geweer-ini van Taito (taito_gun_ini)
std::string build_ini(const Profile& pr, const std::map<std::string, std::string>& pd, bool windowed_override);
bool taito_gun_ini(const Profile& pr, std::string& name, std::string& text);

// --- direct_files.cpp: view, tweede exe, drop-ins --------------------------------------------------------------------------------------
std::string view_wortel(const std::string& base, const DirectPaths& p);
bool plan_view(const std::string& name, const gsn::Ini& settings, std::string& game, const std::string& et, const DirectPaths& p, bool make,
               std::vector<std::string>& notes, std::string& err);

struct SecondStart { std::string path, profile_args, own_args; bool first = false; double wait = 5.0; };
// second_start(): 0 = een gewone game (een exe), 1 = `out` gevuld, -1 = niet direct te starten, -2 = instelling geweigerd (melding in `m`).
int second_start(const Profile& pr, const gsn::Ini& settings, const std::string& game, const DirectPaths& p, SecondStart& out, std::string& m);
std::vector<std::string> spel_argumenten(const std::string& loader, const std::vector<std::string>& profiel_delen, const std::string& eigen_ruw);

bool dropin_keten(const std::string& base_in, const std::string& dest_in, const std::string& wortel_in, std::string& wortel,
                  std::vector<std::string>& delen, std::string& err);
bool dropin_toets(const std::string& base, const std::string& dest, const std::string& wortel_in, std::string& err, std::vector<std::string>& lokaal);

struct DropinResult {
    EnvMap env;
    std::vector<std::string> overrides, preload, audit, geen_audit, errors;
    struct Kanaal { std::string file, var, value, by; bool loads = false; };
    std::vector<Kanaal> kanalen;
};
std::string find_dxvk(const DirectPaths& p, const EnvMap& env, bool is64, std::string& from);
DropinResult dropin_plan(DirectPlan& pl, const DirectPaths& p);
void merge_env(DirectPlan& pl, const EnvMap& extra);   // LD_PRELOAD en WINEDLLOVERRIDES samenvoegen, de rest overschrijven

}  // namespace direct_detail

}  // namespace ravengun::launch
