#pragma once
// Tweede, alleen-lezende hidraw-descriptor op dezelfde Move (fase 1).
//
// De kernel bezorgt elk inputrapport aan iedere open hidraw-descriptor. PSMoveAPI leest via zijn
// eigen descriptor (hidapi) en levert de gekalibreerde waarden; deze tap leest dezelfde rapporten
// en levert wat PSMoveAPI niet publiek maakt: de interne tijdstempel, de temperatuur en de ruwe
// sensorwaarden (eigen parsing, zie core/move_report.hpp). Bovendien kan de HID-thread op deze
// descriptor wachten met poll() in plaats van elke milliseconde te pollen.
//
// Koppeling: een PSMoveAPI-rapport hoort bij het oudste tap-rapport met hetzelfde 4-bit
// volgnummer en hetzelfde (door PSMoveAPI gemiddelde) ruwe accelerometerpaar. Oudere tap-rapporten
// zonder tegenhanger worden weggegooid en geteld. Lukt koppelen niet, dan krijgt het rapport geen
// stempel (de pijplijn valt dan voor dat rapport terug op de ontvangsttijd).
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "ravengun/core/move_report.hpp"

namespace ravengund {

class HidrawTap {
public:
    ~HidrawTap() { close(); }
    // Zoekt in /sys/class/hidraw de node met HID_UNIQ == adres (Bluetooth, Sony 054c:03d5) en
    // opent die O_RDONLY|O_NONBLOCK. false met err als dat niet lukt.
    bool open_for(const std::string& address, std::string& err);
    void close();
    bool is_open() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    const std::string& node() const { return node_; }
    // Leest alles wat klaarstaat (niet-blokkerend).
    void drain();
    // Haalt het bij een PSMoveAPI-rapport horende tap-rapport op.
    std::optional<ravengun::core::MoveRawInput> take(int seq, const int accel_avg[3]);

    uint64_t reads = 0, matched = 0, unmatched = 0, discarded = 0, parse_errors = 0, overflow = 0;

private:
    int fd_ = -1;
    std::string node_;
    std::deque<ravengun::core::MoveRawInput> q_;
};

}  // namespace ravengund
