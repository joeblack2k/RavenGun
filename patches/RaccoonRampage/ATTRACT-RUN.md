# ATTRACT-RUN — Raccoon Rampage runs its attract loop on our own core

**Date:** 2026-10-14, 00:16 – 01:27. **Game:** `RSGame/Binaries/Win64/RSGame-Win64-Shipping.exe`,
66,510,336 B, PE32+ x86-64, Unreal Engine 4.26 (version resource 4.26.0.0), sha256 `118b8335…153a`; UNIS.
**Core:** our own 64-bit OpenParrot build (`OpenParrotx64/OpenParrot64.dll`, sha256 `fe2ecf8c…5794`) on its
generic route — it has no code for this title — started by `scripts/launch-direct.py RaccoonRampage --run`
under Wine (`proton-11.0-2`) with DXVK, as a 1920×1080 fullscreen window on the TV (no gamescope).
**Labels:** **MEASURED** = a log line, a file comparison, a `/proc/<pid>/maps` listing, or a frame that was
looked at; **DERIVED** = a conclusion drawn from measurements; **DATED** = a claim from earlier notes that
was not re-measured tonight.
**Evidence:** `bewijs-attract-20261014/` (`harness/`, `runs/`, `shots/`).

The claim in this file is made on **our core**, and it rests on **run rr10**. TeknoParrot's closed core
appears only as the reference in §4, to find out what differs.

---

## 1. Result, and what is NOT shown

**Shown.** The game starts and runs its attract loop — UNIS logo, title, splash art and the play-demo
films, with the four player HUDs — for thirteen minutes, with its own saved settings loaded.

| run | core | game folder on the share | window | length | how it ended |
|---|---|---|---|---|---|
| **rr10** | **ours** | writable throw-away layer | 01:13:35 (first game frame) – 01:26:25 (last capture) | **12 min 50 s** | the run's own limit (780 s, rc 124) at 01:26:27 |
| rr3 | ours | read-only | 00:22:07 – 00:34:58 | 12 min 51 s | the run's own limit (780 s) |
| rr9 | ours | writable throw-away layer | 00:55:10 – 00:57:18 | 2 min | the run's own limit (150 s) |
| rr2 | ours | read-only | about 00:19:45 – 00:21:29 | 1 min 45 s | the run's own limit (120 s) |

**rr10 in detail (MEASURED).** Started 01:13:25. Splash art on the capture of 01:13:35; HUD and banner
from 01:13:46; play-demo film from 01:14:06. Counted from 01:13:46 the five-minute mark is 01:18:46;
the run went on for 7 min 39 s past it and was stopped by its own limit, not by the game.
72 captures, one every 10 s (`runs/rr10-opnames-index.txt`: time, hash, size). 71 distinct; the one
repeat is a still of the loop caught twice 7 min 48 s apart (01:16:43 and 01:24:31), with different
frames in between. Four capture attempts wrote no file (about 01:22:58, 01:23:18, 01:24:42, 01:25:02);
the run's record has the game process alive at 01:23:08, 01:23:38, 01:24:38 and 01:25:09.
Looked at: `shots/rr10-sheet-start.jpg` (01:13:25 … 01:14:06), `rr10-sheet-a.jpg` (eleven frames,
01:13:35 … 01:18:48), `rr10-sheet-mark.jpg` (twelve, 01:18:17 … 01:20:11, across the five-minute mark),
`rr10-sheet-whole.jpg` (twelve across the run), `rr10-sheet-end.jpg` (the last twelve, to 01:26:25).
Single frames: `shots/rr10-shot-003-011346.jpg`, `-032-011848.jpg`, `-076-012625.jpg`.

**The full-resolution captures of rr10 no longer exist.** I deleted them from the session's scratch
folder in /tmp at about 01:30, after the coordinator's confirmation and before the instruction to keep
such captures in the game's folder. What is left of rr10's pictures is in this folder: five contact
sheets, three frames at 1280 pixels wide, and the index with every capture's time, hash and size.

