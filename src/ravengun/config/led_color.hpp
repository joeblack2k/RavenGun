#pragma once
// LED-kleur en de bijbehorende tracker-tint. De boltracker zoekt op tint (chroma-hoek in het
// camerabeeld), dus een andere LED-kleur zonder andere tint betekent dat de bol niet meer
// gevonden wordt. Deze module koppelt beide op een plek; de gemeten tinten staan in
// docs/decisions.md (sectie "LED-kleur").
#include <string>
#include <vector>

namespace ravengun::config {

struct LedPreset {
    const char* name;  // cyaan, magenta, rood, groen, blauw, wit
    int r, g, b;
};
const std::vector<LedPreset>& led_presets();

struct LedChoice {
    std::string preset;     // naam van een preset, of "custom"
    int r = 0, g = 0, b = 0;
    bool hue_known = false; // false: de tracker-tint blijft ongewijzigd (wit, bijna grijs, uit)
    double hue_deg = 0;     // tint voor de tracker (camera-UV)
    double hue_tol_deg = 30;
    std::string hue_source; // "gemeten", "nominaal" of "geen"
    std::vector<std::string> warnings;
};

inline constexpr double kPresetHueTolDeg = 30;
inline constexpr double kCustomHueTolDeg = 45;

// Preset op naam (hoofdletterongevoelig, ook Engelse namen). false bij een onbekende naam.
bool resolve_led_preset(const std::string& name, LedChoice& out);
// RGB 0..255. Komt de kleur exact overeen met een preset, dan is het die preset (gemeten tint);
// anders "custom" met de nominale tint, een ruimere tolerantie en een waarschuwing.
LedChoice resolve_led_rgb(int r, int g, int b);

}  // namespace ravengun::config
