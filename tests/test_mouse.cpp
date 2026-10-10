// Tests van "Muis als lightgun": de parser van /proc/bus/input/devices (met een tijdelijke by-id-map), de toestandsmachine
// MouseGun, de IPC-parser van `mice` en `set_mouse_gun`, en MouseService met een FIFO als nep-apparaat (nooit een echte
// muis). Eigen CHECK-macro (actief bij NDEBUG), zoals de andere tests.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
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

#include "mouse_service.hpp"
#include "output_service.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/mouse_gun.hpp"
#include "ravengun/sysinfo/mice.hpp"

using namespace ravengun;
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

static bool contains(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------------------------------------------------------------
// Parser

static const char* kProc = R"(I: Bus=0011 Vendor=0001 Product=0001 Version=ab41
N: Name="AT Translated Set 2 keyboard"
P: Phys=isa0060/serio0/input0
S: Sysfs=/devices/platform/i8042/serio0/input/input3
U: Uniq=
H: Handlers=sysrq kbd event3 leds
B: PROP=0
B: EV=120013
B: KEY=402000000 3803078f800d001 feffffdfffefffff fffffffffffffffe
B: MSC=10
B: LED=7

I: Bus=0003 Vendor=046d Product=c52b Version=0111
N: Name="Logitech USB Receiver"
P: Phys=usb-0000:00:14.0-2/input0
S: Sysfs=/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/0003:046D:C52B.0001/input/input5
U: Uniq=
H: Handlers=sysrq kbd event4 leds
B: PROP=0
B: EV=120013
B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe

I: Bus=0003 Vendor=046d Product=c52b Version=0111
N: Name="Logitech USB Receiver Mouse"
P: Phys=usb-0000:00:14.0-2/input1
S: Sysfs=/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.1/0003:046D:C52B.0002/input/input6
U: Uniq=
H: Handlers=mouse1 event5
B: PROP=0
B: EV=17
B: KEY=ffff0000 0 0 0 0
B: REL=1943
B: MSC=10

I: Bus=0003 Vendor=056a Product=0374 Version=0110
N: Name="Wacom Intuos S Pen"
P: Phys=usb-0000:00:14.0-3/input0
S: Sysfs=/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3:1.0/input/input9
U: Uniq=
H: Handlers=mouse2 event6
B: PROP=1
B: EV=b
B: KEY=1c03 0 0 0 0 0 0 0 0
B: ABS=3000003

I: Bus=0003 Vendor=1209 Product=7202 Version=0001
N: Name="RavenGun Virtual Lightgun"
P: Phys=
S: Sysfs=/devices/virtual/input/input20
U: Uniq=
H: Handlers=mouse3 event7
B: PROP=0
B: EV=1b
B: KEY=1f0000 0 0 0 0
B: ABS=3

I: Bus=0019 Vendor=0000 Product=0005 Version=0000
N: Name="Lid Switch"
P: Phys=PNP0C0D/button/input0
S: Sysfs=/devices/LNXSYSTM:00/LNXSYBUS:00/PNP0C0D:00/input/input1
U: Uniq=
H: Handlers=event0
B: PROP=0
B: EV=21
B: SW=1

I: Bus=0005 Vendor=054c Product=0c5e Version=0001
N: Name="Naamloze muis zonder bitmaps"
P: Phys=
S: Sysfs=/devices/virtual/input/input30
U: Uniq=
H: Handlers=mouse4 event8
B: PROP=0
)";

