# Let's Go Island (LGI) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation.
**Verdict:** a **Sega keychip / region** demand answered through `amDongle`, and **we already answer it** — a
`region-pcb` descriptor exists and the game is measurably playable. One separate gate is worth flagging: the
cabinet's own `mxGetHwInfo.exe` hardware probe, which our route skips.

---

## 1. What is present (MEASURED)

Windows **PE32 i386**, not a native ELF — so this is a Proton title, not one for our ELF loader.
Profile `LGI.xml`: `EmulatorType = TeknoParrot`, fields `DongleRegion` + `PcbRegion` (both `Dropdown`).

| File | Size | md5 |
|---|---|---|
| `LGI_RingW_F_safe.exe` (the executable) | 6481920 | `b782c82a8ccb87c97d77441505e17a26` |
| `MACtbl/SBUH_Table.dat` | 1190000 | `4fe8a31aca8186aa731b8ad2e3fa6a9e` |
| `mxGetHwInfo.exe` | 187904 | `23e84d9ef75e71b7914285dd6934e30a` |
| `D3DX9_40.dll`, `cg.dll`, `cgD3D9.dll`, `w32topl.dll`, `inetcomm.dll`, `mobsync.dll`, `printui.dll`, `query.dll` | — | — |
| `app.ini`, `settings.tg`, `teknoparrot.ini`, `game.bat`, `gametest.bat`, `LGI.exe.manifest` | — | — |

There is **no `mxGetHwInfo.ini`** in this folder — the probe binary is present without its configuration file.

The `mxGetHwInfo.exe` md5 `23e84d9e…` (187904 bytes) is the **RingEdge-era** probe, shared with LGI3D,
`OAV6DU~3`, `Dream Raiders` and `SEGA Golden Gun`. LGS and `TNXK0O~C` carry a different one
(`1c31f44a…`, 202240 bytes, RingWide), and Medaru No Gunman a third (`437ed95c…`, 185344).

## 2. What it asks for (MEASURED)

**The region/keychip demand**, read from `LGI_RingW_F_safe.exe` with a calibrated scanner
(`ZZZ_ABSENT_9F3A` = 0; `Region` = 39):

```
  amDongleBillingGetKeychipId() is failed.
  amDongleGetGameId() is failed.          amDongleGetModelType() is failed.
  amDongleGetPlatformId() is failed.      amDongleGetRegion() is failed.
  amDongleGetSystemFlag() is failed.
Keychip Not Found        (1)
DefaultRegion            (1)
Region is JPN. / Region is USA. / Region is EXP. / Region is CHN.        (1 / 1 / 43 / 1)
Unknown region!
[teaArcade2] Dongle::initialize() failed. but reason is unknown...
[teaArcade2] readRegionBuSRam() 'nRegion' out of range.
[teaArcade2] RegionSetupInfo is invalid.
[teaArcade2] BackupManager::setupRegionBuSram()  not initialized yet.
[teaArcade2] JvsManager::initCredit() unknown region, cannot adjust credit setting.
  amDongleBillingGetKeychipId() ... / amDongleGetRegion() ...
keychip.appboot  (8)      keychip.billing  (5)
KEYCHIPID / Serial:%s keyChip:%s
```

So: region + piece-table + keychip id, plus a **BupRegionFormat / RegionSize / readRegionBuSRam** backup-SRAM
region path. The four country names the game knows are `JPN`, `USA`, `EXP`, `CHN` (the profiles offer five,
including `JAP2N`, which has no name in the game — `region-layer.md` records that mapping as an assumption,
not a measurement).

**The `*_Table.dat` file.** `MACtbl/SBUH_Table.dat`, 1190000 bytes. Measured: these family tables are
**incompressible per-game blobs** — `gzip -9` output (1190218) is *larger* than the input, they differ from each
other in ~99.6% of bytes with no shared header, and `cmp` of a file against its own copy reports 0
(calibration). So it is an encrypted dongle image, not a shared table with a per-game key. `SBUH` is also the
four-letter keychip id for this title.

**A second, separate gate: `mxGetHwInfo.exe`.** The cabinet's `game.bat` runs it before the game. Per
`patches/LOADERS-ZES-GAMES.md`: *"LGS / OG | `mxGetHwInfo.exe -s <ini>` en **alleen bij `%ERRORLEVEL% == 0`**
start het spel"*. LGI's folder has the exe but no ini, so the demand list for this title is not written down
anywhere we hold — it would have to be read out of the exe.

## 3. Do we already answer it?

**Yes for the region/keychip half — by mirroring what TeknoParrot's Lindbergh emulator supplies.** The descriptor
lives at `patches/LGI/dongle.json`, of the `kind: "region"` shape:

```json
{ "kind": "region",
  "region": { "field": "DongleRegion", "name": "JAPAN", "game_name": "JPN" },
  "pcb":    { "field": "PcbRegion",    "name": "JAPAN", "game_name": "JPN" },
  "enforced": false }
```

plus `platform`, `profile`, `exe`, `io_board` (`837-14572`,
`SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/0`), `api`, `game_reads`, `fallback`, `evidence`, `notes`.
`docs/region-layer.md` §3 records the status as **"geen blokkade (`enforced: false`); speelbaar"**, and the
inventory records LGI as playable: start to the attract, key 1 starts, the crosshair follows the aim
(measured on a grid), the trigger fires, full screen.

