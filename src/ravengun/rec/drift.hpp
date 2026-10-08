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
    double min_interval_s = 2.0;     // kortere intervallen worden overgeslagen
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
};

struct DriftSummary {
    size_t intervals = 0;
    double median_abs_per_min = NAN;   // mediaan van |drift|/min, met |drift| = hoek tussen (yaw, pitch)-verschil als vector
    double p90_abs_per_min = NAN;
    double median_yaw_abs_per_min = NAN, p90_yaw_abs_per_min = NAN;
    // Pearson-correlatie van |yaw-drift| (graden) met: gedraaide hoek, max snelheid, verloren rapporten, duur. NaN als < 3 intervallen of geen spreiding.
    double r_rotated = NAN, r_max_rate = NAN, r_lost = NAN, r_duration = NAN;
};

struct DriftResult {
    bool ok = false;
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
