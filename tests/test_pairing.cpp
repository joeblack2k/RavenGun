// Koppelen zonder hardware: feature-rapporten (master-adres), de argumenten voor de hulp met rootrechten en de
// toestandsmachine van de wizard (src/ravengun/pairing/pairing.hpp).
#include <cstdio>
#include <string>
#include <vector>

#include "ravengun/pairing/pairing.hpp"

using namespace ravengun::pairing;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static const std::string kHost = "1c:ce:51:98:5d:6b";
static const std::string kMove = "00:07:04:8c:70:ad";

static void test_reports() {
    // Move: 0x05 met het adres omgekeerd (laagste byte eerst), 23 bytes.
    auto m = encode_move_set_master(kHost);
    CHECK(m.size() == 23 && m[0] == 0x05 && m[1] == 0x6b && m[2] == 0x5d && m[6] == 0x1c && m[7] == 0 && m[22] == 0);
    CHECK(encode_move_set_master("bla").empty() && encode_move_set_master("00:00:00:00:00:00").empty());
    // Lezen (0x04): eigen adres op 1..6, master op 10..15, beide omgekeerd.
    uint8_t g[16] = {0x04, 0xad, 0x70, 0x8c, 0x04, 0x07, 0x00, 0, 0, 0, 0x6b, 0x5d, 0x98, 0x51, 0xce, 0x1c};
    std::string own, master;
    CHECK(decode_move_addresses(g, sizeof g, own, master) && own == kMove && master == kHost);
    CHECK(!decode_move_addresses(g, 10, own, master));
    g[0] = 0x05;
    CHECK(!decode_move_addresses(g, sizeof g, own, master));
    // Navigation (BlueZ-sixaxis): 0xF5, 0x01, adres in leesvolgorde.
    auto n = encode_nav_set_master(kHost);
    CHECK(n.size() == 8 && n[0] == 0xF5 && n[1] == 0x01 && n[2] == 0x1c && n[7] == 0x6b);
    CHECK(decode_nav_master(n.data(), n.size(), master) && master == kHost);  // teruglezen geeft hetzelfde
    uint8_t f2[18] = {0xF2, 0, 0, 0, 0x00, 0x06, 0xf7, 0x0e, 0xc1, 0x93};
    CHECK(decode_nav_own(f2, sizeof f2, own) && own == "00:06:f7:0e:c1:93");
    CHECK(!decode_nav_own(f2, 5, own));
}

// De daemon geeft de hulp alleen vaste argumenten: een geldig adres (hoofdletters) en een type uit een vaste lijst.
static void test_helper_argv() {
    auto v = helper_argv("register", kMove, "move");
    CHECK(v.size() == 6 && v[0] == "/usr/bin/sudo" && v[1] == "-n" && v[2] == kBtHelperPath && v[3] == "register" && v[4] == "00:07:04:8C:70:AD" && v[5] == "move");
    CHECK(helper_argv("register", "00:06:F7:0E:C1:93", "navigation").size() == 6);
    CHECK(helper_argv("register", kMove, "move2").back() == "move2");
    v = helper_argv("unregister", kMove, "");
    CHECK(v.size() == 5 && v[3] == "unregister");
    for (const char* bad : {"", "00:07:04:8c:70", "00:07:04:8c:70:ad; rm -rf /", "00:07:04:8c:70:ad\n", "../../etc", "00:00:00:00:00:00",
                            "FF:FF:FF:FF:FF:FF", "0:7:4:8c:70:ad", "00-07-04-8c-70-ad", "$(id)", "00:07:04:8c:70:ag"})
        CHECK(helper_argv("register", bad, "move").empty());
    for (const char* bad : {"", "dualshock", "move;id", "MOVE", "navigation "})
        CHECK(helper_argv("register", kMove, bad).empty());
    CHECK(helper_argv("unregister", kMove, "move").empty());   // unregister heeft geen type
    CHECK(helper_argv("install", kMove, "move").empty());      // alleen register en unregister
    CHECK(helper_type("move", "ZCM1") == "move" && helper_type("move", "ZCM2") == "move2" && helper_type("navigation", "") == "navigation");
    CHECK(helper_type("dualshock", "").empty());
}

static Observation obs_with(const std::vector<UsbCandidate>& usb, bool helper = true) {
    Observation o;
    o.usb = usb;
    o.host = kHost;
    o.helper_installed = helper;
    return o;
}

static void test_new_move() {
    Machine m;
    int64_t t = 1'000'000'000;
    CHECK(m.state() == State::Idle && !m.active());
    m.start(t);
    CHECK(m.state() == State::WaitUsb && m.active());
    // Nog niets aangesloten, of het adres is nog niet gelezen: wachten.
    CHECK(m.step(t, obs_with({})).kind == Action::Kind::None && m.state() == State::WaitUsb);
    UsbCandidate c{"/dev/hidraw12", "move", "ZCM1", kMove, "11:22:33:44:55:66"};
    UsbCandidate c0 = c;
    c0.address.clear();
    CHECK(m.step(t, obs_with({c0})).kind == Action::Kind::None);
    // Aangesloten: master-adres zetten.
    Action a = m.step(t, obs_with({c}));
    CHECK(a.kind == Action::Kind::SetMaster && a.host == kHost && a.target.node == "/dev/hidraw12" && m.state() == State::SetMaster && m.busy());
    CHECK(m.step(t, obs_with({c})).kind == Action::Kind::None);  // een actie tegelijk
    m.done(t, true);
    CHECK(m.state() == State::Register && !m.busy());
    a = m.step(t, obs_with({c}));
    CHECK(a.kind == Action::Kind::Register && a.target.address == kMove);
    m.done(t, true);
    CHECK(m.state() == State::WaitUnplug);
    CHECK(m.step(t, obs_with({c})).kind == Action::Kind::None && m.state() == State::WaitUnplug);
    m.step(t, obs_with({}));
    CHECK(m.state() == State::WaitBluetooth && m.instruction().find("PS-knop") != std::string::npos);
    Observation bt = obs_with({});
    bt.bt_connected.insert(kMove);
    m.step(t, bt);
    CHECK(m.state() == State::Done && !m.active());
}

