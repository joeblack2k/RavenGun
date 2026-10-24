# LATE-DUMP-INSTRUMENT.md — a late, repeated dump of the 17 hook targets

**What this is.** A measurement instrument for the generic PE inline hook, so that **one single launch**
answers the question: *when, if ever, do the 17 byte ranges in `dropin/dongle-haken.ini` materialise,
and to what?*

**Labels.** **[MEASURED]** = a command was run and its raw output is quoted. **[INFERRED]** = a
conclusion drawn from those measurements. A negative is only reported where the same instrument is
shown to produce a positive (§6).

**Scope and safety.** No game, emulator, launcher or container was started. Nothing on the NAS was
touched. `~/projects/ravengun` (the loader repository) is **unmodified**. Nothing was written into
`patches/LuigisMansion/dropin/` — **the new DLL is NOT deployed** (see §3 and §8).

---

## 1. Where the hook source lives, and what I was allowed to do

**[MEASURED]** `rg-pe-hook.dll` is **not** built from this patches repo. Its source is the **loader**
repository:

| file | role | sha256 (unchanged, still) |
|---|---|---|
| `~/projects/ravengun/tools/rt-hasphook/rg_pe_hook_dll.c` | the Windows side: a DLL that picks up the main program's image | `1033b474aa2eab7b1b55038063a7bfd7dfffbb4a0f5981f93386803d8e7f5232` |
| `~/projects/ravengun/tools/rt-hasphook/rg_pe_hook.c` | the engine: PE header, per-address validation, stubs, prologue conversion, wait route | `3f3b75f972b0f70390c58a8d1d8f59bc14c4fc6dcb7d6fb17abcf9186fb76588` |
| `~/projects/ravengun/tools/rt-hasphook/rg_pe_hook.h` | the engine header | `ae24452269ab53573baa62ef264d1be60533ad91565bf9f5fd3c0ba06a023ffe` |

**[MEASURED]** Those three files are still byte-identical to what they were when this work started
(mtime 09:42–09:51, sha256 above). I did **not** modify the loader repository.

**[INFERRED] How the instrument was built anyway.** The loader repo is off-limits, so the three files
were copied into a scratch tree, and **only the DLL file** (`rg_pe_hook_dll.c`) was changed there.
`rg_pe_hook.c` and `rg_pe_hook.h` in the build tree are **byte-identical** to the loader copies (checked
with `cmp`, §7). That is the strongest available form of the guarantee "the existing hooking behaviour
is unchanged": the entire engine — validation, stub generation, prologue conversion, the wait route —
is the same binary code as the deployed baseline.

### Reachability check before instrumenting
**[MEASURED]** The subject can reach the path. `logs-exittrace-r7/launch.log` (the 10:14 run) shows
`Loading requested compatibility DLL: Z:\...\exe\x64\rg-pe-hook.dll` followed by
`Requested compatibility DLL loaded successfully.`, and the hook writes its own log inside the game
process. In all four recorded attempts `rg-pe-hook.log` contains the PE line and the address-list line,
so "no attach summary" means "found no recognisable prologue", not "the DLL never ran". The channel is
live; what was missing was resolution, which is what this instrument supplies.

---

## 2. What was changed

Exactly three things were **added**. Nothing existing was removed or altered.

| # | Addition | Where |
|---|---|---|
| 1 | **Load proof** — a line written from a GCC constructor at load time | `rg_pe_hook_dll.c` |
| 2 | **A second thread** that samples the target ranges every 250 ms from load until the process dies | `rg_pe_hook_dll.c` + new `rg_late_dump.c/.h` |
| 3 | **A summary** stating whether each target ever became non-zero, and at which sample | `rg_late_dump.c` |

The existing route (`rg_doe_het_werk()` → `rg_pe_hook()`) is present **verbatim**. The instrument
**writes nothing into the game, patches nothing, hooks nothing and answers nothing**: it only **reads**
the first N bytes at exactly the same address the engine's own validation already reads
(`module pointer + RVA`, i.e. `beeld + rva`), from the very same `GetModuleHandleW(NULL)`.

**[MEASURED]** The engine and header in the build tree are byte-identical to the loader copies:
`cmp` reports `IDENTIEK rg_pe_hook.c` and `IDENTIEK rg_pe_hook.h`. The import table of the new DLL is
identical to the baseline's (`diff` → no difference). The existing log strings are all still present
(`wachtroute` 6/6, `aangehaakt` 7/7, `bewijsartefact` 2/2, `hoofdprogramma` 2/2).

