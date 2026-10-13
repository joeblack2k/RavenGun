# DONGLE-ROUTE — Terminator Salvation: how the game reaches its licence hardware

**Subject:** `game` (7,764,284 B, SHA-256 `798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`),
folder `Terminator Salvation ` on the share (the folder name ends in a space — a measured pitfall).
**Method:** the game is on the share read-only; `game`, `t4-patch` and `ServiceDongleApp/ServiceDongleApp`
were copied to `/tmp/t4-route/` on the deck and only *read* and *disassembled* there. **Nothing was
started.** No emulator, no loader, no wine, no container. No process was killed.
**Raw output:** `/tmp/t4-route/` on the deck — file names are given per section below.
**Labels:** **MEASURED** = a command with its raw output; **INFERRED** = a conclusion drawn from
measurements; **assumption** = untested.

Vaddr↔file-offset for this binary (needed to follow every address below):
first `PT_LOAD` maps `off 0 → vaddr 0x08048000`, so for offsets `< 0x54fd7b`,
`vaddr = file_offset + 0x08048000`. The RW segment maps `off 0x54fd7c → vaddr 0x08598d7c`
(`vaddr = file_offset + 0x08049000`). **MEASURED** (`readelf -l`, `/tmp/t4-route/01-elf.txt`).

---

## 1. How the game is linked and loaded

### 1.1 Headers — plain, unpacked, section headers present

| fact | value | source |
|---|---|---|
| `file` verdict | `ELF 32-bit LSB executable, Intel i386, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux.so.2, for GNU/Linux 2.6.9, stripped` | **MEASURED** `file game` |
| type | `EXEC` (ET_EXEC, fixed load address — not PIE) | **MEASURED** `readelf -h` |
| entry | `0x08057240` | **MEASURED** |
| program headers | **8** | **MEASURED** |
| section headers | **29** | **MEASURED** `readelf -h` / `readelf -S` |
| `PT_LOAD[0]` | off `0x0` → va `0x08048000`, filesz `0x54fd7b`, `R E`, align `0x1000` | **MEASURED** |
| `PT_LOAD[1]` | off `0x54fd7c` → va `0x08598d7c`, filesz `0x20ada8`, memsz `0x755074`, `RW` | **MEASURED** |
| `.text` | va `0x08057240`, size `0x4a31f8` | **MEASURED** `readelf -S` |
| `.rodata` | va `0x084fa460`, size `0x0952e9` | **MEASURED** |
| `.data` | va `0x08599ec0`, size `0x209c64` | **MEASURED** |
| `.bss` | va `0x087a3b40`, size `0x54a2b0` | **MEASURED** |
| `DT_NEEDED` | **13**: `libGL.so.1 libGLU.so.1 libglut.so.3 libpthread.so.0 librt.so.1 libtheora.so.0 libogg.so.0 libPhysXLoader.so.1 libm.so.6 libgcc_s.so.1 libc.so.6 libdl.so.2 libstdc++.so.6` | **MEASURED** `readelf -d` |
| `DT_RPATH` | `/usr/lib/PhysX/v2.8.1/` | **MEASURED** |
| hash tables | **no `DT_HASH`**; `DT_GNU_HASH` at `0x08048168` | **MEASURED** |
| relocations | **1013** `R_386_*`; `JMPREL` at `0x08054374`, `REL` at `0x0805334c` | **MEASURED** `readelf -d`, `readelf -r` |

**There is nothing unusual here and nothing is packed:** the whole file is two `PT_LOAD` segments mapped
directly, `.text` is 4.8 MB of ordinary code, no `.text` is decompressed at runtime, there is no second
`PT_LOAD` that would overwrite code, and there is no self-modifying stub before `e_entry`.
**MEASURED**, and this matters for §4: a hook address in `.text` is the address the game actually runs.

### 1.2 Which tool returned nothing — stated explicitly

* `nm game` → **`nm: game: no symbols`**. The binary is `stripped`: there is **no `.symtab`**.
  **MEASURED** (`/tmp/t4-route/17-calib.txt`). This is the one instrument that legitimately returns
  nothing for this file.
* `readelf -s game` **works**: it reports `Symbol table '.dynsym' contains 1020 entries`.
  It works because the 29 section headers exist. **MEASURED**.
* `readelf -h`, `-S`, `-l`, `-d` all work fully. **MEASURED**. `/tmp/t4-route/01-elf.txt`.

Consequence worth stating because prior work tripped over the opposite case: for this title
**you do not need to reconstruct the dynamic tables from `PT_DYNAMIC`** — `readelf` reads them directly.
(The loader still has to, because it does not use `readelf`.)

### 1.3 Dynamic symbols and imports

| measurement | value | source |
|---|---|---|
| `readelf --dyn-syms -W game \| grep -c '^ *[0-9]'` | **1020** | **MEASURED** |
| `nm -D game \| wc -l` | **1019** (1020 minus the null entry) | **MEASURED** |
| undefined (`UND`) dynamic symbols | **498** | **MEASURED** |
| import names wiped | **0** — every import has a name | **MEASURED** |

### 1.4 The "zero `hasp_` symbols" claim: CONFIRMED, and calibrated

```
$ nm -D game | grep -ci hasp                      ->  0
$ readelf --dyn-syms -W game | grep -ci hasp      ->  0
$ nm -D game | grep -ci glut                      ->  15      <-- calibration: instrument CAN say yes
$ nm -D game | grep -ci pthread                   ->  29      <-- calibration
$ nm -D game | wc -l                              ->  1019
$ strings -a game | grep -ci hasp                 ->  45      <-- the STRINGS are there
$ nm -D t4-patch | grep -ci hasp                  ->  0
$ nm -D t4-patch | wc -l                          ->  168
$ nm ServiceDongleApp | grep -c ' hasp'           ->  27      <-- the same client, WITH symbols
$ nm ServiceDongleApp | wc -l                     ->  17614
```
**MEASURED**, `/tmp/t4-route/17-calib.txt`, `/tmp/t4-route/02-syms.txt`, `/tmp/t4-route/03-syms2.txt`.

So the claim holds, and the calibration shows the empty result is meaningful: the same two instruments
return 15 `glut` and 29 `pthread` names in the same file, and 27 `hasp` **symbols** in the unstripped
twin. The correct reading is therefore **not** "the game has no HASP client" but:

> **The HASP client is statically linked into `game`.** There is no dynamic `hasp_*` symbol to
> interpose, so a symbol-name hook is impossible by construction — exactly as the current drop-in's
> manifest says. What is *not* true is that the client is absent.

The 45 `hasp` strings are the HASP library's own text, e.g. **MEASURED** `strings -a -t x game`:

```
 52e976 hasp                                 52ef84 <haspformat root="hasp_info"><si_feature /></haspformat>
 52ea2c <haspspec><feature id="%d" /></haspspec>   52f432 /dev/Hardlock
 52eadb         <hasp type="HASP-HL" />       5301b4 v2c_req
 52eb9f         <hasp type="HASP-SL" />       5301bc v2c_reply
 5302fe commuter_info                        74e07b hasp_vtc_d
 752ec0 nethasp.ini                          74e76d v2c_hw_container
```
Because the client is a static library, its code sits in the middle of `.text` with **no** dynamic
symbols pointing at it. That is why the interception points in §4 are addresses, not names.

### 1.5 `ServiceDongleApp` is the map — and the map has a limit

`ServiceDongleApp/ServiceDongleApp` (8,994,378 B, `ELF 32-bit ... with debug_info, not stripped`,
17,614 symbols, 27 `hasp` symbols) is the same build series. Its `hasp_*` symbols are **defined text
symbols**, so it is a name map for the stripped `game`. **MEASURED**.

Inside the HASP block the address delta `game − ServiceDongleApp = 0x000BB1B0` is constant. I verified it
by byte comparison rather than by trusting it — first 64 bytes, **12 of 13 targets are 0-byte-different**:

```
name                  game VA    twin VA    diff bytes / 64   first bytes of game
hasp_login            0x083bbe60 0x08300cb0   0   53 83 ec 08 …   (push ebx; sub $8,esp)
hasp_login_scope      0x083baac0 0x082ff910   0   56 53 81 ec …
hasp_login_ex         0x083bab50 0x082ff9a0   0   e9 4b 4b 00 00 … (jmp rel32 — ALIAS)
hasp_logout           0x083bbe00 0x08300c50   0   53 83 ec 1c …
hasp_read             0x083bad30 0x082ffb80   0   57 56 53 83 …
hasp_write            0x083bac80 0x082ffad0   0   57 56 53 83 …
hasp_encrypt          0x083bbd60 0x08300bb0   0   53 83 ec 18 …
hasp_decrypt          0x083bbce0 0x08300b30   0   53 83 ec 18 …
hasp_get_size         0x083babe0 0x082ffa30   0   57 56 53 83 …
hasp_get_info         0x083ba8a0 0x082ff6f0   6   55 57 56 53 …
hasp_get_sessioninfo  0x083bae80 0x082ffcd0   0   55 57 56 53 …
hasp_get_rtc          0x083bab60 0x082ff9b0   0   53 83 ec 1c …
hasp_free             0x083baab0 0x082ff900   0   e9 ab 22 00 00 … (jmp rel32 — ALIAS)
```
**MEASURED**, `/tmp/t4-route/22-byte-identity.txt` and `/tmp/t4-route/28-login_ex.txt`.

**And here is the limit, measured the hard way.** The delta does **not** hold outside that block:
`SDA physxAddSimpleNOW+0x1320` maps to game `0x081c6a50`, which is not a PhysX function at all — it is
the game's own dongle wrapper. So **twin names may only be used where the bytes have been compared**;
a delta-derived name outside `.text` 0x083ba8a0–0x083bc000 is worthless. I hit this myself:
`SDA hasp_login_ex = 0x082ff9a0` looked like game `0x083baa50`, which is wrong; the correct address is
`0x083bab50`. Both are recorded in `/tmp/t4-route/28-login_ex.txt` so the error is reproducible.

---

## 2. The actual route to the dongle — MEASURED, with bytes

The route is **not** a socket to a game server and **not** an in-process table of emulated values.
It is two **character-device / Unix-socket transports**, both reached from the statically linked HASP
client:

**(a) HASP classic / "Hardlock": `open("/dev/Hardlock", O_RDWR)` + `ioctl(fd, 0x02FED112, buf)` + `close(fd)`.**

The `/dev/Hardlock` literal is at vaddr `0x08577432` (`strings -a -t x game` → `52f432`,
`+0x08048000`). It is referenced from exactly **two** code sites. **MEASURED**, `/tmp/t4-route/07-hardlock.txt`:

