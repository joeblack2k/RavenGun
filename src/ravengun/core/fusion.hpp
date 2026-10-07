#pragma once
// Oriëntatieschatting uit gyroscoop en accelerometer (IMU-only).
//
// Algoritme: Mahony-complementair filter met alleen een proportionele term (geen integrator),
// plus een aparte, expliciete gyro-biasschatter die alleen in stilstand bijwerkt.
// De magnetometer wordt hier (nog) niet gebruikt; yaw heeft dus geen absolute referentie
// en drift met de resterende bias. Zie docs/architecture.md.
//
// Eenheden in: gyro in rad/s (controllerstelsel), accelerometer in g (controllerstelsel,
// in rust ongeveer +1 g langs de "omhoog"-richting), dt in seconden.
#include <cstdint>
#include <deque>

#include "ravengun/core/math.hpp"

namespace ravengun::core {

struct BiasConfig {
    double window_s = 0.5;               // lengte van het stilstandsvenster
    double gyro_std_max = 0.035;         // rad/s (2 graden/s), max standaardafwijking per as binnen het venster
    double gyro_mean_max = 0.10;         // rad/s, max |gemiddelde - bias| (echte rotatie afwijzen)
    // Fase 4: zodra de bias "estimated" is, alleen nog vensters waarvan het gemiddelde hooguit zoveel van
    // de bias afwijkt (0,6 graden/s; de bias zelf verloopt veel trager). Daarvoor nam de schatter een
    // langzame zwaai tot 5,7 graden/s als stilstand op (synthetische test en richten met de hand).
    double gyro_mean_max_settled = 0.0105;
    double accel_norm_tol = 0.08;        // g, | |a| - 1 | binnen het hele venster
    // Bijwerken: tot tau_s aan stilstand gezien is een lopend gemiddelde over alle stille samples
    // (snelste convergentie), daarna een exponentieel gemiddelde met tijdconstante tau_s
    // (volgt langzame temperatuurdrift).
    double tau_s = 30.0;
    double settled_after_s = 3.0;        // cumulatieve stilstand voordat de schatting "estimated" heet
    // Alleen voor analyse (ravengun-analyze --freeze-bias): na zoveel seconden niet meer bijwerken, zoals
    // in de hand waar bijna nooit stilstand is. <= 0: uit.
    double freeze_after_s = -1;
};

enum class BiasState { None, Estimating, Estimated };
const char* to_string(BiasState s);

// Schat de gyro-offset (rad/s) uit perioden van stilstand.
// Beperking: een zeer langzame, gelijkmatige rotatie (< gyro_mean_max en zonder ruis boven
// gyro_std_max) is niet van bias te onderscheiden en wordt met tijdconstante tau_s deels opgenomen.
class GyroBiasEstimator {
public:
    explicit GyroBiasEstimator(BiasConfig cfg = {}) : cfg_(cfg) {}
    // Verwerk één subsample. Geeft true als het venster op dit moment als stil telt.
    bool update(Vec3 gyro, Vec3 accel, double dt);
    Vec3 bias() const { return bias_; }
    BiasState state() const;
    bool still() const { return still_; }
    double still_time_total() const { return still_total_; }
    void reset();
    const BiasConfig& config() const { return cfg_; }

private:
    struct S { double age; Vec3 g; double an; };
    BiasConfig cfg_;
    std::deque<S> win_;
    double win_span_ = 0;
    Vec3 bias_{};
    bool still_ = false;
    double still_total_ = 0;
    double age_ = 0;
};

struct FusionConfig {
    double kp = 0.5;                 // 1/s, terugkoppeling van de accelerometer (tilt-correctie)
    double accel_gate = 0.15;        // g, geen accelerometercorrectie als | |a| - 1 | groter is
    double max_dt_s = 0.25;          // langere gaten worden niet geïntegreerd (zie Pipeline)
};

class OrientationFilter {
public:
    explicit OrientationFilter(FusionConfig cfg = {}) : cfg_(cfg) {}
    // Eerste sample: zet roll/pitch uit de accelerometer, yaw = 0 (willekeurig).
    void init_from_accel(Vec3 accel);
    // Eén stap met een bias-gecorrigeerde gyro. Geeft false als dt ongeldig was (niet geïntegreerd).
    bool update(Vec3 gyro_unbiased, Vec3 accel, double dt);
    bool initialized() const { return init_; }
    Quat q() const { return q_; }  // q_wb: controller -> wereld (Z omhoog)
    // Alleen voor de yaw-correctie van de magnetometer (rotatie om wereld-Z, zie mag_yaw.hpp).
    void set_q(Quat q) { if (init_) q_ = normalized(q); }
    void reset() { init_ = false; q_ = Quat{}; }
    bool last_accel_used() const { return last_accel_used_; }
    const FusionConfig& config() const { return cfg_; }

private:
    FusionConfig cfg_;
    Quat q_{};
    bool init_ = false;
    bool last_accel_used_ = false;
};

}  // namespace ravengun::core
