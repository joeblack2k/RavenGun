// Tests van de poort vóór de start (src/ravengun/games/readiness.*, docs/prelaunch.md): soort game (lightgun of pad, aantal guns) uit
// de invoerroute, wie er moet wachten, wie meedoet en kalibreert, en de time-out. Zuiver: geen daemon, geen hardware, geen klok.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "ravengun/games/readiness.hpp"
#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/iobridge/routes.hpp"
#include "ravengun/json_parse.hpp"

using namespace ravengun;
using namespace ravengun::games::readiness;
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

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static const std::string kSrc = RAVENGUN_SOURCE_DIR;

struct Packaging {
    iobridge::TpShmProfiles profiles;
    iobridge::HidBoards hid;
    iobridge::JvsBoards jvs;
};

static Packaging load_packaging() {
    Packaging p;
    std::string e1, e2, e3;
    p.profiles = iobridge::parse_tp_shm_profiles(slurp(kSrc + "/packaging/tp-shm-profiles.json"), e1);
    p.hid = iobridge::parse_hid_boards(slurp(kSrc + "/packaging/hid-boards.json"), e2);
    p.jvs = iobridge::parse_jvs_boards(slurp(kSrc + "/packaging/jvs-boards.json"), e3);
    CHECK(e1.empty() && e2.empty() && e3.empty());
    return p;
}

static iobridge::IoRoute route(const std::string& r, const std::string& layout = {}, const std::string& board = {}) {
    iobridge::IoRoute x;
    x.game = "Spel";
    x.route = r;
    x.layout = layout;
    x.board = board;
    return x;
}

static void test_gun_players_of() {
    CHECK(gun_players_of({}) == 0);
    CHECK(gun_players_of({"p1.x"}) == 0);                     // alleen x (Let's Go Safari: een stuur, geen vizier)
    CHECK(gun_players_of({"p1.x", "p1.y"}) == 1);
    CHECK(gun_players_of({"p1.y", "p2.x", "p2.y", "p1.x"}) == 2);
    CHECK(gun_players_of({"p1.x", "p1.y", "p2.x", "p2.y", "p3.x", "p3.y", "p4.x", "p4.y"}) == 4);
    CHECK(gun_players_of({"wheel", "gas", "brake"}) == 0);
}

static void test_classify(const Packaging& pk) {
    // Zonder [Input]: een lightgun voor elke speler (zoals voorheen).
    GameKind g = classify(InputInfo{});
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 4);
    // De virtuele gun en de Unity-plugin zijn altijd lightgun.
    auto r = route("evdev");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 4);
    r = route("unity-tcp");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun);
    // tp-shm: de assen van de indeling.
    r = route("tp-shm", "WartranTroopers");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 4);
    r = route("tp-shm", "TimeCrisis5");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 1);
    r = route("tp-shm", "MKDX");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Pad && g.gun_players == 0);
    // jvs: de analoge kanalen van de borden; Mario Kart (stuur) is een pad-game, een geweerbord niet.
    r = route("jvs", "MKDX", "MKDX");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Pad);
    r = route("jvs", {}, "SegaGun2P");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 2);
    // hid-board: de assen van het bord.
    r = route("hid-board", {}, "SaeGunBoard");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 2);
    // Een onbekende indeling of bord: liever wachten dan blind starten.
    r = route("tp-shm", "BestaatNiet");
    InputInfo in = input_info(&r, pk.profiles, pk.hid, pk.jvs);
    CHECK(!in.layout_known);
    CHECK(classify(in).kind == Kind::Lightgun);
    r = route("jvs", {}, "BestaatNiet");
    CHECK(classify(input_info(&r, pk.profiles, pk.hid, pk.jvs)).kind == Kind::Lightgun);
    // [Input] GunPlayers gaat voor; een ongeldige waarde telt niet.
    r = route("jvs", "MKDX", "MKDX");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs, "1"));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 1);
    r = route("tp-shm", "WartranTroopers");
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs, "0"));
    CHECK(g.kind == Kind::Pad && g.gun_players == 0);
    g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs, "2"));
    CHECK(g.kind == Kind::Lightgun && g.gun_players == 2);
    for (const char* bad : {"7", "-1", "twee", "1x"}) {
        g = classify(input_info(&r, pk.profiles, pk.hid, pk.jvs, bad));
        CHECK(g.kind == Kind::Lightgun && g.gun_players == 4);
    }
    CHECK(!classify(input_info(&r, pk.profiles, pk.hid, pk.jvs)).source.empty());
}

