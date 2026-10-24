# Running on Linux

All of RavenGun is developed and measured on Linux: a Steam Deck running SteamOS (Arch-based, KDE Plasma on Wayland in Desktop Mode), connected to a 1920x1080 TV over HDMI. The games were made for Windows-based and Linux-based cabinets. This page collects what is specific to getting them to run on a Linux PC, and the pitfalls that cost us time. Tags are those from the [Home](Home) page.

## The two kinds of game

| The cabinet runs | On Linux we use | What you need |
|---|---|---|
| Windows | Wine | A plain Wine build, one prefix, the loader and core |
| Linux (often an old 32-bit distribution) | The game binary itself | A sandbox that shows the game its expected paths, the old libraries it links against, and an input helper |

"Linux game" does not mean "runs on a current Linux". These binaries expect fixed absolute paths (`/pm/...`), 32-bit libraries that distributions no longer ship, and a kernel and C library from their own era.

## Wine

**Use a plain Wine build, not a wrapper script.** Our launcher starts `wine` directly. Launch wrappers that expect to be started from a game store add their own environment and process tree; the launcher refuses them.

**One prefix, and always name it.** Every Wine command we run sets `WINEPREFIX` explicitly. `wineserver -k` without a prefix acts on the default prefix, which is not the one your game runs in. One of our tools made exactly that mistake.

**Environment variables pass through unchanged.** **MEASURED** (2026-10-13): variables in the Unix environment arrive in the Windows process and its children with the same name and value.

**Arguments survive, if you pass a list.** Start Wine with an argument list (no shell in between). Wine builds the Windows command line with the standard quoting rules, and the program on the other side splits it back. **MEASURED** (2026-10-13): a parameter containing quotes, spaces and a backslash before a quote arrived byte for byte.

**Drive letters are symbolic links.** A drive `X:` in a prefix is the link `<prefix>/dosdevices/x:` pointing at a folder. **MEASURED** (2026-10-13 11:16, a test prefix):

| Question | Result |
|---|---|
| Does a program start from `X:\exe\x64\game.exe` after creating the link? | Yes |
| What does a script on X: see as its own path? | `X:\exe\x64\...` |
| Does a child process started with working directory `X:\exe\x64` see it? | Yes |
| Does it work if a Wine server for the prefix was already running when the link was added? | Yes, immediately |
| After removing the link, is X: gone for Wine? | Yes, immediately |

Our launcher creates such a link for the duration of one run and removes it afterwards, also if the run fails, and keeps a record of the links it made so that a run that was killed does not leave one behind. It refuses a letter that already exists and points elsewhere. A drive letter left behind in a shared prefix changes the setup of every later run.

**The working directory is not what you might expect.** A program started directly by `wine` gets the Unix working directory, seen as `Z:\...`. A game started through the OpenParrot loader gets the folder of the path the loader was given (**READ**, loader source). So starting the same file as `Z:\...\game.exe` or as `X:\exe\x64\game.exe` gives the game a different working directory string, even though it is the same folder.

**`LD_PRELOAD` is not safe under Wine.** Wine passes its environment to everything it starts. One of the loaders we start under Wine is a Cygwin program, and Cygwin exits on `LD_PRELOAD` (**MEASURED**, 2026-10-11). Use the channel that fits the route; see the table under "Drop-ins" in [Loader architecture](Loader-Architecture).

**Serial ports are links too.** Wine re-creates `dosdevices/com1` to `com32` at every cold start of its server. A link you made yourself is overwritten unless the server is already running.

## Native Linux games

**A sandbox instead of a chroot.** The native start script builds a `bubblewrap` sandbox: `/usr` and `/etc` from the host read-only, the game's folder bound read-only at the path the game expects, and a local overlay for everything the game writes. Nothing is copied and nothing is written into the game's own folder. The network namespace is unshared.

**Shared-memory graphics need the host IPC namespace.** A game that uses the X shared-memory extension crashes in an isolated IPC namespace (**MEASURED** with one title). The sandbox isolates IPC by default and has a switch per game to share it.

**The game does not read your input devices.** See "Native games: the input helper" in [Loader architecture](Loader-Architecture). This is the finding that mattered most on the native route.

**Old 32-bit binaries may need their own loader.** Some of these binaries carry only a GNU hash table and rely on libraries being present in the global scope in a particular order. Our ELF loader handles both.

