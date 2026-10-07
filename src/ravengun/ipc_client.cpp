#include "ravengun/ipc_client.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ravengun/ipc_protocol.hpp"

namespace ravengun::ipc {

std::string runtime_dir() {
    const char* x = std::getenv("XDG_RUNTIME_DIR");
    if (!x || x[0] != '/') return {};
    return std::string(x) + "/ravengun";
}
std::string socket_path() { auto d = runtime_dir(); return d.empty() ? d : d + "/ravengund.sock"; }
std::string lock_path() { auto d = runtime_dir(); return d.empty() ? d : d + "/ravengund.lock"; }

Client::~Client() { close(); }

void Client::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    buf_.clear();
}

bool Client::connect(const std::string& path, int timeout_ms) {
    close();
    (void)timeout_ms;  // connect op een lokale Unix-socket blokkeert niet merkbaar
    sockaddr_un a{};
    if (path.empty() || path.size() >= sizeof a.sun_path) { err_ = "ongeldig socketpad"; return false; }
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) { err_ = std::strerror(errno); return false; }
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        err_ = std::strerror(errno);
        close();
        return false;
    }
    return true;
}

bool Client::send_line(const std::string& line) {
    if (fd_ < 0) { err_ = "niet verbonden"; return false; }
    std::string s = line + "\n";
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::send(fd_, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            err_ = std::strerror(errno);
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

std::optional<std::string> Client::read_line(int timeout_ms) {
    while (true) {
        auto nl = buf_.find('\n');
        if (nl != std::string::npos) {
            std::string line = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            return line;
        }
        if (fd_ < 0) { err_ = "niet verbonden"; return std::nullopt; }
        if (buf_.size() > kMaxLineBytes * 4) { err_ = "antwoordregel te lang"; return std::nullopt; }
        pollfd p{fd_, POLLIN, 0};
        int r = ::poll(&p, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR) { err_ = "onderbroken"; return std::nullopt; }
            err_ = std::strerror(errno);
            return std::nullopt;
        }
        if (r == 0) { err_ = "time-out"; return std::nullopt; }
        char tmp[8192];
        ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
        if (n == 0) { err_ = "verbinding gesloten door de daemon"; return std::nullopt; }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            err_ = std::strerror(errno);
            return std::nullopt;
        }
        buf_.append(tmp, static_cast<size_t>(n));
    }
}

}  // namespace ravengun::ipc
