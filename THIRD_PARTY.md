# Third-party code and files

RavenGun is licensed under GPL-3.0-or-later (see LICENSE). This file lists what comes from other projects, with source and license.

## PSMoveAPI (BSD-2-Clause)

Source: https://github.com/thp/psmoveapi, commit `256b6299e6076fe7b113e7277ed67016903f067a`. Copyright (c) 2011, 2012 Thomas Perl. RavenGun's engine (`ravengund`) links against the unmodified library `libpsmoveapi.so`, built from that commit without the optional parts (tracker, PS3EYEDriver, sixpair). The release archive includes the library as `lib/libpsmoveapi.so` and the license text as `share/ravengun/licenses/PSMoveAPI-COPYING`. The Bluetooth helper (`scripts/ravengun-bt-helper`) contains an HID service record taken from `src/platform/psmove_port_linux.cpp` of PSMoveAPI (BSD-2-Clause, license text in the helper's header). How to build it: `docs/build.md`.

## Dolphin Emulator (GPL-2.0-or-later)

Source: https://github.com/dolphin-emu/dolphin, commit `e6f3ae17627e4344da95b13424af5baf4c892b08` (7 October 2026). Copyright Dolphin Emulator Project. GPL-2.0-or-later is compatible with GPL-3.0-or-later; the files taken over keep their own SPDX line (GPL-2.0-or-later) and name the Dolphin file in their header. The user interface of the Qt app (`src/app`) is modeled on DolphinQt.

Code taken over (adapted for RavenGun):

| RavenGun | Dolphin (Source/Core/DolphinQt/...) | What |
|---|---|---|
| `src/app/widgets/StackedSettingsWindow.{h,cpp}` | `Config/SettingsWindow.{h,cpp}` (StackedSettingsWindow) | window with a navigation list on the left and panels on the right (Properties, Settings) |
| `src/app/widgets/QtUtils.{h,cpp}` | `QtUtils/WrapInScrollArea.{h,cpp}`, `QtUtils/NonDefaultQPushButton.{h,cpp}` | scroll area that proposes the size of its content; button that never becomes the default button |
| `src/app/widgets/SearchBar.{h,cpp}` | `SearchBar.{h,cpp}` | search bar above the game list |
| `src/app/widgets/ToolBar.{h,cpp}` | `ToolBar.{h,cpp}` | toolbar with text under 32 px icons and equal button widths |
| `src/app/widgets/MenuBar.{h,cpp}` | `MenuBar.{h,cpp}` (AddGameListTypeSection, AddListColumnsMenu, menu layout) | menu bar |
| `src/app/widgets/GameList.{h,cpp}` | `GameList/GameList.{h,cpp}`, `GameList/ListProxyModel.{h,cpp}`, `GameList/GridProxyModel.{h,cpp}` | list and grid view in a QStackedWidget, proxy models, zoom, OnHeaderViewChanged, context menu, empty view with double-click |
| `src/app/widgets/Resources.{h,cpp}` | `Resources.{h,cpp}` (LoadNamedIcon) | loading icons with @2x/@4x |
| `src/app/core/GameListModel.{h,cpp}` | `GameList/GameListModel.{h,cpp}` | structure: columns as an enum, SORT_ROLE, search filter, scale |

Only as an example for the layout (no code copied verbatim): `MainWindow.cpp` (CreateComponents, ConnectMenuBar/ToolBar/GameList), `Config/GamecubeControllersWidget.cpp` (rows "Port N" with a choice list and Configure; ours is ControllersWindow), `Config/PropertiesDialog.cpp` and `Settings/PathPane.cpp` (Game Folders with Add.../Remove and "Search Subfolders").

Files: the icons of the "Clean" theme (`Data/Sys/Themes/Clean`: browse, classic, config, fullscreen, gcpad, graphics, open, play, refresh, stop, each 1x/2x/4x) are in `src/app/resources/themes/clean/` and built into the app through `src/app/resources/app.qrc`.

## VQF (MIT)

`third_party/vqf/`, see `third_party/vqf/README.md`. Laidig and Seel, "VQF: Highly Accurate IMU Orientation Estimation with Bias Estimation and Magnetic Disturbance Rejection".

## OpenParrot (GPL-3.0)

Source: https://github.com/teknogods/OpenParrot (the open loader and the open core), commit `fc8ba632e1f67c5c340ff88f30efca743e8d38f5` (11 September 2026). Copyright the OpenParrot / TeknoGods contributors; GPL-3.0 (file `LICENSE` in the source), which is compatible with RavenGun's GPL-3.0-or-later. The source is not part of this repository: `scripts/build-openparrot.sh` fetches exactly that commit and builds `OpenParrotLoader.exe`, `OpenParrotLoader64.exe`, `OpenParrot.dll` and `OpenParrot64.dll` on your machine (MinGW-w64 and clang in a podman container) into `~/.local/share/ravengun/openparrot/<commit>/`. The build is opt-in per game (`[Loader] Build = ravengun`); by default RavenGun keeps using the loader from your TeknoParrot runtime. RavenGun ships only our changes to that source, as patches in `third_party/openparrot/patches/` (GPL-3.0, same license as the source): the Wine fix in the loader (no `CONTEXT_FLOATING_POINT` in `SetThreadContext`) and small portability changes for MinGW-w64. Details, toolchain pins and what is not built: `third_party/openparrot/README.md`.

## lindbergh-loader (CC BY-NC-SA 4.0, not included)

Source: https://github.com/lindbergh-loader/lindbergh-loader, commit `0af606d845b70339c335785c0eba68b47b78df3c` (5 May 2026). Copyright the Lindbergh Loader Development Team; Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (file `LICENSE.md` in the source). That license is **not** compatible with RavenGun's GPL-3.0-or-later (the NonCommercial clause is a further restriction), so no lindbergh-loader code is part of this repository or of the release archive. `scripts/build-lindbergh-loader.sh` fetches exactly that commit and builds it on your machine, for your own non-commercial use, in a rootless podman container (Ubuntu 22.04 i386, `packaging/containers/lindbergh-loader/Containerfile`), together with the i386 libraries SteamOS lacks (SDL 3, SDL_ttf 3 and SDL_image 3 under the zlib license, FAudio under zlib, and libstdc++5, ncurses 5 and libXmu from Ubuntu), into `~/.local/share/ravengun/lindbergh-loader/<commit>/`. RavenGun's engine loads the resulting `lindbergh.so` for the Sega Lindbergh games (`docs/lindbergh.md`). The one change we apply to that source, `third_party/lindbergh-loader/patches/0001-passthrough-count-escaped-bytes.patch`, is a derivative of it and therefore carries CC BY-NC-SA 4.0 (SPDX line in the file); it is not part of RavenGun's GPL code. The source's `libs/` directory also provides `libCg.so` (NVIDIA Cg 2.0 runtime) and `libopenal.so.0`, copied into that same local output. Details: `third_party/lindbergh-loader/README.md`.

## System libraries (not included)

The app uses the Qt 6 libraries of the system (LGPL-3.0, dynamically linked; SteamOS and Arch provide them). The engine uses the system's hidapi, BlueZ, dbus, libudev and systemd libraries. None of these are part of the release archive.

## Not included

RavenGun contains no games, no TeknoParrot components, no DemulShooter files and no proprietary runtimes. The optional Unity plugin (`plugins/unity`) is our own code; it is built on your machine against BepInEx, which you download yourself (`plugins/unity/README.md`).
