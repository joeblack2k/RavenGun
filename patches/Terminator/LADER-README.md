# Terminator Salvation — per-game loader dossier (32-bit i386 Linux ELF)

**Subject:** `/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game`
(folder name ends in a **space**), Raw Thrills 2010, `game` = 7,764,284 bytes.
**Profile:** `GameProfiles/Terminator.xml` → `EmulatorType: ElfLdr2`, `EmulationProfile: RawThrillsGUN`,
`ExecutableName: game`, 32-bit.

**Everything below was measured read-only, on the Deck (`deck@192.168.2.247`), against the live NAS
mount — not against a mirror.** Nothing was started; no game process was created; nothing was written
to the NAS. Where a number is not a direct reading it is marked **[inferred]**.

Reproduce with the two scripts next to this file: `classify-elf.py` (class) and
`find-dongle-symbols.py` (dongle symbol map). The commands for every number are in the sections.

---

## 1. Class: **UNPACKED** (not HASPE)

Verdict: this is a plain glibc i386 executable with a complete import topology. Every single marker of
the HASPE-packed class is absent. This is the *easiest* class in the family.

`readelf -h game`:

```
Class:                             ELF32
Type:                              EXEC (Executable file)     <- ET_EXEC, non-PIE, fixed load address
Machine:                           Intel 80386
Entry point address:               0x8057240
Start of section headers:          7763124
Number of section headers:         29
```

| # | packed-class marker | what packed games show | **Terminator** |
|---|---|---|---|
| 1 | section headers (`SHNUM`) | **0** (GalagaAssault, JurassicPark, PacmanChomp, Halo) | **29 sections present** — full `.plt`, `.got`, `.got.plt`, `.dynsym`, `.dynstr`, `.rel.dyn`, `.rel.plt`, `.init`, `.text` |
| 2 | PLT stub form `ff 25 <got32> 68 ef be ad de` | 357 / 577 / 442 such stubs | **0** such stubs |
| 3 | raw `0xdeadbeef` anywhere in the file | present (hundreds) | **0 occurrences** |
| 4 | GOT prefilled in the *file* with `0xb7xxxxxx` (foreign absolute addresses) | 356 × `0xb7…` / 576 × `0xb7…` / 441 × `0xb7…` | **0 of 496** JMP_SLOT slots hold `0xb7xxxxxx`; **496 of 496** hold their textbook lazy value `0x08xxxxxx` (= PLT stub + 6) |
| 5 | `DT_HASH.nchain` lowered against the real symbol count | −258 / −472 / −248, as a contiguous tail block of fully-zero `dynsym` records | **no `DT_HASH` at all** — see below |
| 6 | erased import names | 258 of 357 / 472 of 577 destroyed | **0 of 497 destroyed** |
| 7 | packer signature string | `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` | `strings -a game \| grep -c HASPE` → **0** |

### 1.1 PLT stubs: **496**

- `.plt` = 0x08055328, size **0x1f10 = 7952 = 497 × 16** → 1 resolver stub + **496** import stubs.
- `DT_JMPREL` = 0x08054374, `DT_PLTRELSZ` = 0xf80 = 3968 → 3968 / 8 = **496** `R_386_JMP_SLOT`.
- A byte scan for `ff 25` finds 576 hits; the longest contiguous 16-byte-spaced chain is **496 stubs**
  at file 0xd338..0xf228. The 80 extra `ff 25` hits are *inside `.text`* (ordinary instructions, e.g.
  `0xc2928`, `0xf3b69`, `0x14c230`), not stubs — an artefact of scanning code bytes, not PLT entries.
- Of those 496 stubs, **0** push `0xdeadbeef`.

### 1.2 Import names: **497 named, 1 unnamed, 0 erased**

- `dynsym` = **1020** entries, and `(DT_STRTAB − DT_SYMTAB) / 16 = (0x0804d3a0 − 0x080493e0) / 16`
  `= 0x3fc0 / 16 = 1020` — consistent, so the whole table is accounted for.
- This arithmetic is *exact* here because `.dynsym` (file 0x13e0, size 0x3fc0) ends at file **0x53a0**,
  which is byte-for-byte where `.dynstr` starts (file 0x53a0). No gap, no hidden records.
- 522 **defined** + 498 **undefined** = 1020. Of the 498 undefined, **497 carry a real name** and
  exactly **1** is unnamed — and it is symbol index 0, the ELF-mandated null symbol.
- There is **no contiguous tail block of fully-zero dynsym records**: 0 defined symbols with an empty
  name, 1 undefined with an empty name (index 0). The packed games instead carry 258/472/248 such
  records in a block at the end of `.dynsym`, invisible to `ld.so` because `DT_HASH.nchain` was lowered.

**Therefore the hard ceiling that limits the packed games does not apply here.** For GalagaAssault and
JurassicPark the honest statement is "258 of 357 / 472 of 577 import names exist nowhere in the file,
so no loader can bind them without ElfLdr2's runtime table". For Terminator the corresponding number is
**0 of 497**: every import has a name, and every name can be resolved by `dlsym`.

### 1.3 `DT_HASH` is absent — the `nchain` marker is not evaluable

```
$ readelf -d game | grep -E 'HASH'
 0x6ffffef5 (GNU_HASH)                   0x8048168
$ readelf -SW game | grep -E '\.hash|\.gnu\.hash'
  [ 3] .gnu.hash   GNU_HASH   08048168  000168  001278  04   A  4  0  4
```

There is **no `DT_HASH` and no `.hash` section**, only `DT_GNU_HASH` / `.gnu.hash` (0x1278 bytes).
So the packed-class marker "lowered `nchain`" cannot be computed at all; the equivalent test — a
contiguous erased-name block — was done instead (§1.2) and comes back clean.

**This is not a footnote, it is the single most important loader fact in this dossier. See §3.1.**

### 1.4 The other three executables are unpacked too

