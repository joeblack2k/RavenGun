// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * rg-direct: het hoofdproces van de unit ravengun-tpui bij een directe start (A10), op de plaats van scripts/launch-direct.py --run.
 *
 *   rg-direct --run PLAN.json          het plan van de daemon uitvoeren (src/ravengun/launch/direct.*)
 *   rg-direct --plan PROFIEL [--patches DIR] [--user DIR] [--tp DIR] [--shm-source PAD] [--control-source PAD] [--log-full] [--make-view]
 *                    [--session-id ID]
 *                                      het plan als JSON tonen (geen bijwerkingen, behalve met --make-view: dan
 *                                      de view van een game op de NAS maken zoals een start; voor tests/direct_golden.py)
 *   rg-direct --jvs-check              per regel hex van stdin: de pakketten en de antwoorden van het standaard JVS-bord
 *   rg-direct --tp-refs [--patches DIR] [--user DIR] [--tp DIR] [--json] [--expect-free GAME,...]
 *                                      per game in patches/ de bestanden uit de TeknoParrot-map die de start nog gebruikt (alleen lezen)
 *
 * --run doet wat launch-direct.py in _run() doet nadat de drop-ins en teknoparrot.ini geplaatst zijn (dat deed de daemon al):
 *   1. /dev/shm/TeknoParrot_JvsState (64 bytes) aanmaken en wachten op het bronbestand (de IO-brug van de daemon);
 *   2. de TCP-kant van \\.\pipe\TeknoParrot_JVS (verbind-vlag in byte 0 bij reset/adres, het standaardbord bij RG_JVS_BOARD=true,
 *      of alles naar de pty van de daemon bij RG_JVS_PTY);
 *   3. rg-jvsbridge.exe of pipehelper.exe in de prefix starten en wachten tot hij zich meldt; heeft het plan een besturingspipe
 *      ([Input] ControlPipe, bv. het ALLS USB-IO-rapport van House of the Dead: Scarlet Dawn), dan ook de TCP-kant daarvan (het
 *      rapport van de daemon uit control_source, elke control_interval_ms) en een tweede helper "<pipe> 127.0.0.1 <poort>";
 *   4. de lader starten (in gamescope als het plan dat zegt), zijn uitvoer in het logboek, en volgen tot de game weg is; bij twee exe's
 *      (HasTwoExecutables) de starts in hun orde met de wachttijd ertussen, een halve opstelling afbreken (exitcode 7), het spel volgen
 *      en daarna de andere start stoppen (launch-direct.py run_starts);
 *   5. de A11-samenvatting, de helper en de pipe dicht.
 * Geen shell en geen Python: elk proces wordt met posix_spawn gestart, met de omgeving uit het plan.
 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/games/games.hpp"
#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/iobridge/control_pipe.hpp"
#include "ravengun/json.hpp"
#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/direct_internal.hpp"
#include "ravengun/launch/amdaemon.hpp"
#include "ravengun/launch/jvs_pipe.hpp"
#include "ravengun/launch/native.hpp"

namespace L = ravengun::launch;
namespace J = ravengun::launch::jvspipe;

namespace {

// De AM-daemon van de run (alleen met [Launch] AmDaemon = true); run_einde() stopt hem.
std::unique_ptr<L::amd::Listener> g_am;

constexpr const char* kShmName = "TeknoParrot_JvsState";
// Het gedeelde geheugen van de kern. RG_DIRECT_SHM_PATH (alleen voor tests) verlegt het, zodat een test nooit het bestand van een
// game op de bench aanraakt; de naam voor de helper (TeknoParrot_JvsState) blijft.
std::string g_shm_path = "/dev/shm/TeknoParrot_JvsState";
constexpr int kShmSize = 64;

std::mutex g_log_m;
void log(const std::string& msg) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[16];
    std::strftime(b, sizeof b, "%H:%M:%S", &tm);
    std::lock_guard<std::mutex> g(g_log_m);
    std::printf("[rg-direct %s] %s\n", b, msg.c_str());
    std::fflush(stdout);
}
void out_line(const std::string& line) {
    std::lock_guard<std::mutex> g(g_log_m);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::string quote(const std::string& s) {   // shlex.quote
    if (s.empty()) return "''";
    bool safe = true;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || std::strchr("_@%+=:,./-", c))) safe = false;
    if (safe) return s;
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\"'\"'") : std::string(1, c);
    return out + "'";
}
std::string join_quoted(const std::vector<std::string>& v, size_t from = 0) {
    std::string out;
    for (size_t i = from; i < v.size(); ++i) out += (i > from ? " " : "") + quote(v[i]);
    return out;
}
bool ld_truthy(const std::string& v) {
    std::string l;
    for (char c : v) if (!std::isspace(static_cast<unsigned char>(c))) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l == "true";
}
std::string env_get(const L::EnvMap& e, const char* k) { auto it = e.find(k); return it == e.end() ? std::string() : it->second; }

std::atomic<bool> g_stop{false};
void on_term(int) { g_stop.store(true); }
// SIGTERM en SIGINT zetten g_stop (run_plan en --alleen-stappen; review 13, ronde 2, punt 4).
void zet_stopsignalen() {
    struct sigaction sa{};
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
}

// Het bronbestand dat de helper naar het gedeelde geheugen van de game spiegelt (plan.shm_source). Op de route tp-shm is dat
// /dev/shm/ravengun_JvsState van de daemon, en de helper kopieert byte 0 daaruit vlak voor elk antwoord; leeg = gelijk aan g_shm_path.
std::string g_shm_mirror_source;

// De verbind-vlag (byte 0): de kern meldt "geadresseerd" in de modemstatus alleen als die niet nul is. Hij moet in BEIDE bestanden
// staan voordat het antwoord op F1 de game bereikt. Stond hij alleen in TeknoParrot_JvsState, dan zette de spiegel er bij het volgende
// antwoord de oude 0 uit de bron overheen (de daemon zet hem pas een tik later): Gaia Attack 4 en Haunted Museum deelden dan adres na
// adres uit aan borden die er niet zijn en kwamen nooit aan pollen toe (gelezen in game.exe, gemeten in de logboeken, 2026-10-15).
void set_connect_flag(uint8_t v) {
    for (const std::string* path : {&g_shm_path, &g_shm_mirror_source}) {
        if (path->empty() || (path == &g_shm_mirror_source && *path == g_shm_path)) continue;
        const int fd = ::open(path->c_str(), O_RDWR);
        if (fd < 0) continue;
        (void)!::pwrite(fd, &v, 1, 0);
        ::close(fd);
    }
}

// --- de JVS-pipe (JvsPipeServer) -------------------------------------------------------------------------------------------------
class JvsPipeServer {
public:
    JvsPipeServer(bool board, bool f1_once, bool debug, std::string pty, J::Board b)
        : board_(board), f1_once_(f1_once), debug_(debug), pty_(std::move(pty)), bd_(std::move(b)) {}
    bool listen() {
        sock_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (sock_ < 0) return false;
        int one = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(sock_, 1) != 0) return false;
        socklen_t l = sizeof a;
        ::getsockname(sock_, reinterpret_cast<sockaddr*>(&a), &l);
        port_ = ntohs(a.sin_port);
        th_ = std::thread([this] { run(); });
        return true;
    }
    int port() const { return port_; }
    void close() {
        closing_.store(true);
        if (sock_ >= 0) { ::shutdown(sock_, SHUT_RDWR); ::close(sock_); sock_ = -1; }
        {
            std::lock_guard<std::mutex> g(cm_);
            if (conn_ >= 0) ::shutdown(conn_, SHUT_RDWR);
        }
        if (th_.joinable()) th_.join();
    }

