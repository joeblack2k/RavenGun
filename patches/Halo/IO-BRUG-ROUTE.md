# IO-BRUG-ROUTE: which input route is live for Halo: Fireteam Raven

*RavenGun input lane, 2026-10-13, 14:25-14:56. All shell work on the Deck (`deck@192.168.2.247`), through
`re\rcput.ps1` and `re\rc.ps1`. Three runs (20, 21, 22) plus bench-free static work. **Claims are labelled
MEASURED or INFERRED.** MEASURED = read from the binary or from the running game's memory with a
calibrated instrument, and the instrument's calibration is stated. Nothing here was taken from a photo:
**a still frame is not evidence and no photo is used as a result in this dossier.**

Source binary: `/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game`,
**33554432 bytes, sha256 `e88ba3543d5613c828d14aba26329a3b35b362d5dd7f817db7178c86a3b2fe98`**.
Both LOAD segments are `vaddr - file offset = 0x400000`, so a vaddr maps to file offset `vaddr - 0x400000`
(MEASURED, `readelf -lW`). The running game maps it at exactly those vaddrs (MEASURED: `/proc/<pid>/maps`
shows `0x1661000-0x23d3000 /pm/g7/halo/game`, and file-vs-process byte comparison agrees 100% on
`0x400000` and `0x1f392a0`, 91.8% on `0x180ae80` -- the differences there are the fields the game writes).

---

## 0. The short answer

1. **The class-0x4 board table IS the live route -- but only for what the writer types into it.**
   MEASURED: the table is completely empty at runtime except the slot our own input helper types as a
   switch, and that slot's press counter tracks our START pulses exactly. **The game does not fill the
   table; nothing does but us.** There is no fixed board image in this binary (the whole table is zero in
   the file, and it is BSS).
2. **The two masks: `0x1766f40 = 0x904` and `0x181d8a0 = 0x904`, bit `0x8` NOT set in either.**
   MEASURED at three moments in run 20 and again in run 21. The loader builder's expectation holds.
3. **The aim consumed by the per-frame player input filler is NOT a slot in the class-0x4 table.**
   MEASURED: that filler (0x624120) asks the analog getter for channel numbers **793 and 794** for
   player 1, and those numbers resolve to **fixed addresses `0x181d418` and `0x181d538`**, outside the
   32-slot table. The table only serves signal numbers `0x239..0x298` (3 boards x 32 slots).
4. **The ten board slots the game does poll at runtime are board 0 slots 27-31, board 1 slots 1/6/7 and
   board 2 slots 15/19** -- MEASURED from the runtime list at `0x1771850`. Their kind field is 0, so they
   are read as **analog channels**.
5. **Those analog slots do not reach the game in the attract loop.** MEASURED by writing a unique 4-byte
   value into each of board 0 slots 27-31 and searching the whole process image for it with a calibrated
   searcher: the value is found **only where it was written**, never copied anywhere else. Caveat in §7.
6. **The aim path at 0x624120 is dead in our runs, with a concrete cause**: player count `[0x1f392d0] = 0`
   and the per-player structure pointer `[0x181f7e0]` is **NULL** (MEASURED). Writing the player count,
   the device block, the port byte, the two analog bounds and the aim pair **all landed and stayed** and
   changed nothing -- the game neither consumed them nor crashed.

---

## 1. The address model of the board table is closed (MEASURED, disassembler)

The `0xaea0` that the input helper uses is not a mystery and not a second layout. It is a displacement in
the switch getter itself:

```
61e8b4: lea  0x11ec5c5(%rip),%rsi        # 0x180ae80        <- the base
61e8bb: sar  $0x5,%eax                   # signal-0x239 >> 5   -> the BOARD
61e8b1: and  $0x1f,%edx                  # signal-0x239 & 31   -> the SLOT
61e8be: lea  (%rdx,%rdx,4),%rdx ; shl $0x4,%rdx              # slot * 0x50
61e8c7: shl  $0xd,%rax  ; 61e8cf: shl $0x9,%rcx ; add %rcx,%rax   # board * 0x2200
61e8d6: add  %rax,%rdx
61e8d9: mov  0xaea0(%rsi,%rdx,1),%eax    # <-- [0x180ae80 + 0xaea0 + board*0x2200 + slot*0x50 + 0]
61e8e0: lea  -0x1(%rax),%ecx
61e8e3: lea  0xaea8(%rsi,%rdx,1),%rax    # <-- entry + 8
61e8f0: cmp  $0x1,%ecx
61e8f3: cmova %rdx,%rax                  # return NULL unless kind is 1 or 2
```

