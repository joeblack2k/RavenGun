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
    kBtnPump = 1u << 24,     // RL (pomp/herladen), ext byte 0 bit 0x80
    kBtnSsFire = 1u << 25,   // fire van de Sharp Shooter, bit 0x40
    kBtnSel1 = 1u << 26,     // vuurstand 1, bit 0x01
    kBtnSel2 = 1u << 27,     // vuurstand 2, bit 0x02
    kBtnSel3 = 1u << 28,     // vuurstand 3, bit 0x04
};
constexpr uint32_t kAllExtButtons = kBtnPump | kBtnSsFire | kBtnSel1 | kBtnSel2 | kBtnSel3;

// Uitbreidingsmodus: "auto" (een verbonden EXT-apparaat is de Sharp Shooter; het device-id wordt nog niet
// gelezen), "sharp_shooter" (idem, expliciet) of "none" (EXT-bytes worden genegeerd).
enum class ExtMode { Auto, SharpShooter, None };
const char* to_string(ExtMode m);
bool parse_ext_mode(const char* s, ExtMode& out);
// Virtuele knopbits uit de EXT-bytes; 0 als niet verbonden of modus none.
uint32_t ext_buttons(ExtMode mode, bool connected, const uint8_t ext[5]);
constexpr uint32_t kAllMoveButtons =
    kBtnTriangle | kBtnCircle | kBtnCross | kBtnSquare | kBtnSelect | kBtnStart | kBtnPS | kBtnMove | kBtnT;

struct ButtonName { uint32_t bit; const char* name; };
inline constexpr std::array<ButtonName, 14> kButtonNames{{
    {kBtnTriangle, "triangle"}, {kBtnCircle, "circle"}, {kBtnCross, "cross"}, {kBtnSquare, "square"},
    {kBtnSelect, "select"}, {kBtnStart, "start"}, {kBtnPS, "ps"}, {kBtnMove, "move"}, {kBtnT, "t"},
    {kBtnPump, "pump"}, {kBtnSsFire, "ss_fire"}, {kBtnSel1, "sel1"}, {kBtnSel2, "sel2"}, {kBtnSel3, "sel3"}}};

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
