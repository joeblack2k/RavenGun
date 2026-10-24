# Terminator Salvation

*Raw Thrills, 2010. The game is a 32-bit **Linux** program, not a Windows one: an i386 ELF. Our route:
our own 32-bit ELF loader inside a sandbox. The five-minute bar is **not met**: the best run reaches
5 seconds and then exits with code 0, without a picture.*

This page covers the game and the loader: what you need to run this title, how it is started, how its
cabinet input reaches it, and where it stops. Labels are **MEASURED** (with the numbers, timestamps,
paths and offsets that came out of the measurement), **INFERRED** (a conclusion drawn from
measurements) and **assumption** (a guess we did not test). Everything here was measured on a Steam
Deck running SteamOS, with the game files on network storage that is only read.

## 1. Platform and cabinet

- **Cabinet:** a Raw Thrills lightgun cabinet from **2010**, with two guns. **MEASURED** from the game
  itself (it asks for `p1.x`/`p1.y`/`p2.x`/`p2.y`, two start buttons and two triggers) and from the
  files.
- **The system underneath:** **32-bit i386 Linux**. The game is an ordinary glibc ELF with
  `/lib/ld-linux.so.2` as its interpreter and `DT_NEEDED` entries for OpenGL, GLUT, PhysX, Theora and
  Ogg. **MEASURED.**
- **Picture and APIs:** OpenGL through **GLUT** (the game imports 15 `glut*` functions) and **PhysX
  2.8** (`libPhysXLoader` plus `libPhysXCore`, and during the start the game opens `libNxCooking.so`).
  `DT_RPATH` in the game is `/usr/lib/PhysX/v2.8.1/`. **MEASURED.**
- **The game file we examined:** `game` — 7,764,284 bytes, **ELF32 i386, `ET_EXEC`** (not
  position-independent, fixed load address `0x08048000`), `e_entry = 0x08057240`, **13 `DT_NEEDED`**
  libraries (`libGL`, `libGLU`, `libglut`, `libpthread`, `librt`, `libtheora`, `libogg`,
  `libPhysXLoader`, `libm`, `libgcc_s`, `libc`, `libdl`, `libstdc++`), SHA-256
  `798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`. **MEASURED.** The game's folder
  also holds two further 32-bit programs (`t4-patch`, 1,368,560 bytes, and `irgun/gunrec_update`,
  1,057,072 bytes) and a third utility of 8,994,378 bytes.
- **A folder-name pitfall: MEASURED.** The folder's name on our share ends in a **space**
  (`Terminator Salvation `). That costs you an afternoon if you do not know it, because some tools then
  report "directory name invalid".

> **One misunderstanding out of the way at once:** every symbol in `game` is readable: there are 29 section
> headers, 496 PLT stubs, 1020 dynamic symbols and **zero** `0xdeadbeef` stubs; **0 of 497** import
> names have been wiped. That is a rare and pleasant starting position: everything the game imports has
> a name and can be resolved. **MEASURED.**

## 2. What the game needs

Everything below comes from a **file measurement**: we let the game run for 5 seconds with an
`LD_PRELOAD` logger that records every `open`/`fopen`/`opendir`/`stat` the game does (not `strace`; see
section 9 for why that matters). In total **88 lines** of real game activity. The failures, grouped:

| What the game asks for | How often | Does it exist in the dump? |
|---|---|---|
| **`/g3/T4Data/etranslation/english.csv`** | 2× | **YES**, as `T4Data/etranslation/english.csv` (5,168 bytes) |
| **`/g3/T4Data/etranslation_app/english.csv`** | 2× | **YES**, as `T4Data/etranslation_app/english.csv` (5,880 bytes) |
| **`/T4User/log/syslog_bb3.txt`** (absolute) | 8× | **YES**, as `T4User/log/syslog_bb3.txt` (12,019 bytes) |
| **`syslog_bb3.txt`** (relative to the working directory) | 8× | No — it exists only in `T4User/log/` |
| `/home/<user>/.PhysX.conf` and `/etc/PhysX.conf` | 1× each | **No**, nowhere |
| A configuration file of the cabinet vendor's runtime, probed next to the game, in the home folder and in `/etc` | 1× each | **No**, nowhere |
| `/dev/js0` · `/dev/dsp` · `/dev/sierra2` | 1× each | No |
| `/dev/ttyS0` | 1× | **Yes, but `EPERM`** (permissions `660 root:uucp`) |

