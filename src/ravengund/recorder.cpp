#include "recorder.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/json.hpp"

namespace ravengund {

using namespace ravengun;

namespace {
bool mkdirs(const std::string& dir, std::string& err) {
    std::string cur;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if ((i == dir.size() || dir[i] == '/') && !cur.empty()) {
            if (::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
                err = "kan " + cur + " niet maken: " + std::strerror(errno);
                return false;
            }
        }
        if (i < dir.size()) cur += dir[i];
    }
    ::chmod(dir.c_str(), 0700);
    return true;
}
}  // namespace

std::string Recorder::start(rec::Header h, uint64_t max_bytes, double max_duration_s, int64_t now_ns) {
    if (active()) return "er loopt al een opname (" + file_ + ")";
    if (writer_.joinable()) writer_.join();
    if (dir_.empty()) return "geen opnamemap (HOME niet gezet)";
    if (!rec::valid_tag(h.tag)) return "ongeldige tag (alleen a-z, 0-9, _ en -, max. 40 tekens)";
    std::string err;
    if (!mkdirs(dir_, err)) return err;

    char ts[32];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    ::localtime_r(&now, &tm);
    std::strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", &tm);
    std::string base = std::string(ts) + "-" + h.tag;
    int fd = -1;
    std::string name;
    for (int k = 1; k < 100 && fd < 0; ++k) {
        name = base + (k > 1 ? "-" + std::to_string(k) : "") + ".rgrec.jsonl";
        fd = ::open((dir_ + "/" + name).c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0 && errno != EEXIST) return "kan opnamebestand niet maken: " + std::string(std::strerror(errno));
    }
    if (fd < 0) return "kan geen vrije bestandsnaam vinden";
    ::fchmod(fd, 0600);

    h.t0_ns = now_ns;
    h.unix_s = static_cast<int64_t>(now);
    std::string header = rec::format_header(h) + "\n";
    {
        std::lock_guard<std::mutex> g(m_);
        fd_ = fd;
        file_ = name;
        tag_ = h.tag;
        limit_hit_.clear();
        last_reason_.clear();
        max_bytes_ = max_bytes;
        max_duration_s_ = max_duration_s;
        start_ns_ = now_ns;
        stop_ns_ = 0;
        bytes_ = header.size();
        written_ = 0;
        write_errors_ = 0;
        q_.clear();
        q_.push_back(std::move(header));
        q_bytes_ = bytes_;
        writer_stop_ = false;
        reports_ = optical_ = leds_ = marks_ = dropped_ = 0;
        t0_.store(now_ns);
    }
    writer_ = std::thread(&Recorder::writer_main, this);
    active_.store(true, std::memory_order_release);
    return {};
}

void Recorder::push_line(std::string line, std::atomic<uint64_t>& counter) {
    line += '\n';
    std::lock_guard<std::mutex> g(m_);
    if (!active_.load(std::memory_order_acquire)) return;
    if (!limit_hit_.empty()) return;
    if (bytes_ + line.size() > max_bytes_) { limit_hit_ = "size_limit"; return; }
    if (q_bytes_ + line.size() > kMaxQueueBytes) { ++dropped_; return; }
    bytes_ += line.size();
    q_bytes_ += line.size();
    q_.push_back(std::move(line));
    ++counter;
    cv_.notify_one();
}

void Recorder::push_report(const core::RawReport& r) {
    if (!active()) return;
    push_line(rec::format_report(r, t0_.load()), reports_);
}

void Recorder::push_optical(const core::OpticalSample& o) {
    if (!active()) return;
    push_line(rec::format_optical(o, t0_.load()), optical_);
}

void Recorder::push_led(int64_t t, int r, int g, int b) {
    if (!active()) return;
    rec::LedEvent e;
    e.t_ns = t;
    e.rgb[0] = r; e.rgb[1] = g; e.rgb[2] = b;
    push_line(rec::format_led(e, t0_.load()), leds_);
}

void Recorder::push_mark(int64_t t, const std::string& label) {
    if (!active()) return;
    push_line(rec::format_mark({t, label}, t0_.load()), marks_);
}

