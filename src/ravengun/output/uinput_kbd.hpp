#pragma once
// uinput-backend van het virtuele toetsenbord (zelfde identiteit als het hulpprogramma vinput kbd).
//
// Identiteit: naam "RavenGun Virtual Keyboard", bus USB, VID 1209, PID 7201, versie 1, toetsenset =
// output/key_table.hpp. De udev-regel (packaging/udev/70-ravengun.rules) maakt
// /dev/input/ravengun-kbd en /dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd (het pad dat
// TeknoParrot als binding opslaat).
#include <string>

namespace ravengun::output {

inline constexpr unsigned short kKbdVendorId = 0x1209;
inline constexpr unsigned short kKbdProductId = 0x7201;
inline constexpr const char* kKbdDeviceName = "RavenGun Virtual Keyboard";
inline constexpr const char* kKbdStableLink = "/dev/input/ravengun-kbd";

// Abstract toetsenbord. Alleen schrijven (net als GunDevice).
class KeyDevice {
public:
    virtual ~KeyDevice() = default;
    virtual bool create(std::string& err) = 0;   // idempotent
    virtual void destroy() = 0;
    virtual bool created() const = 0;
    virtual bool write_key(int code, bool down) = 0;  // EV_KEY + SYN_REPORT
};

class UinputKeyboard : public KeyDevice {
public:
    UinputKeyboard() = default;
    ~UinputKeyboard() override { destroy(); }
    UinputKeyboard(const UinputKeyboard&) = delete;
    UinputKeyboard& operator=(const UinputKeyboard&) = delete;

    bool create(std::string& err) override;
    void destroy() override;
    bool created() const override { return fd_ >= 0; }
    bool write_key(int code, bool down) override;

    std::string event_node() const;  // waar kKbdStableLink naar wijst, leeg als hij niet bestaat

private:
    int fd_ = -1;
};

}  // namespace ravengun::output
