#include "ravengun/doctor.hpp"

#include <algorithm>

#include "ravengun/parse.hpp"

namespace ravengun {
namespace {

std::string join(const std::vector<std::string>& v, const char* sep = ", ") {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) { if (i) r += sep; r += v[i]; }
    return r;
}

Check make(std::string id, Status s, std::string summary, std::string remedy = {}) {
    return Check{std::move(id), s, std::move(summary), std::move(remedy)};
}

const char* move_model(UsbId id) { return id == kMoveZcm2 ? "ZCM2" : "ZCM1"; }

void check_camera(const Snapshot& s, std::vector<Check>& out) {
    const VideoDevice* eye = nullptr;
    for (const auto& v : s.video) if (v.usb && is_ps3eye(*v.usb)) { eye = &v; break; }
    const bool eye_on_usb = std::any_of(s.usb.begin(), s.usb.end(), [](const UsbDevice& u) { return is_ps3eye(u.id); });

    if (eye) {
        out.push_back(make("camera.present", Status::Ok,
            "PS3 Eye (1415:2000) op " + eye->node + ", driver " + (eye->driver.empty() ? "onbekend" : eye->driver)));
        if (eye->accessible) {
            out.push_back(make("camera.access", Status::Ok, "lees- en schrijfrechten op " + eye->node));
        } else {
            out.push_back(make("camera.access", Status::Fail, "geen lees-/schrijfrechten op " + eye->node,
                "Controleer dat je in een lokale sessie zit (uaccess-ACL) en niet via een sessie zonder seat; "
                "een udev-voorstel staat in docs/hardware.md, onder 'Voorgestelde hostwijzigingen'."));
        }
        return;
    }
    if (eye_on_usb) {
        out.push_back(make("camera.present", Status::Fail, "PS3 Eye zit op USB maar heeft geen /dev/video-node",
            "De kernelmodule ov534 (gspca) ontbreekt of is niet geladen: controleer `lsmod | grep -E 'ov534|gspca'` en `journalctl -k | grep -i ov534`."));
        return;
    }
    if (!s.video.empty()) {
        out.push_back(make("camera.present", Status::Warn,
            "geen PS3 Eye gevonden; wel andere videoapparaten (" + s.video.front().name + ")",
            "Sluit de PS3 Eye (USB 1415:2000) aan op een USB-poort, bij voorkeur rechtstreeks op de pc."));
        return;
    }
    out.push_back(make("camera.present", Status::Fail, "geen camera gevonden",
        "Sluit de PS3 Eye aan op een USB-poort (bij voorkeur USB 2.0 rechtstreeks op de pc) en controleer met `lsusb`."));
}

void check_move(const Snapshot& s, std::vector<Check>& out) {
    auto moves = find_moves(s);
    if (!moves.empty()) {
        std::vector<std::string> desc;
        bool all_access = true;
        for (const auto& m : moves) {
            desc.push_back(m.node + " (" + move_model(m.id) + ", " + (m.bus == 3 ? "USB" : m.bus == 5 ? "Bluetooth" : "bus ?") + ")");
            all_access = all_access && m.accessible;
        }
        if (all_access) {
            out.push_back(make("move.hid", Status::Ok, "Move gevonden: " + join(desc)));
        } else {
            out.push_back(make("move.hid", Status::Fail, "Move gevonden maar geen lees-/schrijfrechten: " + join(desc),
                "Het hidraw-apparaat van de Move is niet voor de huidige gebruiker toegankelijk. Een udev-voorstel staat in "
                "docs/hardware.md, onder 'Voorgestelde hostwijzigingen'."));
        }
    } else {
        bool usb_without_hid = std::any_of(s.usb.begin(), s.usb.end(), [](const UsbDevice& u) { return is_move(u.id); });
        if (usb_without_hid) {
            out.push_back(make("move.hid", Status::Warn, "Move zit op USB maar heeft geen hidraw-apparaat",
                "Koppel de Move los en weer aan; controleer `journalctl -k | grep -i hid`."));
        } else {
            out.push_back(make("move.hid", Status::Warn, "geen Move gevonden (niet op USB en niet via Bluetooth verbonden)",
                "Sluit de Move aan met een USB-DATAkabel (een kabel die alleen laadt werkt niet) of druk op de PS-knop als hij al gekoppeld is."));
        }
    }

    auto is_move_dev = [](const BtDevice& d) { return looks_like_move_name(d.name); };
    bool paired = std::any_of(s.bt_paired.begin(), s.bt_paired.end(), is_move_dev);
    bool known = std::any_of(s.bt_known.begin(), s.bt_known.end(), is_move_dev);
    if (!s.bluetoothctl_available) {
        out.push_back(make("move.paired", Status::Warn, "bluetoothctl niet beschikbaar; koppelstatus onbekend",
            "Installeer of activeer BlueZ-tools (bluetoothctl) of controleer of bluetoothd draait."));
    } else if (paired) {
        out.push_back(make("move.paired", Status::Ok, "minstens een Move is gekoppeld bij BlueZ"));
    } else if (known) {
        out.push_back(make("move.paired", Status::Warn, "een Move is bij BlueZ bekend maar niet gekoppeld",
            "Koppel opnieuw: zie de koppelprocedure in docs/hardware.md (vereist USB-datakabel en PS-knop)."));
    } else {
        out.push_back(make("move.paired", Status::Warn, "geen Move gekoppeld bij BlueZ",
            "Koppelen vereist een USB-datakabel en een druk op de PS-knop; zie docs/hardware.md."));
    }
}

void check_bluetooth(const Snapshot& s, std::vector<Check>& out) {
    if (s.bt_adapters.empty()) {
        out.push_back(make("bluetooth.adapter", Status::Fail, "geen Bluetooth-adapter gevonden",
            "Controleer `lsusb` op de adapter en `journalctl -k | grep -i bluetooth`; schakel Bluetooth in het BIOS/KDE in."));
    } else {
        out.push_back(make("bluetooth.adapter", Status::Ok, "Bluetooth-adapter: " + join(s.bt_adapters)));
    }

    std::vector<std::string> blocked_soft, blocked_hard;
    int bt_count = 0;
    for (const auto& r : s.rfkill) {
        if (r.type != "bluetooth") continue;
        ++bt_count;
        if (r.hard_blocked) blocked_hard.push_back(r.name);
        else if (r.soft_blocked) blocked_soft.push_back(r.name);
    }
    if (!blocked_hard.empty()) {
        out.push_back(make("bluetooth.rfkill", Status::Fail, "Bluetooth hardwarematig geblokkeerd (rfkill hard): " + join(blocked_hard),
            "Zet de fysieke draadloos-schakelaar of BIOS-instelling om; software kan dit niet opheffen."));
    } else if (!blocked_soft.empty()) {
        out.push_back(make("bluetooth.rfkill", Status::Fail, "Bluetooth softwarematig geblokkeerd (rfkill soft): " + join(blocked_soft),
            "Zet Bluetooth aan in de KDE-systeemvoorkeuren, of draai `rfkill unblock bluetooth`."));
    } else if (bt_count > 0) {
        out.push_back(make("bluetooth.rfkill", Status::Ok, "rfkill: Bluetooth niet geblokkeerd"));
    } else {
        out.push_back(make("bluetooth.rfkill", Status::Info, "geen rfkill-apparaat van type bluetooth gezien"));
    }

    if (s.bluez_version) {
        out.push_back(make("bluetooth.bluez", Status::Ok, "BlueZ " + *s.bluez_version));
    } else {
        out.push_back(make("bluetooth.bluez", Status::Warn, "BlueZ-versie niet te bepalen",
            "Controleer of `bluetoothctl --version` werkt en of bluetoothd draait (`systemctl status bluetooth`)."));
    }

    if (!s.input_conf_readable) {
        out.push_back(make("bluetooth.classic_bonded_only", Status::Info, "input.conf niet leesbaar; ClassicBondedOnly onbekend"));
    } else if (s.classic_bonded_only.has_value() && !*s.classic_bonded_only) {
        out.push_back(make("bluetooth.classic_bonded_only", Status::Info, "ClassicBondedOnly=false (ongebonden HID-verbindingen toegestaan)"));
    } else {
        out.push_back(make("bluetooth.classic_bonded_only", Status::Info,
            s.classic_bonded_only.has_value() ? "ClassicBondedOnly=true"
                                              : "ClassicBondedOnly niet ingesteld (BlueZ-standaard: true)",
            "Normaal geen actie: de Move moet eerst worden gebonden. Verbindt een gekoppelde Move toch niet, "
            "zie het voorstel in docs/hardware.md."));
    }
}

void check_uinput(const Snapshot& s, std::vector<Check>& out) {
    if (!s.uinput_exists) {
        out.push_back(make("uinput.access", Status::Fail, "/dev/uinput bestaat niet",
            "De kernelmodule uinput is niet geladen (laden vereist root); voorstel in docs/hardware.md."));
    } else if (!s.uinput_accessible) {
        out.push_back(make("uinput.access", Status::Fail, "/dev/uinput bestaat maar is niet lees-/schrijfbaar voor deze gebruiker",
            "Een udev-regel met uaccess of groepslidmaatschap is nodig; voorstel in docs/hardware.md."));
    } else {
        out.push_back(make("uinput.access", Status::Ok, "/dev/uinput is lees- en schrijfbaar"));
    }
}

void check_psmoveapi(const Snapshot& s, std::vector<Check>& out) {
    if (!s.psmoveapi_found) {
        out.push_back(make("psmoveapi.found", Status::Fail, "PSMoveAPI niet gevonden (gezocht: " + join(s.psmoveapi_searched) + ")",
            "Bouw PSMoveAPI volgens docs/build.md, of zet RAVENGUN_PSMOVEAPI_PREFIX naar de installatiemap."));
    } else if (!s.psmoveapi_missing.empty()) {
        out.push_back(make("psmoveapi.found", Status::Warn,
            "PSMoveAPI gevonden in " + s.psmoveapi_prefix + ", maar onvolledig (ontbreekt: " + join(s.psmoveapi_missing) + ")",
            "Bouw en installeer PSMoveAPI opnieuw volgens docs/build.md."));
    } else {
        out.push_back(make("psmoveapi.found", Status::Ok, "PSMoveAPI gevonden in " + s.psmoveapi_prefix));
    }
}

}  // namespace

std::vector<HidDevice> find_moves(const Snapshot& snap) {
    std::vector<HidDevice> r;
    for (const auto& h : snap.hid) if (is_move(h.id)) r.push_back(h);
    return r;
}

std::vector<Check> evaluate(const Snapshot& snap) {
    std::vector<Check> out;
    check_camera(snap, out);
    check_move(snap, out);
    check_bluetooth(snap, out);
    check_uinput(snap, out);
    check_psmoveapi(snap, out);
    return out;
}

int exit_code_for(const std::vector<Check>& checks) {
    int code = kExitOk;
    for (const auto& c : checks) {
        if (c.status == Status::Fail) return kExitFail;
        if (c.status == Status::Warn) code = kExitWarn;
    }
    return code;
}

const char* to_string(Status s) {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::Info: return "info";
        case Status::Warn: return "warn";
        case Status::Fail: return "fail";
    }
    return "?";
}

}  // namespace ravengun
