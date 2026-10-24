# RavenGun wiki

RavenGun makes arcade lightgun games playable on a Linux PC (developed on a Steam Deck running SteamOS, connected to a TV) with PlayStation Move controllers as the guns. It consists of an engine (`ravengund`) that reads the controllers and turns them into game input, a launcher that starts each game the way that game needs, and a Qt app on top.

This wiki records **what we found out about each game and about starting arcade games on Linux**, so that someone who later builds their own front-end, loader or emulator does not have to rediscover it. It is written for a reader who does not know the project.

## How to read this wiki

Every claim says where it comes from:

| Tag | Meaning |
|---|---|
| **MEASURED** | We ran it and saw it. The date, and where it matters the time and the numbers, are given. |
| **READ** | Taken from a file we could open: a game's own start script, a configuration file, or open-source code. |
| **DERIVED** | Reasoned from other facts. Not run. Treat it as a hypothesis until someone measures it. |
| **NOT MEASURED** | We know we do not know. These are listed on purpose; they are the open work. |

Two rules we hold ourselves to, and that you should hold us to:

- **"The game is running" is not "the game works."** A game counts as passed only after **five minutes of attract mode or game mode**. A start screen does not count; on these cabinets the first screen is often the gun calibration.
- **A measurement names its setup.** Which launcher file ran (path and SHA-256), which route, with or without a picture on the screen. A result without that is not comparable with the next one.

## Pages

**General**

- [Loader architecture](Loader-Architecture) — the routes we use to start a game, the launcher and its plan, input bridges, drop-ins, and what the logs record.
- [Official start scripts](Official-Start-Scripts) — what a cabinet's own `game.bat` or `run_game.sh` does, why we never execute it, and how we take over only the parts a game needs.
- [Running on Linux](Running-on-Linux) — what is specific to Linux: Wine prefixes and drive letters, the sandbox for native games, displays, shared memory, and the pitfalls we hit.

**Per game**

Each game page has the same sections: platform and cabinet, what the game needs, the official start script, the route we use, input, timing, what we saw on screen, status, open questions.

| Game | Platform | Route we use | Page |
|---|---|---|---|
| Luigi's Mansion Arcade | Windows x64 | Wine + OpenParrot loader + core | [Luigi's Mansion](Luigis-Mansion) |
| Rabbids Hollywood | Windows, Unity | Wine + game plug-in + TCP input | [Rabbids Hollywood](Rabbids-Hollywood) |
| Halo: Fireteam Raven | Linux x86-64 | Native, in a sandbox, with an input helper | [Halo: Fireteam Raven](Halo-Fireteam-Raven) |
| Terminator Salvation | Linux i386 | Native, with our own ELF loader | [Terminator Salvation](Terminator-Salvation) |
| Transformers: Shadows Rising | Windows x86 | Wine + OpenParrot loader + core, with a virtual gun board | [Transformers: Shadows Rising](Transformers-Shadows-Rising) |
| House of the Dead: Scarlet Dawn | Windows x64, Unreal Engine 4 | Wine + OpenParrot loader, two programs | [House of the Dead: Scarlet Dawn](House-of-the-Dead-Scarlet-Dawn) |

The status of each game is on its own page, with the measurement behind it.

## What this wiki does not cover

This wiki is about the **game** and the **loader**: what a front-end or emulator must know to start these titles and feed them input. It deliberately leaves out everything about the hardware security and licensing layers of the cabinets. You need the original game files and whatever the cabinet needs to run them; nothing here replaces that.

## Terms used throughout

| Term | Meaning |
|---|---|
| **Route** | One complete way to start a game and deliver input to it. A game has one recommended route. |
| **Loader** | The small program that creates the game process and puts a helper library ("core") inside it before the game's own code runs. |
| **Core** | The library inside the game process that answers the game's cabinet I/O calls. |
| **View** | A local folder that mirrors a game's folder with symbolic links. The game files themselves stay where they are (in our case on network storage, read-only in practice); only files we add are real files in the view. |
| **Drop-in** | A file of ours that the launcher places in a game's view before the start, declared per game. |
| **Board image** | A 64-byte block of shared memory in which the engine publishes the state of the cabinet's I/O board: switches, gun buttons, aim. |
| **Plan** | What the launcher prints with `--plan`: exactly what it would start, without starting anything. |
