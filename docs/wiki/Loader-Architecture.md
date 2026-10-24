# Loader architecture

This page describes how a game is started and how it gets its input: the routes, the loader and its
helper program, the I/O bridge, drop-ins, per-game profiles, the start plan (`--plan`) and the
identity rule that makes every log say which tool performed the run. It is written for someone who
wants to build their own launcher for these titles.

Labels are **MEASURED** (with the numbers, paths or hashes that came out of the measurement),
**INFERRED** (a conclusion drawn from measurements) and **assumption** (a guess we did not test).

## 1. What the loader has to do

An arcade title is not a normal PC game. It is written to find a cabinet around it:

- it expects its own working directory and its own data roots (**MEASURED** on several titles);
- it asks a board for its input, over shared memory, a serial line, a HID device, a joystick API or a
  TCP socket, depending on the family (**MEASURED** per title);
- it may expect a second program next to it (a platform daemon) to be running first (**MEASURED**);
- its code is often not in the file as plain bytes, or it is linked in a way a stock loader will not handle (**MEASURED**: one title's
  executable has five sections whose bytes are not in the file, and six of the seven Linux titles in
  our set are linked to be set up in memory by a specific wrapper).

So a launcher has to do four things: choose the right way of loading the guest, put the working
directory, environment and drive letters in place, deliver input through the channel the game reads,
and be able to say afterwards which tool, which route and which files produced the result.

## 2. The routes

There are four routes in use. Which one applies is a property of the title, not a preference.

### 2.1 Native: the kernel's own `ld.so`, inside a sandbox

For a title that is already a Linux ELF, no loader is needed: the kernel's dynamic linker starts it
directly. **MEASURED** (2026-10-10 and later) with *Halo: Fireteam Raven*, which is a 64-bit
`ET_EXEC` `EM_X86_64` ELF.

```sh
scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
  --bind-ro <our helper>.so /rg/lib/<our helper>.so \
  --bind-ro <switch image> /rg/jvs --bind-rw <log dir> /rg/tap \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:<our helper>.so \
  --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=400
```

The sandbox is **bubblewrap** with explicit binds: `/usr`, `/etc` and `/sys` read-only, `/dev`,
`/dev/dri` and `/dev/input` passed through, its own network and PID namespace, `/tmp` as a tmpfs, and
the game folder as a **read-only lower layer with an overlay** for everything the game writes.
**MEASURED** that the game writes in its own folder (`pmuser/`, `aud/save/*.xml`, `adj/save/`), so the
overlay is what keeps the source untouched. `HOME` and the working directory are set to the game
folder. For a picture we use **gamescope** (`--backend headless` for runs without a screen, or
full-screen on the TV).

**Why this is not the general case. MEASURED:** all seven Linux-ELF titles in our set were started
with the kernel loader alone, in an 8-second window, bare and with the graphics fix preloaded. **0 of
7 started.** Six stopped in the dynamic linker on the first library this machine does not have
(`libGLU.so.1` for *GhostBusters* and *Terminator*, `libstdc++.so.5` for *GoGoStrike*, `libfmod.so.10`
for *Halo*, `libtheora.so.0` for *JurassicPark*, `libXrandr.so.2` for *PacmanChomp*); the seventh
(*GalagaAssault*) resolved every library and still died with SIGSEGV and not one line of output, also
with the graphics fix preloaded. **INFERRED:** those executables are linked to be set up in memory by
the wrapper below (fixed GOT slots, PLT stubs that push a placeholder), so a route for that family is
its own ELF loader and not a few extra libraries.

### 2.2 ElfLdr2 (the Budgie wrapper) for Linux ELFs

The alternative for a Linux ELF is `ElfLdr2/BudgieLoader.exe` (32-bit) or
`ElfLdr2/x64/BudgieLoader_x64.exe` (64-bit). **MEASURED:** it is a Cygwin program that loads the guest
ELF **into its own address space**; that single fact explains most of the pitfalls below, because
anything preloaded into the wrapper's process is not in the guest and anything the guest needs can
kill the wrapper.

