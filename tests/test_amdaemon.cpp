// Tests voor de AM-daemon van de Sega Ring-games (src/ravengun/launch/amdaemon.*): het ontleden van een verzoek zoals libpcp het bouwt,
// de antwoorden die de AM Library van de game kan lezen (gelezen uit LGI_RingW_F_safe.exe: amMasterCheckResponse, amDongleResponseCheck,
// amDongleSetupKeychip), en de luisteraar met een nepcliënt over echte sockets op 127.0.0.1. Start geen game en geen Wine.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ravengun/launch/amdaemon.hpp"

using namespace ravengun::launch::amd;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static int free_port() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
    socklen_t l = sizeof a;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &l);
    const int p = ntohs(a.sin_port);
    ::close(s);
    return p;
}

// Een nepcliënt zoals libpcp: verbinden, op de prompt wachten, een verzoek sturen, het antwoord tot de volgende prompt lezen.
struct Client {
    int fd = -1;
    bool connect_to(int port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<uint16_t>(port));
        return ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    }
    // Leest tot er een '>' in zit (of na `ms` milliseconden niets meer); geeft alles wat kwam.
    std::string read_until_prompt(int ms = 2000) {
        std::string got;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end && got.find('>') == std::string::npos) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 100) <= 0) continue;
            char b[512];
            const ssize_t n = ::recv(fd, b, sizeof b, 0);
            if (n <= 0) break;
            got.append(b, static_cast<size_t>(n));
        }
        return got;
    }
    std::string ask(const std::string& line) {
        const std::string w = line + "\r\n";
        ::send(fd, w.data(), w.size(), MSG_NOSIGNAL);
        return read_until_prompt();
    }
    ~Client() { if (fd >= 0) ::close(fd); }
};

static void test_parse() {
    // Zoals de game het stuurt (gezien op de bench, 2026-10-17).
    Request r = parse_request("keychip.appboot.systemflag=?&cache=0");
    CHECK(r.valid && r.units.size() == 2);
    CHECK(r.first()->key == "keychip.appboot.systemflag" && r.first()->query && r.first()->has_value);
    CHECK(r.units[1].key == "cache" && r.units[1].value == "0" && !r.units[1].query);
    CHECK(r.find("cache") != nullptr && r.find("nope") == nullptr);
    r = parse_request("mxmaster.foreground.getcount=2");
    CHECK(r.valid && r.units.size() == 1 && r.first()->value == "2" && !r.first()->query);
    r = parse_request("mxmaster.develop");   // een sleutel zonder waarde (pcppSetSendPacket kent "%s\r\n")
    CHECK(r.valid && !r.first()->has_value);
    // Ongeldig: leeg, geen sleutel, een teken dat libpcp niet als woord kent, te lang.
    CHECK(!parse_request("").valid);
    CHECK(!parse_request("=1").valid);
    CHECK(!parse_request("a b=1").valid);
    CHECK(!parse_request("a=1&&b=2").valid);
    CHECK(!parse_request(std::string(300, 'a')).valid);
    CHECK(is_word_char('.') && is_word_char('_') && is_word_char('\\') && !is_word_char(' ') && !is_word_char('&') && !is_word_char('>'));
    CHECK(valid_value("SBUH") && valid_value("A000-00000000000") && valid_value("0.0.0.0") && !valid_value("") && !valid_value("a b") &&
          !valid_value("a&b") && !valid_value("a>b") && !valid_value(std::string(201, 'a')));
}