// De echte games in patches/ (als de koppeling er is): Mario Kart DX is een pad-game, Halo en Time Crisis 5 zijn lightgun-games.
static void test_real_games(const Packaging& pk) {
    const std::string patches = kSrc + "/patches";
    if (!fs::exists(patches + "/MarioKartDX/game.ini")) {
        std::printf("patches/ ontbreekt: de test met de echte games is overgeslagen\n");
        return;
    }
    gamesettings::Dirs dirs{patches, {}};
    auto kind = [&](const char* profile) {
        const gamesettings::Game g = gamesettings::load_game(dirs, profile);
        std::string err;
        const iobridge::IoRoute r = iobridge::io_route_from_settings(g, err);
        return classify(input_info(&r, pk.profiles, pk.hid, pk.jvs, g.get("Input", "GunPlayers")));
    };
    CHECK(kind("MarioKartDX").kind == Kind::Pad);
    CHECK(kind("Halo").kind == Kind::Lightgun && kind("Halo").gun_players >= 2);
    if (fs::exists(patches + "/TC5/game.ini")) CHECK(kind("TC5").kind == Kind::Lightgun);
    if (fs::exists(patches + "/TombRaider/game.ini")) CHECK(kind("TombRaider").kind == Kind::Lightgun);
}

static GameKind gun(int n) { GameKind g; g.kind = Kind::Lightgun; g.gun_players = n; return g; }

static void test_decide() {
    GameKind pad;
    pad.kind = Kind::Pad;
    pad.gun_players = 0;
    Inputs none;
    // Pad-game: nooit wachten, geen kalibratie, ook niet als speler 1 een Move heeft die uit staat.
    Inputs p1off;
    p1off.players[0].move_assigned = true;
    Decision d = decide(pad, p1off);
    CHECK(d.ready && d.waiting.empty() && d.calibrate.empty() && d.optional_off.empty() && !d.waiting_any);

    // Speler 1 heeft een Move die uit staat: wachten op speler 1.
    d = decide(gun(2), p1off);
    CHECK(!d.ready && d.waiting == std::vector<int>{1} && !d.waiting_any && d.calibrate.empty());
    // Een andere Move of een gamepad helpt niet: het moet de Move van speler 1 zijn.
    Inputs other = p1off;
    other.any_move_connected = true;
    other.any_pad_connected = true;
    other.players[1].move_assigned = other.players[1].move_connected = true;
    d = decide(gun(2), other);
    CHECK(!d.ready && d.waiting == std::vector<int>{1} && d.calibrate == std::vector<int>{2});
    // ... maar een verbonden muis van speler 1 (de muismodus) wel.
    Inputs mouse = p1off;
    mouse.players[0].mouse_assigned = mouse.players[0].mouse_connected = true;
    d = decide(gun(2), mouse);
    CHECK(d.ready && d.waiting.empty() && d.calibrate.empty());
    // De Move van speler 1 aan: klaar, en speler 1 kalibreert.
    Inputs p1on = p1off;
    p1on.players[0].move_connected = true;
    p1on.any_move_connected = true;
    d = decide(gun(2), p1on);
    CHECK(d.ready && d.calibrate == std::vector<int>{1} && d.optional_off.empty());

    // Speler 2 heeft een Move die uit staat: geen wachten, wel in optional_off (alleen als de game een tweede gun heeft).
    Inputs p2off = p1on;
    p2off.players[1].move_assigned = true;
    d = decide(gun(2), p2off);
    CHECK(d.ready && d.optional_off == std::vector<int>{2} && d.calibrate == std::vector<int>{1});
    d = decide(gun(1), p2off);
    CHECK(d.ready && d.optional_off.empty() && d.calibrate == std::vector<int>{1});
    // Speler 2 aan: hij kalibreert na speler 1; speler 3 (aan) valt buiten een twee-gun-game.
    Inputs p23on = p2off;
    p23on.players[1].move_connected = true;
    p23on.players[2].move_assigned = p23on.players[2].move_connected = true;
    d = decide(gun(2), p23on);
    CHECK(d.ready && d.calibrate == (std::vector<int>{1, 2}) && d.optional_off.empty());
    d = decide(gun(4), p23on);
    CHECK(d.calibrate == (std::vector<int>{1, 2, 3}));
    // Een muis van speler 2 is geen Move die uit staat.
    Inputs p2mouse = p2off;
    p2mouse.players[1].mouse_assigned = p2mouse.players[1].mouse_connected = true;
    CHECK(decide(gun(2), p2mouse).optional_off.empty());

    // Speler 1 zonder Move: het oude gedrag (een willekeurige Move, muis of gamepad).
    d = decide(gun(4), none);
    CHECK(!d.ready && d.waiting_any && d.waiting.empty());
    Inputs anymove;
    anymove.any_move_connected = true;
    CHECK(decide(gun(4), anymove).ready);
    Inputs anypad;
    anypad.any_pad_connected = true;
    CHECK(decide(gun(4), anypad).ready);
    Inputs p3mouse;
    p3mouse.players[2].mouse_assigned = p3mouse.players[2].mouse_connected = true;
    CHECK(decide(gun(4), p3mouse).ready);
    // Een gun_players buiten 1..4 wordt geklemd (geen lees buiten de spelers).
    CHECK(decide(gun(9), p23on).calibrate == (std::vector<int>{1, 2, 3}));
    CHECK(decide(gun(0), p23on).calibrate == std::vector<int>{1});
}

