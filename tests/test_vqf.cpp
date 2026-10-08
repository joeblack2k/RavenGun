// Tests voor de VQF-modi (vqf en vqf_mag): deterministisch afspelen, statische bias, gaten (verloren rapporten),
// geen NaN, nauwkeurigheid tegen een synthetische waarheid, de magnetometer, en live omschakelen zonder sprong
// in de aim. Zelfde synthetische opzet als test_fusion.cpp (twee subsamples per rapport, 13 % verlies).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/mag_yaw.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/core/vqf_estimator.hpp"

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
    uint64_t s = 987654321;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }
};

struct Sim {
    double period = 0.01127;
    double field = 174, dip_deg = 60, field_heading_deg = 30;
    double still_s = 5;
    Vec3 bias_dps{1.5, -2.4, 0.9};
    Vec3 bias_extra_dps{0, 0, 0};
    double gyro_noise_dps = 0.5, accel_noise = 0.005, mag_noise = 3.8;
    double drop_p = 0.13;
    int max_run = 3;
    std::function<Quat(double)> truth;
    Lcg rng;

    Vec3 field_w() const {
        double d = dip_deg * kRadPerDeg, h = field_heading_deg * kRadPerDeg;
        return {std::cos(d) * std::sin(h), std::cos(d) * std::cos(h), -std::sin(d)};
    }
    static Vec3 body_rate(Quat a, Quat b, double dt) {
        Quat d = normalized(conj(a) * b);
        if (d.w < 0) d = {-d.w, -d.x, -d.y, -d.z};
        double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (s < 1e-15) return {};
        return Vec3{d.x / s, d.y / s, d.z / s} * (2 * std::atan2(s, d.w) / dt);
    }
    struct Out { RawReport r; Quat q_true; double t; };
    std::vector<Out> run(double dur) {
        std::vector<Out> out;
        const int n = static_cast<int>(dur / period);
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
                r.gyro[i] = body_rate(q0, q1, period / 2) + b + Vec3{rng.gauss(), rng.gauss(), rng.gauss()} * (gyro_noise_dps * kRadPerDeg);
                r.accel[i] = rotate(conj(q1), Vec3{0, 0, 1}) + Vec3{rng.gauss(), rng.gauss(), rng.gauss()} * accel_noise;
            }
            Quat q = truth(t);
            Vec3 mb = rotate(conj(q), field_w());
            Vec3 raw{-mb.x, mb.y, -mb.z};
            for (int a = 0; a < 3; ++a) {
                double v = (a == 0 ? raw.x : a == 1 ? raw.y : raw.z) * field + rng.gauss() * mag_noise;
                r.mag_raw[a] = static_cast<int>(std::lround(v));
            }
            r.has_stamp = true;
            r.stamp = static_cast<uint16_t>(static_cast<int64_t>(std::llround(t * kMoveTicksPerSecond)) & 0xFFFF);
            r.hid_seq = k % 16;
            r.t_rx_ns = static_cast<int64_t>(t * 1e9) + 1'000'000'000;
            out.push_back({r, q, t});
        }
        return out;
    }
};

static MagCalibration ideal_cal(double field, double dip_deg) {
    MagCalibration c;
    c.kind = MagFitKind::Full;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) c.soft.m[i][j] = i == j ? 1.0 / field : 0.0;
    c.y_flip = true;
    c.negate = true;
    c.field_counts = field;
    c.dip_ref_deg = dip_deg;
    return c;
}

static Quat aim_pose(double yaw_deg, double pitch_deg, double roll_deg = 0) {
    return from_axis_angle({0, 0, 1}, -yaw_deg * kRadPerDeg) * from_axis_angle({1, 0, 0}, pitch_deg * kRadPerDeg) *
           from_axis_angle({0, 1, 0}, roll_deg * kRadPerDeg);
}
static double yaw_deg_of(Quat q) { return world_angles(q).yaw_rad * kDegPerRad; }
static double pitch_deg_of(Quat q) { return world_angles(q).pitch_rad * kDegPerRad; }
static bool finite_q(Quat q) { return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::fabs(qnorm(q) - 1) < 1e-6; }

static std::function<Quat(double)> swing(double still_s, double extra_yaw = 0) {
    return [still_s, extra_yaw](double t) {
        if (t < still_s) return aim_pose(0, 0);
        double u = t - still_s;
        double yaw = 15 * std::sin(2 * kPi * 0.25 * u) + 8 * std::sin(2 * kPi * 0.03 * u) + extra_yaw;
        double pitch = 5 * std::sin(2 * kPi * 0.17 * u);
        return aim_pose(yaw, pitch, 2);
    };
}

