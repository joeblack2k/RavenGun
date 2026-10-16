// ravengunctl: controllers, pair, unpair, controller en led.
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

static bool parse_led(const std::string& arg, vision::Rgb& c) {
    int r = 0, g = 0, b = 0;
    char tail = 0;
    if (std::sscanf(arg.c_str(), "%d,%d,%d%c", &r, &g, &b, &tail) == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
        c = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        return true;
    }
    return vision::led_color_by_name(arg, c);
}

static std::string when_text(double unix_s) {
    if (!(unix_s > 0)) return "nooit";
    time_t t = static_cast<time_t>(unix_s);
    char b[64];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
    return b;
}

int cmd_controllers(bool json, bool full) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::Controllers;
    rq.full_ids = full;
    Res x = simple(rq, 8000);
    if (x.code != kExitOk) return x.code;
    if (json) { std::printf("%s\n", x.r.line.c_str()); return kExitOk; }
    const json::Value* list = path(*x.result(), {"controllers"});
    if (!list || !list->is_array() || list->arr.empty()) std::printf("Geen controllers bekend. Koppel er een met: ravengunctl pair\n");
    for (const auto& c : list ? list->arr : std::vector<json::Value>{}) {
        const bool move = str(c, {"type"}) == "move";
        std::string led = "-";
        if (const json::Value* rgb = path(c, {"led", "rgb"}); rgb && rgb->is_array() && rgb->arr.size() == 3) {
            char b[32];
            std::snprintf(b, sizeof b, "%.0f,%.0f,%.0f", rgb->arr[0].num, rgb->arr[1].num, rgb->arr[2].num);
            led = b;
        }
        const double pl = num(c, {"player"}, 0);
        std::printf("%-22s %-10s %-8s %-9s %-13s batterij %-16s laatst gezien %-16s LED %-11s %s %s\n",
                    str(c, {"name"}).c_str(), move ? "Move" : "Navigation", pl > 0 ? ("speler " + std::to_string(static_cast<int>(pl))).c_str() : "-",
                    boolean(c, {"connected"}) ? "verbonden" : "uit", boolean(c, {"removed"}) ? "(verwijderd)" : boolean(c, {"paired"}) ? "gekoppeld" : "niet gekoppeld",
                    str(c, {"battery", "text"}).c_str(), when_text(num(c, {"last_seen_unix"}, 0)).c_str(), led.c_str(),
                    str(c, {"address"}).c_str(), str(c, {"key"}).c_str());
    }
    const json::Value* h = path(*x.result(), {"helper"});
    if (h && !boolean(*h, {"installed"}))
        std::printf("\nLet op: de hulp voor het registreren bij Bluetooth is niet geïnstalleerd (nodig om te koppelen):\n  %s\n", str(*h, {"install_command"}).c_str());
    return kExitOk;
}

