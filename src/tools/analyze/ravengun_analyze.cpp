// ravengun-analyze: offline analyse van RavenGun-opnamen (fase 0/1/3). Leest alleen opnamebestanden;
// opent nooit hardware.
//
//   ravengun-analyze replay FILE [--dt move|rx] [--json]       deterministisch afspelen, digest
//   ravengun-analyze timebase FILE                              stempel, wrap, volgnummers, klokkoppeling,
//                                                               subsample-afstand, bias per subsample
//   ravengun-analyze mag FILE [--cal CAL.json] [--settle S]     ruis, LED-segmenten, (met --cal) detector
//   ravengun-analyze magfit FILE [--out CAL.json]               ellipsoïdefit op de magnetometerdata
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/move_clock.hpp"
#include "ravengun/rec/replay.hpp"

using namespace ravengun;
using namespace ravengun::core;

namespace {

void usage(FILE* f) {
    std::fputs(
        "gebruik: ravengun-analyze <commando> OPNAME [opties]\n"
        "  replay FILE [--dt move|rx] [--json]   pijplijn deterministisch afspelen; toont digest en drift\n"
        "  timebase FILE                         eenheid/wrap van de Move-stempel, volgnummers, klokkoppeling,\n"
        "                                        spreiding van de sampletijd, bias per subsample en temperatuur\n"
        "  mag FILE [--cal CAL.json] [--settle S] magnetometerruis, LED-segmenten; met --cal ook de\n"
        "                                        verstoringsdetector over de opname\n"
        "  magfit FILE [--out CAL.json]          ellipsoïdefit (hard/soft iron) met kwaliteitsmaten\n",
        f);
}

struct Stat {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    size_t n() const { return v.size(); }
    double mean() const { double s = 0; for (double x : v) s += x; return v.empty() ? NAN : s / static_cast<double>(v.size()); }
    double sd() const {
        if (v.size() < 2) return NAN;
        double m = mean(), s = 0;
        for (double x : v) s += (x - m) * (x - m);
        return std::sqrt(s / static_cast<double>(v.size() - 1));
    }
    double pct(double p) const {
        if (v.empty()) return NAN;
        auto c = v;
        size_t k = static_cast<size_t>(p * static_cast<double>(c.size() - 1) + 0.5);
        std::nth_element(c.begin(), c.begin() + static_cast<long>(k), c.end());
        return c[k];
    }
    double min() const { return v.empty() ? NAN : *std::min_element(v.begin(), v.end()); }
    double max() const { return v.empty() ? NAN : *std::max_element(v.begin(), v.end()); }
    // Ruis uit opeenvolgende verschillen (ongevoelig voor langzame drift): sd(diff)/sqrt(2).
    double diff_noise() const {
        if (v.size() < 3) return NAN;
        Stat d;
        for (size_t i = 1; i < v.size(); ++i) d.add(v[i] - v[i - 1]);
        return d.sd() / std::sqrt(2.0);
    }
};

std::vector<const RawReport*> reports_of(const std::vector<rec::Record>& recs) {
    std::vector<const RawReport*> r;
    for (const auto& x : recs) if (x.kind == rec::Kind::Report) r.push_back(&x.report);
    return r;
}

bool load(const char* path, std::vector<rec::Record>& recs) {
    std::string err;
    if (!rec::load_file(path, recs, err)) { std::fprintf(stderr, "ravengun-analyze: %s\n", err.c_str()); return false; }
    return true;
}

// ---------------- replay ----------------

int cmd_replay(const char* path, bool dt_move, bool as_json) {
    rec::ReplayOptions o;
    o.pipeline.use_move_clock = dt_move;
    o.pipeline.auto_center_on_start = true;
    rec::ReplayResult r = rec::replay_file(path, o);
    if (!r.ok) { std::fprintf(stderr, "ravengun-analyze: %s\n", r.error.c_str()); return 2; }
    double dur = static_cast<double>(r.last.t_eval_ns - r.header.t0_ns) / 1e9;
    AimAngles a0 = world_angles(r.first_valid.q_wb), a1 = world_angles(r.last.q_wb);
    double dyaw = wrap_pi(a1.yaw_rad - a0.yaw_rad) * kDegPerRad, dpitch = (a1.pitch_rad - a0.pitch_rad) * kDegPerRad;
    if (as_json) {
        std::printf("{\"digest\":\"%016llx\",\"reports\":%llu,\"samples\":%llu,\"optical\":%llu,\"duration_s\":%.3f,\"yaw_change_deg\":%.4f,\"pitch_change_deg\":%.4f,\"dt\":\"%s\"}\n",
                    static_cast<unsigned long long>(r.digest), static_cast<unsigned long long>(r.reports),
                    static_cast<unsigned long long>(r.samples), static_cast<unsigned long long>(r.optical), dur, dyaw, dpitch,
                    dt_move ? "move" : "rx");
        return 0;
    }
    std::printf("Opname:        %s (tag %s, %s, controller %s)\n", path, r.header.tag.c_str(), r.header.model.c_str(), r.header.controller.c_str());
    std::printf("Inhoud:        %llu rapporten, %llu optische metingen, %llu LED-wissels, %llu labels, einde: %s\n",
                static_cast<unsigned long long>(r.reports), static_cast<unsigned long long>(r.optical),
                static_cast<unsigned long long>(r.leds), static_cast<unsigned long long>(r.marks),
                r.ended ? r.end_reason.c_str() : "(geen eindregel)");
    std::printf("Afgespeeld:    %llu samples, dt uit %s, digest %016llx\n", static_cast<unsigned long long>(r.samples),
                dt_move ? "Move-stempel (terugval ontvangsttijd)" : "ontvangsttijd", static_cast<unsigned long long>(r.digest));
    std::printf("Duur:          %.1f s\n", dur);
    std::printf("Oriëntatie:    yaw-verandering %+.3f graden (%.3f graden/min), pitch %+.3f graden, bias %s\n", dyaw,
                dur > 0 ? dyaw / dur * 60 : 0.0, dpitch, to_string(r.last.bias_state));
    std::printf("Bias (eind):   %.3f %.3f %.3f graden/s\n", r.last.gyro_bias.x * kDegPerRad, r.last.gyro_bias.y * kDegPerRad,
                r.last.gyro_bias.z * kDegPerRad);
    return 0;
}

// ---------------- timebase ----------------

int cmd_timebase(const char* path) {
    std::vector<rec::Record> recs;
    if (!load(path, recs)) return 2;
    auto rs = reports_of(recs);
    std::vector<const RawReport*> st;
    for (auto* r : rs) if (r->has_stamp) st.push_back(r);
    std::printf("Opname:        %s: %zu rapporten, %zu met stempel\n", path, rs.size(), st.size());
    if (st.size() < 50) { std::printf("Te weinig rapporten met stempel.\n"); return 1; }

    // 1. Eenheid: som van stempeldelta's over korte intervallen (geen wrap-ambiguïteit) / hosttijd.
    double sum_ticks = 0, sum_host = 0;
    for (size_t i = 1; i < st.size(); ++i) {
        double h = static_cast<double>(st[i]->t_rx_ns - st[i - 1]->t_rx_ns) / 1e9;
        if (h <= 0 || h > 0.2) continue;
        sum_ticks += stamp_delta(st[i - 1]->stamp, st[i]->stamp);
        sum_host += h;
    }
    double tps = sum_ticks / sum_host;
    std::printf("Eenheid:       %.1f ticks/s gemeten (aangenomen %.0f; 1 tick = %.3f us); 16 bit -> wrap na %.4f s\n", tps,
                kMoveTicksPerSecond, 1e6 / tps, 65536.0 / tps);

    // 2. Volgnummers t.o.v. stempels.
    std::map<int, Stat> by_dseq;
    for (size_t i = 1; i < st.size(); ++i) {
        int dseq = (st[i]->hid_seq - st[i - 1]->hid_seq + 16) % 16;
        double h = static_cast<double>(st[i]->t_rx_ns - st[i - 1]->t_rx_ns) / 1e9;
        if (h > 0.5) continue;
        by_dseq[dseq].add(stamp_delta(st[i - 1]->stamp, st[i]->stamp));
    }
    double period = by_dseq.count(1) ? by_dseq[1].pct(0.5) : NAN;
    std::printf("Rapportperiode: mediaan %.0f ticks = %.3f ms (%.2f rapporten/s); subsample-afstand %.3f ms (aanname: 2 gelijk verdeelde subsamples per rapport)\n",
                period, period / tps * 1e3, tps / period, period / tps * 1e3 / 2);
    std::printf("Volgnummer-sprong -> stempelsprong:\n");
    uint64_t lost_seq = 0, lost_stamp = 0;
    for (auto& [d, s] : by_dseq) {
        std::printf("  dseq %2d: %6zu keer, stempeldelta mediaan %.0f (= %.2f perioden), min %.0f, max %.0f\n", d, s.n(), s.pct(0.5),
                    s.pct(0.5) / period, s.min(), s.max());
        for (double x : s.v) {
            lost_seq += static_cast<uint64_t>(std::max(0, d - 1));
            lost_stamp += static_cast<uint64_t>(std::max(0L, std::lround(x / period) - 1));
        }
    }
    std::printf("  overgeslagen volgens volgnummer %llu, volgens stempel %llu\n", static_cast<unsigned long long>(lost_seq),
                static_cast<unsigned long long>(lost_stamp));

    // 3. Spreiding van de sampletijd: ontvangsttijd tegenover stempel, en de stempel zelf.
    MoveClock clk;
    Stat resid, rx_dev, stamp_jit, rx_int;
    std::vector<std::pair<double, double>> pts;  // (stempel-s, t_rx-s)
    int64_t t0 = st[0]->t_rx_ns;
    for (size_t i = 0; i < st.size(); ++i) {
        ClockSample cs = clk.add(st[i]->stamp, st[i]->t_rx_ns);
        if (i > 0) {
            resid.add(cs.residual_ms);
            rx_int.add(static_cast<double>(st[i]->t_rx_ns - st[i - 1]->t_rx_ns) / 1e6);
            int dseq = (st[i]->hid_seq - st[i - 1]->hid_seq + 16) % 16;
            if (dseq >= 1 && cs.dticks < 3 * period * dseq)
                stamp_jit.add((static_cast<double>(cs.dticks) - period * dseq) / tps * 1e3);
        }
        pts.push_back({static_cast<double>(cs.ticks) / tps, static_cast<double>(st[i]->t_rx_ns - t0) / 1e9});
    }
    // Lineaire fit t_rx = a + b * stempel (kleinste kwadraten) -> skew en afwijking van t_rx.
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = static_cast<double>(pts.size());
    for (auto& [x, y] : pts) { sx += x; sy += y; sxx += x * x; sxy += x * y; }
    double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n;
    for (auto& [x, y] : pts) rx_dev.add((y - (a + b * x)) * 1e3);
    std::printf("Klok:          skew Move t.o.v. host %+.1f ppm (kleinste kwadraten), ondergrens-koppeling %+.1f ppm\n", (b - 1) * 1e6,
                clk.skew_ppm());
    std::printf("Vóór (t_rx):   interval p5 %.2f / p50 %.2f / p95 %.2f / max %.2f ms; afwijking van de rechte sd %.2f ms, p95 |.| %.2f ms\n",
                rx_int.pct(0.05), rx_int.pct(0.5), rx_int.pct(0.95), rx_int.max(), rx_dev.sd(), [&] {
                    Stat q; for (double x : rx_dev.v) q.add(std::fabs(x)); return q.pct(0.95); }());
    std::printf("Na (stempel):  restvertraging t_rx - gekoppeld p50 %.2f / p95 %.2f / p99 %.2f / max %.2f ms;\n"
                "               jitter van de stempel t.o.v. een vast raster sd %.3f ms, p5 %.3f / p95 %.3f ms\n",
                resid.pct(0.5), resid.pct(0.95), resid.pct(0.99), resid.max(), stamp_jit.sd(), stamp_jit.pct(0.05), stamp_jit.pct(0.95));

    // 4. Bias per subsample (alleen stille rapporten) en temperatuur.
    Stat g[2][3], gr[2][3];
    std::map<int, Stat> temp_bias_z;
    Stat temps;
    for (auto* r : st) {
        if (r->temperature_raw >= 0) temps.add(r->temperature_raw);
        bool still = r->subsamples == 2;
        for (int k = 0; k < r->subsamples && still; ++k)
            if (norm(r->gyro[k]) > 0.10 || std::fabs(norm(r->accel[k]) - 1) > 0.05) still = false;
        if (!still) continue;
        for (int k = 0; k < 2; ++k) {
            g[k][0].add(r->gyro[k].x * kDegPerRad); g[k][1].add(r->gyro[k].y * kDegPerRad); g[k][2].add(r->gyro[k].z * kDegPerRad);
            if (r->has_raw) for (int ax = 0; ax < 3; ++ax) gr[k][ax].add(r->gyro_raw[k][ax]);
        }
        if (r->temperature_raw >= 0) temp_bias_z[r->temperature_raw].add((r->gyro[0].z + r->gyro[1].z) / 2 * kDegPerRad);
    }
    std::printf("Bias per subsample (stille rapporten: %zu):\n", g[0][0].n());
    const char* ax = "XYZ";
    for (int i = 0; i < 3; ++i)
        std::printf("  %c: frame0 %+.3f (sd %.3f) frame1 %+.3f (sd %.3f) graden/s, verschil %+.3f; ruw %+.1f / %+.1f counts\n", ax[i],
                    g[0][i].mean(), g[0][i].sd(), g[1][i].mean(), g[1][i].sd(), g[1][i].mean() - g[0][i].mean(), gr[0][i].mean(),
                    gr[1][i].mean());
    if (temps.n())
        std::printf("Temperatuur:   ruw %.0f..%.0f (gemiddeld %.1f)\n", temps.min(), temps.max(), temps.mean());
    if (temp_bias_z.size() > 1) {
        std::printf("Z-bias per temperatuur (ruw):\n");
        for (auto& [t, s] : temp_bias_z)
            if (s.n() >= 50) std::printf("  %d: %+.3f graden/s (%zu rapporten)\n", t, s.mean(), s.n());
    }
    return 0;
}

// ---------------- mag ----------------

int cmd_mag(const char* path, const char* cal_path, double settle_s) {
    std::vector<rec::Record> recs;
    if (!load(path, recs)) return 2;
    struct Seg { int rgb[3]; int64_t t0; Stat m[3]; Stat temp; };
    std::vector<Seg> segs;
    Seg cur{};
    cur.rgb[0] = recs[0].header.led_enabled ? recs[0].header.led[0] : 0;
    cur.rgb[1] = recs[0].header.led_enabled ? recs[0].header.led[1] : 0;
    cur.rgb[2] = recs[0].header.led_enabled ? recs[0].header.led[2] : 0;
    cur.t0 = recs[0].header.t0_ns;
    Stat all[3];
    for (const auto& x : recs) {
        if (x.kind == rec::Kind::Led) {
            if (cur.m[0].n()) segs.push_back(cur);
            cur = Seg{};
            for (int i = 0; i < 3; ++i) cur.rgb[i] = x.led.rgb[i];
            cur.t0 = x.led.t_ns;
        } else if (x.kind == rec::Kind::Report) {
            for (int i = 0; i < 3; ++i) all[i].add(x.report.mag_raw[i]);
            if (static_cast<double>(x.report.t_rx_ns - cur.t0) / 1e9 < settle_s) continue;
            for (int i = 0; i < 3; ++i) cur.m[i].add(x.report.mag_raw[i]);
            if (x.report.temperature_raw >= 0) cur.temp.add(x.report.temperature_raw);
        }
    }
    if (cur.m[0].n()) segs.push_back(cur);
    std::printf("Opname:        %s, %zu magnetometersamples (één per rapport)\n", path, all[0].n());
    const char* ax = "XYZ";
    for (int i = 0; i < 3; ++i)
        std::printf("  %c: gemiddeld %+.2f, sd %.3f, ruis uit verschillen %.3f counts, bereik %.0f..%.0f\n", ax[i], all[i].mean(),
                    all[i].sd(), all[i].diff_noise(), all[i].min(), all[i].max());
    double mag = std::sqrt(all[0].mean() * all[0].mean() + all[1].mean() * all[1].mean() + all[2].mean() * all[2].mean());
    std::printf("  |gemiddelde ruwe vector| %.1f counts (zonder kalibratie: inclusief hard-iron-offset)\n", mag);
    MagCalibration cal;
    bool have_cal = false;
    if (cal_path) {
        FILE* f = std::fopen(cal_path, "re");
        std::string text, err;
        if (f) { char b[4096]; size_t k; while ((k = std::fread(b, 1, sizeof b, f)) > 0) text.append(b, k); std::fclose(f); }
        have_cal = mag_calibration_from_json(text, cal, err);
        if (!have_cal) std::fprintf(stderr, "ravengun-analyze: kalibratie %s: %s\n", cal_path, err.c_str());
    }
    std::printf("LED-segmenten (eerste %.1f s na een wissel overgeslagen):\n", settle_s);
    const Seg* ref = nullptr;
    for (const auto& s : segs) if (s.rgb[0] == 0 && s.rgb[1] == 0 && s.rgb[2] == 0) { ref = &s; break; }
    if (!ref && !segs.empty()) ref = &segs[0];
    for (const auto& s : segs) {
        double d[3], se[3];
        for (int i = 0; i < 3; ++i) {
            d[i] = s.m[i].mean() - ref->m[i].mean();
            se[i] = std::sqrt(std::pow(s.m[i].diff_noise(), 2) / static_cast<double>(s.m[i].n()) +
                              std::pow(ref->m[i].diff_noise(), 2) / static_cast<double>(ref->m[i].n()));
        }
        double dn = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        std::printf("  LED %3d,%3d,%3d: %5zu samples, gem. %+8.2f %+8.2f %+8.2f, sd %.2f %.2f %.2f; t.o.v. ref %+6.2f %+6.2f %+6.2f (|d| %.2f counts",
                    s.rgb[0], s.rgb[1], s.rgb[2], s.m[0].n(), s.m[0].mean(), s.m[1].mean(), s.m[2].mean(), s.m[0].sd(), s.m[1].sd(),
                    s.m[2].sd(), d[0], d[1], d[2], dn);
        if (have_cal && cal.field_counts > 0) std::printf(" = %.2f %% van het veld", dn / cal.field_counts * 100);
        std::printf(", standaardfout ~%.2f), temp %.0f\n", std::sqrt(se[0] * se[0] + se[1] * se[1] + se[2] * se[2]), s.temp.mean());
    }
    if (have_cal) {
        MagDisturbanceDetector det;
        det.set_reference_dip(cal.dip_ref_deg);
        int64_t last = 0;
        Stat norms, dips;
        for (const auto& x : recs) {
            if (x.kind != rec::Kind::Report) continue;
            const RawReport& r = x.report;
            Vec3 a = (r.accel[0] + r.accel[1]) * 0.5, g = (r.gyro[0] + r.gyro[1]) * 0.5;
            double dt = last ? static_cast<double>(r.t_rx_ns - last) / 1e9 : 0;
            last = r.t_rx_ns;
            MagCheck c = det.update(mag_apply(cal, r.mag_raw), a, g, dt);
            norms.add(c.norm);
            if (!std::isnan(c.dip_deg)) dips.add(c.dip_deg);
        }
        std::printf("Detector (kalibratie %s): |m| gem. %.4f sd %.4f; dip gem. %.2f sd %.3f (ref %.2f); geaccepteerd %llu/%llu, afgewezen norm %llu dip %llu rate %llu; verstoord %llu keer, eindtoestand %s\n",
                    cal_path, norms.mean(), norms.sd(), dips.mean(), dips.sd(), cal.dip_ref_deg,
                    static_cast<unsigned long long>(det.accepted()), static_cast<unsigned long long>(det.total()),
                    static_cast<unsigned long long>(det.rejected(kRejectNorm)), static_cast<unsigned long long>(det.rejected(kRejectDip)),
                    static_cast<unsigned long long>(det.rejected(kRejectRate)), static_cast<unsigned long long>(det.disturbed_episodes()),
                    to_string(det.state()));
    }
    return 0;
}

int cmd_magfit(const char* path, const char* out) {
    std::vector<rec::Record> recs;
    if (!load(path, recs)) return 2;
    MagCollector coll;
    for (const auto& x : recs)
        if (x.kind == rec::Kind::Report) {
            Vec3 a = (x.report.accel[0] + x.report.accel[1]) * 0.5;
            coll.add(x.report.mag_raw, a, static_cast<double>(x.report.t_rx_ns) / 1e9);
        }
    MagFitResult r = fit_magnetometer(coll.samples());
    const MagCalibration& c = r.cal;
    std::printf("Samples:       %d gezien, %zu na uitdunnen\n", coll.seen(), coll.samples().size());
    std::printf("Fit:           %s, %s\n", to_string(c.kind), r.ok ? "GESLAAGD" : "NIET GESLAAGD");
    std::printf("Hard iron:     %.2f %.2f %.2f counts\n", c.offset.x, c.offset.y, c.offset.z);
    std::printf("Soft iron A:   [%.5f %.5f %.5f; %.5f %.5f %.5f; %.5f %.5f %.5f]\n", c.soft.m[0][0], c.soft.m[0][1], c.soft.m[0][2],
                c.soft.m[1][0], c.soft.m[1][1], c.soft.m[1][2], c.soft.m[2][0], c.soft.m[2][1], c.soft.m[2][2]);
    std::printf("Kwaliteit:     veld %.1f counts, restfout RMS %.2f %% (max %.2f %%), dekking %.0f %%, dip %.2f ± %.2f graden (zonder Y-flip ± %.2f), %d rustig\n",
                c.field_counts, c.rms_norm_err * 100, r.max_norm_err * 100, c.coverage * 100, r.dip_mean_deg, c.dip_std_deg,
                r.dip_std_noflip_deg, r.dip_samples);
    for (const auto& p : r.problems) std::printf("  - %s\n", p.c_str());
    if (out && r.fitted) {
        MagCalibration oc = c;
        oc.controller = recs[0].header.controller;
        oc.controller_key = "opname";
        oc.holder = "opname";
        std::string err;
        if (!atomic_write_file(out, mag_calibration_to_json(oc) + "\n", err)) { std::fprintf(stderr, "ravengun-analyze: %s\n", err.c_str()); return 2; }
        std::printf("Geschreven:    %s (alleen voor offline analyse; de daemon laadt dit niet)\n", out);
    }
    return r.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(stderr); return 64; }
    std::string cmd = argv[1];
    const char* file = argv[2];
    const char *cal = nullptr, *out = nullptr;
    bool dt_move = true, js = false;
    double settle = 1.0;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json") js = true;
        else if (a == "--dt" && i + 1 < argc) dt_move = std::string(argv[++i]) != "rx";
        else if (a == "--cal" && i + 1 < argc) cal = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--settle" && i + 1 < argc) settle = std::atof(argv[++i]);
        else { std::fprintf(stderr, "ravengun-analyze: onbekende optie '%s'\n", a.c_str()); usage(stderr); return 64; }
    }
    if (cmd == "replay") return cmd_replay(file, dt_move, js);
    if (cmd == "timebase") return cmd_timebase(file);
    if (cmd == "mag") return cmd_mag(file, cal, settle);
    if (cmd == "magfit") return cmd_magfit(file, out);
    usage(stderr);
    return 64;
}
