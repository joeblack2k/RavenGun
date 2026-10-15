# IRTrack Rev. 2 gun-camera protocol (Halo: Fireteam Raven, Linux x86_64)

*RavenGun, 2026-10-15. Static analysis only. Nothing was started; no bench, no sudo, NAS read-only.*

Binary: `/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game` (ET_EXEC, vaddr = file offset + 0x400000).
Disassembly: `objdump -D --no-show-raw-insn -b binary -m i386:x86-64 --adjust-vma=0x400000` over 0xdc8000-0xdec000
(cmgr + IR library) and 0xff9000-0x1001000 (libftdi). Every item below is labelled **MEASURED** (read from the
code, address given) or **INFERRED** (my interpretation). "NOT DETERMINED" means exactly that.

Struct and global names used below are mine. `conn` = one camera-connection record. There are 32 of them
at `0x1a7cb20`, stride `0x1348`, ending at `0x1aa3420`.

---

## 0. Summary for the emulator author

* The cabinet's guns are **FTDI FT232R-class devices, VID 0x0403 / PID 0x6001**, USB manufacturer string
  `"Raw Thrills, Inc."` and product string `"IRTrack - Rev. 2"`, matched exactly with NUL. The USB serial
  string identifies the gun (MEASURED).
* Link: **1,500,000 baud**. 230,400 is the fallback. The game uses no latency, line-property or flow-control
  calls (MEASURED).
* Gun to host: a **printable-ASCII message stream**. Each message is
  `<type char> <hex payload> 'z' <2 hex checksum chars>`, with type in `{G,Q,S,X,Y,Z}`. The checksum is the
  8-bit sum of every byte from the type char through the `'z'`. A checksum of `00` is accepted without
  checking (MEASURED).
* Host to gun: short `W` commands, `W<digit>[args]z<cs>`, with the same checksum. An emulator can accept
  and ignore them (MEASURED format, INFERRED meaning).
* To keep a connection healthy, the emulator must send:
  * `X` (IR blobs) with **no gap longer than 100 ms**. In practice send 60-100 Hz.
  * `G` (switches) at least every 5 s.
  * `Y` (status) at least every 15 s, with frame-count field 141..159 (or 291..309), stuck-bits `& 0x70 == 0`
    and firmware byte != 0.

  With that, nothing ever resets or drops the connection (MEASURED thresholds, §2.6).
* Trigger and buttons come **in the same stream**, in the `G` message nibbles. cmgr never touches the RIO
  board (MEASURED that cmgr has no other input; whether the game also uses RIO for guns: NOT DETERMINED).
* Aim: the game solves the aim from the **IR blob constellation** (cabinet LED pattern) in its IR library.
  Synthesising blobs that land on a chosen screen point needs the cabinet LED geometry and the solver, and
  that is **NOT DETERMINED** here. The robust route is to feed healthy packets for "connected" and write
  the aim at the setter `0xdd5cb0` / array `0x1f392a0` (see §4).

---

## 1. libftdi usage

### 1.1 Function map (MEASURED: identified by libftdi 1.x error strings and argument patterns)

| addr | libftdi 1.x | used by cmgr? |
|---|---|---|
| `0xff98c0` | `ftdi_new()` (ftdi_init inlined: malloc 0x70, read/write timeout 5000 ms, readbuffer 4096) | yes |
| `0xff9bb0` | `ftdi_free()` (ftdi_deinit inlined) | yes |
| `0xff9cd0` | `ftdi_usb_find_all(ftdi, &list, vendor, product)` | yes |
| `0xffa020` | `ftdi_list_free(&list)` | yes |
| `0xffa0b0` | `ftdi_usb_get_strings(ftdi, dev, mfr, 128, desc, 128, serial, 128)` | yes |
| `0xffa2b0` | `ftdi_usb_reset()` ("FTDI reset failed") | yes |
| `0xffa4d0` | `ftdi_usb_purge_buffers()` | yes |
| `0xffa5f0` | `ftdi_usb_close()` | only in utility `0xdd3b50` (no direct caller) |
| `0xffab40` | `ftdi_set_baudrate()` | yes |
| `0xffb120` | `ftdi_usb_open_dev()` | only `0xdd3b50` + firmware programmer |
| `0xffb590` | `ftdi_usb_open_desc_index()` (internal) | via `0xffb8d0` |
| `0xffb8d0` | `ftdi_usb_open_desc(ftdi, vid, pid, desc, serial)`: `jmp 0xffb590` with index 0 | yes |
| `0xffb910` | `ftdi_usb_open(ftdi, vid, pid)` | no |
| `0xffbe00` | `ftdi_write_data()` | yes |
| `0xffc250` | `ftdi_read_data()` | yes |
| `0xffc660` | `ftdi_set_bitmode(ftdi, mask, mode)` (SIO request 0x0b) | yes |
| `0xffc7e0` | `ftdi_read_pins()` | firmware programmer only |
| `0xffc890` | `ftdi_set_latency_timer()` | firmware programmer only |
| `0xffbbc0` | `ftdi_set_line_property()` | firmware programmer only |
| `0xffcb00` | `ftdi_setflowctrl()` | firmware programmer only |
| `0xffcc60` | `ftdi_setrts()` | firmware programmer only |
| `0x1000d50` | `ftdi_get_error_string()` | yes (log only) |

