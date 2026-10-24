# Official start scripts

Most arcade games ship with their own start script: a `game.bat` on Windows-based cabinets, a `run_game.sh` on Linux-based ones. It is the most reliable description of how the cabinet starts the game. This page explains what those scripts do, why RavenGun **never executes them**, and how we take over only the parts a game needs. Tags are those from the [Home](Home) page.

## Why they matter

For a long time our launcher started each game with a command line we had made up: loader, core, executable, nothing else. That is not what the cabinet does. Two findings changed our approach:

- **MEASURED** (2026-10-13, House of the Dead: Scarlet Dawn): the game's own script passes `-UNATTENDED`, which tells the engine not to show error windows. We passed nothing. The game then sat on an error window for 60 seconds before it was stopped. One argument would have avoided that.
- **MEASURED** (2026-10-13): of the twelve games in our collection that have a start script, the script does something we did not do in eleven.

The difference between "our start" and "the cabinet's start" is now printed by the launcher itself, for every game that has a script (see below).

## What a start script does

Reading the scripts of twelve games (**READ**, 2026-10-13), everything they do falls into six groups:

| Group | Examples from the scripts | Does the game need it? |
|---|---|---|
| **Environment variables** | `set GAMEID=SDCF`; `set LAUNCH_ON_IMAGE=1`; `export DISPLAY=:0` | Sometimes. A variable the script sets and never uses itself is meant for the game. |
| **Arguments** | `-launch="cabinet" -UserDir="..." -NotInstalled -UNATTENDED`; `-NOINI -Language=JPN -playside=1`; `-f1280x720 -e -mg` | Often. This is where our starts differed most. |
| **Working directory** | `pushd ..\Hodzero` before starting an executable two folders deeper | Sometimes; games use relative paths from it. |
| **Other programs first** | a tool that sets the screen resolution; a tool that sets the audio volume; a program that writes firmware to the I/O board over a serial port; a background service that must run before the game | The background service: yes. The screen and audio tools: no, on a PC. |
| **Drive letters and fixed paths** | `X:\` for the game image, `Y:\` for application data; `/pm/...` on Linux cabinets | The game needs the paths to resolve. How is route-specific. |
| **Cabinet housekeeping** | `rmdir /S /Q Y:\`; `reg add HKLM\SYSTEM\...`; `taskkill`; on Linux `reboot`, `rm -rf` of the user-data folder after a particular exit code | No. These exist because the cabinet is an appliance. |

Some scripts also run a hardware check before the game and start the game only if it succeeds. That belongs to the cabinet's hardware layer and is outside the scope of this wiki.

## Why we never execute them

Two independent reasons. Either one would be enough.

**1. The script starts the executable itself.** In our Wine routes the loader must create the game process: it creates it suspended and loads the core into it before the game's first instruction (see [Loader architecture](Loader-Architecture)). A script that runs `start /wait game.exe` produces a game process without the core. The same holds for a game whose first program is a shell that launches the real game.

**2. The script does things that only make sense on the cabinet, and some are destructive.** All of these are in real scripts we read:

```
rmdir /S /Q Y:\                         :: wipe the application-data drive if a folder is missing
reg add "HKLM\SYSTEM\..." /f            :: write cabinet settings into the registry
taskkill /im <service>.exe              :: stop the background service afterwards
reboot                                   # Linux cabinet: the game asked for a reboot on exit
rm -rf ${USERDIR}                        # Linux cabinet: wipe the audits after a particular exit code
```

On a cabinet `Y:` is a dedicated partition and a reboot is harmless. On someone's PC neither is true.

## What we do instead: read it, and take over the facts

The launcher **reads** the script and puts it next to its own plan. Nothing in the script is run. For a real game the plan then contains, for example:

```
eigen startscript van het spel: game.bat -- gelezen, NIET uitgevoerd
  zet GAMEID=SDCF -- WIJ GEVEN DIE NIET MEE (het script gebruikt hem ook zelf)
  start eerst  sendFlash COM1 LMAIO.bin -- WIJ NIET
  start het spel: VACUUM.exe (zonder argumenten)
    de argumenten zijn gelijk aan de onze
  gebruikt de schijfletters X:, Y:
  kastding (daarom niet uitvoeren): rmdir /S /Q Y:\ (voorwaardelijk)
