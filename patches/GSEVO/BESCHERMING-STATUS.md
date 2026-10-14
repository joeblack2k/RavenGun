# Ghost Squad Evolution (GSEvo) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new.
**Verdict:** a **native Linux ELF** Sega Lindbergh title with the **older `amDongle` (Ver.0.12, 2007)** dongle and
**no keychip layer**. The dongle-validation code is not stripped and carries debug info, so its structure is
readable by symbol name — this makes it the cheapest of the Sega family to instrument. We answer **nothing** for
it yet.

---

## 1. What is present (MEASURED)

`Patches/`, `drv/`, `vsg_l/`. 2416 files, 561 MB.

| File | Size | md5 | Kind |
|---|---|---|---|
| **`vsg_l/vsg`** — the game the profile names | 3116173 | `02ba374e95541ec5237aa109cb804730` | **ELF 32-bit LSB executable, Intel i386, interpreter `/lib/ld-linux.so.2`, for GNU/Linux 2.4.17, dynamically linked, with debug_info, not stripped** |
| `vsg_l/librnalindbergh_jr.so` (= `.so.1.46`) | 35873 | `fbc516c143e4de1d19d7506df7175aeb` | ELF 32-bit shared object (audio only — see below) |
| `vsg_l/snd_util.so` | 691147 | `0c04a761cddc…` | ELF 32-bit shared object |
| `vsg_l/shader/Remix.exe`, `vsg_l/shader/unit/mix.exe` | 286720 each | — | PE32 console (shader tooling, host-side) |
| `drv/openal/0.0.8-1.0.1/lib/libopenal.so.0.0.0` | 250476 | — | ELF 32-bit |
| `drv/openal/0.0.8-1.0.1/lib/libalut.so.0.0.0` | 25512 | — | ELF 32-bit |
| `Patches/GSEvo_OpenAL_1.1_Windows/oalinst.exe` | 809496 | — | PE32 GUI (OpenAL installer for Windows) |

`vsg`'s `DT_NEEDED`: `libstdc++.so.6`, `libglut.so.3`, `libGLU.so.1`, `libGL.so.1`, `libXmu.so.6`, `libXi.so.6`,
`libXext.so.6`, `libX11.so.6`, `libpthread.so.0`, `libz.so.1`, `libncurses.so.5`, `libm.so.6`, `libgcc_s.so.1`,
`libc.so.6`, `libdl.so.2` — a classic Lindbergh/X11 OpenGL title.

Identity strings in `vsg`: `GHOST SQUAD EVOLUTION`, `:GHOST SQUAD`, `GHOST SQUAD`,
`Ghost Squad Evolution/(C) SEGA Corporation, 2007`, `SEGA CORPORATION`, `GAME VERSION`, `SHOW VERSION`.

`librnalindbergh_jr.so` is **an audio library, not the platform layer** — a correction worth recording because
its name invites the opposite assumption. It exports only `ALRNA_*` (32 symbols: `ALRNA_Create`, `ALRNA_Process`,
`ALRNA_SetFrequency`, `ALRNA_Start`, …), its `NEEDED` is only `libc.so.6`, and a calibrated scan finds
`dongle` 0, `region` 0, `keychip` 0. The calibration is honest: `libc.so.6` = 1, `GLIBC` = 6,
`ZZZ_ABSENT_9F3A` = 0, over 476 strings. So the region/dongle answer is **not** in that library.

TeknoParrot profiles: `GSEVO.xml` (emulator `Lindbergh`, `ExecutableName = vsg`, fields `DongleRegion` +
`PcbRegion`) and `GSEVOELF2.xml` (emulator `ElfLdr2`, same executable). The `ElfLdr2` profile is our own-loader
route, and on that route the answer must come from us.

## 2. What it asks for (MEASURED)

Calibrated string scan of `vsg` (`GLIBC` = 151, `ZZZ_ABSENT_9F3A` = 0):

| needle | hits |
|---|---|
| `Dongle` | **78** |
| `amDongle` | **77** |
| `keychip` / `Keychip` / `Keychip Not Found` | **0 / 0 / 0** |
| `Region` / `REGION` (uppercase, standalone) | 0 / 0 |
| `USA` / `EXP` / `CHN` | 5 / 9 / 3 |
| `JPN` | 0 |

**`amDongle Ver.0.12 Build:Feb 14 2007 17:39:06`** — the **older, bit-banged** variant, and **without the keychip
layer**. This is the split that matters for the shared layer: GSEVO (2007), LGJ and LGJS all show `keychip` = 0,
while Medaru, LGI, LGI3D, LGS, Operation G.H.O.S.T. and TNXK0O~C all carry `keychip.appboot.*` and
`keychip.billing.*`. Two generations, not one.

The API, from the strings:

```
amDongleInit            amDongleInit: chip exists.        amDongleInit: enter develop mode.
amDongleIsAvailable     amDongleIsDevelop                 amDongleExit
amDongleReadEeprom      amDongleReadEepromEx              amDongleReadSram
amDongleEncrypt         amDongleDecrypt                   amDongleEncryptEx / amDongleDecryptEx
amDongleChipInfo        amDongleContext                  amDongleDebugLevel
amDongleBit_GetData / _SetData_and_Strobe / _SetBusy / _ResetBusy / _SetStrobe / _ResetStrobe / _Wait0 / _Wait1
```

with per-call error text (`amDongleReadEeprom: recv error.(err=%d)`, `amDongleDecryptEx: set(recv) failed.(err=%d)`,
`amDongleChipInfo: send error.(err=%d)`). Note the `amDongleBit_*` family: this build talks to the dongle
**bit-banged over a port**, which is a very different interception point from HOTDSD's
`CreateFileW` + `DeviceIoControl` pair.

**The check structure is on the symbol table**, because `vsg` is not stripped and has debug info
(`nm` returns thousands of symbols, including `T main`):

| mangled | meaning |
|---|---|
| `_Z18is_dongle_validatev` | `is_dongle_validate()` |
| `_Z20request_dongle_checki` | `request_dongle_check(int)` |
| `_Z21check_dongle_validatev` | `check_dongle_validate()` |
| `_ZZ30check_dongle_validate_internalvE10valid_data` | function-local static `valid_data` inside `check_dongle_validate_internal()` |
| `_ZZ30check_dongle_validate_internalvE12src_data_rel` | function-local static `src_data_rel`, same function |
| `_Z25get_arcade_region_countryv` | `get_arcade_region_country()` |
| `_Z27check_system_region_countryv` | `check_system_region_country()` |

That is unusually good: `check_dongle_validate_internal()` holds the decision and its `valid_data` static is
named, so a disassembly or a debugger breakpoint lands on a named symbol. The region is checked by
`get_arcade_region_country()` / `check_system_region_country()` and the country strings (`USA`, `EXP`, `CHN`)
live inside that path rather than as a standalone `Region` field.

## 3. Do we already answer it?

**No.** There is no `patches/GSEvo/`, no dongle descriptor for this game, and no `amDongle` layer anywhere in the
repo. `dongles/` has `hasp-hl`, `region-pcb` and `sega-scan`; `region-pcb` carries only a region and a
piece-table id, which is what TeknoParrot's Lindbergh emulator supplies through `DongleRegion`/`PcbRegion` —
that is enough for the *region* half, and it is why these titles run under TeknoParrot at all. It is not enough
for `amDongleReadEeprom` / `amDongleReadSram` / `amDongleChipInfo`, and it does not exist on the `ElfLdr2`
route at all.

## 4. What is missing (and cost)

1. **An `amDongle` (0.12-generation) answering layer.** Must supply `amDongleInit`, `amDongleIsAvailable`,
   `amDongleIsDevelop`, `amDongleChipInfo`, `amDongleReadEeprom(+Ex)`, `amDongleReadSram`, and
   `amDongleEncrypt/Decrypt(+Ex)`. **Cost: shares with LGJ/LGJS** (same generation, same code shape), so build
   once. First cut: 1–2 days, plus the EEPROM/SRAM contents.