### The new files
| file | what |
|---|---|
| `rg_late_dump.h` / `rg_late_dump.c` | the portable sampler core: list reader, byte sampling, form classification, summary. **No OS calls** — the same code runs in the Windows DLL and in a Linux test (§6) |
| `rg_pe_hook_dll.c` | the loader copy plus the constructor, the sampler thread and the detach handler |
| `late_dump_toets.c` | the Linux positive control on the core (§6) |

### Environment knobs (all optional)
| variable | default | meaning |
|---|---|---|
| `RG_LATE_DUMP` | `1` | `0` disables sampling; the load proof still appears |
| `RG_LATE_DUMP_LOG` | `rg-pe-hook-late-dump.log` | the full sample log |
| `RG_LATE_DUMP_MS` | `250` | interval, minimum 20 |
| `RG_LATE_DUMP_START_MS` | `0` | delay before the first sample |
| `RG_LATE_DUMP_BYTES` | `8` | bytes per target, 1..16 |

---

## 3. The build

### Why not the documented route
**[MEASURED]** The project's own recipe is `podman run --rm … localhost/ravengun-mingw …`
(`scripts/build-hasp.sh:81`). That route **cannot run on this deck**: starting a container is not
allowed here, and independently, **the image does not exist** — the podman store holds exactly one
image, `docker.io/cachyos/cachyos:latest`
(`~/.local/share/containers/storage/overlay-images/images.json`). `build-hasp.sh` therefore skips the
Windows DLL with only a warning. Anyone relying on it is building nothing.

**[MEASURED]** There is also no native cross compiler: a search over `/usr /opt /home /var /srv` for
`*w64-mingw32*` finds no compiler, and there is no `clang`, `zig`, `lld` or `rustc`.

### The substitute that was used
**[MEASURED]** The Arch Linux packages that `Containerfile` would have installed inside the image were
downloaded and extracted into a scratch prefix — **no container started, nothing in the host `/usr`**:

```bash
/home/deck/.cache/ravengun-mingw/mingw-gcc -shared -O2 -o rg-pe-hook.dll \
    rg_pe_hook_dll.c rg_pe_hook.c rg_late_dump.c \
    -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
```

`mingw-gcc` is a small wrapper around
`sysroot/usr/bin/x86_64-w64-mingw32-gcc` with two `-B` flags, because GCC is configured with
`--prefix=/usr` and would otherwise look for `cc1`, headers and libraries on the host. See
**`/home/deck/.cache/ravengun-mingw/BUILD.md`** for the wrapper, the package versions, the archive
sha256s and how to check that a build really produced a new file.

**[MEASURED] The toolchain is provably the right one.** Rebuilding the **unmodified** loader source
with this compiler, using the original output filename, produces a **byte-identical** file:

```
562664483e775c8e37e38d1965d6f8ba10f5d1444ae01b2f6351b43536c459bf   rebuild (pristine source)
562664483e775c8e37e38d1965d6f8ba10f5d1444ae01b2f6351b43536c459bf   deployed baseline
cmp: BYTE-IDENTIEK
```

**[INFERRED]** So the difference between the new DLL and the baseline is attributable **entirely** to
the source change, not to the compiler. Note the toolchain's GCC is 16.2.0 — the same version family
as the `GCC: (GNU) 16.2.0` string inside the deployed DLL.

**Note for whoever builds next:** the output filename is embedded in the DLL's export directory. Building
to a different name changes bytes in `.edata` (and the PE checksum) even from identical source. Always
compare against the same filename.

---

## 4. sha256 before → after

| file | sha256 | size |
|---|---|---|
| `dropin/rg-pe-hook.dll` — **baseline, still in place, untouched** | `562664483e775c8e37e38d1965d6f8ba10f5d1444ae01b2f6351b43536c459bf` | 134 437 |
| `rg-pe-hook.dll` — **new instrument, built, NOT deployed** | `9caa11c6505918f13aa8fe9656257fb38a9a8a92c3d3156791792f2ec1c7d532` | 147 105 |

Other `dropin/` files, unchanged and unread-by-me-except-for-reference:
`dongle-haken.ini` `23ecb63c39c6d761fb6f5c242d98b2a84771502961993f006725ae8b90d22932`,
`dropin.ini` `169110a990209a43a78053a6510d3a77515dbb93a04c1ef4df86147367e3d4ee`.

**[MEASURED] Collision check.** `patches/LuigisMansion/dropin/` was re-checked at
`2026-10-13T16:38:57+02:00`; all three mtimes are still 09:42–09:51 and all three sha256 are exactly
the values recorded at the start of this work. The directory was **not** touched by anyone during the
session, and I did not write into it.

