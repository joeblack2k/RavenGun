# Building

Status per component: implemented and automatically tested unless stated otherwise.

## ravengunctl and camprobe (on the host)

The host has g++ and cmake (no ninja). No containers needed:

    cmake -S . -B build
    cmake --build build -j
    ctest --test-dir build --output-on-failure
    ./build/ravengunctl doctor
    ./build/ravengunctl devices --json

Requirements: C++20, CMake >= 3.16, only the standard library and Linux kernel headers (`linux/videodev2.h`). Test framework: none; `tests/test_main.cpp` uses its own `CHECK` macro (active under NDEBUG) through CTest. 88 checks pass on this host.

Exit codes of `ravengunctl`: 0 everything is fine (or `devices` succeeded), 1 at least one warning, 2 at least one error, 64 wrong usage, 70 internal error. The program is read-only. It only calls `bluetoothctl --version`, `bluetoothctl devices` and `bluetoothctl devices Paired`. Bluetooth addresses are redacted by default (`--full-addresses` shows them).

`camprobe` (`src/tools/camprobe`) measures fps and frame intervals through V4L2 timestamps and never reads image content:

    ./build/camprobe --format YUYV --width 640 --height 480 --fps 60 --seconds 10 [--json]

## ravengund (the daemon)

`cmake -S . -B build` looks for PSMoveAPI under `~/.local/share/ravengun/psmoveapi` (`-DPSMOVEAPI_ROOT=…` for another location). Found: ravengund is linked against it, with an rpath to `…/psmoveapi/lib` (after `cmake --install`: `$ORIGIN/../lib`, see "Install layout"). Not found: ravengund is built without HID (cmake warning). `-DRAVENGUN_BUILD_DAEMON=OFF` skips the daemon. On the host only g++ is needed; the PSMoveAPI headers and library come from the container build below.

    ./build/ravengund                 # foreground; Ctrl-C or SIGTERM stops cleanly
    ./build/ravengunctl status        # or --json
    ./build/ravengunctl recenter
    ./build/ravengunctl watch --rate 10 [--duration 30] [--json]
    ./build/ravengunctl filter --off | --on [--min-cutoff 1.0] [--beta 0.5]

Options of ravengund: see `ravengund --help` (`--no-filter`, `--h-edge-deg`, `--v-edge-deg`, `--controller <address>`, `--no-auto-center`, `--no-hid`). Exit codes of ravengund: 0 stopped normally, 64 wrong usage, 69 already running or no `XDG_RUNTIME_DIR`, 71 socket or system error. New exit codes of ravengunctl: 69 daemon unreachable, 75 daemon temporarily refuses (for example recenter without a Move), 76 unexpected answer.

Tests: `ctest --test-dir build` runs five tests, among others (plus `ravengun_gamesettings_tests` and `ravengun_gamesettings_py`: the reader of `patches/<Game>/game.ini` in C++ and Python, the migration and `tp-add-game.py`; and `ravengun_launch_direct`: also the file patches): `ravengun_tests` (88 checks), `ravengun_core_tests` (279 checks: quaternions, axes, units, fusion, bias, aim, 1€, buttons, stale/disconnect, JSON and protocol) and `ravengund_ipc` (`tests/ipc_smoke.sh`: daemon without hardware, permissions 0700/0600, single instance, CLI, SIGTERM). All pass on this host. `ravengun_output_tests` (188 checks) covers the coordinate transformations (inversion, scale, offset, clamp, letterboxing exactly once), arm/disarm, releasing buttons on disarm, write errors and shutdown (with a fake device), the test patterns and the new IPC commands. Tested on real hardware: see `docs/hardware.md`, the measurement with ravengund.

Phase 0/1/3 adds `ravengun_phase013_tests` (report parsing, Move clock, stamp-dt, recording format, deterministic playback, ellipsoid fit, quality gates, storage, disturbance detection and the new IPC commands), and extends `ravengund_ipc` and `ravengund_web` with recording, LED and magnetometer status. The analysis program `ravengun-analyze` (replay, timebase, mag, magfit) only reads recording files and does not open hardware. New CLI commands: `ravengunctl record`, `led` and `calibrate mag` (see `docs/procedures.md`).

## The `ravengun` app (Qt 6, in a container)

SteamOS has Qt 6.11.1 as a system library but no headers and no CMake files. The app is therefore built in a rootless podman container with the same Qt (decision and background: `docs/decisions.md`, "The Qt app: Qt 6.11 from a container"):

    scripts/build-app.sh            # create the image if missing, configure and build
    scripts/build-app.sh --test     # plus the app tests, in the container and then on the host
    scripts/build-app.sh --clean    # empty build-app/ first
    build-app/ravengun              # start it (on the desktop)

