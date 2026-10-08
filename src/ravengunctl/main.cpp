// ravengunctl: diagnose (alleen-lezen) en beheer van ravengund via de Unix-socket.
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <time.h>
#include <vector>

#include "ravengun/doctor.hpp"
#include "ravengun/ipc_client.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"
#include "ravengun/vision/ball_tracker.hpp"

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
        "  camera      camera-identiteit, modus, gemeten fps en intervallen, controls en trackerstatistiek\n"
        "  record      opname (alleen getallen) naar ~/.local/state/ravengun/recordings/:\n"
        "                record [--tag T] [--duration S] [--max-mb N]   opnemen tot S s of Ctrl-C\n"
        "                record --start|--stop|--status, record --mark LABEL\n"
        "  led         LED-kleur: led cyaan|rood|groen|blauw|wit|magenta|geel|uit of led R,G,B\n"
        "  fusion      oriëntatieschatter: fusion mahony (IMU-only) of fusion mahony_mag (met magnetometer-yaw);\n"
        "              wordt bewaard in config.json\n"
        "  calibrate   calibrate mag [--holder H] [--duration S]: magnetometer kalibreren (ronddraaien;\n"
        "              stopt zelf bij >= 80 % dekking en >= 300 samples), of --start|--stop|--abort|--status;\n"
        "              calibrate mag --check [--duration 3]: veldcheck (norm en dip t.o.v. de kalibratie)\n"
        "  calibrate-ui  opent het fullscreen kalibratiescherm (stappenwizard, bediening met de controller) in een\n"
        "              kiosk-Brave op de tv; --close sluit alleen dat kioskproces, --status toont of het open is\n"
        "\n"
        "uitvoer (virtueel lightgun-apparaat; standaard DISARMED en dan gaan er geen events heen):\n"
        "  arm         schakelt de uitvoer in en maakt het apparaat aan (RavenGun Virtual Lightgun, 1209:7202)\n"
        "  disarm      stopt een patroon, geeft alle knoppen vrij en schakelt de uitvoer uit\n"
        "  output      status van de uitvoer (armed, apparaat, positie, knoppen, transformatie, patroon)\n"
        "  test-pattern center|corners|grid|sweep|stop   synthetische posities zonder Move (vereist arm)\n"
        "  test-pattern point X Y                         één punt, X en Y in [0,1] (linksboven 0 0)\n"
        "  output-button trigger|reload|grenade|aux1|aux2 [down|up|tap]   testknop (standaard tap)\n"
        "  set-output  transformatie van de uitvoer: --invert-x 0|1, --invert-y 0|1, --scale-x/-y S,\n"
        "              --offset-x/-y D, --letterbox SCHERM_ASPECT CONTENT_ASPECT (bv. 1.7778 1.3333)\n"
        "\n"
        "opties:\n"
        "  --json             machineleesbare uitvoer (JSON op stdout; bij watch één regel per frame)\n"
        "  --full-addresses   toon volledige Bluetooth-adressen (standaard geredigeerd)\n"
        "  --rate HZ          watch: max. frames per seconde (1..250, standaard 10; --json: 30)\n"
        "  --duration S       watch/record/calibrate: stop na S seconden; test-pattern sweep: duur (standaard 12)\n"
        "  --dwell MS         test-pattern corners/grid: tijd per punt (standaard 2000)\n"
        "  --margin M         test-pattern: marge vanaf de rand (corners 0, grid 0,1, sweep 0,05)\n"
        "  --tag T            record: label in de bestandsnaam (a-z, 0-9, _ en -)\n"
        "  --max-mb N         record: maximale bestandsgrootte (standaard 64)\n"
        "  --holder H         calibrate mag: houder, bijv. sharp_shooter of los\n"
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

struct Reply {
    int code = kExitOk;
    std::string line;
    json::Value v;
};