`ftdi_init`, `ftdi_usb_open_desc_index` and `ftdi_usb_open_dev` are **not** called directly by cmgr.
There are no latency, line-property, flow-control or DTR/RTS calls in cmgr (MEASURED: complete call list of
0xdcd000-0xdd8000).

**Direct struct access (MEASURED):** cmgr reads `ftdi_context->usb_dev` (offset **+0x08**) itself in two places:
* `0xdd109a`: USB node reset, `libusb_reset_device(ftdi->usb_dev)` at `0xd40860`.
* `0xdcf7d9`: speed query via `0xd3fe20` / `0xd3fdf0`.

Both are skipped when usb_dev is NULL; the function then returns -1. **An emulated context must therefore
keep +0x08 == NULL** (or a valid libusb handle). Everything else cmgr only passes through.
`ftdi_device_list` is used as `{next @+0, dev @+8}` (MEASURED: `0xdd221e`, `0xdd2226`).

### 1.2 Discovery: `cmgr_thread_main` `0xdd19e0`, scan block `0xdd2123..0xdd2409` (MEASURED)

```
ftdi = ftdi_new();                                   // dd2123
n = ftdi_usb_find_all(ftdi, &list, 0x0403, 0x6001);  // dd2148  (n<0: log 0x1293138; n==0: nothing)
for (d = list; d; d = d->next) {
    ftdi_usb_get_strings(ftdi, d->dev, mfr,128, desc,128, serial,128);   // dd224d (err: log, continue)
    rec = {vid=0x403, pid=0x6001, mfr, desc, serial};                     // 0x188-byte record
    if (memcmp(mfr,"Raw Thrills, Inc.",18)==0 && memcmp(desc,"IRTrack - Rev. 2",17)==0) {  // dd21f7, dd2207
        devtab[k++] = rec;                           // 0x1f39420, stride 0x188, max 32 (count @0x1aa3420)
        if (already_connected(rec))  continue;       // 0xdd0200 compares vid/pid/mfr/desc/serial of live conns
        if (single_gun_mode && player0 busy) continue;     // 0x1aa3440
        cmgr_connection_add(&rec, &conn);            // dd238b
        sleep(1);                                    // dd2398
        if (guninfo_mode) guninfo_update(...);       // 0xdd4600
    }
}
ftdi_list_free(&list); ftdi_free(ftdi);
```

The same matching appears in a second enumerator at `0xdd0a50` (find_all 0x403/0x6001 at `0xdd0ae6`,
get_strings at `0xdd0b27`, compares at `0xdd0b32` / `0xdd0b45`, max 32 entries).

**When is a scan done?** (MEASURED `0xdd1b84..0xdd1ba9`): only if the number of live connections is below the
gun count `0x1f392d0`, **and** (rescan counter `0x1aa3434 > 0` **or** continuous-scan flag `0x1aa3430 != 0`),
**and** the callback at `*0x1aa35c8` returns 0. `cmgr_init` sets `0x1aa3434 = 2`, so it scans twice at start.
Each killed connection adds 1. Between iterations the thread sleeps in 50 ms steps for up to 2 s
(`0xdd1f38..0xdd1f5f`).

**Device identity**: the USB serial string, up to 127 chars. cmgr only uses it with `strcmp`/`strncpy`.
No format is enforced (MEASURED). Stats keep 32 chars (`0xdd0e1c`) and the guninfo table keeps 32 chars.
Real serial format: NOT DETERMINED.

### 1.3 Opening: `cmgr_connection_add` `0xdd1110` (MEASURED)

