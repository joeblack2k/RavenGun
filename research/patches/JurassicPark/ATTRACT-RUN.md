# ATTRACT-RUN — Jurassic Park Arcade runs its attract loop on our own loader

**Date:** 2026-10-13, 22:25–23:21. **Game:** `game`, 17,694,720 B, sha256 `c18a9329…b4269`, version 1.08,
build May 13 2015. **Loader:** our own `rg-elfloader`, sha256 `2792521b…` (the build with the slot-table
reader), through the product launcher `scripts/play-native.sh --loader --gamescope`, in a bubblewrap
sandbox, on the TV through gamescope.
**Labels:** **MEASURED** = a log line, a file comparison, a frame that was looked at, or disassembly read
from a function head; **DERIVED** = a conclusion drawn from measurements.
**Evidence:** `bewijs-attract-20261013/` next to this file. **The import table:** `EERSTE-PLT-SLOTS.md`.

This file supersedes the "parked" status in `game.ini`, the note in `packaging/native-games.json`, and
the parts of `README.md` listed in §6.

---

## 1. Result, and what is NOT shown

**Shown.** The game boots, initialises its dongle, loads its assets and runs its attract loop: intro
captions, gameplay demos, the Universal and Raw Thrills logos, high-score tables, the epilepsy notice,
"FREEPLAY" / "PRESS START TO BEGIN" along the bottom. Full 16:9 at 1360×768, scaled to the TV.

| run | attract window | length | how it ended | witnessed by |
|---|---|---|---|---|
| **11** | 23:08:39 (game log: "Entering attract mode.") – 23:21:01 (last capture) | 12 min 22 s | the run's own limit (`--seconds 780`) | the run's captures (71 since 23:08:45, 69 distinct; two contact sheets of twelve looked at) **and** the coordinator's independent capture, 23:14:35 – 23:20:24, 26 frames all distinct |
| 10 | 23:06:11 – 23:07:44 | 1.5 min | the run's own limit (120 s) | nine captures, looked at |

**NOT shown — open items, stated plainly:**

1. **"RIO Disconnected" is on every frame.** The game's I/O board (Raw Thrills "RIO", reached over USB)
   is not there. The game demos anyway, but it has **no inputs**: no START, no coin, no trigger, no
   test button. That is the whole difference between "it demos" and "it is playable" for this title,
   and it is not solved. The game logs `InputInit(): RIO not connected (error code: -3)` and
   `ErrorMessage shown: error=0x1 'STR_ERRORMSG_RIO_DISCONNECTED'` on every boot.
   **Shared with NERF Arcade (added 2026-10-14):** NERF shows the same banner, and both games open
   the same USB device (vendor `0x0C70`, product `0x0780`) with the same library code. It is one open
   item for two titles: `patches/RIO-BORD.md`.
2. **Nothing has been played**, and no gun has been tried. `Joysticks Found=0`.
3. **Four shaders fail to compile** (`shader108`, `shader108c_sk`, `shader108d`, `shader108d_sk`,
   `ec=-9596`). The attract loop ran 12 minutes without them; what they draw has not been looked at.
4. **Sound: an audio stream exists, nobody listened.** The game process had a live, uncorked stereo
   stream at 23:15:08 (`pactl`). The game's second sound path fails on every boot
   (`JPS: jpsdrvr_init(): Could not open DEV_DSP` → `Failed to init JPS Sound System`), as do 122 rumble
   sound files it cannot open.
5. **It is not a product run yet.** The launcher is the product's, but six things were supplied by hand
   (§5).
