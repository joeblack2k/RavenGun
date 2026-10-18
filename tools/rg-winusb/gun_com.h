// SPDX-License-Identifier: GPL-3.0-or-later
// Het model van het seriele geweer (COM-poort) van Time Crisis 5 (tools/rg-winusb/rg_winusb.cpp en tests/test_tc5_guncom.cpp).
// Geen Windows-code: de DLL en de eenheidstest gebruiken dezelfde klasse.
//
// WAAROM: aim en trekker lopen bij TC5 NIET over het USIO-bord en NIET via de muis. Ze komen van een geweer op een seriele poort. Alleen als die
// poort niet opent valt het spel terug op de muis (relatieve delta's, geen trekker): precies wat de tv-proef van 2026-10-18 liet zien.
//
// BRON (alles gemeten aan TimeCrisisGame-Win64-Shipping.exe, statisch gelezen; niets uit de kern van TeknoParrot):
//   * Openen (0x140a54c30): CreateFileW(naam, 0xC0000000, 0, NULL, OPEN_EXISTING, 0, NULL) voor de namen COM1, COM2, COM15, COM14 .. COM3 (16-byte
//     tabel UTF-16 op 0x14140a868, in die volgorde, zonder "\\.\"-voorvoegsel); GetCommState, SetCommState (38400 baud, 8 databits, oneven pariteit,
//     1 stopbit), GetCommTimeouts/SetCommTimeouts (10/100/10/10/100 ms), ClearCommError (cbInQue > 0: leegmaken met ReadFile), daarna een eerste
//     transactie met opdracht 0. Slaagt die niet, dan CloseHandle en de volgende naam. De eerste poort die antwoordt blijft open (0x1416f2f70).
//   * Transactie (0x140a547b0): WriteFile(1 byte = opdrachtcode), ReadFile(antwoord, 1+ keer tot de lengte vol is), dan de controlesom. De tabel
//     opdracht -> {code, antwoordlengte} staat op 0x1416f2f80: 0:{0x08,11} 1:{0x10,2} 2:{0x1c,11} 3:{0x14,2} 4:{0x18,2} 5:{0x0c,2} 6:{0x04,2} 7:{0xff,2}.
//     Controlesom = laatste byte van het antwoord = ~(som van alle bytes ervoor) & 0xFF.
//   * Opdracht 0 (code 0x08) is de gewerepoll (0x140a54f90, elke ronde, ook de eerste na het openen): antwoord van 11 bytes
//         b0..1 X1 (16 bit, groot-endiaan)   b2..3 Y1   b4..5 X2   b6..7 Y2   b8 vlaggen   b9 (ongebruikt)   b10 controlesom
//     X1,Y1,X2,Y2 -> 0x141860420.. -> UTCMouseInterfacePlayerInput.X1/Y1/X2/Y2 (+0x2e8..+0x2f4). Vlaggen (b8): bit 5 = trekker 1 (Trigger1, +0x2fc;
//     het spel OR't hem vast tot het hem leest, dus een korte puls gaat niet verloren), bit 4 = Trigger2 (+0x304), bit 7 = buiten beeld (bIsOutOfFrame,
//     +0x2f8 bit 1). Het script (TimeCrisisGame.u, class TCMouseInterfacePlayerInput, UpdatePlayerInput) gebruikt X1/1024 en (0x3ff - Y1)/1024
//     als genormaliseerde invoer van de ijking (AdjustPosition, 5 of 8 punten): X1 en Y1 zijn dus 10-bits waarden 0..1023 met Y1 = 0 onderaan.
//   * Opdrachten 3 en 4 (0x14, 0x18; SetLEDFreqForUE, 0x140a554a0) en 5, 6, 7: antwoord van 2 bytes {b, ~b}.
//   * De versievraag (0x140a54ae0, alleen met de debugvlag [0x1418604f4]): WriteFile(0x10), dan 1 byte per ReadFile tot 31 bytes; het eerste byte
//     moet 'R' (0x52) zijn.
//   * Verbreking: 120 mislukte transacties achter elkaar -> toestand 4 (CommunicationAborted) -> het script valt terug op de muis. De DLL laat dus
//     nooit een transactie mislukken zolang het spel een geldige opdracht stuurt.
//
// WAT NIET BEWEZEN IS: het bereik/de richting van de ruwe X1/Y1 (de standaardijking staat in de gecookte standaardwaarden van de klasse en is niet
// uitgelezen) en welke bit van b8 precies in het frame zit buiten de drie hierboven. `GunMap` is daarom per sleutel te overschrijven (RG_TC5_GUN).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_usio5 {

