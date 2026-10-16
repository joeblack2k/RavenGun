// Tests voor fase 4: assenconventie van gyro, accelerometer en magnetometer, de yaw-correctie met de
// magnetometer (mahony_mag) en de vergelijking met Mahony IMU-only, op synthetische sporen met bekende
// rotaties, gyro-bias, verloren rapporten (Bluetooth) en magnetische verstoringen. Geen hardware.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/mag_yaw.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"

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
    uint64_t s = 123456789;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }
};

// ---------------- Synthetisch spoor ----------------
//
// De "waarheid" is q_wb(t). Daaruit: gyro (hoeksnelheid in B over elk half rapportinterval, plus bias en
// ruis), accelerometer (omhoog in B plus ruis) en de RUWE magnetometer zoals de ZCM1 hem geeft:
// ruw = diag(-1,+1,-1) m_B · veld + ruis (de gemeten conventie, zie magnetometer.hpp). De Move maakt
// elke 11,27 ms een rapport met twee subsamples; een deel van de rapporten gaat verloren (Bluetooth),
// en de stempel loopt dan gewoon door.
struct Sim {
    double period = 0.01127;
    double field = 174, dip_deg = 60, field_heading_deg = 30;  // wereldrichting van het horizontale veld
    double still_s = 5;            // eerst stil (biasschatter)
    Vec3 bias_dps{1.5, -2.4, 0.9}; // gyro-bias (graden/s)
    Vec3 bias_extra_dps{0, 0, 0};  // extra bias na de stilte (hand vs tafel, temperatuur)
    double gyro_noise_dps = 0.5, accel_noise = 0.005, mag_noise = 3.8;
    double drop_p = 0.13;          // ~13 % verloren rapporten (gemeten)
    int max_run = 3;               // tot 4 perioden gat (~45 ms)
    std::function<Quat(double)> truth;
    // Verstoring: extra veld (fractie van het veld, wereldstelsel) in [t0, t1).
    double dist_t0 = -1, dist_t1 = -1;
    Vec3 dist_w{0, 0, 0};
    Lcg rng;

    Vec3 field_w(double t) const {
        double d = dip_deg * kRadPerDeg, h = field_heading_deg * kRadPerDeg;
        Vec3 m{std::cos(d) * std::sin(h), std::cos(d) * std::cos(h), -std::sin(d)};
        if (t >= dist_t0 && t < dist_t1) m = m + dist_w;
        return m;
    }
    static Vec3 body_rate(Quat a, Quat b, double dt) {
        Quat d = normalized(conj(a) * b);
        if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z};
        double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (s < 1e-15) return {};
        double ang = 2 * std::atan2(s, d.w);
        return Vec3{d.x / s, d.y / s, d.z / s} * (ang / dt);
    }
    struct Out { RawReport r; Quat q_true; double t; };
    // Alle rapporten (met verlies); t in seconden vanaf 0.
    std::vector<Out> run(double dur) {
        std::vector<Out> out;
        int n = static_cast<int>(dur / period);
        int run_left = 0;
        for (int k = 1; k <= n; ++k) {
            double t = k * period;
            bool drop = false;
            if (run_left > 0) { drop = true; --run_left; }
            else if (rng.uniform() < drop_p) { drop = true; run_left = static_cast<int>(rng.uniform() * max_run); }
            if (drop) continue;
            RawReport r;
            r.subsamples = 2;
            Vec3 b = bias_dps * kRadPerDeg;
            if (t > still_s) b = b + bias_extra_dps * kRadPerDeg;
            for (int i = 0; i < 2; ++i) {
                double ts = t - (1 - i) * period / 2;
                Quat q0 = truth(ts - period / 2), q1 = truth(ts);
                Vec3 w = body_rate(q0, q1, period / 2);
                r.gyro[i] = w + b + Vec3{rng.gauss(), rng.gauss(), rng.gauss()} * (gyro_noise_dps * kRadPerDeg);
                r.accel[i] = rotate(conj(q1), Vec3{0, 0, 1}) + Vec3{rng.gauss(), rng.gauss(), rng.gauss()} * accel_noise;
            }
            Quat q = truth(t);
            Vec3 mb = rotate(conj(q), field_w(t));
            Vec3 raw{-mb.x, mb.y, -mb.z};
            for (int a = 0; a < 3; ++a) {
                double v = (a == 0 ? raw.x : a == 1 ? raw.y : raw.z) * field + rng.gauss() * mag_noise;
                r.mag_raw[a] = static_cast<int>(std::lround(v));
            }
            r.has_stamp = true;
            r.stamp = static_cast<uint16_t>(static_cast<int64_t>(std::llround(t * kMoveTicksPerSecond)) & 0xFFFF);
            r.hid_seq = k % 16;
            r.t_rx_ns = static_cast<int64_t>((t + 0.003 + rng.uniform() * 0.015) * 1e9) + 1'000'000'000;
            out.push_back({r, q, t});
        }
        // t_rx moet monotoon zijn (jitter kan anders inhalen).
        for (size_t i = 1; i < out.size(); ++i)
            if (out[i].r.t_rx_ns <= out[i - 1].r.t_rx_ns) out[i].r.t_rx_ns = out[i - 1].r.t_rx_ns + 1000;
        return out;
    }
};

