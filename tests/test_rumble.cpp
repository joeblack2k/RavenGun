// Tests van het trillen bij schieten (output/rumble.hpp): schotpuls bij de down-flank, einde na pulse_ms, herhaling bij
// vasthouden, uit bij loslaten/disarm/verbindingsverlies, pomppuls, uitgeschakeld = altijd 0, testpuls, en dat het aantal
// uitvoerrapporten begrensd blijft (alleen bij verandering plus de keepalive). Geen hardware, simulatietijd.
#include <cstdio>
#include <string>

#include "ravengun/ipc_protocol.hpp"
#include "ravengun/output/rumble.hpp"

using namespace ravengun;
using namespace ravengun::output;

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

static constexpr int64_t kMs = 1'000'000;
static RumbleInput in(bool armed, bool trig, bool pump = false, bool conn = true) {
    RumbleInput r;
    r.connected = conn; r.armed = armed; r.trigger = trig; r.pump = pump;
    return r;
}

static RumbleConfig cfg() {
    RumbleConfig c;  // standaard: 180, 60 ms, 9 Hz, pomp 90
    return c;
}

static void test_defaults_and_validation() {
    RumbleConfig c;
    CHECK(c.enabled && c.strength == 180 && c.pulse_ms == 60 && c.repeat_hz == 9 && c.pump_strength == 90);
    CHECK(validate_rumble(c).empty());
    c.strength = 256; CHECK(!validate_rumble(c).empty()); c.strength = 180;
    c.pulse_ms = 5; CHECK(!validate_rumble(c).empty()); c.pulse_ms = 501; CHECK(!validate_rumble(c).empty()); c.pulse_ms = 60;
    c.repeat_hz = 0.5; CHECK(!validate_rumble(c).empty()); c.repeat_hz = 31; CHECK(!validate_rumble(c).empty());
    c.repeat_hz = 0; CHECK(validate_rumble(c).empty());
    c.pump_strength = -1; CHECK(!validate_rumble(c).empty());
}

static void test_single_shot() {
    RumbleEngine e;
    e.set_config(cfg());
    CHECK(e.update(0, in(true, false)) == 0);
    CHECK(e.update(10 * kMs, in(true, true)) == 180);      // down-flank: puls
    CHECK(e.update(40 * kMs, in(true, true)) == 180);
    CHECK(e.update(69 * kMs, in(true, true)) == 180);
    CHECK(e.update(71 * kMs, in(true, true)) == 0);        // einde na pulse_ms (60 ms)
    CHECK(e.update(100 * kMs, in(true, true)) == 0);
    CHECK(e.pulses() == 1);
    // Een korte tik (loslaten voor het einde van de puls): de puls maakt zich af, daarna uit.
    RumbleEngine t;
    t.set_config(cfg());
    t.update(0, in(true, true));
    CHECK(t.update(20 * kMs, in(true, false)) == 180);
    CHECK(t.update(61 * kMs, in(true, false)) == 0);
    CHECK(t.pulses() == 1);
}

static void test_repeat_while_held() {
    RumbleEngine e;
    e.set_config(cfg());  // 9 Hz: periode 111,1 ms
    int starts = 0;
    uint8_t prev = 0;
    for (int ms = 0; ms <= 1000; ++ms) {
        uint8_t v = e.update(ms * kMs, in(true, ms >= 5));
        if (v > 0 && prev == 0) ++starts;
        prev = v;
    }
    CHECK(starts == 9 || starts == 10);                     // 995 ms ingedrukt: pulsen op 5, 116, ... 
    CHECK(e.pulses() == static_cast<uint64_t>(starts));
    // Loslaten stopt de herhaling.
    uint64_t before = e.pulses();
    for (int ms = 1001; ms < 1500; ++ms) e.update(ms * kMs, in(true, false));
    CHECK(e.pulses() == before && e.value() == 0);
    // repeat_hz = 0: ook bij lang vasthouden maar een puls.
    RumbleConfig c = cfg(); c.repeat_hz = 0;
    RumbleEngine one;
    one.set_config(c);
    for (int ms = 0; ms < 2000; ++ms) one.update(ms * kMs, in(true, true));
    CHECK(one.pulses() == 1);
    // Opnieuw indrukken geeft weer een puls.
    one.update(2000 * kMs, in(true, false));
    CHECK(one.update(2010 * kMs, in(true, true)) == 180 && one.pulses() == 2);
}