```
--- site 1: inside the function that starts at 0x083c5f10
 83c6b0c:  6a 02                    push   $0x2
 83c6b0e:  68 32 74 57 08           push   $0x8577432          ; "/dev/Hardlock"
 83c6b13:  e8 40 e8 c8 ff           call   8055358 <open@plt>
 83c6b18:  83 c4 10                 add    $0x10,%esp
 83c6b1b:  89 c6                    mov    %eax,%esi
 83c6b1d:  83 f8 ff                 cmp    $0xffffffff,%eax
 83c6b20:  0f 84 1d 02 00 00        je     83c6d43
 83c6b26:  83 ec 0c                 sub    $0xc,%esp
 83c6b29:  68 00 10 00 00           push   $0x1000             ; 4096-byte buffer
 83c6b2e:  e8 65 ef c8 ff           call   8055a98 <malloc@plt>
 ...
 83c6b42:  55                       push   %ebp
 83c6b43:  8b 7c 24 1c              mov    0x1c(%esp),%edi
 83c6b47:  57                       push   %edi
 83c6b48:  68 12 d1 fe 02           push   $0x2fed112         ; <-- the ioctl request
 83c6b4d:  56                       push   %esi               ; fd
 83c6b4e:  e8 45 04 c9 ff           call   8056f98 <ioctl@plt>
 83c6b53:  89 c3                    mov    %eax,%ebx
 83c6b55:  89 34 24                 mov    %esi,(%esp)
 83c6b58:  e8 db 01 c9 ff           call   8056d38 <close@plt>

--- site 2: the small self-contained function at 0x083d3d70
 83d3d7f:  6a 02                    push   $0x2
 83d3d81:  68 32 74 57 08           push   $0x8577432          ; "/dev/Hardlock"
 83d3d86:  e8 cd 15 c8 ff           call   8055358 <open@plt>
 ...
 83d3d9d:  68 00 10 00 00           push   $0x1000
 83d3da2:  e8 f1 1c c8 ff           call   8055a98 <malloc@plt>
 ...
 83d3dd0:  55                       push   %ebp
 83d3dd1:  50                       push   %eax
 83d3dd2:  68 12 d1 fe 02           push   $0x2fed112
 83d3dd7:  56                       push   %esi
 83d3dd8:  e8 bb 31 c8 ff           call   8056f98 <ioctl@plt>
 83d3ddd:  89 34 24                 mov    %esi,(%esp)
 83d3de0:  89 c3                    mov    %eax,%ebx
 83d3de2:  e8 51 2f c8 ff           call   8056d38 <close@plt>
```
Raw bytes quoted from `objdump -s` (`/tmp/t4-route/23-final.txt`), e.g. site 2:
`83d3d70 55575653 83ec1489 54240c89 4c24086a` / `83d3d80 02683274 5708e8cd 15c8ff83 c41089c6`
→ `55 57 56 53 | 83 ec 14 | 89 54 24 0c | 89 4c 24 08 | 6a 02 | 68 32 74 57 08 | e8 cd 15 c8 ff`.

* `O_RDWR` = `0x2`. **MEASURED.**
* ioctl request = **`0x02FED112`** — the *only* ioctl request constant in the whole HASP region
  0x083b0000–0x083e0000. **MEASURED** (`/tmp/t4-route/12-transport.txt`): the scan of that region for
  `push $0x2xxxxxxx` yields exactly the two lines `68 12 d1 fe 02` shown above.
* A second instrument agrees: `push $0x2fed112` appears exactly twice in the entire binary.
* The small function at `0x083d3d70` has **25** `E8` call sites. **MEASURED** (`/tmp/t4-route/19-chain.txt`).
* **What comes back** is parsed immediately after the ioctl: `mov (%edi),%esi; test; jle` → an `int32`
  count at `buf+0`, then a loop over **8-byte records**, `mov 0x0(%ebp),%ebx` (16-bit id),
  `mov 0x4(%ebp),%edx; shr $0x10,%edx` (16-bit value), `cmp $0xffff,%ebx; ja` → records with an id
  above `0xFFFF` are skipped. **MEASURED** (`/tmp/t4-route/12-transport.txt`, 0x083d3e00–0x083d3e4d).
  **INFERRED:** this is a *list of HASP/Hardlock IDs present on the local port*, not licence data —
  it matches `t4-patch`'s own text `classic_check_local_port: invalid HASP ID, want: %u, found: %u`
  (**MEASURED** `strings -a -t x t4-patch`, offset 0xfb720), and the same shape appears again at
  `0x083c6b67`–`0x083c6d71` in site 1's function.

**(b) HASP HL via the Aladdin `aksusbd` daemon on an AF_UNIX socket `/tmp/.aksusb`.**

There is no `"aksusb"` string anywhere in the binary — the path is **built from three immediates**.
Its construction site is at `0x083d6aa3`. **MEASURED**, `/tmp/t4-route/14-hl.txt`, raw bytes
`/tmp/t4-route/23-final.txt`:

```
 83d6a70:  56                       push   %esi
 83d6a71:  53                       push   %ebx
 83d6a72:  83 ec 78                 sub    $0x78,%esp
 83d6a77:  6a 00                    push   $0x0                 ; protocol
 83d6a79:  6a 01                    push   $0x1                 ; SOCK_STREAM
 83d6a7b:  6a 01                    push   $0x1                 ; AF_UNIX
 83d6a7d:  e8 76 ea c7 ff           call   80554f8 <socket@plt>
 83d6a85:  a3 ac 3e 79 08           mov    %eax,0x8793eac       ; global socket fd
 ...
 83d6a8d:  8d 5c 24 06              lea    0x6(%esp),%ebx       ; &sockaddr_un
 83d6a92:  6a 6e                    push   $0x6e                ; sizeof(struct sockaddr_un) = 110
 83d6a96:  53                       push   %ebx
 83d6a97:  e8 ac 00 c8 ff           call   8056b48 <memset@plt>
 83d6a9c:  66 c7 44 24 16 01 00     movw   $0x1,0x16(%esp)      ; sun_family = AF_UNIX
 83d6aa3:  c7 44 24 18 2f 74 6d 70  movl   $0x706d742f,0x18(%esp)   ; "/tmp"
 83d6aab:  c7 44 24 1c 2f 2e 61 6b  movl   $0x6b612e2f,0x1c(%esp)   ; "/.ak"
 83d6ab3:  c7 44 24 20 73 75 73 62  movl   $0x62737573,0x20(%esp)   ; "susb"
 83d6abb:  c6 44 24 24 00           movb   $0x0,0x24(%esp)          ; NUL
 83d6ac3:  8b 0d ac 3e 79 08        mov    0x8793eac,%ecx
 83d6ac9:  6a 6e                    push   $0x6e
 83d6acb:  53                       push   %ebx
 83d6acc:  51                       push   %ecx
 83d6acd:  e8 56 ed c7 ff           call   8055828 <connect@plt>
```
`"/tmp" + "/.ak" + "susb"` = **`/tmp/.aksusb`**. I checked that this is not a coincidental byte match:
each of the three immediates occurs **exactly once** in the whole 7.7 MB file (**MEASURED**,
`/tmp/t4-route/15-*`): `"/tmp"` 1×, `"/.ak"` 1×, `"susb"` 1× — all at `0x083d6aa7/0x083d6aaf/0x083d6ab7`.
`0x6e` = 110 = `sizeof(struct sockaddr_un)`, and the family word lands exactly at struct offset 0
once the four `memset` pushes are accounted for. **INFERRED** (high confidence): `sun_path = "/tmp/.aksusb"`.

The request/response loop on that socket is `send@plt` at **0x083bce14** and `recv@plt` at **0x083bcff0**,
inside the cluster `0x083bc000–0x083bd020`. **MEASURED**. The payload is the HASP-HL XML dialect — the
format strings are in `.rodata`: `<hasp_info>`, `<haspscope>`, `<haspformat format="keyinfo"/>`,
`<haspformat format="sessioninfo"/>`, `<hasp type="HASP-HL" />`, `<hasp type="HASP-SL" />`,
`<license_manager hostname="localhost">`, plus the internal names `v2c_req`, `v2c_reply`, `c2v_req`,
`c2v_reply`, `xlm_api_login_request`, `xlm_api_login_ex_request`. **MEASURED**.

**Not a syscall route.** Raw `int 0x80` / `syscall` / `sysenter` byte pairs do occur in `.text`
(10 / 9 / 3 hits, `/tmp/t4-route/24-syscall-dlopen.txt`) but **both** dongle transports go through the
`@plt` stubs above, so no raw-syscall interception is needed; and I did **not** calibrate that byte-pair
scan against false positives inside immediates, so I do not claim those hits are real instructions.

**Not a `dlopen` route.** `dlopen@plt` is called at only two sites. At `0x082e3a1c` it is
`dlopen(NULL, RTLD_LAZY)` (`movl $0x0,(%esp)`), i.e. a handle for `dlsym` on the main program. At
`0x082e41b6` the name comes from a runtime pointer with flags `0x102`/`0x103`. **MEASURED**
(`/tmp/t4-route/25-dlopen.txt`). There is no `dlopen("libhasp…")` anywhere: the HASP client is linked in.

### 2.1 The call chain into the licence check

```
game dongle wrapper, fn at 0x081c6a50
   E8-calls hasp_login (0x083bbe60) at 0x081c6b1d, 0x081c6c63, 0x081c6cdf        MEASURED
   hasp_login 0x083bbe60  --  e9 d2 32 00 00  @0x083bbe99  -->  0x083bf170        MEASURED
   0x083bf170 E8-calls 0x083bc900 @0x083bf1b7 and 0x083bc6e0 @0x083bf1ec       MEASURED
       (both live in 0x083bc000-0x083bd020, the cluster holding send@plt/recv@plt)
   ... -> API_HL_CSTACK 0x083d6a50 -> 0x083d6a70 -> connect("/tmp/.aksusb")      INFERRED (see below)
```
**MEASURED** with a `call rel32` xref scan (`/tmp/t4-route/19-chain.txt`). The tool is calibrated: it
found 3 callers of `hasp_login`, 25 of the Hardlock transaction function, 5 of `hasp_read`, 5 of
`hasp_write`, 1 each of `hasp_encrypt`/`hasp_decrypt`, and **0** of `hasp_get_info` — a spread that
only makes sense if it is reading real call sites.

**Two honest gaps in that chain.** `API_HL_CSTACK` (0x083d6a50) has **0** `E8` callers, and I could not
find a single plain 32-bit pointer to it (or to `hasp_login`, `0x083bf170`, `0x083c5f10`, `0x083d3d70`)
anywhere in the file — **0 raw occurrences** for each, **MEASURED** (`/tmp/t4-route/20-*`). So the HASP
library dispatches internally through *encoded* pointers; this is consistent with its obfuscated data
region (the marker `!DST` occurs 64× — 60 in `.data`, 4 in `.text` — and the `.data` string pool around
vaddr `0x0879707b` holds `hasp_vtc_d`, `v2c_hw_container`, `v2c_sw_file`, `vtc_container`, `c2v2`).
**The last hop — HL session layer → `connect("/tmp/.aksusb")` — is therefore INFERRED, not measured.**

