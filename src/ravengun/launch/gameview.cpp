// SPDX-License-Identifier: GPL-3.0-or-later
// De view van een game; zie gameview.hpp. Elke functie hier heeft een tegenhanger met dezelfde naam (of een vertaling ervan) in
// scripts/gameview.py, die tp-add-game.py --view en launch-direct.py gebruiken; tests/test_gameview.py vergelijkt de bomen.
#include "ravengun/launch/gameview.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace ravengun::launch {

namespace fs = std::filesystem;

namespace {

// Mappen die in de wortel van de view LOKAAL gekopieerd worden in plaats van gelinkt (LOKAAL_MAPPEN): BepInEx/ is ons artefact
// (onze plugin met config, cache en log) en hoort nooit via een symlink op de NAS (gemeten 2026-10-11, Rabbids Hollywood).
bool lokaal_map(const std::string& name) {
    std::string l = name;
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l == "bepinex";
}

std::string pjoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}
std::string parent_of(const std::string& p) {
    const fs::path a = fs::path(p).lexically_normal();
    std::string s = a.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return fs::path(s).parent_path().string();
}
bool lexists(const std::string& p) {
    struct stat st{};
    return ::lstat(p.c_str(), &st) == 0;
}
bool islink(const std::string& p) {
    struct stat st{};
    return ::lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}
bool isdir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool exists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}
std::string realpath_(const std::string& p) {
    std::error_code ec;
    const fs::path r = fs::weakly_canonical(fs::path(p), ec);
    return ec ? p : r.string();
}
bool listdir(const std::string& d, std::vector<std::string>& out) {
    out.clear();
    std::error_code ec;
    fs::directory_iterator it(d, ec);
    if (ec) return false;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) return false;
        out.push_back(it->path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return true;
}
std::vector<std::string> split_rel(const std::string& rel) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= rel.size()) {
        const size_t b = rel.find('/', a);
        const std::string part = rel.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty() && part != ".") out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// Een schrijfstap in `doel`: de map erboven moet lokaal zijn (gevolgd). Zo komt er nooit iets op de NAS, ook niet via een link
// van een vorige keer.
struct Guard {
    const NasFn& on_nas;
    std::string& err;
    bool ok(const std::string& doel) {
        const std::string ouder = parent_of(doel);
        if (!on_nas(ouder)) return true;
        err = doel + ": de map erboven komt uit op " + realpath_(ouder) + " en dat is de NAS; daar wordt nooit geschreven";
        return false;
    }
};

bool do_mkdirs(const std::string& p, Guard& g) {
    if (isdir(p)) return true;
    if (lexists(p)) { g.err = p + " bestaat al en is geen map"; return false; }
    if (!g.ok(p)) return false;
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) { g.err = p + ": " + ec.message(); return false; }
    return true;
}
bool do_unlink(const std::string& p, Guard& g) {
    if (!g.ok(p)) return false;
    if (::unlink(p.c_str()) != 0) { g.err = p + ": " + std::strerror(errno); return false; }
    return true;
}
bool do_symlink(const std::string& target, const std::string& p, Guard& g) {
    if (!g.ok(p)) return false;
    if (::symlink(target.c_str(), p.c_str()) != 0) { g.err = p + ": " + std::strerror(errno); return false; }
    return true;
}
bool do_copy(const std::string& src, const std::string& dst, Guard& g) {
    if (!g.ok(dst)) return false;
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);   // shutil.copyfile + copymode: inhoud en rechten
    if (ec) { g.err = dst + ": " + ec.message(); return false; }
    return true;
}

// kopieer_lokaal(): dst een echte map met echte kopieen van src (recursief). Bestaande lokale bestanden blijven staan; een symlink
// op die plek wordt vervangen (alleen de link zelf).
bool kopieer_lokaal(const std::string& src, const std::string& dst, Guard& g) {
    if (islink(dst) && !do_unlink(dst, g)) return false;
    if (!do_mkdirs(dst, g)) return false;
    std::vector<std::string> names;
    if (!listdir(src, names)) { g.err = src + " is niet te lezen"; return false; }
    for (const std::string& e : names) {
        const std::string sp = pjoin(src, e), dp = pjoin(dst, e);
        if (islink(dp) && !do_unlink(dp, g)) return false;
        if (isdir(sp)) { if (!kopieer_lokaal(sp, dp, g)) return false; }
        else if (!lexists(dp) && !do_copy(sp, dp, g)) return false;
    }
    return true;
}

