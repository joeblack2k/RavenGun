# Transformers: Shadows Rising

*Sega RingEdge 2, built as a "Game Shell" title, 2018. The game is a 32-bit Windows program. Our
route: Wine with the OpenParrot loader, a core, and a virtual gun board. The five-minute bar is
**met**: the attract loop was visible for about 8.5 minutes.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS under plain Wine, with the game files on network storage that is only read.

| | |
|---|---|
| Game | Transformers: Shadows Rising |
| Platform | Sega RingEdge 2, "Game Shell" build |
| Year | 2018 |
| Kind | Lightgun shooter, 2 players |
| Folder on our share | `Teknoparrot/Transformers Shadows Rising` (and, as a copy, `Teknoparrot/Sega`) |
| Build | `shell/Shell.exe` (Game Shell 180605) starts `Transformers2/Transformers2.exe` |
| Our status | **Playable with a PlayStation Move as the gun**; the five-minute bar is met (section 8) |
| Not finished | the cabinet I/O board (lamps, key panel, hardware recoil) — see sections 5 and 8 |

## 1. Platform and cabinet

**The platform (MEASURED).** The game is a Sega RingEdge 2 title from 2018, built as a **"Game Shell"**:
not a single program, but a small Sega environment (`shell/`) that starts the actual game
(`Transformers2/`) and then keeps an eye on it. Both are 32-bit Windows programs (PE32, i386):

| File | Size | Role |
|---|---|---|
| `shell/Shell.exe` | 1,438,720 bytes | The Game Shell: reads the installation, starts the game, tracks the state |
| `Transformers2/Transformers2.exe` | 9,332,736 bytes | The game itself (window title `TF2_Gun \| Release`) |
| `Launcher/SystemLauncher.exe` | 514,048 bytes | Sega's own start program; reads `Launcher/Launcher.ini` (`ShellDirectory=c:\sega\shell`) |

The shell expects a fixed installation folder: the shell's `Install.ini` says
`Directory=C:\Sega\Shell` and the game's says `Directory=C:\Sega\Transformers2` (both
`Version=180605`, manufacturers "Hollywood Gaming" and "SEGA Amusements" respectively).
`shell/Game.ini` also sets `AssetDir=C:\sega\shelldata`. **MEASURED** from those ini files.

**The two processes (MEASURED).** The loader starts **one** process, `Shell.exe`. That program in turn
starts `Transformers2.exe` and stays alive afterwards as a companion. There is therefore no "start two
executables": it is a chain of two. `Shell.exe` contains the HID and SetupDi code (below);
`Transformers2.exe` does **not** — it reads its gun through DirectInput.

**The cabinet's board layout (MEASURED).** The cabinet has two input devices that have nothing to do
with each other, and that is the most important thing for an emulator to know:

1. **The gun** — a **DirectInput joystick**. `Transformers2.exe` opens the system's first joystick
   (`CreateDevice(GUID_Joystick)`, with no filter on vendor, product or name), sets every axis to the
   range −1000…1000 and reads an 80-byte `DIJOYSTATE`. Horizontal position comes from axis `lY` and
   vertical from `lX` (player 2: `lRx` horizontal, `lZ` vertical); the game converts with
   `(1000 − value) × 255 / 1000`, so left/top is the low end. In the game's linker map that class is
   called `CSAEBoard` (object `saebd.obj`). **MEASURED** from the code and confirmed on the TV.
