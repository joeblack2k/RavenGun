# Men in Black — what the game is and what it expects

> **STAND 2026-10-14 — lees eerst `ATTRACT-RUN.md`. NIET op de lat.** De kast-keten start wel op onze eigen
> kern: `SystemLauncher` → `Shell.exe` → `MIB.exe`. De shell komt na vier stappen (checksum, IO-bord,
> beveiligingssleutel, integriteit) langs en start de game; de game laadt, toont het MIB-logo met "PLEASE CALL
> ATTENDANT — TICKETS OWED", en stopt daarna in een herstartlus (geen vijf minuten). Open punt: de game stuurt
> een `GAME_MSG_SECURITY_CHALLENGE` die de shell zonder dongle niet beantwoordt, en "NOT Connected to serial";
> welke van de twee de game laat stoppen is niet vastgesteld. Zie `ATTRACT-RUN.md` §1.

Read `ATTRACT-RUN.md` first for where the title stands. This file is the static description. Everything
here was read from the files on the share or from a run on 2026-10-14; nothing existed about this title
before. **[M]** = measured, **[D]** = derived.

**The dump:** `Men in Black/` on the share, 6.7 GB. On the cabinet it is the content of `C:\Sega\`.

| folder | what | |
|---|---|---|
| `Launcher/` | `SystemLauncher.exe`, PE32 i386, 88,576 B; `Launcher.ini` (`version=0.1.0`, `ShellDirectory=c:\sega\shell`, 1920×1080) | the cabinet's entry point: replaces the Windows shell, looks for updates, starts the Game Shell [M, strings] |
| `shell/` | `Shell.exe`, PE32 i386, 1,434,624 B, with `Shell.map`; "Game Shell" **230307RED** (`install.ini`: Manufacturer "Hollywood Gaming") | credits, test menu, tickets, I/O board, lights, recoil; starts the game |
| `MIB/` | `MIB.exe`, PE32+ x86-64, 653,824 B + `UnityPlayer.dll`; Unity **2020.3.14f1**; `install.ini`: Manufacturer "SEGA Amusements", `Type=Ticket`, `Product=MIB`, `Directory=C:\Sega\MIB`, `Version=240913RED` | the game (6.6 GB) |
| `ShellData/` | `ShellData.ini` (credits: `Freeplay=0`, `GameCost=1`; gun calibration ranges), `GameSettings.ini` (difficulty, tickets, "SWIPE CARD TO PLAY=ON") | settings shared by shell and game |
| `GameData/`, `backup/` | empty | |

It is the same shell family as Transformers: Shadows Rising (`patches/TransformersShadowsRising/`), one
generation newer: that title has Game Shell 180605. This shell still carries Transformers stage and
character names (`shell/GameInfo.ini`) and the path `c:\Sega\Transformers\install.ini`.

## What it expects from the cabinet

* **Fixed paths under `C:\Sega\`** in both programs [M, strings]: the shell reads
  `c:\Sega\Shell\game.ini`, `c:\sega\launcher\launcher.ini`, `c:\Sega\ShellData`, `c:\Sega\GameData`;
  the game reads `C:/Sega/ShellData/ShellData.ini`, `C:/Sega/GameData/`.
* **An I/O board** — `shell/Game.ini`: `[CabinetSKU] IOBoardMkII=1`; the shell's log:
  "IOMkII: IOBOARD_INITIALISE", "InitIOBoard : Couldn't find IOBoard device" [M, run m1].
* **A security key** — `shell/dk2win32.dll` / `dk2win64.dll` (DK2 dongle); the shell's log: "Unable to
  find security key." [M, run m1]. The Transformers shell logs the same and launches its game anyway.
* **Serial devices** — `shell/Game.ini`: `SerialLight=1`, `SerialLEDPort=3`. **The game itself opens the
  first serial port that answers**: Unity's log in run m2 says "CONNECTING TO SERIAL" / "SUCCESS!
  Connected to COM33" [M]. On the bench machine COM33 was the owner's Steam Controller Puck
  (`/dev/ttyACM0`). The device has to be hidden from the game.
* **Tickets and cards** — a ticket-redemption build: ticket vend modules in the shell, "ETICKETS",
  "SWIPE CARD TO PLAY" in the settings.
* **Shell and game talk over sockets** [M, strings on both sides: the shell has "Unable to create
  listening socket" and `SHELL_MSG_SET_SKU`; the game has bind/listen/accept/connect messages].
* Also in the shell folder: `hidapi.dll`, `inpout32.dll` / `inpoutx64.dll` (parallel port), `Elo*.dll`
  (touch screen), `recoil*.xml`, the test-menu `test*.xml` files.

## For comparison: TeknoParrot

Profile `GameProfiles/MIB.xml` (revision 5, subscriber-only): starts `MIB.exe` **directly**, without the
shell, on its closed 64-bit core, emulation profile `WartranTroopers`; buttons Test, Service, two coins,
start and trigger and a gun per player. So there the core stands in for the shell.
