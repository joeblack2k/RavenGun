#include "ravengun/iobridge/tp_shm.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

const TpShmProfile* TpShmProfiles::find(const std::string& name) const {
    for (const auto& p : profiles)
        if (p.name == name) return &p;
    return nullptr;
}

namespace {

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

std::string str_or(const json::Value& o, const char* key, const std::string& def) {
    const json::Value* v = o.find(key);
    return v && v->is_string() ? v->str : def;
}

}  // namespace

std::string validate_tp_shm_profile(const TpShmProfile& p) {
    const std::string pre = "tp-shm-profiles.json: " + p.name + ": ";
    if (p.name.empty()) return "tp-shm-profiles.json: een profiel heeft geen naam";
    if (p.size < 16 || p.size > 4096) return pre + "'size' moet 16..4096 zijn";
    std::set<std::pair<int, int>> used;
    for (const auto& b : p.bits) {
        if (!valid_button_signal(b.signal)) return pre + "onbekend knopsignaal '" + b.signal + "'";
        if (b.offset < 0 || b.bit < 0 || b.bit > 31 || b.offset + b.bit / 8 >= p.size) return pre + "bit buiten het geheugen: " + b.signal;
        if (!used.insert({b.offset, b.bit}).second) return pre + "twee signalen op dezelfde bit (" + b.signal + ")";
    }
    std::set<int> bytes;
    for (const auto& a : p.axes) {
        if (!valid_axis_signal(a.signal)) return pre + "onbekend as-signaal '" + a.signal + "'";
        if (a.bytes != 1 && a.bytes != 2) return pre + a.signal + ": 'bytes' moet 1 of 2 zijn";
        if (a.offset < 0 || a.offset + a.bytes > p.size) return pre + a.signal + ": offset buiten het geheugen";
        if (a.min < 0 || a.max < a.min || a.max > (a.bytes == 1 ? 255.0 : 65535.0)) return pre + a.signal + ": ongeldig min/max";
        if (a.idle < 0 || a.idle > 1) return pre + a.signal + ": 'idle' moet 0..1 zijn";
        for (int i = 0; i < a.bytes; ++i)
            if (!bytes.insert(a.offset + i).second) return pre + a.signal + ": overlapt een andere as";
    }
    if (p.mirror_first != -1 && (p.mirror_first < 0 || p.mirror_last < p.mirror_first || p.mirror_last >= p.size))
        return pre + "ongeldige 'mirror_bytes'";
    return {};
}

TpShmProfiles parse_tp_shm_profiles(const std::string& text, std::string& err) {
    TpShmProfiles out;
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "tp-shm-profiles.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return {};
    }
    const json::Value* profs = pr.value.find("profiles");
    if (!profs || !profs->is_object()) { err = "tp-shm-profiles.json: 'profiles' ontbreekt"; return {}; }
    for (const auto& kv : profs->obj) {
        const json::Value& o = kv.second;
        if (!o.is_object()) { err = "tp-shm-profiles.json: " + kv.first + " is geen object"; return {}; }
        TpShmProfile p;
        p.name = kv.first;
        p.description = str_or(o, "description", "");
        bool ok = true;
        p.size = static_cast<int>(num_or(o, "size", 64, ok));
        p.complete = bool_or(o, "complete", true, ok);
        if (const json::Value* m = o.find("mirror_bytes")) {
            if (m->is_array() && m->arr.size() == 2 && m->arr[0].is_number() && m->arr[1].is_number()) {
                p.mirror_first = static_cast<int>(m->arr[0].num);
                p.mirror_last = static_cast<int>(m->arr[1].num);
            } else if (m->is_array() && m->arr.empty()) {
                p.mirror_first = -1;
            } else ok = false;
        }
        if (const json::Value* bits = o.find("bits")) {
            if (!bits->is_array()) ok = false;
            else for (const auto& b : bits->arr) {
                if (!b.is_object()) { ok = false; break; }
                TpBit tb;
                tb.signal = str_or(b, "signal", "");
                tb.offset = static_cast<int>(num_or(b, "offset", 8, ok));
                tb.bit = static_cast<int>(num_or(b, "bit", 0, ok));
                p.bits.push_back(std::move(tb));
            }
        }
        if (const json::Value* axes = o.find("axes")) {
            if (!axes->is_array()) ok = false;
            else for (const auto& a : axes->arr) {
                if (!a.is_object()) { ok = false; break; }
                TpAxis ta;
                ta.signal = str_or(a, "signal", "");
                ta.offset = static_cast<int>(num_or(a, "offset", 12, ok));
                ta.bytes = static_cast<int>(num_or(a, "bytes", 1, ok));
                ta.min = num_or(a, "min", 0, ok);
                ta.max = num_or(a, "max", ta.bytes == 1 ? 255 : 65535, ok);
                ta.invert = bool_or(a, "invert", false, ok);
                ta.big_endian = bool_or(a, "big_endian", true, ok);
                ta.idle = num_or(a, "idle", 0.5, ok);
                p.axes.push_back(std::move(ta));
            }
        }
        if (!ok) { err = "tp-shm-profiles.json: " + kv.first + ": een veld heeft het verkeerde type"; return {}; }
        if (std::string bad = validate_tp_shm_profile(p); !bad.empty()) { err = bad; return {}; }
        out.profiles.push_back(std::move(p));
    }
    return out;
}

