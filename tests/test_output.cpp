// Unittests voor de outputbackend: coördinaattransformaties (inversie, schaal, clamp, letterboxing
// precies één keer), arm/disarm, het vrijgeven van knoppen bij disarm/fouten/shutdown, testpatronen
// en de nieuwe IPC-commando's. Geen hardware: een nep-apparaat registreert wat er geschreven wordt.
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "ravengun/ipc_protocol.hpp"
#include "ravengun/output/aim_clock.hpp"
#include "ravengun/output/gun_output.hpp"
#include "ravengun/output/gun_transform.hpp"
#include "ravengun/core/navcon.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/output/key_table.hpp"
#include "ravengun/output/test_pattern.hpp"
#include "ravengun/output/uinput_kbd.hpp"
#include "ravengun/output/key_mux.hpp"
#include "ravengun/output/uinput_gun.hpp"

using namespace ravengun;
using namespace ravengun::output;

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

// Nep-apparaat: onthoudt events en welke knoppen volgens de schrijfacties nu ingedrukt zijn.
struct FakeDevice : GunDevice {
    bool exists = false;
    bool fail_create = false, fail_pos = false, fail_btn = false;
    int creates = 0, destroys = 0;
    std::vector<std::string> log;
    std::set<int> pressed;  // zoals de kernel het zou zien
    int last_x = -1, last_y = -1;

    bool create(std::string& err) override {
        if (exists) return true;
        if (fail_create) { err = "nep: create mislukt"; return false; }
        exists = true;
        ++creates;
        return true;
    }
    void destroy() override {
        if (!exists) return;
        exists = false;
        ++destroys;
        pressed.clear();  // de kernel geeft knoppen vrij bij UI_DEV_DESTROY
        log.push_back("destroy");
    }
    bool created() const override { return exists; }
    bool write_position(int x, int y) override {
        if (!exists || fail_pos) return false;
        last_x = x;
        last_y = y;
        log.push_back("pos " + std::to_string(x) + " " + std::to_string(y));
        return true;
    }
    bool write_button(GunButton b, bool down) override {
        if (!exists || fail_btn) return false;
        if (down) pressed.insert(static_cast<int>(b)); else pressed.erase(static_cast<int>(b));
        log.push_back(std::string(down ? "down " : "up ") + to_string(b));
        return true;
    }
    int count(const std::string& prefix) const {
        int n = 0;
        for (const auto& l : log) if (l.rfind(prefix, 0) == 0) ++n;
        return n;
    }
};

// ---------------- Transformatie ----------------

static void test_transform_basic() {
    GunTransform t;
    auto mid = to_abs(t, 0.5, 0.5);
    CHECK(mid && mid->x == 16384 && mid->y == 16384);  // lround(0.5 * 32767)
    auto tl = to_abs(t, 0, 0);
    CHECK(tl && tl->x == 0 && tl->y == 0 && !tl->clamped_x && !tl->clamped_y);
    auto br = to_abs(t, 1, 1);
    CHECK(br && br->x == kAbsMax && br->y == kAbsMax && !br->clamped_x);
    auto q = to_abs(t, 0.25, 0.75);
    CHECK(q && q->x == 8192 && q->y == 24575);  // lround(8191.75), lround(24575.25)
    // Lineair: halve afstand geeft halve waarde (binnen afronding).
    for (double u : {0.0, 0.1, 0.37, 0.5, 0.91, 1.0}) {
        auto p = to_abs(t, u, 1 - u);
        CHECK(p && std::abs(p->x - u * kAbsMax) <= 0.5 + 1e-9 && std::abs(p->y - (1 - u) * kAbsMax) <= 0.5 + 1e-9);
    }
}

static void test_transform_invert() {
    GunTransform t;
    t.invert_x = true;
    auto a = to_abs(t, 0.0, 0.0);
    CHECK(a && a->x == kAbsMax && a->y == 0);
    auto b = to_abs(t, 0.25, 0.25);
    CHECK(b && b->x == 24575 && b->y == 8192);
    auto m = to_abs(t, 0.5, 0.5);
    CHECK(m && m->x == 16384 && m->y == 16384);  // midden blijft midden
    t.invert_y = true;
    auto c = to_abs(t, 0.0, 0.0);
    CHECK(c && c->x == kAbsMax && c->y == kAbsMax);
    // Inversie is een involutie: tweemaal spiegelen (via twee transformaties) geeft de originele waarde terug.
    GunTransform plain;
    auto p1 = to_abs(t, 0.3, 0.8);
    auto p2 = to_abs(plain, 1 - 0.3, 1 - 0.8);
    CHECK(p1 && p2 && p1->x == p2->x && p1->y == p2->y);
}

static void test_transform_scale_offset_clamp() {
    GunTransform t;
    t.scale_x = 2;  // versterking rond het midden: 0.75 -> 1.0
    auto a = to_abs(t, 0.75, 0.5);
    CHECK(a && a->x == kAbsMax && !a->clamped_x);
    auto b = to_abs(t, 0.9, 0.5);  // 0.5 + 0.4*2 = 1.3 -> clamp
    CHECK(b && b->x == kAbsMax && b->clamped_x && !b->clamped_y);
    auto c = to_abs(t, 0.0, 0.5);  // 0.5 - 1.0 = -0.5 -> clamp
    CHECK(c && c->x == 0 && c->clamped_x);
    GunTransform o;
    o.offset_y = 0.1;
    auto d = to_abs(o, 0.5, 0.5);
    CHECK(d && d->y == lround(0.6 * kAbsMax) && d->x == 16384);
    o.offset_y = -0.7;
    auto e = to_abs(o, 0.5, 0.5);
    CHECK(e && e->y == 0 && e->clamped_y);
    // Buiten het scherm (ongeclampte aim) wordt precies één keer geclampt.
    GunTransform id;
    auto f = to_abs(id, -0.4, 1.7);
    CHECK(f && f->x == 0 && f->y == kAbsMax && f->clamped_x && f->clamped_y);
    // Schaal om het midden is neutraal in het midden.
    auto g = to_abs(t, 0.5, 0.5);
    CHECK(g && g->x == 16384);
}

static void test_transform_invalid() {
    GunTransform t;
    CHECK(!to_abs(t, NAN, 0.5));
    CHECK(!to_abs(t, 0.5, INFINITY));
    GunTransform bad = t;
    bad.scale_x = 0;
    CHECK(!to_abs(bad, 0.5, 0.5));
    bad = t;
    bad.viewport.x1 = bad.viewport.x0;
    CHECK(!to_abs(bad, 0.5, 0.5));
    bad = t;
    bad.abs_max = 0;
    CHECK(!to_abs(bad, 0.5, 0.5));
    bad = t;
    bad.offset_x = NAN;
    CHECK(!to_abs(bad, 0.5, 0.5));
}

static void test_letterbox() {
    // 4:3 content op 16:9 scherm: pillarbox (balken links/rechts), breedte 0.75.
    Viewport v = letterbox_viewport(16.0 / 9, 4.0 / 3);
    CHECK_NEAR(v.x0, 0.125, 1e-12); CHECK_NEAR(v.x1, 0.875, 1e-12);
    CHECK_NEAR(v.y0, 0, 1e-12); CHECK_NEAR(v.y1, 1, 1e-12);
    // 21:9 content op 16:9: letterbox (balken boven/onder).
    Viewport w = letterbox_viewport(16.0 / 9, 21.0 / 9);
    CHECK_NEAR(w.x0, 0, 1e-12); CHECK_NEAR(w.x1, 1, 1e-12);
    CHECK_NEAR(w.y0, (1 - (16.0 / 9) / (21.0 / 9)) / 2, 1e-12);
    CHECK_NEAR(w.y1 - w.y0, (16.0 / 9) / (21.0 / 9), 1e-12);
    // Gelijke aspecten of ongeldige invoer: volledig scherm.
    Viewport e = letterbox_viewport(16.0 / 9, 16.0 / 9);
    CHECK(e.x0 == 0 && e.x1 == 1 && e.y0 == 0 && e.y1 == 1);
    Viewport n = letterbox_viewport(0, 4.0 / 3);
    CHECK(n.x0 == 0 && n.x1 == 1);
    CHECK(v.valid() && w.valid() && e.valid());

    // Letterboxing precies één keer: de rand van de viewport is 0, de andere rand abs_max, het
    // midden blijft het midden, en er is geen tweede aspectcorrectie in de keten (lineair).
    GunTransform t;
    t.viewport = v;
    auto l = to_abs(t, v.x0, 0.5);
    auto r = to_abs(t, v.x1, 0.5);
    auto m = to_abs(t, 0.5, 0.5);
    CHECK(l && l->x == 0 && !l->clamped_x);
    CHECK(r && r->x == kAbsMax && !r->clamped_x);
    CHECK(m && m->x == 16384 && m->y == 16384);
    // Het beeldvlak is lineair: een kwart van de viewport geeft een kwart van het bereik.
    auto q = to_abs(t, v.x0 + 0.25 * (v.x1 - v.x0), 0.5);
    CHECK(q && std::abs(q->x - 0.25 * kAbsMax) <= 0.5 + 1e-9);
    // Punten op de zwarte balken worden geclampt, niet doorgerekend.
    auto bar = to_abs(t, 0.02, 0.5);
    CHECK(bar && bar->x == 0 && bar->clamped_x);
    // Y is onaangetast bij een pillarbox.
    auto y = to_abs(t, 0.5, 0.25);
    CHECK(y && std::abs(y->y - 0.25 * kAbsMax) <= 0.5 + 1e-9);
    // Combinatie met inversie: inversie werkt in viewport-ruimte (precies één keer).
    t.invert_x = true;
    auto li = to_abs(t, v.x0, 0.5);
    CHECK(li && li->x == kAbsMax);
    // Verticale letterbox.
    GunTransform u;
    u.viewport = w;
    auto top = to_abs(u, 0.5, w.y0);
    auto bot = to_abs(u, 0.5, w.y1);
    CHECK(top && top->y == 0 && bot && bot->y == kAbsMax);
    CHECK(to_abs(u, 0.5, 0.5)->y == 16384);
}

