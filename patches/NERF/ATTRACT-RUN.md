# ATTRACT-RUN — NERF Arcade runs its attract loop natively, on our own launcher

**Date:** 2026-10-14, 01:34 – 01:58. **Game:** `Nerf.x86_64`, 27,513,640 B, ELF 64-bit x86-64, a Unity
2018.2.21f1 player for Linux, sha256 `1466297f…5371` (game version file: "Nerf 1.55, OS 04.02c").
**Route:** native. The original Linux executable is started by our own launcher,
`scripts/play-native.sh --game NERF --gamescope`, in its sandbox. **No Wine, no TeknoParrot core, no
BepInEx, and none of TeknoParrot's plug-in** is in the process; nothing is emulated.
**Labels:** **MEASURED** = a log line, a file comparison, a `/proc/<pid>/maps` listing, or a frame that was
looked at; **DERIVED** = a conclusion drawn from measurements; **DATED** = a claim from earlier notes that
was not re-measured tonight.
**Evidence:** `bewijs-attract-20261014/` (`harness/`, `runs/`, `shots/`). **What the dump is:** §2.

---

## 1. Result, and what is NOT shown

**Shown.** The game runs its attract loop: Raw Thrills logo, the NERF ARCADE title, Hasbro and Raw
Thrills logos, the "Nerf Masters" score table, the epilepsy warning, and all four levels in turn
(graveyard, drone hangar, army base, zombie lagoon) with their targets moving, and "Free Play — Press
Start!" for player 1 and player 2.

| run | window | length | how it ended |
|---|---|---|---|
| **n5** | 01:44:34 (Raw Thrills logo) – 01:57:20 (last capture) | **12 min 46 s** | the run's own limit (780 s) at 01:57:25 |
| n3 | 01:39:59 (Raw Thrills logo) – 01:41:23 (last capture) | 1 min 24 s | the run's own limit (100 s) |

**n5 in detail (MEASURED).** Started 01:44:23. Logo on the capture of 01:44:34; first level with HUD and
banner from 01:44:44; the game's own log: "Game booted up" at 01:44:42. Counted from 01:44:44 the
five-minute mark is 01:49:44; the run went on for 7 min 36 s past it and was stopped by its own limit.
75 captures, one every 10 s, all distinct, none missing (`runs/n5-opnames-index.txt`: time, hash, size).
Looked at: `shots/n5-sheet-start.jpg` (01:44:23 … 01:45:37), `n5-sheet-mark.jpg` (twelve, 01:49:17 …
01:51:13, across the five-minute mark), `n5-sheet-whole.jpg` (thirteen across the run),
`n5-sheet-end.jpg` (the last twelve, to 01:57:20). Single frames: `shots/n5-shot-003-014444.jpg`,
`-033-014959.jpg`, `-075-015720.jpg`.

**THE OPEN ITEM OF THIS TITLE, named by the game itself:**

> **"RIO DISCONNECTED"** — in red at the top of every game frame.

RIO is Raw Thrills' I/O board (`librio.so`, `libRIO2.so` are loaded; there is no board). It is where
START, coin, TEST and — as far as is known — the guns come from. It is the difference between "it
demos" and "it plays", the same shape as Jurassic Park's "RIO Disconnected" and Raccoon Rampage's
"guns are not calibrated".

**NOT shown — the other open items, stated plainly:**

1. **No inputs were sent and none were tried.** Nothing was pressed in any run. Whether a keyboard key
   does anything in this build is not known.
2. **Nothing has been played**, and no gun has been tried.
3. **The dongle.** The game's own HASP client (`libhasp_linux_23714.so`) is loaded and finds no dongle;
   our dongle emulator was **not** used. In thirteen minutes the game did not complain. What the dongle
   is needed for was not determined; n4's log shows the game logging in to it from
   `TestPreferences.BuildDefaultPrefs()` and `AuditManager` (cabinet identity, default settings, coin
   audits). The settings the game used are the ones saved in the dump (`LocalData/testmenu/prefs`,
   free play). A cabinet without those files may behave differently.
4. **The particle plug-in is not loaded.** `Nerf_Data/Plugins/libPK-UnityPlugin.so` (PopcornFX) needs
   `libbfd-2.28-system.so`, which this host does not have ("Unable to preload"). Whatever effects it
   draws are missing; there is no reference picture to say which. It imports five `bfd_*` functions; a
   small stub library would let it load. Not tried.
