# Drakons Realm Keepers — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14. **Status of this note:** new; this game was
not in our inventory before.
**Verdict:** a **Matrix/SBK dongle** game of exactly the same shape as Rabbids Hollywood, and **our existing
`matrix64.dll` already answers everything it asks** — but no drop-in exists for it yet, and it has never been
run with the dongle answered, so what is missing is the *delivery*, not the *emulation*.

---

## 1. What is present (MEASURED)

Platform, from TeknoParrot's own metadata (`TeknoParrotUi-2.0.0.130-linux-x64/Metadata/Drakons.json`):
`"platform": "Adrenaline Amusements PC-Based"`, `"release_year": "2023"`, `"game_genre": "Shooter"`.
Profile `Drakons.xml`: `EmulationProfile = WartranTroopers`, `Is64Bit = true`, `GunGame = true`,
`ExecutableName = Game.exe`. `GameSetup/Drakons.xml` is trivial — **TeknoParrot expects no dongle file for
this game**.

The build is **Unity IL2CPP, 64-bit** (`GameAssembly.dll` + `Game_Data/il2cpp_data/Metadata/global-metadata.dat`;
metadata magic `0xFAB11BAF`, version `29`).

| File | Size | md5 |
|---|---|---|
| `Game.exe` | 650752 | `566a80087807f8081b652076a8a47362` |
| `GameAssembly.dll` | 35264000 | `783a592917167b3a3a3e42f9f0717a06` |
| `baselib.dll` | 405016 | `abec43c8c411cc895c48f8bd69646e2f` |
| `UnityPlayer.dll` | 48999960 | `de65276bb5b90887d89ba045876659ca` |
| `Game_Data/il2cpp_data/Metadata/global-metadata.dat` | 7873652 | — |
| **`Game_Data/Plugins/x86_64/matrix64.dll`** | **93184** | **`c86908ab10b1d9249c896e42b7618f67`** |

