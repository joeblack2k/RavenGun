#include "ravengun/iobridge/hid_board.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uhid.h>
#include <linux/uinput.h>
#include <set>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <utility>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

const HidBoard* HidBoards::find(const std::string& name) const {
    for (const auto& b : boards)
        if (b.name == name) return &b;
    return nullptr;
}

int hid_board_evdev_axis(const std::string& n) {
    if (n == "X") return ABS_X;
    if (n == "Y") return ABS_Y;
    if (n == "Z") return ABS_Z;
    if (n == "RX") return ABS_RX;
    if (n == "RY") return ABS_RY;
    if (n == "RZ") return ABS_RZ;
    return -1;
}

int hid_board_hid_usage(const std::string& n) {
    if (n == "X") return 0x30;
    if (n == "Y") return 0x31;
    if (n == "Z") return 0x32;
    if (n == "RX") return 0x33;
    if (n == "RY") return 0x34;
    if (n == "RZ") return 0x35;
    return -1;
}

// ---------------------------------------------------------------- beschrijving (JSON)

namespace {

std::string str_or(const json::Value& o, const char* key, const std::string& def) {
    const json::Value* v = o.find(key);
    return v && v->is_string() ? v->str : def;
}

// Een getal of een tekst als "0x1209".
bool int_field(const json::Value& o, const char* key, int def, int& out) {
    out = def;
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (v->is_number()) { out = static_cast<int>(std::lround(v->num)); return true; }
    if (v->is_string() && !v->str.empty()) {
        char* end = nullptr;
        long x = std::strtol(v->str.c_str(), &end, 0);
        if (end && *end == 0) { out = static_cast<int>(x); return true; }
    }
    return false;
}

}  // namespace

std::string validate_hid_board(const HidBoard& b) {
    const std::string pre = "hid-boards.json: " + b.name + ": ";
    if (b.name.empty()) return "hid-boards.json: een bord heeft geen naam";
    if (b.transport != "uinput" && b.transport != "uhid") return pre + "'transport' moet uinput of uhid zijn";
    if (b.device_name.empty() || b.device_name.size() > 120) return pre + "ongeldige 'name'";
    if (b.vendor < 0 || b.vendor > 0xFFFF || b.product < 0 || b.product > 0xFFFF) return pre + "vendor/product buiten 0..65535";
    const int max_btn = b.transport == "uinput" ? 16 : 32;
    if (b.application_usage < 1 || b.application_usage > 0xFFFF) return pre + "'application_usage' buiten 1..65535";
    if (b.output_report_length < 0 || b.output_report_length > 64) return pre + "'output_report_length' buiten 0..64";
    if (b.output_report_length > 0 && b.transport != "uhid")
        return pre + "'output_report_length' vraagt transport uhid (uinput is evdev en kent geen uitvoerrapporten)";
    // Een vast inputrapport vervangt het berekende knoppen+assen-rapport; dan horen die twee leeg te zijn.
    if (b.input_report_length > 0) {
        if (b.input_report_length > 64) return pre + "'input_report_length' buiten 1..64";
        if (b.transport != "uhid") return pre + "'input_report_length' vraagt transport uhid";
        if (!b.axes.empty()) return pre + "'input_report_length' gaat samen met een leeg 'axes'";
        if (!b.buttons.empty()) return pre + "'input_report_length' gaat samen met een leeg 'buttons'";
        // Antwoordtabel: een bord dat liegt is erger dan een bord dat weigert, dus elke respons moet passen.
        std::set<std::pair<int, int>> seen;
        for (const auto& r : b.responses) {
            if (r.a < 0 || r.a > 0xFF) return pre + "'responses': 'a' buiten 0..255";
            if (r.b < 0 || r.b > 0xFF) return pre + "'responses': 'b' buiten 0..255";
            if (r.value < 0 || r.value > 0xFF) return pre + "'responses': 'value' buiten 0..255";
            if (r.offset < 0 || r.offset >= b.input_report_length)
                return pre + "'responses': offset " + std::to_string(r.offset) + " valt buiten het inputrapport van " +
                       std::to_string(b.input_report_length) + " bytes";
            if (!seen.insert({r.a, r.b}).second)
                return pre + "'responses': twee keer een respons voor hetzelfde rapport (" +
                       std::to_string(r.a) + ", " + std::to_string(r.b) + ")";
        }
        return {};
    }
    if (!b.responses.empty()) return pre + "'responses' vraagt 'input_report_length'";
    if (b.button_count < 1 || b.button_count > max_btn) return pre + "'button_count' buiten 1.." + std::to_string(max_btn);
    if (b.axes.empty() || b.axes.size() > 6) return pre + "een bord heeft 1 tot 6 assen";
    std::set<std::string> names;
    for (const auto& a : b.axes) {
        if (hid_board_evdev_axis(a.name) < 0) return pre + "onbekende as '" + a.name + "' (X, Y, Z, RX, RY, RZ)";
        if (!names.insert(a.name).second) return pre + "as '" + a.name + "' staat twee keer in de lijst";
        if (!valid_axis_signal(a.signal)) return pre + "onbekend as-signaal '" + a.signal + "'";
        if (a.min < -32767 || a.max > 32767 || a.min >= a.max) return pre + a.name + ": ongeldig min/max (-32767..32767)";
        if (a.idle < 0 || a.idle > 1) return pre + a.name + ": 'idle' moet 0..1 zijn";
        if (a.has_out && (a.out_min < a.min || a.out_max > a.max || a.out_min >= a.out_max))
            return pre + a.name + ": out_min/out_max moeten binnen min..max liggen en out_min < out_max";
    }
    std::set<int> idx;
    for (const auto& k : b.buttons) {
        if (k.index < 0 || k.index >= b.button_count) return pre + "knopindex " + std::to_string(k.index) + " buiten 0..button_count-1";
        if (!idx.insert(k.index).second) return pre + "knopindex " + std::to_string(k.index) + " staat twee keer in de lijst";
        if (!valid_button_signal(k.signal)) return pre + "onbekend knop-signaal '" + k.signal + "'";
    }
    return {};
}