// Zie ook usio_board.h: hetzelfde gedeelde geheugen (TeknoParrot_JvsState, indeling TimeCrisis5, packaging/tp-shm-profiles.json).
constexpr size_t kGunShmSize = 64;
constexpr size_t kGunOffControl = 8, kGunOffP1X = 12, kGunOffP1Y = 16;
constexpr uint32_t kGunCtlTrigger = 0x10;   // p1.trigger (offset 8, bit 4)

// Vlaggen van byte 8 in het gewerekader (zie hierboven).
constexpr uint8_t kGunFlagTrigger2 = 0x10, kGunFlagTrigger1 = 0x20, kGunFlagOutOfFrame = 0x80;

struct GunMap {
    int x0 = 0, x1 = 1023;     // ruwe X1 voor aim 0 (links) en aim 255 (rechts)
    int y0 = 0, y1 = 1023;     // ruwe Y1 voor aim 0 (boven) en aim 255 (onder), vooraf aan flip_y
    bool flip_y = true;        // Y1 = 0 onderaan (het script doet 0x3ff - Y1): aim 0 (boven) geeft dan Y1 = 1023
    bool oof = true;           // bit 7 zetten als de daemon-as <= 1 of >= 254 is (buiten beeld, packaging/tp-shm-profiles.json)
    uint8_t trig_flag = kGunFlagTrigger1;   // welke vlag de trekker is (standaard bit 5)
};

// "RG_TC5_GUN=x0=0,x1=1023,y0=0,y1=1023,flipy=1,oof=1,trig=0x20": overschrijft de afbeelding. Onbekende sleutels worden geteld in `unknown`.
inline int parse_gunmap(const char* s, GunMap& m, int* unknown = nullptr) {
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
            char* tail = nullptr;
            const long v = std::strtol(val, &tail, 0);
            const bool ok = tail != val;
            auto is = [&](const char* k) { return std::strlen(k) == klen && std::strncmp(p, k, klen) == 0; };
            bool hit = ok;
            if (!ok) hit = false;
            else if (is("x0")) m.x0 = static_cast<int>(v);
            else if (is("x1")) m.x1 = static_cast<int>(v);
            else if (is("y0")) m.y0 = static_cast<int>(v);
            else if (is("y1")) m.y1 = static_cast<int>(v);
            else if (is("flipy")) m.flip_y = v != 0;
            else if (is("oof")) m.oof = v != 0;
            else if (is("trig")) m.trig_flag = static_cast<uint8_t>(v);
            else hit = false;
            if (hit) ++applied;
            else if (unknown) ++*unknown;
        }
        p = *end ? end + 1 : end;
    }
    return applied;
}

// De antwoordlengte per opdrachtcode (tabel 0x1416f2f80). 0 = onbekende code.
inline size_t gun_reply_len(uint8_t code) {
    switch (code) {
        case 0x08: case 0x1c: return 11;
        case 0x10: return 31;   // de versievraag; als transactie 1 zou het 2 zijn, maar die gebruikt het spel niet
        case 0x14: case 0x18: case 0x0c: case 0x04: case 0xff: return 2;
        default: return 0;
    }
}

inline uint8_t gun_checksum(const uint8_t* d, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; ++i) s += d[i];
    return static_cast<uint8_t>(~s & 0xFFu);
}

class GunCom {
public:
    explicit GunCom(const GunMap& m = GunMap{}) : map_(m) {}
    void set_map(const GunMap& m) { map_ = m; }
    const GunMap& map() const { return map_; }

    // Eén monster van het gedeelde geheugen (64 bytes).
    void update(const uint8_t* shm) {
        uint32_t c = 0;
        std::memcpy(&c, shm + kGunOffControl, 4);
        trig_ = (c & kGunCtlTrigger) != 0;
        ax_ = shm[kGunOffP1X];
        ay_ = shm[kGunOffP1Y];
    }

