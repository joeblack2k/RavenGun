#pragma once
// De besturingspipe van een game (\\.\pipe\TeknoParrotPipe), naast het gedeelde geheugen van tp-shm. TeknoParrotUi schrijft daar bij
// sommige EmulationProfiles een eigen rapport in (bij Sega ALLS het "USB-IO"-rapport van 64 bytes, elke ~15 ms; alleen schrijven, geen
// handdruk). Zonder TeknoParrotUi komt dat rapport van de daemon: het dialect hieronder rekent het uit de toestand (IoState) uit en
// schrijft het in een bestand (standaard /dev/shm/ravengun_ControlPipe); rg-direct leest dat bestand en stroomt het over TCP naar een
// tweede rg-jvsbridge in de prefix, die de pipe voor de game maakt (tools/rg-direct, "de besturingspipe").
//
// De indeling staat per profiel in packaging/control-pipe-profiles.json (niet in code), zodat andere ALLS-games hem kunnen hergebruiken:
//   analog  acht 16-bit-kanalen vanaf `offset`, little-endian; waarde = min(byte * 257 + 1, 65534) uit een byte 0..255 (zoals
//           TeknoParrotUi zijn analoge bytes omzet). Kanalen zonder as krijgen `idle_byte`.
//   axes    welk as-signaal (p1.x, ...) in welk kanaal staat; fractie 0..1 (na 'invert') naar byte 0..255.
//   bits    bit N van het little-endian bitveld dat op `offset` begint; aan als een van de `signals` aan is, tenzij een van de `unless`
//           aan is (zo komen Up en Down nooit samen, net als bij TeknoParrotUi).
//   coins   een munttellier: telt op bij het LOSLATEN van een van de `signals`, geschreven als 16-bit little-endian (teller * scale).
// Clean-room: de indeling is een feit over de interface, met eigen code beschreven (zie docs/decisions.md).
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/iobridge/dialect.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kDefaultControlPipePath = "/dev/shm/ravengun_ControlPipe";

struct CpAxis {
    std::string signal;
    int channel = 0;
    bool invert = false;
    double idle = 0.5;           // fractie zonder levende bron (geen geldige aim)
};

struct CpBit {
    std::vector<std::string> signals;   // aan als een van deze aan is
    std::vector<std::string> unless;    // ... en geen van deze
    int offset = 0;
    int bit = 0;
};

struct CpCoin {
    std::vector<std::string> signals;
    int offset = 0;
    int scale = 1;
};

struct ControlPipeProfile {
    std::string name;            // bv. "AllsUsbIo"
    std::string description;
    std::string pipe = "TeknoParrotPipe";   // de naam van de pipe in de prefix (zonder \\.\pipe\)
    int size = 64;
    int interval_ms = 15;        // zo vaak schrijft TeknoParrotUi een rapport; rg-direct stroomt in hetzelfde tempo
    int analog_offset = 0, analog_channels = 0, idle_byte = 0;
    std::vector<CpAxis> axes;
    std::vector<CpBit> bits;
    std::vector<CpCoin> coins;
};

struct ControlPipeProfiles {
    std::vector<ControlPipeProfile> profiles;
    const ControlPipeProfile* find(const std::string& name) const;
};

// Leest packaging/control-pipe-profiles.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
ControlPipeProfiles parse_control_pipe_profiles(const std::string& json_text, std::string& err);
std::string validate_control_pipe_profile(const ControlPipeProfile& p);   // leeg = geldig
// De ingebouwde profielen (packaging/control-pipe-profiles.json zoals bij het bouwen; voor het plan van de directe start).
ControlPipeProfiles builtin_control_pipe_profiles(std::string& err);

// Het rapport voor toestand `s` met de huidige muntstanden `coins` (een per CpCoin). Zuiver: wat de tests byte voor byte controleren.
// Bytes die het profiel niet beschrijft zijn 0.
std::vector<uint8_t> control_pipe_render(const ControlPipeProfile& p, const IoState& s, const std::vector<uint32_t>& coins);
// De analoge omzetting van een byte (0..255) naar het 16-bit-kanaal.
uint16_t control_pipe_analog(int byte);

class ControlPipeDialect final : public IoDialect {
public:
    ControlPipeDialect(ControlPipeProfile profile, std::string game, std::string path = kDefaultControlPipePath);
    ~ControlPipeDialect() override { close(); }
    const char* name() const override { return "control-pipe"; }
    std::string game() const override { return game_; }
    bool open(std::string& err) override;
    // Schrijft het rapport; een munt telt op bij het loslaten (flank tussen twee aanroepen).
    void write(const IoState& s) override;
    // Niets ingedrukt, aim in het midden; de muntstanden blijven (een munt die nog vastzat telt niet).
    void neutral() override;
    void close() override;
    std::string status_json() const override;

    const ControlPipeProfile& profile() const { return profile_; }
    const std::string& path() const { return path_; }
    const std::vector<uint32_t>& coins() const { return coins_; }
    std::vector<uint8_t> last_bytes() const { return last_; }

private:
    void put(const IoState& s);
    ControlPipeProfile profile_;
    std::string game_, path_;
    int fd_ = -1;
    bool existed_ = false;
    std::vector<uint32_t> coins_;
    std::vector<bool> coin_down_;
    std::vector<uint8_t> last_;
    uint64_t writes_ = 0, changes_ = 0;
};

}  // namespace ravengun::iobridge
