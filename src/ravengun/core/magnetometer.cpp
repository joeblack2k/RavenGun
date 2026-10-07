#include "ravengun/core/magnetometer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::core {

// ---------------- 3x3-algebra ----------------

Vec3 mul(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = 0;
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    return r;
}

void eigen_sym3(const Mat3& in, double vals[3], Mat3& v) {
    double a[3][3];
    std::memcpy(a, in.m, sizeof a);
    v = Mat3{};
    for (int sweep = 0; sweep < 50; ++sweep) {
        double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
        if (off < 1e-15) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-300) continue;
                double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 3; ++k) {  // a = J^T a J
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    double vkp = v.m[k][p], vkq = v.m[k][q];
                    v.m[k][p] = c * vkp - s * vkq;
                    v.m[k][q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < 3; ++i) vals[i] = a[i][i];
}

namespace {

// Gauss-eliminatie met kolompivot; false bij een (bijna) singuliere matrix.
bool solve(std::vector<double> a, std::vector<double> b, int n, std::vector<double>& x) {
    for (int c = 0; c < n; ++c) {
        int p = c;
        for (int r = c + 1; r < n; ++r)
            if (std::fabs(a[static_cast<size_t>(r * n + c)]) > std::fabs(a[static_cast<size_t>(p * n + c)])) p = r;
        if (std::fabs(a[static_cast<size_t>(p * n + c)]) < 1e-14) return false;
        if (p != c) {
            for (int k = 0; k < n; ++k) std::swap(a[static_cast<size_t>(c * n + k)], a[static_cast<size_t>(p * n + k)]);
            std::swap(b[static_cast<size_t>(c)], b[static_cast<size_t>(p)]);
        }
        for (int r = c + 1; r < n; ++r) {
            double f = a[static_cast<size_t>(r * n + c)] / a[static_cast<size_t>(c * n + c)];
            for (int k = c; k < n; ++k) a[static_cast<size_t>(r * n + k)] -= f * a[static_cast<size_t>(c * n + k)];
            b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(c)];
        }
    }
    x.assign(static_cast<size_t>(n), 0);
    for (int r = n - 1; r >= 0; --r) {
        double s = b[static_cast<size_t>(r)];
        for (int k = r + 1; k < n; ++k) s -= a[static_cast<size_t>(r * n + k)] * x[static_cast<size_t>(k)];
        x[static_cast<size_t>(r)] = s / a[static_cast<size_t>(r * n + r)];
    }
    return true;
}

bool inverse3(const Mat3& a, Mat3& inv) {
    const auto& m = a.m;
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (!(std::fabs(det) > 1e-300)) return false;
    inv.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    inv.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    inv.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    inv.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    inv.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    inv.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    inv.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    inv.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    inv.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return true;
}

// Ellipsoïde x^T M x + 2 u^T x = 1 in genormaliseerde coördinaten -> middelpunt c en A (sqrt(M/k)).
bool ellipsoid_from_quadric(const Mat3& M, Vec3 u, Vec3& c, Mat3& A) {
    Mat3 Mi;
    if (!inverse3(M, Mi)) return false;
    Vec3 mc = mul(Mi, u);
    c = {-mc.x, -mc.y, -mc.z};
    double k = 1 + dot(c, mul(M, c));
    if (!(k > 0)) return false;
    double vals[3];
    Mat3 V;
    eigen_sym3(M, vals, V);
    for (double& l : vals) {
        l /= k;
        if (!(l > 0)) return false;
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            A.m[i][j] = 0;
            for (int e = 0; e < 3; ++e) A.m[i][j] += V.m[i][e] * std::sqrt(vals[e]) * V.m[j][e];
        }
    return true;
}

double det3(const Mat3& a) {
    const auto& m = a.m;
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

std::string pct(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f %%", v * 100);
    return b;
}
std::string deg(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f°", v);
    return b;
}

}  // namespace

const char* to_string(MagFitKind k) {
    switch (k) {
        case MagFitKind::None: return "none";
        case MagFitKind::Full: return "full";
        case MagFitKind::AxisAligned: return "axis_aligned";
    }
    return "none";
}

