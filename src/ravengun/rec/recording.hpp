#pragma once
// Opnameformaat van RavenGun (fase 0): alleen getallen, nooit beelden.
//
// JSON Lines (UTF-8), één object per regel, veld "k" geeft het soort:
//   header  eerste regel: formaat, versie, tag, geredigeerd controller-ID, model, t0, LED.
//   r       één HID-rapport: t_rx, volgnummer, beide subsamples (g en rad/s na de USB-kalibratie
//           van PSMoveAPI), ruwe magnetometer, knoppen, trigger, batterij, en (als het rapport aan
//           hidraw gekoppeld is) de interne stempel, temperatuur en ruwe accel/gyro, en (optioneel, van
//           na de Sharp Shooter-uitbreiding) "xc" (EXT verbonden, 0/1) en "x" (5 ruwe EXT-bytes).
//   o       één optische meting (2D-beeldpositie en straal, nooit pixels).
//   led     LED-kleur gewijzigd (op het moment van versturen).
//   mark    een vrij label van de gebruiker (bijv. "pomp ingedrukt").
//   end     laatste regel: reden van stoppen en tellers.
// Getallen worden met de kortste exact terugleesbare notatie geschreven (std::to_chars), zodat
// opnemen en teruglezen bit-exact dezelfde waarden geven. Onbekende velden worden genegeerd
// (vooruitcompatibel); een hogere versie dan kFormatVersion wordt geweigerd.
// Specificatie: docs/aim-fusion-design.md, sectie "Metingen fase 0/1/3" en docs/procedures.md.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "ravengun/core/input_model.hpp"

namespace ravengun::rec {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFormatName = "ravengun-recording";

struct Header {
    int version = kFormatVersion;
    std::string tag;
    std::string controller;   // altijd geredigeerd (xx:xx:xx:xx:xx:ac)
    std::string model;        // ZCM1, ZCM2, onbekend
    int64_t t0_ns = 0;        // CLOCK_MONOTONIC bij de start
    int64_t unix_s = 0;       // wandklok bij de start (alleen informatief)
    int led[3]{0, 0, 0};
    bool led_enabled = false;
    double move_ticks_per_s = 100000.0;
    std::string dt_source;    // "move" of "rx" (instelling van de daemon)
};

struct LedEvent { int64_t t_ns = 0; int rgb[3]{0, 0, 0}; };
struct Mark { int64_t t_ns = 0; std::string label; };
struct End {
    int64_t t_ns = 0;
    std::string reason;
    uint64_t reports = 0, optical = 0, dropped = 0;
};

enum class Kind { Header, Report, Optical, Led, Mark, End };

struct Record {
    Kind kind = Kind::Header;
    Header header;
    core::RawReport report;
    core::OpticalSample optical;
    LedEvent led;
    Mark mark;
    End end;
};

// Tijden staan in de regels relatief t.o.v. t0 (in de header als string, exact), zodat ze ruim
// binnen de 2^53 van een JSON-getal blijven en de rondgang bit-exact is.
std::string format_header(const Header& h);
std::string format_report(const core::RawReport& r, int64_t t0);
std::string format_optical(const core::OpticalSample& o, int64_t t0);
std::string format_led(const LedEvent& e, int64_t t0);
std::string format_mark(const Mark& m, int64_t t0);
std::string format_end(const End& e, int64_t t0);

// Ontleedt één regel; false met err bij een fout. t0 wordt door een header-regel gezet en voor
// de andere regels gebruikt.
bool parse_line(const std::string& line, Record& out, std::string& err, int64_t& t0);

// Leest een opnamebestand regel voor regel.
class Reader {
public:
    Reader() = default;
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    bool open(const std::string& path, std::string& err);
    // nullopt bij EOF of een fout (error() niet leeg bij een fout).
    std::optional<Record> next();
    const std::string& error() const { return err_; }
    const Header& header() const { return header_; }
    size_t line_number() const { return line_; }

private:
    FILE* f_ = nullptr;
    std::string err_;
    Header header_;
    size_t line_ = 0;
    int64_t t0_ = 0;
};

// Toegestane tag: [a-z0-9_-], 1..40 tekens (wordt deel van de bestandsnaam).
bool valid_tag(const std::string& tag);

// Standaardmap: $XDG_STATE_HOME/ravengun/recordings of ~/.local/state/ravengun/recordings.
std::string default_recordings_dir();

}  // namespace ravengun::rec
