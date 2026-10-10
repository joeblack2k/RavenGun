# Game compatibility

<!-- Generated from packaging/games-notes.json by scripts/gen-compatibility.py. Do not edit by hand. -->

This list shows what has been tried so far, on one setup (a Steam Deck with a PS Move ZCM1 and a PS3 Eye). It is a snapshot, not a promise: arcade games differ a lot, and some depend on the TeknoParrot runtime or on your own dump.
RavenGun ships no games. You need your own legally obtained copies.

## Playable (10)

These games start from RavenGun and can be played with the lightgun. Some are not yet confirmed with a real PS Move in every detail; the note says what was tested.

- **Gashaaaan Refill!!!!**: Playable after a one-time setup in the test menu (clock, screen, free play; now stored in the game's save data). Then start, aiming and trigger (the ball) work. Test menu: key 0, select with start, execute with P1 aux1.
- **Gashaaaan!!!!**: Playable: coin (key 5), start (key 1), aiming and trigger (the ball) work. First start shows 'BACKUP DATA ERROR': press TEST (key 0) once to repair. Free play can be set in the test menu (P1 aux1 = execute).
- **Go Go Strike**: Works: bowling with the Move as trackball (direction and swing set the throw), start (key 1) and trigger (click). 4:3 picture with black bars (gamescope).
- **HALO Fireteam Raven**: Fully working: lightgun, buttons and rumble.
- **Luigi's Mansion Arcade**: Works: title, attract (free play), start (key 1), aiming the vacuum and sucking with the trigger; test menu with key 0, select switch on aux1. Loading takes about 45 s; press start once the attract mode runs.
- **Rabbids Hollywood Arcade**: Works without the TeknoParrot UI: lightgun, coin (four times) and start, rumble when shooting. Not yet played with the real Move buttons.
- **Terminator Salvation**: Works full screen (through gamescope). Aiming and shooting work well, also through the I/O bridge (shared memory route).
- **Time Crisis 5**: Playable through the I/O bridge (shared memory): aiming, trigger and left pedal (middle button) work; coin = the Move button for key 5 (the engine sends Shift+T to the game). Shoot '1-player solo' in the menu. The right pedal has not been checked on its own yet.
- **Tomb Raider Arcade**: Works without the TeknoParrot UI: lightgun, reload, coin and start, rumble when shooting. Not yet played with the real Move buttons.
- **Transformers: Shadows Rising**: Playable with the Move: the engine acts as the gun board of the game (hid-board route). Start (key 1 or Move Start), trigger and aiming work; the first load takes about 3.5 minutes ("SYSTEM INITIALIZING"). No coin needed (free play). The gun calibration screen, the test menu and the levers (attack/defend) have not been tried yet. Do not restart the engine while the game runs.

## Not working yet (17)

These games do not start, or are blocked by something RavenGun does not work around (for example copy protection). They are listed so you know what to expect; they may start working in a later version.

- **Gaia Attack 4**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Galaga Assault**: Does not start yet: font files are missing from the dump. Parked.
- **Ghostbusters**: Does not start yet: the loader rejects the game path (a file seems to be missing from the dump). Parked.
- **Haunted Museum**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Haunted Museum II**: Does not start yet: no picture. The Wine crash (MXCSR=0 caused by the loader) is worked around (RG_LOADER_FPFIX); the game runs through the JVS chain but stays black (no 0x20 polling). Parked; see games-status.md, eleventh round.
- **Infinity Blade FX**: Does not start yet: UE3 starts (needs Wine Mono and a first-run dialog for TeknoParrot.dll), then a white or black picture; the log stops at 'Kaboom: InitLib() failed'. Parked.
- **Jurassic Park Arcade**: Does not work yet: the game stops shortly after starting (crash in the ElfLdr2 layer). Parked.
- **Let's Go Safari**: Does not start yet: the game waits without a window for a Sega network service on 127.0.0.1:40100 and never opens its JVS port. The engine can already act as its JVS board (jvs route). Parked.
- **Lethal Enforcers 3**: Does not start yet: the game crashes when it opens a .m1v movie; Wine has no 32-bit GStreamer (lib32-gstreamer plus an MPEG-1 decoder), which the SteamOS repos do not offer. Parked.
- **Music GunGun 2**: Does not start yet: the Wine crash is worked around (RG_LOADER_FPFIX), but the game shows its own 'Direct3D initialisation failed' window (also with DXVK) and does not open the JVS pipe. Parked.
- **NERF Arcade**: Partly works: the game starts and shows its attract mode, but start and trigger do not arrive yet.
- **Omatsuri Yasan - Ganso Takoyaki**: Does not start yet: white window (1024x768), the game never creates a D3D device; the core makes an online activation request at start. Parked.
- **Omatsuri Yasan - Kingyo Sukui**: Does not start yet: the game exits with code 8 after scanning all drive letters for rqrdlist.rqr and campaign*.bmp. Parked.
- **Otomedius Excellent**: Does not start yet: white window; the game looks for an ACIO micom on COM2 ('COM Port 2 Open Error') and goes no further. Parked.
- **Pac-Man Chomp-Mania**: Does not start yet: black screen (same problem as Jurassic Park). Parked.
- **Pigs Might Fly**: Does not start yet: the start program is packed and fails under Wine. Parked.
- **Raccoon Rampage**: Partly works: picture, start, coin and trigger. The crosshair does not follow yet (the game wants its own calibration; its Setting Menu is not reachable yet).
