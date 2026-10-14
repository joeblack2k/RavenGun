#include "iobridge_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "aagames_service.hpp"
#include "gamepad_service.hpp"
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

// Route jvs met een Layout in [Input]: het JVS-bord EN de tp-shm-indeling. Gemeten bij Let's Go Safari: de game vraagt nooit JVS
// 0x22 (analoog); het stuur komt alleen uit het gedeelde geheugen. Naar buiten is dit het jvs-dialect; de status krijgt "shm" erbij.
class JvsWithShmDialect final : public IoDialect {
public:
    JvsWithShmDialect(std::unique_ptr<IoDialect> jvs, std::unique_ptr<TpShmDialect> shm) : jvs_(std::move(jvs)), shm_(std::move(shm)) {}
    ~JvsWithShmDialect() override { close(); }
    const char* name() const override { return jvs_->name(); }
    std::string game() const override { return jvs_->game(); }
    bool open(std::string& err) override {
        if (!shm_->open(err)) return false;
        if (!jvs_->open(err)) { shm_->close(); return false; }
        return true;
    }
    void write(const IoState& s) override { jvs_->write(s); shm_->write(s); }
    void neutral() override { jvs_->neutral(); shm_->neutral(); }
    void close() override { jvs_->close(); shm_->close(); }
    IoOutputs outputs() const override { return jvs_->outputs(); }
    std::string status_json() const override {
        std::string j = jvs_->status_json();
        if (!j.empty() && j.back() == '}') {
            j.pop_back();
            j += std::string(j.size() > 1 ? "," : "") + "\"shm\":" + shm_->status_json() + "}";
        }
        return j;
    }

private:
    std::unique_ptr<IoDialect> jvs_;
    std::unique_ptr<TpShmDialect> shm_;
};

// Route tp-shm met ControlPipe in [Input]: het gedeelde geheugen EN het rapport voor de besturingspipe (House of the Dead: Scarlet Dawn:
// de trekkers en de aim uit het geheugen, start, test, subtrekker en munt uit het ALLS USB-IO-rapport). Naar buiten is dit het
// tp-shm-dialect; de status krijgt "control_pipe" erbij.
class WithControlPipeDialect final : public IoDialect {
public:
    WithControlPipeDialect(std::unique_ptr<IoDialect> main, std::unique_ptr<ControlPipeDialect> cp) : main_(std::move(main)), cp_(std::move(cp)) {}
    ~WithControlPipeDialect() override { close(); }
    const char* name() const override { return main_->name(); }
    std::string game() const override { return main_->game(); }
    bool open(std::string& err) override {
        if (!cp_->open(err)) return false;
        if (!main_->open(err)) { cp_->close(); return false; }
        return true;
    }
    void write(const IoState& s) override { main_->write(s); cp_->write(s); }
    void neutral() override { main_->neutral(); cp_->neutral(); }
    void close() override { main_->close(); cp_->close(); }
    IoOutputs outputs() const override { return main_->outputs(); }
    std::string status_json() const override {
        std::string j = main_->status_json();
        if (!j.empty() && j.back() == '}') {
            j.pop_back();
            j += std::string(j.size() > 1 ? "," : "") + "\"control_pipe\":" + cp_->status_json() + "}";
        }
        return j;
    }

private:
    std::unique_ptr<IoDialect> main_;
    std::unique_ptr<ControlPipeDialect> cp_;
};
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

IoRoutes IoBridgeService::effective_routes() const {
    std::string err;
    IoRoutes r = load_io_routes(cfg_.dirs, err);
    std::lock_guard<std::mutex> lk(routes_m_);
    if (err != routes_err_ && !err.empty()) logf("waarschuwing", "IO-brug: %s", err.c_str());
    routes_err_ = err;
    return r;
}

