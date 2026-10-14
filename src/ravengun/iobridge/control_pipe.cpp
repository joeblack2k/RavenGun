#include "ravengun/iobridge/control_pipe.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/web/web_core.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

const ControlPipeProfile* ControlPipeProfiles::find(const std::string& name) const {
    for (const auto& p : profiles)
        if (p.name == name) return &p;
    return nullptr;
}

namespace {

int int_or(const json::Value& o, const char* key, int def, bool& ok) {
    const json::Value* v = o.find(key);
    if (!v) return def;
    if (!v->is_number() || v->num != std::floor(v->num)) { ok = false; return def; }
    return static_cast<int>(v->num);
}

double num_or(const json::Value& o, const char* key, double def, bool& ok) {
    const json::Value* v = o.find(key);
    if (!v) return def;
    if (!v->is_number()) { ok = false; return def; }
    return v->num;
}

bool bool_or(const json::Value& o, const char* key, bool def, bool& ok) {
    const json::Value* v = o.find(key);
    if (!v) return def;
    if (!v->is_bool()) { ok = false; return def; }
    return v->b;
}

std::string str_or(const json::Value& o, const char* key, const std::string& def, bool& ok) {
    const json::Value* v = o.find(key);
    if (!v) return def;
    if (!v->is_string()) { ok = false; return def; }
    return v->str;
}

// "signals": ["a", "b"] of "signal": "a".
std::vector<std::string> strings(const json::Value& o, const char* list_key, const char* one_key, bool& ok) {
    std::vector<std::string> out;
    if (const json::Value* v = o.find(list_key)) {
        if (!v->is_array()) { ok = false; return out; }
        for (const auto& e : v->arr) {
            if (!e.is_string()) { ok = false; return out; }
            out.push_back(e.str);
        }
    }
    if (one_key)
        if (const json::Value* v = o.find(one_key)) {
            if (!v->is_string()) ok = false;
            else out.push_back(v->str);
        }
    return out;
}

bool any_on(const IoState& s, const std::vector<std::string>& names) {
    for (const auto& n : names) {
        bool v = false;
        if (io_button_signal(s, n, v) && v) return true;
    }
    return false;
}

}  // namespace

std::string validate_control_pipe_profile(const ControlPipeProfile& p) {
    const std::string pre = "control-pipe-profiles.json: " + p.name + ": ";
    if (p.name.empty()) return "control-pipe-profiles.json: een profiel heeft geen naam";
    if (p.pipe.empty() || p.pipe.find_first_of("\\/ ") != std::string::npos) return pre + "'pipe' moet een naam zonder \\, / of spatie zijn";
    if (p.size < 1 || p.size > 4096) return pre + "'size' moet 1..4096 zijn";
    if (p.interval_ms < 1 || p.interval_ms > 1000) return pre + "'interval_ms' moet 1..1000 zijn";
    if (p.idle_byte < 0 || p.idle_byte > 255) return pre + "'idle_byte' moet 0..255 zijn";
    std::set<int> owned;   // bytes van de kanalen en de munten
    if (p.analog_channels < 0 || p.analog_offset < 0 || p.analog_offset + 2 * p.analog_channels > p.size) return pre + "analoge kanalen buiten het rapport";
    for (int i = 0; i < 2 * p.analog_channels; ++i) owned.insert(p.analog_offset + i);
    std::set<int> chans;
    for (const auto& a : p.axes) {
        if (!valid_axis_signal(a.signal)) return pre + "onbekend as-signaal '" + a.signal + "'";
        if (a.channel < 0 || a.channel >= p.analog_channels) return pre + a.signal + ": kanaal buiten 0.." + std::to_string(p.analog_channels - 1);
        if (!chans.insert(a.channel).second) return pre + a.signal + ": kanaal " + std::to_string(a.channel) + " is al bezet";
        if (a.idle < 0 || a.idle > 1) return pre + a.signal + ": 'idle' moet 0..1 zijn";
    }
    for (const auto& c : p.coins) {
        if (c.signals.empty()) return pre + "een munt zonder signaal";
        for (const auto& s : c.signals)
            if (!valid_button_signal(s)) return pre + "onbekend knopsignaal '" + s + "' bij een munt";
        if (c.offset < 0 || c.offset + 2 > p.size) return pre + "munt buiten het rapport";
        if (c.scale < 1 || c.scale > 65535) return pre + "'scale' van een munt moet 1..65535 zijn";
        for (int i = 0; i < 2; ++i)
            if (!owned.insert(c.offset + i).second) return pre + "munt op offset " + std::to_string(c.offset) + " overlapt";
    }
    std::set<std::pair<int, int>> used;
    for (const auto& b : p.bits) {
        if (b.signals.empty()) return pre + "een bit zonder signaal";
        for (const auto* list : {&b.signals, &b.unless})
            for (const auto& s : *list)
                if (!valid_button_signal(s)) return pre + "onbekend knopsignaal '" + s + "'";
        if (b.offset < 0 || b.bit < 0 || b.bit > 31 || b.offset + b.bit / 8 >= p.size) return pre + "bit buiten het rapport: " + b.signals[0];
        const int byte = b.offset + b.bit / 8;
        if (owned.count(byte)) return pre + b.signals[0] + ": de bit ligt in een kanaal of een munt";
        if (!used.insert({byte, b.bit % 8}).second) return pre + "twee bits op dezelfde plek (" + b.signals[0] + ")";
    }
    return {};
}

