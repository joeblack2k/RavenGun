// IO-brug: toestandsmodel, tp-shm-indeling per profiel (bytes exact), schaal en inversie, neutrale toestand, keuze van de
// route per game en dat een nieuwe brug (daemon-herstart) het bestand en de route weer overneemt. Zonder hardware: nep-guns.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include "iobridge_service.hpp"
#include "output_service.hpp"
#include "ravengun/iobridge/control_pipe.hpp"
#include "ravengun/iobridge/hid_board.hpp"
#include "ravengun/iobridge/jvs_dialect.hpp"
#include "ravengun/iobridge/routes.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/key_table.hpp"
#include "shared.hpp"

using namespace ravengun;
using namespace ravengun::iobridge;
using namespace ravengund;
namespace fs = std::filesystem;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct FakeGun : output::GunDevice {
    bool exists = false;
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; }
    bool created() const override { return exists; }
    bool write_position(int, int) override { return exists; }
    bool write_button(output::GunButton, bool) override { return exists; }
};
struct FakeKbd : output::KeyDevice {
    bool exists = false;
    std::atomic<bool> shift{false}, t{false};
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; }
    bool created() const override { return exists; }
    bool write_key(int code, bool down) override {
        if (code == KEY_LEFTSHIFT) shift = down;
        if (code == KEY_T) t = down;
        return exists;
    }
};

