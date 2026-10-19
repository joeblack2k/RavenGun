#include "ravengun/launch/native.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "ravengun/json_parse.hpp"
#include "ravengun/strutil.hpp"

namespace fs = std::filesystem;

namespace ravengun::launch {
namespace {

using json::Value;

bool is_dir(const std::string& path);

// De spelmap voor "@nas/<Map>/...": een vaste (p.nas), anders de eerste spelmap van de app waarin <Map> staat (find_game_folder).
// Staat hij nergens, dan de eerste spelmap: de fout volgt bij de mount.
std::string nas_for(const std::string& s, const NativePaths& p) {
    if (!p.nas.empty() || p.nas_roots.empty()) return p.nas;
    const size_t at = s.find("@nas/");
    if (at == std::string::npos) return p.nas_roots.front();
    const size_t b = at + 5, e = s.find('/', b);
    const std::string map = s.substr(b, e == std::string::npos ? std::string::npos : e - b);
    const std::string f = find_game_folder(map, p.nas_roots);
    return f.empty() ? p.nas_roots.front() : f.substr(0, f.size() - map.size() - 1);
}

std::string expand(std::string s, const NativePaths& p) {
    // Bestanden die niet verspreid worden (eigen dongledata, shaders) blijven in
    // de gebruikerslaag. Ook @patches-verwijzingen in meegeleverde profielen vinden ze daar.
    if (s.starts_with("@patches/") && !p.user_patches.empty()) {
        const std::string user = p.user_patches + s.substr(8);
        std::error_code ec;
        if (fs::exists(user, ec)) return user;
    }
    const std::string nas = nas_for(s, p);
    const std::pair<const char*, const std::string*> keys[] = {{"@nas", &nas},       {"@tpui", &p.tpui},   {"@data", &p.data},
                                                               {"@lib32", &p.lib32}, {"@build", &p.build}, {"@patches", &p.patches}};
    for (const auto& [k, v] : keys) {
        const size_t kl = std::strlen(k);
        for (size_t at = s.find(k); at != std::string::npos; at = s.find(k, at + v->size())) s.replace(at, kl, *v);
    }
    return s;
}

// Een vingerafdruk van een boom: relatieve paden, soort, grootte en wijzigingstijd van de bestanden en het doel van de
// koppelingen (geen inhoud; een samenvoeging hoeft maar opnieuw als er iets aan de bronnen veranderd is). Gesorteerd, want de
// volgorde van een map is niet vastgelegd.
std::uint64_t tree_fingerprint(const std::string& root) {
    std::vector<std::string> regels;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const fs::file_status st = it->symlink_status(e2);
        // lexically_relative: fs::relative gaat via weakly_canonical en volgt dus de koppelingen (een koppeling kreeg het pad van haar
        // doel, en twee verschillende bomen konden dezelfde afdruk krijgen).
        std::string r = it->path().lexically_relative(root).string();
        if (fs::is_symlink(st)) r += "|l|" + fs::read_symlink(it->path(), e2).string();
        else if (fs::is_directory(st)) r += "|d";
        else r += "|f|" + std::to_string(fs::file_size(it->path(), e2)) + "|" + std::to_string(fs::last_write_time(it->path(), e2).time_since_epoch().count());
        regels.push_back(std::move(r));
    }
    std::sort(regels.begin(), regels.end());
    std::uint64_t h = 1469598103934665603ULL;   // FNV-1a
    for (const std::string& r : regels) {
        for (unsigned char c : r) { h ^= c; h *= 1099511628211ULL; }
        h ^= '\n'; h *= 1099511628211ULL;
    }
    return h;
}

// Een veld uit /proc/*/mountinfo: spaties, tabs, regeleinden en "\" staan er als \ooo (octaal) in.
std::string mountinfo_veld(const std::string& v) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 3 < v.size() && std::isdigit(static_cast<unsigned char>(v[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(v[i + 2])) && std::isdigit(static_cast<unsigned char>(v[i + 3]))) {
            r += static_cast<char>((v[i + 1] - '0') * 64 + (v[i + 2] - '0') * 8 + (v[i + 3] - '0'));
            i += 3;
        } else {
            r += v[i];
        }
    }
    return r;
}

struct MountRegel {
    std::string dev, root, punt, regel;
};

std::vector<MountRegel> lees_mountinfo(const std::string& pad) {
    std::vector<MountRegel> r;
    std::ifstream f(pad);
    std::string regel;
    while (std::getline(f, regel)) {
        std::istringstream in(regel);
        std::string id, ouder, dev, root, punt;
        if (!(in >> id >> ouder >> dev >> root >> punt)) continue;
        r.push_back({dev, mountinfo_veld(root), mountinfo_veld(punt), regel});
    }
    return r;
}

bool pad_onder(const std::string& pad, const std::string& wortel) {
    if (wortel == "/") return !pad.empty() && pad[0] == '/';
    return pad == wortel || (pad.size() > wortel.size() && pad.compare(0, wortel.size(), wortel) == 0 && pad[wortel.size()] == '/');
}

// Heeft een proces (dat we mogen lezen) een mount van of in <pad>? Een bwrap-sandbox bindt de samengevoegde map onder een ander
// pad; in zijn mountinfo staat dan het pad BINNEN het bestandssysteem (veld 4, bijvoorbeeld /deck/... als /home een eigen partitie
// is). Dat zetten we om via onze eigen mountinfo (zelfde apparaat). Na een rename toont de kernel daar de nieuwe naam (.oud.*).
// Daarnaast telt het absolute pad ergens in de regel (koppelpunt, overlay-opties met upperdir=/lowerdir=).
bool in_gebruik_door_mount(const std::string& pad_in) {
    std::error_code ec;
    const std::string pad = fs::weakly_canonical(pad_in, ec).string();
    if (ec || pad.empty()) return true;   // niet vast te stellen: voorzichtig, dan laten staan
    std::string dev, fsrel;   // het apparaat en het pad binnen dat bestandssysteem, via het langste eigen koppelpunt
    size_t beste = 0;
    for (const MountRegel& m : lees_mountinfo("/proc/self/mountinfo")) {
        if (!pad_onder(pad, m.punt) || m.punt.size() < beste) continue;   // gelijk: de latere (bovenste) mount wint
        beste = m.punt.size();
        dev = m.dev;
        const std::string rest = m.punt == "/" ? pad : pad.substr(m.punt.size());
        fsrel = m.root == "/" ? (rest.empty() ? "/" : rest) : m.root + rest;
    }
    const auto in_regel = [&](const std::string& regel) {
        for (size_t at = regel.find(pad); at != std::string::npos; at = regel.find(pad, at + 1)) {
            const size_t e = at + pad.size();
            if (e == regel.size() || regel[e] == '/' || regel[e] == ' ' || regel[e] == ',' || regel[e] == ':') return true;
        }
        return false;
    };
    for (fs::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || !std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
        for (const MountRegel& m : lees_mountinfo(it->path().string() + "/mountinfo")) {
            if (!fsrel.empty() && m.dev == dev && pad_onder(m.root, fsrel)) return true;
            if (in_regel(m.regel)) return true;
        }
    }
    return false;
}

