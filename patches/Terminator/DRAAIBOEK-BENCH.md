# DRAAIBOEK — bench run for Terminator Salvation via our own loader

**Status: NOT RUN. Do not run this until the bench is free and the owner's screen is released.**
The bench takes one game at a time and this run brings up a window.

**Purpose.** Prove, end to end and unambiguously, that `tools/rg-elfloader/` loads this game and reaches
the guest's own `main` after the `DT_HASH` fix — and that the next obstacle is the game's own dongle
check, not our loader.

**What this run does NOT do.** It does not attempt any dongle, HASP, keychip, integrity or checksum
work. When the guest stops on `DongleInitialize() FAILED!` that is a **success** for this run: it means
the loader is finished and the next problem is elsewhere.

---

## 0. Preconditions (check these first, they are cheap)

| # | check | command (on the Deck) | required result |
|---|---|---|---|
| 0.1 | bench free | `pgrep -af 'wine\|BudgieLoader\|rg-elfloader\|bowling_usa\|gamescope\|Terminator' \| grep -v pgrep` | nothing from another game |
| 0.2 | the benchmark game of this round is not running | same as 0.1 | — |
| 0.3 | NAS readable | `ls -l "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"` | 7,764,284 bytes |
| 0.4 | loader built **with the fix** | `grep -n 'DT_GNU_HASH\|strtab - (uint32_t)symtab' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader.c` | shows the new code, **and** the binary is newer than the source: `stat -c '%y %n' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader{,.c}` |
| 0.5 | loader is still 32-bit | `file ~/projects/ravengun/tools/rg-elfloader/rg-elfloader` | `ELF 32-bit LSB pie executable, Intel i386` |
| 0.6 | libraries resolve | `LD_LIBRARY_PATH=<ElfLdr2/libs>:<runtime/lib32> ldd "…/game"` | **no** `not found` line |

`<ElfLdr2/libs>` = `/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs`
`<runtime/lib32>` = `/home/deck/.local/share/ravengun/runtime/lib32`
(`libglut.so.3` lives in the **second** one; with both on the path nothing is missing.)

**Run on a throwaway copy of the guest, never on the NAS file.**
The loader only reads it, but a copy keeps the NAS untouched and makes the run reproducible — and it
also keeps the guest's `argv[0]`/path out of the trailing-space problem:

```sh
mkdir -p /tmp/ts-bench && cd /tmp/ts-bench
cp "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game" ./game
sha256sum ./game
```

---

## 1. The run

**Working directory matters** — the engine looks for its data under absolute `/g3/…` paths, and the
guest's own relative lookups (`hasp/`, `T4User/`, `T4Data/`, `env/`) are relative to the cwd. Use the
view, which is the project's established way around the trailing-space folder name:

```sh
cd ~/.local/share/ravengun/runtime/views/Terminator
export LD_LIBRARY_PATH="/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs:/home/deck/.local/share/ravengun/runtime/lib32"
export RG_ELFLOADER_VERBOSE=1
export DISPLAY=:0            # or leave it for the gamescope wrapper; the point is: exactly one display
mkdir -p /tmp/ts-bench/log
RG_ELFLOADER_VERBOSE=1 /home/deck/.local/share/ravengun/bin/rg-elfloader \
    /tmp/ts-bench/game 2>&1 | tee /tmp/ts-bench/log/run1.txt
```

Notes:
- `RG_ELFLOADER_VERBOSE=1` is what makes the outcome unambiguous; without it the loader prints only the
  `say()` lines and you lose the `dbg()` diagnostics that distinguish the failure modes in §3.
- Use the **installed** binary (`~/.local/share/ravengun/bin/rg-elfloader`) or the repo one
  (`~/projects/ravengun/tools/rg-elfloader/rg-elfloader`) — they are usually byte-identical; check with
  `md5sum` (0.4) so the log names the build you actually tested.
- If the loader is wired through `scripts/play-native.sh`, the equivalent is
  `scripts/play-native.sh --game Terminator --loader`; that path adds `gamescope` and a bubblewrap view.
  **Prefer the direct invocation above for the first run**, because the wrapper's extra layers make a
  failure harder to attribute. Do the wrapped run only after the direct run succeeds.

