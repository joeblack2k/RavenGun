# Halo: Fireteam Raven

*Raw Thrills, 2018. The game is a 64-bit Linux program, not a Windows one. Our route: native, started
by the kernel's own dynamic linker inside a sandbox, with an input helper loaded into the game. The
five-minute bar is **met**: the attract loop ran for 6.0 minutes without interruption in one run, and
9.5 minutes across two runs.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS, with the game files on network storage that is only read.

## 1. Platform and cabinet

**MEASURED:** the game is a **64-bit Linux ELF** (`ET_EXEC`, `EM_X86_64`) — not a Windows program. It
runs on a Raw Thrills "PC-based" cabinet from 2018 with its own Linux image.

**MEASURED:** the game folder on the cabinet is `/pm/g7/halo`, containing `game` (the game),
`game_original` (the original from before the modification), `halo.so` (a library that ships with it),
`data/` (the game data), `eFileMaps/` (the manifest the game's own file test uses), and `utils/` (the
cabinet's scripts).

**MEASURED:** the game is a **lightgun title for 1 to 4 players**, and the cabinet variants differ.
`rtinit -ct` returns a code that selects the variant: **9** = dual-screen 4-player environmental,
3880×1080; **4** = mounted-gun 2-player, 1920×1080; **3** = super-deluxe 4-player tethered,
1920×1080; **8** = 55-inch dedicated 2-player tethered, 1920×1080.

**MEASURED** from the game itself: resolution, player count and cabinet type are determined at start,
and the game reports its version as `Welcome to Halo v03.11`, `Game Rev: 57529, Library Rev: 1412`.

**INFERRED:** the cabinet has, per player, a gun with an **optically visible point** and a **webcam**;
the game measures a `brightness difference` in the camera image in order to find the gun. Section 5 has
what was measured about this and why it matters.

## 2. What the game needs

- **MEASURED:** 13 libraries in `DT_NEEDED`: `libGL.so.1`, `libGLU.so.1`, `libpthread.so.0`,
  `libdl.so.2`, `libudev.so.1`, `liblapack.so.3`, `libiw.so.30`, **`libfmod.so.10`**,
  **`libfmodstudio.so.10`**, `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`. On an
  ordinary Linux machine 11 of those resolve; **`libfmod.so.10` and `libfmodstudio.so.10` are missing**
  (FMOD 1.10 is proprietary). The game then misses **only sound**: a stub that exports the FMOD names
  and returns error code 52 is enough to get past it (section 10.7).
- **MEASURED:** `libftdi` and `libusb` are **not** in `DT_NEEDED`; they are linked **statically into
  `game`**. The game also resolves its OpenGL, FMOD and udev calls **itself**, with
  `dlopen`/`dlsym`/`dlvsym` through an internal name table (571 names, 474 version tags) — there are no
  FMOD names and no OpenGL symbols in its own dynamic symbol table.
- **MEASURED:** the game needs `data/` (among others `data/g7/*.g7`, `data/ssl/*.crt`, `data/snd`),
  the manifest `eFileMaps/` (7 maps) and write access in `pmuser/`.
- **MEASURED:** `game` is a **modified** binary: 33,554,432 bytes (exactly 32 MiB), against
  `game_original` at 31,270,111 bytes; the start point was moved and the file padded. The game's own
  file test reports exactly that: `FileTest Failure: '/pm/g7/halo/game'`.
- **MEASURED:** that test is **not fatal**: the game reports
  `FileTest Reported 1 errd files at startup!` and continues as soon as the player presses START
  (section 5). The screen it puts up is in section 7.

## 3. The official start script, or why there is none

**MEASURED:** `utils/run_game.sh` (and `utils/go.sh`) start the game on the cabinet. The core lines:

```sh
cd /pm/g7/halo
LD_PRELOAD=/pm/g7/halo/halo.so ${GAMEDIR}/game2 -f${SCREEN_RES} -n
```

with, before it, `utils/rtinit -ct` (which determines `SCREEN_RES`), `export DISPLAY=:0` and a call to
`pulseaudio_set_default.sh`; and after the game a `case` on its **exit code**: **222** = restart,
**223** = reboot, **224** = modification, **225** = factory reset, **226** = power-cycle.

**MEASURED:** `game2` does not exist in the folder. The official start names it; what is actually there
is `game`, the same modified image, and that is what we start.

**WARNING — never execute this script.** It contains, literally: `reboot -f` and `sync`,
`rm -rf ${USERDIR}` (throw the user's folder away), and in `go.sh` also `passwd -l`, `swapoff -a`
and network setup. **MEASURED** from the files. **Read the script and take over only the parts you need**
— here: the arguments, the working directory, and a single probe such as `rtinit -ct` run on its own.
The general reasoning is on the [Official start scripts](Official-Start-Scripts) page.

## 4. The route we use

**MEASURED:** the game starts directly through the **kernel's `ld.so`**, inside a **bubblewrap**
sandbox:

```sh
scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
  --bind-ro <our helper>.so /rg/lib/<our helper>.so \
  --bind-ro <switch image> /rg/jvs --bind-rw <log dir> /rg/tap \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:<our helper>.so \
  --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=400
```

**MEASURED:** what the launcher itself passes (from our log): working directory `/pm/g7/halo`, `HOME`
`/pm/g7/halo`, arguments `-f1920x1080 -n`, and the share as a **read-only lower layer with an
overlay** for everything the game writes.

**MEASURED:** the launcher writes an **identity line** at the top of every log: the path, the
repository commit, the SHA-256 of the script, of the data, of the helpers and of everything that is
passed in — and whether the installed copy of the script is the same as the one in the repository.
That rule is described on the [Loader architecture](Loader-Architecture) page.

**MEASURED:** `halo.so` is **not** an input source. Our own helper puts the cabinet's switches into the
game's switch table (section 5).

## 5. Input

**The switches (MEASURED, from our helper's source and from the game).** The game reads its cabinet
switches from a table at `0x180ae80 + 0xaea0`, with **0x50 per switch** and **0x2200 per board**; the
getter inside the game sits at `0x61e890`. The indices are: **k 0 = start, 1 = volume up, 2 = volume
down, 3 = service, 4 = diag/test, 5..8 = coin 1..4**. A switch is a **counter of presses**: the game
reads `[input+4] != 0` as "pressed", so a press is a **short pulse** — 14 ms for a $0.25 coin,
~300–400 ms for start; a longer press counts several times per frame.

**The board format the cabinet uses (`WartranTroopers`, MEASURED from the profile).** A 64-byte image,
with a 32-bit button field at **offset 8** and the aim at **offset 12..19**:

```
bit 0 test      bit 6 p1.start    per player: p1.trigger = bit 7, p1.reload = 8,
bit 1 service   ...               p1.grenade = 9, p1.aux1 = 10
bit 2 coin 1                      (p2.start = bit 11, p2.trigger = bit 12, ...)
bit 3 coin 2
bit 4/5 volume up/down
AXES: 1 byte per axis at offset 12..19 -> p1.x 12, p1.y 13, p2.x 14, p2.y 15,
      p3.x 16, p3.y 17, p4.x 18, p4.y 19; min 0, max 255; left/top = 0;
      inside the screen the axes stay at 2..253 (the cabinet reads an axis
      <= 1 or >= 254 as "off screen")
```

**What was measured about that, one item at a time:**

- **MEASURED: TEST does nothing.** Six TEST pulses on all four boards left the game's log at exactly
  the same number of lines and seven identical screenshots behind.
- **MEASURED: START is enough.** In three runs in a row, with bit 0 **never** set, START took the game
  through the file test, through the calibration and into the attract loop.
- **MEASURED: the coin works** (bit 2): `Money In` goes up; a press that lasts too long gives the
  warning `Detected more coin pulses than safely allowed`.
- **NOT YET CONNECTED (MEASURED that the game does not receive it): the trigger and the aim.** Our
  helper writes the switch fields and does not write the button bits 7..10 and the eight axis bytes;
  the destination addresses in the game for those bytes have **not been measured**. What we tried instead — the trigger of a
  virtual evdev lightgun — arrived at the device but not at the game, because the game does not read
  its gun through evdev.
- **MEASURED:** during the calibration the game holds **no** `/dev/input` device open and has **not**
  loaded SDL (it does load `libudev`). An extra virtual input device is therefore not the route for
  this title.
- **The camera.** **MEASURED:** on this machine `/dev/video0` exists (`gspca`, "USB Camera-B4.09.24.1")
  and the game does its **brightness measurement** there (`Baseline %u: %u`,
  `New lum ... (diff:%u)`) to find its webcams and the gun. In our runs the game reported
  `brightness difference of 0`: it looked for a light point and found none. **INFERRED** (a strong
  inference, not proof): for this game the gun has to be **seen** by the camera, which is a different
  channel from the switches, and a virtual device is invisible to a camera. The proof would be the
  screen moving once something visible stands in front of the camera.

## 6. Timing

**MEASURED**:

| Measurement | Value |
|---|---|
| First line in the log after the start, **warm** file cache | **1 s** (4 runs) |
| The same, **cold** cache (first run in a long time) | **11 s** |
| File test finished after a START pulse | In the same second (3 runs) |
| From start to the attract bank | ~50–75 s (start 11:55:51, attract bank 11:56:32) |

**Warning:** a time comparison between runs says something only if it states whether the run was
**warm or cold**. The difference of ten seconds above came entirely from the disk cache and not from the
picture path (**MEASURED**: the same start-up time with and without a screen, as long as the cache was
warm).

## 7. What we saw on screen

**MEASURED** with screenshots and from the game's own log:

1. **Black** for a few seconds (screenshots of 11.8 kB).
2. The game's **file test screen**: `Welcome to Halo v03.11`, `Performing Integrity Test.`,
   `Tested: 2232`, `Failed: 1`, `Press START or Ding to skip File Test.`
3. The **gun calibration screen**: `Player 1 gun (left): Pull gun all the way back and towards the
   left, then press the trigger. ... Press the TEST button to skip calibration for this gun.`
4. The **attract loop**: the title screen with two marines, **`Press START to Begin`**,
   **`Money In: $16.75`** — and the messages that are still on it and have to go:
   **`QR scanners need to be configured!`**, **`P1 QR Scanner is not detected!`**,
   **`P2 QR Scanner is not detected!`**, **`Factory Setup Has Not Been Completed!`**
5. The attract loop **changes level demo** (the banks `attract`, `level1` … `level5`) — which is the
   proof that it really is attract mode and not a frozen picture (**MEASURED**, 9.5 minutes across two
   runs).

## 8. Current state and open questions

- **MET (MEASURED):** the attract loop ran **6.0 minutes** without interruption in one run (and 9.5
  minutes across two runs). The bar for this set is **five minutes of attract mode or game mode**, so
  it is met — with the note that it is the **attract loop** and not the game itself.
- **What works:** starting without any injected core, passing the file test, passing the calibration,
  the attract loop, coins, and the identity line in the log. **Sound does not** (FMOD 1.10 is missing;
  the stub returns code 52).
- **OPEN 1 — the QR and factory messages.** These have to go, and they are the remaining acceptance
  point for this title. What we know: the game knows the state names `ATTRACT_FACTORY_SETUP_INCOMP`
  and `ActivateCell_FactorySetup_INST`; it writes and reads QR images as `<path>/webcamID/qr<i>.ppm`
  and `/pm/qr<i>.ppm`; and the message belongs to a **webcam step**
  (`All webcams reported brightness difference of 0`). The message texts themselves are **not** in
  `game` as readable text and not in the data files we examined. **Next steps:** find the source of the
  message text (an archive, or text built up at run time), and establish whether the factory setup is a
  **service menu** item (the service switch is k 3) or a flag the game writes itself in `pmuser/`.
- **OPEN 2 — aim and trigger.** The bytes are known (bit 7 of offset 8, and offsets 12..19), but our
  helper does not write them. That is code, not a measurement.
- **OPEN 3 — starting the game itself.** In the attract loop, START does not reproducibly lead to game
  mode (once it did, with a mode choice and mission 1).
- **OPEN 4 — the FTDI gun board.** The game has libftdi and libusb linked into itself statically; we
  have never seen a USB enumeration from the game.

## 9. What we tried and what did not work

Per item: what we tried, what we saw, and what we were allowed to conclude from it. These are the
negatives and the lessons, and they are worth as much as the things that did work.

1. **The rule "TEST skips the calibration of gun 1" — MEASURED FALSE on this setup.** Our own note said
   that the test button skips player 1's calibration and that START does player 2's. In one run we gave
   **six TEST pulses on all four boards**: the game's log stayed at **exactly 256 lines** and all seven
   screenshots stayed on the calibration screen (1.47 MB each). After that, **four START pulses**
   moved the game on (the log started growing, the screenshots jumped to 2.7–3.1 MB). In another run
   bit 0 (test) was **never** set and the attract loop appeared anyway. **Conclusion:** START alone is
   enough; the TEST rule belongs out of the documentation.
2. **The board hypothesis — MEASURED, but not by the run that was supposed to prove it.** One run
   seemed to show that a TEST press on four boards caused the crash. That run changed **three things at
   once** (the helper, the writing method and the scope of the TEST), so it did **not** isolate the
   board hypothesis. The real cause of that crash was something else (item 5). **Lesson:** a run that
   changes more than one thing proves neither.
3. **The joystick hypothesis — MEASURED and refuted.** We thought the game looked for its gun through
   **SDL joysticks** and that we therefore had to offer a virtual joystick device. One run showed that
   the game keeps **no** `/dev/input` device open during the calibration and that **SDL is not loaded**
   (it does load `libudev`). **Conclusion:** building an extra input device would be work the game
   never looks at; the input runs through the board channels (section 5).
4. **The optical-aim hypothesis — MEASURED, and then withdrawn.** We read in the log that the game
   measures a `brightness difference` and concluded from that that the aim was **optical** (a gun with
   a light point for the camera). Later it turned out that the aim runs through the **board channels**
   of the `WartranTroopers` model (bits plus eight axis bytes) and that the webcam step is something
   else: it also failed on a day when the game **did** reach the attract loop. **Conclusion:** the
   webcam step is not the difference, and the aim is a board channel.
5. **The crash in our own input helper — MEASURED and fixed.** One run died with SIGBUS and the stack
   trace named **our** helper at `+0x1f44`. That address is `movzbl 0x8(%rdi)`: reading the **mapped**
   switch image. The cause was the writing method: `open(...,"wb")` **truncates** the file, and a
   truncated file behind a mapping gives the kernel a reason to send SIGBUS. Proven without a game
   running at all (an mmap reader dies, a pread reader stays alive) and fixed on both sides: the helper
   now reads with `pread` and the pulses write in place.
6. **The time measurement — MEASURED, and our own explanation refuted.** The first run gave **11 s** to
   the first line in the log, every later run **1 s**. We had written down beforehand that the file-test
   moment would stay the same and that a large difference would be a finding about our measurement
   setup. The difference turned out **not** to come from the screen: a **headless** run directly after
   a TV run also gave **1 s**. **Conclusion:** it is the **file cache** (cold 11 s, warm 1 s), and every
   time comparison has to state whether the run was warm or cold.
7. **Our own test overriding that removed the working press — MEASURED.** In three runs we used a probe
   that **overwrote** the helper's default table and limited the TEST press to **board 0**. Those runs
   failed, and we concluded that TEST did not work. When the probe was removed, it worked immediately.
   **Lesson:** a probe that changes the default table changes the test — and then you measure your own
   intervention.
8. **"We need the Move" — MEASURED FALSE.** We assumed we had to connect the Move because the game was
   supposed to ask for a gun. The project turns out to have a **mouse mode** with which you drive a gun
   without a Move, and that is the route for input. For the **aim** of this game the Move is not needed
   either, because the aim runs through the board channels.
9. **What is still on the screen — MEASURED, and not solved.** The messages about the QR scanners and
   the factory setting are **attract states** (`ATTRACT_FACTORY_SETUP_INCOMP`, `ATTRACT_WEBCAM_CONFIG`,
   `P%u_WEBCAM`, `ATTRACT_GUN%i_DISABLED`, `ATTRACT_GUN%i_MISSING`). They mean "this cabinet has not
   been set up", not "a scanner is missing". The next step is the cabinet's **service menu** (reachable
   with the service switch, k = 3), where the hardware and the online setup belong — that is reading
   plus one targeted run, not imitating hardware.

## 10. What is Linux-specific, and what differs on Windows

Per point: **MEASURED** or an assumption, and what someone on Windows would do differently.

**1. The native route on Linux. MEASURED.** This game is a **Linux ELF** and runs on Linux **natively**:
no Wine, no Windows layer. We start the ELF with the **kernel's `ld.so`** and our own runtime helper
(the route of `scripts/play-native.sh`). The game resolves its own imports
(`dlopen`/`dlsym`/`dlvsym`), so no import reconstruction is needed.
*On Windows* you could not start this game directly: you would need a Linux environment or an ELF
loader with library translation. On Linux the route is the simpler one.

**2. The sandbox and the display. MEASURED.** We run the game in **bubblewrap**: `/usr`, `/etc` and
`/sys` read-only, `/dev`, `/dev/dri` and `/dev/input` passed through, its own network and PID
namespace, `/tmp` as a tmpfs, and the game folder as a **read-only lower layer with an overlay** for
everything the game writes (it writes in `pmuser/`, see sections 2 and 7). For the picture we use
**gamescope**: `--backend headless` for tests without a screen, or full-screen on the TV. The
screenshots in section 7 were taken with gamescope's own recorder (and with a desktop screenshot tool
on the real screen).
*On Windows* you put a game in a folder and hope it finds its paths; on Linux you have to arrange the
working directory, `HOME` and the write permissions explicitly, or the game writes into its read-only
source.

**3. The input helper. MEASURED.** The cabinet switches come in through a helper that is loaded into
the **game process** with **`LD_PRELOAD`**. That helper reads the **shared memory of the I/O bridge**
(64 bytes) and writes the switches into the game's **switch table** at `0x180ae80 + 0xaea0`, with
**0x50 per switch** and **0x2200 per board** (the getter sits at `0x61e890`). Two warnings: the helper
loads **only in the process `.../game`** (not in the utilities the game starts itself) and **only for
this game version**, after a **byte check** on that getter — if it does not match, the helper does
nothing and says so.
*On Windows* `LD_PRELOAD` does not exist; there you would write the same bytes through DLL injection or
a memory tool, and then you lose the process check that is free here.

**4. The virtual devices. MEASURED.** Our lightgun is a **`uinput`** device (vendor id `1209`) and
deliberately a **mouse-class** device: `udevadm info` reports `ID_INPUT=1`, **`ID_INPUT_MOUSE=1`** and
**no** `ID_INPUT_JOYSTICK` (`ABS_X`/`ABS_Y` plus `BTN_LEFT`). That is needed for a front-end that
selects devices by their kernel handler (`mouseN`), but it also means that a **native SDL query** does
**not** see this device as a joystick — SDL looks at `ID_INPUT_JOYSTICK` on Linux. **And for this game
that is not the solution:** during the calibration the game opens **no** `/dev/input` device and has
**not** loaded SDL (see section 5). An extra virtual input device is therefore not a route here.
*On Windows* you would make a virtual HID device (DirectInput/HID); on Linux `uinput` is the way, and
for games that look for their board through HID, `uhid` is the heavier but more faithful variant.

**5. The engine's mouse mode. MEASURED.** To drive a gun without a Move, this project has a mouse mode:
`ravengunctl mice` lists the mice, `ravengunctl mouse-gun --player N --mouse <id>` attaches one to
player N, `ravengunctl arm` makes the virtual gun active, and with `ravengunctl test-pattern point X Y`
plus `ravengunctl output-button trigger tap` you send position and trigger. The engine is the only
owner of the hardware; clients talk to it over a socket.
*On Windows* you would need a virtual mouse with absolute axes; on Linux `uinput` provides that.

**6. The camera. MEASURED.** On this machine `/dev/video0` exists (`gspca`, "USB Camera-B4.09.24.1") and
the game does its **brightness measurement** there to find its webcams and the gun. In our runs it
reported `brightness difference of 0` (section 5). *On Windows* the camera API is a different one; on
Linux V4L2 is the way and the device has to be passed into the sandbox.

**7. The FMOD stub. MEASURED.** `libfmod.so.10` and `libfmodstudio.so.10` are proprietary and are not
on this machine. We supply a **stub** that exports the FMOD names and returns **error code 52**. That
52 in the log is therefore **working tooling**, not a fault: the game carries on without sound. Without
the stub the game stops with a jump to address 0 (measured earlier). *On Windows* the problem is the
same, only with a DLL instead of a `.so`.

**8. What was not needed. MEASURED.** No kernel modules, no ELF loader of our own for this game, and no
kernel change. The only device permissions that had to be arranged were those on `/dev/input/*`
(uinput) and `/dev/dri/*` (picture).

**9. udev rules and permissions. MEASURED.** A `uinput` device gets `/dev/input/eventN` as
`root:input 0660`; without a udev rule with `uaccess` (or group membership) an ordinary user cannot
read it. This project installs a rule per vendor/product id for that.

**10. X/Wayland. MEASURED.** For a picture on a TV we use `gamescope` (with `--backend headless` for
tests without a screen, or full-screen on the TV). On an ordinary desktop an X11/Wayland display with
the right `DISPLAY`/`XAUTHORITY` is enough; the game draws through OpenGL/GLX.

**11. On Windows (INFERRED).** This game could only run there through a Linux subsystem or an ELF
loader with Wine-like library translation; the route above is the Linux route and it is the simpler
one, because the game is already a Linux program.

---

*Everything above was measured on one machine: a Steam Deck running SteamOS, with the game files on
network storage that is only read. Where a claim is labelled MEASURED there is a number, timestamp,
path, index or offset with it; where it is labelled INFERRED or assumption it is a conclusion or a
guess.*