static void test_wait_and_text() {
    Decision ready;
    Decision waiting;
    waiting.ready = false;
    waiting.waiting = {1};
    CHECK(wait_step(ready, 0, 999999) == WaitStep::Ready);
    CHECK(wait_step(waiting, 1000, 1000) == WaitStep::Waiting);
    CHECK(wait_step(waiting, 1000, 1000 + kMoveWaitMs - 1) == WaitStep::Waiting);
    CHECK(wait_step(waiting, 1000, 1000 + kMoveWaitMs) == WaitStep::TimedOut);
    CHECK(wait_step(waiting, 0, 5000, 5000) == WaitStep::TimedOut);
    CHECK(wait_step(waiting, 0, 1LL << 40, 0) == WaitStep::Waiting);   // 0 = geen limiet (het startscherm van de app)
    CHECK(kMoveWaitMs == 120000);

    CHECK(waiting_text(ready).empty());
    CHECK(waiting_text(waiting).find("speler 1") != std::string::npos);
    Decision any;
    any.ready = false;
    any.waiting_any = true;
    CHECK(waiting_text(any).find("PS Move") != std::string::npos);
}

static void test_json() {
    Inputs in;
    in.players[0].move_assigned = true;
    in.players[1].move_assigned = true;
    in.players[1].move_connected = true;
    in.players[2].mouse_assigned = true;
    const Decision d = decide(gun(2), in);
    const auto pr = json::parse(decision_json(d, in));
    CHECK(pr.ok);
    const json::Value& v = pr.value;
    CHECK(v.find("lightgun") && v.find("lightgun")->b);
    CHECK(v.find("gun_players") && v.find("gun_players")->num == 2);
    CHECK(v.find("ready") && !v.find("ready")->b);
    CHECK(v.find("waiting") && v.find("waiting")->arr.size() == 1 && v.find("waiting")->arr[0].num == 1);
    CHECK(v.find("calibrate") && v.find("calibrate")->arr.size() == 1 && v.find("calibrate")->arr[0].num == 2);
    CHECK(v.find("optional_off") && v.find("optional_off")->arr.empty());
    CHECK(v.find("waiting_any") && !v.find("waiting_any")->b);
    CHECK(v.find("players") && v.find("players")->arr.size() == 4);
    CHECK(v.find("players")->arr[2].find("mouse")->b && !v.find("players")->arr[2].find("mouse_connected")->b);
    CHECK(v.find("source") && v.find("source")->is_string());
}

int main() {
    const Packaging pk = load_packaging();
    test_gun_players_of();
    test_classify(pk);
    test_real_games(pk);
    test_decide();
    test_wait_and_text();
    test_json();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
