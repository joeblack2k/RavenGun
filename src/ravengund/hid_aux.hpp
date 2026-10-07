#pragma once
// Uitbreidingen van de HID-thread voor fase 0/1/3, apart gehouden van hid_thread.cpp zodat die
// klein blijft: hidraw-tap (stempel, temperatuur, poll-fd), recorder-koppeling, magnetometer
// (kalibratie laden, live verstoringsdetectie, kalibratiesessie) en de bijbehorende status.
// Alles hier draait in de HID-thread (geen eigen locks nodig), behalve de Recorder zelf.
#include <cmath>
#include <cstdint>
#include <string>

#include "hidraw_tap.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/core/pipeline.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "recorder.hpp"
#include "shared.hpp"

namespace ravengund {

class HidAux {
public:
    HidAux(SharedState& sh, Recorder* rec, bool use_move_clock) : sh_(sh), rec_(rec), move_clock_(use_move_clock) {}

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
    bool move_clock_;
    bool connected_ = false;
    std::string id_, model_, tap_err_;
    int last_led_[3]{-1, -1, -1};
    double temp_c_ = std::nan("");
    int64_t next_progress_ = 0;
    bool progress_dirty_ = false;

    // Magnetometer
    std::string mag_dir_;
    bool cal_loaded_ = false;
    std::string cal_err_ = "geen controller";
    ravengun::core::MagCalibration cal_;
    ravengun::core::MagDisturbanceDetector det_;
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