// Wat een eerdere samenvoeging naast dst liet liggen. <dst>.nieuw.<pid> is een halve bouw van een proces dat gestorven is (een
// lopend proces mag hem houden); <dst>.oud.<tijd>.<pid> is de vorige map, opzij gezet omdat een draaiende game hem nog gemount kan
// hebben (leeghalen onder zijn voeten maakt de game stuk). Die laatste ruimen we pas na een week op, en ook dan niet zolang een proces
// dat we kunnen lezen hem (of iets erin) nog gemount heeft (of dat niet vast te stellen is). <dst>.stempel.tmp.<pid> van een
// gestorven proces gaat ook weg; <dst>.lock blijft.
void sweep_merge_leftovers(const std::string& dst) {
    std::error_code ec;
    const fs::path d(dst);
    const std::string base = d.filename().string(), nieuw = base + ".nieuw.", oud = base + ".oud.", stempel_tmp = base + ".stempel.tmp.";
    const long now = static_cast<long>(std::time(nullptr));
    std::vector<fs::path> weg, oude;
    for (fs::directory_iterator it(d.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.starts_with(nieuw) || n.starts_with(stempel_tmp)) {   // ook een halve stempel (tmp) van een gestorven proces
            const size_t na = n.starts_with(nieuw) ? nieuw.size() : stempel_tmp.size();
            const long pid = std::strtol(n.c_str() + na, nullptr, 10);
            if (pid > 0 && pid != static_cast<long>(::getpid()) && ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) weg.push_back(it->path());
        } else if (n.starts_with(oud)) {
            const long tijd = std::strtol(n.c_str() + oud.size(), nullptr, 10);
            if (tijd > 0 && now - tijd > 7L * 24 * 3600) oude.push_back(it->path());
        }
    }
    for (const fs::path& o : oude)
        if (!in_gebruik_door_mount(o.string())) weg.push_back(o);
    for (const fs::path& w : weg) fs::remove_all(w, ec);
}

// Een exclusief slot (flock) op <dst>.lock rond controle, bouw en wissel van een samenvoeging: twee gamestarts die tegelijk
// opnieuw samenvoegen zaten elkaar anders in de weg (tussen de twee renames bestaat dst niet; de verliezer viel terug op alleen
// de meegeleverde map). Wacht hoogstens ~30 s; lukt het dan nog niet (of kan het slotbestand niet gemaakt worden), dan gaat het
// zonder slot verder en vangt de controle na een mislukte rename het op.
class MergeSlot {
public:
    explicit MergeSlot(const std::string& pad) {
        fd_ = ::open(pad.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd_ < 0) return;
        for (int i = 0; i < 300; ++i) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) { vast_ = true; return; }
            if (errno != EWOULDBLOCK && errno != EINTR) return;
            ::usleep(100 * 1000);
        }
    }
    ~MergeSlot() {
        if (fd_ >= 0) ::close(fd_);   // sluiten geeft het slot vrij
    }
    MergeSlot(const MergeSlot&) = delete;
    MergeSlot& operator=(const MergeSlot&) = delete;
    bool vast() const { return vast_; }

private:
    int fd_ = -1;
    bool vast_ = false;
};

std::string hex64(std::uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

// De gebruikerslaag gaat per BESTAND voor op de meegeleverde laag. Zonder dit wint een map
// in de gebruikerslaag op de hele meegeleverde map: een halve of verouderde
// ravengun-hasp-map (of data/-map) verbergt dan de bestanden die een verse checkout wel
// heeft, en de game start niet meer. Botst het, dan maken we in @data/merge-patches een
// samengevoegde map: eerst de meegeleverde bestanden, daarna die van de gebruiker eroverheen.
//
// De samenvoeging wordt NIET bij elke aanroep opnieuw gebouwd: naast de map staat een stempel met de vingerafdruk van beide
// bronnen, en zolang die klopt blijft de map zoals hij is (een draaiende game houdt hem gemount). Een nieuwe samenvoeging komt
// in een eigen map en wordt pas op zijn plek gezet als hij af is. Lukt het samenvoegen niet, dan vallen we terug op de MEEGELEVERDE
// map (die is compleet en klopt met de repo; de gebruikerslaag kan halfverouderd zijn en was juist de reden voor de samenvoeging),
// met een melding dat de bestanden uit de gebruikerslaag dan ontbreken. *merged zegt of het resultaat de samengevoegde map is.
std::string merge_patches(const std::string& src, const NativePaths& p, std::vector<std::string>* notes, bool* merged = nullptr) {
    const auto meld = [&](const std::string& tekst) {
        if (notes) notes->push_back(tekst);
    };
    if (merged) *merged = false;
    if (p.user_patches.empty() || p.patches.empty() || p.data.empty()) return src;
    const std::string up = p.user_patches + "/", sp = p.patches + "/";
    std::string rel;
    if (src.starts_with(up)) rel = src.substr(up.size());
    else if (src.starts_with(sp)) rel = src.substr(sp.size());
    else return src;
    if (rel.empty()) return src;
    const std::string user = p.user_patches + "/" + rel, shipped = p.patches + "/" + rel;
    // Alleen bij twee mappen; is het een bestand, dan is de keuze van expand() al goed.
    if (!is_dir(user) || !is_dir(shipped)) return src;
    std::error_code ec;
    if (fs::equivalent(user, shipped, ec) && !ec) return src;   // hetzelfde (bijvoorbeeld een koppeling)
    const std::string dst = p.data + "/merge-patches/" + rel, stempel = dst + ".stempel";
    const std::string vingerafdruk = "1 " + hex64(tree_fingerprint(shipped)) + " " + hex64(tree_fingerprint(user));
    const auto klopt = [&]() {
        std::ifstream f(stempel);
        std::string oud;
        return f && std::getline(f, oud) && oud == vingerafdruk && is_dir(dst);
    };
    if (klopt()) {   // de gewone start: niets te doen, geen slot nodig
        if (merged) *merged = true;
        return dst;
    }
    // Controle, bouw en wissel onder het slot; daarna eerst opnieuw kijken: een andere start kan net klaar zijn.
    fs::create_directories(fs::path(dst).parent_path(), ec);
    ec.clear();
    MergeSlot slot(dst + ".lock");
    if (!slot.vast()) meld("patches-laag samenvoegen (" + rel + "): geen slot op " + dst + ".lock; verder zonder");
    if (klopt()) {
        if (merged) *merged = true;
        return dst;
    }
    // Mislukt een rename, dan kan een andere start (zonder slot) net dezelfde samenvoeging op zijn plek gezet hebben: klopt de
    // stempel dan, gebruik die map in plaats van terug te vallen.
    const auto toch_klaar = [&]() {
        if (!klopt()) return false;
        if (merged) *merged = true;
        return true;
    };
    const auto terugval = [&](const std::string& waarom) {
        meld("patches-laag samenvoegen mislukt (" + rel + "): " + waarom + "; de meegeleverde map wordt gebruikt, bestanden uit de gebruikerslaag (" + user +
             ") ontbreken daardoor");
        return shipped;
    };
    const std::string nieuw = dst + ".nieuw." + std::to_string(::getpid());
    sweep_merge_leftovers(dst);
    fs::remove_all(nieuw, ec);
    ec.clear();
    fs::create_directories(nieuw, ec);
    if (ec) return terugval(ec.message());
    const auto opties = fs::copy_options::recursive | fs::copy_options::copy_symlinks | fs::copy_options::overwrite_existing;
    fs::copy(shipped, nieuw, opties, ec);
    if (!ec) fs::copy(user, nieuw, opties, ec);
    if (ec) { const std::string m = ec.message(); fs::remove_all(nieuw, ec); return terugval(m); }
    // Op zijn plek: eerst de stempel weg (een onderbreking laat nooit een stempel bij een halve map achter), dan wisselen.
    // De oude map gaat opzij (rename), niet weg: een draaiende game kan hem nog gemount hebben en remove_all zou hem onder zijn voeten
    // leeghalen. De kopie van de gamestart blijft zo bruikbaar; sweep_merge_leftovers ruimt hem later op.
    fs::remove(stempel, ec);
    std::string opzij;
    if (fs::exists(fs::symlink_status(dst, ec))) {
        const std::string basis = dst + ".oud." + std::to_string(static_cast<long>(std::time(nullptr))) + "." + std::to_string(::getpid());
        opzij = basis;
        for (int n = 1; fs::exists(fs::symlink_status(opzij, ec)) && n < 1000; ++n) opzij = basis + "." + std::to_string(n);   // twee keer in dezelfde seconde
        ec.clear();
        fs::rename(dst, opzij, ec);
        if (ec) {
            const std::string m = ec.message();
            fs::remove_all(nieuw, ec);
            if (toch_klaar()) return dst;
            return terugval(m);
        }
    }
    ec.clear();
    fs::rename(nieuw, dst, ec);
    if (ec) {
        const std::string m = ec.message();
        fs::remove_all(nieuw, ec);
        if (toch_klaar()) return dst;   // ENOTEMPTY/EEXIST: een andere start zette dezelfde samenvoeging net neer
        if (!opzij.empty() && !fs::exists(fs::symlink_status(dst, ec))) fs::rename(opzij, dst, ec);   // de vorige map terug, liever een verouderde dan geen
        return terugval(m);
    }
    {   // de stempel atomair: een lezer ziet nooit een halve regel
        const std::string tmp = stempel + ".tmp." + std::to_string(::getpid());
        { std::ofstream(tmp, std::ios::trunc) << vingerafdruk << "\n"; }
        fs::rename(tmp, stempel, ec);
        if (ec) fs::remove(tmp, ec);
    }
    meld("patches-laag samengevoegd (meegeleverd + gebruikerslaag): " + rel);
    if (merged) *merged = true;
    return dst;
}

std::string str_of(const Value* v) { return v && v->is_string() ? v->str : std::string(); }

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec)) || fs::exists(path, ec);
}
bool is_dir(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}
std::string base_name(const std::string& path) {
    const size_t sl = path.rfind('/');
    return sl == std::string::npos ? path : path.substr(sl + 1);
}
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Bij een ontbrekend bibliotheekbestand: welk onderdeel (packaging/components.json, "Components" in de app) het levert. Leeg = onbekend.
// De gepinde downloads staan in @lib32 (runtime/lib32); PhysX 2.8.1 (van NVIDIA zelf) staat er ook; de FMOD-DLL's van de gebruiker hebben een eigen map onder runtime/.
std::string component_hint(const std::string& path, const NativePaths& p) {
    static const std::pair<const char*, const char*> by_name[] = {
        {"libstdc++.so.5", "libstdcxx5"}, {"libcsv.so", "libcsv"},
        {"libssl.so.1.0.0", "openssl10-i386"}, {"libcrypto.so.1.0.0", "openssl10-i386"},
        {"libSDL-1.2.so.0", "sdl12-compat"},
        {"libPhysXLoader.so.1", "physx-linux"}, {"libPhysXCore.so", "physx-linux"}, {"libNxCharacter.so.1", "physx-linux"},
        {"libNxCooking.so", "physx-linux"}};
    const std::string name = base_name(path);
    for (const auto& [n, id] : by_name)
        if (name == n) return id;
    if (contains(path, "/runtime/fmod-win")) return "fmod-win";
    if (!p.lib32.empty() && path.rfind(p.lib32 + "/", 0) == 0) return "lib32-sniper";
    return {};
}

