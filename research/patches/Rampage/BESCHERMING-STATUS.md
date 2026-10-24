# Rampage — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation + one policy caution.
**Verdict:** a **Matrix/SBK dongle** is present and loaded, but the game **tolerates its absence** — it logs
and plays on. Our drop-in exists and has never been measured in the game. Nothing is missing for reach; the
open question is whether rolling the dongle in is desirable at all for a title that does not need it.

---

## 1. What is present (MEASURED)

Unity game, 64-bit Mono, `EmulationProfile = WartranTroopers`. The game is **not an arcade lightgun title** —
it is a three-player joystick-and-attack game, so its controls are not the dongle's business.

| File | Size | md5 |
|---|---|---|
| `Game.exe` | 650752 | `[Identify] Sha256 = 979f9736a99745454429908227cdd2f48875f193d439cfa33a9e14b4578da95d` |
| `matrix32.dll` (root and `Game_Data/Plugins/`) | 114688 | `6e75f453dca9380144620d80f467a0e6` |
| `matrix64.dll` | 93184 | `c86908ab10b1d9249c896e42b7618f67` |
| `Game_Data/Plugins/KaboomAdrioFxPlusFourFeeders64.dll` | — | `2657236a866337b62ea8a4eb39364367` |
| `Game_Data/Plugins/sxCore.dll`, `ftdi.dll`, `usb.dll`, `enet.dll`, `pthread.dll` | — | — |

Both md5s are the 2009 originals. Note the file naming inconsistency already present in the dump:
`readme.md`-era inventories call the root copy `matrix64.dll` while the profile's `game.exe` is lowercase in
`Rampage.xml` (`ExecutableName = game.exe`) and `Game.exe` on disk — `[Identify]` resolves it.

Note also: `Game_Data/Managed/Assembly-CSharp.dll` contains the literal string **`MATRIX_NO_DONGLE_FOUND`**,
and the game looks for `C:\windows\SYSTEM\MATRIX.INI`, which does not exist. That is the same shape as
Rabbids Hollywood's dead-end path, which is why this game was originally read as "MATRIX, playable".

## 2. What it asks for (MEASURED / from earlier work)

`patches/Rampage/dropin/matrix/matrix.ini` records the IL of `MatrixManager::GetSerial` in
`Game_Data/Managed/Assembly-CSharp.dll`:

```il
IL_001D: ldc.i4.s 85   ; stloc.0 = the port
IL_0021: call MatrixDLLCSharp.MatrixWrapper::Dongle_Count
IL_0038: ldloc.0       ; the same port goes on to Dongle_ReadSerNr
IL_0039: call MatrixDLLCSharp.MatrixWrapper::Dongle_ReadSerNr
```

So the demand is the same three calls as Rabbids Hollywood, on port 85, and the criterion is `> 0`. No dongle
memory on the start-up path (no `memory` line in the shipped `matrix.ini`).

But the **outcome differs**: measured, the game starts and plays with no dongle at all. `findings/Rampage.md`
records it playing through intro movie, tutorial, climbing, walking and demolishing buildings, and states
plainly that "aan de bescherming is niets gedaan of omzeild, niets uitgezocht" — nothing was done to or about
the protection. This game is the reason `findings/MATRIX-dongle-API.md` §4 asked whether one Matrix layer
covers all Matrix games; the answer we now have is that the *layer* is shared but the *strictness* is not:
Rabbids quits, Rampage shrugs.

## 3. Do we already answer it?

**We can, but the game does not require it.** A drop-in is already in place at
`/home/deck/projects/ravengun-patches/patches/Rampage/dropin/`:

| File | Size | md5 |
|---|---|---|
| `dropin.ini` | 2388 | `dcb6d58a7f044203651102faca29627c` |
| `matrix/matrix.ini` | 1456 | `263e3bbd07416e6fa21f8f91852f5e58` |
| `matrix64.dll` | 25614 | `ef2b7832894118e03ab23ba06dd267d6` |
| `ravengun-hasp/Rampage/dongle.json` | 2900 | `edc19fe8c032061b4245714ada0a7b63` |

