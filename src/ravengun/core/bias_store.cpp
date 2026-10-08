#include "ravengun/core/bias_store.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ravengun/core/magnetometer.hpp"   // atomic_write_file, controller_key
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::core {

std::string default_bias_dir() {
    const char* x = std::getenv("XDG_STATE_HOME");
    if (x && x[0] == '/') return std::string(x) + "/ravengun/gyro-bias";
    const char* home = std::getenv("HOME");
    if (!home || home[0] != '/') return {};
    return std::string(home) + "/.local/state/ravengun/gyro-bias";
}

std::string bias_to_json(const SavedBias& b) {
    json::Writer w;
    w.begin_object();
    w.key("format").value("ravengun-gyro-bias");
    w.key("version").value(kBiasFileVersion);
    w.key("controller_key").value(b.key);
    w.key("bias_deg_s").begin_array()
        .value(b.bias.x * kDegPerRad).value(b.bias.y * kDegPerRad).value(b.bias.z * kDegPerRad).end_array();
    w.key("tc").value(b.tc);
    w.key("unix_s").value(static_cast<long long>(b.unix_s));
    w.key("source").value(b.source);
    w.end_object();
    return w.str();
}

bool bias_from_json(const std::string& text, SavedBias& out, std::string& err) {
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) { err = "beschadigd: geen geldige JSON"; return false; }
    const json::Value& o = pr.value;
    const json::Value* f = o.find("format");
    if (!f || !f->is_string() || f->str != "ravengun-gyro-bias") { err = "beschadigd: verkeerd formaat"; return false; }
    const json::Value* v = o.find("version");
    if (!v || !v->is_number() || static_cast<int>(v->num) != kBiasFileVersion) { err = "andere versie"; return false; }
    const json::Value* b = o.find("bias_deg_s");
    if (!b || !b->is_array() || b->arr.size() != 3) { err = "beschadigd: bias_deg_s"; return false; }
    double d[3];
    for (size_t i = 0; i < 3; ++i) {
        const json::Value& e = b->arr[i];
        // Een Move-gyro heeft een bias van een paar graden/s; alles boven 20 is een fout, geen meting.
        if (!e.is_number() || !std::isfinite(e.num) || std::fabs(e.num) > 20.0) { err = "beschadigd: bias buiten bereik"; return false; }
        d[i] = e.num;
    }
    SavedBias s;
    s.bias = Vec3{d[0], d[1], d[2]} * kRadPerDeg;
    if (const json::Value* k = o.find("controller_key"); k && k->is_string()) s.key = k->str;
    if (const json::Value* t = o.find("tc"); t && t->is_number() && std::isfinite(t->num)) s.tc = static_cast<int>(t->num);
    if (const json::Value* u = o.find("unix_s"); u && u->is_number() && std::isfinite(u->num)) s.unix_s = static_cast<int64_t>(u->num);
    if (const json::Value* sr = o.find("source"); sr && sr->is_string()) s.source = sr->str;
    out = s;
    return true;
}

bool save_bias(const std::string& dir, const SavedBias& b, std::string& err) {
    if (dir.empty()) { err = "geen map (HOME niet gezet)"; return false; }
    if (b.key.empty()) { err = "geen controller-sleutel"; return false; }
    return atomic_write_file(dir + "/" + b.key + ".json", bias_to_json(b) + "\n", err);
}

bool load_bias(const std::string& dir, const std::string& key, SavedBias& out, std::string& err) {
    if (dir.empty()) { err = "geen map"; return false; }
    std::string path = dir + "/" + key + ".json";
    FILE* f = std::fopen(path.c_str(), "re");
    if (!f) { err = "ontbreekt"; return false; }
    std::string text;
    char buf[2048];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 16384) text.append(buf, n);
    std::fclose(f);
    if (!bias_from_json(text, out, err)) return false;
    if (!out.key.empty() && out.key != key) { err = "beschadigd: hoort bij een andere controller"; return false; }
    out.key = key;
    return true;
}

