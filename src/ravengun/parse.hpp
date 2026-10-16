#pragma once
// Zuivere parsers zonder I/O (goed testbaar).
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ravengun/model.hpp"
#include "ravengun/strutil.hpp"  // trim

namespace ravengun {

constexpr UsbId kPs3EyeId{0x1415, 0x2000};
constexpr UsbId kMoveZcm1{0x054c, 0x03d5};
constexpr UsbId kMoveZcm2{0x054c, 0x0c5e};

bool is_move(UsbId id);
bool is_ps3eye(UsbId id);

std::string strip_ansi(std::string_view s);

// Regels "Device AA:BB:.. naam" uit `bluetoothctl devices` (ANSI-kleuren worden genegeerd).
std::vector<BtDevice> parse_bluetoothctl_devices(std::string_view text);

// "KEY=waarde"-regels (sysfs uevent). Regels zonder '=' worden overgeslagen.
std::map<std::string, std::string> parse_keyvalue(std::string_view text);

struct HidId {
    unsigned bus = 0;
    UsbId id;
};
// "0003:0000054C:000003D5" (HID_ID)
std::optional<HidId> parse_hid_id(std::string_view s);

// Hexadecimale 16-bit waarde uit sysfs ("054c\n" of "1415").
std::optional<uint16_t> parse_hex16(std::string_view s);

// Waarde van ClassicBondedOnly in een BlueZ input.conf; commentaarregels (# en ;) tellen niet mee.
std::optional<bool> parse_classic_bonded_only(std::string_view text);

// "bluetoothctl: 5.87" of "5.87" -> "5.87"
std::optional<std::string> parse_bluez_version(std::string_view text);

// "AA:BB:CC:DD:EE:FF" -> "xx:xx:xx:xx:xx:FF"; andere invoer blijft ongewijzigd.
std::string redact_mac(std::string_view mac);

// Naam waarmee een Move zich bij BlueZ meldt.
bool looks_like_move_name(std::string_view name);

}  // namespace ravengun