```
if (live connections > 31) fail "too many open connections"         // dd1204
thread_id = rand() % 100000, unique                                  // dd1188..dd11e4
player:  if guninfo_mode (0x1aa3444): player = guninfo_lookup(serial) (0xdd4870, /pm/g7/guninfo.txt);
         if that is <0 or >= gun count -> lowest free player index   // dd187c..dd18de
         else lowest player index not used by a live conn            // dd1235..dd1260
conn.ftdi = ftdi_new()                                              // dd1303
r = ftdi_usb_open_desc(ftdi, 0x0403, 0x6001, desc, serial)          // dd1330
    r<0: log; unless r==-5 -> USB node reset (0xdd0f90) and give up // dd191d..dd1970
3x: ftdi_set_baudrate(ftdi, baudtab[conn.baud_type])                 // dd137b, baud_type=0 -> 1500000
    on error: ftdi_usb_reset() (0xdd0e50) + usleep(1 s), retry; all 3 failed -> USB node reset
3x: ftdi_usb_purge_buffers(ftdi)                                    // dd14d9, same retry scheme
copy conn into first free slot of 0x1a7cb20[32]; init 4 mutexes
pthread_create(&conn+0x40, attr, cmgr_thread_conn_read 0xdd2420, (void*)thread_id)  // dd1735
log "SPAWNING CONNECTION ... Baud ... Player"
```

Baud table at `0x173f008`: `[0] = 1500000`, `[1] = 230400` (MEASURED). `conn.baud_type` (+0x38) starts at
0, so the open baud is **1,500,000**.

**Nothing is written to the gun at open** (MEASURED: no `ftdi_write_data` in connection_add). There is no
bitmode, latency, line-property or flow-control setup. The gun is expected to stream on its own.

### 1.4 Players and guns per cabinet (MEASURED)

* Gun count `0x1f392d0` is set by `irt_systemSetCabinetType` `0xdd4bc0`: cabinet type 7, 8 or 9 gives
  **2 guns**; type 10 or 11 gives **4 guns**. Any other type is an error ("bad cabinet type", `0x1293738`).
  The expected count `0x1f39400` is set to the same value at `0xdd4df5`; the game later changes it via
  `0xdd4d90` (callers at 0x43477b etc.).
* The game chooses the type at runtime (`0x61c940`, arg 3 gives 11, 5/7 gives 7, 6 gives 8, 8 gives 9) and
  passes it as `irt_init(type, x)` = `0xdd5400` (cmgr flags = 1 = guninfo mode) from `0x624015`. Which value
  Halo uses on a given cabinet: NOT DETERMINED statically. INFERRED: the 4-player Halo cabinet uses type 11.
* Max 32 connections (`0xdd1204`). Player index = `conn+0x3c`.

### 1.5 Other libftdi calls during operation (MEASURED)

| where | call | purpose |
|---|---|---|
| `0xdd27cd` | `ftdi_read_data(ftdi, buf, 1)` | **reads ONE byte per call**, loop body of the read thread |
| `0xdd2dfd` | `ftdi_write_data(ftdi, conn+0x1330, conn+0x1340)` | sends a pending `W` command (≤16 bytes) |
| `0xdd0c60` | `set_bitmode(0xff, 1 /*BITBANG*/)`, `purge`, `write_data(256 × 0x00)`, `purge`, `set_bitmode(0xff, 0 /*RESET*/)` | **FPGA reset** (stat "FPGA force reset"). Returns 0 only if the write returns 256 |
| `0xdd0e50` | `ftdi_usb_reset()` | "FTDI force reset" |
| `0xdd0f90` | `libusb_reset_device(ftdi->usb_dev)` | "USB node reset" |
| `0xdd3178` | `ftdi_usb_purge_buffers()` | after an FPGA reset |
| `0xdd344f` | `ftdi_set_baudrate(baudtab[toggle])` | baud change after 15 s of silence (only for cabinet types 1/5/6, so **not** for types 7..11, MEASURED `0xdd327a..0xdd3293`) |
| `0xdd1d4f` | `ftdi_free()` | on connection kill (no `ftdi_usb_close`) |

`ftdi_read_data` return handling (MEASURED `0xdd27dd`, `0xdd2e56..`):
* `> 0`: parse.
* `== 0`: no sleep, the loop just continues. Real hardware blocks about 16 ms in the bulk read, so **an
  emulator should block briefly (1-16 ms) before returning 0** to avoid a hot spin.
* `== -16`: `usleep(10 ms)` and continue.
* `== -19` or `-666`: "Device missing", stat "FTDI read fail NODEV", thread stops.
* any other negative: `ftdi_usb_reset`; if that fails, USB node reset; then the thread stops.

---

