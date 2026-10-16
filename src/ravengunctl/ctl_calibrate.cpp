// ravengunctl: calibrate ui en calibrate mag.
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

static void print_fit(const json::Value& f) {
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

}  // namespace ravengunctl
