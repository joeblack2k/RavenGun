// Tests van de gamepad naast de lightgun (docs/gamepad.md): de opsomming uit /proc/bus/input/devices (met redenen en een tijdelijke
// by-id-map), PadReader, de indeling uit [Gamepad] (standaard, lagen, herkomst), de nieuwe signalen in de IO-brug (alle drie de
// bordsoorten), de live bronnen per toets, de IPC-parser, het bewaren in config.json en GamepadService met een FIFO als
// nep-apparaat (nooit een echte gamepad). Eigen CHECK-macro (actief bij NDEBUG), zoals de andere tests.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <linux/input.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "gamepad_service.hpp"
#include "output_service.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/gamesettings/gamepad_settings.hpp"
#include "ravengun/iobridge/hid_board.hpp"
#include "ravengun/iobridge/jvs.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/gamepad.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/sysinfo/gamepads.hpp"
#include "ravengun/sysinfo/mice.hpp"

using namespace ravengun;
using namespace ravengun::output;
namespace fs = std::filesystem;
using ravengund::GamepadServiceConfig;

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

static bool contains(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << text;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Opsomming

// Een Xbox 360-pad (xpad), de PS Move, onze eigen gun, libvirtualhid, een toetsenbord, de virtuele pad van de Steam-client (geen
// phys, geen uniq), een Bluetooth-pad (uniq) en de Steam Controller-puck zonder controller (alleen muis en toetsen).
static const char* kProc = R"PROC(I: Bus=0003 Vendor=045e Product=028e Version=0114
N: Name="Microsoft X-Box 360 pad"
P: Phys=usb-0000:00:14.0-2/input0
U: Uniq=
H: Handlers=event20 js0
B: EV=20000b
B: KEY=7cdb000000000000 0 0 0 0
B: ABS=3003f

I: Bus=0005 Vendor=054c Product=03d5 Version=0001
N: Name="Motion Controller"
P: Phys=1c:ce:51:98:5d:6b
U: Uniq=00:07:04:8c:70:ac
H: Handlers=event13 js1
B: EV=20001b
B: KEY=1f 0 0 0 0 0 0 ffff00000000 0 0 0 0
B: ABS=10000000039

I: Bus=0003 Vendor=1209 Product=7202 Version=0001
N: Name="RavenGun Virtual Lightgun"
P: Phys=
U: Uniq=
H: Handlers=event260 mouse8 js3
B: EV=b
B: KEY=1f0000 0 0 0 0
B: ABS=3

I: Bus=0003 Vendor=1209 Product=0003 Version=0001
N: Name="libvirtualhid Mouse (Absolute)"
P: Phys=
U: Uniq=
H: Handlers=event256 mouse5 js1
B: EV=b
B: KEY=1f0000 0 0 0 0
B: ABS=3

I: Bus=0003 Vendor=413c Product=2113 Version=0111
N: Name="Dell KB216 Wired Keyboard"
P: Phys=usb-0000:0c:00.0-3/input0
U: Uniq=
H: Handlers=sysrq kbd leds event17
B: EV=120013
B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe

I: Bus=0003 Vendor=28de Product=11ff Version=0001
N: Name="Microsoft X-Box 360 pad 0"
P: Phys=
U: Uniq=
H: Handlers=event40 js4
B: EV=20000b
B: KEY=7cdb000000000000 0 0 0 0
B: ABS=3003f

I: Bus=0005 Vendor=045e Product=0b13 Version=0509
N: Name="Xbox Wireless Controller"
P: Phys=aa:bb:cc:dd:ee:ff
U: Uniq=11:22:33:44:55:66
H: Handlers=event41 js5
B: EV=20001b
B: KEY=7fff000000000000 0 0 0 0
B: ABS=3063f

I: Bus=0003 Vendor=28de Product=1304 Version=0111
N: Name="Valve Software Steam Controller Puck"
P: Phys=usb-0000:0e:00.4-1/input2
U: Uniq=FXB9955202923
H: Handlers=sysrq kbd event7 mouse0
B: EV=100017
B: KEY=30000 0 0 e080ffdf01cfffff fffffffffffffffe
B: REL=1943
)PROC";