static MagCalibration ideal_cal(double field, double dip_deg) {
    MagCalibration c;
    c.kind = MagFitKind::Full;
    c.offset = {0, 0, 0};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) c.soft.m[i][j] = i == j ? 1.0 / field : 0.0;
    c.y_flip = true;
    c.negate = true;
    c.field_counts = field;
    c.dip_ref_deg = dip_deg;
    return c;
}

// Richten met de hand: een basisstand (loop horizontaal naar wereld +Y, yaw0), daarna zwaaien.
static Quat aim_pose(double yaw_deg, double pitch_deg, double roll_deg = 0) {
    return from_axis_angle({0, 0, 1}, -yaw_deg * kRadPerDeg) * from_axis_angle({1, 0, 0}, pitch_deg * kRadPerDeg) *
           from_axis_angle({0, 1, 0}, roll_deg * kRadPerDeg);
}

static double yaw_deg_of(Quat q) { return world_angles(q).yaw_rad * kDegPerRad; }

struct RunResult {
    double final_err_deg = 0;      // |yaw-fout| aan het eind (t.o.v. de fout aan het eind van de stilte)
    double max_err_deg = 0;        // max |yaw-fout| na de stilte
    double max_corr_step_deg = 0;  // grootste yaw-correctie in één rapport
    double max_still_rate = 0;     // grootste correctiesnelheid bij stilstand (graden/s)
    double max_rate = 0;
    double corr_in_dist_deg = 0;   // totale correctie tijdens de verstoring
    bool saw_disturbed = false;
    MagYawStatus st;
};

static RunResult run_pipeline(Sim& sim, double dur, FusionMode mode, bool with_cal, double settle_s = 30) {
    PipelineConfig pc;
    pc.fusion_mode = mode;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    if (with_cal) p.set_mag_calibration(ideal_cal(sim.field, sim.dip_deg));
    auto rs = sim.run(dur);
    RunResult res;
    bool have_ref = false;
    double ref = 0, corr_at_d0 = 0;
    bool in_dist = false;
    for (const auto& o : rs) {
        double total_before = p.mag().status().total_deg;
        InputSample s = p.on_report(o.r);
        double step = s.yaw_corr_total_deg - total_before;
        res.max_corr_step_deg = std::max(res.max_corr_step_deg, std::fabs(step));
        res.max_rate = std::max(res.max_rate, std::fabs(s.yaw_corr_rate_deg_s));
        Vec3 g = o.r.gyro[1] - p.bias().bias();
        if (norm(g) * kDegPerRad < 2.0) res.max_still_rate = std::max(res.max_still_rate, std::fabs(s.yaw_corr_rate_deg_s));
        if (std::string(s.mag_state) == "disturbed") res.saw_disturbed = true;
        if (o.t >= sim.dist_t0 && o.t < sim.dist_t1) {
            if (!in_dist) { in_dist = true; corr_at_d0 = s.yaw_corr_total_deg; }
            res.corr_in_dist_deg = std::fabs(s.yaw_corr_total_deg - corr_at_d0);
        }
        double err = wrap_pi((yaw_deg_of(s.q_wb) - yaw_deg_of(o.q_true)) * kRadPerDeg) * kDegPerRad;
        // Referentie: het moment van de magnetometervergrendeling (daarna houdt de magnetometer de yaw op die
        // wereld vast), zonder kalibratie het einde van de stilte. Fouten worden pas na de stilte geteld.
        bool ref_now = with_cal ? p.mag().status().locked : o.t >= sim.still_s;
        if (!have_ref && ref_now) { ref = err; have_ref = true; }
        if (have_ref && o.t >= sim.still_s) {
            double e = wrap_pi((err - ref) * kRadPerDeg) * kDegPerRad;
            if (o.t > sim.still_s + settle_s) res.max_err_deg = std::max(res.max_err_deg, std::fabs(e));
            res.final_err_deg = std::fabs(e);
            if (std::getenv("RG_DEBUG") && static_cast<int>(o.t * 88.7) % 443 == 0)
                std::printf("  t %6.1f err %+6.2f pend %+6.2f total %+6.2f bv %+.3f drift %+.3f state %s rs %u rate %.2f used %llu rej %llu\n", o.t, e,
                            s.yaw_pending_deg, s.yaw_corr_total_deg, s.yaw_vertical_bias_deg_s, p.mag().status().drift_dps, s.mag_state, p.mag().status().last.reasons, p.mag().status().last.rate_err_deg_s,
                            static_cast<unsigned long long>(p.mag().status().used_samples), static_cast<unsigned long long>(p.mag().status().innov_rejected));
        }
    }
    res.st = p.mag().status();
    return res;
}

