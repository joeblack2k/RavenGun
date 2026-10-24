# `TNXK0O~C` — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new. **The title is not identified.** Everything below is measured; the *name* of the
game is INFERRED.
**Verdict:** a **2012-generation Sega keychip** title — `amDongle Ver.1.12`, with `amDongleGetSsdResponse`,
`amDongleGetDsMac`, `amDongleGetPicVersion` — carrying a keychip table **`SDDV_Table.dat`** (1190000 bytes) and a
**RingWide** hardware probe. It is very close to Operation G.H.O.S.T. in engine and content, but it is a
different, larger, export-region build. We answer none of the keychip; the profile supplies only
`DongleRegion` + `PcbRegion`.

---

## 1. What is present (MEASURED)

Windows **PE32 i386**. The folder name is an 8.3 volume label, so the title had to be inferred from content.

| File | Size | md5 |
|---|---|---|
| `gs2.exe` | 2704384 | `3b508229088a65baf0329469e1aedab3` |
| `gs2.exe.original` | 2704384 | `a93befe8fa764059b96296f2d40bb5db` |
| **`SDDV_Table.dat`** | **1190000** | **`8f50b15cd355d0c0a359fe18df76f4c4`** |
| `mxGetHwInfo.exe` | 202240 | `1c31f44a89d198bcdf4770c6fc8c9a4e` |
| `mxRingwide.ini` / `mxRingwide.ini.original` | 3467 / 3458 | — |
| `gs2.ini` / `gs2.ini.original` | 718 / 718 | — |
| `game.bat` / `gametest.bat` | 90 / 110 | — |
| `svn_info.txt` | 0 | — (empty) |

Data: `root/` with the same subtree names as `OAV6DU~3` (`Effect`, `Model`, `animation`, `declaration`, `img`,
`locale`, `lua`, `scene`, `shader`, `sound`, `subtitles`, `textparam`, `tonemap`).

`mxGetHwInfo.exe` md5 `1c31f44a…` (202240) is the **RingWide-era** probe — the same file as Let's Go Safari and
`Transformers Human Alliance`, and different from the RingEdge-era `23e84d9e…` used by `OAV6DU~3`.

**Build identity strings in `gs2.exe`:** `SEGA AM Library Ver.1.17 Build:Sep  4 2012 00:17:35`, `amSramInit()`,
`AMLIB INITIALIZE FAILED`, `AMLIB NOT LOADED.`, `FAIL: Init AMLibContext %d`, `.?AVCDipSw@AMLib@@`,
`EnableAMLib`, plus `Lua 5.1.4`. The `amDongle` build is `Ver.1.12 Build:Sep  4 2012 00:17:40` — **the same
day and the same generation as Let's Go Safari's**. So this is the 2012 Sega AM Library generation, not 2011.

`gs2.ini` (`[BBS]`): `CabinetType=0`, `EnableRecoil=1`, `EnableLedBoard=1`, `EnableAMLib=1`, `EnableJvs=1`,
`Difficulty=2`, **`Life=3`**, `Armor=1`, `EnableBlood=1`, **`RomType=4`**, **`Region=4`** (EXP),
**`Language=en`**, `SwipeCard=0`, `AutoShow_ErrorLog=0`, `PrintSvnVersion=0`, the same eight `Version_*` slots,
`Photaesthesia_Version=0`. `gs2.ini` and `gs2.ini.original` are byte-identical in content.

Both `game.bat` and `gametest.bat` follow the cabinet pattern:
```
mxGetHwInfo.exe -s mxRingwide.ini
if %ERRORLEVEL% == 0 gs2.exe          (gametest.bat: gs2.exe -testmode)
```
Unlike `OAV6DU~3`, this one names a file that **exists** — the dump is startable by its own script.

## 2. What it asks for (MEASURED)

Calibrated scan of `gs2.exe` (`ZZZ_ABSENT_9F3A` = 0):

