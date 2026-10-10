#include "mouse_service.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "output_service.hpp"
#include "ravengun/json.hpp"
#include "shared.hpp"

namespace ravengund {

using namespace ravengun;

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// De eventN-naam komt uit onze eigen opsomming; toch geen enkel pad-teken doorlaten.
bool event_name_ok(const std::string& e) {
    if (e.empty() || e.size() > 32 || e[0] == '.') return false;
    for (char c : e)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

std::string open_error(const std::string& path, int e) {
    if (e == EACCES || e == EPERM)
        return "geen toegang tot " + path + " (leesrechten nodig: de groep 'input' of een udev-regel met uaccess)";
    if (e == ENOENT || e == ENODEV || e == ENXIO) return "apparaat " + path + " bestaat niet meer (muis losgekoppeld?)";
    return "openen van " + path + " mislukt: " + std::strerror(e);
}

}  // namespace

MouseService::MouseService(OutputService& out, MouseServiceConfig cfg) : out_(out), cfg_(std::move(cfg)) {
    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (cfg_.retry_ms < 50) cfg_.retry_ms = 50;
}

MouseService::~MouseService() {
    stop();
    if (wake_fd_ >= 0) ::close(wake_fd_);
}

std::vector<sysinfo::Mouse> MouseService::mice() const {
    return cfg_.lister ? cfg_.lister() : sysinfo::list_system_mice();
}

void MouseService::wake() {
    if (wake_fd_ < 0) return;
    uint64_t one = 1;
    if (::write(wake_fd_, &one, sizeof one) < 0) { /* de poll heeft een time-out */ }
}

void MouseService::start() {
    std::lock_guard<std::mutex> g(m_);
    if (running_ || stop_) return;
    running_ = true;
    th_ = std::thread([this] { run(); });
}

void MouseService::stop() {
    std::thread t;
    {
        std::lock_guard<std::mutex> g(m_);
        if (stop_) return;
        stop_ = true;
        t = std::move(th_);
    }
    wake();
    cv_.notify_all();
    if (t.joinable()) t.join();
    // Zonder werkerthread (nooit gestart) is er niets geopend; anders heeft de thread bij het afsluiten alles vrijgegeven.
    std::lock_guard<std::mutex> g(m_);
    running_ = false;
}

unsigned MouseService::mask_locked() const {
    unsigned m = 0;
    for (int i = 0; i < kPlayers; ++i)
        if (!slots_[static_cast<size_t>(i)].mouse.empty()) m |= 1u << i;
    return m;
}

bool MouseService::assign(int player, const std::string& id, std::optional<int> counts, std::string& code, std::string& msg) {
    if (player < 1 || player > kPlayers) { code = "invalid_argument"; msg = "ongeldige speler"; return false; }
    if (counts && (*counts < output::kMouseCountsMin || *counts > output::kMouseCountsMax)) {
        code = "invalid_argument";
        msg = "'counts_per_screen' moet tussen 100 en 20000 liggen";
        return false;
    }
    if (!id.empty()) {
        // Alleen een id uit onze eigen opsomming: nooit een vrij pad openen.
        bool known = sysinfo::mouse_id_valid(id);
        if (known) {
            known = false;
            for (const auto& m : mice()) if (m.id == id) known = true;
        }
        if (!known) { code = "invalid_argument"; msg = "onbekende muis '" + id.substr(0, 64) + "' (zie 'mice' voor de beschikbare muizen)"; return false; }
    }
    std::unique_lock<std::mutex> lk(m_);
    if (stop_) { code = "unavailable"; msg = "muisservice is afgesloten"; return false; }
    if (!id.empty()) {
        for (int i = 0; i < kPlayers; ++i)
            if (i != player - 1 && slots_[static_cast<size_t>(i)].mouse == id) {
                code = "invalid_argument";
                msg = "deze muis is al aan speler " + std::to_string(i + 1) + " toegewezen";
                return false;
            }
    }
    Slot& s = slots_[static_cast<size_t>(player - 1)];
    s.mouse = id;
    if (counts) s.counts = *counts;
    s.connected = false;
    s.last_error.clear();
    ++s.gen;
    // Het masker en de gun bestaan voor de eerste openpoging; de lock van OutputService wordt hier nooit andersom genomen.
    out_.set_mouse_players(mask_locked());
    if (!id.empty()) logf("info", "muis %s -> speler %d (%d counts per schermbreedte)", id.c_str(), player, s.counts);
    else logf("info", "muis van speler %d losgekoppeld", player);
    const uint64_t before = s.attempts;
    wake();
    if (running_ && !id.empty()) cv_.wait_for(lk, std::chrono::milliseconds(400), [&] { return s.attempts > before || stop_; });
    return true;
}

std::string MouseService::assignment_locked(int player) const {
    const Slot& s = slots_[static_cast<size_t>(player - 1)];
    json::Writer w;
    w.begin_object().key("player").value(player);
    if (s.mouse.empty()) w.key("mouse").null(); else w.key("mouse").value(s.mouse);
    w.key("connected").value(s.connected);
    if (s.last_error.empty()) w.key("last_error").null(); else w.key("last_error").value(s.last_error);
    w.key("counts_per_screen").value(s.counts).end_object();
    return w.str();
}

std::string MouseService::assignment_json(int player) const {
    std::lock_guard<std::mutex> g(m_);
    return assignment_locked(player);
}

std::string MouseService::status_json() const {
    std::lock_guard<std::mutex> g(m_);
    std::string s = "[";
    for (int p = 1; p <= kPlayers; ++p) {
        if (p > 1) s += ',';
        s += assignment_locked(p);
    }
    return s + "]";
}

std::string MouseService::mice_json() const {
    json::Writer w;
    w.begin_object().key("mice").begin_array();
    for (const auto& m : mice()) {
        w.begin_object().key("id").value(m.id).key("name").value(m.name).key("absolute").value(m.absolute).key("event").value(m.event).end_object();
    }
    w.end_array().end_object();
    std::string s = w.str();
    s.pop_back();
    s += ",\"assignments\":" + status_json() + "}";
    return s;
}

void MouseService::close_and_release(size_t i) {
    Slot& s = slots_[i];
    const bool was_open = s.fd >= 0;
    if (was_open) { ::close(s.fd); s.fd = -1; }
    s.carry.clear();
    const bool pressed = s.gun.button(output::GunButton::Trigger) || s.gun.button(output::GunButton::Reload) ||
                         s.gun.button(output::GunButton::Grenade) || s.gun.button(output::GunButton::Aux1) ||
                         s.gun.button(output::GunButton::Aux2);
    if (was_open || pressed) out_.feed_live(static_cast<int>(i) + 1, s.gun.release());
}

void MouseService::lose(size_t i, const std::string& why) {
    close_and_release(i);
    {
        std::lock_guard<std::mutex> g(m_);
        Slot& s = slots_[i];
        s.connected = false;
        s.last_error = why;
        s.next_retry_ms = now_ms() + cfg_.retry_ms;
    }
    logf("waarschuwing", "muis van speler %zu: %s", i + 1, why.c_str());
}

void MouseService::try_open(size_t i) {
    Slot& s = slots_[i];
    std::string id;
    int counts;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> g(m_);
        id = s.mouse;
        counts = s.counts;
        gen = s.gen;
    }
    std::string err;
    int fd = -1;
    output::MouseGunConfig gc = s.gun.config();
    gc.counts_per_screen = counts;
    const sysinfo::Mouse* found = nullptr;
    const auto list = mice();
    for (const auto& m : list) if (m.id == id) found = &m;
    if (!found) {
        err = "muis '" + id + "' is niet aangesloten";
    } else if (!event_name_ok(found->event)) {
        err = "ongeldig apparaat voor muis '" + id + "'";
    } else {
        const std::string path = cfg_.dev_dir + "/" + found->event;
        // Geen EVIOCGRAB: de desktopcursor blijft werken.
        fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            err = open_error(path, errno);
        } else if (found->absolute) {
            input_absinfo ax{}, ay{};
            if (::ioctl(fd, EVIOCGABS(ABS_X), &ax) < 0 || ::ioctl(fd, EVIOCGABS(ABS_Y), &ay) < 0) {
                err = "bereik van de assen van " + path + " niet te lezen: " + std::strerror(errno);
            } else {
                gc.abs_x = {ax.minimum, ax.maximum};
                gc.abs_y = {ay.minimum, ay.maximum};
            }
        } else {
            gc.abs_x = gc.abs_y = {};
        }
        if (!err.empty() && fd >= 0) { ::close(fd); fd = -1; }
    }
    bool ok = false;
    {
        std::lock_guard<std::mutex> g(m_);
        if (s.gen != gen) {
            // Intussen opnieuw toegewezen: dit resultaat is verouderd.
            if (fd >= 0) ::close(fd);
            return;
        }
        ++s.attempts;
        if (!err.empty()) {
            if (s.last_error != err) logf("waarschuwing", "muis van speler %zu: %s", i + 1, err.c_str());
            s.connected = false;
            s.last_error = err;
            s.next_retry_ms = now_ms() + cfg_.retry_ms;
        } else {
            s.fd = fd;
            s.connected = true;
            s.last_error.clear();
            s.gun.configure(gc);
            ok = true;
        }
    }
    cv_.notify_all();
    if (ok) {
        logf("info", "muis van speler %zu verbonden (%s)", i + 1, id.c_str());
        out_.feed_live(static_cast<int>(i) + 1, s.gun.snapshot());
    }
}

