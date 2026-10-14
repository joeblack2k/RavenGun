# Rabbids Hollywood — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation. The work here is **done and measured**; this note records how, so it is
not redone.
**Verdict:** a **Matrix/SBK LPT dongle** check that asks for exactly two numbers. We answer it, and the game
itself logs the success. Nothing is missing for reach.

---

## 1. What is present (MEASURED)

Unity game, 64-bit, Mono (not IL2CPP) — `Game_Data/Managed/Assembly-CSharp.dll` holds the wrapper.

| File | Size | md5 |
|---|---|---|
| `Game.exe` | 650752 | — (profile `[Identify] Sha256 = 979f9736a99745454429908227cdd2f48875f193d439cfa33a9e14b4578da95d`) |
| `matrix64.dll` (root) | 93184 | `c86908ab10b1d9249c896e42b7618f67` |
| `Game_Data/Plugins/matrix64.dll` | 93184 | `c86908ab10b1d9249c896e42b7618f67` |
| `matrix32.dll` (root and `Game_Data/Plugins/`) | 114688 | `6e75f453dca9380144620d80f467a0e6` |

Both md5s are the **2009 originals**, in every copy. `Game_Data/Plugins/` also holds `sxCore.dll`,
`KaboomAdrioFxPlusFourFeeders64.dll` (`2657236a866337b62ea8a4eb39364367` — the same file as in Rampage and
Tomb Raider), `enet.dll`, `ftdi.dll`, `pthread.dll`, `usb.dll`. `Firmware_Shooter.hex` (58292 bytes) sits in
the root.

## 2. What it asks for (MEASURED)

From the IL of `SBK.Matrix.MatrixManager` (read with `re/dnlib.py`; full write-up in
`/home/deck/projects/ravengun/docs/matrix-layer.md` §4 and
`patches/RabbidsHollywood/DONGLE-ONDERZOEK.md`). `Awake` calls `KDLGNPHHMKI()`, which does:

```il
Init_MatrixAPI()                          ; < 0  -> "***** Failed to initialized MatrixAPI correctly"
Dongle_Count(85)                          ; <= 0 -> code 2
Dongle_ReadSerNr(dongle, aantal, 85)      ; <= 0 -> code 3
                                          ; else code 0
```

Non-zero result → the coroutine `MatrixManager+FKNNJPMOCDC` logs `**** DONGLE NOT FOUND`,
`WaitForSeconds(30.0)`, then `UnityEngine.Application::Quit`. That 30 s wait plus start-up is the measured
~57 s death. Success → `MatrixManager+GJOEKNLLLHL` logs `**** DONGLE FOUND: <getal>` and does nothing else.

Three traps that cost earlier rounds, recorded so they are not re-entered:

* There is a second, **unused** check variant (`EGIEJAEHODM`) demanding `Dongle_Count(40) > 1`. `Awake` uses
  the port **85** variant, which only needs `> 0`. Reading the first one gives port 40 and a wrong minimum.
* Answering `Dongle_Count` alone is **not enough** — the check goes on to `Dongle_ReadSerNr`. That is why the
  first patched run still died at 57 s.
* No dongle memory. `Dongle_ReadData`/`Dongle_WriteData` do not appear on the game path. `Dongle_EncryptData`/
  `Dongle_DecryptData` appear in six wrapper methods, none of which has a single IL reference in this build —
  **dead code**. The savegame path runs via `SBK.SaveGameManager` → `IJGPGJJKJAG` → `ServerRequestManager::GetDeviceID`,
  which returns `MatrixManager.GetDongleSerialNumber().ToString()` when it is > 0 and `"0"` otherwise.

The DLL is loaded from **`Game_Data/Plugins/`**, not the game root. Measured with `WINEDEBUG=+loaddll`: both
copies load, but the calls out of `MatrixDLLCSharp` come from `Game_Data/Plugins/matrix64.dll`. With our DLL
only in the root the game still quit after ~62 s.

## 3. Do we already answer it?

**Yes, and it is measured in the game.** Two routes exist; the drop-in is the intended one.

*Delivery (drop-in), `/home/deck/projects/ravengun-patches/patches/RabbidsHollywood/dropin/`:*

| File | Size | md5 |
|---|---|---|
| `dropin.ini` | 2946 | `71bbcc7c4ab5b79e9ce2ba5988c7eea6` |
| `matrix/matrix.ini` | 1347 | `499d686861e967c4421ef2a3959348f6` |
| `matrix64.dll` | 25614 | `ef2b7832894118e03ab23ba06dd267d6` |
| `ravengun-hasp/RabbidsHollywood/dongle.json` | 3625 | `3c049fa16ab71efcbaee1495557dc5af` |

`dropin.ini` sends `matrix64.dll` to `To = Game_Data/Plugins` with `DllOverride = native` and
`Env = RAVENGUN_MATRIX_DIR=matrix;RAVENGUN_MATRIX_GAME=RabbidsHollywood;RAVENGUN_MATRIX_DLL=1;RAVENGUN_MATRIX_LOG=matrix.log`.
`RAVENGUN_MATRIX_DLL=1` switches off the managed patch in `plugins/unity/src/rabbids/Rabbids.cs` so the DLL
alone answers. `matrix.ini` is `poort = 85`, `aantal = 2`, **no `memory` line** (correct — the game reads no
dongle memory).

