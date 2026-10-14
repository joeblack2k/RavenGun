# Sega Game Shell (NAS folder `Sega`) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new measurements on the shell's own dongle scan; **corrects a NOT DETERMINED** in
`patches/SEGA-GAME-SHELL.md`.
**Verdict:** this dump is **Transformers: Shadows Rising, shell build 180605**, in the **pre-patched arrival
shape**. The DK2/keychip scan (`MainThreadDongleScan`) fails, is logged, and **does not block**; all four gates
pass and the shell launches the game. Nothing to build. The `Dongle - *.log` files are the only measured window
we have onto the Sega dongle scan, and they say exactly one thing.

---

## 1. What is present (MEASURED)

`Sega/` = `GameData/`, `Launcher/`, `ShellData/`, `Transformers2/`, `shell/`, `temp/`.

**Identification, three independent ways:**

* `shell/GameInfo.ini` lists Transformers content: stages `EAST_AMERICA`, `ENGLAND_CENTRAL_CITY`,
  `NORTH_AFRICA_DESERT`, `USA_WEST_COAST`, `CANADIAN_FRONTIER`; characters `MEGATRON`, `STARSCREAM`,
  `LASERBEAK`, `SOUNDWAVE`, `BRAWLER`, `SCORPONOK_KING`, `SKYWARP`, `RAVAGE_DRONE`, …
* `shell/Install.ini`: `Manufacturer=Hollywood Gaming`, `Type=System`, `Product=Shell`,
  `Directory=C:\Sega\Shell`, `Version=180605`, `FilesCount=69`, `Date=05/06/2018 09:58`.
  `Transformers2/install.ini`: `Manufacturer=SEGA Amusements`, `Type=Shooting`, `Product=Transformers2`,
  `Directory=C:\Sega\Transformers2`, `Version=180605`, `InstallFile=install.crc`, `FilesCount=359`,
  **`Secured=2`**, `CleanInstall=1`.
* `shell/Mainthread - 261013.log` prints `Shell Version: 180605`, `Launcher Version: 2.0.0`, and
  `Launched z:\...\sega\\Transformers2\Transformers2.exe`.

| File | Size | md5 |
|---|---|---|
| `shell/Shell.exe` | 1438720 | `27d9cc23bc37ee4d7187a4d3d8f3be6d` |
| `shell/Game.ini` | 738 | — |
| `shell/GameInfo.ini` | 509 | — |
| `shell/Install.ini` | 171 | — |
| `shell/config.ini` | 31 | — (`TEST=13`, `SERVICE=14`) |
| `shell/teknoparrot.ini` | 179 | — |
| `Transformers2/Transformers2.exe` | 9332736 | — |
| `Transformers2/install.crc` | 14400 | — |
| `Launcher/SystemLauncher.exe` | 514048 | — |
| `Launcher/Launcher.ini` | 174 | — (`version=2.0.0`, `ShellDirectory=c:\sega\shell`, 1920x1080) |
| `ShellData/GameSettings.ini` | 299 | — |
| `ShellData/ShellData.ini` | 236 | — |
| `shell/dk2win32.dll` | 7315552 | — |
| `shell/dk2win64.dll` | 10203232 | — |
| `shell/inpout32.dll` / `inpoutx64.dll` | 213109 / 98304 | — |
| `shell/hidapi.dll` | 40448 | — |

`dk2win32.dll` + `dk2win64.dll` + `deskey64.cpl` + `dkcpanel.exe` + `dk2inst.dll` are the **Dinkey (DK2)**
security-key stack, i.e. gate C from `SEGA-GAME-SHELL.md`.

## 2. What it asks for (MEASURED)

The family's four gates are documented in `patches/SEGA-GAME-SHELL.md` and are not repeated here. What this
note adds is the **measured state of gate C** and of the shell↔game challenge on this dump.

**Gate C is answered by configuration, not by emulation.** `shell/Game.ini` `[Debug]`:

```
[Debug]
Enabled=1
Purge=7
DeployEnabled=0
ExpandGameInit=1
Forceinpout64=1
SecurityDisabled=1
NoVerify=1
```

