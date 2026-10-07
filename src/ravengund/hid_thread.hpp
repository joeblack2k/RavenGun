#pragma once
#include <string>

#include "ravengun/core/pipeline.hpp"
#include "shared.hpp"

namespace ravengund {

struct HidOptions {
    bool enabled = true;                    // false: geen PSMoveAPI (alleen IPC; voor tests)
    std::string preferred_controller;       // leeg = eerste Bluetooth-Move
    int64_t disconnect_ns = 1'500'000'000;  // geen rapport: handle sluiten en opnieuw zoeken
    int idle_sleep_us = 1000;               // slaap als psmove_poll() niets nieuws had
    ravengun::core::PipelineConfig pipeline{};
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
};

// Draait tot shared.running false wordt. Eigenaar van de PSMoveAPI-handle en de pijplijn.
void hid_thread_main(SharedState& shared, HidOptions opt);

}  // namespace ravengund
