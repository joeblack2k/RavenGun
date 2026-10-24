# ATTRACT-RUN — Terminator Salvation runs its attract loop on our own loader

**Date:** 2026-10-13, 21:05–22:10. **Game:** `game` (7,764,284 B), build `01.25.00`, Sep 12 2011.
**Loader:** our own `tools/rg-elfloader/rg-elfloader`, in a bubblewrap sandbox, on the TV through
gamescope 3.16.30 (Mesa 26.1.2, radeonsi, RX 6950 XT).
**Labels:** **MEASURED** = a log line, a file comparison, a frame that was looked at, or disassembly
read from a function head; **DERIVED** = a conclusion drawn from measurements.
**Evidence:** `bewijs-attract-20261013/` next to this file — harness, instrument sources, per-run logs,
frames. The raw run directories (`/tmp/ts-c/` for r1–r5, `/tmp/ts-b8/` for the 20:01 run) do not
survive a reboot.

This file supersedes §1 of `KETEN-EN-METHODE.md` ("exits 0 without ever creating a window") and
everything else in the dossier that says the game does not draw.

---

## 1. Result, and what is NOT shown

**Shown.** The game boots, passes its warning screen on a START press, and runs its attract loop —
demo scenes, gun tutorials, the Play Mechanix, Halcyon and CoinUp logos, "FREEPLAY" along the bottom.

| run | attract window | length | how it ended | witnessed by |
|---|---|---|---|---|
| **r4** | 21:43:20 – 22:02:25 (first and last attract capture) | 19 min 5 s | the run's own time limit (rc 124, 1200 s) | the run's captures (110, of which 109 distinct; two contact sheets of twelve frames looked at) **and** the coordinator's independent capture, 21:48:32 – 21:59:05 |
| r3 | 21:30:38 – 21:41:46 | 11 min 8 s | the run's own time limit (rc 124, 720 s) | the run's captures only (65, all distinct; two contact sheets of twelve frames looked at) |

**NOT shown — open items, not footnotes:**

1. **Nothing has been played.** No coin, no started game, no trigger pulled in play, and **no real
   light gun at all**. "It runs" today means **it runs and demos**. Gun aim on this route has not been
   looked at; §2.6 has the only lead.
2. **No sound.** `/dev/dsp` is `/dev/null` on this route.
3. **It is not a product run.** Nine pieces live only in a test harness (§6), and the START press that
   gets past the warning screen is typed by a script, not by the daemon.
4. **The network is open.** The game tries TCP to `67.217.33.130:8985` every 25–100 s; no connect is
   known to have succeeded. The product sandbox isolates the network; no run with isolation has been
   made for this title.
5. **The unit now identifies as UK** (§3.5). Whether that is what the owner wants is his call.
6. **The file-check warning** still appears on every boot (§4.3).
7. **No run with only the needed pieces.** Every run carried the diagnostic loggers.

---

## 2. What the game was waiting for: START, on a serial line

### 2.1 The stall was not at "Verifying program files"

The 20:01 TV run (`/tmp/ts-b8`) kept its own full-screen captures. **MEASURED** (frames looked at):

| capture | what is on it |
|---|---|
| 20:02:07 | boot text complete, ending "Game file errors detected. / Please run file check in 'test' mode. / System initialization complete. / Game starting..." (`shots/b8-200207-boottekst-compleet.png`) |
| 20:02:22 – 20:07:59, 23 identical captures | "WARNING! Data Files Corrupted. (Game Operation May Become Unstable.) Press Start To Continue." (`shots/b8-200759-data-files-corrupted.png`) |

The image that ended at "Verifying program files." (`/tmp/t4-laatste.png`, saved 20:05;
`shots/b8-t4-laatste-verouderd-beeld.png`) is a real frame of the boot — r1 produced the
byte-identical frame at 21:19:32 and 21:19:43 — but it was not what the TV showed at 20:05.
**Instrument lesson 9: a screenshot file's timestamp is not the time of the picture.**

### 2.2 Where START comes from

* The warning screen (`0x080ef170`) registers callback `0x080ed550` for switch ids 7, 8 and 0x1b
  (left START, right START, DIAG) and loops until it sets `[0x088bcecc] = 1`. **MEASURED** (disassembly).
* Switches do **not** come from `/dev/js0` (its readers keep axis events only), the keyboard (no GLUT
  keyboard callback is registered) or shared memory. **MEASURED** for those doors.
