# Let's Go Jungle Special (LGJS) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new.
**Verdict:** the sibling of Let's Go Jungle and measured to be **the same protection shape**: a **native Linux
ELF**, the **older `amDongle` with no keychip layer**, and an `arcade::_sArcadeBackup` region/EEPROM/BuSRAM/dongle
structure. No `*_Table.dat`. One difference from LGJ that matters operationally: the cabinet script
**always** passes an X config file. We answer **nothing** for it yet.

---

## 1. What is present (MEASURED)

| File | Size | md5 | Kind |
|---|---|---|---|
| **`lgjsp_app`** (the game) | 12927939 | `b95a4cb3fe5a0d3d25484e52e16d2b7a` | **ELF 32-bit LSB executable, Intel i386, dynamically linked** |
| `game` | 680 | `2bf24dc486acc04c8b56c78882e70665` | Bourne-Again shell script (cabinet boot script) |
| `lgjsprc` | 132 | `160b246193ee9c0a720c3d7a577c3977` | shell snippet that runs the game |
| `lgjspXF86Config` | 16464 | — | X server configuration (ASCII) |
| `libCg.so`, `libCgFX.so`, `libCgFXGL.so`, `libCgGL.so` | 2176700 / 2302677 / 1171047 / 251234 | — | ELF 32-bit shared objects |
| `libpng.so`, `.so.2`, `.so.2.1.0.8` | 436999 each | — | ELF 32-bit shared objects |
| `libxerces-c.so`, `.so.26`, `.so.26.0` | 4448550 each | — | ELF 32-bit shared objects |

Data: `data/adx`, `data/csb`; `shader/Cg`, `extraShader/Cg`. (LGJ additionally has `data/aix`; LGJS does not.)
**No `*_Table.dat`, no `keychip/`, no `MACtbl/`** — measured absence.

Profiles: **`LGJS.xml`** (`EmulatorType = Lindbergh`) and **`LGJSElf2.xml`** (`EmulatorType = ElfLdr2`), both
declaring `DongleRegion` + `PcbRegion`. The `ElfLdr2` profile is our own-loader route, so there the answer must
come from us.

**The cabinet's start script (MEASURED, read in full).** Identical in structure to LGJ's, with one difference —
the X config is passed in **both** branches:

```sh
export LD_LIBRARY_PATH=/home/disk0/:/usr/local/ssound/final1.5/lib:$LD_LIBRARY_PATH
export GLIBCPP_FORCE_NEW=1
export TEA_DIR=/home/disk0
lgjsp_path=$(pwd)
if [ -f XF86Config ]; then
	/X11R6/bin/xinit $lgjsp_path/lgjsprc $1 -- /X11R6/bin/X -s 0 -nolisten tcp -xf86config $lgjsp_path/lgjspXF86Config
else
	/X11R6/bin/xinit $lgjsp_path/lgjsprc $1 -- /X11R6/bin/X -s 0 -nolisten tcp -xf86config $lgjsp_path/lgjspXF86Config
fi
```

and `lgjsprc` is:

```sh
xsetroot -display :0 -solid gray75 &
xsetroot -display :0 -cursor /X11R6/lib/X11/left_ptr /X11R6/lib/X11/left_ptrmsk
./lgjsp_app $1
```

So: **data root `TEA_DIR=/home/disk0`**, sibling libs via `LD_LIBRARY_PATH`, and the game is started under its
own X server via `xinit` with **`lgjspXF86Config` unconditionally**. `$1` is forwarded straight into `lgjsp_app`.

This is the `patches/LOADERS-ZES-GAMES.md` finding (*"elke game heeft een eigen startscript dat wij overslaan"*)
in its native-Linux form. It is not a licence check, but it is a requirement our route has to satisfy, and it is
cheap to get wrong: a game that cannot find its data looks like a protection failure.

## 2. What it asks for (MEASURED)

Calibrated string scan of `lgjsp_app` (`GLIBC` = 196, `ZZZ_ABSENT_9F3A` = 0). The counts are **identical to
LGJ's** in every category measured, which is the evidence that these two titles share one protection layer:

| needle | `lgjsp_app` | `lgj_final` |
|---|---|---|
| `amDongle` | **81** | 81 |
| `Dongle` | **96** | 96 |
| `Region` | **26** | 26 |
| `EXP` | 6 | 6 |
| `USA` / `JPN` / `CHN` | 0 / 0 / 0 | 0 / 0 / 0 |
| `keychip` / `Keychip` / `Keychip Not Found` | **0 / 0 / 0** | 0 / 0 / 0 |
| `amDongleGetRegion`, `amDongleSetupKeychip` | **0** | 0 |
| `amDongleReadEeprom` / `amDongleReadSram` / `amDongleChipInfo` | 5 / 5 / 3 | 5 / 5 / 3 |
| `amDongleIsAvailable` / `amDongleIsDevelop` | 1 / 1 | 1 / 1 |
| `keychip.appboot` / `keychip.billing` | 0 / 0 | 0 / 0 |