static void test_values() {
    CHECK(region_code("JAPAN") == "01" && region_code("JAP2N") == "01" && region_code("USA") == "02" && region_code("EXPORT") == "04" &&
          region_code("CHINA") == "08" && region_code("usa") == "02" && region_code("") == "01");
    const auto d = default_values("USA");
    CHECK(d.at("keychip.appboot.region") == "02");
    // De ontleding van de game: hex-cijfers met een vaste lengte (1 byte = 2 cijfers, versie 4 cijfers), tekst met een maximum, 8 hexcijfers.
    CHECK(d.at("keychip.appboot.systemflag").size() == 2 && d.at("keychip.version").size() == 4);
    for (const char* k : {"keychip.appboot.modeltype", "keychip.appboot.region", "keychip.appboot.dvdflag"}) CHECK(d.at(k).size() == 2);
    CHECK(d.at("keychip.appboot.gameid").size() <= 4 && d.at("keychip.appboot.platformid").size() <= 3);
    CHECK(d.at("keychip.billing.keyid").size() <= 16 && d.at("keychip.billing.mainid").size() <= 11);
    for (const char* k : {"keychip.billing.playcount", "keychip.billing.playlimit", "keychip.billing.nearfull"}) CHECK(d.at(k).size() == 8);
    for (const auto& [k, v] : d) CHECK(valid_value(v) || !(std::fprintf(stderr, "waarde %s=%s ongeldig\n", k.c_str(), v.c_str()), true));
}

static void test_responder() {
    Responder rs(default_values("JAPAN"));
    // De eerste sleutel van het antwoord is die van het verzoek, met de waarde erbij; geen `code` bij succes.
    Reply a = rs.answer(parse_request("keychip.appboot.systemflag=?&cache=0"));
    CHECK(a.known && a.line == "keychip.appboot.systemflag=00");
    a = rs.answer(parse_request("keychip.appboot.region=?&cache=0"));
    CHECK(a.known && a.line == "keychip.appboot.region=01");
    a = rs.answer(parse_request("keychip.version=?&cache=0"));
    CHECK(a.known && a.line == "keychip.version=0101");
    // amMasterCheckResponse (getcount): de waarde is het gevraagde mode, `count` het resultaat.
    a = rs.answer(parse_request("mxmaster.foreground.getcount=2"));
    CHECK(a.known && a.line == "mxmaster.foreground.getcount=2&count=1");
    a = rs.answer(parse_request("mxmaster.foreground.getcount=3"));
    CHECK(a.line == "mxmaster.foreground.getcount=3&count=1");
    // Onbekend: een voorzichtig antwoord, gemeld als onbekend.
    a = rs.answer(parse_request("keychip.ssd.proof=?&cache=0"));
    CHECK(!a.known && a.line == "keychip.ssd.proof=0" && contains(a.note, "onbekend"));
    a = rs.answer(parse_request("mxmaster.foreground.next=1"));
    CHECK(!a.known && a.line == "mxmaster.foreground.next=0");
    a = rs.answer(parse_request("iets.anders=7"));
    CHECK(!a.known && a.line == "iets.anders=7");
    a = rs.answer(parse_request("a b"));
    CHECK(!a.known && a.line.empty() && contains(a.note, "ongeldig"));
    // Een waarde uit game.ini wint en een ander count ook.
    auto v = default_values("JAPAN");
    v["keychip.appboot.gameid"] = "SBWB";
    v["mxmaster.foreground.getcount.count"] = "0";
    Responder r2(v);
    CHECK(r2.answer(parse_request("keychip.appboot.gameid=?&cache=0")).line == "keychip.appboot.gameid=SBWB");
    CHECK(r2.answer(parse_request("mxmaster.foreground.getcount=2")).line == "mxmaster.foreground.getcount=2&count=0");
}

