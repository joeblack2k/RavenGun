#pragma once
// Kleine vector-/quaternionwiskunde voor de oriëntatieschatting.
//
// Conventies (zie docs/architecture.md, "Assen en quaternionconventie"):
// - Alle stelsels zijn rechtshandig.
// - Quat q = (w, x, y, z), Hamilton-product, eenheidsquaternion.
// - Een oriëntatie-quaternion q_wb roteert een vector uit het controllerstelsel (body)
//   naar het wereldstelsel: v_w = q_wb * v_b * conj(q_wb).
// - Hoeken in radialen, tenzij de naam op _deg eindigt.
#include <cmath>

namespace ravengun::core {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegPerRad = 180.0 / kPi;
constexpr double kRadPerDeg = kPi / 180.0;

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double norm(Vec3 a) { return std::sqrt(dot(a, a)); }

struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat conj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
inline double qnorm(Quat q) { return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z); }
inline Quat normalized(Quat q) {
    double n = qnorm(q);
    if (n <= 0 || !std::isfinite(n)) return Quat{};
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

// Quaternion voor een rotatie van `angle` rad rond eenheidsas `axis` (rechterhandregel).
inline Quat from_axis_angle(Vec3 axis, double angle) {
    double n = norm(axis);
    if (n <= 0) return Quat{};
    double s = std::sin(angle / 2) / n;
    return {std::cos(angle / 2), axis.x * s, axis.y * s, axis.z * s};
}

// v' = q * v * conj(q)
inline Vec3 rotate(Quat q, Vec3 v) {
    Quat p{0, v.x, v.y, v.z};
    Quat r = q * p * conj(q);
    return {r.x, r.y, r.z};
}

// Kleinste hoek (rad) tussen twee oriëntaties.
inline double angle_between(Quat a, Quat b) {
    Quat d = normalized(conj(a) * b);
    double w = std::fabs(d.w);
    if (w > 1) w = 1;
    return 2 * std::acos(w);
}

// Hoek terugbrengen naar (-pi, pi].
inline double wrap_pi(double a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

}  // namespace ravengun::core
