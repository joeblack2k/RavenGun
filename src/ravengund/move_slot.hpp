#pragma once
// Eén speler in de HID-thread: de pijplijn van zijn Move en alles eromheen (bias, fusie, aim, filter, voorspelling, trim,
// knoppen, rumble, LED, opname- en kalibratiehulp, de live koppeling aan zijn virtuele gun). Bevat geen PSMoveAPI-code:
// hid_thread.cpp leest de rapporten en stuurt de uitvoerrapporten; tests voeren synthetische rapporten aan (tests/test_players.cpp).
// Alles draait in de HID-thread (geen locks), behalve wat via SharedState/OutputService gaat.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "hid_aux.hpp"
#include "hid_thread.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/output/rumble.hpp"
#include "shared.hpp"

namespace ravengund {

struct ControllerInfo {
    bool open = false;
    std::string id;
    std::string model;
    std::string connection;
    bool has_calibration = false;
    int64_t connected_at_ns = 0;
    uint64_t connects = 0, disconnects = 0;
};

class ProcStats {
public:
    void add(double us) {
        if (v_.size() < 4096) v_.push_back(us);
        else { v_[pos_] = us; pos_ = (pos_ + 1) % 4096; }
    }
    double pct(double p) const {
        if (v_.empty()) return 0;
        auto c = v_;
        size_t k = static_cast<size_t>(p * static_cast<double>(c.size() - 1) + 0.5);
        std::nth_element(c.begin(), c.begin() + static_cast<long>(k), c.end());
        return c[k];
    }
    double max() const { return v_.empty() ? 0 : *std::max_element(v_.begin(), v_.end()); }

private:
    std::vector<double> v_;
    size_t pos_ = 0;
};

// Wat er naar de Move moet (LED en rumble in één uitvoerrapport).
struct MoveOutputReport {
    uint8_t r = 0, g = 0, b = 0, rumble = 0;
    bool led_due = false;   // LED-keepalive of gewijzigde kleur
};

// Zet de instellingen (effectief voor de controller van deze speler) om in HidOptions. Velden in `keep` (sleutels zoals in
// config.json, bv. "filter.beta") komen van de opdrachtregel en blijven zoals ze in `o` staan.
void apply_settings_to_options(const ravengun::config::Settings& eff, const std::set<std::string>& keep, int player, HidOptions& o);

class MoveSlot {
public:
    // base: de opties van de daemon (standaard plus opdrachtregel); de slot neemt er een kopie van.
    MoveSlot(int player, SharedState& sh, const HidOptions& base);
    MoveSlot(const MoveSlot&) = delete;
    MoveSlot& operator=(const MoveSlot&) = delete;

    int player() const { return player_; }
    // Het adres waarvoor de instellingen nu gelden (leeg = de standaard, geen Move aan deze speler toegewezen).
    const std::string& assigned() const { return assigned_; }
    // Neemt de instellingen over (zonder de pijplijn te resetten als er niets verandert). eff = config::effective() voor het
    // adres van de controller van deze speler; cli_keys = velden die de opdrachtregel vastlegde.
    void configure(const std::string& address, const ravengun::config::Settings& eff, const std::set<std::string>& cli_keys);

    // Levenscyclus van de Move van deze speler.
    void connect(const ControllerInfo& ci, int64_t now_ns);
    // Eén rapport (de HID-thread leest het met PSMoveAPI). temp_c = temperatuur in graden (of NaN).
    void on_report(const ravengun::core::RawReport& r, double temp_c, int64_t t_done_ns = 0);
    // Elke lusronde: staleness en de rumble-tijd. Geeft true als de verbinding als verloren moet worden gesloten.
    bool tick(int64_t now_ns);
    void disconnect(int64_t now_ns);
    bool connected() const { return ci_.open; }
    const ControllerInfo& info() const { return ci_; }

    // LED/rumble: het uitvoerrapport dat nu weg moet (leeg = niets). Na de poging: output_sent.
    std::optional<MoveOutputReport> due_output(int64_t now_ns);
    // ok = verstuurd; ignored = PSMoveAPI hield het tegen (ongewijzigd binnen 4 s): dan geen teller, wel opnieuw proberen.
    void output_sent(int64_t now_ns, const MoveOutputReport& rep, bool ok, bool ignored = false);
    // Laatste rapport bij afsluiten: rumble 0 (en de LED uit als wij hem aanstuurden). Leeg = niets te doen.
    std::optional<MoveOutputReport> final_output() const;
    // Knipperen (om te zien welke Move het is): wit/uit om de 250 ms en een korte trilling.
    void blink(int64_t now_ns, int ms = 3000);

