# Jurassic Park — the lightgun board does not answer, and the NERF table cannot make it answer: the board needs EVENTS

**Date:** 2026-10-14 (afternoon). **Game:** `game` 1.08, sha256 `c18a9329…b4269`, on our own loader (`rg-elfloader`),
recipe of `../ATTRACT-RUN.md §4` plus the banner fix from `../bewijs-rio-20261014/`.
**Order:** make the lightgun board answer on Jurassic Park, then aim and trigger through the bridge, then a played run of
five minutes or more. The order also said: if the board needs something the NERF table cannot express, say so and stop.
**That is what this is.** Items 2 and 3 (aim/trigger frames, a round in the high-score table, a five-minute played run)
were **not attempted**.
**Labels:** MEASURED = a log line, a value read from the live game process, a frame, or code read from the game's own
binary; INTERPRETATION = mine; NOT DETERMINED.

## The answer

**The board needs X, where X = input delivered as events.** Jurassic Park gets every switch and every lightgun channel
from its RIO library as an *event*: the library's USB read thread decodes a board report and queues
`(id, type 3, value)` for every input that changed. The game's input loop calls the queued callbacks. The value is a
transition counter for switches (odd = pressed) and a 12-bit value for lightgun channels. On a cabinet those events come
only from **64-byte input reports read from USB device `0c70:0780`**. Each report has type byte 2, is encrypted with XTEA
under the key stored in the game itself, and carries a checksum in byte 63 (the sum of bytes 0–62).

**The NERF table cannot express that.** Its four roles (`verbonden`, `schakelaar`, `adc`, `adc_ex`) replace the
*return value* of four functions that NERF's Unity code *polls* every frame, inside a 64-bit `librio.so` loaded with
`dlopen`. Jurassic Park has nothing to poll:

* MEASURED (static, the call graph of `game`, `tools/xref.py`): `RIO_SW_Count` has **no caller**; `RIO_ADC_Val_Ex`
  has **no caller**; `RIO_ADC_Val` is called only from `Motion_CBHeight` (motion-cabinet code). `RIO_SW_State` is called
  only from the `Motion_Mat*` functions and from the operator Switch Tests page. Gameplay input comes **only** through
  `RIO_Uses` callbacks (`io_rio_init` registers them) and `RIO_ProcessCallbacks` (`io_rio_loop`, once a frame). That
  queue is filled only by `process_sw`/`process_adc` in the library's read thread (`read_thread_func` →
  `RIO_GetReport` → `parse_in_report` → `parse_device_input_report`).
* Also, the bridge is x86-64 and waits for a `librio.so` to be `dlopen`ed. Jurassic Park is an i386 image with the RIO
  library linked into `game`.

### MEASURED — the banner fix makes the board *report present*, nothing more (run jpP1)

Banner-fixed `game` (`RIO_Connected` → 0), with a 32-bit meter preloaded into the game process
(`tools/jp_rio_meter.c`; it reads the library's own variables through `/proc/self/mem` once a second,
`logs/jpP1-meter.log`):

```
io-masker=0x1240 rio-actief=0 devp=0x0 verbonden=0 leesdraad-tikken=0 wachtrij schrijf=0 lees=0 ids-geregistreerd=54
```

* The device init fails: `io_rio_init` → `RIO_Connect` → `pmrt_usb_open(0x0c70, 0x0780)` returns NULL, so
  `io_rio_init` returns −1. `io_reinit` then **drops the RIO backend from the game's input loop**: the active mask is
  `0x1240`, and bit `0x2` (RIO) is missing for the whole run. Device handle 0, the library's read thread never ran
  (0 ticks). The game did register callbacks for 54 board ids.
* Four events put straight into the library's own callback queue at 99–107 s: **written 4, read 0**. The
  game never looks at the queue. The attract loop runs clean, with no banner (`01-…jpg`, `sheet-jpP1.jpg`).

### MEASURED — even with the device init forced, no board event arrives (run jpC1, first 89 s)

`RIO_Connect` **also** made to return 0 (`tools/jp-game-rio-connect-patch.sh`). This is the most the NERF table's
`verbonden` role could say:

```
io-masker=0x1242 rio-actief=1 devp=0x0 verbonden=0 leesdraad-tikken=0 wachtrij schrijf=0 lees=0 io_rio_loop-arg=8
```

