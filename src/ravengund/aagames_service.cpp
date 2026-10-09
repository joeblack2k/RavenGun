#include "aagames_service.hpp"

#include <chrono>

#include "output_service.hpp"
#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun::games;

AagamesService::AagamesService(SharedState& sh, AaConfig cfg) : sh_(sh), cfg_(std::move(cfg)) {
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
    const AaGame* g = cfg_.find(game_key);
    if (!g) return "onbekende game '" + game_key + "' (zie packaging/aagames-games.json)";
    std::lock_guard<std::mutex> lk(m_);
    if (client_) { client_->stop(); client_.reset(); }
    auto c = std::make_unique<AaClient>();
    SharedState& sh = sh_;
    // Alle spelers: de rumble komt uit de game zolang deze route actief is.
    sh.game_rumble_mask.store((1u << kPlayers) - 1u);
    c->start(*g, cfg_.port,
        [&sh](AaInputs& in) {
            if (!sh.output) return;
            for (int p = 1; p <= kAaPlayers; ++p) {
                OutputService::AimSnapshot s;
                auto& o = in[static_cast<size_t>(p - 1)];
                if (!sh.output->aim_snapshot(p, s) || !s.have_position) continue;
                o.active = true;
                o.u = s.u;
                o.v = s.v;
                o.trigger = s.trigger;
                o.reload = s.reload;
            }
        },
        [&sh](int player) { sh.game_recoil[static_cast<size_t>(player - 1)].fetch_add(1, std::memory_order_relaxed); });
    client_ = std::move(c);
    logf("info", "Unity-route: %s, klant naar 127.0.0.1:%d (%dx%d, %d Hz)", g->name.c_str(), cfg_.port, g->width, g->height, g->send_hz);
    return {};
}

void AagamesService::stop() {
    std::lock_guard<std::mutex> lk(m_);
    stop_locked();
}

void AagamesService::stop_locked() {
    if (!client_) return;
    client_->stop();
    client_.reset();
    sh_.game_rumble_mask.store(0);
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
    for (const auto& g : cfg_.games) w.value(g.key);
    w.end_array().end_object();
    return w.str();
}

}  // namespace ravengund