Wrap it in a timeout so a hang does not hold the bench:

```sh
timeout 90 /home/deck/.local/share/ravengun/bin/rg-elfloader /tmp/ts-bench/game 2>&1 | tee /tmp/ts-bench/log/run1.txt
echo "exit=$?"
```

---

## 2. What SUCCESS looks like (the unambiguous outcome)

The log must contain these four lines, in this order:

```
lib-defs: <N> gedefinieerde symbolen uit de NEEDED-libs gelezen
shim geladen: <M> gast-symbolen in de globale scope
geladen; spel /tmp/ts-bench/game krijgt argv[0]=/tmp/ts-bench/game
spring naar e_entry 0x8057240 (esp 0x……, argc …, envc …, <K> auxv)
```

with:

| field | expected value | meaning |
|---|---|---|
| `<N>` lib-defs | small (the GNU_HASH-only libs contribute nothing until step 4 of the plan) | `parse_lib_file()` ran; not a gate |
| `<M>` guest symbols | **in the low hundreds** (the guest defines 522 dynamic symbols; `shim_skip()` and `lib_defines()` remove some) | **this line existing at all is the proof that the `DT_HASH` fix works** |
| `e_entry` | **exactly `0x8057240`** | the right guest, the right build (it is permuted if the file was swapped) |
| `argc` | the number of args you passed +1-style accounting by the loader | the loader got its argv plumbing |
| `<K>` auxv | ~17-22 | the SysV start stack was built |

**Then the guest itself must take over and do this:**

1. Initialise GL/GLUT through the loader-supplied libraries and open a window (in this direct run, a
   window titled roughly *TeknoBudgie — Terminator Salvation* at about 1360x768; through `gamescope`,
   fullscreen).
2. Reach its own `main` and the dongle path.
3. Print its own text and exit **255** with the group
   `Begin HL-RUS License ---` / `DongleInitialize() FAILED!` / `End HL-RUS License ---`.

That paragraph is the success criterion for this run. It is exactly the same wall the project already
measured for the plain native route, and the same one TeknoParrot's closed core handles.

---

## 3. Reading a deviating outcome — first suspect per failure mode

| what you see | where it died | first suspect | what to check |
|---|---|---|---|
| `rg-elfloader: FATAAL: geen DT_HASH: kan het aantal gast-symbolen niet bepalen (shim)` | still in the loader, **before** `maak_shim()` produced anything | **the `DT_HASH` fix is not in the binary you ran** | 0.4: source has the new code; binary newer than source; and you ran that binary (`md5sum` it into the log). This is the single most likely explanation for this exact line. |
| `FATAAL: kan de shim niet bouwen: …` with a `gcc` error below it | `maak_shim()` | the generated `shim.c` did not compile — usually a symbol name that is not a legal assembler identifier, or the missing dev header `gnu/stubs-32.h` | set `RG_ELFLOADER_INC=~/projects/ravengun/tools/rg-elfloader/inc` (that directory exists for exactly this reason) and look at the `gcc` line in the log |
| `FATAAL: dlopen(<lib>) mislukt` | `laad_needed()` | a library is not on the search path | 0.6. `libglut.so.3` is the one that bites: it is in `runtime/lib32`, **not** in `ElfLdr2/libs` |
| `FATAAL: kan het spelbeeld niet op 0x8048000 mappen` | `map_segments()` | the address range is occupied — almost always **the loader was launched twice**, or a leftover process still holds part of `0x08048000-0x08cee000` | 0.1, then `pgrep -af rg-elfloader` |
| `FATAAL: kan segment <n> niet inlezen` | `map_segments()` | a truncated copy of the guest | `sha256sum` the copy against the NAS file (§0) — 7,764,284 bytes |
| `LET OP: <k> onopgeloste symbolen in DT_JMPREL` and then a crash in the guest | the loader, but **after** the jump | name resolution gaps | if `k` is small and only touches `glut*`, `libglut` was not loaded (0.6). If `k` is large, this is a real finding: capture the list, it names what is missing. |
| guest starts, **window appears**, then dies with a **SIGSEGV / core dump** rather than exit 255 | inside the guest, before the dongle | the loader is essentially done; the fault is guest-side (this is where the packed-class games died on a `0xb750xxxx` slot) | get a backtrace: `gdb -q --batch -ex bt /home/deck/.local/share/ravengun/bin/rg-elfloader core`. Compare against §2 step 3 — if the strings `HL-RUS`/`DongleInitialize` are **not** in the output, the guest died before the dongle check. |
| **exactly the §2 lines, then exit 255 with `DongleInitialize() FAILED!`** | nowhere — this is the target | — | **SUCCESS.** The loader is finished. Record `exit=255` and the log. |
| the §2 lines, then the guest runs **past** the dongle check and continues | — | — | Still a success for the loader (better than expected); record what happens next. It would mean the dongle is *not* the blocker in this configuration. |
| hang with a window, no exit | — | guest is waiting (e.g. on the licence manager / a socket) | `timeout` catches it; capture `pgrep -af rg-elfloader` and `/proc/<pid>/wchan` before killing |
| the loader prints the §2 lines but **no window ever appears** | the guest got control but produced no output | GL/library mismatch, or the guest needs `LD_LIBRARY_PATH` minus the 64-bit dirs | check `ldd` again against the exact environment used, and try the `librt-glfix.so` drop-in as `LD_PRELOAD` |

