# EERSTE-PLT-SLOTS — naming the 806 import slots of The Walking Dead's dumped `game`

**Date:** 2026-10-15. **Game:** `game`, 19,165,184 B, sha256 `efbd9b52…2a0006`, ELF32 i386, ET_EXEC at
`0x08048000`, no section headers. **Loader:** our own `tools/rg-elfloader` (`RG_ELF_SLOT_TABLE`).
**Labels:** **MEASURED** = a script output, a file comparison, or disassembly read at an address;
**DERIVED** = a conclusion drawn from measurements.
**The table:** `dropin/eerste-plt-slots.tsv` (813 rows, same columns as Jurassic Park's). **How it was made:**
`probe/` (see its `LEESMIJ.txt`). **Nothing was started**: no game, no bench, no loader run. The table is
therefore static work; it has not yet been exercised the way JP's was (JP §1).

---

## 1. Result

| | slots |
|---|---|
| first-PLT stubs | **813** (`0x8052a20..0x8055ce0`, GOT slots `0x8a2700c..0x8a27cbc`) |
| slots holding a stale cabinet address (`0xb6…`/`0xb7…`) | **806** (803 distinct values) |
| … named | **806** — 0 without a name |
| slots bound by `DT_JMPREL` (value = stub+6) | 6: `fork`, `XDestroyWindow`, `XUngrabPointer`, `XMapWindow`, `XUnmapWindow`, `XGrabPointer` |
| slot holding 0 | 1: `__gmon_start__` (weak) |

**Confidence (column 7):** **37 certain** (31 stale slots whose address the envelope's relocation table also
names + the 6 `DT_JMPREL` slots), **776 high**, **0 medium**, **0 unnamed**. **MEASURED** (`mktable.py`).

**The certain check.** The envelope's `DT_JMPREL` has 106 entries; 34 of them hold a stale cabinet address
(31 of those addresses also sit in the first GOT; `syscall`, `dlvsym`, `getlogin_r` do not). The fit names
all 34 the same as the relocation table: **34 agree, 0 disagree**. **MEASURED** (`anchorcheck.py`).

Every name in the table except the nine `libcaca` ones is exported by a library the native route loads
(column 8). **MEASURED** (`mktable.py`; see §6 for libcaca).

---

## 2. What the binary is

**MEASURED** on the file:

* Same packing as Jurassic Park: 813 first-PLT stubs `ff 25 <slot> 68 <imm> e9 …`; **807 push `0xdeadbeef`**,
  six push a real relocation offset (`0x0, 0x8 … 0x28`: the six `DT_JMPREL` slots above).
* The dynamic symbol table has 320 entries. `DT_JMPREL` (106 entries): six for the first GOT, 100 for a
  **second GOT at `0x924e3f8`** that belongs to the envelope (its names include `comaladdinhlx86envspoony`).
  The string `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` is in the image. `game_ori` (19,160,591 B) is the envelope
  version: entry `0x91b18da`, no usable `.dynamic`.
* `VERNEED`: highest versions asked are `GLIBC_2.17`, `GLIBC_2.15` (libm), `GLIBCXX_3.4`, `OPENSSL_1.0.1`,
  `PULSE_0`, `ALSA_0.9.0rc4`. The image carries `GCC: (Ubuntu 4.8.4-2ubuntu1~14.04.3) 4.8.4`.
* `utils/go.sh` in the game folder has `#start lightdm` and runs `xinit`; `utils/run_game.sh` reads
  `modinfo nvidia`. Both point at an Ubuntu 14.04 cabinet with the NVIDIA driver; the fit below confirms it.

### 2.1 e_entry is `main`, not `_start` — a difference from Jurassic Park

**MEASURED** (disassembly): `e_entry` = `0x8055d70` is `main`: it starts `push %ebp; mov %esp,%ebp`, reads
`8(%ebp)`/`0xc(%ebp)` and calls `0x80801c0`. The real `_start` is at **`0x8055e47`**
(`31 ed 5e 89 e1 83 e4 f0 …`); it pushes `0x8055d70` as main and calls stub `0x8052b40`
(`__libc_start_main`), followed by `hlt` at `0x8055e68`. JP's `e_entry` (`0x813befc`) is a real `_start`.
**DERIVED:** the dump tool rewrote `e_entry` to `main`. A loader that jumps to `e_entry` with the SysV start
stack (`esp` → argc) enters `main` with `argc` = the `argv[0]` pointer and `argv` = `argv[1]`, and
`__libc_start_main` (init array, static constructors) never runs. The loader needs to start at `0x8055e47`
for this image (or call `main` the way ElfLdr2 presumably does). This is a loader decision; nothing was
changed here.

---

## 3. Method: rebuild the cabinet's library layout

As for Jurassic Park: the stale addresses fall in library-sized clusters; each candidate library must put
many stale addresses exactly on its exported functions at one page-aligned base (`align.py`). The 34 named
anchors check the result.

### 3.1 Which Ubuntu, which libc

trusty point releases of glibc are no longer in the archive pool, and `launchpadlibrarian.net` did not answer
from this host (as for JP). What was tried against the 22 libc anchors (`anchorfit.py`, **MEASURED**):

| libc candidate | source | anchors at one page-aligned base |
|---|---|---|
| 2.23-0ubuntu3, 2.23-0ubuntu11.3, 2.27-3ubuntu1 | archive.ubuntu.com | no common base (best 2) |
| 2.19-10ubuntu2, 2.21-0ubuntu4, 2.24-3ubuntu1 | old-releases.ubuntu.com | no common base (best 4) |
| 2.19-0ubuntu6.15 | Docker Hub `i386/ubuntu:14.04` | 4 of 22 at `0xb6dfc000`, then drifts |
| **2.19-0ubuntu6.9** | **Ubuntu 14.04.5 desktop i386 image** (`casper/filesystem.squashfs`, dpkg status) | **22 of 22 at `0xb6dfc000`** (19 by exported symbol, 1 `getpwuid_r` through its compat-version entry, 2 IFUNC implementations `memset`/`memcpy`) |

So the cabinet's userland is the 14.04.5 image (or a build with the same layout). Unlike JP — whose libc was
"near, piecewise" — the Walking Dead libc fits **exactly** at one base, and so does every other library.

### 3.2 libGL

The gap between the end of libGLU (`0xb76ef000`) and libcsv (`0xb775e000`) is only `0x6f000`: too small for a
classic NVIDIA `libGL.so.<ver>` (~1.1 MB), right for a GLVND `libGL.so.1.0.0`. 25 NVIDIA Linux-x86 drivers
(361.42 … 390.157) were unpacked (libGL files only, `.run` never executed) and fitted (`glfit.py`, **MEASURED**):

| libGL file | best base | stale values matched (of 128) |
|---|---|---|
| GLVND `libGL.so.1.0.0` of **375.20, 375.26, 375.39, 378.13** (one identical file, sha256 `35cb0a44…`) | **`0xb76ef000`** | **128** |
| GLVND `libGL.so.1.0.0` of 361.42–370.28, 375.10 | `0xb76ee000`/`0xb76f0000` | 126 |
| GLVND `libGL.so.1.0.0`/`1.7.0` of 375.66–390.157 | `0xb76f2000`–`0xb76f4000` | 58–59 |
| classic `libGL.so.<ver>` (any version) | (overlaps libGLU/libpthread) | ≤ 97 |

**DERIVED:** the cabinet ran NVIDIA **375.20 … 378.13** with the GLVND libGL. The driver version inside that
range cannot be told from libGL alone (the game imports nothing from `libGLX_nvidia`). Note: Jurassic Park's
cabinet ran 346.59 with a classic libGL — a different OS image.

### 3.3 The per-library fit

**MEASURED** (`align.py`, `layout.py`): every library sits at a page-aligned base, and the libraries **tile the
address space exactly in reverse `DT_NEEDED` order** (each ends where the next begins, at most one page apart):

| cabinet base | library | identified as | stale values matched | relocation anchors | table rows |
|---|---|---|---|---|---|
| `0xb6dfc000` | libc | libc6 2.19-0ubuntu6.9 (25 IFUNC rows named via resolvers) | 244 | 22/22 | 244 |
| `0xb6fac000` | libgcc_s | libgcc1 1:4.9.3-0ubuntu4 | 6 | – | 6 |
| `0xb6fc9000` | libm | libc6 2.19-0ubuntu6.9 (3 IFUNC rows) | 36 | – | 36 |
| `0xb700f000` | libstdc++ | libstdc++6 4.8.4-2ubuntu1~14.04.3 | 41 | – | 41 |
| `0xb70f7000` | libcrypto | libssl1.0.0 1.0.1f-1ubuntu2.19 | 60 | – | 60 |
| `0xb72a4000` | libssl | libssl1.0.0 1.0.1f-1ubuntu2.19 | 19 | – | 19 |
| `0xb72fd000` | libpulse-simple | libpulse0 1:4.0-0ubuntu11.1 | 2 (pinned) | – | 2 |
| `0xb7302000` | libpulse | libpulse0 1:4.0-0ubuntu11.1 | 22 | – | 22 |
| `0xb7351000` | libcaca | libcaca0 0.99.beta18-1ubuntu5 | 9 | – | 9 |
| `0xb741d000` | libasound | libasound2 1.0.27.2-3ubuntu7 | 27 | – | 27 |
| `0xb7513000` | libX11 | libx11-6 2:1.6.2-1ubuntu2 | 106 | (+5 JMPREL slots) | 106 |
| `0xb7648000` | libXext | libxext6 2:1.3.2-1ubuntu0.0.14.04.1 | 12 | – | 12 |
| `0xb765b000` | libdl | libc6 2.19-0ubuntu6.9 | 5 | 3/3 | 4 |
| `0xb7660000` | libpthread | libc6 2.19-0ubuntu6.9 | 66 | 9/9 | 67 |
| `0xb767c000` | libGLU | libglu1-mesa 9.0.0-2 | 2 (pinned) | – | 2 |
| `0xb76ef000` | libGL | NVIDIA 375.20–378.13 GLVND `libGL.so.1.0.0` | 128 | – | 128 |
| `0xb775e000` | libcsv | the copy in TeknoParrot's `ElfLdr2/libs` | 1 (pinned) | – | 1 |
| `0xb7761000` | libz | zlib1g 1:1.2.8.dfsg-1ubuntu1 | 20 | – | 20 |

"Stale values matched" counts distinct values including the three envelope-only anchors; "table rows" counts
slots (three pairs of slots share one address, see §5). Plus 6 `DT_JMPREL` rows and 1 weak row = 813.

"Pinned": libpulse-simple, libGLU and libcsv are too small to win a run on their own (1–2 imports). Their base
is the one the layout leaves: libpulse-simple fills `0xb72fd000–0xb7302000` exactly and puts both values on
`pa_simple_free`/`pa_simple_new`; libGLU fills `0xb767c000–0xb76ef000` and puts both on `gluErrorString`/
`gluBuild2DMipmaps` (the same pair JP imports); libcsv fills `0xb775e000–0xb7761000` and its one value lands
on `csv_write2` at the unaligned offset `0x16ee`. **MEASURED.**

**Second opinions that agree** (`callers.py`, direct call references): `_ZNSsD1Ev` 6,895, `_Unwind_Resume`
1,498, `__stack_chk_fail` 909; `__libc_start_main` has exactly one caller, `_start` (`0x8055e63`); the
`dlopen` stub (`0x8053a90`) is called from the routine that loads `libvorbisfile.so.3` (`0x81fa764`).
**MEASURED.**

---

## 4. Confidence per row

* **certain** (37): the relocation table names the same address (31) or the slot is a `DT_JMPREL` slot (6).
* **high** (776): exact offset in the identified build at the library's page-aligned base, inside a layout
  that tiles without gaps; or an IFUNC implementation returned by that build's resolver; or the weak
  `__gmon_start__` slot (value 0, only caller `DT_INIT` at `0x80529e8`, guarded by `test/je` — crti pattern).
* **medium / none**: 0.

---

## 5. Limits, written down

* **Not run.** This table has not been through a loader run. JP's run proved the names that were actually
  called; here not even that. A wrong name would show at its first call.
* **Exact package versions.** The libraries come from one image (14.04.5, August 2016). Later point releases
  (libc 2.19-0ubuntu6.10…6.14, libssl 1.0.1f-1ubuntu2.20+, …) could not be fetched; if one of them has the
  same layout, the cabinet may have run that one instead. The fit is exact either way: same offsets, same names.
* **No -dbg packages.** 28 IFUNC rows (25 libc, 3 libm) are named through the resolver of the exported IFUNC
  (`ifuncmap.py`), not through debug symbols. Where one implementation serves two exported names, both are
  listed in the method column (`index`/`strchr`, `bcmp`/`memcmp`, `stpcpy`/`__stpcpy`, …); they are the same
  function.
* **Shared addresses.** Three pairs of slots hold one stale address each and get one name: `srand`
  (= `srandom`), `htonl` (= `ntohl`), `__pthread_key_create` (= `pthread_key_create`; the game's dynsym has
  `__pthread_key_create` as a weak undefined name, so that alias is used for both). Same function per pair.
* **Aliases.** Where several exported names share an address, the table prefers a name the game's own dynsym
  has, then a name without a leading underscore, then one the host exports. The alternatives are in column 6.
* **libpthread wrappers.** `close`, `fcntl`, `connect`, `recv`, `send`, `sigaction`, … were bound to
  libpthread's copies on the cabinet (the anchors confirm 9 of them). On a glibc ≥ 2.34 host these live in libc;
  `dlsym` by name gives the same function.
* The packages were only read with `readelf`/`objdump`; nothing from them runs, and they are not in this
  repository (`probe/LEESMIJ.txt` lists them).

---

## 6. Other things found on the way

### 6.1 The dongle API

**MEASURED** (strings, xrefs, disassembly):

* The game links the **Sentinel HASP / LDK client statically** (no `hasp_` names in the dynsym, like JP and
  Terminator). The client carries its own trace strings, `enter hasp_login`, `enter hasp_read`,
  `enter hasp_decrypt`, `enter hasp_encrypt`, `enter hasp_write`, `enter hasp_get_rtc`,
  `enter hasp_get_sessioninfo`, `enter hasp_legacy_encrypt/decrypt/set_rtc/set_idletime`, … — which locate
  the API: **`hasp_login` = `0x837bbc0`** (only reference to `"enter hasp_login"` at `0x837bbcd`),
  `hasp_decrypt` = `0x837be38`, `hasp_read` = `0x837c9b8`.
* `hasp_login` has **three** direct callers: `0x81d6410` (feature **`0xffff0000`**), `0x81d64e9` (feature
  **0**), `0x81d686a` (feature from a variable). JP logs into `0xffff0000` too (JP `README.md`), and so does
  Terminator (feature `0xFFFF0000`, three callers).
* Transports inside the client: `/tmp/.aksusb` (the `aksusbd` socket, referenced at `0x8394486`),
  `/dev/Hardlock` (two code references, `0x837d021`, `0x839a551`), a static libusb (`usb_os_init`,
  `libusb_open`), plus the HASP4 legacy layer (`classic_key_info: LowAPI (API_HASP_STATUS)`, service names
  `R_GETHASP2_CODE0…`, `R_READ_MEMO_BLOCK`, `R_GET_NETHASP_MOD`). Game-side: `ReInitAladdinDongles: resetting
  hasp daemon`, `/pm/utils/reset_usb_bus.sh`, `Game dongle initialization failed!` (`0x807ebc7`).

**DERIVED:** this is the **HASP HL (Sentinel LDK) API**, the same `hasp_login(0xffff0000, …)` route as JP and
Terminator — not a HASP4/Hardlock API of its own. `/dev/Hardlock`, `/tmp/.aksusb` and the `R_*` HASP4 service
names belong to the LDK client's transport and legacy layers; Terminator's client has the same transports
(Terminator `DONGLE-ROUTE.md` §2: `/dev/Hardlock` + `ioctl`, and `aksusbd` on `/tmp/.aksusb`). Walking Dead's client is a newer build than JP's (JP has neither
`/tmp/.aksusb` nor the per-function trace strings). A hook keyed on addresses (like Terminator's
`hasp-haken.ini`) would start from `hasp_login` = `0x837bbc0`; the rest of the API is reachable the same way
through its trace strings.

### 6.2 Libraries the native route lacks

`DT_NEEDED` (18): `libz.so.1 /usr/lib/libcsv.so libGL.so.1 libGLU.so.1 libpthread.so.0 libdl.so.2
libXext.so.6 libX11.so.6 libasound.so.2 libcaca.so.0 libpulse.so.0 libpulse-simple.so.0 libssl.so.1.0.0
libcrypto.so.1.0.0 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6`. **MEASURED.** Against the
`native-games.json` entry plus the host's `/usr/lib32`:

* **`libcaca.so.0` (i386) is missing**: not in `runtime/lib32`, not in `/usr/lib32` (the entry lists
  `@lib32/libcaca.so.0`, which does not exist). The game imports 9 functions from it (`__caca0_init`,
  `__caca0_end`, `__caca0_create_bitmap`, `__caca0_free_bitmap`, `__caca0_get_event`, `caca_dither_bitmap`,
  `caca_refresh_display`, `caca_get_canvas_width/height`) and has two `R_386_COPY` relocations on its data
  (`__caca0_cv`, `__caca0_dp`). Trusty's libcaca0 itself needs `libslang.so.2`, `libncursesw.so.5`,
  `libtinfo.so.5` — none of which has an i386 copy on this host (only `libncursesw.so.6`/`libtinfo.so.6`).
* Everything else resolves: `/usr/lib/libcsv.so` is an absolute `DT_NEEDED`, which the loader already maps by
  base name (`rg_elfloader.c` ~line 405) to the `ElfLdr2/libs` copy; libssl/libcrypto 1.0.0 come from
  `ElfLdr2/libs`; libGLU from `runtime/lib32`; the rest from `/usr/lib32`.
* Loaded at run time with `dlopen`: `libvorbisfile.so.3` (host has it), and `nvtlsdk.so` / `nvtlactv.so`
  (Novatel cellular-modem SDK, for the cabinet's modem; not on the host, presumably optional).