## 2. The wire protocol

### 2.1 Framing: read thread `0xdd2420` (MEASURED)

Byte state machine (`0x70(%rsp)`, states 0..3). The start-char set is a bitmask `0xe1401` over `c - 0x47`,
which gives **`G` `Q` `S` `X` `Y` `Z`**. The terminator is **`'z'`** (0x7a).

```
state 0: skip bytes until a start char -> buf[0]=c, len=1                         (dd2a7d..dd2ac6)
state 1: c in start-set           -> error (CRC counter++), resync                (dd2b38)
         c == 'z'                 -> buf[len++]='z', go to state 2                (dd2af7, dd34c0)
         else                     -> buf[len++]=c   (max 0x27fe bytes)            (dd2aff)
state 2: first checksum char  (if c in start-set or 'z' -> error)                (dd34ee)
state 3: second checksum char (same rule) -> verify                              (dd2828)
verify:  chk = strtol("<c1><c2>", 0, 16);  sum = (uint8) Σ buf[0..len-1]          (dd2857..dd2a34)
         ok if (sum ^ chk)&0xff == 0  OR  chk == 0
```

* **Checksum**: an 8-bit additive sum of everything from the type char up to and including `'z'`, sent as
  2 hex chars (either case). A value of **0 is always accepted** (MEASURED `0xdd2a41`).
* Bad checksum: `conn+0x60++` ("RECORDED %d CRC ERRORS" at thread end), status slot 1 marked failed
  (`0xdd87a0(p,1)`). **CRC errors never cause a reset or disconnect** (MEASURED: counter only read for logs and
  the stats accessor `0xdcf8a0`).
* Good message: `conn+0x5c++` (packets), `conn+0x64 = 1`, `conn+0x68 = 0`, last-good-time = now, then
  `parse(buf, conn)` `0xdd6f90`, then status slot 1 OK (`0xdd87f0(p,1)`).
* The payload must not contain `G Q S X Y Z z`. Hex digits `0-9 A-F a-f` are safe.

### 2.2 Message types: parser `0xdd6f90`, jump table `0x1293840` (MEASURED)

Fields are fixed-width hex, parsed with `strtol(..., 16)`. Offsets count from the type char (index 0).

#### `X`: IR blob list ("led message"), handler `0xdd7058`

Header: `X NN`, chars 1-2 = blob count N (hex). `conn+0x134` = now (led-message time). If N > 8,
`conn+0x138` = now (a "too many LEDs" timestamp; not used in the health check).

**baud_type 0 (1.5 Mbaud, the normal case)**: N records of **16 chars** each, starting at char 3:

| chars | meaning |
|---|---|
| 0-4 | **Σx**: 5 hex digits (x-moment, i.e. centroid × weight) |
| 5-9 | **Σy**: 5 hex digits |
| 10-11 | **w**: weight / pixel count, 2 hex digits |
| 12-15 | ignored by this build |

Per record: `x = Σx/w`, `y = Σy/w` (floats). The blob is **kept only if 6 ≤ w ≤ 254 and 5.0 < y < 474.0**
(constants `0x113bc20` = 5.0, `0x1293890` = 474.0); x is not range-checked. Kept blobs are stored as
`{float x, float y, float w}` in `conn+0x78` (raw) and `conn+0x70` (**led_data**). When lens mode
`0x1f32c80 == 1`, they first go through the undistort at `0xdcd330` with k = 0.018 / -0.0003
(`0x1292b28`/`b2c`). The count after filtering goes to `conn+0x80` (max 255), and `conn+0xb0++` is the
"SAMPLE NUMBER".
* So the camera coordinate space is **640 × 480** (INFERRED from the y-limits). Downstream code multiplies by
  1.25 (`0x1292b30`, at `0xdcc0ce`), giving 800 × 600 with optical centre (400, 300). For cabinet type 10 the
  centre is (396.081, 303.698) (`0x1e946d0` / `0x1e946d4`, set in `0xdd4bc0`).
* `X00z32` (N = 0) clears led_data (`0xdd79e1`).

**baud_type 1 (230,400)**: N records of 6 chars (`x` = 3 hex, `y` = 3 hex, w fixed at 30.0), followed by 3
switch nibbles (same meaning as `G`). These are stored in `conn+0x70` only and also refresh the switch time
`conn+0x130` (`0xdd7e6c..0xdd8196`).

led_data buffers: `malloc(0xbf4)` = 255 × 12 bytes (`0xdd2572`, error "could not malloc led_data space").

