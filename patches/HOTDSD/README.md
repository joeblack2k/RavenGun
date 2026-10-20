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

Measured headless, without the TeknoParrot folder: amdaemon.exe starts (`Ver.2437`, `setup Game Mode` then
`Advertise`) and stays up, the game process starts with our core and both drop-ins, initialises Direct3D 11
through DXVK and polls the emulated ALLS board continuously (more than 280,000 input reports read in one
run).

**Not working yet: the picture.** In that run no window appears in gamescope, so attract mode has not been
seen and coin, Start, trigger, reload, grenade and aim are unmeasured. `[Status] Status = untested` for that
reason; the `Note`/`NoteEN` keys in `game.ini` say the same.

## Verify (without TeknoParrot)

```sh
ravengunctl game status        # nothing may be running that you did not start
ravengunctl game start HOTDSD
```

With the TV free this runs full screen through gamescope. While the TV is in use, the measurement recipe
is: `[Display] GamescopeBackend = headless` plus `[Env] VK_LOADER_LAYERS_DISABLE =
VK_LAYER_FROG_gamescope_wsi_x86_64` (the gamescope WSI layer cannot get its Wayland objects from a
headless gamescope, and DXVK then fails to create its swap chain), screenshots with
`GAMESCOPE_WAYLAND_DISPLAY=gamescope-N gamescopectl screenshot <file>`. Remove both keys afterwards.
