// Unittests voor de kern (wiskunde, fusie, aim, 1€-filter, knoppen, pijplijn, JSON/IPC-protocol).
// Synthetische data; geen hardware. Zelfde CHECK-stijl als test_main.cpp.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/fusion.hpp"
#include "ravengun/core/input_model.hpp"
#include "ravengun/core/one_euro.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

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

// Deterministische ruis (geen afhankelijkheid van std::random-implementaties).
struct Lcg {
    uint64_t s = 12345;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }  // ~N(0,1)
};

// ---------------- Quaternions, assen, eenheden ----------------

static void test_quaternion() {
    Quat q = from_axis_angle({0, 0, 1}, kPi / 2);
    Vec3 v = rotate(q, {1, 0, 0});
    CHECK_NEAR(v.x, 0, 1e-12); CHECK_NEAR(v.y, 1, 1e-12); CHECK_NEAR(v.z, 0, 1e-12);
    Quat i = q * conj(q);
    CHECK_NEAR(i.w, 1, 1e-12); CHECK_NEAR(i.x, 0, 1e-12);
    // Samenstelling: eerst 90 graden rond X, dan 90 graden rond Z (wereldvolgorde: qz * qx).
    Quat qx = from_axis_angle({1, 0, 0}, kPi / 2), qz = from_axis_angle({0, 0, 1}, kPi / 2);
    Vec3 w = rotate(qz * qx, {0, 1, 0});  // Y -> Z (rond X), Z blijft Z (rond Z)
    CHECK_NEAR(w.z, 1, 1e-12);
    CHECK_NEAR(qnorm(normalized(Quat{2, 0, 0, 0})), 1, 1e-12);
    CHECK_NEAR(angle_between(Quat{}, from_axis_angle({0, 1, 0}, 0.3)), 0.3, 1e-9);
    CHECK_NEAR(angle_between(q, Quat{-q.w, -q.x, -q.y, -q.z}), 0, 1e-6);  // q en -q zijn dezelfde rotatie
    CHECK_NEAR(wrap_pi(3 * kPi / 2), -kPi / 2, 1e-12);
    CHECK_NEAR(wrap_pi(-kPi), kPi, 1e-12);
    CHECK_NEAR(kDegPerRad * kRadPerDeg, 1, 1e-15);
    CHECK_NEAR(1.0 * kDegPerRad, 57.29577951308232, 1e-12);
}

static void test_axes() {
    // Richtas = controller +Y; identiteit kijkt naar wereld +Y.
    AimAngles a = world_angles(Quat{});
    CHECK_NEAR(a.yaw_rad, 0, 1e-12); CHECK_NEAR(a.pitch_rad, 0, 1e-12);
    // Naar rechts draaien = vanaf boven met de klok mee = negatieve rotatie rond wereld +Z.
    a = world_angles(from_axis_angle({0, 0, 1}, -30 * kRadPerDeg));
    CHECK_NEAR(a.yaw_rad * kDegPerRad, 30, 1e-9);
    // Omhoog richten = positieve rotatie rond +X (Y draait naar Z).
    a = world_angles(from_axis_angle({1, 0, 0}, 20 * kRadPerDeg));
    CHECK_NEAR(a.pitch_rad * kDegPerRad, 20, 1e-9);
    CHECK_NEAR(a.yaw_rad, 0, 1e-12);
    // Rol rond de richtas verandert yaw/pitch niet.
    a = world_angles(from_axis_angle({0, 1, 0}, 1.0));
    CHECK_NEAR(a.yaw_rad, 0, 1e-12); CHECK_NEAR(a.pitch_rad, 0, 1e-12);
}

