# NOTITIE — where the Game Shell reads `game.ini` (and two other traps that cost an hour)

**Written 2026-10-14 after the first dynamic attempt on the MIB security challenge failed with a dead
instrument.** Applies to the whole SEGA "Game Shell" family (MIB `Shell.exe` 230307RED, Transformers
180605). Every statement is labelled **[M]** (read from a file or a log) or **[I]** (inferred).

## 1. The shell's working directory is NOT "the shell's folder"

* **[M]** The loader itself prints the working directory it gives the shell. `run.out` of the
  2026-10-14 attempt, from `OpenParrotLoader.exe`:
  `Working directory: Z:\home\deck\.local\share\ravengun\runtime\views\MenInBlack\shell`
  The app-level plan says `cwd: .../TeknoParrotUi-2.0.0.130-linux-x64` (the launcher process), but the
  **shell process** gets the folder that holds `Shell.exe`.
* **[M]** The shell resolves at least one ini name **relatively**: the 11-byte string `.\game.ini`
  lives in `Shell.exe` at VA `0x48c5c4` and is returned by the getter at `0x411470`, which is called
  from `0x436e5b` (the update/install path), together with `.\recoil%d.xml` and `GameShell`.
* **[M]** The shell writes its **own logs** as bare `<Module> - <ddmmyy>.log` names; in every recorded
  run they land in the folder that holds `Shell.exe` (`views/MenInBlack/shell/`).
* **Consequence [I]:** when someone changes where `Shell.exe` lives or who sets its `cwd`, every
  relative read/write moves with it. Put `Game.ini` next to `Shell.exe` **and** verify from the
  loader's own `Working directory:` line — do not assume the launcher's `cwd:` is the shell's.

## 2. The four gates, and which `[Debug]` switch answers which (corrected table)

| gate | what it asks | answer | survives without it? |
|---|---|---|---|
| A checksum | `/k=<8 hex>` == CRC of `shell.exe` in the **shell's** working directory | computed from the file | no, parks |
| B I/O board | HID vendor `0x1043` within ~5 s | nine-byte force at file offset `0xbc21` | no, parks |
| C DK2 key | `dk2win32.dll` finds a Dinkey | `[Debug] SecurityDisabled=1` | **no — see below** |
| D integrity/backup | backup copy + per-file checksums of the game | `[Debug] NoVerify=1` | **no** |

* **[M]** `m1` had **no** switches and parked after `Validated GameData directory exists`.
  `m5` had `SecurityDisabled=1` only and parked at the **same** line. `m6` had
  `SecurityDisabled=1` **and** `NoVerify=1` and reached
  `Closing down shell ready for game launch` → `Launched …\MIB\MIB.exe`.
* **Do not read `SecurityDisabled=1` as sufficient.** In this shell family the pair is what worked;
  set both in the `Game.ini` the shell actually reads, and check that the run reaches
  `Closing down shell ready for game launch` before you believe any later measurement.

## 3. The trap that killed the 2026-10-14 attempt: a view-local `MIB` folder

* **[M]** The run that worked (`m6`) had `views/MenInBlack/MIB` as a **symlink to the share**, so the
  game used the share's own `MIB_Data/Managed/Assembly-CSharp.dll`
  (`b51bbc38…`).
* **[M]** The failed attempt replaced that with a **real folder** holding a **patched**
  `Assembly-CSharp.dll` (`cf04ad1a…`), everything else symlinked. It parked after
  `Validated GameData directory exists`, i.e. **before** the game was launched.
* **[M]** The loader then reported `lader gestopt (exitcode 4)`. In
  `scripts/launch-direct.py`, `EXIT_UNSUPPORTED = 4` and 4 is also what the **loader** returns.
* **Open [I]:** whether the parked shell is caused by the patched assembly itself, by the
  real-folder layout, or by something the direct launcher refuses in that layout, is **not determined**.
  Whoever needs a patched managed assembly: change **one** variable from a known-good run at a time.

## 4. Two mechanical notes

* **[M]** The `--ro-bind <file> <file>` bwrap form works for a single file and leaves no trace on the
  host — use it to place a `Game.ini` for one run without touching a shared runtime directory.
* **[M]** `systemd-run --user --unit=<name> --collect --no-block /bin/bash <script>` is the tidy way to
  own one bounded run; `--collect` removes the unit even when it fails.

## 5. WHY the 2026-10-14 attempt parked: the shell verifies the Managed folder

* **[M]** `MIB/install.crc` (33,683 bytes) lists **every** file of the game, including
  `\MIB_Data\Managed\Assembly-CSharp.dll` and the other 116 managed assemblies. The shell's
  integrity gate (gate D) is what reads it.
* **[M]** The one thing the failed attempt changed relative to the run that worked (`m6`) was
  exactly that file: the view held a **patched** `Assembly-CSharp.dll` (`cf04ad1a…`) instead of
  the share's own (`b51bbc38…`), because `views/MenInBlack/MIB` had been made a real folder.
* **[M]** The shell parked immediately after `Validated GameData directory exists` — the same
  line `m1` and `m5` parked on — and the loader then returned `exitcode 4`.
* **Conclusion [I]:** `NoVerify=1` does **not** exempt the game's per-file checksums in this build,
  or it is not the switch that governs them. Either way:
  **a patched managed assembly is not a usable route for this title.** The security-challenge fix
  has to live in `Shell.exe` (the shell computes the answer the game accepts), not in the game.
* **[M]** The `[Debug]` Game.ini was **already** in the right place during that attempt: the loader's
  own stdout says `Working directory: Z:\…\views\MenInBlack\shell`, and the shell writes its logs
  into that same folder. So the game.ini location was *not* the cause; do not "fix" it again.