| file | class | entry | SHNUM | NEEDED | JMP_SLOT | `0xdeadbeef` |
|---|---|---|---|---|---|---|
| `game` | ELF32 i386 | 0x08057240 | 29 | 13 | 496 | 0 |
| `t4-patch` | ELF32 i386 | 0x0804a540 | 28 | 4 | 156 | 0 |
| `irgun/gunrec_update` | ELF32 i386 | 0x08051130 | 38 | 7 | 80 | 0 |
| `ServiceDongleApp/ServiceDongleApp` | ELF32 i386 | 0x08056c00 | 40 | 13 | 465 | 0 |

`ServiceDongleApp` is **not stripped and carries full DWARF** (`.debug_info` 1.45 MB, `.debug_str`,
`.debug_pubnames`, `.symtab` with 19,295 symbols). That turns out to be the key that unlocks the dongle
layer — see §7.

---

## 2. What our own loader needs for this exact ELF

Full dynamic picture of `game`, as measured:

```
PT_LOAD [0]  off 0x000000   vaddr 0x08048000  filesz 0x54fd7b  memsz 0x54fd7b  R-X
PT_LOAD [1]  off 0x54fd7c   vaddr 0x08598d7c  filesz 0x20ada8  memsz 0x755074  RW-   (bss 0x54a2b0)
PT_DYNAMIC   off 0x54fdc0   vaddr 0x08598dc0  filesz 0x130
PT_INTERP    /lib/ld-linux.so.2
PT_GNU_STACK RWX            PT_TLS: absent     PT_GNU_RELRO: absent
image extent 0x08048000 .. 0x08ceddf0  (12.65 MB of address space)

DT_INIT   0x08055300        DT_FINI 0x084fa438        DT_INIT_ARRAY: absent
DT_SYMTAB 0x080493e0        DT_STRTAB 0x0804d3a0      DT_STRSZ 0x5611
DT_REL    0x0805334c  sz 0x1028 (517 entries)
DT_JMPREL 0x08054374  sz 0x0f80 (496 entries)
DT_PLTGOT 0x085996ec        DT_RPATH "/usr/lib/PhysX/v2.8.1/"
DT_VERSYM + DT_VERNEEDNUM 7 (GLIBC_2.0/2.1/2.2/2.2.4/2.3/2.3.2, GLIBCXX_3.4, CXXABI_1.3)
DT_BIND_NOW absent          DT_FLAGS/DT_FLAGS_1 absent
```

