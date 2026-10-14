#include "ravengun/sysinfo/mice.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace ravengun::sysinfo {

namespace fs = std::filesystem;

namespace {

constexpr unsigned kOwnVendor = 0x1209;  // pid.codes-testbereik: de virtuele apparaten van RavenGun

std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// "Bus=0003 Vendor=046d ..." -> waarde van `key` als hex.
bool hex_field(std::string_view line, std::string_view key, unsigned& out) {
    size_t at = line.find(std::string(key) + "=");
    if (at == std::string_view::npos) return false;
    at += key.size() + 1;
    size_t end = at;
    while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end]))) ++end;
    if (end == at || end - at > 8) return false;
    out = static_cast<unsigned>(std::strtoul(std::string(line.substr(at, end - at)).c_str(), nullptr, 16));
    return true;
}

// B: REL=903 of B: ABS=3 -> zijn bit 0 (X) en bit 1 (Y) gezet? Het laatste hex-woord bevat de laagste bits, ongeacht de woordbreedte.
bool xy_bits(std::string_view value) {
    auto words = split_ws(value);
    if (words.empty()) return false;
    const std::string& w = words.back();
    if (w.size() > 16) return false;
    for (char c : w) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    unsigned long long v = std::strtoull(w.c_str(), nullptr, 16);
    return (v & 0x3) == 0x3;
}

bool handler_ok(const std::string& h, std::string_view prefix) {
    if (h.size() <= prefix.size() || h.compare(0, prefix.size(), prefix) != 0) return false;
    for (size_t i = prefix.size(); i < h.size(); ++i) if (h[i] < '0' || h[i] > '9') return false;
    return h.size() - prefix.size() <= 6;
}

std::string find_handler(const std::vector<std::string>& hs, std::string_view prefix) {
    for (const auto& h : hs) if (handler_ok(h, prefix)) return h;
    return {};
}

// "1f 0 ffff00000000" -> woorden met het laagste woord eerst; leeg bij iets wat geen hex is.
std::vector<unsigned long long> bitmap_words(std::string_view value) {
    std::vector<unsigned long long> out;
    auto words = split_ws(value);
    for (auto it = words.rbegin(); it != words.rend(); ++it) {
        if (it->size() > 16) return {};
        for (char c : *it) if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        out.push_back(std::strtoull(it->c_str(), nullptr, 16));
    }
    return out;
}

bool bit_set(const std::vector<unsigned long long>& w, int code) {
    if (code < 0) return false;
    const size_t i = static_cast<size_t>(code) / 64;
    return i < w.size() && ((w[i] >> (static_cast<unsigned>(code) % 64)) & 1ULL) != 0;
}

void finish_block(InputDevice& d, bool& any, std::vector<InputDevice>& out) {
    if (any) out.push_back(std::move(d));
    d = InputDevice{};
    any = false;
}

}  // namespace

bool InputDevice::has_key(int code) const { return bit_set(key_bits, code); }
bool InputDevice::has_abs(int code) const { return bit_set(abs_bits, code); }

std::string InputDevice::event_handler() const { return find_handler(handlers, "event"); }
std::string InputDevice::mouse_handler() const { return find_handler(handlers, "mouse"); }

