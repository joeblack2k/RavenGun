// Tests van de games-laag (lijst, iconen, starten/stoppen met nep-spawner en nep-/proc) en de Navigation Controller.
// Geen echte processen, geen systemd, geen hardware.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include "ravengun/games/games.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/launch/gamefind.hpp"
#include "ravengun/launch/native.hpp"
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
    write(d + "/patches/Weg/game.ini", "[Status]\nNote = " + std::string(301, 'x') + "\n");  // te lang: afgekapt, niet weggelaten
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
        if (g.profile == "Weg") weg = g.icon_file.empty() && g.note == std::string(299, 'x') + "\xE2\x80\xA6";  // icoon bestaat niet: geen icoon
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
    CHECK(load_notes(c.dirs).size() == 2 && load_notes(c.dirs)[0].first == "JurassicPark" && load_notes(c.dirs)[1].first == "Weg");
    CHECK(load_notes(ravengun::gamesettings::Dirs{}).empty());   // zonder mappen: geen notities
    write(d + "/user-patches/JurassicPark/game.ini", "[Status]\nNote = Eigen notitie.\n");   // de gebruikerslaag wint
    for (const auto& g : list_games(c)) if (g.profile == "JurassicPark") CHECK(g.note == "Eigen notitie.");
}

static size_t utf8_chars(const std::string& s) {
    size_t n = 0;
    for (unsigned char ch : s) n += (ch & 0xC0) != 0x80;
    return n;
}

// Een te lange notitie wordt afgekapt en blijft zichtbaar; vroeger viel hij helemaal weg (13 van de 36 games, o.a. Jurassic Park met 767 tekens).
static void test_long_note(const std::string& d) {
    const std::string dots = "\xE2\x80\xA6";
    CHECK(clip_note("") == "" && clip_note("Werkt niet.") == "Werkt niet.");
    CHECK(clip_note(std::string(300, 'x')) == std::string(300, 'x'));                       // precies de grens: ongemoeid
    std::string accents;                                                                     // 300 tekens, 600 bytes: tekens tellen, geen bytes
    for (int i = 0; i < 300; ++i) accents += "\xC3\xA9";
    CHECK(clip_note(accents) == accents);
    const std::string cut_accents = clip_note(accents + "\xC3\xA9");                        // 301 tekens zonder spatie: hard afgekapt op een tekengrens
    CHECK(cut_accents == accents.substr(0, 2 * 299) + dots && utf8_chars(cut_accents) == 300);

    // 767 tekens zoals een echte notitie: woorden, leestekens en hier en daar een teken van twee bytes.
    std::string note;
    while (utf8_chars(note) < 767) note += "De daemon st\xC3\xA9\xC3\xA9" "ds opnieuw gestart, be\xC3\xAF" "nvloedt het beeld niet. ";
    while (utf8_chars(note) > 767) note.pop_back();
    while ((static_cast<unsigned char>(note.back()) & 0xC0) == 0x80 || note.back() == ' ') note.pop_back();
    while (utf8_chars(note) < 767) note += "x";
    CHECK(utf8_chars(note) == 767);
    const std::string cut = clip_note(note);
    CHECK(utf8_chars(cut) <= kMaxNoteChars && utf8_chars(cut) > kMaxNoteChars - 30);
    CHECK(cut.size() > dots.size() && cut.compare(cut.size() - dots.size(), dots.size(), dots) == 0);
    const std::string kept = cut.substr(0, cut.size() - dots.size());
    CHECK(note.compare(0, kept.size(), kept) == 0);                                          // een echt begin van de notitie
    CHECK(note[kept.size()] == ' ' && kept.back() != ' ');                                   // op een woordgrens, zonder losse spatie
    CHECK(clip_note(cut) == cut);                                                            // nog eens afkappen verandert niets

    // Door de keten: game.ini -> load_notes -> list_games -> de JSON van de gamelijst.
    const std::string sub = d + "/lange-notitie";
    Config c;
    c.tp_dir = sub + "/tp";
    write(sub + "/tp/UserProfiles/JurassicPark.xml", "<GameProfile></GameProfile>");
    write(sub + "/patches/JurassicPark/game.ini", "[Status]\nNote = " + note + "\n");
    c.dirs.shipped = sub + "/patches";
    const auto notes = load_notes(c.dirs);
    CHECK(notes.size() == 1 && notes[0].first == "JurassicPark" && notes[0].second == cut);
    const auto games = list_games(c);
    CHECK(games.size() == 1 && games[0].note == cut);
    GameService svc(c, [](const std::vector<std::string>&, std::string&) { return 0; });
    const auto parsed = json::parse(svc.list_json());
    CHECK(parsed.ok && parsed.value.is_object());
    const json::Value* list = parsed.ok ? parsed.value.find("games") : nullptr;
    CHECK(list && list->is_array() && list->arr.size() == 1);
    if (list && list->is_array() && list->arr.size() == 1) {
        const json::Value* n = list->arr[0].find("note");
        CHECK(n && n->is_string() && n->str == cut);                                         // niet null, en de "…" komt heel door de JSON
    }
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
    CHECK(!game_process_running(proc));
    fake_proc(proc, 100, "bash", {"bash", "/home/u/editor.sh"});
    CHECK(!game_process_running(proc));
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
    c.log_path = d + "/logs/play-last.log";
    c.proc_root = d + "/proc2";
    write(d + "/tp2/UserProfiles/Halo.xml", "<GameProfile/>");
    fs::create_directories(c.proc_root);
    std::mutex m;
    std::vector<std::vector<std::string>> calls;
    GameService svc(c, [&](const std::vector<std::string>& argv, std::string&) {
        std::lock_guard<std::mutex> g(m);
        calls.push_back(argv);
        return 0;
    });
    CHECK(state_of(svc) == "idle");

    // Weigeringen: niets wordt gespawnd.
    CHECK(!svc.start("").ok && !svc.start("Nope").ok && !svc.start("../x").ok && !svc.start("Halo.xml").ok && !svc.start("a b").ok);
    CHECK(svc.start("Nope").code == "invalid_argument" && svc.start("a;b").code == "invalid_argument");
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }

    // Zonder eigen route (geen native, geen Unity, geen directe route) start niets: geen script, geen TeknoParrotUi. De start eindigt
    // in "failed" met de reden, ook in het startlogboek.
    std::string msg;
    CHECK(svc.start("Halo").ok);
    wait_idle_worker(svc);
    CHECK(state_of(svc, &msg) == "failed" && msg.find("nog geen eigen route") != std::string::npos);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }
    {
        std::ifstream lf(c.log_path);
        const std::string inhoud((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        CHECK(inhoud.find("route: Halo: nog geen eigen route") != std::string::npos);
    }

    // Startwijze: auto en direct; TeknoParrotUi bestaat niet meer.
    CHECK(valid_launch("direct") && valid_launch("auto") && valid_launch(""));
    CHECK(!valid_launch("teknoparrot-ui") && !valid_launch("Direct") && !valid_launch("--direct") && !valid_launch("x"));
    {
        const Result r = svc.start("Halo", "teknoparrot-ui");
        CHECK(!r.ok && r.code == "invalid_argument" && r.message.find("TeknoParrotUi") != std::string::npos);
        CHECK(svc.start("Halo", "--tpui;rm").code == "invalid_argument");
    }

    // Stoppen zonder game is geen fout en spawnt niets.
    CHECK(svc.stop().ok);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }

    // Een game die al draait (bv. gestart voor een herstart van de daemon): starten weigert, stoppen stopt de unit van de game.
    fake_proc(d + "/proc2", 12, "BudgieLoader_x6", {"BudgieLoader_x64.exe"});
    CHECK(state_of(svc) == "failed" || state_of(svc) == "running");
    CHECK(svc.start("Halo").code == "unavailable");
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 1 && calls[0][0] == "systemctl" && calls[0].back() == c.tpui_unit + ".service");
    }
    fs::remove_all(d + "/proc2/12");

    // De opdrachtregel van het stoppen bevat nooit een shell.
    for (const auto& x : stop_unit_argv(c)) CHECK(x != "sh" && x != "bash" && x != "-c" && x != "/bin/sh");
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

