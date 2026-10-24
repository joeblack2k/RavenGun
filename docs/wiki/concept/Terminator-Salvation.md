# Terminator Salvation

*Raw Thrills, 2010. Linux i386, two players. Route described here: native, in a sandbox, with our own ELF loader. Status on this route: the five-minute bar is **not met** — the game loads, starts, runs for about 5 seconds without a picture and ends by itself.*

Everything on this page was measured on a Steam Deck (SteamOS), with the game files on network storage that is only read. Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

- **The cabinet**: a lightgun cabinet with two guns, on a PC running **32-bit i386 Linux**. **MEASURED** from the game itself (it asks for two aim positions, two start buttons, two triggers) and from the files.
- **The game file**: `game`, 7,764,284 bytes, a 32-bit program with a fixed load address (`0x08048000`) and entry point `0x08057240`, linked against 13 shared libraries: `libGL`, `libGLU`, `libglut`, `libpthread`, `librt`, `libtheora`, `libogg`, `libPhysXLoader`, `libm`, `libgcc_s`, `libc`, `libdl`, `libstdc++`. SHA-256 `798dec07a71c5c37703242edab6dce6d5a7e018b8720a85986842e3ef99b206a`. **MEASURED**.
- **All imports have names** (**MEASURED**): 29 section headers, 496 import stubs, 1020 dynamic symbols, none erased. Everything the game imports can be resolved by name.
- **Graphics**: OpenGL through GLUT (15 `glut*` functions imported) and PhysX 2.8. The game's library search path is `/usr/lib/PhysX/v2.8.1/`. **MEASURED**.
- **Devices it opens at start** (**MEASURED**): `/dev/sierra2` and `/dev/ttyS0` (read-write, non-blocking), `/dev/js0` (read-only) and `/dev/dsp` (write-only), and it lists USB devices under `/dev/bus/usb`. That `/dev/sierra2` is the transport to the gun board is **DERIVED** from the name, the serial port beside it and the two guns; we have not measured what the game does with it when it exists.
- A practical trap: the folder name on our share **ends in a space**. Some tools then report "directory name invalid".

## 2. What the game needs

The following comes from a **file measurement**: the game ran for 5 seconds with a preloaded logger that records every file open, directory open and file-status call the game makes (88 lines of real activity). **MEASURED**:

| What the game asks for | How often | Is it in our copy? |
|---|---|---|
| `/g3/T4Data/etranslation/english.csv` | 2× | Yes, as `T4Data/etranslation/english.csv` (5,168 bytes) |
| `/g3/T4Data/etranslation_app/english.csv` | 2× | Yes, as `T4Data/etranslation_app/english.csv` (5,880 bytes) |
| `/T4User/log/syslog_bb3.txt` (absolute) | 8× | Yes, as `T4User/log/syslog_bb3.txt` (12,019 bytes) |
| `syslog_bb3.txt` (relative to the working directory) | 8× | No — it is only in `T4User/log/` |
| `PhysX.conf` in the home folder and in `/etc` | 1× each | No, nowhere |
| `/dev/js0`, `/dev/dsp`, `/dev/sierra2` | 1× each | No |
| `/dev/ttyS0` | 1× | Exists, but permission denied (`660 root:uucp`) |

**In one sentence: the game uses two ABSOLUTE roots, `/g3/` (its data) and `/T4User/` (its user data), which do not exist on an ordinary machine — while the files it looks for there are in the copy, under a relative path.** **MEASURED**.

**What turns out to be optional.** The game does not stop at what it cannot find: after probing `PhysX.conf` it lives on for 4.1 seconds. **MEASURED** that the file is absent and how it is accessed (opened for reading); **DERIVED** that its absence is not immediately fatal.

**A principle we keep**: we do **not** supply configuration files that are not in the copy. There is no source for their content. Inventing content would make the next measurement worthless, because you would be measuring your own invention.

## 3. The official start script

