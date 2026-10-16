// Tests voor de native route zonder shellscripts (src/ravengun/launch/native.*): profiel lezen met @-paden, het commando
// (env -i -> gamescope -> bwrap -> rg-elfloader -> game), de invoertabel (gebruikerslaag voor meegeleverd), de schrijflaag (seed en
// link alleen als het pad er nog niet is) en sha256. Gebouwd met tijdelijke mappen en nep-programma's in PATH; start niets.
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ravengun/launch/native.hpp"

namespace fs = std::filesystem;
using namespace ravengun::launch;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void write(const fs::path& p, const std::string& text, bool exec = false) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
    if (exec) ::chmod(p.c_str(), 0755);
}
// Staat de reeks `seq` aaneengesloten in `v`?
static bool has_seq(const std::vector<std::string>& v, const std::vector<std::string>& seq) {
    return std::search(v.begin(), v.end(), seq.begin(), seq.end()) != v.end();
}
static size_t index_of(const std::vector<std::string>& v, const std::string& s) {
    return static_cast<size_t>(std::find(v.begin(), v.end(), s) - v.begin());
}

// Referentie voor de spel-ID: CRC-32 met een tabel (een andere uitvoering dan die in native.cpp).
static uint32_t crc32_ref(const unsigned char* p, size_t n) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Een kleine i386-ELF met drie programmakoppen; de derde is PT_LOAD R+X op bestandsoffset `seg`. Geeft de verwachte spel-ID.
static std::string write_elf(const fs::path& path, uint32_t seg, unsigned char fill) {
    std::vector<unsigned char> f(seg + 10 + 0x4000 + 64, 0);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) f[o + i] = static_cast<unsigned char>(v >> (8 * i)); };
    auto put16 = [&](size_t o, uint16_t v) { f[o] = static_cast<unsigned char>(v); f[o + 1] = static_cast<unsigned char>(v >> 8); };
    f[0] = 0x7f; f[1] = 'E'; f[2] = 'L'; f[3] = 'F'; f[4] = 1; f[5] = 1; f[6] = 1;
    put16(16, 2); put16(18, 3);           // ET_EXEC, EM_386
    put32(28, 52);                        // e_phoff
    put16(42, 32); put16(44, 3);          // e_phentsize, e_phnum
    const size_t ph2 = 52 + 2 * 32;
    put32(ph2, 1); put32(ph2 + 4, seg); put32(ph2 + 8, 0x08048000 + seg); put32(ph2 + 24, 5);
    for (size_t i = seg; i < f.size(); ++i) f[i] = static_cast<unsigned char>(fill + i * 7);
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
    char id[16];
    std::snprintf(id, sizeof id, "0x%08x", crc32_ref(f.data() + seg + 10, 0x4000));
    return id;
}

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// De Lindbergh-route: spel-ID, lindbergh.ini met de vaste sleutels, de laag in het zoekpad, de JVS-pty als apparaat.
static void test_lindbergh(const fs::path& root, NativePaths p) {
    write(root / "bin" / "gamescope", "#!/bin/sh\nexit 0\n", true);

    // Spel-ID tegen de referentie; geen ELF, een te kort bestand of een verkeerde derde programmakop: leeg.
    const std::string id = write_elf(p.nas + "/Lb/elf/game.elf", 0x200, 0x11);
    CHECK(lindbergh_game_id(p.nas + "/Lb/elf/game.elf") == id);
    CHECK(id.size() == 10 && id.compare(0, 2, "0x") == 0);
    write(root / "geen.elf", "geen elf");
    CHECK(lindbergh_game_id((root / "geen.elf").string()).empty());
    CHECK(lindbergh_game_id((root / "ontbreekt.elf").string()).empty());
    {
        const std::string other = write_elf(root / "ander.elf", 0x200, 0x12);
        CHECK(other != id);
        std::fstream f(root / "ander.elf", std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(52 + 2 * 32 + 24);
        f.put(6);   // RW in plaats van R+X: lindbergh.so slaat zo'n kop over
        f.close();
        CHECK(lindbergh_game_id((root / "ander.elf").string()).empty());
    }
    // Gemeten op de NAS (2026-10-14): de dump van HOTD4 Special is Rev B (0x7d6ab3e3 in config.h van lindbergh-loader).
    if (const char* h = std::getenv("HOME")) {
        const std::string nas = std::string(h) + "/mnt/spidernas_emulation/Teknoparrot/The House of the Dead 4 Special/elf/hod4M.elf";
        if (fs::exists(nas)) CHECK(lindbergh_game_id(nas) == "0x7d6ab3e3");
    }

    // lindbergh.ini: vervangen in de sectie, aanvullen achteraan de sectie, nieuwe sectie, commentaar blijft.
    const std::string base =
        "; mijn instellingen\n[Display]\nWIDTH = 640\nHEIGHT = 480\n\n[Emulation]\nemulate_jvs = true\nFREEPLAY = true\n\n[Graphics]\nGPU_VENDOR = 2\n";
    const std::string ini = lindbergh_ini(base, {{"Display", "WIDTH", "1024"}, {"Display", "HEIGHT", "768"}, {"Display", "FULLSCREEN", "true"},
                                                 {"Emulation", "EMULATE_JVS", "false"}, {"Paths", "JVS_PATH", "\"/dev/ttyRG0\""}});
    CHECK(ini.find("; mijn instellingen\n[Display]\nWIDTH = 1024\nHEIGHT = 768\nFULLSCREEN = true\n\n[Emulation]\n") == 0);
    CHECK(ini.find("EMULATE_JVS = false\nFREEPLAY = true\n") != std::string::npos);
    CHECK(ini.find("emulate_jvs = true") == std::string::npos && ini.find("640") == std::string::npos);
    CHECK(ini.find("[Graphics]\nGPU_VENDOR = 2\n\n[Paths]\nJVS_PATH = \"/dev/ttyRG0\"\n") != std::string::npos);
    CHECK(lindbergh_ini("", {{"Paths", "JVS_PATH", "x"}}) == "[Paths]\nJVS_PATH = x\n");

    // Het profiel.
    const fs::path layer = root / "lindbergh-loader";
    for (const char* f : {"lindbergh.so", "libsegaapi.so", "libkswapapi.so", "libposixtime.so", "BUILDINFO.txt"}) write(layer / f, "x");
    p.lindbergh = layer.string();
    write(p.patches + "/Lb/lindbergh.ini", "[Graphics]\nGPU_VENDOR = 2\n");
    const std::string json = R"({"schema":1,"games":{
      "Lb":{"title":"Lindbergh-test","enabled":true,"arch":"i386","launcher":"gamescope","route":"jvs",
        "lindbergh":{"id":")" + id + R"(","ini":"@patches/Lb/lindbergh.ini","jvs":"passthrough"},
        "mounts":[{"src":"@nas/Lb","dst":"/lb","mode":"overlay"}],
        "libdirs":[{"src":"@lib32","dst":"/rg/lib32"}],
        "env":{"MESA_LOADER_DRIVER_OVERRIDE":"zink"},
        "exe":"/lb/elf/game.elf","cwd":"/lb/elf","home":"/lb/elf","res":"1024x768"},
      "LbEmu":{"route":"evdev","lindbergh":{"id":")" + id + R"(","ini":"@patches/Lb/lindbergh.ini","jvs":"emulate","libcg":true},
        "mounts":[{"src":"@nas/Lb","dst":"/lb","mode":"overlay"}],"exe":"/lb/elf/game.elf","cwd":"/lb/elf"},
      "LbFoutRoute":{"route":"tp-shm","lindbergh":{"id":"0x12345678","ini":"x"},"exe":"/x"},
      "LbFoutId":{"route":"jvs","lindbergh":{"id":"7d6ab3e3","ini":"x"},"exe":"/x"},
      "LbFoutLader":{"route":"jvs","loader":"rg-elfloader","lindbergh":{"id":"0x12345678","ini":"x"},"exe":"/x"}}})";
    NativeProfile pr;
    std::string err;
    CHECK(load_profile(json, "Lb", p, pr, err));
    CHECK(pr.lindbergh.on && pr.lindbergh.id == id && pr.lindbergh.ini == p.patches + "/Lb/lindbergh.ini" && pr.lindbergh.jvs == "passthrough" && !pr.lindbergh.libcg);
    CHECK(!load_profile(json, "LbFoutRoute", p, pr, err) && err.find("route jvs") != std::string::npos);
    CHECK(!load_profile(json, "LbFoutId", p, pr, err) && err.find("acht hexcijfers") != std::string::npos);
    CHECK(!load_profile(json, "LbFoutLader", p, pr, err) && err.find("zonder lader") != std::string::npos);

    CHECK(load_profile(json, "Lb", p, pr, err));
    NativePlan pl;
    err.clear();
    CHECK(plan(pr, p, PlanOptions{}, pl, err));
    if (!err.empty()) std::fprintf(stderr, "plan: %s\n", err.c_str());
    const auto& a = pl.argv;
    CHECK(has_seq(a, {"--ro-bind", layer.string(), "/rg/lindbergh"}));
    CHECK(has_seq(a, {"--dev-bind", kJvsPtyPlaceholder, "/dev/ttyRG0"}));
    CHECK(pl.needs_jvs_pty);
    CHECK(has_seq(a, {"--setenv", "LD_PRELOAD", "/rg/lindbergh/lindbergh.so"}));
    CHECK(has_seq(a, {"--setenv", "LD_LIBRARY_PATH", "/rg/lindbergh:/rg/lib32:/lb/elf:/lb/elf/lib:/lb/lib"}));
    CHECK(has_seq(a, {"--setenv", "LINDBERGH_CONFIG_PATH", "/rg/lindbergh.ini"}));
    CHECK(has_seq(a, {"--setenv", "MESA_LOADER_DRIVER_OVERRIDE", "zink"}));
    CHECK(has_seq(a, {"--ro-bind", p.data + "/native/Lb/lindbergh.ini", "/rg/lindbergh.ini"}));
    CHECK(has_seq(a, {"--bind", p.data + "/native/Lb/var-tmp", "/var/tmp"}));
    CHECK(std::any_of(pl.layer.begin(), pl.layer.end(), [&](const LayerStep& s) { return s.kind == LayerStep::Kind::Mkdir && s.to == p.data + "/native/Lb/var-tmp"; }));
    CHECK(has_seq(a, {"-w", "1024", "-h", "768"}));
    CHECK(a.back() == "/lb/elf/game.elf" && pl.comm == "game.elf");
    CHECK(std::find(a.begin(), a.end(), "/rg/bin/rg-elfloader") == a.end());
    // De ini staat in de schrijfstappen, met de sleutels van de route.
    const LayerStep* w = nullptr;
    for (const LayerStep& s : pl.layer) if (s.kind == LayerStep::Kind::Write) w = &s;
    CHECK(w && w->to == p.data + "/native/Lb/lindbergh.ini");
    if (w) {
        CHECK(w->from.find("[Graphics]\nGPU_VENDOR = 2\n") != std::string::npos);
        CHECK(w->from.find("EMULATE_JVS = false") != std::string::npos && w->from.find("JVS_PATH = \"/dev/ttyRG0\"") != std::string::npos);
        CHECK(w->from.find("WIDTH = 1024") != std::string::npos && w->from.find("FULLSCREEN = true") != std::string::npos);
    }
    std::vector<std::string> log;
    CHECK(apply_layer(pl, log, err));
    CHECK(slurp(p.data + "/native/Lb/lindbergh.ini").find("JVS_PATH = \"/dev/ttyRG0\"") != std::string::npos);
    const auto ids = identity_lines(pl);
    CHECK(std::any_of(ids.begin(), ids.end(), [&](const std::string& l) { return l.find("lindbergh-laag: " + layer.string() + "/lindbergh.so sha256 ") != std::string::npos; }));
    CHECK(std::any_of(pl.notes.begin(), pl.notes.end(), [&](const std::string& l) { return l.find("spel-ID " + id) != std::string::npos; }));

    // De pty: zonder link een duidelijke fout; met link het doel in het commando.
    NativePlan pj = pl;
    CHECK(!resolve_jvs(pj, (root / "run/geen-link").string(), err) && err.find("pty") != std::string::npos);
    write(root / "pts/7", "");
    fs::create_directories(root / "run/ravengun");
    fs::create_symlink(root / "pts/7", root / "run/ravengun/jvs-com");
    CHECK(resolve_jvs(pj, (root / "run/ravengun/jvs-com").string(), err));
    CHECK(has_seq(pj.argv, {"--dev-bind", (root / "pts/7").string(), "/dev/ttyRG0"}) && !pj.needs_jvs_pty);
    CHECK(std::find(pj.argv.begin(), pj.argv.end(), kJvsPtyPlaceholder) == pj.argv.end());

    // De gebruikerslaag van lindbergh.ini gaat voor.
    write(p.user_patches + "/Lb/lindbergh.ini", "[Emulation]\nFREEPLAY = false\n");
    CHECK(plan(pr, p, PlanOptions{}, pl, err));
    w = nullptr;
    for (const LayerStep& s : pl.layer) if (s.kind == LayerStep::Kind::Write) w = &s;
    CHECK(w && w->from.find("FREEPLAY = false") != std::string::npos && w->from.find("GPU_VENDOR") == std::string::npos);

    // Emulatie: geen pty, wel libCg; zonder libCg.so in de laag een fout met het bouwscript.
    CHECK(load_profile(json, "LbEmu", p, pr, err));
    CHECK(!plan(pr, p, PlanOptions{}, pl, err) && err.find("libCg.so") != std::string::npos && err.find("build-lindbergh-loader.sh") != std::string::npos);
    write(layer / "libCg.so", "x");
    CHECK(plan(pr, p, PlanOptions{}, pl, err));
    CHECK(!pl.needs_jvs_pty);
    CHECK(std::find(pl.argv.begin(), pl.argv.end(), kJvsPtyPlaceholder) == pl.argv.end());
    w = nullptr;
    for (const LayerStep& s : pl.layer) if (s.kind == LayerStep::Kind::Write) w = &s;
    CHECK(w && w->from.find("EMULATE_JVS = true") != std::string::npos && w->from.find("LIBCG_PATH = \"/rg/lindbergh/libCg.so\"") != std::string::npos);

    // stackalign: de shim VOOR lindbergh.so in LD_PRELOAD; zonder de shim in de laag een fout met het bouwscript.
    {
        const std::string js = R"({"schema":1,"games":{"LbAlign":{"route":"jvs","lindbergh":{"id":")" + id +
                               R"(","ini":"@patches/Lb/lindbergh.ini","stackalign":true},"mounts":[{"src":"@nas/Lb","dst":"/lb","mode":"overlay"}],)"
                               R"("exe":"/lb/elf/game.elf","cwd":"/lb/elf"},"LbAlignFout":{"route":"jvs","lindbergh":{"id":")" + id +
                               R"(","ini":"x","stackalign":"ja"},"exe":"/x"}}})";
        NativeProfile pa;
        CHECK(load_profile(js, "LbAlign", p, pa, err) && pa.lindbergh.stackalign);
        CHECK(!load_profile(js, "LbAlignFout", p, pa, err) && err.find("lindbergh.stackalign") != std::string::npos);
        CHECK(load_profile(js, "LbAlign", p, pa, err));
        CHECK(!plan(pa, p, PlanOptions{}, pl, err) && err.find("librg-stackalign.so") != std::string::npos);
        write(layer / "librg-stackalign.so", "x");
        CHECK(plan(pa, p, PlanOptions{}, pl, err));
        CHECK(has_seq(pl.argv, {"--setenv", "LD_PRELOAD", "/rg/lindbergh/librg-stackalign.so:/rg/lindbergh/lindbergh.so"}));
    }

    // Een andere ELF dan het profiel verwacht: niet starten.
    CHECK(load_profile(json, "Lb", p, pr, err));
    write_elf(p.nas + "/Lb/elf/game.elf", 0x200, 0x55);
    CHECK(!plan(pr, p, PlanOptions{}, pl, err) && err.find("verkeerde ELF") != std::string::npos);
    // Laag niet gebouwd.
    p.lindbergh = (root / "geen-laag").string();
    CHECK(!plan(pr, p, PlanOptions{}, pl, err) && err.find("build-lindbergh-loader.sh") != std::string::npos);
}

