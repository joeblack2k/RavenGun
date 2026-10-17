// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ravengun-bt-helper: registreert of verwijdert een PS Move of Navigation Controller bij BlueZ. Het ENIGE dat RavenGun tijdens
// het spelen met rootrechten doet (de daemon start het met `sudo -n`, alleen dit pad; docs/decisions.md, "Root-hulp voor BlueZ").
// Dit is de C++-vervanger van scripts/ravengun-bt-helper (zelfde interface en dezelfde bestanden voor BlueZ).
//
//   ravengun-bt-helper register   AA:BB:CC:DD:EE:FF move|move2|navigation
//   ravengun-bt-helper unregister AA:BB:CC:DD:EE:FF
//
// register: schrijft /var/lib/bluetooth/<adapter>/<ADRES>/info (Name, Class, Trusted=true, HID-service, DeviceID 054c/<product>
//   en een koppelsleutel, zodat BlueZ de inkomende verbinding van een controller zonder eigen authenticatie accepteert) en de
//   SDP-cache (HID-beschrijving), en herstart bluetooth.service. Een bestaande registratie wordt alleen overschreven als het een
//   Sony-controller is (DeviceID Vendor=1356); de eerste versie gaat naar info.ravengun-backup.
// unregister: verwijdert de map en de cache, maar alleen als het een Sony-HID-registratie is (Vendor=1356, product 981, 3166 of
//   1071). Normaal ontkoppelt de daemon zonder root via D-Bus; dit is de terugval.
//
// Gehard: precies deze twee acties en dit aantal argumenten, een adres als AA:BB:CC:DD:EE:FF (hoofdletters, geen nul- of
// broadcastadres, niet het adres van de adapter), een type uit een vaste lijst, geen omgevingsvariabelen (lege omgeving, geen
// PATH-zoekopdracht: alleen /usr/bin/systemctl), umask 077, bestandsbewerkingen met O_NOFOLLOW vanaf bestandsbeschrijvingen,
// atomisch schrijven, logging naar de journal (syslog) en /var/log/ravengun-bt-helper.log.
// Exitcodes: 0 gelukt, 1 mislukt, 2 ongeldige argumenten, 3 niet als root gestart, 4 geweigerd (geen Sony-registratie, geen of
// meer dan een adapter, adres van de adapter, symlink).
#include <functional>
#include <string>

namespace ravengun::syshelper {

inline constexpr int kBtOk = 0, kBtFailed = 1, kBtUsage = 2, kBtNotRoot = 3, kBtRefused = 4;

struct BtRequest {
    enum class Action { Register, Unregister } action = Action::Register;
    std::string address;  // AA:BB:CC:DD:EE:FF
    std::string type;     // move | move2 | navigation (alleen bij Register)
};

// Leest de opdrachtregel (argv[0] telt niet mee). Geeft kBtOk of kBtUsage met een uitleg in err. Niets wordt uitgevoerd.
int parse_bt_args(int argc, const char* const* argv, BtRequest& req, std::string& err);

// Is dit info-bestand de registratie van een Sony-HID-apparaat (Move of Navigation Controller)?
bool bt_info_is_sony(const std::string& info_text);
// De inhoud van het info-bestand en van de SDP-cache voor een type (leeg bij een onbekend type).
std::string bt_info_text(const std::string& type, const std::string& link_key_hex);
std::string bt_cache_text(const std::string& type);
const char* bt_device_name(const std::string& type);

// De omgeving van de uitvoering; alles is te vervangen in tests (tijdelijke map, geen systemctl).
struct BtEnv {
    std::string btdir = "/var/lib/bluetooth";
    std::function<bool()> stop_bluetooth;                 // leeg = niets doen
    std::function<bool()> start_bluetooth;                // leeg = niets doen
    std::function<void(const std::string&)> log;          // leeg = niet loggen
    std::function<std::string()> random_key;              // 32 hoofdletter-hexcijfers; leeg = getrandom(2)
};

// Voert de opdracht uit (zonder de rootcontrole; die doet main). out krijgt de melding voor de gebruiker, err de reden bij een fout.
int run_bt_request(const BtRequest& req, BtEnv& env, std::string& out, std::string& err);

}  // namespace ravengun::syshelper
