// SPDX-License-Identifier: GPL-3.0-or-later
// Het model van een Namco USIO-bord voor rg-usio.dll (en de unittest): de waarden die het spel via bnusio.dll leest, afgeleid van het
// gedeelde geheugen dat de daemon schrijft (tp-shm-indeling PointBlankX). Geen Windows-code: de DLL (rg_usio.cpp) en tests/test_usio_model.cpp
// gebruiken dezelfde klasse, zodat de afbeelding (bits -> schakelaars, aim -> tienbits-positie, muntteller) op de host te toetsen is.
//
// BRONNEN (alles gemeten aan het spel, niet aan de kern van TeknoParrot):
//   * PBX100-2-NA-MPR0-A63_Data/Managed/Assembly-CSharp-firstpass.dll, klasse BNUsio: de 48 invoerpunten, de schakelaarmaskers
//     (SW_TEST 0x80, SW_SERVICE 0x4000, SW_UP 0x2000, SW_DOWN 0x1000, SW_ENTER 0x200) en de retourcodes (RET_EBUSY -16).
//   * Assembly-CSharp.dll, BNUsioController: start 1P = usio_IsSwOn(0x02000000), start 2P = 0x01000000, vuurwapen via gun_*, firmwareversie
//     moet 120..126 zijn, muntteller via usio_GetCoin(0)/usio_DecCoin.
//   * Plugins/bnusio.dll (het echte bestand, objdump): alle exports zijn cdecl zonder decoratie; gun_GetPositionX/Y geven de tienbits
//     ADC-waarde van het bord naar links verschoven met 6 (bereik 0..0xFFC0); usio_GetCoin(slot) leest een tabel met 12 bytes per slot.
//   * EightPointsGunCalibration: de standaardkalibratie is de identiteit op [0,1] met x > 0 verplicht, dus positie 0 is "buiten beeld".
//     De terugval van het spel (muis) gebruikt Input.mousePosition / Screen = oorsprong linksonder (Unity), dus y loopt naar boven.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace rg_usio {

// De indeling van het gedeelde geheugen (packaging/tp-shm-profiles.json, PointBlankX = TeknoParrotUi PointBlankPipe).
constexpr size_t kShmSize = 64;
constexpr size_t kOffControl = 8;   // 4 bytes: bitveld
constexpr size_t kOffP1X = 12, kOffP1Y = 16, kOffP2X = 20, kOffP2Y = 24;   // 1 byte elk: 0..255, links/boven = 0

// Bits in het besturingswoord (offset 8).
enum ControlBit : uint32_t {
    kCoin = 0x1, kService = 0x2, kTest = 0x4, kUp = 0x8, kDown = 0x10, kEnter = 0x20,
    kP1Start = 0x40, kP1Trigger = 0x80, kP2Start = 0x400, kP2Trigger = 0x800,
};

// Schakelaarmaskers van usio_GetSwIn() zoals het spel ze gebruikt (BNUsio.cs / BNUsioController.cs).
enum SwitchMask : uint32_t {
    kSwTest = 0x80, kSwService = 0x4000, kSwUp = 0x2000, kSwDown = 0x1000, kSwEnter = 0x200,
    kSwP1Start = 0x02000000, kSwP2Start = 0x01000000,
};

constexpr uint16_t kFirmwareVersion = 123;   // het spel eist 120..126 (anders "I/O ERROR 2")
constexpr int kAdcBits = 10;                 // gun_GetPosition* = 10-bits waarde << 6

class PointBlankModel {
public:
    // flip_y: het bord telt y van onderen (Unity), de daemon van boven. Standaard aan.
    explicit PointBlankModel(bool flip_y = true) : flip_y_(flip_y) {}

