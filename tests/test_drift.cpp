// Tests voor `ravengun-analyze drift`: driftmeting per PS-interval op een synthetische opname met een
// bekende gyro-bias, een kort interval (wordt overgeslagen), verloren rapporten en gyro-verzadiging.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/rec/drift.hpp"

using namespace ravengun;
using namespace ravengun::core;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        ++g_checks;                                                                                    \
        double _a = (a), _b = (b);                                                                     \
        if (!(std::fabs(_a - _b) <= (tol))) {                                                          \
            ++g_failures;                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s = %.9g, verwacht %.9g +- %g\n", __FILE__, __LINE__, #a, \
                         _a, _b, static_cast<double>(tol));                                            \
        }                                                                                              \
    } while (0)

static Quat truth(double t) {
    // Heen en weer zwaaien in yaw: +-20 graden, periode 2 s (piek 63 graden/s), pitch en roll nul.
    double yaw = 20.0 * std::sin(2 * 3.14159265358979323846 * t / 2.0);
    return from_axis_angle({0, 0, 1}, -yaw * kRadPerDeg);
}

static Vec3 body_rate(Quat a, Quat b, double dt) {
    Quat d = normalized(conj(a) * b);
    if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z};
    double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (s < 1e-15) return {};
    return Vec3{d.x / s, d.y / s, d.z / s} * (2 * std::atan2(s, d.w) / dt);
}

// Opname van 40 s. Constante gyro-bias bias_z_dps rond de body-Z (= wereld-omhoog). PS op 3, 13, 23, 24 en 34 s.
static std::vector<rec::Record> make_recording(double bias_z_dps) {
    std::vector<rec::Record> recs;
    rec::Record h;
    h.kind = rec::Kind::Header;
    h.header.t0_ns = 1'000'000'000;
    recs.push_back(h);
    const double period = 0.01127;
    const int n = static_cast<int>(40.0 / period);
    const double press_s[] = {3, 13, 23, 24, 34};
    std::vector<int> press_k;
    for (double ps : press_s) press_k.push_back(static_cast<int>(std::lround(ps / period)));
    for (int k = 1; k <= n; ++k) {
        if (k == 300 || k == 301) continue;  // 2 verloren rapporten in het eerste interval
        double t = k * period;
        rec::Record x;
        x.kind = rec::Kind::Report;
        RawReport& r = x.report;
        r.subsamples = 2;
        for (int i = 0; i < 2; ++i) {
            double ts = t - (1 - i) * period / 2;
            Quat q0 = truth(ts - period / 2), q1 = truth(ts);
            r.gyro[i] = body_rate(q0, q1, period / 2) + Vec3{0, 0, bias_z_dps * kRadPerDeg};
            r.accel[i] = rotate(conj(q1), Vec3{0, 0, 1});
        }
        r.hid_seq = k % 16;
        r.t_rx_ns = h.header.t0_ns + static_cast<int64_t>(std::llround(t * 1e9));
        r.has_stamp = true;
        r.stamp = static_cast<uint16_t>(static_cast<int64_t>(std::llround(t * kMoveTicksPerSecond)) & 0xFFFF);
        r.has_raw = true;
        for (int f = 0; f < 2; ++f) for (int a = 0; a < 3; ++a) r.gyro_raw[f][a] = 100;
        // Drie subsamples nabij de int16-grens in het tweede interval (13..23 s).
        if (k >= 1500 && k < 1503) r.gyro_raw[1][0] = -32767;
        r.temperature_raw = 2000 + k / 500;
        for (int pk : press_k)
            if (k >= pk && k < pk + 4) r.buttons |= kBtnPS;  // 4 rapporten ingedrukt
        recs.push_back(x);
    }
    return recs;
}

static void test_drift_known_bias() {
    const double bias = 1.2;  // graden/s rond omhoog -> yaw loopt weg met -1,2 graden/s
    auto recs = make_recording(bias);
    rec::DriftResult r = rec::compute_drift(recs, rec::DriftOptions{});
    CHECK(r.ok);
    CHECK(r.presses == 5);
    CHECK(r.skipped_short == 1);          // 23 -> 24 s
    CHECK(r.intervals.size() == 3);       // 3->13, 13->23, 24->34
    if (r.intervals.size() != 3) return;
    for (const auto& d : r.intervals) {
        CHECK_NEAR(d.duration_s, 10.0, 0.05);
        CHECK_NEAR(d.yaw_deg, -bias * d.duration_s, 0.8);       // verwachte drift
        CHECK_NEAR(d.yaw_per_min, -bias * 60, 5.0);
        CHECK(std::fabs(d.pitch_deg) < 1.0);                     // tilt wordt door de accelerometer vastgehouden
        CHECK(d.max_rate_dps > 55 && d.max_rate_dps < 75);       // zwaaipiek 63 graden/s
        CHECK(d.rotated_deg > 380 && d.rotated_deg < 440);       // 5 x 80 graden heen en weer
        CHECK(d.temp_begin >= 2000 && d.temp_end >= d.temp_begin);
    }
    const auto& a = r.intervals[0];
    CHECK(a.lost == 2 && std::fabs(a.lost_pct - 100.0 * 2 / (a.reports + 2)) < 1e-9);
    CHECK(a.longest_gap_ms > 33 && a.longest_gap_ms < 35);      // drie perioden
    CHECK(a.saturated == 0);
    CHECK(r.intervals[1].lost == 0 && r.intervals[1].saturated == 3);
    CHECK(r.intervals[1].longest_gap_ms > 11 && r.intervals[1].longest_gap_ms < 12);
    CHECK_NEAR(r.summary.median_yaw_abs_per_min, bias * 60, 5.0);
    CHECK(r.summary.p90_yaw_abs_per_min >= r.summary.median_yaw_abs_per_min);
}

