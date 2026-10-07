// Unittests voor de camera-/visielaag: begrensde wachtrij, frame-intervalstatistiek en de
// boltracker op synthetische YUYV-beelden (getekende schijven met ruis, reflecties, twee
// kandidaten, gedeeltelijke afdekking, verzadiging). Geen hardware en geen echte beelden.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "ravengun/vision/ball_tracker.hpp"
#include "ravengun/vision/frame_timing.hpp"
#include "ravengun/vision/latest_queue.hpp"

using namespace ravengun::vision;

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
#define CHECK_NEAR(a, b, tol)                                                                              \
    do {                                                                                                   \
        ++g_checks;                                                                                        \
        double _a = (a), _b = (b);                                                                         \
        if (!(std::fabs(_a - _b) <= (tol))) {                                                              \
            ++g_failures;                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s = %.6g, verwacht %.6g +- %g\n", __FILE__, __LINE__, #a, _a, \
                         _b, static_cast<double>(tol));                                                    \
        }                                                                                                  \
    } while (0)
#define CHECK_REASON(r, expected)                                                                           \
    do {                                                                                                    \
        ++g_checks;                                                                                         \
        if ((r) != (expected)) {                                                                            \
            ++g_failures;                                                                                   \
            std::fprintf(stderr, "FAIL %s:%d: reden %s, verwacht %s\n", __FILE__, __LINE__, to_string(r),   \
                         to_string(expected));                                                              \
        }                                                                                                   \
    } while (0)

struct Lcg {
    uint64_t s = 777;
    double uniform() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return static_cast<double>(s >> 11) / 9007199254740992.0; }
    double gauss() { double u = 0; for (int i = 0; i < 12; ++i) u += uniform(); return u - 6.0; }
};

// ---------------- Synthetische YUYV-beelden ----------------

struct Yuv { double y, u, v; };
static Yuv rgb_to_yuv(double r, double g, double b) {
    double y = 0.299 * r + 0.587 * g + 0.114 * b;
    return {y, 128 + 0.492 * (b - y), 128 + 0.877 * (r - y)};
}

struct Canvas {
    int w, h;
    std::vector<Yuv> px;
    Canvas(int w_, int h_, Yuv bg) : w(w_), h(h_), px(static_cast<size_t>(w_ * h_), bg) {}
    Yuv& at(int x, int y) { return px[static_cast<size_t>(y * w + x)]; }
    // Harde schijf; pixel hoort erbij als zijn middelpunt binnen de straal ligt.
    void disc(double cx, double cy, double r, Yuv c) {
        for (int y = std::max(0, static_cast<int>(cy - r - 1)); y < std::min(h, static_cast<int>(cy + r + 2)); ++y)
            for (int x = std::max(0, static_cast<int>(cx - r - 1)); x < std::min(w, static_cast<int>(cx + r + 2)); ++x)
                if (std::hypot(x + 0.5 - cx, y + 0.5 - cy) <= r) at(x, y) = c;
    }
    void ellipse(double cx, double cy, double a, double b, Yuv c) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                double dx = (x + 0.5 - cx) / a, dy = (y + 0.5 - cy) / b;
                if (dx * dx + dy * dy <= 1) at(x, y) = c;
            }
    }
    void rect(int x0, int y0, int x1, int y1, Yuv c) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) at(x, y) = c;
    }
    void noise(double sigma_y, double sigma_c, Lcg& rng) {
        for (auto& p : px) { p.y += sigma_y * rng.gauss(); p.u += sigma_c * rng.gauss(); p.v += sigma_c * rng.gauss(); }
    }
    std::vector<uint8_t> yuyv() const {
        std::vector<uint8_t> out(static_cast<size_t>(w * h * 2));
        auto cl = [](double v) { return static_cast<uint8_t>(std::lround(std::fmin(255, std::fmax(0, v)))); };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; x += 2) {
                const Yuv& a = px[static_cast<size_t>(y * w + x)];
                const Yuv& b = px[static_cast<size_t>(y * w + x + 1)];
                uint8_t* o = &out[static_cast<size_t>((y * w + x) * 2)];
                o[0] = cl(a.y); o[1] = cl((a.u + b.u) / 2); o[2] = cl(b.y); o[3] = cl((a.v + b.v) / 2);
            }
        return out;
    }
};

static const Yuv kBg = rgb_to_yuv(70, 65, 60);          // gedempte, licht warme kamer
static const Yuv kMagenta = rgb_to_yuv(200, 40, 200);   // bol (niet verzadigd)
static const Yuv kMagentaDim = rgb_to_yuv(120, 30, 120);
static const Yuv kCyan = rgb_to_yuv(30, 200, 200);
static const Yuv kWhite{255, 128, 128};

