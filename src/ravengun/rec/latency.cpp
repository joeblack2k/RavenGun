#include "ravengun/rec/latency.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ravengun/core/move_clock.hpp"

namespace ravengun::rec {

namespace {
constexpr size_t kShifts = kShiftMaxMs - kShiftMinMs + 1;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kMaxSpanS = 0.08;  // verder uit elkaar dan dit wordt niet geïnterpoleerd

double hypot2(double dx, double dy) { return std::sqrt(dx * dx + dy * dy); }

// Waarheid (px) op sensortijd tt; false buiten het bereik of over een groot gat. j = lopende index (tt stijgt).
bool truth_at(const Track& b, size_t& j, double tt, double& x, double& y) {
    const size_t n = b.t.size();
    if (n < 2 || tt < b.t.front() || tt > b.t.back()) return false;
    while (j + 1 < n && b.t[j + 1] < tt) ++j;
    while (j > 0 && b.t[j] > tt) --j;
    if (j + 1 >= n) return false;
    const double t0 = b.t[j], t1 = b.t[j + 1];
    if (t1 - t0 > kMaxSpanS || t1 <= t0) return false;
    const double u = (tt - t0) / (t1 - t0);
    x = b.tx[j] + u * (b.tx[j + 1] - b.tx[j]);
    y = b.ty[j] + u * (b.ty[j + 1] - b.ty[j]);
    return true;
}
}  // namespace

double lat_percentile(std::vector<double> v, double p) {
    if (v.empty()) return kNaN;
    size_t k = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1) + 0.5);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
    return v[k];
}

Track collect_track(const std::vector<Record>& recs, const ReplayOptions& o0) {
    Track tr;
    ReplayOptions o = o0;
    tr.nominal_s = o.pipeline.nominal_report_s;
    bool have_prev = false;
    uint16_t prev_stamp = 0;
    int64_t prev_rx = 0;
    double t_phys = 0;
    bool first = true;
    double ticks_per_s = 100000.0;
    int64_t first_rx = 0, last_rx = 0;
    o.on_sample = [&](const core::InputSample& s, const core::RawReport* r) {
        if (!r) return;
        ++tr.reports;
        if (first) { first_rx = r->t_rx_ns; first = false; }
        last_rx = r->t_rx_ns;
        // Sensortijd: de Move-stempel ontrold (wrap na 65536 ticks; het aantal gemiste wraps volgt uit de hosttijd).
        double dt = 0;
        if (have_prev) {
            const double host_s = static_cast<double>(r->t_rx_ns - prev_rx) / 1e9;
            if (r->has_stamp) {
                int64_t d = static_cast<uint16_t>(r->stamp - prev_stamp);
                const double expect = host_s * ticks_per_s;
                const int64_t k = static_cast<int64_t>(std::llround((expect - static_cast<double>(d)) / 65536.0));
                if (k > 0) d += k * 65536;
                dt = static_cast<double>(d) / ticks_per_s;
            } else {
                dt = host_s;
            }
        }
        t_phys += dt;
        have_prev = true;
        prev_stamp = r->stamp;
        prev_rx = r->t_rx_ns;
        if (!s.aim_valid) return;
        tr.t.push_back(t_phys);
        tr.rx.push_back(r->t_rx_ns);
        tr.tx.push_back(s.x_raw * kScreenW);
        tr.ty.push_back(s.y_raw * kScreenH);
        tr.ox.push_back(s.aim.x * kScreenW);
        tr.oy.push_back(s.aim.y * kScreenH);
        tr.vx.push_back(s.aim_vx * kScreenW);
        tr.vy.push_back(s.aim_vy * kScreenH);
        tr.age.push_back(s.age_ms);
        tr.gap.push_back(dt > 0 && dt > o.pipeline.nominal_report_s * 1.5 ? 1 : 0);
    };
    ReplayResult res = replay_records(recs, o);
    (void)res;
    tr.duration_s = static_cast<double>(last_rx - first_rx) / 1e9;
    return tr;
}