**Independently witnessed.** The coordinator recorded the same run from their side: 01:13:37 – 01:26:29,
12 min 52 s, loaded by our own core; 27 substantive frames out of 38 samples (eleven samples failed to
capture — real frames appear again after each gap, including at 01:25:02), and the frame they looked at
shows the attract scene, "Please Insert Coin" three times with 1/6, 1/6 and 2/6, the "Press to start"
slot (read by them as "Dress to start" in the game's lettering), and
the red banner. Their record is theirs; it is not in this folder.

**THE OPEN ITEM OF THIS TITLE, named by the game itself:**

> **"The guns are not calibrated, please enter Setting Menu to calibrate!"** — a red banner across the
> top of every frame on our core.

It is the difference between "it demos" and "it plays", and it has the same shape as Jurassic Park's
"RIO Disconnected" and Silent Hill's missing I/O: the game runs and says which piece of cabinet hardware
it cannot find. Here that is the gun board behind `UsbPluginsDll.dll`. On the closed core, with
everything else the same, the banner is not there (§4).

**NOT shown — the other open items, stated plainly:**

1. **The banner above**, and with it the guns: no aiming, no trigger.
2. **No inputs.** MEASURED (§4): a TEST press written into the daemon's shared input state does nothing
   on our core; the same press opens the game's menu within a second on the closed core. The game
   **demos; it is not playable on our core.**
3. **Nothing has been played**, and no gun has been tried.
4. **Sound was not judged.** Nobody listened.
5. **Coins: only partly determined.** Read-only folder: two coin presses (key 5) changed nothing, on
   either core. Writable layer: player 1 already has saved credits, so the HUD cannot show an added
   coin. On the closed core the game booked the START press as a coin for player 1 (§9).
6. On the closed core — the route the owner plays — START, aiming and the trigger were measured later
   the same night (run or1, §9): the game was played there and the score rose to 17,936.

---

## 2. What rr10's own record says

`runs/rr10.run` is written by the harness during the run (`harness/win-run.sh`, `win-inner.sh`,
`win-watch.sh`), not afterwards. It exists so this run cannot be mistaken for a reference run on the
closed core.

```
RUN rr10 -- RaccoonRampage -- started 2026-10-14 01:13:25
DECLARED: core=ravengun  share=overlay  shm-source=launcher default  limit=780s  captures every 10s
launcher plan (scripts/launch-direct.py RaccoonRampage --plan):
    core_dll: …/openparrot/current/OpenParrotx64/OpenParrot64.dll
    core_sha256: fe2ecf8c2c8844bd7abb3af616271a36364028453c2885fc92d61c9ffd155794
    core_source: ravengun
inside the sandbox, the share folder is mounted as: overlay rw,…,lowerdir=/proc/self/fd/8,upperdir=/tmp-overlay-upper-0,…
game process first seen 01:13:27
launcher says: [launch-direct 01:13:25] kern (ravengun): …/OpenParrotx64/OpenParrot64.dll sha256 fe2ecf8c…5794
01:13:37 alive: pid 1853664, 127 threads; core DLL mapped: …/openparrot/fc8ba632…/OpenParrotx64/OpenParrot64.dll
   … one line every 30 s, 26 in all, the last at 01:26:09 …
ENDED 2026-10-14 01:26:29 -- launcher: 01:26:27 klaar (exitcode gestopt) -- rc=124
files in the share's game folder newer than the start of this run: 0
captures: 72, first shot-001-011325.png, last shot-076-012625.png
```

* **The declaration is enforced.** The harness reads the launcher's plan before starting and refuses the
  run if the plan's core is not the declared one. `runs/guardtest.run` is that refusal, taken at 01:13:13
  while the settings still selected the closed core: "REFUSED: the plan's core_source is 'runtime' but
  this run was declared as 'ravengun' -- not started".
* **All 26 samples name our core**, by full path, in the live game process; none names another core, and
  none found the process gone.
* The record's "left running afterwards" lists one process: the harness's own shell, matched by its
  command line. No game, Wine or helper process was left.

---

## 3. Run by run: what changed, and what each run showed

No run of this title crashed tonight. What "failed" was narrower each time, and each line below changes
one thing against the run it names.

| run | start | against | the one change | what it showed |
|---|---|---|---|---|
| rr1 | 00:16:44 | the shipped setup | `[Loader] Core = ravengun` in the user layer | starts and stays up on our core; **white background where the films belong**; banner; HUD 0/6 ×4 |
| rr2 | 00:19:34 | rr1 | the real `Movies` folder in the view **and** `[Wine] GStreamerPlugins` (the two belong together) | **the films play**; no exit |
| rr3 | 00:21:57 | rr2 | nothing; 780 s instead of 120 s | 12 min 51 s of attract — but with the read-only folder (see rr8) |
| rr4 | 00:37:26 | rr3 | the closed core | no banner; films play there too. Invalid for inputs: my wrapper lacked `--shm-source` |
| rr5 | 00:41:07 | rr4 | input bridge wired (`--shm-source`), two coin presses | presses reach the shared state (byte 8 = `04`); HUD unchanged |
| rr6 | 00:44:33 | rr5 | a TEST press added; captures every 4 s | **closed core: the menu opens** within 1 s |
| rr7 | 00:47:26 | rr6 | our core | **ours: nothing happens**; banner |
| rr8 | 00:51:37 | rr6 | writable throw-away layer instead of read-only | **the game now loads its saved settings** (HUD: Press to start / 1 / 1 / 2); menu opens |
| rr9 | 00:54:53 | rr7 | writable throw-away layer | settings loaded; **banner stays; TEST does nothing** |
| **rr10** | 01:13:25 | rr3 | writable throw-away layer; the run writes its own record | **12 min 50 s of attract on our core with the saved settings loaded** |

### The list — what this title needs to do this again

1. **The films' folder must be the real one.** The view's `RSGame/Content/Movies` is a link to the
   share's folder (8 `.mp4` files, 292 MB; MPEG-4 Part 2 video with AAC audio). An empty folder there
   (`tp-add-game.py --empty-dir`, the 2026-10-11 workaround) gives the white background of rr1.
2. **`[Wine] GStreamerPlugins = ~/.local/share/ravengun/tools/gst-extra/plugins`** — now in `game.ini`.
   The folder holds the 64-bit `libgstlibav.so` (`scripts/fetch-gst-wmv.sh` fetches it). With the films
   present and without it the game exits with code 1 (**DATED**, 2026-10-11; not re-measured).
3. **DXVK**: `[Wine] Prefix = ravengun-test-dxvk`, `DllOverrides = d3d11,d3d10core,dxgi=n,b`,
   `Runner = wrapper` — in `game.ini` since 2026-10-11 and used by every run tonight. That Wine's own
   Direct3D is not enough is **DATED**.
4. **The loader's default start route** (`TP_REMOTETHREAD=off`, from the profile's old `.env`; in the
   plan of every run tonight). **DATED** reason: with the post-start remote thread no input arrives.
