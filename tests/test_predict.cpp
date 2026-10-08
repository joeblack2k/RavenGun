// Tests voor de voorspelling van de oriëntatie (core/predict), het live instellen ervan in de pijplijn, de snelheid die de
// uitvoerklok gebruikt, de latentiemeting (rec/latency) en het deterministisch afspelen met voorspelling. Geen hardware.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ravengun/core/pipeline.hpp"
#include "ravengun/core/predict.hpp"
#include "ravengun/rec/latency.hpp"
#include "ravengun/rec/replay.hpp"

using namespace ravengun;
using namespace ravengun::core;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                            \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        ++g_checks;                                                                                    \
        double _a = (a), _b = (b);                                                                     \
        if (!(std::fabs(_a - _b) <= (tol))) {                                                          \
            ++g_failures;                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s = %.6g, verwacht %.6g +- %g\n", __FILE__, __LINE__, #a, \
                         _a, _b, static_cast<double>(tol));                                            \
        }                                                                                              \
    } while (0)

namespace {

struct Rng {
    uint64_t s = 88172645463325252ULL;
    double uniform() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double a = uniform() + 1e-12, b = uniform(); return std::sqrt(-2 * std::log(a)) * std::cos(2 * kPi * b); }
};

constexpr double kPeriod = 0.01127;

// Eén rapport met draaisnelheid w (rad/s, rond de verticale body-z-as = yaw) op tijd t_ns.
RawReport rep(int64_t t_ns, double w_z, double noise = 0, Rng* rng = nullptr, double w_x = 0) {
    RawReport r;
    r.t_rx_ns = t_ns;
    for (int k = 0; k < 2; ++k) {
        r.accel[k] = {0, 0, 1};
        r.gyro[k] = {w_x, 0, w_z};
        if (noise > 0 && rng) { r.gyro[k].x += noise * rng->gauss(); r.gyro[k].y += noise * rng->gauss(); r.gyro[k].z += noise * rng->gauss(); }
    }
    return r;
}

PipelineConfig base_cfg(double horizon_ms, bool filter) {
    PipelineConfig c;
    c.filter.enabled = filter;
    c.predict.horizon_ms = horizon_ms;
    c.auto_center_on_start = true;
    return c;
}

constexpr double kPxPerScreen = 1920.0;

}  // namespace

// Exact bij constante draaisnelheid: de voorspelling is w * T, omgezet naar het scherm; de oriëntatie zelf verandert niet.
static void test_constant_rate_exact() {
    const double w = 60 * kRadPerDeg;  // 60 graden/s
    const double T = 30;
    Pipeline a(base_cfg(T, false)), b(base_cfg(0, false));
    a.on_connect("00:00:00:00:00:41", 0);
    b.on_connect("00:00:00:00:00:41", 0);
    int64_t t = 0;
    double prev_raw = 0;
    InputSample sa, sb;
    for (int i = 0; i < 200; ++i) {
        t += static_cast<int64_t>(kPeriod * 1e9);
        sa = a.on_report(rep(t, w));
        sb = b.on_report(rep(t, w));
        CHECK(sa.x_raw == sb.x_raw && sa.y_raw == sb.y_raw);  // de voorspelling raakt de schatter niet
        CHECK(sb.x_pred == sb.x_raw);                          // uit: gelijk aan de ruwe aim
        if (i > 5) {
            const double expect = (w * T * 1e-3 * kDegPerRad) / (2 * 20.0);  // schermbreedtes (h_edge 20 graden)
            const double lead = sa.x_pred - sa.x_raw;
            CHECK_NEAR(std::fabs(lead), expect, 1e-4);
            CHECK((lead > 0) == (sa.x_raw - prev_raw > 0));      // in de bewegingsrichting
            CHECK_NEAR(sa.aim.x, sa.x_pred, 1e-12);              // filter uit: de aim is de voorspelde aim
            CHECK_NEAR(sa.pred_deg, 60 * T * 1e-3, 1e-6);
            CHECK_NEAR(std::fabs(sa.aim_vx), 60.0 / 40.0, 1e-3);  // schermen/s, voor de uitvoerklok
        }
        prev_raw = sa.x_raw;
    }
    // 'Exact' ook voor andere snelheden en horizons: lead = w * T (in graden), pitch-as (x-gyro) idem.
    for (double dps : {20.0, 45.0, 120.0}) {
        for (double h : {10.0, 40.0}) {
            Pipeline p(base_cfg(h, false));
            p.on_connect("00:00:00:00:00:42", 0);
            int64_t tt = 0;
            InputSample s;
            for (int i = 0; i < 120; ++i) { tt += static_cast<int64_t>(kPeriod * 1e9); s = p.on_report(rep(tt, dps * kRadPerDeg)); }
            CHECK_NEAR(std::fabs(s.x_pred - s.x_raw), dps * h * 1e-3 / 40.0, 1e-4);
        }
    }
    // Pitch: draaiing om de body-x-as geeft een verticale voorspelling.
    Pipeline pp(base_cfg(30, false));
    pp.on_connect("00:00:00:00:00:43", 0);
    int64_t tt = 0;
    InputSample sp;
    for (int i = 0; i < 100; ++i) { tt += static_cast<int64_t>(kPeriod * 1e9); sp = pp.on_report(rep(tt, 0, 0, nullptr, 50 * kRadPerDeg)); }
    CHECK_NEAR(std::fabs(sp.y_pred - sp.y_raw), 50 * 0.030 / 22.5, 2e-4);
    CHECK_NEAR(sp.x_pred, sp.x_raw, 1e-9);
}