// link_tree(): elke ingang van src als link in dst; een pad uit `empties` wordt een lege map en de mappen erlangs echte mappen met
// links voor de rest. Op het hoogste niveau wordt BepInEx/ lokaal gekopieerd. Afwijking van het oude tp-add-game.py: een link op de
// plek van een map die echt moet worden, wordt eerst weggehaald (anders kwamen de links erin op de NAS terecht); de Python-tweeling
// doet hetzelfde.
bool link_tree(const std::string& src, const std::string& dst, const std::vector<std::vector<std::string>>& empties, bool top, Guard& g) {
    if (!top && islink(dst) && !do_unlink(dst, g)) return false;
    if (!do_mkdirs(dst, g)) return false;
    std::vector<std::string> names;
    if (!listdir(src, names)) { g.err = src + " is niet te lezen"; return false; }
    for (const std::string& e : names) {
        const std::string sp = pjoin(src, e), dp = pjoin(dst, e);
        const bool first = std::any_of(empties.begin(), empties.end(), [&](const auto& x) { return !x.empty() && x[0] == e; });
        if (first) {
            std::vector<std::vector<std::string>> sub;
            bool leeg = false;
            for (const auto& x : empties)
                if (!x.empty() && x[0] == e) {
                    if (x.size() == 1) leeg = true;
                    else sub.emplace_back(x.begin() + 1, x.end());
                }
            if (leeg) {
                if (islink(dp) && !do_unlink(dp, g)) return false;
                if (!do_mkdirs(dp, g)) return false;
            } else if (!link_tree(sp, dp, sub, false, g)) {
                return false;
            }
        } else if (top && lokaal_map(e) && isdir(sp)) {
            if (!kopieer_lokaal(sp, dp, g)) return false;
        } else {
            if (islink(dp) && !do_unlink(dp, g)) return false;              // een vorige keer
            if (!lexists(dp) && !do_symlink(sp, dp, g)) return false;      // een echte map van een vorige keer blijft staan
        }
    }
    return true;
}

// maak_keten_lokaal(): de map van de exe en elke map erboven tot de wortel wordt een ECHTE map met links naar de inhoud.
bool maak_keten_lokaal(const std::string& src, const std::string& view, const std::string& rel_dir, std::vector<std::string>& gedaan, Guard& g) {
    std::string sp = src, dp = view;
    for (const std::string& deel : split_rel(rel_dir)) {
        sp = pjoin(sp, deel);
        dp = pjoin(dp, deel);
        if (islink(dp) && !do_unlink(dp, g)) return false;
        if (!do_mkdirs(dp, g)) return false;
        std::vector<std::string> names;
        listdir(dp, names);
        for (const std::string& e : names)
            if (islink(pjoin(dp, e)) && !do_unlink(pjoin(dp, e), g)) return false;
        if (!listdir(sp, names)) { g.err = sp + " is niet te lezen"; return false; }
        for (const std::string& e : names)
            if (!lexists(pjoin(dp, e)) && !do_symlink(pjoin(sp, e), pjoin(dp, e), g)) return false;
        gedaan.push_back(dp);
    }
    return true;
}

std::string rel_dir_of(const std::string& exe_rel) {
    const size_t i = exe_rel.rfind('/');
    return i == std::string::npos ? std::string() : exe_rel.substr(0, i);
}

std::string strip_slashes(std::string s) {
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

}  // namespace