#### `G`: switches ("switch message"), handler `0xdd75d0`

`conn+0x130` = now. In baud_type 0, chars 1, 2 and 3 are **single hex nibbles** a, b, c:

| field (conn+) | value |
|---|---|
| sw0: count `0xb4`, raw `0xc8`, state `0xdc` | a>>1, a, a&1 |
| sw1: `0xb8`, `0xcc`, `0xe0` | b>>1, b, b&1 |
| sw2: `0xbc`, `0xd0`, `0xe4` | (b+1)>>1, b+1, (b+1)&1 (literal code: b+1; zero if b+1 > 15) |
| sw3: `0xc0`, `0xd4`, `0xe8` | c>>1, c, c&1 |
| sw4: `0xc4`, `0xd8`, `0xec` | (c+1)>>1, c+1, (c+1)&1 (same oddity) |

In baud_type 1, `G` fills sw0, sw1 and sw3 from a, b, c (`0xdd7cfe`).

INFERRED: each nibble is `(press_counter << 1) | current_state`. bit0 is the live switch state and bits 1-3
are a 3-bit press counter (the game sees a press as a counter change). Stats names "No recent trig press" /
"No recent pump press" (`0x1292cbe` / `0x1292cd3`) suggest **sw0 = trigger** and another switch = pump. Which
nibble is the pump: NOT DETERMINED.

Game-side accessors (exported, no direct callers):
* `0xdd5da0` → `0xdcf6c0(p,0)` = state of sw0
* `0xdd5d90`/`0xdd5dd0` → `0xdcf580(p,i)` = counts
* `0xdd5e50` → `0xdcf620(p,i)` = raw nibbles

#### `Y`: status ("status message"), handler `0xdd76c8`

`conn+0x13c` = now. Layout (char index: width, destination):

| chars | width | conn+ | used for |
|---|---|---|---|
| 1-4 | 4 hex | `0x118` | **camera frame count**. Must be in [0x8d..0x9f] = 141..159 or [0x123..0x135] = 291..309, else "Camera frame count err" (`0xdd2c87`) |
| 5-8 | 4 hex | `0x11c` | not checked in cmgr |
| 9-12 | 4 hex | `0x120` | not checked |
| 13-16 | 4 hex | `0x124` | not checked |
| 17-18 | 2 hex | `0x128` | **stuck bits**: `& 0x70` must be 0 (else "Camera stuck bits err"). If `conn+0x54`, low nibble `0xf` for more than 5 s is also an error |
| 19-20 | 2 hex | `0x129` | **firmware version**. **Must be != 0** (`0xdd2cf1`), else reset. Copied to `0x1aa36c0[player]` (`0xdd5440`) |
| 21-22 | 2 hex | `0x12c` | hw / type byte, returned by `0xdcf760` |

Firmware version: the game compares it with the newest `/pm/irgun/firmware/prom_%02x.mcs` (`0xdd5550`,
`0xddcb30`). A firmware update (`0xdddf90`, bit-banged FPGA flash through the FTDI) is only started from
exported functions `0xdd5670` / `0xdd56b0`, which have no direct callers (INFERRED: operator menu). If the
version is 0, `0xdd5470` sends `W7z08` and waits for a `Y`.

#### `Q`: handler `0xdd7620`

`conn+0x140` = now. Payload ignored. Not checked in the health loop. INFERRED: a heartbeat.

#### `S`: handler `0xdd7640`

Chars 1-4 → `conn+0x12a` (u16). Not checked.

#### `Z`: handler `0xdd73e8`

15 × 8 chars. Each group is hex2 a[2i], hex2 a[2i+1], hex2 b[2i], hex2 b[2i+1]. Then
`0x1aa36a0[player] = (a[0..29] == b[0..29])` (`0xdd5730`). INFERRED: a self-test or echo. Not needed.

### 2.3 Host to gun commands: `0xdcf970(player, cmd, data)`, table `0x1292db4` (MEASURED)

The command is built into `conn+0x1330` (len `conn+0x1340`) and sent by the read thread at `0xdd2dfd`.
Format: `'W' <digit> [raw arg bytes] 'z' <cs as "%02x">`, with cs = 8-bit sum of all preceding bytes.

