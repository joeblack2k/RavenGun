#pragma once
// Live koppeling van de pijplijn aan de virtuele lightgun (fase 4): van een InputSample naar wat de
// uitvoer moet doen. Pure logica zonder apparaat of threads (getest in tests/test_output.cpp).
//
// Regels:
//  - De aim gaat alleen naar de uitvoer als hij geldig is (aim.valid: verse link, oriëntatie en midden).
//    Is hij ongeldig, dan stopt het bewegen (geen positie, geen sprong naar een hoek).
//  - De trekker komt uit de analoge T-waarde (0..255) met drempel en hysterese; reload en grenade uit
//    instelbare Move-knoppen (standaard Kruis en Vierkant). Knoppen volgen de pijplijn: bij "stale" (gat
//    tot 250 ms) blijven ze staan, bij "lost" en "disconnected" worden alle gun-knoppen vrijgegeven.
//  - Recenter (standaard de Move-knop) is een verzoek bij de down-flank; de aanroeper voert het uit.
// Bewust NIET gebruikt: de mapping sharp_shooter.buttons uit de kalibratie-UI (eerste meting fout).
#include <cstdint>
#include <string>

#include "ravengun/core/input_model.hpp"

namespace ravengun::output {

struct LiveButtonMap {
    int trigger_on = 140;
    int trigger_off = 100;
    uint32_t reload = 0, grenade = 0, recenter = 0;  // Move-knopbits (0 = uit)
};

// Move-knopnaam (triangle, circle, cross, square, select, start, ps, move, t) -> bit; 0 bij leeg/onbekend.
uint32_t move_button_bit(const std::string& name);

struct LiveAction {
    bool move = false;          // positie zetten
    double x = 0.5, y = 0.5;    // aim-ruimte, ongeclampt
    bool buttons = false;       // knoptoestand hieronder toepassen
    bool trigger = false, reload = false, grenade = false;
    bool release_all = false;   // bronuitval: alles los
    bool recenter = false;      // down-flank van de recenter-knop
};

class LiveGunFeed {
public:
    explicit LiveGunFeed(LiveButtonMap m = {}) : map_(m) {}
    LiveAction update(const core::InputSample& s);
    void reset() { trigger_down_ = false; }
    const LiveButtonMap& map() const { return map_; }
    void set_map(const LiveButtonMap& m) { map_ = m; }
    bool trigger_down() const { return trigger_down_; }

private:
    LiveButtonMap map_;
    bool trigger_down_ = false;
};

}  // namespace ravengun::output