```

In English: the script sets `GAMEID=SDCF` and we do not pass it; it first runs a firmware tool and we do not; it starts the game without arguments, as we do; it uses drive letters X: and Y:; and it contains a destructive cabinet command, which is why it is not executed.

What is needed is then entered **as data** in the game's own settings file, by whoever looks after that game:

| What the script does | Where it goes |
|---|---|
| sets an environment variable | `[Env] NAME = value` |
| passes arguments | `[Launch] Arguments = ...` |
| changes the working directory | `[Launch] WorkingDirectory = ...` |
| starts a service before the game | the two-program start (order and wait come from the profile) |
| starts the game from a drive letter | `[Launch] Exe = X:\...` with `[Launch] Drives = X=.` |

Once it is there, the plan says "we do too" for that line. The comparison is information only: reading a script never changes what is started.

### The reader decides what the files decide

A start script is a small program. One of ours, a chain of seven batch files, picks the executable like this:

```bat
if exist "%APP_EXE_PATH_SHIPPING%" (
  set APP_EXE_PATH=%APP_EXE_PATH_SHIPPING%
) else if exist "%APP_EXE_PATH_TEST%" (
  ...
) else (
  set APP_EXE_PATH=%APP_EXE_PATH_DEBUG%
)
```

A reader that takes "the last assignment" reports the debug executable, which is not there. Ours evaluates what can be evaluated from the game folder and from the script's own variables: `if exist <file in the game folder>`, `if "%X%"=="y"`, `goto`, and `call` into further scripts with their arguments. It follows the branch that would be taken on this copy of the game.

- **MEASURED** (2026-10-13): for that seven-script chain the reader arrives at the shipping executable, the right configuration file for the background service, launch mode `cabinet`, and working directory `App/Hodzero` — all decided from the files.

What depends on the cabinet is **not** guessed: an `errorlevel`, or a file on a drive that is not the game image. Everything inside such a branch is reported as *conditional*.

For shell scripts the reader distinguishes exported variables (which reach the game) from plain shell variables (which do not).

## What the comparison found

**READ / MEASURED** (2026-10-13, the launcher's plan for each profile):

| Game | What the cabinet's script does that our start did not |
|---|---|
| House of the Dead: Scarlet Dawn | five arguments for the game, among them `-UNATTENDED`; two arguments for the background service; three tools before the game; working directory `App/Hodzero`; drives X: and Y: |
| Luigi's Mansion Arcade | `GAMEID=SDCF`; a firmware tool for the I/O board first; starts the game from `x:\exe\x64\`; drives X: and Y: |
| Time Crisis 5 | `-NOINI -Language=JPN -playside=1` |
| GhostBusters (through Wine) | `-f1280x720 -e -mg` |
| Jurassic Park (through Wine) | `-n -f<resolution>`; two tools first |
| Halo: Fireteam Raven | two tools first, among them an initialisation step with its own arguments |

For two native games the arguments we already used turned out to match the script exactly (`-n -f<resolution>` and `-f<resolution> -e -mg`). That is what the comparison is for: it shows agreement as plainly as difference.

## Using the reader on its own

```
python3 scripts/startscript.py <script> --root <game folder> [--json]
```

It prints the variables, the programs in order with their arguments, the working directory at the moment the game is started, the drive letters, and the cabinet commands that are the reason not to run the script.

## Limits

- It is a reader, not an interpreter. `for` loops are reported as conditional as a whole; substring expansions such as `%DATE:~2,6%` are shown as placeholders, not computed.
- A step the script takes **before** the game is only reported. The launcher does not perform it.
- **NOT MEASURED**: whether each of the differences in the table above changes the game's behaviour. Only the first one (`-UNATTENDED`) has a measured consequence so far.

See also: [Loader architecture](Loader-Architecture), [Running on Linux](Running-on-Linux).
