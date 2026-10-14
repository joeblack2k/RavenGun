# Jurassic Park Arcade — playable from the RavenGun app (2026-10-14, afternoon)

**Labels:** MEASURED = read from a log, a file, a frame that was looked at, or the game's own code; NOT MEASURED = said
plainly where it applies. **Route:** native Linux, our own loader (`rg-elfloader`), OpenGL on Vulkan (Mesa zink on RADV),
started by a **double-click on the title in the RavenGun app** (installed copy, `~/.local/opt/ravengun`).

## 1. What is shown

| | result | evidence |
|---|---|---|
| Double-click in the app starts the game | MEASURED: app → daemon `game_start` → `play-halo.sh` → `play-native.sh` (installed copy, see the identity block at the top of each run log); row turns "Running" | `01-app-doubleclick-running.jpg` |
| 5 minutes of attract, no crash, no serious graphics fault | MEASURED: run of 12:20:14 started by double-click, attract 12:20:47 → 12:28:08 (7 min 21 s), 28 captures all distinct, process alive at the end | `sheet-app-attract-1220.jpg` |
| OpenGL on Vulkan | MEASURED: `/proc/<pid>/maps` of the game process holds `libvulkan_radeon.so` and `libgallium` (zink); the game itself only has OpenGL (`libGL.so.1`, no Vulkan strings in `game`) | `logs/laadbewijs.txt` |
| **"RIO Disconnected" is gone because the lightgun board answers** | MEASURED: the game opens the board (`pmrt_usb_open(0c70,0780)` → our handle), its read thread takes 250 reports/s, its own messages to the board decrypt to structured data (`01 23 c6 …`), and the game log has no "RIO not connected" / "RIO Disconnected" in these runs | `02-attract-no-rio-banner.jpg`, `logs/bridge-excerpt.txt` |
| START | MEASURED: daemon key 1 → board-image bit 4 → id 7 → game log "Player 1 joined. Game Started" in the same second | `03-choose-your-mission.jpg` |
| Aim and trigger | MEASURED: aim (0.33, 0.27) → lightgun channels 19/20 = 1338/1093; trigger (bit 5 → id 15) fires: laser to the crosshair, score 0 → 50 → 1,025, "STREAK X11", game log "Player 1 killed attacker" (22 kills in the final app-started round) | `04-firing-score-50.jpg`, `sheet-play.jpg` |
| **Final run, started by double-click, with the board answering** | MEASURED: double-click 13:00:06, attract 13:00:36, P1 joined 13:01:09, mission 13:01:39, 22 kills, died 13:02:43, back to attract, process alive at 13:06:57 (6 min 21 s); 24 captures all distinct, no "RIO Disconnected" on any; bridge counters: START id 7 = 2, trigger id 15 = 180, 52,315 reports; error log untouched during the run | `sheet-final-app-run.jpg`, `logs/bridge-excerpt.txt`, `logs/gamelog-app-run-1300.txt`, `logs/errorlog-tail.txt` |
| **Played with a mouse only** | MEASURED: double-click 13:13:43; player 1's lightgun = the Steam Controller's mouse (`/dev/input/event7`, the daemon's mouse-gun). Every input was injected into that real device node (it is writable for the user; the kernel hands injected events to every reader, so the daemon reads them exactly as hand movement). Mouse moves → daemon aim (0.500 → 0.292 → 0.708 …) → lightgun channels; left button → p1.trigger → id 15. The trigger alone joined the game in free play (13:14:24) and chose the mission; 15 kills, two deaths, two continues, all by the trigger; START id 7 never pressed. 22 captures, crosshair at a different place in each | `sheet-mouse-run.jpg`, `logs/mouse-run-1313.txt`, `tools/` |
| **Played with a PS Move (by the owner)** | MEASURED: first try, the owner reported up/down swapped → `rio-invoer.ini` channels 20/22 inverted (board Y 0 = bottom, as on NERF), game restarted by double-click 13:49:38; then START (id 7 = 2), 48 trigger presses (id 15 = 96), **54 kills** 13:50-13:53, streaks up to x46, power-ups; crosshair all over the screen | `sheet-move-run.jpg`, `logs/move-run-1349.txt` |
| Game options in the app | MEASURED: Properties → Options shows the game's own settings with their real values (free play on, difficulty 3, volumes 80/50, …); Apply wrote `gameVolume.{0,1,2}.txt` in the local write layer with a valid SHA-1 and `val = 79`; the game booted with it and left it as it was; set back to 80 the same way afterwards; the NAS copy is unchanged. Whether the game *sounds* different was not measured | `05-app-options.jpg` |

## 2. The lightgun board — how it answers

Jurassic Park takes its lightgun board (Raw Thrills RIO, USB `0c70:0780`) as **events** from the library's USB read thread
(measured earlier today, `../bewijs-bord-20261014/`). The NERF way (replace polled functions) cannot reach that. So the RIO
bridge got a second attachment: it **answers as the board itself**:

* the four `pmrt_usb_*` functions of the game image get a jump to the bridge (open / interrupt_read / interrupt_write / close);
  every other USB device (card readers, the UMC board) goes through a trampoline to the real code;
* `pmrt_usb_open(0x0c70, 0x0780)` returns our own handle;
* every `interrupt_read` gets, on the board's rhythm (4 ms), a type-2 input report built from the daemon's board image: 4-bit
  switch counters (odd = pressed) and 12-bit lightgun channels at the bytes the game's own decoder reads, the checksum in
  byte 63, **encrypted with the game's own `xrtea`** (the bridge checks once, at the first open, that encrypt → decrypt
  gives the original back);