static TrackResult run(BallTracker& t, const Canvas& c) {
    std::vector<uint8_t> buf = c.yuyv();
    ImageView v{buf.data(), c.w, c.h, c.w * 2, PixelFormat::YUYV};
    return t.process(v);
}

static TrackerConfig magenta_cfg() {
    TrackerConfig c;
    c.hue_deg = nominal_hue_deg({255, 0, 255});
    return c;
}

// ---------------- Wachtrij ----------------

static void test_queue_basic() {
    LatestQueue<int> q(2);
    CHECK(q.capacity() == 2);
    CHECK(!q.try_pop_latest());
    for (int i = 1; i <= 5; ++i) q.push(i);
    auto c = q.counters();
    CHECK(c.pushed == 5 && c.dropped_full == 3 && c.depth == 2);
    auto v = q.try_pop_latest();
    CHECK(v && *v == 5);  // altijd het nieuwste
    c = q.counters();
    CHECK(c.dropped_stale == 1 && c.depth == 0 && c.popped == 1);
    CHECK(!q.pop_latest(std::chrono::milliseconds(5)));  // time-out zonder element
    // Capaciteit 1: nooit meer dan één element.
    LatestQueue<int> one(1);
    one.push(1); one.push(2);
    CHECK(one.counters().depth == 1 && one.counters().dropped_full == 1);
    CHECK(*one.try_pop_latest() == 2);
    // Capaciteit 0 wordt 1.
    LatestQueue<int> z(0);
    CHECK(z.capacity() == 1);
    // close() maakt wachtenden los en weigert nieuwe elementen.
    q.close();
    q.push(9);
    CHECK(!q.pop_latest(std::chrono::milliseconds(50)));
}

static void test_queue_recycle() {
    LatestQueue<std::vector<uint8_t>> q(2);
    std::vector<uint8_t> a = q.acquire();
    CHECK(a.empty());
    a.assign(1000, 1);
    const uint8_t* ptr = a.data();
    q.push(std::move(a));
    auto b = q.try_pop_latest();
    CHECK(b && b->size() == 1000);
    q.recycle(std::move(*b));
    std::vector<uint8_t> c = q.acquire();
    CHECK(c.data() == ptr);  // dezelfde buffer hergebruikt, geen nieuwe allocatie
}

