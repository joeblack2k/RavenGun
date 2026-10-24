# House of the Dead: Scarlet Dawn

*Sega ALLS UX, 2018, built in Unreal Engine 4.18. Both programs are 64-bit Windows programs. Our
route: Wine with the OpenParrot loader, two programs (a platform daemon first, then the game). The
five-minute bar is **met**: at least 363 seconds of attract mode in one run on 2026-10-13, with no error
lines. The open question is reliability: of eight runs with a picture that day, four reached attract and
four stopped during start-up with the same error.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths, indices and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS under plain Wine, with the game files on network storage that is only read.

## 1. Platform and cabinet

The game is **House of the Dead: Scarlet Dawn** (Sega, 2018) on the **Sega ALLS** platform, the UX
variant. It is a 64-bit Windows program in Unreal Engine 4.18 and a lightgun game for two players.

| | |
|---|---|
| Platform | Sega ALLS UX — `pxGetHwInfo.ini`: `platform-id = ACA0`, and the Japanese note in that file calls `ACA0` explicitly "ALLS UX" **MEASURED** |
| Engine | Unreal Engine **4.18.1** — `CrashContext.runtime-xml`: `EngineVersion 4.18.1-0+++UE4+Release-4.18`, `BuildConfiguration Shipping`, `PlatformName WindowsNoEditor` **MEASURED** |
| Game file | `Hodzero-Win64-Shipping.exe`, **54,128,128 bytes**, PE32+ x86-64, GUI subsystem, 9 sections, image base `0x140000000`, entry point RVA `0x1c2929c`, timestamp `2018-08-21T10:10:19Z`, and **50 imported libraries** **MEASURED** |
| Code name | `Hodzero`; `-Win64-Shipping.exe` is the standard name Unreal gives a shipping build **MEASURED** |
| Game id | `SDET` (`App/Startup/99_StartApp.bat`: `set GAME_ID=SDET`) **MEASURED** |
| Second program | `amdaemon.exe`, **8,272,384 bytes**, PE32+ x64, **console** subsystem, entry point RVA `0x2268a4`, timestamp `2018-04-19T14:13:45Z`, version line `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37` **MEASURED** |
| Content | One Unreal package of **7,847,593,660 bytes** plus loose CRIWARE content **MEASURED** |

**The cabinet.** The input and the picture are described in
`App/Hodzero/WindowsNoEditor/AMDaemon/config.json` (3,171 bytes) **MEASURED**:

- **two players**, each with a gun: per player `start` (USB index 7), `gun_select` (index 4) and
  `gun_special` (index 1), all on USB node 0;
- cabinet switches on that same node: `test` (index 0, not bound to a player), `service` (index 6),
  `bakuon` (14), `eye0` (10), `proximity_cabinet` (10), `cabinet_kind` (11) and three payment-terminal
  switches (`emoney_left`/`right`/`center`, index 13/12/11);
- the I/O board must have four functions according to that configuration: `coin_input` (slot 2),
  `switch_input` (2 players × 8 buttons), `general_output` (slot 20) and one `unique_io` command
  (command 65, parameter `[6]`, output only);
- picture: `video.enable = true`, `resolution 1920 × 1080`, `ignore_unconnected = true`. The official
  start chain also really sets the desktop to 1920 × 1080 (`SetResolution.exe 1920 1080 0`);
- coins: `credit.enable = true`, `max_credit = 24`, `game_cost = [-1,-1,1,1,-1,-1,-1,-1]`;
- error handling: `error.auto_reset = true` — a restart after an error is therefore **expected
  behaviour**.

The machine that appears in the logs (Ryzen 7 7800X3D, Radeon RX 6950 XT under RADV, 31 GB) is **our
test machine**, not the cabinet. **MEASURED.**

## 2. What the game needs

### Folders and paths (**MEASURED**)

The main folder on our share (`…/Teknoparrot/House of the Dead Scarlet Dawn`) holds **2,081 files,
13,095,511,155 bytes, and zero empty files**. The tree:

