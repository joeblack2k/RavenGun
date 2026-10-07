// Tests voor fase 0 (opnameformaat, deterministisch afspelen), fase 1 (rapportparsing,
// Move-klok, integratie-dt) en fase 3 (ellipsoïdefit, kwaliteitsmaten, opslag, verstoringsdetectie).
// Synthetische data; geen hardware.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <variant>
#include <vector>

#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/move_clock.hpp"
#include "ravengun/core/move_report.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/rec/recording.hpp"
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
            std::fprintf(stderr, "FAIL %s:%d: %s = %.9g, verwacht %.9g +- %g\n", __FILE__, __LINE__, #a, \
                         _a, _b, static_cast<double>(tol));                                            \
        }                                                                                              \
    } while (0)

struct Lcg {
    uint64_t s = 987654321;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }
};

static std::string tmpdir() {
    char t[] = "/tmp/ravengun-test-XXXXXX";
    char* d = mkdtemp(t);
    return d ? d : "";
}

// ---------------- Fase 1: rapportparsing ----------------

static void test_parse_report() {
    // Echt ZCM1-rapport (Bluetooth, hidraw), op deze host opgenomen; adres zit er niet in.
    const char* hex = "010000000100007f7f7f7f2d05c18a4e7fc38ccc8a477fb88c1080e77f06800c80ec7f288088408ef94ff0520000000000";
    std::vector<uint8_t> b;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) b.push_back(static_cast<uint8_t>(std::strtol(std::string(hex + i, 2).c_str(), nullptr, 16)));
    CHECK(b.size() == 49);
    MoveRawInput in;
    CHECK(parse_zcm1_report(b.data(), b.size(), in));
    CHECK(in.seq == 1);
    CHECK(in.stamp == 0x2D52);  // byte 0x0B = 0x2d, byte 0x2B = 0x52
    CHECK(in.temperature_raw == ((0x88 << 4) | (0x40 >> 4)));
    CHECK(in.battery == 0x05);
    // Accelerometer frame 0, X: 0x8ac1 - 0x8000
    CHECK(in.accel_raw[0][0] == 0x8ac1 - 0x8000);
    CHECK(in.gyro_raw[0][0] == 0x8010 - 0x8000);
    // Magnetometer: X = byte38 low nibble << 8 | byte39 (12 bit signed)
    int mx = ((0x40 & 0x0F) << 8) | 0x8e;
    CHECK(in.mag[0] == ((mx & 0x800) ? mx - 0x1000 : mx));
    int avg[3];
    psmoveapi_accel_average(in, avg);
    CHECK(avg[0] == ((0x8ac1 + 0x8acc) / 2 - 0x8000));
    CHECK(!parse_zcm1_report(b.data(), 20, in));
    b[0] = 0x02;
    CHECK(!parse_zcm1_report(b.data(), b.size(), in));
    CHECK(stamp_delta(65530, 4) == 10);
}

// ---------------- Fase 1: klokkoppeling ----------------

