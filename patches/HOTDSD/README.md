# House of the Dead: Scarlet Dawn (HOTDSD)

Settings: `game.ini`. Findings and measurement history: `findings/HOTDScarletDawn.md` on the NAS next to
the games, not in this repo (that file is named `HOTDScarletDawn.md`, not `HOTDSD.md`, which is why the
`Findings` key points at that name).

## What this game is

| | |
|---|---|
| Name | House of the Dead: Scarlet Dawn |
| Manufacturer | Sega (SEGA ALLS platform) |
| Year | 2018 |
| Platform | Sega ALLS, Unreal Engine 4.18, 64-bit |
| Genre | Shooter (light gun) |
| Programs | **two**: `amdaemon.exe` first, then `Hodzero-Win64-Shipping.exe` |
| Profile | `GameProfiles/HOTDSD.xml`, revision 21 |

`game.ini` fixes the two exe's the way the profile has them: `Executable = Hodzero-Win64-Shipping.exe`,
`Executable2 = amdaemon.exe`, `HasTwoExecutables = true` and `LaunchSecondExecutableFirst = true`: first
`amdaemon.exe`, a 5 second wait (`DAEMON_WAIT_SECONDS` from `StartApp.bat`), then the game.

## The route this profile ships: no TeknoParrot

`[Launch] Mode = direct`; the engine (`rg-direct`) starts both exe's itself. There is no TeknoParrot core,
no TeknoParrotUi, no start script and no wrapper script in the launch path.

* **Plain Wine** with the shared prefix (`[Wine] Prefix = shared`), exactly like MarioKartDX. The engine
  sets `WINEPREFIX`; the old `[Wine] Runner = wrapper` (`scripts/wine-prefix.sh`) is gone.
