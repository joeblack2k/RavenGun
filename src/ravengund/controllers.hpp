#pragma once
// Controllers in ravengund: de lijst van bekende Moves en Navigation Controllers (config.json + BlueZ + wat er nu verbonden
// is), de koppelwizard (ravengun/pairing/pairing.hpp) en het verwijderen. Eigen thread voor de wizard en het verversen van de
// BlueZ-lijst; de HID-thread wordt alleen via een opdracht (reconfigure) op de hoogte gebracht.
//
// Systeemtoegang zit achter ControllerOps (tests gebruiken een nep): sysfs (alleen lezen), hidraw-feature-rapporten van een
// controller aan USB (master-adres), BlueZ via busctl (lijst, RemoveDevice; geen root nodig) en de hulp met rootrechten via
// `sudo -n` met een vaste argumentenlijst (execv, geen shell).
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/pairing/pairing.hpp"
#include "ravengun/sysinfo/hidraw_scan.hpp"
#include "ravengun/sysinfo/navcon.hpp"
#include "shared.hpp"

namespace ravengund {

struct BluezDevice {
    std::string address;   // kleine letters
    std::string name;
    std::string type;      // move | navigation (uit Modalias 054c:03d5/0c5e/042f)
    std::string model;     // ZCM1 | ZCM2 | ""
    bool paired = false, connected = false;
    std::string path;      // D-Bus-pad (/org/bluez/hci0/dev_...)
};

class ControllerOps {
public:
    virtual ~ControllerOps() = default;
    virtual std::vector<ravengun::sysinfo::HidrawDevice> scan() = 0;
    // Sony-controllers die BlueZ kent, plus het adres van de adapter. false bij een fout (err).
    virtual bool bluez_list(std::vector<BluezDevice>& out, std::string& adapter, std::string& err) = 0;
    virtual bool bluez_remove(const std::string& address, std::string& err) = 0;
    // Is de hulp geïnstalleerd en mag hij zonder wachtwoord (sudo -n -l)?
    virtual bool helper_available() = 0;
    // Voert de hulp uit (argv uit pairing::helper_argv); exitcode, uitvoer in `output`.
    virtual int run_helper(const std::vector<std::string>& argv, std::string& output) = 0;
    // Feature-rapporten via USB-hidraw: het eigen adres en het huidige master-adres lezen, het master-adres zetten.
    virtual bool read_usb(const ravengun::sysinfo::HidrawDevice& d, std::string& own, std::string& master, std::string& err) = 0;
    virtual bool set_master(const ravengun::sysinfo::HidrawDevice& d, const std::string& host, std::string& err) = 0;
    // Batterij uit /sys/class/power_supply (sony_controller_battery_<adres>); connected=false als die er niet is.
    virtual ravengun::sysinfo::NavconStatus battery(const std::string& address) = 0;
    // Bestand verwijderen (kalibratie en bias van een verwijderde controller).
    virtual void remove_file(const std::string& path) = 0;
};
std::unique_ptr<ControllerOps> make_system_controller_ops();
// Nep-systeem uit JSON (ravengund --controllers-fake, tests en schermafdrukken); nullptr bij een fout (err).
std::unique_ptr<ControllerOps> make_fake_controller_ops(const std::string& json_text, std::string& err);

class ControllerService {
public:
    ControllerService(SharedState& sh, std::unique_ptr<ControllerOps> ops);
    ~ControllerService();
    void start();
    void stop();

    // De lijst (JSON-object met "controllers", "adapter", "helper", "pairing"). full_ids: volledige adressen.
    std::string list_json(bool full_ids);
    // Wizard: start | status | confirm | cancel. Resultaat: de toestand (JSON); bij een fout code en melding.
    std::string pair(const std::string& action, std::string& code, std::string& msg);
    // Ontkoppelen bij BlueZ (D-Bus; terugval de hulp) en uit de lijst halen (keep: instellingen bewaren).
    std::string unpair(const std::string& address, bool keep, std::string& code, std::string& msg);
    // Alleen voor tests: één ronde van de wizard nu (zonder thread).
    void tick_for_test() { pair_round(); }
    // Ververst de BlueZ-lijst nu (anders elke 10 s, en elke 2 s tijdens het koppelen).
    void refresh();
    // Gekoppelde controllers uit BlueZ die nog niet in config.json staan, daar opnemen (standaard aan; uit met --no-hid).
    void set_auto_register(bool on) { auto_register_ = on; }

private:
    void main_loop();
    void pair_round();
    bool refresh_bluez_locked(std::unique_lock<std::mutex>& lk);
    void auto_register(const std::vector<BluezDevice>& devs);
    std::string pair_json_locked() const;
    void post_reconfigure();

    SharedState& sh_;
    std::unique_ptr<ControllerOps> ops_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread thread_;
    bool stop_ = false;
    ravengun::pairing::Machine machine_;
    std::vector<BluezDevice> bluez_;
    std::string adapter_, bluez_error_;
    int64_t bluez_at_ = 0;
    bool helper_ok_ = false;
    int64_t helper_at_ = 0;
    // Gelezen adressen per USB-node (een feature-rapport per nieuw aangesloten controller, niet elke ronde).
    struct UsbRead { std::string own, master, err; };
    std::vector<std::pair<std::string, UsbRead>> usb_reads_;
    bool auto_register_ = true;
    std::string registered_;   // adres dat de wizard net heeft opgenomen
    int registered_player_ = 0;
};

}  // namespace ravengund
