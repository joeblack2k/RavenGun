#pragma once
// Eigen parsing van het ruwe ZCM1-inputrapport (zoals het uit hidraw komt, inclusief report-ID).
//
// Waarom: PSMoveAPI (commit 256b629) geeft de interne tijdstempel van de Move niet via de publieke
// API. In plaats van een patch op PSMoveAPI leest de daemon dezelfde rapporten via een tweede,
// alleen-lezen hidraw-descriptor en parseert hier de bytes (zie docs/decisions.md, fase 1).
// De byte-indeling volgt de struct-commentaren in PSMoveAPI src/psmove.c
// (PSMove_Data_Input_Common en PSMove_ZCM1_Data_Input; BSD-2-clause, Thomas Perl). Er is geen
// code overgenomen, alleen de offsets.
//
// Indeling (offset in bytes, rapport begint met type 0x01):
//   1..4   knoppen (byte 4 bits 0-3: 4-bit volgnummer)
//   5, 6   trigger (twee frames)
//   0x0B   tijdstempel, hoge byte;  0x2B (43) tijdstempel, lage byte
//   12     batterij
//   13..24 accelerometer, 2 frames x 3 assen, little-endian, offset 0x8000
//   25..36 gyroscoop, idem
//   37, 38 temperatuur (12 bit: byte37 << 4 | byte38 >> 4); byte38 bits 0-3: magnetometer X hoog
//   39..42 magnetometer (12 bit signed per as)
//   4      bit 0x10 (buttons4): uitbreidingsconnector (EXT) heeft een apparaat (psmove_is_ext_connected)
//   44..48 5 bytes uitbreidingsdata (PSMove_ZCM1_Data_Input.extdata, PSMOVE_EXT_DATA_BUF_SIZE = 5);
//          het volledige ZCM1-rapport is 49 bytes. Sharp Shooter, byte 44: 0x01/0x02/0x04 = vuurstand-
//          bits (bits 0-2, `c & 7`), 0x40 = fire, 0x80 = RL (pomp/herladen) volgens test_extension.c.
#include <cstddef>
#include <cstdint>

namespace ravengun::core {

// Eenheid van de interne tijdstempel: gemeten op deze ZCM1 (7 okt 2026) als 100 000 ticks/s
// (10 us per tick), 16 bit, wrap na 0,65536 s. Zie docs/aim-fusion-design.md, "Metingen".
inline constexpr double kMoveTicksPerSecond = 100000.0;
inline constexpr int64_t kMoveNsPerTick = 10'000;
inline constexpr int kMoveStampModulo = 65536;
inline constexpr size_t kZcm1ReportMinBytes = 44;
inline constexpr size_t kZcm1ReportFullBytes = 49;  // met de 5 EXT-bytes
inline constexpr int kExtDataBytes = 5;

struct MoveRawInput {
    int seq = -1;               // 0..15
    uint16_t stamp = 0;         // 16-bit interne tijdstempel
    int temperature_raw = 0;    // 12 bit, zoals psmove_get_temperature()
    int mag[3]{0, 0, 0};        // 12 bit signed, ruwe sensorassen (Y niet omgedraaid)
    int accel_raw[2][3]{};      // signed (waarde - 0x8000), frame 0 = oudste
    int gyro_raw[2][3]{};
    int battery = 0;
    uint8_t trigger[2]{0, 0};
    bool has_ext = false;       // rapport was lang genoeg (49 bytes) voor de EXT-bytes
    bool ext_connected = false; // buttons4 & 0x10
    uint8_t ext[kExtDataBytes]{0, 0, 0, 0, 0};
};

// false bij een te kort rapport of een verkeerd report-type.
bool parse_zcm1_report(const uint8_t* buf, size_t len, MoveRawInput& out);

// Gemiddelde van beide frames zoals psmove_get_accelerometer() het berekent (voor het koppelen
// van een hidraw-rapport aan het rapport dat PSMoveAPI las).
void psmoveapi_accel_average(const MoveRawInput& in, int out[3]);

// Verschil b - a in ticks modulo 2^16 (0..65535).
inline int stamp_delta(uint16_t a, uint16_t b) { return static_cast<int>(static_cast<uint16_t>(b - a)); }

}  // namespace ravengun::core