static void test_off_conditions() {
    RumbleEngine e;
    e.set_config(cfg());
    e.update(0, in(true, true));
    CHECK(e.update(10 * kMs, in(true, true)) == 180);
    CHECK(e.update(20 * kMs, in(false, true)) == 0);        // disarm: meteen uit
    CHECK(e.update(30 * kMs, in(false, true)) == 0);        // en geen nieuwe puls zolang ongewapend
    e.update(40 * kMs, in(false, false));
    CHECK(e.update(50 * kMs, in(true, false)) == 0);        // weer wapenen zonder trekker: stil
    // Wapenen terwijl de trekker al ingedrukt is, is geen schot.
    RumbleEngine h;
    h.set_config(cfg());
    h.update(0, in(false, true));
    CHECK(h.update(10 * kMs, in(true, true)) == 0 && h.pulses() == 0);
    // Verbindingsverlies midden in een puls.
    RumbleEngine l;
    l.set_config(cfg());
    l.update(0, in(true, true));
    CHECK(l.update(10 * kMs, in(true, true)) == 180);
    CHECK(l.update(20 * kMs, in(true, true, false, false)) == 0);
    CHECK(l.update(30 * kMs, in(true, true)) == 0);          // terug maar zonder nieuwe flank: stil
    // Uitgeschakeld: altijd 0, ook live midden in een puls.
    RumbleEngine d;
    d.set_config(cfg());
    d.update(0, in(true, true));
    CHECK(d.update(10 * kMs, in(true, true)) == 180);
    RumbleConfig off = cfg(); off.enabled = false;
    d.set_config(off);
    CHECK(d.value() == 0);
    for (int ms = 20; ms < 1000; ms += 5) CHECK(d.update(ms * kMs, in(true, (ms / 100) % 2 == 0, (ms / 50) % 2 == 0)) == 0);
    CHECK(!d.test(1000 * kMs, 100));
    CHECK(d.update(1010 * kMs, in(true, false)) == 0);
}

static void test_pump_pulse() {
    RumbleEngine e;
    e.set_config(cfg());
    e.update(0, in(true, false));
    CHECK(e.update(10 * kMs, in(true, false, true)) == 90);  // pomp: zwakker
    CHECK(e.update(39 * kMs, in(true, false, true)) == 90);
    CHECK(e.update(41 * kMs, in(true, false, true)) == 0);   // korter dan een schotpuls: pulse_ms / 2 = 30 ms
    CHECK(e.update(200 * kMs, in(true, false, true)) == 0);  // vasthouden geeft geen herhaling
    // pump_strength 0 = uit.
    RumbleConfig c = cfg(); c.pump_strength = 0;
    RumbleEngine z;
    z.set_config(c);
    z.update(0, in(true, false));
    CHECK(z.update(10 * kMs, in(true, false, true)) == 0 && z.pulses() == 0);
    // Pomp ongewapend: niets.
    RumbleEngine u;
    u.set_config(cfg());
    u.update(0, in(false, false));
    CHECK(u.update(10 * kMs, in(false, false, true)) == 0);
}

static void test_test_pulse() {
    RumbleEngine e;
    e.set_config(cfg());
    CHECK(e.update(0, in(false, false)) == 0);
    CHECK(e.test(10 * kMs, 100));
    CHECK(e.update(11 * kMs, in(false, false)) == 180);      // ook ongewapend
    CHECK(e.update(60 * kMs, in(false, false)) == 180);
    CHECK(e.update(111 * kMs, in(false, false)) == 0);       // na 100 ms
    // Verbindingsverlies breekt ook een testpuls af.
    CHECK(e.test(200 * kMs, 500));
    CHECK(e.update(210 * kMs, in(false, false)) == 180);
    CHECK(e.update(220 * kMs, in(false, false, false, false)) == 0);
    // Bij sterkte 0 toch voelbaar.
    RumbleConfig c = cfg(); c.strength = 0;
    RumbleEngine z;
    z.set_config(c);
    z.test(0, 50);
    CHECK(z.update(1 * kMs, in(false, false)) == 180);
}

