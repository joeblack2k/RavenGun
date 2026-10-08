#pragma once
// Bewaarde gyro-bias per controller (eigen toestand "restored" in de biasschatter).
//
// Bestand: <map>/<controller_key>.json (standaard ~/.local/state/ravengun/gyro-bias/), geschreven met
// atomic_write_file. De map is instelbaar (ravengund --bias-dir, of uit: --no-bias-store); de tests en
// ravengun-analyze schrijven nooit naar de echte state-map.
//
// Bewaarbeleid (BiasSaver, pure logica, geen threads): alleen een eigen meting (stilstand of quasi-stil, geen kale
// herstelde waarde), de eerste keer 20 s na het verbinden of zodra de bias ten minste 3 s is gemeten, daarna bij een
// verschil van 0,15 graden/s (niet vaker dan elke 30 s) of van 0,03 graden/s (niet vaker dan elke 300 s), en bij het
// afsluiten of verbreken als er iets veranderd is.
#include <cstdint>
#include <string>

#include "ravengun/core/fusion.hpp"
#include "ravengun/core/math.hpp"
#include "ravengun/core/pipeline.hpp"

namespace ravengun::core {

inline constexpr int kBiasFileVersion = 1;

struct SavedBias {
    Vec3 bias{};               // rad/s, controllerstelsel
    int tc = -1;               // ruwe temperatuur op het moment van bewaren; -1 onbekend
    int64_t unix_s = 0;        // wandklok bij het bewaren
    std::string key;           // controller_key
    std::string source;        // "still" of "quasi-still"
};

std::string default_bias_dir();   // $XDG_STATE_HOME/ravengun/gyro-bias; leeg zonder HOME
std::string bias_to_json(const SavedBias& b);
bool bias_from_json(const std::string& text, SavedBias& out, std::string& err);
bool save_bias(const std::string& dir, const SavedBias& b, std::string& err);
bool load_bias(const std::string& dir, const std::string& key, SavedBias& out, std::string& err);

// Wanneer de huidige bias bewaard moet worden. Elke aanroep is goedkoop; schrijft zelf niets.
class BiasSaver {
public:
    // now_s: monotone seconden (willekeurige oorsprong). Geeft true als er nu bewaard moet worden.
    bool due(const GyroBiasEstimator& e, double now_s) const;
    // Na een geslaagde schrijfactie.
    void saved(const GyroBiasEstimator& e, double now_s) { last_ = e.bias(); last_t_ = now_s; have_ = true; }
    // Bij het verbinden: nieuwe sessie (de laatst bewaarde waarde blijft de vergelijkingswaarde).
    void connected(double now_s) { connect_t_ = now_s; }
    // Afsluiten of verbreken: is er iets veranderd dat de moeite waard is?
    bool due_final(const GyroBiasEstimator& e) const;
    bool have_saved() const { return have_; }
    Vec3 last_saved() const { return last_; }

private:
    static bool eligible(const GyroBiasEstimator& e);
    Vec3 last_{};
    double last_t_ = 0, connect_t_ = 0;
    bool have_ = false;
};

// Laden bij het verbinden en bewaren tijdens het spelen, voor de daemon (HidAux) en getest zonder hardware.
// Leeg `dir` = uit. Alle methoden draaien in de HID-thread.
class BiasPersistence {
public:
    void set_dir(std::string d) { dir_ = std::move(d); }
    const std::string& dir() const { return dir_; }
    bool enabled() const { return !dir_.empty(); }
    // Na Pipeline::on_connect(): laadt het bestand van deze controller en herstelt het als de pijplijn nog niets
    // gemeten heeft. true als er hersteld is; `info` krijgt dan een logregel (en bij een mislukte lading de reden).
    bool on_connect(Pipeline& p, const std::string& controller_id, double now_s, int64_t unix_s, std::string* info = nullptr);
    // Per rapport (goedkoop). true als er net geschreven is (of het schrijven mislukte: zie error()).
    bool on_report(const Pipeline& p, double now_s, int64_t unix_s, std::string* info = nullptr);
    // Verbroken of afsluiten: nog bewaren als er iets veranderd is. true als er geschreven is.
    bool on_disconnect(const Pipeline& p, double now_s, int64_t unix_s, std::string* info = nullptr);

    bool loaded() const { return loaded_; }
    const SavedBias& loaded_value() const { return loaded_val_; }
    uint64_t saves() const { return saves_; }
    int64_t last_saved_unix() const { return last_saved_unix_; }
    const std::string& error() const { return err_; }

private:
    bool save(const Pipeline& p, double now_s, int64_t unix_s, const char* why, std::string* info);
    std::string dir_, key_, err_;
    BiasSaver saver_;
    bool loaded_ = false;
    SavedBias loaded_val_;
    uint64_t saves_ = 0;
    int64_t last_saved_unix_ = 0;
};

}  // namespace ravengun::core