static void test_units_json_sample() {
    InputSample s;
    s.gyro_rad_s = {1.0, 0, -kPi};
    s.gyro_bias = {0.5 * kRadPerDeg, 0, 0};
    s.controller_id = "00:07:04:aa:bb:cc";
    json::Writer w;
    write_sample_json(w, s, false);
    auto pr = json::parse(w.str());
    CHECK(pr.ok);
    const json::Value* g = pr.value.find("source")->find("gyro_deg_s");
    CHECK(g && g->arr.size() == 3);
    CHECK_NEAR(g->arr[0].num, 57.2957795, 1e-6);
    CHECK_NEAR(g->arr[2].num, -180, 1e-6);
    CHECK_NEAR(pr.value.find("orientation")->find("gyro_bias_deg_s")->arr[0].num, 0.5, 1e-9);
    CHECK(pr.value.find("controller_id")->str == "xx:xx:xx:xx:xx:cc");  // geredigeerd
    CHECK(pr.value.find("schema_version")->num == kInputSchemaVersion);
    CHECK(pr.value.find("aim")->find("x")->is_null());  // ongeldige aim: null, geen 0.5
    CHECK(!pr.value.find("position")->find("valid")->b);
    json::Writer w2;
    write_sample_json(w2, s, true);
    CHECK(json::parse(w2.str()).value.find("controller_id")->str == "00:07:04:aa:bb:cc");
    // Batterij: alleen de werkelijke niveaus, geen verzonnen percentages.
    CHECK(std::string(battery_info(0x03).code) == "ge60");
    CHECK(std::string(battery_info(0xEE).code) == "charging" && battery_info(0xEE).charging);
    CHECK(std::string(battery_info(42).code) == "unknown");
    CHECK(buttons_to_string(kBtnT | kBtnMove) == "move,t");
    // NaN wordt null in JSON.
    json::Writer w3;
    w3.begin_array().value(std::nan("")).value(1.5).end_array();
    CHECK(w3.str() == "[null,1.5]");
}

// ---------------- Fusie ----------------

static Vec3 up_in_body(Quat q_wb) { return rotate(conj(q_wb), {0, 0, 1}); }

static void test_fusion_converges_still() {
    // Echte houding: 30 graden gekanteld rond X. Filter begint (fout) op identiteit.
    Quat truth = from_axis_angle({1, 0, 0}, 30 * kRadPerDeg);
    Vec3 acc = up_in_body(truth);
    OrientationFilter f;
    f.init_from_accel({0, 0, 1});
    double dt = 1.0 / 154;
    for (int i = 0; i < 154 * 15; ++i) f.update({0, 0, 0}, acc, dt);
    Vec3 est = up_in_body(f.q());
    CHECK_NEAR(est.x, acc.x, 1e-3); CHECK_NEAR(est.y, acc.y, 1e-3); CHECK_NEAR(est.z, acc.z, 1e-3);
    // init_from_accel geeft direct de juiste tilt.
    OrientationFilter g;
    g.init_from_accel(acc * 1.02);
    Vec3 e2 = up_in_body(g.q());
    CHECK_NEAR(e2.y, acc.y, 1e-9); CHECK_NEAR(e2.z, acc.z, 1e-9);
    // Ongeldige dt wordt niet geïntegreerd.
    CHECK(!g.update({1, 0, 0}, acc, 0.0));
    CHECK(!g.update({1, 0, 0}, acc, 1.0));  // > max_dt_s
    CHECK(!g.update({1, 0, 0}, acc, std::nan("")));
}

static void test_fusion_rotation_one_axis() {
    // 90 graden/s rond controller-Z gedurende 1 s, controller plat (Z omhoog), variabele dt.
    for (int variant = 0; variant < 2; ++variant) {
        OrientationFilter f;
        f.init_from_accel({0, 0, 1});
        Lcg r;
        double t = 0, w = 90 * kRadPerDeg;
        while (t < 1.0 - 1e-12) {
            double dt = variant == 0 ? 1.0 / 154 : 0.002 + 0.008 * r.uniform();
            if (t + dt > 1.0) dt = 1.0 - t;
            f.update({0, 0, w}, {0, 0, 1}, dt);
            t += dt;
        }
        // Rotatie +90 graden rond +Z draait de richtas (+Y) naar -X: yaw naar links = -90.
        AimAngles a = world_angles(f.q());
        CHECK_NEAR(a.yaw_rad * kDegPerRad, -90, 0.05);
        CHECK_NEAR(a.pitch_rad * kDegPerRad, 0, 0.05);
    }
    // Rotatie rond X met correcte accelerometer (pitch 45 graden omhoog in 0.5 s).
    OrientationFilter f;
    f.init_from_accel({0, 0, 1});
    double dt = 1.0 / 154, w = 90 * kRadPerDeg;
    int n = 77;
    for (int i = 1; i <= n; ++i) {
        Quat truth = from_axis_angle({1, 0, 0}, w * dt * i);
        f.update({w, 0, 0}, up_in_body(truth), dt);
    }
    CHECK_NEAR(world_angles(f.q()).pitch_rad * kDegPerRad, 45, 0.2);
}

