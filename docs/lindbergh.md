# De Lindbergh-route

*RavenGun, 2026-10-14. Hoe de Sega Lindbergh-games native uit de app starten: een dubbelklik, de daemon, bwrap, de game-ELF met
`lindbergh.so` in LD_PRELOAD, en JVS naar het bord van de daemon. Geen TeknoParrot, geen Wine, geen shellketen.*

## 1. Wat een Lindbergh-game nodig heeft

Een Lindbergh-game is een gewone 32-bits Linux-ELF (`ET_EXEC`, `/lib/ld-linux.so.2`). Hij draait met de systeemlader; er is geen eigen
ELF-lader nodig (anders dan bij de Raw Thrills-dumps zonder sectiekoppen, `docs/elfldr2-route.md`). Wat ontbreekt is de kast:

- **het baseboard** (`/dev/lbb`): daarachter zitten de JVS-lijn naar het I/O-bord, de sense-lijn, het serienummer en de flash;
- **de EEPROM** (`/dev/i2c-0`) en het **security board** (dipswitches, de kleur van de kast);
- **de dongle** (`amDongle*` in de ELF) en de controles op de hardware (`/proc/cpuinfo`, PCI-id's, een NVIDIA-kaart, een Intel-CPU:
  `__intel_new_proc_init_P`);
- **de bibliotheken van het systeem**: `libsegaapi.so` (geluid), `libkswapapi.so`, `libposixtime.so`, en de NVIDIA-drivers van 2006
  (`drv/nvidia` in de dump). De shaders zijn NV-Cg of ARB-programma's met NVIDIA-uitbreidingen; op Mesa moeten ze gepatcht of opnieuw
  gecompileerd worden.

### TeknoParrot

TeknoParrot heeft twee wegen, beide gesloten en beide via Wine (gemeten in de open TeknoParrotUI-bron,
`TeknoParrotUi.Common/GameLaunch/GameSession.cs` en `GameLaunchArguments.cs`): `EmulatorType=Lindbergh` start `TeknoParrot\BudgieLoader.exe`
(de oude Windows-ELF-lader met Regal als OpenGL-laag, `REGAL_LOAD_GL=opengl32.dll`, `TEA_DIR`), `EmulatorType=ElfLdr2` start
`ElfLdr2\BudgieLoader.exe` (een Cygwin/msys-programma dat de ELF in zijn eigen proces laadt, `docs/elfldr2-route.md`). De emulatie van
baseboard, JVS en EEPROM zit in `TeknoParrot.dll`; daar is geen bron van. Niets daarvan is bruikbaar voor een native route.

### lindbergh-loader

lindbergh-loader (github.com/lindbergh-loader/lindbergh-loader) doet hetzelfde native, als LD_PRELOAD-bibliotheek: `lindbergh.so` haakt
`open`/`ioctl`/`read`/`write` van `/dev/lbb`, `/dev/i2c-0`, `/dev/ttyS*` en `/proc/...`, emuleert baseboard, EEPROM en security board,
patcht per game de dongle, de dipswitches en de CPU-controle op vaste adressen, en patcht of hercompileert de shaders voor Mesa. Hij
herkent de game aan een CRC-32 van 0x4000 bytes vanaf het uitvoerbare segment (`hook.c`, `dl_iterate_phdr`); alle patches hangen aan die ID.

**Licentie: CC BY-NC-SA 4.0, niet verenigbaar met de GPL-3.0-or-later van RavenGun.** Besluit: niet in de repo, wel gebouwd op de machine
van de eigenaar, net als OpenParrot (`third_party/lindbergh-loader/README.md`, `THIRD_PARTY.md`). `scripts/build-lindbergh-loader.sh`
haalt de vastgepinde bron op en bouwt hem in een podman-container (Ubuntu 22.04 i386) naar `~/.local/share/ravengun/lindbergh-loader/current`,
met de i386-bibliotheken die SteamOS niet heeft (SDL3, libstdc++5, ncurses5, libXmu) en `libCg.so` 2.0 uit de bron. De starter
`lindbergh` van de bron wordt niet gebruikt: de engine doet zijn werk.