**So `entry(board, slot) = 0x180ae80 + 0xaea0 + board*0x2200 + slot*0x50 = 0x1815d20 + board*0x2200 + slot*0x50`.**
32 slots per board, stride `0x50`, board stride `0x2200`. That is exactly what
`tools/rt-input-shim/rg_rt_input.c` computes (`TABLE_BASE 0x180ae80`, `TABLE_OFF 0xaea0`,
`BOARD_STRIDE 0x2200`, `ENTRY_STRIDE 0x50`). **The helper writes the right place.**

*Correction of my own working note:* I first wrote "0x1812d20" in a scratch file -- that was my arithmetic
slip, not the binary's. The base is **0x1815d20** (`0x80ae80 + 0xaea0 = 0x815d20`). Every runtime read in
this dossier uses 0x1815d20.

## 2. Slot[0] is the kind: 0 = analog channel, 1/2 = switch (MEASURED, both getters)

```
getter for switches, 0x61e890:  reads entry[0]; returns entry+8 only if entry[0] is 1 or 2, else NULL
sister getter,      0x61ee70:  mov 0xaea0(%rcx,%rdx,1),%eax ; test %eax,%eax
                               lea 0xaea8(%rcx,%rdx,1),%rax ; cmovne NULL
                               -> returns entry+8 only if entry[0] == 0
```

The loader builder's model -- every slot is either a switch or an analog channel, dispatched on a kind
field -- is **confirmed at the instruction level**. Stated in his terms: the aim being an analog channel
and the trigger a switch is the right *shape*; what is wrong is *where* the aim channel lives (§5).

## 3. The two masks (task 1) -- MEASURED

Read with `probe/memlees.py`, which first proves itself (see §6). Values:

| moment | `0x1766f40` | `0x181d8a0` |
|---|---|---|
| run 20, t=12 s (before attract) | `0x00000904` | `0x00000904` |
| run 20, at attract | `0x00000904` | `0x00000904` |
| run 20, attract + 20 s | `0x00000904` | `0x00000904` |
| run 21, at attract and after every write | `0x00000904` | `0x00000904` |

**Bit `0x8` is not set in either, at any moment read.** The bits the code actually tests:

* `0x2` = not set; `0x4` = **set**; `0x6` = **set** (bit 2 only); `0x8` = not set; `0x400` = not set.
* `0x904` = bit 2 (`0x4`) + bit 8 (`0x100`) + bit 11 (`0x800`).

The readers (MEASURED, disassembler):

* **Mask A `0x1766f40`** -- three accessors in the 0x4axxxx subsystem:
  `0x4af8f0: mov 0x1766f40,%eax ; and $0x6,%eax ; ret`,
  `0x4af900: ... and $0x8,%eax ; ret`, `0x4af910: ... and $0x400,%eax ; ret`.
  So `0x4af900` is literally "is bit 0x8 set in mask A" -- the bit the loader builder asked about.
* **Mask B `0x181d8a0`** -- seven readers in the input layer (`0x61cc34`, `0x61cc55`, `0x61cc70`,
  `0x61ccf0`, `0x61f260`, `0x61f280`, plus a setter/clearer/tester triple):
  `0x61cd10: or %edi,%eax ; mov %eax,0x181d8a0` (set bits),
  `0x61cd20: not %eax ; and 0x181d8a0,%eax ; mov %eax,0x181d8a0` (clear bits),
  `0x61cd40: test %edi,0x181d8a0 ; setne %al` (test bits).
  The behavioural readers test `$0x2`, `$0x4` and `$0x6`; **nothing in the binary tests bit `0x8` of mask B**.