bool BiasSaver::eligible(const GyroBiasEstimator& e) {
    return e.state() == BiasState::Estimated && (e.source() == BiasSource::Still || e.source() == BiasSource::Quasi);
}

bool BiasSaver::due(const GyroBiasEstimator& e, double now_s) const {
    if (!eligible(e)) return false;
    if (!have_) return now_s - connect_t_ >= 20.0 || e.still_time_total() >= 3.0;
    double diff = norm(e.bias() - last_) * kDegPerRad;
    double since = now_s - last_t_;
    return (diff >= 0.15 && since >= 30.0) || (diff >= 0.03 && since >= 300.0);
}

bool BiasSaver::due_final(const GyroBiasEstimator& e) const {
    if (!eligible(e)) return false;
    if (!have_) return true;
    return norm(e.bias() - last_) * kDegPerRad >= 0.03;
}

bool BiasPersistence::on_connect(Pipeline& p, const std::string& id, double now_s, int64_t unix_s, std::string* info) {
    loaded_ = false;
    err_.clear();
    key_ = controller_key(id);
    if (dir_.empty()) return false;
    saver_.connected(now_s);
    std::string err;
    if (!load_bias(dir_, key_, loaded_val_, err)) {
        if (err != "ontbreekt" && info) *info = "bewaarde gyro-bias niet gebruikt: " + err;
        return false;
    }
    loaded_ = true;
    if (p.bias().state() != BiasState::None) return false;   // er is al gemeten (herverbinding): die waarde is nieuwer
    p.restore_bias(loaded_val_.bias, loaded_val_.tc);
    saver_.saved(p.bias(), now_s);   // niet meteen terugschrijven
    if (info) {
        char age[48] = "tijd onbekend";
        if (loaded_val_.unix_s > 0 && unix_s > 0)
            std::snprintf(age, sizeof age, "%.1f uur oud", static_cast<double>(unix_s - loaded_val_.unix_s) / 3600.0);
        char b[256];
        std::snprintf(b, sizeof b, "gyro-bias hersteld uit %s: %+.2f %+.2f %+.2f graden/s (tc %d, %s)", dir_.c_str(),
                      loaded_val_.bias.x * kDegPerRad, loaded_val_.bias.y * kDegPerRad, loaded_val_.bias.z * kDegPerRad, loaded_val_.tc, age);
        *info = b;
    }
    return true;
}

bool BiasPersistence::save(const Pipeline& p, double now_s, int64_t unix_s, const char* why, std::string* info) {
    SavedBias sb;
    sb.bias = p.bias().bias();
    sb.tc = p.last_temperature_raw();
    sb.unix_s = unix_s;
    sb.key = key_;
    sb.source = to_string(p.bias().source());
    std::string err;
    const bool ok = save_bias(dir_, sb, err);
    saver_.saved(p.bias(), now_s);   // ook na een fout: pas na de volgende termijn opnieuw proberen
    if (ok) {
        ++saves_;
        last_saved_unix_ = unix_s;
        err_.clear();
        if (info) {
            char b[200];
            std::snprintf(b, sizeof b, "gyro-bias bewaard (%s): %+.2f %+.2f %+.2f graden/s, tc %d", why, sb.bias.x * kDegPerRad,
                          sb.bias.y * kDegPerRad, sb.bias.z * kDegPerRad, sb.tc);
            *info = b;
        }
    } else {
        if (info && err != err_) *info = "gyro-bias bewaren mislukt: " + err;   // één melding per soort fout
        err_ = err;
    }
    return true;
}

bool BiasPersistence::on_report(const Pipeline& p, double now_s, int64_t unix_s, std::string* info) {
    if (dir_.empty() || key_.empty() || !saver_.due(p.bias(), now_s)) return false;
    return save(p, now_s, unix_s, "meting", info);
}

bool BiasPersistence::on_disconnect(const Pipeline& p, double now_s, int64_t unix_s, std::string* info) {
    if (dir_.empty() || key_.empty() || !saver_.due_final(p.bias())) return false;
    return save(p, now_s, unix_s, "verbroken", info);
}

}  // namespace ravengun::core