    // Eén monster van het gedeelde geheugen (64 bytes). Randen (munt, service) tellen mee zolang de aanroepen elkaar niet te ver
    // opvolgen; de DLL pollt daarom in een eigen draad op ~1 kHz.
    void update(const uint8_t* shm) {
        uint32_t c = 0;
        std::memcpy(&c, shm + kOffControl, 4);
        // Een munt of servicedruk is een rand: omhoog = +1 (zoals het bord de pulsen van de muntgever telt).
        if ((c & kCoin) && !(prev_ & kCoin)) coin_ = sat_add(coin_, 1);
        if ((c & kService) && !(prev_ & kService)) service_ = sat_add(service_, 1);
        prev_ = c;
        control_ = c;
        px_[0] = shm[kOffP1X]; py_[0] = shm[kOffP1Y];
        px_[1] = shm[kOffP2X]; py_[1] = shm[kOffP2Y];
    }

    uint32_t sw_in() const {
        uint32_t s = 0;
        if (control_ & kTest) s |= kSwTest;
        if (control_ & kUp) s |= kSwUp;
        if (control_ & kDown) s |= kSwDown;
        if (control_ & kEnter) s |= kSwEnter;
        if (control_ & kService) s |= kSwService;
        if (control_ & kP1Start) s |= kSwP1Start;
        if (control_ & kP2Start) s |= kSwP2Start;
        return s;
    }
    uint64_t sw_in64() const { return sw_in(); }

    // usio_GetCoin / usio_GetService: slot 0 is de eerste muntgever. Andere slots blijven 0.
    uint16_t coin(int slot) const { return slot == 0 ? coin_ : 0; }
    uint16_t service(int slot) const { return slot == 0 ? service_ : 0; }
    void dec_coin(int slot, uint16_t n) { if (slot == 0) coin_ = n > coin_ ? 0 : static_cast<uint16_t>(coin_ - n); }
    void dec_service(int slot, uint16_t n) { if (slot == 0) service_ = n > service_ ? 0 : static_cast<uint16_t>(service_ - n); }
    void reset_coin() { coin_ = 0; service_ = 0; }

    // gun_GetPositionX/Y(speler 1..2): tienbits waarde << 6. Een onbekende speler geeft 0.
    uint16_t gun_x(int player) const { return axis(player, false); }
    uint16_t gun_y(int player) const { return axis(player, true); }
    bool gun_triggered(int player) const {
        return player == 1 ? (control_ & kP1Trigger) != 0 : player == 2 ? (control_ & kP2Trigger) != 0 : false;
    }
    // De daemon meldt geen "buiten beeld"; de positie 0 telt in het spel zelf als buiten beeld (kalibratie), dus: altijd binnen.
    bool gun_in_range(int player) const { return player == 1 || player == 2; }

    // Uitgangen (lampjes, terugslag): het spel leest ze terug met usio_GetGout.
    void set_gout(uint8_t ch, uint8_t v) { if (ch < gout_.size()) gout_[ch] = v; }
    uint8_t gout(uint8_t ch) const { return ch < gout_.size() ? gout_[ch] : 0; }

    // 8-bits as (0..255) naar de tienbits ADC-waarde van het bord, en die << 6 zoals bnusio.dll doet.
    static uint16_t scale_axis(uint8_t v) {
        const uint32_t ten = (static_cast<uint32_t>(v) * 1023u + 127u) / 255u;   // afgerond, 0 -> 0, 255 -> 1023
        return static_cast<uint16_t>(ten << 6);
    }

private:
    static uint16_t sat_add(uint16_t a, uint16_t b) { return a > 0xFFFFu - b ? uint16_t{0xFFFF} : static_cast<uint16_t>(a + b); }
    uint16_t axis(int player, bool y) const {
        if (player != 1 && player != 2) return 0;
        const uint8_t raw = y ? py_[player - 1] : px_[player - 1];
        return scale_axis(y && flip_y_ ? static_cast<uint8_t>(255 - raw) : raw);
    }

    bool flip_y_;
    uint32_t prev_ = 0, control_ = 0;
    uint16_t coin_ = 0, service_ = 0;
    uint8_t px_[2] = {0, 0}, py_[2] = {0, 0};
    std::array<uint8_t, 32> gout_{};
};

}  // namespace rg_usio
