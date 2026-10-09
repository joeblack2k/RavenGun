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
    if (b.button_count < 1 || b.button_count > max_btn) return pre + "'button_count' buiten 1.." + std::to_string(max_btn);
    if (b.axes.empty() || b.axes.size() > 6) return pre + "een bord heeft 1 tot 6 assen";
    std::set<std::string> names;
    for (const auto& a : b.axes) {
        if (hid_board_evdev_axis(a.name) < 0) return pre + "onbekende as '" + a.name + "' (X, Y, Z, RX, RY, RZ)";
        if (!names.insert(a.name).second) return pre + "as '" + a.name + "' staat twee keer in de lijst";
        if (!valid_axis_signal(a.signal)) return pre + "onbekend as-signaal '" + a.signal + "'";
        if (a.min < -32767 || a.max > 32767 || a.min >= a.max) return pre + a.name + ": ongeldig min/max (-32767..32767)";
        if (a.idle < 0 || a.idle > 1) return pre + a.name + ": 'idle' moet 0..1 zijn";
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
                  int_field(o, "version", b.version, b.version) && int_field(o, "button_count", b.button_count, b.button_count);
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
    for (const auto& a : b.axes)
        f.axis.push_back(static_cast<int32_t>(std::lround(a.min + a.idle * (a.max - a.min))));
    return f;
}

HidBoardFrame hid_board_frame(const HidBoard& b, const IoState& s) {
    HidBoardFrame f = hid_board_neutral(b);
    if (!s.armed) return f;
    for (size_t i = 0; i < b.axes.size(); ++i) {
        const HidBoardAxis& a = b.axes[i];
        double v = 0;
        if (!io_axis_signal(s, a.signal, v)) continue;
        // Een speler zonder geldige aim blijft op idle.
        if (a.signal.size() > 2 && a.signal[0] == 'p' && a.signal[1] >= '1' && a.signal[1] <= '4' &&
            !s.player[static_cast<size_t>(a.signal[1] - '1')].valid)
            continue;
        v = std::clamp(v, 0.0, 1.0);
        if (a.invert) v = 1.0 - v;
        f.axis[i] = static_cast<int32_t>(std::lround(a.min + v * (a.max - a.min)));
    }
    for (const auto& k : b.buttons) {
        bool v = false;
        if (io_button_signal(s, k.signal, v) && v) f.buttons |= 1u << k.index;
    }
    return f;
}

size_t hid_board_report_size(const HidBoard& b) {
    return static_cast<size_t>((b.button_count + 7) / 8) + 2 * b.axes.size();
}

std::vector<uint8_t> hid_board_report(const HidBoard& b, const HidBoardFrame& f) {
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
    std::vector<uint8_t> d = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01};   // Generic Desktop, Joystick, Application
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
    d.push_back(0xC0);
    return d;
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
    // anders wacht de aanroeper in Wine seconden. Uitvoerrapporten (recoil, lampen) worden geteld.
    void poll_output() override {
        if (fd_ < 0) return;
        for (int i = 0; i < 16; ++i) {
            uhid_event ev{};
            ssize_t n = ::read(fd_, &ev, sizeof ev);
            if (n <= 0) return;
            switch (ev.type) {
                case UHID_OUTPUT: ++outputs_; break;
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
    push(hid_board_neutral(board_), true);
    return true;
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
}

void HidBoardDialect::write(const IoState& s) {
    ++st_.writes;
    if (!dev_ || !dev_->created()) return;
    dev_->poll_output();
    push(hid_board_frame(board_, s), false);
}

void HidBoardDialect::neutral() { push(hid_board_neutral(board_), true); }

void HidBoardDialect::close() {
    if (!dev_) return;
    if (dev_->created()) {
        push(hid_board_neutral(board_), true);
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
    if (!st_.last_error.empty()) w.key("error").value(st_.last_error);
    w.end_object();
    return w.str();
}

}  // namespace ravengun::iobridge
