#include "ravengun/iobridge/routes.hpp"

#include <cctype>
#include <cstdlib>

#include "ravengun/json_parse.hpp"
#include "ravengun/output/key_table.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

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

// Leest de velden van één game uit `o` in `r` (alleen wat er staat). false + err bij een verkeerd type.
bool read_route_fields(const json::Value& o, const std::string& name, IoRoute& r, std::string& err) {
    if (const json::Value* rt = o.find("route")) {
        if (!rt->is_string() || !valid_route(rt->str)) { err = "io-routes.json: " + name + ": ongeldige 'route'"; return false; }
        r.route = rt->str;
    }
    if (const json::Value* l = o.find("layout"); l && l->is_string()) r.layout = l->str;
    if (const json::Value* l = o.find("board"); l && l->is_string()) r.board = l->str;
    if (const json::Value* n = o.find("note"); n && n->is_string()) r.note = n->str;
    if (const json::Value* n = o.find("com_port"); n && n->is_string()) r.com_port = n->str;
    if (const json::Value* bl = o.find("boards")) {
        if (!bl->is_array()) { err = "io-routes.json: " + name + ": 'boards' moet een lijst zijn"; return false; }
        r.boards.clear();
        for (const auto& b : bl->arr) {
            if (!b.is_string()) { err = "io-routes.json: " + name + ": 'boards' bevat een niet-tekst"; return false; }
            r.boards.push_back(b.str);
        }
    }
    if (const json::Value* ck = o.find("coin_keys")) {
        if (!ck->is_array()) { err = "io-routes.json: " + name + ": 'coin_keys' moet een lijst zijn"; return false; }
        r.coin_keys.clear();
        for (const auto& k : ck->arr) {
            if (!k.is_string() || ravengun::output::key_code(k.str) < 0) { err = "io-routes.json: " + name + ": onbekende toets in 'coin_keys'"; return false; }
            r.coin_keys.push_back(k.str);
        }
    }
    return true;
}

bool validate_route(const IoRoute& r, std::string& err) {
    if (!valid_route(r.route)) { err = "io-routes.json: " + r.game + ": ongeldige 'route'"; return false; }
    if (r.route == kRouteTpShm && r.layout.empty()) { err = "io-routes.json: " + r.game + ": route tp-shm vraagt een 'layout'"; return false; }
    if (r.route == kRouteHidBoard && r.board.empty()) { err = "io-routes.json: " + r.game + ": route hid-board vraagt een 'board'"; return false; }
    if (r.route == kRouteJvs && r.board.empty() && r.boards.empty()) { err = "io-routes.json: " + r.game + ": route jvs vraagt een 'board' of 'boards'"; return false; }
    return true;
}

}  // namespace

IoRoutes parse_io_routes(const std::string& text, std::string& err) {
    IoRoutes out;
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "io-routes.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return {};
    }
    if (const json::Value* d = pr.value.find("default")) {
        if (!d->is_string() || !valid_route(d->str)) { err = "io-routes.json: ongeldige 'default'"; return {}; }
        out.default_route = d->str;
    }
    const json::Value* games = pr.value.find("games");
    if (!games || !games->is_object()) { err = "io-routes.json: 'games' ontbreekt"; return {}; }
    for (const auto& kv : games->obj) {
        if (!kv.second.is_object()) { err = "io-routes.json: " + kv.first + " is geen object"; return {}; }
        IoRoute r;
        r.game = kv.first;
        const json::Value* rt = kv.second.find("route");
        if (!rt || !rt->is_string() || !valid_route(rt->str)) { err = "io-routes.json: " + kv.first + ": ongeldige 'route'"; return {}; }
        if (!read_route_fields(kv.second, kv.first, r, err)) return {};
        if (!validate_route(r, err)) return {};
        out.games.push_back(std::move(r));
    }
    return out;
}

bool apply_io_routes_override(IoRoutes& base, const std::string& text, std::string& err) {
    err.clear();
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i == text.size()) return true;
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "gebruikersbestand io-routes.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return false;
    }
    IoRoutes out = base;
    if (const json::Value* d = pr.value.find("default")) {
        if (!d->is_string() || !valid_route(d->str)) { err = "gebruikersbestand io-routes.json: ongeldige 'default'"; return false; }
        out.default_route = d->str;
    }
    if (const json::Value* games = pr.value.find("games")) {
        if (!games->is_object()) { err = "gebruikersbestand io-routes.json: 'games' moet een object zijn"; return false; }
        for (const auto& kv : games->obj) {
            if (!kv.second.is_object()) { err = "gebruikersbestand io-routes.json: " + kv.first + " is geen object"; return false; }
            IoRoute* r = nullptr;
            for (auto& g : out.games)
                if (g.game == kv.first) r = &g;
            if (!r) {
                IoRoute n;
                n.game = kv.first;
                n.route = out.default_route;
                out.games.push_back(std::move(n));
                r = &out.games.back();
            }
            if (!read_route_fields(kv.second, kv.first, *r, err)) return false;
            if (!validate_route(*r, err)) return false;
        }
    }
    base = std::move(out);
    return true;
}

std::string default_user_io_routes_path() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (x && *x == '/') return std::string(x) + "/ravengun/io-routes.json";
    const char* h = std::getenv("HOME");
    if (h && *h == '/') return std::string(h) + "/.config/ravengun/io-routes.json";
    return {};
}

}  // namespace ravengun::iobridge