std::string with_hint(const std::string& path, const NativePaths& p) {
    const std::string id = component_hint(path, p);
    return id.empty() ? path : path + " (onderdeel '" + id + "': installeer het bij Components in de app)";
}

// Een programma in PATH (zoals posix_spawnp het zou vinden); leeg = niet gevonden.
std::string find_program(const std::string& name) {
    if (name.find('/') != std::string::npos) return ::access(name.c_str(), X_OK) == 0 ? name : std::string();
    const char* path = std::getenv("PATH");
    std::string dirs = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        const size_t end = dirs.find(':', start);
        std::string d = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (d.empty()) d = ".";
        const std::string cand = d + "/" + name;
        if (::access(cand.c_str(), X_OK) == 0) return cand;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

// --- sha256 (FIPS 180-4), voor het identiteitsblok -------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t used = 0;
    uint64_t total = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const unsigned char* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void add(const unsigned char* p, size_t n) {
        total += n;
        while (n) {
            const size_t take = std::min(n, sizeof buf - used);
            std::memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == sizeof buf) { block(buf); used = 0; }
        }
    }
    std::string hex() {
        const uint64_t bits = total * 8;
        const unsigned char pad = 0x80, zero = 0;
        add(&pad, 1);
        while (used != 56) add(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        add(len, 8);
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
};

// CRC-32 (IEEE, gespiegeld, zoals zlib): de spel-ID van lindbergh.so.
uint32_t crc32(const unsigned char* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string read_file(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    ok = static_cast<bool>(f);
    if (!ok) return {};
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// Het pad op de host van een pad in de sandbox, langs de mounts (de langste dst wint); leeg = valt onder geen mount.
std::string host_of(const std::vector<Mount>& mounts, const std::string& path) {
    const Mount* best = nullptr;
    for (const Mount& m : mounts)
        if ((path == m.dst || (path.size() > m.dst.size() && path.compare(0, m.dst.size(), m.dst) == 0 && path[m.dst.size()] == '/')) &&
            (!best || m.dst.size() > best->dst.size()))
            best = &m;
    return best ? best->src + path.substr(best->dst.size()) : std::string();
}

std::string parent_dir(const std::string& path) {
    const size_t sl = path.rfind('/');
    return sl == std::string::npos || sl == 0 ? std::string("/") : path.substr(0, sl);
}

}  // namespace

std::string lindbergh_game_id(const std::string& elf_path) {
    std::ifstream f(elf_path, std::ios::binary);
    if (!f) return {};
    unsigned char eh[52];
    if (!f.read(reinterpret_cast<char*>(eh), sizeof eh)) return {};
    if (std::memcmp(eh, "\x7f" "ELF", 4) != 0 || eh[4] != 1 /* ELFCLASS32 */ || eh[5] != 1 /* little endian */) return {};
    auto u16 = [&](int o) { return static_cast<uint32_t>(eh[o] | eh[o + 1] << 8); };
    auto u32 = [](const unsigned char* b) { return static_cast<uint32_t>(b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24); };
    const uint32_t phoff = u32(eh + 28), phentsize = u16(42), phnum = u16(44);
    if (phnum < 3 || phentsize < 32) return {};
    unsigned char ph[32];
    f.seekg(static_cast<std::streamoff>(phoff) + 2 * static_cast<std::streamoff>(phentsize));
    if (!f.read(reinterpret_cast<char*>(ph), sizeof ph)) return {};
    // lindbergh.so (dl_iterate_phdr): de derde programmakop moet PT_LOAD met vlaggen R+X (5) zijn; dan 0x4000 bytes vanaf
    // p_vaddr + 10 in het geheugen, wat in het bestand p_offset + 10 is (het segment is uit het bestand gemapt).
    if (u32(ph) != 1 /* PT_LOAD */ || u32(ph + 24) != 5) return {};
    std::vector<unsigned char> buf(0x4000);
    f.seekg(static_cast<std::streamoff>(u32(ph + 4)) + 10);
    if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) return {};
    char out[16];
    std::snprintf(out, sizeof out, "0x%08x", crc32(buf.data(), buf.size()));
    return out;
}

std::string lindbergh_ini(const std::string& base, const std::vector<IniKey>& forced) {
    std::vector<std::string> lines;
    {
        size_t at = 0;
        while (at < base.size()) {
            size_t nl = base.find('\n', at);
            if (nl == std::string::npos) nl = base.size();
            std::string l = base.substr(at, nl - at);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            at = nl + 1;
        }
    }
    std::vector<bool> done(forced.size(), false);
    std::vector<std::string> out;
    std::string section;
    auto flush_section = [&](const std::string& sec) {
        // Ontbrekende sleutels van deze sectie achteraan; lege regels aan het eind van de sectie blijven erna staan.
        size_t blanks = 0;
        while (!out.empty() && trim(out.back()).empty()) { out.pop_back(); ++blanks; }
        for (size_t i = 0; i < forced.size(); ++i)
            if (!done[i] && lower(forced[i].section) == lower(sec)) {
                out.push_back(forced[i].key + " = " + forced[i].value);
                done[i] = true;
            }
        out.insert(out.end(), blanks, std::string());
    };
    for (const std::string& l : lines) {
        const std::string t = trim(l);
        if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
            if (!section.empty()) flush_section(section);
            section = t.substr(1, t.size() - 2);
            out.push_back(l);
            continue;
        }
        const size_t eq = t.find('=');
        if (!section.empty() && eq != std::string::npos && t[0] != ';' && t[0] != '#') {
            const std::string key = trim(t.substr(0, eq));
            bool replaced = false;
            for (size_t i = 0; i < forced.size(); ++i)
                if (lower(forced[i].section) == lower(section) && lower(forced[i].key) == lower(key)) {
                    if (!done[i]) out.push_back(forced[i].key + " = " + forced[i].value);
                    done[i] = true;
                    replaced = true;
                }
            if (replaced) continue;
        }
        out.push_back(l);
    }
    if (!section.empty()) flush_section(section);
    // Secties die er nog niet waren.
    for (size_t i = 0; i < forced.size(); ++i) {
        if (done[i]) continue;
        if (!out.empty() && !trim(out.back()).empty()) out.push_back("");
        out.push_back("[" + forced[i].section + "]");
        for (size_t j = i; j < forced.size(); ++j)
            if (!done[j] && lower(forced[j].section) == lower(forced[i].section)) {
                out.push_back(forced[j].key + " = " + forced[j].value);
                done[j] = true;
            }
    }
    while (!out.empty() && trim(out.back()).empty()) out.pop_back();
    std::string s;
    for (const std::string& l : out) s += l + "\n";
    return s;
}