// ---------------- Tests ----------------

// Assenconventie: gyro, accelerometer en magnetometer in één stelsel B (rechtshandig).
//  - een wereldvast vector v_B (zwaartekracht, veld) draait in B volgens dv/dt = -ω × v;
//  - de ruwe magnetometer gaat met diag(-1,+1,-1) naar B (mag_apply met y_flip+negate);
//  - de dip is positief (veld wijst omlaag, Nederland).
static void test_axis_convention() {
    // Rotatie om een schuine as: integreer v met de gyro en vergelijk met de waarheid.
    Quat q0 = aim_pose(10, 5, 3);
    Vec3 axis{0.3, -0.5, 0.8};
    double rate = 90 * kRadPerDeg, dt = 0.005;
    Vec3 g_b = rotate(conj(q0), Vec3{0, 0, 1});
    Vec3 v = g_b;
    Quat q = q0;
    for (int i = 0; i < 200; ++i) {
        Quat dq = from_axis_angle(axis, rate * dt);  // rotatie in B (rechtsvermenigvuldiging)
        Vec3 w = axis * (rate / norm(axis));
        // dv/dt = -ω × v
        Vec3 k1 = cross(w, v) * -1.0;
        Vec3 vm = v + k1 * (dt / 2);
        Vec3 k2 = cross(w, vm) * -1.0;
        v = v + k2 * dt;
        q = q * dq;
    }
    Vec3 truth = rotate(conj(q), Vec3{0, 0, 1});
    CHECK(norm(v - truth) < 1e-3);
    // Pipeline-integratie en OrientationFilter gebruiken hetzelfde: q <- q ⊗ exp(ω dt/2).
    OrientationFilter f(FusionConfig{0.0, 0.15, 0.25});
    f.init_from_accel(rotate(conj(q0), Vec3{0, 0, 1}));
    Quat qf0 = f.q();
    for (int i = 0; i < 200; ++i) f.update(axis * (rate / norm(axis)), {0, 0, 0}, dt);
    Quat rel = conj(qf0) * f.q(), rel_true = conj(q0) * q;
    CHECK(angle_between(rel, rel_true) < 1e-6);

    // Magnetometer: ruw = diag(-1,+1,-1) m_B (zoals op de ZCM1 gemeten). mag_apply geeft m_B terug.
    MagCalibration c = ideal_cal(174, 60);
    double d = 60 * kRadPerDeg;
    Vec3 mw{0, std::cos(d), -std::sin(d)};
    Vec3 mb = rotate(conj(q0), mw);
    Vec3 raw{-mb.x * 174, mb.y * 174, -mb.z * 174};
    Vec3 back = mag_apply(c, raw);
    CHECK(norm(back - mb) < 1e-9);
    CHECK_NEAR(dip_deg(back, rotate(conj(q0), Vec3{0, 0, 1})), 60, 1e-6);
    // De oude conventie (alleen y_flip) geeft hetzelfde veld met omgekeerd teken: dip -60.
    MagCalibration old = c;
    old.negate = false;
    CHECK_NEAR(dip_deg(mag_apply(old, raw), rotate(conj(q0), Vec3{0, 0, 1})), -60, 1e-6);

    // Gyro-consistentie van de magnetometer: bij rotatie om de verticaal accepteert de detector de juiste
    // afbeelding, en wijst hij een gespiegelde afbeelding (Y niet omgedraaid) af op "rate".
    auto rate_check = [&](bool flip_ok) {
        MagCalibration cc = c;
        if (!flip_ok) cc.y_flip = false;
        MagDisturbanceDetector det;
        det.set_reference_dip(60);
        double yaw = 0, wz = 30 * kRadPerDeg, step = 0.0113;
        MagCheck chk;
        int rate_rej = 0;
        for (int i = 0; i < 400; ++i) {
            Quat qq = from_axis_angle({0, 0, 1}, yaw) * q0;
            Vec3 m_b = rotate(conj(qq), mw);
            Vec3 rw{-m_b.x * 174, m_b.y * 174, -m_b.z * 174};
            chk = det.update(mag_apply(cc, rw), rotate(conj(qq), Vec3{0, 0, 1}), rotate(conj(qq), Vec3{0, 0, wz}), step);
            if (chk.reasons & kRejectRate) ++rate_rej;
            yaw += wz * step;
        }
        return rate_rej;
    };
    CHECK(rate_check(true) == 0);
    CHECK(rate_check(false) > 100);
}