ControlPipeProfiles parse_control_pipe_profiles(const std::string& text, std::string& err) {
    ControlPipeProfiles out;
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "control-pipe-profiles.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return {};
    }
    const json::Value* profs = pr.value.find("profiles");
    if (!profs || !profs->is_object()) { err = "control-pipe-profiles.json: 'profiles' ontbreekt"; return {}; }
    for (const auto& kv : profs->obj) {
        const json::Value& o = kv.second;
        if (!o.is_object()) { err = "control-pipe-profiles.json: " + kv.first + " is geen object"; return {}; }
        ControlPipeProfile p;
        bool ok = true;
        p.name = kv.first;
        p.description = str_or(o, "description", "", ok);
        p.pipe = str_or(o, "pipe", p.pipe, ok);
        p.size = int_or(o, "size", 64, ok);
        p.interval_ms = int_or(o, "interval_ms", 15, ok);
        if (const json::Value* a = o.find("analog")) {
            if (!a->is_object()) ok = false;
            else {
                p.analog_offset = int_or(*a, "offset", 0, ok);
                p.analog_channels = int_or(*a, "channels", 0, ok);
                p.idle_byte = int_or(*a, "idle_byte", 0, ok);
            }
        }
        auto each = [&](const char* key, auto&& f) {
            const json::Value* v = o.find(key);
            if (!v) return;
            if (!v->is_array()) { ok = false; return; }
            for (const auto& e : v->arr) {
                if (!e.is_object()) { ok = false; return; }
                f(e);
            }
        };
        each("axes", [&](const json::Value& e) {
            CpAxis a;
            a.signal = str_or(e, "signal", "", ok);
            a.channel = int_or(e, "channel", 0, ok);
            a.invert = bool_or(e, "invert", false, ok);
            a.idle = num_or(e, "idle", 0.5, ok);
            p.axes.push_back(std::move(a));
        });
        each("bits", [&](const json::Value& e) {
            CpBit b;
            b.signals = strings(e, "signals", "signal", ok);
            b.unless = strings(e, "unless", nullptr, ok);
            b.offset = int_or(e, "offset", 0, ok);
            b.bit = int_or(e, "bit", 0, ok);
            p.bits.push_back(std::move(b));
        });
        each("coins", [&](const json::Value& e) {
            CpCoin c;
            c.signals = strings(e, "signals", "signal", ok);
            c.offset = int_or(e, "offset", 0, ok);
            c.scale = int_or(e, "scale", 1, ok);
            p.coins.push_back(std::move(c));
        });
        if (!ok) { err = "control-pipe-profiles.json: " + kv.first + ": een veld heeft het verkeerde type"; return {}; }
        if (std::string bad = validate_control_pipe_profile(p); !bad.empty()) { err = bad; return {}; }
        out.profiles.push_back(std::move(p));
    }
    return out;
}

ControlPipeProfiles builtin_control_pipe_profiles(std::string& err) {
    const auto* a = ravengun::web::find_asset("control-pipe-profiles.json");
    if (!a) { err = "control-pipe-profiles.json is niet ingebouwd"; return {}; }
    return parse_control_pipe_profiles(std::string(a->data), err);
}

uint16_t control_pipe_analog(int byte) {
    const int b = std::clamp(byte, 0, 255);
    return static_cast<uint16_t>(std::min(b * 257 + 1, 65534));
}