Both masks are **zero in the file** (BSS) and become `0x904` at runtime. The two are separate addresses
with identical values (their neighbourhoods differ: mask A sits in a run of zeros, mask B has `03000000
01000000` before it and pointers after it) -- reported as MEASURED, not explained.

*What I could not determine: the provenance the loader builder was asked for.* I asked for it (§9) and had
no answer within this lane's budget, so I do not claim what the masks mean, only what they are and who
reads which bit.

## 4. The 32-slot table at runtime: empty except what we write (MEASURED)

Run 20/21/22, attract loop, `probe/rk.py tafel` (board 0) and `probe/memlees.py` (boards 0-3):

```
bord 0 soorten: 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
bord 1 soorten: 0 0 ... 0        bord 2: idem        bord 3: idem
vak 0  0x1815d20  [0]=1 [6]=13 [7]=13      <- typed as a switch by OUR helper; counters = our START pulses
vak 17 0x1816270  [0]=0 [2]=2
vak 18 0x18162c0  [0]=0 [7]=1166012416
vak 19 0x1816310  [0]=0 [2]=2
vak 20 0x1816360  [0]=0 [7]=1166012410
```

The stray data in slots 17-20 is **reproducible across runs 20, 21 and 22** (same slots, same fields, same
values), so it is a stable overlapping structure, not noise. I did not identify it.

**The living calibration of the reader** (this is the important one): the helper's own log prints
`indruk bit 6 -> bord 0 schakelaar 0; tabel NA de indruk: ... [6]=N`, and my *external* reader sees the
same counter at `0x1815d20+0x18`: 13 after 13 START pulses, 15 after 15. Two instruments, one answer, on a
live address -- not just on the ELF header. The helper reported a valid table each time
(`geen tabel: de ingebouwde standaard (alleen de kastschakelaars)`).

## 5. The aim the game actually consumes is NOT a table slot (MEASURED)

The analog getter `0x61ee70` has **exactly three callers in the whole binary** (MEASURED: scan of every
`e8 rel32` in 33554432 bytes; and there are **no** 8-byte function pointers to it, so no indirect calls
are missed): `0x4ad800`, `0x6241c4`, `0x6241d1`.

Two of them are in the **per-frame player input filler `0x624120`**, which loads its channel numbers from
rodata, not from the board table:

```
62412f: movdqa 0x11bf3a0,%xmm0 ; 624160: movaps %xmm0,0x30(%rsp)
624175: movdqa 0x11bf3b0,%xmm0 ; 624180: movaps %xmm0,0x40(%rsp)
6241bf: mov 0x30(%rsp,%r12,1),%edi ; 6241c4: call 0x61ee70      <- channel A for this player
6241c9: mov 0x40(%rsp,%r12,1),%edi ; 6241d1: call 0x61ee70      <- channel B for this player
624302: add $0x4,%r12                                           <- r12 = player * 4
624306: cmp %ebx,0x1f392d0 ; 62430c: jg 0x6241b0                <- loop while player < [0x1f392d0]
```

The rodata arrays at `0x11bf3a0` / `0x11bf3b0` are `793, 798, 803, 808` and `794, 799, 804, 809`
(MEASURED) -- stride 5, so **player 1 = channels 793 and 794**. Those numbers fall *outside* the table's
`0x239..0x298` range, and the getter's dispatch sends them to **fixed-address stubs** (MEASURED, jump
tables at `0x11bcd70` and `0x11bc68c`):

| signal | analog getter `0x61ee70` | switch getter `0x61e890` |
|---|---|---|
| 793 (0x319) | fixed address **`0x181d418`** | NULL |
| 794 (0x31a) | fixed address **`0x181d538`** | NULL |
| 795 (0x31b) | NULL | fixed address `0x181d658` |
| 796 (0x31c) | NULL | fixed address `0x181d718` |
| 797 (0x31d) | NULL | fixed address `0x181d7d8` |

**So the loader builder's expectation that the aim is an analog slot of the class-0x4 board table is
contradicted at the aim consumer.** And a second consequence, also MEASURED: **no code anywhere calls the
analog getter with a board-table signal** -- its only three callers pass 793/794-style numbers or entries
from a runtime list. For the 32-slot table the analog getter is effectively unused; the table is read
through the *switch* getter.

