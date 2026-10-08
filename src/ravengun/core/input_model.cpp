#include "ravengun/core/input_model.hpp"

#include "ravengun/core/buttons.hpp"
#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"

namespace ravengun::core {

const char* to_string(TrackingMode m) {
    switch (m) {
        case TrackingMode::None: return "none";
        case TrackingMode::Orientation: return "orientation";
        case TrackingMode::OpticalDiagnostic: return "optical_diagnostic";
        case TrackingMode::Hybrid: return "hybrid";
        case TrackingMode::GeometricRay: return "geometric_ray";
    }
    return "none";
}

const char* to_string(LinkState s) {
    switch (s) {
        case LinkState::Disconnected: return "disconnected";
        case LinkState::Fresh: return "fresh";
        case LinkState::Stale: return "stale";
        case LinkState::Lost: return "lost";
    }
    return "disconnected";
}

const char* to_string(YawReference r) {
    switch (r) {
        case YawReference::None: return "none";
        case YawReference::Continuous: return "continuous";
        case YawReference::Interrupted: return "interrupted";
    }
    return "none";
}

BatteryInfo battery_info(int raw) {
    switch (raw) {
        case 0x00: return {"min", "bijna leeg (<20%)", false};
        case 0x01: return {"ge20", "minstens 20%", false};
        case 0x02: return {"ge40", "minstens 40%", false};
        case 0x03: return {"ge60", "minstens 60%", false};
        case 0x04: return {"ge80", "minstens 80%", false};
        case 0x05: return {"max", "vol (niet aan de lader)", false};
        case 0xEE: return {"charging", "laadt", true};
        case 0xEF: return {"charged", "vol (aan de lader)", true};
        default: return {"unknown", "onbekend", false};
    }
}

std::string buttons_to_string(uint32_t mask) {
    std::string r;
    for (const auto& b : kButtonNames) {
        if (mask & b.bit) {
            if (!r.empty()) r += ',';
            r += b.name;
        }
    }
    return r;
}

static void write_buttons(json::Writer& w, uint32_t mask) {
    w.begin_array();
    for (const auto& b : kButtonNames) if (mask & b.bit) w.value(b.name);
    w.end_array();
}

void write_optical_json(json::Writer& w, const OpticalSample& o) {
    w.key("position").begin_object();
    w.key("kind").value("image_2d");
    w.key("valid").value(o.valid);
    w.key("reason").value(o.valid ? "none" : o.reason);
    w.key("camera").value(o.camera_state);
    w.key("track_state").value(o.track_state);
    w.key("frame_seq").value(static_cast<unsigned long long>(o.frame_seq));
    w.key("t_frame_ns").value(static_cast<long long>(o.t_frame_ns));
    w.key("t_processed_ns").value(static_cast<long long>(o.t_processed_ns));
    w.key("image_w").value(o.width).key("image_h").value(o.height);
    if (o.valid) {
        w.key("x_px").value(o.x_px).key("y_px").value(o.y_px);
        w.key("x_norm").value(o.x_norm).key("y_norm").value(o.y_norm);
        w.key("radius_px").value(o.radius_px);
    } else {
        for (const char* k : {"x_px", "y_px", "x_norm", "y_norm", "radius_px"}) w.key(k).null();
    }
    w.key("candidates").value(o.candidates);
    w.key("depth").null();  // onbekend: geen gekalibreerd cameramodel
    w.end_object();
}

void write_sample_json(json::Writer& w, const InputSample& s, bool full_ids) {
    w.begin_object();
    w.key("schema_version").value(s.schema_version);
    w.key("seq").value(static_cast<unsigned long long>(s.seq));
    w.key("t_rx_ns").value(static_cast<long long>(s.t_rx_ns));
    w.key("t_eval_ns").value(static_cast<long long>(s.t_eval_ns));
    w.key("report_dt_ms").value(s.report_dt_s * 1e3);
    w.key("hid_seq").value(s.hid_seq);
    w.key("controller_id").value(full_ids ? s.controller_id : redact_mac(s.controller_id));
    w.key("tracking_mode").value(to_string(s.mode));
    w.key("link").value(to_string(s.link));

    w.key("orientation").begin_object();
    w.key("valid").value(s.orientation_valid);
    w.key("q_wb").begin_array().value(s.q_wb.w).value(s.q_wb.x).value(s.q_wb.y).value(s.q_wb.z).end_array();
    AimAngles wa = world_angles(s.q_wb);
    w.key("world_yaw_deg").value(wa.yaw_rad * kDegPerRad);
    w.key("world_pitch_deg").value(wa.pitch_rad * kDegPerRad);
    w.key("bias_state").value(to_string(s.bias_state));
    w.key("still").value(s.still);
    w.key("gyro_bias_deg_s").begin_array()
        .value(s.gyro_bias.x * kDegPerRad).value(s.gyro_bias.y * kDegPerRad).value(s.gyro_bias.z * kDegPerRad)
        .end_array();
    w.key("yaw_reference").value(to_string(s.yaw_ref));
    w.key("fusion").begin_object();
    w.key("mode").value(s.fusion_mode);
    w.key("yaw_source").value(s.yaw_source);
    w.key("magnetometer").value(s.mag_state);
    w.key("mag_locked").value(s.mag_locked);
    w.key("yaw_pending_deg").value(s.yaw_pending_deg);
    w.key("correction_rate_deg_s").value(s.yaw_corr_rate_deg_s);
    w.key("correction_limit_deg_s").value(s.yaw_corr_limit_deg_s);
    w.key("correction_total_deg").value(s.yaw_corr_total_deg);
    w.key("vertical_bias_deg_s").value(s.yaw_vertical_bias_deg_s);
    w.key("mag_unusable_s").value(s.mag_unusable_s);
    w.end_object();
    w.end_object();

    write_optical_json(w, s.optical);

    w.key("aim").begin_object();
    w.key("valid").value(s.aim_valid);
    w.key("center_source").value(to_string(s.center_source));
    w.key("filter_enabled").value(s.filter_enabled);
    if (s.aim_valid) {
        w.key("yaw_rel_deg").value(s.yaw_rel_deg);
        w.key("pitch_rel_deg").value(s.pitch_rel_deg);
        w.key("x_raw").value(s.x_raw);
        w.key("y_raw").value(s.y_raw);
        w.key("x").value(s.aim.x);
        w.key("y").value(s.aim.y);
        w.key("x_clamped").value(s.aim.x_clamped);
        w.key("y_clamped").value(s.aim.y_clamped);
        w.key("off_screen").value(s.aim.off_screen);
    } else {
        for (const char* k : {"yaw_rel_deg", "pitch_rel_deg", "x_raw", "y_raw", "x", "y", "x_clamped", "y_clamped", "off_screen"})
            w.key(k).null();
    }
    w.end_object();

    w.key("trigger").value(static_cast<int>(s.trigger));
    w.key("ext").begin_object();
    w.key("known").value(s.ext_known);
    w.key("connected").value(s.ext_connected);
    w.key("active").value(s.ext_active);
    w.key("bytes").begin_array();
    for (uint8_t b : s.ext) w.value(static_cast<int>(b));
    w.end_array();
    w.end_object();
    w.key("buttons").begin_object();
    w.key("mask").value(s.buttons);
    w.key("down");
    write_buttons(w, s.buttons);
    w.key("pressed");
    write_buttons(w, s.pressed);
    w.key("released");
    write_buttons(w, s.released);
    w.end_object();

    w.key("source").begin_object();
    w.key("accel_g").begin_array().value(s.accel_g.x).value(s.accel_g.y).value(s.accel_g.z).end_array();
    w.key("gyro_deg_s").begin_array()
        .value(s.gyro_rad_s.x * kDegPerRad).value(s.gyro_rad_s.y * kDegPerRad).value(s.gyro_rad_s.z * kDegPerRad)
        .end_array();
    w.end_object();
    w.end_object();
}

}  // namespace ravengun::core