*Measured results:*

| Run | Route | Result |
|---|---|---|
| plugin patch that never applied (`poort` vs the real argument name `Port`) | patch | 57 s, then `Application.Quit` |
| after the parameter-name fix, `Dongle_Count` → 1 only | patch | 57 s — proves `Dongle_ReadSerNr` is the missing link |
| `Dongle_Count` → 2 **and** `Dongle_ReadSerNr` → 69420 | patch | > 200 s, clean log |
| drop-in in the game root only | DLL | ~62 s — the wrong copy |
| drop-in in `Game_Data/Plugins`, patches off | DLL | 132 s and 92 s |
| final revision, data directory beside the library | DLL | 92 s, still running |
| own route, no TeknoParrot, attract loop | DLL | **288 s**, stopped by us |

Our library's own log proves it is *our* DLL and not the original:
`Init_MatrixAPI -> 0`, `Dongle_Count -> aantal, poort = 85`, `Dongle_ReadSerNr -> 69420, poort = 85`, plus
`matrix.ini gelezen, poort = 85` / `aantal = 2`. The game's `game.ini` `[Status]` note records the same run
and adds that the game logs `**** DONGLE FOUND: 69420` itself.

## 4. What is missing (and cost)

Nothing for reach. Two open items, both recorded before this note:

1. **Savegame persistence across two starts.** Our `Dongle_EncryptData`/`Dongle_DecryptData` return 0 and leave
   the buffer untouched. That is deliberate (both directions treated alike keeps the store self-consistent)
   and it is not exercised yet. The IL says the wrappers are dead code and the encryption is not in managed
   code — the candidate native module is `Game_Data/Plugins/sxCore.dll` with `sxCoreSettings.txt`.
   **Cost:** one two-start test.
2. **A 10-minute soak.** The 288 s run was headless (gamescope without a display), so coin, start and crosshair
   were not exercised. The 10-minute run is already prepared. **Cost:** one run on the TV.

## 5. Proposed test (NOT RUN) + positive control

**The queued 10-minute run**, plus one savegame test. Deliberately *not* run here — the bench is busy.

* **Passing result (positive control).** (a) `matrix.log` shows the three answers above on every start;
  (b) the game's own log shows `**** DONGLE FOUND: 69420` and never `**** DONGLE NOT FOUND`;
  (c) the game survives past 600 s; (d) for the savegame item: write something in run 1, start run 2, and the
  save is read back.
* **Failing result (negative control).** Remove `matrix64.dll` from `Game_Data/Plugins/` so the **original**
  stays (it is still there, md5 `c86908ab…`), keep `RAVENGUN_MATRIX_DLL=1` so the managed patch stays off, and
  run again: `matrix.log` must not be created, the game must log `**** DONGLE NOT FOUND`, and it must quit
  ~30 s later. If it does **not** quit under that negative control, the check is not fatal in this build and
  the 57 s deaths seen earlier had another cause — which would be a finding, not a failed test.

## 6. Evidence log

```
md5sum "Rabbids Hollywood /matrix64.dll" "Rabbids Hollywood /Game_Data/Plugins/matrix64.dll"
  -> c86908ab10b1d9249c896e42b7618f67  93184   (twice)
md5sum "Rabbids Hollywood /matrix32.dll" "Rabbids Hollywood /Game_Data/Plugins/matrix32.dll"
  -> 6e75f453dca9380144620d80f467a0e6  114688  (twice)
ls -la "Rabbids Hollywood /Game_Data/Plugins/"
  -> KaboomAdrioFxPlusFourFeeders64.dll, enet.dll, ftdi.dll, matrix32.dll, pthread.dll, sxCore.dll, usb.dll
cat patches/RabbidsHollywood/dropin/dropin.ini          -> To = Game_Data/Plugins; DllOverride = native; Env = ...
cat patches/RabbidsHollywood/dropin/matrix/matrix.ini   -> poort = 85 ; aantal = 2   (no memory line)
cat patches/RabbidsHollywood/game.ini                   -> [Status] Note: "Dongle_Count(85) -> 2,
    Dongle_ReadSerNr -> 69420, en de game logt zelf '**** DONGLE FOUND: 69420'; geen 'DONGLE NOT FOUND'
    en geen Application.Quit meer" ... "gemeten 2026-10-12, 288 s in de attract-lus"
md5sum patches/RabbidsHollywood/dropin/{dropin.ini,matrix/matrix.ini,matrix64.dll}
  -> 71bbcc7c4ab5b79e9ce2ba5988c7eea6 2946 / 499d686861e967c4421ef2a3959348f6 1347 /
     ef2b7832894118e03ab23ba06dd267d6 25614
```

Not re-measured for this note (read, not redone): the IL of `MatrixManager`, the `WINEDEBUG=+loaddll` run, and
the seven route/measurement table rows above all come from `ravengun/docs/matrix-layer.md` and
`patches/RabbidsHollywood/` as recorded on 2026-10-11/12.