| needle | `TNXK0O~C` | `OAV6DU~3` (OG) | LGS |
|---|---|---|---|
| `amDongle` | **19** | 11 | — |
| `Dongle` | **20** | 12 | — |
| `keychip` / `Keychip` | 22 / 5 | 22 / 4 | 22 / 5 |
| `keychip.appboot` / `keychip.billing` | 8 / 5 | 8 / 5 | 8 / 5 |
| `Region` | 6 | 5 | — |
| `USA` / `EXP` | 7 / 59 | 7 / 59 | — |
| `JPN` / `CHN` | 0 / 0 | 0 / 0 | — |
| `amDongleSetupKeychip` / `amDongleGetRegion` / `amDongleIsAvailable` | 1 / 1 / 1 | 1 / 0 / 1 | 1 / 1 / — |
| `amDongleReadEeprom` / `amDongleReadSram` / `amDongleChipInfo` | **0 each** | 0 each | — |

The API, from the strings — **richer than `OAV6DU~3`'s**, and matching LGS's 2012 set:

```
amDongle Ver.1.12 Build:Sep  4 2012 00:17:40
amDongleInit / amDongleOpen / amDongleOpenEx / amDongleSetupKeychip / amDongleUpdate / amDongleBusy
amDongleResponseCheck / amDongleSendAndRecvEx / amDongleIsAvailable
amDongleGetRegion / amDongleGetGameId / amDongleGetModelType / amDongleGetPicVersion
amDongleGetDsMac          <- device/DS MAC
amDongleGetSsdResponse    <- SSD host proof
amDongleBillingGetKeychipId
keychip.appboot.{dvdflag,formattype,...} ; keychip.billing.*
KEYCHIP  : %s ; KEYCHIP ACCESS FAILED ; Faild to get Keychip ID. ; Faild to get Region. ; GetRegion
BoardRegion:%d ; KeyRegion: %d ; WRONG REGION ; Unknown region %d
Init: Faild to get DangleInfo ... ; Init: amDongleIsAvailable() is false ; Error Response KeychipError !!!
```