```
game.bat  firewall.cfg  pxGetHwinfo.exe  pxGetHwInfo.ini
App/Startup/                 5 .bat  (05_SetVideoPort, 10_SetResolution, 20_SetAudioVolume, 99_StartApp, AppStartup)
App/Tools/                   3 .exe  (SetResolution, SetAudioVolume, CleanupLogDir)
App/Hodzero/StartApp.bat
App/Hodzero/WindowsNoEditor/
   AMDaemon/                  5 files + aime_firm/ (6) + am_resources/ (9)
   Engine/Binaries/ThirdParty/  PhysX (11), Vorbis (2), Ogg (1), NVaftermath (1), Windows/DirectX (1)
   Hodzero/Binaries/Win64/    2 .exe, 2 .png, teknoparrot.ini
   Hodzero/Content/Paks/      1 package, 7,847,593,660 bytes
   Hodzero/Content/Hodzero_V1/ADX/  2,014 files (1,890 .uasset + 123 .usm + 1 .acf), 5,024,069,116 bytes
   Hodzero/Saved/Config/WindowsNoEditor/Game.ini  (274 bytes)
```

### Is it complete? Four structural checks (**MEASURED**)

This is a **structural** check and not a hash check: the expected file hashes of the integrity test that
the commercial tool uses are not in the profile files. What we could check:

| Check | Outcome |
|---|---|
| Does the executable the profile names exist | **Yes** — `Hodzero-Win64-Shipping.exe` of 54,128,128 bytes (exactly the size the profile records) and `amdaemon.exe` |
| Empty files (0 bytes) | **0 of 2,081** |
| Is the image whole (not cut off) | **32 PE files, 0 cut off** — the end of the last section is exactly the end of each file |
| Are all libraries findable | **0 unresolved** — the game imports 50 libraries, the daemon 23, `hwvalue.exe` 7 and `pxGetHwinfo.exe` 5; all **44 unique names** are present in the Wine prefix (`system32` and `syswow64`, 598 and 631 libraries respectively), including the usual pain points `vcruntime140`, `msvcp140`, `ucrtbase`, `mfplat`, `d3d9`/`d3d11`/`dxgi`, `xinput1_3` and `X3DAudio1_7` |
| References in the game's own scripts that are missing | 9, and **all 9 have a measured, harmless explanation** (fallback executables that are not needed, developer markers whose absence is exactly what selects the "cabinet" mode, and a file name that only occurs in a Japanese comment line) |

**The package is not cut off**, and that can be proven without starting the game: the last 44 bytes are
a complete Unreal footer — magic `0x5A6F12E1`, version 4, `IndexOffset 7,839,093,987`,
`IndexSize 8,499,628` — and `index_offset + index_size` ends **exactly** where that footer begins, with
the footer exactly at the end of the file. There is no room for missing content and no dangling offset.
**MEASURED.**

### What the package does not contain (**MEASURED**)

That is worth knowing for anyone who wants to copy or move this title. Unreal stores an asset as a pair
(`.uasset` + `.uexp`):

| Collection | Count |
|---|---|
| In the package under `HodZero_v1/ADX/` | **2,874** (1,437 + 1,437) |
| Loose on disk under `Content/Hodzero_V1/ADX/` | **2,014** (1,890 `.uasset` + 123 `.usm` + 1 `.acf`) |
| **Loose, and nowhere in the package** | **577** (453 `.uasset` + 123 `.usm` + 1 `.acf`) |

Those 577 files therefore exist in only one place: **the 123 video files and 453 assets do not come out
of the package**. The largest movie is `adver_prime.usm` (167,611,104 bytes). The engine asks for that
folder with a **capital Z** (`…/Hodzero/Content/HodZero_v1/Fonts/…`) while the folder on disk is called
`Hodzero_V1`; that works here because the network mount is case-insensitive. See section 10.

### The role of the daemon (**MEASURED**)

This game has **two programs**. `amdaemon.exe` is Sega's AM Daemon, and it must run **before** the game:
the commercial tool's profile says so itself (`HasTwoExecutables = true`,
`LaunchSecondExecutableFirst = true`, `ExecutableName2 = amdaemon.exe`), and the game's own start chain
waits five seconds for it (section 3).

The daemon is the **platform layer**: it provides the services the game asks for — the I/O board's
input, the coins, the bookkeeping, the file system that the game sees as drive letters, and the network
layer. The game is its **client**: the game file contains the client side of that coupling
(`amdaemon::ipc::` occurs **506×** in the game executable, with `ClientRequest` 131×, `DaemonShared`,
`DaemonSharedPart` and `makeDaemonShared`), and that coupling runs over **shared memory**, not over a
port and not over a named pipe. The daemon has the server side (`amdaemon::server::`, 378 names) plus
the thirteen mutex names the client does not have.

### The game's own configuration (**MEASURED**)

`Hodzero/Saved/Config/WindowsNoEditor/Game.ini` is a real Unreal config with its own section:

