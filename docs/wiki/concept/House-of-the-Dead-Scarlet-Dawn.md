# House of the Dead: Scarlet Dawn

*Windows x64, Unreal Engine 4. Route: Wine + OpenParrot loader + core, **two programs** (a background service, then the game). Status: see section 9. **This page is waiting for the game specialist's digest**; what is here comes from the loader side of the project and from reading the game's own start scripts.*

Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

An Unreal Engine 4 game for a Windows-based Sega cabinet. It consists of **two programs** (**READ**, the game's profile and its own scripts):

| Program | Role |
|---|---|
| `Hodzero-Win64-Shipping.exe` | The game |
| `amdaemon.exe` | Sega's background service; the game talks to it and it must be running first |

*More about the cabinet: pending the specialist's digest.*

## 2. What the game needs

- **The background service running before the game** (**READ**, section 3).
- **The arguments the cabinet passes** (section 3). One of them has a measured effect.
- *More: pending the specialist's digest.*

## 3. The official start script

The game has a chain of **seven batch files**, starting at `game.bat`. We never execute it (see [Official start scripts](Official-Start-Scripts)); the launcher reads it. **READ / MEASURED** (2026-10-13, the launcher's start-script reader, which follows the chain and decides the branches that can be decided from the game folder):

| What the chain does | Detail |
|---|---|
| Sets the screen | a tool with `1920 1080 0` |
| Sets the audio volume | a tool with `85 75` |
| Cleans a log folder | a tool on `Y:\SDET\log` |
| Starts the service, without waiting | `amdaemon.exe -c WindowsNoEditor\AMDaemon\config.json -l "<log file on Y:>"` |
| Waits | 5 seconds (`DAEMON_WAIT_SECONDS=5`) |
| Starts the game and waits for it | `Hodzero-Win64-Shipping.exe -launch="cabinet" -ABSLOG="<log on Y:>" -UserDir="<folder on Y:>" -NotInstalled -UNATTENDED` |
| Stops the service afterwards | `taskkill` |
| Working directory at the start of the game | `App/Hodzero` (the executable is several folders deeper) |
| Drive letters | `X:` for the game image, `Y:` for application data |

The reader arrives at the *shipping* executable, the plain `config.json` and launch mode `cabinet` because those are the branches taken on this copy: the script picks the executable with `if exist`, and the two development files that would select another mode are not present.

The chain also contains cabinet housekeeping that must not run on a PC: registry writes for the video ports, `rmdir /S /Q "Y:\"` under a condition, and the `taskkill`.

**Why this matters.** **MEASURED** (2026-10-13, by the game specialists): started without arguments, the game waited 60 seconds on an error window before it was stopped. The cabinet's script passes `-UNATTENDED`, which tells the engine not to show error windows.

## 4. The route we use

Until now this title was started through another front-end, because our launcher refused profiles with two programs. The launcher can now start both (see "Two programs" in [Loader architecture](Loader-Architecture)): the service first, a wait of 5 s, then the game, with the same Wine build, loader, core, working directory, prefix and environment for both, and output and exit code stored per program.

- **MEASURED** (2026-10-13): the plan for this profile shows both starts in order with the wait, and the second program resolved inside the same view as the first.
- **NOT MEASURED**: a real run of this title through the launcher.
- Not yet entered in the game's settings: the arguments from section 3. Those belong to whoever looks after this title.

## 5. Input

*Pending the specialist's digest.*

## 6. Timing

The cabinet waits 5 seconds between the service and the game (**READ**). *Measured timings: pending the specialist's digest.*

## 7. What we saw on screen

*Pending the specialist's digest.*

## 8. Linux notes

- Both programs must run in the **same Wine prefix** with the same environment; they talk to each other through shared objects that exist per prefix.
- The scripts use `X:` and `Y:`. Under Wine a drive letter is a link in the prefix (see [Running on Linux](Running-on-Linux)); the launcher can create such links for the duration of a run.
- The second program must be started **inside the same view** as the game. A profile that points at the service on the share directly would run it outside the view; the launcher maps the path into the view or refuses.

*More: pending the specialist's digest.*

## 9. Status

*Pending the specialist's digest.* From the loader side: the two-program start exists and is covered by tests that replace only the process start; it has not been run on this game.

## 10. Open questions

1. Does the game behave differently with the cabinet's arguments (`-launch="cabinet"`, the log and user-folder arguments, `-NotInstalled`, `-UNATTENDED`)? Only `-UNATTENDED` has a measured consequence so far.
2. Does the game need the working directory `App/Hodzero`, as the script sets it?
3. Which of the script's environment variables, if any, does the game read?

---

*Sources so far: the loader side of the project and the game's own start scripts. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
