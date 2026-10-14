# GEWEER-CALIBRATIE — Raccoon Rampage: what the gun plugin wants, and what makes the banner go away

**Date:** 2026-10-14, preparation only — **no run was made** (the bench is busy). Everything below is
read from files on the share (read-only) and from the loader/patches trees on the deck.
**Game:** `RSGame/Binaries/Win64/RSGame-Win64-Shipping.exe`, 66,510,336 B, PE32+ x86-64, ImageBase
`0x140000000`, UE 4.26.0.0, sha256 `118b8335…153a`.
**Plugin:** `RSGame/Plugins/UsbPluginsDll.dll`, 188,416 B, PE32+ x86-64, ImageBase `0x180000000`,
sha256 `b130f882493fcef5…f65c1`; the vendor original beside it, sha256 `b5a756f58831e2b0…7a973`.
**Labels:** **MEASURED** = a command and its raw output (all raw output in `/tmp/rr-geweer/`);
**INFERRED** = a conclusion drawn from measurements. Nothing here rests on "this game is different"
as an assumption — §5 states each difference with the measurement that shows it.

Section addresses are `VA = ImageBase + RVA`. For the exe, `.text` is file `0x600` → `0x140001000`
and `.rdata` is file `0x2BF5400` → `0x142BF6000`; for the DLL, `.text` is file `0x400` →
`0x180001000`.

---

## 1. What `UsbPluginsDll.dll` actually is and wants

### 1.1 The export reader, calibrated first

`27-exports.py` parses the PE export directory by hand (stdlib only). It is calibrated against a
**known answer**: the 107-name list already in
`bewijs-attract-20261014/usbplugins-exports.txt`.

```
# machine 0x8664 sections 8 magic 0x20b ImageBase 0x180000000
# export dir RVA 0x286c0 size 3352
# NumberOfFunctions 107  NumberOfNames 107
# parsed 107 named exports
# exports NOT inside .text (forwarders?): 0
########## CALIBRATION
  known list length            : 107
  parsed named exports         : 107
  known names MISSING from PE  : []
  parsed names NOT in the list : []
  all parsed exports inside .text: True
```

So the reader finds exactly the exports that are known to be there, no more and no fewer — an empty
result from it would be trustworthy. A second, independent reader (`13-dll.sh`, `objdump -p`) was
cross-checked against the same list: `comm -23` (known but not found) is **empty**. Two independent
readers agree.

The full table (`name`, ordinal, RVA, VA, size to the next export, section) is
`/tmp/rr-geweer/usb-exports-final.tsv`. Table addresses: Export Address Table `0x286E8`, Name Pointer
Table `0x28894`, Ordinal Table `0x28A40`. **All 107 live in `.text`** (RVA `0x1000`..`0x20B28`); there
are no forwarders and no data exports. The build's own `.pdb` path is in the file — `objdump -x`
(`13.out`) prints `I:\Project\UE4.26_RaccoonRampage\UsbPluginsDll\x64\Release\UsbPluginsDll.pdb)` from
the debug directory. (My own `27-exports.py` regex for it was written with doubled backslashes and
reported "not found inline"; that line is a bug in my reader, not evidence of absence.)

### 1.2 Which exports the game actually resolves

**MEASURED**, and this is the finding that reshapes §4.2 of `ATTRACT-RUN.md`. The exe names the plugin
functions it wants; `18-pool2.py` resolves every RIP-relative `.rdata` string reference in `.text` to
its string, and `15-bindmap.py` recovers the `GetProcAddress` site and the global that caches the
result. The exe contains **44 distinct ANSI `BIND_*` names** — those are the `GetProcAddress`
arguments — and, in parallel, **38 distinct UTF-16LE `BIND_*` names** which sit next to
`UUsbBPLibrary::…` log/format strings and are log text, not lookup keys. The two sets overlap
heavily. The loader is one function:

* `0x1409DC3D9` `lea … 0x142CD8228` = `"DewoDll.dll"` → `LoadLibraryW` → module handle `0x143C19210`
* `0x1409DC455` `lea … 0x142CD8240` = `"UsbPluginsDll.dll"` → `LoadLibraryW` → handle `0x143C19208`
* `0x1409DC4F6` `"BIND_InitDLLPath"` (len 17) → `GetProcAddress` → `0x143C19090`, then **called** with
  the game directory

`GetProcAddress` is the import thunk at IAT `0x140BF9B00`; the name is built as an ANSI string with a
length in `edx`/`r9d` by `0x1406DD0E0` before the call. The wrappers that follow each cache their
pointer in a global (`21-probe.py`, `15-bindmap.py`):

