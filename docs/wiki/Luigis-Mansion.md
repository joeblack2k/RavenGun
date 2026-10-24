# Luigi's Mansion Arcade

*Sega Nu 1.1 cabinet. The game is a 64-bit Windows program. Our route: Wine, the OpenParrot loader and
our own core. The five-minute bar is **not met**: the game ends about 5–7 seconds after the start.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS under plain Wine, with the game files on network storage that is only read.

## 1. Platform and cabinet

The cabinet is a **Sega Nu 1.1**: a PC running Windows, with a normal 1920×1080 display. The game is a
64-bit Windows program. **MEASURED** from the file:

| | |
|---|---|
| Game file | `exe/x64/VACUUM.exe`, 3,436,544 bytes |
| Kind | `PE32+ executable for MS Windows 6.00 (GUI), x86-64`, 8 sections |
| Image base | `0x140000000` |
| Entry point | RVA `0xf23125` (VA `0x140f23125`) |
| Subsystem and APIs | GUI; Direct3D 11, DirectInput |

The cabinet's controls are a **lightgun combined with a vacuum handle**: aim with the gun, pull the
trigger to vacuum, and a separate button for the flash attachment. A second player is possible.
**INFERRED** from how the game behaves; we have never seen the cabinet.

**The game's code is not in the file as plain bytes. MEASURED:** five sections (`.text`, `.rdata`,
`.data`, `.pdata`, `.capcom0`) have a large virtual size and **zero bytes in the file**; only
`.capcom1` (3,434,496 bytes at file offset `0x400`, entropy 7.9553) is present. The entry point lies
**inside** `.capcom1`, so the program begins in start-up code that rebuilds the rest of the image
rather than in the game itself. The data directories confirm it: `import` (RVA `0x10980d8`), `export`
(`0x121cc00`), `exception` (`0x122eb70`), `iat` (`0x120d000`) and `loadconfig` (`0x122eb00`) all point
into ranges whose bytes are not in the file, so there is no usable import table to read statically
either.

## 2. What the game needs

### Folders and paths (**MEASURED**)

The game looks for its data **relative to its own folder**, under two different roots:

- `..\..\DataGlobal\EN\Data\...` — the actual game data (models, effects, sound, movies);
- `..\..\Data\...` — a second root the game also asks for, for example `Data\System\Model\cube.bin`.

With the game's top folder as the working directory it stops with an access violation, so the working
directory must be `exe/x64`.

| What | Why | Status |
|---|---|---|
| Working directory `exe/x64` | Otherwise the run ends in an access violation | **MEASURED** |
| A folder `Data` **next to** `DataGlobal` | The game asks for `..\..\Data\...` | **MEASURED** |
| The movies in `Data/Movie/*.wmv` | Without an ASF demuxer and a WMV decoder the game exits at the first movie | **MEASURED** |
| The GStreamer plug-ins `libgstasf.so` and `libgstlibav.so` | Same reason; supplied through `GST_PLUGIN_PATH` | **MEASURED** |
| Write access to `Ram/` and `Log/` | The game writes its settings, credits and logs there | **MEASURED** |

### What the cabinet prepares, and what turned out not to matter

The official start script sets up four things. We tested **each one separately**, with everything else
unchanged; three of them make no measurable difference on our setup and the fourth is inert. The
measurements are in section 9, together with how each step was isolated.

## 3. The official start script, or why there is none

Next to the game is `game.bat`. Literally (the comments in the file are in Japanese):

```bat
@echo
setlocal
rem <game ID>
set GAMEID=SDCF
rem <exclusive use of the app data area>
if not exist Y:\%GAMEID% (
    rmdir /S /Q Y:\
    mkdir Y:\%GAMEID%
)
start /wait sendFlash COM1 LMAIO.bin
start /wait x:\exe\x64\VACUUM.exe
```

**We never execute this script.** It does three destructive or fragile things:

1. `rmdir /S /Q Y:\` wipes an **entire drive** if `Y:\SDCF` is missing. On a cabinet that is a
   dedicated data volume; on an ordinary machine `Y:` may be yours.
2. `setlocal` without a matching `endlocal`, combined with `start /wait`, asks for a restart of the
   environment; the script is written for a cabinet that reboots anyway.
3. It throws away the user's app data area (the `%GAMEID%` folder) instead of keeping it.

We take over only the parts a title actually needs — the working directory, the data roots and the
movies — and not the wipe, not `sendFlash` and not the environment variable. Section 9 shows why that
is justified. The general reasoning is on the [Official start scripts](Official-Start-Scripts) page.

## 4. The route we use

We start the game with a loader built from the public OpenParrot source (GPL-3.0) and **our own core**:

```
wine <loader> <core, without .dll> "<game>.exe"
```

- **The core is the loader's first argument**; the loader appends `+ ".dll"`. Our core and the
  cabinet's original core are therefore interchangeable by changing only that argument, which is how
  they were compared fairly.
- The loader supports an **override for the executable** (`--exe PATH`, or `[Launch] Exe` in the
  profile) and **drive letters per run** (`--drive X=.`). The override changes **only the path the
  loader receives for the game**; the rest of the setup stays identical, and the plan shows that it is
  an override. **MEASURED.**
- For the drive letter we create a link in the Wine prefix (`dosdevices/x:` → the folder the game is
  in). It must be created **after** the Wine server has warmed up, because Wine rewrites `dosdevices`
  when it starts. The link must be removed afterwards as well: a `x:` that stays behind changes every
  later run in that prefix. **MEASURED** — this cost a measurement.

Two pitfalls we measured, so you do not have to find them again:

- **The loader's working directory is not the game's.** The loader sets the game's working directory
  correctly, but the loader itself must start in its own runtime folder, or the core looks for its own
  files in the wrong place. We do that through the per-game working-directory setting, not by changing
  directory ourselves.
- **`TP_REMOTETHREAD=off` means, in practice: leave the variable out.** Its mere presence puts the
  loader on its remote-thread route, and that route does not work for this game. Setting it to `off`
  is still "present".

**What our core does inside the process. MEASURED:** the game rebuilds its own `.text` completely.
With dense sampling of the game's memory (`/proc/<pid>/mem`, eight blocks of 64 KB across the text
section) `.text` goes from 100 % zero to **10.0 % zero** — the same end state the cabinet's original
core reaches (measured at 11.2 %). At that moment the entry point is back on its pristine first bytes
(`eb 08 92 1b …`, byte for byte equal to the file). And then, about **0.2 seconds later**, the game
ends. The rebuild is therefore not the problem; section 8 gives the numbers and the exit trace.

## 5. Input

- The cabinet's input is a lightgun with a vacuum handle (section 1): aim, trigger, and a separate
  button for the flash. **INFERRED** — we have never seen the cabinet.
- **MEASURED:** our own input layer feeds this title through the shared board image, in the layout this
  family uses. **MEASURED** with a real controller: an aim of 0.8 / 0.3 comes out as the x byte **202**
  and the y byte **79**, exactly the expected values; the trigger sets bit `0x4` of the control field,
  the second button bit `0x2`, and test bit `0x100`. The board image and the layouts are described on
  the [Loader architecture](Loader-Architecture) page.
- **MEASURED:** the game itself **never touches COM1**. With a pseudo-terminal on `com1` and a trace of
  all file operations there were **0** COM names in **86** file-open lines. The flash step belongs to
  the separate tool in the start script, not to the game.
- **Not measured:** the input actually arriving at the game with our own core, because the game does
  not get far enough (section 8). Our input layer is not what stops it.

## 6. Timing

All times are **seconds after the game process appears**, on a Steam Deck under Wine. **MEASURED**:

| Moment | Value |
|---|---|
| The loader's entry-point trampoline is active | 2.5 s until 4.0–6.0 s (varies per run) |
| Entry point restored, the rebuild begins | 4.5–6.8 s (varies) |
| The rebuild done (`.text` ~10–11 % zero) | almost immediately after, within ~0.2 s |
| End of the process on our core | 0.2–0.3 s after the rebuild |
| End of the process on the cabinet's core | not at all; still alive after >300 s |

**A warning about cold and warm cache.** The same moment in the same start on the same core was
measured at **4.8 s** and at **6.0 s** — more than a second of difference on a process that lives
about six seconds in total. It was also measured that **heavier instrumentation slows the process down
and changes how long it lives**: with a full file trace (`WINEDEBUG=+file`) the game lived 25–30 s
where it lived 5–7 s without it. Never compare a measurement with instrumentation to one without.
**MEASURED.**

## 7. What we saw on screen

**There is no screen.** All measurements above were made **headless**: the game gets an Xwayland
display with no physical output, so that the TV stays free and nobody has to watch. **MEASURED** in the
sense that the setup does exactly that.

What that means: these numbers say something about **starting, rebuilding and ending**, and
**nothing** about how the game looks. With the cabinet's own core a picture has been seen on this
machine (title, attract mode, start, vacuuming, catching ghosts, the test menu) — that is an
**INFERRED** observation from earlier work on the same machine, not from the runs in this report.

A run with a picture on a real display is **not comparable** with this series: different resolution,
sound, and the loading of movies and music lands in the same window that is measured here.

## 8. Current state and open questions

**The bar:** the game must run for **five minutes** in attract mode or game mode. That is **not met**.

- **What is achieved:** the game starts on our own core, rebuilds its own `.text` completely, and the
  environment allows five minutes — with the cabinet's original core a baseline of **300 s** was
  measured with the process still alive (92 threads, entry point restored, `.text` filled).
  **MEASURED.**
- **What is in the way:** the end of the process ~0.2 s after a completed rebuild, from inside a
  function with tag `0x919e48fe` whose code is generated while it runs. **MEASURED.**
- **What is not in the way:** the image rebuild (it succeeds), the input layer (that is already ours),
  and the four steps of the start script (section 9).

**MEASURED** over several runs, with the same setup otherwise and the core's SHA-256 checked against
the loader's log every time:

| Run | Entry point released and `.text` end state | End of process | Gap |
|---|---|---|---|
| Our core, no drop-in | 4.8 s — `.text` 10.0 % zero | 5.0 s | ~0.2 s |
| Our core, no drop-in (second run) | 6.0 s — `.text` 10.0 % zero | 6.2 s | ~0.2 s |
| Our core, started from `X:\exe\x64\` | 6.8 s — `.text` 10.0 % zero | 7.0 s | ~0.2 s |
| Cabinet's original core (baseline) | ~9–10 s — `.text` 11.2 % zero | **alive after >300 s** | — |

**Where the process ends**, from a trace that hooks `ExitProcess`, `TerminateProcess`,
`RtlExitUserProcess` and `NtTerminateProcess` with the exit code, the return address as module plus
offset, the section name and a stack walk (**MEASURED**):

```
frame  2 = 0000000140F1374D  VACUUM.exe+0xf1374d  section .capcom0
(nested) RtlExitUserProcess code=0x140415820 (low byte 32)
(nested) NtTerminateProcess  handle=0 code=0x40415820 (low byte 32) ntdll.dll+0x29f71
```

The exit comes from **inside a generated function with tag `0x919e48fe`**, which has exactly **one**
caller. The value handed over as the exit code is a **leftover register value** (a table pointer) —
**not an error code**. In other words: the `32` you see in the exit code is not a meaningful error
code but whatever was left in the register at the moment of the call. **MEASURED** from the trace.

**What that means if you continue this work:** do not look at the image rebuild and not at the "exit
code". Look at **what the generated function with tag `0x919e48fe` does**, and at the caller that
causes the exit. A log point at `0x14011c5f0` that records the `rcx` register should confirm that
`rcx == 0x140415820` at the moment of the exit, which closes the exit-code question with one
measurement. **INFERRED** as a next step; not yet done.

Open questions, each with the next step:

1. **What does the generated function with tag `0x919e48fe` do, and who calls the exit?**
   Next step: a **log-only** hook at `0x14011c5f0` that records `rcx`, to confirm `rcx == 0x140415820`
   at the exit.
2. **Does the end depend on a moment or on a duration?** Our runs give ~0.2 s after the rebuild; a
   dense meter run by another party gave 1.26 s. Both see the same thing (the rebuild succeeds), so
   the difference is in the exposure afterwards. Next step: two runs with a different weight of
   instrumentation, on the same core. If the duration scales with the instrument, it is an effect of
   measuring.
3. **Why does the rebuild take as long or longer on our core than on the cabinet's, and why does the
   process end afterwards?** Next step: put the output of the log point above next to the timeline.
4. **The generated-code system itself.** The exit comes from inside a function whose code is generated
   while it runs; what that function protects or handles has not been established. That is the area of
   research that remains now that the four start steps have been crossed off.

## 9. What we tried and what did not work

**The four steps of the start script, measured one at a time**, with the same setup otherwise, on our
own core, with the core's SHA-256 checked against the loader's log every time (**MEASURED**):

| Step | Outcome | Moments |
|---|---|---|
| `set GAMEID=SDCF` | **No effect** | Entry point released ≤5.8 s; process ends at **6.0 s** |
| `sendFlash COM1 LMAIO.bin` | **Inert** | Exit code **0**; 35 bytes in five blocks `/i\n/e\n\x03`; the 140,688-byte firmware file is **never** sent |
| Preparing `Y:\SDCF` | **No effect** | Entry point released at 6.2 s with `.text` 10.0 % zero (rebuild done); process ends at **6.5 s** |
| Starting from `X:\exe\x64\` | **No effect** | Entry point released at 6.8 s with `.text` 10.0 % zero (rebuild done); process ends at **7.0 s** |

All four steps of the official start script are therefore crossed off. That is the kind of table that
saves weeks of work: it removes a whole class of suspects before you start.

**What we also tried, and what it taught us:**

- **Comparing the two cores directly.** Because the core is the loader's first argument, our core and
  the cabinet's core can be run with everything else identical. The result is in section 8: both reach
  the same end state of the rebuild (10.0 % versus 11.2 % zero), and the difference is what happens in
  the fifth of a second after it. **Lesson:** the image rebuild is not the suspect, however much it
  looks like one while it is still running.
- **Explaining the exit code.** The exit code looked like a meaningful `32` for a long time and was
  chased as one. The trace in section 8 shows it is a leftover register value whose lowest byte happens
  to be 32. **Lesson:** an exit code is only a number until you have seen which register produced it.
- **Trusting an instrument that recognises the game by name only.** One of our samplers accepted a
  process as "the game" only if its command line matched a specific string. In a run where the game is
  started from a drive letter, that string is different, and the sampler then reports that it never saw
  the game at all. **Lesson:** a run in which the process was never found says nothing about the game;
  prove that the instrument looked before you read its silence as a result.

## 10. What is Linux-specific, and what differs on Windows

The game is a Windows program; on Linux it runs under **Wine**. This is what we had to arrange on
Linux, with per point whether it is **MEASURED** or an **assumption**.

| Point | What is needed on Linux | Status |
|---|---|---|
| Wine itself | An ordinary Wine build (ours: the Kron4ek `wine-proton-11.0-2` build), not Proton | **MEASURED** (the path of the Wine binary is in our run logs) |
| Prefix | A Wine prefix in which the game runs; everything of the game (registers, libraries, drives) hangs off it | **MEASURED** |
| 64-bit loader and core | A 64-bit loader plus a 64-bit core, because the game is `PE32+`; a 32-bit loader cannot load it | **MEASURED** |
| `dosdevices` | Wine creates `z:` (the root) and `com1..com32` itself. An **extra** drive letter is a symbolic link in `<prefix>/dosdevices/`, for example `x:` → the folder with the game | **MEASURED** |
| **Warming up the Wine server** | Wine rewrites `dosdevices/` as soon as a Wine server or `wineboot` starts. A link created before that is gone afterwards. So: start the server first (`wineserver -p`, `wine cmd /c exit`), **then** create the link | **MEASURED** (this cost a measurement) |
| Movies | The `Data/Movie/*.wmv` films go through Wine's GStreamer backend. Supply the **ASF demuxer and the WMV decoder** separately (`libgstasf.so`, `libgstlibav.so`) and point `GST_PLUGIN_PATH` at them; without those two the game exits at the first movie | **MEASURED** |
| Writable folders | The game writes in `Ram/` (settings, credits) and `Log/`. Those must be writable even when the game files themselves are read-only | **MEASURED** |
| Read-only source and a view | If the game is on a read-only disk (ours is on network storage), make a **view**: a folder of symbolic links to all game files, plus `Data` → `DataGlobal/EN/Data`. The source stays untouched and the folder structure the game expects is there | **MEASURED** |
| A pitfall with symbolic links | Use a **real folder with links inside it** for the folder the game is **in** (here `exe`), not a single link to the source. A launcher that wants to place files (extra libraries, configuration) cannot write through such a link and fails silently | **MEASURED** |
| COM1 | Wine maps `com1` to `/dev/ttyS0`. On a Steam Deck that device is `root:uucp 0660`, so an ordinary user gets **ACCESS_DENIED** instead of "does not exist" — and the game can tell the difference. To measure what happens on the port, put a pseudo-terminal on `dosdevices/com1` | **MEASURED** |

**What you do differently on Windows** (an **assumption** — we did not test it):

- **No prefix and no `dosdevices`**: drive letters are real letters. `X:` is a partition, a mapped
  drive or a `subst`. The symbolic-link trick and the Wine server warm-up do not exist there.
- **No Wine server rewriting your folders**: links and mappings stay between runs.
- **Codecs**: for the `.wmv` movies the cabinet relies on the system's codecs; the GStreamer step above
  is specific to Wine.
- **COM1** is a real or virtual serial port instead of a `/dev/ttyS*` device, so the permission pitfall
  above does not apply.
- **Timing**: we expect the absolute times on Windows to be different (other scheduler, other driver
  stack). The proportions — the rebuild finishes, then ~0.2 s later the process ends — should be the
  same, because they come from the game and not from the operating system. That part is an
  **assumption**.

---

*Everything above was measured on one machine: a Steam Deck running SteamOS with plain Wine, with the
game files on network storage that is only read. Where a claim is labelled MEASURED there is a number,
timestamp or offset with it; where it is labelled INFERRED it is a conclusion from those measurements.*