HidBoards parse_hid_boards(const std::string& text, std::string& err) {
    HidBoards out;
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "hid-boards.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return {};
    }
    const json::Value* boards = pr.value.find("boards");
    if (!boards || !boards->is_object()) { err = "hid-boards.json: 'boards' ontbreekt"; return {}; }
    for (const auto& kv : boards->obj) {
        const json::Value& o = kv.second;
        if (!o.is_object()) { err = "hid-boards.json: " + kv.first + " is geen object"; return {}; }
        HidBoard b;
        b.name = kv.first;
        b.description = str_or(o, "description", "");
        b.transport = str_or(o, "transport", b.transport);
        b.device_name = str_or(o, "name", b.device_name);
        if (const json::Value* c = o.find("complete"); c && c->is_bool()) b.complete = c->b;
        bool ok = int_field(o, "vendor", b.vendor, b.vendor) && int_field(o, "product", b.product, b.product) &&
                  int_field(o, "version", b.version, b.version) && int_field(o, "button_count", b.button_count, b.button_count) &&
                  int_field(o, "application_usage", b.application_usage, b.application_usage) &&
                  int_field(o, "output_report_length", b.output_report_length, b.output_report_length) &&
                  int_field(o, "input_report_length", b.input_report_length, b.input_report_length);
        if (const json::Value* axes = o.find("axes")) {
            if (!axes->is_array()) ok = false;
            else for (const auto& a : axes->arr) {
                if (!a.is_object()) { ok = false; break; }
                HidBoardAxis ax;
                ax.name = str_or(a, "name", "");
                ax.signal = str_or(a, "signal", "");
                if (const json::Value* i = a.find("invert"); i && i->is_bool()) ax.invert = i->b;
                ok = ok && int_field(a, "min", ax.min, ax.min) && int_field(a, "max", ax.max, ax.max);
                if (const json::Value* i = a.find("idle"); i && i->is_number()) ax.idle = i->num;
                const bool o1 = a.find("out_min") != nullptr, o2 = a.find("out_max") != nullptr;
                if (o1 != o2) ok = false;   // allebei of geen van beide
                if (o1 && o2) {
                    ax.has_out = true;
                    ok = ok && int_field(a, "out_min", 0, ax.out_min) && int_field(a, "out_max", 0, ax.out_max);
                }
                b.axes.push_back(std::move(ax));
            }
        }
        if (const json::Value* btns = o.find("buttons")) {
            if (!btns->is_array()) ok = false;
            else for (const auto& k : btns->arr) {
                if (!k.is_object()) { ok = false; break; }
                HidBoardButton hb;
                ok = ok && int_field(k, "index", 0, hb.index);
                hb.signal = str_or(k, "signal", "");
                b.buttons.push_back(std::move(hb));
            }
        }
        if (const json::Value* rs = o.find("responses")) {
            if (!rs->is_array()) ok = false;
            else for (const auto& r : rs->arr) {
                if (!r.is_object()) { ok = false; break; }
                HidBoardResponse hr;
                ok = ok && int_field(r, "a", 0, hr.a) && int_field(r, "b", 0, hr.b) &&
                     int_field(r, "offset", 0, hr.offset) && int_field(r, "value", 0, hr.value);
                b.responses.push_back(hr);
            }
        }
        if (!ok) { err = "hid-boards.json: " + kv.first + ": een veld heeft het verkeerde type"; return {}; }
        const std::string v = validate_hid_board(b);
        if (!v.empty()) { err = v; return {}; }
        out.boards.push_back(std::move(b));
    }
    return out;
}