std::string Recorder::check_limits(int64_t now) {
    if (!active()) return {};
    std::string reason;
    {
        std::lock_guard<std::mutex> g(m_);
        if (!limit_hit_.empty()) reason = limit_hit_;
        else if (max_duration_s_ > 0 && static_cast<double>(now - start_ns_) / 1e9 >= max_duration_s_) reason = "duration_limit";
    }
    if (!reason.empty()) stop(reason, now);
    return reason;
}

void Recorder::writer_main() {
    std::unique_lock<std::mutex> lk(m_);
    while (true) {
        cv_.wait(lk, [&] { return writer_stop_ || !q_.empty(); });
        std::deque<std::string> batch;
        batch.swap(q_);
        q_bytes_ = 0;
        bool done = writer_stop_;
        int fd = fd_;
        lk.unlock();
        std::string buf;
        for (auto& l : batch) buf += l;
        size_t off = 0;
        uint64_t errs = 0;
        while (off < buf.size()) {
            ssize_t w = ::write(fd, buf.data() + off, buf.size() - off);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { ++errs; break; }
            off += static_cast<size_t>(w);
        }
        lk.lock();
        written_ += off;
        write_errors_ += errs;
        if (done && q_.empty()) break;
    }
}

std::string Recorder::stop(const std::string& reason, int64_t now) {
    if (!active_.exchange(false)) {
        if (writer_.joinable()) writer_.join();
        return {};
    }
    if (now == 0) {
        timespec t{};
        clock_gettime(CLOCK_MONOTONIC, &t);
        now = static_cast<int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
    }
    {
        std::lock_guard<std::mutex> g(m_);
        rec::End e;
        e.t_ns = now;
        e.reason = reason;
        e.reports = reports_;
        e.optical = optical_;
        e.dropped = dropped_;
        std::string line = rec::format_end(e, t0_.load()) + "\n";
        bytes_ += line.size();  // de eindregel mag de grens overschrijden (begrensd, < 200 bytes)
        q_.push_back(std::move(line));
        writer_stop_ = true;
        last_reason_ = reason;
        stop_ns_ = now;
        cv_.notify_one();
    }
    if (writer_.joinable()) writer_.join();
    std::lock_guard<std::mutex> g(m_);
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
    return summary_json_locked();
}

std::string Recorder::summary_json_locked() const {
    json::Writer w;
    w.begin_object();
    w.key("file").value(file_);
    w.key("dir").value(dir_);
    w.key("tag").value(tag_);
    w.key("reason").value(last_reason_);
    w.key("bytes").value(static_cast<unsigned long long>(bytes_));
    w.key("written").value(static_cast<unsigned long long>(written_));
    w.key("write_errors").value(static_cast<unsigned long long>(write_errors_));
    w.key("duration_s").value(static_cast<double>((stop_ns_ ? stop_ns_ : start_ns_) - start_ns_) / 1e9);
    w.key("reports").value(static_cast<unsigned long long>(reports_.load()));
    w.key("optical").value(static_cast<unsigned long long>(optical_.load()));
    w.key("led_events").value(static_cast<unsigned long long>(leds_.load()));
    w.key("marks").value(static_cast<unsigned long long>(marks_.load()));
    w.key("dropped").value(static_cast<unsigned long long>(dropped_.load()));
    w.end_object();
    return w.str();
}

std::string Recorder::status_json(int64_t now) const {
    std::lock_guard<std::mutex> g(m_);
    json::Writer w;
    w.begin_object();
    bool act = active();
    w.key("active").value(act);
    w.key("dir").value(dir_);
    if (act) {
        w.key("file").value(file_);
        w.key("tag").value(tag_);
        w.key("elapsed_s").value(static_cast<double>(now - start_ns_) / 1e9);
        w.key("max_duration_s").value(max_duration_s_);
        w.key("bytes").value(static_cast<unsigned long long>(bytes_));
        w.key("max_bytes").value(static_cast<unsigned long long>(max_bytes_));
        w.key("reports").value(static_cast<unsigned long long>(reports_.load()));
        w.key("optical").value(static_cast<unsigned long long>(optical_.load()));
        w.key("led_events").value(static_cast<unsigned long long>(leds_.load()));
        w.key("marks").value(static_cast<unsigned long long>(marks_.load()));
        w.key("dropped").value(static_cast<unsigned long long>(dropped_.load()));
    } else if (!file_.empty()) {
        w.key("last").raw(summary_json_locked());
    }
    w.end_object();
    return w.str();
}

}  // namespace ravengund