TpShmProfiles load_profiles() {
    std::string err;
    TpShmProfiles p = parse_tp_shm_profiles(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/tp-shm-profiles.json"), err);
    CHECK(err.empty());
    return p;
}

ControlPipeProfiles load_control_pipes() {
    std::string err;
    ControlPipeProfiles p = parse_control_pipe_profiles(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/control-pipe-profiles.json"), err);
    CHECK(err.empty());
    return p;
}

uint32_t control(const std::vector<uint8_t>& b) {
    uint32_t c;
    std::memcpy(&c, b.data() + 8, 4);
    return c;
}

std::vector<uint8_t> render(const TpShmProfile& p, const IoState& s, uint8_t fill = 0) {
    std::vector<uint8_t> b(static_cast<size_t>(p.size), fill);
    tp_shm_render(p, s, b.data(), b.size());
    return b;
}

IoPlayer aim(double x, double y) {
    IoPlayer p;
    p.valid = true;
    p.x = x;
    p.y = y;
    p.offscreen = x <= 0.0 || x >= 1.0 || y <= 0.0 || y >= 1.0;   // een punt op de rand telt als buiten het scherm
    return p;
}

// Een punt op de rand dat nog wel op het scherm ligt.
IoPlayer aim_inside(double x, double y) {
    IoPlayer p = aim(x, y);
    p.offscreen = false;
    return p;
}

// Toestandsmodel: signaalnamen.
void test_model() {
    IoState s;
    s.player[1].trigger = true;
    s.sys.coin[2] = true;
    s.sys.test = true;
    bool v = false;
    CHECK(io_button_signal(s, "p2.trigger", v) && v);
    CHECK(io_button_signal(s, "p1.trigger", v) && !v);
    CHECK(io_button_signal(s, "coin3", v) && v);
    CHECK(io_button_signal(s, "test", v) && v);
    CHECK(!io_button_signal(s, "p5.trigger", v) && !io_button_signal(s, "p1.zzz", v) && !io_button_signal(s, "coin5", v));
    s.player[3].x = 0.25;
    double d = 0;
    CHECK(io_axis_signal(s, "p4.x", d) && d == 0.25);
    CHECK(!io_axis_signal(s, "p1.trigger", d));
    CHECK(valid_button_signal("volume_up") && !valid_button_signal("p1.x") && valid_axis_signal("p2.y") && !valid_axis_signal("coin1"));
}

// De profielen: bytes exact.
void test_layouts() {
    TpShmProfiles ps = load_profiles();
    for (const char* n : {"WartranTroopers", "RawThrillsGUN", "TimeCrisis5", "TransformersShadowsRising", "LGS", "MusicGunGun2Pipe", "HOTDSDPipe", "FarCryPipe"})
        CHECK(ps.find(n));
    CHECK(!ps.find("Bestaat niet"));

    // WartranTroopers (Halo): munt 1 = 0x4, trekker P1 = 0x80, start P2 = 0x800, granaat P4 = bit 24.
    {
        const TpShmProfile& p = *ps.find("WartranTroopers");
        IoState s;
        s.player[0] = aim(0.0, 1.0);
        s.player[0].trigger = true;
        s.player[1].start = true;
        s.player[3] = aim(1.0, 0.0);
        s.player[3].grenade = true;
        s.sys.coin[0] = true;
        auto b = render(p, s);
        CHECK(control(b) == (0x4u | 0x80u | 0x800u | (1u << 24)));
        CHECK(b[12] == 0 && b[13] == 255);        // P1 X, Y
        CHECK(b[14] == 128 && b[15] == 128);      // P2 zonder aim: midden
        CHECK(b[18] == 255 && b[19] == 0);        // P4
        IoState t;
        t.sys.test = t.sys.service = true;
        t.player[0].reload = true;                // Option = Button2 = bit 8
        t.player[2].aux1 = true;                  // Sense P3 = bit 20
        CHECK(control(render(p, t)) == (0x1u | 0x2u | 0x100u | (1u << 20)));
    }
    // RawThrillsGUN (Terminator, Jurassic Park).
    {
        const TpShmProfile& p = *ps.find("RawThrillsGUN");
        IoState s;
        s.player[0] = aim(0.25, 0.75);
        s.player[0].start = s.player[0].reload = true;
        s.sys.test = true;
        auto b = render(p, s);
        CHECK(control(b) == (0x1u | 0x10u | 0x80u));
        CHECK(b[12] == 64 && b[16] == 191);       // X en Y van P1
        CHECK(b[20] == 128 && b[24] == 128);
        IoState t;
        t.player[0].trigger = t.player[0].grenade = true;
        t.player[1].trigger = true;
        t.sys.coin[1] = true;
        t.sys.volume_down = true;
        CHECK(control(render(p, t)) == (0x20u | 0x40u | 0x200u | 0x8u | 0x2000u));
    }
    // TimeCrisis5: munt, trekker, pedaal (links), start = Enter.
    {
        const TpShmProfile& p = *ps.find("TimeCrisis5");
        IoState s;
        s.player[0] = aim(1.0, 0.0);
        s.player[0].trigger = s.player[0].start = s.player[0].grenade = true;
        s.sys.coin[0] = true;
        auto b = render(p, s);
        CHECK(control(b) == (0x8u | 0x10u | 0x400u | 0x40u));
        CHECK(b[12] == 255 && b[16] == 0);
        IoState t;
        t.player[0].reload = t.player[0].aux1 = true;
        t.sys.test = true;
        t.sys.service = true;
        CHECK(control(render(p, t)) == (0x20u | 0x80u | 0x1u | 0x4u));
    }
    // Transformers: complement, Y op 12 en X op 16; start, trekker, hendel, munt.
    {
        const TpShmProfile& p = *ps.find("TransformersShadowsRising");
        IoState s;
        s.player[0] = aim(0.0, 1.0);
        s.player[0].start = s.player[0].trigger = true;
        s.player[1] = aim(0.5, 0.25);
        s.player[1].trigger = true;
        s.sys.coin[0] = true;
        auto b = render(p, s);
        CHECK(control(b) == (0x1u | 0x2u | 0x8u | 0x40u));
        CHECK(b[16] == 255 && b[12] == 0);        // P1 X links = 255, Y onder = 0
        CHECK(b[24] == 127 && b[20] == 191);      // P2 X 0,5 -> 128, complement 127; Y 0,25 -> 64, complement 191
        IoState t;
        t.player[0].reload = true;
        t.player[0].grenade = true;
        CHECK(control(render(p, t)) == (0x10u | 0x20u));
        auto n = render(p, IoState{});
        CHECK(control(n) == 0 && n[12] == 127 && n[16] == 127);   // neutraal: midden (complement van 128)
    }
    // MusicGunGun2Pipe (Taito Type X2: Gaia Attack 4, Haunted Museum 1/2, Music GunGun 2): zes bits en acht assen.
    {
        const TpShmProfile& p = *ps.find("MusicGunGun2Pipe");
        CHECK(!p.complete && p.axes.size() == 8 && p.bits.size() == 6);
        IoState s;
        s.player[0] = aim(0.25, 0.75);
        s.player[0].trigger = s.player[0].start = true;         // start: niet in dit geheugen (JVS-pipe)
        s.player[1] = aim(0.5, 0.5);
        s.player[1].trigger = true;
        s.player[2] = aim(0.1, 0.9);
        s.player[2].trigger = true;
        s.player[3] = aim(0.9, 0.1);
        s.player[3].trigger = true;
        s.sys.coin[0] = s.sys.test = s.sys.service = true;      // idem
        auto b = render(p, s);
        CHECK(control(b) == (0x1u | 0x4u | 0x40u | 0x80u));
        CHECK(b[12] == 64 && b[16] == 191 && b[20] == 128 && b[24] == 128);
        CHECK(b[28] == 26 && b[32] == 230 && b[36] == 230 && b[40] == 26);
        IoState t;
        t.sys.volume_up = true;
        t.player[0].reload = t.player[0].grenade = true;        // geen bit: de pipe kent die knoppen niet
        CHECK(control(render(p, t)) == 0x10u);
        t.sys.volume_up = false;
        t.sys.volume_down = true;
        CHECK(control(render(p, t)) == 0x20u);
        auto n = render(p, IoState{});
        CHECK(control(n) == 0 && n[12] == 128 && n[40] == 128);
    }
    // HOTDSDPipe (House of the Dead: Scarlet Dawn): twee trekkers, vier assen; de rest gaat over de besturingspipe (AllsUsbIo).
    {
        const TpShmProfile& p = *ps.find("HOTDSDPipe");
        CHECK(p.complete && p.axes.size() == 4 && p.bits.size() == 2);
        IoState s;
        s.player[0] = aim(0.2, 0.4);
        s.player[0].trigger = true;
        s.player[1] = aim(0.8, 0.6);
        s.player[1].trigger = true;
        auto b = render(p, s);
        CHECK(control(b) == 0x3u);
        CHECK(b[12] == 51 && b[16] == 102 && b[20] == 204 && b[24] == 153);
        IoState t;
        t.player[1].trigger = true;
        CHECK(control(render(p, t)) == 0x2u);
        t.player[1].trigger = false;
        t.player[0].start = t.player[0].reload = true;
        CHECK(control(render(p, t)) == 0u);
    }
    // FarCryPipe (Far Cry Paradise Lost, Medaru No Gunman): bits 3, 5..10, 13..16.
    {
        const TpShmProfile& p = *ps.find("FarCryPipe");
        CHECK(p.complete && p.axes.size() == 4 && p.bits.size() == 11);
        IoState s;
        s.player[0] = aim(0.0, 1.0);
        s.player[0].trigger = s.player[0].start = s.player[0].grenade = true;
        s.sys.coin[0] = s.sys.test = s.sys.service = true;
        s.sys.coin[1] = true;                                    // er is maar een muntbit
        auto b = render(p, s);
        CHECK(control(b) == (0x200u | 0x100u | 0x20u | 0x400u | 0x40u | 0x80u));
        CHECK(b[12] == 0 && b[16] == 255);
        IoState t;
        t.player[1].trigger = t.player[1].start = t.player[1].grenade = true;
        t.sys.volume_up = t.sys.volume_down = true;
        CHECK(control(render(p, t)) == (0x4000u | 0x2000u | 0x8u | 0x8000u | 0x10000u));
    }
    // LGS: alleen het stuurwiel.
    {
        const TpShmProfile& p = *ps.find("LGS");
        CHECK(!p.complete && p.bits.empty());
        IoState s;
        s.player[0] = aim(1.0, 0.5);
        auto b = render(p, s);
        CHECK(b[12] == 255 && control(b) == 0);
    }
    // Overige bytes blijven ongemoeid.
    {
        const TpShmProfile& p = *ps.find("RawThrillsGUN");
        auto b = render(p, IoState{}, 0xAA);
        CHECK(b[0] == 0xAA && b[7] == 0xAA && b[28] == 0xAA && b[63] == 0xAA);
        CHECK(control(b) == 0xAAAA8000u);   // de gebruikte bits (0..13) zijn gewist; de ongebruikte (14..31) blijven zoals ze waren
    }
}

// Schaal, inversie, 16 bit en ongeldige beschrijvingen.
void test_scale_and_validation() {
    std::string err;
    const char* js = R"({"profiles":{"X":{"size":32,"bits":[{"signal":"p1.trigger","offset":4,"bit":9}],
      "axes":[{"signal":"p1.x","offset":12,"bytes":2,"min":0,"max":65535},
              {"signal":"p1.y","offset":16,"bytes":2,"min":100,"max":200,"invert":true,"big_endian":false,"idle":0.0},
              {"signal":"p2.x","offset":20,"min":10,"max":20}]}}})";
    TpShmProfiles ps = parse_tp_shm_profiles(js, err);
    CHECK(err.empty() && ps.find("X"));
    const TpShmProfile& p = *ps.find("X");
    IoState s;
    s.player[0] = aim(0.5, 0.25);
    s.player[0].trigger = true;
    auto b = render(p, s);
    CHECK(b[5] == 0x02 && b[4] == 0);                       // bit 9 = byte 5, bit 1
    CHECK(b[12] == 0x80 && b[13] == 0x00);                  // 0,5 * 65535 = 32767,5 -> 32768, big-endian
    CHECK(b[16] == 175 && b[17] == 0);                      // 1 - 0,25 = 0,75 -> 175, little-endian
    CHECK(b[20] == 15);                                     // P2 zonder aim: midden van 10..20
    IoState n;
    auto bn = render(p, n);
    CHECK(bn[16] == 200 && bn[5] == 0);                     // idle 0.0 met inversie -> max
    // Buiten 0..1 wordt begrensd.
    s.player[0] = aim(-3.0, 7.0);
    auto bc = render(p, s);
    CHECK(bc[12] == 0 && bc[13] == 0 && bc[16] == 100);   // y = 7 -> 1, geinverteerd: min
    // Ongeldige beschrijvingen.
    auto bad = [&](const char* text) { std::string e; parse_tp_shm_profiles(text, e); return !e.empty(); };
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signal":"p9.trigger","offset":8,"bit":0}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signal":"test","offset":8,"bit":0},{"signal":"service","offset":8,"bit":0}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"axes":[{"signal":"p1.x","offset":63,"bytes":2}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"axes":[{"signal":"p1.x","offset":12},{"signal":"p1.y","offset":12}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"axes":[{"signal":"p1.trigger","offset":12}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"axes":[{"signal":"p1.x","offset":12,"min":300}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"size":8}}})"));
    CHECK(bad("geen json"));
    CHECK(!bad(R"({"profiles":{}})"));
}

