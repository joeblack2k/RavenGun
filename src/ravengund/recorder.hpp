#pragma once
// Recorder (fase 0): schrijft HID-rapporten, optische metingen, LED-wissels en labels naar een
// opnamebestand (formaat: ravengun/rec/recording.hpp). Alleen getallen, nooit beelden.
//
// - Bestanden in ~/.local/state/ravengun/recordings/ (map 0700, bestand 0600, O_EXCL|O_NOFOLLOW);
//   de naam komt uit tijdstip en tag, nooit uit een pad van een client.
// - Het controller-ID staat alleen geredigeerd in de header.
// - Begrensd: max_bytes (strikt: een regel die de grens zou overschrijden wordt niet meer
//   geschreven; de opname stopt dan met reden "size_limit") en max_duration_s.
// - Niet-blokkerend voor de aanroepers: push_*() formatteert en zet de regel in een begrensde
//   wachtrij; een eigen schrijfthread schrijft weg. Is de wachtrij vol (> 8 MiB), dan wordt de
//   regel geteld als "dropped".
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "ravengun/rec/recording.hpp"

namespace ravengund {

class Recorder {
public:
    explicit Recorder(std::string dir) : dir_(std::move(dir)) {}
    ~Recorder() { stop("shutdown"); }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Leeg resultaat = gelukt; anders een Nederlandse foutmelding.
    std::string start(ravengun::rec::Header h, uint64_t max_bytes, double max_duration_s, int64_t now_ns);
    bool active() const { return active_.load(std::memory_order_acquire); }
    void push_report(const ravengun::core::RawReport& r);
    void push_optical(const ravengun::core::OpticalSample& o);
    void push_led(int64_t t_ns, int r, int g, int b);
    void push_mark(int64_t t_ns, const std::string& label);
    // Door de HID-thread periodiek aangeroepen: stopt bij een bereikte grens. Geeft de reden of "".
    std::string check_limits(int64_t now_ns);
    // Stopt (idempotent). Geeft een JSON-samenvatting van de gestopte opname of "" als er niets liep.
    std::string stop(const std::string& reason, int64_t now_ns = 0);
    std::string status_json(int64_t now_ns) const;
    const std::string& dir() const { return dir_; }

    static constexpr uint64_t kMaxQueueBytes = 8u << 20;

private:
    void push_line(std::string line, std::atomic<uint64_t>& counter);
    void writer_main();
    std::string summary_json_locked() const;

    std::string dir_;
    std::atomic<bool> active_{false};
    std::atomic<int64_t> t0_{0};
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    uint64_t q_bytes_ = 0;
    bool writer_stop_ = false;
    std::thread writer_;
    int fd_ = -1;
    // Toestand van de lopende (of laatste) opname, onder m_.
    std::string file_, tag_, limit_hit_, last_reason_;
    uint64_t max_bytes_ = 0, bytes_ = 0, written_ = 0, write_errors_ = 0;
    double max_duration_s_ = 0;
    int64_t start_ns_ = 0, stop_ns_ = 0;
    std::atomic<uint64_t> reports_{0}, optical_{0}, leds_{0}, marks_{0}, dropped_{0};
};

}  // namespace ravengund
