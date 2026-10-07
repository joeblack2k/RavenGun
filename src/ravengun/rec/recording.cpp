#include "ravengun/rec/recording.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <unordered_set>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::rec {

namespace {

void num(std::string& s, double v) {
    if (!std::isfinite(v)) { s += "null"; return; }
    char buf[40];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    s.append(buf, r.ptr);
}
void num(std::string& s, int64_t v) { s += std::to_string(v); }
void num(std::string& s, int v) { s += std::to_string(v); }
void num(std::string& s, uint64_t v) { s += std::to_string(v); }

void vec2(std::string& s, const core::Vec3 (&a)[2], int n) {
    s += '[';
    for (int i = 0; i < n; ++i) {
        if (i) s += ',';
        num(s, a[i].x); s += ','; num(s, a[i].y); s += ','; num(s, a[i].z);
    }
    s += ']';
}
void ivec2(std::string& s, const int (&a)[2][3], int n) {
    s += '[';
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) {
            if (i || k) s += ',';
            num(s, a[i][k]);
        }
    s += ']';
}

// Stabiele const char* voor de tekstvelden van OpticalSample (die wijzen normaal naar literals).
const char* intern(const std::string& s) {
    static std::mutex m;
    static std::unordered_set<std::string> pool;
    std::lock_guard<std::mutex> g(m);
    return pool.insert(s).first->c_str();
}

const json::Value* get(const json::Value& o, const char* k) { return o.find(k); }
bool getnum(const json::Value& o, const char* k, double& out) {
    const json::Value* v = o.find(k);
    if (!v || !v->is_number()) return false;
    out = v->num;
    return true;
}
bool getint(const json::Value& o, const char* k, int64_t& out) {
    double d;
    if (!getnum(o, k, d) || std::floor(d) != d) return false;
    out = static_cast<int64_t>(d);
    return true;
}
bool numarr(const json::Value* v, double* out, size_t n) {
    if (!v || !v->is_array() || v->arr.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!v->arr[i].is_number()) return false;
        out[i] = v->arr[i].num;
    }
    return true;
}

}  // namespace