// ---------------------------------------------------------------- beeld en rapport

HidBoardFrame hid_board_neutral(const HidBoard& b) {
    HidBoardFrame f;
    for (const auto& a : b.axes) {
        const int lo = a.has_out ? a.out_min : a.min, hi = a.has_out ? a.out_max : a.max;
        f.axis.push_back(static_cast<int32_t>(std::lround(lo + a.idle * (hi - lo))));
    }
    return f;
}

HidBoardFrame hid_board_frame(const HidBoard& b, const IoState& s) {
    HidBoardFrame f = hid_board_neutral(b);
    if (!s.armed) return f;
    for (size_t i = 0; i < b.axes.size(); ++i) {
        const HidBoardAxis& a = b.axes[i];
        double v = 0;
        if (!io_axis_signal(s, a.signal, v)) continue;
        // Een as zonder levende bron (geen aim, geen gamepad; io_axis_live) blijft op idle.
        if (!io_axis_live(s, a.signal)) continue;
        v = std::clamp(v, 0.0, 1.0);
        if (a.invert) v = 1.0 - v;
        const int lo = a.has_out ? a.out_min : a.min, hi = a.has_out ? a.out_max : a.max;
        f.axis[i] = static_cast<int32_t>(std::lround(lo + v * (hi - lo)));
    }
    for (const auto& k : b.buttons) {
        bool v = false;
        if (io_button_signal(s, k.signal, v) && v) f.buttons |= 1u << k.index;
    }
    return f;
}

size_t hid_board_report_size(const HidBoard& b) {
    if (b.input_report_length > 0) return static_cast<size_t>(b.input_report_length);
    return static_cast<size_t>((b.button_count + 7) / 8) + 2 * b.axes.size();
}

std::vector<uint8_t> hid_board_report(const HidBoard& b, const HidBoardFrame& f) {
    // Een vast inputrapport: de daemon vult de bytes zelf. Te weinig bytes worden 0, te veel vallen weg, zodat het
    // verzonden rapport altijd precies 'input_report_length' bytes is (anders klopt de beschrijving niet meer).
    if (b.input_report_length > 0) {
        std::vector<uint8_t> r = f.raw;
        r.resize(static_cast<size_t>(b.input_report_length), 0);
        return r;
    }
    std::vector<uint8_t> r;
    const int nb = (b.button_count + 7) / 8;
    for (int i = 0; i < nb; ++i) r.push_back(static_cast<uint8_t>((f.buttons >> (8 * i)) & 0xFF));
    for (size_t i = 0; i < b.axes.size(); ++i) {
        const int32_t v = i < f.axis.size() ? f.axis[i] : 0;
        const auto u = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(v, -32768, 32767)));
        r.push_back(static_cast<uint8_t>(u & 0xFF));
        r.push_back(static_cast<uint8_t>(u >> 8));
    }
    return r;
}