// ---------------- GunOutput: arm/disarm en vrijgeven ----------------

static void test_default_disarmed() {
    FakeDevice d;
    GunOutput o(d);
    CHECK(!o.armed());
    CHECK(!o.set_position(0.5, 0.5));
    CHECK(!o.set_button(GunButton::Trigger, true));
    CHECK(d.log.empty());
    CHECK(!d.exists);  // het apparaat bestaat pas na arm
    CHECK(o.stats().rejected == 2);
}

static void test_arm_and_events() {
    FakeDevice d;
    GunOutput o(d);
    std::string err;
    CHECK(o.arm(err));
    CHECK(o.armed() && d.exists && d.creates == 1);
    CHECK(d.log.empty());  // arm zelf stuurt geen events
    CHECK(o.set_position(0.5, 0.5));
    CHECK(d.last_x == 16384 && d.last_y == 16384);
    CHECK(o.set_position(0.5, 0.5));  // dubbel: geen nieuw event
    CHECK(d.count("pos") == 1);
    CHECK(o.set_position(0.0, 1.0));
    CHECK(d.count("pos") == 2 && d.last_x == 0 && d.last_y == kAbsMax);
    CHECK(o.set_button(GunButton::Trigger, true));
    CHECK(o.set_button(GunButton::Trigger, true));  // dubbel: geen event
    CHECK(d.count("down") == 1 && d.pressed.count(0));
    CHECK(o.set_button(GunButton::Trigger, false));
    CHECK(d.pressed.empty());
    CHECK(o.arm(err) && d.creates == 1);  // opnieuw arm is idempotent
    CHECK(!o.set_position(NAN, 0.5));     // ongeldig: geen event
    CHECK(d.count("pos") == 2);
}

static void test_disarm_releases_buttons() {
    FakeDevice d;
    GunOutput o(d);
    std::string err;
    CHECK(o.arm(err));
    o.set_button(GunButton::Trigger, true);
    o.set_button(GunButton::Grenade, true);
    o.set_button(GunButton::Aux2, true);
    CHECK(d.pressed.size() == 3);
    o.disarm();
    CHECK(d.pressed.empty());               // alle knoppen vrijgegeven
    CHECK(d.count("up") == 3);
    CHECK(!o.armed());
    CHECK(d.exists);                        // apparaat blijft bestaan (stabiele binding)
    CHECK(!o.button_down(GunButton::Trigger));
    size_t n = d.log.size();
    CHECK(!o.set_position(0.1, 0.1));       // na disarm: geen events
    CHECK(!o.set_button(GunButton::Trigger, true));
    CHECK(d.log.size() == n);
    CHECK(o.stats().releases == 1);
    // Opnieuw arm: schone toestand, eerste positie wordt altijd verstuurd.
    CHECK(o.arm(err));
    CHECK(o.set_position(0.5, 0.5));
    CHECK(d.log.size() == n + 1);
    CHECK(d.pressed.empty());
}

static void test_release_all_keeps_armed() {
    FakeDevice d;
    GunOutput o(d);
    std::string err;
    o.arm(err);
    o.set_button(GunButton::Reload, true);
    o.release_all();  // bronuitval
    CHECK(d.pressed.empty() && o.armed());
    CHECK(o.set_button(GunButton::Reload, true));  // werkt daarna weer
    CHECK(d.pressed.count(static_cast<int>(GunButton::Reload)));
}

static void test_write_error_releases() {
    // Positie-schrijffout terwijl een knop ingedrukt is: knoppen vrij, apparaat weg, disarmed.
    {
        FakeDevice d;
        GunOutput o(d);
        std::string err;
        o.arm(err);
        o.set_button(GunButton::Trigger, true);
        d.fail_pos = true;
        CHECK(!o.set_position(0.3, 0.3));
        CHECK(!o.armed());
        CHECK(!o.button_down(GunButton::Trigger));
        CHECK(d.pressed.empty());
        CHECK(!d.exists && d.destroys == 1);
        CHECK(o.stats().write_errors >= 1);
        CHECK(!o.last_error().empty());
        // Herstel: nieuwe arm maakt het apparaat opnieuw aan.
        d.fail_pos = false;
        CHECK(o.arm(err) && d.exists && d.creates == 2);
        CHECK(o.last_error().empty());
        CHECK(o.set_position(0.3, 0.3));
    }
    // Knop-schrijffout bij het loslaten van de trigger.
    {
        FakeDevice d;
        GunOutput o(d);
        std::string err;
        o.arm(err);
        o.set_button(GunButton::Trigger, true);
        o.set_button(GunButton::Reload, true);
        d.fail_btn = true;
        CHECK(!o.set_button(GunButton::Trigger, false));
        CHECK(!o.armed());
        d.fail_btn = false;
        CHECK(d.pressed.empty());  // device is vernietigd; kernel geeft alles vrij
        CHECK(!o.button_down(GunButton::Trigger) && !o.button_down(GunButton::Reload));
    }
    // Een mislukte create laat de uitvoer disarmed.
    {
        FakeDevice d;
        d.fail_create = true;
        GunOutput o(d);
        std::string err;
        CHECK(!o.arm(err));
        CHECK(!err.empty() && !o.armed() && !d.exists);
        CHECK(!o.set_position(0.5, 0.5));
        CHECK(d.log.empty());
    }
}

static void test_shutdown_and_destructor_release() {
    FakeDevice d;
    {
        GunOutput o(d);
        std::string err;
        o.arm(err);
        o.set_button(GunButton::Trigger, true);
        o.set_button(GunButton::Aux1, true);
        CHECK(d.pressed.size() == 2);
        // geen expliciete shutdown: de destructor moet vrijgeven
    }
    CHECK(d.pressed.empty());
    CHECK(d.count("up") == 2);  // eerst netjes los, dan pas destroy
    CHECK(!d.exists);
    // Volgorde: up-events komen vóór destroy.
    size_t last_up = 0, destroy_at = 0;
    for (size_t i = 0; i < d.log.size(); ++i) {
        if (d.log[i].rfind("up", 0) == 0) last_up = i;
        if (d.log[i] == "destroy") destroy_at = i;
    }
    CHECK(last_up < destroy_at);
}

static void test_transform_applied_in_output() {
    FakeDevice d;
    GunOutput o(d);
    GunTransform t;
    t.invert_y = true;
    t.viewport = letterbox_viewport(16.0 / 9, 4.0 / 3);
    o.set_transform(t);
    std::string err;
    o.arm(err);
    o.set_position(0.5, 0.0);
    CHECK(d.last_x == 16384 && d.last_y == kAbsMax);
    o.set_position(t.viewport.x0, 0.5);
    CHECK(d.last_x == 0);
    // Transformatie wisselen herstelt deduplicatie (nieuwe waarde wordt verstuurd).
    GunTransform id;
    size_t n = d.log.size();
    o.set_transform(id);
    o.set_position(t.viewport.x0, 0.5);
    CHECK(d.log.size() == n + 1);
}

static void test_button_names() {
    for (int i = 0; i < kGunButtonCount; ++i) {
        GunButton b;
        CHECK(parse_button(to_string(static_cast<GunButton>(i)), b) && static_cast<int>(b) == i);
    }
    GunButton b;
    CHECK(!parse_button("start", b));
}

// ---------------- Testpatronen ----------------