`SecurityDisabled=1` (gate C) and `NoVerify=1` (gate D) are both set.

**`Shell.exe` already carries the nine-byte I/O force.** Byte-pattern scan for
`31 c0 40 a3 6c 38 40 01 90` finds it **exactly once**; the same scan against `ShellData/ShellData.ini` finds
**0** (calibration). That is the same nine bytes `SEGA-GAME-SHELL.md` records for the pre-patched 180605
Transformers build. So gate B (I/O board) is forced, gate A (`/k=`) is skipped, and gates C and D come from the
ini. All four accounted for.

**The DK2 scan's only output, across 27 sessions.** `shell/Dongle - 261009.log`, `- 261010.log`, `- 261011.log`
and `- 261013.log` together contain exactly one line family:

```
     27 DONGLE : MainThreadDongleScan: failed stage 1
```

27 occurrences, exactly one per session (16 + 1 + 8 + 2 sessions, dated 09/10/2026 through 13/10/2026).
**`stage 2` never appears in any log.** Searching every file under `Sega/shell/` for the text `stage 2`
returns exactly one hit, and it is `Shell.exe` **itself** — the string exists in the binary, never in a log.
So the scan always stops at stage 1 and we have never observed what stage 2 would ask. That is the whole
`sega-scan` unknown, and it is unchanged by this dump.

**It does not block — measured three ways:**

1. `shell/system - 261013.log` contains gate C's own line, `Unable to find security key.`, twice — and the shell
   continues past it.
2. `shell/Mainthread - 261013.log` reaches, in order:
   `Validated GameData directory exists` → `Closing down shell ready for game launch` →
   `Launched ...\Transformers2\Transformers2.exe` → `MAINTHREAD : Sending Power up Credit`.
3. `shell/GameStateMachine - 261013.log` then shows the game running: `GAME_MSG_LAUNCHED_OK`,
   `GAME_MSG_ASSETS_LOADED, Load Time = 76 sec`, `GAME_MSG_ATTRACT_START`, `GAME_MSG_ATTRACT_LOGO - 1/2/3/4`.

**A correction to `SEGA-GAME-SHELL.md`.** That document marks this as NOT DETERMINED: *"Whether the shell must
answer that challenge to keep the game alive is NOT DETERMINED"*. It is determined here.
`GameStateMachine - 261013.log` shows the shell emitting a challenge **every 10 seconds** — first 6 s after
`GAME_MSG_LAUNCHED_OK`, and continuing through the 76 s asset load and on into the attract:

```
[ 09:34:19:448 ] GAME_MSG_LAUNCHED_OK
[ 09:34:25:623 ] SHELL_MSG_CHALLENGE data = 00 2d 2d 00
[ 09:34:35:627 ] SHELL_MSG_CHALLENGE data = cf 46 89 cf
[ 09:34:45:639 ] SHELL_MSG_CHALLENGE data = 29 04 2d 29
[ 09:34:55:658 ] SHELL_MSG_CHALLENGE data = b4 78 cc b4
[ 09:35:05:657 ] SHELL_MSG_CHALLENGE data = d8 68 b0 d8
[ 09:35:15:663 ] SHELL_MSG_CHALLENGE data = a7 ff 58 a7
[ 09:35:25:660 ] SHELL_MSG_CHALLENGE data = 3f 2b 14 3f
[ 09:35:35:681 ] SHELL_MSG_CHALLENGE data = f1 fc 0d f1
[ 09:35:35:818 ] GAME_MSG_ASSETS_LOADED, Load Time = 76 sec
[ 09:35:35:895 ] GAME_MSG_ATTRACT_START
[ 09:35:45:673 ] SHELL_MSG_CHALLENGE data = d9 7a a3 d9
[ 09:35:55:669 ] SHELL_MSG_CHALLENGE data = 96 09 9f 96
[ 09:36:05:697 ] SHELL_MSG_CHALLENGE data = 2c a5 89 2c
[ 09:36:15:702 ] SHELL_MSG_CHALLENGE data = 57 74 23 57
[ 09:36:25:708 ] SHELL_MSG_CHALLENGE data = 64 c4 a0 64
[ 09:36:35:721 ] SHELL_MSG_CHALLENGE data = af 15 ba af
```

