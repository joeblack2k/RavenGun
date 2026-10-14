# RIO-BORD — one open item shared by two titles: the Raw Thrills RIO board

**Written:** 2026-10-14. **Titles:** Jurassic Park Arcade and NERF Arcade. Both run their attract loop on
our own launcher, and both put the same sentence on every frame: **"RIO Disconnected" / "RIO
DISCONNECTED"**. It is one missing piece of hardware, so it is one open item: fixing it once counts
twice.
**Labels:** **MEASURED** = read from the files or from a run; **DERIVED** = concluded; **NOT DETERMINED**.

## What is measured

| | Jurassic Park | NERF Arcade |
|---|---|---|
| binary | `game`, ELF 32-bit, the RIO code linked in | `Nerf.x86_64`, ELF 64-bit; the RIO code is `librio.so` (434 exports: `RIO_Init`, `RIO_Connect`, `RIO_GetReport`, `RIO_SampleInput`, `RIO_SW_Count`, `RIO_ADC_Val`, `RIO_GunModeSet`, `RIO_CoinMeterAdd`, …) |
| the call that opens the board | `pmrt_usb_open(0x0c70, 0x0780)`, twice (the instruction pair `movl $0x780,4(%esp); movl $0xc70,(%esp)` at `0x836b178` and `0x836b245`) | `pmrt_usb_open(0x0c70, 0x0780)` in `RIO_Connect` and in `RIO_ConnectEx` |
| how it reaches USB | libusb linked in; strings `libusb_open`, `/dev/bus/usb` | libusb linked into `librio.so`; strings `libusb_*`, `/dev/bus/usb`, "could not find usbfs" |
| what the game says without it | log: `InputInit(): RIO not connected (error code: -3)`; banner "RIO Disconnected" | banner "RIO DISCONNECTED"; nothing in its log |
| what else it shows | runs its attract loop (12 min, 2026-10-13) | runs its attract loop (12 min 46 s, 2026-10-14) |

**So both games look for the same USB device — vendor `0x0C70`, product `0x0780` — with the same
library code.** The board's bootloader is opened as vendor `0x15A2` (`RIO_BL_Connect`), and the library
names the microcontroller in its exports (`rio_jm60_erase_range`).