bool resolve_jvs(NativePlan& pl, const std::string& link, std::string& err) {
    if (!pl.needs_jvs_pty) return true;
    char buf[4096];
    const ssize_t n = link.empty() ? -1 : ::readlink(link.c_str(), buf, sizeof buf - 1);
    if (n <= 0) { err = "de jvs-route heeft geen pty (" + (link.empty() ? std::string("geen symlink") : link) + "): is de IO-brug gestart?"; return false; }
    std::string target(buf, static_cast<size_t>(n));
    if (target[0] != '/') target = parent_dir(link) + "/" + target;
    if (::access(target.c_str(), R_OK | W_OK) != 0) { err = "de pty van de jvs-route (" + target + ") is niet te openen"; return false; }
    for (std::string& a : pl.argv)
        if (a == kJvsPtyPlaceholder) a = target;
    pl.needs_jvs_pty = false;
    pl.notes.push_back(std::string("JVS: ") + kLindberghJvsDev + " in de sandbox = " + target + " (de pty van de IO-brug, " + link + ")");
    return true;
}

std::string sha256_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    Sha256 s;
    std::array<char, 65536> b{};
    while (f) {
        f.read(b.data(), b.size());
        const std::streamsize n = f.gcount();
        if (n > 0) s.add(reinterpret_cast<const unsigned char*>(b.data()), static_cast<size_t>(n));
    }
    return s.hex();
}

std::string profile_file(const NativePaths& p, const std::string& game) {
    // Alleen een gewone profielnaam: hij komt in een pad en in de JSON-tekst.
    if (game.empty() || game.size() > 64) return {};
    for (char ch : game)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return {};
    for (const std::string& dir : {p.user_patches, p.patches}) {
        if (dir.empty()) continue;
        const std::string f = dir + "/" + game + "/native.json";
        if (exists(f)) return f;
    }
    return p.json;
}

std::string profile_text(const NativePaths& p, const std::string& game) {
    const std::string f = profile_file(p, game);
    if (f.empty()) return {};
    bool ok = false;
    const std::string text = read_file(f, ok);
    if (!ok) return {};
    if (f == p.json) return text;
    const auto r = json::parse(text);
    if (!r.ok || !r.value.is_object()) return {};   // een kapot bestand in de gebruikerslaag: niet terugvallen op het meegeleverde
    return R"({"defaults":{"res":"1920x1080"},"games":{")" + game + "\":" + text + "}}";
}

bool profile_enabled(const std::string& json_text, const std::string& game) {
    const auto r = json::parse(json_text);
    if (!r.ok) return false;
    const Value* games = r.value.find("games");
    const Value* g = games ? games->find(game) : nullptr;
    const Value* e = g ? g->find("enabled") : nullptr;
    return e && e->is_bool() && e->b;
}