| export | bound at | cached in | call shape |
|---|---|---|---|
| `BIND_InitDLLPath` | `0x1409DC4F6` | `0x143C19090` | `(const TCHAR* dir)`; return value used as the init result |
| `BIND_InitDongle` | `0x1409DC605` | `0x143C19070` | `()` → bool |
| `BIND_IsDongleExist` | `0x1409DC995` | `0x143C19078` | `()` → bool |
| `BIND_IsDongleVerify` | `0x1409DCA9E` | `0x143C19088` | `()` → bool |
| `BIND_UnInitDongle` | `0x1409DE7BA` | `0x143C19080` | `()` |
| `BIND_InitUnisIo` | `0x1409DC7D5` | `0x143C19098` | `()` → bool |
| `BIND_IsIoWorking` | `0x1409DC8B5` | `0x143C190A0` | `()` → bool |
| `BIND_GetIoData` | `0x1409DC6ED` | `0x143C190A8` | `(rcx)` → bool |
| `BIND_SetState`, `BIND_SetGameState`, `BIND_SetEnableTicketHalve`, `BIND_SetGunLight_*`, `BIND_SetPedalLight_*`, `BIND_SetConsoleLight_*` | `0x1409DE455` … `0x1409DD92E` | (own cell each) | `(…)` → void |
| `BIND_AddKeyboardCoins` / `BIND_GetCoinNumByPlayer` / `BIND_DecCoins` / `BIND_ResetMachine` | `0x1409DC0F3` / `0x1409DC2EC` / `0x1409DC1C3` / `0x1409DCB90` | — | coin bookkeeping |
| `BIND_ReadInI_WChar`, `BIND_WriteInI_WChar`, `BIND_TcharToChar`, `BIND_CharToTchar`, `BIND_OpenDB`, `BIND_CreateDBTable`, `BIND_GetItemMax`, `BIND_InsertData`, `BIND_ReadData_ALL`, `BIND_DeleteData` | `0x1409A984F`, `0x1409A9E04`, `0x1409A9D05`, `0x1409A9661`, `0x1409B7A7F`, `0x1409A7F10`, `0x1409A8983`, `0x1409A936B`, `0x1409C28DF`, `0x1409A8440` | — | `IniFile.dll` / `SQLite.dll` wrappers (`13.out`: `IniFile.dll` at `0x142CD80C8`, `SQLite.dll` at `0x142CD8140`) |

**The gun-device family is never bound.** `08-exestr.py` searched the whole 66 MB exe for the exact
names: `BIND_OpenGunDevice` **0 occurrences**, `BIND_GetGunData` **0 occurrences**
(ASCII and UTF-16LE). Also absent by name: `BIND_GetESIoData`, `BIND_OpenESIODevice`,
`BIND_CloseESIODevice`, `BIND_IsExistESIODevice`, `BIND_OpenZlbDevice`, `BIND_IsExistZlb`,
`BIND_GetPlayerData`, `BIND_SetGunShakeVal`, `BIND_SetGunShock`, `BIND_GetDeviceNumber` is present,
`BIND_EnterCalibrate` is present, `BIND_GunSwap` is present, but the **HID gun path**
(`HID\Vid_0471&Pid_485e`) is not reached by any name the exe builds. The wrapper that logs its calls
is `UUsbBPLibrary` — the exe contains `UUsbBPLibrary::Usb_SetCoinMeter %dP`,
`UUsbBPLibrary::Usb_SetTicketMeter %dP`, `UUsbBPLibrary::Usb_SetNewGunLight`, `…Usb_SetCardLight`,
`…Usb_SetICO_1`, `…Usb_SetICO_2`, `…Usb_SetWingMotor`, `…Usb_SetCardMeter_12P`, `…Usb_SetCardMeter_34P`,
`…Usb_SetDrainage` (all UTF-16, next to the matching `BIND_*` name).

**What the plugin is for, then (INFERRED from the name, the call site and the arguments):** it is the
cabinet's single IO/accounting facade. For this title the *live* part of it is the **UNIS IO board**
(`BIND_InitUnisIo` / `BIND_IsIoWorking` / `BIND_GetIoData`) plus coin/credit bookkeeping and
lamp/motor setters; the HID gun classes (`CUnisIo`, `CAmusementParkIO`, `CZlbDev`, `CCoDongle2`) and
the serial ESIO board are present in the DLL but **the exe never asks for those entry points by name** —
so they are dead weight for this title, not the missing input path. This is the opposite of what
`ATTRACT-RUN.md §4.2` assumed when it listed `BIND_OpenGunDevice`/`BIND_GetGunData` as "something has
to answer".

### 1.3 The DLL as shipped is already a patched build

**MEASURED** (`03-settings.sh`, `cmp -l`): 8 bytes differ from `UsbPluginsDll.dll.original`, at three
places, and `25-sites2.sh` reads each in the aligned disassembly.

| file offset | VA | shipped | original | what it does |
|---|---|---|---|---|
| `0xCC80` | `0x18000D880` | `e9 63 02 00` | `ba 00 c2 01` (`mov edx,0x1C200`) | **replaces the COM/ESIO serial-port setup** (115200 baud; the skipped code at `0x18000D885` calls `0x18001E4BE`/`0x18001E4DC` with a 1024-byte buffer and a 3000 ms timeout — a board read) with a direct `jmp 0x18000DAE8` |
| `0x14DEA` | `0x1800159EA` | `eb 14` | `74 1c` (`je`) | inside the predicate `0x1800159B0` (checks `cmpl $0,0x18002E7BC` and two handles against `-1`); the `je` becomes unconditional, and the target `0x180015A00` is `mov $1,%al; ret` → **this predicate now returns true always** |
| `0x15127` | `0x180015D27` | `eb 1d` | `74 20` (`je`) | inside `0x180015D20` (`cmpb $0,0x18002CDF2`); `0x180015D46` is `mov $1,%al; ret`, `0x180015D49` is `xor %al,%al; ret` → **this predicate now returns true always** |