Vec3 mag_apply(const MagCalibration& c, Vec3 raw) {
    Vec3 m = mul(c.soft, raw - c.offset);
    if (c.y_flip) m.y = -m.y;
    return m;
}

Vec3 mag_apply(const MagCalibration& c, const int raw[3]) {
    return mag_apply(c, Vec3{static_cast<double>(raw[0]), static_cast<double>(raw[1]), static_cast<double>(raw[2])});
}

double dip_deg(Vec3 m, Vec3 a) {
    double nm = norm(m), na = norm(a);
    if (nm <= 0 || na <= 0) return std::nan("");
    double s = -dot(m, a) / (nm * na);
    return std::asin(std::clamp(s, -1.0, 1.0)) * kDegPerRad;
}

// ---------------- Dekking ----------------

const std::array<Vec3, SphereCoverage::kBins>& SphereCoverage::directions() {
    static const std::array<Vec3, kBins> d = [] {
        const double p = (1 + std::sqrt(5.0)) / 2, ip = 1 / p;
        std::array<Vec3, kBins> r{};
        int i = 0;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
                for (int sz : {-1, 1}) r[static_cast<size_t>(i++)] = {double(sx), double(sy), double(sz)};
        for (int s1 : {-1, 1})
            for (int s2 : {-1, 1}) {
                r[static_cast<size_t>(i++)] = {0, s1 * ip, s2 * p};
                r[static_cast<size_t>(i++)] = {s1 * ip, s2 * p, 0};
                r[static_cast<size_t>(i++)] = {s1 * p, 0, s2 * ip};
            }
        for (auto& v : r) v = v * (1 / norm(v));
        return r;
    }();
    return d;
}

void SphereCoverage::add(Vec3 dir) {
    double n = norm(dir);
    if (!(n > 0)) return;
    dir = dir * (1 / n);
    int best = 0;
    double bd = -2;
    const auto& d = directions();
    for (int i = 0; i < kBins; ++i) {
        double c = dot(dir, d[static_cast<size_t>(i)]);
        if (c > bd) { bd = c; best = i; }
    }
    ++n_[static_cast<size_t>(best)];
}

double SphereCoverage::fraction(int min_count) const {
    int k = 0;
    for (int c : n_) if (c >= min_count) ++k;
    return static_cast<double>(k) / kBins;
}

// ---------------- Fit ----------------