bool make_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err) {
    Guard g{on_nas, err};
    if (on_nas(s.view)) { err = "de view " + s.view + " komt uit op " + realpath_(s.view) + " en dat is de NAS; een view is altijd een lokale map"; return false; }
    if (islink(s.view)) { err = "de view " + s.view + " is zelf een symlink (naar " + realpath_(s.view) + ")"; return false; }
    if (!do_mkdirs(s.view, g)) return false;
    std::vector<std::string> names;
    listdir(s.view, names);
    for (const std::string& e : names) {   // links en lege mappen van een vorige keer
        const std::string pth = pjoin(s.view, e);
        std::vector<std::string> inhoud;
        if (islink(pth)) { if (!do_unlink(pth, g)) return false; }
        else if (isdir(pth) && e != "TeknoParrot" && listdir(pth, inhoud) && inhoud.empty()) {
            if (!g.ok(pth)) return false;
            ::rmdir(pth.c_str());
        }
    }
    std::vector<std::vector<std::string>> empties;
    for (const std::string& e : s.empty_dirs) empties.push_back(split_rel(strip_slashes(e)));
    if (!link_tree(s.source, s.view, empties, true, g)) return false;
    std::vector<std::string> gedaan;
    if (!maak_keten_lokaal(s.source, s.view, rel_dir_of(s.exe_rel), gedaan, g)) return false;
    for (const std::string& d : gedaan)
        log.push_back("view: " + fs::path(d).lexically_relative(s.view).string() + "/ is een echte map met symlinks (de map van de exe hoort lokaal te zijn)");
    for (const std::string& r : s.real_dirs) {
        gedaan.clear();
        if (!maak_keten_lokaal(s.source, s.view, strip_slashes(r), gedaan, g)) return false;
        log.push_back("view: " + strip_slashes(r) + "/ is een echte map met symlinks (ViewRealDirs)");
    }
    if (!s.elfldr2.empty() && !lexists(pjoin(s.view, "ElfLdr2")) && !do_symlink(s.elfldr2, pjoin(s.view, "ElfLdr2"), g)) return false;
    const std::vector<std::string> fout = view_write_errors(s, on_nas);
    if (!fout.empty()) {
        err = "schrijfpaden in de view die op de NAS uitkomen (dat mag nooit):";
        for (const std::string& f : fout) err += "\n  " + f;
        return false;
    }
    log.push_back("view: " + s.view + " gemaakt (links naar " + s.source + ")");
    return true;
}