std::vector<uint8_t> hid_board_descriptor(const HidBoard& b) {
    // Generic Desktop, <application_usage>, Application. Voor 0x04 (Joystick) zijn dit dezelfde bytes als voorheen.
    std::vector<uint8_t> d = {0x05, 0x01};
    if (b.application_usage <= 0xFF) {
        d.insert(d.end(), {0x09, static_cast<uint8_t>(b.application_usage)});
    } else {
        d.insert(d.end(), {0x0A, static_cast<uint8_t>(b.application_usage & 0xFF),
                           static_cast<uint8_t>(b.application_usage >> 8)});
    }
    d.insert(d.end(), {0xA1, 0x01});
    // Een vast inputrapport: ondoorzichtige bytes op de leverancierspagina (0xFF00), geen assen en geen knoppen.
    if (b.input_report_length > 0) {
        d.insert(d.end(), {0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,
                           0x95, static_cast<uint8_t>(b.input_report_length), 0x81, 0x02});
        if (b.output_report_length > 0)
            d.insert(d.end(), {0x75, 0x08, 0x95, static_cast<uint8_t>(b.output_report_length), 0x91, 0x02});
        d.push_back(0xC0);
        return d;
    }
    // Knoppen: 1..button_count als bitveld, aangevuld tot hele bytes.
    d.insert(d.end(), {0x05, 0x09, 0x19, 0x01, 0x29, static_cast<uint8_t>(b.button_count), 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
                       0x95, static_cast<uint8_t>(b.button_count), 0x81, 0x02});
    if (b.button_count % 8) d.insert(d.end(), {0x75, 0x01, 0x95, static_cast<uint8_t>(8 - b.button_count % 8), 0x81, 0x03});
    // Assen: elk 16 bit met signed logisch bereik.
    for (const auto& a : b.axes) {
        const auto lo = static_cast<uint16_t>(static_cast<int16_t>(a.min));
        const auto hi = static_cast<uint16_t>(static_cast<int16_t>(a.max));
        d.insert(d.end(), {0x05, 0x01, 0x09, static_cast<uint8_t>(hid_board_hid_usage(a.name)), 0x16, static_cast<uint8_t>(lo & 0xFF),
                           static_cast<uint8_t>(lo >> 8), 0x26, static_cast<uint8_t>(hi & 0xFF), static_cast<uint8_t>(hi >> 8), 0x75,
                           0x10, 0x95, 0x01, 0x81, 0x02});
    }
    // Uitvoerrapport (optioneel): de game schrijft dit naar het bord. Zonder dit item zegt HidP_GetCaps() dat het bord
    // geen uitvoerrapport heeft en dan kan de game niets sturen.
    if (b.output_report_length > 0)
        d.insert(d.end(), {0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,
                           0x95, static_cast<uint8_t>(b.output_report_length), 0x91, 0x02});
    d.push_back(0xC0);
    return d;
}

HidBoardOutput hid_board_parse_output(const HidBoard& b, const std::vector<uint8_t>& report) {
    HidBoardOutput o;
    o.size = static_cast<size_t>(b.output_report_length);
    o.truncated = report.size() < o.size;
    if (!report.empty()) o.id = report[0];
    if (report.size() > 1) o.a = report[1];
    if (report.size() > 2) o.b = report[2];
    return o;
}

const HidBoardResponse* hid_board_find_response(const HidBoard& b, int a, int b_byte) {
    for (const auto& r : b.responses)
        if (r.a == a && r.b == b_byte) return &r;
    return nullptr;
}

// ---------------------------------------------------------------- transport: uinput

namespace {

class UinputBoardDevice final : public HidBoardDevice {
public:
    ~UinputBoardDevice() override { destroy(); }

