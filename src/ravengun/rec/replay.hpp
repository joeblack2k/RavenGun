#pragma once
// Deterministisch afspelen van een opname door de Pipeline (fase 0).
// Zelfde invoer + zelfde PipelineConfig = bit-exact dezelfde reeks samples (de pijplijn gebruikt
// geen wandklok, geen random en geen threads). De digest (FNV-1a-64 over de JSON van elk sample,
// met volledig ID) maakt dat controleerbaar.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ravengun/core/pipeline.hpp"
#include "ravengun/rec/recording.hpp"

namespace ravengun::rec {

struct ReplayOptions {
    core::PipelineConfig pipeline{};
    // Fase 4: magnetometerkalibratie voor de fusie (zoals de daemon die bij het verbinden laadt).
    bool have_mag_cal = false;
    core::MagCalibration mag_cal{};
    // Beginwaarde voor de gyro-bias (rad/s), zoals de daemon die uit het bewaarde bestand haalt. Standaard geen:
    // een koude start.
    bool have_bias_init = false;
    core::Vec3 bias_init{};
    int bias_init_tc = -1;
    bool tick_before_report = true;  // pijplijn.tick(t_rx) vóór elk rapport (stale/lost zoals live)
    bool keep_json = false;          // alle sample-JSON bewaren (tests)
    // Optioneel: na elk sample (bijv. voor analyse).
    std::function<void(const core::InputSample&, const core::RawReport*)> on_sample;
};

struct ReplayResult {
    bool ok = false;
    std::string error;
    Header header;
    uint64_t reports = 0, optical = 0, leds = 0, marks = 0, samples = 0;
    uint64_t digest = 1469598103934665603ULL;
    bool ended = false;
    std::string end_reason;
    core::InputSample first_valid{}, last{};
    bool have_first_valid = false;
    std::vector<std::string> json;
};

ReplayResult replay_records(const std::vector<Record>& recs, const ReplayOptions& o);
ReplayResult replay_file(const std::string& path, const ReplayOptions& o);
// Leest een hele opname in het geheugen (voor de analysetools).
bool load_file(const std::string& path, std::vector<Record>& out, std::string& err);

}  // namespace ravengun::rec
