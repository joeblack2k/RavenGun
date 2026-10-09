#include "ravengun/core/navcon.hpp"

namespace ravengun::core {

uint32_t nav_button_bit(const std::string& name) {
    for (const auto& b : kNavButtonNames)
        if (name == b.name) return b.bit;
    return 0;
}

namespace {
// Eén richting van de stick met hysterese: d = afstand tot het midden in die richting (positief = die kant op).
bool dir_on(bool was, int d) { return was ? d > kNavStickOff : d > kNavStickOn; }
}  // namespace

bool parse_nav_report(const uint8_t* b, size_t n, const NavState& prev, NavState& out) {
    if (!b || n < 19 || b[0] != 0x01) return false;
    NavState s;
    uint32_t m = 0;
    if (b[2] & 0x02) m |= kNavL3;
    if (b[2] & 0x10) m |= kNavUp;
    if (b[2] & 0x20) m |= kNavRight;
    if (b[2] & 0x40) m |= kNavDown;
    if (b[2] & 0x80) m |= kNavLeft;
    if (b[3] & 0x01) m |= kNavL2;
    if (b[3] & 0x04) m |= kNavL1;
    if (b[3] & 0x20) m |= kNavCircle;
    if (b[3] & 0x40) m |= kNavCross;
    if (b[4] & 0x01) m |= kNavPS;
    s.stick_x = b[6];
    s.stick_y = b[7];
    s.l2 = b[18];
    const int dx = s.stick_x - 128, dy = s.stick_y - 128;
    if (dir_on(prev.buttons & kNavStickUp, -dy)) m |= kNavStickUp;
    if (dir_on(prev.buttons & kNavStickDown, dy)) m |= kNavStickDown;
    if (dir_on(prev.buttons & kNavStickLeft, -dx)) m |= kNavStickLeft;
    if (dir_on(prev.buttons & kNavStickRight, dx)) m |= kNavStickRight;
    s.buttons = m;
    out = s;
    return true;
}

}  // namespace ravengun::core