Source files in the build tree:
| file | sha256 |
|---|---|
| `rg_pe_hook_dll.c` (changed) | `6bd46abcb7d7a4170f73c23ad83b0c6be7801e194fd4d67270a657d6e64c01cb` |
| `rg_late_dump.c` (new) | `c7b8ade47673c100051a217dddaaad298f8a3cac4af8f75a65926527091e441d` |
| `rg_late_dump.h` (new) | `f505c1c7e7c184c4d3f1e22e61a64e87f73bff583d152c2731ea51effda1797e` |
| `late_dump_toets.c` (new) | `d239c8c1bc5d7c729bf7f9132e2b149818c35a17c3d5ca0c8f3b0d3d239024fe` |

Scratch location of everything: **`/tmp/b0-latedump/artefact/`**
(the DLL there is named `rg-pe-hook-late-dump.dll` so it can never be confused with the deployed one).

---

## 5. The exact log lines to grep

All new lines carry the prefix **`[rg-pe-hook.late-dump] `**, so no existing line or its format changes.
Existing greps are unaffected: the new text contains none of `wachtroute`, `aangehaakt`, `afgekeurd`,
`totaal`.

Two files are written:
* `rg-pe-hook.log` (the existing log; via `RG_HOOK_LOG`) — gets the **load proof** and each
  **summary** at a state change. Compact.
* `rg-pe-hook-late-dump.log` (new; via `RG_LATE_DUMP_LOG`) — the **full per-sample detail**.

| what you want to know | command |
|---|---|
| **"did the DLL load at all?"** (can no longer be answered with silence) | `grep LAADBEWIJS rg-pe-hook.log` |
| **the one-grep answer** — did each target ever become non-zero, and at which sample | `grep 'SAMENVATTING-DOELEN' rg-pe-hook-late-dump.log \| tail -1` |
| the moment each target materialised | `grep EERSTE-NIET-NUL rg-pe-hook-late-dump.log` |
| the counts per sample | `grep 'SAMENVATTING monster' rg-pe-hook-late-dump.log` |
| the full time series of one target | `grep 'DOEL .* amDongleInit ' rg-pe-hook-late-dump.log` |
| every sample's header | `grep 'MONSTER ' rg-pe-hook-late-dump.log` |
| targets that never materialised | `grep -o '[A-Za-z]*=NOOIT' rg-pe-hook-late-dump.log \| sort -u` |
| a quick count of samples taken | `grep -c 'MONSTER ' rg-pe-hook-late-dump.log` |

Line formats (verbatim, `VORM` = `NUL` | `CC` | `E9` | `DATA` | `BUITEN`):

```
[rg-pe-hook.late-dump] LAADBEWIJS constructors-regel: de DLL is GELADEN en uitgevoerd. pid=… tid=… hoofdmodule=… pad=… t=+…ms wandklok=…
[rg-pe-hook.late-dump] DOELEN: 17 regel(s) uit …; bemonsterd=17 niet-bemonsterd=0
[rg-pe-hook.late-dump] MONSTER 0007 t=+1.750s ms=1750 wandklok=… bytes=8
[rg-pe-hook.late-dump]   DOEL 05/17 amDongleInit                         rva=0x2b4170 lees=… = 40 53 48 83 ec 00 00 00 : DATA
[rg-pe-hook.late-dump] EERSTE-NIET-NUL amDongleInit rva=0x2b4170 monster=0007 t=+1.750s : 40 53 48 83 ec 00 00 00 (DATA)
[rg-pe-hook.late-dump] SAMENVATTING monster=0007 t=+1.750s nul=13/17 niet-nul=4 ooit-niet-nul=4/17 buiten-beeld=0 niet-bemonsterd=0
[rg-pe-hook.late-dump] SAMENVATTING-DOELEN amDongleBillingGetKeychipId=NOOIT | amDongleGetGameId=NOOIT | … | amDongleInit=#7 | …
[rg-pe-hook.late-dump] EINDE DLL_PROCESS_DETACH t=+…ms monsters=…
```

**`VORM` means, exactly:** `NUL` = all N sampled bytes are zero (the question "is it still all-zero");
`CC` = all N bytes are `0xcc` (int3 padding — **non-zero, but not a function**); `E9` = first byte is
`0xe9`, i.e. already detoured by some other core; `DATA` = anything else. **`DATA` is deliberately not
the same as "a valid prologue"** — a snapshot mid-unpack looks like `DATA` too. The instrument reports
bytes; the engine's own validation decides validity, and it logs that separately.

**Because the process can be killed hard** (the exit comes from the anti-tamper VM, see
`EXITBESLUIT.md`), `DLL_PROCESS_DETACH` may never run. Therefore the summary is rewritten on **every
sample**: the last `SAMENVATTING-DOELEN` line on disk is always the final state, whether the process
exited cleanly or was terminated.