## 2. Gemeten op de dumps (NAS, 2026-10-14)

Spel-ID zoals `lindbergh.so` hem berekent (`lindbergh_game_id()` in `src/ravengun/launch/native.cpp`, dezelfde som), tegen `config.h` van
lindbergh-loader; "schoon" = de CRC van de hele file staat in de lijst `cleanElfCRC32` van de starter.

| Titel | Profiel | ELF | Spel-ID | lindbergh-loader | Schoon | Bord |
|---|---|---|---|---|---|---|
| The House of the Dead 4 Special | `HOTD4SP` | `elf/hod4M.elf` | `0x7d6ab3e3` | HOTD4 Special Rev B (DVP-0010B) | ja | Lindbergh (Type 3) |
| The House of the Dead 4 | `HOTD4` | `elf/hod4M.elf` | `0xe213414d` | HOTD4 Rev C (DVP-0003C) | ja | Lindbergh |
| The House of the Dead EX | `HOTDEX` | `elf/hodexRI.elf` | `0x20115a92` | HOTD EX (DVP-0063) | nee | Lindbergh |
| Rambo | `Rambo` | `elf/ramboM.elf` | `0x048f49dd` | Rambo (DVP-0069) | nee | Lindbergh |
| Too Spicy | `2Spicy` | `elf/apacheM.elf` | `0x46bb306e` | Too Spicy (DVP-0027A) | nee | Lindbergh |
| Ghost Squad Evolution | `GSEVO` | `vsg_l/vsg` | `0x455edaec` | GSEvo (DVP-0029A) | nee | LindberghType1 |
| Primeval Hunt | `Primevil` | `prog/main.exe` | `0x6868215c` | Primeval Hunt (DVP-0048A) | nee | Lindbergh |
| Star Trek Voyager | – | `trek.e4` | `0x29632402` | onbekend | – | – |

- **HOTD4 Special is de eerste titel**: schone ELF, door lindbergh-loader op alle vier de GPU-families als werkend opgegeven, geen
  libCg nodig (alleen `cacheModedShaderFiles`), een gewone lightgun-game op het standaard Type 3-bord.
- **Rambo**: de bibliotheek wijst `ramboD.elf` aan (ID `0x98eacb5f`, onbekend: de laag zou niets patchen en de game zou op de dongle
  vastlopen). Het profiel start `ramboM.elf`; `plan()` weigert elke andere ELF met de melding "verkeerde ELF".
- **HOTD4**: de map "The House of the Dead 4" is Rev C, geen Special. De bibliotheek koppelt hem aan het sjabloon HOTD4SP; tot dat
  in de bibliotheek naar HOTD4 wijst, start een dubbelklik op die map de Special.
- **Niet schoon** (HOTD EX, Rambo, Too Spicy, GSEvo, Primeval Hunt): de ID klopt (de eerste 16 KiB code zijn gelijk), maar de file is al
  gewijzigd (`patches/<Game>/BESCHERMING-STATUS.md`: dongle-controle geforceerd). De patches van de laag landen dan op een gewijzigd
  beeld; meestal op dezelfde plekken en met hetzelfde doel, maar dat is niet gemeten.
- **Primeval Hunt**: `main.exe` is een i386-ELF, geen Windows-programma.
- **Star Trek Voyager** is geen Lindbergh-game (Team Play-pc, `trek.e4` niet in lindbergh-loader); hij krijgt geen profiel op deze route.
- **libCg**: HOTD EX, Rambo en Too Spicy laten de laag op Mesa hun Cg-shaders opnieuw compileren met `libCg.so` versie 2.0.0.12
  (`loadLibCg()` in `patch.c`, `GPUVendor != NVIDIA_GPU`). GSEvo krijgt een ARB-shaderpatch, HOTD4/HOTD4SP en Primeval Hunt alleen
  gewijzigde shaderbestanden.

## 3. De route zoals gebouwd

