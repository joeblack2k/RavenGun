# Halo: Fireteam Raven — what the native route needs, and the loader question

*Per-game loader specialist report. Read-only on the NAS; no game started; no git commit.*
*Date of measurement: 2026-10-12.*

**How this was measured.** The work started on the Windows side against the SMB mirror of the
same NAS (`\\192.168.2.3\emulation` as `Z:`), and was then repeated/confirmed on the Deck via
`re\rc.ps1` / `re\rcput.ps1`. Every file whose contents I report was verified **byte-identical**
between the mirror and the Deck's own view (`sha256sum`, first 16 hex digits):

| file | bytes | sha256[0:16] |
|---|---|---|
| `g7/halo/game` | 33,554,432 | `e88ba3543d5613c8` |
| `g7/halo/game_original` | 31,270,111 | `7f19a90404cdd2fd` |
| `g7/halo/Patch` | 14,230,751 | `9ec994ff1a1b33c2` |
| `g7/halo/halo.so` | 880,528 | `f000a002e74ba378` |
| `g7/halo/utils/rtinit` | 5,419,528 | `bbb6029838930009` |
| `g7/halo/utils/rtinit_original` | 5,415,432 | `0fed9da8b56f58f0` |
| `g7/halo/utils/rtinit.so` | 880,520 | `15c5159e113c69cf` |