The RIO backend is now in the loop (`0x1242`) and `io_rio_loop` runs every frame (its argument, the frame time, is 8–9 ms).
But there is still no device and no read thread, so **zero events**. The game cannot be started, aimed or fired this
way.

### MEASURED — positive control: events through the library's own queue drive the game (run jpC1, 89–218 s)

The meter put one tap per switch id (count 1 = pressed, 2 = released) into the same queue `process_sw` writes to
(`logs/jpC1-driver.log`). The game consumed every one (`schrijf=38 lees=38`). Its own log (`logs/jpC1-gamelog.txt`)
answers in the same second:

| id | injected | the game's own log / the frame | reading |
|---|---|---|---|
| 7 | 10:42:46 | `Player 1 joined.` `Game Started` (frame: "CHOOSE YOUR MISSION", `02-…jpg`) | P1 Start |
| 8 | 10:42:53 | `Player 2 joined.` | P2 Start |
| 0 | 10:43:00 | `Coin in. Type: 0. Clicks: 1.` | coin 1 |
| 1 | 10:43:07 | `Coin in. Type: 1. …` | coin 2 |
| 2 | 10:43:14 | `Coin in. Type: 3. …` | coin |
| 4 | 10:43:21 | `Coin in. Type: 4. …` | coin |
| 5 | 10:43:28 | frame: "GAME VOLUME 92" overlay; `Adj_Save 'sound\gameVolume' to 94` follows at :35 | Vol+ |
| 6 | 10:43:35 | frame: "GAME VOLUME 82"; `Adj_Save … to 80` follows at :42 | Vol− |
| 12 | 10:44:03 | `Player 2 fired grenade.` | INTERPRETATION: a P2 button |
| 13 | 10:44:10 | `Player 1 fired grenade.` | INTERPRETATION: a P1 button |
| 16 | 10:44:31 | `Player 2 fired grenade.` | INTERPRETATION: a P2 button |
| 3 | 10:44:52 | `Entering Diag Menu` (frame: operator "Main Menu", `04-…jpg`) | Test |
| 9, 10, 11, 14, 15, 17, 18 | | no log line | NOT DETERMINED (P2's score went 0 → 50 between the frames after ids 16 and 17; no lightgun channel event was sent in this run, so aim was not involved) |

So the path from a board event to the game works and is complete. The missing piece is the thing that **produces** the
events: an answering board. The positive control is a measurement instrument, not a delivered fix. It bypasses the
board's report path, which is exactly the kind of workaround the order ruled out.

## Is it the same device as NERF's? Yes. The bridge does not carry over, and that matters

MEASURED, from both games' own report decoders (`parse_device_input_report` in `game` and in NERF's `librio.so`,
sha256 `a32fb2e6…`):

* the same USB device (`0c70:0780`), the same XTEA key (`e4176525d2d9f572776e4e2de8e2dcf2` in both), the same 64-byte
  report with the same checksum;
* **the same layout** for switches 0–18 and lightgun channels 19–22 (table below), and the same switch ids for every
  function measured on both titles: 7 = P1 Start, 8 = P2 Start, 3 = Test, 5/6 = volume.

The differences that matter:

1. **How the game consumes the board.** NERF's game polls `RIO_SW_Count`/`RIO_ADC_Val`. Jurassic Park's takes callbacks.
   This is why the NERF table, which replaces polled return values, cannot reach Jurassic Park.
2. **Library generation.** Jurassic Park carries RIO library **0.13** (interface 6), NERF **0.16** (interface 14). For
   switches 0–18 and lightgun channels 19–22 the decoding is identical. Version 0.16 also decodes further fields after
   payload byte 17.
3. **Packaging.** Jurassic Park is i386, with the library linked into `game` (local symbols). NERF is x86-64, with an
   exported `librio.so`.

So "they are the same device" is right. "The bridge carries over" is not.

### The input report (type 2), as Jurassic Park decodes it

MEASURED with `tools/reportmap.py`. Payload byte *N* = report byte 3+*N* (byte 0 = type, bytes 1–2 = sequence, byte
63 = checksum; the whole report is XTEA-encrypted, `RIO_GetReport` decrypts it with `xrtea(…, −1, rio_xrtea_key)`).