void tp_shm_render(const TpShmProfile& p, const IoState& s, uint8_t* bytes, size_t n) {
    if (n < static_cast<size_t>(p.size)) return;
    for (const auto& b : p.bits) {
        bool v = false;
        io_button_signal(s, b.signal, v);
        uint8_t& byte = bytes[b.offset + b.bit / 8];
        const uint8_t mask = static_cast<uint8_t>(1u << (b.bit % 8));
        byte = v ? static_cast<uint8_t>(byte | mask) : static_cast<uint8_t>(byte & ~mask);
    }
    for (const auto& a : p.axes) {
        double f = a.idle;
        int pi = a.signal[1] - '1';
        const IoPlayer& pl = s.player[static_cast<size_t>(pi)];
        if (pl.valid) io_axis_signal(s, a.signal, f);
        f = std::min(1.0, std::max(0.0, f));
        // Eerst afronden, dan pas inverteren (min+max-waarde): bij 0..255 het complement van de byte, zoals de bron het doet.
        double v = std::round(a.min + f * (a.max - a.min));
        if (a.invert) v = a.min + a.max - v;
        v = std::min(a.bytes == 1 ? 255.0 : 65535.0, std::max(0.0, v));
        const unsigned u = static_cast<unsigned>(v);
        if (a.bytes == 1) {
            bytes[a.offset] = static_cast<uint8_t>(u);
        } else if (a.big_endian) {
            bytes[a.offset] = static_cast<uint8_t>(u >> 8);
            bytes[a.offset + 1] = static_cast<uint8_t>(u & 0xFF);
        } else {
            bytes[a.offset] = static_cast<uint8_t>(u & 0xFF);
            bytes[a.offset + 1] = static_cast<uint8_t>(u >> 8);
        }
    }
}

std::vector<int> tp_shm_owned_bytes(const TpShmProfile& p) {
    std::set<int> s;
    for (const auto& b : p.bits) s.insert(b.offset + b.bit / 8);
    for (const auto& a : p.axes)
        for (int i = 0; i < a.bytes; ++i) s.insert(a.offset + i);
    return {s.begin(), s.end()};
}

TpShmDialect::TpShmDialect(TpShmProfile profile, std::string game, std::string path, std::string tpui_path)
    : profile_(std::move(profile)), game_(std::move(game)), path_(std::move(path)), tpui_path_(std::move(tpui_path)) {
    owned_ = tp_shm_owned_bytes(profile_);
    last_.assign(static_cast<size_t>(profile_.size), 0);
}

bool TpShmDialect::open(std::string& err) {
    if (map_) return true;
    struct stat sb {};
    st_.existed = ::stat(path_.c_str(), &sb) == 0;
    // 0644: pipehelper.exe (zelfde gebruiker) opent het bestand lezend en schrijvend.
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) { err = "kan " + path_ + " niet openen: " + std::strerror(errno); return false; }
    struct stat fs {};
    if (::fstat(fd_, &fs) != 0 || fs.st_size < profile_.size) {
        if (::ftruncate(fd_, profile_.size) != 0) {   // alleen verlengen: bestaande inhoud blijft staan
            err = "kan " + path_ + " niet op " + std::to_string(profile_.size) + " bytes zetten: " + std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            return false;
        }
    }
    void* m = ::mmap(nullptr, static_cast<size_t>(profile_.size), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED) {
        err = "mmap van " + path_ + " mislukt: " + std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    map_ = static_cast<uint8_t*>(m);
    if (profile_.mirror_first >= 0) {
        tpui_fd_ = ::open(tpui_path_.c_str(), O_RDONLY | O_CLOEXEC);
        st_.tpui_present = tpui_fd_ >= 0;
    }
    apply(IoState{});   // neutrale beginstand (een bestaand bestand houdt zijn overige bytes)
    return true;
}

