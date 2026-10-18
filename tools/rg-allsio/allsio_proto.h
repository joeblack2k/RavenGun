// SPDX-License-Identifier: GPL-3.0-or-later
// Het USB-IO-bord van Sega ALLS (837-15257 "I/O CONTROL BD") zoals amdaemon.exe het via HID bedient, voor rg-allsio.dll en de
// eenheidstest. Geen Windows-code.
//
// BRON (alles statisch gelezen in amdaemon.exe, "Sega AM Daemon 2437", libam amUsbio Rev 7255; niets uit de kern van TeknoParrot):
//   * Vinden (amUsbioSetupDevice 0x140232ad0 -> 0x140232f80): HID-apparaten via SetupDi (GUID uit HidD_GetHidGuid), het pad (in kleine
//     letters) moet "vid_0ca3" bevatten; CreateFileW(0xC0000000, deel 3, OPEN_EXISTING, 0x40000080 = overlapped); dan
//     HidD_GetManufacturerString (moet met "SEGA" beginnen) en HidD_GetProductString (ParseNodeInfo 0x140231860).
//   * Productstring (ParseNodeInfo): 7 velden gescheiden door ';', daarna ';' en een staart. Velden: (0) naam, exact "I/O CONTROL BD"
//     (< 16 tekens), (1) model < 9 tekens, (2) hex-byte, (3) hex-byte, (4) hex-woord, (5) tekst < 6 tekens, (6) hex-byte.
//     Staart: items gescheiden door '_', "NAAM=waarde[,waarde]" met hex-waarden: GOUT=<n>, PWMOUT=<n>, ADIN=<n>,<m>, ROTIN=<n>, COININ=<n>,
//     SWIN=<spelers>,<knoppen>, en UQ1=, UQ2=, ... (<commando>,<tot drie parameters>) voor eigen commando's.
//   * Rapporten: 64 bytes. Verzoek (host -> bord): byte 0 = rapport-id 0x10, byte 1 = commando, rest = argumenten. Antwoord/ingang
//     (bord -> host): byte 0 = rapport-id 0x01, byte 1 = commando (antwoord) of, voor de periodieke ingang, de eerste ingangsbyte.
//     Commando's met bit 7 (0x84 modus, 0x85 firmware, 0x88 reset(0xD9)) gaan over de besturingspijp (HidD_SetOutputReport /
//     HidD_GetInputReport); de andere (0x01 time-out, 0x02 samplingtelling, 0x03 wis status, 0x04 algemene uitgang, 0x05 PWM, eigen
//     UQ-commando's) over de interruptpijp (WriteFile / ReadFile, overlapped). Ingangsgegevens: 63 bytes na het rapport-id.
//   * Indeling van die 63 ingangsbytes volgens de bestaande AllsUsbIo-besturingspijp (packaging/control-pipe-profiles.json): 8 analoge kanalen
//     (0-15), 4 draaiknoppen (16-23), muntteller (24-27), schakelaars speler 1 (28-29) en 2 (30-31).
//
// GEGOKT (niet uit de code te halen, daarom in het logboek van de DLL te meten): de exacte tekst van de staart (ADIN/SWIN-waarden), de
// inhoud van de antwoorden op de verzoeken, en dat byte k van de AllsUsbIo-pijp op ingangsbyte k staat (rapport-id ervoor).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace rg_allsio {

constexpr uint16_t kVid = 0x0CA3;   // Sega
constexpr uint16_t kPid = 0x0021;   // gegokt (het spel kijkt er niet naar: alleen "vid_0ca3" in het pad)
constexpr size_t kReport = 64;
constexpr uint8_t kIdOut = 0x10, kIdIn = 0x01;

// De productstring die we aan amdaemon melden. De staart moet voldoen aan config.json van het spel: munt 2 sloten, schakelaars speler 2 met
// 8 knoppen, algemene uitgang 20 (hex 14), eigen commando 65 (hex 41) met parameter 6.
inline std::wstring product_string() {
    return L"I/O CONTROL BD;15257;01;90;1831;6679A;00;GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6";
}
inline std::wstring manufacturer_string() { return L"SEGA"; }
// Het pad dat de SetupDi-nabootsing teruggeeft (kleine letters, bevat "vid_0ca3").
inline const wchar_t* device_path() { return L"\\\\?\\hid#vid_0ca3&pid_0021#7&rgio&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"; }

