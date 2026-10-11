# Antwoorden TeknoParrot-team: ronde 23, Jurassic Park en de losse antwoorden (stand 2026-10-11)

Bron: de NAS-map `findings/` is opgeruimd. Antwoorden staan nu in `findings/QA/` en per spel in `findings/games/<Spel>/`
(dossier, `QA/vragen.md`, `QA/antwoorden.md` en kopieën van de technische documenten; de dossiers zijn gelijk aan de
oude `findings/<Spel>.md`). Nieuw sinds `antwoorden-tp-ronde5-22.md`:

| Bestand | Over |
|---|---|
| `antwoorden_ronde23.md` | vragen 67 t/m 76 (HOTD Scarlet Dawn, Rampage, Rainbow Bombergirl) |
| `antwoorden_jurassic.md` + `_aanvulling`, `_aanvulling2`, `_aanvulling3` | `jurassic_vragen.txt` (JP1 t/m JP10) |
| `antwoorden_diepere_pass.md` | vragen 2, 29, 32 en 33 opnieuw |
| `antwoorden_scarlet_dawn.md` | de opstartketen van HOTD Scarlet Dawn |
| `antwoorden_zonder_teknoparrot.md` | vraag 9: Raw Thrills-spellen draaien zonder TeknoParrot |
| `antwoorden_basisadressen.md` | waarom de GOT-koppeling van Jurassic Park niet in het bestand staat |

Niet geopend (zoals eerder besloten): `findings/dongles/`, `findings/decompiled/`, `13_opengestelde_cores.md`,
`14_openstellen.md`, `17_hasp_dongle.md` (ook de kopieën in `games/*/techniek/`) en `loaders/*/BINNENKANT.md`.

## 1. Ronde 23 (vragen 67 t/m 76)

| # | Antwoord | Actie RavenGun |
|---|---|---|
| 67 | De route met de geparkeerde entry-point is bedoeld voor Wine (bron: `OpenParrotLoader/src/main.cpp`, openbaar). `TP_REMOTETHREAD` niet zetten. Of TeknoParrotUi hem zet: niet te bevestigen, geen profielveld | Bevestigt onze standaard (`TP_REMOTETHREAD=off`) en `RG_UNSET_REMOTETHREAD=1` voor HOTDSD. Geen werk |
| 68 | HOTDSD = profiel `ALLSHOTDSD`, Unreal-build, 64-bit, RawInput; knoppen zijn bits in het 32-bits woord op positie 8. Bitindeling niet opgezocht | Geen: HOTDSD loopt met naambindingen via TeknoParrotUi. Bitindeling gevraagd (vraag 77) voor een latere directe start |
| 69 | Tweede pijp `\\.\pipe\TeknoParrotPipe` naast `teknoparrot_jvs`. Amdaemon via de SegaTools-opdrachtregel hoort bij de IDZ-titels, niet bij HOTDSD | Geen (de pijpen kenden we al) |
| 70 | Geen logschakelaar. Dat AM Daemon blijft draaien is normaal. Later (Scarlet Dawn-antwoord): `config.json` heeft `"error": {"auto_reset": true}` | Een herstart na een fout is gewenst gedrag; zie de herstartlus hieronder |
| 71 | Rampage = `WartranTroopers`, drie spelers met elk COINS, START, UP, DOWN, LEFT, RIGHT en ATTACK; plus Test Menu en Test Select. 26 bits besturing op positie 8, acht asbytes op 12 t/m 19. Bitvolgorde niet opgezocht | Indeling voor drie spelers in `patches/Rampage/`; bitvolgorde gevraagd (vraag 77) |
| 72 | De core kent `matrix64.dll` en `MATRIX.INI` niet; het ontbrekende bestand is spelgedrag | Geen |
| 73 | `.\ForceReboot`, `.\ForceRestart`, `.\Game.Run` worden door het spel gemaakt en door de core opgemerkt | Herstartlus in `launch-direct.py` (staat al als actie bij vraag 25): na het einde van het spel opnieuw starten als `ForceRestart` of `ForceReboot` bestaat. Per game aan te zetten in `[Launch]` |
| 74 | Bombergirl: `ForceMouseAsTouchInput = 1`; de knoppenlijst heeft alleen `Test`. Het spel start met een aanraking (muisklik) op de startknop op het scherm, niet met een knopbit | Proef op de tv: klik op de startknop van het aanraakscherm. Verklaart het hangen na munt en start |
| 75 | Bombergirl: `RequiresBepInEx = true`, `Patreon = true`, `UnlockAll = 0`, geen `FreePlay`-veld; "Please insert coin" klopt dus | Geen freeplay verwachten; munten gebruiken. Freeplay alleen via het testmenu van het spel zelf |
| 76 | Bombergirl: `Windowed = 0`, `SingleMonitorLayout = FakeLandscapeTouchBottom`, rotaties `Rotate90Right`, `CustomResolution = 0`. De indelingslaag is voor volledig scherm gemaakt | `Windowed = 0` laten en `DisplayMode = Fullscreen Windowed` gebruiken; onze `CustomResolution = 1920x1080` weghalen in de proef |

## 2. Jurassic Park, Pac-Man Chomp-Mania en Galaga Assault (32-bits Raw Thrills-familie)

Wat Johan meldt (JP1 t/m JP10, aanvullingen, diepere pass, basisadressen):