static void test_bias_estimation() {
    Vec3 bias{0.02, -0.035, 0.012};  // rad/s (vergelijkbaar met de gemeten ZCM1: ~1-2 graden/s)
    GyroBiasEstimator be;
    Lcg r;
    double dt = 1.0 / 154;
    CHECK(be.state() == BiasState::None);
    for (int i = 0; i < 154 * 15; ++i) {
        Vec3 g{bias.x + 0.008 * r.gauss(), bias.y + 0.008 * r.gauss(), bias.z + 0.018 * r.gauss()};
        Vec3 a{0.003 * r.gauss(), 0.003 * r.gauss(), 1.0 + 0.003 * r.gauss()};
        be.update(g, a, dt);
    }
    CHECK(be.state() == BiasState::Estimated);
    CHECK(be.still());
    CHECK_NEAR(be.bias().x, bias.x, 0.001);
    CHECK_NEAR(be.bias().y, bias.y, 0.001);
    CHECK_NEAR(be.bias().z, bias.z, 0.002);
    // Echte rotatie (0.5 rad/s) telt niet als stilstand en verandert de bias niet.
    Vec3 before = be.bias();
    for (int i = 0; i < 154 * 3; ++i) be.update({0, 0, 0.5}, {0, 0, 1}, dt);
    CHECK(!be.still());
    CHECK_NEAR(be.bias().z, before.z, 1e-12);
    // Beweging met versnelling (|a| != 1) telt niet als stilstand.
    GyroBiasEstimator b2;
    for (int i = 0; i < 154 * 3; ++i) b2.update(bias, {0, 0.5, 1.0}, dt);
    CHECK(b2.state() == BiasState::None);

    // Pijplijn: na biasschatting is de yaw-drift door de bias weg. Gyroruis is hier klein (0.002 rad/s),
    // zodat de onvermijdelijke random walk van witte ruis (sigma*sqrt(dt*T)) de toets niet domineert.
    Pipeline p;
    p.on_connect("00:00:00:00:00:01", 0);
    int64_t t = 0;
    double yaw_at_20 = 0;
    for (int i = 0; i < 77 * 80; ++i) {
        t += 13'000'000;
        RawReport rr;
        rr.t_rx_ns = t;
        rr.hid_seq = i % 16;
        for (int k = 0; k < 2; ++k) {
            rr.gyro[k] = {bias.x + 0.002 * r.gauss(), bias.y + 0.002 * r.gauss(), bias.z + 0.002 * r.gauss()};
            rr.accel[k] = {0.003 * r.gauss(), 0.003 * r.gauss(), 1.0 + 0.003 * r.gauss()};
        }
        InputSample s = p.on_report(rr);
        if (i == 77 * 20) yaw_at_20 = s.yaw_rel_deg;
    }
    // Zonder biasschatting zou dit 0.012 rad/s * 60 s = 41 graden zijn.
    CHECK(p.last().bias_state == BiasState::Estimated);
    CHECK_NEAR(p.last().yaw_rel_deg, yaw_at_20, 0.3);
    CHECK(p.last().aim_valid);
}

// ---------------- Aim ----------------

