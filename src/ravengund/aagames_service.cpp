#include "aagames_service.hpp"

#include <chrono>

#include "iobridge_service.hpp"
#include "output_service.hpp"
#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun::games;

AagamesService::AagamesService(SharedState& sh, ravengun::launch::UnityPaths paths) : sh_(sh), paths_(std::move(paths)) {
    watch_ = std::thread(&AagamesService::watch_main, this);
}

AagamesService::~AagamesService() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (watch_.joinable()) watch_.join();
    stop();
}

// Bewaker: stopt de route als de game weg is (zie set_timeouts).
void AagamesService::watch_main() {
    std::unique_lock<std::mutex> lk(m_);
    while (!quit_) {
        cv_.wait_for(lk, std::chrono::milliseconds(500), [&] { return quit_; });
        if (quit_ || !client_) continue;
        const AaClientStats s = client_->stats();
        const int64_t now = AaClient::clock_ms();
        const bool gone = s.connects > 0 && !s.connected && s.last_disconnect_ms > 0 && now - s.last_disconnect_ms > grace_ms_;
        const bool never = s.connects == 0 && now - s.started_ms > never_ms_;
        if (gone || never) {
            logf("info", gone ? "Unity-route: de game is weg (verbinding verbroken), route stopt" : "Unity-route: de game verbond niet, route stopt");
            stop_locked();
        }
    }
}

std::string AagamesService::start(const std::string& game_key) {
    namespace L = ravengun::launch;
    const std::string text = L::unity_text(paths_, game_key);
    if (text.empty()) return "onbekende game '" + game_key + "' (geen patches/" + game_key + "/unity.json)";
    AaGame game;
    std::string perr;
    if (!parse_aagames_game(game_key, text, game, perr)) return perr;
    L::UnityRoute route;
    if (!L::load_unity_route(paths_, route, perr)) return perr;
    const AaGame* g = &game;
    std::lock_guard<std::mutex> lk(m_);
    if (client_) { client_->close(); client_.reset(); }
    SharedState& sh = sh_;
    // Alle spelers: de rumble komt uit de game zolang deze route actief is.
    sh.game_rumble_mask.store((1u << kPlayers) - 1u);
    // De toestand komt uit dezelfde bron als bij de andere dialecten van de IO-brug (OutputService::io_snapshot).
    auto c = std::make_unique<ravengun::iobridge::UnityTcpDialect>(
        *g, route.port,
        [&sh](ravengun::iobridge::IoState& s) { return sh.output && sh.output->io_snapshot(s); },
        [&sh](int player) { sh.game_recoil[static_cast<size_t>(player - 1)].fetch_add(1, std::memory_order_relaxed); });
    std::string oerr;
    c->open(oerr);
    client_ = std::move(c);
    if (sh_.iobridge) sh_.iobridge->set_external(ravengun::iobridge::kRouteUnityTcp, g->key);
    logf("info", "Unity-route: %s, klant naar 127.0.0.1:%d (%dx%d, %d Hz)", g->name.c_str(), route.port, g->width, g->height, g->send_hz);
    return {};
}

void AagamesService::stop() {
    std::lock_guard<std::mutex> lk(m_);
    stop_locked();
}

void AagamesService::stop_locked() {
    if (!client_) return;
    client_->close();
    client_.reset();
    sh_.game_rumble_mask.store(0);
    if (sh_.iobridge) sh_.iobridge->clear_external(ravengun::iobridge::kRouteUnityTcp);
    logf("info", "Unity-route gestopt");
}

bool AagamesService::active() const {
    std::lock_guard<std::mutex> lk(m_);
    return client_ != nullptr;
}

std::string AagamesService::status_json() const {
    namespace json = ravengun::json;
    std::lock_guard<std::mutex> lk(m_);
    if (client_) {
        std::string s = client_->status_json();   // begint met '{': "active" vooraan zetten
        return "{\"active\":true," + s.substr(1);
    }
    json::Writer w;
    w.begin_object().key("active").value(false).key("games").begin_array();
    for (const std::string& g : ravengun::launch::unity_games(paths_)) w.value(g);
    w.end_array().end_object();
    return w.str();
}

}  // namespace ravengund