**Overall:** the game reaches its licence hardware through **the Aladdin HASP client that is compiled
into `game`, talking either to `/dev/Hardlock` by `ioctl` (`0x02FED112`) or to the `aksusbd` daemon on
the AF_UNIX socket `/tmp/.aksusb`.** There is no dynamic symbol anywhere on that path.

---

## 3. What the game expects back

### 3.1 Which HASP calls the game makes, and with which file IDs — MEASURED

`hasp_read`/`hasp_write` are the game's queries, so the arguments are readable. Call sites and
immediates (`/tmp/t4-route/27-args.txt`); argument order per the HASP HL API
`hasp_read(handle, fileid, offset, length, buffer)` on cdecl, i.e. `(%esp)`=handle, `0x4`=fileid,
`0x8`=offset, `0xc`=length — **INFERRED** from the layout, the values are **MEASURED**:

| call | fileid | offset | length |
|---|---|---|---|
| `hasp_read` @ `0x081c66ae` | **0xFFF2** | `0x1C` (28) | **1** |
| `hasp_read` @ `0x081c6787` | **0xFFF2** | `0x3C` (60) | **4** |
| `hasp_read` @ `0x081c6854` | **0xFFF2** | — | **4** |
| `hasp_read` @ `0x081c68ee` | **0xFFF2** | — | **4** |
| `hasp_read` @ `0x081c6a25` | **0xFFF0** | — | — |
| `hasp_write` @ `0x081c636b` | **0xFFF2** | `0x1C` | **1** |
| `hasp_write` @ `0x081c63eb` | **0xFFF2** | `0x3C` | **4** |
| `hasp_write` @ `0x081c6461` | **0xFFF2** | — | **4** |
| `hasp_write` @ `0x081c6516` | **0xFFF2** | — | **4** |
| `hasp_write` @ `0x081c6611` | **0xFFF2** | — | — |

plus `hasp_encrypt` once @ `0x081c5eb5` and `hasp_decrypt` once @ `0x081c5e48`, both from the function at
`0x081c5df0`. **MEASURED.**

**This refutes the current drop-in's central data assumption.** Its `dongle.json` declares one memory
file with **`fileid: 65524 = 0xFFF4`** and admits in its own `notes` that the fileid is *not measured*
but copied from Halo. Measured for this title: the game uses **0xFFF2** (read *and* write) and
**0xFFF0** (read). **0xFFF4 never appears in `game`.** The image file name
`memory/00000000000000001111111111110100` encodes that wrong fileid: the naming is 16 zero bits
followed by the fileid in 16 bits, so `…11110100` = 0xFFF4, whereas the game needs
`…11110010` = **0xFFF2** and `…11110000` = **0xFFF0**. The encoding is confirmed by an independent
measurement: `patches/JurassicPark/.../dongle.json` uses exactly
`memory/00000000000000001111111111110010` for **fileid 65522 = 0xFFF2**. **MEASURED.**

What the game does with the answers: the 1-byte read at `0xFFF2+0x1C` and the 4-byte reads at
`0xFFF2+0x3C` are compared/branched on, and `hasp_decrypt`/`hasp_encrypt` are used to turn a stored
blob into a valid one. The feature IDs handed to `hasp_login` are **0xFFFF0000** (classic range),
**0x00000000**, and a computed `0xFFFF0000 | arg` — **MEASURED** at `0x081c6c5c`, `0x081c6cd4`,
`0x081c6aa0`/`0x081c6b00`. The returned status is compared with `0x1f` and with `0`
(`cmp $0x1,%eax; sbb; not; and $0x5`), and stored to globals `0x087c4220` and state `+0x10`.
The vendor-code buffer is the global at **0x087c4240**. **MEASURED.**

### 3.2 `hhl_mem.dmp` — 128 bytes, of which the last 16 are a tool trailer

**MEASURED** (`/tmp/t4-route/20-*`), all 32 dwords:

```
+0x00  a47e36da   +0x04  3dffd0fc   +0x08  b46baf2c   +0x0c  59e37ac8
+0x10  72e7b19b   +0x14  bcaa93a6   +0x18  86d6dd75   +0x1c  e8add8b0
+0x20  335b552a   +0x24  b586cbdb
+0x28 … +0x6b : 0xffffffff, except +0x4c = 0xffffff03      (68 bytes of 0xFF, one odd byte)
+0x6c  00000a62 (2658)
+0x70  54504257  -> ASCII "WBPT"        <-- not plausible dongle memory
+0x74  00000001
+0x78  00000010
+0x7c  b1280000
```
**INFERRED:** the trailing 16 bytes (`"WBPT"`, `1`, `0x10`, `0xb1280000`) are a **trailer of the dumping
tool**, not dongle content — a 4-byte ASCII tag is not something a dongle memory page contains, and it
is 16 bytes. The repo's other `kind: hasp` image, Halo Fireteam's, is **112 bytes = 128 − 16**.
**INFERRED, not measured:** the Terminator image should be **112 bytes** (drop the last 16) rather than
the 128 the current `dongle.json` declares.

### 3.3 The `hasp/` folder — what it is not established to be

**MEASURED** (`/tmp/t4-route/05-haspdir.txt`, `/tmp/t4-route/10-haspdir2.txt`, `/tmp/t4-route/16-*`):

* **5413 entries**: `5410 × 32 B`, plus `hhl_mem.dmp` (128 B), plus one 256 B file
  (`f95bc779ab3b4e692720f1de5424a271`) and one 512 B file (`d43663f21b77e5b8108c942773a0d787`).
  There is **no `nethasp.ini`** in the folder (the one 11-character name is `hhl_mem.dmp` itself).
* Names: **3734 × 32 hex chars**, **1676 × 64 hex chars**. The 64-char names are the 32-char names
  **with 32 more hex chars appended** — e.g. `0314528b4c7378325f10c2e1d138cf96` exists **and**
  `0314528b4c7378325f10c2e1d138cf967d89cc1355f20ef923cabfcc9a5197cd` exists. That is a collision-extended
  hash naming scheme, not random names. **MEASURED**.
* Contents: all **distinct** (1500/1500 sampled), aggregate byte entropy over 48,000 bytes =
  **7.9960 bits/byte** (8.0 = uniform) → **hash/key-like, not structured licence data**. **MEASURED**.
* **No record appears inside `hhl_mem.dmp`** (0 of 300 sampled). **MEASURED**.
* **The game binary contains no literal path containing `hasp`.** `strings -a game | grep -E '/hasp|hasp/|hasp%|hasp\b'`
  returns only HASP XML/API text, never a filesystem path. **MEASURED** (`/tmp/t4-route/23-final.txt`).

**Therefore: what the game appears to want back is the HASP-HL container data for file IDs 0xFFF2 and
0xFFF0 (§3.1), not the `hasp/` directory.** Whether anything reads that directory, and what wrote it,
is **not established** — see §7. **INFERRED** (weak): given the 128-byte image sitting in it and the HASP
library's own container strings (`hasp_vtc_d`, `v2c_hw_container`, `v2c_sw_file`, `commuter_info`,
`c2v_read_commuter_file` in the twin), the folder is a HASP-side data store, but neither producer nor
consumer was measured.

---

## 4. Candidate interception points, ranked

### Rank 1 — inline hook on the game's own statically linked HASP API entries (address-keyed)

This is the highest-level and the most complete point: it never needs the transport, and it answers
exactly what the game consumes.

| target | game VA | prologue | confidence |
|---|---|---|---|
| `hasp_login` | **0x083bbe60** | `53 83 ec 08` | byte-identical to twin, 0/64 |
| `hasp_login_scope` | **0x083baac0** | `56 53 81 ec` | 0/64 |
| `hasp_logout` | **0x083bbe00** | `53 83 ec 1c` | 0/64 |
| `hasp_read` | **0x083bad30** | `57 56 53 83` | 0/64 |
| `hasp_write` | **0x083bac80** | `57 56 53 83` | 0/64 |
| `hasp_encrypt` | **0x083bbd60** | `53 83 ec 18` | 0/64 |
| `hasp_decrypt` | **0x083bbce0** | `53 83 ec 18` | 0/64 |
| `hasp_get_size` | **0x083babe0** | `57 56 53 83` | 0/64 |
| `hasp_get_sessioninfo` | **0x083bae80** | `55 57 56 53` | 0/64 |
| `hasp_get_rtc` | **0x083bab60** | `53 83 ec 1c` | 0/64 |
| `hasp_get_info` | **0x083ba8a0** | `55 57 56 53` | 6/64 bytes differ — 2nd-grade |

* **Why reachable:** `hasp_login` is `E8`-called from `0x081c6b1d`, `0x081c6c63`, `0x081c6cdf` inside the
  game's own wrapper at `0x081c6a50`; that wrapper is reached from `0x080aa5e0` ← `0x080ab020`.
  `hasp_read` has 5 callers, `hasp_write` 5. **MEASURED**.
* **Why it does not rely on names:** the targets are internal code with no dynamic symbol; the hook key
  is the file offset/VA, guarded by file size `7764284` and `e_entry 0x08057240`.
* **What could go wrong — measured, not hypothetical:**
  1. **`hasp_free` (0x083baab0) and `hasp_login_ex` (0x083bab50) are `jmp rel32` aliases**, not functions:
     `e9 ab 22 00 00` and `e9 4b 4b 00 00`. A prologue-based inline hook **must refuse** them; patching
     the first 5 bytes of a `jmp` is a jump into the middle of a trampoline. (The repo's `hasp-haken.ini`
     already predicts this refusal for `hasp_free`; I confirm the bytes and add `hasp_login_ex`.)
  2. `hasp_get_info` has **0** `E8` callers — hooking it may be hooking a function nothing calls.
  3. `hasp_encrypt`/`hasp_decrypt` each have exactly one caller and need the dongle's real AES key to
     produce output the game accepts. A login that succeeds while encrypt/decrypt is answered wrongly
     moves the failure later, which is worse than failing here.
  4. The wrapper tries **three** logins (features `0xFFFF0000`, `0`, computed) and stores results into a
     shared `0x2e0`-byte state plus globals `0x087c4220`/`0x087c4240`. All paths must answer consistently.

### Rank 2 — loader-side GOT override of the libc transport functions (address-keyed, no HASP names)

The loader already writes these slots; this is a 4-byte write in code that already runs for this title.

| import | GOT slot (`R_386_JUMP_SLOT`) | PLT stub |
|---|---|---|
| `open@GLIBC_2.0` | **0x08599700** | 0x08055358 |
| `ioctl@GLIBC_2.0` | **0x08599e10** | 0x08056f98 |
| `connect@GLIBC_2.0` | **0x08599834** | 0x08055828 |
| `send@GLIBC_2.0` | **0x085999c8** | 0x08055e78 |
| `recv@GLIBC_2.0` | **0x08599d34** | 0x08056c28 |
| `socket@GLIBC_2.0` | 0x08599768 | 0x080554f8 |
| `close@GLIBC_2.0` | 0x08599d78 | 0x08056d38 |

