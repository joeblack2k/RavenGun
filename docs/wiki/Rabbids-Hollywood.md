# Rabbids Hollywood Arcade

*Adrenaline Amusements "PC Based" cabinet. The game is a 64-bit Unity (Mono) program. Our route:
Proton plus a plug-in inside the game, with input over TCP. The five-minute bar is **met**: 608 s
(10 min 8 s) on our own route, with coins and start accepted and a real level played.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS under Wine/Proton, with the game files on network storage that is only read.

## 1. Platform and cabinet

Unlike the Sega titles in this set, this is a **PC title**: the cabinet runs an ordinary Windows
installation and the game is a Unity program. **MEASURED** from the profile: emulator type
`TeknoParrot`, emulation profile `WartranTroopers`, `ExecutableName: Game.exe`, `Is64Bit: true`. That
profile name is a generic arrangement for four-player shooters, which fits the four axis pairs in the
wire protocol of section 5.

**MEASURED** from the folder:

| File | Size | Role |
|---|---|---|
| `Game.exe` | 650,752 bytes | The game; exactly the name the profile gives |
| `UnityPlayer.dll` | 23 MB | The Unity runtime, 64-bit |
| `Game_Data/` | 1.9 GB | The Unity assets (`Managed`, `Plugins`, `Resources`, `StreamingAssets`, `Mono`, `GI`) |
| `UnityPlayer_Win32_x64*.pdb`, `WindowsPlayer_x64_Master*.pdb` | — | **Debug symbols are in the folder**; useful if you want to follow the runtime |
| `Firmware_Shooter.hex`, `libpng15.dll` | — | Cabinet firmware data and a loose library the game ships |
| `BepInEx/`, `doorstop_config.ini`, `.doorstop_version`, `winhttp.dll` | — | A doorstop-based plug-in loader already present in the folder |

**The cabinet and the picture** (**MEASURED** from the game's behaviour, **INFERRED** for the cabinet
itself): the cabinet has **lightguns**, and the game reads its aim point as a **mouse position in its
own window** — which is exactly why the wire protocol uses screen pixels and why the Y origin is at
the bottom (section 5). The TV is 1920×1080, which is the resolution we run the game in. The screen
shows "Insert credits" per player, and coins are counted in **quarters** ("1/4 credits"). We have
never seen the cabinet.

**The language** (**MEASURED**): the game is multilingual and has a language setting; we set it to
English and the title screen came up in English (section 7).

## 2. What the game needs

We counted the folder for completeness. **MEASURED**:

| Check | Outcome |
|---|---|
| Executable | `Game.exe`, 650,752 bytes — exactly what the profile names |
| Empty files | **0 of 1,788** |
| Size | **2.0 GB** (of which `Game_Data` alone is 1.9 GB) |
| Unity structure | `Game_Data` with `Managed`, `Plugins`, `Resources`, `StreamingAssets`, `Mono`, `GI`; `UnityPlayer.dll` (23 MB) |
| A doorstop route is already present | `BepInEx/{core,plugins,config,cache,patchers}`, `doorstop_config.ini`, `.doorstop_version`, `winhttp.dll` |
| Tools next to the game | `GameLauncher.exe`, and an operator tool with its own `.conf` and a `README.txt` |

The folder is therefore structurally complete: no file is empty and nothing is missing for a start.
No separate asset folder or drive letter is needed, as it is on some other titles: everything is under
the game folder.

**What we add ourselves, and where** (**MEASURED**). We write **nothing** into the folder on the
network share. Instead we build a **local view** of links:

```
~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/     <- local folder with links
    <every file from the share, nothing copied>
    Game_Data/                       <- a REAL folder with one link per part
    BepInEx/                         <- our own BepInEx 5.4.21.0, not the one from the share
    winhttp.dll, doorstop_config.ini <- the doorstop
    BepInEx/plugins/RavenGun.RabbidsHollywood.dll   <- our plug-in inside the game
```

