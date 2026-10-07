#include "web_server.hpp"

#include <cerrno>
#include <cstring>
#include <list>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "commands.hpp"
#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun;
using namespace ravengun::web;

namespace {

constexpr int kMaxConns = 32;
constexpr int kMaxSse = 8;
constexpr double kStreamHz = 30.0;
constexpr int64_t kReadTimeoutNs = 10'000'000'000;   // verzoek moet binnen 10 s compleet zijn
constexpr int64_t kStallTimeoutNs = 15'000'000'000;  // SSE-client die 15 s niets afneemt
constexpr int64_t kKeepaliveNs = 15'000'000'000;
constexpr int64_t kWriteTimeoutNs = 10'000'000'000;

struct Conn {
    int fd = -1;
    std::string in, out;
    int64_t created = 0;
    int64_t last_progress = 0;  // laatste keer dat er bytes verstuurd zijn
    bool closing = false;       // sluiten zodra out leeg is
    bool sse = false;
    SseBuffer sse_buf;
    Throttle throttle{kStreamHz};
    uint64_t last_seq = 0, last_optical = 0;
    int64_t last_keepalive = 0;
    bool sent_any_frame = false;
};

void send_some(Conn& c, int64_t now) {
    auto flush = [&](std::string& buf, auto consume) {
        while (!buf.empty()) {
            ssize_t n = ::send(c.fd, buf.data(), buf.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) { consume(static_cast<size_t>(n)); c.last_progress = now; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            c.closing = true;
            c.out.clear();
            c.sse_buf.consume(c.sse_buf.pending().size());
            return;
        }
    };
    flush(c.out, [&](size_t n) { c.out.erase(0, n); });
    if (c.out.empty() && c.sse) {
        // Kopieer niet: stuur rechtstreeks uit de SSE-buffer.
        while (!c.sse_buf.empty()) {
            ssize_t n = ::send(c.fd, c.sse_buf.pending().data(), c.sse_buf.pending().size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) { c.sse_buf.consume(static_cast<size_t>(n)); c.last_progress = now; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            c.closing = true;
            c.sse_buf.consume(c.sse_buf.pending().size());
            break;
        }
    }
}

bool has_pending(const Conn& c) { return !c.out.empty() || !c.sse_buf.empty(); }

std::string stream_frame(const Snapshot& snap, uint64_t skipped) {
    json::Writer w;
    w.begin_object().key("sample");
    core::write_sample_json(w, snap.sample, false);  // controller-ID geredigeerd
    w.key("skipped_frames").value(static_cast<unsigned long long>(skipped));
    w.end_object();
    return sse_frame("sample", w.str());
}

}  // namespace

int web_listen(const WebConfig& cfg) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) { logf("fout", "web: socket(): %s", std::strerror(errno)); return -1; }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(cfg.port));
    a.sin_addr.s_addr = htonl(cfg.lan ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        logf("fout", "web: bind(%s:%d): %s", cfg.lan ? "0.0.0.0" : "127.0.0.1", cfg.port, std::strerror(errno));
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) != 0) { logf("fout", "web: listen(): %s", std::strerror(errno)); ::close(fd); return -1; }
    return fd;
}

