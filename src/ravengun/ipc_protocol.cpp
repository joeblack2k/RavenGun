#include "ravengun/ipc_protocol.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::ipc {

const char* to_string(Cmd c) {
    switch (c) {
        case Cmd::Status: return "status";
        case Cmd::Sample: return "sample";
        case Cmd::Recenter: return "recenter";
        case Cmd::Watch: return "watch";
        case Cmd::SetFilter: return "set_filter";
        case Cmd::SetAim: return "set_aim";
    }
    return "?";
}

namespace {

Error err(const char* code, std::string msg, const std::string& id = {}) { return Error{code, std::move(msg), id}; }

bool allowed(const json::Value& o, std::initializer_list<const char*> extra, std::string& bad) {
    for (const auto& kv : o.obj) {
        if (kv.first == "v" || kv.first == "cmd" || kv.first == "id") continue;
        bool ok = false;
        for (const char* e : extra) if (kv.first == e) ok = true;
        if (!ok) { bad = kv.first; return false; }
    }
    return true;
}

// Haalt een optioneel getal binnen [lo, hi]; false bij een fout (err gevuld).
bool opt_number(const json::Value& o, const char* key, double lo, double hi, std::optional<double>& out, Error& e,
                const std::string& id) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (!v->is_number()) { e = err("invalid_argument", std::string("'") + key + "' moet een getal zijn", id); return false; }
    if (!(v->num >= lo && v->num <= hi)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "'%s' moet tussen %g en %g liggen", key, lo, hi);
        e = err("invalid_argument", buf, id);
        return false;
    }
    out = v->num;
    return true;
}

bool opt_bool(const json::Value& o, const char* key, std::optional<bool>& out, Error& e, const std::string& id) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (!v->is_bool()) { e = err("invalid_argument", std::string("'") + key + "' moet true of false zijn", id); return false; }
    out = v->b;
    return true;
}

}  // namespace

