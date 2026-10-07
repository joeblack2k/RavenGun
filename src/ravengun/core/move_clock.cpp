#include "ravengun/core/move_clock.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ravengun::core {

void MoveClock::reset() { *this = MoveClock(cfg_); }

int64_t MoveClock::map(int64_t ticks) const {
    double x = static_cast<double>(ticks - ref_ticks_) * static_cast<double>(kMoveNsPerTick);
    double off = static_cast<double>(ref_off_) + a_ns_ + skew_ * x;
    return ticks * kMoveNsPerTick + static_cast<int64_t>(std::llround(off));
}

void MoveClock::refit() {
    // Rechte door de minima (kleinste kwadraten), daarna omlaag tot onder alle minima.
    size_t n = bins_.size();
    if (n == 0) return;
    if (static_cast<int>(n) < cfg_.min_bins) {
        skew_ = 0;
        int64_t m = bins_.front().off_ns;
        for (const auto& b : bins_) m = std::min(m, b.off_ns);
        a_ns_ = static_cast<double>(m - ref_off_);
        return;
    }
    // De lopende (onvolledige) bak telt niet mee: zijn "minimum" kan nog een vertraagd rapport
    // zijn en zou de helling scheeftrekken. De lijn wordt voor de nieuwste rapporten dus
    // geëxtrapoleerd vanuit de afgesloten bakken (skew < 1e-4: verwaarloosbaar over 1 s).
    if (static_cast<int>(n) - 1 >= cfg_.min_bins) --n;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; ++i) {
        const Bin& b = bins_[i];
        double x = static_cast<double>(b.ticks - ref_ticks_) * static_cast<double>(kMoveNsPerTick);
        double y = static_cast<double>(b.off_ns - ref_off_);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double dn = static_cast<double>(n);
    double den = dn * sxx - sx * sx;
    double s = den > 0 ? (dn * sxy - sx * sy) / den : 0;
    // Klokafwijking van kristallen is < 1e-3; een grotere helling is ruis of een fout.
    s = std::clamp(s, -1e-3, 1e-3);
    double a = (sy - s * sx) / dn;
    double shift = 0;
    for (size_t i = 0; i < n; ++i) {
        const Bin& b = bins_[i];
        double x = static_cast<double>(b.ticks - ref_ticks_) * static_cast<double>(kMoveNsPerTick);
        double y = static_cast<double>(b.off_ns - ref_off_);
        shift = std::min(shift, y - (a + s * x));
    }
    a_ns_ = a + shift;
    skew_ = s;
}

ClockSample MoveClock::add(uint16_t stamp, int64_t t_rx_ns) {
    ClockSample cs;
    ++samples_;
    if (!have_) {
        have_ = true;
        last_ticks_ = stamp;
        ref_ticks_ = stamp;
        ref_off_ = t_rx_ns - static_cast<int64_t>(stamp) * kMoveNsPerTick;
        cs.first = true;
    } else {
        int d = stamp_delta(last_stamp_, stamp);
        double host_ticks = static_cast<double>(t_rx_ns - last_rx_) / static_cast<double>(kMoveNsPerTick);
        long k = std::lround((host_ticks - d) / static_cast<double>(kMoveStampModulo));
        if (k < 0) k = 0;
        if (k > 0) ++wrap_corr_;
        cs.wraps_added = static_cast<int>(k);
        cs.dticks = d + static_cast<int64_t>(k) * kMoveStampModulo;
        last_ticks_ += cs.dticks;
    }
    last_stamp_ = stamp;
    last_rx_ = t_rx_ns;
    cs.ticks = last_ticks_;
    cs.dt_s = static_cast<double>(cs.dticks) / kMoveTicksPerSecond;

    // Minimum per bak bijhouden.
    int64_t off = t_rx_ns - cs.ticks * kMoveNsPerTick;
    int64_t bin_ticks = std::max<int64_t>(1, static_cast<int64_t>(cfg_.bin_s * kMoveTicksPerSecond));
    int64_t idx = (cs.ticks - ref_ticks_) / bin_ticks;
    bool changed = false;
    if (bins_.empty() || bins_.back().index < idx) {
        bins_.push_back({idx, cs.ticks, off});
        changed = true;
    } else if (bins_.back().index == idx && off < bins_.back().off_ns) {
        bins_.back().ticks = cs.ticks;
        bins_.back().off_ns = off;
        changed = true;
    }
    int64_t keep = std::max<int64_t>(1, static_cast<int64_t>(cfg_.window_s / cfg_.bin_s));
    while (!bins_.empty() && bins_.back().index - bins_.front().index >= keep) { bins_.pop_front(); changed = true; }
    if (changed) refit();

    cs.coupled = coupled();
    cs.t_mapped_ns = map(cs.ticks);
    cs.residual_ms = static_cast<double>(t_rx_ns - cs.t_mapped_ns) / 1e6;
    return cs;
}

void ResidualStats::add(double ms) {
    ++n_;
    max_ = std::max(max_, ms);
    ring_.push_back(ms);
    if (ring_.size() > 8192) ring_.pop_front();
}

double ResidualStats::percentile(double p) const {
    if (ring_.empty()) return 0;
    std::vector<double> v(ring_.begin(), ring_.end());
    size_t k = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

}  // namespace ravengun::core
