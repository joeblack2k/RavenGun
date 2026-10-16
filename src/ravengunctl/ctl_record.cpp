// ravengunctl: record.
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

static void print_record_summary(const json::Value& l) {
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

}  // namespace ravengunctl
