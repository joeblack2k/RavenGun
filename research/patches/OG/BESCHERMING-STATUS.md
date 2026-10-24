# Operation G.H.O.S.T. (NAS folder `OAV6DU~3`) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new.
**Verdict:** a **Sega keychip** title of the **2011 generation** (`amDongle Ver.1.09`), with
`keychip.appboot.*` / `keychip.billing.*` and a **RingWide** hardware probe in front of it. Profile `OG.xml`
asks only for `DongleRegion` + `PcbRegion`; the keychip itself we answer **not at all**. **No keychip table file
is present in this dump** — and unlike the other keychip titles, the cabinet's own `game.bat` here points at a
filename that does not exist, so this dump's start path is already broken by hand.

---

## 1. What is present (MEASURED)

Windows **PE32 i386**. Profile `OG.xml`: `EmulatorType = TeknoParrot`, `ExecutableName = gs2.exe`, fields
`DongleRegion` + `PcbRegion`.

| File | Size | md5 |
|---|---|---|
| `gs2.exe` (the executable) | 2658816 | `54718bfa35392c36b0480f71335778fc` |
| `gs2.exe.ori` | 2658816 | `40f795933abc4f441c98acc778610aa2` |
| `gs2.exe_1080` | 2658816 | `f837cc79d4cf980ae62b71e8214d1e2f` |
| `OpGhost_ResChanger.exe` | 12800 | — (+ `OpGhost_ResChanger.txt`, 185) |
| `OutputBlaster.dll` | 1308160 | — (+ `OutputBlaster.ini`, 114) |
| `mxGetHwInfo.exe` | 187904 | `23e84d9ef75e71b7914285dd6934e30a` |
| `mxRingwide.ini` | 3461 | — |
| `gs2.ini` | 693 | — |
| `game.bat` / `gametest.bat` | 69 / 79 | — |
| `inetcomm.dll`, `mobsync.dll`, `printui.dll`, `query.dll`, `w32topl.dll` | — | — |
| `svn_info.txt` | 0 | — (empty) |

Data: `root/` with `Effect`, `Model`, `animation`, `declaration`, `img`, `locale`, `lua`, `scene`, `shader`,
`sound`, `subtitles`, `textparam`, `tonemap`; plus `log/`.

`mxGetHwInfo.exe` here is the **RingEdge-era** build (`23e84d9e…`, 187904) — the same file as LGI, LGI3D,
`Dream Raiders` and `SEGA Golden Gun`. There is **no `mxGetHwInfo.ini`**; the RingWide probe config is
`mxRingwide.ini` instead.

**Three copies of the executable, two of them hand-made.** `gs2.exe`, `gs2.exe.ori` and `gs2.exe_1080` are all
2658816 bytes with three different md5s, and `OpGhost_ResChanger.exe` plus `OpGhost_ResChanger.txt` sit beside
them. So this dump has already been resolution-patched by its operator. Its `game.bat`:

```
mxGetHwInfo.exe -s mxRingwide.ini
if %ERRORLEVEL% == 0 gs2.exe.bak
```

— it launches **`gs2.exe.bak`**, and no such file exists in the folder. `gametest.bat` has the same defect.
**MEASURED: this dump cannot be started by its own script.** That is a dump-integrity fact, not a protection
fact, but it is the kind of thing that wastes a session if it is not written down.

`gs2.ini` (`[BBS]`) records the cabinet configuration: `CabinetType=0`, `EnableRecoil=1`, `EnableLedBoard=0`,
`EnableAMLib=1`, `EnableJvs=1`, `Difficulty=2`, `Life=8`, `Armor=1`, `EnableBlood=1`, `RomType=1`,
**`Region=1`** (JPN), `Language=en`, `SwipeCard=0`, the eight `Version_*` slots (Korean and Japanese developer
names), `Photaesthesia_Version=0`.

## 2. What it asks for (MEASURED)

Calibrated scan of `gs2.exe` (`ZZZ_ABSENT_9F3A` = 0):

| needle | hits |
|---|---|
| `amDongle` / `Dongle` | 11 / 12 |
| `keychip` / `Keychip` | 22 / 4 |
| `keychip.appboot` / `keychip.billing` | 8 / 5 |
| `Region` | 5 |
| `USA` / `EXP` | 7 / 59 |
| `JPN` / `CHN` | 0 / 0 |
| `amDongleSetupKeychip` / `amDongleIsAvailable` | 1 / 1 |
| `amDongleReadEeprom` / `amDongleReadSram` / `amDongleChipInfo` / `amDongleIsDevelop` | **0 each** |
| `amDongleGetRegion` / `Keychip Not Found` / `DefaultRegion` | 0 / 0 / 0 |

The API surface, in order from the strings:

```
amDongle Ver.1.09 Build:May 12 2011 01:09:02
amDongleOpen / amDongleOpenEx / amDongleSetupKeychip / amDongleUpdate / amDongleBusy
amDongleResponseCheck / amDongleSendAndRecvEx / amDongleIsAvailable
Faild to amDongleExit()
keychip.appboot.dvdflag / .formattype / ...
keychip.billing ...
KEYCHIP  : %s          KEYCHIP ACCESS FAILED
Faild to get Keychip ID.        Faild to get Region.        GetRegion
BoardRegion:%d        KeyRegion: %d        Region        WRONG REGION
Unknown region %d
Init: Faild to get DongleInfo. (AMU::Dongle::Info() Faild)
Init: Faild to init DongleLib. (amDongleInit() Faild)
Init: amDongleIsAvailable() is false
Error Response KeychipError !!!
```

So this is the **2011 keychip generation**: it wants the keychip id and the region, and it has the
`keychip.appboot.*` / `keychip.billing.*` key namespaces. Notably it does **not** have the `amDongleReadEeprom` /
`amDongleReadSram` / `amDongleChipInfo` calls that the 2007 generation (Ghost Squad Evolution, LGJ, LGJS) has —
the keychip generation replaced the raw EEPROM reads with the keychip value set. Two different APIs, not two
versions of one.

**`BoardRegion:%d` and `KeyRegion: %d` are separate** — a board region and a key region, both printed, plus
`WRONG REGION` and `Unknown region %d`. That pair is the same "dongle region vs main region must agree" shape
Medaru No Gunman shows as `CheckRegion_EepRom:Dongle(...) and Main(...)`.

**No keychip table.** `find` over the folder for `*_Table.dat` / `keychip` / `MACtbl` finds nothing — measured
absence, unlike LGI (`SBUH`), LGI3D (`SBWB`), Medaru (`SBRZ`, `SBTF`) and `TNXK0O~C` (`SDDV`). Whether this title
never uses one or the dump omits it is **not determined**.

## 3. Do we already answer it?

**The region: only as far as TeknoParrot's profile field goes.** `OG.xml` declares `DongleRegion` + `PcbRegion`,
which TeknoParrot's Lindbergh emulator supplies. We hold no `patches/OG/dongle.json` descriptor for it and no
`keychip` layer.

**The keychip: no.** We do not answer `amDongleSetupKeychip`, `amDongleGetRegion`, `Faild to get Keychip ID.`,
the `keychip.appboot.*` reads or `keychip.billing.*`. This is the same missing piece as Medaru No Gunman,
LGS, LGI, LGI3D and HOTDSD — one shared `sega-keychip` layer, which `dongles/` does not have a type for.

**The hardware probe: we skip it.** `patches/LOADERS-ZES-GAMES.md` records `OG` next to `LGS` in the
`mxGetHwInfo.exe -s <ini>` category, and our route bypasses the cabinet's scripts. Here the probe is configured
by `mxRingwide.ini` (3461 bytes), not an `mxGetHwInfo.ini`, and it is present — so the demand list is readable
if we ever want to satisfy it instead of skipping it.

## 4. What is missing (and cost)