private:
    // Na zoveel verbindingen alleen nog de telling: een game die blijft herverbinden mag het logboek niet vullen.
    static constexpr int kLogConnections = 10;

    void send_all(const J::Bytes& b) {
        size_t off = 0;
        while (off < b.size()) {
            const ssize_t n = ::send(conn_, b.data() + off, b.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return;
            off += static_cast<size_t>(n);
        }
    }
    // Een game mag de pipe sluiten en opnieuw openen: Gaia Attack 4 doet dat na de eerste bordcontrole, en zonder antwoord op
    // de tweede verbinding meldt hij "I/O ERROR 2". Elke verbinding krijgt het bord dus opnieuw. accept met een poll, zodat
    // close() de wachtende thread ook zonder verbinding loslaat.
    void run() {
        int verbinding = 0;
        while (!closing_.load()) {
            pollfd p{sock_, POLLIN, 0};
            const int r = ::poll(&p, 1, 200);
            if (r < 0 && errno != EINTR) return;
            if (r <= 0) continue;
            const int c = ::accept4(sock_, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) continue;
            {
                std::lock_guard<std::mutex> g(cm_);
                conn_ = c;
            }
            int one = 1;
            ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            ++verbinding;
            verbose_ = verbinding <= kLogConnections;
            if (verbose_)
                log("JVS-pipe: game verbonden (pipehelper via TCP" + (verbinding > 1 ? ", verbinding " + std::to_string(verbinding) : "") + ")");
            if (!pty_.empty()) run_pty_bridge();
            else serve_board();
            std::lock_guard<std::mutex> g(cm_);
            ::close(conn_);
            conn_ = -1;
        }
        if (verbinding > kLogConnections) log("JVS-pipe: " + std::to_string(verbinding) + " verbindingen in totaal");
    }
    // Het standaard JVS-bord in dit proces (zonder pty van de daemon).
    void serve_board() {
        J::Bytes buf;
        std::set<std::pair<int, int>> seen;
        long bytes_in = 0, packets = 0, answered = 0, unanswered = 0, rest_logged = 0;
        unsigned address = 1;
        bool assigned = false;
        uint8_t rb[4096];
        for (;;) {
            const ssize_t n = ::recv(conn_, rb, sizeof rb, 0);
            if (n <= 0) break;
            bytes_in += n;
            J::Bytes all = buf;
            all.insert(all.end(), rb, rb + n);
            const auto frs = J::frames(all, buf);
            if (!buf.empty() && rest_logged < 10) { ++rest_logged; log("JVS-pipe: onvolledig pakket in de buffer: " + J::hex(buf)); }
            for (const auto& fr : frs) {
                if (fr.size() < 3) continue;
                ++packets;
                const int node = fr[0], cmd = fr[2];
                if (debug_ && packets <= 300) log("JVS-pipe: #" + std::to_string(packets) + " van de game: " + J::hex(fr));
                if (cmd == 0xF0) {
                    set_connect_flag(0);
                    assigned = false;
                } else if (cmd == 0xF1 && f1_once_ && assigned) {
                    // RG_JVS_F1_ONCE=true: alleen het eerste adres toekennen; statusbyte 2 als "geen bord meer".
                    send_all({0xE0, 0x00, 0x02, 0x02, static_cast<uint8_t>((0x00 + 0x02 + 0x02) & 0xFF)});
                    ++unanswered;
                } else if (cmd == 0xF1) {
                    set_connect_flag(1);
                    assigned = true;
                    if (fr.size() > 3) address = fr[3];
                    send_all({0xE0, 0x00, 0x03, 0x01, 0x01, static_cast<uint8_t>((0x03 + 0x01 + 0x01) & 0xFF)});
                } else {
                    std::optional<J::Bytes> ans;
                    if (board_ && node >= 1 && static_cast<unsigned>(node) <= address) ans = J::answer(bd_, J::Bytes(fr.begin() + 2, fr.end() - 1));
                    if (ans) {
                        const J::Bytes packed = J::pack(*ans);
                        if (debug_ && packets <= 300) log("JVS-pipe: antwoord: " + J::hex(packed));
                        send_all(packed);
                        ++answered;
                    } else {
                        ++unanswered;
                    }
                }
                if (verbose_ && seen.insert({node, cmd}).second) {
                    char b[96];
                    std::snprintf(b, sizeof b, "JVS-pipe: pakket node %02X commando %02X (%zu bytes)", node, cmd, fr.size());
                    std::string extra;
                    if (cmd != 0xF0 && cmd != 0xF1)
                        extra = board_ ? " - beantwoord met het standaard JVS-bord (A10)" : " - niet beantwoord (geen JVS-bord in de directe start)";
                    if (debug_) extra += ": " + J::hex(fr);
                    log(b + extra);
                }
            }
        }
        if (verbose_)
            log("JVS-pipe: verbinding dicht (" + std::to_string(bytes_in) + " bytes van de game, " + std::to_string(packets) + " pakketten, " +
                std::to_string(answered) + " beantwoord, " + std::to_string(unanswered) + " niet beantwoord)");
    }
    // RG_JVS_PTY: alle JVS-bytes ongewijzigd naar de pty van de daemon en terug; alleen meekijken voor de verbind-vlag.
    void run_pty_bridge() {
        const int fd = ::open(pty_.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) { log("JVS-pipe: pty van de daemon niet te openen (" + pty_ + "): " + std::strerror(errno)); return; }
        termios t{};
        if (::tcgetattr(fd, &t) == 0) {
            t.c_iflag = 0;
            t.c_oflag = 0;
            t.c_lflag = 0;
            t.c_cflag = (t.c_cflag & ~(CSIZE | PARENB | CSTOPB)) | CS8 | CLOCAL | CREAD;
            t.c_cc[VMIN] = 1;
            t.c_cc[VTIME] = 0;
            ::tcsetattr(fd, TCSANOW, &t);
        }
        // Wat de daemon nog naar een vorige verbinding stuurde, hoort niet bij deze: weg ermee voordat de game iets leest.
        ::tcflush(fd, TCIOFLUSH);
        std::atomic<bool> stop{false};
        std::atomic<long> back{0};
        std::thread to_game([&] {
            uint8_t b[4096];
            long chunks = 0;
            while (!stop.load()) {
                pollfd p{fd, POLLIN, 0};
                const int r = ::poll(&p, 1, 200);
                if (r <= 0) { if (r < 0 && errno != EINTR) break; continue; }
                const ssize_t n = ::read(fd, b, sizeof b);
                if (n < 0) break;
                if (n == 0) continue;
                back += n;
                if (debug_ && ++chunks <= 300) log("JVS-pipe: van de daemon: " + J::hex(J::Bytes(b, b + n)));
                send_all(J::Bytes(b, b + n));
            }
        });
        J::Bytes buf;
        std::set<std::pair<int, int>> seen;
        long bytes_in = 0, packets = 0;
        uint8_t rb[4096];
        for (;;) {
            const ssize_t n = ::recv(conn_, rb, sizeof rb, 0);
            if (n <= 0) break;
            bytes_in += n;
            if (::write(fd, rb, static_cast<size_t>(n)) < 0) break;
            J::Bytes all = buf;
            all.insert(all.end(), rb, rb + n);
            for (const auto& fr : J::frames(all, buf)) {
                if (fr.size() < 3) continue;
                ++packets;
                const int cmd = fr[2];
                if (cmd == 0xF0) set_connect_flag(0);
                else if (cmd == 0xF1) set_connect_flag(1);
                if (debug_ && packets <= 300) log("JVS-pipe: #" + std::to_string(packets) + " van de game: " + J::hex(fr));
                if (verbose_ && seen.insert({fr[0], cmd}).second) {
                    char b[120];
                    std::snprintf(b, sizeof b, "JVS-pipe: pakket node %02X commando %02X (%zu bytes) - naar de pty van de daemon", fr[0], cmd, fr.size());
                    log(b);
                }
            }
        }
        stop.store(true);
        to_game.join();
        ::close(fd);
        if (verbose_)
            log("JVS-pipe: verbinding dicht (" + std::to_string(bytes_in) + " bytes van de game, " + std::to_string(packets) + " pakketten, " +
                std::to_string(back.load()) + " bytes van de daemon terug)");
    }

    bool board_, f1_once_, debug_;
    bool verbose_ = true;
    std::string pty_;
    J::Board bd_;
    int sock_ = -1, conn_ = -1, port_ = 0;
    std::mutex cm_;   // conn_ wordt door close() gelezen terwijl run() hem wisselt
    std::atomic<bool> closing_{false};
    std::thread th_;
};

// --- de besturingspipe (TeknoParrotPipe) ----------------------------------------------------------------------------------------
// TeknoParrotUi schrijft bij sommige profielen (Sega ALLS: ALLSUsbIoPipe) elke ~15 ms een rapport in \\.\pipe\TeknoParrotPipe en
// leest nooit iets terug. Hier: de TCP-kant voor een tweede rg-jvsbridge in de prefix ("<pipe> 127.0.0.1 <poort>"), die pas verbindt
// als de game de pipe opent en na een verbreking een nieuwe pipe maakt. Het rapport komt uit het bestand van de daemon (IO-brug,
// [Input] ControlPipe); dit proces leest het en stuurt het ongewijzigd door. Kent rg-direct de indeling (ingebouwd, op naam), dan
// staat elke knop die aan of uit gaat en elke munt in het logboek ("besturingspipe: aan: p1.start"): het bewijs dat START aankomt.
class ControlPipeServer {
public:
    ControlPipeServer(std::string source, int size, int interval_ms, std::string pipe, const ravengun::iobridge::ControlPipeProfile* prof)
        : source_(std::move(source)), size_(size), interval_ms_(interval_ms), pipe_(std::move(pipe)) {
        if (prof) prof_ = *prof, have_prof_ = true;
    }
    bool listen() {
        sock_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (sock_ < 0) return false;
        int one = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(sock_, 1) != 0) return false;
        socklen_t l = sizeof a;
        ::getsockname(sock_, reinterpret_cast<sockaddr*>(&a), &l);
        port_ = ntohs(a.sin_port);
        th_ = std::thread([this] { run(); });
        return true;
    }
    int port() const { return port_; }
    void close() {
        closing_.store(true);
        if (sock_ >= 0) { ::shutdown(sock_, SHUT_RDWR); ::close(sock_); sock_ = -1; }
        {
            std::lock_guard<std::mutex> g(cm_);
            if (conn_ >= 0) ::shutdown(conn_, SHUT_RDWR);
        }
        if (th_.joinable()) th_.join();
    }

private:
    bool send_all(const std::vector<uint8_t>& b) {
        size_t off = 0;
        while (off < b.size()) {
            const ssize_t n = ::send(conn_, b.data() + off, b.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return false;
            off += static_cast<size_t>(n);
        }
        return true;
    }
    void run() {
        int verbinding = 0;
        while (!closing_.load()) {
            pollfd p{sock_, POLLIN, 0};
            const int r = ::poll(&p, 1, 200);
            if (r < 0 && errno != EINTR) return;
            if (r <= 0) continue;
            const int c = ::accept4(sock_, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) continue;
            {
                std::lock_guard<std::mutex> g(cm_);
                conn_ = c;
            }
            int one = 1;
            ::setsockopt(conn_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            ++verbinding;
            log("besturingspipe: game verbonden (" + pipe_ + " via TCP, verbinding " + std::to_string(verbinding) + ")");
            const long n = serve();
            log("besturingspipe: verbinding " + std::to_string(verbinding) + " dicht (" + std::to_string(n) + " rapporten van " +
                std::to_string(size_) + " bytes verstuurd)");
            std::lock_guard<std::mutex> g(cm_);
            ::close(conn_);
            conn_ = -1;
        }
    }
    // Stuurt elke interval_ms het rapport tot de verbinding weg is; geeft het aantal verstuurde rapporten.
    long serve() {
        const int fd = ::open(source_.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) log("besturingspipe: " + source_ + " niet te openen: " + std::strerror(errno) + " (nullen tot het er is)");
        int src = fd;
        std::vector<uint8_t> buf(static_cast<size_t>(size_));
        long sent = 0;
        auto next = std::chrono::steady_clock::now();
        while (!closing_.load()) {
            // Wat van de pipe-kant komt (de game schrijft niets terug) weglezen; 0 = de helper sloot de verbinding.
            pollfd p{conn_, POLLIN, 0};
            if (::poll(&p, 1, 0) > 0) {
                uint8_t rb[512];
                const ssize_t n = ::recv(conn_, rb, sizeof rb, MSG_DONTWAIT);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) break;
            }
            if (src < 0) src = ::open(source_.c_str(), O_RDONLY | O_CLOEXEC);
            std::fill(buf.begin(), buf.end(), 0);
            if (src >= 0 && ::pread(src, buf.data(), buf.size(), 0) < 0) std::fill(buf.begin(), buf.end(), 0);
            if (!send_all(buf)) break;
            if (++sent == 1) log("besturingspipe: eerste rapport verstuurd (" + std::to_string(size_) + " bytes)");
            if (have_prof_) watch(buf);
            next += std::chrono::milliseconds(interval_ms_);
            const auto now = std::chrono::steady_clock::now();
            if (next < now) next = now;
            std::this_thread::sleep_until(next);
        }
        if (src >= 0) ::close(src);
        return sent;
    }
    // Knoppen en munten uit het rapport, met de namen uit de indeling; een regel per verandering (hoogstens kMaxLines).
    void watch(const std::vector<uint8_t>& b) {
        static constexpr int kMaxLines = 400;
        if (lines_ >= kMaxLines) return;
        if (state_.empty()) state_.assign(prof_.bits.size() + prof_.coins.size(), -1);
        for (size_t i = 0; i < prof_.bits.size() && lines_ < kMaxLines; ++i) {
            const auto& bit = prof_.bits[i];
            const size_t at = static_cast<size_t>(bit.offset + bit.bit / 8);
            const int on = at < b.size() && (b[at] & (1u << (bit.bit % 8))) ? 1 : 0;
            if (on == state_[i] || (state_[i] < 0 && !on)) { state_[i] = on; continue; }
            state_[i] = on;
            std::string naam;
            for (const auto& s : bit.signals) naam += (naam.empty() ? "" : "|") + s;
            log(std::string("besturingspipe: ") + (on ? "aan: " : "uit: ") + naam);
            if (++lines_ == kMaxLines) log("besturingspipe: genoeg regels; verdere knoppen niet meer in het logboek");
        }
        for (size_t i = 0; i < prof_.coins.size() && lines_ < kMaxLines; ++i) {
            const auto& c = prof_.coins[i];
            const size_t at = static_cast<size_t>(c.offset);
            const int v = at + 1 < b.size() ? static_cast<int>((b[at] | (b[at + 1] << 8)) / c.scale) : 0;
            int& was = state_[prof_.bits.size() + i];
            if (v == was) continue;
            if (was >= 0) {
                log("besturingspipe: munt " + c.signals[0] + ": teller " + std::to_string(v));
                ++lines_;
            }
            was = v;
        }
    }

    std::string source_;
    int size_, interval_ms_;
    std::string pipe_;
    ravengun::iobridge::ControlPipeProfile prof_;
    bool have_prof_ = false;
    std::vector<int> state_;
    int lines_ = 0;
    int sock_ = -1, conn_ = -1, port_ = 0;
    std::mutex cm_;
    std::atomic<bool> closing_{false};
    std::thread th_;
};

// --- processen ---------------------------------------------------------------------------------------------------------------------
struct Child { pid_t pid = -1; int out = -1; };