Two measured properties of the payload, both consistent across all 14 samples:

* it **changes every time** — so it is not a constant we could simply hard-code;
* **byte[0] == byte[3]** in every sample (`00..00`, `cf..cf`, `29..29`, `b4..b4`, `d8..d8`, `a7..a7`,
  `3f..3f`, `f1..f1`, `d9..d9`, `96..96`, `2c..2c`, `57..57`, `64..64`, `af..af`).

So the exchange happens and the game survives **with no DK2 dongle present**. The direction of
`SHELL_MSG_CHALLENGE` is **INFERRED** as shell→game from the `SHELL_MSG_` prefix (the log also uses
`GAME_MSG_*` for messages from the game); the measured fact is the line and its survival, not the direction.

## 3. Do we already answer it?

**Yes, fully, and nothing is left to build for reach.** This dump is the pre-patched shape:
`SecurityDisabled=1` + `NoVerify=1` in `Game.ini`, the nine-byte I/O force already in `Shell.exe`, and the
`/k=` check skipped. `patches/SEGA-GAME-SHELL.md` documents the family and its recipe;
`patches/TransformersShadowsRising/` and `patches/MenInBlack/` hold the scripts that reproduce this shape for
a stock arrival (`mib-shell-copy.sh` for the I/O change and the checksum, `mib-gameini.sh` for the switches,
`mib-view.sh` for a view whose `shell/` is local).

For a **stock** shell the four gates must be answered explicitly — supply `/k=` for the file (or patch the
check), force the I/O flag, set both `[Debug]` switches in a **view-local** `Game.ini`. That is a known,
scripted recipe, not an open problem. The share is never written.

Two family-level cautions already recorded and still valid: forcing the I/O flag is **not** emulating the
board (the shell then believes a board is there and gets no data from it — its I/O log fills with
`WriteHID — Lost HiD Handle!`), and the shell's monitor can call `ExitRequired` on that same flag and log
`FORCED SHUTDOWN of Game!!!`, terminating the game after 20 s (MEASURED on Men in Black; it did not fire here).

## 4. What is missing (and cost)

1. **Nothing for reach.** This dump launches its game.
2. **What stage 2 of the dongle scan asks** — still unknown, and this dump cannot tell us, because the scan never
   gets there. Closing it needs a *different* measurement: break on the shell's scan routine and step it, or
   trace the device calls the scan makes before it gives up on stage 1. **Cost: a traced session on the shell,
   a few hours.** This is the single biggest unknown in this family — see the family summary.
3. **What the 4-byte challenge payload means**, and whether a stock shell would need a real answer to it.
   **Cost: static, inside `Shell.exe`** — the string and its handler are in a 1438720-byte binary we have.
4. **The DK2 dongle's own contents.** Unchanged from `SEGA-GAME-SHELL.md`. Only worth doing if a title turns out
   to *require* it; nothing measured so far does.
5. **Real I/O board input.** As above: the force is a lie, not a board.

## 5. Proposed test (NOT RUN) + positive control

**One test**, and it is a calibration of the switch rather than of the game — because the interesting question
is "is `SecurityDisabled=1` what carries gate C?", not "does the game run?" (it does).

* Arm A: a fresh start of this dump as-is.
* Arm B: the same, but with `SecurityDisabled=0` and `NoVerify=0` in a **view-local** `Game.ini`.

* **Passing result (positive control).** Arm A writes exactly **one**
  `DONGLE : MainThreadDongleScan: failed stage 1` line, `system - <date>.log` shows
  `Unable to find security key.`, and `Mainthread - <date>.log` still reaches
  `Closing down shell ready for game launch` and `Launched ...Transformers2.exe`. Passing here means the scan
  failure is provably non-blocking, which is the claim this note makes.
* **Failing result (negative control).** Arm B must **park**: gate C's line appears and the shell never reaches
  "Closing down shell ready for game launch" (per the family document the shell shows an "…FAILURE" screen or
  parks in its error state). If Arm B behaves exactly like Arm A, then `SecurityDisabled` is **not** what makes
  gate C pass — the dongle scan is inert regardless — and that would falsify the interpretation in §2 and must
  be reported as such. A test whose negative arm cannot fail proves nothing.
