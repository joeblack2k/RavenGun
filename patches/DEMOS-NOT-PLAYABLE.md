# DEMOS-NOT-PLAYABLE — the nine that hold five minutes on our core, and the one thing each still cannot do

**Written:** 2026-10-14 (count corrected the same day — see below). The bar met so far is **five minutes
of attract or gameplay, on our own core, seen on the TV**. **Nine** titles clear it on our core. **Not
one has been played through with a real light gun by a person** — the inputs that have been shown were
injected synthetically (the daemon's shared memory / mouse-mode / a scripted key), not played. One more
title, **House of the Dead: Scarlet Dawn, is shown but NOT on our core** (closed TeknoParrot core), so it
does not count. This page is the blunt state: one row per game, the check or banner still unmet, the
evidence that named it, and what would make it count as **played** rather than demoing.

**The count was ten, and it was wrong.** Scarlet Dawn's five minutes were measured on the closed core
(`patches/HOTDSD/AB-UITKOMST.md`, `LOADER-AB-MET-BEELD.md`: the core in all four runs is
`TeknoParrot/TeknoParrot64.dll 1.0.0.3761`; `README.md`: "De speelbare route is TeknoParrotUi",
`[Launch] Mode = tpui`). The loader was ours; the core was not. By the rule held all along — our own core
or it does not count — that is **nine** on our core, not ten. The same standard that made Raccoon Rampage
and NERF prove the owner-route separately applies here.

**Read "on our core" strictly.** Where a game is only playable on the closed TeknoParrot route, that is
said in its row; it does not count.

## My judgement (what you asked for)

**The single biggest obstacle is that there is no one input bridge, and there cannot be a cheap one:**
every cabinet reads its gun, coins and buttons through its own device or library — a Raw Thrills RIO USB
board (JP, NERF), the game's own `UsbPluginsDll.dll` (Raccoon), a `shaiolib.dll` (Silent Hill), the
WartranTroopers shared memory (Halo, Terminator) — so "wire the gun" is a different adapter for almost
every title. The daemon already holds the intent (aim, trigger, coin); the wall is the per-game adapter
from that intent to the game's own input source. Boot is solved everywhere; this is the whole remaining
distance.

**The one fix that unblocks the most titles: the RIO board — count 2 (Jurassic Park + NERF).** Of the
nine, it is the **only** wall shared by more than one title: both games have **no input at all** on our
core and both open the **same** USB device `0c70:0780` with the same code. Every other wall serves
exactly one title (Raccoon's `UsbPluginsDll.dll`, Silent Hill's `shaiolib`, Luigi's own path), or is a
title that already has input and needs a person's playthrough (Terminator, Transformers), or is Rabbids
(needs to play past world-select). So on the "most titles per fix" measure the RIO board wins outright, 2
to 1. It is also the **most tractable**: `librio.so` keeps its export names (`RIO_GetReport`,
`RIO_SampleInput`, `RIO_SW_Count`, `RIO_ADC_Val`), so the board's report format can be read statically
before any run, and both titles already hold attract on our core — inputs are the only distance left.
**That is the one I would do next.**

| game | how it clears the bar | the unmet thing, with its evidence | what would make it count as PLAYED |
|---|---|---|---|
| **Luigi's Mansion** | **demo, our core** — ~6 min continuous attract, no error banner (drop-in `rg-pe-hook.dll`) | nearest to "demos and nothing else known missing": no banner is up. But **no input is wired** and it carries an unused OKAO/QR webcam layer (`Nbam_QR_Code.dll`, `eOkao*` in `BESCHERMINGSLAAG.md §624`). | aim + trigger wired from the daemon, and a person plays it on our core with the gun. |
| **Rabbids Hollywood** | **demo, own route (no TP)** — 288 s attract, dongle answered | **"Savegame corrupted : … will be resetted"** on every boot (`LOADER-AND-PATCHES-REPORT.md §264`); the 288 s run **never left attract/world-select**, so no actual level was played (`§5` of its outstanding list). | play past world-select into a level with the Move; the savegame-corrupted line gone. |
| **Halo: Fireteam Raven** | **demo, our core (native)** — attract reached with START; coins counted ("Money In: $16.75") | **permanent banners** "QR scanners need to be configured!", "P1/P2 QR Scanner is not detected!", "Factory Setup Has Not Been Completed!" (`HALO-GEHAALD.md §2`); **aim and trigger were never wired on the native route** ("de trekker is nog steeds nooit aangesloten geweest", `§6`). The `game.ini` "fully working lightgun" is the **closed** TP route, not this one. | the two QR scanners + factory setup satisfied (or their banners shown to be harmless), aim/trigger wired, a real-gun playthrough on the native core. |
| **Terminator Salvation** | **demo, our core (own loader)** — attract loop, aim/shooting shown via the I/O-bridge (shared memory) | the **file-check warning every boot** — "WARNING! Data Files Corrupted … Press Start To Continue" (`ATTRACT-RUN.md §1/§4.3`), costs one START per boot, no operator setting suppresses it; inputs shown only by **synthetic injection**, never played with a real gun. | a person plays it on our loader with the gun; the boot warning accepted as harmless-but-present. |
| **Transformers: Shadows Rising** | **gameplay, our daemon** — the daemon is the gun board (hid-board route); start/trigger/aim work, crosshair within 1.5% at every grid point | closest to played: inputs are real through our board, but the **game's own gun calibration was not done** (`Sega/shell/testcalibrate.xml`, `README.md §85`), and the 5 min shown was measured aim points, not a person's continuous playthrough. | the in-game calibration run once, then a person plays a full session with the Move. |
| **House of the Dead: Scarlet Dawn** — **DOES NOT COUNT, not on our core** | **closed core** — all its runs are on `TeknoParrot/TeknoParrot64.dll 1.0.0.3761` (`AB-UITKOMST.md`, `LOADER-AB-MET-BEELD.md`); playable route is TeknoParrotUi (`README.md`, `[Launch] Mode = tpui`) | it is **not shown on our own core at all** — that is the wall; also player 2, the **real Move buttons** and the test menu untested (`README.md`); the AiMe card reader absent — shipped state "no card reader" (`REPORT.md §9.5`). | bring it up on our own core first, then a real-gun playthrough; card reads only if wanted. |
| **Jurassic Park** | **demo, our loader** — 12 min attract, independently witnessed | **"RIO Disconnected"** on every frame (`ATTRACT-RUN.md §1`); **no inputs** — the RIO I/O board is absent. Shared item with NERF: `patches/RIO-BORD.md`. | the RIO board answered (one fix serves both titles), then a real-gun playthrough. |
| **Silent Hill: The Arcade** | **demo, our core** — 12 min attract movie, independently witnessed | the replacement `shaiolib.dll` **delivers no inputs yet** — no buttons, coins or guns (`ATTRACT-RUN.md §1`, placeholder `rg_poll_inputs()`); an **e-Amusement ticker** and "FREE PLAY" on screen; region Asia. | the input bridge filled in at `rg_poll_inputs()`, then a real-gun playthrough. |
| **Raccoon Rampage** | **demo, our core** — 12 min 50 s attract, independently witnessed | **"The guns are not calibrated, please enter Setting Menu to calibrate!"** on every frame (`ATTRACT-RUN.md §1`); **no inputs** — the game reads its own `UsbPluginsDll.dll` (107 `BIND_*`), not the daemon's state (`§4.1`); a TEST press did nothing (rr7/rr9). | our core answering `UsbPluginsDll.dll`'s functions from the daemon (hooks or a replacement plug-in) + the calibration state, then a real-gun playthrough. |
| **NERF Arcade** | **demo, our core (native)** — 12 min 46 s attract, independently witnessed | **"RIO DISCONNECTED"** on every frame (`ATTRACT-RUN.md §1`); **no input tried** — same missing RIO board as Jurassic Park (`patches/RIO-BORD.md`). | the RIO board answered (shared with Jurassic Park), then a real-gun playthrough. |

## What this says, blunt

* **The common wall is inputs, not boot.** Each of the nine boots and holds on our core; none has been
  played through with a real gun. Of the nine: **five** (JP, NERF, Silent Hill, Raccoon, Luigi) have **no
  input wired at all** on our core; **three** (Terminator, Halo, Rabbids) have inputs shown only by
  **synthetic injection**; **one** (Transformers) has real input through our board but no person's
  playthrough and no in-game calibration. Scarlet Dawn is the tenth title — shown, but on the **closed**
  core, so it is not one of the nine; bringing it to our core is its own wall, before inputs.
* **Two titles share one fix:** Jurassic Park and NERF both want the Raw Thrills **RIO board** (same USB
  device, same code — `patches/RIO-BORD.md`). Fixing it once counts twice — the only such case.
* **The closest to "played":** Transformers (real input through the daemon's gun board; needs the in-game
  calibration and a real session). **The furthest among the nine:** the five with no input at all.
* **The cleanest demo:** Luigi's Mansion — no banner, nothing on screen says anything is missing; the only
  gap is that inputs were never wired.

## Where the evidence lives

Each row's claim is in that game's own folder — `ATTRACT-RUN.md`/`README.md` and the `bewijs-*` captures
and logs. The banners above are measured strings a frame or a log line carries; follow the named section
in the game's folder for the frame or log that shows it.