Het profiel staat in `patches/HOTD4SP/native.json` (docs/native-profiel.md), met een blok `lindbergh`:

```json
{
  "enabled": true, "arch": "i386", "launcher": "gamescope", "route": "jvs",
  "lindbergh": {"id": "0x7d6ab3e3", "ini": "@patches/HOTD4SP/lindbergh.ini", "jvs": "passthrough", "libcg": false},
  "mounts": [{"src": "@nas/The House of the Dead 4 Special", "dst": "/lb", "mode": "overlay"}],
  "libdirs": [{"src": "@lib32", "dst": "/rg/lib32"}],
  "env": {"MESA_LOADER_DRIVER_OVERRIDE": "zink", "LIBGL_KOPPER_DRI2": "1"},
  "exe": "/lb/elf/hod4M.elf", "cwd": "/lb/elf", "home": "/lb/elf", "res": "1024x768"
}
```

`load_profile()` controleert het blok (ID-vorm, `jvs` is `passthrough` of `emulate`, `passthrough` vraagt `route: jvs`, i386, geen
`loader`). `plan()` doet daarna, bovenop de gewone native route (`src/ravengun/launch/native.*`):

1. **De laag**: `~/.local/share/ravengun/lindbergh-loader/current` (of `$RAVENGUN_LINDBERGH`) moet `lindbergh.so`, `libsegaapi.so`,
   `libkswapapi.so`, `libposixtime.so` hebben (en `libCg.so` bij `libcg`); anders "Lindbergh-laag onvolledig ... bouw met
   scripts/build-lindbergh-loader.sh". De map gaat alleen-lezend naar `/rg/lindbergh`, vooraan in `LD_LIBRARY_PATH`, gevolgd door de
   `libdirs` en, zoals de starter van lindbergh-loader, de map van de game, `lib/` en `../lib` (`librnalindbergh_jr.so` e.d.).
   `LD_PRELOAD=/rg/lindbergh/lindbergh.so` (plus de `preload` van het profiel). `LINDBERGH_LOADER_CURRENT_DIR=/rg/lindbergh` (daar
   zoekt de laag `libCg.so`).
2. **De ELF**: de spel-ID van de exe op de NAS wordt nagerekend; een andere ELF start niet.
3. **lindbergh.ini**: `patches/<Game>/lindbergh.ini` (de gebruikerslaag gaat voor) met de sleutels die bij de route horen erin gezet:
   `[Display] WIDTH/HEIGHT` uit `res`, `FULLSCREEN = true`, `[Emulation] EMULATE_JVS = false` en `[Paths] JVS_PATH = "/dev/ttyRG0"`,
   bij `libcg` `[Paths] LIBCG_PATH`. Het resultaat wordt bij elke start geschreven naar
   `~/.local/share/ravengun/native/<Game>/lindbergh.ini` en als `/rg/lindbergh.ini` getoond (`LINDBERGH_CONFIG_PATH`). EEPROM en SRAM
   (`eeprom.bin`, `sram.bin` in de map van de game) landen in de lokale schrijflaag van de overlay; de NAS blijft onaangeroerd.
4. **JVS**: `--dev-bind @jvs-pty /dev/ttyRG0`. De pty bestaat pas als de route jvs gestart is; `run_native()` start eerst de route
   (`start_route(profile, "jvs")`: de daemon wordt het bord uit `[Input] Board` van `game.ini`) en vult dan met `resolve_jvs()` het doel
   van `$XDG_RUNTIME_DIR/ravengun/jvs-com` in (`/dev/pts/N`). bwrap kan die symlink zelf niet volgen: na zijn pivot_root zou `/dev/pts/N`
   de eigen devpts van de sandbox zijn.

### JVS: de daemon is het I/O-bord

