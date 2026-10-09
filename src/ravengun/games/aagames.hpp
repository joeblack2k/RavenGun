#pragma once
// Unity-games zonder TeknoParrot (Tomb Raider Arcade, Rabbids Hollywood Arcade): de BepInEx-plugin van DemulShooter
// draait in de game en luistert als TCP-server op 127.0.0.1:33610; ravengund neemt de rol van DemulShooterX64.exe over en
// is de client. Dit bestand bevat het protocol (clean-room: alleen de feiten over de bytes, geen code van DemulShooter),
// het lezen van packaging/aagames-games.json en de verbindingsklant met herverbinden. Onderzoek: docs/research/
// demulshooter-integratie.md; de geverifieerde bytevolgorde staat in docs/decisions.md.
//
// Protocol (alles little-endian, geen padding):
//  - Client naar plugin (invoer): per bericht precies een pakket van vaste lengte, zonder lengte of header. Velden op
//    alfabetische volgorde van de veldnaam; arrays hebben vier elementen (speler 1..4):
//      Axis_X float[4], Axis_Y float[4]   schermpixels van het gamevenster (Y-oorsprong: zie AaGame::y_origin)
//      EnableInputsHack u8, HideCrosshairs u8
//      Reload u8[4] (alleen Tomb Raider), Trigger u8[4]          1 = ingedrukt
//    Tomb Raider 42 bytes, Rabbids Hollywood 38 bytes (geen Reload).
//  - Plugin naar client (uitvoer): int32 lengte (= payload + 1), u8 koptype (2 = uitvoer), payload van 60 bytes:
//      Tomb Raider: Ammo i32[4], Credits i32[4], Damaged u8[4], IsPlaying u8[4], Life f32[4], Recoil u8[4]
//      Rabbids:     Ammo i32[4], Credits i32[4], Damaged u8[4], Life f32[4], Recoil u8[4], StartLamp u8[4]
//      (alfabetisch op veldnaam; bij Rabbids staat StartLamp dus achteraan; de plugin meldt de veldvolgorde zelf in
//      BepInEx/LogOutput.log). Recoil en Damaged zijn eenmalige vlaggen: elk bericht met Recoil[p] != 0 is een schot.
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ravengun::games {

inline constexpr int kAaPlayers = 4;
inline constexpr int kAaDefaultPort = 33610;

enum class AaType { TombRaider, RabbidsHollywood };
const char* to_string(AaType t);  // "tomb_raider", "rabbids_hollywood"

struct AaGame {
    std::string key;           // TombRaider, RabbidsHollywood
    std::string name;
    AaType type = AaType::TombRaider;
    std::string nas_dir;
    std::string exe = "Game.exe";
    std::string plugin_dir;
    int width = 1920, height = 1080;        // resolutie van het gamevenster (pixels)
    double offset_x = 0, offset_y = 0;      // kalibratie: verschuiving in schermbreedte/-hoogte, na de schaal
    double scale_x = 1, scale_y = 1;        // kalibratie: versterking rond het midden
    std::string y_origin = "bottom";        // "bottom": Y=0 onderaan (Unity-conventie); "top": Y=0 bovenaan
    int send_hz = 120;                      // hoe vaak het invoerpakket gaat (1..1000)
};

struct AaConfig {
    int port = kAaDefaultPort;
    std::string demulshooter_version;
    std::vector<AaGame> games;
    const AaGame* find(const std::string& key) const;
};

// Leest packaging/aagames-games.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
AaConfig parse_aagames_config(const std::string& json_text, std::string& err);
// Leeg = geldig.
std::string validate_aagames_game(const AaGame& g);

// Invoer van een speler zoals de klant hem nodig heeft: aim als schermfractie (0,0 = linksboven, 1,1 = rechtsonder).
struct AaPlayerInput {
    bool active = false;       // speler heeft een Move met geldige aim
    double u = 0.5, v = 0.5;
    bool trigger = false, reload = false;
};
using AaInputs = std::array<AaPlayerInput, kAaPlayers>;

struct AaPixel { float x = 0, y = 0; };
// Fractie naar pixels van het gamevenster: eerst de kalibratie (schaal rond het midden, offset), begrensd op [0,1], dan
// de resolutie en de Y-oorsprong. De rechterrand is width-1.
AaPixel to_pixels(const AaGame& g, double u, double v);

size_t aa_input_size(AaType t);  // 42 of 38
std::vector<uint8_t> build_input_packet(const AaGame& g, const AaInputs& in, bool hide_crosshairs = false);

// Uitvoer van de plugin.
struct AaOutputs {
    int32_t ammo[kAaPlayers] = {};
    int32_t credits[kAaPlayers] = {};
    uint8_t damaged[kAaPlayers] = {};
    uint8_t playing[kAaPlayers] = {};   // IsPlaying (Tomb Raider) of StartLamp (Rabbids Hollywood: de startknop van die speler brandt)
    float life[kAaPlayers] = {};
    uint8_t recoil[kAaPlayers] = {};
};
inline constexpr size_t kAaOutputPayload = 60;

// Leest de stroom van de plugin: berichten met een lengteprefix, ook als ze in stukken binnenkomen.
class AaFrameParser {
public:
    explicit AaFrameParser(AaType t = AaType::TombRaider) : type_(t) {}
    void feed(const uint8_t* data, size_t n);
    // Volgend volledig uitvoerbericht; false als er (nog) geen is.
    bool next(AaOutputs& out);
    bool broken() const { return broken_; }   // onmogelijke lengte: de stroom is niet meer te volgen
    uint64_t frames() const { return frames_; }
    uint64_t skipped() const { return skipped_; }   // berichten met een ander koptype of een te korte inhoud
    void reset();

private:
    AaType type_;
    std::vector<uint8_t> buf_;
    bool broken_ = false;
    uint64_t frames_ = 0, skipped_ = 0;
};

struct AaClientStats {
    bool connected = false;
    uint64_t connect_attempts = 0, connects = 0, disconnects = 0;
    uint64_t packets_sent = 0, bytes_sent = 0, send_dropped = 0;
    double packets_per_s = 0;
    uint64_t output_frames = 0, output_skipped = 0;
    uint64_t recoils[kAaPlayers] = {};
    AaOutputs last{};
    bool have_outputs = false;
    std::string last_error;
    int64_t connected_since_ms = 0;
    int64_t last_disconnect_ms = 0;   // steady-klok in ms; 0 = nog nooit verbonden geweest
    int64_t started_ms = 0;           // start van de klant (zelfde klok)
};

class AaClient {
public:
    using StateFn = std::function<void(AaInputs&)>;          // vult de invoer per speler; wordt in de klantthread aangeroepen
    using RecoilFn = std::function<void(int player)>;        // speler 1..4: een schot (Recoil-vlag)
    AaClient() = default;
    ~AaClient() { stop(); }
    AaClient(const AaClient&) = delete;
    AaClient& operator=(const AaClient&) = delete;

    // Start de klantthread: verbindt met host:port (herhaald tot de game luistert) en stuurt send_hz keer per seconde het
    // invoerpakket. Opnieuw verbinden na elk verlies (reconnect_ms).
    void start(const AaGame& g, int port, StateFn state, RecoilFn recoil = {}, const std::string& host = "127.0.0.1",
               int reconnect_ms = 1000);
    void stop();
    bool running() const { return running_.load(); }
    AaClientStats stats() const;
    const AaGame& game() const { return game_; }
    std::string status_json() const;
    static int64_t clock_ms();        // de klok van connected_since_ms / last_disconnect_ms

private:
    void run();
    bool connect_once();
    void close_socket();

    AaGame game_;
    int port_ = kAaDefaultPort;
    std::string host_;
    int reconnect_ms_ = 1000;
    StateFn state_;
    RecoilFn recoil_;
    std::thread th_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    int fd_ = -1;
    mutable std::mutex m_;
    AaClientStats st_;
    std::vector<int64_t> sent_ms_;   // tijden van recente pakketten (voor pakketten per seconde)
};

}  // namespace ravengun::games
