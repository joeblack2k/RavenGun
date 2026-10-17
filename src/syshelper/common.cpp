// SPDX-License-Identifier: GPL-3.0-or-later
#include "syshelper/common.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ravengun::syshelper {

namespace {
bool is_hex_upper(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }
}  // namespace

bool is_bt_dir_name(const std::string& a) {
    if (a.size() != 17) return false;
    for (size_t i = 0; i < 17; ++i) {
        if (i % 3 == 2) {
            if (a[i] != ':') return false;
        } else if (!is_hex_upper(a[i])) {
            return false;
        }
    }
    return true;
}

bool valid_bt_address(const std::string& a) {
    return is_bt_dir_name(a) && a != "00:00:00:00:00:00" && a != "FF:FF:FF:FF:FF:FF";
}

bool valid_user_name(const std::string& n) {
    if (n.empty() || n.size() > 32 || n == "root") return false;
    for (size_t i = 0; i < n.size(); ++i) {
        const char c = n[i];
        const bool first_ok = (c >= 'a' && c <= 'z') || c == '_';
        const bool rest_ok = first_ok || (c >= '0' && c <= '9') || c == '-';
        if (i == 0 ? !first_ok : !rest_ok) return false;
    }
    return true;
}

bool read_fd_all(int fd, std::string& out) {
    out.clear();
    char buf[8192];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return true;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > (64u << 20)) return false;  // niets hier is groter dan 64 MB
    }
}

bool read_file_nofollow(const std::string& path, std::string& out) {
    out.clear();
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st{};
    const bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && read_fd_all(fd, out);
    ::close(fd);
    return ok;
}

bool write_file_atomic(const std::string& dir, const std::string& name, const std::string& data, mode_t mode, uid_t uid, gid_t gid,
                       std::string& err) {
    if (name.empty() || name.find('/') != std::string::npos) { err = "invalid file name"; return false; }
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) { err = dir + ": " + std::strerror(errno); return false; }
    std::string tmp;
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        char suffix[48];
        std::snprintf(suffix, sizeof suffix, ".rg-%d-%d-%ld", static_cast<int>(::getpid()), attempt, static_cast<long>(::time(nullptr)));
        tmp = std::string(".") + name + suffix;
        fd = ::openat(dfd, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) { err = dir + "/" + name + ": " + std::strerror(errno); ::close(dfd); return false; }
    errno = 0;
    bool ok = true;
    size_t off = 0;
    while (ok && off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) { if (errno == EINTR) continue; ok = false; break; }
        off += static_cast<size_t>(n);
    }
    if (ok && (uid != static_cast<uid_t>(-1) || gid != static_cast<gid_t>(-1))) ok = ::fchown(fd, uid, gid) == 0;
    if (ok) ok = ::fchmod(fd, mode) == 0;
    if (ok) ok = ::fsync(fd) == 0;
    const int saved = errno;
    ::close(fd);
    if (ok) ok = ::renameat(dfd, tmp.c_str(), dfd, name.c_str()) == 0;
    if (!ok) {
        err = dir + "/" + name + ": " + std::strerror(errno ? errno : saved);
        ::unlinkat(dfd, tmp.c_str(), 0);
        ::close(dfd);
        return false;
    }
    ::fsync(dfd);
    ::close(dfd);
    return true;
}

bool make_dirs(const std::string& path, mode_t mode, std::string& err) {
    if (path.empty() || path[0] != '/') { err = "relative path: " + path; return false; }
    size_t pos = 1;
    while (pos <= path.size()) {
        const size_t next = path.find('/', pos);
        const std::string part = path.substr(0, next == std::string::npos ? path.size() : next);
        if (!part.empty() && ::mkdir(part.c_str(), mode) != 0 && errno != EEXIST) {
            err = part + ": " + std::strerror(errno);
            return false;
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return true;
}

int run_program(const std::vector<std::string>& argv, std::string* out, int timeout_ms) {
    if (argv.empty() || argv[0].empty() || argv[0][0] != '/') return -1;  // alleen absolute paden, nooit PATH
    int pipefd[2] = {-1, -1};
    if (out && ::pipe2(pipefd, O_CLOEXEC) != 0) return -1;
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    char* empty_env[] = {nullptr};
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (out) { ::close(pipefd[0]); ::close(pipefd[1]); }
        return -1;
    }
    if (pid == 0) {
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) ::dup2(devnull, 0);
        if (out) { ::dup2(pipefd[1], 1); ::dup2(pipefd[1], 2); }
        else if (devnull >= 0) { ::dup2(devnull, 1); ::dup2(devnull, 2); }
        ::execve(args[0], args.data(), empty_env);
        ::_exit(127);
    }
    bool timed_out = false;
    if (out) {
        ::close(pipefd[1]);
        out->clear();
        const time_t start = ::time(nullptr);
        char buf[4096];
        for (;;) {
            struct pollfd p{pipefd[0], POLLIN, 0};
            const int r = ::poll(&p, 1, 500);
            if (r > 0) {
                const ssize_t n = ::read(pipefd[0], buf, sizeof buf);
                if (n <= 0) break;
                if (out->size() < (1u << 20)) out->append(buf, static_cast<size_t>(n));
            }
            if (::time(nullptr) - start > timeout_ms / 1000) { timed_out = true; break; }
        }
        ::close(pipefd[0]);
    }
    if (timed_out) ::kill(pid, SIGKILL);
    int status = 0;
    for (int i = 0; i < timeout_ms / 100 + 1; ++i) {
        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) return (!timed_out && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
        if (w < 0 && errno != EINTR) return -1;
        ::usleep(100 * 1000);
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    return -1;
}

void sanitize_process() {
    // Bestandsbeschrijvingen boven 2 dicht (geërfd van de aanroeper), stdin/stdout/stderr zeker open, omgeving leeg.
    if (DIR* d = ::opendir("/proc/self/fd")) {
        const int dfd = ::dirfd(d);
        std::vector<int> fds;
        while (const dirent* e = ::readdir(d)) {
            const int fd = std::atoi(e->d_name);
            if (e->d_name[0] != '.' && fd > 2 && fd != dfd) fds.push_back(fd);
        }
        ::closedir(d);
        for (int fd : fds) ::close(fd);
    }
    for (int fd = 0; fd <= 2; ++fd) {
        if (::fcntl(fd, F_GETFD) < 0) {
            const int n = ::open("/dev/null", O_RDWR);
            if (n >= 0 && n != fd) { ::dup2(n, fd); ::close(n); }
        }
    }
    ::clearenv();
    ::umask(077);
}

}  // namespace ravengun::syshelper