Op een Lindbergh gaat JVS niet over een seriële poort maar door het baseboard: de game schrijft een volledig JVS-pakket (sync, knoop,
lengte, gegevens, som) in het gedeelde geheugen van `/dev/lbb` en vraagt het antwoord op. Met `EMULATE_JVS = false` schrijft
`lindbergh.so` dat pakket byte voor byte naar `JVS_PATH` en leest een leesdraad het antwoord (`passthrough.c`); de sense-lijn volgt de
`F0`/`F1` die erdoor gaan. Aan de andere kant van de pty zit `JvsDialect` van de daemon (`src/ravengun/iobridge/jvs*`), dezelfde
JVS-kern als bij Let's Go Island. Zo komt alle invoer (Move, muismodus, gamepad, munt, test) uit één engine, met het
kalibratievenster in `packaging/jvs-boards.json`.

De borden (`packaging/jvs-boards.json`):

- **`Lindbergh`**: het Type 3-bord 837-14572 met de waarden die lindbergh-loader voor deze games emuleert (`jvs.c`, `SEGA_TYPE_3`:
  versies 0x13/0x30/0x10, 2 spelers x 14 schakelaars, 2 munten, 8 analoge kanalen van 10 bit, 20 uitgangen). Kanaal 0..3 = P1 X, P1 Y,
  P2 X, P2 Y, 0 = links/boven. Trigger = BUTTON_1 (byte 0 bit 1), herladen = BUTTON_2 (byte 0 bit 0), gun-knop BUTTON_3, actieknop
  BUTTON_4.
- **`LindberghType1`** (GSEvo): het Type 1-bord 837-13551 (`SEGA_TYPE_1`: 0x11/0x30/0x10, 13 schakelaars, 8-bits assen, 6 uitgangen).

Herladen: een Sega-gun meldt "naast het scherm" als BUTTON_2. Het nieuwe signaal **`pN.offscreen`** (`io_state.cpp`) is de
herlaadknop of een geldige aim buiten het scherm; zo herlaadt de speler door naast het scherm te richten, zoals lindbergh-loader het met
de muis doet.

**De patch op de doorgifte.** De leesdraad van lindbergh-loader telt de lengte van een antwoord in ruwe bytes (lengtebyte + 3). Een
escape (`D0` + waarde - 1) maakt het pakket een byte langer, dus elk antwoord met `D0`/`E0` in de gegevens of de som kwam afgekapt
binnen; bij een lightgun-as van 0xD0xx (een gewone plek op het scherm) en in 1 op 128 sommen. `0001-passthrough-count-escaped-bytes.patch`
telt de escapes mee. Model en meting: `lindbergh_reader()` in `tests/test_jvs.cpp` (as 0xD0D0: zonder patch twee bytes te kort, met patch
heel). Zonder de patch zou de game af en toe een pakket zonder som krijgen.

### GPU en drivers

De Deck-klasse AMD-GPU met Mesa. Twee wegen, beide met de patches van het Mesa-pad van de laag:

- **zink (standaard, de voorkeur van de eigenaar: Vulkan)**: `MESA_LOADER_DRIVER_OVERRIDE=zink` en `LIBGL_KOPPER_DRI2=1` (wat de starter
  van lindbergh-loader met `--zink` zet). OpenGL gaat via RADV. Zink meldt `GL_VENDOR` "Mesa"; de laag zou de GPU dan als onbekend zien.
  Daarom zet elke `lindbergh.ini` `GPU_VENDOR = 2` (AMD): dezelfde patches als op radeonsi, en geen verborgen SDL-venster om de
  leverancier te meten.
- **radeonsi**: de twee env-sleutels uit het profiel halen. Dit is de weg die lindbergh-loader als "AMD (Mesa)" opgeeft.

De NVIDIA-drivers van de dump (`drv/nvidia`) worden niet gebruikt: ze staan niet in het zoekpad. `libGL.so.1` komt van het systeem
(i386-Mesa), `libGLU.so.1` en `libglut.so.3` uit `@lib32`.

## 4. Per titel op de bank

Voor elke titel: eerst `scripts/build-lindbergh-loader.sh` (eenmalig), daemon herstarten, dubbelklik in de app. Logboeken:
de daemon (`journalctl --user -u ravengund`), de start (`~/.local/state/ravengun/logs/native-<Profiel>-<tijd>.log`, met daarin ook de
uitvoer van de game en van `lindbergh.so`).