// Offscreen-regel (findings 09 par. 2): de kern leest een as <= 1 of >= 254 als "buiten beeld" (herladen). Binnen het scherm
// schrijven we dus nooit zo'n byte; 0 of 255 alleen als de speler echt buiten het scherm mikt.
void test_offscreen_rule() {
    TpShmProfiles ps = load_profiles();
    for (const char* n : {"WartranTroopers", "RawThrillsGUN", "TimeCrisis5", "MusicGunGun2Pipe", "HOTDSDPipe", "FarCryPipe"}) {
        const TpShmProfile& p = *ps.find(n);
        CHECK(p.offscreen_margin == 2);
        // Binnen het scherm, ook op de uiterste rand: altijd 2..253, over een fijn raster.
        for (int i = 0; i <= 1000; ++i) {
            const double t = i / 1000.0;
            IoState s;
            s.player[0] = aim_inside(t, 1.0 - t);
            s.player[1] = aim_inside(1.0 - t, t);
            auto b = render(p, s);
            for (const auto& a : p.axes) {
                if (a.signal[1] > '2') continue;
                CHECK(b[static_cast<size_t>(a.offset)] >= 2 && b[static_cast<size_t>(a.offset)] <= 253);
            }
        }
        // De uiterste rand binnen het scherm is precies 2 en 253; het midden blijft ongemoeid.
        IoState e;
        e.player[0] = aim_inside(0.0, 1.0);
        auto be = render(p, e);
        CHECK(be[static_cast<size_t>(p.axes[0].offset)] == 2 && be[static_cast<size_t>(p.axes[1].offset)] == 253);
        IoState m;
        m.player[0] = aim_inside(0.5, 0.5);
        auto bm = render(p, m);
        CHECK(bm[static_cast<size_t>(p.axes[0].offset)] == 128 && bm[static_cast<size_t>(p.axes[1].offset)] == 128);
        // Buiten het scherm: de as die de rand raakt wordt 0 of 255 (herladen), de andere blijft gewoon begrensd.
        IoState o;
        o.player[0] = aim(0.0, 0.5);
        o.player[0].offscreen = true;
        auto bo = render(p, o);
        CHECK(bo[static_cast<size_t>(p.axes[0].offset)] == 0 && bo[static_cast<size_t>(p.axes[1].offset)] == 128);
        o.player[0] = aim(0.3, 1.0);
        auto bo2 = render(p, o);
        CHECK(bo2[static_cast<size_t>(p.axes[0].offset)] == 77 && bo2[static_cast<size_t>(p.axes[1].offset)] == 255);
        o.player[0] = aim(1.0, 0.0);
        auto bo3 = render(p, o);
        CHECK(bo3[static_cast<size_t>(p.axes[0].offset)] == 255 && bo3[static_cast<size_t>(p.axes[1].offset)] == 0);
        // Zonder geldige aim: midden.
        auto bn = render(p, IoState{});
        CHECK(bn[static_cast<size_t>(p.axes[0].offset)] == 128);
    }
    // Stuurwiel en joystick-achtige profielen: geen marge.
    for (const char* n : {"LGS", "TransformersShadowsRising"}) CHECK(ps.find(n)->offscreen_margin == 0);
    {
        IoState s;
        s.player[0] = aim_inside(1.0, 0.5);
        CHECK(render(*ps.find("LGS"), s)[12] == 255);
    }
    // Een venster dat niet tot de uiteinden loopt (Sega-achtig) en 16 bit blijven onaangeroerd.
    std::string err;
    const char* js = R"({"profiles":{"V":{"axes":[{"signal":"p1.x","offset":12,"min":27,"max":208},
      {"signal":"p1.y","offset":16,"bytes":2,"min":0,"max":65535}]}}})";
    TpShmProfiles vs = parse_tp_shm_profiles(js, err);
    CHECK(err.empty());
    IoState s;
    s.player[0] = aim_inside(0.0, 1.0);
    auto b = render(*vs.find("V"), s);
    CHECK(b[12] == 27 && b[16] == 255 && b[17] == 255);
    // Ongeldige marge.
    parse_tp_shm_profiles(R"({"profiles":{"X":{"offscreen_margin":50}}})", err);
    CHECK(!err.empty());
}

