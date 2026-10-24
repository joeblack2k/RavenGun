# Let's Go Safari (LGS) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** confirmation, plus one finding that matters more than the region: **the region/keychip
answer is already in place, but this title has a second gate we can now read in full** — the cabinet's
`mxGetHwInfo.exe` hardware probe, whose configuration file is present here and populated.

---

## 1. What is present (MEASURED)

Windows **PE32 i386** (Proton route). Profile `LGS.xml`: `EmulatorType = TeknoParrot`,
`TestMenuIsExecutable = true`, `ExecutableName = GameSampR_RingWide.exe`, fields `DongleRegion` + `PcbRegion`.

| File | Size | md5 |
|---|---|---|
| `GameSampR_RingWide.exe` (the executable) | 2177536 | — |
| `GameTestR_RingWide.exe` | 890368 | — |
| **`mxGetHwInfo.ini`** | **3775** | **`168cf9f96b571f3d85d15ffa6520ce0b`** |
| `mxGetHwInfo.exe` | 202240 | `1c31f44a89d198bcdf4770c6fc8c9a4e` |
| `D3DX9_40.dll` / `D3dx9d_40.dll` | 4379984 / 4499280 | — |
| `GameConfig.ini` | 83 | — |
| `gameid.txt` | 4 | — (content not read in this pass) |
| `firewall.cfg` | 62 | — |
| `game.bat` / `gametest.bat` | 93 each | — |
| `teknoparrot.ini` | 221 | — |

Data trees: `dat/` (`CellSprite`, `Event_dds`, `SOUND`, `STAGE`, `font`, `movie`),
`dat_otherLanguage/` (same shape), and **seven per-language data trees**: `data_english/`, `data_japanese/`,
`data_chinese/`, `data_indonesia/`, `data_spain/`, `data_thailand/`, plus `data_japanese`. Also
`_nncompiledshader/`, `nnstdshader/`, `posteffect/`.

The `mxGetHwInfo.exe` here (md5 `1c31f44a…`, 202240) is the **RingWide-era** probe — the same file as
`TNXK0O~C` and `Transformers Human Alliance`, and *different* from the one in LGI/LGI3D/`OAV6DU~3`
(`23e84d9e…`, 187904).

## 2. What it asks for (MEASURED)

### 2a. The region/keychip demand

Calibrated scan of `GameSampR_RingWide.exe` (`ZZZ_ABSENT_9F3A` = 0):

```
amDongle Ver.1.12 Build:Sep  4 2012 00:17:40
  amDongleInit / amDongleOpen / amDongleOpenEx / amDongleSetupKeychip / amDongleUpdate
  amDongleBillingGetKeychipId / amDongleGetRegion / amDongleGetModelType / amDongleGetPlatformId
  amDongleGetGameId / amDongleGetNetworkAddress / amDongleGetPicVersion / amDongleGetVersion
  amDongleGetDsMac / amDongleGetSsdResponse          <- SSD host proof
  amDongleResponseCheck / amDongleSendAndRecvEx / amDongleBusy
Faild to amDongleExit() ; Faild to get Keychip ID. ; Failed to get Region.
Init: Faild to get DongleInfo. (AMU::Dongle::Info() Faild)
Init: Faild to init DongleLib. (amDongleInit() Faild)
Error Response KeychipError !!! ; Keychip Not Found ; Wrong Region ; KEYCHIP ID: %s
keychip.appboot.dvdflag / .formattype / .gameid / ...
```

This is the **2012 generation** — the same `amDongle Ver.1.12 Build:Sep  4 2012` as `TNXK0O~C`, and richer than
the 2009/2011 keychip builds: it adds `amDongleGetDsMac`, `amDongleGetPicVersion`, `amDongleGetNetworkAddress`
and **`amDongleGetSsdResponse`**. That last one is the same capability HOTDSD's daemon exposes as
`amDongleRequestSsdHostProof` (listed in `patches/HOTDSD/dongle.sega-keychip.draft.json` under
`not_implemented_in_libam`). So the SSD-proof path is a real feature of this generation, not a HOTDSD peculiarity.

No `keychip` table file is present in this folder — unlike LGI/LGI3D (`MACtbl/*_Table.dat`). Whether that means
LGS does not use one or the dump omits it is **not determined**.