**INFERRED:** someone (TeknoParrot's per-game patching, which keeps the `.original` beside it — the
folder's `teknoparrot.ini` carries `[Error] Unsupported Executable=… NEW CRC: 71E52EC0!`, so this
profile is in use) has already made this DLL answer "the board is there" without a serial board. The
two `always true` patches are the classic shape of "lie about the hardware". **The DLL in the view is
therefore not the blocker for the banner** — and it is on the read-only share, so we cannot change it
there anyway (nor may we: §8 of `ATTRACT-RUN.md` records that we never write to the share).

---

## 2. How the game decides "not calibrated"

### 2.1 Where the text is — and is not

**MEASURED** (`05-strings.py`, `10-pakgrep.sh`): the banner text is **not** in the exe and **not**
findable in the pak.

```
--- 'not calibrated': ascii=0 utf16le=0
--- 'calibrat': ascii=0 utf16le=0
--- 'Calibrat': ascii=26 utf16le=13        (all of them property/function names, see below)
--- 'Setting Menu': ascii=0 utf16le=0
```
and in `pakchunk0-WindowsNoEditor.pak` (1,298,542,974 B):
```
########## ASCII 'calibrated' (case-insensitive)   -> (no output)
########## ASCII 'not calibrated'                  -> (no output)
########## ASCII 'guns are not'                    -> (no output)
########## ASCII 'Setting Menu'                    -> (no output)
```
The pak grep is calibrated by the fact that it *does* find `0x197`/`0x2d5`-shaped header data and that
the same `grep` finds strings in the exe; its empty result here means the pak's entries are compressed
(a UE4 pak stores entries zlib-compressed), so an empty grep is **not** evidence of absence. What it
does establish is that the text is not in plaintext form in either file. **INFERRED:** the banner is an
FText/UMG string inside a compressed `.uasset`, and the decision is Blueprint-side. That is consistent
with §2.3, where the C++ side exposes exactly one predicate for Blueprint to call.

### 2.2 The predicate, in full

The exe's UE4 reflection table gives the mapping directly. `16-dump.py` read the
`FNameNativePtrPair` array — `{const char* NameUTF8; void* FunctionPtr}` at stride 0x10:

```
0x142d02ea8 = 0x142d013e0  .rdata ANSI 'ClearGunCalibrateData'
0x142d02eb0 = 0x140a6f790  .text
0x142d02eb8 = 0x142d013f8  .rdata ANSI 'IsGun'
0x142d02ec0 = 0x140a70130  .text
0x142d02ec8 = 0x142d01400  .rdata ANSI 'IsGunCalibrate'
0x142d02ed0 = 0x140a70200  .text
```
(a second copy of the same trio, with different thunks, at `0x142D01750`: `0x140A6E2B0`,
`0x140A6E310`, `0x1425135F0` — the same functions registered on another class).

So the game has three UFUNCTIONs. Each exec thunk unpacks its parameters and tail-calls the C++
implementation (`19-dis4.sh`, `dis-thunks.txt`):

| UFUNCTION | exec thunk | C++ implementation |
|---|---|---|
| `ClearGunCalibrateData(PlayerId)` | `0x140A6F790` | **`0x1409ACFC0`** |
| `IsGun` | `0x140A70130` | **`0x1409B70A0`** |
| `IsGunCalibrate(PlayerId)` | `0x140A70200` | **`0x1409B7180`** |

**`IsGunCalibrate` at `0x1409B7180` is the comparison** (`dis-isgun.txt`). It returns `true` only when
**all** of the following hold:

```
1409b7196: mov %rcx,%rdx ; mov $1,%r8d
1409b719f: mov 0x143dfb6b8,%rcx          ; the engine/world context
1409b71a6: call 0x142371e00             ; -> World
1409b71b4: mov 0x180(%rax),%rbx         ; rbx = World->0x180   (the GameState)
1409b71c4: call 0x140a60c30             ; current context
1409b71d5: cmp 0x38(%rdx),%eax ; jg fail
1409b71e5: cmp %r8,(%rax,%rcx,8) ; jne fail     ; UE4 "is of class" test
1409b71ef: mov 0x218(%rbx),%r8 ; test %r8,%r8 ; je fail   ; r8 = GameState->0x218 (the gun/IO manager)
1409b71ff: test %edi,%edi ; js fail                       ; edi = PlayerId
1409b7207: cmp 0x2d8(%r8),%edi ; jge fail                 ; PlayerId < manager->0x2d8
1409b7210: cmp 0x340(%rbx),%edi ; jge fail                 ; PlayerId < GameState->0x340
1409b7218: mov 0x338(%rbx),%rax
1409b7222: imul $0x5c,%rdi,%rcx                           ; per-player record, stride 0x5C = 92 bytes
1409b7226: cmpb $0x0,0x59(%rcx,%rax,1) ; je fail          ; <-- (5) the per-player byte at +0x59
1409b722d: mov 0x2d0(%r8),%rax
1409b7237: movslq 0x8(%rax,%rdx,8),%rcx ; test %ecx,%ecx ; je fail   ; <-- (6) table index != 0
1409b7240: mov (%rax,%rdx,8),%rdx                          ; table base
1409b7244: movss 0x142c1e120,%xmm2      ; = 0x7FFFFFFF (abs mask, andps)
1409b724c: movss 0x142cdc13c,%xmm3      ; = 200.0      (the threshold, comiss)
1409b7254: imul $0x70,%rcx,%rcx                            ; 0x70-byte table records
1409b7258: movss -0x50(%rcx,%rdx,1),%xmm0 ; subss 0x10(%rdx),%xmm0
1409b7263: movss -0x4c(%rcx,%rdx,1),%xmm1 ; subss 0x14(%rdx),%xmm1
1409b726e: andps %xmm2,%xmm0 ; comiss %xmm3,%xmm0 ; jb fail   ; <-- (7) |dX| >= 200.0
1409b7276: andps %xmm2,%xmm1 ; comiss %xmm3,%xmm1 ; jb fail   ; <-- (7) |dY| >= 200.0
1409b727e: mov $1,%al ; ret
1409b7290: xor %al,%al ; ret
```