// Games met een eigen profiel in game.ini (docs/per-game-patches.md): geldig zonder UserProfiles-XML en in de lijst als ze in een gamemap of
// een view staan.
static void test_own_profile(const std::string& d) {
    Config c;
    c.tp_dir = d + "/own-tp";
    c.dirs.shipped = d + "/own-patches";
    c.dirs.user = d + "/own-user";
    c.home = d + "/own-home";
    c.library_file = d + "/own-library.json";
    c.views_dir = d + "/own-views";
    fs::create_directories(c.tp_dir + "/UserProfiles");
    write(c.dirs.shipped + "/Eigen/game.ini", "[Game]\nName = Eigen Spel\nExecutable = game.exe\nProfileRevision = 3\n[Identify]\nPath = bin/game.exe\nSize = 3\n[Status]\nNote = Speelbaar.\n");
    write(c.dirs.shipped + "/ZonderProfiel/game.ini", "[Game]\nName = Alleen status\n");
    write(c.dirs.shipped + "/Elders/game.ini", "[Game]\nName = Niet geinstalleerd\nExecutable = x.exe\n[Identify]\nPath = x.exe\nSize = 1\n");
    CHECK(profile_exists(c, "Eigen") && profile_exists(c, "Elders"));        // de XML is niet nodig
    CHECK(!profile_exists(c, "ZonderProfiel") && !profile_exists(c, "Nope"));
    // Nog geen gamemap: de lijst bevat de games met een eigen profiel niet.
    CHECK(list_games(c).empty());
    write(c.library_file, "{\"schema\":1,\"folders\":[{\"path\":\"" + d + "/own-games\",\"recursive\":true}]}");
    CHECK(list_games(c).empty());                                           // de gamemap is leeg
    write(d + "/own-games/Map van Eigen/bin/game.exe", "abc");
    auto games = list_games(c);
    CHECK(games.size() == 1 && games[0].profile == "Eigen" && games[0].name == "Eigen Spel" && games[0].note == "Speelbaar.");
    CHECK(library_folders(c.library_file, c.home).size() == 1);
    CHECK(library_folders(d + "/bestaat-niet.json", "").empty());
    // Een lokale view telt ook, en een profiel dat ook als XML bestaat komt maar een keer in de lijst.
    write(c.views_dir + "/Elders/x.exe", "x");
    CHECK(list_games(c).size() == 2);
    write(c.tp_dir + "/UserProfiles/Eigen.xml", "<GameProfile><GameNameInternal>Uit de XML</GameNameInternal></GameProfile>");
    games = list_games(c);
    CHECK(games.size() == 2);
    // De lijst van de daemon (list_json) zoekt niet zelf in de gamemappen: een achtergrondthread vult de cache, het verzoek wacht niet.
    {
        GameService svc(c, [](const std::vector<std::string>&, std::string&) { return 0; });
        auto count = [&] {
            const auto pr = json::parse(svc.list_json());
            const json::Value* gs = pr.value.find("games");
            return gs && gs->is_array() ? gs->arr.size() : size_t(0);
        };
        CHECK(count() >= 1);   // de XML-profielen staan er meteen in; de eigen profielen zodra de thread klaar is
        svc.wait_own_games();
        CHECK(count() == 2);
        fs::remove_all(c.views_dir + "/Elders");
        CHECK(count() == 2);   // uit de cache (kOwnGamesTtlMs), geen nieuwe zoektocht
        CHECK(own_profile_games(c).empty());   // Eigen staat als XML in de lijst, Elders is weg
    }
}