static void test_enumeration(const std::string& dir) {
    const std::string byid = dir + "/by-id";
    fs::create_directories(byid);
    fs::create_symlink("../event20", byid + "/usb-Microsoft_Controller_0001-event-joystick");
    fs::create_symlink("../js0", byid + "/usb-Microsoft_Controller_0001-joystick");
    std::vector<sysinfo::GamepadVerdict> v;
    auto pads = sysinfo::list_gamepads(kProc, byid, &v);
    CHECK(pads.size() == 3);
    CHECK(v.size() == 8);
    if (pads.size() == 3) {
        CHECK(pads[0].event == "event20" && pads[0].id == "usb-Microsoft_Controller_0001-event-joystick" && pads[0].by_id);
        CHECK(pads[1].event == "event40" && pads[1].id == "pad-Microsoft_X-Box_360_pad_0" && !pads[1].by_id);
        CHECK(pads[2].event == "event41" && pads[2].name == "Xbox Wireless Controller" && pads[2].uniq == "11:22:33:44:55:66");
        CHECK(pads[2].id.rfind("pad-Xbox_Wireless_Controller-", 0) == 0 && !contains(pads[2].id, "11:22"));   // geen adres in het id
        CHECK(sysinfo::mouse_id_valid(pads[2].id) && sysinfo::mouse_id_valid(pads[1].id));
        // Stabiel: zelfde naam en uniq geven hetzelfde id, een andere uniq een ander.
        CHECK(sysinfo::gamepad_fallback_id("Xbox Wireless Controller", "11:22:33:44:55:66", "x") == pads[2].id);
        CHECK(sysinfo::gamepad_fallback_id("Xbox Wireless Controller", "11:22:33:44:55:67", "x") != pads[2].id);
    }
    auto why = [&](const std::string& name) {
        for (const auto& x : v) if (x.name == name) return x.why;
        return std::string("?");
    };
    CHECK(contains(why("Motion Controller"), "PS Move"));
    CHECK(contains(why("RavenGun Virtual Lightgun"), "eigen virtueel apparaat"));
    CHECK(contains(why("libvirtualhid Mouse (Absolute)"), "libvirtualhid"));
    CHECK(contains(why("Dell KB216 Wired Keyboard"), "BTN_SOUTH"));
    CHECK(contains(why("Valve Software Steam Controller Puck"), "BTN_SOUTH"));
    CHECK(contains(why("Microsoft X-Box 360 pad"), "gamepad (id uit by-id)"));
    CHECK(contains(why("Xbox Wireless Controller"), "gamepad (id uit naam en uniq/phys)"));
    // Twee gelijke pads zonder uniq en phys: toch unieke ids.
    std::string two = std::string(kProc).substr(0, 0) +
                      "I: Bus=0003 Vendor=28de Product=11ff\nN: Name=\"Steam Virtual Gamepad\"\nH: Handlers=event50\nB: KEY=1000000000000 0 0 0 0\nB: ABS=3\n\n"
                      "I: Bus=0003 Vendor=28de Product=11ff\nN: Name=\"Steam Virtual Gamepad\"\nH: Handlers=event51\nB: KEY=1000000000000 0 0 0 0\nB: ABS=3\n";
    auto p2 = sysinfo::list_gamepads(two, dir + "/geen-by-id");
    CHECK(p2.size() == 2 && p2[0].id == "pad-Steam_Virtual_Gamepad" && p2[1].id == "pad-Steam_Virtual_Gamepad-2");
    // Bitmaps: BTN_SOUTH (304) staat in het vijfde woord van onderen.
    auto devs = sysinfo::parse_input_devices(kProc);
    CHECK(devs[0].has_key(BTN_SOUTH) && devs[0].has_key(BTN_START) && !devs[0].has_key(BTN_LEFT));
    CHECK(devs[0].has_abs(ABS_X) && devs[0].has_abs(ABS_RZ) && devs[0].has_abs(ABS_HAT0Y) && !devs[0].has_abs(ABS_GAS));
    CHECK(devs[0].uniq.empty() && devs[1].uniq == "00:07:04:8c:70:ac");
}

// ---------------------------------------------------------------------------------------------------------------------
// PadReader

