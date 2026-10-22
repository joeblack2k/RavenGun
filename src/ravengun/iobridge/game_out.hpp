#pragma once
// Game outputs written by a hook inside the game process ("game-out" file), read by the IO bridge to drive the PS Move
// rumble from the game itself. First user: Luigi's Mansion Arcade, whose rg-pe-hook.dll board emulation
// (tools/rt-hasphook/rg_capcom_board.c) copies the game's own vacuum-gun motor level (MO, 0..8) and solenoid (SO) per
// player into this file. A game declares it with `[Input] Rumble = game` in its game.ini (route tp-shm).
//
// File layout (64 bytes, little-endian; the writer rewrites the whole file at offset 0):
//    0  char[4]  magic "RGGO"
//    4  u16      version (1)
//    6  u16      players in use (1..4)
//    8  u32      sequence (begin): +1 on every write
//   12  u32      reserved (0)
//   16  4 x 8    per player: u8 motor level, u8 motor maximum (level / maximum = strength 0..1), u8 solenoid (0/1),
//                u8 reserved, u32 solenoid rising edges since the writer started
//   48  u32      sequence (end): equal to the begin sequence when the frame is complete (no torn read)
//   52  12 bytes reserved
// The writer writes on every change and at least every ~100 ms while a motor or solenoid is on (heartbeat), so a
// reader can tell a live "motor on" from one left behind by a crashed game.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ravengun/iobridge/io_state.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kDefaultGameOutPath = "/dev/shm/ravengun_GameOut";
inline constexpr size_t kGameOutSize = 64;
inline constexpr uint16_t kGameOutVersion = 1;
inline constexpr int64_t kGameOutStaleMs = 500;

struct GameOutFrame {
    uint32_t seq = 0;
    int players = 0;
    std::array<int, kIoPlayers> motor{}, motor_max{};
    std::array<bool, kIoPlayers> solenoid{};
    std::array<uint32_t, kIoPlayers> pulses{};
};

// Parses one file image; false (with the reason in `why`) for a short, foreign, newer or torn frame.
bool parse_game_out(const unsigned char* b, size_t n, GameOutFrame& f, std::string* why = nullptr);
// The writer's side (used by the tests; the hook writes the same layout in C).
void encode_game_out(const GameOutFrame& f, unsigned char out[kGameOutSize]);

// What the daemon makes of the file over time.
struct GameOutView {
    std::array<double, kIoPlayers> level{};       // 0..1, the strength of the continuous rumble (0 when stale or absent)
    std::array<int, kIoPlayers> motor{}, motor_max{};
    std::array<bool, kIoPlayers> solenoid{};
    std::array<uint64_t, kIoPlayers> pulses{};    // solenoid rising edges seen since the tracker started
    std::array<uint64_t, kIoPlayers> changes{};   // motor level changes seen since the tracker started
    bool present = false;   // a valid frame was read on the last poll
    bool fresh = false;     // the sequence changed within the stale time
    bool stale = false;     // a motor is on in the file, but the writer stopped updating: forced off
    uint32_t seq = 0;
    uint64_t frames = 0;    // sequence changes seen
    uint64_t invalid = 0;   // polls with a file that did not parse
};

// Pure state machine: feed it the frame from each poll (nullptr = no file or no valid frame) and the time.
// Safety rules (tests/test_game_out.cpp):
//  - A file that is already there when the tracker starts belongs to an earlier run until its sequence changes: its
//    motor levels and pulse counts are ignored (baseline). A file that appears later is new: counted from zero.
//  - The motor level only counts while the sequence changed within `stale_ms`; otherwise the level is 0.
//  - No file or an invalid file: level 0. Only a missing file resets the baseline (the next file is a new writer).
class GameOutTracker {
public:
    explicit GameOutTracker(int64_t stale_ms = kGameOutStaleMs) : stale_ms_(stale_ms) {}
    // Returns the number of new solenoid pulses per player since the previous poll. nullptr = there is no file.
    std::array<uint32_t, kIoPlayers> update(int64_t now_ms, const GameOutFrame* f);
    // A file that is there but did not parse (short, torn, being rewritten): everything off for this poll, but the
    // baseline stays, so the next good frame is not counted as a new writer.
    void invalid();
    const GameOutView& view() const { return v_; }
    void reset() { *this = GameOutTracker(stale_ms_); }

private:
    int64_t stale_ms_;
    bool polled_ = false, have_ = false;
    uint32_t seq_ = 0;
    int64_t changed_ms_ = 0;
    bool changed_once_ = false;
    std::array<uint32_t, kIoPlayers> last_pulses_{};
    std::array<int, kIoPlayers> last_motor_{};
    GameOutView v_;
};

}  // namespace ravengun::iobridge