### 2b. The hardware probe — fully readable here

`game.bat` runs `mxGetHwInfo.exe -s mxGetHwInfo.ini`, and `patches/LOADERS-ZES-GAMES.md` records the rule:
*"LGS / OG | `mxGetHwInfo.exe -s <ini>` en **alleen bij `%ERRORLEVEL% == 0`** start het spel"*. So this is a
**hard gate in the cabinet's own start script**, and unlike LGI/LGI3D the configuration file is right here —
every demand is written down (all values below are the live, uncommented ones):

```
[OS]      OSVersion = 0045.03.01
[System]  board-type     = RINGWIDE_RICOH | RINGWIDE_ADVANTECH
[Hardware]
          graphic-device = 1002:94cb          ; RINGWIDE
          sound-device   = 10ec:0883          ; RINGWIDE
          mem-size       = 1024   ; MB
          sram-size      = 256    ; KB
          patch-size     = 1004   ; MB
          appdata-size   = 0      ; MB
[resolution] 1280 x 720 ;  vertical = no
```

So the cabinet's gate is: a specific OS version, one of two mainboard types, an AMD `1002:94cb` graphics device,
a Realtek `10ec:0883` audio device, and exact memory/SRAM/patch sizes. The commented alternatives show the
other hardware generations (RINGEDGE: `10de:0623`/`10de:1241` graphics, `11d4:1882`/`10ec:0888` audio,
`mem-size 2048`).

## 3. Do we already answer it?

**The region: yes, `enforced: false`.** `patches/LGS/dongle.json` is a `kind: "region"` descriptor with
`region` = **USA**, `pcb` = **USA** — and here the values come from a profile rather than an assumption:
`DongleRegion=USA`, `PcbRegion=USA` in `LGS.xml`'s config values. The descriptor's own note records the
correction: *"Let op: dit is een correctie op een eerdere versie van het loskoppeldocument, waarin LGS als 'geen
dongle' stond."* `docs/region-layer.md` §3: *"geen blokkade; speelbaar"*.

Measured playable: start to the attract, course selection and riding, with red/blue/yellow/green bound to
A/S/D/F.

**The hardware probe: we skip it, and that is a deliberate, systemic choice.** Per
`patches/LOADERS-ZES-GAMES.md`, the cabinet's own start scripts are bypassed on our route because we start the
executable directly. That is the same finding the owner's loader work already recorded. For LGS it means the
`mxGetHwInfo.exe` gate is not evaluated at all.

**The keychip/`amDongle` half: not answered, and not visible either.** We supply a region and a piece-table id;
we do not supply `amDongleSetupKeychip`, `amDongleGetSsdResponse`, `amDongleGetDsMac` or the `keychip.appboot.*`
values. Since the game is playable with `enforced: false`, none of that is currently on the critical path — but
it is the same layer Medaru No Gunman and HOTDSD need, and LGS is evidence of how much of it exists in the wild.

## 4. What is missing (and cost)

1. **Nothing for reach.** Playable today.
2. **The `mxGetHwInfo` gate is bypassed rather than satisfied.** This is the interesting gap, because the demand
   list is fully known here for the first time. Two open questions: (a) does the probe actually return 0 on our
   route's environment if we ran it, and (b) if we adopted the cabinet's own start script (a `game.bat`-honouring
   route, which `LOADERS-ZES-GAMES.md` argues for on principle), would it pass? **Cost: one run of the probe —
   it is a self-contained console exe and its ini is present. Hours.**
3. **`gameid.txt` (4 bytes) content is unread** in this pass. It may name the title's game id, which is one of
   the `keychip.appboot.gameid` inputs. **Cost: minutes.**
4. **No keychip table present.** Whether LGS needs one is undetermined; the `amDongleGetSsdResponse` capability
   suggests this generation leans on the dongle more than 2009's, so it is worth an explicit answer rather than
   an assumption. **Cost: static, a few hours** (look for a table filename or a read path in the exe and in
   `GameConfig.ini`).

## 5. Proposed test (NOT RUN) + positive control

**One test, and it is the probe rather than the game** — because that is the gate whose demand list is now fully
known and which we currently slip past.