- Small configuration files (`*.json`, `*.conf`, `*.cfg`, smaller than 64 KiB) and empty flag files
  (`Game.run`, `ForceReboot`) are **copied** into the view instead of linked, because the game writes
  in them. **MEASURED:** in a ten-minute run all those writes indeed landed locally in the view
  (modification times during the run) and the share stayed untouched.
- The loader that is present **on the share** and the game's own settings file are **not** put in the
  view: we use our own clean loader. **MEASURED** in the setup script.

## 3. The official start script, or why there is none

**There is no `game.bat` for this title.** The folder contains no `.bat` or `.cmd` file at all
(**MEASURED** from the file list). What is there instead are two Windows programs:

- `GameLauncher.exe` — a start program in the game folder;
- an operator tool with a configuration file and a read-me.

What those two do is **INFERRED** (we did not run them and did not dissect them): their names and the
read-me make it clear that they are cabinet tooling, not the game loop.

**We never execute this kind of script or program.** That is not caution for the sake of form: another
title in this same set has an official `game.bat` that begins with `rmdir /S /Q Y:\`, which **wipes a
whole drive** if the expected data folder is missing. On a real cabinet that is a dedicated volume; on
an ordinary machine `Y:` may be yours. The general reasoning, with all the examples, is on the
[Official start scripts](Official-Start-Scripts) page.

Our approach is therefore: **read** the start tooling, take over only the parts you need (paths,
arguments, environment variables, order), and then start the game yourself. For this title nothing
came out of the tooling that we needed: `Game.exe` starts directly, and the two problems you would
reach for a start script for — the folder name with a trailing space, and getting our plug-in into the
game — are solved by the view and the doorstop (sections 4 and 10).

## 4. The route we use

**No loader and no injection.** This is the simplest route in our project and at the same time the
reason this title met the bar first: the game is started **directly by Proton**, and the only thing
that gets into the game's process is a **plug-in** loaded through the doorstop.

The chain, with the exact files (**MEASURED** in the scripts):

```
scripts/play-halo.sh --profile RabbidsHollywood
   |- sees the profile in packaging/aagames-games.json and goes to
scripts/play-unity.sh --game RabbidsHollywood
   |- python3 -I scripts/aagames-setup.py RabbidsHollywood    # build the view and the prefix
   |- systemctl --user start ravengund                        # the engine must be running
   |- ravengunctl arm                                         # arm the output (the virtual gun)
   |- systemd-run --user --unit=ravengun-tpui \
   |     --working-directory=<view> \
   |     <GE-Proton>/proton run Game.exe -popupwindow
   |- ravengunctl aagames start RabbidsHollywood              # the engine becomes the TCP client