| cmd | bytes | when (callers) |
|---|---|---|
| 0 | `W1z02` (also `conn+0x144` = now) | **camera reset** (`0xdd30e2`, `0xdd32ed`, thread start `0xdd2535`) |
| 1 | `W2` + 4 bytes + `z` + cs (9 bytes) | NOT DETERMINED |
| 2 | `W3z04` | `0x415667` |
| 3 | `W4z05` | NOT DETERMINED |
| 4 | `W5` + 6 bytes + `z` + cs (11 bytes) | `0xdd607d`, `0xdd645d` ... with data from `0x1aa3820` (INFERRED: gun LED / feedback) |
| 5 | `W6` + 1 byte + `z` + cs | NOT DETERMINED |
| 6 | `W7z08` | request firmware/status (`0xdd547c`, `0x415656`) |

Write failure (<0): status slot 2 failed (`0xdd87a0(p,2)`). Success: slot 2 OK. **An emulator may accept
writes, return the length, and ignore them.**

### 2.4 Rates (MEASURED thresholds; rates INFERRED)

* `X` gaps > 100 ms are recorded (`0xdd3568..0xdd35a7`, a 1024-entry timestamp history at `conn+0x174`).
  If the 50th-previous gap event is less than 60 s old, or the 100th less than 300 s old, the gun is "Boggy gun
  detected". **So send `X` continuously with gaps ≤ 100 ms.** The real camera probably runs at 150 frames
  per status period (frame-count window), INFERRED about 150 Hz.
* `G`: at least every 5 s. `Y`: at least every 15 s. A valid message of any type: at least every 15 s.

### 2.5 Startup grace

The health check first runs after a **30 s** countdown (`0x7530` at `0xdd265a`). After that it runs on every
loop pass while healthy. During a firmware update (`conn+0x12fc != 0`) it is skipped.

### 2.6 Health check and reset ladder: `0xdd2bf8..0xdd2dab`, `0xdd3080..0xdd3384` (MEASURED)

Two kinds of trouble flags are collected:

| condition | stat idx (name) | flag |
|---|---|---|
| now − last `G` > 5000 ms | 0 "No recent switch message" | r15 |
| now − last `X` > 5000 ms | 1 "No recent led message" | r14 |
| now − last `Y` > 15000 ms | 2 "No recent status message" | r15 |
| now − last valid msg > 15000 ms | log "... seconds of inactivity" (+ baud toggle for types 1/5/6) | r15 |
| `Y.frame` outside 141..159 / 291..309 | 5 "Camera frame count err" | r14 |
| `Y.stuck & 0x70` | 6 "Camera stuck bits err" | r14 |
| `Y.fw == 0` | none | r15 |
| boggy (§2.4) | 11 "Boggy gun detected" | r14 |

Stat index → name: base array at `0x1f3c520` (stride 0x5c, `0x20 + 4*idx`), names at `0x1292c76`..`0x1292d9e`
in the same order: 0 switch, 1 led, 2 status, 3 trig press, 4 pump press, 5 frame count, 6 stuck bits,
7 camera force reset, 8 FPGA force reset, 9 FTDI force reset, 10 FTDI read fail NODEV, 11 boggy,
12 USB node force reset, 13 USB bulk reset request, 14 system reset request.

* All clear: consecutive counter = 0, `0x1aa3428 = 0`, flush pending `W` command.
* Any flag: consecutive++; if ≤ 3, send camera reset `W1z02` (cmd 0) and `sleep(1)`.
  * If r15 is set, **or** this is the 4th consecutive time: FPGA reset (`0xdd0c60`), `usleep(5 s)`, purge,
    `conn+0x68++`. **More than 3 FPGA resets within 24 h** (`0x5265c00` ms) leads to a USB node reset and the
    connection thread stops.
  * If only r14 (or boggy): stat 13 "USB bulk reset request", `0x1aa3428 = 1`, and **the connection thread
    stops** (`conn+0x4c = 1`, `0xdd337b`). The main thread then kills it (§2.7) and rescans.

### 2.7 Connection end (MEASURED)

The thread exits when `conn+0x4c` is set (by errors above, by the callback `*0x1aa35c8`, or by quit). On exit
it logs "READ %d PACKETS IN %2.2f SECONDS", "SAMPLE NUMBER", "RECORDED %d CRC ERRORS" and sets
`conn+0x50 = 1`. `cmgr_thread_main` then logs "KILLED CONNECTION", calls `ftdi_free`, records serial and
player in the "recently killed" table `0x1aa3460` (8 × 0x2c), clears the slot and does `0x1aa3434++`
(rescan).

---

## 3. From led_data to aim, buttons and flags

### 3.1 Aim pipeline (MEASURED call chain; the math is NOT DETERMINED)