1. **The shared `sega-keychip` layer** (see Medaru No Gunman §4 and HOTDSD's draft). **Cost: 2–4 days for a
   first shim, amortised across six titles.**
2. **A descriptor for this title** in `patches/OG/`, with its `region`, `pcb`, and keychip parameters once the
   layer exists. **Cost: hours.**
3. **This dump's start path is broken by its operator** — `game.bat`/`gametest.bat` launch `gs2.exe.bak`, which
   does not exist, and there are three hand-made copies of the executable. Before any measurement, decide which
   of `gs2.exe` / `gs2.exe.ori` / `gs2.exe_1080` is the reference. **Cost: minutes — and it must be decided
   before a run, or the run measures the wrong binary.** The NAS is read-only, so this is a view-local choice.
4. **Which region this build is.** `gs2.ini` says `Region=1` (JPN) and `RomType=1`, while TNXK0O~C says `Region=4`
   (EXP). `BoardRegion` and `KeyRegion` are both printed, so the two may differ. **Cost: static.**
5. **The `mxRingwide.ini` demand list** for this title is present and unread in detail — worth extracting, since
   it is the one hardware gate we currently skip for this title. **Cost: an hour.**
6. **The keychip table question** (absent). **Cost: static, a few hours.**

## 5. Proposed test (NOT RUN) + positive control

**One test**, and it begins with a decision rather than a run, because §4.3 makes the choice of binary part of
the experiment.

* Step 0 (prerequisite, not the test): pick one of the three executables and record why — md5, plus which one
  `OpGhost_ResChanger.txt` describes.
* Arm A: start with the descriptor supplying this title's region and pcb, no keychip layer, and capture the
  dongle log lines.
* Arm B: the same, plus the `sega-keychip` shim answering `amDongleIsAvailable` / `amDongleSetupKeychip` /
  `amDongleGetRegion` and the `keychip.appboot.*` reads.

* **Passing result (positive control).** Arm A reaches the attract and the game prints `BoardRegion:<n>` and
  `KeyRegion: <n>` — proving the probe/region path runs and is observable. Arm B additionally removes
  `Init: amDongleIsAvailable() is false`, `Faild to get Keychip ID.`, `Faild to get Region.`,
  `KEYCHIP ACCESS FAILED` and `Error Response KeychipError !!!`, and `KEYCHIP  : %s` then names a keychip id.
* **Failing result (negative control).** Arm A must show `Init: amDongleIsAvailable() is false` (or
  `Init: Faild to init DongleLib.`) and `Faild to get Keychip ID.`; if it does not, the keychip is not on this
  build's critical path at all and item 4.1 drops out of this title's list — a finding, not a failure. Second
  control, for the region specifically: set a region that cannot match (e.g. change `BoardRegion`/`Region`) in a
  **view-local** `gs2.ini` and confirm `WRONG REGION` or `Unknown region %d` appears. If a wrong region changes
  nothing, the region answer is decorative here.

## 6. Evidence log

```
ls -la "OAV6DU~3/"  -> gs2.exe 2658816 ; gs2.exe.ori 2658816 ; gs2.exe_1080 2658816 ;
                       OpGhost_ResChanger.exe 12800 + .txt 185 ; OutputBlaster.dll 1308160 ;
                       mxGetHwInfo.exe 187904 ; mxRingwide.ini 3461 ; gs2.ini 693 ;
                       game.bat 69 ; gametest.bat 79 ; svn_info.txt 0 ; log/ ; root/
md5sum gs2.exe gs2.exe.ori gs2.exe_1080
  -> 54718bfa35392c36b0480f71335778fc / 40f795933abc4f441c98acc778610aa2 / f837cc79d4cf980ae62b71e8214d1e2f
cat game.bat       -> "mxGetHwInfo.exe -s mxRingwide.ini" then "if %ERRORLEVEL% == 0 gs2.exe.bak"
                      -> gs2.exe.bak DOES NOT EXIST in the folder   (same in gametest.bat)
cat gs2.ini        -> [BBS] Region=1 RomType=1 Life=8 CabinetType=0 EnableRecoil=1 EnableLedBoard=0
                      EnableAMLib=1 Language=en SWipeCard=0 + eight Version_* slots
md5sum mxGetHwInfo.exe -> 23e84d9ef75e71b7914285dd6934e30a  187904  (RingEdge-era build: also LGI, LGI3D,
                          Dream Raiders, SEGA Golden Gun)
for p in ZZZ_ABSENT_9F3A amDongle Dongle keychip Keychip Region USA JPN EXP CHN \
         amDongleSetupKeychip amDongleIsAvailable amDongleReadEeprom amDongleReadSram amDongleChipInfo \
         amDongleIsDevelop amDongleGetRegion 'Keychip Not Found' DefaultRegion keychip.appboot keychip.billing; do
    printf '%-24s %s\n' "$p" "$(strings -a -n 4 gs2.exe | grep -c -F "$p")" ; done
  -> 0 / 11 / 12 / 22 / 4 / 5 / 7 / 0 / 59 / 0 / 1 / 1 / 0 / 0 / 0 / 0 / 0 / 0 / 8 / 5
strings -a -n 6 gs2.exe | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> amDongle Ver.1.09 Build:May 12 2011 01:09:02 ; amDongleOpen/OpenEx/SetupKeychip/Update/Busy/
     ResponseCheck/SendAndRecvEx/IsAvailable ; KEYCHIP  : %s ; KEYCHIP ACCESS FAILED ;
     Faild to get Keychip ID. ; Faild to get Region. ; GetRegion ; BoardRegion:%d ; KeyRegion: %d ;
     WRONG REGION ; Unknown region %d ; Init: amDongleIsAvailable() is false ;
     Init: Faild to get Keychip ID. (AMU::Dongle::Info() Faild) ; Error Response KeychipError !!!
find "OAV6DU~3" -iname '*_Table.dat' -o -iname '*keychip*' -o -iname '*MACtbl*'   -> (nothing)
GameProfiles/OG.xml -> EmulatorType TeknoParrot, ExecutableName gs2.exe,
                       FieldName DongleRegion + PcbRegion
patches/LOADERS-ZES-GAMES.md -> "LGS / OG | mxGetHwInfo.exe -s <ini> en alleen bij %ERRORLEVEL% == 0
                                 start het spel"
```

**Not measured, and not claimed:** which of the three `gs2.exe` copies is the reference; whether the keychip is
on the critical path; whether this title ever needs a keychip table; what `mxRingwide.ini` demands.
