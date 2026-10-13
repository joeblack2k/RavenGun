# ANTWOORDENSET-GESLOTEN-CORE — per-function evidence for the 17 `am` detours in `VACUUM.exe`

**What this is.** A verification pass on the claim: *"under the closed reference core, 17 prologues in
`VACUUM.exe` are detoured (8 `amDongle*`, 5 `amHm*`, 4 `amMaster*`) into a 4096-byte `rwxp` page at
`0x13fff0000`, and all 17 answers are 0 except `amDongleGetNetworkAddress`, which returns
`192.168.1.0` (`0x1a8c0`)."*

**How this pass was done.** Read-only. No game, launcher, emulator, container, wine or podman was
started, nothing was killed, nothing under `~/projects/ravengun` or
`~/projects/ravengun-patches/patches/LuigisMansion/dropin/` was touched. Exactly one new file was
created in the repository: this one. Everything else was written under `/tmp/antwoordset/`.

**Labels.** **MEASURED** = I ran the command shown and the raw output is here or in the referenced
`/tmp/antwoordset/` file. **INFERRED** = a conclusion drawn from those measurements, not itself read
from bytes.

---

## 0. Verdict in one screen

| claim | verdict |
|---|---|
| the referenced dump still exists | **yes** — `/tmp/b0-geheugen-b/image-gesloten.bin`, 19 259 392 B, sha256 `11bb710b7c71686b…`, the same sha the analysis cites. Nothing had been deleted. **MEASURED** |
| number of detoured prologues in the image | **18**, not 17. 17 are `am` functions; the 18th is at RVA `0x485a` and is not an `am` function. **MEASURED** |
| 8 / 5 / 4 split over the 17 | **correct** (8 `amDongle*`, 5 `amHm*`, 4 `amMaster*`). **MEASURED** |
| all 17 rerouted into the 4096-byte page at `0x13fff0000` | **correct**. Page is `rwxp`, anonymous, and contains 18 (A,B) slot pairs. **MEASURED** |
| the page is a two-slot detour table (A = return to the original body, B = the closed-core implementation) | **correct** for all 18 pairs. **MEASURED** |
| all 17 answers are 0 | **15 of 17 rows**: 4 measured unambiguously, 11 where the `xor eax,eax; … ret` bytes are measured inside the function but the path to them is not (INFERRED). **2 rows (1 and 6) have no return-value evidence at all** |
| the odd one out writes `0x1a8c0` | **correct, but two corrections**: the function **returns 0** and *writes* the address through its output pointer; and the write is **conditional** — it writes either a global dword or the constant `0x1a8c0`. **MEASURED** |
| summary wording "returns 192.168.1.0" | **imprecise** — it returns 0. The address goes into `*rcx`. **MEASURED** |

Return-value column of the table, condensed:

```
0   amDongleGetGameId              INFERRED  (epilogue bytes measured, path not traceable)
0   amDongleGetModelType           INFERRED
0   amDongleGetNetworkAddress       MEASURED  (whole function decodes; both branches return 0)
0   amDongleInit                   INFERRED
0   amDongleSetupKeychip           INFERRED
0   amDongleUpdate                 INFERRED
0   amHmExit                       INFERRED
0   amHmGetCautionFlag             INFERRED
0   amHmInit                       INFERRED
0   amHmResetCautionFlag           INFERRED
0   amHmUpdate                     MEASURED  (whole function = mov [rsp+8],ecx; xor eax,eax; ret)
0   amMasterExitInternal           INFERRED
0   amMasterInit                   INFERRED
0   amMasterSetBackupEventLogParam MEASURED  (whole function = save 4 args; xor eax,eax; ret)
0   amMasterUpdate                 MEASURED  (whole function = mov [rsp+8],cl; xor eax,eax; ret)
?   amDongleBillingGetKeychipId    NOT ESTABLISHED (its epilogue has no xor eax,eax)
?   amDongleIsAvailableInternal    NOT ESTABLISHED (no xor eax,eax in a 1024-byte window)
```

So the summary "all 17 are 0" is **not fully verified**: it holds for 15 rows with the strength
indicated, and for the other 2 rows there is no evidence of any return value at all.

---

## 1. Sources, with size and sha256

All paths are on the deck (`deck@192.168.2.247`). All were read, none written.

```
11bb710b7c71686b44157f3e5f14b56df5922c2aca6f50c1d99758c1cf37d172  19259392  /tmp/b0-geheugen-b/image-gesloten.bin
f12cf907e07e083716bf031ea5486212529b9ac14f7ffe6a13fa875f3e553685  132699    /tmp/b0-geheugen-b/maps-gesloten.txt
6b71eae91a769a51aa51049bd43a935b4f2108460c3184ec76e418bb4ab2bd3d  1634      /tmp/b0-geheugen-b/samenvatting-gesloten.txt
9d611636fa6841d214c25f7d9db0346c0fa9a278bd92e7bf5834de14cd09624b  2656      /tmp/b0-geheugen-b/gesloten.log
4dda7a4b5c78ee3da17e633e96042aecb006a264ba0a55ac789bcb69f9a1b2f7  218       /tmp/b0-geheugen-b/vergelijking.txt
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-meet-gesloten/tramp-10.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-meet-gesloten/tramp-12.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-meet-gesloten/tramp-16.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-meet-gesloten/tramp-20.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-meet-gesloten/tramp-25.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-pak-gesloten/tramp-12.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-pak-gesloten/tramp-18.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-kern2/tramp-12.0.bin
28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321  4096      /tmp/lm-kern2/tramp-15.0.bin
ede0b675bdd157d55358c89f0e2b711dbcd4b70893480954494902fa8f3fe636  2656      /tmp/lm-meet-gesloten/run.log
5ab4b8180f92eedff2f6b14987a73cc39a031f6f0e44c6e73475de3bab6dec26  47876     /tmp/lm-meet-gesloten/meting-huidig.txt
f85b1992c1e9102714cf86718158f764f5c71dca2f279eceab6ec1a218f46e6d  42878     /tmp/lm-meet-kern/meting-huidig.txt
d8fd31abc8cf2611f3f0336f052ca609d48153edfc6688f2a27cf9da8663116a  65        /tmp/lm-meet-gesloten/werkelijk-sha
d8fd31abc8cf2611f3f0336f052ca609d48153edfc6688f2a27cf9da8663116a  65        /tmp/lm-meet-kern/werkelijk-sha
6ff01930107178601baf62cf69dd840f0977953979cefcabd8fd7defd5da31b0  3496500   /tmp/vacuum-dump/text.bin
10e2103ee73921931a7828ebdf325d3a3a64c7a90cc1da5c0ca6fe17b1e3dd78  1056      /tmp/lm-pak-gesloten/amMaster-12.0.bin
076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560  512       /tmp/lm-pak-gesloten/dongle-12.0.bin
52716d9ec0146e81a2de62a733a8322af0a79d6d91e8323df1b4d160af7a1d15  43332     /home/deck/projects/ravengun-patches/patches/LuigisMansion/AM-LAAG.md
```

Kernel-implementation windows used (each `<name>` is one of the 17 function names):

```
/tmp/lm-pak-gesloten/kern-<name>-12.0.bin   1024 B   (run 1, closed core)
/tmp/lm-kern2/kern-<name>-15.0.bin          1024 B   (run 2, closed core)
/tmp/lm-meet-kern/kern-<name>-25.0.bin       192 B   (run 3, closed core)
```

Per-file sha256 (1024-byte windows) — **MEASURED**. The two 1024-byte runs are byte-identical per
function, which is a check on its own:

```
18b138b4cce3d63f497aecf6e08747895195a8109b5d5dc2d8f3b10d5ba52abe  kern-amDongleBillingGetKeychipId-12.0.bin  (= -15.0.bin)
5e55674875014dc63ee41d07df3790127638503a03fcc132fb7ed03412e37d17  kern-amDongleGetGameId-12.0.bin            (= -15.0.bin)
7007f3571c0c27242013e51f74116fb302368f86b196d25d83b7d934a2a45bb5  kern-amDongleGetModelType-12.0.bin         (= -15.0.bin)
ff63a94476c29ec6408eb2f0e328a28e95df25255db05e0c2baf76a4b8354dc6  kern-amDongleGetNetworkAddress-12.0.bin    (= -15.0.bin)
6781f2d34a82b177ac38898bc5fe97786312e9becc2e8c12433bb1e2e2616273  kern-amDongleInit-12.0.bin                 (= -15.0.bin)
0a8be9d7a270acf996b76fac1e51598f365da6374acdd9de6943f1e4e79412e9  kern-amDongleIsAvailableInternal-12.0.bin  (= -15.0.bin)
ee7c670160166a0c3e3a624d81edc11c000e96dd47308705fbde739e7015785a  kern-amDongleSetupKeychip-12.0.bin         (= -15.0.bin)
12dd260cc8c82141f82cd60b3b95880567af4ec709ac048adb2bc6a66fceca6c  kern-amDongleUpdate-12.0.bin               (= -15.0.bin)
c574dbbeea2f4ff95afd33d2a6de038bd9e69b2c86a31999c803909f0a01aa6a  kern-amHmExit-12.0.bin                     (= -15.0.bin)
e041ae0b6eacf4dd23801dd66a3f608e4634c4848c48940d8fe154b8fa302559  kern-amHmGetCautionFlag-12.0.bin           (= -15.0.bin)
300e4661302276bf9fc4908296a8d6f4cf717acde5c02d45cfd18f8b2544a80c  kern-amHmInit-12.0.bin                     (= -15.0.bin)
662f6e4269ad84205719b8e21be36f9273f697adb85ee241ae1939f9a0a32f11  kern-amHmResetCautionFlag-12.0.bin         (= -15.0.bin)
ab0c8a7fbb818bcb62720a81defa2876b37039d225ded99cd82ad44cb86743f0  kern-amHmUpdate-12.0.bin                   (= -15.0.bin)
680c6f2111e7e2a28623cc7820e43ad6721dfdaab324cd58e90377433c695bff  kern-amMasterExitInternal-12.0.bin         (= -15.0.bin)
c82e3bc63acf8d6d2b23102187ec9fd331d7fdb08ac899d354231861a7450675  kern-amMasterInit-12.0.bin                 (= -15.0.bin)
ce4eab44b48c315ed90e23029b84723fab81106f73ed9a4dd861c087e17fcc7d  kern-amMasterSetBackupEventLogParam-12.0.bin (= -15.0.bin)
0b727da8c6735a0e042b955299cedb42a4cc09bff32371773bf0051db723b63b  kern-amMasterUpdate-12.0.bin               (= -15.0.bin)
49550f146626c8cbaf394baf0ece884f75f48bd4415774a4fa620bb63039d623  kern-niet-am 0x485a-12.0.bin               (= -15.0.bin)
```

**Which run is which.** `/tmp/b0-geheugen-b/gesloten.log` and `/tmp/lm-meet-gesloten/run.log` both
name the core `TeknoParrot64.dll sha256 f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`
**MEASURED** — two independent launch logs, so this fact is two-source.

**The image dump is not the page.** `image-gesloten.bin` is the game module only
(`0x140000000..0x14125e000`). The page at `0x13fff0000` lies *below* the image base and is therefore
**not** in that file; it comes from the `tramp-*.bin` dumps. **MEASURED** (the scanner reads
`/tmp/b0-geheugen-b/image-gesloten.bin` and finds 18 `e9` instructions pointing into a range that the
file itself does not cover — see §2).

---

## 2. Controls

### 2.1 Instrument self-test

The whole method rests on one instrument: *read the image, treat `offset == RVA`, and find
`e9 rel32` whose target lands in `0x13fff0000..0x13fff1000`*. Self-test: the file is read and hashed
before use.

```
$ python3 /tmp/antwoordset/scan.py
### instrument self-test: file read
IMG /tmp/b0-geheugen-b/image-gesloten.bin size=19259392 sha256=11bb710b7c71686b44157f3e5f14b56df5922c2aca6f50c1d99758c1cf37d172
```

Full output: `/tmp/antwoordset/scan.out`; the hit list also as `/tmp/antwoordset/jump-hits.txt`.

### 2.2 Positive control — the instrument finds the non-zero answer

Two separate positives were demanded: the instrument must find (a) a detour, and (b) the non-zero
answer.

**(a)** The scan finds the 18 detour sites — a positive of the right *kind*. **MEASURED**

```
total hits = 18
  RVA 0x0000485a  abs 0x14000485a  bytes e9 2f c3 fe ff  -> 0x13fff0b8e
  RVA 0x002b3b70  abs 0x1402b3b70  bytes e9 5f d4 d3 ff  -> 0x13fff0fd4
  RVA 0x002b3d30  abs 0x1402b3d30  bytes e9 9f d1 d3 ff  -> 0x13fff0ed4
  RVA 0x002b3ff0  abs 0x1402b3ff0  bytes e9 9f ce d3 ff  -> 0x13fff0e94
  RVA 0x002b40b0  abs 0x1402b40b0  bytes e9 9f cd d3 ff  -> 0x13fff0e54
  RVA 0x002b4170  abs 0x1402b4170  bytes e9 df cd d3 ff  -> 0x13fff0f54
  RVA 0x002b4270  abs 0x1402b4270  bytes e9 5e c9 d3 ff  -> 0x13fff0bd3
  RVA 0x002b4320  abs 0x1402b4320  bytes e9 6f cc d3 ff  -> 0x13fff0f94
  RVA 0x002b4740  abs 0x1402b4740  bytes e9 d4 c7 d3 ff  -> 0x13fff0f19
  RVA 0x002b50f0  abs 0x1402b50f0  bytes e9 a4 bb d3 ff  -> 0x13fff0c99
  RVA 0x002b5180  abs 0x1402b5180  bytes e9 94 bb d3 ff  -> 0x13fff0d19
  RVA 0x002b5200  abs 0x1402b5200  bytes e9 14 ba d3 ff  -> 0x13fff0c19
  RVA 0x002b55a0  abs 0x1402b55a0  bytes e9 34 b7 d3 ff  -> 0x13fff0cd9
  RVA 0x002b56f0  abs 0x1402b56f0  bytes e9 5f b5 d3 ff  -> 0x13fff0c54
  RVA 0x002b5860  abs 0x1402b5860  bytes e9 72 b5 d3 ff  -> 0x13fff0dd7
  RVA 0x002b5cb0  abs 0x1402b5cb0  bytes e9 5f b1 d3 ff  -> 0x13fff0e14
  RVA 0x002b6050  abs 0x1402b6050  bytes e9 00 ad d3 ff  -> 0x13fff0d55
  RVA 0x002b6160  abs 0x1402b6160  bytes e9 34 ac d3 ff  -> 0x13fff0d99
```

**(b)** The non-zero answer. The instrument that reads the *closed-core* bytes is the kernel-window
reader. I searched the literal `c0 a8 01 00` (little-endian `0x1a8c0`) inside **each of the 17
implementations' own span** `[start, next implementation start)` in the 1024-byte window file:

```
$ python3 /tmp/antwoordset/poscontrol.py
   amDongleIsAvailableInternal      span 0x6ffff6c97d50..0x6ffff6caff50 (98816 B)  literal at ABSENT
   amHmUpdate                       span 0x6ffff6caff50..0x6ffff6cc2130 (74208 B)  literal at ABSENT
   amMasterUpdate                   span 0x6ffff6cc2130..0x6ffff6cfdd80 (244816 B)  literal at ABSENT
   amDongleBillingGetKeychipId      span 0x6ffff6cfdd80..0x6ffff6cfe040 (704 B)  literal at ABSENT
   amDongleGetGameId                span 0x6ffff6cfe040..0x6ffff6cfe0a0 (96 B)  literal at ABSENT
   amDongleGetModelType             span 0x6ffff6cfe0a0..0x6ffff6cfe0e0 (64 B)  literal at ABSENT
   amDongleGetNetworkAddress        span 0x6ffff6cfe0e0..0x6ffff6cfe1f0 (272 B)  literal at +0x40
   amDongleInit                     span 0x6ffff6cfe1f0..0x6ffff6cfe2a0 (176 B)  literal at ABSENT
   amDongleSetupKeychip             span 0x6ffff6cfe2a0..0x6ffff6cfe3e0 (320 B)  literal at ABSENT
   amDongleUpdate                   span 0x6ffff6cfe3e0..0x6ffff6d00520 (8512 B)  literal at ABSENT
   amHmExit                         span 0x6ffff6d00520..0x6ffff6d00550 (48 B)  literal at ABSENT
   amHmGetCautionFlag               span 0x6ffff6d00550..0x6ffff6d00590 (64 B)  literal at ABSENT
   amHmInit                         span 0x6ffff6d00590..0x6ffff6d005c0 (48 B)  literal at ABSENT
   amHmResetCautionFlag             span 0x6ffff6d005c0..0x6ffff6d062e0 (23840 B)  literal at ABSENT
   amMasterExitInternal             span 0x6ffff6d062e0..0x6ffff6d06320 (64 B)  literal at ABSENT
   amMasterSetBackupEventLogParam   span 0x6ffff6d06320..0x6ffff6d063d0 (176 B)  literal at ABSENT
   amMasterInit                     span 0x6ffff6d063d0..0x6ffff6d065d0 (512 B)  literal at ABSENT

### and the two bytes-fields of the same instruction
   window bytes +0x38..+0x48: 0b 48 8b 44 24 30 c7 00 c0 a8 01 00 33 c0 48 83
```