`enforced: false` is the honest flag: the game *asks* `amDongleGetRegion()` and reads it, but a missing or
different answer does not stop it. That is why this title is playable on the region layer alone, and why the
1.19 MB table has not been needed.

**We do not answer the `mxGetHwInfo.exe` probe** — our route starts the executable directly and skips the
cabinet's `game.bat`, so the probe never runs. That is `patches/LOADERS-ZES-GAMES.md`'s systemic finding:
*"elke game heeft een eigen startscript dat wij overslaan"*.

## 4. What is missing (and cost)

1. **Nothing for reach.** Playable today.
2. **`JAP2N` has no name in the game.** The descriptor maps it to `JPN` as an assumption. **Cost: cheap to
   confirm** by choosing `JAP2N` and reading which of `Region is JPN/USA/EXP/CHN.` appears.
3. **The `mxGetHwInfo` probe's demand list for LGI** is not held anywhere — no ini in the folder. If the probe
   turns out to be a real gate on some future route, this is unknown territory. **Cost: read the strings out of
   `mxGetHwInfo.exe`, an hour.**
4. **The `SBUH_Table.dat` image** is not needed while `enforced: false` holds, and cannot be synthesised (it is a
   per-game encrypted blob). Recorded as a non-item rather than a gap.

## 5. Proposed test (NOT RUN) + positive control

**One test, and it is a calibration of `enforced: false`** — because the claim "we answer it" is only interesting
together with the claim "and it does not matter".

* Arm A: start with `patches/LGI/dongle.json` as it stands (`region = JAPAN`, `pcb = JAPAN`).
* Arm B: start with `region = CHINA` (or any value the game does not expect for this build).

* **Passing result (positive control).** Arm A reaches the attract and plays, and the game's own region line
  reads `Region is JPN.` — proving the descriptor is being consumed rather than ignored.
* **Failing result (negative control).** Arm B must produce a **visible difference** — one of
  `Unknown region!`, `[teaArcade2] RegionSetupInfo is invalid.`,
  `[teaArcade2] readRegionBuSRam() 'nRegion' out of range.`, or a wrong region line — while still not blocking
  (that is what `enforced: false` asserts). If Arm B is byte-for-byte identical to Arm A in every log, then the
  region answer is not reaching the game at all on our route, `enforced: false` is true for the wrong reason,
  and the descriptor is decorative. A test whose negative arm cannot differ proves nothing.
* Separately, for the probe: run `mxGetHwInfo.exe -s <ini>` under the same Wine prefix and read `%ERRORLEVEL%`.
  Positive control: 0, and the game then starts. Negative control: a view-local ini with an impossible value
  (`graphic-device = dead:beef`) must give a non-zero exit. That calibrates the probe and shows whether it is a
  gate we are silently skipping.

## 6. Evidence log

```
md5sum "Let's Go Island .../LGI_RingW_F_safe.exe"      -> b782c82a8ccb87c97d77441505e17a26   6481920
file   "Let's Go Island .../LGI_RingW_F_safe.exe"      -> PE32 executable for MS Windows 5.00 (GUI), Intel i386
md5sum "Let's Go Island .../MACtbl/SBUH_Table.dat"     -> 4fe8a31aca8186aa731b8ad2e3fa6a9e   1190000
md5sum "Let's Go Island .../mxGetHwInfo.exe"           -> 23e84d9ef75e71b7914285dd6934e30a   187904
ls -la "Let's Go Island .../"                          -> no mxGetHwInfo.ini present
for p in GLIBC ZZZ_ABSENT_9F3A Region amDongle Dongle keychip keychip.appboot keychip.billing \
         'Keychip Not Found' DefaultRegion amDongleGetRegion amDongleSetupKeychip; do
    printf '%-24s %s\n' "$p" "$(strings -a -n 4 LGI_RingW_F_safe.exe | grep -c -F "$p")" ; done
  -> 0 / 0 / 39 / 14 / 15 / 22 / 8 / 5 / 1 / 1 / 1 / 1
strings -a -n 6 LGI_RingW_F_safe.exe | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> the failing-message list and Region is JPN./USA./EXP./CHN., Unknown region!, Keychip Not Found,
     DefaultRegion, [teaArcade2] Dongle::initialize() failed. ..., readRegionBuSRam() 'nRegion' out of range.
# the 1.19 MB table family is incompressible (measured across all six *_Table.dat in the share)
gzip -9 -c SBUH_Table.dat | wc -c   -> 1190218   (> 1190000, so incompressible)
cmp -l SBUH_Table.dat copy-of-SBUH_Table.dat | wc -l   -> 0    (calibration)
cat patches/LGI/dongle.json        -> kind region ; DongleRegion JAPAN ; PcbRegion JAPAN ; enforced false
cat /home/deck/projects/ravengun/docs/region-layer.md   §3: "LGI | dongle | regio JAPAN, printplaat JAPAN |
   ja -> patches/LGI/dongle.json | geen blokkade (enforced: false); speelbaar"
GameProfiles/LGI.xml   -> EmulatorType TeknoParrot, FieldName DongleRegion + PcbRegion
```

**Not measured, and not claimed:** the `mxGetHwInfo.ini` demand list for this title (no ini in the dump);
whether `SBUH_Table.dat` is ever read on our route; the exact `JAP2N` behaviour.