The two constants were read out of `.rdata` (`21-probe.py`):

```
0x142c1e120 .rdata xmm2 mask      : bytes ffffff7f  as u32 0x7fffffff   (abs mask)
0x142cdc13c .rdata xmm3 threshold : bytes 00004843  float 200.0         (u32 0x43480000)
```

**So, exactly what has to become true for the banner to disappear:**

1. line **5** — the byte at `[*(GameState+0x338) + PlayerId*0x5C + 0x59]` must be **non-zero**;
2. line **6** — the 32-bit table index at `[*(manager+0x2D0) + PlayerId*16 + 8]` must be **non-zero**;
3. line **7** — the calibration span for that player must be **≥ 200.0** in both X and Y. The span is
   computed as `*(float*)(base + idx*0x70 − 0x50) − *(float*)(base + 0x10)` and
   `*(float*)(base + idx*0x70 − 0x4C) − *(float*)(base + 0x14)`, where `base` is the per-player table
   pointer and `idx` the non-zero index from line 6. With `idx = 1` (the common case) that is
   `table[0].+0x20 − table[0].+0x10` in X and `table[0].+0x24 − table[0].+0x14` in Y.

(In the calibration builder — `dis-calibA.txt`, the function at `0x1409BBF00`…`0x1409BC400` — the
fields it writes are `+0x00,+0x04,+0x08,+0x0C,+0x10,+0x14,+0x18,+0x1C,+0x20,+0x24,+0x28,+0x2C` of each
0x70-byte record, and `+0x18 = +0x10 + spanX`, `+0x24 = +0x14 + spanY`; that is why the comparison is
between `+0x20` and `+0x10`. **INFERRED** for the field naming; **MEASURED** for the arithmetic.)

### 2.3 Who sets that byte, and the fact that only one byte matters

`21-probe.py` scanned the whole 46 MB `.text` for every instruction that touches displacement `0x59`
with a record-shaped addressing mode. Exactly **three** sites exist in the per-player machinery:

```
cmpb $0,0x59(base,index,1)  [80 7c xx 59 00]: 2 occurrences
   VA 0x1409b7226  bytes 807c015900      <- the READ inside IsGunCalibrate
   VA 0x1419f3fc9  bytes 807c245900      <- unrelated (outside this machinery)
movb $imm,0x59(base,index,1) [c6 44 xx 59 imm]: 39 occurrences in the whole .text, of which exactly
   VA 0x1409bc3c7  bytes c644115900      <- SETS +0x59 = 0
   VA 0x1409c46f8  bytes c644025901      <- SETS +0x59 = 1
are in this per-player machinery (the other 37 are unrelated structures that happen to use disp 0x59)
(and imul $0x5c appears only 9 times in the whole .text; four of them are this machinery:
 0x1409b7130, 0x1409b7222, 0x1409bc35a, 0x1409c3ba7/…/0x1409c3ddb)
```

* **The writer of `1` is `0x1409C46F8`** — `movb $0x1,0x59(%rdx,%rax,1)`, the disassembler's own
  reading of `c6 44 02 59 01` — inside the function containing `0x1409C4391`. That function
  fills the 0x70-byte table (it calls `0x1409AA950` at `0x1409C4652`, which is the same 16-point
  builder the save side uses, and copies 0x40 bytes per record with `add $0x70,%rbp`), **then** sets
  `movb $0x1,0x59(…)`, then calls `0x1409C4AC0(manager, 1, 0)` and `0x1409D4D20`. This is the
  **load-and-apply** side.
* **The writer of `0` is `0x1409BC3C7`** — `movb $0x0,0x59(%rcx,%rdx,1)` (`c6 44 11 59 00`) — inside
  the function containing `0x1409BC2F6` — the same
  function that at `0x1409BC34A` loads the literal `GunCalibrate_bIsHandleVerify_%dP` (`0x142CDAED0`)
  and at `0x1409BC403` the literal `Calibrate_%dP_%d_LT_X` (`0x142CDAF18`). This is the
  **save/clear** side.
* **Neither writer has a direct `call`.** `IsGunCalibrate` itself has exactly **one** caller: its own
  exec thunk `0x140A70200`. **MEASURED** (`20-callers.py`), and it is the important structural fact:
  **the C++ side only exposes the predicate; the banner's condition is evaluated in Blueprint.** The
  two writers are therefore reached indirectly (UFunction/tick/delegate); the enclosing-function starts
  my `int3`-boundary heuristic returned for them (`0x1409C4391`, `0x1409BC2F6`) are themselves inside
  the real functions, so the "0 callers" figure is **INFERRED**, not measured, for those two.

### 2.4 The state on disk says player 1 is verified — and it does not matter

**MEASURED** (`03-settings.sh`), `RSGame/Binaries/Win64/Setting_Hardware.ini`, mtime 2026-10-12 01:47,
3656 B, `[/Script/RSGame.SettingData]`:

```
GunCalibrate_bIsHandleVerify_1P=1
GunCalibrate_bIsHandleVerify_2P=0
GunCalibrate_bIsHandleVerify_3P=0
GunCalibrate_bIsHandleVerify_4P=0
; and 128 Calibrate_1P_<n>_<corner>_<axis> keys, n = 0..15 (16 aiming points),
; corner in {LT,RT,LB,RB}, axis in {X,Y} -- 8 keys per point. The full file is in 03.out.
Calibrate_1P_0_LT_X=4112    Calibrate_1P_0_LT_Y=7196    Calibrate_1P_0_LB_Y=20046   ...
Calibrate_1P_15_LB_X=47031  Calibrate_1P_15_RB_X=61423 Calibrate_1P_15_RT_Y=45489  ...
```

The points span 4112…61423 in X and 6939…58339 in Y — differences far above the 200.0 threshold, so
condition 3 is satisfied by the stored data. `_1P=1` satisfies the *persisted* form of condition 1.

**And yet the banner is on every frame of rr9 and rr10**, which loaded those very files (the HUD showed
the saved coin counts, `ATTRACT-RUN.md §4.3`), while the closed core with the same files shows no banner
(or1). **MEASURED by those runs.** Therefore: **the banner is decided by runtime state, not by the
persisted ini value.** Either the apply path (`0x1409C4391`) did not run to completion on our core, or
the clear path (`0x1409BC2F6`) ran after it. Which of the two is **not determined** — see "Could not
determine" 2.

---

## 3. The cheapest honest way to satisfy it — ranked

Context that decides the ranking: this title's own `game.ini` row is
`Type = TeknoParrot`, `EmulationProfile = WartranTroopers`, `Route = tp-shm`, and `INVOER-ROUTE.md`
classifies it as "**een kern**" — the input path contains a core. `WartranTroopers` is shared by **11
of the 15** games that still need an own profile. And the loader already has two working channels for
exactly this situation:

* **a replacement game DLL** — Silent Hill's `shaiolib_rg.c` (669 lines, `patches/SilentHill/`): exports
  the same 52 names at the same ordinals via an inline `.def`, built `-nostdlib -ffreestanding` with the
  project's mingw-w64 sysroot (`build-shaiolib.sh`), states that the boards are present, answers the
  security check, writes `rg-shaiolib.log` next to itself as load proof, and has a placeholder
  `rg_poll_inputs()` where the input bridge belongs.
* **a PE inline-hook DLL** — `rg-pe-hook.dll` (source `tools/rt-hasphook/rg_pe_hook*.c` in the loader
  repo), dropped in by `patches/LuigisMansion/dropin/dropin.ini`, address list in `dongle-haken.ini`,
  `Preload = true`, `Audit = false`, `Env = RG_HOOK_INI=…;RG_HOOK_LOG=…`. On a Windows game under Wine
  via `OpenParrotLoader64.exe` the launcher puts the path in **`TP_PRELOAD_DLL`** (one DLL, one Wine
  path) and the loader loads it **into the game process after the core** (`docs/dropins.md`). It writes
  `rg-pe-hook.log` in the game's working directory (the view) as evidence, with a per-address verdict
  and a summary `totaal aangehaakt: N van M; afgekeurd: M; waarvan antwoord TODO: K`; and it **refuses**
  to hook an address whose prologue already starts with `e9` ("already replaced by another core?").

### (A) Replacement `UsbPluginsDll.dll` in the view — the actual fix

Recognised precedent, exact interface, and the game already loads this DLL by a path built from its own
directory (**MEASURED**: `0x1409DC455` builds the path at runtime with a `0x3F` separator and calls
`LoadLibraryW` — it is not a bare module name, so a file in the view's `RSGame/Plugins/` is what gets
loaded, and **no `WINEDLLOVERRIDES` is needed**). We have the full 107-name/ordinal table with
addresses and sizes, so a `.def` can be generated mechanically; the Silent Hill build script is the
template.

* **What it costs:** one DLL (107 stubs), a `dropin/dropin.ini` with `To = RSGame/Plugins`, and the
  answers taken from the daemon's 64-byte board state (`/dev/shm/ravengun_JvsState`, which the launcher
  already mirrors; `ATTRACT-RUN.md §4.1` measured byte 8 = `04` for a coin and `01` for TEST).
* **What could go wrong:** (1) all 107 entry points must exist — a missing one makes `GetProcAddress`
  return NULL where today it returns the vendor's function, and the wrapper's `test %rax,%rax; je`
  paths silently answer "no device"; (2) the game may check the plugin's own bytes/hash — Silent Hill
  checks its own exe on disk, so this title may check its plugin; (3) the share is read-only, so the
  file must go through the drop-in motor, which materialises a local directory and never writes to the
  NAS (`docs/dropins.md`).
* **Why not first:** it answers the *input* question as well, so if it is the first run and the banner
  goes away, we will not know which of the two gates changed.

### (B) PE hook on the calibration predicate — the instrument

`rg-pe-hook.dll` already exists, the channel is proven, and the target is a single well-identified
address. Two hooks, in order of directness:

* write `1` to `[*(GameState+0x338) + i*0x5C + 0x59]` for `i = 0..*(GameState+0x340)-1` each tick, or
* hook `IsGunCalibrate` (`0x1409B7180`) to return `1`.

