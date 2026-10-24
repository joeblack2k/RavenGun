# Loader architecture

This page describes how RavenGun starts a game and gets input into it. It is the material you need if you want to build your own front-end for these titles. Where a statement is tagged, the tags are those from the [Home](Home) page.

## The idea in one paragraph

An arcade game expects a cabinet: an I/O board with switches and guns, fixed drive letters or paths, a start script that prepares the environment. On a PC none of that exists. A **route** supplies it. Every route has the same three parts: something that **starts** the game the way that game expects, something inside or beside the game that **answers its I/O**, and a **bridge** from the engine (which reads the real controllers) to that I/O. The per-game facts live in data files, one folder per game; the launcher itself contains no game names.

## The routes

| Route | For | How the game is started | How input arrives |
|---|---|---|---|
| **Wine + loader + core** | Windows games | `wine <loader> <core> <game.exe> [one parameter]`. The loader creates the game process suspended, loads the core into it, then lets it run. | The core reads the board image from shared memory. |
| **Wine + ELF loader** | Linux games started through a Windows-side ELF loader | `wine <elf-loader> <game>`. The loader maps the Linux binary into its own process. | The same board image. |
| **Native, sandboxed** | Linux games run directly | The game binary runs in a `bubblewrap` sandbox that shows it the paths it expects (`/pm/...`), read-only. | An input helper library inside the game copies the board image into the game's own I/O structure. |
| **Native, own ELF loader** | 32-bit Linux games whose binaries the system loader cannot start as they are | Our loader (`rg-elfloader`) maps the binary and its libraries and jumps to its entry point, inside the same sandbox. | As for the native route. |
| **Unity plug-in** | Unity games | The game is started under Wine with a plug-in loaded by a plug-in framework. | The plug-in receives the gun state over a local TCP connection. |

Which route a game uses is on its own page. A game can have more than one working route; the pages say which one each measurement used.

## The launcher: plan, check, run

The launcher for the Wine routes is one script, `scripts/launch-direct.py`, with three modes:

```
launch-direct.py <Profile> --plan     # print exactly what would be started; start nothing
launch-direct.py <Profile> --check    # exit 0 if the start is possible, non-zero with the reason if not
launch-direct.py <Profile> --run      # do it
```

**The plan is the contract.** Everything the run will do is in the plan first, in readable lines and as JSON (`--plan --json`). If something cannot work, the plan says so and `--check` and `--run` refuse; the launcher does not start a game in a setup it knows to be incomplete. This rule came from experience: three measurements were lost in one week to runs that continued with a warning.

What the plan shows:

| Plan item | What it tells you |
|---|---|
| `launcher:` | The path and SHA-256 of the launcher file itself, whether it is the copy in the source checkout or the installed one, and whether the two differ. |
| `argv`, `cwd`, `prefix` | The exact command line, the launcher's working directory and the Wine prefix. |
| `spel:` | The path the loader receives for the game, and whether it is an override. |
| `argumenten voor <exe>:` | The arguments the game receives, as the game will see them, and from which setting they come. |
| `omgevingsvariabelen voor het spel:` | The per-game environment variables. |
| `start 1/2`, `wachten`, `start 2/2` | For a game with two programs: the order and the wait between them. |
| `drop-in:` | Every file that will be placed in the view, and through which channel a library is loaded into the game. |
| `eigen startscript van het spel:` | What the cabinet's own start script does that we do not (see [Official start scripts](Official-Start-Scripts)). |

