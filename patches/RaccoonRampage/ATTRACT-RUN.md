# ATTRACT-RUN — Raccoon Rampage runs its attract loop on our own core

**Date:** 2026-10-14, 00:16 – 00:58. **Game:** `RSGame/Binaries/Win64/RSGame-Win64-Shipping.exe`,
66,510,336 B, PE32+ x86-64, Unreal Engine 4.26 (version resource 4.26.0.0), sha256 `118b8335…153a`; UNIS.
**Core:** our own 64-bit OpenParrot build (`OpenParrotx64/OpenParrot64.dll`, sha256 `fe2ecf8c…5794`) on its
generic route — it has no code for this title — started by `scripts/launch-direct.py RaccoonRampage --run`
under Wine (`proton-11.0-2`) with DXVK, as a 1920×1080 fullscreen window on the TV (no gamescope).
**Labels:** **MEASURED** = a log line, a file comparison, a `/proc/<pid>/maps` listing, or a frame that was
looked at; **DERIVED** = a conclusion drawn from measurements; **DATED** = a claim from earlier notes that
was not re-measured tonight.
**Evidence:** `bewijs-attract-20261014/` (`harness/`, `runs/`, `shots/`).

The claim in this file is made on **our core**. TeknoParrot's closed core appears only as the reference
in §3, to find out what differs.

---

## 1. Result, and what is NOT shown

**Shown.** The game starts and runs its attract loop: UNIS logo, title, splash art and the play-demo
film, with the four player HUDs ("Please Insert Coin — Coin: 0 / 6").

| run | window | length | how it ended | witnessed by |
|---|---|---|---|---|
| **rr3** | 00:22:07 (first movie frame) – 00:34:58 (last capture) | 12 min 51 s | the run's own limit (780 s, rc 124); nothing left running | the run's captures (73, all distinct; three contact sheets looked at) |
| rr2 | about 00:19:45 – 00:21:29 (last capture) | 1 min 45 s | the run's own limit (120 s) | 12 captures, sheet looked at |
| rr9 | 00:55:10 – 00:57:18 (last capture looked at) | 2 min | the run's own limit (150 s) | 34 captures; a sheet of sixteen looked at |

**NOT shown — open items, stated plainly:**

1. **A red banner is on every frame:** "The guns are not calibrated, please enter Setting Menu to
   calibrate!" On the closed core, with everything else the same, it is not there (§3).
2. **No inputs.** MEASURED (§3): a TEST press written into the daemon's shared input state does nothing
   on our core; the same press opens the game's menu within a second on the closed core. The game
   **demos; it is not playable on our core.**
3. **Nothing has been played**, and no gun has been tried.
4. **Sound was not judged.** Nobody listened.
5. **The witness run rr3 had the game's folder on the share mounted read-only, and in that mode the game
   does not load its own saved settings** (§3.3). In the faithful mode — a writable throw-away layer over
   the share — our core was run for 150 s only (rr9): the same loop, the same banner, and the saved coin
   counts in the HUD. Five minutes in that mode were not run.
6. **Coins: not determined.** Read-only: two coin presses changed nothing, on either core. Writable
   layer: player 1 already has saved credits, so the HUD cannot show an added coin.
7. On the closed core only TEST was re-measured tonight. That aiming, trigger and coin work there is
   **DATED** (2026-10-11).
8. **An independent capture** by the coordinator had not been reported when this was written.

---

## 2. What it took

No code. Two settings, and one dated belief that turned out not to hold.

| # | what was in the way | what removed it | run |
|---|---|---|---|
| 1 | the belief that this title needs the closed core to start | nothing: `[Loader] Core = ravengun` | rr1 |
| 2 | the attract background is plain white where the films belong | the real `Movies` folder in the view, and `[Wine] GStreamerPlugins` | rr1 → rr2 |

**MEASURED**, rr1 (00:16:44, 120 s; view as it was: `RSGame/Content/Movies` an empty local folder): the
game starts on our core and stays up; white background, the banner, the four HUDs
(`shots/rr1-sheet.jpg`).

The empty folder was a workaround from 2026-10-11: the films (`RSGame/Content/Movies/*.mp4`, 8 files,
292 MB; MPEG-4 Part 2 video with AAC audio) made the game exit with code 1 because this Wine had no
decoder for them (**DATED** — the exit was not re-measured). The 64-bit `libgstlibav.so` that
`scripts/fetch-gst-wmv.sh` puts in `~/.local/share/ravengun/tools/gst-extra/plugins` decodes them.

