#include "ravengun/sysinfo/navcon.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ravengun/json.hpp"

namespace ravengun::sysinfo {

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p, size_t max = 4096) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string s(max, '\0');
    f.read(s.data(), static_cast<std::streamsize>(max));
    s.resize(static_cast<size_t>(f.gcount()));
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool is_navcon(const std::string& uevent) {
    std::string u = lower(uevent);
    if (u.find("navigation controller") != std::string::npos) return true;
    // HID_ID=0005:0000054C:0000042F (Bluetooth) of 0003:... (USB): leverancier Sony, product 042F.
    size_t at = u.find("hid_id=");
    if (at == std::string::npos) return false;
    std::string line = u.substr(at, u.find('\n', at) - at);
    return line.find("0000054c") != std::string::npos && line.find("0000042f") != std::string::npos;
}

}  // namespace

NavconStatus classify_battery(int percent, const std::string& power_status) {
    NavconStatus n;
    n.connected = true;
    n.percent = percent >= 0 && percent <= 100 ? percent : -1;
    std::string st = lower(power_status);
    n.status = st == "charging" ? "charging" : st == "discharging" ? "discharging" : st == "full" ? "full" : "unknown";
    if (n.status == "charging") { n.code = "charging"; n.text = "laadt"; }
    else if (n.status == "full") { n.code = "charged"; n.text = "vol (aan de lader)"; }
    else if (n.percent < 0) { n.code = "unknown"; n.text = "onbekend"; }
    else {
        n.code = n.percent < 20 ? "min" : n.percent < 40 ? "ge20" : n.percent < 60 ? "ge40" : n.percent < 80 ? "ge60" : n.percent < 100 ? "ge80" : "max";
        n.text = std::to_string(n.percent) + " procent";
    }
    return n;
}

NavconStatus read_navcon(const std::string& root) {
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path dir = it->path();
        if (!is_navcon(slurp(dir / "device" / "uevent"))) continue;
        int pct = -1;
        std::string cap = trim(slurp(dir / "capacity", 16));
        if (!cap.empty() && std::all_of(cap.begin(), cap.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            pct = std::stoi(cap);
        return classify_battery(pct, trim(slurp(dir / "status", 32)));
    }
    return {};
}

std::string navcon_json(const NavconStatus& n) {
    json::Writer w;
    w.begin_object().key("connected").value(n.connected);
    w.key("battery").begin_object();
    if (n.connected) {
        if (n.percent >= 0) w.key("percent").value(n.percent); else w.key("percent").null();
        w.key("code").value(n.code).key("text").value(n.text).key("charging").value(n.status == "charging");
    } else {
        w.key("percent").null().key("code").value("unknown").key("text").value("niet verbonden").key("charging").value(false);
    }
    w.end_object().end_object();
    return w.str();
}

}  // namespace ravengun::sysinfo
