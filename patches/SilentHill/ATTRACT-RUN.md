# ATTRACT-RUN — Silent Hill: The Arcade runs its attract loop on our own core

**Date:** 2026-10-13 23:37 – 2026-10-14 00:09. **Game:** `KSHG.exe`, 3,022,848 B, PE32 i386,
sha256 `9b8a6c1b…2995`. **Core:** our own 32-bit OpenParrot build (`OpenParrotWin32/OpenParrot.dll`,
sha256 `413027de…`) on its generic route — it has no code for this title — started by
`scripts/launch-direct.py SilentHill --run` under Wine (`proton-11.0-2`), in gamescope on the TV.
TeknoParrot's closed core was not loaded.
**Labels:** **MEASURED** = a log line, a file comparison, a frame that was looked at, or disassembly read
from a function start; **DERIVED** = a conclusion drawn from measurements.
**Evidence:** `bewijs-attract-20261013/`. **What the game is:** `README.md`.

Nothing existed about this title in the project before tonight.

---

## 1. Result, and what is NOT shown

**Shown.** The game passes its twelve-step boot self-test and runs its attract loop: the region
warning, the caution screen, the opening movie, the title, two play demos, the story movies, the Konami
logo, the shop-ranking tables, the how-to-play screens — with "Press the START button" for both
players, the e-Amusement ticker and "FREE PLAY".

| run | window | length | how it ended | witnessed by |
|---|---|---|---|---|
| **7** | 23:56:21 (first attract screen) / 23:56:42 (movie starts) – 00:08:57 (last capture) | 12 min 15 s of movie-era attract | the run's own limit (780 s, rc 124); nothing left running | the run's captures (75, all distinct; two contact sheets of twelve looked at) |
| 6 | 23:54:46 – 23:55:38 | 1 min | the run's own limit (100 s) | six captures, looked at |

**NOT shown — open items, stated plainly:**

1. **No inputs.** Our replacement I/O library answers "the boards are there" and delivers nothing: no
   START, no coin, no trigger, no TEST. The game **demos; it is not playable.** `rg_poll_inputs()` in
   `shaiolib_rg.c` is where the input bridge has to go.