So exactly **1 of 17** implementations carries `0x1a8c0`, at offset `+0x40`, and the bytes carrying it
are the immediate of `mov DWORD PTR [rax],0x1a8c0` (disassembly in §4.4). The negative side of this
control is strong: 16 spans, several of them 70 kB–240 kB, contain the literal zero times.

The literal is **absent from the game image** as well, so it cannot have been found by accident in the
game's own data:

```
### 6. OCURRENCES of the positive-control literal
   little-endian c0 a8 01 00 in the game image: 0 occurrence(s) []
   big-endian 1a 8c 00 00 in the game image: 0 occurrence(s) []
```

### 2.3 Negative control — a plausible address that is *not* detoured

I picked three `am`-layer functions that are *not* in the 17. Their start was found without using the
detour list: scan backwards from a `lea` reference to that function's own log-tag string to the
preceding `int3` padding, then read the first bytes.

```
$ python3 /tmp/antwoordset/kern2.py
### B. NEGATIVE CONTROL: non-detoured am functions, function start found by
###    scanning back from a lea reference to the preceding int3 padding (cc).
  amBackupInit                             ref 0x2b315a    start 0x2b308d    bytes cc cc cc 48 83 ec 28 83 3d a5 ab 22 00 00 75 0a  detoured=NO
  amHmGetCpuTempStatus                     ref 0x2b4f8b    start 0x2b4f5c    bytes cc cc cc cc 40 53 48 83 ec 20 48 8b d9 48 8d 54  detoured=NO
  amHmSaveHwInfo                           ref 0x2b56a5    start 0x2b562d    bytes cc cc cc 48 89 5c 24 10 57 48 81 ec 30 02 00 00  detoured=NO
```

All three have authentic MSVC prologues (`48 83 ec 28` / `40 53 48 83 ec 20` / `48 89 5c 24 10 57`)
and none begins with `e9`. The same test applied to the 17 returns `detoured=YES` for all of them, so
"17 of 17" is a measured number, not a number I cannot account for: the instrument reports positives
and negatives correctly on the same code region.

The instrument's own accounting is also complete: the whole-image scan found **exactly 18** hits, and
each hit is accounted for — 17 `am` functions plus RVA `0x485a`. **MEASURED**

---

## 3. The 17 rows