`To = Game_Data/Plugins` (measured: that is where the calls come from), `DllOverride = native`,
`Env = RAVENGUN_MATRIX_DIR=matrix;RAVENGUN_MATRIX_GAME=Rampage;RAVENGUN_MATRIX_LOG=matrix.log`.
No `RAVENGUN_MATRIX_DLL` — that variable belongs to the Rabbids managed patch, which this game does not have.

Its `dropin.ini` carries an honest warning that is worth repeating, because it is a **policy** issue and not a
technical one: the drop-in engine rolls a drop-in out **always**, not only when a dongle was chosen
(`docs/geen-dongle-in-de-loader.md`). Rampage is playable today *without* a dongle; with this drop-in it gets
one. That change has **never been measured in the game.**
And it sits against the rule the owner set for shared material: *"er wordt nooit iets impliciet aangezet —
een game zonder configuratie gedraagt zich precies zoals vandaag."* For Rampage the safe default is **off**.

## 4. What is missing (and cost)

1. **A decision, not a build.** Either the drop-in stays and is measured, or it is removed / marked opt-in
   because the game does not need it. **Cost: minutes.**
2. **Proof that the drop-in changes nothing.** Because the game tolerates a missing dongle, the only useful
   measurement is a *comparison*, not a success. **Cost: two runs.**
3. If the drop-in is kept: `matrix.ini`'s port 85 should be re-derived rather than trusted — it currently comes
   from the IL listed in §2, which is a sound source, but it was written by hand into the ini.

## 5. Proposed test (NOT RUN) + positive control

**One comparative test.** Same game, same route, two starts, one variable.

* Arm A: with the drop-in in place, `RAVENGUN_MATRIX_LOG` set.
* Arm B: with the drop-in removed, so the original `matrix64.dll` (`c86908ab…`) is what loads.

* **Passing result (positive control).** Arm A produces `matrix.log` with `Init_MatrixAPI -> 0`,
  `Dongle_Count -> aantal, poort = 85`, `Dongle_ReadSerNr -> 69420, poort = 85` — that is the proof our
  library was actually reached. And both arms behave identically in the game: same attract, same coin, same
  start, same measurable progress into a round.
* **Failing result (negative control).** Arm B must produce **no** `matrix.log` (our library is not loaded) —
  that is the control that proves `matrix.log` is evidence of our DLL rather than something the game writes.
  A second negative control for the *value* claim: run Arm A with `matrix.ini` set to `aantal = 0`; if the game
  is gated on the dongle at all, that is exactly when it should fail. If `aantal = 0` changes nothing, the
  check is inert and the drop-in can be dropped with evidence rather than assumption.

## 6. Evidence log

```
md5sum Rampage/matrix32.dll Rampage/Game_Data/Plugins/matrix32.dll   -> 6e75f453dca9380144620d80f467a0e6  114688
md5sum Rampage/matrix64.dll                                        -> c86908ab10b1d9249c896e42b7618f67   93184
md5sum Rampage/Game_Data/Plugins/KaboomAdrioFxPlusFourFeeders64.dll
  -> 2657236a866337b62ea8a4eb39364367      (identical in Tomb Raider/ and Rabbids Hollywood /)
cat patches/Rampage/dropin/matrix/matrix.ini   -> poort = 85 ; aantal = 2 ; IL of MatrixManager::GetSerial quoted
cat patches/Rampage/dropin/dropin.ini          -> To = Game_Data/Plugins ; GEEN RAVENGUN_MATRIX_DLL ;
   "LET OP: de drop-in-motor rolt een drop-in ALTIJD uit ... Deze game is vandaag speelbaar zonder dongle;
    met deze drop-in krijgt hij er een. Dat is nog niet in het spel gemeten"
cat patches/Rampage/game.ini                   -> [Status] Status = playable ... "geen vizier"
findings/Rampage.md                            -> "Eén ding dat op een dongle lijkt: de game laadt
   Game_Data/Plugins/matrix64.dll en zoekt C:\windows\SYSTEM\MATRIX.INI (die ontbreekt); de woorden
   MATRIX_NO_DONGLE_FOUND staan in Assembly-CSharp.dll. Het spel start en speelt toch."
```

Read, not redone for this note: the `MatrixManager::GetSerial` IL, the playability measurements, and the
DXVK / gst-libav findings all come from `findings/Rampage.md` and `patches/Rampage/` as recorded on
2026-10-10/11.