MagFitResult fit_magnetometer(const std::vector<MagSample>& s, const MagFitOptions& o) {
    MagFitResult res;
    const size_t n = s.size();
    if (n < 10) {
        res.problems.push_back("te weinig samples voor een fit (" + std::to_string(n) + ")");
        return res;
    }
    // Normaliseren voor de conditie: middelpunt en RMS-straal van de data.
    Vec3 mu{};
    for (const auto& x : s) mu = mu + x.raw;
    mu = mu * (1.0 / static_cast<double>(n));
    double sc = 0;
    for (const auto& x : s) sc += dot(x.raw - mu, x.raw - mu);
    sc = std::sqrt(sc / static_cast<double>(n));
    if (!(sc > 0)) {
        res.problems.push_back("alle samples gelijk: is de controller gedraaid?");
        return res;
    }

    auto try_fit = [&](bool full, Vec3& c, Mat3& A) {
        const int m = full ? 9 : 6;
        std::vector<double> N(static_cast<size_t>(m * m), 0), r(static_cast<size_t>(m), 0), v;
        double row[9];
        for (const auto& x : s) {
            Vec3 p = (x.raw - mu) * (1 / sc);
            if (full) {
                double t[9] = {p.x * p.x, p.y * p.y, p.z * p.z, 2 * p.y * p.z, 2 * p.x * p.z, 2 * p.x * p.y, 2 * p.x, 2 * p.y, 2 * p.z};
                std::memcpy(row, t, sizeof t);
            } else {
                double t[6] = {p.x * p.x, p.y * p.y, p.z * p.z, 2 * p.x, 2 * p.y, 2 * p.z};
                std::memcpy(row, t, sizeof t);
            }
            for (int i = 0; i < m; ++i) {
                r[static_cast<size_t>(i)] += row[i];
                for (int j = 0; j < m; ++j) N[static_cast<size_t>(i * m + j)] += row[i] * row[j];
            }
        }
        if (full) {
            double tr = 0;
            for (int i = 0; i < m; ++i) tr += N[static_cast<size_t>(i * m + i)];
            for (int i = 3; i < 6; ++i) N[static_cast<size_t>(i * m + i)] += o.ridge * tr / m;
        }
        if (!solve(N, r, m, v)) return false;
        Mat3 M;
        Vec3 u;
        if (full) {
            M.m[0][0] = v[0]; M.m[1][1] = v[1]; M.m[2][2] = v[2];
            M.m[1][2] = M.m[2][1] = v[3];
            M.m[0][2] = M.m[2][0] = v[4];
            M.m[0][1] = M.m[1][0] = v[5];
            u = {v[6], v[7], v[8]};
        } else {
            M = Mat3{};
            M.m[0][0] = v[0]; M.m[1][1] = v[1]; M.m[2][2] = v[2];
            u = {v[3], v[4], v[5]};
        }
        return ellipsoid_from_quadric(M, u, c, A);
    };

    // Eerst asgericht (6 parameters, robuust bij weinig dekking). De dekking rond dát middelpunt
    // bepaalt of de volledige fit (9 parameters, ook kruistermen) verantwoord is; anders blijft
    // het bij de asgerichte fit.
    Vec3 c{};
    Mat3 A;
    MagFitKind kind = MagFitKind::None;
    if (try_fit(false, c, A)) {
        kind = MagFitKind::AxisAligned;
        SphereCoverage pre;
        for (const auto& x : s) pre.add(mul(A, (x.raw - mu) * (1 / sc) - c));
        Vec3 cf{};
        Mat3 Af;
        if (pre.fraction(o.coverage_min_count) >= o.full_fit_min_coverage && n >= 30 && try_fit(true, cf, Af)) {
            kind = MagFitKind::Full;
            c = cf;
            A = Af;
        }
    } else if (n >= 30 && try_fit(true, c, A)) {
        kind = MagFitKind::Full;
    }
    if (kind == MagFitKind::None) {
        res.problems.push_back("de fit convergeert niet (geen ellipsoïde): verder en in alle richtingen draaien");
        return res;
    }
    MagCalibration& cal = res.cal;
    cal.kind = kind;
    cal.offset = mu + c * sc;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) cal.soft.m[i][j] = A.m[i][j] / sc;
    cal.y_flip = o.y_flip;
    double d = det3(cal.soft);
    cal.field_counts = d > 0 ? std::cbrt(1 / d) : 0;
    cal.samples = static_cast<int>(n);
    res.fitted = true;

    // Kwaliteit.
    SphereCoverage cov;
    double se = 0, maxe = 0;
    std::vector<double> dips, dips_nf;
    for (const auto& x : s) {
        Vec3 m = mag_apply(cal, x.raw);
        double e = norm(m) - 1;
        se += e * e;
        maxe = std::max(maxe, std::fabs(e));
        Vec3 mr = mul(cal.soft, x.raw - cal.offset);
        cov.add(mr);
        double an = norm(x.accel);
        if (std::fabs(an - 1) <= o.accel_static_tol) {
            dips.push_back(dip_deg(m, x.accel));
            Vec3 nf = mr;
            if (!cal.y_flip) nf.y = -nf.y;  // de andere hypothese
            dips_nf.push_back(dip_deg(nf, x.accel));
        }
    }
    cal.rms_norm_err = std::sqrt(se / static_cast<double>(n));
    res.max_norm_err = maxe;
    cal.coverage = cov.fraction(o.coverage_min_count);
    auto meanstd = [](const std::vector<double>& v, double& mean, double& sd) {
        mean = sd = 0;
        if (v.empty()) return;
        for (double x : v) mean += x;
        mean /= static_cast<double>(v.size());
        for (double x : v) sd += (x - mean) * (x - mean);
        sd = std::sqrt(sd / static_cast<double>(v.size()));
    };
    double nfm = 0;
    meanstd(dips, res.dip_mean_deg, cal.dip_std_deg);
    meanstd(dips_nf, nfm, res.dip_std_noflip_deg);
    cal.dip_ref_deg = res.dip_mean_deg;
    res.dip_samples = static_cast<int>(dips.size());

    if (static_cast<int>(n) < o.min_samples)
        res.problems.push_back("te weinig samples: " + std::to_string(n) + " (minstens " + std::to_string(o.min_samples) + "); langer en in meer richtingen draaien");
    if (cal.coverage < o.min_coverage)
        res.problems.push_back("te weinig dekking: " + pct(cal.coverage) + " van de bol (minstens " + pct(o.min_coverage) +
                               "); draai ook met de kolf omhoog en omlaag en rol om de loop");
    if (cal.rms_norm_err > o.max_rms)
        res.problems.push_back("veld niet homogeen of fit slecht: restfout " + pct(cal.rms_norm_err) + " (max " + pct(o.max_rms) +
                               "); verder van metalen tafels, speakers en de tv draaien");
    if (res.dip_samples < 30)
        res.problems.push_back("te weinig rustige samples voor de dipcontrole (" + std::to_string(res.dip_samples) + "); langzamer draaien");
    else if (cal.dip_std_deg > o.max_dip_std_deg)
        res.problems.push_back("dip spreidt " + deg(cal.dip_std_deg) + " (max " + deg(o.max_dip_std_deg) +
                               "): veld niet homogeen of te snel gedraaid; langzamer draaien, weg van metaal");
    res.ok = res.problems.empty();
    res.marginal = !res.ok && cal.coverage >= o.min_coverage && cal.samples >= o.min_samples &&
                   cal.rms_norm_err <= o.marginal_max_rms && cal.dip_std_deg <= o.marginal_max_dip_std_deg;
    cal.marginal = res.marginal;
    return res;
}

