#include "ravengun/core/buttons.hpp"

namespace ravengun::core {

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
