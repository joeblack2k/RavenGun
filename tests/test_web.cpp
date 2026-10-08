// Tests van de webkern: parser, routing, beveiliging, JSON-respons, token-opslag en SSE-throttling.
// Eigen CHECK-macro (actief bij NDEBUG), zoals de andere tests.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <mutex>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/kiosk/kiosk.hpp"
#include "ravengun/web/web_core.hpp"

using namespace ravengun;
using namespace ravengun::web;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static bool contains(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

static const char* kCsrf = "0123456789abcdef0123456789abcdef";
static const char* kToken = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

struct Fake {
    std::string last_line;
    int calls = 0;
    std::string reply = R"({"v":1,"ok":true,"result":{"hello":"wereld"}})";
};

static WebApp make_app(Fake& f, bool lan = false, int port = 8765) {
    WebConfig c;
    c.port = port;
    c.lan = lan;
    c.csrf = kCsrf;
    if (lan) { c.token = kToken; c.hostname = "steamdeck"; }
    Backend b;
    b.command = [&f](const std::string& line) { f.last_line = line; ++f.calls; return f.reply; };
    return WebApp(c, b);
}

static HttpRequest get(const std::string& path, const std::string& host = "127.0.0.1:8765") {
    HttpRequest r;
    r.method = "GET";
    r.path = path;
    r.headers["host"] = host;
    return r;
}

static HttpRequest post(const std::string& path, const std::string& body, bool csrf = true) {
    HttpRequest r = get(path);
    r.method = "POST";
    r.body = body;
    r.headers["content-type"] = "application/json";
    r.headers["origin"] = "http://127.0.0.1:8765";
    if (csrf) r.headers["x-csrf-token"] = kCsrf;
    return r;
}

static void test_parser() {
    auto ok = parse_http("GET /api/status?x=1 HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nX-Test:  waarde \r\n\r\n");
    CHECK(ok.state == ParseState::Complete);
    CHECK(ok.request.method == "GET" && ok.request.path == "/api/status" && ok.request.query == "x=1");
    CHECK(ok.request.header("x-test") == "waarde");

    CHECK(parse_http("GET /x HTTP/1.1\r\nHost: a\r\n").state == ParseState::Incomplete);
    auto body = parse_http("POST /api/filter HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\n\r\n{\"a\"");
    CHECK(body.state == ParseState::Incomplete);
    auto full = parse_http("POST /api/filter HTTP/1.1\r\nHost: a\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(full.state == ParseState::Complete && full.request.body == "{}" && full.consumed > 10);

    CHECK(parse_http("GET /x HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n").state == ParseState::Bad);
    CHECK(parse_http("POST /x HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n").state == ParseState::Bad);
    CHECK(parse_http("GET x HTTP/1.1\r\nHost: a\r\n\r\n").state == ParseState::Bad);
    CHECK(parse_http("GET /x HTTP/2\r\nHost: a\r\n\r\n").state == ParseState::Bad);
    CHECK(parse_http("GET /x HTTP/1.1\r\n Host: a\r\n\r\n").state == ParseState::Bad);
    CHECK(parse_http("POST /x HTTP/1.1\r\nHost: a\r\nContent-Length: 99999\r\n\r\n").state == ParseState::TooLarge);
    CHECK(parse_http(std::string(kMaxHeaderBytes + 10, 'a')).state == ParseState::TooLarge);
}

static void test_routing_and_json() {
    Fake f;
    WebApp app = make_app(f);

    HttpResponse r = app.handle(get("/api/status"));
    CHECK(r.status == 200 && f.calls == 1);
    CHECK(f.last_line == R"({"v":1,"cmd":"status"})");
    auto pr = json::parse(r.body);
    CHECK(pr.ok && pr.value.find("ok") && pr.value.find("ok")->b);
    CHECK(r.content_type.rfind("application/json", 0) == 0);
    app.handle(get("/api/camera"));
    CHECK(f.last_line == R"({"v":1,"cmd":"camera"})");
    app.handle(get("/api/sample"));
    CHECK(f.last_line == R"({"v":1,"cmd":"sample"})");

    r = app.handle(get("/api/csrf"));
    CHECK(contains(r.body, kCsrf));

    CHECK(app.handle(get("/api/bestaatniet")).status == 404);
    CHECK(app.handle(get("/geheim.txt")).status == 404);
    CHECK(app.handle(get("/api/recenter")).status == 405);
    HttpRequest put = get("/api/status");
    put.method = "PUT";
    CHECK(app.handle(put).status == 405);
    HttpRequest opt = get("/api/status");
    opt.method = "OPTIONS";  // geen CORS-preflight
    CHECK(app.handle(opt).status == 405);
    CHECK(app.handle(post("/api/status", "{}")).status == 405);

    // Statische pagina met ingevuld CSRF-token, zonder externe bronnen.
    r = app.handle(get("/"));
    CHECK(r.status == 200 && r.content_type.rfind("text/html", 0) == 0);
    CHECK(contains(r.body, kCsrf) && !contains(r.body, "{{CSRF}}"));
    CHECK(!contains(r.body, "http://") && !contains(r.body, "https://") && !contains(r.body, "//cdn"));
    CHECK(app.handle(get("/app.js")).status == 200);
    CHECK(app.handle(get("/app.css")).status == 200);
    CHECK(app.handle(get("/api/stream")).sse);
    CHECK(app.handle(get("/api/stream")).sse_hz == 30.0);
    {
        HttpRequest q = get("/api/stream");
        q.query = "hz=60";
        CHECK(app.handle(q).sse_hz == 60.0);
    }

    // Foutafbeelding: IPC-foutcodes worden HTTP-statussen.
    f.reply = R"({"v":1,"ok":false,"error":{"code":"unavailable","message":"geen Move"}})";
    r = app.handle(post("/api/recenter", "{}"));
    CHECK(r.status == 503 && contains(r.body, "geen Move"));
    f.reply = R"({"v":1,"ok":false,"error":{"code":"invalid_argument","message":"te groot"}})";
    CHECK(app.handle(post("/api/aim", R"({"h_edge_deg":500})")).status == 400);
    f.reply = "geen json";
    CHECK(app.handle(get("/api/status")).status == 500);
}

static void test_post_mapping() {
    Fake f;
    WebApp app = make_app(f);
    HttpResponse r = app.handle(post("/api/filter", R"({"enabled":true,"min_cutoff_hz":1.5,"beta":0.25})"));
    CHECK(r.status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_filter","enabled":true,"min_cutoff_hz":1.5,"beta":0.25})");
    r = app.handle(post("/api/aim", R"({"h_edge_deg":25,"v_edge_deg":14})"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"set_aim","h_edge_deg":25,"v_edge_deg":14})");
    r = app.handle(post("/api/aim", R"({"predict_ms":35})"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"set_aim","predict_ms":35})");
    r = app.handle(post("/api/clock", R"({"clock_hz":250,"extrapolate_ms":40})"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"set_clock","clock_hz":250,"extrapolate_ms":40})");
    CHECK(app.handle(post("/api/clock", R"({"hz":250})")).status == 400);
    CHECK(app.handle(get("/api/clock")).status == 405);
    r = app.handle(post("/api/rumble", R"({"enabled":true,"strength":200,"pulse_ms":80,"repeat_hz":0,"pump_strength":40})"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"set_rumble","enabled":true,"strength":200,"pulse_ms":80,"repeat_hz":0,"pump_strength":40})");
    r = app.handle(post("/api/rumble-test", R"({"ms":250})"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"rumble_test","ms":250})");
    r = app.handle(post("/api/rumble-test", "{}"));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"rumble_test"})");
    CHECK(app.handle(post("/api/rumble", R"({"volume":3})")).status == 400);
    CHECK(app.handle(get("/api/rumble")).status == 405);
    int calls = f.calls;
    // Geen injectie van cmd/v of onbekende velden; geen niet-scalaire waarden; geen rommel.
    CHECK(app.handle(post("/api/filter", R"({"cmd":"recenter"})")).status == 400);
    CHECK(app.handle(post("/api/filter", R"({"beta":"hoog"})")).status == 400);
    CHECK(app.handle(post("/api/aim", R"({"h_edge_deg":[1]})")).status == 400);
    CHECK(app.handle(post("/api/filter", "{kapot")).status == 400);
    CHECK(app.handle(post("/api/filter", "[1]")).status == 400);
    CHECK(f.calls == calls);  // niets bereikte de backend
    r = app.handle(post("/api/recenter", ""));
    CHECK(r.status == 200 && f.last_line == R"({"v":1,"cmd":"recenter"})");
}

