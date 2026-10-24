# Halo: Fireteam Raven

*Raw Thrills, 2018. Linux x86-64, one to four players. Route: native, in a sandbox, with an input helper inside the game. Status: **the attract loop runs and the five-minute bar is met** (6.0 minutes in one run). Not finished: aim and trigger, sound, and the cabinet set-up messages on the title screen.*

Everything on this page was measured on a Steam Deck (SteamOS), with the game files on network storage that is only read. Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

- **MEASURED**: the game is a **64-bit Linux program** (`ET_EXEC`, x86-64), not a Windows program. It runs on a PC-based cabinet with its own Linux image.
- **MEASURED**: on the cabinet the game lives in `/pm/g7/halo`: the game binary `game`, `data/` (the game data), `eFileMaps/` (the manifest for the game's own file test), and `utils/` (the cabinet's scripts).
- **MEASURED**: there are several cabinet variants. The cabinet's own probe `rtinit -ct` returns a code for the type: 9 (dual-screen, four players, 3880x1080), 4 (mounted guns, two players, 1920x1080), 3 (deluxe, four players, 1920x1080) or 8 (55-inch, two players, 1920x1080).
- **MEASURED** (from the game's own output): it reports itself as `Welcome to Halo v03.11`, `Game Rev: 57529, Library Rev: 1412`.
- **DERIVED**: each player's gun has a point that a camera can see; the game measures a brightness difference in the camera picture to find the gun.

## 2. What the game needs

- **MEASURED**: 13 shared libraries: `libGL.so.1`, `libGLU.so.1`, `libpthread.so.0`, `libdl.so.2`, `libudev.so.1`, `liblapack.so.3`, `libiw.so.30`, **`libfmod.so.10`**, **`libfmodstudio.so.10`**, `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`. On an ordinary Linux machine eleven resolve; the two FMOD libraries (version 1.10, proprietary) are missing.
- **MEASURED**: a **stub** that exports the FMOD names and returns error code 52 is enough to get past the start. The game then has no sound. Without the stub the game jumps to address 0. The 52 in the log is therefore the stub working, not a fault.
- **MEASURED**: the game resolves its graphics, audio and device calls itself at run time through an internal name table (571 names); it has no such symbols in its own dynamic symbol table. The USB libraries for its gun board are linked statically into `game`.
- **MEASURED**: it needs `data/`, the manifest `eFileMaps/` (7 maps), and it **writes** into `pmuser/` and its audit and adjustment folders. Those writes must be able to go somewhere.
- **MEASURED**: at start the game runs its own file test over its files. On our copy it reports one failed file (`FileTest Reported 1 errd files at startup!`). That is **not fatal**: the game offers `Press START or Ding to skip File Test` and continues when START is given.

## 3. The official start script

**READ** (2026-10-13, the launcher's start-script reader on `utils/run_game.sh`):

```sh
cd /pm/g7/halo
<one library preloaded> ${GAMEDIR}/game2 -f${SCREEN_RES} -n
```

The script changes to the game's folder and starts the game binary with one library that ships with this copy preloaded, and the arguments `-f<resolution> -n`. Before that the script runs `pulseaudio_set_default.sh` and the probe `rtinit -ct` (which determines `SCREEN_RES`), and exports `DISPLAY=:0`. After the game it switches on the game's **exit code** (222 restart, 223 reboot, 224 adjustment, 225 factory reset, 226 power cycle).

- The script starts `game2`, which does not exist in our copy; we start `game`.
- The arguments it passes, `-f<resolution> -n`, are the ones we use.

**We never execute this script.** It contains `reboot -f` and `rm -rf` of the user-data folder, each under a condition on the game's exit code; the companion script `go.sh` also locks accounts, turns off swap and sets up the network. On a cabinet that is correct. See [Official start scripts](Official-Start-Scripts).

## 4. The route we use

The game is started directly by the system's own program loader, inside a **bubblewrap** sandbox (**MEASURED**):

```sh
scripts/play-native.sh --game Halo --gamescope --seconds 200 \
    --env RG_RT_LOG=1 ...
```

What the start script of our project does: the game's folder is bound read-only at `/pm/g7/halo` with a local overlay for everything the game writes; the working directory and `HOME` are `/pm/g7/halo`; the arguments are `-f1920x1080 -n`; our input helper is preloaded into the game.

No Wine, no own loader and no kernel module is needed for this title (**MEASURED**).

The first lines of every run log name the script that ran, with path and SHA-256, the SHA-256 of the game data file, of our own libraries that go into the sandbox and of every file passed in for that run (see [Loader architecture](Loader-Architecture)).

## 5. Input

**The game reads its own I/O structure, not the system's input devices.** This is the central finding for this title.

- **MEASURED**: during calibration the game has **no** `/dev/input` device open and has not loaded SDL (it has loaded `libudev`). An extra virtual input device is therefore not the way in.
- **MEASURED** (run 8, 2026-10-13): the trigger and position of a virtual lightgun arrived at the device and changed nothing in the game.

**Switches** (**MEASURED**, from our helper's source and from the game): the game reads its cabinet switches from a table at `0x180ae80 + 0xaea0`, with `0x50` per switch and `0x2200` per board; the game's getter is at `0x61e890`. Index 0 is start, 1 volume up, 2 volume down, 3 service, 4 diagnostics/test, 5 to 8 coin 1 to 4. A switch is a press counter: the game reads "pressed" from one field of the entry, so a press is a **short pulse** (14 ms for a coin; 300–400 ms for start; longer counts several times).

**The board image** the helper reads is the four-player layout described in [Loader architecture](Loader-Architecture): a bit field at offset 8 (bit 0 test, 1 service, 2 coin 1, 3 coin 2, 4 and 5 volume, then start/trigger/reload/grenade/auxiliary per player from bit 6), and eight axis bytes at offsets 12 to 19.

What was measured with it:

| | Result |
|---|---|
| **TEST** | **Does nothing.** Six TEST pulses on all four boards left the game's log at exactly the same number of lines (256) and seven identical screenshots (**MEASURED**, run 11) |
| **START** | **Is enough.** In three runs in a row, with the test bit never set, START took the game past the file test, past the gun calibration and into the attract loop (**MEASURED**; in run 13 `attract` was reached at 11:56:32 after a start at 11:55:51) |
| **Coin** | Works: `Money In` goes up. Too long a press gives `Detected more coin pulses than safely allowed` (**MEASURED**) |
| **Trigger and aim** | **Not connected yet** |

> **Correction of an earlier claim.** Our own notes said that TEST skips the calibration of gun 1 and START that of gun 2, and the game's calibration screen itself says "Press the TEST button to skip calibration for this gun". On this machine that is not what happens: TEST does nothing and START alone gets through, as measured above. On a different route (the same game started through a Windows-side loader under Wine) pressing Test twice did skip the calibration in earlier measurements; that route was not re-measured.

**Trigger and aim.** The bits and bytes are known on the engine's side (trigger is bit 7 of the field at offset 8; the aim is at offsets 12 to 19). What is missing is **where in the game's memory** they must go. The helper now takes that from a table per game (see "Native games: the input helper" in [Loader architecture](Loader-Architecture)), so once the destination is known it is a table entry, not a code change. **NOT MEASURED**: the destination. One hypothesis is that the four gun buttons are entries 9 to 12 of the same switch table; that can be decided by reading the game's code around its getter.

## 6. Timing

| Measurement | Value |
|---|---|
| First line in the game's log after the start, **warm** file cache | **1 s** (**MEASURED**, 4 runs) |
| The same, **cold** cache (first run in a long time) | **11 s** (**MEASURED**) |
| File test finished after a START pulse | within the same second (**MEASURED**, 3 runs) |
| From start to the attract loop | about 40–75 s (**MEASURED**: 11:56:32 after a start at 11:55:51) |

**A timing comparison only means something if it says whether the run was warm or cold.** The 10-second difference above came entirely from the file cache, not from the display path: the start time was the same with and without a screen as long as the cache was warm (**MEASURED**).

## 7. What we saw on screen

**MEASURED** with screenshots:

1. **Black**, for a few seconds.
2. **The game's file-test screen**: `Welcome to Halo v03.11`, `Performing Integrity Test.`, `Tested: 2232`, `Failed: 1`, `Press START or Ding to skip File Test.`
3. **The gun calibration screen**: `Player 1 gun (left): Pull gun all the way back and towards the left, then press the trigger. ... Press the TEST button to skip calibration for this gun.`
4. **The attract loop**: the title screen, `Press START to Begin`, `Money In: $16.75` — and messages that should not be there: `QR scanners need to be configured!`, `P1 QR Scanner is not detected!`, `P2 QR Scanner is not detected!`, `Factory Setup Has Not Been Completed!`
5. The attract loop **changes level demo** (sound banks `attract`, `level1` ... `level5` are requested in turn). That is the evidence that it is really attract and not a still picture (**MEASURED**, 9.5 minutes over two runs).

## 8. Linux notes

- **The game is already a Linux program** (**MEASURED**). No Wine is needed. On Windows you would need a Linux environment or a loader that translates libraries.
- **The sandbox** (**MEASURED**): `/usr`, `/etc` and `/sys` read-only; `/dev`, `/dev/dri` and `/dev/input` passed through; its own network and process namespace; `/tmp` as a temporary file system; the game's folder read-only with an overlay for writes. If you do not arrange the working directory, `HOME` and somewhere to write, the game tries to write into its read-only source.
- **The input helper is loaded with `LD_PRELOAD`** into the game process only (not into the helper programs the game starts itself), and only acts on this exact build of the game, after comparing bytes at the getter's address. On another build it does nothing and says so.
- **Read the board image with `pread`, not `mmap`** (**MEASURED**, run 4): a writer that truncated the shared file for an instant killed the game with `SIGBUS` through the helper's mapping.
- **Virtual devices** (**MEASURED**): our lightgun is a `uinput` device of the mouse class (`ID_INPUT_MOUSE=1`, no `ID_INPUT_JOYSTICK`). A program that looks for joysticks through SDL does not see it — but, as section 5 shows, that is not this game's input path anyway.
- **Picture**: `gamescope`, either `--backend headless` for runs without a screen or full-screen on the TV. Screenshots in headless runs come from the compositor's capture stream.
- **The camera** (**MEASURED**): the game looks for its camera through V4L2 (`/dev/video0`) and takes a brightness measurement. In our runs it reported `brightness difference of 0`: it looked for a light point and found none.
- **Not needed** (**MEASURED**): kernel modules, kernel changes.

## 9. Status

**Met** (**MEASURED**): the attract loop runs for **6.0 minutes** without interruption in one run, and 9.5 minutes over two runs. The bar is five minutes of attract or game mode — with the note that this is the **attract loop**, not gameplay.

| Works | Does not work yet |
|---|---|
| Starting natively, in the sandbox | Aim and trigger (section 5) |
| Getting past the file test and the calibration with START | Sound (the audio library is a stub) |
| The attract loop, changing level demos | Reliably entering a game from attract: START led to mode selection and mission 1 once, not reproducibly |
| Coin | The cabinet set-up messages on the title screen |

## 10. Open questions

1. **Where does the game read the trigger and the aim?** Next step: read the code around the getter at `0x61e890` and its callers; no run is needed for that.
2. **The QR-scanner and factory-setup messages.** The game knows state names such as `ATTRACT_FACTORY_SETUP_INCOMP`, reads and writes QR pictures as `<path>/webcamID/qr<i>.ppm` and `/pm/qr<i>.ppm`, and the message belongs to a camera step. The message texts are not readable strings in `game` or in the data files examined. Next steps: find where the text comes from, and whether factory set-up is in a service menu (the service switch is index 3) or a flag the game writes itself.
3. **Starting an actual game from attract**, reproducibly.
4. **The gun board.** The game carries its USB libraries statically; we have never seen it enumerate USB devices.

---

*Sources: the game specialist's digest (2026-10-13); section 3 was re-read from the script itself with the launcher's reader. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