static void test_queue_threads() {
    // Snelle producent, trage consument: de consument ziet strikt oplopende waarden, de diepte
    // blijft begrensd en elke waarde is ofwel verwerkt, ofwel geteld als weggegooid.
    LatestQueue<int> q(2);
    std::atomic<bool> done{false};
    std::vector<int> seen;
    std::thread cons([&] {
        while (true) {
            auto v = q.pop_latest(std::chrono::milliseconds(20));
            if (v) { seen.push_back(*v); std::this_thread::sleep_for(std::chrono::microseconds(300)); }
            else if (done.load()) break;
        }
    });
    size_t max_depth = 0;
    for (int i = 0; i < 3000; ++i) {
        q.push(i);
        max_depth = std::max(max_depth, q.counters().depth);
        if (i % 100 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    done.store(true);
    cons.join();
    bool inc = true;
    for (size_t i = 1; i < seen.size(); ++i) inc = inc && seen[i] > seen[i - 1];
    CHECK(inc);
    CHECK(max_depth <= 2);
    CHECK(!seen.empty() && seen.back() == 2999);  // het laatste element wordt altijd verwerkt
    auto c = q.counters();
    CHECK(c.pushed == 3000);
    CHECK(c.popped + c.dropped_full + c.dropped_stale + c.depth == 3000);
    CHECK(c.popped == seen.size());
}

// ---------------- Intervalstatistiek ----------------

static void test_ring_percentile() {
    Ring r(5);
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) r.add(v);
    CHECK_NEAR(r.percentile(0.5), 3, 1e-12);
    CHECK_NEAR(r.percentile(0.95), 4.8, 1e-12);  // lineair geïnterpoleerd
    CHECK_NEAR(r.percentile(0.0), 1, 1e-12);
    CHECK_NEAR(r.max(), 5, 1e-12);
    CHECK_NEAR(r.mean(), 3, 1e-12);
    r.add(100);  // overschrijft de oudste (1)
    CHECK(r.size() == 5);
    CHECK_NEAR(r.mean(), (2 + 3 + 4 + 5 + 100) / 5.0, 1e-12);
    Ring e;
    CHECK(e.percentile(0.5) == 0);
}

static void test_frame_timing() {
    FrameTiming t;
    const int64_t p = 16'666'667;  // 60 fps
    int64_t ts = 1'000'000'000;
    uint32_t seq = 10;
    for (int i = 0; i < 600; ++i) {
        // Twee frames gemist rond i == 300 (volgnummer springt 3, tijd 3 perioden).
        if (i == 300) { seq += 2; ts += 2 * p; }
        t.on_frame(ts, seq, ts + 200'000);
        t.on_processed(1'500'000, 400);
        ts += p;
        ++seq;
    }
    CHECK(t.frames == 600);
    CHECK(t.missed == 2);
    CHECK(t.gap_events == 1);
    CHECK(t.non_monotonic == 0);
    CHECK_NEAR(t.intervals_ms().percentile(0.5), 16.667, 0.001);
    CHECK_NEAR(t.intervals_ms().max(), 50.0, 0.001);
    // fps over alles: 599 intervallen over 601 perioden (twee gemiste frames tellen als tijd).
    CHECK_NEAR(t.fps_total(), 599.0 / (601 * p / 1e9), 1e-6);
    CHECK_NEAR(t.fps_window(2.0), 60.0, 0.05);  // na het gat: weer 60
    CHECK_NEAR(t.dequeue_delay_ms().percentile(0.95), 0.2, 1e-9);
    CHECK_NEAR(t.age_ms().percentile(0.5), 1.5, 1e-9);
    CHECK_NEAR(t.processing_us().percentile(0.5), 400, 1e-9);
    // Bakken: 598 intervallen van 16,67 ms in [12,18) en één van 50 ms in [50,...).
    CHECK(t.buckets[4] == 598);
    CHECK(t.buckets[7] == 1);
    // Niet-monotone tijd en teruglopend volgnummer worden geteld, niet als interval gebruikt.
    t.on_frame(ts - 5 * p, 5, ts);
    CHECK(t.non_monotonic == 1 && t.sequence_reset == 1);
    CHECK(t.intervals_ms().size() == 599);
    t.reset();
    CHECK(t.frames == 0 && t.fps_total() == 0);
}

// ---------------- Boltracker ----------------

static void test_hue() {
    CHECK_NEAR(nominal_hue_deg({255, 0, 255}), 60.7, 0.2);
    CHECK_NEAR(nominal_hue_deg({0, 255, 255}), -76.5, 0.3);
    Rgb c;
    CHECK(led_color_by_name("cyaan", c) && c.g == 255 && c.r == 0);
    CHECK(!led_color_by_name("paars-ish", c));
}

static void test_single_disc_with_noise() {
    BallTracker t(magenta_cfg());
    Lcg rng;
    Canvas c(640, 480, kBg);
    c.disc(321.3, 200.7, 10, kMagenta);
    c.noise(4, 3, rng);
    TrackResult r = run(t, c);
    CHECK(!r.valid);
    CHECK_REASON(r.reason, Reason::AcquirePending);  // eerste frame: wacht op bevestiging
    r = run(t, c);
    CHECK(r.valid);
    CHECK_REASON(r.reason, Reason::None);
    CHECK_NEAR(r.x_px, 321.3, 0.3);
    CHECK_NEAR(r.y_px, 200.7, 0.3);
    CHECK_NEAR(r.radius_px, 10, 0.5);
    CHECK_NEAR(r.x_norm, 321.3 / 640, 0.001);
    CHECK(r.candidates == 1);
    CHECK(r.full_frame_searched);
    r = run(t, c);
    CHECK(r.valid && r.used_roi && !r.full_frame_searched);  // daarna in de ROI
}

static void test_saturated_core() {
    // Overbelichte kern (wit) binnen een gekleurde rand: telt mee voor vorm en positie.
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kBg);
    c.disc(100, 100, 12, kMagenta);
    c.disc(100, 100, 7, kWhite);
    run(t, c);
    TrackResult r = run(t, c);
    CHECK(r.valid);
    CHECK_NEAR(r.radius_px, 12, 0.6);
    CHECK_NEAR(r.x_px, 100, 0.3);
    CHECK(r.sat_px > 100);
}

static void test_white_lamp_rejected() {
    // Witte lamp (verzadigd, geen kleur) en een bol: alleen de bol is kandidaat.
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kBg);
    c.disc(500, 120, 25, kWhite);
    c.disc(200, 300, 9, kMagenta);
    run(t, c);
    TrackResult r = run(t, c);
    CHECK(r.valid);
    CHECK_NEAR(r.x_px, 200, 0.4);
    CHECK(r.candidates == 1);
    bool lamp_rejected = false;
    for (const Blob& b : t.last_blobs()) if (b.area > 1500 && b.verdict == Reason::TooLittleColor) lamp_rejected = true;
    CHECK(lamp_rejected);
}

