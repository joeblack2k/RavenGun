// Tests van de games-laag (lijst, iconen, starten/stoppen met nep-spawner en nep-/proc) en de Navigation Controller.
// Geen echte processen, geen systemd, geen hardware.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include "ravengun/games/games.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/sysinfo/navcon.hpp"

using namespace ravengun;
using namespace ravengun::games;
namespace fs = std::filesystem;

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

static void write(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary);
    f << text;
}

static void test_names() {
    for (const char* ok : {"Halo", "Jurassic_Park-2", "a", "X9"}) CHECK(valid_profile_name(ok));
    for (const char* bad : {"", "Halo.xml", "../Halo", "a b", "a/b", "a\\b", "Halo;", "$(id)", "`id`", "é", "a\n"}) CHECK(!valid_profile_name(bad));
    CHECK(!valid_profile_name(std::string(65, 'a')) && valid_profile_name(std::string(64, 'a')));
    CHECK(valid_icon_file("Halo.png") && valid_icon_file("a-b_c.jpeg") && valid_icon_file("x.webp"));
    for (const char* bad : {"Halo", ".png", "Halo.PNG", "Halo.svg", "../Halo.png", "a/b.png", "Halo.png.exe", "Halo..png", "Ha lo.png", ""}) CHECK(!valid_icon_file(bad));
}