**The core of it in one sentence: the game uses two ABSOLUTE roots, `/g3/` (its data folder) and
`/T4User/` (its user folder), and those do not exist on an ordinary machine — while the files it looks
for there are in the dump, only under a *relative* path. MEASURED.** Every other path the game opens
successfully is there, including the 13 libraries and a USB enumeration (`/dev/bus/usb/...`).

**What turns out to be optional.** The game does **not** stop on the things it cannot find: it lives on
for **4.1 seconds** after the physics configuration file and ends with **exit 0**. Those two
configuration files are also not touched in the same way: the physics one is opened with `mode="rb"`,
so it is **read**; the vendor one is only **probed** for existence with `stat` and not read.
**MEASURED** that they are missing and how they are touched; **INFERRED** that they are not an
immediate fatal error.

**Our decision about those two configuration files, and it is a principle:** we do **not** supply
them. In the dump, in the runtime that ships with it and in our own repositories there is **no source**
for their content. **Inventing content would make the next measurement useless**, because then you are
measuring your own invention. **MEASURED** that there is no source.

## 3. The official start script, or why there is none

We found **no official start script** in this folder. What is there instead is
`GeneralPurposeStateMachine.txt` (5,782 bytes, a state-machine description) and the game's data and user
folders alongside it.

**A warning for anyone reading this:** if you come across a start or installation script of a cabinet
somewhere, **do not execute it** to see what it does. Take over only the parts you need (and read them
first). Such scripts are written for the cabinet, not for your machine, and they can write to paths you
do not expect. This set contains titles whose official script wipes a whole drive, reboots the machine,
deletes the user's folder and locks an account; the examples are on the
[Official start scripts](Official-Start-Scripts) page.

## 4. The route we use

**The loader.** Our own 32-bit ELF loader maps the game's `PT_LOAD` segments at their own addresses,
applies the `R_386` relocations (`GLOB_DAT`, `JMP_SLOT`, `COPY`), builds a "shim" that exports the
game's symbols, and jumps to `e_entry`. For this game that works: the log ends with
**`jumping to e_entry 0x8057240`**, and after that the game does its own thing. **MEASURED.**

> **Note for anyone copying this:** our loader first read the number of guest symbols out of `DT_HASH`
> alone. This game has **no `DT_HASH`**, only **`DT_GNU_HASH`** — so the counter stayed at zero and the
> loader stopped before doing anything. The fix is to derive the count from
> `(DT_STRTAB − DT_SYMTAB) / 16` (here exactly **1020**) or to parse `DT_GNU_HASH` itself.
> **MEASURED** (the loader reports `1020 guest symbols (source: (DT_STRTAB-DT_SYMTAB)/16, no DT_HASH)`).

**The sandbox (bubblewrap).** We run the game in a `bwrap` sandbox with **explicit binds**, not with
`--ro-bind / /`. That is not a style choice but a necessity:

- **`--ro-bind / /` does not work** if you want to add new paths: `bwrap` cannot create `/g3` then →
  `bwrap: Can't create file /g3: Read-only file system`. You have to build the root out of **separate
  binds** (`--ro-bind /usr /usr`, `--ro-bind /etc /etc`, …); then the root itself stays a tmpfs and new
  paths can be made. **MEASURED.**
- **`/lib`, `/lib64`, `/bin` and `/sbin` are symbolic links** to `usr/…`. Take those over as
  `--symlink usr/lib /lib` and **not** as a bind of their contents. If you bind them, the relative
  chain of the **32-bit** interpreter breaks (`/usr/lib/ld-linux.so.2 -> ../lib32/ld-linux.so.2`) and
  the loader dies with **exit 127** and `env: '…/loader': No such file or directory` — while the file
  is right there. **MEASURED** (this happened to us).
- **Writable folders: use an overlay.** For a folder the game writes in (here `T4User`) we use
  `--overlay-src <source> --overlay <upper> <work> <target>`: the **source stays untouched** and all
  writes land in the `upper` folder (in `/tmp` for us). That is the clean way to let a game write
  without touching the source. **MEASURED:** the game wrote 12,481 bytes into the upper layer while the
  source file stayed at 12,019 bytes with its old timestamp.

**The exact commands.** Our run (shortened; `<GAME>` is the folder on the share, `<W>` a writable
folder in `/tmp`):