static void test_parser(const std::string& dir) {
    auto devs = sysinfo::parse_input_devices(kProc);
    CHECK(devs.size() == 7);
    CHECK(devs[0].name == "AT Translated Set 2 keyboard" && devs[0].event_handler() == "event3" && devs[0].mouse_handler().empty());
    CHECK(devs[2].name == "Logitech USB Receiver Mouse" && devs[2].bus == 3 && devs[2].vendor == 0x046d && devs[2].product == 0xc52b);
    CHECK(devs[2].version == 0x0111 && devs[2].phys == "usb-0000:00:14.0-2/input1");
    CHECK(devs[2].event_handler() == "event5" && devs[2].mouse_handler() == "mouse1");
    CHECK(devs[2].has_rel_xy && !devs[2].has_abs_xy && devs[2].has_rel_bitmap && !devs[2].has_abs_bitmap);
    CHECK(devs[3].has_abs_xy && !devs[3].has_rel_xy);   // woord met 64 bits: de laagste bits staan achteraan
    CHECK(devs[4].vendor == 0x1209);
    CHECK(devs[5].handlers.size() == 1 && devs[5].event_handler() == "event0");
    CHECK(sysinfo::parse_input_devices("").empty());
    CHECK(sysinfo::parse_input_devices("onzin zonder dubbele punt\n\n\nH: Handlers=\n").empty());
    {
        // Een I:-regel direct na het vorige blok (geen lege regel) start toch een nieuw apparaat; CRLF en rommel overleven.
        auto d2 = sysinfo::parse_input_devices("I: Bus=0003 Vendor=0001 Product=0002 Version=0003\r\nN: Name=\"A\"\r\nH: Handlers=mouse0 event0\r\n"
                                                "I: Bus=0003 Vendor=0004 Product=0005 Version=0006\nN: Name=\"B\"\nH: Handlers=mouse9x event1 event12345678\n");
        CHECK(d2.size() == 2 && d2[0].name == "A" && d2[0].event_handler() == "event0");
        CHECK(d2[1].mouse_handler().empty() && d2[1].event_handler() == "event1");   // "mouse9x" en een te lang nummer tellen niet
    }

    // By-id-map met symlinks.
    const std::string byid = dir + "/by-id";
    fs::create_directories(byid);
    fs::create_symlink("../event5", byid + "/usb-Logitech_USB_Receiver-event-mouse");
    fs::create_symlink("../mouse1", byid + "/usb-Logitech_USB_Receiver-mouse");
    fs::create_symlink("../event4", byid + "/usb-Logitech_USB_Receiver-if01-event-kbd");
    fs::create_symlink("../event7", byid + "/usb-RavenGun_Virtual_Lightgun-event-mouse");
    fs::create_symlink("../event6", byid + "/naam met spatie");   // ongeldig id: valt terug op eventN
    {
        std::FILE* f = std::fopen((byid + "/gewoon-bestand").c_str(), "w");   // geen symlink: negeren
        if (f) std::fclose(f);
    }
    auto mice = sysinfo::list_mice(kProc, byid);
    CHECK(mice.size() == 3);
    if (mice.size() == 3) {
        CHECK(mice[0].id == "usb-Logitech_USB_Receiver-event-mouse" && mice[0].event == "event5");
        CHECK(mice[0].name == "Logitech USB Receiver Mouse" && !mice[0].absolute);
        CHECK(mice[0].vendor == 0x046d && mice[0].product == 0xc52b && mice[0].bus == 3);
        CHECK(mice[1].id == "event6" && mice[1].event == "event6" && mice[1].absolute && mice[1].name == "Wacom Intuos S Pen");
        CHECK(mice[2].id == "event8" && !mice[2].absolute);   // zonder bitmaps: niet absoluut
    }
    for (const auto& m : mice) CHECK(m.event != "event3" && m.event != "event4" && m.event != "event7" && m.event != "event0");
    // Zonder by-id-map: alles eventN.
    auto plain = sysinfo::list_mice(kProc, dir + "/bestaat-niet");
    CHECK(plain.size() == 3 && plain[0].id == "event5" && plain[1].id == "event6" && plain[2].id == "event8");
    // Twee symlinks naar hetzelfde event: de "-event-mouse" wint.
    fs::create_symlink("../event5", byid + "/usb-AAA-andere-naam");
    CHECK(sysinfo::list_mice(kProc, byid)[0].id == "usb-Logitech_USB_Receiver-event-mouse");

    CHECK(sysinfo::mouse_id_valid("usb-Logitech_USB_Receiver-event-mouse") && sysinfo::mouse_id_valid("event12"));
    CHECK(!sysinfo::mouse_id_valid("") && !sysinfo::mouse_id_valid("../event5") && !sysinfo::mouse_id_valid("/dev/input/event5"));
    CHECK(!sysinfo::mouse_id_valid("a b") && !sysinfo::mouse_id_valid(std::string(129, 'a')) && sysinfo::mouse_id_valid(std::string(128, 'a')));
    CHECK(!sysinfo::mouse_id_valid(std::string("a\0b", 3)) && !sysinfo::mouse_id_valid("a\n"));
}

