// ravengunctl: gedeelde hulpfuncties (JSON-toegang, verzoek aan de daemon, opmaak).
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <time.h>
#include <vector>

#include "ravengun/core/mag_yaw.hpp"
#include "ravengun/doctor.hpp"
#include "ravengun/ipc_client.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"
#include "ravengun/vision/ball_tracker.hpp"
#include "ctl_common.hpp"

using namespace ravengun;

namespace ravengunctl {

std::atomic<bool> g_stop{false};
// --player N: de speler voor per-controller-opdrachten (status, watch, filter, rumble, led, ...); 0 = niet gegeven (speler 1).
int g_player = 0;
void on_signal(int) { g_stop.store(true); }

const json::Value* path(const json::Value& v, std::initializer_list<const char*> keys) {
    const json::Value* cur = &v;
    for (const char* k : keys) {
        cur = cur->find(k);
        if (!cur) return nullptr;
    }
    return cur;
}
double num(const json::Value& v, std::initializer_list<const char*> keys, double def) {
    const json::Value* x = path(v, keys);
    return x && x->is_number() ? x->num : def;
}
std::string str(const json::Value& v, std::initializer_list<const char*> keys, const char* def) {
    const json::Value* x = path(v, keys);
    return x && x->is_string() ? x->str : def;
}
bool boolean(const json::Value& v, std::initializer_list<const char*> keys) {
    const json::Value* x = path(v, keys);
    return x && x->is_bool() && x->b;
}

// "ja [80 00 00 00 00]" / "nee" / "onbekend" voor de uitbreidingsconnector (Sharp Shooter) van een sample.
std::string ext_text(const json::Value& s) {
    const json::Value* x = path(s, {"ext"});
    if (!x || !x->is_object()) return "onbekend";
    if (!boolean(*x, {"known"})) return "onbekend";
    if (!boolean(*x, {"connected"})) return "nee";
    std::string t = "ja [";
    if (const json::Value* b = path(*x, {"bytes"}); b && b->is_array()) {
        char h[8];
        for (size_t i = 0; i < b->arr.size(); ++i) {
            std::snprintf(h, sizeof h, "%s%02X", i ? " " : "", static_cast<unsigned>(b->arr[i].num) & 0xFF);
            t += h;
        }
    }
    return t + "]";
}
std::string names(const json::Value* arr) {
    std::string r;
    if (!arr || !arr->is_array()) return r;
    for (const auto& e : arr->arr) {
        if (!r.empty()) r += ',';
        r += e.str;
    }
    return r.empty() ? "-" : r;
}
std::string edge_names(const json::Value* obj) {
    std::string r;
    if (!obj || !obj->is_object()) return r;
    for (const auto& kv : obj->obj) {
        if (!r.empty()) r += ',';
        r += kv.first;
        if (kv.second.num > 1) r += "x" + std::to_string(static_cast<long long>(kv.second.num));
    }
    return r;
}

Reply call(ipc::Client& c, const ipc::Request& rq_in, int read_timeout_ms) {
    Reply r;
    ipc::Request rq = rq_in;
    if (g_player > 0 && ipc::takes_selector(rq.cmd) && !rq.player && rq.controller.empty()) rq.player = g_player;
    std::string sock = ipc::socket_path();
    if (sock.empty()) { std::fprintf(stderr, "ravengunctl: XDG_RUNTIME_DIR is niet gezet\n"); r.code = kExitDaemonUnavailable; return r; }
    if (!c.connect(sock)) {
        std::fprintf(stderr, "ravengunctl: ravengund niet bereikbaar op %s (%s). Start eerst ravengund.\n", sock.c_str(),
                     c.error().c_str());
        r.code = kExitDaemonUnavailable;
        return r;
    }
    if (!c.send_line(ipc::make_request(rq))) { std::fprintf(stderr, "ravengunctl: verzenden mislukt: %s\n", c.error().c_str()); r.code = kExitDaemonUnavailable; return r; }
    auto line = c.read_line(read_timeout_ms);
    if (!line) { std::fprintf(stderr, "ravengunctl: geen antwoord: %s\n", c.error().c_str()); r.code = kExitProtocol; return r; }
    r.line = *line;
    auto pr = json::parse(*line);
    if (!pr.ok || !pr.value.is_object()) { std::fprintf(stderr, "ravengunctl: ongeldig antwoord\n"); r.code = kExitProtocol; return r; }
    r.v = std::move(pr.value);
    if (!boolean(r.v, {"ok"})) {
        std::string code = str(r.v, {"error", "code"});
        std::fprintf(stderr, "ravengunctl: daemon meldt %s: %s\n", code.c_str(), str(r.v, {"error", "message"}).c_str());
        r.code = code == "unavailable" ? kExitTempFail : kExitProtocol;
    }
    return r;
}

std::string fmt(double v, const char* f) {
    if (std::isnan(v)) return "-";
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

double now_s() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

Res simple(const ipc::Request& rq, int read_timeout_ms) {
    ipc::Client c;
    Res x;
    x.r = call(c, rq, read_timeout_ms);
    x.code = x.r.code;
    return x;
}

}  // namespace ravengunctl
