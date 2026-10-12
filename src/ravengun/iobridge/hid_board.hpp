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
//
// Een bord is niet per se een joystick. Drie velden maken de vorm instelbaar zonder code, alle drie met de bestaande
// waarde als standaard zodat een bord dat ze weglaat byte-voor-byte hetzelfde blijft:
//   application_usage       de usage van de Application-collectie (0x04 = Joystick, 0x05 = Game Pad, ...)
//   output_report_length    bytes die de GAME naar het bord schrijft (zet het Output-item in de beschrijving)
//   input_report_length     een vast, ondoorzichtig inputrapport in plaats van knoppen+assen
#include <cstdint>
#include <functional>
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
    // Uitvoerbereik binnen min..max (optioneel): het deel van het bereik dat de game als het hele scherm ziet. Een game die
    // het volle bereik zelf nog door een eigen kalibratie haalt (Transformers) krijgt zo alleen het stuk dat op het scherm valt.
    bool has_out = false;
    int out_min = 0, out_max = 0;
    double idle = 0.5;           // fractie als de speler geen geldige aim heeft
};

struct HidBoardButton {
    int index = 0;               // 0..button_count-1 (evdev: BTN_JOYSTICK + index; HID: knop index+1)
    std::string signal;          // bv. "p1.trigger", "test", "coin1" (zie io_button_signal)
};

// Een antwoord op een uitvoerrapport van de game: bij ontvangst van "00 <a> <b>" zet byte <offset> van ons
// inputrapport op <value>. Data, geen codepad: een bord dat antwoordt heeft geen eigen code nodig.
// De velden zijn int zodat de validator een waarde buiten bereik kan weigeren in plaats van hem stil af te kappen.
struct HidBoardResponse {
    int a = 0;                   // rapport[1] van het uitvoerrapport; 0..255
    int b = 0;                   // rapport[2]; 0..255
    int offset = 0;              // byte in ons inputrapport; 0..input_report_length-1
    int value = 0;               // wat daar komt; 0..255
};

struct HidBoard {
    std::string name;            // sleutel in hid-boards.json
    std::string description;
    std::string transport = "uinput";   // uinput | uhid
    std::string device_name = "RavenGun Board";
    int vendor = 0x1209, product = 0x72F0, version = 1;
    int button_count = 16;       // 1..16 voor uinput, 1..32 voor uhid
    // Usage van de Application-collectie op de Generic Desktop-pagina (0x0001). 0x04 = Joystick (standaard, alle
    // bestaande borden), 0x05 = Game Pad, 0x06 = Keyboard, 0x08 = Multi-axis Controller.
    int application_usage = 0x04;
    // Bytes die de GAME naar het bord schrijft. 0 = geen Output-item in de beschrijving, dus geen uitvoerrapport.
    // LET OP: dit getal staat in de rapportbeschrijving en wordt daarmee HidP_GetCaps().OutputReportByteLength. Een game
    // die WriteFile met precies die lengte doet (de Sega-shell doet dat), schrijft rommel of krijgt een fout als de
    // beschrijving een andere lengte opgeeft dan de game verwacht. Daarom staat het hier en niet als constante in de code.
    int output_report_length = 0;
    // Een vast, ondoorzichtig inputrapport van deze lengte in plaats van het berekende knoppen+assen-rapport. 0 = berekend.
    // Een bord dat dit zet, houdt 'axes' en 'buttons' leeg; de daemon vult de bytes zelf (HidBoardFrame::raw).
    int input_report_length = 0;
    // Antwoordtabel: welk uitvoerrapport welke byte in ons inputrapport zet (leeg = het bord antwoordt niet).
    std::vector<HidBoardResponse> responses;
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
// Heeft het bord een 'input_report_length', dan is 'raw' het hele rapport en doen axis/buttons niet mee.
struct HidBoardFrame {
    std::vector<int32_t> axis;
    uint32_t buttons = 0;
    std::vector<uint8_t> raw;
    bool operator==(const HidBoardFrame& o) const { return axis == o.axis && buttons == o.buttons && raw == o.raw; }
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

// Het uitvoerrapport dat de GAME naar het bord schrijft (alleen voor borden met 'output_report_length').
// Vorm, gemeten uit de Sega Game Shell van Transformers: Shadows Rising (patches/TransformersShadowsRising):
//   [0] = 0 (geen rapport-ID)   [1] = <a>   [2] = <b>   en verdere bytes ongebruikt.
// 'size' is de lengte uit het bord, niet het aantal ontvangen bytes; 'truncated' zegt of de game minder stuurde.
struct HidBoardOutput {
    size_t size = 0;
    uint8_t id = 0;
    uint8_t a = 0;
    uint8_t b = 0;
    bool truncated = false;
};
HidBoardOutput hid_board_parse_output(const HidBoard& b, const std::vector<uint8_t>& report);

// Welke respons hoort bij dit uitvoerrapport? nullptr = geen (het bord laat ons inputrapport dan ongemoeid).
const HidBoardResponse* hid_board_find_response(const HidBoard& b, int a, int b_byte);

// Het apparaat zelf (transport). Een nep-apparaat in de tests vervangt dit.
class HidBoardDevice {
public:
    virtual ~HidBoardDevice() = default;
    virtual bool create(const HidBoard& b, std::string& err) = 0;
    virtual bool send(const HidBoardFrame& f) = 0;          // alleen aangeroepen als het beeld veranderde (en als verversing)
    virtual void poll_output() {}                           // leest uitvoerrapporten van de game (uhid), zonder te blokkeren
    // Het laatst ontvangen uitvoerrapport (uhid). false = dit transport levert ze niet, of er is er nog geen.
    virtual bool last_output(std::vector<uint8_t>& out) const { (void)out; return false; }
    virtual uint64_t output_reports() const { return 0; }   // aantal ontvangen uitvoerrapporten
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
    std::vector<uint8_t> raw;          // het laatst verzonden vaste inputrapport (leeg bij een berekend rapport)
    uint64_t output_reports = 0;       // door de game ontvangen uitvoerrapporten
    std::vector<uint8_t> last_output;  // de bytes daarvan (leeg als er nog geen is)
    uint64_t responses_set = 0;        // keren dat een antwoordtabel een byte in ons rapport zette
    std::string last_response;         // mensleesbare regel over het laatst ontvangen uitvoerrapport
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
    // Regel per ontvangen uitvoerrapport (welke respons er gezet is, of dat er geen bij past). De brug zet hier
    // dezelfde soort haak op als het JVS-dialect, zodat het in het logboek van de daemon terechtkomt.
    void set_trace(std::function<void(const std::string&)> f) { trace_ = std::move(f); }

private:
    void push(const HidBoardFrame& f, bool force);
    HidBoardFrame with_raw(HidBoardFrame f) const;              // vast rapport: onze bytes erin
    void apply_response(const std::vector<uint8_t>& report);    // antwoordtabel toepassen op raw_
    HidBoard board_;
    std::string game_;
    std::unique_ptr<HidBoardDevice> dev_;
    HidBoardFrame last_;
    bool have_last_ = false;
    uint64_t since_send_ = 0;
    std::vector<uint8_t> raw_;         // ons vaste inputrapport (alleen bij input_report_length > 0)
    uint64_t outputs_done_ = 0;        // hoeveel uitvoerrapporten we al verwerkt hebben
    std::function<void(const std::string&)> trace_;
    HidBoardStats st_;
};

}  // namespace ravengun::iobridge
