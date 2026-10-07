#pragma once
// Beheerprotocol tussen ravengund en clients (ravengunctl, later de webinterface).
// Transport: Unix-stream-socket, één JSON-object per regel (UTF-8, afgesloten met '\n').
// Specificatie: docs/architecture.md, sectie "IPC".
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ravengun::ipc {

inline constexpr int kProtocolVersion = 1;
inline constexpr size_t kMaxLineBytes = 64 * 1024;
inline constexpr double kWatchRateMin = 1.0;
inline constexpr double kWatchRateMax = 250.0;

enum class Cmd { Status, Sample, Recenter, Watch, SetFilter, SetAim, Camera };
const char* to_string(Cmd c);

struct Request {
    Cmd cmd = Cmd::Status;
    std::string id_json;   // id van de client als JSON-tekst (getal of string), leeg = geen id
    bool full_ids = false; // status/sample/watch: controller-ID ongeredigeerd
    double rate_hz = 30;   // watch
    std::optional<bool> filter_enabled;
    std::optional<double> min_cutoff_hz, beta, d_cutoff_hz;
    std::optional<double> h_edge_deg, v_edge_deg;
};

struct Error {
    std::string code;     // parse_error, bad_request, unsupported_version, unknown_command,
                          // invalid_argument, line_too_long, unavailable, internal
    std::string message;  // Nederlands, voor mensen
    std::string id_json;  // indien uit het verzoek te halen
};

std::variant<Request, Error> parse_request(std::string_view line);

// Antwoorden (zonder afsluitende '\n').
std::string ok_response(const std::string& id_json, std::string_view result_json);
std::string error_response(const Error& e);
// Event-regel voor watch: {"v":1,"event":"<name>","data":<data_json>}
std::string event_line(std::string_view name, std::string_view data_json);

// Verzoek opbouwen (client-kant).
std::string make_request(const Request& r);

}  // namespace ravengun::ipc