static void test_bias_drift_and_mag_correction() {
    // 0..5 s stil, daarna 120 s richten (zwaaien ±15° yaw, ±5° pitch, langzaam pannen), met een extra
    // verticale bias van 0,15 graden/s die de stilstandsschatter niet kent (gemeten verschil hand/tafel).
    auto make = [] {
        Sim s;
        s.bias_extra_dps = {0, 0, 0.15};
        s.truth = [st = s.still_s](double t) {
            if (t < st) return aim_pose(0, 0);
            double u = t - st;
            double yaw = 15 * std::sin(2 * kPi * 0.25 * u) + 8 * std::sin(2 * kPi * 0.03 * u);
            double pitch = 5 * std::sin(2 * kPi * 0.17 * u);
            return aim_pose(yaw, pitch, 2);
        };
        return s;
    };
    Sim a = make();
    RunResult imu = run_pipeline(a, 125, FusionMode::Mahony, true);
    Sim b = make();
    RunResult mag = run_pipeline(b, 125, FusionMode::MahonyMag, true);
    std::printf("synthetisch 120 s richten, extra bias 0,15 graden/s: mahony eindfout %.2f, mahony_mag eindfout %.2f (max %.2f na 30 s), b_v %.3f graden/s, max stap %.4f graden\n",
                imu.final_err_deg, mag.final_err_deg, mag.max_err_deg, mag.st.vertical_bias_dps, mag.max_corr_step_deg);
    CHECK(imu.final_err_deg > 10);  // verwacht ~0,15 * 120 = 18 graden
    CHECK(mag.final_err_deg < 1.0);
    CHECK(mag.max_err_deg < 2.0);
    CHECK_NEAR(mag.st.vertical_bias_dps, 0.15, 0.05);  // de verticale bias is geleerd
    CHECK(mag.st.locked);
    CHECK(mag.st.used_samples > 5000);
    // Geen sprongen: per rapport (max ~56 ms bij verlies) hooguit 0,2 graden/s.
    CHECK(mag.max_rate <= 0.2 + 1e-9);
    CHECK(mag.max_corr_step_deg <= 0.2 * 0.12);  // langste gat in het spoor ~0,1 s
    // In de schaduw (mahony) wordt niets gecorrigeerd maar wel gemeten.
    CHECK(imu.st.total_deg == 0);
    CHECK(imu.st.locked);
    CHECK(std::fabs(imu.st.pending_deg) > 10);
    // Zonder kalibratie: mahony_mag valt terug op IMU-only (geen magnetometer).
    Sim c = make();
    RunResult nocal = run_pipeline(c, 125, FusionMode::MahonyMag, false);
    CHECK(nocal.final_err_deg > 10);
    CHECK(!nocal.st.calibrated);
}

