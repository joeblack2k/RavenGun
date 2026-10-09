#pragma once
#include <string>

#include "ravengun/core/pipeline.hpp"
#include "ravengun/output/live_feed.hpp"
#include "ravengun/output/rumble.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "shared.hpp"

namespace ravengund {

struct HidOptions {
    bool enabled = true;                    // false: geen PSMoveAPI (alleen IPC; voor tests)
    std::string preferred_controller;       // leeg = eerste Bluetooth-Move
    int64_t disconnect_ns = 1'500'000'000;  // geen rapport: handle sluiten en opnieuw zoeken
    int idle_sleep_us = 1000;               // slaap als psmove_poll() niets nieuws had
    ravengun::core::PipelineConfig pipeline{};
    // LED van de bol (voor de optische tracking). De Move dooft de LED na ~4-5 s zonder nieuwe
    // opdracht, dus de HID-thread stuurt de kleur elke led_refresh_ms opnieuw.
    bool led_enabled = false;
    uint8_t led_r = 0, led_g = 0, led_b = 0;
    int led_refresh_ms = 1000;
    // Trillen bij schieten (output/rumble.hpp), live instelbaar.
    ravengun::output::RumbleConfig rumble{};
    // Bewaarde gyro-bias per controller (map met <controller_key>.json). Leeg = uit (geen laden, geen schrijven).
    std::string bias_dir;
    class Recorder* recorder = nullptr;     // fase 0: opnamen (eigendom van main)
    // Fase 4: live koppeling aan de virtuele lightgun (trekker/reload/grenade/recenter).
    ravengun::output::LiveButtonMap gun_buttons{};
};

// Wat de IPC-thread via SharedState::post() in de HID-thread mag doen.
class HidContext {
public:
    virtual ~HidContext() = default;
    virtual std::string status_json(bool full_ids) = 0;
    virtual bool recenter() = 0;
    // Wist de geleerde yaw-trim (het bewaarde bestand volgt). false = niet beschikbaar.
    virtual bool clear_trim() { return false; }
    virtual ravengun::core::OneEuroConfig filter() const = 0;
    virtual void set_filter(const ravengun::core::OneEuroConfig& c) = 0;
    virtual ravengun::core::AimConfig aim() const = 0;
    virtual void set_aim(const ravengun::core::AimConfig& c) = 0;
    // Voorspelling van de oriëntatie (live, geen reset): de horizon staat in horizon_ms.
    virtual ravengun::core::PredictConfig predict() const { return {}; }
    virtual void set_predict(const ravengun::core::PredictConfig&) {}
    // Fase 0/1/3. Resultaat: JSON, of "!code|melding" bij een fout.
    virtual std::string record(const ravengun::ipc::Request& r) = 0;
    virtual std::string calib_mag(const ravengun::ipc::Request& r) = 0;
    // LED-kleur van de bol (getypeerd; de HID-thread stuurt hem meteen en daarna elke led_refresh_ms).
    virtual void set_led(bool enabled, uint8_t r, uint8_t g, uint8_t b) = 0;
    // Trillen bij schieten: huidige instelling, live wijzigen, en een testpuls (ook ongewapend). rumble_test geeft "1"
    // of "!code|melding" (uitgeschakeld, geen Move).
    virtual ravengun::output::RumbleConfig rumble() const { return {}; }
    virtual void set_rumble(const ravengun::output::RumbleConfig&) {}
    virtual std::string rumble_test(int /*ms*/) { return "!unavailable|rumble niet beschikbaar"; }
    // Fase 4: oriëntatieschatter (mahony | mahony_mag).
    virtual ravengun::core::FusionMode fusion() const = 0;
    virtual void set_fusion(ravengun::core::FusionMode m) = 0;
    // Yaw-trim leren uit centreerdrukken (live aan/uit).
    virtual bool recenter_learning() const { return true; }
    virtual void set_recenter_learning(bool) {}
    // Knoppenindeling van de live koppeling (zie output/live_feed.hpp), zonder herstart.
    virtual void set_gun_buttons(const ravengun::output::LiveButtonMap&) {}
};

// Draait tot shared.running false wordt. Eigenaar van de PSMoveAPI-handle en de pijplijn.
void hid_thread_main(SharedState& shared, HidOptions opt);

}  // namespace ravengund
