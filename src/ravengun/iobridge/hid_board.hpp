#pragma once
// Dialect "hid-board": de daemon speelt het invoerbord van een game na als een virtuele joystick/HID-apparaat, zodat de
// game (via Wine/DirectInput of HID) zijn eigen bord "ziet". Het bord staat in packaging/hid-boards.json: assen (met
// schaal en inversie), knoppen (signaalnamen uit io_state.hpp), vendor/product en het transport. Een ander spel met een
// HID-bord heeft dus alleen een beschrijving nodig, geen code.
//
// Transporten:
//   uinput  een evdev-joystick via /dev/uinput (rechten: de gebruiker heeft ze al, ID_INPUT_JOYSTICK geeft uaccess).
//           Wine bouwt er zelf een HID-apparaat van (winebus, evdev-pad); DirectInput ziet een gewone joystick.
//   uhid    een echt HID-apparaat via /dev/uhid (rapportbeschrijving en rapporten hier gebouwd). Nodig als een game het
//           apparaat via SetupDi/HidD (hidraw) zoekt. /dev/uhid is standaard alleen voor root: zie packaging/udev/.
//
// Gemeten voor Transformers: Shadows Rising (docs/research/teknoparrot-io.md): Transformers2.exe leest GUID_Joystick via
// DirectInput 8 (c_dfDIJoystick, assen -1000..1000, 80 bytes DIJOYSTATE); er is geen VID/PID-filter.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ravengun/iobridge/dialect.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kDefaultUhidPath = "/dev/uhid";

struct HidBoardAxis {
    std::string name;            // X, Y, Z, RX, RY, RZ (HID-gebruik en evdev-ABS-code)
    std::string signal;          // bv. "p1.x" (zie io_axis_signal)
    bool invert = false;         // fractie wordt 1 - fractie
    int min = -1000, max = 1000; // waardebereik; ook de logische min/max in de rapportbeschrijving
    double idle = 0.5;           // fractie als de speler geen geldige aim heeft
};

struct HidBoardButton {
    int index = 0;               // 0..button_count-1 (evdev: BTN_JOYSTICK + index; HID: knop index+1)
    std::string signal;          // bv. "p1.trigger", "test", "coin1" (zie io_button_signal)
};

struct HidBoard {
    std::string name;            // sleutel in hid-boards.json
    std::string description;
    std::string transport = "uinput";   // uinput | uhid
    std::string device_name = "RavenGun Board";
    int vendor = 0x1209, product = 0x72F0, version = 1;
    int button_count = 16;       // 1..16 voor uinput, 1..32 voor uhid
    std::vector<HidBoardAxis> axes;
    std::vector<HidBoardButton> buttons;
    bool complete = true;
};

struct HidBoards {
    std::vector<HidBoard> boards;
    const HidBoard* find(const std::string& name) const;
};

// Leest packaging/hid-boards.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
HidBoards parse_hid_boards(const std::string& json_text, std::string& err);
std::string validate_hid_board(const HidBoard& b);   // leeg = geldig

// Een beeld van het bord: ruwe waarden per as (zelfde volgorde als board.axes) en een bitveld van knoppen.
struct HidBoardFrame {
    std::vector<int32_t> axis;
    uint32_t buttons = 0;
    bool operator==(const HidBoardFrame& o) const { return axis == o.axis && buttons == o.buttons; }
};

// Zuiver: toestand -> beeld. Niet gewapend of speler zonder geldige aim: as op idle; niet gewapend: geen knoppen.
HidBoardFrame hid_board_frame(const HidBoard& b, const IoState& s);
HidBoardFrame hid_board_neutral(const HidBoard& b);
// uhid: de rapportbeschrijving (HID report descriptor) en het input-rapport (geen rapport-ID): eerst de knoppen als bitveld
// (button_count bits, aangevuld tot hele bytes), daarna elke as als little-endian int16.
std::vector<uint8_t> hid_board_descriptor(const HidBoard& b);
std::vector<uint8_t> hid_board_report(const HidBoard& b, const HidBoardFrame& f);
size_t hid_board_report_size(const HidBoard& b);
int hid_board_evdev_axis(const std::string& name);   // ABS_*-code, -1 als onbekend
int hid_board_hid_usage(const std::string& name);    // 0x30.. voor X Y Z RX RY RZ, -1 als onbekend

// Het apparaat zelf (transport). Een nep-apparaat in de tests vervangt dit.
class HidBoardDevice {
public:
    virtual ~HidBoardDevice() = default;
    virtual bool create(const HidBoard& b, std::string& err) = 0;
    virtual bool send(const HidBoardFrame& f) = 0;          // alleen aangeroepen als het beeld veranderde (en als verversing)
    virtual void poll_output() {}                           // leest uitvoerrapporten van de game (uhid), zonder te blokkeren
    virtual void destroy() = 0;
    virtual bool created() const = 0;
    virtual std::string node() const { return {}; }
};

std::unique_ptr<HidBoardDevice> make_uinput_board_device();
std::unique_ptr<HidBoardDevice> make_uhid_board_device(const std::string& path = kDefaultUhidPath);

struct HidBoardStats {
    uint64_t writes = 0, frames_sent = 0;
    uint32_t buttons = 0;
    std::vector<int32_t> axis;
    std::string last_error;
};

class HidBoardDialect final : public IoDialect {
public:
    HidBoardDialect(HidBoard board, std::string game, std::unique_ptr<HidBoardDevice> dev);
    ~HidBoardDialect() override { close(); }
    const char* name() const override { return "hid-board"; }
    std::string game() const override { return game_; }
    bool open(std::string& err) override;
    void write(const IoState& s) override;
    void neutral() override;
    void close() override;
    std::string status_json() const override;

    const HidBoard& board() const { return board_; }
    HidBoardStats stats() const { return st_; }

private:
    void push(const HidBoardFrame& f, bool force);
    HidBoard board_;
    std::string game_;
    std::unique_ptr<HidBoardDevice> dev_;
    HidBoardFrame last_;
    bool have_last_ = false;
    uint64_t since_send_ = 0;
    HidBoardStats st_;
};

}  // namespace ravengun::iobridge