Naming method (`lea rdx,[rip+…]` → the function's own log tag inside its own body) — **MEASURED**, see
§5.2. Addresses: RVA and the absolute address used (`ImageBase 0x140000000`). "Byte proof" is the
kernel implementation; all offsets are `+` from the implementation start.

| # | name | RVA | absolute | slot B (page) | kernel impl | return value | byte proof | strength |
|---|---|---|---|---|---|---|---|---|
| 1 | `amDongleBillingGetKeychipId` | `0x002b3b70` | `0x1402b3b70` | `0x13fff0fd4` | `0x6ffff6cfdd80` | **NOT ESTABLISHED** | epilogue `48 81 c4 58 01 00 00 5f 5e c3` at `+0x118`; the only `33 c0` before it (`+0x101`) is the stack-cookie check | — |
| 2 | `amDongleGetGameId` | `0x002b3d30` | `0x1402b3d30` | `0x13fff0ed4` | `0x6ffff6cfe040` | **0** | `33 c0 48 83 c4 28 c3` at `+0x32`, then `cc` | INFERRED |
| 3 | `amDongleGetModelType` | `0x002b3ff0` | `0x1402b3ff0` | `0x13fff0e94` | `0x6ffff6cfe0a0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x33`, then `cc` | INFERRED |
| 4 | `amDongleGetNetworkAddress` | `0x002b40b0` | `0x1402b40b0` | `0x13fff0e54` | `0x6ffff6cfe0e0` | **0**, and writes through the out-pointer | whole function decodes; both branches end at `33 c0 48 83 c4 28 c3` (`+0x44`); `mov DWORD PTR [rax],0x1a8c0` at `+0x3e` | **MEASURED** |
| 5 | `amDongleInit` | `0x002b4170` | `0x1402b4170` | `0x13fff0f54` | `0x6ffff6cfe1f0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x29`, next function starts at `+0x30` | INFERRED |
| 6 | `amDongleIsAvailableInternal` | `0x002b4270` | `0x1402b4270` | `0x13fff0bd3` | `0x6ffff6c97d50` | **NOT ESTABLISHED** | epilogue `48 83 c4 38 c3` at `+0x58`; **no `33 c0` anywhere in 1024 bytes** | — |
| 7 | `amDongleSetupKeychip` | `0x002b4320` | `0x1402b4320` | `0x13fff0f94` | `0x6ffff6cfe2a0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x12f`, `cc` at `+0x136` | INFERRED |
| 8 | `amDongleUpdate` | `0x002b4740` | `0x1402b4740` | `0x13fff0f19` | `0x6ffff6cfe3e0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x1c`, `cc` at `+0x23` | INFERRED |
| 9 | `amHmExit` | `0x002b50f0` | `0x1402b50f0` | `0x13fff0c99` | `0x6ffff6d00520` | **0** | `33 c0 48 83 c4 28 c3` at `+0x29`, next function starts at `+0x30` | INFERRED |
| 10 | `amHmGetCautionFlag` | `0x002b5180` | `0x1402b5180` | `0x13fff0d19` | `0x6ffff6d00550` | **0** | `33 c0 48 83 c4 28 c3` at `+0x32`, `cc` at `+0x39` | INFERRED |
| 11 | `amHmInit` | `0x002b5200` | `0x1402b5200` | `0x13fff0c19` | `0x6ffff6d00590` | **0** | `33 c0 48 83 c4 28 c3` at `+0x29`, next function starts at `+0x30` | INFERRED |
| 12 | `amHmResetCautionFlag` | `0x002b55a0` | `0x1402b55a0` | `0x13fff0cd9` | `0x6ffff6d005c0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x2a`, `cc` at `+0x31` | INFERRED |
| 13 | `amHmUpdate` | `0x002b56f0` | `0x1402b56f0` | `0x13fff0c54` | `0x6ffff6caff50` | **0** | whole function `89 4c 24 08 33 c0 c3` = `mov [rsp+8],ecx; xor eax,eax; ret` | **MEASURED** |
| 14 | `amMasterExitInternal` | `0x002b5860` | `0x1402b5860` | `0x13fff0dd7` | `0x6ffff6d062e0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x32`, `cc` at `+0x39` | INFERRED |
| 15 | `amMasterInit` | `0x002b5cb0` | `0x1402b5cb0` | `0x13fff0e14` | `0x6ffff6d063d0` | **0** | `33 c0 48 83 c4 28 c3` at `+0x29` (ret at `+0x2f`), a new prologue `89 54 24 10 48 89 4c 24 08 48 83 ec 28` at `+0x30` | INFERRED |
| 16 | `amMasterSetBackupEventLogParam` | `0x002b6050` | `0x1402b6050` | `0x13fff0d55` | `0x6ffff6d06320` | **0** | whole function `44 89 4c 24 20 44 89 44 24 18 89 54 24 10 89 4c 24 08 33 c0 c3` | **MEASURED** |
| 17 | `amMasterUpdate` | `0x002b6160` | `0x1402b6160` | `0x13fff0d99` | `0x6ffff6cc2130` | **0** | whole function `88 4c 24 08 33 c0 c3` = `mov [rsp+8],cl; xor eax,eax; ret` | **MEASURED** |
| — | (not an `am` function) | `0x0000485a` | `0x14000485a` | `0x13fff0b8e` | `0x6ffff6c979a0` | not part of the claim | — | — |

The 18th site (`0x485a`) is *not* an `am` function: its body's tags are `Bishamon`/`Effects/bishamon/`
strings, and its A-slot target is `0x140e74327` (RVA `0xe74327`, inside `.capcom0`), not `start+N` like
the 17. **MEASURED**

### 3.1 Why 11 rows are INFERRED and not MEASURED

For 12 of the 17 implementations the body is `… ; lea rcx,[<log string in the core>] ; call|jmp
<helper far inside the core>` and then, at a small offset, the 6-byte group
`33 c0 48 83 c4 28 c3` followed by either `cc cc cc` padding or the next function's prologue. That the
group is the function's own epilogue is well supported — the padding / next prologue fixes the function
boundary, and the boundary is confirmed independently by the next *known* implementation address
(e.g. `amHmExit +0x30` = `amHmGetCautionFlag`).

What is **not** established is the *path* from the entry to that epilogue. Linear disassembly of the
bytes between the log call and the epilogue produces instruction streams that cannot be the executing
code: wild indirect jumps, `in eax,0x7e`, `syscall`, writes through uninitialised registers. Example,
`amDongleGetGameId` (`objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x6ffff6cfe040
--start-address=0x6ffff6cfe040 --stop-address=0x6ffff6cfe079 …`):

```
6ffff6cfe040: 89 54 24 10          mov    DWORD PTR [rsp+0x10],edx
6ffff6cfe044: 48 89 4c 24 08       mov    QWORD PTR [rsp+0x8],rcx
6ffff6cfe049: 48 83 ec 28          sub    rsp,0x28
6ffff6cfe04d: 48 8d 0d cc e2 12 00 lea    rcx,[rip+0x12e2cc]        # 0x6ffff6e2c320
6ffff6cfe054: e8 72 2d 4e 02       call   0x6ffff91e0dcb
6ffff6cfe059: 3e 47                ds rex.RXB
6ffff6cfe05b: 65 7e 53             gs jle 0x6ffff6cfe0b1
6ffff6cfe05e: 58                   pop    rax
6ffff6cfe05f: 36 a4                ss movs BYTE PTR [rdi],BYTE PTR [rsi]
6ffff6cfe061: 9e                   sahf
6ffff6cfe062: e5 7e                in     eax,0x7e
6ffff6cfe064: 4d 55                rex.WRB push r13
6ffff6cfe066: 2b 16                sub    edx,DWORD PTR [rsi]
6ffff6cfe068: a6                   cmps   BYTE PTR [rsi],BYTE PTR [rdi]
6ffff6cfe069: ff 21                jmp    QWORD PTR [rcx]
6ffff6cfe06b: 95                   xchg   ebp,eax
6ffff6cfe06c: d1 4f 41             ror    DWORD PTR [rdi+0x41],1
6ffff6cfe06f: 59                   pop    rcx
6ffff6cfe070: b7 c5                mov    bh,0xc5
6ffff6cfe072: 33 c0                xor    eax,eax
6ffff6cfe074: 48 83 c4 28          add    rsp,0x28
6ffff6cfe078: c3                   ret
```

`0x6ffff91e0dcb` is a real mapped address (it is inside the core's anonymous executable arena
`0x6ffff6fec000..0x6ffff9f46000`, listed in `samenvatting-gesloten.txt`), but that arena was never
dumped, so the callee and its return value are unknown. Whatever the mechanism is — inserted junk
code, or a copy of the body that is not the executing image — **the trace from entry to `ret` is not
readable**, and a reader who took the `33 c0 … ret` group as proof would be reading a *pattern*, not a
path. This is exactly the weakness that was inherited from the prose analysis; I am reporting it
instead of repeating it as MEASURED.

This is also the reason two rows come out as NOT ESTABLISHED: for `amDongleBillingGetKeychipId` even
the *pattern* is absent (its epilogue is the stack-cookie teardown, no `xor eax,eax`), and for
`amDongleIsAvailableInternal` there is no `33 c0` anywhere in a 1024-byte window.

---

## 4. Traced return values, with the bytes

`objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=<impl addr> … <window>`; full listings in
`/tmp/antwoordset/dis-<name>.txt`.

### 4.1 Fully decodable stubs (MEASURED, no call at all)

```
amHmUpdate          0x6ffff6caff50   window /tmp/lm-pak-gesloten/kern-amHmUpdate-12.0.bin
    6ffff6caff50: 89 4c 24 08      mov DWORD PTR [rsp+0x8],ecx
    6ffff6caff54: 33 c0            xor eax,eax
    6ffff6caff56: c3               ret
    -> eax = 0 ; padding cc cc cc cc cc at +0x7

amMasterUpdate      0x6ffff6cc2130   window /tmp/lm-pak-gesloten/kern-amMasterUpdate-12.0.bin
    6ffff6cc2130: 88 4c 24 08      mov BYTE PTR [rsp+0x8],cl
    6ffff6cc2134: 33 c0            xor eax,eax
    6ffff6cc2136: c3               ret
    -> eax = 0 ; padding at +0x7

amMasterSetBackupEventLogParam   0x6ffff6d06320   window …/kern-amMasterSetBackupEventLogParam-12.0.bin
    6ffff6d06320: 44 89 4c 24 20   mov DWORD PTR [rsp+0x20],r9d
    6ffff6d06325: 44 89 44 24 18   mov DWORD PTR [rsp+0x18],r8d
    6ffff6d0632a: 89 54 24 10      mov DWORD PTR [rsp+0x10],edx
    6ffff6d0632e: 89 4c 24 08      mov DWORD PTR [rsp+0x8],ecx
    6ffff6d06332: 33 c0            xor eax,eax
    6ffff6d06334: c3               ret
    -> eax = 0 ; padding at +0x15
```

### 4.2 The positive control, in full (MEASURED)

```
amDongleGetNetworkAddress   0x6ffff6cfe0e0   window /tmp/lm-pak-gesloten/kern-amDongleGetNetworkAddress-12.0.bin
    6ffff6cfe0e0: 89 54 24 10           mov    DWORD PTR [rsp+0x10],edx
    6ffff6cfe0e4: 48 89 4c 24 08        mov    QWORD PTR [rsp+0x8],rcx
    6ffff6cfe0e9: 48 83 ec 28           sub    rsp,0x28
    6ffff6cfe0ed: 0f b6 05 0d dd 28 00  movzx  eax,BYTE PTR [rip+0x28dd0d]   # 0x6ffff6f8be01
    6ffff6cfe0f4: 85 c0                 test   eax,eax
    6ffff6cfe0f6: 74 21                 je     0x6ffff6cfe119
    6ffff6cfe0f8: 8b 15 4e 05 1f 00     mov    edx,DWORD PTR [rip+0x1f054e]   # 0x6ffff6eee64c
    6ffff6cfe0fe: 48 8d 0d b3 e1 12 00  lea    rcx,[rip+0x12e1b3]            # 0x6ffff6e2c2b8
    6ffff6cfe105: e8 56 07 04 00        call   0x6ffff6d3e860
    6ffff6cfe10a: 48 8b 44 24 30        mov    rax,QWORD PTR [rsp+0x30]      ; = the caller's rcx
    6ffff6cfe10f: 8b 0d 37 05 1f 00     mov    ecx,DWORD PTR [rip+0x1f0537]   # 0x6ffff6eee64c
    6ffff6cfe115: 89 08                 mov    DWORD PTR [rax],ecx
    6ffff6cfe117: eb 0b                 jmp    0x6ffff6cfe124
    6ffff6cfe119: 48 8b 44 24 30        mov    rax,QWORD PTR [rsp+0x30]
    6ffff6cfe11e: c7 00 c0 a8 01 00     mov    DWORD PTR [rax],0x1a8c0      ; <-- 192.168.1.0
    6ffff6cfe124: 33 c0                 xor    eax,eax
    6ffff6cfe126: 48 83 c4 28           add    rsp,0x28
    6ffff6cfe12a: c3                    ret
```

Three facts follow, all **MEASURED**:

1. `eax = 0` on **both** branches. The function itself does not return the address.
2. The address is written through the caller's second argument (`rcx`, saved at `[rsp+0x8]` and reloaded
   at `[rsp+0x30]` after the `sub rsp,0x28`) — `*(uint32_t*)rcx`.
