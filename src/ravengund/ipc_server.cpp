#include "ipc_server.hpp"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "hid_thread.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun;

namespace {

constexpr size_t kMaxOut = 256 * 1024;      // daarboven wordt een client verbroken
constexpr size_t kWatchSkipAbove = 16 * 1024;  // watch-frame overslaan als er nog zoveel wacht
constexpr int kMaxClients = 16;
constexpr auto kJobTimeout = std::chrono::milliseconds(500);

struct Client {
    int fd = -1;
    std::string in, out;
    bool watching = false;
    bool full_ids = false;
    int64_t period_ns = 0;
    int64_t next_due = 0;
    uint64_t last_seq = 0;
    uint64_t skipped = 0;
    std::array<uint64_t, 32> press_base{}, release_base{};
    bool closing = false;  // na het legen van out sluiten
};

std::string run_job(SharedState& sh, SharedState::Job j, std::string& err) {
    auto f = sh.post(std::move(j));
    if (f.wait_for(kJobTimeout) != std::future_status::ready) { err = "HID-thread reageert niet binnen 500 ms"; return {}; }
    try {
        return f.get();
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

void write_edges(json::Writer& w, const std::array<uint64_t, 32>& now, const std::array<uint64_t, 32>& base) {
    w.begin_object();
    for (const auto& b : core::kButtonNames) {
        int bit = __builtin_ctz(b.bit);
        uint64_t d = now[bit] - base[bit];
        if (d) w.key(b.name).value(static_cast<unsigned long long>(d));
    }
    w.end_object();
}

std::string handle(SharedState& sh, Client& c, const std::string& line) {
    auto pr = ipc::parse_request(line);
    if (auto* e = std::get_if<ipc::Error>(&pr)) return ipc::error_response(*e);
    const ipc::Request& r = std::get<ipc::Request>(pr);
    std::string err;
    switch (r.cmd) {
        case ipc::Cmd::Status: {
            bool full = r.full_ids;
            std::string res = run_job(sh, [full](HidContext& h) { return h.status_json(full); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::Sample: {
            Snapshot s = sh.get();
            json::Writer w;
            core::write_sample_json(w, s.sample, r.full_ids);
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Recenter: {
            std::string res = run_job(sh, [](HidContext& h) { return std::string(h.recenter() ? "1" : "0"); }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            if (res != "1")
                return ipc::error_response({"unavailable", "geen geldige, actuele oriëntatie: is de Move verbonden?", r.id_json});
            Snapshot s = sh.get();
            json::Writer w;
            w.begin_object().key("recentered").value(true).key("sample");
            core::write_sample_json(w, s.sample, false);
            w.end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::Watch: {
            Snapshot s = sh.get();
            c.watching = true;
            c.full_ids = r.full_ids;
            c.period_ns = static_cast<int64_t>(1e9 / r.rate_hz);
            c.next_due = 0;
            c.last_seq = 0;
            c.press_base = s.press_total;
            c.release_base = s.release_total;
            json::Writer w;
            w.begin_object().key("watching").value(true).key("rate_hz").value(r.rate_hz).end_object();
            return ipc::ok_response(r.id_json, w.str());
        }
        case ipc::Cmd::SetFilter: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::OneEuroConfig f = h.filter();
                if (rq.filter_enabled) f.enabled = *rq.filter_enabled;
                if (rq.min_cutoff_hz) f.min_cutoff_hz = *rq.min_cutoff_hz;
                if (rq.beta) f.beta = *rq.beta;
                if (rq.d_cutoff_hz) f.d_cutoff_hz = *rq.d_cutoff_hz;
                h.set_filter(f);
                json::Writer w;
                w.begin_object().key("enabled").value(f.enabled).key("min_cutoff_hz").value(f.min_cutoff_hz)
                    .key("beta").value(f.beta).key("d_cutoff_hz").value(f.d_cutoff_hz).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
        case ipc::Cmd::SetAim: {
            ipc::Request rq = r;
            std::string res = run_job(sh, [rq](HidContext& h) {
                core::AimConfig a = h.aim();
                if (rq.h_edge_deg) a.h_edge_deg = *rq.h_edge_deg;
                if (rq.v_edge_deg) a.v_edge_deg = *rq.v_edge_deg;
                h.set_aim(a);
                json::Writer w;
                w.begin_object().key("h_edge_deg").value(a.h_edge_deg).key("v_edge_deg").value(a.v_edge_deg).end_object();
                return w.str();
            }, err);
            if (!err.empty()) return ipc::error_response({"internal", err, r.id_json});
            return ipc::ok_response(r.id_json, res);
        }
    }
    return ipc::error_response({"internal", "onbekende toestand", r.id_json});
}

void flush(Client& c) {
    while (!c.out.empty()) {
        ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) { c.out.erase(0, static_cast<size_t>(n)); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n < 0 && errno == EINTR) continue;
        c.closing = true;
        c.out.clear();
        return;
    }
}

}  // namespace

int ipc_listen(const std::string& path) {
    sockaddr_un a{};
    if (path.size() >= sizeof a.sun_path) { logf("fout", "socketpad te lang"); return -1; }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { logf("fout", "socket(): %s", std::strerror(errno)); return -1; }
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
    ::unlink(path.c_str());  // alleen aangeroepen terwijl wij de lockfile houden
    mode_t old = ::umask(0177);
    int rc = ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    ::umask(old);
    if (rc != 0) { logf("fout", "bind(%s): %s", path.c_str(), std::strerror(errno)); ::close(fd); return -1; }
    ::chmod(path.c_str(), 0600);
    if (::listen(fd, 8) != 0) { logf("fout", "listen(): %s", std::strerror(errno)); ::close(fd); return -1; }
    return fd;
}

void ipc_thread_main(SharedState& sh, int lfd, int wake_fd) {
    std::list<Client> clients;
    while (sh.running.load()) {
        int64_t now = mono_ns();
        int timeout_ms = 200;
        for (auto& c : clients) {
            if (c.watching) {
                int64_t d = c.next_due - now;
                int ms = d <= 0 ? 0 : static_cast<int>((d + 999'999) / 1'000'000);
                if (ms < timeout_ms) timeout_ms = ms;
            }
        }
        std::vector<pollfd> pf;
        pf.push_back({lfd, POLLIN, 0});
        pf.push_back({wake_fd, POLLIN, 0});
        for (auto& c : clients) pf.push_back({c.fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
        int r = ::poll(pf.data(), pf.size(), timeout_ms);
        if (r < 0 && errno != EINTR) { logf("fout", "poll(): %s", std::strerror(errno)); break; }
        if (!sh.running.load() || (pf[1].revents & POLLIN)) break;

        if (pf[0].revents & POLLIN) {
            while (true) {
                int cfd = ::accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
                if (cfd < 0) break;
                // Alleen dezelfde gebruiker (de socket is 0600, dit is een tweede controle).
                ucred cr{};
                socklen_t len = sizeof cr;
                if (::getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cr, &len) != 0 || cr.uid != ::getuid() ||
                    static_cast<int>(clients.size()) >= kMaxClients) {
                    ::close(cfd);
                    continue;
                }
                Client c;
                c.fd = cfd;
                clients.push_back(std::move(c));
            }
        }

        size_t idx = 2;
        for (auto& c : clients) {
            short re = idx < pf.size() && pf[idx].fd == c.fd ? pf[idx].revents : 0;
            ++idx;
            if (re & (POLLERR | POLLNVAL)) { c.closing = true; c.out.clear(); continue; }
            if (re & (POLLIN | POLLHUP)) {
                char buf[8192];
                while (true) {
                    ssize_t n = ::recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
                    if (n > 0) { c.in.append(buf, static_cast<size_t>(n)); continue; }
                    if (n == 0) { c.closing = true; }
                    break;
                }
                size_t nl;
                while ((nl = c.in.find('\n')) != std::string::npos) {
                    std::string line = c.in.substr(0, nl);
                    c.in.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;
                    c.out += handle(sh, c, line);
                    c.out += '\n';
                }
                if (c.in.size() > ipc::kMaxLineBytes) {
                    c.out += ipc::error_response({"line_too_long", "verzoek groter dan 64 KiB; verbinding gesloten", ""});
                    c.out += '\n';
                    c.in.clear();
                    c.closing = true;
                }
            }
        }

        now = mono_ns();
        Snapshot snap;
        bool have_snap = false;
        for (auto& c : clients) {
            if (!c.watching || c.closing || now < c.next_due) continue;
            if (!have_snap) { snap = sh.get(); have_snap = true; }
            c.next_due = now + c.period_ns;
            if (snap.sample.seq == c.last_seq) continue;  // niets nieuws
            if (c.out.size() > kWatchSkipAbove) { ++c.skipped; continue; }  // trage client: frame overslaan
            json::Writer w;
            w.begin_object().key("sample");
            core::write_sample_json(w, snap.sample, c.full_ids);
            w.key("edges_since_last_frame").begin_object();
            w.key("pressed");
            write_edges(w, snap.press_total, c.press_base);
            w.key("released");
            write_edges(w, snap.release_total, c.release_base);
            w.end_object();
            w.key("skipped_frames").value(static_cast<unsigned long long>(c.skipped));
            w.end_object();
            c.press_base = snap.press_total;
            c.release_base = snap.release_total;
            c.last_seq = snap.sample.seq;
            c.out += ipc::event_line("sample", w.str());
            c.out += '\n';
        }

        for (auto it = clients.begin(); it != clients.end();) {
            flush(*it);
            if (it->out.size() > kMaxOut || (it->closing && it->out.empty())) {
                ::close(it->fd);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& c : clients) ::close(c.fd);
    logf("info", "IPC-thread gestopt");
}

}  // namespace ravengund