**MEASURED** (`readelf -r`, `/tmp/t4-route/21-rels.txt`).

* **Why reachable:** `~/projects/ravengun/tools/rg-elfloader/rg_elfloader.c`, function `pas_rel_toe`,
  cases `R_386_GLOB_DAT` / `R_386_JMP_SLOT` do literally `*waar = (uint32_t)(uintptr_t)sym;`, with the
  address coming from `zoek_symbool(naam)`. The rewrite point already exists, already runs, and the
  1013 relocations of this title are already applied successfully. **MEASURED** (source read-only).
* **What it would answer:** a shim that (i) answers `open("/dev/Hardlock")` and `ioctl(fd, 0x02FED112, …)`,
  and/or (ii) listens on `/tmp/.aksusb` and answers the `xlm_api_*` XML. Note `/tmp/.aksusb` lies inside
  `/tmp`, which the current route already binds writable (`--bind /tmp /tmp`), so a fake daemon is
  reachable without changing the sandbox root.
* **What could go wrong:** `open`, `ioctl` and `connect` are generic — `connect@plt` has **11** call sites
  in the whole binary and the Hardlock transaction function has **25**. A blanket override breaks normal
  file/device/network I/O. The override must match **exactly** on the string `"/dev/Hardlock"`
  (vaddr `0x08577432`) and on request `0x02FED112`, or on `AF_UNIX` + `"/tmp/.aksusb"`. It must also
  replace the *resolved address* only — the guest's own PLT stubs read the same slots.
* The largest risk is not the patch, it is the protocol: this point **requires a working HASP emulator**
  (aksusb XML *and* the classic port list) that does not exist yet in this repo.

### Rank 3 — patch the game-level status test (blunt; last resort)

`0x081c6b22: 83 f8 1f  cmp $0x1f,%eax`, then the success test `cmp $0x1,%eax; sbb; not; and $0x5` at
`0x081c6b2f`–`0x081c6b36`, forcing the wrapper to return 0. **MEASURED** (`/tmp/t4-route/11-logincaller.txt`).

* Cheapest to try; least likely to be sufficient.
* **Risk:** the same wrapper also fills the handle at state `+0xc` and the vendor-code buffer at
  `0x087c4240`; a faked OK leaves them empty, so the subsequent `hasp_read`/`hasp_decrypt` calls run
  against a null handle. The strings `Too many consexutive dongle errs (%d), bailing` and
  `ReInitAladdinDongles called: %d times` (**MEASURED**) show there is a retry counter and a re-init
  path, i.e. the same decision is taken more than once.

### Rank 4 — supply the devices in the sandbox (no code change)

Make a real `/dev/Hardlock` and run an aksusbd-alike on `/tmp/.aksusb`. This is what the cabinet does.
But we do not have an aksusbd implementation, and the `ioctl(0x02FED112)` layout plus the XML dialect
would have to be implemented anyway — this is Rank 2's work moved into a daemon. **Not** something the
loader can do alone.

### Explicitly *not* workable, and why

* **A symbol-name hook on `hasp_*`** — **0** of **1019** dynamic symbols. This is precisely why the
  current drop-in is inert, and no amount of hooking the loader's symbol resolver can fix it.
* **`LD_PRELOAD` / `LD_AUDIT` of `ravengun-hasp.so`** — there is no dynamic `hasp_*` symbol to interpose;
  and the drop-in's own `dropin.ini` already records that the library does not export `la_version`,
  so the dynamic loader ignores it as an audit library (`undefined symbol: la_version; ignored`).

**So: the honest answer is *not* "the only workable point is in the loader".** The loader's own ELF
loader is the right home for **Rank 2**, but **Rank 1** — an address-keyed inline hook inside the game's
own statically linked HASP client — is the more complete point and is fully reachable, because the game
is unpacked `ET_EXEC` with a fixed `e_entry` and the client's entry points are byte-verified targets.

---

## 5. A falsifiable check

**The one command.** Take the existing loader route for this title and add **Rank 2 for `open` only**:
override `open@GLIBC_2.0` (GOT slot `0x08599700`) with a shim that logs `(path, return address)` and then
calls the real `open`. Add **exactly one** line to the log per call. Then start the title once under the
loader — **this requires the bench, which is another lane; nothing was started for this document.**

**The prediction that must come true.** The log contains a line whose path is `/dev/Hardlock` and whose
return address is **`0x083c6b18` or `0x083d3d8b`** — the two measured `open@plt` return addresses
(§2). Change the shim to return `ENOENT` for that path and nothing else, and the game's dongle path must
change state (one fewer attempt, a different status in `0x087c4220`) while every other file open is
untouched.

**What a false positive would look like — three distinct ones.**

1. **The hook never fires at all** (empty log). That is *only* informative if the instrument is
   calibrated in the same run: the same shim **must** log at least one known-good open. Pick
   `/dev/urandom` — the project's own in-process logger already records the game reading `/dev/urandom`
   ~31 ms after the jump — or `libNxCooking.so`, which the game is known to `dlopen` at startup.
   **If the log has no `/dev/urandom` line, the empty `/dev/Hardlock` line means the hook is broken,
   not that the game does not use the device.**
2. **The line is the loader's own open, not the guest's.** The loader itself opens files (it maps the
   guest, loads 13 libraries). Require the logged return address to lie inside the guest `.text`
   range `0x08057240`–`0x084fa438`, and for the `/dev/Hardlock` case require it to be exactly
   `0x083c6b18` or `0x083d3d8b`. A line with a host address (above `0x08…`, i.e. a `0x7…`/`0xb7…`
   address) is a false positive caused by the instrument.
3. **The correct conclusion is "not used, and here is why":** it is entirely possible that the title
   takes the HASP-HL branch first (`hasp_login` tail-jumps to `0x083bf170`, §2.1) and **never** falls
   back to the classic `/dev/Hardlock` port. In that case the expected true negative is an empty
   `/dev/Hardlock` line *with* a present `/dev/urandom` line — and the Rank 2 shim must then be moved
   to `connect` (slot `0x08599834`) and look for `AF_UNIX` + `/tmp/.aksusb`. That distinction is exactly
   what this check is for.

**A cheaper pre-check that needs no run** (and that I did do): confirm the two `open@plt` return
addresses by reading the bytes at `0x083c6b13`/`0x083d3d86` and adding 5. **MEASURED** — done in §2.
If the Run-1 log shows a *different* return address for `/dev/Hardlock`, then the file on the deck is not
SHA-256 `798dec07…b206a`, and the whole address table is void.

---

## 6. Cross-check: is Terminator different, or is it the same as the others?

Both sides, measured.

**The titles whose dongle we answer successfully** — `RabbidsHollywood`, `LGI`, `LGS`, `OG` — reach their
licence through a **named import**, and that is the whole reason a name-based drop-in works
(**MEASURED**, `/tmp/t4-route/13-*` and `/tmp/t4-route/18-*`):

| game | `kind` | route | named entry point the drop-in matches |
|---|---|---|---|
| RabbidsHollywood | `matrix` | `matrix64.dll` (Unity Mono, 64-bit) | `Init_MatrixAPI`, `Dongle_Count`, `Dongle_ReadSerNr` |
| LGI | `region` | `amDongle*` API | `amDongleOpen`, `amDongleGetRegion`, … |
| LGS | `region` | `amDongle*` API | `amDongleOpen`, `amDongleGetRegion`, … |
| OG | `region` | `amDongle*` API | `amDongleOpen`, `amDongleGetRegion`, … |

For those, the answering side is a **named symbol or exported DLL function** — the name-based route has
something to hold on to.

**The other `kind: hasp` titles in the repo are in the same position as Terminator:**

* `Halo/dropin/ravengun-hasp/Halo/dongle.json` has `kind: hasp` and its own `notes` say, verbatim:
  *"LET OP: de hasp-aanroepen van deze game zijn statisch gelinkt, dus de emulator wordt geladen maar nog
  niet aangesproken"* — **Halo is also inert for the same reason.** **MEASURED** (file read).
* `JurassicPark`'s `kind: hasp` image is explicitly labelled a **constructed** image, not a dump.
  **MEASURED**.
* `t4-patch` also has **0** `hasp` dynamic symbols (168 dynsyms) while carrying a full static HASP
  classic client (`classic_check_local_port`, `classic_search_remote`) and **two** `/dev/Hardlock`
  strings (file offsets `0xfb4f5`, `0xfdd0e`). **MEASURED.**

**Conclusion, and it is a measured difference, not an assumption:** the difference is **not** between
"Terminator" and "the games that work". It is between **a licence reached through a named import**
(matrix / `amDongle*` — those work) and **a licence reached through a statically linked HASP client**
(Terminator, Halo, Jurassic Park, `t4-patch` — those cannot work by name, and none of them currently
does). Terminator is not special; it is in the majority of the `hasp` family, and the family is inert as
a group. Terminator's distinguishing features are positive ones: it is an **unpacked `ET_EXEC` with
section headers**, its HASP entry points are **byte-verified against an unstripped twin**, and its file
IDs are **readable from its own call sites** — so it is the best of the group in which to build the
address-keyed route.

---

## 7. Could not determine

1. **Whether either transport is actually exercised at startup, and in what order.** Both are present in
   code and both are `E8`-reachable, but that is static. Deciding whether the title takes the
   `/tmp/.aksusb` branch, the `/dev/Hardlock` branch, or both, needs a run — and no run was allowed.
   §5 is the instrument that would settle it.
2. **The last hop of the call chain.** `API_HL_CSTACK` (`0x083d6a50`) has **0** `E8` callers and **no**
   plain absolute pointer anywhere in the file, so the edge *HL session layer → `connect("/tmp/.aksusb")`*
   is **INFERRED**, not measured. The three path immediates being unique in the file proves the
   *socket target*; it does not prove *who calls it*.
3. **Which feature ID and which vendor code the login actually uses.** The wrapper computes the feature
   at runtime (`0xFFFF0000|arg`, or `arg`) and fills the vendor-code buffer by `memcpy` from its
   arguments; the concrete values live in the callers of `0x081c6a50`, which I enumerated only one level
   (`0x080ab099` ← fn `0x080ab020`). Deeper than that is not measured. The three *immediate* feature IDs
   (`0xFFFF0000`, `0`, computed) are measured; the computed one is not.
4. **What wrote the `hasp/` folder, and whether anything reads it.** The 5410 records are hash-like
   (entropy 7.996 bits/byte, all distinct, collision-extended hex names), and `game` contains **no**
   `hasp`-containing path literal. Producer and consumer are both unmeasured. My weak inference (a HASP
   data store) rests on `hhl_mem.dmp`'s name and the HASP container strings, not on a measurement.