// De native route zonder script: de daemon wacht op een lightgun, wapent, start de route, zet de schrijflaag, schrijft het logboek en
// start de game met systemd-run in de unit ravengun-tpui (geen play-halo.sh, geen play-native.sh); stoppen ruimt zelf op.
static void test_native_service(const std::string& d) {
    const std::string b = d + "/nat";
    for (const char* prog : {"bwrap", "env", "gamescope", "rg-exec"}) {
        write(b + "/bin/" + prog, "#!/bin/sh\n");
        fs::permissions(b + "/bin/" + prog, fs::perms::owner_all);
    }
    const std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    setenv("PATH", (b + "/bin").c_str(), 1);
    fs::create_directories(b + "/nas/Spel");
    write(b + "/tp/UserProfiles/Nat.xml", "<GameProfile/>");
    write(b + "/native-games.json", R"({"games":{"Nat":{"title":"Natief spel","enabled":true,"launcher":"gamescope",
        "mounts":[{"src":"@nas/Spel","dst":"/pm","mode":"overlay"}],"links":[{"target":"../../pmuser","dst":"/pm/a/b/pmuser"}],
        "exe":"/pm/game","cwd":"/pm","args":["-f{res}"],"res":"1360x768","route":"tp-shm"},
      "Script":{"enabled":false,"exe":"/x"}}})");
    Config c;
    c.tp_dir = b + "/tp";
    c.proc_root = b + "/proc";
    c.native.json = b + "/native-games.json";
    c.native.nas = b + "/nas";
    c.native.data = b + "/data";
    c.native.runtime_dir = b + "/run";
    c.native.home = b + "/home";
    c.native.exec_helper = b + "/bin/rg-exec";
    c.native_log_dir = b + "/logs";
    c.lightgun_wait_ms = 2000;
    std::atomic<bool> gun{false};
    std::mutex m;
    std::vector<std::string> events;
    std::vector<std::vector<std::string>> calls;
    c.lightgun_ready = [&] { return gun.load(); };
    c.arm = [&] { std::lock_guard<std::mutex> g(m); events.push_back("arm"); return std::string(); };
    c.start_route = [&](const std::string& game, const std::string& route) { std::lock_guard<std::mutex> g(m); events.push_back("route " + game + " " + route); return std::string(); };
    c.stop_inputs = [&] { std::lock_guard<std::mutex> g(m); events.push_back("stop_inputs"); };
    CHECK(native_route(c, "Nat"));
    CHECK(!native_route(c, "Script"));
    GameService svc(c, [&](const std::vector<std::string>& argv, std::string&) {
        std::lock_guard<std::mutex> g(m);
        calls.push_back(argv);
        events.push_back("spawn " + argv[0]);
        return 0;
    });

    // Zonder lightgun: wachten (de UI toont "wacht op de Move"), met een lightgun: verder.
    CHECK(svc.start("Nat").ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(state_of(svc) == "waiting_move");
    { std::lock_guard<std::mutex> g(m); CHECK(events.empty() && calls.empty()); }
    gun.store(true);
    for (int i = 0; i < 400 && (state_of(svc) == "waiting_move" || state_of(svc) == "starting"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string msg;
    CHECK(state_of(svc, &msg) != "failed");
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(events.size() == 3 && events[0] == "arm" && events[1] == "route Nat tp-shm" && events[2] == "spawn systemd-run");
        CHECK(calls.size() == 1);
        const auto& a = calls[0];
        auto has = [&](const std::string& x) { return std::find(a.begin(), a.end(), x) != a.end(); };
        CHECK(has("--unit=ravengun-tpui") && has("--user") && has("--") && has(b + "/bin/rg-exec") && has("DISPLAY"));
        CHECK(has(b + "/bin/env") && has("-i") && has(b + "/bin/gamescope") && has(b + "/bin/bwrap") && a.back() == "-f1360x768");
        bool script = false, out = false;
        for (const auto& x : a) {
            if (x.find("play-halo") != std::string::npos || x.find("play-native") != std::string::npos || x == "sh" || x == "/bin/sh") script = true;
            if (x.rfind("--property=StandardOutput=append:" + b + "/logs/native-Nat-", 0) == 0) out = true;
        }
        CHECK(!script);   // geen script en geen shell in de keten
        CHECK(out);
    }
    // De schrijflaag en het logboek.
    CHECK(fs::is_symlink(b + "/data/native/Nat/0/upper/a/b/pmuser"));
    bool logged = false;
    for (const auto& e : fs::directory_iterator(b + "/logs")) {
        std::ifstream f(e.path());
        std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        logged = all.find("== native Nat") == 0 && all.find("gegevens: " + b + "/native-games.json sha256 ") != std::string::npos &&
                 all.find("link: /pm/a/b/pmuser -> ../../pmuser gezet") != std::string::npos && all.find("cmd: ") != std::string::npos;
    }
    CHECK(logged);

    // Stoppen: de unit van de game weg, de invoer neutraal en ontwapend; geen script.
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 2 && calls[1].size() >= 4 && calls[1][calls[1].size() - 1] == "ravengun-tpui.service" && calls[1][calls[1].size() - 2] == "stop");
        CHECK(events.back() == "stop_inputs");
    }

    // Stoppen terwijl de start op een lightgun wacht: afbreken, niets gespawnd.
    gun.store(false);
    { std::lock_guard<std::mutex> g(m); calls.clear(); events.clear(); }
    CHECK(svc.start("Nat").ok);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(svc.stop().ok);
    for (int i = 0; i < 400 && state_of(svc) == "waiting_move"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(state_of(svc, &msg) == "failed" && msg.find("afgebroken") != std::string::npos);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }

    // Een game die de camera in het spel gebruikt, heeft (nog) geen native route.
    c.dirs.shipped = b + "/patches";
    write(b + "/patches/Nat/game.ini", "[Game]\nName = Nat\n[Camera]\nUse = game\n");
    CHECK(!native_route(c, "Nat"));
    setenv("PATH", old_path.c_str(), 1);
}

