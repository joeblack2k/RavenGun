# RavenGun User Guide

RavenGun turns a PlayStation Move into a lightgun for arcade games. The RavenGun app is the one place where you manage everything: your games, the controllers and the settings. It looks and works much like the Dolphin emulator.

## Starting

Open **RavenGun** from the desktop. The window shows your game library, a toolbar and a status bar. The status bar shows the running game, your PS Moves with their battery level, and whether the engine is running (green dot).

The *engine* is the background service that reads the PS Move and drives the virtual lightgun. If it is stopped, use **Engine > Start Engine**.

## Your games

- **Add Folder** (toolbar or File menu) adds a folder with games, for example the TeknoParrot folder on your NAS. RavenGun searches it and recognises the games by their files. Nothing in the folder is changed.
- **Refresh** searches all game folders again.
- **View > List View / Grid View** switches between a table and big icons. **View > Search** (Ctrl+F) filters the list; Ctrl+Plus and Ctrl+Minus zoom the icons.
- The **Status** column tells you whether a game is *Ready*, *Not set up* (recognised, but RavenGun still has to prepare it) or *Unconfigured* (a folder RavenGun does not know).

Right-click a game for more:

- **Play**, **Properties / Edit...**
- **Add to Steam...**: the game gets its own entry in your Steam library (see "Add to Steam").
- **Set Up Game...** for a game that is *Not set up*: RavenGun prepares it with the lightgun bindings. For a game on the NAS, keep "Use a local view" checked.
- **Link to Known Game...** for an *Unconfigured* folder: pick the right game in Properties > General.
- **Set as Default Game**, **Open Containing Folder**, **Remove from List** (no files are deleted; **View > Show Removed Games Again** brings them back).

## Playing

Double-click a game, or select it and press **Play**. **Stop** ends the game.

Before a lightgun game starts, RavenGun makes sure you are ready, on a black full-screen page that you control with the PS Move alone:

1. **PS Move on.** If Player 1 has a PS Move and it is off, the screen says **"Please turn on your PS Move controller"**. Press the PS button on the Move; the game starts only when the Move is on. Press B (gamepad) or Esc to cancel.
2. **Aim calibration.** Each player whose Move is on aims at the target in the middle of the screen and pulls the trigger. During this step Player 1's Move glows blue and Player 2's red, so you know whose turn it is; afterwards they get their own color back. Press the MOVE button to skip a player (for example when you calibrated a minute ago).
3. **Player 2 can join.** If the game has a second gun and Player 2's Move is off, you get 10 seconds to turn it on ("Player 2: turn on your PS Move to join"). Player 1 can pull the trigger to start right away. Player 2 never blocks the start: you can always play alone.
4. The game starts.

Racing games played with a gamepad (such as Mario Kart Arcade GP DX) skip all of this and start at once. So does a game when nothing needs to be done (the Move is already on and the calibration is turned off). To turn the calibration off, use **Config > Display > Before a lightgun game starts**; RavenGun then still waits for the Move.

During play, the PS button recenters the aim: point at the middle of the screen and press PS.

## Game properties

**Properties** has seven pages:

- **General**: name, folders, input route and your own note. The **Input route** is how the lightgun reaches the game. The recommended route is preselected; some games offer another one (for example the virtual lightgun instead of the I/O bridge). Your choice is saved at once and used the next time the game starts. Choose the recommended route again to undo it.
- **Options**: the **Dongle** group (see below) and game settings such as free play or language. Click **Apply**; they take effect the next time the game starts. Settings of games stored on the NAS can only be changed when the game runs from a local view; otherwise they show "read-only (on NAS)".
- **Controls**: for lightgun games, what each button does in this game and which PS Move button you chose for it (player 1). **Set up controls...** walks you through the game's buttons ("Press the button for: Reload"; press PS to keep the default, pull the trigger to save). **Reset to defaults** removes your choice. Games played with a gamepad (Mario Kart) have nothing to pick here: set their buttons under Gamepad in General and in Options. Your choice applies from the next start of the game and only for that game.
- **Display**: resolution, window or full screen, and gamescope.
- **Patches**: small fixes RavenGun can apply to a game, for example a changed byte in the game runtime that works around a Wine bug. Each patch has a check box and a description (hover over it or select it). Your choice is saved at once and used the next time the game starts. A patch only changes a copy of a file (never the original, never a file on the NAS) and refuses to run when the file is not the version it was made for. Below the list you see the settings that apply to the game and where each comes from (type default, shipped with RavenGun, or yours). **Open user folder** opens your own settings for this game (`~/.local/share/ravengun/patches/<Game>/`): a `game.ini` and a `patches.ini` there win over the shipped ones, key by key, and you can add your own patches. **Open shipped folder** shows the shipped files (read-only).
- **Memory Card**: save data, NVRAM and high scores. Select files and press **Delete...** to reset them. Files on the NAS and files the game needs are read-only.
- **Info**: platform, I/O board and technical details.