**What it costs:** one `dropin.ini` (2 files: the DLL plus an address ini in the `dongle-haken.ini`
shape with `Preload = true`, `Audit = false`), no new code if `rg_pe_hook` already supports a
"force return value" action, and no rebuild of anything. **Cheapest option by a wide margin.**
**What could go wrong:** (1) `0x1409B7180`'s prologue is `48 89 5C 24 08` with no `nop` hotpatch pad,
so the hook must relocate the prologue — the tool does its own trampoline, but this must be stated in
the address list; (2) the exe's ImageBase is `0x140000000` and it has `.reloc`, so hooking by RVA with
an ASLR-slide check is required, and the address is build-specific — the ini must be tied to sha256
`118b8335…153a`; (3) it changes the *symptom's* gate, not the input path, so TEST will probably still
do nothing (which is itself the informative outcome, §4).

### (C) Answer from the daemon / our own core's `WartranTroopers` profile — the doctrinal target

`EIGEN-KERN.md` already assigns this game to `WartranTroopers`, and 11 games need that one profile.
Our core has no code for CRC `0x71E52EC0` (`ATTRACT-RUN.md §4.2`), so this is real core work: read the
daemon's 64-byte board state and serve this game's plugin calls from it, plus the calibration state.

* **What it costs:** the most — core code, a rebuild of `OpenParrot64.dll`, re-verification, and it
  touches the shared core that other lanes are using on the same bench.
* **What could go wrong:** the only source of truth for how the closed core's `WartranTroopers` profile
  makes the calibration pass is the closed component, which we are not reading; guessing it burns the
  bench slot. It is also the *only* option that both removes the banner and delivers input without a
  per-game drop-in, and it pays for 11 games — which is why it is the target, not the next step.

### (D) Writing the calibration state the game persists — disproven as a standalone answer

The data is already on disk and already correct (`GunCalibrate_bIsHandleVerify_1P=1`, 16 points with
spans ≫ 200). rr9/rr10 **loaded it and the banner stayed** (`ATTRACT-RUN.md §4.3`, MEASURED). So D alone
cannot be the answer. It is also the only option that would require writing into the game's folder,
which on the share is forbidden and in a view is a throwaway overlay — so it buys nothing that A/B do
not.

### What I would pick, and why

**B as the instrument, then A as the fix, with C as the target.** B costs almost nothing, reuses a tool
that is already proven on a Windows game in this exact loader, and — decisively — it is the option whose
failure mode is *informative*: if the banner survives a verified `IsGunCalibrate = 1`, then the banner
is not gated by that predicate and the Blueprint branch is somewhere else, which is worth knowing before
anyone writes a 107-export DLL. A is the fix that makes the title playable and is architecturally
identical to Silent Hill; it is worth doing immediately after B, not instead of it. C is where the
loader doctrine points and where the leverage is (11 games), but it is not a preparation-time step.

---

## 4. The single test

**One run, on our own core, over the share with a kept throw-away write layer, whose only change
against rr9/rr10 is a `dropin/` that hooks the calibration predicate.**

```
# ~/.local/share/ravengun/patches/RaccoonRampage/game.ini   (user layer, for the run only)
[Loader]
Core = ravengun

# ~/projects/ravengun-patches/patches/RaccoonRampage/dropin/dropin.ini
[rg-pe-hook.dll]
To = .
Preload = true
Audit = false
Env = RG_HOOK_INI=Z:\home\deck\.local\share\ravengun\runtime\views\RaccoonRampage\RSGame\Binaries\Win64\geweer-haken.ini;RG_HOOK_LOG=Z:\home\deck\.local\share\ravengun\runtime\views\RaccoonRampage\RSGame\Binaries\Win64\rg-pe-hook.log

[geweer-haken.ini]
To = RSGame/Binaries/Win64

# geweer-haken.ini -- the two calibration targets, tied to the build
#   image sha256 118b8335977ba3706e7d7745eade8e65e0a2b35adf852217c6be8cf4e7a9153a
#   ImageBase 0x140000000
# IsGunCalibrate, RVA 0x9B7180 (VA 0x1409B7180), prologue 48 89 5C 24 08 57 48 83 EC 20 -> force return 1
# ... and/or per-player byte +0x59 in the 0x5C record array at *[GameState+0x338],
#     count at [GameState+0x340], GameState = World+0x180

WIN_EXPECT_CORE=ravengun WIN_SHARE=overlay-keep SHOTSECS=4 \
    bash win-run.sh RaccoonRampage "Raccoon Rampage" RSGame-Win64-Sh rr-geweer1 300
# then, next to the live run:  bash win-play.sh RSGame-Win64-Sh rr-geweer1 25 test
```

**What it changes:** exactly one thing — whether the game's own calibration predicate answers "true".
Nothing else is touched (no input route, no DLL replacement, no core change).

**The positive control that proves the instrument was alive.** The hook writes
`rg-pe-hook.log` **into the game's working directory (the view)**, and it writes its first line
unconditionally — the PE line with `ImageBase` and `SizeOfImage` — before it looks at any address. So
the control is a **chain of three things in the same run's evidence**:

1. `dropins.preload` in `launch-direct.py RaccoonRampage --plan` names `rg-pe-hook.dll` **and**
   `loads=true`/`var=TP_PRELOAD_DLL` (the plan, before the run);
