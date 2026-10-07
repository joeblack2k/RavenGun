#pragma once
#include <string>

#include "ravengun/core/pipeline.hpp"
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
    class Recorder* recorder = nullptr;     // fase 0: opnamen (eigendom van main)
};

// Wat de IPC-thread via SharedState::post() in de HID-thread mag doen.
class HidContext {
public:
    virtual ~HidContext() = default;
    virtual std::string status_json(bool full_ids) = 0;
    virtual bool recenter() = 0;
    virtual ravengun::core::OneEuroConfig filter() const = 0;
    virtual void set_filter(const ravengun::core::OneEuroConfig& c) = 0;
    virtual ravengun::core::AimConfig aim() const = 0;
    virtual void set_aim(const ravengun::core::AimConfig& c) = 0;
    // Fase 0/1/3. Resultaat: JSON, of "!code|melding" bij een fout.
    virtual std::string record(const ravengun::ipc::Request& r) = 0;
    virtual std::string calib_mag(const ravengun::ipc::Request& r) = 0;
    // LED-kleur van de bol (getypeerd; de HID-thread stuurt hem meteen en daarna elke led_refresh_ms).
    virtual void set_led(bool enabled, uint8_t r, uint8_t g, uint8_t b) = 0;
};

// Draait tot shared.running false wordt. Eigenaar van de PSMoveAPI-handle en de pijplijn.
void hid_thread_main(SharedState& shared, HidOptions opt);

}  // namespace ravengund
