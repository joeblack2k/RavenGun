// Tests voor de gyro-biasschatter zonder stilleggen (quasi-stille vensters), de bewaarde bias (restored),
// de temperatuurcompensatie en het opslagbestand. Synthetische sporen met een bekende bias; geen hardware.
//
// Eisen uit de opdracht van 2026-10-08 (Halo-opname, speler houdt de gun de hele tijd vast):
//  (a) vaste bias 2,5 graden/s, handtrilling 0,6 graden/s sd, nooit echt stil: binnen 30 s op 0,3 graden/s gevonden;
//  (b) een echte langzame draai (3 graden/s, 5 s) en daarna 20 s stilhouden: de aim kruipt niet meer dan 0,5 graad terug;
//  (c) geen sprong in de aim wanneer de schatting bijwerkt.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/bias_store.hpp"
#include "ravengun/core/fusion.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/core/yaw_trim.hpp"
#include "ravengun/json.hpp"
#include "ravengun/rec/drift.hpp"
#include "ravengun/rec/replay.hpp"

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

struct Lcg {
    uint64_t s = 20261008;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }
};

static std::string tmpdir() {
    char t[] = "/tmp/ravengun-bias-test-XXXXXX";
    char* d = mkdtemp(t);
    return d ? d : "";
}

static Vec3 dps(double x, double y, double z) { return Vec3{x, y, z} * kRadPerDeg; }
static double dist_dps(Vec3 a, Vec3 b) { return norm(a - b) * kDegPerRad; }

// ---------------- Handmodel ----------------
//
// Gyro = werkelijke hoeksnelheid + bias + trilling (wit) + zwaai (Ornstein-Uhlenbeck, per as). Accelerometer =
// omhoog (0,0,1) met kleine ruis. De zwaai is langzaam genoeg om als "bijna stil" te tellen (zie shake/sway).
struct Hand {
    Vec3 bias = dps(1.5, -2.2, 0.9);
    double tremor_dps = 0.6;     // witte trilling per as, sd
    double sway_dps = 0.0;       // OU-zwaai per as, sd
    double sway_tau_s = 0.3;
    double accel_noise = 0.004;  // g
    Vec3 sway{};
    Lcg rng;
    // Werkelijke hoeksnelheid (rad/s, controllerstelsel) en de ruwe meting op tijd t, stap dt.
    void sample(Vec3 w_true, double dt, Vec3& gyro, Vec3& accel) {
        if (sway_dps > 0) {
            double a = std::exp(-dt / sway_tau_s), s = sway_dps * std::sqrt(1 - a * a);
            sway = Vec3{sway.x * a + s * rng.gauss(), sway.y * a + s * rng.gauss(), sway.z * a + s * rng.gauss()};
        }
        Vec3 tr{rng.gauss(), rng.gauss(), rng.gauss()};
        gyro = w_true + bias + (sway + tr * tremor_dps) * kRadPerDeg;
        accel = Vec3{rng.gauss() * accel_noise, rng.gauss() * accel_noise, 1.0 + rng.gauss() * accel_noise};
    }
};

// ---------------- Estimator rechtstreeks ----------------

static const double kSub = 0.01127 / 2;  // seconde per subsample

// Eis (a): bias 2,5 graden/s op elke as-component van een schuine vector, nooit echt stil.
static void test_bias_found_in_hand() {
    for (int variant = 0; variant < 2; ++variant) {
        Hand h;
        h.bias = variant == 0 ? dps(2.5, 0, 0) : dps(1.2, -1.6, 1.5);  // variant 1: norm 2,5
        h.tremor_dps = 0.6;
        h.sway_dps = 0.3;      // ... en een langzame zwaai erbovenop (de hand staat nooit echt stil)
        h.sway_tau_s = 0.3;
        GyroBiasEstimator e;
        double found_at = -1;
        for (int i = 0; i < static_cast<int>(30.0 / kSub); ++i) {
            Vec3 g, a;
            h.sample({}, kSub, g, a);
            e.update(g, a, kSub);
        }
        found_at = dist_dps(e.bias(), h.bias);
        std::printf("test (a) variant %d: bias na 30 s binnen %.3f graden/s (bron %s)\n", variant, found_at, to_string(e.source()));
        CHECK(found_at <= 0.3);
        CHECK(e.state() == BiasState::Estimated);
    }
}

// (a2) Schudden dat de strikte stilstandsvensters (sd <= 2 graden/s) nooit haalt, maar quasi-stil is (sd 2,2): alleen de
// nieuwe modus vindt de bias; legacy blijft op 0.
static void test_bias_quasi_only() {
    Hand h;
    h.bias = dps(2.5, -1.0, 1.0);
    h.tremor_dps = 2.2;
    GyroBiasEstimator legacy([] { BiasConfig c; c.mode = BiasMode::Legacy; return c; }());
    GyroBiasEstimator e;
    int t_found = -1;
    for (int i = 0; i < static_cast<int>(40.0 / kSub); ++i) {
        Vec3 g, a;
        h.sample({}, kSub, g, a);
        legacy.update(g, a, kSub);
        e.update(g, a, kSub);
        if (t_found < 0 && e.state() == BiasState::Estimated) t_found = static_cast<int>(i * kSub);
    }
    std::printf("test (a2): shudden sd 2,2 graden/s: nieuw %.3f graden/s fout (gevonden na %d s, bron %s), legacy %s\n",
                dist_dps(e.bias(), h.bias), t_found, to_string(e.source()), to_string(legacy.state()));
    CHECK(e.state() == BiasState::Estimated);
    CHECK(e.source() == BiasSource::Quasi);
    CHECK(t_found >= 0 && t_found <= 30);
    CHECK(dist_dps(e.bias(), h.bias) <= 0.3);
    CHECK(legacy.state() != BiasState::Estimated);   // hooguit een los strikt venster; nooit 3 s stilstand
}

