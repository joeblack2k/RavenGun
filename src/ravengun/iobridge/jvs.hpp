#pragma once
// JVS-kern (Sega/Namco I/O-protocol, "JAMMA Video Standard") voor het dialect "jvs" van de IO-brug: framing (sync 0xE0,
// escape 0xD0), checksum, adressering van meerdere nodes aan een bus, en de commando's van een I/O-bord (identificatie,
// versies, functiecapabilities, schakelaars, munten, analoge ingangen, de uitgangen). De kern is zuiver: bytes erin, bytes
// eruit; geen bestanden of apparaten (de pty en de thread zitten in jvs_dialect.*). Per bord een beschrijving in
// packaging/jvs-boards.json (ID-string, capabilities, spelers, schakelbits, analoge kanalen met kalibratievenster); wat
// een signaalnaam betekent staat in io_state.hpp.
//
// Bronnen en clean-room: het JVS-protocol is een publieke specificatie (JAMMA Video Standard 3.0) en de feiten over de
// board-ID-strings, versiebytes, schakelbitindeling en de aswiskunde komen uit de onderzoeksmap van de eigenaar
// (findings/07_jvs_implementation_spec.md en 08_gun_axis_math.md). Er is geen code overgenomen uit TeknoParrot,
// OpenParrot of uit de gedecompileerde bestanden; deze implementatie is zelf geschreven (docs/decisions.md, "IO-brug stap 3").
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ravengun/iobridge/io_state.hpp"

