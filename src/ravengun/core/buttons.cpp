#include "ravengun/core/buttons.hpp"

#include <cstring>

namespace ravengun::core {

const char* to_string(ExtMode m) {
    switch (m) {
        case ExtMode::Auto: return "auto";
        case ExtMode::SharpShooter: return "sharp_shooter";
        case ExtMode::None: return "none";
    }
    return "auto";
}

bool parse_ext_mode(const char* s, ExtMode& out) {
    if (!s) return false;
    for (ExtMode m : {ExtMode::Auto, ExtMode::SharpShooter, ExtMode::None})
        if (std::strcmp(s, to_string(m)) == 0) { out = m; return true; }
    return false;
}

uint32_t ext_buttons(ExtMode mode, bool connected, const uint8_t ext[5]) {
    if (mode == ExtMode::None || !connected || !ext) return 0;
    // Sharp Shooter (PSMoveAPI src/utils/test_extension.c): byte 0, bits 0x01/0x02/0x04 = vuurstand, 0x40 fire, 0x80 RL.
    uint32_t b = 0;
    if (ext[0] & 0x80) b |= kBtnPump;
    if (ext[0] & 0x40) b |= kBtnSsFire;
    if (ext[0] & 0x01) b |= kBtnSel1;
    if (ext[0] & 0x02) b |= kBtnSel2;
    if (ext[0] & 0x04) b |= kBtnSel3;
    return b;
}

ButtonEdges EdgeAccumulator::update(uint32_t state) {
    ButtonEdges e;
    e.pressed = state & ~state_;
    e.released = state_ & ~state;
    e.state = state;
    for (int b = 0; b < 32; ++b) {
        uint32_t m = 1u << b;
        if (e.pressed & m) { e.press_count[b] = 1; if (acc_.press_count[b] < 0xFFFF) ++acc_.press_count[b]; }
        if (e.released & m) { e.release_count[b] = 1; if (acc_.release_count[b] < 0xFFFF) ++acc_.release_count[b]; }
    }
    acc_.pressed |= e.pressed;
    acc_.released |= e.released;
    state_ = state;
    acc_.state = state;
    return e;
}

ButtonEdges EdgeAccumulator::take() {
    ButtonEdges r = acc_;
    r.state = state_;
    acc_ = ButtonEdges{};
    acc_.state = state_;
    return r;
}

}  // namespace ravengun::core