3. The write is **conditional**. If the byte at `0x6ffff6f8be01` is non-zero the function writes the
   dword at `0x6ffff6eee64c` instead of the constant. Both of those addresses lie in the core region
   `0x6ffff6fae000..0x6ffff6fe6000`, which **was never dumped**, so which branch ran in any of these
   runs is unknown (see §8).

The analysis in `AM-LAAG.md` §5.1 writes this row as "`*(uint32*)rcx = 192.168.1.0`"; that matches the
`je`-taken branch, and the sentence "returns 192.168.1.0" in the summary is not what the bytes say.

### 4.3 The two rows whose return value is not established

```
amDongleBillingGetKeychipId   0x6ffff6cfdd80   window …-12.0.bin (1024 B)
    entry        : 89 54 24 10 48 89 4c 24 08 56 57 48 81 ec 58 01 00 00
                   48 8b 05 bf 5f 1f 00 48 33 c4 48 89 84 24 40 01 00 00   ; stack cookie
    +0x22        : 48 8d 0d 8d e5 12 00  lea rcx,[rip+0x12e58d]   # 0x6ffff6e2c338
    +0x29        : e8 80 4d bd 00        call 0x6ffff78d2b30
    +0x101       : 33 c0                xor eax,eax          <-- NOT the return: followed by
                   48 8b 8c 24 40 01 00 00                    the cookie reload
    end at +0x11d: 48 81 c4 58 01 00 00 5f 5e c3   = add rsp,0x158; pop rdi; pop rsi; ret
                   cc cc cc at +0x11d
    -> the epilogue does not zero eax; no `33 c0 c3` / `33 c0 48 83 c4` before it.

amDongleIsAvailableInternal   0x6ffff6c97d50   window …-12.0.bin (1024 B)
    entry        : 48 83 ec 38           sub rsp,0x38
                   48 8d 0d f5 22 18 00  lea rcx,[rip+0x1822f5]  # 0x6ffff6e1a050
                   e9 61 18 c5 00        jmp 0x6ffff78e95c1         ; tail jump to a helper
    end at +0x5c : 48 83 c4 38 c3        add rsp,0x38; ret
                   cc cc cc at +0x5c
    -> scan of all 1024 bytes: **no `33 c0` at all**.
```

`AM-LAAG.md` §7 flags this second row itself ("waarde niet met zekerheid gelezen") — that caution is
correct and I confirm it. It does **not** flag `amDongleBillingGetKeychipId`, whose cited evidence is
`33 c0 c3` at `+0x17c`; `+0x17c` is *past this function's end* (`+0x11d`), i.e. it is a different
function's stub inside the same 1024-byte window. **MEASURED** (the padding at `+0x11d` and the
epilogue at `+0x118` are in `/tmp/antwoordset/kern3.out`).

For the same reason the offsets `AM-LAAG.md` §7 lists are first-occurrence-by-pattern, not
function-scoped: e.g. it gives `amDongleInit +0x241` while that function ends at `+0x2f`; the correct
in-function group is at `+0x29`. **MEASURED**

---

## 5. The page at `0x13fff0000`

### 5.1 Identity and permissions

```
$ grep -n "13fff" /tmp/b0-geheugen-b/maps-gesloten.txt
99:13fff0000-13fff1000 rwxp 00000000 00:00 0
```

**MEASURED, single source** (this maps file only; the *content* of the page has three independent
runs behind it, but the *permissions* line exists once). The same file's `samenvatting-gesloten.txt`
lists it among the anonymous executable regions:

```
anonieme uitvoerbare gebieden: 19
  0x13fff0000-0x13fff1000       4096 B
```

### 5.2 The name → address mapping

Not from an export table and not from the prose. Each of the 17 contains, inside its own body, a
`lea rdx,[rip+…]` to a C string in `.rdata` that is exactly its own name. **MEASURED**, from
`/tmp/antwoordset/scan.out`:

```
  -- function 0x002b3b70 (amDongleBillingGetKeychipId), range [0x2b3b70,0x2b3d30)
     0x002b3b8d lea rdx -> 0x14036a8a0  "amDongleBillingGetKeychipId"
  -- function 0x002b3d30 (amDongleGetGameId)
     0x002b3d7a lea rdx -> 0x14036a838  "amDongleGetGameId"
  -- function 0x002b3ff0 (amDongleGetModelType)
     0x002b4038 lea rdx -> 0x14036a868  "amDongleGetModelType"
  -- function 0x002b40b0 (amDongleGetNetworkAddress)
     0x002b40f8 lea rdx -> 0x14036a880  "amDongleGetNetworkAddress"
  -- function 0x002b4170 (amDongleInit)
     0x002b41cc lea rdx -> 0x14036a6a0  "amDongleInit"
  -- function 0x002b4270 (amDongleIsAvailableInternal)
     0x002b42c7 lea rdx -> 0x14036a7f8  "amDongleIsAvailableInternal"
  -- function 0x002b4320 (amDongleSetupKeychip)
     0x002b4384 lea rdx -> 0x14036a738  "amDongleSetupKeychip"
  -- function 0x002b4740 (amDongleUpdate)
     0x002b498b lea rdx -> 0x14036a700  "amDongleUpdate"
  -- function 0x002b50f0 (amHmExit)
     0x002b510d lea rdx -> 0x14036aa48  "amHmExit"
  -- function 0x002b5180 (amHmGetCautionFlag)
     0x002b519d lea rdx -> 0x14036aab0  "amHmGetCautionFlag"
  -- function 0x002b5200 (amHmInit)
     0x002b5229 lea rdx -> 0x14036a988  "amHmInit"
  -- function 0x002b55a0 (amHmResetCautionFlag)
     0x002b55bd lea rdx -> 0x14036aac8  "amHmResetCautionFlag"
  -- function 0x002b56f0 (amHmUpdate)
     0x002b5711 lea rdx -> 0x14036aa80  "amHmUpdate"
  -- function 0x002b5860 (amMasterExitInternal)
     0x002b58b9 lea rdx -> 0x14036ada0  "amMasterExitA"
     0x002b5962 lea rdx -> 0x14036adb0  "amMasterExitInternal"   (+5 more references)
  -- function 0x002b5cb0 (amMasterInit)
     0x002b5d11 lea rdx -> 0x14036ad20  "amMasterInit"
  -- function 0x002b6050 (amMasterSetBackupEventLogParam)
     0x002b6075 lea rdx -> 0x14036af60  "amMasterSetBackupEventLogParam"
  -- function 0x002b6160 (amMasterUpdate)
     0x002b6187 lea rdx -> 0x14036afd0  "amMasterUpdate"
```

Row 14 is the one soft spot: the range `[0x2b5860, 0x2b5cb0)` contains `"amMasterExitA"` once and
`"amMasterExitInternal"` six times, so the name is a best assignment, not proof. **MEASURED /
single-source for that row.** The addresses and jump targets are hard.

The same scan also shows that the ranges contain references to *other* functions' tags
(`amHmGetCpuTempStatus`, `amHmLoadHwInfo`, `amMasterOutputLog`, …) because the range
`[start, next start)` spans functions that are not detoured; that does not affect the 17 mappings above,
each of which has at least one reference to exactly its own name.

### 5.3 Structure of the 4096 bytes

**MEASURED** — `/tmp/antwoordset/page-struct.out`, `/tmp/antwoordset/header.out`,
`/tmp/antwoordset/page.out`.

```
page 0x13fff0000 .. 0x13fff0fff, 4096 B, sha256 28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321
non-zero bytes: 610   (0x000..0xb7f: 216 ; 0xb80..0xfff: 394)
```

The page has **two different structures**, and neither of them is a table of answers:

**(a) `0x000..0xb7f` — allocator/slab metadata (data, not code). 216 non-zero bytes.**

```
0x13fff0000  qword 0x6ffffec10000      (a 4096-byte mapped core region: 0x6ffffec10000-0x6ffffec11000)
0x13fff0008  qword 0x13fff0b40         (a pointer into this page)
0x13fff0010  qword 0x12 = 18           (a count — and there are exactly 18 slots below)
0x13fff0040  qword 0
0x13fff0080 -> 0x13fff0040
0x13fff00c0 -> 0x13fff0080
0x13fff0100 -> 0x13fff00c0
 ... every 0x40 bytes, each first qword pointing at the block 0x40 bytes below ...
0x13fff0b40 -> 0x13fff0b00
```