TruthInfo analyze_truth(const Track& b, const LatencyParams& p) {
    TruthInfo ti;
    const size_t n = b.t.size();
    ti.speed.assign(n, 0);
    ti.band.assign(n, 0);
    ti.still.assign(n, 0);
    ti.hold.assign(n, 0);
    if (n < 8) return ti;
    // Snelheid: centraal verschil over ±2 rapporten (~45 ms), alleen over korte spans.
    for (size_t k = 2; k + 2 < n; ++k) {
        const double dt = b.t[k + 2] - b.t[k - 2];
        if (dt <= 0 || dt > 0.12) { ti.speed[k] = -1; continue; }
        ti.speed[k] = hypot2(b.tx[k + 2] - b.tx[k - 2], b.ty[k + 2] - b.ty[k - 2]) / dt;
    }
    for (size_t k = 0; k < n; ++k) {
        const double v = ti.speed[k];
        if (v >= p.motion_px_s) {
            ti.band[k] = v >= p.band_fast_px_s ? 3 : v >= p.band_mid_px_s ? 2 : 1;
            ++ti.n_motion;
        }
    }
    // Stilstands- en rustig-houden-segmenten: aaneengesloten, snelheid onder de drempel, minstens still_min_s.
    for (int pass = 0; pass < 2; ++pass) {
        const double thr = pass == 0 ? p.still_px_s : p.hold_px_s;
        std::vector<char>& mark = pass == 0 ? ti.still : ti.hold;
        size_t& cnt = pass == 0 ? ti.n_still : ti.n_hold;
        size_t k = 0;
        while (k < n) {
            if (!(ti.speed[k] >= 0 && ti.speed[k] < thr)) { ++k; continue; }
            size_t e = k;
            while (e + 1 < n && ti.speed[e + 1] >= 0 && ti.speed[e + 1] < thr && b.t[e + 1] - b.t[e] < kMaxSpanS) ++e;
            if (b.t[e] - b.t[k] >= p.still_min_s) {
                for (size_t i = k; i <= e; ++i) mark[i] = 1;
                cnt += e - k + 1;
            }
            k = e + 1;
        }
    }
    // Abrupte stops.
    size_t last_stop = 0;
    for (size_t i = 3; i + 1 < n; ++i) {
        if (!(ti.speed[i] >= 0 && ti.speed[i] < p.stop_to_px_s)) continue;
        if (ti.speed[i - 1] < p.stop_to_px_s) continue;  // alleen de overgang
        if (i < last_stop + 10) continue;
        // Abrupt: ~70 ms eerder was de snelheid nog minstens stop_from_px_s. j = begin van de 150 ms ervoor (richting).
        size_t j = i, j70 = i;
        while (j > 0 && b.t[i] - b.t[j - 1] <= 0.15) { --j; if (b.t[i] - b.t[j] <= 0.07) j70 = j; }
        if (ti.speed[j70] < p.stop_from_px_s) continue;
        // Daarna rustig voor stop_rest_s.
        double sx = 0, sy = 0;
        size_t m = 0;
        bool calm = true;
        size_t e = i;
        while (e + 1 < n && b.t[e + 1] - b.t[i] <= p.stop_rest_s) {
            ++e;
            if (ti.speed[e] < 0 || ti.speed[e] > p.stop_to_px_s * 1.5) { calm = false; break; }
        }
        if (!calm || b.t.back() - b.t[i] < p.stop_rest_s) continue;
        for (size_t q = i + 2; q <= e; ++q) { sx += b.tx[q]; sy += b.ty[q]; ++m; }
        if (m < 3) continue;
        // Richting: gemiddelde verplaatsing in de 150 ms voor de stop.
        double dx = b.tx[i] - b.tx[j], dy = b.ty[i] - b.ty[j];
        const double dn = hypot2(dx, dy);
        if (dn < 1) continue;
        StopEvent se;
        se.k = i;
        se.dirx = dx / dn;
        se.diry = dy / dn;
        se.restx = sx / static_cast<double>(m);
        se.resty = sy / static_cast<double>(m);
        ti.stops.push_back(se);
        last_stop = i;
    }
    return ti;
}

