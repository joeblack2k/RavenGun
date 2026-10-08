#include "ravengun/core/fusion.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ravengun::core {

const char* to_string(BiasState s) {
    switch (s) {
        case BiasState::None: return "none";
        case BiasState::Restored: return "restored";
        case BiasState::Estimating: return "estimating";
        case BiasState::Estimated: return "estimated";
    }
    return "none";
}

const char* to_string(BiasSource s) {
    switch (s) {
        case BiasSource::None: return "none";
        case BiasSource::Restored: return "restored";
        case BiasSource::Still: return "still";
        case BiasSource::Quasi: return "quasi-still";
    }
    return "none";
}

const char* to_string(BiasMode m) { return m == BiasMode::Legacy ? "legacy" : "new"; }

bool parse_bias_mode(const char* s, BiasMode& out) {
    if (!s) return false;
    if (std::string(s) == "legacy") { out = BiasMode::Legacy; return true; }
    if (std::string(s) == "new") { out = BiasMode::New; return true; }
    return false;
}

namespace {
// Gewogen mediaan van (waarde, gewicht)-paren; leeg geeft 0.
double weighted_median(std::vector<std::pair<double, double>>& v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    double total = 0;
    for (const auto& p : v) total += p.second;
    double acc = 0;
    for (const auto& p : v) {
        acc += p.second;
        if (acc >= total / 2) return p.first;
    }
    return v.back().first;
}
}  // namespace

void GyroBiasEstimator::reset() {
    win_.clear();
    win_span_ = 0;
    bias_ = {};
    still_ = false;
    still_total_ = 0;
    age_ = 0;
    source_ = BiasSource::None;
    restored_ = false;
    restored_tc_ = -1;
    reacq_t_ = 0;
    reacq_mean_ = {};
    quasi_acquired_ = false;
    quasi_total_ = 0;
    quasi_accepted_ = quasi_rejected_ = 0;
    qwin_.clear();
    q_n_ = 0;
    q_t_ = 0;
    q_sum_ = q_sq_ = q_asum_ = {};
    q_adev_ = 0;
    q_last_update_t_ = 0;
    tev_.clear();
    slope_ = {};
    shift_since_anchor_ = {};
    prev_tc_ = -1;
    ev_acc_t_ = 0;
}

void GyroBiasEstimator::restore(Vec3 bias, int tc) {
    if (still_total_ > 0 || quasi_acquired_) return;  // er is al gemeten; een oude waarde is dan slechter
    bias_ = bias;
    restored_ = true;
    restored_tc_ = tc;
    source_ = BiasSource::Restored;
}

BiasState GyroBiasEstimator::state() const {
    if (settled()) return BiasState::Estimated;
    if (still_total_ > 0 || quasi_total_ > 0) return BiasState::Estimating;
    return restored_ ? BiasState::Restored : BiasState::None;
}

