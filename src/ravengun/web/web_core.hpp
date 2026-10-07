#pragma once
// Kern van de lokale webinterface: HTTP-parser, routing, beveiliging (Host/Origin/CSRF/token),
// SSE-buffer en rate-limiter. Geen sockets en geen daemon-toestand: alle commando's lopen via
// Backend::command, dezelfde opdrachtafhandeling als de IPC (zie src/ravengund/commands.hpp).
// Ontwerp en dreigingsmodel: docs/web-ui.md.
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ravengun::web {

inline constexpr size_t kMaxHeaderBytes = 8 * 1024;
inline constexpr size_t kMaxBodyBytes = 4 * 1024;
inline constexpr int kDefaultPort = 8765;

struct HttpRequest {
    std::string method;
    std::string path;   // zonder query
    std::string query;  // zonder '?'
    std::map<std::string, std::string> headers;  // namen in kleine letters
    std::string body;
    std::string header(const std::string& lower_name) const {
        auto it = headers.find(lower_name);
        return it == headers.end() ? std::string() : it->second;
    }
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    bool sse = false;  // de server opent hierna een Server-Sent-Events-stroom
};

// Serialiseert statusregel, vaste beveiligingsheaders, Content-Length en body.
std::string serialize(const HttpResponse& r);

enum class ParseState { Incomplete, Complete, Bad, TooLarge };
struct ParseResult {
    ParseState state = ParseState::Incomplete;
    size_t consumed = 0;
    int error_status = 400;
    HttpRequest request;
};
// Strikte, kleine HTTP/1.x-parser: geen chunked, geen dubbele Host/Content-Length.
ParseResult parse_http(std::string_view buf);

struct WebConfig {
    bool lan = false;           // bind op alle interfaces; vereist token
    int port = kDefaultPort;
    std::string token;          // LAN-token (hex); leeg bij localhost
    std::string csrf;           // per proces gegenereerd CSRF-token
    std::string hostname;       // alleen LAN: extra toegestane Host-naam (naast IP-adressen)
};
// Lege string = geldig; anders een Nederlandse foutmelding (bijv. LAN zonder token).
std::string validate(const WebConfig& c);

struct Backend {
    // Voert één IPC-verzoek (JSON-regel) uit en geeft het IPC-antwoord (JSON-object) terug.
    std::function<std::string(const std::string& request_line)> command;
};

class WebApp {
public:
    WebApp(WebConfig cfg, Backend b) : cfg_(std::move(cfg)), be_(std::move(b)) {}
    HttpResponse handle(const HttpRequest& r) const;
    const WebConfig& config() const { return cfg_; }

    bool host_allowed(const std::string& host) const;
    bool origin_allowed(const std::string& origin) const;
    bool token_ok(const HttpRequest& r) const;

private:
    HttpResponse route(const HttpRequest& r) const;
    HttpResponse api_command(const std::string& line) const;
    WebConfig cfg_;
    Backend be_;
};

// Willekeurige hex-string van 2*bytes tekens uit getrandom(2).
std::string random_hex(size_t bytes);
bool constant_time_equal(std::string_view a, std::string_view b);
// Leest of maakt het LAN-token (0600, map 0700). Bij een fout: leeg resultaat en err gevuld.
std::string load_or_create_token(const std::string& path, std::string& err);
std::string default_token_path();

// Ingebakken statische bestanden (gegenereerd door CMake uit src/ravengund/web/).
struct Asset { const char* path; const char* content_type; const char* data; };
const Asset* find_asset(std::string_view path);

// SSE-hulp.
std::string sse_frame(std::string_view event, std::string_view data_json);

// Uitgaande buffer van één SSE-client. push() blokkeert nooit en groeit nooit boven max_bytes:
// is er al meer dan skip_above wachtend, dan wordt het frame overgeslagen (trage client).
class SseBuffer {
public:
    enum class Push { Queued, Skipped, Overflow };
    explicit SseBuffer(size_t skip_above = 32 * 1024, size_t max_bytes = 256 * 1024)
        : skip_above_(skip_above), max_(max_bytes) {}
    Push push(std::string_view frame);
    const std::string& pending() const { return buf_; }
    void consume(size_t n) { buf_.erase(0, n < buf_.size() ? n : buf_.size()); }
    bool empty() const { return buf_.empty(); }
    uint64_t skipped() const { return skipped_; }
private:
    std::string buf_;
    size_t skip_above_, max_;
    uint64_t skipped_ = 0;
};

// Laat hoogstens één keer per periode door.
class Throttle {
public:
    explicit Throttle(double hz) : period_ns_(static_cast<int64_t>(1e9 / hz)) {}
    bool due(int64_t now_ns) {
        if (now_ns < next_) return false;
        next_ = now_ns + period_ns_;
        return true;
    }
    int64_t next() const { return next_; }
private:
    int64_t period_ns_;
    int64_t next_ = 0;
};

}  // namespace ravengun::web