static void test_restored_and_gates() {
    GyroBiasEstimator e;
    CHECK(e.state() == BiasState::None);
    e.restore(dps(1.5, -2.2, 0.9), 2050);
    CHECK(e.state() == BiasState::Restored);
    CHECK(e.source() == BiasSource::Restored);
    CHECK(!e.measured());
    CHECK_NEAR(dist_dps(e.bias(), dps(1.5, -2.2, 0.9)), 0, 1e-9);
    CHECK(e.restored_temperature() == 2050);
    // Vensters die 3 graden/s van de bewaarde waarde liggen (een bewuste draai) worden niet opgenomen.
    Hand h;
    h.bias = dps(1.5, -2.2, 0.9);
    for (int i = 0; i < static_cast<int>(6.0 / kSub); ++i) {
        Vec3 g, a;
        h.sample(dps(0, 0, 3.0), kSub, g, a);
        e.update(g, a, kSub);
    }
    CHECK(e.state() == BiasState::Restored);
    CHECK_NEAR(dist_dps(e.bias(), dps(1.5, -2.2, 0.9)), 0, 1e-9);
    // Echte rust: de schatting gaat naar de gemeten waarde (de bewaarde was 0,4 graden/s te hoog) en wordt "estimated".
    Hand h2;
    h2.bias = dps(1.5, -2.2, 0.5);
    for (int i = 0; i < static_cast<int>(12.0 / kSub); ++i) {
        Vec3 g, a;
        h2.sample({}, kSub, g, a);
        e.update(g, a, kSub);
    }
    CHECK(e.state() == BiasState::Estimated);
    CHECK(e.source() == BiasSource::Still);
    CHECK(dist_dps(e.bias(), h2.bias) < 0.1);
    // Een tweede restore na een meting doet niets.
    Vec3 before = e.bias();
    e.restore(dps(5, 5, 5), 1);
    CHECK_NEAR(dist_dps(e.bias(), before), 0, 1e-12);
    // Bewaarde waarde ver (3 graden/s) van de werkelijkheid: na 10 s consistente rust herstelt reacquire de bias.
    GyroBiasEstimator f;
    f.restore(dps(1.5, -2.2, 3.9), -1);
    Hand h3;
    h3.bias = dps(1.5, -2.2, 0.9);
    int t_ok = -1;
    for (int i = 0; i < static_cast<int>(40.0 / kSub); ++i) {
        Vec3 g, a;
        h3.sample({}, kSub, g, a);
        f.update(g, a, kSub);
        if (t_ok < 0 && dist_dps(f.bias(), h3.bias) < 0.2) t_ok = static_cast<int>(i * kSub);
    }
    std::printf("test restored ver weg: bias weer goed na %d s\n", t_ok);
    CHECK(t_ok >= 6 && t_ok <= 20);
    CHECK(f.state() == BiasState::Estimated);
}

// Een draai van 3 graden/s over 5 s mag nooit (ook niet gedeeltelijk) als bias worden opgenomen, in elke toestand.
static void test_slow_turn_not_bias() {
    for (int variant = 0; variant < 3; ++variant) {
        Hand h;
        h.bias = dps(1.5, -2.2, 0.9);
        h.tremor_dps = variant == 2 ? 2.2 : 0.6;  // variant 2: quasi-schudden
        GyroBiasEstimator e;
        if (variant == 1) e.restore(h.bias, 2000);  // bewaarde (juiste) beginwaarde
        double t = 0;
        auto run = [&](double dur, Vec3 w) {
            for (int i = 0; i < static_cast<int>(dur / kSub); ++i) {
                Vec3 g, a;
                h.sample(w, kSub, g, a);
                e.update(g, a, kSub);
                t += kSub;
            }
        };
        run(variant == 2 ? 30 : 12, {});      // bias vastleggen
        Vec3 b0 = e.bias();
        CHECK(dist_dps(b0, h.bias) < 0.35);
        run(5.0, dps(0, 0, 3.0));               // draai
        double after_turn = dist_dps(e.bias(), b0);
        run(20.0, {});                          // vasthouden
        double after_hold = dist_dps(e.bias(), h.bias);
        std::printf("test (b) estimator variant %d: bias-verschuiving tijdens draai %.3f graden/s, fout na het vasthouden %.3f graden/s\n", variant,
                    after_turn, after_hold);
        CHECK(after_turn < 0.05);   // de draai zelf verschuift de bias niet
        CHECK(after_hold < 0.3);
    }
}

// Temperatuurcompensatie: de z-bias loopt mee met de temperatuur (0,006 graden/s per telling). Eerst 30 s rust met
// stijgende temperatuur (de helling wordt geleerd), daarna zwaaien zonder rust terwijl de temperatuur doorloopt.
static void test_temperature_comp() {
    for (int variant = 0; variant < 2; ++variant) {
        const double slope = variant == 0 ? 0.006 : 0.0;  // variant 1: temperatuur stijgt, bias niet
        Hand h;
        h.bias = dps(1.5, -2.2, 0.9);
        GyroBiasEstimator e;
        double t = 0;
        int tc = 1860;
        auto step = [&](Vec3 w, double shake) {
            double tc_real = 1860 + t * 1.0;  // 1 telling per seconde
            tc = static_cast<int>(tc_real);
            Hand hh = h;
            Vec3 zb = dps(0, 0, slope * (tc_real - 1860));
            Vec3 g, a;
            h.tremor_dps = shake;
            h.sample(w, kSub, g, a);
            e.set_temperature(tc);
            e.update(g + zb, a, kSub);
            (void)hh;
            t += kSub;
        };
        for (int i = 0; i < static_cast<int>(40.0 / kSub); ++i) step({}, 0.6);        // rust, 40 tellingen
        for (int i = 0; i < static_cast<int>(80.0 / kSub); ++i) {                      // 80 s zwaaien: geen rust
            double ph = std::fmod(t, 2.0) / 2.0;
            step(dps(0, 0, 60.0 * std::sin(2 * kPi * ph)), 0.6);
        }
        Vec3 truth = h.bias + dps(0, 0, slope * (1860 + t - 1860));
        double err = dist_dps(e.bias(), truth);
        std::printf("test temperatuur variant %d: bias-fout na 120 s %.3f graden/s (helling z %+.4f graden/s per telling)\n", variant, err,
                    e.temp_slope().z * kDegPerRad);
        if (variant == 0) {
            CHECK(err < 0.25);
            CHECK(e.temp_slope().z * kDegPerRad > 0.003);
        } else {
            CHECK(err < 0.15);
            CHECK(std::fabs(e.temp_slope().z * kDegPerRad) < 0.002);
        }
    }
}

static void test_quasi_disabled_by_mag() {
    // Zolang de magnetometer de yaw corrigeert, stuurt het quasi-bewijs de bias niet bij (niet dubbel corrigeren).
    Hand h;
    h.bias = dps(2.5, -1.0, 1.0);
    h.tremor_dps = 2.2;
    GyroBiasEstimator e;
    e.set_quasi_enabled(false);
    for (int i = 0; i < static_cast<int>(30.0 / kSub); ++i) {
        Vec3 g, a;
        h.sample({}, kSub, g, a);
        e.update(g, a, kSub);
    }
    CHECK(e.state() != BiasState::Estimated);
    CHECK(e.quasi_accepted() == 0 && e.source() != BiasSource::Quasi);
    e.set_quasi_enabled(true);
    for (int i = 0; i < static_cast<int>(30.0 / kSub); ++i) {
        Vec3 g, a;
        h.sample({}, kSub, g, a);
        e.update(g, a, kSub);
    }
    CHECK(e.state() == BiasState::Estimated);
}

// ---------------- Pijplijn: aim ----------------