void MouseService::drain(size_t i) {
    Slot& s = slots_[i];
    constexpr size_t kEv = sizeof(input_event);
    char buf[kEv * 64];
    for (int round = 0; round < 8; ++round) {
        const ssize_t n = ::read(s.fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            lose(i, errno == ENODEV ? "muis losgekoppeld" : std::string("lezen mislukt: ") + std::strerror(errno));
            return;
        }
        if (n == 0) { lose(i, "apparaat gesloten"); return; }
        s.carry.append(buf, static_cast<size_t>(n));
        size_t off = 0;
        while (s.carry.size() - off >= kEv) {
            input_event ev;
            std::memcpy(&ev, s.carry.data() + off, kEv);
            off += kEv;
            if (auto a = s.gun.event(ev.type, ev.code, ev.value)) out_.feed_live(static_cast<int>(i) + 1, *a);
        }
        s.carry.erase(0, off);
        if (static_cast<size_t>(n) < sizeof buf) return;
    }
}

void MouseService::run() {
    for (;;) {
        // 1. Nieuwe toewijzingen overnemen: oude muis sluiten en alles vrijgeven.
        std::vector<size_t> changed;
        bool want[kPlayers] = {};
        {
            std::lock_guard<std::mutex> g(m_);
            if (stop_) break;
            for (size_t i = 0; i < slots_.size(); ++i) {
                Slot& s = slots_[i];
                if (s.gen != s.applied_gen) { s.applied_gen = s.gen; s.next_retry_ms = 0; changed.push_back(i); }
            }
        }
        for (size_t i : changed) close_and_release(i);
        {
            std::lock_guard<std::mutex> g(m_);
            for (size_t i = 0; i < slots_.size(); ++i) want[i] = !slots_[i].mouse.empty() && slots_[i].fd < 0;
        }
        // 2. Openpogingen die aan de beurt zijn.
        int64_t timeout = 1000;
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!want[i]) continue;
            int64_t due;
            { std::lock_guard<std::mutex> g(m_); due = slots_[i].next_retry_ms; }
            if (due <= now_ms()) {
                try_open(i);
                std::lock_guard<std::mutex> g(m_);
                if (slots_[i].fd < 0) timeout = std::min<int64_t>(timeout, std::max<int64_t>(0, slots_[i].next_retry_ms - now_ms()));
            } else {
                timeout = std::min<int64_t>(timeout, due - now_ms());
            }
        }
        // 3. Wachten op events, een wekking of de volgende poging.
        pollfd pfd[kPlayers + 1];
        size_t owner[kPlayers + 1];
        int n = 0;
        pfd[n] = {wake_fd_, POLLIN, 0};
        owner[n++] = 0;
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].fd < 0) continue;
            pfd[n] = {slots_[i].fd, POLLIN, 0};
            owner[n++] = i;
        }
        const int pr = ::poll(pfd, static_cast<nfds_t>(n), static_cast<int>(timeout));
        if (pr < 0 && errno != EINTR) {
            logf("fout", "muisservice: poll mislukt: %s", std::strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (pr <= 0) continue;
        if (pfd[0].revents & POLLIN) {
            uint64_t v;
            if (::read(wake_fd_, &v, sizeof v) < 0) { /* leeg */ }
        }
        for (int k = 1; k < n; ++k) {
            const size_t i = owner[k];
            if (slots_[i].fd < 0) continue;
            if (pfd[k].revents & POLLIN) drain(i);   // eerst leegmaken: een HUP kan na de laatste events komen
            if (slots_[i].fd >= 0 && (pfd[k].revents & (POLLHUP | POLLERR | POLLNVAL))) lose(i, "muis losgekoppeld");
        }
    }
    // Afsluiten: niets mag blijven hangen in de virtuele gun.
    for (size_t i = 0; i < slots_.size(); ++i) close_and_release(i);
}

}  // namespace ravengund