5. **`[Loader] Core = ravengun`** selects our core. It is a user-layer line
   (`~/.local/share/ravengun/patches/RaccoonRampage/game.ini`), not in `game.ini`: the shipped default
   stays the closed core, on which the owner plays this title. The user layer is also read by the
   installed app, so the line is there only for the length of a run.
6. **The game must be able to write next to its executable.** With a read-only folder it does not load
   `Setting_*.ini` at all. On the bench that is the throw-away layer (`WIN_SHARE=overlay`); nothing
   reaches the share.
7. **No inputs are to be expected on our core** until §4.2 is built.

Nothing else was changed: no patch, no drop-in, no change to the loader or the core.

---

## 4. The comparison with the closed core

Same game files, same view, same Wine and prefix, same films, same input bridge, same key presses. The
only difference inside each pair is the core.

| run | core | game folder | banner | saved coins in the HUD | TEST press |
|---|---|---|---|---|---|
| rr6 | closed | read-only | **no** | no (0/6 ×4) | **menu opens** within 1 s |
| rr7 | **ours** | read-only | **yes** | no (0/6 ×4) | **nothing** |
| rr8 | closed | writable throw-away layer | **no** | yes | **menu opens** |
| rr9 | **ours** | writable throw-away layer | **yes** | yes | **nothing** |

Sheets: `shots/rr6-sheet.jpg`, `rr7-sheet.jpg`, `rr8-sheet.jpg`, `rr9-sheet.jpg`. Presses and the bytes
they produced: `runs/rr*-probe.txt`. What was mapped: `runs/rr*-paths.txt`.