2. **Nothing has been played**, and no gun has been tried.
3. **The region is Asia** (`GNGHLAAA`), chosen because it passes the game's SPEAKER step without a
   speaker-setup file. The game says so on screen ("for sale and use only in Asia excluding Japan and
   Korea"). Europe (`GKGHLEAA`) needs `Data/TestDat/speaker.1`, which the dump does not have; three
   bytes `01 F1 D1` are expected to do (not tried). Which region the owner wants is his call.
4. **Sound was not judged.** Nobody listened; the dump's own DirectSound wrapper is not used.
5. **The 32-bit GStreamer comes from a Proton build that happens to be on this machine**, through three
   host-specific paths in the user layer (§2.2). That is not a product arrangement.
6. **The AMD fix** TeknoParrot offers for this title ("invisible enemies") has not been looked at; the
   attract loop shows monsters in the play demos, which are movies.
7. **An independent capture** by the coordinator had not been reported when this was written.

---

## 2. The changes that got it there

| # | wall | what removed it | run |
|---|---|---|---|
| 1 | "LOW CONTROL ERROR 5-1500-0011 — PC and an I/O board cannot be communicated." | our own `shaiolib.dll` in place of Konami's | 2 → 3 |
| 2 | the game exits cleanly 37 s after start, right after the caution screen | a 32-bit GStreamer for Wine's DirectShow | 3/4 → 5 |
| 3 | the movie area stays black | a 3-byte in-memory patch that removes one call | 5 → 6 |

### 2.1 The I/O library

**MEASURED**, run 2 (23:37): on our core as it stands the game starts and shows the error above
(`shots/run2-233800-low-control-error.jpg`). Konami's `shaiolib.dll` opens COM1 and COM2 and waits for
two boards and the security plug.

Our replacement exports the same 52 names (`objdump -p` of both: identical names, order and ordinals).
**MEASURED**, run 3 (23:45): its log shows it loaded and every boot call arriving; the game's self-test
screen shows **OK on I/O BOARD, GUN I/O, SECURITY, SYSTEM DATA1, SYSTEM DATA2, DATA1, DATA2, DATA3,
CLOCK and SPEAKER**, then the warning and caution screens
(`shots/run3-zelftest-ok-waarschuwing-caution.jpg`). The bookkeeping files were created by the game
itself under `Data\eng\TeknoParrot\E|F\KSHG\KACBK` — no TEST press was needed.

### 2.2 The movies

**MEASURED**, run 4 (same as run 3 with Wine's DirectShow trace on): the game calls
`RenderFile("../eng/Movie/sceneA_ENGLISH.vid")`; Wine fails to create every splitter it has
("GStreamer splitter filter", "MPEG-I Stream Splitter", "AVI Splitter", "Wave Parser", each
`hr 0x8007000e`), `RenderFile` returns `0x80040218`, and the game exits with code 0. Those filters live
in Wine's 32-bit `winegstreamer.so`, which needs a 32-bit GStreamer; this host has none installed.

GE-Proton 10-34 ships one (`files/lib/i386-linux-gnu`, 134 plugins, with the MPEG-2 parser and the
libav decoder). Its folder holds no core system libraries; with it on the library path every dependency
resolves except `libvpx.so.6`, which was copied from the Steam runtime into
`~/.local/share/ravengun/runtime/gst32/`. In the user layer (`harness/gebruikerslaag-game.ini`):

```
[Env]
LD_LIBRARY_PATH = <GE-Proton10-34>/files/lib/i386-linux-gnu:<runtime>/gst32
GST_PLUGIN_SYSTEM_PATH_1_0 = <GE-Proton10-34>/files/lib/i386-linux-gnu/gstreamer-1.0
GST_REGISTRY_1_0 = ~/.cache/ravengun/gst-registry-silenthill-i386.bin
```

**MEASURED**, run 5: `RenderFile` returns 0, the decoder reports "MPEG-2 Video (Main Profile)", the
game's renderer accepts RGB32 640×480, and the game no longer exits. The dump's `clvsd.ax` is not used.
Do **not** put the whole Steam runtime folder on the library path: it carries its own C library.

The same gap is what parked Lethal Enforcers 3 (`.m1v` movies) earlier; this may be the fix there too.
Not tried.

### 2.3 The black movie

**MEASURED**, run 5: the graph is built and frames are decoded, but the movie area is black
(`shots/run5-film-zwart.jpg`). The trace shows why: the game calls `StopWhenReady` right after
`RenderFile` (thread 0198) and then `Run` from its main thread (00fc). Wine implements `StopWhenReady`
as "pause now, and stop from a worker thread once the pause has completed"; that worker (01b8) issues
its `Stop` **after** the game's `Run`. The allocator is decommitted, the delivery thread logs
`Failed to get a sample, hr 0x80040211` and exits, and nothing runs the graph again. On Windows the
game evidently wins this race.

The call is at `0x401ebf` in `KSHG.exe`: `ff 52 3c` (`call [edx+0x3c]`, slot 15 of `IMediaControl`),
and its return value is not used. `dropin/rg-shaiolib.patches` replaces it with `83 c4 04`
(`add esp,4`, dropping the pushed argument), so the graph stays stopped until the game's own `Run`.
The patch is applied in memory by our `shaiolib.dll` at load, only if the expected bytes are there —
in memory because the game checks `KSHG.exe` on disk at boot.

**MEASURED**, run 6: log "patch at 0x00401ebf: applied, 3 byte(s)"; the trace shows `RenderFile`, `Run`,
the delivery thread starting, no stop, and 3,737 decoder buffer lines; the movie is on screen
(`shots/run6-film-speelt.jpg`).

---

## 3. Reproducing run 7

```
# once: the title, a view of links to the share, and the folders the game writes to made local
python3 -I scripts/tp-add-game.py SilentHill "<share>/Silent Hill The Arcade/KSHG.exe" --view --gamescope
bash harness/sh-view.sh
# user layer ~/.local/share/ravengun/patches/SilentHill/game.ini: the [Env] block of §2.2
# the run (harness/sh-run.sh wraps this in scripts/rg-tv-env.sh, a capture loop, and a wrapper that makes the
# game's folder on the share READ-ONLY for the whole process tree)
python3 scripts/launch-direct.py SilentHill --run
```

`game.ini` here selects our own core (`[Loader] Core = ravengun`) and the drop-in puts `shaiolib.dll`
and `rg-shaiolib.patches` into the view.

**Checksums** (`runs/run7-SHA256SUMS.txt`; the DLL and the patch list in the run were byte-identical to
`dropin/`, and the settings file in the view was still identical to the share's):

| piece | sha256 |
|---|---|
| `KSHG.exe` | `9b8a6c1bf81f9d69…` |
| our core `OpenParrotWin32/OpenParrot.dll` | `413027dece3c32218e3c2a9b5965b5d79a4b8a36099df81745a473010c2faafc` |
| `OpenParrotWin32/OpenParrotLoader.exe` | `e6aa9a73e852780e…` |
| `dropin/shaiolib.dll` (rebuild from `shaiolib_rg.c` is byte-identical) | `02468795cae29bb2baa48159d5ebe496b11a85fd08c34db414b836df762b2ff4` |
| `dropin/rg-shaiolib.patches` | `b9c55a28ae973b8e…` |
| Wine `proton-11.0-2/bin/wine` | `3731a5030f97fcbe…` |
| `scripts/launch-direct.py` (working tree) | `66b54659984e51ce…` |

**Frames:** `runs/run7-opnames-index.txt` lists all 75 captures with time, hash and size.
`shots/run7-contactvel.jpg` (twelve frames, 23:56:42 … 00:08:57) and
`shots/run7-contactvel-eerste-5min.jpg` (twelve, 23:56:42 … 00:02:03) were looked at.

---

## 4. What was loaded, and where the instruments are blind

* **MEASURED** on the live game process (`runs/run7-LAADBEWIJS.txt`, `/proc/<pid>/maps`): at 23:56:13
  our `shaiolib.dll` and our core `OpenParrotWin32/OpenParrot.dll` are mapped and TeknoParrot's
  `TeknoParrot.dll` is not; at 23:57:30, during the movie, `winegstreamer.so`, GE-Proton's 32-bit
  `libgstreamer`, `libgstlibav.so`, `gst32/libvpx` and `quartz.dll` are mapped too.
* `runs/run7-rg-shaiolib.log` is the library's own record: the load line, the patch line, the first
  call of each export, and counters every two seconds (23,051 `BUTTON_status` calls in the run).
* Run 7 ran with `WINEDEBUG=-all`: there is no Wine trace of that run. The DirectShow facts in §2 come
  from runs 4, 5 and 6, which had the trace on and were otherwise the same.
* The first run of the series (run 1, 23:37:02) proves nothing about the game: my read-only wrapper
  broke gamescope's X socket check and the loader exited in two seconds. Fixed in the wrapper.

## 5. The share

The game's folder on the share was mounted read-only for every run (`harness/sh-inner.sh`; a write
attempt inside the wrapper fails with "Read-only file system"). What the game writes went to the local
folders of its view: `rg-shaiolib.log`, six Konami `*.log` files, `teknoparrot.ini`, and the
bookkeeping under `Data/eng/TeknoParrot/`. `setting.1` and `gun.1` were not rewritten.