(The line labels are Dutch; the project's internal documentation is in Dutch.)

Exit codes: `0` done, `1` error during the start, `2` usage, `4` this profile cannot be started this way, `5` a per-game setting, patch or drop-in was refused, `7` one of two programs was gone before the other started. Any other value is the exit code of the game's loader.

## Per-game data

Everything specific to a game is in `patches/<Game>/game.ini` (shipped, with a user layer on top). The launcher reads it; nothing game-specific is compiled in. The parts that matter for starting a game:

| Setting | Effect |
|---|---|
| `[Env] NAME = value` | The variable is in the environment of the game process, name and value unchanged. Default: none. |
| `[Launch] Arguments = ...` | Arguments for the game, written as on a command line. Default: none. |
| `[Launch] WorkingDirectory` | The game's working directory, if it must differ from the folder of the executable. |
| `[Launch] Exe`, `[Launch] Drives` | Start the game from a different path, for instance from a drive letter, and map drive letters for the run. |
| `[Launch] SecondWait`, `SecondArguments` | For a game with two programs: the wait between the two starts and the arguments of the second. |
| `[Launch] StartScript` | Which of the game's own start scripts to read (not execute) for the comparison in the plan. |
| `[Input] Route`, `Layout` | Which input bridge and which board layout. |

### Arguments: the loader takes exactly one

The OpenParrot loader accepts `<core> <game.exe>` and **one** further parameter, which it pastes verbatim after the game's path to form the game's command line: `"game.exe" <parameter>`.

- **READ** (loader source, `OpenParrotLoader/src/main.cpp`): the argument count must be 3 or 4; with more, the loader prints its usage text, waits for a key and returns 0.
- **MEASURED** (2026-10-13 10:28, the real loader under Wine, with a game path that does not exist so that nothing starts): one parameter containing quotes, a space inside a quoted path and a backslash before a quote arrived **byte for byte**; two separate arguments produced the usage text and **exit code 0**.

So a front-end must hand the loader all game arguments as a single string. Passing them as separate arguments looks like a successful start (exit code 0) and starts nothing. Our launcher now always combines them; before that, any profile with two or more arguments failed silently.

### Environment variables

- **MEASURED** (2026-10-13 10:51, Wine, a test prefix): eight variables set in the Unix environment arrived unchanged in the Windows process and in a child of that process, including a value with spaces and quotes, a lower-case name, an empty value, and `X:\` as a value. Wine does not rename or translate them.

### Two programs

Some games are two programs: a background service and the game. The launcher starts them in a fixed order (from the profile), waits a configurable time between them (default 5 s), and gives both the same Wine build, loader, core, working directory, prefix and environment. Output and exit code are stored per program. If the first is gone before the second starts, the run stops with exit code 7. The loader process may exit before the program it started (the OpenParrot loader does); the launcher therefore also looks for a live process with the program's name before it calls a start dead.

- **NOT MEASURED**: a complete two-program game run through this launcher. The logic is covered by tests that replace only the process start.

## Views: the game files stay where they are

Our game files live on network storage. The launcher never copies a game. It works in a **view**: a local folder of symbolic links to the game's files. Files of ours (a configuration file for the core, a drop-in library) are real files in the view. If a folder on the way is itself a link, the launcher turns that one folder into a real folder of links first, so that nothing of ours is ever written into the game's own folder.

Two consequences for a front-end:

- Check where a destination **really** ends up (`realpath`), not what its path looks like. A path can look local and resolve to the network share through a link above it. This exact case cost us two runs.
- A game writing its own saves and settings into its own folder is normal and is left alone. The rule is only about files that are ours.

## Drop-ins

A drop-in is a file the launcher places in the view before the start, declared in `patches/<Game>/dropin/dropin.ini`: where it goes, whether Wine should prefer it over its built-in library of the same name, which environment variables come with it, and whether it must be loaded into the game process.

"Loaded into the game process" needs a different channel per route, and choosing the wrong one fails without an error:

| Route | Channel | Notes |
|---|---|---|
| Windows game under Wine, OpenParrot loader | `TP_PRELOAD_DLL=<Windows path>` | The loader loads this one library into the game after the core. One library only. **READ** in the loader source; before we used this variable the library never arrived in the game (**MEASURED**: 0 of 2 files in the process's memory map). |
| Linux game under the Windows-side ELF loader | `LD_AUDIT=<path>` | `LD_PRELOAD` is fatal here: the ELF loader is a Cygwin program and exits with "fatal error - error while loading shared libraries" (**MEASURED**, 2026-10-11). |
| Native Linux game | `LD_PRELOAD=<path>` | Works as usual. |

The plan names the channel for each file and says so when a file would not be loaded by anything.

## Input: from controller to game

```
PS Move (Bluetooth) -> engine (ravengund) -> I/O state -> a dialect per route -> the game
```

The engine fuses the controller's motion sensors into an aim point and keeps one model of a cabinet I/O board: per player an aim (x, y in 0..1), trigger, reload, grenade, start; plus coin, test, service. A **dialect** writes that model in the form a route expects.

| Dialect | Form | Used by |
|---|---|---|
| `tp-shm` | A 64-byte block in shared memory (`/dev/shm/...`). A bit field holds the switches and buttons; each aim axis is one or two bytes at a fixed offset. The layout differs per family of games and is described in a data file (`packaging/tp-shm-profiles.json`). | The Wine routes with a core, and the native route. |
| `unity-tcp` | Gun state over a local TCP connection to a plug-in in the game. | Unity games. |
| `hid-board` | A virtual USB joystick that looks like the cabinet's gun board. | Games that look for their board as a HID device. |
| `jvs` | The engine acts as a JVS board on a serial line. | Games that speak JVS to a COM port. |

Besides the dialects, the engine always offers a virtual lightgun (absolute pointer with buttons) and a virtual keyboard through `uinput`, for games that read ordinary input devices.

### The board image, concretely

For the four-player shooter layout that [Halo: Fireteam Raven](Halo-Fireteam-Raven) uses (**READ**, `packaging/tp-shm-profiles.json`; other games use other layouts from the same file):

- a 32-bit little-endian bit field at offset 8: bit 0 test, 1 service, 2 coin 1, 3 coin 2, 4 volume up, 5 volume down, then five bits per player: start, trigger, reload, grenade, auxiliary (player 1: bits 6 to 10, player 2: 11 to 15, and so on);
- eight axis bytes at offsets 12 to 19: player 1 x, player 1 y, player 2 x, ... One byte per axis, 0 is left and top.

The **off-screen rule**: inside the screen an axis stays within 2..253; a value of 0, 1, 254 or 255 means the player is aiming off-screen (used for reloading). The conversion is: clamp the fraction to 0..1, **round first** (`round(min + f*(max-min))`), **then invert** if the layout says so, **then clamp** to the margin, unless the player is off-screen, in which case the extreme value stays.

- **MEASURED** (2026-10-12, real controller): 88 reports per second over Bluetooth, 0.66 % missed by sequence number, yaw within ±0.4° over one minute at rest. An aim of 0.8 / 0.3 in another layout (the one Luigi's Mansion uses) gave bytes 202 and 79, exactly as computed, and the trigger set its bit.
- **NOT MEASURED** in that session: real button presses, and anything with the camera (the tracking sphere was not in view).

On the Wine side a small Windows program mirrors the Linux shared-memory file into the named Windows mapping the core opens.

### Native games: the input helper

A native game reads its switches, gun buttons and aim from its **own** I/O structure in memory, filled by a board library built into the game. It does not read the system's input devices. This is the most important thing we learned about the native route: a virtual lightgun that sent perfect positions and trigger presses changed nothing on screen, because the game was listening on a different channel (**MEASURED**, Halo, 2026-10-13).

The input helper (`librg_rt_input.so`, preloaded into the game) reads the board image and writes the values into the game's memory where the game reads them. **What goes where is a table per game**, not code:

```
build       getter=0x... signatuur=...                    # only act on this exact build
schakelaar  bit=6 bord=0 k=0 puls=300                     # switch: a 300 ms pulse
schakelaar  bit=7 bord=0 k=9 puls=0                       # button: held while the bit is set
bit         bit=7 adres=0x... breedte=4 doelbit=0         # or: a bit in a word of the game
as          bron=12 adres=0x... soort=u8 min=0 max=255 marge=2
```

A table that does not parse, or names an address that is not writable in the process, is rejected as a whole and the helper then does nothing. Writing to a wrong address in a running game is worse than no input.

## What every log records

The first lines of every run log say which code ran:

```
launcher: /.../scripts/launch-direct.py
  the copy from the source checkout (commit ..., with uncommitted changes); sha256 ...; helper modules (5) sha256 ...
  the installed launcher (/.../launch-direct.py) is a DIFFERENT version (sha256 ...)
```

The native start script does the same and adds the SHA-256 of the game data file, of our own libraries that go into the sandbox, and of any file passed in for that run.

The reason is the most expensive lesson of the week: **three measurements were lost to a stale copy nobody knew was running** (an old ELF loader in the install folder, an old library, an old installed launcher). Each time the source had been fixed and the run still showed the old behaviour. A build step that leaves the installed file unchanged now fails; and every log names the file that ran.

## Lessons for anyone building a front-end

1. **Say which launcher ran.** See above. A hash in the log costs nothing and ends a whole class of confusion.
2. **The loader must be the one that creates the game process.** It creates the process suspended and loads the core before the game's first instruction. Anything else that starts the executable — the cabinet's own start script, a wrapper, a shell program that launches the real game — produces a game without the core. That is why we read start scripts and do not run them.
3. **A start that returns 0 may have started nothing.** The loader's usage path returns 0. Check for the game process, not for the exit code.
4. **Do not compare timings across setups.** Cold versus warm file cache changes a start by more than a second on a process that lives six (**MEASURED**, see [Luigi's Mansion](Luigis-Mansion)); heavy tracing changed a lifetime from 5–7 s to 25–30 s. A run with a picture on a real display is paced by that display; a headless run is not.
5. **Follow the level, not the edge, for buttons.** A switch such as coin or start is a short pulse. A trigger must stay pressed as long as it is held.
6. **Decide what the files decide; do not guess the rest.** A start script chooses its executable with `if exist`. That can be decided from the game folder. An error level or a file on another cabinet drive cannot; report it as conditional.
7. **Refuse loudly.** A setup you know to be incomplete should not start. A warning scrolls past; a refusal gets fixed.

See also: [Official start scripts](Official-Start-Scripts), [Running on Linux](Running-on-Linux).
