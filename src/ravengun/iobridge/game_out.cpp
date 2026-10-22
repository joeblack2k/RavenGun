#include "ravengun/iobridge/game_out.hpp"

#include <algorithm>
#include <cstring>

namespace ravengun::iobridge {

namespace {
uint16_t rd16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void wr16(unsigned char* p, uint16_t v) { p[0] = static_cast<unsigned char>(v); p[1] = static_cast<unsigned char>(v >> 8); }
void wr32(unsigned char* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}
bool fail(std::string* why, const char* m) {
    if (why) *why = m;
    return false;
}
}  // namespace

bool parse_game_out(const unsigned char* b, size_t n, GameOutFrame& f, std::string* why) {
    if (n < kGameOutSize) return fail(why, "short");
    if (std::memcmp(b, "RGGO", 4) != 0) return fail(why, "magic");
    if (rd16(b + 4) != kGameOutVersion) return fail(why, "version");
    const int players = rd16(b + 6);
    if (players < 1 || players > kIoPlayers) return fail(why, "players");
    const uint32_t seq = rd32(b + 8);
    if (rd32(b + 48) != seq) return fail(why, "torn");
    f = GameOutFrame{};
    f.seq = seq;
    f.players = players;
    for (int p = 0; p < players; ++p) {
        const unsigned char* q = b + 16 + 8 * p;
        f.motor[static_cast<size_t>(p)] = q[0];
        f.motor_max[static_cast<size_t>(p)] = q[1];
        f.solenoid[static_cast<size_t>(p)] = q[2] != 0;
        f.pulses[static_cast<size_t>(p)] = rd32(q + 4);
    }
    return true;
}

void encode_game_out(const GameOutFrame& f, unsigned char out[kGameOutSize]) {
    std::memset(out, 0, kGameOutSize);
    std::memcpy(out, "RGGO", 4);
    wr16(out + 4, kGameOutVersion);
    wr16(out + 6, static_cast<uint16_t>(f.players));
    wr32(out + 8, f.seq);
    for (int p = 0; p < f.players && p < kIoPlayers; ++p) {
        unsigned char* q = out + 16 + 8 * p;
        q[0] = static_cast<unsigned char>(f.motor[static_cast<size_t>(p)]);
        q[1] = static_cast<unsigned char>(f.motor_max[static_cast<size_t>(p)]);
        q[2] = f.solenoid[static_cast<size_t>(p)] ? 1 : 0;
        wr32(q + 4, f.pulses[static_cast<size_t>(p)]);
    }
    wr32(out + 48, f.seq);
}

void GameOutTracker::invalid() {
    polled_ = true;
    ++v_.invalid;
    v_.present = v_.fresh = v_.stale = false;
    v_.level = {};
    v_.motor = {};
    v_.solenoid = {};
    last_motor_ = {};
}

std::array<uint32_t, kIoPlayers> GameOutTracker::update(int64_t now_ms, const GameOutFrame* f) {
    std::array<uint32_t, kIoPlayers> fresh_pulses{};
    const bool first_poll = !polled_;
    polled_ = true;
    if (!f) {
        // No (valid) file: everything off. A file that shows up later is a new writer: count it from zero.
        have_ = false;
        last_pulses_ = {};
        v_.present = v_.fresh = v_.stale = false;
        v_.level = {};
        v_.motor = {};
        v_.solenoid = {};
        last_motor_ = {};
        return fresh_pulses;
    }
    v_.present = true;
    if (!have_) {
        have_ = true;
        seq_ = f->seq;
        if (first_poll) {
            // Left over from an earlier run (or a daemon restart mid-game): a baseline until the sequence changes.
            last_pulses_ = f->pulses;
            changed_once_ = false;
        } else {
            // A new writer: this frame is live.
            changed_ms_ = now_ms;
            changed_once_ = true;
            ++v_.frames;
            for (int p = 0; p < kIoPlayers; ++p) fresh_pulses[static_cast<size_t>(p)] = f->pulses[static_cast<size_t>(p)];
            last_pulses_ = f->pulses;
        }
    } else if (f->seq != seq_) {
        seq_ = f->seq;
        changed_ms_ = now_ms;
        changed_once_ = true;
        ++v_.frames;
        for (size_t p = 0; p < static_cast<size_t>(kIoPlayers); ++p) {
            // A counter that went down means the writer restarted: its count is all new.
            fresh_pulses[p] = f->pulses[p] >= last_pulses_[p] ? f->pulses[p] - last_pulses_[p] : f->pulses[p];
        }
        last_pulses_ = f->pulses;
    }
    v_.seq = seq_;
    v_.fresh = changed_once_ && now_ms - changed_ms_ <= stale_ms_;
    bool any_on = false;
    for (size_t p = 0; p < static_cast<size_t>(kIoPlayers); ++p) {
        const bool in_use = static_cast<int>(p) < f->players;
        const int m = in_use ? f->motor[p] : 0;
        const int mx = in_use ? f->motor_max[p] : 0;
        any_on = any_on || m > 0;
        v_.motor[p] = v_.fresh ? m : 0;
        v_.motor_max[p] = mx;
        v_.solenoid[p] = v_.fresh && in_use && f->solenoid[p];
        v_.level[p] = (v_.fresh && mx > 0) ? std::clamp(static_cast<double>(m) / mx, 0.0, 1.0) : 0.0;
        if (v_.motor[p] != last_motor_[p]) {
            ++v_.changes[p];
            last_motor_[p] = v_.motor[p];
        }
        v_.pulses[p] += fresh_pulses[p];
    }
    v_.stale = !v_.fresh && any_on;
    return fresh_pulses;
}

}  // namespace ravengun::iobridge
