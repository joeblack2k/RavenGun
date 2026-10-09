#pragma once
// Navigation Controller (de losse joystick bij de Move): aanwezigheid en batterij uit sysfs.
// PSMoveAPI is in deze build niet voor de Navigation Controller gelinkt; de kernel (hid-sony) levert hem wel als
// HID-apparaat met een power_supply. We lezen alleen /sys/class/power_supply (geen hardware openen, geen schrijven).
// Zonder zo'n apparaat is het resultaat gewoon "niet verbonden".
// Niet op echte hardware getest (er was geen Navigation Controller beschikbaar); de logica is getest met een nepmap.
#include <string>

namespace ravengun::sysinfo {

struct NavconStatus {
    bool connected = false;
    int percent = -1;          // 0..100, -1 = onbekend
    std::string status;        // charging | discharging | full | unknown
    std::string code;          // zelfde woorden als PSMove_Battery_Level: min, ge20, ge40, ge60, ge80, max, charging, charged, unknown
    std::string text;          // Nederlands
};

// Vertaalt een percentage en een laadstatus ("Charging", "Discharging", "Full", ...) naar code en tekst.
NavconStatus classify_battery(int percent, const std::string& power_status);
// root = /sys/class/power_supply (of een nepmap in tests).
NavconStatus read_navcon(const std::string& root = "/sys/class/power_supply");
// JSON-object voor de status: {"connected":..,"battery":{"percent":..,"code":..,"text":..,"charging":..}}
std::string navcon_json(const NavconStatus& n);

}  // namespace ravengun::sysinfo