// Start argv in cwd met precies deze omgeving. stdout (en stderr als merge) naar een pijp; stdout_null: stdout naar /dev/null.
Child spawn(const std::vector<std::string>& argv, const std::string& cwd, const L::EnvMap& env, bool merge_stderr,
             bool stdout_null, bool eigen_groep = false) {
    Child c;
    int pfd[2];
    if (::pipe2(pfd, O_CLOEXEC) != 0) return c;
    std::vector<std::string> envs;
    for (const auto& [k, v] : env) envs.push_back(k + "=" + v);
    std::vector<char*> av, ev;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    for (auto& s : envs) ev.push_back(s.data());
    ev.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (stdout_null) posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    else posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 2);
    if (!merge_stderr && !stdout_null) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGTERM);
    sigaddset(&def, SIGINT);
    posix_spawnattr_setsigdefault(&at, &def);
    short vlaggen = POSIX_SPAWN_SETSIGDEF;
    if (eigen_groep) {
        /* Eigen procesgroep (pgid = het kind): een time-out kan dan de hele boom meenemen. wine start
         * meer dan een proces, dus alleen de eerste doden laat de rest staan. */
        posix_spawnattr_setpgroup(&at, 0);
        vlaggen |= POSIX_SPAWN_SETPGROUP;
    }
    posix_spawnattr_setflags(&at, vlaggen);
    pid_t pid = 0;
    // Het programma is een absoluut pad (wine, gamescope); anders zoekt posix_spawnp in de PATH van het plan niet, maar in de onze.
    const int rc = ::posix_spawnp(&pid, av[0], &fa, &at, av.data(), ev.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    ::close(pfd[1]);
    if (rc != 0) {
        ::close(pfd[0]);
        log("kan " + argv[0] + " niet starten: " + std::strerror(rc));
        return c;
    }
    c.pid = pid;
    c.out = pfd[0];
    return c;
}

// Regels uit een pijp (gesplitst op '\n', zoals een bestand in binaire modus in Python).
void read_lines(int fd, const std::function<void(const std::string&)>& f) {
    std::string buf;
    char b[4096];
    for (;;) {
        const ssize_t n = ::read(fd, b, sizeof b);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buf.append(b, static_cast<size_t>(n));
        size_t at;
        while ((at = buf.find('\n')) != std::string::npos) {
            f(buf.substr(0, at + 1));
            buf.erase(0, at + 1);
        }
    }
    if (!buf.empty()) f(buf);
    ::close(fd);
}

// decode_console(): de OpenParrot-lader schrijft UTF-16 naar de console, de rest UTF-8.
std::string decode_console(std::string raw) {
    const size_t nul = static_cast<size_t>(std::count(raw.begin(), raw.end(), '\0'));
    if (nul * 3 >= raw.size() && raw.size() > 2) raw.erase(std::remove(raw.begin(), raw.end(), '\0'), raw.end());
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) raw.pop_back();
    return raw;
}

