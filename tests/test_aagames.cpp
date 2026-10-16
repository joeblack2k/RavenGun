// Unity-route (Tomb Raider / Rabbids Hollywood zonder TeknoParrot): pakketopbouw byte voor byte, het lezen van uitvoer met
// lengteprefix (ook in stukken), de configuratie, en de klant tegen een nep-plugin op loopback (verbinden, pakketten,
// recoil, opnieuw verbinden na verlies van de verbinding).
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ravengun/games/aagames.hpp"
#include "ravengun/json_parse.hpp"

using namespace ravengun::games;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

std::string slurp(const std::string& path) {
    std::string s;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

std::string hex(const std::vector<uint8_t>& v) {
    std::string s;
    char h[4];
    for (size_t i = 0; i < v.size(); ++i) { std::snprintf(h, sizeof h, "%s%02x", i ? " " : "", v[i]); s += h; }
    return s;
}

template <typename T> void put(std::vector<uint8_t>& b, T v) { uint8_t r[sizeof(T)]; std::memcpy(r, &v, sizeof(T)); b.insert(b.end(), r, r + sizeof(T)); }

// Een uitvoerbericht zoals de plugin het stuurt: int32 lengte (= payload + 1), koptype 2, 60 bytes.
std::vector<uint8_t> output_frame(const AaOutputs& o, uint8_t header = 2) {
    std::vector<uint8_t> b;
    put<int32_t>(b, 61);
    put<uint8_t>(b, header);
    for (int i = 0; i < 4; ++i) put<int32_t>(b, o.ammo[i]);
    for (int i = 0; i < 4; ++i) put<int32_t>(b, o.credits[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.damaged[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.playing[i]);
    for (int i = 0; i < 4; ++i) put<float>(b, o.life[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.recoil[i]);
    return b;
}

// Rabbids Hollywood: Ammo, Credits, Damaged, Life, Recoil, StartLamp (StartLamp achteraan).
std::vector<uint8_t> output_frame_rh(const AaOutputs& o) {
    std::vector<uint8_t> b;
    put<int32_t>(b, 61);
    put<uint8_t>(b, 2);
    for (int i = 0; i < 4; ++i) put<int32_t>(b, o.ammo[i]);
    for (int i = 0; i < 4; ++i) put<int32_t>(b, o.credits[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.damaged[i]);
    for (int i = 0; i < 4; ++i) put<float>(b, o.life[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.recoil[i]);
    for (int i = 0; i < 4; ++i) put<uint8_t>(b, o.playing[i]);
    return b;
}

// Nep-plugin: luistert op een vrije loopback-poort, accepteert een klant tegelijk.
struct FakePlugin {
    int lfd = -1, port = 0;
    int cfd = -1;
    FakePlugin() {
        lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int one = 1;
        ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(lfd, 1);
        socklen_t l = sizeof a;
        ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &l);
        port = ntohs(a.sin_port);
    }
    ~FakePlugin() { drop(); if (lfd >= 0) ::close(lfd); }
    bool accept_client(int ms) {
        pollfd p{lfd, POLLIN, 0};
        if (::poll(&p, 1, ms) <= 0) return false;
        cfd = ::accept(lfd, nullptr, nullptr);
        return cfd >= 0;
    }
    // Leest bytes tot `n` binnen zijn of de tijd om is.
    std::vector<uint8_t> read_some(size_t n, int ms) {
        std::vector<uint8_t> out;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (out.size() < n && std::chrono::steady_clock::now() < end) {
            pollfd p{cfd, POLLIN, 0};
            if (::poll(&p, 1, 20) > 0) {
                uint8_t b[512];
                ssize_t r = ::recv(cfd, b, sizeof b, 0);
                if (r <= 0) break;
                out.insert(out.end(), b, b + r);
            }
        }
        return out;
    }
    void drop() { if (cfd >= 0) { ::close(cfd); cfd = -1; } }
};

bool wait_for(const std::function<bool()>& f, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (f()) return true;
        ::usleep(5000);
    }
    return f();
}

}  // namespace

int main() {
    AaGame tr;
    tr.key = "TombRaider";
    tr.type = AaType::TombRaider;
    tr.width = 1920;
    tr.height = 1080;
    AaGame rh = tr;
    rh.key = "RabbidsHollywood";
    rh.type = AaType::RabbidsHollywood;

    // ---- Pixels ----
    {
        AaPixel p = to_pixels(tr, 0, 0);               // linksboven, Unity-oorsprong linksonder: Y bovenaan = height-1
        CHECK(p.x == 0.0f && p.y == 1079.0f);
        p = to_pixels(tr, 1, 1);
        CHECK(p.x == 1919.0f && p.y == 0.0f);
        AaGame top = tr;
        top.y_origin = "top";
        p = to_pixels(top, 0.25, 0.75);
        CHECK(std::fabs(p.x - 479.75f) < 0.01f && std::fabs(p.y - 809.25f) < 0.01f);
        AaGame cal = tr;                               // kalibratie: schaal 2 rond het midden, offset 0,1 omhoog in aim
        cal.scale_x = 2; cal.offset_x = 0.1;
        p = to_pixels(cal, 0.5, 0.5);
        CHECK(std::fabs(p.x - 0.6f * 1919) < 0.01f);
        p = to_pixels(cal, 1, 0.5);                    // buiten het bereik: begrensd
        CHECK(p.x == 1919.0f);
        p = to_pixels(tr, std::nan(""), 0.5);          // niet eindig: midden
        CHECK(std::fabs(p.x - 959.5f) < 0.01f);
    }

    // ---- Invoerpakket Tomb Raider: 42 bytes ----
    {
        AaInputs in{};
        in[0] = {true, 0.5, 0.5, true, false};
        in[1] = {true, 0.0, 1.0, false, true};
        const auto b = build_input_packet(tr, in);
        CHECK(b.size() == 42 && aa_input_size(AaType::TombRaider) == 42);
        // Axis_X: 959.5 = 0x446fe000 (little-endian 00 e0 6f 44), speler 2 u=0: 0; speler 3/4 (niet actief): midden.
        // Axis_Y: 539.5 = 0x4406e000 (midden); speler 2: v=1 (onderaan) wordt Y=0 bij Y-oorsprong onderaan.
        const std::string want =
            "00 e0 6f 44  00 00 00 00  00 e0 6f 44  00 e0 6f 44"            // Axis_X
            " 00 e0 06 44  00 00 00 00  00 e0 06 44  00 e0 06 44"            // Axis_Y
            " 01 00"                                                         // EnableInputsHack, HideCrosshairs
            " 00 01 00 00"                                                   // Reload
            " 01 00 00 00";                                                  // Trigger
        std::string w2;
        for (char c : want) if (c != ' ') w2 += c;
        std::string got = hex(b);
        std::string g2;
        for (char c : got) if (c != ' ') g2 += c;
        CHECK(g2 == w2);
        if (g2 != w2) std::fprintf(stderr, "  gekregen: %s\n  verwacht: %s\n", got.c_str(), want.c_str());
        // Niet actieve spelers sturen nooit een trekker, ook niet als de invoer die zou hebben.
        AaInputs x{};
        x[2].trigger = true; x[2].reload = true;
        auto bx = build_input_packet(tr, x);
        CHECK(bx[34 + 2] == 0 && bx[38 + 2] == 0);
    }
    // ---- Invoerpakket Rabbids Hollywood: 38 bytes, geen Reload ----
    {
        AaInputs in{};
        in[0] = {true, 0.5, 0.5, true, true};
        in[3] = {true, 1.0, 0.0, true, true};
        const auto b = build_input_packet(rh, in);
        CHECK(b.size() == 38 && aa_input_size(AaType::RabbidsHollywood) == 38);
        CHECK(b[32] == 1 && b[33] == 0);          // EnableInputsHack, HideCrosshairs
        CHECK(b[34] == 1 && b[35] == 0 && b[36] == 0 && b[37] == 1);  // Trigger P1 en P4
        const auto h = build_input_packet(rh, in, true);
        CHECK(h[33] == 1);
    }

    // ---- Uitvoer lezen ----
    {
        AaOutputs o;
        o.ammo[0] = 25; o.ammo[1] = -1; o.credits[0] = 3; o.damaged[2] = 1; o.playing[0] = 1; o.life[0] = 0.75f; o.life[3] = 1.0f;
        o.recoil[0] = 1; o.recoil[2] = 1;
        const auto f = output_frame(o);
        CHECK(f.size() == 65);
        AaFrameParser ps;
        AaOutputs r;
        ps.feed(f.data(), 3);                      // in stukken: eerst de halve lengte
        CHECK(!ps.next(r));
        ps.feed(f.data() + 3, 20);
        CHECK(!ps.next(r));
        ps.feed(f.data() + 23, f.size() - 23);
        CHECK(ps.next(r) && !ps.next(r));
        CHECK(r.ammo[0] == 25 && r.ammo[1] == -1 && r.credits[0] == 3 && r.damaged[2] == 1 && r.playing[0] == 1);
        CHECK(r.life[0] == 0.75f && r.life[3] == 1.0f && r.recoil[0] == 1 && r.recoil[1] == 0 && r.recoil[2] == 1);
        // Twee berichten in een keer, daartussen een bericht met een ander koptype (wordt overgeslagen).
        std::vector<uint8_t> two = f;
        const auto other = output_frame(o, 1);
        two.insert(two.end(), other.begin(), other.end());
        two.insert(two.end(), f.begin(), f.end());
        ps.feed(two.data(), two.size());
        int got = 0;
        while (ps.next(r)) ++got;
        CHECK(got == 2 && ps.frames() == 3 && ps.skipped() == 1 && !ps.broken());
        // Onmogelijke lengte: de stroom is niet meer te volgen.
        const uint8_t junk[8] = {0xff, 0xff, 0xff, 0x7f, 2, 0, 0, 0};
        ps.feed(junk, sizeof junk);
        CHECK(!ps.next(r) && ps.broken());
        ps.reset();
        CHECK(!ps.broken());
        // Rabbids Hollywood heeft een andere veldvolgorde (StartLamp achteraan).
        {
            AaOutputs q;
            q.ammo[1] = 7; q.credits[1] = 2; q.life[1] = 0.25f; q.recoil[1] = 1; q.playing[3] = 1; q.damaged[0] = 1;
            const auto fr = output_frame_rh(q);
            AaFrameParser rp(AaType::RabbidsHollywood);
            rp.feed(fr.data(), fr.size());
            AaOutputs got2;
            CHECK(rp.next(got2));
            CHECK(got2.ammo[1] == 7 && got2.credits[1] == 2 && got2.life[1] == 0.25f && got2.recoil[1] == 1 && got2.playing[3] == 1 &&
                  got2.playing[1] == 0 && got2.damaged[0] == 1 && got2.recoil[3] == 0);
        }
        // Een te kort bericht (payload < 60) wordt overgeslagen, niet gelezen.
        std::vector<uint8_t> shortf;
        put<int32_t>(shortf, 11);
        put<uint8_t>(shortf, 2);
        for (int i = 0; i < 10; ++i) put<uint8_t>(shortf, 0);
        ps.feed(shortf.data(), shortf.size());
        CHECK(!ps.next(r) && ps.skipped() == 2);
    }

    // ---- Configuratie: patches/<Game>/unity.json ----
    {
        std::string err;
        AaGame g;
        // De echte bestanden (de meegeleverde laag: $RAVENGUN_PATCHES_DIR, anders patches/ in de repo); overgeslagen als ze er niet zijn.
        const char* pd = std::getenv("RAVENGUN_PATCHES_DIR");
        const std::string patches = pd && *pd ? std::string(pd) : std::string(RAVENGUN_SOURCE_DIR) + "/patches";
        const std::string tr_text = slurp(patches + "/TombRaider/unity.json"), rh_text = slurp(patches + "/RabbidsHollywood/unity.json");
        if (tr_text.empty() || rh_text.empty()) {
            std::printf("aagames: geen unity.json in %s (echte bestanden overgeslagen)\n", patches.c_str());
        } else {
            CHECK(parse_aagames_game("TombRaider", tr_text, g, err) && err.empty());
            CHECK(g.type == AaType::TombRaider && g.width == 1920 && g.height == 1080 && g.send_hz > 0 && g.plugin_dir == "TombRaider");
            CHECK(parse_aagames_game("RabbidsHollywood", rh_text, g, err));
            CHECK(g.type == AaType::RabbidsHollywood && g.plugin_dir == "RabbidsHollywood");
            // Het meegeleverde bestand noemt geen pad van een machine: de map van de game is een mapnaam in de gamemappen.
            for (const std::string* t : {&tr_text, &rh_text}) {
                CHECK(t->find("nas_dir") == std::string::npos && t->find("/home/") == std::string::npos && t->find("/mnt/") == std::string::npos);
                CHECK(t->find("\"folder\"") != std::string::npos);
            }
        }
        CHECK(!parse_aagames_game("X", "{", g, err) && !err.empty());
        CHECK(!parse_aagames_game("X", R"({"type":"onbekend"})", g, err) && err.find("onbekend type") != std::string::npos);
        CHECK(err.find("patches/X/unity.json") != std::string::npos);
        CHECK(!parse_aagames_game("X", R"({"type":"tomb_raider","scale_x":0})", g, err) && err.find("schaal") != std::string::npos);
        CHECK(!parse_aagames_game("X", R"({"type":"tomb_raider","y_origin":"midden"})", g, err) && err.find("y_origin") != std::string::npos);
        CHECK(!parse_aagames_game("X", R"({"type":"tomb_raider","width":"breed"})", g, err) && err.find("getal") != std::string::npos);
        CHECK(parse_aagames_game("X", R"({"type":"rabbids_hollywood","width":1280,"height":720,"offset_y":0.05,"y_origin":"top"})", g, err));
        CHECK(err.empty() && g.key == "X" && g.name == "X" && g.width == 1280 && g.offset_y == 0.05 && g.y_origin == "top");
    }

    // ---- Klant tegen een nep-plugin ----
    {
        FakePlugin plugin;
        AaGame g = tr;
        g.send_hz = 100;
        std::atomic<int> shots{0};
        std::atomic<int> last_player{0};
        std::atomic<bool> trig{false};
        AaClient client;
        // Vóór de plugin luistert is de poort dicht: nog niets accepteren, de klant probeert opnieuw.
        client.start(g, plugin.port,
            [&](AaInputs& in) { in[0] = {true, 0.5, 0.5, trig.load(), false}; },
            [&](int p) { ++shots; last_player = p; }, "127.0.0.1", 100);
        CHECK(plugin.accept_client(2000));
        CHECK(wait_for([&] { return client.stats().connected; }, 1000));
        auto bytes = plugin.read_some(42 * 10, 1500);
        CHECK(bytes.size() >= 42 * 10);
        const size_t first = bytes.size();
        // Alles zijn hele pakketten van 42 bytes met de vaste kop (midden, geen trekker).
        bool framed = true;
        for (size_t i = 0; i + 42 <= bytes.size(); i += 42)
            framed = framed && bytes[i + 32] == 1 && bytes[i + 38] == 0 && bytes[i] == 0x00 && bytes[i + 1] == 0xe0 && bytes[i + 2] == 0x6f && bytes[i + 3] == 0x44;
        CHECK(framed);
        ::usleep(1100000);                         // een volle seconde sinds het verbinden
        double pps = client.stats().packets_per_s;
        CHECK(pps > 60 && pps < 140);              // rond send_hz
        // Trekker: komt in het volgende pakket (Trigger P1 op byte 38).
        trig = true;
        ::usleep(60000);
        auto more = plugin.read_some(1 << 20, 300);          // alles wat er ligt; de stroom heeft geen kaders, dus vanaf het begin tellen
        bytes.insert(bytes.end(), more.begin(), more.end());
        bool saw = false;
        for (size_t i = (first / 42) * 42; i + 42 <= bytes.size(); i += 42) saw = saw || bytes[i + 38] == 1;
        CHECK(saw);
        // Het laatste volledige pakket heeft de trekker nog steeds.
        CHECK(bytes[(bytes.size() / 42 - 1) * 42 + 38] == 1);
        // Uitvoer van de plugin: een schot van speler 3 (in twee delen verstuurd) en daarna status.
        AaOutputs o;
        o.recoil[2] = 1; o.playing[0] = 1; o.ammo[0] = 17; o.life[0] = 0.5f; o.credits[0] = 2;
        auto f = output_frame(o);
        ::send(plugin.cfd, f.data(), 10, MSG_NOSIGNAL);
        ::usleep(20000);
        ::send(plugin.cfd, f.data() + 10, f.size() - 10, MSG_NOSIGNAL);
        CHECK(wait_for([&] { return shots.load() == 1; }, 1000));
        CHECK(last_player.load() == 3);
        auto st = client.stats();
        CHECK(st.have_outputs && st.last.ammo[0] == 17 && st.recoils[2] == 1 && st.output_frames == 1);
        auto js = ravengun::json::parse(client.status_json());
        CHECK(js.ok && js.value.find("connected")->b && js.value.find("players")->arr.size() == 4);
        CHECK(js.value.find("players")->arr[0].find("ammo")->num == 17 && js.value.find("players")->arr[2].find("recoils")->num == 1);
        // Verbinding weg (de game sluit): de klant verbindt opnieuw zodra de plugin weer luistert.
        plugin.drop();
        CHECK(wait_for([&] { return !client.stats().connected; }, 1000));
        CHECK(plugin.accept_client(2500));
        CHECK(wait_for([&] { return client.stats().connected && client.stats().connects == 2; }, 1000));
        auto again = plugin.read_some(42 * 3, 1000);
        CHECK(again.size() >= 42 * 3);
        CHECK(client.stats().disconnects == 1);
        client.stop();
        CHECK(!client.stats().connected && !client.running());
        // Na stop() staat de poort leeg: geen thread die nog verbindt.
        const uint64_t attempts = client.stats().connect_attempts;
        ::usleep(300000);
        CHECK(client.stats().connect_attempts == attempts);
    }

    // ---- Klant zonder luisterende plugin: herhaalt, zonder te blokkeren, en stopt netjes ----
    {
        int dead_port;
        {
            FakePlugin p;
            dead_port = p.port;
        }                                           // poort is nu weer dicht
        AaClient c;
        c.start(tr, dead_port, [](AaInputs&) {}, {}, "127.0.0.1", 60);
        ::usleep(400000);
        const auto st = c.stats();
        CHECK(!st.connected && st.connect_attempts >= 3 && !st.last_error.empty());
        const auto t0 = std::chrono::steady_clock::now();
        c.stop();
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));
    }

    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