Everything marked **[measured]** below is a value read out of those files or out of the Deck.
Everything marked **[inferred]** is my reading of a measurement, not itself a measurement.
Project-side files (scripts, docs, `native-games.json`) were read from the Deck, not from the
stale copies that sit in the Windows workspace under `re\scratch\` and `staging\` — the two
differ (the Windows copy of `packaging/native-games.json` still lists GoGoStrike/Galaga/Pacman;
the Deck's current copy does not).

---

## 1. What the game is, and what route it needs

`game` is a **64-bit Linux ELF** for Raw Thrills' own Linux kiosk OS (`/pm/g7/...`), Raw Thrills
2018, 64 MiB class. Measured on the file itself:

* `ET_EXEC`, `EM_X86_64`, entry **0x414600**, 9 program headers, 2 `PT_LOAD` (both `RWE`, both
  page-aligned to 0x200000), **0 section headers** ("stripped"), `PT_INTERP` =
  `/lib64/ld-linux-x86-64.so.2`. **[measured]**
* The entry point at 0x414600 is a **stock glibc `_start`**, not a Raw Thrills stub: **[measured]**
  ```
  xor ebp,ebp / mov r9,rdx / pop rsi / mov rdx,rsp / and rsp,-16 / push rax / push rsp
  mov r8,0x1137550 / mov rcx,0x11374e0 / mov rdi,0x413e70 / call 0x40e218 / hlt
  ```
  i.e. `__libc_start_main(main = 0x413E70, ...)`. `game_original` has the same code at the same
  virtual addresses and only differs in the entry value (0x1F65240), the `PT_LOAD` file offsets
  (page alignment) and the section table. **[measured]**
* `DT_NEEDED` (13): `libGL.so.1`, `libGLU.so.1`, `libpthread.so.0`, `libdl.so.2`,
  `libudev.so.1`, `liblapack.so.3`, `libiw.so.30`, `libfmod.so.10`, `libfmodstudio.so.10`,
  `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`. **[measured]** — this matches the
  figure in the brief; I re-derived it from `PT_DYNAMIC`, not from section headers.
* On the Deck, **11 of the 13 resolve through `ldconfig`**; only `libfmod.so.10` and
  `libfmodstudio.so.10` are absent. **[measured]**
* No `libftdi*` / `libusb*` in `DT_NEEDED`: both are **statically linked into `game`** (their
  error strings are in the image). **[measured]**

**Route it needs: the kernel's own `ld.so`, not a loader of ours.** See §3.

### 1.1 The dynamic symbol table is not where the real imports are — and that matters

This is the single most useful thing I found, because it *invalidates the premise of the FMOD
question* and it changes how the dongle work must be attached.

* `game`'s **real** dynamic symbol table (`DT_SYMTAB`, reached through `DT_HASH`, because there
  are no section headers) has **220 entries: 182 undefined, 37 defined** (index 0 is the null
  symbol). **[measured]**
* All 182 undefined names are **libc / pthread / socket / dl** (`malloc`, `open`, `ioctl`,
  `pthread_*`, `dlopen`, `dlsym`, `dlvsym`, `dladdr`, `syscall`, …). There is **no** `FMOD_*`,
  **no** `_ZN4FMOD*`, **no** `gl*`, **no** `udev_*`, **no** `hasp_*`. `DT_JMPREL` has 178
  relocations, `DT_RELA` 12, and the highest symbol index any relocation refers to is 219 — so
  the 220-entry table is complete. **[measured]**
* Yet the game is linked against `libGL`, `libfmod`, `libfmodstudio`, `libudev`, `liblapack`,
  `libiw` (that is what `DT_NEEDED` says), and those calls obviously happen.
* The explanation is in the image: at virtual address **0x1F492C8 … ~0x1F54000** there is a
  second name blob — an interleaved list of `symbol-name\0[version-name\0]` pairs,
  **571 symbol names + 474 version tags** **[measured]**:
  * `GLIBC_2.2.5`, `GLIBC_2.3`, `GLIBC_2.3.4`, `LIBUDEV_183`, `GLIBCXX_3.4.x`, … are present as
    versions, and `dlvsym` is imported → the game resolves its own imports **by name at
    runtime**, version tags included.
  * Groups in that blob: **66 GL**, **60 FMOD**, **20 udev**, **13 iw**, ~509 libc/C++/other.
* So the game carries its own import resolver. This is the mechanism behind the earlier note in
  `docs/decisions.md` ("the GOT entry at 0x1691880 stayed 0"): the game asks its own table for
  `_ZNK4FMOD6Studio6System8getEventEPKcPPNS0_16EventDescriptionE`, `dlvsym()`s it, and writes the
  result into a slot near `DT_PLTGOT` (0x1690B58). If the name is not there, the slot stays 0 and
  the call crashes. **[inferred]** from the measured table + the earlier gdb measurement.

### 1.2 FMOD 1.10: **not a startup blocker** — the premise was wrong

* **`halo.so` imports nothing from FMOD.** Its `DT_NEEDED` is exactly `libdl.so.2`, `libc.so.6`,
  `ld-linux-x86-64.so.2`; its 49 undefined symbols are all libc/`dl` (`dlvsym`, `dladdr1`,
  `dlinfo`, `dlsym`, `dlopen`, `mprotect`, `mincore`, `_r_debug`, …). No `libfmod*` anywhere in
  it, and `strings halo.so | grep -i libfmod` is empty. **[measured]**
* The FMOD consumer is `game` itself, through the table from §1.1: **exactly 60 FMOD names**
  (58 `_ZN4FMOD…`/`_ZNK4FMOD…` C++ + `FMOD_Debug_Initialize` and `FMOD_Memory_Initialize`), none
  of them carrying a version tag (so plain `dlsym`, not `dlvsym`). **[measured]**
* `tools/fmod-stub/symbols.txt` on the Deck has exactly 60 names and the set is **identical** to
  the 60 names I extracted independently from `game`:
  `in game but not in stub: 0`, `in stub but not in game: 0`, `identical sets: True`. **[measured]**
* Therefore: **symbol resolution is complete.** The missing FMOD 1.10 is *not* a blocker for
  starting or for playing; it costs **sound**, because the stub is a diagnostic stub that returns
  error codes. That is exactly what the Deck note "geen geluid (FMOD 1.10 ontbreekt)" says, and
  the earlier measurement that the game reaches the attract loop with the stub confirms it.
  **[measured, project note + my set comparison]**
* Real audio needs the real `libfmod.so.10`/`libfmodstudio.so.10` (FMOD 1.10, proprietary;
  redistribution is not permitted) or a much fuller stub. **[inferred]**

### 1.3 The FTDI / gun-board traffic (question 3)

There are **two different board paths** in this game, and they are easy to conflate.

**(a) The guns — statically linked libftdi + libusb.**

* Six call sites pass the vendor/product pair immediately before the call: **[measured]**
  ```
  file 0x9d0ad1  b9 01 60 00 00   mov ecx,0x6001      (product)
                 ba 03 04 00 00   mov edx,0x0403      (vendor)
                 ...              call  (-> vaddr 0xFF9CD0)
  ```
  at file offsets `0x9d0ad1`, `0x9d213b`, `0x9d3b8f`, `0x9d9e52`, `0x9da54a`, `0x9de23e`, plus a
  seventh site at `0x9de1e9`. **All six discovery sites call the same function, at virtual address
  0xFF9CD0.** The argument order (`rdi`=context, `rsi`=device-list, `edx`=vendor, `ecx`=product)
  is libftdi's `ftdi_usb_find_all()` — which the image also names in
  `"cmgr_thread_main():  ftdi_usb_find_all() failed, error %d (%s)"`. **[measured for the
  arguments and the target address; inferred that the target is `ftdi_usb_find_all`]**
* **VID:PID = `0403:6001`** (FT232R). `"0403"`/`"6001"` do **not** occur as text anywhere in the
  file, so this is a code constant, not a string. **[measured]**
* The game requires **exactly one** such chip:
  `"Did not find exactly one FTDI chip (found: %d)"` (file 0xE93E00). **[measured]**
* **Baud rates present as immediates in the same code region: 115200 and 230400.** `115200`
  occurs at file 0x9d9787, 0x9d9fba, 0x9da5d3; `230400` at 0x9dcf96 and 0x9de29f. The image has
  `"Changing baud type: Old baud_type = %d (%d baud)"` → **two baud types**. **[measured for the
  constants and the string; inferred that they are the two baud types]**
* Message families, from the string catalogue (file 0xE92C00–0xE94098): `switch message`,
  `led message`, `status message`, `trig press`, `SAMPLE NUMBER = %d`,
  `RECORDED %d CRC ERRORS` → a **CRC'd packet protocol** with distinct message kinds.
  Resets: `Camera force reset`, `FPGA force reset`, `FTDI force reset`, `USB node force reset`,
  `USB bulk reset request`, `System reset request`, `Boggy gun detected`,
  `Camera stuck bits err`. **[measured]**
* Gun firmware: `/pm/irgun/firmware/prom_%02x.mcs`, `ir_UpdateGunToLatestFirmware`, FTDI EEPROM
  read/write/erase, `setup new eeprom data (serial# %s)`, `ftdi_set_latency_timer`. Gun table:
  `/pm/g7/guninfo.txt` (`guninfo_commit`, `guninfo_update`). **[measured]**
* The libftdi internals are visible as strings (`ftdi_new`, `ftdi_usb_open_desc`,
  `ftdi_usb_get_strings`, `ftdi_read_data`, `ftdi_set_baudrate`, `ftdi_get_eeprom_value`,
  `ftdi_read_eeprom`, `ftdi_write_eeprom`, `ftdi_eeprom_decode`, plus libftdi's own chip table
  `FT232R USB UART / Dual RS232 / FT4232H / Single-RS232-HS` and libusb's whole logging text) —
  i.e. **both libraries are inside the file**. **[measured]**
* **The bytes themselves are not statically recoverable.** The framing is CRC'd and the payloads
  are produced at run time. That is a capture job, not a disassembly job — see §5. **[inferred]**

**(b) The RIO2 board — switches, coin, tickets, LEDs.**

* A separate path with its own code: `rio2_init`, `rio2_dev_init`, `rio2_update`,
  `io_rio2_loop`, `rio2_shutdown`, `rio2_io_config`, `/dev/tty`, and
  `"Conn Wizard … Data Link Failure"` / `"… Serial Link Failure"` → **RION2 is reached over
  serial `/dev/tty` or over Ethernet**, not through the FTDI chip. **[measured]**
* `"Number of RIO2 boards detected: %s"`, `"RIO2 without jumper: %i"`,
  `"Missing RIO2 configuration function!"`. **[measured]**
* The project already bypasses this board: `tools/rt-input-shim/rg_rt_input.c` writes
  start/test/service/coin/volume directly into the game's own switch table
  (`getter 0x61E890`, table `0x180AE80 + 0xAEA0`, stride 0x50 per switch, 0x2200 per board).
  **[measured, project source]**

**(c) The dongle (`fff4`) — my assignment, so here it is in full.**

* The game's HASP client is **statically linked**: 0 undefined `hasp_*` symbols, 0 `hasp`
  PLT relocations, and **0 occurrences of the byte string `hasp_login` anywhere in `game`**.
  The only HASP traces are the Sentinel LDK strings plus the packer signature
  `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` / `HASPE_LINUX_LOADER_1.1` (×2) and `/dev/Hardlock` (×2).
  **[measured]**
* So `LD_PRELOAD` **cannot** intercept it by symbol interposition, and neither can a `dlopen`
  name lookup: `hasp_login` is not a name the game ever looks up. The only way in is to
  **rewrite the call sites / the function entry points in the game's own code** — which is
  exactly what `halo.so` does. **[inferred, and it is the project's own measured conclusion in
  `docs/hasp-layer.md` §0 and §4]**
* `halo.so` is that mechanism and nothing else: it exports the full `hasp_*` API (10 names) plus
  `real_hasp_*`, `flh_inline_hook*`, `plthook_*`, `GOT_FIX_SYM`, `GOT_FIX_VSYM` and an embedded
  Capstone x86 decoder (`cs_*`, `MCInst_*`, `X86_getInstruction`). Its embedded XML is
  `<haspid>748675309</haspid>`, `<hasp id="744811821" type="HASP-HL">`,
  `<hasp4 /> <hasphl /> <sentinelhl /> <nethasptype>0</nethasptype>`. **[measured]**
* The project's dongle data for Halo already matches it: `patches/Halo/dropin/ravengun-hasp/Halo/`
  has `"haspid": 748675309`, `hasphl/newintf/hasp4/aes/sentinelhl`, and three 112-byte memory
  images. **All three images are byte-identical to each other and to the two files that sit in the
  game folder**, `fff4` and `00000000000000001111111111110100`
  (`sha256[0:16] = 3932b3aa7b0d0626`, first bytes `2c 03 ab 63 2a 91 92 3f`). **[measured]**
  So `fff4` = fileid 65524 (read/write), `…10101` = 65525 (read-only), and the game folder's copy
  of the image is the authority. Note the earlier `hasp-layer.md` claim about the *value* of the
  images is confirmed for Halo: the game folder itself carries them, so no capture is needed here.
* The project's own replacements exist and are built (`patches/Halo/dropin/ravengun-hasp-x64.so`,
  `libravengun-hasphook-x64.so`, sources in `hasp/src/` and `tools/rt-hasphook/`), but in the
  **native** route they are **not loaded**: `packaging/native-games.json` sets
  `"preload": "/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so"` — the project's dongle layer is
  missing from that line, so the native route still depends on `halo.so` for the dongle.
  **[measured]**

---

## 2. What already exists in the project

Read from the Deck, so this is current:

| piece | path | state |
|---|---|---|
| native launcher (bubblewrap, NAS read-only, overlay writes) | `scripts/play-native.sh` | works; `--game`, `--headless`, `--seconds`, `--snap`, `--strace`, `--gdb`, `--env`, `--bind-ro`, `--dry-run` |
| per-game recipe | `packaging/native-games.json`, key `Halo` | complete for Halo: mounts `@nas/Halo Fireteam/g7/halo` → `/pm/g7/halo` (overlay), `@nas/…/g7/dump` → `/pm/g7/dump` (ro), substitutes `rtinit_original` and `run_game.sh_original`, `libdirs` = `~/.local/share/ravengun/runtime/lib-fmodstub` → `/rg/libfmod`, `exe` = `/pm/g7/halo/game`, args `-f{res} -n`, `route` = `tp-shm`, `extra_ro` = `/dev/shm/TeknoParrot_JvsState` |
| FMOD stub | `tools/fmod-stub/{fmod_stub.c,symbols.txt,gen.sh}`, built into `runtime/lib-fmodstub/{libfmod,libfmodstudio}.so.10` | 60/60 names, set-identical to what the game asks (§1.2); no sound |
| switch/coin/test shim | `tools/rt-input-shim/rg_rt_input.c`, built to `build/librg_rt_input.so` | proven: a START bit moves the game off the FileTest screen |
| HASP emulator + inline hook | `hasp/src/`, `tools/rt-hasphook/hasp_inline_hook.c`, `scripts/build-hasp.sh` | **built** for x64; hooks the statically linked `hasp_*` entry points by reading their addresses out of the game's own `DT_SYMTAB`/`DT_HASH`; **not wired into the native Halo recipe** |
| Halo dongle data | `patches/Halo/dropin/ravengun-hasp/Halo/{dongle.json,memory/*}` | complete and byte-identical to the game folder |
| Halo dossier | `patches/Halo/{README.md,game.ini,dropin/dropin.ini}` | the ElfLdr2-route measurement history; `Audit = false` fix documented |
| own ELF loader | `tools/rg-elfloader/` (i386, `R_386_*`, proven on GoGoStrike) | **32-bit only — does not apply to Halo** |
| RT-Thrills family docs | `patches/{ELF-NATIVE.md,LOADERS-ZES-GAMES.md}`, `docs/{hasp-layer.md,elfldr2-route.md}` | `LOADERS-ZES-GAMES.md` already records the correct conclusion for Halo: "onze lader is 32-bits en geldt hier dus niet; de native route loopt via de kernel-`ld.so`" |

What does **not** exist: any instrumentation for the FTDI/libusb path (the reason §5 exists), and
a native-route wiring of the project's own dongle layer.

---

## 3. The 64-bit loader question (question 2)

**No own loader is needed. The kernel's `ld.so` is enough, and it is already what the native route
uses.**

* `game` is `ET_EXEC` with `PT_INTERP = /lib64/ld-linux-x86-64.so.2` and a **stock glibc `_start`**
  at its entry point (§1). The kernel maps the two `PT_LOAD` segments, loads the interpreter and
  jumps to `_start`; `__libc_start_main` runs `main` at 0x413E70. Nothing has to be relocated by
  hand, no `DT_NEEDED` has to be reconstructed, no entry point has to be patched. **[measured]**
* `scripts/play-native.sh` does exactly that: `bwrap … --setenv LD_PRELOAD … /pm/g7/halo/game
  -f1080p -n`. There is no loader step. **[measured, script read]**
* Our own loader is **not applicable**: `tools/rg-elfloader/rg_elfloader.c` is i386
  (`set_arch(CS_ARCH_X86, CS_MODE_32)`, `R_386_*`, `-m32` shim stubs) and was proven on a 32-bit
  game. Halo is `ELFCLASS64`. Saying it "does not apply as-is" is right; saying it needs porting
  to become useful is **wrong** — there is nothing for it to do here.
* The only thing the kernel route must satisfy is the **`DT_NEEDED` closure**. For Halo that is
  11 system libraries plus two FMOD libraries that do not exist on the Deck; the recipe supplies
  the latter as `/rg/libfmod` through `LD_LIBRARY_PATH`. Without them the kernel's loader stops
  with `error while loading shared libraries: libfmod.so.10: cannot open shared object file`.
  **[measured project note + my own `ldconfig` check]**
* **One residual item worth one check, and I want to flag it honestly.** `game` contains **24+
  ElfLdr2-style PLT thunks of the shape `ff 25 <got> ; 68 ef be ad de ; e9 <plt0>`** — i.e. a
  dummy relocation index `0xdeadbeef` — at vaddr 0x40D22F… (stride 0x10), and at least two more at
  vaddr 0x1F5CB87 and 0x1F5D0C7. And the file is **not** bound eagerly: `DT_FLAGS = 0`,
  `DT_FLAGS_1 = 0`, no `DT_BIND_NOW`. If any of those thunks were reached before its GOT slot was
  filled, lazy resolution would push a bogus index and go wrong. **[measured]**
  The project nevertheless measured this same `game` running natively through image, its own
  FileTest and into the attract loop. So the thunks are **not** a blocker — but I cannot explain
  from the file alone why they are not, and that is worth one targeted check (§5, check C).
  **[measured present, unexplained]**

---

## 4. Blockers, in order of severity

1. **The FTDI gun path is neither emulated nor measured on the wire.** This is the real blocker
   for "point and shoot natively", and it is the only blocker that is *not* already solved to some
   degree. Statically I now have the library (libftdi+libusb, static), the VID:PID (`0403:6001`),
   the "exactly one chip" requirement, the two baud rates and the message families (§1.3a); what I
   do **not** have is the framing and the payload bytes. That needs a capture with a board
   present. **Severity: high, and currently blocked on hardware.**
   *No FTDI device is attached to the Deck right now* — `lsusb` shows only the hub, a Cooler
   Master ARES, a Dell KB216, a Bluetooth radio, an AURA LED controller, the Steam Controller Puck
   and the **PlayStation Eye (`1415:2000`, i.e. the Move camera)**; there is no `0403:*` device and
   no `/dev/ttyUSB*`. **[measured]** So a byte capture is not possible at this moment.
2. **`halo.so` is an unknown-provenance binary and the native route still depends on it** for the
   HASP dongle (its only real function — it supplies no input and no FMOD). The project already has
   a functional replacement (`hasp/src` + `tools/rt-hasphook` + `patches/Halo/dropin/…`), built,
   with the dongle data verified byte-identical, but **not in the recipe's `preload` line**.
   **Severity: medium** — a wiring change plus one verification run, but it removes the last
   closed binary from the Halo chain, which is the whole point of the project.
3. **No sound.** Not a startup blocker (measured); needs real FMOD 1.10 or a much fuller stub.
   **Severity: medium, partly a licensing question, not mine to decide.**
4. **START does not reproducibly start the game from the attract loop; aim and trigger are
   untested natively.** **[measured project note]** **Severity: medium** — pure measurement work.
5. **The 0xdeadbeef PLT thunks in `game` are unexplained** (§3). Empirically harmless.
   **Severity: low, but cheap to close.**
6. **The ElfLdr2 route is not a fallback for measurement.** In that route the project's inline
   hook cannot be loaded at all: `LD_AUDIT` kills the Cygwin loader, `LD_PRELOAD` kills it too.
   So any dongle or FTDI hooking has to happen in the **native** route, in the game's own process.
   **[measured project note `patches/Halo/README.md` §3]** **Severity: constraint, not a blocker.**

---

## 5. The loader/route plan, in numbered steps

Ordered so that each step is verifiable before the next, and so that the highest-risk item is
de-risked early without needing the owner's screen.

1. **Close the loader question with one zero-run check.** Confirm on the Deck that the `DT_NEEDED`
   closure is the only thing standing between `game` and `ld.so`:
   ```
   cd ~/projects/ravengun
   LD_LIBRARY_PATH="$HOME/.local/share/ravengun/runtime/lib-fmodstub" \
     ldd "$HOME/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game" | grep -i 'not found'
   ```
   Expected: empty output. If it is empty, no own loader is needed and `tools/rg-elfloader` stays
   out of this game permanently. **No game start.**
2. **Wire the project's own dongle layer into the native Halo recipe** and drop `halo.so` from the
   preload line:
   `"preload": "/rg/lib/librg_rt_input.so:<libravengun-hasphook-x64.so>:<ravengun-hasp-x64.so>"`,
   with `RAVENGUN_HASP_DIR`/`RAVENGUN_HASP_GAME` and `RAVENGUN_HASP_ELF_LOG` set. The hook already
   resolves the `hasp_*` entry addresses from the game's own `DT_SYMTAB`/`DT_HASH`, so nothing has
   to be hardcoded. Verify in one run that the game reaches the same place it reaches with
   `halo.so` and that `RAVENGUN_HASP_VERBOSE=1` shows the ten calls being answered.
3. **Close the 0xdeadbeef question** in the same run (check C in §6): with `RD_DEBUG`/`LD_DEBUG=bindings`
   or a breakpoint on the thunk region, confirm no thunk is entered before its GOT slot is filled.
4. **Capture the gun path with the USB tap (`probe/libusb-tap.so`, built and described in §6).**
   Run 1 without a board closes the static questions (which libftdi entry points, in what order,
   with what arguments, where it gives up). Run 2 with the owner's `0403:6001` board attached gives
   the actual bytes. **This is the step that decides whether native Halo ever gets aim and trigger.**
5. **Turn the capture into a device or a shim.** Two options, in order of preference:
   (a) a **libftdi-level shim** inside the game process that answers the gun protocol from the gun
   bridge (same technique as `rt-hasphook`: patch the statically linked entry point), which keeps
   the game's own framing and CRC; or (b) a **virtual FTDI device** at OS level (`usbip`/`gadget`
   or a `uhid`-like approach — note the guns are FTDI *serial/bulk*, not HID, so a real usbfs
   device is the honest answer). (a) is far less work and does not require the kernel to cooperate.
   **[inferred recommendation]**