```

- **Loader:** `GE-Proton11-7-x86_64` (DXVK), called as `proton run`. There is **no** separate
  loader library and **no** injected core in the game's process. **MEASURED.**
- **Inside the game:** BepInEx 5.4.21.0 (Mono, x64) with the doorstop (`winhttp.dll` +
  `doorstop_config.ini`), plus our plug-in `RavenGun.RabbidsHollywood.dll`.
- **Direction of the connection (section 5):** the plug-in inside the game is the TCP **server** on
  `127.0.0.1`, and **our engine is the client**.

**Environment and arguments** (**MEASURED** from the start scripts and the setup script):

| Name | Value | Why |
|---|---|---|
| `WINEDLLOVERRIDES` | `winhttp=n,b` | **Required**: without this override Wine does not load the doorstop (`winhttp.dll`) and no plug-in gets into the game |
| `WINEDEBUG` | `-all` | No noise in the log while playing |
| `STEAM_COMPAT_DATA_PATH` | `~/.local/share/ravengun/prefixes/unity-aagames` | The Wine prefix of this route |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | `~/.local/share/Steam` | Needed for the Proton call |
| `RAVENGUN_UNITY_PLUGIN` | `ravengun` | Which plug-in goes into the view; the fallback is the `demulshooter` route |
| `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`, `XAUTHORITY` | from the session | So that a start from a terminal or from the app lands on the owner's screen |
| Argument to the game | `Game.exe -popupwindow` | Borderless full-screen window |

The resolution is not set with an argument but by the plug-in: `FORCE_RESOLUTION=1`, `WIDTH=1920`,
`HEIGHT=1080`, `FULLSCREEN=1` in its ini. **MEASURED.**

**The view** (**MEASURED**). The setup script rebuilds
`~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood` at every start: links to every file
from the share, `Game_Data` as a real folder with links, and locally our loader, the doorstop and our
plug-in. The game process starts with the **view** as its working directory, not the share folder —
which is also the solution to the trailing space in the folder name (section 10.2).

**Drop-ins, with one honest limitation.** Our launcher has a general drop-in mechanism: a per-file
section in a `dropin.ini` names a destination in the view, a library override, environment variables
and whether the file must be loaded into the game (see the
[Loader architecture](Loader-Architecture) page). **On this route no drop-in is needed in this round:**
what the game needs comes from our plug-in. Two things to know if you want to place something here
yourself:

1. **MEASURED** in the source: the drop-in engine sits in the **direct** start path
   (`scripts/launch-direct.py`); the Unity start script does **not** read `dropin.ini` and only sets
   `WINEDLLOVERRIDES=winhttp=n,b`. A drop-in is therefore not applied on this route.
2. **MEASURED:** `Game_Data` is a real folder full of links in the view, so a drop-in that names
   `Game_Data/Plugins` points at a **link to the share**. Whoever places a file there writes onto the
   share. The engine should make that folder local first; our own start scripts do not do that on this
   route yet. That is a general limitation of this setup and not a property of the game.

**Stopping.** One command stops the unit the game runs in, which is all that is needed.

**Checking it yourself** (**MEASURED** commands):

```sh
# build the view (and see which path it picks)
python3 -I scripts/aagames-setup.py RabbidsHollywood --print-view

# start
scripts/play-halo.sh --profile RabbidsHollywood

# watch the plug-in load inside the game
grep -a -E 'Loading \[RavenGun|listening on 127.0.0.1|Daemon connected|Input from the daemon' \
  ~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood/BepInEx/LogOutput.log

# the connection and the rate
ravengunctl status          # a line about the Unity game: CONNECTED, ~120 packets/s

