# ATTRACT-RUN — Men in Black: the shell chain starts on our own core, the game launches, and where it stops

**Date:** 2026-10-14, 02:31 – 02:56. **Status: NOT at the bar.** The game reaches its MIB logo but exits
after it loads, in a restart loop; it does not hold for five minutes. This file is the honest record of
how far the chain got and where it stops.

**The programs:** `shell/Shell.exe` (PE32 i386, "Game Shell" 230307RED) and `MIB/MIB.exe` (PE32+ x86-64,
Unity 2020.3.14f1) — see `README.md` for what the dump is. **Core:** our own 32-bit OpenParrot build
(`OpenParrotWin32/OpenParrot.dll`, sha256 `413027de…`) starts the shell; the shell starts the game.
**Labels:** **MEASURED** = a log line, a file comparison, a `/proc/<pid>/maps` listing, or a frame that
was looked at; **DERIVED**; **NOT DETERMINED**. **Evidence:** `bewijs-attract-20261014/`
(`harness/`, `runs/`, `shots/`; raw captures out of git, see `OPNAMES.txt`).

Nothing existed about this title in the project before today.

---

## 1. Where it stops, stated plainly

**MEASURED**, run m6: the shell launches the game, the game connects back to the shell over a socket,
loads its assets (37 s), shows the **MIB logo** with **"PLEASE CALL ATTENDANT — 5 TICKETS OWED / 14
TICKETS OWED"** (`shots/m6-logo.jpg`, `m6-tickets-owed.jpg`), and then **exits ~4 s after it finishes
loading**. The shell logs "Unexpect Game termination, retrying" and starts it again. In the 4-minute run
this cycle repeated eight times; each game instance lived 30–52 s (`runs/m6-shell-logs/Mainthread_*.log`,
`GameStateMachine_*.log`). So the title **does not hold five minutes**; it loops through its own logo.

**Why it exits — NOT DETERMINED, two candidates, both MEASURED as present:**

1. **The security challenge is never answered.** The game sends `GAME_MSG_SECURITY_CHALLENGE` to the
   shell twice (10 s apart); the shell logs receiving it and sends no reply
   (`runs/m6-shell-logs/Networking_*.log`). The shell cannot answer it without the DK2 dongle (§3). The
   game finishes loading and quits shortly after.
2. **The serial is not connected.** The game's own log repeats "CONNECTING TO SERIAL" / "BOOOO! NOT
   Connected to serial" (`runs/m6-MIB-Player-een-cyclus.log`). In run m2, started directly without the
   shell and with the owner's Steam Controller Puck still visible as COM33, the game connected to that
   serial and lived a little longer before quitting; with the puck hidden (m3–m6) it never connects.

The Unity log ends each cycle with "Unloading 19 Unused Serialized files" and no exception stack, so
the game quits by its own decision, not on a crash. **NOT DETERMINED** which of the two it waits for; a
dynamic measurement would settle it — a shell that answers the security challenge, or a serial endpoint
that answers, and seeing which one stops the exit.

**Also not shown:** no input was tried; the I/O board is absent (§2); the DK2 dongle is absent; sound was
not judged; no independent capture.

---

## 2. The chain, run by run — what each change bought

