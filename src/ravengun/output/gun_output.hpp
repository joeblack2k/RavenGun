#pragma once
// Outputlogica voor het virtuele lightgun-apparaat, los van uinput (testbaar met een nep-apparaat).
//
// Afspraken (agentprompt sectie 13):
//  - Standaard DISARMED: er gaan geen events naar het apparaat (set_position/set_button weigeren).
//  - Bij disarm, schrijffouten, shutdown en een door de aanroeper gemelde bronuitval
//    (release_all) worden alle ingedrukte knoppen en toetsen vrijgegeven.
//  - Geen feedbackloop: deze laag leest nooit uit het apparaat (GunDevice heeft geen leesfunctie).
//  - Eén eigenaar tegelijk: de klasse is niet thread-safe; de daemon serialiseert met een mutex.
#include <array>
#include <cstdint>
#include <set>
#include <string>

#include "ravengun/output/gun_transform.hpp"
#include "ravengun/output/uinput_kbd.hpp"

namespace ravengun::output {

// Logische knoppen. De evdev-codes (BTN_LEFT, ...) horen bij de uinput-backend; TeknoParrot
// herkent BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA van een muis-achtig apparaat als MouseButton.
enum class GunButton : int { Trigger = 0, Reload, Grenade, Aux1, Aux2 };
inline constexpr int kGunButtonCount = 5;
const char* to_string(GunButton b);                       // "trigger", "reload", "grenade", "aux1", "aux2"
bool parse_button(const std::string& name, GunButton& b);

// Abstract uitvoerapparaat. Alleen schrijven; lezen is bewust niet mogelijk.
class GunDevice {
public:
    virtual ~GunDevice() = default;
    virtual bool create(std::string& err) = 0;     // maakt het apparaat (idempotent als het al bestaat)
    virtual void destroy() = 0;                    // verwijdert het apparaat (kernel geeft knoppen vrij)
    virtual bool created() const = 0;
    virtual bool write_position(int x, int y) = 0;  // ABS_X, ABS_Y, SYN_REPORT
    virtual bool write_button(GunButton b, bool down) = 0;  // knop + SYN_REPORT
};

struct GunOutputStats {
    uint64_t position_events = 0;
    uint64_t button_events = 0;
    uint64_t rejected = 0;        // genegeerd omdat disarmed, niet-eindig of ongeldige transformatie
    uint64_t write_errors = 0;
    uint64_t releases = 0;        // aantal keer dat knoppen bij uitval/disarm zijn vrijgegeven
    uint64_t arms = 0;
};

class GunOutput {
public:
    // 'kbd' (optioneel) is het virtuele toetsenbord: het wordt samen met de gun aangemaakt (arm) en
    // verwijderd (shutdown/fout); toetsen gaan alleen door als de uitvoer gewapend is.
    explicit GunOutput(GunDevice& dev, KeyDevice* kbd = nullptr) : dev_(dev), kbd_(kbd) {}
    ~GunOutput() { shutdown(); }
    GunOutput(const GunOutput&) = delete;
    GunOutput& operator=(const GunOutput&) = delete;

    // Maakt zo nodig het apparaat en schakelt events in. false bij een fout (blijft disarmed).
    bool arm(std::string& err);
    // Geeft alle knoppen vrij en schakelt events uit. Het apparaat blijft bestaan (stabiele binding).
    void disarm();
    // Disarm en verwijdert het apparaat.
    void shutdown();

    bool armed() const { return armed_; }
    bool device_created() const { return dev_.created(); }

    // Positie in aim-ruimte (genormaliseerd scherm, ongeclampt), via de transformatie. Stuurt alleen
    // een event als de gehele waarde verandert (of na arm/herstel). false = niets verstuurd.
    bool set_position(double nx, double ny);
    // Knop in/uit. Alleen events bij een werkelijke wijziging.
    bool set_button(GunButton b, bool down);
    // Toets (evdev-code) in/uit op het virtuele toetsenbord. Alleen events bij een wijziging; false bij
    // disarmed of zonder toetsenbord.
    bool set_key(int code, bool down);
    bool key_down(int code) const { return keys_down_.count(code) != 0; }
    const std::set<int>& keys_down() const { return keys_down_; }
    bool keyboard_created() const { return kbd_ && kbd_->created(); }
    // Fout bij het aanmaken van het toetsenbord bij de laatste arm (leeg = goed of geen toetsenbord).
    const std::string& keyboard_error() const { return kbd_error_; }
    // Bronuitval (Move weg, patroon gestopt): alle ingedrukte knoppen vrijgeven; blijft armed.
    void release_all();

    const GunTransform& transform() const { return t_; }
    void set_transform(const GunTransform& t) { t_ = t; have_last_ = false; }

    bool button_down(GunButton b) const { return down_[static_cast<size_t>(b)]; }
    bool have_position() const { return last_valid_; }  // er is ooit een geldige positie gezet
    double last_nx() const { return last_nx_; }
    double last_ny() const { return last_ny_; }
    int last_abs_x() const { return last_x_; }
    int last_abs_y() const { return last_y_; }
    const GunOutputStats& stats() const { return stats_; }
    const std::string& last_error() const { return last_error_; }

private:
    void fail(const char* what);  // schrijffout: knoppen vrijgeven (best effort), apparaat weg, disarmed

    GunDevice& dev_;
    KeyDevice* kbd_ = nullptr;
    std::set<int> keys_down_;
    std::string kbd_error_;
    GunTransform t_;
    bool armed_ = false;
    std::array<bool, kGunButtonCount> down_{};
    bool have_last_ = false;      // laatste abs-waarde is bekend bij het apparaat (voor deduplicatie)
    bool last_valid_ = false;     // er is ooit een positie gezet (voor status)
    int last_x_ = 0, last_y_ = 0;
    double last_nx_ = 0, last_ny_ = 0;
    GunOutputStats stats_;
    std::string last_error_;
};

}  // namespace ravengun::output