static void test_list_and_icons(const std::string& d) {
    Config c;
    c.tp_dir = d + "/tp";
    write(d + "/tp/UserProfiles/Halo.xml", "\xEF\xBB\xBF<?xml?><GameProfile><GameNameInternal>HALO &amp; Co</GameNameInternal><IconName>Icons/Halo.png</IconName></GameProfile>");
    write(d + "/tp/UserProfiles/JurassicPark.xml", "<GameProfile><GamePath>/x</GamePath></GameProfile>");
    write(d + "/tp/UserProfiles/Slecht naam.xml", "<GameProfile/>");     // spatie: wordt genegeerd
    write(d + "/tp/UserProfiles/Halo.env", "A=1");                      // geen .xml
    write(d + "/tp/UserProfiles/geen;shell.xml", "<GameProfile/>");     // ; : genegeerd
    write(d + "/tp/UserProfiles/Weg.xml", "<GameProfile><IconName>Icons/Bestaatniet.png</IconName></GameProfile>");
    write(d + "/tp/UserProfiles/Pad.xml", "<GameProfile><IconName>../../etc/passwd</IconName></GameProfile>");
    fs::create_directories(d + "/tp/UserProfiles/Map.xml");              // map, geen bestand
    write(d + "/tp/Icons/Halo.png", "PNGDATA");
    write(d + "/tp/secret.png", "GEHEIM");
    // Notities: [Status] Note in patches/<Game>/game.ini (meegeleverd), de gebruikerslaag gaat voor.
    write(d + "/patches/JurassicPark/game.ini", "[Status]\nNote = Werkt niet.\n");
    write(d + "/patches/Bad Name/game.ini", "[Status]\nNote = x\n");
    write(d + "/patches/Halo/game.ini", "[Game]\nName = Halo\n");                          // geen notitie
    write(d + "/patches/Weg/game.ini", "[Status]\nNote = " + std::string(301, 'x') + "\n");  // te lang: genegeerd
    c.dirs.shipped = d + "/patches";
    c.dirs.user = d + "/user-patches";
    auto games = list_games(c);
    std::vector<std::string> profiles;
    for (const auto& g : games) profiles.push_back(g.profile);
    CHECK(profiles.size() == 4);  // Halo, JurassicPark, Pad, Weg
    bool halo = false, jp = false, weg = false, pad = false;
    for (const auto& g : games) {
        if (g.profile == "Halo") { halo = g.name == "HALO & Co" && g.icon_file == "Halo.png" && g.note.empty(); }
        if (g.profile == "JurassicPark") { jp = g.name == "Jurassic Park" && g.icon_file.empty() && g.note == "Werkt niet."; }
        if (g.profile == "Weg") weg = g.icon_file.empty();  // icoon bestaat niet: geen icoon
        if (g.profile == "Pad") pad = g.icon_file.empty() || g.icon_file == "passwd";
        CHECK(valid_profile_name(g.profile));
    }
    CHECK(halo && jp && weg);
    CHECK(pad);
    CHECK(profile_exists(c, "Halo") && !profile_exists(c, "Slecht naam") && !profile_exists(c, "../UserProfiles/Halo") && !profile_exists(c, "Map"));
    CHECK(!profile_exists(c, "Nope"));

    // Iconen: alleen een kale bestandsnaam in de Icons-map.
    std::string ct, data;
    CHECK(read_icon(c, "Halo.png", ct, data).empty() && ct == "image/png" && data == "PNGDATA");
    CHECK(read_icon(c, "../secret.png", ct, data) == "invalid");
    CHECK(read_icon(c, "..%2fsecret.png", ct, data) == "invalid");
    CHECK(read_icon(c, "/etc/passwd", ct, data) == "invalid");
    CHECK(read_icon(c, "Icons/Halo.png", ct, data) == "invalid");
    CHECK(read_icon(c, "..", ct, data) == "invalid");
    CHECK(read_icon(c, "", ct, data) == "invalid");
    CHECK(read_icon(c, "Halo.xml", ct, data) == "invalid");
    CHECK(read_icon(c, "Nope.png", ct, data) == "not_found");
    // Een symlink in Icons die naar buiten wijst, wordt niet gevolgd.
    std::error_code ec;
    fs::create_symlink(d + "/tp/secret.png", d + "/tp/Icons/Link.png", ec);
    CHECK(!ec);
    CHECK(read_icon(c, "Link.png", ct, data) == "invalid" && data == "PNGDATA");
    // Te groot.
    write(d + "/tp/Icons/Groot.png", std::string(kMaxIconBytes + 1, 'x'));
    CHECK(read_icon(c, "Groot.png", ct, data) == "invalid");
    // Zonder tp_dir of met een ontbrekende map.
    Config none;
    CHECK(list_games(none).empty() && read_icon(none, "Halo.png", ct, data) == "invalid");
    Config gone;
    gone.tp_dir = d + "/bestaat-niet";
    CHECK(list_games(gone).empty() && read_icon(gone, "Halo.png", ct, data) == "not_found");
    CHECK(load_notes(c.dirs).size() == 1 && load_notes(c.dirs)[0].first == "JurassicPark");
    CHECK(load_notes(ravengun::gamesettings::Dirs{}).empty());   // zonder mappen: geen notities
    write(d + "/user-patches/JurassicPark/game.ini", "[Status]\nNote = Eigen notitie.\n");   // de gebruikerslaag wint
    for (const auto& g : list_games(c)) if (g.profile == "JurassicPark") CHECK(g.note == "Eigen notitie.");
}

static void fake_proc(const std::string& root, int pid, const std::string& comm, const std::vector<std::string>& args) {
    write(root + "/" + std::to_string(pid) + "/comm", comm + "\n");
    std::string raw;
    for (const auto& a : args) raw += a + '\0';
    write(root + "/" + std::to_string(pid) + "/cmdline", raw);
}

static void test_processes(const std::string& d) {
    const std::string proc = d + "/proc";
    fs::create_directories(proc);
    CHECK(!game_process_running(proc) && script_state(proc, "/x/play-halo.sh") == 0);
    fake_proc(proc, 100, "bash", {"bash", "/home/u/editor.sh"});
    fake_proc(proc, 101, "vim", {"vim", "/x/play-halo.sh"});  // het script als argument van iets anders telt niet
    CHECK(script_state(proc, "/x/play-halo.sh") == 0);
    fake_proc(proc, 102, "play-halo.sh", {"/bin/bash", "/x/play-halo.sh", "--profile", "Halo"});
    CHECK(script_state(proc, "/x/play-halo.sh") == 1);
    fake_proc(proc, 103, "play-halo.sh", {"/x/play-halo.sh", "--stop"});
    CHECK(script_state(proc, "/x/play-halo.sh") == 2);
    fs::remove_all(proc + "/102");
    fs::remove_all(proc + "/103");
    fake_proc(proc, 104, "BudgieLoader_x6", {"BudgieLoader_x64.exe"});
    CHECK(game_process_running(proc));
    fs::remove_all(proc + "/104");
    fake_proc(proc, 105, "BudgieLoader.ex", {"BudgieLoader.exe"});
    CHECK(game_process_running(proc));
    fs::remove_all(proc + "/105");
    CHECK(!game_process_running(proc));
}

