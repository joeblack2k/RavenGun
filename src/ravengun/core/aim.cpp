#include "ravengun/core/aim.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::core {

const char* to_string(CenterSource s) {
    switch (s) {
        case CenterSource::None: return "none";
        case CenterSource::AutoInitial: return "auto_initial";
        case CenterSource::User: return "user";
    }
    return "none";
}

AimAngles world_angles(Quat q_wb) {
    Vec3 d = rotate(q_wb, kAimAxisBody);
    AimAngles a;
    a.yaw_rad = std::atan2(d.x, d.y);
    a.pitch_rad = std::atan2(d.z, std::hypot(d.x, d.y));
    return a;
}

void AimMapper::recenter(Quat q_wb, CenterSource src) {
    center_ = world_angles(q_wb);
    has_center_ = true;
    src_ = src;
}

AimAngles AimMapper::relative(Quat q_wb) const {
    AimAngles w = world_angles(q_wb);
    if (!has_center_) return w;
    return {wrap_pi(w.yaw_rad - center_.yaw_rad), w.pitch_rad - center_.pitch_rad};
}

AimPoint AimMapper::clamp(double x, double y) {
    AimPoint p;
    p.x = x;
    p.y = y;
    p.x_clamped = std::clamp(x, 0.0, 1.0);
    p.y_clamped = std::clamp(y, 0.0, 1.0);
    p.off_screen = !(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0);
    return p;
}

AimPoint AimMapper::map(double yaw_rad, double pitch_rad) const {
    double h = cfg_.h_edge_deg * kRadPerDeg, v = cfg_.v_edge_deg * kRadPerDeg;
    double x = 0.5 + yaw_rad / (2.0 * h);
    double y = 0.5 - pitch_rad / (2.0 * v);
    return clamp(x, y);
}

}  // namespace ravengun::core
