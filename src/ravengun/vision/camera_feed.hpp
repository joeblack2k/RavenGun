#pragma once
// De virtuele camera voor een spel, Linux-kant: het camerabeeld van de daemon in een bestand in /dev/shm zetten (de indeling staat in
// camera_feed_format.h), zodat een bibliotheek in het spelproces het als camera kan aanbieden. Onder Wine: tools/rg-mfcam (Media
// Foundation; Wine zelf heeft geen videocamera's: MFEnumDeviceSources geeft er 0). De daemon blijft de enige eigenaar van het
// apparaat; het spel opent geen /dev/video.
//
// Ook hier: welke game de camera krijgt ([Camera] in game.ini, zoals scripts/camera.py) en de keuze in de app ([Games] camera).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/vision/camera_feed_format.h"

namespace ravengun::vision {

inline constexpr const char* kCameraFeedPath = RG_CAMFEED_PATH_LINUX;

// Schrijft beelden in het bestand (mmap, seqlock). Niet thread-safe: een schrijver.
class CameraFeedWriter {
public:
    CameraFeedWriter() = default;
    ~CameraFeedWriter() { close(false); }
    CameraFeedWriter(const CameraFeedWriter&) = delete;
    CameraFeedWriter& operator=(const CameraFeedWriter&) = delete;
    // Maakt of hergebruikt het bestand voor beelden van deze vorm (YUY2). Een andere vorm dan de vorige: opnieuw aangelegd.
    bool open(const std::string& path, uint32_t width, uint32_t height, uint32_t stride, uint32_t fps_num, uint32_t fps_den, std::string& err);
    bool is_open() const { return map_ != nullptr; }
    // Een beeld van height x stride bytes (de vorm van open()). t_ms: tijdstempel voor de kop.
    void write(const uint8_t* frame, uint32_t t_ms);
    // Dicht; unlink = het bestand ook weghalen (dan ziet het spel geen camera meer).
    void close(bool unlink);
    uint32_t frames() const { return frames_; }
    const std::string& path() const { return path_; }
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
private:
    std::string path_;
    uint8_t* map_ = nullptr;
    size_t len_ = 0;
    uint32_t w_ = 0, h_ = 0, stride_ = 0, seq_ = 0, frames_ = 0;
};

// Leest een beeld uit een kopie of mapping van het bestand (zuiver, voor de tests en als voorbeeld voor de lezer in het spelproces).
// `seq_before`/`seq_after`: de seq-waarde voor en na het kopieren; gelijk en even = een heel beeld.
struct CameraFeedFrame {
    rg_camfeed_header head{};
    std::vector<uint8_t> data;
};
bool camera_feed_read(const uint8_t* file, size_t len, CameraFeedFrame& out);

// [Camera] van een game: welke rol en welk apparaat (de regels van scripts/camera.py, kies()).
struct CameraChoice {
    std::string role;      // game | presence | setup | none
    std::string device;    // "" = geen camera, "auto", of /dev/videoN
    std::string why;       // Nederlands, voor het logboek
    bool error = false;    // een instelling die niet klopt (onbekende rol of apparaat)
};
// `use`, `device`: [Camera] Use en Device uit game.ini (gebruikerslaag eroverheen); `app`: [Games] camera uit ravengun-app.conf.
CameraChoice camera_choice(const std::string& use, const std::string& device, const std::string& app);
// [Games] camera uit de tekst van ravengun-app.conf ("auto" zonder sleutel).
std::string app_camera_setting(const std::string& conf_text);

}  // namespace ravengun::vision