---

## 6. How the instrument was proven to work without a launch

An instrument that reports "nothing seen" is worthless unless it can also see something. The sampler
core has no OS calls, so the **same code** that runs in the Windows DLL was compiled with the native
`gcc` and run against a synthetic image in which targets go from zero to non-zero on cue.

```
gcc -O2 -Wall -Wextra -o late_dump_toets late_dump_toets.c rg_late_dump.c
./late_dump_toets
```

**[MEASURED] Result: 37 PASS, 0 FAIL, `ALLE TOETSEN GESLAAGD`.** The test proves, among others:
* a target that goes non-zero is reported at the **correct sample number** (`doelA=#2`);
* non-zero that is **not** in the first byte is still detected (`00 00 00 40 …` → `DATA`, counted);
* `CC`, `E9`, `NUL` and `DATA` are distinguished;
* a target that stays zero is reported `NOOIT` — the negative is produced by an instrument that
  demonstrably produces positives **in the same run**;
* a rule pointing at another module (`module = …`) is **not** sampled and is counted as
  `NIET-BEMONSTERD`, instead of silently reading the wrong address;
* a target outside `SizeOfImage` is counted and reported without crashing.

Raw output saved as `/tmp/b0-latedump/artefact/toets-uitvoer.txt`.

### The constructor really is wired
**[MEASURED]** A `__attribute__((constructor))` is only worth something if the CRT calls it. This was
checked in the object file and in the built DLL, not by grepping a section name:
* the object has a `.ctors` section with one relocation pointing at `rg_constructor_laadbweis`;
* in the DLL, `.ctors[0]` **contains the value `0x0000000180007150`**, which `nm` reports as exactly
  `rg_constructor_laadbweis`;
* the call chain is present: `_CRT_INIT` calls `__main`, `__main` jumps to `__do_global_ctors`,
  `__do_global_ctors` reads `__CTOR_LIST__` and does `call *(%rbx)` on each entry;
* in `DllMainCRTStartup` the `DLL_PROCESS_ATTACH` branch (`cmp $0x1,%edx`) calls `_CRT_INIT`
  **before** `call DllMain`.

**[INFERRED]** The load proof therefore fires at load time, before the existing hooking code runs. As
a second line of defence `DllMain` also calls the same writer if the flag is not yet set, so the load
proof cannot be lost even if the constructor mechanism ever changes.

---

## 7. Rollback

**Nothing needs rolling back: the deployed files were never replaced.** `dropin/rg-pe-hook.dll` is
still the baseline (`sha256sum` → `562664483e775c8e37e38d1965d6f8ba10f5d1444ae01b2f6351b43536c459bf`).

I deliberately did **not** create a backup folder inside `dropin/`: I was instructed not to write there,
and a backup would be redundant because (a) nothing was overwritten and (b) the baseline is **byte
reproducible** from the loader source with the documented command. That is a stronger guarantee than a
copy, and it is verifiable:

```bash
# is the deployed file still the untouched baseline?
sha256sum ~/projects/ravengun-patches/patches/LuigisMansion/dropin/rg-pe-hook.dll
# expect: 562664483e775c8e37e38d1965d6f8ba10f5d1444ae01b2f6351b43536c459bf

# and it can be regenerated from the loader source at any time:
cd /tmp/b0-latedump/src-orig && \
/home/deck/.cache/ravengun-mingw/mingw-gcc -shared -O2 -o rg-pe-hook.dll \
    rg_pe_hook_dll.c rg_pe_hook.c \
    -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
```

**If someone does deploy the instrument**, the rollback is one copy of the baseline back into place —
and the two commands above prove it worked:

```bash
cp -a /tmp/b0-latedump/artefact/rg-pe-hook.dll \
      /tmp/b0-latedump/backup-baseline-rg-pe-hook.dll   # take this BEFORE deploying
# ... deploy, run, then:
cp -a /tmp/b0-latedump/backup-baseline-rg-pe-hook.dll \
      ~/projects/ravengun-patches/patches/LuigisMansion/dropin/rg-pe-hook.dll
sha256sum ~/projects/ravengun-patches/patches/LuigisMansion/dropin/rg-pe-hook.dll
```

`dongle-haken.ini` and `dropin.ini` need no change at all for this instrument: it reads
`RG_HOOK_INI` (already set by `dropin/dropin.ini`) and derives its own log path from the working
directory. **If** a later lane wants the sample log inside the view, it must add
`RG_LATE_DUMP_LOG=…` to the existing `Env =` line of `[rg-pe-hook.dll]` — that is the only
configuration change the instrument can ever need.