* what the game sends to the board is accepted (the first messages are logged decrypted).

All per-game facts (addresses, signatures, report layout, which board-image bit is which switch id, channel ranges) are in
`../rio-invoer.ini`; the code is `tools/rio-bridge/rg_rio_bridge.c` in the loader repository (32-bit build
`lib32/librg_rio_bridge.so`). **Same device as NERF, same board model, one bridge**; only the attachment differs.

Mapping (board-image layout `RawThrillsGUN`, the game's own): test → 3, coin1 → 0, coin2 → 1, service → 4 (not measured as
service), **p1.start → 7, p1.trigger → 15 (measured here)**, p1.grenade → 13, p2.start → 8, p2.grenade → 12, volume ± → 5/6.
Channels 19/20 = P1 X/Y, 21/22 = P2 X/Y, 0..4095 (the game's default calibration is 0..4096).

**NOT MEASURED:** P2's trigger id (16 or 17 are candidates; not mapped, so a second gun does not fire yet), the reload/grenade
buttons in play, coin with free play off, and P2's Y direction (inverted like P1, not tried). P1's Y was inverted after the owner played with a Move.

## 3. The shader errors (fixed)

Every boot logged `Failed to load shader: shader108 / 108d / 108c_sk / 108d_sk (ec=-9596)`. MEASURED, with Mesa itself on the
exact sources the game passed to it (`MESA_SHADER_DUMP_PATH`, then compiled with an EGL context):
`fragment shader writes to both 'gl_FragData' and 'out_Bloom'`. The game prepends `include/frag_bloom.gls`, which declares
`out vec4 out_Bloom` when bloom is on, to glow shaders that write the old `gl_FragData[]`; GLSL forbids the mix, NVIDIA (the
cabinet) allowed it. Fix, data only: `../data/shaders/` (made by `../maak-shaders.py` from the originals) writes
`out_Color` / `out_Bloom` (the game binds them to outputs 0 / 1, the same buffers) and lists `frag_bloom.gls` first in
`shader108.shader`. Bound over the originals in the sandbox; the NAS is untouched. MEASURED: all four compile with Mesa; in the
game no shader failure since; the glow on hit targets is visible (`sheet-play.jpg`).

## 4. Other messages, looked at and left alone

* `xicoOpenPort(): Error`, `msr100OpenPort(): Error`: the magnetic card reader is absent; the game carries on (by design).
* `ifconfig` / `nvidia-settings: command not found`, and `wrong ELF class` for the 32-bit preloads in 64-bit helpers: the game
  calls shell tools that the sandbox lacks; harmless.
* `Compressor Tank Purge Valve Open/Closed`: the motion-cabinet compressor routine; this cabinet type is upright.

## 5. Not shown / open

* **A hand on a mouse.** The mouse path was driven by injecting events into the Steam Controller's real mouse device, not by a hand. A separate synthetic mouse (uinput) cannot be read by the daemon (`/dev/input/eventN` is root-only without a `uaccess` rule); whether an ordinary USB mouse is readable depends on the same rule — the Steam Controller's mouse is. Mouse buttons: left = trigger (also joins and continues), right = reload, middle = grenade; START/coin come from key 1/5 (a Move button, or the daemon).
* **The PS Move**: played by the owner (see the table); whether up/down now feels right was the owner's call after the fix.
* The ten settings in the app are the game's own `adj` values; region (country) lives in the dongle record (byte `0x1c`) and is
  not in the app yet.
* **Sound — fixed, and it was not the game.** The owner heard nothing. MEASURED: the game has its own stream (`rg-elfloader`, PulseAudio on PipeWire, 100 %, not muted) and it carries sound (3 s recorded from the stream itself: RMS ~560, peak 1654). The system's default output, HDMI 7.1 to the TV, had **front-left and front-right at 0 %** (the other six channels at 100 %), so every stereo stream was silent — the virtual-surround plugin's own output too. Set to 100 % (`pactl set-sink-volume <hdmi sink> 100% ×8`; the previous state is saved in the session scratchpad); the owner then heard the game on the Sonos. What set them to 0 % was not found (the surround plugin does not set volumes). The game's second sound system (JPS, OSS `/dev/dsp`) drives the guns' rumble only and is not started while 'Gun rumble' is off.

## 6. Files

`logs/` — bridge excerpt, game-log excerpts, load proof. `OPNAMES.txt` — where the full capture sets are (outside git).

## 7. Working (2026-10-14, 14:35-14:45)

The RavenGun engine now starts native games itself (`src/ravengun/launch/native.*`, no `play-halo.sh`/`play-native.sh`): MEASURED, double-click 14:39:04, process chain `gamescope -> bwrap -> rg-elfloader` (`rg-exec --keep DISPLAY` in between, so the game gets only DISPLAY from gamescope: a first try without it loaded gamescope's WSI layer into zink and showed "Gamescope WSI Layer Error"), attract 14:39:35, board answered, played with the mouse (score 3,225). `game.ini` [Status] Status = working: the app shows a green "Working" (`06-app-working-green.jpg`). NERF Arcade the same day through the same starter: START, mouse play, score 12,050 (`../../NERF/working-c++-starter-20261014.jpg`).