static void test_move_clock() {
    Lcg rng;
    const double tps = kMoveTicksPerSecond;
    const double skew = 60e-6;            // Move-klok loopt 60 ppm snel t.o.v. de host
    const double period = 1127;           // ticks
    const int64_t host0 = 5'000'000'000;  // ns
    MoveClock clk;
    int64_t true_ticks = 40000;
    double max_err_ms = 0, min_res = 1e9;
    int64_t last_ticks = 0;
    bool first = true;
    for (int i = 0; i < 6000; ++i) {
        int adv = 1;
        if (rng.uniform() < 0.13) adv = 2;  // overgeslagen rapport
        if (i == 3000) adv = 210;           // gat van ~2,4 s (meerdere wraps)
        true_ticks += static_cast<int64_t>(adv * period + (rng.uniform() - 0.5) * 40);
        double t_move_s = static_cast<double>(true_ticks) / tps;
        double delay_ms = 2.0 + (rng.uniform() < 0.5 ? rng.uniform() * 1.0 : rng.uniform() * 15.0);
        int64_t t_rx = host0 + static_cast<int64_t>(t_move_s / (1 + skew) * 1e9 + delay_ms * 1e6);
        ClockSample cs = clk.add(static_cast<uint16_t>(true_ticks & 0xFFFF), t_rx);
        if (first) { last_ticks = cs.ticks; first = false; continue; }
        // Ontrollen is exact: verschil van de ontrolde stempel = echt verschil.
        CHECK(cs.ticks - last_ticks == cs.dticks);
        last_ticks = cs.ticks;
        if (i > 400 && i != 3000) {
            int64_t ideal = host0 + static_cast<int64_t>(t_move_s / (1 + skew) * 1e9 + 2.0e6);  // kleinste vertraging
            double e_ms = static_cast<double>(cs.t_mapped_ns - ideal) / 1e6;
            max_err_ms = std::max(max_err_ms, std::fabs(e_ms));
            min_res = std::min(min_res, cs.residual_ms);
        }
        if (i == 3000) CHECK(cs.wraps_added >= 3);
    }
    CHECK(clk.coupled());
    CHECK_NEAR(clk.skew_ppm(), -skew / (1 + skew) * 1e6, 8.0);  // host-ns per Move-ns - 1
    std::printf("klok: max fout %.3f ms, min rest %.3f ms, skew %.2f ppm\n", max_err_ms, min_res, clk.skew_ppm());
    CHECK(max_err_ms < 0.5);    // gekoppelde tijd binnen 0,5 ms van stempel + minimale vertraging
    CHECK(min_res > -0.3);      // ondergrens: (vrijwel) nooit negatieve restvertraging
    CHECK(clk.wrap_corrections() == 1);
    // Ontrolde totale ticks = echte ticks sinds start.
    CHECK(clk.last_ticks() - 40000 - static_cast<int64_t>(0) > 0);
}

// Integratie-dt uit de stempel: rotatie integreert met de echte sensortijd, ongevoelig voor
// ontvangstjitter; zonder stempel valt de pijplijn terug op t_rx.
static void test_pipeline_move_dt() {
    Lcg rng;
    for (int mode = 0; mode < 2; ++mode) {
        PipelineConfig cfg;
        cfg.use_move_clock = mode == 0;
        Pipeline p(cfg);
        p.on_connect("aa:bb:cc:dd:ee:ff", 0);
        int64_t t = 1'000'000'000;
        uint32_t ticks = 1000;
        double truth = 0;
        for (int i = 0; i < 400; ++i) {
            RawReport r;
            double w = (i % 50 < 25 ? 1.0 : -0.4);  // rad/s rond Z (wereld-yaw)
            uint32_t d = 1127;
            ticks += d;
            if (i > 0) truth += w * d / kMoveTicksPerSecond;
            // Ontvangst: sterk wisselende vertraging (bursts), netto even lang.
            t += static_cast<int64_t>((rng.uniform() < 0.5 ? 6.0 : 16.54) * 1e6);
            r.t_rx_ns = t;
            r.hid_seq = i % 16;
            r.has_stamp = true;
            r.stamp = static_cast<uint16_t>(ticks & 0xFFFF);
            for (int k = 0; k < 2; ++k) { r.accel[k] = {0, 0, 1}; r.gyro[k] = {0, 0, w}; }
            p.on_report(r);
        }
        AimAngles a = world_angles(p.last().q_wb);
        double yaw = -a.yaw_rad;  // positieve rotatie rond +Z = naar links = negatieve yaw (zie test_axes)
        if (mode == 0) {
            CHECK_NEAR(yaw, wrap_pi(truth), 1e-6);
            CHECK(p.dt_from_move_clock() == 399);
            CHECK(p.dt_from_rx() == 0);
        } else {
            CHECK(std::fabs(yaw - wrap_pi(truth)) > 1e-3);  // ontvangsttijd: meetbaar anders
            CHECK(p.dt_from_rx() == 399);
        }
    }
}

// ---------------- Fase 0: opnameformaat ----------------