// Verloren rapporten (gaten van 2-4 perioden) verstoren de voorspelling bij constante snelheid niet.
static void test_gaps_do_not_disturb() {
    const double w = 40 * kRadPerDeg;
    Pipeline p(base_cfg(40, false));
    p.on_connect("00:00:00:00:00:44", 0);
    Rng rng;
    int64_t t = 0;
    double max_dev = 0;
    for (int i = 0; i < 250; ++i) {  // < 180 graden in totaal (daarna wikkelt de yaw om)
        const double gap = (rng.uniform() < 0.3 ? (rng.uniform() < 0.5 ? 2 : 4) : 1) * kPeriod;
        t += static_cast<int64_t>(gap * 1e9);
        InputSample s = p.on_report(rep(t, w));
        if (i > 5) max_dev = std::max(max_dev, std::fabs(std::fabs(s.x_pred - s.x_raw) - 40 * 0.040 / 40.0));
    }
    CHECK(max_dev < 5e-4);
}

// Stilhouden wordt niet onrustiger: onder de drempel (handtrilling, ruis, langzaam wegzakken) is de voorspelling exact nul.
static void test_stillness_no_extra_jitter() {
    Rng rng;
    for (int variant = 0; variant < 3; ++variant) {
        Pipeline a(base_cfg(40, true)), b(base_cfg(0, true));
        a.on_connect("00:00:00:00:00:45", 0);
        b.on_connect("00:00:00:00:00:45", 0);
        int64_t t = 0;
        double sum2 = 0, max_diff = 0;
        int n = 0;
        Rng r1 = rng;
        for (int i = 0; i < 1500; ++i) {
            t += static_cast<int64_t>(kPeriod * 1e9);
            double w = 0;
            if (variant == 0) w = 0.02 * std::sin(2 * kPi * 9.0 * i * kPeriod);   // trilling 9 Hz, 1,1 graden/s
            if (variant == 1) w = 1.5 * kRadPerDeg;                                // langzaam wegzakken, 1,5 graden/s
            if (variant == 2) w = 0.02 * std::sin(2 * kPi * 11.0 * i * kPeriod) + 1.2 * kRadPerDeg * std::sin(2 * kPi * 0.5 * i * kPeriod);
            Rng ra = r1, rb = r1;
            RawReport ra_r = rep(t, w, 0.004, &ra), rb_r = rep(t, w, 0.004, &rb);
            r1.gauss(); r1.gauss(); r1.gauss(); r1.gauss(); r1.gauss(); r1.gauss();
            InputSample sa = a.on_report(ra_r), sb = b.on_report(rb_r);
            if (i > 200) {
                CHECK(sa.pred_deg == 0);
                const double d = (sa.aim.x - sb.aim.x) * kPxPerScreen;
                sum2 += d * d;
                max_diff = std::max(max_diff, std::fabs(d));
                ++n;
            }
        }
        CHECK(max_diff < 1e-9);               // geen enkele extra beweging
        CHECK(std::sqrt(sum2 / n) < 0.02);    // en dus ook geen extra jitter (px)
    }
    // Net boven de drempel met ruis: de extra ruis door de voorspelling blijft onder 1 px RMS (bij 40 ms).
    Pipeline a(base_cfg(40, false)), b(base_cfg(0, false));
    a.on_connect("00:00:00:00:00:46", 0);
    b.on_connect("00:00:00:00:00:46", 0);
    int64_t t = 0;
    double sum2 = 0;
    int n = 0;
    Rng rr;
    for (int i = 0; i < 1500; ++i) {
        t += static_cast<int64_t>(kPeriod * 1e9);
        RawReport r = rep(t, 10 * kRadPerDeg, 0.01, &rr);
        InputSample sa = a.on_report(r), sb = b.on_report(r);
        if (i > 200) { const double d = ((sa.x_pred - sa.x_raw) - (sb.x_pred - sb.x_raw)) * kPxPerScreen; sum2 += d * d; ++n; }
    }
    // De lead zelf is hier een constante van ~(gewicht * w * T); wat we meten is de ruis eromheen, na aftrek van het gemiddelde.
    (void)sum2; (void)n;
    double mean = 0, var = 0;
    {
        Pipeline c(base_cfg(40, false));
        c.on_connect("00:00:00:00:00:47", 0);
        std::vector<double> lead;
        int64_t tc = 0;
        Rng r3;
        for (int i = 0; i < 900; ++i) {  // < 180 graden
            tc += static_cast<int64_t>(kPeriod * 1e9);
            InputSample s = c.on_report(rep(tc, 10 * kRadPerDeg, 0.01, &r3));
            if (i > 200) lead.push_back((s.x_pred - s.x_raw) * kPxPerScreen);
        }
        for (double v : lead) mean += v;
        mean /= static_cast<double>(lead.size());
        for (double v : lead) var += (v - mean) * (v - mean);
        var /= static_cast<double>(lead.size());
    }
    CHECK(std::sqrt(var) < 1.5);  // zware ruis (0,57 graden/s per subsample); bij realistische ruis < 0,5 px
}

