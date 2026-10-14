# Let's Go Jungle (LGJ) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new.
**Verdict:** a **native Linux ELF** Lindbergh title with the **older `amDongle` (no keychip layer)**, an
**arcade backup** layer (`EEPROM` / `BuSRAM` / dongle-EEPROM) that carries a region, and **no `*_Table.dat`** in
the dump. Because it is native, the region/dongle answer must come from **our own ELF loader** — TeknoParrot's
Lindbergh emulator is only a reference here. Its cabinet launcher also demands a **path** (`TEA_DIR`), which is a
different kind of gate. We answer **nothing** for it yet.

---

## 1. What is present (MEASURED)

| File | Size | md5 | Kind |
|---|---|---|---|
| **`lgj_final`** (the game) | 12687190 | `875486650a16abce5b1901fd319001c0` | **ELF 32-bit LSB executable, Intel i386, dynamically linked** |
| `game` | 613 | `3221673b1422fc16b757225a49824092` | **Bourne-Again shell script** — the cabinet's own boot script |
| `lgjrc` | 132 | `e239b9cc1641a0a8064ff57d3df1b935` | shell snippet that runs the game |
| `libCg.so`, `libCgFX.so`, `libCgFXGL.so`, `libCgGL.so` | 2176700 / 2302677 / 1171047 / 251234 | — | ELF 32-bit shared objects (Cg shaders) |
| `libpng.so`, `.so.2`, `.so.2.1.0.8` | 436999 each | — | ELF 32-bit shared objects |
| `libxerces-c.so`, `.so.26`, `.so.26.0` | 4448550 each | — | ELF 32-bit shared objects |

Data: `data/adx`, `data/aix`, `data/csb`; `shader/Cg`, `extraShader/Cg`. **No `*_Table.dat`, no `keychip/`, no
`MACtbl/`** — measured absence (`find` for `*Table.dat`, `*keychip*`, `*MACtbl*` returns nothing).

Profile `LGJ.xml`: `EmulatorType = ElfLdr2` (our loader), fields `DongleRegion` + `PcbRegion`.

**The cabinet's own start script (MEASURED, read in full).** `game` is not a licence gate but it does state
requirements our route has to satisfy:

```sh
export LD_LIBRARY_PATH=/home/disk0/:/usr/local/ssound/final1.5/lib:$LD_LIBRARY_PATH
export GLIBCPP_FORCE_NEW=1
export TEA_DIR=/home/disk0
lgj_path=$(pwd)
if [ -f XF86Config ]; then
	/X11R6/bin/xinit $lgj_path/lgjrc $1 -- /X11R6/bin/X -s 0 -nolisten tcp -xf86config XF86Config
else
	/X11R6/bin/xinit $lgj_path/lgjrc $1 -- /X11R6/bin/X -s 0 -nolisten tcp
fi
```

and `lgjrc` is:

```sh
xsetroot -display :0 -solid gray75 &
xsetroot -display :0 -cursor /X11R6/lib/X11/left_ptr /X11R6/lib/X11/left_ptrmsk
./lgj_final $1
```

Three things follow, and they are cheap to get wrong: the game's data root is **`TEA_DIR=/home/disk0`**, the
sibling libs are found via **`LD_LIBRARY_PATH`**, and the game is started **under its own X server via `xinit`**
with `-nolisten tcp`. This is the `game.bat`/`run_game.sh` pattern of `patches/LOADERS-ZES-GAMES.md`
(*"elke game heeft een eigen startscript dat wij overslaan"*) in its native-Linux form. `$1` is forwarded
straight through to `lgj_final`, so a command-line argument is available for our use.

## 2. What it asks for (MEASURED)

Calibrated string scan of `lgj_final` (`GLIBC` = 188, `ZZZ_ABSENT_9F3A` = 0):

| needle | hits |
|---|---|
| `amDongle` | **81** |
| `Dongle` | **96** |
| `Region` | **26** |
| `EXP` | 6 |
| `keychip` / `Keychip` / `Keychip Not Found` | **0 / 0 / 0** |
| `amDongleGetRegion`, `amDongleSetupKeychip`, `keychip.appboot`, `keychip.billing` | **0 each** |
| `amDongleReadEeprom` / `amDongleReadSram` / `amDongleChipInfo` | 5 / 5 / 3 |
| `amDongleIsAvailable` / `amDongleIsDevelop` | 1 / 1 |

So LGJ is the **2007-era generation, same as Ghost Squad Evolution**: the `amDongle` API with
EEPROM/SRAM/ChipInfo, and **no keychip layer at all**.

The most informative strings are the **unstripped C++ symbols** of the arcade backup layer — this binary is not
stripped, so the whole region/backup mechanism is legible by name:

```
Dongle GameId Error !!!!
_ZN6arcade14_sArcadeBackup18_nBuSRAM_RegionNumE          arcade::_sArcadeBackup::_nBuSRAM_RegionNum
_ZN6arcade14_sArcadeBackup18_nEEPROM_RegionNumE          ...::_nEEPROM_RegionNum
_ZN6arcade14_sArcadeBackup18_nSizeDongleEEPROME          ...::_nSizeDongleEEPROM
_ZN6arcade14_sArcadeBackup20_nOffsetDongleEEPROME        ...::_nOffsetDongleEEPROM
_ZN6arcade14_sArcadeBackup20initUserDongleEEPROMEjPh     ...::initUserDongleEEPROM(unsigned int, unsigned char*)
_ZN6arcade14_sArcadeBackup20setParamDongleEEPROMEjPh     ...::setParamDongleEEPROM(unsigned int, unsigned char*)
_ZN6arcade14_sArcadeBackup21_pWorkRamDongleEEPROME       ...::_pWorkRamDongleEEPROM
_ZN6arcade14_sArcadeBackup22_isUseDongleEEPROM_CRCE      ...::_isUseDongleEEPROM_CRC
_ZN6arcade14_sArcadeBackup22_updateUserDongleEEPROMEv    ...::updateUserDongleEEPROM()
_ZN6arcade14_sArcadeBackup23readRawUserDongleEEPROMEjjPh ...::readRawUserDongleEEPROM(unsigned int, unsigned int, unsigned char*)
_ZN6arcade14_sArcadeBackup25_isUseDongleEEPROM_UniqIdE   ...::_isUseDongleEEPROM_UniqId
_ZN6arcade14_sArcadeBackup26getRegionAlignedSizeBuSRAMEj ...::getRegionAlignedSizeBuSRAM(unsigned int)
_ZN6arcade14_sArcadeBackup26getRegionAlignedSizeEEPROMEj ...::getRegionAlignedSizeEEPROM(unsigned int)
_ZN6arcade14_sArcadeBackup28getRegionAlignedOffsetBuSRAMEj
_ZN6arcade14_sArcadeBackup28getRegionAlignedOffsetEEPROMEj
```

That is the whole answer to "what does it ask for", and it is unusually precise:

* there is a **region number for BuSRAM and a region number for EEPROM** (`_nBuSRAM_RegionNum`,
  `_nEEPROM_RegionNum`);
* the **dongle EEPROM has a size and an offset inside a larger image** (`_nSizeDongleEEPROM`,
  `_nOffsetDongleEEPROM`) — i.e. the dongle's data is *a slice of the backup image*, and the per-region
  **sizes and offsets are computed** (`getRegionAlignedSize/Offset`, both for EEPROM and BuSRAM);
* there are two switchable modes, by **CRC** (`_isUseDongleEEPROM_CRC`) and by **unique id**
  (`_isUseDongleEEPROM_UniqId`), plus `_pWorkRamDongleEEPROM` and `initUserDongleEEPROM` /
  `setParamDongleEEPROM` / `updateUserDongleEEPROM` / `readRawUserDongleEEPROM`;
* `Dongle GameId Error !!!!` is the one dongle error message we can read, and it is about the **game id**.

So this is not a boolean dongle-present check. It is a **region-indexed backup structure**, and a correct answer
means reproducing a layout, not a return value. That is the single most important measured fact in this note,
and it is a different shape of work from the Matrix family.

## 3. Do we already answer it?

**No.** There is no `patches/LGJ/`, no `dongle.json`, and no `amDongle` layer. `dongles/` offers three types —
`hasp-hl`, `region-pcb` (region + piece-table id only), `sega-scan` (a framework) — and none of them can express
`_nEEPROM_RegionNum` + `_nSizeDongleEEPROM` / `_nOffsetDongleEEPROM` + the CRC/UniqueId mode.

