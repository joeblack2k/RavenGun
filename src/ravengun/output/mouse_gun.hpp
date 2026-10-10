#pragma once
// Een gewone systeemmuis als bron voor de virtuele lightgun van een speler: een zuivere toestandsmachine die evdev-events
// (type, code, value) verwerkt en bij elke EV_SYN/SYN_REPORT met een wijziging een LiveAction oplevert (dezelfde vorm als de
// live Move, zie live_feed.hpp). Geen apparaat, geen threads: getest in tests/test_mouse.cpp. Zie "Muis als lightgun" in
// docs/architecture.md.
//
// Positie (aim-ruimte 0..1, linksboven 0,0), altijd geclampt:
//  - Relatieve muis: REL_X/REL_Y verplaatsen de positie. `counts_per_screen` counts = een volle schermbreedte; Y gebruikt
//    dezelfde fysieke schaal via `screen_aspect` (breedte/hoogte, standaard 16/9): een count is horizontaal 1/counts_per_screen
//    van het scherm en verticaal aspect/counts_per_screen.
//  - Absoluut apparaat (tablet, touchscreen): ABS_X/ABS_Y worden met min/max (EVIOCGABS) genormaliseerd naar 0..1.
// Knoppen: BTN_LEFT = trekker, BTN_RIGHT = herladen, BTN_MIDDLE = granaat, BTN_SIDE = aux1, BTN_EXTRA = aux2.
// Startpositie: het midden.
#include <cstdint>
#include <optional>

#include "ravengun/output/live_feed.hpp"

namespace ravengun::output {

inline constexpr double kMouseCountsDefault = 1920.0;
inline constexpr double kMouseCountsMin = 100.0;
inline constexpr double kMouseCountsMax = 20000.0;

struct AbsAxis {
    int min = 0, max = 0;
    bool valid() const { return max > min; }
};

struct MouseGunConfig {
    double counts_per_screen = kMouseCountsDefault;  // REL: counts voor een volle schermbreedte
    double screen_aspect = 16.0 / 9.0;               // breedte/hoogte van het scherm (voor de Y-schaal)
    AbsAxis abs_x, abs_y;                            // ABS: bereik van de assen; ongeldig = ABS-events negeren
};

class MouseGun {
public:
    explicit MouseGun(const MouseGunConfig& cfg = {}) : cfg_(cfg) { clamp_cfg(); }

    // Nieuwe instellingen; positie en knoppen blijven.
    void configure(const MouseGunConfig& cfg) { cfg_ = cfg; clamp_cfg(); }
    const MouseGunConfig& config() const { return cfg_; }

    // Verwerkt één evdev-event (type, code, value zoals in struct input_event). Geeft bij SYN_REPORT een actie als positie of
    // knoppen sinds de vorige zijn veranderd. SYN_DROPPED: de events tot en met de volgende SYN_REPORT worden genegeerd en
    // alle knoppen gaan los (de toestand is niet meer te vertrouwen).
    std::optional<LiveAction> event(uint16_t type, uint16_t code, int32_t value);

    // De huidige positie en knoppen als actie (om na het openen van het apparaat de gun in te stellen).
    LiveAction snapshot() const;
    // Bronuitval: een actie met release_all; de interne knopstand gaat naar los, de positie blijft.
    LiveAction release();

    double x() const { return x_; }
    double y() const { return y_; }
    bool button(GunButton b) const { return down_[static_cast<int>(b)]; }

private:
    void clamp_cfg();
    LiveAction make() const;

    MouseGunConfig cfg_;
    double x_ = 0.5, y_ = 0.5;
    bool down_[kGunButtonCount] = {};
    bool dirty_ = false;
    bool dropped_ = false;
};

}  // namespace ravengun::output