// ---------------------------------------------------------------------------------------------------------------------
// MouseGun

static std::optional<output::LiveAction> sync(output::MouseGun& g) { return g.event(EV_SYN, SYN_REPORT, 0); }

static void test_mouse_gun() {
    using output::GunButton;
    output::MouseGun g;
    CHECK(near(g.x(), 0.5) && near(g.y(), 0.5));
    {
        auto s = g.snapshot();
        CHECK(s.move && s.buttons && near(s.x, 0.5) && near(s.y, 0.5) && !s.trigger && !s.reload && !s.grenade && !s.aux1 && !s.aux2 && !s.release_all);
    }
    // Relatief: 1920 counts = een schermbreedte; Y met dezelfde fysieke schaal (16/9).
    CHECK(!g.event(EV_REL, REL_X, 192));
    CHECK(near(g.x(), 0.6));   // de toestand loopt mee, maar er komt pas een actie bij SYN_REPORT
    auto a = sync(g);
    CHECK(a && a->move && a->buttons && near(a->x, 0.6) && near(a->y, 0.5) && !a->release_all);
    CHECK(!sync(g));            // geen wijziging, geen actie
    g.event(EV_REL, REL_Y, 108);
    a = sync(g);
    CHECK(a && near(a->x, 0.6) && near(a->y, 0.6, 1e-9));
    g.event(EV_REL, REL_X, -96);
    g.event(EV_REL, REL_Y, -216);
    a = sync(g);
    CHECK(a && near(a->x, 0.55) && near(a->y, 0.4, 1e-9));
    // Clamp.
    g.event(EV_REL, REL_X, 100000);
    g.event(EV_REL, REL_Y, 100000);
    a = sync(g);
    CHECK(a && near(a->x, 1.0) && near(a->y, 1.0));
    g.event(EV_REL, REL_X, -100000);
    g.event(EV_REL, REL_Y, -100000);
    a = sync(g);
    CHECK(a && near(a->x, 0.0) && near(a->y, 0.0));
    // Een wiel, een onbekende toets en een ander type doen niets.
    g.event(EV_REL, REL_WHEEL, 1);
    g.event(EV_KEY, BTN_TOUCH, 1);
    g.event(EV_MSC, MSC_SCAN, 7);
    g.event(EV_ABS, ABS_X, 100);    // zonder bereik genegeerd
    CHECK(!sync(g));
    CHECK(near(g.x(), 0.0));
    // Een andere gevoeligheid: 960 counts per breedte.
    output::MouseGunConfig c;
    c.counts_per_screen = 960;
    g.configure(c);
    CHECK(near(g.x(), 0.0));        // positie blijft
    g.event(EV_REL, REL_X, 96);
    a = sync(g);
    CHECK(a && near(a->x, 0.1));
    // Een andere beeldverhouding (4:3): Y-schaal 4/3 per count.
    c.screen_aspect = 4.0 / 3.0;
    g.configure(c);
    g.event(EV_REL, REL_Y, 96);
    a = sync(g);
    CHECK(a && near(a->y, 96 * (4.0 / 3.0) / 960.0));
    // Buiten bereik wordt gelimiteerd (100..20000).
    c.counts_per_screen = 5;
    g.configure(c);
    CHECK(near(g.config().counts_per_screen, output::kMouseCountsMin));
    c.counts_per_screen = 1e9;
    g.configure(c);
    CHECK(near(g.config().counts_per_screen, output::kMouseCountsMax));

    // Knoppen: links = trekker, rechts = herladen, midden = granaat, side = aux1, extra = aux2.
    output::MouseGun b;
    struct Map { uint16_t code; bool output::LiveAction::*member; GunButton gb; };
    const Map map[] = {{BTN_LEFT, &output::LiveAction::trigger, GunButton::Trigger},
                       {BTN_RIGHT, &output::LiveAction::reload, GunButton::Reload},
                       {BTN_MIDDLE, &output::LiveAction::grenade, GunButton::Grenade},
                       {BTN_SIDE, &output::LiveAction::aux1, GunButton::Aux1},
                       {BTN_EXTRA, &output::LiveAction::aux2, GunButton::Aux2}};
    for (const auto& m : map) {
        b.event(EV_KEY, m.code, 1);
        auto d = sync(b);
        CHECK(d && d->buttons && (*d).*(m.member) && b.button(m.gb));
        CHECK(!b.event(EV_KEY, m.code, 2) && !sync(b));    // autorepeat verandert niets
        b.event(EV_KEY, m.code, 0);
        d = sync(b);
        CHECK(d && !((*d).*(m.member)) && !b.button(m.gb));
    }
    // Twee knoppen tegelijk, in één frame samen met een beweging.
    b.event(EV_KEY, BTN_LEFT, 1);
    b.event(EV_KEY, BTN_RIGHT, 1);
    b.event(EV_REL, REL_X, 192);
    auto d = sync(b);
    CHECK(d && d->trigger && d->reload && !d->grenade && near(d->x, 0.6));

    // release(): alles los, positie blijft, daarna geen actie zonder wijziging.
    auto r = b.release();
    CHECK(r.release_all && !r.move && !r.buttons);
    CHECK(!b.button(GunButton::Trigger) && !b.button(GunButton::Reload) && near(b.x(), 0.6));
    CHECK(!sync(b));
    b.event(EV_KEY, BTN_LEFT, 1);
    CHECK(sync(b) && b.button(GunButton::Trigger));

    // SYN_DROPPED: alles tot de volgende SYN_REPORT negeren en de knoppen loslaten.
    output::MouseGun dr;
    dr.event(EV_KEY, BTN_LEFT, 1);
    sync(dr);
    CHECK(dr.button(GunButton::Trigger));
    dr.event(EV_SYN, SYN_DROPPED, 0);
    dr.event(EV_REL, REL_X, 500);
    dr.event(EV_KEY, BTN_RIGHT, 1);
    CHECK(near(dr.x(), 0.5) && !dr.button(GunButton::Reload));
    d = sync(dr);
    CHECK(d && !d->trigger && !d->reload && near(d->x, 0.5));
    dr.event(EV_REL, REL_X, 192);   // daarna werkt alles weer
    d = sync(dr);
    CHECK(d && near(d->x, 0.6));

    // Absoluut: genormaliseerd met min/max; clamp; ABS zonder bereik of een andere as negeren.
    output::MouseGunConfig ac;
    ac.abs_x = {0, 1000};
    ac.abs_y = {100, 300};
    output::MouseGun ab(ac);
    ab.event(EV_ABS, ABS_X, 250);
    ab.event(EV_ABS, ABS_Y, 200);
    auto e = sync(ab);
    CHECK(e && e->move && near(e->x, 0.25) && near(e->y, 0.5));
    ab.event(EV_ABS, ABS_X, 5000);
    ab.event(EV_ABS, ABS_Y, -50);
    e = sync(ab);
    CHECK(e && near(e->x, 1.0) && near(e->y, 0.0));
    ab.event(EV_ABS, ABS_PRESSURE, 5);
    CHECK(!sync(ab));
    ab.event(EV_ABS, ABS_X, 0);
    e = sync(ab);
    CHECK(e && near(e->x, 0.0));
    output::MouseGunConfig bad;
    bad.abs_x = {10, 10};   // ongeldig bereik
    output::MouseGun nb(bad);
    nb.event(EV_ABS, ABS_X, 10);
    CHECK(!sync(nb) && near(nb.x(), 0.5));
}

