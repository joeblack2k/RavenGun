# NERF Arcade — played through RavenGun, on our own core (the lightgun board answered in software)

**Date:** 2026-10-14. **Route:** native (`Nerf.x86_64` via `scripts/play-native.sh`; no Wine, no TeknoParrot core, no
BepInEx). **Result:** NERF ran **11 minutes** on our core while being **played through the RavenGun daemon**: start, aim and
trigger from the daemon, rounds played, points scored, a round entered the game's own high-score table. The arcade lightgun
input reaches the game through a new piece, the **RIO bridge** (`tools/rio-bridge/` in the loader repo, table
`patches/NERF/rio-invoer.ini`). The same chain also ran through the **app's own launch path** (`scripts/play-halo.sh`), and there **a mouse** aimed and fired
through RavenGun's mouse gun (point 6).
**Labels:** MEASURED = seen on screen or in a log; INTERPRETATION = mine; NOT MEASURED.

## What is shown

1. **MEASURED — 11 minutes on our core, playing** (run ten2, `logs/ten2.run`, `logs/ten2-drive.log`,
   `06-ten-minute-run-every-30s.jpg`). One process start to finish: pid 2288409, `exe /pm/Nerf.x86_64`, first seen
   09:07:58, last sample 09:18:40, stopped by the run limit at 09:18:59; **0 Wine/TeknoParrot/OpenParrot/BepInEx mappings**
   in the game process; 0 files written to the share. Two rounds were played from the daemon:
   * round A: started 09:08:29 by the daemon's Start, score 20,350 → 90,300 while the TIME counter ran down, ended with the
     game's own "New High Score! Enter Your Name" screen, and the "Nerf Masters" table then lists **"8. DART 94,350"**
     (`08-highscore-table-our-round-94350.jpg`);
   * attract 09:13–09:15;
   * round B: started 09:15:26, score 2,850 → 57,650, still running (TIME 15) when the run limit stopped the game.
   (A Start pressed at 09:11:58 landed mid-round A, which time bonuses had extended, and changed nothing.)
   The game called the bridge ~76,000× `RIO_Connected`, ~983,000× `RIO_SW_Count`, ~302,000× `RIO_ADC_Val`
   (`logs/ten2-bridge.log`); 1,176 trigger presses went through.
   **Honest limit of this run:** for its last ~65 s the daemon published neutral input (centre aim, no buttons). Cause, from
   the daemon's own log: "IO-brug (tp-shm): de game verscheen niet, route stopt" — the daemon's watchdog watches the
   `ravengun-tpui` unit, and this bench run was started outside that unit. The game kept running. In the app's own path
   (point 5) the game runs inside that unit and the IO bridge stays up.

2. **MEASURED — the game shows RavenGun's values on its own screen.** The game's GUN CALIBRATION page reads
   "PLAYER 1 (LEFT GUN)  X: 2056   Y: 2039" — exactly the values the bridge serves for the daemon's centred aim
   (`02-gun-calibration-reads-bridge-values.jpg`); after aiming the daemon at the top-left it reads "X: 0032   Y: 4063"
   (`03-p1-top-left-accepted-0032-4063.jpg`).

3. **MEASURED — the arcade lightguns are calibrated, by the game itself.** Driven entirely from the daemon: test held →
   operator menu (`01-daemon-test-opens-operator-menu.jpg`), Vol− → cursor to CALIBRATION, Test → GUN CALIBRATION, then the
   game's own instruction ("Aim at top left corner of screen and press trigger/start.", then bottom right) for both players,
   ending with "Calibration complete. Press Service/Shoulder to exit." (`04-calibration-complete.jpg`). The game stored
   P1 and P2 Gun Min/Max X/Y = 32 / 4063 itself (`logs/calibration-prefs.txt`), in the native route's local write layer
   (`~/.local/share/ravengun/native/NERF/`), not on the NAS. Player 2 was calibrated from the daemon's Player-1 sources with a
   calibration-only table (`patches/NERF/bewijs-bridge-20261014/tabellen/rio-invoer-kalibratie.ini` (research table via RG_RIO_TABLE; the launcher only ever reads `rio-invoer.ini`)): the daemon builds no Player-2 state without a second
   lightgun.

