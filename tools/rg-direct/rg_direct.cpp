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
#include "ravengun/launch/jvs_pipe.hpp"
#include "ravengun/launch/native.hpp"

namespace L = ravengun::launch;
namespace J = ravengun::launch::jvspipe;

namespace {

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
Child spawn(const std::vector<std::string>& argv, const std::string& cwd, const L::EnvMap& env, bool merge_stderr, bool stdout_null) {
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
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF);
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
    {
        char self[4096];
        const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
        const std::string me = n > 0 ? std::string(self, static_cast<size_t>(n)) : "rg-direct";
        log("launcher: " + me + " sha256 " + L::sha256_file(me) + " (C++, gestart door ravengund)");
        log("plan: " + plan_file + " sha256 " + L::sha256_file(plan_file));
    }
    struct sigaction sa{};
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
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
        log("lader gestopt (exitcode " + std::to_string(rc) + "); de game volgen");
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
    if (argc >= 2 && std::strcmp(argv[1], "--plan") == 0) return show_plan(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--jvs-check") == 0) return jvs_check();
    if (argc >= 2 && std::strcmp(argv[1], "--tp-refs") == 0) return show_tp_refs(argc, argv);
    std::fprintf(stderr, "gebruik: rg-direct --run PLAN.json | --plan PROFIEL [--patches DIR] [--user DIR] [--tp DIR] | --jvs-check\n"
                         "       rg-direct --tp-refs [--patches DIR] [--user DIR] [--tp DIR] [--json] [--expect-free GAME,...]\n");
    return 64;
}
