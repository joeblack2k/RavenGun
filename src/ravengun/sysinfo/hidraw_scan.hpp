#pragma once
// Sony-controllers zoeken via sysfs (/sys/class/hidraw/*/device/uevent): goedkoop (geen apparaat openen), dus geschikt om
// elke seconde in de HID-thread en tijdens het koppelen te draaien. Alleen lezen.
#include <cstdint>
#include <string>
#include <vector>

namespace ravengun::sysinfo {

inline constexpr unsigned kSonyVendor = 0x054c;
inline constexpr unsigned kMoveZcm1 = 0x03d5;   // PS Move (CECH-ZCM1)
inline constexpr unsigned kMoveZcm2 = 0x0c5e;   // PS Move (CECH-ZCM2)
inline constexpr unsigned kNavigation = 0x042f; // Navigation Controller (CECH-ZCK1)

struct HidrawDevice {
    std::string node;      // /dev/hidrawN
    std::string sysname;   // hidrawN
    unsigned bus = 0;      // 3 = USB, 5 = Bluetooth
    unsigned vendor = 0, product = 0;
    std::string name;      // HID_NAME
    std::string uniq;      // HID_UNIQ in kleine letters (via Bluetooth het adres van de controller; via USB meestal leeg)
    std::string type() const;   // "move", "navigation" of ""
    std::string model() const;  // "ZCM1", "ZCM2" of ""
    bool usb() const { return bus == 3; }
    bool bluetooth() const { return bus == 5; }
};

// Leest één uevent-tekst. false als het geen HID-apparaat is.
bool parse_hid_uevent(const std::string& text, HidrawDevice& out);
// Alle Sony-Moves en Navigation Controllers onder `root` (standaard /sys/class/hidraw), gesorteerd op node.
std::vector<HidrawDevice> scan_sony_hidraw(const std::string& root = "/sys/class/hidraw", const std::string& dev_dir = "/dev");

}  // namespace ravengun::sysinfo