// Eén verzoek, één antwoord.
Reply call(ipc::Client& c, const ipc::Request& rq, int read_timeout_ms = 3000) {
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

// Optische meting als korte tekst (2D-beeldpositie, geen diepte).
std::string optical_text(const json::Value& s) {
    const json::Value* p = path(s, {"position"});
    if (!p) return "-";
    if (boolean(*p, {"valid"}))
        return "bol op (" + fmt(num(*p, {"x_px"}), "%.1f") + ", " + fmt(num(*p, {"y_px"}), "%.1f") + ") px, genormaliseerd (" +
               fmt(num(*p, {"x_norm"}), "%.3f") + ", " + fmt(num(*p, {"y_norm"}), "%.3f") + "), straal " +
               fmt(num(*p, {"radius_px"}), "%.1f") + " px (2D-beeldpositie, diepte onbekend)";
    return "ongeldig: " + str(*p, {"reason"}) + " (camera " + str(*p, {"camera"}) + ")";
}

void print_camera(const json::Value& c) {
    std::printf("Toestand:     %s%s%s\n", str(c, {"state"}).c_str(), path(c, {"error"}) && path(c, {"error"})->is_string() ? ": " : "",
                path(c, {"error"}) && path(c, {"error"})->is_string() ? str(c, {"error"}).c_str() : "");
    std::printf("Apparaat:     %s\n              by-path %s -> %s (driver %s, %s)\n", str(c, {"device", "by_id"}).c_str(),
                str(c, {"device", "by_path"}).c_str(), str(c, {"device", "node"}).c_str(), str(c, {"device", "driver"}).c_str(),
                str(c, {"device", "bus_info"}).c_str());
    std::printf("Modus:        %s %.0fx%.0f, gevraagd %.0f fps, driver %s fps; timestamps %s/%s\n", str(c, {"mode", "format"}).c_str(),
                num(c, {"mode", "width"}, 0), num(c, {"mode", "height"}, 0), num(c, {"mode", "requested_fps"}, 0),
                fmt(num(c, {"mode", "driver_fps"}), "%.2f").c_str(), str(c, {"timestamps", "type"}, "-").c_str(),
                str(c, {"timestamps", "source"}, "-").c_str());
    std::printf("Frames:       %.0f ontvangen, gemeten %s fps (totaal) / %s fps (5 s), gemist (sequence) %.0f, fout %.0f, kort %.0f\n",
                num(c, {"frames", "received"}, 0), fmt(num(c, {"frames", "fps_total"}), "%.2f").c_str(),
                fmt(num(c, {"frames", "fps_5s"}), "%.2f").c_str(), num(c, {"frames", "missed_by_sequence"}, 0),
                num(c, {"frames", "error_frames"}, 0), num(c, {"frames", "short_frames"}, 0));
    auto ring = [&](const char* label, const char* k, const char* unit) {
        std::printf("%-14sp50 %s, p95 %s, p99 %s, max %s %s\n", label, fmt(num(c, {k, "p50"}), "%.3f").c_str(),
                    fmt(num(c, {k, "p95"}), "%.3f").c_str(), fmt(num(c, {k, "p99"}), "%.3f").c_str(), fmt(num(c, {k, "max"}), "%.3f").c_str(), unit);
    };
    ring("Interval:", "interval_ms", "ms");
    ring("Leeftijd:", "age_at_processing_ms", "ms (start verwerking - frametimestamp)");
    ring("Verwerking:", "processing_us", "us per frame");
    std::printf("Wachtrij:     capaciteit %.0f, weggegooid vol %.0f / verouderd %.0f\n", num(c, {"queue", "capacity"}, 0),
                num(c, {"queue", "dropped_full"}, 0), num(c, {"queue", "dropped_stale"}, 0));
    std::printf("Controls:    ");
    if (const json::Value* a = path(c, {"controls"}); a && a->is_array())
        for (const auto& e : a->arr)
            std::printf(" %s=%s%s;", str(e, {"name"}).c_str(), fmt(num(e, {"value"}), "%.0f").c_str(),
                        path(e, {"requested"}) && path(e, {"requested"})->is_string() ? "*" : "");
    std::printf("\n              (* = door ravengund gezet)\n");
    std::printf("Tracker:      %.0f verwerkt, %.0f geldig; ROI %.0f, volledig beeld %.0f; straal p50 %s px\n", num(c, {"tracker", "processed"}, 0),
                num(c, {"tracker", "valid"}, 0), num(c, {"tracker", "roi_hits"}, 0), num(c, {"tracker", "full_frame_searches"}, 0),
                fmt(num(c, {"tracker", "radius_px", "p50"}), "%.1f").c_str());
    std::printf("Redenen:     ");
    if (const json::Value* o = path(c, {"tracker", "reasons"}); o && o->is_object())
        for (const auto& kv : o->obj) std::printf(" %s %.0f;", kv.first.c_str(), kv.second.num);
    std::printf("\n");
    std::printf("Laatste:      %s (%s), kandidaten %.0f, componenten %.0f\n", boolean(c, {"tracker", "last", "valid"}) ? "geldig" : "ongeldig",
                str(c, {"tracker", "last", "reason"}).c_str(), num(c, {"tracker", "last", "candidates"}, 0),
                num(c, {"tracker", "last", "components"}, 0));
    if (boolean(c, {"scene", "available"})) {
        auto at = [&](std::initializer_list<const char*> k, size_t i) {
            const json::Value* a = path(c, k);
            return a && a->is_array() && a->arr.size() > i && a->arr[i].is_number() ? a->arr[i].num : NAN;
        };
        std::printf("Scène:        Y gem %s p50 %s p99 %s, verzadigd %s %%, RGB gem %s/%s/%s, kleurmatch %s %%\n",
                    fmt(at({"scene", "mean_yuv"}, 0), "%.0f").c_str(), fmt(num(c, {"scene", "y_p50"}), "%.0f").c_str(),
                    fmt(num(c, {"scene", "y_p99"}), "%.0f").c_str(), fmt(num(c, {"scene", "saturated_frac"}) * 100, "%.2f").c_str(),
                    fmt(at({"scene", "mean_rgb"}, 0), "%.0f").c_str(), fmt(at({"scene", "mean_rgb"}, 1), "%.0f").c_str(),
                    fmt(at({"scene", "mean_rgb"}, 2), "%.0f").c_str(), fmt(num(c, {"scene", "color_match_frac"}) * 100, "%.3f").c_str());
    }
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
    const json::Value* p = path(s, {"position"});
    std::string opt = p && boolean(*p, {"valid"})
                          ? "bol " + fmt(num(*p, {"x_px"}), "%6.1f") + "," + fmt(num(*p, {"y_px"}), "%6.1f") + " r" + fmt(num(*p, {"radius_px"}), "%4.1f")
                          : "bol " + (p ? str(*p, {"reason"}) : std::string("-"));
    std::string xt = ext_text(s);
    std::printf("%-8s %s  yaw %7s pitch %7s  trig %3d  %-24s ext %s  knoppen %s%s\n", str(s, {"link"}).c_str(), aim.c_str(),
                fmt(num(s, {"aim", "yaw_rel_deg"}), "%+.2f").c_str(), fmt(num(s, {"aim", "pitch_rel_deg"}), "%+.2f").c_str(),
                static_cast<int>(num(s, {"trigger"}, 0)), opt.c_str(), xt.c_str(), names(path(s, {"buttons", "down"})).c_str(), e.c_str());
    std::fflush(stdout);
}

void print_record_status(const json::Value& v);

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
    std::printf("Positie:      %s\n", optical_text(s).c_str());
    if (const json::Value* cam = path(r, {"camera"}); cam && cam->is_object())
        std::printf("Camera:       %s, %s/s (5 s), gemist %.0f, bol geldig %.0f van %.0f frames\n", str(*cam, {"state"}).c_str(),
                    fmt(num(*cam, {"frames", "fps_5s"}), "%.2f").c_str(), num(*cam, {"frames", "missed_by_sequence"}, 0),
                    num(*cam, {"tracker", "valid"}, 0), num(*cam, {"tracker", "processed"}, 0));
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
    std::printf("Uitbreiding:  ext verbonden: %s\n", ext_text(s).c_str());
    if (const json::Value* tb = path(r, {"timebase"})) {
        std::printf("Tijdbasis:    %s; %.0f stempels, %s, skew %s ppm, restvertraging p50 %s / p95 %s / max %s ms; temperatuur %s (ruw %.0f)\n",
                    str(*tb, {"integration_dt"}).c_str(), num(*tb, {"stamps"}, 0), boolean(*tb, {"coupled"}) ? "gekoppeld" : "nog niet gekoppeld",
                    fmt(num(*tb, {"skew_ppm"}), "%.1f").c_str(), fmt(num(*tb, {"residual_ms", "p50"}), "%.2f").c_str(),
                    fmt(num(*tb, {"residual_ms", "p95"}), "%.2f").c_str(), fmt(num(*tb, {"residual_ms", "max"}), "%.2f").c_str(),
                    fmt(num(*tb, {"temperature_c"}), "%.1f C").c_str(), num(*tb, {"temperature_raw"}, -1));
    }
    if (const json::Value* h = path(r, {"hidraw"}))
        std::printf("hidraw-tap:   %s %s, gekoppeld %.0f, niet gekoppeld %.0f, weggegooid %.0f\n", boolean(*h, {"open"}) ? "open" : "dicht",
                    str(*h, {"node"}, "").c_str(), num(*h, {"matched"}, 0), num(*h, {"unmatched"}, 0), num(*h, {"discarded"}, 0));
    if (const json::Value* m = path(r, {"magnetometer"})) {
        if (boolean(*m, {"calibrated"}))
            std::printf("Magnetometer: gekalibreerd (%s, houder %s), |m| %.3f, dip %s graden (ref %.2f), toestand %s%s%s; %s\n",
                        str(*m, {"calibration", "kind"}).c_str(), str(*m, {"calibration", "holder"}).c_str(), num(*m, {"live", "norm"}, 0),
                        fmt(num(*m, {"live", "dip_deg"}), "%.2f").c_str(), num(*m, {"calibration", "dip_ref_deg"}, 0),
                        str(*m, {"live", "state"}).c_str(), str(*m, {"live", "reasons"}, "").empty() ? "" : ", afgewezen: ",
                        str(*m, {"live", "reasons"}, "").c_str(), boolean(*m, {"used_in_fusion"}) ? "in de fusie" : "niet in de fusie");
        else
            std::printf("Magnetometer: niet gekalibreerd (%s)\n", str(*m, {"calibration_error"}).c_str());
        if (const json::Value* y = path(*m, {"yaw_correction"}))
            std::printf("Yaw-correctie: %s, %s; openstaand %+.2f graden (σ %.2f), drift %+.3f graden/s, verticale bias %+.3f graden/s, "
                        "correctie nu %+.3f graden/s (grens %.3f), totaal %+.2f graden\n",
                        str(*y, {"mode"}).c_str(), boolean(*y, {"applied"}) ? (boolean(*y, {"locked"}) ? "actief" : "wacht op vergrendeling") : "schaduw (niet toegepast)",
                        num(*y, {"pending_deg"}, 0), num(*y, {"sigma_deg"}, 0), num(*y, {"drift_deg_s"}, 0), num(*y, {"vertical_bias_deg_s"}, 0),
                        num(*y, {"rate_deg_s"}, 0), num(*y, {"rate_limit_deg_s"}, 0), num(*y, {"total_deg"}, 0));
        if (const json::Value* c = path(*m, {"session", "mag_calibration"}); c && boolean(*c, {"active"}))
            std::printf("              kalibratie loopt: dekking %.0f %%, %.0f samples\n", num(*c, {"coverage_pct"}, 0), num(*c, {"samples"}, 0));
    }
    if (const json::Value* rc = path(r, {"recording"}); rc && rc->is_object()) print_record_status(*rc);
}