Inside the filler, the aim pair strongly suspected before (`0x1f392a0`, via `0xdd6c60`) is compared with
the two channels' floats, and the outcome is written into the per-player structure `[0x181f7e0] + player*0x48`:

```
62420c: movss 0x14(%r15),%xmm1 ; 624212: ucomiss %xmm0,%xmm1 ; 624215: ja 0x624238
624217: ucomiss 0x18(%r15),%xmm0 ; 62421c: ja 0x624238
624224: movss 0x14(%r13),%xmm2 ; 62422a: ucomiss %xmm1,%xmm2 ; 62422d: jbe 0x6243f0
624238: orl  $0x10000,(%rax) ; orl  $0x10000,0x120(%rax)        <- SET   bit 0x10000
6243f0: ucomiss 0x18(%r13),%xmm1 ; 6243f5: ja 0x624238
62440e: andl $0xfffeffff,(%rax) ; andl $0xfffeffff,0x120(%rax)  <- CLEAR bit 0x10000
```

The constant compared against the aim is `-1.0f` (`0x1139ba4`), consistent with `-1` meaning "no aim".

## 6. What the game *does* poll in the board table at runtime (MEASURED)

The `0x4axxxx` subsystem walks a **runtime list** (counter at `0x1771850`, records at `0x1771858`) and
calls **both** getters with each entry's signal number (`0x4ad800` analog, `0x4ad80c` switch). Measured in
run 21: the counter was **3 at t=11 s and 47 at attract**, and the signals in it were

```
596 597 598 599 600 602 607 608 648 652
```

which with the proven formula (`board = (n-0x239)>>5`, `slot = (n-0x239)&31`, `0x239 = 569`) are:

| signal | board | slot |
|---|---|---|
| 596, 597, 598, 599, 600 | 0 | **27, 28, 29, 30, 31** |
| 602, 607, 608 | 1 | 1, 6, 7 |
| 648, 652 | 2 | 15, 19 |

Their kind field is 0, so the switch getter returns NULL and the analog getter returns `entry+8`:
**these are the only slots that are read as analog channels at runtime.** Separately, the game polls four
signals from static globals as switches (`0x16acc60 = 576` = board 0 slot 7, and `0x16acc70 = 569`,
`0x16acc74 = 601`, `0x16acc78 = 633` = board 0/1/2 slot 0); slot 0 of each board is exactly what our helper
types as START, which is why START works.

INFERRED (not proven): the empty kind fields are **the missing board image**, not the intended state --
i.e. on a real cabinet the driver that fills this table also sets the kinds, and the ten polled slots are
probably meant to be a mixture. The loader builder's class-0x4 feed function `0x6208e0` is where that
would show.

## 7. The probes: prediction, outcome, and one probe I got wrong

### Run 20 -- the mask read (passive)
*Prediction stated beforehand:* the reader must pass K1-K4 or its values count for nothing; the masks are
read, not written, so there is no "effect" to see; success = a calibrated value plus the `0x8` verdict.
*Outcome:* instrument calibrated; masks `0x904`/`0x904`, bit `0x8` clear, stable over 20 s. Attract after
13 START pulses. **A negative that is only a read is not a result about input, and I do not treat it as one.**
*Identity:* `librg_rt_input.so 8199ef9b…`, `play-native.sh 9f1fe809…`, `memlees.py 0d44195c…`,
`pols.py d1a52693…`, `run20_uitvoeren.sh e0b28c56…`, commit `5c3ba99`. Log: `gamelog-run20.txt`,
`run20-launcherlog.txt`, `run20-uitvoer/`.