void put(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string repo_patches() { return std::string(RAVENGUN_SOURCE_DIR) + "/patches"; }

void test_routes(const std::string& dir) {
    std::string err;
    gamesettings::Dirs real{repo_patches(), {}};
    IoRoutes r = load_io_routes(real, err);
    CHECK(err.empty());
    CHECK(r.route_for("TransformersShadowsRising") == "hid-board" && r.route_for("TC5") == "tp-shm");
    CHECK(r.find("TransformersShadowsRising")->board == "SaeGunBoard");
    CHECK(r.route_for("HOTDSD") == "evdev" && r.route_for("TombRaider") == "unity-tcp");
    CHECK(r.route_for("JurassicPark") == "tp-shm");     // het lightgun-bord via de RIO-brug (2026-10-14)
    CHECK(r.route_for("OnbekendeGame") == "evdev");     // standaardroute
    CHECK(r.find("TC5")->layout == "TimeCrisis5");
    CHECK(r.find("TC5")->coin_keys.size() == 2 && r.find("TC5")->coin_keys[0] == "lshift");
    CHECK(r.find("LGS")->route == "jvs" && r.find("LGS")->board == "LGS" && r.find("LGS")->com_port == "COM4");
    // Elke layout in patches/*/game.ini bestaat in tp-shm-profiles.json, elke ControlPipe in control-pipe-profiles.json.
    TpShmProfiles ps = load_profiles();
    const ControlPipeProfiles cps = load_control_pipes();
    for (const auto& g : r.games) {
        if (!g.layout.empty()) CHECK(ps.find(g.layout));
        if (!g.control_pipe.empty()) CHECK(cps.find(g.control_pipe));
    }

    // Eigen map in de test: controles per route, lijsten en de gebruikerslaag.
    const std::string sh = dir + "/rt-shipped", us = dir + "/rt-user";
    fs::remove_all(sh);
    fs::remove_all(us);
    put(sh + "/_defaults/Unity.ini", "[Input]\nRoute = unity-tcp\n");
    put(sh + "/A/game.ini", "[Input]\nRoute = hid-board\n");                       // zonder Board: ongeldig
    put(sh + "/B/game.ini", "[Input]\nRoute = jvs\n");                             // zonder Board
    put(sh + "/C/game.ini", "[Input]\nRoute = jvs\nBoards = X, Y\nComPort = COM2 ; commentaar\n");
    put(sh + "/D/game.ini", "[Input]\nRoute = tp-shm\n");                          // zonder Layout
    put(sh + "/E/game.ini", "[Input]\nRoute = bluetooth\n");
    put(sh + "/F/game.ini", "[Input]\nRoute = evdev\nCoinKeys = bestaatniet\n");
    put(sh + "/G/game.ini", "[Game]\nType = Unity\n");                             // de route komt uit _defaults/Unity.ini
    put(sh + "/H/game.ini", "[Game]\nName = H\n");                                 // geen [Input]
    put(sh + "/I/game.ini", "[Input]\nRoute = hid-board\nBoards = SaeGunBoard, SaeIoBoard\n");          // lijst
    put(sh + "/J/game.ini", "[Input]\nRoute = hid-board\nBoard = SaeGunBoard\nBoards = SaeIoBoard\n");  // lijst wint
    put(sh + "/TC5/game.ini", "[Input]\nRoute = tp-shm\nLayout = TimeCrisis5\nCoinKeys = lshift,t\n");
    put(sh + "/K/game.ini", "[Input]\nRoute = jvs\nBoard = LGS\nControlPipe = AllsUsbIo\n");      // ControlPipe hoort bij tp-shm
    put(sh + "/L/game.ini", "[Input]\nRoute = tp-shm\nLayout = HOTDSDPipe\nControlPipe = AllsUsbIo\n");
    put(sh + "/M/game.ini", "[Input]\nControlPipe = AllsUsbIo\n");                                  // evdev: mag (de directe start maakt er tp-shm van)
    gamesettings::Dirs d{sh, {}};
    IoRoutes t = load_io_routes(d, err);
    CHECK(!err.empty());                                                           // de eerste melding
    CHECK(!t.find("A") && !t.find("B") && !t.find("D") && !t.find("E") && !t.find("F") && !t.find("K"));
    CHECK(t.find("L") && t.find("L")->control_pipe == "AllsUsbIo" && t.find("M") && t.find("M")->route == "evdev" && t.find("M")->control_pipe == "AllsUsbIo");
    CHECK(t.find("TC5") && t.find("TC5")->control_pipe.empty());
    CHECK(t.find("C") && t.find("C")->boards.size() == 2 && t.find("C")->boards[1] == "Y" && t.find("C")->com_port == "COM2");
    CHECK(t.route_for("G") == "unity-tcp" && t.route_for("H") == "evdev" && !t.find("H"));
    // hid-board mag één bord of een lijst hebben; de lijst is de vorm voor de gun naast het IO-bord.
    CHECK(t.find("I") && t.find("I")->route == "hid-board" && t.find("I")->boards.size() == 2 &&
          t.find("I")->boards[0] == "SaeGunBoard" && t.find("I")->boards[1] == "SaeIoBoard");
    CHECK(t.find("J") && t.find("J")->boards.size() == 1 && t.find("J")->boards[0] == "SaeIoBoard");
    CHECK(t.route_for("TC5") == "tp-shm" && t.find("TC5")->coin_keys.size() == 2);

    // Gebruikerslaag: per sleutel over de meegeleverde laag; een lege waarde zet een sleutel terug.
    for (const char* bad : {"A", "B", "D", "E", "F", "K"}) fs::remove_all(sh + "/" + bad);   // de ongeldige games hierboven
    d.user = us;
    put(us + "/TC5/game.ini", "[Input]\nRoute = evdev\n");
    put(us + "/Nieuw/game.ini", "[Input]\nRoute = evdev\n");
    IoRoutes u = load_io_routes(d, err);
    CHECK(u.route_for("TC5") == "evdev" && u.find("TC5")->layout == "TimeCrisis5" && u.find("TC5")->coin_keys.size() == 2);   // de rest blijft
    CHECK(u.route_for("Nieuw") == "evdev" && u.find("Nieuw"));
    put(us + "/TC5/game.ini", "[Input]\nCoinKeys =\n");
    u = load_io_routes(d, err);
    CHECK(u.route_for("TC5") == "tp-shm" && u.find("TC5")->coin_keys.empty());
    // Een gebruikerslaag die de route ongeldig maakt, telt niet mee (melding), de meegeleverde stand blijft.
    put(us + "/TC5/game.ini", "[Input]\nRoute = onzin\n");
    u = load_io_routes(d, err);
    CHECK(u.route_for("TC5") == "tp-shm" && err.find("TC5") != std::string::npos);
    put(us + "/TC5/game.ini", "[Input]\nRoute = hid-board\n");                    // hid-board zonder Board
    u = load_io_routes(d, err);
    CHECK(u.route_for("TC5") == "tp-shm" && !err.empty());
    fs::remove_all(us);
    CHECK(load_io_routes(d, err).route_for("TC5") == "tp-shm" && err.empty());
}

uint16_t word(const std::vector<uint8_t>& b, size_t off) { return static_cast<uint16_t>(b[off] | (b[off + 1] << 8)); }

// De besturingspipe (ALLS USB-IO-rapport van TeknoParrotUi, packaging/control-pipe-profiles.json): bytes exact, munt bij het loslaten,
// Up en Down nooit samen, controle van de indeling, en het dialect op een echt bestand (ook na een herstart: de munt telt door).
void test_control_pipe(const std::string& dir) {
    const ControlPipeProfiles cps = load_control_pipes();
    const ControlPipeProfile* pp = cps.find("AllsUsbIo");
    CHECK(pp != nullptr);
    if (!pp) return;
    const ControlPipeProfile& p = *pp;
    CHECK(p.pipe == "TeknoParrotPipe" && p.size == 64 && p.interval_ms == 15 && p.analog_channels == 8 && p.axes.size() == 4);
    {   // de ingebouwde kopie (voor het plan van de directe start en rg-direct) is dezelfde
        std::string e;
        const ControlPipeProfiles bi = builtin_control_pipe_profiles(e);
        CHECK(e.empty() && bi.find("AllsUsbIo") && bi.find("AllsUsbIo")->bits.size() == p.bits.size());
    }
    // min(byte * 65535/255 + 1, 65534)
    CHECK(control_pipe_analog(0) == 1 && control_pipe_analog(1) == 258 && control_pipe_analog(128) == 32897 && control_pipe_analog(254) == 65279);
    CHECK(control_pipe_analog(255) == 65534 && control_pipe_analog(300) == 65534);
    // Neutraal: aim in het midden (byte 128) op de vier gebruikte kanalen, de andere kanalen byte 0, verder alles nul.
    IoState s;
    auto b = control_pipe_render(p, s, {0});
    CHECK(b.size() == 64);
    CHECK(word(b, 0) == 32897 && word(b, 2) == 1 && word(b, 4) == 32897 && word(b, 6) == 1 && word(b, 8) == 32897 && word(b, 12) == 32897);
    CHECK(word(b, 10) == 1 && word(b, 14) == 1);
    {
        bool rest_nul = true;
        for (size_t i = 16; i < 64; ++i) rest_nul = rest_nul && b[i] == 0;
        CHECK(rest_nul);
    }
    // Alles tegelijk: P1 links onder, trekker, start, subtrekker, test en service; P2 rechts boven, start en controllerkeuze.
    s.player[0] = aim(0.0, 1.0);
    s.player[0].trigger = s.player[0].start = s.player[0].grenade = true;
    s.player[1] = aim(1.0, 0.0);
    s.player[1].start = s.player[1].aux1 = true;
    s.sys.test = s.sys.service = true;
    b = control_pipe_render(p, s, {3});
    CHECK(word(b, 0) == 1 && word(b, 4) == 65534 && word(b, 8) == 65534 && word(b, 12) == 1);   // P1 X, P1 Y, P2 X, P2 Y
    CHECK(b[24] == 0 && b[25] == 3);                                                            // munttellier 1 (teller * 256)
    CHECK(b[28] == (0x80 | 0x40 | 0x20 | 0x02) && b[29] == 0x02);                              // start, service, Up, Button1; test
    CHECK(b[30] == (0x80 | 0x10) && b[31] == 0);                                                // P2: start, Down
    // Up en Down nooit samen: de trekker wint; controllerkeuze ook van de gamepad (btn1).
    IoState t;
    t.player[0].aux1 = true;
    CHECK(control_pipe_render(p, t, {0})[28] == 0x10);
    t.player[0].trigger = true;
    CHECK(control_pipe_render(p, t, {0})[28] == 0x20);
    IoState g;
    g.player[0].btn[0] = true;
    CHECK(control_pipe_render(p, g, {0})[28] == 0x10);
    // Herladen, aux2 en munt 2 staan niet in het rapport.
    IoState n;
    n.player[0].reload = n.player[0].aux2 = true;
    n.sys.coin[1] = true;
    b = control_pipe_render(p, n, {0});
    CHECK(b[28] == 0 && b[29] == 0 && b[25] == 0);
    CHECK(control_pipe_render(p, IoState{}, {300})[25] == (300 & 0xFF));   // de teller loopt rond in een byte, zoals bij TeknoParrotUi

    // Controle van de indeling.
    auto bad = [&](const char* text) { std::string e; parse_control_pipe_profiles(text, e); return !e.empty(); };
    CHECK(bad("[]") && bad(R"({"profiles":1})"));
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signals":["p1.vliegen"],"offset":28,"bit":1}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signals":["p1.start"],"offset":28,"bit":1},{"signals":["p1.trigger"],"offset":29,"bit":-7}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signals":["p1.start"],"offset":28,"bit":1},{"signals":["p2.start"],"offset":28,"bit":1}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"analog":{"offset":0,"channels":8},"bits":[{"signals":["p1.start"],"offset":4,"bit":0}]}}})"));   // in een kanaal
    CHECK(bad(R"({"profiles":{"X":{"analog":{"offset":0,"channels":2},"axes":[{"signal":"p1.x","channel":2}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"analog":{"offset":60,"channels":8}}}})"));
    CHECK(bad(R"({"profiles":{"X":{"coins":[{"signals":["coin1"],"offset":63}]}}})"));
    CHECK(bad(R"({"profiles":{"X":{"pipe":"a\\b"}}})") && bad(R"({"profiles":{"X":{"interval_ms":0}}})") && bad(R"({"profiles":{"X":{"size":"64"}}})"));
    CHECK(bad(R"({"profiles":{"X":{"bits":[{"signals":["p1.start"],"unless":["nee"],"offset":28,"bit":1}]}}})"));
    CHECK(!bad(R"({"profiles":{"X":{"bits":[{"signal":"p1.start","offset":28,"bit":1}]}}})"));   // "signal" mag ook

    // Het dialect op een echt bestand.
    const std::string path = dir + "/control";
    {
        ControlPipeDialect d(p, "HOTDSD", path);
        std::string err;
        CHECK(d.open(err) && fs::file_size(path) == 64);
        CHECK(slurp(path).size() == 64 && static_cast<uint8_t>(slurp(path)[0]) == 0x81);
        IoState c;
        c.sys.coin[0] = true;
        d.write(c);
        CHECK(d.coins()[0] == 0);                 // ingedrukt telt nog niet
        c.sys.coin[0] = false;
        d.write(c);
        CHECK(d.coins()[0] == 1);                 // bij het loslaten
        c.player[0].start = true;
        d.write(c);
        std::string f = slurp(path);
        CHECK(static_cast<uint8_t>(f[25]) == 1 && static_cast<uint8_t>(f[28]) == 0x80);
        c.sys.coin[0] = true;
        d.write(c);
        d.neutral();                              // een munt die nog vastzat telt bij neutraal niet
        f = slurp(path);
        CHECK(d.coins()[0] == 1 && static_cast<uint8_t>(f[25]) == 1 && f[28] == 0);
        CHECK(d.status_json().find("\"profile\":\"AllsUsbIo\"") != std::string::npos && d.status_json().find("\"coins\":[1]") != std::string::npos);
        c.sys.coin[0] = false;
        d.write(c);                               // na neutraal: geen flank meer
        CHECK(d.coins()[0] == 1);
        d.close();
    }
    {   // Herstart: de teller loopt door vanaf wat in het bestand staat.
        ControlPipeDialect d(p, "HOTDSD", path);
        std::string err;
        CHECK(d.open(err) && d.coins()[0] == 1);
        IoState c;
        c.sys.coin[0] = true;
        d.write(c);
        c.sys.coin[0] = false;
        d.write(c);
        CHECK(d.coins()[0] == 2 && static_cast<uint8_t>(slurp(path)[25]) == 2);
    }
    ControlPipeDialect d3(p, "HOTDSD", dir + "/bestaat-niet/control");
    std::string err;
    CHECK(!d3.open(err) && !err.empty());
}