struct PipeRun {
    Pipeline p;
    Hand h;
    double t = 0;
    double yaw_truth = 0;   // graden, rond omhoog
    int64_t t_ns = 1'000'000'000;
    int seq = 0;
    int tc = 2000;
    int tc_step_every = 0;  // 0: constante temperatuur; anders: +1 telling per zoveel rapporten
    int n = 0;
    explicit PipeRun(PipelineConfig c = {}) : p(c) { p.on_connect("aa:bb:cc:dd:ee:ff", t_ns); }
    InputSample step(double yaw_rate_dps, Vec3 extra_bias = {}) {
        const double period = 0.01127;
        RawReport r;
        r.subsamples = 2;
        for (int i = 0; i < 2; ++i) {
            Vec3 a;
            h.sample(dps(0, 0, -yaw_rate_dps), period / 2, r.gyro[i], a);  // yaw naar rechts = rotatie om -z
            r.gyro[i] = r.gyro[i] + extra_bias;
            r.accel[i] = a;
        }
        yaw_truth += yaw_rate_dps * period;
        t += period;
        t_ns += static_cast<int64_t>(period * 1e9);
        r.t_rx_ns = t_ns;
        r.hid_seq = ++seq % 16;
        if (tc_step_every > 0 && ++n % tc_step_every == 0) ++tc;
        r.temperature_raw = tc;
        return p.on_report(r);
    }
    double yaw() const { return world_angles(p.last().q_wb).yaw_rad * kDegPerRad; }
};

static void test_turn_then_hold_pipeline() {
    for (int variant = 0; variant < 2; ++variant) {
        // Zelfde ruis met en zonder draai (zelfde generator, zelfde aantal stappen): het verschil in de yaw-verandering
        // tijdens het vasthouden is precies het effect van de draai op de schatter.
        double creep[2], shift[2];
        for (int with_turn = 0; with_turn < 2; ++with_turn) {
            PipeRun r;
            r.h.bias = dps(1.5, -2.2, 0.9);
            r.h.tremor_dps = variant == 0 ? 0.6 : 2.2;   // variant 1: nooit strikt stil, alleen quasi
            const double lead = variant == 0 ? 12 : 35;
            for (double t = 0; t < lead; t += 0.01127) r.step(0);
            CHECK(r.p.last().bias_state == BiasState::Estimated);
            Vec3 b0 = r.p.bias().bias();
            const double yaw_before_turn = r.yaw();
            for (double t = 0; t < 5.0; t += 0.01127) r.step(with_turn ? 3.0 : 0.0);   // echte draai van 15 graden
            double yaw_after_turn = r.yaw();
            if (with_turn) CHECK(std::fabs(std::fabs(yaw_after_turn - yaw_before_turn) - 15.0) < 1.0);   // de draai is gewoon zichtbaar
            for (double t = 0; t < 20.0; t += 0.01127) r.step(0);
            creep[with_turn] = r.yaw() - yaw_after_turn;
            shift[with_turn] = dist_dps(r.p.bias().bias(), b0);
        }
        std::printf("test (b) pijplijn variant %d: aim kruipt %.3f graden in 20 s na de draai (zonder draai %.3f); bias-verschuiving %.3f (zonder draai %.3f) graden/s\n",
                    variant, creep[1], creep[0], shift[1], shift[0]);
        CHECK(std::fabs(creep[1] - creep[0]) <= 0.5);       // wat de draai erbij doet
        CHECK(std::fabs(shift[1] - shift[0]) < 0.1);
        if (variant == 0) CHECK(std::fabs(creep[1]) <= 0.5);   // na echte rust: ook absoluut niet terug
    }
}

static void test_no_jump_on_bias_update() {
    // De bias loopt geleidelijk weg (temperatuur) terwijl de gun stilstaat met schudden; de schatting werkt bij.
    // Geen enkele stap in de aim of de bias mag groter zijn dan wat integratie van ruis en een kleine biasfout geeft.
    PipeRun r;
    r.h.bias = dps(1.5, -2.2, 0.9);
    r.h.tremor_dps = 2.2;
    r.tc_step_every = 89;  // ~1 telling per seconde
    for (double t = 0; t < 30; t += 0.01127) r.step(0);
    double max_dyaw = 0, max_dbias = 0, bias_total = 0;
    Vec3 pb = r.p.bias().bias();
    double py = r.yaw();
    uint64_t changes = 0;
    for (double t = 0; t < 120; t += 0.01127) {
        // z-bias loopt 0,6 graden/s omhoog over 120 s
        InputSample s = r.step(0, dps(0, 0, 0.6 * t / 120.0));
        (void)s;
        double y = r.yaw();
        max_dyaw = std::max(max_dyaw, std::fabs(y - py));
        py = y;
        Vec3 b = r.p.bias().bias();
        double db = dist_dps(b, pb);
        if (db > 0) ++changes;
        max_dbias = std::max(max_dbias, db);
        pb = b;
    }
    bias_total = dist_dps(r.p.bias().bias(), dps(1.5, -2.2, 0.9));
    std::printf("test (c): grootste aimstap per rapport %.4f graden, grootste biasstap %.5f graden/s, %llu biaswijzigingen, bias %.2f graden/s verschoven\n",
                max_dyaw, max_dbias, static_cast<unsigned long long>(changes), bias_total);
    CHECK(changes > 100);      // de schatting werkt echt bij
    // Een biasstap verandert alleen de integratiesnelheid vanaf dat moment (er wordt niets met terugwerkende kracht
    // verzet): per rapport hooguit stap x 11 ms. Samen met de ruis (2,2 graden/s over twee subsamples: sd 0,02 graden)
    // blijft de aim binnen 0,1 graden per rapport.
    CHECK(max_dbias < 0.1);
    CHECK(max_dbias * 0.01127 < 0.002);
    CHECK(max_dyaw < 0.1);
}