```ini
[/Script/Hodzero.AppGameInstance]
DevelopSetting_PlayerGunDevice=0
DebugAMBackup=false
DebugAMCredit=false
DebugAMCreditFreePlay=false
DebugWithoutAMDaemon=false
DevelopSetting_DspEnemyAIWorkingStatus=true
bIsKoreanROMSetting=false
```

Interesting for anyone who wants to drive this game: there is a **switch to run without the daemon** in
it (`DebugWithoutAMDaemon`), next to a free-play switch (`DebugAMCreditFreePlay`). The official start
chain also has the game read this configuration from a **user folder** (`-UserDir=…`), which is a neat
way to set such values per game without touching the game files. **MEASURED** that the chain does it;
**INFERRED** from that same chain that the game really uses that folder.

### The hardware requirement does nothing (**MEASURED**)

`pxGetHwInfo.ini` demands `platform-id ACA0`, `mem-size 16384`, `appdata-size 17000` and
`graphics-board-lane-width 16`, and `pxGetHwinfo.exe` (701,440 bytes) is in the game folder to check
exactly that. **Nothing in the start chain runs that program**, and the game was measured playing on a
machine with different values. The requirement is inert in this copy.

## 3. The official start script, or why there is none

**We never execute these scripts.** Our launcher **reads** them and takes over only what is needed.
That is not a formality: this chain does cabinet-wide things — it changes the platform's registry, sets
the desktop resolution, and does `rmdir /S /Q "Y:\"` at every start. Our launcher reports those commands
explicitly as a *cabinet thing (therefore not executed)* and does not run them. **MEASURED.**

The chain is `game.bat` → `App/Startup/AppStartup.bat` → `05_SetVideoPort.bat`,
`10_SetResolution.bat`, `20_SetAudioVolume.bat`, `99_StartApp.bat` → `App/Hodzero/StartApp.bat`:

| Step | What it does |
|---|---|
| `game.bat` | Sets `LAUNCH_ON_IMAGE=1` and `IMAGE_ROOT_DIR=X:\`, and starts `AppStartup.bat` |
| `AppStartup.bat` | Calls the four following scripts in order |
| `05_SetVideoPort.bat` | Writes five register values (`port_1..port_5` = `DVI0, DP0, DP1, DP2, HDMI0`) |
| `10_SetResolution.bat` | `..\Tools\SetResolution.exe 1920 1080 0` |
| `20_SetAudioVolume.bat` | `..\Tools\SetAudioVolume.exe 85 75` |
| `99_StartApp.bat` | `GAME_ID=SDET`, `APP_DIR=Y:\SDET`, `APP_LOG_DIR=Y:\SDET\current_log`; creates `Y:\SDET`, wipes `Y:\` if that folder is missing, and moves to `App/Hodzero` |
| `App/Hodzero/StartApp.bat` | Picks the newest of four game executables (shipping first), sets `DAEMON_WAIT_SECONDS=5`, starts the daemon, waits, starts the game, waits, and cleans up |

The relevant lines from `StartApp.bat`, literally (**MEASURED**; the files are Shift-JIS, the keys are
plain ASCII):

```
set DAEMON_WAIT_SECONDS=5
start /min "" "%DAEMON_EXE_PATH%" -c %DAEMON_CONFIG_PATH% -l "%DAEMON_LOG_PATH%"
timeout %DAEMON_WAIT_SECONDS%
start /wait "" "%APP_EXE_PATH%" %APP_EXE_ARGS%
timeout /t 1
taskkill /im "%DAEMON_EXE_FILE_NAME%"
```

with `%DAEMON_CONFIG_PATH% = WindowsNoEditor\AMDaemon\config.json`,
`%APP_EXE_ARGS% = -launch="cabinet" -ABSLOG=… -UserDir=… -NotInstalled -UNATTENDED`, and the working
directory the chain builds being `App/Hodzero` (`99_StartApp.bat` does `pushd ..\Hodzero`).

**The `-UNATTENDED` finding.** One of those arguments is `-UNATTENDED`, and the script's comment on it
says "do not show the error window". That is not cosmetic. In one of the measured sessions this game
failed fatally **74 seconds** after the start; the log shows the error **without a message**
(`Fatal error!` followed by an empty line), then Unreal tries to start its crash reporter (that file is
not shipped with the game), and then there are **sixty seconds with no line at all** between the last
normal log line and the shutdown. Without `-UNATTENDED`, Unreal sits on a modal window before it shuts
down. **MEASURED** for the 60-second gap and for the comment; **INFERRED** for the mechanism.

We therefore take over from this chain: **the order** (daemon, 5 seconds, game), **`-UNATTENDED`**, and
nothing else. The arguments are not a condition for playing, by the way: the commercial runtime gives
this game **no arguments at all** and the game was measured playing that way. **MEASURED.**

## 4. The route we use

Our launcher is `scripts/launch-direct.py` from the project repository. It reads the game profile plus
the per-game settings, builds a **plan** from them, and can execute that plan — without executing the
game's own start script. **MEASURED.**

```sh
# the plan, without starting anything:
python3 scripts/launch-direct.py HOTDSD --plan
# and the run:
python3 scripts/launch-direct.py HOTDSD --run
```

The plan contains the complete command line per start. For this game there are **two**, and that is
immediately the answer to how you start a game with a separate helper program:

```
start 1/2: amdaemon.exe (second executable)
  wine-prefix …/OpenParrotLoader64.exe 'Z:\…\TeknoParrot64' 'Z:\…\AMDaemon\amdaemon.exe'
