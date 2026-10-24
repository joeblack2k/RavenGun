# Transformers: Shadows Rising

*Sega RingEdge 2 cabinet, "Game Shell" build, 2018. Windows x86, two players. Route: Wine + OpenParrot loader + core, with a virtual gun board. Status: **playable with a PlayStation Move as the gun; the five-minute bar is met.** Not finished: the cabinet's I/O board (lamps, button panel, hardware recoil).*

Everything on this page was measured on a Steam Deck (SteamOS) under plain Wine, with the game files on network storage that is only read. Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

The game is not one program but a small Sega environment (the **Game Shell**) that starts the actual game and then keeps watching it. Both are 32-bit Windows programs (**MEASURED** from the files):

| File | Size | Role |
|---|---|---|
| `shell/Shell.exe` | 1,438,720 bytes | The Game Shell (version 180605): reads the installation, starts the game, tracks its state |
| `Transformers2/Transformers2.exe` | 9,332,736 bytes | The game (window title `TF2_Gun \| Release`) |
| `Launcher/SystemLauncher.exe` | 514,048 bytes | Sega's own start program; reads `Launcher/Launcher.ini` (`ShellDirectory=c:\sega\shell`) |

The shell expects a fixed installation folder: its `Install.ini` says `Directory=C:\Sega\Shell`, the game's says `Directory=C:\Sega\Transformers2`, and `shell/Game.ini` sets `AssetDir=C:\sega\shelldata` (**READ**).

**Two processes, one start** (**MEASURED**). The loader starts **one** process, `Shell.exe`. The shell starts `Transformers2.exe` itself and stays alive beside it. This is a chain, not "two executables to start".

**The cabinet has two input devices that have nothing to do with each other.** For an emulator this is the most important thing to know.

1. **The gun is a DirectInput joystick.** `Transformers2.exe` opens the first joystick of the system (no filter on vendor, product or name), sets every axis to the range −1000..1000 and reads an 80-byte joystick state. Horizontal position comes from axis `lY`, vertical from `lX` (player 2: `lRx` horizontal, `lZ` vertical). The game converts with `(1000 − value) × 255 / 1000`, so left and top are the low end. **MEASURED** from the code and confirmed on the TV.
2. **The cabinet I/O board is a HID device.** `Shell.exe` uses the HID and device-setup APIs to find it. This board drives the lamps, the external button panel and the hardware recoil. **MEASURED** from the import tables.

**How the shell recognises the I/O board** (**MEASURED**): not by vendor/product ID and not by name. It walks a built-in table of (HID usage page, HID usage) pairs and writes its reports to the device that shows up as **Generic Desktop (0x0001) / Game Pad (0x0005)**. It then writes **3 bytes** of the form `00 <a> <b>` (report ID 0, two data bytes); the length it writes is the device's reported output-report length. Whoever wants to imitate this board must get the **usage** right, not a name or an ID.

**DERIVED**: `Transformers2.exe` also contains code for LED tracking and a camera model, which points to an infrared camera gun whose raw LED positions go to the PC. Our setup does not use that path — we feed the DirectInput joystick — but someone imitating a real cabinet should know the code is there.

## 2. What the game needs

**The files** (**MEASURED**): one copy of the game is 1,748 files, with `GameData`, `Launcher`, `ShellData`, `Transformers2`, `shell` and `temp` at the top. The only two empty files (`Transformers2/TF2_Gun_R.ilk` and `shell/install.crc`) are not a defect.

