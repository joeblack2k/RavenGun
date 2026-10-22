// Game-out file (ravengun/iobridge/game_out.hpp): layout round trip, rejected frames (short, magic, version, players, torn),
// and the tracker's safety rules: a file left over from an earlier run is ignored until it changes, the motor level only
// counts while the writer keeps updating (stale -> 0), a missing file is 0, solenoid edges are counted once, and a writer
// restart (counter going down) is counted from zero. No hardware, simulated time.
#include <cstdio>
#include <string>

#include "ravengun/iobridge/game_out.hpp"

using namespace ravengun::iobridge;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static GameOutFrame frame(uint32_t seq, int m1, int m2, uint32_t p1 = 0, uint32_t p2 = 0) {
    GameOutFrame f;
    f.seq = seq;
    f.players = 2;
    f.motor[0] = m1; f.motor[1] = m2;
    f.motor_max[0] = f.motor_max[1] = 8;
    f.pulses[0] = p1; f.pulses[1] = p2;
    return f;
}

static void test_layout() {
    unsigned char b[kGameOutSize];
    GameOutFrame f = frame(77, 5, 1, 3, 4);
    f.solenoid[1] = true;
    encode_game_out(f, b);
    GameOutFrame g;
    std::string why;
    CHECK(parse_game_out(b, sizeof b, g, &why));
    CHECK(g.seq == 77 && g.players == 2 && g.motor[0] == 5 && g.motor[1] == 1 && g.motor_max[0] == 8 && !g.solenoid[0] && g.solenoid[1]);
    CHECK(g.pulses[0] == 3 && g.pulses[1] == 4);
    // Byte positions as the hook writes them.
    CHECK(b[0] == 'R' && b[3] == 'O' && b[4] == 1 && b[6] == 2 && b[8] == 77 && b[16] == 5 && b[17] == 8 && b[24] == 1 && b[26] == 1 && b[28] == 4 && b[48] == 77);
    CHECK(!parse_game_out(b, kGameOutSize - 1, g, &why) && why == "short");
    unsigned char c[kGameOutSize];
    std::copy(b, b + kGameOutSize, c); c[0] = 'X';
    CHECK(!parse_game_out(c, sizeof c, g, &why) && why == "magic");
    std::copy(b, b + kGameOutSize, c); c[4] = 2;
    CHECK(!parse_game_out(c, sizeof c, g, &why) && why == "version");
    std::copy(b, b + kGameOutSize, c); c[6] = 0;
    CHECK(!parse_game_out(c, sizeof c, g, &why) && why == "players");
    std::copy(b, b + kGameOutSize, c); c[6] = 5;
    CHECK(!parse_game_out(c, sizeof c, g, &why) && why == "players");
    std::copy(b, b + kGameOutSize, c); c[48] = 78;   // half-written frame
    CHECK(!parse_game_out(c, sizeof c, g, &why) && why == "torn");
}

static void test_leftover_file_ignored() {
    GameOutTracker t(500);
    GameOutFrame old = frame(40, 6, 0, 9, 2);
    auto p = t.update(0, &old);
    CHECK(p[0] == 0 && p[1] == 0);                         // its counts are a baseline
    CHECK(t.view().level[0] == 0 && t.view().motor[0] == 0 && !t.view().fresh && t.view().stale);
    p = t.update(100, &old);
    CHECK(t.view().level[0] == 0);
    GameOutFrame now = frame(41, 4, 0, 10, 2);              // the writer is alive again
    p = t.update(120, &now);
    CHECK(p[0] == 1 && p[1] == 0 && t.view().pulses[0] == 1);
    CHECK(t.view().level[0] == 0.5 && t.view().motor[0] == 4 && t.view().fresh && !t.view().stale);
}

static void test_new_file_counts_from_zero() {
    GameOutTracker t(500);
    t.update(0, nullptr);                                  // no file yet when the route starts
    CHECK(!t.view().present && t.view().level[0] == 0);
    GameOutFrame f = frame(1, 8, 1, 1, 0);
    auto p = t.update(10, &f);
    CHECK(p[0] == 1 && t.view().level[0] == 1.0 && t.view().level[1] == 0.125 && t.view().fresh);
}