static void test_two_candidates() {
    // Twee gelijke bollen zonder voorgeschiedenis: niet de grootste kiezen, maar ongeldig.
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kBg);
    c.disc(150, 200, 10, kMagenta);
    c.disc(450, 220, 12, kMagenta);
    for (int i = 0; i < 5; ++i) {
        TrackResult r = run(t, c);
        CHECK(!r.valid);
        CHECK_REASON(r.reason, Reason::MultipleCandidates);
        CHECK(r.candidates == 2);
    }
    // Tijdens volgen verschijnt een tweede bol ver weg: de gevolgde blijft gekozen.
    BallTracker t2(magenta_cfg());
    Canvas one(640, 480, kBg);
    one.disc(150, 200, 10, kMagenta);
    run(t2, one);
    CHECK(run(t2, one).valid);
    TrackResult r = run(t2, c);
    CHECK(r.valid);
    CHECK_NEAR(r.x_px, 150, 0.4);
}

static void test_reflection() {
    // Reflectie van de bol op een glanzend vlak: langgerekte, minder heldere vlek met dezelfde
    // tint. Vormcriterium verwerpt hem, dus de echte bol is de enige kandidaat.
    BallTracker t(magenta_cfg());
    Lcg rng;
    Canvas c(640, 480, kBg);
    c.disc(300, 150, 10, kMagenta);
    c.ellipse(300, 330, 30, 6, kMagentaDim);
    c.noise(3, 2, rng);
    run(t, c);
    TrackResult r = run(t, c);
    CHECK(r.valid);
    CHECK_NEAR(r.y_px, 150, 0.4);
    bool refl = false;
    for (const Blob& b : t.last_blobs()) if (b.cy > 300 && b.verdict == Reason::NotRound) refl = true;
    CHECK(refl);
}

static void test_partial_occlusion() {
    BallTracker t(magenta_cfg());
    Canvas full(640, 480, kBg);
    full.disc(320, 240, 12, kMagenta);
    run(t, full);
    CHECK(run(t, full).valid);
    // Kwart afgedekt (rechts): nog rond genoeg, zwaartepunt schuift naar links (gedocumenteerd).
    Canvas q = full;
    q.rect(326, 200, 360, 280, kBg);
    TrackResult r = run(t, q);
    CHECK(r.valid);
    CHECK(r.x_px < 320);
    CHECK(r.radius_px < 12);
    // Ruim de helft afgedekt: niet meer rond, ongeldig met reden.
    Canvas h = full;
    h.rect(318, 200, 360, 280, kBg);
    r = run(t, h);
    CHECK(!r.valid);
    CHECK_REASON(r.reason, Reason::NotRound);
    CHECK(r.state == TrackState::Tracking);  // nog niet verloren
    // Afdekking weg: meteen weer geldig (nog binnen de verliesgrens).
    r = run(t, full);
    CHECK(r.valid);
    CHECK_NEAR(r.x_px, 320, 0.4);
}

static void test_saturated_frame() {
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kWhite);
    for (int i = 0; i < 3; ++i) {
        TrackResult r = run(t, c);
        CHECK(!r.valid);
        CHECK_REASON(r.reason, Reason::TooLittleColor);
    }
    std::vector<uint8_t> buf = c.yuyv();
    SceneStats s = t.scene_stats({buf.data(), 640, 480, 1280, PixelFormat::YUYV});
    CHECK_NEAR(s.sat_frac, 1.0, 1e-9);
    CHECK_NEAR(s.mean_y, 255, 1e-9);
}

static void test_too_small_and_none() {
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kBg);
    c.disc(100, 100, 1.3, kMagenta);  // 4 px
    TrackResult r = run(t, c);
    CHECK(!r.valid);
    CHECK_REASON(r.reason, Reason::TooSmall);
    Canvas e(640, 480, kBg);
    r = run(t, e);
    CHECK_REASON(r.reason, Reason::NoCandidate);
    // Cyaan bol bij magenta-instelling: wordt niet gezien.
    Canvas cy(640, 480, kBg);
    cy.disc(200, 200, 10, kCyan);
    r = run(t, cy);
    CHECK_REASON(r.reason, Reason::NoCandidate);
    // Kapot frame.
    ImageView bad{nullptr, 640, 480, 1280, PixelFormat::YUYV};
    CHECK_REASON(t.process(bad).reason, Reason::BadFrame);
}