* **Our own core**: `[Loader] Core = ravengun` loads RavenGun's OpenParrot64 in both processes.
  `[Loader] RemoteThread = off` keeps `TP_REMOTETHREAD` unset, which is the loader's default
  entry-point trampoline route ("Have fun :)"): the only one that lets the core attach under Wine. The
  remote-thread injection needs TeknoParrotUi and fails under Wine ("Failed to find kernel32.dll in target
  process").
* **DXVK** (`[Launch] Dxvk = true`): UE4 renders through Direct3D 11 and wined3d cannot even create a
  context for it in this prefix (`err:d3d:wined3d_caps_gl_ctx_create Failed to find a suitable pixel
  format`, measured 2026-10-20). The DXVK DLLs land next to both exe's in the view, because the
  `d3d9,d3d11,dxgi = native` override holds for the whole run and our core imports `d3d9.dll` in both
  processes (without them amdaemon.exe stopped with `err:module:import_dll Library d3d9.dll ... not
  found`).
* **Gamescope** (`[Display] Gamescope = true`): full screen on the TV. `[Display] GamescopeBackend`
  (`headless` for a measurement while the TV is in use) is honoured.

## What amdaemon needs from us

* The platform identity in the registry: `HKLM\System\SEGA\SystemProperty\Static` (`PlatformId`, exactly
  four characters). `data/platform.reg` holds synthetic values in the format the daemon validates; no
  dongle data, keys or game files are in this repo.
* The Sega ALLS USB-IO board, which amdaemon looks up as a HID device: `dropin/rg-allsio.dll` (source
  `tools/rg-allsio`, build `tools/dev/build-allsio.sh`) fakes it in the daemon process and feeds it from
  the AllsUsbIo control pipe of our daemon.
* The gun sensor on COM1 inside the game process (`RG_ALLSIO_GUN_SENSOR=1`) and the local Unreal gun mode
  (`RG_ALLSIO_GUN_CONFIG=1`).
* `dropin/config.json`: the AMDaemon configuration (a USB-IO node with coin input, 8 start switches and the
  unique-IO feature the game expects, plus the input table with start, gun select, gun special, test and
  service). It goes into `AMDaemon/` in the view, which is why this game needs a view of the whole game
  folder (root `App/.../WindowsNoEditor`), not just the folder of the exe.

Input: `[Input] Route = tp-shm` with `Layout = HOTDSDPipe` for aim and the triggers, and
`ControlPipe = AllsUsbIo` for START, sub-trigger (grenade), controller select, test and coin through the
64-byte ALLS USB-IO report. Free play is on (`General/FreePlay = 1`).

## Dongle and patches

**No dongle in the sense of a USB key.** `findings/WAT-ELK-SPEL-NODIG-HEEFT.md` (Johan, TeknoParrot team)
is explicit: Scarlet Dawn is in the list of games **without** a HASP file, and what this game needs is
`amdaemon.exe` -- Sega's AM Daemon -- running **before** the game. `docs/loskoppelen-per-game.md` (row 8)
therefore groups this game under the dongle kind `two-exe (amdaemon)`.

There is no `dongle.json` and there is no `[Dongle]` section; the loader no longer knows dongles
(`docs/geen-dongle-in-de-loader.md`). What stands in for the cabinet hardware is the drop-in above.

## State (2026-10-20)

Measured headless, without the TeknoParrot folder (`ravengunctl game start HOTDSD`): amdaemon.exe starts (`Ver.2437`,
`setup Game Mode` then `Advertise`) and stays up, the game loads `PL_InitializeTest` and then `PL_Advertise` and shows its
own attract loop with `PRESS START BUTTON / FREE PLAY` (`docs/screenshots/hotdsd-attract-2026-10-20.png`), more than 5
minutes without a crash.

The earlier "no window" was not this game: the shared Wine prefix had `HKCU\Software\Wine\Drivers` `Graphics=wayland`
(set 2026-10-20 16:30 during an experiment), so Wine drew with winewayland on the desktop compositor instead of on
gamescope's Xwayland. With that value deleted Wine uses its X11 driver, like MarioKartDX in its own prefix.

**Play (measured headless, 2026-10-20).** Free play, no coin needed. START during the attract first jumps to the
title; a second START enters the game. Shooting selects in the menus; the intro skips with START. In the stage the
trigger fires, the reload button refills the ammo (offset 8 bits 2/3 of `HOTDSDPipe`, RavenGun's own bits, which
rg-allsio reports to the game as "gun off screen with the trigger pulled"; the game also restocks by itself when you
aim off screen), and the sub-trigger and controller select both switch to the next item. CONTINUE? takes START, GAME
OVER returns to the attract. rg-allsio.log lists every change of the switch words amdaemon reads ("ingang:
schakelaars ...").

**Gun adjustment.** The adjustment stored in the game's backup did not match our gun sensor (a centred aim showed the
cursor at about 0.54/0.37). It was redone once in the game's TEST menu (CONTROLLER ADJUSTMENT: START selects TOP, LEFT,
CENTER, RIGHT, BOTTOM, the trigger takes the aim on that target; player 1 is now 48/28/514/514/995/975) and lives in
RavenGun's write layer (`writelayer/HOTDSD/.../AMDaemon/TeknoParrot/D/SDET/appfile.dat`). Since then the cursor
follows the aim to the corners and the middle. A fresh write layer no longer needs it: `seed/AMDaemon/TeknoParrot/D/SDET/appfile.dat`
is that backup with player 2 set like player 1 (`[Launch] SeedRoot = App/Hodzero/WindowsNoEditor` makes `seed/` start at the folder
that holds `AMDaemon/`; an existing `appfile.dat` is never overwritten). The adjustment record is in the `TEDS` block at 0x24e00
(mirror at 0x422e00, 1536 bytes, first u32 = CRC32 of the other 1532 bytes): player 1 at +0x90, player 2 at +0xa4, each five
(x, y) pairs of u16 for TOP, LEFT, CENTER, RIGHT, BOTTOM. Measured on a fresh layer: both players' cursors land within 2 px of the
aim at the centre and four corner points. `[Status] Status = untested` until the owner's PS Move test.

## Verify (without TeknoParrot)

```sh
ravengunctl game status        # nothing may be running that you did not start
ravengunctl game start HOTDSD
```

With the TV free this runs full screen through gamescope. While the TV is in use: `[Display] GamescopeBackend =
headless` in the user layer (nothing else is needed), screenshots with `GAMESCOPE_WAYLAND_DISPLAY=gamescope-N gamescopectl
screenshot <file>` into a folder under `$HOME` (the game's systemd unit has a private `/tmp`, so a path there fails with
"Failed to save screenshot"). The start screen waits for player 1's PS Move; without it, `ravengunctl mouse-gun --player 1
--mouse <id>` lets the start through. Remove the user-layer key afterwards.
