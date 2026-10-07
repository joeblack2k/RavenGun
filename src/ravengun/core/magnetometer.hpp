#pragma once
// Magnetometer van de ZCM1 (fase 3): ellipsoïdefit (hard en soft iron) met kwaliteitsmaten,
// dekking van de bol, opslag per controller en verstoringsdetectie. Geen hardwarecode.
// Ontwerp: docs/aim-fusion-design.md, secties 2.3 en 4.2.
//
// Stelsels: de fit gebeurt in het ruwe sensorstelsel van de magnetometer. m_cal = A (m_raw - b)
// is daarna ongeveer een eenheidsvector. Naar het controllerstelsel B (dat van accelerometer en
// gyro) gaat het met diag(-1, +1, -1): een rotatie van 180 graden om Y (fase 4, gemeten op de ZCM1,
// zie docs/aim-fusion-design.md "Metingen fase 4"):
//  - de gyro-consistentie d m/dt = -ω × m klopt alleen met een omgedraaide Y ten opzichte van X en Z
//    (wat PSMoveAPI ook doet: y_flip); dat legt de afbeelding vast op een globaal teken na;
//  - het globale teken volgt uit de dip: met alleen y_flip wijst het veld in Nederland OMHOOG
//    (dip −60°), met diag(-1,+1,-1) omlaag (+60°), zoals het aardveld. diag(-1,+1,-1) is bovendien
//    een echte rotatie (det +1), y_flip alleen een spiegeling.
// Oude kalibratiebestanden (zonder "b_map") gebruikten alleen y_flip; die worden bij het laden
// omgezet (teken van de vector en van dip_ref omgedraaid), zodat ze geldig blijven.
// Dip (inclinatie) = asin(m̂ · (-â)) in graden: positief als het veld naar beneden wijst
// (Nederland: ongeveer 67°). â is de accelerometerrichting ("omhoog" in rust).
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "ravengun/core/math.hpp"