    // Ruwe 10-bits waarden zoals ze in het frame komen.
    uint16_t raw_x() const { return clamp10(lerp(map_.x0, map_.x1, ax_)); }
    uint16_t raw_y() const {
        const int ay = map_.flip_y ? 255 - ay_ : ay_;
        return clamp10(lerp(map_.y0, map_.y1, ay));
    }
    bool out_of_frame() const { return map_.oof && (ax_ <= 1 || ax_ >= 254 || ay_ <= 1 || ay_ >= 254); }
    uint8_t flags() const {
        uint8_t f = 0;
        if (trig_) f |= map_.trig_flag;
        if (out_of_frame()) f |= kGunFlagOutOfFrame;
        return f;
    }
    bool trigger() const { return trig_; }

    // Het 11-byte gewerekader van opdracht 0 (code 0x08).
    std::array<uint8_t, 11> frame() const {
        std::array<uint8_t, 11> f{};
        const uint16_t x = raw_x(), y = raw_y();
        f[0] = static_cast<uint8_t>(x >> 8); f[1] = static_cast<uint8_t>(x);
        f[2] = static_cast<uint8_t>(y >> 8); f[3] = static_cast<uint8_t>(y);
        // X2/Y2 (b4..b7): tweede geweer, niet in de tp-shm-indeling van TC5; midden.
        f[4] = 0x02; f[5] = 0x00; f[6] = 0x02; f[7] = 0x00;
        f[8] = flags();
        f[9] = 0;
        f[10] = gun_checksum(f.data(), 10);
        return f;
    }

    // WriteFile van het spel: een opdrachtcode (gewoonlijk 1 byte). Zet het antwoord klaar; een onbekende code geeft geen antwoord (het spel
    // telt dat als mislukte transactie). Geeft het aantal klaargezette bytes.
    size_t write(const uint8_t* d, size_t n) {
        if (!d || n == 0) return 0;
        const uint8_t code = d[0];
        ++writes_;
        last_code_ = code;
        rx_len_ = rx_pos_ = 0;
        const size_t want = gun_reply_len(code);
        if (want == 0) { ++unknown_; return 0; }
        if (code == 0x08) {
            const auto f = frame();
            std::memcpy(rx_.data(), f.data(), f.size());
            rx_len_ = f.size();
            ++polls_;
        } else if (code == 0x1c) {
            // Tienbits-pakket (10 bits in 2 bytes: b0<<2 | b1>>6); het spel gebruikt het niet, wel een geldig kader.
            std::memset(rx_.data(), 0, 11);
            const uint16_t x = raw_x(), y = raw_y();
            rx_[0] = static_cast<uint8_t>(x >> 2); rx_[1] = static_cast<uint8_t>((x & 3) << 6);
            rx_[2] = static_cast<uint8_t>(y >> 2); rx_[3] = static_cast<uint8_t>((y & 3) << 6);
            rx_[10] = gun_checksum(rx_.data(), 10);
            rx_len_ = 11;
        } else if (code == 0x10) {
            static const char kId[] = "RavenGun TC5 gun emulation";   // 26 tekens + opvulling = 31 bytes, eerste byte 'R'
            std::memset(rx_.data(), 0, 31);
            std::memcpy(rx_.data(), kId, sizeof kId - 1);
            rx_len_ = 31;
        } else {
            rx_[0] = 0x00;
            rx_[1] = gun_checksum(rx_.data(), 1);   // {0x00, 0xFF}
            rx_len_ = 2;
        }
        return rx_len_;
    }

    // ReadFile van het spel: tot `cap` bytes van het klaargezette antwoord.
    size_t read(uint8_t* out, size_t cap) {
        const size_t left = rx_len_ - rx_pos_;
        const size_t n = left < cap ? left : cap;
        if (n && out) std::memcpy(out, rx_.data() + rx_pos_, n);
        rx_pos_ += n;
        return n;
    }
    size_t pending() const { return rx_len_ - rx_pos_; }
    void purge() { rx_len_ = rx_pos_ = 0; }

    uint32_t writes() const { return writes_; }
    uint32_t polls() const { return polls_; }
    uint32_t unknown() const { return unknown_; }
    uint8_t last_code() const { return last_code_; }

private:
    static int lerp(int a, int b, int v) { return a + (b - a) * v / 255; }
    static uint16_t clamp10(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 1023 ? 1023 : v)); }

    GunMap map_;
    std::array<uint8_t, 64> rx_{};
    size_t rx_len_ = 0, rx_pos_ = 0;
    bool trig_ = false;
    uint8_t ax_ = 128, ay_ = 128;
    uint32_t writes_ = 0, polls_ = 0, unknown_ = 0;
    uint8_t last_code_ = 0;
};

}  // namespace rg_usio5
