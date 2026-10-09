#include "ravengun/output/key_mux.hpp"

namespace ravengun::output {

bool KeyMux::press(int code) {
    int& n = count_[code];
    if (n++ > 0) return true;
    if (real_.write_key(code, true)) return true;
    n = 0;  // het echte apparaat heeft hem niet: niet als ingedrukt tellen
    return false;
}

bool KeyMux::unpress(int code) {
    auto it = count_.find(code);
    if (it == count_.end() || it->second <= 0) return true;
    if (--it->second > 0) return true;
    count_.erase(it);
    return real_.write_key(code, false);
}

void KeyMux::shutdown() {
    for (auto& p : ports_) p.destroy();
    count_.clear();
    real_.destroy();
}

bool KeyMux::Port::create(std::string& err) {
    if (!mux_->real_.created() && !mux_->real_.create(err)) return false;
    created_ = true;
    return true;
}

void KeyMux::Port::release() {
    for (int c : held_) mux_->unpress(c);
    held_.clear();
}

void KeyMux::Port::destroy() {
    release();
    created_ = false;
}

bool KeyMux::Port::write_key(int code, bool down) {
    if (!created()) return false;
    if (down) {
        if (held_.count(code)) return true;
        if (!mux_->press(code)) return false;
        held_.insert(code);
        return true;
    }
    if (!held_.count(code)) return true;
    held_.erase(code);
    return mux_->unpress(code);
}

}  // namespace ravengun::output