// ---------------- Verzamelen ----------------

void MagCollector::reset() { *this = MagCollector{}; }

bool MagCollector::add(const int raw[3], Vec3 accel, double t) {
    Vec3 r{static_cast<double>(raw[0]), static_cast<double>(raw[1]), static_cast<double>(raw[2])};
    ++seen_;
    if (!have_) t0_ = t;
    last_t_ = t;
    lo_ = {std::min(lo_.x, r.x), std::min(lo_.y, r.y), std::min(lo_.z, r.z)};
    hi_ = {std::max(hi_.x, r.x), std::max(hi_.y, r.y), std::max(hi_.z, r.z)};
    double range = std::max({hi_.x - lo_.x, hi_.y - lo_.y, hi_.z - lo_.z}) / 2;
    double min_step = std::max(4.0, 0.03 * range);
    if (have_ && norm(r - last_) < min_step) return false;
    if (s_.size() >= kMaxSamples) return false;
    have_ = true;
    last_ = r;
    s_.push_back({r, accel});
    return true;
}

MagCoverageDetail MagCollector::detail(int min_count) const {
    MagCoverageDetail d;
    if (s_.empty()) return d;
    Vec3 c = (lo_ + hi_) * 0.5;
    Vec3 h = (hi_ - lo_) * 0.5;
    SphereCoverage cov;
    double mean_norm = 0;
    auto unit = [&](const Vec3& raw) {
        Vec3 v = raw - c;
        return Vec3{h.x > 1 ? v.x / h.x : 0, h.y > 1 ? v.y / h.y : 0, h.z > 1 ? v.z / h.z : 0};
    };
    for (const auto& x : s_) {
        cov.add(unit(x.raw));
        mean_norm += norm(x.raw);
    }
    mean_norm /= static_cast<double>(s_.size());
    // Normaliseren op de eigen omvang maakt van een klein wolkje (nauwelijks gedraaid) al een
    // "volle bol". Een echte rondgang geeft per as een halve spreiding in de orde van de veldsterkte;
    // daarom telt de dekking pas volledig mee als elke as minstens ~60 % daarvan bestrijkt.
    double need = 0.6 * mean_norm;
    d.spread = need > 0 ? std::min({1.0, h.x / need, h.y / need, h.z / need}) : 0.0;
    d.counts = cov.counts();
    d.coverage = cov.fraction(min_count) * d.spread;
    Vec3 l = unit(s_.back().raw);
    double n = norm(l);
    if (n > 1e-9) { d.dir = l * (1 / n); d.has_dir = true; }
    return d;
}

