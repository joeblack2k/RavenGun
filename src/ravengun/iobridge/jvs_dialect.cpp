#include "ravengun/iobridge/jvs_dialect.hpp"

#include <poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>

#include "ravengun/json.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

namespace {

int64_t mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string hex(const std::vector<uint8_t>& v, size_t max = 40) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < v.size() && i < max; ++i) {
        std::snprintf(b, sizeof b, "%02X", v[i]);
        if (i) s += ' ';
        s += b;
    }
    if (v.size() > max) s += " ...";
    return s;
}

// ---- pty

class PtyTransport final : public JvsTransport {
public:
    explicit PtyTransport(std::string link) : link_(std::move(link)) {}
    ~PtyTransport() override { close(); }

    bool open(std::string& err) override {
        if (master_ >= 0) return true;
        int m = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (m < 0) { err = std::string("pty: posix_openpt mislukt: ") + std::strerror(errno); return false; }
        if (::grantpt(m) < 0 || ::unlockpt(m) < 0) {
            err = std::string("pty: grantpt/unlockpt mislukt: ") + std::strerror(errno);
            ::close(m);
            return false;
        }
        char name[128];
        if (::ptsname_r(m, name, sizeof name) != 0) {
            err = std::string("pty: ptsname mislukt: ") + std::strerror(errno);
            ::close(m);
            return false;
        }
        // De slave vasthouden: zo blijft de lijninstelling (raw) staan als de game hem opent en sluit.
        int s = ::open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (s < 0) {
            err = std::string("pty: slave openen mislukt: ") + std::strerror(errno);
            ::close(m);
            return false;
        }
        termios tio{};
        if (::tcgetattr(s, &tio) == 0) {
            ::cfmakeraw(&tio);
            ::cfsetispeed(&tio, B115200);
            ::cfsetospeed(&tio, B115200);
            tio.c_cflag |= CLOCAL | CREAD;
            tio.c_cc[VMIN] = 0;
            tio.c_cc[VTIME] = 0;
            ::tcsetattr(s, TCSANOW, &tio);
        }
        master_ = m;
        slave_ = s;
        path_ = name;
        if (!link_.empty()) {
            ::unlink(link_.c_str());
            if (::symlink(path_.c_str(), link_.c_str()) != 0) {
                err = "pty: symlink " + link_ + " mislukt: " + std::strerror(errno);
                close();
                return false;
            }
        }
        return true;
    }
    int fd() const override { return master_; }
    long read(uint8_t* p, size_t n) override {
        if (master_ < 0) return -1;
        ssize_t r;
        do { r = ::read(master_, p, n); } while (r < 0 && errno == EINTR);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        // EIO komt voor zodra alle slaves dicht zijn; wij houden er zelf een open, dus dat gebeurt niet.
        return r;
    }
    bool write(const uint8_t* p, size_t n) override {
        if (master_ < 0) return false;
        size_t done = 0;
        int spins = 0;
        while (done < n) {
            ssize_t w = ::write(master_, p + done, n - done);
            if (w > 0) { done += static_cast<size_t>(w); continue; }
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && spins++ < 50) { ::usleep(1000); continue; }
            return false;
        }
        return true;
    }
    void close() override {
        if (!link_.empty() && !path_.empty()) {
            char buf[256];
            ssize_t n = ::readlink(link_.c_str(), buf, sizeof buf - 1);
            if (n > 0 && std::string(buf, static_cast<size_t>(n)) == path_) ::unlink(link_.c_str());
        }
        if (slave_ >= 0) { ::close(slave_); slave_ = -1; }
        if (master_ >= 0) { ::close(master_); master_ = -1; }
        path_.clear();
    }
    bool is_open() const override { return master_ >= 0; }
    std::string path() const override { return path_; }

private:
    std::string link_, path_;
    int master_ = -1, slave_ = -1;
};

}  // namespace

std::unique_ptr<JvsTransport> make_pty_transport(const std::string& link) { return std::make_unique<PtyTransport>(link); }

// ---------------------------------------------------------------- dialect