static void test_reader() {
    PadReader r;
    r.set_range(ABS_X, {-32768, 32767});
    r.set_range(ABS_Z, {0, 1023});
    CHECK(near(r.state().value(PadControl::LeftX), 0.5) && near(r.state().value(PadControl::RT), 0));
    // Niets gebeurt tot SYN_REPORT.
    CHECK(!r.event(EV_ABS, ABS_X, 32767));
    CHECK(near(r.state().value(PadControl::LeftX), 0.5));
    CHECK(r.event(EV_SYN, SYN_REPORT, 0));
    CHECK(near(r.state().value(PadControl::LeftX), 1.0));
    CHECK(!r.event(EV_SYN, SYN_REPORT, 0));   // niets veranderd
    // Dode zone: 5 % uitslag telt als midden; daarbuiten herschaald.
    r.event(EV_ABS, ABS_X, static_cast<int32_t>(-0.05 * 32767));
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::LeftX), 0.5));
    r.event(EV_ABS, ABS_X, -32768);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::LeftX), 0.0));
    r.event(EV_ABS, ABS_X, static_cast<int32_t>(0.6 * 32767.5 - 0.5));   // 0,6 uitslag -> (0,6-0,08)/0,92 = 0,565
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::LeftX), 0.5 + 0.52 / 0.92 / 2.0, 0.002));
    // Y: boven = 0 (zonder bereik geldt -32768..32767).
    r.event(EV_ABS, ABS_Y, -32768);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::LeftY), 0.0));
    // Trigger met eigen bereik, en als knop vanaf 0,5.
    r.event(EV_ABS, ABS_Z, 511);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::LT), 511.0 / 1023.0) && !r.state().pressed(PadControl::LT));
    r.event(EV_ABS, ABS_Z, 600);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(r.state().pressed(PadControl::LT));
    // ABS_GAS (sommige Bluetooth-pads) is RT; BTN_TR2 (digitaal) ook.
    r.event(EV_ABS, ABS_GAS, 255);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::RT), 1.0));
    r.event(EV_ABS, ABS_GAS, 0);
    r.event(EV_KEY, BTN_TR2, 1);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(near(r.state().value(PadControl::RT), 1.0));
    // Knoppen en d-pad (hat en BTN_DPAD_*).
    r.event(EV_KEY, BTN_SOUTH, 1);
    r.event(EV_KEY, BTN_X, 1);
    r.event(EV_KEY, BTN_START, 2);   // herhaling = ingedrukt
    r.event(EV_ABS, ABS_HAT0X, -1);
    r.event(EV_KEY, BTN_DPAD_UP, 1);
    r.event(EV_SYN, SYN_REPORT, 0);
    const PadState& s = r.state();
    CHECK(s.pressed(PadControl::A) && s.pressed(PadControl::X) && !s.pressed(PadControl::Y) && s.pressed(PadControl::Start));
    CHECK(s.pressed(PadControl::DPadLeft) && !s.pressed(PadControl::DPadRight) && s.pressed(PadControl::DPadUp));
    // Stick als knop: vanaf de helft van de uitslag.
    CHECK(s.pressed(PadControl::LeftY) && s.pressed(PadControl::LeftX) && !s.pressed(PadControl::RightX));
    // SYN_DROPPED: alles los, ook wat ervoor kwam, tot de volgende SYN_REPORT.
    r.event(EV_SYN, SYN_DROPPED, 0);
    r.event(EV_KEY, BTN_EAST, 1);
    CHECK(r.event(EV_SYN, SYN_REPORT, 0));
    CHECK(!r.state().pressed(PadControl::A) && !r.state().pressed(PadControl::B) && near(r.state().value(PadControl::LeftX), 0.5));
    r.event(EV_KEY, BTN_EAST, 1);
    r.event(EV_SYN, SYN_REPORT, 0);
    CHECK(r.state().pressed(PadControl::B));
}

// ---------------------------------------------------------------------------------------------------------------------
// Indeling

