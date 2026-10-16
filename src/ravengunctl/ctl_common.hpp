#pragma once
// ravengunctl: wat de subcommando-bestanden (ctl_*.cpp) en main.cpp delen.
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <string>

#include "ravengun/ipc_client.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/model.hpp"

namespace ravengunctl {

extern std::atomic<bool> g_stop;
// --player N: de speler voor per-controller-opdrachten (status, watch, filter, rumble, led, ...); 0 = niet gegeven (speler 1).
extern int g_player;
void on_signal(int);

// JSON-toegang langs een pad van sleutels.
const ravengun::json::Value* path(const ravengun::json::Value& v, std::initializer_list<const char*> keys);
double num(const ravengun::json::Value& v, std::initializer_list<const char*> keys, double def = NAN);
std::string str(const ravengun::json::Value& v, std::initializer_list<const char*> keys, const char* def = "?");
bool boolean(const ravengun::json::Value& v, std::initializer_list<const char*> keys);

std::string ext_text(const ravengun::json::Value& s);
std::string names(const ravengun::json::Value* arr);
std::string edge_names(const ravengun::json::Value* obj);
std::string fmt(double v, const char* f);
double now_s();

struct Reply {
    int code = ravengun::kExitOk;
    std::string line;
    ravengun::json::Value v;
};
// Eén verzoek, één antwoord.
Reply call(ravengun::ipc::Client& c, const ravengun::ipc::Request& rq_in, int read_timeout_ms = 3000);

// Eén verzoek; geeft het "result"-object (of nullptr) en zet code.
struct Res {
    int code = ravengun::kExitOk;
    Reply r;
    const ravengun::json::Value* result() const { return path(r.v, {"result"}); }
};
Res simple(const ravengun::ipc::Request& rq, int read_timeout_ms = 3000);

// ctl_status.cpp
std::string rumble_text(const ravengun::json::Value& rm, bool counters = true);
std::string iobridge_text(const ravengun::json::Value& a);
std::string mouse_assignment_text(const ravengun::json::Value& a);
std::string pad_assignment_text(const ravengun::json::Value& a);
std::string aagames_text(const ravengun::json::Value& a);
void print_camera(const ravengun::json::Value& c);
void print_sample_line(const ravengun::json::Value& s, const ravengun::json::Value* edges);
void print_status(const ravengun::json::Value& r);
void print_output(const ravengun::json::Value& o, const std::string& what);

// ctl_record.cpp
void print_record_status(const ravengun::json::Value& v);
int cmd_record(bool json, const std::string& action, const std::string& tag, const std::string& mark, double duration, double max_mb);

// ctl_controllers.cpp
int cmd_controllers(bool json, bool full);
int cmd_pair(bool json, const std::string& action);
int cmd_unpair(bool json, const std::string& addr, bool forget);
int cmd_controller(bool json, const std::string& addr, const std::string& name, int player, const std::string& led, bool reset, bool blink);
int cmd_led(bool json, const std::string& arg);

// ctl_calibrate.cpp
int cmd_calibrate_ui(bool json, const std::string& action);
int cmd_calibrate_mag(bool json, const std::string& action, const std::string& holder, double duration);

}  // namespace ravengunctl
