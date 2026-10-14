# DEMOS-NOT-PLAYABLE — the ten that hold five minutes, and the one thing each still cannot do

**Written:** 2026-10-14. The bar met so far is **five minutes of attract or gameplay, on our own core,
seen on the TV**. Ten titles clear it. **Not one has been played through with a real light gun by a
person** — the inputs that have been shown were either injected synthetically (the daemon's shared
memory / mouse-mode / a scripted key) or shown on TeknoParrot's **closed** core, not ours. This page is
the blunt state: one row per game, the check or banner still unmet, the evidence that named it, and what
would have to be true for the game to count as **played** rather than demoing.

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

**If you told me to pick one next, I would do the RIO board (Jurassic Park + NERF).** It is the only wall
that clears **two** titles with one fix; it is the **most readable** of them — `librio.so` keeps its
export names (`RIO_GetReport`, `RIO_SampleInput`, `RIO_SW_Count`, `RIO_ADC_Val`) and both games open the
**same** USB device `0c70:0780`, so the report format can be read statically before any run; and both
titles already hold attract on our core, so inputs are the only thing left between them and a real-gun
playthrough. Everything else serves one title, or (HOTDSD) is not on our core yet.

| game | how it clears the bar | the unmet thing, with its evidence | what would make it count as PLAYED |
|---|---|---|---|
| **Luigi's Mansion** | **demo, our core** — ~6 min continuous attract, no error banner (drop-in `rg-pe-hook.dll`) | nearest to "demos and nothing else known missing": no banner is up. But **no input is wired** and it carries an unused OKAO/QR webcam layer (`Nbam_QR_Code.dll`, `eOkao*` in `BESCHERMINGSLAAG.md §624`). | aim + trigger wired from the daemon, and a person plays it on our core with the gun. |
| **Rabbids Hollywood** | **demo, own route (no TP)** — 288 s attract, dongle answered | **"Savegame corrupted : … will be resetted"** on every boot (`LOADER-AND-PATCHES-REPORT.md §264`); the 288 s run **never left attract/world-select**, so no actual level was played (`§5` of its outstanding list). | play past world-select into a level with the Move; the savegame-corrupted line gone. |
| **Halo: Fireteam Raven** | **demo, our core (native)** — attract reached with START; coins counted ("Money In: $16.75") | **permanent banners** "QR scanners need to be configured!", "P1/P2 QR Scanner is not detected!", "Factory Setup Has Not Been Completed!" (`HALO-GEHAALD.md §2`); **aim and trigger were never wired on the native route** ("de trekker is nog steeds nooit aangesloten geweest", `§6`). The `game.ini` "fully working lightgun" is the **closed** TP route, not this one. | the two QR scanners + factory setup satisfied (or their banners shown to be harmless), aim/trigger wired, a real-gun playthrough on the native core. |
| **Terminator Salvation** | **demo, our core (own loader)** — attract loop, aim/shooting shown via the I/O-bridge (shared memory) | the **file-check warning every boot** — "WARNING! Data Files Corrupted … Press Start To Continue" (`ATTRACT-RUN.md §1/§4.3`), costs one START per boot, no operator setting suppresses it; inputs shown only by **synthetic injection**, never played with a real gun. | a person plays it on our loader with the gun; the boot warning accepted as harmless-but-present. |
| **Transformers: Shadows Rising** | **gameplay, our daemon** — the daemon is the gun board (hid-board route); start/trigger/aim work, crosshair within 1.5% at every grid point | closest to played: inputs are real through our board, but the **game's own gun calibration was not done** (`Sega/shell/testcalibrate.xml`, `README.md §85`), and the 5 min shown was measured aim points, not a person's continuous playthrough. | the in-game calibration run once, then a person plays a full session with the Move. |
| **House of the Dead: Scarlet Dawn** | **gameplay, CLOSED core** — playable route is **TeknoParrotUi**, not ours (`README.md`: "de speelbare route is TeknoParrotUi") | it is **not shown on our own core at all**; player 2, the **real Move buttons** and the test menu are untested (`README.md`); the AiMe card reader is absent — shipped state "no card reader" (`REPORT.md §9.5`). | bring it up on our own core first, then a real-gun playthrough; card reads only if wanted. |
| **Jurassic Park** | **demo, our loader** — 12 min attract, independently witnessed | **"RIO Disconnected"** on every frame (`ATTRACT-RUN.md §1`); **no inputs** — the RIO I/O board is absent. Shared item with NERF: `patches/RIO-BORD.md`. | the RIO board answered (one fix serves both titles), then a real-gun playthrough. |
| **Silent Hill: The Arcade** | **demo, our core** — 12 min attract movie, independently witnessed | the replacement `shaiolib.dll` **delivers no inputs yet** — no buttons, coins or guns (`ATTRACT-RUN.md §1`, placeholder `rg_poll_inputs()`); an **e-Amusement ticker** and "FREE PLAY" on screen; region Asia. | the input bridge filled in at `rg_poll_inputs()`, then a real-gun playthrough. |
| **Raccoon Rampage** | **demo, our core** — 12 min 50 s attract, independently witnessed | **"The guns are not calibrated, please enter Setting Menu to calibrate!"** on every frame (`ATTRACT-RUN.md §1`); **no inputs** — the game reads its own `UsbPluginsDll.dll` (107 `BIND_*`), not the daemon's state (`§4.1`); a TEST press did nothing (rr7/rr9). | our core answering `UsbPluginsDll.dll`'s functions from the daemon (hooks or a replacement plug-in) + the calibration state, then a real-gun playthrough. |
| **NERF Arcade** | **demo, our core (native)** — 12 min 46 s attract, independently witnessed | **"RIO DISCONNECTED"** on every frame (`ATTRACT-RUN.md §1`); **no input tried** — same missing RIO board as Jurassic Park (`patches/RIO-BORD.md`). | the RIO board answered (shared with Jurassic Park), then a real-gun playthrough. |

## What this says, blunt

* **The common wall is inputs, not boot.** Every title boots and holds; none has been played through with
  a real gun on our own core. Five of the ten (JP, NERF, Silent Hill, Raccoon, Luigi) have **no input
  wired at all** on our core; three (Terminator, Halo, Rabbids) have inputs shown only by **synthetic
  injection**; one (Transformers) has real input through our board but no person's playthrough and no
  in-game calibration; one (HOTDSD) is not on our core at all.
* **Two titles share one fix:** Jurassic Park and NERF both want the Raw Thrills **RIO board** (same USB
  device, same code — `patches/RIO-BORD.md`). Fixing it once counts twice.
* **The closest to "played":** Transformers (real input through the daemon's gun board; needs the in-game
  calibration and a real session). The **furthest:** HOTDSD (only shown on the closed core).
* **The cleanest demo:** Luigi's Mansion — no banner, nothing on screen says anything is missing; the only
  gap is that inputs were never wired.

## Where the evidence lives

Each row's claim is in that game's own folder — `ATTRACT-RUN.md`/`README.md` and the `bewijs-*` captures
and logs. The banners above are measured strings a frame or a log line carries; follow the named section
in the game's folder for the frame or log that shows it.