### 4.1 Inputs

The daemon's bridge (`ravengunctl iobridge start RaccoonRampage --route tp-shm`) writes the 64-byte
board state to `/dev/shm/ravengun_JvsState`; the launcher's helper mirrors it into the Windows mapping
`TeknoParrot_JvsState`. **MEASURED** in every probe: byte 8 is `04` while key 5 (coin) is held and `01`
while key 0 (TEST) is held, and `00` before and after.

* Closed core, rr6: TEST down at 00:45:56, the game's "Homepage" menu on the capture of 00:45:57.
  rr8: TEST down at 00:53:03, menu on the capture of 00:53:06.
* Our core, rr7: TEST down at 00:48:51 — the attract loop carries on (captures to 00:49:12).
  rr9: TEST down at 00:56:17 — the loop carries on (captures to 00:57:18).

**DERIVED:** the game does not read `TeknoParrot_JvsState` itself; the closed core serves the game's I/O
from it. That answers, for this title, the question `docs/research/eigen-kern-hooks-plan.md` left open
("who reads the mapping: the game, or the closed core?").

### 4.2 What is missing on our side

* **Inputs.** The game talks to its cabinet through its own plug-in, `RSGame/Plugins/UsbPluginsDll.dll`
  (188,416 B, 107 exported `BIND_*` functions; list in `bewijs-attract-20261014/usbplugins-exports.txt`):
  `BIND_OpenGunDevice`, `BIND_GetGunData`, `BIND_EnterCalibrate`, `BIND_InitUnisIo`, `BIND_GetIoData`,
  `BIND_IsIoWorking`, `BIND_GetCoinNumByPlayer`, `BIND_OpenESIODevice`, `BIND_InitDongle`,
  `BIND_IsDongleExist`, … Something has to answer those from the daemon's state: either code in our
  core for this executable (CRC `0x71e52ec0`, not in our core's table — `patches/EIGEN-KERN.md`), or a
  replacement plug-in with the same exports (the way Silent Hill's `shaiolib.dll` was done). Which of the
  two the closed core does was **not determined**.
* **The banner.** What the game checks was **not determined**. It is not the saved calibration alone:
  in rr9 and rr10 the game had loaded its settings (the saved coins show) and the banner stayed.
  Candidates: the gun-device state the plug-in reports, or the per-player flags
  `GunCalibrate_bIsHandleVerify_*` in `Setting_Hardware.ini` (1 for player 1, 0 for players 2–4).

### 4.3 Two mistakes in my own harness, and what they showed

* **rr4 and the input route.** My wrapper called the launcher without `--shm-source`, so its helper
  mirrored the launcher's default file and not the one the bridge writes
  (`scripts/play-halo.sh` passes it). rr4 is valid for the banner and the films; it says nothing about
  inputs. rr5 onward have it (`harness/win-inner.sh`, `WIN_SHM_SOURCE`).
* **The read-only folder changes the game.** rr1–rr7 mounted the game's folder on the share read-only.
  **MEASURED**, rr8 against rr6: with a writable throw-away layer instead (overlayfs, writes go to a
  tmpfs and are gone after the run) the HUD shows "Press to start / 1 / 1 / 2" from the first frame —
  the counts saved in `Setting_System.ini` (`CoinNum_1P_Test=22`, `2P=1`, `3P=1`, `4P=2`) — where the
  read-only runs show 0/6 four times (`shots/rr8-hud-early.jpg`). That is why rr3, although it ran for
  thirteen minutes, is not the run the claim rests on, and why rr10 was run.

---

## 5. Notes elsewhere that these runs correct

* `game.ini`, `[Status]`: "runs … without intro videos" and `[Input]`: "empty Movies folder". The films
  play, on both cores (rr2, rr3, rr4, rr10). Both notes are rewritten, and `[Wine] GStreamerPlugins` is
  now in `game.ini`.
* `README.md` (was generated): "what blocks a start without TeknoParrot: the closed core … solved through
  the test menu (calibration in the game's save data)". The game starts without the closed core; the
  banner is there on our core with the saved settings loaded (rr9, rr10). Rewritten.
* The archive page (`findings/RaccoonRampage.md`, on the share, not changed): "coin (key 5) works" was
  neither reproduced nor disproved (§1, item 5).

---

## 6. What was loaded, and where the instruments are blind

* **rr10:** §2 — 26 samples of the live game process, each with the full path of the core DLL.
* **rr3**, pid 1819410 (`runs/rr3-paths.txt`, 00:30:41, the wider listing): our core from
  `openparrot/fc8ba632…/OpenParrotx64/OpenParrot64.dll`; **no** `TeknoParrot64.dll`; the game's own
  `UsbPluginsDll.dll`, `DewoDll.dll`, `IniFile.dll` and `SQLite.dll` from the share; `d3d11.dll` and
  `dxgi.dll` from the DXVK prefix; `winegstreamer`, `gst-extra/plugins/libgstlibav.so`, `mf.dll`,
  `mfplat.dll`, `mfreadwrite.dll`. For rr10 the wider listing was not taken; its pieces have the same
  checksums as rr3's (`runs/rr10-SHA256SUMS.txt` against `runs/rr3-SHA256SUMS.txt`).
* Reference runs: `TeknoParrot/TeknoParrot64.dll` mapped and `OpenParrot64.dll` not
  (`runs/rr4-paths.txt`, `rr5-`, `rr6-`, `rr8-`). Ours again in `rr7-` and `rr9-paths.txt`.
* **Blind spots.** Every run had `WINEDEBUG=-all`: there is no Wine trace. The game is a shipping build
  and wrote no log of its own. Nothing was recorded inside the helper processes. The loader's banner
  prints a loader path under the launcher's working directory that does not exist (version 0.0.0.0);
  the loader that was started is the one in the launcher's `start:` line (`runs/rr10.out`).

## 7. Reproducing rr10

```
# 1. the view: RSGame/Content/Movies is a link to the share's Movies folder (not an empty folder)
# 2. user layer, for the run only: ~/.local/share/ravengun/patches/RaccoonRampage/game.ini
#      [Loader]
#      Core = ravengun
# 3. the run -- as run, from bewijs-attract-20261014/harness/:
WIN_EXPECT_CORE=ravengun WIN_SHARE=overlay SHOTSECS=10 \
    bash win-run.sh RaccoonRampage "Raccoon Rampage" RSGame-Win64-Sh rr10 780
#    which is, inside scripts/rg-tv-env.sh and a sandbox that lays the throw-away layer over the game's folder:
python3 scripts/launch-direct.py RaccoonRampage --run
# 4. take the [Loader] line out of the user layer again
```

The harness scripts carry the path of the session's scratch folder; change `O=` to use them elsewhere.

**Checksums** (`runs/rr10-SHA256SUMS.txt`):

| piece | sha256 |
|---|---|
| `RSGame-Win64-Shipping.exe` | `118b8335977ba3706e7d7745eade8e65e0a2b35adf852217c6be8cf4e7a9153a` |
| our core `OpenParrotx64/OpenParrot64.dll` | `fe2ecf8c2c8844bd7abb3af616271a36364028453c2885fc92d61c9ffd155794` |
| the loader `OpenParrotx64/OpenParrotLoader64.exe` | `1953d91ac9db844d3602d2da8f5de700bbebdd8be79697b3ad287a55d1c6c017` |
| `UsbPluginsDll.dll` (the game's own, as in the dump) | `b130f882493fcef5…` |
| Wine `proton-11.0-2/bin/wine` | `3731a5030f97fcbe…` |
| `gst-extra/plugins/libgstlibav.so` | `0d0c42008a0e9b1a…` |
| `scripts/launch-direct.py` (working tree, commit 8286b6a plus changes) | `66b54659984e51ce…` |
| the closed core of the reference runs only, `TeknoParrot64.dll` | `f5c7532d6e638485…` |

## 8. The share, and the dump

* Nothing was written to the share. rr1–rr7: read-only mount. rr8–rr10: the throw-away layer; a probe
  file written inside it was not on the share afterwards, rr10's record counts 0 files newer than its
  start, and no file in the game's `Binaries`, `Plugins` or `Movies` folders is newer than 2026-10-12.
* The dump's `UsbPluginsDll.dll` is **not** the vendor's file: it differs from the
  `UsbPluginsDll.dll.original` next to it (sha256 `b5a756f5…`) in eight bytes at three places — file
  offset `0xcc80` (`ba 00 c2 01` → `e9 63 02 00`, a jump where a `mov edx,115200` was), `0x14dea`
  (`74 1c` → `eb 14`) and `0x15127` (`74 20` → `eb 1d`). What they skip was not determined.
* Strings in the plug-in (**MEASURED**): `HID\Vid_0471&Pid_485e` (the guns), `\\.\COM%d` and
  `CComPort::OpenESIO Com(%d)` (a serial board), `\\.\BeaconMS_USBfilter` (a driver device), classes
  `CUnisIo`, `CAmusementParkIO`, `CZlbDev`, `CCoDongle2`.

---

## 9. The two routes — which one the owner is handed, and what it would take to switch

`game.ini` leaves the core at its default, and that is what the app starts. That default is **not
ours**: our launcher and our loader executable start it, but the core is TeknoParrot's closed
`TeknoParrot64.dll`. It cannot count for the bar. It is the game the owner actually plays, so it was run
tonight, past five minutes and with a played part, to prove that it still works with the films restored.

| | the route in `game.ini` (what the app starts) | our own core |
|---|---|---|
| what differs | core `TeknoParrot/TeknoParrot64.dll` (`f5c7532d…`) | core `OpenParrotx64/OpenParrot64.dll` (`fe2ecf8c…`), selected by `[Loader] Core = ravengun` |
| run | **or1**, 02:18:34 – 02:26:36 (own limit, 480 s) | **rr10**, 01:13:25 – 01:26:27 |
| attract, untouched | 02:18:55 (splash) – 02:24:02: **5 min 07 s**, films playing | 12 min 50 s, films playing |
| "The guns are not calibrated" | **not shown** | on every frame |
| START (key 1, through the daemon's bridge) | 02:24:02 → player 1 joins: "Assign Cards", "Select Map", "Tips"; the game writes `Setting_System.ini` in that second (`CoinNum_1P` 0 → 1) | nothing arrives (TEST measured, rr7/rr9) |
| aiming and trigger | five aimed shots 02:24:16 – 02:24:34; crosshair at the aimed points; score 1,018 → 17,936 | not possible |
| counts for the bar | **no** | **yes** (attract only) |

Evidence for or1: `runs/or1.run` (16 samples, each naming `TeknoParrot/TeknoParrot64.dll`, none naming
our core), `runs/or1-play.txt`, `runs/or1-wat-de-game-schreef.txt`, `shots/or1-sheet-attract.jpg`,
`or1-sheet-play.jpg`, `or1-sheet-edges.jpg`, and the 47 captures in `opnames-or1/`.

**How or1 differs from the owner's own start.** Same launcher command with the bridge started first, but
inside the bench wrapper with a kept write layer over the share; his start writes the game's
`Setting_*.ini` to the share itself. Nothing was written to the share in or1 — so the credit that or1
used is not missing from his saved game.

**Which is the better answer.** Our own core — no closed component — **but it should not replace the
closed core yet**: on ours the game demos with the banner and takes no input. What it takes to switch:

1. **Inputs on our core.** The game talks to its cabinet through `RSGame/Plugins/UsbPluginsDll.dll`
   (107 `BIND_*` functions) and does not read the daemon's shared state itself (§4.1). Our core has to
   answer those functions from that state — by hooks for this executable, or by a replacement plug-in
   with the same exports (the way Silent Hill's I/O library was done).
2. **The calibration state.** What makes the banner go away on the closed core is not determined; it
   is part of the same interface (`BIND_OpenGunDevice`, `BIND_GetGunData`, `BIND_EnterCalibrate`).
3. **Then the proof**: five minutes of play on our core, on the TV — the same test or1 passed — and
   only then `[Loader] Core = ravengun` in `game.ini`.

**Coins, now determined on this route:** the START press was booked by the game as a coin for player 1
(`CoinNum_1P` 0 → 1 in the kept layer). Whether key 5 does the same was not repeated.
