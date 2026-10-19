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
scripts/build-app.sh              # build and install into ~/.local/opt/ravengun
scripts/make-portable.sh          # assemble dist/RavenGun from that install plus this repo
scripts/make-portable.sh --reset-user   # rebuild User/ from scratch
dist/RavenGun/ravengun.sh app     # start from the folder
```

The target folder must be new, empty, or an existing portable folder (it has `ravengun.sh` or the marker
`.ravengun-portable`). The script refuses a target that is the home folder, the runtime, the install, the
configuration, the TeknoParrot Proton build or the repository, or a parent of one of them (a run replaces
`bin/`, `lib/`, `share/` and `User*` in the target), and a target inside one of them except under
`<repo>/dist`. All of this is compared on real paths.

`make-portable.sh` copies `bin/`, `lib/` and `share/ravengun/` from the install
(replacing them, not merging), copies the shipped part of `patches/` from the committed repository (`git archive HEAD patches` and then
`tools/dev/ship-patches.py`, like `make-release.sh`: no dossier, no untracked or uncommitted files), copies the settings from `~/.config/ravengun`
into `User/config/ravengun` - without `web-token` and the `backups/*.bak` - and
copies the whole runtime and game state (`wine`, `proton`, `prefixes`,
`openparrot`, `runtime`, `native`, `writelayer`, `patches`, ...) from
`~/.local/share/ravengun` into `User/share/ravengun/`. That big copy runs with
`rsync -aX` into `User.new` next to `User/` (hidden entries included), after a check for `rsync` and free space; only after
the links have been rewritten and checked is `User.new` renamed to `User/`. A failed or interrupted run (an
rsync error other than "files vanished", Ctrl-C, no space) therefore never leaves a half-built `User/`
and never touches an existing one; the script exits non-zero. `bin/`, `lib/` and `share/` are likewise built as
`<name>.new` and then swapped in, so an interrupted run never leaves the folder without `share/`. A re-run with an existing
`User/` only refreshes the application and the profiles, and never
touches the user's state. Only `--reset-user` rebuilds it, and it refuses to start, and refuses the final swap, while any
process it can inspect still uses `User/` (a mount of or inside it, also as a bind under another path in a bwrap sandbox; its
working directory; an open or mapped file: `scripts/portable_tools.py busy`). After the swap the old tree stays as
`User.old` until the next successful run, so a game that still runs from it keeps its overlay upper, prefix and saves; it
needs room for two copies (three while an older `User.old` is still there; the script says how to remove it). If the
second rename of the swap fails, the old `User/` is put back. Unreadable overlay work directories (mode 000, made by games
that run from the folder) are made readable before anything is copied (only `u+r`/`u+rx` on the source runtime, never a
write bit: Wine reads a missing write bit as read-only); their original modes are recorded and restored right after the
copy (and on any exit), so the source runtime is left exactly as it was. In trees the script owns they are removed with
write permission. The overlay state of the write layers
(`writelayer/`, `native/*/upper`: whiteouts are character devices 0:0, plus `user.overlay.*` xattrs; bwrap mounts them as kernel
overlayfs in a user namespace) is carried over too: an unprivileged rsync skips the devices, so
`scripts/portable_tools.py whiteouts` recreates them with `mknod` (allowed for 0:0 without privileges) and checks them per
path. It only looks where overlays live (`writelayer/`, a top-level entry that carries `user.overlay.*` xattrs, and `upper`
directories up to three levels deep), not the ~100k inodes of `proton/` and `prefixes/`. A game that runs during the copy
changes the source after rsync: what exists only in the source (a new directory, a whiteout that vanished between scan and
stat) is reported and not compared; a difference on a path present in both stops the script, unless rsync ended with code
24 (files vanished), then it is a warning. A kept `User/` is neither scanned nor modified. Only `bench-captures`
(the proof screenshots) and `halo-overlay` (a root-owned overlay work dir) are
skipped. The game installations themselves are never touched.

After copying, every symbolic link that points into `~/.local` or into the
repository is rewritten to a relative link inside the folder (or dereferenced when
the target is not part of the folder, e.g. the gstreamer plugins of a Steam
Proton, also through a chain of directory links); a link into a renamed directory (`.verborgen`) is pointed at that
directory, a link to the root of the runtime/install/config itself too. A directory link becomes an EMPTY directory only
for a target on the allow list (`RG_PORTABLE_EMPTY_OK`, default the Steam `appcache`, which Steam refills); any other
directory is really copied (up to `RG_PORTABLE_MAX_EXTRACT`, 2 GiB) or the script stops. This is one pass
(`scripts/portable_tools.py links`) that rewrites and checks at once, lexically, so the thousands of NAS links are never
stat-ed; measuring a directory before extracting it never follows a link and stops as soon as the limit is passed, and a
file or directory is only extracted when it fits the limit and the free space. `~/.steam` and `~/.steampath` count as
`~/.local` (on the Deck they are links to `~/.local/share/Steam`). It exits non-zero if a link still points into
`~/.local`, if a relative link points out of the folder, or if a relative link points at something that is not in the
folder (for example into the skipped `bench-captures`); absolute links to the game
installations on the NAS are left as they are. The absolute paths in `library.json` (and `config.json`) are rewritten
to the folder, and `ravengun.sh` repeats that on every start so the folder may be
moved. The Wine wrappers in `User/share/ravengun/wrappers/` resolve the runtime
relative to themselves (they are rewritten after the links, via a temporary file and a rename, and never through a link:
live they are links to `scripts/wine-*.sh` in the repository, which must stay untouched), and the engine's fallbacks for the wine binary, the
prefixes, `redist` and the TeknoParrot runtime follow `$XDG_DATA_HOME` instead of
a hardcoded `~/.local` (the launcher points `XDG_*` at the folder).

The result is checked by the script itself: `find <folder> -type l -lname
"$HOME/.local/*" | wc -l` must be 0.

Measured (2026-10-18): `dist/RavenGun` is about 19 GB (almost all of it Wine, the
prefixes and the OpenParrot build) and building it takes about four minutes. With
`~/.local/share/ravengun`, `~/.config/ravengun` **and** `~/.local/opt/ravengun`
temporarily renamed away, the daemon and `ravengunctl` start from the folder, the
console reports `XDG_DATA_HOME` inside the folder, and SDR reached the attract from
it for more than two minutes (`bench-captures/SDR/20261018-portable-isolation-proof.png`).

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
