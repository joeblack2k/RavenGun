// Unittests zonder extern framework: eigen CHECK-macro (blijft actief bij NDEBUG), uitgevoerd via CTest.
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "ravengun/doctor.hpp"
#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"

using namespace ravengun;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                            \
    } while (0)

static bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

// Een volledig gezonde momentopname; tests passen hier iets aan.
static Snapshot healthy() {
    Snapshot s;
    s.video.push_back({"/dev/video0", "gspca main driver", "ov534", UsbId{0x1415, 0x2000}, true});
    s.hid.push_back({"/dev/hidraw9", "Motion Controller", 3, kMoveZcm1, true});
    s.rfkill.push_back({"hci0", "bluetooth", false, false});
    s.bt_adapters = {"hci0"};
    s.bluetoothctl_available = true;
    s.bluez_version = "5.87";
    s.input_conf_readable = true;
    s.bt_paired.push_back({"00:11:22:33:44:55", "Motion Controller"});
    s.bt_known = s.bt_paired;
    s.uinput_exists = s.uinput_accessible = true;
    s.psmoveapi_found = true;
    s.psmoveapi_prefix = "/opt/psmoveapi";
    return s;
}

static const Check* find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v) if (c.id == id) return &c;
    return nullptr;
}

static void test_json() {
    CHECK(json::escape("a\"b\\c\n\t") == "a\\\"b\\\\c\\n\\t");
    CHECK(json::escape(std::string("x\x01y")) == "x\\u0001y");
    CHECK(json::escape("é") == "é");  // UTF-8 blijft ongewijzigd
    json::Writer w;
    w.begin_object().key("a").value(1).key("b").begin_array().value("x").value(true).null().end_array()
        .key("c").begin_object().key("d").value("e").end_object().end_object();
    CHECK(w.str() == R"({"a":1,"b":["x",true,null],"c":{"d":"e"}})");
}

static void test_parse() {
    const std::string bt = "\x1b[1;30mDevice AA:BB:CC:DD:EE:01 Motion Controller\x1b[0m\r\n"
                           "Device AA:BB:CC:DD:EE:02 Xbox Wireless Controller\n"
                           "Device AA:BB:CC:DD:EE:03 AA-BB-CC-DD-EE-03\n"
                           "ruis zonder apparaat\n"
                           "Device ZZ:BB:CC:DD:EE:04 Ongeldig\n";
    auto d = parse_bluetoothctl_devices(bt);
    CHECK(d.size() == 3);
    CHECK(d[0].address == "AA:BB:CC:DD:EE:01" && d[0].name == "Motion Controller");
    CHECK(d[1].name == "Xbox Wireless Controller");
    CHECK(looks_like_move_name(d[0].name) && !looks_like_move_name(d[1].name));
    CHECK(parse_bluetoothctl_devices("").empty());

    auto kv = parse_keyvalue("DRIVER=sony\nHID_ID=0003:0000054C:000003D5\nzonder\nHID_UNIQ=\n");
    CHECK(kv.at("DRIVER") == "sony");
    CHECK(kv.at("HID_UNIQ").empty());
    CHECK(kv.count("zonder") == 0);

    auto h = parse_hid_id("0003:0000054C:000003D5");
    CHECK(h && h->bus == 3 && h->id == kMoveZcm1);
    CHECK(is_move(h->id) && !is_ps3eye(h->id));
    auto h2 = parse_hid_id("0005:0000054C:00000C5E");
    CHECK(h2 && h2->bus == 5 && h2->id == kMoveZcm2);
    CHECK(!parse_hid_id("onzin") && !parse_hid_id("0003:zz:01"));

    CHECK(parse_hex16("054c\n") == uint16_t{0x054c});
    CHECK(parse_hex16("1415") == uint16_t{0x1415});
    CHECK(!parse_hex16("") && !parse_hex16("12345") && !parse_hex16("xyz"));

    CHECK(parse_classic_bonded_only("[General]\n") == std::nullopt);
    CHECK(parse_classic_bonded_only("[General]\n#ClassicBondedOnly=false\n") == std::nullopt);
    CHECK(parse_classic_bonded_only("[General]\nClassicBondedOnly=false\n") == std::optional<bool>(false));
    CHECK(parse_classic_bonded_only("[General]\n ClassicBondedOnly = True \n") == std::optional<bool>(true));
    CHECK(parse_classic_bonded_only("; ClassicBondedOnly=true\nClassicBondedOnly=0\n") == std::optional<bool>(false));

    CHECK(parse_bluez_version("bluetoothctl: 5.87\n") == std::optional<std::string>("5.87"));
    CHECK(parse_bluez_version("5.86") == std::optional<std::string>("5.86"));
    CHECK(!parse_bluez_version("") && !parse_bluez_version("bluetoothctl: x.y"));

    CHECK(redact_mac("AA:BB:CC:DD:EE:FF") == "xx:xx:xx:xx:xx:FF");
    CHECK(redact_mac("Smart Brick") == "Smart Brick");
    CHECK(strip_ansi("\x1b[1;30mabc\x1b[0m") == "abc");
}