    // Knoppen van de Navigation Controller van deze speler (0 = geen of losgelaten).
    void set_nav(uint32_t buttons, int64_t now_ns);

    // HidContext-opdrachten voor deze speler.
    std::string status_json(bool full_ids, int64_t start_ns);
    std::string record(const ravengun::ipc::Request& r);
    std::string calib_mag(const ravengun::ipc::Request& r) { return aux_.calib_mag(r, mono_ns()); }
    bool recenter();
    bool clear_trim();
    ravengun::core::OneEuroConfig filter() const { return pipe_.config().filter; }
    void set_filter(const ravengun::core::OneEuroConfig& c) { pipe_.set_filter(c); opt_.pipeline.filter = c; }
    ravengun::core::AimConfig aim() const { return pipe_.config().aim; }
    void set_aim(const ravengun::core::AimConfig& c) { pipe_.set_aim(c); opt_.pipeline.aim = c; }
    ravengun::core::PredictConfig predict() const { return pipe_.config().predict; }
    void set_predict(const ravengun::core::PredictConfig& c) { pipe_.set_predict(c); opt_.pipeline.predict = c; }
    void set_led(bool en, uint8_t r, uint8_t g, uint8_t b);
    ravengun::output::RumbleConfig rumble() const { return opt_.rumble; }
    void set_rumble(const ravengun::output::RumbleConfig& c) { opt_.rumble = c; rumble_.set_config(c); }
    std::string rumble_test(int ms);
    ravengun::core::FusionMode fusion() const { return pipe_.fusion_mode(); }
    void set_fusion(ravengun::core::FusionMode m);
    bool recenter_learning() const { return pipe_.trim().config().enabled; }
    void set_recenter_learning(bool on);
    void set_gun_buttons(const ravengun::output::LiveButtonMap& m);
    const ravengun::output::LiveButtonMap& gun_buttons() const { return opt_.gun_buttons; }

    // Opname en voortgang: alleen de speler die opneemt voert de recorder; alleen één speler publiceert de voortgang.
    void set_recording_owner(bool on) { aux_.set_recording_owner(on); }
    void set_progress_owner(bool on) { aux_.set_progress_owner(on); }
    bool calibrating() const { return aux_.calibrating(); }
    void periodic(int64_t now_ns) { aux_.periodic(now_ns); }

    ravengun::core::Pipeline& pipeline() { return pipe_; }
    HidAux& aux() { return aux_; }
    const HidOptions& options() const { return opt_; }

private:
    void emit(const ravengun::core::InputSample& s);
    void ps_hold_step(const ravengun::core::InputSample& s);
    void rumble_step(int64_t now);
    void publish_info();

    int player_;
    SharedState& sh_;
    HidOptions opt_;
    std::string assigned_;
    ravengun::core::Pipeline pipe_;
    HidAux aux_;
    ControllerInfo ci_;
    ProcStats ps_;
    ravengun::output::RumbleEngine rumble_;
    ravengun::output::ReportGate gate_;
    ravengun::output::RumbleInput rin_;
    ravengun::output::LiveGunFeed live_;
    int64_t armed_checked_ = 0;
    uint64_t recoil_seen_ = 0;  // Unity-route: laatst verwerkte stand van SharedState::game_recoil
    // PS-knop lang ingedrukt (kPsHoldStopNs): de lopende game stoppen, zoals de Stop-knop van de app. Eén keer per vasthouden.
    int64_t ps_down_since_ = 0;
    bool ps_hold_fired_ = false;
    int64_t next_led_ = 0;
    bool led_dirty_ = false;
    uint64_t optical_seen_ = 0;
    uint32_t nav_ = 0;
    int last_battery_ = -2;
    int64_t blink_until_ = 0, blink_start_ = 0;
    uint8_t blink_last_ = 0;
    // Tellers (alleen in de HID-thread).
    uint64_t led_updates_ = 0, led_failures_ = 0;
    uint64_t rumble_reports_ = 0, rumble_failures_ = 0, rumble_sent_ = 0;
};

}  // namespace ravengund
