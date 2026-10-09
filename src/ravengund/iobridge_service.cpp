#include "iobridge_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "aagames_service.hpp"
#include "output_service.hpp"
#include "ravengun/games/games.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/key_table.hpp"

namespace ravengund {

using namespace ravengun::iobridge;
namespace json = ravengun::json;

namespace {
int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

IoBridgeService::IoBridgeService(SharedState& sh, Config cfg) : sh_(sh), cfg_(std::move(cfg)) {
    th_ = std::thread(&IoBridgeService::main_loop, this);
}

void IoBridgeService::shutdown() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    std::lock_guard<std::mutex> lk(m_);
    if (dialect_) {   // neutraal bij het afsluiten; de route blijft in het toestandsbestand voor een herstart
        dialect_->neutral();
        dialect_->close();
        dialect_.reset();
    }
}

bool IoBridgeService::snapshot(IoState& s) const {
    s = {};
    return sh_.output && sh_.output->io_snapshot(s);
}

// Games die hun munt uit het toetsenbord lezen: zolang de munt van speler n ingedrukt is (toets 5), houdt de daemon ook de
// toetsen van coin_keys vast (op de poort van die speler). Aangeroepen met m_ vast.
void IoBridgeService::apply_coin_keys(const IoState& s) {
    if (coin_codes_.empty() || !sh_.output) return;
    for (int p = 0; p < kIoPlayers; ++p) {
        const bool now = s.sys.coin[static_cast<size_t>(p)];
        if (now == coin_prev_[static_cast<size_t>(p)]) continue;
        coin_prev_[static_cast<size_t>(p)] = now;
        if (now) for (int c : coin_codes_) sh_.output->key(p + 1, c, "down");
        else for (auto it = coin_codes_.rbegin(); it != coin_codes_.rend(); ++it) sh_.output->key(p + 1, *it, "up");
    }
}

void IoBridgeService::tick_once() {
    std::lock_guard<std::mutex> lk(m_);
    if (!dialect_) return;
    IoState s;
    snapshot(s);   // niet gewapend: s blijft neutraal
    dialect_->write(s);
    apply_coin_keys(s);
    ++ticks_;
}

void IoBridgeService::main_loop() {
    using namespace std::chrono;
    std::unique_lock<std::mutex> lk(m_);
    auto next = steady_clock::now();
    int64_t last_watch = 0;
    while (!quit_) {
        if (!dialect_) {
            cv_.wait_for(lk, milliseconds(200), [&] { return quit_ || dialect_ != nullptr; });
            next = steady_clock::now();
            continue;
        }
        IoState s;
        snapshot(s);
        dialect_->write(s);
        apply_coin_keys(s);
        ++ticks_;
        // Bewaking: de game is weg (de unit ravengun-tpui is leeg) of verscheen nooit.
        const int64_t now = steady_ms();
        if (!cfg_.tpui_procs.empty() && now - last_watch >= 500) {
            last_watch = now;
            const bool alive = ravengun::games::unit_alive(cfg_.tpui_procs);
            if (alive) { seen_alive_ = true; gone_since_ms_ = 0; }
            else if (seen_alive_) {
                if (gone_since_ms_ == 0) gone_since_ms_ = now;
                if (now - gone_since_ms_ > cfg_.grace_ms) {
                    logf("info", "IO-brug (%s): de game is weg, route stopt", route_.c_str());
                    stop_locked();
                    continue;
                }
            } else if (now - started_ms_ > cfg_.never_ms) {
                logf("info", "IO-brug (%s): de game verscheen niet, route stopt", route_.c_str());
                stop_locked();
                continue;
            }
        }
        next += microseconds(1'000'000 / std::max(1, cfg_.tick_hz));
        const auto n = steady_clock::now();
        if (next < n) next = n;   // achterstand niet inhalen
        cv_.wait_until(lk, next, [&] { return quit_; });
    }
}

void IoBridgeService::write_state_file(const std::string& route, const std::string& game) {
    if (cfg_.state_path.empty()) return;
    std::ofstream f(cfg_.state_path, std::ios::trunc);
    if (!f) return;
    json::Writer w;
    w.begin_object().key("route").value(route).key("game").value(game).end_object();
    f << w.str() << "\n";
}

std::string IoBridgeService::start(const std::string& game, const std::string& route_override) {
    std::string route = route_override.empty() ? cfg_.routes.route_for(game) : route_override;
    if (!valid_route(route)) return "onbekende route '" + route + "' (evdev, tp-shm of unity-tcp)";
    if (route == kRouteEvdev) {
        stop();
        if (sh_.aagames) sh_.aagames->stop();
        return {};
    }
    if (route == kRouteUnityTcp) {
        if (!sh_.aagames) return "de Unity-route is niet beschikbaar in deze daemon";
        stop();
        return sh_.aagames->start(game);
    }
    // tp-shm
    const IoRoute* r = cfg_.routes.find(game);
    if (!r || r->layout.empty()) return "game '" + game + "' heeft geen tp-shm-indeling in io-routes.json";
    const TpShmProfile* prof = cfg_.profiles.find(r->layout);
    if (!prof) return "tp-shm-indeling '" + r->layout + "' ontbreekt in tp-shm-profiles.json";
    auto d = std::make_unique<TpShmDialect>(*prof, game, cfg_.shm_path, cfg_.tpui_shm_path);
    std::string err;
    if (!d->open(err)) return err;
    std::lock_guard<std::mutex> lk(m_);
    if (dialect_) { dialect_->neutral(); dialect_->close(); }
    dialect_ = std::move(d);
    coin_codes_.clear();
    coin_prev_ = {};
    for (const auto& k : r->coin_keys) coin_codes_.push_back(ravengun::output::key_code(k));
    game_ = game;
    route_ = kRouteTpShm;
    seen_alive_ = false;
    gone_since_ms_ = 0;
    started_ms_ = steady_ms();
    write_state_file(route_, game_);
    logf("info", "IO-brug: tp-shm voor %s (indeling %s, %s)", game.c_str(), prof->name.c_str(), cfg_.shm_path.c_str());
    cv_.notify_all();
    return {};
}

bool IoBridgeService::resume() {
    if (cfg_.state_path.empty()) return false;
    std::ifstream f(cfg_.state_path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    auto pr = json::parse(ss.str());
    auto remove_state = [&] { ::unlink(cfg_.state_path.c_str()); };
    if (!pr.ok || !pr.value.is_object()) { remove_state(); return false; }
    const json::Value* rt = pr.value.find("route");
    const json::Value* g = pr.value.find("game");
    if (!rt || !g || !rt->is_string() || !g->is_string() || rt->str != kRouteTpShm) { remove_state(); return false; }
    if (!cfg_.tpui_procs.empty() && !ravengun::games::unit_alive(cfg_.tpui_procs)) {
        logf("info", "IO-brug: toestandsbestand voor %s, maar de game draait niet meer; niet hervat", g->str.c_str());
        remove_state();
        return false;
    }
    const std::string err = start(g->str, kRouteTpShm);
    if (!err.empty()) { logf("warn", "IO-brug: hervatten mislukt: %s", err.c_str()); return false; }
    {
        std::lock_guard<std::mutex> lk(m_);
        ++resumes_;
        seen_alive_ = true;
    }
    // De herstarte daemon begint ontwapend; de game draait nog en heeft zijn invoer nodig: opnieuw wapenen.
    if (sh_.output) {
        const OutputResult a = sh_.output->arm();
        if (!a.ok) logf("warn", "IO-brug: opnieuw wapenen na de herstart mislukt: %s", a.message.c_str());
    }
    logf("info", "IO-brug: tp-shm voor %s hervat na een herstart", g->str.c_str());
    return true;
}

void IoBridgeService::stop_locked() {
    if (!dialect_) return;
    apply_coin_keys(IoState{});   // vastgehouden munttoetsen loslaten
    dialect_->neutral();
    dialect_->close();
    if (route_ == kRouteTpShm) ::unlink(cfg_.shm_path.c_str());
    dialect_.reset();
    coin_codes_.clear();
    route_.clear();
    game_.clear();
    if (!cfg_.state_path.empty()) ::unlink(cfg_.state_path.c_str());
    logf("info", "IO-brug gestopt");
}

void IoBridgeService::stop() {
    std::lock_guard<std::mutex> lk(m_);
    stop_locked();
}

bool IoBridgeService::active() const {
    std::lock_guard<std::mutex> lk(m_);
    return dialect_ != nullptr || !ext_route_.empty();
}

std::string IoBridgeService::active_route() const {
    std::lock_guard<std::mutex> lk(m_);
    return dialect_ ? route_ : ext_route_;
}

void IoBridgeService::set_external(const std::string& route, const std::string& game) {
    std::lock_guard<std::mutex> lk(m_);
    ext_route_ = route;
    ext_game_ = game;
}

void IoBridgeService::clear_external(const std::string& route) {
    std::lock_guard<std::mutex> lk(m_);
    if (ext_route_ == route) { ext_route_.clear(); ext_game_.clear(); }
}

std::string IoBridgeService::status_json() const {
    std::lock_guard<std::mutex> lk(m_);
    json::Writer w;
    w.begin_object();
    const bool act = dialect_ != nullptr || !ext_route_.empty();
    w.key("active").value(act);
    w.key("route").value(dialect_ ? route_ : ext_route_.empty() ? std::string(kRouteEvdev) : ext_route_);
    w.key("game").value(dialect_ ? game_ : ext_game_);
    w.key("ticks").value(ticks_);
    w.key("resumes").value(resumes_);
    if (dialect_) {
        w.key("dialect").value(dialect_->name());
        w.key("detail").raw(dialect_->status_json());
    } else if (!ext_route_.empty()) {
        w.key("dialect").value(ext_route_);
    }
    w.key("routes").begin_object();
    w.key("default").value(cfg_.routes.default_route);
    for (const auto& g : cfg_.routes.games) w.key(g.game).value(g.route);
    w.end_object();
    w.end_object();
    return w.str();
}

}  // namespace ravengund
