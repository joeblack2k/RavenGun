#pragma once
// De AM-daemon van de Sega Ring-games (RingWide/RingEdge) als antwoordgever op 127.0.0.1:40100..40115.
//
// De AM Library zit statisch in de exe van LGI, LGI3D, LGS, GG, OG, SDR en Transformers (docs/research/segajvs/README.md). Op een
// echte kast antwoordt een daemon op de poorten; wij zijn die daemon voor de duur van een run. Het protocol (libpcp, gelezen uit
// de exe's zelf, niet uit TeknoParrot):
//   * tekst; een verzoek is `sleutel=waarde[&sleutel=waarde...]\r\n`, hoogstens 256 bytes; `waarde` is `?` bij een vraag;
//   * de daemon stuurt een prompt `>` bij het verbinden en na elk antwoord (pcppRecvPrompt wacht erop vóór een verzoek);
//   * een antwoord is `sleutel=waarde[&sleutel=waarde...]\r\n`: de EERSTE sleutel moet dezelfde zijn als die van het verzoek (de
//     game vergelijkt pcpaGetKeyword(0) met de naam van zijn vraag), met de waarde erbij; extra velden zijn per vraag (bv. `count`);
//     een veld `code=N` (N > 0) is een foutmelding en blijft weg bij succes;
//   * de waarden zijn tekst die de game ontleedt: hex-bytes in hoofdletters (`keychip.appboot.systemflag`, `.modeltype`, `.region`, `.dvdflag` = 1 byte = 2
//     hexcijfers, `keychip.version` = 2 bytes = 4 hexcijfers; de game leest alleen dat aantal cijfers, 48d4c0 in LGI), tekst met een maximumlengte (`gameid` 4, `platformid` 3, `billing.keyid` 16, `billing.mainid` 11), een IPv4-adres
//     (`networkaddr`, inet_addr), 8 hexcijfers (`billing.playcount/playlimit/nearfull`), en `region` als hex-bitveld (01 JPN, 02 USA,
//     04 EXP, 08 CHN; fwRegion in de exe).
// Het ontleden en antwoorden zit hier zonder sockets (te testen met een nepcliënt); de luisteraar zit in Listener.
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ravengun::launch::amd {

constexpr size_t kMaxMessage = 256;   // libpcp: het pakket past in 0x100 bytes
extern const int kDefaultPorts[];     // 40100, 40102-40104, 40106-40108, 40110, 40114, 40115
extern const size_t kDefaultPortCount;

struct Unit {
    std::string key, value;
    bool has_value = false;
    bool query = false;   // waarde is `?`
};

struct Request {
    std::vector<Unit> units;
    bool valid = false;
    std::string error;
    const Unit* first() const { return units.empty() ? nullptr : &units[0]; }
    const Unit* find(const std::string& key) const;
};

// Een regel zonder afsluitende CRLF. Ongeldig: leeg, te lang, een teken buiten de woordtekens van libpcp, of geen sleutel vooraan.
Request parse_request(const std::string& line);
// Woordtekens van libpcp (letters, cijfers en . _ - : @ % / \ # { }); de rest scheidt of is ongeldig.
bool is_word_char(unsigned char c);
// Mag deze tekst als waarde in een antwoord (woordtekens, geen CR/LF/&/=/> of spatie, hoogstens 200 tekens)?
bool valid_value(const std::string& v);
// `response.<naam>` in [AmDaemon]: extra velden `k=v&k2=v2` achter `response=<naam>&result=success` (de nieuwere AM Library, request=<naam>).
bool valid_response_extra(const std::string& v);

// De regio-naam van het TeknoParrot-profiel (JAPAN, USA, EXPORT, CHINA, JAP2N) als hex-bitveld voor keychip.appboot.region.
std::string region_code(const std::string& name);
// De standaardwaarden per sleutel (zie boven); `region` is DongleRegion uit game.ini (leeg = JAPAN).
std::map<std::string, std::string> default_values(const std::string& region);

struct Reply {
    std::string line;    // het antwoord zonder CRLF
    bool known = false;  // een sleutel die we kennen (anders een voorzichtig antwoord en een melding in het logboek)
    std::string note;
};

// `keychip.ds.compute` en `keychip.ssd.proof`: de game vergelijkt het antwoord met het verwachte antwoord uit <spel-id>_Table.dat (tools/rg-amhw/amhw_table.h).
// De tabel wordt bij de eerste vraag gezocht in `table_dirs` (mappen, met <spel-id>_Table.dat erin), of staat in de waarde `auth.table` (een bestandspad);
// ontbreekt hij overal, dan wordt dezelfde vervangende tabel gemaakt als rg-amhw.dll voor de game maakt.
struct AuthState;

class Responder {
public:
    explicit Responder(std::map<std::string, std::string> values, std::vector<std::string> table_dirs = {});
    Reply answer(const Request& r) const;
    const std::map<std::string, std::string>& values() const { return values_; }
    // Waar de tabel vandaan komt (leeg zolang er niet naar gevraagd is); voor het logboek en de tests.
    std::string table_source() const;

private:
    std::map<std::string, std::string> values_;
    std::vector<std::string> table_dirs_;
    std::shared_ptr<AuthState> auth_;
};

// De luisteraar: één thread die op alle poorten wacht, per verbinding de prompt stuurt, regels ontleedt en antwoordt. Een poort die
// bezet is (bv. pcp-listen.py) wordt gemeld en overgeslagen; de rest blijft werken.
class Listener {
public:
    using LogFn = std::function<void(const std::string&)>;
    Listener(Responder responder, std::vector<int> ports, LogFn log) : responder_(std::move(responder)), ports_(std::move(ports)), log_(std::move(log)) {}
    ~Listener() { close(); }
    // Geeft het aantal poorten dat luistert (0 = geen enkele; close() mag dan nog steeds).
    int listen();
    void close();
    long requests() const { return requests_.load(); }
    long unknown() const { return unknown_.load(); }
    std::vector<int> bound_ports() const;

private:
    struct Conn { int fd; int port; std::string buf; long served = 0; };
    void run();
    void say(const std::string& m) const { if (log_) log_(m); }
    void handle_line(Conn& c, const std::string& line);
    bool send_all(int fd, const std::string& s) const;

    Responder responder_;
    std::vector<int> ports_;
    LogFn log_;
    std::vector<std::pair<int, int>> socks_;   // (poort, fd)
    std::vector<Conn> conns_;
    std::map<std::string, long> seen_;         // per sleutel hoe vaak gezien (het logboek raakt anders vol)
    std::atomic<bool> closing_{false};
    std::atomic<long> requests_{0}, unknown_{0};
    std::thread th_;
};

}  // namespace ravengun::launch::amd