JvsDialect::JvsDialect(std::vector<JvsBoard> boards, std::string game, std::unique_ptr<JvsTransport> t, std::string modem_file)
    : game_(std::move(game)), t_(std::move(t)), modem_file_(std::move(modem_file)) {
    for (auto& b : boards) bus_.add_node(std::move(b));
}

bool JvsDialect::open(std::string& err) {
    if (opened_) return true;
    if (!t_) { err = "geen transport voor de JVS-lijn"; return false; }
    if (bus_.node_count() == 0) { err = "geen JVS-borden"; return false; }
    if (!t_->open(err)) return false;
    opened_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        bus_.set_input(IoState{});
        publish_modem(bus_.modem_status());
    }
    quit_ = false;
    th_ = std::thread(&JvsDialect::loop, this);
    return true;
}

// De modemstatus voor de LD_PRELOAD-hulp in het bestand zetten (alleen bij verandering). Met m_ vast aangeroepen.
void JvsDialect::publish_modem(uint8_t v) {
    if (modem_file_.empty() || v == modem_published_) return;
    modem_published_ = v;
    const std::string tmp = modem_file_ + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    const ssize_t w = ::write(fd, &v, 1);
    ::close(fd);
    if (w == 1) ::rename(tmp.c_str(), modem_file_.c_str());
}

void JvsDialect::write(const IoState& s) {
    std::lock_guard<std::mutex> lk(m_);
    bus_.set_input(s);
}

void JvsDialect::neutral() {
    std::lock_guard<std::mutex> lk(m_);
    bus_.set_input(IoState{});
}

void JvsDialect::close() {
    quit_ = true;
    if (th_.joinable()) th_.join();
    if (t_) t_->close();
    if (opened_ && !modem_file_.empty()) ::unlink(modem_file_.c_str());
    opened_ = false;
}

void JvsDialect::loop() {
    while (!quit_) pump(50);
}

// Leest wat er is (wacht maximaal timeout_ms) en antwoordt. Geeft het aantal gelezen bytes.
int JvsDialect::pump(int timeout_ms) {
    if (!t_ || !t_->is_open()) { ::usleep(static_cast<useconds_t>(timeout_ms) * 1000); return 0; }
    pollfd p{t_->fd(), POLLIN, 0};
    const int r = ::poll(&p, 1, timeout_ms);
    if (r <= 0) return 0;
    uint8_t buf[512];
    int total = 0;
    for (;;) {
        const long n = t_->read(buf, sizeof buf);
        if (n <= 0) {
            if (n < 0) {
                std::lock_guard<std::mutex> lk(m_);
                st_.last_error = std::string("lezen van de JVS-lijn mislukt: ") + std::strerror(errno);
                ::usleep(20000);   // geen razende lus bij een blijvende fout
            }
            break;
        }
        total += static_cast<int>(n);
        handle_bytes(buf, static_cast<size_t>(n));
        if (n < static_cast<long>(sizeof buf)) break;
    }
    return total;
}

void JvsDialect::handle_bytes(const uint8_t* p, size_t n) {
    std::vector<JvsFrame> frames;
    std::vector<std::pair<JvsFrame, std::vector<uint8_t>>> work;
    {
        std::lock_guard<std::mutex> lk(m_);
        st_.rx_bytes += n;
        parser_.feed(p, n, frames);
        const int64_t now = mono_ms();
        for (auto& f : frames) {
            ++st_.frames;
            if (!st_.first_frame_ms) st_.first_frame_ms = now;
            st_.last_frame_ms = now;
            // Tellers per soort verzoek (alleen het eerste commando van het packet telt voor de pollteller).
            for (size_t i = 0; i < f.data.size(); ++i) {
                if (f.data[i] == kCmdSwitches) ++st_.switch_polls;
                else if (f.data[i] == kCmdAnalog) ++st_.analog_polls;
                else if (f.data[i] == kCmdIdentify) ++st_.identifies;
            }
            std::vector<uint8_t> wire = bus_.process(f);
            publish_modem(bus_.modem_status());
            std::string line = std::to_string(now) + " <- node " + hex({f.node}) + " [" + hex(f.data, 256) + "]" + (f.checksum_ok ? "" : " SOMFOUT");
            line += wire.empty() ? "  (geen antwoord)" : "  -> [" + hex(wire, 256) + "]";
            recent_.push_back(line);
            while (recent_.size() > 16) recent_.pop_front();
            // Elk nieuw soort packet (zelfde bytes) één keer in het logboek, de eerste 100.
            if (trace_ && seen_.size() < 100) {
                const std::string key = hex({f.node}) + ":" + hex(f.data, 64);
                if (seen_.insert(key).second) trace_(line);
            }
            work.emplace_back(std::move(f), std::move(wire));
        }
    }
    for (auto& w : work) {
        if (w.second.empty()) continue;
        const bool ok = t_->write(w.second.data(), w.second.size());
        std::lock_guard<std::mutex> lk(m_);
        if (ok) { ++st_.writes; st_.tx_bytes += w.second.size(); }
        else { ++st_.write_errors; st_.last_error = "schrijven naar de JVS-lijn mislukt"; }
    }
}