double MagCollector::coverage(int min_count) const { return detail(min_count).coverage; }

// ---------------- Opslag ----------------

std::string controller_key(const std::string& id) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : id) {
        h ^= static_cast<unsigned char>(std::tolower(ch));
        h *= 1099511628211ULL;
    }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

std::string default_mag_calibration_dir() {
    const char* c = std::getenv("XDG_CONFIG_HOME");
    if (c && c[0] == '/') return std::string(c) + "/ravengun/magnetometer";
    const char* home = std::getenv("HOME");
    if (!home || home[0] != '/') return {};
    return std::string(home) + "/.config/ravengun/magnetometer";
}

std::string mag_calibration_to_json(const MagCalibration& c) {
    json::Writer w;
    w.begin_object();
    w.key("format").value("ravengun-mag-calibration");
    w.key("version").value(c.version);
    w.key("controller").value(c.controller);
    w.key("controller_key").value(c.controller_key);
    w.key("holder").value(c.holder);
    w.key("created_unix").value(static_cast<long long>(c.created_unix));
    w.key("kind").value(to_string(c.kind));
    w.key("offset").begin_array().value(c.offset.x).value(c.offset.y).value(c.offset.z).end_array();
    w.key("soft").begin_array();
    for (int i = 0; i < 3; ++i) {
        w.begin_array();
        for (int j = 0; j < 3; ++j) w.value(c.soft.m[i][j]);
        w.end_array();
    }
    w.end_array();
    w.key("y_flip").value(c.y_flip);
    w.key("field_counts").value(c.field_counts);
    w.key("dip_ref_deg").value(c.dip_ref_deg);
    w.key("marginal").value(c.marginal);
    w.key("quality").begin_object();
    w.key("rms_norm_err").value(c.rms_norm_err);
    w.key("dip_std_deg").value(c.dip_std_deg);
    w.key("coverage").value(c.coverage);
    w.key("samples").value(c.samples);
    w.end_object();
    w.key("temperature_raw").value(c.temperature_raw);
    w.end_object();
    return w.str();
}

bool mag_calibration_from_json(const std::string& text, MagCalibration& out, std::string& err) {
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) { err = "beschadigd: geen geldige JSON"; return false; }
    const json::Value& o = pr.value;
    auto str = [&](const char* k, std::string& dst) {
        const json::Value* v = o.find(k);
        if (v && v->is_string()) { dst = v->str; return true; }
        return false;
    };
    auto num = [&](const json::Value* v, double& dst) {
        if (!v || !v->is_number() || !std::isfinite(v->num)) return false;
        dst = v->num;
        return true;
    };
    std::string fmt;
    if (!str("format", fmt) || fmt != "ravengun-mag-calibration") { err = "beschadigd: verkeerd formaat"; return false; }
    double ver = 0;
    if (!num(o.find("version"), ver)) { err = "beschadigd: versie ontbreekt"; return false; }
    if (static_cast<int>(ver) != kMagCalibrationVersion) { err = "andere versie (" + std::to_string(static_cast<int>(ver)) + ")"; return false; }
    MagCalibration c;
    str("controller", c.controller);
    str("controller_key", c.controller_key);
    str("holder", c.holder);
    std::string kind;
    str("kind", kind);
    c.kind = kind == "full" ? MagFitKind::Full : kind == "axis_aligned" ? MagFitKind::AxisAligned : MagFitKind::None;
    if (c.kind == MagFitKind::None) { err = "beschadigd: onbekend fit-soort"; return false; }
    const json::Value* off = o.find("offset");
    if (!off || !off->is_array() || off->arr.size() != 3) { err = "beschadigd: offset"; return false; }
    double v3[3];
    for (int i = 0; i < 3; ++i) if (!num(&off->arr[static_cast<size_t>(i)], v3[i])) { err = "beschadigd: offset"; return false; }
    c.offset = {v3[0], v3[1], v3[2]};
    const json::Value* soft = o.find("soft");
    if (!soft || !soft->is_array() || soft->arr.size() != 3) { err = "beschadigd: soft"; return false; }
    for (int i = 0; i < 3; ++i) {
        const json::Value& row = soft->arr[static_cast<size_t>(i)];
        if (!row.is_array() || row.arr.size() != 3) { err = "beschadigd: soft"; return false; }
        for (int j = 0; j < 3; ++j) if (!num(&row.arr[static_cast<size_t>(j)], c.soft.m[i][j])) { err = "beschadigd: soft"; return false; }
    }
    if (!(det3(c.soft) > 0)) { err = "beschadigd: soft-iron-matrix niet positief"; return false; }
    if (const json::Value* y = o.find("y_flip"); y && y->is_bool()) c.y_flip = y->b;
    if (const json::Value* mg = o.find("marginal"); mg && mg->is_bool()) c.marginal = mg->b;
    num(o.find("field_counts"), c.field_counts);
    num(o.find("dip_ref_deg"), c.dip_ref_deg);
    double t = 0;
    if (num(o.find("created_unix"), t)) c.created_unix = static_cast<int64_t>(t);
    if (num(o.find("temperature_raw"), t)) c.temperature_raw = static_cast<int>(t);
    if (const json::Value* q = o.find("quality"); q && q->is_object()) {
        num(q->find("rms_norm_err"), c.rms_norm_err);
        num(q->find("dip_std_deg"), c.dip_std_deg);
        num(q->find("coverage"), c.coverage);
        if (num(q->find("samples"), t)) c.samples = static_cast<int>(t);
    }
    out = c;
    return true;
}

