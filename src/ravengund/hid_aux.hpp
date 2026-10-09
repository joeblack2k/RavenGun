#pragma once
// Uitbreidingen van de HID-thread voor fase 0/1/3, apart gehouden van hid_thread.cpp zodat die
// klein blijft: hidraw-tap (stempel, temperatuur, poll-fd), recorder-koppeling, magnetometer
// (kalibratie laden, live verstoringsdetectie, kalibratiesessie) en de bijbehorende status.
// Alles hier draait in de HID-thread (geen eigen locks nodig), behalve de Recorder zelf.
#include <cmath>
#include <cstdint>
#include <string>

#include "hidraw_tap.hpp"
#include "ravengun/core/bias_store.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "recorder.hpp"
#include "shared.hpp"

namespace ravengund {

class HidAux {
public:
    HidAux(SharedState& sh, Recorder* rec, bool use_move_clock) : sh_(sh), rec_(rec), move_clock_(use_move_clock) {}
    // De pijplijn van de HID-thread: de magnetometerkalibratie gaat daarin (fase 4: fusie en detector).
    void attach(ravengun::core::Pipeline* p) { pipe_ = p; }
    // Map voor de bewaarde gyro-bias; leeg = uit (niet laden, niet bewaren).
    void set_bias_dir(std::string d) { bias_store_.set_dir(std::move(d)); }
    // De yaw-trim is gewist: het bewaarde bestand moet de nul krijgen.
    void note_trim_cleared() { bias_store_.note_trim_cleared(); }
    // Meerdere spelers: alleen de speler die opneemt voert de recorder (rapporten, LED); alleen één speler publiceert de
    // voortgang van opname en magnetometerkalibratie (die met een lopende kalibratie, anders speler 1).
    void set_recording_owner(bool on) { rec_feed_ = on; }
    bool recording_owner() const { return rec_feed_; }
    void set_progress_owner(bool on) { if (on && !progress_owner_) progress_dirty_ = true; progress_owner_ = on; }
    bool calibrating() const { return collecting_; }

    void on_connect(const std::string& id, const std::string& model, int64_t now_ns);
    void on_disconnect(int64_t now_ns);
    // Na de pijplijn, per rapport.
    void on_report(const ravengun::core::RawReport& r, const ravengun::core::Pipeline& p, double temp_c);
    // Elke lusronde: grenzen van de recorder, voortgang publiceren.
    void periodic(int64_t now_ns);
    // De HID-thread heeft zojuist een LED-kleur verstuurd (alleen bij een wissel gelogd).
    void on_led_sent(int64_t t_ns, int r, int g, int b);

    // Commando's. Resultaat is JSON, of "!code|melding" bij een fout.
    std::string record(const ravengun::ipc::Request& rq, bool led_enabled, const int led[3], int64_t now_ns);
    std::string calib_mag(const ravengun::ipc::Request& rq, int64_t now_ns);
    // Extra statusblokken: "timebase", "hidraw", "magnetometer", "recording" (zonder accolades
    // eromheen; begint met een komma), om in het statusobject te plakken.
    std::string status_blocks(const ravengun::core::Pipeline& p, int64_t now_ns) const;

    HidrawTap tap;

private:
    std::string progress_json(int64_t now_ns) const;
    std::string mag_json(int64_t now_ns) const;

    SharedState& sh_;
    Recorder* rec_;
    bool rec_feed_ = true;
    bool progress_owner_ = true;
    bool move_clock_;
    bool connected_ = false;
    std::string id_, model_, tap_err_;
    int last_led_[3]{-1, -1, -1};
    double temp_c_ = std::nan("");
    double gyro_dps_ = 0;  // gladgemaakte |gyro| (bias-gecorrigeerd), voor de tempo-indicator
    int64_t next_progress_ = 0;
    bool progress_dirty_ = false;

    // Bewaarde gyro-bias
    ravengun::core::BiasPersistence bias_store_;
    // Magnetometer
    std::string mag_dir_;
    bool cal_loaded_ = false;
    std::string cal_err_ = "geen controller";
    ravengun::core::MagCalibration cal_;
    ravengun::core::Pipeline* pipe_ = nullptr;
    ravengun::core::MagCheck last_check_{};
    double norm_ema_ = 0, dip_ema_ = 0;
    bool ema_init_ = false;
    int64_t last_rx_ = 0;
    // Kalibratiesessie
    bool collecting_ = false;
    std::string holder_;
    int64_t collect_start_ns_ = 0;
    int collect_temp_ = -1;
    ravengun::core::MagCollector coll_;
    std::string last_fit_json_;
};

}  // namespace ravengund