- Image: `localhost/ravengun-qt-build` from `packaging/containers/qt-app/Containerfile` (`--rebuild-image` rebuilds it). Base `archlinux:latest`, pinned back to the Arch Linux Archive of 2026-07-01: `qt6-base` 6.11.1, glibc 2.43, GCC 16.1.1, the same as the host. The container has no network during the build and mounts only the repo, at the same path.
- Configuration: `cmake -G Ninja -DRAVENGUN_BUILD_DAEMON=OFF -DRAVENGUN_BUILD_CAMPROBE=OFF` in `build-app/`, targets `ravengun_app` (binary `ravengun`) and `ravengun_app_tests`. In the normal host build (`build/`) CMake does not find Qt and skips the app (message "Qt 6 (with headers) not found"); all other targets and ctest suites stay the same.
- Check: the script runs `ldd build-app/ravengun` on the host and stops at a missing library; `libQt6Core.so.6` and `libQt6Widgets.so.6` come from `/usr/lib` (the system Qt). The app tests then run once more on the host against the system Qt (`QT_QPA_PLATFORM=offscreen`).
- Installing: the app installs itself (see "Install layout" below: wizard, step 1, or `ravengun --install`); for a tree from the build use `cmake --install` (the app build installs with `cmake --install build-app --component app`). The shortcut comes from the template `packaging/desktop/ravengun-qt.desktop.in`, filled in by the app (`src/app/core/Install.cpp`); the old `packaging/desktop/ravengun.desktop` and `ravengun-qt.desktop` (fixed repo paths) are only for the existing installation on this machine.

Screenshots for developers (never on the TV; against a separate test engine):

    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg \
      build/ravengund --no-hid --no-camera --web-port 18765 --controllers-fake tests/fake_controllers.json \
      --tp-dir /tmp/rg/tp --games-dry-run &
    XDG_RUNTIME_DIR=/tmp/rg/run HOME=/tmp/rg/home XDG_CONFIG_HOME=/tmp/rg/cfg XDG_DATA_HOME=/tmp/rg/home/.local/share \
      QT_QPA_PLATFORM=offscreen RAVENGUN_SYSTEMCTL=/bin/false \
      build-app/ravengun --screenshot-dir /tmp/rg/shots --screenshot-game tp:TC5

`--screenshot-dir` opens in turn the main window (list and grid), Properties (all panels), Controllers, the Move settings (all tabs), the pairing wizard, Settings (all panels), Options and General (route) of four games (`$RAVENGUN_SCREENSHOT_GAMES`, default TC5, Terminator, Transformers, Jurassic Park) plus the open route list, the eight wizard steps and full-screen mode at 1920x1080 (menu, aim point, selected tile, all games, dimmed, screen saver, exit question), saves them with `QWidget::grab()` and stops. Nothing is clicked; `RAVENGUN_SYSTEMCTL=/bin/false` makes sure that even a mistake cannot touch a real unit. Also set `RAVENGUN_PKEXEC=/bin/false`, `RAVENGUN_NO_GAMEPAD=1` and `RAVENGUN_STEAM_ROOT=<a copy or empty folder>`, so the wizard and Add to Steam never touch the real Steam files or root.

App tests: `ravengun_app_tests` (core, without windows: options, library, routes, notes, full-screen logic, VDF and Steam, wizard commands) and `ravengun_app_widget_tests` (windows offscreen: Properties for all games with a real `patches/<Game>/options.json`, full-screen, wizard with a fake pkexec, Add to Steam on a copy). `ravengun_components_tests` (components, see below: catalogue, states, sha256, unpacking, staging, downloading through `file://`, Steam and Wine recipes with fake files; 23 checks). `scripts/build-app.sh --test` runs all three, in the container and on the host. The test on a copy of a real `shortcuts.vdf` only runs with `RAVENGUN_TEST_SHORTCUTS_VDF=<copy>` (otherwise skipped).

### Components from the app (no shell scripts)

The wizard step "Components" (also Tools > Components) fetches everything that belongs in the user folder (`~/.local/share/ravengun`): Wine (Kron4ek `proton-11.0-2`), DXVK, BepInEx, mods, PhysX, the DLLs from Steam, 32-bit libraries, our own components and the user's TeknoParrot runtime. The catalogue is data: `packaging/components.json` (embedded in the binary, the files in the data map take precedence), read by `src/app/core/Components.*`; installation is done by `core/ComponentInstaller.*`; the page is `widgets/ComponentsPage.*`. Without a window (no display needed, offscreen): `ravengun --components` (list: id, name, version, size, state) and `ravengun --install-component ID` (repeatable; or `--install-components a,b`; `all` = everything missing or outdated; exit code 0/1, progress at most every 5%), in `core/ComponentsCli.*` with the same `ComponentInstaller`. Test: `ravengun_components_tests` runs the real program with a fake catalogue (`RAVENGUN_DATA_DIR`), `XDG_DATA_HOME` in a temporary folder and `file://` downloads.