void web_thread_main(SharedState& sh, WebConfig cfg, int lfd, int wake_fd) {
    Backend be;
    be.command = [&sh](const std::string& line) { return execute_line(sh, line); };
    WebApp app(cfg, be);
    std::list<Conn> conns;
    int sse_count = 0;
    uint64_t total_skipped = 0;

    while (sh.running.load()) {
        int64_t now = mono_ns();
        int timeout_ms = 200;
        for (auto& c : conns)
            if (c.sse && !c.closing) {
                int64_t d = c.throttle.next() - now;
                int ms = d <= 0 ? 0 : static_cast<int>((d + 999'999) / 1'000'000);
                if (ms < timeout_ms) timeout_ms = ms;
            }
        std::vector<pollfd> pf;
        pf.push_back({lfd, POLLIN, 0});
        pf.push_back({wake_fd, POLLIN, 0});
        for (auto& c : conns) pf.push_back({c.fd, static_cast<short>(POLLIN | (has_pending(c) ? POLLOUT : 0)), 0});
        int r = ::poll(pf.data(), pf.size(), timeout_ms);
        if (r < 0 && errno != EINTR) { logf("fout", "web: poll(): %s", std::strerror(errno)); break; }
        if (!sh.running.load() || (pf[1].revents & POLLIN)) break;
        now = mono_ns();

        if (pf[0].revents & POLLIN) {
            while (true) {
                int cfd = ::accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
                if (cfd < 0) break;
                if (static_cast<int>(conns.size()) >= kMaxConns) { ::close(cfd); continue; }
                int one = 1;
                ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                Conn c;
                c.fd = cfd;
                c.created = c.last_progress = now;
                conns.push_back(std::move(c));
            }
        }

        size_t idx = 2;
        for (auto& c : conns) {
            short re = idx < pf.size() && pf[idx].fd == c.fd ? pf[idx].revents : 0;
            ++idx;
            if (re & (POLLERR | POLLNVAL)) { c.closing = true; c.out.clear(); c.sse_buf.consume(c.sse_buf.pending().size()); continue; }
            if (!(re & (POLLIN | POLLHUP)) || c.closing) continue;
            char buf[4096];
            while (true) {
                ssize_t n = ::recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
                if (n > 0) {
                    if (!c.sse && c.out.empty()) c.in.append(buf, static_cast<size_t>(n));
                    continue;  // SSE-clients hoeven niets te sturen; invoer wordt genegeerd
                }
                if (n == 0) { c.closing = true; c.out.clear(); c.sse_buf.consume(c.sse_buf.pending().size()); }
                break;
            }
            if (c.closing || c.sse || c.in.empty() || !c.out.empty()) continue;
            ParseResult pr = parse_http(c.in);
            if (pr.state == ParseState::Incomplete) continue;
            c.in.clear();
            HttpResponse resp;
            if (pr.state != ParseState::Complete) {
                resp.status = pr.error_status;
                resp.body = "{\"v\":1,\"ok\":false,\"error\":{\"code\":\"bad_request\",\"message\":\"ongeldig HTTP-verzoek\"}}";
                c.out = serialize(resp);
                c.closing = true;
                continue;
            }
            resp = app.handle(pr.request);
            if (resp.sse) {
                if (sse_count >= kMaxSse) {
                    resp = HttpResponse{};
                    resp.status = 503;
                    resp.body = "{\"v\":1,\"ok\":false,\"error\":{\"code\":\"unavailable\",\"message\":\"te veel open streams\"}}";
                } else {
                    c.sse = true;
                    ++sse_count;
                    c.out = serialize(resp) + "retry: 2000\n\n";
                    c.last_keepalive = now;
                    continue;
                }
            }
            c.out = serialize(resp);
            c.closing = true;
        }

        // SSE: hoogstens 30 Hz, alleen bij nieuwe data; een trage client krijgt frames overgeslagen.
        Snapshot snap;
        bool have_snap = false;
        for (auto& c : conns) {
            if (!c.sse || c.closing || !c.out.empty()) continue;
            if (!c.throttle.due(now)) continue;
            if (!have_snap) { snap = sh.get(); have_snap = true; }
            bool fresh = !c.sent_any_frame || snap.sample.seq != c.last_seq || snap.optical_updates != c.last_optical;
            if (fresh) {
                auto res = c.sse_buf.push(stream_frame(snap, c.sse_buf.skipped()));
                if (res == SseBuffer::Push::Overflow) { c.closing = true; continue; }
                if (res == SseBuffer::Push::Queued) {
                    c.sent_any_frame = true;
                    c.last_seq = snap.sample.seq;
                    c.last_optical = snap.optical_updates;
                    c.last_keepalive = now;
                } else {
                    ++total_skipped;
                }
            } else if (c.sse_buf.empty() && now - c.last_keepalive >= kKeepaliveNs) {
                c.sse_buf.push(": keepalive\n\n");
                c.last_keepalive = now;
            }
        }

        for (auto it = conns.begin(); it != conns.end();) {
            send_some(*it, now);
            bool drop = false;
            if (it->closing && !has_pending(*it)) drop = true;
            else if (!it->sse && it->out.empty() && now - it->created > kReadTimeoutNs) drop = true;  // trage/halve request
            else if (has_pending(*it) && now - it->last_progress > (it->sse ? kStallTimeoutNs : kWriteTimeoutNs)) drop = true;
            if (drop) {
                if (it->sse) --sse_count;
                ::close(it->fd);
                it = conns.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& c : conns) ::close(c.fd);
    (void)total_skipped;
    logf("info", "web-thread gestopt");
}

}  // namespace ravengund
