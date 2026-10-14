# Metal Gear Arcade — assessment (not started)

> **STAND 2026-10-14 — ASSESSMENT ONLY, nothing was run.** This is a **Konami e-Amusement PC** game, and it
> is unlike every title brought to the bar tonight: there is **no own-core route**. The only loader that
> runs it is TeknoParrot's closed, subscriber-only **TeknoMacaw**, which RavenGun has never run (0 of its 35
> games use it). Reaching "five minutes on our own core" would mean **building an own loader for the whole
> Konami AVS/ACIO environment** — the largest single piece on the list. The decision on whether and when to
> take that on is the owner's; this file is the measured assessment that decision rests on.

Everything here was read from the files on 2026-10-14; **[M]** = measured, **[D]** = derived.

## What it is

* **Konami game code I36**, 2010. The dump (`Metal Gear Arcade/` on the share, 4.5 GB) has **no `.exe`
  anywhere** — only `modules/*.dll` and the game data under `cdrom.img/`. [M]
* It is a **Konami AVS / e-Amusement PC** title. [M] `modules/` holds the AVS framework
  (`libavs-win32.dll`, `libavs-win32-ea3.dll`), the ACIO I/O library (`libacio.dll`), and the game's own
  modules (`bootmode.dll`, `gamemode.dll`, `testmode.dll`, `system.dll` — 19.8 MB, the game). All are
  **32-bit i386**, Windows DLLs.
* **How it boots** [M]: `prop/game-config.xml` names the boot module (`<boot>system.dll</boot>`) and the
  DLLs it registers (`xaudio2_6.dll`, `wmadmod.dll`, `wmvdecod.dll`). `modules/launch.dll` exports
  `dll_entry_init` and `dll_entry_main` — the AVS entry points a loader calls. So the chain is
  **loader → `launch.dll` → AVS (`libavs-win32`) → `system.dll` → bootmode/gamemode/testmode**.
* **Config** in `prop/`: `avs-config.xml` (the AVS virtual filesystem), `eamuse-config.xml` (the
  e-Amusement network: `eamuse.konami.fun`, model `I36`, dest `J`, SSL), `coin.xml`, `game-config.xml`.
  The test menu is `tm-menu/ja/testmode-*.bin` (io_check, sensor_check, mech_check, network_options, …).

## Which launcher / which shell / which core

| question | answer | |
|---|---|---|
| **launcher** | TeknoParrot's **TeknoMacaw.exe** (32-bit console PE) | [M] `GameProfiles/MetalGearArcade.xml`: `EmulatorType = TeknoMacaw`, `ExecutableName = launch.dll`. Our `scripts/launch-direct.py` knows how to start TeknoMacaw, but refuses until the game is added (no UserProfile yet). |
| **shell** | the Konami **AVS** framework itself (not a separate program) | [M] boots `system.dll`; config in `prop/`. |
| **core** | **TeknoMacaw _is_ the core.** It stands in for the whole Konami e-Amusement PC: the ACIO I/O board, the e-Amusement network, the security/dongle, the card reader. | [D] there is no OpenParrot-style core for Konami AVS, and `EmulationProfile = KonamiAcio` is TeknoMacaw's, not a core we build. |
| **own-core route** | **none exists.** | [M] `docs/loskoppelen-per-game.md`: of 35 games, **0 use TeknoMacaw**; RavenGun has never run it. There is no own loader or own core for this class. |

## What it expects of the cabinet

* **The Konami ACIO I/O board `i36g`** [M]: `system.dll` exports/uses `ac_io_i36g_add_coin`,
  `ac_io_i36g_control_coin_blocker_*`, `ac_io_i36g_control_lamp_bright`, `ac_io_i36g_control_motor_power`,
  `ac_io_i36g_get_watchdog_*`, `ac_io_i36g_get_systemid/softwareid`, … — coins, lamps, a motor, a
  watchdog. TeknoMacaw emulates this board.
* **The e-Amusement network** (`eamuse.konami.fun`) [M] — offline here; TeknoMacaw stubs it.
* **A card reader + PIN** [M]: the profile's inputs include "Insert Card", "Enter Saved Pin".
* **The Metal Gear Arcade controller** [M, profile buttons]: trigger, aim, crouch, two item buttons, a
  reload lever, weapon change, sniper mode, **and an analog joystick (X/Y)** — the pod controller.
  `data/system/` has `stick-leftup.bin` / `stick-neutral.bin` / `stick-rightdown.bin` (stick calibration)
  and `input_check` / `screen_check` / `monitor_check` images.
* **Windows Media codecs** [M]: `xaudio2_6.dll`, `wmadmod.dll`, `wmvdecod.dll` are **not in the dump** —
  Wine/Windows must provide them (as Raccoon Rampage and NERF needed GStreamer codecs). WMV movies live in
  `cdrom.img/movie/`.
* **1920×1080, 3D drawn at 1920×540** [M, `game-config.xml`: `draw3d_cx 1920 / draw3d_cy 540`] — the
  autostereoscopic 3D screen the cabinet used.
* **Japanese**, Shift-JIS, region `J`, model `I36`. [M]

## The bar, and the options (for the owner to choose)

"Five minutes on our own core" has **no route today** for this title, because Konami AVS has no own core.
The honest options:

1. **Reference run on the closed route.** Add the game and run it once under TeknoParrot's TeknoMacaw on the
   TV — the way the owner-plays reference runs were done for NERF and Raccoon Rampage. It proves the game
   runs and gives a reference, but it is the **closed** loader and **does not count for the bar**. First it
   must be seen whether TeknoMacaw even runs under this Wine (it is a 32-bit Konami loader; untried here).
2. **Build an own loader for Konami AVS.** The only path to the bar for this title: a TeknoMacaw
   replacement that boots `launch.dll`/`system.dll`, answers the ACIO `i36g` I/O, stubs the e-Amusement
   network and the security, and provides the WM codecs. This is a large reverse-engineering project on the
   scale of the whole AVS/ACIO stack — the biggest single item on the thirteen.
3. **Defer it**, the way Mario Kart DX is deferred, because it is categorically harder than the rest.

Nothing here commits to any of the three; this is the assessment so the choice is made with the facts in
hand.
