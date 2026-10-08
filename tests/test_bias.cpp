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
    test_store();
    test_saver_policy();
    test_persistence_cycle();
    test_replay_bias_init_and_mode();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