2. `rg-pe-hook.log` exists in the view and its **first line is the PE line** (the DLL ran);
3. the same log's summary line `totaal aangehaakt: N van M` has **N ≥ 1** and names the address
   (the hook landed), and `win-watch.sh`'s 30-second samples still name `OpenParrot64.dll`
   (`fe2ecf8c…5794`) and never `TeknoParrot64.dll` (the core is ours).

If the log is missing, or the PE line is missing, or N = 0, the banner's presence means **nothing** —
that is a dead instrument, not a negative result. (The `rg-pe-hook` precedent already burned a run on
exactly this: `B0-VIJFMIN.md` records `totaal aangehaakt: 0 van 17` and a wiring bug where the launcher
set `RG_DROPIN_PRELOAD` but not `TP_PRELOAD_DLL`.)

**What could make the test lie:**

* **`--plan` says the drop-in rolled out but the DLL never loaded.** The plan's `dest` only proves a
  file was copied. The proof is `rg-pe-hook.log` + the DLL in `/proc/<pid>/maps`. Take both.
* **The prologue already starts with `e9`.** Then the hook *refuses* the address by design. **MEASURED
  that this is not the case here** on the share's copy (`dis-isgun.txt`: `48 89 5C 24 08 57 48 83 EC 20`),
  but it must be re-checked in the *run's* view, because the closed core writes `e9` and a stale view
  would poison the run.
* **The predicate is evaluated once, at widget construction, before the first tick.** Then a hook that
  only fires later proves nothing about a banner that was already drawn. Tell: the banner is present in
  the frame but `rg-pe-hook.log` shows the hook was called *after* the first non-black frame.
* **The banner is not gated by `IsGunCalibrate` at all.** Then the hook lands, the predicate returns 1,
  and the banner stays. That is a real (negative) result, and it is only trustworthy because of the
  control above.
* **The exe is not the one this analysis is about.** Tie the run to `rr10-SHA256SUMS.txt`
  (`118b8335…153a`) and refuse to interpret a run whose exe differs.
* **The write layer hides the game's own settings.** rr8 proved the read-only folder changes the game
  (HUD 0/6 vs the saved counts). Use `overlay-keep` so `Setting_Hardware.ini` loads *and* we can read
  what the game wrote afterwards — that is also the instrument for "Could not determine" 2.

---

## 5. Falsifiable prediction, and the alternatives with their tell

**Prediction.** *If the calibration state is satisfied, then the banner is gone from the FRAME and the
game accepts a TEST press.*

**This prediction has to be split, and the split is the measurement.** The banner is gated by
`IsGunCalibrate` (§2.2); the TEST press is gated by whether the game reads any IO at all —
`BIND_GetIoData` / `BIND_IsIoWorking` / `BIND_InitUnisIo`, and (measured) the game does not read
`TeknoParrot_JvsState` itself (`ATTRACT-RUN.md §4.1`; TEST byte 8 = `01` and the attract loop carried
on in rr7 and rr9). Those are **two different gates**, and §4's test moves only the first. So:

| outcome | what it means | the tell in the same run |
|---|---|---|
| **banner gone, TEST still dead** *(the prediction I expect)* | the calibration gate and the input gate are **independent**; `IsGunCalibrate` is the banner's only C++ gate, and the input path is still the plugin's IO read | `rg-pe-hook.log` shows the hook landed (N ≥ 1) **and** the frame after the hook has no banner **and** `win-play.sh`'s TEST press at `rr-geweer1-play.txt` shows byte 8 = `01` in `/dev/shm/ravengun_JvsState` with no menu in the following captures |
| **banner gone and TEST works** | the calibration state is a *precondition* of the whole gun layer: the game withholds IO polling while uncalibrated. One gate, not two, and §4.2 of `ATTRACT-RUN.md` collapses into this one item | same log, and the game's own "Homepage" menu appears within ~1 s of TEST down (the rr6/rr8 signature) |
| **banner stays, hook landed** | the banner is **not** gated by `IsGunCalibrate` alone — either a second predicate, or the widget latched its visibility before the hook's first call | `rg-pe-hook.log` shows the correct address hooked and `totaal aangehaakt: 1 van 1`, the frame still carries the banner, and (if the hook can log return values) the predicate is seen returning 1 |
| **banner gone, then the game stops advancing / dies** | we broke the state, most likely by writing `+0x59` on a record the game then re-derives, or by a bad trampoline | the last frame's timestamp vs the run's own limit, the process gone in `win-watch.sh`'s samples, and the rc from the launcher — not a game-side negative |
| **no `rg-pe-hook.log` at all** | the instrument was dead; the run says nothing | plan shows the drop-in, `/proc/<pid>/maps` has no `rg-pe-hook.dll` |

**The honest negative I would accept as an answer:** banner gone and TEST dead. It proves the banner is
a *cosmetic* gate on the calibration predicate, that the real blocker for playability is the plugin's
IO read, and it names the next piece of work exactly — a replacement `UsbPluginsDll.dll` (option A) or
the `WartranTroopers` profile (option C).

---

## 6. What is different about this title, measured — not assumed