// Abrupt stoppen: geen terugdraaien, begrensd en snel weer netjes op het rustpunt.
static void test_abrupt_stop_no_swing() {
    for (double dps : {60.0, 150.0, 400.0}) {
        Pipeline p(base_cfg(40, false));
        p.on_connect("00:00:00:00:00:48", 0);
        int64_t t = 0;
        std::vector<double> xp, xr;
        for (int i = 0; i < 300; ++i) {
            t += static_cast<int64_t>(kPeriod * 1e9);
            const bool moving = i < 120;
            InputSample s = p.on_report(rep(t, moving ? dps * kRadPerDeg : 0.0));
            xp.push_back(s.x_pred);
            xr.push_back(s.x_raw);
            CHECK(s.pred_deg <= p.config().predict.max_angle_deg + 1e-9);
        }
        const double rest = xr.back();
        const double dir = xr[100] > xr[99] ? 1 : -1;
        double max_over = -1e9, min_under = 1e9;
        for (size_t i = 120; i < xp.size(); ++i) {
            const double o = (xp[i] - rest) * dir * kPxPerScreen;
            max_over = std::max(max_over, o);
            min_under = std::min(min_under, o);
        }
        const double unlimited_px = std::min(dps * 0.040, p.config().predict.max_angle_deg) / 40.0 * kPxPerScreen;
        CHECK(max_over <= unlimited_px * 0.9);     // het model remt af, dus minder dan w*T doorschieten
        CHECK(min_under > -1.0);                    // nooit terugdraaien voorbij het rustpunt
        CHECK_NEAR((xp.back() - rest) * kPxPerScreen, 0.0, 0.01);  // en na een korte tijd precies op het rustpunt
        // Binnen 150 ms na de stop is de voorspelling weer onder 1 px.
        const size_t k150 = 120 + static_cast<size_t>(0.150 / kPeriod);
        CHECK(std::fabs(xp[k150] - rest) * kPxPerScreen < std::max(1.0, unlimited_px * 0.3));
    }
    // Begrenzing: een uitschieter in de gyro geeft hooguit max_angle_deg.
    Pipeline p(base_cfg(60, false));
    p.on_connect("00:00:00:00:00:49", 0);
    int64_t t = 0;
    InputSample s;
    for (int i = 0; i < 50; ++i) { t += static_cast<int64_t>(kPeriod * 1e9); s = p.on_report(rep(t, 1000 * kRadPerDeg)); }
    CHECK(s.pred_deg > 7.9 && s.pred_deg <= 8.0001);
}

