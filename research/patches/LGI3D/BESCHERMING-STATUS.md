# Let's Go Island 3D (LGI3D) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation.
**Verdict:** the same **Sega keychip / region** demand as Let's Go Island, **already answered** by a `region-pcb`
descriptor, and measurably playable. As with LGI, the cabinet's `mxGetHwInfo.exe` hardware probe is a second,
separate gate that our route skips — and here the probe has no ini either.

---

## 1. What is present (MEASURED)

Windows **PE32 i386** (Proton route, not the ELF loader). Profile `LGI3D.xml`: `EmulatorType = TeknoParrot`,
fields `DongleRegion` + `PcbRegion`.

| File | Size | md5 |
|---|---|---|
| `LGI.exe` (the executable) | 6619648 | `eddbb0cb3e1b7a3a16881d614a7399fd` |
| `MACtbl/SBWB_Table.dat` | 1190000 | `5121b793be7dcd64a0659f3a461cee4f` |
| `mxGetHwInfo.exe` | 187904 | `23e84d9ef75e71b7914285dd6934e30a` |
| `OutputBlaster.dll` | 1288704 | — (+ `OutputBlaster.ini`, 134 bytes) |
| `ViM3DLib.dll` | 140800 | — |
| `ViMCplPage.dll` | 173568 | — |
| `ViMD3DLib.dll` | 37376 | — |
| `vimasdlib.dll` | 1069056 | — |
| `vimasdlib2.dll` | 3046912 | — |
| `vimlicrt.dll` / `vimlicrtx.dll` | 684032 / 874496 | — |
| `vimimage32.dll` | 137216 | — |
| `config.ini` (`[Network]`), `app.ini` (+`.bak`, `~`), `game.bat` (+`~`), `gametest.bat` (+`~`), `mon.bat` | — | — |
| `LGI.exe.MEM_LOG.bin` | 5520 | — |
| `debugout.txt` | 1451 | — |
| `failed_shader.cg` | 31555 | — |
| `copy_1.bat` … `copy_55555.bat` | 147 each | — |
| `msvsmon.exe` | 3201024 | — |

Two things in that listing are worth naming as *not* protection: `msvsmon.exe` and `LGI.exe.MEM_LOG.bin` are
debugging leftovers from the dump, and the five `copy_*.bat` files are an operator's own scratch. The
`vim*`/`ViM*` DLLs are the cabinet's panel/imaging layer, and `OutputBlaster.dll` is the lamp/output driver.
There is **no `mxGetHwInfo.ini`** here either.

## 2. What it asks for (MEASURED)

Calibrated scan of `LGI.exe` (`ZZZ_ABSENT_9F3A` = 0; `Region` = 39):

```
  amDongleBillingGetKeychipId() is failed.
  amDongleGetGameId() is failed.          amDongleGetModelType() is failed.
  amDongleGetPlatformId() is failed.      amDongleGetRegion() is failed.
  amDongleGetSystemFlag() is failed.
Keychip Not Found (1) ; DefaultRegion (1) ; Unknown region!
Region is JPN. (2) / Region is USA. (1) / Region is EXP. (45) / Region is CHN. (1)
  amDongleBillingGetKeychipId() ... / amDongleGetRegion() ...
keychip.appboot (8) ; keychip.billing (5)
```

| needle | `LGI.exe` | `LGI_RingW_F_safe.exe` |
|---|---|---|
| `amDongle` | 14 | 14 |
| `keychip` | 22 | 22 |
| `keychip.appboot` | 8 | 8 |
| `keychip.billing` | 5 | 5 |
| `Keychip Not Found` | 1 | 1 |
| `Region` | 39 | 39 |
| `JPN` / `USA` / `EXP` / `CHN` | 2 / 1 / 45 / 1 | 1 / 1 / 43 / 1 |

So the 3D build and the 2D build ask the **same** thing, with the same API surface: `amDongleGetRegion`,
`GetModelType`, `GetPlatformId`, `GetGameId`, `GetSystemFlag`, `amDongleBillingGetKeychipId`, and the
`keychip.appboot.*` / `keychip.billing.*` key sets — the same shape as Medaru No Gunman and HOTDSD.

The file here is `MACtbl/SBWB_Table.dat` (1190000 bytes, md5 `5121b793…`) — a **different** image from LGI's
`SBUH_Table.dat` (`4fe8a31a…`). Measured, these family tables differ from each other by ~99.6% of bytes with no
shared header, and `gzip -9` output exceeds the input for every one of them (1190218 > 1190000), i.e. they are
**incompressible, encrypted, per-game** blobs. `cmp` of a file against its own copy reports 0, so the diff is
trustworthy. `SBWB` is also the keychip id.

## 3. Do we already answer it?

**Yes, on the region layer, with `enforced: false`.** `patches/LGI3D/dongle.json` is a `kind: "region"`
descriptor: `region` field `DongleRegion` = **USA**, `pcb` field `PcbRegion` = **USA**, `enforced: false`,
plus `platform`, `profile`, `exe`, `io_board`, `api`, `game_reads`, `fallback`, `evidence`, `notes`.
`docs/region-layer.md` §3 records it as *"geen blokkade; speelbaar"*.