The cabinet runs `SystemLauncher.exe` → `Shell.exe` → `MIB.exe`. On our core the shell has four gates
before it will launch the game; a static read of `Shell.exe` (addresses in `runs/`, confirmed by each
run's logs) named them, and the runs went through them one at a time.

| run | the one change | how far the shell got (its own log) |
|---|---|---|
| m1 | the shell as it is, no argument | parks at **gate A**: no log past "Validated GameData directory exists"; MIB.exe never starts |
| m3 | argument `/k=0d051181` (the checksum of its own file, which the cabinet's SystemLauncher passes) | still parks after "Validated…" — gate A passed but **gate B** (I/O board) stops it |
| m4 | `[Debug] SecurityDisabled=1` in the view's Game.ini | same — gate B is before the security gate |
| m5 | the view's Shell.exe is a local copy with a **nine-byte I/O-board change**, argument `/k=e6222935` | past gates A+B: logs "Failed to find valid cabinet SKU" and **"Updates : Scanning"** (gate C passed) — then stops at **gate D** |
| **m6** | `[Debug] NoVerify=1` in the view's Game.ini | **all four gates pass**: "Closing down shell ready for game launch", "Launched …\MIB\MIB.exe" — then §1 |

**The four gates, the order and where each answer comes from are the SEGA Game Shell recipe, shared with
Transformers: Shadows Rising and written once in `patches/SEGA-GAME-SHELL.md`.** Below is this title's run
of them.

### The four gates (MEASURED, static read of `Shell.exe` 230307RED; each confirmed by a run)

* **A — checksum of its own file.** The shell reads `/k=<8 hex>` off its command line and compares it to
  a CRC of the file `shell.exe` in its working directory (table and seed inside the binary). No match →
  it parks. On the cabinet `SystemLauncher.exe` computes and passes it. For the unmodified file the value
  is `/k=0d051181` (m3); for the view's modified copy it is `/k=e6222935` (m5–m6). The script
  `harness/mib-shell-copy.sh` computes it the way the shell does.
* **B — the I/O board.** The shell polls for a HID device (vendor `0x1043`) and, if it is not there after
  5 s, shows "IO BOARD FAILURE" and stops. The dump of the sister title Transformers: Shadows Rising
  carries a **nine-byte change** in its own `Shell.exe` that makes the shell believe the board is there;
  the same change at the same place (file offset `0xbc21`, `cmp [flag],0; je` → set the flag and `nop`)
  is applied to the view's **local copy** of `Shell.exe` by `harness/mib-shell-copy.sh`. It does **not**
  emulate the board — the shell gets no data from it (m6's I/O log is 2,815 lines of "WriteHID — Lost
  HiD Handle!"). That the board is only believed, not present, is an open item of this title.
* **C — the DK2 security key.** "Unable to find security key." → the shell would park, unless
  `[Debug] SecurityDisabled=1` is set, which lets it carry on (and log "Failed to find valid cabinet SKU",
  because the cabinet type normally comes from the dongle).
* **D — the game's integrity/backup check.** Without `[Debug] NoVerify=1` the shell wants a backup copy
  of the game's install file (the dump's `backup/` is empty) and would log "UPDATE FAILED"; with it set
  it skips that and the per-file checksum and goes straight to the launch.

The two `[Debug]` switches are exactly the two the Transformers dump sets in its own `Game.ini`
(`SecurityDisabled=1`, `NoVerify=1`); that shell is also patched in the same I/O spot. So this is the
same recipe the working sister title already uses, applied to local copies — **the share is never
written** (§5).

### What the game then does (MEASURED, m6 `Networking`/`GameStateMachine` logs + Unity log)

The shell opens a socket; the game connects; `GAME_MSG_LAUNCHED_OK`; the shell sends
`SET_CREDIT_VALUE = 3` and `SHELL_MSG_SET_SKU — Cab Type = STANDARD`; the game loads its assets
(`GAME_MSG_FINISHED_LOADING_ASSETS`, 37 s), sends `GAME_MSG_SECURITY_CHALLENGE` twice, shows its logo —
and quits (§1). So the shell↔game socket protocol works far enough to start the game and set its
credits and cabinet type; the challenge is where it ends.

---

## 3. What the game expects of the cabinet (and what is missing here)

| piece | expected by | here |
|---|---|---|
| the I/O board (HID vendor `0x1043`, lamps/switches/recoil) | `Shell.exe` gate B | absent; the shell is made to believe it is there (§2) |
| the DK2 dongle / security key (`dk2win32.dll`, `dk2win64.dll`) | `Shell.exe` gate C, and the game's `GAME_MSG_SECURITY_CHALLENGE` | absent; `SecurityDisabled=1` gets the shell past, the game's challenge is unanswered (§1) |
| a serial device the game opens (the first COM port that answers) | `MIB.exe` ("CONNECTING TO SERIAL") | none offered; on this machine the only serial port was the owner's Steam Controller Puck, which must be hidden |
| the gun, start, coin, tickets | the I/O board and the shell's ticket modules | none driven |

So two cabinet parts are stubbed to get the game on screen (the I/O board flag, the security gate) and a
third (the serial) is still open. None of this gives the game its inputs. The TeknoParrot route
(`GameProfiles/MIB.xml`, subscriber-only) sidesteps all of it by starting `MIB.exe` directly on its
closed core, without the shell — that core stands in for the shell, the dongle and the board at once.

---

## 4. What was loaded

* **MEASURED**, m6 (`runs/m6.run`): the shell runs as `Shell.exe` on our core
  (`OpenParrotWin32/OpenParrot.dll`, full path `openparrot/fc8ba632…`), and it starts `MIB.exe` as a
  separate process (pid seen each cycle, 78–86 threads). No core DLL is mapped inside `MIB.exe` — the
  game runs on plain Wine/DXVK; the core is in the shell. The run's record names our 32-bit core in
  every sample and refuses to start if the plan's core is not ours (the guard, as for the other titles).
* **MEASURED**, m2 (`runs/m2.run`, `m2-Player.log`): `MIB.exe` started **directly**, without the shell,
  on our 64-bit core — it shows the MIB logo too, connects to the serial (the puck) and quits. That run
  proves the game itself runs on our core; it is not the cabinet's chain.
* **Blind spot:** nothing was recorded inside `MIB.exe` beyond its own Unity log; the shell↔game protocol
  was read only from the shell's side.

## 5. The share, and the view

Nothing was written to the share in any run (each record counts 0 files newer than its start). The view
`~/.local/share/ravengun/runtime/views/MenInBlack` is the cabinet's `C:\Sega`: links to the share,
except the `shell/` folder, which is local so that two files in it can be local copies — the **modified
`Shell.exe`** and the **`Game.ini` with the two `[Debug]` switches** (`harness/mib-view.sh`,
`mib-shell-copy.sh`, `mib-gameini.sh`). The game's writes land in the Wine prefix
`ravengun-test-mib` (a copy of the DXVK prefix; its `drive_c/Sega` points at the view), not on the
share. The dump's own `Shell.exe` and `Game.ini` are never changed.

## 6. Reproducing m6

```
bash bewijs-attract-20261014/harness/mib-view.sh                 # the view: shell/ local, the rest links to the share
bash bewijs-attract-20261014/harness/mib-shell-copy.sh           # the local Shell.exe with the I/O change; prints /k=e6222935
bash bewijs-attract-20261014/harness/mib-gameini.sh 1 1          # SecurityDisabled=1 NoVerify=1 in the local Game.ini
# game.ini here: [Launch] Arguments = /k=e6222935, [Wine] Prefix = ravengun-test-mib, our core
cd <loader repository> && python3 scripts/launch-direct.py MenInBlack --run
# as run, with a kept write layer and the owner's serial hidden:
#   WIN_EXPECT_CORE=ravengun WIN_SHARE=overlay-keep WIN_MASK_DEV=/dev/ttyACM0 WIN_COMM2=MIB.exe \
#       bash harness/win-run.sh MenInBlack "Men in Black" Shell.exe m6 240
```

**Checksums:** `runs/SHA256SUMS.txt` (the dump's `Shell.exe` `e6a7bae5…`, the view's local copy
`1067a79a…`; `MIB.exe` `afed0bdd…`; our 32-bit core `413027de…`).
