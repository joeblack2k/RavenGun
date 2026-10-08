#pragma once
// Oriëntatieschatter VQF (Laidig & Seel, MIT; gevendord onder third_party/vqf) als alternatief voor Mahony.
//
// Wat deze wikkel doet:
//  - VQF verwacht een vaste bemonsteringstijd en geeft geen raad met verloren Bluetooth-rapporten (13,5 %).
//    De wikkel voert VQF daarom met een vaste "subsampletijd" (halve nominale rapporttijd) en vult een gat
//    van k verloren rapporten op met 2k lineair geïnterpoleerde gyro-/accelerometersamples (en k
//    magnetometersamples) tussen het laatste subsample van het vorige rapport en het eerste van het nieuwe.
//  - De werkelijke tijd per rapport (Move-stempel, dt_int van de pijplijn) wordt exact bewaard door de
//    hoeksnelheid per sample te schalen met dt_sample / Ts. Zo is de geïntegreerde hoek exact, ook als de
//    stempel een paar promille van de nominale 11,27 ms afwijkt. De bias wordt buiten die schaling gehouden
//    (zie feed()), zodat een bias van 2 graden/s niet 1 % ernaast zit.
//  - Biasbron: "external" (standaard) trekt de bias van de GyroBiasEstimator van de pijplijn eerst af en zet
//    de VQF-biasschatting uit. "own" laat VQF de bias zelf schatten uit de ruwe gyro (rustdetectie; in beweging
//    alleen horizontaal, de verticale as vergeet VQF zelf naar nul) en negeert de onze. De twee worden niet
//    allebei toegepast (geen dubbele correctie); "hybrid" (beide) bestaat alleen voor analyse. Keuze: op de
//    Halo-opnames is "own" slechter in 6D (de bias loopt met de opwarming mee en rust is zeldzaam), gelijk in 9D;
//    "external" houdt één bias in het systeem (bewaarde bias, temperatuurcompensatie, camera-bijsturing).
//  - De uitvoer is een oriëntatie q_wb in dezelfde conventie als OrientationFilter (controller -> wereld,
//    Z omhoog), met een yaw-offset rond wereld-Z die bij het omschakelen van schatter de richting van de
//    richtas laat doorlopen (geen sprong in de aim). De absolute yaw van VQF is arbitrair (6D) of de
//    magnetische noord (9D); alleen verschillen tellen.
//  - De magnetometer is de gekalibreerde vector in het controllerstelsel B (mag_apply, asconventie van
//    core/magnetometer.hpp). Zonder kalibratie draait VQF 6D.
#include <cstdint>
#include <memory>

#include "ravengun/core/input_model.hpp"
#include "ravengun/core/math.hpp"

class VQF;  // third_party/vqf/vqf.hpp; bewust niet in deze header

namespace ravengun::core {

enum class VqfBias { Own, External, Hybrid };
const char* to_string(VqfBias b);
bool parse_vqf_bias(const char* s, VqfBias& out);

struct VqfConfig {
    VqfBias bias = VqfBias::External;   // zie de afweging hieronder en docs/decisions.md
    double tau_acc_s = 3.0;              // VQF-standaard
    double tau_mag_s = 9.0;              // VQF-standaard
    bool motion_bias = true;             // bias in beweging (horizontaal; alleen bij bias own/hybrid)
    bool rest_bias = true;               // bias in rust
    bool mag_dist_rejection = true;
    bool seed_mag_ref = true;            // beginreferentie (veldsterkte 1, dip) uit de magnetometerkalibratie, zie set_mag_reference()
    // De Move heeft een gyro-bias tot 2,4 graden/s (standaard VQF-clip: 2) en ruis van 0,75 graden/s per as
    // per sample (standaard rustdrempel: 2 graden/s op de normvector, dus rust werd nooit herkend).
    double bias_clip_dps = 4.0;
    double rest_th_gyr_dps = 4.0;
    double rest_th_acc = 0.5;            // m/s2 (VQF-standaard)
    double nominal_report_s = 0.01127;   // gemeten: 1127 ticks van 10 us per rapport
    double max_gap_s = 0.25;             // langere gaten worden niet geïntegreerd (zoals OrientationFilter)
    int max_fill_reports = 24;           // bovengrens voor de opvulling per rapport (0,27 s)
};

class VqfEstimator {
public:
    explicit VqfEstimator(VqfConfig cfg = {});
    ~VqfEstimator();
    VqfEstimator(const VqfEstimator& o);
    VqfEstimator& operator=(const VqfEstimator& o);