That is a singly linked chain of 44 blocks of 0x40 bytes, terminating on the zero at
`0x13fff0040`, sitting directly under the 18 slots. It is bookkeeping; it is not executed and it
carries no dongle value.

**(b) `0x13fff0b80..0x13fff0fdf` — 18 detour pairs. 394 non-zero bytes.**

`page.py` finds exactly 36 slots matching `ff 25 00 00 00 00 <qword>` (`jmp qword ptr [rip+0]`), and
they form 18 pairs with the layout `A = B - 0x0e`:

```
A  <replay of the stolen original prologue, relocated> ; ff 25 00 00 00 00 <ptr to game body start+N>
B  ff 25 00 00 00 00 <ptr to the closed-core implementation>
```

The prologue of each game function jumps to **B**. Verified for all 18: the game's `e9` target always
equals a B slot, and the matching A slot's qword always equals `0x140000000 + RVA + N` where N is the
length of the replayed prefix. **MEASURED**

Full pair table (measured):

```
function                         A slot       N   replay (original prologue, relocated)   A target     B target
amDongleBillingGetKeychipId      0x13fff0fc6  6   40 53 48 83 ec 20                        0x1402b3b76  0x6ffff6cfdd80
amDongleGetGameId                0x13fff0ec6  6   40 53 48 83 ec 20                        0x1402b3d36  0x6ffff6cfe040
amDongleGetModelType             0x13fff0e86  6   40 53 48 83 ec 20                        0x1402b3ff6  0x6ffff6cfe0a0
amDongleGetNetworkAddress        0x13fff0e46  6   40 53 48 83 ec 20                        0x1402b40b6  0x6ffff6cfe0e0
amDongleInit                     0x13fff0f46  6   40 53 48 83 ec 20                        0x1402b4176  0x6ffff6cfe1f0
amDongleIsAvailableInternal      0x13fff0bc5  5   cc cc cc cc cc                           0x1402b4275  0x6ffff6c97d50
amDongleSetupKeychip             0x13fff0f86  6   40 53 48 83 ec 20                        0x1402b4326  0x6ffff6cfe2a0
amDongleUpdate                   0x13fff0f0b  11  48 83 ec 28 48 8b 0d 5d bd 4a 00         0x1402b474b  0x6ffff6cfe3e0
amHmExit                         0x13fff0c8b  11  48 83 ec 28 83 3d 25 da 4e 00 00         0x1402b50fb  0x6ffff6d00520
amHmGetCautionFlag               0x13fff0d0b  11  48 83 ec 28 83 3d a5 d9 4e 00 00         0x1402b518b  0x6ffff6d00550
amHmInit                         0x13fff0c0b  11  48 83 ec 28 48 8d 0d 25 9d 37 00         0x1402b520b  0x6ffff6d00590
amHmResetCautionFlag             0x13fff0ccb  11  48 83 ec 28 83 3d e5 d9 4e 00 00         0x1402b55ab  0x6ffff6d005c0
amHmUpdate                       0x13fff0c46  6   40 53 48 83 ec 20                        0x1402b56f6  0x6ffff6caff50
amMasterExitInternal             0x13fff0dc9  9   40 53 48 81 ec c0 01 00 00               0x1402b5869  0x6ffff6d062e0
amMasterInit                     0x13fff0e06  6   40 53 48 83 ec 20                        0x1402b5cb6  0x6ffff6d063d0
amMasterSetBackupEventLogParam   0x13fff0d47  7   48 83 ec 38 44 8b d2                     0x1402b6057  0x6ffff6d06320
amMasterUpdate                   0x13fff0d8b  11  48 83 ec 28 48 8b 0d 4d bf 4a 00         0x1402b616b  0x6ffff6cc2130
(non-am, 0x485a)                 0x13fff0b80  -   (none)                                   0x140e74327  0x6ffff6c979a0
```

Raw dump of the used part, one line per 16 bytes (`/tmp/antwoordset/page-struct.out` has all of it):

```
13fff0b80  ff 25 00 00 00 00 27 43 e7 40 01 00 00 00 ff 25   <- A (+base) / B (non-am) start
13fff0b90  00 00 00 00 a0 79 c9 f6 ff 6f 00 00 00 00 00 00   <- B qword 0x6ffff6c979a0
13fff0bc0  cc cc cc cc cc ff 25 00 00 00 00 75 42 2b 40 01   <- A of amDongleIsAvailableInternal (replay = int3!)
13fff0bd0  00 00 00 ff 25 00 00 00 00 50 7d c9 f6 ff 6f 00   <- B qword 0x6ffff6c97d50
13fff0c00  48 83 ec 28 48 8d 0d 25 9d 37 00 ff 25 00 00 00   <- A of amHmInit: relocated prologue + jmp
13fff0c10  00 0b 52 2b 40 01 00 00 00 ff 25 00 00 00 00 90   <- A qword 0x1402b520b ; B opcode
13fff0c20  05 d0 f6 ff 6f 00 00 00 00 00 00 00 00 00 00 00   <- B qword 0x6ffff6d00590
...
13fff0fd0  01 00 00 00 ff 25 00 00 00 00 80 dd cf f6 ff 6f   <- last pair
13fff0fe0  00 ... (all zero to 0x13fff0fff)
```

**Code vs data in the page.**

| part | bytes | what |
|---|---|---|
| 36 × `ff 25 00 00 00 00` | 216 | **code** (`jmp qword ptr [rip+0]`) |
| 36 × 8-byte absolute pointer | 288 | **data** (addresses; 18 game bodies, 18 core implementations) |
| 17 × replayed prologue | 135 | **code** (the stolen instructions, relocated: RIP-relative displacements recomputed) |
| 0x000..0xb7f slab metadata | 216 non-zero | **data** |
| **return values** | **0** | **the page carries no dongle answer of any kind** |

The page was searched for the positive-control literal: `c0 a8 01 00` occurs **0 times** in 4096 bytes.
**MEASURED** The answers are not in the page — they are in the closed-core module, which is why the
"4096-byte page" is a *detour table*, not a return-value table.

### 5.4 Two independent sources for the page content

The page is byte-identical at five sample times in one run and across two further runs:

```
/tmp/lm-meet-gesloten/tramp-10.0.bin   28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321
/tmp/lm-meet-gesloten/tramp-12.0.bin   28bce6a918b5a3cfaf0b23af3650c14206d84f974eb6bfbc56357d1ea9ef0321
/tmp/lm-meet-gesloten/tramp-16.0.bin   28bce6a9…  (identical)
/tmp/lm-meet-gesloten/tramp-20.0.bin   28bce6a9…  (identical)
/tmp/lm-meet-gesloten/tramp-25.0.bin   28bce6a9…  (identical)
/tmp/lm-pak-gesloten/tramp-12.0.bin    28bce6a9…  (different run)
/tmp/lm-pak-gesloten/tramp-18.0.bin    28bce6a9…  (different run)
/tmp/lm-kern2/tramp-12.0.bin           28bce6a9…  (different run)
/tmp/lm-kern2/tramp-15.0.bin           28bce6a9…  (different run)
```

That the core implementations live at the same addresses in three separate runs is therefore
**two-source** (page qwords + the `kern-*` windows taken at those addresses, which decode to sane stubs
and hash identically across two runs).

### 5.5 Two independent sources for the original prologues

The page's A slots replay the stolen prologue *relocated*; the game dump still holds the un-overwritten
tail of it after the 5-byte `e9`. They agree. **MEASURED** (`/tmp/antwoordset/xcheck3.out`):

```
   amDongleUpdate           replay instr at +4 len 7 -> 0x14049cc68 ; dump tail -> 0x14049cc68  MATCH
   amHmInit                 replay instr at +4 len 7 -> 0x14036a930 ; dump tail -> 0x14036a930  MATCH
   amMasterUpdate           replay instr at +4 len 7 -> 0x14049ccd8 ; dump tail -> 0x14049ccd8  MATCH
```