bool GyroBiasEstimator::update(Vec3 gyro, Vec3 accel, double dt) {
    if (!(dt > 0) || !std::isfinite(dt)) return still_;
    age_ += dt;
    if (cfg_.freeze_after_s > 0 && age_ > cfg_.freeze_after_s) { still_ = false; return false; }
    if (cfg_.mode == BiasMode::New && cfg_.temp_comp && tc_ >= 0) {
        if (prev_tc_ >= 0 && tc_ != prev_tc_ && std::abs(tc_ - prev_tc_) <= 20 && settled()) {
            Vec3 d = slope_ * static_cast<double>(tc_ - prev_tc_);
            double n = norm(d);
            if (n > cfg_.temp_max_shift) d = d * (cfg_.temp_max_shift / n);
            // Zonder nieuwe meting nooit meer dan temp_max_total opschuiven: een verkeerde helling mag niet onbegrensd doorlopen.
            Vec3 total = shift_since_anchor_ + d;
            double tn = norm(total);
            if (tn > cfg_.temp_max_total) d = total * (cfg_.temp_max_total / tn) - shift_since_anchor_;
            shift_since_anchor_ = shift_since_anchor_ + d;
            bias_ = bias_ + d;
        }
        prev_tc_ = tc_;
    }
    // Venster bijhouden op basis van echte dt (leeftijd per sample).
    for (auto& s : win_) s.age += dt;
    win_.push_back({0.0, gyro, norm(accel)});
    while (!win_.empty() && win_.front().age > cfg_.window_s) win_.pop_front();
    win_span_ = win_.empty() ? 0 : win_.front().age;

    still_ = false;
    if (win_.size() >= 8 && win_span_ >= cfg_.window_s * 0.8) {
        Vec3 mean{};
        bool acc_ok = true;
        for (const auto& s : win_) {
            mean = mean + s.g;
            if (std::fabs(s.an - 1.0) > cfg_.accel_norm_tol) acc_ok = false;
        }
        mean = mean * (1.0 / static_cast<double>(win_.size()));
        Vec3 var{};
        for (const auto& s : win_) {
            Vec3 d = s.g - mean;
            var = var + Vec3{d.x * d.x, d.y * d.y, d.z * d.z};
        }
        var = var * (1.0 / static_cast<double>(win_.size()));
        double smax = std::sqrt(std::max({var.x, var.y, var.z}));
        Vec3 off = mean - bias_;
        // Voor de eerste schatting (of na het herstellen van een bewaarde waarde) is de bias nog niet
        // gemeten: dan geldt de ruime grens op |mean - bias|.
        double mean_max = settled() ? cfg_.gyro_mean_max_settled
                          : (restored_ && cfg_.mode == BiasMode::New) ? cfg_.restored_gate : cfg_.gyro_mean_max;
        const bool candidate = acc_ok && smax <= cfg_.gyro_std_max;
        if (candidate && norm(off) <= mean_max) {
            still_ = true;
            still_total_ += dt;
            const double prior = (restored_ && cfg_.mode == BiasMode::New && !settled()) ? cfg_.restored_weight_s : 0.0;
            double a = dt / std::min(still_total_ + prior, cfg_.tau_s);
            bias_ = bias_ + (mean - bias_) * std::min(1.0, a);
            source_ = BiasSource::Still;
            reacq_t_ = 0;
            shift_since_anchor_ = {};
            qwin_.clear();  // stilstand is leidend: het quasi-bewijs van daarvoor hoort bij de oude bias
            ev_acc_t_ += dt;
            if (cfg_.mode == BiasMode::New && ev_acc_t_ >= cfg_.quasi_window_s) {
                ev_acc_t_ = 0;
                temp_add(mean, 1.0);
            }
        } else if (candidate && cfg_.mode == BiasMode::New) {
            // Stil, maar ver van de bias: pas als dit lang en op hetzelfde punt blijft, is de bias zelf fout.
            if (reacq_t_ > 0 && norm(mean - reacq_mean_) <= cfg_.reacquire_spread) {
                reacq_t_ += dt;
                reacq_mean_ = reacq_mean_ + (mean - reacq_mean_) * std::min(1.0, dt / reacq_t_);
            } else {
                reacq_t_ = dt;
                reacq_mean_ = mean;
            }
            if (reacq_t_ >= cfg_.reacquire_s) {
                bias_ = reacq_mean_;
                shift_since_anchor_ = {};
                still_total_ = std::max(still_total_, cfg_.settled_after_s);
                source_ = BiasSource::Still;
                reacq_t_ = 0;
                qwin_.clear();
            }
        } else {
            reacq_t_ = 0;
        }
    } else {
        reacq_t_ = 0;
    }
    if (cfg_.mode == BiasMode::New) quasi_sample(gyro, accel, dt);
    return still_;
}

// ---- quasi-stille vensters ----

void GyroBiasEstimator::quasi_sample(Vec3 gyro, Vec3 accel, double dt) {
    if (dt > 0.1) {  // een gat: dit venster is niet aaneengesloten
        q_n_ = 0; q_t_ = 0; q_sum_ = q_sq_ = q_asum_ = {}; q_adev_ = 0;
        return;
    }
    ++q_n_;
    q_t_ += dt;
    q_sum_ = q_sum_ + gyro;
    q_sq_ = q_sq_ + Vec3{gyro.x * gyro.x, gyro.y * gyro.y, gyro.z * gyro.z};
    q_asum_ = q_asum_ + accel;
    q_adev_ = std::max(q_adev_, std::fabs(norm(accel) - 1.0));
    if (q_t_ >= cfg_.quasi_window_s) {
        quasi_window_done();
        q_n_ = 0; q_t_ = 0; q_sum_ = q_sq_ = q_asum_ = {}; q_adev_ = 0;
    }
}