void LatencyAccum::merge(const LatencyAccum& o) {
    for (int b = 0; b < kBands; ++b)
        for (size_t i = 0; i < kShifts; ++i) { cost_sum[b][i] += o.cost_sum[b][i]; cost_n[b][i] += o.cost_n[b][i]; }
    jit_out_sq += o.jit_out_sq;
    jit_truth_sq += o.jit_truth_sq;
    jit_n += o.jit_n;
    hold_out_sq += o.hold_out_sq;
    hold_truth_sq += o.hold_truth_sq;
    hold_n += o.hold_n;
    auto cat = [](std::vector<double>& a, const std::vector<double>& b) { a.insert(a.end(), b.begin(), b.end()); };
    cat(overshoot_out, o.overshoot_out);
    cat(overshoot_truth, o.overshoot_truth);
    cat(overshoot_excess, o.overshoot_excess);
    cat(jump_gap_plain, o.jump_gap_plain);
    cat(jump_gap_extrap, o.jump_gap_extrap);
    cat(jump_gap_move_plain, o.jump_gap_move_plain);
    cat(jump_gap_move_extrap, o.jump_gap_move_extrap);
    cat(jump_all_plain, o.jump_all_plain);
    cat(jump_all_extrap, o.jump_all_extrap);
    hold_sq += o.hold_sq;
    hold_total += o.hold_total;
    hold_sq_clk += o.hold_sq_clk;
    samples += o.samples;
}