We found **no** start script in this game's folder. There is a state-machine description (`GeneralPurposeStateMachine.txt`, 5,782 bytes) and the folders `T4Data`, `T4User`, `env` and `irgun`.

The general rule applies to anything you do find: do not execute a cabinet's start or installation script to see what it does. See [Official start scripts](Official-Start-Scripts).

## 4. The route we use

**The loader.** Our own 32-bit ELF loader (`rg-elfloader`) maps the game's loadable segments at their own addresses, applies the relocations, builds a shim that exports the game's symbols to the libraries, and jumps to the entry point. For this game that works (**MEASURED**): `jump to e_entry 0x8057240`, and after that the game does things by itself.

> **For anyone rebuilding this**: our loader first took the number of the game's symbols only from the classic hash table. This game has **no** classic hash table, only the GNU one; the count then stayed zero and the loader stopped before anything else. The fix is to derive the count from the distance between the string table and the symbol table, divided by 16 — here exactly **1020** — or to parse the GNU hash table itself. **MEASURED**.

**The sandbox.** The game runs in a `bubblewrap` sandbox built from **explicit binds**. That is a necessity, not a style choice (all **MEASURED**; each of these happened to us):

- **Binding the whole root read-only does not work** if you want to add new paths: the sandbox tool cannot create `/g3` on a read-only root. Build the root from separate binds (`/usr`, `/etc`, ...); the root itself is then a temporary file system and new paths can be created.
- **`/lib`, `/lib64`, `/bin` and `/sbin` are symbolic links** to folders under `/usr`. Recreate them as links, do not bind their contents. Otherwise the relative chain of the **32-bit** program interpreter breaks, and the loader dies with exit code 127 and "No such file or directory" while the file is there.
- **Folders the game writes to: use an overlay.** The source stays untouched and all writes land in a local upper layer. The game wrote 12,481 bytes into the upper layer while the source file stayed at 12,019 bytes with its old timestamp.

The shape of the command (shortened; `<GAME>` is the folder on the share, `<W>` a work folder):

```sh
bwrap --die-with-parent \
  --ro-bind /usr /usr \
  --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin \
  --ro-bind /etc /etc --proc /proc --dev /dev \
  --dev-bind-try /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd \
  --bind /tmp /tmp \
  --dir /g3 --ro-bind "<GAME>" /g3 \
  --overlay-src "<GAME>/T4User" --overlay <W>/upper <W>/work /T4User \
  --setenv LD_LIBRARY_PATH "<32-bit libraries>" \
  --chdir "<W>/view" \
  rg-elfloader "<W>/view/game"
```

## 5. Input

- **MEASURED**: the devices the game opens are listed in section 1.
- **Our board layout for this family** (**READ**, our own data file): a 64-byte image with the buttons in a field at offset 8 (test, service, two coins, per player start, trigger, grenade, reload, volume) and **one byte per axis at offsets 12, 16, 20 and 24** for player 1 x and y, player 2 x and y; 0 is left and top.
- **MEASURED** (2026-10-09, on a **different route**: the same game started through a Windows-side ELF loader under Wine): with that layout and real controllers the crosshair followed a grid to within about 1 %, and start, trigger and reload worked. That was measured with another loader on the game's side, not with the loader described on this page.
- **Our approach for the device nodes**: we do not supply a real serial port and we do not widen permissions. We create a **synthetic node** (a pseudo-terminal pair bound as `/dev/sierra2` in the sandbox) that our bridge can drive. **MEASURED**: the game then opens the node without an error. **DERIVED**: that this is the right transport.

## 6. Timing

- **Lifetime in our runs**: **5 seconds**, then exit code 0: the game ends by itself. The same with and without the path links from section 9. **MEASURED**.
- **Within those 5 s** (**MEASURED**, seconds after the jump): at 0.026 the game probes `PhysX.conf` and starts its first child process; from 0.036 it starts a child about every 11 ms; at 4.04 a final burst (log file, translation tables, device nodes); at 4.15 it ends.
- **Cold and warm cache.** Our runs read the game from a network mount. The first read of a file can cost tens of milliseconds to seconds; after that it is cached. These timings are not suitable for comparing performance, and a second run is measurably faster at the same spot.