bool load_profile(const std::string& json_text, const std::string& game, const NativePaths& p, NativeProfile& out, std::string& err) {
    const auto r = json::parse(json_text);
    if (!r.ok) { err = "native.json: " + r.error; return false; }
    const Value* games = r.value.find("games");
    const Value* g = games && games->is_object() ? games->find(game) : nullptr;
    if (!g || !g->is_object()) { err = "onbekend native profiel: " + game; return false; }
    out = NativeProfile{};
    out.name = game;
    for (auto [key, dst] : std::initializer_list<std::pair<const char*, std::string*>>{
             {"title", &out.title}, {"status", &out.status}, {"note", &out.note}, {"arch", &out.arch}, {"preload", &out.preload},
             {"exe", &out.exe}, {"cwd", &out.cwd}, {"home", &out.home}, {"route", &out.route}, {"launcher", &out.launcher},
             {"loader", &out.loader}, {"res", &out.res}}) {
        const Value* v = g->find(key);
        if (v && !v->is_string()) { err = game + ": '" + key + "' is geen tekst"; return false; }
        *dst = str_of(v);
    }
    if (out.res.empty()) {
        const Value* d = r.value.find("defaults");
        out.res = str_of(d ? d->find("res") : nullptr);
        if (out.res.empty()) out.res = "1920x1080";
    }
    const Value* en = g->find("enabled");
    out.enabled = en && en->is_bool() && en->b;
    const Value* si = g->find("share_ipc");
    out.share_ipc = si && si->is_bool() && si->b;
    if (const Value* fv = g->find("fake_video")) {
        if (!fv->is_number() || fv->num < 0 || fv->num > 4 || fv->num != static_cast<int>(fv->num)) {
            err = game + ": 'fake_video' is een getal van 0 tot en met 4";
            return false;
        }
        out.fake_video = static_cast<int>(fv->num);
    }
    auto pairs = [&](const char* key, const char* a, const char* b, std::vector<SrcDst>& to) {
        const Value* v = g->find(key);
        if (!v) return true;
        if (!v->is_array()) { err = game + ": '" + key + "' is geen lijst"; return false; }
        for (const Value& e : v->arr) {
            const std::string s = str_of(e.find(a)), d = str_of(e.find(b));
            if (s.empty() || d.empty() || d[0] != '/') { err = game + ": '" + key + "' heeft een regel zonder " + a + " of met een relatief " + b; return false; }
            bool gemerged = false;
            const std::string bron = std::string(a) == "target" ? s : merge_patches(expand(s, p), p, &out.notes, &gemerged);
            to.push_back({bron, d, gemerged});
        }
        return true;
    };
    auto strings = [&](const char* key, std::vector<std::string>& to, bool exp) {
        const Value* v = g->find(key);
        if (!v) return true;
        if (!v->is_array()) { err = game + ": '" + key + "' is geen lijst"; return false; }
        for (const Value& e : v->arr) {
            if (!e.is_string()) { err = game + ": '" + key + "' bevat iets anders dan tekst"; return false; }
            to.push_back(exp ? merge_patches(expand(e.str, p), p, &out.notes) : e.str);
        }
        return true;
    };
    if (const Value* m = g->find("mounts")) {
        if (!m->is_array()) { err = game + ": 'mounts' is geen lijst"; return false; }
        for (const Value& e : m->arr) {
            Mount mt{str_of(e.find("mode")), expand(str_of(e.find("src")), p), str_of(e.find("dst"))};
            if (mt.mode.empty()) mt.mode = "ro";
            if (mt.src.empty() || mt.dst.empty() || mt.dst[0] != '/') { err = game + ": een mount zonder src of met een relatief dst"; return false; }
            mt.src = merge_patches(mt.src, p, &out.notes);
            out.mounts.push_back(mt);
        }
    }
    if (!pairs("files", "src", "dst", out.files) || !pairs("seed", "src", "dst", out.seeds) || !pairs("links", "target", "dst", out.links) ||
        !pairs("libdirs", "src", "dst", out.libdirs) || !strings("libs", out.libs, true) || !strings("extra_ro", out.extra_ro, true) ||
        !strings("args", out.args, false))
        return false;
    if (const Value* b = g->find("binds_rw")) {
        if (!b->is_array()) { err = game + ": 'binds_rw' is geen lijst"; return false; }
        for (const Value& e : b->arr) {
            if (!e.is_array() || e.arr.size() != 2 || !e.arr[0].is_string() || !e.arr[1].is_string()) { err = game + ": 'binds_rw' wil paren"; return false; }
            out.binds_rw.push_back({expand(e.arr[0].str, p), expand(e.arr[1].str, p)});
        }
    }
    if (const Value* e = g->find("env")) {
        if (!e->is_object()) { err = game + ": 'env' is geen object"; return false; }
        for (const auto& [k, v] : e->obj) {
            if (!v.is_string() || k.empty() || k.find('=') != std::string::npos) { err = game + ": ongeldige omgevingsvariabele '" + k + "'"; return false; }
            out.env.emplace_back(k, v.str);
        }
    }
    if (const Value* lb = g->find("lindbergh")) {
        if (!lb->is_object()) { err = game + ": 'lindbergh' is geen object"; return false; }
        LindberghSpec& s = out.lindbergh;
        s.on = true;
        for (auto [key, dst] : std::initializer_list<std::pair<const char*, std::string*>>{{"id", &s.id}, {"ini", &s.ini}, {"jvs", &s.jvs}}) {
            const Value* v = lb->find(key);
            if (v && !v->is_string()) { err = game + ": 'lindbergh." + key + "' is geen tekst"; return false; }
            if (v) *dst = v->str;
        }
        for (auto [key, dst] : std::initializer_list<std::pair<const char*, bool*>>{{"libcg", &s.libcg}, {"stackalign", &s.stackalign}})
            if (const Value* c = lb->find(key)) {
                if (!c->is_bool()) { err = game + ": 'lindbergh." + key + "' is geen true/false"; return false; }
                *dst = c->b;
            }
        s.ini = expand(s.ini, p);
        s.id = lower(s.id);
        bool hex = s.id.size() == 10 && s.id.compare(0, 2, "0x") == 0;
        for (size_t i = 2; hex && i < s.id.size(); ++i) hex = std::isxdigit(static_cast<unsigned char>(s.id[i])) != 0;
        if (!hex) { err = game + ": 'lindbergh.id' moet 0x plus acht hexcijfers zijn (de spel-ID van lindbergh.so)"; return false; }
        if (s.ini.empty()) { err = game + ": 'lindbergh.ini' ontbreekt"; return false; }
        if (s.jvs != "passthrough" && s.jvs != "emulate") { err = game + ": 'lindbergh.jvs' is passthrough of emulate"; return false; }
        if (s.jvs == "passthrough" && out.route != "jvs") { err = game + ": lindbergh.jvs = passthrough vraagt route jvs (het JVS-bord van de daemon)"; return false; }
        if (!out.arch.empty() && out.arch != "i386") { err = game + ": een Lindbergh-game is i386"; return false; }
        if (!out.loader.empty()) { err = game + ": een Lindbergh-game draait zonder lader (geen 'loader')"; return false; }
    }
    if (out.exe.empty()) { err = game + ": geen exe"; return false; }
    return true;
}

