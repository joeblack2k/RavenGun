# RavenGun in one folder (portable mode)

RavenGun can live in a single folder, like Dolphin in portable mode: the binaries,
the data map, the game profiles, the drop-ins and our own loader/core all sit in
that folder and are found relative to the binary. Per-user material (settings,
logs, views, HASP answers, wine prefixes) goes into a `User/` subfolder inside the
same folder. Nothing is written outside that folder, except the game installations
themselves.

## Layout

```
RavenGun/
  bin/                      ravengun, ravengunctl, ravengund, rg-direct, rg-exec, rg-elfloader
  lib/                      shared libraries (Qt, lib32)
  share/ravengun/
    patches/<Game>/         profiles, game.ini, native.json, drop-in DLLs/so's, dongle data
    packaging/              JVS board layouts, tp-shm profiles and shared input layouts
    plugins/unity/          the Unity/Proton drop-in
    docs/                   user guide
  User/                     everything the user makes (do not put this in a fresh copy)
    config/                 XDG_CONFIG_HOME: config.json, ravengun.json
    state/                  XDG_STATE_HOME: logs/, recordings/
    cache/                  XDG_CACHE_HOME
    share/ravengun/         XDG_DATA_HOME: wine/, proton/, prefixes/, openparrot/, runtime/, patches/ (user layer)
  ravengun.sh               starter: sets every path to this folder
```

The starter exports `RAVENGUN_ROOT`, `RAVENGUN_DATA_DIR`, `RAVENGUN_PATCHES_DIR`,
`RAVENGUN_USER_PATCHES_DIR` and the four `XDG_*` variables, all pointing into the
folder, and then starts a program:

```sh
./ravengun.sh app        # the Qt control app
./ravengun.sh daemon     # the daemon (instead of the systemd user service)
./ravengun.sh ctl game start SDR
./ravengun.sh direct --plan SDR
```

## Building the folder

```sh
scripts/build-app.sh           # build and install into ~/.local/opt/ravengun
scripts/make-portable.sh       # assemble dist/RavenGun from that install plus this repo
dist/RavenGun/ravengun.sh app  # start from the folder
```

`make-portable.sh` copies `bin/`, `lib/` and `share/ravengun/` from the install,
copies `patches/` from the repository (so a fresh checkout has the newest profiles),
copies the settings from `~/.config/ravengun` into `User/config/ravengun` and copies
the whole runtime and game state (`wine`, `proton`, `prefixes`, `openparrot`,
`runtime`, `native`, `writelayer`, `patches`, ...) from `~/.local/share/ravengun`
into `User/share/ravengun/`. Everything is a real copy: after that the folder needs
nothing from `~/.local` anymore. Only `bench-captures` (the proof screenshots) and
`halo-overlay` (a root-owned overlay work dir) are skipped, and a directory that
cannot be read is reported and left out instead of aborting the copy. The game
installations themselves are never touched.

Measured (2026-10-18): `dist/RavenGun` is about 18 GB (almost all of it Wine, the
prefixes and the OpenParrot build), the daemon and `ravengunctl` run straight from
the folder and SDR reached the attract from it for more than a minute.

## What the user adds themselves

Two things cannot travel in the Git repository, because they are far above
GitHub's file limits or are licensed material:

1. **The game installations.** They stay where they are (the NAS, an external
   disk, a game folder). Point RavenGun at them with the game folders in the
   app (`Settings`, or `User/config/config.json`). The profiles in
   `share/ravengun/patches/<Game>/` describe how each game is started;
   `native.json` and `game.ini` contain the folder names relative to the roots
   that the app knows.
2. **The Wine/Proton runtime** (`User/share/ravengun/wine` and `proton`) and the
   **OpenParrot build** (`User/share/ravengun/openparrot`). A fresh copy starts
   with an empty `User/`; the setup in the app can download the pinned Proton
   build, and `scripts/build-openparrot.sh` builds our loader and core locally
   (that build is ours, not TeknoParrot's). The prefixen under
   `User/share/ravengun/prefixes` are created on the first start.

Everything else a game needs to run - the profiles, the drop-ins (`rg-amhw.dll`,
`rg-mfcam.dll`, `ravengun-hasp`, the ELF hooks), the dongle data and the HASP
answer tables - is in `share/ravengun/patches/<Game>/` in the repository, so a
fresh clone can start the games without extra steps. The repository is private,
so that data may live in Git.

## Checking that nothing leaks

```sh
strace -f -e trace=openat -o /tmp/rg.strace dist/RavenGun/ravengun.sh ctl game start SDR
grep -v "$PWD/dist/RavenGun" /tmp/rg.strace | grep -v '^[0-9]* *openat(AT_FDCWD, "/usr' | head
```

Only reads from `/usr`, `/etc`, the game installation and the folder itself
should appear.