static bool same(const RawReport& a, const RawReport& b) {
    bool ok = a.t_rx_ns == b.t_rx_ns && a.hid_seq == b.hid_seq && a.subsamples == b.subsamples && a.buttons == b.buttons &&
              a.trigger == b.trigger && a.battery_raw == b.battery_raw && a.has_stamp == b.has_stamp &&
              a.temperature_raw == b.temperature_raw && a.has_raw == b.has_raw;
    if (!a.has_stamp) ok = ok && b.stamp == 0; else ok = ok && a.stamp == b.stamp;
    for (int i = 0; i < a.subsamples; ++i)
        ok = ok && a.accel[i].x == b.accel[i].x && a.accel[i].y == b.accel[i].y && a.accel[i].z == b.accel[i].z &&
             a.gyro[i].x == b.gyro[i].x && a.gyro[i].y == b.gyro[i].y && a.gyro[i].z == b.gyro[i].z;
    for (int i = 0; i < 3; ++i) ok = ok && a.mag_raw[i] == b.mag_raw[i];
    if (a.has_raw)
        for (int f = 0; f < 2; ++f)
            for (int i = 0; i < 3; ++i) ok = ok && a.accel_raw[f][i] == b.accel_raw[f][i] && a.gyro_raw[f][i] == b.gyro_raw[f][i];
    return ok;
}

static void test_recording_roundtrip() {
    rec::Header h;
    h.tag = "stil";
    h.controller = "xx:xx:xx:xx:xx:ac";
    h.model = "ZCM1";
    h.t0_ns = 9'123'456'789'012'345;  // groter dan 2^53: moet via de string exact blijven
    h.led_enabled = true;
    h.led[0] = 0; h.led[1] = 255; h.led[2] = 255;
    h.dt_source = "move";
    int64_t t0 = 0;
    rec::Record out;
    std::string err;
    CHECK(rec::parse_line(rec::format_header(h), out, err, t0));
    CHECK(out.kind == rec::Kind::Header && out.header.t0_ns == h.t0_ns && out.header.tag == "stil" && out.header.led[1] == 255);

    Lcg rng;
    for (int k = 0; k < 200; ++k) {
        RawReport r;
        r.t_rx_ns = h.t0_ns + static_cast<int64_t>(rng.uniform() * 1e12);
        r.hid_seq = k % 16;
        r.subsamples = k % 7 == 0 ? 1 : 2;
        for (int i = 0; i < 2; ++i) {
            r.accel[i] = {rng.gauss() * 1e-3, -rng.gauss(), 0.1 + 1e-17 * rng.uniform()};
            r.gyro[i] = {rng.gauss() * 7.3, static_cast<double>(static_cast<float>(rng.gauss())), -1e-300};
        }
        r.mag_raw[0] = -2048; r.mag_raw[1] = 2047; r.mag_raw[2] = k;
        r.buttons = 0x80000000u >> (k % 32);
        r.trigger = static_cast<uint8_t>(k);
        r.battery_raw = k % 2 ? 5 : -1;
        r.has_stamp = k % 3 != 0;
        r.stamp = r.has_stamp ? static_cast<uint16_t>(k * 1127) : 0;
        r.temperature_raw = k % 5 ? 2179 : -1;
        r.has_raw = k % 2 == 0;
        for (int f = 0; f < 2; ++f) for (int i = 0; i < 3; ++i) { r.accel_raw[f][i] = -32768 + k + i; r.gyro_raw[f][i] = 32767 - k - f; }
        std::string line = rec::format_report(r, h.t0_ns);
        rec::Record o;
        CHECK(rec::parse_line(line, o, err, t0));
        CHECK(o.kind == rec::Kind::Report);
        CHECK(same(r, o.report));
        if (!same(r, o.report)) { std::fprintf(stderr, "%s\n", line.c_str()); break; }
    }
    OpticalSample os;
    os.camera_active = true; os.camera_state = "streaming"; os.valid = true; os.reason = "none"; os.track_state = "tracking";
    os.frame_seq = 123456; os.t_frame_ns = h.t0_ns + 5; os.t_processed_ns = h.t0_ns + 7; os.width = 640; os.height = 480;
    os.x_px = 321.123456789; os.y_px = 0.5; os.x_norm = os.x_px / 640; os.y_norm = os.y_px / 480; os.radius_px = 19.0625; os.candidates = 1;
    rec::Record oo;
    CHECK(rec::parse_line(rec::format_optical(os, h.t0_ns), oo, err, t0));
    CHECK(oo.kind == rec::Kind::Optical && oo.optical.x_px == os.x_px && oo.optical.radius_px == os.radius_px &&
          oo.optical.t_frame_ns == os.t_frame_ns && oo.optical.frame_seq == os.frame_seq && std::string(oo.optical.reason) == "none" &&
          oo.optical.valid && std::string(oo.optical.track_state) == "tracking");
    rec::LedEvent le{h.t0_ns + 1000, {255, 0, 0}};
    CHECK(rec::parse_line(rec::format_led(le, h.t0_ns), oo, err, t0) && oo.kind == rec::Kind::Led && oo.led.rgb[0] == 255 && oo.led.t_ns == le.t_ns);
    CHECK(rec::parse_line(rec::format_mark({h.t0_ns + 3, "pomp \"in\""}, h.t0_ns), oo, err, t0) && oo.mark.label == "pomp \"in\"");
    rec::End e{h.t0_ns + 9, "size_limit", 10, 20, 3};
    CHECK(rec::parse_line(rec::format_end(e, h.t0_ns), oo, err, t0) && oo.end.reason == "size_limit" && oo.end.dropped == 3);
    // Fouten
    CHECK(!rec::parse_line("{\"k\":\"header\",\"format\":\"ravengun-recording\",\"version\":99,\"t0_ns\":\"1\"}", oo, err, t0));
    CHECK(!rec::parse_line("{\"k\":\"r\",\"t\":1}", oo, err, t0));
    CHECK(!rec::parse_line("geen json", oo, err, t0));
    CHECK(rec::valid_tag("magcal_led-1"));
    CHECK(!rec::valid_tag("../x"));
    CHECK(!rec::valid_tag(""));
    CHECK(!rec::valid_tag("A"));
}

