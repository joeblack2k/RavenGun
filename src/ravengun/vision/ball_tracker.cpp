#include "ravengun/vision/ball_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace ravengun::vision {

namespace {
constexpr double kPi = 3.14159265358979323846;

double hue_dist(double a, double b) {
    double d = std::fmod(std::fabs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

struct Acc {
    int n = 0, color = 0, sat = 0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sl = 0, su = 0, sv = 0;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
};

int32_t find_root(std::vector<int32_t>& p, int32_t a) {
    while (p[static_cast<size_t>(a)] != a) {
        p[static_cast<size_t>(a)] = p[static_cast<size_t>(p[static_cast<size_t>(a)])];
        a = p[static_cast<size_t>(a)];
    }
    return a;
}
void unite(std::vector<int32_t>& p, int32_t a, int32_t b) {
    a = find_root(p, a);
    b = find_root(p, b);
    if (a != b) {
        if (a < b) p[static_cast<size_t>(b)] = a;
        else p[static_cast<size_t>(a)] = b;
    }
}
}  // namespace

double nominal_hue_deg(Rgb c) {
    double r = c.r, g = c.g, b = c.b;
    double y = 0.299 * r + 0.587 * g + 0.114 * b;
    double u = 0.492 * (b - y), v = 0.877 * (r - y);
    return std::atan2(v, u) * 180.0 / kPi;
}

bool led_color_by_name(const std::string& n, Rgb& out) {
    if (n == "magenta") out = {255, 0, 255};
    else if (n == "cyaan" || n == "cyan") out = {0, 255, 255};
    else if (n == "rood" || n == "red") out = {255, 0, 0};
    else if (n == "groen" || n == "green") out = {0, 255, 0};
    else if (n == "blauw" || n == "blue") out = {0, 0, 255};
    else if (n == "geel" || n == "yellow") out = {255, 255, 0};
    else if (n == "uit" || n == "off") out = {0, 0, 0};
    else return false;
    return true;
}

double measured_hue_deg(const std::string& n) {
    if (n == "cyaan" || n == "cyan") return -75;
    if (n == "magenta") return 86;
    if (n == "rood" || n == "red") return 119;
    if (n == "groen" || n == "green") return -145;
    if (n == "blauw" || n == "blue") return -31;
    return std::nan("");
}

const char* to_string(Reason r) {
    switch (r) {
        case Reason::None: return "none";
        case Reason::NoCandidate: return "no_candidate";
        case Reason::TooSmall: return "too_small";
        case Reason::TooLarge: return "too_large";
        case Reason::NotRound: return "not_round";
        case Reason::TooLittleColor: return "too_little_color";
        case Reason::MultipleCandidates: return "multiple_candidates";
        case Reason::JumpUnconfirmed: return "jump_unconfirmed";
        case Reason::AcquirePending: return "acquire_pending";
        case Reason::BadFrame: return "bad_frame";
    }
    return "?";
}

const char* to_string(TrackState s) { return s == TrackState::Tracking ? "tracking" : "searching"; }

BallTracker::BallTracker(TrackerConfig cfg) : cfg_(cfg) { build_lut(); }

void BallTracker::set_config(const TrackerConfig& c) {
    cfg_ = c;
    build_lut();
    reset();
}

void BallTracker::reset() {
    state_ = TrackState::Searching;
    have_last_ = false;
    pending_ = false;
    pend_count_ = 0;
    misses_ = 0;
    vx_ = vy_ = 0;
}

void BallTracker::build_lut() {
    lut_.assign(65536, 0);
    for (int u = 0; u < 256; ++u) {
        for (int v = 0; v < 256; ++v) {
            double du = u - 128, dv = v - 128;
            double c = std::sqrt(du * du + dv * dv);
            if (c < cfg_.min_chroma) continue;
            double h = std::atan2(dv, du) * 180.0 / kPi;
            if (hue_dist(h, cfg_.hue_deg) <= cfg_.hue_tol_deg) lut_[static_cast<size_t>(u << 8 | v)] = 1;
        }
    }
}

void BallTracker::find_blobs(const ImageView& img, int x0, int y0, int x1, int y1) {
    blobs_.clear();
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    labels_.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    parent_.clear();
    parent_.push_back(0);
    // Klasse per pixel: bit0 = kleurpixel, bit1 = verzadigd. Opgeslagen in labels_ via een aparte
    // regelbuffer om twee keer converteren te vermijden.
    std::vector<uint8_t> cls(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = img.data + static_cast<size_t>(y + y0) * static_cast<size_t>(img.stride);
        uint8_t* c = &cls[static_cast<size_t>(y) * static_cast<size_t>(w)];
        for (int x = 0; x < w; ++x) {
            int ax = x + x0;
            const uint8_t* pair = row + (ax & ~1) * 2;
            int Y = row[ax * 2];
            int U = pair[1], V = pair[3];
            uint8_t k = 0;
            if (Y >= cfg_.min_luma && lut_[static_cast<size_t>(U << 8 | V)]) k |= 1;
            if (Y >= cfg_.sat_luma) k |= 2;
            c[x] = k;
        }
    }
    // Eerste doorgang: 8-buren, labels met union-find.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            if (!cls[i]) continue;
            int32_t best = 0;
            auto look = [&](int nx, int ny) {
                if (nx < 0 || nx >= w || ny < 0) return;
                int32_t l = labels_[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)];
                if (!l) return;
                if (!best) best = l;
                else if (l != best) unite(parent_, best, l);
            };
            look(x - 1, y);
            look(x - 1, y - 1);
            look(x, y - 1);
            look(x + 1, y - 1);
            if (!best) {
                best = static_cast<int32_t>(parent_.size());
                parent_.push_back(best);
            }
            labels_[i] = best;
        }
    }
    // Tweede doorgang: momenten per wortel.
    std::vector<int32_t> root_index(parent_.size(), -1);
    std::vector<Acc> acc;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = img.data + static_cast<size_t>(y + y0) * static_cast<size_t>(img.stride);
        for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            int32_t l = labels_[i];
            if (!l) continue;
            int32_t r = find_root(parent_, l);
            int32_t& ix = root_index[static_cast<size_t>(r)];
            if (ix < 0) { ix = static_cast<int32_t>(acc.size()); acc.emplace_back(); }
            Acc& a = acc[static_cast<size_t>(ix)];
            double px = x + x0 + 0.5, py = y + y0 + 0.5;
            ++a.n;
            if (cls[i] & 1) {
                ++a.color;
                const uint8_t* pair = row + ((x + x0) & ~1) * 2;
                a.su += pair[1] - 128;
                a.sv += pair[3] - 128;
            }
            if (cls[i] & 2) ++a.sat;
            a.sx += px; a.sy += py;
            a.sxx += px * px; a.syy += py * py; a.sxy += px * py;
            a.sl += row[(x + x0) * 2];
            a.x0 = std::min(a.x0, x + x0); a.x1 = std::max(a.x1, x + x0);
            a.y0 = std::min(a.y0, y + y0); a.y1 = std::max(a.y1, y + y0);
        }
    }
    for (const Acc& a : acc) {
        if (a.n < cfg_.noise_area_px) continue;
        Blob b;
        b.area = a.n; b.color_px = a.color; b.sat_px = a.sat;
        double n = a.n;
        b.cx = a.sx / n; b.cy = a.sy / n;
        double cxx = a.sxx / n - b.cx * b.cx + 1.0 / 12, cyy = a.syy / n - b.cy * b.cy + 1.0 / 12;
        double cxy = a.sxy / n - b.cx * b.cy;
        double m = (cxx + cyy) / 2, d = std::sqrt(std::max(0.0, (cxx - cyy) * (cxx - cyy) / 4 + cxy * cxy));
        double l1 = m + d, l2 = std::max(1e-9, m - d);
        b.elongation = std::sqrt(l1 / l2);
        b.fill = n / (4 * kPi * std::sqrt(l1 * l2));
        b.radius = std::sqrt(n / kPi);
        b.x0 = a.x0; b.y0 = a.y0; b.x1 = a.x1; b.y1 = a.y1;
        b.mean_luma = a.sl / n;
        if (a.color) {
            double mu = a.su / a.color, mv = a.sv / a.color;
            b.mean_hue_deg = std::atan2(mv, mu) * 180.0 / kPi;
            b.mean_chroma = std::hypot(mu, mv);
        }
        blobs_.push_back(b);
    }
}