void print_output(const json::Value& o, const std::string& what) {
    if (what == "arm") std::printf("Uitvoer GEWAPEND.\n");
    else if (what == "disarm") std::printf("Uitvoer ontwapend; alle knoppen vrijgegeven.\n");
    std::printf("Uitvoer:      %s; apparaat %s%s (%s:%s), event %s, abs %.0f..%.0f\n", boolean(o, {"armed"}) ? "GEWAPEND" : "ontwapend",
                boolean(o, {"device", "created"}) ? "aangemaakt" : "niet aangemaakt", "", str(o, {"device", "vendor"}).c_str(),
                str(o, {"device", "product"}).c_str(), str(o, {"device", "event_node"}, "-").c_str(), num(o, {"device", "abs_min"}, 0),
                num(o, {"device", "abs_max"}, 0));
    std::printf("Positie:      aim %s,%s -> abs %s,%s; knoppen: %s\n", fmt(num(o, {"position", "nx"}), "%.3f").c_str(),
                fmt(num(o, {"position", "ny"}), "%.3f").c_str(), fmt(num(o, {"position", "abs_x"}), "%.0f").c_str(),
                fmt(num(o, {"position", "abs_y"}), "%.0f").c_str(), names(path(o, {"buttons_down"})).c_str());
    std::printf("Toetsenbord:  %s, toetsen: %s\n", boolean(o, {"keyboard", "created"}) ? "aangemaakt" : "niet aangemaakt",
                names(path(o, {"keys_down"})).c_str());
    if (const json::Value* m = path(o, {"map"}); m && m->is_object() && !m->obj.empty()) {
        std::string line;
        for (const auto& kv : m->obj) line += (line.empty() ? "" : ", ") + kv.first + "=" + (kv.second.is_string() ? kv.second.str : "?");
        std::printf("Indeling:     %s\n", line.c_str());
    }
    std::printf("Transformatie: invert %d/%d, schaal %.3f/%.3f, offset %.3f/%.3f, viewport (%.3f,%.3f)-(%.3f,%.3f)\n",
                boolean(o, {"transform", "invert_x"}), boolean(o, {"transform", "invert_y"}), num(o, {"transform", "scale_x"}),
                num(o, {"transform", "scale_y"}), num(o, {"transform", "offset_x"}), num(o, {"transform", "offset_y"}),
                num(o, {"transform", "viewport", "x0"}), num(o, {"transform", "viewport", "y0"}), num(o, {"transform", "viewport", "x1"}),
                num(o, {"transform", "viewport", "y1"}));
    std::printf("Patroon:      %s", str(o, {"pattern", "state"}).c_str());
    if (str(o, {"pattern", "state"}) != "none")
        std::printf(" (%s, stap %.0f/%.0f, %.1f s)", str(o, {"pattern", "kind"}).c_str(), num(o, {"pattern", "step"}, 0),
                    num(o, {"pattern", "steps"}, 0), num(o, {"pattern", "length_s"}, 0));
    std::printf("\nTellers:      %.0f positie-events, %.0f knop-events, %.0f geweigerd, %.0f schrijffouten, %.0f vrijgaves\n",
                num(o, {"stats", "position_events"}, 0), num(o, {"stats", "button_events"}, 0), num(o, {"stats", "rejected"}, 0),
                num(o, {"stats", "write_errors"}, 0), num(o, {"stats", "releases"}, 0));
}

