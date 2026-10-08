#pragma once
// Oriëntatieschatting uit gyroscoop en accelerometer (IMU-only).
//
// Algoritme: Mahony-complementair filter met alleen een proportionele term (geen integrator),
// plus een aparte, expliciete gyro-biasschatter: stilstand, een bewaarde beginwaarde en quasi-stille
// vensters tijdens het spelen (zie BiasConfig).
// De magnetometer wordt hier (nog) niet gebruikt; yaw heeft dus geen absolute referentie
// en drift met de resterende bias. Zie docs/architecture.md.
//
// Eenheden in: gyro in rad/s (controllerstelsel), accelerometer in g (controllerstelsel,
// in rust ongeveer +1 g langs de "omhoog"-richting), dt in seconden.
#include <cstdint>
#include <deque>

#include "ravengun/core/math.hpp"

namespace ravengun::core {

// legacy: alleen de stilstandsschatter (gedrag van voor de Halo-sessie van 2026-10-08).
// new: stilstand + herstel van een bewaarde bias + "quasi-stille" vensters tijdens het spelen.
enum class BiasMode { Legacy, New };
const char* to_string(BiasMode m);
bool parse_bias_mode(const char* s, BiasMode& out);

struct BiasConfig {
    BiasMode mode = BiasMode::New;
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

    // ---- mode new ----
    // Met een bewaarde beginbias (restored) mogen de eerste stille vensters hooguit zoveel van die waarde
    // afwijken (1,5 graden/s; dezelfde Move herhaalde gisteren en vandaag binnen 0,2 graden/s). Een venster
    // verder weg is bijna zeker nog beweging; blijft zo'n punt 10 s staan, dan herstelt reacquire de bias.
    double restored_gate = 0.0262;
    // De bewaarde waarde telt mee als zoveel seconden stilstand, zodat een paar losse (vervuilde) vensters
    // haar niet meteen vervangen; een Move die echt stil ligt overtuigt binnen enkele seconden.
    double restored_weight_s = 1.5;
    // Herstel na een lange rust: stille vensters die steeds op hetzelfde punt ver van de bias liggen
    // (bijvoorbeeld na een gedateerde bewaarde waarde of een bias die tijdens het spelen is weggelopen).
    // Bewust langer dan een bewuste langzame draai (5 s aan 3 graden/s mag nooit als bias tellen).
    double reacquire_s = 10.0;
    double reacquire_spread = 0.0087;    // rad/s (0,5 graden/s): zo dicht moeten die vensters bij elkaar liggen
    // Quasi-stille vensters (niet-overlappend, quasi_window_s): de bias volgt, zonder stilleggen, het
    // gewogen medium van de vensters waarvan de standaardafwijking klein is en de accelerometer ongeveer 1 g
    // geeft. Een venster telt alleen mee als zijn gemiddelde binnen quasi_gate van de huidige bias ligt: een
    // bewuste draai (>= 3 graden/s) valt erbuiten, de trage bias (temperatuur) niet.
    double quasi_window_s = 0.5;
    double quasi_std_max = 0.0436;       // rad/s (2,5 graden/s), per as
    double quasi_accel_tol = 0.10;       // g, | |a| - 1 | binnen het venster
    double quasi_gate = 0.0209;          // rad/s (1,2 graden/s) rond de bias
    double quasi_horizon_s = 90.0;       // vensters ouder dan dit tellen niet meer
    double quasi_recency_s = 30.0;       // weging exp(-leeftijd / dit)
    int quasi_min_windows = 6;           // minimaal aantal vensters voordat de bias beweegt
    double quasi_tau_s = 10.0;           // de bias loopt naar het doel met deze tijdconstante
    double quasi_slew = 0.00052;         // rad/s per s (0,03 graden/s per s): grootste snelheid van verandering
    // Eerste schatting zonder stilstand (geen bewaarde bias): het gemiddelde van minstens acquire_windows
    // vensters binnen acquire_gate van nul, waarvan het grootste deel dicht bij elkaar ligt.
    double acquire_gate = 0.14;          // rad/s (8 graden/s)
    int acquire_windows = 16;            // 8 s
    double acquire_cluster = 0.0157;     // rad/s (0,9 graden/s) straal van de cluster
    double acquire_fraction = 0.6;
    // Temperatuurcompensatie: de bias loopt mee met de ruwe temperatuur ("tc", elke ~3 s een telling erbij tijdens
    // het opwarmen). Uit de stille en quasi-stille vensters van de afgelopen temp_horizon_s wordt per as een
    // helling (rad/s per telling) geschat (gewogen kleinste kwadraten); alleen als de temperatuur in dat bewijs
    // minstens temp_min_range tellingen is verlopen, en verkleind naar nul als ze niet significant is. Bij elke
    // verandering van de temperatuur schuift de bias met helling x verandering mee (geleidelijk, geen sprong).
    bool temp_comp = true;
    double temp_horizon_s = 600.0;
    double temp_recency_s = 300.0;
    int temp_min_entries = 12;
    int temp_min_range = 25;
    double temp_slope_max = 0.000175;    // rad/s per telling (0,01 graden/s per telling)
    double temp_sigma_floor = 0.0044;    // rad/s (0,25 graden/s): ondergrens voor de spreiding van een venstergemiddelde
    double temp_max_shift = 0.035;       // rad/s (2 graden/s): grootste stap per temperatuurwijziging (glitchbeveiliging)
    double temp_max_total = 0.0175;      // rad/s (1 graden/s): meer dan dit schuift het model niet op sinds de laatste meting