So it is the **2012 keychip generation**: `amDongleGetSsdResponse` is the SSD host proof (the same capability
HOTDSD's `amdaemon` exposes as `amDongleRequestSsdHostProof`), `amDongleGetDsMac` a device MAC, and
`amDongleGetPicVersion` a picture/version read. This build asks for **more** than the 2011 build does.

**The keychip table is present and is an encrypted per-game image.** `SDDV_Table.dat`, 1190000 bytes,
md5 `8f50b15c…`. Measured across the whole family: `gzip -9` output (1190218) exceeds the input, the six
`*_Table.dat` files differ from each other in ~99.6% of bytes with no shared header, and `cmp` of a file against
its own copy reports 0 (calibration). So it cannot be derived from another title's table and cannot be
synthesised.

Note the naming pattern that the family now makes visible: **the table filename is the Sega game id** — `SBUH`
for LGI, `SBWB` for LGI3D, `SBYR` for the Transformers titles, `SBRZ`/`SBTF` for Medaru No Gunman, `SDDV` here —
and Let's Go Safari's `gameid.txt` reads **`SDAX`**, which is why LGS has no table of that name. So `SDDV` is
strong evidence that **this title's game id is `SDDV`**, and `SDDV` is also one of the `keychip.appboot.gameid`
inputs.

## 3. What it is (INFERRED) — and why the identification matters

**Not determined.** What is measured:

* it shares the `gs2.exe` engine name, the full `[BBS]` ini schema and nearly the whole `root/scene/` tree with
  `OAV6DU~3` (Operation G.H.O.S.T.): both contain `Cam3DUI.xml`, `EffectViewer.xml`, `LoopDemo`, `NameEntry`,
  `SoundAdjust`, `advertise`, `demoplay`, `ending`, `gameover`, `howtoplay`, `led_blue/flash/…`, and military
  names such as `led_handgrenade`, `led_m3ambient`, `led_damage`, `led_deathlimit`;
* this one adds `led_teamorder` and `led_windowcrash`; `OAV6DU~3` adds `instruction`;
* it is **45568 bytes larger** than `OAV6DU~3`'s executable, is a **later build** (AM Library 1.17 / Sep 2012 vs
  `OAV6DU~3`'s amDongle 1.09 / May 2011), is set to **`Region=4` (EXP)** and `Language=en` while `OAV6DU~3` is
  `Region=1` (JPN), and ships **English and Chinese locale art** (`root/locale/EC*.en.png` and `EC*.zh.png`);
* it carries a keychip table and `OAV6DU~3` does not.

**INFERRED:** this is a **later, export-region build of the same game family as `OAV6DU~3` (Operation G.H.O.S.T.)
— most plausibly a sequel or a re-release** — with a Chinese-market locale set. It is **not** identified to a
title, and no TeknoParrot profile has been matched to it in this pass.

Why it matters: `OG.xml` is the profile for Operation G.H.O.S.T., and if this is a *different* title then
attaching `OG.xml` to it would be a mis-attribution that would silently inherit the wrong `DongleRegion` /
`PcbRegion`. That is exactly the failure mode `region-pcb`'s `enforced: false` makes invisible.

## 4. What is missing (and cost)

1. **The title itself.** Until it is named, no descriptor can be trusted. **Cost: the identification test in §5
   — likely under an hour, because the game's own text is in `root/textparam/` and `root/locale/`.**
2. **The shared `sega-keychip` layer**, including the 2012-only calls `amDongleGetSsdResponse`,
   `amDongleGetDsMac`, `amDongleGetPicVersion` and `amDongleBillingGetKeychipId`. **Cost: shared with the other
   five keychip titles; 2–4 days for a first shim, plus the SSD-proof answer.**
3. **A descriptor** in `patches/TNXK0O~C/` once the title is known — region, pcb, `gameid = SDDV`, and the
   keychip parameters. **Cost: hours.**
4. **The `mxRingwide.ini` demand list**, present and unread in detail, and the `mxGetHwInfo.exe` gate that
   `game.bat` enforces (`%ERRORLEVEL% == 0`). **Cost: an hour.**
5. **`SDDV_Table.dat` is a non-item** unless the keychip layer turns out to need the real image — the same
   reasoning that lets TeknoParrot run the Ring titles with only a region and a pcb id. **Recorded, not a gap.**

## 5. Proposed test (NOT RUN) + positive control

**Two tests; the first one is the identification, and it must come first** — testing the dongle of a game we
cannot name is how a measure lands on the wrong title.

**Test 1 — identify the title (static, no run).**
* Read the game's own text and locale assets: `root/textparam/*.txt` (the same tree that in `OAV6DU~3` holds
  `e_Main.txt`, `e_Jvs.txt`, `e_Gun.txt`, `e_HUD.txt`, …), `root/locale/EC*.en.png` / `EC*.zh.png`, and
  `root/scene/howtoplay` / `demoplay` / `advertise`; then look for a title string in `gs2.exe` and compare
  against the TeknoParrot game list to find the profile it belongs to.
* **Passing result (positive control).** A title string or a profile match is found, and it *disagrees or agrees
  with* `OG.xml` explicitly. Either outcome is a result: agreement means `OG.xml` can be reused; disagreement
  means this is a separate profile and must get its own descriptor.
* **Failing result (negative control).** Calibration that the search is capable of finding a title at all:
  the identical method applied to `OAV6DU~3` must surface its Operation G.H.O.S.T. identity. If the method
  cannot identify the title we *already* know, it cannot identify this one either, and an empty result here
  means nothing.

**Test 2 — the keychip, same shape as Operation G.H.O.S.T.'s.**
* Arm A: start with only `DongleRegion` / `PcbRegion` supplied; capture the dongle lines.
* Arm B: the same plus the 2012 `sega-keychip` shim.
* **Passing result (positive control).** Arm A prints `BoardRegion:<n>` and `KeyRegion: <n>` (proving the path is
  observable); Arm B additionally clears `Init: amDongleIsAvailable() is false`, `Faild to get Keychip ID.`,
  `Faild to get Region.`, `KEYCHIP ACCESS FAILED` and `Error Response KeychipError !!!`, and `KEYCHIP  : %s`
  names an id.
* **Failing result (negative control).** Arm A must show `Init: amDongleIsAvailable() is false`; if it does not,
  the keychip is not on this build's critical path. And a wrong-region control: change the region in a
  **view-local** `gs2.ini` and confirm `WRONG REGION` or `Unknown region %d`. A test whose negative arms cannot
  differ proves nothing.

## 6. Evidence log