void TpShmDialect::apply(const IoState& s) {
    // TeknoParrotUi maakt zijn eigen bestand pas bij zijn start: tot het er is elke ~250 schrijfacties opnieuw proberen.
    if (tpui_fd_ < 0 && profile_.mirror_first >= 0 && st_.writes % 250 == 0) {
        tpui_fd_ = ::open(tpui_path_.c_str(), O_RDONLY | O_CLOEXEC);
        st_.tpui_present = tpui_fd_ >= 0;
    }
    std::vector<uint8_t> buf(map_, map_ + profile_.size);
    tp_shm_render(profile_, s, buf.data(), buf.size());
    if (tpui_fd_ >= 0 && profile_.mirror_first >= 0) {
        uint8_t tb[64];
        const size_t cnt = static_cast<size_t>(profile_.mirror_last - profile_.mirror_first + 1);
        if (cnt <= sizeof tb) {
            const ssize_t r = ::pread(tpui_fd_, tb, cnt, profile_.mirror_first);
            if (r == static_cast<ssize_t>(cnt)) std::memcpy(buf.data() + profile_.mirror_first, tb, cnt);
        }
    }
    uint64_t changed = 0;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (map_[i] != buf[i]) {
            map_[i] = buf[i];
            ++changed;
        }
    }
    st_.bytes_changed += changed;
    last_ = std::move(buf);
    last_state_ = s;
}

void TpShmDialect::write(const IoState& s) {
    if (!map_) return;
    ++st_.writes;
    apply(s);
}

void TpShmDialect::neutral() {
    if (!map_) return;
    apply(IoState{});
}

void TpShmDialect::close() {
    if (map_) {
        apply(IoState{});
        ::munmap(map_, static_cast<size_t>(profile_.size));
        map_ = nullptr;
    }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    if (tpui_fd_ >= 0) { ::close(tpui_fd_); tpui_fd_ = -1; }
}

TpShmStats TpShmDialect::stats() const { return st_; }

std::vector<uint8_t> TpShmDialect::last_bytes() const { return last_; }

std::vector<uint8_t> TpShmDialect::read_file() const {
    std::vector<uint8_t> v(static_cast<size_t>(profile_.size), 0);
    int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (::pread(fd, v.data(), v.size(), 0) < 0) v.clear();
        ::close(fd);
    }
    return v;
}

std::string TpShmDialect::status_json() const {
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : last_) { hex += kHex[b >> 4]; hex += kHex[b & 15]; }
    // Bytes buiten wat het profiel bezit of overneemt die niet nul zijn: dat heeft de game (of iets anders) geschreven.
    std::vector<uint8_t> file = read_file();
    json::Writer w;
    w.begin_object();
    w.key("layout").value(profile_.name);
    w.key("file").value(path_);
    w.key("size").value(profile_.size);
    w.key("writes").value(st_.writes);
    w.key("bytes_changed").value(st_.bytes_changed);
    w.key("file_existed").value(st_.existed);
    w.key("tpui_present").value(st_.tpui_present);
    w.key("complete").value(profile_.complete);
    w.key("bytes").value(hex);
    std::set<int> own(owned_.begin(), owned_.end());
    w.key("foreign").begin_array();
    for (size_t i = 0; i < file.size(); ++i) {
        const int ii = static_cast<int>(i);
        const bool mirrored = profile_.mirror_first >= 0 && ii >= profile_.mirror_first && ii <= profile_.mirror_last;
        if (!own.count(ii) && !mirrored && file[i] != 0) w.value(std::to_string(i) + "=" + std::to_string(file[i]));
    }
    w.end_array();
    w.key("players").begin_array();
    for (int p = 0; p < kIoPlayers; ++p) {
        const IoPlayer& pl = last_state_.player[static_cast<size_t>(p)];
        w.begin_object().key("valid").value(pl.valid).key("x").value(pl.x).key("y").value(pl.y);
        std::string b;
        auto add = [&](bool on, const char* n) { if (on) { if (!b.empty()) b += ","; b += n; } };
        add(pl.trigger, "trigger"); add(pl.reload, "reload"); add(pl.grenade, "grenade"); add(pl.aux1, "aux1");
        add(pl.aux2, "aux2"); add(pl.start, "start"); add(last_state_.sys.coin[static_cast<size_t>(p)], "coin");
        w.key("buttons").value(b).end_object();
    }
    w.end_array();
    std::string sysb;
    auto adds = [&](bool on, const char* n) { if (on) { if (!sysb.empty()) sysb += ","; sysb += n; } };
    adds(last_state_.sys.test, "test"); adds(last_state_.sys.service, "service");
    adds(last_state_.sys.volume_up, "volume_up"); adds(last_state_.sys.volume_down, "volume_down");
    w.key("system").value(sysb);
    if (!st_.last_error.empty()) w.key("last_error").value(st_.last_error);
    w.end_object();
    return w.str();
}

}  // namespace ravengun::iobridge