5. **The correct file IDs in the drop-in are measured (0xFFF2 read+write, 0xFFF0 read) but the *content*
   the game expects at `0xFFF2+0x1C` (1 byte) and `0xFFF2+0x3C` (4 bytes) is not.** We know the offsets
   and lengths; we do not know the expected values, and `hhl_mem.dmp` is a 128-byte page whose mapping
   onto those offsets is unverified.
6. **Whether the AES key material is recoverable.** The twin holds `aladdinPassHL`, `aladdinPassSeedHL`,
   `gRockeySeed` and `DongleGetRand`; the per-title key that makes `hasp_encrypt`/`hasp_decrypt` answer
   correctly is **not** established, and the current drop-in contains no key material at all.
7. **The byte-exact HASP-HL XML dialect.** The format strings and the `v2c_*`/`c2v_*` names are measured,
   but no request/response pair was reconstructed, so an aksusb emulator cannot be written from this
   document alone.
8. **Whether `t4-patch` matters to the licence check.** It carries its own static HASP classic client and
   two `/dev/Hardlock` references, but `game` contains **no** reference to `t4-patch` (0 literal hits),
   and the loader profile (`Type=ElfLdr2`) loads `game`. Whether the two are started together on the
   cabinet, and which one holds the decisive check, is not measured.
9. **A second protection family is present and untouched by this document.** The twin exports
   `AllocRockey`, `FindRockey`, `ClearRockey`, `gRockeySeed`, `cardGetDevice`, `device_open` and the
   string `rockey_ioctl` — a **Feitian Rockey** path alongside Aladdin. Whether `game` uses it, and
   whether it is a fallback or independent, was not investigated.
10. **The raw `int 0x80` / `syscall` / `sysenter` hits are uncalibrated.** 10 / 9 / 3 byte-pair hits in
    `.text`, not verified as real instructions and not checked against false positives inside immediates.
    I did not need them, because both dongle transports use `@plt` calls — but the negative is not
    calibrated, so I do not claim those are all false positives either.

---

## Raw material

All on the deck in `/tmp/t4-route/` (read-only inputs copied from the share; nothing on the share or in
the repositories was modified):

| file | contents |
|---|---|
| `game`, `t4-patch`, `ServiceDongleApp`, `hhl_mem.dmp` | copies of the read-only inputs |
| `00-listing*.txt` | directory listings, incl. the 5416-line `hasp/` listing |
| `01-elf.txt` | `file`, `readelf -h/-S/-l/-d` for `game` and `t4-patch` |
| `02-syms.txt`, `03-syms2.txt` | dynsym counts, `hasp` calibration, string offsets |
| `04-embed.txt` | embedded-ELF scan, `/dev/*` strings, `.so` names |
| `05-haspdir.txt`, `10-haspdir2.txt`, `16-*` | `hasp/` record statistics and non-32-byte entries |
| `06-sda.txt` | `ServiceDongleApp` symbols and transport strings |
| `07-hardlock.txt` | disassembly of both `/dev/Hardlock` call sites; PLT stubs |
| `11-logincaller.txt` | full disassembly of the game's dongle wrapper `0x081c6a50` |
| `12-transport.txt` | raw transport function, `hasp_login`, ioctl scan, socket scan |
| `14-hl.txt`, `23-final.txt`, `24-syscall-dlopen.txt`, `25-dlopen.txt` | `/tmp/.aksusb` construction, raw bytes, syscall/`dlopen` scans |
| `17-calib.txt` | instrument calibration table |
| `19-chain.txt`, `26-readcallers.txt`, `27-args.txt` | call chain, call sites, `hasp_read`/`hasp_write` file IDs |
| `20-*`, `28-login_ex.txt` | dispatch-pointer negatives, `hhl_mem.dmp` parse, `hasp_login_ex` correction |
| `21-rels.txt` | the seven `R_386_JUMP_SLOT` transport slots |
| `22-byte-identity.txt` | 13-way byte identity against the unstripped twin |
| `/tmp/t4-xref.py`, `/tmp/t4-map.py`, `/tmp/t4-callers.py`, `/tmp/t4-1[5-6].py`, `/tmp/t4-2[0,2,8].py` | the instrument scripts |

Reproducible drivers are also kept next to the workspace tooling as `re/_t4-*.sh` and `re/_t4-*.py`.

---

## Reachability from the entry point

**Choice of file.** This section was **appended to DONGLE-ROUTE.md** rather than split into a
separate DONGLE-BEREIKBAARHEID.md. At 638 lines / 41,786 B the dossier is not too large to
append to safely, and the result is a single coherent document: this section is a direct
continuation of the same address table, and it corrects three entries in it. The append was
verified to be append-only: the first 41,786 bytes hash to 5ea80c6d5442d0fbf6c6d86f6e87b00bac9244c1ffb4a65b47f14ec755485669,
identical to the file before the append.

**Dated:** 2026-10-13 (evening, deck local time; run host clock `2026-10-13T16:19:58Z`).
**Subject:** the same `game`, re-hashed this session. **Raw output:** `/tmp/t4-bereik/` on the deck
(file names given per section; scripts `00-`…`31-`). **Nothing was started, nothing was killed, no
process was touched, and no file outside `/tmp/t4-bereik/` was created.**
**Labels:** **MEASURED** = command with its raw output; **INFERRED** = a conclusion drawn from
measurements.

### 0. Inputs verified before anything else

| file | size | sha256 | source |
|---|---|---|---|
| `…/Terminator Salvation /game` | 7,764,284 | `798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a` | **MEASURED** `sha256sum` on the share, and again on the copy in `/tmp/t4-bereik/` |
| `…/ServiceDongleApp/ServiceDongleApp` | 8,994,378 | `13c7cfd14465b392b1f56ce4ca19cca275c0bf93da312fd993feddf0f126b548` | **MEASURED** |

The hash above **matches the one in the header of this dossier**, so every address already recorded
here remains valid. Two independent confirmations that this is the file that actually runs:

* `/home/deck/.local/share/ravengun/runtime/views/Terminator/game` is a **symlink** to
  `/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game` — **MEASURED**
  (`ls -la`, `file`, `sha256sum`). The runtime view does not hold a second copy.