std::vector<int> own_cgroup_pids() {
    std::vector<int> out;
    std::ifstream f("/proc/self/cgroup");
    std::string line, cg;
    while (std::getline(f, line))
        if (line.rfind("0::", 0) == 0) cg = line.substr(3);
    if (cg.empty()) return out;
    std::ifstream p("/sys/fs/cgroup" + cg + "/cgroup.procs");
    int pid;
    while (p >> pid) out.push_back(pid);
    return out;
}
std::string comm_of(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string s;
    std::getline(f, s);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool game_alive(const std::set<std::string>& comms) {
    const int me = ::getpid();
    for (int pid : own_cgroup_pids())
        if (pid != me && comms.count(comm_of(pid))) return true;
    return false;
}

// --- A11 ---------------------------------------------------------------------------------------------------------------------------
struct A11 {
    struct Pat { const char* kind; std::regex rx; };
    std::vector<Pat> pats;
    std::map<std::string, std::vector<std::string>> hits;
    std::vector<std::string> order;
    std::string profile, state;
    A11(std::string p, std::string s) : profile(std::move(p)), state(std::move(s)) {
        const auto ic = std::regex::ECMAScript | std::regex::icase;
        pats.push_back({"overgeslagen", std::regex(R"(skipped:\s*unexpected original bytes|overgeslagen:\s*onverwachte oorspronkelijke bytes)", ic)});
        pats.push_back({"toegepast", std::regex(R"(\b(?:patch(?:ed)?|aangepast|aanpassing)\s+'[^']+'\s+@0x[0-9A-Fa-f]+)", ic)});
        pats.push_back({"koppeling", std::regex(R"(\b(?:GOT hook|hook|koppeling)\b\s+'?[\w:~@]+'?\s+(?:@0x|installed|\(0x))", ic)});
        pats.push_back({"versie", std::regex(R"(\bRUNNING\s+\S|^BudgieLoader \d|^OpenParrot\S* \d|^(?:Loader|Core|Game):\s+\S|Version: \d|version \d+\.\d+|Detected Linux game:)")});
        pats.push_back({"fout", std::regex(R"(could not (?:hook|koppeling)|GAME PATH CHECK FAILED|DENYING|Failed to load dll|Failed to Load DLL|Unhandled exception|DEADC0DE)", ic)});
        for (const auto& p2 : pats) { order.push_back(p2.kind); hits[p2.kind]; }
    }
    void feed(const std::string& raw) {
        static const std::regex ansi("\x1b\\[[0-9;]*m");
        const std::string line = std::regex_replace(raw, ansi, "");
        if (line.rfind("[gamescope]", 0) == 0 || line.rfind("[launch-direct", 0) == 0 || line.rfind("[rg-direct", 0) == 0) return;
        for (const auto& p : pats)
            if (std::regex_search(line, p.rx)) {
                auto& v = hits[p.kind];
                if (std::find(v.begin(), v.end(), line) == v.end() && v.size() < 400) v.push_back(line);
                break;
            }
    }
    void write(const std::vector<std::string>& extra_files) {
        for (const auto& f : extra_files) {
            std::ifstream in(f);
            std::string l;
            while (std::getline(in, l)) feed(l);
        }
        const std::string d = state + "/a11";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char b[32];
        std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
        const std::string path = d + "/" + profile + "-" + b + ".txt";
        std::ofstream o(path);
        std::string summary;
        for (const auto& k : order) {
            o << "## " << k << " (" << hits[k].size() << ")\n";
            for (const auto& l : hits[k]) o << l << "\n";
            summary += (summary.empty() ? "" : ", ") + k + " " + std::to_string(hits[k].size());
        }
        log("A11: " + summary);
        log("A11: samenvatting in " + path);
    }
};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string stamp(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[32];
    std::strftime(b, sizeof b, fmt, &tm);
    return b;
}
std::string fmt_g(double v) {   // Python "%g"
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

// --- meer dan een start (launch-direct.py run_starts, _start_weg, _stop_start) -----------------------------------------------------
constexpr int kExitHalf = 7;            // EXIT_HALF: een start was al weg voordat de volgende begon (halve opstelling)
constexpr useconds_t kStartStap = 250000;   // START_STAP: zo vaak wordt tijdens het wachten gekeken of wat al gestart is nog leeft

struct StartRun {
    int n = 0;
    const L::DirectStart* s = nullptr;
    std::string log, stopped_by, started;
    bool meld = false, done = false;
    pid_t pid = -1;
    int rc = 0;
    std::thread pump;
    std::atomic<bool> pump_done{false};
    // proc.poll(): true als het gestarte proces (wine met de lader) geeindigd is; de exitcode staat dan in rc.
    bool poll() {
        if (done) return true;
        if (pid < 0) { done = true; rc = 1; return true; }
        int st = 0;
        if (::waitpid(pid, &st, WNOHANG) == pid) { done = true; rc = WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st); }
        return done;
    }
};

// True als deze start echt weg is: het gestarte proces is geeindigd EN er leeft geen proces meer met de naam van zijn exe. Twee keer
// gemeten met een stap ertussen, zodat een programma dat net gestart is en zijn naam nog niet draagt niet voor dood wordt aangezien.
bool start_weg(StartRun& r) {
    const std::set<std::string> comms(r.s->comms.begin(), r.s->comms.end());
    for (int poging = 0; poging < 2; ++poging) {
        if (!r.poll() || game_alive(comms)) return false;
        if (poging == 0) ::usleep(kStartStap);
    }
    return true;
}

// Stop een start: het gestarte proces en wat er nog onder de naam van zijn exe leeft (alleen in onze eigen cgroup).
void stop_start(StartRun& r, const std::string& waarom) {
    if (!r.poll() && r.pid > 0) ::kill(r.pid, SIGTERM);
    const int me = ::getpid();
    const std::set<std::string> comms(r.s->comms.begin(), r.s->comms.end());
    for (const auto& [sig, wachten] : {std::pair<int, int>{SIGTERM, 20}, {SIGKILL, 4}}) {
        std::vector<int> pids;
        for (int pid : own_cgroup_pids())
            if (pid != me && comms.count(comm_of(pid))) pids.push_back(pid);
        if (pids.empty()) break;
        for (int pid : pids) ::kill(pid, sig);
        for (int i = 0; i < wachten; ++i) {
            if (std::none_of(pids.begin(), pids.end(), [&](int pid) { return comms.count(comm_of(pid)) > 0; })) break;
            ::usleep(kStartStap);
        }
    }
    r.poll();
    r.stopped_by = waarom;
}

// De starts van het plan in hun vaste orde, met de wachttijd ertussen, en het spel volgen tot het weg is. Elke start krijgt dezelfde
// omgeving, werkmap, lader en kern; uitvoer en exitcode per start in <state>/starts/<Profiel>-<tijd>/. Is een start al weg voordat de
// volgende begint, dan wordt alles wat al liep gestopt en is de uitkomst kExitHalf. have_rc = false: gestopt (geen exitcode).
int run_starts(const L::DirectPlan& plan, const L::EnvMap& env, A11& a11, bool& have_rc) {
    have_rc = false;
    const auto& starts = plan.starts;
    const size_t n = starts.size();
    const std::string N = std::to_string(n);
    const std::string logdir = plan.state + "/starts/" + plan.profile + "-" + stamp("%Y%m%d-%H%M%S");
    std::error_code ec;
    std::filesystem::create_directories(logdir, ec);
    for (size_t i = 0; i < n; ++i) {   // start_log_lines()
        const auto& s = starts[i];
        if (i) log("wachten: " + fmt_g(s.wait_before) + " s; daarna alleen verder als elke eerdere start nog leeft");
        log("start " + std::to_string(i + 1) + "/" + N + ": " + s.name + " (" + (s.role == "main" ? "het spel" : "tweede exe") +
            (s.gamescope ? ", in gamescope" : "") + ") -- " + join_quoted(s.argv));
    }
    {
        std::string e;
        for (const auto& [k, v] : plan.launch_env) e += (e.empty() ? "" : " ") + k + "=" + v;
        log("omgeving (voor elke start dezelfde): " + e);
    }
    log("uitvoer en exitcode per start: " + logdir);

    std::vector<std::unique_ptr<StartRun>> lopend;
    std::vector<StartRun*> blijft;
    auto begin = [&](size_t i, const L::DirectStart& s) {
        auto r = std::make_unique<StartRun>();
        r->n = static_cast<int>(i + 1);
        r->s = &s;
        r->log = logdir + "/" + std::to_string(i + 1) + "-" + s.name + ".log";
        log("start " + std::to_string(i + 1) + "/" + N + " (" + s.name + "): cwd=" + s.cwd);
        Child c = spawn(s.argv, s.cwd, env, true, false);
        r->pid = c.pid;
        r->started = stamp("%H:%M:%S");
        StartRun* rp = r.get();
        const bool main = s.role == "main";
        if (c.out >= 0)
            r->pump = std::thread([rp, main, &a11, fd = c.out] {
                std::ofstream f(rp->log, std::ios::trunc);
                read_lines(fd, [&](const std::string& raw) {
                    const std::string line = decode_console(raw);
                    f << line << "\n";
                    f.flush();
                    out_line("[" + rp->s->name + "] " + line);
                    if (main) a11.feed(line);
                });
                rp->pump_done.store(true);
            });
        else
            r->pump_done.store(true);
        lopend.push_back(std::move(r));
    };
    auto verslag = [&](bool have, int rc_run) {
        ravengun::json::Writer w;
        w.begin_object().key("profile").value(plan.profile).key("exitcode_run");
        if (have) w.value(rc_run); else w.null();
        w.key("starts").begin_array();
        for (auto& r : lopend) {
            if (r->pump.joinable()) {   // net als thread.join(2)
                const int64_t until = now_ms() + 2000;
                while (now_ms() < until && !r->pump_done.load()) ::usleep(50000);
                if (r->pump_done.load()) r->pump.join(); else r->pump.detach();
            }
            if (!r->pump_done.load()) blijft.push_back(r.get());   // de losgelaten pomp leest nog: deze start blijft bestaan tot het einde
            const bool klaar = r->poll();
            log("start " + std::to_string(r->n) + "/" + N + " (" + r->s->name + "): exitcode " + (klaar ? std::to_string(r->rc) : std::string("nog lopend")) +
                (r->stopped_by.empty() ? std::string() : " (gestopt door de launcher: " + r->stopped_by + ")") + "; uitvoer in " + r->log);
            w.begin_object().key("n").value(r->n).key("name").value(r->s->name).key("role").value(r->s->role).key("argv").begin_array();
            for (const auto& a : r->s->argv) w.value(a);
            w.end_array().key("pid").value(static_cast<long long>(r->pid)).key("started").value(r->started).key("exitcode");
            if (klaar) w.value(r->rc); else w.null();
            w.key("exitcode_of").value("het gestarte proces (wine met de lader); de lader kan eerder stoppen dan de exe");
            w.key("stopped_by_launcher").value(r->stopped_by).key("output").value(r->log).end_object();
        }
        for (size_t i = lopend.size(); i < n; ++i) {
            w.begin_object().key("n").value(static_cast<long long>(i + 1)).key("name").value(starts[i].name).key("role").value(starts[i].role).key("argv").begin_array();
            for (const auto& a : starts[i].argv) w.value(a);
            w.end_array().key("pid").null().key("started").null().key("exitcode").null().key("not_started").value(true).end_object();
        }
        w.end_array().end_object();
        std::ofstream(logdir + "/starts.json", std::ios::trunc) << w.str() << "\n";
        for (auto& r : lopend)   // een start met een losgelaten pomp niet opruimen (de pomp schrijft er nog in)
            if (std::find(blijft.begin(), blijft.end(), r.get()) != blijft.end()) (void)r.release();
    };

    // De orde: start, wachten, controleren dat alles wat al liep nog leeft, volgende start.
    for (size_t i = 0; i < n; ++i) {
        const auto& s = starts[i];
        if (i) {
            log("wachten: " + fmt_g(s.wait_before) + " s voor start " + std::to_string(i + 1) + "/" + N + " (" + s.name + ")");
            StartRun* dood = nullptr;
            auto zoek = [&]() -> StartRun* { for (auto& r : lopend) if (start_weg(*r)) return r.get(); return nullptr; };
            const long stappen = static_cast<long>(std::ceil(s.wait_before / 0.25));
            for (long k = 0; k < stappen; ++k) {
                dood = zoek();
                if (dood || g_stop.load()) break;
                ::usleep(kStartStap);
            }
            if (!dood) dood = zoek();
            if (g_stop.load() && !dood) break;
            if (dood) {
                const std::string code = dood->poll() ? std::to_string(dood->rc) : std::string("None");
                log("FOUT: start " + std::to_string(dood->n) + "/" + N + " (" + dood->s->name + ") is GESTOPT (exitcode " + code + ") voordat start " +
                    std::to_string(i + 1) + "/" + N + " (" + s.name + ") begon. De run wordt afgebroken: een halve opstelling is geen geldige run. "
                    "De uitvoer van de gestopte start staat in " + dood->log);
                std::fprintf(stderr, "Halve opstelling: start %d/%zu (%s) stopte voordat start %zu/%zu (%s) begon\n", dood->n, n, dood->s->name.c_str(),
                             i + 1, n, s.name.c_str());
                for (auto& r : lopend)
                    if (r.get() != dood) stop_start(*r, "een eerdere start was weg voordat de volgende begon");
                verslag(true, kExitHalf);
                have_rc = true;
                return kExitHalf;
            }
        }
        begin(i, s);
    }

    // Volgen: de run duurt zolang het spel leeft. Stopt een andere start onderweg, dan staat dat een keer in het logboek.
    StartRun* hoofd = nullptr;
    for (auto& r : lopend) if (r->s->role == "main") { hoofd = r.get(); break; }
    int rc = 0;
    while (hoofd && !g_stop.load()) {
        if (hoofd->poll()) { have_rc = true; rc = hoofd->rc; break; }
        for (auto& r : lopend)
            if (r.get() != hoofd && !r->meld && start_weg(*r)) {
                r->meld = true;
                log("LET OP: start " + std::to_string(r->n) + "/" + N + " (" + r->s->name + ") is gestopt (exitcode " + std::to_string(r->rc) +
                    ") terwijl het spel nog draait; uitvoer in " + r->log);
            }
        ::usleep(500000);
    }
    if (hoofd && have_rc && !g_stop.load()) {
        log("lader van " + hoofd->s->name + " gestopt (exitcode " + std::to_string(rc) + "); het spel volgen");
        const std::set<std::string> comms(hoofd->s->comms.begin(), hoofd->s->comms.end());
        int quiet = 0;
        while (!g_stop.load() && quiet < 3) {
            quiet = game_alive(comms) ? 0 : quiet + 1;
            ::sleep(1);
        }
    }
    // Het spel is weg (of de run is gestopt): wat er nog naast liep hoort er niet te blijven staan.
    for (auto& r : lopend)
        if (r.get() != hoofd || g_stop.load())
            if (!start_weg(*r)) stop_start(*r, g_stop.load() ? "de run is gestopt" : "het spel is gestopt");
    verslag(have_rc, rc & 0xFF);
    return rc;
}

// Een helper in de prefix (rg-jvsbridge.exe of pipehelper.exe): zijn regels met "pipehelper" in het logboek (met `tag` ervoor), en
// klaar zodra hij "created" of "shm mirror active" meldt.
struct Helper {
    Child c;
    std::thread log_th;
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    void start(const std::vector<std::string>& args, const std::string& cwd, const L::EnvMap& env, const std::string& tag) {
        c = spawn(args, cwd, env, true, true);
        if (c.out < 0) return;
        log_th = std::thread([this, tag, fd = c.out] {
            read_lines(fd, [&](const std::string& raw) {
                std::string line = raw;
                while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
                if (line.find("pipehelper") == std::string::npos) return;
                log(tag + line);
                if (line.find("created") != std::string::npos || line.find("shm mirror active") != std::string::npos) {
                    std::lock_guard<std::mutex> g(m);
                    ready = true;
                    cv.notify_all();
                }
            });
        });
    }
    bool wait_ready(int seconds) {
        std::unique_lock<std::mutex> lk(m);
        return cv.wait_for(lk, std::chrono::seconds(seconds), [&] { return ready; });
    }
    void stop() {
        if (c.pid > 0) {
            int st = 0;
            if (::waitpid(c.pid, &st, WNOHANG) == 0) ::kill(c.pid, SIGTERM);
        }
        if (log_th.joinable()) log_th.detach();
    }
};

// De besturingspipe van een run (alleen als het plan er een heeft).
struct ControlSide {
    std::unique_ptr<ControlPipeServer> server;
    Helper helper;
};

int run_einde(const L::DirectPlan& plan, bool have_rc, int rc, Helper& helper, JvsPipeServer& server, ControlSide* ctl, A11& a11);

// --- [Launch] PrefixSetup ------------------------------------------------------------------------------------------------------------
// waitpid met een grens: true = het kind is opgehaald (of was er al niet meer). Een kind dat in de D-toestand op een haperende
// CIFS-koppeling hangt, houdt de start zo niet voor altijd vast (review 13, ronde 2, punt 4).
bool wacht_begrensd(pid_t pid, int& st, int ms) {
    const auto einde = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        const pid_t r = ::waitpid(pid, &st, WNOHANG);
        if (r == pid || (r < 0 && errno == ECHILD)) return true;
        if (std::chrono::steady_clock::now() >= einde) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// Leeft er nog een proces in deze procesgroep? (signaal 0; EPERM telt als ja.)
bool groep_leeft(pid_t pgid) { return ::kill(-pgid, 0) == 0 || errno == EPERM; }

bool wacht_op_groep(pid_t pgid, int ms) {
    const auto einde = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (groep_leeft(pgid)) {
        if (std::chrono::steady_clock::now() >= einde) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// Seconden uit de omgeving van rg-direct zelf (niet die van het plan): RG_PREFIXSTAP_TIMEOUT en RG_PREFIXSTAP_NALOOP (proeven).
int seconden_uit_omgeving(const char* naam, int standaard) {
    if (const char* t = ::getenv(naam)) {
        const int w = std::atoi(t);
        if (w > 0) return w;
    }
    return standaard;
}

// De wineserver bij de wine van het plan, en de omgeving waarmee hij de prefix vindt. De wrapper wine-prefix
// (scripts/wine-prefix.sh) zet zelf WINESERVER = <map van RG_WINE, anders de standaard-wine>/wineserver en WINEPREFIX =
// RG_WINEPREFIX; naast de wrapper staat geen wineserver, dus dirname(plan.wine) vond hem nooit en de kill werd stil overgeslagen
// (review 13, ronde 2, punt 4). Zonder wrapper: WINESERVER uit het plan als die er is (die volgt wine zelf ook), anders naast wine.
std::string wineserver_van(const L::DirectPlan& plan, const L::EnvMap& env, L::EnvMap& kenv) {
    namespace D = ravengun::launch::direct_detail;
    kenv = env;
    std::string ws;
    if (D::basename(plan.wine) == "wine-prefix") {
        std::string echt = env_get(env, "RG_WINE");
        if (echt.empty()) {
            std::string home = env_get(env, "HOME");
            if (home.empty())
                if (const char* h = ::getenv("HOME")) home = h;
            echt = home + "/.local/share/ravengun/wine/proton-11.0-2/bin/wine";
        }
        ws = D::dirname(echt) + "/wineserver";
        if (!env_get(env, "RG_WINEPREFIX").empty()) kenv["WINEPREFIX"] = env_get(env, "RG_WINEPREFIX");
    } else {
        ws = env_get(env, "WINESERVER");
        if (ws.empty()) ws = D::dirname(plan.wine) + "/wineserver";
    }
    if (env_get(kenv, "WINEPREFIX").empty()) kenv["WINEPREFIX"] = plan.prefix;
    return ws;
}

// De uitvoer van een stap: regels uit de pijp naar het logboek.
struct StapUitvoer {
    int fd = -1;
    std::string kop, rest;
    // 1 = er is gelezen, 0 = niets klaar (binnen ms), -1 = de pijp is dicht.
    int lees(int ms) {
        if (fd < 0) return -1;
        struct pollfd pfd { fd, POLLIN, 0 };
        const int pr = ::poll(&pfd, 1, ms);
        if (pr == 0 || (pr < 0 && errno == EINTR)) return 0;
        if (pr < 0) { sluit(); return -1; }
        char b[4096];
        const ssize_t n = ::read(fd, b, sizeof b);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
        if (n <= 0) { sluit(); return -1; }
        rest.append(b, static_cast<size_t>(n));
        size_t pos = 0;
        while ((pos = rest.find('\n')) != std::string::npos) {
            log(kop + decode_console(rest.substr(0, pos)));
            rest.erase(0, pos + 1);
        }
        return 1;
    }
    // Na het einde van het kind wat er nog in de pijp staat: de laatste regels zijn vaak de foutmelding (N6). Begrensd (1 MiB),
    // want een achtergebleven proces kan de pijp openhouden en blijven schrijven.
    void einde() {
        for (int i = 0; i < 256 && lees(0) == 1; ++i) {}
        if (!rest.empty()) log(kop + decode_console(rest));
        rest.clear();
        sluit();
    }
    void sluit() {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
};

// Na een time-out of een stop: de hele procesgroep van de stap weg, en dan wineserver -k voor wat wine nog in de prefix heeft
// staan (dat hoort bij deze stap). Alles met een grens: een proces in de D-toestand op de NAS houdt de start niet vast.
void stap_afbreken(const L::DirectPlan& plan, const L::EnvMap& env, pid_t pid, StapUitvoer& uit) {
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    int st = 0;
    if (!wacht_begrensd(pid, st, 5000))
        log("prefixstap: proces " + std::to_string(pid) + " is 5 s na SIGKILL nog niet weg (hangt het op de NAS?); verder zonder te wachten");
    uit.einde();
    L::EnvMap kenv;
    const std::string ws = wineserver_van(plan, env, kenv);
    if (::access(ws.c_str(), X_OK) != 0) {
        log("prefixstap: wineserver -k overgeslagen: " + ws + " bestaat niet of is niet uitvoerbaar");
        return;
    }
    log("prefixstap: " + ws + " -k (WINEPREFIX=" + env_get(kenv, "WINEPREFIX") + ")");
    Child k = spawn({ws, "-k"}, "/", kenv, true, true);
    if (k.pid <= 0) return;
    StapUitvoer ku{k.out, "prefixstap: wineserver: ", ""};
    int ks = 0;
    if (!wacht_begrensd(k.pid, ks, 10000)) {
        log("prefixstap: wineserver -k is na 10 s niet klaar; die wordt gestopt en de start gaat verder zonder te wachten");
        ::kill(k.pid, SIGKILL);
        wacht_begrensd(k.pid, ks, 1000);
    }
    ku.einde();   // ook de pijp dicht (die lekte eerder)
}

// Na een GESLAAGDE stap: wat er nog van de stap draait (wine start meer dan een proces) mag niet doorlopen terwijl de volgende
// stap controleert en schrijft. Eerst laten uitlopen (Wine stopt services.exe en co. zelf als het laatste programma weg is), dan
// SIGTERM, dan SIGKILL. false = de groep is niet leeg te krijgen (een proces hangt in de D-toestand): dan start er niets.
bool groep_opruimen(pid_t pgid, int naloop_s) {
    if (!groep_leeft(pgid)) return true;
    log("prefixstap: er draait nog iets van de stap (procesgroep " + std::to_string(pgid) + "); hoogstens " + std::to_string(naloop_s) +
        " s laten uitlopen");
    if (wacht_op_groep(pgid, naloop_s * 1000)) {
        log("prefixstap: de procesgroep is vanzelf leeg");
        return true;
    }
    log("prefixstap: de procesgroep loopt nog; SIGTERM");
    ::kill(-pgid, SIGTERM);
    if (wacht_op_groep(pgid, 2000)) return true;
    log("prefixstap: de procesgroep loopt nog; SIGKILL");
    ::kill(-pgid, SIGKILL);
    if (wacht_op_groep(pgid, 2000)) return true;
    log("prefixstap: de procesgroep " + std::to_string(pgid) + " is niet leeg te krijgen (hangt een proces op de NAS?)");
    return false;
}

// De controle voor er iets start, voor rg-direct --run en --alleen-stappen (review 13, ronde 2, punt 6: --alleen-stappen draaide
// de stappen zonder controle). De schrijfmappen zoals de laatste stap van de daemon (schrijfmap_problemen: de ouder, het pad, een
// zuster in andere hoofdletters en elk kind), en de prefixstappen met prefixstappen_problemen: het pad zelf en de weg ernaartoe,
// NIET de kinderen. Eerder gingen ook werkmap en doelmap door schrijfmap_problemen; voor Mario Kart DX is dat de view-wortel
// met ~24 koppelingen naar de NAS, en dan weigerde elke start (N1). 0 = in orde, 2 = geweigerd.
int controle_voor_start(const L::DirectPlan& plan, const L::DirectPaths& wp, const std::string& wie) {
    namespace D = ravengun::launch::direct_detail;
    if (plan.view_writable.empty() && plan.prefix_setup.empty()) return 0;
    const D::MountTableScope koppelpunten(wp);
    if (const std::string k = D::koppelpunten_fout(wp); !k.empty()) {
        log("schrijfmap: " + wie + " weigert: " + k);
        return 2;
    }
    for (const std::string& pad : plan.view_writable) {
        const std::vector<std::string> fout = D::schrijfmap_problemen(pad, wp);
        if (fout.empty()) continue;
        log("schrijfmap: " + wie + " weigert " + pad + ": een game mag nooit op de NAS schrijven; start via de daemon (die zet de "
            "schrijfmap om) of los dit op:");
        for (const std::string& f : fout) log("schrijfmap:   " + f);
        return 2;
    }
    const std::vector<std::string> fout = D::prefixstappen_problemen(plan, wp);
    if (!fout.empty()) {
        log("prefixstappen: " + wie + " weigert: een prefixstap mag nooit door een koppeling of op de NAS uitkomen; start via de "
            "daemon (die zet de schrijfmappen eerst om) of los dit op:");
        for (const std::string& f : fout) log("prefixstappen:   " + f);
        return 2;
    }
    return 0;
}

// De stappen van [Launch] PrefixSetup. Apart van run_plan zodat de proeven precies deze weg kunnen
// draaien zonder een spel te starten (rg-direct --alleen-stappen PLAN.json, met dezelfde controle_voor_start). Geeft 0 als alle
// stappen gelukt zijn (of overgeslagen werden door de stempel), 1 bij een mislukte of geweigerde stap.
int voer_prefixstappen(const L::DirectPlan& plan, const L::EnvMap& env, const L::DirectPaths& wp) {
    namespace D = ravengun::launch::direct_detail;
    /* De koppelpuntentabel een keer inlezen voor alle on_nas()-vragen hieronder. Nodig omdat deze
     * functie ook los draait (--alleen-stappen), en dan leest on_nas() anders de echte tabel. */
    const D::MountTableScope koppelpunten(wp);
    /* De loper controleert elke stap VLAK voor hij draait of schrijft opnieuw, met dezelfde helper als de controle vooraf en de
     * motor (prefixstap_problemen): tussen het plan en nu, en tussen twee stappen, kan er van alles veranderd zijn (review 13,
     * punt 1; ronde 2, punt 3: de werkmap van een run-stap werd hier eerder niet meer bekeken). */
    const std::string wortel = D::view_wortel(D::dirname(plan.game), wp);
    /* De prefix EEN keer bepalen, voor de controle van elke stap en de prefix:-bron (review 15, ronde 2, punt 3): een lege of
     * relatieve prefix weigert prefixstap_problemen() bij een prefix:-bron, hier vlak voor het lezen net als bij de controle
     * vooraf. */
    const std::string pfx = D::prefix_van_plan(plan);
    auto geweigerd = [&](const L::DirectSetupStep& stap) {
        const std::vector<std::string> f = D::prefixstap_problemen(stap, wortel, pfx, wp);
        for (const std::string& x : f) log("prefixstap: GEWEIGERD (" + stap.tekst + "): " + x);
        return !f.empty();
    };
    // 2z. [Launch] PrefixSetup: de eenmalige stappen per prefix, met EEN stempel voor de HELE lijst
    // (review 13, punt 3). Die stempel wordt pas geschreven als alle stappen gelukt zijn; een
    // kopieerstap loopt opnieuw als het doel ontbreekt (de stempel is per prefix, het doel per view).
    // Draait met de wine van de lader, zonder WINEARCH (de prefix van Mario Kart DX is 64-bits en met
    // WINEARCH=win32 weigert wine).
    if (!plan.prefix_setup.empty()) {
        /* EEN prefix voor alles (review 15, ronde 2, punt 4): pfx hierboven is die van de stempel, de prefix:-bron en de
         * NAS-controle. Wat wine (WINEPREFIX) of de wrapper (RG_WINEPREFIX) in de omgeving vindt moet daarmee kloppen, en de
         * stappen en wineserver -k/-w krijgen hem opgelegd: stapenv, niet env. */
        if (const std::vector<std::string> pf = D::prefix_omgeving_problemen(plan); !pf.empty()) {
            for (const std::string& x : pf) log("prefixstap: GEWEIGERD: " + x);
            log("prefixstap MISLUKT: de prefix van de stappen is niet eenduidig; de game wordt niet gestart");
            return 1;
        }
        const L::EnvMap stapenv = D::prefixstap_omgeving(plan, env);
        const bool stempel_gezet =
            !plan.prefix_setup_mark.empty() && ::access(plan.prefix_setup_mark.c_str(), F_OK) == 0;
        if (stempel_gezet)
            log("prefixstappen: stempel " + plan.prefix_setup_mark +
                " staat er; alleen ontbrekende kopieen lopen opnieuw");
        const int timeout_s = seconden_uit_omgeving("RG_PREFIXSTAP_TIMEOUT", 180);   // standaard 180 s
        const int naloop_s = seconden_uit_omgeving("RG_PREFIXSTAP_NALOOP", 10);
        for (const auto& stap : plan.prefix_setup) {
            if (stempel_gezet && !(stap.copy && ::access(stap.to.c_str(), F_OK) != 0)) {
                log("prefixstap: al gedaan (" + stap.tekst + ")");
                continue;
            }
            if (g_stop.load()) {
                log("prefixstap: stop gevraagd; de game wordt niet gestart");
                return 1;
            }
            bool goed = true;
            if (stap.copy) {
                /* De bron in de prefix opnieuw in elkaar zetten met de prefix van DIT plan: het plan
                 * kan met een andere prefix gemaakt zijn dan waarmee het nu draait (review 13, punt 4).
                 * from_rel is in prefixstap_problemen() gecontroleerd (geen .., niet absoluut). */
                const std::string bron = stap.from_prefix ? D::pjoin(pfx, stap.from_rel) : stap.from;
                if (stap.from_prefix && bron != stap.from)
                    log("prefixstap: bron in de prefix: " + bron + " (het plan noemde " + stap.from + ")");
                std::string fout, inhoud, rerr, werr;
                if (geweigerd(stap)) fout = "zie hierboven";
                if (fout.empty() && !D::read_file_strict(bron, inhoud, rerr)) fout = rerr;
                /* Nog een keer vlak voor het schrijven (het lezen van de bron kan duren). Geschreven wordt met write_atomic
                 * (rename vervangt de naam, volgt geen koppeling), niet met copy_file(overwrite). */
                if (fout.empty() && geweigerd(stap)) fout = "zie hierboven";
                if (fout.empty() && !D::write_atomic(stap.to, inhoud, werr)) fout = werr;
                goed = fout.empty();
                log(std::string("prefixstap: kopieren ") + (goed ? "klaar" : "GEWEIGERD") + ": " + bron +
                    " -> " + stap.to + (goed ? "" : " (" + fout + ")"));
            } else {
                /* De werkmap vlak voor het starten opnieuw (een eerdere stap kan hem veranderd hebben). */
                if (geweigerd(stap)) {
                    log("prefixstap MISLUKT: " + stap.tekst + "; de game wordt niet gestart");
                    return 1;
                }
                log("prefixstap: " + join_quoted(stap.argv) + " (cwd " + stap.cwd + ")");
                /* Eigen procesgroep: een time-out moet de hele boom meenemen. */
                Child c = spawn(stap.argv, stap.cwd, stapenv, true, false, true);
                if (c.pid <= 0) return 1;
                StapUitvoer uit{c.out, "prefixstap: ", ""};
                const auto einde = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
                int st = 0;
                bool klaar = false, afgebroken = false, gestopt = false;
                while (true) {
                    if (g_stop.load()) { gestopt = true; break; }
                    if (std::chrono::steady_clock::now() >= einde) { afgebroken = true; break; }
                    const pid_t r = ::waitpid(c.pid, &st, WNOHANG);
                    if (r == c.pid) { klaar = true; break; }
                    if (r < 0 && errno != EINTR) break;
                    /* De pijp leegmaken met poll() en niet met read_lines(): dat wacht op EOF en liet
                     * een hangende stap de hele start ophouden (review 13, punt 2). */
                    if (uit.lees(100) < 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (klaar) {
                    uit.einde();   // de laatste regels uit de pijp (N6)
                    goed = WIFEXITED(st) && WEXITSTATUS(st) == 0;
                    log(std::string("prefixstap: klaar (status ") + std::to_string(st) + ")" + (goed ? "" : " -- MISLUKT"));
                    /* Niets van deze stap mag nog lopen als de volgende begint (review 13, ronde 2, punt 4). */
                    if (!groep_opruimen(c.pid, naloop_s)) goed = false;
                } else {
                    log(std::string("prefixstap: ") +
                        (gestopt ? "stop gevraagd" : afgebroken ? "afgebroken na " + std::to_string(timeout_s) + " s" :
                                                                  "de wachtdienst op de stap gaf een fout") +
                        "; de procesgroep gaat dood");
                    stap_afbreken(plan, stapenv, c.pid, uit);
                    goed = false;
                    if (gestopt) return 1;
                }
            }
            if (!goed) {
                log("prefixstap MISLUKT: " + stap.tekst + "; de game wordt niet gestart");
                return 1;
            }
        }
        /* De stempel voor de hele lijst, pas nu alles gelukt is. Mislukt het schrijven, dan zeggen we
         * dat: anders zou de volgende start alles opnieuw doen zonder dat iemand weet waarom. */
        if (!plan.prefix_setup_mark.empty() && !stempel_gezet) {
            /* Nog een keer controleren, NU, vlak voor het schrijven (review 15, punt 4): tussen de
             * controle bij de start en dit moment kan er van alles veranderd zijn -- de map zelf kan
             * een koppeling naar de NAS geworden zijn, en create_directories en write_atomic volgen
             * die. Klopt er iets niet, dan slaan we de stempel over en zeggen we dat: de stappen
             * lopen dan een volgende keer opnieuw, wat altijd veilig is. */
            const std::vector<std::string> sfout = D::stempel_problemen(plan, wp);
            if (!sfout.empty()) {
                log("prefixstappen: de stempel wordt NIET geschreven -- " + sfout.front() +
                    "; de stappen lopen een volgende keer opnieuw");
                return 0;
            }
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(plan.prefix_setup_mark).parent_path(), ec);
            std::string werr;
            if (!D::write_atomic(plan.prefix_setup_mark, "klaar\n", werr))
                log("prefixstappen: de stempel " + plan.prefix_setup_mark + " kon niet geschreven worden (" + werr +
                    "); de volgende start doet de stappen opnieuw");
            else
                log("prefixstappen: stempel " + plan.prefix_setup_mark);
        }
    }

    return 0;
}

int run_plan(const std::string& plan_file) {
    std::string text;
    {
        std::ifstream f(plan_file, std::ios::binary);
        if (!f) { log("plan " + plan_file + " niet te lezen"); return 2; }
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    L::DirectPlan plan;
    std::string err;
    if (!L::plan_from_json(text, plan, err)) { log(err); return 2; }
    /* Review 10, A4: dezelfde controle als in de daemon-route (apply_direct). --run start de game en
     * zou anders een schrijfmap kunnen starten die (nog) een koppeling naar de NAS is. Pass 2 (bevinding 5): voor de schrijfmappen
     * precies de laatste stap van de daemon (schrijfmap_problemen, ook elk kind). De prefixstappen apart (review 13, ronde 2, N1):
     * zie controle_voor_start. */
    const L::DirectPaths wp = L::default_direct_paths(L::environ_map());
    if (const int rc = controle_voor_start(plan, wp, "--run"); rc != 0) return rc;
    {
        char self[4096];
        const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
        const std::string me = n > 0 ? std::string(self, static_cast<size_t>(n)) : "rg-direct";
        log("launcher: " + me + " sha256 " + L::sha256_file(me) + " (C++, gestart door ravengund)");
        log("plan: " + plan_file + " sha256 " + L::sha256_file(plan_file));
    }
    zet_stopsignalen();
    ::signal(SIGPIPE, SIG_IGN);

    const L::EnvMap& env = plan.env;
    J::Board board = J::default_board();
    if (!J::configure(board, env_get(env, "RG_JVS_IDENT"), env_get(env, "RG_JVS_FEATURES"))) log("RG_JVS_FEATURES is geen geldige hex; het standaardbord blijft");
    if (!plan.core_dll.empty()) log("kern (" + plan.core_source + "): " + plan.core_dll + " sha256 " + L::sha256_file(plan.core_dll));
    for (const auto& l : plan.patch_lines) log(l);

    // 2. Gedeeld geheugen en de bron voor de helper.
    {
        // Elke start begint schoon, zoals een kast na het aanzetten (TeknoParrotUi: JvsPackageEmulator.Initialize -> ResetState).
        // Byte 0 is de sense-lijn van de JVS-bus (de verbindingsvlag, 1 na een adres). Een 1 van de vorige run laat de game na
        // SETADDR 01 een tweede bord zien: hij vraagt adres 02, krijgt geen antwoord en stopt (gemeten met Let's Go Safari,
        // 2026-10-14: "F1 02 (geen antwoord)", game na 30 s weg).
        const int fd = ::open(g_shm_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0) {
            const std::array<uint8_t, kShmSize> zero{};
            if (::ftruncate(fd, kShmSize) != 0 || ::pwrite(fd, zero.data(), zero.size(), 0) != static_cast<ssize_t>(zero.size()))
                log("gedeeld geheugen " + g_shm_path + " niet op nul gezet: " + std::strerror(errno));
            ::close(fd);
        }
    }
    for (int i = 0; i < 100 && ::access(plan.shm_source.c_str(), F_OK) != 0; ++i) ::usleep(100000);
    if (::access(plan.shm_source.c_str(), F_OK) != 0) { log("bronbestand " + plan.shm_source + " bestaat niet (start de IO-brug eerst)"); return 1; }
    g_shm_mirror_source = plan.shm_source;
    if (plan.shm_source != g_shm_path) {
        // Het bronbestand van de daemon (/dev/shm/ravengun_JvsState) houdt bij een nieuwe game de bytes van de vorige: de daemon
        // schrijft alleen de bytes van zijn eigen indeling. De helper kopieert het hele bestand naar de game. TeknoParrotUi begint
        // met 64 nullen; een oude byte op 9..11 of 13..19 (bv. van Rampage) kan een game die 16 of 32 bits leest in de war brengen
        // (Time Crisis 5, 2026-10-14). Op nul zetten; de daemon zet zijn eigen bytes bij de volgende tik terug.
        const int sfd = ::open(plan.shm_source.c_str(), O_RDWR | O_CLOEXEC);
        if (sfd >= 0) {
            const std::array<uint8_t, kShmSize> zero{};
            if (::pwrite(sfd, zero.data(), zero.size(), 0) != static_cast<ssize_t>(zero.size()))
                log("bronbestand " + plan.shm_source + " niet op nul gezet: " + std::strerror(errno));
            ::close(sfd);
        }
    }
    if (!plan.control_pipe.empty()) {   // het rapport van de besturingspipe komt ook van de IO-brug
        for (int i = 0; i < 100 && ::access(plan.control_source.c_str(), F_OK) != 0; ++i) ::usleep(100000);
        if (::access(plan.control_source.c_str(), F_OK) != 0) {
            log("bronbestand " + plan.control_source + " van de besturingspipe bestaat niet (start de IO-brug eerst; [Input] ControlPipe = " +
                plan.control_pipe + ")");
            return 1;
        }
    }

    if (!plan.prefix_setup.empty()) {
        const int rc = voer_prefixstappen(plan, env, wp);
        if (rc != 0) return rc;
    }

    // 2a. [Launch] Install: het installatieprogramma uit de spelmap één keer per prefix (de markering zegt of het al gebeurd is).
    if (!plan.install_cmd.empty() && ::access(plan.install_mark.c_str(), F_OK) != 0) {
        log("installatie: " + join_quoted(plan.install_cmd));
        Child c = spawn(plan.install_cmd, L::plan_run_dir(plan), env, true, false);
        if (c.pid <= 0) return 1;
        read_lines(c.out, [&](const std::string& l) { log("installatie: " + decode_console(l)); });
        int st = 0;
        ::waitpid(c.pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            log("installatie mislukt (status " + std::to_string(st) + "); de game wordt niet gestart");
            return 1;
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(plan.install_mark).parent_path(), ec);
        if (FILE* f = std::fopen(plan.install_mark.c_str(), "w")) { std::fputs(plan.install_cmd.back().c_str(), f); std::fclose(f); }
        log("installatie klaar: " + plan.install_mark);
    } else if (!plan.install_cmd.empty()) log("installatie: al gedaan (" + plan.install_mark + ")");

    // 2b. [Launch] Registry: elk .reg-bestand één keer per prefix inlezen ("wine reg import" in de weergave van de game, 32 of 64
    // bits). Een markering <prefix>/.ravengun-reg/<naam>.<sha256>.<bits> houdt bij wat al is ingelezen; een gewijzigd bestand gaat
    // opnieuw. Lukt het inlezen niet, dan geen start: een run zonder zijn instelling is een ongeldige run (zoals bij de drop-ins).
    for (const std::string& reg : plan.reg_files) {
        const std::string bits = plan.is64 ? "64" : "32";
        const std::string mdir = plan.prefix + "/.ravengun-reg";
        const std::string mark = mdir + "/" + std::filesystem::path(reg).filename().string() + "." + L::sha256_file(reg).substr(0, 16) + "." + bits;
        if (::access(mark.c_str(), F_OK) == 0) { log("register: " + std::filesystem::path(reg).filename().string() + " is al ingelezen (" + bits + " bits)"); continue; }
        const std::vector<std::string> rargs{plan.wine, "reg", "import", L::wine_path(reg), "/reg:" + bits};
        log("register: " + join_quoted(rargs));
        Child c = spawn(rargs, L::plan_run_dir(plan), env, true, false);
        if (c.pid <= 0) return 1;
        read_lines(c.out, [&](const std::string& l) { log("register: " + decode_console(l)); });
        int st = 0;
        ::waitpid(c.pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            log("register: " + std::filesystem::path(reg).filename().string() + " niet ingelezen (status " + std::to_string(st) + "); de game wordt niet gestart");
            return 1;
        }
        ::mkdir(mdir.c_str(), 0755);
        if (FILE* f = std::fopen(mark.c_str(), "w")) { std::fputs(reg.c_str(), f); std::fclose(f); }
        log("register: " + std::filesystem::path(reg).filename().string() + " ingelezen in " + plan.prefix + " (" + bits + " bits)");
    }

    // 2c. [Launch] Drives: de schijfletters voor deze run (een die er al stond blijft; de rest gaat na afloop weer weg).
    std::vector<std::string> gezette_letters;
    for (const auto& d : plan.drives) {
        if (d.present) { log("schijfletter " + d.letter + ": staat al in de prefix (-> " + d.target + ")"); continue; }
        if (d.letter.size() > 1) {   // een map op C: (C:\PC\ALIENS): de bovenliggende mappen in drive_c aanmaken
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(d.link).parent_path(), ec);
        }
        if (::symlink(d.target.c_str(), d.link.c_str()) != 0) {
            log("schijfletter " + d.letter + ": niet te zetten (" + d.link + "): " + std::strerror(errno) + "; de game wordt niet gestart");
            for (const auto& l : gezette_letters) ::unlink(l.c_str());
            return 1;
        }
        gezette_letters.push_back(d.link);
        log("schijfletter " + d.letter + ": -> " + d.target + " (voor deze run)");
    }

    // 3. De JVS-pipe en de helper in de prefix (voor de game, zoals TeknoParrotUi).
    JvsPipeServer server(ld_truthy(env_get(env, "RG_JVS_BOARD")), ld_truthy(env_get(env, "RG_JVS_F1_ONCE")),
                         ld_truthy(env_get(env, "RG_JVS_DEBUG")), env_get(env, "RG_JVS_PTY"), board);
    if (!server.listen()) { log("JVS-pipe: luisteren op 127.0.0.1 mislukt"); return 1; }
    std::vector<std::string> hargs{plan.wine, plan.pipehelper};
    if (plan.pipe_mode == "pipe")
        hargs.insert(hargs.end(), {"TeknoParrot_JVS", "127.0.0.1", std::to_string(server.port()), kShmName, std::to_string(kShmSize), L::wine_path(plan.shm_source)});
    else
        hargs.insert(hargs.end(), {"shm", kShmName, std::to_string(kShmSize), L::wine_path(plan.shm_source)});
    L::EnvMap henv = env;
    henv["TP_PIPEHELPER_OWNER_PID"] = std::to_string(::getpid());
    log("pipehelper: " + join_quoted(hargs, 1));
    Helper helper;
    helper.start(hargs, L::plan_run_dir(plan), henv, "helper: ");   // de TeknoParrot-map alleen als het plan die gebruikt
    if (!helper.wait_ready(20)) log("pipehelper meldde zich niet binnen 20 s; toch verder");

    // 3a. De AM-daemon ([Launch] AmDaemon = true, voor de Sega Ring-games): rg-direct beantwoordt de AM Library van de game op
    // 127.0.0.1:40100..40115 zolang de run duurt en logt elk verzoek met het antwoord. Hier en niet in de daemon: de levensduur is
    // die van de run, een tweede game kan er niet door gestoord worden, en het logboek staat bij dat van de JVS-pipe.
    if (plan.am_daemon) {
        std::vector<int> ports;
        for (const auto& pt : plan.am_ports) ports.push_back(std::atoi(pt.c_str()));
        // Waar <spel-id>_Table.dat kan staan (de keychip-antwoorden op ds.compute/ssd.proof komen uit die tabel, zie amhw_table.h): MACtbl/ (LGI,
        // LGI3D), fs/table/ (GG, Transformers: ..\fs\table vanuit exe/) en rg-amhw-tables/ (de vervanging van rg-amhw.dll naast de DLL), onder de
        // werkmap, de map van de exe en de map erboven.
        std::vector<std::string> tdirs;
        auto parent = [](const std::string& p) { const size_t k = p.find_last_of('/'); return k == std::string::npos || k == 0 ? std::string() : p.substr(0, k); };
        for (const std::string& base : {plan.cwd, parent(plan.game), parent(parent(plan.game))}) {
            if (base.empty()) continue;
            for (const char* sub : {"MACtbl", "fs/table", "rg-amhw-tables", "exe/rg-amhw-tables", ""}) {
                const std::string d = std::string(sub).empty() ? base : base + "/" + sub;
                if (std::find(tdirs.begin(), tdirs.end(), d) == tdirs.end()) tdirs.push_back(d);
            }
        }
        g_am = std::make_unique<L::amd::Listener>(L::amd::Responder(plan.am_values, tdirs), ports, [](const std::string& m) { log(m); });
        if (g_am->listen() == 0) log("AM-daemon: geen enkele poort beschikbaar; de game start toch (hij zal op zijn AM Library wachten)");
    }

    // 3b. De besturingspipe (TeknoParrotPipe): de TCP-kant en een tweede helper die de pipe in de prefix maakt (vóór de game, zoals
    // TeknoParrotUi zijn ControlPipe start voordat het spel draait).
    std::unique_ptr<ControlSide> ctl;
    if (!plan.control_pipe.empty()) {
        ctl = std::make_unique<ControlSide>();
        std::string perr;
        const auto profs = ravengun::iobridge::builtin_control_pipe_profiles(perr);
        const auto* prof = profs.find(plan.control_pipe);
        ctl->server = std::make_unique<ControlPipeServer>(plan.control_source, plan.control_size, plan.control_interval_ms, plan.control_pipe_name, prof);
        if (!ctl->server->listen()) {
            log("besturingspipe: luisteren op 127.0.0.1 mislukt");
            helper.stop();
            server.close();
            return 1;
        }
        const std::vector<std::string> cargs{plan.wine, plan.pipehelper, plan.control_pipe_name, "127.0.0.1", std::to_string(ctl->server->port())};
        log("besturingspipe " + plan.control_pipe + ": " + join_quoted(cargs, 1) + " (rapport uit " + plan.control_source + ", elke " +
            std::to_string(plan.control_interval_ms) + " ms" + (prof ? "" : "; indeling onbekend in deze build, knoppen niet in het logboek") + ")");
        ctl->helper.start(cargs, L::plan_run_dir(plan), henv, "helper (" + plan.control_pipe_name + "): ");
        if (!ctl->helper.wait_ready(20)) log("besturingspipe: de helper meldde zich niet binnen 20 s; toch verder");
    }

    // 4. De lader (eventueel in gamescope). Een profiel met meer dan een start (twee exe's) gaat via run_starts(); een gewone game
    // (een exe) neemt het pad hieronder.
    A11 a11(plan.profile, plan.state);
    bool have_rc = false;
    int rc = 0;
    if (plan.starts.size() > 1) {
        rc = run_starts(plan, env, a11, have_rc);
        return run_einde(plan, have_rc, rc, helper, server, ctl.get(), a11);
    }
    log("start: cwd=" + plan.cwd);
    log("start: " + join_quoted(plan.argv));
    {
        std::string e;
        for (const auto& [k, v] : plan.launch_env) e += (e.empty() ? "" : " ") + k + "=" + v;
        log("omgeving: " + e);
    }
    Child game = spawn(plan.argv, plan.cwd, env, true, false);
    std::thread pump;
    std::atomic<bool> pump_done{false};
    if (game.out >= 0)
        pump = std::thread([&, fd = game.out] {
            read_lines(fd, [&](const std::string& raw) {
                const std::string line = decode_console(raw);
                out_line(line);
                a11.feed(line);
            });
            pump_done.store(true);
        });
    while (game.pid > 0 && !g_stop.load()) {
        int st = 0;
        const pid_t w = ::waitpid(game.pid, &st, WNOHANG);
        if (w == game.pid) { have_rc = true; rc = WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st); break; }
        ::usleep(500000);
    }
    if (game.pid < 0) { have_rc = true; rc = 1; }
    // De lader kan eerder stoppen dan de game (OpenParrotLoader): volgen tot ook de game weg is.
    if (have_rc && !g_stop.load() && game.pid > 0) {
        // OpenParrotLoader geeft de exitcode van de hoofdthread van de game terug; Wine kort een uitzonderingscode af op de laatste byte:
        // 5 is dan 0xC0000005 (toegangsfout in de game), 9 is 0xC0000409 (stapelcontrole). Het adres staat met +seh in het logboek.
        const char* hint = rc == 5 ? " [vermoedelijk 0xC0000005, een toegangsfout in de game; zet WINEDEBUG = -all,+seh in [Env] van game.ini voor het adres]"
                           : rc == 9 ? " [vermoedelijk 0xC0000409, een stapelcontrole in de game; zie WINEDEBUG = -all,+seh]" : "";
        log("lader gestopt (exitcode " + std::to_string(rc) + ")" + hint + "; de game volgen");
        const std::set<std::string> comms(plan.game_comms.begin(), plan.game_comms.end());
        int quiet = 0;
        while (!g_stop.load() && quiet < 3) {
            quiet = game_alive(comms) ? 0 : quiet + 1;
            ::sleep(1);
        }
    }
    if (pump.joinable()) {
        // net als pt.join(2): hoogstens 2 s op de laatste uitvoer wachten (een kind van de lader kan de pijp openhouden)
        const int64_t until = now_ms() + 2000;
        while (now_ms() < until && !pump_done.load()) ::usleep(50000);
        if (pump_done.load()) pump.join(); else pump.detach();
    }
    return run_einde(plan, have_rc, rc, helper, server, ctl.get(), a11);
}

// 5. run_einde(): de logbestanden van de kern in het logboek, de A11-oogst, de helper en de pipe dicht.
int run_einde(const L::DirectPlan& plan, bool have_rc, int rc, Helper& helper, JvsPipeServer& server, ControlSide* ctl, A11& a11) {
    std::vector<std::string> files;
    if (plan.log_full) {
        const std::time_t now = std::time(nullptr);
        for (const char* cand : {"teknoparrot_log.txt", "tp_log.txt", "TeknoParrot/teknoparrot_log.txt"})
            for (const std::string& base : {plan.cwd, plan.game.substr(0, plan.game.rfind('/')), plan.tp}) {
                if (base.empty()) continue;   // een plan zonder TeknoParrot-map
                const std::string pth = base + "/" + cand;
                struct stat st{};
                if (::stat(pth.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_mtime > now - 7200 &&
                    std::find(files.begin(), files.end(), pth) == files.end())
                    files.push_back(pth);
            }
        for (const auto& pth : files) {
            log("logbestand van de kern: " + pth);
            std::ifstream in(pth);
            std::string l;
            while (std::getline(in, l)) out_line("  | " + l);
        }
    }
    a11.write(files);
    helper.stop();
    server.close();
    if (g_am) { g_am->close(); g_am.reset(); }
    if (ctl) {
        ctl->helper.stop();
        if (ctl->server) ctl->server->close();
    }
    for (const auto& d : plan.drives) {   // [Launch] Drives: wat deze run zette weer weghalen
        char buf[4096];
        const ssize_t n = d.present ? -1 : ::readlink(d.link.c_str(), buf, sizeof buf - 1);
        if (n > 0 && std::string(buf, static_cast<size_t>(n)) == d.target && ::unlink(d.link.c_str()) == 0) log("schijfletter " + d.letter + ": weggehaald");
    }
    log(std::string("klaar (exitcode ") + (have_rc ? std::to_string(rc) : std::string("gestopt")) + ")");
    return have_rc ? (rc & 0xFF) : 0;
}

int show_plan(int argc, char** argv) {
    std::string profile, tp, patches, user, shm, session, control;
    bool log_full = false, make_view = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) { if (i + 1 < argc) v = argv[++i]; };
        if (a == "--tp") next(tp);
        else if (a == "--patches") next(patches);
        else if (a == "--user") next(user);
        else if (a == "--shm-source") next(shm);
        else if (a == "--control-source") next(control);
        else if (a == "--session-id") next(session);
        else if (a == "--log-full") log_full = true;
        else if (a == "--make-view") make_view = true;   // ook bij --plan de view maken of verversen ([Launch] View)
        else if (profile.empty() && a[0] != '-') profile = a;
        else { std::fprintf(stderr, "rg-direct: onbekende optie %s\n", a.c_str()); return 64; }
    }
    if (profile.empty()) { std::fprintf(stderr, "rg-direct --plan PROFIEL\n"); return 64; }
    const L::EnvMap env = L::environ_map();
    L::DirectPaths p = L::default_direct_paths(env);
    if (!tp.empty()) p.tp = tp;
    p.dirs = ravengun::gamesettings::default_dirs("");
    if (!patches.empty()) p.dirs.shipped = patches;
    if (!user.empty()) p.dirs.user = user;
    L::DirectOptions o;
    o.base_env = env;
    o.shm_source = shm;
    o.control_source = control;
    o.log_full = log_full;
    o.session_id = session;
    o.make_view = make_view;
    L::DirectPlan plan;
    std::string why, iwhy;
    const L::Verdict v = L::plan_direct(profile, p, o, plan, why);
    L::DirectInput in;
    const L::Verdict iv = L::direct_input(profile, p, in, iwhy);
    ravengun::json::Writer w;
    w.begin_object().key("verdict").value(L::verdict_name(v)).key("why").value(why);
    w.key("input").begin_object().key("verdict").value(L::verdict_name(iv)).key("why").value(iwhy).key("route").value(in.route);
    w.key("layout").value(in.layout).key("board").value(in.board).key("transport").value(in.transport).key("control_pipe").value(in.control_pipe).end_object();
    w.key("launch_mode").value(L::launch_mode(profile, p));
    w.key("plan").raw(v == L::Verdict::Ok ? L::plan_to_json(plan) : std::string("null"));
    w.end_object();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

// --tp-refs: per game in patches/ welke bestanden uit de TeknoParrot-map de start nog gebruikt (het doel: geen enkel; docs/loskoppelen-per-game.md,
// "Centrale afhankelijkheden van de TeknoParrot-map"). De route zoals de daemon hem kiest (games::native_route, unity_route, anders
// direct). Native: de "@tpui/..."-verwijzingen in native.json. Unity: de map van Proton (in de TeknoParrot-map of die van TeknoParrotUi
// telt mee). Direct: plan_tp_refs van het plan, of bij een plan dat niet lukt de paden uit de TeknoParrot-map in de melding.
// Alleen lezen; geen view maken. Met --expect-free A,B,...: exitcode 1 als een van die games niet "ok" is of nog iets verwijst.
int show_tp_refs(int argc, char** argv) {
    std::string tp, patches, user, expect;
    bool as_json = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) { if (i + 1 < argc) v = argv[++i]; };
        if (a == "--tp") next(tp);
        else if (a == "--patches") next(patches);
        else if (a == "--user") next(user);
        else if (a == "--expect-free") next(expect);
        else if (a == "--json") as_json = true;
        else { std::fprintf(stderr, "rg-direct: onbekende optie %s\n", a.c_str()); return 64; }
    }
    const L::EnvMap env = L::environ_map();
    L::DirectPaths p = L::default_direct_paths(env);
    if (!tp.empty()) p.tp = tp;
    p.dirs = ravengun::gamesettings::default_dirs("");
    if (!patches.empty()) p.dirs.shipped = patches;
    if (!user.empty()) p.dirs.user = user;
    ravengun::games::Config c;
    c.dirs = p.dirs;
    c.native.patches = p.dirs.shipped;
    c.native.user_patches = p.dirs.user;
    c.unity.patches = p.dirs.shipped;
    c.unity.user_patches = p.dirs.user;
    const std::string tpui_data = p.home + "/.local/share/TeknoParrotUI";   // de downloads van TeknoParrotUi (Proton)
    const std::string proton = env_get(env, "RAVENGUN_UNITY_PROTON").empty() ? L::default_unity_proton(p.data, p.home) : env_get(env, "RAVENGUN_UNITY_PROTON");
    auto onder = [](const std::string& pad, const std::string& map) {
        return !map.empty() && (pad == map || (pad.size() > map.size() && pad.compare(0, map.size(), map) == 0 && pad[map.size()] == '/'));
    };
    // Paden uit de TeknoParrot-map in een tekst (een melding): de stukken tot een spatie of een haakje.
    auto uit_tekst = [&](const std::string& s) {
        std::vector<std::string> out;
        for (size_t i = s.find(p.tp + "/"); !p.tp.empty() && i != std::string::npos; i = s.find(p.tp + "/", i + 1)) {
            size_t j = i + p.tp.size() + 1;
            while (j < s.size() && s[j] != ' ' && s[j] != ')' && s[j] != ';' && s[j] != '\n') ++j;
            out.push_back(s.substr(i + p.tp.size() + 1, j - i - p.tp.size() - 1));
        }
        return out;
    };
    struct Rij { std::string game, route, verdict, why; std::vector<std::string> refs; };
    std::vector<Rij> rijen;
    for (const std::string& name : ravengun::gamesettings::list_games(p.dirs)) {
        Rij r;
        r.game = name;
        if (ravengun::games::native_route(c, name)) {
            r.route = "native";
            r.verdict = "ok";
            const std::string text = L::profile_text(c.native, name);
            for (size_t i = text.find("@tpui/"); i != std::string::npos; i = text.find("@tpui/", i + 1)) {
                size_t j = i + 6;
                while (j < text.size() && text[j] != '"' && text[j] != ':' && text[j] != ' ') ++j;
                r.refs.push_back(text.substr(i + 6, j - i - 6));
            }
            if (text.find("@tpui\"") != std::string::npos) r.refs.push_back(".");
        } else if (ravengun::games::unity_route(c, name, "")) {
            r.route = "unity";
            r.verdict = ::access((proton + "/proton").c_str(), X_OK) == 0 ? "ok" : "fallback";
            r.why = "Proton: " + proton;
            if (onder(proton, p.tp)) r.refs.push_back(proton.substr(p.tp.size() + 1));
            if (onder(proton, tpui_data)) r.refs.push_back("~/.local/share/TeknoParrotUI/" + proton.substr(tpui_data.size() + 1));
        } else if (L::launch_mode(name, p) == "direct") {
            r.route = "direct";
            L::DirectOptions o;
            o.base_env = env;
            o.session_id = "tp-refs";
            L::DirectPlan plan;
            const L::Verdict v = L::plan_direct(name, p, o, plan, r.why);
            r.verdict = L::verdict_name(v);
            r.refs = v == L::Verdict::Ok ? L::plan_tp_refs(plan, p.tp) : uit_tekst(r.why);
        } else {
            r.route = "geen";   // geen eigen route ([Launch] Mode is niet direct en geen native.json of unity.json): start niet
            r.verdict = "-";
        }
        std::sort(r.refs.begin(), r.refs.end());
        r.refs.erase(std::unique(r.refs.begin(), r.refs.end()), r.refs.end());
        rijen.push_back(std::move(r));
    }
    int rc = 0;
    std::vector<std::string> fout;
    std::vector<std::string> verwacht;
    {
        std::string cur;
        for (char ch : expect + ",") {
            if (ch != ',') { if (ch != ' ') cur += ch; continue; }
            if (!cur.empty()) verwacht.push_back(cur);
            cur.clear();
        }
    }
    for (const std::string& g : verwacht) {
        auto it = std::find_if(rijen.begin(), rijen.end(), [&](const Rij& r) { return r.game == g; });
        if (it == rijen.end()) fout.push_back(g + ": geen game in patches/");
        else if (it->verdict != "ok" || !it->refs.empty())
            fout.push_back(g + ": " + it->verdict + (it->refs.empty() ? "" : ", " + std::to_string(it->refs.size()) + " verwijzing(en)") +
                           (it->why.empty() ? "" : " (" + it->why + ")"));
    }
    if (!fout.empty()) rc = 1;
    if (as_json) {
        ravengun::json::Writer w;
        w.begin_object().key("tp").value(p.tp).key("games").begin_array();
        for (const Rij& r : rijen) {
            w.begin_object().key("game").value(r.game).key("route").value(r.route).key("verdict").value(r.verdict).key("why").value(r.why);
            w.key("refs").begin_array();
            for (const auto& x : r.refs) w.value(x);
            w.end_array().end_object();
        }
        w.end_array().key("expect_free_failures").begin_array();
        for (const auto& f : fout) w.value(f);
        w.end_array().end_object();
        std::printf("%s\n", w.str().c_str());
        return rc;
    }
    size_t vrij = 0, met = 0, niet_ok = 0;
    std::printf("Verwijzingen naar de TeknoParrot-map (%s) per game:\n", p.tp.c_str());
    for (const Rij& r : rijen) {
        if (r.route == "geen") continue;
        (!r.refs.empty() ? met : r.verdict == "ok" ? vrij : niet_ok) += 1;
        std::string lijst;
        for (const auto& x : r.refs) lijst += (lijst.empty() ? "" : ", ") + x;
        std::printf("  %-26s %-7s %-9s %2zu  %s\n", r.game.c_str(), r.route.c_str(), r.verdict.c_str(), r.refs.size(), lijst.c_str());
    }
    std::string geen;
    for (const Rij& r : rijen) if (r.route == "geen") geen += (geen.empty() ? "" : ", ") + r.game;
    std::printf("zonder TeknoParrot-map (ok, 0 verwijzingen): %zu; met verwijzingen: %zu; plan lukt hier niet (zonder verwijzing in de melding): %zu; "
                "geen eigen route: %s\n", vrij, met, niet_ok, geen.empty() ? "-" : geen.c_str());
    for (const auto& f : fout) std::printf("VERWACHT ZONDER TP-MAP, MAAR: %s\n", f.c_str());
    return rc;
}

int jvs_check() {
    const J::Board b = J::default_board();
    std::string line;
    while (std::getline(std::cin, line)) {
        J::Bytes in, rest;
        if (!J::from_hex(line, in)) { std::printf("?\n"); continue; }
        std::string out;
        for (const auto& fr : J::frames(in, rest)) {
            out += J::hex(fr) + ":";
            if (fr.size() >= 3) {
                const auto a = J::answer(b, J::Bytes(fr.begin() + 2, fr.end() - 1));
                out += a ? J::hex(J::pack(*a)) : std::string("reset");
            }
            out += " ";
        }
        std::printf("%srest=%s\n", out.c_str(), J::hex(rest).c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (const char* s = std::getenv("RG_DIRECT_SHM_PATH"); s && *s) g_shm_path = s;
    if (argc >= 3 && std::strcmp(argv[1], "--run") == 0) return run_plan(argv[2]);
    /* Alleen de prefixstappen van een plan uitvoeren (proeven; tests/prefix_setup_test.sh). */
    if (argc >= 3 && std::strcmp(argv[1], "--alleen-stappen") == 0) {
        std::string tekst;
        {
            std::ifstream f(argv[2], std::ios::binary);
            if (!f) { log(std::string("plan ") + argv[2] + " niet te lezen"); return 2; }
            std::ostringstream ss;
            ss << f.rdbuf();
            tekst = ss.str();
        }
        L::DirectPlan plan;
        std::string err;
        if (!L::plan_from_json(tekst, plan, err)) { log(err); return 2; }
        const L::DirectPaths wp = L::default_direct_paths(L::environ_map());
        /* Dezelfde controle als --run en dezelfde stopsignalen (review 13, ronde 2, punt 6 en 4): deze weg zit in het
         * programma dat ook in productie staat, en draait wine. */
        if (const int rc = controle_voor_start(plan, wp, "--alleen-stappen"); rc != 0) return rc;
        zet_stopsignalen();
        return voer_prefixstappen(plan, plan.env, wp);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--plan") == 0) return show_plan(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--jvs-check") == 0) return jvs_check();
    if (argc >= 2 && std::strcmp(argv[1], "--tp-refs") == 0) return show_tp_refs(argc, argv);
    std::fprintf(stderr, "gebruik: rg-direct --run PLAN.json | --plan PROFIEL [--patches DIR] [--user DIR] [--tp DIR] | --jvs-check\n"
                         "       rg-direct --tp-refs [--patches DIR] [--user DIR] [--tp DIR] [--json] [--expect-free GAME,...]\n");
    return 64;
}
