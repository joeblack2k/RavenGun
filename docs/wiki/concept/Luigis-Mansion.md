# Luigi's Mansion Arcade

*Sega Nu 1.1 cabinet. Windows x64. Route: Wine + OpenParrot loader + core. Status: the five-minute bar is **not met** with our own core.*

Everything on this page was measured on a Steam Deck (SteamOS) under plain Wine, with the game files on network storage that is only read. Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

The cabinet is a **Sega Nu 1.1**: a PC running Windows. The game is a 64-bit Windows program.

| | |
|---|---|
| Game file | `exe/x64/VACUUM.exe`, 3,436,544 bytes (**MEASURED**) |
| Kind | `PE32+ executable for MS Windows (GUI), x86-64` (**MEASURED**) |
| Graphics and input APIs | Direct3D 11, DirectInput (**MEASURED** from the file) |
| Picture | 1920x1080 |

The cabinet's controls are a lightgun combined with a vacuum handle: aim with the gun, pull the trigger to vacuum, and a separate button for the flash. A second player is possible. **DERIVED** from how the game behaves; we have never seen the cabinet.

## 2. What the game needs

### Folders and paths (**MEASURED**)

The game looks for its data **relative to its own folder**, under two different roots:

- `..\..\DataGlobal\EN\Data\...` — the actual game data (models, effects, sound, movies);
- `..\..\Data\...` — a second root the game also asks for, for example `Data\System\Model\cube.bin`.

| What | Why | Status |
|---|---|---|
| Working directory `exe/x64` | With the game's top folder as working directory it stops with an access violation | **MEASURED** |
| A folder `Data` **next to** `DataGlobal` | The game asks for `..\..\Data\...` | **MEASURED** |
| The movies in `Data/Movie/*.wmv` playable | Without an ASF demuxer and a WMV decoder the game exits at the first movie | **MEASURED** |
| Write access to `Ram/` and `Log/` | The game writes its settings, credits and logs there | **MEASURED** |

### What the cabinet prepares, and what turned out not to matter

The official start script prepares four things. We tested **each one separately**; see section 3.

## 3. The official start script

Next to the game is `game.bat`. Literally (comments in Japanese omitted):

```bat
setlocal
set GAMEID=SDCF
if not exist Y:\%GAMEID% (
    rmdir /S /Q Y:\
    mkdir Y:\%GAMEID%
)
start /wait sendFlash COM1 LMAIO.bin
start /wait x:\exe\x64\VACUUM.exe
```