static void test_patterns() {
    std::string err;
    PatternParams p;
    p.kind = PatternKind::Center;
    auto c = generate_pattern(p, err);
    CHECK(c.size() == 1 && c[0].x == 0.5 && c[0].y == 0.5);

    p.kind = PatternKind::Corners;
    p.dwell_s = 1.5;
    auto k = generate_pattern(p, err);
    CHECK(k.size() == 4);
    CHECK(k[0].x == 0 && k[0].y == 0 && k[1].x == 1 && k[1].y == 0 && k[2].x == 1 && k[2].y == 1 && k[3].x == 0 && k[3].y == 1);
    CHECK_NEAR(k[3].t_s, 4.5, 1e-12);
    CHECK_NEAR(pattern_length_s(p, k), 6.0, 1e-12);

    p.kind = PatternKind::Grid;
    p.dwell_s = 1;
    auto g = generate_pattern(p, err);
    CHECK(g.size() == 9);
    std::set<std::pair<int, int>> uniq;
    for (const auto& s : g) {
        CHECK(s.x >= 0 && s.x <= 1 && s.y >= 0 && s.y <= 1);
        uniq.insert({static_cast<int>(std::lround(s.x * 1000)), static_cast<int>(std::lround(s.y * 1000))});
    }
    CHECK(uniq.size() == 9);
    CHECK_NEAR(g[0].x, 0.1, 1e-12); CHECK_NEAR(g[4].x, 0.5, 1e-12); CHECK_NEAR(g[4].y, 0.5, 1e-12); CHECK_NEAR(g[8].x, 0.9, 1e-12);
    p.margin = 0.2;
    auto g2 = generate_pattern(p, err);
    CHECK_NEAR(g2[0].x, 0.2, 1e-12);

    p = PatternParams{};
    p.kind = PatternKind::Sweep;
    p.duration_s = 6;
    p.rate_hz = 100;
    auto s = generate_pattern(p, err);
    CHECK(s.size() == 601);
    bool mono = true, inside = true;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && !(s[i].t_s > s[i - 1].t_s)) mono = false;
        if (!(s[i].x >= 0 && s[i].x <= 1 && s[i].y >= 0 && s[i].y <= 1)) inside = false;
    }
    CHECK(mono && inside);
    CHECK_NEAR(s.back().t_s, 6.0, 1e-9);
    // Eerste been horizontaal (y = 0,5): midden -> marge links -> marge rechts -> midden (200 stappen).
    CHECK_NEAR(s[0].x, 0.5, 1e-12); CHECK_NEAR(s[0].y, 0.5, 1e-12);
    CHECK_NEAR(s[50].x, 0.05, 1e-9);
    CHECK_NEAR(s[150].x, 0.95, 1e-9);
    CHECK_NEAR(s[200].x, 0.5, 1e-9);
    // Tweede been verticaal (x = 0,5): bovenmarge bij 1/4, ondermarge bij 3/4 van het been.
    CHECK_NEAR(s[250].y, 0.05, 1e-9); CHECK_NEAR(s[350].y, 0.95, 1e-9); CHECK_NEAR(s[250].x, 0.5, 1e-12);
    // Maximale stapgrootte (vloeiendheid): geen sprongen in het synthetische pad.
    double maxstep = 0;
    for (size_t i = 1; i < s.size(); ++i) maxstep = std::max(maxstep, std::hypot(s[i].x - s[i - 1].x, s[i].y - s[i - 1].y));
    CHECK(maxstep < 0.03);

    p = PatternParams{};
    p.kind = PatternKind::Point;
    p.x = 0.25; p.y = 0.75;
    auto pt = generate_pattern(p, err);
    CHECK(pt.size() == 1 && pt[0].x == 0.25 && pt[0].y == 0.75);
    p.x = -0.5;   // off the screen: allowed (aim off-screen test), the output clamps it
    auto off = generate_pattern(p, err);
    CHECK(off.size() == 1 && off[0].x == -0.5 && off[0].y == 0.75);
    p.x = 3.0;
    CHECK(generate_pattern(p, err).empty() && !err.empty());
    p = PatternParams{};
    p.kind = PatternKind::Corners;
    p.dwell_s = 0;
    CHECK(generate_pattern(p, err).empty());
    p = PatternParams{};
    p.kind = PatternKind::Sweep;
    p.duration_s = 0.1;
    CHECK(generate_pattern(p, err).empty());

    PatternKind kk;
    for (const char* n : {"center", "corners", "grid", "sweep", "point"}) CHECK(parse_pattern(n, kk) && std::string(to_string(kk)) == n);
    CHECK(!parse_pattern("stop", kk));
}

// De volledige keten: patroon -> GunOutput -> nep-apparaat. Elk gridpunt eindigt op een eigen abs-paar.
static void test_pattern_through_output() {
    FakeDevice d;
    GunOutput o(d);
    std::string err;
    o.arm(err);
    PatternParams p;
    p.kind = PatternKind::Grid;
    auto steps = generate_pattern(p, err);
    for (const auto& s : steps) o.set_position(s.x, s.y);
    CHECK(d.count("pos") == 9);
    o.disarm();
    CHECK(d.pressed.empty());
}

// ---------------- IPC ----------------

static ipc::Request parse_ok(const std::string& line, bool& ok) {
    auto r = ipc::parse_request(line);
    ok = std::holds_alternative<ipc::Request>(r);
    return ok ? std::get<ipc::Request>(r) : ipc::Request{};
}
static std::string parse_err(const std::string& line) {
    auto r = ipc::parse_request(line);
    if (auto* e = std::get_if<ipc::Error>(&r)) return e->code;
    return "";
}

static void test_ipc() {
    bool ok;
    auto a = parse_ok(R"({"v":1,"cmd":"arm"})", ok);
    CHECK(ok && a.cmd == ipc::Cmd::Arm);
    CHECK(parse_ok(R"({"v":1,"cmd":"disarm"})", ok).cmd == ipc::Cmd::Disarm && ok);
    CHECK(parse_ok(R"({"v":1,"cmd":"output"})", ok).cmd == ipc::Cmd::Output && ok);
    CHECK(parse_err(R"({"v":1,"cmd":"arm","x":1})") == "bad_request");

    auto t = parse_ok(R"({"v":1,"cmd":"test_pattern","pattern":"grid","dwell_ms":500,"margin":0.2})", ok);
    CHECK(ok && t.cmd == ipc::Cmd::TestPattern && t.pattern == "grid" && *t.dwell_ms == 500 && *t.margin == 0.2);
    auto pt = parse_ok(R"({"v":1,"cmd":"test_pattern","pattern":"point","x":0.1,"y":0.9})", ok);
    CHECK(ok && *pt.x == 0.1 && *pt.y == 0.9);
    CHECK(parse_err(R"({"v":1,"cmd":"test_pattern"})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"test_pattern","pattern":"grid","dwell_ms":1})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"test_pattern","pattern":"point","x":2.5})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"test_pattern","pattern":5})") == "invalid_argument");

    auto b = parse_ok(R"({"v":1,"cmd":"output_button","button":"trigger"})", ok);
    CHECK(ok && b.button == "trigger" && b.action == "tap");
    CHECK(parse_err(R"({"v":1,"cmd":"output_button","button":"trigger","action":"hold"})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"output_button"})") == "invalid_argument");

    auto so = parse_ok(R"({"v":1,"cmd":"set_output","invert_x":true,"scale_y":1.2,"offset_x":-0.05})", ok);
    CHECK(ok && so.invert_x && *so.invert_x && *so.scale_y == 1.2 && *so.offset_x == -0.05);
    CHECK(parse_err(R"({"v":1,"cmd":"set_output"})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"set_output","screen_aspect":1.77})") == "invalid_argument");
    CHECK(parse_err(R"({"v":1,"cmd":"set_output","scale_x":0})") == "invalid_argument");
    CHECK(parse_ok(R"({"v":1,"cmd":"set_output","screen_aspect":1.77,"content_aspect":1.33})", ok).screen_aspect && ok);

    // Rondreis: make_request levert iets wat parse_request accepteert.
    ipc::Request r;
    r.cmd = ipc::Cmd::TestPattern;
    r.pattern = "sweep";
    r.duration_s = 20;
    auto rt = parse_ok(ipc::make_request(r), ok);
    CHECK(ok && rt.pattern == "sweep" && *rt.duration_s == 20);
    ipc::Request s;
    s.cmd = ipc::Cmd::SetOutput;
    s.invert_y = true;
    s.vp_x0 = 0.125;
    auto st = parse_ok(ipc::make_request(s), ok);
    CHECK(ok && *st.invert_y && *st.vp_x0 == 0.125);
    ipc::Request bt;
    bt.cmd = ipc::Cmd::OutputButton;
    bt.button = "reload";
    bt.action = "down";
    auto bb = parse_ok(ipc::make_request(bt), ok);
    CHECK(ok && bb.button == "reload" && bb.action == "down");
    ipc::Request ar;
    ar.cmd = ipc::Cmd::Arm;
    CHECK(parse_ok(ipc::make_request(ar), ok).cmd == ipc::Cmd::Arm && ok);
}

// ---------------- Live Move -> virtuele lightgun (fase 4) ----------------