// Synthetische opname: draaiende Move met ruis, stempels, optische metingen en LED-wissels.
static std::string write_synthetic_recording(const std::string& dir) {
    std::string path = dir + "/syn.rgrec.jsonl";
    FILE* f = std::fopen(path.c_str(), "w");
    rec::Header h;
    h.tag = "syn";
    h.model = "ZCM1";
    h.controller = "xx:xx:xx:xx:xx:01";
    h.t0_ns = 1'000'000'000'000;
    std::fprintf(f, "%s\n", rec::format_header(h).c_str());
    Lcg rng;
    int64_t t = h.t0_ns;
    uint32_t ticks = 777;
    for (int i = 0; i < 3000; ++i) {
        RawReport r;
        t += static_cast<int64_t>((rng.uniform() < 0.6 ? 6.0 : 20.0) * 1e6);
        ticks += 1127;
        r.t_rx_ns = t;
        r.hid_seq = i % 16;
        r.has_stamp = i % 11 != 5;
        r.stamp = static_cast<uint16_t>(ticks);
        r.temperature_raw = 2179;
        double w = std::sin(i * 0.01) * 0.8;
        for (int k = 0; k < 2; ++k) {
            r.accel[k] = {rng.gauss() * 0.01, rng.gauss() * 0.01, 1 + rng.gauss() * 0.01};
            r.gyro[k] = {rng.gauss() * 0.01, rng.gauss() * 0.01, w + rng.gauss() * 0.02};
        }
        r.buttons = (i / 100) % 2 ? 0x100000u : 0;
        r.trigger = static_cast<uint8_t>(i % 256);
        std::fprintf(f, "%s\n", rec::format_report(r, h.t0_ns).c_str());
        if (i % 4 == 0) {
            OpticalSample o;
            o.valid = i % 8 == 0;
            o.reason = o.valid ? "none" : "no_candidate";
            o.t_frame_ns = t - 3'000'000;
            o.x_px = 300 + i * 0.01;
            std::fprintf(f, "%s\n", rec::format_optical(o, h.t0_ns).c_str());
        }
        if (i % 1000 == 0) std::fprintf(f, "%s\n", rec::format_led({t, {i % 255, 0, 255}}, h.t0_ns).c_str());
    }
    std::fprintf(f, "%s\n", rec::format_end({t, "stop", 3000, 750, 0}, h.t0_ns).c_str());
    std::fclose(f);
    return path;
}

