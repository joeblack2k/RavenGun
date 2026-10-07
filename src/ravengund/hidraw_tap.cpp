#include "hidraw_tap.hpp"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

namespace ravengund {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

bool HidrawTap::open_for(const std::string& address, std::string& err) {
    close();
    std::string want = lower(address);
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) { err = "/sys/class/hidraw niet leesbaar"; return false; }
    std::string found;
    while (dirent* e = ::readdir(d)) {
        if (std::strncmp(e->d_name, "hidraw", 6) != 0) continue;
        std::ifstream f(std::string("/sys/class/hidraw/") + e->d_name + "/device/uevent");
        std::string line, uniq, id;
        while (std::getline(f, line)) {
            if (line.rfind("HID_UNIQ=", 0) == 0) uniq = lower(line.substr(9));
            if (line.rfind("HID_ID=", 0) == 0) id = lower(line.substr(7));
        }
        // Alleen ZCM1 via Bluetooth (bus 0005, Sony 054c, product 03d5): alleen dat rapport parsen we.
        if (uniq == want && id == "0005:0000054c:000003d5") { found = e->d_name; break; }
    }
    ::closedir(d);
    if (found.empty()) { err = "geen hidraw-node voor deze ZCM1 (Bluetooth) gevonden"; return false; }
    node_ = "/dev/" + found;
    fd_ = ::open(node_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) { err = node_ + ": " + std::strerror(errno); return false; }
    q_.clear();
    reads = matched = unmatched = discarded = parse_errors = overflow = 0;
    return true;
}

void HidrawTap::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    q_.clear();
}

void HidrawTap::drain() {
    if (fd_ < 0) return;
    uint8_t buf[128];
    for (int i = 0; i < 256; ++i) {
        ssize_t n = ::read(fd_, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        ++reads;
        ravengun::core::MoveRawInput in;
        if (!ravengun::core::parse_zcm1_report(buf, static_cast<size_t>(n), in)) { ++parse_errors; continue; }
        q_.push_back(in);
        if (q_.size() > 256) { q_.pop_front(); ++overflow; }
    }
}

std::optional<ravengun::core::MoveRawInput> HidrawTap::take(int seq, const int a[3]) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (size_t i = 0; i < q_.size(); ++i) {
            int avg[3];
            ravengun::core::psmoveapi_accel_average(q_[i], avg);
            if (q_[i].seq == seq && avg[0] == a[0] && avg[1] == a[1] && avg[2] == a[2]) {
                ravengun::core::MoveRawInput f = q_[i];
                discarded += i;  // oudere tap-rapporten zonder PSMoveAPI-tegenhanger
                q_.erase(q_.begin(), q_.begin() + static_cast<long>(i) + 1);
                ++matched;
                return f;
            }
        }
        drain();  // het rapport kan net na de vorige drain binnengekomen zijn
    }
    ++unmatched;
    return std::nullopt;
}

}  // namespace ravengund