    bool create(const HidBoard& b, std::string& err) override {
        if (fd_ >= 0) return true;
        int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            err = std::string("/dev/uinput openen mislukt: ") + std::strerror(errno);
            return false;
        }
        bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 && ioctl(fd, UI_SET_EVBIT, EV_SYN) == 0;
        // Alle knoppen 0..button_count-1 aangeven (ook zonder signaal): Wine nummert ze op volgorde van de code.
        for (int i = 0; ok && i < b.button_count; ++i) ok = ioctl(fd, UI_SET_KEYBIT, BTN_JOYSTICK + i) == 0;
        for (const auto& a : b.axes) {
            const int code = hid_board_evdev_axis(a.name);
            ok = ok && ioctl(fd, UI_SET_ABSBIT, code) == 0;
            uinput_abs_setup as{};
            as.code = static_cast<__u16>(code);
            as.absinfo.minimum = a.min;
            as.absinfo.maximum = a.max;
            ok = ok && ioctl(fd, UI_ABS_SETUP, &as) == 0;
        }
        uinput_setup us{};
        us.id.bustype = BUS_USB;
        us.id.vendor = static_cast<__u16>(b.vendor);
        us.id.product = static_cast<__u16>(b.product);
        us.id.version = static_cast<__u16>(b.version);
        std::strncpy(us.name, b.device_name.c_str(), UINPUT_MAX_NAME_SIZE - 1);
        ok = ok && ioctl(fd, UI_DEV_SETUP, &us) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
        if (!ok) {
            err = std::string("uinput instellen mislukt: ") + std::strerror(errno);
            ::close(fd);
            return false;
        }
        char name[64] = {0};
        if (ioctl(fd, UI_GET_SYSNAME(sizeof name), name) == 0) sysname_ = name;
        fd_ = fd;
        board_ = b;
        have_ = false;
        send(hid_board_neutral(b));
        return true;
    }

    bool send(const HidBoardFrame& f) override {
        if (fd_ < 0) return false;
        bool ok = true, any = false;
        for (size_t i = 0; i < board_.axes.size() && i < f.axis.size(); ++i) {
            if (have_ && i < last_.axis.size() && last_.axis[i] == f.axis[i]) continue;
            ok = emit(EV_ABS, hid_board_evdev_axis(board_.axes[i].name), f.axis[i]) && ok;
            any = true;
        }
        for (int i = 0; i < board_.button_count; ++i) {
            const uint32_t bit = 1u << i;
            if (have_ && (last_.buttons & bit) == (f.buttons & bit)) continue;
            ok = emit(EV_KEY, BTN_JOYSTICK + i, (f.buttons & bit) ? 1 : 0) && ok;
            any = true;
        }
        if (any || !have_) ok = emit(EV_SYN, SYN_REPORT, 0) && ok;
        last_ = f;
        have_ = true;
        return ok;
    }

    void destroy() override {
        if (fd_ < 0) return;
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
        fd_ = -1;
        sysname_.clear();
    }
    bool created() const override { return fd_ >= 0; }
    std::string node() const override { return sysname_.empty() ? std::string() : "/sys/devices/virtual/input/" + sysname_; }

private:
    bool emit(int type, int code, int value) {
        input_event e{};
        e.type = static_cast<__u16>(type);
        e.code = static_cast<__u16>(code);
        e.value = value;
        ssize_t n;
        do { n = ::write(fd_, &e, sizeof e); } while (n < 0 && errno == EINTR);
        return n == static_cast<ssize_t>(sizeof e);
    }
    int fd_ = -1;
    std::string sysname_;
    HidBoard board_;
    HidBoardFrame last_;
    bool have_ = false;
};

// ---------------------------------------------------------------- transport: uhid

class UhidBoardDevice final : public HidBoardDevice {
public:
    explicit UhidBoardDevice(std::string path) : path_(std::move(path)) {}
    ~UhidBoardDevice() override { destroy(); }

