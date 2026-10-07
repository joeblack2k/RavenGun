#pragma once
// Leest de hostsituatie (alleen lezen) en vult een Snapshot.
#include <string>
#include <vector>

#include "ravengun/model.hpp"

namespace ravengun {

struct ProbeOptions {
    std::string sys_root = "/sys";
    std::string dev_root = "/dev";
    std::string input_conf = "/etc/bluetooth/input.conf";
    // Extra zoekprefixen voor PSMoveAPI (voor de standaardlijst: zie default_psmoveapi_prefixes()).
    std::vector<std::string> psmoveapi_prefixes;
    // Roep `bluetoothctl` aan (uit te zetten in tests).
    bool use_bluetoothctl = true;
};

// RAVENGUN_PSMOVEAPI_PREFIX, $XDG_DATA_HOME/ravengun/psmoveapi, /usr/local, /usr
std::vector<std::string> default_psmoveapi_prefixes();

Snapshot probe(const ProbeOptions& opt);

// Losse onderdelen (publiek zodat tests ze met een nepboom kunnen draaien).
std::vector<VideoDevice> probe_video(const ProbeOptions& opt);
std::vector<HidDevice> probe_hid(const ProbeOptions& opt);
std::vector<UsbDevice> probe_usb(const ProbeOptions& opt);
std::vector<RfkillDevice> probe_rfkill(const ProbeOptions& opt);
std::vector<std::string> probe_bt_adapters(const ProbeOptions& opt);
void probe_psmoveapi(const ProbeOptions& opt, Snapshot& snap);

// Voert een commando uit zonder shell (argv[0] via PATH), geeft stdout terug.
// Time-out in milliseconden; bij fout of time-out ok=false.
struct RunResult {
    bool ok = false;
    std::string out;
};
RunResult run_capture(const std::vector<std::string>& argv, int timeout_ms = 3000);

}  // namespace ravengun