// Dialect op een echt bestand: aanmaken, schrijven, neutraal, sluiten; overname van TeknoParrotUi's vlag-bytes.
void test_dialect_file(const std::string& dir) {
    TpShmProfiles ps = load_profiles();
    const std::string path = dir + "/shm", tpui = dir + "/tpui";
    {   // TeknoParrotUi's bestand met een verbind-vlag
        std::ofstream f(tpui, std::ios::binary);
        std::string z(64, '\0');
        z[0] = 1;
        z[5] = 7;
        z[30] = 9;      // buiten de overgenomen bytes en buiten het profiel: wordt niet overgenomen
        f.write(z.data(), 64);
    }
    TpShmDialect d(*ps.find("TransformersShadowsRising"), "TransformersShadowsRising", path, tpui);
    std::string err;
    CHECK(d.open(err) && err.empty());
    CHECK(fs::file_size(path) == 64);
    auto f0 = d.read_file();
    CHECK(f0[0] == 1 && f0[5] == 7 && f0[30] == 0);
    CHECK(control(f0) == 0 && f0[12] == 127);
    IoState s;
    s.player[0] = aim(0.0, 0.0);
    s.player[0].trigger = true;
    d.write(s);
    auto f1 = d.read_file();
    CHECK(control(f1) == 0x2u && f1[16] == 255 && f1[12] == 255);
    CHECK(d.stats().writes == 1 && d.stats().bytes_changed >= 3);
    // Iets wat de game schrijft op een byte buiten het profiel blijft staan en wordt als vreemd gemeld.
    {
        int fd = ::open(path.c_str(), O_RDWR);
        uint8_t v = 0x55;
        CHECK(::pwrite(fd, &v, 1, 40) == 1);
        ::close(fd);
    }
    d.write(s);
    CHECK(d.read_file()[40] == 0x55);
    CHECK(d.status_json().find("\"40=85\"") != std::string::npos);
    d.neutral();
    auto f2 = d.read_file();
    CHECK(control(f2) == 0 && f2[16] == 127 && f2[12] == 127);
    d.write(s);
    d.close();
    auto f3 = d.read_file();
    CHECK(control(f3) == 0 && f3[16] == 127);            // close() laat de invoer neutraal achter
    // Een bestaand bestand (na een herstart) wordt hergebruikt: inhoud buiten het profiel blijft.
    TpShmDialect d2(*ps.find("TransformersShadowsRising"), "TransformersShadowsRising", path, tpui);
    CHECK(d2.open(err) && d2.stats().existed);
    CHECK(d2.read_file()[40] == 0x55);
    // Onmogelijk pad: nette fout.
    TpShmDialect d3(*ps.find("TransformersShadowsRising"), "T", dir + "/bestaat-niet/shm", tpui);
    CHECK(!d3.open(err) && !err.empty());
}

bool wait_for(const std::function<bool()>& f, int ms = 3000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}

