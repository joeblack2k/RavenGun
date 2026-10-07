#pragma once
// Begrensde wachtrij die altijd het nieuwste element bewaart.
//
// Bedoeld tussen de capture-thread (producent) en de tracker-thread (consument). Is de wachtrij
// vol, dan wordt het OUDSTE element weggegooid (en geteld), zodat de consument nooit een groeiende
// achterstand opbouwt. pop_latest() levert het nieuwste element en gooit eventuele oudere ook weg:
// de consument verwerkt dus altijd het recentste frame. Weggegooide elementen gaan via
// recycle() terug naar een vrije lijst, zodat framebuffers niet steeds opnieuw gealloceerd worden.
//
// Alleen voor cameraframes. IMU-rapporten gaan hier NIET doorheen: die worden allemaal verwerkt.
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace ravengun::vision {

template <typename T>
class LatestQueue {
public:
    explicit LatestQueue(size_t capacity) : cap_(capacity < 1 ? 1 : capacity) {}

    size_t capacity() const { return cap_; }

    // Een element uit de vrije lijst (of een nieuw, standaard-geconstrueerd element).
    T acquire() {
        std::lock_guard<std::mutex> g(m_);
        if (free_.empty()) return T{};
        T t = std::move(free_.back());
        free_.pop_back();
        return t;
    }

    // Voegt toe; is de wachtrij vol, dan valt het oudste element weg (telt als dropped).
    void push(T v) {
        {
            std::lock_guard<std::mutex> g(m_);
            if (closed_) { recycle_locked(std::move(v)); return; }
            while (q_.size() >= cap_) {
                recycle_locked(std::move(q_.front()));
                q_.pop_front();
                ++dropped_full_;
            }
            q_.push_back(std::move(v));
            ++pushed_;
        }
        cv_.notify_one();
    }

    // Wacht maximaal `timeout` op een element. Geeft het nieuwste; oudere elementen worden
    // weggegooid (geteld als dropped_stale). nullopt bij time-out of na close().
    template <typename Rep, typename Period>
    std::optional<T> pop_latest(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> l(m_);
        if (!cv_.wait_for(l, timeout, [&] { return !q_.empty() || closed_; })) return std::nullopt;
        if (q_.empty()) return std::nullopt;
        while (q_.size() > 1) {
            recycle_locked(std::move(q_.front()));
            q_.pop_front();
            ++dropped_stale_;
        }
        T v = std::move(q_.front());
        q_.pop_front();
        ++popped_;
        return v;
    }

    std::optional<T> try_pop_latest() { return pop_latest(std::chrono::milliseconds(0)); }

    // Geeft een verwerkt element terug voor hergebruik.
    void recycle(T v) {
        std::lock_guard<std::mutex> g(m_);
        recycle_locked(std::move(v));
    }

    void close() {
        {
            std::lock_guard<std::mutex> g(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    struct Counters {
        uint64_t pushed = 0, popped = 0;
        uint64_t dropped_full = 0;   // weggegooid bij push omdat de wachtrij vol was
        uint64_t dropped_stale = 0;  // weggegooid bij pop omdat er een nieuwer element was
        size_t depth = 0;
    };
    Counters counters() const {
        std::lock_guard<std::mutex> g(m_);
        return {pushed_, popped_, dropped_full_, dropped_stale_, q_.size()};
    }

private:
    void recycle_locked(T v) {
        if (free_.size() < cap_ + 2) free_.push_back(std::move(v));
    }

    const size_t cap_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
    std::vector<T> free_;
    bool closed_ = false;
    uint64_t pushed_ = 0, popped_ = 0, dropped_full_ = 0, dropped_stale_ = 0;
};

}  // namespace ravengun::vision
