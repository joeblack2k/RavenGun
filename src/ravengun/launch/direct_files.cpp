// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10): drop-ins (dropin.ini, DXVK, plaatsen in de view), de view ([Launch] View) en de tweede exe (HasTwoExecutables).
// Zie direct.hpp; de helpers die meer bestanden delen staan in direct_internal.hpp.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/direct_internal.hpp"
#include "ravengun/launch/gameview.hpp"

namespace ravengun::launch {

namespace direct_detail {

// --- drop-ins (dropin_specs, dropin_toets, dropin_plan) ---------------------------------------------------------------------------
namespace {

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
            if (to != "." && naam.starts_with(tos + "/")) {
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

}  // namespace

std::string view_wortel(const std::string& base, const DirectPaths& p) {
    const std::string views = abspath(p.views()), b = abspath(base);
    if (b != views && binnen(b, views)) return pjoin(views, split(relpath(b, views), '/')[0]);
    return b;
}

// --- de view ([Launch] View; launch-direct.py plan_view, de opbouw in gameview.cpp) ------------------------------------------------
// Het pad van de game waarmee het plan verder rekent. auto: staat de game op de NAS en is er nog geen view, dan wordt views/<Profiel>
// gemaakt zoals tp-add-game.py --view (de wortel waar [Identify] Path begint); een getal N: altijd een view, N mappen omhoog; off: de
// oude route. Een bestaande view met deze game blijft staan, alleen de links worden ververst. Zonder o.make_view (--plan) wordt er
// niets gemaakt. false + err = weigeren (een ongeldige instelling, een view die er al is zonder deze game, of iets op de NAS).
bool plan_view(const std::string& name, const gsn::Ini& settings, std::string& game, const std::string& et, const DirectPaths& p, bool make,
               std::vector<std::string>& notes, std::string& err) {
    ViewSetting vs;
    if (!parse_view_setting(settings.get("Launch", "View"), settings.get("Launch", "ViewEmptyDirs"), vs, err)) return false;
    if (vs.mode == ViewSetting::Mode::Off) return true;
    const NasFn nas = [&p](const std::string& x) { return on_nas(x, p); };
    const std::string views = p.views();
    auto log_into = [&](const std::vector<std::string>& l) { notes.insert(notes.end(), l.begin(), l.end()); };
    if (binnen(game, views) && abspath(game) != abspath(views)) {   // al in een view: alleen de links verversen
        const std::string wortel = view_wortel(dirname(game), p), rel = relpath(abspath(game), wortel), echt = realpath_(game);
        if (make && echt.ends_with("/" + rel) && !islink(wortel)) {
            std::vector<std::string> l;
            if (!refresh_game_view({echt.substr(0, echt.size() - rel.size() - 1), wortel, rel, vs.empty_dirs, {}}, nas, l, err)) {
                err = "view: " + err;
                return false;
            }
            log_into(l);
        }
        return true;
    }
    if (vs.mode == ViewSetting::Mode::Auto && !on_nas(game, p)) return true;
    int n = vs.up;
    if (vs.mode == ViewSetting::Mode::Auto) {
        const std::string idp = gsn::identify_of(settings).path;
        n = 0;
        if (!idp.empty() && game.ends_with("/" + idp))
            for (const std::string& d : split(dirname(idp), '/')) n += d.empty() ? 0 : 1;
    }
    std::string gdir = dirname(abspath(game));
    for (int i = 0; i < n; ++i) gdir = dirname(gdir);
    const std::string rel = relpath(abspath(game), gdir), view = pjoin(views, name), vgame = pjoin(view, rel);
    const GameViewSpec spec{gdir, view, rel, vs.empty_dirs, et == "ElfLdr2" ? pjoin(p.tp, "ElfLdr2") : std::string()};
    bool leeg = false;
    if (isdir(view) && !islink(view)) {
        std::error_code ec;
        leeg = fs::is_empty(view, ec) && !ec;
    }
    if (lexists(view) && !leeg) {
        if (isfile(vgame) && samefile(vgame, game)) {
            if (make) {
                std::vector<std::string> l;
                if (!refresh_game_view(spec, nas, l, err)) { err = "view: " + err; return false; }
                log_into(l);
            }
            game = vgame;
            return true;
        }
        err = "view: " + view + " bestaat al maar bevat de game niet op " + rel + "; de view blijft ongemoeid (maak hem opnieuw met "
              "scripts/tp-add-game.py " + name + " <pad> --view, of zet [Launch] View = off)";
        return false;
    }
    if (!make) {
        notes.push_back("view: " + view + " ontbreekt; wordt bij de start gemaakt (links naar " + gdir + ")");
        return true;
    }
    std::vector<std::string> l;
    if (!make_game_view(spec, nas, l, err)) { err = "view: " + err; return false; }
    log_into(l);
    game = vgame;
    return true;
}

// --- twee exe's (in_dezelfde_view, second_start) -------------------------------------------------------------------------------------
namespace {

// Het pad van de tweede exe BINNEN de view van de eerste; leeg + reden als hij erbuiten ligt (zie launch-direct.py in_dezelfde_view).
std::string in_dezelfde_view(const std::string& game, const std::string& second, const DirectPaths& p, std::string& waarom) {
    const std::string wortel = view_wortel(dirname(game), p);
    if (wortel == abspath(dirname(game))) return second;   // de eerste staat niet in een view: er valt niets gelijk te trekken
    if (binnen(second, wortel)) return second;
    const std::string rel = relpath(abspath(game), wortel);
    const std::string echt = realpath_(game);
    if (echt.ends_with("/" + rel)) {   // waar de view naar wijst: de map van het spel zelf
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

}  // namespace

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
            if (!path.empty() && game.ends_with("/" + path)) {
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

namespace {

std::string lader_naam(const DirectPlan& pl) {
    auto it = std::find(pl.argv.begin(), pl.argv.end(), pl.wine);
    if (!pl.wine.empty() && it != pl.argv.end() && it + 1 != pl.argv.end()) return basename(*(it + 1));
    return {};
}

// [Launch] Dxvk: de DLL's die DXVK vervangt; een map telt pas als ze er allemaal staan.
const char* const kDxvkDlls[] = {"d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"};

bool dxvk_compleet(const std::string& dir) {
    for (const char* d : kDxvkDlls) if (!isfile(pjoin(dir, d))) return false;
    return true;
}

}  // namespace

// Waar DXVK staat, in deze volgorde: RAVENGUN_DXVK (een map met x32/ en x64/), ~/.local/share/ravengun/dxvk/current
// (scripts/fetch-dxvk.sh), en anders de nieuwste Proton van Steam (compatibilitytools.d/*, steamapps/common/Proton*:
// files/lib/wine/dxvk/{i386,x86_64}-windows). `from` zegt welke het werd, met de versie als die erbij staat.
std::string find_dxvk(const DirectPaths& p, const EnvMap& env, bool is64, std::string& from) {
    const std::string sub = is64 ? "x64" : "x32", wsub = is64 ? "x86_64-windows" : "i386-windows";
    auto versie = [](const std::string& f) {
        std::string t;
        if (!read_file(f, t)) return std::string();
        t = trim(t.substr(0, t.find('\n')));
        return t.empty() ? std::string() : " (" + t + ")";
    };
    for (const std::string& root : {getenv_or(env, "RAVENGUN_DXVK"), p.home + "/.local/share/ravengun/dxvk/current"}) {
        if (root.empty() || !dxvk_compleet(pjoin(root, sub))) continue;
        from = realpath_(root) + versie(pjoin(root, "version"));
        return pjoin(root, sub);
    }
    std::vector<std::string> kandidaten;
    for (const std::string& base : {p.home + "/.local/share/Steam/compatibilitytools.d", p.home + "/.local/share/Steam/steamapps/common"}) {
        DIR* d = ::opendir(base.c_str());
        if (!d) continue;
        while (dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            if (base.ends_with("/common") && !n.starts_with("Proton")) continue;
            const std::string dir = base + "/" + n + "/files/lib/wine/dxvk";
            if (dxvk_compleet(pjoin(dir, wsub))) kandidaten.push_back(dir);
        }
        ::closedir(d);
    }
    if (kandidaten.empty()) return {};
    // De nieuwste: de hoogste versie in het versiebestand van DXVK ("<hash> dxvk (v2.7.1-509-g...)"), anders de naam.
    auto sleutel = [&](const std::string& dir) {
        std::string t;
        read_file(pjoin(dir, "version"), t);
        const size_t v = t.find("(v");
        std::vector<int> nums;
        if (v != std::string::npos) {
            int cur = -1;
            for (size_t i = v + 2; i < t.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(t[i]))) cur = (cur < 0 ? 0 : cur * 10) + (t[i] - '0');
                else { if (cur >= 0) nums.push_back(cur); cur = -1; if (t[i] != '.' && t[i] != '-') break; }
            }
            if (cur >= 0) nums.push_back(cur);
        }
        return std::make_pair(nums, dir);
    };
    std::sort(kandidaten.begin(), kandidaten.end(), [&](const std::string& a, const std::string& b) { return sleutel(a) > sleutel(b); });
    const std::string dir = kandidaten.front();
    std::string t;
    read_file(pjoin(dir, "version"), t);
    const size_t v = t.find("(v"), e = t.find(')', v == std::string::npos ? 0 : v);
    from = dir + (v != std::string::npos && e != std::string::npos ? " (dxvk " + t.substr(v + 1, e - v - 1) + ")" : "");
    return pjoin(dir, wsub);
}

DropinResult dropin_plan(DirectPlan& pl, const DirectPaths& p) {
    DropinResult r;
    std::vector<DropinSpec> specs = dropin_specs(pl.profile, p, r.errors);
    // [Launch] Dxvk: de DLL's van DXVK als drop-ins (een eigen drop-in met dezelfde naam in dropin/ wint).
    if (!pl.dxvk_dir.empty())
        for (const char* d : kDxvkDlls) {
            if (std::any_of(specs.begin(), specs.end(), [&](const DropinSpec& s) { return lower(s.name) == d; })) continue;
            DropinSpec s;
            s.name = d;
            s.to = ".";
            s.dll = "native";
            s.ini = "[Launch] Dxvk";
            s.src = pjoin(pl.dxvk_dir, d);
            specs.push_back(std::move(s));
        }
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
            const std::string dllnaam = lower(s.name).ends_with(".dll") ? s.name.substr(0, s.name.size() - 4) : s.name;
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
    if (!pl.dxvk_dir.empty()) pl.dropin_lines.push_back("dxvk: " + pl.dxvk_dir + " uit " + pl.dxvk_from);
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

}  // namespace direct_detail

using namespace direct_detail;

bool parse_view_links(const std::string& text, const std::string& base, std::vector<std::pair<std::string, std::string>>& out,
                      std::string& err) {
    out.clear();
    for (const std::string& ruw : split(text, ',')) {
        const std::string item = trim(ruw);
        if (item.empty()) continue;
        const size_t pijl = item.find("->");
        if (pijl == std::string::npos || item.find("->", pijl + 2) != std::string::npos) {
            err = "'" + item + "': verwacht <link> -> <doel>, meerdere gescheiden door komma's";
            return false;
        }
        std::string rel[2] = {trim(item.substr(0, pijl)), trim(item.substr(pijl + 2))};
        for (std::string& r : rel) {
            std::replace(r.begin(), r.end(), '\\', '/');
            const std::string n = normpath(r);
            if (r.empty() || r[0] == '/' || n == "." || n == ".." || n.starts_with("../")) {
                err = "'" + r + "': een pad binnen de gamemap, relatief en zonder ..";
                return false;
            }
            r = n;
        }
        if (rel[0] == rel[1]) { err = "'" + item + "': link en doel zijn hetzelfde"; return false; }
        out.emplace_back(normpath(pjoin(base, rel[0])), normpath(pjoin(base, rel[1])));
    }
    if (out.empty()) { err = "geen link"; return false; }
    return true;
}

}  // namespace ravengun::launch