double now_s() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}

// Eén verzoek; geeft het "result"-object (of nullptr) en zet code.
struct Res {
    int code = kExitOk;
    Reply r;
    const json::Value* result() const { return path(r.v, {"result"}); }
};
Res simple(const ipc::Request& rq, int read_timeout_ms = 3000) {
    ipc::Client c;
    Res x;
    x.r = call(c, rq, read_timeout_ms);
    x.code = x.r.code;
    return x;
}

void print_record_status(const json::Value& v) {
    if (boolean(v, {"active"})) {
        std::printf("Opname:       loopt, %s, %.0f s, %.2f MiB van %.0f MiB, %.0f rapporten, %.0f optisch, %.0f LED, %.0f labels, %.0f verloren\n",
                    str(v, {"file"}).c_str(), num(v, {"elapsed_s"}, 0), num(v, {"bytes"}, 0) / 1048576.0,
                    num(v, {"max_bytes"}, 0) / 1048576.0, num(v, {"reports"}, 0), num(v, {"optical"}, 0),
                    num(v, {"led_events"}, 0), num(v, {"marks"}, 0), num(v, {"dropped"}, 0));
    } else if (const json::Value* l = path(v, {"last"})) {
        std::printf("Opname:       geen actief; laatste %s/%s (%s), %.1f s, %.2f MiB, %.0f rapporten, %.0f optisch, %.0f verloren\n",
                    str(v, {"dir"}).c_str(), str(*l, {"file"}).c_str(), str(*l, {"reason"}).c_str(), num(*l, {"duration_s"}, 0),
                    num(*l, {"bytes"}, 0) / 1048576.0, num(*l, {"reports"}, 0), num(*l, {"optical"}, 0), num(*l, {"dropped"}, 0));
    } else {
        std::printf("Opname:       geen\n");
    }
}

