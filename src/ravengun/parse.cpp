#include "ravengun/parse.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ravengun {

bool is_move(UsbId id) { return id == kMoveZcm1 || id == kMoveZcm2; }
bool is_ps3eye(UsbId id) { return id == kPs3EyeId; }

std::string strip_ansi(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;  // eindbyte van de CSI-reeks
            continue;
        }
        if (s[i] == '\r') continue;
        r += s[i];
    }
    return r;
}

static bool looks_like_mac(std::string_view s) {
    if (s.size() != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        if (i % 3 == 2) { if (s[i] != ':') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

std::vector<BtDevice> parse_bluetoothctl_devices(std::string_view text) {
    std::vector<BtDevice> out;
    std::istringstream in(strip_ansi(text));
    std::string line;
    while (std::getline(in, line)) {
        // Interactieve prompts kunnen voorloop-tekst geven; zoek "Device ".
        auto pos = line.find("Device ");
        if (pos == std::string::npos) continue;
        std::string rest = line.substr(pos + 7);
        if (rest.size() < 17 || !looks_like_mac(std::string_view(rest).substr(0, 17))) continue;
        BtDevice d;
        d.address = rest.substr(0, 17);
        d.name = trim(rest.size() > 17 ? rest.substr(17) : std::string());
        out.push_back(std::move(d));
    }
    return out;
}

std::map<std::string, std::string> parse_keyvalue(std::string_view text) {
    std::map<std::string, std::string> m;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        m[line.substr(0, eq)] = trim(line.substr(eq + 1));
    }
    return m;
}

std::optional<uint16_t> parse_hex16(std::string_view s) {
    std::string t = trim(s);
    if (t.empty() || t.size() > 8) return std::nullopt;
    unsigned long v = 0;
    for (char c : t) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        v = v * 16 + static_cast<unsigned long>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
    }
    if (v > 0xffff) return std::nullopt;
    return static_cast<uint16_t>(v);
}

std::optional<HidId> parse_hid_id(std::string_view s) {
    std::string t = trim(s);
    auto a = t.find(':');
    if (a == std::string::npos) return std::nullopt;
    auto b = t.find(':', a + 1);
    if (b == std::string::npos) return std::nullopt;
    auto bus = parse_hex16(t.substr(0, a));
    auto vid = parse_hex16(t.substr(a + 1, b - a - 1));
    auto pid = parse_hex16(t.substr(b + 1));
    if (!bus || !vid || !pid) return std::nullopt;
    return HidId{*bus, UsbId{*vid, *pid}};
}

std::optional<bool> parse_classic_bonded_only(std::string_view text) {
    std::optional<bool> result;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        if (trim(t.substr(0, eq)) != "ClassicBondedOnly") continue;
        std::string v = trim(t.substr(eq + 1));
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
        if (v == "true" || v == "1" || v == "yes") result = true;
        else if (v == "false" || v == "0" || v == "no") result = false;
    }
    return result;
}

std::optional<std::string> parse_bluez_version(std::string_view text) {
    std::string t = trim(text);
    auto colon = t.rfind(':');
    if (colon != std::string::npos) t = trim(t.substr(colon + 1));
    if (t.empty() || !std::isdigit(static_cast<unsigned char>(t[0]))) return std::nullopt;
    for (char c : t) if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return std::nullopt;
    return t;
}

std::string redact_mac(std::string_view mac) {
    if (!looks_like_mac(mac)) return std::string(mac);
    return "xx:xx:xx:xx:xx:" + std::string(mac.substr(15, 2));
}

bool looks_like_move_name(std::string_view name) {
    return name.find("Motion Controller") != std::string_view::npos;
}

}  // namespace ravengun
