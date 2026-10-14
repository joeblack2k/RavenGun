#pragma once
// Gamepads (evdev) vinden uit /proc/bus/input/devices, als extra bediening naast de lightgun van een speler (docs/gamepad.md).
// Alleen lezen: er wordt geen apparaat geopend. De parser werkt op tekst (testbaar zonder hardware); list_system_gamepads() leest
// het echte /proc en /dev/input/by-id.
//
// Een gamepad: EV_KEY met BTN_SOUTH (= BTN_GAMEPAD) en EV_ABS met ABS_X en ABS_Y. Niet: onze eigen virtuele apparaten (vendor
// 0x1209 of een naam die met "RavenGun" begint), libvirtualhid en de PS Move ("Motion Controller"). Wel: de virtuele pad van de
// Steam-client ("Steam Virtual Gamepad", "Microsoft X-Box 360 pad") en de Steam Controller via hid-steam.
#include <string>
#include <string_view>
#include <vector>

namespace ravengun::sysinfo {

struct Gamepad {
    std::string id;      // stabiel: de by-id-symlink naar dit eventN, anders "pad-<naam>-<hash van uniq of phys>"
    std::string name;
    std::string event;   // "eventN" (het apparaat is /dev/input/<event>)
    std::string phys, uniq;
    unsigned bus = 0, vendor = 0, product = 0;
    bool by_id = false;  // het id komt uit /dev/input/by-id
};

// Waarom een invoerapparaat wel of geen gamepad is (voor het logboek en `pads`, om op de bench te zien wat een controller laat zien).
struct GamepadVerdict {
    std::string name, event;
    unsigned vendor = 0, product = 0;
    bool gamepad = false;
    std::string why;     // Nederlands, kort
};

// Id zonder by-id-link: "pad-" + de naam (alleen [A-Za-z0-9_.-], spaties worden _) + "-" + 8 hex van een hash van uniq (anders phys).
// Zonder uniq en phys alleen de naam. Geen adres in het id zelf (Bluetooth-adressen horen niet in logboeken).
std::string gamepad_fallback_id(const std::string& name, const std::string& uniq, const std::string& phys);

// Alle gamepads, gesorteerd op eventnummer. `verdicts` (optioneel) krijgt een regel per invoerapparaat.
std::vector<Gamepad> list_gamepads(std::string_view proc_text, const std::string& byid_dir = "/dev/input/by-id",
                                   std::vector<GamepadVerdict>* verdicts = nullptr);
std::vector<Gamepad> list_system_gamepads(std::vector<GamepadVerdict>* verdicts = nullptr);

}  // namespace ravengun::sysinfo