static void test_parse_and_names() {
    FusionMode m;
    CHECK(parse_fusion_mode("vqf", m) && m == FusionMode::Vqf);
    CHECK(parse_fusion_mode("vqf_mag", m) && m == FusionMode::VqfMag);
    CHECK(parse_fusion_mode("vqf-mag", m) && m == FusionMode::VqfMag);
    CHECK(parse_fusion_mode("mahony", m) && m == FusionMode::Mahony);
    CHECK(!parse_fusion_mode("vqf9", m));
    CHECK(std::string(to_string(FusionMode::Vqf)) == "vqf" && std::string(to_string(FusionMode::VqfMag)) == "vqf_mag");
    CHECK(is_vqf(FusionMode::Vqf) && is_vqf(FusionMode::VqfMag) && !is_vqf(FusionMode::Mahony) && !is_vqf(FusionMode::MahonyMag));
    CHECK(PipelineConfig{}.fusion_mode == FusionMode::Mahony);  // standaard blijft mahony
    VqfBias b;
    CHECK(parse_vqf_bias("own", b) && b == VqfBias::Own && parse_vqf_bias("external", b) && b == VqfBias::External);
    CHECK(!parse_vqf_bias("both", b));
    CHECK(VqfConfig{}.bias == VqfBias::External);
}

// Dezelfde invoer + dezelfde configuratie = bit-exact dezelfde reeks oriëntaties.
static void test_deterministic() {
    for (FusionMode mode : {FusionMode::Vqf, FusionMode::VqfMag}) {
        std::vector<Quat> first;
        for (int pass = 0; pass < 2; ++pass) {
            Sim s;
            s.truth = swing(s.still_s);
            auto rs = s.run(40);
            PipelineConfig pc;
            pc.fusion_mode = mode;
            Pipeline p(pc);
            p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
            p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
            std::vector<Quat> qs;
            for (const auto& o : rs) qs.push_back(p.on_report(o.r).q_wb);
            if (pass == 0) first = qs;
            else {
                CHECK(qs.size() == first.size());
                bool same = qs.size() == first.size();
                for (size_t i = 0; same && i < qs.size(); ++i)
                    same = std::memcmp(&qs[i], &first[i], sizeof(Quat)) == 0;
                CHECK(same);
            }
        }
    }
}

// Stilliggend met een vaste bias: bias own vindt hem; bias external laat de yaw stilstaan op de pijplijnbias.
static void test_static_bias() {
    Sim s;
    s.still_s = 1e9;  // altijd stil
    s.bias_dps = {1.5, -2.4, 0.9};
    s.truth = [](double) { return aim_pose(0, 0); };
    auto rs = s.run(120);
    {
        PipelineConfig pc;
        pc.fusion_mode = FusionMode::Vqf;
        pc.vqf.bias = VqfBias::Own;
        Pipeline p(pc);
        p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
        double y60 = 0;
        for (const auto& o : rs) {
            InputSample smp = p.on_report(o.r);
            if (o.t >= 60 && y60 == 0) y60 = yaw_deg_of(smp.q_wb);
        }
        Vec3 b = p.vqf().bias();
        CHECK_NEAR(b.x * kDegPerRad, 1.5, 0.12);
        CHECK_NEAR(b.y * kDegPerRad, -2.4, 0.12);
        CHECK_NEAR(b.z * kDegPerRad, 0.9, 0.12);
        CHECK(p.vqf().rest_samples() > 0);
        CHECK(std::fabs(yaw_deg_of(p.orientation()) - y60) < 0.5);  // geen wegloop in de tweede minuut
    }
    {
        PipelineConfig pc;
        pc.fusion_mode = FusionMode::Vqf;  // bias external (standaard)
        Pipeline p(pc);
        p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
        double y60 = 0;
        for (const auto& o : rs) {
            InputSample smp = p.on_report(o.r);
            if (o.t >= 60 && y60 == 0) y60 = yaw_deg_of(smp.q_wb);
        }
        CHECK(norm(p.bias().bias() - Vec3{1.5, -2.4, 0.9} * kRadPerDeg) * kDegPerRad < 0.15);  // onze schatter vond hem
        CHECK(std::fabs(yaw_deg_of(p.orientation()) - y60) < 0.5);
        CHECK(norm(p.vqf().bias()) < 1e-12);  // VQF schat zelf niets: niet dubbel corrigeren
    }
}