1. Read thread → `conn+0x70` led_data (§2.2).
2. Per player per frame: `0xdd6163` → `0xdd5860(player)`:
   * `0xdc9030()`, `0xdc90e0(p)`, then **`0xdcbfc0(p)`**. This copies led_data via `0xdcf3e0` into
     `0x1f32ca0 + p*0xbf4`, needs **at least 3 blobs** (`0xdcc08b`), scales by 1.25 and runs LED
     identification / model fitting (`0xdcd850`, `0xdcd7d0`, `0xde6xxx`, `0xddfb20`).
   * Only for cabinet types 7..11: `0xdd8350(p, &x, &y, &n)` collects the identified LEDs and the result
     `0x1b83e80[p]` (getter `0xddfa30`). It returns 0 when x != -1 and y != -1, otherwise -1. LED count n == 0
     marks status slot 5 failed.
   * **Lost** (`0xdd58f2`): `0x1aa37c0[p] = 1`, `0x1aa37a0[p] = 0`, `0x1aa37b0[p]++`,
     `0x1e94680[p] = (-1, -1)`, `0xddf850(p, -1, -1)`.
   * **Found** (`0xdd5950`): `0xdc85b0` blends the new point with the previous `0x1e94680[p]`.
     `0x1aa37c0[p] = 0`, `0x1aa37a0[p]` = blend result, `0x1aa3690[p]` = n. Then `0xddf850(p, x, y)`.
   * `0xddf850`: unless `0x1aa3860[p] != 0`, it calls **`0xdd5cb0(p, x, y)`**, which writes **`0x1f392a0[p]`**
     (the aim pair read by getter `0xdd5c60`).
3. Cabinet LED model (INFERRED): the index tables at `0x173f420` (8 entries, types 7/8), `0x173f380` (10, type 9),
   `0x173f280` (18, type 10) and `0x173f1a0` (12, type 11) list cabinet IR LEDs in even/odd order. That suggests
   8, 10, 18 or 12 cabinet LEDs. **Their physical positions, and therefore the blob set that maps to a given
   screen point, are NOT DETERMINED.**

Aim units/range: NOT DETERMINED here. `0x1f392e0[p]` is **not** "aim index 8". It is a separate per-gun
calibration pair: setter `0xdd5cd0` logs "Gun %d Cal: (%f, %f)", and `0xdd5d40` copies it from `0x1e94700`.
This corrects the reading in `AIM-GELEZEN.md` §2.

### 3.2 The gate byte `0x1aa37c0[player]` (MEASURED writers)

* `0xdd525f`: memset 0 (init).
* `0xdd5928`: = 1 (tracking lost).
* `0xdd59c2`: = 0 (tracking found).

The getters `0xdd5c60` (aim) and `0xdd5bf0` (raw `0x1e94680`) return (-1, -1) only when the byte is > 3. With
these writers that never happens, so in practice "lost" shows up as aim = (-1, -1) from the lost branch, not
through the gate. No other writer was found by absolute address.

### 3.3 Trigger and buttons

From the `G` nibbles (sw0..sw4) of the camera stream (§2.2), read by the game through the exported accessors.
cmgr does not use the RIO IO board (MEASURED: no other input). Whether the game additionally takes gun
buttons from RIO: NOT DETERMINED.

### 3.4 Flags (MEASURED)

* `0x1aa3424` = **"System reset request"**. Set at `0xdd2022` when live connections < expected count
  `0x1f39400` for more than **10 min** (`0x927c0` ms); stat 14 is bumped too. Cleared at `0xdd205b` as soon as
  enough guns are connected. Read by `0xdd44b7` / `0xdd44c8` (getter/clear). **This is not a "ready" flag.**
* `0x1aa3428` = **"USB bulk reset request"**. Set at `0xdd3371` when a connection is dropped for the r14 class
  (§2.6). Cleared at `0xdd2dab` on every healthy check. Accessors `0xdd44a8` / `0xdd44d0`.
* `0x1f392d0` = gun count (2 or 4), from `0xdd4bc0`. `0x1f39400` = expected connected guns.
* Per-player status slots `0x1f3d100 + p*0x2c0 + slot*0x58` (`0xdd87a0` = fail, `0xdd87f0` = ok; +0x40
  state, +0x44 fail count, +0x48/+0x50 times).
  * slot 0: connection present (thread_main, every pass, `0xdd1b2c`/`0xdd1b42`)
  * slot 1: last message CRC ok
  * slot 2: last write ok
  * slot 5: LEDs seen

  INFERRED: these feed the "Player Gun is not connected / data read error / data write error / cannot see
  cabinet LED's" texts at `0x129389b..`.

---