std::vector<InputDevice> parse_input_devices(std::string_view text) {
    std::vector<InputDevice> out;
    InputDevice cur;
    bool any = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) { finish_block(cur, any, out); continue; }
        if (line.size() < 3 || line[1] != ':') continue;
        const char tag = line[0];
        std::string_view rest = line.substr(2);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        switch (tag) {
            case 'I':
                // Een nieuw I:-blok zonder lege regel ervoor: het vorige afsluiten.
                finish_block(cur, any, out);
                any = true;
                hex_field(rest, "Bus", cur.bus);
                hex_field(rest, "Vendor", cur.vendor);
                hex_field(rest, "Product", cur.product);
                hex_field(rest, "Version", cur.version);
                break;
            case 'N': {
                size_t q = rest.find('"');
                size_t q2 = rest.rfind('"');
                if (q != std::string_view::npos && q2 != std::string_view::npos && q2 > q) cur.name = std::string(rest.substr(q + 1, q2 - q - 1));
                break;
            }
            case 'P':
                if (rest.rfind("Phys=", 0) == 0) cur.phys = std::string(rest.substr(5));
                break;
            case 'U':
                if (rest.rfind("Uniq=", 0) == 0) cur.uniq = std::string(rest.substr(5));
                break;
            case 'H':
                if (rest.rfind("Handlers=", 0) == 0) cur.handlers = split_ws(rest.substr(9));
                break;
            case 'B':
                if (rest.rfind("REL=", 0) == 0) { cur.has_rel_bitmap = true; cur.has_rel_xy = xy_bits(rest.substr(4)); }
                else if (rest.rfind("ABS=", 0) == 0) { cur.has_abs_bitmap = true; cur.has_abs_xy = xy_bits(rest.substr(4)); cur.abs_bits = bitmap_words(rest.substr(4)); }
                else if (rest.rfind("KEY=", 0) == 0) cur.key_bits = bitmap_words(rest.substr(4));
                break;
            default: break;
        }
    }
    finish_block(cur, any, out);
    return out;
}

bool mouse_id_valid(std::string_view id) {
    if (id.empty() || id.size() > kMouseIdMax) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' ||
                        c == '+' || c == '@' || c == ',' || c == '=' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::map<std::string, std::vector<std::string>> byid_links(const std::string& byid_dir) {
    // eventN -> namen van by-id-symlinks die ernaar wijzen (alfabetisch, dus deterministisch).
    std::map<std::string, std::vector<std::string>> byid;
    std::error_code ec;
    for (fs::directory_iterator it(byid_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_symlink(e2)) continue;
        fs::path target = fs::read_symlink(it->path(), e2);
        if (e2) continue;
        const std::string ev = target.filename().string();
        if (!handler_ok(ev, "event")) continue;
        const std::string name = it->path().filename().string();
        if (mouse_id_valid(name)) byid[ev].push_back(name);
    }
    for (auto& kv : byid) std::sort(kv.second.begin(), kv.second.end());
    return byid;
}

std::vector<Mouse> list_mice(std::string_view proc_text, const std::string& byid_dir) {
    std::map<std::string, std::vector<std::string>> byid = byid_links(byid_dir);
    for (auto& kv : byid) {
        // Liefst de naam die op een muis wijst ("...-event-mouse").
        std::stable_partition(kv.second.begin(), kv.second.end(), [](const std::string& n) {
            return n.size() >= 12 && n.compare(n.size() - 12, 12, "-event-mouse") == 0;
        });
    }

    std::vector<Mouse> out;
    std::set<std::string> used;
    for (const auto& d : parse_input_devices(proc_text)) {
        if (d.vendor == kOwnVendor) continue;
        const std::string ev = d.event_handler();
        if (ev.empty() || d.mouse_handler().empty()) continue;
        Mouse m;
        m.name = d.name;
        m.event = ev;
        m.phys = d.phys;
        m.bus = d.bus;
        m.vendor = d.vendor;
        m.product = d.product;
        m.absolute = d.has_abs_xy && !d.has_rel_xy;
        m.id = ev;
        auto it = byid.find(ev);
        if (it != byid.end())
            for (const auto& n : it->second)
                if (!used.count(n)) { m.id = n; break; }
        used.insert(m.id);
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const Mouse& a, const Mouse& b) {
        return a.event.size() != b.event.size() ? a.event.size() < b.event.size() : a.event < b.event;
    });
    return out;
}

std::vector<Mouse> list_system_mice() {
    std::ifstream f("/proc/bus/input/devices", std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return list_mice(ss.str());
}

}  // namespace ravengun::sysinfo