static void test_status_logic() {
    {
        auto c = evaluate(healthy());
        CHECK(exit_code_for(c) == kExitOk);
        CHECK(find(c, "camera.present")->status == Status::Ok);
        CHECK(find(c, "move.paired")->status == Status::Ok);
    }
    {  // geen camera
        Snapshot s = healthy();
        s.video.clear();
        auto c = evaluate(s);
        CHECK(find(c, "camera.present")->status == Status::Fail);
        CHECK(!find(c, "camera.present")->remedy.empty());
        CHECK(exit_code_for(c) == kExitFail);
    }
    {  // camera op USB maar geen video-node
        Snapshot s = healthy();
        s.video.clear();
        s.usb.push_back({"9-1", kPs3EyeId, "USB Camera"});
        auto c = evaluate(s);
        CHECK(find(c, "camera.present")->status == Status::Fail);
        CHECK(contains(find(c, "camera.present")->remedy, "ov534"));
    }
    {  // andere camera
        Snapshot s = healthy();
        s.video[0].usb = UsbId{0x046d, 0x0825};
        CHECK(find(evaluate(s), "camera.present")->status == Status::Warn);
    }
    {  // camera zonder rechten
        Snapshot s = healthy();
        s.video[0].accessible = false;
        CHECK(find(evaluate(s), "camera.access")->status == Status::Fail);
    }
    {  // geen Move: waarschuwing, exit 1
        Snapshot s = healthy();
        s.hid.clear();
        s.bt_paired.clear();
        s.bt_known.clear();
        auto c = evaluate(s);
        CHECK(find(c, "move.hid")->status == Status::Warn);
        CHECK(find(c, "move.paired")->status == Status::Warn);
        CHECK(exit_code_for(c) == kExitWarn);
    }
    {  // Move bekend maar niet gekoppeld
        Snapshot s = healthy();
        s.bt_paired.clear();
        CHECK(contains(find(evaluate(s), "move.paired")->summary, "niet gekoppeld"));
    }
    {  // Move zonder rechten
        Snapshot s = healthy();
        s.hid[0].accessible = false;
        CHECK(find(evaluate(s), "move.hid")->status == Status::Fail);
    }
    {  // andere Sony-apparaten tellen niet als Move
        Snapshot s = healthy();
        s.hid[0].id = UsbId{0x054c, 0x0ce6};
        CHECK(find_moves(s).empty());
    }
    {  // rfkill
        Snapshot s = healthy();
        s.rfkill[0].soft_blocked = true;
        auto c = evaluate(s);
        CHECK(find(c, "bluetooth.rfkill")->status == Status::Fail);
        CHECK(contains(find(c, "bluetooth.rfkill")->remedy, "rfkill unblock"));
        s.rfkill[0].hard_blocked = true;
        CHECK(contains(find(evaluate(s), "bluetooth.rfkill")->summary, "hard"));
    }
    {  // geen adapter
        Snapshot s = healthy();
        s.bt_adapters.clear();
        CHECK(find(evaluate(s), "bluetooth.adapter")->status == Status::Fail);
    }
    {  // uinput
        Snapshot s = healthy();
        s.uinput_accessible = false;
        CHECK(find(evaluate(s), "uinput.access")->status == Status::Fail);
        s.uinput_exists = false;
        CHECK(contains(find(evaluate(s), "uinput.access")->summary, "bestaat niet"));
    }
    {  // PSMoveAPI
        Snapshot s = healthy();
        s.psmoveapi_missing = {"bin/psmove"};
        CHECK(find(evaluate(s), "psmoveapi.found")->status == Status::Warn);
        s.psmoveapi_found = false;
        s.psmoveapi_searched = {"/a", "/b"};
        auto* c = find(evaluate(s), "psmoveapi.found");
        CHECK(c->status == Status::Fail && contains(c->summary, "/a, /b"));
    }
    {  // Info beinvloedt de exitcode niet; bluetoothctl ontbreekt = waarschuwing
        Snapshot s = healthy();
        s.classic_bonded_only = true;
        CHECK(find(evaluate(s), "bluetooth.classic_bonded_only")->status == Status::Info);
        CHECK(exit_code_for(evaluate(s)) == kExitOk);
        s.bluetoothctl_available = false;
        CHECK(exit_code_for(evaluate(s)) == kExitWarn);
    }
    {  // Fail wint van Warn
        std::vector<Check> v{{"a", Status::Warn, "", ""}, {"b", Status::Fail, "", ""}, {"c", Status::Ok, "", ""}};
        CHECK(exit_code_for(v) == kExitFail);
        CHECK(exit_code_for({}) == kExitOk);
    }
}

