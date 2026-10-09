#pragma once
// Koppelen van een PS Move of Navigation Controller aan deze pc, als pure logica (geen hardware, getest in tests/test_pairing.cpp).
//
// Stappen (docs/architecture.md, "Koppelen en verwijderen"):
//  1. De gebruiker sluit de controller via USB aan. De daemon ziet hem in sysfs (hidraw, bus USB, 054c:03d5/0c5e/042f).
//  2. Het master-adres in de controller wordt het adres van de Bluetooth-adapter van deze pc. Dat kan als gewone gebruiker
//     (hidraw, HIDIOCSFEATURE; de udev-regels van SteamOS geven Sony-hidraw uaccess):
//       Move:       feature 0x05, [0x05, adapter-adres omgekeerd (laagste byte eerst), nullen] (23 bytes; PSMoveAPI
//                   _psmove_set_btaddr). Lezen: feature 0x04 (16 bytes): bytes 1..6 eigen adres, 10..15 master (beide omgekeerd).
//       Navigation: feature 0xF5, [0xF5, 0x01, adapter-adres in leesvolgorde] (8 bytes; BlueZ plugins/sixaxis.c: baswap van
//                   bdaddr_t, dus de hoogste byte eerst). Lezen: 0xF5 bytes 2..7 (master), 0xF2 bytes 4..9 (eigen adres).
//  3. De registratie bij BlueZ (info-bestand met Trusted, Class, DeviceID en een koppelsleutel, plus de SDP-cache) vraagt
//     root: dat doet de hulp scripts/ravengun-bt-helper via sudo (zie docs/decisions.md).
//  4. "Haal de kabel eruit en druk op PS": de wizard wacht tot de controller via Bluetooth verbindt.
// Een controller die al bij BlueZ bekend is en al naar deze pc wijst, geeft eerst de vraag "al gekoppeld; opnieuw koppelen?".
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ravengun::pairing {

// ---- Feature-rapporten (adressen als "aa:bb:cc:dd:ee:ff", kleine letters) ----
inline constexpr unsigned char kMoveGetAddr = 0x04, kMoveSetMaster = 0x05;
inline constexpr size_t kMoveGetSize = 16, kMoveSetSize = 23;
inline constexpr unsigned char kNavOwnAddr = 0xF2, kNavMaster = 0xF5;
inline constexpr size_t kNavOwnSize = 18, kNavMasterSize = 8;

bool parse_address(const std::string& a, uint8_t out[6]);   // leesvolgorde (hoogste byte eerst)
std::string format_address(const uint8_t b[6]);              // kleine letters
std::vector<uint8_t> encode_move_set_master(const std::string& host);  // leeg bij een ongeldig adres
bool decode_move_addresses(const uint8_t* buf, size_t n, std::string& own, std::string& master);
std::vector<uint8_t> encode_nav_set_master(const std::string& host);
bool decode_nav_master(const uint8_t* buf, size_t n, std::string& master);
bool decode_nav_own(const uint8_t* buf, size_t n, std::string& own);

// ---- De hulp met rootrechten ----
inline constexpr const char* kBtHelperPath = "/opt/ravengun/bin/ravengun-bt-helper";
inline constexpr const char* kBtHelperInstall = "sudo ~/projects/ravengun/scripts/install-bt-helper.sh --apply";
// Type voor de hulp: "move" (ZCM1), "move2" (ZCM2) of "navigation".
std::string helper_type(const std::string& type, const std::string& model);
// De volledige argumentenlijst voor execv: {"/usr/bin/sudo", "-n", <hulp>, "register", "AA:BB:..", <type>}. Leeg als het adres of
// het type niet klopt (dezelfde controle als in de hulp zelf: hoofdletters, een vaste lijst typen, geen nul- of broadcastadres).
std::vector<std::string> helper_argv(const std::string& action, const std::string& address, const std::string& helper_type,
                                     const std::string& helper_path = kBtHelperPath);

// ---- Toestandsmachine van de wizard ----
enum class State {
    Idle,           // niets aan de hand
    WaitUsb,        // wacht tot er een Move of Navigation Controller via USB aan hangt
    AlreadyPaired,  // die controller is al gekoppeld: opnieuw koppelen? (confirm) of klaar (cancel)
    SetMaster,      // het master-adres wordt gezet
    NeedHelper,     // de hulp met rootrechten ontbreekt (installatiecommando tonen); confirm = opnieuw proberen
    Register,       // registratie bij BlueZ via de hulp (Bluetooth herstart even)
    WaitUnplug,     // "haal de kabel eruit"
    WaitBluetooth,  // "druk op PS": wacht op de Bluetooth-verbinding
    Done,
    Failed,
    Cancelled,
};
const char* to_string(State s);

struct UsbCandidate {
    std::string node;      // /dev/hidrawN
    std::string type;      // move | navigation
    std::string model;     // ZCM1 | ZCM2 | ""
    std::string address;   // eigen adres (uit het feature-rapport), leeg als onbekend
    std::string master;    // huidig master-adres, leeg als onbekend
};

struct Observation {
    std::vector<UsbCandidate> usb;           // nu via USB aangesloten
    std::set<std::string> bt_connected;      // adressen die nu via Bluetooth verbonden zijn
    std::set<std::string> bluez_paired;      // adressen die BlueZ als gekoppeld kent
    std::string host;                        // adres van de Bluetooth-adapter van deze pc (leeg = geen)
    bool helper_installed = false;
};

// Wat de daemon nu moet doen (een stap tegelijk; het resultaat gaat terug via done()).
struct Action {
    enum class Kind { None, SetMaster, Register } kind = Kind::None;
    UsbCandidate target;
    std::string host;
};

struct Timeouts {
    int64_t wait_usb_ns = 300'000'000'000;       // 5 minuten
    int64_t wait_unplug_ns = 120'000'000'000;    // 2 minuten
    int64_t wait_bluetooth_ns = 120'000'000'000; // 2 minuten
};

class Machine {
public:
    explicit Machine(Timeouts t = {}) : to_(t) {}
    // Gebruiker: start (ook na Done/Failed/Cancelled), bevestigen (opnieuw koppelen of opnieuw proberen), annuleren.
    void start(int64_t now_ns);
    bool confirm(int64_t now_ns);   // false als er niets te bevestigen is
    void cancel();
    // Elke ronde (± 4 per seconde) met wat er nu te zien is. Geeft de volgende actie (hoogstens een tegelijk).
    Action step(int64_t now_ns, const Observation& obs);
    // Resultaat van de laatste actie.
    void done(int64_t now_ns, bool ok, const std::string& message = {});

    State state() const { return state_; }
    bool busy() const { return pending_; }
    bool active() const { return state_ != State::Idle && state_ != State::Done && state_ != State::Failed && state_ != State::Cancelled; }
    const UsbCandidate& target() const { return target_; }
    const std::string& message() const { return message_; }
    // Uitleg voor de gebruiker bij de huidige stap (gewone taal).
    std::string instruction() const;

private:
    void go(State s, int64_t now_ns, std::string msg = {});
    Timeouts to_;
    State state_ = State::Idle;
    int64_t since_ = 0;
    bool pending_ = false;
    bool confirmed_ = false;
    bool set_done_ = false;
    UsbCandidate target_;
    std::string host_;
    std::string message_;
};

}  // namespace ravengun::pairing
