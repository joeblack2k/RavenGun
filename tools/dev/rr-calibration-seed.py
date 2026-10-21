#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the gun calibration seed of Raccoon Rampage (patches/RaccoonRampage/seed/Setting_Hardware.ini and ..._Backups.ini).

The game keeps its calibration in Setting_Hardware.ini ([/Script/RSGame.SettingData]): per player 1..4 a 4x4 grid of cells,
each cell with its four corners (LT, RT, LB, RB) as raw gun values X and Y, and GunCalibrate_bIsHandleVerify_<n>P = 1 once
that player is calibrated. The game's calibration screen shoots a 5x5 grid of targets at x = 120, 540, 960, 1380, 1800 and
y = 120, 330, 540, 750, 960 on its 1920x1080 picture. rg-unis hands the game the aim as a byte (0..255 over the whole screen,
round(f * 255)) in both halves of a 16-bit value, i.e. byte * 257. The raw value at a target is therefore round(px / size * 255) * 257.
Measured 2026-10-21 (claude-calib): player 2 calibrated in the game's own menu with test-pattern points at those targets wrote
exactly these 128 values (0 differences). The game writes the players in the order 4, 3, 2, 1, the cells 15..0 and the corners
LB, RB, RT, LT (Y before X), with CR LF line ends and no final line end; this script reproduces that.

usage: rr-calibration-seed.py OUTDIR
"""
import sys
from pathlib import Path

W, H = 1920, 1080
XS = [round(px / W * 255) * 257 for px in (120, 540, 960, 1380, 1800)]
YS = [round(px / H * 255) * 257 for px in (120, 330, 540, 750, 960)]


def build():
    lines = ["[/Script/RSGame.SettingData]"]
    for player in (4, 3, 2, 1):
        for cell in range(15, -1, -1):
            row, col = divmod(cell, 4)
            corner = {"LT": (col, row), "RT": (col + 1, row), "LB": (col, row + 1), "RB": (col + 1, row + 1)}
            for name in ("LB", "RB", "RT", "LT"):
                cx, cy = corner[name]
                lines.append(f"Calibrate_{player}P_{cell}_{name}_Y={YS[cy]}")
                lines.append(f"Calibrate_{player}P_{cell}_{name}_X={XS[cx]}")
    for player in (4, 3, 2, 1):
        lines.append(f"GunCalibrate_bIsHandleVerify_{player}P=1")
    return "\r\n".join(lines).encode()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    data = build()
    for name in ("Setting_Hardware.ini", "Setting_Hardware_Backups.ini"):
        (out / name).write_bytes(data)
    print(f"{len(data)} bytes -> {out}")
