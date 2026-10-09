#include "ravengun/iobridge/routes.hpp"

#include "ravengun/json_parse.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

bool valid_route(const std::string& r) { return r == kRouteEvdev || r == kRouteTpShm || r == kRouteUnityTcp; }

const IoRoute* IoRoutes::find(const std::string& game) const {
    for (const auto& g : games)
        if (g.game == game) return &g;
    return nullptr;
}

std::string IoRoutes::route_for(const std::string& game) const {
    const IoRoute* r = find(game);
    return r ? r->route : default_route;
}

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
        r.route = rt->str;
        if (const json::Value* l = kv.second.find("layout"); l && l->is_string()) r.layout = l->str;
        if (const json::Value* n = kv.second.find("note"); n && n->is_string()) r.note = n->str;
        if (r.route == kRouteTpShm && r.layout.empty()) { err = "io-routes.json: " + kv.first + ": route tp-shm vraagt een 'layout'"; return {}; }
        out.games.push_back(std::move(r));
    }
    return out;
}

}  // namespace ravengun::iobridge