static std::string state_of(GameService& s, std::string* message = nullptr) {
    auto pr = json::parse(s.status_json());
    if (message) { const json::Value* m = pr.value.find("message"); *message = m && m->is_string() ? m->str : ""; }
    return pr.value.find("state")->str;
}

static void wait_idle_worker(GameService& s) {
    for (int i = 0; i < 400; ++i) {
        auto pr = json::parse(s.status_json());
        const std::string st = pr.value.find("state")->str;
        if (st != "starting" && st != "stopping") return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void test_service(const std::string& d) {
    Config c;
    c.tp_dir = d + "/tp2";
    c.play_script = d + "/play.sh";
    c.log_path = d + "/logs/play-last.log";
    c.proc_root = d + "/proc2";
    c.start_grace_ms = 0;
    write(d + "/tp2/UserProfiles/Halo.xml", "<GameProfile/>");
    write(d + "/play.sh", "#!/bin/sh\n");
    fs::permissions(d + "/play.sh", fs::perms::owner_all);
    fs::create_directories(c.proc_root);
    bool move = false;
    c.move_connected = [&move] { return move; };
    std::mutex m;
    std::vector<std::vector<std::string>> calls;
    int rc = 0;
    std::string script_output;  // wat het (nep)startscript in het logbestand zet
    GameService svc(c, [&](const std::vector<std::string>& argv, std::string& err) {
        std::lock_guard<std::mutex> g(m);
        calls.push_back(argv);
        if (!script_output.empty() && argv[0] == "systemd-run" && argv.back() != "--stop") write(c.log_path, script_output);
        if (rc < 0) err = "systemd-run: bestaat niet";
        return rc;
    });
    CHECK(state_of(svc) == "idle");

    // Weigeringen: niets wordt gespawnd.
    CHECK(!svc.start("").ok && !svc.start("Nope").ok && !svc.start("../x").ok && !svc.start("Halo.xml").ok && !svc.start("a b").ok);
    CHECK(svc.start("Nope").code == "invalid_argument" && svc.start("a;b").code == "invalid_argument");
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }

    // Een geslaagde start; zonder script of game daarna en zonder succesregel in het log is de start mislukt.
    script_output = "Zet de Move aan: druk op de PS-knop.\nGeen Move gevonden binnen 2 minuten. Druk op PS en start opnieuw.\n";
    CHECK(svc.start("Halo").ok);
    wait_idle_worker(svc);
    { std::lock_guard<std::mutex> g(m);
      CHECK(calls.size() == 1 && calls[0].back() == "Halo" && calls[0][calls[0].size() - 2] == "--profile");
      bool prop = false;
      for (const auto& a : calls[0]) if (a == "--property=StandardOutput=file:" + c.log_path) prop = true;
      CHECK(prop); }
    std::string msg;
    CHECK(state_of(svc, &msg) == "failed" && msg.find("Geen Move gevonden") == 0);
    CHECK(state_of(svc) == "failed");  // blijft staan tot de volgende start

    // Script draait: wacht op Move, daarna starten; game draait: draait.
    fake_proc(d + "/proc2", 10, "play-halo.sh", {"/bin/bash", d + "/play.sh", "--profile", "Halo"});
    CHECK(svc.start("Halo").code == "unavailable");  // er loopt al een start
    CHECK(state_of(svc) == "waiting_move" || state_of(svc) == "failed");  // (failed zolang de oude start nog sticky is)
    CHECK(svc.stop().code == "" );
    wait_idle_worker(svc);
    fs::remove_all(d + "/proc2/10");
    CHECK(state_of(svc) == "idle");
    { std::lock_guard<std::mutex> g(m);
      CHECK(calls.size() == 3);  // start, systemctl stop, systemd-run --stop
      CHECK(calls[1][0] == "systemctl" && calls[1].back() == "ravengun-play.service");
      CHECK(calls[2].back() == "--stop" && calls[2][calls[2].size() - 2] == d + "/play.sh"); }

    // Tweede start: script draait zonder Move -> wacht op Move; met Move -> start; game -> draait.
    script_output = "Halo start. Move verbonden, lightgun klaar.\n";
    CHECK(svc.start("Halo").ok);
    wait_idle_worker(svc);
    fake_proc(d + "/proc2", 11, "play-halo.sh", {d + "/play.sh", "--profile", "Halo"});
    move = false;
    CHECK(state_of(svc) == "waiting_move");
    move = true;
    CHECK(state_of(svc) == "starting");
    fs::remove_all(d + "/proc2/11");
    fake_proc(d + "/proc2", 12, "BudgieLoader_x6", {"BudgieLoader_x64.exe"});
    CHECK(state_of(svc) == "running");
    CHECK(svc.start("Halo").code == "unavailable");  // er draait al een game
    fs::remove_all(d + "/proc2/12");
    CHECK(state_of(svc) == "idle");  // game ten einde, geen foutmelding
    CHECK(state_of(svc) == "idle");

    // Systemd-run faalt (code of niet startbaar).
    rc = 1;
    CHECK(svc.start("Halo").ok);
    wait_idle_worker(svc);
    CHECK(state_of(svc, &msg) == "failed" && msg.find("code 1") != std::string::npos);
    rc = -1;
    CHECK(svc.start("Halo").ok);
    wait_idle_worker(svc);
    CHECK(state_of(svc, &msg) == "failed" && msg.find("bestaat niet") != std::string::npos);

    // Stoppen zonder game is geen fout en spawnt niets.
    rc = 0;
    fs::remove_all(d + "/proc2");
    fs::create_directories(d + "/proc2");
    script_output = "Halo start. Move verbonden, lightgun klaar.\n";
    CHECK(svc.start("Halo").ok);   // reset de mislukking van hierboven
    wait_idle_worker(svc);
    CHECK(state_of(svc) == "idle");
    size_t before;
    { std::lock_guard<std::mutex> g(m); before = calls.size(); }
    CHECK(svc.stop().ok);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.size() == before); }

    // Zonder uitvoerbaar script wordt niet gestart.
    Config c2 = c;
    c2.play_script = d + "/bestaat-niet.sh";
    GameService svc2(c2, [&](const std::vector<std::string>&, std::string&) { return 0; });
    CHECK(svc2.start("Halo").code == "unavailable");
    // Startwijze (A10): direct -> --direct, teknoparrot-ui -> --tpui, leeg/auto -> niets extra (het script kiest per profiel).
    {
        auto a = start_argv(c, "Halo", "direct");
        CHECK(a.size() >= 3 && a[a.size() - 2] == "Halo" && a.back() == "--direct");
        a = start_argv(c, "Halo", "teknoparrot-ui");
        CHECK(a.back() == "--tpui");
        CHECK(start_argv(c, "Halo").back() == "Halo" && start_argv(c, "Halo", "").back() == "Halo");
        CHECK(valid_launch("direct") && valid_launch("teknoparrot-ui") && valid_launch("auto") && valid_launch(""));
        CHECK(!valid_launch("Direct") && !valid_launch("--direct") && !valid_launch("x"));
        GameService svc3(c, [&](const std::vector<std::string>&, std::string&) { return 0; });
        CHECK(svc3.start("Halo", "--tpui;rm").code == "invalid_argument");
    }
    // De opdrachtregels bevatten nooit een shell.
    for (const auto& a : {start_argv(c, "Halo"), start_argv(c, "Halo", "direct"), stop_unit_argv(c), stop_script_argv(c)})
        for (const auto& x : a) CHECK(x != "sh" && x != "bash" && x != "-c" && x != "/bin/sh");
}

