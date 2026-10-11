# Game compatibility

<!-- Generated from patches/<Game>/game.ini by scripts/gen-compatibility.py. Do not edit by hand. -->

This list shows what has been tried so far, on one setup (a Steam Deck with a PS Move ZCM1 and a PS3 Eye). It is a snapshot, not a promise: arcade games differ a lot, and some depend on the TeknoParrot runtime or on your own dump.
RavenGun ships no games. You need your own legally obtained copies.

## Playable (17)

These games start from RavenGun and can be played with the lightgun. Some are not yet confirmed with a real PS Move in every detail; the note says what was tested.

- **Gashaaaan Refill!!!!**: Playable after a one-time setup in the test menu (clock, screen, free play; now stored in the game's save data). Then start, aiming and trigger (the ball) work. Test menu: key 0, select with start, execute with P1 aux1.
- **Gashaaaan!!!!**: Playable: coin (key 5), start (key 1), aiming and trigger (the ball) work. First start shows 'BACKUP DATA ERROR': press TEST (key 0) once to repair. Free play can be set in the test menu (P1 aux1 = execute).
- **Go Go Strike**: Works: bowling with the Move as trackball (direction and swing set the throw), start (key 1) and trigger (click). 4:3 picture with black bars (gamescope).
- **HALO Fireteam Raven**: Fully working: lightgun, buttons and rumble.
- **House of the Dead: Scarlet Dawn**: Playable through the TeknoParrot UI (two programs: AM Daemon and the game): start (key 1), crosshair (measured at the corners and centre), trigger (left button) and sub trigger (right button, switches weapon) work; free play, one round played. Loading to the first stage takes about 1.5 minutes. Real Move buttons, player 2 and the test menu not tested.
- **Let's Go Island 3D: Lost on the Island of Tropics**: Playable: starts through to the attract, 3D calibration (red-cyan picture), start (key 1) and play. The crosshair follows the aim (measured on a grid in the cutscene) and the trigger fires. Loading takes about a minute. Real Move buttons and sound not checked.
- **Let's Go Island: Lost on the Island of Tropics**: Playable: starts through to the attract, start (key 1) and play. The crosshair follows the aim (measured on a grid) and the trigger fires. Fullscreen, no title bar. Loading takes about a minute. Real Move buttons and sound not checked.
- **Let's Go Safari**: Playable: the game starts through to the attract and driving (course choice and start with the buttons). Red, blue, yellow and green are the keys A, S, D and F, the wheel is the left and right arrow keys, coin is key 5. The steering keys change the picture (how much steering is not measured); the wheel from the Move's horizontal aim is not there yet; the window is not fullscreen.
- **Luigi's Mansion Arcade**: Works: title, attract (free play), start (key 1), aiming the vacuum and sucking with the trigger; test menu with key 0, select switch on aux1. Loading takes about 45 s; press start once the attract mode runs.
- **NERF Arcade**: Playable: free play, start (key 1), aiming and trigger (the score counts up). Runs with DXVK in its own Wine prefix; the BepInEx plugin loads. Not tested: real Move buttons, player 2 and sound.
- **Pigs Might Fly**: Playable: start (key 1) and trigger (the lever that launches the pig) work; portrait picture in the middle of the TV. Test menu: TEST (key 0), service (F3). Runs in its own Wine prefix where C:\Sega points at the game.
- **Raccoon Rampage**: Playable: aiming, trigger (also start) and coin (key 5) work; test menu with TEST (key 0), service (F3) and F4. Calibration was done once and is stored in the game's save data. Runs with DXVK and without intro videos.
- **Rampage**: Playable, but not a lightgun game: a joystick-and-attack game for three players. Player 1: up = trigger, down = reload, left = grenade, right = aux1, attack = key F4, coin = key F5, start = key 1 (player 2: coin key 5, start key 2). Measured: intro movie, tutorial, climbing, walking left and right, demolishing buildings ($ 7,500). Runs with DXVK and gst-libav (AAC decoder for the movies); no crosshair. Real Move buttons, players 2 and 3 and the test menu not tested.
- **Terminator Salvation**: Works full screen (through gamescope). Aiming and shooting work well, also through the I/O bridge (shared memory route).
- **Time Crisis 5**: Playable through the I/O bridge (shared memory): aiming, trigger and left pedal (middle button) work; coin = the Move button for key 5 (the engine sends Shift+T to the game). Shoot '1-player solo' in the menu. The right pedal has not been checked on its own yet.
- **Tomb Raider Arcade**: Works without the TeknoParrot UI: lightgun, reload, coin and start, rumble when shooting. Not yet played with the real Move buttons.
- **Transformers: Shadows Rising**: Playable with the Move: the engine acts as the gun board of the game (hid-board route). Start (key 1 or Move Start), trigger and aiming work; the first load takes about 3.5 minutes ("SYSTEM INITIALIZING"). No coin needed (free play). The gun calibration screen, the test menu and the levers (attack/defend) have not been tried yet. Do not restart the engine while the game runs.

## Not working yet (16)

These games do not start, or are blocked by something RavenGun does not work around (for example copy protection). They are listed so you know what to expect; they may start working in a later version.

- **Gaia Attack 4**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Galaga Assault**: Parked. One of three Raw Thrills games (with Jurassic Park, Pac-Man Chomp-Mania and Galaga Assault) that TeknoParrot's loader does not start under Wine yet. A home-made fix would have to undo the copy protection's work, which we don't do. Waiting on TeknoParrot's loader.
- **Ghostbusters**: Does not start yet: the loader rejects the game path (a file seems to be missing from the dump). Parked.
- **Haunted Museum**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Haunted Museum II**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Infinity Blade FX**: Does not start yet: UE3 starts (needs Wine Mono and a first-run dialog for TeknoParrot.dll), then a white or black picture; the log stops at 'Kaboom: InitLib() failed'. Parked.
- **Jurassic Park Arcade**: Parked. One of three Raw Thrills games (with Jurassic Park, Pac-Man Chomp-Mania and Galaga Assault) that TeknoParrot's loader does not start under Wine yet. A home-made fix would have to undo the copy protection's work, which we don't do. Waiting on TeknoParrot's loader.
- **Lethal Enforcers 3**: Does not start yet: the game crashes when it opens a .m1v movie; Wine has no 32-bit GStreamer (lib32-gstreamer plus an MPEG-1 decoder), which the SteamOS repos do not offer. Parked.
- **Medaru No Gunman**: Does not start cleanly yet: picture, coin (key 5), reload, trigger and aim work, but on every fresh start the game reports 3001 RAM DATA IS BAD or 3003 ROM HAS CHANGED. After one pass through its test menu it plays (that process only). Window 1024x768, top left.
- **Music GunGun 2**: Does not start yet: the Wine crash is worked around (RG_LOADER_FPFIX), but the game shows its own 'Direct3D initialisation failed' window (also with DXVK) and does not open the JVS pipe. Parked.
- **Omatsuri Yasan - Ganso Takoyaki**: Does not start yet: white window (1024x768), the game never creates a D3D device; the core makes an online activation request at start. Parked.
- **Omatsuri Yasan - Kingyo Sukui**: Does not start yet: the game exits with code 8 after scanning all drive letters for rqrdlist.rqr and campaign*.bmp. Parked.
- **Otomedius Excellent**: Does not start yet: white window; the game looks for an ACIO micom on COM2 ('COM Port 2 Open Error') and goes no further. Parked.
- **Pac-Man Chomp-Mania**: Parked. One of three Raw Thrills games (with Jurassic Park, Pac-Man Chomp-Mania and Galaga Assault) that TeknoParrot's loader does not start under Wine yet. A home-made fix would have to undo the copy protection's work, which we don't do. Waiting on TeknoParrot's loader.
- **Rabbids Hollywood Arcade**: Starts and responds to the Move (coin, start, world select), but quits itself after about a minute: the game's own dongle check (SBK.Matrix, "DONGLE NOT FOUND"). Playable with the real dongle (pass-through, to be built).
- **Rainbow Bombergirl**: Does not get past the start of the game: it starts through to the attract (two screens: portrait main screen on the right, touch screen bottom left), and the test menu (TEST key 0, service F3) and input check show every button, but after coin (key 5) and start it stays at SYSTEM ENTRY / the PASELI notice. Parked.
