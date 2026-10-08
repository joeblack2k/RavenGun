#include "ravengun/web/web_core.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::web {

namespace {

const char* status_text(int s) {
    switch (s) {
        case 200: return "OK";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
    }
    return "Status";
}

// Bewust geen Access-Control-*-headers: de API is alleen same-origin bruikbaar.
const char* kCommonHeaders =
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "X-Frame-Options: DENY\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Cross-Origin-Resource-Policy: same-origin\r\n"
    "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; "
    "img-src 'self' data:; base-uri 'none'; form-action 'self'; frame-ancestors 'none'\r\n"
    "Connection: close\r\n";

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return std::string(s.substr(a, b - a));
}

bool is_tchar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}

HttpResponse error_json(int status, const char* code, const std::string& msg) {
    HttpResponse r;
    r.status = status;
    json::Writer w;
    w.begin_object().key("v").value(1).key("ok").value(false).key("error").begin_object();
    w.key("code").value(code).key("message").value(msg).end_object().end_object();
    r.body = w.str();
    return r;
}

bool is_ipv4_literal(std::string_view h) {
    int dots = 0;
    if (h.empty()) return false;
    for (char c : h) {
        if (c == '.') ++dots;
        else if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return dots == 3;
}

bool is_ipv6_literal(std::string_view h) {
    if (h.size() < 4 || h.front() != '[' || h.back() != ']') return false;
    for (char c : h.substr(1, h.size() - 2))
        if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.')) return false;
    return true;
}

// Token uit een query "a=b&token=X": alleen hexadecimale tekens.
std::string query_token(const std::string& q) {
    size_t pos = 0;
    while (pos <= q.size()) {
        size_t amp = q.find('&', pos);
        std::string part = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (part.rfind("token=", 0) == 0) return part.substr(6);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

bool all_hex(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Zet een JSON-body om in een IPC-verzoekregel (set_filter, set_aim, set_led, set_camera, ...).
// Alleen bekende sleutels met een bool of getal (of een korte tekst bij 'string_keys'); de
// waardebereiken controleert de IPC-parser (één bron van waarheid).
bool build_set_request(const char* cmd, const std::string& body, std::initializer_list<const char*> keys,
                       std::string& line, std::string& err, std::initializer_list<const char*> string_keys = {},
                       const char* action = nullptr) {
    auto pr = json::parse(body);
    if (!pr.ok) { err = "ongeldige JSON: " + pr.error; return false; }
    if (!pr.value.is_object()) { err = "verwacht een JSON-object"; return false; }
    json::Writer w;
    w.begin_object().key("v").value(1).key("cmd").value(cmd);
    if (action) w.key("action").value(action);
    for (const auto& kv : pr.value.obj) {
        bool known = false;
        for (const char* k : keys) if (kv.first == k) known = true;
        bool is_str_key = false;
        for (const char* k : string_keys) if (kv.first == k) is_str_key = true;
        if (!known && !is_str_key) { err = "onbekend veld '" + kv.first + "'"; return false; }
        if (is_str_key) {
            if (!kv.second.is_string()) { err = "'" + kv.first + "' moet een tekst zijn"; return false; }
            w.key(kv.first).value(kv.second.str);
        } else if (kv.second.is_number()) w.key(kv.first).value(kv.second.num);
        else if (kv.second.is_bool()) w.key(kv.first).value(kv.second.b);
        else { err = "'" + kv.first + "' moet een getal of true/false zijn"; return false; }
    }
    w.end_object();
    line = w.str();
    return true;
}

}  // namespace

std::string serialize(const HttpResponse& r) {
    std::string s = "HTTP/1.1 " + std::to_string(r.status) + " " + status_text(r.status) + "\r\n";
    s += kCommonHeaders;
    if (r.sse) {
        s += "Content-Type: text/event-stream\r\n";
    } else {
        s += "Content-Type: " + r.content_type + "\r\n";
        s += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    }
    for (const auto& h : r.headers) s += h.first + ": " + h.second + "\r\n";
    s += "\r\n";
    if (!r.sse) s += r.body;
    return s;
}

ParseResult parse_http(std::string_view buf) {
    ParseResult pr;
    size_t he = buf.find("\r\n\r\n");
    if (he == std::string_view::npos) {
        if (buf.size() > kMaxHeaderBytes) { pr.state = ParseState::TooLarge; pr.error_status = 431; }
        return pr;
    }
    if (he > kMaxHeaderBytes) { pr.state = ParseState::TooLarge; pr.error_status = 431; return pr; }
    auto bad = [&](int st = 400) { pr.state = ParseState::Bad; pr.error_status = st; return pr; };

    std::string_view head = buf.substr(0, he);
    size_t eol = head.find("\r\n");
    std::string_view line = head.substr(0, eol);
    // Verzoekregel: METHOD SP target SP HTTP/1.x
    size_t s1 = line.find(' ');
    size_t s2 = s1 == std::string_view::npos ? s1 : line.find(' ', s1 + 1);
    if (s2 == std::string_view::npos) return bad();
    std::string_view method = line.substr(0, s1), target = line.substr(s1 + 1, s2 - s1 - 1), ver = line.substr(s2 + 1);
    if (method.empty() || method.size() > 8) return bad();
    for (char c : method) if (!std::isupper(static_cast<unsigned char>(c))) return bad();
    if (ver != "HTTP/1.1" && ver != "HTTP/1.0") return bad(505);
    if (target.empty() || target[0] != '/' || target.size() > 2048) return bad();
    for (char c : target) if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return bad();
    pr.request.method = std::string(method);
    size_t q = target.find('?');
    pr.request.path = std::string(target.substr(0, q));
    if (q != std::string_view::npos) pr.request.query = std::string(target.substr(q + 1));

    size_t pos = eol == std::string_view::npos ? head.size() : eol + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        std::string_view h = head.substr(pos, e == std::string_view::npos ? std::string_view::npos : e - pos);
        pos = e == std::string_view::npos ? head.size() : e + 2;
        if (h.empty() || h[0] == ' ' || h[0] == '\t') return bad();  // geen obs-fold
        size_t colon = h.find(':');
        if (colon == std::string_view::npos || colon == 0) return bad();
        for (char c : h.substr(0, colon)) if (!is_tchar(c)) return bad();
        std::string name = lower(std::string(h.substr(0, colon)));
        std::string val = trim(h.substr(colon + 1));
        for (char c : val) if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7f) return bad();
        if (pr.request.headers.count(name)) return bad();  // dubbele headers weigeren
        pr.request.headers[name] = val;
    }
    if (pr.request.headers.count("transfer-encoding")) return bad(501);
    size_t len = 0;
    auto cl = pr.request.headers.find("content-length");
    if (cl != pr.request.headers.end()) {
        const std::string& v = cl->second;
        if (v.empty() || v.size() > 9) return bad();
        for (char c : v) if (!std::isdigit(static_cast<unsigned char>(c))) return bad();
        len = static_cast<size_t>(std::stoul(v));
        if (len > kMaxBodyBytes) { pr.state = ParseState::TooLarge; pr.error_status = 413; return pr; }
    }
    size_t total = he + 4 + len;
    if (buf.size() < total) { pr.state = ParseState::Incomplete; return pr; }
    pr.request.body = std::string(buf.substr(he + 4, len));
    pr.consumed = total;
    pr.state = ParseState::Complete;
    return pr;
}

std::string validate(const WebConfig& c) {
    if (c.port < 1 || c.port > 65535) return "poort moet tussen 1 en 65535 liggen";
    if (c.lan && c.token.size() < 32) return "LAN-toegang vereist een token van minstens 32 tekens; web-interface niet gestart";
    if (c.csrf.size() < 16) return "CSRF-token ontbreekt";
    return {};
}

bool WebApp::host_allowed(const std::string& host_in) const {
    std::string host = lower(host_in);
    if (host.empty()) return false;
    std::string suffix = ":" + std::to_string(cfg_.port);
    if (host.size() <= suffix.size() || host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    std::string name = host.substr(0, host.size() - suffix.size());
    if (name == "127.0.0.1" || name == "localhost" || name == "[::1]") return true;
    if (!cfg_.lan) return false;
    if (is_ipv4_literal(name) || is_ipv6_literal(name)) return true;
    if (!cfg_.hostname.empty()) {
        std::string hn = lower(cfg_.hostname);
        if (name == hn || name == hn + ".local") return true;
    }
    return false;
}

bool WebApp::origin_allowed(const std::string& origin) const {
    if (origin.rfind("http://", 0) != 0) return false;
    return host_allowed(origin.substr(7));
}

bool WebApp::token_ok(const HttpRequest& r) const {
    if (!cfg_.lan) return true;
    std::string a = r.header("authorization");
    if (a.rfind("Bearer ", 0) == 0 && constant_time_equal(a.substr(7), cfg_.token)) return true;
    if (constant_time_equal(r.header("x-ravengun-token"), cfg_.token)) return true;
    std::string ck = r.header("cookie");
    size_t pos = 0;
    while (pos < ck.size()) {
        size_t semi = ck.find(';', pos);
        std::string part = trim(ck.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos));
        if (part.rfind("rg_token=", 0) == 0 && constant_time_equal(part.substr(9), cfg_.token)) return true;
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return false;
}

HttpResponse WebApp::api_command(const std::string& line) const {
    std::string resp = be_.command ? be_.command(line) : std::string();
    HttpResponse r;
    auto pr = json::parse(resp);
    if (!pr.ok || !pr.value.is_object()) return error_json(500, "internal", "ongeldig antwoord van de daemon");
    r.body = resp;
    const json::Value* ok = pr.value.find("ok");
    if (ok && ok->is_bool() && ok->b) return r;
    std::string code;
    if (const json::Value* e = pr.value.find("error"))
        if (const json::Value* c = e->find("code")) if (c->is_string()) code = c->str;
    if (code == "unavailable") r.status = 503;
    else if (code == "internal") r.status = 500;
    else r.status = 400;
    return r;
}

HttpResponse WebApp::handle(const HttpRequest& r) const {
    if (r.method != "GET" && r.method != "POST") {
        HttpResponse e = error_json(405, "method_not_allowed", "alleen GET en POST");
        e.headers.push_back({"Allow", "GET, POST"});
        return e;
    }
    // Host: tegen DNS-rebinding, voor elke request.
    if (!host_allowed(r.header("host"))) return error_json(403, "bad_host", "Host-header niet toegestaan");

    if (cfg_.lan) {
        // Eenmalige aanmelding: /?token=... zet een HttpOnly-cookie en stuurt door naar /.
        if (r.method == "GET" && r.path == "/") {
            std::string t = query_token(r.query);
            if (all_hex(t) && constant_time_equal(t, cfg_.token)) {
                HttpResponse s;
                s.status = 303;
                s.content_type = "text/plain; charset=utf-8";
                s.headers.push_back({"Location", "/"});
                s.headers.push_back({"Set-Cookie", "rg_token=" + cfg_.token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=31536000"});
                return s;
            }
        }
        if (!token_ok(r)) {
            HttpResponse e = error_json(401, "unauthorized",
                                        "token vereist (LAN-modus). Open /?token=... of stuur Authorization: Bearer ...");
            return e;
        }
    }

    if (r.method == "POST") {
        // Cross-site schrijfverzoeken: Origin, Sec-Fetch-Site, Content-Type en CSRF-token.
        if (r.headers.count("origin") && !origin_allowed(r.header("origin")))
            return error_json(403, "bad_origin", "Origin niet toegestaan");
        std::string sfs = r.header("sec-fetch-site");
        if (!sfs.empty() && sfs != "same-origin" && sfs != "none")
            return error_json(403, "bad_origin", "cross-site verzoek geweigerd");
        if (!constant_time_equal(r.header("x-csrf-token"), cfg_.csrf))
            return error_json(403, "csrf", "CSRF-token ontbreekt of is onjuist (header X-CSRF-Token)");
        std::string ct = lower(r.header("content-type"));
        if (ct.rfind("application/json", 0) != 0)
            return error_json(415, "unsupported_media_type", "Content-Type moet application/json zijn");
    }
    return route(r);
}

HttpResponse WebApp::route(const HttpRequest& r) const {
    const std::string& p = r.path;
    const bool get = r.method == "GET";
    auto wrong_method = [&](const char* allow) {
        HttpResponse e = error_json(405, "method_not_allowed", std::string("gebruik ") + allow);
        e.headers.push_back({"Allow", allow});
        return e;
    };

    if (p.rfind("/api/", 0) == 0) {
        if (p == "/api/status" || p == "/api/sample" || p == "/api/info" || p == "/api/settings") {
            if (!get) return wrong_method("GET");
            return api_command(std::string("{\"v\":1,\"cmd\":\"") + (p.c_str() + 5) + "\"}");
        }
        // Fase 0/3: alleen-lezen voortgang van opname en magnetometerkalibratie (voor de web-UI).
        if (p == "/api/record" || p == "/api/magnetometer") {
            if (!get) return wrong_method("GET");
            return api_command(p == "/api/record" ? "{\"v\":1,\"cmd\":\"record\",\"action\":\"status\"}"
                                                  : "{\"v\":1,\"cmd\":\"calib_mag\",\"action\":\"status\"}");
        }
        if (p == "/api/diagnostics") {
            // Diagnose-export (geredigeerd): als download aangeboden.
            if (!get) return wrong_method("GET");
            HttpResponse o = api_command("{\"v\":1,\"cmd\":\"diagnostics\"}");
            if (o.status == 200) o.headers.push_back({"Content-Disposition", "attachment; filename=\"ravengun-diagnose.json\""});
            return o;
        }
        if (p == "/api/camera") {
            if (get) return api_command("{\"v\":1,\"cmd\":\"camera\"}");
            std::string line, err;
            if (!build_set_request("set_camera", r.body, {"exposure", "gain", "auto_exposure", "auto_gain", "auto_awb"}, line, err, {"mode"}))
                return error_json(400, "bad_request", err);
            return api_command(line);
        }
        if (p == "/api/led") {
            if (get) return wrong_method("POST");
            std::string line, err;
            if (!build_set_request("set_led", r.body, {"r", "g", "b"}, line, err, {"preset"})) return error_json(400, "bad_request", err);
            return api_command(line);
        }
        if (p == "/api/web") {
            if (get) return wrong_method("POST");
            std::string line, err;
            if (!build_set_request("set_web", r.body, {"port"}, line, err)) return error_json(400, "bad_request", err);
            return api_command(line);
        }
        if (p == "/api/arm" || p == "/api/disarm") {
            if (get) return wrong_method("POST");
            return api_command(std::string("{\"v\":1,\"cmd\":\"") + (p.c_str() + 5) + "\"}");
        }
        if (p == "/api/test-pattern") {
            if (get) return wrong_method("POST");
            std::string line, err;
            if (!build_set_request("test_pattern", r.body, {"dwell_ms", "duration_s", "margin", "x", "y", "rate_hz"}, line, err, {"pattern"}))
                return error_json(400, "bad_request", err);
            return api_command(line);
        }
        if (p == "/api/output") {
            // GET: uitvoerstatus; POST: transformatie-instellingen (set_output).
            if (get) return api_command("{\"v\":1,\"cmd\":\"output\"}");
            std::string line, err;
            if (!build_set_request("set_output", r.body,
                                   {"invert_x", "invert_y", "scale_x", "scale_y", "offset_x", "offset_y", "viewport_x0", "viewport_y0",
                                    "viewport_x1", "viewport_y1", "screen_aspect", "content_aspect"}, line, err))
                return error_json(400, "bad_request", err);
            return api_command(line);
        }
        // Kalibratie-UI op de tv (docs/calibration-ui.md): alleen vaste acties, geen paden of programma's.
        if (p.rfind("/api/calibrate/", 0) == 0) {
            std::string line, err;
            const std::string sub = p.substr(15);
            if (sub == "ui") {
                if (get) return api_command("{\"v\":1,\"cmd\":\"calibrate_ui\",\"action\":\"status\"}");
                auto pr = json::parse(r.body);
                if (!pr.ok || !pr.value.is_object()) return error_json(400, "bad_request", "verwacht een JSON-object");
                const json::Value* a = pr.value.find("action");
                const json::Value* st = pr.value.find("step");
                bool step_ok = !st || (st->is_string() && st->str == "practice" && a && a->is_string() && a->str == "open");
                if (pr.value.obj.size() != (st ? 2u : 1u) || !a || !a->is_string() || (a->str != "open" && a->str != "close") || !step_ok)
                    return error_json(400, "bad_request", "alleen {\"action\":\"open\"} (eventueel met \"step\":\"practice\") of {\"action\":\"close\"}");
                return api_command("{\"v\":1,\"cmd\":\"calibrate_ui\",\"action\":\"" + a->str + "\"" +
                                   (st ? ",\"step\":\"practice\"" : "") + "}");
            }
            if (get) return wrong_method("POST");
            if (sub == "mag/start" || sub == "mag/stop" || sub == "mag/abort") {
                if (!build_set_request("calib_mag", r.body, {}, line, err, {"holder"}, sub.c_str() + 4)) return error_json(400, "bad_request", err);
                return api_command(line);
            }
            if (sub == "rec/start" || sub == "rec/mark" || sub == "rec/stop") {
                bool ok = sub == "rec/start" ? build_set_request("record", r.body, {"duration_s", "max_mb"}, line, err, {"tag"}, "start")
                          : sub == "rec/mark" ? build_set_request("record", r.body, {}, line, err, {"label"}, "mark")
                                              : build_set_request("record", r.body, {}, line, err, {}, "stop");
                if (!ok) return error_json(400, "bad_request", err);
                return api_command(line);
            }
            if (sub == "buttons") {
                if (!build_set_request("set_buttons", r.body, {}, line, err,
                                       {"trekker", "pomp", "driehoek", "cirkel", "kruis", "vierkant", "start", "select", "move", "ps"}))
                    return error_json(400, "bad_request", err);
                return api_command(line);
            }
            return error_json(404, "not_found", "onbekend eindpunt");
        }
        if (p == "/api/csrf") {
            if (!get) return wrong_method("GET");
            HttpResponse o;
            json::Writer w;
            w.begin_object().key("csrf").value(cfg_.csrf).end_object();
            o.body = w.str();
            return o;
        }
        if (p == "/api/stream") {
            if (!get) return wrong_method("GET");
            HttpResponse o;
            o.sse = true;
            o.sse_hz = stream_hz_from_query(r.query);
            return o;
        }
        if (p == "/api/recenter") {
            if (get) return wrong_method("POST");
            return api_command("{\"v\":1,\"cmd\":\"recenter\"}");
        }
        if (p == "/api/filter" || p == "/api/aim" || p == "/api/clock") {
            if (get) return wrong_method("POST");
            std::string line, err;
            bool ok = p == "/api/filter"
                          ? build_set_request("set_filter", r.body, {"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"}, line, err)
                          : p == "/api/clock" ? build_set_request("set_clock", r.body, {"clock_hz", "extrapolate_ms"}, line, err)
                                              : build_set_request("set_aim", r.body, {"h_edge_deg", "v_edge_deg", "predict_ms"}, line, err);
            if (!ok) return error_json(400, "bad_request", err);
            return api_command(line);
        }
        return error_json(404, "not_found", "onbekend eindpunt");
    }

    if (!get) return wrong_method("GET");
    std::string name = p == "/" ? "index.html" : p == "/calibrate" ? "calibrate.html" : p.substr(1);
    const Asset* a = find_asset(name);
    if (!a) return error_json(404, "not_found", "niet gevonden");
    HttpResponse o;
    o.content_type = a->content_type;
    o.body = a->data;
    if (name == "index.html" || name == "calibrate.html") {
        const std::string ph = "{{CSRF}}";
        size_t at = o.body.find(ph);
        if (at != std::string::npos) o.body.replace(at, ph.size(), cfg_.csrf);
    }
    return o;
}

std::string random_hex(size_t bytes) {
    std::string raw(bytes, '\0');
    size_t got = 0;
    while (got < bytes) {
        ssize_t n = ::getrandom(&raw[got], bytes - got, 0);
        if (n < 0) { if (errno == EINTR) continue; std::abort(); }  // geen zwak token als terugval
        got += static_cast<size_t>(n);
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : raw) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

bool constant_time_equal(std::string_view a, std::string_view b) {
    unsigned diff = static_cast<unsigned>(a.size() ^ b.size());
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) diff |= static_cast<unsigned>(static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    return diff == 0;
}

std::string default_token_path() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const char* h = std::getenv("HOME");
    std::string base = x && *x ? x : (h && *h ? std::string(h) + "/.config" : std::string());
    if (base.empty()) return {};
    return base + "/ravengun/web-token";
}

std::string load_or_create_token(const std::string& path, std::string& err) {
    namespace fs = std::filesystem;
    if (path.empty()) { err = "geen configuratiemap bekend (HOME/XDG_CONFIG_HOME)"; return {}; }
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    fs::create_directories(dir, ec);
    if (ec) { err = "kan " + dir.string() + " niet maken: " + ec.message(); return {}; }
    ::chmod(dir.c_str(), 0700);

    for (int attempt = 0; attempt < 2; ++attempt) {
        int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) {
            struct stat st{};
            if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::getuid()) {
                ::close(fd);
                err = path + " is geen eigen gewoon bestand";
                return {};
            }
            if (st.st_mode & 077) {
                ::close(fd);
                err = path + " is leesbaar voor anderen; zet de rechten op 0600 (chmod 600) of verwijder het bestand";
                return {};
            }
            char buf[256] = {0};
            ssize_t n = ::read(fd, buf, sizeof buf - 1);
            ::close(fd);
            std::string t = n > 0 ? trim(std::string_view(buf, static_cast<size_t>(n))) : std::string();
            while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
            if (t.size() < 32 || !all_hex(t)) { err = path + " bevat geen geldig token; verwijder het bestand om een nieuw te laten maken"; return {}; }
            return t;
        }
        if (errno != ENOENT) { err = "kan " + path + " niet lezen: " + std::strerror(errno); return {}; }
        fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            err = "kan " + path + " niet maken: " + std::strerror(errno);
            return {};
        }
        std::string t = random_hex(32);
        std::string line = t + "\n";
        ssize_t w = ::write(fd, line.data(), line.size());
        ::close(fd);
        if (w != static_cast<ssize_t>(line.size())) { err = "schrijven van " + path + " mislukt"; return {}; }
        return t;
    }
    err = "token-bestand niet te lezen of te maken";
    return {};
}

double stream_hz_from_query(const std::string& q) {
    size_t pos = 0;
    while (pos <= q.size()) {
        size_t amp = q.find('&', pos);
        std::string part = q.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (part.rfind("hz=", 0) == 0) {
            std::string v = part.substr(3);
            if (v.empty() || v.size() > 3) return kDefaultStreamHz;
            int n = 0;
            for (char c : v) { if (c < '0' || c > '9') return kDefaultStreamHz; n = n * 10 + (c - '0'); }
            return n >= 1 && n <= static_cast<int>(kMaxStreamHz) ? static_cast<double>(n) : kDefaultStreamHz;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return kDefaultStreamHz;
}

std::string sse_frame(std::string_view event, std::string_view data_json) {
    std::string s = "event: ";
    s += event;
    s += "\ndata: ";
    s += data_json;  // JSON bevat geen ruwe regeleinden
    s += "\n\n";
    return s;
}

SseBuffer::Push SseBuffer::push(std::string_view frame) {
    if (buf_.size() > skip_above_) { ++skipped_; return Push::Skipped; }
    if (buf_.size() + frame.size() > max_) return Push::Overflow;
    buf_.append(frame);
    return Push::Queued;
}

}  // namespace ravengun::web