That last md5 is the important one: it is the **original 2009 Matrix LPT-dongle DLL**, byte-identical to the
copy in Rampage, Tomb Raider and Rabbids Hollywood. This game ships the *real* dongle library — TeknoParrot
has **not** replaced it with its own emulator (contrast Infinity Blade FX, where `matrix32.TEKNOPARROT.dll`
holds the original and TeknoParrot's 109056-byte emulator took its place).

The internal project name is in `Game_Data/app.info`:
```
Sarbakan
09038_Adrenaline_Skyride_Turret
```
and `Game_Data/boot.config` repeats it (`player-connection-project-name=09038_Adrenaline_Skyride_Turret`).
`Game_Data/ScriptingAssemblies.json` lists the game's own assemblies, including **`SBK.Matrix.dll`** — a real,
separate Matrix assembly (in Rabbids Hollywood the wrapper lives inside `Assembly-CSharp.dll`) — alongside
`SBK.Kaboom.dll`, `SBK.ArcadeFlow.dll`, `SBK.ArcadeSystem.dll`, `SBK.SaveGame.dll`, `SBK.CreditManagement.dll`,
`SBK.TicketFeeder.dll`, `ChannelPurchase.dll`, `SBK.Cheats.dll`.

Family evidence: the whole product line shares the same I/O plug-ins. `KaboomAdrioFxPlusFourFeeders64.dll`
md5 `2657236a866337b62ea8a4eb39364367` sits in `Tomb Raider/`, `Rabbids Hollywood /` and `Rampage/`;
Drakons ships the same file names, plus `KS_Diagnostics_Process.dll`, `AkSoundEngine.dll` and `RS_232_DLL.dll`.

---

## 2. What it asks for (MEASURED)

Read out of the two IL2CPP string tables with `strings`, calibrated against a known-present string
(`UnityEngine` = 1597 hits in `GameAssembly.dll`, 3383 in the metadata) and a deliberately-absent one
(`ZZZ_CALIBRATION_ABSENT_9F3A` = 0 in both). An empty result is therefore meaningful.

**The native entry points this game demands — five, and no more:**

| Export | `GameAssembly.dll` | `global-metadata.dat` |
|---|---|---|
| `Init_MatrixAPI` | 1 | 1 |
| `Dongle_Count` | 1 | 1 |
| `Dongle_ReadSerNr` | 1 | 1 |
| `Dongle_EncryptData` | 1 | 1 |
| `Dongle_DecryptData` | 1 | 1 |
| *the other 108 Matrix names* | **0** | **0** |

`Dongle_Find`, `Dongle_MemSize`, `Dongle_ReadData`, `Dongle_WriteData`, `Dongle_Version`, `Dongle_Model`,
`Dongle_Exit`, `Release_MatrixAPI`, `GetVersionAPI`, `GetPortAdr`, `Pause/ResumePrinterActivity`,
`MasterKey_Find`, `ReadPort`, `WritePort` — all **0 hits**. So there is **no dongle memory** in the picture:
like Rabbids Hollywood, this is a numbers-only check.

The metadata string table gives the assembly layout in declaration order, which is as good as a class listing:

```
SBK.Matrix / SBK.Matrix.dll / MatrixWrapper / MatrixDLLCSharp
Init_MatrixAPI / Dongle_Count / UserCode / DngNr / Dongle_ReadSerNr / DataBlock
Dongle_EncryptData / Dongle_DecryptData / MatrixManager
get_GetDongleSerialNumber / set_GetDongleSerialNumber / get_IsDonglePresent / set_IsDonglePresent
GetDeviceId / ReadSerial / bData / EncryptWithDongle / i_In / DecryptWithDongle / i_Out
Dongle / mb_Init / DongleId / UsbPort
<GetDongleSerialNumber>k__BackingField / <IsDonglePresent>k__BackingField
GetDongleSerialNumber / IsDonglePresent
MatrixError / MATRIX_NO_ERROR / MATRIX_NOT_INITIALIZED / MATRIX_NO_DONGLE_FOUND / MATRIX_DONGLE_READ_ERROR
MatrixDongle
```

Facts that follow from that, and matter:

* **Parameter names are visible**: `Dongle_Count(UserCode)` and `Dongle_ReadSerNr(DngNr, …)`. **`UserCode` is
  not necessarily 85.** We cannot read the constant without decompiling the IL2CPP body — see §4.
* **The same three log messages as Rabbids Hollywood** are in the metadata:
  `***** DONGLE FOUND: `, `**** DONGLE NOT FOUND`, `***** Failed to initialized MatrixAPI correctly`.
* **The same four-valued error enum as Rabbids** (`MATRIX_NO_ERROR` / `NOT_INITIALIZED` / `NO_DONGLE_FOUND` /
  `DONGLE_READ_ERROR`). `MATRIX_DONGLE_READ_ERROR` most plausibly maps to the `Dongle_ReadSerNr <= 0` case —
  the same code 3 that killed Rabbids before `Dongle_ReadSerNr` was answered.
* The P/Invoke module name is the UTF-16LE string **`matrix64`**, listed among the native library names
  (`advapi32.dll`, `api-ms-win-core-timezone-l1-1-0.dll`, `Kernel32.dll`, `libc`, `matrix64`,
  `libmono-btls-shared`, `iphlpapi.dll`, `kernel32`) — so the runtime builds `matrix64.dll` and loads it, and
  the game's own `Game_Data/Plugins/x86_64/` is where that resolves.
* `UsbPort` and `ReadSerial` exist as names, but `Dongle_FindEx` / `GetVersionDRV_USB` are absent, so this is
  the **LPT** path, not the USB variant.

---

## 3. Do we already answer it?

**Yes — with the library we already ship, unchanged.** Our `matrix/src/matrix.c` answers all five:

| Call | Our answer | Source |
|---|---|---|
| `Init_MatrixAPI()` | `0` | `matrix.c`, returns 0 unconditionally |
| `Dongle_Count(short poort)` | `2` | `mx_aantal`, default 2 — **the port argument is ignored** (`(short)mx_aantal`), so `UserCode` being unknown does not matter |
| `Dongle_ReadSerNr(...)` | `69420` (`0x10F2C`) | returns the constant, ignoring every argument |
| `Dongle_EncryptData(...)` | `0`, buffer untouched | deliberate, same as the HASP layer |
| `Dongle_DecryptData(...)` | `0`, buffer untouched | idem |

This is a **strict subset** of what Rabbids Hollywood demands, and that game is measured working through our
drop-in (288 s in the attract loop; the game itself logs `**** DONGLE FOUND: 69420`). So the *shared* piece is
already built and already proven on a sibling title: `matrix/out/matrix64.dll`, md5
`a2dc210fd57d6d722efd3e15324a7b64` (25614 bytes), the same bytes served as
`patches/RabbidsHollywood/dropin/matrix64.dll` (md5 `ef2b7832894118e03ab23ba06dd267d6` — a different build of
the same source; both export 113 names).

What does **not** exist: `patches/DrakonsRealmKeepers/` in any form. No `game.ini`, no `dropin/`, no
`matrix.ini`.

Note on the mechanism: because it is IL2CPP, the Rabbids *managed-patch* route (Harmony on
`MatrixManager.Awake`) does **not** exist here — there is no managed IL to patch. The DLL drop-in is the only
route, which is exactly the case `docs/matrix-layer.md` §9 anticipated.

---

## 4. What is missing (and cost)

1. **The drop-in itself.** `patches/DrakonsRealmKeepers/dropin/` with:
   * `matrix64.dll` — reuse `matrix/out/matrix64.dll`;
   * `matrix/matrix.ini` — `poort = <the UserCode the game passes>` and `aantal = 2`;
   * `dropin.ini` — `[matrix64.dll] To = Game_Data/Plugins/x86_64`, `DllOverride = native`,
     `Env = RAVENGUN_MATRIX_DIR=matrix;RAVENGUN_MATRIX_GAME=DrakonsRealmKeepers;RAVENGUN_MATRIX_LOG=matrix.log`.
   The destination differs from every other Matrix game we have: this one is under
   **`Game_Data/Plugins/x86_64/`**, not `Game_Data/Plugins/`. That path must be local in the view as well.
   **Cost: ~1 hour**, no new code.
2. **The `UserCode`, and the game's exact criteria.** `poort` is only used for logging by our library, so this
   is not blocking — but it is the one number we would want for the record. Getting it needs an IL2CPP dump of
   `MatrixManager` (Il2CppDumper or equivalent); **cost: a few hours**, and it is optional.
3. **Whether the check is fatal.** Unknown, and it decides whether the drop-in is *required* or merely
   *correct*. See the biggest unknown below. **Cost: one measured run.**
4. **Whether `Dongle_EncryptData`/`Dongle_DecryptData` are live.** `EncryptWithDongle` / `DecryptWithDongle`
   and `GetDeviceId` exist as method names, so there *is* a wrapper — but in Rabbids Hollywood those same
   wrappers were proven dead code by IL analysis. Here we cannot yet tell. If Drakons really encrypts its save
   through the dongle, our no-op returns 0 and leaves the buffer alone, and the same "does persistence
   survive two starts?" question as Rabbids applies. Same open item as `patches/RabbidsHollywood` §10/§11.
   **Cost: a two-start savegame test.**

---

## 5. Proposed test (NOT RUN) + positive control

**One test.** Put the drop-in in place for Drakons, then start it once through the normal route with
`RAVENGUN_MATRIX_LOG` set. Do not run on the busy bench — this is queued.

* **Passing result (positive control).** Two independent logs agree:
  1. our own `matrix.log` contains `Init_MatrixAPI -> 0`, `Dongle_Count -> aantal, poort = <N>`,
     `Dongle_ReadSerNr -> 69420, poort = <N>` — proof that *our* library was called and not the original;
  2. the game's own log shows `***** DONGLE FOUND:` and **never** `**** DONGLE NOT FOUND`, and the game is
     still running well past one minute after the check.
* **Failing result (negative control).** The same run **with the drop-in removed**, so the original 2009
  `matrix64.dll` stays in `Game_Data/Plugins/x86_64/`. Then `matrix.log` must be absent-or-empty (our library
  is not loaded at all) and — if the check is fatal, as in Rabbids — the game must quit roughly 30 s after the
  check, with `**** DONGLE NOT FOUND` on the way out.
* **The interesting third outcome.** If the negative control *does not* quit, Drakons tolerates a missing
  dongle exactly like Rampage does. That is a real result, not a failed test: it would demote this item to
  "the drop-in is correct but not required", and it would close the Rampage puzzle from
  `findings/MATRIX-dongle-API.md` §4 for a second title.

---

## 6. Evidence log

All commands run read-only on the deck. NAS was never written.

```
# What the game is and what it ships
ls -la "/home/deck/mnt/spidernas_emulation/Teknoparrot/Drakons Realm Keepers"
file  .../Game.exe .../GameAssembly.dll .../baselib.dll .../UnityPlayer.dll
md5sum .../Game_Data/Plugins/x86_64/matrix64.dll
  -> c86908ab10b1d9249c896e42b7618f67  93184  (== the 2009 original, same as Rabbids/Rampage/TombRaider)
cat .../Game_Data/app.info
  -> Sarbakan / 09038_Adrenaline_Skyride_Turret
cat .../Game_Data/ScriptingAssemblies.json | tr ',' '\n'
  -> includes SBK.Matrix.dll, SBK.Kaboom.dll, SBK.ArcadeFlow.dll, SBK.SaveGame.dll, ...
cat .../Game_Data/boot.config      -> player-connection-project-name=09038_Adrenaline_Skyride_Turret
cat .../MireSeparation.json        -> {"Offset":0.5}   (14 bytes; nothing to do with protection)

# Calibrated string scan of the two IL2CPP tables
GA=.../GameAssembly.dll ; MD=.../Game_Data/il2cpp_data/Metadata/global-metadata.dat
strings -a -n 4 "$GA" | grep -c -F UnityEngine        -> 1597
strings -a -n 4 "$MD" | grep -c -F UnityEngine        -> 3383
strings -a -n 4 "$GA" | grep -c -F ZZZ_CALIBRATION_ABSENT_9F3A  -> 0
strings -a -n 4 "$MD" | grep -c -F ZZZ_CALIBRATION_ABSENT_9F3A  -> 0
for p in Init_MatrixAPI Dongle_Count Dongle_ReadSerNr Dongle_EncryptData Dongle_DecryptData \
         Dongle_Find Dongle_MemSize Dongle_ReadData Dongle_WriteData Dongle_Version Dongle_Model \
         Release_MatrixAPI GetVersionAPI GetPortAdr MasterKey_Find ReadPort WritePort; do
    printf '%-22s %s\n' "$p" "$(strings -a -n 4 "$GA" | grep -c -F "$p")" ; done
  -> the first five = 1 each; every other line = 0

# The declaration-order window in the metadata
strings -a -n 3 "$MD" | grep -n -A 30 -B 10 'MatrixDLLCSharp'
  -> SBK.Matrix / SBK.Matrix.dll / MatrixWrapper / MatrixDLLCSharp / Init_MatrixAPI / Dongle_Count / UserCode /
     DngNr / Dongle_ReadSerNr / DataBlock / Dongle_EncryptData / Dongle_DecryptData / MatrixManager /
     get_GetDongleSerialNumber ... MatrixError / MATRIX_NO_ERROR / MATRIX_NOT_INITIALIZED /
     MATRIX_NO_DONGLE_FOUND / MATRIX_DONGLE_READ_ERROR / MatrixDongle
strings -a -n 4 "$MD" | grep -i 'dongle'
  -> get_GetDongleSerialNumber, get_IsDonglePresent, ... plus
     "***** DONGLE FOUND: " / "**** DONGLE NOT FOUND" / "***** Failed to initialized MatrixAPI correctly"
strings -a -e l -n 3 "$GA" | grep -n -A 4 -B 4 'matrix64'
  -> matrix64 listed among the P/Invoke module names (advapi32.dll, Kernel32.dll, libc, iphlpapi.dll, ...)

# Our side: the port argument is ignored, so UserCode does not matter
sed -n '375,424p' /home/deck/projects/ravengun/matrix/src/matrix.c
  -> short MX_CALL Dongle_Count(short poort) { mx_gegevens_vinden(); mx_log_regel(...); return (short)mx_aantal; }
  -> int32_t MX_CALL Dongle_ReadSerNr(...) { ...; return MX_SERIENUMMER; }     /* 0x10F2C = 69420 */
```

**Not measured, and not claimed:** whether the failure path quits; what constant is passed as `UserCode`;
whether `EncryptWithDongle`/`DecryptWithDongle` are reachable at runtime.