// De horizon is live instelbaar: geen reset van het filter of de schatters, gewoon direct andere uitvoer.
static void test_live_horizon() {
    Pipeline p(base_cfg(0, true));
    p.on_connect("00:00:00:00:00:4a", 0);
    int64_t t = 0;
    InputSample s;
    for (int i = 0; i < 150; ++i) { t += static_cast<int64_t>(kPeriod * 1e9); s = p.on_report(rep(t, 50 * kRadPerDeg)); }
    CHECK(s.pred_deg == 0 && s.x_pred == s.x_raw);
    const double lag_before = s.x_raw - s.aim.x;  // het 1€-filter loopt achter
    CHECK(std::fabs(lag_before) > 1e-4);
    PredictConfig pc = p.config().predict;
    pc.horizon_ms = 40;
    p.set_predict(pc);
    t += static_cast<int64_t>(kPeriod * 1e9);
    s = p.on_report(rep(t, 50 * kRadPerDeg));
    CHECK_NEAR(s.pred_deg, 50 * 0.040, 1e-6);
    // Geen filterreset: de uitvoer loopt nog steeds achter op de ruwe aim (bij een reset zou aim = x_pred zijn).
    CHECK(std::fabs(s.aim.x - s.x_pred) > 1e-4);
    // Terug naar 0: meteen weer gelijk aan de ruwe aim.
    pc.horizon_ms = 0;
    p.set_predict(pc);
    t += static_cast<int64_t>(kPeriod * 1e9);
    s = p.on_report(rep(t, 50 * kRadPerDeg));
    CHECK(s.pred_deg == 0 && s.x_pred == s.x_raw);
}

// De snelheid voor de uitvoerklok (aim_vx) komt uit de Move-tijd en is 0 na een reset of een te groot gat.
static void test_output_velocity() {
    Pipeline p(base_cfg(0, false));
    p.on_connect("00:00:00:00:00:4b", 0);
    int64_t t = 0;
    InputSample s;
    for (int i = 0; i < 20; ++i) { t += static_cast<int64_t>(kPeriod * 1e9); s = p.on_report(rep(t, 30 * kRadPerDeg)); }
    CHECK_NEAR(std::fabs(s.aim_vx), 30.0 / 40.0, 1e-3);
    CHECK_NEAR(s.aim_vy, 0.0, 1e-9);
    // Gat van 100 ms (boven de 60 ms): geen snelheid (en bij > max_dt wordt het filter gereset).
    t += 100'000'000;
    s = p.on_report(rep(t, 30 * kRadPerDeg));
    CHECK(s.aim_vx == 0 && s.aim_vy == 0);
    // Recenter: het filter begint opnieuw, maar de snelheid springt niet (de eerste uitvoer na de reset is het nieuwe midden).
    t += static_cast<int64_t>(kPeriod * 1e9);
    s = p.on_report(rep(t, 30 * kRadPerDeg));
    CHECK(p.recenter());
    CHECK(p.last().aim_vx == 0);
    t += static_cast<int64_t>(kPeriod * 1e9);
    s = p.on_report(rep(t, 30 * kRadPerDeg));
    CHECK_NEAR(std::fabs(s.aim_vx), 30.0 / 40.0, 1e-3);
}

