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
//  - Knoppenindeling: LiveButtonMap koppelt Move-knoppen aan gun-knoppen (reload/grenade/aux1/aux2),
//    toetsen van het virtuele toetsenbord en recenter; meerdere knoppen op dezelfde actie tellen als OR.
//    Zie config::GunButtonSettings en build_live_map().
//  - Recenter is een verzoek bij de down-flank; de aanroeper voert het uit. De PS-knop is ALTIJD recenter
//    (hard bedraad, ook als de uitvoer niet gewapend is); daarnaast kan een Move-knop in de map "recenter" zijn.
// Bewust NIET gebruikt: de mapping sharp_shooter.buttons uit de kalibratie-UI (eerste meting fout).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/config/settings.hpp"
#include "ravengun/core/input_model.hpp"
#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

struct KeyBinding {
    int code = 0;          // evdev-toetscode
    std::string name;      // canonieke naam ("5")
    uint32_t bits = 0;     // Move-knopbits die deze toets indrukken (OR)
};

struct LiveButtonMap {
    int trigger_on = 140;
    int trigger_off = 100;
    uint32_t gun[kGunButtonCount] = {};   // per GunButton (Trigger ongebruikt): Move-knopbits (OR); 0 = geen
    uint32_t recenter = 0;                // Move-knopbits uit de map (PS komt er altijd bij, zie hierboven)
    std::vector<KeyBinding> keys;
    // Voor weergave (status, startlog): knopnaam -> actie in vaste volgorde, inclusief "ps" -> "recenter (vast)".
    std::vector<std::pair<std::string, std::string>> layout;
};

// Bouwt de live indeling uit de (gevalideerde) instellingen.
LiveButtonMap build_live_map(const config::GunButtonSettings& g);
// Een regel voor het startlog, bv. "move=grenade cross=reload ... ps=recenter(vast)".
std::string describe_layout(const LiveButtonMap& m);

// Move-knopnaam (triangle, circle, cross, square, select, start, ps, move, t) -> bit; 0 bij leeg/onbekend.
uint32_t move_button_bit(const std::string& name);

struct LiveAction {
    bool move = false;          // positie zetten
    double x = 0.5, y = 0.5;    // aim-ruimte, ongeclampt
    bool buttons = false;       // knoptoestand hieronder toepassen
    bool trigger = false, reload = false, grenade = false, aux1 = false, aux2 = false;
    std::vector<std::pair<int, bool>> keys;  // elke gebonden toets (evdev-code) met zijn gewenste toestand
    bool release_all = false;   // bronuitval: alles los
    bool recenter = false;      // down-flank van de recenter-knop (PS of een Move-knop uit de map)
    bool recenter_ps = false;   // ... en die flank kwam van de PS-knop
};

class LiveGunFeed {
public:
    explicit LiveGunFeed(LiveButtonMap m = {}) : map_(std::move(m)) {}
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
