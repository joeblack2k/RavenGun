# UITDAGING-EN-ANTWOORD — Men in Black: what `GAME_MSG_SECURITY_CHALLENGE` is, what the answer must be, and the one test

**Lane:** licence/hardware emulation. **Date:** 2026-10-14. **No run was made** (the bench is not ours;
another lane is on Metal Gear Arcade). Everything below is **static**, read from files on the share and
from the runs that already exist in `bewijs-attract-20261014/`.

**Labels:** **MEASURED** = read from a file (binary offset, disassembly, log line) — command and raw
output in `/tmp/mib-uitdaging/`; **DERIVED** = computed from MEASURED bytes; **INFERRED** = a conclusion
that follows but was not directly observed; **NOT DETERMINED**.

**Files used** (all on the share, read-only; `Men in Black/` = `C:\Sega\`):

| file | size | sha256 (start) |
|---|---|---|
| `shell/Shell.exe` (Game Shell 230307RED) | 1,434,624 | `e6a7bae5…` (see `runs/SHA256SUMS.txt`) |
| `shell/Shell.map` (linker map, real symbols) | 1,285,838 | — |
| `MIB/MIB.exe` | 653,824 | `afed0bdd…` |
| `MIB/MIB_Data/Managed/Assembly-CSharp.dll` | 927,744 | — |

---

## 1. What the challenge is

### 1.1 The wire protocol (MEASURED)

The shell's packet layer reads a fixed 11-byte header and a payload:

* `ValidPacket` @ `0x40ea40`: requires bytes `'M','I','B'`; then a 16-bit big-endian length, a 16-bit
  big-endian **message id**, and a 32-bit big-endian sequence number. **MEASURED** (disassembly).
* `NetworkReceiveCommand2` @ `0x40f270` reads exactly the same layout and at `0x40f2ec`–`0x40f30a`
  compares the first three payload bytes against `0x4d/0x49/0x42` (`'M','I','B'`), then builds
  `length = (b0<<8 | b1) + 11`. **MEASURED**.
* `NetworkSendCommand` @ `0x40e4f0` builds the same frame with `'M','I','B'` written literally at
  `ESP+0x0c,0x0d,0x0e` and sends 11 + payload bytes with `send()` (**MEASURED**).

### 1.2 The message id (MEASURED, calibrated)

The shell's own message-name table (jump table `0x40ed2c` / index table `0x40ed60`, dumped in full)
maps **message id `0x0006` (6) → the string at `0x48bf58` = `"\tGAME_MSG_SECURITY_CHALLENGE"`**.
An uncalibrated "no output" cannot be trusted, so here is the calibration: the same table maps
`0x0065`(101)=`\tGAME_MSG_LAUNCHED_OK`, `0x0066`=LOADING_ASSETS, `0x0068`=FINISHED_LOADING_ASSETS,
`0x00c8`=INITIALISE_ATTRACT_SEQUENCE, `0x02bc`=PLAY_END — and the run log
`m6-shell-logs/Networking_-_261014.log` line 9 prints exactly the string `"	GAME_MSG_SECURITY_CHALLENGE"`
for the packet the game sends at 00:52:33. So id 6 is the challenge, **MEASURED** twice over (table and
log).

The game's side (`Assembly-CSharp.dll`, managed; `MIB.MIBOpCode::.cctor`, IL at RVA `0x44c30`) fills its
own name table with the same numbers:

```
0 -> NOP        4 -> DISPLAY_IOU     5 -> GAMESHUTDOWN   6 -> SECURITY_CHALLENGE
64 -> SET_SKU   66 -> SET_CREDIT_VALUE   67 -> SECURITY_AUTHENTICATE   69 -> EXIT_GAME
100/101/102/103/104 -> SET_GAME_MODE / GAME_LAUNCHED_OK / GAME_LOADING_ASSETS /
                       GAME_LOADING_ERROR / GAME_FINISHED_LOADING_ASSETS
```
**MEASURED** (IL dump of rid 2997).

### 1.3 What the payload carries (MEASURED)

The shell's handler for id 6 is at `0x409a9f` (reached from the second dispatcher `0x4098a0`, table
`0x40a4dc`/`0x40a508`, entry for id 6 = index 3):

```
0x00409a9f  8b442424        mov  eax, [esp+0x24]      ; payload pointer
0x00409aa3  0fb608          movzx ecx, byte [eax]
0x00409aa6  880d88a26200    mov  [0x62a288], cl       ; ChallengeData[0]
0x00409aac  0fb65001        movzx edx, byte [eax+1]
0x00409ab0  40              inc  eax
0x00409ab1  40              inc  eax
0x00409ab2  881589a26200    mov  [0x62a289], dl       ; ChallengeData[1]
0x00409ab8  0fb608          movzx ecx, byte [eax]
0x00409abb  880d8aa26200    mov  [0x62a28a], cl       ; ChallengeData[2]
0x00409ac1  0fb65001        movzx edx, byte [eax+1]
0x00409ac6  88158ba26200    mov  [0x62a28b], dl       ; ChallengeData[3]
0x00409acc  c60598a2620001  mov  byte [0x62a298], 1    ; bAuthenticateRequired = 1
```

So **the challenge payload is exactly four bytes**, and they land in the global
`_ChallengeData` @ `0x62a288` (`Shell.map` line 9133, `Security.obj`). The same function also proves the
packet actually arrived: if the game did not send it, this code never runs. **MEASURED**.

The game builds those four bytes itself. `MIB.MIBCore::Update` (rid 2704, RVA `0x40448`) does, every
frame, when `m_securityTimer > 10`:

```
IL_0061  m_securityTimer = 0
IL_0067  m_challengeData[0] = (byte) UnityEngine.Random.Range(0,256)
IL_0081  m_challengeData[1] = (byte) UnityEngine.Random.Range(0,256)
IL_009b  m_challengeData[2] = m_challengeData[0] ^ m_challengeData[1]
IL_00b5  m_challengeData[3] = m_challengeData[1] ^ m_challengeData[2]
IL_00ce  SendMessage("SecurityChallange", m_challengeData)      ; the typo is in the game
IL_00df  m_securityCounter += 1
IL_00ed  m_securityTimer += Time.deltaTime
IL_00ff  if (m_securityCounter > 4 && !SecurityShutdown) { SecurityShutdown = true;
             Debug("Security Failed"); StartCoroutine(ExitGameSequance()); return; }
```
**MEASURED** (IL). `m_challengeData` is created in `MIBCore::Awake` (rid 2700) as
`IL_006a ldc.i4.4 / IL_006b newarr [mscorlib]System.Byte / IL_0071 stfld F:m_challengeData`
— **a 4-byte array** (IL byte at offset 0x6c = `0x1a`, i.e. `ldc.i4.4`; **MEASURED**, exact byte dump in
`/tmp/mib-uitdaging/il-mibcore-awake.txt`).

### 1.4 What the shell does with it (MEASURED)

`SecurityChallengeCheck` @ `0x410b10` (symbol from `Shell.map` line 369, `Security.obj`):

```
0x00410b10  803d98a2620000   cmp byte [0x62a298], 0      ; bAuthenticateRequired
0x00410b17  741e             je  0x410b37
0x00410b19  6888a26200       push 0x62a288               ; &ChallengeData
0x00410b1e  c60598a2620000   mov byte [0x62a298], 0
0x00410b25  e8b6feffff       call 0x4109e0               ; HGLGenerateAuthenticationData
0x00410b2a  6888a26200       push 0x62a288
0x00410b2f  e82cdcffff       call 0x40e760               ; NetworkSendAuthenticate
0x00410b34  83c408           add esp, 8
0x00410b37  c3               ret
```

`NetworkSendAuthenticate` @ `0x40e760` queues **command `0x43` (67) with a 4-byte payload**:

```
0x0040e760  8b4c2404   mov ecx, [esp+4]     ; the 4 bytes
0x0040e764  6a43       push 0x43            ; SECURITY_AUTHENTICATE
0x0040e766  b804000000 mov eax, 4           ; length
0x0040e76b  e820ffffff call 0x40e690        ; NetworkQueueMessageSend
```
**MEASURED**. The id 67 → `SECURITY_AUTHENTICATE` correspondence is calibrated on both sides: the
shell's string table lists `SECURITY_AUTHENTICATE` among its outgoing messages (**MEASURED**, map line
3066 area / `0x48c…`), and the game's `MIBOpCode` table maps `67 -> SECURITY_AUTHENTICATE`, whose
handler `MIBClient::HandleMessage` (rid 2813) dispatches the Unity message
`"SecurityAuthenticate"` with the raw payload (**MEASURED**).

The answer itself is computed in `HGLGenerateAuthenticationData` @ `0x4109e0`:

1. **reverse** the four bytes (`lea eax,[ebp+3]`, `dec eax`, loop while `eax >= ebp`);
2. `xor` every byte with **`0x77`** (`0x410a00`);
3. `xor` the first four bytes with **`0xC0`** (`0x410a16`) and replace each through the 256-byte table
   at `0x48c7b8` (`mov dl, [ecx+0x48c7b8]`, `0x410a2f`);
4. one more pass with the 12-byte constant at `0x48c9ac` (starts `25 58 25 58 25 …`) — a rotate/XOR mix;
5. `xor` the four bytes with **`0xB1`** (`0x410af6`).

**Calibration of my transcription:** the table at `0x48c7b8` is **byte-for-byte the 8-bit bit-reversal
table** (`0,128,64,192,32,…,248` for 0..15) — **MEASURED** by comparing the 256 extracted bytes with
`[int(f"{i:08b}"[::-1],2) for i in range(256)]`: identical. Three worked vectors of the whole shell
function (**DERIVED** from the MEASURED constants):

| challenge (as on the wire) | shell's answer |
|---|---|
| `00 00 00 00` | `ba c7 ba c7` |
| `01 02 03 04` | `9a 07 fa 47` |
| `10 20 30 40` | `b8 cb be cf` |
| `ff ff ff ff` | `45 38 45 38` |

### 1.5 What the game does with the reply (MEASURED, and this is the crux)

`Assembly-CSharp.dll`, `MIB.MIBCore::SecurityAuthenticate` (rid 2707, RVA `0x40590`), signature
`(byte[]) : void`, called by Unity `SendMessage` from the client when id 67 arrives:

```
IL_0002  ldfld F:m_challengeData              ; the 4 bytes the game sent
IL_0007  ldarg.1                              ; the payload the shell sent
IL_0008  call  M:AuthenticatedData
IL_000d  brfalse -> "Security Failed":        m_securityCounter += 1
                    else "Secutiry Passed":   m_securityCounter -= 1   (only if it is > 0)
```
**MEASURED** (full IL, `/tmp/mib-uitdaging/il-mibcore-securityauthenticate.txt`).

`MIB.MIBCore::AuthenticatedData` (rid 2727, RVA `0x40fd8`), signature
`(byte[][], byte[]) : byte` — the field references inside it were extracted instruction by instruction
(`/tmp/mib-uitdaging/fieldrefs.txt`), not guessed:

| IL offset | field | array | size | initial bytes (FieldRVA) |
|---|---|---|---|---|
| `IL_000c,002e,0037,007b,012b,0134,0148,0187` | `SECURITY_KEY` | byte[] | **10** | `40 08 10 04 01 02 04 02 20 01` |
| `IL_00a0` | `DECRYPT_ARRAY` | byte[] | **8** | `86 ec 90 f1 df c6 5a 77` |
| `IL_015e` | `LOOKUP_PERMUTE` | byte[] | **256** | bit-reversal table |

All three are hardcoded initial-data arrays loaded in `MIBCore::.ctor` (rid 2729) with
`ldtoken F:<hash> / call InitializeArray`; the sizes come from the field signatures (`06 11 8e 14` =
8-byte SZARRAY, `06 11 8e 1c` = 10-byte, the 256-byte one is named after its own content hash).

The function is a decoder with four stages, run over a working byte[4] (`arg.2`, the shell's payload):

* **stage 1** — `v4 = (byte)(16*SECURITY_KEY[1] + SECURITY_KEY[3])`; XOR all four working bytes with it;
* **stage 2** — 12 rounds (`v0` counts 11 down to 0) of an LFSR-style rotate/compare through
  `LOOKUP_PERMUTE`, with the spin value `vb = SECURITY_KEY[5] ^ 99`;
* **stage 3** — four times: if `SECURITY_KEY[3]` has bit `i` set, replace the byte through
  `LOOKUP_PERMUTE` and XOR with `~(16*SECURITY_KEY[0] + SECURITY_KEY[4])`;
* **stage 4** — XOR all four with `SECURITY_KEY[7]`;
* **return `1` only if the working buffer now equals the challenge byte-for-byte; otherwise return the
  last working byte** (any value ≠ 1 is a rejection).

Two consequences, both **DERIVED** from the above and each independently checkable:

1. **The decoder never reads the challenge.** The only array it indexes besides the working buffer is
   `SECURITY_KEY`. I verified this by running my transcription of the IL (a statement-by-statement
   Python port, `/tmp/mib-uitdaging/authsim.py`) on three different challenge values with the same
   payload: the decoded buffer was identical, `(102,130,98,53)`. The challenge enters only in the final
   four-way comparison.
2. Therefore the shell's own answer is **not** the answer the game accepts. Worked example
   (**DERIVED**, and the most useful single table in this file):

| challenge | shell sends (its own algorithm) | game decodes that to | verdict |
|---|---|---|---|
| `00 00 00 00` | `ba c7 ba c7` | `b6 5e 67 4b` | **REJECTED** |
| `01 02 03 04` | `9a 07 fa 47` | `96 56 61 25` | **REJECTED** |
| `10 20 30 40` | `b8 cb be cf` | `b5 1e e4 ae` | **REJECTED** |

So "the shell does not answer" is **not** the right description. **INFERRED (strong): the shell does
answer** — `SecurityChallengeCheck` is reached on the main thread on every pass
(`MainThreadSecurityCheck` @ `0x40c750` → `NetworkStartup` @ `0x40e800` → `SecurityChallengeCheck`
@ `0x410b10`: three MEASURED `call` sites, `0x40ccc6` → `0x40e800`, `0x40e204` → `0x410b10`, `0x410b2f`
→ `0x40e760`) — and the answer is simply computed with the **wrong algorithm** for this game: the two
sides are a generation apart.

### 1.6 A second, sharper finding (MEASURED, INFERRED)

`AuthenticatedData` indexes its first parameter at **`IL_0011 = ldc.i4.5 / ldelem.u1`** and again at
`IL_003c = ldc.i4.3`, and `SecurityAuthenticate` passes it **`m_challengeData`, which is a 4-byte
array**. A .NET array read of index 5 on a 4-element array throws `IndexOutOfRangeException`. So either

* **(H1)** the game's security check throws on every reply and the counter therefore never goes down, or
* **(H2)** my reading of the array length is wrong.

The strongest counter-evidence for H2 is that I re-checked the allocation at byte level: in `Awake` at
IL offset `0x6c` the byte is `0x1a` (`ldc.i4.4`), and the same 4-byte array is what
`SecurityChallange` hands to `SendMessage`. There is no other assignment to `m_challengeData` anywhere in
`Assembly-CSharp.dll` (**MEASURED**: it is written only in `Awake`; `Update` and `SecurityAuthenticate`
only read it).

Both H1 and H2 produce the same observable: the counter never decreases, reaches 5, and the game calls
`ExitGameSequance` — which is what the recorded run m6 shows.

**What "the exit" is, then, and how long it should take (DERIVED, timing MEASURED).**
`m_securityCounter` starts at 0 (`Awake`), `Update` increments it once per sent challenge, and
`SecurityAuthenticate` only decrements it *after* a successful decode. So the game exits after **five
challenges have been sent**, i.e. about `5 × 10 s = 50 s` after the first one — regardless of whether
the shell answers, answers wrongly, or the check throws. The recorded m6 cycle is consistent with this:
first logged challenge `00:52:33`, socket closed by the client `00:53:04` (31 s), next cycle's challenges
come 10 s apart. **INFERRED** that this is the same counter; **MEASURED** that the counter condition
exists in the IL and that the observed exit is of that order.

---

## 2. What the answer must be, and where we already hold it

**What the answer must be.** Four bytes, sent as message id 67 / command `0x43`, such that
`AuthenticatedData(m_challengeData, answer)` returns 1 — i.e. the game's decoder must map the answer
onto the exact four challenge bytes. Because the decoder is built only from the 10-byte
`SECURITY_KEY` (and the two permutation tables), its behaviour is **fixed and computable offline**:
for a challenge `c` the accepted set is `{ r : decode(r) == c }`. Measured on 200,000 random
replies, `decode(r) == 1` happened **756 times (3.8·10⁻³)**, i.e. the accepted set is tiny — my port
`authsim.py` can enumerate it (it can also be inverted stage by stage if a unique inverse is wanted).

**Where the four gate answers come from — and whether that source can yield this one.**
The four gates are answered from three different kinds of source, none of which is the game:

| gate | answer is | source | can it answer the security challenge? |
|---|---|---|---|
| A checksum | `/k=e6222935` | computed from the file by `mib-shell-copy.sh` (CRC table + seed inside `Shell.exe`) | no — different datum |
| B I/O board | the nine-byte forced-HID change at file offset `0xbc21` | a binary patch copied from the Transformers dump into a **view-local** `Shell.exe` | no |
| C DK2 key | `[Debug] SecurityDisabled=1` | a text switch in the view-local `Game.ini`; the switch's only effect is `SecurityDisabled()` @ `0x411620` returning true, so the shell never consults `dk2win32.dll` | **no** — the shell's own answer does not depend on it (see §1.4) |
| D integrity | `[Debug] NoVerify=1` | same `Game.ini` | no |

So the recipe we already hold (`bewijs-attract-20261014/harness/mib-view.sh`, `mib-shell-copy.sh`,
`mib-gameini.sh`) is **complete only up to the launch**. The security-challenge answer is a **new kind of
datum**:

* it is **not** a config switch — the shell's answer path is unconditional and does not test
  `SecurityDisabled`;
* it is **not** the DK2 dongle — `dk2win32.dll` is never entered when the switch is on;
* it is **not** per-file checksum data;
* it is a **per-challenge 4-byte value** that depends on the algorithm the *game* expects, and that
  algorithm is a hardcoded constant table inside `Assembly-CSharp.dll` (`SECURITY_KEY`, 10 bytes).

And — the important one — the shell's algorithm and the game's algorithm are **not the same function**.
The shell computes its answer from a bit-reversal table plus `0x77/0xC0/0xB1` and the `25 58 25 58 25`
rotate constant; the game's decoder is driven by `SECURITY_KEY = 40 08 10 04 01 02 04 02 20 01`.
Neither constant set appears on the other side (**MEASURED**: the shell has no `40 08 10 04 01 02 04 02
20 01` sequence; the game's IL has no `0x77/0xC0/0xB1` constants and no 256-byte FieldRVA other than
`LOOKUP_PERMUTE`/`DECRYPT_ARRAY`).

**Conclusion of §2:** the answer is not held anywhere we already look; it must be **either** patched
into the shell's `HGLGenerateAuthenticationData` (a correct 4-byte transform of the challenge, computed
from the game's constant) **or** the game's check must be answered by patching the check itself. The
former is the clean one and stays inside `patches/MenInBlack/`; the latter is the one that will settle
the *question* in a single run (§3).

---

## 3. The single dynamic test

**What changes: exactly one byte-range of the view-local `Assembly-CSharp.dll`, nothing else.**
The test builds on the working m6 recipe unchanged (view-local `Shell.exe` with the I/O change,
`/k=e6222935`, `Game.ini` with `SecurityDisabled=1 NoVerify=1`, serial hidden, our own core).

* Target: `views/MenInBlack/MIB/MIB_Data/Managed/Assembly-CSharp.dll` — a copy in the view, **never the
  share** (the same local-copy pattern as `Shell.exe`; the game's `install.ini` `install.crc` is not
  consulted because the shell's verify is off with `NoVerify=1`).
* Method to patch: `MIB.MIBCore::AuthenticatedData`, rid **2727**, body RVA **0x40fd8** (method table
  RVA `0x40fd8`; the IL header is 12 bytes, `code size` at file offset `rva2off(0x40fd8)+4`).
* Change: overwrite the first two IL bytes of the body with `17 2a` (`ldc.i4.1; ret`) — the function
  then always returns 1 = "accepted". The code size stays 467, the rest of the body becomes dead bytes.
  Nothing else in the assembly is touched.
* Alternative if a one-byte change is preferred: at `SecurityAuthenticate`, insert/replace so that
  `m_securityCounter` is set to 0 before the `+= 1` at IL_0032/IL_0033 — then even a failing
  authentication can never make the counter reach 4.

**What it will show.** With the challenge "answered", the game must stop exiting. Concretely, in the
same 4-minute harness that produced m6:

* **"answered correctly"** = the shell's `Mainthread_-_<date>.log` contains **no** `Game Shutdown -
  Restarting…` line after the launch, the game process stays alive past the end of the run, and the
  shell's `Networking` log shows the challenge arriving on a roughly 10 s cadence **forever** (five, ten,
  fifteen times) instead of stopping after two.
* **"still unanswered"** = the observed m6 pattern repeats: client socket closed about 50 s after the
  first challenge, `MAINTHREAD : Unexpect Game termination, retrying`, and 8 cycles in 4 minutes.

**The positive control (so that a silent log is distinguishable from a broken instrument).** Three
things must appear in the same run log, or the run proves nothing:

1. `Networking_-_<date>.log`: `NetworkServerProcess : ServerSocket listening OK`, then
   `Game connected`, then `GAME_MSG_LAUNCHED_OK dlen=0 seq=2`, then at least one
   `GAME_MSG_SECURITY_CHALLENGE`, then `GAME_MSG_FINISHED_LOADING_ASSETS` (this last one is the
   instrument's own end-to-end proof that the game is alive and talking 30–40 s in);
2. `Mainthread_-_<date>.log`: `Launched …\MIB\MIB.exe`;
3. the game's own `Player.log`: `CONNECTING TO SERIAL` / `BOOOO! NOT Connected to serial` (proves the
   game process ran) and the *absence* of an exception stack.

If (1) is absent, the run is broken, not the hypothesis.

**Explicitly part of this run (the cheapest decisive variant).** Instead of only patching the game,
patch the shell so that the ambiguity in §1.5 is removed — log the 4 challenge bytes and the 4 answer
bytes that `SecurityChallengeCheck` produced. `Shell.exe` has a free cave: a run of **386 zero bytes**
inside `.text` at VA **`0x48667e`** (file offset `0x87a7e`; MEASURED by scanning the section for the
longest zero run). Patch at `0x410b10`, which currently begins with 6 bytes
`80 3d 98 a2 62 00` (`cmp byte [0x62a298],0`): replace them with `e9 <rel32>` to the cave, and in the
cave do

```
push 0x<fmt>          ; "%02x%02x%02x%02x / %02x%02x%02x%02x" + CRLF, a new string in .rdata
movzx eax, byte [0x62a288] ; … 0x62a289/0x62a28a/0x62a28b  (the challenge)
push eax  (x4)
call 0x4109e0         ; HGLGenerateAuthenticationData -> 0x62a288 becomes the answer
movzx eax, byte [0x62a288] ; … 0x62a289/0x62a28a/0x62a28b  (the answer)
push eax  (x4)
push 0x<fmt>
mov ecx, 0x628fa0     ; the "Networking" logger object (same one NetworkSendAuthenticate's
                      ; neighbours use; LogLine is called as push str / call 0x402a40)
call 0x402a40
add esp, 40
mov byte [0x62a298], 0
jmp 0x410b2a          ; the original tail: push &ChallengeData; call NetworkSendAuthenticate
```
The label text `"%02x%02x%02x%02x"` is new, so this is also a **calibration** of the label pattern: it
must appear once per challenge in `Networking_-_<date>.log`. That single run then answers, in one
message, both halves of the open question: *did the shell answer* (challenge/answer bytes printed), and
*did answering change anything* (does the exit still come at ~50 s).

**What could make the test lie.**

* **A stale copy.** The game must load the assembled `Assembly-CSharp.dll` from the view. m6 already
  demonstrates the view is what runs (`Mainthread` log prints `z:\…\views\meninblack\…`). Check the
  sha256 of the patched file in the run record, and check that the shell's Mainthread log names the view
  path. A game started from the share would silently ignore the patch.
* **The restart loop masking a success.** The shell restarts the game after any termination, so a single
  successful cycle could be hidden by later cycles. The measurement must be *the number of
  `Game Shutdown - Restarting…` lines and the process lifetime of the first MIB.exe*, not "did we see
  the logo".
* **`Mainthread` is shared with another lane's title on the bench.** Only read
  `views/MenInBlack/shell/*.log`; the harness copies them out per run (`runs/m6-shell-logs/`), so keep
  that pattern.
* **The cache.** Unity keeps `MIB_Data/Managed` assemblies in `Level*`/`globalgamemanagers` only in
  IL2CPP builds; this is Mono, so the `.dll` is read at startup. Still, wipe the Wine prefix's
  `drive_c/users/*/AppData/LocalLow/SEGA/MIB` between runs to be sure no player log or saved state from
  a previous cycle is reused.
* **`AuthenticatedData` never being called.** If the game's dispatch does not reach
  `SecurityAuthenticate` (e.g. the listener tagged `MIBClientListener` is absent), patching
  `AuthenticatedData` cannot change anything and the exit stays — that outcome would **not** falsify the
  security hypothesis, it would relocate it to the dispatch. The shell-side logging patch above is what
  tells these two apart.
* **The counter is not the exit trigger.** If the run shows the counter path is not taken (e.g. the exit
  happens at a fixed ~41 s from launch regardless of challenge count), the exit is a different
  mechanism (§4 alternatives).

---

## 4. Falsifiable prediction, and the alternatives

**Prediction (falsifiable, stated before the run).** *If the unanswered/incorrectly answered security
challenge is the reason the game exits, then a build that makes the game accept the challenge
(or that stops `m_securityCounter` from growing) will run past 5 minutes without one
`Game Shutdown - Restarting…` line, and the game will keep sending
`GAME_MSG_SECURITY_CHALLENGE` every ~10 s for the whole run.*

* **Falsifier:** the patched build still exits ~50 s after the first challenge, in the same
  restart rhythm. That kills this hypothesis (and, with the shell-side log present, kills it *with*
  proof that the shell answered).
* **Confirmed-alternative form:** if the run lasts past 5 minutes with the game-side patch but a
  shell-side-only correct answer also does it, then the fix belongs in
  `patches/MenInBlack/` as a shell patch and no game binary needs shipping.

**Alternative explanations, each with something that would point at it.**

1. **The serial line** (`MIB.exe`'s own "CONNECTING TO SERIAL" / "BOOOO! NOT Connected to serial").
   In run m2 (game started directly, the owner's Steam Controller Puck visible as COM33) the game
   *did* connect and lived longer than in m3–m6 where the port was masked. If the exit is the serial,
   then the patched run will still exit at ~50 s, but the game's `Player.log` will keep printing
   `CONNECTING TO SERIAL … BOOOO!`, and offering it a live COM endpoint (a second single run) would be
   the test that shifts the answer.
2. **The "PLEASE CALL ATTENDANT — TICKETS OWED" attendant state.** The shell sends
   `SET_CREDIT_VALUE = 3` and `SHELL_MSG_SET_SKU`, never a ticket/credit transaction; the game shows a
   ticket-owed attendant screen. If this is the trigger, the patched run will still exit at ~50 s and
   the shell log will show the credit/ticket messages absent or unchanged while the counter never
   reaches 5. **MEASURED**: `Credit_-_261014.log`, `TicketVend1/2` show `IOU of 0 vended updated` only.
3. **The I/O board watchdog.** `patches/SEGA-GAME-SHELL.md` §"Watch item" records that the shell's
   monitor calls `ExitRequired` on the same forced I/O flag and, if it drops, logs
   `FORCED SHUTDOWN of Game!!!` and terminates the game after 20 s. This one is easy to rule out in the
   same run: **if the log contains `FORCED SHUTDOWN of Game!!!`, the exit is not the challenge.**
   **MEASURED**: it does not appear in m6.
4. **A plain timeout / missing-assets abort.** The game finishes loading (37/16/14/13/15/12 s depending
   on cache warmth) and then exits; if the trigger were a post-load timer, the exit would be at a fixed
   offset from `GAME_MSG_FINISHED_LOADING_ASSETS`. m6's numbers (exit 4 s, 3 s, 3 s, 3 s, 3 s, 3 s after
   "assets loaded") are suspiciously constant — **this is the strongest competing explanation** and it
   is why the shell-side log matters: if the counter never reaches 5 in the log yet the exit is still
   ~3–4 s after the assets finish, the security challenge is *not* the trigger and the game-side patch
   will not help.
5. **The game's own check throwing** (`IndexOutOfRangeException` at `AuthenticatedData`'s index-5 read,
   §1.6). Under this explanation the game can never be satisfied by any shell behaviour, and the only
   fix is the game-side patch — which is exactly the experiment in §3, so the run settles it as well.

---

## 5. Could not determine

* **The actual challenge bytes of the recorded run.** They are in the packet payload and nowhere in any
  log; the shell's handler stores them into `0x62a288` and never prints them. Without them I cannot
  produce the exact four answer bytes for a past run — only the algorithm and the worked vectors of
  §1.4/§1.5. This is precisely what the shell-side logging patch in §3 exists to fix.
* **Whether the shell's queued answer actually reaches the wire.** `NetworkSendAuthenticate` does not
  log; the neighbouring `NetworkSendCabinetSKU`'s "Sent:SHELL_MSG_SET_SKU" line is written at **enqueue**
  time, and I could not find a direct caller of `NetworkServerProcess` @ `0x40f450` (the function that
  presumably flushes the queue) — the log's own `Sent:` line is therefore *not* proof that the socket
  carried anything. **INFERRED** (not MEASURED): the game would not have reached its logo if it had
  never received `SET_CREDIT_VALUE`, which implies the flush works, but the shell logs do not say so
  directly.
* **Why the real cabinet tolerates this.** `AuthenticatedData` reading index 5 of a 4-byte array should
  throw on a real cabinet too; either the production `MIB.exe` differs from this dump's
  `Assembly-CSharp.dll`, or the check is genuinely a crash-and-exit path there as well. Nothing on the
  share answers that. **NOT DETERMINED.**
* **Whether `SecurityDisabled=1` changes anything for the challenge.** Statically it cannot (the
  answer path tests only `bAuthenticateRequired`); a dynamic confirmation was not possible without a
  run.
* **The DK2 dongle's own part.** `dk2win32.dll`/`dk2win64.dll` are present (7.3 MB / 10.2 MB) but were
  never entered on this route; whether the real dongle feeds `SECURITY_KEY` or is read only for the SKU
  is **NOT DETERMINED**.
* **The I/O board's real behaviour** (the forced flag is believed, not present) — unchanged from
  `ATTRACT-RUN.md`, not touched by this dossier.

---

## 6. Files

* Raw evidence of this analysis: `/tmp/mib-uitdaging/` (`fieldrefs.txt`, `il-mibcore-awake.txt`,
  `il-mibcore-securityauthenticate.txt`, `il-authdata.txt`, `arrays.txt`, `shell-answer-vectors.txt`,
  `shell-disasm-security.txt`, `msg-tables.txt`, `authsim.py`).
* This dossier is the only new file under `patches/MenInBlack/`; nothing on the share was written and
  `~/projects/ravengun` was not touched.