* They come from a serial "JAMMA I/O" board on **`/dev/ttyS0`**, 115200 raw (`0x081daab0` opens it,
  `0x081dacb3`/`0x081d6405` frame it, `0x081d9026` handles type `S`). **MEASURED** (disassembly).
* Frame: `LF` + type character + hex digit pairs + `CR`, no checksum. Type `S` must decode to exactly
  9 bytes with bit 7 set in byte 1. Bytes 2–8 are fourteen **4-bit wrapping transition counters**, one
  per switch; the first valid frame is a baseline and each later increment is one edge (even→odd =
  press, odd→even = release).

  | byte | high nibble | low nibble |
  |---|---|---|
  | 2 | trigger 1 | trigger 2 |
  | 3 | pump 1 | pump 2 |
  | 4 | START 1 | START 2 |
  | 5 | COIN 1 | COIN 2 |
  | 6 | volume up | volume down |
  | 7 | TEST | SERVICE |
  | 8 | tilt | bill |

  A START 1 press: `\nS8000000000000000\r` (baseline), `\nS8000001000000000\r` (press),
  `\nS8000002000000000\r` (release).

### 2.3 Bench confirmation

**MEASURED**, r1: the three frames written at 21:20:30; `scr_start_cb` traced at +67.29 s with first
argument 7; the warning screen gone in the next capture. Later in the same run TEST opened the
operator MAIN MENU (game log: `GConLoop() diag sw cnt=1, calling GameStartDiag()`), volume-down moved
the cursor, START selected. A trigger frame on the country-code screen did nothing visible.

Tool: `bewijs-attract-20261013/harness/ts-jamma-inject.py <peer-pty> <switch>`.

### 2.4 The game also writes to the board, and the writes must be consumed

The game sends `P<n><hex>`, `W03`, `s` continuously. With nobody reading the far end of the pty, the
buffer fills: in r3 the game log started `JAMMA: jamma_msg() -1` at 21:34:54 (4 min 29 s after START)
and logged it 2,142 times — 11 lines in minute 21:34, 2,103 in 21:35, 28 in 21:36. Both pty peers were
drained with `cat` from 21:36:33; the last error line is 21:36:30 and none followed in the remaining
5 min 17 s. In r4 both peers were drained from the first second: **zero** such lines in 20 minutes.
The attract loop kept running through r3's error flood. **MEASURED.**

### 2.5 Not determined

Whether the game ever needs *replies* to its own commands (it reached attract and the test menu
without any; the JAMMA version on the version page, `26q.H00.F00.U00 R`, is what it shows with no
board answering). Which of the two ptys cures `JammaInit`: `/dev/sierra2` got 12 bytes from the game
in a whole run, `/dev/ttyS0` about 10 kB a minute.

### 2.6 A lead for the gun route: the connection manager looks for a USB serial port

The lines `rm: cannot remove '/g3/irgun/cmgrNNN.txt'` in the run output are **not** a file missing from
the dump. The game runs `system("find /sys/devices/ -maxdepth 6 -name ttyUSB* > /g3/irgun/cmgrNNN.txt")`
followed by `system("rm …")`, twice two seconds apart, at +1/+3 s and then about every 3.5 minutes;
and once at start `ls -r /g3/irgun/firmware/ | grep ".mcs" | … > /g3/irgun/cmgrNNN.txt`. The redirect
fails with "Read-only file system" because the harness mounts `/g3` read-only. **MEASURED** (game-side
exit log, `r3/exit.kern.log`). So the gun board is expected on a `ttyUSB*` port, and `/g3/irgun` must
be writable before that scan can ever succeed.

---

## 3. "Game must be registered": our own dongle emulation

### 3.1 What happened

After START, r1 showed the standby screen "PLEASE STAND BY / Game must be registered / MACHINE IS
UNDERGOING SYSTEM MAINTENANCE" and stayed there (`shots/r1-212034-game-must-be-registered.jpg`). The
operator menu in the same run offered "ENTER SERIAL NUMBER" and "ENTER COUNTRY CODE", and the version
page said "SERIAL NUMBER: NOT SET" (`shots/r1-212415-versies-serial-not-set.jpg`). **MEASURED.**

### 3.2 Cause

The drop-in served **both** HASP file ids from byte 0 of the same 128-byte dump (`hasp/hhl_mem.dmp`).
The dump is **one 112-byte memory holding both**:

| file id | dump range | what the game reads there | fit |
|---|---|---|---|
| `0xFFF0` (main) | `0x00–0x2F` (48 B) | 36 bytes at 0 and a CRC32 at 36 | zlib `crc32(dump[0:36])` = `b586cbdb` = `dump[0x24:0x28]` — **MEASURED** (recomputed) |
| `0xFFF2` (licence) | `0x30–0x6F` (64 B) | feature words at 0 and 12; country code at `0x1c`; cabinet serial at `0x3c` (offsets from the code) | country byte = `dump[0x4c]` = 3; serial = `dump[0x6c:0x70]` = 2658; feature words = `FF FF FF FF` — **MEASURED** values; the game shows exactly that serial (§3.3) |
| — | `0x70–0x7F` | nothing | dump trailer (`WBPT…`) |

With the old mapping the game read `da 36 7e a4` at `0xFFF2+0`. The feature decoder (`0x81c6880`)
maps `FF FF FF FF` → 0, `00 00 FF FF` → 1, **anything else → 2**; 2 means "revenue share on", and with
no registration file (`/T4User/aud/op.00{0,1,2}.aud` — absent, also on the share) the decision
function `0x81adad0` sets halt reason 5 = "Game must be registered". **MEASURED** (disassembly +
`bestand.log`), chain **DERIVED**. **Do not zero-fill the memory files: `00 00 00 00` also decodes
to 2.**

### 3.3 Confirmation

* r2: same recipe as r1 with only the `0xFFF2` content replaced by `dump[0x30:0x70]` → after START a
  ten-second standby "Restarting Game Machine." (`shots/r2-212656-restarting-game-machine.jpg`), then
  attract (`shots/r2-212711-attract.jpg`). **MEASURED.**
* r3, r4, r5: the drop-in as it now stands (48- and 64-byte files, no override) → same. **MEASURED.**
* r5: the version page reads **"SERIAL NUMBER 2658"**, and the two "ENTER …" entries are gone from the
  main menu (`shots/r5-220839-versies-serial-2658-UK.jpg`,
  `shots/r5-220801-testmenu-zonder-serial-en-land.jpg`). 2658 exists in the dump only at `0x6c`.
  **MEASURED.**
* The bench measurement and the static analysis were done independently and agree.

### 3.4 Changed in the drop-in (working tree, not committed)

* `dropin/ravengun-hasp/Terminator/memory/00000000000000001111111111110000` — `dump[0x00:0x30]`, 48 B, sha256 `b57b9865…`
* `dropin/ravengun-hasp/Terminator/memory/00000000000000001111111111110010` — `dump[0x30:0x70]`, 64 B, sha256 `6920e9b0…`
* `dropin/ravengun-hasp/Terminator/dongle.json` — sizes 48 / 64, and the note rewritten.

The game wrote nothing to the dongle in any run: the hook's call log has no `hasp_write` in r1, r4 or
r5, and the memory files in each run's view are unchanged. **MEASURED.**

### 3.5 Side effect: the unit is a UK unit

With the licence area read correctly the game shows **`01.25.00.UK`** in its menus (r5); with the old
garbage (country byte `0xb0`, invalid) it showed `01.25.00.US` (r1). The game's own log header always
says `.US` — it is written before the dongle is read, so it is no evidence either way. Country byte 3
is what the dump holds; an earlier reading of mine that 3 = USA (from the order of the list on the
country screen) was **wrong**. What the region changes in the game has not been looked at. The game
can set the country itself (test menu, one byte written to `0xFFF2+0x1c` in the view copy).

---

## 4. The crash in attract, and the shaders

### 4.1 What happened

r2 died 155 s after START (21:29:21): `Received FATAL signal SIGSEGV (11)`, `eip: 080DAC20`,
`cr2: 00000020`, `eax: 00000000`, exit 255. **MEASURED** (game log, `r2/syslog-this-run.txt`).

At `0x80dac1b` the renderer loads the *current shader* global `0x88b6d38` and reads `[eax+0x20]`. The
current shader had just been set from the material's shader pointer (`[esi+0x90]`, bound through
`0x80e1b20`). A material whose shader failed to load therefore gives a null current shader.
**MEASURED** (disassembly from `0x80dab7c`); that this is what happened in r2 is **DERIVED**.

### 4.2 Why shaders fail here, and the fix

