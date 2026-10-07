#include "ravengun/report.hpp"

#include <cstdio>
#include <sstream>

#include "ravengun/doctor.hpp"
#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"

namespace ravengun {
namespace {

std::string hex4(uint16_t v) {
    char b[8];
    std::snprintf(b, sizeof b, "%04x", v);
    return b;
}
std::string usb_str(UsbId id) { return hex4(id.vid) + ":" + hex4(id.pid); }
std::string addr(const BtDevice& d, const ReportOptions& o) { return o.full_addresses ? d.address : redact_mac(d.address); }

const char* tag(Status s) {
    switch (s) {
        case Status::Ok: return "[ OK ]";
        case Status::Info: return "[INFO]";
        case Status::Warn: return "[WARN]";
        case Status::Fail: return "[FAIL]";
    }
    return "[ ?? ]";
}

const char* bus_name(unsigned b) { return b == 3 ? "usb" : b == 5 ? "bluetooth" : "onbekend"; }

}  // namespace

std::string doctor_human(const std::vector<Check>& checks) {
    std::ostringstream o;
    for (const auto& c : checks) {
        o << tag(c.status) << ' ' << c.id << ": " << c.summary << '\n';
        if (!c.remedy.empty()) o << "         actie: " << c.remedy << '\n';
    }
    int code = exit_code_for(checks);
    o << (code == kExitOk ? "Resultaat: alles in orde." : code == kExitWarn ? "Resultaat: waarschuwingen (exitcode 1)." : "Resultaat: fouten (exitcode 2).") << '\n';
    return o.str();
}

std::string doctor_json(const std::vector<Check>& checks, int exit_code) {
    json::Writer w;
    w.begin_object();
    w.key("exit_code").value(exit_code);
    w.key("checks").begin_array();
    for (const auto& c : checks) {
        w.begin_object();
        w.key("id").value(c.id);
        w.key("status").value(to_string(c.status));
        w.key("summary").value(c.summary);
        w.key("remedy");
        if (c.remedy.empty()) w.null(); else w.value(c.remedy);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str() + "\n";
}

std::string devices_human(const Snapshot& s, const ReportOptions& opt) {
    std::ostringstream o;
    o << "Camera's (video4linux):\n";
    if (s.video.empty()) o << "  geen\n";
    for (const auto& v : s.video) {
        o << "  " << v.node << "  " << v.name << "  driver=" << (v.driver.empty() ? "?" : v.driver)
          << "  usb=" << (v.usb ? usb_str(*v.usb) : "?") << (v.usb && is_ps3eye(*v.usb) ? " (PS3 Eye)" : "")
          << "  rechten=" << (v.accessible ? "rw" : "geen") << '\n';
    }
    o << "Sony HID-apparaten (hidraw):\n";
    bool any = false;
    for (const auto& h : s.hid) {
        if (h.id.vid != 0x054c) continue;
        any = true;
        o << "  " << h.node << "  " << h.name << "  id=" << usb_str(h.id) << "  bus=" << bus_name(h.bus)
          << (is_move(h.id) ? "  (Move)" : "") << "  rechten=" << (h.accessible ? "rw" : "geen") << '\n';
    }
    if (!any) o << "  geen\n";
    o << "Bluetooth-adapters:\n";
    if (s.bt_adapters.empty()) o << "  geen\n";
    for (const auto& a : s.bt_adapters) o << "  " << a << '\n';
    o << "Bluetooth-apparaten met Move-naam:\n";
    bool anybt = false;
    for (const auto& d : s.bt_known) {
        if (!looks_like_move_name(d.name)) continue;
        anybt = true;
        bool paired = false;
        for (const auto& p : s.bt_paired) if (p.address == d.address) paired = true;
        o << "  " << addr(d, opt) << "  " << d.name << "  " << (paired ? "gekoppeld" : "niet gekoppeld") << '\n';
    }
    if (!anybt) o << "  geen (" << s.bt_known.size() << " bekende apparaten in totaal, " << s.bt_paired.size() << " gekoppeld)\n";
    o << "uinput: " << (!s.uinput_exists ? "ontbreekt" : s.uinput_accessible ? "rw" : "aanwezig, geen rechten") << '\n';
    return o.str();
}

std::string devices_json(const Snapshot& s, const ReportOptions& opt) {
    json::Writer w;
    w.begin_object();
    w.key("video").begin_array();
    for (const auto& v : s.video) {
        w.begin_object();
        w.key("node").value(v.node);
        w.key("name").value(v.name);
        w.key("driver").value(v.driver);
        w.key("usb_id");
        if (v.usb) w.value(usb_str(*v.usb)); else w.null();
        w.key("ps3eye").value(v.usb && is_ps3eye(*v.usb));
        w.key("accessible").value(v.accessible);
        w.end_object();
    }
    w.end_array();
    w.key("hid_sony").begin_array();
    for (const auto& h : s.hid) {
        if (h.id.vid != 0x054c) continue;
        w.begin_object();
        w.key("node").value(h.node);
        w.key("name").value(h.name);
        w.key("usb_id").value(usb_str(h.id));
        w.key("bus").value(bus_name(h.bus));
        w.key("move").value(is_move(h.id));
        w.key("accessible").value(h.accessible);
        w.end_object();
    }
    w.end_array();
    w.key("bluetooth").begin_object();
    w.key("adapters").begin_array();
    for (const auto& a : s.bt_adapters) w.value(a);
    w.end_array();
    w.key("bluez_version");
    if (s.bluez_version) w.value(*s.bluez_version); else w.null();
    w.key("known_devices_total").value(static_cast<long long>(s.bt_known.size()));
    w.key("paired_devices_total").value(static_cast<long long>(s.bt_paired.size()));
    w.key("move_devices").begin_array();
    for (const auto& d : s.bt_known) {
        if (!looks_like_move_name(d.name)) continue;
        bool paired = false;
        for (const auto& p : s.bt_paired) if (p.address == d.address) paired = true;
        w.begin_object();
        w.key("address").value(addr(d, opt));
        w.key("name").value(d.name);
        w.key("paired").value(paired);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    w.key("uinput").begin_object();
    w.key("exists").value(s.uinput_exists);
    w.key("accessible").value(s.uinput_accessible);
    w.end_object();
    w.end_object();
    return w.str() + "\n";
}

}  // namespace ravengun
