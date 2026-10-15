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

Het profiel staat in `packaging/native-games.json`, met een blok `lindbergh`:

```json
"HOTD4SP": {
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
| `HOTD4SP` (eerst) | munt, START, schieten, naast het scherm richten = herladen | Kalibreer de guns in het testmenu als het vizier verschuift (TEST); de EEPROM blijft in de schrijflaag. Geluid: `libsegaapi.so` van de laag (FAudio op SDL3/Pulse). |
| `HOTD4` | idem | Start alleen als het bibliotheekitem naar HOTD4 wijst. |
| `HOTDEX` | munt, START, schieten | `Cg`-hercompilatie: kijk naar shaderfouten van de laag in het logboek; eerste start duurt langer (shadercache). |
| `Rambo` | munt, START, schieten, herladen, gun-knop (grenade) | `ramboM.elf`; `RAMBO_GUNS_SWITCH` in `lindbergh.ini` als de game één gun ziet. |
| `2Spicy` | munt, START, schieten, herladen | Hercompilatie met libCg. Bibliotheekitem "Too Spicy" stuurt profiel 2Spicy. |
| `GSEVO` | munt, START, schieten, naast het scherm = herladen | Type 1-bord (8-bits assen). |
| `Primevil` | munt, START, schieten, gun-knop | Eén scherm (`PRIMEVAL_HUNT_SCREEN_MODE = 1`); het aanraakscherm ontbreekt bewust. |

### Gemeten op de bank (2026-10-15)

| Profiel | Uitkomst |
|---|---|
| `Rambo` | **werkt**: dubbelklik, attract, munt, START, 5,5 min gespeeld in muismodus (zink) |
| `GSEVO` | **werkt** na de extra bibliotheken hieronder; richtvenster nog niet gekalibreerd |
| `Primevil` | **werkt** na de extra bibliotheken; het aanraakscherm ontbreekt bewust (het spel kiest op de timer) |
| `2Spicy` | **werkt** met `stackalign` (zie hieronder) en `libsndio.so.7`; 5,5 min gespeeld |
| `HOTD4SP` | draait met `stackalign` en `/var/tmp`; ride-reset met START, dan attract (spelstart: zie patch 0004) |
| `HOTD4` | draait met `stackalign`; attract klopt, maar tijdens het spel wit beeld (shaders uitgesloten); open |
| `LGJ` | **werkt** met `stackalign` en patch 0003 |
| `LGJS` | **werkt** (ride-versie) met `stackalign` en patches 0003 en 0004; bij de start eerst START voor de ride-reset |
| `HOTDEX` | **werkt** met `stackalign` |

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
