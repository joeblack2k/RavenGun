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
};
constexpr uint32_t kAllMoveButtons =
    kBtnTriangle | kBtnCircle | kBtnCross | kBtnSquare | kBtnSelect | kBtnStart | kBtnPS | kBtnMove | kBtnT;

struct ButtonName { uint32_t bit; const char* name; };
inline constexpr std::array<ButtonName, 9> kButtonNames{{
    {kBtnTriangle, "triangle"}, {kBtnCircle, "circle"}, {kBtnCross, "cross"}, {kBtnSquare, "square"},
    {kBtnSelect, "select"}, {kBtnStart, "start"}, {kBtnPS, "ps"}, {kBtnMove, "move"}, {kBtnT, "t"}}};

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
