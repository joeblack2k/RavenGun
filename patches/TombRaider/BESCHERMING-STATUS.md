# Tomb Raider (Arcade) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation, with one caution about what "the dongle" means for this game.
**Verdict:** the Matrix dongle is **not** what blocked this game — the **USB I/O board** was, and that is
solved. Our drop-in is in place and unmeasured. Nothing is missing for reach.

---

## 1. What is present (MEASURED)

Unity game, 64-bit.

| File | Size | md5 |
|---|---|---|
| `Game.exe` | 652288 | — |
| `matrix32.dll` (root and `Game_Data/Plugins/`) | 114688 | `6e75f453dca9380144620d80f467a0e6` |
| `matrix64.dll` | 93184 | `c86908ab10b1d9249c896e42b7618f67` |
| `Game_Data/Plugins/KaboomAdrioFx.dll`, `KaboomAdrioFx64.dll`, `KaboomAdrioFxPlus.dll`, `KaboomAdrioFxPlus64.dll`, `Khronos/` … | — | — |
| `Firmware_TRLC.hex` | 58292 | — |
| `FlashIO.exe` | 22831616 | — |
| `Sixences/` (`Commands_Files/`, `Firmware/`, `Matrix_Files/`, `device_fw/`, `device_script/`) | — | — |
| `Gun_10_9_2026_0.log` | 301 | — |

Both matrix md5s are again the 2009 originals, unchanged.

**Two different "dongle" words live in this game's directory tree and they are not the same thing:**

* `matrix64.dll` / `matrix32.dll` — the **LPT Matrix dongle** (the thing this lane emulates).
* `GetVersionDRV_USB` — the **USB** variant of the same DLL family. This is the I/O-board path, and it is the
  one that mattered. `Gun_10_9_2026_0.log` records `ERROR: 1001: Base not found`.

The `Sixences/Matrix_Files/` directory is worth a look precisely because it is *not* a `matrix32.dll`-only
path: it belongs to the I/O board's scripting/firmware side (`Sixences/Commands_Files`, `device_fw`,
`device_script`), i.e. the same area as `FlashIO.exe` and the `.hex` firmware. It should not be read as
"Matrix dongle data files" without evidence.

## 2. What it asks for (MEASURED, from earlier work)

Two separate demands, only one of which is a licence check:

1. **The USB I/O board.** `CheckIOBoard` plus `libusb`/`ftdi.dll`/`usb.dll` out of `Game_Data/Plugins`; the log
   line `ERROR: 1001: Base not found` is the board, and the game stops at `IO BOARD ERROR`. `findings/TombRaider.md`
   records that this was **not** the JVS pipe — a hand-started helper did not help either.
2. **The Matrix dongle.** The same shape as Rabbids/Rampage (the same `MatrixDLLCSharp` wrapper, the same
   original DLLs). `findings/TombRaider.md` states plainly that the dongle was not the blocker: *"blokkeerde
   niet op de dongle maar op het USB-I/O-bord"*.

The game genuinely works without TeknoParrot on the Unity route — mode select, chapters, movies
("Hold Trigger to Skip"), gameplay with crosshair, ammo, score, credits, recoil on fire.

## 3. Do we already answer it?

**Yes for the thing that blocked it; the dongle answer is not required.** Our own BepInEx plugin
(`plugins/unity/`, "Route 2", TCP 127.0.0.1:33610) replaces the I/O board, and after that there is no board
error. Verified live: coin (key 5 ×4 → 4 credits), start (key 1), trigger holds skip the movies, ammo 10 → 9,
reload refills; crosshair verified on a 3×3 grid, including the Y origin at the bottom; the daemon can be
stopped and restarted without killing the game.

A Matrix drop-in is nonetheless shipped at
`/home/deck/projects/ravengun-patches/patches/TombRaider/dropin/` (`matrix64.dll` 25614,
md5 `ef2b7832894118e03ab23ba06dd267d6`; `dropin.ini` `To = Game_Data/Plugins`, `DllOverride = native`,
`RAVENGUN_MATRIX_DIR=matrix`, `RAVENGUN_MATRIX_LOG=matrix.log`; no `RAVENGUN_MATRIX_DLL` because this game's
plugin is `plugins/unity/src/tombraider/` and does not touch the dongle check).

Its `dropin.ini` carries the same honest caution as Rampage's:

> *"LET OP: de drop-in-motor rolt een drop-in ALTIJD uit. Deze game is vandaag speelbaar zonder dongle …; met
> deze drop-in krijgt hij er een. Dat is nog niet in het spel gemeten."*

Same conclusion as Rampage: the safe default on the "never enable anything implicitly" rule is **off**, and
what is missing here is a **decision plus a comparison run**, not emulation code.

## 4. What is missing (and cost)

1. **A decision on the drop-in** — keep and measure, or drop because the game does not need it.
   **Cost: minutes.**
2. **Whether the Matrix check is strict here at all.** Unmeasured. Because the game already plays without a
   dongle, the interesting question is whether a wrong answer changes anything. **Cost: two runs.**
3. **`Sixences/Matrix_Files/` has not been characterised.** It may hold dongle-side data, or nothing relevant.
   Worth a listing and a calibrated string scan before anyone builds anything on it. **Cost: under an hour,
   static.**
4. Not an emulation item but adjacent: `FlashIO.exe` and `Firmware_TRLC.hex` imply a firmware-flash step in the
   cabinet's bring-up; whether the game ever needs it is unknown. **Cost: unclear.**

## 5. Proposed test (NOT RUN) + positive control

**One comparative test**, same shape as Rampage's.

* Arm A: with the drop-in in place and `RAVENGUN_MATRIX_LOG` set.
* Arm B: with the drop-in removed, original `matrix64.dll` (`c86908ab…`) back in `Game_Data/Plugins/`.

* **Passing result (positive control).** Arm A yields `matrix.log` with `Init_MatrixAPI -> 0`,
  `Dongle_Count -> aantal, poort = N`, `Dongle_ReadSerNr -> 69420, poort = N`; and the measurable gameplay
  facts from §3 reproduce identically in both arms (4 credits from four coin presses, ammo 10 → 9, crosshair at
  the expected grid points).
* **Failing result (negative control).** Arm B must produce **no** `matrix.log`. Then, to show the test can
  detect the fault it is looking for: set `aantal = 0` in a view-local `matrix.ini` and confirm that either the
  game reacts (→ the check is live and the drop-in matters) or it does not (→ the check is inert and the drop-in
  should be dropped). A variant that cannot fail is not a test.
* **Separate, static check for item 3:** `find "Sixences/Matrix_Files"` plus a calibrated `strings` scan for
  `matrix`, `MTX`, `Dongle`, `ReadSerNr` with a known-present positive control from the same tree.

## 6. Evidence log

```
md5sum "Tomb Raider/matrix32.dll" "Tomb Raider/Game_Data/Plugins/matrix32.dll"
  -> 6e75f453dca9380144620d80f467a0e6  114688   (twice)
md5sum "Tomb Raider/matrix64.dll"  -> c86908ab10b1d9249c896e42b7618f67  93184
ls -la "Tomb Raider/"              -> Game.exe 652288, FlashIO.exe 22831616, Firmware_TRLC.hex 58292,
                                      Gun_10_9_2026_0.log 301, sixences/ , sudo.cmd, readme.txt
find "Tomb Raider/Sixences" -maxdepth 2 -type d
  -> Commands_Files , Firmware , Matrix_Files , device_fw , device_script
find "Tomb Raider/Game_Data/Plugins" -maxdepth 2 -type f
  -> KaboomAdrioFx{,64,Plus,Plus64}.dll , matrix32.dll , ... (no matrix64.dll here; it is in the root)
cat patches/TombRaider/dropin/dropin.ini   -> To = Game_Data/Plugins ; DllOverride = native ;
   Env = RAVENGUN_MATRIX_DIR=matrix;RAVENGUN_MATRIX_GAME=TombRaider;RAVENGUN_MATRIX_LOG=matrix.log ;
   "LET OP ... nog niet in het spel gemeten"
findings/TombRaider.md  -> "Wat het oorspronkelijk blokkeerde (TeknoParrot-route): 'IO BOARD ERROR'. De game
   heeft CheckIOBoard en gebruikt libusb/ftdi.dll/usb.dll uit Game_Data/Plugins voor een eigen USB-I/O-bord
   ('GetVersionDRV_USB'); Gun_*.log zegt 'ERROR: 1001: Base not found'."
   and "de blokkade was het USB-I/O-bord, niet de dongle"
```

Read, not redone for this note: the live input measurements, the plugin route and the daemon
disconnect/reconnect test all come from `findings/TombRaider.md` and `patches/TombRaider/`.