    bool create(const HidBoard& b, std::string& err) override {
        if (fd_ >= 0) return true;
        int fd = ::open(path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            err = path_ + " openen mislukt: " + std::strerror(errno) +
                  (errno == EACCES ? " (zie packaging/udev/71-ravengun-uhid.rules en docs/procedures.md)" : "");
            return false;
        }
        const std::vector<uint8_t> rd = hid_board_descriptor(b);
        uhid_event ev{};
        ev.type = UHID_CREATE2;
        std::strncpy(reinterpret_cast<char*>(ev.u.create2.name), b.device_name.c_str(), sizeof ev.u.create2.name - 1);
        std::strncpy(reinterpret_cast<char*>(ev.u.create2.uniq), "ravengun-board", sizeof ev.u.create2.uniq - 1);
        ev.u.create2.rd_size = static_cast<__u16>(rd.size());
        ev.u.create2.bus = BUS_USB;
        ev.u.create2.vendor = static_cast<__u32>(b.vendor);
        ev.u.create2.product = static_cast<__u32>(b.product);
        ev.u.create2.version = static_cast<__u32>(b.version);
        ev.u.create2.country = 0;
        std::memcpy(ev.u.create2.rd_data, rd.data(), rd.size());
        if (!write_event(fd, ev)) {
            err = std::string("uhid: UHID_CREATE2 mislukt: ") + std::strerror(errno);
            ::close(fd);
            return false;
        }
        fd_ = fd;
        board_ = b;
        return true;
    }

    bool send(const HidBoardFrame& f) override {
        if (fd_ < 0) return false;
        const std::vector<uint8_t> r = hid_board_report(board_, f);
        uhid_event ev{};
        ev.type = UHID_INPUT2;
        ev.u.input2.size = static_cast<__u16>(r.size());
        std::memcpy(ev.u.input2.data, r.data(), r.size());
        return write_event(fd_, ev);
    }

    // Leest wat de kernel en de lezers van het apparaat sturen. GET_REPORT/SET_REPORT moeten een antwoord krijgen,
    // anders wacht de aanroeper in Wine seconden. Uitvoerrapporten (recoil, lampen) worden geteld en bewaard, zodat de
    // daemon en het statusrapport kunnen laten zien wat de game werkelijk stuurde.
    void poll_output() override {
        if (fd_ < 0) return;
        for (int i = 0; i < 16; ++i) {
            uhid_event ev{};
            ssize_t n = ::read(fd_, &ev, sizeof ev);
            if (n <= 0) return;
            switch (ev.type) {
                case UHID_OUTPUT:
                    ++outputs_;
                    last_output_.assign(ev.u.output.data, ev.u.output.data + ev.u.output.size);
                    break;
                case UHID_GET_REPORT: {
                    uhid_event rp{};
                    rp.type = UHID_GET_REPORT_REPLY;
                    rp.u.get_report_reply.id = ev.u.get_report.id;
                    rp.u.get_report_reply.err = EIO;
                    write_event(fd_, rp);
                    break;
                }
                case UHID_SET_REPORT: {
                    uhid_event rp{};
                    rp.type = UHID_SET_REPORT_REPLY;
                    rp.u.set_report_reply.id = ev.u.set_report.id;
                    rp.u.set_report_reply.err = 0;
                    write_event(fd_, rp);
                    break;
                }
                default: break;
            }
        }
    }

    void destroy() override {
        if (fd_ < 0) return;
        uhid_event ev{};
        ev.type = UHID_DESTROY;
        write_event(fd_, ev);
        ::close(fd_);
        fd_ = -1;
    }
    bool created() const override { return fd_ >= 0; }
    bool last_output(std::vector<uint8_t>& out) const override {
        if (last_output_.empty()) return false;
        out = last_output_;
        return true;
    }
    uint64_t output_reports() const override { return outputs_; }

private:
    static bool write_event(int fd, const uhid_event& ev) {
        ssize_t n;
        do { n = ::write(fd, &ev, sizeof ev); } while (n < 0 && errno == EINTR);
        return n == static_cast<ssize_t>(sizeof ev);
    }
    std::string path_;
    int fd_ = -1;
    HidBoard board_;
    uint64_t outputs_ = 0;
    std::vector<uint8_t> last_output_;
};

}  // namespace