- **MEASURED** (Terminator Salvation, 2026-10-12): 1020 guest symbols counted from the dynamic section when the classic hash table is absent, 13 libraries loaded, 2 of them only on a second attempt because a library with no dependency entries of its own needed others loaded first.
- A lesson from the same work: **a dry-run mode must take the same path as the real run.** Ours retried failed library loads only in the analysis mode, so the dry run was green and the real run died at the same spot.

## Shared memory between the engine and a game

The engine publishes the board image as a file in `/dev/shm`. Two things we learned the hard way:

- **Read it with `pread`, do not `mmap` it.** A writer that truncates the file even for an instant (opening it with truncation does that) invalidates a mapping, and the reader then dies with `SIGBUS` on its next access. That reader was a library inside the game, so the game died. **MEASURED** (Halo, run 4, 2026-10-13).
- **Write in place.** Never recreate the file to update it.

## Displays

Measurement runs can be headless or on the real screen, and the two are **not comparable** for timing or screenshots.

| | Headless | On the TV |
|---|---|---|
| How | `gamescope --backend headless`, or no display in the environment | `gamescope -f`, or a window on the desktop session |
| Frame pacing | not tied to a display | tied to the display (ours: 59.96 Hz) |
| Sound | none | yes |
| Input from the desk | does not reach the game | reaches the game; `gamescope` grabs the pointer |

**A run started from a service or an SSH session has no display.** It needs the environment of the desktop session: `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, the session bus address, and `XAUTHORITY`. Without `XAUTHORITY` the X server refuses the connection ("Authorization required, but no authorization protocol specified", **MEASURED** 2026-10-13). `XAUTHORITY` changes with every desktop session, so read it from a running session process each time. Our helper `scripts/rg-tv-env.sh` does that and refuses to start the command if the display is not reachable, instead of starting an invisible run.

## Virtual input devices

The engine creates its devices through `uinput`: an absolute-pointer lightgun per player and a keyboard. Two practical points:

- The user needs write access to `/dev/uinput`; we install a narrow `udev` rule for it.
- **A game that opens input devices does so at start.** The virtual device must exist before the game starts (**MEASURED** on the Wine ELF-loader route, 2026-10-07: the loader held the event device open only if it existed at launch).

## Game files on network storage

Our games stay on a network share and are started from there through local **views** (folders of symbolic links; see [Loader architecture](Loader-Architecture)). What that means on Linux:

- **Resolve links before you write.** A path inside the view can resolve to the share through a link in a parent folder.
- **Expect slow first reads.** A file-integrity pass that a game runs at start took about two minutes from a local disk and an estimated 25 minutes from the share (**MEASURED**, 4 files per second, 5941 files).
- **Cold and warm cache differ.** The same start measured twice gave different times purely from the file cache (see lesson 4 in [Loader architecture](Loader-Architecture)).

## Measuring a running game

- **Only an ancestor may read another process's memory** when the kernel's `ptrace_scope` is 1, which is the default on many distributions. A helper started beside the game gets "permission denied"; the process that started the game does not. Open the file descriptor in the parent and hand it to the helper.
- **Instrumentation changes what you measure.** A full file trace made one game live four to five times longer than without it (**MEASURED**, see [Luigi's Mansion](Luigis-Mansion)). Compare runs only with the same instrumentation.
- **Wine keeps servers alive.** After a test, stop the server of the prefix you used, by name, and check that it is gone before the next measurement. A leftover server makes the next party's "is anything running?" check fail.

## Stale copies

On Linux it is easy to have the same program in two places: the source checkout and an install folder under the home directory. A fix in one is not a fix in the other.

- A build script that produces a binary must also install it, **and fail if the installed file differs from the built one**.
- Every run log starts with the path and SHA-256 of the script that ran (see "What every log records" in [Loader architecture](Loader-Architecture)).
- Update an installation as a whole. Copying one script into it breaks it when that script has gained a helper file.

## One measurement at a time

Two games at once share the engine, the shared-memory file and often the Wine prefix. A second start during someone else's run invalidates both runs. Before starting, check for a running game, a compositor of ours, and a Wine server, by process name rather than by a pattern on command lines (a pattern matches the shell that runs the check).

See also: [Loader architecture](Loader-Architecture), [Official start scripts](Official-Start-Scripts).
