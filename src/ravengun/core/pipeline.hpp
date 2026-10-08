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
#include "ravengun/core/mag_yaw.hpp"
#include "ravengun/core/move_clock.hpp"
#include "ravengun/core/one_euro.hpp"
#include "ravengun/core/predict.hpp"
#include "ravengun/core/vqf_estimator.hpp"
#include "ravengun/core/yaw_trim.hpp"

namespace ravengun::core {

struct PipelineConfig {
    int64_t stale_ns = 50'000'000;     // daarna: aim/oriëntatie ongeldig
    int64_t release_ns = 250'000'000;  // daarna: alle knoppen vrijgeven
    bool auto_center_on_start = true;  // eenmalig per daemon-run, zichtbaar als center_source=auto_initial
    FusionConfig fusion{};
    BiasConfig bias{};
    AimConfig aim{};
    OneEuroConfig filter{};
    // dt voor het 1€-filter uit de Move-stempel (fysieke tijd tussen twee rapporten) in plaats van de ontvangsttijd. De rapporten
    // komen via Bluetooth in paren binnen (afwisselend ~5 en ~20 ms), de stempels staan regelmatig op 11,3 ms.
    bool filter_dt_move = true;
    // Voorspelling van de oriëntatie vóór de aim-omrekening (predict.hpp); horizon_ms = 0 is uit.
    PredictConfig predict{};
    // Integratie-dt uit de interne tijdstempel van de Move (fase 1) in plaats van de ontvangsttijd.
    // Alleen als dit én het vorige rapport een stempel hebben; anders de ontvangst-dt.
    bool use_move_clock = true;
    ClockConfig clock{};
    // Fase 4: oriëntatieschatter. mahony = IMU-only (terugval); mahony_mag = met yaw-correctie van de
    // magnetometer (vereist een kalibratie via set_mag_calibration; zonder blijft het IMU-only).
    FusionMode fusion_mode = FusionMode::Mahony;
    // VQF (modes vqf en vqf_mag): draait altijd mee in de schaduw (ook in mahony-modes), zodat omschakelen
    // een warme schatter geeft; de uitvoer wordt bij het omschakelen op dezelfde richtas uitgelijnd.
    VqfConfig vqf{};
    // Verloren rapporten (fase 4, ~13 % via Bluetooth): bij een stempel-dt boven 1,5 rapportperiode
    // wordt het deel vóór de twee subsamples van het nieuwe rapport geïntegreerd met het gemiddelde van
    // de laatste hoeksnelheid vóór het gat en de eerste erna (lineaire interpolatie), en krijgt elk
    // subsample zijn nominale halve periode. Zonder: elk subsample de helft van het gat (oud gedrag).
    bool interpolate_gaps = true;
    double nominal_report_s = 0.01127;  // gemeten: 1127 ticks per rapport
    MagYawConfig mag{};
    // Sharp Shooter via de EXT-connector van de Move (zie buttons.hpp, ExtMode).
    ExtMode ext_mode = ExtMode::Auto;
    // De pomp (afgeleid uit T en het fire-bit, zie SharpShooterPump). on/off volgen gun_buttons.trigger_on/off.
    PumpConfig pump{};
    // Lerende yaw-trim uit gebruikers-centreerdrukken (zie yaw_trim.hpp).
    YawTrimConfig trim{};
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
    uint64_t gap_interpolated = 0;     // verloren rapporten overbrugd met geïnterpoleerde hoeksnelheid
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
    // Expliciete recenter. false als er geen geldige oriëntatie is. from_controller: de druk komt van de Move (PS of een
    // knop met actie recenter): de speler richt dan op het schermmidden en de druk telt als meetpunt voor de yaw-trim.
    bool recenter(bool from_controller = false);
    // Alleen het meetpunt voor de yaw-trim (zonder het midden te verleggen), voor replay: een markerdruk in een opname.
    bool note_recenter_press();
    const YawTrim& trim() const { return trim_; }
    void set_trim(double dps, uint32_t learned = 0) { trim_.set(dps, learned); }
    void clear_trim() { trim_.clear(); }
    void set_trim_learning(bool on) { cfg_.trim.enabled = on; trim_.set_enabled(on); }
    // Optische meting van de bol (uit de camera); alleen het bewijs voor de gyro-bias (stilstand in beeld).
    void on_optical(const OpticalSample& o);

    void set_filter(const OneEuroConfig& c);
    void set_aim(const AimConfig& c);
    // Voorspelling live instellen (geen herstart, geen reset van de schatters).
    void set_predict(const PredictConfig& c);
    const RatePredictor& predictor() const { return pred_; }
    // Fase 4: magnetometer en schatter.
    void set_mag_calibration(const MagCalibration& c);
    void clear_mag_calibration() { mag_.clear_calibration(); }
    void set_fusion_mode(FusionMode m);
    FusionMode fusion_mode() const { return cfg_.fusion_mode; }
    const MagYawCorrector& mag() const { return mag_; }
    const VqfEstimator& vqf() const { return vqf_; }
    // De oriëntatie die de aim gebruikt: van het actieve filter (Mahony of VQF).
    Quat orientation() const { return q_out(); }
    // Yaw/pitch van de richtas t.o.v. het aim-midden volgens de actieve schatter (zonder 1€-filter); (0,0) zonder midden.
    AimAngles aim_relative() const { return aim_.relative(q_out()); }
    const PipelineConfig& config() const { return cfg_; }
    const InputSample& last() const { return last_; }
    const ReportStats& stats() const { return stats_; }
    EdgeAccumulator& edges() { return edges_; }
    const GyroBiasEstimator& bias() const { return bias_; }
    // Bewaarde gyro-bias als beginwaarde (rad/s, controllerstelsel; tc = ruwe temperatuur of -1). Doet niets als
    // er al gemeten is. Aanroepen na on_connect().
    void restore_bias(Vec3 bias, int tc) { bias_.restore(bias, tc); }
    // Corrigeert de magnetometer nu de yaw (mahony_mag, vergrendeld, toestand ok)?
    bool mag_yaw_active() const;
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
    Quat q_out() const { return is_vqf(cfg_.fusion_mode) && vqf_.initialized() ? vqf_.q() : fusion_.q(); }
    bool vqf_mag_active() const;
    InputSample make_sample(int64_t t_eval);
    void fill_aim(InputSample& s, double dt, double dt_move = 0);

    PipelineConfig cfg_;
    GyroBiasEstimator bias_;
    OrientationFilter fusion_;
    MagYawCorrector mag_;
    VqfEstimator vqf_;
    AimMapper aim_;
    OneEuro fx_, fy_;
    RatePredictor pred_;
    bool have_out_ = false;       // vorige gefilterde uitvoer bekend (voor de snelheid van de uitvoerklok)
    double out_x_ = 0, out_y_ = 0;
    EdgeAccumulator edges_;
    SharpShooterPump pump_;
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
    YawTrim trim_;
    bool trim_mag_seen_ = false;      // sinds de vorige druk corrigeerde de magnetometer de yaw
    int64_t last_optical_t_ = -1;
    Vec3 prev_gyro_{};          // laatste (ruwe) subsample van het vorige rapport
    bool have_prev_gyro_ = false;
};

}  // namespace ravengun::core