std::vector<uint8_t> control_pipe_render(const ControlPipeProfile& p, const IoState& s, const std::vector<uint32_t>& coins) {
    std::vector<uint8_t> out(static_cast<size_t>(p.size), 0);
    auto put16 = [&](int off, uint16_t v) {
        out[static_cast<size_t>(off)] = static_cast<uint8_t>(v & 0xFF);
        out[static_cast<size_t>(off) + 1] = static_cast<uint8_t>(v >> 8);
    };
    std::vector<int> bytes(static_cast<size_t>(p.analog_channels), p.idle_byte);
    for (const auto& a : p.axes) {
        double f = a.idle;
        if (io_axis_live(s, a.signal)) io_axis_signal(s, a.signal, f);
        f = std::clamp(f, 0.0, 1.0);
        int b = static_cast<int>(std::lround(f * 255.0));
        if (a.invert) b = 255 - b;
        bytes[static_cast<size_t>(a.channel)] = b;
    }
    for (int i = 0; i < p.analog_channels; ++i) put16(p.analog_offset + 2 * i, control_pipe_analog(bytes[static_cast<size_t>(i)]));
    for (size_t i = 0; i < p.coins.size(); ++i) {
        const uint32_t n = i < coins.size() ? coins[i] : 0;
        put16(p.coins[i].offset, static_cast<uint16_t>((n * static_cast<uint32_t>(p.coins[i].scale)) & 0xFFFF));
    }
    for (const auto& b : p.bits)
        if (any_on(s, b.signals) && !any_on(s, b.unless)) out[static_cast<size_t>(b.offset + b.bit / 8)] |= static_cast<uint8_t>(1u << (b.bit % 8));
    return out;
}

ControlPipeDialect::ControlPipeDialect(ControlPipeProfile profile, std::string game, std::string path)
    : profile_(std::move(profile)), game_(std::move(game)), path_(std::move(path)) {
    coins_.assign(profile_.coins.size(), 0);
    coin_down_.assign(profile_.coins.size(), false);
}

bool ControlPipeDialect::open(std::string& err) {
    if (fd_ >= 0) return true;
    struct stat sb {};
    existed_ = ::stat(path_.c_str(), &sb) == 0;
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) { err = "kan " + path_ + " niet openen: " + std::strerror(errno); return false; }
    // Na een herstart van de daemon: de muntstanden uit het bestand overnemen, zodat de teller van de game niet terugloopt.
    if (existed_) {
        std::vector<uint8_t> old(static_cast<size_t>(profile_.size), 0);
        if (::pread(fd_, old.data(), old.size(), 0) == static_cast<ssize_t>(old.size()))
            for (size_t i = 0; i < profile_.coins.size(); ++i) {
                const auto& c = profile_.coins[i];
                const uint32_t v = old[static_cast<size_t>(c.offset)] | (old[static_cast<size_t>(c.offset) + 1] << 8);
                coins_[i] = v / static_cast<uint32_t>(c.scale);
            }
    }
    if (::ftruncate(fd_, profile_.size) != 0) {
        err = "kan " + path_ + " niet op " + std::to_string(profile_.size) + " bytes zetten: " + std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    last_.clear();
    put(IoState{});
    return true;
}

void ControlPipeDialect::put(const IoState& s) {
    // Elke keer schrijven, ook als er niets veranderde: een vorig dialect op hetzelfde bestand (een herstart van de route) kan er bij
    // zijn afsluiten nog iets in gezet hebben. 64 bytes in tmpfs per tik is niets.
    std::vector<uint8_t> b = control_pipe_render(profile_, s, coins_);
    if (::pwrite(fd_, b.data(), b.size(), 0) == static_cast<ssize_t>(b.size()) && b != last_) {
        ++changes_;
        last_ = std::move(b);
    }
}

void ControlPipeDialect::write(const IoState& s) {
    if (fd_ < 0) return;
    ++writes_;
    for (size_t i = 0; i < profile_.coins.size(); ++i) {
        const bool down = any_on(s, profile_.coins[i].signals);
        if (coin_down_[i] && !down) ++coins_[i];   // TeknoParrotUi telt bij het loslaten
        coin_down_[i] = down;
    }
    put(s);
}

void ControlPipeDialect::neutral() {
    if (fd_ < 0) return;
    std::fill(coin_down_.begin(), coin_down_.end(), false);
    put(IoState{});
}

void ControlPipeDialect::close() {
    if (fd_ < 0) return;
    neutral();
    ::close(fd_);
    fd_ = -1;
}

std::string ControlPipeDialect::status_json() const {
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : last_) { hex += kHex[b >> 4]; hex += kHex[b & 15]; }
    json::Writer w;
    w.begin_object();
    w.key("profile").value(profile_.name);
    w.key("pipe").value(profile_.pipe);
    w.key("file").value(path_);
    w.key("file_existed").value(existed_);
    w.key("writes").value(writes_);
    w.key("changes").value(changes_);
    w.key("coins").begin_array();
    for (uint32_t c : coins_) w.value(static_cast<long long>(c));
    w.end_array();
    w.key("bytes").value(hex);
    w.end_object();
    return w.str();
}

}  // namespace ravengun::iobridge