So LGJS is the **2007-generation `amDongle`**, with **no keychip layer** — the same generation as Ghost Squad
Evolution and LGJ, and a different generation from Medaru No Gunman, LGI, LGI3D, LGS, Operation G.H.O.S.T. and
`TNXK0O~C`.

The unstripped C++ symbols give the same legible structure as LGJ:

```
Dongle GameId Error !!!!
arcade::_sArcadeBackup::_nBuSRAM_RegionNum        arcade::_sArcadeBackup::_nEEPROM_RegionNum
arcade::_sArcadeBackup::_nSizeDongleEEPROM        arcade::_sArcadeBackup::_nOffsetDongleEEPROM
arcade::_sArcadeBackup::initUserDongleEEPROM(unsigned int, unsigned char*)
arcade::_sArcadeBackup::setParamDongleEEPROM(unsigned int, unsigned char*)
arcade::_sArcadeBackup::_pWorkRamDongleEEPROM    arcade::_sArcadeBackup::_isUseDongleEEPROM_CRC
arcade::_sArcadeBackup::updateUserDongleEEPROM()  arcade::_sArcadeBackup::_isUseDongleEEPROM_UniqId
arcade::_sArcadeBackup::readRawUserDongleEEPROM(unsigned int, unsigned int, unsigned char*)
arcade::_sArcadeBackup::getRegionAlignedSizeBuSRAM / getRegionAlignedSizeEEPROM
arcade::_sArcadeBackup::getRegionAlignedOffsetBuSRAM / getRegionAlignedOffsetEEPROM
```

As with LGJ, this is **not** a boolean dongle-present check: there is a region number for EEPROM and one for
BuSRAM, the dongle data is a **sized and offset slice of a backup image**, the per-region sizes and offsets are
computed, and there are two modes (by CRC and by unique id) with init/set/update/readRaw entry points. A correct
answer means reproducing a layout, not returning a value.

## 3. Do we already answer it?

**No.** There is no `patches/LGJS/`, no `dongle.json`, and no `amDongle` layer. `dongles/`' three types cannot
express `_nEEPROM_RegionNum` + `_nSizeDongleEEPROM` / `_nOffsetDongleEEPROM` + the CRC/UniqueId mode.

`LGJS.xml` (Lindbergh) and `LGJSElf2.xml` (ElfLdr2) both name `DongleRegion` and `PcbRegion`, which is what
TeknoParrot's emulator supplies. On the `ElfLdr2` route — our loader — that emulator is a reference to compare
against, not a dependency, and for a native ELF there is no Windows device-open to hook: the interception is a
loader/PLT or `LD_PRELOAD` decision.

One measured, useful simplification: because LGJS and LGJ show **bit-for-bit the same demand profile** in every
category above, one layer and one descriptor shape serve both, with only the region value and the executable name
differing. That is the shared-material outcome the owner asked for.

## 4. What is missing (and cost)

1. **A region/backup descriptor with real geometry** — `nEEPROM_RegionNum`, `nBuSRAM_RegionNum`,
   `nOffsetDongleEEPROM`, `nSizeDongleEEPROM`, and the CRC/UniqueId mode. All named symbols, so bounded.
   **Cost: half a day to a day of disassembly — shared with LGJ, not doubled.**
2. **An `amDongle` (2007-generation) answering layer**, shared with GSEvo and LGJ: `amDongleInit`,
   `amDongleIsAvailable`, `amDongleIsDevelop`, `amDongleChipInfo`, `amDongleReadEeprom(+Ex)`,
   `amDongleReadSram`. **Cost: 1–2 days for the shared layer.**
3. **A schema type that can carry the geometry** (`sega-backup`, or an extended `region-pcb`). **Cost: hours, and
   the same schema work HOTDSD's keychip draft and Medaru's descriptor need — do it once.**
4. **The launcher environment**: `TEA_DIR=/home/disk0`, `LD_LIBRARY_PATH`, and `lgjspXF86Config` (which LGJS
   needs unconditionally, unlike LGJ). **Cost: minutes to set.**
5. **Confirm the two titles really share a layer.** The string-count identity is strong evidence but not proof
   that the same region value satisfies both (LGJ and LGJS are different builds of a series, and LGI vs LGI3D
   showed the same family can ship two different region values: `JAPAN` vs `USA`). **Cost: one comparison run.**

## 5. Proposed test (NOT RUN) + positive control

**One test**, deliberately paired with LGJ's so the two titles' answers can be compared — because the open
family question is whether one descriptor covers both.