    // ---- optische stilstand (mode new): de camera als bewaker van de bias ----
    // Staat de bol optische_window_s stil in beeld (spreiding van x, y en straal < optical_still_px, minstens
    // optical_min_frames geldige frames, geen gat tussen frames boven optical_frame_gap_s), dan beweegt de Move niet
    // en is het gemiddelde van de gyro in dat venster de bias (gemeten spreiding ongeveer 0,19 graden/s per venster),
    // ook als de speler de gun vasthoudt en de IMU zelf nooit "stil" ziet. De IMU moet dat venster bevestigen: de
    // accelerometer dicht bij 1 g en een kleine spreiding van de gyro. De vensters sturen de bias als het gewogen
    // medium van de laatste optical_horizon_s, geleidelijk (tijdconstante en maximale snelheid, geen sprong).
    bool optical = true;
    double optical_still_px = 0.2;        // sd van x, y en straal binnen het venster (pixels)
    int optical_min_frames = 24;          // geldige frames per venster van 0,5 s
    double optical_window_s = 0.5;
    double optical_frame_gap_s = 0.05;    // een gat tussen twee geldige frames (bol weg) breekt het venster
    double optical_lag_s = 0.0;           // cameraframe-tijd + dit = IMU-tijd (V4L2 en HID delen CLOCK_MONOTONIC)
    double optical_std_max = 0.0436;      // rad/s (2,5 graden/s) per as: de IMU bevestigt dat het venster rustig was
    double optical_accel_tol = 0.10;      // g, | |a| - 1 | per sample
    double optical_accel_mean_tol = 0.05; // g, | |gemiddelde a| - 1 |
    double optical_gate = 0.0698;         // rad/s (4 graden/s) rond de bias als die bekend is
    double optical_gate_acquire = 0.14;   // rad/s (8 graden/s) zolang er niets gemeten is
    int optical_min_windows = 4;          // minimaal aantal vensters voordat de bias beweegt
    int optical_acquire_windows = 8;      // zoveel vensters nodig om de bias te "vestigen" (eerste meting)
    double optical_horizon_s = 60.0;
    double optical_recency_s = 20.0;
    double optical_tau_s = 5.0;           // de bias loopt naar het doel met deze tijdconstante
    double optical_slew = 0.0026;         // rad/s per s (0,15 graden/s per s): grootste snelheid van verandering
};

enum class BiasState { None, Restored, Estimating, Estimated };
const char* to_string(BiasState s);
// Waar de huidige bias vandaan komt (laatste wijziging): bewaarde waarde, echte stilstand, quasi-stil of optisch stil (camera).
enum class BiasSource { None, Restored, Still, Quasi, Optical };
const char* to_string(BiasSource s);

// Schat de gyro-offset (rad/s) uit perioden van stilstand en (mode new) uit quasi-stille vensters.
// Beperking: een zeer langzame, gelijkmatige rotatie (< gyro_mean_max en zonder ruis boven
// gyro_std_max) is niet van bias te onderscheiden en wordt met tijdconstante tau_s deels opgenomen.
class GyroBiasEstimator {
public:
    explicit GyroBiasEstimator(BiasConfig cfg = {}) : cfg_(cfg) {}
    // Verwerk één subsample. Geeft true als het venster op dit moment als stil telt.
    // t_s: tijd van dit subsample (CLOCK_MONOTONIC, seconden) voor het optische bewijs; < 0 = onbekend (dan telt
    // het subsample niet mee als optisch bewijs).
    bool update(Vec3 gyro, Vec3 accel, double dt, double t_s = -1);
    // Een cameraframe-meting (tijd van het frame, geldig, positie en straal in pixels). Niet geldig = de bol is weg.
    void optical_frame(double t_s, bool valid, double x, double y, double r);
    // Totale duur (s) van de optisch-stille vensters die de bias mochten sturen.
    double optical_time_total() const { return optical_total_; }
    uint64_t optical_accepted() const { return optical_accepted_; }
    uint64_t optical_rejected() const { return optical_rejected_; }
    // Vensters waarin de bol stil stond (voor de IMU-bevestiging).
    uint64_t optical_still_windows() const { return optical_still_windows_; }
    void set_optical_enabled(bool on) { optical_enabled_ = on; }
    Vec3 bias() const { return bias_; }
    BiasState state() const;
    BiasSource source() const { return source_; }
    bool still() const { return still_; }
    double still_time_total() const { return still_total_; }
    // Totale duur (s) van de quasi-stille vensters die de bias mochten sturen.
    double quasi_time_total() const { return quasi_total_; }
    // Bewaarde bias als beginwaarde (alleen als er nog niets gemeten is). tc: ruwe temperatuur, -1 onbekend.
    void restore(Vec3 bias, int tc = -1);
    // Ruwe temperatuur van het laatste rapport (-1 onbekend): bewaard bestand en temperatuurcompensatie.
    void set_temperature(int tc) { tc_ = tc; }
    // Geleerde helling per as (rad/s per telling), na verkleining; (0,0,0) zolang er te weinig bewijs is.
    Vec3 temp_slope() const { return slope_; }
    int temperature() const { return tc_; }
    int restored_temperature() const { return restored_tc_; }
    // De pijplijn zet dit uit zolang de magnetometer de yaw corrigeert (die leert zelf een verticale bias;
    // niet dubbel corrigeren). Stilstand blijft gewoon meetellen.
    void set_quasi_enabled(bool on) { quasi_enabled_ = on; }
    // Aantal windows dat geaccepteerd/afgewezen is (diagnose).
    uint64_t quasi_accepted() const { return quasi_accepted_; }
    uint64_t quasi_rejected() const { return quasi_rejected_; }
    // Is de bias betrouwbaar genoeg om te bewaren (eigen meting, geen kale herstelde waarde)?
    bool measured() const { return still_total_ >= cfg_.settled_after_s || quasi_acquired_ || optical_acquired_; }
    void reset();
    const BiasConfig& config() const { return cfg_; }

private:
    struct S { double age; Vec3 g; double an; };
    struct QWin { double t; Vec3 mean; double w; };
    struct TEv { double t; int tc; Vec3 mean; double w; };
    bool settled() const { return still_total_ >= cfg_.settled_after_s || quasi_acquired_ || optical_acquired_; }
    void quasi_sample(Vec3 gyro, Vec3 accel, double dt);
    void quasi_window_done();
    void quasi_track();
    void temp_add(Vec3 mean, double w);
    void temp_fit();
    bool quasi_acquire();
    void optical_try_eval();
    void optical_window_done(Vec3 mean);
    struct OFrame { double t, x, y, r; };
    struct OImu { double t; Vec3 g, a; };
    struct OWin { double t0, t1; };
    BiasConfig cfg_;
    std::deque<S> win_;
    double win_span_ = 0;
    Vec3 bias_{};
    bool still_ = false;
    double still_total_ = 0;
    double age_ = 0;
    BiasSource source_ = BiasSource::None;
    int tc_ = -1, restored_tc_ = -1;
    bool restored_ = false;
    // Herstel (reacquire)
    double reacq_t_ = 0;
    Vec3 reacq_mean_{};
    // Quasi
    bool quasi_enabled_ = true;
    bool quasi_acquired_ = false;
    double quasi_total_ = 0;
    uint64_t quasi_accepted_ = 0, quasi_rejected_ = 0;
    std::deque<QWin> qwin_;
    int q_n_ = 0;
    double q_t_ = 0;
    Vec3 q_sum_{}, q_sq_{}, q_asum_{};
    double q_adev_ = 0;
    double q_last_update_t_ = 0;
    // Optisch
    bool optical_enabled_ = true;
    bool optical_acquired_ = false;
    double optical_total_ = 0, optical_last_update_t_ = 0;
    uint64_t optical_accepted_ = 0, optical_rejected_ = 0, optical_still_windows_ = 0;
    std::deque<OFrame> oframes_;
    std::deque<OImu> oimu_;
    std::deque<OWin> opending_;
    std::deque<QWin> owin_;
    double oimu_latest_ = -1;
    // Temperatuur
    std::deque<TEv> tev_;
    Vec3 slope_{};
    int prev_tc_ = -1;
    Vec3 shift_since_anchor_{};   // door het temperatuurmodel verschoven sinds de laatste meting
    double ev_acc_t_ = 0;     // stilstandstijd sinds het laatste bewijspunt
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