static void test_aim_mapping() {
    AimMapper m({20.0, 10.0});
    AimPoint c = m.map(0, 0);
    CHECK_NEAR(c.x, 0.5, 1e-12); CHECK_NEAR(c.y, 0.5, 1e-12); CHECK(!c.off_screen);
    AimPoint r = m.map(20 * kRadPerDeg, 0);
    CHECK_NEAR(r.x, 1.0, 1e-12); CHECK(!r.off_screen);
    AimPoint up = m.map(0, 5 * kRadPerDeg);
    CHECK_NEAR(up.y, 0.25, 1e-12);  // omhoog = kleinere y (schermcoördinaten)
    AimPoint far = m.map(-30 * kRadPerDeg, -12 * kRadPerDeg);
    CHECK_NEAR(far.x, -0.25, 1e-12);  // unclamped blijft bewaard
    CHECK_NEAR(far.x_clamped, 0.0, 1e-12);
    CHECK_NEAR(far.y, 1.1, 1e-12);
    CHECK_NEAR(far.y_clamped, 1.0, 1e-12);
    CHECK(far.off_screen);
    // Recenter: huidige richting wordt het midden.
    Quat q = from_axis_angle({0, 0, 1}, -15 * kRadPerDeg) * from_axis_angle({1, 0, 0}, 4 * kRadPerDeg);
    m.recenter(q, CenterSource::User);
    AimAngles rel = m.relative(q);
    CHECK_NEAR(rel.yaw_rad, 0, 1e-12); CHECK_NEAR(rel.pitch_rad, 0, 1e-12);
    CHECK(m.center_source() == CenterSource::User);
    Quat q2 = from_axis_angle({0, 0, 1}, -25 * kRadPerDeg) * from_axis_angle({1, 0, 0}, 4 * kRadPerDeg);
    CHECK_NEAR(m.relative(q2).yaw_rad * kDegPerRad, 10, 1e-9);
    // Yaw-omslag rond +-180 graden geeft geen sprong.
    m.recenter(from_axis_angle({0, 0, 1}, 175 * kRadPerDeg), CenterSource::User);
    CHECK_NEAR(m.relative(from_axis_angle({0, 0, 1}, -175 * kRadPerDeg)).yaw_rad * kDegPerRad, -10, 1e-9);
}

// ---------------- 1€-filter ----------------

static void test_one_euro() {
    OneEuro off({false, 1.0, 0.5, 1.0});
    CHECK(off.filter(0.3, 0.01) == 0.3);
    CHECK(off.filter(0.9, 0.01) == 0.9);

    OneEuro f({true, 1.0, 0.0, 1.0});
    CHECK(f.filter(0.5, 0.01) == 0.5);  // eerste sample ongewijzigd
    for (int i = 0; i < 100; ++i) CHECK_NEAR(f.filter(0.5, 0.01), 0.5, 1e-12);
    // dt <= 0: vorige uitvoer, geen sprong.
    CHECK_NEAR(f.filter(1.0, 0.0), 0.5, 1e-12);
    CHECK_NEAR(f.filter(1.0, -1.0), 0.5, 1e-12);

    // Met beta = 0 is het een eerste-orde laagdoorlaat: na T s is de stapresponsie ~1-exp(-T/tau),
    // ongeacht hoe de tijd in stappen is verdeeld (variabele dt).
    auto run = [](bool variable) {
        OneEuro g({true, 1.0, 0.0, 1.0});
        g.filter(0.0, 0.01);
        Lcg r;
        double t = 0, y = 0;
        while (t < 0.3 - 1e-12) {
            double dt = variable ? 0.001 + 0.009 * r.uniform() : 0.001;
            if (t + dt > 0.3) dt = 0.3 - t;
            y = g.filter(1.0, dt);
            t += dt;
        }
        return y;
    };
    double tau = 1.0 / (2 * kPi * 1.0);
    double expect = 1 - std::exp(-0.3 / tau);
    CHECK_NEAR(run(false), expect, 0.01);
    CHECK_NEAR(run(true), expect, 0.03);
    // Met beta > 0 volgt het filter snelle bewegingen sneller dan met beta = 0.
    OneEuro slow({true, 1.0, 0.0, 1.0}), fast({true, 1.0, 5.0, 1.0});
    slow.filter(0, 0.01); fast.filter(0, 0.01);
    double ys = 0, yf = 0;
    for (int i = 1; i <= 20; ++i) { ys = slow.filter(0.05 * i, 0.01); yf = fast.filter(0.05 * i, 0.01); }
    CHECK(yf > ys);
    CHECK(yf <= 1.0 + 1e-9);
    CHECK_NEAR(OneEuro::alpha(1.0, 1e9), 1.0, 1e-6);
}

// ---------------- Knoppen ----------------