bool refresh_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err) {
    Guard g{on_nas, err};
    if (on_nas(s.view)) { err = "de view " + s.view + " komt uit op " + realpath_(s.view) + " en dat is de NAS"; return false; }
    std::vector<std::pair<std::string, std::string>> paren{{s.view, s.source}};
    for (const std::string& deel : split_rel(rel_dir_of(s.exe_rel)))
        paren.emplace_back(pjoin(paren.back().first, deel), pjoin(paren.back().second, deel));
    // ViewRealDirs: een pad dat (nog) een link is wordt een echte map met links; daarna ververst zoals de keten naar de exe.
    std::vector<std::pair<std::string, std::string>> echte;
    for (const std::string& r : s.real_dirs) {
        std::string vd = s.view, sd = s.source;
        bool echt = true;
        for (const std::string& deel : split_rel(strip_slashes(r))) {
            vd = pjoin(vd, deel);
            sd = pjoin(sd, deel);
            if (islink(vd) || !isdir(vd)) echt = false;
        }
        if (!echt) {
            std::vector<std::string> gedaan;
            if (!maak_keten_lokaal(s.source, s.view, strip_slashes(r), gedaan, g)) return false;
            log.push_back("view: " + strip_slashes(r) + "/ is een echte map met symlinks geworden (ViewRealDirs)");
        }
        echte.emplace_back(vd, sd);
    }
    int bij = 0, weg = 0;
    // ViewEmptyDirs op een bestaande view: elk pad wordt een lege echte map. Een symlink gaat weg (alleen de link, nooit wat erachter
    // staat), een ontbrekend pad wordt aangemaakt, een echte map (leeg of met inhoud) blijft zoals hij is. De mappen erboven moeten
    // echte mappen zijn; achter een symlink is het niet van ons.
    for (const std::string& e : s.empty_dirs) {
        std::string p = s.view;
        const std::vector<std::string> delen = split_rel(strip_slashes(e));
        for (size_t k = 0; k < delen.size(); ++k) {
            p = pjoin(p, delen[k]);
            const bool laatste = k + 1 == delen.size();
            if (!laatste) { if (islink(p) || !isdir(p)) break; continue; }
            if (islink(p)) { if (!do_unlink(p, g)) return false; ++weg; }
            else if (lexists(p)) break;
            if (!do_mkdirs(p, g)) return false;
        }
    }
    std::set<std::string> leeg_bedoeld;
    for (const std::string& e : s.empty_dirs) leeg_bedoeld.insert(strip_slashes(e));
    /* Review 10, A6: binnen een schrijfmap ([Launch] ViewWritable) komt nooit een link naar de NAS.
     * Anders zet de verversing bijvoorbeeld SwordGame/Logs/Launch.log weer als link neer en schrijft
     * de game via die link op de NAS. */
    auto in_writable = [&](const std::string& rel) {
        for (const std::string& w : s.writable_dirs) {
            const std::string ww = strip_slashes(w);
            if (!ww.empty() && (rel == ww || rel.compare(0, ww.size() + 1, ww + "/") == 0)) return true;
        }
        return false;
    };
    // Een echte map van de view verversen: links bij voor wat er in de gamemap bij kwam, verdwenen links weg.
    auto ververs = [&](const std::string& vd, const std::string& sd, bool wortel) -> bool {
        std::vector<std::string> names;
        if (listdir(sd, names))
            for (const std::string& e : names) {
                if (wortel && lokaal_map(e)) continue;
                const std::string dp = pjoin(vd, e);
                // Een LEGE echte map die niet in ViewEmptyDirs staat, waar de gamemap een map met inhoud heeft, is een overblijfsel
                // (Bug Busters: video/ van een eerdere proef met ViewEmptyDirs; het spel vond zijn films niet en viel, 2026-10-15).
                // Die wordt weer een link. Een map met inhoud, of een die bewust leeg is, blijft staan.
                if (lexists(dp) && !islink(dp) && isdir(dp)) {
                    const std::string rel = fs::path(dp).lexically_relative(s.view).generic_string();
                    if (in_writable(rel)) continue;   // een schrijfmap blijft een echte map
                    std::vector<std::string> in_view, in_bron;
                    if (!leeg_bedoeld.count(rel) && listdir(dp, in_view) && in_view.empty() && isdir(pjoin(sd, e)) &&
                        listdir(pjoin(sd, e), in_bron) && !in_bron.empty()) {
                        std::error_code ec;
                        if (!fs::remove(dp, ec) || ec) { err = "kan de lege map " + dp + " niet weghalen: " + ec.message(); return false; }
                        if (!do_symlink(pjoin(sd, e), dp, g)) return false;
                        ++bij;
                        log.push_back("view: " + rel + "/ was een lege map zonder ViewEmptyDirs; weer een link naar de gamemap");
                    }
                    continue;
                }
                if (lexists(dp)) continue;
                if (in_writable(fs::path(dp).lexically_relative(s.view).generic_string())) {
                    log.push_back("view: " + e + " valt binnen een schrijfmap; geen link naar de NAS");
                    continue;
                }
                if (!do_symlink(pjoin(sd, e), dp, g)) return false;
                ++bij;
            }
        listdir(vd, names);
        for (const std::string& e : names) {
            const std::string dp = pjoin(vd, e);
            if (!islink(dp) || exists(dp)) continue;
            std::error_code ec;
            const std::string doel = fs::read_symlink(dp, ec).string();
            if (ec || doel.compare(0, sd.size() + 1, sd + "/") != 0) continue;   // alleen links naar de gamemap zelf
            if (!do_unlink(dp, g)) return false;
            ++weg;
        }
        return true;
    };
    for (size_t i = 0; i < paren.size(); ++i) {
        const auto& [vd, sd] = paren[i];
        if (islink(vd) || !isdir(vd)) break;   // geen echte map (meer): wat eronder zit is niet van ons
        if (!ververs(vd, sd, i == 0)) return false;
    }
    for (const auto& [vd, sd] : echte)
        if (!ververs(vd, sd, false)) return false;
    if (bij || weg) log.push_back("view: " + s.view + " ververst (" + std::to_string(bij) + " links bij, " + std::to_string(weg) + " verdwenen links weg)");
    // Geen view_write_errors() hier: een bestaande view blijft zoals hij is (ook een met de hand gemaakte waarin de map van de exe nog
    // een link is; de drop-ins maken die zelf lokaal en teknoparrot.ini wordt dan niet op de NAS geschreven). Verversen schrijft zelf
    // alleen links in echte lokale mappen (Guard).
    return true;
}