**MEASURED**, rr2 (00:19:34, 120 s; `Movies` in the view is a link to the share again, and
`[Wine] GStreamerPlugins` points at that folder): the films play and the game does not exit
(`shots/rr2-sheet.jpg`). Both things were changed between rr1 and rr2; the launcher's log does not print
the variable, the proof that the plug-in was in use is the process map of rr3 (§5).

---

## 3. The comparison with the closed core

Same game files, same view, same Wine and prefix, same films, same input bridge, same key presses. The
only difference inside each pair is the core.

| run | core | share | banner | saved coins in the HUD | TEST press |
|---|---|---|---|---|---|
| rr6 | closed | read-only | **no** | no (0/6 ×4) | **menu opens** within 1 s |
| rr7 | **ours** | read-only | **yes** | no (0/6 ×4) | **nothing** |
| rr8 | closed | writable throw-away layer | **no** | yes | **menu opens** |
| rr9 | **ours** | writable throw-away layer | **yes** | yes | **nothing** |

Sheets: `shots/rr6-sheet.jpg`, `rr7-sheet.jpg`, `rr8-sheet.jpg`, `rr9-sheet.jpg`. Presses and the bytes
they produced: `runs/rr*-probe.txt`. What was mapped: `runs/rr*-paths.txt`.

### 3.1 Inputs

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

### 3.2 What is missing on our side

