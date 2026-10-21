// SPDX-License-Identifier: GPL-3.0-or-later
// Aim correction for Lost Land Adventure's gun peripheral in rg-mfcam.dll (RG_DOMEGUN_GRID, see dome_gun.h), without Windows calls, so
// that tests/test_dome_gun.cpp can test it on the host.
//
// WHY: the game does not draw its crosshair where the camera spot says. It turns the spot into a direction (0x140a2e460: the spot
// minus 0.5 times the gun camera's field of view), bends that with its own gun adjustment per corner (testmode.xml varGunAdjustP1-*,
// for example pitch 1.4 at the top corners and 1.1/1.2 at the bottom ones) and projects it onto the dome picture. Measured
// 2026-10-21 with a straight mapping: spot y 0.3 -> screen 0.04, 0.5 -> 0.44, 0.75 -> 0.70, while x stays nearly straight. The daemon
// writes where the player aims on the screen (0..1), so rg-mfcam has to send the spot that lands there.
//
// WHAT: a Grid holds the measured forward map: for each spot (u[i], v[j]) of a lattice, the screen point (0..1) where the game drew
// the crosshair. invert() finds the spot that lands on a wanted screen point with Newton steps on the bilinear interpolation of the
// lattice (outside the lattice the edge cells are extended), clamped to 0..1.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_domegun {

inline constexpr int kGridMax = 12;

struct Grid {
    int nu = 0, nv = 0;
    float u[kGridMax] = {}, v[kGridMax] = {};                          // the lattice's spots, increasing
    float sx[kGridMax * kGridMax] = {}, sy[kGridMax * kGridMax] = {};  // screen point of spot (u[i], v[j]) at j * nu + i

    bool valid() const { return nu >= 2 && nv >= 2; }

    // The screen point the game draws for spot (a, b).
    void forward(float a, float b, float& x, float& y) const {
        int i = 0, j = 0;
        while (i < nu - 2 && a > u[i + 1]) ++i;
        while (j < nv - 2 && b > v[j + 1]) ++j;
        const float s = (a - u[i]) / (u[i + 1] - u[i]), t = (b - v[j]) / (v[j + 1] - v[j]);
        const int k = j * nu + i;
        const float w00 = (1 - s) * (1 - t), w10 = s * (1 - t), w01 = (1 - s) * t, w11 = s * t;
        x = w00 * sx[k] + w10 * sx[k + 1] + w01 * sx[k + nu] + w11 * sx[k + nu + 1];
        y = w00 * sy[k] + w10 * sy[k + 1] + w01 * sy[k + nu] + w11 * sy[k + nu + 1];
    }

    // The spot (clamped to 0..1) whose screen point is (tx, ty).
    void invert(float tx, float ty, float& a, float& b) const {
        a = 0.5f;
        b = 0.5f;
        for (int it = 0; it < 32; ++it) {
            const float h = 1e-3f;
            float x, y, xa, ya, xb, yb;
            forward(a, b, x, y);
            forward(a + h, b, xa, ya);
            forward(a, b + h, xb, yb);
            const float j11 = (xa - x) / h, j21 = (ya - y) / h, j12 = (xb - x) / h, j22 = (yb - y) / h;
            const float det = j11 * j22 - j12 * j21;
            if (det > -1e-6f && det < 1e-6f) break;
            const float ex = tx - x, ey = ty - y;
            float da = (j22 * ex - j12 * ey) / det, db = (-j21 * ex + j11 * ey) / det;
            da = da > 0.25f ? 0.25f : (da < -0.25f ? -0.25f : da);
            db = db > 0.25f ? 0.25f : (db < -0.25f ? -0.25f : db);
            a += da;
            b += db;
            a = a < 0 ? 0 : (a > 1 ? 1 : a);
            b = b < 0 ? 0 : (b > 1 ? 1 : b);
            if (da < 1e-5f && da > -1e-5f && db < 1e-5f && db > -1e-5f) break;
        }
    }
};

// The grid text (per-game data next to the DLL); '#' starts a comment:
//   player 1|2       the lines that follow are for this player
//   u <spots...>     the lattice's x spots, increasing (2..kGridMax)
//   v <spots...>     the lattice's y spots, increasing (2..kGridMax)
//   row <x y ...>    one row per v (in v order), nu screen points (0..1) as x y pairs
// A player with a malformed or incomplete grid gets none (no correction). Returns the number of players with a grid.
inline int parse_grids(const char* text, Grid out[2]) {
    Grid g[2];
    int p = -1, rows[2] = {0, 0};
    const char* s = text;
    while (s && *s) {
        const char* e = s;
        while (*e && *e != '\n') ++e;
        char line[1024];
        size_t n = static_cast<size_t>(e - s);
        if (n >= sizeof line) n = sizeof line - 1;
        std::memcpy(line, s, n);
        line[n] = 0;
        s = *e ? e + 1 : e;
        for (char* c = line; *c; ++c)
            if (*c == '#' || *c == '\r') {
                *c = 0;
                break;
            }
        char* c = line;
        while (*c == ' ' || *c == '\t') ++c;
        if (!*c) continue;
        char* rest = c;
        while (*rest && *rest != ' ' && *rest != '\t') ++rest;
        const size_t kw = static_cast<size_t>(rest - c);
        auto numbers = [rest](float* dst, int maxn) {
            int k = 0;
            const char* q = rest;
            for (;;) {
                char* end = nullptr;
                const float f = std::strtof(q, &end);
                if (end == q) break;
                if (k < maxn) dst[k] = f;
                ++k;
                q = end;
            }
            return k;
        };
        if (kw == 6 && std::strncmp(c, "player", 6) == 0) {
            const long v = std::strtol(rest, nullptr, 10);
            p = (v == 1 || v == 2) ? static_cast<int>(v - 1) : -1;
        } else if (p < 0) {
            continue;
        } else if (kw == 1 && *c == 'u') {
            g[p].nu = numbers(g[p].u, kGridMax);
        } else if (kw == 1 && *c == 'v') {
            g[p].nv = numbers(g[p].v, kGridMax);
        } else if (kw == 3 && std::strncmp(c, "row", 3) == 0) {
            float xy[2 * kGridMax];
            const int k = numbers(xy, 2 * kGridMax);
            if (rows[p] < 0 || g[p].nu < 2 || g[p].nu > kGridMax || k != 2 * g[p].nu || rows[p] >= g[p].nv || rows[p] >= kGridMax) {
                rows[p] = -1000;   // malformed: this player gets no grid
                continue;
            }
            for (int i = 0; i < g[p].nu; ++i) {
                g[p].sx[rows[p] * g[p].nu + i] = xy[2 * i];
                g[p].sy[rows[p] * g[p].nu + i] = xy[2 * i + 1];
            }
            ++rows[p];
        }
    }
    int count = 0;
    for (int q = 0; q < 2; ++q) {
        bool ok = g[q].nu >= 2 && g[q].nu <= kGridMax && g[q].nv >= 2 && g[q].nv <= kGridMax && rows[q] == g[q].nv;
        for (int i = 1; ok && i < g[q].nu; ++i) ok = g[q].u[i] > g[q].u[i - 1];
        for (int j = 1; ok && j < g[q].nv; ++j) ok = g[q].v[j] > g[q].v[j - 1];
        out[q] = ok ? g[q] : Grid{};
        count += ok ? 1 : 0;
    }
    return count;
}

}  // namespace rg_domegun
