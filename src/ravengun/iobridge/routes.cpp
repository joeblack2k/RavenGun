#include "ravengun/iobridge/routes.hpp"

#include <cctype>

#include "ravengun/output/key_table.hpp"

namespace ravengun::iobridge {

namespace gs = ravengun::gamesettings;

bool valid_route(const std::string& r) { return r == kRouteEvdev || r == kRouteTpShm || r == kRouteUnityTcp || r == kRouteHidBoard || r == kRouteJvs; }

const IoRoute* IoRoutes::find(const std::string& game) const {
    for (const auto& g : games)
        if (g.game == game) return &g;
    return nullptr;
}

std::string IoRoutes::route_for(const std::string& game) const {
    const IoRoute* r = find(game);
    return r ? r->route : default_route;
}

namespace {

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        size_t a = 0, b = cur.size();
        while (a < b && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
        if (b > a) out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (char c : s) {
        if (c == ',') flush();
        else cur += c;
    }
    flush();
    return out;
}

// Controle zoals voorheen: de combinatie moet kloppen voor de gekozen route.
bool validate_route(const IoRoute& r, std::string& err) {
    if (!valid_route(r.route)) { err = r.game + ": ongeldige Route '" + r.route + "' in [Input]"; return false; }
    if (r.route == kRouteTpShm && r.layout.empty()) { err = r.game + ": route tp-shm vraagt een Layout in [Input]"; return false; }
    if (r.route == kRouteHidBoard && r.board.empty() && r.boards.empty()) { err = r.game + ": route hid-board vraagt een Board of Boards in [Input]"; return false; }
    if (r.route == kRouteJvs && r.board.empty() && r.boards.empty()) { err = r.game + ": route jvs vraagt een Board of Boards in [Input]"; return false; }
    for (const auto& k : r.coin_keys)
        if (ravengun::output::key_code(k) < 0) { err = r.game + ": onbekende toets '" + k + "' in CoinKeys"; return false; }
    return true;
}

bool has_input(const gs::Game& g) {
    for (const char* k : {"Route", "Layout", "Board", "Boards"})
        if (g.settings.has("Input", k)) return true;
    return false;
}

IoRoute build(const gs::Game& g) {
    IoRoute r;
    r.game = g.profile;
    r.route = g.get("Input", "Route", kRouteEvdev);
    r.layout = g.get("Input", "Layout");
    r.board = g.get("Input", "Board");
    r.boards = split_list(g.get("Input", "Boards"));
    r.com_port = g.get("Input", "ComPort");
    r.note = g.get("Input", "Note");
    r.coin_keys = split_list(g.get("Input", "CoinKeys"));
    return r;
}

}  // namespace

IoRoute io_route_from_settings(const gs::Game& g, std::string& err) {
    err.clear();
    if (!has_input(g)) return {};
    IoRoute r = build(g);
    if (!validate_route(r, err)) return {};
    return r;
}

IoRoutes load_io_routes(const gs::Dirs& dirs, std::string& err) {
    IoRoutes out;
    err.clear();
    for (const std::string& name : gs::list_games(dirs)) {
        gs::Game g = gs::load_game(dirs, name);
        std::string e;
        IoRoute r = io_route_from_settings(g, e);
        if (!e.empty() && !dirs.user.empty()) {
            // De gebruikerslaag maakt het ongeldig: die telt dan niet mee.
            gs::Dirs shipped_only{dirs.shipped, {}};
            const std::string first = e;
            r = io_route_from_settings(gs::load_game(shipped_only, name), e);
            if (err.empty()) err = first + " (gebruikerslaag genegeerd)";
            if (!e.empty()) r = {};
        }
        if (!e.empty() && err.empty()) err = e;
        if (!r.game.empty()) out.games.push_back(std::move(r));
    }
    return out;
}

}  // namespace ravengun::iobridge
