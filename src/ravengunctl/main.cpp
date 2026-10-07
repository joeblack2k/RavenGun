// ravengunctl: diagnose (alleen-lezen) en beheer van ravengund via de Unix-socket.
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <time.h>

#include "ravengun/doctor.hpp"
#include "ravengun/ipc_client.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"

namespace {

using namespace ravengun;

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

void usage(FILE* f) {
    std::fputs(
        "gebruik: ravengunctl <commando> [opties]\n"
        "\n"
        "commando's (alleen-lezen, zonder daemon):\n"
        "  doctor      controleert camera, Move, Bluetooth, uinput en PSMoveAPI en geeft per punt een herstelactie\n"
        "  devices     toont de gevonden camera's, Sony HID-apparaten, Bluetooth-adapters en uinput\n"
        "\n"
        "commando's (via ravengund):\n"
        "  status      verbinding, batterij, oriëntatie, aim, geldigheid, rapportfrequentie en gaten\n"
        "  recenter    legt de huidige richting van de Move vast als midden van het scherm\n"
        "  watch       toont live aim en knoppen (rate-limited) tot Ctrl-C of --duration\n"
        "  filter      zet het 1€-filter: --on/--off, --min-cutoff HZ, --beta B\n"
        "\n"
        "opties:\n"
        "  --json             machineleesbare uitvoer (JSON op stdout; bij watch één regel per frame)\n"
        "  --full-addresses   toon volledige Bluetooth-adressen (standaard geredigeerd)\n"
        "  --rate HZ          watch: max. frames per seconde (1..250, standaard 10; --json: 30)\n"
        "  --duration S       watch: stop na S seconden\n"
        "  -h, --help         deze tekst\n"
        "\n"
        "exitcodes:\n"
        "  0   doctor: alles in orde; overige: gelukt\n"
        "  1   doctor: minstens een waarschuwing, geen fouten\n"
        "  2   doctor: minstens een fout\n"
        "  64  verkeerd gebruik (onbekend commando of optie)\n"
        "  69  ravengund niet bereikbaar (draait de daemon?)\n"
        "  70  interne fout\n"
        "  75  daemon weigert tijdelijk (bijv. recenter zonder verbonden Move)\n"
        "  76  onverwacht antwoord van de daemon\n"
        "\n"
        "doctor en devices wijzigen niets op het systeem.\n", f);
}

const json::Value* path(const json::Value& v, std::initializer_list<const char*> keys) {
    const json::Value* cur = &v;
    for (const char* k : keys) {
        cur = cur->find(k);
        if (!cur) return nullptr;
    }
    return cur;
}
double num(const json::Value& v, std::initializer_list<const char*> keys, double def = NAN) {
    const json::Value* x = path(v, keys);
    return x && x->is_number() ? x->num : def;
}
std::string str(const json::Value& v, std::initializer_list<const char*> keys, const char* def = "?") {
    const json::Value* x = path(v, keys);
    return x && x->is_string() ? x->str : def;
}
bool boolean(const json::Value& v, std::initializer_list<const char*> keys) {
    const json::Value* x = path(v, keys);
    return x && x->is_bool() && x->b;
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

struct Reply {
    int code = kExitOk;
    std::string line;
    json::Value v;
};

// Eén verzoek, één antwoord.
Reply call(ipc::Client& c, const ipc::Request& rq) {
    Reply r;
    std::string sock = ipc::socket_path();
    if (sock.empty()) { std::fprintf(stderr, "ravengunctl: XDG_RUNTIME_DIR is niet gezet\n"); r.code = kExitDaemonUnavailable; return r; }
    if (!c.connect(sock)) {
        std::fprintf(stderr, "ravengunctl: ravengund niet bereikbaar op %s (%s). Start eerst ravengund.\n", sock.c_str(),
                     c.error().c_str());
        r.code = kExitDaemonUnavailable;
        return r;
    }
    if (!c.send_line(ipc::make_request(rq))) { std::fprintf(stderr, "ravengunctl: verzenden mislukt: %s\n", c.error().c_str()); r.code = kExitDaemonUnavailable; return r; }
    auto line = c.read_line(3000);
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

void print_sample_line(const json::Value& s, const json::Value* edges) {
    bool av = boolean(s, {"aim", "valid"});
    std::string aim = av ? "x " + fmt(num(s, {"aim", "x"}), "%6.3f") + " y " + fmt(num(s, {"aim", "y"}), "%6.3f") +
                               (boolean(s, {"aim", "off_screen"}) ? " BUITEN" : "       ")
                         : std::string("aim ongeldig            ");
    std::string e;
    if (edges) {
        std::string p = edge_names(path(*edges, {"pressed"})), r = edge_names(path(*edges, {"released"}));
        if (!p.empty()) e += " +" + p;
        if (!r.empty()) e += " -" + r;
    }
    std::printf("%-8s %s  yaw %7s pitch %7s  trig %3d  knoppen %s%s\n", str(s, {"link"}).c_str(), aim.c_str(),
                fmt(num(s, {"aim", "yaw_rel_deg"}), "%+.2f").c_str(), fmt(num(s, {"aim", "pitch_rel_deg"}), "%+.2f").c_str(),
                static_cast<int>(num(s, {"trigger"}, 0)), names(path(s, {"buttons", "down"})).c_str(), e.c_str());
    std::fflush(stdout);
}

void print_status(const json::Value& r) {
    const json::Value& s = *path(r, {"sample"});
    bool con = boolean(r, {"controller", "connected"});
    std::printf("Daemon:       pid %.0f, actief %.0f s%s\n", num(r, {"daemon", "pid"}), num(r, {"daemon", "uptime_s"}),
                boolean(r, {"daemon", "hid_enabled"}) ? "" : " (HID uit: --no-hid)");
    if (con) {
        std::printf("Move:         verbonden (%s), %s, id %s, kalibratie %s\n", str(r, {"controller", "connection"}).c_str(),
                    str(r, {"controller", "model"}).c_str(), str(r, {"controller", "id"}).c_str(),
                    boolean(r, {"controller", "has_calibration"}) ? "ja" : "NEE");
    } else {
        std::printf("Move:         niet verbonden (druk op de PS-knop)\n");
    }
    std::printf("Batterij:     %s\n", str(r, {"controller", "battery", "text"}).c_str());
    std::printf("Verbinding:   %s\n", str(s, {"link"}).c_str());
    std::printf("Rapporten:    %.0f totaal, %s/s (5 s), %s/s sinds verbinden; 2 IMU-subsamples per rapport\n",
                num(r, {"reports", "total"}), fmt(num(r, {"reports", "rate_hz_5s"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "rate_hz_since_connect"}), "%.1f").c_str());
    std::printf("Intervallen:  p50 %s ms, p95 %s ms, p99 %s ms, max %s ms; gemist volgens seq %.0f\n",
                fmt(num(r, {"reports", "interval_ms", "p50"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "p95"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "p99"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "max"}), "%.1f").c_str(), num(r, {"reports", "dropped_by_seq"}));
    std::printf("Gaten:       ");
    if (const json::Value* h = path(r, {"reports", "interval_histogram"}); h && h->is_array()) {
        for (const auto& b : h->arr) {
            double c = num(b, {"count"}, 0);
            if (c <= 0) continue;
            double to = num(b, {"to_ms"});
            if (std::isnan(to)) std::printf(" >=%.0f ms: %.0f;", num(b, {"from_ms"}), c);
            else std::printf(" %.0f-%.0f ms: %.0f;", num(b, {"from_ms"}), to, c);
        }
    }
    std::printf("\n");
    std::printf("Verwerking:   p95 %s us per rapport\n", fmt(num(r, {"processing_us", "p95"}), "%.0f").c_str());
    std::printf("Oriëntatie:   %s, bias %s%s, yaw-referentie %s, wereld-yaw %s graden, pitch %s graden\n",
                boolean(s, {"orientation", "valid"}) ? "geldig" : "ONGELDIG", str(s, {"orientation", "bias_state"}).c_str(),
                boolean(s, {"orientation", "still"}) ? " (stil)" : "", str(s, {"orientation", "yaw_reference"}).c_str(),
                fmt(num(s, {"orientation", "world_yaw_deg"}), "%.2f").c_str(),
                fmt(num(s, {"orientation", "world_pitch_deg"}), "%.2f").c_str());
    std::printf("Positie:      ongeldig (nog geen camera)\n");
    if (boolean(s, {"aim", "valid"})) {
        std::printf("Aim:          x %.3f y %.3f (ongefilterd %.3f %.3f)%s, yaw %+.2f pitch %+.2f graden, midden: %s\n",
                    num(s, {"aim", "x"}), num(s, {"aim", "y"}), num(s, {"aim", "x_raw"}), num(s, {"aim", "y_raw"}),
                    boolean(s, {"aim", "off_screen"}) ? " BUITEN SCHERM" : "", num(s, {"aim", "yaw_rel_deg"}),
                    num(s, {"aim", "pitch_rel_deg"}), str(s, {"aim", "center_source"}).c_str());
    } else {
        std::printf("Aim:          ONGELDIG (midden: %s)\n", str(s, {"aim", "center_source"}).c_str());
    }
    std::printf("Aim-mapping:  oriëntatiemodus (voorlopig), rand bij %.1f graden horizontaal / %.2f verticaal; filter %s\n",
                num(r, {"config", "aim", "h_edge_deg"}), num(r, {"config", "aim", "v_edge_deg"}),
                boolean(r, {"config", "filter", "enabled"})
                    ? ("1€ (min_cutoff " + fmt(num(r, {"config", "filter", "min_cutoff_hz"}), "%.2f") + " Hz, beta " +
                       fmt(num(r, {"config", "filter", "beta"}), "%.2f") + ")").c_str()
                    : "uit");
    std::printf("Knoppen:      %s, trigger %.0f\n", names(path(s, {"buttons", "down"})).c_str(), num(s, {"trigger"}, 0));
}

double now_s() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ravengun;
    try {
        std::string cmd;
        bool json = false, full = false;
        double rate = -1, duration = -1, min_cutoff = -1, beta = -1;
        int filter_on = -1;
        auto need = [&](int& i, const char* name, double lo, double hi) -> double {
            if (i + 1 >= argc) { std::fprintf(stderr, "ravengunctl: %s mist een waarde\n", name); std::exit(kExitUsage); }
            char* end = nullptr;
            double v = std::strtod(argv[++i], &end);
            if (!end || *end || !(v >= lo && v <= hi)) { std::fprintf(stderr, "ravengunctl: ongeldige waarde voor %s\n", name); std::exit(kExitUsage); }
            return v;
        };
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") { usage(stdout); return kExitOk; }
            else if (a == "--json") json = true;
            else if (a == "--full-addresses") full = true;
            else if (a == "--rate") rate = need(i, "--rate", ipc::kWatchRateMin, ipc::kWatchRateMax);
            else if (a == "--duration") duration = need(i, "--duration", 0.1, 1e7);
            else if (a == "--min-cutoff") min_cutoff = need(i, "--min-cutoff", 0.001, 100);
            else if (a == "--beta") beta = need(i, "--beta", 0, 1000);
            else if (a == "--on") filter_on = 1;
            else if (a == "--off") filter_on = 0;
            else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "ravengunctl: onbekende optie '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
            else if (cmd.empty()) cmd = a;
            else { std::fprintf(stderr, "ravengunctl: onverwacht argument '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
        }
        if (cmd.empty()) { usage(stderr); return kExitUsage; }

        if (cmd == "doctor" || cmd == "devices") {
            Snapshot snap = probe(ProbeOptions{});
            ReportOptions ro;
            ro.full_addresses = full;
            if (cmd == "doctor") {
                auto checks = evaluate(snap);
                int code = exit_code_for(checks);
                std::fputs((json ? doctor_json(checks, code) : doctor_human(checks)).c_str(), stdout);
                return code;
            }
            std::fputs((json ? devices_json(snap, ro) : devices_human(snap, ro)).c_str(), stdout);
            return kExitOk;
        }

        ipc::Client c;
        ipc::Request rq;
        rq.full_ids = full;
        if (cmd == "status") {
            rq.cmd = ipc::Cmd::Status;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"}); res && path(*res, {"sample"})) print_status(*res);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "recenter") {
            rq.cmd = ipc::Cmd::Recenter;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Midden vastgelegd op de huidige richting van de Move.\n");
            return kExitOk;
        }
        if (cmd == "filter") {
            if (filter_on < 0 && min_cutoff < 0 && beta < 0) { std::fprintf(stderr, "ravengunctl: filter vereist --on, --off, --min-cutoff of --beta\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::SetFilter;
            if (filter_on >= 0) rq.filter_enabled = filter_on == 1;
            if (min_cutoff >= 0) rq.min_cutoff_hz = min_cutoff;
            if (beta >= 0) rq.beta = beta;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            std::printf("%s\n", json ? r.line.c_str() : "Filterinstelling toegepast (alleen voor deze daemon-run).");
            return kExitOk;
        }
        if (cmd == "watch") {
            rq.cmd = ipc::Cmd::Watch;
            rq.rate_hz = rate > 0 ? rate : (json ? 30 : 10);
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
            double t0 = now_s();
            while (!g_stop.load()) {
                if (duration > 0 && now_s() - t0 >= duration) break;
                auto line = c.read_line(200);
                if (!line) {
                    if (c.error() == "time-out" || c.error() == "onderbroken") continue;
                    std::fprintf(stderr, "ravengunctl: %s\n", c.error().c_str());
                    return kExitDaemonUnavailable;
                }
                if (json) { std::printf("%s\n", line->c_str()); std::fflush(stdout); continue; }
                auto pr = json::parse(*line);
                if (!pr.ok) continue;
                const json::Value* d = path(pr.value, {"data"});
                if (!d) continue;
                const json::Value* s = path(*d, {"sample"});
                if (s) print_sample_line(*s, path(*d, {"edges_since_last_frame"}));
            }
            return kExitOk;
        }
        std::fprintf(stderr, "ravengunctl: onbekend commando '%s'\n\n", cmd.c_str());
        usage(stderr);
        return kExitUsage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ravengunctl: interne fout: %s\n", e.what());
        return kExitInternal;
    }
}