std::vector<std::string> view_write_errors(const GameViewSpec& s, const NasFn& on_nas) {
    std::vector<std::string> fouten;
    if (on_nas(s.view)) return {"de view " + s.view + " ligt op de NAS"};
    if (islink(s.view)) return {"de view " + s.view + " is een symlink naar " + realpath_(s.view)};
    auto check = [&](const std::string& p) {
        if (islink(p) && on_nas(p)) fouten.push_back(p + " -> " + realpath_(p));
        else if (on_nas(p)) fouten.push_back(p + " ligt op de NAS");
    };
    std::function<void(const std::string&)> boom = [&](const std::string& p) {
        check(p);
        std::vector<std::string> names;
        if (isdir(p) && !islink(p) && listdir(p, names))
            for (const std::string& e : names) boom(pjoin(p, e));
    };
    std::string d = s.view;
    for (const std::string& deel : split_rel(rel_dir_of(s.exe_rel))) {   // hier komt teknoparrot.ini
        d = pjoin(d, deel);
        if (lexists(d)) check(d);
    }
    std::vector<std::string> names;
    listdir(s.view, names);
    for (const std::string& e : names)
        if (lokaal_map(e)) boom(pjoin(s.view, e));
    for (const std::string& e : s.empty_dirs) {
        const std::string p = pjoin(s.view, strip_slashes(e));
        if (lexists(p)) check(p);
    }
    return fouten;
}

bool parse_view_setting(const std::string& view_in, const std::string& empty_in, ViewSetting& out, std::string& err, const std::string& real_in) {
    out = ViewSetting{};
    std::string v;
    for (char c : view_in)
        if (!std::isspace(static_cast<unsigned char>(c))) v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v.empty() || v == "auto") out.mode = ViewSetting::Mode::Auto;
    else if (v == "off" || v == "false" || v == "no") out.mode = ViewSetting::Mode::Off;
    else if (v.size() <= 2 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        out.mode = ViewSetting::Mode::Levels;
        out.up = std::stoi(v);
    } else {
        err = "[Launch] View = " + view_in + " (auto, off, of het aantal mappen boven de map van de exe waar de view begint)";
        return false;
    }
    // Een lijst paden ten opzichte van de view-wortel, gescheiden door komma of puntkomma; `..` mag niet.
    auto paden = [&](const std::string& key, const std::string& in, std::vector<std::string>& uit) -> bool {
        std::string cur;
        auto flush = [&]() -> bool {
            size_t a = 0, b = cur.size();
            while (a < b && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
            const std::string t = strip_slashes(cur.substr(a, b - a));
            cur.clear();
            if (t.empty()) return true;
            for (const std::string& deel : split_rel(t))
                if (deel == "..") { err = "[Launch] " + key + " = " + in + ": '" + t + "' gaat buiten de view"; return false; }
            uit.push_back(t);
            return true;
        };
        for (char c : in) {
            if (c == ',' || c == ';') { if (!flush()) return false; }
            else cur += c;
        }
        return flush();
    };
    return paden("ViewEmptyDirs", empty_in, out.empty_dirs) && paden("ViewRealDirs", real_in, out.real_dirs);
}

}  // namespace ravengun::launch