// ---------------- Latentiemeting ----------------

static std::vector<rec::Record> synthetic_recording(bool lossy) {
    std::vector<rec::Record> recs;
    rec::Record h;
    h.kind = rec::Kind::Header;
    h.header.controller = "xx:xx:xx:xx:xx:99";
    h.header.t0_ns = 0;
    recs.push_back(h);
    // Beweging in stukken: 0,35 s met 90 graden/s (links/rechts afwisselend), dan 0,7 s rust: abrupte stops en stilstand.
    int64_t t = 0;
    uint16_t stamp = 100;
    Rng rng;
    for (int i = 0; i < 4200; ++i) {
        t += static_cast<int64_t>(kPeriod * 1e9);
        stamp = static_cast<uint16_t>(stamp + 1127);
        if (lossy && i % 7 == 3) continue;  // verloren rapport
        const double ts = i * kPeriod;
        const double phase = std::fmod(ts, 1.05);
        const int seg = static_cast<int>(ts / 1.05);
        double w = 0;
        if (phase < 0.35) w = (seg % 2 ? -1 : 1) * 90 * kRadPerDeg;
        rec::Record r;
        r.kind = rec::Kind::Report;
        r.report = rep(t, w, 0.003, &rng);
        r.report.has_stamp = true;
        r.report.stamp = stamp;
        recs.push_back(r);
    }
    return recs;
}

static void test_latency_measurement() {
    const std::vector<rec::Record> recs = synthetic_recording(false);
    rec::ReplayOptions o;
    o.pipeline.filter.enabled = false;
    rec::LatencyParams lp;
    rec::Track base = rec::collect_track(recs, o);
    CHECK(base.t.size() > 3500);
    rec::TruthInfo ti = rec::analyze_truth(base, lp);
    CHECK(ti.n_motion > 500 && ti.n_still > 500);
    CHECK(ti.stops.size() >= 6);
    // Zonder voorspelling en filter: uitvoer = waarheid -> vertraging 0, fout 0.
    rec::LatencyAccum a0 = rec::evaluate(base, base, ti, lp);
    rec::DelayResult d0 = rec::delay_of(a0, 0, 50);
    CHECK(d0.ok && std::fabs(d0.delay_ms) < 0.6);
    CHECK_NEAR(d0.rmse_at_best_px, 0.0, 1e-6);
    // Met voorspelling T: de uitvoer loopt T ms voor (negatieve vertraging), binnen enkele ms.
    for (double T : {20.0, 40.0}) {
        rec::ReplayOptions op = o;
        op.pipeline.predict.horizon_ms = T;
        rec::Track tr = rec::collect_track(recs, op);
        rec::LatencyAccum a = rec::evaluate(tr, base, ti, lp);
        rec::DelayResult d = rec::delay_of(a, 0, T);
        CHECK(d.ok && std::fabs(d.delay_ms + T) < 3.0);
        // En de fout bij een aangenomen latentie gelijk aan de horizon is veel kleiner dan zonder voorspelling.
        rec::DelayResult dn = rec::delay_of(a0, 0, T);
        CHECK(d.rmse_at_assume_px < 0.45 * dn.rmse_at_assume_px);  // de start van een beweging blijft onvoorspelbaar
        // Overshoot: met voorspelling > 0 (en nooit negatief), zonder precies 0.
        CHECK(!a.overshoot_excess.empty());
        for (double v : a.overshoot_excess) CHECK(v >= 0);
        CHECK(rec::lat_percentile(a.overshoot_excess, 0.95) > 0.5);
        CHECK(rec::lat_percentile(a0.overshoot_excess, 0.95) == 0);
        // Jitter in stilstand: de voorspelling voegt niets toe.
        CHECK_NEAR(rec::jitter_px(a), rec::jitter_px(a0), 0.02);
    }
    // 1€-filter: vertraging > 0 tijdens beweging, minder jitter in stilstand.
    rec::ReplayOptions of = o;
    of.pipeline.filter.enabled = true;
    of.pipeline.filter.min_cutoff_hz = 1.0;
    of.pipeline.filter.beta = 10.0;
    of.pipeline.filter.d_cutoff_hz = 1.0;
    rec::Track tf = rec::collect_track(recs, of);
    rec::LatencyAccum af = rec::evaluate(tf, base, ti, lp);
    CHECK(rec::delay_of(af, 0, 50).delay_ms > 3.0);
    CHECK(rec::jitter_px(af) < rec::jitter_px(a0));
}

