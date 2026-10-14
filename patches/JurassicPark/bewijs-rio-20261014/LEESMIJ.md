# Jurassic Park — the RIO board answered; the wall moved to the dongle (NOT a banner-gone frame yet)

**Date:** 2026-10-14. This is the Jurassic Park half of the RIO item (`patches/RIO-BORD.md`), and it is a
**different claim from NERF**. For NERF the board is answered and the banner is gone, in a frame. For JP:

* **The board is answered.** `RIO_Connected` (at `0x836b320` in `game`) was patched to report connected
  (`jp-game-rio-patch.sh`). The old `InputInit(): RIO not connected (error code: -3)` line is **gone**.
* **The game then moved forward**, further than it ever had: past `InputInit`, past the card reader
  (`xicoOpenPort`/`msr100OpenPort` — the Xico 7104USB USB card reader, handled gracefully), into a loop
  of `hasp_decrypt (lengte=792)` — and it **renders black** (`jr2-log-rio-connected-dan-dongle-lus.txt`).
* **So JP's banner is a mode gate, not a flag.** Clearing it takes the game out of its safe "RIO
  Disconnected" attract into full-cabinet init, which stalls at a HASP dongle 792-byte decrypt our
  emulator does not satisfy (it answered the 512/256 decrypts that reach attract, not this one). JP's
  remaining wall after RIO is the **dongle** (`EERSTE-PLT-SLOTS.md §6`), not the card reader, not more RIO.

**No banner-gone frame for JP** — the game does not render in this state. The RIO wall is cleared; the
dongle is the new wall.