# stop
scripts/play-halo.sh --stop
```

## 5. Input

There are **two** input worlds for this title, and it is important not to mix them up:

1. **The cabinet dialect** — how the real cabinet gives the gun to the game. The `WartranTroopers`
   profile describes it as a bit field plus one X and one Y byte per player (1 byte per axis).
   **MEASURED** from the profile. We do **not** use it on our route.
2. **The `unity-tcp` dialect** — what we use in practice: a small binary protocol between a plug-in
   **inside** the game and our engine. That is what someone needs who wants to drive this title
   without the commercial runtime, so it is described here in full.

### 5a. The `unity-tcp` protocol (**MEASURED**, byte for byte)

Roles: the **plug-in inside the game is the TCP server** on `127.0.0.1`, by default on **port 33610**;
the **engine is the client**. There is one client at a time. Everything is little-endian, with no
padding.

**Input, client → plug-in.** One packet of fixed length per message, **without a length and without a
header**. The fields are in **alphabetical order of the field name** and arrays have four elements
(player 1 to 4). For this game the packet is **38 bytes**:

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0–15 | `Axis_X` | `float32[4]` | Aim X per player, in **screen pixels** of the game window: `0 .. width-1` (at 1920: `0..1919`) |
| 16–31 | `Axis_Y` | `float32[4]` | Aim Y per player, in screen pixels, with the **Y origin at the bottom**: `0 .. height-1` |
| 32 | `EnableInputsHack` | `u8` | **Must be 1**; when it is off the plug-in falls back to mouse and keyboard |
| 33 | `HideCrosshairs` | `u8` | Hide the game's own crosshair |
| 34–37 | `Trigger` | `u8[4]` | Trigger per player, 1 = pressed |

Two things you must know here:

- **There is no `Reload` field in this game.** A sister title in the same protocol has one and
  therefore comes to **42 bytes**: there `Reload u8[4]` sits at 34–37 and `Trigger u8[4]` at 38–41.
  Whoever supports both games has to switch on the length or on the game.
  **MEASURED** in our implementation and in the plug-in's source.
- **The Y origin is at the bottom** (Unity's `Input.mousePosition`). Convert accordingly: if `v` is
  your aim as a fraction **from the top** (0 = top, 1 = bottom), then
  `Axis_Y = (1 - v) * (height - 1)`. The game accepts that and puts the crosshair at
  `height - 1 - Axis_Y ≈ v * height` pixels from the top edge. **MEASURED:** an aim point of
  `u=0.3, v=0.3` put the crosshair at **576/324** pixels from the top left (0.3 × 1920 = 576;
  0.3 × 1080 = 324). The right edge is `width - 1`.

**Output, plug-in → client** (**MEASURED**). Each message: an `int32` length (= content + 1, so
**61**), then a `u8` message type (**2** = output), then **60 bytes** of content. Again alphabetical by
field name, and again the order differs per game:

| Order | Field | Type | Meaning |
|---|---|---|---|
| 1 | `Ammo` | `i32[4]` | Ammunition per player |
| 2 | `Credits` | `i32[4]` | Coins per player (quarters) |
| 3 | `Damaged` | `u8[4]` | One-shot flag: this player was hit |
| 4 | `Life` | `float32[4]` | Health bar |
| 5 | `Recoil` | `u8[4]` | **One-shot flag: this message is a shot** |
| 6 | `StartLamp` | `u8[4]` | Start lamp per player — **in this game this field is last** |

Note the last one: in the sister title that position holds a different field and `StartLamp` is not
there at all. Whoever reads the output has to know the field order per game; the plug-in reports its
own order in its log. `Recoil` and `Damaged` are **one-shot flags**: every message with
`Recoil[p] != 0` is a shot, which we tie to the vibration of the Move.

**Rate and source** (**MEASURED**). We send a fixed **120 packets per second** (a per-game setting)
instead of sending on events: a lost packet then costs nothing, and the plug-in needs repeated values
(trigger held) anyway. The content of `Axis_X`/`Axis_Y` is the absolute axis position of that player's
virtual gun as a fraction **0..1**, read at the moment of sending — that is, after our own
transformation and prediction, and with the same viewport and inversion a commercial front-end would
see. A player without an armed gun gets the centre and no buttons.

### 5b. What we measured about aiming and the buttons

- **The crosshair follows the requested point.** `u=0.3, v=0.3` → crosshair at **576/324** px; the
  bottom Y origin is therefore **measured** for this game, not assumed. **MEASURED.**
- **Our own output layer at full scale.** `ravengunctl test-pattern point 0.3 0.3` reported
  `aim 0.300,0.300 -> abs 9830,9830` and `0.7 0.7` reported `0.700,0.700 -> abs 22937,22937`. Our
  absolute axis therefore runs over **0..32767** (0.3 × 32767 = 9830; 0.7 × 32767 = 22937), and the
  conversion to screen pixels happens in the client. **MEASURED.**
- **Coins and start, with the numbers.** Coins are counted in **quarters** and the default keys are:
  **5** = coin for player 1 (and 6/7/8 for players 2–4), **1** = start for player 1 (2/3/4 likewise),
  **0** = test, **Esc** = quit. In the ten-minute run, with the keys going to gamescope's own
  Xwayland:

  ```
  [23:12:27] State: credits 2.0 ammo 0 life 100 playing 0
  [23:12:27] State: credits 3.0 ammo 0 life 100 playing 0
  [23:12:27] State: credits 4.0 ammo 0 life 100 playing 0
  [23:12:27] State: credits 4.0 ammo 0 life 100 playing 1     <- start accepted
  [23:12:27] State: credits 0.0 ammo 0 life 100 playing 1     <- coin consumed
  ```

  `playing` is our reading of `StartLamp`; four coins and then start works exactly as on the cabinet.
  **MEASURED.**
- **There is no `Reload`.** This game does not reload over the channel and does not read a `Reload`
  byte. **MEASURED** in the protocol and in the plug-in.
- **What we did NOT measure:** the **trigger and the vibration of a real Move** (no Move was connected
  in the ten-minute run), and the crosshair **visually** on a screen (sections 7 and 8).
- **Players 2–4.** The protocol and the packet support four players, and the game accepts "Insert
  credits" per player, but we only exercised player 1. **INFERRED** that 2–4 work.

## 6. Timing

All times are **seconds after the unit starts**, on a Steam Deck under Proton. **MEASURED** unless
marked otherwise:

| Moment | Value | Source |
|---|---|---|
| Creating a clean prefix for the first time | **~30 s** (Proton makes the prefix) | Documented procedure |
| Unit start → `Game.exe` visible (288 s run) | **~3 s** (21:59:34 → 21:59:37) | Lifeline log |
| Unit start → `Game.exe` visible (608 s run) | **~16 s** (**INFERRED**: 23:21:10 minus 608 s = 23:11:02, and the unit started at 23:10:46) | Same log, calculated back |
| `Game.exe` → coins and start accepted | **~78 s** (**INFERRED**: 23:12:20 minus 23:11:02) | Same log |
| Game to the title screen and the attract loop | seconds; no separate measurement | — |

**A warning about cold and warm — this is not a detail.** The two runs above do the same thing (start
the unit, wait for `Game.exe`) and give **3 s** and **16 s**: a factor of five on a step you would
happily write down as "the start-up time". The difference is in the state of the machine (prefix,
caches, whether the plug-in was already in the view) and not in the game. **INFERRED** for the cause,
**MEASURED** for the difference itself.

Two rules follow from that, and we have taken them up as project rules:

1. **Always say whether a run was cold or warm.** A time comparison without that says nothing.
2. **Never compare an instrumented run with a non-instrumented one.** From another title in this set it
   was measured that a full file trace (`WINEDEBUG=+file`) kept a process alive for **25–30 s** where
   it normally lived 5–7 s. The instrument changes what you measure.

## 7. What we saw on screen

An honest distinction is needed here: **the ten-minute run that section 8 rests on was headless.** The
game got an Xwayland display with no physical output, so that the owner's TV stayed free. In that run
there is therefore **no screen** and there are **no screenshots**.

**What was seen on the TV in earlier runs** (**MEASURED** in those runs, not in the ten-minute run):

1. the **title screen**, in English;
2. **"Insert credits" per player**, with coins in quarters ("1/4 credits");
3. after **four coins and Start**: the **loading screen**;
4. then the **game**, with a **crosshair**, a **weapon** and a **health bar**.

**What the ten-minute run itself produced** (**MEASURED**, but from state readings and the log, not
from a picture):

| Time | What happened | How we know |
|---|---|---|
| 23:10:46 | The unit starts; the view is built and the plug-in is installed | Lifeline log |
| ~23:11:02 | `Game.exe` appears (**INFERRED**, section 6) | Calculated back from the game clock |
| 23:11:02–23:12:20 | Attract loop; the game sits on the title screen waiting for coins | The engine reads `credits 2` at its first look |
| 23:12:20–23:12:27 | Four coins and start accepted: `credits 2 → 3 → 4`, `playing 1`, coin consumed to 0 | State lines in the log |
| 23:12:27 → 23:21:10 | **A real level**, not the attract loop: the health bar ran down `100 → 44 → 35 → 25 → 15 → 4 → -6`, the player died, and the game returned cleanly to attract (`playing 0`, life back to 100) | State lines in the log |
| 23:21:10 | Stop moment: `Game.exe` (pid 504342) was **still alive**, and was stopped after that | Lifeline log |

The state machine ran **menu → demo → play → death → attract**, visible in the log as the game's own
`OnDemoStateExit` / `OnMenuStateEnter` / `OnDemoStateEnter` lines. **MEASURED.**

That is the difference between "the process is alive" and **"the game is playable"**. But note what we
do **not** know from it: whether the **crosshair** is in the right place on a real screen. What we do
measure is that the aim is live (before any intervention our output read `aim 0.543,0.498`, drifting
slightly to 0.546/0.535) and that an explicit point comes out exactly (section 5b); the **picture**
itself is not confirmed. A run on a real display has to bring those two together.

## 8. Current state and open questions

**The bar for this title:** ten minutes running on our own route. **That is met.**

### 8.1 What was measured, and with what

| | |
|---|---|
| Harness | Script `~/rg_10min.sh`; systemd user unit **`rg-10min`** |
| Log | `~/rg-10min/main.log` (lifelines every 30 s) |
| Picture | **Headless** through `gamescope --backend headless`, 1920×1080 — the owner's screen stayed free |
| Stop criterion | **600 s of player time**, with a hard limit of 720 s |
| Window | Start **2026-10-12 23:10:46**, end **23:21:10** |

### 8.2 The numbers (**MEASURED**)

| Measurement | Outcome |
|---|---|
| **Player time** | **608 s** (10 min 8 s) — the log reports `t=608s STOP MOMENT (600s) reached` |
| **End** | **Stopped by itself**, not crashed: `Game.exe` (pid 504342, `-popupwindow`) was still alive at the stop moment, and was stopped after that |
| **Threads** | **115** in the game process at the stop moment |
| `Application.Quit` | **0** |
| Connection | **120 packets/s on all 20 lifelines**, never dropped |
| Process environment | **No commercial front-end process** in the cgroup (only Wine/Proton infrastructure and `Game.exe`) |
| The share | Over the whole window **0 entries newer than 23:10:46** — nothing was written to the share |
| Game content | Coins and start accepted, coin consumed, **a real level played** (section 7) |

The 8 s above 600 s comes from the 30 s tick of the lifeline: the game was neither stopped too early
nor too late. **MEASURED.**

### 8.3 Why this number means something

- The game **kept running** without any outside intervention: no restart, no input needed to stay
  alive, and the connection never dropped in ten minutes.
- The game was **played**, not merely started: a level was started and lost, and the game recovered to
  attract by itself. That is exactly what a player on the cabinet would see.
- The whole run happened **without any injected core in the process**: no loader library, no core, no
  injection — only Proton, the doorstop and our plug-in.

### 8.4 What this run did not exercise

- **The trigger and the vibration of a real Move were not exercised**: no Move was connected. The mouse
  mode can give a trigger, but then a physical mouse has to be moved, which is not possible headless.
- **The crosshair is not visually confirmed** (headless, no screen). What is measured is that the
  engine's aim is live and that an explicit point comes out exactly (section 5b).
- **One run.** A second ten-minute run would make the result stronger; the machine moved on to another
  title afterwards.
- **Player 1.** Players 2–4 were not exercised.
- **Sound and frame rate** were never judged or measured.

### 8.5 Open questions

1. **The crosshair on a real screen.** The next run should happen on the TV, with coins, start and an
   explicit test point, so that "it starts and lives" and "coin/start/crosshair" stand next to each
   other in **one** measurement.
2. **Trigger and vibration with a real Move.** That is the actual goal of the project: aiming with
   the Move. For this title it has not happened yet; the Move pipeline (Bluetooth → hidraw → our
   fusion → virtual gun → packet) has not been exercised end to end here.
3. **Players 2 to 4.** The protocol supports four players and the game asks for coins per player; we
   only did player 1.
4. **Game status is not kept.** The game's status (metrics, operator menu, options) is treated as
   invalid at every start and **reset**. That is not solved yet, and it is one of the few points where
   this title still behaves differently from a cabinet. What exactly goes wrong has not been
   established.
5. **One long session with input attached.** The ten-minute run stayed in attract and one level;
   whether something differs after long play (memory, a second level, a slow level change) has not
   been measured.

## 9. What we tried and what did not work

These are the things that cost us time or invited a wrong conclusion. They are here so that you can
skip the detour.

1. **We looked for a game that shut itself down, and it was an old binary in the view.** Our own route
   kept the game alive for about a minute at first, and we treated that as a property of the game for a
   long time. **MEASURED:** it turned out to be an installation problem — the view held an old plug-in
   (**32,768 bytes**) without the answer the game needed, while the fresh one (**34,304 bytes**) was
   correct. The same run went from ~70 s to **288 s** by only refreshing the plug-in in the view, and
   after that to 608 s (section 8). Whoever does not check the view measures the old binary.
   **What to take from it:** compare the **size and checksum** of what is in the view with what you
   built — not the timestamp, because that lies.
2. **Our own diagnostic tool next to the engine did not work.** It is not an injector but a **second
   TCP client** of the plug-in, and the plug-in keeps one client. As soon as the tool connected,
   connecting and disconnecting alternated: 10 connects and 10 disconnects, with "the game closed the
   connection" as the error. **MEASURED.**
   **What to take from it:** never run a second client next to a connected engine; stop the engine
   first, or send the test through the engine itself.
3. **Keys sent to the session compositor did not arrive.** In a headless gamescope the virtual keys
   from our own control did not reach the game: there was exactly one state line (`credits 0`).
   **MEASURED.** What **did** work was sending them to gamescope's own Xwayland
   (`DISPLAY=:1 xdotool key …`) — that is how coins and start were accepted in the ten-minute run
   (section 5b). **What to take from it:** without that difference you wrongly conclude that "coins and
   start do not work".
4. **An outcome you did not write down beforehand cannot be judged afterwards.** We therefore write the
   expectation down **before** the run, and a measurement that finds nothing has to show first that it
   looked. That has stopped a wrong conclusion three times in one week.
5. **A time measurement without "cold" or "warm" is unusable** — see section 6, where the same moment
   cost 3 s in one run and 16 s in another.

## 10. What is Linux-specific, and what differs on Windows

Everything runs on **Linux** (SteamOS on a Steam Deck) under **Wine/Proton**. For this title that is
manageable — there is no loader and no injection — but there are several things you have to arrange on
Linux and that a Windows builder does not meet. Per point whether it is **MEASURED** or an
**assumption**.

**10.1 The Wine prefix and the doorstop (**MEASURED**).** The game runs in its **own** Wine prefix
(`~/.local/share/ravengun/prefixes/unity-aagames`, made by Proton with DXVK). We deliberately do not
share that prefix with other titles: the doorstop override should not leak into another game's prefix.
The first time, Proton builds the prefix (~30 s); after that it is warm.
**`WINEDLLOVERRIDES=winhttp=n,b` is required** — without that override Wine does not load the doorstop
and no plug-in gets into the game. **Windows difference:** there `winhttp.dll` + `doorstop_config.ini`
simply sit next to the executable and the doorstop works without any override. That environment
variable is purely a Wine thing.

**10.2 The folder name with a trailing space (**MEASURED**).** The folder is called
`Rabbids Hollywood ` — **with a space after "Hollywood"**. Wine refuses that:
`Directory name invalid (0x10B)`. Our solution is a **local view with links** instead of starting the
share folder directly (sections 2 and 4). That is not a cosmetic detour: without the view the game does
not start. **Windows difference:** Windows tolerates the space, so the view/link layer is not needed
there. If you add a game on Linux, check for this — it cost us a session to find.

**10.3 The "core" in the game's process (**MEASURED**).** There is **no** injected core for this title.
The only things that enter the game's process are the loader's preloader and our **managed** plug-in,
loaded through the doorstop. We therefore need no equivalent of a commercial arcade core — and that is
exactly why this title met the bar first. **Windows difference:** the mechanism is identical; only the
way the files get into the game folder differs (through the view here, next to the executable there).

**10.4 Drop-ins: `WINEDLLOVERRIDES` and `LD_PRELOAD` (**MEASURED**, with one **assumption**).** Our
launcher has a general drop-in mechanism (section 4). On Linux it translates into Wine-specific
means: a library override becomes a line in `WINEDLLOVERRIDES`, and a "must be loaded into the game"
file becomes `LD_PRELOAD` of the Proton process. Neither concept exists on Windows: there you put the
file next to the executable, or you use the library search order. **assumption:** that this is the
intended working of the mechanism is read from the scripts and the documentation, not exercised on
this title — on this route no drop-in is needed and the Unity start script does not read `dropin.ini`
at all (section 4). Whoever wants to place a drop-in here has to solve those two points first.

**10.5 Virtual devices and udev (**MEASURED**).** Our "gun" on Linux is a **virtual absolute mouse
device** through `/dev/uinput` (evdev), plus a **virtual keyboard** for coin, start and test:

| Device | Id | What it is for |
|---|---|---|
| Virtual lightgun, player 1 | `1209:7202` | The aim point that the game reads as a mouse position |
| Virtual lightguns, players 2–4 | `1209:7203` … `1209:7205` | Exist only when that player has a Move |
| Virtual keyboard | `1209:72f0` | Coin (5–8), start (1–4), test (0), Esc — the plug-in reads keys |

In `/dev/input/by-id/` they appear as `usb-RavenGun_Virtual_Lightgun…`. The aiming device (the Move)
hangs on **hidraw over Bluetooth**; master addressing uses a HID `SET_FEATURE`, and SteamOS grants
hidraw access through its **own** rule (`60-steam-input.rules` with `uaccess`) — we did not need a rule
of our own. We keep our virtual gun deliberately **invisible** to the desktop and to gamescope with
`LIBINPUT_IGNORE_DEVICE`, otherwise the owner's mouse cursor moves along during play.
**Windows difference:** Windows has no `uinput`. You would need a virtual input driver (for example a
ViGEm-style one) or a real lightgun driver, and there is no udev or `uaccess` layer. What you gain
there is that there is no compositor to fight with (10.6).

**10.6 Gamescope and the owner's screen (**MEASURED**).** We use **gamescope** to make a session
without occupying the owner's screen: `gamescope --backend headless -W 1920 -H 1080`. The game then
gets an Xwayland display (`DISPLAY=:1`). Two things you meet here:

- the virtual keys from our own control do **not** arrive there (they go to the session compositor,
  which has no window); through gamescope's own Xwayland with `xdotool` they **do** (sections 7 and 9);
- there is therefore no picture to look at — a headless run cannot judge the game visually.

**Windows difference:** not applicable; a Windows session always has a desktop. But it is also the
reason that "on the TV" and "headless" are two different kinds of evidence for us.

**10.7 The settings layer (**MEASURED**).** Our launcher has two layers on Linux: a shipped per-game
file and a user file in `~/.local/share/ravengun/patches/<Game>/game.ini`, where the user layer wins
**per key**. That way you change one game without touching the shipped values, and deleting one file
puts it back. **Windows difference:** on Windows you would work with copies of profile files or with
the front-end's own user profiles; the per-key layering is our Linux shape of the same idea.

---

*Everything above was measured on a Steam Deck running SteamOS with Wine/Proton, with the game files on
network storage that is only read. Where a claim is labelled MEASURED there is a number, timestamp,
path or offset with it; where it is labelled INFERRED or assumption it is a conclusion or a guess. The
ten-minute run is one run; section 8.4 and section 9 state exactly what it did not exercise.*
