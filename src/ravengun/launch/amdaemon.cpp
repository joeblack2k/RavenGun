// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/launch/amdaemon.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "../../../tools/rg-amhw/amhw_table.h"

namespace ravengun::launch::amd {

// 40110: de "catcher" van de nieuwere AM Library (OG 0x594210: `request=startcatcher`); zonder luisteraar probeert pcpaOpenClient het 60 s (timeout 0xEA60) en wacht de game zo lang.
const int kDefaultPorts[] = {40100, 40102, 40103, 40104, 40106, 40107, 40108, 40110, 40114, 40115};
const size_t kDefaultPortCount = sizeof kDefaultPorts / sizeof kDefaultPorts[0];

bool is_word_char(unsigned char c) {
    if (std::isalnum(c)) return true;
    switch (c) {
        case '.': case '_': case '-': case ':': case '@': case '%': case '/': case '\\': case '#': case '{': case '}': return true;
        default: return false;
    }
}

bool valid_value(const std::string& v) {
    if (v.empty() || v.size() > 200) return false;
    return std::all_of(v.begin(), v.end(), [](unsigned char c) { return is_word_char(c); });
}

bool valid_response_extra(const std::string& v) {
    if (v.empty() || v.size() > 200) return false;
    return std::all_of(v.begin(), v.end(), [](unsigned char c) { return is_word_char(c) || c == '&' || c == '='; });
}

const Unit* Request::find(const std::string& key) const {
    for (const Unit& u : units)
        if (u.key == key) return &u;
    return nullptr;
}

Request parse_request(const std::string& line) {
    Request r;
    if (line.empty()) { r.error = "leeg verzoek"; return r; }
    if (line.size() > kMaxMessage - 2) { r.error = "verzoek langer dan 254 tekens"; return r; }
    size_t pos = 0;
    while (pos <= line.size()) {
        size_t amp = line.find('&', pos);
        const std::string part = line.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        Unit u;
        const size_t eq = part.find('=');
        u.key = part.substr(0, eq);
        if (eq != std::string::npos) { u.has_value = true; u.value = part.substr(eq + 1); }
        u.query = u.has_value && u.value == "?";
        if (u.key.empty()) { r.error = "eenheid zonder sleutel"; return r; }
        for (unsigned char c : u.key)
            if (!is_word_char(c)) { r.error = std::string("teken buiten libpcp in de sleutel: ") + static_cast<char>(c); return r; }
        for (unsigned char c : u.value)
            if (!is_word_char(c) && c != '?') { r.error = "teken buiten libpcp in de waarde van " + u.key; return r; }
        r.units.push_back(std::move(u));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    r.valid = true;
    return r;
}

std::string region_code(const std::string& name) {
    std::string n;
    for (unsigned char c : name) n += static_cast<char>(std::toupper(c));
    if (n == "USA") return "02";
    if (n == "EXPORT" || n == "EXP") return "04";
    if (n == "CHINA" || n == "CHN") return "08";
    return "01";   // JAPAN, JAP2N en onbekend: de game valt voor wat hij niet kent zelf terug op DefaultRegion
}

std::map<std::string, std::string> default_values(const std::string& region) {
    // Alles wat de game uit de keychip leest. Gemeten/gelezen: de ontleding (hex, tekstlengte, bitveld). Gokken: de waarden die de
    // game nergens vergelijkt (platformid, modeltype, billing); per game te overschrijven in [AmDaemon] van game.ini.
    return {
        {"keychip.appboot.systemflag", "00"},         // 1 byte hex (2 hexcijfers); bit 0 zet vlag [esi+0x1f] in fwAM (amDongleGetSystemFlag)
        {"keychip.version", "0101"},                  // 2 bytes hex (4 hexcijfers)
        {"keychip.appboot.gameid", "SBUH"},           // tot 4 tekens; per game (de naam van de *_Table.dat in MACtbl, gameid.txt)
        {"keychip.appboot.modeltype", "01"},          // 1 byte hex
        {"keychip.appboot.region", region_code(region)},
        {"keychip.appboot.platformid", "AAM"},        // tot 3 tekens; RingWide AAM, RingEdge AAL (de game leest hem nergens terug; zie docs/research/segajvs/README.md)
        {"keychip.appboot.dvdflag", "00"},            // 1 byte hex
        {"keychip.appboot.networkaddr", "0.0.0.0"},   // inet_addr
        {"keychip.billing.keyid", "A000-00000000000"},   // tot 16 tekens
        {"keychip.billing.mainid", "00000000000"},    // tot 11 tekens
        {"keychip.billing.playcount", "00000000"},    // 8 hexcijfers
        {"keychip.billing.playlimit", "FFFFFFFF"},
        {"keychip.billing.nearfull", "FFFFFFFF"},
        {"mxmaster.foreground.getcount.count", "1"},  // het aantal voorgrondprocessen dat amMasterGetProcessCount terugkrijgt
    };
}

struct AuthState {
    bool tried = false;
    std::unique_ptr<rg_amhw::AuthTable> table;
    std::string source;
};

Responder::Responder(std::map<std::string, std::string> values, std::vector<std::string> table_dirs)
    : values_(std::move(values)), table_dirs_(std::move(table_dirs)), auth_(std::make_shared<AuthState>()) {}

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Zoekt de tabel van deze game, eenmalig. Een bestand van de verkeerde grootte telt niet (de game leest het ook niet als tabel).
static void load_table(AuthState& st, const std::map<std::string, std::string>& values, const std::vector<std::string>& dirs) {
    st.tried = true;
    std::string id = "SBUH";
    if (auto it = values.find("keychip.appboot.gameid"); it != values.end()) id = it->second;
    const std::string name = id + "_Table.dat";
    std::vector<std::string> candidates;
    if (auto it = values.find("auth.table"); it != values.end()) candidates.push_back(it->second);
    for (const std::string& d : dirs) candidates.push_back(d + "/" + name);
    for (const std::string& path : candidates) {
        std::vector<uint8_t> data;
        if (read_file(path, data) && data.size() == rg_amhw::kTableSize) {
            st.table = std::make_unique<rg_amhw::AuthTable>(std::move(data));
            st.source = path;
            return;
        }
    }
    st.table = std::make_unique<rg_amhw::AuthTable>(rg_amhw::fake_table(name));
    st.source = "vervangende tabel (zoals rg-amhw.dll hem maakt) voor " + name + "; geen bestand gevonden";
}

std::string Responder::table_source() const { return auth_ && auth_->tried ? auth_->source : std::string(); }

Reply Responder::answer(const Request& r) const {
    Reply out;
    if (!r.valid) { out.note = "ongeldig verzoek: " + r.error; return out; }
    const Unit& u = *r.first();
    const std::string& key = u.key;
    if (key == "mxmaster.foreground.getcount") {
        // amMasterCheckResponse (state 8): atoi(waarde) moet het gevraagde mode zijn, en `count` is het resultaat (strtoul).
        std::string mode = u.has_value && !u.query ? u.value : std::string("2");
        if (mode.empty() || !std::all_of(mode.begin(), mode.end(), [](unsigned char c) { return std::isdigit(c); })) mode = "2";
        auto it = values_.find("mxmaster.foreground.getcount.count");
        out.line = key + "=" + mode + "&count=" + (it == values_.end() ? "1" : it->second);
        out.known = true;
        return out;
    }
    if (key == "mxmaster.foreground.next") {
        // amMasterExitA (SDR 0x6FE9F0..): het verzoek is `mxmaster.foreground.next=<n>` (n = de modus, 3 bij het afsluiten). amMasterCheckResponse (status 0xD,
        // 0x6FE3A6) eist dat atoi(waarde) gelijk is aan wat de game stuurde, anders fout -5 en een nieuwe poging (honderden keren); een `size`-veld is niet nodig.
        std::string n = u.has_value && !u.query ? u.value : std::string();
        if (n.empty() || !std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); })) { out.note = "foreground.next zonder getal"; return out; }
        out.line = key + "=" + n;
        out.known = true;
        return out;
    }
    if (key == "keychip.ds.compute" || key == "keychip.ssd.proof") {
        // De game vergelijkt het antwoord met een waarde uit zijn <spel-id>_Table.dat (zie amhw_table.h). Een uitdaging die niet in de tabel staat is een
        // willekeurige: daar vergelijkt de game niet, elk antwoord van de juiste lengte (40 hexcijfers ds, 32 hexcijfers ssd) is goed.
        const bool ds = key == "keychip.ds.compute";
        const std::string chal = u.has_value && !u.query ? u.value : std::string();
        if (chal.empty() || !auth_) { out.note = "uitdaging ontbreekt"; return out; }
        if (!auth_->tried) load_table(*auth_, values_, table_dirs_);
        int page = 0;
        if (const Unit* pg = r.find("page"); pg && pg->has_value && !pg->value.empty() && std::all_of(pg->value.begin(), pg->value.end(), [](unsigned char c) { return std::isdigit(c); }))
            page = std::atoi(pg->value.c_str());
        std::string ans = ds ? auth_->table->ds(chal, page) : auth_->table->ssd(chal);
        out.known = true;
        if (ans.empty()) {
            ans = std::string(ds ? 40 : 32, '0');
            out.note = "uitdaging niet in de tabel (willekeurig; de game vergelijkt niet)";
        } else {
            out.note = "antwoord uit de tabel (" + auth_->source + ")";
        }
        out.line = key + "=" + ans;
        return out;
    }
    if (key == "request") {
        // De nieuwere AM Library (LGS, GG, OG, Transformers) praat op install (40102/3) en storage (40114/5) in de vorm
        // `request=<naam>&...` en eist `response=<naam>&result=success` terug (responsecontrole in amInstall/amStorage).
        // Extra velden per verzoek kunnen in [AmDaemon] als `response.<naam>` = `k=v&k2=v2` worden meegegeven.
        const std::string name = u.has_value && !u.query ? u.value : std::string();
        if (name.empty()) { out.note = "request zonder naam"; return out; }
        out.line = "response=" + name + "&result=success";
        auto ex = values_.find("response." + name);
        if (ex != values_.end() && !ex->second.empty()) out.line += "&" + ex->second;
        else if (name == "query_appdata_status") {
            // amInstallResponseCheck (OG 0x5910A0, 0x592108): `status=` is verplicht (available/restored/unknown/error/needed/checking/formatting; zonder dat
            // fout -6 en "Faild to check application data area", OG 3005) en bij available/restored ook `id=` met het spel-id (4 tekens).
            auto gi = values_.find("keychip.appboot.gameid");
            out.line += "&status=available&id=" + (gi == values_.end() ? std::string("SBUH") : gi->second);
        }
        out.known = true;
        out.note = "request=<naam>: result=success";
        if (out.line.size() > kMaxMessage - 3) { out.note += "; antwoord te lang"; out.line.clear(); }
        return out;
    }
    auto it = values_.find(key);
    if (it != values_.end()) {
        out.line = key + "=" + it->second;
        out.known = true;
    } else if (key.rfind("mxmaster.", 0) == 0 || u.query) {
        out.line = key + "=0";
        out.note = "onbekende sleutel; antwoord is een gok";
    } else {
        out.line = key + "=" + u.value;
        out.note = "onbekende sleutel; waarde teruggestuurd";
    }
    if (out.line.size() > kMaxMessage - 3) { out.note += "; antwoord te lang"; out.line.clear(); }
    return out;
}

