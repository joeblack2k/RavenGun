# RavenGun wiki

This wiki covers arcade lightgun games and the loader that starts them. It is written for someone who
does not know this project and who wants to build their own front-end, loader or emulator on top of
what we measured: what each title needs, how we start it, how its cabinet input reaches it, and where
it stops.

RavenGun itself is the program that makes these titles playable on a Linux PC (developed on a Steam
Deck running SteamOS, connected to a TV) with **PlayStation Move** controllers as the guns. It has
three parts: an engine that reads the controllers and publishes the cabinet's input state, a launcher
that starts a game the way that game needs, and a Qt app on top of both. This wiki is about the games
and about that launcher.

## How to read this wiki

Every claim carries a label. The label is the point: it tells you how much a statement is worth
before you build on it.

| Label | Meaning |
|---|---|
| **MEASURED** | We ran it and observed it. The numbers, timestamps, paths, indices or offsets that came out of the measurement are given with it. |
| **INFERRED** | A conclusion drawn from measurements. Useful, but not itself observed. |
| **assumption** | A guess about something we did not test at all. Treat it as a hypothesis. |

Where something is simply unknown, the page says so in words ("not measured") instead of dressing it
up.

Three project rules apply to everything below, and they are worth holding us to:

- **"The game is running" is not "the game works."** A title counts as passed only when **five minutes
  of attract mode or game mode** run. A title screen does not count, and on these cabinets the first
  screen is usually the gun calibration.
- **A measurement names its setup.** Which launcher file ran (with path and SHA-256), which route, and
  whether there was a picture on a screen. Without that, two runs are not comparable.
- **A clock reading without "cold" or "warm" says nothing.** Disk cache moved the same moment in the
  same start from 1 s to 11 s on one title, and from 4.8 s to 6.0 s on another. Both are on the pages
  that follow.

## The pages

**General**

- [Loader architecture](Loader-Architecture) — the routes we use to start a game, the loader and its
  helper, the I/O bridge, drop-ins, per-game profiles, the start plan (`--plan`), and the identity
  rule that makes every log say which tool performed the run.
- [Official start scripts](Official-Start-Scripts) — what a cabinet's own `game.bat` or `run_game.sh`
  does, why we never execute one, and which parts we take over instead.
- [Running on Linux](Running-on-Linux) — what is specific to Linux: Wine prefixes and drive
  letters, the sandbox for native games, displays, shared memory, virtual devices and the
  permissions they need.

**Per game**

Each game page has the same ten sections: platform and cabinet, what the game needs, the official
start script, the route we use, input, timing, what we saw on screen, current state and open
questions, what we tried and what did not work, and what is Linux-specific.

| Game | Platform | Route we use | Five-minute bar |
|---|---|---|---|
| [Luigi's Mansion Arcade](Luigis-Mansion) | Sega Nu 1.1, Windows x86-64 | Wine + OpenParrot loader + our core | **Not met**: the game ends about 5–7 s after the start |
| [Rabbids Hollywood Arcade](Rabbids-Hollywood) | Adrenaline Amusements PC-based, Unity | Proton + a plug-in inside the game, input over TCP | **Met**: 608 s (10 min 8 s), with a real level played |
| [Halo: Fireteam Raven](Halo-Fireteam-Raven) | Raw Thrills 2018, Linux x86-64 ELF | Native (kernel `ld.so`) in a sandbox | **Met**: 6.0 min attract in one run, 9.5 min over two runs |
| [Terminator Salvation](Terminator-Salvation) | Raw Thrills 2010, Linux i386 ELF | Our own 32-bit ELF loader in a sandbox | **Not met**: 5 s, then exit 0 |
| [Transformers: Shadows Rising](Transformers-Shadows-Rising) | Sega RingEdge 2, Windows i386 | Wine + OpenParrot loader + our core, with a virtual gun board | **Met**: attract visible for about 8.5 min |
| [House of the Dead: Scarlet Dawn](House-of-the-Dead-Scarlet-Dawn) | Sega ALLS UX, Unreal Engine 4.18, Windows x86-64 | Wine + OpenParrot loader, two programs | **Not met** on our own route; the game has reached its first level on this machine |

Three of the six meet the bar: Rabbids Hollywood, Halo: Fireteam Raven and Transformers: Shadows
Rising. The table on each page gives the measurement behind the verdict.

## Terms used on every page

| Term | Meaning |
|---|---|
| **Route** | One complete way of starting a game and delivering input to it. A title has one route we use. |
| **Loader** | The small program that creates the game process and puts a library (the "core") inside it before the game's own code runs. |
| **Core** | The library inside the game process that answers the cabinet I/O calls the game makes. |
| **Guest** | The game itself, seen from the loader's side: the program that gets loaded or started. |
| **View** | A local folder that mirrors a game's folder using symbolic links. The game files stay where they are; only files we add are real files in the view. |
| **Drop-in** | One of our files that the launcher places in a game's view before the start, declared per game. |
| **Board image** | The block of shared memory in which the engine publishes the cabinet's input state: switches, gun buttons, aim. The size and the field positions differ per game family. |
| **Plan** | What the launcher prints with `--plan`: exactly what it would start, without starting anything. |

## Where the numbers come from

Everything in this wiki was measured on one machine: a **Steam Deck** running SteamOS, with plain
Wine, one Proton build for the Unity title, and the game files on network storage that is read but not
written. Each page says when its measurements were taken and which part of the setup was in play.

---

*This wiki is about the games and the loader: what you need in order to run these titles and to
understand where they stop. Game files and the cabinet's own runtime components are yours to supply.*