static void test_latency_gap_jumps() {
    const std::vector<rec::Record> recs = synthetic_recording(true);
    rec::ReplayOptions o;
    o.pipeline.filter.enabled = false;
    rec::LatencyParams lp;
    rec::Track base = rec::collect_track(recs, o);
    rec::TruthInfo ti = rec::analyze_truth(base, lp);
    rec::LatencyAccum a = rec::evaluate(base, base, ti, lp);
    // Er zijn gaten (elk 7e rapport weg) en tijdens beweging is de sprong zonder extrapolatie groot.
    CHECK(a.jump_gap_plain.size() > 400);
    CHECK(a.jump_gap_move_plain.size() > 50);
    const double p95_plain = rec::lat_percentile(a.jump_gap_move_plain, 0.95);
    const double p95_ext = rec::lat_percentile(a.jump_gap_move_extrap, 0.95);
    CHECK(p95_plain > 40);                  // 90 graden/s * 22,5 ms = ~97 px
    CHECK(p95_ext < p95_plain);
    // De mediaan (constante snelheid binnen de beweging) wordt met extrapolatie praktisch nul; het p95 wordt bepaald door het
    // begin en het einde van een beweging, waar de snelheid niet te raden is.
    CHECK(rec::lat_percentile(a.jump_gap_move_extrap, 0.5) < 0.1 * rec::lat_percentile(a.jump_gap_move_plain, 0.5));
    // Maximum ook beperkt.
    CHECK(*std::max_element(a.jump_gap_move_extrap.begin(), a.jump_gap_move_extrap.end()) <
          *std::max_element(a.jump_gap_move_plain.begin(), a.jump_gap_move_plain.end()));
    // Transport: verloren rapporten worden herkend.
    rec::TransportStats ts = rec::transport_stats(recs);
    CHECK(ts.reports > 3500 && ts.lost_est > 450 && ts.lost_est < 700);
}

// Deterministisch: dezelfde opname + dezelfde instelling = dezelfde digest, ook met voorspelling; andere horizon = andere digest.
static void test_replay_deterministic_with_predict() {
    const std::vector<rec::Record> recs = synthetic_recording(true);
    rec::ReplayOptions o;
    o.pipeline.predict.horizon_ms = 40;
    o.keep_json = true;
    rec::ReplayResult a = rec::replay_records(recs, o), b = rec::replay_records(recs, o);
    CHECK(a.ok && b.ok && a.digest == b.digest && a.json == b.json);
    rec::ReplayOptions o0 = o;
    o0.pipeline.predict.horizon_ms = 0;
    rec::ReplayResult c = rec::replay_records(recs, o0);
    CHECK(c.digest != a.digest);
    // De track (voor de analyse) is ook deterministisch.
    rec::Track t1 = rec::collect_track(recs, o), t2 = rec::collect_track(recs, o);
    CHECK(t1.ox == t2.ox && t1.vx == t2.vx && t1.age == t2.age);
    // Standaard (horizon 0) wijkt niet af van het gedrag zonder voorspelling: x_pred == x_raw in elk sample.
    rec::ReplayOptions off;
    off.pipeline.predict.horizon_ms = 0;
    bool same = true;
    off.on_sample = [&](const InputSample& s, const RawReport*) { if (s.x_pred != s.x_raw || s.y_pred != s.y_raw) same = false; };
    rec::replay_records(recs, off);
    CHECK(same);
}

int main() {
    test_constant_rate_exact();
    test_gaps_do_not_disturb();
    test_stillness_no_extra_jitter();
    test_abrupt_stop_no_swing();
    test_live_horizon();
    test_output_velocity();
    test_latency_measurement();
    test_latency_gap_jumps();
    test_replay_deterministic_with_predict();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
