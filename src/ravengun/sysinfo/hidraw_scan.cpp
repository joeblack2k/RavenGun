#include "ravengun/sysinfo/hidraw_scan.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace ravengun::sysinfo {

namespace fs = std::filesystem;

std::string HidrawDevice::type() const {
    if (vendor != kSonyVendor) return {};
    if (product == kMoveZcm1 || product == kMoveZcm2) return "move";
    if (product == kNavigation) return "navigation";
    return {};
}

std::string HidrawDevice::model() const {
    if (vendor != kSonyVendor) return {};
    return product == kMoveZcm1 ? "ZCM1" : product == kMoveZcm2 ? "ZCM2" : "";
}

bool parse_hid_uevent(const std::string& text, HidrawDevice& out) {
    bool have_id = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.rfind("HID_ID=", 0) == 0) {
            unsigned b = 0, v = 0, p = 0;
            char tail = 0;
            if (std::sscanf(line.c_str() + 7, "%x:%x:%x%c", &b, &v, &p, &tail) == 3) { out.bus = b; out.vendor = v; out.product = p; have_id = true; }
        } else if (line.rfind("HID_NAME=", 0) == 0) {
            out.name = line.substr(9);
        } else if (line.rfind("HID_UNIQ=", 0) == 0) {
            out.uniq = line.substr(9);
            for (auto& c : out.uniq) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return have_id;
}

std::vector<HidrawDevice> scan_sony_hidraw(const std::string& root, const std::string& dev_dir) {
    std::vector<HidrawDevice> out;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("hidraw", 0) != 0) continue;
        std::ifstream f(it->path() / "device" / "uevent");
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        HidrawDevice d;
        if (!parse_hid_uevent(text, d) || d.type().empty()) continue;
        d.sysname = name;
        d.node = dev_dir + "/" + name;
        out.push_back(std::move(d));
    }
    std::sort(out.begin(), out.end(), [](const HidrawDevice& a, const HidrawDevice& b) { return a.node < b.node; });
    return out;
}

}  // namespace ravengun::sysinfo