static void test_jump_and_reacquire() {
    TrackerConfig cfg = magenta_cfg();
    BallTracker t(cfg);
    Canvas a(640, 480, kBg);
    a.disc(100, 100, 10, kMagenta);
    run(t, a);
    CHECK(run(t, a).valid);
    // Sprong van ~400 px: eerst onbevestigd, het frame daarna geldig op de nieuwe plek.
    Canvas b(640, 480, kBg);
    b.disc(500, 380, 10, kMagenta);
    TrackResult r = run(t, b);
    CHECK(!r.valid);
    CHECK_REASON(r.reason, Reason::JumpUnconfirmed);
    r = run(t, b);
    CHECK(r.valid);
    CHECK_NEAR(r.x_px, 500, 0.4);
    // Eén frame een uitbijter elders, daarna terug: de uitbijter wordt nooit geldig.
    r = run(t, a);
    CHECK(!r.valid);
    r = run(t, b);
    CHECK(r.valid && std::fabs(r.x_px - 500) < 1);
    // Verlies: lost_after_frames lege frames, daarna zoeken; terugkeer vraagt bevestiging.
    Canvas e(640, 480, kBg);
    for (int i = 0; i < cfg.lost_after_frames; ++i) r = run(t, e);
    CHECK(r.state == TrackState::Searching);
    r = run(t, a);
    CHECK_REASON(r.reason, Reason::AcquirePending);
    r = run(t, a);
    CHECK(r.valid && r.state == TrackState::Tracking);
}

static void test_motion() {
    // Bol beweegt 9 px per frame met ruis: blijft geldig via de ROI met snelheidsvoorspelling.
    BallTracker t(magenta_cfg());
    Lcg rng;
    int valid = 0, roi = 0;
    double max_err = 0;
    for (int i = 0; i < 50; ++i) {
        Canvas c(640, 480, kBg);
        double x = 80 + 9.0 * i, y = 240 + 40 * std::sin(i * 0.2);
        c.disc(x, y, 9, kMagenta);
        c.noise(4, 3, rng);
        TrackResult r = run(t, c);
        if (r.valid) {
            ++valid;
            roi += r.used_roi;
            max_err = std::max(max_err, std::hypot(r.x_px - x, r.y_px - y));
        }
    }
    CHECK(valid == 49);  // alleen het eerste frame wacht op bevestiging
    CHECK(roi >= 47);
    CHECK(max_err < 0.5);
}

static void test_scene_hue_hist() {
    BallTracker t(magenta_cfg());
    Canvas c(640, 480, kMagenta);
    std::vector<uint8_t> buf = c.yuyv();
    SceneStats s = t.scene_stats({buf.data(), 640, 480, 1280, PixelFormat::YUYV}, 4);
    // Magenta ~61 graden: sector [60,90) = index 8.
    CHECK_NEAR(s.hue_hist_frac[8], 1.0, 1e-9);
    CHECK_NEAR(s.color_match_frac, 1.0, 1e-9);
    CHECK(s.mean_r > 150 && s.mean_b > 150 && s.mean_g < 80);
}

static void test_small_resolution() {
    // 320x240: zelfde bol op halve schaal (straal 5) wordt nog gevonden.
    BallTracker t(magenta_cfg());
    Lcg rng;
    Canvas c(320, 240, kBg);
    c.disc(160.4, 120.2, 5, kMagenta);
    c.noise(4, 3, rng);
    run(t, c);
    TrackResult r = run(t, c);
    CHECK(r.valid);
    CHECK_NEAR(r.x_px, 160.4, 0.4);
    CHECK_NEAR(r.radius_px, 5, 0.5);
}

int main() {
    test_queue_basic();
    test_queue_recycle();
    test_queue_threads();
    test_ring_percentile();
    test_frame_timing();
    test_hue();
    test_single_disc_with_noise();
    test_saturated_core();
    test_white_lamp_rejected();
    test_two_candidates();
    test_reflection();
    test_partial_occlusion();
    test_saturated_frame();
    test_too_small_and_none();
    test_jump_and_reacquire();
    test_motion();
    test_scene_hue_hist();
    test_small_resolution();
    std::printf("vision-tests: %d controles, %d fouten\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