namespace ravengun::core {

struct Mat3 {
    double m[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};
Vec3 mul(const Mat3& a, Vec3 v);
Mat3 mul(const Mat3& a, const Mat3& b);
// Eigenwaarden/-vectoren van een symmetrische 3x3 (Jacobi). Kolommen van vecs zijn de vectoren.
void eigen_sym3(const Mat3& a, double vals[3], Mat3& vecs);

inline constexpr int kMagCalibrationVersion = 1;

enum class MagFitKind { None, Full, AxisAligned };
const char* to_string(MagFitKind k);

struct MagCalibration {
    int version = kMagCalibrationVersion;
    MagFitKind kind = MagFitKind::None;
    Vec3 offset{};            // hard iron b (ruwe counts)
    Mat3 soft{};              // A, symmetrisch: m_cal = A (m_raw - b)
    bool y_flip = true;       // ruw -> B: Y omdraaien
    bool negate = true;       // ruw -> B: daarna de hele vector omdraaien (samen: diag(-1,+1,-1))
    double field_counts = 0;  // veldsterkte in ruwe counts (geometrisch gemiddelde straal)
    double dip_ref_deg = 0;   // gemiddelde dip tijdens de kalibratie
    double rms_norm_err = 0;  // RMS van |m_cal| - 1
    double dip_std_deg = 0;
    double coverage = 0;      // 0..1
    int samples = 0;
    int temperature_raw = -1;
    int64_t created_unix = 0;
    std::string controller;      // geredigeerd
    std::string controller_key;  // hash van het adres (bestandsnaam)
    std::string holder;          // bijv. "sharp_shooter" of "los"
    bool marginal = false;       // strenge eisen niet gehaald, ruime wel; pas na veldcheck bruikbaar
};

// Gekalibreerde vector in het controllerstelsel B.
Vec3 mag_apply(const MagCalibration& c, const int raw[3]);
Vec3 mag_apply(const MagCalibration& c, Vec3 raw);
// Dip in graden; NaN als a of m nul is.
double dip_deg(Vec3 m_b, Vec3 accel);

// Dekking van de bol: 20 richtingen (hoekpunten van een dodecaëder = middens van de vlakken van
// een icosaëder), elk monster telt bij de dichtstbijzijnde richting.
class SphereCoverage {
public:
    static constexpr int kBins = 20;
    void add(Vec3 dir);
    double fraction(int min_count = 3) const;
    const std::array<int, kBins>& counts() const { return n_; }
    void reset() { n_.fill(0); }
    static const std::array<Vec3, kBins>& directions();

private:
    std::array<int, kBins> n_{};
};

struct MagSample {
    Vec3 raw{};     // ruwe counts (sensorstelsel)
    Vec3 accel{};   // g, controllerstelsel
    double gyro_dps = -1;  // |ω| (graden/s) op dat moment; < 0 = onbekend
};

struct MagFitOptions {
    int min_samples = 300;
    double min_coverage = 0.80;
    double max_rms = 0.02;              // RMS van |m_cal| - 1
    double max_dip_std_deg = 1.5;
    // Ruime grenzen: de ruis per sample is ~2 % van het veld (3,8 counts op ~175), dus de strenge
    // eisen liggen op de ruisvloer. Daarbinnen wordt opgeslagen als "marginaal" (zie veldcheck).
    double marginal_max_rms = 0.04;
    double marginal_max_dip_std_deg = 8.0;
    double full_fit_min_coverage = 0.5; // daaronder: alleen asgericht (minder vrijheidsgraden)
    double ridge = 1e-6;                // regularisatie van de kruistermen (genormaliseerde data)
    double accel_static_tol = 0.05;     // g: alleen quasi-statische samples voor de dip
    int coverage_min_count = 3;
    // Dip alleen uit langzame samples: tijdens draaien vervalst een versnelling loodrecht op g de
    // richting van â zonder |a| veel te veranderen (0,3 g dwars: |a| +4 %, richting 17°).
    double dip_max_gyro_dps = 20.0;
    bool y_flip = true;
    bool negate = true;
};

struct MagFitResult {
    bool fitted = false;        // er is een kalibratie (kan wel slecht zijn)
    bool ok = false;            // alle kwaliteitseisen gehaald
    MagCalibration cal;
    double dip_mean_deg = 0;
    double dip_std_noflip_deg = 0;  // dezelfde spreiding zonder Y-flip (assencontrole)
    int dip_samples = 0;
    double max_norm_err = 0;
    bool marginal = false;      // !ok, maar binnen de ruime grenzen (opslaan met markering)
    std::vector<std::string> problems;  // Nederlands, concreet
};

MagFitResult fit_magnetometer(const std::vector<MagSample>& s, const MagFitOptions& o = {});

// Verzamelt samples tijdens het ronddraaien (live, in de HID-thread). Dunt uit: een sample wordt
// alleen bewaard als hij genoeg van de vorige afwijkt, zodat stilhouden de fit niet scheeftrekt.
// Dekking per richting plus de laatste meetrichting, voor de kalibratie-UI op de tv.
struct MagCoverageDetail {
    std::array<int, SphereCoverage::kBins> counts{};  // samples per richting (zelfde indeling als SphereCoverage::directions())
    Vec3 dir{};          // laatste sample, genormaliseerd op de eigen omvang (eenheidsvector); alleen geldig met has_dir
    bool has_dir = false;
    double spread = 0;   // 0..1: hoe groot de rondgang per as is t.o.v. de veldsterkte
    double coverage = 0; // zoals MagCollector::coverage()
};

class MagCollector {
public:
    void reset();
    // Geeft true als het sample bewaard is.
    bool add(const int raw[3], Vec3 accel, double t_s, double gyro_dps = -1);
    const std::vector<MagSample>& samples() const { return s_; }
    int seen() const { return seen_; }
    // Voortgang met een voorlopig middelpunt (midden van min/max per as).
    double coverage(int min_count = 3) const;
    MagCoverageDetail detail(int min_count = 3) const;
    double elapsed_s() const { return have_ ? last_t_ - t0_ : 0; }
    static constexpr size_t kMaxSamples = 20000;

private:
    std::vector<MagSample> s_;
    Vec3 last_{};
    Vec3 lo_{1e9, 1e9, 1e9}, hi_{-1e9, -1e9, -1e9};
    bool have_ = false;
    int seen_ = 0;
    double t0_ = 0, last_t_ = 0;
};

// Opslag: één JSON-bestand per controller, atomisch (tijdelijk bestand, fsync, rename), 0600.
std::string mag_calibration_to_json(const MagCalibration& c);
bool mag_calibration_from_json(const std::string& text, MagCalibration& out, std::string& err);
std::string default_mag_calibration_dir();          // $XDG_CONFIG_HOME/ravengun/magnetometer
std::string controller_key(const std::string& id);  // FNV-1a-64 (hex) van het kleine-letter-adres
bool save_mag_calibration(const std::string& dir, const MagCalibration& c, std::string& err);
// false met err ("ontbreekt", "beschadigd: ...", "andere versie") als er niets bruikbaars is.
bool load_mag_calibration(const std::string& dir, const std::string& key, MagCalibration& out, std::string& err);
// Algemeen: atomisch schrijven met rechten 0600 (map 0700 wordt gemaakt).
bool atomic_write_file(const std::string& path, const std::string& content, std::string& err);

// Verstoringsdetectie per sample (ontwerp 2.3 punt 5). De NIS-poort volgt pas met de ESKF (fase 4).
struct MagDisturbanceConfig {
    double norm_tol = 0.05;           // |m_cal| mag 5 % van 1,0 afwijken
    // Norm en dip worden getoetst na een kort exponentieel gemiddelde (fase 4): per sample is de ruis
    // ~2 % (norm) en ~1,3-1,6° (dip, gemeten in rust), zodat een poort van 2° per los sample ~10-20 %
    // vals afwees en het herstel (2 s aaneengesloten) vrijwel nooit lukte.
    double smooth_tau_s = 0.15;
    double dip_tol_deg = 2.0;         // dip t.o.v. de referentie
    // Magnetometer-yaw t.o.v. de gyro: helling (kleinste kwadraten) van (ψ_mag + ∫ω·up) over
    // rate_window_s. Fase 4: 1,5 s in plaats van de eindpunten over 0,5 s; met ~2,5° ruis per sample
    // gaf dat een ruis van ~7°/s en werd in rust ~80 % van de samples afgewezen (gemeten).
    double rate_tol_deg_s = 2.0;
    double rate_window_s = 1.5;
    double dip_max_gyro_dps = 30.0;   // dip-test alleen bij langzame rotatie (zie MagFitOptions)
    double disturb_after_s = 1.0;     // aaneengesloten afwijzing -> "verstoord"
    double recover_after_s = 2.0;     // aaneengesloten consistent -> weer "ok"
    double accel_static_tol = 0.10;   // g; daarbuiten geen dip-test (versnelling vervalst â)
};

enum MagReject : unsigned {
    kRejectNorm = 1u,
    kRejectDip = 2u,
    kRejectRate = 4u,
    kRejectRumble = 8u,
};
std::string reject_names(unsigned mask);

enum class MagState { Unknown, Ok, Disturbed };
const char* to_string(MagState s);

struct MagCheck {
    bool accepted = false;
    unsigned reasons = 0;
    double norm = 0;              // |m_cal|
    double dip_deg = 0;           // NaN als niet bepaald
    double rate_err_deg_s = 0;    // NaN als niet bepaald
    MagState state = MagState::Unknown;
};

class MagDisturbanceDetector {
public:
    explicit MagDisturbanceDetector(MagDisturbanceConfig c = {}) : cfg_(c) {}
    void set_reference_dip(double dip_deg) { dip_ref_ = dip_deg; }
    // m_b: gekalibreerd, controllerstelsel. gyro: rad/s (controllerstelsel). dt: s sinds vorige.
    MagCheck update(Vec3 m_b, Vec3 accel, Vec3 gyro, double dt, bool rumble = false);
    void reset();
    MagState state() const { return state_; }
    uint64_t accepted() const { return acc_; }
    uint64_t rejected(unsigned reason_bit) const;
    uint64_t total() const { return total_; }
    uint64_t disturbed_episodes() const { return episodes_; }

private:
    struct W { double t, e; };
    MagDisturbanceConfig cfg_;
    double dip_ref_ = 0;
    double norm_f_ = 0, dip_f_ = 0;
    bool norm_init_ = false, dip_init_ = false;
    double t_ = 0, yaw_g_ = 0, psi_prev_ = 0, psi_unwrapped_ = 0;
    bool have_psi_ = false;
    std::deque<W> win_;
    double bad_run_ = 0, good_run_ = 0;
    MagState state_ = MagState::Unknown;
    uint64_t acc_ = 0, total_ = 0, episodes_ = 0;
    uint64_t rej_[4]{};
};

}  // namespace ravengun::core