static void test_json_output() {
    std::vector<Check> v{{"x.y", Status::Warn, "tekst met \"aanhalingstekens\"", "doe dit\nen dat"}, {"z", Status::Ok, "goed", ""}};
    std::string j = doctor_json(v, 1);
    CHECK(contains(j, R"("exit_code":1)"));
    CHECK(contains(j, R"("id":"x.y","status":"warn")"));
    CHECK(contains(j, R"(tekst met \"aanhalingstekens\")"));
    CHECK(contains(j, R"(doe dit\nen dat)"));
    CHECK(contains(j, R"("remedy":null)"));
    CHECK(j.front() == '{' && j.back() == '\n');

    Snapshot s = healthy();
    s.bt_known.push_back({"AA:BB:CC:DD:EE:99", "Motion Controller"});
    std::string d = devices_json(s, ReportOptions{});
    CHECK(contains(d, "xx:xx:xx:xx:xx:99"));
    CHECK(!contains(d, "AA:BB:CC:DD:EE:99"));
    ReportOptions full;
    full.full_addresses = true;
    CHECK(contains(devices_json(s, full), "AA:BB:CC:DD:EE:99"));
    CHECK(contains(devices_json(s, ReportOptions{}), R"("ps3eye":true)"));
    std::string h = doctor_human(evaluate(s));
    CHECK(contains(h, "[ OK ] camera.present"));
}

static void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

