#pragma once
// Live koppeling van de pijplijn aan de virtuele lightgun (fase 4): van een InputSample naar wat de
// uitvoer moet doen. Pure logica zonder apparaat of threads (getest in tests/test_output.cpp).
//
// Regels:
//  - De aim gaat alleen naar de uitvoer als hij geldig is (aim.valid: verse link, oriëntatie en midden).
//    Is hij ongeldig, dan stopt het bewegen (geen positie, geen sprong naar een hoek).
//  - De trekker komt uit de analoge T-waarde (0..255) met drempel en hysterese; met een actieve Sharp Shooter-
//    uitbreiding (ext_active) is hij het fire-bit van de uitbreiding en is T zonder fire de pomp (knop "pump").
//    Reload en grenade uit instelbare Move-knoppen (standaard Kruis en Vierkant). Knoppen volgen de pijplijn: bij "stale" (gat
//    tot 250 ms) blijven ze staan, bij "lost" en "disconnected" worden alle gun-knoppen vrijgegeven.
//  - Knoppenindeling: LiveButtonMap koppelt Move-knoppen aan gun-knoppen (reload/grenade/aux1/aux2),
//    toetsen van het virtuele toetsenbord en recenter; meerdere knoppen op dezelfde actie tellen als OR.
//    Zie config::GunButtonSettings en build_live_map().
//  - Recenter is een verzoek bij de down-flank; de aanroeper voert het uit. De PS-knop is ALTIJD recenter
//    (hard bedraad, ook als de uitvoer niet gewapend is); daarnaast kan een Move-knop in de map "recenter" zijn.
// Bewust NIET gebruikt: de mapping sharp_shooter.buttons uit de kalibratie-UI (eerste meting fout).
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/config/settings.hpp"
#include "ravengun/core/input_model.hpp"
#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

// Knopbits in de live koppeling: de lage 32 bits zijn de Move-knoppen (core/buttons.hpp), de hoge 32 de knoppen van de
// Navigation Controller van dezelfde speler (core/navcon.hpp, naam "nav_..."), zodat beide in één indeling staan.
inline constexpr int kNavBitShift = 32;

struct KeyBinding {
    int code = 0;          // evdev-toetscode
    std::string name;      // canonieke naam ("5")
    uint64_t bits = 0;     // knopbits die deze toets indrukken (OR)
};

struct LiveButtonMap {
    int trigger_on = 140;
    int trigger_off = 100;
    uint64_t gun[kGunButtonCount] = {};   // per GunButton (Trigger ongebruikt): knopbits (OR); 0 = geen
    uint64_t recenter = 0;                // knopbits uit de map (PS komt er altijd bij, zie hierboven)
    std::vector<KeyBinding> keys;
    // Voor weergave (status, startlog): knopnaam -> actie in vaste volgorde, inclusief "ps" -> "recenter (vast)".
    std::vector<std::pair<std::string, std::string>> layout;
};

// Bouwt de live indeling uit de (gevalideerde) instellingen.
LiveButtonMap build_live_map(const config::GunButtonSettings& g);
// Een regel voor het startlog, bv. "move=grenade cross=reload ... ps=recenter(vast)".
std::string describe_layout(const LiveButtonMap& m);

// Knopnaam -> bit in het gecombineerde masker: Move-knoppen (triangle, circle, ..., t) in de lage 32 bits, "nav_..." in de
// hoge 32; 0 bij leeg/onbekend.
uint64_t move_button_bit(const std::string& name);

struct LiveAction {
    bool move = false;          // positie zetten
    double x = 0.5, y = 0.5;    // aim-ruimte, ongeclampt
    // Voor de uitvoerklok (aim_clock.hpp): snelheid van de aim (per seconde), ontvangsttijd en extra transportvertraging.
    double vx = 0, vy = 0, age_ms = 0;
    int64_t t_ns = 0;
    bool buttons = false;       // knoptoestand hieronder toepassen
    bool trigger = false, reload = false, grenade = false, aux1 = false, aux2 = false;
    std::vector<std::pair<int, bool>> keys;  // elke gebonden toets (evdev-code) met zijn gewenste toestand
    bool release_all = false;   // bronuitval: alles los
    bool recenter = false;      // down-flank van de recenter-knop (PS of een Move-knop uit de map)
    bool recenter_ps = false;   // ... en die flank kwam van de PS-knop
};