void GyroBiasEstimator::quasi_window_done() {
    if (!quasi_enabled_ || still_ || q_n_ < 16) return;
    const double n = static_cast<double>(q_n_);
    Vec3 mean = q_sum_ * (1.0 / n);
    Vec3 var{q_sq_.x / n - mean.x * mean.x, q_sq_.y / n - mean.y * mean.y, q_sq_.z / n - mean.z * mean.z};
    double sd = std::sqrt(std::max({var.x, var.y, var.z, 0.0}));
    if (sd > cfg_.quasi_std_max || q_adev_ > cfg_.quasi_accel_tol) { ++quasi_rejected_; return; }
    const bool known = settled();
    const double gate = known ? cfg_.quasi_gate : cfg_.acquire_gate;
    if (norm(mean - bias_) > gate) { ++quasi_rejected_; return; }
    const double w = 1.0 / (1.0 + (sd / 0.0122) * (sd / 0.0122));  // 0,7 graden/s: gewicht 1/2
    if (known) temp_add(mean, 0.5 * w);
    qwin_.push_back({age_, mean, w});
    while (!qwin_.empty() && qwin_.front().t < age_ - cfg_.quasi_horizon_s) qwin_.pop_front();
    if (!known) {
        if (!quasi_acquire()) return;
    }
    quasi_track();
}

void GyroBiasEstimator::temp_add(Vec3 mean, double w) {
    if (!cfg_.temp_comp || tc_ < 0) return;
    tev_.push_back({age_, tc_, mean, w});
    while (!tev_.empty() && tev_.front().t < age_ - cfg_.temp_horizon_s) tev_.pop_front();
    while (tev_.size() > 1200) tev_.pop_front();
    temp_fit();
}

// Gewogen kleinste kwadraten van het venstergemiddelde tegen de temperatuur, per as.
void GyroBiasEstimator::temp_fit() {
    slope_ = {};
    if (static_cast<int>(tev_.size()) < cfg_.temp_min_entries) return;
    int tmin = tev_.front().tc, tmax = tev_.front().tc;
    double W = 0, mt = 0;
    Vec3 mm{};
    std::vector<double> ws;
    ws.reserve(tev_.size());
    for (const auto& e : tev_) {
        double w = e.w * std::exp(-(age_ - e.t) / cfg_.temp_recency_s);
        ws.push_back(w);
        W += w;
        mt += w * e.tc;
        mm = mm + e.mean * w;
        tmin = std::min(tmin, e.tc);
        tmax = std::max(tmax, e.tc);
    }
    if (tmax - tmin < cfg_.temp_min_range || W < 3.0) return;
    mt /= W;
    mm = mm * (1.0 / W);
    double sxx = 0;
    Vec3 sxy{};
    size_t i = 0;
    for (const auto& e : tev_) {
        double w = ws[i++], dt = e.tc - mt;
        sxx += w * dt * dt;
        sxy = sxy + (e.mean - mm) * (w * dt);
    }
    if (!(sxx > 1e-9)) return;
    Vec3 s = sxy * (1.0 / sxx);
    // Restspreiding per as en onzekerheid van de helling; het bewijs is gecorreleerd (overlappende trilling),
    // dus tel de effectieve steekproef als de helft.
    double res[3] = {0, 0, 0};
    i = 0;
    for (const auto& e : tev_) {
        double w = ws[i++], dt = e.tc - mt;
        Vec3 r = e.mean - mm - s * dt;
        res[0] += w * r.x * r.x; res[1] += w * r.y * r.y; res[2] += w * r.z * r.z;
    }
    double sv[3] = {s.x, s.y, s.z}, out[3];
    for (int a = 0; a < 3; ++a) {
        double sigma = std::max(std::sqrt(res[a] / W), cfg_.temp_sigma_floor);
        double sig_s = sigma / std::sqrt(sxx);
        double shrink = std::fabs(sv[a]) > sig_s ? 1.0 - (sig_s * sig_s) / (sv[a] * sv[a]) : 0.0;
        out[a] = std::max(-cfg_.temp_slope_max, std::min(cfg_.temp_slope_max, sv[a] * shrink));
    }
    slope_ = {out[0], out[1], out[2]};
}

