#pragma once
// Verwerkingspijplijn: HID-rapport -> biasschatting -> oriëntatie -> aim -> 1€-filter -> InputSample.
// Bevat geen hardwarecode en geen threads; de HID-thread roept dit aan, tests doen dat met
// synthetische rapporten.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/fusion.hpp"
#include "ravengun/core/input_model.hpp"
#include "ravengun/core/move_clock.hpp"
#include "ravengun/core/one_euro.hpp"

namespace ravengun::core {

struct PipelineConfig {
    int64_t stale_ns = 50'000'000;     // daarna: aim/oriëntatie ongeldig
    int64_t release_ns = 250'000'000;  // daarna: alle knoppen vrijgeven
    bool auto_center_on_start = true;  // eenmalig per daemon-run, zichtbaar als center_source=auto_initial
    FusionConfig fusion{};
    BiasConfig bias{};
    AimConfig aim{};
    OneEuroConfig filter{};
    // Integratie-dt uit de interne tijdstempel van de Move (fase 1) in plaats van de ontvangsttijd.
    // Alleen als dit én het vorige rapport een stempel hebben; anders de ontvangst-dt.
    bool use_move_clock = true;
    ClockConfig clock{};
};

// Statistiek over de ontvangst van rapporten (alleen gemeten waarden).
class ReportStats {
public:
    static constexpr std::array<double, 9> kBucketUpperMs{5, 10, 15, 20, 30, 50, 100, 250, 1e30};
    void add_interval(double ms);
    void add_report(int64_t t_ns);
    void reset();
    uint64_t reports = 0;
    uint64_t intervals = 0;
    uint64_t dropped_seq = 0;          // gemiste rapporten volgens het 4-bit volgnummer
    uint64_t non_monotonic = 0;        // t_rx kleiner dan vorige (zou niet mogen)
    uint64_t long_dt = 0;              // 50..250 ms: geïntegreerd met constante hoeksnelheid
    uint64_t gap_not_integrated = 0;   // > max_dt: niet geïntegreerd, yaw-referentie onderbroken
    uint64_t still_samples = 0;
    std::array<uint64_t, 9> buckets{};
    double max_ms = 0;
    int64_t first_t_ns = 0, last_t_ns = 0;
    // Percentiel over de laatste (max 16384) intervallen.
    double percentile(double p) const;
    // Rapporten per seconde over het venster [now-window, now].
    double rate(int64_t now_ns, double window_s) const;

private:
    std::vector<double> ring_;
    size_t ring_pos_ = 0;
    std::vector<int64_t> times_;  // ring van ontvangsttijden voor de frequentie
    size_t times_pos_ = 0;
};

class Pipeline {
public:
    explicit Pipeline(PipelineConfig cfg = {});

    // Nieuwe controller geopend. Een ander controller-ID reset de volledige toestand.
    void on_connect(const std::string& controller_id, int64_t now_ns);
    // Eén rapport verwerken (beide subsamples, in volgorde). Geeft het gepubliceerde sample.
    InputSample on_report(const RawReport& r);
    // Tijd laten verstrijken zonder rapport. Geeft een sample als de toestand verandert
    // (Fresh -> Stale -> Lost), anders niets.
    std::optional<InputSample> tick(int64_t now_ns);
    // Controller weg: knoppen vrij, alles ongeldig.
    InputSample on_disconnect(int64_t now_ns);
    // Expliciete recenter. false als er geen geldige oriëntatie is.
    bool recenter();

    void set_filter(const OneEuroConfig& c);
    void set_aim(const AimConfig& c);
    const PipelineConfig& config() const { return cfg_; }
    const InputSample& last() const { return last_; }
    const ReportStats& stats() const { return stats_; }
    EdgeAccumulator& edges() { return edges_; }
    const GyroBiasEstimator& bias() const { return bias_; }
    uint64_t auto_center_count() const { return auto_centers_; }
    uint64_t user_center_count() const { return user_centers_; }
    // Tijdbasis (fase 1).
    const MoveClock& clock() const { return clock_; }
    const ResidualStats& clock_residuals() const { return clock_res_; }
    const ClockSample& last_clock() const { return last_clock_; }
    uint64_t dt_from_move_clock() const { return dt_move_; }
    uint64_t dt_from_rx() const { return dt_rx_; }
    int last_temperature_raw() const { return last_temp_; }

private:
    InputSample make_sample(int64_t t_eval);
    void fill_aim(InputSample& s, double dt);

    PipelineConfig cfg_;
    GyroBiasEstimator bias_;
    OrientationFilter fusion_;
    AimMapper aim_;
    OneEuro fx_, fy_;
    EdgeAccumulator edges_;
    ReportStats stats_;
    InputSample last_{};
    std::string controller_id_;
    uint64_t seq_ = 0;
    int64_t last_rx_ns_ = 0;
    bool have_rx_ = false;
    int last_hid_seq_ = -1;
    LinkState link_ = LinkState::Disconnected;
    YawReference yaw_ref_ = YawReference::None;
    bool auto_centered_once_ = false;
    uint64_t auto_centers_ = 0, user_centers_ = 0;
    bool reset_filter_ = true;
    MoveClock clock_;
    ResidualStats clock_res_;
    ClockSample last_clock_{};
    bool prev_had_stamp_ = false;
    uint64_t dt_move_ = 0, dt_rx_ = 0;
    int last_temp_ = -1;
};

}  // namespace ravengun::core