wait: 5 s; only continue if every earlier start is still alive
start 2/2: Hodzero-Win64-Shipping.exe (the game)
  wine-prefix …/OpenParrotLoader64.exe 'Z:\…\TeknoParrot64' 'Z:\…\Hodzero-Win64-Shipping.exe'
```

**The arguments.** For the daemon: **none** (the official chain passes `-c` and `-l`, we do not — the
plan says literally "arguments for amdaemon.exe: none"). For the game: also **none**. What we do take
over is the working-directory handling: the launcher sets the target's working directory to the
target's own folder, not to Wine's. **MEASURED.**

**The environment variables the launcher sets** (**MEASURED**):

| Variable | Value | Why |
|---|---|---|
| `WINEPREFIX` | `~/.local/share/ravengun/prefixes/shared/wine` | The shared prefix |
| `RG_UNSET_REMOTETHREAD` | `1` | See below — **the most important one** |
| `OPENSSL_ia32cap` | `:~0x20000000` | A project setting for this series of titles |
| `WINEDEBUG` | `-all` | No noise in the log |
| `TP_LAUNCH_SESSION_ID` | a UUID per run | So that one run's output can be found back |

**The remote-thread trap.** The commercial front-end sets a remote-thread variable under Wine itself,
and then the loader chooses its *remote-thread* injection route, which fails here with
*"Failed to find kernel32.dll in target process"*. Our wrapper therefore removes that variable, after
which the loader takes its default route: the **entry-point trampoline**, which announces itself in the
log as `Resolved target kernel32.dll procedure: local=… remote=… rva=0xF81C` followed by `Have fun :)`.
**That is the route that works.** **MEASURED.**

**What the launcher identifies.** Every log starts with the tool that performed the run, so that a run
with the wrong version can no longer be confused with a good one (**MEASURED**):

```
launcher: <repo>/scripts/launch-direct.py
  the version from the REPO (commit 5cdfaa8; scripts/ equal to that commit);
  sha256 327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0;
  helper modules (5) sha256 c882ade50b66ad0cd20260050983968dfc629aba9033c1e5233d92f5f364d656
  the installed launcher (…/opt/ravengun/share/ravengun/scripts/launch-direct.py) is the same
  core: …/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll
  sha256 f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28, version 1.0.0.3761