---

## 8. What could NOT be verified without a launch — explicitly

Everything below is **[INFERRED]** at best and none of it is measured. A launch is needed for all of it,
and by the brief that run is scheduled by someone else.

1. **That the DLL actually loads and the load proof actually appears.** Statically the constructor is
   wired (§6), and the same DLL channel is measured working in `logs-exittrace-r7/launch.log` — but no
   line of this instrument has ever been executed inside `VACUUM.exe`. This is the single most
   important gap, and it is exactly the gap the load proof exists to close on the next run.
2. **Whether any of the 17 ranges ever becomes non-zero, and at which sample.** That is the question
   the instrument was built to answer. It is unanswered until a run happens.
3. **Whether the 250 ms poll is fast enough.** The measured window under our core is t≈4.7 s (unpacked)
   to t≈5.96 s (death) — about 1.3 s, i.e. ~5 samples at 250 ms. If the ranges materialise and vanish
   faster than that the instrument would show nothing; `RG_LATE_DUMP_MS=50` lowers that risk. The
   *default* is deliberately the conservative 250 ms.
4. **Two writers on one log file.** The existing engine thread and the sampler thread each hold their
   own `FILE *` opened with `fopen(…, "a")` (O_APPEND), and each writes a line with one `fprintf`
   followed by `fflush`. That should keep lines intact, but **line-atomicity of interleaved appends
   under Wine/Proton is not measured here.** The full detail goes to the separate late-dump log,
   precisely so a corrupted interleave in the main log cannot lose the measurement.
5. **Whether the sampling thread perturbs the game.** It only sleeps and reads, but a real run is the
   only way to see the effect, and a baseline A/B (drop-in with `RG_LATE_DUMP=0` versus `1`) is the way
   to check it. `RG_LATE_DUMP=0` exists for exactly that comparison and leaves the load proof in place.
6. **Whether the `DLL_PROCESS_DETACH` summary is safe.** It writes to an already-open file under the
   loader lock. That is why the summary is also rewritten on every sample, so a hard kill loses nothing.
7. **Whether tearing matters.** The sampler reads 8 bytes with `memcpy` while the unpacker may be
   writing the same range; a sample can therefore catch a half-written value. That is a property of
   the observation, not a fault; consecutive samples make it visible.
8. **Whether the game gets past t≈6 s.** Not this instrument's question, but it is the complementary
   measurement that decides whether the addresses were ever reachable in a live run.

### Behaviour changes considered and deliberately NOT made
Per the brief these were left alone and are recorded instead:

* **`bytes = …` guards on the 17 lines** (`BESCHERMINGSLAAG.md` §5.2). This is exactly what the late
  dump is meant to *supply data for* — but adding a guard changes what the hook accepts and rejects,
  so it must not be added while a baseline comparison is pending. The instrument's job is to produce
  the bytes; deciding to enforce them is a separate step.
* **Raising the wait-route resolution** (`wacht_ms`, currently 50 ms). It changes hooking behaviour and
  the timing of any future conversion, so it was not touched. If the late dump shows the window is
  narrower than 50 ms, that is a finding to report, not a change to smuggle in.
* **Merging the sampler into the engine.** It would have been fewer files, but it would have changed
  `rg_pe_hook.c`, and keeping the engine byte-identical is worth more than the tidiness.

---

## 9. Reproducing all of it

```bash
# 1. the toolchain (already in place, self-contained, no container)
ls /home/deck/.cache/ravengun-mingw/          # BUILD.md, mingw-gcc, sysroot/, pkgs/

# 2. the sources (scratch)
ls /tmp/b0-latedump/src/                     # rg_pe_hook.c/.h (byte-identical to the loader)
                                             # rg_pe_hook_dll.c (changed), rg_late_dump.c/.h (new)

# 3. prove the engine is unchanged
cmp /tmp/b0-latedump/src/rg_pe_hook.c ~/projects/ravengun/tools/rt-hasphook/rg_pe_hook.c
cmp /tmp/b0-latedump/src/rg_pe_hook.h ~/projects/ravengun/tools/rt-hasphook/rg_pe_hook.h

# 4. prove the toolchain reproduces the baseline byte-for-byte
bash /home/deck/b0-build-same.sh

# 5. the positive control on the sampler core
bash /home/deck/b0-run-toets.sh

# 6. the full verification suite (imports, sections, strings, ctor wiring)
bash /home/deck/b0-verify-all.sh
bash /home/deck/b0-verify-ctor2.sh
bash /home/deck/b0-verify-ctor3.sh
```
