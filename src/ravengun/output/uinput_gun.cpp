#include "ravengun/output/uinput_gun.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace ravengun::output {

namespace {

int evdev_button(GunButton b) {
    switch (b) {
        case GunButton::Trigger: return BTN_LEFT;
        case GunButton::Reload: return BTN_RIGHT;
        case GunButton::Grenade: return BTN_MIDDLE;
        case GunButton::Aux1: return BTN_SIDE;
        case GunButton::Aux2: return BTN_EXTRA;
    }
    return BTN_LEFT;
}

void sleep_ms(int ms) {
    timespec t{ms / 1000, (ms % 1000) * 1'000'000L};
    nanosleep(&t, nullptr);
}

}  // namespace

GunIdentity gun_identity(int player) {
    GunIdentity g;
    if (player < 2 || player > kMaxGunPlayers) return g;  // speler 1: de oorspronkelijke identiteit
    const std::string n = std::to_string(player);
    g.player = player;
    g.product = static_cast<unsigned short>(kProductId + (player - 1));  // 7203, 7204, 7205
    g.name = std::string(kDeviceName) + " P" + n;
    g.stable_link = std::string(kStableLink) + "-p" + n;
    g.by_id_link = "/dev/input/by-id/usb-RavenGun_Virtual_Lightgun_P" + n + "-event-mouse";
    return g;
}

bool UinputGun::create(std::string& err) {
    if (fd_ >= 0) return true;
    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        err = std::string("/dev/uinput openen mislukt: ") + std::strerror(errno);
        return false;
    }
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
    for (int i = 0; ok && i < kGunButtonCount; ++i) ok = ioctl(fd, UI_SET_KEYBIT, evdev_button(static_cast<GunButton>(i))) == 0;
    ok = ok && ioctl(fd, UI_SET_ABSBIT, ABS_X) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_Y) == 0;
    for (int a : {ABS_X, ABS_Y}) {
        uinput_abs_setup as{};
        as.code = static_cast<__u16>(a);
        as.absinfo.minimum = kAbsMin;
        as.absinfo.maximum = kAbsMax;
        ok = ok && ioctl(fd, UI_ABS_SETUP, &as) == 0;
    }
    uinput_setup us{};
    us.id.bustype = BUS_USB;
    us.id.vendor = id_.vendor;
    us.id.product = id_.product;
    us.id.version = 1;
    std::strncpy(us.name, id_.name.c_str(), UINPUT_MAX_NAME_SIZE - 1);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &us) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        err = std::string("uinput instellen mislukt: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    char name[64] = {0};
    if (ioctl(fd, UI_GET_SYSNAME(sizeof name), name) == 0) sysname_ = name;
    fd_ = fd;
    // Wacht (begrensd) tot udev de stabiele symlink heeft gemaakt, zodat een binding direct bestaat.
    for (int i = 0; i < 40 && event_node().empty(); ++i) sleep_ms(50);
    return true;
}

void UinputGun::destroy() {
    if (fd_ < 0) return;
    ioctl(fd_, UI_DEV_DESTROY);
    ::close(fd_);
    fd_ = -1;
    sysname_.clear();
}

bool UinputGun::emit(int type, int code, int value) {
    input_event e{};
    e.type = static_cast<__u16>(type);
    e.code = static_cast<__u16>(code);
    e.value = value;
    ssize_t n;
    do { n = ::write(fd_, &e, sizeof e); } while (n < 0 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof e);
}

bool UinputGun::write_position(int x, int y) {
    if (fd_ < 0) return false;
    return emit(EV_ABS, ABS_X, x) && emit(EV_ABS, ABS_Y, y) && emit(EV_SYN, SYN_REPORT, 0);
}

bool UinputGun::write_button(GunButton b, bool down) {
    if (fd_ < 0) return false;
    return emit(EV_KEY, evdev_button(b), down ? 1 : 0) && emit(EV_SYN, SYN_REPORT, 0);
}

std::string UinputGun::event_node() const {
    char buf[256];
    ssize_t n = ::readlink(id_.stable_link.c_str(), buf, sizeof buf - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    std::string t = buf;
    if (!t.empty() && t[0] != '/') t = "/dev/input/" + t;
    return t;
}

}  // namespace ravengun::output
