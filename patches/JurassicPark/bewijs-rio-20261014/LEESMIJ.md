# Jurassic Park — the RIO board is answered (board seen); the game then does not render (why: not known)

**Date:** 2026-10-14. This is the Jurassic Park half of the RIO item (`patches/RIO-BORD.md`), and it is a
**different, and smaller, claim than NERF**. For NERF the board is answered and the banner is gone, in a
frame. For JP there are two parts, and the second is unfinished — **I corrected a wrong claim here, see
below.**

## Part 1 — the board is seen (TRUE, evidenced)

`RIO_Connected` (at `0x836b320` in `game`) was patched to report connected (`jp-game-rio-patch.sh`).
JP writes "RIO Disconnected" / "STR_ERRORMSG_RIO_DISCONNECTED" to its own
`pmuser/errorlog.txt` on every boot. That file carries those lines for **every 2026-10-13 run and for
NONE of tonight's patched runs** — so the patch stops the RIO-disconnected state. The board is seen.

## Part 2 — the game does not render attract with the patch (cause NOT determined)

jr1/jr2 (2026-10-14) show **black frames**; the earlier unpatched runs reached attract (with the banner).
So forcing RIO connected appears to stop JP reaching its attract render. **What stops it is not pinned.**

**A wrong claim, corrected.** An earlier version of this note and of the commit said the wall was a
`hasp_decrypt(792)` loop "only reached once the cabinet is present." I checked that against the logs and
it is **false**: the 792-byte decrypt appears in 2026-10-13 runs too (e.g. `…-225845.log` has 52 of
them), **without** the patch — so it is not the differentiator. jr2's game-side log ends on the **same
four `shader108` compile asserts the successful attract run also has** (`g5/shader_gl.c:2180`), which are
known and not fatal there. So the render blocker under the patch is **not determined**; it needs a
controlled comparison (patched vs unpatched in the same harness, long enough to render), which needs the
bench.

## What xico/msr100 is (it is NOT the blocker)

The **Xico 7104USB**, a USB magnetic **card reader** (`g5/cardreader.c`, `msr100_change_to_HID_mode`,
`CardReaderSetCallback`, "creditCardSwipes"). Same *shape* as the RIO board (a USB device). The game
handles its absence gracefully (`xicoOpenPort(): Error` / `msr100OpenPort(): Error`, then continues), so
it is not where JP stalls.

## Bottom line

For JP the board is **answered (seen)**; the patch does **not** yet yield a banner-gone attract frame —
the game does not render, and why is **not yet known**. The RIO-disconnected state is gone (an advance),
but this is **not** the NERF result and **not** a finished "wall moved to the dongle" claim.