4. **MEASURED — the trigger is RIO switch id 15, not id 7.** Firing the daemon's trigger through each candidate id at one aim
   point during a round (probe session prb1, `logs/prb1-probe.log`, `05-trigger-probe-score-id15.png`): the score stays 0 for
   ids 0, 1, 2, 4, 13, becomes 4,500 on id 15, stays 4,500 for 16, 17. id 7 taps in earlier rounds never scored. So id 7 is
   Player-1 **Start**, id 15 Player-1 **Trigger**.

5. **MEASURED — the app's own launch path works.** `scripts/play-halo.sh --profile NERF --no-wait-move` (the script the app
   runs) took the native route from the profile, started the daemon's IO bridge itself, and ran the game in `ravengun-tpui`
   with the bridge supplied by the profile alone (`logs/mse1-app-path-bridge.log`: `preload=/rg/lib/librg_rio_bridge.so`,
   table `/rg/tabel/rio-invoer.ini`, both `librio.so` copies patched). Start from the daemon began a round; P1's crosshair sat
   exactly at the daemon's aim (0.5, 0.6 → `09-app-path-crosshair-at-daemon-aim.jpg`); aiming and firing from the daemon
   scored 0 → 1,700 (`10-app-path-score-1700.png`); the IO bridge stayed up for the whole session.

6. **MEASURED — the game sees a mouse through RavenGun** (session mse2, app path again, `logs/mse2-mouse-drive.log`,
   `logs/mse2-app-path-bridge.log`). RavenGun's own mouse gun (`ravengunctl mouse-gun --player 1 --mouse …if02-event-mouse`:
   "verbonden") read a real mouse device — the Steam Controller Puck's mouse interface, the kind of node the daemon is allowed
   to read (`user:deck:rw-` from `70-ravengun.rules`). Its motion and left clicks were injected into that device node
   (`write()` on `/dev/input/event7`: every reader, the daemon included, receives them as the device's own); only the hand was
   synthetic. The lightgun channels followed the mouse — (0.25,0.35) → 1028/2682, (0.75,0.35) → 3067/2682, (0.75,0.55) →
   3067/1863 — P1's crosshair sat on the mouse position on screen ((0.75,0.34) in `11-mouse-at-075-035-crosshair-follows.jpg`,
   (0.25,0.54) in `12-mouse-at-025-055-score-10100.jpg`), and the 30 left clicks fired: score 1,000 → 10,100. The mouse gun
   was released afterwards ("speler 1: geen muis").

## The PS Move leg

NOT MEASURED here, as agreed (the owner tests it). The Move feeds the same daemon output stage as the mouse and the test pattern;
everything from that stage to the game is measured above. A first try with a virtual uinput mouse failed on permissions, not on
the game: the daemon cannot read uinput-created nodes ("event258 NIET verbonden … geen toegang", `logs/mse1-mouse-drive.log`),
which is why the real mouse node was used in point 6.

## How it works (short)

The game loads its own `librio.so` (two copies). The bridge, preloaded into the game, waits for that load and patches four
functions in memory — `RIO_Connected`, `RIO_SW_Count`, `RIO_ADC_Val`, `RIO_ADC_Val_Ex` — only if their first bytes match the
dump's build (the table holds the signatures; any other build is left alone). The patched functions read the daemon's 64-byte
board image (`/dev/shm/ravengun_JvsState`, layout WartranTroopers): buttons become RIO switch **transition counters** (odd =
pressed), aim bytes become **lightgun channel** values 0..4095 (Y inverted). The NAS file is never changed. Tested without the
game: `tests/rio_bridge_test.sh`.

## Open

* Player-2's firing id (16 or 17 are the candidates) — a second lightgun's trigger currently joins Player 2 but does not shoot.
* Service (the calibration screen's exit) has no id yet; Vol+ = id 5 by elimination (Vol− = id 6 measured); coin not mapped
  (free play).
* Sound not judged.
* `play-halo.sh` also passes the Wine-route extras from the legacy `UserProfiles/NERF.env` to the native game (harmless).
* The bench round sits in the game's high-score table ("8. DART 94,350") in the local write layer; RESETS in the operator
  menu clears it if unwanted.

## Where the raw is

Selected frames and logs are here. Full frame sets, run records and the game's full logs (runs brg1, cal2, prb1, ten2, mse1)
are out of git under `~/.local/share/ravengun/bench-captures/NERF/bridge/`.