```
ls -la "TNXK0O~C/"  -> gs2.exe 2704384 ; gs2.exe.original 2704384 ; SDDV_Table.dat 1190000 ;
                       mxGetHwInfo.exe 202240 ; mxRingwide.ini 3467 + .original 3458 ;
                       gs2.ini 718 + .original 718 ; game.bat 90 ; gametest.bat 110 ; svn_info.txt 0 ; root/
md5sum gs2.exe gs2.exe.original                -> 3b508229088a65baf0329469e1aedab3 / a93befe8fa764059b96296f2d40bb5db
md5sum SDDV_Table.dat                          -> 8f50b15cd355d0c0a359fe18df76f4c4   1190000
md5sum mxGetHwInfo.exe                         -> 1c31f44a89d198bcdf4770c6fc8c9a4e   202240
   (same file as Let's Go Safari and Transformers Human Alliance; different from OAV6DU~3's 23e84d9e)
cat game.bat        -> "mxGetHwInfo.exe -s mxRingwide.ini" then "if %ERRORLEVEL% == 0 gs2.exe"
cat gametest.bat    -> same, with "gs2.exe -testmode"
cat gs2.ini         -> [BBS] Region=4 RomType=4 Language=en Life=3 EnableLedBoard=1 EnableRecoil=1
                       EnableAMLib=1 + eight Version_* slots ; identical to gs2.ini.original
strings -a -n 6 gs2.exe | grep -i -E 'sega|AMLib|copyright|\(c\)|ver\.'
  -> SEGA AM Library Ver.1.17 Build:Sep  4 2012 00:17:35 ; AMLIB INITIALIZE FAILED ; AMLIB NOT LOADED. ;
     FAIL: Init AMLibContext %d ; .?AVCDipSw@AMLib@@ ; EnableAMLib ;
     Init: Failed to init SramLib. (amSramInit() Faild) ; Lua 5.1.4
strings -a -n 6 gs2.exe | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> amDongle Ver.1.12 Build:Sep  4 2012 00:17:40 ; amDongleGetRegion / GetGameId / GetModelType /
     GetPicVersion / GetDsMac / GetSsdResponse ; amDongleBillingGetKeychipId ; amDongleSetupKeychip ;
     keychip.appboot.* ; keychip.billing.* ; KEYCHIP  : %s ; KEYCHIP ACCESS FAILED ;
     Faild to get Keychip ID. ; Faild to get Region. ; BoardRegion:%d ; KeyRegion: %d ; WRONG REGION ;
     Unknown region %d ; Init: amDongleIsAvailable() is false ; Error Response KeychipError !!!
for p in ZZZ_ABSENT_9F3A amDongle Dongle keychip Keychip Region USA JPN EXP CHN \
         amDongleSetupKeychip amDongleGetRegion amDongleIsAvailable amDongleReadEeprom amDongleReadSram \
         keychip.appboot keychip.billing; do
    printf '%-24s %s\n' "$p" "$(strings -a -n 4 gs2.exe | grep -c -F "$p")" ; done
  -> 0 / 19 / 20 / 22 / 5 / 6 / 7 / 0 / 59 / 0 / 1 / 1 / 1 / 0 / 0 / 8 / 5
# the family table set, and why the filename is the game id
find "$NAS" -iname '*_Table.dat'  ->  SBRZ, SBTF (Medaru) ; SBUH (LGI) ; SBWB (LGI3D) ; SDDV (here) ;
                                      SBYR (x3: Sega/Transformers2, Transformers Human Alliance,
                                      Transformers Shadows Rising)
cat "Let's Go Safari/gameid.txt"   -> SDAX      (LGS has no matching table)
# the table images are incompressible and per-game
gzip -9 -c SDDV_Table.dat | wc -c          -> 1190218   (> 1190000)
cmp -l SDDV_Table.dat copy-of-itself | wc -l -> 0        (calibration)
cmp -l SDDV_Table.dat SBUH_Table.dat | wc -l -> 1185264  (99.6% differ; no shared header)
find "TNXK0O~C/root/locale" | head  -> EC0.en.png, EC0.zh.png, EC1000.en.png, EC1000.zh.png, ... (en + zh)
```

**Not measured, and not claimed:** the title; whether `OG.xml` is the right profile for it; whether the keychip
is on the critical path; what `mxRingwide.ini` demands.