static void test_probe_fake_tree() {
    fs::path root = fs::temp_directory_path() / ("ravengun-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::path sys = root / "sys", dev = root / "dev";
    // camera: video0 -> device (usb-interface) met idVendor/idProduct in de bovenliggende map
    fs::path usbdev = sys / "devices/usb9/9-1";
    write(usbdev / "idVendor", "1415\n");
    write(usbdev / "idProduct", "2000\n");
    write(usbdev / "product", "USB Camera-B4.09.24.1\n");
    fs::create_directories(usbdev / "9-1:1.0");
    fs::create_directories(sys / "bus/usb/drivers/ov534");
    fs::create_directories(sys / "class/video4linux/video0");
    write(sys / "class/video4linux/video0/name", "gspca main driver\n");
    fs::create_directory_symlink(usbdev / "9-1:1.0", sys / "class/video4linux/video0/device");
    fs::create_directory_symlink(sys / "bus/usb/drivers/ov534", usbdev / "9-1:1.0/driver");
    write(dev / "video0", "");
    fs::create_directories(sys / "bus/usb/devices");
    fs::create_directory_symlink(usbdev, sys / "bus/usb/devices/9-1");
    // Move over USB
    write(sys / "class/hidraw/hidraw9/device/uevent", "DRIVER=sony\nHID_ID=0003:0000054C:000003D5\nHID_NAME=Sony Computer Entertainment Motion Controller\n");
    write(dev / "hidraw9", "");
    // rfkill + Bluetooth
    write(sys / "class/rfkill/rfkill0/type", "bluetooth\n");
    write(sys / "class/rfkill/rfkill0/name", "hci0\n");
    write(sys / "class/rfkill/rfkill0/soft", "1\n");
    write(sys / "class/rfkill/rfkill0/hard", "0\n");
    fs::create_directories(sys / "class/bluetooth/hci0");
    fs::create_directories(sys / "class/bluetooth/hci0:256");
    write(root / "input.conf", "[General]\nClassicBondedOnly=false\n");
    write(dev / "uinput", "");

    ProbeOptions opt;
    opt.sys_root = sys.string();
    opt.dev_root = dev.string();
    opt.input_conf = (root / "input.conf").string();
    opt.psmoveapi_prefixes = {(root / "nergens").string()};
    opt.use_bluetoothctl = false;
    Snapshot s = probe(opt);

    CHECK(s.video.size() == 1);
    if (s.video.size() == 1) {
        CHECK(s.video[0].name == "gspca main driver");
        CHECK(s.video[0].driver == "ov534");
        CHECK(s.video[0].usb && is_ps3eye(*s.video[0].usb));
        CHECK(s.video[0].accessible);
    }
    CHECK(s.hid.size() == 1 && is_move(s.hid[0].id) && s.hid[0].bus == 3);
    CHECK(s.usb.size() == 1 && s.usb[0].product == "USB Camera-B4.09.24.1");
    CHECK(s.rfkill.size() == 1 && s.rfkill[0].soft_blocked && !s.rfkill[0].hard_blocked);
    CHECK(s.bt_adapters == std::vector<std::string>{"hci0"});  // hci0:256 is een verbinding
    CHECK(s.input_conf_readable && s.classic_bonded_only == std::optional<bool>(false));
    CHECK(s.uinput_exists && s.uinput_accessible);
    CHECK(!s.psmoveapi_found && s.psmoveapi_searched.size() == 1);

    // PSMoveAPI-prefix zonder header en CLI: gevonden maar onvolledig
    write(root / "psm/lib/libpsmoveapi.so", "");
    opt.psmoveapi_prefixes = {(root / "psm").string()};
    Snapshot s2 = probe(opt);
    CHECK(s2.psmoveapi_found && s2.psmoveapi_missing.size() == 2);
    write(root / "psm/include/psmoveapi/psmove.h", "");
    write(root / "psm/bin/psmove", "");
    Snapshot s3 = probe(opt);
    CHECK(s3.psmoveapi_found && s3.psmoveapi_missing.empty());

    fs::remove_all(root);
}

static void test_run_capture() {
    auto r = run_capture({"echo", "hallo"});
    CHECK(r.ok && r.out == "hallo\n");
    CHECK(!run_capture({"ravengun-bestaat-niet-xyz"}).ok);
    CHECK(!run_capture({"false"}).ok);
    CHECK(!run_capture({"sleep", "5"}, 200).ok);  // time-out
}

int main() {
    test_json();
    test_parse();
    test_status_logic();
    test_json_output();
    test_probe_fake_tree();
    test_run_capture();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