void print_record_summary(const json::Value& l) {
    std::printf("Opname gestopt (%s): %s/%s\n  %.1f s, %.2f MiB, %.0f rapporten, %.0f optische metingen, %.0f LED-wissels, %.0f labels, %.0f verloren, schrijffouten %.0f\n",
                str(l, {"reason"}).c_str(), str(l, {"dir"}).c_str(), str(l, {"file"}).c_str(), num(l, {"duration_s"}, 0),
                num(l, {"bytes"}, 0) / 1048576.0, num(l, {"reports"}, 0), num(l, {"optical"}, 0), num(l, {"led_events"}, 0),
                num(l, {"marks"}, 0), num(l, {"dropped"}, 0), num(l, {"write_errors"}, 0));
}

int cmd_record(bool json, const std::string& action, const std::string& tag, const std::string& mark, double duration, double max_mb) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::Record;
    if (action == "stop" || action == "status" || action == "mark") {
        rq.action = action;
        rq.label = mark;
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) { std::printf("%s\n", x.r.line.c_str()); return kExitOk; }
        if (action == "stop") print_record_summary(*x.result());
        else if (action == "mark") std::printf("Label '%s' toegevoegd.\n", mark.c_str());
        else print_record_status(*x.result());
        return kExitOk;
    }
    if (!action.empty() && action != "start") { std::fprintf(stderr, "ravengunctl: record kent --start, --stop, --status en --mark\n"); return kExitUsage; }
    rq.action = "start";
    rq.tag = tag;
    if (duration > 0) rq.duration_s = std::min(duration, 7200.0);
    if (max_mb > 0) rq.max_mb = max_mb;
    Res x = simple(rq);
    if (x.code != kExitOk) return x.code;
    if (action == "start") {
        if (json) std::printf("%s\n", x.r.line.c_str());
        else std::printf("Opname gestart: %s/%s (stoppen: ravengunctl record --stop)\n", str(*x.result(), {"dir"}).c_str(),
                         str(*x.result(), {"file"}).c_str());
        return kExitOk;
    }
    // Blokkerend: voortgang tonen tot de daemon zelf stopt (duur/grootte) of Ctrl-C.
    if (!json) std::printf("Opname gestart: %s/%s; Ctrl-C stopt.\n", str(*x.result(), {"dir"}).c_str(), str(*x.result(), {"file"}).c_str());
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!g_stop.load()) {
        timespec ts{1, 0};
        nanosleep(&ts, nullptr);
        ipc::Request st;
        st.cmd = ipc::Cmd::Record;
        st.action = "status";
        Res y = simple(st);
        if (y.code != kExitOk) return y.code;
        const json::Value* v = y.result();
        if (!v) return kExitProtocol;
        if (!boolean(*v, {"active"})) {
            if (json) std::printf("%s\n", y.r.line.c_str());
            else if (const json::Value* l = path(*v, {"last"})) print_record_summary(*l);
            return kExitOk;
        }
        if (!json) {
            std::printf("\r  %.0f s, %.2f MiB, %.0f rapporten, %.0f optisch   ", num(*v, {"elapsed_s"}, 0), num(*v, {"bytes"}, 0) / 1048576.0,
                        num(*v, {"reports"}, 0), num(*v, {"optical"}, 0));
            std::fflush(stdout);
        }
    }
    if (!json) std::printf("\n");
    ipc::Request sp;
    sp.cmd = ipc::Cmd::Record;
    sp.action = "stop";
    Res z = simple(sp);
    if (z.code != kExitOk) return z.code;
    if (json) std::printf("%s\n", z.r.line.c_str());
    else print_record_summary(*z.result());
    return kExitOk;
}

