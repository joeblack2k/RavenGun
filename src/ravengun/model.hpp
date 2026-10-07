#pragma once
// Gegevensmodel: momentopname van de host (Snapshot) en controle-uitkomsten (Check).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ravengun {

struct UsbId {
    uint16_t vid = 0;
    uint16_t pid = 0;
    bool operator==(const UsbId&) const = default;
};

struct VideoDevice {
    std::string node;     // /dev/video0
    std::string name;     // sysfs "name"
    std::string driver;   // basename van device/driver-symlink (kan leeg zijn)
    std::optional<UsbId> usb;
    bool accessible = false;  // R+W voor het huidige proces
};

struct HidDevice {
    std::string node;  // /dev/hidraw3
    std::string name;  // HID_NAME
    unsigned bus = 0;  // 3 = USB, 5 = Bluetooth
    UsbId id;
    bool accessible = false;
};

struct UsbDevice {
    std::string sysname;
    UsbId id;
    std::string product;
};

struct RfkillDevice {
    std::string name;
    std::string type;  // bluetooth, wlan, ...
    bool soft_blocked = false;
    bool hard_blocked = false;
};

struct BtDevice {
    std::string address;
    std::string name;
};

struct Snapshot {
    std::vector<VideoDevice> video;
    std::vector<HidDevice> hid;
    std::vector<UsbDevice> usb;
    std::vector<RfkillDevice> rfkill;
    std::vector<std::string> bt_adapters;  // hci0, ...
    bool bluetoothctl_available = false;
    std::optional<std::string> bluez_version;
    bool input_conf_readable = false;
    std::optional<bool> classic_bonded_only;  // nullopt = niet ingesteld (BlueZ-standaard: true)
    std::vector<BtDevice> bt_paired;          // `bluetoothctl devices Paired`
    std::vector<BtDevice> bt_known;           // `bluetoothctl devices`
    bool uinput_exists = false;
    bool uinput_accessible = false;
    bool psmoveapi_found = false;
    std::string psmoveapi_prefix;                // gevonden prefix, of leeg
    std::vector<std::string> psmoveapi_missing;  // ontbrekende onderdelen in gevonden prefix
    std::vector<std::string> psmoveapi_searched;
};

enum class Status { Ok, Info, Warn, Fail };

struct Check {
    std::string id;
    Status status = Status::Ok;
    std::string summary;
    std::string remedy;  // leeg bij Ok/Info zonder actie
};

const char* to_string(Status s);

// Exitcodes van `doctor`.
constexpr int kExitOk = 0;
constexpr int kExitWarn = 1;
constexpr int kExitFail = 2;
constexpr int kExitUsage = 64;
constexpr int kExitInternal = 70;

}  // namespace ravengun