static void test_map(const std::string& dir) {
    PadControl c;
    CHECK(pad_control_from_name("leftx", c) && c == PadControl::LeftX);
    CHECK(pad_control_from_name("DPadDown", c) && c == PadControl::DPadDown);
    CHECK(!pad_control_from_name("Z", c));
    PadTarget t;
    std::string err;
    CHECK(parse_pad_target("key:F2", t, err) && t.kind == PadTargetKind::Key && t.index == KEY_F2 && pad_target_name(t) == "key:f2");
    CHECK(parse_pad_target("BTN8", t, err) && t.kind == PadTargetKind::Btn && t.index == 7);
    CHECK(!parse_pad_target("btn9", t, err) && contains(err, "onbekend doel"));
    CHECK(!parse_pad_target("key:bestaatniet", t, err));
    CHECK(parse_pad_target("aux2", t, err) && t.kind == PadTargetKind::Gun && pad_target_name(t) == "aux2");

    // De standaardindeling (docs/gamepad.md).
    const GamepadMap d = default_gamepad_map();
    auto desc = describe_gamepad_map(d);
    std::string text;
    for (const auto& kv : desc) text += kv.first + "=" + kv.second + " ";
    CHECK(text == "A=trigger B=reload X=grenade Y=aux1 LB=btn1 RB=btn2 LT=brake RT=gas Start=start Back=coin LeftX=wheel ");

    // Per sleutel over de standaard; none haalt weg; fouten worden overgeslagen met een melding.
    std::vector<std::string> w;
    GamepadMap m = gamepad_map_from({{"A", "btn3"}, {"LT", "none"}, {"RightY", "brake"}, {"dpadup", "key:f2"}, {"Q", "gas"},
                                     {"B", "vliegen"}, {"X", "wheel"}},
                                    &w);
    CHECK(m.at(PadControl::A).kind == PadTargetKind::Btn && m.at(PadControl::LT).kind == PadTargetKind::None);
    CHECK(m.at(PadControl::RightY).kind == PadTargetKind::Brake && m.at(PadControl::DPadUp).kind == PadTargetKind::Key);
    CHECK(m.at(PadControl::B).kind == PadTargetKind::Gun && m.at(PadControl::X).kind == PadTargetKind::Gun);   // ongewijzigd
    CHECK(m.at(PadControl::LeftX).kind == PadTargetKind::Wheel);
    CHECK(w.size() == 3 && contains(w[0], "onbekende bediening") && contains(w[1], "onbekend doel") && contains(w[2], "geen stuur"));

    // Toepassen.
    PadState s;
    s.v[static_cast<size_t>(PadControl::A)] = 1;
    s.v[static_cast<size_t>(PadControl::LeftX)] = 0.2;
    s.v[static_cast<size_t>(PadControl::RT)] = 0.7;
    s.v[static_cast<size_t>(PadControl::LT)] = 0.4;
    s.v[static_cast<size_t>(PadControl::LB)] = 1;
    s.v[static_cast<size_t>(PadControl::Back)] = 1;
    PadOutput o = apply_gamepad_map(d, s);
    CHECK(o.gun[0] && !o.gun[1] && o.wheel_mapped && near(o.wheel, 0.2) && o.gas_mapped && near(o.gas, 0.7) && near(o.brake, 0.4));
    CHECK(o.btn[0] && !o.btn[1] && o.coin && !o.start && o.keys.empty());
    // Zonder stuur in de indeling: wheel_mapped false (dan stuurt de aim); een stick naar rem geeft zijn uitslag.
    GamepadMap m2 = gamepad_map_from({{"LeftX", "none"}, {"RightY", "brake"}, {"RT", "trigger"}, {"Y", "key:f2"}, {"X", "key:F2"}}, nullptr);
    s.v[static_cast<size_t>(PadControl::RightY)] = 0.1;
    s.v[static_cast<size_t>(PadControl::Y)] = 1;
    o = apply_gamepad_map(m2, s);
    CHECK(!o.wheel_mapped && near(o.brake, 0.8) && o.gun[0]);   // A en RT (0,7 >= 0,5) allebei trekker
    CHECK(o.keys.size() == 1 && o.keys[0].first == KEY_F2 && o.keys[0].second);   // twee bedieningen op F2: één toets, OR
    // Twee bedieningen op het stuur: de grootste uitslag.
    GamepadMap m3 = gamepad_map_from({{"RightX", "wheel"}}, nullptr);
    s.v[static_cast<size_t>(PadControl::RightX)] = 0.9;
    CHECK(near(apply_gamepad_map(m3, s).wheel, 0.9));   // LeftX 0,2 (uitslag 0,3) tegen RightX 0,9 (0,4)
    s.v[static_cast<size_t>(PadControl::RightX)] = 0.0;
    CHECK(near(apply_gamepad_map(m3, s).wheel, 0.0));

    // Lagen en herkomst: [Gamepad] in het meegeleverde game.ini en de gebruikerslaag (die wint).
    gamesettings::Dirs dirs{dir + "/patches", dir + "/user"};
    write_file(dirs.shipped + "/Safari/game.ini", "[Game]\nName = Safari\n\n[Gamepad]\nLeftX = wheel\nLB = btn5\nLT = none\nRS = btn9\n");
    write_file(dirs.user + "/Safari/game.ini", "[Gamepad]\nlb = btn6\nDPadUp = key:f3\n");
    write_file(dirs.shipped + "/Plain/game.ini", "[Game]\nName = Plain\n");
    auto gm = gamesettings::gamepad_mapping_for(dirs, "Safari");
    CHECK(gm.has_section && gm.warnings.size() == 1 && contains(gm.warnings[0], "RS"));
    std::map<std::string, std::pair<std::string, std::string>> rows;
    for (const auto& r : gm.rows) rows[r.control] = {r.target, r.source};
    CHECK(rows["LB"] == std::make_pair(std::string("btn6"), std::string("user")));
    CHECK(rows["DPadUp"] == std::make_pair(std::string("key:f3"), std::string("user")));
    CHECK(rows["LeftX"] == std::make_pair(std::string("wheel"), std::string("shipped")));
    CHECK(rows["LT"] == std::make_pair(std::string("none"), std::string("shipped")));
    CHECK(rows["A"] == std::make_pair(std::string("trigger"), std::string("default")));
    CHECK(!rows.count("RS") && !rows.count("Guide"));
    auto plain = gamesettings::gamepad_mapping_for(dirs, "Plain");
    CHECK(!plain.has_section && plain.map == default_gamepad_map() && plain.rows.size() == 11);
    for (const auto& r : plain.rows) CHECK(r.source == "default");
    CHECK(gamesettings::gamepad_mapping_for(dirs, "").map == default_gamepad_map());
}

// ---------------------------------------------------------------------------------------------------------------------
// IO-brug: signalen op alle drie de bordsoorten