- Ons `game` is exact het bestand dat TeknoParrot ondersteunt (zelfde grootte en SHA-256).
- Alle drie: 32-bits ELF zonder sectiekoppen, wel `PT_DYNAMIC`; nul GL- en SDL-imports; GL via `dlsym` (GLEW statisch).
  Ongeveer zestig GOT-slots met vaste adressen uit één eerdere uitvoering (`0xb7xxxxxx`). De index in de PLT-stubs is in
  `game` en `game_original` vervangen door `0xdeadbeef`: volgens Johan het werk van de pakker van Raw Thrills.
- De functie op `0x8357df0` heet `VidStart`. Bij `GL_MAX_TEXTURE_SIZE <= 2047` roept hij `SysLockup` aan (melding "An
  uncaught lockup has occured"). Het crashadres is de val na `__stack_chk_fail`; die stub springt via een vaste GOT-slot.
- Bibliotheken (JP): SDL 1.2, libGL/GLU/glut, PhysX 2.8.1 (RPATH `/usr/lib/PhysX/v2.8.1/`), Theora/Ogg, udev, X11; OpenCV
  2.1.0 zit erin. Pac-Man en Galaga: alleen GL, X11 en libc (plus `ld-linux.so.2` in `DT_NEEDED`).
- JP: resolutie in `utils/default.cfg` (`RESOLUTION 1360 768`); de kast zette X op 1600x1200. Pistoolkalibratie als UTF-16-XML
  in `pmuser/aud/save/gun0..3Calibration.{0,1,2}.xml`, twee punten per as, bereik 0 tot 4096 (12 bits), vier pistolen.
  Diagnosemenu met kalibratie per speler (`DiagCalibrateInstructionsP1_0..3`, `P2_0..1`).

Botsing met onze eigen meting: wij zagen na een GL-aanroep met twee argumenten een esp die 8 bytes hoger stond
(`findings/JurassicPark.md`). Dat past bij een callee-pop-aanroep en verklaart zowel de verkeerde waarde bij de
`GL_MAX_TEXTURE_SIZE`-vergelijking (`[esp+0x18]`) als de mislukte canarycontrole (`[esp+0xac]`). Johans verhaal (te kleine
waarde, dan `SysLockup`, dan de canary) en het onze wijzen naar dezelfde plek, maar wij hebben meer bewijs voor de
stackverschuiving. De GL-aanroepen komen onder ElfLdr2 dus wel in Wine's opengl32 aan.

Johans voorstel (eigen preload-bibliotheek, de vaste GOT opnieuw opbouwen door bibliotheekbases te raden of uit een lopend
proces te lezen, `dlsym` onderscheppen, plus een dongle-laag) nemen we niet over. Zie `docs/decisions.md`, 2026-10-11 "32-bits
Raw Thrills-familie". De drie blijven geparkeerd. Wat wel kan: de drie draaien met de lader van TeknoParrot zelf (ElfLdr2)
zodra die ze onder Wine aankan; daarvoor staat vraag 78 open.

## 3. HOTD Scarlet Dawn: opstartketen

- Profiel: `ExecutableName = Hodzero-Win64-Shipping.exe`, `ExecutableName2 = amdaemon.exe`, `HasTwoExecutables = true`,
  `LaunchSecondExecutableFirst = true`. De kastscripts starten `amdaemon.exe` en wachten 5 seconden (`DAEMON_WAIT_SECONDS`).
- `AMDaemon/config.json`: twee spelers, Engels, 1920x1080, `error.auto_reset = true`.
- `pxGetHwInfo.ini` beschrijft de hardware-eis (ALLS UX `ACA0`, 16 GB, 17.000 MB appdata, 16 lanes).
- De spelmap bevat TeknoParrot-versies van `amdaemon.exe` en de game naast de originelen, en een `TeknoParrot/`-map voor
  de schijfletters (AMFS, D, Y).

Voor RavenGun: de directe start met twee exe's (open punt) start eerst `amdaemon.exe`, wacht 5 seconden en start dan de
game, als gegevens in `patches/HOTDSD/game.ini`. Niet overgenomen: de originele exe's terugzetten (de NAS is alleen-lezen voor
ons en dat is ook geen taak van RavenGun), `minime` met eigen PKI (vervangt keychip- en billingdiensten), de
hardwarecontrole omzeilen, en een dongle-laag. De route via TeknoParrotUi blijft de speelbare route.

## 4. "Zonder TeknoParrot" (vraag 9)

Johans plan voor de Raw Thrills-spellen bestaat uit vijf delen: preload-bibliotheek, vaste GOT herschrijven, `dlsym`
onderscheppen, dongle-laag, invoer plus schijfletters. Wat RavenGun ervan doet: de invoer (eigen I/O-laag, protocollen
uit de antwoorden) en paden (symlink-views). De GOT-reconstructie en de dongle-laag niet (zie de beslissing). Voor de
64-bits familie (Halo, Terminator, GhostBusters, Go Go Strike) blijft de route via TeknoParrot zoals hij is.

`hints.dat` (diepere pass, vraag 32): cache van patroonzoekacties uit de openbare OpenParrot-bron
(`src/Utility/Hooking.Patterns.cpp`), records van 16 bytes (FNV-1-hash, offset vanaf de modulebasis). Geen rol voor ons.

## 5. Nieuwe vragen

Ronde 12 (vragen 77 t/m 82) in `findings/QA/vragen.txt`.
