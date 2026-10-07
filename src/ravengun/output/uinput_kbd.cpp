#include "ravengun/output/uinput_kbd.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "ravengun/output/key_table.hpp"

namespace ravengun::output {

bool UinputKeyboard::create(std::string& err) {
    if (fd_ >= 0) return true;
    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        err = std::string("/dev/uinput openen mislukt (toetsenbord): ") + std::strerror(errno);
        return false;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
    for (const auto& k : kKeyNames) ok = ok && ioctl(fd, UI_SET_KEYBIT, k.code) == 0;
    uinput_setup us{};
    us.id.bustype = BUS_USB;
    us.id.vendor = kKbdVendorId;
    us.id.product = kKbdProductId;
    us.id.version = 1;
    std::strncpy(us.name, kKbdDeviceName, UINPUT_MAX_NAME_SIZE - 1);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &us) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        err = std::string("uinput instellen mislukt (toetsenbord): ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    fd_ = fd;
    for (int i = 0; i < 40 && event_node().empty(); ++i) {
        timespec t{0, 50'000'000L};
        nanosleep(&t, nullptr);
    }
    return true;
}

void UinputKeyboard::destroy() {
    if (fd_ < 0) return;
    ioctl(fd_, UI_DEV_DESTROY);
    ::close(fd_);
    fd_ = -1;
}

bool UinputKeyboard::write_key(int code, bool down) {
    if (fd_ < 0) return false;
    input_event ev[2]{};
    ev[0].type = EV_KEY;
    ev[0].code = static_cast<__u16>(code);
    ev[0].value = down ? 1 : 0;
    ev[1].type = EV_SYN;
    ev[1].code = SYN_REPORT;
    ssize_t n;
    do { n = ::write(fd_, ev, sizeof ev); } while (n < 0 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof ev);
}

std::string UinputKeyboard::event_node() const {
    char buf[256];
    ssize_t n = ::readlink(kKbdStableLink, buf, sizeof buf - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    std::string t = buf;
    if (!t.empty() && t[0] != '/') t = "/dev/input/" + t;
    return t;
}

}  // namespace ravengun::output
