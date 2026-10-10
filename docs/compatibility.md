# Game compatibility

<!-- Generated from packaging/games-notes.json by scripts/gen-compatibility.py. Do not edit by hand. -->

This list shows what has been tried so far, on one setup (a Steam Deck with a PS Move ZCM1 and a PS3 Eye). It is a snapshot, not a promise: arcade games differ a lot, and some depend on the TeknoParrot runtime or on your own dump.
RavenGun ships no games. You need your own legally obtained copies.

## Playable (7)

These games start from RavenGun and can be played with the lightgun. Some are not yet confirmed with a real PS Move in every detail; the note says what was tested.

- **Go Go Strike**: Works: bowling with the Move as trackball (direction and swing set the throw), start (key 1) and trigger (click). 4:3 picture with black bars (gamescope).
- **HALO Fireteam Raven**: Fully working: lightgun, buttons and rumble.
- **Rabbids Hollywood Arcade**: Works without the TeknoParrot UI: lightgun, coin (four times) and start, rumble when shooting. Not yet played with the real Move buttons.
- **Terminator Salvation**: Works full screen (through gamescope). Aiming and shooting work well, also through the I/O bridge (shared memory route).
- **Time Crisis 5**: Playable through the I/O bridge (shared memory): aiming, trigger and left pedal (middle button) work; coin = the Move button for key 5 (the engine sends Shift+T to the game). Shoot '1-player solo' in the menu. The right pedal has not been checked on its own yet.
- **Tomb Raider Arcade**: Works without the TeknoParrot UI: lightgun, reload, coin and start, rumble when shooting. Not yet played with the real Move buttons.
- **Transformers: Shadows Rising**: Playable with the Move: the engine acts as the gun board of the game (hid-board route). Start (key 1 or Move Start), trigger and aiming work; the first load takes about 3.5 minutes ("SYSTEM INITIALIZING"). No coin needed (free play). The gun calibration screen, the test menu and the levers (attack/defend) have not been tried yet. Do not restart the engine while the game runs.

## Not working yet (20)

These games do not start, or are blocked by something RavenGun does not work around (for example copy protection). They are listed so you know what to expect; they may start working in a later version.

- **Gaia Attack 4**: Does not start yet: black screen under Wine after the I/O handshake. Parked.
- **Galaga Assault**: Does not start yet: font files are missing from the dump. Parked.
- **Gashaaaan Refill!!!!**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Gashaaaan!!!!**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Ghostbusters**: Does not start yet: the loader rejects the game path (a file seems to be missing from the dump). Parked.
- **Haunted Museum**: Does not start yet: black screen under Wine after the I/O handshake. Parked.
- **Haunted Museum II**: Does not start yet: black screen under Wine. Parked.
- **Infinity Blade FX**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Jurassic Park Arcade**: Does not work yet: the game stops shortly after starting (crash in the ElfLdr2 layer). Parked.
- **Let's Go Safari**: Does not start yet: the game waits without a window for a Sega network service on 127.0.0.1:40100 and never opens its JVS port. The engine can already act as its JVS board (jvs route). Parked.
- **Lethal Enforcers 3**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Luigi's Mansion Arcade**: Does not start yet (stops at once or crashes at start). Parked.
- **Music GunGun 2**: Does not start yet: the game reports that Direct3D failed to start. Parked.
- **NERF Arcade**: Partly works: the game starts and shows its attract mode, but start and trigger do not arrive yet.
- **Omatsuri Yasan - Ganso Takoyaki**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Omatsuri Yasan - Kingyo Sukui**: Does not start yet: the 32-bit runtime core stops the game under Wine. Parked.
- **Otomedius Excellent**: Does not start yet: the game stops after a few seconds without a picture. Parked.
- **Pac-Man Chomp-Mania**: Does not start yet: black screen (same problem as Jurassic Park). Parked.
- **Pigs Might Fly**: Does not start yet: the start program is packed and fails under Wine. Parked.
- **Raccoon Rampage**: Partly works: picture, start, coin and trigger. The crosshair does not follow yet (the game wants its own calibration; its Setting Menu is not reachable yet).