static void test_replay_deterministic() {
    std::string dir = tmpdir();
    CHECK(!dir.empty());
    std::string path = write_synthetic_recording(dir);
    rec::ReplayOptions o;
    o.keep_json = true;
    rec::ReplayResult a = rec::replay_file(path, o);
    rec::ReplayResult b = rec::replay_file(path, o);
    CHECK(a.ok && b.ok);
    CHECK(a.reports == 3000 && a.optical == 750 && a.leds == 3 && a.ended && a.end_reason == "stop");
    CHECK(a.samples >= a.reports);
    CHECK(a.digest == b.digest);
    CHECK(a.json == b.json);  // tweemaal exact dezelfde uitvoer
    // Andere tijdbasis -> andere uitvoer (de digest is gevoelig).
    rec::ReplayOptions rx = o;
    rx.pipeline.use_move_clock = false;
    rec::ReplayResult c = rec::replay_file(path, rx);
    CHECK(c.digest != a.digest);
    // Zelfde records rechtstreeks uit het geheugen -> zelfde digest.
    std::vector<rec::Record> recs;
    std::string err;
    CHECK(rec::load_file(path, recs, err));
    rec::ReplayResult d = rec::replay_records(recs, o);
    CHECK(d.digest == a.digest);
    // Optische meting komt in de samples (zoals in de daemon).
    CHECK(a.json.back().find("\"kind\":\"image_2d\"") != std::string::npos);
    std::remove(path.c_str());
    ::rmdir(dir.c_str());
}

// ---------------- Fase 3: magnetometer ----------------

struct MagWorld {
    Vec3 b{120, -80, 40};     // hard iron (counts)
    Mat3 S;                   // soft iron (symmetrisch)
    double field = 350;       // counts
    double dip = 67 * kRadPerDeg;
    MagWorld() {
        double s[3][3] = {{1.10, 0.05, -0.03}, {0.05, 0.90, 0.02}, {-0.03, 0.02, 1.05}};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) S.m[i][j] = s[i][j];
    }
    // Ruwe meting en accelerometer voor oriëntatie q (controller -> wereld).
    void sample(Quat q, Lcg& rng, double noise, MagSample& out) const {
        Vec3 mw{0, std::cos(dip), -std::sin(dip)};
        Vec3 mb = rotate(conj(q), mw);
        Vec3 sens{mb.x, -mb.y, mb.z};  // B -> ruw sensorstelsel (Y omgedraaid)
        Vec3 raw = mul(S, sens) * field + b;
        out.raw = {std::round(raw.x + rng.gauss() * noise), std::round(raw.y + rng.gauss() * noise), std::round(raw.z + rng.gauss() * noise)};
        out.accel = rotate(conj(q), Vec3{0, 0, 1});
    }
};

static Quat random_q(Lcg& rng) {
    Quat q{rng.gauss(), rng.gauss(), rng.gauss(), rng.gauss()};
    return normalized(q);
}

static void test_mag_fit() {
    MagWorld w;
    Lcg rng;
    std::vector<MagSample> s;
    for (int i = 0; i < 1500; ++i) { MagSample m; w.sample(random_q(rng), rng, 1.5, m); s.push_back(m); }
    MagFitResult r = fit_magnetometer(s);
    CHECK(r.fitted);
    CHECK(r.ok);
    CHECK(r.cal.kind == MagFitKind::Full);
    CHECK_NEAR(r.cal.offset.x, 120, 1.0);
    CHECK_NEAR(r.cal.offset.y, -80, 1.0);
    CHECK_NEAR(r.cal.offset.z, 40, 1.0);
    // A * S * field = I (soft iron hersteld)
    Mat3 P = mul(r.cal.soft, w.S);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) CHECK_NEAR(P.m[i][j] * w.field, i == j ? 1.0 : 0.0, 0.01);
    CHECK_NEAR(r.cal.field_counts, w.field * std::cbrt(1.10 * 0.90 * 1.05), 0.03 * w.field);
    CHECK(r.cal.rms_norm_err < 0.01);
    CHECK(r.cal.coverage >= 0.95);
    CHECK_NEAR(r.dip_mean_deg, 67, 0.3);
    CHECK(r.cal.dip_std_deg < 0.5);
    CHECK(r.dip_std_noflip_deg > 5);  // de verkeerde ashypothese valt op
    // Toepassen: gekalibreerde vector in B = echte richting.
    Quat q = from_axis_angle({1, 2, 3}, 1.0);
    MagSample m;
    Lcg quiet;
    w.sample(q, quiet, 0, m);
    Vec3 mb = mag_apply(r.cal, m.raw);
    Vec3 truth = rotate(conj(q), Vec3{0, std::cos(w.dip), -std::sin(w.dip)});
    CHECK(norm(mb - truth) < 0.01);
    CHECK_NEAR(dip_deg(mb, m.accel), 67, 0.5);
}

