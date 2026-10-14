# Jurassic Park — the RIO board answered, the "RIO Disconnected" banner GONE, in a frame

**Date:** 2026-10-14. This is the Jurassic Park half of the RIO item (`patches/RIO-BORD.md`), and it is
**delivered**: the board is answered and the banner is gone, in a frame — the same result as NERF.
**Evidence:** `rio-banner-boven-voor-onder-na.jpg` (top = unpatched, the gameplay demo with "RIO
Disconnected"; bottom = patched, the same kind of gameplay demo with a clean top edge, no banner);
`jpF-banner-gone-frame.jpg`.

This file replaces two earlier wrong readings I made tonight (both retracted below). The fix and the
truth took a controlled comparison and then a missing harness bind to reach.

## The fix

`RIO_Connected` (at `0x836b320` in `game`) patched to report connected (`jp-game-rio-patch.sh`, three
bytes `55 89 e5` → `31 c0 c3`). With the game rendering correctly, the patched attract loop runs with
**no "RIO Disconnected" on any frame** (run jpF: volcano intro, high scores, "RETURN TO THE ISLAND", the
dinosaur gameplay demos with "PRESS START TO BEGIN" — all clean). The game logs no "RIO Disconnected"
this run (unpatched run jpE, same recipe, does — the controlled half, below).

## It took isolating a render regression that was NOT the patch

My first runs (jr1/jr2, jpA/jpB) rendered **black**, and I twice guessed wrong about why (a "dongle
hasp_decrypt(792) wall," then "the patch stops the render"). A controlled comparison killed both:

* **Controlled, one variable (jpA unpatched vs jpB patched, identical harness, 3 bytes different):** arm A
  logs "RIO Disconnected", arm B does not — so the patch does remove the RIO state (board seen) — **and
  both arms are black**, so the black screen is **not** the patch.
* **The black screen was my harness.** The game loads its state-machine programs from
  `/pm/g5/dino/data/programs_enc`; the decrypted copies in `programs_dec/` must be **bound over** that
  path (JP's own `ATTRACT-RUN.md §4`: `--bind-ro <share>/Jurassic Park/programs_dec
  /pm/g5/dino/data/programs_enc`, plus `--env SDL12COMPAT_OPENGL_SCALING=0`). The jp-bench harness I
  reused for the RIO runs was missing that bind, so the game read the encrypted programs, failed to load
  them (`Silent failure loading program 'CircleFail'`, 3.7 million times, all tonight, none on the
  2026-10-13 runs that rendered), and drew nothing. With the bind restored, the program-load failures go
  to **0** and JP renders — unpatched (with the banner) and patched (without it).

## Retracted (for the record)

1. "The wall moved to a `hasp_decrypt(792)` dongle loop only reached once the cabinet is present." False:
   the 792-decrypt occurs in unpatched runs too.
2. "The patch stops JP's render." False: the unpatched control is equally black; the black screen was the
   missing `programs_dec` bind, not the patch.

`CircleFail` is a game **program** (`programs_dec/perm/CircleFail.txt`), not a shader — "Silent failure
loading program" is a program-load failure, which is why `programs_dec` fixes it.

## What xico/msr100 is (still true, still not a blocker)

The **Xico 7104USB**, a USB magnetic **card reader** (`g5/cardreader.c`). The game handles its absence
gracefully; it was never the wall.