    // Eén rapport verwerken. dt_s: tijd sinds het vorige rapport (0 of negatief bij het eerste rapport:
    // dan geldt de nominale tijd). ext_bias: bias van de pijplijn (rad/s, alleen bij bias external/hybrid).
    // mag_b: gekalibreerde magnetometer in B of nullptr (dan geen magnetometer-update). use_mag bepaalt
    // alleen welke uitvoer q() geeft; VQF draait 9D zodra mag_b wordt aangeboden.
    void feed(const RawReport& r, double dt_s, Vec3 ext_bias, const Vec3* mag_b);
    // Gat dat niet wordt geïntegreerd (te lang) of herverbinding: de interpolatie begint opnieuw.
    void on_interrupt() { have_prev_ = false; }

    bool initialized() const { return fed_ > 0; }
    // 6D- of 9D-oriëntatie (met yaw-offset); 9D alleen als use_mag(true) is gezet én er magnetometerdata is.
    Quat q() const;
    Quat q6() const;
    Quat q9() const;
    // Beginreferentie voor de magnetometer (veldsterkte in eenheden van de gekalibreerde vector, ~1; dip in graden,
    // positief als het veld naar beneden wijst). Zonder dit accepteert VQF pas een referentie na 5 s draaien met
    // minstens 20 graden/s (in een speelsessie met weinig snelle beweging gebeurt dat niet en blijft de
    // magnetometer "verstoord"); de referentie past zichzelf daarna aan (tijdconstante 20 s).
    void set_mag_reference(double norm, double dip_deg);
    void set_use_mag(bool on) { use_mag_ = on; }
    bool use_mag() const { return use_mag_; }
    // Laat de richtas van q() dezelfde yaw hebben als `target` (yaw-offset rond wereld-Z). Pitch en roll
    // blijven die van VQF.
    void align_yaw_to(Quat target);

    // Diagnose.
    Vec3 bias() const;                   // VQF's eigen schatting (rad/s)
    bool rest_detected() const;
    bool mag_disturbed() const;
    double mag_ref_norm() const;
    double mag_ref_dip_deg() const;
    double heading_correction_deg() const;   // delta (9D-yaw t.o.v. 6D), graden
    uint64_t reports() const { return fed_; }
    uint64_t mag_samples() const { return mag_n_; }
    uint64_t mag_disturbed_samples() const { return mag_dist_n_; }
    uint64_t rest_samples() const { return rest_n_; }
    uint64_t filled_reports() const { return filled_; }
    uint64_t skipped_reports() const { return skipped_; }
    const VqfConfig& config() const { return cfg_; }
    void reset();

private:
    void make();
    void step(Vec3 gyro, Vec3 accel, double dt_sample, Vec3 ext_bias);
    void step_mag(Vec3 mag);
    VqfConfig cfg_;
    std::unique_ptr<VQF> vqf_;
    bool use_mag_ = false;
    bool have_prev_ = false, have_mag_ = false;
    bool have_ref_ = false;
    double ref_norm_ = 0, ref_dip_deg_ = 0;
    Vec3 prev_gyro_{}, prev_accel_{}, prev_mag_{};
    double yaw_off_ = 0;     // rad, rotatie van de uitvoer rond wereld-Z
    uint64_t fed_ = 0, mag_n_ = 0, mag_dist_n_ = 0, rest_n_ = 0, filled_ = 0, skipped_ = 0;
};

}  // namespace ravengun::core