// Koppelen, stap voor stap (dezelfde toestandsmachine als de wizard in de app).
int cmd_pair(bool json, const std::string& action) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::Pair;
    if (action == "status" || action == "abort") {
        rq.action = action == "abort" ? "cancel" : "status";
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) std::printf("%s\n", x.r.line.c_str());
        else std::printf("%s: %s\n", str(*x.result(), {"state"}).c_str(), str(*x.result(), {"instruction"}).c_str());
        return kExitOk;
    }
    rq.action = "start";
    Res x = simple(rq);
    if (x.code != kExitOk) return x.code;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::string last_state, last_text;
    while (!g_stop.load()) {
        rq.action = "status";
        Res s = simple(rq);
        if (s.code != kExitOk) return s.code;
        const json::Value& r = *s.result();
        const std::string st = str(r, {"state"});
        const std::string text = str(r, {"instruction"});
        if (st != last_state || text != last_text) {
            if (json) std::printf("%s\n", s.r.line.c_str());
            else {
                std::printf("- %s\n", text.c_str());
                if (st == "need_helper") std::printf("    %s\n", str(r, {"helper", "install_command"}).c_str());
                if (const json::Value* m = path(r, {"message"}); m && m->is_string()) std::printf("  %s\n", m->str.c_str());
            }
            std::fflush(stdout);
            last_state = st;
            last_text = text;
        }
        if (st == "already_paired" || st == "need_helper") {
            if (json) { /* interactief alleen zonder --json */ }
            else {
                std::printf(st == "already_paired" ? "Opnieuw koppelen? [j/N] " : "Opnieuw proberen? [j/N] ");
                std::fflush(stdout);
                char ans[16] = {0};
                if (!std::fgets(ans, sizeof ans, stdin) || (ans[0] != 'j' && ans[0] != 'J' && ans[0] != 'y' && ans[0] != 'Y')) {
                    if (st == "already_paired") std::printf("Niet opnieuw: haal de kabel eruit en druk op de PS-knop; de wizard wacht daarop.\n");
                    else { rq.action = "cancel"; simple(rq); return kExitTempFail; }
                } else {
                    rq.action = "confirm";
                    simple(rq);
                }
                last_text.clear();
            }
        }
        if (st == "done") {
            if (!json) {
                const double pl = num(r, {"player"}, 0);
                if (pl > 0) std::printf("Gekoppeld als speler %.0f. Zie: ravengunctl controllers\n", pl);
            }
            return kExitOk;
        }
        if (st == "failed" || st == "cancelled" || st == "idle") return kExitTempFail;
        struct timespec ts{0, 300'000'000};
        nanosleep(&ts, nullptr);
    }
    rq.action = "cancel";
    simple(rq);
    std::printf("Afgebroken.\n");
    return kExitTempFail;
}

int cmd_unpair(bool json, const std::string& addr, bool forget) {
    ipc::Request rq;
    rq.cmd = ipc::Cmd::Unpair;
    rq.controller = addr;
    rq.keep_settings = !forget;
    Res x = simple(rq, 40000);
    if (x.code != kExitOk) return x.code;
    if (json) std::printf("%s\n", x.r.line.c_str());
    else std::printf("Verwijderd (%s); instellingen %s.\n", str(*x.result(), {"bluez"}).c_str(), boolean(*x.result(), {"settings_kept"}) ? "bewaard" : "gewist");
    return kExitOk;
}

int cmd_controller(bool json, const std::string& addr, const std::string& name, int player, const std::string& led, bool reset, bool blink) {
    if (name.empty() && player <= 0 && led.empty() && !reset && !blink) {
        std::fprintf(stderr, "ravengunctl: controller ADRES verwacht --name, --player, --led, --reset of --blink\n");
        return kExitUsage;
    }
    int code = kExitOk;
    if (!name.empty() || player > 0 || reset) {
        ipc::Request rq;
        rq.cmd = ipc::Cmd::SetController;
        rq.controller = addr;
        rq.name = name;
        if (player > 0) rq.new_player = player;
        rq.reset_settings = reset;
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) std::printf("%s\n", x.r.line.c_str());
        else {
            std::printf("%s: speler %.0f", str(*x.result(), {"name"}).c_str(), num(*x.result(), {"player"}, 0));
            if (const json::Value* sw = path(*x.result(), {"swapped_with"}); sw && sw->is_string()) std::printf(" (geruild met %s)", sw->str.c_str());
            std::printf("\n");
        }
    }
    if (!led.empty()) {
        vision::Rgb c{};
        if (!parse_led(led, c)) { std::fprintf(stderr, "ravengunctl: onbekende LED-kleur '%s'\n", led.c_str()); return kExitUsage; }
        ipc::Request rq;
        rq.cmd = ipc::Cmd::SetLed;
        rq.controller = addr;
        rq.led_r = c.r; rq.led_g = c.g; rq.led_b = c.b;
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) std::printf("%s\n", x.r.line.c_str());
        else std::printf("LED: %d,%d,%d (de camera zoekt die kleur voor deze speler)\n", c.r, c.g, c.b);
    }
    if (blink) {
        ipc::Request rq;
        rq.cmd = ipc::Cmd::Blink;
        rq.controller = addr;
        Res x = simple(rq);
        if (x.code != kExitOk) return x.code;
        if (json) std::printf("%s\n", x.r.line.c_str());
        else std::printf("De LED knippert 3 seconden wit.\n");
    }
    return code;
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

}  // namespace ravengunctl
