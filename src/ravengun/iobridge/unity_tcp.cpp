#include "ravengun/iobridge/unity_tcp.hpp"

namespace ravengun::iobridge {

using namespace ravengun::games;

UnityTcpDialect::UnityTcpDialect(AaGame game, int port, Source source, RecoilFn recoil)
    : game_(std::move(game)), port_(port), source_(std::move(source)), recoil_(std::move(recoil)) {}

bool UnityTcpDialect::open(std::string&) {
    if (client_.running()) return true;
    client_.start(
        game_, port_,
        [this](AaInputs& in) {
            IoState s;
            if (!source_ || !source_(s)) return;
            for (int p = 0; p < kAaPlayers && p < kIoPlayers; ++p) {
                const IoPlayer& pl = s.player[static_cast<size_t>(p)];
                if (!pl.valid) continue;
                auto& o = in[static_cast<size_t>(p)];
                o.active = true;
                o.u = pl.x;
                o.v = pl.y;
                o.trigger = pl.trigger;
                o.reload = pl.reload;
            }
        },
        [this](int player) { if (recoil_) recoil_(player); });
    return true;
}

void UnityTcpDialect::close() { client_.stop(); }

IoOutputs UnityTcpDialect::outputs() const {
    IoOutputs o;
    const AaClientStats s = client_.stats();
    o.have_outputs = s.have_outputs;
    for (int p = 0; p < kAaPlayers && p < kIoPlayers; ++p) {
        o.recoil_pulses[static_cast<size_t>(p)] = s.recoils[p];
        o.credits[static_cast<size_t>(p)] = s.have_outputs ? s.last.credits[p] : -1;
        o.start_lamp[static_cast<size_t>(p)] = s.have_outputs && s.last.playing[p] != 0;
    }
    return o;
}

std::string UnityTcpDialect::status_json() const { return client_.status_json(); }

}  // namespace ravengun::iobridge
