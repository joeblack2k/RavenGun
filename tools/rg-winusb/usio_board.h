// SPDX-License-Identifier: GPL-3.0-or-later
// Het model van het Namco USIO-bord zoals Time Crisis 5 het via WinUSB bedient (tools/rg-winusb/rg_winusb.cpp en tests/test_usio_board.cpp).
// Geen Windows-code: de DLL en de eenheidstest gebruiken dezelfde klasse.
//
// BRON (alles gemeten aan TimeCrisisGame-Win64-Shipping.exe, klasse CUsioRaw, statisch gelezen; niets uit de kern van TeknoParrot):
//   * Het transport (0x140002f90 lezen, 0x1400030b0 schrijven, 0x140003650/0x140003730 de pijpen):
//       lezen    = WritePipe(bulk-OUT, 6 bytes {0x10, 0x00, adres(16 LE), lengte(16 LE)}); daarna ReadPipe(bulk-IN, lengte bytes)
//       schrijven= WritePipe(bulk-OUT, 6 bytes {0x90, ~(adres>>8)&0xF0, adres(16 LE), lengte(16 LE)}); WritePipe(bulk-OUT, lengte bytes
//                  gegevens); daarna ReadPipe(bulk-IN, 1 byte) als bevestiging (de inhoud wordt niet gelezen, alleen het slagen)
//   * De communicatiedraad (0x1400026b0), elke ronde ~10 ms: lees 0x100 bytes @0x0000, lees 0x80 bytes @0x1080, schrijf 0x20 bytes @0x1420
//     (uitgangen); eenmalig lees 0x80 bytes @0x1800. De opstart (0x140a553a0, UpdateUSIOForUE): het foutwoord @0x0002 moet 0 zijn (anders
//     een foutscherm met oneindige lus: back-up-, firmware-/IRM1-, geheugen- of systeemfout), dan schrijft het spel 0x220 bytes @0x2000 en,
//     als het woord @0x0028 niet 0x8173 is, zet het dat woord.
//   * @0x1080 staat het 32-bits schakelaarwoord. Het spel leest daar de bits 9, 10, 11, 14 en 23 (UpdateUSIOForUE, 0x140a56e96: vijf
//     knopvlaggen die de UE-invoer vullen) en bits 14 en 15 in de muntlogica (0x140002900, maskertabel 0x4000/0x8000 op 0x1414a5b90).
//   * @0x1090 en @0x1092 staan twee muntstanden (16 bit): onderste 14 bits = teller, bovenste 2 bits = status (0 = goed). Het spel
//     neemt het verschil met de vorige stand (0x14000286c) als aantal munten.
//
// WAT GEMETEN IS EN WAT GEGOKT: het transport, de leesbereiken, de foutwoord-eis en de muntteller-indeling zijn uit de code gelezen. De
// toewijzing van de schakelaarbits aan knoppen is NIET gemeten, behalve wat de Point Blank X-bibliotheek van dezelfde USIO-familie laat
// zien (test 0x80, service 0x4000, omhoog 0x2000, omlaag 0x1000, enter 0x200). Voor pedalen en de gunknop is het een gok (standaard
// 0x800 / 0x400 / 0x800000, de drie bits die het spel naast enter en service leest); `Mapping` is daarom per sleutel te overschrijven
// (RG_TC5_BITS) en de DLL logt elke verandering van het schakelaarwoord.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_usio5 {

// Het gedeelde geheugen TeknoParrot_JvsState, indeling TimeCrisis5 (packaging/tp-shm-profiles.json).
constexpr size_t kShmSize = 64;
constexpr size_t kOffControl = 8;    // bitveld (4 bytes)
constexpr size_t kOffP1X = 12, kOffP1Y = 16;   // 1 byte: 0..255, links/boven = 0

enum ControlBit : uint32_t {
    kCtlTest = 0x01, kCtlService = 0x04, kCtlCoin = 0x08, kCtlTrigger = 0x10, kCtlGun = 0x20,
    kCtlPedalL = 0x40, kCtlPedalR = 0x80, kCtlSelUp = 0x100, kCtlSelDown = 0x200, kCtlEnter = 0x400,
};

// Welke bit van het schakelaarwoord @0x1080 bij welke knop hoort. 0 = niet aansluiten.
struct Mapping {
    uint32_t test = 0x00000080;      // Point Blank X: SW_TEST (zelfde USIO-familie)
    uint32_t service = 0x00004000;   // idem SW_SERVICE; het spel leest bit 14 ook
    uint32_t enter = 0x00000200;     // idem SW_ENTER; het spel leest bit 9
    uint32_t sel_up = 0x00002000;    // idem SW_UP (het spel gebruikt hem niet)
    uint32_t sel_down = 0x00001000;  // idem SW_DOWN (idem)
    uint32_t pedal_l = 0x00000800;   // GOK: bit 11
    uint32_t pedal_r = 0x00000400;   // GOK: bit 10
    uint32_t gun = 0x00800000;       // GOK: bit 23
    uint32_t trigger = 0;            // de trekker loopt standaard als muisknop (SendInput), niet over het bord
    bool coin = false;               // munt via het bord (teller @0x1090); standaard uit: de daemon geeft Shift+T aan het spel
};