## 4. Minimal recipe

### 4.1 Replace these libftdi entry points (in-process hook)

`ftdi_new` 0xff98c0, `ftdi_free` 0xff9bb0, `ftdi_usb_find_all` 0xff9cd0, `ftdi_list_free` 0xffa020,
`ftdi_usb_get_strings` 0xffa0b0, `ftdi_usb_open_desc` 0xffb8d0, `ftdi_set_baudrate` 0xffab40,
`ftdi_usb_purge_buffers` 0xffa4d0, `ftdi_read_data` 0xffc250, `ftdi_write_data` 0xffbe00,
`ftdi_set_bitmode` 0xffc660, `ftdi_usb_reset` 0xffa2b0, `ftdi_get_error_string` 0x1000d50.
For safety also cover `ftdi_usb_close` 0xffa5f0 and `ftdi_usb_open_dev` 0xffb120.

* `ftdi_new`: return a zeroed ≥0x70-byte block with **+0x08 = NULL**. Keep your own state elsewhere, keyed by
  pointer.
* `find_all(…, 0x403, 0x6001)`: return 2 (or 4), with a list of `{next, dev}` nodes, where dev is a fake
  non-NULL token per gun.
* `get_strings`: mfr `"Raw Thrills, Inc."`, desc `"IRTrack - Rev. 2"`, serial e.g. `"RVG00001"` /
  `"RVG00002"` (any unique string ≤ 31 chars). Return 0.
* `open_desc`: bind the context to the gun whose serial matches. Return 0.
* `set_baudrate`, `purge_buffers`, `usb_reset`, `set_bitmode`: return 0. `write_data`: return size (the FPGA
  reset requires 256).
* `read_data(ctx, buf, 1)`: hand out one byte of the generated stream; if none is pending, sleep about
  1-2 ms and return 0.

Player mapping: guninfo mode is on (`irt_init` passes flags = 1). A serial missing from `/pm/g7/guninfo.txt`
gets the lowest free player index (MEASURED). Order of enumeration = order of player assignment for a fresh
file.

### 4.2 Byte stream per gun (checksums computed with the measured algorithm)

| message | when | bytes (ASCII) |
|---|---|---|
| status | every 1 s | `Y0096000000000000000100z03` (frame = 0x96 = 150, stuck = 00, fw = 01, hw = 00) |
| switches, idle | every 50-100 ms (≤ 5 s) | `G000z51` |
| switches, trigger held (sw0 bit0) | | `G100z52` |
| trigger held, press counter 1 | | `G300z54` |
| blobs, none | ≥ 10 Hz, gaps ≤ 100 ms | `X00z32` |
| blobs, one blob at (320, 240), w = 100 | | `X0107D0005DC0640000z84` |

Generic `X` record: `"%05X%05X%02X0000" % (round(x*w), round(y*w), w)`, with 6 ≤ w ≤ 254 and 5 < y < 474.
The checksum may also be sent as `00`; it is then accepted unchecked (MEASURED), which is handy while
debugging.

With these, (a) **the gun counts as connected and stays connected**: no CRC, timeout, frame-count, stuck-bit
or firmware triggers.

(b) **Aim at a chosen screen point via blobs: NOT DETERMINED.** It needs ≥ 3 blobs (MEASURED) that the IR
library identifies as cabinet LEDs. Their geometry was not recovered. Recommended: feed `X` with N = 0 or a
static blob set to keep the link healthy, and **override the aim at `0xdd5cb0`** (`void set_aim(int p, float
x, float y)`, writes `0x1f392a0 + 8p`). Alternatively hook `0xdd8350` to return 0 with the wanted (x, y): that
keeps the game's own smoothing (`0xdc85b0`) and lost/found logic. Expect the per-frame lost branch to write
(-1, -1) when no blobs are identified; hooking `0xdd8350` or `0xddf850` avoids that fight. The units of
(x, y) still need one bench measurement (see `AIM-GELEZEN.md` and `KALIBRATIE-EN-AIM.md`).

---

## 5. Not determined (explicit)

* Physical cabinet LED layout, the identification/solver math, and therefore a blob set for a given aim.
* Units and range of the aim pair `0x1f392a0`.
* Which cabinet type value (7/8/9/11) this Halo install passes at runtime.
* Which `G` nibble is the pump/second button. The meaning of `W2` / `W4` / `W5` / `W6` and of `Y` fields
  1-3 and the hw byte.
* Real serial-number format of IRTrack guns.
* Whether the game reads any gun button from the RIO board in addition to the camera stream.