bool plan(const NativeProfile& pr, const NativePaths& p, const PlanOptions& o, NativePlan& out, std::string& err) {
    out = NativePlan{};
    out.notes = pr.notes;
    out.res = !o.res_override.empty() ? o.res_override : pr.res;
    const size_t x = out.res.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= out.res.size()) { err = "ongeldige resolutie: " + out.res; return false; }
    const std::string W = out.res.substr(0, x), H = out.res.substr(x + 1);
    const bool gamescope = pr.launcher == "gamescope";
    const std::string bwrap = find_program("bwrap"), gs = gamescope ? find_program("gamescope") : std::string(), envp = find_program("env");
    if (bwrap.empty()) { err = "bwrap niet gevonden"; return false; }
    if (gamescope && gs.empty()) { err = "gamescope niet gevonden"; return false; }
    if (envp.empty()) { err = "env niet gevonden"; return false; }
    if (gamescope && (p.exec_helper.empty() || ::access(p.exec_helper.c_str(), X_OK) != 0)) {
        err = "rg-exec niet gevonden (" + p.exec_helper + "): het hulpprogramma tussen gamescope en bwrap";
        return false;
    }
    if (!p.nas.empty() && !is_dir(p.nas)) { err = "spelmap " + p.nas + " niet bereikbaar (is de automount actief?)"; return false; }
    if (p.nas.empty() && !p.nas_roots.empty() && std::none_of(p.nas_roots.begin(), p.nas_roots.end(), [](const std::string& r) { return is_dir(r); })) {
        err = "geen van de spelmappen van de app is bereikbaar (" + p.nas_roots.front() + (p.nas_roots.size() > 1 ? ", ..." : "") +
              "; is de automount actief?)";
        return false;
    }
    const std::string rundir = p.runtime_dir.empty() ? "/run/user/" + std::to_string(::getuid()) : p.runtime_dir;
    const std::string ovlbase = p.data + "/native/" + pr.name;

    std::vector<std::string> bw{bwrap, "--die-with-parent", "--unshare-pid", "--unshare-uts", "--unshare-net",
                                "--ro-bind", "/usr", "/usr", "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
                                "--symlink", "usr/bin", "/bin", "--symlink", "usr/bin", "/sbin", "--ro-bind", "/etc", "/etc",
                                "--proc", "/proc", "--dev", "/dev", "--dev-bind", "/dev/dri", "/dev/dri", "--dev-bind-try", "/dev/snd", "/dev/snd",
                                "--ro-bind", "/sys", "/sys", "--ro-bind-try", "/run/udev", "/run/udev", "--tmpfs", "/tmp"};
    if (!pr.share_ipc) bw.push_back("--unshare-ipc");
    bw.insert(bw.end(), {"--dev-bind-try", "/dev/input", "/dev/input"});

    struct Ovl { std::string dst, src, upper; };
    std::vector<Ovl> ovl;
    for (size_t n = 0; n < pr.mounts.size(); ++n) {
        const Mount& m = pr.mounts[n];
        if (!is_dir(m.src)) {
            // ro-try: iets wat de gebruiker zelf heeft of niet (de FMOD-DLL's van een TeknoParrot-installatie); zonder bron start de
            // game gewoon, en wie de bron nodig heeft valt zelf terug (de FMOD-wrapper speelt dan stil).
            if (m.mode == "ro-try") { out.notes.push_back("bron ontbreekt, niet gekoppeld: " + m.src + " -> " + m.dst); continue; }
            err = "bron ontbreekt: " + with_hint(m.src, p);
            return false;
        }
        if (m.mode == "overlay") {
            const std::string up = ovlbase + "/" + std::to_string(n) + "/upper", wk = ovlbase + "/" + std::to_string(n) + "/work";
            out.layer.push_back({LayerStep::Kind::Mkdir, {}, up, {}, {}});
            out.layer.push_back({LayerStep::Kind::Mkdir, {}, wk, {}, {}});
            bw.insert(bw.end(), {"--overlay-src", m.src, "--overlay", up, wk, m.dst});
            ovl.push_back({m.dst, m.src, up});
        } else if (m.mode == "ro" || m.mode == "ro-try") {
            bw.insert(bw.end(), {"--ro-bind", m.src, m.dst});
        } else {
            err = "onbekende mount-modus: " + m.mode;
            return false;
        }
    }
    for (const SrcDst& f : pr.files) {
        if (!exists(f.src)) { err = "bestand ontbreekt: " + f.src; return false; }
        bw.insert(bw.end(), {"--ro-bind", f.src, f.dst});
        std::error_code ec;
        if (fs::is_regular_file(f.src, ec)) out.identity.push_back("meegegeven=" + f.src);
    }
    auto in_layer = [&](const std::string& dst, std::string& upper, std::string& lower) {
        for (const Ovl& v : ovl)
            if (dst.size() > v.dst.size() + 1 && dst.compare(0, v.dst.size() + 1, v.dst + "/") == 0) {
                const std::string rel = dst.substr(v.dst.size() + 1);
                upper = v.upper + "/" + rel;
                lower = v.src + "/" + rel;
                return true;
            }
        return false;
    };
    for (const SrcDst& s : pr.seeds) {
        std::string up, low;
        if (!exists(s.src)) { err = "seed-bron ontbreekt: " + s.src; return false; }
        if (!in_layer(s.dst, up, low)) { err = "seed " + s.dst + " valt niet onder een overlay-mount"; return false; }
        out.layer.push_back({LayerStep::Kind::Seed, s.src, up, low, s.dst, s.merged});
    }
    for (const SrcDst& l : pr.links) {
        std::string up, low;
        if (!in_layer(l.dst, up, low)) { err = "link " + l.dst + " valt niet onder een overlay-mount"; return false; }
        out.layer.push_back({LayerStep::Kind::Link, l.src, up, low, l.dst});
    }
    std::string ldpath;
    // Lindbergh: de laag (lindbergh.so en de vervangende bibliotheken van het baseboard) vooraan in het zoekpad, de spel-ID van de
    // exe nagerekend, lindbergh.ini met de vaste sleutels in de toestandsmap, en de JVS-lijn naar het bord van de daemon.
    std::string lb_preload;
    if (pr.lindbergh.on) {
        const LindberghSpec& s = pr.lindbergh;
        const std::string dir = !p.lindbergh.empty() ? p.lindbergh : p.data + "/lindbergh-loader/current";
        std::vector<std::string> need{"lindbergh.so", "libsegaapi.so", "libkswapapi.so", "libposixtime.so"};
        if (s.libcg) need.push_back("libCg.so");
        if (s.stackalign) need.push_back("librg-stackalign.so");
        std::string missing;
        for (const std::string& f : need)
            if (!exists(dir + "/" + f)) missing += (missing.empty() ? "" : " ") + f;
        if (!missing.empty()) { err = "Lindbergh-laag onvolledig in " + dir + " (" + missing + "): bouw met scripts/build-lindbergh-loader.sh"; return false; }
        bw.insert(bw.end(), {"--ro-bind", dir, "/rg/lindbergh"});
        ldpath = "/rg/lindbergh";
        // De shim eerst: de game komt dan uitgelijnd binnen, ook in lindbergh.so en in wat die zelf aanroept.
        lb_preload = s.stackalign ? "/rg/lindbergh/librg-stackalign.so:/rg/lindbergh/lindbergh.so" : "/rg/lindbergh/lindbergh.so";
        out.identity.push_back("lindbergh-laag=" + dir + "/lindbergh.so");
        if (exists(dir + "/BUILDINFO.txt")) out.identity.push_back("lindbergh-bouw=" + dir + "/BUILDINFO.txt");
        // De spel-ID: lindbergh.so herkent de game alleen aan deze CRC; een andere ELF (bv. ramboD.elf in plaats van ramboM.elf)
        // krijgt geen patches en hangt op de dongle.
        const std::string host_exe = host_of(pr.mounts, pr.exe);
        if (host_exe.empty()) { err = "exe " + pr.exe + " valt niet onder een mount"; return false; }
        const std::string id = lindbergh_game_id(host_exe);
        if (id.empty()) { err = "exe " + host_exe + " is niet te lezen of geen i386-ELF met een uitvoerbaar segment"; return false; }
        if (id != s.id) { err = "verkeerde ELF voor " + pr.name + ": " + host_exe + " heeft spel-ID " + id + ", het profiel verwacht " + s.id; return false; }
        out.notes.push_back("lindbergh: spel-ID " + id + " (" + pr.exe + ")");
        // lindbergh.ini: de gebruikerslaag gaat voor de meegeleverde; de engine zet de sleutels die bij deze route horen.
        std::string ini = s.ini;
        if (!p.user_patches.empty() && fs::is_regular_file(p.user_patches + "/" + pr.name + "/lindbergh.ini")) ini = p.user_patches + "/" + pr.name + "/lindbergh.ini";
        bool ok = false;
        const std::string base = read_file(ini, ok);
        if (!ok) { err = "lindbergh.ini ontbreekt: " + ini; return false; }
        std::vector<IniKey> forced{{"Display", "WIDTH", W}, {"Display", "HEIGHT", H}, {"Display", "FULLSCREEN", "true"}};
        if (s.jvs == "passthrough") {
            forced.push_back({"Emulation", "EMULATE_JVS", "false"});
            forced.push_back({"Paths", "JVS_PATH", std::string("\"") + kLindberghJvsDev + "\""});
            bw.insert(bw.end(), {"--dev-bind", kJvsPtyPlaceholder, kLindberghJvsDev});
            out.needs_jvs_pty = true;
        } else {
            forced.push_back({"Emulation", "EMULATE_JVS", "true"});
        }
        if (s.libcg) forced.push_back({"Paths", "LIBCG_PATH", "\"/rg/lindbergh/libCg.so\""});
        const std::string made = ovlbase + "/lindbergh.ini";
        out.layer.push_back({LayerStep::Kind::Write, "; Gemaakt door RavenGun uit " + ini + " (docs/lindbergh.md); wordt bij elke start opnieuw geschreven.\n" +
                                                         lindbergh_ini(base, forced),
                             made, {}, "/rg/lindbergh.ini"});
        bw.insert(bw.end(), {"--ro-bind", made, "/rg/lindbergh.ini"});
        out.identity.push_back("lindbergh-ini=" + ini);
        // /var/tmp: de games zetten daar markeringen ("touch /var/tmp/atr_init" in HOTD4 Special, /var/tmp/warning in Too Spicy;
        // zonder de map mislukt dat, bench 2026-10-15). Blijvend per game, zoals op de kast; nooit op de NAS.
        const std::string vartmp = ovlbase + "/var-tmp";
        out.layer.push_back({LayerStep::Kind::Mkdir, {}, vartmp, {}, {}});
        bw.insert(bw.end(), {"--bind", vartmp, "/var/tmp"});
    }
    for (const SrcDst& l : pr.libdirs) {
        if (!is_dir(l.src)) { err = "bibliotheekmap ontbreekt: " + with_hint(l.src, p); return false; }
        bw.insert(bw.end(), {"--ro-bind", l.src, l.dst});
        ldpath += (ldpath.empty() ? "" : ":") + l.dst;
    }
    if (!pr.libs.empty()) {
        std::string missing;
        for (const std::string& l : pr.libs) {
            if (!exists(l)) { missing += (missing.empty() ? "" : "; ") + with_hint(l, p); continue; }
            bw.insert(bw.end(), {"--ro-bind", l, "/rg/lib/" + base_name(l)});
            if ((!p.build.empty() && l.rfind(p.build + "/", 0) == 0) || (!p.patches.empty() && l.rfind(p.patches + "/", 0) == 0))
                out.identity.push_back("bibliotheek=" + l);
        }
        if (!missing.empty()) { err = "bibliotheken ontbreken: " + missing; return false; }
        ldpath += (ldpath.empty() ? "" : ":") + std::string("/rg/lib");
    }
    for (const std::string& e : pr.extra_ro) bw.insert(bw.end(), {"--ro-bind-try", e, e});
    // Virtuele webcams: het spel zoekt ze met `ls -1 /dev/video*`; open/ioctl/read vangt tools/v4l2-fake af (RG_V4L2_FAKE).
    for (int i = 0; i < pr.fake_video; ++i) bw.insert(bw.end(), {"--ro-bind", "/dev/null", "/dev/video" + std::to_string(i)});
    if (pr.fake_video > 0) bw.insert(bw.end(), {"--setenv", "RG_V4L2_FAKE", std::to_string(pr.fake_video)});

    // De invoertabellen: WAT een invoerhulp waarheen schrijft staat per game in patches/<Game>/, de gebruikerslaag gaat voor.
    std::vector<std::pair<std::string, std::string>> table_env;
    auto table = [&](const char* lib, const char* file, const char* var) {
        if (!contains(pr.preload, lib)) return;
        for (const auto& [k, v] : pr.env) if (k == var) return;   // het profiel kiest zelf
        for (const std::string& dir : {p.user_patches, p.patches}) {
            if (dir.empty()) continue;
            const std::string t = dir + "/" + pr.name + "/" + file;
            std::error_code ec;
            if (fs::is_regular_file(t, ec)) {
                bw.insert(bw.end(), {"--ro-bind", t, std::string("/rg/tabel/") + file});
                table_env.emplace_back(var, std::string("/rg/tabel/") + file);
                out.identity.push_back(std::string("invoertabel=") + t);
                return;
            }
        }
        out.notes.push_back(std::string("geen ") + file + " voor " + pr.name + ": de invoerhulp doet niets");
    };
    table("librg_rt_input.so", "rt-invoer.ini", "RG_RT_TABLE");
    table("librg_rio_bridge.so", "rio-invoer.ini", "RG_RIO_TABLE");
    table("librg_irtrack.so", "irtrack.ini", "RG_IRT_TABLE");
    for (const SrcDst& b : pr.binds_rw) bw.insert(bw.end(), {"--bind", b.src, b.dst});

    // Het scherm. Met gamescope: diens Xwayland (DISPLAY komt van gamescope, het nummer is nu nog onbekend: de hele socketmap).
    if (gamescope) {
        bw.insert(bw.end(), {"--ro-bind-try", "/tmp/.X11-unix", "/tmp/.X11-unix"});
    } else {
        const std::string disp = o.host_display.empty() ? ":0" : o.host_display;
        std::string num = disp.substr(disp.find(':') == std::string::npos ? 0 : disp.find(':') + 1);
        if (num.find('.') != std::string::npos) num.resize(num.find('.'));
        bw.insert(bw.end(), {"--dir", "/tmp/.X11-unix", "--ro-bind", "/tmp/.X11-unix/X" + num, "/tmp/.X11-unix/X" + num, "--setenv", "DISPLAY", disp});
        if (!o.host_xauthority.empty() && ::access(o.host_xauthority.c_str(), R_OK) == 0)
            bw.insert(bw.end(), {"--ro-bind", o.host_xauthority, "/tmp/.Xauthority", "--setenv", "XAUTHORITY", "/tmp/.Xauthority"});
    }
    bw.insert(bw.end(), {"--dir", rundir, "--chmod", "0700", rundir, "--ro-bind-try", rundir + "/pulse", rundir + "/pulse", "--ro-bind-try",
                         rundir + "/pipewire-0", rundir + "/pipewire-0", "--setenv", "PULSE_SERVER", "unix:" + rundir + "/pulse/native"});

    std::vector<std::string> gamecmd{pr.exe};
    for (std::string a : pr.args) {
        for (size_t at = a.find("{res}"); at != std::string::npos; at = a.find("{res}", at + out.res.size())) a.replace(at, 5, out.res);
        gamecmd.push_back(a);
    }
    out.comm = base_name(pr.exe).substr(0, 15);
    if (!pr.loader.empty()) {
        const std::string lb = !p.loader.empty() ? p.loader : p.data + "/bin/rg-elfloader";
        if (::access(lb.c_str(), X_OK) != 0) { err = "rg-elfloader niet gevonden (" + lb + "); bouw met scripts/build-elfloader.sh"; return false; }
        bw.insert(bw.end(), {"--ro-bind", lb, "/rg/bin/rg-elfloader"});
        gamecmd.insert(gamecmd.begin(), "/rg/bin/rg-elfloader");
        out.identity.push_back("lader=" + lb);
        out.comm = "rg-elfloader";
    }

    bw.insert(bw.end(), {"--chdir", pr.cwd.empty() ? "/" : pr.cwd, "--setenv", "HOME", pr.home.empty() ? "/tmp" : pr.home, "--setenv", "PATH",
                         "/usr/bin", "--setenv", "XDG_RUNTIME_DIR", rundir, "--setenv", "LANG", "C.UTF-8", "--unsetenv", "WAYLAND_DISPLAY"});
    if (pr.lindbergh.on) {
        // Zoals de starter van lindbergh-loader: de map van de game, daarin lib/ en ../lib (librnalindbergh_jr.so e.d.).
        const std::string gd = pr.cwd.empty() ? parent_dir(pr.exe) : pr.cwd;
        ldpath += ":" + gd + ":" + gd + "/lib:" + parent_dir(gd) + "/lib";
        bw.insert(bw.end(), {"--setenv", "LINDBERGH_CONFIG_PATH", "/rg/lindbergh.ini", "--setenv", "LINDBERGH_LOADER_CURRENT_DIR", "/rg/lindbergh"});
    }
    if (!ldpath.empty()) bw.insert(bw.end(), {"--setenv", "LD_LIBRARY_PATH", ldpath});
    const std::string preload = lb_preload.empty() ? pr.preload : pr.preload.empty() ? lb_preload : lb_preload + ":" + pr.preload;
    if (!preload.empty()) bw.insert(bw.end(), {"--setenv", "LD_PRELOAD", preload});
    for (const auto& [k, v] : pr.env) bw.insert(bw.end(), {"--setenv", k, v});
    for (const auto& [k, v] : o.env) bw.insert(bw.end(), {"--setenv", k, v});
    for (const auto& [k, v] : table_env) bw.insert(bw.end(), {"--setenv", k, v});

    // Schone omgeving voor gamescope (en dus voor bwrap): alleen wat nodig is; gamescope voegt DISPLAY e.d. toe voor zijn kind.
    out.argv = {envp, "-i", "HOME=" + (p.home.empty() ? std::string("/tmp") : p.home), "PATH=/usr/bin:/bin", "XDG_RUNTIME_DIR=" + rundir,
                "LANG=C.UTF-8"};
    if (gamescope) {
        out.argv.push_back("WAYLAND_DISPLAY=" + o.wayland_display);
        out.argv.insert(out.argv.end(), {gs, "-f", "-W", std::to_string(o.out_w), "-H", std::to_string(o.out_h), "-w", W, "-h", H,
                                         "--force-grab-cursor", "--", p.exec_helper, "--keep", "DISPLAY", "--"});
        out.identity.push_back("hulp=" + p.exec_helper);
    }
    out.argv.insert(out.argv.end(), bw.begin(), bw.end());
    out.argv.insert(out.argv.end(), gamecmd.begin(), gamecmd.end());
    if (const std::string f = profile_file(p, pr.name); !f.empty()) out.identity.insert(out.identity.begin(), "gegevens=" + f);
    return true;
}

