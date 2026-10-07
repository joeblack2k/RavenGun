#pragma once
// Voorlopige oriëntatie-aim (trackingmodus "orientation"). GEEN geometrische aim-ray:
// de controllerpositie en het schermvlak spelen geen rol. Yaw en pitch van de richtas
// ten opzichte van een vastgelegd midden worden lineair naar schermcoördinaten omgezet.
//
// Richtas: +Y van het controllerstelsel (van handvat naar bol). Nog te bevestigen per
// controller/houder; zie docs/architecture.md.
//
// Genormaliseerde coördinaten: x in [0,1] van links naar rechts, y in [0,1] van boven naar
// beneden, (0.5, 0.5) is het midden. Unclamped waarden kunnen buiten [0,1] liggen.
#include "ravengun/core/math.hpp"

namespace ravengun::core {

struct AimConfig {
    double h_edge_deg = 20.0;   // yaw (graden) vanaf het midden die de linker/rechter schermrand vormt
    double v_edge_deg = 11.25;  // pitch (graden) vanaf het midden die de boven/onderrand vormt
};

enum class CenterSource { None, AutoInitial, User };
const char* to_string(CenterSource s);

struct AimAngles {
    double yaw_rad = 0;    // positief = naar rechts gedraaid t.o.v. het midden
    double pitch_rad = 0;  // positief = omhoog t.o.v. het midden
};

struct AimPoint {
    double x = 0.5, y = 0.5;  // unclamped
    double x_clamped = 0.5, y_clamped = 0.5;
    bool off_screen = false;
};

// Richtas in het controllerstelsel.
inline constexpr Vec3 kAimAxisBody{0, 1, 0};

// Absolute yaw/pitch van de richtas in het wereldstelsel (Z omhoog; yaw rond Z, 0 = wereld +Y,
// positief naar wereld +X ("rechts")).
AimAngles world_angles(Quat q_wb);

class AimMapper {
public:
    explicit AimMapper(AimConfig cfg = {}) : cfg_(cfg) {}
    void recenter(Quat q_wb, CenterSource src);
    void clear_center() { has_center_ = false; src_ = CenterSource::None; }
    bool has_center() const { return has_center_; }
    CenterSource center_source() const { return src_; }
    AimAngles relative(Quat q_wb) const;
    AimPoint map(double yaw_rad, double pitch_rad) const;
    static AimPoint clamp(double x, double y);
    void set_config(const AimConfig& c) { cfg_ = c; }
    const AimConfig& config() const { return cfg_; }

private:
    AimConfig cfg_;
    bool has_center_ = false;
    CenterSource src_ = CenterSource::None;
    AimAngles center_{};
};

}  // namespace ravengun::core
