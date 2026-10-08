// ravengun-driftlab: experimenten naar de oorzaak van bewegingsafhankelijke yaw-drift (Taak 1, 2026-10-08).
//
// Deze tool herhaalt de integratie van Pipeline::on_report (zelfde biasschatter, zelfde Move-klok, zelfde
// gat-interpolatie) maar met schakelbare varianten, en meet op dezelfde manier als `ravengun-analyze drift`:
// de yaw van de richtas bij elk meetpunt (PS en/of Move-knop) en de cumulatieve verschuiving van het eerste
// naar het laatste meetpunt. Zonder varianten reproduceert dit de cijfers van `analyze drift` exact (`--check`).
//
// Gebruik:
//   ravengun-driftlab [opties] BESTAND:ps|move|both [BESTAND:...]
// Opties (alle varianten staan standaard uit = pijplijngedrag):
//   --kp X --accel-gate G       Mahony-versterking (1/s) en poort op | |a|-1 | (g)
//   --jerk-gate G               geen accel-correctie als |a - a_vorige| > G (g per subsample)
//   --rate-gate D               geen accel-correctie als |omega| > D graden/s
//   --gap off|interp|hold-prev|hold-next|zero   gatvulling (interp = pijplijn)
//   --dt rx|move                integratie-dt
//   --first-order               kleine-hoekintegratie i.p.v. exacte exponent
//   --scale X,Y,Z               schaalfactoren per body-as (1 = niets)
//   --cross a,b,c,d,e,f         kruiskoppeling: gx+=a*gy+b*gz; gy+=c*gx+d*gz; gz+=e*gx+f*gy (voor correctie)
//   --asym X,Y,Z                g_i += alfa_i*|g_i| (asymmetrie plus/min), per body-as
//   --bias-add X,Y,Z            extra offset (graden/s) op de bias, per body-as
//   --intervals                 print per interval kenmerken (voor regressies)
//   --bias-trace S              print elke S seconden de biasschatting (graden/s), bron en toestand
//   --maglog CAL.json S         per blok van S seconden: magnetometerheading tegen de gyro-yaw (fout van de gyro) naast de beweging
//   --yawdef aim|twist|x           yaw-definitie voor de meting (standaard azimut van de richtas)
//   --vqf 6|9 [--cal CAL.json]  meet met VQF (6D of 9D) i.p.v. Mahony; --vqf-bias own|external|hybrid; --vqf-set sleutel=waarde
//   --rest-span T0              stilliggende opname: yaw-verandering en lineaire helling vanaf T0 s tot het einde
//   --check                     vergelijk met compute_drift
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ravengun/core/aim.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/fusion.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/move_clock.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/core/vqf_estimator.hpp"
#include "ravengun/rec/drift.hpp"
#include "ravengun/rec/replay.hpp"

using namespace ravengun;
using namespace ravengun::core;