static void test_unit_cgroup(const std::string& d) {
    const std::string f = d + "/cg/ravengun-tpui.service/cgroup.procs";
    CHECK(!unit_alive(f) && !unit_alive(""));        // bestaat niet / geen pad
    write(f, "");
    CHECK(!unit_alive(f));                            // leeg
    write(f, "1234\n5678\n");
    CHECK(unit_alive(f));
    CHECK(default_tpui_procs(1000) == "/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app.slice/ravengun-tpui.service/cgroup.procs");

    // Een game die buiten Budgie om draait (ander loader-proces) telt via de cgroup van de unit.
    Config c;
    c.tp_dir = d + "/tp3";
    c.play_script = d + "/play.sh";
    c.proc_root = d + "/proc3";
    c.tpui_procs = f;
    c.log_path = d + "/logs/p3.log";
    fs::create_directories(c.proc_root);
    write(d + "/tp3/UserProfiles/Halo.xml", "<GameProfile/>");
    GameService svc(c, [&](const std::vector<std::string>&, std::string&) { return 0; });
    auto st = json::parse(svc.status_json());
    CHECK(st.value.find("state")->str == "running" && st.value.find("stoppable")->b && st.value.find("profile")->is_null());
    CHECK(svc.start("Halo").code == "unavailable");   // er draait al iets
    write(f, "");
    CHECK(json::parse(svc.status_json()).value.find("state")->str == "idle");
}