std::unique_ptr<HidBoardDevice> make_uinput_board_device() { return std::make_unique<UinputBoardDevice>(); }
std::unique_ptr<HidBoardDevice> make_uhid_board_device(const std::string& path) { return std::make_unique<UhidBoardDevice>(path); }

// ---------------------------------------------------------------- dialect

HidBoardDialect::HidBoardDialect(HidBoard board, std::string game, std::unique_ptr<HidBoardDevice> dev)
    : board_(std::move(board)), game_(std::move(game)), dev_(std::move(dev)) {}

bool HidBoardDialect::open(std::string& err) {
    if (!dev_) { err = "geen apparaat voor het bord"; return false; }
    if (dev_->created()) return true;
    if (!dev_->create(board_, err)) return false;
    have_last_ = false;
    raw_.assign(static_cast<size_t>(board_.input_report_length > 0 ? board_.input_report_length : 0), 0);
    outputs_done_ = 0;
    push(with_raw(hid_board_neutral(board_)), true);
    return true;
}

// Een bord met een vast inputrapport stuurt onze eigen bytes; een berekend bord laat ze leeg.
HidBoardFrame HidBoardDialect::with_raw(HidBoardFrame f) const {
    if (board_.input_report_length > 0) f.raw = raw_;
    return f;
}

// Antwoordtabel: welk uitvoerrapport van de game zet welke byte in ons inputrapport. Past er geen, dan blijft ons
// rapport ongemoeid; dat is met opzet, want een bord dat op goed geluk bytes zet is erger dan een bord dat wacht.
void HidBoardDialect::apply_response(const std::vector<uint8_t>& report) {
    if (board_.input_report_length <= 0) return;
    const HidBoardOutput o = hid_board_parse_output(board_, report);
    char gezien[32];
    std::snprintf(gezien, sizeof gezien, "%02X %02X %02X", report.size() > 0 ? report[0] : 0, o.a, o.b);
    const HidBoardResponse* r = hid_board_find_response(board_, o.a, o.b);
    if (!r) {
        st_.last_response = std::string("uitvoerrapport ") + gezien + ": geen respons in de tabel, rapport ongemoeid";
    } else if (static_cast<size_t>(r->offset) >= raw_.size()) {
        // Kan niet gebeuren: de validator weigert zulke borden. Toch geen stille afkapping.
        st_.last_response = std::string("uitvoerrapport ") + gezien + ": respons valt buiten het rapport, niets gezet";
    } else {
        raw_[static_cast<size_t>(r->offset)] = static_cast<uint8_t>(r->value);
        ++st_.responses_set;
        char regel[96];
        std::snprintf(regel, sizeof regel, "uitvoerrapport %s -> raw[%d]=0x%02X", gezien, r->offset, r->value);
        st_.last_response = regel;
    }
    if (trace_) trace_(st_.last_response);
}

void HidBoardDialect::push(const HidBoardFrame& f, bool force) {
    if (!dev_ || !dev_->created()) return;
    ++since_send_;
    if (!force && have_last_ && f == last_ && since_send_ < 25) return;   // 25 tikken (100 ms) als verversing voor uhid-lezers
    if (!dev_->send(f)) st_.last_error = "verzenden naar het apparaat mislukt";
    else ++st_.frames_sent;
    last_ = f;
    have_last_ = true;
    since_send_ = 0;
    st_.buttons = f.buttons;
    st_.axis = f.axis;
    st_.raw = f.raw;
}

void HidBoardDialect::write(const IoState& s) {
    ++st_.writes;
    if (!dev_ || !dev_->created()) return;
    dev_->poll_output();
    st_.output_reports = dev_->output_reports();
    dev_->last_output(st_.last_output);   // false laat de vorige waarde staan: "laatst gezien" is het nuttige antwoord
    // Elk nieuw uitvoerrapport verwerken. De onderhandeling is strikt opeenvolgend (elke stap wacht op ons antwoord),
    // dus als er ooit twee in één tik binnenkomen is de laatste de juiste om op te antwoorden.
    if (st_.output_reports > outputs_done_) {
        outputs_done_ = st_.output_reports;
        apply_response(st_.last_output);
    }
    push(with_raw(hid_board_frame(board_, s)), false);
}