LatencyAccum evaluate(const Track& c, const Track& b, const TruthInfo& ti, const LatencyParams& p) {
    LatencyAccum a;
    const size_t n = c.t.size();
    if (n != b.t.size() || n < 16) return a;
    a.samples = n;

    // 1. Kostencurve per verschuiving: uitvoer(t) tegen waarheid(t + L), alleen tijdens beweging.
    for (size_t li = 0; li < kShifts; ++li) {
        const double shift_s = (kShiftMinMs + static_cast<int>(li)) * 1e-3;
        size_t j = 0;
        for (size_t k = 0; k < n; ++k) {
            const int band = ti.band[k];
            if (!band) continue;
            double tx, ty;
            if (!truth_at(b, j, c.t[k] + shift_s, tx, ty)) continue;
            const double dx = c.ox[k] - tx, dy = c.oy[k] - ty;
            const double e2 = dx * dx + dy * dy;
            a.cost_sum[0][li] += e2;
            ++a.cost_n[0][li];
            a.cost_sum[band][li] += e2;
            ++a.cost_n[band][li];
        }
    }

    // 2. Jitter in stilstand en bij rustig houden: hoogfrequente component (uitvoer min lopend gemiddelde van ±11 rapporten).
    constexpr size_t W = 11;
    for (size_t k = W; k + W < n; ++k) {
        bool all_still = true, all_hold = true;
        for (size_t q = k - W; q <= k + W && (all_still || all_hold); ++q) { all_still = all_still && ti.still[q]; all_hold = all_hold && ti.hold[q]; }
        if (!all_still && !all_hold) continue;
        double mox = 0, moy = 0, mtx = 0, mty = 0;
        for (size_t q = k - W; q <= k + W; ++q) { mox += c.ox[q]; moy += c.oy[q]; mtx += b.tx[q]; mty += b.ty[q]; }
        const double inv = 1.0 / static_cast<double>(2 * W + 1);
        const double dox = c.ox[k] - mox * inv, doy = c.oy[k] - moy * inv;
        const double dtx = b.tx[k] - mtx * inv, dty = b.ty[k] - mty * inv;
        if (all_still) { a.jit_out_sq += dox * dox + doy * doy; a.jit_truth_sq += dtx * dtx + dty * dty; ++a.jit_n; }
        if (all_hold) { a.hold_out_sq += dox * dox + doy * doy; a.hold_truth_sq += dtx * dtx + dty * dty; ++a.hold_n; }
    }

    // 3. Overshoot bij abrupte stops: max. voorbij het rustpunt langs de bewegingsrichting, van 100 ms voor de stop
    //    tot stop_window_s erna.
    for (const StopEvent& s : ti.stops) {
        double mo = 0, mt = 0;
        for (size_t q = s.k; q > 0 && b.t[s.k] - b.t[q] <= 0.1; --q) {
            mo = std::max(mo, (c.ox[q] - s.restx) * s.dirx + (c.oy[q] - s.resty) * s.diry);
            mt = std::max(mt, (b.tx[q] - s.restx) * s.dirx + (b.ty[q] - s.resty) * s.diry);
        }
        for (size_t q = s.k; q < n && b.t[q] - b.t[s.k] <= p.stop_window_s; ++q) {
            mo = std::max(mo, (c.ox[q] - s.restx) * s.dirx + (c.oy[q] - s.resty) * s.diry);
            mt = std::max(mt, (b.tx[q] - s.restx) * s.dirx + (b.ty[q] - s.resty) * s.diry);
        }
        a.overshoot_out.push_back(mo);
        a.overshoot_excess.push_back(std::max(0.0, mo - mt));
        a.overshoot_truth.push_back(mt);
    }

    // 4. Sprong bij aankomst van een rapport: zonder extrapolatie (vorige uitvoer blijft staan) en met de uitvoerklok
    //    (anker + snelheid * min(Δ, limiet)). Δ is de ontvangsttijd tussen de twee rapporten, zoals live.
    output::OutputClockConfig cc = p.clock;
    cc.hz = cc.hz > 0 ? cc.hz : 250;
    output::AimClock clk(cc), clk2(cc);
    auto anchor = [&](output::AimClock& k, size_t i) {
        core::InputSample s;
        s.aim_valid = true;
        s.t_eval_ns = c.rx[i];
        s.age_ms = c.age.empty() ? 0.0 : c.age[i];
        s.aim.x = c.ox[i] / kScreenW;
        s.aim.y = c.oy[i] / kScreenH;
        s.aim_vx = c.vx[i] / kScreenW;
        s.aim_vy = c.vy[i] / kScreenH;
        k.on_sample(s);
    };
    for (size_t k = 1; k < n; ++k) {
        const double dt_rx = static_cast<double>(c.rx[k] - c.rx[k - 1]) / 1e9;
        if (dt_rx <= 0 || dt_rx > 0.25) continue;
        const bool moving = ti.band[k - 1] != 0 || ti.band[k] != 0;
        anchor(clk, k - 1);
        anchor(clk2, k);
        const output::AimPos e = *clk.at(c.rx[k]);       // waar de klok de cursor net voor aankomst had
        const output::AimPos nw = *clk2.at(c.rx[k]);     // waar het nieuwe rapport hem meteen zet
        const double plain = hypot2(c.ox[k] - c.ox[k - 1], c.oy[k] - c.oy[k - 1]);
        const double ext = hypot2(nw.x * kScreenW - e.x * kScreenW, nw.y * kScreenH - e.y * kScreenH);
        a.jump_all_plain.push_back(plain);
        a.jump_all_extrap.push_back(ext);
        if (c.gap[k]) {
            a.jump_gap_plain.push_back(plain);
            a.jump_gap_extrap.push_back(ext);
            if (moving) { a.jump_gap_move_plain.push_back(plain); a.jump_gap_move_extrap.push_back(ext); }
        }
        // Houdleeftijd (rx-tijd): per rapport en met klok (alleen voorbij de limiet).
        a.hold_sq += dt_rx * dt_rx / 2;
        a.hold_total += dt_rx;
        const double over = std::max(0.0, dt_rx - cc.max_extrap_ms * 1e-3);
        a.hold_sq_clk += over * over / 2;  // de leeftijd groeit pas na de limiet
    }
    return a;
}