### Dongle

Some games check for a hardware dongle. If you own the real dongle for a game, tick **Use dongle** under **Options > Dongle** (off by default). The **Dongle source** list has one entry for now: **USB pass-through (physical dongle)**. This means your own, physical dongle stays plugged into the Steam Deck (or its dock) and the game talks to it through the maker's own software. RavenGun does not emulate, copy or imitate a dongle and does not change the game.

What you need:

- The physical dongle, plugged in. The status line shows what RavenGun sees ("Dongle detected: <maker> <product> (vendor:product) — runtime: running", or "No dongle found"). It only reads the maker name, the product name and the USB vendor and product ID; it never reads or shows the serial number. **Refresh** checks again.
- The **official runtime of the dongle's maker**. For Aladdin/SafeNet/Thales HASP and Sentinel dongles that is the "Sentinel LDK Runtime" from Thales (the `aksusbd` and `hasplmd` services; its License Manager listens on `localhost:1947`). For Wibu-Systems dongles it is the CodeMeter Runtime. RavenGun does not install, ship or download it: if it is missing, the status line says "not installed" and points to the maker's official installation page. If it is installed but not running, start its service.
- Device access for the dongle: the setup wizard's **Permissions** step installs a device rule (`72-ravengun-dongle.rules`) that lets you, as a normal user, use USB devices of these makers. If you updated RavenGun, run the permissions step once more.

With **Use dongle** on, RavenGun checks for the dongle and its runtime before every start. If either is missing the game does not start and tells you why (exit code 6 in the log). With the box off nothing is checked and nothing changes.

## Controllers

**Controllers** (toolbar) shows Player 1 to 4. For each player choose **None**, a **Mouse** or a **PS Move**, and press **Configure**:

- **General**: name, LED color, blink and vibrate.
- **Aiming**: drift correction (Compass is recommended), prediction, smoothing, how far to turn to reach the screen edge, and the calibration screen on the TV.
- **Rumble**: strength and rhythm of the vibration when shooting, with a **Test** button.
- **Buttons**: what each Move, Sharp Shooter and Navigation Controller button does (coin, start, reload, ...). Press **Save**.
- **Calibration**: compass calibration and the gyroscope status.

**Pair New Controller...** walks you through connecting a new Move or Navigation Controller: cable in, pair, cable out, press PS. **Remove...** unpairs a controller.

A mouse as lightgun moves the gun of that player (the desktop pointer moves too); the left button shoots, right reloads.

## Big Picture Mode (TV and Steam Game Mode)

**View > Big Picture Mode** (F11), or start RavenGun from Steam, opens a full-screen menu made for the TV: a black screen with big game icons.

- **With the lightgun**: aim at a game and shoot. The game is selected ("Shoot again to start"). Shoot it again within 3 seconds, or hold the trigger for a second, to start it. The PS button centers the aim, as always.
- **With a mouse, keyboard or gamepad**: click a game, or use the arrow keys (D-pad) and Enter (A). Escape (B) asks whether to exit.
- Only games known to work well are shown. **Show All Games** at the bottom shows every game that is set up; **Show Playable Games** goes back.
- While a game runs, the menu hides. When the game ends, it comes back. **Stop Game** appears while a game is starting or running.
- The top right corner shows your PS Moves: player, battery and whether they are on.
- To protect OLED TVs, the screen stays black. After a while without input the icons dim and move slowly; after five minutes only a small moving text remains. Shoot or press a key to wake it.
- Behind the scenes, the user service `ravengun-oled-saver` runs `ravengun --saver-loop` (no shell script involved). It shows the black saver only when no game is on the TV, and a game always takes the screen back within a few seconds.
- **Exit** closes RavenGun (you are asked first).

## Setup Wizard

The first time you start RavenGun on the desktop, a wizard helps you set everything up. You can open it again with **Tools > Setup Wizard**. It checks this PC, then walks you through:

1. **Welcome and install**: if you started RavenGun from the unpacked download folder, press **Install RavenGun**. RavenGun copies itself to your home folder (`~/.local/opt/ravengun`), adds a menu entry and a desktop icon, sets up the engine to start when you log in, and restarts from the installed copy. No password is needed and your settings and games are not touched. If an older copy is installed, the button says **Update RavenGun** and replaces it. When RavenGun already runs from the installed copy, this step only shows where it is installed, and has **Uninstall RavenGun...** (removes the program, the engine service, the menu entry and, with your password, the device rules; your settings, library and games stay).
2. **Permissions**: one password prompt installs the device rules (so games see the virtual lightgun, and so you can use your own USB dongle) and the Bluetooth helper (so you can pair Moves without a password). Skip it if the check says everything is installed. **Remove** takes them away again.
3. **Engine**: install the engine as a service that starts when you log in.
4. **Controllers**: pair your PS Move.
5. **Calibration**: calibrate the compass of the Move on the TV.
6. **Games**: add your game folder.
7. **Components**: downloads and sets up what RavenGun needs to run games (see below).
8. **Steam**: add RavenGun to your Steam library.

When you are done: reboot into Game Mode and start RavenGun from your library.

## Components

The **Components** step (also under **Tools > Components**) lists everything RavenGun needs next to the app, with its state (installed, missing, outdated or not available), the download size and a progress bar per item. **Install All** fetches and sets up what is missing; **Install Selected** does just the rows you picked; **Cancel** stops after the current file (a half-finished download is resumed next time). Nothing is installed system-wide: everything goes into `~/.local/share/ravengun`, and nothing needs a terminal.

Without a window (ssh, scripts) the same code is available on the command line; no display is needed:

- `ravengun --components` lists every component: id, name, version, download size and state (`installed`, `missing`, `outdated`, `not available`), tab-separated.
- `ravengun --install-component ID [--install-component ID ...]` (or `--install-components id1,id2`) installs those components and what they depend on, prints progress (at most every 5%), checks the sha256 and exits 0 on success or 1 with the reason on stderr. `all` installs everything that is missing or outdated.

- **Downloaded and checked**: Wine, DXVK, BepInEx, the mods that your games' settings ask for (`[Mod ...]` in `game.ini`), PhysX, 32-bit SDL 1.2 and the WMV movie plug-ins, and DemulShooter's Unity plug-ins (from the author's own release page). Every file has a fixed SHA-256; a file that does not match is thrown away.
- **Taken from your Steam**: the DirectX and Visual C++ 2010 DLLs (Steamworks Shared), 32-bit libraries (Steam Linux Runtime) and 32-bit GStreamer (a Proton in Steam). Nothing is downloaded for these; if Steam does not have them the row says "Not available" and tells what to install in Steam.
- **Built by RavenGun**: the OpenParrot loader and cores, the JVS bridge and similar helpers; these come with the RavenGun download and are only copied into place.
- **Your TeknoParrot folder**: the TeknoParrotUi folder from your own game folders (add the folder in the **Games** step first). It is copied, never changed at its source. There is no official download for it: TeknoParrot's core files come from TeknoParrot's own updater.

## Add to Steam

**Tools > Add to Steam** adds RavenGun to your Steam library as a non-Steam game that opens Big Picture Mode, with its own artwork. Steam has to be closed for a moment: RavenGun asks before it closes Steam. A backup of your list of non-Steam games is kept next to it. Doing it again updates the entry instead of adding a second one.

To put a single game in your Steam library, right-click it and choose **Add to Steam...**. In Game Mode that entry shows the same ready check as above ("Please turn on your PS Move controller", then the aim calibration) and then starts the game; when you quit the game you are back in Steam. **Exit game** in the Steam menu stops the game too.

A desktop shortcut can do the same: use `ravengun --play <profile>` as the command (for example `ravengun --play Halo`).

## Settings

**Config** (toolbar) or **Options > Configuration...**:

- **Engine**: start, stop or restart the engine, and start it automatically at login.
- **Remote**: allow your phone to open the RavenGun web page on the home network, and see its address.
- **Display**: OLED protection (a black screen with a small clock when no game runs) and its wait time, the camera for games, and whether the aim is calibrated before a lightgun game starts.
- **Paths**: game folders, the TeknoParrot runtime folder (RavenGun checks it and tells you if something is missing) and how games are launched.
- **Advanced**: the engine log, the log of the last game start and the last library scan.