static void test_signals() {
    using namespace ravengun::iobridge;
    for (const char* n : {"p1.wheel", "p4.gas", "p2.brake", "p1.x"}) CHECK(valid_axis_signal(n));
    for (const char* n : {"p1.btn1", "p3.btn8", "p1.trigger"}) CHECK(valid_button_signal(n));
    for (const char* n : {"p1.btn9", "p1.btn0", "p1.btn"}) CHECK(!valid_button_signal(n));
    CHECK(!valid_axis_signal("p1.btn1") && !valid_button_signal("p1.wheel"));

    IoState s;
    s.armed = true;
    // Zonder gamepad is het stuur de aim-x (richten is sturen), en alleen levend met een geldige aim.
    s.player[0].valid = true;
    s.player[0].x = 0.8;
    double v = 0;
    CHECK(io_axis_signal(s, "p1.wheel", v) && near(v, 0.8));
    CHECK(io_axis_live(s, "p1.wheel") && !io_axis_live(s, "p1.gas"));
    // Met een gamepad-stuur geldt de pad, ook zonder aim.
    s.player[1].pad = s.player[1].wheel_from_pad = true;
    s.player[1].wheel = 0.1;
    s.player[1].gas = 0.6;
    s.player[1].btn[7] = true;
    CHECK(io_axis_signal(s, "p2.wheel", v) && near(v, 0.1) && io_axis_live(s, "p2.wheel") && io_axis_live(s, "p2.gas"));
    CHECK(!io_axis_live(s, "p2.x"));
    bool b = false;
    CHECK(io_button_signal(s, "p2.btn8", b) && b && io_button_signal(s, "p2.btn1", b) && !b);

    // tp-shm: Let's Go Safari, het stuur op offset 12 (packaging/tp-shm-profiles.json).
    std::string err;
    TpShmProfiles ps = parse_tp_shm_profiles(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/tp-shm-profiles.json"), err);
    CHECK(err.empty() && ps.find("LGS") && ps.find("LGS")->axes.size() == 1 && ps.find("LGS")->axes[0].signal == "p1.wheel");
    if (const TpShmProfile* lgs = ps.find("LGS")) {
        uint8_t bytes[64] = {};
        IoState t;
        t.armed = true;
        t.player[0].pad = t.player[0].wheel_from_pad = true;   // alleen een gamepad, geen Move
        t.player[0].wheel = 0.0;
        tp_shm_render(*lgs, t, bytes, sizeof bytes);
        CHECK(bytes[12] == 0);
        t.player[0] = IoPlayer{};
        t.player[0].valid = true;   // alleen een Move: richten is sturen
        t.player[0].x = 1.0;
        tp_shm_render(*lgs, t, bytes, sizeof bytes);
        CHECK(bytes[12] == 255);
        tp_shm_render(*lgs, IoState{}, bytes, sizeof bytes);   // geen bron: midden
        CHECK(bytes[12] == 128);
    }
    // jvs: een analoog kanaal op gas (bv. Mario Kart), idle zonder gamepad.
    JvsAnalogChannel ch;
    ch.signal = "p1.gas";
    ch.idle = 0.0;
    IoState j;
    j.armed = true;
    CHECK(jvs_analog_value(ch, j) == 0);
    j.player[0].pad = true;
    j.player[0].gas = 1.0;
    CHECK(jvs_analog_value(ch, j) == 0xFFFF);
    j.armed = false;
    CHECK(jvs_analog_value(ch, j) == 0);
    // hid-board: rem op een as en btn2 op een knop.
    HidBoard hb;
    HidBoardAxis ax;
    ax.name = "Z";
    ax.signal = "p1.brake";
    ax.min = 0;
    ax.max = 1000;
    ax.idle = 0;
    hb.axes.push_back(ax);
    HidBoardButton bt;
    bt.index = 3;
    bt.signal = "p1.btn2";
    hb.buttons.push_back(bt);
    IoState h;
    h.armed = true;
    CHECK(hid_board_frame(hb, h).axis[0] == 0 && hid_board_frame(hb, h).buttons == 0);
    h.player[0].pad = true;
    h.player[0].brake = 0.25;
    h.player[0].btn[1] = true;
    CHECK(hid_board_frame(hb, h).axis[0] == 250 && hid_board_frame(hb, h).buttons == (1u << 3));
}

// ---------------------------------------------------------------------------------------------------------------------
// Live bronnen per toets

static void test_override_keys() {
    OverrideLayer ov;
    // De gamepad (bron 2) houdt toets 1 vast; een Move-rapport (bron 0) dat toets 1 los meldt wist dat niet.
    CHECK(ov.live_key(KEY_1, true, 2));
    CHECK(ov.live_key(KEY_1, false, 0));
    CHECK(ov.want_key(KEY_1));
    CHECK(!ov.live_key(KEY_1, false, 2));
    CHECK(!ov.want_key(KEY_1));
    // Synthetisch blijft OR.
    ov.hold_key(KEY_5);
    CHECK(ov.live_key(KEY_5, false, 2));
    ov.release_key(KEY_5);
    CHECK(!ov.want_key(KEY_5));
    // Gun-knoppen per bron (bestond al; nu met de derde bron).
    CHECK(ov.live_button(GunButton::Trigger, true, 2));
    CHECK(ov.live_button(GunButton::Trigger, false, 0));
    CHECK(!ov.live_button(GunButton::Trigger, false, 2));
    ov.live_key(KEY_1, true, 2);
    ov.clear();
    CHECK(!ov.want_key(KEY_1));
}

// ---------------------------------------------------------------------------------------------------------------------
// IPC en instellingen

static void test_ipc_and_settings(const std::string& dir) {
    auto req = [](const std::string& line) { return ipc::parse_request(line); };
    {
        auto r = req(R"({"v":1,"cmd":"pads"})");
        CHECK(std::holds_alternative<ipc::Request>(r) && std::get<ipc::Request>(r).cmd == ipc::Cmd::Pads);
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"pads","player":1})")));
    }
    {
        auto r = req(R"({"v":1,"cmd":"set_pad","player":2,"pad":"usb-Pad-event-joystick"})");
        CHECK(std::holds_alternative<ipc::Request>(r));
        if (auto* q = std::get_if<ipc::Request>(&r)) {
            CHECK(q->cmd == ipc::Cmd::SetPad && q->player == 2 && q->pad == std::string("usb-Pad-event-joystick"));
            const std::string again = ipc::make_request(*q);
            auto r2 = req(again);
            CHECK(std::holds_alternative<ipc::Request>(r2) && std::get<ipc::Request>(r2).pad == q->pad && std::get<ipc::Request>(r2).player == 2);
        }
        auto off = req(R"({"v":1,"cmd":"set_pad","player":1,"pad":""})");
        CHECK(std::holds_alternative<ipc::Request>(off) && std::get<ipc::Request>(off).pad == std::string());
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"set_pad","pad":"x"})")));                 // speler ontbreekt
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"set_pad","player":1})")));                // pad ontbreekt
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"set_pad","player":1,"pad":"/dev/input/event3"})")));
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"set_pad","player":5,"pad":"x"})")));
        CHECK(std::holds_alternative<ipc::Error>(req(R"({"v":1,"cmd":"set_pad","controller":"default","pad":"x"})")));
    }
    // config.json: bewaard en terug gelezen; ongeldige tekens geweigerd.
    config::Settings s;
    s.gamepads[0] = "usb-Microsoft_Controller_0001-event-joystick";
    s.gamepads[2] = "pad-Xbox_Wireless_Controller-1a2b3c4d";
    const std::string text = config::to_json(s);
    CHECK(contains(text, "\"gamepads\""));
    config::Settings back;
    std::string err;
    std::vector<std::string> warn;
    CHECK(config::parse_settings(text, back, err, &warn) && err.empty());
    CHECK(back.gamepads == s.gamepads);
    for (const auto& x : warn) CHECK(!contains(x, "gamepads"));
    std::string bad = text;
    bad.replace(bad.find("usb-Microsoft"), 3, "u/b");
    CHECK(!config::parse_settings(bad, back, err) && contains(err, "gamepads.1"));
    // Een bestand zonder "gamepads" (oudere daemon): geen toewijzingen.
    config::Settings none;
    CHECK(config::parse_settings(R"({"schema":2})", none, err) && none.gamepads[0].empty());
    (void)dir;
}

