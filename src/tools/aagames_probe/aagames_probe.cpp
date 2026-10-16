// ravengun-aagames-probe: testprogramma voor de Unity-route (Tomb Raider / Rabbids Hollywood zonder TeknoParrot).
// Praat als TCP-client met de BepInEx-plugin van DemulShooter in de game (127.0.0.1:33610), met dezelfde klant als de
// daemon (ravengun/games/aagames.hpp), maar leest de aim van een DRAAIENDE ravengund via de gewone IPC (commando
// `output`), zodat de daemon niet herstart hoeft te worden. Bij een recoil-vlag van de game geeft de probe een rumble-puls
// (IPC `rumble_test`). Munt, start en test kan de probe als eigen virtueel toetsenbord (uinput, eigen identiteit
// 1209:72f0, dus zonder de udev-links van ravengund) naar de game sturen.
//
// Gebruik: ravengun-aagames-probe --game TombRaider|RabbidsHollywood [--config PAD] [--route PAD] [--source ipc|fixed|circle]
//            [--u 0.5 --v 0.5] [--trigger] [--reload] [--seconds N] [--hex] [--no-rumble] [--y-origin top|bottom]
//            [--key NAAM[@SEC]]... (toets op SEC seconden na de start, 300 ms ingedrukt; namen: 0-9, enter, up, down, esc)
//   --config PAD   het unity.json van de game (standaard patches/<Game>/unity.json, de gebruikerslaag eerst)
//   --route PAD    packaging/unity-route.json (poort; standaard die naast build/)
//   ipc     de aim en knoppen van de echte Move, via ravengund (speler 1..4 volgens de daemon)
//   fixed   vast punt (--u/--v), met optioneel --trigger/--reload
//   circle  een cirkel om het midden (1 s per ronde), om de richting van X en Y te zien
#include <linux/input.h>
#include <linux/uinput.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ravengun/games/aagames.hpp"
#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/launch/unity.hpp"
#include "ravengun/ipc_client.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/key_table.hpp"

using namespace ravengun;
using namespace ravengun::games;

namespace {
std::atomic<bool> g_stop{false};
void on_sig(int) { g_stop = true; }

std::string slurp(const std::string& path) {
    std::string s;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

const json::Value* path(const json::Value& v, std::initializer_list<const char*> keys) {
    const json::Value* c = &v;
    for (const char* k : keys) { c = c->find(k); if (!c) return nullptr; }
    return c;
}

// Poller van de aim via de daemon.
class IpcAim {
public:
    bool open() { return c_.connect(ipc::socket_path(), 1000); }
    const std::string& error() const { return c_.error(); }
    bool poll(AaInputs& in, bool& armed) {
        if (!c_.send_line("{\"v\":1,\"cmd\":\"output\",\"id\":1}")) return false;
        auto line = c_.read_line(500);
        if (!line) return false;
        auto pr = json::parse(*line);
        if (!pr.ok) return false;
        const json::Value* pl = path(pr.value, {"result", "players"});
        const json::Value* ar = path(pr.value, {"result", "armed"});
        armed = ar && ar->is_bool() && ar->b;
        auto read_player = [&](const json::Value& p, int i, bool act) {
            auto& o = in[static_cast<size_t>(i)];
            const json::Value* ax = path(p, {"position", "abs_x"});
            const json::Value* ay = path(p, {"position", "abs_y"});
            o.active = act && ax && ax->is_number() && ay && ay->is_number();
            if (!o.active) return;
            o.u = ax->num / 32767.0;
            o.v = ay->num / 32767.0;
            if (const json::Value* bd = p.find("buttons_down"); bd && bd->is_array())
                for (const auto& b : bd->arr) {
                    if (b.is_string() && b.str == "trigger") o.trigger = true;
                    if (b.is_string() && b.str == "reload") o.reload = true;
                }
        };
        if (pl && pl->is_array()) {
            for (const auto& p : pl->arr) {
                const json::Value* num = p.find("player");
                if (!num || !num->is_number()) continue;
                const int i = static_cast<int>(num->num) - 1;
                if (i < 0 || i >= kAaPlayers) continue;
                const json::Value* act = p.find("active");
                const json::Value* pa = p.find("armed");
                read_player(p, i, act && act->is_bool() && act->b && pa && pa->is_bool() && pa->b);
            }
        } else if (const json::Value* res = path(pr.value, {"result"}); res && armed) {
            read_player(*res, 0, true);   // oudere daemon zonder 'players': alleen speler 1 op het hoogste niveau
        } else {
            return false;
        }
        return true;
    }
    // true als de daemon de puls aannam.
    bool rumble(int player, int ms) {
        // Speler 1 zonder veld 'player': ook een oudere daemon (zonder meerdere spelers) begrijpt dat.
        c_.send_line("{\"v\":1,\"cmd\":\"rumble_test\"" + (player > 1 ? ",\"player\":" + std::to_string(player) : std::string()) +
                     ",\"ms\":" + std::to_string(ms) + "}");
        auto l = c_.read_line(500);
        return l && l->find("\"ok\":true") != std::string::npos;
    }

private:
    ipc::Client c_;
};

// Eigen virtueel toetsenbord van de probe (geen RavenGun-identiteit: de udev-links van ravengund blijven ongemoeid).
class ProbeKeyboard {
public:
    bool create() {
        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_ < 0) return false;
        ::ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        for (const auto& k : output::kKeyNames) ::ioctl(fd_, UI_SET_KEYBIT, k.code);
        uinput_setup us{};
        us.id = {BUS_USB, 0x1209, 0x72f0, 1};
        std::snprintf(us.name, sizeof us.name, "RavenGun Probe Keyboard");
        if (::ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ::ioctl(fd_, UI_DEV_CREATE) < 0) { ::close(fd_); fd_ = -1; return false; }
        ::usleep(300000);  // KWin moet het apparaat oppikken
        return true;
    }
    void key(int code, bool down) {
        if (fd_ < 0) return;
        input_event e[2]{};
        e[0].type = EV_KEY; e[0].code = static_cast<unsigned short>(code); e[0].value = down;
        e[1].type = EV_SYN; e[1].code = SYN_REPORT;
        if (::write(fd_, e, sizeof e) < 0) { /* best effort */ }
    }
    ~ProbeKeyboard() { if (fd_ >= 0) { ::ioctl(fd_, UI_DEV_DESTROY); ::close(fd_); } }

private:
    int fd_ = -1;
};

struct TimedKey { int code; double at_s; bool down = false, done = false; double t_down = 0; };
}  // namespace

