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

## Result — 2026-10-14 (BOTH titles: the banner is gone, in a frame)

**Independently confirmed.** The coordinator checked both banner-gone frames themselves — NERF's
attract screen and Jurassic Park's high-score table — and neither carries the "RIO Disconnected"
banner. So the claim is confirmed by someone other than the run that produced it, which is the part
that makes it worth anything.

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

### Jurassic Park — the board is answered and the banner is GONE, in a frame. ✓ (done)

Same model as NERF, different attachment: `RIO_Connected` at `0x836b320` in the dumped `game` patched to
report connected (three bytes `55 89 e5` → `31 c0 c3`; `jp-game-rio-patch.sh`). With the game rendering
correctly, the patched attract loop runs with **no "RIO Disconnected" on any frame** (run jpF: volcano
intro, high scores, the dinosaur gameplay demos — all clean). Evidence:
`patches/JurassicPark/bewijs-rio-20261014/rio-banner-boven-voor-onder-na.jpg` — the same gameplay demo
with the banner (unpatched) and without it (patched). A **controlled comparison** also confirms the state
cleanly: unpatched arm A logs "RIO Disconnected" to `pmuser/errorlog.txt`, patched arm B does not (one
variable, the three bytes).

**It took isolating a render regression that was NOT the RIO patch.** The first JP RIO runs rendered
black, and I twice guessed wrong about why (a "dongle hasp_decrypt(792) wall", then "the patch stops the
render"). The controlled comparison showed **both arms black** — so the black screen was not the patch —
and the real cause was the **harness**: the game loads its state-machine programs from
`/pm/g5/dino/data/programs_enc`, and the decrypted `programs_dec/` must be **bound over** that path
(`ATTRACT-RUN.md §4`, plus `SDL12COMPAT_OPENGL_SCALING=0`). The jp-bench harness I reused for the RIO
runs had dropped that bind, so the game read encrypted programs, failed to load them
(`Silent failure loading program`, 3.7M times, all tonight), and drew nothing. Bind restored → failures
to 0 → JP renders → patched game shows no banner. **Two retracted guesses** (the 792 dongle loop; the
patch stopping the render) are recorded in the JP folder; the render was never RIO's doing.

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
* **The JP black screen was never the RIO patch** (controlled: both arms black). It was the jp-bench
  harness missing the `programs_dec`-over-`programs_enc` bind (and `SDL12COMPAT_OPENGL_SCALING=0`); with
  those restored JP renders and the patched game shows no banner. Fixed, not an open item.