// amLibGetMainSerialId (daemon 2437, 0x140234030) gebruikt GetComputerNameA en
// weigert elke lengte behalve 15. Alleen een expliciet ingestelde ASCII-naam
// gebruiken; geen gedeelde Wine-computernaam of hostidentiteit veranderen.
inline bool valid_main_id(const char* name) {
    if (!name || std::strlen(name) != 15) return false;
    for (size_t i = 0; i < 15; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

// Openbare platform-IOCTLs, vergeleken met segatools platform/{nusec,hwmon}.c
// en de aanroepen in daemon 2437. Geen sleutels of cryptografische antwoorden.
// Onbekende opdrachten worden nooit als geslaagd behandeld.
enum class PlatformDevice { Keychip, Hwmon, Reset };
enum class IoctlResult { Ok, Unsupported, ShortBuffer };
inline IoctlResult platform_ioctl(PlatformDevice device, uint32_t code,
                                  void* output, size_t capacity, size_t& written) {
    written = 0;
    uint32_t values[2] = {};
    size_t count = 1;
    if (device == PlatformDevice::Hwmon && code == 0x80006000) {
        values[0] = 52; // sensorwaarde; de aanroeper past zijn temperatuurbias toe
    } else if (device == PlatformDevice::Reset && code == 0x80002000) {
        values[0] = 1; // alleen bevestigen, nooit de host herstarten
    } else if (device == PlatformDevice::Keychip) {
        switch (code) {
        case 0x22a114: return IoctlResult::Ok; // ping: geen antwoordpayload
        case 0x22e150: values[0] = 0; break; // speelteller
        case 0x22e204: values[0] = 1024; break; // speellimiet
        case 0x22e20c: values[0] = 0x00010200; break; // nearfull
        case 0x22e19c: values[0] = 7154; break; // vrije logrecords
        case 0x22e24c: values[0] = 10; values[1] = 4096; count = 2; break;
        case 0x22e198: count = 2; break; // lege trace: aantal, staart
        default: return IoctlResult::Unsupported;
        }
    } else {
        return IoctlResult::Unsupported;
    }
    const size_t need = count * sizeof(uint32_t);
    if (!output || capacity < need) return IoctlResult::ShortBuffer;
    auto* bytes = static_cast<uint8_t*>(output);
    for (size_t i = 0; i < need; ++i)
        bytes[i] = static_cast<uint8_t>(values[i / 4] >> (8 * (i % 4)));
    written = need;
    return IoctlResult::Ok;
}

// FGunSensor in Hodzero: synchrone COM1, reset 04, tekstconfiguratie S P2 / S Fn,
// poll 08. Parser 0x1401e2840: vier big-endian 10-bit assen, Y omgekeerd,
// vlaggen in byte 8, byte 9 ongebruikt, complement van de som in byte 10.
// Dit staat los van het HID-bord van amdaemon. Invoer: HOTDSDPipe (64 bytes).
class GunSensor {
public:
    static uint8_t checksum(const uint8_t* data, size_t n) {
        uint8_t sum = 0;
        for (size_t i = 0; i < n; ++i) sum = static_cast<uint8_t>(sum + data[i]);
        return static_cast<uint8_t>(~sum);
    }
    static void frame(const uint8_t* shm, uint8_t* out) {
        std::memset(out, 0, 11);
        out[8] = 0xc0; // actieve-lage on-screen-bits: zonder invoer buiten beeld
        for (size_t player = 0; player < 2; ++player) {
            const unsigned x = shm ? shm[12 + player * 8] : 128;
            const unsigned y = shm ? shm[16 + player * 8] : 128;
            const unsigned raw_x = (x * 1023 + 127) / 255;
            const unsigned raw_y = 1023 - (y * 1023 + 127) / 255;
            out[player * 4] = static_cast<uint8_t>(raw_x >> 8);
            out[player * 4 + 1] = static_cast<uint8_t>(raw_x);
            out[player * 4 + 2] = static_cast<uint8_t>(raw_y >> 8);
            out[player * 4 + 3] = static_cast<uint8_t>(raw_y);
            if (shm && x > 1 && x < 254 && y > 1 && y < 254)
                out[8] &= static_cast<uint8_t>(~(0x80 >> player));
            if (shm && (shm[8] & (1u << player))) out[8] |= static_cast<uint8_t>(0x20 >> player);
        }
        out[10] = checksum(out, 10);
    }
    // Antwoorden worden pas na een opdracht beschikbaar; deelreads verbruiken
    // dezelfde momentopname, niet steeds een nieuw (mogelijk gescheurd) pakket.
    bool write(const uint8_t* data, size_t n, const uint8_t* shm) {
        if (!data || !n) return false;
        if (n == 1 && data[0] == 0x04) {
            configured_ = false;
            command_size_ = 0;
            reply_[0] = 0; reply_[1] = 0xff;
            pos_ = 0; size_ = 2;
            return true;
        }
        if (n == 1 && data[0] == 0x08) {
            if (!configured_ || command_size_) return false;
            frame(shm, reply_);
            pos_ = 0; size_ = 11;
            return true;
        }
        // Alleen de gemeten opdrachten; onbekende tekst geeft geen verzonnen ACK.
        for (size_t i = 0; i < n; ++i) {
            if (command_size_ >= sizeof command_ || data[i] < 0x0a || data[i] > 0x7e) {
                command_size_ = 0; purge(); return false;
            }
            command_[command_size_++] = static_cast<char>(data[i]);
            if (data[i] == '\n') {
                const bool players = command_size_ == 6 && std::memcmp(command_, "S P2\r\n", 6) == 0;
                const bool frequency = command_size_ == 6 && std::memcmp(command_, "S F", 3) == 0 &&
                    command_[3] >= '0' && command_[3] <= '3' && command_[4] == '\r';
                command_size_ = 0;
                if (!players && !frequency) { purge(); return false; }
                if (players) configured_ = true;
                reply_[0] = 0x75; reply_[1] = 0x8a;
                pos_ = 0; size_ = 2;
            }
        }
        return true;
    }
    size_t read(uint8_t* out, size_t capacity) {
        if (!out || !capacity) return 0;
        const size_t n = capacity < pending() ? capacity : pending();
        std::memcpy(out, reply_ + pos_, n);
        pos_ += n;
        return n;
    }
    size_t pending() const { return size_ - pos_; }
    void purge() { pos_ = size_ = 0; }
    void close() { purge(); command_size_ = 0; configured_ = false; }
private:
    uint8_t reply_[11] = {};
    char command_[16] = {};
    size_t pos_ = 0, size_ = 0, command_size_ = 0;
    bool configured_ = false;
};

enum class Pipe { Control, Interrupt };

struct Request {
    uint8_t cmd = 0;
    uint8_t args[62] = {};
    Pipe pipe = Pipe::Interrupt;
};

class Board {
public:
    // Een uitvoerrapport van de host (64 bytes: id, commando, argumenten). Geeft false als het geen rapport van ons protocol is.
    bool on_output(const uint8_t* rpt, Pipe via, Request* out = nullptr) {
        if (rpt[0] != kIdOut) { ++bad_; return false; }
        Request r;
        r.cmd = rpt[1];
        std::memcpy(r.args, rpt + 2, sizeof r.args);
        r.pipe = via;
        last_cmd_ = r.cmd;
        if (r.cmd == 0x01) comm_timeout_ = r.args[0];
        if (r.cmd == 0x02) sampling_ = r.args[0];
        if (r.cmd == 0x03) status_cleared_ = true;
        ++count_;
        if (out) *out = r;
        return true;
    }

    // Het antwoord op een besturingsverzoek (HidD_GetInputReport): id 1, het commando, status 0. GEGOKT.
    void control_ack(uint8_t* rpt) const {
        std::memset(rpt, 0, kReport);
        rpt[0] = kIdIn;
        rpt[1] = last_cmd_;
    }

    // Het periodieke ingangsrapport uit de AllsUsbIo-pijp (64 bytes of null = niets ontvangen): byte 0 = id, byte 1.. = pijp[0..62].
    static void input_frame(const uint8_t* pipe64, uint8_t* rpt) {
        std::memset(rpt, 0, kReport);
        rpt[0] = kIdIn;
        if (pipe64) std::memcpy(rpt + 1, pipe64, kReport - 1);
    }

    uint8_t last_cmd() const { return last_cmd_; }
    uint8_t sampling() const { return sampling_; }
    uint8_t comm_timeout() const { return comm_timeout_; }
    bool status_cleared() const { return status_cleared_; }
    uint32_t requests() const { return count_; }
    uint32_t bad() const { return bad_; }

private:
    uint8_t last_cmd_ = 0, sampling_ = 0, comm_timeout_ = 0;
    bool status_cleared_ = false;
    uint32_t count_ = 0, bad_ = 0;
};

// Controle van de productstring zoals ParseNodeInfo hem leest (voor de test; de DLL levert dezelfde string). Geeft 0 bij goed, anders de
// nummer van de eerste afgekeurde eis.
inline int check_product_string(const std::wstring& s) {
    size_t pos = 0;
    std::wstring f[8];
    int n = 0;
    while (n < 7) {
        const size_t e = s.find(L';', pos);
        if (e == std::wstring::npos) return 1;   // minder dan 7 puntkomma's
        f[n++] = s.substr(pos, e - pos);
        pos = e + 1;
    }
    const std::wstring tail = s.substr(pos);
    if (s.size() >= 0x7f) return 2;                    // GetProductString levert hooguit 0xfe bytes
    if (f[0].size() >= 16 || f[0] != L"I/O CONTROL BD") return 3;
    if (f[1].size() >= 9) return 4;
    if (f[2].empty() || f[3].empty() || f[4].empty() || f[6].empty()) return 5;   // hex-velden
    if (f[4].size() > 4) return 6;                     // hex-woord
    if (f[5].size() >= 6) return 7;
    if (tail.find(L"COININ=") == std::wstring::npos || tail.find(L"SWIN=") == std::wstring::npos || tail.find(L"GOUT=") == std::wstring::npos) return 8;
    return 0;
}

}  // namespace rg_allsio
