#pragma once
// De TCP-kant van \\.\pipe\TeknoParrot_JVS bij een directe start (A10): pakketten uit de bytestroom halen, antwoorden in JVS-vorm
// inpakken en het standaard JVS-bord van RG_JVS_BOARD=true. Gelijk aan jvs_frames(), jvs_pack() en jvs_answer() in
// scripts/launch-direct.py (tests/direct_golden.py vergelijkt ze). rg-direct (tools/rg-direct) gebruikt ze.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ravengun::launch::jvspipe {

using Bytes = std::vector<uint8_t>;

// Complete pakketten (knoop, lengte, gegevens, som; zonder sync, escapes opgelost) uit buf; `rest` is een onvolledig pakket.
std::vector<Bytes> frames(const Bytes& buf, Bytes& rest);
// Antwoordpakket naar de host: sync, adres 0, lengte, gegevens, som; E0 en D0 in de inhoud met escape.
Bytes pack(const Bytes& body);

struct Board {
    Bytes ident;      // met de afsluitende 0 (RG_JVS_IDENT)
    Bytes features;   // functielijst met eindbyte 00 (RG_JVS_FEATURES, hex)
};
Board default_board();
// RG_JVS_IDENT (tekst, latin-1) en RG_JVS_FEATURES (hex; spaties tussen de bytes mogen). false bij ongeldige hex.
bool configure(Board& b, const std::string& ident, const std::string& features_hex);
// Antwoord op de opdrachten (zonder knoop/lengte/som); nullopt bij een reset (0xF0).
std::optional<Bytes> answer(const Board& b, const Bytes& data);

std::string hex(const Bytes& b);
bool from_hex(const std::string& s, Bytes& out);   // bytes.fromhex: spaties tussen de bytes mogen

}  // namespace ravengun::launch::jvspipe