* **Inputs.** The game talks to its cabinet through its own plug-in, `RSGame/Plugins/UsbPluginsDll.dll`
  (188,416 B, 107 exported `BIND_*` functions; list in `bewijs-attract-20261014/usbplugins-exports.txt`):
  `BIND_OpenGunDevice`, `BIND_GetGunData`, `BIND_EnterCalibrate`, `BIND_InitUnisIo`, `BIND_GetIoData`,
  `BIND_IsIoWorking`, `BIND_GetCoinNumByPlayer`, `BIND_OpenESIODevice`, `BIND_InitDongle`,
  `BIND_IsDongleExist`, … Something has to answer those from the daemon's state: either code in our
  core for this executable (CRC `0x71e52ec0`, not in our core's table — `patches/EIGEN-KERN.md`), or a
  replacement plug-in with the same exports (the way Silent Hill's `shaiolib.dll` was done). Which of the
  two the closed core does was **not determined**.
* **The banner.** What the game checks was **not determined**. It is not the saved calibration alone:
  in rr9 the game had loaded its settings (the saved coins show) and the banner stayed. Candidates: the
  gun-device state the plug-in reports, or the per-player flags `GunCalibrate_bIsHandleVerify_*` in
  `Setting_Hardware.ini` (1 for player 1, 0 for players 2–4).

### 3.3 Two mistakes in my own harness, and what they showed

* **rr4 and the input route.** My wrapper called the launcher without `--shm-source`, so its helper
  mirrored the launcher's default file and not the one the bridge writes
  (`scripts/play-halo.sh` passes it). rr4 is valid for the banner and the films; it says nothing about
  inputs. rr5 onward have it (`harness/win-inner.sh`, `WIN_SHM_SOURCE`).
* **The read-only share changes the game.** rr1–rr7 mounted the game's folder on the share read-only.
  **MEASURED**, rr8 against rr6: with a writable throw-away layer instead (overlayfs, writes go to a
  tmpfs and are gone after the run) the HUD shows "Press to start / 1 / 1 / 2" from the first frame —
  the counts saved in `Setting_System.ini` (`CoinNum_1P_Test=22`, `2P=1`, `3P=1`, `4P=2`) — where the
  read-only runs show 0/6 four times (`shots/rr8-hud-early.jpg`). So with a read-only folder the game
  does not load its saved settings. For Windows titles that keep their settings next to the executable,
  the bench wrapper should use the throw-away layer (`WIN_SHARE=overlay`).

---

## 4. Notes elsewhere that these runs correct

* `game.ini`, `[Status]`: "runs … without intro videos" and `[Input]`: "empty Movies folder". The films
  play, on both cores (rr2, rr3, rr4). Both notes are rewritten, and `[Wine] GStreamerPlugins` is now in
  `game.ini`.
* `README.md` (was generated): "what blocks a start without TeknoParrot: the closed core … solved through
  the test menu (calibration in the game's save data)". The game starts without the closed core; the
  banner is there on our core with the saved settings loaded (rr9). Rewritten.
* The archive page (`findings/RaccoonRampage.md`, on the share, not changed): "coin (key 5) works" was
  neither reproduced nor disproved (§1, item 6).

---

## 5. What was loaded, and where the instruments are blind

* **MEASURED** on the live game process of rr3, pid 1819410 (`runs/rr3-paths.txt`, `/proc/<pid>/maps`
  with full paths, 00:30:41): our core from
  `openparrot/fc8ba632…/OpenParrotx64/OpenParrot64.dll`; **no** `TeknoParrot64.dll`; the game's own
  `UsbPluginsDll.dll`, `DewoDll.dll`, `IniFile.dll` and `SQLite.dll` from the share; `d3d11.dll` and
  `dxgi.dll` from the DXVK prefix; `winegstreamer`, `gst-extra/plugins/libgstlibav.so`, `mf.dll`,
  `mfplat.dll`, `mfreadwrite.dll`. `runs/rr3-loaded.txt` (00:22:41) is the earlier, shorter listing; its
  pattern would also match TeknoParrot's own `OpenParrotx64/` folder, which is why the full paths were
  taken as well.
* Reference runs: `TeknoParrot/TeknoParrot64.dll` mapped and `OpenParrot64.dll` not
  (`runs/rr4-paths.txt`, `rr5-`, `rr6-`, `rr8-`). Ours again in `rr7-` and `rr9-paths.txt`.
* **Blind spots.** Every run had `WINEDEBUG=-all`: there is no Wine trace. The game is a shipping build
  and wrote no log of its own. Nothing was recorded inside the helper processes. The loader's banner
  prints a loader path under the launcher's working directory that does not exist (version 0.0.0.0);
  the loader that was started is the one in the launcher's `start:` line.
* Three capture attempts of rr3 (about 00:33:04, 00:33:25, 00:33:46) produced no file; the captures
  before and after show the loop running (`shots/rr3-sheet-end.jpg`).

## 6. Reproducing rr3

```
# user layer for the run: ~/.local/share/ravengun/patches/RaccoonRampage/game.ini
#   [Loader]
#   Core = ravengun
# the view: RSGame/Content/Movies must be the real folder (a link to the share), not an empty one
python3 scripts/launch-direct.py RaccoonRampage --run
# as run: harness/win-run.sh RaccoonRampage "Raccoon Rampage" RSGame-Win64-Sh rr3 780
```

`game.ini` here leaves the core at its default — the closed one, on which the owner plays this title.
Selecting our core is a user-layer line and was removed again after the runs; the user layer now only
holds the `GStreamerPlugins` key (the installed copy of this folder does not have it yet).

**Checksums:** `runs/rr3-SHA256SUMS.txt`.

| piece | sha256 |
|---|---|
| `RSGame-Win64-Shipping.exe` | `118b8335977ba3706e7d7745eade8e65e0a2b35adf852217c6be8cf4e7a9153a` |
| our core `OpenParrotx64/OpenParrot64.dll` | `fe2ecf8c2c8844bd7abb3af616271a36364028453c2885fc92d61c9ffd155794` |
| `OpenParrotx64/OpenParrotLoader64.exe` | `1953d91ac9db844d…` |
| `UsbPluginsDll.dll` (the game's own, as in the dump) | `b130f882493fcef5…` |
| Wine `proton-11.0-2/bin/wine` | `3731a5030f97fcbe…` |
| `gst-extra/plugins/libgstlibav.so` | `0d0c42008a0e9b1a…` |
| `scripts/launch-direct.py` (working tree) | `66b54659984e51ce…` |
| the closed core of the reference runs, `TeknoParrot64.dll` | `f5c7532d6e638485…` |

**Frames:** `runs/rr3-opnames-index.txt` lists all 73 captures with time, hash and size.
`shots/rr3-sheet-a.jpg` (twelve frames, 00:22:07 … 00:27:30), `rr3-sheet-whole.jpg` (twelve across the
run) and `rr3-sheet-end.jpg` (thirteen, 00:32:22 … 00:34:58) were looked at.

## 7. The share, and the dump

* Nothing was written to the share. rr1–rr7: read-only mount. rr8, rr9: the throw-away layer; a probe
  file written inside it was not on the share afterwards, and no file in the game's `Binaries`, `Plugins`
  or `Movies` folders is newer than 2026-10-12.
* The dump's `UsbPluginsDll.dll` is **not** the vendor's file: it differs from the
  `UsbPluginsDll.dll.original` next to it (sha256 `b5a756f5…`) in eight bytes at three places — file
  offset `0xcc80` (`ba 00 c2 01` → `e9 63 02 00`, a jump where a `mov edx,115200` was), `0x14dea`
  (`74 1c` → `eb 14`) and `0x15127` (`74 20` → `eb 1d`). What they skip was not determined.
* Strings in the plug-in (**MEASURED**): `HID\Vid_0471&Pid_485e` (the guns), `\\.\COM%d` and
  `CComPort::OpenESIO Com(%d)` (a serial board), `\\.\BeaconMS_USBfilter` (a driver device), classes
  `CUnisIo`, `CAmusementParkIO`, `CZlbDev`, `CCoDongle2`.