// Pijplijn met synthetische rapporten -> LiveGunFeed -> GunOutput (nep-apparaat), zoals in de HID-thread.
static void test_live_feed() {
    core::PipelineConfig pc;
    pc.filter.enabled = false;
    core::Pipeline p(pc);
    p.on_connect("aa:bb:cc:dd:ee:ff", 1'000'000'000);
    FakeDevice d;
    GunOutput o(d);
    config::GunButtonSettings gs;  // oude indeling uit fase 4: Kruis = reload, Vierkant = grenade, Move = recenter
    gs.map = {{"cross", "reload"}, {"square", "grenade"}, {"move", "recenter"}};
    LiveButtonMap m = build_live_map(gs);
    CHECK(m.gun[static_cast<int>(GunButton::Reload)] == core::kBtnCross && m.gun[static_cast<int>(GunButton::Grenade)] == core::kBtnSquare &&
          m.recenter == core::kBtnMove && m.keys.empty());
    CHECK(move_button_bit("") == 0 && move_button_bit("onzin") == 0);
    LiveGunFeed feed(m);
    int64_t t = 1'000'000'000;
    auto report = [&](uint8_t trig, uint32_t buttons, double yaw_rate = 0) {
        core::RawReport r;
        t += 11'270'000;
        r.t_rx_ns = t;
        r.subsamples = 2;
        for (int i = 0; i < 2; ++i) { r.accel[i] = {0, 0, 1}; r.gyro[i] = {0, 0, yaw_rate}; }
        r.trigger = trig;
        r.buttons = buttons;
        return p.on_report(r);
    };
    auto apply = [&](const core::InputSample& s) {
        LiveAction a = feed.update(s);
        if (a.release_all) { o.release_all(); return a; }
        if (!o.armed()) return a;
        if (a.move) o.set_position(a.x, a.y);
        if (a.buttons) {
            o.set_button(GunButton::Trigger, a.trigger);
            o.set_button(GunButton::Reload, a.reload);
            o.set_button(GunButton::Grenade, a.grenade);
        }
        return a;
    };
    // Disarmed: niets naar het apparaat.
    apply(report(200, core::kBtnCross));
    CHECK(d.log.empty());
    std::string err;
    CHECK(o.arm(err));
    // Trekker met hysterese: 150 in, 120 blijft in, 90 los.
    apply(report(0, 0));
    CHECK(d.count("pos") >= 1);
    apply(report(150, 0));
    CHECK(o.button_down(GunButton::Trigger));
    apply(report(120, 0));
    CHECK(o.button_down(GunButton::Trigger));
    apply(report(90, 0));
    CHECK(!o.button_down(GunButton::Trigger));
    // Reload/grenade uit Kruis/Vierkant; de Move-trekkerknop (T-bit) doet niets extra.
    apply(report(0, core::kBtnCross | core::kBtnT));
    CHECK(o.button_down(GunButton::Reload) && !o.button_down(GunButton::Grenade) && !o.button_down(GunButton::Trigger));
    apply(report(0, core::kBtnSquare));
    CHECK(!o.button_down(GunButton::Reload) && o.button_down(GunButton::Grenade));
    // Recenter alleen op de down-flank van de Move-knop.
    LiveAction a = apply(report(0, core::kBtnMove));
    CHECK(a.recenter);
    a = apply(report(0, core::kBtnMove));
    CHECK(!a.recenter);
    // Beweging: de aim verandert en gaat naar het apparaat.
    int x0 = d.last_x;
    for (int i = 0; i < 20; ++i) apply(report(160, core::kBtnCross, 0.5));
    CHECK(d.last_x != x0);
    CHECK(o.button_down(GunButton::Trigger) && o.button_down(GunButton::Reload));
    // Stale (60 ms geen rapport): aim ongeldig -> niet meer bewegen; knoppen blijven (gat tot 250 ms).
    int npos = d.count("pos");
    auto st = p.tick(t + 60'000'000);
    CHECK(st && st->link == core::LinkState::Stale && !st->aim_valid);
    a = apply(*st);
    CHECK(!a.move);
    CHECK(d.count("pos") == npos);
    CHECK(o.button_down(GunButton::Trigger) && o.button_down(GunButton::Reload));
    // Lost (> 250 ms): alle gun-knoppen los, cursor blijft staan (geen sprong).
    auto lost = p.tick(t + 300'000'000);
    CHECK(lost && lost->link == core::LinkState::Lost);
    a = apply(*lost);
    CHECK(a.release_all);
    CHECK(d.pressed.empty());
    CHECK(!o.button_down(GunButton::Trigger) && !o.button_down(GunButton::Reload) && !o.button_down(GunButton::Grenade));
    CHECK(d.count("pos") == npos);
    // Na herstel geen "hangende" trekker uit de oude toestand: eerst onder de drempel.
    apply(report(110, 0));
    CHECK(!o.button_down(GunButton::Trigger));
    // Disconnect geeft ook alles vrij.
    apply(report(200, core::kBtnSquare));
    CHECK(o.button_down(GunButton::Trigger) && o.button_down(GunButton::Grenade));
    apply(p.on_disconnect(t + 1'000'000));
    CHECK(d.pressed.empty());
}

// ---------------- Algemene knoppenindeling en virtueel toetsenbord ----------------

// Nep-toetsenbord: onthoudt welke toetsen volgens de schrijfacties nu ingedrukt zijn.
struct FakeKbd : KeyDevice {
    bool exists = false, fail_create = false, fail_write = false;
    int creates = 0, destroys = 0, writes = 0;
    std::set<int> pressed;
    bool create(std::string& err) override {
        if (exists) return true;
        if (fail_create) { err = "nep: toetsenbord"; return false; }
        exists = true; ++creates; return true;
    }
    void destroy() override { if (exists) { exists = false; ++destroys; pressed.clear(); } }
    bool created() const override { return exists; }
    bool write_key(int code, bool down) override {
        if (!exists || fail_write) return false;
        ++writes;
        if (down) pressed.insert(code); else pressed.erase(code);
        return true;
    }
};

static void test_key_table() {
    // Dezelfde toetsen en codes als src/tools/vinput/vinput.c (C-kopie van de tabel; hier de verwachte inhoud).
    CHECK(kKeyNames.size() == 60);
    CHECK(key_code("esc") == KEY_ESC && key_code("enter") == KEY_ENTER && key_code("space") == KEY_SPACE);
    CHECK(key_code("lshift") == KEY_LEFTSHIFT && key_code("lctrl") == KEY_LEFTCTRL && key_code("lalt") == KEY_LEFTALT);
    CHECK(key_code("up") == KEY_UP && key_code("f12") == KEY_F12 && key_code("f1") == KEY_F1);
    CHECK(key_code("1") == KEY_1 && key_code("5") == KEY_5 && key_code("0") == KEY_0 && key_code("z") == KEY_Z);
    CHECK(key_code("KEY_5") == KEY_5 && key_code("Key_A") == KEY_A);
    CHECK(key_code("") == -1 && key_code("onzin") == -1 && key_code("f13") == -1);
    CHECK(std::string(key_name(KEY_5)) == "5" && std::string(key_name(KEY_LEFTSHIFT)) == "lshift");
    // Alle namen uniek en terug te vinden.
    std::set<std::string> names;
    for (const auto& k : kKeyNames) { names.insert(k.name); CHECK(key_code(k.name) == k.code); }
    CHECK(names.size() == kKeyNames.size());
    // Identiteit van het toetsenbord = vinput kbd.
    CHECK(std::string(kKbdDeviceName) == "RavenGun Virtual Keyboard" && kKbdVendorId == 0x1209 && kKbdProductId == 0x7201);
}

static void test_live_map_defaults() {
    config::GunButtonSettings gs;
    LiveButtonMap m = build_live_map(gs);
    CHECK(config::validate(config::Settings{}) == "");
    // Lage 32 bits: de Move; hoge 32: de Navigation Controller van dezelfde speler (L1 herladen, L2 granaat).
    const uint64_t nav_l1 = static_cast<uint64_t>(core::kNavL1) << kNavBitShift, nav_l2 = static_cast<uint64_t>(core::kNavL2) << kNavBitShift;
    CHECK(m.gun[static_cast<int>(GunButton::Grenade)] == (core::kBtnMove | nav_l2));
    CHECK(m.gun[static_cast<int>(GunButton::Reload)] == (core::kBtnCross | core::kBtnPump | core::kBtnSsRl | nav_l1));
    // key:5 = triangle OR select; key:1 = square OR start (OR van twee knoppen).
    CHECK(m.keys.size() == 2);
    for (const auto& k : m.keys) {
        if (k.code == KEY_5) CHECK(k.bits == (core::kBtnTriangle | core::kBtnSelect));
        else if (k.code == KEY_1) CHECK(k.bits == (core::kBtnSquare | core::kBtnStart));
        else CHECK(false);
    }
    CHECK(m.recenter == 0);
    CHECK(describe_layout(m).find("move=grenade") != std::string::npos && describe_layout(m).find("ps=recenter (vast)") != std::string::npos);
    // Sharp Shooter-pomp (pump) is standaard reload; de overige EXT-knoppen doen niets.
    CHECK((m.gun[static_cast<int>(GunButton::Reload)] & core::kBtnPump) && (m.gun[static_cast<int>(GunButton::Reload)] & core::kBtnSsRl));
    CHECK(describe_layout(m).find("pump=reload") != std::string::npos && describe_layout(m).find("ss_fire=none") != std::string::npos);
    for (const auto& k : m.keys) CHECK(!(k.bits & core::kAllExtButtons));
    gs = config::GunButtonSettings{};
    gs.map["sel2"] = "key:5";
    gs.map["ss_fire"] = "aux1";
    LiveButtonMap m2 = build_live_map(gs);
    CHECK(m2.gun[static_cast<int>(GunButton::Aux1)] == core::kBtnSsFire);
    bool found = false;
    for (const auto& k : m2.keys) if (k.code == KEY_5) found = (k.bits & core::kBtnSel2) != 0;
    CHECK(found);
}

static core::InputSample sample(uint32_t buttons, uint32_t pressed = 0, uint8_t trig = 0,
                                core::LinkState link = core::LinkState::Fresh) {
    core::InputSample s;
    s.link = link;
    s.buttons = buttons;
    s.pressed = pressed;
    s.trigger = trig;
    return s;
}

static void test_live_feed_layout() {
    LiveGunFeed feed(build_live_map(config::GunButtonSettings{}));
    auto find = [](const LiveAction& a, int code) {
        for (const auto& k : a.keys) if (k.first == code) return k.second;
        return false;
    };
    LiveAction a = feed.update(sample(0));
    CHECK(a.keys.size() == 2 && !find(a, KEY_5) && !find(a, KEY_1) && !a.reload && !a.grenade);
    a = feed.update(sample(core::kBtnTriangle));
    CHECK(find(a, KEY_5) && !find(a, KEY_1));
    a = feed.update(sample(core::kBtnTriangle | core::kBtnSelect));  // OR: twee knoppen, een toets
    CHECK(find(a, KEY_5));
    a = feed.update(sample(core::kBtnSelect));
    CHECK(find(a, KEY_5));
    a = feed.update(sample(core::kBtnStart | core::kBtnCross | core::kBtnMove));
    CHECK(find(a, KEY_1) && !find(a, KEY_5) && a.reload && a.grenade);
    a = feed.update(sample(core::kBtnCircle));  // circle = none
    CHECK(!find(a, KEY_1) && !find(a, KEY_5) && !a.reload && !a.grenade);
    // PS: recenter, ook zonder gewapende uitvoer (de feed weet daar niets van); geen gunknop of toets.
    a = feed.update(sample(core::kBtnPS, core::kBtnPS));
    CHECK(a.recenter && a.recenter_ps && !a.reload && !a.grenade && !find(a, KEY_1));
    a = feed.update(sample(core::kBtnPS, 0));  // alleen de down-flank
    CHECK(!a.recenter);
    // Een Move-knop als recenter uit de map.
    config::GunButtonSettings gs;
    gs.map["circle"] = "recenter";
    LiveGunFeed f2(build_live_map(gs));
    a = f2.update(sample(core::kBtnCircle, core::kBtnCircle));
    CHECK(a.recenter && !a.recenter_ps);
    // aux1/aux2 naar de gun-knoppen.
    gs.map["circle"] = "aux1";
    gs.map["triangle"] = "aux2";
    LiveGunFeed f3(build_live_map(gs));
    a = f3.update(sample(core::kBtnCircle));
    CHECK(a.aux1 && !a.aux2);
    a = f3.update(sample(core::kBtnTriangle));
    CHECK(!a.aux1 && a.aux2 && a.keys.size() == 2 /* select=key:5 en square/start=key:1 */);
    // Verlies: release_all.
    a = feed.update(sample(core::kBtnStart, 0, 0, core::LinkState::Lost));
    CHECK(a.release_all);
}

// Met een actieve Sharp Shooter-uitbreiding is de trekker het fire-bit; T zonder fire is de pomp (reload) en geeft geen schot.
// Zonder uitbreiding blijft de analoge T met hysterese de trekker.
static void test_live_feed_sharpshooter() {
    LiveGunFeed feed(build_live_map(config::GunButtonSettings{}));
    auto ext_sample = [](uint32_t buttons, uint8_t trig) {
        core::InputSample s = sample(buttons, 0, trig);
        s.ext_active = true;
        s.ext_connected = true;
        return s;
    };
    // Schot: fire-bit en T tegelijk; loslaten met 1 rapport scheefloop (fire los, T nog 127).
    LiveAction a = feed.update(ext_sample(0, 0));
    CHECK(!a.trigger);
    a = feed.update(ext_sample(core::kBtnSsFire | core::kBtnT, 255));
    CHECK(a.trigger && !a.reload);                       // meteen, geen extra rapport
    a = feed.update(ext_sample(core::kBtnSsFire | core::kBtnT, 255));
    CHECK(a.trigger);
    a = feed.update(ext_sample(core::kBtnT, 127));       // fire los, T nog niet: de trekker is los
    CHECK(!a.trigger);
    // Pomp: T 255 zonder fire. De pijplijn zet de afgeleide knop "pump"; de feed maakt er reload van, geen schot.
    a = feed.update(ext_sample(core::kBtnT, 255));
    CHECK(!a.trigger && !a.reload);                      // eerste rapport: nog ontdenderen (in de pijplijn)
    a = feed.update(ext_sample(core::kBtnT | core::kBtnPump, 255));
    CHECK(!a.trigger && a.reload);
    a = feed.update(ext_sample(0, 0));
    CHECK(!a.trigger && !a.reload);
    // T hoog zonder fire geeft ook zonder pomp-knop nooit een schot (met uitbreiding).
    a = feed.update(ext_sample(core::kBtnT, 255));
    CHECK(!a.trigger);
    // Het RL-bit is een eigen knop met dezelfde standaardactie.
    a = feed.update(ext_sample(core::kBtnSsRl, 0));
    CHECK(a.reload);
    // Zonder uitbreiding: de analoge T met hysterese 140/100.
    LiveGunFeed plain(build_live_map(config::GunButtonSettings{}));
    a = plain.update(sample(0, 0, 139));
    CHECK(!a.trigger);
    a = plain.update(sample(core::kBtnT, 0, 140));
    CHECK(a.trigger);
    a = plain.update(sample(core::kBtnT, 0, 101));
    CHECK(a.trigger);
    a = plain.update(sample(0, 0, 100));
    CHECK(!a.trigger);
    // Verlies: trekker los.
    a = feed.update(ext_sample(core::kBtnSsFire, 255));
    CHECK(a.trigger);
    core::InputSample lost = ext_sample(core::kBtnSsFire, 255);
    lost.link = core::LinkState::Lost;
    a = feed.update(lost);
    CHECK(a.release_all && !feed.trigger_down());
}

// Zoals OutputService::feed_live: toetsen alleen als gewapend; release_all altijd.
static void apply_keys(GunOutput& o, const LiveAction& a) {
    if (a.release_all) { o.release_all(); return; }
    if (!o.armed()) return;
    for (const auto& k : a.keys) o.set_key(k.first, k.second);
}

static void test_keyboard_output() {
    FakeDevice d;
    FakeKbd kb;
    {
        GunOutput o(d, &kb);
        LiveGunFeed feed(build_live_map(config::GunButtonSettings{}));
        // Ongewapend: geen toetsenbord, geen events.
        CHECK(!kb.exists && !o.set_key(KEY_5, true));
        apply_keys(o, feed.update(sample(core::kBtnSquare)));
        CHECK(kb.writes == 0 && kb.pressed.empty());
        std::string err;
        CHECK(o.arm(err));
        CHECK(kb.exists && d.exists && o.keyboard_created());  // het toetsenbord bestaat zodra de gun bestaat
        // Key down/up-flanken, zonder dubbele events.
        apply_keys(o, feed.update(sample(core::kBtnSquare)));
        CHECK(kb.pressed == std::set<int>{KEY_1} && kb.writes == 1);
        apply_keys(o, feed.update(sample(core::kBtnSquare | core::kBtnStart)));
        CHECK(kb.pressed == std::set<int>{KEY_1} && kb.writes == 1);
        apply_keys(o, feed.update(sample(core::kBtnStart | core::kBtnTriangle)));
        CHECK(kb.pressed == (std::set<int>{KEY_1, KEY_5}) && kb.writes == 2);
        CHECK(o.key_down(KEY_1) && o.key_down(KEY_5));
        apply_keys(o, feed.update(sample(0)));
        CHECK(kb.pressed.empty() && kb.writes == 4);
        // Bronuitval: release_all laat toetsen los, en blijft gewapend.
        apply_keys(o, feed.update(sample(core::kBtnStart | core::kBtnTriangle)));
        CHECK(kb.pressed.size() == 2);
        apply_keys(o, feed.update(sample(0, 0, 0, core::LinkState::Lost)));
        CHECK(kb.pressed.empty() && o.armed() && o.keys_down().empty());
        // Disarm laat toetsen los; het toetsenbord blijft bestaan (net als de gun).
        apply_keys(o, feed.update(sample(core::kBtnStart)));
        CHECK(kb.pressed.size() == 1);
        o.disarm();
        CHECK(kb.pressed.empty() && kb.exists && !o.set_key(KEY_1, true));
        // Opnieuw wapenen, toets in, dan afsluiten: apparaat weg.
        CHECK(o.arm(err));
        apply_keys(o, feed.update(sample(core::kBtnStart)));
        CHECK(kb.pressed.size() == 1);
        o.shutdown();
        CHECK(kb.pressed.empty() && !kb.exists && !d.exists);
        // Schrijffout op het toetsenbord: disarm + apparaten weg.
        CHECK(o.arm(err));
        kb.fail_write = true;
        CHECK(!o.set_key(KEY_1, true));
        CHECK(!o.armed() && !kb.exists);
    }
    // Destructor zonder shutdown: toetsen los.
    {
        FakeDevice d2;
        FakeKbd k2;
        {
            GunOutput o(d2, &k2);
            std::string err;
            CHECK(o.arm(err));
            CHECK(o.set_key(KEY_5, true) && k2.pressed.size() == 1);
        }
        CHECK(k2.pressed.empty() && !k2.exists);
    }
    // Toetsenbord niet aan te maken: arm lukt (gun werkt), fout zichtbaar.
    {
        FakeDevice d3;
        FakeKbd k3;
        k3.fail_create = true;
        GunOutput o(d3, &k3);
        std::string err;
        CHECK(o.arm(err) && o.armed() && !o.keyboard_error().empty() && !o.set_key(KEY_1, true));
    }
}

// ---------------- Uitvoerklok (fase 6) ----------------

static core::InputSample clock_sample(int64_t t_ns, double x, double y, double vx, double vy, double age_ms = 0) {
    core::InputSample s;
    s.aim_valid = true;
    s.t_eval_ns = t_ns;
    s.aim.x = x;
    s.aim.y = y;
    s.aim_vx = vx;
    s.aim_vy = vy;
    s.age_ms = age_ms;
    return s;
}

static void test_aim_clock_extrapolation_limit() {
    OutputClockConfig c;
    c.hz = 250;
    c.max_extrap_ms = 40;
    AimClock k(c);
    CHECK(!k.at(0).has_value() && !k.tick(0).has_value());  // geen anker
    k.on_sample(clock_sample(1'000'000'000, 0.5, 0.4, 0.5, -0.25));
    auto p0 = k.at(1'000'000'000);
    CHECK(p0 && p0->x == 0.5 && p0->y == 0.4 && !p0->extrapolating);
    auto p10 = k.at(1'010'000'000);
    CHECK(p10 && p10->extrapolating && !p10->held);
    CHECK_NEAR(p10->x, 0.5 + 0.5 * 0.010, 1e-9);
    CHECK_NEAR(p10->y, 0.4 - 0.25 * 0.010, 1e-9);
    // Op de limiet en daarna: vastgehouden op anker + v * 40 ms, hoe lang het gat ook duurt.
    auto p40 = k.at(1'040'000'000);
    CHECK(p40 && p40->extrapolating);
    CHECK_NEAR(p40->x, 0.5 + 0.5 * 0.040, 1e-9);
    for (int64_t ms : {41, 60, 250, 5000}) {
        auto q = k.at(1'000'000'000 + ms * 1'000'000);
        CHECK(q && q->held && !q->extrapolating);
        CHECK_NEAR(q->x, 0.5 + 0.5 * 0.040, 1e-9);
        CHECK_NEAR(q->y, 0.4 - 0.25 * 0.040, 1e-9);
    }
    // Een tijd vóór het anker: geen terugextrapolatie.
    auto before = k.at(900'000'000);
    CHECK(before && before->x == 0.5);
    // Een ongeldige aim (stale/lost) wist het anker: de cursor blijft staan, er wordt niets geëxtrapoleerd.
    core::InputSample bad = clock_sample(1'020'000'000, 0.9, 0.9, 1, 1);
    bad.aim_valid = false;
    k.on_sample(bad);
    CHECK(!k.at(1'030'000'000).has_value());
    // Klok uit (hz = 0): tick geeft niets; at() blijft werken voor de analyse.
    OutputClockConfig off;
    off.hz = 0;
    AimClock k0(off);
    k0.on_sample(clock_sample(0, 0.5, 0.5, 1, 1));
    CHECK(!k0.tick(10'000'000).has_value() && k0.at(10'000'000).has_value());
    // Taper: de snelheid loopt lineair naar 0 op de limiet; verplaatsing op de limiet is dan v * limiet / 2.
    OutputClockConfig tc = c;
    tc.taper = 1.0;
    AimClock kt(tc);
    kt.on_sample(clock_sample(0, 0.0, 0.0, 1.0, 0.0));
    CHECK_NEAR(kt.at(40'000'000)->x, 0.020, 1e-9);
    CHECK_NEAR(kt.at(500'000'000)->x, 0.020, 1e-9);
    CHECK(kt.at(20'000'000)->x < 0.020 && kt.at(20'000'000)->x > 0.010);
    // Leeftijdscompensatie: een rapport dat 15 ms later binnenkwam, wordt 15 ms verder geëxtrapoleerd.
    AimClock ka(c);
    ka.on_sample(clock_sample(1'000'000'000, 0.5, 0.5, 1.0, 0.0, 15.0));
    CHECK_NEAR(ka.at(1'000'000'000)->x, 0.5 + 0.015, 1e-9);
    OutputClockConfig nc = c;
    nc.age_comp = false;
    AimClock kn(nc);
    kn.on_sample(clock_sample(1'000'000'000, 0.5, 0.5, 1.0, 0.0, 15.0));
    CHECK_NEAR(kn.at(1'000'000'000)->x, 0.5, 1e-12);
}

static void test_aim_clock_grid() {
    OutputClockConfig c;
    c.hz = 250;
    AimClock k(c);
    CHECK(k.period_ns() == 4'000'000);
    // Het eerste tikmoment legt het raster vast; elk volgend moment ligt exact één periode verder.
    int64_t t = k.next_tick_after(1'000'000'000);
    CHECK(t == 1'004'000'000);
    int64_t prev = t;
    for (int i = 0; i < 100; ++i) {
        const int64_t nx = k.next_tick_after(prev);
        CHECK(nx - prev == 4'000'000);
        prev = nx;
    }
    // Te laat (klokthread kreeg geen CPU): gemiste tikken worden overgeslagen, het raster blijft liggen.
    const int64_t late = k.next_tick_after(1'000'000'000 + 4'000'000 * 200 + 1'500'000);
    CHECK(late == 1'000'000'000 + 4'000'000 * 201);
    // Klok uit: geen raster (af en toe kijken).
    OutputClockConfig off;
    off.hz = 0;
    AimClock k0(off);
    CHECK(k0.period_ns() == 0 && k0.next_tick_after(5) > 5);
    // set_config met rare waarden wordt veilig genomen.
    OutputClockConfig bad;
    bad.hz = -3;
    bad.max_extrap_ms = std::nan("");
    bad.taper = 7;
    k.set_config(bad);
    CHECK(!k.config().enabled() && k.config().max_extrap_ms == 0 && k.config().taper == 1.0);
}

// Gelijkmatige events: rapporten met 14 % verlies en bursts, een positie die lineair beweegt (v constant), klok op 250 Hz.
// Elke tik geeft een nieuwe, monotone positie met een stap van hooguit v * periode; in een gat langer dan de limiet stopt de beweging.
static void test_clock_smooth_and_stops() {
    FakeDevice dev;
    GunOutput o(dev);
    std::string err;
    CHECK(o.arm(err));
    OutputClockConfig c;
    c.hz = 250;
    c.max_extrap_ms = 40;
    c.age_comp = false;
    AimClock clk(c);
    const double v = 0.5;                   // schermen/s
    const double pos0 = 0.2;
    auto truth = [&](int64_t t_ns) { return pos0 + v * static_cast<double>(t_ns) / 1e9; };
    // Rapporten elke 11,27 ms; elk 7e verloren; een lang gat van 200 ms rond t = 1,0 s.
    std::vector<int64_t> arrivals;
    int n = 0;
    for (int64_t t = 0; t < 1'500'000'000; t += 11'270'000, ++n) {
        if (n % 7 == 3) continue;
        if (t > 1'000'000'000 && t < 1'200'000'000) continue;
        arrivals.push_back(t);
    }
    size_t ai = 0;
    std::vector<double> tick_pos;
    std::vector<int64_t> tick_t;
    int64_t tk = clk.next_tick_after(0);
    double max_step = 0, last_x = -1;
    uint64_t events_after_limit = 0;
    bool prev_held = false;
    while (tk < 1'500'000'000) {
        while (ai < arrivals.size() && arrivals[ai] <= tk) {
            clk.on_sample(clock_sample(arrivals[ai], truth(arrivals[ai]), 0.5, v, 0));
            ++ai;
        }
        auto p = clk.tick(tk);
        if (p) {
            const uint64_t before = o.stats().position_events;
            o.set_position(p->x, p->y);
            if (p->held && prev_held && o.stats().position_events != before) ++events_after_limit;
            // De grote stap na het lange gat (het nieuwe rapport) telt hier niet mee; die meet de analyse.
            if (last_x >= 0 && tk < 1'040'000'000) max_step = std::max(max_step, std::fabs(p->x - last_x));
            prev_held = p->held;
            last_x = p->x;
            tick_pos.push_back(p->x);
            tick_t.push_back(tk);
        }
        const int64_t nx = clk.next_tick_after(tk);
        CHECK(nx - tk == 4'000'000);
        tk = nx;
    }
    CHECK(tick_pos.size() > 300);
    // De getoonde positie springt nergens meer dan één tikstap plus de maximale afwijking van de lineaire baan (hier 0: exact).
    CHECK(max_step <= v * 0.004 * 1.01 + 1e-9);
    // Gat > limiet: tussen 1,04 s en 1,2 s staat de positie stil: geen enkel nieuw event.
    CHECK(events_after_limit == 0);
    int64_t last_before_gap = 0;
    for (int64_t a : arrivals) if (a <= 1'000'000'000) last_before_gap = a;
    double frozen = -1;
    int frozen_n = 0;
    for (size_t i = 0; i < tick_t.size(); ++i) {
        if (tick_t[i] > last_before_gap + 41'000'000 && tick_t[i] < 1'190'000'000) {
            if (frozen < 0) frozen = tick_pos[i];
            CHECK_NEAR(tick_pos[i], frozen, 1e-12);
            ++frozen_n;
        }
    }
    CHECK(frozen_n > 20);
    // De positie volgt de waarheid binnen de limiet nauwkeurig (constante snelheid): fout < 1e-9 schermen tijdens extrapolatie.
    for (size_t i = 0; i < tick_t.size(); ++i) {
        if (tick_t[i] < 900'000'000 && tick_t[i] > 100'000'000) CHECK_NEAR(tick_pos[i], truth(tick_t[i]), 1e-9);
    }
}

// Speler 1 houdt exact de oude identiteit (bestaande TeknoParrot-bindingen); speler 2..4 een eigen PID, naam en by-id-link.
static void test_gun_identity() {
    GunIdentity p1 = gun_identity(1);
    CHECK(p1.vendor == 0x1209 && p1.product == 0x7202 && p1.name == "RavenGun Virtual Lightgun");
    CHECK(p1.stable_link == "/dev/input/ravengun-gun" && p1.by_id_link == "/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse");
    CHECK(UinputGun().identity().product == 0x7202 && UinputGun().identity().name == kDeviceName);
    GunIdentity p2 = gun_identity(2);
    CHECK(p2.player == 2 && p2.vendor == 0x1209 && p2.product == 0x7203 && p2.name == "RavenGun Virtual Lightgun P2");
    CHECK(p2.stable_link == "/dev/input/ravengun-gun-p2" && p2.by_id_link == "/dev/input/by-id/usb-RavenGun_Virtual_Lightgun_P2-event-mouse");
    CHECK(gun_identity(4).product == 0x7205 && gun_identity(4).name == "RavenGun Virtual Lightgun P4");
    CHECK(gun_identity(0).product == 0x7202 && gun_identity(9).product == 0x7202);  // buiten bereik: speler 1
    // De udev-regel kent elke PID en zijn by-id-link (de naam van de link volgt de identiteit).
    std::ifstream f(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/udev/70-ravengun.rules");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string rules = ss.str();
    for (int p = 1; p <= 4; ++p) {
        GunIdentity g = gun_identity(p);
        char pid[8];
        std::snprintf(pid, sizeof pid, "%04x", g.product);
        const std::string by_id = g.by_id_link.substr(std::string("/dev/").size());
        const std::string link = g.stable_link.substr(std::string("/dev/").size());
        size_t at = rules.find(std::string("ATTRS{id/product}==\"") + pid + "\"");
        CHECK(at != std::string::npos);
        if (at == std::string::npos) continue;
        std::string line = rules.substr(at, rules.find('\n', at) - at);
        CHECK(line.find("TAG+=\"uaccess\"") != std::string::npos && line.find(by_id) != std::string::npos && line.find(link + " ") != std::string::npos);
        CHECK(line.find("LIBINPUT_IGNORE_DEVICE}=\"1\"") != std::string::npos);
    }
}

// Eén toetsenbord voor alle spelers: een toets blijft ingedrukt zolang een speler hem vasthoudt.
static void test_key_mux() {
    FakeKbd real;
    KeyMux mux(real);
    std::string err;
    KeyDevice& a = mux.port(1);
    KeyDevice& b = mux.port(2);
    CHECK(!a.created() && a.create(err) && b.create(err) && real.creates == 1 && a.created() && b.created());
    CHECK(a.write_key(6, true) && real.pressed.count(6) && real.writes == 1);   // speler 1 drukt Coin
    CHECK(b.write_key(6, true) && real.writes == 1 && mux.holders(6) == 2);     // speler 2 ook: geen tweede event
    CHECK(a.write_key(6, false) && real.pressed.count(6) && real.writes == 1);   // speler 1 laat los: blijft ingedrukt
    CHECK(a.write_key(6, false) && mux.holders(6) == 1);                         // dubbel loslaten telt niet
    CHECK(b.write_key(6, false) && !real.pressed.count(6) && real.writes == 2);  // laatste laat los: up-event
    // Een port vrijgeven (bronuitval van een speler) laat alleen diens toetsen los.
    a.write_key(2, true); b.write_key(3, true); b.write_key(2, true);
    a.destroy();
    CHECK(!a.created() && real.pressed.count(2) && real.pressed.count(3) && real.exists);
    b.destroy();
    CHECK(real.pressed.empty() && real.exists);
    CHECK(!a.write_key(4, true));  // vrijgegeven port schrijft niet
    // Een schrijffout van het echte apparaat: niet als ingedrukt tellen.
    CHECK(a.create(err));
    real.fail_write = true;
    CHECK(!a.write_key(7, true) && mux.holders(7) == 0);
    real.fail_write = false;
    mux.shutdown();
    CHECK(!real.exists && real.destroys == 1);
    // Via GunOutput: twee spelers met dezelfde toets.
    FakeKbd r2;
    KeyMux m2(r2);
    FakeDevice g1, g2;
    GunOutput o1(g1, &m2.port(1)), o2(g2, &m2.port(2));
    CHECK(o1.arm(err) && o2.arm(err) && r2.creates == 1);
    o1.set_key(6, true); o2.set_key(6, true);
    o1.release_all();
    CHECK(r2.pressed.count(6) && o2.key_down(6) && !o1.key_down(6));
    o2.disarm();
    CHECK(r2.pressed.empty());
}

// Minimum hold time of the virtual keyboard: short presses are stretched, long ones pass, quick double presses stay two.
static void test_key_mux_min_hold() {
    constexpr int64_t ms = 1'000'000;
    int64_t t = 1000 * ms;
    FakeKbd real;
    KeyMux mux(real);
    mux.set_clock([&] { return t; });
    mux.set_timing({200 * ms, 50 * ms});
    std::string err;
    KeyDevice& a = mux.port(1);
    CHECK(a.create(err));
    auto advance = [&](int64_t d) { t += d * ms; mux.poll(t); };
    int wakes = 0;
    mux.set_wake([&] { ++wakes; });
    // A 40 ms tap: down at once, the up is held back until 200 ms after the down.
    CHECK(a.write_key(6, true) && real.pressed.count(6) && real.writes == 1);
    advance(40);
    CHECK(a.write_key(6, false) && real.pressed.count(6) && real.writes == 1 && mux.held_by(1, 6) && wakes == 1);
    CHECK(mux.next_due() == t + 160 * ms);
    advance(159);
    CHECK(real.pressed.count(6) && mux.down(6));
    advance(1);
    CHECK(!real.pressed.count(6) && real.writes == 2 && !mux.down(6) && mux.next_due() == KeyMux::kNever);
    // A press longer than the minimum is unchanged: the up passes through at once.
    advance(500);
    CHECK(a.write_key(6, true) && real.pressed.count(6));
    advance(300);
    CHECK(a.write_key(6, false) && !real.pressed.count(6) && real.writes == 4 && wakes == 1);
    // Two quick taps (40 ms down, 30 ms apart) must stay two presses: down, up@200, gap 50, down@250, up@450.
    advance(500);
    real.writes = 0;
    CHECK(a.write_key(6, true) && real.writes == 1);        // t0: down
    advance(40);
    CHECK(a.write_key(6, false));                           // t0+40: up (delayed to +200)
    advance(30);
    CHECK(a.write_key(6, true) && real.writes == 1);        // t0+70: down again (delayed to +250)
    advance(40);
    CHECK(a.write_key(6, false));                           // t0+110: up (delayed to +450)
    CHECK(real.pressed.count(6) && real.writes == 1);
    advance(90);                                            // t0+200: first up
    CHECK(!real.pressed.count(6) && real.writes == 2);
    advance(49);                                            // t0+249: still in the gap
    CHECK(!real.pressed.count(6) && real.writes == 2);
    advance(1);                                             // t0+250: second down
    CHECK(real.pressed.count(6) && real.writes == 3);
    advance(199);
    CHECK(real.pressed.count(6));
    advance(1);                                             // t0+450: second up
    CHECK(!real.pressed.count(6) && real.writes == 4 && mux.next_due() == KeyMux::kNever);
    // A release (disarm, source loss) is immediate and drops what was queued.
    advance(500);
    CHECK(a.write_key(7, true));
    advance(10);
    CHECK(a.write_key(7, false) && real.pressed.count(7));
    a.destroy();
    CHECK(!real.pressed.count(7) && mux.next_due() == KeyMux::kNever);
    // Timing off: identical to a plain pass-through.
    FakeKbd r2;
    KeyMux m2(r2);
    m2.set_clock([&] { return t; });
    KeyDevice& b = m2.port(1);
    CHECK(b.create(err) && b.write_key(5, true) && b.write_key(5, false) && !r2.pressed.count(5) && r2.writes == 2 && m2.next_due() == KeyMux::kNever);
    // Two players pressing the same key keep their own timing; the device sees the union.
    FakeKbd r3;
    KeyMux m3(r3);
    m3.set_clock([&] { return t; });
    m3.set_timing({200 * ms, 50 * ms});
    CHECK(m3.port(1).create(err) && m3.port(2).create(err));
    m3.port(1).write_key(6, true);
    m3.port(2).write_key(6, true);
    m3.port(1).write_key(6, false);
    m3.port(2).write_key(6, false);
    t += 200 * ms; m3.poll(t);
    CHECK(!r3.pressed.count(6) && r3.writes == 2 && m3.holders(6) == 0);
}

// Navigation Controller: rapport naar knoppen (Sixaxis-indeling) en de stick met hysterese.
static void test_nav_report() {
    uint8_t r[49] = {};
    r[0] = 0x01; r[6] = 128; r[7] = 128;
    core::NavState s, prev;
    CHECK(core::parse_nav_report(r, sizeof r, prev, s) && s.buttons == 0);
    r[3] = 0x04 | 0x40;  // L1 + kruis
    r[2] = 0x10 | 0x02;  // omhoog + L3
    r[4] = 0x01;         // PS
    r[18] = 200;
    CHECK(core::parse_nav_report(r, sizeof r, prev, s));
    CHECK(s.buttons == (core::kNavL1 | core::kNavCross | core::kNavUp | core::kNavL3 | core::kNavPS) && s.l2 == 200);
    // Stick: omhoog voorbij de drempel, terug tot binnen de hysterese.
    std::memset(r + 2, 0, 3);
    r[7] = 128 - 70;
    CHECK(core::parse_nav_report(r, sizeof r, s, s) && s.buttons == core::kNavStickUp);
    r[7] = 128 - 50;  // tussen uit (40) en aan (64): blijft aan
    CHECK(core::parse_nav_report(r, sizeof r, s, s) && s.buttons == core::kNavStickUp);
    r[7] = 128 - 30;
    CHECK(core::parse_nav_report(r, sizeof r, s, s) && s.buttons == 0);
    r[7] = 128 - 50;  // vanuit uit: nog niet aan
    CHECK(core::parse_nav_report(r, sizeof r, s, s) && s.buttons == 0);
    r[6] = 255; r[7] = 128;
    CHECK(core::parse_nav_report(r, sizeof r, s, s) && s.buttons == core::kNavStickRight);
    // Te kort of een ander rapport-ID: geweigerd, uitvoer ongewijzigd.
    core::NavState keep = s;
    CHECK(!core::parse_nav_report(r, 10, s, s) && s.buttons == keep.buttons);
    r[0] = 0x02;
    CHECK(!core::parse_nav_report(r, sizeof r, s, s));
    CHECK(core::nav_button_bit("nav_l2") == core::kNavL2 && core::nav_button_bit("l2") == 0);
    CHECK(move_button_bit("nav_cross") == (static_cast<uint64_t>(core::kNavCross) << kNavBitShift) && move_button_bit("cross") == core::kBtnCross);
}

// Knoppen van de Navigation Controller in de indeling van een speler: acties, toetsen en recenter op de flank.
static void test_live_feed_nav() {
    config::GunButtonSettings gs;
    gs.map["nav_cross"] = "key:2";
    gs.map["nav_ps"] = "recenter";
    gs.map["nav_stick_left"] = "key:left";
    LiveGunFeed feed(build_live_map(gs));
    core::InputSample s = sample(0);
    LiveAction a = feed.update(s, core::kNavL1 | core::kNavCross);
    CHECK(a.reload && !a.grenade && !a.recenter);
    bool two = false;
    for (const auto& k : a.keys) if (k.first == key_code("2")) two = k.second;
    CHECK(two);
    a = feed.update(s, core::kNavL2 | core::kNavPS);
    CHECK(a.grenade && !a.reload && a.recenter && !a.recenter_ps);
    a = feed.update(s, core::kNavPS);  // vastgehouden: geen nieuwe flank
    CHECK(!a.recenter);
    a = feed.update(s, core::kNavStickLeft);
    bool left = false;
    for (const auto& k : a.keys) if (k.first == key_code("left")) left = k.second;
    CHECK(left && !a.recenter);
    // Bronuitval van de Move: alles los, ook de nav-acties.
    core::InputSample lost = s;
    lost.link = core::LinkState::Lost;
    CHECK(feed.update(lost, core::kNavL1).release_all);
}

// ---------------- Override-laag: synthetische invoer naast live rapporten ----------------

// Zoals OutputService::feed_live: knoppen en toetsen van een live rapport, OR-ed met de synthetische stand.
static void apply_live(GunOutput& o, OverrideLayer& ov, const LiveAction& a) {
    if (a.release_all) { o.release_all(); ov.clear(); return; }
    if (!o.armed()) return;
    o.set_button(GunButton::Trigger, ov.live_button(GunButton::Trigger, a.trigger));
    o.set_button(GunButton::Reload, ov.live_button(GunButton::Reload, a.reload));
    o.set_button(GunButton::Grenade, ov.live_button(GunButton::Grenade, a.grenade));
    o.set_button(GunButton::Aux1, ov.live_button(GunButton::Aux1, a.aux1));
    o.set_button(GunButton::Aux2, ov.live_button(GunButton::Aux2, a.aux2));
    for (const auto& k : a.keys) o.set_key(k.first, ov.live_key(k.first, k.second));
}

// Twee live bronnen voor dezelfde speler (Move = 0, muis = 1): de Move meldt ~80 Hz "los", de muis houdt het pedaal vast.
// Zoals OutputService::feed_live met src: de knoppen van beide bronnen tellen als OR.
static void test_live_sources() {
    FakeDevice d;
    GunOutput o(d);
    OverrideLayer ov;
    std::string err;
    CHECK(o.arm(err));
    const int gren = static_cast<int>(GunButton::Grenade);
    auto feed = [&](const LiveAction& a, int src) {
        o.set_button(GunButton::Trigger, ov.live_button(GunButton::Trigger, a.trigger, src));
        o.set_button(GunButton::Grenade, ov.live_button(GunButton::Grenade, a.grenade, src));
    };
    LiveAction mouse_down, move_up;
    mouse_down.buttons = move_up.buttons = true;
    mouse_down.grenade = true;
    feed(mouse_down, 1);                                 // muis: middenknop (granaat, bv. het pedaal van Time Crisis 5) vast
    for (int i = 0; i < 80; ++i) {                       // een seconde Move-rapporten zonder knoppen
        feed(move_up, 0);
        CHECK(d.pressed.count(gren) && o.button_down(GunButton::Grenade));
    }
    CHECK(d.count("down grenade") == 1 && d.count("up grenade") == 0);   // geen geflikker
    LiveAction mouse_up;
    mouse_up.buttons = true;
    feed(mouse_up, 1);                                   // muis laat los: nu pas los
    CHECK(!d.pressed.count(gren) && d.count("up grenade") == 1);
    // Omgekeerd: de Move drukt de trekker, een muisrapport zonder knoppen laat hem staan.
    LiveAction move_trig = move_up;
    move_trig.trigger = true;
    feed(move_trig, 0);
    feed(mouse_up, 1);
    CHECK(o.button_down(GunButton::Trigger));
    feed(move_up, 0);
    CHECK(!o.button_down(GunButton::Trigger));
    // clear() (bronuitval, disarm) wist de stand van alle bronnen.
    feed(mouse_down, 1);
    ov.clear();
    o.release_all();
    feed(move_up, 0);
    CHECK(!o.button_down(GunButton::Grenade));
    // Een ongeldige bron telt als de Move.
    CHECK(ov.live_button(GunButton::Aux1, true, 7) && ov.live_button(GunButton::Aux1, false, 0) == false);
}

static void test_override_layer() {
    FakeDevice d;
    FakeKbd kb;
    GunOutput o(d, &kb);
    OverrideLayer ov;
    LiveGunFeed feed(build_live_map(config::GunButtonSettings{}));   // Vierkant = toets 1, Kruis = herladen
    std::string err;
    CHECK(o.arm(err));
    const int trig = static_cast<int>(GunButton::Trigger), rel = static_cast<int>(GunButton::Reload);

    // Een tap van de trekker terwijl de Move ~80 Hz "niet ingedrukt" meldt: de druk blijft de hele tap staan.
    const uint64_t t1 = ov.tap_button(GunButton::Trigger);
    CHECK(o.set_button(GunButton::Trigger, true));
    for (int i = 0; i < 7; ++i) {                       // 7 rapporten x 12,5 ms = de tap van 80 ms
        apply_live(o, ov, feed.update(sample(0)));
        CHECK(d.pressed.count(trig) && o.button_down(GunButton::Trigger));
    }
    ov.end_tap_button(GunButton::Trigger, t1);
    CHECK(o.set_button(GunButton::Trigger, ov.want_button(GunButton::Trigger)));
    CHECK(!d.pressed.count(trig) && d.count("down trigger") == 1 && d.count("up trigger") == 1);   // een druk, een los: geen geflikker

    // Een toets die de Move ook koppelt (Vierkant = toets 1) en een die hij niet koppelt (0).
    const uint64_t k1 = ov.tap_key(KEY_1), k0 = ov.tap_key(KEY_0);
    CHECK(o.set_key(KEY_1, true) && o.set_key(KEY_0, true));
    for (int i = 0; i < 7; ++i) {
        apply_live(o, ov, feed.update(sample(0)));
        CHECK(kb.pressed == (std::set<int>{KEY_1, KEY_0}));
    }
    ov.end_tap_key(KEY_1, k1);
    ov.end_tap_key(KEY_0, k0);
    CHECK(o.set_key(KEY_1, ov.want_key(KEY_1)) && o.set_key(KEY_0, ov.want_key(KEY_0)));
    CHECK(kb.pressed.empty());

    // Live drukken werkt ongewijzigd: Vierkant down/up volgt de Move, Kruis = herladen.
    apply_live(o, ov, feed.update(sample(core::kBtnSquare | core::kBtnCross)));
    CHECK(kb.pressed == std::set<int>{KEY_1} && d.pressed.count(rel));
    apply_live(o, ov, feed.update(sample(0)));
    CHECK(kb.pressed.empty() && !d.pressed.count(rel));

    // Live loslaten wist een synthetische down niet; pas up (of clear) laat hem los. Live OR synthetisch.
    ov.hold_button(GunButton::Reload);
    CHECK(o.set_button(GunButton::Reload, true));
    apply_live(o, ov, feed.update(sample(core::kBtnCross)));
    apply_live(o, ov, feed.update(sample(0)));
    CHECK(d.pressed.count(rel));
    ov.release_button(GunButton::Reload);
    CHECK(o.set_button(GunButton::Reload, ov.want_button(GunButton::Reload)) && !d.pressed.count(rel));
    // up terwijl de Move zelf nog drukt: de live druk blijft staan.
    apply_live(o, ov, feed.update(sample(core::kBtnCross)));
    ov.hold_button(GunButton::Reload);
    ov.release_button(GunButton::Reload);
    CHECK(ov.want_button(GunButton::Reload) && d.pressed.count(rel));
    apply_live(o, ov, feed.update(sample(0)));
    CHECK(!d.pressed.count(rel));

    // Een nieuwere tap vervangt de oudere: het einde van de oude laat de nieuwe staan.
    const uint64_t a = ov.tap_button(GunButton::Grenade);
    const uint64_t b = ov.tap_button(GunButton::Grenade);
    CHECK(a != b);
    ov.end_tap_button(GunButton::Grenade, a);
    CHECK(ov.held_button(GunButton::Grenade));
    ov.end_tap_button(GunButton::Grenade, b);
    CHECK(!ov.held_button(GunButton::Grenade) && !ov.any_synthetic());
    // Een tap na een down laat de down staan.
    ov.hold_key(KEY_5);
    const uint64_t kt = ov.tap_key(KEY_5);
    ov.end_tap_key(KEY_5, kt);
    CHECK(ov.held_key(KEY_5));
    ov.release_key(KEY_5);
    CHECK(!ov.held_key(KEY_5) && !ov.any_synthetic());

    // Bronuitval, disarm en een stale token: alles los, ook de synthetische stand.
    ov.hold_button(GunButton::Trigger);
    ov.hold_key(KEY_0);
    const uint64_t stale = ov.tap_key(KEY_5);
    CHECK(o.set_button(GunButton::Trigger, true) && o.set_key(KEY_0, true) && o.set_key(KEY_5, true));
    apply_live(o, ov, feed.update(sample(0, 0, 0, core::LinkState::Lost)));
    CHECK(d.pressed.empty() && kb.pressed.empty() && !ov.any_synthetic() && o.armed());
    ov.end_tap_key(KEY_5, stale);                        // een tap die na de bronuitval afloopt doet niets
    CHECK(!ov.held_key(KEY_5) && !ov.want_key(KEY_5));
    ov.hold_button(GunButton::Aux1);
    ov.clear();
    CHECK(!ov.any_synthetic() && !ov.want_button(GunButton::Aux1));
    o.disarm();
    CHECK(d.pressed.empty() && kb.pressed.empty());
}

int main() {
    test_gun_identity();
    test_key_mux();
    test_key_mux_min_hold();
    test_nav_report();
    test_live_feed_nav();
    test_key_table();
    test_live_map_defaults();
    test_live_feed_layout();
    test_live_feed_sharpshooter();
    test_keyboard_output();
    test_override_layer();
    test_live_sources();
    test_live_feed();
    test_transform_basic();
    test_transform_invert();
    test_transform_scale_offset_clamp();
    test_transform_invalid();
    test_letterbox();
    test_default_disarmed();
    test_arm_and_events();
    test_disarm_releases_buttons();
    test_release_all_keeps_armed();
    test_write_error_releases();
    test_shutdown_and_destructor_release();
    test_transform_applied_in_output();
    test_button_names();
    test_patterns();
    test_pattern_through_output();
    test_ipc();
    test_aim_clock_extrapolation_limit();
    test_aim_clock_grid();
    test_clock_smooth_and_stops();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
