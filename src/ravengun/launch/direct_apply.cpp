// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10): het plan uitvoeren (apply_direct: drop-ins, bestanden, view-links, native DLL's en de stapelkopie plaatsen) en
// het plan als JSON voor rg-direct (plan_to_json, plan_from_json). Zie direct.hpp.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/direct_internal.hpp"
#include "ravengun/launch/native.hpp"
#include "ravengun/launch/pe_stack.hpp"
#include "ravengun/launch/ttc.hpp"

namespace ravengun::launch {

using namespace direct_detail;

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

// [Launch] StackReserve (launch-direct.py place_stack_copy): de exe in de view wordt een lokale kopie van het origineel met een
// grotere SizeOfStackReserve. De koppeling naar het origineel blijft als <exe>.ravengun-orig staan, zodat een volgende start weer
// van het origineel uitgaat. Al groot genoeg of al actueel: niets schrijven.
bool place_stack_copy(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    if (pl.stack_reserve <= 0 && !pl.pe_fill_pages) return true;
    const uint64_t want = static_cast<uint64_t>(std::max(0ll, pl.stack_reserve));
    const std::string& dest = pl.stack_dest;
    auto fout = [&](const std::string& m) {
        log.push_back("stapel: FOUT: " + m + " -- de game wordt niet gestart");
        err = "stapel: " + m;
        return false;
    };
    if (dest.empty() || pl.stack_src.empty()) return fout("plan zonder bron of doel");
    if (on_nas(dirname(dest), p)) return fout(dest + " ligt op de NAS; daar wordt nooit een exe geschreven");
    std::string data;
    if (!read_file(pl.stack_src, data)) return fout(pl.stack_src + " is niet te lezen");
    const uint32_t kop_crc = crc32_bytes(data, kHeaderCrcLength);
    uint64_t was = 0;
    std::string perr;
    bool stapel = false;
    if (pl.stack_reserve > 0) {
        if (!pe_stack_reserve(data, was, perr)) return fout(pl.stack_src + ": " + perr);
        stapel = was < want;
    }
    std::vector<std::string> secties;
    if (pl.pe_fill_pages && !pe_fill_section_pages(data, secties, perr)) return fout(pl.stack_src + ": " + perr);
    if (!stapel && secties.empty()) {
        if (pl.stack_reserve > 0)
            log.push_back("stapel: " + basename(dest) + " heeft al SizeOfStackReserve " + stack_size_text(was) + " (gevraagd " +
                          stack_size_text(want) + ")" + (pl.pe_fill_pages ? " en geen sectie met data achter SizeOfRawData" : "") + "; niets geschreven");
        else
            log.push_back("stapel: " + basename(dest) + " heeft geen sectie met data achter SizeOfRawData (PeFillPages); niets geschreven");
        return true;
    }
    // De CRC32 van de eerste 0x400 bytes (van het origineel, kop_crc hierboven) blijft gelijk: daaraan herkent de kern de game
    // (pe_stack.hpp, pe_keep_header_crc).
    if ((stapel && !pe_set_stack_reserve(data, want, perr)) || !pe_keep_header_crc(data, kop_crc, perr)) return fout(pl.stack_src + ": " + perr);
    std::string cur;
    if (!islink(dest) && isfile(dest) && read_file(dest, cur) && cur == data) {
        log.push_back("stapel: " + dest + " is al actueel (" + (stapel ? "SizeOfStackReserve " + stack_size_text(want) : std::string("secties tot de bladzijde")) +
                      ", bron " + pl.stack_src + ")");
        return true;
    }
    const std::string orig = dest + kStackOrigSuffix;
    if (islink(dest)) {
        if (!lexists(orig)) {
            if (::rename(dest.c_str(), orig.c_str()) != 0) return fout(dest + " -> " + orig + ": " + std::strerror(errno));
        } else if (::unlink(dest.c_str()) != 0) {
            return fout(dest + ": " + std::strerror(errno));
        }
    }
    std::string werr;
    if (!write_atomic(dest, data, werr)) return fout(werr);
    ::chmod(dest.c_str(), 0755);
    char crc_hex[16];
    std::snprintf(crc_hex, sizeof crc_hex, "%08x", kop_crc);
    std::string wat;
    if (stapel) wat = " met SizeOfStackReserve " + stack_size_text(was) + " -> " + stack_size_text(want);
    if (!secties.empty()) {
        wat += std::string(stapel ? " en" : " met") + " secties tot de bladzijde (";
        for (size_t i = 0; i < secties.size(); ++i) wat += (i ? ", " : "") + secties[i];
        wat += ")";
    }
    log.push_back("stapel: " + dest + " geschreven: lokale kopie van " + pl.stack_src + wat + ", CRC32 van de kop gelijk (" + crc_hex + ")" +
                  (islink(orig) ? "; het origineel blijft bereikbaar als " + basename(orig) : ""));
    return true;
}

// [Launch] ViewLinks (launch-direct.py place_view_links): de link in de view wordt een symlink naar het bestand waar het doel naar
// wijst (realpath). De mappen op de weg worden eerst echte lokale mappen (zoals bij een drop-in); een echt bestand op de plaats van de
// link blijft staan (dan niet starten). Op de NAS verandert niets: alleen een symlink in een lokale map wordt vervangen.
// Een link in de view naar `bron` (een absoluut pad); `stil` = "al actueel" niet melden (de mod heeft er honderd).
bool link_in_view(const DirectPlan& pl, const DirectPaths& p, const std::string& link, const std::string& bron, const std::string& tag, bool stil,
                  std::vector<std::string>& log, std::string& err) {
    auto fout = [&](const std::string& m) {
        log.push_back(tag + ": FOUT: " + m + " -- de game wordt niet gestart");
        err = tag + ": " + m;
        return false;
    };
    std::string waarom;
    std::vector<std::string> lokaal;
    if (!dropin_toets(pl.dropin_base, link, pl.dropin_root, waarom, lokaal)) return fout(link + ": " + waarom);
    if (lokaal.empty() && islink(link)) {
        std::error_code ec;
        if (fs::read_symlink(link, ec).string() == bron && !ec) {
            if (!stil) log.push_back(tag + ": " + link + " is al actueel (-> " + bron + ")");
            return true;
        }
    }
    if (lokaal.empty() && lexists(link) && !islink(link)) return fout(link + " is een echt bestand in de view; niet vervangen");
    std::string msg;
    if (!dropin_materialiseer(pl.dropin_base, link, pl.dropin_root, log, msg)) return fout(msg);
    if (on_nas(dirname(link), p)) return fout(dirname(link) + " ligt op de NAS; daar wordt nooit een link gezet");
    if (lexists(link)) return fout(link + " is een echt bestand in de view; niet vervangen");
    if (::symlink(bron.c_str(), link.c_str()) != 0) return fout(link + ": " + std::strerror(errno));
    if (!stil) log.push_back(tag + ": " + link + " -> " + bron + " (alleen in de view)");
    return true;
}

bool place_view_links(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    for (const auto& [link, doel] : pl.view_links) {
        // Een map mag ook (de link wordt een symlink naar die map): Lethal Enforcers 3 leest zijn films en archieven uit
        // D:\D22\DATA, en D22/DATA -> origineel:DATA geeft hem de map van de game in een keer.
        if (!isfile(doel) && !isdir(doel)) {
            log.push_back("viewlink: FOUT: " + doel + " bestaat niet (geen bestand en geen map) -- de game wordt niet gestart");
            err = "viewlink: " + doel + " bestaat niet of is geen bestand";
            return false;
        }
        if (!link_in_view(pl, p, link, realpath_(doel), "viewlink", false, log, err)) return false;
    }
    return true;
}

// [Launch] Mod: elk doel met een offset wordt een keer gebouwd in mod_built (kopie van het origineel op de NAS, de bestanden van de mod
// op hun offset; een stempel naast het bestand onthoudt grootte en mtime van het origineel en de boom van de mod), een doel met
// "replace" linkt naar het bestand van de mod. Daarna wijzen de doelen in de view daarheen. Links in de view die nog naar een mod
// van deze game wijzen maar niet meer gevraagd zijn (Mod = none, een andere mod) gaan terug naar het origineel.
bool place_view_mod(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    if (pl.mod_root.empty() || pl.mod_view_root.empty()) return true;
    auto fout = [&](const std::string& m) {
        log.push_back("mod: FOUT: " + m + " -- de game wordt niet gestart");
        err = "mod: " + m;
        return false;
    };
    // Per doel in de view: waar het heen moet wijzen; per te bouwen doel: de regels in hun volgorde.
    std::map<std::string, std::string> want;
    std::map<std::string, std::vector<const DirectPlan::ModEntry*>> bouw;
    for (const auto& e : pl.mod_entries) {
        const std::string link = pjoin(pl.mod_view_root, e.target);
        if (e.offset < 0) want[link] = e.src;
        else { want[link] = pjoin(pl.mod_built, e.target); bouw[e.target].push_back(&e); }
    }
    // 1. Terug naar het origineel wat niet meer bij de gevraagde mod hoort (alleen echte mappen van de view, links niet volgen).
    std::vector<std::string> terug;
    {
        std::error_code ec;
        const std::string prefix = pl.mod_root + "/";
        for (fs::recursive_directory_iterator it(pl.mod_view_root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_symlink(e2)) continue;
            const std::string link = it->path().string();
            const std::string doel = fs::read_symlink(link, e2).string();
            if (!e2 && doel.starts_with(prefix) && !want.count(link)) terug.push_back(link);
        }
    }
    for (const std::string& link : terug) {
        const std::string orig = pjoin(pl.mod_game_root, relpath(link, pl.mod_view_root));
        if (::unlink(link.c_str()) != 0) return fout(link + ": " + std::strerror(errno));
        if (lexists(orig) && ::symlink(orig.c_str(), link.c_str()) != 0) return fout(link + ": " + std::strerror(errno));
    }
    if (!terug.empty()) log.push_back("mod: " + std::to_string(terug.size()) + " link(s) in de view terug naar het origineel (" + pl.mod_game_root + ")");
    if (pl.mod_name.empty()) return true;
    // 2. Bouwen wat ontbreekt of verouderd is.
    int gebouwd = 0;
    bool exe_ok = false;
    for (const auto& [target, regels] : bouw) {
        const std::string orig = pjoin(pl.mod_game_root, target), uit = pjoin(pl.mod_built, target), stempel = uit + ".stamp";
        struct stat st{};
        if (::stat(orig.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return fout(orig + " (het origineel) ontbreekt");
        const std::string wil = std::to_string(st.st_size) + " " + std::to_string(st.st_mtime) + " " + pl.mod_tree + "\n";
        std::string heeft;
        if (isfile(uit) && read_file(stempel, heeft) && heeft == wil) continue;
        if (!exe_ok && !pl.mod_exe_sha256.empty()) {
            const std::string h = sha256_file(pl.mod_exe);
            if (h != pl.mod_exe_sha256) return fout(pl.mod_exe + " is niet de versie van de mod (sha256 " + (h.empty() ? "niet te lezen" : h) + ", verwacht " + pl.mod_exe_sha256 + ")");
            exe_ok = true;
        }
        std::error_code ec;
        fs::create_directories(dirname(uit), ec);
        const std::string tmp = uit + ".ravengun-tmp";
        fs::copy_file(orig, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) return fout(orig + " -> " + tmp + ": " + ec.message());
        const int fd = ::open(tmp.c_str(), O_WRONLY);
        if (fd < 0) return fout(tmp + ": " + std::strerror(errno));
        for (const auto* r : regels) {
            std::string data;
            if (!read_file(r->src, data)) { ::close(fd); return fout(r->src + " is niet te lezen"); }
            size_t klaar = 0;
            while (klaar < data.size()) {
                const ssize_t n = ::pwrite(fd, data.data() + klaar, data.size() - klaar, static_cast<off_t>(r->offset) + static_cast<off_t>(klaar));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { const int e = errno; ::close(fd); return fout(tmp + ": " + std::strerror(e)); }
                klaar += static_cast<size_t>(n);
            }
        }
        if (::close(fd) != 0) return fout(tmp + ": " + std::strerror(errno));
        if (std::rename(tmp.c_str(), uit.c_str()) != 0) return fout(uit + ": " + std::strerror(errno));
        std::string werr;
        if (!write_atomic(stempel, wil, werr)) return fout(werr);
        ++gebouwd;
    }
    // 3. De doelen in de view.
    for (const auto& [link, bron] : want)
        if (!link_in_view(pl, p, link, bron, "mod", true, log, err)) return false;
    log.push_back("mod: " + pl.mod_name + ": " + std::to_string(want.size()) + " bestanden in de view (" + std::to_string(bouw.size()) + " gebouwd in " +
                  pl.mod_built + (gebouwd ? ", " + std::to_string(gebouwd) + " nu opnieuw" : ", alle al actueel") + ")");
    return true;
}

// [Launch] ViewCopies: een link in de view wordt een lokale kopie van het bestand erachter (lezen van de NAS mag, schrijven nooit).
// Al een echt bestand: niets doen (de game heeft het al beschreven).
bool place_view_copies(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    for (const std::string& pad : pl.view_copies) {
        auto fout = [&](const std::string& m) {
            log.push_back("viewcopy: FOUT: " + m + " -- de game wordt niet gestart");
            err = "viewcopy: " + m;
            return false;
        };
        if (lexists(pad) && !islink(pad)) { log.push_back("viewcopy: " + pad + " is al een lokaal bestand"); continue; }
        if (!isfile(pad)) return fout(pad + " bestaat niet of is geen bestand");
        if (on_nas(dirname(pad), p)) return fout(dirname(pad) + " ligt op de NAS; daar wordt nooit iets geschreven");
        const std::string bron = realpath_(pad);
        std::string data;
        if (!read_file(bron, data)) return fout(bron + " is niet te lezen");
        ::unlink(pad.c_str());
        std::string werr;
        if (!write_atomic(pad, data, werr)) return fout(werr);
        log.push_back("viewcopy: " + pad + " is nu een lokale kopie van " + bron + " (" + std::to_string(data.size()) + " bytes)");
    }
    return true;
}

// [Launch] ViewTtcAsTtf: een link in de view naar een TrueType-collectie wordt een lokaal bestand met face 0 als losse font (ttc.hpp).
// Al een lokaal bestand zonder collectiekop: niets doen. De map van het bestand moet lokaal zijn (ViewRealDirs).
bool place_view_ttc_fonts(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    for (const std::string& pad : pl.view_ttc_fonts) {
        auto fout = [&](const std::string& m) {
            log.push_back("ttc: FOUT: " + m + " -- de game wordt niet gestart");
            err = "ttc: " + m;
            return false;
        };
        std::string data;
        if (lexists(pad) && !islink(pad) && read_file(pad, data) && !is_ttc(data)) { log.push_back("ttc: " + pad + " is al een losse font"); continue; }
        if (!isfile(pad)) return fout(pad + " bestaat niet of is geen bestand");
        if (on_nas(dirname(pad), p))
            return fout(dirname(pad) + " ligt op de NAS (of is in de view een link); zet de map in [Launch] ViewRealDirs, daar wordt nooit iets geschreven");
        const std::string bron = realpath_(pad);
        if (!read_file(bron, data)) return fout(bron + " is niet te lezen");
        std::string ttf, terr;
        if (!ttc_face(data, 0, ttf, terr)) return fout(bron + ": " + terr);
        ::unlink(pad.c_str());
        std::string werr;
        if (!write_atomic(pad, ttf, werr)) return fout(werr);
        log.push_back("ttc: " + pad + " is nu face 0 van " + bron + " als losse font (" + std::to_string(ttf.size()) + " bytes)");
    }
    return true;
}

// [Launch] NativeDlls: de native DLL op zijn plaats in de prefix (nooit op de NAS). Staat er al precies deze DLL, dan niets.
bool place_native_dlls(const DirectPlan& pl, const DirectPaths& p, std::vector<std::string>& log, std::string& err) {
    for (const auto& [bron, doel] : pl.native_dlls) {
        std::string want, have;
        if (!read_file(bron, want)) { err = "native DLL " + bron + " niet te lezen"; return false; }
        if (on_nas(dirname(doel), p)) { err = "native DLL: " + doel + " ligt op de NAS"; return false; }
        if (read_file(doel, have) && have == want) { log.push_back("native: " + doel + " is al actueel"); continue; }
        const std::string bewaard = doel + ".wine-bewaard";
        if (isfile(doel) && !lexists(bewaard) && ::rename(doel.c_str(), bewaard.c_str()) != 0) {
            err = "native DLL: " + doel + " niet te bewaren: " + std::strerror(errno);
            return false;
        }
        std::string werr;
        if (!write_atomic(doel, want, werr)) { err = "native DLL " + doel + ": " + werr; return false; }
        log.push_back("native: " + doel + " <- " + bron + " (Wine's eigen versie bewaard als " + basename(bewaard) + ")");
    }
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
        // Een symlink in een lokale map (de view) wordt een eigen lokaal bestand, ook als hij naar een bestand op de NAS wijst: dan
        // wordt alleen de link weggehaald en blijft het bestand op de NAS ongemoeid.
        if (on_nas(path, p) && !(islink(path) && !on_nas(dirname(abspath(path)), p))) {
            log.push_back("bestand: " + path + " ligt op de NAS en " + (have ? "wijkt af" : "ontbreekt") + "; niet geschreven (de game gebruikt het bestaande bestand)");
            continue;
        }
        if (islink(path)) ::unlink(path.c_str());
        std::string werr;
        if (!write_atomic(path, text, werr)) { err = "bestand " + path + ": " + werr; return false; }
        log.push_back("bestand: " + path + " geschreven");
    }
    if (!place_view_links(pl, p, log, err)) return false;
    if (!place_view_copies(pl, p, log, err)) return false;
    if (!place_view_ttc_fonts(pl, p, log, err)) return false;
    if (!place_view_mod(pl, p, log, err)) return false;
    if (!place_native_dlls(pl, p, log, err)) return false;
    return place_stack_copy(pl, p, log, err);
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
             {"control_source", &pl.control_source}, {"stack_src", &pl.stack_src}, {"stack_dest", &pl.stack_dest}, {"mod_name", &pl.mod_name},
             {"mod_root", &pl.mod_root}, {"mod_built", &pl.mod_built}, {"mod_game_root", &pl.mod_game_root}, {"mod_view_root", &pl.mod_view_root}})
        w.key(k).value(*v);
    w.key("stack_reserve").value(pl.stack_reserve);
    w.key("mod_entries").value(static_cast<long long>(pl.mod_entries.size()));
    w.key("pe_fill_pages").value(pl.pe_fill_pages);
    w.key("control_size").value(pl.control_size).key("control_interval_ms").value(pl.control_interval_ms);
    w.key("is64").value(pl.is64).key("gamescope").value(pl.gamescope).key("windowed_override").value(pl.windowed_override);
    w.key("log_full").value(pl.log_full).key("windows").value(pl.windows);
    strs("argv", pl.argv);
    map("env", pl.env);
    map("launch_env", pl.launch_env);
    w.key("files").begin_array();
    for (const auto& [a, b] : pl.files) w.begin_array().value(a).value(b).end_array();
    w.end_array();
    w.key("view_links").begin_array();
    for (const auto& [a, b] : pl.view_links) w.begin_array().value(a).value(b).end_array();
    w.end_array();
    strs("view_copies", pl.view_copies);
    strs("view_ttc_fonts", pl.view_ttc_fonts);
    w.key("exe_override").value(pl.exe_override);
    w.key("drives").begin_array();
    for (const auto& d : pl.drives)
        w.begin_object().key("letter").value(d.letter).key("target").value(d.target).key("link").value(d.link).key("present").value(d.present).end_object();
    w.end_array();
    w.key("native_dlls").begin_array();
    for (const auto& [a, b] : pl.native_dlls) w.begin_array().value(a).value(b).end_array();
    w.end_array();
    strs("reg_files", pl.reg_files);
    strs("install_cmd", pl.install_cmd);
    w.key("install_mark").value(pl.install_mark);
    w.key("am_daemon").value(pl.am_daemon);
    strs("am_ports", pl.am_ports);
    map("am_values", pl.am_values);
    w.key("dropin_files").begin_array();
    for (const auto& f : pl.dropin_files) w.begin_object().key("src").value(f.src).key("dest").value(f.dest).key("name").value(f.name).end_object();
    w.end_array();
    strs("dropin_materialise", pl.dropin_materialise);
    strs("dropin_lines", pl.dropin_lines);
    strs("game_comms", pl.game_comms);
    strs("patch_lines", pl.patch_lines);
    strs("notes", pl.notes);
    strs("tp_files", pl.tp_files);
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
    s("install_mark", pl.install_mark); s("pipe_mode", pl.pipe_mode); s("shm_source", pl.shm_source); s("pipehelper", pl.pipehelper); s("core_dll", pl.core_dll);
    s("core_source", pl.core_source); s("state", pl.state); s("dropin_base", pl.dropin_base); s("dropin_root", pl.dropin_root);
    s("control_pipe", pl.control_pipe); s("control_pipe_name", pl.control_pipe_name); s("control_source", pl.control_source);
    auto n = [&](const char* k, int& out) { const auto* x = v.find(k); if (x && x->is_number()) out = static_cast<int>(x->num); };
    n("control_size", pl.control_size); n("control_interval_ms", pl.control_interval_ms);
    s("stack_src", pl.stack_src); s("stack_dest", pl.stack_dest);
    s("mod_name", pl.mod_name); s("mod_root", pl.mod_root); s("mod_built", pl.mod_built); s("mod_game_root", pl.mod_game_root); s("mod_view_root", pl.mod_view_root);
    if (const auto* x = v.find("stack_reserve"); x && x->is_number()) pl.stack_reserve = static_cast<long long>(x->num);
    if (const auto* x = v.find("pe_fill_pages"); x && x->is_bool()) pl.pe_fill_pages = x->b;
    if (pl.stack_reserve < 0 || static_cast<uint64_t>(pl.stack_reserve) > kStackReserveMax ||
        ((pl.stack_reserve > 0 || pl.pe_fill_pages) && (pl.stack_src.empty() || pl.stack_dest.empty()))) {
        err = "plan: stack_reserve buiten de grenzen of zonder bron en doel";
        return false;
    }
    b("am_daemon", pl.am_daemon); sv("am_ports", pl.am_ports); mp("am_values", pl.am_values);
    b("is64", pl.is64); b("gamescope", pl.gamescope); b("windowed_override", pl.windowed_override); b("log_full", pl.log_full); b("windows", pl.windows);
    sv("argv", pl.argv); sv("dropin_materialise", pl.dropin_materialise); sv("dropin_lines", pl.dropin_lines); sv("game_comms", pl.game_comms);
    sv("patch_lines", pl.patch_lines); sv("notes", pl.notes); sv("reg_files", pl.reg_files); sv("install_cmd", pl.install_cmd); sv("view_copies", pl.view_copies); sv("view_ttc_fonts", pl.view_ttc_fonts);
    sv("tp_files", pl.tp_files);
    mp("env", pl.env); mp("launch_env", pl.launch_env);
    if (const auto* f = v.find("files"); f && f->is_array())
        for (const auto& e : f->arr)
            if (e.is_array() && e.arr.size() == 2 && e.arr[0].is_string() && e.arr[1].is_string()) pl.files.emplace_back(e.arr[0].str, e.arr[1].str);
    if (const auto* f = v.find("view_links"); f && f->is_array())
        for (const auto& e : f->arr)
            if (e.is_array() && e.arr.size() == 2 && e.arr[0].is_string() && e.arr[1].is_string()) pl.view_links.emplace_back(e.arr[0].str, e.arr[1].str);
    if (const auto* f = v.find("native_dlls"); f && f->is_array())
        for (const auto& e : f->arr)
            if (e.is_array() && e.arr.size() == 2 && e.arr[0].is_string() && e.arr[1].is_string()) pl.native_dlls.emplace_back(e.arr[0].str, e.arr[1].str);
    if (const auto* f = v.find("exe_override"); f && f->is_string()) pl.exe_override = f->str;
    if (const auto* f = v.find("drives"); f && f->is_array())
        for (const auto& e : f->arr) {
            DirectDrive d;
            if (const auto* x = e.find("letter"); x && x->is_string()) d.letter = x->str;
            if (const auto* x = e.find("target"); x && x->is_string()) d.target = x->str;
            if (const auto* x = e.find("link"); x && x->is_string()) d.link = x->str;
            if (const auto* x = e.find("present"); x && x->is_bool()) d.present = x->b;
            pl.drives.push_back(d);
        }
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

std::string plan_run_dir(const DirectPlan& pl) { return pl.tp.empty() ? pl.cwd : pl.tp; }

std::vector<std::string> plan_tp_refs(const DirectPlan& pl, const std::string& tp) {
    std::vector<std::string> out(pl.tp_files.begin(), pl.tp_files.end());
    std::string t = tp;
    while (t.size() > 1 && t.back() == '/') t.pop_back();
    if (!t.empty()) {
        std::string w = wine_path(t);   // Z:\pad: argv en omgeving hebben paden ook in de Wine-vorm
        auto kijk = [&](const std::string& s) {
            for (int vorm = 0; vorm < 2; ++vorm) {
                const std::string& basis = vorm == 0 ? t : w;
                const char sep = vorm == 0 ? '/' : '\\';
                for (size_t i = s.find(basis); i != std::string::npos; i = s.find(basis, i + 1)) {
                    const size_t e = i + basis.size();
                    if (e < s.size() && s[e] != sep) continue;   // /x/tp-ander is niet /x/tp
                    // Het pad loopt tot een scheidingsteken van een lijst (;, :, spatie, aanhalingsteken) of het einde.
                    size_t j = e;
                    while (j < s.size() && s[j] != ';' && s[j] != ':' && s[j] != ' ' && s[j] != '"' && s[j] != '\n') ++j;
                    std::string rel = s.substr(e < s.size() ? e + 1 : e, j > e ? j - e - 1 : 0);
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    out.push_back(rel.empty() ? std::string(".") : rel);
                }
            }
        };
        std::function<void(const json::Value&)> loop = [&](const json::Value& v) {
            if (v.is_string()) kijk(v.str);
            else if (v.is_array()) for (const auto& x : v.arr) loop(x);
            else if (v.is_object()) for (const auto& [k, x] : v.obj) { kijk(k); loop(x); }
        };
        DirectPlan zonder = pl;
        zonder.tp.clear();   // het veld tp zelf is geen gebruik: dat zegt tp_files
        zonder.env.erase("RAVENGUN_TP_DIR");   // idem de keuze van de map in de omgeving van de daemon (die erft het plan mee)
        const auto r = json::parse(plan_to_json(zonder));
        if (r.ok) loop(r.value);
        if (!pl.tp.empty() && pl.tp_files.empty()) out.push_back(".");   // tp gezet zonder bestanden: ook een verwijzing
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // De kern staat in argv zonder .dll (Z:\...\TeknoParrot\TeknoParrot): een keer tellen, als het bestand.
    const std::vector<std::string> alle = out;
    out.erase(std::remove_if(out.begin(), out.end(), [&](const std::string& x) { return std::binary_search(alle.begin(), alle.end(), x + ".dll"); }),
              out.end());
    return out;
}

}  // namespace ravengun::launch
