#include "ravengun/core/move_report.hpp"

namespace ravengun::core {

namespace {
int twelve_bit_signed(int x) { return (x & 0x800) ? x - 0x1000 : x; }
int u16(const uint8_t* b, size_t lo) { return static_cast<int>(b[lo]) | (static_cast<int>(b[lo + 1]) << 8); }
}  // namespace

bool parse_zcm1_report(const uint8_t* b, size_t len, MoveRawInput& o) {
    if (!b || len < kZcm1ReportMinBytes || b[0] != 0x01) return false;
    o.seq = b[4] & 0x0F;
    o.trigger[0] = b[5];
    o.trigger[1] = b[6];
    o.stamp = static_cast<uint16_t>((b[11] << 8) | b[43]);
    o.battery = b[12];
    for (int f = 0; f < 2; ++f)
        for (int a = 0; a < 3; ++a) {
            o.accel_raw[f][a] = u16(b, 13 + static_cast<size_t>(f * 6 + a * 2)) - 0x8000;
            o.gyro_raw[f][a] = u16(b, 25 + static_cast<size_t>(f * 6 + a * 2)) - 0x8000;
        }
    o.temperature_raw = (b[37] << 4) | ((b[38] & 0xF0) >> 4);
    o.mag[0] = twelve_bit_signed(((b[38] & 0x0F) << 8) | b[39]);
    o.mag[1] = twelve_bit_signed((b[40] << 4) | ((b[41] & 0xF0) >> 4));
    o.mag[2] = twelve_bit_signed(((b[41] & 0x0F) << 8) | b[42]);
    return true;
}

void psmoveapi_accel_average(const MoveRawInput& in, int out[3]) {
    // PSMoveAPI: ((lo1 + lo2) + ((hi1 + hi2) << 8)) / 2 - 0x8000 == (u1 + u2) / 2 - 0x8000 (int-deling).
    for (int a = 0; a < 3; ++a) {
        int u1 = in.accel_raw[0][a] + 0x8000, u2 = in.accel_raw[1][a] + 0x8000;
        out[a] = (u1 + u2) / 2 - 0x8000;
    }
}

}  // namespace ravengun::core
