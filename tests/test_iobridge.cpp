// IO-brug: toestandsmodel, tp-shm-indeling per profiel (bytes exact), schaal en inversie, neutrale toestand, keuze van de
// route per game en dat een nieuwe brug (daemon-herstart) het bestand en de route weer overneemt. Zonder hardware: nep-guns.
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include "iobridge_service.hpp"
#include "output_service.hpp"
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

// De vijf profielen: bytes exact.
void test_layouts() {
    TpShmProfiles ps = load_profiles();
    for (const char* n : {"WartranTroopers", "RawThrillsGUN", "TimeCrisis5", "TransformersShadowsRising", "LGS"}) CHECK(ps.find(n));
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

void test_routes() {
    std::string err;
    IoRoutes r = parse_io_routes(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/io-routes.json"), err);
    CHECK(err.empty());
    CHECK(r.route_for("TransformersShadowsRising") == "tp-shm" && r.route_for("TC5") == "tp-shm");
    CHECK(r.route_for("Halo") == "evdev" && r.route_for("TombRaider") == "unity-tcp");
    CHECK(r.route_for("OnbekendeGame") == "evdev");     // standaardroute
    CHECK(r.find("TC5")->layout == "TimeCrisis5");
    // Elke layout in io-routes.json bestaat in tp-shm-profiles.json.
    TpShmProfiles ps = load_profiles();
    for (const auto& g : r.games)
        if (!g.layout.empty()) CHECK(ps.find(g.layout));
    parse_io_routes(R"({"games":{"A":{"route":"tp-shm"}}})", err);
    CHECK(!err.empty());                                 // tp-shm zonder layout
    parse_io_routes(R"({"games":{"A":{"route":"bluetooth"}}})", err);
    CHECK(!err.empty());
    CHECK(r.find("TC5")->coin_keys.size() == 2 && r.find("TC5")->coin_keys[0] == "lshift");
    parse_io_routes(R"({"games":{"A":{"route":"evdev","coin_keys":["bestaatniet"]}}})", err);
    CHECK(!err.empty());
    parse_io_routes(R"({"default":"tp-shm","games":{}})", err);
    CHECK(err.empty());
    IoRoutes d = parse_io_routes(R"({"default":"tp-shm","games":{}})", err);
    CHECK(d.route_for("x") == "tp-shm");
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

// De brug in de daemon: uitvoer (nep-guns) -> toestand -> bestand; routekeuze; herstart neemt het bestand over.
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
    cfg.routes = parse_io_routes(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/io-routes.json"), err);
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
        CHECK(br.route_for("TC5") == "tp-shm" && br.route_for("Halo") == "evdev");
        CHECK(!br.active());
        // Route evdev: niets te doen. Onbekende override: fout. tp-shm zonder layout: fout.
        CHECK(br.start("Halo").empty() && !br.active());
        CHECK(!br.start("Halo", "bluetooth").empty());
        CHECK(!br.start("OnbekendeGame", "tp-shm").empty());
        CHECK(!fs::exists(sdir + "/shm"));
        // Start tp-shm voor Transformers: bestand ontstaat neutraal, ook zonder gewapende uitvoer.
        CHECK(br.start("TransformersShadowsRising").empty());
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
        CHECK(br2.start("Halo").empty() && !br2.active());   // stoppen laat vastgehouden munttoetsen los
        CHECK(!kbp->shift.load() && !kbp->t.load());
        out.key(1, KEY_5, "up");
        // Een toestandsbestand met een route die niet te hervatten is wordt opgeruimd.
        { std::ofstream f(state); f << R"({"route":"unity-tcp","game":"TombRaider"})"; }
        CHECK(!br2.resume() && !fs::exists(state));
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
    test_routes();
    test_dialect_file(dir);
    test_service(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