bool valid_tag(const std::string& t) {
    if (t.empty() || t.size() > 40) return false;
    for (char c : t)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

std::string default_recordings_dir() {
    const char* st = std::getenv("XDG_STATE_HOME");
    if (st && st[0] == '/') return std::string(st) + "/ravengun/recordings";
    const char* home = std::getenv("HOME");
    if (!home || home[0] != '/') return {};
    return std::string(home) + "/.local/state/ravengun/recordings";
}

std::string format_header(const Header& h) {
    json::Writer w;
    w.begin_object();
    w.key("k").value("header");
    w.key("format").value(kFormatName);
    w.key("version").value(h.version);
    w.key("tag").value(h.tag);
    w.key("controller").value(h.controller);
    w.key("model").value(h.model);
    w.key("t0_ns").value(std::to_string(h.t0_ns));
    w.key("unix_s").value(static_cast<long long>(h.unix_s));
    w.key("led_enabled").value(h.led_enabled);
    w.key("led").begin_array().value(h.led[0]).value(h.led[1]).value(h.led[2]).end_array();
    w.key("move_ticks_per_s").value(h.move_ticks_per_s);
    w.key("dt_source").value(h.dt_source);
    w.key("content").value("alleen getallen: HID-rapporten, optische metingen (positie/straal), LED-wissels; geen beelden");
    w.end_object();
    return w.str();
}

std::string format_report(const core::RawReport& r, int64_t g_t0) {
    std::string s;
    s.reserve(420);
    s += "{\"k\":\"r\",\"t\":"; num(s, r.t_rx_ns - g_t0);
    s += ",\"q\":"; num(s, r.hid_seq);
    s += ",\"n\":"; num(s, r.subsamples);
    s += ",\"a\":"; vec2(s, r.accel, r.subsamples);
    s += ",\"g\":"; vec2(s, r.gyro, r.subsamples);
    s += ",\"m\":["; num(s, r.mag_raw[0]); s += ','; num(s, r.mag_raw[1]); s += ','; num(s, r.mag_raw[2]); s += ']';
    s += ",\"b\":"; num(s, static_cast<int64_t>(r.buttons));
    s += ",\"tr\":"; num(s, static_cast<int>(r.trigger));
    s += ",\"bat\":"; num(s, r.battery_raw);
    if (r.has_stamp) { s += ",\"s\":"; num(s, static_cast<int>(r.stamp)); }
    if (r.temperature_raw >= 0) { s += ",\"tc\":"; num(s, r.temperature_raw); }
    if (r.has_raw) {
        s += ",\"ar\":"; ivec2(s, r.accel_raw, 2);
        s += ",\"gr\":"; ivec2(s, r.gyro_raw, 2);
    }
    s += '}';
    return s;
}

std::string format_optical(const core::OpticalSample& o, int64_t g_t0) {
    std::string s;
    s.reserve(260);
    s += "{\"k\":\"o\",\"tf\":"; num(s, o.t_frame_ns ? o.t_frame_ns - g_t0 : 0);
    s += ",\"tp\":"; num(s, o.t_processed_ns ? o.t_processed_ns - g_t0 : 0);
    s += ",\"fs\":"; num(s, o.frame_seq);
    s += ",\"ca\":"; s += o.camera_active ? "true" : "false";
    s += ",\"cs\":" + json::quote(o.camera_state ? o.camera_state : "");
    s += ",\"v\":"; s += o.valid ? "true" : "false";
    s += ",\"re\":" + json::quote(o.reason ? o.reason : "");
    s += ",\"ts\":" + json::quote(o.track_state ? o.track_state : "");
    s += ",\"w\":"; num(s, o.width);
    s += ",\"h\":"; num(s, o.height);
    if (o.valid) {
        s += ",\"x\":"; num(s, o.x_px);
        s += ",\"y\":"; num(s, o.y_px);
        s += ",\"xn\":"; num(s, o.x_norm);
        s += ",\"yn\":"; num(s, o.y_norm);
        s += ",\"r\":"; num(s, o.radius_px);
    }
    s += ",\"c\":"; num(s, o.candidates);
    s += '}';
    return s;
}

std::string format_led(const LedEvent& e, int64_t g_t0) {
    std::string s = "{\"k\":\"led\",\"t\":";
    num(s, e.t_ns - g_t0);
    s += ",\"rgb\":["; num(s, e.rgb[0]); s += ','; num(s, e.rgb[1]); s += ','; num(s, e.rgb[2]); s += "]}";
    return s;
}

std::string format_mark(const Mark& m, int64_t g_t0) {
    std::string s = "{\"k\":\"mark\",\"t\":";
    num(s, m.t_ns - g_t0);
    s += ",\"label\":" + json::quote(m.label) + "}";
    return s;
}

std::string format_end(const End& e, int64_t g_t0) {
    std::string s = "{\"k\":\"end\",\"t\":";
    num(s, e.t_ns - g_t0);
    s += ",\"reason\":" + json::quote(e.reason);
    s += ",\"reports\":"; num(s, e.reports);
    s += ",\"optical\":"; num(s, e.optical);
    s += ",\"dropped\":"; num(s, e.dropped);
    s += '}';
    return s;
}

bool parse_line(const std::string& line, Record& out, std::string& err, int64_t& g_t0) {
    auto pr = json::parse(line);
    if (!pr.ok) { err = "ongeldige JSON: " + pr.error; return false; }
    const json::Value& o = pr.value;
    const json::Value* k = get(o, "k");
    if (!o.is_object() || !k || !k->is_string()) { err = "regel zonder 'k'"; return false; }
    const std::string& kind = k->str;
    int64_t t = 0;
    if (kind == "header") {
        out.kind = Kind::Header;
        Header& h = out.header;
        const json::Value* f = get(o, "format");
        if (!f || !f->is_string() || f->str != kFormatName) { err = "geen RavenGun-opname"; return false; }
        int64_t v = 0;
        if (!getint(o, "version", v)) { err = "versie ontbreekt"; return false; }
        if (v < 1 || v > kFormatVersion) { err = "opnameversie " + std::to_string(v) + " wordt niet ondersteund"; return false; }
        h.version = static_cast<int>(v);
        if (const auto* x = get(o, "tag"); x && x->is_string()) h.tag = x->str;
        if (const auto* x = get(o, "controller"); x && x->is_string()) h.controller = x->str;
        if (const auto* x = get(o, "model"); x && x->is_string()) h.model = x->str;
        if (const auto* x = get(o, "dt_source"); x && x->is_string()) h.dt_source = x->str;
        const json::Value* t0 = get(o, "t0_ns");
        if (!t0 || !t0->is_string()) { err = "t0_ns ontbreekt"; return false; }
        char* end = nullptr;
        h.t0_ns = std::strtoll(t0->str.c_str(), &end, 10);
        if (!end || *end) { err = "t0_ns ongeldig"; return false; }
        getint(o, "unix_s", h.unix_s);
        if (const auto* x = get(o, "led_enabled"); x && x->is_bool()) h.led_enabled = x->b;
        double led[3];
        if (numarr(get(o, "led"), led, 3)) for (int i = 0; i < 3; ++i) h.led[i] = static_cast<int>(led[i]);
        getnum(o, "move_ticks_per_s", h.move_ticks_per_s);
        g_t0 = h.t0_ns;
        return true;
    }
    if (kind == "r") {
        out.kind = Kind::Report;
        core::RawReport r;
        int64_t q = -1, n = 2, b = 0, tr = 0, bat = -1;
        if (!getint(o, "t", t) || !getint(o, "q", q) || !getint(o, "n", n) || n < 1 || n > 2) { err = "rapport onvolledig"; return false; }
        r.t_rx_ns = g_t0 + t;
        r.hid_seq = static_cast<int>(q);
        r.subsamples = static_cast<int>(n);
        double a[6], g[6], m[3];
        size_t cnt = static_cast<size_t>(n) * 3;
        if (!numarr(get(o, "a"), a, cnt) || !numarr(get(o, "g"), g, cnt) || !numarr(get(o, "m"), m, 3)) { err = "rapport: a/g/m ongeldig"; return false; }
        for (int i = 0; i < n; ++i) {
            r.accel[i] = {a[i * 3], a[i * 3 + 1], a[i * 3 + 2]};
            r.gyro[i] = {g[i * 3], g[i * 3 + 1], g[i * 3 + 2]};
        }
        for (int i = 0; i < 3; ++i) r.mag_raw[i] = static_cast<int>(m[i]);
        getint(o, "b", b); getint(o, "tr", tr); getint(o, "bat", bat);
        r.buttons = static_cast<uint32_t>(b);
        r.trigger = static_cast<uint8_t>(tr);
        r.battery_raw = static_cast<int>(bat);
        int64_t st = 0, tc = 0;
        if (getint(o, "s", st)) { r.has_stamp = true; r.stamp = static_cast<uint16_t>(st); }
        if (getint(o, "tc", tc)) r.temperature_raw = static_cast<int>(tc);
        double ar[6], gr[6];
        if (numarr(get(o, "ar"), ar, 6) && numarr(get(o, "gr"), gr, 6)) {
            r.has_raw = true;
            for (int i = 0; i < 6; ++i) {
                r.accel_raw[i / 3][i % 3] = static_cast<int>(ar[i]);
                r.gyro_raw[i / 3][i % 3] = static_cast<int>(gr[i]);
            }
        }
        out.report = r;
        return true;
    }
    if (kind == "o") {
        out.kind = Kind::Optical;
        core::OpticalSample s;
        int64_t tf = 0, tp = 0, fs = 0, w = 0, h = 0, c = 0;
        getint(o, "tf", tf); getint(o, "tp", tp); getint(o, "fs", fs); getint(o, "w", w); getint(o, "h", h); getint(o, "c", c);
        s.t_frame_ns = tf ? g_t0 + tf : 0;
        s.t_processed_ns = tp ? g_t0 + tp : 0;
        s.frame_seq = static_cast<uint64_t>(fs);
        s.width = static_cast<int>(w);
        s.height = static_cast<int>(h);
        s.candidates = static_cast<int>(c);
        if (const auto* x = get(o, "ca"); x && x->is_bool()) s.camera_active = x->b;
        if (const auto* x = get(o, "v"); x && x->is_bool()) s.valid = x->b;
        if (const auto* x = get(o, "cs"); x && x->is_string()) s.camera_state = intern(x->str);
        if (const auto* x = get(o, "re"); x && x->is_string()) s.reason = intern(x->str);
        if (const auto* x = get(o, "ts"); x && x->is_string()) s.track_state = intern(x->str);
        getnum(o, "x", s.x_px); getnum(o, "y", s.y_px); getnum(o, "xn", s.x_norm); getnum(o, "yn", s.y_norm);
        getnum(o, "r", s.radius_px);
        out.optical = s;
        return true;
    }
    if (kind == "led") {
        out.kind = Kind::Led;
        double rgb[3];
        if (!getint(o, "t", t) || !numarr(get(o, "rgb"), rgb, 3)) { err = "led onvolledig"; return false; }
        out.led.t_ns = g_t0 + t;
        for (int i = 0; i < 3; ++i) out.led.rgb[i] = static_cast<int>(rgb[i]);
        return true;
    }
    if (kind == "mark") {
        out.kind = Kind::Mark;
        const json::Value* l = get(o, "label");
        if (!getint(o, "t", t) || !l || !l->is_string()) { err = "mark onvolledig"; return false; }
        out.mark.t_ns = g_t0 + t;
        out.mark.label = l->str;
        return true;
    }
    if (kind == "end") {
        out.kind = Kind::End;
        getint(o, "t", t);
        out.end.t_ns = g_t0 + t;
        if (const auto* x = get(o, "reason"); x && x->is_string()) out.end.reason = x->str;
        int64_t v = 0;
        if (getint(o, "reports", v)) out.end.reports = static_cast<uint64_t>(v);
        if (getint(o, "optical", v)) out.end.optical = static_cast<uint64_t>(v);
        if (getint(o, "dropped", v)) out.end.dropped = static_cast<uint64_t>(v);
        return true;
    }
    err = "onbekend soort regel '" + kind + "'";
    return false;
}

Reader::~Reader() { if (f_) std::fclose(f_); }

bool Reader::open(const std::string& path, std::string& err) {
    f_ = std::fopen(path.c_str(), "re");
    if (!f_) { err = "kan " + path + " niet openen"; return false; }
    auto first = next();
    if (!first || first->kind != Kind::Header) {
        err = err_.empty() ? "eerste regel is geen header" : err_;
        return false;
    }
    header_ = first->header;
    return true;
}

std::optional<Record> Reader::next() {
    if (!f_) return std::nullopt;
    std::string line;
    int c;
    while (true) {
        line.clear();
        while ((c = std::fgetc(f_)) != EOF && c != '\n') line += static_cast<char>(c);
        if (line.empty() && c == EOF) return std::nullopt;
        ++line_;
        if (line.empty()) continue;
        break;
    }
    Record r;
    std::string e;
    if (!parse_line(line, r, e, t0_)) {
        err_ = "regel " + std::to_string(line_) + ": " + e;
        return std::nullopt;
    }
    return r;
}

}  // namespace ravengun::rec