On our route 13 shaders failed to load (`ec=-9596, shdr=0, prgm=0`). Reproduced offline 13 for 13 in a
headless Mesa context: the sources carry no `#version`, so Mesa compiles them as GLSL 1.10 and rejects
1.20 constructs (`mat3(mat4)` in `include/vert_anim.gls`, `transpose()`). **MEASURED** (compiler
messages). TeknoParrot launches in the same syslog log exactly one failure, `shader103b`, in each of
the 8 launches that reached shader load.

**`force_glsl_version=120` in the game's environment: 13 failures → 1** (`shader103b`), measured in
the game in r3, r4 and r5; r3 and r4 ran 11 and 19 minutes past the point where r2 crashed.
**MEASURED.** That the remaining `shader103b` failure is harmless is not proven; it matches what
TeknoParrot logs.

### 4.3 The file-check warning itself

`DiagCheckAllFiles` compares size, CRC32 and SHA-1 of each shader file against a table in the binary
(105 entries at `0x86209a0`). **104 of 105 match.** `include/frag_shadmap.gls` on the share is a
modified file (1088 B); its untouched original sits next to it as `frag_shadmap.gls.ORI` and matches
the table exactly (1104 B, CRC `4f66c0db`). The modification joins three multi-line macros; the
original does not compile on Mesa at all ("Unterminated #if"), so the modified file is the one to
keep. **MEASURED** (recomputed and decrypted offline). The dongle crypto answers are correct — all 107
shader files decrypt and inflate with the table answers.

So the warning is true, harmless, and costs one START per boot. No operator setting suppresses it
(**MEASURED** in the code). Not yet tried: correcting table entry #5 in memory after load (48 bytes at
vaddr `0x8620c24`: size `40040000`, CRC `f6c8af05`, SHA-1 `caca30f8666e3d65a4ca5ee7187ea1bc6e243b98`).
Overlaying `.ORI` is **not** an option: the check passes but eight more shaders then fail.

---

## 5. Reproducing it: the runs, the exact differences, the checksums

### 5.1 The runs

| run | time | outcome |
|---|---|---|
| b8 | 20:01–20:08 | warning screen; nothing could press START; killed from outside after 380 s |
| r1 | 21:19:22–21:25:41 | START accepted 21:20:30 → **"Game must be registered"**; operator menu explored; stopped by me |
| r2 | 21:26:09–21:29:22 | START 21:26:46 → attract from ≈21:27:00 → **SIGSEGV 21:29:21** |
| r3 | 21:29:47–21:41:47 | START 21:30:25 → **attract 21:30:38 to the time limit** |
| r4 | 21:42:28–22:02:28 | START 21:43:06 → **attract 21:43:20 to the time limit**; left untouched; independently witnessed |
| r5 | 22:06:41–22:09:33 | START 22:07:19, TEST 22:07:42: serial 2658 and `.UK` read off the version page |

### 5.2 What changed from run to run — nothing else did

| step | the difference | effect |
|---|---|---|
| b8 → r1 | a START frame on the serial link; emulator `ravengun-hasp.so` rebuilt at 20:36 (`7eb191e0…` → `6c7833dc…`; r1 reached the same warning screen, byte-identical capture) | past the warning; lockout |
| r1 → r2 | **the split, licence half only**: the `0xFFF2` file in the run's view replaced by `dump[0x30:0x70]` padded with `FF` to 128 B (sha256 `7d5e380a…`); `0xFFF0` still the whole dump (`759668ae…`) | lockout gone; attract; crash after 155 s |
| r2 → r3 | **(a) the shader version**: `force_glsl_version=120` exported in the game's environment (`GAME_ENV` in the harness; visible in `r3/binnenr3.sh`); **(b) the split moved into the drop-in at its true sizes**: `0xFFF0` = `dump[0x00:0x30]` 48 B (`b57b9865…`), `0xFFF2` = `dump[0x30:0x70]` 64 B (`6920e9b0…`), no override | 13 shader failures → 1; no crash |
| r3 → r4 | both serial pty peers drained from the first second (two `cat` lines in `c-game.sh`); time limit 1200 s | no `jamma_msg` errors |

Of the two changes in r2 → r3, (a) is the one that bears on the crash: for every dongle read made in
an untouched attract run (eight, all within `0xFFF0` 0–39 and `0xFFF2` 0–3, 12–15, 28) change (b)
serves the same bytes as r2's override did. **DERIVED** from the two files and the read list.