std::string IoBridgeService::start(const std::string& game, const std::string& route_override) {
    const IoRoutes routes = effective_routes();
    std::string route = route_override.empty() ? routes.route_for(game) : route_override;
    if (!valid_route(route)) return "onbekende route '" + route + "' (evdev, tp-shm, unity-tcp, hid-board of jvs)";
    // Vanaf nu geldt de gamepad-indeling van deze game ([Gamepad] in game.ini), op elke route (ook evdev: toetsen en knoppen).
    if (sh_.gamepad) sh_.gamepad->set_game(game);
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
    if (route == kRouteHidBoard) return start_hid_board(game, routes.find(game));
    if (route == kRouteJvs) return start_jvs(game, routes.find(game));
    // tp-shm
    const IoRoute* r = routes.find(game);
    if (!r || r->layout.empty()) return "game '" + game + "' heeft geen tp-shm-indeling (Layout in [Input] van game.ini)";
    const TpShmProfile* prof = cfg_.profiles.find(r->layout);
    if (!prof) return "tp-shm-indeling '" + r->layout + "' ontbreekt in tp-shm-profiles.json";
    std::unique_ptr<IoDialect> d = std::make_unique<TpShmDialect>(*prof, game, cfg_.shm_path, cfg_.tpui_shm_path);
    std::string cp_note;
    if (!r->control_pipe.empty()) {
        const ControlPipeProfile* cp = cfg_.control_pipes.find(r->control_pipe);
        if (!cp) return "besturingspipe '" + r->control_pipe + "' ontbreekt in control-pipe-profiles.json";
        d = std::make_unique<WithControlPipeDialect>(std::move(d), std::make_unique<ControlPipeDialect>(*cp, game, cfg_.control_pipe_path));
        cp_note = ", besturingspipe " + cp->name + " (" + cp->pipe + ") in " + cfg_.control_pipe_path;
    }
    std::string err;
    if (!d->open(err)) return err;
    std::lock_guard<std::mutex> lk(m_);
    if (dialect_) { dialect_->neutral(); dialect_->close(); }
    if (control_ && cp_note.empty()) ::unlink(cfg_.control_pipe_path.c_str());   // de vorige route had een besturingspipe, deze niet
    dialect_ = std::move(d);
    coin_codes_.clear();
    coin_prev_ = {};
    for (const auto& k : r->coin_keys) coin_codes_.push_back(ravengun::output::key_code(k));
    game_ = game;
    route_ = kRouteTpShm;
    shm_ = false;
    control_ = !cp_note.empty();
    seen_alive_ = false;
    gone_since_ms_ = 0;
    started_ms_ = steady_ms();
    write_state_file(route_, game_);
    logf("info", "IO-brug: tp-shm voor %s (indeling %s, %s%s)", game.c_str(), prof->name.c_str(), cfg_.shm_path.c_str(), cp_note.c_str());
    cv_.notify_all();
    return {};
}

// Route hid-board: een virtueel joystick/HID-apparaat dat het bord van de game nabootst (hid_board.hpp).
// Meerdere borden mogen naast elkaar: `Boards = A, B` (net als bij jvs), elk met zijn eigen transport. Eén bord
// houdt de enkel-bord-vorm, zodat bestaand gedrag en de bestaande statusvorm niet veranderen.
std::string IoBridgeService::start_hid_board(const std::string& game, const IoRoute* r) {
    if (!r) return "game '" + game + "' heeft geen [Input] in game.ini";
    std::vector<std::string> namen = r->boards;
    if (namen.empty() && !r->board.empty()) namen.push_back(r->board);
    if (namen.empty()) return "game '" + game + "' heeft geen Board of Boards in [Input] van game.ini";
    auto trace = [](const std::string& line) { logf("info", "hid-board: %s", line.c_str()); };
    std::vector<std::unique_ptr<HidBoardDialect>> borden;
    std::string omschrijving;
    for (const auto& naam : namen) {
        const HidBoard* b = cfg_.boards.find(naam);
        if (!b) return "bord '" + naam + "' ontbreekt in hid-boards.json";
        std::unique_ptr<HidBoardDevice> dev;
        if (cfg_.board_device) dev = cfg_.board_device(*b);
        else dev = b->transport == "uhid" ? make_uhid_board_device(cfg_.uhid_path) : make_uinput_board_device();
        auto d = std::make_unique<HidBoardDialect>(*b, game, std::move(dev));
        // Per ontvangen uitvoerrapport een regel in het logboek: welke respons er gezet is, of dat er geen bij past.
        d->set_trace(trace);
        borden.push_back(std::move(d));
        if (!omschrijving.empty()) omschrijving += ", ";
        omschrijving += b->name + " via " + b->transport;
    }
    std::unique_ptr<IoDialect> d;
    if (borden.size() == 1) d = std::move(borden.front());
    else d = std::make_unique<HidBoardsDialect>(game, std::move(borden));
    std::string err;
    if (!d->open(err)) return err;
    std::lock_guard<std::mutex> lk(m_);
    if (dialect_) { apply_coin_keys(IoState{}); dialect_->neutral(); dialect_->close(); }
    if (control_) { ::unlink(cfg_.control_pipe_path.c_str()); control_ = false; }
    dialect_ = std::move(d);
    coin_codes_.clear();
    coin_prev_ = {};
    for (const auto& k : r->coin_keys) coin_codes_.push_back(ravengun::output::key_code(k));
    game_ = game;
    route_ = kRouteHidBoard;
    shm_ = false;
    seen_alive_ = false;
    gone_since_ms_ = 0;
    started_ms_ = steady_ms();
    write_state_file(route_, game_);
    logf("info", "IO-brug: hid-board voor %s (%s)", game.c_str(), omschrijving.c_str());
    cv_.notify_all();
    return {};
}

