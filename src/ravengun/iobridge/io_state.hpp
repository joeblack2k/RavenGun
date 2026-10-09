#pragma once
// Toestandsmodel van de IO-brug: wat de speler(s) aan de "kast" doen, los van elk dialect. De brug vult dit uit de
// bestaande pijplijn (aim na transformatie, voorspelling en uitvoerklok; knoppen en toetsen van de virtuele apparaten);
// een dialect (tp-shm, unity-tcp, later jvs of hid) vertaalt het naar het formaat dat een game verwacht. Ontwerp:
// docs/architecture.md, "IO-brug".
#include <array>
#include <cstdint>
#include <string>

namespace ravengun::iobridge {

inline constexpr int kIoPlayers = 4;

// Eén speler. De aim is een fractie van het scherm (0,0 = linksboven, 1,1 = rechtsonder), na de transformatie van de
// uitvoer; hij ligt altijd binnen 0..1. `offscreen` zegt dat de ruwe aim buiten het scherm lag (de fractie is dan de rand).
struct IoPlayer {
    bool valid = false;        // er is een geldige aim (Move verbonden en de uitvoer gewapend)
    double x = 0.5, y = 0.5;
    bool offscreen = false;
    bool trigger = false, reload = false, grenade = false, aux1 = false, aux2 = false;
    bool start = false;
};

// Cabinetknoppen. coin[p] is een niveau (ingedrukt zolang de toets ingedrukt is); een dialect maakt er zo nodig pulsen van.
struct IoSystem {
    std::array<bool, kIoPlayers> coin{};
    bool test = false, service = false, volume_up = false, volume_down = false;
};

struct IoState {
    std::array<IoPlayer, kIoPlayers> player{};
    IoSystem sys;
    // Waar komt de toestand vandaan: false = de uitvoer is niet gewapend (alles neutraal).
    bool armed = false;
};

// Van de game naar de daemon (waar het dialect dat kan lezen): recoil-pulsen per speler (teller), lampen, credits.
struct IoOutputs {
    std::array<uint64_t, kIoPlayers> recoil_pulses{};
    std::array<bool, kIoPlayers> start_lamp{};
    std::array<int, kIoPlayers> credits{};   // -1 = onbekend
    bool have_outputs = false;
};

// Namen van de signalen in de dialectbestanden: "test", "service", "coin1".."coin4", "volume_up", "volume_down" en
// "p1.start", "p1.trigger", "p1.reload", "p1.grenade", "p1.aux1", "p1.aux2" (p1..p4), voor assen "p1.x", "p1.y".
// Geeft de waarde van een knopsignaal; false als de naam onbekend is.
bool io_button_signal(const IoState& s, const std::string& name, bool& value);
// Idem voor een astsignaal (0..1); false als de naam onbekend is.
bool io_axis_signal(const IoState& s, const std::string& name, double& value);
bool valid_button_signal(const std::string& name);
bool valid_axis_signal(const std::string& name);

}  // namespace ravengun::iobridge
