#pragma once
// Tijdbasis van de Move: ontrollen van de 16-bit interne tijdstempel en koppeling met de
// hostklok (CLOCK_MONOTONIC) met de ondergrensmethode.
//
// Model: t_rx = t_move * (1 + skew) + offset + vertraging, met vertraging >= 0 (Bluetooth,
// kernel, poll-lus). Per bak van bin_s seconden (in Move-tijd) wordt het kleinste
// (t_rx - t_move) bewaard; over de laatste window_s seconden wordt door die minima een rechte
// gefit en daarna omlaag geschoven tot hij onder alle minima ligt (ondergrens). De gekoppelde tijd
// t_mapped = t_move + lijn is dus "stempelmoment plus de kleinste waargenomen transportvertraging",
// niet het belichtings- of samplemoment van de sensor (die vaste rest is zonder externe referentie
// niet meetbaar). residual = t_rx - t_mapped >= 0 is de extra vertraging van dit rapport.
//
// Ontrollen: de stempel wrapt na 65536 ticks (0,655 s). Het aantal gemiste wraps volgt uit de
// hosttijd: k = round((dt_host_ticks - delta) / 65536). Bluetooth-jitter (< 50 ms = 5000 ticks)
// is ruim kleiner dan een halve wrap, dus dit is eenduidig, ook na lange gaten.
#include <cstdint>
#include <deque>

#include "ravengun/core/move_report.hpp"

namespace ravengun::core {

struct ClockConfig {
    double window_s = 20.0;  // venster voor de fit (Move-tijd)
    double bin_s = 1.0;      // bakgrootte voor de minima
    int min_bins = 3;        // daaronder: alleen offset (skew 0)
};

struct ClockSample {
    bool first = false;          // eerste stempel na reset (geen delta)
    int64_t ticks = 0;           // ontrolde stempel (ticks sinds de eerste stempel + eerste stempel)
    int64_t dticks = 0;          // ticks sinds het vorige rapport (0 bij first)
    double dt_s = 0;             // dticks / ticks_per_s
    int wraps_added = 0;         // extra wraps uit de hosttijd (> 0 alleen na een gat > 0,65 s)
    int64_t t_mapped_ns = 0;     // gekoppelde hosttijd van deze stempel
    double residual_ms = 0;      // t_rx - t_mapped
    bool coupled = false;        // fit met skew actief (>= min_bins bakken)
};

class MoveClock {
public:
    explicit MoveClock(ClockConfig c = {}) : cfg_(c) {}
    ClockSample add(uint16_t stamp, int64_t t_rx_ns);
    void reset();
    // Hosttijd (ns) die bij een ontrolde stempel hoort; geldig zodra er minstens één stempel is.
    int64_t map(int64_t ticks) const;
    bool coupled() const { return static_cast<int>(bins_.size()) > cfg_.min_bins; }
    double skew_ppm() const { return skew_ * 1e6; }
    uint64_t samples() const { return samples_; }
    uint64_t wrap_corrections() const { return wrap_corr_; }
    int64_t last_ticks() const { return last_ticks_; }
    const ClockConfig& config() const { return cfg_; }

private:
    struct Bin { int64_t index; int64_t ticks; int64_t off_ns; };
    void refit();

    ClockConfig cfg_;
    bool have_ = false;
    uint16_t last_stamp_ = 0;
    int64_t last_rx_ = 0;
    int64_t last_ticks_ = 0;
    int64_t ref_ticks_ = 0;     // referentie voor de fit (eerste stempel)
    int64_t ref_off_ = 0;       // offset (t_rx - ticks*ns_per_tick) van het eerste rapport
    double a_ns_ = 0;           // lijn: off(t) = ref_off + a + skew * (ticks - ref_ticks) * ns_per_tick
    double skew_ = 0;
    std::deque<Bin> bins_;
    uint64_t samples_ = 0, wrap_corr_ = 0;
};

// Ingebouwde statistiek voor de status en de analyse: verdeling van de restvertraging.
class ResidualStats {
public:
    void add(double ms);
    double percentile(double p) const;
    uint64_t count() const { return n_; }
    double max() const { return max_; }
    void reset() { *this = ResidualStats{}; }

private:
    std::deque<double> ring_;
    uint64_t n_ = 0;
    double max_ = 0;
};

}  // namespace ravengun::core
