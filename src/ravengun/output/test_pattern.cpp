#include "ravengun/output/test_pattern.hpp"

#include <cmath>

namespace ravengun::output {

const char* to_string(PatternKind k) {
    switch (k) {
        case PatternKind::Center: return "center";
        case PatternKind::Corners: return "corners";
        case PatternKind::Grid: return "grid";
        case PatternKind::Sweep: return "sweep";
        case PatternKind::Point: return "point";
    }
    return "?";
}

bool parse_pattern(const std::string& name, PatternKind& k) {
    for (PatternKind c : {PatternKind::Center, PatternKind::Corners, PatternKind::Grid, PatternKind::Sweep, PatternKind::Point}) {
        if (name == to_string(c)) { k = c; return true; }
    }
    return false;
}

std::vector<PatternStep> generate_pattern(const PatternParams& p, std::string& err) {
    std::vector<PatternStep> s;
    auto bad = [&](const char* m) { err = m; return std::vector<PatternStep>{}; };
    if (!(p.margin < 0 || (p.margin >= 0 && p.margin < 0.5))) return bad("margin moet in [0, 0,5) liggen");
    switch (p.kind) {
        case PatternKind::Center:
            s.push_back({0, 0.5, 0.5, 0});
            break;
        case PatternKind::Point:
            // Outside 0..1 is allowed down to -1 and up to 2: an aim off the screen (the output clamps the axes to the edge and the
            // IO bridge reports pN.offscreen), the only way to test "aim off-screen to reload" without a Move.
            if (!(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= -1 && p.x <= 2 && p.y >= -1 && p.y <= 2))
                return bad("point must lie within [-1,2] x [-1,2] (outside 0..1 = aimed off the screen)");
            s.push_back({0, p.x, p.y, 0});
            break;
        case PatternKind::Corners: {
            if (!(p.dwell_s > 0)) return bad("dwell moet > 0 zijn");
            double m = p.margin < 0 ? 0.0 : p.margin;
            // Volgorde: linksboven, rechtsboven, rechtsonder, linksonder (met de klok mee).
            const double pts[4][2] = {{m, m}, {1 - m, m}, {1 - m, 1 - m}, {m, 1 - m}};
            for (int i = 0; i < 4; ++i) s.push_back({i * p.dwell_s, pts[i][0], pts[i][1], i});
            break;
        }
        case PatternKind::Grid: {
            if (!(p.dwell_s > 0)) return bad("dwell moet > 0 zijn");
            double m = p.margin < 0 ? 0.1 : p.margin;
            const double v[3] = {m, 0.5, 1 - m};
            int i = 0;
            for (int r = 0; r < 3; ++r)       // rijsgewijs: boven naar onder, links naar rechts
                for (int c = 0; c < 3; ++c, ++i) s.push_back({i * p.dwell_s, v[c], v[r], i});
            break;
        }
        case PatternKind::Sweep: {
            if (!(p.duration_s >= 0.5) || !(p.rate_hz >= 1 && p.rate_hz <= 1000)) return bad("sweep: duur >= 0,5 s en rate 1..1000 Hz");
            double m = p.margin < 0 ? 0.05 : p.margin;
            int n = static_cast<int>(std::lround(p.duration_s * p.rate_hz));
            if (n < 3) n = 3;
            const double pi = 3.14159265358979323846;
            // Elk been begint en eindigt in het midden, zodat het pad nergens springt.
            auto line = [&](double g, double lo, double hi) {  // midden -> lo -> hi -> midden
                if (g < 0.25) return 0.5 + (lo - 0.5) * (g / 0.25);
                if (g < 0.75) return lo + (hi - lo) * ((g - 0.25) / 0.5);
                return hi + (0.5 - hi) * ((g - 0.75) / 0.25);
            };
            for (int i = 0; i <= n; ++i) {
                double f = static_cast<double>(i) / n;  // 0..1 over het hele patroon
                double t = f * p.duration_s;
                double x, y;
                if (f < 1.0 / 3) {                       // been 1: horizontaal over de middenlijn
                    x = line(f * 3, m, 1 - m);
                    y = 0.5;
                } else if (f < 2.0 / 3) {                // been 2: verticaal over de middenlijn
                    x = 0.5;
                    y = line((f - 1.0 / 3) * 3, m, 1 - m);
                } else {                                 // been 3: cirkel met straal 0,5 - m (aspect niet gecorrigeerd)
                    double g = (f - 2.0 / 3) * 3, r = 0.5 - m;
                    if (g < 0.1) { x = 0.5 + r * (g / 0.1); y = 0.5; }
                    else if (g < 0.9) { double a = 2 * pi * (g - 0.1) / 0.8; x = 0.5 + r * std::cos(a); y = 0.5 + r * std::sin(a); }
                    else { x = 0.5 + r * ((1 - g) / 0.1); y = 0.5; }
                }
                s.push_back({t, x, y, i});
            }
            break;
        }
    }
    return s;
}

double pattern_length_s(const PatternParams& p, const std::vector<PatternStep>& steps) {
    if (steps.empty()) return 0;
    if (p.kind == PatternKind::Corners || p.kind == PatternKind::Grid) return steps.back().t_s + p.dwell_s;
    return steps.back().t_s;
}

}  // namespace ravengun::output