// ---------------------------------------------------------------------------------------------------------------------
// GamepadService met een FIFO

namespace {

struct FakeGun : GunDevice {
    bool exists = false;
    std::map<int, bool> btn;
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; }
    bool created() const override { return exists; }
    bool write_position(int, int) override { return exists; }
    bool write_button(GunButton b, bool d) override { btn[static_cast<int>(b)] = d; return exists; }
};
struct FakeKbd : KeyDevice {
    bool exists = false;
    std::mutex m;
    std::map<int, bool> keys;
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; }
    bool created() const override { return exists; }
    bool write_key(int code, bool down) override { std::lock_guard<std::mutex> g(m); keys[code] = down; return exists; }
    bool down(int code) { std::lock_guard<std::mutex> g(m); return keys.count(code) && keys[code]; }
};

bool wait_for(const std::function<bool()>& f, int ms = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}

void put(int fd, uint16_t type, uint16_t code, int32_t value) {
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    ssize_t n = ::write(fd, &ev, sizeof ev);
    (void)n;
}

json::Value parse_json(const std::string& s) {
    auto pr = json::parse(s);
    if (!pr.ok) std::fprintf(stderr, "ongeldige JSON: %s\n", s.c_str());
    return pr.value;
}

}  // namespace

static void test_service(const std::string& dir) {
    const std::string devdir = dir + "/dev-input";
    fs::create_directories(devdir);
    const std::string fifo = devdir + "/event20";
    CHECK(::mkfifo(fifo.c_str(), 0600) == 0);
    std::mutex lm;
    std::vector<sysinfo::Gamepad> list;
    sysinfo::Gamepad pad;
    pad.id = "fifo-pad";
    pad.name = "Neppad";
    pad.event = "event20";
    sysinfo::Gamepad gone;
    gone.id = "weg-pad";
    gone.name = "Verdwenen pad";
    gone.event = "event21";   // bestaat niet in devdir
    list.push_back(pad);
    GamepadServiceConfig::Assignments saved{};
    int saves = 0;
    gamesettings::Dirs dirs{dir + "/svc-patches", ""};
    write_file(dirs.shipped + "/Kart/game.ini", "[Gamepad]\nA = btn3\nRT = gas\nLeftX = none\nY = key:f2\n");

    // Writer eerst openen (O_RDWR): de service ziet dan nooit een einde van bestand.
    int wr = ::open(fifo.c_str(), O_RDWR | O_NONBLOCK);
    CHECK(wr >= 0);
    std::map<int, FakeGun*> guns;
    auto kbd_owned = std::make_unique<FakeKbd>();
    FakeKbd* kbd = kbd_owned.get();
    {
        ravengund::OutputService out([&](int p) { auto g = std::make_unique<FakeGun>(); guns[p] = g.get(); return g; }, std::move(kbd_owned));
        GamepadServiceConfig cfg;
        cfg.lister = [&](std::vector<sysinfo::GamepadVerdict>* v) {
            std::lock_guard<std::mutex> g(lm);
            if (v) for (const auto& p : list) v->push_back({p.name, p.event, 0, 0, true, "gamepad"});
            return list;
        };
        cfg.dev_dir = devdir;
        cfg.scan_ms = 100;
        cfg.dirs = dirs;
        cfg.save = [&](const GamepadServiceConfig::Assignments& a) { saved = a; ++saves; return std::string(); };
        ravengund::GamepadService svc(out, cfg);
        svc.start();
        CHECK(out.arm().ok);
        // Automatisch: precies één pad en speler 1 heeft er geen: hij hoort bij speler 1 en wordt geopend.
        CHECK(wait_for([&] {
            auto j = parse_json(svc.assignment_json(1));
            return j.find("pad") && j.find("pad")->is_string() && j.find("pad")->str == "fifo-pad" && j.find("auto")->b && j.find("connected")->b;
        }));
        // A = trekker (bron 2, zonder aim), Start = toets 1, linkerstick = stuur.
        put(wr, EV_KEY, BTN_SOUTH, 1);
        put(wr, EV_KEY, BTN_START, 1);
        put(wr, EV_ABS, ABS_X, 32767);
        put(wr, EV_SYN, SYN_REPORT, 0);
        ravengund::OutputService::AimSnapshot as;
        CHECK(wait_for([&] { return out.aim_snapshot(1, as) && as.trigger; }));
        CHECK(!as.have_position);   // de gamepad zet geen positie
        CHECK(wait_for([&] { return kbd->down(KEY_1); }));
        iobridge::IoState st;
        CHECK(wait_for([&] { return out.io_snapshot(st) && st.player[0].pad && st.player[0].wheel_from_pad && near(st.player[0].wheel, 1.0); }));
        CHECK(st.player[0].start && st.player[0].trigger && !st.player[1].pad);
        // Live weergave in pads.
        {
            auto j = parse_json(svc.pads_json());
            const json::Value* ps = j.find("pads");
            CHECK(ps && ps->arr.size() == 1);
            if (ps && ps->arr.size() == 1) {
                const json::Value& p = ps->arr[0];
                CHECK(p.find("player")->num == 1 && p.find("connected")->b);
                const json::Value* live = p.find("live");
                CHECK(live && live->is_object() && near(live->find("axes")->find("LeftX")->num, 1.0));
                std::set<std::string> pressed;
                for (const auto& b : live->find("buttons")->arr) pressed.insert(b.str);
                CHECK(pressed == std::set<std::string>({"A", "Start"}));
            }
            CHECK(j.find("game")->is_null() && j.find("mapping")->arr.size() == 11 && j.find("devices")->arr.size() == 1);
        }
        // Loslaten.
        put(wr, EV_KEY, BTN_SOUTH, 0);
        put(wr, EV_KEY, BTN_START, 0);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.aim_snapshot(1, as) && !as.trigger && !kbd->down(KEY_1); }));
        // Een game met [Gamepad]: A wordt btn3 (alleen IO-brug), Y = F2, het stuur valt terug op de aim.
        svc.set_game("Kart");
        CHECK(svc.wait_round());
        put(wr, EV_KEY, BTN_SOUTH, 1);
        put(wr, EV_KEY, BTN_Y, 1);
        put(wr, EV_ABS, ABS_RZ, 255);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.io_snapshot(st) && st.player[0].btn[2] && near(st.player[0].gas, 1.0); }));
        CHECK(!st.player[0].wheel_from_pad && !st.player[0].trigger);
        CHECK(wait_for([&] { return kbd->down(KEY_F2); }));
        CHECK(parse_json(svc.pads_json()).find("game")->str == "Kart");
        // Speler-toewijzing: onbekend id en ongeldige speler geweigerd; toewijzen aan speler 2 wordt bewaard en verhuist alles.
        std::string code, msg;
        CHECK(!svc.assign(2, "bestaat-niet", code, msg) && code == "invalid_argument" && contains(msg, "onbekende gamepad"));
        CHECK(!svc.assign(2, "/dev/input/event20", code, msg) && code == "invalid_argument");
        CHECK(!svc.assign(0, "fifo-pad", code, msg) && code == "invalid_argument");
        CHECK(saves == 0);
        CHECK(svc.assign(2, "fifo-pad", code, msg));
        CHECK(saves == 1 && saved[1] == "fifo-pad" && saved[0].empty());
        CHECK(wait_for([&] { return !kbd->down(KEY_F2) || out.io_snapshot(st); }));
        // Speler 1 laat alles los (geen automatische pad meer: die hangt nu aan speler 2); speler 2 heeft hem (zonder eigen gun).
        CHECK(wait_for([&] { return out.io_snapshot(st) && !st.player[0].pad && st.player[1].pad && st.player[1].btn[2]; }));
        CHECK(!st.player[0].btn[2] && near(st.player[0].gas, 0));
        {
            auto j = parse_json(svc.assignment_json(1));
            CHECK(j.find("pad")->is_null() && !j.find("auto")->b);
        }
        CHECK(!svc.assign(3, "fifo-pad", code, msg) && contains(msg, "speler 2"));
        // Pad verdwijnt uit de opsomming: alles van speler 2 los; de toewijzing blijft staan (niet verbonden).
        {
            std::lock_guard<std::mutex> g(lm);
            list = {gone};
        }
        CHECK(wait_for([&] { return out.io_snapshot(st) && !st.player[1].pad && !st.player[1].btn[2]; }));
        CHECK(wait_for([&] {
            auto j = parse_json(svc.assignment_json(2));
            return j.find("pad")->str == "fifo-pad" && !j.find("connected")->b && j.find("last_error")->is_string();
        }));
        // Een pad die bestaat in de opsomming maar niet in /dev: nette fout, geen crash.
        CHECK(svc.assign(1, "weg-pad", code, msg));
        CHECK(wait_for([&] {
            auto j = parse_json(svc.assignment_json(1));
            return j.find("last_error")->is_string() && contains(j.find("last_error")->str, "bestaat niet meer");
        }));
        CHECK(svc.assign(1, "", code, msg) && saved[0].empty());
        // Terug: de pad hangt weer aan speler 2 zodra hij er weer is.
        {
            std::lock_guard<std::mutex> g(lm);
            list = {pad};
        }
        CHECK(wait_for([&] { auto j = parse_json(svc.assignment_json(2)); return j.find("connected")->b; }));
        put(wr, EV_KEY, BTN_SOUTH, 1);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.io_snapshot(st) && st.player[1].btn[2]; }));
        svc.stop();
        CHECK(out.io_snapshot(st) && !st.player[1].pad && !st.player[1].btn[2]);
        CHECK(!svc.assign(2, "fifo-pad", code, msg) && code == "unavailable");
    }
    // Bewaarde toewijzing bij het starten (load), en open_devices = false (--no-hid): nooit iets openen.
    {
        auto kbd2 = std::make_unique<FakeKbd>();
        ravengund::OutputService out([&](int) { return std::make_unique<FakeGun>(); }, std::move(kbd2));
        GamepadServiceConfig cfg;
        cfg.lister = [&](std::vector<sysinfo::GamepadVerdict>*) { return std::vector<sysinfo::Gamepad>{pad}; };
        cfg.dev_dir = devdir;
        cfg.scan_ms = 100;
        cfg.load = [] { return GamepadServiceConfig::Assignments{"", "", "fifo-pad", "ongeldig/id"}; };
        cfg.open_devices = false;
        ravengund::GamepadService svc(out, cfg);
        svc.start();
        CHECK(svc.wait_round());
        auto j = parse_json(svc.status_json());
        CHECK(j.arr.size() == 4 && j.arr[2].find("pad")->str == "fifo-pad" && j.arr[3].find("pad")->is_null());
        CHECK(!j.arr[2].find("connected")->b && j.arr[0].find("pad")->is_null());   // speler 1: de pad hangt al aan speler 3
    }
    ::close(wr);
}

int main() {
    std::string dir = (fs::temp_directory_path() / ("rg-gamepad-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_enumeration(dir);
    test_reader();
    test_map(dir);
    test_signals();
    test_override_keys();
    test_ipc_and_settings(dir);
    test_service(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