constexpr size_t kRegSize = 0x3000;
constexpr uint16_t kRegSwitches = 0x1080;   // 32 bit
constexpr uint16_t kRegCoin1 = 0x1090;      // 16 bit
constexpr uint16_t kRegCoin2 = 0x1092;      // 16 bit
constexpr uint16_t kRegOutputs = 0x1420;    // 0x20 bytes, door het spel geschreven
constexpr uint16_t kRegError = 0x0002;      // foutwoord, moet 0 zijn
constexpr uint16_t kRegInitFlag = 0x0028;   // het spel zet hier 0x8173

class Board {
public:
    explicit Board(const Mapping& m = Mapping{}) : map_(m) {}

    void set_mapping(const Mapping& m) { map_ = m; }
    const Mapping& mapping() const { return map_; }

    // Eén monster van het gedeelde geheugen (64 bytes). Randen van de munt tellen als +1 op de teller (alleen met map_.coin).
    void update(const uint8_t* shm) {
        uint32_t c = 0;
        std::memcpy(&c, shm + kOffControl, 4);
        if (map_.coin && (c & kCtlCoin) && !(ctl_ & kCtlCoin)) bump_coin(kRegCoin1);
        ctl_ = c;
        uint32_t sw = 0;
        if (c & kCtlTest) sw |= map_.test;
        if (c & kCtlService) sw |= map_.service;
        if (c & kCtlEnter) sw |= map_.enter;
        if (c & kCtlSelUp) sw |= map_.sel_up;
        if (c & kCtlSelDown) sw |= map_.sel_down;
        if (c & kCtlPedalL) sw |= map_.pedal_l;
        if (c & kCtlPedalR) sw |= map_.pedal_r;
        if (c & kCtlGun) sw |= map_.gun;
        if (c & kCtlTrigger) sw |= map_.trigger;
        put32(kRegSwitches, sw);
        x_ = shm[kOffP1X];
        y_ = shm[kOffP1Y];
    }
    uint32_t switches() const { return get32(kRegSwitches); }
    uint32_t control() const { return ctl_; }
    uint8_t aim_x() const { return x_; }
    uint8_t aim_y() const { return y_; }
    bool trigger() const { return (ctl_ & kCtlTrigger) != 0; }
    uint16_t coin1() const { return get16(kRegCoin1); }

    // Een OUT-overdracht van het spel (bulk-OUT). Geeft false bij iets wat het protocol niet kent (de DLL logt dat).
    bool out_transfer(const uint8_t* d, size_t n) {
        if (state_ == State::WriteData) {
            state_ = State::Idle;
            if (n != pend_len_) { ++stray_; return false; }
            if (static_cast<size_t>(pend_addr_) + n > kRegSize) { ++stray_; return false; }
            // Het schakelaarwoord en de munttellers zijn alleen-lezen voor het spel (het bord vult ze).
            for (size_t i = 0; i < n; ++i) {
                const size_t a = static_cast<size_t>(pend_addr_) + i;
                if (read_only(a)) continue;
                regs_[a] = d[i];
            }
            ack_ = true;
            ++writes_;
            return true;
        }
        if (n != 6) { ++stray_; return false; }
        const uint16_t addr = static_cast<uint16_t>(d[2] | (d[3] << 8));
        const uint16_t len = static_cast<uint16_t>(d[4] | (d[5] << 8));
        if (d[0] == 0x10) {
            // Lezen. d[1] hoort 0 te zijn; we doen niet moeilijk als dat niet zo is.
            pend_addr_ = addr;
            pend_len_ = len;
            read_ = true;
            return true;
        }
        if (d[0] == 0x90) {
            pend_addr_ = addr;
            pend_len_ = len;
            state_ = State::WriteData;
            return true;
        }
        ++stray_;
        return false;
    }

    // Een IN-overdracht (bulk-IN) met `cap` bytes gevraagd. Geeft het aantal bytes dat het bord levert (0 = niets te melden).
    size_t in_transfer(uint8_t* out, size_t cap) {
        if (read_) {
            read_ = false;
            const size_t n = pend_len_ < cap ? pend_len_ : cap;
            for (size_t i = 0; i < n; ++i) {
                const size_t a = static_cast<size_t>(pend_addr_) + i;
                out[i] = a < kRegSize ? regs_[a] : 0;
            }
            ++reads_;
            return n;
        }
        if (ack_) {
            ack_ = false;
            if (cap >= 1) { out[0] = 0x00; return 1; }
        }
        ++stray_;
        return 0;
    }