(`amHmExit`, `amHmGetCautionFlag`, `amHmResetCautionFlag` resolve to `0x1404de6b0` from the page; in
the dump that instruction's 7-byte form is split by the 5-byte overwrite so the standalone tail no
longer decodes as RIP-relative — hence "n/a" in the script, not a disagreement. The page value
`0x1404de6b0` is the `amHm` global that `AM-LAAG.md` §3.4 mentions.)

This also corrects a detail in `AM-LAAG.md` §4: its `amHmInit` hexdump labels the `jmp` as being at
`0x13fff0c0a`; measured, the slot is at `0x13fff0c0b` (the `sub rsp,0x28` occupies `0x13fff0c00..c03`,
so the `lea` starts at `0xc04`, not `0xc03`). Their address column is off by one from the second line
on; the resolved target `0x14036a930` is correct and matches my measurement. **MEASURED**

### 5.6 Two anomalies around `amDongleIsAvailableInternal` (single-source, partly unexplained)

**(a) The hooked address is inside `int3` padding, and the real body is 0x20 bytes later.**

```
0x002b4250  00 00 48 89 88 ac 01 00 00 8b cb 48 83 c4 20 5b e9 db f9 ff ff cc cc cc cc cc cc cc cc cc cc cc
0x002b4270  e9 5e c9 d3 ff cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc
0x002b4280  cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc 40 53 48 83 ec 20 48 8b 0d cb 89 1e 00 4c 8d 41
0x002b4290  40 53 48 83 ec 20 48 8b 0d cb 89 1e 00 4c 8d 41 10 48 8d 91 e8 01 00 00 48 83 c1 08 e8 2f 79 00
```

**MEASURED**: the previous function ends with `e9 db f9 ff ff` at `0x2b4269`, then `int3` runs from
`0x2b426e` to `0x2b428f` (34 bytes), and the function body named `amDongleIsAvailableInternal` starts at
`0x2b4290` (`40 53 48 83 ec 20 48 8b 0d cb 89 1e 00 …`, the same prologue `AM-LAAG.md` §3.2 shows for
"body from `0x002b4290`"). The closed core's hook is at `0x2b4270`, i.e. **inside the padding**, not at
the body. `AM-LAAG.md` lists the function start as `0x002b4270` in §1 and its body as "from
`0x002b4290`" in §3.2 without noting the discrepancy.

**(b) `0x2b4270` is nonetheless really called, and `0x2b4290` is called too.**

```
### the two call sites that reach 0x2b4270, verified by hand
   at 0x000ab8b9 : e8 b2 89 20 00  rel32=2132402 -> 0x1402b4270
   at 0x001349d8 : e8 93 f8 17 00  rel32=1570963 -> 0x1402b4270
### is there any e8 to 0x2b4290 (the real body) anywhere?
   call at 0x2b479d
   total calls to 0x1402b4290: 1
### any 8-byte pointer equal to 0x1402b4270 or 0x1402b4290 in the image?
   0x1402b4270 : 0 occurrence(s) []
   0x1402b4290 : 0 occurrence(s) []
```

**MEASURED**: two direct calls go to the `int3` padding at `0x2b4270`, and one call — from inside
`amDongleUpdate`'s body at `0x2b479d` — goes to the real body at `0x2b4290`. Neither address appears as
an 8-byte pointer anywhere.

**INFERRED (single-source, mechanism unproven):** the shape fits a five-byte `int3` trap *gate*: the
outward call sites enter the gate, an exception handler supplies the implementation at `0x2b4290`, and
`amDongleUpdate` uses the internal entry directly. Under that reading, the closed core hooking the gate
at `0x2b4270` is the correct thing to hook, and the A slot of this pair — `cc cc cc cc cc` followed by
`jmp [rip+0]` to `0x1402b4275` — is "trap into the original gate", not "replay the original prologue".
I could not verify this: no dump contains the handler, no dump contains the on-disk original of this
region, and `AM-LAAG.md` does not mention the padding. This is the one row where the note "the closed
core overwrites the 17 prologues" is literally true but the word *prologue* is misleading.

---

## 6. Are they called in the dump, and by what?

**MEASURED**, whole-image scan for `E8 rel32` whose target is a claimed start (equal to a second
source, `AM-LAAG.md` §3.6, which lists the same call sites):

```
amDongleBillingGetKeychipId        callers=['0x4d30d']
amDongleGetGameId                  callers=['0x4d371']
amDongleGetModelType               callers=['0x4d38a']
amDongleGetNetworkAddress          callers=['0x4d33f']
amDongleInit                       callers=['0x4d2df']
amDongleIsAvailableInternal        callers=['0xab8b9', '0x1349d8']
amDongleSetupKeychip               callers=['0x4d2f7']
amDongleUpdate                     callers=['0x4d314', '0x4e7a9']
amHmExit                           callers=['0x4e57b']
amHmGetCautionFlag                 callers=['0x4d5cd']
amHmInit                           callers=['0x4d5ac']
amHmResetCautionFlag               callers=['0xabc74']
amHmUpdate                         callers=['0x4e75d']
amMasterExitInternal               callers=['0x4e6cd']
amMasterInit                       callers=['0x4d9cd']
amMasterSetBackupEventLogParam     callers=['0x4dc97']
amMasterUpdate                     callers=['0x4e735']
```

All 17 are called directly (`call rel32`), from two clusters (`0x4d2df..0x4dc97`, `0x4e57b..0x4e7a9`)
plus two outlying callers of `amDongleIsAvailableInternal`. Supporting measurement — there are **no
8-byte pointers to the core module anywhere in the image**, so nothing reaches these functions through
a function-pointer table:

```
### 4. 8-byte words in the image that fall in the core module range
### range 0x6ffff6b61000..0x6ffff6ddc000
   total = 0
```