// Verloren rapporten en lange gaten: nooit NaN, norm 1; en de opvulling klopt (constante draaiing blijft goed).
static void test_gaps_no_nan() {
    Sim s;
    s.drop_p = 0.45;
    s.max_run = 9;
    s.truth = swing(s.still_s);
    auto rs = s.run(60);
    for (FusionMode mode : {FusionMode::Vqf, FusionMode::VqfMag}) {
        PipelineConfig pc;
        pc.fusion_mode = mode;
        Pipeline p(pc);
        p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
        p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
        bool ok = true;
        size_t i = 0;
        for (const auto& o : rs) {
            RawReport r = o.r;
            if (++i == 1500) r.t_rx_ns += 2'000'000'000, r.stamp = static_cast<uint16_t>(r.stamp + 20000);  // gat van 0,2 s in stempeltijd
            if (i == 2500) r.stamp = static_cast<uint16_t>(r.stamp + 40000);  // gat van 0,4 s: niet geïntegreerd
            InputSample smp = p.on_report(r);
            ok = ok && finite_q(smp.q_wb) && std::isfinite(smp.x_raw) && std::isfinite(smp.y_raw);
        }
        CHECK(ok);
        CHECK(p.vqf().filled_reports() > 100);
    }
    // Rechtstreeks: NaN in de invoer wordt overgeslagen en breekt VQF niet.
    VqfEstimator v;
    RawReport r;
    r.subsamples = 2;
    r.accel[0] = r.accel[1] = {0, 0, 1};
    r.gyro[0] = r.gyro[1] = {0, 0, 0};
    for (int k = 0; k < 50; ++k) v.feed(r, 0.01127, {}, nullptr);
    RawReport bad = r;
    bad.gyro[0].x = NAN;
    v.feed(bad, 0.01127, {}, nullptr);
    for (int k = 0; k < 50; ++k) v.feed(r, 0.01127, {}, nullptr);
    CHECK(finite_q(v.q()) && v.skipped_reports() == 1);
}

// Een constante draaiing om de verticaal met verloren rapporten: de opvulling (lineaire interpolatie) en de
// werkelijke stempeltijd laten de geïntegreerde hoek kloppen.
static void test_gap_fill_angle() {
    VqfConfig cfg;
    VqfEstimator v(cfg);
    const double rate = 40 * kRadPerDeg;  // om wereld-Z (= body-Z bij accel (0,0,1))
    const double T = 0.01127;
    RawReport r;
    r.subsamples = 2;
    r.accel[0] = r.accel[1] = {0, 0, 1};
    r.gyro[0] = r.gyro[1] = {0, 0, rate};
    Lcg rng;
    double t_prev = 0, t = 0;
    v.feed(r, T, {}, nullptr);
    const double yaw0 = yaw_deg_of(v.q());
    int k = 0;
    while (t < 20) {
        ++k;
        t = k * T;
        if (k % 3 == 0 || (k % 7 == 0)) continue;  // een deel gaat verloren
        v.feed(r, t - t_prev, {}, nullptr);
        t_prev = t;
    }
    // draaiing om +Z = tegen de klok; yaw (positief naar rechts) neemt af
    const double expect = -rate * kDegPerRad * t_prev;
    CHECK_NEAR(wrap_pi((yaw_deg_of(v.q()) - yaw0 - expect) * kRadPerDeg) * kDegPerRad, 0.0, 0.2);
    CHECK(v.filled_reports() > 100);
}

struct Track {
    double final_yaw_err = 0, max_yaw_err = 0, max_pitch_err = 0;
};

// Pipeline-run tegen de waarheid; de yaw-fout wordt vanaf het eind van de stilte gerekend (de absolute yaw is arbitrair).
static Track run_track(Sim& s, double dur, FusionMode mode, bool cal, double settle = 20) {
    PipelineConfig pc;
    pc.fusion_mode = mode;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    if (cal) p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
    auto rs = s.run(dur);
    Track tr;
    bool have = false;
    double ref = 0;
    for (const auto& o : rs) {
        InputSample smp = p.on_report(o.r);
        const double e = wrap_pi((yaw_deg_of(smp.q_wb) - yaw_deg_of(o.q_true)) * kRadPerDeg) * kDegPerRad;
        if (!have && o.t >= s.still_s) { ref = e; have = true; }
        if (!have) continue;
        const double ee = wrap_pi((e - ref) * kRadPerDeg) * kDegPerRad;
        tr.final_yaw_err = ee;
        if (o.t > s.still_s + settle) tr.max_yaw_err = std::max(tr.max_yaw_err, std::fabs(ee));
        tr.max_pitch_err = std::max(tr.max_pitch_err, std::fabs(pitch_deg_of(smp.q_wb) - pitch_deg_of(o.q_true)));
    }
    return tr;
}

