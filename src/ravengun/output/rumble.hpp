#pragma once
// Trillen van de Move bij schieten (terugslag): pure toestandsmachine zonder apparaat of threads, getest in
// tests/test_rumble.cpp. De HID-thread voedt hem met de trekker- en pomptoestand en de tijd; hij geeft de gewenste
// rumblewaarde (0..255) terug. Het eigenlijke uitvoerrapport (LED plus rumble, zie hid_thread.cpp) gaat alleen
// de deur uit als ReportGate dat toestaat: bij verandering van de waarde (met een minimale tussentijd) of als de
// bestaande LED-keepalive aan de beurt is.
//
// Regels:
//  - Een schotpuls (strength, pulse_ms) bij elke down-flank van de trekker; blijft de trekker ingedrukt, dan volgt
//    elke 1/repeat_hz seconde een nieuwe puls (repeat_hz = 0: alleen een puls per schot). Loslaten stopt alleen de
//    herhaling: een puls die loopt maakt zich af (zo geeft een korte tik toch een hele puls).
//  - Een pompflank (herladen met de Sharp Shooter) geeft een kortere, zwakkere puls (pump_strength, helft van
//    pulse_ms, minstens 20 ms); pump_strength = 0 zet hem uit.
//  - Schotpulsen en pomppulsen horen bij een gewapende uitvoer: bij disarm meteen uit. Verbindingsverlies zet
//    alles uit, ook een testpuls. enabled = false geeft altijd 0.
//  - hold (0..1, from the game's own motor output): continuous rumble at hold x strength while armed and connected;
//    it stops as soon as hold goes back to 0 (the caller makes sure it does, see game_out.hpp).
//  - test() geeft een enkele puls, ook ongewapend (voor `ravengunctl rumble test`).
#include <cstdint>
#include <string>

namespace ravengun::output {

struct RumbleConfig {
    bool enabled = true;
    int strength = 180;        // 0..255, schotpuls
    int pulse_ms = 60;         // 10..500
    double repeat_hz = 9.0;    // 0 = geen herhaling; anders 1..30
    int pump_strength = 90;    // 0..255, 0 = geen pomppuls
};

// Leeg = geldig, anders een Nederlandse melding.
std::string validate_rumble(const RumbleConfig& c);

struct RumbleInput {
    bool connected = false;    // Move verbonden en de link niet Lost/Disconnected
    bool armed = false;        // virtuele lightgun gewapend
    bool trigger = false;      // trekker ingedrukt (LiveGunFeed::trigger_down)
    bool pump = false;         // pomp (Sharp Shooter)
    // Continuous rumble driven by the game (its gun motor), 0..1 of `strength`. Only while armed; 0 = none.
    double hold = 0.0;
};

class RumbleEngine {
public:
    const RumbleConfig& config() const { return cfg_; }
    // Live wijzigen; uitgeschakeld stopt alle pulsen meteen.
    void set_config(const RumbleConfig& c);
    // Alles uit en flanken vergeten (bij verbinden).
    void reset();
    // Werkt de toestand bij en geeft de gewenste waarde. Aanroepen bij elk nieuw sample en elke lusronde (de tijd loopt door).
    uint8_t update(int64_t now_ns, const RumbleInput& in);
    // Eén testpuls van ms milliseconden (10..1000) met de ingestelde sterkte (0 = 180). false als uitgeschakeld.
    bool test(int64_t now_ns, int ms);
    uint8_t value() const { return value_; }
    uint64_t pulses() const { return pulses_; }  // aantal gestarte pulsen (schot, herhaling, pomp, test)

private:
    void start_shot(int64_t now_ns);
    RumbleConfig cfg_;
    bool prev_trigger_ = false, prev_pump_ = false, repeating_ = false;  // repeating_: de trekker is gewapend ingedrukt
    int64_t shot_until_ = 0, pump_until_ = 0, test_until_ = 0, next_repeat_ = 0;
    uint8_t test_value_ = 0, value_ = 0;
    uint64_t pulses_ = 0;
};

// Beperkt het aantal uitvoerrapporten (Bluetooth is krap): een rapport als de rumblewaarde anders is dan de laatst
// verstuurde (maar nooit vaker dan min_gap_ns), of als iets anders (LED gewijzigd, keepalive) om een rapport vraagt.
// Zonder verandering gaat er dus alleen de bestaande keepalive heen.
class ReportGate {
public:
    explicit ReportGate(int64_t min_gap_ns = 20'000'000) : min_gap_ns_(min_gap_ns) {}
    bool due(int64_t now_ns, uint8_t want, bool other_due) const;
    // Na een poging. ok = verstuurd; bij false blijft last_sent staan zodat het opnieuw wordt geprobeerd.
    void sent(int64_t now_ns, uint8_t value, bool ok);
    void reset() { last_sent_ = 0; last_attempt_ = kNever; }
    uint8_t last_sent() const { return last_sent_; }
    uint64_t reports() const { return reports_; }
    uint64_t rumble_reports() const { return rumble_reports_; }  // daarvan door een veranderde rumblewaarde

private:
    static constexpr int64_t kNever = INT64_MIN / 2;
    int64_t min_gap_ns_;
    int64_t last_attempt_ = kNever;
    uint8_t last_sent_ = 0;
    uint64_t reports_ = 0, rumble_reports_ = 0;
};

}  // namespace ravengun::output