**MEASURED:** these dumps have **zero section headers** (`readelf -h` reports 0, `readelf -S` reports
no sections). The wrapper warns twice with `Warning SHT_DYNSYM not found in section headers` and
carries on. That warning is harmless: *PacmanChomp* and *GalagaAssault* ran past it, and in the period
that *Halo* was playable the same two lines were in the wrapper's log followed by the game's own
lines. The dynamic symbols are reachable through the program's dynamic segment (`DT_HASH`,
`DT_SYMTAB`, `DT_STRTAB`); our own reader got 220 symbols out of `DT_HASH` there. **INFERRED:** no
patch on the ELF is needed for this warning.

### 2.3 TeknoParrot / OpenParrot under Wine

For a Windows title the launcher starts Wine with a loader and a core:

```sh
wine <loader> [<core, without .dll>] "Z:\...\<game>.exe" [arguments]
```

- **The core is the loader's first argument** and the loader appends `.dll` itself. Two cores can
  therefore be compared by changing only that argument, with everything else identical.
  **MEASURED:** that is how the cabinet's original core and our own core were compared on *Luigi's
  Mansion Arcade*.
- The loader is chosen per emulator type in the profile: `TeknoParrot` →
  `OpenParrotWin32/OpenParrotLoader.exe` or `OpenParrotx64/OpenParrotLoader64.exe`;
  `OpenParrot` → the same loader with the OpenParrot core; `ElfLdr2` → the wrapper of section 2.2;
  there are also Lindbergh, N2 and TeknoMacaw entries, which we did not run live. **MEASURED** from
  the launcher's source and from the plans it prints.
- **MEASURED:** `launch-direct.py` derives loader, core argument, working directory, prefix,
  environment and the game's own settings file, and for Terminator and Time Crisis 5 the result was
  **byte for byte** what the commercial TeknoParrot UI starts.
- Working directory: the game's own folder for ElfLdr2/N2/Lindbergh titles, the runtime folder for the
  rest. **MEASURED**, and the difference matters: on *Luigi's Mansion Arcade* the loader must start in
  its own runtime folder while the game's working directory must be `exe/x64`, or the core looks for
  its own files in the wrong place.
