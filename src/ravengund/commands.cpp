#include "commands.hpp"

#include <chrono>
#include <cmath>

#include "hid_thread.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun;

namespace {

constexpr auto kJobTimeout = std::chrono::milliseconds(500);

std::string run_job(SharedState& sh, SharedState::Job j, std::string& err) {
    auto f = sh.post(std::move(j));
    if (f.wait_for(kJobTimeout) != std::future_status::ready) { err = "HID-thread reageert niet binnen 500 ms"; return {}; }
    try {
        return f.get();
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

// Resultaat van een HidContext-opdracht uit fase 0/1/3: JSON, of "!code|melding".
std::string context_reply(const ipc::Request& r, const std::string& res, const std::string& err) {
    if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
    if (!res.empty() && res[0] == '!') {
        size_t bar = res.find('|');
        std::string code = res.substr(1, bar == std::string::npos ? std::string::npos : bar - 1);
        std::string msg = bar == std::string::npos ? std::string() : res.substr(bar + 1);
        return ipc::error_response({code, msg, r.id_json});
    }
    return ipc::ok_response(r.id_json, res);
}

}  // namespace

std::string execute_command(SharedState& sh, const ipc::Request& r) {
    std::string err;
    switch (r.cmd) {
        case ipc::Cmd::Status: {
            bool full = r.full_ids;
            std::string res = run_job(sh, [full](HidContext& h) { return h.status_json(full); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            // Camerastatus (uit de camera-service, niet uit de HID-thread) als extra sleutel.
            if (sh.camera_status && !res.empty() && res.back() == '}') {
                res.pop_back();
                res += ",\"camera\":" + sh.camera_status() + "}";
            }
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Camera: {
            if (!sh.camera_status) return ipc::error_response({"unavailable", "geen camera-service", r.id_json});
            return ipc::ok_response(r.id_json, sh.camera_status());
        }
        case ipc::Cmd::Sample: {
            Snapshot s = sh.get();
            json::Writer w;
            core::write_sample_json(w, s.sample, r.full_ids);
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Recenter: {
            std::string res = run_job(sh, [](HidContext& h) { return std::string(h.recenter() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1")
                return ipc::error_response({"unavailable", "geen geldige, actuele oriëntatie: is de Move verbonden?", r.id_json});
            Snapshot s = sh.get();
            json::Writer w;
            w.begin_object().key("recentered").value(true).key("sample");
            core::write_sample_json(w, s.sample, false);
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Watch:
            return ipc::error_response({"bad_request", "watch hoort bij een IPC-verbinding", r.id_json});
        case ipc::Cmd::SetFilter: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::OneEuroConfig f = h.filter();
                if (rq.filter_enabled) f.enabled = *rq.filter_enabled;
                if (rq.min_cutoff_hz) f.min_cutoff_hz = *rq.min_cutoff_hz;
                if (rq.beta) f.beta = *rq.beta;
                if (rq.d_cutoff_hz) f.d_cutoff_hz = *rq.d_cutoff_hz;
                h.set_filter(f);
                json::Writer w;
                w.begin_object().key("enabled").value(f.enabled).key("min_cutoff_hz").value(f.min_cutoff_hz)
                    .key("beta").value(f.beta).key("d_cutoff_hz").value(f.d_cutoff_hz).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::SetAim: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::AimConfig a = h.aim();
                if (rq.h_edge_deg) a.h_edge_deg = *rq.h_edge_deg;
                if (rq.v_edge_deg) a.v_edge_deg = *rq.v_edge_deg;
                h.set_aim(a);
                json::Writer w;
                w.begin_object().key("h_edge_deg").value(a.h_edge_deg).key("v_edge_deg").value(a.v_edge_deg).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Record: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) { return h.record(rq); }, err);
            return context_reply(r, res, err);
        }
        case ipc::Cmd::CalibMag: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) { return h.calib_mag(rq); }, err);
            return context_reply(r, res, err);
        }
        case ipc::Cmd::SetLed: {
            int c0 = r.rgb[0].value_or(0), c1 = r.rgb[1].value_or(0), c2 = r.rgb[2].value_or(0);
            std::string res = run_job(sh, [c0, c1, c2](HidContext& h) { return h.set_led(c0, c1, c2); }, err);
            return context_reply(r, res, err);
        }
    }
    return ipc::error_response({"internal", "onbekende toestand", r.id_json});
}

std::string execute_line(SharedState& sh, const std::string& line) {
    auto pr = ipc::parse_request(line);
    if (auto* e = std::get_if<ipc::Error>(&pr)) return ipc::error_response(*e);
    return execute_command(sh, std::get<ipc::Request>(pr));
}

}  // namespace ravengund