std::variant<Request, Error> parse_request(std::string_view line) {
    if (line.size() > kMaxLineBytes) return err("line_too_long", "verzoek groter dan 64 KiB");
    auto pr = json::parse(line);
    if (!pr.ok) return err("parse_error", "ongeldige JSON: " + pr.error + " (positie " + std::to_string(pr.offset) + ")");
    const json::Value& o = pr.value;
    if (!o.is_object()) return err("bad_request", "verzoek moet een JSON-object zijn");

    Request r;
    if (const json::Value* id = o.find("id")) {
        if (id->is_number() && std::floor(id->num) == id->num && std::fabs(id->num) < 9e15) {
            r.id_json = std::to_string(static_cast<long long>(id->num));
        } else if (id->is_string() && id->str.size() <= 128) {
            r.id_json = json::quote(id->str);
        } else {
            return err("bad_request", "'id' moet een geheel getal of een string (max 128 tekens) zijn");
        }
    }
    const json::Value* v = o.find("v");
    if (!v) return err("bad_request", "'v' (protocolversie) ontbreekt", r.id_json);
    if (!v->is_number() || v->num != kProtocolVersion)
        return err("unsupported_version", "alleen protocolversie 1 wordt ondersteund", r.id_json);
    const json::Value* c = o.find("cmd");
    if (!c || !c->is_string()) return err("bad_request", "'cmd' (string) ontbreekt", r.id_json);

    std::string bad;
    Error e;
    const std::string& cmd = c->str;
    if (cmd == "status" || cmd == "sample") {
        r.cmd = cmd == "status" ? Cmd::Status : Cmd::Sample;
        if (!allowed(o, {"full_ids"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<bool> f;
        if (!opt_bool(o, "full_ids", f, e, r.id_json)) return e;
        r.full_ids = f.value_or(false);
    } else if (cmd == "recenter") {
        r.cmd = Cmd::Recenter;
        if (!allowed(o, {}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
    } else if (cmd == "watch") {
        r.cmd = Cmd::Watch;
        if (!allowed(o, {"rate_hz", "full_ids"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        std::optional<double> rate;
        if (!opt_number(o, "rate_hz", kWatchRateMin, kWatchRateMax, rate, e, r.id_json)) return e;
        r.rate_hz = rate.value_or(30.0);
        std::optional<bool> f;
        if (!opt_bool(o, "full_ids", f, e, r.id_json)) return e;
        r.full_ids = f.value_or(false);
    } else if (cmd == "set_filter") {
        r.cmd = Cmd::SetFilter;
        if (!allowed(o, {"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"}, bad))
            return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_bool(o, "enabled", r.filter_enabled, e, r.id_json)) return e;
        if (!opt_number(o, "min_cutoff_hz", 0.001, 100, r.min_cutoff_hz, e, r.id_json)) return e;
        if (!opt_number(o, "beta", 0, 1000, r.beta, e, r.id_json)) return e;
        if (!opt_number(o, "d_cutoff_hz", 0.001, 100, r.d_cutoff_hz, e, r.id_json)) return e;
        if (!r.filter_enabled && !r.min_cutoff_hz && !r.beta && !r.d_cutoff_hz)
            return err("invalid_argument", "set_filter zonder te wijzigen velden", r.id_json);
    } else if (cmd == "set_aim") {
        r.cmd = Cmd::SetAim;
        if (!allowed(o, {"h_edge_deg", "v_edge_deg"}, bad)) return err("bad_request", "onbekend veld '" + bad + "'", r.id_json);
        if (!opt_number(o, "h_edge_deg", 1, 89, r.h_edge_deg, e, r.id_json)) return e;
        if (!opt_number(o, "v_edge_deg", 1, 89, r.v_edge_deg, e, r.id_json)) return e;
        if (!r.h_edge_deg && !r.v_edge_deg) return err("invalid_argument", "set_aim zonder te wijzigen velden", r.id_json);
    } else {
        return err("unknown_command", "onbekend commando '" + cmd + "'", r.id_json);
    }
    return r;
}

std::string ok_response(const std::string& id_json, std::string_view result_json) {
    std::string s = "{\"v\":1,\"ok\":true";
    if (!id_json.empty()) s += ",\"id\":" + id_json;
    s += ",\"result\":";
    s += result_json;
    s += '}';
    return s;
}

std::string error_response(const Error& e) {
    json::Writer w;
    w.begin_object().key("v").value(kProtocolVersion).key("ok").value(false);
    if (!e.id_json.empty()) w.key("id").raw(e.id_json);
    w.key("error").begin_object().key("code").value(e.code).key("message").value(e.message).end_object();
    w.end_object();
    return w.str();
}

std::string event_line(std::string_view name, std::string_view data_json) {
    std::string s = "{\"v\":1,\"event\":" + json::quote(name) + ",\"data\":";
    s += data_json;
    s += '}';
    return s;
}

std::string make_request(const Request& r) {
    json::Writer w;
    w.begin_object().key("v").value(kProtocolVersion).key("cmd").value(to_string(r.cmd));
    if (!r.id_json.empty()) w.key("id").raw(r.id_json);
    switch (r.cmd) {
        case Cmd::Status:
        case Cmd::Sample:
            if (r.full_ids) w.key("full_ids").value(true);
            break;
        case Cmd::Watch:
            w.key("rate_hz").value(r.rate_hz);
            if (r.full_ids) w.key("full_ids").value(true);
            break;
        case Cmd::SetFilter:
            if (r.filter_enabled) w.key("enabled").value(*r.filter_enabled);
            if (r.min_cutoff_hz) w.key("min_cutoff_hz").value(*r.min_cutoff_hz);
            if (r.beta) w.key("beta").value(*r.beta);
            if (r.d_cutoff_hz) w.key("d_cutoff_hz").value(*r.d_cutoff_hz);
            break;
        case Cmd::SetAim:
            if (r.h_edge_deg) w.key("h_edge_deg").value(*r.h_edge_deg);
            if (r.v_edge_deg) w.key("v_edge_deg").value(*r.v_edge_deg);
            break;
        case Cmd::Recenter: break;
    }
    w.end_object();
    return w.str();
}

}  // namespace ravengun::ipc
