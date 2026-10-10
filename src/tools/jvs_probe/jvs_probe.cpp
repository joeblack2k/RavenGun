// ravengun-jvs-probe: het JVS-bord van de jvs-route zonder daemon, voor proeven met een game of met Wine. Maakt een pty met een
// symlink (standaard /tmp/ravengun-jvs-probe), speelt het bord uit packaging/jvs-boards.json en schrijft elk nieuw soort
// packet naar stdout; aan het eind (of bij Ctrl-C) de status. De toestand is vast: aim in het midden of --aim X,Y, en met
// --press NAAM (bv. p1.trigger, coin1, test) wordt dat signaal ingedrukt gehouden. Handig om te zien wat een game vraagt
// (bv. `ln -sfn /tmp/ravengun-jvs-probe <prefix>/dosdevices/com4`) zonder dat de daemon draait.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#include "ravengun/iobridge/jvs.hpp"
#include "ravengun/iobridge/jvs_dialect.hpp"

using namespace ravengun::iobridge;

static volatile std::sig_atomic_t g_stop = 0;
static void on_sig(int) { g_stop = 1; }

int main(int argc, char** argv) {
    std::string board = "LGS", link = "/tmp/ravengun-jvs-probe", file;
    int seconds = 60;
    double ax = 0.5, ay = 0.5;
    std::vector<std::string> press;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--board") board = next();
        else if (a == "--link") link = next();
        else if (a == "--file") file = next();
        else if (a == "--seconds") seconds = std::atoi(next().c_str());
        else if (a == "--aim") std::sscanf(next().c_str(), "%lf,%lf", &ax, &ay);
        else if (a == "--press") press.push_back(next());
        else {
            std::fprintf(stderr, "Gebruik: %s [--board NAAM] [--file jvs-boards.json] [--link PAD] [--seconds N] [--aim X,Y] [--press SIGNAAL]...\n", argv[0]);
            return 2;
        }
    }
    if (file.empty()) {
        const char* src = std::getenv("RAVENGUN_SOURCE_DIR");
        file = std::string(src ? src : ".") + "/packaging/jvs-boards.json";
    }
    std::ifstream f(file);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    JvsBoards bs = parse_jvs_boards(ss.str(), err);
    if (!err.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const JvsBoard* b = bs.find(board);
    if (!b) { std::fprintf(stderr, "bord '%s' niet gevonden in %s\n", board.c_str(), file.c_str()); return 1; }
    JvsDialect d({*b}, board, make_pty_transport(link), "/tmp/ravengun-jvs-probe.modem");
    d.set_trace([](const std::string& l) { std::printf("%s\n", l.c_str()); std::fflush(stdout); });
    if (!d.open(err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("JVS-bord %s op %s (link %s); de game opent die als COM-poort. Modemstatusbestand: /tmp/ravengun-jvs-probe.modem (RG_JVS_MODEM)\n", board.c_str(), "pty", link.c_str());
    std::fflush(stdout);
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);
    IoState s;
    s.armed = true;
    for (auto& p : s.player) { p.valid = true; p.x = ax; p.y = ay; }
    for (const auto& n : press) {
        if (n == "test") s.sys.test = true;
        else if (n == "service") s.sys.service = true;
        else if (n.size() == 5 && n.compare(0, 4, "coin") == 0) s.sys.coin[static_cast<size_t>(n[4] - '1') % kIoPlayers] = true;
        else if (n.size() > 3 && n[0] == 'p' && n[2] == '.') {
            IoPlayer& p = s.player[static_cast<size_t>(n[1] - '1') % kIoPlayers];
            const std::string r = n.substr(3);
            if (r == "start") p.start = true; else if (r == "trigger") p.trigger = true; else if (r == "reload") p.reload = true;
            else if (r == "grenade") p.grenade = true; else if (r == "aux1") p.aux1 = true; else if (r == "aux2") p.aux2 = true;
        }
    }
    for (int i = 0; i < seconds * 50 && !g_stop; ++i) {
        d.write(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("%s\n", d.status_json().c_str());
    d.close();
    return 0;
}