6. **The dongle image is constructed**, not dumped (§3.6).
7. **The network was isolated** in these runs (the launcher's sandbox does that); the game calls
   `ifconfig` and `nvidia-settings`, which the sandbox does not have. Both are harmless so far.

---

## 2. The changes that got it there, in the order the walls appeared

| # | wall (what the game or the system said) | what removed it | run |
|---|---|---|---|
| 1 | `Failed loading SDL2 library.`, exit 134, before any game code | six i386 libraries SDL2 needs, added to the launcher profile and its staging list | 0 → 1 |
| 2 | kernel: fault at `0x813bf1d`, the `hlt` after the entry code's first import call | **the rebuilt import table**: all 577 first-PLT slots named (`dropin/eerste-plt-slots.tsv`), read by the loader (`RG_ELF_SLOT_TABLE`) | 1 → 2 |
| 3 | `Game dongle initialization failed!` (`src/game_sys.c:760`) | the dongle hook and emulator loaded — and, the actual cause, **a preload that retries a thread without real-time priority** | 2 → 4 |
| 4 | `unable to create usb flash drive mount rules` (`g5/sys_linux.c:289`) | **the udev rules file present in the sandbox** | 4 → 5 |
| 5 | `Silent failure loading program 'CircleFail'`, 2.29 million times in a minute | **the dump's own decrypted scripts** mounted over the encrypted ones | 5 → 6 |
| 6 | a black screen while the game log says it is loading | **SDL 1.2 OpenGL scaling off** (`SDL12COMPAT_OPENGL_SCALING=0`) | 6 → 9 |
| 7 | the factory screen "Cabinet Type: Not Set", asking for Vol+/Vol−/Test | **the cabinet-type byte set to 4 ("upright")** in the dongle licence record | 9 → 10 |
| — | SIGSEGV right after "Welcome to 1.08" | **`force_glsl_version=120` must be OFF for this game** | 7, 8 |

### 2.1 The import table

See `EERSTE-PLT-SLOTS.md`. In one line: the image was dumped from a running cabinet, its 577 import
stubs no longer say which function they are, and the names were recovered by rebuilding the cabinet's
library layout (Ubuntu 14.04 i386 + NVIDIA 346.59). **MEASURED** (run 2): 576 slots set by name, 0 not
found, 0 left on the no-op; `main` runs.

### 2.2 The real-time thread, not the dongle

With the hook and emulator loaded (run 3) every dongle answer was accepted and the game still asserted.
`DongleInitialize` (0x83362f0) ends by starting two threads through `StartThread` (0x84609d6), which
always asks for an explicit schedule; the dongle write thread asks for SCHED_FIFO priority 50, and for
an unprivileged process `pthread_create` then returns EPERM. On the cabinet the game runs as root.
**MEASURED**: a standalone 32-bit program asking for SCHED_FIFO/50 gets `1 (Operation not permitted)` on
this host (`harness/rt-draad-proef.c`); with the preload it gets its thread.

`librg-threadprio.so` (source `tools/rg-threadprio/rg_threadprio.c` in the loader repository) wraps
`pthread_create`: if the call fails with EPERM and the caller asked for an explicit schedule, it retries
with the caller's own schedule. **MEASURED** (run 4): its log shows the refused thread and the retry
returning 0, and the game passes dongle init. Run 11 logged 2,233 such retries: the game starts every
worker thread this way.

### 2.3 The udev rules file

The game looks for `/etc/udev/rules.d/10-usb-automount.rules` and, not finding it, tries to write it
(it is root on the cabinet) and asserts when that fails. A directory holding that file, bound over
`/etc/udev/rules.d` in the sandbox, is enough (`harness/udev-rules.d/`; the two rule lines are the ones
the game itself would write). **MEASURED** (run 5).

### 2.4 The encrypted scripts

The game loads state-machine scripts from `/pm/g5/dino/data/programs_enc`; when the length of a
script's name is a multiple of five it sends the whole file through `hasp_decrypt` and requires the
result to start with `//\n`, retrying without limit. 354 of the 1,516 files are encrypted. The dump
ships the same 1,516 files in clear as `programs_dec/` (same paths and sizes). With `programs_dec`
mounted read-only over `programs_enc` the game calls `hasp_decrypt` on text that is already readable
and the emulator's pass-through returns it unchanged. **MEASURED** (run 5: 2,289,875 decrypt calls in a
minute and the same line 2,289,868 times in the game log; run 6: 10 decrypt calls, no failure line).
No file on the share is changed and no game content is copied.

Not needed, but prepared and untested: a reply table for the 316 distinct encrypted prefixes, which
would leave the files byte-identical on disk in case the game's boot CRC list ever objects.

### 2.5 The black screen

`libSDL-1.2.so.0` here is sdl12-compat on SDL2. By default it makes the game render into its own
buffer and scales that to the window. This game takes its OpenGL functions by name and binds
framebuffers itself, so what the compatibility layer presents is its own empty buffer. **MEASURED**:
run 6 — eight identical black captures (`shots/run6-230116-zwart-met-sdl-schaling.jpg`) while the game
log reaches "Loading perm programs"; run 9, identical except `SDL12COMPAT_OPENGL_SCALING=0` — the
"Loading" screen and then the game's own operator screen
(`shots/run9-230336-loading-rio-disconnected.jpg`, `shots/run9-230407-cabinet-type-not-set.jpg`).

