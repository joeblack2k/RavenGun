#pragma once
// Gemeenschappelijke interface van de dialecten van de IO-brug. Een dialect krijgt de toestand (IoState) en maakt er het
// formaat van dat de game verwacht; het mag uitvoer van de game teruggeven (IoOutputs). De brug (src/ravengund/
// iobridge_service) roept write() periodiek aan voor dialecten met wants_tick(); een "pull"-dialect (unity-tcp) haalt
// de toestand zelf op uit zijn eigen thread.
#include <string>

#include "ravengun/iobridge/io_state.hpp"

namespace ravengun::iobridge {

class IoDialect {
public:
    virtual ~IoDialect() = default;
    virtual const char* name() const = 0;            // "tp-shm", "unity-tcp"
    virtual std::string game() const = 0;            // profiel/game waarvoor het dialect draait
    virtual bool open(std::string& err) = 0;         // begint (idempotent); false + Nederlandse melding bij een fout
    // Schrijft de toestand naar de game. Alleen aangeroepen als wants_tick().
    virtual void write(const IoState& s) = 0;
    virtual bool wants_tick() const { return true; }
    // Alles neutraal (niets ingedrukt, aim in het midden), bijvoorbeeld bij disarm of voor het sluiten.
    virtual void neutral() = 0;
    virtual void close() = 0;
    virtual IoOutputs outputs() const { return {}; }
    // JSON-object (zonder "route"/"game": die zet de brug erbij) met dialectspecifieke status.
    virtual std::string status_json() const = 0;
};

}  // namespace ravengun::iobridge
