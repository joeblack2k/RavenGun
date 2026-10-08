#pragma once
// Driftmeting uit een opname van echt spelen. Elke down-flank van de PS-knop is een meetpunt: de speler richt
// op het schermmidden en drukt PS. De opname wordt door de pijplijn afgespeeld (zonder recenter); het verschil
// in yaw en pitch tussen twee opeenvolgende PS-drukken is de in dat interval opgelopen drift. Per interval
// worden mogelijke oorzaken meegemeten (gedraaide hoek, maximale draaisnelheid, verloren rapporten,
// gyro-verzadiging, temperatuur), zodat er correlaties berekend kunnen worden.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/rec/recording.hpp"

namespace ravengun::rec {

struct DriftOptions {
    core::FusionMode mode = core::FusionMode::Mahony;
    bool have_cal = false;
    core::MagCalibration cal{};
    core::BiasConfig bias{};         // mode legacy|new en parameters van de biasschatter
    core::VqfConfig vqf{};           // parameters van VQF (modes vqf en vqf_mag)
    bool have_bias_init = false;     // beginbias (rad/s) zoals uit het bewaarde bestand; anders koude start
    core::Vec3 bias_init{};
    int bias_init_tc = -1;
    double min_interval_s = 2.0;     // kortere intervallen worden overgeslagen
    uint32_t marker_mask = 0;        // knopbits die als meetpunt tellen; 0 = alleen PS (zie drift.cpp)
    // Yaw-trim: de markerdrukken zijn de centreerdrukken voor de lerende trim (trim.enabled = leren aan). have_trim_init:
    // beginwaarde in graden/s (anders 0).
    core::YawTrimConfig trim = [] { core::YawTrimConfig c; c.enabled = false; return c; }();   // de analysetool zet leren standaard aan
    bool have_trim_init = false;
    double trim_init_dps = 0;
    int saturation_raw = 32000;      // |ruwe gyrowaarde| >= dit telt als "nabij de int16-grens" (max 32767)
};

struct DriftInterval {
    double t0_s = 0, t1_s = 0, duration_s = 0;   // t.o.v. het begin van de opname (ontvangsttijd)
    double yaw_deg = 0, pitch_deg = 0;           // yaw(nu) - yaw(vorige PS-druk), gewikkeld; idem pitch
    double yaw_per_min = 0, pitch_per_min = 0;
    double rotated_deg = 0;                      // totaal gedraaide hoek (som van de hoeken tussen opeenvolgende oriëntaties)
    double max_rate_dps = 0;                     // grootste |gyro| (na biascorrectie) in een rapport
    uint64_t reports = 0, lost = 0;              // ontvangen en verloren rapporten (seq-gaten)
    double lost_pct = 0;
    double longest_gap_ms = 0;                   // langste tijd tussen twee opeenvolgende rapporten
    int64_t saturated = -1;                      // subsamples met een as nabij de int16-grens; -1 = geen ruwe waarden in de opname
    int temp_begin = -1, temp_end = -1;          // ruwe temperatuur ("tc"); -1 = onbekend
    double trim_dps = 0;                         // yaw-trim tijdens dit interval (graden/s, om de wereld-verticaal)
};

struct DriftSummary {
    size_t intervals = 0;
    double median_abs_per_min = NAN;   // mediaan van |drift|/min, met |drift| = hoek tussen (yaw, pitch)-verschil als vector
    double p90_abs_per_min = NAN;
    double median_yaw_abs_per_min = NAN, p90_yaw_abs_per_min = NAN;
    // Cumulatieve yaw-drift van de eerste tot de laatste bruikbare PS-druk (som van de gewikkelde verschillen tussen
    // opeenvolgende drukken, ook de te korte). Het richtinstrument van de speler (ongeveer 1-2 graden per druk) telt
    // dan maar twee keer mee in plaats van bij elk interval; NaN bij minder dan twee drukken.
    double total_yaw_deg = NAN, total_s = NAN, total_yaw_per_min = NAN;
    // Pearson-correlatie van |yaw-drift| (graden) met: gedraaide hoek, max snelheid, verloren rapporten, duur. NaN als < 3 intervallen of geen spreiding.
    double r_rotated = NAN, r_max_rate = NAN, r_lost = NAN, r_duration = NAN;
};

struct DriftResult {
    bool ok = false;
    core::Vec3 final_bias{};           // gyro-bias aan het eind van de opname (rad/s), te gebruiken als warme start
    int final_tc = -1;
    double final_trim_dps = 0;         // yaw-trim aan het eind (graden/s)
    uint32_t trim_learned = 0;         // drukken waarmee de trim is bijgewerkt
    uint64_t optical_windows = 0;      // optisch-stille vensters die de bias stuurden
    uint64_t optical_still = 0, optical_rejected = 0;   // vensters met stilstaande bol; daarvan door de IMU/poort afgewezen
    double optical_s = 0;
    core::BiasState final_state = core::BiasState::None;
    core::BiasSource final_source = core::BiasSource::None;
    // VQF aan het eind (zie ReplayResult): hoe vaak de magnetometer als verstoord werd afgewezen, rustdetectie.
    uint64_t vqf_reports = 0, vqf_mag_samples = 0, vqf_mag_disturbed = 0, vqf_rest_samples = 0, vqf_filled = 0;
    core::Vec3 vqf_bias{};
    double vqf_ref_norm = 0, vqf_ref_dip_deg = 0;
    std::string error;
    size_t presses = 0;                // aantal PS-down-flanken
    size_t skipped_short = 0;          // intervallen korter dan min_interval_s
    std::vector<DriftInterval> intervals;
    DriftSummary summary;
};

// De drift per PS-interval; geen I/O.
DriftResult compute_drift(const std::vector<Record>& recs, const DriftOptions& opt);

// Hulp (ook getest): Pearson-correlatie; NaN bij < 3 punten of nulvariantie. Percentiel (0..1) met dezelfde afronding als Stat.
double pearson(const std::vector<double>& x, const std::vector<double>& y);
double percentile(std::vector<double> v, double p);

}  // namespace ravengun::rec