- Types: `download` (https, sha256 pinned, resume with Range, `.part` -> check -> rename), `bundled` (ships in `<datamap>/components/<id>/`, is copied), `userfolder` (searched in the user's game folders, copied without overwriting anything), `steam` (recipes `redist-directx`, `redist-vc2010`, `gst32`, `lib32-sniper`: read from the Steam installation) and `wine` (`wine-prefix`, `wine-physx`: start RavenGun's Wine directly).
- Mods are not in the catalogue but come from `[Mod <name>]` in the `game.ini` of the games the app knows (search order of `gamesettings`, so also in a release without `patches/` in the repo); `TreeSha256` guards the content, as in the old `scripts/fetch-mods.py`.
- Staging: everything is first unpacked in `<data>/components/.staging/<id>` and only moved into place with a `rename` after the check (an existing folder is moved aside first and then removed). An installation gets a stamp in `<data>/components/<id>.json`; a manually prepared component that satisfies `detect` counts as installed, a different version in the stamp as outdated.
- Unpacking with **libarchive** (tar.gz/xz, zip, zst, cab), without `tar` or `unzip`; paths with `..` and symlinks outside the folder are rejected. libarchive is in the build container (`archive.h`, through pacman) and on SteamOS (`libarchive.so.13`); the main `CMakeLists.txt` does not build the app if libarchive headers are missing. Nothing in `packaging/containers/qt-app/Containerfile` needs to change.
- Trying manually without touching the real folder: `XDG_DATA_HOME=/tmp/x build-app/ravengun`. The opt-in tests `RAVENGUN_COMPONENTS_REAL=1` (read-only: the state on this machine) and `RAVENGUN_COMPONENTS_NET=1` (really download to a temporary folder) are in `ravengun_components_tests`.

## PSMoveAPI (in a container)

SteamOS does not provide development headers for hidapi, libusb, OpenCV, glm and BlueZ. PSMoveAPI is therefore built in a rootless, unprivileged podman container (not distrobox: that starts with `--privileged` and mounts the whole host).

    podman build -t ravengun-build -f cmake/container/Containerfile cmake/container
    mkdir -p ~/.cache/ravengun/{src,build,pc} ~/.local/share/ravengun/psmoveapi
    cp cmake/container/glm.pc ~/.cache/ravengun/pc/
    git clone https://github.com/thp/psmoveapi ~/.cache/ravengun/src/psmoveapi
    git -C ~/.cache/ravengun/src/psmoveapi checkout 256b6299e6076fe7b113e7277ed67016903f067a
    cd ~/.cache/ravengun/src/psmoveapi
    RUN=~/projects/ravengun/cmake/container/run.sh
    $RUN sh -c 'PKG_CONFIG_PATH=$HOME/.cache/ravengun/pc cmake -S . -B ~/.cache/ravengun/build/psmoveapi -G Ninja \
        -DPSMOVEAPI_VENDORING=OFF -DPSMOVE_BUILD_TRACKER=OFF -DPSMOVE_BUILD_NAVCON_TEST=OFF -DPSMOVE_USE_SIXPAIR=OFF \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib" \
        -DCMAKE_INSTALL_PREFIX=$HOME/.local/share/ravengun/psmoveapi && ninja -C ~/.cache/ravengun/build/psmoveapi'

The container mounts only `~/.cache/ravengun`, `~/projects/ravengun` and `~/.local/share/ravengun/psmoveapi` (see `cmake/container/run.sh`).