```

The rule behind those lines is described on the [Loader architecture](Loader-Architecture) page.

**The view and the share.** The launcher starts the game from a **view**
(`~/.local/share/ravengun/runtime/views/HOTDSD`) with the same tree inside it as the game. If that view
is full of **symbolic links to the game folder**, then the daemon and the launcher write straight
through those links into the game folder on the share. If you do not want that — and we do not — make a
**real copy** of everything that gets written and keep only the read-only bulk (the `Content` folder of
12 GB and `Engine`) as a symbolic link. That is exactly what the launcher itself advises when a target
is on the share: *"…is on the NAS; not written. Make a local view first."* **MEASURED.**

### 4.1 The second program: the daemon and the platform layer

This is where it goes wrong, and it is worth being precise about it.

**Start `amdaemon.exe` on its own and it dies after one second.** It prints its banner first and then
dies in the **platform layer**, before anything to do with the disks or the input. The literal line from
its own log (**MEASURED**):

```
<< 2026/10/13 12:13:02 >>
Runtime exception occurred.
File: D:\jenkins_work\workspace\all_build\libs\libamw\src\amw_platform_manager.cpp
Line: 102
Function: void __cdecl am::platform::Manager::initialize(void)
Message: amPlatformGetPlatformInformation(). ErrCode 0.
```

The exit code is **0** and the wall time **1 second**: it ends itself neatly after logging the
exception. The banner that does appear is `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37`
**MEASURED.**

**Which layers it never touched.** Counted from a file trace (`WINEDEBUG=+file`, a log channel and not
an API tracer) of **1.5 MB** per run, **MEASURED**:

| Layer | Count in the trace |
|---|---|
| The AM file system that the game sees as drive letters (`AMFS`) | **0×** |
| The app-data file (`appfile`, normally 8,388,608 bytes) | **0×** |
| Graphics libraries (`nvapi`, `dxgi`, `d3d11`, `vulkan`) | **0×** |
| Device enumeration through `setupapi` / `cfgmgr32` | **45×** and **29×** |

It therefore dies in the **last** of those rows: the platform check asks something about the cabinet's
hardware, and on an ordinary PC it does not find it. The function that fails belongs to the system and
platform module of the AM framework; the file contains, among others, `amPlatformGetMemorySize`,
`amPlatformGetGraphicInfo`, `amPlatformGetDevInfo`, `amPlatformIsAvailableSubStorage` and
`amSysDataGetDotClockTiming` **MEASURED**. There are **no device-interface GUIDs** in the file, so what
it looks up is not determined by a fixed literal GUID.

**What the A/B decides.** We ran the daemon in six set-ups, each with **one** variable different from
the previous. The layers from the table above were counted per arm:

| Arm | What it changes | Daemon lifetime | Evidence | `AMFS` | `appfile` |
|---|---|---|---|---|---|
| A1 | The full route: launcher + core + **also the game** | **≥95 s** (our limit); game process **88 s** | The daemon prints `Process state: setup Game Mode`, `Error 0`, **`Process state: Advertise`** | not countable¹ | not countable¹ |
| A2 | Versus A1: the game is **not** started | **≥30 s**, still alive | **`AMFS` 95×, `appfile` 11×** — it gets past the platform phase | **95×** | **11×** |
| A3 | Versus A2: **a different core** | **≥30 s**, still alive | `AMFS` 0× — it survives but does not progress | 0× | 0× |
| A4 | Versus A2/A3: **leave the launcher out** (start directly) | **~1 s**, dies by itself | As above | 0× | 0× |
| A5 | Versus A4: **add the arguments** (`-c`, `-l`) | **~1 s** | The same platform error, now in the log | 0× | 0× |
| A6 | Versus A5: **the shape of the folder** (bare copy) | **~1 s** | The same platform error | 0× | 0× |

¹ The launcher sets `WINEDEBUG=-all`, so for A1 there is no trace and the counts are *not countable* —
which is something different from "not touched".

**The answer, and it is not the answer we expected.** The question was whether the commercial tool's
core satisfies that platform check. There turn out to be **two** conditions:

1. **The injection itself takes the fatal ending out.** With the launcher and *any* library injected the
   daemon lives **≥30 s** (arm A3, with a completely different core). Started directly it dies after
   1 s. That was not predicted and it is in no earlier note.
2. **Only with the actual core does it also get further.** Arm A2 touches the mount and app-data layers
   (**95×** and **11×**) with the real core; arm A3 with the other core touches them **0×**. Because the
   platform check runs inside `Manager::initialize()` **before** the mount phase, touching those two
   layers proves that the daemon is past the platform step. **MEASURED** for the counts; **INFERRED**
   for the causal reading (the other core could also change only the exception handling, so that the
   daemon hangs instead of dying — those two cannot be separated with these data).

## 5. Input

**MEASURED** from the cabinet's own `config.json` (3,171 bytes), the daemon's log and the game's log:

- **Two players**, each with a gun: per player `start` (USB index 7), `gun_select` (index 4) and
  `gun_special` (index 1), all on USB node 0.
- **Cabinet switches** on the same node: `test` (index 0, not bound to a player — the switch that opens
  the service/test screen), `service` (index 6), `bakuon` (14), `eye0` (10), `proximity_cabinet` (10),
  `cabinet_kind` (11), and three payment-terminal switches (`emoney_left`/`right`/`center`, index
  13/12/11).
- **The I/O board** itself must offer four functions according to that configuration: `coin_input`
  (slot 2), `switch_input` (2 players × 8 buttons), `general_output` (slot 20) and one `unique_io`
  command (command 65, parameter `[6]`, output only). **MEASURED** from the configuration; **INFERRED**
  that these are exactly the four things the platform layer asks the board for, because the daemon
  builds its board interface from this file.
- **Coins** are enabled and counted: `credit.enable = true`, `max_credit = 24`,
  `game_cost = [-1,-1,1,1,-1,-1,-1,-1]` (two slots at 1 credit).
- **The guns are calibrated per player and the game writes those values out.** From a real session on
  this machine (**MEASURED**): `P2_CenterX : 474`, `P2_CenterY : 622`, `P2_RightX : 890`,
  `P2_BottomY : 979`, `P2_LeftX : 42`, `P2_TopY : 235`. That is useful if you want to know the screen
  area the game expects from a gun.
- **The daemon delivers the board input and the coins.** That follows from the role of the daemon
  (section 2) and from the A/B of section 4.1: the input and the coins are part of the platform layer,
  and the game is its client. **INFERRED**, with the client and server symbol counts of section 2 as
  the evidence.
- **Our route for input:** `evdev` — our engine writes directly to an evdev device and the game reads
  input devices itself, outside any core. That is the route recorded for this title in our per-game
  settings. **MEASURED** in the settings; **not measured** end to end, because our own run was headless
  and lasted 88 seconds (section 6).
- **The game's own input error:** the game reports, once per session, on the same log line,
  `LogAMDaemon: Error: Exception caused. (Category:Logic Msg:"aimeId" is invalid.)` when the login
  screen is entered, and then simply continues (`UPlayLog::StartPlayLog PlayCount=1`). That belongs to
  the card reader, which is absent in this copy; the game knows a guest path next to the card path.
  **MEASURED**, and it does not block play.

## 6. Timing

**MEASURED, on our measurement day (2026-10-13):** the daemon lives **≥95 s** in the launcher route
(cut off by our own limit, exit code `-15`), the game process **88 s** (12:11:12 → 12:12:40). Started
directly: **1 s**. With the launcher but without the actual core: **≥30 s** but with no progress at all.

**MEASURED, from the logs of 2026-10-11 — six sessions in one day:**

| # | Opened | Closed | Duration | How it ended |
|---|---|---|---|---|
| 1 | 08:41:39 | 08:42:12 | 33 s | Stopped (no crash block, no `Log file closed`) |
| 2 | 08:44:21 | 08:44:47 | 26 s | Stopped |
| 3 | 08:46:27 | 08:46:54 | 27 s | Stopped |
| 4 | 08:47:52 | 08:51:56 | **4 min 4 s** | Stopped |
| 5 | 08:52:40 | 08:54:54 | 2 min 14 s | **Crash**, 74 s after the start |
| 6 | 08:55:41 | 09:02:44 | **7 min 3 s** | Stopped |

That five of the six have no crash block is checkable: their log ends in the middle of the bookkeeping
without `Log file closed`, and the launcher's log stops at the same moment. The crash of session 5 falls
**exactly** on the moment the first level finished loading (74 s after the start, `SecondsSinceStart 74`
in the crash report, and the last normal log line is in that same second). Memory was **not** the
problem at that moment: **22,759 MB free of 31,705 MB**, and the process was using 1.19 GB. A
**minidump of 297,524 bytes** was kept. It is moreover **not reproducible at that point**: session 6
loaded the same level and then ran for another **105 seconds**.

**What a measurement does not say.** This says that the game starts, plays and keeps score — not that
every frame is good.

## 7. What we saw on screen

**From our own runs: nothing.** We ran headless (`DISPLAY` and `WAYLAND_DISPLAY` unset), so there was no
display server and nothing could be rendered. That is visible in the log: the session of that day stops
after **0.8 s** at `LogInit: Using OS detected locale`, **before** any `LogD3D11RHI` line and before
mounting the plug-ins, while on 2026-10-11 `LogD3D11RHI: Aftermath initialized` was already in there
early. Whether that is because our `SIGTERM` threw away the buffered log or because the game was stuck
in initialisation, **we cannot separate** — so we claim **no** screen observation. **MEASURED** that the
log stops there.

**What the logs of 2026-10-11 do show** (so from the files, not from a screen) **MEASURED**:

- `Match State Changed from WaitingToStart to InProgress` — the game reaches the playing state;
- `Took 5.198148 seconds to LoadMap(/Game/HodZero_v1/Maps/Stage0/PL_Stage0)`, and in another session
  **1.98 s** for the same map;
- all sublevels of that level are activated (`SL_Stage0_ITEM`, `…_BROKEN_OBJECT`, `…_COLLISION`,
  `…_ENEMY`, `…_BG`, `…_LIGHT`, `…_EFFECT`, `…_VOLUME`);
- the plug-ins mount: `AMDaemon`, `CriWare`, `ABaaSGS`, `EmitMan`, `StateMan`, `TaskMan`, `WidgetMan`,
  `ApexDestruction`, `Paper2D`, `LowEntryExtendedStandardLibrary`;
- the calibration values of player 2 are written out (section 5);
- the game's own bookkeeping runs: `UBookkeeping` with `P1 GameTime`, `AddDamageLogCount`,
  `AddFirstDeathLogCount`, `AddSecondDeathLogCount`, and ranking lines;
- and the game reports one error of its own, twice in the seven-minute session and once per session,
  always on **the same line in the log (line 416)**, when the login screen is entered (section 5).

**The bar.** We count a title as "met" at **five minutes of attract mode or game mode**; a loading
screen or the title screen does not count. **That bar is met.** In the best run on 2026-10-13 the game
held **363 seconds of attract** (from 13:48:31 to 13:54:34), with **zero** error lines, and the run was
ended by **our** 420-second limit, not by the game — so the honest wording is "**at least** 363 s".
**MEASURED** from the game's own log.

## 8. Current state and open questions

**Status: the game starts, the daemon reaches `Process state: Advertise` on the full route, and on
2026-10-11 the game came into its first level with bookkeeping and calibration. Our own route meets the
bar (at least 363 s of attract on 2026-10-13), but not on every start: four of eight runs with a picture
that day reached attract and four stopped during start-up with the same error.**

*Sections 1 to 7 and the table below were written from measurements up to 2026-10-13 12:22 and have not
been revised against the later runs.*

| # | Open point | Next step |
|---|---|---|
| 1 | **No screen observation.** Our run was headless and the game logs nothing after 0.8 s. | A run **with** a display server (gamescope or the owner's Xwayland), a longer limit, and **stopping cleanly** instead of killing — otherwise the log is cut off again. |
| 2 | **Which launcher?** Today's route uses a launcher of 1,227,264 bytes (`…/openparrot/current/…`, sha256 `1953d91a…`); the runs of 2026-10-11 used one of 333,824 bytes (`…/runtime/TeknoParrotUi-2.0.0.130-linux-x64/OpenParrotx64/…`, sha256 `ad8528b2…`). The core is the same in both (`f5c7532d…`, 1.0.0.3761). | Compare the two launchers with the same setup; that is the first candidate for "why does today's game not get past 0.8 s while on 2026-10-11 it reached the first level" — but it could just as well be point 1. |
| 3 | **The two conditions of the platform check** (injection *and* core) cannot be reduced to one cause. | An instrument inside the core that shows which call the platform check actually makes. |
| 4 | **The drive letters** (`X:`, `Y:`, `D:`) are not supplied by us. The official chain creates them with `rmdir`/`mkdir` on `Y:\`, and we do not execute that. | Find out which letters the daemon looks up on our route; on 2026-10-11 it did (its app-data file was written). |
| 5 | **The start measurement** has no row for this game yet: it could not be planned while the launcher could not start two executables. | That is possible now (section 4). A run through the start measurement delivers the row. |
| 6 | **`DebugWithoutAMDaemon`** is set to `false` in the game's own configuration. | Whether the game plays without the daemon with that switch on `true` has not been tested; see section 2. |

## 9. What we tried and what did not work

**The arms that changed one thing and decided nothing (MEASURED).** Three of the six arms of section
4.1 exist purely to exclude something, and they all ended the same way — the daemon died after ~1 s with
the same platform error:

- **A4 — start the daemon directly, without the launcher.** Same error, same second. That excludes the
  launcher as the *cause* of the error (it is, however, the thing that *prevents* it: arm A3).
- **A5 — add the arguments the official chain uses (`-c`, `-l`).** Same error, now visible in the
  daemon's own log instead of only on its console. That excludes "the daemon needs its configuration
  file to get further" as an explanation for the failure.
- **A6 — change the shape of the folder (a bare copy instead of the view).** Same error. That excludes
  the layout of the game folder as the explanation.

**What we also tried, and the lesson each time:**

1. **Running headless and reading the log afterwards.** The game stopped logging after 0.8 s, and we
   could **not** tell whether the process was stuck or whether our own termination signal had thrown the
   buffered log away. **Lesson:** stop a game cleanly if you intend to read its log; a `SIGTERM` costs
   you the evidence, not just the run.
2. **Treating the 60-second silence as the failure.** Without `-UNATTENDED` a fatal Unreal error sits on
   a modal window for **sixty seconds** before the process ends. The failure is the error at 74 s; the
   minute is the *consequence* of not passing that argument. **Lesson:** the gap between the last log
   line and the shutdown is not always part of the failure.
3. **Reading the crash of session 5 as "the game falls over at the first level".** Session 6 loaded the
   same level and then ran **105 seconds** longer. Memory was not the problem (22.7 GB free). **Lesson:**
   one crash at a point is not a property of that point; it needs a second run before you call it a
   pattern.
4. **Taking the launcher as a given.** Two different launcher binaries were in play between the
   sessions (section 8, point 2) and the core was the same in both. That difference is now the first
   candidate for the difference in outcome, and it was nearly overlooked. **Lesson:** let the tool print
   its own path and SHA-256 at the top of the log (the [Loader architecture](Loader-Architecture) page
   describes that rule) before comparing two runs.
5. **Claiming a screen observation from a headless run.** We do not. The picture evidence for this title
   comes from the logs of the earlier sessions, and they are described as such (section 7).

## 10. What is Linux-specific, and what differs on Windows

We run this game on **SteamOS** (Arch-based) under **plain Wine**, in a shared Wine prefix, with the
game files on a network mount. That asks for a number of things that do not come up on Windows.

**MEASURED:**

- **Wine version and prefix.** `wine-11.0` (a Proton build used as plain Wine) with
  `WINEPREFIX=~/.local/share/ravengun/prefixes/shared/wine`. That prefix contains **598 libraries in
  `system32`** and **631 in `syswow64`**; all 44 libraries that the game and the daemon import are
  present there. Without that prefix (for example a fresh, empty one) you would expect to miss several,
  especially `msvcp110`/`msvcr110` (the daemon imports those two) and `vcruntime140`/`msvcp140`.
- **The remote-thread variable must go.** The remote-thread injection route fails under Wine
  (*"Failed to find kernel32.dll in target process"*); with the variable removed the launcher takes the
  entry-point trampoline and it works. That is purely Wine-specific (section 4).
- **The daemon runs without a display.** It is a **console** program (subsystem 3) and starts with
  `DISPLAY` unset; it prints its banner and dies on the platform check. For the **game** that is
  different: that is a Direct3D 11 title and needs a display server.
- **Case sensitivity.** The network mount (CIFS) is case-insensitive, which is why `HodZero_v1` (the
  spelling in the package and in the engine) and `Hodzero_V1` (the spelling on disk) land on the same
  folder. **On a case-sensitive file system (ext4) that can break**, and specifically for the **577
  loose files** from section 2 that are nowhere in the package. It is not a problem now — the game has
  played — but it is a real risk as soon as someone copies the files to a local disk.
- **Permissions and the share.** The network mount is writable for the user. Our rule is that we only
  read there, and that is therefore an agreement and not a property of the file system. Whoever starts a
  game from such a folder has to reckon with the daemon and the launcher writing their own files next to
  the executable: the daemon its app-data file of 8.4 MB and the launcher a `teknoparrot.ini`. We catch
  that with a view of **real copies** plus symbolic links for the read-only bulk of 12 GB (section 4).
- **Speed and limits.** A cold Wine prefix costs the first seconds; our arms with a hard limit sometimes
  did not even let the daemon begin. A limit of 90 s per start is ample for the daemon (it dies or
  succeeds within 2 s) but not for the game.

**INFERRED, and different on Windows:**

- **Drive letters.** We do not supply the letters `X:`, `Y:` and `D:`; the official chain creates them
  with `rmdir`/`mkdir` on `Y:\` and we do not execute that. On Windows the obvious route is `subst` or a
  real partition; under Wine it would have to be a prefix link or a hook in the launcher. We have **not**
  built and **not** tested this.
- **The resolution and the volume.** `SetResolution.exe 1920 1080 0` and `SetAudioVolume.exe 85 75` are
  Windows programs from the game folder. We do not run them; under Wine the display server
  (gamescope/Xwayland) is what determines the resolution.
- **What you would probably do most simply on Windows:** start the game's executables with your own
  launcher, run the daemon first with a five-second wait, pass `-UNATTENDED` to the game, and arrange the
  drive letters with `subst`. That is **not tested by us** — it is the translation of what we learned
  here.

---

*Everything above was measured on one machine: a Steam Deck running SteamOS with plain Wine, and the
game files on network storage that is only read. The section 4.1 measurements come from two runs on
2026-10-13 (12:08–12:20) and the session history from logs of 2026-10-11. Where a measurement wrote to
the share, that is in our own notes and not on this page; this page describes the game and the loader.
The numbers, paths and line numbers are copied literally from the files; every INFERRED label points at
exactly the place where we have only indirect evidence.*