### Run 21 -- can the aim path be made to consume our values?
*Prediction stated beforehand, per phase:* phase 1 (player count 4) no effect, because the filler looks up
a device block per player first; phase 2 (+ device block present, port open) the filler body runs;
phase 3 (+ analog bounds and the aim pair) **the flag bit `0x10000` must be SET at `[0x181f7e0]+0` and
`+0x120`**, and 3b/3c must then toggle it the other way -- a toggle keyed to our writes would be proof the
aim logic consumed them.
*Outcome:* **no effect, and the prediction cannot even be reached**: `[0x181f7e0]` is **NULL** in the
attract loop (MEASURED, printed in every rapport) -- the structure the result would be written into does
not exist. Every write landed and was read back immediately ("SCHRIJF LANDT": `0x1f392d0 = 4`,
`0x1a7cb20 = 1`, `0x1aa37c0 = 0`, `0x181d42c = 1e9f`, `0x181d54c = -1e9f`, `0x1f392a0/4 = 0.5f`) and all of
them were still there seconds later, i.e. the game neither consumed nor reset them. The game did not
crash and still answered START (counter 13 -> 15), so the run was alive, not hung.
*Two explanations remain and I can not separate them (this is the honest limit of this probe):* either
`0x624120` is not called during the attract loop at all, or its per-player device lookup `0xdd4b10`
returns 0 for player 0 despite the block we filled.
*Identity:* `rk.py 1a6c0342…`, `run21_uitvoeren.sh 4c551674…`, same helper and launcher as run 20.
Log: `gamelog-run21.txt`, `run21-launcherlog.txt`, `run21-uitvoer/`. Run 14:45:46-14:49:38, exit 0.

### Run 22 -- do analog slots of the class-0x4 table reach the game?
*Prediction stated beforehand:* write a unique 4-byte value into each of board 0 slots 27-31 (the slots the
game actually polls), then search the whole process image. Success = the value appears somewhere other
than the write site, i.e. the game stored it. No effect = it appears only where I wrote it. Wrong address =
the searcher does not even find it at the write site, which must **not** be counted as "no effect".
*Outcome:* **the values were written and confirmed at the write site, and were found nowhere else.**
MEASURED, corrected search (§8): each magic found exactly 6 times, all six inside its own slot at
`0x1816598 / 0x18165e8 / 0x1816638 / 0x1816688 / 0x18166d8`; the control value `0x0BADFFFF` (never written)
appears twice, both in `/usr/lib/libicudata.so` -- a byte coincidence, not our value. The search covered
398 regions / 798 MB, including the game's own data segment `0x1661000-0x23d3000`.
**So in the attract loop the game does not take the analog values of board 0 slots 27-31 anywhere.**
*Caveat, and it matters:* "not stored" is not the same as "not read". A path that reads the value and only
*compares* it (threshold, change detection) leaves no trace and would look identical to this instrument.
*Identity:* `rk.py aad11ba0…`, `zoek.py ab0ec1dc…`, `run22_uitvoeren.sh f01b45a0…`. Run 14:51:22-14:54:4x.

## 8. Instrument faults found today -- in my own instruments, before they could produce a false answer

The project's rule is that an instrument which cannot prove it is alive may not report a negative. Four
things went wrong in this lane; all four were caught by a control, not by luck:

1. **`struct.unpack('<16I', d)` on an 80-byte buffer raises** (`unpack requires a buffer of 64 bytes`).
   The first run's table dump died silently in the middle (the whole `C.` section of run 20 is a
   traceback). Fixed to `unpack_from`, and the reading was repeated while the game was still alive.
   *This is why run 20's own output has no table map and the map comes from the re-read at 14:39:39.*
2. **A search for values that were never written.** In run 22, `printf '0x0BAD%04X' 27` produced
   `0x0BAD001B` (the *hex* of the decimal slot), so the values actually written were `0x0BAD001B..1F`
   while the search looked for `0x0BAD0027..2B`. The first search found nothing -- correctly, because the
   values were not there. **It was not a negative; it was a broken probe.** Corrected and re-run inside the
   same game session (14:53:50), which is where the real result in §7 comes from. A read-back control
   (`SCHRIJF LANDT`) passed both times, because it compared against the value actually written -- the
   lesson is that a read-back proves the *write path*, not that the *name of the value* is right.