    // Voor de tests en het logboek.
    uint8_t reg8(size_t a) const { return a < kRegSize ? regs_[a] : 0; }
    uint16_t get16(size_t a) const { return a + 2 <= kRegSize ? static_cast<uint16_t>(regs_[a] | (regs_[a + 1] << 8)) : 0; }
    uint32_t get32(size_t a) const {
        return a + 4 <= kRegSize ? static_cast<uint32_t>(regs_[a]) | (static_cast<uint32_t>(regs_[a + 1]) << 8) |
                                       (static_cast<uint32_t>(regs_[a + 2]) << 16) | (static_cast<uint32_t>(regs_[a + 3]) << 24)
                                 : 0;
    }
    uint32_t reads() const { return reads_; }
    uint32_t writes() const { return writes_; }
    uint32_t strays() const { return stray_; }
    bool pending_write_data() const { return state_ == State::WriteData; }

private:
    enum class State { Idle, WriteData };

    static bool read_only(size_t a) { return a >= kRegSwitches && a < kRegSwitches + 0x80; }
    void put32(size_t a, uint32_t v) {
        for (int i = 0; i < 4; ++i) regs_[a + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
    }
    void put16(size_t a, uint16_t v) {
        regs_[a] = static_cast<uint8_t>(v);
        regs_[a + 1] = static_cast<uint8_t>(v >> 8);
    }
    void bump_coin(uint16_t reg) {
        const uint16_t cur = get16(reg);
        const uint16_t next = static_cast<uint16_t>((cur & 0xC000u) | ((cur + 1u) & 0x3FFFu));   // 14 bits teller, status ongemoeid
        put16(reg, next);
    }

    Mapping map_;
    std::array<uint8_t, kRegSize> regs_{};
    State state_ = State::Idle;
    uint16_t pend_addr_ = 0, pend_len_ = 0;
    bool read_ = false, ack_ = false;
    uint32_t ctl_ = 0, reads_ = 0, writes_ = 0, stray_ = 0;
    uint8_t x_ = 0, y_ = 0;
};

// "RG_TC5_BITS=pedal_l=0x800,pedal_r=0x400,gun=0x800000,coin=1": overschrijft de toewijzing. Onbekende sleutels worden genegeerd
// (geteld in `unknown`). Geeft het aantal toegepaste sleutels.
inline int parse_mapping(const char* s, Mapping& m, int* unknown = nullptr) {
    int applied = 0;
    if (unknown) *unknown = 0;
    if (!s) return 0;
    const char* p = s;
    while (*p) {
        const char* end = p;
        while (*end && *end != ',' && *end != ';') ++end;
        const char* eq = p;
        while (eq < end && *eq != '=') ++eq;
        if (eq < end) {
            const std::size_t klen = static_cast<std::size_t>(eq - p);
            char val[32] = {};
            const std::size_t vlen = static_cast<std::size_t>(end - eq - 1) < sizeof val - 1 ? static_cast<std::size_t>(end - eq - 1) : sizeof val - 1;
            std::memcpy(val, eq + 1, vlen);
            // strtoul met basis 0: 0x.. hex, 0.. octaal, anders decimaal.
            char* tail = nullptr;
            const unsigned long v = std::strtoul(val, &tail, 0);
            const bool ok = tail != val;
            auto is = [&](const char* k) { return std::strlen(k) == klen && std::strncmp(p, k, klen) == 0; };
            bool hit = ok;
            if (!ok) hit = false;
            else if (is("test")) m.test = static_cast<uint32_t>(v);
            else if (is("service")) m.service = static_cast<uint32_t>(v);
            else if (is("enter")) m.enter = static_cast<uint32_t>(v);
            else if (is("sel_up")) m.sel_up = static_cast<uint32_t>(v);
            else if (is("sel_down")) m.sel_down = static_cast<uint32_t>(v);
            else if (is("pedal_l")) m.pedal_l = static_cast<uint32_t>(v);
            else if (is("pedal_r")) m.pedal_r = static_cast<uint32_t>(v);
            else if (is("gun")) m.gun = static_cast<uint32_t>(v);
            else if (is("trigger")) m.trigger = static_cast<uint32_t>(v);
            else if (is("coin")) m.coin = v != 0;
            else hit = false;
            if (hit) ++applied;
            else if (unknown) ++*unknown;
        }
        p = *end ? end + 1 : end;
    }
    return applied;
}

}  // namespace rg_usio5
