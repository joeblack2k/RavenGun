#pragma once
// Gewone systeemmuizen (evdev) vinden uit /proc/bus/input/devices, als bron voor de virtuele lightgun van een speler
// (zie "Muis als lightgun" in docs/architecture.md). Alleen lezen: er wordt geen apparaat geopend. De parser werkt op
// tekst (testbaar zonder hardware); list_system_mice() leest het echte /proc en /dev/input/by-id.
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ravengun::sysinfo {

// Eén blok uit /proc/bus/input/devices.
struct InputDevice {
    unsigned bus = 0, vendor = 0, product = 0, version = 0;
    std::string name;                   // N: Name="..." zonder aanhalingstekens
    std::string phys;                   // P: Phys=
    std::string uniq;                   // U: Uniq= (bij Bluetooth meestal het adres van het apparaat)
    std::vector<std::string> handlers;  // H: Handlers= (bijv. "mouse1", "event5")
    bool has_rel_xy = false;            // B: REL= heeft REL_X en REL_Y
    bool has_abs_xy = false;            // B: ABS= heeft ABS_X en ABS_Y
    bool has_rel_bitmap = false;        // er was een B: REL=-regel (anders is niet te zien of het apparaat relatief is)
    bool has_abs_bitmap = false;
    // De volledige bitmaps van B: KEY= en B: ABS= (64-bit woorden, laagste woord eerst; zoals de kernel ze op een 64-bit systeem
    // schrijft). Voor has_key/has_abs.
    std::vector<unsigned long long> key_bits, abs_bits;
    bool has_key(int code) const;
    bool has_abs(int code) const;
    // "eventN" / "mouseN" uit de handlers (leeg als die er niet is). Alleen cijfers na het voorvoegsel.
    std::string event_handler() const;
    std::string mouse_handler() const;
};

// Parseert de tekst van /proc/bus/input/devices (blokken gescheiden door lege regels). Onbekende regels worden genegeerd.
std::vector<InputDevice> parse_input_devices(std::string_view proc_text);

// Een muis zoals de daemon hem aanbiedt.
struct Mouse {
    std::string id;     // stabiel: naam van de by-id-symlink naar dit eventN, anders "eventN"
    std::string name;
    std::string event;  // "eventN" (het apparaat is /dev/input/<event>)
    std::string phys;
    unsigned bus = 0, vendor = 0, product = 0;
    bool absolute = false;  // ABS_X/ABS_Y zonder REL_X/REL_Y (tablet, touchscreen); anders relatief
};

// Een geldig id: 1..kMouseIdMax tekens uit [A-Za-z0-9_.:+@,=-] (geen pad, geen spaties).
inline constexpr size_t kMouseIdMax = 128;
bool mouse_id_valid(std::string_view id);

// eventN -> namen van de symlinks in `byid_dir` (/dev/input/by-id) die ernaar wijzen, alfabetisch; alleen geldige ids.
std::map<std::string, std::vector<std::string>> byid_links(const std::string& byid_dir);

// Alle apparaten met een mouseN- en een eventN-handler, behalve onze eigen virtuele apparaten (vendor 0x1209). `byid_dir` is
// /dev/input/by-id (of een nepmap in tests): de naam van de symlink die naar het eventN wijst wordt het id.
std::vector<Mouse> list_mice(std::string_view proc_text, const std::string& byid_dir = "/dev/input/by-id");
// Dezelfde lijst voor dit systeem (leest /proc/bus/input/devices); leeg als dat niet leesbaar is.
std::vector<Mouse> list_system_mice();

}  // namespace ravengun::sysinfo