namespace {

enum class Gap { Interp, Off, HoldPrev, HoldNext, Zero };

struct Variant {
    double rest_from_s = -1;    // --rest-span T0: yaw-verandering van T0 tot het einde (stilliggende opname)
    bool seed_ref = true;
    int vqf = 0;                // 0 uit, 6 = VQF 6D, 9 = VQF 9D (vereist --cal)
    VqfConfig vc{};
    bool have_cal = false;
    MagCalibration cal{};
    double maglog_s = -1;
    double trace_s = -1;
    double kp = 0.5, accel_gate = 0.15;
    double jerk_gate = -1, rate_gate_dps = -1;
    Gap gap = Gap::Interp;
    bool dt_move = true;
    bool first_order = false;
    Vec3 scale{1, 1, 1};
    double cross[6] = {0, 0, 0, 0, 0, 0};
    Vec3 asym{0, 0, 0};
    Vec3 bias_add{0, 0, 0};   // rad/s
};

struct Feat {  // kenmerken per interval
    double dur = 0, yaw = 0;
    double abs_b[3] = {0, 0, 0};    // integraal |omega_i| dt per body-as (graden)
    double sgn_b[3] = {0, 0, 0};    // integraal omega_i dt (graden)
    double abs_w[3] = {0, 0, 0};    // idem in het wereldstelsel (x, y, z=verticaal)
    double sgn_w[3] = {0, 0, 0};
    double prod_w[3] = {0, 0, 0};   // integraal |wx*wy|, wx*wy, wy*wz.. (zie code)
    double acc_dyn = 0;             // integraal | |a|-1 | dt (g s)
    double acc_use = 0;             // tijd (s) dat de accel-correctie actief was
    double lost = 0, reports = 0;
    double dyaw_acc = 0;            // door de accel-correctie toegepaste draaiing om de verticaal (graden)
};

struct Result {
    double cum_deg = NAN, cum_s = NAN;
    double med_abs_per_min = NAN;
    std::vector<double> iv_yaw, iv_dur;
    std::vector<Feat> feats;
    int presses = 0;
};

int g_yawdef = 0;  // 0 = azimut van de richtas (zoals drift.cpp), 1 = twist om wereld-Z, 2 = azimut van body-X, 3 = azimut van body-Z-projectie
double yaw_of(Quat q) {
    switch (g_yawdef) {
        case 1: return -2 * std::atan2(q.z, q.w) * kDegPerRad;  // twist om Z (teken: yaw positief naar rechts = klokwijs)
        case 2: { Vec3 d = rotate(q, Vec3{1, 0, 0}); return std::atan2(d.x, d.y) * kDegPerRad; }
        default: return world_angles(q).yaw_rad * kDegPerRad;
    }
}

Vec3 apply_xform(const Variant& v, Vec3 g) {
    // volgorde: kruiskoppeling, schaal, asymmetrie
    Vec3 h = g;
    h.x += v.cross[0] * g.y + v.cross[1] * g.z;
    h.y += v.cross[2] * g.x + v.cross[3] * g.z;
    h.z += v.cross[4] * g.x + v.cross[5] * g.y;
    h = {h.x * v.scale.x, h.y * v.scale.y, h.z * v.scale.z};
    h.x += v.asym.x * std::fabs(g.x);
    h.y += v.asym.y * std::fabs(g.y);
    h.z += v.asym.z * std::fabs(g.z);
    return h;
}

Result run(const std::vector<rec::Record>& recs, const Variant& v, uint32_t marker_mask, const BiasConfig& bc) {
    Result res;
    FusionConfig fc;
    fc.kp = v.kp;
    fc.accel_gate = v.accel_gate;
    OrientationFilter fusion(fc);
    GyroBiasEstimator bias(bc);
    MoveClock clock;
    VqfEstimator vq(v.vc);
    vq.set_use_mag(v.vqf == 9);
    if (v.vqf == 9 && v.have_cal && v.seed_ref) vq.set_mag_reference(1.0, v.cal.dip_ref_deg);
    PipelineConfig pc;  // voor nominale rapporttijd en max_dt
    const double nominal = pc.nominal_report_s;

    ReportStats stats;
    bool have_rx = false, prev_had_stamp = false, have_prev_gyro = false;
    int64_t last_rx = 0;
    int last_temp = -1;
    Vec3 prev_gyro{}, prev_acc{};
    bool have_prev_acc = false;
    uint32_t prev_buttons = 0;
    int last_seq = -1;
    uint64_t lost_acc = 0, rep_acc = 0;
    const int64_t t0 = recs.empty() ? 0 : recs[0].header.t0_ns;

    bool have_ref = false;
    double ref_yaw = 0, ref_t = 0, cum = 0, cum_prev = 0, cum_t0 = 0, cum_t1 = 0;
    size_t cum_n = 0;
    Feat f;
    std::vector<double> absall;
    // magnetometerlog per blok
    struct Blk { double t0 = -1, cs = 0, sn = 0; int n = 0; double w = 0, wz = 0, wzs = 0, dyn = 0, rot = 0; int nr = 0; Quat pq{}; bool hq = false; double sx = 0, sy = 0, sz = 0; double pitch = 0; };
    Blk blk;
    std::vector<std::pair<double, double>> yseries;  // (t, ongewikkelde yaw) elke ~1 s, voor --rest-span
    double last_y = 0, unwrap = 0, next_y_t = 0;
    bool have_y = false;

    for (const rec::Record& rc : recs) {
        if (rc.kind != rec::Kind::Report) continue;
        const RawReport& r = rc.report;
        double dt = 0;
        if (have_rx && r.t_rx_ns >= last_rx) dt = static_cast<double>(r.t_rx_ns - last_rx) / 1e9;
        double dt_int = dt;
        bool used_move = false;
        ClockSample cs{};
        if (r.has_stamp) {
            cs = clock.add(r.stamp, r.t_rx_ns);
            if (v.dt_move && have_rx && prev_had_stamp && !cs.first) { dt_int = cs.dt_s; used_move = true; }
        }
        (void)used_move;
        prev_had_stamp = r.has_stamp;
        if (r.temperature_raw >= 0) last_temp = r.temperature_raw;

        // verloren rapporten (zelfde telling als drift.cpp)
        ++rep_acc;
        if (last_seq >= 0 && r.hid_seq >= 0) { int d = (r.hid_seq - last_seq) & 15; lost_acc += d ? static_cast<uint64_t>(d - 1) : 0; }
        if (r.hid_seq >= 0) last_seq = r.hid_seq;

        const int n = std::clamp(r.subsamples, 1, 2);
        double sub_dt = dt_int / n;
        if (dt_int > fc.max_dt_s) { sub_dt = 0; have_prev_gyro = false; }
        if (!fusion.initialized()) fusion.init_from_accel(r.accel[0]);
        double fuse_dt = sub_dt, gap_dt = 0;
        const bool is_gap = n == 2 && have_prev_gyro && sub_dt > 0 && dt_int > 1.5 * nominal;
        if (v.gap != Gap::Off && is_gap) {
            gap_dt = dt_int - nominal;
            fuse_dt = nominal / 2;
        }
        bias.set_temperature(last_temp);
        bias.set_quasi_enabled(true);
        for (int i = 0; i < n; ++i) {
            bias.update(r.gyro[i], r.accel[i], sub_dt);
            Vec3 b = bias.bias() + v.bias_add;
            auto step = [&](Vec3 graw, Vec3 acc, double h) {
                Vec3 g = apply_xform(v, graw - b);
                Vec3 a = acc;
                bool use = true;
                if (v.jerk_gate >= 0 && have_prev_acc && norm(acc - prev_acc) > v.jerk_gate) use = false;
                if (v.rate_gate_dps >= 0 && norm(g) * kDegPerRad > v.rate_gate_dps) use = false;
                if (!use) a = Vec3{0, 0, 0};
                Quat qb = fusion.q();
                if (!(h > 0) || h > fc.max_dt_s) { fusion.update(g, a, h); return; }
                if (v.first_order && fusion.initialized()) {
                    // zelfde accel-term als OrientationFilter, maar q += 0.5 q (x) (0,w) dt
                    Vec3 gg = g;
                    if (norm(a) > 1e-6 && std::fabs(norm(a) - 1.0) <= fc.accel_gate && fc.kp > 0) {
                        Vec3 an_ = a * (1.0 / norm(a));
                        Vec3 vv = rotate(conj(qb), Vec3{0, 0, 1});
                        gg = gg + cross(an_, vv) * fc.kp;
                    }
                    Quat dq = qb * Quat{0, gg.x, gg.y, gg.z};
                    Quat q2{qb.w + 0.5 * h * dq.w, qb.x + 0.5 * h * dq.x, qb.y + 0.5 * h * dq.y, qb.z + 0.5 * h * dq.z};
                    fusion.set_q(q2);
                } else {
                    fusion.update(g, a, h);
                }
                // kenmerken
                const double hd = h;
                Quat qa = fusion.q();
                Vec3 gw = rotate(qa, g);
                f.dur += hd;
                f.abs_b[0] += std::fabs(g.x) * hd * kDegPerRad; f.abs_b[1] += std::fabs(g.y) * hd * kDegPerRad; f.abs_b[2] += std::fabs(g.z) * hd * kDegPerRad;
                f.sgn_b[0] += g.x * hd * kDegPerRad; f.sgn_b[1] += g.y * hd * kDegPerRad; f.sgn_b[2] += g.z * hd * kDegPerRad;
                f.abs_w[0] += std::fabs(gw.x) * hd * kDegPerRad; f.abs_w[1] += std::fabs(gw.y) * hd * kDegPerRad; f.abs_w[2] += std::fabs(gw.z) * hd * kDegPerRad;
                f.sgn_w[0] += gw.x * hd * kDegPerRad; f.sgn_w[1] += gw.y * hd * kDegPerRad; f.sgn_w[2] += gw.z * hd * kDegPerRad;
                f.prod_w[0] += std::fabs(gw.x * gw.y) * hd; f.prod_w[1] += gw.x * gw.y * hd; f.prod_w[2] += gw.y * gw.z * hd;
                f.acc_dyn += std::fabs(norm(acc) - 1.0) * hd;
                if (fusion.last_accel_used()) f.acc_use += hd;
                prev_acc = acc;
                have_prev_acc = true;
            };
            if (i == 0 && gap_dt > 0) {
                Vec3 gm{};
                switch (v.gap) {
                    case Gap::Interp: gm = (prev_gyro + r.gyro[0]) * 0.5; break;
                    case Gap::HoldPrev: gm = prev_gyro; break;
                    case Gap::HoldNext: gm = r.gyro[0]; break;
                    case Gap::Zero: gm = b; break;
                    default: break;
                }
                step(gm, r.accel[0], gap_dt);
            }
            step(r.gyro[i], r.accel[i], fuse_dt);
        }
        if (v.vqf) {
            Vec3 mb{};
            const Vec3* mp = nullptr;
            if (v.vqf == 9 && v.have_cal) { mb = mag_apply(v.cal, r.mag_raw); mp = &mb; }
            vq.feed(r, dt_int, bias.bias() + v.bias_add, mp);
            if (dt_int > fc.max_dt_s) vq.on_interrupt();
        }
        prev_gyro = r.gyro[n - 1];
        have_prev_gyro = true;
        if (v.maglog_s > 0 && v.have_cal) {
            const double tt = static_cast<double>(r.t_rx_ns - t0) / 1e9;
            if (blk.t0 < 0) blk.t0 = tt;
            Vec3 gavg = (r.gyro[0] + r.gyro[n - 1]) * 0.5 - bias.bias();
            Vec3 gw = rotate(fusion.q(), gavg);
            blk.w += norm(gavg) * kDegPerRad; blk.wz += std::fabs(gw.z) * kDegPerRad; blk.wzs += gw.z * kDegPerRad; ++blk.nr;
            blk.dyn += std::fabs(norm(r.accel[n - 1]) - 1.0);
            if (blk.hq) blk.rot += angle_between(blk.pq, fusion.q()) * kDegPerRad;
            blk.pq = fusion.q(); blk.hq = true;
            blk.pitch += world_angles(fusion.q()).pitch_rad * kDegPerRad;
            {
                Vec3 am = (r.accel[0] + r.accel[n - 1]) * 0.5;
                double an = norm(am);
                Vec3 m = mag_apply(v.cal, r.mag_raw);
                if (an > 0 && std::fabs(an - 1) < 0.05 && norm(gavg) * kDegPerRad < 120) {
                    Vec3 up = am * (1 / an);
                    Vec3 nh = m - up * dot(m, up);
                    Vec3 d = kAimAxisBody - up * dot(kAimAxisBody, up);
                    if (norm(nh) > 0.25 * norm(m) && norm(d) > 0.3) {
                        nh = nh * (1 / norm(nh));
                        double H = std::atan2(dot(d, cross(nh, up)), dot(d, nh));
                        double phi = wrap_pi(world_angles(fusion.q()).yaw_rad - H);
                        blk.cs += std::cos(phi); blk.sn += std::sin(phi); ++blk.n;
                    }
                }
            }
            if (tt - blk.t0 >= v.maglog_s) {
                std::printf("M %7.1f n %4d phi %+8.2f | w %6.1f wz %6.1f wzs %+6.2f dyn %.3f rot %6.1f pitch %+6.1f\n", blk.t0, blk.n,
                            blk.n ? std::atan2(blk.sn, blk.cs) * kDegPerRad : NAN, blk.w / blk.nr, blk.wz / blk.nr, blk.wzs / blk.nr, blk.dyn / blk.nr, blk.rot, blk.pitch / blk.nr);
                blk = Blk{};
            }
        }
        if (v.trace_s > 0) {
            const double tt = static_cast<double>(r.t_rx_ns - t0) / 1e9;
            static double next_t = 0;
            if (tt < next_t - 1e6 || next_t == 0) next_t = tt;
            if (tt >= next_t) {
                Vec3 b = bias.bias();
                std::printf("T %7.1f bias %+.3f %+.3f %+.3f  tc %d  %s/%s still %.1f s quasi %.1f s\n", tt, b.x * kDegPerRad, b.y * kDegPerRad, b.z * kDegPerRad,
                            last_temp, to_string(bias.state()), to_string(bias.source()), bias.still_time_total(), bias.quasi_time_total());
                next_t += v.trace_s;
            }
        }
        have_rx = true;
        last_rx = r.t_rx_ns;

        if (v.rest_from_s >= 0) {
            const double tt = static_cast<double>(r.t_rx_ns - t0) / 1e9;
            if (tt >= next_y_t) {
                const double y = yaw_of(v.vqf ? vq.q() : fusion.q());
                if (have_y) unwrap += wrap_pi((y - last_y) * kRadPerDeg) * kDegPerRad;
                else unwrap = 0;
                last_y = y;
                have_y = true;
                yseries.emplace_back(tt, unwrap);
                next_y_t = tt + 1.0;
            }
        }
        const uint32_t pressed = r.buttons & ~prev_buttons;
        prev_buttons = r.buttons;
        if (!(pressed & marker_mask)) continue;
        ++res.presses;
        const double t = static_cast<double>(r.t_rx_ns - t0) / 1e9;
        const double yaw = yaw_of(v.vqf ? vq.q() : fusion.q());
        if (cum_n == 0) cum_t0 = t;
        else cum += wrap_pi((yaw - cum_prev) * kRadPerDeg) * kDegPerRad;
        cum_prev = yaw;
        cum_t1 = t;
        ++cum_n;
        if (have_ref) {
            const double dur = t - ref_t;
            if (dur >= 2.0) {
                f.yaw = wrap_pi((yaw - ref_yaw) * kRadPerDeg) * kDegPerRad;
                f.lost = static_cast<double>(lost_acc);
                f.reports = static_cast<double>(rep_acc);
                res.iv_yaw.push_back(f.yaw);
                res.iv_dur.push_back(dur);
                absall.push_back(std::fabs(f.yaw / dur * 60));
                f.dur = dur;
                res.feats.push_back(f);
            }
        }
        have_ref = true;
        ref_yaw = yaw; ref_t = t;
        f = Feat{};
        lost_acc = rep_acc = 0;
    }
    if (cum_n >= 2 && cum_t1 > cum_t0) { res.cum_deg = cum; res.cum_s = cum_t1 - cum_t0; }
    res.med_abs_per_min = rec::percentile(absall, 0.5);
    if (v.rest_from_s >= 0) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0, y0 = NAN, y1 = NAN, t0s = 0, t1s = 0, maxdev = 0;
        for (auto& [t, y] : yseries) if (t >= v.rest_from_s) {
            if (std::isnan(y0)) { y0 = y; t0s = t; }
            y1 = y; t1s = t;
            sx += t; sy += y; sxx += t * t; sxy += t * y; n += 1;
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        for (auto& [t, y] : yseries) if (t >= v.rest_from_s) maxdev = std::max(maxdev, std::fabs(y - y0));
        std::printf("   REST %5.0f..%5.0f s: yaw %+.3f gr (eind - begin) = %+.3f gr/min; lineaire helling %+.3f gr/min; max |afwijking| %.3f gr\n", t0s, t1s, y1 - y0,
                    (y1 - y0) / (t1s - t0s) * 60, slope * 60, maxdev);
    }
    if (v.vqf) {
        Vec3 b = vq.bias();
        std::printf("   VQF: bias eind %+.3f %+.3f %+.3f gr/s (ons %+.3f %+.3f %+.3f); rust %.1f %% van de rapporten; magnetometer verstoord %llu/%llu (%.1f %%); opgevuld %llu, overgeslagen %llu; ref |m| %.3f dip %.1f gr; delta %.1f gr\n",
                    b.x * kDegPerRad, b.y * kDegPerRad, b.z * kDegPerRad, bias.bias().x * kDegPerRad, bias.bias().y * kDegPerRad, bias.bias().z * kDegPerRad,
                    100.0 * static_cast<double>(vq.rest_samples()) / std::max<double>(1, static_cast<double>(vq.reports()) * 2),
                    static_cast<unsigned long long>(vq.mag_disturbed_samples()), static_cast<unsigned long long>(vq.mag_samples()),
                    100.0 * static_cast<double>(vq.mag_disturbed_samples()) / std::max<double>(1, static_cast<double>(vq.mag_samples())),
                    static_cast<unsigned long long>(vq.filled_reports()), static_cast<unsigned long long>(vq.skipped_reports()), vq.mag_ref_norm(), vq.mag_ref_dip_deg(), vq.heading_correction_deg());
    }
    return res;
}

bool parse3(const char* s, Vec3& out) {
    double a, b, c;
    if (std::sscanf(s, "%lf,%lf,%lf", &a, &b, &c) != 3) return false;
    out = {a, b, c};
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Variant v;
    bool show_iv = false, check = false;
    std::vector<std::pair<std::string, uint32_t>> files;
    BiasConfig bc;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--kp") v.kp = std::atof(next());
        else if (a == "--accel-gate") v.accel_gate = std::atof(next());
        else if (a == "--jerk-gate") v.jerk_gate = std::atof(next());
        else if (a == "--rate-gate") v.rate_gate_dps = std::atof(next());
        else if (a == "--gap") {
            std::string g = next();
            v.gap = g == "off" ? Gap::Off : g == "hold-prev" ? Gap::HoldPrev : g == "hold-next" ? Gap::HoldNext : g == "zero" ? Gap::Zero : Gap::Interp;
        } else if (a == "--dt") v.dt_move = std::string(next()) != "rx";
        else if (a == "--first-order") v.first_order = true;
        else if (a == "--scale") parse3(next(), v.scale);
        else if (a == "--asym") parse3(next(), v.asym);
        else if (a == "--bias-add") { parse3(next(), v.bias_add); v.bias_add = v.bias_add * kRadPerDeg; }
        else if (a == "--cross") std::sscanf(next(), "%lf,%lf,%lf,%lf,%lf,%lf", v.cross, v.cross + 1, v.cross + 2, v.cross + 3, v.cross + 4, v.cross + 5);
        else if (a == "--maglog") {
            std::string cp = next();
            FILE* fp = std::fopen(cp.c_str(), "re");
            std::string text, err;
            if (fp) { char b[4096]; size_t k; while ((k = std::fread(b, 1, sizeof b, fp)) > 0) text.append(b, k); std::fclose(fp); }
            if (!mag_calibration_from_json(text, v.cal, err)) { std::fprintf(stderr, "kalibratie: %s\n", err.c_str()); return 2; }
            v.have_cal = true;
            v.maglog_s = std::atof(next());
        } else if (a == "--yawdef") { std::string d = next(); g_yawdef = d == "twist" ? 1 : d == "x" ? 2 : 0; }
        else if (a == "--vqf") { v.vqf = std::atoi(next()); }
        else if (a == "--cal") {
            std::string cp = next();
            FILE* fp = std::fopen(cp.c_str(), "re");
            std::string text, err;
            if (fp) { char b[4096]; size_t k; while ((k = std::fread(b, 1, sizeof b, fp)) > 0) text.append(b, k); std::fclose(fp); }
            if (!mag_calibration_from_json(text, v.cal, err)) { std::fprintf(stderr, "kalibratie: %s\n", err.c_str()); return 2; }
            v.have_cal = true;
        } else if (a == "--vqf-bias") { if (!parse_vqf_bias(next(), v.vc.bias)) return 64; }
        else if (a == "--vqf-set") {
            std::string kv = next();
            size_t eq = kv.find('=');
            std::string k = kv.substr(0, eq);
            double x = std::atof(kv.c_str() + eq + 1);
            if (k == "rest_th_gyr") v.vc.rest_th_gyr_dps = x;
            else if (k == "rest_th_acc") v.vc.rest_th_acc = x;
            else if (k == "bias_clip") v.vc.bias_clip_dps = x;
            else if (k == "tau_acc") v.vc.tau_acc_s = x;
            else if (k == "tau_mag") v.vc.tau_mag_s = x;
            else if (k == "motion_bias") v.vc.motion_bias = x != 0;
            else if (k == "rest_bias") v.vc.rest_bias = x != 0;
            else if (k == "mag_rej") v.vc.mag_dist_rejection = x != 0;
            else if (k == "seed_ref") v.seed_ref = x != 0;
            else return 64;
        }
        else if (a == "--rest-span") v.rest_from_s = std::atof(next());
        else if (a == "--bias-trace") v.trace_s = std::atof(next());
        else if (a == "--intervals") show_iv = true;
        else if (a == "--check") check = true;
        else {
            size_t c = a.rfind(':');
            std::string path = c == std::string::npos ? a : a.substr(0, c);
            std::string m = c == std::string::npos ? "ps" : a.substr(c + 1);
            uint32_t mask = m == "move" ? kBtnMove : m == "both" ? (kBtnPS | kBtnMove) : kBtnPS;
            files.emplace_back(path, mask);
        }
    }
    int rc = 0;
    for (auto& [path, mask] : files) {
        std::vector<rec::Record> recs;
        std::string err;
        if (!rec::load_file(path, recs, err)) { std::fprintf(stderr, "driftlab: %s: %s\n", path.c_str(), err.c_str()); return 2; }
        Result r = run(recs, v, mask, bc);
        const char* base = std::strrchr(path.c_str(), '/');
        std::printf("%-44s cum %+7.2f gr in %5.1f s = %+7.2f gr/min | med|yaw| %6.2f gr/min | %zu iv\n", base ? base + 1 : path.c_str(), r.cum_deg, r.cum_s,
                    r.cum_deg / r.cum_s * 60, r.med_abs_per_min, r.feats.size());
        if (check) {
            rec::DriftOptions o;
            o.marker_mask = mask;
            rec::DriftResult d = rec::compute_drift(recs, o);
            std::printf("   compute_drift: cum %+7.2f gr in %5.1f s\n", d.summary.total_yaw_deg, d.summary.total_s);
            if (std::fabs(d.summary.total_yaw_deg - r.cum_deg) > 1e-6) rc = 1;
        }
        if (show_iv) {
            std::printf("# dur yaw | absb(x y z) | sgnb(x y z) | absw(x y z) | sgnw(x y z) | prodw | accdyn accuse | lost reports\n");
            for (const Feat& f : r.feats)
                std::printf("IV %6.2f %+7.3f | %7.1f %7.1f %7.1f | %+7.1f %+7.1f %+7.1f | %7.1f %7.1f %7.1f | %+7.2f %+7.2f %+7.2f | %8.3f %8.3f %8.3f | %7.3f %7.3f | %.0f %.0f\n",
                            f.dur, f.yaw, f.abs_b[0], f.abs_b[1], f.abs_b[2], f.sgn_b[0], f.sgn_b[1], f.sgn_b[2], f.abs_w[0], f.abs_w[1], f.abs_w[2],
                            f.sgn_w[0], f.sgn_w[1], f.sgn_w[2], f.prod_w[0], f.prod_w[1], f.prod_w[2], f.acc_dyn, f.acc_use, f.lost, f.reports);
        }
    }
    return rc;
}