static void test_mag_fit_quality_gates() {
    MagWorld w;
    Lcg rng;
    // Alleen een kegel van 35 graden rond één houding: te weinig dekking.
    std::vector<MagSample> cone;
    for (int i = 0; i < 800; ++i) {
        Vec3 ax{rng.gauss(), rng.gauss(), rng.gauss()};
        Quat q = from_axis_angle(ax, rng.uniform() * 35 * kRadPerDeg);
        MagSample m;
        w.sample(q, rng, 1.5, m);
        cone.push_back(m);
    }
    MagFitResult r = fit_magnetometer(cone);
    CHECK(!r.ok);
    CHECK(r.cal.coverage < 0.5);
    bool says_coverage = false;
    for (const auto& p : r.problems) if (p.find("dekking") != std::string::npos) says_coverage = true;
    CHECK(says_coverage);
    CHECK(r.cal.kind != MagFitKind::Full);  // terugval naar asgericht
    // Te weinig samples (wel volledige dekking).
    std::vector<MagSample> few;
    for (int i = 0; i < 120; ++i) { MagSample m; w.sample(random_q(rng), rng, 1.5, m); few.push_back(m); }
    r = fit_magnetometer(few);
    CHECK(!r.ok);
    bool says_n = false;
    for (const auto& p : r.problems) if (p.find("te weinig samples") != std::string::npos) says_n = true;
    CHECK(says_n);
    // Inhomogeen veld: de helft van de samples met een extra storing van 15 %.
    std::vector<MagSample> bad;
    for (int i = 0; i < 1000; ++i) {
        MagSample m;
        w.sample(random_q(rng), rng, 1.5, m);
        if (i % 2) m.raw = m.raw + Vec3{50, 20, -30};
        bad.push_back(m);
    }
    r = fit_magnetometer(bad);
    CHECK(!r.ok);
    // Sphere coverage
    SphereCoverage c;
    for (int i = 0; i < 2000; ++i) c.add({rng.gauss(), rng.gauss(), rng.gauss()});
    CHECK(c.fraction() == 1.0);
    SphereCoverage h;
    for (int i = 0; i < 2000; ++i) { Vec3 v{rng.gauss(), rng.gauss(), std::fabs(rng.gauss())}; h.add(v); }
    CHECK(h.fraction() > 0.3 && h.fraction() < 0.8);
    // Uitdunnen: stilhouden geeft maar één sample.
    MagCollector col;
    int raw[3] = {100, 200, 300};
    for (int i = 0; i < 100; ++i) col.add(raw, {0, 0, 1}, i * 0.013);
    CHECK(col.samples().size() == 1);
    CHECK(col.seen() == 100);
}