5. `ScreenSelector.so` (Unity's resolution dialog, GTK 2) does not load either; nothing asks for it.
6. **Sound was not judged.** Nobody listened.
7. **Two things are passed on the command line, not by the profile:** `--gamescope`, and the bind of
   the rules file (§3, the list, points 3 and 4). The native launcher has no way yet to name a file in a
   game's own folder from its profile.
8. **An independent capture** by the coordinator had not been reported when this was written.

---

## 2. What the dump is — two games in one folder

The archive page (`findings/NERF.md`, 2026-10-11) and this folder's earlier notes describe only the
Windows side. **MEASURED** from the files on the share (`NERF Arcade/`, 1.7 GB):

| | the original | TeknoParrot's conversion |
|---|---|---|
| executable | `Nerf.x86_64` (ELF 64-bit; `ul` is a byte-identical copy) | `Nerf.exe` (PE32+, 648,704 B) + `UnityPlayer.dll` |
| runtime | `Nerf_Data/MonoBleedingEdge/x86_64/libmonobdwgc-2.0.so` | `MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll` |
| cabinet glue | `libUnityNatives.so`, `librio.so`, `libRIO2.so`, `libhasp_linux_23714.so` | `winhttp.dll` (Doorstop) → BepInEx 5.4.23.2 → `BepInEx/plugins/NERFTPPlugin.dll` |
| started by | `/pm/utils/go.sh` → `xinit /pm/utils/run_game.sh` (Raw Thrills OS) | TeknoParrot's closed core; the profile is subscriber-only (`Patreon = true`) |

* Both players are Unity **2018.2.21f1**, and so is the data (`globalgamemanagers`). The game's code,
  `Nerf_Data/Managed/Assembly-CSharp.dll`, is shared by both.
* The four Linux plug-ins are in the dump twice, byte-identical: `Plugins/x86_64/` and
  `Nerf_Data/Plugins/x86_64_ori/`. The folder the game needs, `Nerf_Data/Plugins/x86_64/`, is **not**
  there — it was renamed to `x86_64_ori`.
* The cabinet: game root `/pm`; `utils/default.cfg` says `GAME Nerf`, `DONGLE DXMYB`,
  `RESOLUTION 1920 1080`. `utils/run_game.sh` is not in the dump.
* What the game expects of the cabinet, from its own code and libraries: a HASP dongle
  (`DongleControl`, `CheckDongle`; "DONGLE NOT FOUND", "Dongle version number invalid. Terminating
  program."; `libUnityNatives.so` exports `HaspReadCabType`, `HaspReadCabTemplate`,
  `HaspReadCabSerialNum`, `HaspReadCountryCode`, `HaspReadDongleVersion`, `HaspReadLifetimeCoinCount`),
  the RIO boards ("Rio disconnected.", "Rio1 error in", "Rio2 error in"), a USB stick mount
  (`/mnt/usbflash`, the udev rules file), and `/pm/FlushBuffers.sh`, which it writes itself.

So the title is "ELF **and** Windows". The native route uses nothing of TeknoParrot's.

---

## 3. Run by run: what changed, and what each run showed

Each run changes one thing against the run before it.

| run | start | the one change | what it showed |
|---|---|---|---|
| n1 | 01:34:34 | the new `NERF` profile as it was: the ELF at `/pm`, the plug-ins also shown at `Nerf_Data/Plugins/x86_64` | the game starts, loads Mono and its plug-ins, then: **"Authorization required, but no authorization protocol specified"**; Unity logs "Desktop is 0 x 0". Nothing on the TV |
| n2 | 01:37:09 | `--gamescope` | a display; logo; then mostly empty grey screens under "RIO DISCONNECTED". The game's log: `UnauthorizedAccessException` on `/etc/udev/rules.d/10-usb-automount.rules` in `Sys_Linux.CreateUSBMountRules()`, called from **`Game.Awake()`, which is cut short**; then `Level.Update()` throws 7,940 times in 70 s |
| n3 | 01:39:46 | that rules file present (read-only) | **the full attract loop.** The log has three new lines and no exception |
| n4 | 01:42:14 | the plug-in mount at `Nerf_Data/Plugins/x86_64` taken out (I believed it redundant: Unity "preloads" from `x86_64_ori` by itself) | **wrong belief.** `DllNotFoundException: UnityNatives` and `: rio`, 329,446 log lines in 100 s; a broken HUD |
| **n5** | 01:44:23 | the mount back in; 780 s | **12 min 46 s of attract**, log quiet |

### The list — what this title needs to do this again

1. **The native profile** `NERF` in `packaging/native-games.json` (loader repository): the share's
   `NERF Arcade` folder at `/pm` with a local write layer, executable `/pm/Nerf.x86_64`, working
   directory and home `/pm`, no arguments, nothing preloaded.
2. **The plug-ins at `Nerf_Data/Plugins/x86_64`** — the profile's second mount shows the dump's
   `x86_64_ori` folder there. The game's managed code looks for `UnityNatives`, `rio`, `RIO2` and
   `hasp_linux_23714` only there (n4). A side effect: Unity preloads each plug-in twice, once per
   folder; both copies are mapped (`runs/n5.run`).
3. **`/etc/udev/rules.d/10-usb-automount.rules` must exist.** `data/udev-rules.d/` holds it (176 B,
   the two lines the game itself would write; identical to the dump's `utils/` copy). Bound read-only:
   `--bind-ro patches/NERF/data/udev-rules.d /etc/udev/rules.d`. Without it `Game.Awake()` dies on
   the exception and the game half-works (n2).
4. **`--gamescope`.** Without it the game cannot open the display in the sandbox (n1). Jurassic Park
   needed the same.
5. **The game's own saved settings** come from the dump (`LocalData/`); what the game writes
   (`LocalData/logs`, `/pm/FlushBuffers.sh`, Unity's `~/.config`) lands in the local write layer
   `~/.local/share/ravengun/native/NERF/0/upper/`.

Nothing else: no patch, no preload, no dongle emulation, no change to the launcher's code.

---

## 4. What n5's own record says

`runs/n5.run` is written by the harness during the run (`harness/n-run.sh`, `n-watch.sh`).

```
RUN n5 -- NERF Arcade -- started 2026-10-14 01:44:23
DECLARED: route=native (Nerf.x86_64 via scripts/play-native.sh --game NERF; no Wine, no TeknoParrot core, no BepInEx)  limit=780s  captures every 10s  gamescope=1
game process first seen 01:44:24
01:44:34 alive: pid 1878669, exe /pm/Nerf.x86_64, 58 threads; hasp mapped: /pm/Nerf_Data/Plugins/x86_64/libhasp_linux_23714.so /pm/Nerf_Data/Plugins/x86_64_ori/libhasp_linux_23714.so ; libRIO2.so librio.so libUnityNatives.so … ; Wine/TeknoParrot/OpenParrot/BepInEx mappings: 0
   … one line every 30 s, 26 in all, the last at 01:57:06 …
gamelog.txt: 1328 bytes, 30 lines
ENDED 2026-10-14 01:57:25
files in the share's game folder newer than the start of this run: 0
captures: 75, first shot-001-014423.png, last shot-075-015720.png
```

* **All 26 samples** show the executable `/pm/Nerf.x86_64`, the game's own HASP and RIO libraries, and
  **zero** mappings of Wine, TeknoParrot, OpenParrot, BepInEx, `winhttp` or `UnityPlayer.dll`. No sample
  found the process gone.
* The game's own log of the run has two new lines ("Game booted up.", "VO Volume set to 22"); Unity's
  log (`runs/n5-Player.log`, 57 lines) has no exception.
* **Blind spots.** Nothing was recorded about what the HASP client or the RIO libraries tried (no
  trace of their calls). The launcher's exit code is that of its own time limit.

## 5. Reproducing n5

```
cd <loader repository>
scripts/play-native.sh --game NERF --gamescope --seconds 780 \
    --bind-ro patches/NERF/data/udev-rules.d /etc/udev/rules.d
# as run (inside scripts/rg-tv-env.sh, with captures and the record), from bewijs-attract-20261014/harness/:
SHOTSECS=10 N_GAMESCOPE=1 bash n-run.sh n5 780 --bind-ro <patches>/NERF/data/udev-rules.d /etc/udev/rules.d
```

The harness scripts carry the path of the session's scratch folder; change `O=` to use them elsewhere.
The run records name the rules folder as `patches/NERF/dropin/udev-rules.d`: that is where it was during
the runs. It was moved to `data/udev-rules.d` afterwards (same file, same sha256), because `dropin/` is
what the Windows launcher places into a game's view, and this file has no business on that route.

**Checksums** (`runs/n5-SHA256SUMS.txt`):

| piece | sha256 |
|---|---|
| `Nerf.x86_64` | `1466297fa6b1eaf54dc27311641b906b65539b9db8122f83a836ab6fdc5b5371` |
| `Nerf_Data/Managed/Assembly-CSharp.dll` | `0d874b7ea94323e0…` |
| `libUnityNatives.so` / `librio.so` / `libRIO2.so` / `libhasp_linux_23714.so` | `87f6dc62…` / `a32fb2e6…` / `331510a4…` / `0eed25a2…` |
| our launcher `scripts/play-native.sh` (working tree, commit 8286b6a plus changes) | `e834a0a59f8539f43f2f8324b0214cb80d8746d8a6d0f69c1edbbc0a571f1cc6` |
| its data `packaging/native-games.json` (with the `NERF` profile), as run — the launcher's log names this sum. The profile's note text was edited after the run (the folder name of the rules file); the file is now `e78a5797…` | `5546ea4539f4a61b9b36f66f28610ca4b1b6f2b1e939cb4b887b93bbeb1fac5d` |
| `data/udev-rules.d/10-usb-automount.rules` | `797da63f32cbad13…` |
| `/usr/bin/gamescope` (3.16.30), `/usr/bin/bwrap` (0.12.0) | `f6058f8c…`, `dc057266…` |
| not used on this route: `Nerf.exe`, `NERFTPPlugin.dll` | `aea588d2…`, `7a7aa72d…` |

## 6. The share, and the route the owner plays

* Nothing was written to the share: the launcher shows it read-only under a local write layer, and each
  run's record counts 0 files newer than its start.
* `game.ini` is unchanged in what it selects: the Windows route on TeknoParrot's closed core with its
  plug-in, on which this title is playable (**DATED**, 2026-10-11; not started tonight). The native route
  is a second route next to it, reached only through `play-native.sh`.