static void test_new_endpoints() {
    Fake f;
    WebApp app = make_app(f);

    // GET: info, instellingen, uitvoerstatus en de diagnose-export (als download).
    CHECK(app.handle(get("/api/info")).status == 200 && f.last_line == R"({"v":1,"cmd":"info"})");
    CHECK(app.handle(get("/api/settings")).status == 200 && f.last_line == R"({"v":1,"cmd":"settings"})");
    CHECK(app.handle(get("/api/output")).status == 200 && f.last_line == R"({"v":1,"cmd":"output"})");
    HttpResponse d = app.handle(get("/api/diagnostics"));
    CHECK(d.status == 200 && f.last_line == R"({"v":1,"cmd":"diagnostics"})");
    bool disp = false;
    for (const auto& h : d.headers) if (h.first == "Content-Disposition" && contains(h.second, "attachment")) disp = true;
    CHECK(disp);
    CHECK(app.handle(post("/api/diagnostics", "{}")).status == 405);
    CHECK(app.handle(post("/api/info", "{}")).status == 405);

    // POST: getypeerde commando's met bewaakte velden.
    CHECK(app.handle(post("/api/led", R"({"preset":"cyaan"})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_led","preset":"cyaan"})");
    CHECK(app.handle(post("/api/led", R"({"r":1,"g":2,"b":3})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_led","r":1,"g":2,"b":3})");
    CHECK(app.handle(post("/api/camera", R"({"exposure":60,"gain":4,"auto_gain":false,"mode":"320x240@125"})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_camera","exposure":60,"gain":4,"auto_gain":false,"mode":"320x240@125"})");
    CHECK(app.handle(post("/api/web", R"({"port":18766})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_web","port":18766})");
    CHECK(app.handle(post("/api/arm", "{}")).status == 200 && f.last_line == R"({"v":1,"cmd":"arm"})");
    CHECK(app.handle(post("/api/disarm", "")).status == 200 && f.last_line == R"({"v":1,"cmd":"disarm"})");
    CHECK(app.handle(post("/api/test-pattern", R"({"pattern":"grid","dwell_ms":300})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"test_pattern","dwell_ms":300,"pattern":"grid"})" ||
          f.last_line == R"({"v":1,"cmd":"test_pattern","pattern":"grid","dwell_ms":300})");
    CHECK(app.handle(post("/api/output", R"({"invert_x":true,"scale_x":1.1})")).status == 200);
    CHECK(f.last_line == R"({"v":1,"cmd":"set_output","invert_x":true,"scale_x":1.1})");

    // Ongeldige of gevaarlijke bodies bereiken de backend niet: geen cmd-injectie, geen verkeerde typen,
    // geen paden of programma's als argument.
    int calls = f.calls;
    CHECK(app.handle(post("/api/led", R"({"preset":5})")).status == 400);
    CHECK(app.handle(post("/api/led", R"({"r":"255","g":0,"b":0})")).status == 400);
    CHECK(app.handle(post("/api/led", R"({"cmd":"recenter"})")).status == 400);
    CHECK(app.handle(post("/api/camera", R"({"device":"/dev/video0"})")).status == 400);
    CHECK(app.handle(post("/api/camera", R"({"mode":640})")).status == 400);
    CHECK(app.handle(post("/api/camera", R"({"exposure":"50"})")).status == 400);
    CHECK(app.handle(post("/api/web", R"({"lan":true})")).status == 400);  // LAN-modus nooit via de web
    CHECK(app.handle(post("/api/test-pattern", R"({"pattern":["a"]})")).status == 400);
    CHECK(app.handle(post("/api/test-pattern", R"({"command":"ls"})")).status == 400);
    CHECK(app.handle(post("/api/output", R"({"path":"/etc/passwd"})")).status == 400);
    CHECK(f.calls == calls);

    // Alle muterende endpoints: zonder CSRF, met vreemde Origin of cross-site 403; verkeerd content-type 415; GET 405.
    const char* writers[] = {"/api/led", "/api/camera", "/api/web", "/api/arm", "/api/disarm", "/api/test-pattern", "/api/output", "/api/recenter",
                             "/api/filter", "/api/aim"};
    for (const char* w : writers) {
        CHECK(app.handle(post(w, "{}", false)).status == 403);
        HttpRequest o = post(w, "{}");
        o.headers["origin"] = "http://evil.example";
        CHECK(app.handle(o).status == 403);
        HttpRequest x = post(w, "{}");
        x.headers["sec-fetch-site"] = "cross-site";
        CHECK(app.handle(x).status == 403);
        HttpRequest t = post(w, "{}");
        t.headers["content-type"] = "text/plain";
        CHECK(app.handle(t).status == 415);
        HttpRequest h = post(w, "{}");
        h.headers["host"] = "evil.example:8765";
        CHECK(app.handle(h).status == 403);
        if (std::string(w) != "/api/camera" && std::string(w) != "/api/output") CHECK(app.handle(get(w)).status == 405);
    }
    CHECK(f.calls == calls);

    // Foutafbeelding: zonder camera geeft set_camera een 503 met de melding.
    f.reply = R"X({"v":1,"ok":false,"error":{"code":"unavailable","message":"de camera staat uit (ravengund draait met --no-camera)"}})X";
    HttpResponse r = app.handle(post("/api/camera", R"({"exposure":60})"));
    CHECK(r.status == 503 && contains(r.body, "--no-camera"));
    f.reply = R"({"v":1,"ok":false,"error":{"code":"invalid_argument","message":"'exposure' moet tussen 0 en 255 liggen"}})";
    CHECK(app.handle(post("/api/camera", R"({"exposure":300})")).status == 400);

    // LAN: ook de nieuwe endpoints vragen om een token.
    Fake fl;
    WebApp lan = make_app(fl, true);
    const std::string host = "192.168.1.5:8765";
    for (const char* g : {"/api/info", "/api/settings", "/api/output", "/api/diagnostics"}) CHECK(lan.handle(get(g, host)).status == 401);
    HttpRequest lp = post("/api/led", R"({"preset":"rood"})");
    lp.headers["host"] = host;
    lp.headers["origin"] = "http://" + host;
    CHECK(lan.handle(lp).status == 401);
    lp.headers["authorization"] = "Bearer " + std::string(kToken);
    CHECK(lan.handle(lp).status == 200);
    CHECK(fl.calls == 1);
}

static void test_security_local() {
    Fake f;
    WebApp app = make_app(f);

    // Host
    CHECK(app.handle(get("/api/status", "evil.example:8765")).status == 403);
    CHECK(app.handle(get("/api/status", "127.0.0.1:9999")).status == 403);
    CHECK(app.handle(get("/api/status", "192.168.1.5:8765")).status == 403);  // IP-literal alleen in LAN-modus
    CHECK(app.handle(get("/api/status", "")).status == 403);
    CHECK(app.handle(get("/api/status", "localhost:8765")).status == 200);
    CHECK(app.handle(get("/api/status", "[::1]:8765")).status == 200);
    CHECK(app.handle(get("/", "evil.example:8765")).status == 403);
    CHECK(f.calls == 2);

    // Origin
    HttpRequest p = post("/api/recenter", "{}");
    p.headers["origin"] = "http://evil.example";
    CHECK(app.handle(p).status == 403);
    p.headers["origin"] = "null";
    CHECK(app.handle(p).status == 403);
    p.headers["origin"] = "https://127.0.0.1:8765";
    CHECK(app.handle(p).status == 403);
    p.headers["origin"] = "http://127.0.0.1:8765";
    CHECK(app.handle(p).status == 200);
    p.headers.erase("origin");  // curl: geen Origin, maar wel CSRF-token
    CHECK(app.handle(p).status == 200);
    p.headers["sec-fetch-site"] = "cross-site";
    CHECK(app.handle(p).status == 403);
    p.headers["sec-fetch-site"] = "same-origin";
    CHECK(app.handle(p).status == 200);

    // CSRF
    int calls = f.calls;
    CHECK(app.handle(post("/api/recenter", "{}", false)).status == 403);
    HttpRequest q = post("/api/recenter", "{}");
    q.headers["x-csrf-token"] = "fout";
    CHECK(app.handle(q).status == 403);
    q.headers["x-csrf-token"] = "";
    CHECK(app.handle(q).status == 403);
    CHECK(f.calls == calls);

    // Content-Type
    HttpRequest t = post("/api/recenter", "{}");
    t.headers["content-type"] = "text/plain";
    CHECK(app.handle(t).status == 415);
    t.headers["content-type"] = "application/x-www-form-urlencoded";
    CHECK(app.handle(t).status == 415);
    CHECK(f.calls == calls);

    // Een token in de URL doet in localhost-modus niets; geen CORS-headers in welk antwoord dan ook.
    for (const HttpRequest& rq : {get("/api/status"), get("/api/nope", "evil:1"), post("/api/recenter", "{}", false), get("/")}) {
        std::string wire = serialize(app.handle(rq));
        CHECK(!contains(wire, "Access-Control-"));
        CHECK(contains(wire, "X-Content-Type-Options: nosniff") && contains(wire, "frame-ancestors 'none'"));
        CHECK(contains(wire, "Cache-Control: no-store"));
    }
}

static void test_security_lan() {
    WebConfig bad;
    bad.lan = true;
    bad.csrf = kCsrf;
    CHECK(!validate(bad).empty());  // LAN zonder token: geweigerd
    bad.token = "kort";
    CHECK(!validate(bad).empty());
    bad.token = kToken;
    CHECK(validate(bad).empty());
    WebConfig loc;
    loc.csrf = kCsrf;
    CHECK(validate(loc).empty());
    loc.port = 0;
    CHECK(!validate(loc).empty());

    Fake f;
    WebApp app = make_app(f, true);
    const std::string lan_host = "192.168.1.5:8765";
    CHECK(app.handle(get("/api/status", lan_host)).status == 401);
    CHECK(app.handle(get("/", lan_host)).status == 401);
    CHECK(f.calls == 0);
    HttpRequest a = get("/api/status", lan_host);
    a.headers["authorization"] = "Bearer " + std::string(kToken);
    CHECK(app.handle(a).status == 200);
    a.headers["authorization"] = "Bearer fout";
    CHECK(app.handle(a).status == 401);
    HttpRequest h = get("/api/status", lan_host);
    h.headers["x-ravengun-token"] = kToken;
    CHECK(app.handle(h).status == 200);
    HttpRequest ck = get("/api/status", "steamdeck.local:8765");
    ck.headers["cookie"] = std::string("x=1; rg_token=") + kToken;
    CHECK(app.handle(ck).status == 200);
    CHECK(app.handle(get("/api/status", "evil.example:8765")).status == 403);  // Host eerst
    HttpRequest wt = get("/api/status", "evil.example:8765");
    wt.headers["authorization"] = "Bearer " + std::string(kToken);
    CHECK(app.handle(wt).status == 403);

    // Aanmelden via /?token=...: cookie en doorsturen.
    HttpRequest login = get("/", lan_host);
    login.query = std::string("token=") + kToken;
    HttpResponse r = app.handle(login);
    std::string wire = serialize(r);
    CHECK(r.status == 303 && contains(wire, "HttpOnly") && contains(wire, "SameSite=Strict") && contains(wire, "Location: /"));
    login.query = "token=aaaa";
    CHECK(app.handle(login).status == 401);

    // POST in LAN-modus: token én CSRF én Origin.
    HttpRequest p = post("/api/recenter", "{}");
    p.headers["host"] = lan_host;
    p.headers["origin"] = "http://" + lan_host;
    CHECK(app.handle(p).status == 401);
    p.headers["authorization"] = "Bearer " + std::string(kToken);
    CHECK(app.handle(p).status == 200);
    p.headers["origin"] = "http://evil.example";
    CHECK(app.handle(p).status == 403);
    p.headers["origin"] = "http://" + lan_host;
    p.headers.erase("x-csrf-token");
    CHECK(app.handle(p).status == 403);
}

static void test_token_file() {
    fs::path d = fs::temp_directory_path() / ("rg_web_test_" + std::to_string(::getpid()));
    fs::remove_all(d);
    std::string path = (d / "cfg" / "web-token").string();
    std::string err;
    std::string t = load_or_create_token(path, err);
    CHECK(err.empty() && t.size() == 64);
    struct stat st{};
    CHECK(::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(::stat((d / "cfg").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(load_or_create_token(path, err) == t);  // blijft gelijk
    ::chmod(path.c_str(), 0644);
    CHECK(load_or_create_token(path, err).empty() && !err.empty());
    ::chmod(path.c_str(), 0600);
    { std::ofstream o(path, std::ios::trunc); o << "geen-hex-token\n"; }
    err.clear();
    CHECK(load_or_create_token(path, err).empty() && !err.empty());
    fs::remove_all(d);
    CHECK(random_hex(16) != random_hex(16));
    CHECK(constant_time_equal("abc", "abc") && !constant_time_equal("abc", "abd") && !constant_time_equal("abc", "ab"));
}

static void test_sse() {
    CHECK(sse_frame("sample", "{\"a\":1}") == "event: sample\ndata: {\"a\":1}\n\n");

    // Streamsnelheid per client: 1..60 geheel, anders de standaard 30.
    CHECK(stream_hz_from_query("") == 30.0 && stream_hz_from_query("hz=60") == 60.0 && stream_hz_from_query("x=1&hz=45") == 45.0);
    CHECK(stream_hz_from_query("hz=61") == 30.0 && stream_hz_from_query("hz=0") == 30.0 && stream_hz_from_query("hz=abc") == 30.0);
    CHECK(stream_hz_from_query("hz=") == 30.0 && stream_hz_from_query("hz=-5") == 30.0 && stream_hz_from_query("hz=1000") == 30.0);
    {
        Throttle t60(60.0);
        int n60 = 0;
        for (int64_t t = 0; t < 1'000'000'000; t += 1'000'000) if (t60.due(t)) ++n60;
        CHECK(n60 >= 59 && n60 <= 61);
    }

    // Throttle: op 1 kHz aangeboden, hoogstens ~30 per seconde doorgelaten.
    Throttle th(30.0);
    int passed = 0;
    for (int64_t t = 0; t < 1'000'000'000; t += 1'000'000) if (th.due(t)) ++passed;
    CHECK(passed >= 29 && passed <= 31);

    // Trage consumer: de buffer blijft begrensd en push blokkeert nooit.
    SseBuffer buf(32 * 1024, 256 * 1024);
    std::string frame(1000, 'x');
    auto t0 = std::chrono::steady_clock::now();
    size_t queued = 0, skipped = 0;
    for (int i = 0; i < 1'000'000; ++i) {
        auto p = buf.push(frame);
        if (p == SseBuffer::Push::Queued) ++queued;
        else if (p == SseBuffer::Push::Skipped) ++skipped;
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(buf.pending().size() <= 33 * 1024 + 1000);
    CHECK(queued <= 40 && skipped > 999'000 && buf.skipped() == skipped);
    CHECK(dt < 2.0);
    buf.consume(buf.pending().size());
    CHECK(buf.empty() && buf.push(frame) == SseBuffer::Push::Queued);  // herstelt na afname
    SseBuffer tiny(10, 100);
    CHECK(tiny.push(std::string(200, 'x')) == SseBuffer::Push::Overflow);

    // Producer-thread (stand-in voor HID/tracker) ongehinderd terwijl een webthread met een
    // trage consumer op 30 Hz leest: de producer mag niet merkbaar vertragen.
    std::mutex m;
    uint64_t counter = 0;
    std::atomic<bool> stop{false};
    std::thread web([&] {
        SseBuffer b;
        Throttle th2(30.0);
        while (!stop.load()) {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            if (th2.due(ns)) {
                uint64_t c;
                { std::lock_guard<std::mutex> g(m); c = counter; }
                b.push(sse_frame("sample", std::to_string(c)));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));  // trage afnemer
        }
    });
    auto p0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 300'000; ++i) { std::lock_guard<std::mutex> g(m); ++counter; }
    double pd = std::chrono::duration<double>(std::chrono::steady_clock::now() - p0).count();
    stop = true;
    web.join();
    CHECK(pd < 1.0);
    CHECK(counter == 300'000);
}

static void test_calibrate_ui() {
    Fake f;
    WebApp app = make_app(f);

    // De pagina zelf: ingebakken, met CSRF-token, zonder externe bronnen of inline code (CSP).
    HttpResponse r = app.handle(get("/calibrate"));
    CHECK(r.status == 200 && r.content_type.rfind("text/html", 0) == 0);
    CHECK(contains(r.body, kCsrf) && !contains(r.body, "{{CSRF}}"));
    CHECK(!contains(r.body, "http://") && !contains(r.body, "https://") && !contains(r.body, " style=") && !contains(r.body, " onclick="));
    CHECK(contains(r.body, "/calibrate.js") && contains(r.body, "/calibrate-core.js") && contains(r.body, "/calibrate.css"));
    for (const char* a : {"/calibrate.js", "/calibrate-core.js", "/calibrate.css"}) {
        HttpResponse x = app.handle(get(a));
        CHECK(x.status == 200 && !x.body.empty() && !contains(x.body, "//cdn") && !contains(x.body, "https://"));
    }
    std::string ser = serialize(r);
    CHECK(contains(ser, "Content-Security-Policy: default-src 'none'; script-src 'self'") && contains(ser, "Cache-Control: no-store"));
    CHECK(app.handle(post("/calibrate", "{}")).status == 405);
    CHECK(app.handle(get("/calibrate", "evil.example:8765")).status == 403);

    // Endpoints: vaste omzetting naar IPC-commando's.
    struct E { const char* path; const char* body; const char* line; };
    const E ok[] = {
        {"/api/calibrate/mag/start", R"({"holder":"sharp_shooter"})", R"({"v":1,"cmd":"calib_mag","action":"start","holder":"sharp_shooter"})"},
        {"/api/calibrate/mag/start", "{}", R"({"v":1,"cmd":"calib_mag","action":"start"})"},
        {"/api/calibrate/mag/stop", "{}", R"({"v":1,"cmd":"calib_mag","action":"stop"})"},
        {"/api/calibrate/mag/abort", "{}", R"({"v":1,"cmd":"calib_mag","action":"abort"})"},
        {"/api/calibrate/rec/start", R"({"tag":"stil-herhaalbaarheid","duration_s":900})", R"({"v":1,"cmd":"record","action":"start","tag":"stil-herhaalbaarheid","duration_s":900})"},
        {"/api/calibrate/rec/mark", R"({"label":"stil_midden_1"})", R"({"v":1,"cmd":"record","action":"mark","label":"stil_midden_1"})"},
        {"/api/calibrate/rec/stop", "{}", R"({"v":1,"cmd":"record","action":"stop"})"},
        {"/api/calibrate/buttons", R"({"trekker":"t","pomp":"move"})", R"({"v":1,"cmd":"set_buttons","trekker":"t","pomp":"move"})"},
        {"/api/calibrate/ui", R"({"action":"open"})", R"({"v":1,"cmd":"calibrate_ui","action":"open"})"},
        {"/api/calibrate/ui", R"({"action":"close"})", R"({"v":1,"cmd":"calibrate_ui","action":"close"})"},
        {"/api/calibrate/ui", R"({"action":"open","step":"practice"})", R"({"v":1,"cmd":"calibrate_ui","action":"open","step":"practice"})"},
    };
    for (const E& e : ok) {
        HttpResponse x = app.handle(post(e.path, e.body));
        CHECK(x.status == 200);
        if (f.last_line != e.line) std::fprintf(stderr, "want %s\n got %s\n", e.line, f.last_line.c_str());
        CHECK(f.last_line == e.line);
    }
    CHECK(app.handle(get("/api/calibrate/ui")).status == 200 && f.last_line == R"({"v":1,"cmd":"calibrate_ui","action":"status"})");

    // Ongeldige invoer bereikt de backend nooit.
    int calls = f.calls;
    const E bad[] = {
        {"/api/calibrate/mag/start", R"({"cmd":"recenter"})", ""},
        {"/api/calibrate/mag/start", R"({"holder":5})", ""},
        {"/api/calibrate/mag/stop", R"({"action":"abort"})", ""},
        {"/api/calibrate/rec/mark", R"({"label":5})", ""},
        {"/api/calibrate/rec/mark", R"({"path":"/tmp/x"})", ""},
        {"/api/calibrate/rec/start", R"({"duration_s":"lang"})", ""},
        {"/api/calibrate/buttons", R"({"laser":"t"})", ""},
        {"/api/calibrate/buttons", R"({"trekker":["t"]})", ""},
        {"/api/calibrate/buttons", "[1]", ""},
        {"/api/calibrate/ui", R"({"action":"verwijder"})", ""},
        {"/api/calibrate/ui", R"({"action":"open","url":"http://evil.example"})", ""},
        {"/api/calibrate/ui", R"({"action":"status"})", ""},
        {"/api/calibrate/ui", R"({"action":"open","step":"mag"})", ""},
        {"/api/calibrate/ui", R"({"action":"close","step":"practice"})", ""},
        {"/api/calibrate/ui", R"({"step":"practice"})", ""},
        {"/api/calibrate/ui", "{kapot", ""},
    };
    for (const E& e : bad) CHECK(app.handle(post(e.path, e.body)).status == 400);
    CHECK(app.handle(post("/api/calibrate/onbekend", "{}")).status == 404);
    CHECK(f.calls == calls);

    // Beveiliging van elk POST-endpoint: CSRF, Origin, Sec-Fetch-Site, Content-Type en Host.
    const char* posts[] = {"/api/calibrate/mag/start", "/api/calibrate/mag/stop", "/api/calibrate/mag/abort", "/api/calibrate/rec/start",
                           "/api/calibrate/rec/mark", "/api/calibrate/rec/stop", "/api/calibrate/buttons", "/api/calibrate/ui"};
    for (const char* p : posts) {
        CHECK(app.handle(post(p, R"({"label":"x"})", false)).status == 403);  // zonder CSRF-token
        HttpRequest o = post(p, "{}");
        o.headers["origin"] = "http://evil.example";
        CHECK(app.handle(o).status == 403);
        HttpRequest cs = post(p, "{}");
        cs.headers["sec-fetch-site"] = "cross-site";
        CHECK(app.handle(cs).status == 403);
        HttpRequest ct = post(p, "{}");
        ct.headers["content-type"] = "text/plain";
        CHECK(app.handle(ct).status == 415);
        HttpRequest h = post(p, "{}");
        h.headers["host"] = "evil.example:8765";
        CHECK(app.handle(h).status == 403);
        HttpRequest wc = post(p, "{}");
        wc.headers["x-csrf-token"] = "00000000000000000000000000000000";
        CHECK(app.handle(wc).status == 403);
        if (std::string(p) != "/api/calibrate/ui") CHECK(app.handle(get(p)).status == 405);
    }
    CHECK(f.calls == calls);

    // LAN: zonder token 401, ook voor de pagina en de nieuwe endpoints.
    Fake fl;
    WebApp lan = make_app(fl, true);
    const std::string host = "192.168.1.5:8765";
    CHECK(lan.handle(get("/calibrate", host)).status == 401);
    HttpRequest lp = post("/api/calibrate/mag/start", "{}");
    lp.headers["host"] = host;
    lp.headers["origin"] = "http://" + host;
    CHECK(lan.handle(lp).status == 401 && fl.calls == 0);
    lp.headers["authorization"] = "Bearer " + std::string(kToken);
    CHECK(lan.handle(lp).status == 200 && fl.calls == 1);
}

static void test_kiosk_helpers() {
    using namespace ravengun::kiosk;
    const std::string prof = "/home/deck/.local/state/ravengun/kiosk";
    auto args = flatpak_args("http://127.0.0.1:8765/calibrate", prof);
    CHECK(args[0] == "run" && args[1] == "--filesystem=" + prof && args[2] == kBrowserAppId);
    bool dd = false, kiosk = false, app = false;
    for (const auto& a : args) {
        if (a == "--user-data-dir=" + prof) dd = true;
        if (a == "--kiosk") kiosk = true;
        if (a == "--app=http://127.0.0.1:8765/calibrate") app = true;
    }
    CHECK(dd && kiosk && app);
    // Sluiten raakt alleen processen met precies dit profiel.
    auto nul = [](std::initializer_list<std::string> v) { std::string s; for (const auto& a : v) { s += a; s += '\0'; } return s; };
    CHECK(cmdline_is_kiosk(nul({"flatpak", "run", "com.brave.Browser", "--user-data-dir=" + prof}), prof));
    CHECK(cmdline_is_kiosk(nul({"/app/brave/brave", "--user-data-dir=" + prof, "--type=renderer"}), prof));
    CHECK(!cmdline_is_kiosk(nul({"/app/brave/brave", "--user-data-dir=/home/deck/.var/app/com.brave.Browser/config"}), prof));
    CHECK(!cmdline_is_kiosk(nul({"/app/brave/brave", "--user-data-dir=" + prof + "2"}), prof));
    CHECK(!cmdline_is_kiosk(nul({"vim", prof}), prof));
    CHECK(!cmdline_is_kiosk(nul({"brave"}), prof) && !cmdline_is_kiosk("", prof) && !cmdline_is_kiosk(nul({"x"}), ""));
    CHECK(xauthority_from_cmdline(nul({"kwin_wayland", "--xwayland", "--xwayland-xauthority", "/run/user/1000/xauth_abc", "--other"})) == "/run/user/1000/xauth_abc");
    CHECK(xauthority_from_cmdline(nul({"kwin_wayland", "--xwayland-xauthority"})).empty());
    CHECK(xauthority_from_cmdline("").empty());
}

int main() {
    test_calibrate_ui();
    test_kiosk_helpers();
    test_parser();
    test_routing_and_json();
    test_post_mapping();
    test_new_endpoints();
    test_security_local();
    test_security_lan();
    test_token_file();
    test_sse();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