static void test_stale_and_heartbeat() {
    GameOutTracker t(500);
    t.update(0, nullptr);
    GameOutFrame f = frame(1, 5, 0);
    t.update(0, &f);
    CHECK(t.view().level[0] == 5.0 / 8);
    t.update(400, &f);                                     // no update for 400 ms: still live
    CHECK(t.view().level[0] == 5.0 / 8 && !t.view().stale);
    t.update(501, &f);                                     // 501 ms: the writer is gone, forced off
    CHECK(t.view().level[0] == 0 && t.view().motor[0] == 0 && t.view().stale);
    f.seq = 2;                                             // heartbeat: back on
    t.update(510, &f);
    CHECK(t.view().level[0] == 5.0 / 8 && !t.view().stale);
    // Motor 0 needs no heartbeat and is never stale.
    f.seq = 3; f.motor[0] = 0;
    t.update(520, &f);
    t.update(5000, &f);
    CHECK(t.view().level[0] == 0 && !t.view().stale);
}

static void test_missing_file_and_restart() {
    GameOutTracker t(500);
    t.update(0, nullptr);
    GameOutFrame f = frame(1, 3, 3, 5, 7);
    t.update(0, &f);
    CHECK(t.view().pulses[0] == 5 && t.view().pulses[1] == 7);
    t.update(10, nullptr);                                 // file gone (game stopped, route cleaned up)
    CHECK(t.view().level[0] == 0 && t.view().level[1] == 0 && !t.view().present);
    GameOutFrame g = frame(1, 0, 0, 1, 0);                 // a new writer, same seq number as before
    auto p = t.update(20, &g);
    CHECK(p[0] == 1 && t.view().pulses[0] == 6);
    // Counter going down without the file disappearing: the writer restarted, its count is new.
    GameOutFrame h = frame(2, 0, 0, 0, 0);
    t.update(30, &h);
    h.seq = 3; h.pulses[0] = 2;
    p = t.update(40, &h);
    CHECK(p[0] == 2 && t.view().pulses[0] == 8);
}

static void test_invalid_keeps_baseline() {
    GameOutTracker t(500);
    GameOutFrame old = frame(9, 4, 0, 4, 4);
    t.update(0, &old);                                     // baseline from an earlier run
    t.invalid();                                           // being rewritten
    CHECK(t.view().level[0] == 0 && t.view().invalid == 1 && !t.view().present);
    t.update(10, &old);                                    // same frame again: still the baseline, nothing counted
    CHECK(t.view().pulses[0] == 0 && t.view().level[0] == 0);
    GameOutFrame now = frame(10, 4, 0, 4, 5);
    t.update(20, &now);
    t.invalid();                                           // a torn read while live: off for this poll only
    CHECK(t.view().level[0] == 0);
    auto p = t.update(24, &now);
    CHECK(p[1] == 0 && t.view().pulses[1] == 1 && t.view().level[0] == 0.5);
}

static void test_changes_and_unused_players() {
    GameOutTracker t(500);
    t.update(0, nullptr);
    GameOutFrame f = frame(1, 1, 0);
    f.players = 1;
    f.motor[1] = 8;                                        // not in use: ignored
    t.update(0, &f);
    CHECK(t.view().level[1] == 0 && t.view().changes[0] == 1);
    f.seq = 2; f.motor[0] = 5;
    t.update(10, &f);
    f.seq = 3; f.motor[0] = 0;
    t.update(20, &f);
    CHECK(t.view().changes[0] == 3 && t.view().frames == 3);
    GameOutFrame z = frame(4, 8, 0);
    z.motor_max[0] = 0;                                    // no maximum: no level
    t.update(30, &z);
    CHECK(t.view().level[0] == 0);
}

int main() {
    test_layout();
    test_leftover_file_ignored();
    test_new_file_counts_from_zero();
    test_stale_and_heartbeat();
    test_missing_file_and_restart();
    test_changes_and_unused_players();
    test_invalid_keeps_baseline();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