2. **The interception point.** This is a **native ELF**, so a Windows-style `CreateFileW`/`DeviceIoControl` hook
   (HOTDSD's plan) does not apply. Options, in the project's own order of preference: (a) a `LD_PRELOAD`-style
   shim for the port I/O the `amDongleBit_*` routines use; (b) a hook inside our own ELF loader at the
   `amDongle*` call sites / PLT; (c) a byte-patch on `check_dongle_validate_internal()`. **Cost: this is the
   real design decision, half a day to choose with evidence, because the binary is unstripped and the
   alternative can be evaluated against the actual disassembly.**
3. **The region country value**, from `get_arcade_region_country()`; `USA`/`EXP`/`CHN` all appear. **Cost: cheap,
   static.**
4. **What `check_dongle_validate_internal()` actually validates** — `valid_data` and `src_data_rel` suggest it
   compares two data blocks. Reading it decides whether a fixed answer suffices or whether EEPROM contents are
   needed. **Cost: half a day, bounded, because of the symbols.**
5. **`amDongleInit: enter develop mode.` / `amDongleIsDevelop`** — a developer mode on the dongle path, exactly
   like Medaru's `e_Main_IsDongleDevelop=1`. Worth understanding before building anything: it may be the cheap
   route. **Cost: an hour, static.**

## 5. Proposed test (NOT RUN) + positive control

**One test**, chosen to be falsifiable without running the game to completion: break on the named decision
function and read its named state.

* Attach to `vsg` under our loader (not TeknoParrot), set a breakpoint on
  `check_dongle_validate_internal` — the symbol exists, so use the name, not an address — and read the
  `valid_data` static; separately break on `get_arcade_region_country` and read its return.

* **Passing result (positive control).** With the `amDongle` layer loaded, `check_dongle_validate_internal`
  returns the valid branch, `is_dongle_validate()` returns true, `check_dongle_validate()` does not take the
  failure path, `get_arcade_region_country()` returns a known country string, and the game reaches its attract.
* **Failing result (negative control).** The identical breakpoints with **no** dongle layer must land on the
  `valid_data == 0` path — that is the state today, so it is available for free and it is what makes the passing
  arm meaningful. A second, sharper negative control: temporarily make the layer answer `amDongleIsAvailable()`
  **false**; `check_dongle_validate()` must then follow the failure path. If changing that answer changes
  nothing, the layer is not on the path the game actually takes and the whole approach is wrong — which is the
  most useful thing this test could reveal.
* Because the binary is unstripped, the negative control's breakpoint hit can be verified by name rather than by
  guessed address, which removes the usual source of "the test proved nothing" in this kind of work.

## 6. Evidence log

```
# the executable exists and is a native ELF (so the profile's ExecutableName=vsg resolves)
file "Ghost Squad Evolution/vsg_l/vsg"
  -> ELF 32-bit LSB executable, Intel i386, version 1 (SYSV), dynamically linked,
     interpreter /lib/ld-linux.so.2, for GNU/Linux 2.4.17, with debug_info, not stripped
md5sum + size -> 02ba374e95541ec5237aa109cb804730   3116173
readelf -d vsg | grep NEEDED -> libstdc++.so.6, libglut.so.3, libGLU.so.1, libGL.so.1, libXmu.so.6,
                                libXi.so.6, libXext.so.6, libX11.so.6, libpthread.so.0, libz.so.1,
                                libncurses.so.5, libm.so.6, libgcc_s.so.1, libc.so.6, libdl.so.2
strings -a -n 6 vsg | grep -i -E 'ghost|sega|\(c\)20' -> "GHOST SQUAD EVOLUTION", ":GHOST SQUAD",
     "Ghost Squad Evolution/(C) SEGA Corporation, 2007", "SEGA CORPORATION", "GAME VERSION"

# calibrated in-binary demand list
strings -a -n 4 vsg | grep -c -F GLIBC                -> 151     (positive control)
strings -a -n 4 vsg | grep -c -F ZZZ_ABSENT_9F3A      -> 0       (negative control)
strings -a -n 4 vsg | grep -c -F Dongle               -> 78
strings -a -n 4 vsg | grep -c -F amDongle             -> 77
strings -a -n 4 vsg | grep -c -F keychip              -> 0
strings -a -n 4 vsg | grep -c -F 'Keychip Not Found'  -> 0
strings -a -n 4 vsg | grep -c -F JPN -> 0 ; USA -> 5 ; EXP -> 9 ; CHN -> 3
strings -a -n 6 vsg | grep -E '^_Z.*(dongle|region)' | sort -u
  -> _Z18is_dongle_validatev ; _Z20request_dongle_checki ; _Z21check_dongle_validatev ;
     _ZZ30check_dongle_validate_internalvE10valid_data ;
     _ZZ30check_dongle_validate_internalvE12src_data_rel ;
     _Z25get_arcade_region_countryv ; _Z27check_system_region_countryv
nm vsg | grep -E ' T main$'                          -> present  (the symbol table is real)
strings -a -n 6 vsg | grep -i -E 'amdongle' 
  -> amDongle Ver.0.12 Build:Feb 14 2007 17:39:06 ; amDongleInit: chip exists. ;
     amDongleInit: enter develop mode. ; amDongleIsAvailable ; amDongleIsDevelop ;
     amDongleReadEeprom(+Ex) ; amDongleReadSram ; amDongleEncrypt/Decrypt(+Ex) ; amDongleChipInfo ;
     amDongleBit_{GetData,SetData_and_Strobe,SetBusy,ResetBusy,SetStrobe,ResetStrobe,Wait0,Wait1}

# librnalindbergh_jr.so is audio, not the platform layer
nm -D --defined-only vsg_l/librnalindbergh_jr.so | head
  -> ALRNA_BeginFlush, ALRNA_Create, ALRNA_Destroy, ALRNA_DoWork, ALRNA_Finalize, ALRNA_GetFrequency, ...
readelf -d vsg_l/librnalindbergh_jr.so | grep NEEDED   -> libc.so.6 only
strings -a -n 4 vsg_l/librnalindbergh_jr.so | wc -l    -> 476
strings -a -n 4 ... | grep -c -F libc.so.6 -> 1 ; GLIBC -> 6 ; ZZZ_ABSENT_9F3A -> 0
strings -a -n 4 ... | grep -c -i dongle -> 0 ; region -> 0 ; keychip -> 0

# profile
GameProfiles/GSEVO.xml      -> EmulatorType Lindbergh, ExecutableName vsg, FieldName DongleRegion + PcbRegion
GameProfiles/GSEVOELF2.xml  -> EmulatorType ElfLdr2,   ExecutableName vsg, FieldName DongleRegion + PcbRegion
```

**Not measured, and not claimed:** the region value; what `check_dongle_validate_internal()` compares; whether
`amDongleIsDevelop` short-circuits the check; the EEPROM/SRAM geometry.