int cmd_led(bool json, const std::string& arg) {
    vision::Rgb c{};
    int r = 0, g = 0, b = 0;
    char tail = 0;
    if (std::sscanf(arg.c_str(), "%d,%d,%d%c", &r, &g, &b, &tail) == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
        c = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    } else if (!vision::led_color_by_name(arg, c)) {
        std::fprintf(stderr, "ravengunctl: onbekende LED-kleur '%s'\n", arg.c_str());
        return kExitUsage;
    }
    ipc::Request rq;
    rq.cmd = ipc::Cmd::SetLed;
    rq.led_r = c.r; rq.led_g = c.g; rq.led_b = c.b;
    Res x = simple(rq);
    if (x.code != kExitOk) return x.code;
    if (json) std::printf("%s\n", x.r.line.c_str());
    else std::printf("LED: %d,%d,%d%s\n", c.r, c.g, c.b, boolean(*x.result(), {"connected"}) ? "" : " (geen Move verbonden; geldt bij verbinden)");
    std::printf("%s", json ? "" : "Let op: de tracker zoekt nog de tint van de opstartkleur.\n");
    return kExitOk;
}

void print_fit(const json::Value& f) {
    std::printf("Resultaat:    %s%s\n", boolean(f, {"ok"}) ? "GESLAAGD" : boolean(f, {"marginal"}) ? "MARGINAAL (bruikbaarheid volgt uit de veldcheck)" : "NIET GESLAAGD",
                boolean(f, {"saved"}) ? (", opgeslagen in " + str(f, {"file"})).c_str() : ", niet opgeslagen");
    std::printf("Fit:          %s, %.0f samples (%.0f gezien), %.0f s\n", str(f, {"kind"}).c_str(), num(f, {"samples"}, 0),
                num(f, {"samples_seen"}, 0), num(f, {"duration_s"}, 0));
    std::printf("Kwaliteit:    dekking %.0f %%, restfout |m|-1 RMS %.2f %% (max %.2f %%), veldsterkte %.1f counts\n",
                num(f, {"coverage"}, 0) * 100, num(f, {"rms_norm_err"}, 0) * 100, num(f, {"max_norm_err"}, 0) * 100,
                num(f, {"field_counts"}, 0));
    std::printf("Dip:          gemiddeld %.2f graden, spreiding %.2f graden (zonder Y-flip %.2f), %.0f rustige samples\n",
                num(f, {"dip_mean_deg"}, 0), num(f, {"dip_std_deg"}, 0), num(f, {"dip_std_noflip_deg"}, 0), num(f, {"dip_samples"}, 0));
    if (const json::Value* p = path(f, {"problems"}); p && p->is_array())
        for (const auto& e : p->arr) std::printf("  - %s\n", e.str.c_str());
}

int cmd_calibrate_ui(bool json, const std::string& action) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::CalibrateUi;
    rq.action = action;
    Res x = simple(rq, 10000);  // browser starten of sluiten kan enkele seconden duren
    if (x.code != kExitOk) return x.code;
    if (json) std::printf("%s\n", x.r.line.c_str());
    else std::printf("%s\n", str(*x.result(), {"message"}).c_str());
    return kExitOk;
}