static void test_live_config_change() {
    RumbleEngine e;
    e.set_config(cfg());
    e.update(0, in(true, true));
    RumbleConfig c = cfg(); c.strength = 255; c.pulse_ms = 100; c.repeat_hz = 0;
    e.set_config(c);
    e.update(5 * kMs, in(true, false));
    e.update(10 * kMs, in(true, true));
    CHECK(e.update(20 * kMs, in(true, true)) == 255);
    CHECK(e.update(105 * kMs, in(true, true)) == 255);
    CHECK(e.update(111 * kMs, in(true, true)) == 0);
}

// Simuleert de HID-lus: elke milliseconde een ronde, met de poort die bepaalt of er een rapport gaat. Telt rapporten.
struct Sim {
    RumbleEngine e;
    ReportGate gate;
    uint64_t reports = 0;
    int64_t next_keepalive = 0;
    int keepalive_ms = 1000;
    uint8_t on_air = 0;
    explicit Sim(RumbleConfig c = RumbleConfig{}) { e.set_config(c); }
    void step(int ms, const RumbleInput& r) {
        const int64_t now = ms * kMs;
        const uint8_t want = e.update(now, r);
        const bool ka = now >= next_keepalive;
        if (gate.due(now, want, ka)) {
            gate.sent(now, want, true);
            on_air = want;
            ++reports;
            if (ka) next_keepalive = now + static_cast<int64_t>(keepalive_ms) * kMs;
        }
    }
};

static void test_report_budget() {
    // Stil: alleen de keepalive (10 s = 10 rapporten), ook al draait de lus 1000 keer per seconde.
    Sim idle;
    for (int ms = 0; ms < 10000; ++ms) idle.step(ms, in(true, false));
    CHECK(idle.reports >= 10 && idle.reports <= 11);
    CHECK(idle.gate.rumble_reports() == 0);

    // Vasthouden 5 s op 9 Hz: per puls twee rapporten (aan en uit) plus de keepalive.
    Sim hold;
    for (int ms = 0; ms < 10000; ++ms) hold.step(ms, in(true, ms >= 2000 && ms < 7000));
    const uint64_t pulses = hold.e.pulses();
    CHECK(pulses >= 44 && pulses <= 46);
    CHECK(hold.gate.rumble_reports() <= 2 * pulses && hold.gate.rumble_reports() >= 2 * pulses - 1);
    CHECK(hold.reports <= 2 * pulses + 12);
    CHECK(hold.on_air == 0);                                // aan het eind staat de motor uit
    CHECK(hold.gate.last_sent() == 0);

    // Minimale tussentijd: ook bij een absurd snelle herhaling nooit meer dan 1 rapport per 20 ms.
    RumbleConfig fast;
    fast.pulse_ms = 10; fast.repeat_hz = 30;
    Sim burst(fast);
    for (int ms = 0; ms < 5000; ++ms) burst.step(ms, in(true, true));
    CHECK(burst.reports <= 5000 / 20 + 1);
    // Een mislukte poging wordt opnieuw geprobeerd, maar ook niet sneller dan de minimale tussentijd.
    ReportGate g;
    CHECK(g.due(0, 100, false));
    g.sent(0, 100, false);
    CHECK(!g.due(5 * kMs, 100, false) && g.due(25 * kMs, 100, false) && g.last_sent() == 0);
    g.sent(25 * kMs, 100, true);
    CHECK(g.last_sent() == 100 && !g.due(100 * kMs, 100, false) && g.due(100 * kMs, 100, true) && g.reports() == 1);
}