To repeat r4: `GAME_ENV="force_glsl_version=120" RUNSECS=1200 scripts/rg-tv-env.sh -- bash
harness/c-outer.sh <tag>` and then `bash harness/c-probe.sh <tag>` (see `harness/LEESMIJ.txt`; the
scripts hold absolute paths to the session's scratch directory and need those adjusted).

### 5.3 Checksums of r4 (`r4/SHA256SUMS.txt`)

| piece | sha256 |
|---|---|
| loader `tools/rg-elfloader/rg-elfloader` (loader repo HEAD `8286b6a`, tree clean) | `7f83edb291f0065c54122392679ef0ffe65190c909d596485e56460e32ac4549` |
| `game` | `798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a` |
| `dropin/libravengun-hasphook.so` | `de9bf43137907e3d70195c9c6142752b9f4d6c9a70e2f7a6d8796f4a0990e233` |
| `dropin/ravengun-hasp.so` | `6c7833dc8ed65fa5a78c93834843eacff432f561155119e044cf3ec30ef5a0f8` |
| `dropin/hasp-haken.ini` | `4b12f8de5193e89bad43ddba461cb866c8d25441a91027f76cd32d2549507055` |
| `dropin/dropin.ini` | `f3c85c6bb942b0550bf1a1453a89b76953c779442293e06a72137e0a6fb3430a` |
| `dropin/…/dongle.json` (as it was during r4; the note text was corrected afterwards) | `302c78ca43437753b03e50b9e667958763eaed9c30dcd513e28efe0b2811aec5` |
| `dropin/…/memory/…0000` (`0xFFF0`) | `b57b98654b18fd9ba241c567bf0fc3fde54e9995adb41924864201b0dfad67bb` |
| `dropin/…/memory/…0010` (`0xFFF2`) | `6920e9b04e3ba3f266bf4a5e8cb60f30f82bdd89afb8ae5a9fb95869fcc260b4` |

The hook, emulator and address list in r4's view were byte-identical to the patch directory (`cmp`).
The instrument libraries' checksums are in the same file.

### 5.4 The frames

* `r4/opnames-index.txt` — all 115 captures of r4 with time, sha256 prefix and size. One pair is
  identical (21:48:43 and 21:55:50, 7 min 7 s apart).
* `shots/r4-contactvel.jpg` — twelve frames of r4, 21:43:20 … 22:02:25; `shots/r3-contactvel.jpg` —
  twelve of r3, 21:30:38 … 21:41:46; plus three single frames of each run. Looked at.
* Per-run notes: `r3/NOTITIE.txt`, `r4/NOTITIE.txt`.

### 5.5 How the 20:01 run came to draw at all

Reconstructed from `/tmp/ts-b4 … ts-b8`; each line names what failed without the ingredient:

1. dongle hook + emulator with the **reply table** for `hasp_encrypt`/`hasp_decrypt` (the game's own
   `hasp/` directory) — without it `GameLoadCnnf` and the telemetry files fail at 0.18 s;
2. a `0xFFF0` memory file — without it exit 0 at 1.1 s;
3. ptys as `/dev/sierra2` and `/dev/ttyS0` — without them `JammaInit() failed -9097`;
4. `/dev/null` as `/dev/dsp` plus an ioctl answer for it (`RG_FAKE_DSP`) — without it sound init fails;
5. the `glutInit` argument rewrite (`RG_GLUT_DISPLAY_FROM_ENV`) — the game hardcodes `-display :0.0`
   and dies at 3 s otherwise;
6. `PR_SET_PTRACER_ANY` (ptracer-permit) — measured necessity from the 09:30 A/B;
7. `/g3` = the game directory read-only, `/T4User` as an overlay, a local view directory as cwd.

---

## 6. Product versus harness — what still lives only in a test script

**In the drop-in now:** hook, address list, emulator, `dongle.json`, the two memory files.

**Harness only** (`bewijs-attract-20261013/harness/` and `instr/`; none of this is in the loader, the
launcher or `play-native.sh`):

1. the sandbox layout (`/g3`, `/T4User` overlay, the view directory) — `packaging/native-games.json`
   still lists Terminator as parked at `/pm` with the old "exit 255" note;
2. the device nodes: ptys for `/dev/ttyS0` and `/dev/sierra2`, `/dev/null` for `/dev/dsp` and `/dev/js0`;
3. **the serial I/O side**: something that holds the far end of `/dev/ttyS0`, reads what the game
   writes and writes `S` frames — a new I/O-bridge dialect for the daemon (START, COIN, TEST, triggers
   and pumps are all on this line);
4. `RG_FAKE_DSP` — implemented only inside the ioctl *logger* (`instr/ioctl-log.c`);
5. the `glutInit` rewrite — implemented only inside the exit *logger* (`instr/exit-log.c`);
6. a built ptracer-permit library in the preload;
7. applying `dropin.ini` on the native route (the harness re-implements it in a Python snippet);
8. `force_glsl_version=120` in the game environment;
9. the gamescope / TV orchestration.

---

## 7. The instruments: what loaded, what did not, and what rests on them

**The claim to settle:** the run output of r3 and r4 contains hundreds of lines
`ERROR: ld.so: object '…' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS32): ignored.`
for all eight preloads, the five loggers (exit, ioctl, network, file, fork) among them.

**What is measured:**

* The game and the loader are **32-bit** (`file`: `ELF 32-bit LSB executable, Intel i386` and
  `ELF 32-bit LSB pie executable, Intel i386`), and so are all eight libraries. They match.
* **All eight were mapped in the game process**, and so was the dongle emulator — read from
  `/proc/<pid>/maps` of the live processes (r3 at 21:38:49, r4 at 21:43:37; `harness/c-loaded.sh`).
* The logs say the same on their own (`r3/LAADBEWIJS.txt`, `r4/LAADBEWIJS.txt`). In r4, lines carrying
  the game's pid: exit log 10,719,944; ioctl log 596,312 (all of it); file log 31,610; net log 36 (all
  of it); trace 61,358. The fork logger wrote its start line in the game and 559 k lines in the game's
  own short-lived children.