int cmd_calibrate_mag(bool json, const std::string& action, const std::string& holder, double duration) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::CalibMag;
    if (action == "check") {
        // Veldcheck: norm en dip t.o.v. de opgeslagen kalibratie, gemiddeld over de duur.
        double dur = duration > 0 ? duration : 3.0;
        rq.action = "status";
        double sn = 0, sd = 0, maxn = 0, maxd = 0, ref = NAN;
        int n = 0, nd = 0, disturbed = 0;
        double t0 = now_s();
        while (now_s() - t0 < dur) {
            Res x = simple(rq);
            if (x.code != kExitOk) return x.code;
            const json::Value* v = x.result();
            if (!v || !boolean(*v, {"calibrated"})) {
                std::fprintf(stderr, "ravengunctl: geen magnetometerkalibratie geladen (%s)\n", v ? str(*v, {"calibration_error"}).c_str() : "?");
                return kExitTempFail;
            }
            ref = num(*v, {"calibration", "dip_ref_deg"});
            double nm = num(*v, {"live", "norm"}), dp = num(*v, {"live", "dip_deg"});
            if (!std::isnan(nm)) { sn += nm; maxn = std::max(maxn, std::fabs(nm - 1)); ++n; }
            if (!std::isnan(dp)) { sd += dp; maxd = std::max(maxd, std::fabs(dp - ref)); ++nd; }
            if (str(*v, {"live", "state"}) == "disturbed") ++disturbed;
            timespec ts{0, 100'000'000};
            nanosleep(&ts, nullptr);
        }
        double mn = n ? sn / n : NAN, md = nd ? sd / nd : NAN;
        bool ok = n > 0 && nd > 0 && std::fabs(mn - 1) <= 0.05 && std::fabs(md - ref) <= 2.0 && disturbed == 0;
        if (json) {
            std::printf("{\"ok\":%s,\"norm_mean\":%.5f,\"norm_max_dev\":%.5f,\"dip_mean_deg\":%.3f,\"dip_ref_deg\":%.3f,\"dip_max_dev_deg\":%.3f,\"samples\":%d,\"disturbed_samples\":%d}\n",
                        ok ? "true" : "false", mn, maxn, md, ref, maxd, n, disturbed);
        } else {
            std::printf("Veldcheck (%.0f s): %s\n  norm gemiddeld %.4f (max afwijking %.1f %%; grens 5 %%)\n  dip gemiddeld %.2f graden, referentie %.2f (max afwijking %.2f; grens 2)\n  verstoord in %d van %d metingen\n",
                        dur, ok ? "IN ORDE" : "NIET IN ORDE (magnetometer hier onbetrouwbaar)", mn, maxn * 100, md, ref, maxd, disturbed, n);
        }
        return ok ? kExitOk : 1;
    }
    if (!action.empty()) {
        rq.action = action;
        rq.holder = holder;
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) { std::printf("%s\n", x.r.line.c_str()); return kExitOk; }
        if (action == "stop") print_fit(*x.result());
        else std::printf("%s\n", x.r.line.c_str());
        return kExitOk;
    }
    // Interactief: starten, voortgang tonen, zelf stoppen bij voldoende dekking (of na de duur).
    rq.action = "start";
    rq.holder = holder;
    Res x = simple(rq);
    if (x.code != kExitOk) return x.code;
    double max_s = duration > 0 ? duration : 180.0;
    if (!json) std::printf("Magnetometerkalibratie gestart. Draai het geweer langzaam in alle richtingen (achtjes, kolf omhoog\nen omlaag, rol om de loop). Stopt zelf bij >= 80 %% dekking en >= 300 samples; Ctrl-C breekt af.\n");
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    double t0 = now_s();
    bool ready = false;
    while (!g_stop.load() && now_s() - t0 < max_s) {
        timespec ts{0, 500'000'000};
        nanosleep(&ts, nullptr);
        ipc::Request st;
        st.cmd = ipc::Cmd::CalibMag;
        st.action = "status";
        Res y = simple(st);
        if (y.code != kExitOk) return y.code;
        const json::Value* s = y.result() ? path(*y.result(), {"session", "mag_calibration"}) : nullptr;
        if (!s || !boolean(*s, {"active"})) { std::fprintf(stderr, "\nravengunctl: kalibratie niet meer actief (verbinding weg?)\n"); return kExitTempFail; }
        if (json) std::printf("%s\n", y.r.line.c_str());
        else std::printf("\r  dekking %3.0f %%, %4.0f samples, %3.0f s   ", num(*s, {"coverage_pct"}, 0), num(*s, {"samples"}, 0), num(*s, {"elapsed_s"}, 0));
        std::fflush(stdout);
        if (boolean(*s, {"ready"})) { ready = true; break; }
    }
    if (!json) std::printf("\n");
    ipc::Request sp;
    sp.cmd = ipc::Cmd::CalibMag;
    sp.action = g_stop.load() ? "abort" : "stop";
    Res z = simple(sp);
    if (z.code != kExitOk) return z.code;
    if (g_stop.load()) { std::printf("Afgebroken; er is niets opgeslagen.\n"); return kExitOk; }
    if (json) std::printf("%s\n", z.r.line.c_str());
    else {
        if (!ready) std::printf("Tijd op (%.0f s) zonder voldoende dekking; er wordt toch gefit.\n", max_s);
        print_fit(*z.result());
    }
    return boolean(*z.result(), {"ok"}) ? kExitOk : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ravengun;
    try {
        std::string cmd;
        bool json = false, full = false;
        double rate = -1, duration = -1, min_cutoff = -1, beta = -1, dwell = -1, margin = -1, max_mb = -1;
        double inv_x = -1, inv_y = -1, sc_x = -1, sc_y = -1, off_x = NAN, off_y = NAN, lb_screen = -1, lb_content = -1;
        std::vector<std::string> pos;
        int filter_on = -1;
        std::string arg2, tag, holder, mark, action;
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
            else if (a == "--dwell") dwell = need(i, "--dwell", 50, 60000);
            else if (a == "--margin") margin = need(i, "--margin", 0, 0.499);
            else if (a == "--invert-x") inv_x = need(i, "--invert-x", 0, 1);
            else if (a == "--invert-y") inv_y = need(i, "--invert-y", 0, 1);
            else if (a == "--scale-x") sc_x = need(i, "--scale-x", 0.05, 20);
            else if (a == "--scale-y") sc_y = need(i, "--scale-y", 0.05, 20);
            else if (a == "--offset-x") off_x = need(i, "--offset-x", -1, 1);
            else if (a == "--offset-y") off_y = need(i, "--offset-y", -1, 1);
            else if (a == "--letterbox") { lb_screen = need(i, "--letterbox", 0.1, 10); lb_content = need(i, "--letterbox", 0.1, 10); }
            else if (a == "--on") filter_on = 1;
            else if (a == "--max-mb") max_mb = need(i, "--max-mb", 1, 1024);
            else if (a == "--close") { if (!action.empty()) { std::fprintf(stderr, "ravengunctl: kies één actie\n"); return kExitUsage; } action = "close"; }
            else if (a == "--start" || a == "--stop" || a == "--status" || a == "--abort" || a == "--check") {
                if (!action.empty()) { std::fprintf(stderr, "ravengunctl: kies één van --start/--stop/--status/--abort/--check\n"); return kExitUsage; }
                action = a.substr(2);
            }
            else if ((a == "--tag" || a == "--holder" || a == "--mark") && i + 1 < argc) {
                std::string v = argv[++i];
                if (a == "--tag") tag = v;
                else if (a == "--holder") holder = v;
                else { mark = v; action = "mark"; }
            }
            else if (a == "--off") filter_on = 0;
            else if (!a.empty() && a[0] == '-' && !(a.size() > 1 && (std::isdigit(static_cast<unsigned char>(a[1])) || a[1] == '.'))) { std::fprintf(stderr, "ravengunctl: onbekende optie '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
            else if (cmd.empty()) cmd = a;
            else if (cmd == "test-pattern" || cmd == "output-button") pos.push_back(a);
            else if (arg2.empty() && (cmd == "calibrate" || cmd == "led" || cmd == "fusion")) arg2 = a;
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
        if (cmd == "camera") {
            rq.cmd = ipc::Cmd::Camera;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_camera(*res);
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
        if (cmd == "arm" || cmd == "disarm" || cmd == "output") {
            rq.cmd = cmd == "arm" ? ipc::Cmd::Arm : cmd == "disarm" ? ipc::Cmd::Disarm : ipc::Cmd::Output;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "test-pattern") {
            if (pos.empty()) { std::fprintf(stderr, "ravengunctl: test-pattern vereist center, corners, grid, sweep, point X Y of stop\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::TestPattern;
            rq.pattern = pos[0];
            if (pos[0] == "point") {
                char *e1 = nullptr, *e2 = nullptr;
                if (pos.size() != 3) { std::fprintf(stderr, "ravengunctl: test-pattern point vereist X en Y (0..1)\n"); return kExitUsage; }
                double px = std::strtod(pos[1].c_str(), &e1), py = std::strtod(pos[2].c_str(), &e2);
                if (*e1 || *e2 || !(px >= 0 && px <= 1 && py >= 0 && py <= 1)) { std::fprintf(stderr, "ravengunctl: X en Y moeten in [0,1] liggen\n"); return kExitUsage; }
                rq.x = px;
                rq.y = py;
            } else if (pos.size() != 1) { std::fprintf(stderr, "ravengunctl: onverwacht argument bij test-pattern\n"); return kExitUsage; }
            if (dwell > 0) rq.dwell_ms = dwell;
            if (duration > 0) rq.duration_s = duration;
            if (margin >= 0) rq.margin = margin;
            if (rate > 0) rq.rate_hz_pattern = rate;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "output-button") {
            if (pos.empty() || pos.size() > 2) { std::fprintf(stderr, "ravengunctl: output-button vereist een knop en optioneel down, up of tap\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::OutputButton;
            rq.button = pos[0];
            rq.action = pos.size() > 1 ? pos[1] : "tap";
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Knop %s: %s.\n", rq.button.c_str(), rq.action.c_str());
            return kExitOk;
        }
        if (cmd == "set-output") {
            rq.cmd = ipc::Cmd::SetOutput;
            if (inv_x >= 0) rq.invert_x = inv_x > 0.5;
            if (inv_y >= 0) rq.invert_y = inv_y > 0.5;
            if (sc_x > 0) rq.scale_x = sc_x;
            if (sc_y > 0) rq.scale_y = sc_y;
            if (!std::isnan(off_x)) rq.offset_x = off_x;
            if (!std::isnan(off_y)) rq.offset_y = off_y;
            if (lb_screen > 0) { rq.screen_aspect = lb_screen; rq.content_aspect = lb_content; }
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "record") return cmd_record(json, action, tag, mark, duration, max_mb);
        if (cmd == "led") return cmd_led(json, arg2);
        if (cmd == "fusion") {
            if (arg2 != "mahony" && arg2 != "mahony_mag" && arg2 != "mahony-mag") {
                std::fprintf(stderr, "ravengunctl: fusion verwacht mahony of mahony_mag\n");
                return kExitUsage;
            }
            rq.cmd = ipc::Cmd::SetFusion;
            rq.mode = arg2 == "mahony" ? "mahony" : "mahony_mag";
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Oriëntatieschatter: %s%s\n", str(r.v, {"result", "mode"}).c_str(),
                             boolean(r.v, {"result", "saved"}) ? " (bewaard in config.json)" : " (niet bewaard)");
            return kExitOk;
        }
        if (cmd == "calibrate-ui") {
            if (!action.empty() && action != "close" && action != "status") { std::fprintf(stderr, "ravengunctl: calibrate-ui kent alleen --close en --status\n"); return kExitUsage; }
            return cmd_calibrate_ui(json, action.empty() ? "open" : action);
        }
        if (cmd == "calibrate") {
            if (arg2 != "mag") { std::fprintf(stderr, "ravengunctl: alleen 'calibrate mag' bestaat (nog)\n"); return kExitUsage; }
            return cmd_calibrate_mag(json, action, holder, duration);
        }
        std::fprintf(stderr, "ravengunctl: onbekend commando '%s'\n\n", cmd.c_str());
        usage(stderr);
        return kExitUsage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ravengunctl: interne fout: %s\n", e.what());
        return kExitInternal;
    }
}
