#include "output_service.hpp"

#include <chrono>

#include "ravengun/json.hpp"
#include "ravengun/output/key_table.hpp"
#include "shared.hpp"

namespace ravengund {

using namespace ravengun::output;
using std::chrono::duration;
using std::chrono::steady_clock;

OutputService::OutputService() : out_(dev_, &kbd_) {}

OutputService::~OutputService() { shutdown(); }

void OutputService::set_initial_transform(const GunTransform& t) {
    std::lock_guard<std::mutex> g(m_);
    out_.set_transform(t);
}

GunTransform OutputService::transform() const {
    std::lock_guard<std::mutex> g(m_);
    return out_.transform();
}

void OutputService::stop_pattern_locked(std::unique_lock<std::mutex>& lk) {
    if (!pattern_thread_.joinable()) return;
    pattern_stop_ = true;
    cv_.notify_all();
    std::thread t = std::move(pattern_thread_);
    lk.unlock();
    t.join();
    lk.lock();
}

OutputResult OutputService::arm() {
    std::unique_lock<std::mutex> lk(m_);
    if (shut_) return {false, "unavailable", "uitvoer is afgesloten"};
    std::string err;
    bool was_armed = out_.armed();
    if (!out_.arm(err)) {
        logf("fout", "arm mislukt: %s", err.c_str());
        return {false, "unavailable", err};
    }
    if (!was_armed) {
        logf("info", "uitvoer GEWAPEND: virtueel lightgun-apparaat %s (%s), toetsenbord %s (%s)", kDeviceName, dev_.event_node().c_str(),
             kKbdDeviceName, kbd_.event_node().c_str());
        if (!out_.keyboard_error().empty()) logf("fout", "virtueel toetsenbord niet beschikbaar: %s", out_.keyboard_error().c_str());
    }
    return {};
}

OutputResult OutputService::disarm() {
    std::unique_lock<std::mutex> lk(m_);
    stop_pattern_locked(lk);
    if (out_.armed()) logf("info", "uitvoer ontwapend: knoppen vrijgegeven");
    out_.disarm();
    return {};
}

OutputResult OutputService::stop_pattern() {
    std::unique_lock<std::mutex> lk(m_);
    stop_pattern_locked(lk);
    out_.release_all();
    return {};
}

void OutputService::release_all() {
    std::lock_guard<std::mutex> g(m_);
    out_.release_all();
}

bool OutputService::armed() const {
    std::lock_guard<std::mutex> g(m_);
    return out_.armed();
}

void OutputService::set_layout(const std::vector<std::pair<std::string, std::string>>& layout) {
    std::lock_guard<std::mutex> g(m_);
    layout_ = layout;
}

void OutputService::feed_live(const LiveAction& a) {
    std::lock_guard<std::mutex> g(m_);
    if (shut_) return;
    ++live_samples_;
    if (a.release_all) {
        live_aim_valid_ = false;
        bool any = false;
        for (int i = 0; i < kGunButtonCount; ++i) any = any || out_.button_down(static_cast<GunButton>(i));
        any = any || !out_.keys_down().empty();
        if (any) { out_.release_all(); ++live_releases_; }
        return;
    }
    if (!out_.armed()) return;
    if (pattern_running_) { ++live_suspended_; return; }  // testpatroon heeft voorrang
    live_aim_valid_ = a.move;
    if (a.move) {
        out_.set_position(a.x, a.y);
        ++live_positions_;
    } else {
        ++live_invalid_;  // ongeldige aim: niet bewegen (cursor blijft staan)
    }
    if (a.buttons) {
        out_.set_button(GunButton::Trigger, a.trigger);
        out_.set_button(GunButton::Reload, a.reload);
        out_.set_button(GunButton::Grenade, a.grenade);
        out_.set_button(GunButton::Aux1, a.aux1);
        out_.set_button(GunButton::Aux2, a.aux2);
        for (const auto& k : a.keys) out_.set_key(k.first, k.second);
    }
}

void OutputService::shutdown() {
    std::unique_lock<std::mutex> lk(m_);
    if (shut_) return;
    shut_ = true;
    stop_pattern_locked(lk);
    out_.shutdown();  // knoppen vrij, apparaat weg
}

OutputResult OutputService::start_pattern(const PatternParams& p) {
    std::string err;
    auto steps = generate_pattern(p, err);
    if (steps.empty()) return {false, "invalid_argument", err.empty() ? "ongeldig patroon" : err};
    std::unique_lock<std::mutex> lk(m_);
    if (!out_.armed()) return {false, "unavailable", "uitvoer is niet gewapend: gebruik eerst 'arm'"};
    stop_pattern_locked(lk);
    if (!out_.armed()) return {false, "unavailable", "uitvoer is niet gewapend"};
    pattern_stop_ = false;
    pattern_running_ = true;
    pattern_state_ = "running";
    pattern_params_ = p;
    pattern_steps_total_ = steps.size();
    pattern_step_ = 0;
    pattern_length_s_ = pattern_length_s(p, steps);
    uint64_t run = ++pattern_run_;
    logf("info", "testpatroon %s gestart (%zu stappen, %.1f s)", to_string(p.kind), steps.size(), pattern_length_s_);
    pattern_thread_ = std::thread(&OutputService::pattern_main, this, std::move(steps), p, run);
    return {};
}

void OutputService::pattern_main(std::vector<PatternStep> steps, PatternParams p, uint64_t run) {
    auto t0 = steady_clock::now();
    auto at = [&](double s) { return t0 + std::chrono::duration_cast<steady_clock::duration>(duration<double>(s)); };
    std::unique_lock<std::mutex> lk(m_);
    bool stopped = false;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (cv_.wait_until(lk, at(steps[i].t_s), [&] { return pattern_stop_ || !out_.armed(); })) { stopped = true; break; }
        out_.set_position(steps[i].x, steps[i].y);
        if (pattern_run_ == run) pattern_step_ = i + 1;
        if (!out_.armed()) { stopped = true; break; }  // schrijffout: GunOutput heeft zichzelf gedisarmd
    }
    if (!stopped && (p.kind == PatternKind::Corners || p.kind == PatternKind::Grid)) {
        // Laatste punt nog dwell_s vasthouden.
        stopped = cv_.wait_until(lk, at(pattern_length_s_), [&] { return pattern_stop_ || !out_.armed(); });
    }
    if (pattern_run_ == run) {
        pattern_running_ = false;
        pattern_state_ = stopped ? "stopped" : "done";
    }
}