// De brug in de daemon: routekeuze uit de instellingen van de game. De brug leest de gebruikerslaag bij elke vraag opnieuw: een
// keuze in de app geldt zonder herstart van de daemon.
void test_user_routes(const std::string& dir) {
    SharedState sh;
    IoBridgeService::Config cfg;
    cfg.dirs.shipped = repo_patches();
    cfg.dirs.user = dir + "/ur-user";
    cfg.profiles = load_profiles();
    cfg.shm_path = dir + "/ur-shm";
    cfg.tpui_shm_path = dir + "/ur-geen-tpui";
    auto put_user = [&](const std::string& game, const std::string& text) { put(cfg.dirs.user + "/" + game + "/game.ini", text); };
    fs::remove_all(cfg.dirs.user);
    IoBridgeService br(sh, cfg);
    CHECK(br.route_for("TC5") == "tp-shm");                       // geen gebruikerslaag: de meegeleverde route
    put_user("TC5", "[Input]\nRoute = evdev\n");
    CHECK(br.route_for("TC5") == "evdev");                        // meteen, zonder herstart
    CHECK(br.start("TC5").empty() && !br.active());               // evdev: geen brug
    auto js = json::parse(br.status_json());
    CHECK(js.ok && js.value.find("routes")->find("TC5")->str == "evdev" && !js.value.find("user_routes_error"));
    put_user("JurassicPark", "[Input]\nRoute = tp-shm\n");
    CHECK(br.start("JurassicPark").empty() && br.active() && br.active_route() == "tp-shm");   // Layout uit de meegeleverde laag
    br.stop();
    // House of the Dead: Scarlet Dawn met de besturingspipe: het gedeelde geheugen en het rapport, en bij het stoppen beide weg.
    put_user("HOTDSD", "[Input]\nRoute = tp-shm\nLayout = HOTDSDPipe\nControlPipe = AllsUsbIo\n");
    CHECK(!br.start("HOTDSD").empty() && !br.active());           // de brug kent geen besturingspipes (cfg zonder control_pipes)
    br.shutdown();
    {
        IoBridgeService::Config c2 = cfg;
        c2.control_pipes = load_control_pipes();
        c2.control_pipe_path = dir + "/ur-control";
        IoBridgeService b2(sh, c2);
        CHECK(b2.start("HOTDSD").empty() && b2.active() && b2.active_route() == "tp-shm");
        CHECK(fs::file_size(c2.shm_path) == 64 && fs::file_size(c2.control_pipe_path) == 64);
        CHECK(static_cast<uint8_t>(slurp(c2.control_pipe_path)[0]) == 0x81);   // neutraal: aim in het midden
        auto js2 = json::parse(b2.status_json());
        const json::Value* det = js2.ok ? js2.value.find("detail") : nullptr;
        CHECK(det && det->find("layout")->str == "HOTDSDPipe" && det->find("control_pipe") && det->find("control_pipe")->find("pipe")->str == "TeknoParrotPipe");
        // Een andere route (zonder besturingspipe) ruimt het rapport op; stoppen ruimt alles op.
        CHECK(b2.start("JurassicPark").empty() && !fs::exists(c2.control_pipe_path));
        CHECK(b2.start("HOTDSD").empty() && fs::exists(c2.control_pipe_path));
        b2.stop();
        CHECK(!fs::exists(c2.control_pipe_path) && !fs::exists(c2.shm_path));
        put_user("HOTDSD", "[Input]\nRoute = tp-shm\nLayout = HOTDSDPipe\nControlPipe = Bestaatniet\n");
        CHECK(b2.start("HOTDSD").find("Bestaatniet") != std::string::npos && !b2.active());
        b2.shutdown();
    }
    fs::remove_all(cfg.dirs.user + "/HOTDSD");
    put_user("TC5", "[Input]\nRoute = onzin\n");
    CHECK(br.route_for("TC5") == "tp-shm");                       // ongeldig: genegeerd
    js = json::parse(br.status_json());
    CHECK(js.ok && js.value.find("user_routes_error") != nullptr);
    fs::remove_all(cfg.dirs.user);
    CHECK(br.route_for("TC5") == "tp-shm");
    br.shutdown();
}