```sh
# once: the shim header this system is missing
INC=~/projects/ravengun/tools/rg-elfloader/inc        # contains gnu/stubs-32.h

bwrap --die-with-parent \
  --ro-bind /usr /usr \
  --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin \
  --ro-bind /etc /etc --proc /proc --dev /dev --dev-bind-try /dev/pts /dev/pts \
  --dev-bind-try /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd \
  --ro-bind /home /home --ro-bind /var /var \
  --ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 \
  --bind /tmp /tmp \
  --dir /g3 --ro-bind "<GAME>" /g3 \
  --overlay-src "<GAME>/T4User" --overlay <W>/upper <W>/work /T4User \
  --setenv LD_LIBRARY_PATH "<elfldr2-libs>:<lib32>" \
  --chdir "<W>/view" \
  rg-elfloader "<W>/view/game"
```

and the working directory (`<W>/view`) holds, next to the game `game`, the data folders.

## 5. Input

- **MEASURED:** the game opens `/dev/sierra2`, `/dev/ttyS0` (`O_RDWR|O_NONBLOCK`), `/dev/js0`
  (`O_RDONLY|O_NONBLOCK`) and `/dev/dsp` (`O_WRONLY`), and it enumerates USB devices through
  `opendir("/dev/bus/usb")`. In our first measurement it found real nodes there and opened them.
- **MEASURED:** our own bridge with the `RawThrillsGUN` profile worked with real Move controllers: the
  JVS-like channel connected, the aim followed the raster within about **1 %**, and the trigger and
  reload worked. That was measured with the **cabinet's own board** on the other side, not with the
  loader described here.
- **The profile for this cabinet** (in our own tooling) is called `RawThrillsGUN`: a block of 64 bytes
  with the buttons at offset 8 (test, service, coin 1/2, per player start/trigger/grenade/reload,
  volume) and **1 byte per axis at offsets 12/16/20/24** for `p1.x`, `p1.y`, `p2.x`, `p2.y`, with
  left/top = 0. **MEASURED** (that is the layout our bridge uses, and the one that worked with the real
  Move).
- **INFERRED:** that `/dev/sierra2` is this title's board transport. The name (Sierra) plus a serial
  port plus two guns points that way, but we have not measured what the game does with that node when
  it exists.
- **Our approach to that node, and why:** we supply **no** real serial port and we **do not** widen any
  permissions. Instead we make a **synthetic node** (a pseudo-terminal pair, through `socat`, bound as
  `/dev/sierra2` inside the sandbox) that our bridge can drive. **MEASURED** that the game then opens
  that node **without an error**; **INFERRED** that this is the right transport shape.
- **Permissions as a point of attention:** `/dev/ttyS0` is `crw-rw---- root:uucp`. An ordinary user gets
  `EPERM` (errno 13) there. That is a **host permission**; we leave it alone and do not solve it by
  widening permissions. **MEASURED.**

## 6. Timing

- **Lifetime in our runs:** **5 seconds**, then **exit 0** (the game ends itself cleanly). Without our
  links it was also 5 s; with the links too. **MEASURED.**
- **Start-up times inside those 5 s** (**MEASURED**, in seconds after the jump): `0.026` probe of the
  physics configuration file · `0.026` first child process · `0.031` reading `/dev/urandom` · `0.032`
  probe of the vendor runtime configuration file · `0.036` second child, after which a child every
  ~**11 ms** · `4.04` end burst (syslog, translation CSV files, device nodes) · `4.15` the game ends
  itself.
- **A warning about warm and cold cache.** Our runs read the game from a **network mount** (CIFS); the
  first read of a file can therefore cost tens of milliseconds to seconds, and after that the file is in
  the cache. All the times above are therefore **not** suitable for comparing performance, and a second
  run is measurably faster at the same point. Compare only runs with the same setup and the same point
  in the sequence.
- **What those 5 s are not:** it is not a loading screen that stays up, and it is not the game. See
  sections 7 and 9.

## 7. What we saw on screen

**Nothing.** In our runs no window and no game picture appeared: the loader jumped to `e_entry`, the
game did its thing for 5 seconds (including the child processes described in section 8), and ended
itself with code 0. We also saw **no message from the game itself** in the output; the only lines are
the loader's. **MEASURED** (the output files of both runs end with the line
`jumping to e_entry 0x8057240 (esp …, argc 1, envc …, 17 auxv)` and nothing after it).

## 8. Current state and open questions

**The bar we hold ourselves to:** *met = five minutes of attract mode or game mode*, and a title screen
or loading screen does not count.

**Status: NOT MET.** Our best run comes to **5 seconds** and **exit 0**, without a picture. What does
stand:

- the game **loads and starts** through our own loader (1020 symbols, 13 of 13 libraries,
  `jumping to e_entry 0x8057240`); **MEASURED**;
- we know **exactly which paths** it looks for and **which of them are missing** (section 2);
  **MEASURED**;
- the two absolute roots are **proven** to be the cause of **20 of the 29** error lines, and with a link
  **12** of them disappear without a single byte of content being invented; **MEASURED**;
- the game **writes** to its user folder and that stays **outside the source** (overlay); **MEASURED**;
- the **paced loop** (below) is characterised, and it does not change because of the links;
  **MEASURED**.

### 8.1 A peculiarity a newcomer must know: observe it from inside

**MEASURED:** with a system-call tracer attached to this game, the game does **not** reach the point
where it opens its files at all. With a library that wraps those calls from inside the game's own
process, the same measurement gives **88** lines of file activity (section 2).

The same host rule is why a helper process started **beside** the game cannot read the game's memory: with the kernel's `ptrace_scope` at its default of 1, only an ancestor may do that. Start such a helper from the process that starts the game, or hand it an already opened file descriptor. **MEASURED.**

**The practical rule: observe this title from inside its own process, never with an external tracer.**
The measurements behind that rule are in section 8.2: the game starts about **390** very short-lived
child processes and then ends itself cleanly, and the pattern does not change when the missing files
are supplied. What drives it is **not measured**.

### 8.2 The paced loop — a measured phenomenon without a measured cause

After the start the game forks about **390 children in ~4 seconds**, one per **11 ms**, with these
measurements (identical in three runs, with and without the links):

- intervals between the children: **0.011 s** median (100 % between 5 and 20 ms);
- **median lifetime per child: 0.4 ms**;
- **at most 2 children at the same time**;
- **exactly one handshake per child**; no child was stopped by a signal;
- after that an end burst and a clean **exit 0**.

That is not a worker pool (the children live too short to do work) and not a classic spin (which decays
to zero instead of holding a metronome at 11 ms). **We call it a *paced* loop: one child per tick, each
with one handshake.** That the links from section 2 do **not** change this loop (same 11 ms, same
0.4 ms, same concurrency of 2) is also **MEASURED** — and it means the cause lies somewhere else.
**INFERRED, and not proven:** that the parent "has nothing else to do but tick" after something in the
start failed; the loop keeps running after the missing files are found.

### 8.3 Open questions, each with the next step

1. **The relative `syslog_bb3.txt` in the working directory** (the only error that still occurs 8×). We
   place a **copy of the file from the dump** in the working directory; that is existing content, not an
   invention. The next step is to measure whether that copy removes the last 8 error lines and whether
   the game then gets further. *Note:* the **mode** in which the game opens that relative file has not
   been measured yet — our logger now records the flags (`flags=`) and the `fopen` mode, so that is the
   next measurement.
2. **The device side.** `/dev/dsp` (OSS audio) and `/dev/js0` (old joystick) do not exist on a modern
   Linux; `/dev/ttyS0` gives `EPERM`. Next step: see what the game does when those nodes **are** there
   (synthetic pseudo-terminals in the sandbox) and whether the audio and input paths then run through.
3. **What drives the paced loop.** Next step: measure not only the timing of the children but also
   **what they do** (system calls per child), because the loop is now a phenomenon without a cause.
4. **The two configuration files** (the physics one and the vendor runtime one). Next step: **first
   measure** what is read from them — which is possible now, because the logger reports the flags and
   the mode. We will only supply content once there is a real source; inventing is excluded.

## 9. What we tried and what did not work

The negatives here are worth as much as the positives; each of them cost a measurement.

1. **`--ro-bind / /` in the sandbox.** It looks like the tidy way to give the game a normal system, but
   it makes the root read-only and then **new paths cannot be created**: `bwrap: Can't create file
   /g3: Read-only file system`. **MEASURED.** The fix is to build the root out of separate binds, which
   leaves the root itself writable as a tmpfs.
2. **Binding `/lib`, `/lib64`, `/bin` and `/sbin` as folders.** Those are **symbolic links** to `usr/…`
   on this system. Binding their contents breaks the relative chain of the 32-bit interpreter, and the
   loader then dies with **exit 127** and `No such file or directory` **while the file exists**.
   **MEASURED** — this cost us a run. The fix is `--symlink usr/lib /lib` and so on.
