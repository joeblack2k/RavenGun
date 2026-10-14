# SEGA-GAME-SHELL — the "Game Shell" launcher, its four gates, and the shell↔game link

**Written:** 2026-10-14. Shared note for the SEGA ("Hollywood Gaming") **Game Shell** cabinet family, where
a small `Shell.exe` is the program TeknoParrot/our loader starts, and the shell starts the real game.
This is the second title that needs it and it will not be the last.
**Titles so far:** Transformers: Shadows Rising (`Shell.exe` 180605 → `Transformers2.exe`) and Men in Black
(`Shell.exe` 230307RED → `MIB.exe`).
**Labels:** **MEASURED** = read from the files or a run; **DERIVED**; **NOT DETERMINED**.

## What the shell is

On the cabinet: `C:\Sega\` holds `Launcher\SystemLauncher.exe` (the Windows-shell replacement, does updates
and starts the Game Shell), `shell\Shell.exe` (the Game Shell: credits, test menu, tickets, I/O board,
lights, recoil — and it launches the game), the game's own folder (`Transformers2\`, `MIB\`, …),
`ShellData\`, `GameData\`, `backup\`. Each game folder has an `install.ini` with `Product`, `Directory`,
`Type`, `Version`, `FilesCount`. **Our loader/core starts `Shell.exe`**, not the game — the shell is the
"game" of the profile (`[Game] Executable = shell.exe;<game>.exe`). The shell then starts
`<root>\<Product>\<Product>.exe` with an empty argument string, in that folder.

## The four gates, in order

The shell's main thread is a state machine. Between "Validated GameData directory exists" and "Closing
down shell ready for game launch" lie four gates; each must pass or the shell parks in its error state
(or shows an "…FAILURE" screen). MEASURED from `Shell.exe` (addresses are for MIB's 230307RED; the
186050 Transformers build differs in offsets and in that it ships pre-patched, see "Two arrival shapes").

| # | gate | what it checks | where the answer comes from |
|---|---|---|---|
| **A** | **checksum** | `/k=<8 hex>` on the command line vs a CRC of the file `shell.exe` in the working directory (table + seed inside the binary). No match → park. | the cabinet's `SystemLauncher.exe` computes and passes it. A changed `Shell.exe` needs a changed value. |
| **B** | **I/O board** | a HID device (vendor `0x1043`) must appear within ~5 s, or "IO BOARD FAILURE". | a Game Shell I/O board (lamps, keypad, recoil). Not the gun — the gun is a separate device (Transformers: a DirectInput SaeGunBoard). |
| **C** | **DK2 security key** | `dk2win32.dll`/`dk2win64.dll` read a Dinkey dongle; "Unable to find security key." → park. The cabinet type (SKU) normally comes from the dongle's bits. | a DK2 dongle. Bypass: `[Debug] SecurityDisabled=1` in the working-directory `Game.ini`; the SKU then stays "UNKNOWN" / "Failed to find valid cabinet SKU", which does **not** itself block. |
| **D** | **integrity / backup** | wants a backup copy of the game's `install.ini` (in `backup\<game>\`) and checksums the game's files. | the cabinet's backup partition. Bypass: `[Debug] NoVerify=1` in that `Game.ini`, which skips both. |

A failing gate is silent in the logs except its own line (gate A/B leave no log at all; C logs "Unable to
find security key."; D logs "…UPDATE FAILED"). The `Game.ini` the shell reads for `[Debug]` is the one in
its **working directory**, not `c:\Sega\Shell\game.ini` (that one is read only to print the version).
The command-line word the shell looks for is only `/k=`; `waitforio` is a status-text id, not an argument.

## Two arrival shapes — pre-patched vs stock

A dump arrives in one of two states, and the work differs:

* **Pre-patched (Transformers 180605).** Its `Shell.exe` already carries a nine-byte change that forces
  the I/O flag on (so gate B passes with no board) and a jump that skips the `/k` check (gate A), and its
  `Game.ini` already has `SecurityDisabled=1` and `NoVerify=1` (gates C, D). So it launches its game under
  our loader with no change from us. MEASURED: the IO-force bytes `31 c0 40 a3 6c 38 40 01 90` are present
  once in that `Shell.exe`; its `[Debug]` block has both switches; its Mainthread log shows "Launched
  …Transformers2.exe".
* **Stock (Men in Black 230307RED).** Its `Shell.exe` is unmodified: it demands the real `/k=`, requires a
  real I/O board, and its `Game.ini` sets neither switch. So all four gates have to be answered: supply
  `/k=` for the file, apply the same nine-byte I/O-force change to a **view-local copy** of `Shell.exe`
  (then supply the `/k=` for the *patched* file), and set the two `[Debug]` switches in a **view-local**
  `Game.ini`. The share is never written. The scripts that do this, as data in the game's own folder:
  `patches/MenInBlack/bewijs-attract-20261014/harness/mib-shell-copy.sh` (the I/O change + the checksum),
  `mib-gameini.sh` (the switches), `mib-view.sh` (a view whose `shell/` is local so those two files can be).

**Forcing the I/O flag is not emulating the board.** The shell then believes a board is there and gets no
data from it (its I/O log fills with "WriteHID — Lost HiD Handle!"). Real inputs through the Game Shell I/O
board are an open item of every stock title on this route; NOT DETERMINED here.

**Watch item (MEASURED, MIB):** after launch the shell's monitor calls `ExitRequired` on the same I/O flag
and, if it drops, logs "FORCED SHUTDOWN of Game!!!" and terminates the game after 20 s. The force keeps the
flag up, so this did not fire; but a real board that disconnects would trip it.

## The shell↔game link

Once launched the shell and the game talk over a local socket (the shell is the server). MEASURED (MIB,
m6 `Networking` log): `GAME_MSG_LAUNCHED_OK` → the shell sends `SET_CREDIT_VALUE` and
`SHELL_MSG_SET_SKU` (Cab Type = STANDARD) → the game loads assets (`GAME_MSG_FINISHED_LOADING_ASSETS`) and
sends `GAME_MSG_SECURITY_CHALLENGE`. Whether the shell must answer that challenge to keep the game alive is
**NOT DETERMINED** (the owner is taking that one measurement for MIB). The game may also open a serial
device of its own ("CONNECTING TO SERIAL"); offer it none that answers, or the game logs "NOT Connected to
serial".

## Not determined (for the family)

The I/O board's report format and the gun path; the DK2 dongle's contents and whether the game's
security challenge needs it; the socket protocol beyond the messages above; whether every Game Shell
version has these exact four gates (only 180605 and 230307RED were read).
