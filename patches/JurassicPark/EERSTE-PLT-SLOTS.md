# EERSTE-PLT-SLOTS — naming the 577 import slots of Jurassic Park's dumped `game`

**Date:** 2026-10-13, 22:20–22:50. **Game:** `game`, 17,694,720 B, sha256 `c18a9329…b4269`, ELF32 i386,
ET_EXEC at `0x08048000`, no section headers. **Loader:** our own `tools/rg-elfloader`.
**Labels:** **MEASURED** = a log line, a file comparison, or disassembly read from a function head;
**DERIVED** = a conclusion drawn from measurements.
**The table:** `dropin/eerste-plt-slots.tsv` (578 rows). **How it was made:** `probe/` (see its `LEESMIJ.txt`).

This file supersedes the note in `packaging/native-games.json` that the native route "jumps to a fixed
address from the dump (SIGSEGV at 0xb7227dc0 before main); the protection layer BudgieLoader puts at
that address is not there natively". `0xb7227dc0` is the cabinet-era address of `syscall` and has
nothing to do with a protection layer (§2).

---

## 1. Result

With the table, **our loader binds every import slot by name and the game runs its own code.**
**MEASURED**, bench run 2 (22:47:33), product launcher `play-native.sh --loader`, on the TV:

```
rg-elfloader: slottabel /rg/jp-slots.tsv: 576 slots op naam gezet, 0 naam-niet-gevonden, 1 slot-had-andere-waarde, 1 zwak/0, 0 buiten beeld
rg-elfloader: eerste-PLT: 0 op naam hersteld, 0 no-op-trampoline, 0 naam-niet-gevonden (van 39 stale-namen)
rg-elfloader: spring naar e_entry 0x813befc …
Assert file src/game_sys.c, line 760:
Game dongle initialization failed!
== exit 0
```

The one slot with another value is `fork` (bound by the normal relocation pass), the one weak slot
stays 0. The game then stops by its own decision at its dongle check — the next wall, not this one.

**What this does and does not prove.** It proves the 576 names resolve on this host and that the path
from `_start` through the C runtime, the static constructors and `main` up to the dongle check works
with them. It does **not** prove each of the 576 names is right: only the slots that were actually
called have been exercised. A wrong name would show up as a crash or wrong behaviour at its first call.

---

## 2. What the binary is, and why the names are gone

**MEASURED** on the file:

* 578 first-PLT stubs at `0x8139740..0x813bb50`, GOT slots `0x8add00c..0x8add910`. 577 stubs push
  `0xdeadbeef` instead of a relocation index; one pushes 0 (`fork`).
* In `game` 576 slots hold a `0xb7…` address (571 distinct), one holds its own stub+6 (`fork`), one
  holds 0 (a weak symbol). These are real function addresses from **one run on the original cabinet**:
  the image was dumped after the cabinet's dynamic linker had bound everything and the HASP envelope
  had restored the code, and before `_start` ran (no constructor guard is set, `.bss` is otherwise zero).
* The dynamic symbol table has only 108 undefined symbols. The relocation table has 101 entries: one
  for `fork`, and 100 for a **second** GOT at `0x090e73f8` that belongs to the envelope and that no
  game code uses. So a normal dynamic linker repairs the wrong table.
* `game_original` (the envelope version) does **not** help: same `0xdeadbeef` stubs, same stripped
  symbol table, every slot at stub+6. It differs from `game` in 14,289 function prologues replaced by
  a call into the envelope, and one 9,412-byte encrypted block. It yields no extra names.

Before this table the loader could name 33 slots (those whose stale address also occurs in the second
GOT, where the name survives) and put the other 543 on a no-op. **MEASURED**, bench run 1 (22:25:59):
the very first import the entry code calls, `__libc_start_main` (stub `0x8139800`, slot `0x8add03c`),
is one of the 543 — the no-op returned and the kernel logged a fault at `0x813bf1d`, the `hlt` right
after that call.

---

## 3. Method: rebuild the cabinet's library layout

The stale addresses fall in library-sized clusters. For each cluster a candidate library is fitted:
one page-aligned base must put many stale addresses exactly on exported symbols. The 33 names already
known are the anchors; the fit reproduces all 33 with no conflict. **MEASURED** (`probe/align.py`,
`probe/mktable.py`).

