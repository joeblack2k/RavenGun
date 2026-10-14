#pragma once
// Dialect "tp-shm": de daemon schrijft zelf de invoer in het gedeelde geheugen dat de TeknoParrot-kern in de game leest
// (64 bytes, per EmulationProfile een eigen indeling), in plaats van TeknoParrotUi dat uit evdev-apparaten doet.
//
// Het formaat is hier clean-room nagebouwd uit een beschrijving van de indeling (docs/research/teknoparrot-io.md en
// docs/decisions.md, "IO-brug: tp-shm"): een 32-bit bitveld "Control" op offset 8 en daarna analoge bytes op vaste
// offsets. De indeling per profiel staat in packaging/tp-shm-profiles.json (bits, offsets, schaal, inversie), niet in code.
// Er is geen code van TeknoParrotUI (GPL-3) overgenomen.
//
// Het bestand: de daemon beschrijft een eigen bestand (standaard /dev/shm/ravengun_JvsState). Een hulpprogramma in de
// Wine-prefix (pipehelper.exe in zijn "shm"-modus; zie scripts/tp-shm-swap.sh) spiegelt dat bestand byte voor byte naar
// de Windows-mapping "TeknoParrot_JvsState" die de kern leest. Zo is de daemon de enige schrijver van de invoervelden.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/iobridge/dialect.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kDefaultTpShmPath = "/dev/shm/ravengun_JvsState";
inline constexpr const char* kTpuiShmPath = "/dev/shm/TeknoParrot_JvsState";

// Een knopbit: bit `bit` van het little-endian bitveld dat op byte `offset` begint (bit 0 = laagste bit van byte offset).
struct TpBit {
    std::string signal;
    int offset = 8;
    int bit = 0;
};

// Een analoge as: fractie 0..1 (na eventuele inversie) naar min..max, als 1 of 2 bytes (2 = big-endian tenzij anders gezet).
struct TpAxis {
    std::string signal;
    int offset = 12;
    int bytes = 1;
    double min = 0, max = 255;
    bool invert = false;         // fractie wordt 1 - fractie (bij 0..255 gelijk aan het complement van de byte)
    bool big_endian = true;
    double idle = 0.5;           // aim-fractie als de speler geen geldige aim heeft
};

struct TpShmProfile {
    std::string name;            // de EmulationProfile-naam, bv. "WartranTroopers"
    std::string description;
    int size = 64;
    std::vector<TpBit> bits;
    std::vector<TpAxis> axes;
    int mirror_first = 0, mirror_last = 7;   // bytes die de daemon van TeknoParrotUi overneemt (verbind-vlag e.d.); -1 = geen
    bool complete = true;        // false: de indeling is niet volledig uit de bron af te leiden (zie description)
    // Offscreen-regel van de kern (findings 09 par. 2): een as <= 1 of >= 254 geldt als "buiten beeld" (herladen). Bij
    // 1-byte-assen met een venster dat 0..255 omvat houdt de weergave binnen het scherm daarom 'offscreen_margin' afstand
    // tot de uiteinden (2 = bytes 2..253) en schrijft ze alleen 0 of 255 als de speler echt buiten het scherm mikt.
    // 0 = uit (stuurwiel, joystick-achtige profielen).
    int offscreen_margin = 2;
};

struct TpShmProfiles {
    std::vector<TpShmProfile> profiles;
    const TpShmProfile* find(const std::string& name) const;
};

// Leest packaging/tp-shm-profiles.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
TpShmProfiles parse_tp_shm_profiles(const std::string& json_text, std::string& err);
std::string validate_tp_shm_profile(const TpShmProfile& p);   // leeg = geldig

// Zet de toestand in `bytes` (n >= p.size): alle bits en assen van het profiel worden overschreven, overige bytes blijven
// zoals ze waren. Zuiver (geen I/O): dit is wat de tests byte voor byte controleren.
void tp_shm_render(const TpShmProfile& p, const IoState& s, uint8_t* bytes, size_t n);
// De bytes die het profiel bezit (bits en assen), gesorteerd.
std::vector<int> tp_shm_owned_bytes(const TpShmProfile& p);

struct TpShmStats {
    uint64_t writes = 0;          // aanroepen van write()
    uint64_t bytes_changed = 0;   // bytes die echt veranderden in het bestand
    bool existed = false;         // het bestand bestond al bij open() (bv. na een herstart van de daemon)
    bool tpui_present = false;    // TeknoParrotUi's eigen bestand werd gevonden (voor de overgenomen bytes)
    std::string last_error;
};

class TpShmDialect final : public IoDialect {
public:
    TpShmDialect(TpShmProfile profile, std::string game, std::string path = kDefaultTpShmPath,
                 std::string tpui_path = kTpuiShmPath);
    ~TpShmDialect() override { close(); }
    const char* name() const override { return "tp-shm"; }
    std::string game() const override { return game_; }
    bool open(std::string& err) override;
    void write(const IoState& s) override;
    void neutral() override;
    void close() override;
    std::string status_json() const override;

    // Alleen de eigen bytes van de indeling schrijven (geen overgenomen bytes, de rest van het bestand nooit aanraken). Voor een bestand
    // dat een ander proces ook schrijft: de jvs-route met een Layout schrijft zo in het bestand van rg-direct, dat in byte 0 de
    // verbindingsvlag van de JVS-bus zet; een volledige schrijfronde zou die met een oudere waarde kunnen overschrijven.
    void set_owned_only(bool on) { owned_only_ = on; }

    const TpShmProfile& profile() const { return profile_; }
    const std::string& path() const { return path_; }
    TpShmStats stats() const;
    // De laatst geschreven bytes (voor tests en status).
    std::vector<uint8_t> last_bytes() const;
    // Leest het bestand rechtstreeks (tests).
    std::vector<uint8_t> read_file() const;

private:
    void apply(const IoState& s);
    TpShmProfile profile_;
    std::string game_, path_, tpui_path_;
    int fd_ = -1, tpui_fd_ = -1;
    uint8_t* map_ = nullptr;
    std::vector<int> owned_;
    bool owned_only_ = false;
    std::vector<uint8_t> last_;
    IoState last_state_;
    mutable TpShmStats st_;
};

}  // namespace ravengun::iobridge
