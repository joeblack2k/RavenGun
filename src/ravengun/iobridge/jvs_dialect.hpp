#pragma once
// Dialect "jvs" van de IO-brug: de daemon is het JVS-bord (of de keten van borden) aan het eind van een seriële lijn. De game
// opent een COM-poort (Let's Go Safari: COM4), schrijft JVS-packets en leest de antwoorden. Transport: een pty die de daemon
// aanmaakt; de slave staat via een vaste symlink ($XDG_RUNTIME_DIR/ravengun/jvs-com) in de Wine-prefix als dosdevices/com4, zodat de
// seriële emulatie van Wine de comm-API's doet. Wine vraagt de modemstatus (TIOCMGET) en een pty kent die niet; de kleine
// LD_PRELOAD-hulp tools/wine-comm-shim/ laat de ioctl voor een pty slagen met de status die dit dialect in een bestand zet
// (kModemFile: een byte met de Windows-bits CTS 0x10 en DSR 0x20; geadresseerd = beide, anders alleen CTS).
//
// Een eigen thread leest de lijn (poll) en antwoordt meteen; write(IoState) ververst de toestand (250 Hz) waaruit de antwoorden komen.
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/iobridge/dialect.hpp"
#include "ravengun/iobridge/jvs.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kJvsModemFile = "/dev/shm/ravengun_jvs_modem";

// De lijn: een pty (productie) of iets anders (tests).
class JvsTransport {
public:
    virtual ~JvsTransport() = default;
    virtual bool open(std::string& err) = 0;
    virtual int fd() const = 0;                          // om op te pollen
    virtual long read(uint8_t* p, size_t n) = 0;         // niet-blokkerend: bytes, 0 niets, <0 fout
    virtual bool write(const uint8_t* p, size_t n) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual std::string path() const { return {}; }      // bv. /dev/pts/12
};

// Maakt een pty (raw, 115200 8N1), houdt een slave-descriptor vast en zet een symlink `link` naar het slave-pad (leeg = geen link).
std::unique_ptr<JvsTransport> make_pty_transport(const std::string& link);

struct JvsStats {
    uint64_t rx_bytes = 0, tx_bytes = 0, frames = 0, writes = 0, write_errors = 0;
    uint64_t switch_polls = 0, analog_polls = 0, identifies = 0;
    int64_t first_frame_ms = 0, last_frame_ms = 0;
    std::string last_error;
};

class JvsDialect final : public IoDialect {
public:
    JvsDialect(std::vector<JvsBoard> boards, std::string game, std::unique_ptr<JvsTransport> t, std::string modem_file = kJvsModemFile);
    ~JvsDialect() override { close(); }
    const char* name() const override { return "jvs"; }
    std::string game() const override { return game_; }
    bool open(std::string& err) override;
    void write(const IoState& s) override;
    void neutral() override;
    void close() override;
    std::string status_json() const override;
    // Meldt elk nieuw soort packet (voor het daemon-logboek); wordt vanuit de JVS-thread aangeroepen.
    void set_trace(std::function<void(const std::string&)> f) { trace_ = std::move(f); }
    // Voor tests: één ronde lezen en antwoorden zonder thread (wacht maximaal `timeout_ms` op bytes).
    int pump(int timeout_ms);
    JvsBusStats bus_stats() const;
    JvsStats stats() const;
    uint8_t modem_status() const;
    std::vector<uint8_t> node_addresses() const;

private:
    void loop();
    void handle_bytes(const uint8_t* p, size_t n);
    void publish_modem(uint8_t v);
    std::string game_;
    std::unique_ptr<JvsTransport> t_;
    std::string modem_file_;
    mutable std::mutex m_;
    JvsBus bus_;
    JvsFrameParser parser_;
    JvsStats st_;
    uint8_t modem_published_ = 0xFF;
    std::deque<std::string> recent_;     // laatste packets als tekst ("ms  <- E0 ..  -> E0 ..")
    std::set<std::string> seen_;         // soorten packets die al gemeld zijn
    std::function<void(const std::string&)> trace_;
    std::atomic<bool> quit_{false};
    std::thread th_;
    bool opened_ = false;
};

}  // namespace ravengun::iobridge