// --- Listener -----------------------------------------------------------------------------------------------------------------------

int Listener::listen() {
    for (int port : ports_) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) { say("AM-daemon: socket mislukt voor poort " + std::to_string(port) + ": " + std::strerror(errno)); continue; }
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<uint16_t>(port));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 8) != 0) {
            say("AM-daemon: poort " + std::to_string(port) + " is bezet of niet te openen (" + std::strerror(errno) +
                "; draait pcp-listen.py of een andere daemon?); overgeslagen");
            ::close(fd);
            continue;
        }
        socks_.emplace_back(port, fd);
    }
    if (socks_.empty()) return 0;
    std::string lijst;
    for (const auto& [p, fd] : socks_) { (void)fd; lijst += (lijst.empty() ? "" : ",") + std::to_string(p); }
    say("AM-daemon: luistert op 127.0.0.1 poort " + lijst + " (" + std::to_string(responder_.values().size()) + " waarden)");
    th_ = std::thread([this] { run(); });
    return static_cast<int>(socks_.size());
}

std::vector<int> Listener::bound_ports() const {
    std::vector<int> v;
    for (const auto& [p, fd] : socks_) { (void)fd; v.push_back(p); }
    return v;
}

bool Listener::send_all(int fd, const std::string& s) const {
    size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd p{fd, POLLOUT, 0};
            if (::poll(&p, 1, 200) <= 0) return false;
            continue;
        }
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