// ---------------------------------------------------------------------------------------------------------------------
// IPC-parser

static ipc::Request must_parse(const std::string& line, bool* ok = nullptr) {
    auto pr = ipc::parse_request(line);
    if (auto* r = std::get_if<ipc::Request>(&pr)) { if (ok) *ok = true; return *r; }
    if (ok) *ok = false;
    std::fprintf(stderr, "onverwachte fout bij %s: %s\n", line.c_str(), std::get<ipc::Error>(pr).message.c_str());
    return {};
}

static std::string parse_code(const std::string& line) {
    auto pr = ipc::parse_request(line);
    if (auto* e = std::get_if<ipc::Error>(&pr)) return e->code;
    return "";
}

static void test_ipc_parser() {
    bool ok = false;
    auto r = must_parse(R"({"v":1,"cmd":"mice"})", &ok);
    CHECK(ok && r.cmd == ipc::Cmd::Mice);
    CHECK(parse_code(R"({"v":1,"cmd":"mice","extra":1})") == "bad_request");
    CHECK(parse_code(R"({"v":1,"cmd":"mice","player":1})") == "bad_request");

    r = must_parse(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"usb-Logitech_USB_Receiver-event-mouse","counts_per_screen":1600,"id":7})", &ok);
    CHECK(ok && r.cmd == ipc::Cmd::SetMouseGun && r.player == 2 && r.mouse && *r.mouse == "usb-Logitech_USB_Receiver-event-mouse");
    CHECK(r.counts_per_screen && *r.counts_per_screen == 1600 && r.id_json == "7" && r.controller.empty());
    r = must_parse(R"({"v":1,"cmd":"set_mouse_gun","player":4,"mouse":""})", &ok);
    CHECK(ok && r.player == 4 && r.mouse && r.mouse->empty() && !r.counts_per_screen);
    must_parse(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":100})", &ok);
    CHECK(ok);
    must_parse(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":20000})", &ok);
    CHECK(ok);
    // make_request <-> parse_request.
    {
        ipc::Request q;
        q.cmd = ipc::Cmd::SetMouseGun;
        q.player = 3;
        q.mouse = "event9";
        q.counts_per_screen = 2400;
        auto back = must_parse(ipc::make_request(q), &ok);
        CHECK(ok && back.cmd == ipc::Cmd::SetMouseGun && back.player == 3 && *back.mouse == "event9" && *back.counts_per_screen == 2400);
        ipc::Request m;
        m.cmd = ipc::Cmd::Mice;
        CHECK(ipc::make_request(m) == R"({"v":1,"cmd":"mice"})");
    }

    // Ongeldige speler: weg, buiten 1..4, geen geheel getal, geen getal.
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","mouse":"event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":0,"mouse":"event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":5,"mouse":"event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1.5,"mouse":"event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":"1","mouse":"event5"})") == "invalid_argument");
    // Onbekend veld en geen `controller`.
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","pad":"/dev/input/event5"})") == "bad_request");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","controller":"default","mouse":"event5"})") == "bad_request");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"controller":"default","mouse":"event5"})") == "bad_request");
    // `mouse` ontbreekt of is geen tekst; te lang; ongeldige tekens (pad, spatie, stuurteken).
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":5})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":null})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":")" + std::string(129, 'a') + R"("})") == "invalid_argument");
    must_parse(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":")" + std::string(128, 'a') + R"("})", &ok);
    CHECK(ok);
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"/dev/input/event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"../event5"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"a b"})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"a\nb"})") == "invalid_argument");
    // counts_per_screen: buiten bereik, geen geheel getal, geen getal.
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":99})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":20001})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":1920.5})") == "invalid_argument");
    CHECK(parse_code(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":"event5","counts_per_screen":"veel"})") == "invalid_argument");
    // `player` blijft bij andere commando's zoals het was.
    CHECK(parse_code(R"({"v":1,"cmd":"arm","player":1})") == "bad_request");
    must_parse(R"({"v":1,"cmd":"status","player":2})", &ok);
    CHECK(ok);
}