3. **Adding a device node inside a bound `/dev`.** With `--dev-bind /dev /dev` you can **not** create a
   new node (`Permission denied`). **MEASURED.** Use bwrap's own `/dev` (`--dev /dev`) and bind the
   existing nodes separately.
4. **Binding `/dev/ptmx`.** It is a symbolic link there, which produces
   `Can't mount on symlink destination`. **MEASURED.** And if you want to use a pseudo-terminal from
   outside the sandbox you have to bind the **host's** `/dev/pts`, otherwise `/dev/pts` is a private
   instance and your pseudo-terminal is unreachable.
5. **`strace`.** It kills this game before you reach anything interesting (section 8.1). **MEASURED.**
   An in-process logger is the way.
6. **Inventing the content of the two configuration files.** There is no source for it anywhere in the
   dump, the runtime or our own files. Supplying made-up content would make every following measurement
   a measurement of our own invention. **MEASURED** that there is no source; the decision not to supply
   them follows from it.
7. **Reading the guest symbol count from `DT_HASH` only.** This game does not have `DT_HASH`, so the
   count stayed zero and the loader stopped **before** loading anything — which looks like "the loader
   cannot handle this game" but was an instrument problem. **MEASURED.** Deriving the count from
   `(DT_STRTAB − DT_SYMTAB) / 16` gives 1020 and the loader works.
8. **The paced loop as a conclusion.** It is tempting to read the ~390 children as "a protection that
   refuses us". What is measured is a metronome, not a decision: the loop does not change when the
   missing files are supplied, and it does not change with the links. **INFERRED** that the loop is a
   consequence of something else that failed earlier, and that is not proven.

## 10. What is Linux-specific, and what differs on Windows

For someone who wants to rebuild this on another machine or another system: this is what we met on the
**Linux** side. Per point whether it is **MEASURED** or an **assumption**.

- **32-bit.** The game is i386. On a 64-bit distribution you need the 32-bit runtime (`/usr/lib32`,
  `ld-linux.so.2`) and a **32-bit loader**; a 64-bit process cannot run the guest. **MEASURED.**
- **The missing development header.** On SteamOS-like systems `gnu/stubs-32.h` is missing, which makes
  `gcc -m32` fail. We put a minimal version in place and compile with `-I` pointing at that folder
  (`tools/rg-elfloader/inc`). **MEASURED** — without it nothing builds.
- **The sandbox with explicit binds.** `bwrap` with `--ro-bind / /` does **not** work if you want to
  add `/g3` or `/T4User` (read-only file system). Build the root from separate binds. **MEASURED.**
- **The `/lib` and `/bin` symbolic links.** Take them over as `--symlink`, not as a bind of their
  contents, or the relative 32-bit interpreter chain breaks and the loader dies with **exit 127** while
  the file exists. **MEASURED.**
- **`/dev` when you need to add a node.** In a bound `/dev` you cannot create a new node; use bwrap's
  own `/dev` and bind existing nodes separately, and do not bind `/dev/ptmx`. **MEASURED.**
- **Writing without touching the source: overlayfs** (`--overlay-src` plus `--overlay`). On a system
  without overlayfs (or inside a container without it) you need another way: a copy of the folder, or
  redirecting the game with `LD_PRELOAD`. **MEASURED** that the overlay works here; the alternatives
  are **assumptions**.
- **Device permissions.** `/dev/ttyS0` is `660 root:uucp`; an ordinary user gets `EPERM`. On Windows
  that path does not exist and you talk to a completely different driver layer (or through a USB HID
  path). **MEASURED** for Linux; the Windows part is an **assumption**.
- **`yama.ptrace_scope`.** This is purely Linux: on Windows there is no Yama and this pitfall does not
  exist. See section 8.1. **MEASURED.**
- **What someone on Windows would do differently:** there is no `bwrap` and no `ld.so`; a 32-bit
  Windows process cannot load the ELF, so you need a translation layer or your own loader either way.
  The **paths** from section 2 stay the same story (the game still uses `/g3/` and `/T4User/`), so that
  mapping has to be arranged differently there. **INFERRED / assumption** — we have not measured a
  Windows run of this title.
- **A network mount and timing.** Our files come from a CIFS mount; the first read is slow and after
  that it comes from the cache. If you do timing over a network mount, you are measuring the mount.
  **MEASURED.**

---

*This page summarises our own measurements of this one version of the game. In doubt, refer to the files
themselves: the SHA-256 of the game is at the top, and the paths and counts above can all be found again
with a file logger or your own loader.*
