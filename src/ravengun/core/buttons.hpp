#pragma once
// Knopstatus en -flanken die nooit verloren gaan.
//
// Elk HID-rapport levert een volledige knopstatus. De pijplijn voert ieder rapport afzonderlijk
// in (er wordt niets samengevoegd), dus elke wisseling tussen twee rapporten wordt een flank.
// Afnemers (IPC, later de outputbackend) lezen in hun eigen tempo; de accumulator bewaart alle
// flanken sinds de vorige take(), inclusief een snelle druk+los tussen twee takes.
// Beperking: een druk korter dan één rapportinterval (~13 ms) die volledig tussen twee rapporten
// valt, zit niet in de hardwaredata en kan dus niet worden gezien.
#include <array>
#include <cstdint>

namespace ravengun::core {

// Bitposities volgen PSMoveAPI (enum PSMove_Button in psmove.h, commit 256b629).
enum Button : uint32_t {
    kBtnTriangle = 1u << 4,
    kBtnCircle = 1u << 5,
    kBtnCross = 1u << 6,
    kBtnSquare = 1u << 7,
    kBtnSelect = 1u << 8,
    kBtnStart = 1u << 11,
    kBtnPS = 1u << 16,
    kBtnMove = 1u << 19,
    kBtnT = 1u << 20,
    // Virtuele knoppen van de Sharp Shooter (uit de EXT-bytes, zie ext_buttons()); niet in PSMoveAPI.
    kBtnSsRl = 1u << 24,     // RL-bit van de uitbreiding, ext byte 0 bit 0x80. Kwam in de Halo-opname van 2026-10-08 nooit voor
    kBtnSsFire = 1u << 25,   // fire van de Sharp Shooter (de trekker), bit 0x40
    kBtnSel1 = 1u << 26,     // vuurstand 1, bit 0x01
    kBtnSel2 = 1u << 27,     // vuurstand 2, bit 0x02
    kBtnSel3 = 1u << 28,     // vuurstand 3, bit 0x04
    kBtnPump = 1u << 29,     // de pomp: AFGELEID (T boven de drempel zonder fire-bit, ontdenderd), zie SharpShooterPump
};
constexpr uint32_t kAllExtButtons = kBtnSsRl | kBtnSsFire | kBtnSel1 | kBtnSel2 | kBtnSel3 | kBtnPump;

// Uitbreidingsmodus: "auto" (een verbonden EXT-apparaat is de Sharp Shooter; het device-id wordt nog niet
// gelezen), "sharp_shooter" (idem, expliciet) of "none" (EXT-bytes worden genegeerd).
enum class ExtMode { Auto, SharpShooter, None };
const char* to_string(ExtMode m);
bool parse_ext_mode(const char* s, ExtMode& out);
// Virtuele knopbits uit de EXT-bytes (ss_rl, ss_fire, sel1..3; de pomp niet, zie SharpShooterPump); 0 als niet
// verbonden of modus none.
uint32_t ext_buttons(ExtMode mode, bool connected, const uint8_t ext[5]);

// De pomp van de Sharp Shooter heeft geen eigen bit: de Move ziet hem als de trekker-knop T zonder dat het
// fire-bit (ext byte 0, 0x40) staat (PSMoveAPI src/utils/test_extension.c doet het ook zo). Gemeten in de Halo-opname
// (270 s, 131 schoten, 5 pompen): bij elk schot zijn T en fire in HETZELFDE rapport omhoog (T springt van 0 naar 255);
// bij loslaten gaat fire soms 1 rapport eerder omlaag dan T (T dan 1 rapport lang 127). Een pomp is T=255 zonder fire
// gedurende 11 tot 22 rapporten, met een glooiende T (0, 27, 69, 102, 255 ... 189, 81, 24). De trekker is dus het
// fire-bit zelf (0 extra rapporten vertraging); de pomp is T >= on zonder fire gedurende `debounce_reports`
// opeenvolgende rapporten (1 extra rapport vertraging bij 2), tot T <= off of fire. De scheefloop van 1 rapport bij het
// loslaten (T=127 < on) en een T die 1 rapport voor fire omhoog zou gaan, geven zo nooit een pomp; een pomp geeft nooit
// een schot (de trekker komt alleen uit fire).
struct PumpConfig {
    int on = 140;               // T-waarde (0..255) om mee te tellen, gelijk aan gun_buttons.trigger_on
    int off = 100;             // pomp los bij T <= off
    int debounce_reports = 2;  // opeenvolgende rapporten met T >= on zonder fire voordat de pomp "in" is
};

class SharpShooterPump {
public:
    explicit SharpShooterPump(PumpConfig c = {}) : cfg_(c) {}
    // Eén rapport. ext_active: de uitbreiding is verbonden en niet uitgezet. Geeft of de pomp nu ingedrukt is.
    bool update(bool ext_active, uint8_t t_value, bool fire);
    bool down() const { return down_; }
    void reset() { down_ = false; count_ = 0; }
    const PumpConfig& config() const { return cfg_; }

private:
    PumpConfig cfg_;
    bool down_ = false;
    int count_ = 0;
};
constexpr uint32_t kAllMoveButtons =
    kBtnTriangle | kBtnCircle | kBtnCross | kBtnSquare | kBtnSelect | kBtnStart | kBtnPS | kBtnMove | kBtnT;

struct ButtonName { uint32_t bit; const char* name; };
inline constexpr std::array<ButtonName, 15> kButtonNames{{
    {kBtnTriangle, "triangle"}, {kBtnCircle, "circle"}, {kBtnCross, "cross"}, {kBtnSquare, "square"},
    {kBtnSelect, "select"}, {kBtnStart, "start"}, {kBtnPS, "ps"}, {kBtnMove, "move"}, {kBtnT, "t"},
    {kBtnSsRl, "ss_rl"}, {kBtnPump, "pump"}, {kBtnSsFire, "ss_fire"}, {kBtnSel1, "sel1"}, {kBtnSel2, "sel2"}, {kBtnSel3, "sel3"}}};

struct ButtonEdges {
    uint32_t pressed = 0;   // knoppen met minstens één down-flank
    uint32_t released = 0;  // knoppen met minstens één up-flank
    uint32_t state = 0;     // status op het moment van take()
    // Aantal flanken per knopbit (index = bitnummer), zodat ook herhaalde drukken tellen.
    std::array<uint16_t, 32> press_count{};
    std::array<uint16_t, 32> release_count{};
};

class EdgeAccumulator {
public:
    // Nieuwe volledige status; geeft de flanken t.o.v. de vorige status (voor dit ene sample).
    ButtonEdges update(uint32_t state);
    // Alle knoppen los (disconnect, verlies, afsluiten); genereert up-flanken.
    ButtonEdges release_all() { return update(0); }
    // Geeft alles sinds de vorige take() en wist de accumulatie.
    ButtonEdges take();
    uint32_t state() const { return state_; }

private:
    uint32_t state_ = 0;
    ButtonEdges acc_{};
};

}  // namespace ravengun::core
