# NERF — the RIO board answered, the "RIO DISCONNECTED" banner gone

**Date:** 2026-10-14. **Result: the banner is gone, in a frame.** On NERF the Raw Thrills RIO board is
answered in software and the red "RIO DISCONNECTED" that was on every frame of the earlier run is no
longer there. **Evidence to look at:** `rio-banner-boven-voor-onder-na.jpg` — top = run n5 (before, the
banner across the top); bottom = run nr1 (after, a clean top edge, attract running). Also
`nr1-sheet.jpg` (the whole run, no banner on any frame) and `nr1-banner-gone-*.jpg`.

This is the first half of the two-title RIO item (`patches/RIO-BORD.md`). It is **NERF only**; Jurassic
Park is a different claim (see that file).

## What was measured, and what the fix is

* **The board is a USB device** `0c70:0780`, opened through libusb by `pmrt_usb_open` inside the game's
  own `librio.so`. **MEASURED** (disassembly): `RIO_Connect` sets the connected flag the instant
  `pmrt_usb_open` returns non-NULL — no handshake, no transfer first. The game polls `RIO_Connected`
  (`0xa300`), which returns `0` when that flag is set ("connected") and `-3` when it is 0 (the code JP
  logs as "RIO not connected"). The banner is drawn on the `-3`.
* **The fix:** a local `librio.so` with `RIO_Connected` changed to `xor eax,eax; ret` (three bytes at
  file offset `0xa300`) — it always reports connected. `RIO_Connect` still runs the real
  `pmrt_usb_open(0x0c70,0x0780)`; it fails (no board) and **no read thread starts**, so nothing reads a
  report that is not there. The game sees connected and drops the banner. The dump's `librio.so` is only
  read; the share is never written.
* **Reproduce:** `nerf-librio-rio-patch.sh` builds the patched `librio.so` from the dump's own file (it
  refuses if that file is a different version) and prints its sha256. The run bound it over both plugin
  paths the game loads (`Nerf_Data/Plugins/x86_64` and `…/x86_64_ori`, each loads a copy) and ran the
  native route otherwise unchanged:
  ```
  scripts/play-native.sh --game NERF --gamescope \
    --bind-ro <patched librio.so> /pm/Nerf_Data/Plugins/x86_64/librio.so \
    --bind-ro <patched librio.so> /pm/Nerf_Data/Plugins/x86_64_ori/librio.so \
    --bind-ro patches/NERF/data/udev-rules.d /etc/udev/rules.d
  ```

## What this is NOT

* **No inputs yet.** Reporting "connected" clears the banner; it does not make the gun, start or coins
  work. For that the board's **reports** must be answered (the read thread fed with report bytes). The
  report format is readable from `librio.so`'s exported names (`RIO_GetReport`, `RIO_SampleInput`,
  `RIO_SW_Count`, `RIO_ADC_Val`) and is the next piece, not this one.
* **The patched `librio.so` is a bench artifact,** staged at
  `~/.local/share/ravengun/rio/NERF/librio.so` (not in this repo; the 3-byte patch and its build script
  are). Wiring it into the native profile so the owner's launcher uses it is a follow-up.

## Checksums

| piece | sha256 |
|---|---|
| dump `librio.so` (read only) | `a32fb2e61b0e2a6bf281b4e4f7426775b09c66c785a32cffe23eb4fa9fd0b605` |
| patched `librio.so` (3 bytes at `0xa300` → `31 c0 c3`) | `51ec8ad6c84d0cff…` (from the build script) |
| `Nerf.x86_64` | `1466297fa6b1eaf54dc27311641b906b65539b9db8122f83a836ab6fdc5b5371` |