static void test_button_edges() {
    EdgeAccumulator acc;
    ButtonEdges e = acc.update(kBtnT);
    CHECK(e.pressed == kBtnT && e.released == 0);
    e = acc.update(0);  // los in het volgende rapport
    CHECK(e.released == kBtnT);
    // Afnemer leest pas nu (tussen twee takes kwam druk én los): beide flanken zichtbaar.
    ButtonEdges t = acc.take();
    CHECK(t.pressed == kBtnT && t.released == kBtnT && t.state == 0);
    CHECK(t.press_count[20] == 1 && t.release_count[20] == 1);
    CHECK(acc.take().pressed == 0);  // gewist
    // Dubbele druk tussen twee takes: tellers 2.
    acc.update(kBtnMove); acc.update(0); acc.update(kBtnMove); acc.update(0);
    t = acc.take();
    CHECK(t.press_count[19] == 2 && t.release_count[19] == 2);
    // Vasthouden over takes heen: geen nieuwe flank, wel status.
    acc.update(kBtnCross);
    acc.take();
    t = acc.take();
    CHECK(t.pressed == 0 && t.state == kBtnCross);
    t = acc.release_all();
    CHECK(t.released == kBtnCross && acc.state() == 0);

    // Pijplijn: elk rapport geeft zijn eigen flanken; niets samengevoegd.
    Pipeline p;
    p.on_connect("00:00:00:00:00:02", 0);
    RawReport r;
    r.accel[0] = r.accel[1] = {0, 0, 1};
    r.t_rx_ns = 13'000'000; r.buttons = kBtnT; r.trigger = 255;
    InputSample s1 = p.on_report(r);
    r.t_rx_ns = 26'000'000; r.buttons = 0; r.trigger = 0;
    InputSample s2 = p.on_report(r);
    CHECK(s1.pressed == kBtnT && s2.released == kBtnT);
    t = p.edges().take();
    CHECK(t.pressed == kBtnT && t.released == kBtnT);
}

// ---------------- Stale, verlies, disconnect, tijdsvolgorde ----------------

