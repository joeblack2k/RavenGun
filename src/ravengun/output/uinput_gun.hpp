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
#include <utility>

#include "ravengun/output/gun_output.hpp"

namespace ravengun::output {

inline constexpr unsigned short kVendorId = 0x1209;
inline constexpr unsigned short kProductId = 0x7202;
inline constexpr const char* kDeviceName = "RavenGun Virtual Lightgun";
inline constexpr const char* kStableLink = "/dev/input/ravengun-gun";

// Identiteit van de virtuele gun van een speler. Speler 1 is EXACT de oorspronkelijke gun (1209:7202, "RavenGun Virtual
// Lightgun", by-id ...Virtual_Lightgun-event-mouse), zodat bestaande TeknoParrot-bindingen blijven werken. Speler 2..4 krijgen
// PID 7203..7205, naam "RavenGun Virtual Lightgun P<n>" en by-id ...Virtual_Lightgun_P<n>-event-mouse (packaging/udev/70-ravengun.rules).
struct GunIdentity {
    int player = 1;
    unsigned short vendor = kVendorId, product = kProductId;
    std::string name = kDeviceName;
    std::string stable_link = kStableLink;                                       // eigen symlink (udev)
    std::string by_id_link = "/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse";  // wat TeknoParrot bewaart
};
inline constexpr int kMaxGunPlayers = 4;
// player 1..4 (buiten dat bereik: speler 1).
GunIdentity gun_identity(int player);

class UinputGun : public GunDevice {
public:
    UinputGun() = default;
    explicit UinputGun(GunIdentity id) : id_(std::move(id)) {}
    const GunIdentity& identity() const { return id_; }
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
    GunIdentity id_{};
    int fd_ = -1;
    std::string sysname_;
};

}  // namespace ravengun::output
