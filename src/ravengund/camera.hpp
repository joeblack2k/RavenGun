#pragma once
// Camera-acquisitie (V4L2, mmap) en boltracking in ravengund.
//
// Twee threads, los van de HID-thread:
//  - capture: enige eigenaar van het V4L2-apparaat. Haalt elk klaarstaand frame op (DQBUF),
//    noteert timestamp/volgnummer, kopieert de inhoud naar een framebuffer in RAM, geeft de
//    driverbuffer meteen terug en zet het frame in een begrensde wachtrij (capaciteit 2, het
//    oudste valt weg).
//  - tracker: neemt altijd het nieuwste frame, draait de boltracker en publiceert de optische
//    meting. Frames worden nooit naar schijf geschreven.
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ravengun/vision/ball_tracker.hpp"
#include "shared.hpp"

namespace ravengund {

struct CameraOptions {
    bool enabled = true;
    std::string device;        // leeg = automatisch: de enige ov534-camera onder /dev/v4l/by-id
    int width = 640, height = 480, fps = 60;
    int queue_capacity = 2;
    bool auto_exposure = false;  // standaard handmatig: AE, AGC en AWB uit (zie docs/decisions.md)
    bool auto_gain = false;
    bool auto_awb = false;
    int exposure = 50;         // ov534: 0..255 (zie docs/decisions.md); alleen bij auto_exposure uit
    int gain = 0;              // ov534: 0..63; alleen bij auto_gain uit
    ravengun::vision::TrackerConfig tracker{};
    int scene_every_frames = 30;  // scènestatistiek elke N verwerkte frames
};

// Een live wijziging (set_camera). Niet-gezette velden blijven zoals ze zijn.
struct CameraChange {
    std::optional<int> exposure, gain;
    std::optional<bool> auto_exposure, auto_gain, auto_awb;
    std::optional<int> width, height, fps;  // alle drie of geen: een toegestane modus (herstart de capture)
};

// Wat de driver na het zetten daadwerkelijk meldt.
struct CameraControls {
    bool readable = false;
    int exposure = 0, gain = 0;
    bool auto_exposure = false, auto_gain = false, auto_awb = false;
};

struct CameraApplyResult {
    bool ok = false;
    std::string code;     // unavailable (geen camera / niet actief), internal
    std::string message;  // Nederlands
    bool restarted = false;
    int width = 0, height = 0, fps = 0;
    double driver_fps = 0;
    CameraControls controls;  // teruggelezen waarden
    std::vector<std::string> warnings;  // bijv. een waarde die de driver niet overnam
};

class CameraService {
public:
    CameraService(SharedState& shared, CameraOptions opt);
    ~CameraService();
    void start();
    void stop();
    std::string status_json() const;

    // Past instellingen live toe in de capture-thread en leest de werkelijke waarden terug. Een
    // moduswissel herstart de capture (en bij een fout wordt de oude modus hersteld). Blokkeert
    // hoogstens ~5 s. Zonder camera (uit of niet actief) volgt een nette foutmelding.
    CameraApplyResult apply(const CameraChange& change);
    // Nieuwe tint/tolerantie/min_chroma voor de tracker (bij een andere LED-kleur); werkt direct. Zonder speler: speler 1.
    // Er draait een tracker per speler op hetzelfde beeld, elk op de tint van de bol van die speler (docs/decisions.md).
    void set_tracker(const ravengun::vision::TrackerConfig& cfg) { set_tracker(1, cfg); }
    void set_tracker(int player, const ravengun::vision::TrackerConfig& cfg);
    ravengun::vision::TrackerConfig tracker(int player = 1) const;
    // De tracker van speler 2..4 zoekt alleen als die speler een Move heeft (speler 1 zoekt altijd).
    void set_tracker_enabled(int player, bool on);
    bool enabled() const;
    // De virtuele camera voor een spel (src/ravengun/vision/camera_feed.hpp): aan = de tracker-thread schrijft elk beeld ook in
    // `path` (standaard /dev/shm/ravengun_Camera), zodat een spel dat de camera IN het spel gebruikt ([Camera] Use = game) het
    // beeld krijgt zonder het apparaat zelf te openen (de daemon blijft de enige eigenaar; de tracking loopt door). Uit = het
    // bestand weg: het spel ziet geen camera. `why` komt in het logboek.
    void set_game_feed(bool on, const std::string& why, const std::string& path = {});
    bool game_feed() const;
    // Het apparaat dat de daemon gebruikt (zoals gevonden bij het openen; leeg zolang er geen camera open is).
    std::string device_path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace ravengund