static void test_ipc_roundtrip() {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::SetRumble;
    rq.rumble_enabled = true; rq.rumble_strength = 200; rq.rumble_pulse_ms = 80; rq.rumble_repeat_hz = 0; rq.rumble_pump_strength = 40;
    auto pr = ipc::parse_request(ipc::make_request(rq));
    auto* back = std::get_if<ipc::Request>(&pr);
    CHECK(back && back->cmd == ipc::Cmd::SetRumble && *back->rumble_enabled && *back->rumble_strength == 200 &&
          *back->rumble_pulse_ms == 80 && *back->rumble_repeat_hz == 0 && *back->rumble_pump_strength == 40);
    ipc::Request t;
    t.cmd = ipc::Cmd::RumbleTest;
    t.rumble_ms = 250;
    pr = ipc::parse_request(ipc::make_request(t));
    back = std::get_if<ipc::Request>(&pr);
    CHECK(back && back->cmd == ipc::Cmd::RumbleTest && *back->rumble_ms == 250);
    // Zonder ms: standaardpuls (de daemon kiest).
    pr = ipc::parse_request(R"({"v":1,"cmd":"rumble_test"})");
    CHECK(std::get_if<ipc::Request>(&pr) && !std::get_if<ipc::Request>(&pr)->rumble_ms);
    // Bereiken en onbekende velden.
    for (const char* bad : {R"({"v":1,"cmd":"set_rumble"})", R"({"v":1,"cmd":"set_rumble","strength":256})",
                            R"({"v":1,"cmd":"set_rumble","strength":10.5})", R"({"v":1,"cmd":"set_rumble","pulse_ms":9})",
                            R"({"v":1,"cmd":"set_rumble","repeat_hz":31})", R"({"v":1,"cmd":"set_rumble","repeat_hz":0.5})",
                            R"({"v":1,"cmd":"set_rumble","pump_strength":-1})", R"({"v":1,"cmd":"set_rumble","enabled":1})",
                            R"({"v":1,"cmd":"set_rumble","volume":1})", R"({"v":1,"cmd":"rumble_test","ms":5})",
                            R"({"v":1,"cmd":"rumble_test","ms":1001})"})
        CHECK(std::get_if<ipc::Error>(&(pr = ipc::parse_request(bad))) != nullptr);
}

// Game-driven continuous rumble (RumbleInput::hold, from the game's motor output): hold x strength while armed and connected,
// 0 as soon as hold is 0, disarmed, disconnected or rumble off; a shot pulse on top takes the larger value.
static void test_game_hold() {
    RumbleEngine e;
    e.set_config(cfg());
    RumbleInput r = in(true, false);
    r.hold = 0.5;
    CHECK(e.update(0, r) == 90);
    r.hold = 1.0;
    CHECK(e.update(1 * kMs, r) == 180);
    r.hold = 2.0;                                       // clamped
    CHECK(e.update(2 * kMs, r) == 180);
    r.hold = 0.0;
    CHECK(e.update(3 * kMs, r) == 0);                   // the motor stopped: off at once
    r.hold = 0.125;
    CHECK(e.update(4 * kMs, r) == 23);
    r.armed = false;
    CHECK(e.update(5 * kMs, r) == 0);                   // disarmed
    r.armed = true; r.connected = false;
    CHECK(e.update(6 * kMs, r) == 0);                   // connection lost
    r.connected = true;
    RumbleConfig c = cfg(); c.strength = 255;           // the owner's strength scales it
    e.set_config(c);
    r.hold = 0.5;
    CHECK(e.update(7 * kMs, r) == 128);
    c.enabled = false;                                  // rumble off in the settings: no game rumble either
    e.set_config(c);
    CHECK(e.update(8 * kMs, r) == 0);
    c.enabled = true; c.strength = 180;
    e.set_config(c);
    r.hold = 0.25;
    e.test(9 * kMs, 60);                                // a solenoid pulse on top of a weak motor: the pulse wins
    CHECK(e.update(10 * kMs, r) == 180);
    CHECK(e.update(80 * kMs, r) == 45);                 // after the pulse: back to the motor
}

int main() {
    test_game_hold();
    test_defaults_and_validation();
    test_single_shot();
    test_repeat_while_held();
    test_off_conditions();
    test_pump_pulse();
    test_test_pulse();
    test_live_config_change();
    test_report_budget();
    test_ipc_roundtrip();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
