#include "ravengun/launch/native.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "ravengun/json_parse.hpp"

namespace fs = std::filesystem;

namespace ravengun::launch {
namespace {

using json::Value;

std::string expand(std::string s, const NativePaths& p) {
    const std::pair<const char*, const std::string*> keys[] = {{"@nas", &p.nas},     {"@tpui", &p.tpui},   {"@data", &p.data},
                                                               {"@lib32", &p.lib32}, {"@build", &p.build}, {"@patches", &p.patches}};
    for (const auto& [k, v] : keys) {
        const size_t kl = std::strlen(k);
        for (size_t at = s.find(k); at != std::string::npos; at = s.find(k, at + v->size())) s.replace(at, kl, *v);
    }
    return s;
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

}  // namespace

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
    if (!r.ok) { err = "native-games.json: " + r.error; return false; }
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
    auto pairs = [&](const char* key, const char* a, const char* b, std::vector<SrcDst>& to) {
        const Value* v = g->find(key);
        if (!v) return true;
        if (!v->is_array()) { err = game + ": '" + key + "' is geen lijst"; return false; }
        for (const Value& e : v->arr) {
            const std::string s = str_of(e.find(a)), d = str_of(e.find(b));
            if (s.empty() || d.empty() || d[0] != '/') { err = game + ": '" + key + "' heeft een regel zonder " + a + " of met een relatief " + b; return false; }
            to.push_back({std::string(a) == "target" ? s : expand(s, p), d});
        }
        return true;
    };
    auto strings = [&](const char* key, std::vector<std::string>& to, bool exp) {
        const Value* v = g->find(key);
        if (!v) return true;
        if (!v->is_array()) { err = game + ": '" + key + "' is geen lijst"; return false; }
        for (const Value& e : v->arr) {
            if (!e.is_string()) { err = game + ": '" + key + "' bevat iets anders dan tekst"; return false; }
            to.push_back(exp ? expand(e.str, p) : e.str);
        }
        return true;
    };
    if (const Value* m = g->find("mounts")) {
        if (!m->is_array()) { err = game + ": 'mounts' is geen lijst"; return false; }
        for (const Value& e : m->arr) {
            Mount mt{str_of(e.find("mode")), expand(str_of(e.find("src")), p), str_of(e.find("dst"))};
            if (mt.mode.empty()) mt.mode = "ro";
            if (mt.src.empty() || mt.dst.empty() || mt.dst[0] != '/') { err = game + ": een mount zonder src of met een relatief dst"; return false; }
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
    if (out.exe.empty()) { err = game + ": geen exe"; return false; }
    return true;
}

bool plan(const NativeProfile& pr, const NativePaths& p, const PlanOptions& o, NativePlan& out, std::string& err) {
    out = NativePlan{};
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
    if (!is_dir(p.nas)) { err = "NAS-map " + p.nas + " niet bereikbaar (is de automount actief?)"; return false; }
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
        if (!is_dir(m.src)) { err = "bron ontbreekt: " + m.src; return false; }
        if (m.mode == "overlay") {
            const std::string up = ovlbase + "/" + std::to_string(n) + "/upper", wk = ovlbase + "/" + std::to_string(n) + "/work";
            out.layer.push_back({LayerStep::Kind::Mkdir, {}, up, {}, {}});
            out.layer.push_back({LayerStep::Kind::Mkdir, {}, wk, {}, {}});
            bw.insert(bw.end(), {"--overlay-src", m.src, "--overlay", up, wk, m.dst});
            ovl.push_back({m.dst, m.src, up});
        } else if (m.mode == "ro") {
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
        out.layer.push_back({LayerStep::Kind::Seed, s.src, up, low, s.dst});
    }
    for (const SrcDst& l : pr.links) {
        std::string up, low;
        if (!in_layer(l.dst, up, low)) { err = "link " + l.dst + " valt niet onder een overlay-mount"; return false; }
        out.layer.push_back({LayerStep::Kind::Link, l.src, up, low, l.dst});
    }
    std::string ldpath;
    for (const SrcDst& l : pr.libdirs) {
        if (!is_dir(l.src)) { err = "bibliotheekmap ontbreekt: " + l.src; return false; }
        bw.insert(bw.end(), {"--ro-bind", l.src, l.dst});
        ldpath += (ldpath.empty() ? "" : ":") + l.dst;
    }
    if (!pr.libs.empty()) {
        std::string missing;
        for (const std::string& l : pr.libs) {
            if (!exists(l)) { missing += (missing.empty() ? "" : " ") + l; continue; }
            bw.insert(bw.end(), {"--ro-bind", l, "/rg/lib/" + base_name(l)});
            if ((!p.build.empty() && l.rfind(p.build + "/", 0) == 0) || (!p.patches.empty() && l.rfind(p.patches + "/", 0) == 0))
                out.identity.push_back("bibliotheek=" + l);
        }
        if (!missing.empty()) { err = "bibliotheken ontbreken: " + missing; return false; }
        ldpath += (ldpath.empty() ? "" : ":") + std::string("/rg/lib");
    }
    for (const std::string& e : pr.extra_ro) bw.insert(bw.end(), {"--ro-bind-try", e, e});

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
    if (!ldpath.empty()) bw.insert(bw.end(), {"--setenv", "LD_LIBRARY_PATH", ldpath});
    if (!pr.preload.empty()) bw.insert(bw.end(), {"--setenv", "LD_PRELOAD", pr.preload});
    for (const auto& [k, v] : pr.env) bw.insert(bw.end(), {"--setenv", k, v});
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
    if (!p.json.empty()) out.identity.insert(out.identity.begin(), "gegevens=" + p.json);
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
        const std::string what = s.kind == LayerStep::Kind::Seed ? "seed" : "link";
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