**We never execute this script.** `rmdir /S /Q Y:\` wipes a whole drive if `Y:\SDCF` is missing. On the cabinet that is a dedicated data drive; on your machine `Y:` may be something of yours. See [Official start scripts](Official-Start-Scripts) for the general reasons.

What we did instead is test its four steps one at a time, with everything else unchanged (**MEASURED**, 2026-10-13, our own core, the core's SHA-256 checked against the loader's log each time):

| Step in the script | Result | What was measured |
|---|---|---|
| `set GAMEID=SDCF` | No effect | The run ends at the same moment with and without the variable |
| `sendFlash COM1 LMAIO.bin` | Inert | The tool exits with code 0 but never sends the 140,688-byte firmware file. It writes the same 7 bytes (`/i\n/e\n\x03`) five times, 35 bytes in total, and gets nothing back |
| Preparing `Y:\SDCF` | No effect | Same outcome with the folder present |
| Starting from `X:\exe\x64\` | No effect | Same outcome with the drive letter |

Also **MEASURED**: the game itself never touches COM1. With a pseudo-terminal on `com1` and a trace of all file operations there were 0 COM names in 86 file-open lines. The firmware step belongs to the separate tool, not to the game.

So none of the four steps of the cabinet's script explains the difference between our start and a working one. A table like this is worth making early: it removes a whole class of suspects.

## 4. The route we use

```
wine <loader> <core, without .dll> "<game.exe>"
```

- The loader is `OpenParrotLoader64.exe`, built from the public OpenParrot source (GPL-3.0). The game is 64-bit, so the loader and the core must be 64-bit.
- **The core is the loader's first argument**; the loader appends `.dll`. Two cores can therefore be compared by changing only that argument, with everything else identical. That is how we compare our own core with the cabinet's original one.
- The launcher can start the game from another path (`--exe 'x:\exe\x64\VACUUM.exe' --drive X=.`). Only the path the loader receives for the game changes; the rest of the setup stays identical, and the plan shows the override. **MEASURED**: with and without the override the result is the same.

Two pitfalls we measured, so you do not have to:

- **The loader's working directory is not the game's.** The loader sets the game's working directory correctly, but the loader itself must start in its own runtime folder, or the core looks for its own files in the wrong place. Set the game's working directory through the per-game setting, not with `cd`.
- **`TP_REMOTETHREAD=off` means: leave the variable out.** Its mere presence switches the loader to its remote-thread method, and that method does not work for this game. Setting it to `off` or `0` is still "present".

## 5. Input

The game gets its input from the board image in shared memory (see [Loader architecture](Loader-Architecture)), in the layout this family of games uses.

- **MEASURED** (2026-10-12, real controller): an aim of 0.8 / 0.3 gives the x byte 202 and the y byte 79 in the image, exactly as computed. The trigger sets bit `0x4`, the second button bit `0x2`, test bit `0x100` of the control field.
- **NOT MEASURED**: input reaching the game with our own core, because the game does not get far enough (section 9).

## 6. Timing

Do not read too much into single timings of this game.

- **Cold and warm cache**: the same moment in the start, on the same core, was measured at 4.8 s in one run and 6.0 s in another (**MEASURED**). That is more than a second on a process that lives about six.
- **Instrumentation changes the result**: with a full file-operation trace the game process lived 25–30 s; without it, 5–7 s (**MEASURED**). Never compare a run with tracing against a run without.

## 7. What we saw on screen

Nothing, by design: all runs on this page were **headless** (a display with no physical output), so that the TV stayed free.

With the cabinet's original core a picture was seen in earlier work on the same machine: title, attract mode, start, vacuuming, catching ghosts, the test menu. That is **DERIVED** from earlier notes, not from the runs reported here. A run with a picture on a real display is not comparable with the headless series: different pacing, sound, and the loading of movies and music falls in the same window that is being measured.

## 8. Linux notes

| Point | What is needed on Linux | Status |
|---|---|---|
| Wine | A plain Wine build (ours: `wine-proton-11.0-2`), started directly | **MEASURED** (the path of the Wine binary is in the run logs) |
| Prefix | One Wine prefix in which the game runs | **MEASURED** |
| Extra drive letter | A symbolic link in `<prefix>/dosdevices/`, for example `x:` → the game's folder | **MEASURED** |
| When to create that link | See below: two measurements disagree | **MEASURED**, unexplained |
| Movies | The `.wmv` movies go through Wine's GStreamer backend. Supply the ASF demuxer and the WMV decoder (`libgstasf.so`, `libgstlibav.so`) and point `GST_PLUGIN_PATH` at them | **MEASURED** |
| Writable folders | `Ram/` and `Log/` must be writable even if the game files are read-only | **MEASURED** |
| Read-only source | Make a **view**: a folder of symbolic links to all game files, plus `Data` → `DataGlobal/EN/Data` | **MEASURED** |
| A pitfall with links | The folder the game is **in** (`exe`) must be a real folder containing links, not one link to the source. A launcher that wants to place files there cannot write through such a link | **MEASURED** (this cost two runs) |
| COM1 | Wine maps `com1` to `/dev/ttyS0`. On a Steam Deck that device is `root:uucp 0660`, so a normal user gets "access denied" rather than "does not exist", and a program can tell the difference. To observe the port, put a pseudo-terminal on `dosdevices/com1` | **MEASURED** |

**The drive-letter link and the Wine server: two measurements that disagree.**

- In the prefix the game runs in, a link created **before** the Wine server started was **gone** afterwards. Creating it after the server was up worked (**MEASURED** by the game specialist; it cost a measurement).
- In a separate test prefix, a link created before any Wine process started was still there afterwards, and a program started from it (**MEASURED**, 2026-10-13 11:16).

Which condition makes Wine remove the link is **NOT MEASURED**. Our launcher therefore checks the link again after its first Wine process is running, re-creates it if it is gone, and does not start the game if the letter is still wrong.

On Windows none of this applies: drive letters are real, links persist, the system's own codecs play the movies, and COM1 is a real or virtual serial port. That is an **assumption**; we did not test on Windows.

## 9. Status

**The bar**: five minutes of attract or game mode. **Not met with our own core.**

| | |
|---|---|
| With the cabinet's original core | The process was alive after **300 s**, with 92 threads (**MEASURED** baseline) |
| With our own core | The game starts, and its process ends after about 5–7 s (**MEASURED**) |
| Ruled out as the cause | The four steps of the start script (section 3), the paths and working directory (section 2), the input layer |

The remaining difference is in how the game's own start-up code interacts with the core. That work is ongoing and is not described here.

## 10. Open questions

1. **Which condition makes Wine remove a `dosdevices` link at server start?** Next step: the same test in both prefixes, with and without a prefix update pending.
2. **Does a run with a picture behave the same as a headless one?** Not measured for this game.
3. **Does the game read `GAMEID` at all?** With our core it makes no difference to the outcome, but the game does not get far enough to say it is never read.
4. **Input with our own core**: untested until the game stays up.

---

*Sources: the game specialist's digest (2026-10-13) for sections 1–4 and 6–9, the loader side of the project for sections 4, 5 and 8. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