JvsStats JvsDialect::stats() const {
    std::lock_guard<std::mutex> lk(m_);
    return st_;
}

uint8_t JvsDialect::modem_status() const {
    std::lock_guard<std::mutex> lk(m_);
    return bus_.modem_status();
}

std::string JvsDialect::status_json() const {
    std::lock_guard<std::mutex> lk(m_);
    json::Writer w;
    w.begin_object();
    w.key("transport").value("pty");
    w.key("path").value(t_ ? t_->path() : std::string());
    w.key("modem_status").value(static_cast<int>(bus_.modem_status()));
    w.key("addressed").value(bus_.all_addressed());
    w.key("rx_bytes").value(st_.rx_bytes);
    w.key("tx_bytes").value(st_.tx_bytes);
    w.key("frames").value(st_.frames);
    w.key("bad_sum").value(bus_.stats().bad_sum);
    w.key("resets").value(bus_.stats().resets);
    w.key("identifies").value(st_.identifies);
    w.key("switch_polls").value(st_.switch_polls);
    w.key("analog_polls").value(st_.analog_polls);
    w.key("resyncs").value(parser_.resyncs());
    w.key("garbage_bytes").value(parser_.garbage());
    w.key("write_errors").value(st_.write_errors);
    const int64_t now = mono_ms();
    w.key("since_last_frame_ms").value(st_.last_frame_ms ? now - st_.last_frame_ms : static_cast<int64_t>(-1));
    if (st_.first_frame_ms && st_.last_frame_ms > st_.first_frame_ms)
        w.key("polls_per_s").value(static_cast<double>(st_.switch_polls + st_.analog_polls) * 1000.0 / static_cast<double>(st_.last_frame_ms - st_.first_frame_ms));
    if (!st_.last_error.empty()) w.key("error").value(st_.last_error);
    w.key("nodes").begin_array();
    for (size_t i = 0; i < bus_.node_count(); ++i) {
        const JvsNode& n = bus_.node(i);
        w.begin_object();
        w.key("board").value(n.board().name);
        w.key("address").value(static_cast<int>(n.address()));
        w.key("requests").value(n.stats().requests);
        w.key("coins").value(static_cast<int>(n.coin_count(0)));
        w.key("commands").begin_object();
        for (const auto& kv : n.stats().commands) {
            char k[8];
            std::snprintf(k, sizeof k, "0x%02X", kv.first);
            w.key(k).value(kv.second);
        }
        w.end_object();
        // De waarden die de game nu zou lezen.
        const JvsBoard& b = n.board();
        const std::vector<uint8_t> sw = jvs_switch_report(b, n.input(), b.players, jvs_switch_bytes(b));
        w.key("switches").value(hex(sw));
        w.key("analog").begin_array();
        for (const auto& c : b.analog) w.value(static_cast<int>(jvs_analog_value(c, n.input())));
        w.end_array();
        if (!n.stats().main_id.empty()) w.key("main_id").value(n.stats().main_id);
        if (!n.stats().gp_out.empty()) w.key("gp_out").value(hex(n.stats().gp_out));
        w.end_object();
    }
    w.end_array();
    w.key("recent").begin_array();
    for (const auto& r : recent_) w.value(r);
    w.end_array();
    w.end_object();
    return w.str();
}

}  // namespace ravengun::iobridge