std::vector<ModeAccum> evaluate_modes(const Track& c, const Track& base, const TruthInfo& ti, const LatencyParams& p) {
    std::vector<ModeAccum> out;
    const size_t n = c.t.size();
    if (n != base.t.size() || n < 64 || c.age.size() != n) return out;
    // Waarheid op minimumtransport-tijd m_k = t_rx - age (s, relatief aan het eerste rapport); alleen stijgende punten.
    Track hb;
    std::vector<size_t> idx;
    const double r0 = static_cast<double>(c.rx[0]) / 1e9;
    for (size_t k = 0; k < n; ++k) {
        const double m = static_cast<double>(c.rx[k]) / 1e9 - r0 - c.age[k] * 1e-3;
        if (!hb.t.empty() && m <= hb.t.back()) continue;
        hb.t.push_back(m);
        hb.tx.push_back(base.tx[k]);
        hb.ty.push_back(base.ty[k]);
        idx.push_back(k);
    }
    const size_t nm = hb.t.size();
    if (nm < 64) return out;
    const char* names[3] = {"per rapport (houden)", "klok, anker op ontvangsttijd", "klok, anker op t_rx - leeftijd"};
    output::OutputClockConfig cc = p.clock;
    cc.hz = cc.hz > 0 ? cc.hz : 250;
    constexpr size_t kS = kShiftMaxMs - kShiftMinMs + 1;
    for (int mode = 0; mode < 3; ++mode) {
        output::OutputClockConfig mc = cc;
        mc.age_comp = mode == 2;
        output::AimClock clk(mc);
        std::vector<double> sum(kS, 0.0);
        std::vector<uint64_t> cnt(kS, 0);
        size_t k = 0;      // laatste aangekomen rapport
        bool have = false;
        const int64_t t_start = c.rx[0] + 500'000'000;
        const int64_t t_end = c.rx[n - 1] - 500'000'000;
        // Waarheidsindex per u voor het bewegingsmasker.
        for (int64_t u = t_start; u < t_end; u += 4'000'000) {
            while (k + 1 < n && c.rx[k + 1] <= u) { ++k; have = false; }
            if (!have) {
                core::InputSample s;
                s.aim_valid = true;
                s.t_eval_ns = c.rx[k];
                s.age_ms = c.age[k];
                s.aim.x = c.ox[k] / kScreenW;
                s.aim.y = c.oy[k] / kScreenH;
                s.aim_vx = c.vx[k] / kScreenW;
                s.aim_vy = c.vy[k] / kScreenH;
                clk.on_sample(s);
                have = true;
            }
            double dx, dy;
            if (mode == 0) { dx = c.ox[k]; dy = c.oy[k]; }
            else { const output::AimPos ps = *clk.at(u); dx = ps.x * kScreenW; dy = ps.y * kScreenH; }
            const double uh = static_cast<double>(u) / 1e9 - r0;
            // Bewegingsmasker: dichtstbijzijnde rapport op minimumtransport-tijd.
            const size_t jn = static_cast<size_t>(std::lower_bound(hb.t.begin(), hb.t.end(), uh) - hb.t.begin());
            if (jn >= nm) continue;
            if (!ti.band[idx[jn]]) continue;
            size_t j = jn > 0 ? jn - 1 : 0;
            for (size_t li = 0; li < kS; ++li) {
                const double ts = uh + (kShiftMinMs + static_cast<int>(li)) * 1e-3;
                double tx, ty;
                if (!truth_at(hb, j, ts, tx, ty)) continue;
                const double ex = dx - tx, ey = dy - ty;
                sum[li] += ex * ex + ey * ey;
                ++cnt[li];
            }
        }
        ModeAccum r;
        r.name = names[mode];
        for (size_t li = 0; li < kS; ++li) { r.acc.cost_sum[0][li] = sum[li]; r.acc.cost_n[0][li] = cnt[li]; }
        out.push_back(std::move(r));
    }
    return out;
}

DelayResult delay_of(const LatencyAccum& a, int band, double assume_ms) {
    DelayResult r;
    double best = std::numeric_limits<double>::infinity();
    int bi = -1;
    std::vector<double> mse(kShifts, kNaN);
    for (size_t i = 0; i < kShifts; ++i) {
        if (a.cost_n[band][i] < 20) continue;
        mse[i] = a.cost_sum[band][i] / static_cast<double>(a.cost_n[band][i]);
        if (mse[i] < best) { best = mse[i]; bi = static_cast<int>(i); }
    }
    if (bi < 0) return r;
    double shift = kShiftMinMs + bi;
    if (bi > 0 && bi + 1 < static_cast<int>(kShifts) && !std::isnan(mse[bi - 1]) && !std::isnan(mse[bi + 1])) {
        const double y0 = mse[bi - 1], y1 = mse[bi], y2 = mse[bi + 1];
        const double den = y0 - 2 * y1 + y2;
        if (den > 0) shift += 0.5 * (y0 - y2) / den;
    }
    r.ok = true;
    r.delay_ms = -shift;
    r.rmse_at_best_px = std::sqrt(best);
    r.n = a.cost_n[band][bi];
    const int ai = static_cast<int>(std::lround(assume_ms)) - kShiftMinMs;
    if (ai >= 0 && ai < static_cast<int>(kShifts) && !std::isnan(mse[ai])) r.rmse_at_assume_px = std::sqrt(mse[ai]);
    else r.rmse_at_assume_px = kNaN;
    return r;
}

