# Jurassic Park — the RIO board is seen (controlled); the black screen is NOT the patch (also controlled)

**Date:** 2026-10-14. This is the Jurassic Park half of the RIO item (`patches/RIO-BORD.md`). It is a
**different, and smaller, claim than NERF**: for NERF the board is answered and the banner is gone, in a
frame. For JP a **controlled comparison** (patched vs unpatched, one variable) settles two things — and
it corrected a wrong story I had told first (the "dongle-792 wall", retracted below).

## The comparison (one thing different)

Two arms, identical harness, both binding a **local** copy of `game` over `/pm/game`, differing in
**exactly three bytes** (`RIO_Connected` at file offset `0x323320`: `55 89 e5` → `31 c0 c3`). Arm A
unpatched (ran 04:23), arm B patched (ran 04:26), 180 s each. Both loaded cleanly (slot table 576 slots,
jump to `e_entry`) — so neither is an invalid run. Evidence: `jpAB-controlled-comparison.txt`.

## Result 1 — the board is SEEN (the patch works at the state level) ✓

**Controlled:** arm A (unpatched) writes `Wed Oct 14 04:23:22 RIO Disconnected` and
`STR_ERRORMSG_RIO_DISCONNECTED` to its own `pmuser/errorlog.txt`; arm B (patched) writes **neither**. One
variable, opposite outcomes — the three-byte `RIO_Connected` patch is what removes the RIO-disconnected
state. So JP's RIO board **is** answered at the state level.

## Result 2 — the black screen is NOT the patch (also controlled)

**Both** arms render **pure black** (every game frame; `jpA-black-frame-unpatched.jpg` is the unpatched
arm). So forcing RIO connected does **not** cause the black screen — the unpatched control is equally
black. The no-render is **independent of the patch**. Both arms hit the same `shader108` compile failures
(`ec=-9596`, `g5/shader_gl.c:2180`); that render failure, not RIO, is why there is no picture tonight —
and it is a **separate problem** (the witnessed attract run of 2026-10-13 rendered; something in this
harness/session regressed since).

## What this means for the bar

JP's RIO fix **works** (board seen, controlled). But a **banner-gone frame cannot be produced for JP**
until JP renders a picture in this harness again — and that is blocked by the render failure (shader108),
**not** by RIO. So: RIO answered for JP at the log level; the frame is owed by a separate render fix.

## Retracted

An earlier version said the patch pushed JP into a `hasp_decrypt(792)` loop "only reached once the
cabinet is present," a dongle wall. **False** — the 792-decrypt occurs in unpatched runs too, and the
controlled comparison shows both arms black regardless. The dongle is not the wall here; the render is,
and the render is not the patch.

## What xico/msr100 is (still true, not the blocker)

The **Xico 7104USB**, a USB magnetic **card reader** (`g5/cardreader.c`, `msr100_change_to_HID_mode`).
The game handles its absence gracefully and continues; not where JP stalls.
