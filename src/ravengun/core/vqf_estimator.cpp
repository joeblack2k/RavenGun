#include "ravengun/core/vqf_estimator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ravengun/core/aim.hpp"
#include "vqf.hpp"  // third_party/vqf

namespace ravengun::core {

namespace {
constexpr double kG = 9.80665;  // VQF rekent met m/s2 (rustdrempel voor de accelerometer)

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Quat from_vqf(const vqf_real_t q[4]) { return normalized(Quat{q[0], q[1], q[2], q[3]}); }
}  // namespace

const char* to_string(VqfBias b) {
    switch (b) {
        case VqfBias::Own: return "own";
        case VqfBias::External: return "external";
        case VqfBias::Hybrid: return "hybrid";
    }
    return "own";
}

bool parse_vqf_bias(const char* s, VqfBias& out) {
    if (!s) return false;
    if (!std::strcmp(s, "own")) { out = VqfBias::Own; return true; }
    if (!std::strcmp(s, "external")) { out = VqfBias::External; return true; }
    if (!std::strcmp(s, "hybrid")) { out = VqfBias::Hybrid; return true; }
    return false;
}

VqfEstimator::VqfEstimator(VqfConfig cfg) : cfg_(cfg) { make(); }
VqfEstimator::~VqfEstimator() = default;

VqfEstimator::VqfEstimator(const VqfEstimator& o)
    : cfg_(o.cfg_), vqf_(o.vqf_ ? std::make_unique<VQF>(*o.vqf_) : nullptr), use_mag_(o.use_mag_), have_prev_(o.have_prev_),
      have_mag_(o.have_mag_), have_ref_(o.have_ref_), ref_norm_(o.ref_norm_), ref_dip_deg_(o.ref_dip_deg_), prev_gyro_(o.prev_gyro_), prev_accel_(o.prev_accel_), prev_mag_(o.prev_mag_), yaw_off_(o.yaw_off_),
      fed_(o.fed_), mag_n_(o.mag_n_), mag_dist_n_(o.mag_dist_n_), rest_n_(o.rest_n_), filled_(o.filled_), skipped_(o.skipped_) {}

VqfEstimator& VqfEstimator::operator=(const VqfEstimator& o) {
    if (this != &o) {
        VqfEstimator t(o);
        cfg_ = t.cfg_;
        vqf_ = std::move(t.vqf_);
        use_mag_ = t.use_mag_; have_prev_ = t.have_prev_; have_mag_ = t.have_mag_;
        have_ref_ = t.have_ref_; ref_norm_ = t.ref_norm_; ref_dip_deg_ = t.ref_dip_deg_;
        prev_gyro_ = t.prev_gyro_; prev_accel_ = t.prev_accel_; prev_mag_ = t.prev_mag_;
        yaw_off_ = t.yaw_off_; fed_ = t.fed_; mag_n_ = t.mag_n_; mag_dist_n_ = t.mag_dist_n_;
        rest_n_ = t.rest_n_; filled_ = t.filled_; skipped_ = t.skipped_;
    }
    return *this;
}

void VqfEstimator::make() {
    VQFParams p;
    p.tauAcc = cfg_.tau_acc_s;
    p.tauMag = cfg_.tau_mag_s;
    const bool est = cfg_.bias != VqfBias::External;
    p.motionBiasEstEnabled = est && cfg_.motion_bias;
    p.restBiasEstEnabled = est && cfg_.rest_bias;
    p.magDistRejectionEnabled = cfg_.mag_dist_rejection;
    p.biasClip = cfg_.bias_clip_dps;
    p.restThGyr = cfg_.rest_th_gyr_dps;
    p.restThAcc = cfg_.rest_th_acc;
    vqf_ = std::make_unique<VQF>(p, cfg_.nominal_report_s / 2, -1.0, cfg_.nominal_report_s);
}

void VqfEstimator::reset() {
    const bool use_mag = use_mag_;
    make();
    use_mag_ = use_mag;
    have_prev_ = have_mag_ = false;
    if (have_ref_) vqf_->setMagRef(ref_norm_, ref_dip_deg_ * kRadPerDeg);
    yaw_off_ = 0;
    fed_ = mag_n_ = mag_dist_n_ = rest_n_ = filled_ = skipped_ = 0;
}

void VqfEstimator::set_mag_reference(double norm, double dip_deg) {
    have_ref_ = norm > 0 && std::isfinite(norm) && std::isfinite(dip_deg);
    ref_norm_ = norm;
    ref_dip_deg_ = dip_deg;
    if (have_ref_) vqf_->setMagRef(norm, dip_deg * kRadPerDeg);
}

void VqfEstimator::step(Vec3 gyro, Vec3 accel, double dt_sample, Vec3 ext_bias) {
    const double Ts = cfg_.nominal_report_s / 2;
    vqf_real_t b[3];
    vqf_->getBiasEstimate(b);
    const Vec3 bv{b[0], b[1], b[2]};
    const Vec3 ext = cfg_.bias == VqfBias::Own ? Vec3{} : ext_bias;
    // VQF trekt zijn eigen bias af en integreert gyr * Ts. (gyro - bias_totaal) * (dt/Ts) + bias_vqf geeft na
    // het aftrekken van bias_vqf precies de te integreren hoek (gyro - bias_totaal) * dt.
    const Vec3 g = (gyro - ext - bv) * (dt_sample / Ts) + bv;
    const vqf_real_t gi[3] = {g.x, g.y, g.z};
    const vqf_real_t ai[3] = {accel.x * kG, accel.y * kG, accel.z * kG};
    vqf_->updateGyr(gi);
    vqf_->updateAcc(ai);
}

void VqfEstimator::step_mag(Vec3 mag) {
    const vqf_real_t m[3] = {mag.x, mag.y, mag.z};
    vqf_->updateMag(m);
}

void VqfEstimator::feed(const RawReport& r, double dt_s, Vec3 ext_bias, const Vec3* mag_b) {
    const int n = std::clamp(r.subsamples, 1, 2);
    Vec3 g[2] = {r.gyro[0], r.gyro[n - 1]};
    Vec3 a[2] = {r.accel[0], r.accel[n - 1]};
    if (n == 1) { g[1] = g[0]; a[1] = a[0]; }
    if (!finite3(g[0]) || !finite3(g[1]) || !finite3(a[0]) || !finite3(a[1]) || !finite3(ext_bias) || (mag_b && !finite3(*mag_b))) {
        ++skipped_;
        have_prev_ = false;
        return;
    }
    double dt = dt_s;
    int m = 1;
    if (!(dt > 0) || !std::isfinite(dt) || dt > cfg_.max_gap_s) {
        // Eerste rapport of een gat dat niet wordt geïntegreerd: nominale tijd, geen opvulling.
        if (fed_ > 0 && dt > cfg_.max_gap_s) ++skipped_;
        dt = cfg_.nominal_report_s;
        have_prev_ = false;
    } else {
        m = std::clamp(static_cast<int>(std::lround(dt / cfg_.nominal_report_s)), 1, cfg_.max_fill_reports);
    }
    const double ds = dt / (2.0 * m);
    const int total = 2 * m;
    for (int k = 0; k < total; ++k) {
        Vec3 gk, ak;
        if (k < total - 2) {
            // Opvulling: lineair tussen het laatste subsample van het vorige rapport (tijd 0) en het eerste van dit rapport.
            const double f = static_cast<double>(k + 1) / static_cast<double>(total - 1);
            const Vec3 p0 = have_prev_ ? prev_gyro_ : g[0];
            const Vec3 q0 = have_prev_ ? prev_accel_ : a[0];
            gk = p0 + (g[0] - p0) * f;
            ak = q0 + (a[0] - q0) * f;
        } else {
            gk = g[k - (total - 2)];
            ak = a[k - (total - 2)];
        }
        step(gk, ak, ds, ext_bias);
        if (k >= total - 2 && vqf_->getRestDetected()) ++rest_n_;
        if (mag_b && (k % 2 == 1)) {
            const int i = k / 2;  // virtueel rapport 0..m-1
            Vec3 mk = *mag_b;
            if (i < m - 1) {
                const Vec3 m0 = have_mag_ ? prev_mag_ : *mag_b;
                mk = m0 + (*mag_b - m0) * (static_cast<double>(i + 1) / static_cast<double>(m));
            }
            step_mag(mk);
            if (i == m - 1) {
                ++mag_n_;
                if (vqf_->getMagDistDetected()) ++mag_dist_n_;
            }
        }
    }
    if (m > 1 && have_prev_) filled_ += static_cast<uint64_t>(m - 1);
    prev_gyro_ = g[1];
    prev_accel_ = a[1];
    if (mag_b) { prev_mag_ = *mag_b; have_mag_ = true; }
    have_prev_ = true;
    ++fed_;
}

Quat VqfEstimator::q6() const {
    vqf_real_t q[4];
    vqf_->getQuat6D(q);
    return normalized(from_axis_angle({0, 0, 1}, yaw_off_) * from_vqf(q));
}

Quat VqfEstimator::q9() const {
    vqf_real_t q[4];
    vqf_->getQuat9D(q);
    return normalized(from_axis_angle({0, 0, 1}, yaw_off_) * from_vqf(q));
}

Quat VqfEstimator::q() const { return use_mag_ && have_mag_ ? q9() : q6(); }

void VqfEstimator::align_yaw_to(Quat target) {
    // Een rotatie van +phi rond wereld-Z verandert het azimut van de richtas met -phi.
    const double want = world_angles(target).yaw_rad;
    const double have = world_angles(q()).yaw_rad;
    yaw_off_ = wrap_pi(yaw_off_ + (have - want));
}

Vec3 VqfEstimator::bias() const {
    vqf_real_t b[3];
    vqf_->getBiasEstimate(b);
    return {b[0], b[1], b[2]};
}

bool VqfEstimator::mag_disturbed() const { return vqf_->getMagDistDetected(); }
double VqfEstimator::mag_ref_norm() const { return vqf_->getMagRefNorm(); }
double VqfEstimator::mag_ref_dip_deg() const { return vqf_->getMagRefDip() * kDegPerRad; }
double VqfEstimator::heading_correction_deg() const { return vqf_->getDelta() * kDegPerRad; }

}  // namespace ravengun::core