**The start sequence** (**MEASURED** from the shell's logs):

1. The shell reads its `Install.ini` and `Game.ini`, checks that the folder `GameData` exists (`Validated GameData directory exists`) and sets its window to 1920×1080.
2. The shell logs `Closing down shell ready for game launch` and starts `Transformers2.exe`.
3. The game loads, shows a black loading screen, then enters its attract loop.
4. The shell stays alive and translates the game's messages (`GAME_MSG_*`: attract, stage, score, health, `GAME_MSG_TRIGGER_RECOIL`) to the cabinet: lamps, panel, recoil. Both binaries contain the same message names, so this is one protocol between two Sega processes.

**What the game itself needs** (**MEASURED**):

- **The shell is mandatory.** Starting `Transformers2.exe` alone leaves it on `SYSTEM INITIALIZING... LOADING..` for more than 50 seconds.
- **Under Wine, native `d3dx9_43`, `d3dcompiler_43` and `d3dcompiler_47`** in the prefix. Without them the game stops with an error window: `E5017: Aborting due to not yet implemented feature: Writing fx_2_0 sampler objects initializers is not implemented`. Wine's own effect compiler cannot compile this shader effect.
- **The 32-bit loader and core**, because the game is 32-bit. The launcher's plan shows it (`is64: False`).
- **The folder layout**: the shell works with `C:\Sega\shell` and `C:\Sega\Transformers2`. In our prefix `drive_c/Sega` is a symbolic link to the game's folder.

## 3. The official start script

**There is none** (**MEASURED**): the game folder contains no `.bat`, `.cmd` or `.sh` file. Sega's official start is a program chain:

```
Launcher/SystemLauncher.exe   (reads Launcher/Launcher.ini: ShellDirectory=c:\sega\shell)
        -> shell/Shell.exe    (Game Shell 180605)
             -> Transformers2/Transformers2.exe
```

The chain can be reconstructed completely from `Launcher/Launcher.ini` and the two `Install.ini` files. We start at `Shell.exe`. For the general rule about start scripts see [Official start scripts](Official-Start-Scripts).

## 4. The route we use

The launcher (`scripts/launch-direct.py`) reads the profile and starts `Shell.exe` through the loader, with the core. The profile says:

| Field | Value | Meaning |
|---|---|---|
| Emulator type | `TeknoParrot` | Loader plus core |
| `ExecutableName` | `shell.exe;Transformers2.exe` | A **list of names**, used to recognise the game's processes |
| Game path | `shell/Shell.exe` | What is actually started |

**A pitfall** (**MEASURED**): the two names in `ExecutableName` are **not** the launcher's "two programs" mechanism. That mechanism is for a game with a separate service beside it (see [House of the Dead: Scarlet Dawn](House-of-the-Dead-Scarlet-Dawn)). Here the launcher starts one process and the shell starts the game.

**The input bridge.** The engine creates the devices the game expects. They are described as data (`packaging/hid-boards.json`), not as code per game:

| Board | Transport | What it is |
|---|---|---|
| `SaeGunBoard` | `uinput` (an event device) | The gun: a virtual joystick that DirectInput sees. Axes `X = p1.y`, `Y = p1.x`, `Z = p2.y`, `RX = p2.x`, with an output range of −217..557 inside −1000..1000 (the part of the range the game treats as the whole screen) |
| `SaeIoBoard` | `uhid` | The cabinet I/O board: a real HID device, Generic Desktop / Game Pad, a fixed 15-byte input report and a 3-byte output report |

Both can be active at once, with a list in the game's settings (`Boards = SaeGunBoard, SaeIoBoard`). The I/O board is added in the **user layer** (`~/.local/share/ravengun/patches/<Game>/game.ini`, which overrides the shipped settings per key), so that removing one file puts the title back as it was.

**The commands that work:**

```sh
# 1. what will be started? (executable, environment, prefix, which core, which launcher file)
python3 scripts/launch-direct.py TransformersShadowsRising --plan

# 2. the run itself
bash scripts/play-halo.sh --profile TransformersShadowsRising --no-wait-move
```

Two lessons from this, each of which can cost a measurement:

- **The play script arms the input route; a bare start does not.** A helper that only calls `launch-direct.py ... --run` never tells the engine to start its input bridge. Then **no virtual board exists at all**, not even the gun. The game still starts, which is why this is easy to miss; the engine says it (`IO bridge: none`).
- **Use the launcher from the source checkout, not an old installed copy.** The plan names the launcher file with path and SHA-256. Run 2 below used launcher SHA-256 `327ca20f...21cc0` and engine SHA-256 `d6c5b596...871e` (**MEASURED**).

## 5. Input

### 5a. The gun — works (**MEASURED**)

- The game reads the **first joystick of the system**. Your device must therefore get joystick ID 0; otherwise the game takes another device (in our case at first the PlayStation Move itself, which Wine also sees as a joystick) and the crosshair sits in a corner.
- Our board is called `RavenGun SAE Gun Board`, vendor/product `0x1209/0x72F0`. Wine turns the event device into a HID device by itself.
- Wine remembers joystick IDs per product in the registry, under `HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput\VID_xxxx&PID_xxxx\Calibration\N`, value `Joystick Id`. A helper script (`scripts/wine-joyid.sh`) sets it.
- **Check this before every run**, before looking at the game or the I/O board: `VID_1209&PID_72F0` must be on **0**. If not, you see a crosshair in the corner and the X axis does nothing. That is a joystick-ID problem, not a board problem.
- **The range.** The game sets every axis to −1000..1000 and then applies its own calibration. With the full range the crosshair sticks to the edges; with an output range of −217..557 the crosshair was within **1.5 % of the screen** at every grid point (**MEASURED** on the TV, 2026-10-11).

### 5b. The cabinet I/O board — does not work yet (**MEASURED**)

This is where a next attempt should start.

- **What works**: the engine creates the board correctly. It exists in the kernel:

  ```
  /sys/class/hidraw/hidraw4/device/uevent
  HID_ID=0003:00001209:000072F1
  HID_NAME=RavenGun SAE IO Board
  ```

- **What does not**: `Shell.exe` does not find it. Its own log says:

  ```
  [ 09:33:59 ] HIDioThread : IOBOARD_INITIALISE
  [ 09:33:59 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:00 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:14 ] HIDioShutdown : Called
  ```

  It tries twice and gives up. The shell searches at its own start, before the game begins, so the board must exist before the shell starts.
- **Neither reader sees the board.** Not the shell, and not Wine's DirectInput: the registry has no entry for `VID_1209&PID_72F1`. (The good side: the I/O board cannot take joystick ID 0 away from the gun.)
- **The clue**: in that `uevent` file **`HID_PHYS=` is empty**. Every real USB HID device in the same list has a bus path there (`HID_PHYS=usb-0000:0c:00.0-3/input0`). A `uhid` device has no physical bus. The first question for whoever continues is therefore **whether Wine passes on HID devices without a bus path at all** — before anything needs to happen in the board's protocol.
- **Consequence for the setup**: as long as this does not work, the I/O board is optional and must be able to fail without taking the gun with it.

## 6. Timing

**MEASURED** with the machine's clock. Note: the shell writes its logs in a time zone two hours behind the machine; everything below is local time, and the shell's log line and the process observation agree exactly.

| Moment | Run 1 | Run 2 |
|---|---|---|
| `Shell.exe` seen | **11:22:03** | **11:33:57** |
| "Closing down shell ready for game launch" | 11:22:21 | 11:34:14 |
| `Transformers2.exe` seen | **11:22:26** (23 s after the shell) | **11:34:19** (22 s) |
| Attract begins (`GAME_MSG_ATTRACT_START`) | **11:23:44** (1 min 42 s after the loader) | — |
| Game stopped | about 11:32:45, alive for about **10 min 19 s** | 11:36:44 (measurement ended) |

So from shell to game takes 22 to 23 seconds, and attract begins about 1 minute 42 after the loader. The often-quoted "3.5 to 4 minutes of loading" did not occur in these runs, probably because the file cache was warm (**DERIVED**). Expect several minutes on a cold machine and measure it yourself.

## 7. What we saw on screen

**MEASURED** with screenshots (1920×1080) at fixed times, plus the shell's logs. The file size of a screenshot is a rough measure of how much is on screen: a nearly black picture is 10–130 kB, a real game picture 1.5–3.5 MB.

| Time | What was on the TV | Screenshot size |
|---|---|---|
| 11:22:02 | Nearly black: the shell window was not there yet | 11.9 kB |
| **11:22:32** | **The Sega Game Shell splash**: "Game Shell / Version 180605 / No Updates Found" | 1.6 MB |
| 11:23:03, 11:23:33 | Nearly black: the game's loading screen | 11.5 kB |
| 11:24:04 → 11:32:41 | **The game**: colourful attract pictures alternating with black transitions | 85 kB – 3.5 MB |
| **11:25:04** | Full screen with attract artwork and "PRESS START BUTTON / FREE PLAY" | 3.25 MB |

The order is: shell splash → black loading screen → attract. The picture changes about every 30 seconds between a colourful image and a nearly black one; that is the attract loop alternating logos and videos.

## 8. Linux notes

- **The gun uses `uinput`** (**MEASURED**). A virtual joystick through `/dev/uinput`; Wine makes a HID device of it and DirectInput sees an ordinary joystick. On Windows you would need a virtual joystick driver instead, and the device order decides which joystick is "first".
- **The I/O board uses `uhid`, which needs a `udev` rule** (**MEASURED**). `/dev/uhid` is `crw------- root root` by default. Without write access the board fails, and because a board that does not open stops the whole route, it takes the gun with it. The rule is `KERNEL=="uhid", SUBSYSTEM=="misc", TAG+="uaccess"`.
- **A `uhid` device appears under `/sys/class/hidraw`, not under `/sys/class/input`** (**MEASURED**; it happened to us). Looking in the wrong place leads to the wrong conclusion that the board does not exist.
- **Wine does not pick the `uhid` device up** (**MEASURED**): see section 5b. This is the Linux-specific bottleneck of this title.
- **Paths**: the game files are started from the network share. In the prefix that is a `Z:` path, and `drive_c/Sega` is a symbolic link to the game's folder so that the shell finds its fixed `C:\Sega\...` paths. On Windows you would map a drive or put the files there.
- **One prefix for this title, with the native DirectX libraries from section 2.** That the shader error is a property of Wine's effect compiler and not of the game is an **assumption**.

## 9. Status

**Met** (**MEASURED**, 2026-10-13): the bar is five minutes of attract or game mode. Attract began at **11:23:44** and the game was visible on screen until **11:32:41**, about **8.5 minutes**, while the shell's state machine restarted the loop **five times** (11:23:44, 11:25:31, 11:27:04, 11:29:06, 11:30:33).

**An honest note on that bar.** In this title the text "PRESS START BUTTON / FREE PLAY" is shown **continuously** over the attract. From a picture alone, attract cannot be told apart from a start screen; the **log** shows the real cycle. Judge it by the log, not by one screenshot.

**Playable with the gun** (section 5a). **Not working**: the cabinet I/O board, and with it lamps, button panel and hardware recoil.

### The lesson of the two runs

Run 1 used a bare start helper. The input route was never armed and no virtual board existed. The game started and reached attract anyway. Only someone looking at the devices sees that nothing is there. That was a mistake in our own procedure, not a property of the game.

Run 2 went through the normal play script, with the route armed and two boards, and delivered what run 1 could not: both boards in the engine's log, the `uhid` device demonstrably in the kernel, the joystick-ID check, all three processes observed at once (loader, shell, game), and the finding that the board is created but not found.

More time in run 1 would have added nothing; run 2 needed a **different setup**, not more time. If a measurement shows "nothing special", first check whether your instrument measures what you think it does. A game that starts does not mean the input path exists: check the path separately.

## 10. Open questions

1. **The cabinet I/O board** (section 5b): does Wine pass on HID devices without a bus path? `HID_PHYS=` is the clue.
2. **The board dialogue after that**: which steps the shell takes once it finds the board and which answers it expects. Prepared from the binary, never run for real, because step 1 does not work yet.
3. **Lamps, button panel, hardware recoil**: unconfirmed for the same reason. The button panel also depends on a setting in the game's own `shell/Game.ini`.
4. **The second player** and the game's own calibration screen have not been tested exhaustively with the controller.

---

*Source: the game specialist's digest (2026-10-13). Local paths have been shortened. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