void HidBoardDialect::neutral() { push(with_raw(hid_board_neutral(board_)), true); }

void HidBoardDialect::close() {
    if (!dev_) return;
    if (dev_->created()) {
        push(with_raw(hid_board_neutral(board_)), true);
        dev_->destroy();
    }
}

std::string HidBoardDialect::status_json() const {
    json::Writer w;
    w.begin_object();
    w.key("board").value(board_.name);
    w.key("transport").value(board_.transport);
    w.key("device").value(board_.device_name);
    w.key("created").value(dev_ && dev_->created());
    w.key("writes").value(st_.writes);
    w.key("frames_sent").value(st_.frames_sent);
    w.key("buttons").value(static_cast<uint64_t>(st_.buttons));
    w.key("axes").begin_array();
    for (int32_t a : st_.axis) w.value(static_cast<int64_t>(a));
    w.end_array();
    if (!st_.raw.empty()) {
        w.key("raw").begin_array();
        for (uint8_t v : st_.raw) w.value(static_cast<int64_t>(v));
        w.end_array();
    }
    w.key("output_reports").value(st_.output_reports);
    w.key("responses_set").value(st_.responses_set);
    if (!st_.last_response.empty()) w.key("last_response").value(st_.last_response);
    if (!st_.last_output.empty()) {
        w.key("last_output").begin_array();
        for (uint8_t v : st_.last_output) w.value(static_cast<int64_t>(v));
        w.end_array();
        // Wat de game stuurde, in de vorm die dit dialect afspreekt (zie hid_board_parse_output): 0, <a>, <b>.
        const HidBoardOutput o = hid_board_parse_output(board_, st_.last_output);
        const HidBoardResponse* resp = hid_board_find_response(board_, o.a, o.b);
        w.key("last_output_parsed").begin_object();
        w.key("size").value(static_cast<uint64_t>(o.size));
        w.key("id").value(static_cast<int64_t>(o.id));
        w.key("a").value(static_cast<int64_t>(o.a));
        w.key("b").value(static_cast<int64_t>(o.b));
        w.key("truncated").value(o.truncated);
        if (resp) {
            w.key("response").begin_object();
            w.key("offset").value(static_cast<int64_t>(resp->offset));
            w.key("value").value(static_cast<int64_t>(resp->value));
            w.end_object();
        }
        w.end_object();
    }
    if (!st_.last_error.empty()) w.key("error").value(st_.last_error);
    w.end_object();
    return w.str();
}

// ---------------------------------------------------------------- meerdere borden op één route

HidBoardsDialect::HidBoardsDialect(std::string game, std::vector<std::unique_ptr<HidBoardDialect>> boards)
    : game_(std::move(game)), boards_(std::move(boards)) {}

bool HidBoardsDialect::open(std::string& err) {
    for (size_t i = 0; i < boards_.size(); ++i) {
        std::string e;
        if (!boards_[i]->open(e)) {
            // Niets half open laten staan: de borden die al open zijn gaan weer dicht.
            for (size_t j = 0; j < i; ++j) boards_[j]->close();
            err = e;
            return false;
        }
    }
    return true;
}

void HidBoardsDialect::write(const IoState& s) {
    for (auto& d : boards_) d->write(s);
}

void HidBoardsDialect::neutral() {
    for (auto& d : boards_) d->neutral();
}

void HidBoardsDialect::close() {
    for (auto& d : boards_) d->close();
}

std::string HidBoardsDialect::status_json() const {
    json::Writer w;
    w.begin_object();
    w.key("boards").begin_array();
    for (const auto& d : boards_) w.raw(d->status_json());
    w.end_array();
    w.end_object();
    return w.str();
}

void HidBoardsDialect::set_trace(std::function<void(const std::string&)> f) {
    for (auto& d : boards_) d->set_trace(f);
}

}  // namespace ravengun::iobridge