// De Unity-route zonder script: de daemon zet de view klaar (symlinks naar de NAS, BepInEx en de plugin lokaal), wapent, start de
// TCP-klant (route unity-tcp) en start `proton run Game.exe -popupwindow` met systemd-run in de unit ravengun-tpui, met de view als
// werkmap; geen play-halo.sh, geen play-unity.sh, geen aagames-setup.py.
static void test_unity_service(const std::string& d) {
    const std::string b = d + "/uni";
    write(b + "/home/nas/Spel/Game.exe", "MZ");
    write(b + "/home/nas/Spel/Game_Data/level0", "x");
    write(b + "/data/bepinex/5.4.21.0/x64/BepInEx/core/BepInEx.dll", "core");
    write(b + "/data/bepinex/5.4.21.0/x64/winhttp.dll", "doorstop");
    write(b + "/data/bepinex/5.4.21.0/x64/doorstop_config.ini", "");
    write(b + "/plug/Uni/RavenGun.Uni.dll", "plugin");
    write(b + "/proton/proton", "#!/bin/sh\n");
    fs::permissions(b + "/proton/proton", fs::perms::owner_all);
    write(b + "/tp/UserProfiles/Uni.xml", "<GameProfile/>");
    write(b + "/aagames.json", R"({"port":33610,"games":{"Uni":{"name":"Unity-spel","nas_dir":"~/nas/Spel","exe":"Game.exe"}}})");
    Config c;
    c.tp_dir = b + "/tp";
    c.proc_root = b + "/proc";
    c.unity.json = b + "/aagames.json";
    c.unity.home = b + "/home";
    c.unity.data = b + "/data";
    c.unity.plugin_dirs = {"", b + "/plug"};
    c.unity.proton = b + "/proton";
    c.native.runtime_dir = b + "/run";
    c.native_log_dir = b + "/logs";
    std::mutex m;
    std::vector<std::string> events;
    std::vector<std::vector<std::string>> calls;
    c.lightgun_ready = [] { return true; };
    c.arm = [&] { std::lock_guard<std::mutex> g(m); events.push_back("arm"); return std::string(); };
    c.start_route = [&](const std::string& game, const std::string& route) { std::lock_guard<std::mutex> g(m); events.push_back("route " + game + " " + route); return std::string(); };
    c.stop_inputs = [&] { std::lock_guard<std::mutex> g(m); events.push_back("stop_inputs"); };
    CHECK(unity_route(c, "Uni"));
    CHECK(unity_route(c, "Uni", "direct"));
    CHECK(!unity_route(c, "Ander"));
    GameService svc(c, [&](const std::vector<std::string>& argv, std::string&) {
        std::lock_guard<std::mutex> g(m);
        calls.push_back(argv);
        events.push_back("spawn " + argv[0]);
        return 0;
    });
    CHECK(svc.start("Uni").ok);
    wait_idle_worker(svc);
    std::string msg;
    CHECK(state_of(svc, &msg) != "failed");
    if (!msg.empty()) std::fprintf(stderr, "unity: %s\n", msg.c_str());
    const std::string view = b + "/data/runtime/views-aagames/Uni";
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(events.size() == 3 && events[0] == "arm" && events[1] == "route Uni unity-tcp" && events[2] == "spawn systemd-run");
        CHECK(calls.size() == 1);
        if (!calls.empty()) {
            const auto& a = calls[0];
            auto has = [&](const std::string& x) { return std::find(a.begin(), a.end(), x) != a.end(); };
            CHECK(has("--unit=ravengun-tpui") && has("--user") && has("--working-directory=" + view));
            CHECK(has("--setenv=WINEDLLOVERRIDES=winhttp=n,b") && has("--setenv=STEAM_COMPAT_DATA_PATH=" + b + "/data/prefixes/unity-aagames"));
            CHECK(has("--setenv=XDG_RUNTIME_DIR=" + b + "/run"));
            const size_t dd = static_cast<size_t>(std::find(a.begin(), a.end(), "--") - a.begin());
            CHECK(dd + 4 < a.size() + 1 && a.size() == dd + 5 && a[dd + 1] == b + "/proton/proton" && a[dd + 2] == "run" &&
                  a[dd + 3] == "Game.exe" && a[dd + 4] == "-popupwindow");
            bool script = false, out = false;
            for (const auto& x : a) {
                if (x.find("play-halo") != std::string::npos || x.find("play-unity") != std::string::npos || x.find("aagames-setup") != std::string::npos ||
                    x == "sh" || x == "/bin/sh") script = true;
                if (x.rfind("--property=StandardOutput=append:" + b + "/logs/unity-Uni-", 0) == 0) out = true;
            }
            CHECK(!script);
            CHECK(out);
        }
    }
    // De view: de game als symlink naar de NAS, onze plugin lokaal.
    CHECK(fs::is_symlink(view + "/Game.exe"));
    CHECK(fs::is_regular_file(view + "/BepInEx/plugins/RavenGun.Uni.dll") && !fs::is_symlink(view + "/BepInEx/plugins/RavenGun.Uni.dll"));
    // Stoppen: de unit weg, de invoer (ook de TCP-klant) uit; geen script.
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 2 && calls[1].back() == "ravengun-tpui.service");
        CHECK(events.back() == "stop_inputs");
    }
    // Zonder plugin: een duidelijke melding, niets gestart, de invoer weer uit.
    fs::remove(b + "/plug/Uni/RavenGun.Uni.dll");
    { std::lock_guard<std::mutex> g(m); calls.clear(); events.clear(); }
    CHECK(svc.start("Uni").ok);
    wait_idle_worker(svc);
    CHECK(state_of(svc, &msg) == "failed" && msg.find("build-unity-plugin.sh Uni") != std::string::npos);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty()); }
    // game.ini vraagt om tp-shm: dan niet de Unity-route.
    c.dirs.shipped = b + "/patches";
    write(b + "/patches/Uni/game.ini", "[Game]\nName = Uni\n[Input]\nRoute = tp-shm\n");
    CHECK(!unity_route(c, "Uni"));
}