NERF also carries `libRIO2.so`, the second-generation board, which is a **serial** device
(`rio2_serial_open`, `/dev/%s`, `tcsetattr`). Halo: Fireteam Raven uses that one; its inputs were done
differently (a shim that writes the game's switch table) and it is not part of this item.

## What the board is to the games

START, coin, TEST/service and, as far as is known, the guns. Without it both titles **demo and cannot be
played** on our launcher. No input of any kind has been sent to either title on this route.

## Where one fix could attach — NOT DETERMINED which

1. **A virtual USB device `0c70:0780`** that libusb finds under `/dev/bus/usb`. Both games would see it
   unmodified, and so would any other RIO title. It needs a virtual host controller in the kernel
   (`vhci_hcd`/usbip, or `dummy_hcd` with a gadget) and root; whether SteamOS ships those modules was
   not checked. The daemon's existing virtual devices are `uhid` devices, which are not USB devices and
   which libusb does not list.
2. **At the library:** for NERF a replacement `librio.so` with the `RIO_*` functions the game calls; for
   Jurassic Park hooks on the same functions inside its executable. One model of the board, two
   attachment points.

Either way the first piece of work is the same and is shared: **the board's report format** — which
bits are the switches and coins, where the gun values are, what the game sends back (lamps, solenoids,
coin meter) and what "connected" means to `RIO_Connected`. It can be read from `librio.so`, which is
not stripped of its exported names; the same code is in Jurassic Park's executable.

## Not determined

* The report format (above), and whether the board answers a challenge (`g_rio_encryption`,
  `RIO_EnableEncryption` are exported).
* Whether the guns of either cabinet go through the RIO or through a separate device.
* Which other titles on the list use the first-generation RIO.

---

## Result — 2026-10-14 (two titles, two different claims; do not blur them)

**The board is a USB device `0c70:0780` we can answer in software.** MEASURED: `RIO_Connect` sets the
connected flag the instant `pmrt_usb_open` returns non-NULL — no handshake. The game polls
`RIO_Connected`, which returns `0` (connected) when the flag is set and `-3` (the "RIO not connected"
code) when it is 0; the banner is drawn on the `-3`. A three-byte change to `RIO_Connected`
(`xor eax,eax; ret`) reports connected. NERF carries the RIO code in `librio.so` (symbols exported →
patch a local copy of the lib); Jurassic Park has it inlined in `game` with local symbols
(not interposable → patch a local copy of the image). Same model, two attachment points.

### NERF — the board is answered and the banner is GONE. ✓ (done)

Run nr1 (2026-10-14): patched `librio.so` bound over both plugin paths; the attract loop runs with **no
"RIO DISCONNECTED" banner on any frame** (`patches/NERF/bewijs-rio-20261014/`,
`rio-banner-boven-voor-onder-na.jpg`: the same scene with the banner before and without it after).
NERF only *checks* the connected flag; clearing it is the whole fix for the banner. (Inputs are a
separate, later piece — the board's **reports** answered.)

### Jurassic Park — the board is answered; the game then does NOT render. (Two parts, and I checked my own claim.)

**Part 1 — the board is seen. TRUE, evidenced.** Runs jr1/jr2 (2026-10-14): patched `game`
(`RIO_Connected` at `0x836b320` → connected) bound over `/pm/game`. JP writes "RIO Disconnected" /
"STR_ERRORMSG_RIO_DISCONNECTED" to its own `pmuser/errorlog.txt` on every boot. **That file carries
those lines for every 2026-10-13 run and for NONE of tonight's patched runs** — so the patch stops the
RIO-disconnected state; the board is seen.

**Part 2 — but JP does not render attract with the patch.** jr1/jr2 show black frames; the earlier
unpatched runs reached attract (with the banner). So forcing RIO connected appears to stop JP reaching
its attract render. **The specific blocker is NOT pinned, and I corrected a wrong claim here:** an
earlier version of this note said the wall was a `hasp_decrypt(792)` loop "only reached once the cabinet
is present." I checked that against the logs and it is **false** — the 792-byte decrypt appears in
2026-10-13 runs too (e.g. `…-225845.log`, 52 of them), without the patch, so it is **not** the
differentiator. jr2's game-side log ends on the **same four `shader108` compile asserts the successful
attract run also has** (`g5/shader_gl.c:2180`), which are known and not fatal there. So what stops the
render under the patch is **not determined**; it needs a controlled comparison (patched vs unpatched in
the same harness, run long enough), which needs the bench.

So, stated plainly: **for JP the board is answered (seen), but the patch does not yet yield a
banner-gone attract frame — the game does not render, and why is not yet known.** That is an advance
(the RIO-disconnected state is gone) but it is **not** the NERF result, and it is **not** a finished
"wall moved to the dongle" claim.

**What xico/msr100 is:** the **Xico 7104USB**, a USB magnetic **card reader** (`g5/cardreader.c`,
`msr100_change_to_HID_mode`, `CardReaderSetCallback`, "creditCardSwipes"). Same *shape* as the RIO board
(a USB device), and the game handles its absence gracefully (`xicoOpenPort(): Error` /
`msr100OpenPort(): Error`, then it continues) — so it is **not** JP's blocker either.

### Determined now (was "not determined")

* The report format is not needed to clear the banner (the connected flag alone does it, for NERF); it
  **is** needed for inputs, and is readable from `librio.so`'s exported names.
* The guns of NERF go through the RIO (the board the game polls); JP's gun is separate (a serial-numbered
  gun, "Found gun with serial #%s"), distinct from the card reader.
* A single unified fix (one virtual USB device for both) needs `vhci_hcd`/`dummy_hcd` + root, which the
  bench user does not have (a system change for the owner). The per-title patches need no root.
* **NOT determined:** why JP does not render attract once RIO is forced connected. The 792-decrypt loop
  is ruled out as the cause (it occurs without the patch).