static void test_accuracy_and_mag() {
    // 6D: bekende bias (extern), zwaaien met 13 % verlies: yaw blijft binnen 1,5 graad over 60 s, pitch binnen 1,5.
    {
        Sim s;
        s.truth = swing(s.still_s);
        Track t = run_track(s, 65, FusionMode::Vqf, false);
        std::printf("vqf 6D, 60 s zwaaien: yaw-fout max %.2f (eind %+.2f), pitch-fout max %.2f graden\n", t.max_yaw_err, t.final_yaw_err, t.max_pitch_err);
        CHECK(t.max_yaw_err < 1.5);
        CHECK(t.max_pitch_err < 1.5);
    }
    // 9D: een extra verticale bias van 0,3 graden/s die de pijplijn niet kent: 6D loopt weg (~35 graden in 120 s),
    // 9D houdt de yaw vast aan het veld (resterende fout ~ bias * tau_mag = 2,7 graden).
    {
        Sim a;
        a.bias_extra_dps = {0, 0, 0.3};
        a.truth = swing(a.still_s);
        Track t6 = run_track(a, 125, FusionMode::Vqf, true);
        Sim b;
        b.bias_extra_dps = {0, 0, 0.3};
        b.truth = swing(b.still_s);
        Track t9 = run_track(b, 125, FusionMode::VqfMag, true);
        std::printf("extra bias 0,3 graden/s, 120 s: vqf eindfout %.1f, vqf_mag eindfout %.2f (max %.2f na 20 s)\n", std::fabs(t6.final_yaw_err), std::fabs(t9.final_yaw_err), t9.max_yaw_err);
        CHECK(std::fabs(t6.final_yaw_err) > 20);
        CHECK(std::fabs(t9.final_yaw_err) < 4.0);
        CHECK(t9.max_yaw_err < 6.0);
    }
    // Zonder kalibratie valt vqf_mag terug op 6D (geen magnetometer-update, geen NaN).
    {
        Sim s;
        s.truth = swing(s.still_s);
        Track t = run_track(s, 45, FusionMode::VqfMag, false);
        CHECK(t.max_yaw_err < 1.5);
    }
}

// Omschakelen tijdens het spelen: de aim (yaw/pitch t.o.v. het midden) springt niet.
static void test_live_switch_no_jump() {
    Sim s;
    s.bias_extra_dps = {0, 0, 0.1};
    s.truth = swing(s.still_s);
    auto rs = s.run(60);
    PipelineConfig pc;
    Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    p.set_mag_calibration(ideal_cal(s.field, s.dip_deg));
    const FusionMode order[] = {FusionMode::Vqf, FusionMode::VqfMag, FusionMode::Mahony, FusionMode::VqfMag, FusionMode::MahonyMag, FusionMode::Vqf, FusionMode::Mahony};
    size_t next = 0;
    double worst = 0;
    int switches = 0;
    size_t i = 0;
    for (const auto& o : rs) {
        p.on_report(o.r);
        ++i;
        if (i == 800) CHECK(p.recenter());  // midden op de huidige richting
        if (i > 800 && i % 450 == 0 && next < sizeof(order) / sizeof(order[0])) {
            const AimAngles before = p.aim_relative();
            const Quat q_before = p.orientation();
            if (next == 1) CHECK(std::fabs(wrap_pi(world_angles(p.vqf().q9()).yaw_rad - world_angles(q_before).yaw_rad)) * kDegPerRad > 5);  // zonder uitlijnen zou het springen
            p.set_fusion_mode(order[next++]);
            const AimAngles after = p.aim_relative();
            worst = std::max({worst, std::fabs(after.yaw_rad - before.yaw_rad) * kDegPerRad, std::fabs(after.pitch_rad - before.pitch_rad) * kDegPerRad});
            CHECK(std::fabs(wrap_pi(world_angles(p.orientation()).yaw_rad - world_angles(q_before).yaw_rad)) * kDegPerRad < 1e-6);
            ++switches;
        }
    }
    CHECK(switches == 7);
    CHECK_NEAR(worst, 0.0, 1e-6);
    CHECK(p.fusion_mode() == FusionMode::Mahony);
    // Na het terugschakelen werkt Mahony verder met een geldige oriëntatie en blijft de aim binnen het beeld.
    CHECK(finite_q(p.orientation()));
}

// Omschakelen vóór het eerste rapport (geen oriëntatie) is veilig.
static void test_switch_before_data() {
    Pipeline p;
    p.set_fusion_mode(FusionMode::Vqf);
    p.set_fusion_mode(FusionMode::VqfMag);
    p.set_fusion_mode(FusionMode::Mahony);
    CHECK(p.fusion_mode() == FusionMode::Mahony);
    CHECK(finite_q(p.orientation()));
}

int main() {
    test_parse_and_names();
    test_deterministic();
    test_static_bias();
    test_gaps_no_nan();
    test_gap_fill_angle();
    test_accuracy_and_mag();
    test_live_switch_no_jump();
    test_switch_before_data();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
