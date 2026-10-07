#include "ravengun/output/gun_transform.hpp"

#include <cmath>

namespace ravengun::output {

bool Viewport::valid() const {
    return std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1) && x1 > x0 && y1 > y0 &&
           x0 >= 0 && y0 >= 0 && x1 <= 1 && y1 <= 1;
}

Viewport letterbox_viewport(double screen_aspect, double content_aspect) {
    Viewport v;
    if (!(std::isfinite(screen_aspect) && std::isfinite(content_aspect) && screen_aspect > 0 && content_aspect > 0)) return v;
    if (content_aspect < screen_aspect) {  // pillarbox: content is smaller in breedte
        double w = content_aspect / screen_aspect;
        v.x0 = (1 - w) / 2;
        v.x1 = v.x0 + w;
    } else if (content_aspect > screen_aspect) {  // letterbox: content is lager
        double h = screen_aspect / content_aspect;
        v.y0 = (1 - h) / 2;
        v.y1 = v.y0 + h;
    }
    return v;
}

double to_viewport_unit(double n, double v0, double v1) { return (n - v0) / (v1 - v0); }

namespace {
double clamp01(double u, bool& clamped) {
    if (u < 0) { clamped = true; return 0; }
    if (u > 1) { clamped = true; return 1; }
    return u;
}
}  // namespace

std::optional<AbsPoint> to_abs(const GunTransform& t, double nx, double ny) {
    if (!std::isfinite(nx) || !std::isfinite(ny)) return std::nullopt;
    if (!t.viewport.valid() || !(t.scale_x > 0) || !(t.scale_y > 0) || !std::isfinite(t.scale_x) || !std::isfinite(t.scale_y) ||
        !std::isfinite(t.offset_x) || !std::isfinite(t.offset_y) || t.abs_max < 1)
        return std::nullopt;
    double u = to_viewport_unit(nx, t.viewport.x0, t.viewport.x1);
    double v = to_viewport_unit(ny, t.viewport.y0, t.viewport.y1);
    if (t.invert_x) u = 1 - u;
    if (t.invert_y) v = 1 - v;
    u = 0.5 + (u - 0.5) * t.scale_x + t.offset_x;
    v = 0.5 + (v - 0.5) * t.scale_y + t.offset_y;
    AbsPoint p;
    u = clamp01(u, p.clamped_x);
    v = clamp01(v, p.clamped_y);
    p.x = static_cast<int>(std::lround(u * t.abs_max));
    p.y = static_cast<int>(std::lround(v * t.abs_max));
    return p;
}

}  // namespace ravengun::output
