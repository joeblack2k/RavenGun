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
    CHECK(!plan(pr, nonas, PlanOptions{}, pl, err) && err.find("NAS-map") != std::string::npos);
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

    fs::remove_all(root);
    std::printf("native-launch: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