// Route jvs: de daemon is de JVS-bord(en) aan het eind van een seriële lijn (pty, jvs_dialect.hpp). Een herstart van de daemon
// kan de lijn niet meenemen (de game houdt de oude pty vast), daarom staat deze route niet in het toestandsbestand.
std::string IoBridgeService::start_jvs(const std::string& game, const IoRoute* r) {
    if (!r) return "game '" + game + "' heeft geen [Input] in game.ini";
    std::vector<std::string> names = r->boards;
    if (names.empty() && !r->board.empty()) names.push_back(r->board);
    if (names.empty()) return "game '" + game + "' heeft geen Board of Boards in [Input] van game.ini";
    std::vector<JvsBoard> boards;
    for (const auto& n : names) {
        const JvsBoard* b = cfg_.jvs_boards.find(n);
        if (!b) return "JVS-bord '" + n + "' ontbreekt in jvs-boards.json";
        boards.push_back(*b);
    }
    std::unique_ptr<JvsTransport> t = cfg_.jvs_transport ? cfg_.jvs_transport() : make_pty_transport(cfg_.jvs_link);
    auto jd = std::make_unique<JvsDialect>(std::move(boards), game, std::move(t), cfg_.jvs_modem_file);
    jd->set_trace([](const std::string& line) { logf("info", "JVS: %s", line.c_str()); });
    std::unique_ptr<IoDialect> d = std::move(jd);
    std::string shm_note;
    if (!r->layout.empty()) {
        // Met een Layout schrijft de daemon ook die tp-shm-indeling (bv. het stuur van Let's Go Safari, dat niet over JVS gaat).
        const TpShmProfile* prof = cfg_.profiles.find(r->layout);
        if (!prof) return "tp-shm-indeling '" + r->layout + "' ontbreekt in tp-shm-profiles.json";
        d = std::make_unique<JvsWithShmDialect>(std::move(d), std::make_unique<TpShmDialect>(*prof, game, cfg_.shm_path, cfg_.tpui_shm_path));
        shm_note = ", plus tp-shm-indeling " + prof->name + " in " + cfg_.shm_path;
    }
    std::string err;
    if (!d->open(err)) return err;
    std::lock_guard<std::mutex> lk(m_);
    if (dialect_) { apply_coin_keys(IoState{}); dialect_->neutral(); dialect_->close(); }
    if (control_) { ::unlink(cfg_.control_pipe_path.c_str()); control_ = false; }
    dialect_ = std::move(d);
    coin_codes_.clear();
    coin_prev_ = {};
    for (const auto& k : r->coin_keys) coin_codes_.push_back(ravengun::output::key_code(k));
    game_ = game;
    route_ = kRouteJvs;
    seen_alive_ = false;
    gone_since_ms_ = 0;
    started_ms_ = steady_ms();
    if (!cfg_.state_path.empty()) ::unlink(cfg_.state_path.c_str());   // geen herstart-toestand voor deze route
    shm_ = !shm_note.empty();
    logf("info", "IO-brug: jvs voor %s (%zu bord(en), lijn %s%s)", game.c_str(), names.size(), cfg_.jvs_link.empty() ? "zonder symlink" : cfg_.jvs_link.c_str(),
         shm_note.c_str());
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
    if (!rt || !g || !rt->is_string() || !g->is_string() || (rt->str != kRouteTpShm && rt->str != kRouteHidBoard)) { remove_state(); return false; }
    if (!cfg_.tpui_procs.empty() && !ravengun::games::unit_alive(cfg_.tpui_procs)) {
        logf("info", "IO-brug: toestandsbestand voor %s, maar de game draait niet meer; niet hervat", g->str.c_str());
        remove_state();
        return false;
    }
    const std::string err = start(g->str, rt->str);
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
    logf("info", "IO-brug: %s voor %s hervat na een herstart", rt->str.c_str(), g->str.c_str());
    return true;
}

void IoBridgeService::stop_locked() {
    if (!dialect_) return;
    apply_coin_keys(IoState{});   // vastgehouden munttoetsen loslaten
    dialect_->neutral();
    dialect_->close();
    if (route_ == kRouteTpShm || shm_) ::unlink(cfg_.shm_path.c_str());
    if (control_) ::unlink(cfg_.control_pipe_path.c_str());
    shm_ = false;
    control_ = false;
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
    const IoRoutes routes = effective_routes();   // buiten m_: leest het gebruikersbestand
    std::string routes_err;
    {
        std::lock_guard<std::mutex> rl(routes_m_);
        routes_err = routes_err_;
    }
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
    w.key("default").value(routes.default_route);
    for (const auto& g : routes.games) w.key(g.game).value(g.route);
    w.end_object();
    if (!routes_err.empty()) w.key("user_routes_error").value(routes_err);
    w.end_object();
    return w.str();
}

}  // namespace ravengund