3. **A fingerprint that measured the mount, not the disk.** `find … -printf '%p %s %T@' | sort | md5sum`
   over the NAS returned `7e4d8b9c…` around 14:37-14:41 and `ed6961e1…` from 14:45 on, each time twice in a
   row, with nothing written. The CIFS client re-rounds mtimes when the autofs mount is re-established, so
   the hash is unstable. **The check that means something is `find "$NAS/g7" -newermt '<today>'`, and it is
   empty** before and after every run in this lane: **nothing on the NAS was written.** The tmpfs copy of
   the binary used for disassembly lives in `/tmp`, and the game's own writes go to the bwrap overlay upper
   in `~/.local/share/ravengun/native/Halo/0/upper`.
4. **A background job killed by the SSH session.** The first attempt at run 20 (14:34:30) died 2 s in:
   the game's own log shows `Received FATAL signal SIGTERM (15)` and gamescope logs
   `gamescopereaper: Parent of gamescopereaper was killed. Killing children`. A `setsid nohup` is **not**
   enough; long work must go through `systemd-run --user` (runs 21 and 22 did, and survived). A "run" that
   is silently terminated looks exactly like a run whose probe had no effect.

Calibrations that did pass, and are what the results rest on:

* `memlees.py` K1-K4: exe is `/pm/g7/halo/game`; ELF header `7f454c46…` at `0x400000`; file vs process
  byte-for-byte on three regions (100%, 91.8%, 100%); **an unmapped address (`0x1000`) must fail, and it
  does** (`EIO`). Instrument declared calibrated, exit code 0.
* `rk.py`: every write is followed by a read of the same address; `SCHRIJF LANDT` / `LANDT NIET`.
* `zoek.py`: **positive control satisfied** -- it finds values we wrote at the place we wrote them;
  negative control value found only in unrelated libraries.

**Two of the four are not Halo facts and must leave this dossier.** (a) `setsid nohup` does not survive the
end of the SSH session on this machine: a long run started that way is killed within seconds and, because
the kill takes the game down with a SIGTERM and gamescope's reaper with it, it looks exactly like a run
whose probe had no effect. Long work must go through `systemd-run --user --unit=… --collect`. (b) A
filesystem fingerprint built on `%T@` measures the mount and not the disk, because the CIFS client
re-rounds mtimes when the autofs mount is re-established; an unstable hash over an untouched tree will
otherwise read as "the NAS was written". Both were relayed to the coordinator for the other lanes.

## 9. What I could not determine -- carried forward as OPEN, not closed

**These are not gaps left behind; they are questions this lane hands on deliberately.** For items 1-4 the
evidence I have does not close the question, and writing a closure anyway would be a guess dressed as a
result. They are to stay open even if a later reading seems to answer them cheaply.

1. **Whether `0x624120` runs at all during the attract loop** (versus `0xdd4b10` returning 0 for player 0).
   This is the single most valuable next measurement: it decides whether the aim path is "not on the
   execution path" or "on it but gated". A tracer-free way to see it, and the reason this stays open rather
   than assumed: the function's own success branch executes `orl $0x10000,(%rax)` with `rax` derived from
   `[0x181f7e0] + player*0x48`, and `[0x181f7e0]` is **NULL** in the attract loop -- so if that function ran
   with a non-zero player count the game **must crash**, and that crash is itself the proof that it ran.
   Conversely, "the game did not crash" after we set the player count (run 21) is evidence that the function
   did not reach that instruction -- but it does not tell us *which* of the two causes applied.
2. **The provenance and meaning of the two masks.** Asked in writing at 14:35 as the loader builder's
   question 2; no answer inside this lane's budget. I report values, bit readers, setter and clearer only.
3. **Which field of a slot holds an analog channel's VALUE, and in what unit.** The getter hands out
   `entry+8`; the only consumer I found (`0x624120`) reads floats at `handle+0x14`/`+0x18` as **limits**,
   not as a value. For the ten polled slots I searched the whole image for a stored copy of a magic and
   found none, so I cannot say which offset a poller would read, nor the scale.
4. **Whether the polled slots are analog by design or merely untouched.** Their kind field is 0, and 0 is
   also what an untouched slot holds. "Analog by design" and "empty because nothing fills it" are
   **indistinguishable from outside the process**; the runtime alone cannot separate them, which is why this
   is open and not a conclusion of §6.