| claim in the brief | what the measurement says |
|---|---|
| "the game reads its own `UsbPluginsDll.dll` and not the daemon's shared-memory state" | **CONFIRMED, and sharpened.** The exe builds `<game dir>\UsbPluginsDll.dll` at `0x1409DC455` and `LoadLibraryW`s it, caching the handle at `0x143C19208`; it resolves entry points by name via `GetProcAddress` (`0x140BF9B00`). `ATTRACT-RUN.md §4.1` measured that it does not read `TeknoParrot_JvsState`. |
| "about 107 `BIND_*` functions" | **CONFIRMED exactly**: 107 named exports, 0 forwarders, all in `.text`. |
| the gun layer that works for other titles is not reaching this game | **CONFIRMED, and the reason is narrower than assumed.** It is *not* that we are missing `BIND_OpenGunDevice`/`BIND_GetGunData` — the exe never asks for those by name (0 occurrences each, ASCII and UTF-16). The live IO interface for this title is the **UNIS IO board**: `BIND_InitUnisIo`, `BIND_IsIoWorking`, `BIND_GetIoData`. |
| Transformers works through the daemon's gun board | **CONFIRMED as a different route**: `Route = hid-board`, "de brug" serves it (`INVOER-ROUTE.md`). Not the same channel as `tp-shm`-through-a-core. |
| Terminator and Halo were shown with synthetic injection | **CONFIRMED but a different world**: both are **Linux ELFs** under `ElfLdr2`/BudgieLoader; their channel is `LD_AUDIT`/`RG_DROPIN_PRELOAD` into a Cygwin loader process, and their target is a statically linked HASP client (`patches/Halo/README.md`, `patches/Terminator/dropin/dropin.ini`). A Windows PE game cannot use that channel. |
| Silent Hill's replacement `shaiolib.dll` has a placeholder `rg_poll_inputs()` | **CONFIRMED** (`patches/SilentHill/shaiolib_rg.c:256`, README:79, `DEMOS-NOT-PLAYABLE.md`). It exports the same 52 names at the same ordinals, built with an inline `.def` by `build-shaiolib.sh`. |
| — | **The one channel that does fit a Windows game here is `TP_PRELOAD_DLL`**, used already by `patches/LuigisMansion/dropin/rg-pe-hook.dll` (`docs/dropins.md`: "Windows-spel onder Wine via `OpenParrotLoader(64).exe`" → `TP_PRELOAD_DLL`, one DLL, loaded by the loader after the core, into the game process). |
| — | **This title is not a singleton.** `EmulationProfile = WartranTroopers`, shared with 13 games; 11 of them need the same profile for input (`INVOER-ROUTE.md`). Whatever is built for the calibration/IO interface is worth building for that whole block. |

---

## Could not determine

1. **Where the banner's text and its widget live.** Not in the exe (0 hits for `not calibrated`,
   `calibrated`, `Setting Menu` in ASCII and UTF-16LE), and not greppable in the 1.3 GB pak. The pak's
   entries are compressed, so the empty grep is not evidence of absence and I did not parse the pak
   index to decompress and search it. The text and the Blueprint branch that shows it are therefore
   unlocated.
2. **Which of the two `+0x59` writers runs at boot on our core, and in what order** — i.e. whether the
   game applies `Setting_Hardware.ini` (writer `0x1409C46F8`, setting 1) and something later clears it
   (writer `0x1409BC3C7`, setting 0), or whether the apply path is never reached. Both are consistent
   with the measurements; nothing in a file can separate them. **This is the one open item that decides
   whether the fix is a hook at all** — and it needs a live run with `WIN_SHARE=overlay-keep` plus a
   sample of `[*(World+0x180)+0x338] + 0x59` per player over time. That is a run, and no run was made.
3. **Which player id the Blueprint passes to `IsGunCalibrate`, and whether the banner covers all
   configured players or only 1P.** The ini has `_1P.._4P` and only `_1P=1`, so the answer matters: if
   the banner requires all four, the current ini guarantees the banner regardless of the runtime flag.
   The Blueprint argument is not statically visible in the exe.
4. **How the closed core's `WartranTroopers` profile satisfies the calibration.** Whether it writes the
   byte, hooks `IsGunCalibrate`, or makes the plugin's `BIND_IsIoWorking` succeed is inside the closed
   component; it was not read, and by the workspace rules it should not be relied on. This is why option
   C is ranked as the target rather than the next step.
5. **Whether the game validates its own plugin DLL** (hash, size, or a self-check like Silent Hill's
   on-disk exe check). If it does, option A must patch the check in memory the way
   `rg-shaiolib.patches` does for Silent Hill — I did not find such a check, and I also did not search
   for one exhaustively.
6. **The exact role of `0x1409B70A0` (`IsGun`)**, the sibling predicate. It returns from a different
   field (`+0x2A0` of the manager / `+0x1C3` of the GameState / `+0x34` of an 0x5C record) and its own
   exec thunk; whether the banner's Blueprint branch calls `IsGun` as well as `IsGunCalibrate` was not
   determined.
7. **`BIND_UpdateMatchData`** — 2056 bytes, the largest real body in the plugin, bound nowhere by name
   in the exe (the only `BIND_*` in the exe's list is absent). Its purpose is undetermined and it is
   probably dead for this title.

---

**Raw output:** `/tmp/rr-geweer/` on the deck — `01`…`29` numbered `.out`/`.sh`/`.py`, the aligned
disassemblies `dis-*.txt`, `usb-exports-final.tsv` (the calibrated 107-row export table),
`usb-shipped.dis`, `usb-shipped.objdump.txt`, `exe-objdump-p.txt`. Nothing was written to the NAS and
nothing in `~/projects/ravengun` was modified.