| cabinet base | library | identified as | slots |
|---|---|---|---|
| `0xb7141000` | libc | near libc6 2.19-0ubuntu6 — **not identical**, piecewise shifts | 219 |
| `0xb72ef000` | libgcc_s | libgcc1 4.9-20140406 | 1 |
| `0xb730c000` | libudev | libudev1 204-5ubuntu20 | 23 |
| `0xb7320000` | libm | libc6 2.19-0ubuntu6 | 37 |
| `0xb736f000` | libpthread | libc6 2.19-0ubuntu6 | 54 |
| `0xb738b000` | libglut | freeglut3 2.8.1-1 | 1 |
| `0xb73c5000` | libGLU | libglu1-mesa 9.0.0-2 | 2 |
| `0xb7439000` | libGL | **NVIDIA 346.59** — 130 of 130; the only one of 23 driver versions tried | 130 |
| `0xb7558000` | libSDL-1.2 | libsdl1.2debian 1.2.15-8ubuntu1.1 | 42 |
| `0xb75f0000` | libdl | libc6 2.19-0ubuntu6 | 5 |
| `0xb75f5000` | libstdc++ | libstdc++6 4.8.2-19ubuntu1 | 29 |
| `0xb76de000` | libPhysXLoader | the copy in TeknoParrot's `ElfLdr2/libs` | 1 |
| `0xb76ea000` | libogg | libogg0 1.3.1-1ubuntu1 | 14 |
| `0xb76f3000` | libtheora | libtheora0 1.1.1+dfsg.1-3.2 | 19 |

So the cabinet ran Ubuntu 14.04 (i386) with the NVIDIA driver. The earlier attempt to name the libGL
slots by offset scored 0 of 93 because it fitted Mesa's libGL; the cabinet's was NVIDIA's.

**Confidence per row** (column 7 of the table): 34 certain (the name is in the relocation table),
540 high, 4 medium. The four medium rows are `iconv`, `iconv_open`, `iconv_close` and `setlocale`:
short runs in a shifted region of libc, backed by their callers for two of them.
Second opinions that agree: `__printf_chk` has 2,364 call references, `_Unwind_Resume` 1,199;
`glGetInfoLogARB` is called from `printInfoLog`, `gluErrorString` from `LogGLErrors`.

**Limits, written down:**

* The libc names rest on a *near* build. The exact cabinet libc was not found (2.19-0ubuntu6.1 … 6.14
  could not be fetched; 6.15 fits worse than 6). 28 rows that are IFUNC-selected routines (memcpy,
  strlen, sinf, …) are named through the implementation symbol of the debug package.
* Five names occur twice (`strtok_r`, `srand`, `htonl`, `htons`, `strdup`) — four pairs share one
  stale address, `strdup` is an alias at a second address.
* The packages were only read with `readelf`; nothing from them is used at run time, and they are not
  in this repository (`probe/LEESMIJ.txt` lists them).

---

## 4. How the loader uses it

`tools/rg-elfloader/rg_elfloader.c`, `pas_slottabel_toe()` (working tree of the loader repository, not
yet committed or installed). `RG_ELF_SLOT_TABLE` names the file; without it nothing happens. A slot is
written only if it **still holds the stale value the table lists**, so a table for another version of
the game touches nothing. After the table, any stale slot still left for the no-op trampoline is
reported loudly. The table is per-game data and lives here, in `dropin/`, with a section in
`dropin.ini` that sets the variable (path relative to the game's working directory).

Not done yet: the native launcher does not apply `dropin.ini` (the bench runs bound the file with
`--bind-ro` and set the variable with `--env`), and there is no test for this class of binary.

---

## 5. Other things measured on the way

* **The SDL2 chain.** Bench run 0 (22:25:05) died while loading libraries: `Failed loading SDL2
  library.`, exit 134. The staged 32-bit `libSDL2-2.0.so.0` needs `libsamplerate.so.0`,
  `libXcursor.so.1`, `libxkbcommon.so.0` and `libdecor-0.so.0`, which this host has no i386 build of,
  and the native profile did not bind `libXrandr.so.2` / `libXrender.so.1`. All six are in the Steam
  runtime the other staged libraries came from. With them bound, all 16 needed libraries load.
* **Do not put all of `ElfLdr2/libs` on the library path.** Its old `libogg.so.0` is then loaded first
  and the host's `libsndfile` (pulled in by SDL2 through libpulse) fails on a missing symbol; the
  result is the same "Failed loading SDL2 library". Only the four PhysX files are needed from there.
* **Watch item.** The loader's shim exports the game's statically linked zlib, libxml2, OpenSSL,
  libusb, hidapi and OpenCV into the global scope. Host libraries loaded later (Mesa's LLVM/gallium
  import `crc32`, `inflate*`, some `xml*`) could bind to the game's 2015 copies. Not observed; not ruled out.
* **Envelope thunks.** 166 defined symbols of the game (`physxFinal`, `FontInit`, `_init`, …) have a
  symbol value that points at envelope thunks at `0x9044111..`. A `dlsym()` of such a name lands in
  the envelope.

## 6. The next wall

The dongle check: see `README.md` (what the game asks) and the run-3 trace (22:48:20) — with the hook
and emulator loaded the game logs in, finds the licence area blank, writes a fresh 64-byte record
(all zeros, byte 30 = 1), calls `hasp_decrypt` on 512 and on 256 bytes, reads four more fields and
asserts "Game dongle initialization failed!". There is no reply data for those two decrypt calls for
this title. Whether they can be answered without the physical dongle is the open question.
