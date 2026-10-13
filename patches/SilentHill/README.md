# Silent Hill: The Arcade — what the game is and what it needs

Read `ATTRACT-RUN.md` first for where the title stands. This file is the static description: what the
binary is, what it expects from the cabinet, and where the pieces live. Everything here was read from
the files on 2026-10-13 (nothing was started for it); **[M]** = measured (file content, import table, or
disassembly read from a function start), **[D]** = derived.

**The dump:** `Silent Hill The Arcade/` on the share, 2.5 GB. `KSHG.exe`, 3,022,848 B, PE32 i386 GUI,
sha256 `9b8a6c1bf81f9d690e2bedb94e3ab2844313a83758923c22704da3241e492995`, linker 7.10, built
2007-12-20, PDB path `d:\SilentHill\program\Release\KSHG.pdb`. Konami "PC Based" hardware.

## The executable

* **Not packed, no protector, no dongle API.** [M] Four ordinary sections, a full import table of 15
  DLLs. Imports: `d3d9`, `DSOUND`, `DINPUT8`, `WINMM`, `EaxMan.dll`, `ole32` (DirectShow through COM),
  and Konami's own `shaiolib` (38 functions), `shaerror`, `tabooword`, `kacbookkeeping`, `libbms`.
* **No command-line switches.** [M] `WinMain` (0x4d0430) never reads its command line. It creates the
  mutex `SILENT HILL GS` and exits silently if it already exists; window class `KSHG`, 640×480.
* **Starts fullscreen 640×480.** [D]
* **Changes its working directory** to `<start dir>\Data\<language>` (0x4d8770); the language comes
  from the settings file. The dump has `eng`, `fra`, `ita`; region folders under `Data/locale`:
  `aa ca ea ka ru ua`.

## What it expects from the cabinet

* **Two serial boards, through `shaiolib.dll`.** [M] `COM1` (19200 8N1): the gun board. `COM2`: the
  "button board" — buttons, coin counter and blocker, IC-card readers, 10-key pads, LEDs, and **two
  security chips** (the "security plug"). No USB, no driver calls.
* **A twelve-step boot self-test** (table 0x6cec88, driver 0x4d7bc0): I/O BOARD, GUN I/O, SECURITY,
  SYSTEM DATA1, SYSTEM DATA2, DATA1, DATA2, DATA3, CLOCK, SPEAKER, GUN CALIBRATION, e-AMUSEMENT. Each
  step polls every frame without a timeout; a failure opens an error screen whose text comes from
  `shaerror.dll` (for example `LOWCONTROL` → "5-1500, PC and an I/O board cannot be communicated").
  **An error screen can only be left with TEST** (0x4df190).
* **Security.** [M] `shaiolib_SECURE_check(model)` checks an 8-character model code against the chips.
  The game tries six codes (table 0x6ce730): `GNGHLJAA`, `GNGHLAAA`, `GKGHLEAA`, `GKGHLUAA`,
  `GKGHLKAA`, `GKGHLABA`; the index of the one that matches is the region. [D]
* **Data integrity.** [M] `Data/TestDat/KSHG.crc` lists 5,946 files with size and CRC16. At boot the
  game checks the manifest itself, **`KSHG.exe` on disk** (so the exe cannot be patched on disk), and
  the files under `Data\TestMode\`. The Konami DLLs are in the manifest but are not checked at boot.
* **e-Amusement.** [M] `libbms.dll` talks to a service over the named pipe `\\.\pipe\bmssvc`
  (`GHL/bin/bms_GHL.exe`, not running here). With the e-Amusement setting off (it is, in the dump's
  `setting.1`) the game continues without it and shows a ticker saying so.

## What it writes

| path | what |
|---|---|
| `..\TestDat\setting.1`, `gun.1`, `speaker.1`, `shoprank.lra` (relative to `Data\<lang>`) | settings, gun calibration, speaker setup, ranking; each with a CRC16 |
| `e:/KSHG/KACBK` and `f:/KSHG/KACBK` | mirrored bookkeeping (`kacbookkeeping.dll`) |
| `Data\acdata` | files are deleted and moved there |
| `shaiolib.log` and other `*.log` in the start directory | logs |

Our core redirects drive letters other than C: and Z: to `.\TeknoParrot\<letter>\` under the current
directory, so the bookkeeping lands in `Data\<lang>\TeknoParrot\E|F\KSHG\`. The view therefore has
`Data/TestDat`, `Data/acdata` and the language folders as LOCAL folders
(`bewijs-attract-20261013/harness/sh-view.sh`); nothing is written to the share.

## Movies

`Data/Movie/*.vid` and `Data/<lang>/Movie/*.vid` (32 files, 507 MB) are **bare MPEG-2 video streams**:
no container, no audio, 640×480, 30 fps. [M] The game plays them through DirectShow
(`IGraphBuilder::RenderFile`, 0x401d70) into its own renderer and brings no splitter or decoder of its
own. The dump ships `clvsd.ax` (CyberLink's PowerDVD video decoder) and a batch file that registers it
on Windows; under Wine it is not used — Wine's own GStreamer-based filters decode the streams once a
32-bit GStreamer is available.

## The I/O library, and our replacement

`shaiolib.dll` exports 52 functions, all cdecl, returning `int` with **-1 = success/true and
0 = failure/false**. `KSHG.exe` imports 38 of them. The groups: lifecycle (`initialize`, `start_boot`,
`update`, `is_boot_*`, `is_error`, `get_lasterror`), guns (`GUN_status(int[8])` — two guns ×
{x, y, trigger, offscreen}), buttons (`BUTTON_status(uint8_t[9])`, per byte bit 7 = held, bit 0 = edge;
bytes 0..8 → Test, Service, ?, Option R P1, Option R P2, Option L P1, Start P1, Option L P2, Start P2),
coins (`COIN_count`, `COIN_consume`), 10-key, LEDs, IC card, security, and system ids.

Our replacement is `dropin/shaiolib.dll`, source `shaiolib_rg.c`, built by `build-shaiolib.sh` (the
rebuild is byte-identical). It says the boards are there, answers the security check for one model
code (`RG_SHAIO_MODEL`, default `GNGHLAAA`), returns synthetic system ids (`0140` + `5247…`), and
delivers **no inputs yet** — the place where they will come from is marked `rg_poll_inputs()`. It
writes `rg-shaiolib.log` next to itself and applies `rg-shaiolib.patches` at load.

## For comparison: TeknoParrot

Profile `GameProfiles/SilentHill.xml`: closed 32-bit core, emulation profile `SilentHill`, gun game,
axes 0–255. Its input record has the buttons as a bit mask at offset 8 (Test 0x01, Service 0x02,
P1 Start 0x04, P1 Trigger 0x08, P1 Option L 0x10, P1 Option R 0x20, Coin 0x40, P2 Start 0x80, …) and
four analog bytes at 12/16/20/24. It offers an "EnableAmdFix — fixes invisible enemies bug on AMD
CPUs"; what that patches is not known, and `KSHG.exe` does contain a check for `AuthenticAMD`
(0x5a0be9). The open OpenParrot source has nothing for this title.

## Not determined

The serial wire protocol of the boards and the security-chip arithmetic (not needed with the
replacement library); the layout of `setting.1` beyond the e-Amusement byte (0x3c), the language (0x2c)
and the clock flag (0x5e); the raw gun coordinate range with calibration on; what the AMD fix does; why
the game creates the COM class `{C6606CA5-2108-4CAF-8E52-1953F2DBF716}` (Wine reports it as not
registered, and the game carries on).