Wat het logboek van de start moet tonen (allemaal titels):

- `lindbergh: spel-ID 0x........ (/lb/...)` en `JVS: /dev/ttyRG0 in de sandbox = /dev/pts/N (de pty van de IO-brug, ...)`;
- `geschreven: /rg/lindbergh.ini (...)` en de identiteitsregels `lindbergh-laag: .../lindbergh.so sha256 ...`, `lindbergh-ini: ...`;
- van de laag zelf: `SEGA Lindbergh Emulator`, `GAME:` met de naam uit de tabel hierboven (niet `Unknown game`; `Unsure what game
  with CRC` = verkeerde ELF), `GPU VENDOR:  AMD`, en `SERIAL: starting thread.` (de JVS-doorgifte draait). Fout:
  `Failed to open '/dev/ttyRG0' for JVS`.

JVS bewezen: in het daemonlogboek `IO-brug: jvs voor <Profiel> (1 bord(en), lijn .../jvs-com)` en `JVS:`-regels per nieuw soort pakket
(reset `F0`, adres `F1`, identificatie `10`, functies `14`, schakelaars `20`, analoog `22`); `ravengunctl iobridge status --json` toont
`frames`, `switch_polls` en `analog_polls` die oplopen, `addressed: true` en `bad_sum: 0`.

GPU bewezen: `/proc/<pid van de game>/maps` bevat `libvulkan_radeon.so` en `libgallium-*.so` (zink); zonder de env-sleutels alleen
`libgallium` (radeonsi). Laadbewijs van de laag: `lindbergh.so` en `libsegaapi.so` uit `/rg/lindbergh` in dezelfde maps.

| Profiel | Wat drukken | Let op |
|---|---|---|
| `HOTD4SP` (eerst) | munt, START, schieten, naast het scherm richten = herladen | De gun-kalibratie staat in de EEPROM (`patches/HOTD4SP/seed/eeprom.bin`, door het testmenu van de game zelf gemaakt, gemeten 2026-10-22: binnen 2 px in x en 4 px in y over het hele scherm; zonder is de standaardkalibratie van de game tot 3% van de breedte scheef). Het testmenu is een apart programma (`elf/hod4testM.elf`, spel-ID `0xae47a9fc`): TEST in het spel doet niets. Wil je opnieuw kalibreren, zet dan in de gebruikerslaag een `patches/HOTD4SP/native.json` met `exe` en `lindbergh.id` van dat programma; SERVICE (F3) stapt, TEST (toets 0) kiest, GUN CALIBRATION SETTING > PLAYER1/PLAYER2 GUN ADJUSTMENT (de marks beschieten, dan TEST om vast te leggen), GUN MARK CHECK toont waar de game denkt dat de gun wijst. De EEPROM blijft in de schrijflaag. Geluid: `libsegaapi.so` van de laag (FAudio op SDL3/Pulse). |
| `HOTD4` | idem | Start alleen als het bibliotheekitem naar HOTD4 wijst. |
| `HOTDEX` | munt, START, schieten | `Cg`-hercompilatie: kijk naar shaderfouten van de laag in het logboek; eerste start duurt langer (shadercache). |
| `Rambo` | munt, START, schieten, herladen, gun-knop (grenade) | `ramboM.elf`; `RAMBO_GUNS_SWITCH` in `lindbergh.ini` als de game één gun ziet. |
| `2Spicy` | munt, START, schieten, herladen | Hercompilatie met libCg. Bibliotheekitem "Too Spicy" stuurt profiel 2Spicy. |
| `GSEVO` | munt, START, schieten, naast het scherm = herladen | Type 1-bord (8-bits assen). |
| `Primevil` | munt, START, schieten, gun-knop | Eén scherm (`PRIMEVAL_HUNT_SCREEN_MODE = 1`); het aanraakscherm ontbreekt bewust. |

### Gemeten op de bank (2026-10-15)