* The refusal lines come from the **64-bit helper processes** that inherit `LD_PRELOAD`: `timeout`,
  `env`, and every `sh`/`find`/`ls`/`sed`/`rm` the game starts through `system()`, plus `gcc` for the
  loader shim — 31 such processes in r3, 43 in r4, eight lines each.
* Two of the "loggers" are not observers at all: `libioctl-log.so` carries `RG_FAKE_DSP` and
  `libexit-log.so` carries the `glutInit` rewrite. Without them the game dies before it draws (§5.5).

**The gaps that do exist, written down:**

1. The five loggers recorded the game and recorded **nothing inside the 64-bit helpers**. What a
   helper did — for instance why the `find … > /g3/irgun/…` redirect failed — is known only from that
   helper's own stderr in the run output, not from a trace. No 64-bit build of the loggers was made.
2. **The dongle emulator's argument trace stops after 64 lines** (`HASP_TRACE_MAX` in
   `hasp/src/dongle.c`). It shows the first eight `hasp_read` calls with file id and offset and then
   nothing. The hook's own log (`hasphook.log`, names only) counts every call: r4, left in attract,
   made 8 reads in 20 minutes; r1 and r5, with the operator menu open, made 1,117 and 3,283. **Which
   offsets those menu-time reads used was not recorded** — that the version page reads the serial at
   `0xFFF2+0x3c` comes from the code and from the number on the screen, not from a trace line.

**What rests on the five logs:** the `system()` commands in §2.6 (exit log), the open of `/dev/ttyS0`
and the missing `op.00x.aud` files in §2.2 and §3.2 (file log), and the network attempts in §1 (net
log) — all recorded in the game process. The attract claim rests on none of them: it rests on the
captures, the game's own syslog, and the coordinator's independent capture.

**Instrument lesson 10: an ld.so refusal line names a library, not a process. Before reading it either
way, check `/proc/<pid>/maps` of the process you mean.**
**Instrument lesson 11: a trace with a line limit goes quiet exactly when the run gets interesting.
Know the limit, and keep a second counter that has none.**

## Full-resolution captures

`bewijs-attract-20261013/opnames-r3-tv/`, `opnames-r4-tv/` and `opnames-r6-tv/` hold the captures of the
three witness runs as they were taken (`shots/`, 128 MB, 208 MB and 24 MB), with each run's `env.txt`,
`gamescope.log` and capture log. They were moved here from `/tmp/ts-c/log/` on 2026-10-14; each copy was
compared byte for byte with its original.