Relocations, by type (all three handled by `rg-elfloader.c`'s `pas_rel_toe()`):

| type | count | note |
|---|---|---|
| `R_386_GLOB_DAT` | 511 | 515 relocations in total reference *defined* symbols — for a non-PIE ET_EXEC the in-file `.got` already holds the correct absolute address, so a successful write is a no-op and a **failed** name lookup leaves the correct value in place |
| `R_386_JMP_SLOT` | 496 | all resolved **eagerly** by name, so the guest's lazy-binding PLT0 resolver is never needed |
| `R_386_COPY` | 6 | `stderr(4)`, `stdout(4)`, `stdin(4)`, `__tzname(8)` from `libc.so.6`; `_ZTVN10__cxxabiv117__class_type_infoE(44)`, `_ZTVN10__cxxabiv120__si_class_type_infoE(44)` from `libstdc++.so.6` |
| `R_386_32`, `PC32`, `RELATIVE`, TLS types | **0** | nothing else to implement; no `PT_TLS`, no `__tls_get_addr` import |

Two undefined symbols have no relocation at all: index 0 (the null symbol) and `_Jv_RegisterClasses`
(the legacy GCJ symbol — harmless, present in countless old binaries).

`e_entry = 0x08057240` is the start of `.text`, so `_start` and the loader's "jump to `e_entry` with a
SysV stack" step line up exactly.

### 2.1 Already covered by `tools/rg-elfloader/` — verified by reading the source

| what the game needs | where it is in `rg_elfloader.c` | verdict |
|---|---|---|
| map 2 `PT_LOAD`s at fixed `p_vaddr` (ET_EXEC) | `map_segments()` L102-183: `fatal("verwacht ET_EXEC …")`, one anonymous `MAP_FIXED` over `ALIGN_DOWN(lo)..ALIGN_UP(hi)`, then `pread` per segment, then `mprotect` per segment | **exactly right for this layout.** `.bss` (0x54a2b0 B beyond `p_filesz`) is zeroed by the anonymous mapping; the unaligned second segment (0x08598d7c) is handled because the whole range is mapped first and only the page-aligned `mprotect` windows are computed |
| `R_386_GLOB_DAT` / `R_386_JMP_SLOT` | `pas_rel_toe()` L501-505, resolves via `dlsym(RTLD_DEFAULT, name)` | covered |
| `R_386_COPY` **from the defining library**, not the shim | `pas_rel_toe()` L514-520 → `zoek_in_libs()` L471-480 (dlsym on the handle returned by `dlopen` of each `DT_NEEDED`) | covered — the project's hard-won fix (source must be the defining lib) applies directly. `stderr/stdout/stdin/__tzname` resolve from the already-loaded glibc, the two vtables from `libstdc++.so.6` |
| resolver stub / first-PLT "packed-class repair" | `bouw_stale_map()` L550-571 + `repareer_eerste_plt()` L599-641 | **safe no-op here.** `bouw_stale_map` only records JMPREL slots whose *in-file* value is `0xb7xxxxxx`; this game has **0** such slots, so `rg_stale_n == 0` and the repair returns immediately at L607 |
| SysV start stack (argc/argv/envp/auxv with AT_PHDR/AT_ENTRY/AT_RANDOM/…) + jump to `e_entry` | `start_spel()` / `spring()` L648-716 | covered. `AT_PHDR` comes from the `PT_PHDR` present at 0x08048034 |
| `DT_INIT` / `INIT_ARRAY` | deliberately not run: `_start → __libc_start_main → __libc_csu_init` does it | correct, and `DT_INIT_ARRAY` is absent anyway |
| version info (`DT_VERSYM`/`DT_VERNEED`) | ignored | benign — resolution is by name and `dlsym` returns the default version |

### 2.2 What must be **added** — two items, one of them a hard blocker

**(A) HARD BLOCKER: the loader derives the guest symbol count only from `DT_HASH`, and this game has
none.** `rg_elfloader.c`:

```c
L190:  uint32_t nchain;   /* aantal dynamische symbolen (uit DT_HASH) */
L205:  case DT_HASH:  d->nchain = ((uint32_t *)dy->d_un.d_ptr)[1]; break;  /* nchain = #symbolen */
L374:  if (!d->nchain)
L375:      fatal("geen DT_HASH: kan het aantal gast-symbolen niet bepalen (shim)");
L404:  for (i = 1; i < d->nchain; i++) { … }        /* the shim is built from this loop */
```

`lees_dyn()` leaves `nchain == 0`, so `maak_shim()` calls `fatal()` and the loader stops **before a
single relocation is applied**. Measured: the loader binary is `ELF 32-bit LSB pie executable, Intel
i386` (correct arch) and was last built 2026-10-12 21:46; the installed copy
`~/.local/share/ravengun/bin/rg-elfloader` is byte-identical (md5 `e25931f0d54d8a141a113bdea7ecf9ee`).

**Fix (small):** when `DT_HASH` is absent, take the count as `(DT_STRTAB − DT_SYMTAB) / 16` — exact for
this file (1020, and `.dynstr` starts precisely where `.dynsym` ends, §1.2) — and assert the result is a
non-zero multiple of 16. A fuller implementation parses `DT_GNU_HASH` (nbuckets / symoffset / bloom /
buckets / chain; the count follows from walking the last bucket's chain to its first zero entry). Either
way this is **~10-25 lines** in `lees_dyn()` plus removing the `fatal()`.

**(B) Latent correctness gap (not a blocker here): `parse_lib_file()` is also `DT_HASH`-only.**

```c
L294:  else if (dy->d_tag == DT_HASH) { … nchain = *(uint32_t *)(m + o + 4); }
L298:  if (symv && strv && nchain && …)   /* else the whole library is skipped */
```

`verzamel_lib_defs()` → `vind_en_parse()` → `parse_lib_file()` is how the loader learns which symbols
the `DT_NEEDED` libraries define (used by `lib_defines()` to keep the "narrow shim"). Measured on this
machine, for Terminator's 13 `DT_NEEDED`:

| library | 32-bit instance used | hash tables |
|---|---|---|
| `libGLU.so.1` | `ElfLdr2/libs/libGLU.so.1` | **DT_HASH** |
| `libtheora.so.0` | `ElfLdr2/libs/libtheora.so.0` | **DT_HASH** |
| `libPhysXLoader.so.1` | `ElfLdr2/libs/libPhysXLoader.so.1` | **DT_HASH** |
| `libogg.so.0` | `ElfLdr2/libs/libogg.so.0` (LD_LIBRARY_PATH wins) | **DT_HASH** |
| `libglut.so.3` | `runtime/lib32/libglut.so.3` | GNU_HASH only |
| `libGL.so.1` | `/usr/lib32/libGL.so.1` | GNU_HASH only |
| `libpthread.so.0`, `librt.so.1`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, `libdl.so.2`, `libstdc++.so.6` | `/usr/lib32/…` | GNU_HASH only |
| all 10 staged `runtime/lib32/*` | | GNU_HASH only |

So the narrow shim is blind to everything libc / libstdc++ / libGL / libglut define. **Measured
consequence for this game: benign.** The intersection of `game`'s 522 guest-defined symbols with the
symbols defined by the 10 GNU_HASH-only libraries is **8**: `stderr`, `stdout`, `stdin`, `__tzname`,
`tzname`, `_IO_stdin_used`, and the two `_ZTVN10__cxxabiv1*__class_type_infoE`. Six of those are the
`R_386_COPY` set, which `maak_shim()` **deliberately keeps** in the shim (L383-399, L416-421);
`_IO_stdin_used` is in `shim_skip()`; that leaves exactly **one** extra exported symbol, `tzname`. So
the only effect is one possibly-preempting absolute symbol — a "verify later" item, not a blocker.
Still worth fixing (same `GNU_HASH` helper as (A), or enumerate via the `dlopen` handle).

### 2.3 The `DT_NEEDED` libraries — where they come from (measured with `ldd`, nothing started)

```
$ LD_LIBRARY_PATH=<ElfLdr2/libs>:<runtime/lib32> ldd "…/Terminator Salvation /game"
    libGL.so.1            => /usr/lib32/libGL.so.1
    libGLU.so.1           => ElfLdr2/libs/libGLU.so.1
    libglut.so.3          => ~/.local/share/ravengun/runtime/lib32/libglut.so.3
    libpthread.so.0       => /usr/lib32/…
    librt.so.1            => /usr/lib32/…
    libtheora.so.0        => ElfLdr2/libs/libtheora.so.0
    libogg.so.0           => ElfLdr2/libs/libogg.so.0
    libPhysXLoader.so.1   => ElfLdr2/libs/libPhysXLoader.so.1
    libm.so.6 … libdl.so.2, libstdc++.so.6, libgcc_s.so.1, libc.so.6  => /usr/lib32/…
    (plus libGLdispatch, libGLX, libX11, libXi, libXxf86vm, libxcb, libXext, libXau, libXdmcp)
```

**Nothing is missing** with both directories on the path. Two things worth recording:

- With only `ElfLdr2/libs` on the path (the state `patches/Terminator/README.md` describes), **only
  `libglut.so.3` is still unresolved** — matching the earlier measurement. It is supplied by
  `runtime/lib32/libglut.so.3`: a genuine **ELF32 i386**, 261,140 B library that exports **all 15**
  `glut*` symbols the guest imports. There is no 32-bit `libglut` in `/usr/lib32` or `ElfLdr2/libs`
  (`/usr/lib/libglut.so.3` is ELF64 — a decoy for 32-bit resolution). No new library has to be built;
  it only has to be in the loader's view.
- The 15 imports that no other library satisfies are exactly the whole `glut*` surface:
  `glutInit`, `glutInitWindowSize`, `glutInitDisplayMode`, `glutInitWindowPosition`, `glutCreateWindow`,
  `glutDisplayFunc`, `glutReshapeFunc`, `glutSetWindow`, `glutSetCursor`, `glutGetModifiers`,
  `glutPostRedisplay`, `glutSwapBuffers`, `glutMainLoop`, `glutEnterGameMode`, `glutGameModeString`.
  Provenance of the other 481 named imports: `libc.so.6` 182, `libGL.so.1` 167, `libGLU.so.1` 35,
  `libPhysXLoader.so.1` 29, `libtheora.so.0` 23, `libm.so.6` 18, `libogg.so.0` 16, `libgcc_s.so.1` 7,
  `libstdc++.so.6` 5.
- `DT_RPATH = /usr/lib/PhysX/v2.8.1/` **does not exist on this Deck** (measured). Harmless as long as
  `LD_LIBRARY_PATH` supplies `libPhysXLoader.so.1` from `ElfLdr2/libs`, which it does.
- `PT_GNU_STACK` is **RWX** — the game asks for an executable stack. The shim is already built with
  `-Wl,-z,execstack`, so nothing to do; noted because it is a property of the binary.

### 2.4 Reusable plumbing that already exists

- `scripts/play-native.sh` already knows how to run a game through our own loader:
  `--loader`, or `"loader": "rg-elfloader"` in `packaging/native-games.json`; it bubblewraps and
  `--ro-bind`s the loader binary to `/rg/bin/rg-elfloader` (L267-274).
- `packaging/native-games.json` already has a `Terminator` entry with `arch: i386`, the NAS mount, and
  a `libs` list that already names `@lib32/libglut.so.3` and `@tpui/ElfLdr2/libs/libPhysXLoader.so.1`.
  It is currently `"enabled": false, "status": "geparkeerd"` and has **no `"loader"` key**, i.e. it
  takes the kernel `ld.so` route, not ours.
- `~/.local/share/ravengun/runtime/views/Terminator/` already exists (symlink farm onto the NAS, with
  the trailing space in the target). The trailing space matters: Wine reports
  `Directory name invalid (0x10B)`; the view is how the project works around it.

---

## 3. The plan, in numbered steps

1. **Add the `GNU_HASH` / `STRTAB−SYMTAB` symbol count to `rg-elfloader`.** This is the one thing that
   currently stops the loader dead. Gate: `RG_ELFLOADER_VERBOSE=1` reports
   `shim geladen: N gast-symbolen` (N in the low hundreds, after `shim_skip`/`lib_defines` filtering)
   instead of `FATAAL: geen DT_HASH`.
2. **Put `libglut.so.3` and the `ElfLdr2` libraries in the loader's view.** Both already exist on disk;
   this is a `--ro-bind`/`LD_LIBRARY_PATH` wiring step in the `play-native.sh` loader route and in the
   `Terminator` entry of `packaging/native-games.json` (add `"loader": "rg-elfloader"`).
3. **Provide the `/g3/` root the engine expects** (see §7.3) and **verify the loader reaches `main`**.
   Gate: the loader prints `spring naar e_entry 0x8057240` and the guest gets as far as its own
   `main`, then stops on the dongle (step 5) — not on a missing file or a crash.
4. **(Optional, correctness)** give `parse_lib_file()` the same `GNU_HASH` enumeration, so
   `lib_defines()` also sees libc/libstdc++/libGL/libglut. Measured impact for *this* game: one extra
   symbol (`tzname`). Do it after step 3 proves the path works.
5. **Reach and classify the dongle wall.** Expect `DongleInitialize() FAILED!` and `exit 255`, the same
   wall the project already measured for the native route — and the same one TeknoParrot's closed core
   handles. Do **not** treat this as a crash: it is the designed failure path.
6. **Stand in for the dongle at the engine level**, using the symbol map in §7.5: patch the prologues of
   `DongleIsPresent`, `DongleFeatureLogin`, `DongleRead`/`DongleRead*` (and `DongleGetID`) in `game` to
   shim trampolines that return the values the engine wants, sourcing them from the engine's own pass /
   seed material and — if it turns out to be a per-question answer store — the `hasp/` directory
   (§7.4). Start at the `Dongle*`/`hasp_*` boundary; do **not** start inside the obfuscated `hasp_*`
   internals (`sFnwoyYfK0cG7l6`, `THpCq4ovSzu74a4`, `__goiberuam01`, `__wurschtsemmel00` — the Aladdin
   runtime is symbol-obfuscated even in the unstripped binary).
7. **Only if step 6 is not enough:** stand in for the HASP **License Manager** the runtime hunts for
   (`<license_manager hostname="localhost">`, `nethasp.ini`, `HL_LICENSEDIR` +
   `hlrus_license_file.alf`). That is a bigger, network-protocol-shaped job; keep it as the fallback.
8. **I/O: nothing to build.** `RawThrillsGUN` is already a productised dialect — see §6.

Estimated effort is in §8.

---

## 4. The exact next measurement

The bench takes one game at a time and I must not start it, so the next measurement is deliberately
**static — it runs no guest code and cannot bring up a window.**

`find-dongle-symbols.py` (next to this file) builds the dongle/HASP symbol map for the stripped `game`
out of the unstripped, DWARF-bearing `ServiceDongleApp` from the same folder. Two evidence methods, and
**every** hit prints the evidence it rests on plus a confidence grade:

- **[BYTES]** a code block at (or just inside) the SDA function entry matches `game`. The mismatch ratio
  is measured over the **non-address** bytes only — absolute addresses and jump targets move between the
  two builds and are wildcarded, everything else must agree.
- **[STRING]** a string referenced inside SDA's function F is referenced in `game` too. The owning SDA
  function of that xref is looked up in the symbol table and the instruction block around the two xrefs
  is compared. On a match the entry projects as `game_entry = game_xref − (sda_xref − sda_entry)`.

Grades: **A** = 0 differing bytes over ≥ 64 non-address bytes (byte-identical); **B** = ≤ 3 %
differing; **C** = ≤ 6 % differing ("probable, single method"); **UNKNOWN** = no evidence, deliberately
not guessed.

**Command (on the Deck):**

```sh
mkdir -p ~/projects/ravengun-patches/patches/Terminator
cd ~/projects/ravengun-patches/patches/Terminator
python3 find-dongle-symbols.py
```

**Current result (measured 2026-10-12/13): 43 of 58 named dongle/HASP FUNCs located in `game`.**

| grade | count | what it means | examples |
|---|---|---|---|
| **A** | **24** | byte-identical block (0 differing bytes) — this is proof | `hasp_login` → `game 0x083bbe60`, `hasp_read` → `0x083bad30`, `hasp_write` → `0x083bac80`, `hasp_encrypt` → `0x083bbd60`, `hasp_decrypt` → `0x083bbce0`, `hasp_echo` → `0x083bbc80`, `hasp_get_size` → `0x083babe0`, `hasp_get_sessioninfo` → `0x083bae80`, `hasp_update` → `0x083bb110`, `hasp_login_scope` → `0x083baac0`, `hasp_logout` → `0x083bbe00`, `hasp_detach` → `0x083ba2d0`, `hasp_get_info` → `0x083ba8a0`, `hasp_get_rtc` → `0x083bab60`, `hasp_monster_get_lms_version` → `0x083c3b50`, `haspregnew` → `0x083d4820`, `__hasp_enxml_ex` → `0x083bc0a0`, `__hasp_monster_detach` → `0x083c11e0`, `hasp_legacy_{encrypt→0x083c74b0, decrypt→0x083c7420, set_idletime→0x083c71f0, set_rtc→0x083c7280}`, `hasp_datetime_to_hasptime` → `0x083bade0`, `MemAllocDongle` → `0x0806f6e0` |
| **B** | **12** | ≤ 3 % differing, single method | `DongleOpen` → `0x081c6b70`, `DongleWrite` → `0x081c6550`, `DongleWriteHLFeature` → `0x081c6490`, `DongleWriteHLFeatureInt` → `0x081c6410`, `DongleReadHLFeature` → `0x081c6880`, `DongleReadHLFeatureInt` → `0x081c6800`, `DongleReadCabSerialNum` → `0x081c6700`, `DongleReadCountryCode` → `0x081c6640`, `DongleIsPresent` → `0x081c5ff0`, `DongleGetRand` → `0x081c5ee0`, `DongleEncrypt` → `0x081c5e70`, `DongleDecrypt` → `0x081c5e00` |
| **C** | **7** | 3-6 % differing — **probable, not proven** | `DongleInit` → `0x081c6d20` (5.2 %), `DongleNoise` → `0x080ac230` (5.6 %), `DongleRead` → `0x081c6970` (3.3 %), `DongleClose` → `0x081c6210`, `DongleFeatureLogin` → `0x081c6a50`, `DongleWriteCountryCode` → `0x081c6310`, `DongleWriteCabSerialNum` → `0x081c6390` |

**15 are UNKNOWN, deliberately not guessed** — including **all eight** that were the target of this
round: `DongleInitialize`, `DongleGetID`, `AcquireAladdinDongle`, `ReleaseAladdinDongle`,
`GetAladdinHLPass`, `DongleFeatureCheck`, `ReInitAladdinDongles`, `SysGetDongleHID`. Plus seven that are
structurally unfingerprintable: `hasp_free` (5 B), `hasp_enable_trace` (6 B), `hasp_login_ex` (5 B),
`hasp_hasptime_to_datetime` (5 B), `GetAladdinSRMPass` (5 B), `hasp_get_trace` (15 B) — these are
jump-thunk-sized stubs, too small to have a pattern — and `hasp_cleanup` (29 B).

**Why those eight cannot be anchored here (measured, and this is the honest reason).** They form a
mutually recursive group whose callers are all inside the same group:

```
SysGetDongleHID   <- DongleInitialize
DongleInitialize  <- (no named caller: it is called from `main`)
AcquireAladdinDongle, ReleaseAladdinDongle, DongleGetID  <- SysGetDongleHID
GetAladdinHLPass  <- DongleInitialize, DongleFeatureCheck
DongleFeatureCheck, ReInitAladdinDongles  <- (no named caller)
```

so call-site alignment cannot bootstrap them, and their bodies genuinely differ between the two builds,
so block matching fails too. Two of them are **provably unanchorable from this source**, because their
own log strings do not exist in `ServiceDongleApp` at all:

```
"ReInitAladdinDongles called: %d times"           in SDA: no   | in game: yes
"Too many consexutive dongle errs (%d), bailing"  in SDA: no   | in game: yes
```

**Correction to an earlier reading in this dossier.** I previously wrote that the xref at
`game 0x080c2cdb` lies inside `game`'s `DongleInitialize`. **That is wrong.** In `ServiceDongleApp` the
string `DongleInitialize() FAILED!` is referenced from inside **`main`** (SDA 0x080be873), so
`game 0x080c2cdb` is inside **`game`'s `main`** — the failure is *reported* by `main`, not by
`DongleInitialize`. Same correction for `version.txt` / `NO VERSION`: those belong to SDA's
`SysLoadVersionInfo`, not to `AcquireAladdinDongle`/`GetAladdinHLPass` (my earlier 3000-byte
disassembly window had overrun into the neighbouring function).

**Measured leads for the UNKNOWN set** (xref addresses are measured; the function *names* are inferred
from the wording of the string and are **not** proven):

| game-only string | game xref address | inferred owner |
|---|---|---|
| `DongleInitialize() FAILED!` | `0x080c2cdb` | `main` (proven — see above) |
| `ReInitAladdinDongles called: %d times` | `0x080aaf10` | `ReInitAladdinDongles` (inferred) |
| `Too many consexutive dongle errs (%d), bailing` | `0x080ac68d` | `DongleFeatureCheck` (inferred) |

`failed to find aladdin dongle`, `failed to read dongle id` and `Unable to read seed from dongle` are in
**`t4-patch`**, not in `game`.

**What counts as success** for this measurement going forward: `DongleInitialize`, `DongleGetID`,
`AcquireAladdinDongle`, `ReleaseAladdinDongle`, `GetAladdinHLPass`, `DongleFeatureCheck`,
`ReInitAladdinDongles`, `SysGetDongleHID` reported with an address **and** evidence. Reaching that needs
a *new* independent anchor, not a looser threshold — relaxing the match ratio would produce exactly the
guesswork the grading is there to prevent. The realistic new anchors are (a) the game-side prologue walk
from the two measured leads above, cross-checked by the call sites in `main`, or (b) a dynamic trace of
`main` during the bench run (see `DRAAIBOEK-BENCH.md`).

**One measurement does need a run, and I am not doing it without your go-ahead:** once step 1 is in, the
loader end-to-end on `game` is the real gate. Success = the loader prints
`shim geladen: N gast-symbolen` and `spring naar e_entry 0x8057240`, and the guest then exits
**255 with its own `DongleInitialize() FAILED!`** — i.e. it reached `.text` and the dongle check,
instead of crashing or dying in the dynamic linker. That run needs the bench (one game at a time) and a
free screen; **ask me for it and I will schedule it, I will not start it unasked.**

---

## 5. I/O side: profile `RawThrillsGUN`

- The profile is **already implemented** in `packaging/tp-shm-profiles.json` → `"RawThrillsGUN"`, a
  64-byte state block (`mirror_bytes` `[0,7]`), used by **19 profiles** (Terminator Salvation,
  Jurassic Park, Pacman Chomp-Mania and others).
- Buttons on offset 8: bits 0-13 = `test`, `service`, `coin1`, `coin2`, `p1.start`, `p1.trigger`,
  `p1.grenade`, `p1.reload`, `p2.start`, `p2.trigger`, `p2.grenade`, `p2.reload`, `volume_up`,
  `volume_down`.
- Aim: **1 byte per axis**, offsets **12 = p1.x, 16 = p1.y, 20 = p2.x, 24 = p2.y**, 0..255, left/top = 0.
  Offscreen rule: the core reads an axis `<= 1` or `>= 254` as off-screen; on-screen values are clamped
  to 2..253 so that 0/255 is reserved for genuinely aiming off-screen.
- What the game expects from the gun board, and what we already have: this is a **Raw Thrills JVS-style
  gun I/O**; the project's bridge is `src/ravengun/iobridge/` (`jvs.cpp`/`jvs_dialect.cpp`, `tp_shm.cpp`,
  `hid_board.cpp`, `routes.cpp`) with the `tp-shm` route as the default and `evdev` via the UI. Both are
  measured working for this game: the JVS bridge connects (`*** GAME CONNECTED ***`), aim tracks the
  raster to ~1 %, trigger and reload work, and a daemon restart mid-game is survived
  (`~/projects/ravengun/findings/Terminator.md` and the NAS `findings/Terminator.md`).
- One caveat that *is* relevant to our own route: per `docs/research/loskoppelen-per-game.md`, the
  game-side behaviour for this dialect lives in the **closed** core — the open OpenParrot source has
  **0 hits** for `RawThrillsGUN` (along with WartranTroopers, FarCry, TimeCrisis5, ALLSHOTDSD,
  InfinityBlade, LuigisMansion, SegaJvsLetsGoIsland). So the profile is ours to keep, but it is a
  *shim-side* description, not something we can read out of an open core.

**Conclusion for the I/O assignment: nothing new has to be built for `RawThrillsGUN`.** The work item is
purely "get the game to its `main` and let it talk JVS", which is exactly steps 1-3.

---

## 6. The dongle situation, stated plainly

This is now an assigned work item, not a stop sign, so here is what is actually there. **Nothing was
started, nothing was emulated, and nothing on the NAS was written.**

### 6.1 Yes, this game wants a dongle — and the client is *inside* `game`

The project's earlier note is correct and I confirm it: **0 `hasp_*` symbols in `game`'s dynamic symbol
table** (1020 dynsym entries, none matching `hasp`/`aladdin`; 0 undefined `hasp_*`; 0 relocations;
no `@@HASPE` export). But the reason is not "there is no HASP client" — it is that **the HASP-HL
runtime and the engine's own dongle wrapper are statically linked into `game`.** Evidence:

- The complete HASP-HL string set is in `game`: `<hasp_info>`, `<haspscope>`, `<v2c>`, `<c2v>`,
  `<hasp type="HASP-HL" />`, `<hasp type="HASP-SL" />`,
  `<license_manager hostname="localhost">`, `<haspid>`, `<haspformat format="keyinfo|sessioninfo|updateinfo|fastupdateinfo"/>`,
  `<haspspec><feature id="%d" /></haspspec>`, `hasp_vtc_data`, `_hasp_version_string`,
  `HASP_ALI_TIMEOUT`, `nethasp.ini`, `hlrus_license_file.alf`, `HL_LICENSEDIR`, `haspregnew`,
  `enter hasp_datetime_to_hasptime: …`.
- Plus the engine's own wrapper strings: `DongleInitialize() FAILED!` (vaddr 0x084feb4e),
  `ReInitAladdinDongles called: %d times` (0x084fd18c),
  `Too many consexutive dongle errs (%d), bailing` (0x084fd210), a bare `dongle`, and the banner
  `Begin HL-RUS License ---` / `End HL-RUS License ---` ("HL-RUS" = the HASP HL Runtime).
- `ElfLdr2/libs` ships **no** HASP library (`ls ElfLdr2/libs | grep -iE 'hasp|aladdin|safenet|sentinel'`
  → none), which is consistent: there is nothing to `LD_PRELOAD`-hook by name.

**The dongle check runs after `main` starts.** All four referencing instructions are in `.text` at
`game 0x0806f6e9`, `0x080aaf10`, `0x080ac68d`, `0x080c2cdb` — all `>= e_entry 0x08057240`, i.e. ordinary
game code, **not** in `.init`/`DT_INIT` (`.init` is only 0x27 bytes, `DT_INIT = 0x08055300`). So the
loader will get through `_start → __libc_start_main → main` and only then meet the wall.

### 6.2 What it asks for, in order (from the unstripped `ServiceDongleApp`)

`DongleInitialize` (608 B) calls, among others: `GetAladdinHLPass`, `DongleInit`, `DongleDecrypt`,
`xSysGetCMOSDateTime`, `SysGetDongleHID`, `StartThread`, `GenerateCryptoKeyFast`,
`DecryptDecompressFileIntoBufferAES_CBC`, and references `gFileSecret`, `gIVSecret`,
`gAladdinDongleHandles`, `gAladdingDongleMutexs`, and starts a **`DongleNoise` thread with 7+ noise
pools**. `AcquireAladdinDongle` additionally does `FsOpen`/`FsRead`/`FsClose` on a `%s/%s` path.

So the request sequence is: _is a key present?_ (`DongleIsPresent` → `<haspformat format="keyinfo"/>` →
`<haspid>`) → _what is its ID?_ (`DongleGetID`, `DongleGetRand`) → _session info_
(`hasp_get_sessioninfo`) → _feature login for feature N_ (`DongleFeatureLogin`,
`<haspspec><feature id="%d" />`) → _read/write the HASP-HL memory_ (`hasp_get_size`, `hasp_read`,
`hasp_write` — the `hhl_mem.dmp` file is **128 bytes**, and `DongleIsPresent` reads a 128-byte
`ftBuffer`) → _encrypt/decrypt/echo_ (`hasp_encrypt`, `hasp_decrypt`, `hasp_echo`,
`DongleEncrypt`/`DongleDecrypt`) → RTC (`hasp_get_rtc`, `hasp_datetime_to_hasptime`,
`hasp_legacy_set_rtc`).

**[inferred]** The material is also used as **key material**: `DongleInitialize` →
`GenerateCryptoKeyFast` + `DecryptDecompressFileIntoBufferAES_CBC` with `gFileSecret`/`gIVSecret` means
the dongle path feeds the AES key/IV used to decrypt game data. If that is right, a dongle shim must
supply *the same* secret the original key produced, or the data files will not decrypt — that is the
part of step 6 to be settled first, and it is measurable, because `GetAladdinHLPass` obtains its input
via `FsOpen`/`FsRead` (a file), not only from the key.

### 6.3 Where it looks

- **A HASP-HL key on the machine** (the `hasp_*` low-level API), and if not:
- **A HASP/Sentinel License Manager on `localhost`** — `<license_manager hostname="localhost">` appears
  in three variants (plain, `+ <hasp type="HASP-HL" />`, `+ <hasp type="HASP-SL" />`), plus a
  network-LM variant. The runtime even has a dedicated LM-discovery module:
  `hasp_monster_get_lms_version`, `__hasp_monster_detach`.
- **A network LM configured by `nethasp.ini`** (the string `nethasp.ini` is at `game 0x0879bec0`, and
  the code around it builds paths).
- **A licence file**: `HL_LICENSEDIR` + `hlrus_license_file.alf` (both strings present in `game`).
- **The engine's own absolute data root `/g3/`** — `game` contains `/g3/` **23 times**, including
  `/g3/ServiceDongleApp/data/conf` roots; `ServiceDongleApp` references `/g3/ServiceDongleApp/data/{conf,cf,hd_pc,snd,shaders,programs,tool2/…}`,
  `/g3/ServiceDongleApp/swatches/data/…` and `/g3/libraries/pmrt/dongle`. **`/g3` does not exist on
  this Deck** (measured). The engine also builds `%s/%s` paths and reads `version.txt`.

**[inferred, with good support]** This is the most likely cause of the measured native failure
`exit 255 / DongleInitialize() FAILED!`: the engine's own files are not at the absolute path it expects,
so the pass/seed lookups under `/g3/…` fail before any HASP key is even consulted. It is also a concrete
requirement for our own loader: **provide `/g3/`** (bind the game folder there, or make the loader do the
prefix mapping). That is step 3 of the plan and it is cheap to test.

Two earlier measurements point at different failure points and both are real, so do not treat them as a
contradiction: `patches/ELF-NATIVE.md` (2026-10-12, `elf-native.py`) reports Terminator stopping at the
**dynamic linker** on `libGLU.so.1` in a bare run, while `findings/Terminator.md` (2026-10-10) reports
that *with the libraries present* it loads and then exits 255 on the dongle. Neither is wrong; they are
two different environments, and the dongle is only reached when the libraries resolve.

### 6.4 The `hasp/` directory: 5413 entries of structured data — but the game never names it

Measured shape of `Terminator Salvation /hasp/`:

- 5,413 entries: **5,410 × 32 bytes**, one **512-byte**, one **256-byte**, plus `hhl_mem.dmp` (**128
  bytes**). No subdirectories. Names: **3,736 × 32 lowercase hex** (16 B) and **1,676 × 64 lowercase
  hex** (32 B).
- **All 1,676 of the 64-hex names have their first 32 characters as an existing 32-hex filename.** This
  is a two-level trie, not random keys. Distinct prefixes collapse fast: 731 distinct at length 4
  (= only 2 bytes!), 1,340 at length 8, 2,295 at 16, 3,736 at 32. A random table of 5,412 16-byte keys
  would have ~5,412 distinct 4-byte prefixes; here there are 731, with one prefix carrying 186 entries.
- Contents: **all 5,412 unique** (zero duplicate MD5s); not `md5(name)`, not the name bytes; the
  256-byte and 512-byte entries are 8 × 32 and 16 × 32 distinct blocks.
- `hhl_mem.dmp` = 128 bytes: data in bytes 0..39, `0xFF` fill, three stray bytes, then a trailer
  `57 42 50 54` ("**WBPT**") + `01 00 00 00` + `10 00 00 00` + `00 00 28 b1` at offset 112.
  (The earlier dossier guessed "WBPT looks like a stamp"; that stands — nothing identifies it further.)

**And here is the important negative result:** `game` contains the literal `hasp/` **0 times** (the 12
`/hasp` matches are `</hasp>`, `</hasp_info>`, `</haspscope>`), and **none** of the 5,412 filenames
appears as a hex token inside `game`, `ServiceDongleApp`, `t4-patch` or `gunrec_update` (`game` and
`ServiceDongleApp` contain **0** hex tokens of length ≥32 at all). So **`hasp/` is not a directory the
game opens by name.** It is dump-author-supplied data sitting next to the game.

**[inferred]** Taken together — a structured 16-byte-key store whose 32-byte values are all unique, a
companion 128-byte HL memory image, and a 256-byte entry of exactly the size of the engine's
`aladdinPassSeedHL` global (with the 512-byte entry matching the two hardcoded 512-byte pass blobs
`DongleInitialize` references) — the most plausible reading is that `hasp/` is a **captured
answer-store plus pass/seed material for this specific key**, i.e. the emulation data someone already
generated from the original dongle. That would make step 6 much cheaper than "write a HASP-HL
emulator". **I have not proven that connection and I am not going to assert it as fact**; §4's
measurement plus reading `GetAladdinHLPass`'s `FsOpen` argument is what would prove or kill it.

### 6.5 The asset that makes this tractable

`ServiceDongleApp/ServiceDongleApp` is **unstripped with full DWARF** and names the whole stack: 72
dongle/HASP symbols, e.g. `DongleInitialize`, `DongleInit`/`Open`/`Close`/`IsPresent`/`Read`/`Write`/
`Encrypt`/`Decrypt`/`GetRand`/`GetID`, `DongleFeatureLogin`/`DongleFeatureCheck`,
`DongleRead|WriteHLFeature(Int)`, `DongleRead|WriteCountryCode`, `DongleRead|WriteCabSerialNum`,
`DongleNoise`, `AcquireAladdinDongle`, `ReleaseAladdinDongle`, `GetAladdinHLPass`, `GetAladdinSRMPass`,
`SysGetDongleHID`, `MemAllocDongle`, `rockey*` (a *different* dongle brand the engine also supports),
and the whole `hasp_*` API; globals `aladdinPassHL` (732 B), `aladdinPassSeedHL` (256 B),
`gAladdinDongleHandles`, `gDongleID`, `gDongleVersion`, `gMasterDongle`, `gDongleSerialStatus`; source
file `dongle.c`; path `/g3/libraries/pmrt/dongle`; dongle kinds `DONGLE_TYPE_ALADDIN`,
`DONGLE_TYPE_ALADDIN_SRM`, `DONGLE_TYPE_FEITIAN`, `DONGLE_TYPE_UNKNOWN`.

Because `game` and `ServiceDongleApp` are the **same PlayMechanix engine**, that symbol table is a
**map for the stripped `game`** — which §4 turns into **43 graded addresses (24 of them byte-identical,
grade A)**, covering the entire `hasp_*` HASP-HL core and most of the `Dongle*` wrapper. The eight
functions that §4 lists as UNKNOWN need a new anchor; the two measured leads for that are in §4.

**Bottom line on the dongle:** it is a real HASP-HL requirement that runs after `main`; the client is
statically linked into `game` (so no symbol-name hook exists, but we now have addresses); the engine's
own data root `/g3/` is missing and is the first thing to fix; and the folder ships 5,413 files of
structured data the game never names, which is the most promising answer source but is not yet proven
to be one. It is **not** established that the dongle is the only blocker.

---

## 7. Honest effort estimate

Assumes one person who already knows this codebase, and that the bench can be booked for the runs.

| step | effort | basis |
|---|---|---|
| 1. `GNU_HASH` / `STRTAB−SYMTAB` symbol count in `rg-elfloader` | **0.5 day** | ~10-25 lines in `lees_dyn()`, plus a build and a static re-run. The highest-value half-day in this dossier. |
| 2. `libglut` + `ElfLdr2` libs in the loader view | **0.5 day** | wiring only; both files already exist and `native-games.json` already names them |
| 3. `/g3/` root + first `main` reach (needs a bench run) | **1-2 days** | the interesting unknown; may also need a path-prefix shim if a bind is not enough |
| 4. `parse_lib_file()` `GNU_HASH` (optional) | **0.5 day** | shares the helper from step 1; measured impact for this game is 1 symbol |
| 5. Run to the dongle wall (needs the bench) | **0.5 day** | mostly scheduling |
| 6. Dongle shim at the engine level, sourced from the pass/seed + `hasp/` | **3-10 days** | wide range because §6.4's central hypothesis is unproven. Cheap end: the pass/seed files are exactly what the engine reads and `hasp/` answers it. Expensive end: the answers are consumed by the obfuscated `hasp_*` internals and must be synthesised per-question. |
| 7. HASP License Manager stand-in (fallback) | **1-3 weeks** | network protocol, only if step 6 fails; keep it as the fallback and get a decision before starting it |
| 8. I/O | **0 days** | already productised; nothing to build |

**Realistic summary:** **~2-3 days to a loader that provably reaches the guest's `main` and stops on its
own dongle check instead of in the dynamic linker** — that is the milestone I would commit to, and steps
1-3 are mostly independent of the dongle. **Then ~1-2 weeks** for the dongle layer, with the honest
caveat that the top end of that range depends on whether `hasp/` really is the answer store. The
"reach `main`" half is small and well understood; the dongle half is real reverse-engineering of a
statically linked, symbol-obfuscated HASP-HL runtime — but with a full DWARF symbol map for the same
engine handed to us in the same folder, which is an unusually good starting position.

---

## 8. Measurement provenance

| fact | how to reproduce |
|---|---|
| class, PLT/GOT/reloc/symbol counts | `python3 classify-elf.py` (this directory) |
| `readelf`/`strings` numbers | `readelf -h -l -d -V -SW game`, `strings -a game \| grep -c HASPE` |
| link state | `LD_LIBRARY_PATH=<ElfLdr2/libs>:<runtime/lib32> ldd "…/Terminator Salvation /game"` |
| no 32-bit `libglut` elsewhere | `find / -xdev -name 'libglut.so*'` + `readelf -h` for `Class` |
| `DT_HASH` per library | `readelf -d <lib> \| grep -E '\((GNU_)?HASH\)'` with an ELF32 check |
| import provenance | `readelf -sW --dyn-syms <lib>` per `DT_NEEDED` |
| `hasp/` shape | `find hasp -type f -printf '%s\n'`, filename-length histogram, MD5 of contents |
| dongle strings / xrefs | `strings -a game \| grep -iE 'hasp\|dongle\|aladdin'`, `objdump -d game` |
| dongle symbol map | `python3 find-dongle-symbols.py` (this directory) |

**Constraints honoured:** NAS read-only throughout; no git commits; **no game process started**; no
dongle, HASP, keychip, integrity or checksum code written or run — §6 is analysis of the binaries and
the data that are already on disk, and it stops at "here is what is asked, here is where it looks, here
is what would be needed".