* Then, for item 2 (the real unknown), a separate traced session: attach to `Shell.exe`, break where the scan
  routine runs, and step past the `failed stage 1` branch to observe what stage 2 requests. Positive control for
  that trace: the breakpoint is hit and the `failed stage 1` string is produced; negative control: with the
  breakpoint removed, `Dongle - *.log` gains exactly one more identical line.

## 6. Evidence log

```
# identification
cat Sega/shell/GameInfo.ini        -> stages EAST_AMERICA ... ; characters MEGATRON, STARSCREAM, ...
cat Sega/shell/Install.ini         -> Hollywood Gaming / Shell / Version=180605 / FilesCount=69
cat Sega/Transformers2/install.ini -> SEGA Amusements / Shooting / Transformers2 / 180605 / FilesCount=359 /
                                      Secured=2 / CleanInstall=1
cat Sega/shell/Mainthread\ -\ 261013.log
  -> Shell Version: 180605 ; Launcher Version: 2.0.0 ; Validated GameData directory exists ;
     Closing down shell ready for game launch ;
     Launched z:\...\sega\\Transformers2\Transformers2.exe ; MAINTHREAD : Sending Power up Credit

# gates C and D are config
cat Sega/shell/Game.ini   -> [Debug] Enabled=1 Purge=7 DeployEnabled=0 ExpandGameInit=1
                             Forceinpout64=1 SecurityDisabled=1 NoVerify=1
cat Sega/shell/config.ini -> [system] TEST=13 SERVICE=14
cat Sega/shell/teknoparrot.ini -> [General] Input API=RawInput, FreePlay=1, Windowed=0

# gate B is forced in the binary; calibration included
md5sum Sega/shell/Shell.exe            -> 27d9cc23bc37ee4d7187a4d3d8f3be6d   1438720
xxd -p Shell.exe | tr -d '\n' | grep -o -F '31c040a3'                -> 1
xxd -p Shell.exe | tr -d '\n' | grep -o -F '31c040a36c38400190'      -> 1
xxd -p Sega/ShellData/ShellData.ini | tr -d '\n' | grep -o -F '31c040a3' -> 0   (calibration)

# the Dongle log: one line family, 27 sessions, never a stage 2
cat "Sega/shell/Dongle - "*.log | sed 's/^\[.*\] //' | sort | uniq -c
  -> 27  DONGLE : MainThreadDongleScan: failed stage 1     (+ 27 "Log Started" lines)
# per file: Dongle - 261009.log 16 sessions ; 261010 1 ; 261011 8 ; 261013 2
grep -rh -o 'MainThreadDongleScan: [a-z ]*stage [0-9]*' Sega/shell/  | sort | uniq -c
  -> 27 MainThreadDongleScan: failed stage 1
grep -rl 'stage 2' Sega/shell/
  -> Sega/shell/Shell.exe      (the only hit is the binary itself; no log ever contains it)

# gate C's own line, and the shell continuing past it
cat "Sega/shell/Security - 261013.log"  -> Watchdog Initialised (x2; no security error logged here)
cat "Sega/shell/system - 261013.log"    -> [ 09:22:15:816 ] Unable to find security key.
                                           [ 09:34:09:646 ] Unable to find security key.

# the shell answers the game's periodic challenge, with no DK2 dongle
tail -30 "Sega/shell/GameStateMachine - 261013.log"
  -> GAME_MSG_LAUNCHED_OK ; SHELL_MSG_CHALLENGE data = <4 bytes> every 10 s ;
     GAME_MSG_ASSETS_LOADED, Load Time = 76 sec ; GAME_MSG_ATTRACT_START ; GAME_MSG_ATTRACT_LOGO - 1/2/3/4
  -> every sample has byte[0] == byte[3]
```

**Not measured, and not claimed:** what stage 2 asks; what the challenge payload encodes; the direction of
`SHELL_MSG_CHALLENGE` (INFERRED shell→game from the message prefix).