int main(int argc, char** argv) {
    std::string game_key, cfg_path, route_path, source = "ipc", y_origin;
    double u = 0.5, v = 0.5, seconds = 0;
    bool trigger = false, reload = false, hex = false, rumble = true, force_trigger = false;
    int rumbles_sent = 0;
    std::vector<TimedKey> keys;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--game") game_key = val();
        else if (a == "--config") cfg_path = val();
        else if (a == "--route") route_path = val();
        else if (a == "--source") source = val();
        else if (a == "--y-origin") y_origin = val();
        else if (a == "--u") u = std::atof(val().c_str());
        else if (a == "--v") v = std::atof(val().c_str());
        else if (a == "--seconds") seconds = std::atof(val().c_str());
        else if (a == "--trigger") trigger = true;
        else if (a == "--reload") reload = true;
        else if (a == "--hex") hex = true;
        else if (a == "--force-trigger") force_trigger = true;
        else if (a == "--no-rumble") rumble = false;
        else if (a == "--key") {
            std::string s = val();
            double at = 0;
            if (auto p = s.find('@'); p != std::string::npos) { at = std::atof(s.c_str() + p + 1); s.resize(p); }
            const int code = output::key_code(s);
            if (code < 0) { std::fprintf(stderr, "onbekende toets '%s'\n", s.c_str()); return 64; }
            keys.push_back({code, at});
        } else { std::fprintf(stderr, "onbekende optie %s (zie de kop van aagames_probe.cpp)\n", a.c_str()); return 64; }
    }
    if (game_key.empty()) { std::fprintf(stderr, "--game vereist\n"); return 64; }
    std::string root;   // build/ravengun-aagames-probe -> de map boven build/ (de repo of de datamap)
    {
        char exe[4096];
        const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = 0;
            root = exe;
            for (int up = 0; up < 2; ++up) { const size_t sl = root.rfind('/'); if (sl == std::string::npos) break; root.resize(sl); }
        }
    }
    launch::UnityPaths paths;
    {
        const gamesettings::Dirs dirs = gamesettings::default_dirs(root);
        paths.patches = dirs.shipped;
        paths.user_patches = dirs.user;
        paths.route_json = !route_path.empty() ? route_path : root + "/packaging/unity-route.json";
    }
    std::string err, text = cfg_path.empty() ? launch::unity_text(paths, game_key) : slurp(cfg_path);
    if (text.empty()) { std::fprintf(stderr, "geen unity.json voor %s (gezocht in %s en %s)\n", game_key.c_str(), paths.user_patches.c_str(), paths.patches.c_str()); return 64; }
    AaGame g;
    if (!parse_aagames_game(game_key, text, g, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 66; }
    launch::UnityRoute route;
    if (!launch::load_unity_route(paths, route, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 66; }
    if (!y_origin.empty()) g.y_origin = y_origin;
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    IpcAim ipc;
    const bool use_ipc = source == "ipc";
    if (use_ipc && !ipc.open()) { std::fprintf(stderr, "geen verbinding met ravengund: %s\n", ipc.error().c_str()); return 69; }
    ProbeKeyboard kbd;
    if (!keys.empty() && !kbd.create()) { std::fprintf(stderr, "eigen toetsenbord maken mislukt (/dev/uinput)\n"); return 69; }

    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    AaInputs cur{};
    std::mutex mu;
    bool armed = false;
    std::atomic<int> recoil_pending[kAaPlayers] = {};
    AaClient client;
    client.start(g, route.port,
        [&](AaInputs& in) {
            std::lock_guard<std::mutex> lk(mu);
            in = cur;
        },
        [&](int p) { recoil_pending[p - 1]++; });
    std::printf("probe: %s (%s), %dx%d, y-oorsprong %s, bron %s\n", g.name.c_str(), to_string(g.type), g.width, g.height,
                g.y_origin.c_str(), source.c_str());

    double last_report = -1;
    while (!g_stop && (seconds <= 0 || elapsed() < seconds)) {
        const double t = elapsed();
        AaInputs in{};
        if (use_ipc) {
            if (!ipc.poll(in, armed)) { std::fprintf(stderr, "IPC-fout; stoppen\n"); break; }
            if (force_trigger) in[0].trigger = true;   // test: de trekker erbij zetten (de aim blijft van de echte Move)
        } else if (source == "circle") {
            in[0] = {true, 0.5 + 0.3 * std::cos(t * 6.2832), 0.5 + 0.3 * std::sin(t * 6.2832), trigger, reload};
        } else {
            in[0] = {true, u, v, trigger, reload};
        }
        { std::lock_guard<std::mutex> lk(mu); cur = in; }
        for (auto& k : keys) {
            if (!k.down && !k.done && t >= k.at_s) { kbd.key(k.code, true); k.down = true; k.t_down = t; std::printf("toets %s ingedrukt\n", output::key_name(k.code)); }
            else if (k.down && t - k.t_down >= 0.3) { kbd.key(k.code, false); k.down = false; k.done = true; }
        }
        for (int p = 0; p < kAaPlayers; ++p)
            while (recoil_pending[p].load() > 0) {
                recoil_pending[p]--;
                if (rumble && use_ipc && ipc.rumble(p + 1, 60)) ++rumbles_sent;
            }
        if (t - last_report >= 1.0) {
            last_report = t;
            const AaClientStats s = client.stats();
            const AaOutputs& o = s.last;
            std::printf("[%5.1fs] %s pakketten %.0f/s (totaal %llu, gedropt %llu) uitvoer %llu", t, s.connected ? "VERBONDEN" : "wacht op game",
                        s.packets_per_s, (unsigned long long)s.packets_sent, (unsigned long long)s.send_dropped, (unsigned long long)s.output_frames);
            if (s.have_outputs) std::printf(" | P1 speelt %d ammo %d leven %.2f credits %d recoil %llu | P2 speelt %d", o.playing[0], o.ammo[0], static_cast<double>(o.life[0]), o.credits[0],
                                            (unsigned long long)s.recoils[0], o.playing[1]);
            if (use_ipc) std::printf(" | rumble-pulsen %d", rumbles_sent);
            if (use_ipc) std::printf(" | daemon %s aim P1 %.3f,%.3f %s%s", armed ? "gewapend" : "NIET gewapend", in[0].u, in[0].v,
                                     in[0].trigger ? "T" : "-", in[0].reload ? "R" : "-");
            if (!s.last_error.empty()) std::printf(" | %s", s.last_error.c_str());
            std::printf("\n");
            if (hex) {
                auto pkt = build_input_packet(g, in);
                std::printf("  pakket:");
                for (uint8_t b : pkt) std::printf(" %02x", b);
                std::printf("\n");
            }
            std::fflush(stdout);
        }
        ::usleep(8000);
    }
    client.stop();
    std::puts(client.status_json().c_str());
    return 0;
}
