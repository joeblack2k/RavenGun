#include "ravengun/games/readiness.hpp"

#include <algorithm>
#include <cstdlib>

#include "ravengun/json.hpp"

namespace ravengun::games::readiness {

namespace io = ravengun::iobridge;

int gun_players_of(const std::vector<std::string>& axis_signals) {
    auto has = [&](const std::string& s) { return std::find(axis_signals.begin(), axis_signals.end(), s) != axis_signals.end(); };
    int n = 0;
    for (int p = 1; p <= kPlayers; ++p) {
        const std::string pre = "p" + std::to_string(p) + ".";
        if (has(pre + "x") && has(pre + "y")) n = p;
    }
    return n;
}

InputInfo input_info(const io::IoRoute* route, const io::TpShmProfiles& profiles, const io::HidBoards& hid_boards,
                     const io::JvsBoards& jvs_boards, const std::string& gun_players_override) {
    InputInfo in;
    in.gun_players_override = gun_players_override;
    if (!route || route->game.empty()) return in;
    in.has_input = true;
    in.route = route->route;
    // Een Layout telt bij tp-shm en ook bij jvs (die schrijft dan ook die indeling, bv. het stuur van Let's Go Safari).
    if (!route->layout.empty() && (in.route == io::kRouteTpShm || in.route == io::kRouteJvs || in.route == io::kRouteEvdev)) {
        if (const io::TpShmProfile* p = profiles.find(route->layout))
            for (const auto& a : p->axes) in.axis_signals.push_back(a.signal);
        else if (in.route == io::kRouteTpShm)
            in.layout_known = false;
    }
    std::vector<std::string> boards = route->boards;
    if (boards.empty() && !route->board.empty()) boards.push_back(route->board);
    for (const std::string& name : boards) {
        if (in.route == io::kRouteJvs) {
            if (const io::JvsBoard* b = jvs_boards.find(name))
                for (const auto& a : b->analog) { if (!a.signal.empty()) in.axis_signals.push_back(a.signal); }
            else
                in.layout_known = false;
        } else if (in.route == io::kRouteHidBoard) {
            if (const io::HidBoard* b = hid_boards.find(name))
                for (const auto& a : b->axes) in.axis_signals.push_back(a.signal);
            else
                in.layout_known = false;
        }
    }
    return in;
}

GameKind classify(const InputInfo& in) {
    GameKind g;
    if (!in.gun_players_override.empty()) {
        char* end = nullptr;
        const long n = std::strtol(in.gun_players_override.c_str(), &end, 10);
        if (end && *end == 0 && n >= 0 && n <= kPlayers) {
            g.kind = n == 0 ? Kind::Pad : Kind::Lightgun;
            g.gun_players = static_cast<int>(n);
            g.source = "[Input] GunPlayers = " + std::to_string(n);
            return g;
        }
        // Een ongeldige waarde telt niet; afleiden zoals zonder.
    }
    if (!in.has_input) { g.source = "geen [Input]: een lightgun voor elke speler (zoals voorheen)"; return g; }
    if (in.route == io::kRouteEvdev || in.route == io::kRouteUnityTcp) {
        // De virtuele gun per speler (evdev) of de Unity-plugin: altijd een lightgun-game. Een Layout bij evdev (directe start met
        // tp-shm) vertelt wel hoeveel guns er zijn.
        const int n = gun_players_of(in.axis_signals);
        g.gun_players = n > 0 ? n : kPlayers;
        g.source = "route " + in.route;
        return g;
    }
    if (!in.layout_known) { g.source = "route " + in.route + ": indeling onbekend, een lightgun voor elke speler"; return g; }
    const int n = gun_players_of(in.axis_signals);
    if (n == 0) {
        g.kind = Kind::Pad;
        g.gun_players = 0;
        g.source = "route " + in.route + ": geen aim-assen (pN.x en pN.y) in de indeling";
        return g;
    }
    g.gun_players = n;
    g.source = "route " + in.route + ": aim-assen voor " + std::to_string(n) + (n == 1 ? " speler" : " spelers");
    return g;
}

Decision decide(const GameKind& g, const Inputs& in) {
    Decision d;
    d.game = g;
    if (g.kind == Kind::Pad) return d;   // geen Move nodig, geen kalibratie
    const int n = std::clamp(g.gun_players, 1, kPlayers);
    const PlayerInput& p1 = in.players[0];
    if (p1.move_assigned) {
        // De Move van speler 1 moet aan; een verbonden muis van speler 1 (de muismodus) telt ook.
        if (!p1.move_connected && !p1.mouse_connected) { d.ready = false; d.waiting.push_back(1); }
    } else {
        // Speler 1 heeft geen Move: elke lightgun of gamepad is genoeg (het oude gedrag).
        bool any_mouse = false;
        for (const auto& p : in.players) any_mouse = any_mouse || p.mouse_connected;
        if (!in.any_move_connected && !any_mouse && !in.any_pad_connected) { d.ready = false; d.waiting_any = true; }
    }
    for (int p = 1; p <= n; ++p) {
        const PlayerInput& pi = in.players[static_cast<size_t>(p - 1)];
        if (pi.move_connected) d.calibrate.push_back(p);
        else if (p > 1 && pi.move_assigned && !pi.mouse_connected) d.optional_off.push_back(p);
    }
    return d;
}

WaitStep wait_step(const Decision& d, int64_t started_ms, int64_t now_ms, int64_t limit_ms) {
    if (d.ready) return WaitStep::Ready;
    if (limit_ms > 0 && now_ms - started_ms >= limit_ms) return WaitStep::TimedOut;
    return WaitStep::Waiting;
}

std::string decision_json(const Decision& d, const Inputs& in) {
    json::Writer w;
    auto ints = [&w](const char* key, const std::vector<int>& v) {
        w.key(key).begin_array();
        for (int x : v) w.value(x);
        w.end_array();
    };
    w.begin_object();
    w.key("lightgun").value(d.game.kind == Kind::Lightgun);
    w.key("gun_players").value(d.game.gun_players);
    w.key("source").value(d.game.source);
    w.key("ready").value(d.ready);
    ints("waiting", d.waiting);
    w.key("waiting_any").value(d.waiting_any);
    ints("optional_off", d.optional_off);
    ints("calibrate", d.calibrate);
    w.key("players").begin_array();
    for (int p = 1; p <= kPlayers; ++p) {
        const PlayerInput& pi = in.players[static_cast<size_t>(p - 1)];
        w.begin_object().key("player").value(p);
        w.key("move_assigned").value(pi.move_assigned).key("move_connected").value(pi.move_connected);
        w.key("mouse").value(pi.mouse_assigned).key("mouse_connected").value(pi.mouse_connected);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

std::string waiting_text(const Decision& d) {
    if (d.ready) return {};
    if (d.waiting_any) return "Zet je PS Move aan (druk op de PS-knop).";
    std::string who;
    for (int p : d.waiting) who += (who.empty() ? "" : " en ") + std::to_string(p);
    return "Zet de PS Move van speler " + who + " aan (druk op de PS-knop).";
}

}  // namespace ravengun::games::readiness