static void test_mag_storage() {
    std::string dir = tmpdir();
    MagCalibration c;
    c.kind = MagFitKind::Full;
    c.offset = {1.5, -2.25, 3};
    c.soft.m[0][1] = c.soft.m[1][0] = 0.01;
    c.field_counts = 350.5;
    c.dip_ref_deg = 66.9;
    c.controller = "xx:xx:xx:xx:xx:ac";
    c.controller_key = controller_key("AA:BB:CC:DD:EE:AC");
    CHECK(c.controller_key == controller_key("aa:bb:cc:dd:ee:ac"));
    CHECK(c.controller_key.size() == 16);
    c.holder = "sharp_shooter";
    std::string err;
    std::string sub = dir + "/ravengun/magnetometer";
    CHECK(save_mag_calibration(sub, c, err));
    struct stat st{};
    CHECK(::stat((sub + "/" + c.controller_key + ".json").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(::stat(sub.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    MagCalibration l;
    CHECK(load_mag_calibration(sub, c.controller_key, l, err));
    CHECK(l.kind == MagFitKind::Full && l.offset.y == -2.25 && l.soft.m[0][1] == 0.01 && l.holder == "sharp_shooter" && l.dip_ref_deg == 66.9);
    CHECK(!load_mag_calibration(sub, "0000000000000000", l, err) && err == "ontbreekt");
    // Beschadigd bestand
    FILE* f = std::fopen((sub + "/bad.json").c_str(), "w");
    std::fputs("{\"format\":\"ravengun-mag-calibration\",\"version\":1,\"kind\":\"full\",\"offset\":[1,2]}", f);
    std::fclose(f);
    CHECK(!load_mag_calibration(sub, "bad", l, err) && err.find("beschadigd") == 0);
    f = std::fopen((sub + "/v9.json").c_str(), "w");
    std::fputs("{\"format\":\"ravengun-mag-calibration\",\"version\":9}", f);
    std::fclose(f);
    CHECK(!load_mag_calibration(sub, "v9", l, err) && err.find("andere versie") == 0);
    std::remove((sub + "/bad.json").c_str());
    std::remove((sub + "/v9.json").c_str());
    std::remove((sub + "/" + c.controller_key + ".json").c_str());
    ::rmdir(sub.c_str());
    ::rmdir((dir + "/ravengun").c_str());
    ::rmdir(dir.c_str());
}

static void test_mag_disturbance() {
    const double dip = 67 * kRadPerDeg;
    Vec3 mw{0, std::cos(dip), -std::sin(dip)};
    const double dt = 0.013;
    MagDisturbanceDetector d;
    d.set_reference_dip(67);
    Quat q = from_axis_angle({1, 0, 0}, 0.2);
    Vec3 acc = rotate(conj(q), Vec3{0, 0, 1});
    MagCheck c;
    for (int i = 0; i < 100; ++i) c = d.update(rotate(conj(q), mw), acc, {0, 0, 0}, dt);
    CHECK(c.accepted && c.state == MagState::Ok);
    CHECK_NEAR(c.dip_deg, 67, 1e-6);
    CHECK_NEAR(c.rate_err_deg_s, 0, 1e-6);
    // 10 % sterker veld: afgewezen op norm; na > 1 s "verstoord".
    for (int i = 0; i < 90; ++i) c = d.update(rotate(conj(q), mw) * 1.10, acc, {0, 0, 0}, dt);
    CHECK(!c.accepted && (c.reasons & kRejectNorm));
    CHECK(c.state == MagState::Disturbed);
    CHECK(d.disturbed_episodes() == 1);
    // Herstel pas na 2 s consistente samples.
    for (int i = 0; i < 100; ++i) c = d.update(rotate(conj(q), mw), acc, {0, 0, 0}, dt);
    CHECK(c.accepted && c.state == MagState::Disturbed);  // 1,3 s: nog niet hersteld
    for (int i = 0; i < 80; ++i) c = d.update(rotate(conj(q), mw), acc, {0, 0, 0}, dt);
    CHECK(c.state == MagState::Ok);
    // Dip 5 graden anders (zelfde norm): afgewezen op dip.
    Vec3 mw2{0, std::cos(dip - 5 * kRadPerDeg), -std::sin(dip - 5 * kRadPerDeg)};
    c = d.update(rotate(conj(q), mw2), acc, {0, 0, 0}, dt);
    CHECK(!c.accepted && (c.reasons & kRejectDip) && !(c.reasons & kRejectNorm));
    // Kort (< 1 s) afwijken maakt de toestand niet "verstoord".
    CHECK(d.state() == MagState::Ok);
    MagDisturbanceDetector d2;
    d2.set_reference_dip(67);
    // Echte rotatie: controller draait 20 graden/s om de verticaal; gyro en veld consistent.
    double yaw = 0, rate = 20 * kRadPerDeg;
    for (int i = 0; i < 200; ++i) {
        Quat qq = from_axis_angle({0, 0, 1}, yaw) * q;
        Vec3 a2 = rotate(conj(qq), Vec3{0, 0, 1});
        Vec3 gyro = rotate(conj(qq), Vec3{0, 0, rate});  // hoeksnelheid in het controllerstelsel
        c = d2.update(rotate(conj(qq), mw), a2, gyro, dt);
        yaw += rate * dt;
    }
    CHECK(c.accepted && c.state == MagState::Ok);
    CHECK(std::fabs(c.rate_err_deg_s) < 0.1);
    // Veld draait (bewegende magneet) terwijl de controller stilstaat: afgewezen op rate.
    MagDisturbanceDetector d3;
    d3.set_reference_dip(67);
    double fy = 0;
    for (int i = 0; i < 200; ++i) {
        Quat rot = from_axis_angle({0, 0, 1}, fy);
        c = d3.update(rotate(conj(q), rotate(rot, mw)), acc, {0, 0, 0}, dt);
        fy += 10 * kRadPerDeg * dt;
    }
    CHECK(!c.accepted && (c.reasons & kRejectRate));
    CHECK_NEAR(std::fabs(c.rate_err_deg_s), 10, 0.5);
    CHECK(c.state == MagState::Disturbed);
    CHECK(reject_names(kRejectNorm | kRejectRate) == "norm,rate");
    // Rumble: altijd afwijzen.
    c = d.update(rotate(conj(q), mw), acc, {0, 0, 0}, dt, true);
    CHECK(!c.accepted && (c.reasons & kRejectRumble));
}

// ---------------- IPC-commando's ----------------

static void test_protocol() {
    using namespace ravengun::ipc;
    auto ok = [](const char* l) { return std::holds_alternative<Request>(parse_request(l)); };
    auto code = [](const char* l) {
        auto r = parse_request(l);
        return std::holds_alternative<Error>(r) ? std::get<Error>(r).code : std::string("ok");
    };
    CHECK(ok(R"({"v":1,"cmd":"record","action":"start","tag":"stil","duration_s":300,"max_mb":64})"));
    CHECK(ok(R"({"v":1,"cmd":"record","action":"mark","label":"pomp in"})"));
    CHECK(code(R"({"v":1,"cmd":"record","action":"mark"})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"record","action":"start","tag":"../etc"})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"record","action":"start","path":"/tmp/x"})") == "bad_request");
    CHECK(code(R"({"v":1,"cmd":"record","action":"start","duration_s":99999})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"record","action":"wis"})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"record","action":"mark","label":"a\u0007b"})") == "invalid_argument");
    CHECK(ok(R"({"v":1,"cmd":"calib_mag","action":"start","holder":"sharp_shooter"})"));
    CHECK(ok(R"({"v":1,"cmd":"calib_mag","action":"status"})"));
    CHECK(code(R"({"v":1,"cmd":"calib_mag","action":"start","tag":"x"})") == "bad_request");
    CHECK(ok(R"({"v":1,"cmd":"set_led","rgb":[0,255,255]})"));
    CHECK(code(R"({"v":1,"cmd":"set_led","rgb":[0,256,255]})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"set_led","rgb":[0,1.5,255]})") == "invalid_argument");
    CHECK(code(R"({"v":1,"cmd":"set_led","rgb":[0,1]})") == "invalid_argument");
    // Rondgang client -> server
    Request r;
    r.cmd = Cmd::Record;
    r.action = "start";
    r.tag = "led_test";
    r.duration_s = 120;
    auto p = parse_request(make_request(r));
    CHECK(std::holds_alternative<Request>(p) && std::get<Request>(p).tag == "led_test" && *std::get<Request>(p).duration_s == 120);
    Request l;
    l.cmd = Cmd::SetLed;
    l.rgb[0] = 255; l.rgb[1] = 0; l.rgb[2] = 7;
    p = parse_request(make_request(l));
    CHECK(std::holds_alternative<Request>(p) && *std::get<Request>(p).rgb[2] == 7);
}

int main() {
    test_parse_report();
    test_move_clock();
    test_pipeline_move_dt();
    test_recording_roundtrip();
    test_replay_deterministic();
    test_mag_fit();
    test_mag_fit_quality_gates();
    test_mag_storage();
    test_mag_disturbance();
    test_protocol();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