static void test_navcon(const std::string& d) {
    using namespace ravengun::sysinfo;
    const std::string root = d + "/psy";
    fs::create_directories(root);
    CHECK(!read_navcon(root).connected);  // niets
    write(root + "/BAT0/capacity", "80\n");
    write(root + "/BAT0/status", "Discharging\n");
    write(root + "/BAT0/device/uevent", "DRIVER=nvme\n");
    CHECK(!read_navcon(root).connected);  // een gewone accu is geen Navigation Controller
    write(root + "/sony1/capacity", "55\n");
    write(root + "/sony1/status", "Discharging\n");
    write(root + "/sony1/device/uevent", "HID_NAME=Navigation Controller\nHID_ID=0005:0000054C:0000042F\n");
    NavconStatus n = read_navcon(root);
    CHECK(n.connected && n.percent == 55 && n.code == "ge40" && n.status == "discharging");
    write(root + "/sony1/status", "Charging\n");
    n = read_navcon(root);
    CHECK(n.connected && n.code == "charging" && n.text == "laadt");
    write(root + "/sony1/status", "Full\n");
    CHECK(read_navcon(root).code == "charged");
    CHECK(classify_battery(5, "Discharging").code == "min" && classify_battery(25, "Discharging").code == "ge20");
    CHECK(classify_battery(65, "Discharging").code == "ge60" && classify_battery(90, "Discharging").code == "ge80");
    CHECK(classify_battery(100, "Discharging").code == "max" && classify_battery(-1, "Discharging").code == "unknown");
    auto pr = json::parse(navcon_json(read_navcon(root)));
    CHECK(pr.ok && pr.value.find("connected")->b && pr.value.find("battery")->find("code")->str == "charged");
    pr = json::parse(navcon_json(NavconStatus{}));
    CHECK(pr.ok && !pr.value.find("connected")->b && pr.value.find("battery")->find("text")->str == "niet verbonden");
    // Herkenning op product-id zonder naam.
    fs::remove_all(root + "/sony1");
    write(root + "/x/capacity", "10");
    write(root + "/x/device/uevent", "HID_ID=0003:0000054C:0000042F\n");
    CHECK(read_navcon(root).connected && read_navcon(root).code == "min");
    CHECK(!read_navcon(d + "/bestaat-niet").connected);
}

int main() {
    std::string d = (fs::temp_directory_path() / ("rg-games-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(d);
    fs::create_directories(d);
    test_names();
    test_list_and_icons(d);
    test_processes(d);
    test_service(d);
    test_unit_cgroup(d);
    test_navcon(d);
    fs::remove_all(d);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