**A measured phenomenon without a measured cause**: after the start the game creates about **390 short-lived child processes in about 4 seconds**, one per 11 ms (median interval 0.011 s, median lifetime per child 0.4 ms, at most 2 at a time), identical in three runs, and then ends cleanly. It is not a worker pool (the children live too briefly to do work). The path links from section 9 do not change it. What drives it is **NOT MEASURED**.

## 7. What we saw on screen

**Nothing.** No window and no picture appeared in our runs. The loader's last line is the jump to the entry point; the game printed no message of its own. **MEASURED**.

## 8. Linux notes

All **MEASURED** unless marked.

- **32-bit.** On a 64-bit distribution you need the 32-bit run-time libraries and a **32-bit loader**; a 64-bit process cannot host this game.
- **A missing development header.** On SteamOS-like systems `gnu/stubs-32.h` is absent, so compiling 32-bit code fails. We ship a minimal version and add its folder to the include path.
- **The sandbox details from section 4**: separate binds instead of the whole root; the `/lib` and `/bin` links recreated as links.
- **Adding a device node.** You cannot create a new node in a `/dev` bound from the host ("permission denied"). Use the sandbox tool's own `/dev` and bind the existing nodes you need separately. Do not bind `/dev/ptmx`; it is a link there. Bind the **host's** `/dev/pts` if you want to use a pseudo-terminal created outside the sandbox.
- **Writing without touching the source**: an overlay (section 4). Where overlays are not available, a copy of the folder would be needed; that alternative is an **assumption**.
- **Device permissions.** `/dev/ttyS0` is `660 root:uucp`; a normal user gets "permission denied". We leave that as it is.
- **Observe from inside, not with a tracer.** We recorded the game's file access with a preloaded logger library. With a system-call tracer attached the game did not reach the point where it opens its files.
- **Network mount and timing.** Timing over a network mount measures the mount.
- On Windows there is no such sandbox tool and no Linux program loader; you would need a translation layer or your own loader in any case, and the two absolute roots from section 2 would have to be provided another way. That is an **assumption**; we measured no Windows run.

## 9. Status

**The bar**: five minutes of attract or game mode; a start or loading screen does not count. **Not met on this route.** The best run reaches 5 seconds and exit code 0, without a picture.

What does stand (**MEASURED**):

- The game **loads and starts** through our own loader: 1020 symbols, 13 of 13 libraries, the jump to the entry point.
- We know **exactly which paths** it looks for and which are missing (section 2).
- The two absolute roots account for **20 of the 29** failed lookups, and with links to the existing files **12** of them go away without one byte of invented content.
- The game **writes** to its user folder and that stays **outside the source** (overlay).
- The child-process pattern from section 6 is characterised, and it does not change with the links.

On another route (through a Windows-side ELF loader under Wine) this title was playable in earlier measurements; see section 5.

## 10. Open questions

1. **The relative `syslog_bb3.txt` in the working directory**, the one failed lookup that still occurs 8 times. Next step: put a copy of the existing file there and measure whether the game gets further. In which mode the game opens it is not measured yet.
2. **The device side.** `/dev/dsp` (old audio interface) and `/dev/js0` (old joystick interface) do not exist on a current Linux; `/dev/ttyS0` gives "permission denied". Next step: see what the game does when those nodes **do** exist as synthetic nodes in the sandbox.
3. **What drives the child-process pattern.** Next step: measure what the children do, not only when.
4. **The configuration file `PhysX.conf`.** Next step: measure what is read from it. Supplying content only if a real source turns up.

---

*Source: the game specialist's digest (2026-10-13). Local paths have been shortened. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