// ---------------------------------------------------------------------------------------------------------------------
// MouseService met een FIFO als nep-apparaat

namespace {

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
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; }
    bool created() const override { return exists; }
    bool write_key(int, bool) override { return exists; }
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

// Knoppen die de gun van speler `p` nu ingedrukt heeft (uit de status; werkt ook als de speler niet meer "actief" is).
size_t buttons_down(ravengund::OutputService& out, int p) {
    auto j = parse_json(out.status_json());
    return j.find("players")->arr[static_cast<size_t>(p - 1)].find("buttons_down")->arr.size();
}

}  // namespace

static void test_service(const std::string& dir) {
    const std::string devdir = dir + "/dev-input";
    fs::create_directories(devdir);
    const std::string fifo = devdir + "/event7";
    CHECK(::mkfifo(fifo.c_str(), 0600) == 0);

    std::mutex lm;
    std::vector<sysinfo::Mouse> list;
    {
        sysinfo::Mouse m;
        m.id = "fifo-muis";
        m.name = "Nepmuis";
        m.event = "event7";
        list.push_back(m);
        sysinfo::Mouse w;
        w.id = "weg-muis";
        w.name = "Verdwenen muis";
        w.event = "event8";   // bestaat niet in devdir
        list.push_back(w);
        sysinfo::Mouse x;
        x.id = "pad-muis";
        x.name = "Ongeldige eventnaam";
        x.event = "../../etc/passwd";
        list.push_back(x);
    }
    std::map<int, FakeGun*> guns;
    auto kbd = std::make_unique<FakeKbd>();
    {
        ravengund::OutputService out([&](int p) { auto g = std::make_unique<FakeGun>(); guns[p] = g.get(); return g; }, std::move(kbd));
        ravengund::MouseServiceConfig cfg;
        cfg.lister = [&] { std::lock_guard<std::mutex> g(lm); return list; };
        cfg.dev_dir = devdir;
        cfg.retry_ms = 100;
        ravengund::MouseService svc(out, cfg);
        svc.start();
        CHECK(out.arm().ok);
        // Speler 2 heeft nog niets: zijn gun bestaat niet.
        CHECK(guns.size() == 4 && !guns[2]->exists);

        // Weigeringen: onbekend id, ongeldig pad, ongeldige speler of gevoeligheid; er verandert niets.
        std::string code, msg;
        CHECK(!svc.assign(2, "bestaat-niet", std::nullopt, code, msg) && code == "invalid_argument" && contains(msg, "onbekende muis"));
        CHECK(!svc.assign(2, "/dev/input/event7", std::nullopt, code, msg) && code == "invalid_argument");
        CHECK(!svc.assign(0, "fifo-muis", std::nullopt, code, msg) && code == "invalid_argument");
        CHECK(!svc.assign(5, "fifo-muis", std::nullopt, code, msg) && code == "invalid_argument");
        CHECK(!svc.assign(2, "fifo-muis", 50, code, msg) && code == "invalid_argument");
        CHECK(out.mouse_players() == 0 && !guns[2]->exists);

        // Writer eerst openen (O_RDWR): de service opent de FIFO dan zonder dat hij ooit een einde van bestand ziet.
        int wr = ::open(fifo.c_str(), O_RDWR | O_NONBLOCK);
        CHECK(wr >= 0);
        CHECK(svc.assign(2, "fifo-muis", 1000, code, msg));
        CHECK(out.mouse_players() == 0b0010 && guns[2]->exists && !guns[3]->exists);   // de gun van speler 2 bestaat nu
        {
            auto j = parse_json(svc.assignment_json(2));
            CHECK(j.find("player")->num == 2 && j.find("mouse")->str == "fifo-muis" && j.find("connected")->b);
            CHECK(j.find("last_error")->is_null() && j.find("counts_per_screen")->num == 1000);
        }
        // Dezelfde muis voor een andere speler: geweigerd.
        CHECK(!svc.assign(3, "fifo-muis", std::nullopt, code, msg) && code == "invalid_argument" && contains(msg, "speler 2"));

        // Events: +250 counts van 1000 per breedte = 0,75; trekker erbij.
        output::GunButton trig = output::GunButton::Trigger;
        (void)trig;
        ravengund::OutputService::AimSnapshot s;
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && s.have_position && std::fabs(s.u - 0.5) < 0.001; }));   // beginstand: midden
        put(wr, EV_REL, REL_X, 250);
        put(wr, EV_SYN, SYN_REPORT, 0);
        put(wr, EV_KEY, BTN_LEFT, 1);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && std::fabs(s.u - 0.75) < 0.001 && s.trigger; }));
        CHECK(std::fabs(s.v - 0.5) < 0.001 && !s.reload);
        // Speler 1 en 3 blijven ongemoeid.
        ravengund::OutputService::AimSnapshot s1;
        CHECK(out.aim_snapshot(1, s1) && !s1.have_position && !s1.trigger);

        // Status.
        {
            auto j = parse_json(svc.mice_json());
            CHECK(j.find("mice")->arr.size() == 3 && j.find("assignments")->arr.size() == 4);
            const auto& m0 = j.find("mice")->arr[0];
            CHECK(m0.find("id")->str == "fifo-muis" && m0.find("name")->str == "Nepmuis" && !m0.find("absolute")->b && m0.find("event")->str == "event7");
            const auto& a2 = j.find("assignments")->arr[1];
            CHECK(a2.find("player")->num == 2 && a2.find("mouse")->str == "fifo-muis" && a2.find("connected")->b);
            const auto& a1 = j.find("assignments")->arr[0];
            CHECK(a1.find("mouse")->is_null() && !a1.find("connected")->b && a1.find("last_error")->is_null());
            auto st = parse_json(svc.status_json());
            CHECK(st.arr.size() == 4);
        }

        // Verlies van het apparaat (de schrijver verdwijnt): alles los, foutmelding, de toewijzing blijft staan.
        ::close(wr);
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && !s.trigger; }));
        CHECK(wait_for([&] {
            auto j = parse_json(svc.assignment_json(2));
            return !j.find("connected")->b && j.find("mouse")->str == "fifo-muis" && j.find("last_error")->is_string() && !j.find("last_error")->str.empty();
        }));
        // Elke 100 ms opnieuw proberen: met een nieuwe schrijver is hij er weer, en de positie is behouden.
        wr = ::open(fifo.c_str(), O_RDWR | O_NONBLOCK);
        CHECK(wait_for([&] { return parse_json(svc.assignment_json(2)).find("connected")->b; }));
        CHECK(out.aim_snapshot(2, s) && std::fabs(s.u - 0.75) < 0.001);
        put(wr, EV_REL, REL_X, -500);
        put(wr, EV_KEY, BTN_RIGHT, 1);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && std::fabs(s.u - 0.25) < 0.001 && s.reload; }));
        // Een half event (drie bytes) en de rest eraan vast: wordt netjes samengevoegd.
        {
            input_event ev{};
            ev.type = EV_REL;
            ev.code = REL_X;
            ev.value = 100;
            ssize_t n1 = ::write(wr, &ev, 3);
            (void)n1;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            ssize_t n2 = ::write(wr, reinterpret_cast<const char*>(&ev) + 3, sizeof ev - 3);
            (void)n2;
            put(wr, EV_SYN, SYN_REPORT, 0);
            CHECK(wait_for([&] { return out.aim_snapshot(2, s) && std::fabs(s.u - 0.35) < 0.001; }));
        }

        // Gevoeligheid wijzigen (zelfde muis): 100 counts per breedte -> +50 = +0,5.
        CHECK(svc.assign(2, "fifo-muis", 100, code, msg));
        CHECK(wait_for([&] { return parse_json(svc.assignment_json(2)).find("connected")->b; }));
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && !s.reload; }));   // opnieuw geopend: knoppen los
        put(wr, EV_REL, REL_X, -20);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && std::fabs(s.u - 0.15) < 0.001; }));

        // Afmelden: knoppen los, toewijzing weg; het gun-masker valt weg (het apparaat blijft tot het afsluiten).
        put(wr, EV_KEY, BTN_LEFT, 1);
        put(wr, EV_SYN, SYN_REPORT, 0);
        CHECK(wait_for([&] { return out.aim_snapshot(2, s) && s.trigger; }));
        CHECK(svc.assign(2, "", std::nullopt, code, msg));
        CHECK(wait_for([&] { return buttons_down(out, 2) == 0; }));
        {
            auto j = parse_json(svc.assignment_json(2));
            CHECK(j.find("mouse")->is_null() && !j.find("connected")->b && j.find("last_error")->is_null());
        }
        CHECK(out.mouse_players() == 0);
        put(wr, EV_KEY, BTN_LEFT, 1);   // events na het afmelden doen niets meer
        put(wr, EV_SYN, SYN_REPORT, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(buttons_down(out, 2) == 0);

        // Een muis die op dat moment niet bestaat: duidelijke fout, toewijzing blijft, nieuwe pogingen lopen.
        CHECK(svc.assign(3, "weg-muis", std::nullopt, code, msg));   // staat in de lijst; het apparaat ontbreekt
        {
            auto j = parse_json(svc.assignment_json(3));
            CHECK(!j.find("connected")->b && j.find("mouse")->str == "weg-muis" && j.find("last_error")->is_string());
            CHECK(contains(j.find("last_error")->str, "bestaat niet meer") && j.find("counts_per_screen")->num == 1920);
        }
        CHECK(out.mouse_players() == 0b0100);
        // Een eventnaam met padtekens wordt nooit geopend.
        CHECK(svc.assign(4, "pad-muis", std::nullopt, code, msg));
        CHECK(contains(parse_json(svc.assignment_json(4)).find("last_error")->str, "ongeldig apparaat"));
        // Het apparaat verschijnt alsnog (zelfde id, andere eventnaam): de volgende poging slaagt.
        {
            int wr2 = -1;
            const std::string f2 = devdir + "/event9";
            CHECK(::mkfifo(f2.c_str(), 0600) == 0);
            wr2 = ::open(f2.c_str(), O_RDWR | O_NONBLOCK);
            { std::lock_guard<std::mutex> g(lm); for (auto& m : list) if (m.id == "weg-muis") m.event = "event9"; }
            CHECK(wait_for([&] { return parse_json(svc.assignment_json(3)).find("connected")->b; }));
            CHECK(parse_json(svc.assignment_json(3)).find("last_error")->is_null());
            put(wr2, EV_KEY, BTN_MIDDLE, 1);
            put(wr2, EV_SYN, SYN_REPORT, 0);
            ravengund::OutputService::AimSnapshot s3;
            CHECK(wait_for([&] { return out.aim_snapshot(3, s3) && s3.have_position; }));
            CHECK(wait_for([&] { return contains(out.status_json(), "\"grenade\""); }));
            // Afsluiten van de service geeft alles vrij (hier: de knoppen van speler 3).
            svc.stop();
            CHECK(out.aim_snapshot(3, s3));
            const std::string st = out.status_json();
            CHECK(!contains(st, "\"grenade\""));
            ::close(wr2);
        }
        CHECK(!svc.assign(2, "fifo-muis", std::nullopt, code, msg) && code == "unavailable");
        ::close(wr);
    }
}

int main() {
    std::string dir = (fs::temp_directory_path() / ("rg-mouse-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_parser(dir);
    test_mouse_gun();
    test_ipc_parser();
    test_service(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
