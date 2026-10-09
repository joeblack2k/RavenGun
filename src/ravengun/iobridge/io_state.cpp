#include "ravengun/iobridge/io_state.hpp"

namespace ravengun::iobridge {

namespace {

// "p1.start" -> (0, "start"); false als het geen speler-signaal is.
bool split_player(const std::string& name, int& idx, std::string& rest) {
    if (name.size() < 4 || name[0] != 'p' || name[1] < '1' || name[1] > '4' || name[2] != '.') return false;
    idx = name[1] - '1';
    rest = name.substr(3);
    return !rest.empty();
}

}  // namespace

bool io_button_signal(const IoState& s, const std::string& name, bool& v) {
    if (name == "test") { v = s.sys.test; return true; }
    if (name == "service") { v = s.sys.service; return true; }
    if (name == "volume_up") { v = s.sys.volume_up; return true; }
    if (name == "volume_down") { v = s.sys.volume_down; return true; }
    if (name.size() == 5 && name.compare(0, 4, "coin") == 0 && name[4] >= '1' && name[4] <= '4') {
        v = s.sys.coin[static_cast<size_t>(name[4] - '1')];
        return true;
    }
    int i;
    std::string r;
    if (!split_player(name, i, r)) return false;
    const IoPlayer& p = s.player[static_cast<size_t>(i)];
    if (r == "start") v = p.start;
    else if (r == "trigger") v = p.trigger;
    else if (r == "reload") v = p.reload;
    else if (r == "grenade") v = p.grenade;
    else if (r == "aux1") v = p.aux1;
    else if (r == "aux2") v = p.aux2;
    else return false;
    return true;
}

bool io_axis_signal(const IoState& s, const std::string& name, double& v) {
    int i;
    std::string r;
    if (!split_player(name, i, r)) return false;
    const IoPlayer& p = s.player[static_cast<size_t>(i)];
    if (r == "x") v = p.x;
    else if (r == "y") v = p.y;
    else return false;
    return true;
}

bool valid_button_signal(const std::string& name) {
    IoState s;
    bool v;
    return io_button_signal(s, name, v);
}

bool valid_axis_signal(const std::string& name) {
    IoState s;
    double v;
    return io_axis_signal(s, name, v);
}

}  // namespace ravengun::iobridge