bool apply_layer(const NativePlan& pl, std::vector<std::string>& log, std::string& err) {
    for (const LayerStep& s : pl.layer) {
        std::error_code ec;
        if (s.kind == LayerStep::Kind::Mkdir) {
            fs::create_directories(s.to, ec);
            if (ec) { err = "map " + s.to + " maken: " + ec.message(); return false; }
            continue;
        }
        if (s.kind == LayerStep::Kind::Write) {
            fs::create_directories(fs::path(s.to).parent_path(), ec);
            std::ofstream f(s.to, std::ios::trunc | std::ios::binary);
            f << s.from;
            f.close();
            if (!f) { err = "schrijven van " + s.to + " mislukt"; return false; }
            log.push_back("geschreven: " + s.label + " (" + s.to + ")");
            continue;
        }
        const std::string what = s.kind == LayerStep::Kind::Seed ? "seed" : "link";
        // Een seed uit een samengevoegde patches-map: een eerdere start kan de schrijflaag gezaaid hebben uit een verouderde of halve
        // gebruikersmap. Wat er staat blijft (het spel kan het bijgewerkt hebben), maar wat ontbreekt wordt aangevuld.
        if (s.kind == LayerStep::Kind::Seed && s.merged && exists(s.to) && !exists(s.lower) && is_dir(s.from) && is_dir(s.to)) {
            int erbij = 0;
            fs::recursive_directory_iterator it(s.from, fs::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                // lexically_relative: fs::relative volgt koppelingen (weakly_canonical); een koppeling met een ../-doel gaf dan een pad
                // buiten de schrijflaag en copy_symlink schreef daar.
                const fs::path doel = fs::path(s.to) / it->path().lexically_relative(s.from);
                const fs::file_status st = it->symlink_status(e2);
                if (!fs::is_directory(st)) it.disable_recursion_pending();   // alleen echte mappen in; een koppeling (ook naar een map) is een los item
                if (fs::exists(fs::symlink_status(doel, e2))) continue;   // bestaat al: blijft
                if (fs::is_directory(st)) fs::create_directories(doel, e2);
                else if (fs::is_symlink(st)) fs::copy_symlink(it->path(), doel, e2);
                else fs::copy_file(it->path(), doel, fs::copy_options::skip_existing, e2);
                if (e2) { err = "seed " + s.label + ": aanvullen mislukt (" + doel.string() + "): " + e2.message(); return false; }
                if (!fs::is_directory(st)) ++erbij;
            }
            if (ec) { err = "seed " + s.label + ": aanvullen mislukt: " + ec.message(); return false; }
            log.push_back("seed: " + s.label + " bestaat al (blijft)" +
                          (erbij ? ", " + std::to_string(erbij) + " ontbrekende bestanden aangevuld uit " + s.from : std::string()));
            continue;
        }
        if (exists(s.to) || exists(s.lower)) { log.push_back(what + ": " + s.label + " bestaat al (blijft)"); continue; }
        fs::create_directories(fs::path(s.to).parent_path(), ec);
        if (ec) { err = what + " " + s.label + ": map maken: " + ec.message(); return false; }
        if (s.kind == LayerStep::Kind::Seed) {
            fs::copy(s.from, s.to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (ec) { err = "seed " + s.label + ": kopiëren mislukt: " + ec.message(); return false; }
            log.push_back("seed: " + s.label + " gezet uit " + s.from);
        } else {
            fs::create_symlink(s.from, s.to, ec);
            if (ec) { err = "link " + s.label + ": maken mislukt: " + ec.message(); return false; }
            log.push_back("link: " + s.label + " -> " + s.from + " gezet");
        }
    }
    return true;
}

std::vector<std::string> identity_lines(const NativePlan& pl) {
    std::vector<std::string> out;
    for (const std::string& item : pl.identity) {
        const size_t eq = item.find('=');
        const std::string label = item.substr(0, eq), path = eq == std::string::npos ? item : item.substr(eq + 1);
        const std::string h = sha256_file(path);
        out.push_back("  " + label + ": " + path + (h.empty() ? " (niet te lezen)" : " sha256 " + h));
    }
    return out;
}

}  // namespace ravengun::launch