void test_service(const std::string& dir) {
    SharedState sh;
    fs::create_directories(dir + "/svc");
    FakeGun* gun1 = nullptr;
    auto kb = std::make_unique<FakeKbd>();
    FakeKbd* kbp = kb.get();
    OutputService out([&](int p) -> std::unique_ptr<output::GunDevice> {
        auto g = std::make_unique<FakeGun>();
        if (p == 1) gun1 = g.get();
        return g;
    }, std::move(kb));
    (void)gun1;
    sh.output = &out;

    IoBridgeService::Config cfg;
    std::string err;
    cfg.dirs.shipped = repo_patches();
    cfg.profiles = load_profiles();
    const std::string sdir = dir + "/svc";
    cfg.shm_path = sdir + "/shm";
    cfg.tpui_shm_path = sdir + "/geen-tpui";
    cfg.state_path = sdir + "/iobridge.json";
    const std::string state = cfg.state_path;
    auto read_file = [&] {
        std::vector<uint8_t> v(64, 0);
        int fd = ::open((sdir + "/shm").c_str(), O_RDONLY);
        if (fd >= 0) { if (::pread(fd, v.data(), 64, 0) < 0) v.clear(); ::close(fd); }
        return v;
    };
    {
        IoBridgeService br(sh, cfg);
        sh.iobridge = &br;
        CHECK(br.route_for("TC5") == "tp-shm" && br.route_for("HOTDSD") == "evdev");
        // Zonder gebruikerslaag: alleen de meegeleverde instellingen (de gebruikerslaag volgt in test_user_routes).
        CHECK(!br.active());
        // Route evdev: niets te doen. Onbekende override: fout. tp-shm zonder layout: fout.
        CHECK(br.start("HOTDSD").empty() && !br.active());
        CHECK(!br.start("HOTDSD", "bluetooth").empty());
        CHECK(!br.start("OnbekendeGame", "tp-shm").empty());
        CHECK(!fs::exists(sdir + "/shm"));
        // Start tp-shm voor Transformers: bestand ontstaat neutraal, ook zonder gewapende uitvoer.
        CHECK(br.start("TransformersShadowsRising", "tp-shm").empty());   // de standaardroute is hid-board; tp-shm blijft kiesbaar
        CHECK(br.active() && br.active_route() == "tp-shm" && fs::exists(state));
        CHECK(wait_for([&] { return br.status_json().find("\"ticks\":0,") == std::string::npos; }));
        auto b = read_file();
        CHECK(control(b) == 0 && b[16] == 127);
        // Gewapend: aim en knoppen komen in het bestand. Een testpunt blijft staan tot een volgend patroon.
        CHECK(out.arm().ok);
        output::PatternParams pp;
        pp.kind = output::PatternKind::Point;
        pp.x = 0.0;
        pp.y = 1.0;
        CHECK(out.start_pattern(pp).ok);
        CHECK(out.button(output::GunButton::Trigger, "down").ok);
        CHECK(out.key(1, 6, "down").ok);      // toets 5 = munt van speler 1 (KEY_5 = 6)
        CHECK(out.key(1, 2, "down").ok);      // toets 1 = start van speler 1
        CHECK(wait_for([&] { auto v = read_file(); return control(v) == (0x1u | 0x2u | 0x40u); }));
        b = read_file();
        CHECK(b[16] == 255 && b[12] == 0);   // X links = 255, Y onder = 0 (complement)
        auto js = json::parse(br.status_json());
        CHECK(js.ok && js.value.find("active")->b && js.value.find("route")->str == "tp-shm");
        CHECK(js.value.find("detail")->find("layout")->str == "TransformersShadowsRising");
        CHECK(js.value.find("routes")->find("TC5")->str == "tp-shm");
        // Disarm: alles neutraal in het bestand.
        out.disarm();
        CHECK(wait_for([&] { auto v = read_file(); return control(v) == 0 && v[16] == 127; }));
        // Herwapenen en een knop vasthouden; dan "daemon-herstart": de brug verdwijnt (neutraal), een nieuwe neemt over.
        CHECK(out.arm().ok);
        CHECK(out.button(output::GunButton::Trigger, "down").ok);
        CHECK(wait_for([&] { auto v = read_file(); return (control(v) & 0x2u) != 0; }));
        br.shutdown();
        sh.iobridge = nullptr;
        CHECK(control(read_file()) == 0);                 // bij het afsluiten neutraal
        CHECK(fs::exists(state) && fs::exists(sdir + "/shm"));   // route en bestand blijven staan
    }
    {
        // Nieuwe brug (de herstarte daemon): geen unit-bewaking in de test, dus de route wordt zonder meer hervat.
        IoBridgeService br2(sh, cfg);
        sh.iobridge = &br2;
        CHECK(br2.resume());
        CHECK(br2.active() && br2.active_route() == "tp-shm");
        CHECK(wait_for([&] { auto v = read_file(); return (control(v) & 0x2u) != 0; }));   // de nog vastgehouden trekker is er weer
        CHECK(json::parse(br2.status_json()).value.find("resumes")->num == 1);
        CHECK(json::parse(br2.status_json()).value.find("detail")->find("file_existed")->b);
        // Expliciet stoppen: bestand en toestandsbestand weg, invoer neutraal.
        br2.stop();
        CHECK(!br2.active() && !fs::exists(state) && !fs::exists(sdir + "/shm"));
        CHECK(!br2.resume());
        // Route evdev stopt een actieve brug.
        CHECK(br2.start("TC5").empty() && br2.active());
        // TC5 leest zijn munt uit het toetsenbord: toets 5 (munt) houdt ook Shift+T vast, en laat ze samen los.
        CHECK(out.key(1, KEY_5, "down").ok);
        CHECK(wait_for([&] { return kbp->shift.load() && kbp->t.load(); }));
        CHECK(out.key(1, KEY_5, "up").ok);
        CHECK(wait_for([&] { return !kbp->shift.load() && !kbp->t.load(); }));
        CHECK(out.key(1, KEY_5, "down").ok);
        CHECK(wait_for([&] { return kbp->t.load(); }));
        CHECK(br2.start("HOTDSD").empty() && !br2.active());   // stoppen laat vastgehouden munttoetsen los
        CHECK(!kbp->shift.load() && !kbp->t.load());
        out.key(1, KEY_5, "up");
        // Een toestandsbestand met een route die niet te hervatten is wordt opgeruimd.
        { std::ofstream f(state); f << R"({"route":"unity-tcp","game":"TombRaider"})"; }
        CHECK(!br2.resume() && !fs::exists(state));
        sh.iobridge = nullptr;
    }
    {
        // Route hid-board: een nep-apparaat vangt de beelden op (geen /dev/uinput nodig). Transformers kan meer dan een bord hebben
        // (Boards = SaeGunBoard, SaeIoBoard in game.ini): de beelden van het geweerbord tellen hieronder.
        struct Rec {
            std::mutex m;
            std::vector<HidBoardFrame> frames;
            bool created = false, destroyed = false;
        };
        struct FakeBoard : HidBoardDevice {
            explicit FakeBoard(std::shared_ptr<Rec> r, bool gun) : rec(std::move(r)), gun(gun) {}
            bool create(const HidBoard&, std::string&) override { std::lock_guard<std::mutex> l(rec->m); rec->created = true; return true; }
            bool send(const HidBoardFrame& f) override { std::lock_guard<std::mutex> l(rec->m); if (gun) rec->frames.push_back(f); return true; }
            void destroy() override { std::lock_guard<std::mutex> l(rec->m); rec->created = false; rec->destroyed = true; }
            bool created() const override { std::lock_guard<std::mutex> l(rec->m); return rec->created; }
            std::shared_ptr<Rec> rec;
            bool gun;
        };
        auto rec = std::make_shared<Rec>();
        IoBridgeService::Config c3 = cfg;
        std::string e3;
        c3.boards = parse_hid_boards(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/hid-boards.json"), e3);
        CHECK(e3.empty() && c3.boards.find("SaeGunBoard"));
        c3.board_device = [rec](const HidBoard& b) { return std::make_unique<FakeBoard>(rec, b.name == "SaeGunBoard"); };
        IoBridgeService br3(sh, c3);
        sh.iobridge = &br3;
        CHECK(br3.start("TransformersShadowsRising").empty());
        CHECK(br3.active() && br3.active_route() == "hid-board" && fs::exists(state));
        { std::lock_guard<std::mutex> l(rec->m); CHECK(rec->created && !rec->frames.empty()); }   // het apparaat bestaat vóór de eerste tik
        auto last = [&] { std::lock_guard<std::mutex> l(rec->m); return rec->frames.empty() ? HidBoardFrame{} : rec->frames.back(); };
        CHECK(out.arm().ok);
        output::PatternParams pp;
        pp.kind = output::PatternKind::Point;
        pp.x = 0.0;
        pp.y = 1.0;
        CHECK(out.start_pattern(pp).ok);
        CHECK(out.button(output::GunButton::Trigger, "down").ok);
        CHECK(wait_for([&] { HidBoardFrame f = last(); return f.axis.size() == 4 && f.axis[0] == 557 && f.axis[1] == -217 && (f.buttons & (1u << 2)); }));   // X = p1.y, Y = p1.x, uitvoerbereik -217..557
        auto js3 = json::parse(br3.status_json());
        CHECK(js3.ok && js3.value.find("route")->str == "hid-board");
        {
            // Eén bord: het bord zelf; meer borden: "boards" met het geweerbord erin.
            const json::Value* det = js3.ok ? js3.value.find("detail") : nullptr;
            const json::Value* gun = det;
            if (det && det->find("boards"))
                for (const auto& b : det->find("boards")->arr)
                    if (b.find("board") && b.find("board")->str == "SaeGunBoard") gun = &b;
            CHECK(gun && gun->find("board") && gun->find("board")->str == "SaeGunBoard" && gun->find("created") && gun->find("created")->b);
        }
        out.disarm();   // neutraal: as in het midden van het uitvoerbereik, geen knoppen
        CHECK(wait_for([&] { HidBoardFrame f = last(); return f.axis.size() == 4 && f.axis[0] == 170 && f.axis[1] == 170 && f.buttons == 0; }));
        br3.stop();
        { std::lock_guard<std::mutex> l(rec->m); CHECK(rec->destroyed && !rec->created); }
        CHECK(!br3.active() && !fs::exists(state));
        // Een bord dat niet bestaat of een game zonder bord: nette fout.
        CHECK(!br3.start("JurassicPark", "hid-board").empty());
        sh.iobridge = nullptr;
    }
    {
        // Route jvs: de daemon is het JVS-bord aan een pty; de test speelt de game via de symlink.
        IoBridgeService::Config c4 = cfg;
        std::string e4;
        c4.jvs_boards = parse_jvs_boards(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/jvs-boards.json"), e4);
        CHECK(e4.empty() && c4.jvs_boards.find("LGS"));
        c4.jvs_link = sdir + "/jvs-com";
        c4.jvs_modem_file = sdir + "/modem";
        IoBridgeService br4(sh, c4);
        sh.iobridge = &br4;
        CHECK(br4.route_for("LGS") == "jvs" && br4.effective_routes().find("LGS")->board == "LGS" && br4.effective_routes().find("LGS")->com_port == "COM4");
        CHECK(br4.start("LGS").empty());
        CHECK(br4.active() && br4.active_route() == "jvs" && fs::is_symlink(c4.jvs_link));
        CHECK(!fs::exists(state));                          // geen herstart-toestand voor jvs (de game houdt de oude pty vast)
        int fd = ::open(c4.jvs_link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        CHECK(fd >= 0);
        auto ask = [&](const std::vector<uint8_t>& data, size_t want) {
            const std::vector<uint8_t> w = jvs_encode(data[0] >= 0xF0 ? 0xFF : 0x01, data);
            if (::write(fd, w.data(), w.size()) != static_cast<ssize_t>(w.size())) return std::vector<uint8_t>{};
            std::vector<uint8_t> r;
            for (int i = 0; i < 100 && r.size() < want; ++i) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 20) > 0) {
                    uint8_t b[64];
                    ssize_t n = ::read(fd, b, sizeof b);
                    if (n > 0) r.insert(r.end(), b, b + n);
                }
            }
            return r;
        };
        ask({0xF0, 0xD9}, 0);
        CHECK(ask({0xF1, 0x01}, 6).size() == 6);
        CHECK(out.arm().ok);
        output::PatternParams pp;
        pp.kind = output::PatternKind::Point;
        pp.x = 1.0;
        pp.y = 0.0;
        CHECK(out.start_pattern(pp).ok);
        CHECK(out.button(output::GunButton::Trigger, "down").ok);
        // Stuur uit de horizontale aim (helemaal rechts = 0xFFFF) en de trekker als rode knop (byte 1, bit 7).
        CHECK(wait_for([&] { auto r = ask({0x22, 0x01}, 8); return r.size() == 8 && r[5] == 0xFF && r[6] == 0xFF; }));
        CHECK(wait_for([&] { auto r = ask({0x20, 0x01, 0x03}, 10); return r.size() == 10 && (r[7] & 0x80); }));
        auto js4 = json::parse(br4.status_json());
        CHECK(js4.ok && js4.value.find("route")->str == "jvs" && js4.value.find("dialect")->str == "jvs");
        const json::Value* det = js4.value.find("detail");
        CHECK(det && det->find("addressed")->b && det->find("switch_polls")->num >= 1 && det->find("analog_polls")->num >= 1);
        // Layout = LGS in [Input]: de daemon schrijft ook het gedeelde geheugen (het stuur op offset 12, p1.wheel = aim-x zonder
        // gamepad). Gemeten: de game vraagt nooit 0x22, het stuur komt alleen hieruit.
        auto shm_byte = [&](size_t i) {
            std::ifstream f(c4.shm_path, std::ios::binary);
            std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return b.size() > i ? static_cast<uint8_t>(b[i]) : 0;
        };
        CHECK(wait_for([&] { return shm_byte(12) == 255; }));
        CHECK(det && det->find("shm") && det->find("shm")->find("layout")->str == "LGS");
        out.disarm();   // neutraal: stuur in het midden, geen knoppen
        CHECK(wait_for([&] { return shm_byte(12) == 128; }));
        CHECK(wait_for([&] { auto r = ask({0x22, 0x01}, 8); return r.size() == 8 && r[5] == 0x80 && r[6] == 0x00; }));
        CHECK(wait_for([&] { auto r = ask({0x20, 0x01, 0x03}, 10); return r.size() == 10 && r[7] == 0; }));
        if (fd >= 0) ::close(fd);
        br4.stop();
        CHECK(!br4.active() && !fs::exists(c4.jvs_link) && !fs::exists(c4.jvs_modem_file) && !fs::exists(c4.shm_path));
        CHECK(!br4.start("OnbekendeGame", "jvs").empty());   // geen [Input] in game.ini: nette fout
        sh.iobridge = nullptr;
    }
    {
        // Route tp-shm met de besturingspipe (HOTDSD): start (toets 1), munt (toets 5, telt bij het loslaten), trekker en aim komen in
        // het rapport; de trekker ook in het gedeelde geheugen. Dit is wat rg-direct naar \\.\pipe\TeknoParrotPipe stroomt.
        IoBridgeService::Config c5 = cfg;
        c5.dirs.user = sdir + "/user5";
        put(c5.dirs.user + "/HOTDSD/game.ini", "[Input]\nRoute = tp-shm\nLayout = HOTDSDPipe\nControlPipe = AllsUsbIo\n");
        c5.control_pipes = load_control_pipes();
        c5.control_pipe_path = sdir + "/control";
        IoBridgeService br5(sh, c5);
        sh.iobridge = &br5;
        CHECK(br5.start("HOTDSD").empty() && br5.active_route() == "tp-shm");
        auto rep = [&] {
            const std::string t = slurp(c5.control_pipe_path);
            return std::vector<uint8_t>(t.begin(), t.end());
        };
        CHECK(out.arm().ok);
        output::PatternParams pp;
        pp.kind = output::PatternKind::Point;
        pp.x = 1.0;
        pp.y = 0.0;
        CHECK(out.start_pattern(pp).ok);
        CHECK(out.key(1, KEY_1, "down").ok);
        CHECK(wait_for([&] { auto r = rep(); return r.size() == 64 && r[28] == 0x80 && word(r, 0) == 65534 && word(r, 4) == 1; }));
        CHECK(out.key(1, KEY_1, "up").ok);
        CHECK(out.key(1, KEY_5, "down").ok);
        CHECK(wait_for([&] { return br5.status_json().find("\"buttons\":\"coin\"") != std::string::npos; }));   // de brug zag de munt ingedrukt
        CHECK(out.key(1, KEY_5, "up").ok);
        CHECK(wait_for([&] { auto r = rep(); return r.size() == 64 && r[25] == 1 && r[28] == 0; }));
        CHECK(out.button(output::GunButton::Trigger, "down").ok);
        CHECK(wait_for([&] { auto r = rep(); auto m = read_file(); return r.size() == 64 && r[28] == 0x20 && (control(m) & 0x1u); }));
        out.disarm();
        CHECK(wait_for([&] { auto r = rep(); return r.size() == 64 && r[28] == 0 && r[25] == 1 && word(r, 0) == 32897; }));
        br5.stop();
        CHECK(!fs::exists(c5.control_pipe_path) && !fs::exists(c5.shm_path));
        sh.iobridge = nullptr;
    }
    // Zonder gewapende uitvoer is de toestand neutraal; de bron meldt dat.
    IoState s;
    CHECK(!out.io_snapshot(s) || out.armed());
    out.shutdown();
}

}  // namespace

int main() {
    const std::string dir = (fs::temp_directory_path() / ("rg_iobridge_" + std::to_string(::getpid()))).string();
    fs::create_directories(dir);
    test_model();
    test_layouts();
    test_scale_and_validation();
    test_offscreen_rule();
    test_routes(dir);
    test_dialect_file(dir);
    test_control_pipe(dir);
    test_service(dir);
    test_user_routes(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