static void test_still_rate_limit() {
    // Stil, met na 20 s een veld dat 3 graden verspringt (na de vergrendeling). De hoekcorrectie blijft in
    // stilstand onder 0,02 graden/s; de bias b_v verandert hooguit 0,01 graden/s per s en blijft binnen
    // bias_max: de cursorsnelheid door de magnetometer is dus altijd begrensd en verloopt zonder sprong.
    Sim s;
    s.drop_p = 0;
    s.truth = [](double) { return aim_pose(0, 0); };
    PipelineConfig pc;
    pc.fusion_mode = FusionMode::MahonyMag;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
    double max_rate = 0, max_step = 0, max_bv_slew = 0, max_bv = 0;
    for (int seg = 0; seg < 2; ++seg) {
        if (seg == 1) s.field_heading_deg += 3;  // na 20 s: veld 3 graden anders (stap)
        auto rs = s.run(20);
        for (auto& o : rs) {
            o.r.t_rx_ns += seg * 20'000'000'000LL;
            o.r.stamp = static_cast<uint16_t>(o.r.stamp + seg * 2'000'000);
            double before = p.mag().status().total_deg, bv0 = p.mag().status().vertical_bias_dps;
            InputSample smp = p.on_report(o.r);
            max_step = std::max(max_step, std::fabs(smp.yaw_corr_total_deg - before));
            max_rate = std::max(max_rate, std::fabs(smp.yaw_corr_rate_deg_s));
            max_bv_slew = std::max(max_bv_slew, std::fabs(smp.yaw_vertical_bias_deg_s - bv0) / s.period);
            max_bv = std::max(max_bv, std::fabs(smp.yaw_vertical_bias_deg_s));
        }
    }
    std::printf("stilstand, veld 3 graden verspringend: max hoekcorrectie %.4f graden/s, max stap %.6f graden, b_v max %.3f graden/s "
                "(verandering max %.4f graden/s2), open %.2f graden\n", max_rate, max_step, max_bv, max_bv_slew, p.mag().status().pending_deg);
    CHECK(max_rate <= 0.02 + 1e-9);
    CHECK(max_step <= 0.02 * 0.02);
    CHECK(max_bv_slew <= 0.01 * 1.7);  // per rapport gemeten; de stempel-dt varieert
    CHECK(max_bv <= 0.3 + 1e-9);
}

static void test_disturbance_rejected() {
    // Een magneet (extra veld van 25 %) tussen 40 en 50 s: afgewezen, geen correctie in die tijd, en de
    // yaw blijft daarna goed.
    Sim s;
    s.bias_extra_dps = {0, 0, 0.05};
    s.dist_t0 = 40;
    s.dist_t1 = 50;
    s.dist_w = {0.2, 0.1, 0.1};
    s.truth = [st = s.still_s](double t) {
        if (t < st) return aim_pose(0, 0);
        double u = t - st;
        return aim_pose(10 * std::sin(2 * kPi * 0.2 * u), 3 * std::sin(2 * kPi * 0.13 * u));
    };
    RunResult r = run_pipeline(s, 80, FusionMode::MahonyMag, true);
    std::printf("verstoring 40-50 s: correctie tijdens verstoring %.3f graden, eindfout %.2f, verstoord gezien %d\n", r.corr_in_dist_deg,
                r.final_err_deg, r.saw_disturbed ? 1 : 0);
    CHECK(r.saw_disturbed);
    CHECK(r.corr_in_dist_deg < 0.3);  // hooguit de eerste fractie van een seconde vóór de afwijzing
    CHECK(r.final_err_deg < 1.0);
}

static void test_heading_wrap() {
    // Veld wijst in de wereld naar ~180 graden: φ springt rond ±π; geen valse correcties.
    Sim s;
    s.field_heading_deg = 179.5;
    s.bias_extra_dps = {0, 0, 0.1};
    s.truth = [st = s.still_s](double t) {
        if (t < st) return aim_pose(0, 0);
        return aim_pose(5 * std::sin(2 * kPi * 0.3 * (t - st)), 0);
    };
    RunResult r = run_pipeline(s, 90, FusionMode::MahonyMag, true);
    CHECK(r.final_err_deg < 1.0);
    CHECK(r.st.innov_rejected < 50);
}

static void test_dropped_reports_dt() {
    // Zelfde beweging met en zonder 13 % verlies: met de stempel-dt blijft de yaw (zonder magnetometer)
    // vrijwel gelijk; verloren rapporten geven geen integratiefout van betekenis.
    auto make = [](double drop) {
        Sim s;
        s.drop_p = drop;
        s.gyro_noise_dps = 0;
        s.truth = [st = s.still_s](double t) {
            if (t < st) return aim_pose(0, 0);
            return aim_pose(20 * std::sin(2 * kPi * 0.5 * (t - st)), 5 * std::sin(2 * kPi * 0.3 * (t - st)));
        };
        return s;
    };
    Sim a = make(0.0), b = make(0.13);
    RunResult ra = run_pipeline(a, 35, FusionMode::Mahony, false, 0);
    RunResult rb = run_pipeline(b, 35, FusionMode::Mahony, false, 0);
    std::printf("verloren rapporten: max yaw-fout zonder verlies %.3f, met 13 %% verlies %.3f graden (30 s zwaaien)\n", ra.max_err_deg,
                rb.max_err_deg);
    CHECK(ra.max_err_deg < 0.5);
    CHECK(rb.max_err_deg < 0.8);
}

