#pragma once
// Gedeelde toestand tussen de HID-thread (eigenaar van hardware en pijplijn) en de IPC-thread.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <time.h>

#include "ravengun/core/input_model.hpp"

namespace ravengund {

class HidContext;  // gedefinieerd in hid_thread.hpp

inline int64_t mono_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

// Log naar stderr met monotone tijd; Bluetooth-adressen horen al geredigeerd te zijn.
template <typename... A>
void logf(const char* level, const char* fmt, A... a) {
    double t = static_cast<double>(mono_ns()) / 1e9;
    std::fprintf(stderr, "[%.3f] %s: ", t, level);
    if constexpr (sizeof...(A) == 0) std::fputs(fmt, stderr);
    else std::fprintf(stderr, fmt, a...);
    std::fputc('\n', stderr);
}

// Laatste sample plus cumulatieve flanktellers per knopbit. Iedere afnemer onthoudt zelf de
// tellerstand van zijn vorige lezing, zodat geen enkele flank verloren gaat, ook niet bij
// meerdere of trage afnemers.
struct Snapshot {
    ravengun::core::InputSample sample;
    std::array<uint64_t, 32> press_total{};
    std::array<uint64_t, 32> release_total{};
};

class SharedState {
public:
    void publish(const ravengun::core::InputSample& s) {
        std::lock_guard<std::mutex> g(m_);
        snap_.sample = s;
        for (int b = 0; b < 32; ++b) {
            if (s.pressed & (1u << b)) ++snap_.press_total[b];
            if (s.released & (1u << b)) ++snap_.release_total[b];
        }
    }
    Snapshot get() const {
        std::lock_guard<std::mutex> g(m_);
        return snap_;
    }

    // Opdrachten die in de HID-thread moeten draaien (pijplijn heeft één eigenaar).
    using Job = std::function<std::string(HidContext&)>;
    std::future<std::string> post(Job j) {
        std::packaged_task<std::string(HidContext&)> t(std::move(j));
        auto f = t.get_future();
        std::lock_guard<std::mutex> g(jm_);
        jobs_.push_back(std::move(t));
        return f;
    }
    void run_jobs(HidContext& ctx) {
        std::deque<std::packaged_task<std::string(HidContext&)>> js;
        {
            std::lock_guard<std::mutex> g(jm_);
            js.swap(jobs_);
        }
        for (auto& j : js) j(ctx);
    }

    std::atomic<bool> running{true};

private:
    mutable std::mutex m_;
    Snapshot snap_;
    std::mutex jm_;
    std::deque<std::packaged_task<std::string(HidContext&)>> jobs_;
};

}  // namespace ravengund
