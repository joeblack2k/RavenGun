#pragma once
// Latentie- en gladheidsmeting op een opname (`ravengun-analyze latency`).
//
// Idee: een opname wordt met een bepaalde instelling (voorspelhorizon, 1€-filter, uitvoerklok) door de echte
// Pipeline afgespeeld. De "waarheid" is de ongefilterde, niet-voorspelde aim (x_raw/y_raw) van dezelfde
// afspeelronde, op sensortijd (Move-stempel, ontrold). De uitgevoerde aim op tijd t wordt vergeleken met die
// waarheid op tijd t + L (de toekomst uit de opname):
//  - effectieve vertraging: de verschuiving L* waarbij de uitvoer het best op de waarheid past, tijdens beweging
//    (vertraging = -L*; negatief = de uitvoer loopt voor);
//  - RMS-fout bij een aangenomen totale latentie Lambda (transport + game + tv): uitvoer(t) tegen waarheid(t+Lambda);
//  - jitter in stilstand: RMS van de hoogfrequente component van de uitvoer (min een lopend gemiddelde van ±125 ms);
//  - overshoot bij abrupte stops: hoe ver de uitvoer voorbij het rustpunt van de waarheid komt (px, langs de
//    bewegingsrichting), per stop, en het surplus boven wat de hand zelf voorbij het rustpunt gaat; p95 over alle stops;
//  - sprong na een gat (verloren rapporten) met en zonder extrapolatie van de uitvoerklok.
// Alles in schermpixels (1920 x 1080 voor aim 0..1). Wat hier niet in zit: de Bluetooth-, game- en tv-latentie, en
// de afwijking van de schatter zelf t.o.v. de echte richting (zie docs/aim-fusion-design.md, fase 6).
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/output/aim_clock.hpp"
#include "ravengun/rec/replay.hpp"