// Override-laag voor synthetische invoer (ravengunctl output-button/key, webinterface) naast de live Move-rapporten.
// Zonder deze laag zet elk Move-rapport (~80 Hz) alle gebonden knoppen en toetsen terug naar zijn eigen stand, zodat een
// synthetische druk op iets wat de Move ook koppelt na enkele milliseconden weer los was. Regels:
//  - Wat de uitvoer krijgt is OR van live en synthetisch: een live loslaten wist een synthetische druk niet, een live
//    druk blijft werken zoals altijd.
//  - Synthetisch is "down" (tot een expliciete up) of "tap" (tot de aanroeper het einde meldt met het token). Een nieuwere
//    tap vervangt de oudere: het einde van de oude tap laat de nieuwe staan (token komt niet meer overeen).
//  - up wist zowel down als lopende taps; clear() (disarm, stop, bronuitval, nieuwe arm) wist alles, ook de live stand.
//  - De live stand wordt per bron bijgehouden (0 = de Move, 1 = een muis als lightgun) en telt als OR: stuurt een Move EN een
//    muis dezelfde speler, dan wist een Move-rapport dat "los" meldt (~80 Hz) een vastgehouden muisknop niet (en omgekeerd).
// Niet thread-safe: de eigenaar (OutputService) serialiseert met zijn mutex. Getest in tests/test_output.cpp.
class OverrideLayer {
public:
    static constexpr int kLiveSources = 2;   // 0 = Move, 1 = muis
    // Live stand van een knop/toets (per rapport van bron `src`); geeft terug wat er naar de uitvoer moet (live van alle bronnen OR
    // synthetisch).
    bool live_button(GunButton b, bool live, int src = 0) {
        live_src_[static_cast<size_t>(src < 0 || src >= kLiveSources ? 0 : src)][idx(b)] = live;
        bool any = false;
        for (const auto& s : live_src_) any = any || s[idx(b)];
        live_btn_[idx(b)] = any;
        return any || held_button(b);
    }
    bool live_key(int code, bool live);
    // Wat de uitvoer nu hoort te hebben (live OR synthetisch), zonder nieuw rapport.
    bool want_button(GunButton b) const { return live_btn_[idx(b)] || held_button(b); }
    bool want_key(int code) const;
    // Alleen het synthetische deel.
    bool held_button(GunButton b) const { const Hold& h = btn_[idx(b)]; return h.down || h.tap != 0; }
    bool held_key(int code) const;

    void hold_button(GunButton b) { btn_[idx(b)].down = true; }
    uint64_t tap_button(GunButton b) { return btn_[idx(b)].tap = ++next_token_; }
    void end_tap_button(GunButton b, uint64_t token) { Hold& h = btn_[idx(b)]; if (h.tap == token) h.tap = 0; }
    void release_button(GunButton b) { btn_[idx(b)] = {}; }
    void hold_key(int code) { keys_[code].down = true; }
    uint64_t tap_key(int code) { return keys_[code].tap = ++next_token_; }
    void end_tap_key(int code, uint64_t token);
    void release_key(int code) { keys_.erase(code); }

    bool any_synthetic() const;
    void clear();   // alles, ook de live stand (die volgt bij het volgende rapport)

private:
    struct Hold { bool down = false; uint64_t tap = 0; };
    static size_t idx(GunButton b) { return static_cast<size_t>(b); }
    std::array<Hold, kGunButtonCount> btn_{};
    std::array<bool, kGunButtonCount> live_btn_{};                            // OR van de bronnen hieronder
    std::array<std::array<bool, kGunButtonCount>, kLiveSources> live_src_{};  // live stand per bron
    std::map<int, Hold> keys_;      // synthetisch per toets
    std::map<int, bool> live_keys_;
    uint64_t next_token_ = 0;
};

class LiveGunFeed {
public:
    explicit LiveGunFeed(LiveButtonMap m = {}) : map_(std::move(m)) {}
    // nav = de knoppen (core::NavButton) van de Navigation Controller die aan deze speler is toegewezen (0 = geen).
    LiveAction update(const core::InputSample& s, uint32_t nav = 0);
    void reset() { trigger_down_ = false; prev_nav_ = 0; }
    const LiveButtonMap& map() const { return map_; }
    void set_map(const LiveButtonMap& m) { map_ = m; }
    bool trigger_down() const { return trigger_down_; }

private:
    LiveButtonMap map_;
    bool trigger_down_ = false;
    uint32_t prev_nav_ = 0;  // voor de flank van een nav-knop met actie recenter
};

}  // namespace ravengun::output