void BallTracker::classify(Blob& b, int height) const {
    // Volgorde: eerst kleur (een wit vlak is geen bol, ongeacht de vorm), dan grootte, dan vorm.
    if (b.color_px < cfg_.min_color_frac * b.area) b.verdict = Reason::TooLittleColor;
    else if (b.area < cfg_.min_area_px) b.verdict = Reason::TooSmall;
    else if (b.radius > cfg_.max_radius_frac * height) b.verdict = Reason::TooLarge;
    else if (b.elongation > cfg_.max_elongation || b.fill < cfg_.min_fill || b.fill > cfg_.max_fill) b.verdict = Reason::NotRound;
    else b.verdict = Reason::None;
}

TrackResult BallTracker::process(const ImageView& img) {
    TrackResult res;
    res.width = img.width;
    res.height = img.height;
    res.state = state_;
    if (!img.data || img.width < 8 || img.height < 8 || (img.width & 1) || img.stride < img.width * 2) {
        res.reason = Reason::BadFrame;
        blobs_.clear();
        if (++misses_ >= cfg_.lost_after_frames) { state_ = TrackState::Searching; have_last_ = false; }
        res.state = state_;
        return res;
    }
    const double scale = img.width / 640.0;
    const bool tracking = state_ == TrackState::Tracking && have_last_;
    const double px = last_x_ + vx_, py = last_y_ + vy_;
    const double speed = std::hypot(vx_, vy_);
    const double gate = std::max(cfg_.gate_px_min * scale, cfg_.gate_radius_factor * last_r_) + speed * (1 + misses_);

    auto pick_near = [&](double x, double y, double g, const Blob*& out) {
        out = nullptr;
        double best = g;
        for (const Blob& b : blobs_) {
            if (b.verdict != Reason::None) continue;
            double d = std::hypot(b.cx - x, b.cy - y);
            if (d <= best) { best = d; out = &b; }
        }
    };
    auto count_good = [&] {
        int n = 0;
        for (const Blob& b : blobs_) n += b.verdict == Reason::None;
        return n;
    };

    const Blob* chosen = nullptr;
    Blob chosen_copy;
    bool accept = false;

    // 1. ROI rond de voorspelde positie.
    if (tracking) {
        double half = std::max(cfg_.roi_min_px * scale, cfg_.roi_margin_factor * last_r_ + speed * (1 + misses_));
        half = std::max(half, gate + 2 * last_r_);
        int x0 = std::max(0, static_cast<int>(px - half)), x1 = std::min(img.width, static_cast<int>(px + half) + 1);
        int y0 = std::max(0, static_cast<int>(py - half)), y1 = std::min(img.height, static_cast<int>(py + half) + 1);
        x0 &= ~1;
        find_blobs(img, x0, y0, x1, y1);
        for (Blob& b : blobs_) {
            classify(b, img.height);
            // Raakt de rand van de ROI (die niet de beeldrand is): vorm onbetrouwbaar, volledig beeld nodig.
            bool edge = (b.x0 <= x0 && x0 > 0) || (b.y0 <= y0 && y0 > 0) || (b.x1 >= x1 - 1 && x1 < img.width) ||
                        (b.y1 >= y1 - 1 && y1 < img.height);
            if (edge && b.verdict == Reason::None) b.verdict = Reason::NotRound, b.fill = -1;  // gemarkeerd
        }
        bool edge_hit = std::any_of(blobs_.begin(), blobs_.end(), [](const Blob& b) { return b.fill < 0; });
        pick_near(px, py, gate, chosen);
        if (chosen && !edge_hit) {
            double ratio = chosen->radius / std::max(0.5, last_r_);
            if (ratio <= cfg_.max_radius_ratio && ratio >= 1.0 / cfg_.max_radius_ratio) {
                accept = true;
                res.used_roi = true;
                res.candidates = count_good();
                res.components = static_cast<int>(blobs_.size());
                chosen_copy = *chosen;
            }
        }
    }

    // 2. Volledig beeld.
    if (!accept) {
        res.full_frame_searched = true;
        find_blobs(img, 0, 0, img.width, img.height);
        for (Blob& b : blobs_) classify(b, img.height);
        res.candidates = count_good();
        res.components = static_cast<int>(blobs_.size());
        if (tracking) {
            pick_near(px, py, gate, chosen);
            if (chosen) {
                double ratio = chosen->radius / std::max(0.5, last_r_);
                if (ratio <= cfg_.max_radius_ratio && ratio >= 1.0 / cfg_.max_radius_ratio) {
                    accept = true;
                    chosen_copy = *chosen;
                }
            }
        }
        if (!accept) {
            // Kandidaat voor bevestiging: in de buurt van een lopende bevestiging, anders de enige.
            const Blob* cand = nullptr;
            if (pending_) pick_near(pend_x_, pend_y_, std::max(cfg_.gate_px_min * scale, cfg_.gate_radius_factor * pend_r_), cand);
            if (!cand && res.candidates == 1) {
                for (const Blob& b : blobs_) if (b.verdict == Reason::None) cand = &b;
                pending_ = false;  // andere plek: opnieuw beginnen
            }
            if (cand) {
                if (pending_) ++pend_count_;
                else { pending_ = true; pend_count_ = 1; }
                pend_x_ = cand->cx; pend_y_ = cand->cy; pend_r_ = cand->radius;
                if (pend_count_ >= cfg_.confirm_frames) {
                    accept = true;
                    chosen_copy = *cand;
                    vx_ = vy_ = 0;
                    have_last_ = false;  // geen snelheid uit een sprong afleiden
                } else {
                    res.reason = tracking ? Reason::JumpUnconfirmed : Reason::AcquirePending;
                }
            } else {
                pending_ = false;
                pend_count_ = 0;
                if (res.candidates > 1) {
                    res.reason = Reason::MultipleCandidates;
                } else {
                    // Reden van de "beste" afgewezen component: die met de meeste kleurpixels, of
                    // (zonder kleurpixels) de grootste, bv. een verzadigd vlak.
                    const Blob* best = nullptr;
                    for (const Blob& b : blobs_)
                        if (b.color_px > 0 && (!best || b.color_px > best->color_px)) best = &b;
                    if (!best)
                        for (const Blob& b : blobs_)
                            if (!best || b.area > best->area) best = &b;
                    res.reason = best ? best->verdict : Reason::NoCandidate;
                    if (res.reason == Reason::None) res.reason = Reason::NoCandidate;
                }
            }
        }
    }

    if (accept) {
        if (have_last_ && state_ == TrackState::Tracking) {
            vx_ = chosen_copy.cx - last_x_;
            vy_ = chosen_copy.cy - last_y_;
        } else {
            vx_ = vy_ = 0;
        }
        last_x_ = chosen_copy.cx;
        last_y_ = chosen_copy.cy;
        last_r_ = chosen_copy.radius;
        have_last_ = true;
        state_ = TrackState::Tracking;
        misses_ = 0;
        pending_ = false;
        pend_count_ = 0;
        res.valid = true;
        res.reason = Reason::None;
        res.x_px = chosen_copy.cx;
        res.y_px = chosen_copy.cy;
        res.x_norm = chosen_copy.cx / img.width;
        res.y_norm = chosen_copy.cy / img.height;
        res.radius_px = chosen_copy.radius;
        res.elongation = chosen_copy.elongation;
        res.fill = chosen_copy.fill;
        res.area_px = chosen_copy.area;
        res.color_px = chosen_copy.color_px;
        res.sat_px = chosen_copy.sat_px;
        res.mean_luma = chosen_copy.mean_luma;
        res.mean_hue_deg = chosen_copy.mean_hue_deg;
        res.mean_chroma = chosen_copy.mean_chroma;
    } else {
        if (++misses_ >= cfg_.lost_after_frames && state_ == TrackState::Tracking) {
            state_ = TrackState::Searching;
            have_last_ = false;
            vx_ = vy_ = 0;
        }
    }
    res.state = state_;
    return res;
}