namespace {
bool mkdirs(const std::string& dir, std::string& err) {
    std::string cur;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            if (!cur.empty() && ::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
                err = "kan " + cur + " niet maken: " + std::strerror(errno);
                return false;
            }
        }
        if (i < dir.size()) cur += dir[i];
    }
    return true;
}
}  // namespace

bool atomic_write_file(const std::string& path, const std::string& content, std::string& err) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) { err = "pad zonder map"; return false; }
    std::string dir = path.substr(0, slash);
    if (!mkdirs(dir, err)) return false;
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { err = "kan " + tmp + " niet schrijven: " + std::strerror(errno); return false; }
    ::fchmod(fd, 0600);
    size_t off = 0;
    while (off < content.size()) {
        ssize_t w = ::write(fd, content.data() + off, content.size() - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { err = std::string("schrijven: ") + std::strerror(errno); ::close(fd); ::unlink(tmp.c_str()); return false; }
        off += static_cast<size_t>(w);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) { err = std::string("fsync: ") + std::strerror(errno); ::unlink(tmp.c_str()); return false; }
    if (::rename(tmp.c_str(), path.c_str()) != 0) { err = std::string("rename: ") + std::strerror(errno); ::unlink(tmp.c_str()); return false; }
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
    return true;
}

bool save_mag_calibration(const std::string& dir, const MagCalibration& c, std::string& err) {
    if (dir.empty()) { err = "geen configuratiemap (HOME niet gezet)"; return false; }
    if (c.controller_key.empty()) { err = "geen controller-sleutel"; return false; }
    return atomic_write_file(dir + "/" + c.controller_key + ".json", mag_calibration_to_json(c) + "\n", err);
}

bool load_mag_calibration(const std::string& dir, const std::string& key, MagCalibration& out, std::string& err) {
    std::string path = dir + "/" + key + ".json";
    FILE* f = std::fopen(path.c_str(), "re");
    if (!f) { err = "ontbreekt"; return false; }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 65536) text.append(buf, n);
    std::fclose(f);
    if (!mag_calibration_from_json(text, out, err)) return false;
    if (!out.controller_key.empty() && out.controller_key != key) { err = "beschadigd: hoort bij een andere controller"; return false; }
    return true;
}

// ---------------- Verstoringsdetectie ----------------