static void test_pipeline_restore_and_json() {
    PipelineConfig c;
    Pipeline p(c);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    p.restore_bias(dps(1.5, -2.2, 0.9), 2050);
    CHECK(p.bias().state() == BiasState::Restored);
    RawReport r;
    r.subsamples = 2;
    r.accel[0] = r.accel[1] = Vec3{0, 0, 1};
    r.gyro[0] = r.gyro[1] = dps(1.5, -2.2, 0.9);
    r.t_rx_ns = 1'100'000'000;
    InputSample s = p.on_report(r);
    CHECK(s.bias_state == BiasState::Restored);
    CHECK(s.bias_source == BiasSource::Restored);
    CHECK_NEAR(dist_dps(s.gyro_bias, dps(1.5, -2.2, 0.9)), 0, 1e-9);
    json::Writer w;
    write_sample_json(w, s, false);
    CHECK(w.str().find("\"bias_state\":\"restored\"") != std::string::npos);
    CHECK(w.str().find("\"bias_source\":\"restored\"") != std::string::npos);
    // Een andere controller wist de toestand (ook de bewaarde waarde).
    p.on_connect("11:22:33:44:55:66", 2'000'000'000);
    CHECK(p.bias().state() == BiasState::None);
    // Legacy-modus: precies het oude gedrag voor de toestandsnamen (geen quasi).
    PipelineConfig lc;
    lc.bias.mode = BiasMode::Legacy;
    Pipeline lp(lc);
    lp.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    CHECK(lp.bias().config().mode == BiasMode::Legacy);
}

// ---------------- Optische stilstand: de camera als bewaker van de bias ----------------

// Voert estimator of pijplijn met een IMU-stroom (90 Hz rapporten, 2 subsamples) en een cameraframe-stroom (60 fps).
enum class Ball { Still, Moving, Dropouts };

struct OptFeed {
    Hand h;
    Lcg orng;
    double t = 100.0;          // seconden, gedeelde klok van IMU en camera
    double next_frame = 100.0;
    int frame_n = 0;
    double accel_scale = 1.0;  // 1,3 = de gun wordt geschud (|a| ver van 1 g)
    static constexpr double period = 0.01127;
    // Geeft de frames die in (t_prev, t] vallen.
    template <typename Imu, typename Cam>
    void run(double seconds, Ball b, Imu&& imu, Cam&& cam) {
        const double end = t + seconds;
        while (t < end) {
            t += period;
            for (int i = 0; i < 2; ++i) {
                Vec3 g, a;
                h.sample({}, period / 2, g, a);
                imu(g, a * accel_scale, period / 2, t - (1 - i) * period / 2);
            }
            while (next_frame <= t) {
                bool valid = true;
                double x = 320, y = 240, r = 12;
                if (b == Ball::Still) {
                    x += orng.gauss() * 0.05; y += orng.gauss() * 0.05; r += orng.gauss() * 0.03;
                } else if (b == Ball::Moving) {
                    x += 60 * std::sin(2 * kPi * 0.8 * next_frame); y += 20 * std::cos(2 * kPi * 0.8 * next_frame);
                } else {
                    x += orng.gauss() * 0.05; y += orng.gauss() * 0.05; r += orng.gauss() * 0.03;
                    valid = (frame_n % 25) < 20;     // 20 frames (0,33 s) in beeld, dan 5 weg
                }
                cam(next_frame, valid, x, y, r);
                next_frame += 1.0 / 60;
                ++frame_n;
            }
        }
    }
};

// Een estimator waarvan stilstand en quasi-stil blind zijn: alleen het optische bewijs kan de bias nog bijsturen.
static BiasConfig blind_cfg() {
    BiasConfig c;
    c.gyro_std_max = 1e-6;
    c.quasi_std_max = 1e-6;
    return c;
}

static void test_optical_corrects_wrong_bias() {
    const Vec3 truth = dps(1.5, -2.2, 0.9);
    const Vec3 wrong = truth + dps(1.4, -1.0, 0.8);   // 1,9 graden/s ernaast, buiten de poort van de stilstandsvensters
    for (int variant = 0; variant < 2; ++variant) {
        GyroBiasEstimator e(blind_cfg());
        e.set_quasi_enabled(false);
        e.restore(wrong, 2050);
        if (variant == 1) e.set_optical_enabled(false);
        OptFeed f;
        f.h.bias = truth;
        f.h.tremor_dps = 0.6;
        double err_at_20 = 0, max_step = 0;
        Vec3 prev = e.bias();
        auto imu = [&](Vec3 g, Vec3 a, double dt, double ts) {
            e.update(g, a, dt, ts);
            max_step = std::max(max_step, dist_dps(e.bias(), prev));
            prev = e.bias();
        };
        auto cam = [&](double ts, bool v, double x, double y, double r) { e.optical_frame(ts, v, x, y, r); };
        f.run(20.0, Ball::Still, imu, cam);
        err_at_20 = dist_dps(e.bias(), truth);
        std::printf("test optisch %d: bias-fout %.3f graden/s na 20 s (begin %.2f), %llu vensters gebruikt, bron %s\n", variant,
                    err_at_20, dist_dps(wrong, truth), static_cast<unsigned long long>(e.optical_accepted()), to_string(e.source()));
        if (variant == 0) {
            CHECK(err_at_20 < 0.25);
            CHECK(e.optical_accepted() > 10);
            CHECK(e.source() == BiasSource::Optical);
            CHECK(e.optical_time_total() > 5);
            CHECK(max_step < 0.15);   // geleidelijk: nooit een sprong (hooguit de slew per venster en as: 0,075 graden/s)
            CHECK(e.measured());
        } else {
            CHECK(err_at_20 > 1.5);    // uit: de bias blijft fout (de rest is blind)
            CHECK(e.optical_accepted() == 0);
        }
    }
}

static void test_optical_moving_ball_no_evidence() {
    const Vec3 truth = dps(1.5, -2.2, 0.9);
    const Vec3 wrong = truth + dps(1.4, -1.0, 0.8);
    for (int variant = 0; variant < 3; ++variant) {
        GyroBiasEstimator e(blind_cfg());
        e.set_quasi_enabled(false);
        e.restore(wrong, 2050);
        OptFeed f;
        f.h.bias = truth;
        f.h.tremor_dps = 0.6;
        if (variant == 2) f.accel_scale = 1.3;   // bol stil, maar de IMU zegt: geschud
        auto imu = [&](Vec3 g, Vec3 a, double dt, double ts) { e.update(g, a, dt, ts); };
        auto cam = [&](double ts, bool v, double x, double y, double r) { e.optical_frame(ts, v, x, y, r); };
        f.run(30.0, variant == 0 ? Ball::Moving : variant == 1 ? Ball::Dropouts : Ball::Still, imu, cam);
        std::printf("test optisch geen bewijs %d: %llu gebruikt, %llu stilstaande vensters, bias-fout %.3f\n", variant,
                    static_cast<unsigned long long>(e.optical_accepted()), static_cast<unsigned long long>(e.optical_still_windows()),
                    dist_dps(e.bias(), truth));
        CHECK(e.optical_accepted() == 0);
        CHECK_NEAR(dist_dps(e.bias(), wrong), 0, 1e-9);   // de bias is niet aangeraakt
        CHECK(e.source() == BiasSource::Restored);
        if (variant != 2) CHECK(e.optical_still_windows() == 0);
    }
}

// Pijplijn: cameraframes via on_optical, tijdbasis gedeeld met de rapporten (t_rx_ns en t_frame_ns).
static void test_optical_pipeline() {
    const Vec3 truth = dps(1.5, -2.2, 0.9);
    const Vec3 wrong = truth + dps(1.4, -1.0, 0.8);
    for (int variant = 0; variant < 2; ++variant) {
        PipelineConfig cfg;
        cfg.bias = blind_cfg();
        cfg.bias.optical = variant == 0;
        PipeRun r(cfg);
        r.h.bias = truth;
        r.h.tremor_dps = 0.6;
        r.p.restore_bias(wrong, 2050);
        Lcg orng;
        double next_frame = 0;
        const int64_t t0 = r.t_ns;
        for (double t = 0; t < 25; t += 0.01127) {
            r.step(0);
            while (next_frame <= static_cast<double>(r.t_ns - t0) / 1e9) {
                OpticalSample o;
                o.camera_active = true;
                o.valid = true;
                o.t_frame_ns = t0 + static_cast<int64_t>(next_frame * 1e9);
                o.x_px = 320 + orng.gauss() * 0.05; o.y_px = 240 + orng.gauss() * 0.05; o.radius_px = 12 + orng.gauss() * 0.03;
                r.p.on_optical(o);
                r.p.on_optical(o);   // dubbel aanbieden telt eenmaal (zelfde frame)
                next_frame += 1.0 / 60;
            }
        }
        const double err = dist_dps(r.p.bias().bias(), truth);
        std::printf("test optisch pijplijn %d: bias-fout %.3f graden/s, bron %s\n", variant, err, to_string(r.p.bias().source()));
        if (variant == 0) {
            CHECK(err < 0.3);
            CHECK(r.p.last().bias_source == BiasSource::Optical);
            CHECK(r.p.last().bias_evidence_s > 5);
            json::Writer w;
            write_sample_json(w, r.p.last(), false);
            CHECK(w.str().find("\"bias_source\":\"optical-still\"") != std::string::npos);
        } else {
            CHECK(err > 1.5);
        }
    }
}

// ---------------- Yaw-trim uit centreerdrukken ----------------

// Pijplijn met een vaste, bekende bias (bevroren schatter) en een constante extra gyro-fout rond omhoog. De speler
// richt vóór elke druk met een richtfout (sd aim_noise graden) op het midden; de draai ernaartoe zit gewoon in de gyro.
struct TrimRun {
    PipeRun r;
    Lcg rng;
    double heading = 0;   // huidige richting t.o.v. het midden (graden, yaw naar rechts)
    static PipelineConfig cfg(bool learn) {
        PipelineConfig c;
        c.bias.freeze_after_s = 0.02;
        c.bias.optical = false;
        c.trim.enabled = learn;
        return c;
    }
    explicit TrimRun(bool learn = true) : r(cfg(learn)) {
        r.h.bias = dps(1.5, -2.2, 0.9);
        r.p.restore_bias(r.h.bias, 2050);
    }
    double press(double hold_s, double err_z_dps, double aim_err_deg) {
        for (double t = 0; t < hold_s; t += 0.01127) r.step(0, dps(0, 0, err_z_dps));
        const double delta = aim_err_deg - heading;
        const int n = 27;   // ~0,3 s
        for (int i = 0; i < n; ++i) r.step(delta / (n * 0.01127), dps(0, 0, err_z_dps));
        heading = aim_err_deg;
        r.p.recenter(true);
        return r.p.trim().rate_dps();
    }
};

static void test_trim_learns_constant_error() {
    const double err = 0.2;   // graden/s extra rotatie rond +Z: de yaw loopt met -0,2 graden/s weg; trim hoort +0,2 te worden
    TrimRun a(true), off(false);
    std::vector<double> trims;
    for (int k = 0; k < 14; ++k) {
        const double noise = k == 0 ? 0.0 : 1.5 * a.rng.gauss();
        const double hold = k == 0 ? 5.0 : 25.0 + 15.0 * a.rng.uniform();   // 25..40 s tussen de drukken
        trims.push_back(a.press(hold, err, noise));
    }
    std::string line;
    for (double t : trims) { char b[24]; std::snprintf(b, sizeof b, " %+.3f", t); line += b; }
    std::printf("test trim: trim na elke druk (doel +0.200):%s\n", line.c_str());
    // Na een paar drukken ligt de resterende fout onder 0,04 graden/s en blijft hij daar.
    for (size_t k = 6; k < trims.size(); ++k) CHECK(std::fabs(err - trims[k]) < 0.04);
    CHECK(std::fabs(err - trims[3]) < 0.12);
    CHECK(a.r.p.trim().learned() >= 10);
    CHECK(a.r.p.trim().rate_dps() <= a.r.p.config().trim.limit_dps);
    // Zonder leren verandert er niets: de trim blijft 0 en de drukken worden alleen geteld.
    for (int k = 0; k < 4; ++k) off.press(k == 0 ? 5.0 : 30.0, err, 0.0);
    CHECK(off.r.p.trim().rate_dps() == 0 && off.r.p.trim().learned() == 0 && off.r.p.trim().presses() == 4);
}

static void test_trim_outlier_press() {
    const double err = 0.2;
    // Dezelfde ruisreeks met en zonder een rare druk (20 graden ernaast) bij druk 8.
    std::vector<double> clean, dirty;
    for (int variant = 0; variant < 2; ++variant) {
        TrimRun a(true);
        auto& out = variant == 0 ? clean : dirty;
        for (int k = 0; k < 16; ++k) {
            double noise = k == 0 ? 0.0 : 1.5 * a.rng.gauss();
            if (variant == 1 && k == 8) noise += 20.0;
            out.push_back(a.press(k == 0 ? 5.0 : 30.0, err, noise));
        }
    }
    double max_dev = 0, dev_after = 0;
    for (size_t k = 8; k < clean.size(); ++k) max_dev = std::max(max_dev, std::fabs(dirty[k] - clean[k]));
    dev_after = std::fabs(dirty.back() - clean.back());
    std::printf("test trim rare druk: grootste afwijking %.3f graden/s, na 7 drukken %.3f (stap begrensd op %.2f)\n", max_dev, dev_after,
                YawTrimConfig{}.step_max_dps);
    CHECK(max_dev <= 0.10);          // hooguit een begrensde stap (en wat ruimte voor de gevolgen in de volgende paren)
    CHECK(dev_after < 0.05);         // en het wordt weer rechtgetrokken
    CHECK(std::fabs(err - dirty.back()) < 0.05);
    // Direct na de rare druk: niet meer dan step_max verplaatst ten opzichte van de schone run.
    CHECK(std::fabs(dirty[8] - clean[8]) <= YawTrimConfig{}.step_max_dps + 1e-9);
}

static void test_trim_rules() {
    YawTrim t;
    // Eerste druk: alleen een referentie. Te korte en te lange tussentijden en grote sprongen tellen niet.
    CHECK(!t.press(0, 0.0).learned);
    CHECK(!t.press(3, 0.01).learned);                         // 3 s: te kort
    CHECK(!t.press(300, 0.02).learned);                       // 297 s: te lang
    CHECK(!t.press(330, 0.02 + 30.0 * kRadPerDeg).learned);   // 30 graden: geen drift maar een sprong
    CHECK(t.rate_dps() == 0 && t.learned() == 0);
    // Gewone druk: yaw 3 graden naar rechts in 30 s -> trim wordt positief (extra +Z-rotatie wordt afgetrokken)... en begrensd.
    YawTrim u;
    u.press(0, 0.0);
    YawTrimUpdate up = u.press(30, 3.0 * kRadPerDeg);
    CHECK(up.learned);
    CHECK_NEAR(up.drift_deg, 3.0, 1e-6);
    CHECK(u.rate_dps() < 0 && std::fabs(up.step_dps) <= u.config().step_max_dps + 1e-12);   // yaw stijgt -> trim negatief
    YawTrim v;
    v.press(0, 0.0);
    for (int k = 1; k < 40; ++k) v.press(k * 30.0, 0.0);   // geen drift: de trim blijft nul
    CHECK(std::fabs(v.rate_dps()) < 1e-9);
    // Grens van de trim zelf.
    YawTrimConfig big;
    big.step_max_dps = 1.0;
    YawTrim w(big);
    w.press(0, 0.0);
    for (int k = 1; k < 30; ++k) w.press(k * 30.0, -20.0 * kRadPerDeg * k);   // 20 graden per 30 s, steeds dezelfde kant
    CHECK(w.rate_dps() <= big.limit_dps + 1e-12 && w.rate_dps() > 0.55);
    // Uit: geen leren; invalidate: de volgende druk is weer een eerste druk; wissen zet alles terug.
    YawTrimConfig offc;
    offc.enabled = false;
    YawTrim x(offc);
    x.press(0, 0.0);
    CHECK(!x.press(30, 0.05).learned && x.rate_dps() == 0);
    u.invalidate();
    CHECK(!u.press(100, 0.0).learned);
    u.clear();
    CHECK(u.rate_dps() == 0 && u.learned() == 0 && u.presses() == 0);
    u.set(5.0, 3);   // begrensd
    CHECK_NEAR(u.rate_dps(), u.config().limit_dps, 1e-12);
    CHECK(u.learned() == 3);
    // Toepassen: Z omhoog in de wereld; bij een controller die rechtop staat is het body-Z.
    YawTrim y;
    y.set(0.5);
    Vec3 b = y.body_rate(Quat{});
    CHECK_NEAR(b.z, 0.5 * kRadPerDeg, 1e-12);
    CHECK_NEAR(b.x, 0, 1e-12);
}

// Niet toepassen en niet leren zolang de magnetometer de yaw corrigeert; een gat breekt de referentie.
static void test_trim_pipeline_rules() {
    TrimRun a(true);
    a.press(5, 0.0, 0.0);
    CHECK(a.r.p.trim().presses() == 1);
    // Een recenter van ravengunctl (niet van de Move) telt niet als meetpunt.
    for (double t = 0; t < 20; t += 0.01127) a.r.step(0);
    a.r.p.recenter(false);
    CHECK(a.r.p.trim().presses() == 1);
    // Een andere controller wist de trim; reconnect van dezelfde behoudt hem.
    a.r.p.set_trim(0.3, 4);
    a.r.p.on_connect("aa:bb:cc:dd:ee:ff", a.r.t_ns + 1'000'000);
    CHECK_NEAR(a.r.p.trim().rate_dps(), 0.3, 1e-12);
    a.r.p.on_connect("11:22:33:44:55:66", a.r.t_ns + 2'000'000);
    CHECK(a.r.p.trim().rate_dps() == 0 && a.r.p.trim().learned() == 0);
    // Het toegepaste effect: een bekende trim draait de yaw weg met precies die snelheid.
    TrimRun b(false);
    b.r.p.set_trim(0.4, 1);
    for (double t = 0; t < 5; t += 0.01127) b.r.step(0);
    const double y0 = b.r.yaw();
    for (double t = 0; t < 50; t += 0.01127) b.r.step(0);
    CHECK_NEAR(b.r.yaw() - y0, 0.4 * 50, 1.0);   // trim +0,4 om +Z afgetrokken -> yaw (rechts) stijgt met 0,4 graden/s
}

// De trim staat in hetzelfde bestand als de bias: heen en terug, laden bij de start, wissen.
static void test_trim_persistence() {
    std::string d = tmpdir();
    CHECK(!d.empty());
    const std::string dir = d + "/gyro-bias";
    const std::string id = "aa:bb:cc:dd:ee:ff";
    const Vec3 truth = dps(1.5, -2.2, 0.9);
    const int64_t unix_s = 1791480934;
    std::string err;
    // Bestandsformaat: met trim, zonder trim (oude bestanden) en een trim buiten bereik.
    SavedBias b;
    b.bias = truth; b.key = "37b2de54e4ef26ba"; b.source = "optical-still"; b.has_trim = true; b.trim_dps = 0.173; b.trim_learned = 7;
    SavedBias l;
    CHECK(bias_from_json(bias_to_json(b), l, err) && l.has_trim && std::fabs(l.trim_dps - 0.173) < 1e-9 && l.trim_learned == 7);
    CHECK(l.source == "optical-still");
    b.has_trim = false;
    CHECK(bias_from_json(bias_to_json(b), l, err) && !l.has_trim);
    CHECK(bias_from_json("{\"format\":\"ravengun-gyro-bias\",\"version\":1,\"bias_deg_s\":[1,2,3],\"yaw_trim_deg_s\":9}", l, err) && !l.has_trim);
    {
        PipeRun r;
        r.h.bias = truth;
        BiasPersistence bp;
        bp.set_dir(dir);
        std::string info;
        bp.on_connect(r.p, id, 0, unix_s, &info);
        for (double t = 0; t < 40; t += 0.01127) { r.step(0); bp.on_report(r.p, r.t, unix_s, &info); }
        CHECK(bp.saves() >= 1);
        const uint64_t before = bp.saves();
        r.p.set_trim(0.21, 6);
        for (double t = 0; t < 8; t += 0.01127) { r.step(0); bp.on_report(r.p, r.t, unix_s, &info); }
        CHECK(bp.saves() == before + 1);   // eenmaal, bij de verandering
        SavedBias on_disk;
        CHECK(load_bias(dir, controller_key(id), on_disk, err) && on_disk.has_trim);
        CHECK_NEAR(on_disk.trim_dps, 0.21, 1e-6);
        CHECK(on_disk.trim_learned == 6 && dist_dps(on_disk.bias, truth) < 0.1);
    }
    {
        // Nieuwe sessie: bias en trim worden hersteld; wissen schrijft nul (de bias blijft staan).
        Pipeline p;
        p.on_connect(id, 1'000'000'000);
        BiasPersistence bp;
        bp.set_dir(dir);
        std::string info;
        CHECK(bp.on_connect(p, id, 0, unix_s + 100, &info));
        CHECK(info.find("yaw-trim hersteld") != std::string::npos && info.find("gyro-bias hersteld") != std::string::npos);
        CHECK_NEAR(p.trim().rate_dps(), 0.21, 1e-6);
        CHECK(p.trim().learned() == 6);
        CHECK(!bp.on_report(p, 1.0, unix_s + 101, &info));   // niets veranderd: niets schrijven
        p.clear_trim();
        bp.note_trim_cleared();
        CHECK(bp.on_report(p, 100.0, unix_s + 200, &info));
        SavedBias on_disk;
        CHECK(load_bias(dir, controller_key(id), on_disk, err) && on_disk.has_trim && on_disk.trim_dps == 0);
        CHECK(dist_dps(on_disk.bias, truth) < 0.1);          // de bewaarde bias is ongemoeid
        // Herverbinding van dezelfde pijplijn: een al geleerde trim wordt niet door het bestand overschreven.
        p.set_trim(0.1, 2);
        p.note_recenter_press();
        BiasPersistence bp2;
        bp2.set_dir(dir);
        bp2.on_connect(p, id, 0, unix_s + 300, &info);
        CHECK_NEAR(p.trim().rate_dps(), 0.1, 1e-12);
    }
    ::unlink((dir + "/" + controller_key(id) + ".json").c_str());
    ::rmdir(dir.c_str());
    ::rmdir(d.c_str());
}

// ---------------- Opslag ----------------

static void test_store() {
    std::string d = tmpdir();
    CHECK(!d.empty());
    std::string dir = d + "/gyro-bias";   // bestaat nog niet: wordt aangemaakt
    SavedBias b;
    b.bias = dps(1.5, -2.25, 0.875);
    b.tc = 2050;
    b.unix_s = 1791480934;
    b.key = "37b2de54e4ef26ba";
    b.source = "quasi-still";
    std::string err;
    CHECK(save_bias(dir, b, err));
    SavedBias l;
    CHECK(load_bias(dir, b.key, l, err));
    CHECK_NEAR(dist_dps(l.bias, b.bias), 0, 1e-9);
    CHECK(l.tc == 2050 && l.unix_s == 1791480934 && l.source == "quasi-still");
    struct stat st{};
    CHECK(::stat((dir + "/" + b.key + ".json").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    // Geen tijdelijke bestanden over.
    CHECK(::stat((dir + "/" + b.key + ".json.tmp." + std::to_string(::getpid())).c_str(), &st) != 0);
    // Onbekende controller, beschadigd, buiten bereik, verkeerd formaat.
    CHECK(!load_bias(dir, "0000000000000000", l, err) && err == "ontbreekt");
    CHECK(!bias_from_json("{", l, err));
    CHECK(!bias_from_json("{\"format\":\"ravengun-gyro-bias\",\"version\":1,\"bias_deg_s\":[1,2,99]}", l, err));
    CHECK(!bias_from_json("{\"format\":\"ravengun-gyro-bias\",\"version\":1,\"bias_deg_s\":[1,2]}", l, err));
    CHECK(!bias_from_json("{\"format\":\"iets-anders\",\"version\":1,\"bias_deg_s\":[1,2,3]}", l, err));
    CHECK(!bias_from_json("{\"format\":\"ravengun-gyro-bias\",\"version\":2,\"bias_deg_s\":[1,2,3]}", l, err));
    CHECK(bias_from_json("{\"format\":\"ravengun-gyro-bias\",\"version\":1,\"bias_deg_s\":[1,2,3]}", l, err));
    // Sleutel in het bestand wijkt af van de bestandsnaam.
    SavedBias o = b;
    o.key = "aaaaaaaaaaaaaaaa";
    CHECK(save_bias(dir, o, err));
    CHECK(::rename((dir + "/aaaaaaaaaaaaaaaa.json").c_str(), (dir + "/bbbbbbbbbbbbbbbb.json").c_str()) == 0);
    CHECK(!load_bias(dir, "bbbbbbbbbbbbbbbb", l, err));
    CHECK(!save_bias("", b, err));
    // Opruimen.
    for (const char* f : {"/37b2de54e4ef26ba.json", "/bbbbbbbbbbbbbbbb.json"}) ::unlink((dir + f).c_str());
    ::rmdir(dir.c_str());
    ::rmdir(d.c_str());
}

static void test_saver_policy() {
    // Meet alleen echte metingen; de eerste keer ruim na het verbinden; daarna op verschil en tijd.
    Hand h;
    h.bias = dps(1.5, -2.2, 0.9);
    GyroBiasEstimator e;
    BiasSaver s;
    CHECK(!s.due(e, 100));                 // niets gemeten
    e.restore(h.bias, 2000);
    CHECK(!s.due(e, 100));                 // alleen hersteld: niet terugschrijven
    CHECK(!s.due_final(e));
    s.connected(0);
    for (int i = 0; i < static_cast<int>(8.0 / kSub); ++i) {
        Vec3 g, a;
        h.sample({}, kSub, g, a);
        e.update(g, a, kSub);
    }
    CHECK(e.state() == BiasState::Estimated);
    CHECK(s.due(e, 8));                    // eerste meting (3 s stilstand genoeg)
    CHECK(s.due_final(e));
    s.saved(e, 8);
    CHECK(!s.due(e, 9));
    CHECK(!s.due_final(e));                // niets veranderd
    // Verschil van 0,3 graden/s: pas na 30 s.
    Vec3 b = e.bias();
    GyroBiasEstimator e2;
    e2.restore(b + dps(0, 0, 0.3), -1);
    for (int i = 0; i < static_cast<int>(20.0 / kSub); ++i) {
        Vec3 g, a;
        Hand h2;
        h2.bias = b + dps(0, 0, 0.3);
        h2.rng.s = 1234 + static_cast<uint64_t>(i);
        h2.sample({}, kSub, g, a);
        e2.update(g, a, kSub);
    }
    CHECK(e2.state() == BiasState::Estimated);
    CHECK(s.due(e2, 8 + 40));
    CHECK(!s.due(e2, 8 + 10));
}

// Het hele pad van de daemon zonder hardware: sessie 1 meet en bewaart, sessie 2 herstelt. Alleen in een tijdelijke map.
static void test_persistence_cycle() {
    std::string d = tmpdir();
    CHECK(!d.empty());
    const std::string dir = d + "/gyro-bias";
    const std::string id = "aa:bb:cc:dd:ee:ff";
    Vec3 truth = dps(1.5, -2.2, 0.9);
    int64_t unix_s = 1791480934;
    {
        PipeRun r;
        r.h.bias = truth;
        r.tc = 2050;
        BiasPersistence bp;
        bp.set_dir(dir);
        std::string info;
        CHECK(!bp.on_connect(r.p, id, 0, unix_s, &info));     // niets te laden
        CHECK(!bp.loaded() && info.empty());
        int saves_seen = 0;
        for (double t = 0; t < 40; t += 0.01127) {
            r.step(0);
            if (bp.on_report(r.p, r.t, unix_s + static_cast<int64_t>(r.t), &info)) ++saves_seen;
        }
        CHECK(saves_seen == 1);                                // eenmaal, niet bij elk rapport
        CHECK(bp.saves() == 1 && bp.error().empty());
        bp.on_disconnect(r.p, r.t, unix_s + 40, &info);        // bij het verbreken nog de verfijnde waarde, hooguit eenmaal extra
        CHECK(bp.saves() <= 2);
        CHECK(!bp.on_disconnect(r.p, r.t, unix_s + 41, &info));   // en daarna niets meer
        SavedBias on_disk;
        std::string err;
        CHECK(load_bias(dir, controller_key(id), on_disk, err) && dist_dps(on_disk.bias, truth) < 0.1 && on_disk.tc == 2050);
    }
    {
        // Sessie 2: dezelfde controller krijgt de bewaarde waarde als beginwaarde.
        Pipeline p;
        p.on_connect(id, 1'000'000'000);
        BiasPersistence bp;
        bp.set_dir(dir);
        std::string info;
        CHECK(bp.on_connect(p, id, 0, unix_s + 3600, &info));
        CHECK(info.find("hersteld") != std::string::npos && info.find("1.0 uur") != std::string::npos);
        CHECK(p.bias().state() == BiasState::Restored && p.bias().source() == BiasSource::Restored);
        CHECK(dist_dps(p.bias().bias(), truth) < 0.2);
        CHECK(bp.loaded() && bp.loaded_value().tc == 2050);
        // Een herstelde, onveranderde waarde wordt niet teruggeschreven.
        CHECK(!bp.on_disconnect(p, 100, unix_s + 3700, &info) && bp.saves() == 0);
        // Een andere controller vindt niets.
        Pipeline p3;
        p3.on_connect("11:22:33:44:55:66", 1'000'000'000);
        BiasPersistence b3;
        b3.set_dir(dir);
        CHECK(!b3.on_connect(p3, "11:22:33:44:55:66", 0, unix_s, &info) && !b3.loaded());
        CHECK(p3.bias().state() == BiasState::None);
        // Uit (lege map): niets laden, niets schrijven.
        Pipeline p4;
        p4.on_connect(id, 1'000'000'000);
        BiasPersistence off;
        CHECK(!off.enabled() && !off.on_connect(p4, id, 0, unix_s, &info) && p4.bias().state() == BiasState::None);
        // Beschadigd bestand: genegeerd met een waarschuwing, geen herstel.
        FILE* f = std::fopen((dir + "/" + controller_key(id) + ".json").c_str(), "w");
        std::fputs("{\"format\":\"ravengun-gyro-bias\",\"version\":1,\"bias_deg_s\":[1,2,999]}", f);
        std::fclose(f);
        Pipeline p5;
        p5.on_connect(id, 1'000'000'000);
        BiasPersistence b5;
        b5.set_dir(dir);
        info.clear();
        CHECK(!b5.on_connect(p5, id, 0, unix_s, &info) && p5.bias().state() == BiasState::None && info.find("niet gebruikt") != std::string::npos);
    }
    // Schrijven in een map waar dat niet kan: een fout, geen herhaling bij elk rapport.
    {
        PipeRun r;
        r.h.bias = truth;
        BiasPersistence bp;
        bp.set_dir("/proc/ravengun-bestaat-niet/gyro-bias");
        std::string info;
        bp.on_connect(r.p, id, 0, unix_s, &info);
        int reports_with_message = 0;
        for (double t = 0; t < 40; t += 0.01127) {
            r.step(0);
            std::string i2;
            bp.on_report(r.p, r.t, unix_s, &i2);
            if (!i2.empty()) ++reports_with_message;
        }
        CHECK(!bp.error().empty() && reports_with_message == 1 && bp.saves() == 0);
    }
    for (const auto& e : std::vector<std::string>{controller_key(id) + ".json"}) ::unlink((dir + "/" + e).c_str());
    ::rmdir(dir.c_str());
    ::rmdir(d.c_str());
}

// ---------------- Replay en driftmeting ----------------

static void test_replay_bias_init_and_mode() {
    // Opname van 20 s met bias en rust: legacy en new geven hetzelfde; een beginbias komt als "restored" terug.
    std::vector<rec::Record> recs;
    rec::Record h;
    h.kind = rec::Kind::Header;
    h.header.t0_ns = 1'000'000'000;
    recs.push_back(h);
    Hand hand;
    hand.bias = dps(1.5, -2.2, 0.9);
    for (int k = 1; k < 1800; ++k) {
        rec::Record x;
        x.kind = rec::Kind::Report;
        RawReport& r = x.report;
        r.subsamples = 2;
        for (int i = 0; i < 2; ++i) hand.sample({}, 0.01127 / 2, r.gyro[i], r.accel[i]);
        r.t_rx_ns = h.header.t0_ns + static_cast<int64_t>(k * 0.01127 * 1e9);
        r.temperature_raw = 2000;
        recs.push_back(x);
    }
    rec::DriftOptions o;
    rec::DriftResult nw = rec::compute_drift(recs, o);
    o.bias.mode = BiasMode::Legacy;
    rec::DriftResult lg = rec::compute_drift(recs, o);
    CHECK(nw.ok && lg.ok);
    CHECK_NEAR(dist_dps(nw.final_bias, lg.final_bias), 0, 1e-9);   // rust: identiek gedrag
    CHECK(nw.final_state == BiasState::Estimated);
    // Beginbias zonder rust: de eindtoestand blijft "restored" als er nooit stilte is.
    rec::ReplayOptions ro;
    ro.have_bias_init = true;
    ro.bias_init = dps(1.0, 1.0, 1.0);
    ro.pipeline.bias.mode = BiasMode::New;
    std::vector<rec::Record> moving = recs;
    for (auto& x : moving)
        if (x.kind == rec::Kind::Report)
            for (int i = 0; i < 2; ++i) x.report.gyro[i] = x.report.gyro[i] + dps(0, 0, 40.0 * std::sin(x.report.t_rx_ns * 1e-8));
    rec::ReplayResult rr = rec::replay_records(moving, ro);
    CHECK(rr.ok);
    CHECK(rr.last.bias_state == BiasState::Restored);
    CHECK_NEAR(dist_dps(rr.last.gyro_bias, dps(1.0, 1.0, 1.0)), 0, 1e-9);
}

int main() {
    test_bias_found_in_hand();
    test_bias_quasi_only();
    test_restored_and_gates();
    test_slow_turn_not_bias();
    test_temperature_comp();
    test_quasi_disabled_by_mag();
    test_turn_then_hold_pipeline();
    test_no_jump_on_bias_update();
    test_pipeline_restore_and_json();
    test_optical_corrects_wrong_bias();
    test_optical_moving_ball_no_evidence();
    test_optical_pipeline();
    test_trim_rules();
    test_trim_learns_constant_error();
    test_trim_outlier_press();
    test_trim_pipeline_rules();
    test_store();
    test_saver_policy();
    test_persistence_cycle();
    test_trim_persistence();
    test_replay_bias_init_and_mode();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
