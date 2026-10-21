// SPDX-License-Identifier: GPL-3.0-or-later
// The gun peripheral of Lost Land Adventure on its COM port, emulated by rg-mfcam.dll (see THIRTEENTH TASK in rg_mfcam.cpp), without
// Windows calls, so that tests/test_dome_gun.cpp can test it on the host.
//
// What the game does (measured on LLA/Binaries/Win64/DomeShooterGame-Win64-Shipping.exe, objdump):
//   - 0x140a2eae0 opens L"COM2" (the string at 0x1413e1eb0) with CreateFileW(GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING),
//     GetCommState, then SetCommState with BaudRate 0x1c200 (115200), ByteSize 8, no parity.
//   - 0x140a2edb0 is the poll: ClearCommError(handle, &errors, &comstat). Fewer than 12 bytes in comstat.cbInQue counts a miss; after
//     more than 60 misses in a row it forgets the handle (0x140a2ee1d: handle = -1, the gun is gone). With more than 36 bytes waiting it
//     drops whole 12-byte blocks first; then it reads one byte at a time until '_' (0x5f) and the 11 bytes after it:
//       [0] '_'  [1] player 0 or 1  [2..5] float x  [6..9] float y  [10] button bits  [11] 0xf5
//     A frame with another end byte or player is skipped.
//   - x and y are the spot in the gun camera's picture, 0..1 with 0.5 the centre: 0x140a2e460 turns (x - 0.5) and (y - 0.5) times the
//     camera's field of view (degrees * pi / 180) into a direction.
//   - The button byte is shared by both guns (one global, 0x14181ee8c). The game's memory shared with the launcher
//     (RSSharedData) gets `bits & 3 != 0` for player 1 and `bits & 0xc != 0` for player 2 (0x140a2c567, 0x140a2c580); each player's
//     button is pressed when `bits & mask == mask` with a mask the game script sets (SetGunPeripheralRawButtonMasks, 0x1416b2240).
//   - After each poll it writes 2 bytes back, one per player, from the table at 0x1413e1e64: player 1 'n' 'f' 'm', player 2 'N' 'F' 'M'
//     (the gun's feedback state 0..2).
// The picture spot comes from the daemon's shared memory (tp-shm, layout LostLandAdventures in packaging/tp-shm-profiles.json):
// one byte per axis, 0..255, at 12/16 (player 1) and 20/24 (player 2); the triggers are bits 0 and 1 of the control word at 8.
// That byte is where the player aims on the screen; with a grid (RG_DOMEGUN_GRID, dome_grid.h) the frame carries the spot whose
// crosshair the game draws there, without one the byte itself.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "dome_grid.h"

namespace rg_domegun {

inline constexpr size_t kFrame = 12;
inline constexpr uint8_t kSync = 0x5f;   // '_'
inline constexpr uint8_t kEnd = 0xf5;
inline constexpr size_t kQueueMax = 36;  // the game drops whole blocks above this; never offer more

// The shm byte (0..255) as the camera coordinate the game expects (0..1).
inline float axis(uint8_t v) { return static_cast<float>(v) / 255.0f; }

// The button byte for both guns from the control word: player 1's trigger is bit 0, player 2's bit 2 (each player's pair of bits,
// see the shared-memory flags above).
inline uint8_t buttons(uint32_t control) {
    uint8_t b = 0;
    if (control & 0x1u) b |= 0x01;
    if (control & 0x2u) b |= 0x04;
    return b;
}

// One frame as the game reads it.
inline void make_frame(uint8_t out[kFrame], uint8_t player, float x, float y, uint8_t bits) {
    out[0] = kSync;
    out[1] = player;
    std::memcpy(out + 2, &x, 4);
    std::memcpy(out + 6, &y, 4);
    out[10] = bits;
    out[11] = kEnd;
}

// The bytes the emulated port has ready for the game. refill() adds one frame per player when less than one frame is waiting and at
// least `period_ms` passed since the last refill; read() hands them out like ReadFile.
struct Port {
    uint8_t q[kQueueMax + 2 * kFrame];
    size_t n = 0;
    uint32_t last_ms = 0;
    bool primed = false;
    const Grid* grid[2] = {nullptr, nullptr};   // RG_DOMEGUN_GRID: per player the measured map from spot to screen (dome_grid.h)

    // shm: the 64 bytes of the shared state, or nullptr (then both guns aim at the centre, no buttons).
    void refill(uint32_t now_ms, uint32_t period_ms, const uint8_t* shm) {
        if (n >= kFrame) return;
        if (primed && now_ms - last_ms < period_ms) return;
        primed = true;
        last_ms = now_ms;
        uint32_t control = 0;
        uint8_t ax[4] = {128, 128, 128, 128};
        if (shm) {
            std::memcpy(&control, shm + 8, 4);
            ax[0] = shm[12];
            ax[1] = shm[16];
            ax[2] = shm[20];
            ax[3] = shm[24];
        }
        const uint8_t bits = buttons(control);
        for (uint8_t p = 0; p < 2 && n + kFrame <= sizeof q; ++p) {
            const float sx = axis(ax[2 * p]), sy = axis(ax[2 * p + 1]);
            float x = sx, y = sy;
            if (grid[p] && grid[p]->valid() && shm) grid[p]->invert(sx, sy, x, y);   // the spot whose crosshair lands on (sx, sy)
            make_frame(q + n, p, x, y, bits);
            n += kFrame;
        }
    }
    size_t read(uint8_t* out, size_t want) {
        const size_t k = want < n ? want : n;
        if (out && k) std::memcpy(out, q, k);
        std::memmove(q, q + k, n - k);
        n -= k;
        return k;
    }
};

// The feedback byte the game wrote for `player` (0 or 1) as 0..2, or -1 when it is not one of the table's letters.
inline int feedback_level(uint8_t c, int player) {
    const char* t = player == 0 ? "nfm" : "NFM";
    for (int i = 0; i < 3; ++i)
        if (c == static_cast<uint8_t>(t[i])) return i;
    return -1;
}

}  // namespace rg_domegun