2. **The cabinet I/O board** — a **HID device**. `Shell.exe` imports `HID.DLL` (`HidD_GetHidGuid`,
   `HidD_GetAttributes`, `HidD_GetPreparsedData`, `HidP_GetCaps`) and `SETUPAPI.dll`
   (`SetupDiGetClassDevsA`, `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
   `SetupDiDestroyDeviceInfoList`) and looks for a HID device with them. That board drives the lamps,
   the external key panel and the hardware recoil. **MEASURED** from the import tables.

**How the shell recognises that I/O board (MEASURED).** Not by vendor/product id and not by a name: the
shell walks a built-in table that maps a (HID usage page, HID usage) pair to a name (`GAMEPAD`,
`JOYSTICK`, `SYSTEM`, `TRACKBALL`, `KEYBOARD`) and writes its reports to the device that comes through
as **Generic Desktop (`0x0001`) / Game Pad (`0x0005`)**. It then writes **3 bytes** to the board in the
form `00 <a> <b>` (report id 0, then two data bytes); the length it writes is
`HidP_GetCaps().OutputReportByteLength`. There is also a literal name (`POLLING FLAGS`) in that code,
but that branch is dead: the comparison can never be true and the result is not read anywhere. Whoever
wants to imitate this board must therefore get **the usage** right, not a name or a vendor/product id.
**MEASURED** (three independent pieces of evidence in our notes).

**INFERRED.** `Transformers2.exe` also contains code for LED tracking and for a camera model
(`LED.obj`, `TF2LED.obj`, `spk_camera_motion_nng20.obj` in the linker map, and
`Sega\libs\HookedWindows.txt` mentions an infrared camera gun and a second gun model). That points to an
infrared camera gun whose raw LED positions go to the PC. Our setup does not use that path — we feed
the DirectInput joystick — but whoever wants to imitate the real cabinet should know that the code is
there.

## 2. What the game needs

**The files (MEASURED).** The game folder is complete. One copy counts **1,748 files**; at the top
level there are `GameData`, `Launcher`, `ShellData`, `Transformers2`, `shell` and `temp`. Note a
peculiarity of our share: in the folder `Transformers Shadows Rising` the whole tree stands **twice** —
once at the top level and once under `Sega/` — which makes `find` count **3,498 files** and `du`
**18 GB** there. It is a real second copy (different inodes), not a hard link. The game does not care,
but anyone re-checking the numbers should know it.

The only two empty files in the tree are **not** a defect: `Transformers2/TF2_Gun_R.ilk` (a leftover of
the incremental linker) and `shell/install.crc`. The shell's `Install.ini` mentions `FilesCount=69` and
the game's `FilesCount=359` — those are the installer's own counts, not the file system's.

**The start ladder (MEASURED).** What happens, in order, with the files that show it:

1. The shell reads its `Install.ini`/`Game.ini`, checks whether the folder `GameData` exists (its
   `Mainthread` log: `Validated GameData directory exists`) and sets its window to 1920×1080.
2. The shell closes itself with the line **`Closing down shell ready for game launch`** and then starts
   `Transformers2.exe` from the same share; the log says literally
   `Launched z:\<the share path>\...\Transformers2\Transformers2.exe`.
3. The game loads, shows a black loading screen first and then comes into its **attract loop**.
4. The shell stays alive afterwards as a companion: it translates the game's messages (`GAME_MSG_*`:
   attract, stage, score, health, `GAME_MSG_TRIGGER_RECOIL`) to the cabinet — lamps, panel, recoil.

**What the shell passes to the game (MEASURED).** Both sides use the same message vocabulary
(`GAME_MSG_ATTRACT_START`, `GAME_MSG_STAGE_START`, `GAME_MSG_CURRENT_SCORE`, `GAME_MSG_CURRENT_HEALTH`,
`GAME_MSG_TRIGGER_RECOIL`, and so on) — that is the channel between the two processes. The shell also
sends `MAINTHREAD : Sending Power up Credit` once the game runs. The names are in both binaries, which
confirms that this is one protocol between two Sega processes and not something of ours.

**What the game itself needs (MEASURED):**

- Starting `Transformers2.exe` **alone** does not work: it stays on
  `SYSTEM INITIALIZING... LOADING..` for more than 50 seconds. The shell is therefore mandatory, not
  optional.
- Under Wine the prefix needs **native** `d3dx9_43`, `d3dcompiler_43` and `d3dcompiler_47`. Without
  those three the game stops with a Windows error window:
  `E5017: Aborting due to not yet implemented feature: Writing fx_2_0 sampler objects initializers is
  not implemented / Write pass assignments` — Wine's HLSL effect compiler cannot compile this shader
  effect; the Microsoft version can.
- The game is 32-bit and therefore uses the **32-bit** core, not the 64-bit variant. **MEASURED** with
  the start plan (`is64: False`).
- The folder structure has to match: the shell works with `C:\Sega\shell` and
  `C:\Sega\Transformers2`. In our prefix `drive_c/Sega` is a symbolic link to the folder on the share.

## 3. The official start script, or why there is none

**There is no official batch or shell script (MEASURED).** In the entire game folder (3,498 files)
there is not a single `.bat`, `.cmd` or `.sh`. Sega's official start path is a program, not a script:

```
Launcher/SystemLauncher.exe   (reads Launcher/Launcher.ini: ShellDirectory=c:\sega\shell)
      -> shell/Shell.exe      (Game Shell 180605)
           -> Transformers2/Transformers2.exe
```

Compare that with the sister title *Transformers: Human Alliance*, where a `game.bat` does exist that
first performs a hardware check and then starts the game — such scripts therefore do exist in this
family, but not for Shadows Rising.

**Warning, and it holds for every title in this set: we never execute such scripts.** A manufacturer's
start script is a program we do not know; it can check hardware, touch files or perform process checks
that we cannot oversee. We read such a script only to see **what** it does (which executable, which
arguments, which helper files, which environment variables) and take over only the parts we need
ourselves. For this title that was nothing: there is no script, and the start path above can be
reconstructed completely from `Launcher/Launcher.ini` and the `Install.ini` files. The general
reasoning is on the [Official start scripts](Official-Start-Scripts) page.

## 4. The route we use

We start the game with our own launcher and our own input bridge, without the commercial front-end. The
launcher is `scripts/launch-direct.py`; it reads the profile and starts `Shell.exe` with the right
arguments and environment. The profile (`GameProfiles/TransformersShadowsRising.xml`, revision 10) says:

| Field | Value | Meaning |
|---|---|---|
| `EmulatorType` | `TeknoParrot` | The loader plus the core, not one of the other emulator types |
| `EmulationProfile` | `TransformersShadowsRising` | The name of the profile |
| `ExecutableName` | `shell.exe;Transformers2.exe` | The profile's **list of names** |
| `GamePath` / `[Identify] Path` | `shell/Shell.exe` | What is actually started |

**IMPORTANT, and this is a trap (MEASURED).** Those two names in `ExecutableName` are **not** the
loader's "two executables" mechanism. That mechanism is called `HasTwoExecutables` with an
`Executable2`, and it is for a game that has a separate helper program that must run next to it: such
a profile is planned as **two** starts, with a wait between them (the
[Loader architecture](Loader-Architecture) page describes it, and *House of the Dead: Scarlet Dawn*
on this wiki is the example). For this title, `ExecutableName` is only the list of names from the
profile; the launcher starts **one** process (`shell/Shell.exe`) and the **shell** then starts the
game itself. Reading those two mechanisms as one is what makes the difference look like a fault in
the launcher.

**The input bridge and the boards.** Our engine imitates the input devices the game expects. The boards
are **data**, in `packaging/hid-boards.json`, not code per game:

| Board | Transport | What it is |
|---|---|---|
| `SaeGunBoard` | `uinput` (evdev) | The gun: a virtual joystick that DirectInput sees. Axes `X = p1.y`, `Y = p1.x`, `Z = p2.y`, `RX = p2.x`, with an output range of −217…557 inside −1000…1000 (that is the part of the range the game treats as the whole screen) |
| `SaeIoBoard` | `uhid` | The cabinet I/O board: a real HID device, Generic Desktop / Game Pad, with a fixed input report of 15 bytes and an output report of 3 bytes |

Both boards can run at the same time, with a **list** in the per-game settings:
`Boards = SaeGunBoard, SaeIoBoard` (section 5 says what measurably works).

**The preparation step (MEASURED, and required).** The I/O board uses `/dev/uhid`, and on Linux that is
writable by root only by default. Without write permission **that board fails and with it the whole
route — including the gun**, because a board that does not open stops the route. Check it before every
run:

```sh
# write permission on /dev/uhid, and whether the udev rule is in place
bash scripts/udev-ravengun.sh --check          # names 71-ravengun-uhid.rules as current
ls -l /dev/uhid                                 # must be writable for the user
sudo bash scripts/udev-ravengun.sh --apply      # once, if the rule is missing
```

**The user layer.** A game's shipped profile lives in `patches/<Game>/game.ini`. A second layer,
`~/.local/share/ravengun/patches/<Game>/game.ini`, goes over it **per key**: you can add or change one
line without touching the shipped state, and deleting the file puts everything back. For this title we
used that to add the I/O board:

```ini
; ~/.local/share/ravengun/patches/TransformersShadowsRising/game.ini
[Input]
Boards = SaeGunBoard, SaeIoBoard
```

**The exact commands (this is the working route).**

```sh
# 1. what is the launcher going to do? (executable, environment, prefix, which core, which launcher version)
python3 scripts/launch-direct.py TransformersShadowsRising --plan

# 2. the run itself -- NOTE: this one, and NOT a loose start test
bash scripts/play-halo.sh --profile TransformersShadowsRising --no-wait-move
```

Two things we learned from this, each of which can cost you one measurement:

- **The play script arms the input route; a loose start test does not.** A start test that only calls
  the launcher and never starts the I/O bridge leaves **no virtual board at all** — the engine says so
  itself: `IO bridge: none (… route evdev)`. The game starts anyway, but there is no gun and no I/O
  board.
- **Use the launcher from the repository, not an installed old copy.** `--plan` now says which launcher
  is running, with its path and SHA-256, and whether the installed copy is a different version. We used
  the repository version: `scripts/launch-direct.py`, SHA-256
  `327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0` (git `89877f1`), with the engine
  from the repository build (`build/ravengund`, SHA-256
  `d6c5b596ee3fd84a4724ebbe7a23b241b94c2a545cb3299514b57409b4d6871e`). **MEASURED.**

## 5. Input

### 5a. The gun — this works (**MEASURED**)

- The game reads the **system's first joystick**. Whoever builds their own device must therefore make
  sure that device gets joystick id 0; otherwise the game picks another device (in our first attempt
  the PlayStation Move itself, which Wine also sees as a joystick) and the crosshair sits in a corner.
- Our board is called `RavenGun SAE Gun Board`, has vendor/product **`0x1209`/`0x72F0`** and comes out
  through `uinput` (evdev); Wine turns that into a HID device itself
  (`HID\VID_1209&PID_72F0`).
- Wine remembers joystick ids per product in the registry under
  `HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput\VID_xxxx&PID_xxxx\Calibration\N`,
  value `Joystick Id`. A helper script (`scripts/wine-joyid.sh`) sets that correctly.
- **The check you do before every run** (this is the first question, before you look at the game or the
  I/O board):

  ```sh
  RG_WINE=~/.local/share/ravengun/wine/proton-11.0-2/bin/wine
  WINEPREFIX=~/.local/share/ravengun/prefixes/ravengun-test-tsr WINEDEBUG=-all "$RG_WINE" reg query \
    'HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput' /s \
    | grep -E 'Calibration|Joystick Id'
  ```

  Expected: `VID_1209&PID_72F0` on **0**. If it is not, you see a crosshair in a corner and the X axis
  does nothing — and that is a joystick-id problem, not a board problem. Repair:
  `scripts/wine-joyid.sh <prefix> 1209:72F0` (set, not read).
- **The range:** the game sets every axis to −1000…1000 itself and then passes the result through its
  own calibration. With the full range the crosshair sticks to the edges; with an output range of
  −217…557 on our board the crosshair sat on all raster points **within 1.5 % of the screen**.
  **MEASURED** on the TV (2026-10-11).

### 5b. The cabinet I/O board — this does NOT work yet, and here is the key (**MEASURED**)

This is the most honest and most useful part of this page, because it is exactly where a next attempt
should begin.

- **What does work:** our engine **creates** the board correctly. It exists in the kernel:

  ```
  /sys/class/hidraw/hidraw4/device/uevent
  HID_ID=0003:00001209:000072F1
  HID_NAME=RavenGun SAE IO Board
  HID_UNIQ=ravengun-board
  ```

  (`0003` = `BUS_USB`, vendor `1209`, product `72F1`.) The engine also logs the route with both boards:
  `IO bridge: hid-board for TransformersShadowsRising (SaeGunBoard via uinput, SaeIoBoard via uhid)`.
- **What does NOT work:** `Shell.exe` does not find the board. Its own log says literally:

  ```
  [ 09:33:59 ] HIDioThread : IOBOARD_INITIALISE
  [ 09:33:59 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:00 ] HIDioThread : IOBOARD_INITIALISE
  [ 09:34:00 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:14 ] HIDioShutdown : Called
  ```

  It tries twice, then gives up, and there is **no** `Initialised OK` and no continuation of the board
  dialogue. (The shell starts this search immediately at start-up, before the game begins — so the
  board has to be there before the shell starts.)
- **Neither reader sees the board.** Not the shell through its `SetupDi`/`HidD` route, and not Wine's
  DirectInput either: the registry holds **no** `VID_1209&PID_72F1`. That last fact is good news for
  section 5a: the I/O board therefore cannot take joystick id 0 away from the gun.
- **THE INDICATION FOR WHOEVER FINISHES THIS:** in `/sys/class/hidraw/hidraw4/device/uevent`
  **`HID_PHYS=` is empty**. Every real USB HID device in the same list has a bus path there
  (`HID_PHYS=usb-0000:0c:00.0-3/input0`, and so on). A `uhid` device has no physical bus, and that is a
  concrete suspicion for why Wine does not take the device into its HID collection and why the shell's
  `SetupDi` enumeration comes up empty. Whoever wants to get this board working should first find out
  **whether Wine passes on devices without a bus path at all** — and if not, whether that can be solved
  (a device with a simulated bus path, or a different transport) before any work goes into the protocol.
- **What stands ready after that:** the board description (usage Game Pad, fixed input report of 15
  bytes, output report of 3 bytes whose form `00 <a> <b>` is measured) and the answers the shell
  expects per step. All of that was taken out of `Shell.exe` and is in our notes; the public core of it
  is in section 1 of this page.
- **Practical consequence in our setup:** as long as this does not work, the I/O board is optional and
  must be able to **fail without taking the gun with it**. That is why it lives in the user layer
  (section 4) and not in the shipped profile: remove one file and the title is as it was.

## 6. Timing

**MEASURED**, with the machine's clock. Note that the **shell's** logs are written in a time zone
**2 hours behind** the Deck; everything below is local time, and the shell's line and the process
measurement match exactly (so that was checked, not assumed).

| Moment | Run 1 | Run 2 |
|---|---|---|
| Loader (`OpenParrotLoader.exe`) seen | 11:22:02 | 11:33:5x |
| `Shell.exe` seen | **11:22:03** | **11:33:57** |
| "Closing down shell ready for game launch" | 11:22:21 | 11:34:14 |
| `Transformers2.exe` seen | **11:22:26** (+23 s after the shell) | **11:34:19** (+22 s) |
| Attract starts (`GAME_MSG_ATTRACT_START`) | **11:23:44** (+1 min 42 s after the loader) | — |
| "Sending Power up Credit" | 11:23:44 | 11:35:35 |
| Game stopped (end of measurement) | ±11:32:45 → **about 10 min 19 s alive** | 11:36:44 (measurement aborted) |

So: **from shell to game it is 22 to 23 seconds**, and the attract is there **about 1 minute 42** after
the loader. The often-mentioned "3.5 to 4 minutes of loading" was **not** seen in these runs; it was
clearly faster (probably a warm cache). Count on a few minutes for a cold machine and measure it
yourself.

## 7. What we saw on screen

**MEASURED** with screenshots (1920×1080) at fixed times, plus the shell's logs. The file size of such a
screenshot is a rough measure of "how much there is to see": a nearly black picture is 10–130 kB, a real
game picture 1.5–3.5 MB.

| Time | What was on the TV | Screenshot size |
|---|---|---|
| 11:22:02 | Almost black — the shell window was not there yet | 11.9 kB |
| **11:22:32** | **The Sega Game Shell splash**: "Game Shell / Version 180605 / No Updates Found" | 1.6 MB |
| 11:23:03 and 11:23:33 | Almost black: the game's loading screen | 11.5 kB |
| 11:24:04 → 11:32:41 | **The game**, alternating between colourful attract images and black transitions | 85 kB – 3.5 MB |
| **11:25:04** | The game **full screen** with attract art and the text **"PRESS START BUTTON / FREE PLAY"** | 3.25 MB |
| 11:33:11 | Back to the Deck's desktop (the game had stopped) | 127 kB |

The order is therefore: **shell splash → black loading screen → attract**. What you see in the
screenshots alternates roughly every 30 seconds between a colourful image and a nearly black
in-between image; that is the attract loop alternating logos and videos.

## 8. Current state and open questions

**What is met (MEASURED).** The measure we hold per title is **five minutes of attract mode or game
mode**, and for this title it is **MET**: the attract started at **11:23:44** and the game stood
visibly on the screen until **11:32:41** — about **8.5 minutes** — while the shell's state machine
restarted the loop **five times** (11:23:44, 11:25:31, 11:27:04, 11:29:06, 11:30:33).

**An honest note on that measure.** In this title the text "PRESS START BUTTON / FREE PLAY" stands
**continuously** over the attract. From the picture alone, "attract" is therefore not distinguishable
from "the start screen"; the **log** shows the real cycle (five restarts, with logos and videos in
between). Whoever wants to apply the measure strictly should judge it through the log and not through
one screenshot. And the measurement was not clean: see section 9.

**What is still open.**

1. **The cabinet I/O board** (section 5b). The board is created, but neither reader sees it. The
   question is whether Wine passes on HID devices without a bus path; the empty `HID_PHYS=` is the
   indication there.
2. **The board dialogue after that:** which steps the shell performs once it has found the board, and
   which answers it expects. That was taken out of the binary and prepared, but never run for real —
   simply because step 1 does not work yet.
3. **Lamps, key panel and hardware recoil**: that is what this board ultimately drives, so it is
   automatically unconfirmed. Note that the operation of the **key panel** in this title also depends on
   a setting in the game's own `shell/Game.ini`; without that setting the board dialogue stops after
   the release step and the panel never gets its turn. That is not a fault of the board.
4. **The second player** and the game's calibration screen have not been exhaustively tested with the
   Move.
5. **The process observation in our own tooling:** our first observation script matched only the
   **loader**, because the loader has the path of `Shell.exe` in its own command line. The second,
   broader pattern does pick up the shell and the game (section 9).

## 9. What we tried and what did not work

We did two runs. They differ, and the difference is instructive for anyone who wants to automate this
game.

**Run 1 did not do something.** We used the project's simple start-test helper, in the belief that it
did the same as our normal play route. It only calls the launcher, however, and **never** starts the I/O
bridge. Result: the input route was never armed and there was **no virtual board at all — the gun
included**. The game started anyway, and that is exactly why this is so easy to miss: the engine said
so itself (`IO bridge: none (… route evdev)`) and the game simply came up in its attract loop. Only
someone who looks at the devices sees that there is nothing. **That was a fault in our own playbook**,
not a property of the game, and the playbook was corrected afterwards. The lesson: **a game that starts
does not mean that the input route exists** — check that path separately.

**Run 2 produced the new data.** It ran through the normal play route, with the input route armed and
**two** boards, and delivered exactly the things run 1 could not show:

- the engine logs both boards (`SaeGunBoard via uinput, SaeIoBoard via uhid`);
- the `uhid` device provably exists in the kernel, with the intended identity
  (`0003:00001209:000072F1`, `RavenGun SAE IO Board`);
- the joystick-id check was done (the gun on 0, and the I/O board does not show up as a joystick);
- a **broadened observation pattern** recorded all three processes at once at 11:26:32 — loader,
  `Shell.exe` and `Transformers2.exe` — while the first pattern saw only the loader;
- and the outcome that the board is created but not found (section 5b).

More time in run 1 would have added nothing; the second run needed a **different setup**, not more
time. That is the most useful lesson of this title: **if a measurement shows "nothing special", first
check whether your instrument is measuring what you think it is.**

## 10. What is Linux-specific, and what differs on Windows

Everything runs on **Linux** (SteamOS on a Steam Deck) under **Wine**. That is not a detail: this
cabinet's two input devices come out through two different kernel couplings on Linux, and the per-game
settings have their own layer there. Per point whether it is **MEASURED** or an **assumption**.

**10.1 The gun: `uinput` (MEASURED).** On Linux we make a virtual joystick through `/dev/uinput`
(evdev). Wine turns that into a HID device itself (`HID\VID_1209&PID_72F0`, through its own bus layer)
and DirectInput sees an ordinary joystick. There is **no** special permission needed for `/dev/uinput`
and no udev rule: the device gets the `ID_INPUT_JOYSTICK` property and the session gets access to it
automatically. **Windows difference:** there is no `uinput`; you would have to install a virtual
joystick driver (a ViGEm-style one) and it would still depend on whether that appears as a DirectInput
device. The joystick-id story of section 5a is different on Windows too: Windows does not know
"joystick id 0" the way Wine keeps it in the registry, but numbers the devices in the order it finds
them — the same pitfall, in a different place.

**10.2 The I/O board: `uhid` plus a udev rule (MEASURED).** The cabinet I/O board has to be a **real**
HID device, because the shell looks for it through `SetupDi`/`HidD` (section 1). On Linux we do that
with `/dev/uhid`, where we write the report description and the reports themselves. Two Linux
peculiarities:

- `/dev/uhid` is `crw------- root root` by default, so **root only**. Without a measure the board
  fails — and because a board that does not open stops the whole route, it takes **the gun with it**.
  The measure is a udev rule (`packaging/udev/71-ravengun-uhid.rules`, content:
  `KERNEL=="uhid", SUBSYSTEM=="misc", TAG+="uaccess"`), installed once with
  `sudo scripts/udev-ravengun.sh --apply`. On this machine that is done and `/dev/uhid` is writable
  (re-measured 2026-10-13).
- The device appears as a `hidraw` device (`/sys/class/hidraw/hidraw4/...`) and **not** in
  `/sys/class/input` — that is exactly the difference from the gun, which is an evdev device. Whoever
  looks for the board with `ls /sys/class/input` does not find it and wrongly concludes that it does
  not exist. **MEASURED** (that happened to us).

**10.3 What Wine then does not do (MEASURED).** This is the Linux-specific bottleneck of this title:
the `uhid` device exists in the kernel, but Wine does not take it up. The prefix's DirectInput registry
holds **no** `VID_1209&PID_72F1`, and the shell does not find it either. Our indication is that
`HID_PHYS=` is empty while every real USB HID device has a bus path — in other words: Wine appears to
ignore devices without a physical bus. If you want to continue here: that is the first question, not
the protocol (section 5b).

**10.4 The user layer (MEASURED).** On Linux our launcher knows two setting layers: the shipped
`patches/<Game>/game.ini` and the user file `~/.local/share/ravengun/patches/<Game>/game.ini`, which
goes over the first **per key**. That is how you add the I/O board without touching the shipped profile,
and removing one file puts it back. That is a Linux-shaped design of ours; on Windows you would work
with copies of profile files, or with the front-end's own user profiles.

**10.5 The paths and the drive letter (MEASURED).** The game files are on a share and the launcher
starts them **from there** — that is the design, not a detour. In the Wine prefix that shows up as a
`Z:` drive with the full Linux path in it (`Launched
z:\<the share path>\...\Transformers2\Transformers2.exe`), and
`drive_c/Sega` is a **symbolic link** to the folder on the share so that the shell finds its fixed
`C:\Sega\...` paths again. **Windows difference:** there you would assign a drive letter (`net use` or
`subst`) or put the files locally; the symbolic-link structure is the Linux variant of the same idea.

**10.6 The prefix and the DirectX libraries (MEASURED, with an assumption).** We use one Wine prefix per
title, with **native** `d3dx9_43`, `d3dcompiler_43` and `d3dcompiler_47` in it (section 2). Without
those three the game stops with the `E5017` shader error. **assumption:** that is a property of Wine's
HLSL effect compiler and not of the game — on Windows with the Microsoft DirectX runtime this problem
does not occur, because those libraries there are exactly the versions the game expects.

**10.7 The launcher identity (MEASURED).** `launch-direct.py --plan` prints which launcher is really
running, with path, SHA-256, git commit and whether the installed copy is a different version. On Linux
that is extra important because the same code can stand in two places (the repository and the
installation) and an old copy costs you measurements. On Windows the same problem would exist with an
old installation folder; the measure is the same: let the tool say itself which version it is.

---

*Everything above was measured on one machine: a Steam Deck running SteamOS with plain Wine, with the
game files on network storage that is only read. Where a claim is labelled MEASURED there is a number,
timestamp, path, index or offset with it; where it is labelled INFERRED or assumption it is a
conclusion or a guess.*