SceneStats BallTracker::scene_stats(const ImageView& img, int step) const {
    SceneStats s;
    if (!img.data || img.width < 2 || img.height < 1 || img.stride < img.width * 2) return s;
    if (step < 1) step = 1;
    std::array<int, 256> yh{};
    double sy = 0, su = 0, sv = 0, sr = 0, sg = 0, sb = 0;
    int sat = 0, match = 0, chrom = 0;
    std::array<int, 12> hh{};
    for (int y = 0; y < img.height; y += step) {
        const uint8_t* row = img.data + static_cast<size_t>(y) * static_cast<size_t>(img.stride);
        for (int x = 0; x < img.width; x += step) {
            const uint8_t* pair = row + (x & ~1) * 2;
            int Y = row[x * 2], U = pair[1], V = pair[3];
            ++s.samples;
            ++yh[static_cast<size_t>(Y)];
            sy += Y; su += U; sv += V;
            double du = U - 128, dv = V - 128;
            sr += std::clamp(Y + 1.140 * dv, 0.0, 255.0);
            sg += std::clamp(Y - 0.395 * du - 0.581 * dv, 0.0, 255.0);
            sb += std::clamp(Y + 2.032 * du, 0.0, 255.0);
            if (Y >= cfg_.sat_luma) ++sat;
            if (Y >= cfg_.min_luma && lut_[static_cast<size_t>(U << 8 | V)]) ++match;
            if (Y >= cfg_.min_luma && std::hypot(du, dv) >= cfg_.min_chroma) {
                ++chrom;
                double h = std::atan2(dv, du) * 180.0 / kPi;  // -180..180
                int bin = std::clamp(static_cast<int>((h + 180.0) / 30.0), 0, 11);
                ++hh[static_cast<size_t>(bin)];
            }
        }
    }
    if (!s.samples) return s;
    double n = s.samples;
    s.mean_y = sy / n; s.mean_u = su / n; s.mean_v = sv / n;
    s.mean_r = sr / n; s.mean_g = sg / n; s.mean_b = sb / n;
    s.sat_frac = sat / n;
    s.color_match_frac = match / n;
    s.chromatic_frac = chrom / n;
    for (size_t i = 0; i < 12; ++i) s.hue_hist_frac[i] = chrom ? hh[i] / static_cast<double>(chrom) : 0;
    int cum = 0;
    bool p50 = false;
    for (int v = 0; v < 256; ++v) {
        cum += yh[static_cast<size_t>(v)];
        if (!p50 && cum >= 0.5 * n) { s.p50_y = v; p50 = true; }
        if (cum >= 0.99 * n) { s.p99_y = v; break; }
    }
    return s;
}

}  // namespace ravengun::vision