bool GyroBiasEstimator::quasi_acquire() {
    if (static_cast<int>(qwin_.size()) < cfg_.acquire_windows) return false;
    std::vector<std::pair<double, double>> vx, vy, vz;
    double wtot = 0;
    for (const auto& q : qwin_) {
        vx.emplace_back(q.mean.x, q.w); vy.emplace_back(q.mean.y, q.w); vz.emplace_back(q.mean.z, q.w);
        wtot += q.w;
    }
    Vec3 m{weighted_median(vx), weighted_median(vy), weighted_median(vz)};
    Vec3 sum{};
    double win = 0;
    int cnt = 0;
    for (const auto& q : qwin_) {
        if (norm(q.mean - m) <= cfg_.acquire_cluster) { sum = sum + q.mean * q.w; win += q.w; ++cnt; }
    }
    if (win < cfg_.acquire_fraction * wtot || cnt < static_cast<int>(cfg_.acquire_windows * cfg_.acquire_fraction)) return false;
    bias_ = sum * (1.0 / win);
    shift_since_anchor_ = {};
    quasi_acquired_ = true;
    quasi_total_ += cnt * cfg_.quasi_window_s;
    source_ = BiasSource::Quasi;
    q_last_update_t_ = age_;
    // Alleen de cluster blijft als bewijs.
    std::deque<QWin> keep;
    for (const auto& q : qwin_) if (norm(q.mean - bias_) <= cfg_.acquire_cluster) keep.push_back(q);
    qwin_.swap(keep);
    return false;  // de bias is net gezet; het volgen begint met de volgende vensters
}

void GyroBiasEstimator::quasi_track() {
    if (static_cast<int>(qwin_.size()) < cfg_.quasi_min_windows) return;
    std::vector<std::pair<double, double>> vx, vy, vz;
    for (const auto& q : qwin_) {
        double w = q.w * std::exp(-(age_ - q.t) / cfg_.quasi_recency_s);
        vx.emplace_back(q.mean.x, w); vy.emplace_back(q.mean.y, w); vz.emplace_back(q.mean.z, w);
    }
    Vec3 target{weighted_median(vx), weighted_median(vy), weighted_median(vz)};
    double since = q_last_update_t_ > 0 ? std::min(age_ - q_last_update_t_, 5.0) : cfg_.quasi_window_s;
    since = std::max(since, cfg_.quasi_window_s);
    double k = std::min(1.0, since / cfg_.quasi_tau_s);
    double lim = cfg_.quasi_slew * since;
    Vec3 step = (target - bias_) * k;
    auto clampv = [&](double v) { return std::max(-lim, std::min(lim, v)); };
    bias_ = bias_ + Vec3{clampv(step.x), clampv(step.y), clampv(step.z)};
    shift_since_anchor_ = {};
    q_last_update_t_ = age_;
    quasi_total_ += cfg_.quasi_window_s;
    ++quasi_accepted_;
    source_ = BiasSource::Quasi;
}

void OrientationFilter::init_from_accel(Vec3 accel) {
    double n = norm(accel);
    if (n <= 1e-6) { q_ = Quat{}; init_ = true; return; }
    Vec3 a = accel * (1.0 / n);
    Vec3 up{0, 0, 1};
    // Kortste rotatie die de gemeten "omhoog" (controllerstelsel) naar wereld-Z draait.
    double c = dot(a, up);
    if (c < -0.999999) {
        q_ = from_axis_angle({1, 0, 0}, kPi);
    } else {
        Vec3 ax = cross(a, up);
        q_ = normalized(Quat{1 + c, ax.x, ax.y, ax.z});
    }
    init_ = true;
}

bool OrientationFilter::update(Vec3 g, Vec3 accel, double dt) {
    last_accel_used_ = false;
    if (!init_) init_from_accel(accel);
    if (!(dt > 0) || !std::isfinite(dt) || dt > cfg_.max_dt_s) return false;
    double an = norm(accel);
    if (an > 1e-6 && std::fabs(an - 1.0) <= cfg_.accel_gate && cfg_.kp > 0) {
        Vec3 a = accel * (1.0 / an);
        // Verwachte "omhoog" in het controllerstelsel volgens de huidige schatting.
        Vec3 v = rotate(conj(q_), Vec3{0, 0, 1});
        Vec3 e = cross(a, v);
        g = g + e * cfg_.kp;
        last_accel_used_ = true;
    }
    // Exacte integratie van een constante hoeksnelheid over dt (body-frame, rechtsvermenigvuldiging).
    double w = norm(g);
    Quat dq = w > 1e-12 ? from_axis_angle(g, w * dt) : Quat{};
    q_ = normalized(q_ * dq);
    return true;
}

}  // namespace ravengun::core