| Profiel | Uitkomst |
|---|---|
| `Rambo` | **werkt**: dubbelklik, attract, munt, START, 5,5 min gespeeld in muismodus (zink). Gun calibration, measured 2026-10-22 (headless, 1280x768): the game's default calibration (identical in the write layer and in a fresh `eeprom.bin`: P1 `24 14 7c 88 e6 ed`, P2 `1f 1e 86 82 ee e6`, not symmetric and different per player) put the aim off by -118..+97 px in x (up to 9.2% of the width) and -71..+33 px in y (9.2% of the height), P2 worse than P1 (CHECK in the game's own CONTROLLER ADJUSTMENT, 13 points per player; the cross clips at the screen edge so the outer corners read short). The game's own P1 and P2 adjustment (marks at 32 px from the edges, fired with output-button trigger) gives both players `0a 06 80 80 f9 f5` and every point within 5 px in x and 3 px in y (0.4%). The test menu is the same `ramboM.elf` started with the argument `-t` (no separate test program; TEST in the running game exits it): put a user-layer `patches/Rambo/native.json` with `"args": ["-t"]`; SERVICE = F3 steps, the START keys (1 and 2) step the cursor of the player panel, the trigger selects. The calibration is in `eeprom.bin` at 0x1000 (a second copy at 0x1080; only 0x1000-0x10b3 differs from the default); `patches/Rambo/seed/eeprom.bin` carries it into a fresh write layer, used only while the write layer has no `eeprom.bin`. |
| `GSEVO` | **werkt** na de extra bibliotheken hieronder. Gun calibration, measured 2026-10-22 (headless, 640x480): the daemon used to send the Type 1 board's 8-bit axes as value*257, and the game misread the non-zero low byte (the GUN ADJUSTMENT samples jumped by 640 at the screen centre, INPUT TEST showed noise). Boards with `analog_bits` <= 8 (only `LindberghType1`) now send `[value, 0]` like lindbergh-loader; INPUT TEST then shows the 8-bit value and the samples are linear (LEFT -305, RIGHT 302, TOP 225, BOTTOM -223, CENTER 0,0 in screen pixels from the centre). The game's default calibration (owner's `eeprom.bin` = fresh default) was then off by up to 95 px in x (15%) and 44 px in y (9%) on the GUN ADJUSTMENT CURSOR; calibrated at the five marks for P1 and P2 it is within 1.2 px in x and 2.0 px in y at 15 points per player, and bullet holes in play land within 2.2/2.9 px of the aim for both players; off-screen + trigger reloads. The test menu is the same `vsg` started with `"args": ["-t"]` (a full user-layer copy of `native.json`; TEST in the game only quits it; SERVICE = F3 steps, TEST = key 0 selects, the trigger samples a mark, TEST stores). The calibration is in `eeprom.bin` 0x1000-0x107f (copy at 0x1080) and ships as `patches/GSEVO/seed/eeprom.bin`, used only while the write layer has no `eeprom.bin`. 1 of 4 starts crashed (lindbergh.so 'Skipping SEGFAULT' loop at 24 s, the first start after a `-t` session); the next start worked. |
| `Primevil` | **werkt** na de extra bibliotheken; het aanraakscherm ontbreekt bewust (het spel kiest op de timer). Richting gemeten 2026-10-22 in het testmenu (`"args": ["-t"]`, GUN ADJUSTMENT > CHECK): 13 punten per speler binnen 1,5 px in x en 2,5 px in y (0,5% van het scherm) met de fabriekskalibratie van de game; niets te kalibreren |
| `2Spicy` | **werkt** met `stackalign` (zie hieronder) en `libsndio.so.7`; 5,5 min gespeeld. Gun calibration, measured 2026-10-22 (headless, 1280x768, `apachetestM.elf`, game ID `0x6510b2e6`, GUN ADJUSTMENT > CHECK shows an orange reticle for P1 only): the default calibration (TOP 10, LEFT 10, CENTER 80/80, RIGHT f0, BOTTOM f0; identical in the write layer and in a fresh `eeprom.bin`) was off in proportion to the distance from the centre: 0.5,0.5 0,0 px; 0.3,0.3 -22,-7; 0.15,0.2 -38,-11; 0.1,0.1 -44,-14; 0.9,0.9 +42,+13; 0.85,0.8 +38,+10 (up to 44 px = 3.4% of the width and 14 px = 1.9% of the height). Aiming the game's own adjustment at its five marks gives TOP 0a, LEFT 06, CENTER 80/80, RIGHT f9, BOTTOM f5 and every point within 2 px in x and 1 px in y. The menu is a separate program: put a user-layer `patches/2Spicy/native.json` with that exe and ID; SERVICE (F3) steps, TEST (key 0) or the trigger selects, EXIT with TEST stores the values (CANCEL discards). The calibration lives in `eeprom.bin` at 0x1000 (copy 0x1080); `patches/2Spicy/seed/eeprom.bin` carries it into a fresh write layer, used only while there is no `eeprom.bin`. |
| `HOTD4SP` | playable with `stackalign` and `/var/tmp` (2026-10-21, headless): the ride's free-play flow runs by itself, START on the safety screens starts a game, GAME OVER returns there; the right picture needs patch 0007 (see `patches/HOTD4SP/game.ini`) |
| `HOTD4` | runs with `stackalign`; the attract is correct. The in-game picture was white until loader patches 0005 (leaked texture units) and 0006 (standard-shader varyings) fixed it (2026-10-21, see game.ini NoteEN; the notes below about the white frame are the earlier investigation). Measured 2026-10-20 (headless, through the daemon): coin and START work and start a real session (chapter 1 intro movie, 'ESCAPE / CHAPTER 1', the stage, GAME OVER, back to the attract); the gun cursor follows the aim. Gun calibration, measured 2026-10-22: the game's default calibration was off by up to 28 px in x (2.2% of the 1280 px width) and 7 px in y at the edges, the same for both players (GUN MARK CHECK, 13 points per player, in the separate test program `elf/hod4testM.elf`, game ID `0x6210582b`; the game's own TEST key does nothing). The game's own PLAYER1/PLAYER2 GUN ADJUSTMENT (three marks) brought every point within 3 px in x and 4 px in y; the calibration is in `eeprom.bin` (0x1000, copy at 0x1080) and ships as `patches/HOTD4/seed/eeprom.bin`, used only while the write layer has no `eeprom.bin`. To recalibrate, put a user-layer `patches/HOTD4/native.json` with that exe and ID (SERVICE = F3 steps, TEST = key 0 selects), see game.ini NoteEN. The white picture is the stage/video layer: the frame right after the chapter card is uniformly white (luma 255) while the 2D layer still draws (subtitle bars), and the attract's own movie and 3D scenes render fine, so it is not shader compilation. Holding TEST (key 0) makes the game exit. Evidence (headless, daemon, 2026-10-20): docs/screenshots/hotd4-coin-credit-1-title-2026-10-20.png (CREDIT(S) 1 after key 5), hotd4-chapter1-2026-10-20.png (START accepted: chapter 1), hotd4-ingame-white-2026-10-20.png (the white frame), hotd4-guncursor-aim015-2026-10-20.png and hotd4-guncursor-aim085-2026-10-20.png (the gun cursor at two test-pattern points). |
| `LGJ` | **werkt** met `stackalign` en patch 0003 |
| `LGJS` | **werkt** (ride-versie) met `stackalign` en patches 0003 en 0004; bij de start eerst START voor de ride-reset |
| `HOTDEX` | **werkt** met `stackalign`. Gun calibration, measured 2026-10-22 (headless, 1360x768, CONTROLLER MARK CHECK, 13 points per player, in the separate test program `elf/hodextestR.elf`, game ID `0xd58a0053`; the game's own TEST key does nothing): the default calibration (identical in the write layer and in a fresh `eeprom.bin`) was off by up to 30 px in x (2.2% of the 1360 px width) and 7 px in y (0.9%), growing with the distance from the centre, the same for both players: 0.5,0.5 +2,+1 px; 0.3,0.3 -14,-2; 0.15,0.2 -28,-4; 0.1,0.1 -30,-5; 0.9,0.9 +29,+7. The game's own PLAYER1/PLAYER2 CONTROLLER ADJUSTMENT (three marks) brought every point within 3 px in x and 4 px in y. The calibration is in `eeprom.bin` (0x1000, copy at 0x1080) and ships as `patches/HOTDEX/seed/eeprom.bin`, used only while the write layer has no `eeprom.bin`. To recalibrate put a user-layer `patches/HOTDEX/native.json` with that exe and ID (SERVICE = F3 steps, TEST = key 0 selects). |

**Bibliotheken.** De volledige NEEDED-afsluiting van elke ELF (objdump, recursief) moet in de sandbox te vinden zijn.
SteamOS heeft voor i386 geen `libXt`, `libSM`, `libICE`, `libbsd` en `libmd`. Die komen nu uit de bouwcontainer in de laag.
`libopenal.so.0` (uit `libs/` van lindbergh-loader) vraagt `libsndio.so.7`. Daarvoor bouwen we een eigen stub
(`third_party/lindbergh-loader/sndio-stub.c`): `sio_open` mislukt en OpenAL valt terug op Pulse/ALSA.

**Stapeluitlijning (`"stackalign": true` in het `lindbergh`-blok).** Deze games zijn gebouwd met een stapel die op 4 bytes
uitgelijnd is. De lib32-bibliotheken van SteamOS zijn met SSE gebouwd en rekenen op 16 bytes. Op Debian, waar lindbergh-loader
voor is gemaakt, is dat niet zo; daarom de waarschuwing "not in debian-like system".

- Too Spicy viel daardoor op `movaps %xmm0,0xc(%esp)` in `libGLdispatch.so.0`.
- `librg-stackalign.so` (`tools/rg-stackalign`, in de laag gebouwd) wikkelt elke geïmporteerde GL/X11/OpenAL-functie in een
  wrapper die de stapel uitlijnt.
- Hij moet **voor** `lindbergh.so` in `LD_PRELOAD` staan. Daarna komen ook de eigen aanroepen van `lindbergh.so` uitgelijnd
  binnen.

**Doorgifte (patch 0003).** Na elke leestime-out (100 ms) publiceerde de leesdraad een leeg frame als "klaar".

- Let's Go Jungle wacht 1 s tussen verzoek en lezen, en kreeg zo steeds het lege frame in plaats van ons antwoord.
- Nu gaat een compleet antwoord altijd voor. Een leeg frame ("geen antwoord", nodig na de reset) komt er alleen als er niets
  ongelezens klaarstaat.

**Ride-versies (patch 0004).** HOTD4 Special en LGJ Special sturen na een spel 0x18 of 0x19 naar de ride en wachten op 5 of 6.
De emulatie gaf daar een verkeerd antwoord op. Bij de start vraagt de ride om START voor de reset. Dat werkt alleen als de game
`/var/tmp` kan schrijven; die map krijgt elke Lindbergh-game nu blijvend per game.

**Diagnose.** Met `RG_LB_SEGV_REPORT=1` in de sandbox meldt de SEGV-handler van lindbergh.so de echte fout: eip, adres, module
plus offset en de bytes (`0002-segv-report.patch`). Normaal slaat de handler een byte over en raakt de processor uit de pas.

## 5. Open punten

- HOTD4, HOTD4 Special en HOTD EX (tabel hierboven).
- De opties uit `[Config]` van `game.ini` (TeknoParrot: FreePlay, regio, ...) gaan nog niet naar `lindbergh.ini`.
- Uitgangen (lampen, recoil) van het bord: de daemon leest `32`/`37`, maar koppelt ze voor deze borden nog niet aan de uitvoer.
- `jvs: emulate` (lindbergh.so is zelf het bord, invoer via evdev) bestaat als terugval in het profielformaat, maar geen profiel gebruikt het.