* Static arm: read `arcade::_sArcadeBackup::getRegionAlignedSizeEEPROM` and `initUserDongleEEPROM` in `lgjsp_app`
  by symbol name; determine the expected region number and whether the dongle slice is written back.
* Dynamic arm: under our ELF loader, break on `arcade::_sArcadeBackup::readRawUserDongleEEPROM` and read its
  `(offset, size, buffer)` arguments.
* Comparison arm: run the same descriptor value against LGJ and against LGJS.

* **Passing result (positive control).** With `TEA_DIR` set and the descriptor in place, the breakpoint is hit
  with an offset/size consistent with `_nOffsetDongleEEPROM` / `_nSizeDongleEEPROM`, the game passes its region
  setup, and — for the comparison arm — the **same** region value satisfies both LGJ and LGJS, so one descriptor
  can be shared.
* **Failing result (negative control).** (1) The same breakpoint with no descriptor must be hit with an
  offset/size the game then rejects; that is today's state and costs nothing. (2) Answer with the wrong region
  and confirm `Dongle GameId Error !!!!` appears or region setup fails. (3) For the comparison arm: if LGJS and
  LGJ need **different** region values, they do **not** share one descriptor, and the string-count identity was
  misleading — that would be a finding, and precisely the kind of thing the test exists to catch. A test whose
  negative arms cannot differ proves nothing.
* Cheap extra arm, since it costs nothing: run `game` (the bash script) rather than `lgjsp_app` directly, so
  `TEA_DIR`, `LD_LIBRARY_PATH` and `lgjspXF86Config` are set exactly as the cabinet did. If behaviour changes,
  the launcher environment was part of the problem and not the dongle.

## 6. Evidence log

```
file   "Let's Go Jungle Special/lgjsp_app" -> ELF 32-bit LSB executable, Intel i386, version 1 (SYSV), dynamically linked
md5sum + size                              -> b95a4cb3fe5a0d3d25484e52e16d2b7a   12927939
file   .../game  -> Bourne-Again shell script, ISO-8859 text executable  (680 bytes, md5 2bf24dc486acc04c8b56c78882e70665)
cat .../game     -> LD_LIBRARY_PATH=/home/disk0/:/usr/local/ssound/final1.5/lib ; GLIBCPP_FORCE_NEW=1 ;
                    TEA_DIR=/home/disk0 ; xinit $lgjsp_path/lgjsprc $1 -- /X11R6/bin/X -s 0 -nolisten tcp
                    -xf86config $lgjsp_path/lgjspXF86Config           (in BOTH branches)
cat .../lgjsprc  -> xsetroot ... ; ./lgjsp_app $1
for p in GLIBC ZZZ_ABSENT_9F3A amDongle Dongle keychip Keychip Region USA JPN EXP CHN \
         amDongleGetRegion amDongleSetupKeychip amDongleReadEeprom amDongleReadSram amDongleChipInfo \
         amDongleIsAvailable amDongleIsDevelop keychip.appboot keychip.billing; do
    printf '%-24s %s\n' "$p" "$(strings -a -n 4 lgjsp_app | grep -c -F "$p")" ; done
  -> 196 / 0 / 81 / 96 / 0 / 0 / 26 / 0 / 0 / 6 / 0 / 0 / 0 / 5 / 5 / 3 / 1 / 1 / 0 / 0
     (identical to lgj_final in every row)
strings -a -n 6 lgjsp_app | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> "Dongle GameId Error !!!!" + the same arcade::_sArcadeBackup symbol set as LGJ:
     _nBuSRAM_RegionNum, _nEEPROM_RegionNum, _nSizeDongleEEPROM, _nOffsetDongleEEPROM,
     initUserDongleEEPROM, setParamDongleEEPROM, _pWorkRamDongleEEPROM, _isUseDongleEEPROM_CRC,
     updateUserDongleEEPROM, readRawUserDongleEEPROM, _isUseDongleEEPROM_UniqId,
     getRegionAlignedSize/OffsetBuSRAM, getRegionAlignedSize/OffsetEEPROM
find "Let's Go Jungle Special" -iname '*Table.dat' -o -iname '*keychip*' -o -iname '*MACtbl*'  -> (nothing)
GameProfiles/LGJS.xml      -> EmulatorType Lindbergh, FieldName DongleRegion + PcbRegion
GameProfiles/LGJSElf2.xml  -> EmulatorType ElfLdr2,   FieldName DongleRegion + PcbRegion
```

**Not measured, and not claimed:** whether LGJS's expected region value equals LGJ's (the comparison arm of §5
exists precisely because it might not); whether the dongle EEPROM slice is written back; whether the game reads
its data relative to `TEA_DIR` on our route.
