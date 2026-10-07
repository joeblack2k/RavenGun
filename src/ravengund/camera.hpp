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
#include <string>

#include "ravengun/vision/ball_tracker.hpp"
#include "shared.hpp"

namespace ravengund {

struct CameraOptions {
    bool enabled = true;
    std::string device;        // leeg = automatisch: de enige ov534-camera onder /dev/v4l/by-id
    int width = 640, height = 480, fps = 60;
    int queue_capacity = 2;
    bool manual = true;        // auto-exposure, auto-gain en AWB uitzetten
    int exposure = 50;         // ov534: 0..255 (zie docs/decisions.md)
    int gain = 0;              // ov534: 0..63
    ravengun::vision::TrackerConfig tracker{};
    int scene_every_frames = 30;  // scènestatistiek elke N verwerkte frames
};

class CameraService {
public:
    CameraService(SharedState& shared, CameraOptions opt);
    ~CameraService();
    void start();
    void stop();
    std::string status_json() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace ravengund
