#include "ravengun/rec/replay.hpp"

#include "ravengun/json.hpp"

namespace ravengun::rec {

namespace {
void fnv(uint64_t& h, const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    h ^= '\n';
    h *= 1099511628211ULL;
}
}  // namespace

ReplayResult replay_records(const std::vector<Record>& recs, const ReplayOptions& o) {
    ReplayResult res;
    core::Pipeline p(o.pipeline);
    core::OpticalSample optical{};
    bool connected = false;
    auto emit = [&](core::InputSample s, const core::RawReport* r) {
        s.optical = optical;  // zoals SharedState::publish: laatste optische meting erbij
        json::Writer w;
        core::write_sample_json(w, s, true);
        fnv(res.digest, w.str());
        if (o.keep_json) res.json.push_back(w.str());
        ++res.samples;
        if (!res.have_first_valid && s.orientation_valid) { res.first_valid = s; res.have_first_valid = true; }
        res.last = s;
        if (o.on_sample) o.on_sample(s, r);
    };
    for (const Record& rc : recs) {
        switch (rc.kind) {
            case Kind::Header:
                res.header = rc.header;
                p.on_connect(rc.header.controller.empty() ? "opname" : rc.header.controller, rc.header.t0_ns);
                if (o.have_mag_cal) p.set_mag_calibration(o.mag_cal);
                if (o.have_bias_init) p.restore_bias(o.bias_init, o.bias_init_tc);
                if (o.have_trim_init) p.set_trim(o.trim_init_dps);
                connected = true;
                break;
            case Kind::Report: {
                if (!connected) {
                    p.on_connect("opname", rc.report.t_rx_ns);
                    if (o.have_mag_cal) p.set_mag_calibration(o.mag_cal);
                    if (o.have_bias_init) p.restore_bias(o.bias_init, o.bias_init_tc);
                    if (o.have_trim_init) p.set_trim(o.trim_init_dps);
                    connected = true;
                }
                if (o.tick_before_report)
                    if (auto t = p.tick(rc.report.t_rx_ns)) emit(*t, nullptr);
                const core::InputSample s = p.on_report(rc.report);
                emit(s, &rc.report);
                if (o.trim_marker_mask && (s.pressed & o.trim_marker_mask)) {
                    p.note_recenter_press();
                    if (o.on_marker) o.on_marker(p, s);
                }
                ++res.reports;
                break;
            }
            case Kind::Optical: optical = rc.optical; p.on_optical(rc.optical); ++res.optical; break;
            case Kind::Led: ++res.leds; break;
            case Kind::Mark: ++res.marks; break;
            case Kind::End: res.ended = true; res.end_reason = rc.end.reason; break;
        }
    }
    res.trim_dps = p.trim().rate_dps();
    res.trim_learned = p.trim().learned();
    res.optical_s = p.bias().optical_time_total();
    res.optical_windows = p.bias().optical_accepted();
    res.optical_still = p.bias().optical_still_windows();
    res.optical_rejected = p.bias().optical_rejected();
    res.ok = true;
    return res;
}

bool load_file(const std::string& path, std::vector<Record>& out, std::string& err) {
    Reader rd;
    if (!rd.open(path, err)) return false;
    Record h;
    h.kind = Kind::Header;
    h.header = rd.header();
    out.push_back(h);
    while (auto r = rd.next()) out.push_back(std::move(*r));
    if (!rd.error().empty()) { err = rd.error(); return false; }
    return true;
}

ReplayResult replay_file(const std::string& path, const ReplayOptions& o) {
    std::vector<Record> recs;
    std::string err;
    if (!load_file(path, recs, err)) {
        ReplayResult r;
        r.error = err;
        return r;
    }
    return replay_records(recs, o);
}

}  // namespace ravengun::rec