On the profile level, `LGJ.xml` names `DongleRegion` and `PcbRegion`, which is what TeknoParrot's Lindbergh
emulator supplies. That emulator is a **reference**, not a dependency (per the project's own main line: native
ELF runs under our loader, not under TeknoParrot's binaries). And for a native ELF there is no
`CreateFileW`/`DeviceIoControl` hook to write — the interception is a loader/PLT or `LD_PRELOAD` matter. So the
honest answer is: **the region machinery the game needs is fully describable from our own binary, and we have
built none of it.**

## 4. What is missing (and cost)

1. **A region/backup descriptor with real geometry.** Not just `region = ...`: the layer needs
   `nEEPROM_RegionNum`, `nBuSRAM_RegionNum`, `nOffsetDongleEEPROM`, `nSizeDongleEEPROM`, and the CRC/UniqueId
   mode flag — all of which are named symbols in `lgj_final`, so they can be read rather than guessed.
   **Cost: half a day to a day of disassembly, because the symbols make it bounded.**
2. **An `amDongle` (2007-generation) answering layer**, sharing with Ghost Squad Evolution: `amDongleInit`,
   `amDongleIsAvailable`, `amDongleIsDevelop`, `amDongleChipInfo`, `amDongleReadEeprom(+Ex)`,
   `amDongleReadSram`. **Cost: shared with GSEvo/LGJS — build once, 1–2 days.**
3. **A `sega-backup` / extended `region-pcb` type in `dongles/`** that can express the geometry. Today
   `region-pcb` has two string fields, which is not enough. **Cost: schema work, hours, and it is the same
   schema HOTDSD's draft and Medaru's keychip need — do it once.**
4. **The launcher environment.** `TEA_DIR=/home/disk0`, `LD_LIBRARY_PATH`, and the `xinit` wrapper. Trivial
   individually, but if the game reads its data relative to `TEA_DIR` then our route must set it or the game
   looks in the wrong place. **Cost: minutes to set, one run to confirm.**
5. **`$1` is available.** `game` forwards `$1` into `lgj_final`, which is a ready-made hook for a diagnostic run.

## 5. Proposed test (NOT RUN) + positive control

**One test, chosen to be falsifiable statically before anything is run** — because the interesting question for a
native ELF is *where* to intercept, and the unstripped symbols let us answer it without guessing.

* Static arm: disassemble/read `arcade::_sArcadeBackup::initUserDongleEEPROM` and
  `getRegionAlignedSizeEEPROM` in `lgj_final`, using the symbol names, and determine (a) which region number the
  build expects and (b) whether the dongle EEPROM slice is read-only or written back. Then attach under our ELF
  loader with a breakpoint on `arcade::_sArcadeBackup::readRawUserDongleEEPROM` and read its arguments.
* **Passing result (positive control).** With `TEA_DIR` set and a descriptor supplying the region geometry, the
  breakpoint on `readRawUserDongleEEPROM` is hit with a plausible `(offset, size, buffer)` triple that matches
  `_nOffsetDongleEEPROM` / `_nSizeDongleEEPROM`, and the game proceeds past its start-up region setup.
* **Failing result (negative control).** Two of them, both cheap:
  1. the identical breakpoint with **no** descriptor must be hit with an offset/size that the game then rejects —
     that is today's state, so it is free;
  2. deliberately answer the region with the **wrong** value (a region that does not match the build) and confirm
     `Dongle GameId Error !!!!` appears or the region setup fails. If changing the region answer changes nothing
     observable, the region is read for bookkeeping only and this whole item is much cheaper than it looks —
     which would be a finding, not a failed test.
* A useful third arm, since it costs nothing: run `game` itself (the bash script) instead of `lgj_final` directly,
  so `TEA_DIR` and `LD_LIBRARY_PATH` are set exactly as the cabinet did. If the game behaves differently, the
  launcher environment was part of the problem and not the dongle.

## 6. Evidence log

```
file   "Let's Go Jungle .../lgj_final"  -> ELF 32-bit LSB executable, Intel i386, version 1 (SYSV), dynamically linked
md5sum + size                            -> 875486650a16abce5b1901fd319001c0   12687190
file   .../game  -> Bourne-Again shell script, ISO-8859 text executable   (613 bytes, md5 3221673b1422fc16b757225a49824092)
cat .../game     -> LD_LIBRARY_PATH=/home/disk0/:/usr/local/ssound/final1.5/lib ; GLIBCPP_FORCE_NEW=1 ;
                    TEA_DIR=/home/disk0 ; xinit $lgj_path/lgjrc $1 -- /X11R6/bin/X -s 0 -nolisten tcp [-xf86config XF86Config]
cat .../lgjrc    -> xsetroot ... ; ./lgj_final $1
for p in GLIBC ZZZ_ABSENT_9F3A amDongle Dongle keychip Keychip Region USA JPN EXP CHN \
         amDongleGetRegion amDongleSetupKeychip amDongleReadEeprom amDongleReadSram amDongleChipInfo \
         amDongleIsAvailable amDongleIsDevelop keychip.appboot keychip.billing; do
    printf '%-24s %s\n' "$p" "$(strings -a -n 4 lgj_final | grep -c -F "$p")" ; done
  -> 188 / 0 / 81 / 96 / 0 / 0 / 26 / 0 / 0 / 6 / 0 / 0 / 0 / 5 / 5 / 3 / 1 / 1 / 0 / 0
strings -a -n 6 lgj_final | grep -i -E 'amdongle|keychip|dongle|region' | sort -u | head -30
  -> "Dongle GameId Error !!!!" and the full arcade::_sArcadeBackup symbol set:
     _nBuSRAM_RegionNum, _nEEPROM_RegionNum, _nSizeDongleEEPROM, _nOffsetDongleEEPROM,
     initUserDongleEEPROM, setParamDongleEEPROM, _pWorkRamDongleEEPROM, _isUseDongleEEPROM_CRC,
     updateUserDongleEEPROM, readRawUserDongleEEPROM, _isUseDongleEEPROM_UniqId,
     getRegionAlignedSize/OffsetBuSRAM, getRegionAlignedSize/OffsetEEPROM
find "Let's Go Jungle ..." -iname '*Table.dat' -o -iname '*keychip*' -o -iname '*MACtbl*'   -> (nothing)
GameProfiles/LGJ.xml  -> EmulatorType ElfLdr2, FieldName DongleRegion + PcbRegion
```

**Not measured, and not claimed:** which region number the build expects; whether the dongle EEPROM slice is
written back; whether the game reads its data relative to `TEA_DIR` on our route; what `Dongle GameId Error !!!!`
is compared against.
