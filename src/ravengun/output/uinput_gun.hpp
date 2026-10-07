#pragma once
// uinput-backend van het virtuele lightgun-apparaat.
//
// Identiteit (stabiel, niet het eventN-nummer): naam "RavenGun Virtual Lightgun", bus USB,
// VID 1209 (pid.codes-testbereik), PID 7202. De udev-regel packaging/udev/70-ravengun.rules
// geeft de ingelogde gebruiker leesrecht en maakt de symlinks /dev/input/ravengun-gun en
// /dev/input/by-id/ravengun-lightgun-event-mouse (die laatste is wat TeknoParrot als stabiel
// DevicePath opslaat).
//
// Capabilities: EV_ABS ABS_X/ABS_Y 0..kAbsMax, EV_KEY BTN_LEFT (trigger), BTN_RIGHT (reload),
// BTN_MIDDLE (grenade), BTN_SIDE, BTN_EXTRA. Geen EV_REL en geen INPUT_PROP_DIRECT (dat is een
// touchscreen). BTN_LEFT plus ABS_X/ABS_Y laat de kernel een mousedev-handler ("mouseN") koppelen:
// alleen dan toont TeknoParrot het apparaat in de lijst "P1 Light Gun" (zie docs/architecture.md).
#include <string>

#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

inline constexpr unsigned short kVendorId = 0x1209;
inline constexpr unsigned short kProductId = 0x7202;
inline constexpr const char* kDeviceName = "RavenGun Virtual Lightgun";
inline constexpr const char* kStableLink = "/dev/input/ravengun-gun";

class UinputGun : public GunDevice {
public:
    UinputGun() = default;
    ~UinputGun() override { destroy(); }
    UinputGun(const UinputGun&) = delete;
    UinputGun& operator=(const UinputGun&) = delete;

    bool create(std::string& err) override;
    void destroy() override;
    bool created() const override { return fd_ >= 0; }
    bool write_position(int x, int y) override;
    bool write_button(GunButton b, bool down) override;

    // Waar de stabiele symlink nu naar wijst ("/dev/input/event35"), leeg als hij niet bestaat.
    std::string event_node() const;
    // Kernel-sysnaam ("input168") na create().
    const std::string& sysname() const { return sysname_; }

private:
    bool emit(int type, int code, int value);
    int fd_ = -1;
    std::string sysname_;
};

}  // namespace ravengun::output