static void test_drift_zero_bias_and_scaling() {
    auto r0 = rec::compute_drift(make_recording(0.0), rec::DriftOptions{});
    auto r2 = rec::compute_drift(make_recording(2.4), rec::DriftOptions{});
    CHECK(r0.ok && r2.ok && r0.intervals.size() == 3 && r2.intervals.size() == 3);
    if (r0.intervals.size() != 3 || r2.intervals.size() != 3) return;
    CHECK(std::fabs(r0.intervals[0].yaw_deg) < 1.0);              // zonder bias: alleen het meetpunt-verschil
    CHECK_NEAR(r2.intervals[1].yaw_deg - r0.intervals[1].yaw_deg, -24.0, 1.0);  // dubbele bias, dubbele drift
}

static void test_drift_edge_cases() {
    // Geen PS-drukken: geen intervallen, wel ok.
    auto recs = make_recording(0.5);
    for (auto& x : recs) x.report.buttons = 0;
    rec::DriftResult r = rec::compute_drift(recs, rec::DriftOptions{});
    CHECK(r.ok && r.presses == 0 && r.intervals.empty() && std::isnan(r.summary.median_abs_per_min));
    CHECK(!rec::compute_drift({}, rec::DriftOptions{}).ok);
    // Zonder ruwe waarden en temperatuur: onbekend (-1).
    recs = make_recording(0.5);
    for (auto& x : recs) { x.report.has_raw = false; x.report.temperature_raw = -1; }
    r = rec::compute_drift(recs, rec::DriftOptions{});
    CHECK(r.ok && r.intervals.size() == 3 && r.intervals[0].saturated == -1 && r.intervals[0].temp_begin == -1);
}

// Leren uit de markerdrukken: een constante extra rotatie van 0,5 graden/s rond omhoog (yaw loopt met -0,5 graden/s weg)
// wordt na een paar drukken deels weggetrokken; zonder leren verandert er niets; met de juiste begintrim is de drift weg.
static void test_drift_trim_learning() {
    auto recs = make_recording(0.5);
    rec::DriftOptions off;
    rec::DriftResult r0 = rec::compute_drift(recs, off);
    rec::DriftOptions on;
    on.trim.enabled = true;
    rec::DriftResult r1 = rec::compute_drift(recs, on);
    CHECK(r0.ok && r1.ok && r0.intervals.size() == 3 && r1.intervals.size() == 3);
    if (r0.intervals.size() != 3 || r1.intervals.size() != 3) return;
    CHECK(r0.final_trim_dps == 0 && r0.trim_learned == 0);
    CHECK(r1.trim_learned >= 2 && r1.final_trim_dps > 0.1 && r1.final_trim_dps <= on.trim.limit_dps);
    CHECK_NEAR(r1.intervals[0].trim_dps, 0, 1e-12);                       // eerste interval: nog niets geleerd
    CHECK(r1.intervals[1].trim_dps > 0);                                  // trim tijdens het volgende interval
    CHECK(std::fabs(r1.intervals[0].yaw_deg - r0.intervals[0].yaw_deg) < 0.2);   // het eerste interval is ongewijzigd
    CHECK(std::fabs(r1.intervals[2].yaw_deg) < 0.7 * std::fabs(r0.intervals[2].yaw_deg));   // later: duidelijk minder drift
    rec::DriftOptions init;
    init.have_trim_init = true;
    init.trim_init_dps = 0.5;
    rec::DriftResult r2 = rec::compute_drift(recs, init);
    CHECK(r2.ok && r2.intervals.size() == 3 && r2.final_trim_dps == 0.5 && r2.trim_learned == 0);
    for (const auto& d : r2.intervals) CHECK(std::fabs(d.yaw_deg) < 1.0);
    // De cumulatieve yaw (eerste -> laatste druk) wordt kleiner met de juiste begintrim.
    CHECK(std::fabs(r2.summary.total_yaw_deg) < 0.2 * std::fabs(r0.summary.total_yaw_deg));
}

static void test_pearson() {
    CHECK_NEAR(rec::pearson({1, 2, 3, 4}, {2, 4, 6, 8}), 1.0, 1e-12);
    CHECK_NEAR(rec::pearson({1, 2, 3, 4}, {8, 6, 4, 2}), -1.0, 1e-12);
    CHECK(std::isnan(rec::pearson({1, 2, 3}, {5, 5, 5})));
    CHECK(std::isnan(rec::pearson({1, 2}, {1, 2})));
    CHECK_NEAR(rec::percentile({5, 1, 3, 2, 4}, 0.5), 3, 1e-12);
    CHECK_NEAR(rec::percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0.9), 9, 1.0);
}

int main() {
    test_drift_known_bias();
    test_drift_zero_bias_and_scaling();
    test_drift_edge_cases();
    test_drift_trim_learning();
    test_pearson();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
