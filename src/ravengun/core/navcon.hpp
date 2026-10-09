#pragma once
// Navigation Controller (CECH-ZCK1, USB 054c:042f): het invoerrapport via hidraw en de knoppen als bitmasker.
//
// Het rapport heeft de indeling van de Sixaxis (de kernel, hid-sony, behandelt de Navigation Controller als Sixaxis; via
// Bluetooth zet hij het rapport om naar dezelfde vorm). Bron: Linux drivers/hid/hid-sony.c (navigation_rdesc, sixaxis_parse_report)
// en BlueZ plugins/sixaxis.c; alleen offsets, geen code overgenomen. Nog NIET op echte hardware bevestigd (de enige
// Navigation Controller gaf bij het bouwen geen teken van leven); de parser is getest met synthetische rapporten.
//   byte 0: rapport-ID 0x01
//   byte 2: bit 0x02 L3 (stick indrukken), 0x10 omhoog, 0x20 rechts, 0x40 omlaag, 0x80 links (D-pad)
//   byte 3: bit 0x01 L2, 0x04 L1, 0x20 cirkel, 0x40 kruis
//   byte 4: bit 0x01 PS
//   byte 6/7: stick X/Y (0..255, midden 128; omhoog is kleiner)
//   byte 18: L2 analoog (0..255)
#include <cstddef>
#include <cstdint>
#include <string>

namespace ravengun::core {

enum NavButton : uint32_t {
    kNavCross = 1u << 0,
    kNavCircle = 1u << 1,
    kNavL1 = 1u << 2,
    kNavL2 = 1u << 3,
    kNavL3 = 1u << 4,
    kNavPS = 1u << 5,
    kNavUp = 1u << 6,
    kNavDown = 1u << 7,
    kNavLeft = 1u << 8,
    kNavRight = 1u << 9,
    kNavStickUp = 1u << 10,
    kNavStickDown = 1u << 11,
    kNavStickLeft = 1u << 12,
    kNavStickRight = 1u << 13,
};

struct NavButtonName { uint32_t bit; const char* name; };
inline constexpr NavButtonName kNavButtonNames[] = {
    {kNavCross, "nav_cross"}, {kNavCircle, "nav_circle"}, {kNavL1, "nav_l1"}, {kNavL2, "nav_l2"}, {kNavL3, "nav_l3"},
    {kNavPS, "nav_ps"}, {kNavUp, "nav_up"}, {kNavDown, "nav_down"}, {kNavLeft, "nav_left"}, {kNavRight, "nav_right"},
    {kNavStickUp, "nav_stick_up"}, {kNavStickDown, "nav_stick_down"}, {kNavStickLeft, "nav_stick_left"},
    {kNavStickRight, "nav_stick_right"}};
uint32_t nav_button_bit(const std::string& name);  // 0 bij een onbekende naam

struct NavState {
    uint32_t buttons = 0;   // NavButton-bits, inclusief de stick als vier richtingen
    int stick_x = 128, stick_y = 128;
    int l2 = 0;             // analoog
};

// Stick als richting: aan voorbij 64 van het midden, uit binnen 40 (hysterese, geen klapperen rond de drempel).
inline constexpr int kNavStickOn = 64, kNavStickOff = 40;

// Parseert een invoerrapport (met rapport-ID). prev: de vorige toestand (voor de hysterese van de stick).
// false bij een te kort rapport of een ander rapport-ID (dan blijft out ongewijzigd).
bool parse_nav_report(const uint8_t* buf, size_t n, const NavState& prev, NavState& out);

}  // namespace ravengun::core