static void test_already_paired_and_confirm() {
    Machine m;
    int64_t t = 0;
    m.start(t);
    UsbCandidate c{"/dev/hidraw12", "move", "ZCM1", kMove, kHost};
    Observation o = obs_with({c});
    o.bluez_paired.insert(kMove);
    m.step(t, o);
    CHECK(m.state() == State::AlreadyPaired && m.instruction().find("opnieuw") != std::string::npos);
    // Opnieuw koppelen: het master-adres staat al goed, dus meteen registreren.
    CHECK(m.confirm(t) && m.state() == State::SetMaster);
    Action a = m.step(t, o);
    CHECK(a.kind == Action::Kind::Register && m.state() == State::Register);
    m.done(t, true);
    CHECK(m.state() == State::WaitUnplug);
    // Of: niet opnieuw, gewoon de kabel eruit en verbinden: dan is hij klaar.
    Machine k;
    k.start(t);
    k.step(t, o);
    Observation bt = obs_with({});
    bt.bluez_paired.insert(kMove);
    bt.bt_connected.insert(kMove);
    k.step(t, bt);
    CHECK(k.state() == State::Done);
    CHECK(!k.confirm(t));
}

static void test_helper_missing() {
    Machine m;
    int64_t t = 0;
    m.start(t);
    UsbCandidate c{"/dev/hidraw7", "navigation", "", "00:06:f7:0e:c1:93", "11:22:33:44:55:66"};
    Action a = m.step(t, obs_with({c}, false));
    CHECK(a.kind == Action::Kind::SetMaster);
    m.done(t, true);
    CHECK(m.step(t, obs_with({c}, false)).kind == Action::Kind::None && m.state() == State::NeedHelper);
    CHECK(m.instruction().find("beheer") != std::string::npos);
    // Na het installeren gaat het vanzelf verder (of met confirm).
    a = m.step(t, obs_with({c}, true));
    CHECK(a.kind == Action::Kind::Register && m.state() == State::Register);
    m.done(t, false, "bluetooth.service herstart niet");
    CHECK(m.state() == State::Failed && m.message().find("bluetooth.service") != std::string::npos);
}

static void test_failures_and_timeouts() {
    Timeouts to;
    to.wait_usb_ns = 1000;
    to.wait_bluetooth_ns = 1000;
    Machine m(to);
    m.start(0);
    m.step(2000, obs_with({}));
    CHECK(m.state() == State::Failed && m.message().find("USB") != std::string::npos);
    // Geen Bluetooth-adapter.
    m.start(0);
    Observation o = obs_with({{"/dev/hidraw1", "move", "ZCM1", kMove, ""}});
    o.host.clear();
    m.step(0, o);
    CHECK(m.state() == State::Failed && m.message().find("adapter") != std::string::npos);
    // Kabel los voordat het master-adres gezet is (opnieuw koppelen bevestigd, kabel al weg).
    m.start(0);
    UsbCandidate c{"/dev/hidraw1", "move", "ZCM1", kMove, kHost};
    Observation po = obs_with({c});
    po.bluez_paired.insert(kMove);
    m.step(0, po);
    CHECK(m.confirm(0) && m.state() == State::SetMaster);
    m.step(0, obs_with({}));
    CHECK(m.state() == State::Failed && m.message().find("kabel") != std::string::npos);
    // Na het master-adres mag de kabel eruit: de registratie bij BlueZ gebeurt op de pc.
    m.start(0);
    c.master.clear();
    m.step(0, obs_with({c}));
    m.done(0, true);
    CHECK(m.step(0, obs_with({})).kind == Action::Kind::Register);
    m.done(0, true);
    m.step(0, obs_with({}));
    CHECK(m.state() == State::WaitBluetooth);
    // Master zetten mislukt.
    m.start(0);
    m.step(0, obs_with({c}));
    m.done(0, false, "EPIPE");
    CHECK(m.state() == State::Failed && m.message().find("EPIPE") != std::string::npos);
    // Verbindt niet via Bluetooth (Navigation Controller met lege accu): duidelijke melding.
    m.start(0);
    UsbCandidate n{"/dev/hidraw2", "navigation", "", "00:06:f7:0e:c1:93", kHost};
    m.step(0, obs_with({n}));
    CHECK(m.state() == State::Register);
    m.step(0, obs_with({n}));
    m.done(0, true);
    m.step(0, obs_with({}));
    CHECK(m.state() == State::WaitBluetooth);
    m.step(5000, obs_with({}));
    CHECK(m.state() == State::Failed && m.message().find("accu") != std::string::npos);
    // Annuleren.
    m.start(0);
    m.cancel();
    CHECK(m.state() == State::Cancelled && !m.active());
    CHECK(std::string(to_string(State::WaitBluetooth)) == "wait_bluetooth");
}

int main() {
    test_reports();
    test_helper_argv();
    test_new_move();
    test_already_paired_and_confirm();
    test_helper_missing();
    test_failures_and_timeouts();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