The inventory records LGI3D as playable: start to the attract, 3D calibration (red-cyan image), key 1 starts,
the crosshair follows the aim measured on a grid in the middle image, whole screen. Note that LGI (2D) is
`JAPAN/JAPAN` while LGI3D is `USA/USA` — the same hardware family, two regional builds, two descriptors. That
difference is exactly what the shared `region-pcb` type exists to express, and it is worth preserving when the
descriptors are consolidated.

`enforced: false` is again the honest flag: the region is asked for and read, but a missing answer does not stop
the game — which is why the 1.19 MB image has never been needed.

**Not answered: the `mxGetHwInfo.exe` probe.** Our route starts `LGI.exe` directly and never runs the cabinet's
`game.bat`, so the probe does not run. Unknown whether it would pass.

## 4. What is missing (and cost)

1. **Nothing for reach.** Playable today.
2. **`mxGetHwInfo` demand list for this title** — the exe is present without an ini. **Cost: read the strings
   from `mxGetHwInfo.exe`, an hour.**
3. **`SBWB_Table.dat` is a non-item** while `enforced: false` holds and cannot be synthesised (per-game encrypted
   blob). Recorded, not a gap.
4. **Housekeeping, not protection:** the dump carries `msvsmon.exe`, `LGI.exe.MEM_LOG.bin`, `debugout.txt`,
   `failed_shader.cg` and five `copy_*.bat` scratch scripts. They do not affect reach; noting them so that a
   later reader does not mistake them for a protection mechanism or try to "clean" the NAS copy. The NAS is
   read-only and these stay where they are.

## 5. Proposed test (NOT RUN) + positive control

**One test — a calibration of `enforced: false`**, so that "we answer it" is verifiable as *reaching the game*
rather than as a file existing.

* Arm A: start with `patches/LGI3D/dongle.json` as it stands (`USA` / `USA`).
* Arm B: start with `region = JAPAN` (the 2D build's value).

* **Passing result (positive control).** Arm A reaches the attract and plays, and the game's own region line
  reads `Region is USA.` — proving the descriptor value is consumed.
* **Failing result (negative control).** Arm B must differ visibly — a different region line, or one of
  `Unknown region!`, `[teaArcade2] RegionSetupInfo is invalid.`,
  `[teaArcade2] readRegionBuSRam() 'nRegion' out of range.` — while still not blocking. If Arm B is identical
  to Arm A in every log, the region answer is not reaching the game on our route and the descriptor is
  decorative for this title too. A test whose negative arm cannot differ proves nothing.
* The natural pairing: run Arm A on **LGI with LGI3D's `USA/USA`** as well, since the two builds are the same
  family. If LGI3D's `USA` answer also satisfies LGI, the two descriptors can be collapsed to one shared
  entry with two parameters — which is the shared-material shape the owner asked for.

## 6. Evidence log

```
md5sum "Let's Go Island 3D .../LGI.exe"                   -> eddbb0cb3e1b7a3a16881d614a7399fd   6619648
file   "Let's Go Island 3D .../LGI.exe"                   -> PE32 executable for MS Windows 5.00 (GUI), Intel i386
md5sum "Let's Go Island 3D .../MACtbl/SBWB_Table.dat"     -> 5121b793be7dcd64a0659f3a461cee4f   1190000
md5sum "Let's Go Island 3D .../mxGetHwInfo.exe"           -> 23e84d9ef75e71b7914285dd6934e30a   187904
ls -la "Let's Go Island 3D .../"  -> no mxGetHwInfo.ini ; msvsmon.exe, LGI.exe.MEM_LOG.bin, debugout.txt,
                                     failed_shader.cg, copy_1.bat .. copy_55555.bat present
for p in GLIBC ZZZ_ABSENT_9F3A Region amDongle Dongle keychip keychip.appboot keychip.billing \
         'Keychip Not Found' DefaultRegion; do printf '%-22s %s\n' "$p" \
    "$(strings -a -n 4 LGI.exe | grep -c -F "$p")" ; done
  -> 0 / 0 / 39 / 14 / 15 / 22 / 8 / 5 / 1 / 1
strings -a -n 6 LGI.exe | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> amDongleBillingGetKeychipId() is failed. ; amDongleGetRegion() is failed. ; Keychip Not Found ;
     DefaultRegion ; Region is JPN./USA./EXP./CHN. ; Unknown region!
# the table family is incompressible and per-game
gzip -9 -c MACtbl/SBWB_Table.dat | wc -c -> 1190218   ( > 1190000 )
cmp -l MACtbl/SBWB_Table.dat copy-of-itself | wc -l -> 0        (calibration)
cmp -l SBWB_Table.dat SBUH_Table.dat | wc -l -> 1185495         (99.6% differ; no shared header)
cat patches/LGI3D/dongle.json   -> kind region ; DongleRegion USA ; PcbRegion USA ; enforced false
cat /home/deck/projects/ravengun/docs/region-layer.md  §3: "LGI3D | dongle | regio USA, printplaat USA |
   ja -> patches/LGI3D/dongle.json | geen blokkade; speelbaar"
GameProfiles/LGI3D.xml -> EmulatorType TeknoParrot, FieldName DongleRegion + PcbRegion
```

**Not measured, and not claimed:** the `mxGetHwInfo` demand list for this title; whether `SBWB_Table.dat` is
read on our route; why the 2D build is JAPAN and the 3D build USA (build provenance, not a protection fact).