int main() {
    char tmpl[] = "/tmp/rg-native-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const fs::path root = t;

    // sha256: bekende testwaarden (FIPS 180-4).
    write(root / "abc.txt", "abc");
    CHECK(sha256_file((root / "abc.txt").string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    write(root / "empty.txt", "");
    CHECK(sha256_file((root / "empty.txt").string()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_file((root / "ontbreekt").string()).empty());

    // Nep-programma's: plan() zoekt bwrap, gamescope en env in PATH.
    for (const char* prog : {"bwrap", "gamescope", "env"}) write(root / "bin" / prog, "#!/bin/sh\nexit 0\n", true);
    setenv("PATH", (root / "bin").c_str(), 1);

    NativePaths p;
    p.json = (root / "native-games.json").string();
    p.nas = (root / "nas").string();
    p.data = (root / "data").string();
    p.lib32 = p.data + "/runtime/lib32";
    p.build = (root / "build").string();
    p.patches = (root / "patches").string();
    p.user_patches = (root / "user-patches").string();
    p.runtime_dir = (root / "run").string();
    p.home = "/home/speler";
    p.loader = (root / "rg-elfloader").string();
    write(p.loader, "#!/bin/sh\n", true);
    p.exec_helper = (root / "rg-exec").string();
    write(p.exec_helper, "#!/bin/sh\n", true);
    fs::create_directories(p.nas + "/Game/pmuser");
    fs::create_directories(p.nas + "/Game/programs_dec");
    write(p.build + "/lib32/librg_rio_bridge.so", "x");
    write(p.patches + "/Game/dropin/hook.so", "x");
    write(p.patches + "/Game/dropin/slots.tsv", "slots");
    write(p.patches + "/Game/dropin/dongle/Game/dongle.json", "{}");
    write(p.patches + "/Game/rio-invoer.ini", "meegeleverd");
    write(p.lib32 + "/libSDL-1.2.so.0", "x");

    const std::string json = R"({"schema":1,"defaults":{"res":"1920x1080"},"games":{
      "Game":{"title":"Testspel","enabled":true,"arch":"i386","loader":"rg-elfloader","launcher":"gamescope",
        "mounts":[{"src":"@nas/Game","dst":"/pm","mode":"overlay"},{"src":"@nas/Game/programs_dec","dst":"/pm/data/programs_enc","mode":"ro"}],
        "files":[{"src":"@patches/Game/dropin/slots.tsv","dst":"/rg/slots.tsv"}],
        "seed":[{"src":"@patches/Game/dropin/dongle","dst":"/pm/dongle"}],
        "links":[{"target":"../../pmuser","dst":"/pm/g5/dino/pmuser"}],
        "libs":["@lib32/libSDL-1.2.so.0","@patches/Game/dropin/hook.so","@build/lib32/librg_rio_bridge.so"],
        "extra_ro":["/dev/shm/ravengun_JvsState"],
        "preload":"/rg/lib/hook.so:/rg/lib/librg_rio_bridge.so",
        "env":{"SDL12COMPAT_OPENGL_SCALING":"0","RG_ELF_SLOT_TABLE":"/rg/slots.tsv"},
        "exe":"/pm/game","cwd":"/pm","home":"/pm","args":["-n","-f{res}"],"res":"1360x768","route":"tp-shm"},
      "Uit":{"enabled":false,"exe":"/x"}}})";
    write(p.json, json);

    CHECK(profile_enabled(json, "Game"));
    CHECK(!profile_enabled(json, "Uit"));
    CHECK(!profile_enabled(json, "Bestaatniet"));
    CHECK(!profile_enabled("{kapot", "Game"));

    NativeProfile pr;
    std::string err;
    CHECK(load_profile(json, "Game", p, pr, err));
    CHECK(err.empty());
    CHECK(pr.mounts.size() == 2 && pr.mounts[0].src == p.nas + "/Game" && pr.mounts[0].mode == "overlay");
    CHECK(pr.links.size() == 1 && pr.links[0].src == "../../pmuser");   // het doel van een link wordt niet ingevuld
    CHECK(pr.libs.size() == 3 && pr.libs[2] == p.build + "/lib32/librg_rio_bridge.so");
    CHECK(pr.res == "1360x768" && pr.route == "tp-shm");
    CHECK(!load_profile(json, "Bestaatniet", p, pr, err) && err.find("onbekend") != std::string::npos);

    CHECK(load_profile(json, "Game", p, pr, err));
    NativePlan pl;
    err.clear();
    CHECK(plan(pr, p, PlanOptions{}, pl, err));
    if (!err.empty()) std::fprintf(stderr, "plan: %s\n", err.c_str());
    const auto& a = pl.argv;
    // env -i met een schone omgeving, dan gamescope op de tv-resolutie met de spelresolutie, dan bwrap.
    CHECK(a.size() > 4 && a[0] == (root / "bin/env").string() && a[1] == "-i");
    CHECK(has_seq(a, {"WAYLAND_DISPLAY=wayland-0", (root / "bin/gamescope").string(), "-f", "-W", "1920", "-H", "1080", "-w", "1360", "-h", "768"}));
    CHECK(has_seq(a, {"--force-grab-cursor", "--", p.exec_helper, "--keep", "DISPLAY", "--", (root / "bin/bwrap").string()}));
    CHECK(has_seq(a, {"--overlay-src", p.nas + "/Game", "--overlay", p.data + "/native/Game/0/upper", p.data + "/native/Game/0/work", "/pm"}));
    CHECK(index_of(a, "/pm/data/programs_enc") > index_of(a, "--overlay"));   // ro-mount na de overlay eronder
    CHECK(has_seq(a, {"--ro-bind", p.patches + "/Game/dropin/slots.tsv", "/rg/slots.tsv"}));
    CHECK(has_seq(a, {"--ro-bind", p.build + "/lib32/librg_rio_bridge.so", "/rg/lib/librg_rio_bridge.so"}));
    CHECK(has_seq(a, {"--ro-bind-try", "/dev/shm/ravengun_JvsState", "/dev/shm/ravengun_JvsState"}));
    CHECK(has_seq(a, {"--ro-bind-try", "/tmp/.X11-unix", "/tmp/.X11-unix"}));
    CHECK(has_seq(a, {"--setenv", "LD_LIBRARY_PATH", "/rg/lib"}));
    CHECK(has_seq(a, {"--setenv", "LD_PRELOAD", "/rg/lib/hook.so:/rg/lib/librg_rio_bridge.so"}));
    CHECK(has_seq(a, {"--setenv", "SDL12COMPAT_OPENGL_SCALING", "0"}));
    CHECK(has_seq(a, {"--unsetenv", "WAYLAND_DISPLAY"}));
    CHECK(std::find(a.begin(), a.end(), "--clearenv") == a.end());   // DISPLAY komt van gamescope
    // De meegeleverde rio-tabel (er is geen gebruikerslaag).
    CHECK(has_seq(a, {"--ro-bind", p.patches + "/Game/rio-invoer.ini", "/rg/tabel/rio-invoer.ini"}));
    CHECK(has_seq(a, {"--setenv", "RG_RIO_TABLE", "/rg/tabel/rio-invoer.ini"}));
    // De lader en het spel, met {res} ingevuld, achteraan.
    CHECK(a.size() >= 4 && a[a.size() - 4] == "/rg/bin/rg-elfloader" && a[a.size() - 3] == "/pm/game" && a[a.size() - 2] == "-n" && a.back() == "-f1360x768");
    CHECK(has_seq(a, {"--ro-bind", p.loader, "/rg/bin/rg-elfloader"}));
    CHECK(pl.comm == "rg-elfloader");
    // Niets in de schrijflaag voor apply_layer.
    CHECK(!fs::exists(p.data + "/native/Game/0/upper"));

    // De gebruikerslaag van de tabel gaat voor.
    write(p.user_patches + "/Game/rio-invoer.ini", "gebruiker");
    CHECK(plan(pr, p, PlanOptions{}, pl, err));
    CHECK(has_seq(pl.argv, {"--ro-bind", p.user_patches + "/Game/rio-invoer.ini", "/rg/tabel/rio-invoer.ini"}));

    // Schrijflaag: mappen, seed (een kopie van de map) en de link; een tweede keer blijft alles staan.
    std::vector<std::string> log;
    CHECK(apply_layer(pl, log, err));
    const std::string up = p.data + "/native/Game/0/upper";
    CHECK(fs::is_directory(p.data + "/native/Game/0/work"));
    CHECK(fs::exists(up + "/dongle/Game/dongle.json"));
    CHECK(fs::is_symlink(up + "/g5/dino/pmuser") && fs::read_symlink(up + "/g5/dino/pmuser") == "../../pmuser");
    write(up + "/dongle/Game/dongle.json", "door het spel bijgewerkt");
    log.clear();
    CHECK(apply_layer(pl, log, err));
    CHECK(log.size() == 2 && log[0].find("bestaat al") != std::string::npos);
    { std::ifstream f(up + "/dongle/Game/dongle.json"); std::string s; std::getline(f, s); CHECK(s == "door het spel bijgewerkt"); }
    // Wat de NAS al heeft, wordt niet gezet.
    fs::remove_all(up + "/g5");
    fs::create_directories(p.nas + "/Game/g5/dino/pmuser");
    log.clear();
    CHECK(apply_layer(pl, log, err));
    CHECK(!fs::exists(up + "/g5/dino/pmuser"));
    // Het identiteitsblok noemt het profiel, de lader, onze bibliotheken, de meegegeven bestanden en de tabel, met sha256.
    const auto id = identity_lines(pl);
    auto has_line = [&](const std::string& part) { return std::any_of(id.begin(), id.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; }); };
    CHECK(has_line("gegevens: " + p.json + " sha256 "));
    CHECK(has_line("lader: " + p.loader + " sha256 "));
    CHECK(has_line("bibliotheek: " + p.build + "/lib32/librg_rio_bridge.so sha256 "));
    CHECK(has_line("invoertabel: " + p.user_patches + "/Game/rio-invoer.ini sha256 "));
    CHECK(!has_line("libSDL-1.2"));   // geen eigen bibliotheek

    // Fouten: een ontbrekende bibliotheek, een NAS die er niet is, geen gamescope.
    NativeProfile bad = pr;
    bad.libs.push_back(p.lib32 + "/libontbreekt.so");
    CHECK(!plan(bad, p, PlanOptions{}, pl, err) && err.find("bibliotheken ontbreken") != std::string::npos);
    NativePaths nonas = p;
    nonas.nas = (root / "geen-nas").string();
    CHECK(!plan(pr, nonas, PlanOptions{}, pl, err) && err.find("spelmap") != std::string::npos);
    fs::remove(root / "bin/gamescope");
    CHECK(!plan(pr, p, PlanOptions{}, pl, err) && err.find("gamescope") != std::string::npos);
    // Zonder gamescope (launcher leeg): het X-scherm van de host, met xauth.
    NativeProfile plain = pr;
    plain.launcher.clear();
    write(root / "xauth", "cookie");
    PlanOptions po;
    po.host_display = ":0";
    po.host_xauthority = (root / "xauth").string();
    CHECK(plan(plain, p, po, pl, err));
    CHECK(has_seq(pl.argv, {"--ro-bind", "/tmp/.X11-unix/X0", "/tmp/.X11-unix/X0", "--setenv", "DISPLAY", ":0"}));
    CHECK(has_seq(pl.argv, {"--ro-bind", (root / "xauth").string(), "/tmp/.Xauthority"}));
    CHECK(std::find(pl.argv.begin(), pl.argv.end(), "-f") == pl.argv.end());

    test_lindbergh(root, p);

    // Het profiel per game: patches/<Game>/native.json, de gebruikerslaag vervangt het meegeleverde als geheel.
    {
        NativePaths q = p;
        q.json.clear();
        CHECK(profile_file(q, "Per").empty() && profile_text(q, "Per").empty());
        write(q.patches + "/Per/native.json", R"({"title":"Meegeleverd","enabled":true,"exe":"/pm/game"})");
        CHECK(profile_file(q, "Per") == q.patches + "/Per/native.json");
        std::string t = profile_text(q, "Per");
        CHECK(profile_enabled(t, "Per"));
        NativeProfile pp;
        CHECK(load_profile(t, "Per", q, pp, err) && pp.title == "Meegeleverd" && pp.res == "1920x1080");
        write(q.user_patches + "/Per/native.json", R"({"title":"Eigen","enabled":false,"exe":"/pm/game"})");
        CHECK(profile_file(q, "Per") == q.user_patches + "/Per/native.json");
        t = profile_text(q, "Per");
        CHECK(!profile_enabled(t, "Per"));
        CHECK(load_profile(t, "Per", q, pp, err) && pp.title == "Eigen");
        // Kapot in de gebruikerslaag: geen profiel (niet stil terug naar het meegeleverde).
        write(q.user_patches + "/Per/native.json", "{kapot");
        CHECK(profile_text(q, "Per").empty());
        // Een naam met een pad erin wordt niet gezocht; het bestand met meerdere profielen blijft de terugval.
        CHECK(profile_file(q, "../Per").empty());
        CHECK(profile_file(p, "Game") == p.json && profile_enabled(profile_text(p, "Game"), "Game"));
        // De identiteit noemt het bestand dat gelezen is.
        write(q.user_patches + "/Per/native.json", R"({"enabled":true,"exe":"/pm/game","mounts":[{"src":"@nas/Game","dst":"/pm","mode":"overlay"}]})");
        CHECK(load_profile(profile_text(q, "Per"), "Per", q, pp, err));
        write(root / "bin/gamescope", "#!/bin/sh\nexit 0\n", true);
        CHECK(plan(pp, q, PlanOptions{}, pl, err));
        CHECK(!pl.identity.empty() && pl.identity[0] == "gegevens=" + q.user_patches + "/Per/native.json");
    }

    // De spelmappen van de app: "@nas/<Map>" komt uit de eerste spelmap waarin <Map> staat (ook een niveau dieper); een vaste
    // spelmap (nas, RAVENGUN_NAS_TP) gaat voor; staat de map nergens, dan de eerste (de fout volgt bij de mount).
    {
        NativePaths q = p;
        q.json.clear();
        q.nas.clear();
        const std::string a = (root / "spelmap-a").string(), b = (root / "spelmap-b").string();
        fs::create_directories(a);
        fs::create_directories(b + "/Teknoparrot/Spel X/bin");
        q.nas_roots = {a, b};
        write(q.patches + "/Lib/native.json", R"({"enabled":true,"exe":"/pm/game","mounts":[{"src":"@nas/Spel X/bin","dst":"/pm","mode":"ro"}]})");
        NativeProfile lp;
        CHECK(load_profile(profile_text(q, "Lib"), "Lib", q, lp, err));
        CHECK(lp.mounts.size() == 1 && lp.mounts[0].src == b + "/Teknoparrot/Spel X/bin");
        fs::create_directories(a + "/Spel X/bin");
        CHECK(load_profile(profile_text(q, "Lib"), "Lib", q, lp, err) && lp.mounts[0].src == a + "/Spel X/bin");
        q.nas = b;   // vast
        CHECK(load_profile(profile_text(q, "Lib"), "Lib", q, lp, err) && lp.mounts[0].src == b + "/Spel X/bin");
        q.nas.clear();
        q.nas_roots = {(root / "geen").string()};
        CHECK(load_profile(profile_text(q, "Lib"), "Lib", q, lp, err) && lp.mounts[0].src == (root / "geen").string() + "/Spel X/bin");
        CHECK(!plan(lp, q, PlanOptions{}, pl, err) && err.find("spelmappen") != std::string::npos);
    }

    fs::remove_all(root);
    std::printf("native-launch: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
