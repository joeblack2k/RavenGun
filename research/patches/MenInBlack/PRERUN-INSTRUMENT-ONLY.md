# PRE-RUN STATEMENT — the instrument-only run (written BEFORE the run, 2026-10-14)

Fire with: `systemd-run --user --unit=mib-uitdaging --collect --no-block /bin/bash /tmp/mib-go.sh`
(~4.5 min). This file exists so the result cannot be argued about after the fact.

## What the run does

* Installs the **instrumented `Shell.exe`** (sha256 `9a183574…`, /k=`bc9b9a16`) over the
  view-local `views/MenInBlack/shell/Shell.exe` for the duration of the run, and puts the original
  back afterwards. The instrument is a code cave on `SecurityChallengeCheck` (`0x410b10`) that
  appends, per challenge, `C\n` + the 4 challenge bytes, `B\n` + the same 4 bytes, `A\n` + the 4
  bytes the shell answered, to `shell\chal.bin`. **[M]** it appends nothing else and changes no
  arithmetic: the answer it writes is the shell's own, computed by `HGLGenerateAuthenticationData`
  at `0x4109e0`, which the cave calls and then returns to the original tail.
* Places `Game.ini` with `[Debug] SecurityDisabled=1` and `NoVerify=1` where the shell looks
  (staged copy, bound read-only into the sandbox; removed afterwards).
* Leaves the game's `MIB_Data\Managed\` **completely alone**.

## Why it cannot trip the game's integrity list

* **[M]** `MIB/install.crc` lists every file under `MIB_Data\Managed\` including
  `Assembly-CSharp.dll`; it does **not** list `Shell.exe` or `Game.ini`.
* **[M]** The run changes only `Shell.exe` and `Game.ini`, both in `shell/`. Nothing under `MIB/`
  is written. The view's `MIB` is a symlink to the share, and the game's `Assembly-CSharp.dll` in
  the view is the share's own (`b51bbc38…`) — verified before the run.
* **[M]** `shell/install.crc` does list `Shell.exe`, but the shell's own checksum gate is answered
  by `/k=` computed from the file as built, and both switches are set.

## What the run CAN show

1. **The real challenge/answer pair.** `chal.bin` gives, per challenge, the 4 bytes the game sent
   and the 4 bytes the shell answered — the first time either has ever been on record. With those
   an exact shell-side answer can be built offline (the game's decoder is fully known:
   `MIBCore::AuthenticatedData`, driven by the hardcoded 10-byte `SECURITY_KEY`).
2. **The cadence** of `GAME_MSG_SECURITY_CHALLENGE` and of the restarts, with the counter theory
   tested against the clock (five challenges ≈ 50 s → `SecurityShutdown` → exit).
3. **That the instrument is alive**: `C\n`/`B\n` present in `chal.bin` and at least one
   `GAME_MSG_SECURITY_CHALLENGE` in the shell's Networking log. If `B\n` is missing, the
   instrument is broken and the run says nothing at all about the game.
4. Whether `SecurityDisabled=1`+`NoVerify=1` in the shell's own folder still gets the chain past
   its four gates now that the game folder is untouched — i.e. the earlier park does not repeat.

## What the run CANNOT show — read this before interpreting the result

* **It cannot stop the exit, and an "it still exits at ~50 s" result is NOT a refutation of the
  security-challenge hypothesis.** The instrument only *reports*; it does not change the answer the
  shell sends. The game rejects that answer (it is the shell's own older transform, not the game's),
  `m_securityCounter` keeps rising, and `MIBCore::Update` reaches five and calls `ExitGameSequance`
  — exactly as before. Stopping it needs the shell-side correct-answer patch, which needs the pair
  from point 1 above.
* It does not prove the challenge *is* the reason for the exit. That needs the run after this one:
  the same setup with the shell answering correctly, and a game that then lives past five minutes.
* It says nothing about the serial line or the ticket/attendant state; those remain the competing
  explanations unless the patched run changes the outcome.

## Open item, stated honestly

The reason the 2026-10-14 attempt parked after `Validated GameData directory exists` is **not fully
proven**. The correlation is strong — that attempt's one difference from the run that worked (`m6`)
was a patched `Assembly-CSharp.dll` in a real `MIB/` folder, and that file is on the game's
integrity list — but a static cross-reference of the `NoVerify` switch to the CRC reader was **not**
achieved, so the mechanism is **[I]**, not **[M]**. The instrument-only run settles it the safe way:
if it now pastes its four gates and launches, the patched assembly was the cause; if it parks on the
same line, the cause is something else about that earlier layout and the search continues.
