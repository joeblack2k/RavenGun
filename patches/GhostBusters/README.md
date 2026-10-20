# Ghostbusters (Raw Thrills / Play Mechanix, 32-bit Linux ELF, board layout RawThrillsGUN)

Runs natively on RavenGun's own loader (`rg-elfloader`, route described in `native.json`), without TeknoParrot or Wine. Status:
`untested` (see `NoteEN` in `game.ini`): everything below was measured on the headless bench with synthetic aim, the owner has not
played it with the PS Move yet.

## Files

| File | What it does |
|---|---|
| `native.json` | The sandbox: overlay on the NAS `pm` folder (the game opens absolute `/pm/...` paths; what it writes lands in `~/.local/share/ravengun/native/GhostBusters/0/upper`), HASP hook, RIO bridge, seeds. |
| `rio-invoer.ini` | Table for `librg_rio_bridge.so`: the bridge answers the game's `pmrt_usb_*` calls as the RIO lightgun board (USB 0c70:0780) from the daemon's board image. Switch ids and aim channels are measured, see its comments. |
| `game.ini` | `[Input]` (route `tp-shm`, layout `RawThrillsGUN`), `[Controls]`, `[Status]`. |
| `options.json` | Options shown in the app (free play, volume, difficulty, ...). Free play defaults to ON. |
| `data/adj/playerCost/freeplay.{0,1,2}.txt` | The game's own "free play = 1" setting, seeded once (see below). |
| `data/aud/mountedGun{0,1}Calibration.{0,1,2}.xml` | The two mounted-gun calibrations, seeded once (see below). |
| `dropin/`, `private/` | HASP emulation: the dongle answers that decrypt the game data. The answers are kept in `private/`. |

## Why it boots straight to attract

On a fresh write layer the game stops at "LEFT GUN: Aim left gun so balls hit target ... Press the DIAG button to skip this gun",
twice (left and right gun), and with the default settings then waits for money ("$3.50 left"). Both are game settings stored in the
write layer, so `native.json` has `seed` entries that copy ready-made files there ONCE, only if the file does not exist yet (the
engine logs `seed: ... gezet uit ...`):

* `pmuser/aud/save/mountedGun{0,1}Calibration.{0,1,2}.xml`: format `[u32 20][SHA-1][u32 length][UTF-16LE XML incl. BOM and NUL]`, with
  `x_min/x_max/y_min/y_max` = the raw readings at the two calibration targets (top-left, bottom-right) and `x_orient=1`, `y_orient=1`.
  They were produced by running the game's own calibration (Utilities > "Gun Cal with Balls", or the boot screens) with the engine's
  synthetic aim at (0.1,0.1) for the first shot and (0.9,0.9) for the second, which gives `x_min=y_min=3703`, `x_max=y_max=391`
  (0.1 and 0.9 of our 12-bit range, inverted). The game places its two targets at 0.1/0.9 of the screen even though the red target
  icon is drawn at about 0.083,0.152 / 0.917,0.848: aiming at the drawn icon (the first version of these files) put the reticle up to
  6% of the screen off at the edges, aiming at 0.1/0.9 puts it within 7 px everywhere. The calibration is the same for every user and
  screen because the engine already delivers calibrated aim.
* `pmuser/adj/save/playerCost/freeplay.{0,1,2}.txt`: `val = 1`, same `[u32][SHA-1][u32][text]` wrapper as the other RTDT adjustment
  files (the builder reproduces the game-written `useBallMechs.0.txt` byte for byte).

Because they are seeded only when absent, a calibration done later in the game's own menu, or free play switched off in the app,
stays. To force a recalibration delete `mountedGun*Calibration.*` from the upper directory (or use Utilities > Gun Cal with Balls).

## Switch ids (measured 2026-10-20)

From the game's own TEST > Tests > Input Tests > Switch Tests screen, driving one board bit at a time through
`RG_RIO_PROBE` (the live probe file of `tools/rio-bridge/rg_rio_bridge.c`):

| RIO id | Game label |
|---|---|
| 3 | Test (also "DIAG" on the calibration screens, "Select" in the operator menu) |
| 4 | Service ("Back" in the operator menu, "recalibrate" on the calibration screens) |
| 5 / 6 | Vol + / Vol - (menu scroll up / down) |
| 0 / 1 / 18 / 2 | Coin Slot 1 / 2 / 3 / 4 |
| 12 and 13 | Gun 1 Trigger |
| 14 | Gun 1 Ball Loaded Switch |
| 15 and 17 | Gun 2 Trigger |
| 16 | Gun 2 Ball Loaded Switch |

There is no Start, grenade or reload switch. The attract screen says "Press ANY BUTTON to Begin": the trigger starts a game (measured),
and id 7 (the Start id of Jurassic Park/NERF) does nothing here. So Start (Square) is mapped to the same id as the trigger, grenade
(Cross) and reload are not mapped. The "ball loaded" switches belong to the real cabinet's ball shooter and were not needed.

## Aim (measured 2026-10-20)

X is RIO channel 20 (gun 1) / 22 (gun 2), Y is channel 19 / 21. Both axes are inverted on the channels (top and left = 4095):

* The calibration screen prints the raw values it reads (`GUN1 X/Y`); `ravengunctl test-pattern corners` gave X 310..3785 and Y 3491..604.
* With X not inverted the game stored `x_orient=0` and the calibration reticle and the proton beam were mirrored in X. With X inverted
  the game stores `x_orient=1` and the beam follows the aim (top-left aim: beam to the top-left, top-right aim: beam to the top-right).

## Reticle check (measured 2026-10-20)

On the calibration verification screen the game draws a reticle at the aimed position. With the seeded values and `ravengunctl
test-pattern` held at a point (commanded vs reticle centre found in the 1920x1080 screenshot): (0.5,0.5), (0.1,0.5), (0.9,0.5),
(0.5,0.35), (0.5,0.7), (0.3,0.7), (0.7,0.4), (0.1,0.1), (0.9,0.1), (0.1,0.9), (0.9,0.9) all within 1 to 7 px (at most 0.4% of the
screen) for gun 1, and 1 to 7 px for gun 2 (9 points). The extreme edges (0.0 and 1.0) were not tested.

## Test notes

* Bench run, 3 clean starts of `ravengunctl game start GhostBusters` straight to attract (FREEPLAY shown), a game started by Start,
  the trigger fired the beam and scored (330 points in one run), then 6 minutes without a crash.
* Rumble: the bridge logs every changed board write (`usb: de game stuurt ...`). The game writes lamp ramps (report type 2) and a few
  setup/event reports (types 1 and 9) but NOTHING changed while the trigger was held in a mission, so the game does not drive gun
  feedback through the board. The engine has no game-output-to-rumble path for RIO routes (only the Unity route feeds `game_recoil`),
  so the Move rumble stays trigger-driven (global rumble setting).
* The calibration screens report "Left Gun" with Gun 1 and "Right Gun" with Gun 2; with only a PS Move for player 1, player 2's gun
  idles at its parked position (a red reticle at the lower right is the idle gun 2).
