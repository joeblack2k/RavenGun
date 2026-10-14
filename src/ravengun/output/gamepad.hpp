#pragma once
// Een gamepad (Xbox-controller, Steam Controller, elke evdev-gamepad) naast de lightgun van een speler: voor wat een lightgun mist
// (stuur, gas en rem, extra kastknoppen). Zuivere logica zonder apparaat of threads (getest in tests/test_gamepad.cpp); de
// daemon (src/ravengund/gamepad_service) leest de events en zet de uitkomst door. Zie docs/gamepad.md.
//
// Drie delen:
//  - PadReader: evdev-events (type, code, value) -> PadState: per bediening een waarde 0..1. Knoppen 0 of 1, triggers 0..1,
//    sticks 0..1 met het midden op 0,5 (links/boven = 0) na een dode zone van kPadDeadZone.
//  - GamepadMap: welke bediening welk signaal geeft ([Gamepad] in game.ini; per sleutel over de standaardindeling heen).
//  - apply_gamepad_map: PadState + GamepadMap -> PadOutput (gun-knoppen, start/munt/toetsen, stuur/gas/rem, btn1..8).
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/iobridge/io_state.hpp"
#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

// De bedieningen, met de namen op een Xbox-controller (sleutels in [Gamepad]).
enum class PadControl : int {
    A, B, X, Y, LB, RB, LT, RT, Start, Back, Guide, LS, RS, DPadUp, DPadDown, DPadLeft, DPadRight, LeftX, LeftY, RightX, RightY
};
inline constexpr int kPadControls = 21;
enum class PadKind { Button, Trigger, Stick };

inline constexpr double kPadDeadZone = 0.08;       // sticks: uitslag (0..1 vanaf het midden) die als 0 telt
inline constexpr double kPadButtonThreshold = 0.5; // trigger als knop: ingedrukt vanaf deze waarde
inline constexpr double kPadStickThreshold = 0.5;  // stick als knop: ingedrukt vanaf deze uitslag (0..1)

const char* pad_control_name(PadControl c);                          // "A", "LeftX", ...
bool pad_control_from_name(const std::string& name, PadControl& out); // hoofdletters maken niet uit
PadKind pad_control_kind(PadControl c);

struct PadState {
    std::array<double, kPadControls> v{};
    PadState() { reset(); }
    void reset();   // alles los, sticks in het midden
    double value(PadControl c) const { return v[static_cast<size_t>(c)]; }
    // Ingedrukt: knop 1, trigger >= kPadButtonThreshold, stick met een uitslag >= kPadStickThreshold (beide kanten).
    bool pressed(PadControl c) const;
    bool operator==(const PadState& o) const { return v == o.v; }
    bool operator!=(const PadState& o) const { return !(*this == o); }
};

// Bereik van een evdev-as (EVIOCGABS).
struct PadAxisRange {
    int min = 0, max = 0;
    bool valid() const { return max > min; }
};

class PadReader {
public:
    PadReader();
    // Bereik van een as (ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y). Zonder
    // (geldig) bereik geldt de gewone waarde: sticks -32768..32767, triggers 0..255, hat -1..1.
    void set_range(int abs_code, PadAxisRange r);
    // Verwerkt één event; true bij SYN_REPORT als de toestand sinds de vorige SYN_REPORT veranderd is. SYN_DROPPED: de events tot
    // en met de volgende SYN_REPORT tellen niet en alles gaat los (de toestand is niet meer te vertrouwen).
    bool event(uint16_t type, uint16_t code, int32_t value);
    const PadState& state() const { return state_; }
    void reset();

private:
    double norm(int abs_code, int32_t value) const;
    void compose();
    std::array<PadAxisRange, 64> range_{};   // per ABS-code (< 64)
    std::array<double, 64> abs_{};           // genormaliseerd: sticks -1..1, triggers 0..1, hat -1/0/1
    std::array<bool, kPadControls> btn_{};   // digitale stand per bediening (ook BTN_TL2/BTN_TR2 en BTN_DPAD_*)
    PadState state_;
    bool dropped_ = false;
};

// Waar een bediening heen gaat.
enum class PadTargetKind { None, Wheel, Gas, Brake, Gun, Start, Coin, Key, Btn };
struct PadTarget {
    PadTargetKind kind = PadTargetKind::None;
    int index = 0;   // Gun: GunButton; Key: evdev-code; Btn: 0..7
    bool operator==(const PadTarget& o) const { return kind == o.kind && index == o.index; }
};
// "wheel", "gas", "brake", "trigger", "reload", "grenade", "aux1", "aux2", "start", "coin", "btn1".."btn8", "key:<naam>"
// (output/key_table.hpp) of "none". Hoofdletters maken niet uit. false + Nederlandse melding bij een onbekende waarde.
bool parse_pad_target(const std::string& text, PadTarget& out, std::string& err);
std::string pad_target_name(const PadTarget& t);
// Mag deze bediening dit doel hebben? Een knop kan geen stuur zijn (een stuur heeft een analoge as nodig).
bool pad_target_allowed(PadControl c, const PadTarget& t);

struct GamepadMap {
    std::array<PadTarget, kPadControls> to{};
    const PadTarget& at(PadControl c) const { return to[static_cast<size_t>(c)]; }
    bool operator==(const GamepadMap& o) const { return to == o.to; }
};
// De standaardindeling (docs/gamepad.md): LeftX = wheel, RT = gas, LT = brake, A = trigger, B = reload, X = grenade, Y = aux1,
// LB = btn1, RB = btn2, Start = start, Back = coin. De rest: none.
GamepadMap default_gamepad_map();
// [Gamepad] (sleutel = bediening, waarde = doel) over `base` heen; "none" haalt een bediening weg. Onbekende sleutels of waarden en
// verboden combinaties worden overgeslagen met een melding in `warnings`.
GamepadMap gamepad_map_from(const std::vector<std::pair<std::string, std::string>>& kv, std::vector<std::string>* warnings,
                            const GamepadMap& base = default_gamepad_map());
// Voor weergave: (bediening, doel) in vaste volgorde, zonder "none".
std::vector<std::pair<std::string, std::string>> describe_gamepad_map(const GamepadMap& m);

struct PadOutput {
    std::array<bool, kGunButtonCount> gun{};
    bool start = false, coin = false;
    std::vector<std::pair<int, bool>> keys;   // elke key:-bediening (evdev-code) met zijn stand; een code hoogstens één keer (OR)
    bool wheel_mapped = false, gas_mapped = false, brake_mapped = false;
    double wheel = 0.5, gas = 0, brake = 0;
    std::array<bool, iobridge::kIoPadButtons> btn{};
};
// Meerdere bedieningen op hetzelfde doel: knoppen OR, gas/rem het maximum, stuur de grootste uitslag. Een stick naar gas of rem
// geeft zijn uitslag (beide kanten), een knop naar gas of rem 0 of 1.
PadOutput apply_gamepad_map(const GamepadMap& m, const PadState& s);

}  // namespace ravengun::output
