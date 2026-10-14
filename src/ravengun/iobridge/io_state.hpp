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
inline constexpr int kIoPadButtons = 8;   // btn1..btn8: extra kastknoppen (camera, wissers, claxon, schakelen) van een gamepad

// Eén speler. De aim is een fractie van het scherm (0,0 = linksboven, 1,1 = rechtsonder), na de transformatie van de
// uitvoer; hij ligt altijd binnen 0..1. `offscreen` zegt dat de ruwe aim buiten het scherm lag (de fractie is dan de rand).
struct IoPlayer {
    bool valid = false;        // er is een geldige aim (Move verbonden en de uitvoer gewapend)
    double x = 0.5, y = 0.5;
    bool offscreen = false;
    bool trigger = false, reload = false, grenade = false, aux1 = false, aux2 = false;
    bool start = false;
    // Gamepad naast de lightgun (docs/gamepad.md). `pad` = deze speler heeft een verbonden gamepad. Het signaal "pN.wheel" (0..1,
    // midden 0,5) is `wheel` als de gamepad het stuur indeelt (`wheel_from_pad`), anders de aim-x: zo stuurt een Move-speler door
    // te richten (io_axis_signal past die regel toe). Gas en rem (0..1) bestaan alleen met een gamepad; btn1..btn8 zijn extra
    // knoppen die alleen in de IO-brug bestaan.
    bool pad = false, wheel_from_pad = false;
    double wheel = 0.5, gas = 0, brake = 0;
    std::array<bool, kIoPadButtons> btn{};
};

// Wat een gamepad aan een speler levert voor de signalen die alleen in de IO-brug bestaan (GamepadService vult het, de
// uitvoer neemt het over in IoPlayer). Zie IoPlayer voor de betekenis.
struct IoPadInput {
    bool pad = false, wheel_from_pad = false;
    double wheel = 0.5, gas = 0, brake = 0;
    std::array<bool, kIoPadButtons> btn{};
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
// "p1.start", "p1.trigger", "p1.reload", "p1.offscreen" (de herlaadknop of een geldige aim buiten het scherm: de offscreen-schakelaar
// van een Sega-gun), "p1.grenade", "p1.aux1", "p1.aux2", "p1.btn1".."p1.btn8" (p1..p4), voor assen "p1.x",
// "p1.y", "p1.wheel", "p1.gas", "p1.brake".
// Geeft de waarde van een knopsignaal; false als de naam onbekend is.
bool io_button_signal(const IoState& s, const std::string& name, bool& value);
// Idem voor een astsignaal (0..1); false als de naam onbekend is.
bool io_axis_signal(const IoState& s, const std::string& name, double& value);
bool valid_button_signal(const std::string& name);
bool valid_axis_signal(const std::string& name);
// Heeft dit astsignaal nu een levende bron? x/y: een geldige aim; wheel: de gamepad of anders een geldige aim; gas/brake: een
// gamepad. Zonder levende bron schrijft een dialect de rustwaarde van de as (idle). Signalen zonder speler: altijd waar.
bool io_axis_live(const IoState& s, const std::string& name);

}  // namespace ravengun::iobridge