- Environment the launcher sets (**MEASURED**): `WINEPREFIX` (one shared prefix), `WINEDEBUG=-all`,
  `TP_LAUNCH_SESSION_ID` (a UUID per run, so one run's output can be found back), and — for the
  ElfLdr2 family — `tp_windowed`.
- **The remote-thread trap. MEASURED:** the loader has two injection methods. The remote-thread
  method fails on these titles with `Failed to find kernel32.dll in target process`. The launcher
  therefore leaves `TP_REMOTETHREAD` **out** of the environment of the run, after which the loader
  uses its default entry-point trampoline; the log then shows
  `Resolved target kernel32.dll procedure: local=… remote=… rva=0xF81C` followed by `Have fun :)`, and
  that is the route that works. Note the shape of the setting: because the variable's *presence* is
  what selects the other method, "off" and "0" are still "present". The profile key is
  `TP_REMOTETHREAD=off`, and it means: do not set it.

### 2.4 Proton with a plug-in inside the game

For the Unity title in our set, no loader and no injected core is used at all: Proton starts the game
and a managed plug-in is loaded into it. **MEASURED** for *Rabbids Hollywood Arcade*:

```sh
systemd-run --user --unit=ravengun-tpui --working-directory=<view> \
  <GE-Proton>/proton run Game.exe -popupwindow
```

with `WINEDLLOVERRIDES=winhttp=n,b` (**required**: without that override Wine does not load the
doorstop, which is the `winhttp.dll` + `doorstop_config.ini` pair that loads the plug-in),
`STEAM_COMPAT_DATA_PATH` (this route has its own prefix), `STEAM_COMPAT_CLIENT_INSTALL_PATH`, and
`WINEDEBUG=-all`. The game's input then arrives over **TCP**: the plug-in inside the game is the
server on `127.0.0.1:33610` and the engine is the client, at a fixed 120 packets per second. Section 6
gives the protocol.

## 3. The helper loader: `pipehelper.exe`

Every Windows route in our set keeps one Windows helper program in the prefix, `pipehelper.exe` from
the runtime. It is the bridge between the shared input state and the game:

- the launcher creates the shared file `/dev/shm/TeknoParrot_JvsState` (64 bytes; existing content is
  kept, not cleared) **before** the game starts;
- it starts `wine pipehelper.exe <pipe name> <host> <port> <shm name> <shm size> Z:\dev\shm\<source>`;
- the helper creates the named pipe `\\.\pipe\TeknoParrot_JVS` and the named file mapping
  `TeknoParrot_JvsState` in the prefix, mirrors that mapping with the shared file **every
  millisecond in both directions** (a byte the game writes goes to the file, a byte the file changes
  goes to the game; on a conflict the file wins), and serves the pipe over TCP to the launcher;
- the launcher listens on `127.0.0.1:<port>`, accepts the helper's positional handshake, sets the
  connect flag (byte 0 of the shared state) on a JVS reset (`0xF0`) and address (`0xF1`) request,
  answers the address, and logs any other packet without answering it. **MEASURED:** the titles in
  our set that use this route send nothing else over the pipe; Type X2 titles do.
- On exit the launcher waits for the loader (or gamescope) and for as long as the game or a loader is
  still in its own cgroup, stops the helper, and lets systemd clean up the rest of the unit.

**MEASURED:** with this route the game connects — at Terminator the pipe reported `GAME CONNECTED` —
and aiming, reloading, the trigger and even restarting the engine during play worked on real
hardware. The connect flag matters for timing: the helper copies byte 0 synchronously just before its
first answer, which is why the flag is set at the reset/address step and not later.

## 4. The I/O bridge: one state model, five dialects

The engine publishes **one** input state and the bridge translates it into whatever the game reads.
**MEASURED** from the engine's source and live on hardware for `tp-shm`, `unity-tcp` and `hid-board`.

The state (`IoState`): per player 1..4 an aim as a fraction 0..1 (after transformation, prediction and
the output clock), `valid` (there is a usable aim), `offscreen` (the raw aim was outside the screen),
trigger, reload, grenade, two extra buttons and start; for the cabinet: coin per player, test,
service, volume up and volume down. The game's output (recoil counters, start lamps, credits) is read
back where a dialect can carry it (only `unity-tcp` today). A tick thread at **250 Hz** reads a
snapshot under the same mutex as the output clock and writes it to the active dialect. A player
without an armed gun or a valid aim is written as neutral/idle, never as the previous position.

| Dialect | How the game gets the input | Used by (measured) |
|---|---|---|
| `tp-shm` | The 64-byte board image in shared memory (section 3). The layout per emulation profile is data in `packaging/tp-shm-profiles.json`: for every bit a signal, an offset and a bit number in the little-endian bit field, and for every axis a signal, an offset, 1 or 2 bytes, minimum and maximum, inversion, byte order and idle value. Layouts in use: `WartranTroopers`, `RawThrillsGUN`, `LuigisMansion`, `TimeCrisis5`, `TransformersShadowsRising`, `FarCryPipe`, `MusicGunGun2Pipe`, `GoGoStrike`, `LGS`. | Luigi's Mansion Arcade, Halo: Fireteam Raven, Terminator Salvation |
| `evdev` | The engine writes directly to an evdev device and the game reads input devices itself. | House of the Dead: Scarlet Dawn, Jurassic Park |
| `unity-tcp` | A plug-in inside the game is the TCP server; the engine is the client and pushes a fixed-size packet at 120 Hz. | Rabbids Hollywood Arcade |
| `hid-board` | The engine builds a virtual input board from `packaging/hid-boards.json`. A board description has a transport (`uinput` for an evdev joystick, `uhid` for a real HID device), a name, a vendor and product id, a button count, axes (name X/Y/Z/RX/RY/RZ with signal, inversion, minimum, maximum, idle) and buttons (index plus signal). `uhid` uses `UHID_CREATE2`, sends `UHID_INPUT2` on every change and every 100 ms, answers `UHID_GET_REPORT` and `UHID_SET_REPORT`, and cleans up with `UHID_DESTROY`. | Transformers: Shadows Rising |
| `jvs` | The engine acts as a JVS I/O board on a pseudo-terminal at 115200 8N1, published as a fixed symlink. Framing: sync `0xE0`, escape `0xD0`, a checksum over node, length and data; commands include reset `0xF0`, address `0xF1`, identify `0x10`, switches `0x20`, coins `0x21` (a rising edge counts one coin), analog `0x22` (16-bit, high byte first), outputs `0x32`/`0x37`. The board per game is data in `packaging/jvs-boards.json` (identify string, versions, players, buttons per player, coin slots, analog channels and bits, outputs, switches with player, byte and bit). A game that asks for the modem status needs a small shim: a pseudo-terminal does not answer `ioctl(TIOCMGET)` (measured `ENOTTY`), so the shim returns the status from a shared file. | Let's Go Safari, Let's Go Island (2D and 3D), Golden Gun |

Route selection is per game and layered:

- the shipped profile has `[Input] Route` (one of the five dialects), plus `Board`/`Boards`,
  `ComPort` and `CoinKeys`;
- the user layer (`~/.local/share/ravengun/patches/<Game>/game.ini`) wins **per key**, so a title can
  be switched without touching the shipped file, and deleting the file restores the shipped state;
- `ravengunctl iobridge start <Game> [--route R]` starts the route, `--route` overrides the profile,
  and `route`/`stop`/`status` manage it. The bridge re-reads the files at every `start`, `route` and
  `status`, so a change applies without restarting the engine;
- `ravengunctl status` reports the route, the dialect, the layout or board, the number of writes, the
  last bytes written, the first player's aim and buttons, and the number of resumes;
- a restart of the engine does not lose the route: it is stored in
  `$XDG_RUNTIME_DIR/ravengun/iobridge.json`, neutral input is written on exit, and the new engine
  resumes the route while the game's unit is still running. If that unit is gone for more than 8
  seconds, or no game appeared within 10 minutes, the route stops by itself.

## 5. Drop-ins

A drop-in is one of our files that the launcher puts in a game's view before the start. The whole
contract lives in the game's own folder: `patches/<Game>/dropin/` holds the files and
`patches/<Game>/dropin/dropin.ini` holds **one section per file**.

| Key | Meaning |
|---|---|
| `To` | Destination folder inside the view. |
| `DllOverride` | An entry for `WINEDLLOVERRIDES`, so the game loads our version of that library. |
| `Env` | An environment variable for the run. |
| `Preload` | This file must be loaded into the game's process. The path lands in `RG_DROPIN_PRELOAD` and, on the routes that read that variable, in `LD_AUDIT` of the Wine start. |
| `Audit` | May this file go into `LD_AUDIT`? Default `true`; `false` keeps it out while the path still lands in `RG_DROPIN_PRELOAD`. |

Three things about this mechanism were learned the hard way and are worth copying:

1. **The destination is `view + To + section name`,** and the section name is the file's path under
   `dropin/`. Putting that path in `To` as well places the file one level too deep, and the mechanism
   reports nothing for it. **MEASURED:** that error was present in six games and has been fixed;
   all 43 sections in the 14 files that exist today are correct, and 15 of the 36 profiles have a
   `dropin/` folder. A file in `dropin/` without its own section is **not** deployed and is not
   reported: that is the one silent failure of this mechanism.
2. **A destination that resolves outside the view through a symbolic link is refused loudly at
   planning time.** The reason is a rule of the project: our own artifacts must not land in a game
   folder on the network share. **MEASURED.**
3. **On the ElfLdr2 route a library that must be loaded by the guest must not be preloaded into the
   wrapper's process.** The wrapper loads the guest into its own address space, so anything in
   `LD_AUDIT` runs there. **MEASURED:** with the hook in `LD_AUDIT` the wrapper died (exit code 1 at
   *Halo*; exit code -11, SIGSEGV, at *Jurassic Park* — with the literal error
   `cannot be loaded as audit interface: wrong ELF class: ELFCLASS32; ignored`), while the same
   library with `Audit = false` left the run alive. `--plan` prints both lists, so the choice is
   visible before the run.

## 6. Per-game profiles

Everything that differs per title is kept in the game's own folder, not in the launcher:

- `patches/<Game>/game.ini` — the shipped profile, with `patches/_defaults/<Type>.ini` beneath it for
  shared defaults;
- `~/.local/share/ravengun/patches/<Game>/game.ini` — the user layer, which wins **per key**;
- `patches/<Game>/dropin/` — the drop-ins of section 5;
- named shared tables stay in `packaging/`: `tp-shm-profiles.json` (board layouts),
  `hid-boards.json` (virtual boards), `jvs-boards.json` (JVS boards). A game refers to them by name,
  so one layout serves several titles.

Sections in `game.ini` (**MEASURED** from the reader): `[Game]` (`GamePath`, `GamePath2`),
`[Identify]` (`Path`, `Path2`), `[Config*]`, `[Input]` (`Route`, `Board`, `Boards`, `ComPort`,
`CoinKeys`), `[Launch]` (`Mode`, `Exe`, `Drives`, `StartScript`, `Arguments`, `SecondWait`,
`SecondArguments`), `[Env]`, `[Loader]` (`Build`, `Core`, `Path`), `[Status]`.

Two layouts of the same settings exist. The commercial runtime uses XML profiles
(`GameProfiles/<P>.xml` with `UserProfiles/<P>.xml` on top), with fields such as `EmulatorType`,
`EmulationProfile`, `ExecutableName`, `Is64Bit`, `HasTwoExecutables`, `Executable2`,
`LaunchSecondExecutableFirst` and `SecondExecutableArguments`. The launcher reads both; if the
profile's `ProfileRevision` does not match the template it refuses to start directly (exit code 4)
and asks for a re-import, rather than starting with a half-known profile. There is one reader per
language — `scripts/gamesettings.py` for the Python tools and `src/ravengun/gamesettings/` for the
engine and the app — and they are tested against the same example files, so the same file means the
same thing everywhere.

**One trap worth naming. MEASURED:** in the *Transformers: Shadows Rising* profile,
`ExecutableName = shell.exe;Transformers2.exe` is **not** the two-executable mechanism. That
mechanism is `HasTwoExecutables` plus `Executable2`, and it exists for a game that has a separate
helper program that must run **next to** it — *House of the Dead: Scarlet Dawn* starts
`amdaemon.exe` first and waits 5 seconds before the game. A profile that merely lists two names in
`ExecutableName` is one start: the *shell* is started, and the shell starts the game itself. Reading
that difference wrongly is exactly why one title starts directly and the other does not.

## 7. Planning: `--plan`

```sh
python3 scripts/launch-direct.py <Profile> --plan [--json]   # what would happen; nothing starts
python3 scripts/launch-direct.py <Profile> --check           # only the checks; exit code says why not
python3 scripts/launch-direct.py <Profile> --run             # the run itself
```

`--plan` is deliberately free of side effects and is backed by the same code path as `--check` and
`--run`, so an impossible start is refused before anything is started instead of halfway. It prints:

- the **identity lines** of section 8 first;
- the plan itself: the profile, the emulator type and emulation profile, whether the title is 64-bit,
  the executable, the Wine prefix, the **core** that will be used with its path and SHA-256, the
  command line, and the game's own start script analysis;
- one line per **drop-in**, including the destination it computed and any refusal;
- the **starts** in order, with the wait between them, when there is more than one (the second start is
  marked as the second executable);
- the **arguments** per start and how they are passed (the OpenParrot loader takes all arguments as
  **one** parameter, which is printed literally);
- the **environment variables** the game gets, and the list of settings files that were read;
- the drive letters for the run and, when `--exe` is used, the overridden path — with the note that
  only the path the loader receives changes.

**MEASURED:** for a 32-bit title the plan reports `is64: False` and the matching 32-bit core; for the
game with two executables the plan prints `start 1/2: amdaemon.exe (second executable)`, then
`waiting: 5 s; only continue if every earlier start is still alive`, then `start 2/2: <game>`.

Exit codes are part of the interface: **0** finished, **1** error during the start, **2** wrong use,
**3** the runtime components in the prefix are not in place (fall back to the UI), **4** the profile is not
supported by a direct start, **5** an enabled patch was refused — with no fallback, because starting
without the patch would measure something else — and **7** with two executables: one start was already
gone before the next began, so the run is aborted as a half setup.

## 8. The identity rule: every log says which tool did the run

**MEASURED**, and it was learned the expensive way: three measurements in one week were lost to an old
copy that nobody knew was running (an old ELF loader, an old drop-in library, an old installed
launcher). Every plan and every run log therefore starts with the tool that made it:

```
launcher: <repo>/scripts/launch-direct.py
  the version from the REPO (commit 5cdfaa8; scripts/ equal to that commit);
  sha256 327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0;
  helper modules (5) sha256 c882ade50b66ad0cd20260050983968dfc629aba9033c1e5233d92f5f364d656
  the installed launcher (…/opt/ravengun/share/ravengun/scripts/launch-direct.py) is the same
```

The rule has five parts, and the sha256 is the one that counts:

1. the **path** of the tool that is running;
2. whether it is **the version from the repository** (with the commit, and whether the files in
   `scripts/` are equal to that commit or have uncommitted changes), **the installed version** (with
   its version number) or **a loose copy** (neither);
3. the **SHA-256 of the tool itself**;
4. the helper modules that belong to it, together with their own SHA-256, so a run can be pinned to a
   whole set of files and not to one;
5. when running from the repository, whether the **installed copy is the same** or a different version,
   because a script started through the installation would not be running this code.

The shell start scripts use the same rule through a shared function that is read in with `.` rather
than being a program of its own: it prints its own path, which of the three kinds it is, its SHA-256,
and the SHA-256 of every file handed to it as a labelled pair. A file that is not there is named as
missing instead of being skipped, and the function can never make the start fail: without git,
without a version file and without `sha256sum` it simply prints less.

## 9. What this means on Windows

Every route above has a Windows counterpart, and none of them was tested by us — this whole section is
an **assumption**.

| Here (Linux) | There (Windows) |
|---|---|
| A Wine prefix with a `dosdevices` folder for extra drive letters | Real letters: a partition, a mapped drive or `subst`. No prefix, no link that Wine can rewrite. |
| `LD_PRELOAD` / `LD_AUDIT` for a library that must be in the game's process | DLL injection, or placing the DLL next to the executable so the search order finds it first. |
| `WINEDLLOVERRIDES` to force our version of a library | The DLL next to the executable, or a manifest. |
| `uinput` for a virtual joystick, `uhid` for a virtual HID device | A virtual input driver (a joystick/HID driver), with the same question of whether the game's API sees it. |
| A shared-memory board image plus the Windows helper from the runtime | The same idea, but the helper must be built or supplied; the shared file becomes a named file mapping. |
| `gamescope --backend headless` for a run without a screen | A normal desktop session; there is no equivalent of a headless compositor here. |
| The kernel's `ld.so` for a Linux ELF | Not available: a Windows process cannot load an ELF. A Linux title needs a Linux environment or a translation layer. |

The measurements themselves travel better than the plumbing: working directories, data roots, the
board layout the game asks for, the file names it probes and the point where it stops are properties
of the game, and they stay the same on the other platform.

---

*Sources: the loader repository's own documentation and scripts (architecture, drop-in contract, procedures,
`launch-direct.py`), and the per-game measurements reported on the game pages. Numbers and hashes above are
copied from those files; the ones that came out of a run are labelled MEASURED.*
