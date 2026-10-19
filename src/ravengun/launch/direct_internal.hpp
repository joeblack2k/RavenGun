#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Alleen voor de bestanden van de directe route (direct*.cpp): de helpers die ze delen. Niet voor andere code; de publieke kant is
// direct.hpp. Alles staat in ravengun::launch::direct_detail; de bestanden halen het binnen met `using namespace direct_detail;`.
// Wat maar een bestand gebruikt staat daar in een anonieme namespace.
#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/launch/direct.hpp"
#include "ravengun/strutil.hpp"

namespace ravengun::launch {
// De schrijflaag: zie direct_apply.cpp (definitie staat daar in direct_detail).

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
bool on_nas(const std::string& path, const DirectPaths& p);   // launch-direct.py on_nas(); niet te volgen = NAS, lege tabel = NAS
// Leeg = de koppelpuntentabel (p.mounts) is leesbaar en heeft regels; anders de melding (dan zegt on_nas() overal ja).
std::string koppelpunten_fout(const DirectPaths& p);
// Het pad zoals de kernel het volgt, ook door een koppeling waarvan het doel ontbreekt; false = onderweg niet te bekijken (EIO, ESTALE).
bool volg_pad(const std::string& path, std::string& out);
// Strenger dan read_file(): elke leesfout telt, en het totaal moet gelijk zijn aan de grootte van fstat() (een afgebroken lezing van
// de NAS wordt nooit een halve kopie). strict_read_fn is ::read; alleen proeven vervangen hem.
bool read_file_strict(const std::string& path, std::string& out, std::string& err);
extern ssize_t (*strict_read_fn)(int, void*, size_t);
// De koppelpunt-tabel; binnen een MountTableScope leest on_nas() hem maar een keer (apply_direct, rg-direct --run).
struct MountRow { std::string mountpoint, type; };
std::vector<MountRow> read_mounts(const std::string& file);
class MountTableScope {
public:
    explicit MountTableScope(const DirectPaths& p);
    ~MountTableScope();
    MountTableScope(const MountTableScope&) = delete;
    MountTableScope& operator=(const MountTableScope&) = delete;

private:
    std::vector<MountRow> rows_;
    std::string file_;
    const std::vector<MountRow>* prev_rows_;
    const std::string* prev_file_;
};
// De schrijflaag (zie direct_apply.cpp): mappen waar de game in schrijft worden echte mappen in de
// view, met een kopie van de NAS-inhoud; komt het pad daarna nog op de NAS uit, dan weigert de start.
// `vers_omgezet` (mag nullptr) krijgt elke schrijfmap die bij deze aanroep van een koppeling naar de NAS een echte map werd.
bool place_view_writable(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log,
                         std::string& err, std::vector<std::string>* vers_omgezet = nullptr);
// [Launch] ViewLocalFiles: in elke map van pl.view_local_files wordt elk kind dat een bestands-koppeling naar de NAS is een lokale
// kopie (dezelfde veilige kopie en grens als een schrijfmap); een koppeling naar een map blijft staan, een echte submap wordt niet
// bekeken. Weigert als de map zelf een koppeling is of op de NAS ligt, of als een kind naar iets anders op de NAS wijst. Eerst
// wordt alles nagekeken, pas daarna gekopieerd: een weigering laat de map zoals hij was.
bool place_view_local_files(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err);
// Alleen lezen, leeg = veilig: wat in een map van [Launch] ViewLocalFiles nog op de NAS uitkomt. De map zelf een koppeling of op de
// NAS, of een direct kind dat een koppeling naar de NAS is en niet naar een map wijst (de exe van het plan, `exe`, en zijn
// .ravengun-orig tellen niet). place_view_local_files (de laatste controle) en rg-direct --run gebruiken hem.
std::vector<std::string> lokale_bestanden_problemen(const std::string& map, const std::string& exe, const DirectPaths& p);
// Het pad zoals Wine het vindt: per onderdeel eerst de precieze naam, anders een naam die alleen in hoofdletters verschilt.
std::string resolve_case(const std::string& pad);
// Alleen lezen: alles waardoor een game die in `pad` schrijft op de NAS kan uitkomen (leeg = veilig). De ouder op de NAS, het pad
// zelf (een koppeling) op de NAS, een kind (diep, ook achter een koppeling naar een lokale map) dat op de NAS uitkomt, en hetzelfde
// voor een zuster die alleen in hoofdletters verschilt. place_view_writable (de laatste controle) en rg-direct --run gebruiken hem.
std::vector<std::string> schrijfmap_problemen(const std::string& pad, const DirectPaths& p);
// [Launch] PrefixSetup (review 13, ronde 2): alleen lezen, leeg = veilig. Een map waarin een prefixstap draait (werkmap) of
// schrijft (de map van een kopieerdoel): binnen de view-wortel, geen .. , bestaat als echte map, GEEN koppeling in een van de
// delen vanaf de wortel, geen zuster in andere hoofdletters die een koppeling is of op de NAS ligt (Wine is niet
// hoofdlettergevoelig), en de map zelf niet op de NAS. Anders dan schrijfmap_problemen() kijkt hij NIET naar de kinderen: de
// view-wortel staat vol koppelingen naar de NAS en is toch de werkmap van een stap. Motor, --run-controle en loper gebruiken hem.
std::vector<std::string> prefixstap_map_problemen(const std::string& map, const std::string& wortel, const DirectPaths& p);
// Een stap uit het plan zoals de loper hem uitvoert: de prefix:-bron (from_rel schoon, en er IS een absolute prefix: die van
// prefix_van_plan()), het kopieerdoel (in de view, zelf geen koppeling, de map volgens prefixstap_map_problemen) of de werkmap
// van een run-stap. Leeg = veilig.
std::vector<std::string> prefixstap_problemen(const DirectSetupStep& st, const std::string& wortel, const std::string& prefix,
                                              const DirectPaths& p);
// De controle van rg-direct --run en --alleen-stappen voor alle prefixstappen van een plan (de view-wortel uit plan.game, de
// koppelpuntentabel, elke stap). Leeg = veilig; de schrijfmappen (ViewWritable) controleert schrijfmap_problemen() apart.
// De prefix die bij dit plan hoort (plan.prefix, anders de omgevingsvariabelen uit plan.env).
std::string prefix_van_plan(const DirectPlan& plan);
// EEN prefix voor de prefixstappen (review 15, ronde 2, punt 4): de stempel, de prefix:-bron en de NAS-controle gebruiken
// prefix_van_plan(), dus de wine van de run-stappen moet in DIE prefix draaien. Een gewone wine leest WINEPREFIX, de wrapper
// wine-prefix leest RG_WINEPREFIX (scripts/wine-prefix.sh); staat daar iets anders in, dan weigert dit. Leeg = in orde (ook
// zonder run-stappen).
std::vector<std::string> prefix_omgeving_problemen(const DirectPlan& plan);
// De omgeving voor de run-stappen en wineserver: env met WINEPREFIX (en bij de wrapper ook RG_WINEPREFIX) op prefix_van_plan().
EnvMap prefixstap_omgeving(const DirectPlan& plan, const EnvMap& env);
// Alleen de stempel van de prefixstappen (review 14, punt 3; review 15, punt 4).
std::vector<std::string> stempel_problemen(const DirectPlan& plan, const DirectPaths& p);
std::vector<std::string> prefixstappen_problemen(const DirectPlan& plan, const DirectPaths& p);
// Schrijft een bestand zonder een koppeling te volgen: de tijdelijke naam wordt met O_EXCL|O_NOFOLLOW
// aangemaakt en daarna met rename() op zijn plaats gezet (rename vervangt de naam zelf, ook als daar
// een koppeling staat). rg-direct gebruikt hem voor de kopieerstap van [Launch] PrefixSetup.
bool write_atomic(const std::string& path, const std::string& data, std::string& err);

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