Installation is done by hand (do NOT use `ninja install`: the install step writes `99-psmove.rules` to `/etc/udev/rules.d` and fails there, correctly):

    P=~/.local/share/ravengun/psmoveapi; B=~/.cache/ravengun/build/psmoveapi; S=~/.cache/ravengun/src/psmoveapi
    mkdir -p $P/bin $P/lib/pkgconfig $P/include/psmoveapi
    cp $B/psmove $B/example $B/example_new_api $B/multiple $P/bin/
    cp $B/libpsmoveapi.so $P/lib/
    cp $S/include/*.h $B/psmove_config.h $P/include/psmoveapi/
    sed 's#lib64#lib#' $B/psmoveapi.pc > $P/lib/pkgconfig/psmoveapi.pc

Chosen options and why:
- `PSMOVEAPI_VENDORING=OFF`: the submodules (hidapi, libusb, PS3EYEDriver) are not cloned; system hidapi (hidraw) from the container.
- `PSMOVE_BUILD_TRACKER=OFF`: the tracker requires OpenCV 4 (`find_package(OpenCV 4)`). The container has OpenCV 5.0.0, which no longer has the C API (`opencv2/core/core_c.h`); a trial build with an adjusted version check failed on that. RavenGun uses its own V4L2 pipeline, so the tracker is not needed for now. Camera support through `PSMOVE_USE_PS3EYE_DRIVER` is also off (requires vendoring, partly GPL).
- `PSMOVE_USE_SIXPAIR=OFF`: only for Navigation Controllers and pulls in libusb-compat (`libusb-0.1.so.4`), which is missing on the host. `PSMOVE_BUILD_NAVCON_TEST=OFF`: no SDL2 needed.
- The rpath is `$ORIGIN/../lib`; the binaries run on the host. Check: `ldd` shows no missing libraries; highest required symbol version is GLIBC_2.43 (host: 2.43). If a host update moves to a glibc older than the container, a rebuild may be needed: pin the image to an Arch archive date then.

Side effect when using it: PSMoveAPI writes a calibration cache to `~/.psmoveapi/<serial>.calibration` (not XDG; move it with `HOME=`).

Built: `libpsmoveapi.so`, `psmove` (CLI: list, pair, register, battery, calibrate, etc.), `example`, `example_new_api`, `multiple`. Not built: tracker, fusion-tracker, test_tracker, camera tools, navcon test, sixpair.

Tested on real hardware (USB): `psmove list` and `psmove help`, `multiple` (1 controller visible). Not tested: pairing, Bluetooth connection, sensor data.

## OpenParrot loader and core (own build, optional)

    scripts/build-openparrot.sh            # everything: OpenParrotLoader(64).exe and OpenParrot(64).dll
    scripts/build-openparrot.sh --versions # commit, base image and package versions

Fetches the pinned commit of the public OpenParrot source (`~/.cache/ravengun/src/openparrot`), applies `third_party/openparrot/patches/` (the Wine fix in the loader and what MinGW-w64 needs) and builds in a rootless podman container (`packaging/containers/openparrot/Containerfile`: MinGW-w64 and clang, no network during the build). Output in `~/.local/share/ravengun/openparrot/<commit>/` (not in the repo), with `BUILDINFO.txt` and the `current` reference. Use: `[Loader] Build = ravengun` in `game.ini` (`docs/per-game-patches.md`). Details and what is not built: `third_party/openparrot/README.md`.

## Output (virtual lightgun)

The daemon creates the uinput device only after `arm`. Without a Move and camera, for the output test:

    ./build/ravengund --no-hid --no-camera &     # does not touch the Move, Bluetooth or the camera
    ./build/ravengunctl arm
    ./build/ravengunctl test-pattern center      # or corners, grid, sweep, point X Y, stop
    ./build/ravengunctl output-button trigger tap
    ./build/ravengunctl set-output --invert-y 1  # change the transformation live
    ./build/ravengunctl disarm

The udev rule (`packaging/udev/70-ravengun.rules`, install with `sudo scripts/udev-ravengun.sh --apply`, roll back with `--rollback`) is needed for readability, the stable by-id symlink and libinput ignoring the device. Order for a game session: first the daemon with `arm` (and the virtual keyboard), then start TeknoParrot/Halo; the daemon must keep running as long as the game runs.


## Install layout (A9)

Engine and app no longer depend on `~/projects/ravengun`. Default prefix: `~/.local/opt/ravengun` (no root; the home partition survives SteamOS updates).

- `bin/`: `ravengun` (the Qt app), `ravengund` (the engine), `ravengunctl`. Rpath `$ORIGIN/../lib`.
- `lib/`: `libpsmoveapi.so` (the only non-system library) and the LD_PRELOAD helpers `librg_comm_shim.so`, `lib32/librg_comm_shim.so` and `librg_rt_input.so`. Qt, hidapi, BlueZ, dbus and libudev come from the system (Qt 6.11 or newer).
- `patches/`: shipped game profiles and our own drop-ins, part of the same Git repository as the application. `share/ravengun/patches -> ../../patches` is only a relative compatibility link, not a second copy. Your own settings, saves and prefixes stay in the existing XDG user folders; updates do not replace them. Private dongle data, game files and captures are not added to Git.
- `share/ravengun/` (the "data map"): `scripts/`, `packaging/` (JSON data, unit and shortcut templates, udev rules, icons, `containers/unity-plugin`), `docs/user-guide.md`, `plugins/unity/`, `third_party/` (readmes and licences), `LICENSE`, `THIRD_PARTY.md`, `VERSION` (version and commit) and in a release build `REQUIRES` (the glibc requirement) and `licenses/`. Not included: `scripts/build-app.sh`, `tools/dev/make-release.sh`, the Qt app and release containers, and this machine's per-game shortcuts.

Search order for the data map (engine: `src/ravengun/paths.*`, app: `AppPaths::dataRoot`, scripts: `scripts/rg-paths.sh`; both C++ sides have tests: `ravengun_paths_tests` and `AppCoreTest::dataRootOrder`):

1. `$RAVENGUN_DATA_DIR` (if it contains scripts/ and packaging/),
2. `<prefix>/share/ravengun`, with `<prefix>` = the folder above the program's folder (`bin/..`),
3. the repo during development: `$RAVENGUN_ROOT`, the folder above `build/` or `build-app/`, and finally `~/projects/ravengun`.

The engine reports at start which data map it uses ("datamap: ... (installation ...)"); `ravengun --print-paths` shows the data map, program folder and engine. The scripts derive `ROOT` (the data map) from their own path; `scripts/rg-paths.sh` provides `RG_BIN` and `RG_LIB`: in the repo `<repo>/build`, in an installation `<prefix>/bin` and `<prefix>/lib` (`RAVENGUN_BIN_DIR` and `RAVENGUN_LIB_DIR` take precedence). In an installation the Unity plugin is built to `~/.local/share/ravengun/unity-plugin` (`RAVENGUN_UNITY_PLUGIN_OUT` takes precedence). Per-game shortcuts (`make-game-shortcut.sh`) go to `~/.local/share/ravengun/shortcuts` in an installation, so an update does not take them along.

Installing yourself (the product path, without any script): the app runs from an unpacked release archive or a `cmake --install` tree (a complete tree: `bin/ravengund`, `bin/ravengunctl` and `share/ravengun/packaging` next to `bin/ravengun`). The wizard (step 1, "Install RavenGun"; or without a window `ravengun --install [--no-start]`) does, in `src/app/core/Install.cpp` (`Install::installTree`):

1. check the prefix (`prefixIsSafe`: absolute, clean, no spaces or `% " ' \`, not `/`, the home folder or above it, not under a system folder) and refuse if running as root or if the folder already exists, is not empty and is not a RavenGun installation;
2. copy `bin/`, `lib/`, `patches/` and `share/` to `<prefix>.new.<pid>` (keeping permissions and symlinks), check the requirements there (glibc from `share/ravengun/REQUIRES` against `gnu_get_libc_version()`; missing system libraries per program with `ld-linux --list`, so Qt 6.11 for the app) and then atomically swap with the existing installation (`rename` to `<prefix>.old.<pid>`, `rename` of the new one, remove the old). On any refusal the existing installation stays. `~/.config/ravengun`, `~/.local/share/ravengun` and `~/.local/state/ravengun` are not touched;
3. write the units from `packaging/systemd/*.in` (`@BINDIR@`, `@DATADIR@` filled in with the installed copy; paths with spaces or `%` are refused): `~/.config/systemd/user/ravengund.service` and `ravengun-oled-saver.service` (the file only; enabling is done by the app). Everything through a temporary file and `rename`;
4. the menu item `~/.local/share/applications/ravengun-qt.desktop` (and on the desktop, if `XDG_DESKTOP_DIR`/`~/Desktop` exists) from `packaging/desktop/ravengun-qt.desktop.in`, plus links `ravengun` and `ravengunctl` in `~/.local/bin`;
5. `systemctl --user daemon-reload` and `enable --now ravengund.service`. If the engine was already running, only `enable` and a restart if no game is running (`/proc` and `ravengun-tpui.service`); otherwise a notice. With `--no-start` only `enable`;
6. the wizard starts `<prefix>/bin/ravengun --setup-wizard` and closes itself.

Anyone starting from an unpacked, not yet installed archive also gets the wizard if it had been completed before; if the app runs from the installation itself, step 1 is only a confirmation (and an "Uninstall RavenGun..." button). `$RAVENGUN_PREFIX` chooses another prefix, `$RAVENGUN_SYSTEMCTL` replaces `systemctl` (tests). The engine step of the wizard (step 3) only rewrites and activates the units (`Install::writeEngineUnit`, `writeOledUnit`, `enableEngine`). Downloading components (DXVK, Wine, etc.) is no longer part of it: that is its own step. The OLED unit starts `<prefix>/bin/ravengun --saver-loop` (and on stop `--saver-stop-view`); enabling is done by the app (Settings, Display).

Installing from the repo (for developers; not part of the product, the scripts will disappear): after `cmake --build build` and `scripts/build-app.sh`, `scripts/install-user.sh` still works, or `cmake --install build --prefix ~/.local/opt/ravengun` (and `--component app` for the app build) followed by `ravengun --install` from that folder.

Permissions (root): a small program of our own, `ravengun-setup-helper` (`src/syshelper/`), which the wizard starts with `pkexec`, and `ravengun-bt-helper`, which the daemon starts with `sudo -n`. Design:

- Why a separate program and not `ravengun --apply-permissions`: the app is a Qt program (plugins, platform theme, many libraries and environment variables); starting that as root makes the attack surface large. `ravengun-setup-helper` is about 650 kB, without Qt, with only libc and a statically linked libstdc++, PIE, RELRO and `-z now`.
- Fixed arguments: exactly one of `--apply`, `--rollback`, `--check`, `--version`; anything else is exit 2. No filenames, no paths, no text from the user. The only input is who started pkexec: `PKEXEC_UID` (set by pkexec itself; `SUDO_UID` as a fallback for manual use), converted with `getpwuid_r` and checked against `^[a-z_][a-z0-9_-]{0,31}$`, never root.
- Everything it installs is embedded in the program itself (`cmake/EmbedBlobs.cmake`): the three udev rules from `packaging/udev` and the program `ravengun-bt-helper`. As root nothing is therefore read from a user-writable folder, except the program file itself (which pkexec already shows to the user). It is the same trade-off as before with the script, but without the script looking for its sources elsewhere.
- `--apply` writes the rules to `/etc/udev/rules.d` (0644), reloads udev (`/usr/bin/udevadm`), creates `/opt/ravengun` and `/opt/ravengun/bin` (root:root 0755), the helper `/opt/ravengun/bin/ravengun-bt-helper` (0755) and a copy of itself `/opt/ravengun/bin/ravengun-setup-helper` (0755; read from `/proc/self/exe`), and the sudoers rule: first as `/etc/sudoers.d/.ravengun-bt-helper.check` (0440; sudo ignores names with a dot), checked with `visudo -cf`, then `rename` to `/etc/sudoers.d/ravengun-bt-helper` and `visudo -c` again; if that last check fails, our rule is removed again. The rule only allows `<helper> register *` and `<helper> unregister *` for that one user, with `env_reset`. All files go through a temporary file in the same folder (`O_EXCL`, `O_NOFOLLOW`), `fsync` and `rename`.
- The copy in `/opt` has a purpose: `--rollback` (uninstall) and `--check` then run the root copy, which the user cannot change. Only `--apply` (install or update) runs the program next to the app, in a user-writable folder; that stays so (a first installation cannot do otherwise), and every prompt shows the path in the pkexec window. If you want to close that, install once with `sudo` from a root folder.
- Child processes (`udevadm`, `visudo`, `sudo -n -l`, `systemctl`): absolute paths, `fork` + `execve`, always an empty environment, no shell, no PATH. The program closes all inherited file descriptors at start and clears the environment (`PKEXEC_UID` is read first).
- `--check` and `--version` work without root; `--check` compares the installation byte for byte with what this program would install and reports the owner and permissions of the helper and folders and whether `sudo -n -l` allows the helper. Exit 0 = everything fine. `--version` shows the "content-id" of the embedded content.

`ravengun-bt-helper` (`src/syshelper/bt_core.*`; replaces `scripts/ravengun-bt-helper`, the same interface and the same BlueZ files): `register AA:BB:CC:DD:EE:FF move|move2|navigation` and `unregister AA:BB:CC:DD:EE:FF`. Exactly these arguments (otherwise exit 2, also without root, so it is testable), address in capitals and not zero or broadcast, a type from a fixed list; no environment and no PATH (only `/usr/bin/systemctl stop|start bluetooth.service`); `umask 077`; all file operations from opened folders with `O_NOFOLLOW` (a symlink where a folder or file should be means exit 4); exactly one adapter under `/var/lib/bluetooth`, not the adapter's own address; only overwrite or remove what is a Sony HID registration (Vendor 1356, product 981, 3166, 1071) or is already called "Motion/Navigation Controller"; the first version goes to `info.ravengun-backup`; the link key from `getrandom(2)`; Bluetooth is always started again (also after an error); logging to syslog and `/var/log/ravengun-bt-helper.log`. Exit codes: 0 success, 1 failed, 2 invalid arguments, 3 not root, 4 refused. The daemon (`src/ravengund/controllers.cpp`, `pairing::helper_argv`) calls `/usr/bin/sudo -n /opt/ravengun/bin/ravengun-bt-helper ...`, unchanged; only the text "how do I install the helper" now points to the app (Tools > Setup Wizard > Permissions).

Removing (wizard step 1 "Uninstall RavenGun..." or `ravengun --uninstall [--remove-config]`; `Install::uninstall`): stops and removes the units (and the saver drop-in), the menu item and icon (only files whose `Exec=` points to the prefix) and the links in `~/.local/bin`, runs `pkexec <root copy or helper> --rollback` if system components are present, removes the prefix and by default keeps the settings (`--remove-config` removes `~/.config/ravengun` and `~/.local/state/ravengun`). `~/.local/share/ravengun` (TeknoParrot runtime, Wine, native overlays) is never removed automatically. `$RAVENGUN_PKEXEC` replaces pkexec (tests).

## Release artefact (A9)

`tools/dev/make-release.sh` builds `dist/ravengun-<version>-x86_64-steamos.tar.gz` plus `ravengun-<version>-x86_64-steamos.sha256` (SHA-256 of the archive and of `ravengun-<version>-source.tar.gz`, the source of the same commit, for the GPL):

    tools/dev/make-release.sh                  # everything in containers; --no-test skips ctest, --rebuild-image rebuilds the images

- Only the committed sources (`git archive HEAD`) plus PSMoveAPI at the pinned commit (from `~/.cache/ravengun/src/psmoveapi`, otherwise cloned; one-time network on the host). With uncommitted changes the script refuses unless `--allow-dirty` (those changes then do not go in anyway).
- Image `localhost/ravengun-release-build` (`packaging/containers/release/Containerfile`) on top of `localhost/ravengun-qt-build` (Arch Linux Archive of 2026-07-01: Qt 6.11.1, glibc 2.43, GCC 16.1.1), with hidapi, libusb, glm, dbus, BlueZ and systemd headers. The build itself runs without network and mounts only a temporary work folder (`/work`).
- In the container: PSMoveAPI (only `libpsmoveapi.so`, the same options as above), then RavenGun in full (engine, CLI, helpers and the Qt app), the ctest suites (the install test and the JS test skip themselves), `cmake --install` and `share/ravengun/REQUIRES` (the highest glibc symbol version in the programs; `install-user.sh` refuses an older glibc with a clear message).
- Our own runtime components (on the host, before assembly; `share/ravengun/components/<id>/`, the "bundled" entries of `packaging/components.json`): `openparrot` (`scripts/build-openparrot.sh`: pinned source and pinned container, source with a fetch in `~/.cache/ravengun/src`), `jvsbridge` (`scripts/build-jvsbridge.sh`) and `rg-pe-hook` (`tools/dev/build-pe-hook.sh`): MinGW in the pinned container, without network; `hasp` (`scripts/build-hasp.sh --no-dropin`) and `memory` (`tools/dev/build-memory.sh`): Linux parts with the compiler of the build machine (`gcc -m32`), Windows parts in the local image `localhost/ravengun-mingw` (NOT yet hermetic; without that image the DLLs are missing with a notice). `lindbergh-loader` only with `--with-lindbergh` and then NOT for distribution (source CC BY-NC-SA, contains Nvidia Cg and old i386 libraries that are not ours). `rio` and `loaders` are no longer in the catalogue: the RIO libraries are patched copies of Raw Thrills game files (not ours, not distributable) and the patched OpenParrot loader is made by the engine itself (`patched/`). `--no-components` skips this (quick trial). TeknoParrot, DemulShooter and Wine remain downloads or the user's own files. Dongle data never goes along.
- Patches: from the same application repository (`--patches DIR`, default `patches/`) only the delivery (`tools/dev/ship-patches.py`: `<Game>/*.ini`, `native.json`, `unity.json`, `options.json`, `README.md`, `dropin/`, `data/`, `seed/`, without private dongle data, game shaders, saves and game-derived configuration; the same list as `install(DIRECTORY patches ...)`) to `patches/` directly in the application folder; no dossier. The data map contains only the relative compatibility link.
- Assembly: `tools/dev/release-assemble.sh` adds `RavenGun.desktop` (double-click in Dolphin starts `bin/ravengun`), `README.md`, `LICENSE` and the components and patches, with `share/ravengun/licenses/PSMoveAPI-COPYING` and `COMPONENTS.txt` (SHA-256 per component file); there is NO install script. Then permissions normalised, `tar --sort=name --mtime=@<commit time> --owner=0 --group=0 --numeric-owner` and `gzip -n -9`.
- Reproducible: fixed container paths, `SOURCE_DATE_EPOCH` = the commit time of HEAD, fixed order, owner and timestamps, the commit recorded in the binary with `-DRAVENGUN_GIT_COMMIT_OVERRIDE`. Building the same commit twice with the same images gives the same SHA-256 (see `docs/todo.md`, A9, for the check). If an image changes (a different pacman snapshot), the result changes.

Contents of the archive (`ravengun-<version>-x86_64-steamos/`, about 3 MB compressed): `RavenGun.desktop`, `README.md`, `LICENSE`, `bin/` (with `ravengun-setup-helper`), `lib/` and `share/ravengun/` (with `components/` and `patches/`) as above. The user unpacks the archive, starts `RavenGun.desktop` or `bin/ravengun` (double-click) and chooses "Install RavenGun" in the wizard; no terminal and no script needed. What the user does is described in `README.md`.

### AppImage: investigated and not chosen

The tarball was chosen (without an install script: the app installs). An AppImage fits this program poorly:

- Systemd user units and `systemd-run` scripts: the engine is a service that starts at login, and the engine starts every game itself through `systemd-run` (no start script any more). An AppImage mounts itself for every process at a changing `/tmp/.mount_XXXX` path (FUSE), and that path only exists as long as that process lives. A unit with `ExecStart=` on the AppImage file would create a FUSE mount on every start, and the scripts and JSON files the engine needs later are at a path that does not stay.
- `pkexec` and root: the wizard runs `install-permissions.sh` as root. Root cannot read a user FUSE mount by default (no `allow_other`), so those scripts, the udev rules and the Bluetooth helper must anyway be unpacked at a normal path.
- FUSE on SteamOS: libfuse2 is now on the Deck (`/usr/lib/libfuse.so.2`), but it is not a guaranteed part of SteamOS; without it the user must know `--appimage-extract-and-run`, and then it is in fact an unpacked tarball.
- Qt: the app is built against Qt 6.11, the same major/minor version as the SteamOS system Qt (see "The `ravengun` app"). An AppImage would have to ship Qt, the platform plugins and ICU itself (roughly 60 MB or more, bundled with `linuxdeploy`, so with network downloads and poorly reproducible). A bundled Qt also does not load the KDE platform theme plugin of the system (Breeze, Plasma integration) and looks different in Desktop Mode from everything else. With the system Qt the app uses the user's theme and dialogs and the download stays small. Binary compatibility: a build against Qt 6.11 runs on every later 6.x; a newer glibc on the build machine is caught by `REQUIRES`.
- Work: an AppImage would only cover the app (no units, udev, helper, engine as a service), so an installer is still needed next to it.

A Flatpak fails for the same reasons, and because the sandbox blocks uinput, udev, BlueZ and `systemctl --user`. Later perhaps an AppImage for the window only, if the engine is installed separately; not needed for the goal "download, start, works".

## Tests of the installation (A9)

`ctest` contains `ravengun_syshelper_tests` (the programs with root rights: argument checking, validators, the BlueZ files in a temporary folder, the udev rules, helper and sudoers in a temporary root, the real programs without root; 194 checks), in `ravengun_app_tests` the path logic, the unit and shortcut files, a complete self-installation, an update, refusals, the engine service, the uninstall and `ravengun --install` with the real app binary (all in a temporary HOME with fake `systemctl` and fake `pkexec`), in `ravengun_app_widget_tests` wizard steps 1 to 3, `ravengun_paths_tests` (search order of the data map), `ravengun_compat_list` (docs/compatibility.md is current; see `tools/dev/gen-compatibility.py`) and `ravengun_install` (`tests/install_test.sh`; skipped with exit 77 as long as `build-app/ravengun` is missing). The install test works in a temporary HOME with all XDG folders inside it, a fake `systemctl` and `pkexec` that only log (`RAVENGUN_SYSTEMCTL`, `RAVENGUN_PKEXEC`) and runs the engine with its own `XDG_RUNTIME_DIR`:

- install from the repo; check the files, units and shortcuts (no repo path in them), rpath and `ldd`;
- `ravengun --print-paths`, the engine (`--no-hid --no-camera`) and `ravengunctl status` from the installation, and the app offscreen with `--screenshot-dir` against that engine (at least ten screenshots);
- update, roll over to a second prefix (the route of the release archive), refuse a foreign folder and `/usr/local`;
- uninstall: units, shortcuts and prefix gone; settings and `~/.local/share/ravengun` stay; `--remove-config` and `--keep-root`; the call `pkexec ... install-permissions.sh --rollback`.