double jitter_px(const LatencyAccum& a) { return a.jit_n ? std::sqrt(a.jit_out_sq / static_cast<double>(a.jit_n)) : kNaN; }
double truth_jitter_px(const LatencyAccum& a) { return a.jit_n ? std::sqrt(a.jit_truth_sq / static_cast<double>(a.jit_n)) : kNaN; }

double hold_jitter_px(const LatencyAccum& a) { return a.hold_n ? std::sqrt(a.hold_out_sq / static_cast<double>(a.hold_n)) : kNaN; }
double hold_truth_jitter_px(const LatencyAccum& a) { return a.hold_n ? std::sqrt(a.hold_truth_sq / static_cast<double>(a.hold_n)) : kNaN; }

TransportStats transport_stats(const std::vector<Record>& recs) {
    TransportStats t;
    core::MoveClock clk;
    core::ResidualStats res;
    std::vector<double> rxi, sti;
    bool have_prev = false;
    int64_t prev_rx = 0, first_rx = 0, last_rx = 0;
    uint16_t prev_stamp = 0;
    double ticks_total = 0;
    uint64_t gaps = 0, ints = 0;
    double sum_sq = 0, sum = 0;
    const double nominal = 0.01127;
    t.nominal_ms = nominal * 1e3;
    for (const Record& rc : recs) {
        if (rc.kind != Kind::Report) continue;
        const core::RawReport& r = rc.report;
        ++t.reports;
        if (!have_prev) first_rx = r.t_rx_ns;
        last_rx = r.t_rx_ns;
        if (r.has_stamp) {
            ++t.with_stamp;
            core::ClockSample cs = clk.add(r.stamp, r.t_rx_ns);
            if (!cs.first) res.add(cs.residual_ms);
            if (have_prev && !cs.first) {
                ticks_total += cs.dt_s;
                sti.push_back(cs.dt_s * 1e3);
                if (cs.dt_s > nominal * 1.5) ++gaps;
            }
        }
        if (have_prev) {
            const double d = static_cast<double>(r.t_rx_ns - prev_rx) / 1e9;
            rxi.push_back(d * 1e3);
            t.max_gap_ms = std::max(t.max_gap_ms, d * 1e3);
            sum += d;
            sum_sq += d * d;
            ++ints;
        }
        have_prev = true;
        prev_rx = r.t_rx_ns;
        prev_stamp = r.stamp;
    }
    (void)prev_stamp;
    t.duration_s = static_cast<double>(last_rx - first_rx) / 1e9;
    t.mean_interval_ms = ints ? sum / static_cast<double>(ints) * 1e3 : 0;
    t.rx_interval_p50_ms = lat_percentile(rxi, 0.5);
    t.rx_interval_p95_ms = lat_percentile(rxi, 0.95);
    t.rx_interval_max_ms = rxi.empty() ? 0 : *std::max_element(rxi.begin(), rxi.end());
    t.stamp_interval_p50_ms = lat_percentile(sti, 0.5);
    t.residual_p50_ms = res.count() ? res.percentile(0.5) : 0;
    t.residual_p95_ms = res.count() ? res.percentile(0.95) : 0;
    t.residual_p99_ms = res.count() ? res.percentile(0.99) : 0;
    t.residual_max_ms = res.max();
    t.gap_fraction = ints ? static_cast<double>(gaps) / static_cast<double>(ints) : 0;
    const double nom_s = t.stamp_interval_p50_ms > 0 ? t.stamp_interval_p50_ms * 1e-3 : nominal;
    t.lost_est = ticks_total > 0 ? static_cast<uint64_t>(std::max(0.0, std::llround(ticks_total / nom_s) - static_cast<double>(ints))) : 0;
    t.hold_age_ms = sum > 0 ? sum_sq / (2 * sum) * 1e3 : 0;
    return t;
}

}  // namespace ravengun::rec