static void test_stale_disconnect() {
    Pipeline p;
    p.on_connect("00:00:00:00:00:03", 0);
    RawReport r;
    r.accel[0] = r.accel[1] = {0, 0, 1};
    r.t_rx_ns = 1'000'000'000;
    r.hid_seq = 3;
    r.buttons = kBtnT | kBtnMove;
    r.trigger = 200;
    InputSample s = p.on_report(r);
    CHECK(s.link == LinkState::Fresh && s.orientation_valid && s.aim_valid);
    CHECK(s.center_source == CenterSource::AutoInitial);
    CHECK(!p.tick(1'040'000'000).has_value());  // nog vers
    auto st = p.tick(1'060'000'000);
    CHECK(st.has_value() && st->link == LinkState::Stale);
    CHECK(st && !st->aim_valid && !st->orientation_valid);
    CHECK(st && st->buttons == (kBtnT | kBtnMove));  // kort gat: knoppen nog vast
    CHECK(!p.tick(1'100'000'000).has_value());       // geen dubbele melding
    auto lost = p.tick(1'300'000'000);
    CHECK(lost.has_value() && lost->link == LinkState::Lost);
    CHECK(lost && lost->buttons == 0 && lost->released == (kBtnT | kBtnMove) && lost->trigger == 0);
    CHECK(!p.recenter());  // geen actuele oriëntatie

    // Rapport komt terug na 400 ms: niet geïntegreerd, yaw-referentie onderbroken, wel weer geldig.
    r.t_rx_ns = 1'400'000'000;
    r.hid_seq = 6;  // 4 en 5 gemist
    r.buttons = 0;
    s = p.on_report(r);
    CHECK(s.link == LinkState::Fresh && s.aim_valid);
    CHECK(s.yaw_ref == YawReference::Interrupted);
    CHECK(p.stats().gap_not_integrated == 1);
    CHECK(p.stats().dropped_seq == 2);
    CHECK(p.recenter());
    CHECK(p.last().yaw_ref == YawReference::Continuous);
    CHECK(p.last().center_source == CenterSource::User);

    // Niet-monotone tijd: geteld, dt = 0, geen integratie.
    r.t_rx_ns = 1'390'000'000;
    s = p.on_report(r);
    CHECK(p.stats().non_monotonic == 1 && s.report_dt_s == 0);

    // Disconnect: alles ongeldig, knoppen vrij.
    r.t_rx_ns = 1'420'000'000;
    r.buttons = kBtnCross;
    p.on_report(r);
    InputSample d = p.on_disconnect(1'500'000'000);
    CHECK(d.link == LinkState::Disconnected && !d.orientation_valid && !d.aim_valid && !d.optical.valid);
    CHECK(d.buttons == 0 && d.released == kBtnCross && d.trigger == 0);
    CHECK(!p.tick(9'000'000'000).has_value());
    // Herverbinden met dezelfde controller: oriëntatie en midden blijven, maar onderbroken.
    p.on_connect("00:00:00:00:00:03", 10'000'000'000);
    r.t_rx_ns = 10'010'000'000;
    s = p.on_report(r);
    CHECK(s.aim_valid && s.yaw_ref == YawReference::Interrupted && s.pressed == kBtnCross);
    // Andere controller: volledige reset, geen midden van de vorige controller.
    p.on_connect("00:00:00:00:00:04", 11'000'000'000);
    r.t_rx_ns = 11'010'000'000;
    s = p.on_report(r);
    CHECK(s.center_source == CenterSource::AutoInitial);
    Pipeline q({});
    PipelineConfig nc;
    nc.auto_center_on_start = false;
    Pipeline p2(nc);
    p2.on_connect("x", 0);
    r.t_rx_ns = 5;
    s = p2.on_report(r);
    CHECK(s.orientation_valid && !s.aim_valid && s.center_source == CenterSource::None);
    CHECK(p2.recenter() && p2.last().center_source == CenterSource::User);
}

static void test_report_stats() {
    ReportStats st;
    for (double ms : {1.0, 6.0, 13.0, 13.0, 19.0, 36.0, 400.0}) st.add_interval(ms);
    CHECK(st.buckets[0] == 1 && st.buckets[1] == 1 && st.buckets[2] == 2 && st.buckets[3] == 1);
    CHECK(st.buckets[5] == 1 && st.buckets[8] == 1);
    CHECK(st.max_ms == 400.0);
    CHECK_NEAR(st.percentile(0.5), 13.0, 1e-12);
    for (int i = 0; i < 100; ++i) st.add_report(static_cast<int64_t>(i) * 10'000'000);
    CHECK_NEAR(st.rate(990'000'000, 0.5), 100.0, 2.1);
}

// ---------------- JSON en IPC-protocol ----------------

static void test_json_parse() {
    auto ok = json::parse(R"( {"a":[1,2.5e1,-0.5,true,false,null],"b":{"c":"x\"\u00e9\ud83d\ude00"}} )");
    CHECK(ok.ok);
    CHECK(ok.value.find("a")->arr.size() == 6 && ok.value.find("a")->arr[1].num == 25);
    CHECK(ok.value.find("b")->find("c")->str == "x\"\xc3\xa9\xf0\x9f\x98\x80");
    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1,}", "01", "1.", "\"abc", "{\"a\":1}x", "tru",
                            "{\"a\":1,\"a\":2}", "\"\\ud800\"", "\"\\x\"", "\"a\nb\"", "1e999", "{a:1}", "[1 2]"}) {
        auto r = json::parse(bad);
        if (r.ok) std::fprintf(stderr, "onterecht geaccepteerd: %s\n", bad);
        CHECK(!r.ok && !r.error.empty());
    }
    std::string deep(40, '[');
    deep += std::string(40, ']');
    CHECK(!json::parse(deep).ok);
    std::string ok_deep(30, '[');
    ok_deep += std::string(30, ']');
    CHECK(json::parse(ok_deep).ok);
}

static const ipc::Error* as_err(const std::variant<ipc::Request, ipc::Error>& v) { return std::get_if<ipc::Error>(&v); }
static const ipc::Request* as_req(const std::variant<ipc::Request, ipc::Error>& v) { return std::get_if<ipc::Request>(&v); }

static void test_protocol() {
    auto r = ipc::parse_request(R"({"v":1,"cmd":"status","id":7})");
    CHECK(as_req(r) && as_req(r)->cmd == ipc::Cmd::Status && as_req(r)->id_json == "7");
    r = ipc::parse_request(R"({"v":1,"cmd":"watch","rate_hz":60,"full_ids":true,"id":"abc"})");
    CHECK(as_req(r) && as_req(r)->rate_hz == 60 && as_req(r)->full_ids && as_req(r)->id_json == "\"abc\"");
    r = ipc::parse_request(R"({"v":1,"cmd":"set_filter","enabled":false})");
    CHECK(as_req(r) && as_req(r)->filter_enabled && !*as_req(r)->filter_enabled);
    r = ipc::parse_request(R"({"v":1,"cmd":"recenter"})");
    CHECK(as_req(r) && as_req(r)->cmd == ipc::Cmd::Recenter);

    struct Case { const char* line; const char* code; };
    for (const Case& c : std::initializer_list<Case>{
             {"niet json", "parse_error"},
             {"[1,2]", "bad_request"},
             {R"({"cmd":"status"})", "bad_request"},
             {R"({"v":2,"cmd":"status"})", "unsupported_version"},
             {R"({"v":"1","cmd":"status"})", "unsupported_version"},
             {R"({"v":1})", "bad_request"},
             {R"({"v":1,"cmd":5})", "bad_request"},
             {R"({"v":1,"cmd":"rm -rf"})", "unknown_command"},
             {R"({"v":1,"cmd":"status","extra":1})", "bad_request"},
             {R"({"v":1,"cmd":"recenter","full_ids":true})", "bad_request"},
             {R"({"v":1,"cmd":"watch","rate_hz":0})", "invalid_argument"},
             {R"({"v":1,"cmd":"watch","rate_hz":1000})", "invalid_argument"},
             {R"({"v":1,"cmd":"watch","rate_hz":"30"})", "invalid_argument"},
             {R"({"v":1,"cmd":"status","full_ids":1})", "invalid_argument"},
             {R"({"v":1,"cmd":"set_filter"})", "invalid_argument"},
             {R"({"v":1,"cmd":"set_filter","beta":-1})", "invalid_argument"},
             {R"({"v":1,"cmd":"set_aim","h_edge_deg":0})", "invalid_argument"},
             {R"({"v":1,"cmd":"status","id":[1]})", "bad_request"},
             {R"({"v":1,"cmd":"status","id":1.5})", "bad_request"},
         }) {
        auto res = ipc::parse_request(c.line);
        const ipc::Error* e = as_err(res);
        if (!e || e->code != c.code)
            std::fprintf(stderr, "verwacht %s voor %s, kreeg %s\n", c.code, c.line, e ? e->code.c_str() : "ok");
        CHECK(e && e->code == c.code && !e->message.empty());
    }
    // id wordt ook bij fouten teruggegeven als het leesbaar is.
    auto e = ipc::parse_request(R"({"v":1,"cmd":"nope","id":42})");
    CHECK(as_err(e) && as_err(e)->id_json == "42");
    std::string resp = ipc::error_response(*as_err(e));
    auto pr = json::parse(resp);
    CHECK(pr.ok && !pr.value.find("ok")->b && pr.value.find("id")->num == 42);
    CHECK(pr.value.find("error")->find("code")->str == "unknown_command");
    // Te lange regel.
    std::string huge = R"({"v":1,"cmd":"status","id":")" + std::string(ipc::kMaxLineBytes, 'x') + "\"}";
    CHECK(as_err(ipc::parse_request(huge)) && as_err(ipc::parse_request(huge))->code == "line_too_long");
    // Rondgang: make_request -> parse_request.
    ipc::Request rq;
    rq.cmd = ipc::Cmd::SetAim;
    rq.h_edge_deg = 25;
    rq.id_json = "\"x\"";
    auto back = ipc::parse_request(ipc::make_request(rq));
    CHECK(as_req(back) && as_req(back)->h_edge_deg && *as_req(back)->h_edge_deg == 25 && !as_req(back)->v_edge_deg);
    rq = {};
    rq.cmd = ipc::Cmd::Watch;
    rq.rate_hz = 12.5;
    back = ipc::parse_request(ipc::make_request(rq));
    CHECK(as_req(back) && as_req(back)->rate_hz == 12.5);
    CHECK(json::parse(ipc::ok_response("", "{}")).ok);
    CHECK(json::parse(ipc::event_line("sample", "{\"a\":1}")).value.find("data")->find("a")->num == 1);
}

int main() {
    test_quaternion();
    test_axes();
    test_units_json_sample();
    test_fusion_converges_still();
    test_fusion_rotation_one_axis();
    test_bias_estimation();
    test_aim_mapping();
    test_one_euro();
    test_button_edges();
    test_stale_disconnect();
    test_report_stats();
    test_json_parse();
    test_protocol();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