void Listener::handle_line(Conn& c, const std::string& line) {
    const Request req = parse_request(line);
    const Reply rep = responder_.answer(req);
    ++requests_;
    if (!rep.known) ++unknown_;
    const std::string key = req.valid ? req.units[0].key : std::string("?");
    const long n = ++seen_[key];
    // De eerste drie keer per sleutel volledig; daarna elke honderdste, zodat een game die blijft vragen het logboek niet vult.
    if (n <= 3 || n % 100 == 0 || !rep.known) {
        std::string m = "AM-daemon :" + std::to_string(c.port) + " <- " + line + "  -> " + (rep.line.empty() ? std::string("(geen antwoord)") : rep.line);
        if (!rep.known) m += "  [ONBEKEND" + (rep.note.empty() ? std::string() : ": " + rep.note) + "]";
        else if (!rep.note.empty()) m += "  [" + rep.note + "]";
        if (n > 3) m += "  (#" + std::to_string(n) + ")";
        say(m);
    }
    std::string wire = rep.line.empty() ? std::string() : rep.line + "\r\n";
    wire += ">";
    if (!send_all(c.fd, wire)) say("AM-daemon :" + std::to_string(c.port) + ": antwoord niet te versturen (verbinding weg)");
    ++c.served;
}

void Listener::run() {
    std::map<int, int> verbindingen;   // per poort, voor het logboek
    while (!closing_.load()) {
        std::vector<pollfd> fds;
        for (const auto& [p, fd] : socks_) { (void)p; fds.push_back({fd, POLLIN, 0}); }
        const size_t nl = fds.size();
        for (const Conn& c : conns_) fds.push_back({c.fd, POLLIN, 0});
        const int r = ::poll(fds.data(), fds.size(), 200);
        if (r < 0 && errno != EINTR) break;
        if (r <= 0) continue;
        std::vector<int> dead;
        for (size_t i = nl; i < fds.size(); ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            Conn& c = conns_[i - nl];
            char buf[1024];
            const ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (n <= 0) { dead.push_back(c.fd); continue; }
            c.buf.append(buf, static_cast<size_t>(n));
            for (;;) {
                const size_t nlp = c.buf.find('\n');
                if (nlp == std::string::npos) break;
                std::string line = c.buf.substr(0, nlp);
                c.buf.erase(0, nlp + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                handle_line(c, line);
            }
            if (c.buf.size() > kMaxMessage) {
                say("AM-daemon :" + std::to_string(c.port) + ": " + std::to_string(c.buf.size()) + " bytes zonder regeleinde; weggegooid");
                c.buf.clear();
            }
        }
        for (int fd : dead) {
            auto it = std::find_if(conns_.begin(), conns_.end(), [fd](const Conn& c) { return c.fd == fd; });
            if (it == conns_.end()) continue;
            if (verbindingen[it->port] <= 5)
                say("AM-daemon :" + std::to_string(it->port) + ": verbinding dicht (" + std::to_string(it->served) + " verzoeken)");
            ::close(fd);
            conns_.erase(it);
        }
        for (size_t i = 0; i < nl; ++i) {
            if (!(fds[i].revents & POLLIN)) continue;
            const int cfd = ::accept4(fds[i].fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (cfd < 0) continue;
            int one = 1;
            ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            const int port = socks_[i].first;
            const int nth = ++verbindingen[port];
            if (nth <= 5) say("AM-daemon :" + std::to_string(port) + ": game verbonden (verbinding " + std::to_string(nth) + "); prompt '>'");
            else if (nth == 6) say("AM-daemon :" + std::to_string(port) + ": meer verbindingen; verder alleen de telling");
            conns_.push_back({cfd, port, {}, 0});
            send_all(cfd, ">");
        }
    }
    for (const Conn& c : conns_) ::close(c.fd);
    conns_.clear();
}

void Listener::close() {
    if (closing_.exchange(true)) return;
    if (th_.joinable()) th_.join();
    for (const auto& [p, fd] : socks_) { (void)p; ::close(fd); }
    if (!socks_.empty())
        say("AM-daemon: gestopt (" + std::to_string(requests_.load()) + " verzoeken, waarvan " + std::to_string(unknown_.load()) + " onbekend)");
    socks_.clear();
}

}  // namespace ravengun::launch::amd