6. **Only then** revisit sound (decide on real FMOD 1.10 vs. accepting silence) and the
   START reproducibility item.
7. **Do not touch `rtinit`.** The recipe already substitutes `rtinit_original` over `rtinit`
   (the `rtinit` in the folder needs `./rtinit.so` and is the TeknoParrot-patched one); the
   cabinet-type detection is thus bypassed by construction, and `run_game.sh` is likewise served
   from `run_game.sh_original`. **[measured, recipe]** Leave that alone unless the cabinet type
   matters for the gun calibration windows.

---

## 6. The exact next measurement

**Instrument (built, on the Deck, not yet run under the game):**
`~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so` from `probe/libusb-tap.c`
(`gcc -shared -fPIC -O2 -Wall -Wextra -o libusb-tap.so libusb-tap.c -ldl`, exit 0, 25,352 bytes).

It is an `LD_PRELOAD` interposer over `open`/`open64`/`openat`/`close`/`read`/`write`/`ioctl`/`syscall`
that logs every transfer to `/dev/bus/usb/**`, decoding `USBDEVFS_CONTROL`, `USBDEVFS_BULK`,
`USBDEVFS_SUBMITURB` and `USBDEVFS_REAPURB{NDELAY}` (the `usbdevfs_urb` and `usb_ctrlrequest`
structs come from `/usr/include/linux/usbdevice_fs.h` and `linux/usb/ch9.h`, so the field offsets
are the kernel's, not mine). It always calls the real function and returns its result unchanged.

Why this and not an address hook: the six known `ftdi_usb_find_all` sites (vaddr 0xFF9CD0) would
only give the discovery arguments. libusb underneath them does all its I/O through `ioctl` — which
`game` imports **dynamically** — so the tap gets the *whole* byte stream (descriptors with the
VID/PID, setup packets, bulk payloads) **without knowing a single address in the game**. That is
also why it is robust: if my `0xFF9CD0` reading were wrong, the tap still works.

**Command (one bench run; `--headless` keeps the owner's screen free):**
```
cd ~/projects/ravengun
scripts/play-native.sh --game Halo --headless --seconds 90 \
  --bind-ro ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so /rg/lib/libusb-tap.so \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:/rg/lib/libusb-tap.so \
  --env RG_USB_LOG=/tmp/usb-tap.log --env RG_USB_LOG_STDOUT=0 --env RG_USB_RAW=1 --env RG_USB_MAX=50000
```
Notes for whoever runs it: `play-native.sh` applies `--env` **after** it sets `LD_PRELOAD` from the
recipe, so the `--env LD_PRELOAD=…` line above intentionally re-states all three libraries. The
equivalent, less quoting-sensitive alternative is to add the tap to the `preload` field of the
`Halo` entry in `packaging/native-games.json` for the duration of the test.

**What counts as success (run 1, no board):**
* `/tmp/usb-tap.log` contains `open /dev/bus/usb/<bus>/<dev>` lines and at least one
  `ctrl … bRequest=0x06 wValue=0x0100` (GET_DESCRIPTOR) or a `urbsub type=2 … ep=0x81` line, plus
  the game's own `Did not find exactly one FTDI chip (found: 0)` in its output; **and**
* the game still reaches the gun-calibration screen / attract loop it reaches today (so the tap
  changed nothing); **and**
* the log shows **no** `0403:6001` device (there is none attached) — i.e. the tap is honest about
  the absence rather than inventing traffic.
* If the log is empty, the run failed for a different reason (tap not loaded, or `ld.so` refused
  it because the tap needs `GLIBC_2.38` from `__isoc23_strtol` — check
  `LD_PRELOAD=… /bin/ls` first, which currently passes with exit code 0).

**What counts as success (run 2, owner attaches the `0403:6001` gun board):**
* the log contains the device descriptor with `… 03 04 01 60 …` (idVendor `0x0403`, idProduct
  `0x6001`), the configuration/interface selection, a `ftdi_set_baudrate` path (look for the
  `USBDEVFS_CONTROL` with `bRequest=0x03` and a 115200/230400-encoded divisor), and then the
  periodic message exchange; **and**
* the game reports a gun present (the `Gun %d Cal` / `cmgr_thread_conn_read: READ %d PACKETS`
  path) instead of `found: 0`; **and**
* the capture is replayed verbatim from the log — that is the deliverable that makes a replacement
  possible, because it pins the framing, the CRC and the message IDs of `switch/led/status/trig`.

**Checks that need no game at all (can be done now):**
* **Check A** (step 1): the `ldd … | grep 'not found'` line above. Success = empty output.
* **Check B**: `readelf -d <game> | grep NEEDED` → 13 entries, the two FMOD ones supplied by the
  stub directory. Success = 13/13 resolvable with
  `LD_LIBRARY_PATH=…/lib-fmodstub ldd` showing no "not found".
* **Check C** (residual item): confirm the 0xdeadbeef thunks are never entered, e.g.
  `gdb --args` with a breakpoint over vaddr 0x40D22F..0x40D3A8 across a run; success = the
  breakpoint is never hit, or is hit only after the corresponding GOT slot is non-zero.

---

## 7. Honest effort estimate

| piece | estimate | confidence |
|---|---|---|
| Steps 1 + 3 + 4 (checks and run 1) | **half a day**, bench-serialised | high — all instruments exist |
| Step 2 (wire our dongle layer, drop `halo.so`, verify) | **half a day to one day** | medium-high — the hook is built and reads its own addresses; the risk is that the game asks for the dongle in a place the ElfLdr2 route never showed |
| Gun protocol: run 2 capture → replayable log | **one day once the board is attached** | medium — depends on the game actually doing a full exchange without a real gun responding |
| Gun protocol: shim that feeds aim/trigger from the gun bridge | **1–3 weeks** of focused work, and this is where the risk lives | low — the framing is CRC'd and unknown; every previous Raw Thrills gun attempt in this project stopped here |
| Sound (real FMOD 1.10 or a fuller stub) | **2–5 days** for a decision-grade answer; real audio is a licensing question | low/unknown |
| START reproducibility + native aim/trigger verification | **half a day to two days** | medium |

**Bottom line.** The route question is **settled and cheap**: kernel `ld.so`, no loader of ours,
two stub libraries as the only missing dependency, and the FMOD premise turns out to be a
non-blocker. The dongle is **solved in substance** — the game folder's own 112-byte image, the
project's emulator, and the project's inline hook all agree — and only needs wiring and one
verification run; that also lets the unknown-provenance `halo.so` go. What is genuinely open, and
what I would not promise, is the **FTDI gun path**: with the board on the bench it is a few days of
capture and replay, without a board it is **blocked, not merely slow**. Overall: **"native Halo
that plays, with aim and trigger" is 2–4 weeks of part-time work with the hardware, and blocked
indefinitely without a physical `0403:6001` FTDI gun board.**

---

## 8. Files added for this report

All under `~/projects/ravengun-patches/patches/Halo/`:

| file | what it is |
|---|---|
| `HALO-NATIVE-ROUTE.md` | this report |
| `probe/libusb-tap.c`, `probe/libusb-tap.so` | the USB/FTDI capture instrument (built, exit 0) |
| `probe/elf_probe.py` | section-header-free ELF64 probe: `PT_DYNAMIC`, `DT_NEEDED`, `PT_LOAD`, import/export counts |
| `probe/dynstr_probe.py` | dumps `DT_STRTAB`/`DT_HASH` and the relocation→symbol index map |
| `probe/extract_pairs.py` | walks the game's internal `name\0[version\0]` table |
| `probe/fmod_diff.py` | proves the 60 FMOD names are set-identical to `tools/fmod-stub/symbols.txt` |
| `probe/ftdi_args.py` | finds the `0403`/`6001` immediates and the `0xdeadbeef` thunks |
| `probe/ftdi_targets.py` | resolves the six `ftdi_usb_find_all` call sites to vaddr `0xFF9CD0` |
| `probe/needles.py`, `probe/phdrs.py`, `probe/dump_range.py`, `probe/syms.py`, `probe/hasp_probe.py`, `probe/final_checks.py` | the small probes behind each measured claim |
| `probe/deck_recon.sh`, `probe/deck_dongle.sh`, `probe/deck_probe2.sh`, `probe/build_tap.sh` | the Deck-side scripts (run with `bash`, no game started) |

Reproduce the load-bearing numbers with:

```bash
cd ~/projects/ravengun-patches/patches/Halo/probe
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
python3 elf_probe.py "$G"                 # DT_NEEDED, PT_LOAD, import/export counts
python3 ftdi_targets.py                   # the 0xFF9CD0 discovery target
python3 extract_pairs.py "$G/game" 0x1b492c8 0x1b54000   # 60 FMOD names + versions
```