OutputResult OutputService::button(GunButton b, const std::string& action) {
    std::unique_lock<std::mutex> lk(m_);
    if (!out_.armed()) return {false, "unavailable", "uitvoer is niet gewapend: gebruik eerst 'arm'"};
    if (action == "down") {
        if (!out_.set_button(b, true)) return {false, "internal", "knop schrijven mislukt"};
    } else if (action == "up") {
        if (!out_.set_button(b, false)) return {false, "internal", "knop schrijven mislukt"};
    } else {  // tap: 80 ms ingedrukt, zonder de lock lang vast te houden
        if (!out_.set_button(b, true)) return {false, "internal", "knop schrijven mislukt"};
        cv_.wait_for(lk, std::chrono::milliseconds(80), [&] { return !out_.armed(); });
        out_.set_button(b, false);
    }
    return {};
}

OutputResult OutputService::set_transform(const GunTransform& t) {
    if (!t.viewport.valid()) return {false, "invalid_argument", "ongeldige viewport"};
    std::lock_guard<std::mutex> g(m_);
    out_.set_transform(t);
    return {};
}

std::string OutputService::status_json() const {
    namespace json = ravengun::json;
    std::lock_guard<std::mutex> g(m_);
    json::Writer w;
    const GunTransform& t = out_.transform();
    const GunOutputStats& s = out_.stats();
    w.begin_object();
    w.key("armed").value(out_.armed());
    w.key("device").begin_object();
    w.key("name").value(kDeviceName).key("vendor").value("1209").key("product").value("7202");
    w.key("created").value(out_.device_created());
    w.key("stable_link").value(kStableLink);
    w.key("event_node").value(dev_.event_node());
    w.key("abs_min").value(kAbsMin).key("abs_max").value(kAbsMax);
    w.end_object();
    w.key("keyboard").begin_object();
    w.key("name").value(kKbdDeviceName).key("vendor").value("1209").key("product").value("7201");
    w.key("created").value(out_.keyboard_created());
    w.key("stable_link").value(kKbdStableLink);
    w.key("event_node").value(kbd_.event_node());
    if (out_.keyboard_error().empty()) w.key("error").null();
    else w.key("error").value(out_.keyboard_error());
    w.end_object();
    w.key("position").begin_object();
    if (out_.have_position()) {
        w.key("nx").value(out_.last_nx()).key("ny").value(out_.last_ny());
        w.key("abs_x").value(out_.last_abs_x()).key("abs_y").value(out_.last_abs_y());
    } else {
        w.key("nx").null().key("ny").null().key("abs_x").null().key("abs_y").null();
    }
    w.end_object();
    w.key("buttons_down").begin_array();
    for (int i = 0; i < kGunButtonCount; ++i)
        if (out_.button_down(static_cast<GunButton>(i))) w.value(to_string(static_cast<GunButton>(i)));
    w.end_array();
    w.key("keys_down").begin_array();
    for (int code : out_.keys_down()) w.value(ravengun::output::key_name(code));
    w.end_array();
    w.key("map").begin_object();
    for (const auto& kv : layout_) w.key(kv.first).value(kv.second);
    w.end_object();
    w.key("transform").begin_object();
    w.key("invert_x").value(t.invert_x).key("invert_y").value(t.invert_y);
    w.key("scale_x").value(t.scale_x).key("scale_y").value(t.scale_y);
    w.key("offset_x").value(t.offset_x).key("offset_y").value(t.offset_y);
    w.key("viewport").begin_object().key("x0").value(t.viewport.x0).key("y0").value(t.viewport.y0).key("x1").value(t.viewport.x1)
        .key("y1").value(t.viewport.y1).end_object();
    w.end_object();
    w.key("pattern").begin_object();
    w.key("state").value(pattern_state_);
    if (pattern_state_ != "none") {
        w.key("kind").value(to_string(pattern_params_.kind));
        w.key("step").value(static_cast<unsigned long long>(pattern_step_));
        w.key("steps").value(static_cast<unsigned long long>(pattern_steps_total_));
        w.key("length_s").value(pattern_length_s_);
    }
    w.end_object();
    w.key("live").begin_object();
    w.key("source").value("move (pijplijn-aim per rapport, geen 250 Hz-klok)");
    w.key("aim_valid").value(live_aim_valid_);
    w.key("samples").value(static_cast<unsigned long long>(live_samples_));
    w.key("positions").value(static_cast<unsigned long long>(live_positions_));
    w.key("invalid_aim").value(static_cast<unsigned long long>(live_invalid_));
    w.key("releases").value(static_cast<unsigned long long>(live_releases_));
    w.key("suspended_by_pattern").value(static_cast<unsigned long long>(live_suspended_));
    w.end_object();
    w.key("stats").begin_object();
    w.key("position_events").value(static_cast<unsigned long long>(s.position_events));
    w.key("button_events").value(static_cast<unsigned long long>(s.button_events));
    w.key("rejected").value(static_cast<unsigned long long>(s.rejected));
    w.key("write_errors").value(static_cast<unsigned long long>(s.write_errors));
    w.key("releases").value(static_cast<unsigned long long>(s.releases));
    w.key("arms").value(static_cast<unsigned long long>(s.arms));
    w.end_object();
    if (out_.last_error().empty()) w.key("last_error").null();
    else w.key("last_error").value(out_.last_error());
    w.end_object();
    return w.str();
}

}  // namespace ravengund