### 2.6 The cabinet type

Run 9 stopped on the factory screen "Cabinet Type" with "Not Set" selected and "Previously Saved Value:
Not Set". The game takes the cabinet type from byte `0x1d` of the dongle licence record, which it had
itself initialised to 0. The game's own option table (`g5/dino/data/adj/game/luts/cabType.txt`) reads
0 none, 1 motion deluxe, 2 motion bench, 3 environmental, **4 upright**, 5 motion test 1, 6 Namco kit,
and the dump's own saved setting (`pmuser/adj/save/hardware/cabType.0.txt`) is `val = 4`. With byte
`0x1d` = 4 the game logs `Adj_Save 'hardware\cabType' to 4` and goes on to "Entering attract mode."
**MEASURED** (run 10, run 11). Upright was taken because it is what the dump itself had saved; it is a
setting, and one byte.

### 2.7 What must stay off

`force_glsl_version=120`, which removed Terminator's crash, **crashes this game**: SIGSEGV inside a
host library two seconds after start, right after "Welcome to 1.08" (runs 7 and 8; in run 8 it was the
only difference from run 6). Why has not been looked at.

---

## 3. The dongle, as the game uses it

**MEASURED** from the emulator's trace of run 3 and from the code:

1. `hasp_login` twice (vendor code `0xffff0000`).
2. `hasp_read(0xFFF2, offset 30, 1)` — the "memory initialised" flag. If it is not 1 the game writes a
   fresh 64-byte record at offset 0: all `00`, byte `0x1e` = `01`.
3. `hasp_read(0xFFF2, 28, 1)` — country (0..7, anything else is treated as 0).
4. `hasp_decrypt` on two tables that are in the binary itself (512 and 256 bytes). Only the status is
   checked; nothing in this build uses the plaintext.
5. `hasp_get_sessioninfo` — a `<haspid>` must be in the answer.
6. `hasp_read` at offsets 29 (cabinet type), 60 (cabinet serial), 32 (lifetime coin count); the return
   values are ignored at init.
7. Later: `hasp_decrypt` on encrypted scripts (§2.4), and a 32-byte `hasp_encrypt` every 30–40 s from a
   "noise" thread (21 in run 11).

`0x1c` country, `0x1d` cabinet type, `0x1e` initialised flag, `0x20–0x23` coin count, `0x3c–0x3f` serial.
There is no checksum. **The image in the drop-in is constructed, not dumped:** the record the game
itself writes, with byte `0x1d` set to 4, followed by 48 bytes of `FF` that are never read
(sha256 `670826fb…`). Nobody has the real dongle's bytes, and nothing only the physical dongle has was
needed to get this far.

---

## 4. Reproducing run 11

```
# once: the four i386 libraries SDL2 needs (now in the launcher's staging list)
scripts/play-native.sh --stage-libs
# the run (harness/jp-run.sh wraps this in scripts/rg-tv-env.sh and a capture loop)
scripts/play-native.sh --game JurassicPark --loader --gamescope --seconds 780 \
  --env RG_ELFLOADER_VERBOSE=1 --env RG_ELF_SLOT_TABLE=/rg/jp-slots.tsv \
  --env LD_PRELOAD=/pm/libravengun-hasphook.so:/pm/librg-threadprio.so --env RG_THREADPRIO_LOG=/pm/threadprio.log \
  --env RAVENGUN_HASP_DIR=ravengun-hasp --env RAVENGUN_HASP_GAME=JurassicPark \
  --env RAVENGUN_HASP_ELF_LIB=./ravengun-hasp.so --env RAVENGUN_HASP_ELF_LOG=hasphook.log --env RAVENGUN_HASP_VERBOSE=1 \
  --env SDL12COMPAT_OPENGL_SCALING=0 \
  --bind-ro <dropin>/eerste-plt-slots.tsv /rg/jp-slots.tsv \
  --bind-ro <harness>/udev-rules.d /etc/udev/rules.d \
  --bind-ro "<share>/Jurassic Park/programs_dec" /pm/g5/dino/data/programs_enc
```