namespace ravengun::rec {

inline constexpr double kScreenW = 1920.0, kScreenH = 1080.0;
inline constexpr int kShiftMinMs = -120, kShiftMaxMs = 120;  // zoekbereik van de verschuiving
inline constexpr int kBands = 4;  // 0 = alle beweging, 1 = traag, 2 = middel, 3 = snel (px/s van de waarheid)

struct LatencyParams {
    double assume_ms = 50;          // aangenomen totale latentie (transport + game + tv) voor de RMS-fout
    double motion_px_s = 300;       // vanaf deze waarheidssnelheid telt een rapport als beweging
    double band_mid_px_s = 1000;    // grens traag/middel
    double band_fast_px_s = 3000;   // grens middel/snel
    double still_px_s = 100;        // onder deze snelheid telt het als stilstand ...
    double still_min_s = 0.6;       // ... als dat minstens zo lang duurt
    double hold_px_s = 250;         // 'rustig houden' (kleine bewegingen, ~5 graden/s): tweede jitterkengetal
    double stop_from_px_s = 700;    // een abrupte stop: ~70 ms eerder was de snelheid minstens dit (~15 graden/s) ...
    double stop_to_px_s = 300;      // ... en zakt onder dit (~6 graden/s) ...
    double stop_rest_s = 0.2;       // ... en blijft daaronder (rustpunt = gemiddelde van de waarheid in die tijd)
    double stop_window_s = 0.4;     // overshoot gemeten tot zo lang na de stop
    double gap_factor = 1.5;        // een interval > factor * nominale rapportduur telt als gat (verloren rapport)
    output::OutputClockConfig clock{};  // voor de sprong-met-extrapolatie
};

// Eén afspeelronde. Alleen rapporten met een geldige aim.
struct Track {
    std::vector<double> t;        // sensortijd (s)
    std::vector<int64_t> rx;      // ontvangsttijd (ns)
    std::vector<double> tx, ty;   // waarheid (px)
    std::vector<double> ox, oy;   // uitvoer (px)
    std::vector<double> vx, vy;   // snelheid van de uitvoer voor extrapolatie (px/s)
    std::vector<char> gap;        // 1 = interval vóór dit rapport was een gat (volgens de Move-stempel)
    std::vector<double> age;      // extra transportvertraging boven het minimum (ms), zie InputSample::age_ms
    // Transport (alleen uit de ontvangst; zie docs).
    uint64_t reports = 0;         // alle rapporten in de opname
    double duration_s = 0;
    double nominal_s = 0.01127;
};

// Spelt `recs` af met `o` en verzamelt de track (waarheid en uitvoer uit dezelfde ronde: de voorspelling en het
// filter veranderen de oriëntatie niet, dus de waarheid is voor elke instelling gelijk).
Track collect_track(const std::vector<Record>& recs, const ReplayOptions& o);

// Alles wat alleen van de waarheid afhangt (masker beweging/stilstand, stops).
struct StopEvent { size_t k = 0; double dirx = 0, diry = 0, restx = 0, resty = 0; };
struct TruthInfo {
    std::vector<double> speed;    // px/s
    std::vector<char> band;       // 0 = geen beweging, 1..3 band
    std::vector<char> still;      // onderdeel van een stilstandssegment
    std::vector<char> hold;       // onderdeel van een rustig-houden-segment (ruimer dan stilstand)
    std::vector<StopEvent> stops;
    size_t n_motion = 0, n_still = 0, n_hold = 0;
};
TruthInfo analyze_truth(const Track& base, const LatencyParams& p);

// Cumuleerbare meetresultaten (opnames samenvoegen = optellen).
struct LatencyAccum {
    double cost_sum[kBands][kShiftMaxMs - kShiftMinMs + 1]{};
    uint64_t cost_n[kBands][kShiftMaxMs - kShiftMinMs + 1]{};
    double jit_out_sq = 0, jit_truth_sq = 0;
    uint64_t jit_n = 0;
    double hold_out_sq = 0, hold_truth_sq = 0;   // idem voor rustig houden
    uint64_t hold_n = 0;
    // px per stop: uitvoer, waarheid (de hand zelf) en het surplus van de uitvoer boven de waarheid (>= 0).
    std::vector<double> overshoot_out, overshoot_truth, overshoot_excess;
    // Sprong bij aankomst van een rapport na een gat (px): zonder en met extrapolatie; gat + beweging apart.
    std::vector<double> jump_gap_plain, jump_gap_extrap, jump_gap_move_plain, jump_gap_move_extrap;
    std::vector<double> jump_all_plain, jump_all_extrap;  // alle intervallen
    // Gemiddelde leeftijd van de getoonde waarde (houdtijd): som van dt^2/2 en som van dt.
    double hold_sq = 0, hold_total = 0;     // per rapport
    double hold_sq_clk = 0;                 // met klok: alleen het deel van elk interval voorbij de extrapolatielimiet
    size_t samples = 0;
    void merge(const LatencyAccum& o);
};

LatencyAccum evaluate(const Track& cfg, const Track& base, const TruthInfo& ti, const LatencyParams& p);

struct DelayResult { double delay_ms = 0; double rmse_at_assume_px = 0; double rmse_at_best_px = 0; uint64_t n = 0; bool ok = false; };
// Vertraging (min de beste verschuiving) en RMS-fout bij de aangenomen latentie, voor een band.
// Tijdsdomein (ontvangsttijd) in plaats van sensortijd: de getoonde positie op een raster van 4 ms, voor drie uitvoermodi:
//  per rapport (houden), uitvoerklok met anker op de ontvangsttijd en uitvoerklok met anker op t_rx - age (jitter
//  weggeëxtrapoleerd). De waarheid wordt op "minimumtransport-tijd" gezet (t_rx - age): de verschuiving is dus de
//  vertraging t.o.v. een rapport dat via het snelste waargenomen transport kwam (het vaste deel is niet meetbaar).
struct ModeAccum {
    std::string name;
    LatencyAccum acc;   // alleen band 0 (de kostencurve)
};
std::vector<ModeAccum> evaluate_modes(const Track& cfg, const Track& base, const TruthInfo& ti, const LatencyParams& p);

DelayResult delay_of(const LatencyAccum& a, int band, double assume_ms);
double jitter_px(const LatencyAccum& a);
double truth_jitter_px(const LatencyAccum& a);
double hold_jitter_px(const LatencyAccum& a);
double hold_truth_jitter_px(const LatencyAccum& a);
double lat_percentile(std::vector<double> v, double p);  // NaN bij leeg

// Transport zoals uit de opname te zien is.
struct TransportStats {
    uint64_t reports = 0, with_stamp = 0, lost_est = 0;   // lost_est: Move-ticks / nominaal - ontvangen
    double duration_s = 0, mean_interval_ms = 0, nominal_ms = 0;
    double rx_interval_p50_ms = 0, rx_interval_p95_ms = 0, rx_interval_max_ms = 0;
    double stamp_interval_p50_ms = 0;
    double residual_p50_ms = 0, residual_p95_ms = 0, residual_p99_ms = 0, residual_max_ms = 0;
    double gap_fraction = 0;               // aandeel intervallen > 1,5 x nominaal
    double max_gap_ms = 0;
    double hold_age_ms = 0;                // gemiddelde leeftijd van de getoonde waarde bij uitvoer per rapport
};
TransportStats transport_stats(const std::vector<Record>& recs);

}  // namespace ravengun::rec
