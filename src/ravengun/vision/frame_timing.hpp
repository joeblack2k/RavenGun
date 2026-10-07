#pragma once
// Statistiek van cameraframes: werkelijke fps, verdeling van frame-intervallen (uit de
// V4L2-timestamps), gemiste frames (gaten in het V4L2-volgnummer) en de leeftijd van een frame
// op het moment van verwerken. Geen hardware; getest met synthetische tijden.
#include <array>
#include <cstdint>
#include <vector>

namespace ravengun::vision {

// Ring met de laatste N waarden en percentielen daarover.
class Ring {
public:
    explicit Ring(size_t n = 4096) : n_(n) {}
    void add(double v);
    void clear() { v_.clear(); pos_ = 0; }
    size_t size() const { return v_.size(); }
    // Lineair geïnterpoleerd percentiel (p in [0,1]); 0 als leeg.
    double percentile(double p) const;
    double max() const;
    double mean() const;

private:
    size_t n_;
    std::vector<double> v_;
    size_t pos_ = 0;
};

class FrameTiming {
public:
    // Bovengrenzen (ms) van de intervalbakken; de laatste bak is open.
    static constexpr std::array<double, 8> kBucketUpperMs{4, 6, 9, 12, 18, 25, 50, 1e30};

    // Eén ontvangen frame: V4L2-timestamp (ns, CLOCK_MONOTONIC), V4L2-volgnummer en
    // ontvangsttijd (ns) in de capture-thread.
    void on_frame(int64_t t_frame_ns, uint32_t sequence, int64_t t_dequeue_ns);
    // Leeftijd van het verwerkte frame: t_verwerking - t_frame.
    void on_processed(int64_t age_ns, double processing_us);
    void reset();

    uint64_t frames = 0;
    uint64_t missed = 0;           // som van gaten in sequence (sequence[i] - sequence[i-1] - 1)
    uint64_t gap_events = 0;       // aantal keren dat er een gat was
    uint64_t non_monotonic = 0;    // timestamp kleiner dan of gelijk aan de vorige
    uint64_t sequence_reset = 0;   // volgnummer liep terug (stream herstart)
    std::array<uint64_t, kBucketUpperMs.size()> buckets{};
    int64_t first_t_ns = 0, last_t_ns = 0;
    uint32_t last_seq = 0;

    // fps uit timestamps over alle frames sinds de start (of de laatste reset).
    double fps_total() const;
    // fps uit timestamps over het venster [laatste - window_s, laatste].
    double fps_window(double window_s) const;

    const Ring& intervals_ms() const { return iv_; }
    const Ring& dequeue_delay_ms() const { return dq_; }
    const Ring& age_ms() const { return age_; }
    const Ring& processing_us() const { return proc_; }

private:
    Ring iv_{8192}, dq_{4096}, age_{4096}, proc_{4096};
    std::vector<int64_t> times_;  // ring van frametijden voor fps_window
    size_t times_pos_ = 0;
};

}  // namespace ravengun::vision