namespace ravengun::iobridge {

inline constexpr uint8_t kJvsSync = 0xE0;
inline constexpr uint8_t kJvsEscape = 0xD0;
inline constexpr uint8_t kJvsBroadcast = 0xFF;
inline constexpr uint8_t kJvsHostAddr = 0x00;

// Statusbyte van een antwoord en rapportbyte per commando.
inline constexpr uint8_t kJvsStatusOk = 0x01, kJvsStatusUnknownCmd = 0x02, kJvsStatusSumError = 0x03, kJvsStatusOverflow = 0x04;
inline constexpr uint8_t kJvsReportOk = 0x01, kJvsReportBadParamCount = 0x02, kJvsReportBadParamData = 0x03, kJvsReportBusy = 0x04;

// Commandocodes.
enum JvsCmd : uint8_t {
    kCmdReset = 0xF0, kCmdSetAddress = 0xF1,
    kCmdIdentify = 0x10, kCmdCommandRev = 0x11, kCmdJvsVersion = 0x12, kCmdCommVersion = 0x13, kCmdFeatures = 0x14, kCmdMainId = 0x15,
    kCmdSwitches = 0x20, kCmdCoins = 0x21, kCmdAnalog = 0x22, kCmdRotary = 0x23, kCmdKeycode = 0x24, kCmdScreenPos = 0x25,
    kCmdMiscSwitches = 0x26, kCmdHopperStatus = 0x2E, kCmdRetransmit = 0x2F,
    kCmdCoinSubtract = 0x30, kCmdPayout = 0x31, kCmdGpOut1 = 0x32, kCmdAnalogOut = 0x33, kCmdCharOut = 0x34, kCmdCoinAdd = 0x35,
    kCmdPayoutSubtract = 0x36, kCmdGpOut2 = 0x37,
};

// Som van alle bytes modulo 256 (de checksum van JVS: node + lengte + gegevens).
uint8_t jvs_checksum(const uint8_t* p, size_t n);
inline uint8_t jvs_checksum(const std::vector<uint8_t>& v) { return jvs_checksum(v.data(), v.size()); }

// Zet 0xE0 en 0xD0 in de gegevens om (0xD0 0xDF en 0xD0 0xCF); zonder sync.
std::vector<uint8_t> jvs_escape(const std::vector<uint8_t>& raw);
// Een packet voor de draad: sync, daarna node, lengte, gegevens en checksum, met escapes. `data` is wat na de lengtebyte komt zonder
// checksum (bij een antwoord: status en rapporten).
std::vector<uint8_t> jvs_encode(uint8_t node, const std::vector<uint8_t>& data);

// Een ontvangen packet (ontstuft). `data` is zonder checksum.
struct JvsFrame {
    uint8_t node = 0;
    std::vector<uint8_t> data;
    bool checksum_ok = true;
    uint8_t checksum_wire = 0, checksum_calc = 0;
};

// Leest een bytestroom (in stukken) en geeft de packets. Een sync midden in een packet begint een nieuw packet (resync).
class JvsFrameParser {
public:
    // Voegt bytes toe en zet voltooide packets achter `out`.
    void feed(const uint8_t* p, size_t n, std::vector<JvsFrame>& out);
    uint64_t resyncs() const { return resyncs_; }       // een nieuw sync terwijl een packet nog niet af was
    uint64_t garbage() const { return garbage_; }       // bytes buiten een packet (voor de eerste sync)
    void reset();
private:
    enum class St { Idle, Node, Len, Body } st_ = St::Idle;
    bool esc_ = false;
    uint8_t node_ = 0, len_ = 0;
    std::vector<uint8_t> body_;
    uint64_t resyncs_ = 0, garbage_ = 0;
};

// ---- beschrijving van een bord (packaging/jvs-boards.json)

struct JvsAnalogChannel {
    std::string signal;         // as-signaal ("p1.x"); leeg = vaste waarde `idle`
    bool invert = false;        // fractie wordt 1 - fractie (de "~" van een niet-omgekeerde muisas)
    int min = 0, max = 255;     // kalibratievenster; als beide binnen 0..255 liggen wordt het met 257 vermenigvuldigd (8 -> 16 bit)
    double idle = 0.5;          // fractie als de speler geen geldige aim heeft of het kanaal geen signaal heeft
};

struct JvsSwitchBit {
    int player = 1;             // 0 = de systeembyte (test, tilt), 1.. = spelerbyte
    int byte = 0;               // byte binnen de spelerblok (0 = start, service, richtingen, knop 1 en 2; 1 = knop 3..)
    int bit = 0;                // 7 = hoogste bit
    std::string signal;         // knopsignaal ("p1.trigger", "test", "service")
};

struct JvsBoard {
    std::string name;
    std::string description;
    std::string identify = "SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10";
    int comm_version = 0x10, jvs_version = 0x30, command_rev = 0x13;
    int players = 2;            // functie 1: aantal spelers
    int buttons = 14;           // functie 1: knoppen per speler (bits); bytes per speler = ceil(buttons / 8) bij de eerste 8 + rest
    int coin_slots = 2;         // functie 2
    int analog_channels = 8;    // functie 3
    int analog_bits = 0;        // functie 3: resolutie, 0 = niet opgegeven
    int gp_outputs = 0;         // functie 0x12 (uitgangen voor algemeen gebruik, in bits; 0 = geen)
    std::vector<JvsAnalogChannel> analog;      // kanaal i = analog[i]; wat ontbreekt is vast op `idle` 0,5
    std::vector<JvsSwitchBit> switches;
    std::vector<std::string> coins;            // munt-signalen per sleuf (standaard coin1, coin2, ...)
    bool complete = true;
    // Namco-bord (NA-JV, "namco": true): het hoofdbord van een Namco-kast (ES3: Mario Kart Arcade GP DX) stuurt naast de gewone
    // commando's eigen commando's die geen JVS-standaard zijn. Zo'n bord leest ze in plaats van met status 2 te stoppen:
    //   0x70 <sub> ...  Namco-subcommando; lengte en antwoord per sub (zie namco_command_length in jvs.cpp), rapport 01;
    //   0x78..0x80      vaste lengte van 15 bytes (commando plus 14), rapport 01;
    //   elk ander onbekend commando: een byte overslaan zonder antwoord, en verder met de rest van het packet.
    // Feiten (lengtes en antwoordbytes) uit de beschrijving van de JVS-node van de open TeknoParrotUI-bron (NBGI_MarioKart3,
    // Namco = true); geen code overgenomen (docs/decisions.md).
    bool namco = false;
};

struct JvsBoards {
    std::vector<JvsBoard> boards;
    const JvsBoard* find(const std::string& name) const;
};

// Leest packaging/jvs-boards.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
JvsBoards parse_jvs_boards(const std::string& json_text, std::string& err);
std::string validate_jvs_board(const JvsBoard& b);   // leeg = geldig

// Aantal bytes in de spelerblok van functie 1 (cmd 0x20): knoppen / 8 naar boven afgerond, minstens 2 als er meer dan 8 knoppen zijn.
int jvs_switch_bytes(const JvsBoard& b);
// Zuiver: de 16-bit waarde van een analoog kanaal uit de toestand (08_gun_axis_math.md: venster, x257, begrenzen).
uint16_t jvs_analog_value(const JvsAnalogChannel& c, const IoState& s);
// Zuiver: de schakelbytes van cmd 0x20 voor `players` spelers van `bytes` bytes: eerst de systeembyte, dan per speler.
std::vector<uint8_t> jvs_switch_report(const JvsBoard& b, const IoState& s, int players, int bytes);

// Namco-commando (alleen voor een bord met `namco`): het aantal bytes dat het commando op d[pos] (de commandobyte zelf) inneemt,
// MET de commandobyte; 0 = geen Namco-commando (de gewone regels gelden). Zuiver, voor de tests.
size_t jvs_namco_command_length(const std::vector<uint8_t>& d, size_t pos);

// ---- een node (I/O-bord) en de bus

struct JvsNodeStats {
    uint64_t requests = 0;                      // packets voor deze node (inclusief broadcast)
    std::map<uint8_t, uint64_t> commands;       // aantal per commandocode
    uint64_t coin_edges = 0, unknown = 0;
    std::string main_id;                        // laatste 0x15 van het hoofdbord
    std::vector<uint8_t> gp_out;                // laatste uitgangen (cmd 0x32/0x37)
};

class JvsNode {
public:
    explicit JvsNode(JvsBoard b) : board_(std::move(b)) { coin_prev_.assign(board_.coins.size(), false); coin_count_.assign(static_cast<size_t>(std::max(0, board_.coin_slots)), 0); }
    const JvsBoard& board() const { return board_; }
    uint8_t address() const { return addr_; }          // 0 = nog geen adres
    void reset() { addr_ = 0; }
    // Nieuwe toestand van de spelers: telt muntflanken en bewaart de momentopname voor de antwoorden.
    void set_input(const IoState& s);
    const IoState& input() const { return in_; }
    // Verwerkt de commando's van een packet (gegevens zonder checksum) voor dit adres; geeft de gegevens van het antwoord (status en
    // rapporten) of lege vector als er niet geantwoord wordt (reset, adresseren van een ander bord).
    // `can_take_address`: de sense-lijn staat bij dit bord (alle voorgaande nodes hebben een adres).
    std::vector<uint8_t> handle(const std::vector<uint8_t>& cmds, bool broadcast, bool can_take_address);
    const JvsNodeStats& stats() const { return stats_; }
    uint16_t coin_count(size_t slot) const { return slot < coin_count_.size() ? coin_count_[slot] : 0; }
private:
    JvsBoard board_;
    uint8_t addr_ = 0;
    IoState in_;
    std::vector<bool> coin_prev_;
    std::vector<uint16_t> coin_count_;
    JvsNodeStats stats_;
};

struct JvsBusStats {
    uint64_t frames = 0, bad_sum = 0, unanswered = 0, resets = 0, answers = 0;
    uint8_t last_cmd = 0;
};

class JvsBus {
public:
    // Voegt een node toe aan het einde van de keten (de eerste node krijgt als eerste een adres).
    void add_node(JvsBoard b) { nodes_.push_back(JvsNode(std::move(b))); }
    size_t node_count() const { return nodes_.size(); }
    JvsNode& node(size_t i) { return nodes_[i]; }
    const JvsNode& node(size_t i) const { return nodes_[i]; }
    void set_input(const IoState& s) { for (auto& n : nodes_) n.set_input(s); }
    // Verwerkt een ontvangen packet; geeft de bytes voor de draad (met sync en escapes) of lege vector (geen antwoord).
    std::vector<uint8_t> process(const JvsFrame& f);
    // Is er al een node geadresseerd? (De "sense"-lijn: de game leest dat via de modemstatus.)
    bool any_addressed() const;
    bool all_addressed() const;
    // Bits van de modemstatus zoals een game ze via GetCommModemStatus leest: CTS 0x10 en DSR 0x20 (geadresseerd: beide, anders alleen CTS).
    uint8_t modem_status() const { return any_addressed() ? 0x30 : 0x10; }
    const JvsBusStats& stats() const { return stats_; }
private:
    std::vector<JvsNode> nodes_;
    JvsBusStats stats_;
    std::vector<uint8_t> last_wire_;   // voor cmd 0x2F (herhaal het laatste antwoord)
};

}  // namespace ravengun::iobridge