* A run of this title that happened during this session (`logs-hasp-meting-20261013-1818/RESULT.txt`,
  another lane's run, read only) records `gast sha256 : 798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`
  — **MEASURED** (file read).

`nm game` → `nm: game: no symbols`; `nm -D game` → 1019 lines; `nm ServiceDongleApp` → 17614 lines.
**MEASURED** (`/tmp/t4-bereik/17-nm-game.txt`, `18-nm-D-game.txt`, `19-nm-sda.txt`). Calibration for
the "the stripped binary has no names" claim holds.

### 1. The entry point, and the first structural fact

`e_entry = 0x08057240`. **MEASURED** three ways that agree: `readelf -h` (`00-tools.txt`),
`pyelftools` (`elf.header['e_entry']`, `20-graph-report.txt`), and the section table (`.text` starts
exactly there: va `0x08057240`, size `0x004a31f8`, `21-graph-report`/`11-sections.txt`).

`_start`, complete — **MEASURED** (`objdump -d --start-address=0x08057240`, `20-graph-report.txt`):

```
 8057240:  xor    %ebp,%ebp
 8057242:  pop    %esi
 8057243:  mov    %esp,%ecx
 8057245:  and    $0xfffffff0,%esp
 8057248:  push   %eax
 8057249:  push   %esp
 805724a:  push   %edx
 805724b:  push   $0x84fa330        ; -> __libc_csu_fini
 8057250:  push   $0x84fa340        ; -> __libc_csu_init
 8057255:  push   %ecx
 8057256:  push   %esi
 8057257:  push   $0x80c22d0        ; -> *** main ***
 805725c:  call   8056168 <__libc_start_main@plt>
 8057261:  hlt
```

**`main` = `0x080c22d0`, and it is not a `call` target.** It is the immediate operand of `push` at
`0x08057257`; `__libc_start_main` (in libc, outside this binary) is what invokes it. Three
independent routes agree on this address:

1. **MEASURED** — the `push $0x80c22d0` immediate in `_start`.
2. **MEASURED** — a whole-file raw 32-bit search for `0x080c22d0` returns **exactly one** hit, at
   file offset `0x57258`, i.e. that same immediate (`22-deep-report.txt`, section C). Likewise
   `__libc_csu_init 0x084fa340` and `__libc_csu_fini 0x084fa330` each occur **once**, in `_start`.
3. **MEASURED (external)** — `KETEN-EN-METHODE.md` §2, written earlier and independently, names
   `0x80c22d0` as "de ketenfunctie" (the chain function). It also matches `DongleInitialize()
   FAILED!`'s single reference site (§2 below).

**Consequence — and this is the single most important structural fact in this section:** the strict
`E8`/`E9` call graph from `e_entry` contains **15 instructions in total** — `_start` plus
`__libc_start_main@plt`. It reaches nothing else, because `__libc_start_main@plt` is a PLT stub
(`jmp *GOT`) and `main` is a *pushed argument*, not an edge. **MEASURED** (`20-graph-report.txt` §1:
`A strict (E8/E9 only) : 15`).

So "walk from the entry point by resolving `E8`/`E9` `rel32` targets and only those" cannot be done
literally: it terminates at the libc boundary. This is a property of every glibc `i386` executable,
not a defect of this one, and it is the first and largest hole (§6). The rest of this section
therefore continues from `main = 0x080c22d0`, which is *measured*, not assumed.

### 2. Method, the two instruments, and their calibration

| instrument | what it does | role |
|---|---|---|
| **A — decoded** | `objdump -d` linear sweep of all executable sections → 1,358,809 instructions; edges from `E8` (call), `E9`/`Jcc` (jmp/branch) with real control-flow semantics: `ret`/`hlt`/`ud2` terminate a path, indirect `call *`/`jmp *` yield **no** edge and are recorded as holes | primary; decides instruction boundaries |
| **B — raw byte superset** | scan for `e8`/`e9` at **every** byte offset of every `PT_LOAD`, resolve `rel32` | superset, used to make *negative* results robust: A's edges are a subset of B's |

Both are in `/tmp/t4-bereik/21-graph.py`, `23-main.py`, `24-chain.py`, `27-verify.sh`, `31-mode.sh`;
the disassembly is `10-disasm.txt` (1,360,304 lines, 45,042,286 B, generated in 0.63 s).

**Calibration, positive — the scanner finds what must be there. MEASURED** (`20-graph-report.txt` §3):

| known-called function | decoded-`E8` callers | raw-byte-`E8` hits |
|---|---|---|
| `main` `0x080c22d0` | found via `_start`'s `push` immediate, and its whole 178,617-instruction strict cone is walked | — |
| wrapper `0x081c6a50` | **1** (`0x080ab099`) | **1** (`0x080ab099`) |
| `hasp_login` `0x083bbe60` | **3** (`0x081c6b1d`, `0x081c6c63`, `0x081c6cdf`) | **3**, same |
| `hasp_read` `0x083bad30` | **5** (`0x081c66ae`, `0x081c6787`, `0x081c6854`, `0x081c68ee`, `0x081c6a25`) | **5**, same |
| Hardlock site-2 fn `0x083d3d70` | **2** (`0x083d3ec4`, `0x083d57b3`) | **2**, same |
| `open@plt` `0x08055358` etc. | PLT stubs resolved from the section headers | — |

**Calibration, negative — the scanner returns "not found" for things that are not there. MEASURED**
(same files):

* `hasp_get_info 0x083ba8a0` → **0** callers in *both* instruments. (This is the function this
  dossier's §4 already flagged as having no callers; the instrument reproduces that.)
* `API_HL_CSTACK 0x083d6a50` → **0** `E8` callers, **0** `E9` jmpers, **0** raw dword pointers.
* `0x08048000` (the ELF header) and `0x08577432` (the `"/dev/Hardlock"` **string**) → reported as
  not-reachable code addresses by both instruments, i.e. the scanner does not mistake data for code
  in these cases.

**Calibration, and one real disagreement — this is why instrument A is primary. MEASURED**
(`22-deep-report.txt` §E, `23-main-report.txt` §3, `27-verify.txt`):

The raw-byte instrument reports an `E8` "call" at `0x080ab06f` targeting `0x080ab074`. It is not an
instruction. `0x080ab06f` is the **displacement byte** of the instruction at `0x080ab06d`:

```
 80ab06d:  c7 45 e8 00 00 00 00   movl   $0x0,-0x18(%ebp)
              ^^ the 0xe8 is the disp8 of -0x18, not an opcode
```

The decoded instrument correctly produces no edge there. **So a naive `E8` byte scan invents at least
one edge in this binary**, and a call graph built from it would be unsound in the "reachable" direction.
Instrument A is used for every positive claim below, and every edge in the final chain is re-verified
against raw bytes in §3.

### 3. The chain — and a correction to §2.1 and §4 of this dossier

**Correction 1 — `0x081c6a50` is not the dongle wrapper that runs. It is dead code.**
**MEASURED** (`27-verify.txt` §A5, `22-deep-report.txt` §C):

```
 81c6b5e:  call  free@plt
 81c6b63:  movl  $0x0,(%edi)
 81c6b69:  jmp   81c6a90            ; loops inside 0x081c6a50
 81c6b6e:  66 90                   ; 2-byte NOP = function boundary
 81c6b70:  55                      ; <-- a DIFFERENT function starts here
 81c6b71:  b8 03 00 00 00          mov $0x3,%eax
 81c6b76:  89 e5                   mov %esp,%ebp
 81c6b78:  83 ec 48                sub $0x48,%esp
```

`0x081c6a50` ends at `0x081c6b69` and `0x081c6b70` is a separate function. Therefore this dossier's
statement in §2.1 and §4 — *"wrapper at `0x081c6a50` calls `hasp_login` at `0x081c6b1d`, `0x081c6c63`,
`0x081c6cdf`"* — is **wrong for the last two**. Only `0x081c6b1d` is inside `0x081c6a50`; `0x081c6c63`
and `0x081c6cdf` are inside `0x081c6b70`. The address table in §4 (Rank 1) is unaffected as a list of
`hasp_*` entry points, but **its Rank-1 hook target `0x081c6a50` is the wrong function to hook**.

`0x081c6a50`'s **only** caller is `0x080ab099`, inside the function at `0x080ab020` — and
`0x080ab020` is **unreachable** by every instrument here:

| probe | result | source |
|---|---|---|
| `E8` callers of `0x080ab020`, raw byte, every offset | **0** | **MEASURED** `22-deep-report.txt` §E |
| `E9` jmpers of `0x080ab020`, raw byte | **0** | **MEASURED** same |
| raw 32-bit dwords equal to `0x080ab020` anywhere in the 7.76 MB file | **0** | **MEASURED** `22-deep-report.txt` §C |
| `E8` callers of *any* address in `[0x080aaf00,0x080ab100)` | exactly one, `0x080ab06f -> 0x080ab074` — **and that is the false positive of §2** | **MEASURED** `23-main-report.txt` §3 |
| member of the strict graph from `_start`, from `main`, or from every direct-call target in the binary (graph `C`) | **no** | **MEASURED** `23-main-report.txt` §6, §5 |

The code before `0x080ab020` ends in `jmp 80aaf20` followed by NOP padding (`23-main-report.txt` §4),
so there is no fall-through entry either. **INFERRED (high confidence):** `0x080ab020` — and with it
the dossier's `0x081c6a50` — is unreachable dead code on this entry path. The only way it could run is
an indirect call whose target is computed at runtime; no constant of it exists anywhere in the file.

**Correction 2 — the reachable HASP wrapper is `0x081c6b70`.** **MEASURED** (`27-verify.txt` §C):

```
 target 0x081c6b70 : E8 callers = 0x081c6dac, 0x081c6e4f      (both inside 0x081c6d20)
 target 0x081c6d20 : E8 callers = 0x080aaf6a, 0x080abfc1, 0x080abffd
 target 0x080abf50 : E8 callers = 0x080c232c                   (inside main)
```

**The chain from the entry point to `hasp_login`, every link re-verified against raw bytes**
(**MEASURED** `27-verify.txt` §A, `24-chain-report.txt` §2):

```
_start 0x08057240
  push $0x080c22d0                    68 d0 22 0c 08      ; main, as an immediate (no E8)
  call __libc_start_main@plt          e8 ...              ; libc then calls main
libc __libc_start_main  (outside this binary — HOLE)
  -> __libc_csu_init 0x084fa340       (passed as a push immediate too)
       call _init 0x08055300
  -> main 0x080c22d0

main 0x080c22d0
  0x080c232c  e8 1f 9c fe ff   call 0x080abf50      ; DongleInitialize   [init step #1]
    0x080abf74  call memset@plt
    0x080abf86  call 0x080aa5e0
    0x080abf99  call 0x082f294d   (mtx on 0x087c3460) ; guard: nonzero -> "Failed mtx aldeen: %i"
    0x080abfc1  e8 5a ad 11 00   call 0x081c6d20      ; the "aldeen" constructor
      0x081c6dac / 0x081c6e4f    call 0x081c6b70      (mode-dependent, see §6)
        0x081c6c63  e8 f8 51 1f 00  call 0x083bbe60   ; hasp_login, feature 0xFFFF0000
        0x081c6cdf                   call 0x083bbe60   ; hasp_login, second call
```

Note the immediate immediately before the `hasp_login` call — **MEASURED**:

```
 81c6c5c:  c7 04 24 00 00 ff ff   movl $0xffff0000,(%esp)
 81c6c63:  e8 f8 51 1f 00         call 83bbe60 <hasp_login>
```

`0xFFFF0000` is the classic-range feature ID this dossier's §3.1 recorded. It is confirmed here at the
reachable call site, not only at the unreachable one.

**So: control flow from the entry point *does* reach `hasp_login` statically — but through `0x081c6b70`,
not through the `0x081c6a50` this dossier nominated.**

### 4. Where the dongle sits in the init order

`main` is a flat chain of `call <fn> ; test %eax,%eax ; js <own-failure-block>`, and each failure block
prints its own message, sets a flag and **jumps on to the next step** (so the chain never aborts).
**MEASURED** (`26-names.txt`): I found **51** such `call;js` steps in `main`'s range
`0x080c22d0..0x080c2d00` (indices 0–50), plus 53 further calls in `main` that do not follow that
pattern.

Each step's name was resolved by a **strict, calibrated** instrument: read the instruction *at the
exact failure-target address* from the `objdump` listing, take its `.rodata` immediate, and resolve it
through `strings -a -t x` offsets. Calibration in `26-names.txt`: six known string addresses resolve to
the expected text, and three non-string-start addresses (`0x84feb4f`, `0x84feb00`, `0x8500000`) all
correctly return empty.

| # | call site | target | failure block | message (from `game`'s own `.rodata`) |
|---|---|---|---|---|
| 0 | `0x080c22ec` | `0x082f2eda` | `0x080c32e5` | `Faileed Dbgg init` (supervisor) |
| **1** | **`0x080c232c`** | **`0x080abf50`** | **`0x080c2cdb`** | **`DongleInitialize() FAILED!`** |
| 2 | `0x080c2339` | `0x08065340` | `0x080c2cc3` | `localizationInit() FAILED!` |
| 3 | `0x080c2346` | `0x080ab180` | `0x080c2cab` | `SysInit() FAILED!` |
| 4 | `0x080c2356` | `0x080c2090` | `0x080c2c93` | `IntInit() FAILED!` |
| 5 | `0x080c2366` | `0x0806f490` | `0x080c2c7b` | `MemInit() FAILED!` |
| 6 | `0x080c2376` | `0x0808d3b0` | `0x080c2c63` | `ProcInit() FAILED!` |
| 7 | `0x080c2386` | `0x080c6f40` | `0x080c2c4b` | `FbInit() FAILED!` |
| 8 | `0x080c2396` | `0x080c5010` | `0x080c2c33` | `WndInit() FAILED!` |
| 9 | `0x080c23a6` | `0x080639a0` | `0x080c2c1b` | `InpInit() FAILED!` |
| 10 | `0x080c23b6` | `0x08061cd0` | `0x080c2c03` | `GunInit() FAILED!` |
| 11 | `0x080c23c6` | `0x08057430` | `0x080c2beb` | `CabInit() FAILED!` |
| 12 | `0x080c23d6` | `0x080e8c20` | `0x080c2bd3` | `VidInit() FAILED!` |
| 13 | `0x080c23e6` | `0x080c6220` | `0x080c2bbb` | `DrawInit() FAILED!` |
| 14 | `0x080c23f6` | `0x080ca150` | `0x080c2ba3` | `FsInit() FAILED!` |
| 15 | `0x080c2406` | `0x080cb880` | `0x080c2b8b` | `NetInit() FAILED!` |
| 16 | `0x080c2416` | `0x080a7530` | `0x080c2b73` | `SndInit() FAILED!` |
| 17 | `0x080c2426` | `0x080944b0` | `0x080c2b5b` | `ResInit() FAILED!` |
| 18 | `0x080c2436` | `0x0807ba90` | `0x080c2b43` | `ObjInit() FAILED!` |
| 19 | `0x080c2446` | `0x080b1820` | `0x080c2b2b` | `TriggersInit() FAILED!` |
| 20 | `0x080c2456` | `0x0806fca0` | `0x080c2b13` | `NBLoadInit() FAILED!` |
| 21 | `0x080c2466` | `0x080c7620` | `0x080c2afb` | `FontInit() FAILED!` |
| 22 | `0x080c2476` | `0x081b9bb0` | `0x080c2ae3` | `PhysXInit() FAILED!` |
| 23 | `0x080c2486` | `0x081b6480` | `0x080c2acb` | `DispatchInit() FAILED!` |
| 24 | `0x080c2496` | `0x0817f7c0` | `0x080c2ab3` | `HardwareDataInit() FAILED!` |
| 25 | `0x080c24a6` | `0x080c1e50` | `0x080c2a9b` | `PostFxInit() FAILED!` |
| 26 | `0x080c24b6` | `0x0810c9e0` | `0x080c2a83` | `gameInit() FAILED!` |
| 27–50 | `0x080c2630` … `0x080c2a39` | — | — | (failure blocks carry no string) |

The names and the numbering 0–26 reproduce `KETEN-EN-METHODE.md` §2 exactly and independently, and
extend it (that document's step 26 failure target `0x80c2a83` and call site `0x80c24b6` are the same
addresses I measure). The one difference: **that document says 49 steps; I measure 51** `call;js` steps
in this range. The step *numbers* 0–26 agree, so this is a counting difference, not a numbering one.
**MEASURED** for my count; the "49" is not reproducible from my instrument.

**Boundaries that answer the question:**

| boundary | where |
|---|---|
| supervisor / first action | step **0**, `0x080c22ec` |
| **the dongle init** | **step 1**, `0x080c232c` → `DongleInitialize` `0x080abf50` |
| framebuffer + window | steps **7** (`FbInit`) and **8** (`WndInit`) — *after* the dongle |
| video / draw init | steps **12** (`VidInit`), **13** (`DrawInit`) — *after* the dongle |
| filesystem init | step **14** (`FsInit`) — *after* the dongle |
| PhysX | step **22** (`PhysXInit`) — *after* the dongle |
| the second big init (`gameInit`, the telemetry init of `KETEN-EN-METHODE.md` §3) | step **26**, `0x080c24b6` → `0x0810c9e0` |
| **the last thing before the observed exit** | `glutDisplayFunc` at `0x080c2a5e` → `0x08056f58` and `glutMainLoop` at `0x080c2a63` → `0x080565f8` — both **outside** the `call;js` chain, after step 50 |

**The observed exit, measured — not assumed.** The run of this title that happened during this session
left `logs-hasp-meting-20261013-1818/RESULT.txt` (**MEASURED**, file read):

```
exitcode       : 0
levensduur     : 4 s
lader sha256   : 7f83edb291f0065c54122392679ef0ffe65190c909d596485e56460e32ac4549
gast sha256    : 798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a
aanroep-regels : 0
```

and `KETEN-EN-METHODE.md` §1 records the mechanism: all steps succeed, then at draw time a switch read
at `0x080c2a49` is non-zero and `jne 0x080c319d` jumps **past** `glutDisplayFunc` (`0x080c2a5e`) and
`glutMainLoop` (`0x080c2a63`) — no window, no image, `exit 0`. The `glutDisplayFunc`/`glutMainLoop`
call sites in that record are byte-identical to the two addresses I measure as call #77/#78 in `main`
(`23-main-report.txt` §1). **INFERRED:** the observed exit is the draw-time skip, and it happens
**after** step 1.

So: **in init order the dongle sits first, long before the exit.** `DongleInitialize` is called
unconditionally as `main`'s third call, ~4,300 bytes and 50 steps before the draw attempt.

### 5. The two transports, separately

**`/tmp/.aksusb` — REACHABLE. MEASURED** (`24-chain-report.txt` §2, `27-verify.txt` §A6/A7, §D):

```
hasp_login 0x083bbe60
  0x083bbe7c  je / 0x083bbebb call 0x083c75a0
    0x083c75e6  call 0x083bc900            ; HASP-HL session layer
      ... 0x083d7a3b call 0x083d6b20
        0x083d6b31  call getenv@plt        ; "AKS_API_COMM_TIMEOUT"  (a TIMEOUT, not a transport choice)
        0x083d6b3d  je 0x083d6b6b           ; env unset -> skip timeout setup, still call
        0x083d6b6d  e8 fe fe ff ff  call 0x083d6a70
          0x083d6a7d  call socket@plt      ; socket(AF_UNIX=1, SOCK_STREAM=1, 0)
          0x083d6aa3  movl $0x706d742f      ; "/tmp"
          0x083d6aab  movl $0x6b612e2f      ; "/.ak"
          0x083d6ab3  movl $0x62737573      ; "susb"
          0x083d6acd  e8 56 ed c7 ff  call connect@plt
```

Both raw bytes and the three unique immediates confirm the dossier's §2(b) exactly. Reachable from
`main`: **YES**; and also from `hasp_login`, from `hasp_get_sessioninfo` and from `hasp_read`
(**MEASURED** `27-verify.txt` §D). Its `connect` call site `0x083d6acd` is reachable.

**`/dev/Hardlock`, site 1 (`0x083c5f10`) — REACHABLE. MEASURED** (`27-verify.txt` §A8, §D):

```
 83c6b0e:  68 32 74 57 08   push $0x8577432   ; "/dev/Hardlock"
 83c6b13:  e8 40 e8 c8 ff   call open@plt     ; O_RDWR
 83c6b48:  68 12 d1 fe 02   push $0x2fed112   ; the ioctl request
 83c6b4e:  e8 45 04 c9 ff   call ioctl@plt
```

`0x083c5f10` is a genuine function start — its own prologue `55 57 56 53 81 ec 70 02 00 00`, preceded
by `ret` + NOP (**MEASURED** `28-final.txt` §A1/A3). Reachable from `main`: **YES**. The `open` call
site `0x083c6b13` and the `ioctl` call site `0x083c6b4e` are both reachable (**MEASURED**
`27-verify.txt` §7). Its `E8` caller is `0x083ba85d`, reached from `hasp_get_sessioninfo 0x083bae80`
via `0x083ba5b0`.

**`/dev/Hardlock`, site 2 (`0x083d3d70`) — NOT REACHABLE. MEASURED** (`27-verify.txt` §A9, §7):

```
 83d3d70:  55 57 56 53 83 ec 14      ; full prologue
 83d3d81:  68 32 74 57 08            push $0x8577432   ; "/dev/Hardlock"
 83d3d86:  e8 cd 15 c8 ff            call open@plt
 83d3dd2:  68 12 d1 fe 02            push $0x2fed112
 83d3dd8:  e8 bb 31 c8 ff            call ioctl@plt
```

Its `E8` callers are `0x083d3ec4` and `0x083d57b3`; **neither is in `main`'s strict reachable set**,
and `0x083d3d70` itself is absent from the strict graph, the generous graph and the widest graph.
**MEASURED.** So this second, self-contained copy of the classic transport is **dead code**.

**What selects between them? MEASURED, and it is not a compile-time or `getenv` switch:**

* Both transports are present in one binary and both are statically reachable — there is **no**
  compile-time gate.
* The only `getenv` on the aksusb path reads **`AKS_API_COMM_TIMEOUT`** (`0x0857753d`, **MEASURED**
  `30-strings.txt`), and the branch it guards only skips a `strtoul` timeout setup
  (`0x083d6b3d je 0x083d6b6b` → falls through to the `connect` call either way). It does **not**
  choose a transport.
* The two transports belong to two **different HASP APIs the game calls from two different places**:
  `hasp_login 0x083bbe60` reaches `/tmp/.aksusb`, and `hasp_get_sessioninfo 0x083bae80` reaches
  `/dev/Hardlock` site 1. **MEASURED** (`27-verify.txt` §D). They are not alternatives for one
  operation; the game asks for different things.
* **INFERRED:** the effective selection is the runtime state of the HASP client and of the guards in
  §6 — i.e. which API call is reached and what the client's internal branches decide — not any
  compile-time or environment switch I can see. I found no `nethasp.ini` reader on either path.

**Correction 3 — dossier §7 item 2 is now resolved, and §2(b)'s address was slightly off.**
`API_HL_CSTACK 0x083d6a50` is a **6-instruction function that ends at `0x083d6a65`**
(`sub $0x18,%esp; …; call 0x083d6190; …; ret` — **MEASURED** `24-chain-report.txt` §3). The
`/tmp/.aksusb` builder is the **separate function `0x083d6a70`**, and it **is** `E8`-called, from
`0x083d6b6d` and `0x083d6d36` (**MEASURED** `27-verify.txt` §C). So the "last hop" this dossier
recorded as *INFERRED* because `0x083d6a50` had 0 callers is now **MEASURED**: the hop is
`0x083d6b6d → 0x083d6a70 → connect("/tmp/.aksusb")` at `0x083d6acd`.

### 6. The holes — what this call graph cannot see

1. **The libc boundary at the very start.** `main` is reached because libc's `__libc_start_main`
   calls a *pushed argument*. Nothing in this binary calls `main`. Any E8/E9-only graph from `e_entry`
   is empty by construction (§1). **MEASURED.**
2. **Indirect calls — 1,486 sites inside `main`'s strict reachable set. MEASURED**
   (`24-chain-report.txt` §5). The dominant ones are loads from a table in `.data`:

   | slot | count | note |
   |---|---|---|
   | `0x0878db04` | **439** | `.data`; initial dword `0x08056518` (non-zero) |
   | `0x0878db08` | 118 | `.data` |
   | `0x0878db10` | 79 | `.data` |
   | `0x0878db0c` | 28 | `.data` |
   | `0x0878db14` | 20 | `.data` |
   | `call *%eax` / `*(%ebx)` / `*%edx` … | 110 / 59 / 53 … | C++ vtable dispatch |

   These are function pointers read from memory; the graph cannot follow them. **Any function reached
   only through one of them is invisible to this analysis.**
3. **But the chain that matters is not hidden by them.** The shortest path `main → hasp_login` contains
   **0** indirect sites (**MEASURED** `24-chain-report.txt` §4) — it is 122 instructions of direct
   `call`s. The same holds for the paths to `aksusb` and to Hardlock site 1 in the filtered listings.
   So the HASP path is *not* reached through a hole; it is reached through plain `E8`s.
4. **The one hole that would matter** is the entry into `0x080ab020` (the function that calls the dead
   `0x081c6a50`). It must be an indirect call with a runtime-computed target: there is no `E8`, no
   `E9`, and no constant of it anywhere in the 7.76 MB file. **MEASURED** for all three negatives.
   I cannot exclude a computed jump, but there is no evidence of one.
5. **The C++ static-initialiser hole is closed by measurement.** `__do_global_ctors_aux 0x084fa410`
   calls the two `.ctors` entries through `call *%eax` (**MEASURED** `22-deep-report.txt` §A/§C,
   `26-names.txt`): `.ctors` at `0x08598d7c` = `{ -1, 0x081b9900, 0x08484740, 0 }`. I walked both
   entries anyway: from `0x08484740` → 34 instructions; from `0x081b9900` → 539 instructions
   (615 with immediate pointers). **Neither reaches the wrapper or `hasp_login`** (**MEASURED**
   `23-main-report.txt` §6). Also: there is **no `.init_array` section and no `DT_INIT_ARRAY` tag**
   (only `DT_INIT 0x08055300` and `.ctors`, 16 bytes), and `__libc_csu_init 0x084fa340` iterates an
   **empty** array — its two `lea` operands are literally the same bytes `8d 83 8f f6 ff ff`, so the
   loop count is 0 (**MEASURED** `26-names.txt`, `22-deep-report.txt` §B).
6. **Runtime guards inside the chain are not resolvable statically.** `DongleInitialize 0x080abf50`
   reaches the "aldeen" constructor `0x081c6d20` only if `call 0x082f294d(0x087c3460)` returns 0
   (`0x080abfa0 jne 0x080ac177`, whose block prints **`Failed mtx aldeen: %i`**), and `0x081c6d20`
   itself branches at `0x081c6d75` and calls `0x081c6b70` at either `0x081c6dac` or `0x081c6e4f`; and
   `0x081c6b70` has a chain of branches (`0x081c6b89`, `0x081c6b96`, `0x081c6ba1`, `0x081c6c33`,
   `0x081c6c42`, `0x081c6c4f`) before the `hasp_login` call at `0x081c6c63`. All **MEASURED** as code
   (`29-dongleinit.txt`, `31-mode.txt`); which branch is taken is not decidable without a run.

### 7. The runtime evidence, and why "0 calls" is *not* proof of unreachability

The same `logs-hasp-meting-20261013-1818/` run contains a hook log. **MEASURED** (file read,
`B.hasp.log`):

```
[ravengun-hasphook] 0 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
[ravengun-hasphook]   hasp_login           0x083bbe60  GELDIG: ... prologue 53 83 ec 08
[ravengun-hasphook]   hasp_read            0x083bad30  GELDIG: ... prologue 57 56 53 83
[ravengun-hasphook]   hasp_get_sessioninfo 0x083bae80  GELDIG: ... prologue 55 57 56 53
[ravengun-hasphook]   hasp_free            0x083baab0  AFGEWEZEN: eerste bytes e9 ab 22 00 zijn
                                                     geen prologue (mogelijk al door een andere hook gepatcht)
[ravengun-hasphook]   aangehaakt: hasp_login ... 10 functies aangehaakt
```

with `aanroep-regels : 0` in `RESULT.txt`, and `[ravengun-hasp] RAVENGUN_HASP_GAME is niet gezet;
geen dongle geladen` in `B.out`.

Two conclusions, and they pull in opposite directions — both must be stated:

* **The instrument is calibrated and it works.** It installed 10 hooks at byte-verified addresses and
  it *correctly refused* `hasp_free 0x083baab0` because that address begins `e9 ab 22 00 00`, i.e. it
  is a `jmp rel32` alias and not a function — exactly what this dossier's §4 predicted. So
  "0 `aanroep` lines" is **not** an uncalibrated empty result in the sense of a broken hook.
* **Therefore "0 `aanroep` lines" says the HASP API was not *executed*.** It does **not** say the code
  is unreachable, and it must not be read that way: a name-based hook cannot fire on a statically
  linked client at all, which is this dossier's own §4 conclusion. The correct reading is narrower and
  more useful: **the API entry points were patched and never entered, so in this run the game never
  asked.**

**Synthesis (INFERRED, high confidence):** the dongle code is **statically reachable and positioned
first** in init order, but in the run we have it was **not executed** — the game exits at the draw-time
skip without ever calling `hasp_login`. That is consistent with this project's own earlier finding
(`DRAAIBOEK-BENCH.md:242`, `KETEN-EN-METHODE.md` §1): the block lies **before** the HASP API, in the
engine's own dongle object or in the missing `/g3/`-data/resource maker. The `"aldeen"` strings
measured in `DongleInitialize` — `Failed mtx aldeen: %i` (`0x084FCDC2`), `Failed aldeen: %i`
(`0x084FCDDA`), `Failed to close aldeen: %i`, `Failed to reinit aldeen: %i`, `Failed when releasing
dh: %d` (**MEASURED** `30-strings.txt`) — show the game has its own dongle-object layer *above* the
HASP API, and that layer is where the decision is taken.

### 8. Verdict

**Not reached in the run we have** — with one unambiguous sub-answer and one correction:

* The dossier's wrapper **`0x081c6a50` is not reachable from the entry path at all.** Its only caller
  `0x080ab099` lives in `0x080ab020`, which has no `E8` caller, no `E9` jmp, no data pointer anywhere
  in the file, and no fall-through entry. It is **dead code**. Confidence: **high** — three independent
  instruments agree on the negative, and the positive control (that the same instruments *do* find
  `hasp_login`, `hasp_read` and the wrapper's single caller) is in §2.
* **The HASP client and both transports are statically reachable from `e_entry`, before the observed
  exit** — but through a *different* wrapper, `0x081c6b70`, reached from `main` step **1**
  (`DongleInitialize 0x080abf50` → `0x081c6d20` → `0x081c6b70` → `hasp_login 0x083bbe60`), i.e. 50
  init steps and ~4,300 bytes of `main` before the `glutDisplayFunc`/`glutMainLoop` pair that the exit
  skips. Confidence: **high** for the static chain — every link is re-verified against raw bytes in §3.
* **But the API was not executed in the run we have**: a hook that demonstrably installed 10 hooks at
  byte-verified addresses recorded 0 invocations, with `exitcode 0` after 4 s. Confidence:
  **medium-high** — the hook's own log shows an asynchronous fallback (`synchroon niet gelukt; de
  kijker-thread neemt het over`), so I cannot fully exclude a logging artefact (§9), and I was not
  permitted to run a positive control.

**What this means for the work plan.** Both Rank-1 and Rank-2 of §4 are premature. Rank 1 as written
targets `0x081c6a50`, which never runs; the reachable wrapper is `0x081c6b70`. And even the correct
wrapper is not entered in the current run, so dongle emulation — of either transport — cannot change
anything yet. The blocker to attack first is the one this project already named: the layer **above**
the HASP API (`Failed aldeen` / `Failed mtx aldeen`) and the missing `/g3/`-data resource. A dongle
emulator becomes worth building only once `aanroep` lines appear.

### 9. Could not determine

1. **Which runtime guard stops the chain.** The code path from `DongleInitialize` to `hasp_login`
   contains at least nine conditional branches (`0x080abfa0`, `0x080abfc8`, `0x080abfdc`, `0x081c6d36`,
   `0x081c6d75`, `0x081c6d8c`, `0x081c6b89`, `0x081c6ba1`, `0x081c6c42`, `0x081c6c4f`). Which one is
   taken in the observed run is not decidable statically, and the run's log does not name it. Naming
   it needs a run with a **calibrated positive control** — a hook that is proven to fire on a call
   known to happen in the same run. I was not permitted to start anything.
2. **Whether the hook's zero count is a logging artefact.** `B.hasp.log` reports
   `synchroon niet gelukt; de kijker-thread neemt het over` — an asynchronous fallback. If that thread
   is what writes invocation lines, a 4-second process could in principle exit before flushing. I
   cannot exclude it without re-running, and I did not.
3. **Whether `0x080ab020` (and hence the dossier's `0x081c6a50`) is reachable by a computed indirect
   call.** No constant of either address exists anywhere in the file, so there is no evidence for it —
   but a target computed at runtime from arithmetic would leave no constant, and my graph cannot
   follow it. This is the only way the dossier's Rank-1 target could still be live.
4. **The 439-way dispatch through `.data:0x0878db04`.** I measured the site count and the slot's
   non-zero initial dword, but not what populates the table at runtime, nor which of its 439 call
   sites can reach HASP code. Any function reached only through it is invisible here.
5. **`main`'s 53 non-chain calls.** They are outside the `call;test;js` pattern and I did not classify
   them; some may be part of the init order the "49 steps" figure was counting, which would explain the
   49-versus-51 difference.
6. **The exact contents of the `0x3ad`-byte buffer** that `DongleInitialize` passes to
   `0x081c6d20` at `0x080abfae` and that `0x081c6d20` `memcpy`s into the container. That buffer is what
   the HASP login is fed; its producer is the "aldeen" layer, which is where §7 says the blocker is.
   Not traced.
7. **Whether `AKS_API_COMM_TIMEOUT` is set in the bench environment.** It only affects a timeout, not
   the transport choice, but I did not measure the environment of the run.

### Raw material for this section

All on the deck under `/tmp/t4-bereik/` (read-only inputs copied from the share; nothing on the share
or in `~/projects/ravengun` was modified):

| file | contents |
|---|---|
| `game`, `ServiceDongleApp` | copies of the read-only inputs, hashes in §0 |
| `00-tools.sh`, `00-tools.txt` | host, tool inventory (objdump 2.46.1, readelf 2.46.1, pyelftools 0.33, **no capstone**), `readelf -h` |
| `01-disasm.sh`, `01-disasm.log`, `10-disasm.txt` | the full linear-sweep disassembly (1,360,304 lines) and the fact-gathering run |
| `11-sections.txt` … `19-nm-sda.txt` | sections, program headers, dynamic tags, relocations, dynsyms, twin symbols, `nm` calibration |
| `21-graph.py`, `20-graph-report.txt` | the three graphs (strict / generous / widest), caller sets, holes, paths |
| `22-deep.py`, `22-deep-report.txt` | sections, `.ctors`, raw dword search, function-pointer-table detector, caller sets |
| `23-main.py`, `23-main-report.txt` | `main`'s 104 calls, pre-main initialisers, widest-graph paths |
| `24-chain.py`, `24-chain-report.txt` | the 51-step chain, paths to both transports, the getenv selector, holes |
| `26-names.sh`, `26-names.txt`, `26-strings.txt` | the strict, calibrated init-step name table |
| `27-verify.sh`, `27-verify.txt` | raw bytes of every critical edge; dispatch slots; two-transport separation matrix |
| `28-final.sh`, `28-final.txt` | function-start check, path from `hasp_login` to the Hardlock `open`, exit evidence |
| `29-dongleinit.sh`, `29-dongleinit.txt` | `DongleInitialize` disassembly, its guards and failback blocks |
| `30-strings.sh`, `30-strings.txt` | `aldeen` / mutex / failback strings, with a 3-address calibration |
| `31-mode.sh`, `31-mode.txt` | the mode argument at all three call sites of `0x081c6d20`; the `container[0]` branch |
| `25-debug.sh`, `25-debug.txt` | the instrument error recorded in §2 and its diagnosis |

Every script listed above is self-contained and reproducible from `/tmp/t4-bereik/` with
`sh <name>.sh` or `python3 <name>.py`; they read only the copied `game` and `ServiceDongleApp`.