**Single source** (my scan; the prose's table agrees but is the same claim).

---

## 7. Where the summary is right, imprecise, or wrong

| # | statement (source) | result |
|---|---|---|
| 1 | "17 prologues in `VACUUM.exe` are detoured" (`AM-LAAG` §1, the summary) | right for the 17 `am` functions; the image contains **18** such prologues — the 18th is at `0x485a` and is not an `am` function (their own §1 lists it) |
| 2 | "8 `amDongle*`, 5 `amHm*`, 4 `amMaster*`" | correct |
| 3 | "all 17 rerouted into a 4096-byte `rwxp` page at `0x13fff0000`" | correct; page perms from `maps-gesloten.txt` line 99, single source |
| 4 | "the page is a two-slot detour table" | correct for all 18 pairs; A = B − 0x0e |
| 5 | "all 17 answers are 0" | holds for **15 of 17** with the strength in §3; **2 rows (1 and 6) have no evidence of a return value**; and for 11 of the 15 the byte group is measured while the *path* to it is not |
| 6 | "`amDongleGetNetworkAddress` returns `192.168.1.0`" | imprecise: it **returns 0** and **writes** the address through `*rcx`; and the write is conditional (constant `0x1a8c0` only when `[0x6ffff6f8be01] == 0`, otherwise the dword at `0x6ffff6eee64c`) |
| 7 | §5.1 "writes nothing in `rcx`" for the other 16 | **not verified** — only the entry stub is readable; anything the body does after the log call is in bytes that do not decode |
| 8 | §7 offset list of `33 c0` occurrences | two rows are wrong: `amDongleBillingGetKeychipId +0x17c` is past that function's end (`+0x11d`), and `amDongleInit +0x241` is past its end (`+0x2f`) |
| 9 | §5.1 "`amDongleIsAvailableInternal` → 0 (AFGELEID)" | the caution is correct; I confirm no `33 c0` anywhere in 1024 bytes |
| 10 | §3.6 call-site table | reproduced exactly |
| 11 | §3.5 amMaster bodies "AFGELEID" | for `amMasterExitInternal`, `amMasterInit`, `amMasterSetBackupEventLogParam`, `amMasterUpdate` the entry stubs and (for the latter three) the `xor eax,eax; ret` are measured; the "same form" claim for the bodies is not verifiable, same reason as §3.1 |
| 12 | §4 "18 different offsets, so the page is a table of 18 stubs, not one shared stub" | correct; 36 slots = 18 pairs |
| 13 | §4 "there is no pointer to the core in the image" | reproduced: 0 hits |
| 14 | §4 dongle object empty | **reproduced independently**: `[0x14049cc68] = 0x1404de050`, 512 bytes at RVA `0x4de050` are **all zero**; `[0x14049ccd8] = 0x1404de700`, 1056 bytes, all zero; `[0x4de6b0]` (amHm global) zero. Additionally the 14 `dongle-*.bin` / `amMaster-*.bin` snapshots from two further runs are all-zero (`dongle` sha256 `076a27c7…`, `amMaster` sha256 `10e2103e…`) — **three sources** |
| 15 | §2 "names come from log-tag strings, not exports" | confirmed independently (§5.2) |
| 16 | `AM-LAAG` §4 `amHmInit` dump: `jmp` at `0x13fff0c0a` | off by one: measured `0x13fff0c0b`; relocated `lea` target `0x14036a92f`, not `0x14036a930` |
| 17 | core sha `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28` | confirmed from two launch logs |
| 18 | §5 "the closed core replaces the 17 `am` functions entirely" | supported for the *entry*: each of the 17 first bytes is `e9` to a slot B that qwords into the core. That the core implementations are the *complete* replacements (and never call slot A back) is **not** measurable from these dumps |

---

## 8. Could not determine

1. **The return value of `amDongleBillingGetKeychipId`** (row 1). Its epilogue is
   `add rsp,0x158; pop rdi; pop rsi; ret` with no `xor eax,eax`; the only `33 c0` in the function
   belongs to the stack-cookie check. `AM-LAAG` §7's evidence for this row (`+0x17c`) lies past the
   function end and belongs to a different function.
2. **The return value of `amDongleIsAvailableInternal`** (row 6). No `33 c0` in 1024 bytes; the entry
   is a tail `jmp` to `0x6ffff78e95c1`, so the value would come from that helper, which was never
   dumped.
3. **The path from entry to epilogue for the 11 INFERRED rows.** The bytes between the log call and
   the `xor eax,eax; … ret` group do not decode into a plausible instruction stream (§3.1). The group
   is measured and is at the function's end (int3 padding / next prologue), but I cannot show it is
   reached, and I cannot show what the far log helper returns.
4. **Which branch `amDongleGetNetworkAddress` actually took** in any run: the flag byte at
   `0x6ffff6f8be01` and the dword at `0x6ffff6eee64c` are in the core region
   `0x6ffff6fae000..0x6ffff6fe6000`, which was never dumped. So "the cabinet answered 192.168.1.0" is
   the *constant* branch; whether the run used it is unknown.
5. **What the core's log/lookup helpers do** and what they return. Their targets
   (`0x6ffff78d2b30`, `0x6ffff78e95c1`, `0x6ffff782f8b5`, `0x6ffff829e10c`, `0x6ffff93aceac`,
   `0x6ffff8cc5be5`, `0x6ffff91e0dcb`, `0x6ffff9243351`, `0x6ffff90c68b2`, `0x6ffff7026ff1`,
   `0x6ffff6fff0f0`, `0x6ffff6d3e860`, …) all lie in the core's anonymous executable arenas
   (`0x6ffff6fec000..0x6ffff9f46000` and above, per `samenvatting-gesloten.txt`). None of those arenas
   was dumped; only the 1024-byte windows at the 17 entry points and the 0xC0-byte windows from a third
   run exist.
6. **The core's log strings.** Their addresses are measured (`0x6ffff6e2c338`, `0x6ffff6e2c320`,
   `0x6ffff6e2c2a0`, `0x6ffff6e2c258`, `0x6ffff6e2c380`, `0x6ffff6e2c268`, `0x6ffff6e2c8c0`,
   `0x6ffff6e2c890`, `0x6ffff6e2ea80`, `0x6ffff6e2ea70`, `0x6ffff6e1a050`, `0x6ffff6e2c860`,
   `0x6ffff6e2c8a8`, `0x6ffff6e2c2b8`); the strings themselves are in the core's `.rdata`, which was
   never dumped. So the per-function log text ("which function logged what") cannot be verified.
7. **Whether the core implementations are ever executed.** The game's call sites are measured, the
   detour bytes are measured, but no execution trace of the core exists in any of these files.
8. **The `int3` site at `0x2b4270`** (§5.6): whether the outward callers enter a deliberate five-byte
   trap gate served by an exception handler, whether the closed core hooked the gate or the wrong
   address, and whether the real body at `0x2b4290` is also reached at run time. Single-source, the
   mechanism is not measurable from these dumps.
9. **The 18th detour at `0x485a`.** Not an `am` function; its A-slot target `0x140e74327` (RVA
   `0xe74327`, inside `.capcom0`) is not `start+N` like the other 17, so its "return path" has a
   different shape. Not investigated.
10. **The game-side meaning of "0".** For `amDongle*` the callers compare against `0`, `1` and `2`
    (`0x4d2f7`, `0x4d319`, `0x4d31e`) so a 0 is "success" in the caller's words; but whether the *core*
    intends 0 as success, and how the caller of `0x4d250` reacts to the status stored at `[obj+0x4]`,
    is not established here (that path is `AM-LAAG` §3.6 and was not re-derived).
11. **The on-disk `.text`.** `/tmp/vacuum-dump/text.bin` (3 496 500 B, sha256 `6ff01930…`) is
    essentially empty at these offsets (`00 00 00 00 00 00 00 00` at every one of the 18 sites), i.e.
    the file's `.text` is the packed section, ~89 % zero against the unpacked dump. It therefore
    contributes **nothing** to the original-prologue question and must not be used as a second source
    for it.

---

## 9. Raw evidence and how to reproduce

All under `/tmp/antwoordset/` (nothing outside `/tmp` was written except this file):

```
scan.py  scan.out  jump-hits.txt          image-wide e9 scan, bytes at the 18 sites, name scan,
                                          core-pointer scan, E8 call-site scan, literal searches
page.py  page.out  page-slots.txt         the 36 slots and their qwords
final.py page-struct.out                  per-pair replay lengths, targets, byte map of the page
header.py header.out                      page header, 0x40-stride chain, sha256 of every source
kern.py  kern.out  dis-<name>.txt         per-implementation disassembly (1024-byte windows)
kern2.py kern2.out                        first `33 c0` per window; negative control
kern3.py kern3.out                        entry bytes, int3-padded function end, in-function `xor eax,eax`
tails.py tails.out                        pair table + aligned tail disassembly
poscontrol.py poscontrol.out              the positive control inside each function's own span
objects.py objects.out                    dongle/amMaster object emptiness, snapshots
xcheck.py xcheck.out                      text.bin comparison
xcheck2.py / xcheck3.py                   relocation and accounting checks
callsite.py callsite.out                  the calls into 0x2b4270 and 0x2b4290
dump4270.py                               raw bytes around the 0x2b4270 int3 site
start-<name>-0x<rva>.bin                  64-byte windows of each detour site
```

Reproduction, in order:

```
python3 /tmp/antwoordset/scan.py       > /tmp/antwoordset/scan.out
python3 /tmp/antwoordset/page.py       > /tmp/antwoordset/page.out
python3 /tmp/antwoordset/final.py      > /tmp/antwoordset/page-struct.out
python3 /tmp/antwoordset/header.py     > /tmp/antwoordset/header.out
python3 /tmp/antwoordset/kern.py       > /tmp/antwoordset/kern.out
python3 /tmp/antwoordset/kern2.py      > /tmp/antwoordset/kern2.out
python3 /tmp/antwoordset/kern3.py      > /tmp/antwoordset/kern3.out
python3 /tmp/antwoordset/poscontrol.py > /tmp/antwoordset/poscontrol.out
python3 /tmp/antwoordset/objects.py    > /tmp/antwoordset/objects.out
```

The disassembly command form used throughout (adjust `--adjust-vma` and the range per row):

```
objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x1402b3b70 \
        --start-address=0x1402b3b70 --stop-address=0x1402b3bf0 \
        /tmp/b0-geheugen-b/image-gesloten.bin
```

---

## 10. Closing statement of scope

Everything above is static reading of files already on disk under `/tmp`. No process was started, and
nothing was killed. The 17 rows are therefore statements about **bytes**, not about **behaviour**: the
section that is fully traceable is one function (row 4); for 11 rows the epilogue is in the bytes but
the route to it is not; for 2 rows there is no return value evidence at all. A drop-in that reproduces
"return 0, and for `amDongleGetNetworkAddress` write `192.168.1.0`" is still the best-supported
specification available — but the phrase "all 17 answers are 0, measured" is stronger than the evidence.