**Rule of thumb for attribution:** if the log contains `spring naar e_entry`, then **our loader did its
job** and everything after that belongs to the guest. If it does not, the failure is ours. That single
line is the boundary.

---

## 4. What to capture for the report

```sh
{
  echo "== date/host =="; date; uname -a
  echo "== loader ==";  md5sum /home/deck/.local/share/ravengun/bin/rg-elfloader \
                            ~/projects/ravengun/tools/rg-elfloader/rg-elfloader
  echo "== source ==";  grep -n 'DT_GNU_HASH' ~/projects/ravengun/tools/rg-elfloader/rg_elfloader.c
  echo "== guest ==";   sha256sum /tmp/ts-bench/game
  echo "== libs ==";    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" ldd /tmp/ts-bench/game
  echo "== run ==";     cat /tmp/ts-bench/log/run1.txt
} > /tmp/ts-bench/report.txt 2>&1
```

A screenshot of the window (if one appears) belongs in the report next to the log, exactly as the
GoGoStrike proof was recorded — with the caveat that GoGoStrike is gone from the NAS, so **this run is
the only reproducible end-to-end proof our loader can currently produce.** That makes it worth doing
cleanly and recording completely.

---

## 5. Follow-ups this run unlocks

1. **If success (exit 255 on the dongle):** the loader milestone is achieved. Next is `/g3/` + the
   dongle anchor work in the README (§4 and §6). The bench can then be handed to the next game.
2. **A dynamic anchor for the eight UNKNOWN dongle functions.** The static map has no anchor for them
   (README §4). This run gives one: `main` calls `DongleInitialize`, and the measured game xref
   `0x080c2cdb` lies in `main`. Run the loader under `gdb` with a breakpoint there and record the call
   target that follows — that is `DongleInitialize`'s address, **by execution, not by inference**.
   Concretely, a second bench slot:
   ```sh
   gdb -q --args /home/deck/.local/share/ravengun/bin/rg-elfloader /tmp/ts-bench/game
   (gdb) break *0x080c2cdb
   (gdb) run
   (gdb) x/8i $eip
   ```
   Success = a `call` within the following few instructions whose target, when disassembled
   (`x/8i <target>`), opens with the `GetAladdinHLPass`/`DongleInit` call pattern recorded in the
   README. That address then becomes the anchor for `AcquireAladdinDongle`/`SysGetDongleHID`/
   `DongleGetID` in the same step.
3. **`/g3/` verification.** If the run stops with a missing-file error instead of the dongle, the
   `/g3/` binding from plan step 3 is the fix and this run should be repeated with it.

---

## 6. Do-not list

- Do not run this while another game is on the bench.
- Do not write anything to `/home/deck/mnt/spidernas_emulation/…`; work on `/tmp/ts-bench/game`.
- Do not chase the dongle in this run. Stop at the measured `exit 255` and report it.
- Do not run the loader against the NAS file directly — the copy keeps the run attributable and the NAS
  untouched.