* Run `mxGetHwInfo.exe -s mxGetHwInfo.ini` under the **same Wine prefix** our route uses, in a **view-local**
  copy of the folder, and read `%ERRORLEVEL%`. Then run `game.bat`'s own logic manually:
  *only if exit 0, start the game.*

* **Passing result (positive control).** The probe exits **0**, and the game then starts to the attract exactly
  as it does today. That would prove the probe is satisfiable on our environment and that adopting the cabinet's
  start script for LGS costs nothing.
* **Failing result (negative control).** Edit a **view-local copy** of `mxGetHwInfo.ini` to demand hardware that
  cannot exist — e.g. `graphic-device = dead:beef` — and confirm the probe exits **non-zero** and the game is
  *not* started. That is the control that proves the probe is actually reading the ini and that exit 0 means
  something. Without it, "exit 0" could just be a console app that always succeeds. A second, sharper negative
  control: point `-s` at a nonexistent file; the probe should not exit 0 (the cabinet's own script has an
  `ERROR01` branch commented as *"mxGetHwInfo.ini cannot be read"*).
* For the region half, the same calibration as the other two Let's Go titles: run with `region = CHINA` in a view
  copy of `dongle.json` and confirm a visible region difference while the game still plays. If nothing differs,
  `enforced: false` is true for the wrong reason.

## 6. Evidence log

```
md5sum "Let's Go Safari/mxGetHwInfo.ini"  -> 168cf9f96b571f3d85d15ffa6520ce0b   3775
md5sum "Let's Go Safari/mxGetHwInfo.exe"  -> 1c31f44a89d198bcdf4770c6fc8c9a4e   202240
file   "Let's Go Safari/GameSampR_RingWide.exe" -> PE32 executable for MS Windows 5.00 (GUI), Intel i386
ls -la "Let's Go Safari/"  -> 7 per-language data trees ; gameid.txt 4 bytes ; GameConfig.ini 83;
                              game.bat 93 ; gametest.bat 93 ; firewall.cfg 62 ; teknoparrot.ini 221
grep -v -E '^\s*;|^\s*$' "Let's Go Safari/mxGetHwInfo.ini"
  -> [OS] OSVersion = 0045.03.01
     [System] board-type = RINGWIDE_RICOH | RINGWIDE_ADVANTECH
     [Hardware] graphic-device = 1002:94cb ; sound-device = 10ec:0883 ;
                mem-size = 1024 ; sram-size = 256 ; patch-size = 1004 ; appdata-size = 0
     resolution = 1280 x 720 ; vertical = no
strings -a -n 6 "Let's Go Safari/GameSampR_RingWide.exe" | grep -i -E 'sega|AMLib|\(c\)|ver\.'
  -> ... SEGA AM Library Ver.1.17 Build:Sep  4 2012 ...
strings -a -n 6 ... | grep -i -E 'amdongle|keychip'
  -> amDongle Ver.1.12 Build:Sep  4 2012 00:17:40 ; amDongleSetupKeychip ; amDongleGetRegion ;
     amDongleGetSsdResponse ; amDongleGetDsMac ; amDongleGetPicVersion ; amDongleGetNetworkAddress ;
     amDongleBillingGetKeychipId ; keychip.appboot.dvdflag / .formattype / .gameid ;
     Keychip Not Found ; Wrong Region ; KEYCHIP ID: %s ; Error Response KeychipError !!!
# CALIBRATION for that scan: ZZZ_ABSENT_9F3A -> 0 hits
cat patches/LGS/dongle.json    -> kind region ; DongleRegion USA ; PcbRegion USA ;
   note: "Uit het profiel LGS.xml (DongleRegion=USA, PcbRegion=USA). Let op: dit is een correctie op een
          eerdere versie van het loskoppeldocument, waarin LGS als 'geen dongle' stond."
GameProfiles/LGS.xml   -> EmulatorType TeknoParrot, TestMenuIsExecutable true,
                          ExecutableName GameSampR_RingWide.exe, FieldName DongleRegion + PcbRegion
patches/LOADERS-ZES-GAMES.md   -> "LGS / OG | mxGetHwInfo.exe -s <ini> en alleen bij %ERRORLEVEL% == 0
                                   start het spel"
```

**Not measured, and not claimed:** `gameid.txt`'s content; whether LGS needs a keychip table; whether the probe
passes on our environment; what `amDongleGetSsdResponse` is expected to return.