static void test_interrupt_reacquire() {
    // Een gat van 400 ms (niet geïntegreerd) terwijl de speler 20 graden draait: daarna brengt de
    // magnetometer de yaw glad (max 5 graden/s) terug, zonder sprong.
    Sim s;
    s.drop_p = 0;
    s.truth = [](double t) { return aim_pose(t < 30 ? 0 : 20, 0); };
    PipelineConfig pc;
    pc.fusion_mode = FusionMode::MahonyMag;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
    auto rs = s.run(50);
    double max_step = 0, ref = 0;
    bool have = false;
    double err_end = 0;
    for (const auto& o : rs) {
        if (o.t > 29.8 && o.t < 30.2) continue;  // gat: rapporten weg, controller draait
        double before = p.mag().status().total_deg;
        InputSample smp = p.on_report(o.r);
        if (std::getenv("RG_DEBUG") && o.t > 29 && o.t < 36 && static_cast<int>(o.t * 88.7) % 20 == 0) { const auto& st = p.mag().status(); std::printf("  r t %.2f locked %d state %s used %d pend %.2f sig %.2f reacq %d rej %llu reasons %u acc %llu\n", o.t, st.locked, smp.mag_state, st.used, st.pending_deg, st.sigma_deg, st.reacquire, static_cast<unsigned long long>(st.innov_rejected), st.last.reasons, static_cast<unsigned long long>(st.accel_skipped)); }
        max_step = std::max(max_step, std::fabs(smp.yaw_corr_total_deg - before));
        double err = wrap_pi((yaw_deg_of(smp.q_wb) - yaw_deg_of(o.q_true)) * kRadPerDeg) * kDegPerRad;
        if (!have && o.t > 20) { ref = err; have = true; }
        if (have) err_end = std::fabs(wrap_pi((err - ref) * kRadPerDeg) * kDegPerRad);
    }
    std::printf("gat van 0,4 s met 20 graden draai: eindfout %.2f graden, max stap %.3f graden\n", err_end, max_step);
    CHECK(p.stats().gap_not_integrated == 1);
    CHECK(err_end < 1.0);
    CHECK(max_step <= 5.0 * 0.03);
}

static void test_mode_switch() {
    FusionMode m;
    CHECK(parse_fusion_mode("mahony", m) && m == FusionMode::Mahony);
    CHECK(parse_fusion_mode("mahony_mag", m) && m == FusionMode::MahonyMag);
    CHECK(parse_fusion_mode("mahony-mag", m) && m == FusionMode::MahonyMag);
    CHECK(!parse_fusion_mode("eskf", m));
    CHECK(std::string(to_string(FusionMode::MahonyMag)) == "mahony_mag");
    // Omschakelen zonder sprong: q verandert niet door het omschakelen zelf.
    Sim s;
    s.bias_extra_dps = {0, 0, 0.2};
    s.truth = [st = s.still_s](double t) { return t < st ? aim_pose(0, 0) : aim_pose(5 * std::sin(t), 0); };
    PipelineConfig pc;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
    auto rs = s.run(40);
    size_t half = rs.size() / 2;
    for (size_t i = 0; i < half; ++i) p.on_report(rs[i].r);
    Quat before = p.last().q_wb;
    p.set_fusion_mode(FusionMode::MahonyMag);
    CHECK(p.fusion_mode() == FusionMode::MahonyMag);
    CHECK(!p.mag().status().locked);  // nieuwe referentie op de huidige stand
    CHECK(angle_between(before, p.last().q_wb) == 0);
    InputSample smp{};
    for (size_t i = half; i < rs.size(); ++i) smp = p.on_report(rs[i].r);
    CHECK(std::string(smp.fusion_mode) == "mahony_mag");
    CHECK(std::string(smp.yaw_source) == "magnetometer");
    CHECK(std::string(smp.mag_state) == "ok");
    p.set_fusion_mode(FusionMode::Mahony);
    smp = p.on_report(rs.back().r);
    CHECK(std::string(smp.yaw_source) == "gyro");
}

int main() {
    test_axis_convention();
    test_bias_drift_and_mag_correction();
    test_still_rate_limit();
    test_disturbance_rejected();
    test_heading_wrap();
    test_dropped_reports_dt();
    test_interrupt_reacquire();
    test_mode_switch();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
