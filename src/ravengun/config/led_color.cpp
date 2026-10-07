#include "ravengun/config/led_color.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "ravengun/vision/ball_tracker.hpp"

namespace ravengun::config {

const std::vector<LedPreset>& led_presets() {
    static const std::vector<LedPreset> p = {
        {"cyaan", 0, 255, 255}, {"magenta", 255, 0, 255}, {"rood", 255, 0, 0},
        {"groen", 0, 255, 0},   {"blauw", 0, 0, 255},     {"wit", 255, 255, 255},
    };
    return p;
}

namespace {

std::string canonical(std::string n) {
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "cyan") return "cyaan";
    if (n == "red") return "rood";
    if (n == "green") return "groen";
    if (n == "blue") return "blauw";
    if (n == "white") return "wit";
    return n;
}

void fill_preset(const LedPreset& p, LedChoice& out) {
    out = LedChoice{};
    out.preset = p.name;
    out.r = p.r; out.g = p.g; out.b = p.b;
    if (out.preset == "wit") {
        out.hue_known = false;
        out.hue_source = "geen";
        out.warnings.push_back(
            "Wit heeft geen tint: de boltracking (die op kleur zoekt) vindt de bol niet. Gebruik wit alleen om te kijken of de LED brandt.");
        return;
    }
    out.hue_known = true;
    out.hue_deg = vision::measured_hue_deg(out.preset);  // gemeten; alle presets behalve wit hebben een meting
    out.hue_tol_deg = kPresetHueTolDeg;
    out.hue_source = "gemeten";
}

}  // namespace

bool resolve_led_preset(const std::string& name, LedChoice& out) {
    std::string n = canonical(name);
    for (const auto& p : led_presets())
        if (n == p.name) { fill_preset(p, out); return true; }
    return false;
}

LedChoice resolve_led_rgb(int r, int g, int b) {
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    for (const auto& p : led_presets())
        if (p.r == r && p.g == g && p.b == b) { LedChoice c; fill_preset(p, c); return c; }
    LedChoice c;
    c.preset = "custom";
    c.r = r; c.g = g; c.b = b;
    int mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx == 0) {
        c.hue_source = "geen";
        c.warnings.push_back("De LED staat uit (0,0,0): de tracker vindt de bol niet. De tracker-tint is niet gewijzigd.");
        return c;
    }
    if (mx - mn < 96) {
        c.hue_source = "geen";
        c.warnings.push_back(
            "Te weinig kleurverzadiging (verschil tussen het sterkste en zwakste kanaal onder 96): de bol heeft in het beeld bijna geen tint, "
            "dus de tracker vindt hem waarschijnlijk niet. De tracker-tint is niet gewijzigd.");
        return c;
    }
    c.hue_known = true;
    c.hue_deg = vision::nominal_hue_deg(vision::Rgb{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)});
    c.hue_tol_deg = kCustomHueTolDeg;
    c.hue_source = "nominaal";
    char buf[400];
    std::snprintf(buf, sizeof buf,
                  "Eigen kleur: de tint in het camerabeeld (nominaal %.0f graden) is niet gemeten en kan tot ongeveer 25 graden afwijken "
                  "(magenta deed dat). De tolerantie is daarom %.0f in plaats van %.0f graden. Controleer op het tabblad Camera of de bol gevonden wordt.",
                  c.hue_deg, kCustomHueTolDeg, kPresetHueTolDeg);
    c.warnings.push_back(buf);
    return c;
}

}  // namespace ravengun::config
