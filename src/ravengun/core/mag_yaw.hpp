#pragma once
// Yaw-correctie met de magnetometer (fase 4): Mahony (tilt uit de accelerometer, ongewijzigd) plus een
// yaw-only magnetometerterm met een klein Kalman-filter op twee toestanden: de yaw-fout en de drift om de
// verticaal. Ontwerp: docs/aim-fusion-design.md, secties 2.2, 2.3 en 3.2; keuze en metingen in
// "Metingen fase 4".
//
// Per rapport (na de gyro-integratie van beide subsamples):
//  1. m_B = mag_apply(kalibratie, ruw); de bestaande MagDisturbanceDetector keurt het sample (norm, dip,
//     gyro-consistentie). Alleen geaccepteerde samples in toestand "ok" tellen.
//  2. φ = atan2(m_W.x, m_W.y) met m_W = q m_B q*: de wereldrichting van de horizontale veldcomponent.
//     Bij de eerste vergrendeling (gemiddelde over lock_s, pas als de gyro-bias geschat is) wordt φ_ref
//     vastgelegd. De magnetometer houdt de wereld-yaw daarna op die referentie. Geen sprong bij het
//     vergrendelen: de bestaande yaw blijft staan.
//  3. Meting e = φ - φ_ref. Een yaw-fout ε van de schatting (q = R_z(ε) q_waar) geeft e = -ε.
//     Kalman-filter op x = [fout f, drift d] met f' = d (rad, rad/s): f is de verwachte e, d de helling
//     van e (de resterende gyro-drift om de verticaal, met omgekeerd teken). Ruis: meting ~2,5° per
//     sample (gemeten: 3,8 counts op ~87 counts horizontaal veld), gyro-random-walk en een langzaam
//     veranderende drift (temperatuur, gemeten). Een innovatiepoort (χ², 1 vrijheidsgraad) wijst
//     uitschieters af.
//  4. De DRIFT wordt weggeregeld via een extra bias b_v om de verticaal: b_v volgt de geschatte drift,
//     met een begrensde veranderingssnelheid (bias_slew_dps2). Dat haalt de oorzaak weg (de yaw blijft
//     stil, ook in stilstand) zonder dat de hoek zelf verspringt. Dit is de continue
//     gyro-bias-schatting via de magnetometer.
//  5. De resterende FOUT f wordt weggewerkt met q <- R_z(Δ) q, Δ = f/tau_corr_s · dt, begrensd in
//     snelheid: rate_still_dps (0,02°/s) bij |ω| < still_gyro_dps, daarboven rate_still +
//     rate_per_motion·|ω| (een vaste fractie van de eigen draaisnelheid van de hand, verstopt in de
//     beweging), tot rate_moving_dps. Nooit een sprong.
// In de modus "mahony" draait dit in de schaduw: detectie en meting (f = opgebouwde drift sinds de
// vergrendeling), maar geen wijziging van q of de bias.
#include <cstdint>

#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/math.hpp"