5. **What the trigger is.** The switch getter is called with board 0 slot 0/7 and board 1/2 slot 0 from
   static globals, and with the runtime list's signals; I found no call with a gun-trigger signal.
   Channels 795/796/797 (fixed addresses `0x181d658`/`0x181d718`/`0x181d7d8`) are switch-typed and belong
   to player 1's group of five, so they are the best remaining candidates -- **INFERRED, not measured.**
6. **Nothing about the camera** -- out of this lane, not touched.
7. **The 5-minute bar is NOT met.** Runs 20-22 each reached the attract loop (13, 13 and 11 START pulses;
   `Failed to load FMOD sound bank: attract` in the game log) and stayed there until the launcher's own
   `--seconds` cap stopped the game (200-230 s), i.e. **about 3.2 minutes of attract at most**, not five.
   **These were input probes, not bar attempts**; the 5-minute attract bar for this game was met by separate
   runs earlier the same day (9.5 minutes, in the other Halo dossiers). No gameplay mode was reached, no
   reticle was observed, and no photo is offered as evidence anywhere in this dossier.
8. **The question itself may be mis-stated, and that is the most useful thing to carry forward.** My
   measurement is that the table is empty, that no code ever calls the analog getter with a table slot, and
   that only our own helper types a slot at all. So "which slot is the aim" may have no answer in this
   binary: the prior question is **"who fills this table, and with what board image?"** -- which is what the
   loader builder's reading of the class-0x4 feed function `0x6208e0` answers. Until it lands, §4/§5/§6 must
   be read as *what is true in a run where nothing fills the board*, not as the game's intended wiring.

## 10. The exact invocations and where the raw evidence is

Launcher invocation for run 21 (runs 20/22 differ only in `--seconds` and the log paths; no table file was
used in any of them -- the helper ran its built-in default, i.e. the cabinet switches only):

```
cd /home/deck/projects/ravengun
timeout 300 scripts/play-native.sh --game Halo --gamescope --mute --seconds 230 \
  --bind-ro /home/deck/.local/share/ravengun/tap/start.jvs /rg/jvs \
  --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so \
  --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1
```

START was sent as bit 6 of the 32-bit control word at offset 8 of `start.jvs`
(`pwrite(fd, struct.pack('<I', 64), 8)`), which is the helper's built-in START for board 0 slot 0.

Raw evidence and scripts, all under `patches/Halo/`:

* dossiers/logs: `gamelog-run20.txt`, `run20-launcherlog.txt`, `run20-uitvoer/`, and the same for 21, 22.
* the reader/instrument: `probe/memlees.py`, `probe/rk.py`, `probe/zoek.py`, `probe/pols.py`; the static
  work: `probe/io_brug_static.sh`, `io_brug_static2.sh`, `io_brug_static3.sh`, `io_brug_static4.sh`,
  `probe/kanalen.py`, `probe/callers.py`; the run scripts: `probe/run20_uitvoeren.sh`,
  `probe/run21_uitvoeren.sh`, `probe/run22_uitvoeren.sh`, `probe/redding22.sh` (the corrected search).

**Nothing under `/home/deck/projects/ravengun` was modified** (it is the loader builder's repo); no host
service or permission changed; the NAS was not written (verified with `-newermt`, §8.3).

## 11. Status of this dossier

Written 14:56 and updated ~15:05 on 2026-10-13. The coordinator accepted §0-§8 and confirmed the findings
are what the other lanes were waiting on (the mask answer for the loader builder, the closed address model,
and the empty-table structural fact). §9 items 1-4 are recorded **open on the coordinator's explicit
instruction**, together with the tracer-free test in item 1 and the re-framed question in item 8.

**Pending input, not mine to obtain:** the loader builder's reading of the class-0x4 feed function
`0x6208e0` (which signal numbers it writes, and whether it writes the kind field) and the provenance of the
two masks. When those land, §9 items 1 and 4 become decidable, and the one-run test that follows is already
built: `probe/rk.py` (read/write with read-back proof), `probe/zoek.py` (whole-image search with a positive
control at the write site and a negative control on a value never written) and `probe/memlees.py`
(K1-K4 calibration before any value is reported).