static void test_listener() {
    std::mutex m;
    std::vector<std::string> log;
    auto sink = [&](const std::string& s) { std::lock_guard<std::mutex> g(m); log.push_back(s); };
    auto all = [&] { std::lock_guard<std::mutex> g(m); std::string r; for (const auto& l : log) r += l + "\n"; return r; };
    const int p1 = free_port();
    int p2 = free_port();
    while (p2 == p1) p2 = free_port();
    Listener l(Responder(default_values("JAPAN")), {p1, p2}, sink);
    CHECK(l.listen() == 2 && l.bound_ports().size() == 2);

    // Een poort die al bezet is wordt gemeld en overgeslagen; de rest werkt.
    Listener l2(Responder(default_values("JAPAN")), {p1, free_port()}, sink);
    CHECK(l2.listen() == 1 && contains(all(), "is bezet"));
    l2.close();

    Client c;
    CHECK(c.connect_to(p1));
    // Bij het verbinden alleen de prompt: de game wacht erop (pcppRecvPrompt) voordat hij iets stuurt.
    CHECK(c.read_until_prompt() == ">");
    // Het antwoord en direct erna weer de prompt, in een keer.
    CHECK(c.ask("keychip.appboot.systemflag=?&cache=0") == "keychip.appboot.systemflag=00\r\n>");
    CHECK(c.ask("keychip.version=?&cache=0") == "keychip.version=0101\r\n>");
    // Meer verzoeken op dezelfde verbinding, en een tweede poort tegelijk.
    Client d;
    CHECK(d.connect_to(p2) && d.read_until_prompt() == ">");
    CHECK(d.ask("mxmaster.foreground.getcount=2") == "mxmaster.foreground.getcount=2&count=1\r\n>");
    CHECK(c.ask("keychip.appboot.gameid=?&cache=0") == "keychip.appboot.gameid=SBUH\r\n>");
    // Twee verzoeken in een pakket worden allebei beantwoord; een verzoek in stukken ook.
    {
        const std::string w = "keychip.appboot.platformid=?&cache=0\r\nkeychip.appboot.dvdflag=?&cache=0\r\n";
        ::send(c.fd, w.data(), w.size(), MSG_NOSIGNAL);
        std::string got = c.read_until_prompt();
        if (!contains(got, "dvdflag")) got += c.read_until_prompt();
        CHECK(contains(got, "keychip.appboot.platformid=AAV\r\n") && contains(got, "keychip.appboot.dvdflag=00\r\n"));
        ::send(c.fd, "keychip.appboot.reg", 19, MSG_NOSIGNAL);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(c.ask("ion=?&cache=0") == "keychip.appboot.region=01\r\n>");
    }
    // Een ongeldig verzoek krijgt alleen de prompt terug (de game wacht anders eeuwig op de prompt).
    CHECK(c.ask("a b=1") == ">");
    // De game mag de verbinding verbreken en opnieuw beginnen (hij doet dat na een time-out): een nieuwe verbinding begint weer met een prompt.
    ::close(c.fd);
    c.fd = -1;
    Client e;
    CHECK(e.connect_to(p1) && e.read_until_prompt() == ">");
    CHECK(e.ask("keychip.appboot.systemflag=?&cache=0") == "keychip.appboot.systemflag=00\r\n>");
    // Een onbekende sleutel staat als ONBEKEND in het logboek met verzoek en antwoord.
    CHECK(e.ask("keychip.ssd.proof=?&cache=0") == "keychip.ssd.proof=0\r\n>");
    // Het logboek heeft elk verzoek met zijn antwoord.
    const std::string lg = all();
    CHECK(contains(lg, "<- keychip.appboot.systemflag=?&cache=0  -> keychip.appboot.systemflag=00"));
    CHECK(contains(lg, "<- keychip.ssd.proof=?&cache=0  -> keychip.ssd.proof=0  [ONBEKEND"));
    CHECK(contains(lg, "game verbonden"));
    CHECK(l.requests() >= 8 && l.unknown() >= 2);   // de twee onbekende (ssd.proof, het ongeldige verzoek)
    // Herhaalde verzoeken vullen het logboek niet: na de derde keer alleen elke honderdste.
    for (int i = 0; i < 20; ++i) CHECK(e.ask("keychip.version=?&cache=0") == "keychip.version=0101\r\n>");
    {
        std::lock_guard<std::mutex> g(m);
        long n = 0;
        for (const auto& s : log) if (contains(s, "<- keychip.version=")) ++n;
        CHECK(n == 3);
    }
    l.close();
    CHECK(contains(all(), "gestopt"));
    // Na close() luistert niemand meer.
    Client f;
    CHECK(!f.connect_to(p1));
}

int main() {
    test_parse();
    test_values();
    test_responder();
    test_listener();
    std::printf("amdaemon: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