| payload byte | high nibble | low nibble |
|---|---|---|
| 0 | switch 0 | switch 1 |
| 1 | switch 2 | switch 18 |
| 2 | switch 3 | switch 4 |
| 3 | switch 5 | switch 6 |
| 5 | switch 7 | switch 8 |
| 6 | switch 9 | switch 10 |
| 7 | switch 11 | switch 12 |
| 8 | switch 17 | — |
| 9 | switch 13 | switch 14 |
| 10 | switch 15 | switch 16 |
| 11 | ch 19 bits 8–11 | ch 20 bits 8–11 |
| 12 | ch 21 bits 8–11 | ch 22 bits 8–11 |
| 13 / 14 / 15 / 16 | ch 19 / 20 / 21 / 22 bits 0–7 | |
| 17 | two further fields, queued separately (not used by the inputs above) | |

Switches are **4-bit transition counters**; `count_4bit_diff` turns them into the 32-bit counter the event carries,
with odd meaning pressed. That is the same counter model as NERF's `RIO_SW_Count`. Output reports (lamps, coin meter,
solenoids) go back through `RIO_SendReport`, encrypted the same way, with a sequence id.

## What X would take — not built, a decision for the coordinator (C is the owner's)

* **A. Answer at the board's transport, inside the game.** Replace `pmrt_usb_open` / `pmrt_usb_interrupt_read` /
  `pmrt_usb_interrupt_write` / `pmrt_usb_close` (in `game`; NERF's `librio.so` has the same layer) with a board model
  that builds encrypted type-2 reports from the daemon's board image. The library's own code then runs unchanged in
  both titles: connect, read thread, decrypt, checksum, decode, events. Needs an i386 build of the bridge, a new
  table role (the transport), and the report encoder. **My recommendation:** this literally makes the board answer,
  and it is one model for both titles in place of NERF's API patches.
* **B. Put events straight into the library's queue** (what the positive control did). Smaller, but it skips the
  board's report path.
* **C. A virtual USB device `0c70:0780` in the kernel** (`vhci_hcd`/`dummy_hcd`, root). This changes the owner's system;
  whether SteamOS ships the modules was not checked.

## Other notes

* The game also runs an **SDL keyboard/mouse input backend** (bit `0x200` of the same mask, active in both runs). Not
  examined. It is not the lightgun board.
* Local write layer only (`~/.local/share/ravengun/native/JurassicPark/0/upper/`), nothing on the NAS. Run jpC1 counted
  four coins in the game's audits (`pmuser/aud/save/`). It also saved `gameVolume` twice, ending at 80, its earlier value.
  Run jpC1 ended in the operator Main Menu with nothing changed there.
* `hasphook.log` in that layer is 1.4 GB, from tonight's earlier black-screen runs (verbose hook log). These runs log to
  their own folder instead (`RAVENGUN_HASP_ELF_LOG=/rg/meter/hasphook.log`, not verbose). The old file was left in place.

## Files

* `01-…` to `04-…jpg`: selected frames. `sheet-jpP1.jpg`, `sheet-jpC1-a.jpg`, `sheet-jpC1-b.jpg`: contact sheets (each
  tile is labelled with the injection it follows).
* `logs/`: meter logs (both runs), injection schedules and driver logs, the launcher output, the game's own log of jpC1.
* `tools/`: `jp_rio_meter.c` (build: `gcc -m32 -shared -fPIC -I<loader>/tools/rg-elfloader/inc … -lpthread`; that dir
  holds the `gnu/stubs-32.h` SteamOS lacks), `jp2-run.sh`/`jp2-inner.sh` (the bench harness; paths are the bench's),
  `jp-game-rio-connect-patch.sh` (arm C, measurement only), `syms.py`/`mem.py`/`dis.sh`/`ann.py`/`xref.py`/`reportmap.py`
  (static reading of `game`'s own dynamic symbol table: 18,811 names), `sheet.sh` (contact sheets with ffmpeg).
* Raw: all 79 captures of both runs (19 + 60), plus the launcher output, are out of git under
  `~/.local/share/ravengun/bench-captures/JurassicPark/bord/{jpP1,jpC1}/` (185 MB); list in `OPNAMES.txt`.
* Checksums: `game` (banner-fixed) `d189eb74…`, arm C `69f9c954…`, meter `9d16ae54…` (in `logs/*-run.txt`).