std::string reject_names(unsigned m) {
    std::string r;
    auto add = [&](unsigned bit, const char* name) {
        if (!(m & bit)) return;
        if (!r.empty()) r += ',';
        r += name;
    };
    add(kRejectNorm, "norm");
    add(kRejectDip, "dip");
    add(kRejectRate, "rate");
    add(kRejectRumble, "rumble");
    return r;
}

const char* to_string(MagState s) {
    switch (s) {
        case MagState::Unknown: return "unknown";
        case MagState::Ok: return "ok";
        case MagState::Disturbed: return "disturbed";
    }
    return "unknown";
}

void MagDisturbanceDetector::reset() {
    double d = dip_ref_;
    *this = MagDisturbanceDetector(cfg_);
    dip_ref_ = d;
}

uint64_t MagDisturbanceDetector::rejected(unsigned bit) const {
    for (int i = 0; i < 4; ++i) if (bit == (1u << i)) return rej_[i];
    return 0;
}

MagCheck MagDisturbanceDetector::update(Vec3 m, Vec3 accel, Vec3 gyro, double dt, bool rumble) {
    MagCheck c;
    if (!(dt >= 0) || !std::isfinite(dt)) dt = 0;
    t_ += dt;
    ++total_;
    c.norm = norm(m);
    c.dip_deg = std::nan("");
    c.rate_err_deg_s = std::nan("");
    if (std::fabs(c.norm - 1) > cfg_.norm_tol) c.reasons |= kRejectNorm;
    double an = norm(accel);
    if (an > 1e-6 && std::fabs(an - 1) <= cfg_.accel_static_tol) {
        c.dip_deg = dip_deg(m, accel);
        if (std::fabs(c.dip_deg - dip_ref_) > cfg_.dip_tol_deg) c.reasons |= kRejectDip;
    }
    // Magnetometer-yaw tegen de gyro: de horizontale component van m draait (in B) tegengesteld
    // aan de draaiing om de verticaal; Δψ_m + ∫ω·â dt ≈ 0 als het veld homogeen en stil is.
    bool psi_ok = false;
    if (an > 0.5) {
        Vec3 up = accel * (1 / an);
        yaw_g_ += dot(gyro, up) * dt;
        Vec3 y{0, 1, 0};
        Vec3 yh = y - up * dot(y, up), mh = m - up * dot(m, up);
        if (norm(yh) > 0.3 && norm(mh) > 0.1 * std::max(c.norm, 1e-9)) {
            double psi = std::atan2(dot(cross(yh, mh), up), dot(yh, mh));
            if (have_psi_) psi_unwrapped_ += wrap_pi(psi - psi_prev_);
            else psi_unwrapped_ = psi;
            psi_prev_ = psi;
            have_psi_ = psi_ok = true;
        }
    }
    if (!psi_ok) { have_psi_ = false; win_.clear(); }
    else {
        win_.push_back({t_, psi_unwrapped_, yaw_g_});
        while (win_.size() > 1 && t_ - win_[1].t >= cfg_.rate_window_s) win_.pop_front();
        double span = t_ - win_.front().t;
        if (span >= cfg_.rate_window_s * 0.8) {
            double err = ((psi_unwrapped_ - win_.front().psi) + (yaw_g_ - win_.front().yaw_g)) / span;
            c.rate_err_deg_s = err * kDegPerRad;
            if (std::fabs(c.rate_err_deg_s) > cfg_.rate_tol_deg_s) c.reasons |= kRejectRate;
        }
    }
    if (rumble) c.reasons |= kRejectRumble;
    c.accepted = c.reasons == 0;
    for (int i = 0; i < 4; ++i) if (c.reasons & (1u << i)) ++rej_[i];
    if (c.accepted) {
        ++acc_;
        good_run_ += dt;
        bad_run_ = 0;
        if (state_ == MagState::Unknown) state_ = MagState::Ok;
        if (state_ == MagState::Disturbed && good_run_ >= cfg_.recover_after_s) state_ = MagState::Ok;
    } else {
        bad_run_ += dt;
        good_run_ = 0;
        if (state_ != MagState::Disturbed && bad_run_ > cfg_.disturb_after_s) {
            state_ = MagState::Disturbed;
            ++episodes_;
        }
    }
    c.state = state_;
    return c;
}

}  // namespace ravengun::core
