#include "ravengun/vision/frame_timing.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::vision {

void Ring::add(double v) {
    if (v_.size() < n_) v_.push_back(v);
    else { v_[pos_] = v; pos_ = (pos_ + 1) % n_; }
}

double Ring::percentile(double p) const {
    if (v_.empty()) return 0;
    std::vector<double> c = v_;
    std::sort(c.begin(), c.end());
    p = std::clamp(p, 0.0, 1.0);
    double idx = p * static_cast<double>(c.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx)), hi = static_cast<size_t>(std::ceil(idx));
    return c[lo] + (c[hi] - c[lo]) * (idx - static_cast<double>(lo));
}

double Ring::max() const { return v_.empty() ? 0 : *std::max_element(v_.begin(), v_.end()); }

double Ring::mean() const {
    if (v_.empty()) return 0;
    double s = 0;
    for (double x : v_) s += x;
    return s / static_cast<double>(v_.size());
}

void FrameTiming::on_frame(int64_t t_frame_ns, uint32_t sequence, int64_t t_dequeue_ns) {
    if (frames > 0) {
        if (sequence > last_seq) {
            uint32_t gap = sequence - last_seq - 1;
            if (gap) { missed += gap; ++gap_events; }
        } else {
            ++sequence_reset;
        }
        if (t_frame_ns <= last_t_ns) {
            ++non_monotonic;
        } else {
            double ms = static_cast<double>(t_frame_ns - last_t_ns) / 1e6;
            iv_.add(ms);
            for (size_t i = 0; i < kBucketUpperMs.size(); ++i)
                if (ms < kBucketUpperMs[i]) { ++buckets[i]; break; }
        }
    } else {
        first_t_ns = t_frame_ns;
    }
    ++frames;
    last_t_ns = t_frame_ns;
    last_seq = sequence;
    dq_.add(static_cast<double>(t_dequeue_ns - t_frame_ns) / 1e6);
    if (times_.size() < 2048) times_.push_back(t_frame_ns);
    else { times_[times_pos_] = t_frame_ns; times_pos_ = (times_pos_ + 1) % times_.size(); }
}

void FrameTiming::on_processed(int64_t age_ns, double processing_us) {
    age_.add(static_cast<double>(age_ns) / 1e6);
    proc_.add(processing_us);
}

void FrameTiming::reset() { *this = FrameTiming{}; }

double FrameTiming::fps_total() const {
    if (frames < 2 || last_t_ns <= first_t_ns) return 0;
    return static_cast<double>(frames - 1) / (static_cast<double>(last_t_ns - first_t_ns) / 1e9);
}

double FrameTiming::fps_window(double window_s) const {
    if (times_.size() < 2) return 0;
    int64_t from = last_t_ns - static_cast<int64_t>(window_s * 1e9);
    int64_t oldest = last_t_ns;
    size_t n = 0;
    for (int64_t t : times_)
        if (t >= from && t <= last_t_ns) { ++n; oldest = std::min(oldest, t); }
    if (n < 2 || last_t_ns <= oldest) return 0;
    return static_cast<double>(n - 1) / (static_cast<double>(last_t_ns - oldest) / 1e9);
}

}  // namespace ravengun::vision