// De directe route ([Launch] Mode = direct) zonder script: wachten op een lightgun, wapenen, de route (tp-shm, jvs over de pipe met de
// pty-koppeling in RG_JVS_PTY), de drop-ins en teknoparrot.ini in de view, het plan voor rg-direct, en systemd-run met rg-direct als
// hoofdproces van de unit. Een route die de engine niet doet start niet; wat de directe route weigert ook niet.
static void test_direct_service(const std::string& d) {
    const std::string b = d + "/dir", home = b + "/home", tp = home + "/tp";
    for (const char* rel : {"/OpenParrotWin32/OpenParrotLoader.exe", "/TeknoParrot/TeknoParrot.dll", "/pipehelper.exe"}) write(tp + rel, "x");
    write(tp + "/GameProfiles/Dir.xml", "<GameProfile><GameProfileRevision>1</GameProfileRevision></GameProfile>");
    write(home + "/.local/share/ravengun/runtime/views/Dir/spel.exe", "MZ");
    write(b + "/patches/Dir/game.ini",
          "[Game]\nType = TeknoParrot\nExecutable = spel.exe\nProfileRevision = 1\n[Launch]\nMode = direct\n[Input]\nRoute = tp-shm\nLayout = L\n"
          "[Identify]\nPath = spel.exe\n[Config]\nGeneral/Windowed = 1\n[ConfigType]\nGeneral/Windowed = Bool\n");
    write(b + "/patches/Dir/dropin/dropin.ini", "[hook.dll]\nTo = .\n");
    write(b + "/patches/Dir/dropin/hook.dll", "MZhook");
    write(b + "/patches/Evd/game.ini", "[Game]\nType = TeknoParrot\nExecutable = e.exe\n[Launch]\nMode = direct\n");
    write(b + "/patches/Weg/game.ini",
          "[Game]\nType = TeknoParrot\nExecutable = spel.exe\nProfileRevision = 1\nDongleRegion = MARS\n[Launch]\nMode = direct\n[Input]\nRoute = tp-shm\nLayout = L\n"
          "[Game]\nGamePath = " + home + "/.local/share/ravengun/runtime/views/Dir/spel.exe\n");
    write(b + "/bin/rg-direct", "#!/bin/sh\n");
    fs::permissions(b + "/bin/rg-direct", fs::perms::owner_all);
    Config c;
    c.tp_dir = tp;
    c.proc_root = b + "/proc";
    c.dirs = {b + "/patches", b + "/user"};
    c.native_log_dir = b + "/logs";
    c.direct.tp = tp;
    c.direct.home = home;
    c.direct.data = home + "/.local/share/ravengun";
    c.direct.state = home + "/.local/state/ravengun";
    c.direct.dirs = c.dirs;
    c.direct.mounts = b + "/geen-mounts";
    c.direct_runner = b + "/bin/rg-direct";
    c.jvs_link = b + "/run/jvs-com";
    c.session_env = [&] { return ravengun::launch::EnvMap{{"HOME", home}, {"PATH", "/usr/bin:/bin"}, {"DISPLAY", ":0"}}; };
    c.lightgun_wait_ms = 2000;
    std::atomic<bool> gun{true};
    std::mutex m;
    std::vector<std::string> events;
    std::vector<std::vector<std::string>> calls;
    c.lightgun_ready = [&] { return gun.load(); };
    c.arm = [&] { std::lock_guard<std::mutex> g(m); events.push_back("arm"); return std::string(); };
    c.start_route = [&](const std::string& game, const std::string& route) { std::lock_guard<std::mutex> g(m); events.push_back("route " + game + " " + route); return std::string(); };
    c.stop_inputs = [&] { std::lock_guard<std::mutex> g(m); events.push_back("stop_inputs"); };

    std::string why;
    CHECK(direct_route(c, "Dir", "", why) == ravengun::launch::Verdict::Ok);
    CHECK(direct_route(c, "Evd", "", why) == ravengun::launch::Verdict::Fallback && why.find("evdev") != std::string::npos);
    CHECK(direct_route(c, "Weg", "", why) == ravengun::launch::Verdict::Refused && why.find("DongleRegion") != std::string::npos);
    Config norunner = c;
    norunner.direct_runner = b + "/bin/bestaat-niet";
    CHECK(direct_route(norunner, "Dir", "", why) == ravengun::launch::Verdict::Fallback);
    Config unity = c;
    unity.unity_game = [](const std::string&) { return true; };
    CHECK(direct_route(unity, "Dir", "", why) == ravengun::launch::Verdict::Ok);   // tp-shm gaat niet via de Unity-route

    GameService svc(c, [&](const std::vector<std::string>& argv, std::string&) {
        std::lock_guard<std::mutex> g(m);
        calls.push_back(argv);
        events.push_back("spawn " + argv[0]);
        return 0;
    });
    // Weigeren: niets gestart, een melding (in de toestand: start() antwoordt meteen, het plan weigert in de worker).
    const Result r = svc.start("Weg");
    CHECK(r.ok);
    wait_idle_worker(svc);
    {
        std::string wmsg;
        CHECK(state_of(svc, &wmsg) == "failed" && wmsg.find("Instelling geweigerd") != std::string::npos);
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.empty() && events.empty());
    }
    CHECK(svc.start("Dir").ok);
    for (int i = 0; i < 400 && (state_of(svc) == "waiting_move" || state_of(svc) == "starting"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string msg;
    CHECK(state_of(svc, &msg) != "failed");
    if (!msg.empty()) std::fprintf(stderr, "direct: %s\n", msg.c_str());
    std::string plan_file;
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(events.size() == 3 && events[0] == "arm" && events[1] == "route Dir tp-shm" && events[2] == "spawn systemd-run");
        CHECK(calls.size() == 1);
        if (!calls.empty()) {
            const auto& a = calls[0];
            auto has = [&](const std::string& x) { return std::find(a.begin(), a.end(), x) != a.end(); };
            CHECK(has("--unit=ravengun-tpui") && has("--working-directory=" + tp) && has("--run") && has(b + "/bin/rg-direct"));
            for (const auto& x : a)
                CHECK(x.find("play-halo") == std::string::npos && x.find("launch-direct") == std::string::npos && x != "sh" && x != "/bin/sh" &&
                      x.find("python") == std::string::npos);
            plan_file = a.back();
        }
    }
    // Het plan voor rg-direct: de lader, het bronbestand van de IO-brug, de drop-in in de omgeving. De bestanden staan in de view.
    {
        std::ifstream f(plan_file);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ravengun::launch::DirectPlan pl;
        std::string err;
        CHECK(ravengun::launch::plan_from_json(text, pl, err));
        CHECK(pl.shm_source == "/dev/shm/ravengun_JvsState" && pl.argv.size() >= 4 && pl.env.at("HOME") == home);
        CHECK(pl.argv[1] == tp + "/OpenParrotWin32/OpenParrotLoader.exe");
        CHECK(fs::exists(home + "/.local/share/ravengun/runtime/views/Dir/hook.dll"));
        CHECK(fs::exists(home + "/.local/share/ravengun/runtime/views/Dir/teknoparrot.ini"));
        CHECK((fs::status(plan_file).permissions() & fs::perms::others_read) == fs::perms::none);
    }
    bool logged = false;
    for (const auto& e : fs::directory_iterator(b + "/logs")) {
        if (e.path().extension() != ".log") continue;
        std::ifstream f(e.path());
        std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        logged = all.find("== direct Dir") == 0 && all.find("Invoerroute: tp-shm") != std::string::npos && all.find("drop-in: ") != std::string::npos &&
                 all.find("bestand: ") != std::string::npos && all.find("rg-direct --run") != std::string::npos;
    }
    CHECK(logged);
    // Stoppen: de unit weg, de invoer neutraal; geen script.
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 2 && calls[1].back() == "ravengun-tpui.service" && events.back() == "stop_inputs");
    }
    // jvs over de pipe: zonder pty-koppeling van de daemon stopt de start (en de invoer gaat weer dicht); met koppeling RG_JVS_PTY.
    // "Layout =" haalt de meegeleverde tp-shm-indeling weg (zoals Let's Go Island: alleen het JVS-bord).
    write(b + "/user/Dir/game.ini", "[Input]\nRoute = jvs\nBoard = LGI\nJvsTransport = pipe\nLayout =\n");
    { std::lock_guard<std::mutex> g(m); calls.clear(); events.clear(); }
    CHECK(svc.start("Dir").ok);
    for (int i = 0; i < 400 && (state_of(svc) == "waiting_move" || state_of(svc) == "starting"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(state_of(svc, &msg) == "failed" && msg.find("pty-symlink") != std::string::npos);
    { std::lock_guard<std::mutex> g(m); CHECK(calls.empty() && events.back() == "stop_inputs"); }
    fs::create_directories(b + "/run");
    fs::create_symlink("/dev/pts/99", c.jvs_link);
    CHECK(svc.start("Dir").ok);
    for (int i = 0; i < 400 && (state_of(svc) == "waiting_move" || state_of(svc) == "starting"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(state_of(svc, &msg) != "failed");
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 1 && std::find(events.begin(), events.end(), "route Dir jvs") != events.end());
        if (!calls.empty()) {
            std::ifstream f(calls[0].back());
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            ravengun::launch::DirectPlan pl;
            std::string err;
            CHECK(ravengun::launch::plan_from_json(text, pl, err) && pl.env.at("RG_JVS_PTY") == c.jvs_link && pl.shm_source == "/dev/shm/TeknoParrot_JvsState");
        }
    }
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    // jvs met een Layout (Let's Go Safari: het stuur staat alleen in het gedeelde geheugen): de daemon schrijft zijn bytes in het bestand
    // van rg-direct, dus de spiegel blijft dat bestand lezen (niet het bestand van de daemon).
    write(b + "/user/Dir/game.ini", "[Input]\nRoute = jvs\nBoard = LGI\nJvsTransport = pipe\n");
    { std::lock_guard<std::mutex> g(m); calls.clear(); events.clear(); }
    CHECK(svc.start("Dir").ok);
    for (int i = 0; i < 400 && (state_of(svc) == "waiting_move" || state_of(svc) == "starting"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(state_of(svc, &msg) != "failed");
    {
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.size() == 1 && std::find(events.begin(), events.end(), "route Dir jvs") != events.end());
        if (!calls.empty()) {
            std::ifstream f(calls[0].back());
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            ravengun::launch::DirectPlan pl;
            std::string err;
            CHECK(ravengun::launch::plan_from_json(text, pl, err) && pl.env.at("RG_JVS_PTY") == c.jvs_link && pl.shm_source == "/dev/shm/TeknoParrot_JvsState");
        }
    }
    CHECK(svc.stop().ok);
    wait_idle_worker(svc);
    // Een directe game waarvan de route niet door de engine gaat: hij start niet (geen script meer), met de reden in de melding.
    write(b + "/user/Dir/game.ini", "[Input]\nRoute = jvs\nBoard = LGI\n");
    { std::lock_guard<std::mutex> g(m); calls.clear(); }
    CHECK(svc.start("Dir").ok);
    wait_idle_worker(svc);
    {
        std::string why2;
        CHECK(state_of(svc, &why2) == "failed" && why2.find("nog geen eigen route") != std::string::npos);
        std::lock_guard<std::mutex> g(m);
        CHECK(calls.empty());
    }
    fs::remove_all(b + "/user/Dir");

    // Een traag plan (een NAS die niet antwoordt): start() antwoordt meteen, de toestand en de lijst ook, en stoppen breekt de start af.
    {
        Config slow = c;
        std::atomic<bool> hold{true};
        std::atomic<int> planning{0};
        slow.session_env = [&] {
            ++planning;
            while (hold.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return ravengun::launch::EnvMap{{"HOME", home}, {"PATH", "/usr/bin:/bin"}, {"DISPLAY", ":0"}};
        };
        std::vector<std::vector<std::string>> scalls;
        GameService ss(slow, [&](const std::vector<std::string>& argv, std::string&) {
            std::lock_guard<std::mutex> g(m);
            scalls.push_back(argv);
            return 0;
        });
        using clk = std::chrono::steady_clock;
        auto ms_since = [](clk::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t).count(); };
        auto t0 = clk::now();
        CHECK(ss.start("Dir").ok);
        CHECK(ms_since(t0) < 500);
        for (int i = 0; i < 400 && planning.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(planning.load() == 1);   // de worker zit in het plan
        t0 = clk::now();
        CHECK(state_of(ss) == "starting");
        CHECK(ss.list_json().find("\"state\":\"starting\"") != std::string::npos);
        CHECK(ss.start("Dir").code == "unavailable");   // al bezig
        CHECK(ms_since(t0) < 500);
        // Stoppen tijdens het plannen: afgebroken, er wordt niets gestart.
        const Result st = ss.stop();
        CHECK(st.ok && st.message.find("afgebroken") != std::string::npos);
        hold.store(false);
        wait_idle_worker(ss);
        std::string smsg;
        CHECK(state_of(ss, &smsg) == "failed" && smsg.find("afgebroken") != std::string::npos);
        { std::lock_guard<std::mutex> g(m); CHECK(scalls.empty()); }
        CHECK(ss.stop().ok);   // niets meer op te ruimen
        { std::lock_guard<std::mutex> g(m); CHECK(scalls.empty()); }
        // Opnieuw, nu loopt het plan door: de start gaat gewoon verder.
        hold.store(true);
        planning.store(0);
        CHECK(ss.start("Dir").ok);
        for (int i = 0; i < 400 && planning.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(state_of(ss) == "starting");
        hold.store(false);
        for (int i = 0; i < 400 && (state_of(ss) == "waiting_move" || state_of(ss) == "starting"); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(state_of(ss, &smsg) != "failed");
        { std::lock_guard<std::mutex> g(m); CHECK(scalls.size() == 1); }
        CHECK(ss.stop().ok);
        wait_idle_worker(ss);
    }
}

// De sha256-cache: per (pad, grootte, mtime) in het geheugen en op schijf; een tweede start of lijst hasht niets opnieuw.
static void test_hash_cache(const std::string& d) {
    namespace L = ravengun::launch;
    const std::string dir = d + "/hc", file = dir + "/Game.exe", cache = dir + "/cache/ravengun/sha256-cache.tsv";
    write(file, "MZ eerste versie");
    std::atomic<int> real{0};
    auto counting = [&](const std::string& p) { ++real; return L::sha256_file(p); };
    const std::string want = L::sha256_file(file);
    {
        L::HashCache hc(cache, counting);
        CHECK(hc.sha256(file) == want && real.load() == 1);
        CHECK(hc.sha256(file) == want && real.load() == 1);   // uit het geheugen
        CHECK(hc.sha256(dir + "/bestaat-niet").empty() && real.load() == 1);
        CHECK(fs::exists(cache));
    }
    {
        L::HashCache hc(cache, counting);   // een nieuw proces: uit het bestand
        CHECK(hc.sha256(file) == want && real.load() == 1 && hc.hashed() == 0);
        // Ander bestand op hetzelfde pad (andere grootte en mtime): opnieuw hashen.
        write(file, "MZ tweede, langere versie");
        fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
        const std::string want2 = L::sha256_file(file);
        CHECK(want2 != want && hc.sha256(file) == want2 && real.load() == 2);
        CHECK(hc.sha256(file) == want2 && real.load() == 2);
    }
    {
        L::HashCache mem({}, counting);   // zonder bestand: alleen in het geheugen
        CHECK(!mem.sha256(file).empty() && real.load() == 3 && !mem.sha256(file).empty() && real.load() == 3);
    }
    // Een kapotte regel in het bestand is geen fout.
    {
        std::ofstream f(cache, std::ios::app);
        f << "kapot\n" << std::string(64, 'a') << "\tx\t1\t/abs\n";
    }
    {
        L::HashCache hc(cache, counting);
        CHECK(!hc.sha256(file).empty() && real.load() == 3);
    }

    // locate_game met de cache: de view hasht een keer, ook bij vijf keer zoeken (plan_direct zoekt meer dan eens per start).
    const std::string views = dir + "/views", nas = dir + "/nas";
    write(views + "/Spel/Game.exe", "MZ eerste versie");
    write(nas + "/Spel/Game.exe", "MZ eerste versie");
    write(nas + "/Ander/Game.exe", "MZ eerste versie");
    ravengun::gamesettings::Ini s;
    s.set("Identify", "Path", "Game.exe");
    s.set("Identify", "Size", std::to_string(fs::file_size(views + "/Spel/Game.exe")));
    s.set("Identify", "Sha256", want);
    L::HashCache hc({}, counting);
    real.store(0);
    std::string how;
    for (int i = 0; i < 5; ++i)
        CHECK(ravengun::gamesettings::locate_game("Spel", s, {nas}, views, "", &how, [&](const std::string& p) { return hc.sha256(p); }) ==
                  views + "/Spel/Game.exe" && how == "view");
    CHECK(real.load() == 1);
    // Zonder view: [Identify] Folder gaat direct naar <gamemap>/<Folder> (geen andere map gehasht), de hint uit library.json ook.
    s.set("Identify", "Folder", "Spel");
    real.store(0);
    CHECK(ravengun::gamesettings::locate_game("Spel", s, {nas}, dir + "/geen-views", "", &how, counting) == nas + "/Spel/Game.exe" && how == "folders");
    CHECK(real.load() == 1);
    ravengun::gamesettings::Ini s2 = s;
    s2.set("Identify", "Folder", "");
    real.store(0);
    CHECK(ravengun::gamesettings::locate_game("Spel", s2, {nas}, dir + "/geen-views", "", &how, counting, {nas + "/Spel"}) ==
              nas + "/Spel/Game.exe" && real.load() == 1);
    real.store(0);   // zonder hint: zoeken, de eerste passende map in alfabetische volgorde (Ander)
    CHECK(ravengun::gamesettings::locate_game("Spel", s2, {nas}, dir + "/geen-views", "", &how, counting) == nas + "/Ander/Game.exe");
    // Een hint die niet past (andere mapnaam bij Folder, of geen bestand) telt niet.
    CHECK(ravengun::gamesettings::locate_game("Spel", s, {nas}, dir + "/geen-views", "", &how, counting, {nas + "/Ander"}) == nas + "/Spel/Game.exe");

    // library_roots: folder en de map van exe voor dit profiel, ~ uitgeklapt, geen dubbele; andere profielen niet.
    write(dir + "/library.json",
          "{\"folders\":[{\"path\":\"" + nas + "\"}],\"games\":["
          "{\"profile\":\"Spel\",\"folder\":\"" + nas + "/Spel/\",\"exe\":\"" + views + "/Spel/Game.exe\"},"
          "{\"profile\":\"Spel\",\"folder\":\"~/x\",\"exe\":\"" + nas + "/Spel/Game.exe\"},"
          "{\"profile\":\"Ander\",\"folder\":\"" + nas + "/Ander\"}]}");
    const auto roots = L::library_roots(dir + "/library.json", "Spel", "Game.exe", "/thuis");
    CHECK(roots.size() == 3 && roots[0] == nas + "/Spel" && roots[1] == views + "/Spel" && roots[2] == "/thuis/x");
    CHECK(L::library_roots(dir + "/bestaat-niet.json", "Spel", "Game.exe", "").empty());
    CHECK(L::library_roots(dir + "/library.json", "Niemand", "Game.exe", "").empty());
}

int main() {
    std::string d = (fs::temp_directory_path() / ("rg-games-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(d);
    fs::create_directories(d);
    test_names();
    test_list_and_icons(d);
    test_long_note(d);
    test_own_profile(d);
    test_processes(d);
    test_service(d);
    test_native_service(d);
    test_unity_service(d);
    test_direct_service(d);
    test_hash_cache(d);
    test_unit_cgroup(d);
    test_navcon(d);
    fs::remove_all(d);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