namespace ravengun::core {

// mahony, mahony_mag: Mahony met (mag) yaw-Kalman hierboven. vqf, vqf_mag: VQF (Laidig & Seel, core/vqf_estimator.hpp),
// 6D zonder respectievelijk 9D met de gekalibreerde magnetometer.
enum class FusionMode { Mahony, MahonyMag, Vqf, VqfMag };
const char* to_string(FusionMode m);                       // "mahony", "mahony_mag", "vqf", "vqf_mag"
bool parse_fusion_mode(const char* s, FusionMode& out);    // ook "mahony-mag" en "vqf-mag"
inline bool is_vqf(FusionMode m) { return m == FusionMode::Vqf || m == FusionMode::VqfMag; }

struct MagYawConfig {
    // Kalman-filter (hoeken in graden in de configuratie).
    double meas_sigma_deg = 2.5;     // ruis van de magnetometer-heading per sample (gemeten)
    double yaw_rw_deg_rs = 0.08;     // gyro-random-walk om de verticaal (°/√s; ontwerp 3.1)
    double drift_rw_dps_rs = 0.02;   // verandering van de drift (°/s per √s); afgestemd op ledmag: −0,06°/s in 40 s
    double nis_gate = 6.63;          // χ²(1), 99 % (ontwerp 2.3)
    double init_drift_sigma_dps = 0.1;
    double reacquire_sigma_deg = 30; // na een onderbroken integratie
    // Correctie van de hoek.
    double tau_corr_s = 10.0;        // ontwerp 3.2: 5-20 s
    double rate_still_dps = 0.02;    // max hoekcorrectie bij |ω| < still_gyro_dps
    double rate_moving_dps = 0.3;    // max tijdens beweging
    double rate_per_motion = 0.02;   // daartussen: 2 % van de eigen draaisnelheid van de hand
    double still_gyro_dps = 3.0;
    double reacquire_rate_dps = 5.0; // na een gat of herverbinding (aim was toen ongeldig)
    double reacquire_max_s = 10.0;
    // Bias (drift wegregelen).
    double bias_slew_dps2 = 0.01;    // max verandering van b_v (°/s per s): de cursor versnelt nooit zichtbaar
    double bias_max_dps = 0.3;       // |b_v| (gemeten restdrift in de hand 0,035-0,15°/s)
    double bias_min_time_s = 5.0;    // pas na zoveel seconden gebruikte samples sinds de vergrendeling
    // Overig.
    double max_gyro_dps = 120.0;     // snellere rotatie: sample niet gebruiken (sampletijd onbekend)
    double lock_s = 2.0;             // middelen voor de referentie bij de vergrendeling (~0,2° ruis)
    double lock_wait_bias_s = 10.0;  // vergrendelen pas als de gyro-bias geschat is, of na zoveel seconden
    double min_horizontal = 0.25;    // |m_h| / |m|: daaronder is de heading slecht bepaald
    double accel_tol_g = 0.05;       // alleen samples met | |a| - 1 | hieronder (tilt uit de accelerometer)
    MagDisturbanceConfig detector{};
};

struct MagYawStatus {
    bool calibrated = false;
    MagState state = MagState::Unknown;  // van de detector
    bool locked = false;                 // referentie vastgelegd
    bool used = false;                   // laatste sample gebruikt in het filter
    bool applying = false;               // laatste rapport werd gecorrigeerd (modus mahony_mag)
    bool reacquire = false;
    double pending_deg = 0;              // f: openstaande yaw-fout volgens het filter (graden)
    double sigma_deg = 0;                // σ van f (graden); groeit zonder bruikbare samples
    double drift_dps = 0;                // geschatte resterende drift (°/s, helling van e)
    double rate_dps = 0;                 // laatst toegepaste hoekcorrectie (°/s, met teken)
    double rate_limit_dps = 0;           // de grens die daarbij gold
    double total_deg = 0;                // som van alle toegepaste hoekcorrecties
    double max_step_deg = 0;             // grootste hoekcorrectie in één rapport
    double vertical_bias_dps = 0;        // b_v
    double unusable_s = 0;               // sinds het laatst gebruikte sample
    uint64_t samples = 0, used_samples = 0, innov_rejected = 0, fast_skipped = 0, weak_horizontal = 0, accel_skipped = 0;
    MagCheck last{};
};

class MagYawCorrector {
public:
    explicit MagYawCorrector(MagYawConfig c = {}) : cfg_(c), det_(c.detector) {}
    void set_calibration(const MagCalibration& c);
    void clear_calibration();
    bool calibrated() const { return have_cal_; }
    const MagCalibration& calibration() const { return cal_; }

    // Eén magnetometersample (één per rapport), na de integratie. q wordt alleen gewijzigd als apply true
    // is. gyro: bias-gecorrigeerd gemiddelde van het rapport (rad/s, B). bias_settled: de
    // stilstandsbiasschatter heeft een schatting (dan pas vergrendelen).
    void update(Quat& q, const int mag_raw[3], Vec3 accel, Vec3 gyro, double dt, bool apply, bool bias_settled = true);
    // Extra gyro-bias in B (rad/s) voor de volgende integratie: b_v langs de verticaal.
    Vec3 bias_body(Quat q) const;
    // De yaw-integratie is onderbroken (gat > max_dt, herverbinding): grote onzekerheid, gladde terugkeer.
    void on_interrupt();
    // Nieuwe referentie op de huidige stand (bij het inschakelen van de correctie).
    void relock();
    void reset();
    const MagYawStatus& status() const { return st_; }
    const MagYawConfig& config() const { return cfg_; }
    const MagDisturbanceDetector& detector() const { return det_; }

private:
    MagYawConfig cfg_;
    MagCalibration cal_;
    bool have_cal_ = false;
    MagDisturbanceDetector det_;
    MagYawStatus st_;
    double age_ = 0;
    // Vergrendeling
    double lock_c_ = 0, lock_s_ = 0, lock_t_ = 0;
    double phi_ref_ = 0;
    // Kalman-filter (rad, rad/s)
    double f_ = 0, d_ = 0;
    double P00_ = 0, P01_ = 0, P11_ = 0;
    double used_t_ = 0;   // s gebruikte samples sinds vergrendeling
    double b_v_ = 0;      // rad/s
    double reacq_t_ = 0;
};

}  // namespace ravengun::core
