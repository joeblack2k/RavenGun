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
- **Set Up Game...** for a game that is *Not set up*: RavenGun prepares it with the lightgun bindings. For a game on the NAS, keep "Use a local view" checked.
- **Link to Known Game...** for an *Unconfigured* folder: pick the right game in Properties > General.
- **Set as Default Game**, **Open Containing Folder**, **Remove from List** (no files are deleted; **View > Show Removed Games Again** brings them back).

## Playing

Double-click a game, or select it and press **Play**. RavenGun asks you to switch on the PS Move ("Waiting for PS Move: press the PS button"), then starts the game. **Stop** ends the game.

During play, the PS button recenters the aim: point at the middle of the screen and press PS.

## Game properties

**Properties** has five pages:

- **General**: name, folders, input route and your own note. The **Input route** is how the lightgun reaches the game. The recommended route is preselected; some games offer another one (for example the virtual lightgun instead of the I/O bridge). Your choice is saved at once and used the next time the game starts. Choose the recommended route again to undo it.
- **Options**: game settings such as free play or language. Click **Apply**; they take effect the next time the game starts. Settings of games stored on the NAS can only be changed when the game runs from a local view; otherwise they show "read-only (on NAS)".
- **Display**: resolution, window or full screen, and gamescope.
- **Memory Card**: save data, NVRAM and high scores. Select files and press **Delete...** to reset them. Files on the NAS and files the game needs are read-only.
- **Info**: platform, I/O board and technical details.

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
- **Exit** closes RavenGun (you are asked first).

## Setup Wizard

The first time you start RavenGun on the desktop, a wizard helps you set everything up. You can open it again with **Tools > Setup Wizard**. It checks this PC, then walks you through:

1. **Permissions**: one password prompt installs the device rules (so games see the virtual lightgun) and the Bluetooth helper (so you can pair Moves without a password). Skip it if the check says everything is installed.
2. **Engine**: install the engine as a service that starts when you log in.
3. **Controllers**: pair your PS Move.
4. **Calibration**: calibrate the compass of the Move on the TV.
5. **Games**: add your game folder.
6. **Steam**: add RavenGun to your Steam library.

When you are done: reboot into Game Mode and start RavenGun from your library.

## Add to Steam

**Tools > Add to Steam** adds RavenGun to your Steam library as a non-Steam game that opens Big Picture Mode, with its own artwork. Steam has to be closed for a moment: RavenGun asks before it closes Steam. A backup of your list of non-Steam games is kept next to it. Doing it again updates the entry instead of adding a second one.

## Settings

**Config** (toolbar) or **Options > Configuration...**:

- **Engine**: start, stop or restart the engine, and start it automatically at login.
- **Remote**: allow your phone to open the RavenGun web page on the home network, and see its address.
- **Display**: OLED protection (a black screen with a small clock when no game runs) and its wait time.
- **Paths**: game folders, the TeknoParrot runtime folder (RavenGun checks it and tells you if something is missing) and how games are launched.
- **Advanced**: the engine log, the log of the last game start and the last library scan.