Placed by hand in the launcher's local overlay of `/pm`
(`~/.local/share/ravengun/native/JurassicPark/0/upper/`) before the run: `libravengun-hasphook.so`,
`ravengun-hasp.so`, `ravengun-hasp/JurassicPark/` (the drop-in's files), `librg-threadprio.so`, and the
symlink `g5/dino/pmuser -> ../../pmuser` (the game writes its logs and saves under `/pm/g5/dino/pmuser`;
on the share `pmuser` sits at the top level). Nothing was written to the share.

**Checksums** (`runs/run11-SHA256SUMS.txt`; the hook, emulator, memory image and slot table in the run
were byte-identical to the drop-in as committed with this file):

| piece | sha256 |
|---|---|
| `game` | `c18a9329e72a3639…` |
| loader `rg-elfloader` (installed copy = repository build) | `2792521b0174ab1a3abf359d51a2b1f0ffa9fcc98deb6bef05ff30b928b620e0` |
| `scripts/play-native.sh` (working tree) | `e834a0a59f8539f4…` |
| `packaging/native-games.json` (working tree) | `09bdded6cb9a1bee…` |
| `dropin/eerste-plt-slots.tsv` | `047d209f34bcf96b…` |
| `dropin/libravengun-hasphook.so` | `de9bf43137907e3d…` |
| `dropin/ravengun-hasp.so` | `6c7833dc8ed65fa5…` |
| `dropin/…/memory/…0010` | `670826fb09d4a41f…` |
| `librg-threadprio.so` (bench build of `rg_threadprio.c` `11915dfc…`) | `7f0acb0ef5286c62…` |

**Frames:** `runs/run11-opnames-index.txt` lists all 75 captures with time, hash and size (three pairs
are identical: two of the loading screen, and two attract frames 6 min 46 s and 3 min 28 s apart).
`shots/run11-contactvel.jpg` (twelve frames, 23:08:52 … 23:21:01) and
`shots/run11-contactvel-eerste-5min.jpg` (twelve, 23:08:52 … 23:14:04) were looked at.

---

## 5. Product versus bench — what was still supplied by hand

1. **Applying the drop-in on the native route.** The launcher does not read `dropin.ini`; the files
   were copied into the overlay and the variables passed with `--env`.
2. **`librg-threadprio.so`**: source in the loader repository's working tree, built by hand, no build
   script and no test yet. It is not game-specific — every cabinet game on our loader runs
   unprivileged — so it belongs in the loader or next to it, not in a game's folder.
3. **The udev rules directory**, the **`programs_dec` mount** and **`SDL12COMPAT_OPENGL_SCALING=0`**:
   per-game facts that belong in this game's launcher profile (it has `mounts`, `files` and `env` keys).
4. **The `pmuser` symlink** in the overlay.
5. **The slot table** was bound from a scratch copy; the committed copy is `dropin/eerste-plt-slots.tsv`.
6. **The I/O board** — not supplied at all (§1, item 1).

---

## 6. Instruments, and corrections to earlier notes

**What was loaded** (`runs/run11-LAADBEWIJS.txt`, read from `/proc/<pid>/maps` of the live game process,
pid 1773663, ELF32, at 23:08:40): the dongle hook, the emulator and the thread-priority library were
mapped; `RG_ELF_SLOT_TABLE`, `LD_PRELOAD` and `SDL12COMPAT_OPENGL_SCALING=0` were in its environment.
No diagnostic loggers were preloaded in these runs. The 64-bit helpers the game starts (`sh`,
`ifconfig`, …) refuse the two 32-bit preloads with "wrong ELF class"; that is expected and says nothing
about the game process. The emulator's argument trace stops after 64 lines; the hook's own log counts
every call by name (`runs/run11-hasphook.samenvatting.log`).

**Corrections to `README.md`** (kept as written, superseded here):

* The game **does** write to the dongle (64 bytes at init when the flag byte is not 1); "read-only" was wrong.
* The reads are 1 and 4 bytes long, not all 4; the feature check at `0x837c9e0` has no caller in this build.
* `hasp_decrypt` and `hasp_encrypt` **are** used.
* "It never reaches its dongle check because of the OpenGL blocker" described the Wine route. On our
  own loader it reaches and passes it.
* The 30 / 29 / 26 `hasp_` symbol counts in various notes: the hook finds and hooks **11** functions by
  name, which is all it needs.

## Full-resolution captures

`bewijs-attract-20261013/opnames-run11/` holds the 75 captures of run 11 as they were taken (161 MB).
They were moved here from the session's scratch folder in /tmp on 2026-10-14; each copy was compared
byte for byte with its original.
