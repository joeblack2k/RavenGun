# Games in TeknoParrot: status na de tweede ronde (2026-10-09)

# Achtste ronde (2026-10-10, avond): A10 direct starten zonder TeknoParrotUi, A11 aanpassingen per game

Samenvatting: zes games starten nu standaard direct, zonder TeknoParrotUi (`scripts/play-halo.sh --direct`, of `"launch": "direct"` in `packaging/io-routes.json`; `scripts/launch-direct.py` start de lader van de runtime zelf, zie `docs/architecture.md`, "Directe start"). Bij elke proef gaf `pgrep -x TeknoParrotUi` niets. Beeld en invoer zijn gelijk aan het oude pad via TeknoParrotUi, met de invoer altijd van de engine (tp-shm of hid-board). Gemeten met schermafdrukken op de tv, `ravengunctl key 5|1|0`, `test-pattern grid --dwell MS` en `output-button trigger`. Let op bij het meten: de verbonden Move zet bij elk rapport zijn eigen knoppen terug, dus een `down` van ravengunctl duurt soms maar enkele milliseconden (herhaal de opdracht of gebruik `tap`); dat geldt voor beide paden.

Per game (direct, vergeleken met het oude pad)
- Terminator Salvation (ElfLdr2, gamescope, nu route tp-shm in plaats van evdev): gelijk. Attract schermvullend (FREEPLAY), start (missiekeuze), aim volgt het raster (vizier linksboven bij 0,1/0,1 en rechtsboven bij 0,9/0,1), trekker (munitie 60 naar 46 bij vasthouden), herladen. De opdrachtregel, werkmap, omgeving en `teknoparrot.ini` zijn byte voor byte gelijk aan wat TeknoParrotUi doet (gemeten aan een lopende sessie van het oude pad).
- Time Crisis 5 (TeknoParrot 64-bit, tp-shm): gelijk. Attract, munt (CREDIT 0/1 naar de modekeuze), schot kiest 1-PLAYER SOLO en "Play from Beginning", de tutorial start. Ook hier dezelfde argumenten en omgeving als TeknoParrotUi (inclusief `OPENSSL_ia32cap`). De game opent de JVS-pipe niet (zoals bekend).
- Transformers: Shadows Rising (TeknoParrot 32-bit, eigen prefix, hid-board): gelijk. Laadt in ongeveer 4 minuten tot de attract, start (toets 1) start het spel en "Continue?", het laserspoor volgt het raster, het bord krijgt de trekker (knoppenmasker 4). Het bekende schaalverschil van het vizier blijft (het vizier staat bij midden-aim linksboven het midden; zie de vijfde ronde).
- Halo: Fireteam Raven (ElfLdr2 64-bit, nu route tp-shm in plaats van evdev): gelijk aan het oude pad. Gun-kalibratiescherm speler 1 en 2 (TEST slaat over), attract met "Money In", munt, start (modekeuze met vizier), aim (vizier volgt het raster) en trekker (kiest een modus).
- Go Go Strike (ElfLdr2, gamescope, route tp-shm met de nieuwe indeling `GoGoStrike`): gelijk. Titelscherm 4:3, start (How to play), de spelerkeuze wisselt mee met de aim (trackball), ballenkeuze en worpen. De munt is hier Coin2 (0x8); Setup (0x4) is bewust niet gekoppeld (instelmenu). De kern meldt "Detected Linux game: Go Go Strike" en "RUNNING GoGoStrike".
- Raccoon Rampage (TeknoParrot 64-bit, DXVK-prefix, tp-shm): gelijk (deels werkend, zoals eerder): melding "guns are not calibrated", munt en start (kaartkeuze, speler 1 doet mee). Het vizier is niet opnieuw onderzocht.

Geparkeerde games, kort direct geprobeerd
- NERF Arcade: gelijk aan het oude pad, ook het beeld: in beide paden een venster met titelbalk en takenbalk zichtbaar (de eerdere notitie "schermvullend" klopt niet). Niet standaard direct (invoer kwam al niet aan).
- Gaia Attack 4 (OpenParrot, Type X2): zwart, zoals via TeknoParrotUi. Verschil: deze game spreekt echt JVS over de pipe (reset, adres, daarna identificatie 0x10); de directe start beantwoordt alleen reset en adres (verbind-vlag in byte 0, gemeten: `shm[0] 0->1`) en geen identificatie. Voor de Type X2-games ontbreekt dus een JVS-bord achter de pipe (zie `docs/todo.md`, A10); de bestaande blokkade (zwart beeld, `handle_syscall_fault`) staat daar los van.
- Gashaaaan (32-bit TeknoParrot): anders dan via TeknoParrotUi. Direct: "Failed to Load DLL via RemoteThread! (Error 4 - VirtualAllocEx failed: 0x5)" na 3 s en de game stopt meteen; via TeknoParrotUi bleef hij eerder hangen op een loader-lock. Beide: geparkeerd, niets omzeild.
- Niet direct geprobeerd: de overige 32-bit TeknoParrot-games, Pigs Might Fly, Let's Go Safari, Luigi's Mansion, Jurassic Park en de andere Raw Thrills-games (zelfde klasse als hierboven).

A11: aanpassingen per game (volledige uitvoer van de lader: `--log-full`, `TP_LOGTOFILE=1`, `WINEDEBUG=+debugstr`; samenvatting per start in `~/.local/state/ravengun/a11/`)
- Uitkomst: geen enkele kern schreef een regel over toegepaste of overgeslagen aanpassingen ("patch '...' @0x..." of "skipped: unexpected original bytes"), in geen van de kanalen: de console van de lader, het logbestand `teknoparrot_log.txt` (ElfLdr2 met `TP_LOGTOFILE=1`, in de map van de game) en OutputDebugString (alleen de eigen debuguitvoer van de games, bv. Transformers). Er is dus ook geen melding van een verkeerde gameversie. Hoe de kern die regels laat zien (een debugschakelaar?) staat niet in de open bron; vraag het team.
- Versies die wel zichtbaar zijn: BudgieLoader 1.0.0.154 (ElfLdr2), TeknoParrot.dll en TeknoParrot64.dll 1.0.0.3761, OpenParrot.dll 1.0.0.787.
- Terminator: de game toont zelf "Version: 01.25.00.US RELEASE" (build 12 september 2011), dezelfde versie die de kern kent ("Terminator 1.25 US", `findings/13_opengestelde_cores.md`); de kern meldt geen herkenning ("RUNNING ...") voor deze game.
- Go Go Strike: de kern herkent de game ("Detected Linux game: Go Go Strike", "RUNNING GoGoStrike"): versie klopt.
- Halo: geen herkenningsregel; de kern meldt alleen "Warning SHT_DYNSYM not found in section headers" bij het laden van `game`.
- Time Crisis 5: `TimeCrisisGame-Win64-Shipping.exe` versie 1.0.10897.0. Raccoon Rampage: 4.26.0.0 (Unreal 4.26). NERF Arcade: 2018.2.21.8949 (Unity 2018.2). Transformers (`Shell.exe`), Gaia Attack 4 en Gashaaaan: geen versie in het bestand (0.0.0.0).

Op de NAS veranderd in deze ronde: niets door de directe start (de gegenereerde `teknoparrot.ini` en `MINIGUN.INI` waren byte voor byte gelijk aan wat er stond; de launcher schrijft nooit naar de NAS). Gecontroleerd met `find -newer` op de gamemappen van Time Crisis 5, Transformers (`Sega/`), Raccoon Rampage, Gaia Attack 4 en Gashaaaan. In de map van NERF Arcade schreef de game zelf weer `BepInEx/LogOutput.log`, `cache/` en `config/BepInEx.cfg`, en TeknoParrotUi bij de vergelijkingsstart via het oude pad `teknoparrot.ini` (met dezelfde inhoud); geaccepteerd. Lokaal schreef de kern `teknoparrot_log.txt` in de view van Terminator en Go Go Strike en in `halo-g7-tp/halo` (alleen met `--log-full`).

# Zesde ronde (2026-10-10, middag): spoor B3 (Type X2, WartranTroopers-games, Raw Thrills)

Samenvatting: Go Go Strike is speelbaar (snelkoppeling). Raccoon Rampage en NERF Arcade starten tot in het spel respectievelijk de attract, maar missen nog aim respectievelijk invoer. De rest is geparkeerd met een oorzaak per game. Nieuwe tp-shm-indelingen (MusicGunGun2Pipe, HOTDSDPipe, FarCryPipe) en de offscreen-regel staan in `packaging/tp-shm-profiles.json` en `src/ravengun/iobridge/tp_shm.cpp` (getest, bytes exact).

Gemeenschappelijke bevindingen
- Beeld controleren: een zwarte schermafdruk betekent hier meestal dat de game echt zwart rendert; de TeknoParrot-UI staat onder de game en komt pas in beeld als de game stopt (dan zie je ook "The game exited with an error (exit code N)").
- 32-bit TeknoParrot-kern onder Wine (Gashaaaan, Gashaaaan Refill, Lethal Enforcers 3, Otomedius, Omatsuri x2, Infinity Blade FX): injectie na 3 s (`TP_POSTSTART_REMOTETHREAD_MS=3000`, de standaard voor 64-bit) laat de game vastlopen op een loader-lock (Wine: "loader_section wait timed out in thread .., blocked by .."); injectie vanaf het startpunt (`TP_ENTRYPOINT_REMOTETHREAD_MS=3000`) laadt de kern, maar de kern beëindigt het proces zelf na ongeveer 17 s met exitcode 0xDEADC0DE (Gashaaaan, Lethal Enforcers 3, Omatsuri x2, Infinity Blade FX). Gashaaaan Refill stopt met 0xC0000354, Otomedius met exitcode 0 zonder beeld. De kern schrijft geen logregel; niets daarvan is omzeild.
- OpenParrot (Taito Type X2): `TP_POSTSTART_REMOTETHREAD_MS` verhindert dat de game de JVS-pipe opent; zonder die variabele verbindt de pipe (GAME CONNECTED), maar de game blijft daarna zwart met een hoge CPU-belasting: Wine meldt eindeloos `handle_syscall_fault code=c0000005` op een vast adres in het 32-bit-deel (Gaia Attack 4 en Haunted Museum: dezelfde offset a88 in het adres), met ervoor `NtGetContextThread` op de eigen thread. Een andere Wine (GE-Proton 10 zonder eigen prefix, Kron4ek 11.19) is geprobeerd: de eerste miste vkd3d in een kale prefix, de tweede startte niet; dat is niet verder uitgezocht.
- UE4/Unity (64-bit): wined3d (OpenGL) is niet genoeg (GL_INVALID_ENUM-fouten en exit 1 bij Raccoon Rampage); DXVK uit een lokaal aanwezige GE-Proton in een eigen prefix-kopie (`scripts/tp-add-game.py --dxvk`) werkt. De gedeelde prefix is niet aangeraakt.

Per game
- Go Go Strike (PAOKAI, Allegro, ElfLdr2; route evdev, gamescope): werkt. Titelscherm, start (toets 1), ballenkeuze en een worp met de Move-beweging als trackball (7 kegels geraakt in de proef). 4:3 met zwarte randen. Snelkoppeling gemaakt. Het credit-getal in beeld is onzin (73862560/27904072) maar de game start gratis.
- Raccoon Rampage (UE4, tp-shm WartranTroopers): werkt deels. Met DXVK en een view met lege `RSGame/Content/Movies` (de intro-video's vragen een AAC-decoder die deze Wine mist; met video's stopt de game met exitcode 1) draait de game: attract, munt (toets 5), start (toets 1), kaart/wapenkeuze en gewoon spelen met schade en score; de trekker lijkt het "druk op de knop om over te slaan"-scherm over te slaan. Open: het vizier staat vast linksboven. De game meldt "The guns are not calibrated, please enter Setting Menu to calibrate!"; het Setting Menu is niet bereikt (F2 vasthouden, F3, F4 deden niets zichtbaars). `Direct Aim Mode` aan of uit maakt geen verschil. Daarom geen snelkoppeling.
- NERF Arcade (Unity, tp-shm WartranTroopers): werkt deels. Met DXVK-prefix en `WINEDLLOVERRIDES=winhttp=n,b` laadt BepInEx de plugin (log in `BepInEx/LogOutput.log`) en draait de attract schermvullend. Start (toets 1) en trekker komen niet aan, ook niet via evdev; in de tp-shm-meting zagen we de startbit na één tik weer verdwijnen. Niet uitgezocht.
- Gaia Attack 4, Haunted Museum, Haunted Museum II, Music GunGun 2 (Type X2, OpenParrot): geparkeerd, zie gemeenschappelijk. Music GunGun 2 toont bovendien een eigen foutvenster "Direct3D-initialisatie mislukt" (ook met DXVK-d3d9). Haunted Museum II: JVS verbindt, beeld zwart.
- Gashaaaan, Gashaaaan Refill, Lethal Enforcers 3, Otomedius, Omatsuri Yasan (2x), Infinity Blade FX: geparkeerd, zie 32-bit TeknoParrot-kern.
- Pigs Might Fly: geparkeerd. `Shell.exe` is ingepakt: injectie vanaf het startpunt geeft "Unhandled illegal instruction at address 0044A5F4" (zoals Luigi's Mansion), injectie na 3 s geeft "VirtualAllocEx failed: 0x5". Niet omzeild.
- GhostBusters: geparkeerd. De loader herkent de game ("Ghostbusters 1.17 US") en meldt dan "GAME PATH CHECK FAILED" voor elke indeling van de view (game in de wortel, in `g6/ghostb`, kleine letters, eigen schrijfmap). Afgeleid: de dump mist het donglebestand dat de loader bij herkende games verwacht (`fff4` bij Halo, `hasp/` bij Terminator); dat past bij de dongle-conclusie van B4.
- Galaga Assault: geparkeerd. De loader start de game (SDL), de game meldt `file cannot be loaded: images/fonts/zoinks*.tga` en `AlSeana*.tga` (staan niet in `galagafiles.txt` en niet in de dump) en crasht op EIP 0x0808957D. Niet de blokkade van Jurassic Park.
- Pacman Chomp-Mania: geparkeerd. Het starten gaat zonder melding, maar het beeld blijft zwart (zelfde klasse als Jurassic Park).

Scriptwijzigingen
- `scripts/tp-add-game.py`: bindingen voor nieuwe knopnamen (Coins, Credits Pn, Throw, Touch, Touch Press, Option, Shoulder, Proton Pack, Trackball, Test / ..., Service / ...); `--dxvk`, `--view-up N` en `--empty-dir REL`; de standaard `TP_POSTSTART_REMOTETHREAD_MS=3000` blijft staan als je `--env` of `--gamescope` meegeeft (niet voor OpenParrot).
- Daemon: F4 en F5 zijn nu volume omhoog/omlaag in de IO-brug (testmenu-"Enter" van veel indelingen).

Op de NAS veranderd door games/TeknoParrot (geaccepteerd): `teknoparrot.ini` in de gamemappen van Gaia Attack 4, Haunted Museum 1 en 2 (voor zover gestart), Music GunGun 2, Lethal Enforcers 3, Omatsuri (2x), Raccoon Rampage (`RSGame/Binaries/Win64`), NERF; `OpenParrot/GA40.CFG`, `GA41.CFG` en `.CRC` in Gaia Attack 4; `_TakoLog_B.log`/`_TakoLog_M.log` in Omatsuri Ganso Takoyaki; `BepInEx/cache`, `config`, `patchers` en `LogOutput.log` in NERF. Lokaal: eigen prefix-kopie `~/.local/share/TeknoParrotUI/prefixes/ravengun-test-dxvk`, views onder `~/.local/share/ravengun/runtime/views/`.

# Zevende ronde (2026-10-10): B4, Raw Thrills native op Linux

Samenvatting: geen enkele Raw Thrills-game draait volledig native; Halo komt het verst (zie `docs/decisions.md`, "B4: Raw Thrills native"). Alles is gemeten met `scripts/play-native.sh` (bubblewrap, NAS alleen-lezen, `--headless` zonder de tv te gebruiken). De route blijft TeknoParrot/BudgieLoader; `packaging/native-games.json` heeft per game de status en reden, en `play-halo.sh --native` start een game native als experiment.

- Halo: native startpunt. Beeld, eigen FileTest en "Welcome to Halo v03.11" werken op de NAS; de SDL-joystick wordt als gun gevonden. `halo.so` is alleen de dongle (hasp-hooks), geen invoer: de aanname dat het onderdeel het gedeelde geheugen van de daemon leest, klopt niet. De kastschakelaars (start, test, munt, service, volume) komen van het RIO2-bord; `tools/rt-input-shim` zet ze uit de IO-brug in de schakelaartabel van de game (START slaat de FileTest over: bewezen). Met een gerepareerde FMOD-stub (de oude miste de const-methoden) loopt de game door "Webcam Init" naar het gun-kalibratiescherm; TEST (300 ms) en START slaan de kalibraties over, daarna draait de attract-lus met munt ($0,25 per indruk). START start het spel daar niet reproduceerbaar (een keer wel: modusmenu, missiekeuze en introfilm), geen geluid (FMOD 1.10 ontbreekt). Aim en trekker zijn native niet getest.
- Jurassic Park, Galaga Assault, Pacman Chomp-Mania: herschreven ELF; native SIGSEGV voor `main` (sprong naar een vast adres uit de dump). Geen preload in de gamemappen. Jurassic Park laadt wel met libSDL-1.2 (sdl12-compat, i386), PhysX en theora; de dongle-bibliotheken `libhasp_linux_*.so` staan in `utils/pmrtdata`.
- Terminator Salvation: native exit 255 (de game wil een HASP-dongle); via TeknoParrot+gamescope werkt hij zoals eerder.
- GhostBusters: wil `/usr/lib/libcsv.so` (absoluut pad), `libesd.so.0` (i386) en een dongle. Go Go Strike: Allegro 4 crasht op NULL na het laden; wil `/dev/w040101_*`.
- Op de NAS veranderd: niets (alle schrijfacties van de games gaan naar `~/.local/share/ravengun/native/<PROFIEL>/`, gecontroleerd op bestanden jonger dan 90 minuten in de gamemappen).
- Voor de eigenaar: een TeknoParrot-Linux-preload met RIO2, webcam en FMOD zou native Halo mogelijk maken; vraag het team of die er is, en of er voor Jurassic Park, Galaga en Pacman een vergelijkbare vooraf te laden bibliotheek bestaat.

# Zesde ronde (2026-10-10): IO-brug stap 3, dialect jvs, Let's Go Safari

Samenvatting: de daemon kan nu een JVS-bord zijn (dialect jvs, `docs/architecture.md`); Let's Go Safari (LGS) is daarmee nog niet speelbaar, omdat de game vastloopt op iets wat eerder komt dan de JVS-poort. Time Crisis 5 blijft via tp-shm (het TeknoParrot-team bevestigt dat TC5 geen JVS gebruikt; ook onze eigen meting: 0 bytes op de pipe).

- Let's Go Safari, wat de game doet (plain Wine, eigen prefix `ravengun-test-lgs`, met en zonder TeknoParrot): na ongeveer 2 s opent hij een TCP-verbinding naar 127.0.0.1 poort 40100 (Sega amNetwork/PCP; vermoedelijk de dienst "mxsegaboot", want `mxGetHwInfo.exe` stuurt foutmeldingen daarheen), wacht dan op een bericht van de server dat niet komt en maakt nooit een venster. Een eigen luisteraar die accepteert en niets, nulbytes of "PCP" stuurt veranderde niets (tot 30 s). De TeknoParrot-kern in het proces (injectie na 300 tot 3000 ms) vangt dit niet af. Daardoor opent de game COM4 niet eens. Wat nodig is: de server van dat protocol (onbekend) of een manier waarop TeknoParrot het onder Wine oplost.
- Tips uit `antwoorden.md`, getest: (1) Proton gebruikt de bridge, plain Wine de gewone pijp: klopt voor zover gezien (onder plain Wine start de pipehelper en meldt "pipe created, waiting for game"), maar lost de hang niet op. (5) "COM4 moet in de omgeving bestaan": het exe heeft inderdaad `COM4` en de comm-API's (ons eerdere onderzoek zei ten onrechte van niet) en Wine koppelt `com4` standaard aan `/dev/ttyS3` (voor ons niet toegankelijk), dus de poort moet naar een pty; maar Wine zet die links bij elke koude start terug (de link moet dus erna), en `ln -s /dev/ttyS0` zou hier niet werken. Dit is niet de eerste oorzaak van de hang. De modemstatus (0x30 of 0x10) en `cbInQue = 0x7FFF` uit dat antwoord zijn consistent met de imports maar nog niet gemeten. (7) TC5 gebruikt geen JVS: klopt (eigen meting). (2), (3), (4) en (6) zijn niet getest in deze ronde.
- Gebouwd en getest (24 bestaande suites plus `ravengun_jvs_tests`): JVS-kern, borden in `packaging/jvs-boards.json` (LGS, en een generiek Sega-gunbord SegaGun2P voor Let's Go Island, Operation G.H.O.S.T. en dergelijke), pty-transport met symlink, `LD_PRELOAD`-hulp voor de modemstatus, route jvs in `io-routes.json` (LGS), `play-halo.sh` zet de COM-poort klaar en ruimt op, `ravengun-jvs-probe` (bord zonder daemon).
- Met Wine gemeten: een Wine-proces opent `COM4` (link naar de pty) en zijn bytes komen bij de daemon aan. Niet gemeten: lezen, `SetCommState` en `GetCommModemStatus` met een echt programma (geen Windows-compiler hier).
- Voor de eigenaar om te proberen: (a) op een echte Windows-installatie met TeknoParrot de loopback-poort 40100 opnemen terwijl LGS start (bv. met RawCap en Wireshark), of laten zien welk programma daar luistert; dat geeft het PCP-protocol. (b) De game met `TP_ENTRYPOINT_REMOTETHREAD_MS` op een nieuwere Wine proberen (de eerdere crash op het entrypoint). Daarna `scripts/play-halo.sh --profile LGS --no-wait-move` en kijken of "IO-brug" packets telt.
- Let's Go Safari is geen lightgun-game (stuurwiel, vier gekleurde knoppen, schakelaar, camerawissel, wissers, claxon, vuurwerk); het stuur komt uit de horizontale aim. De plaats van de extra schakelbits is nog niet gemeten (de testmodus `GameTestR_RingWide.exe` toont de ingangen; daarmee kan dat later).
- Welke andere games het JVS-bord kunnen gebruiken (niet gestart): Let's Go Island en Let's Go Island 3D (EmulationProfile `SegaJvsLetsGoIsland`), Operation G.H.O.S.T. (`SegaJvsGoldenGun`), Rainbow Bombergirl (`SegaJvs`) en verder de Sega-JVS-profielen `SegaJvsDreamRaiders` en `SegaJvsLetsGoJungle` krijgen volgens het onderzoek het standaardbord 837-14572 (comm 0x10, JVS 0x20, revisie 0x13, 14 schakelbits, 8 analoge kanalen: P1 X, P1 Y, P2 X, P2 Y, ...); dat is het bord `SegaGun2P`. Per game nog nodig: het kalibratievenster uit het TeknoParrot-profiel in een eigen kopie van het bord (bv. Dream Raiders 63..207 x 63..191), de plaats van de schakelbits (trekker, herladen, start) per game, een route met `board` en `com_port` (Sega Ring: COM4), een prefix-kopie met `RG_WINEPREFIX`, en wat de game verder van TeknoParrot nodig heeft (dongle, netwerkdienst zoals bij LGS). Halo: Fireteam Raven heeft volgens het onderzoek een tweede JVS-node voor speler 3 en 4: de bus kan al meerdere nodes (`boards` in de route), maar dat pad loopt bij ons via tp-shm en blijft zo. Luigi's Mansion gebruikt COM1 voor een eigen firmware-flits (geen JVS-bord van dit soort). De games met `MusicGunGun2Pipe` (Haunted Museum, Music GunGun 2) en de Taito-games gebruiken volgens het onderzoek COM2 voor JVS of een eigen pipe; niet uitgezocht.
- Veranderd buiten de repo: `UserProfiles/LGS.xml` (WineRunnerPath) en `LGS.env` (RG_WINEPREFIX), back-ups in `~/.local/state/ravengun/LGS.{xml,env}.bak-jvs`; nieuwe prefix `~/.local/share/TeknoParrotUI/prefixes/ravengun-test-lgs` (kopie van `shared/wine`, 604 MB). Op de NAS (map "Let's Go Safari"): alleen `teknoparrot.ini` is door TeknoParrot herschreven (bij elke start); verder is er in die map niets veranderd (gecontroleerd op wijzigingstijd).

# Vijfde ronde (2026-10-09, nacht): IO-brug stap 2, Transformers via het dialect hid-board

Samenvatting: Transformers: Shadows Rising is speelbaar met de Move. Oorzaak van het vorige probleem: `Transformers2.exe` leest zijn gun als eerste DirectInput-joystick (`GUID_Joystick`, bord "SAE" in de linkermap) en dat was op deze machine de PS Move zelf (Wine ziet hem via hidraw als joystick met id 0); de TeknoParrot-kern doet in dit proces niets. Nu maakt de daemon dat bord na als virtuele joystick (`packaging/hid-boards.json`, route `hid-board` in `io-routes.json`), en `scripts/wine-joyid.sh` geeft het in de prefix joystick-id 0. Meetgegevens en de leesbare functies: `docs/research/teknoparrot-io.md`, hoofdstuk 6.

- Live gemeten op de tv (`scripts/play-halo.sh --profile TransformersShadowsRising --no-wait-move`): de game laadt (ongeveer 3,5 minuut), attract met "PRESS START BUTTON / FREE PLAY"; toets 1 (start van speler 1) start het spel (log: `GAME_MSG_START_PRESSED - Player 1`, `GAME_MSG_STAGE_START`); de trekker schiet (log: score 1000, 2000, 2500; "PULL THE TRIGGER!" in de tutorial wordt afgevinkt); het wapen en het spoor van de kogels in beeld volgen het testpunt (hoek linksboven: spoor van linksboven naar het wapen; wapen verschuift mee naar de andere hoeken). Het gun-bord in de status: assen -1000..1000, knoppenmasker volgt de knoppen.
- Niet gedaan of niet gelukt: (1) het kalibratiescherm van de game en het testmenu zijn niet geopend (de testknop als joystickknop 12 en 13 deed niets; `config.ini` van de shell noemt TEST=13 en SERVICE=14 zonder te zeggen waarvan); (2) de nauwkeurigheid van het vizier over het hele scherm is niet gemeten (geen zichtbaar vizier gevonden; de game schaalt met `(1000 - waarde) * 255 / 1000`, dus de assen lopen intern 0..510 in plaats van 0..255; als het vizier te ver of te weinig beweegt, pas dan `min`/`max` van de assen in `hid-boards.json` aan of gebruik de kalibratie van de game); (3) de hendels (aanval/verdediging: tutorial "PUSH THE LEVER") zijn naar de Move-knoppen granaat en herladen gelegd maar niet gedaan; (4) tweede speler niet geprobeerd; (5) recoil naar rumble niet gedaan (de game stuurt recoil naar de shell, niet naar het apparaat); (6) het HID-bord van de shell ("Couldn't find IOBoard device" blijft in het log staan) is niet nagebootst, bewust: het heeft een IDREQUEST/UNLOCK-handdruk die lijkt op een beveiliging.
- Voor de eigenaar om te proberen: spelen met de echte Move (richten, trekker, start-knop; de standaardindeling zet start op toets 1), kijken of het vizier klopt (links/rechts en boven/onder niet omgedraaid, bereik tot de randen), en de hendels. Een Move-knopindeling voor de hendels staat in `hid-boards.json` (index 4 = granaat, index 5 = herladen). Starten: de snelkoppeling "Transformers Shadows Rising" of het commando hierboven. Daemon niet herstarten tijdens het spel: het bordapparaat verdwijnt dan en de game herstelt dat niet.
- De shell en TeknoParrot blijven nodig: `Transformers2.exe` direct starten (zonder shell) blijft op "SYSTEM INITIALIZING... LOADING.." staan (meer dan 50 s gemeten). De invoer loopt wel zonder TeknoParrot: geen evdev-binding, geen tp-shm.
- Op de NAS veranderd (door game en shell): alleen hun gewone logbestanden in `Sega/shell` en eventueel `Sega/Transformers2`; ook de proefstart zonder shell schreef daar niets. In de prefix-kopie `ravengun-test-tsr`: DirectInput-joystick-id's (waarde "Joystick Id", PS Move naar 5, het bord op 0) en een cache van het bord; back-up van de registerbestanden vóór de proef: `~/.local/state/ravengun/bak-hid/`.
- Eerder in deze ronde (aangepast, zie boven): het `/dev/uhid`-transport is gebouwd en getest tegen een nep-apparaat maar niet live gebruikt, omdat `/dev/uhid` alleen voor root is; voor Transformers is het niet nodig (uinput werkt).

# Vierde ronde (2026-10-09, avond): IO-brug (tp-shm)

Samenvatting: de daemon heeft nu een eigen IO-brug (docs/architecture.md, "IO-brug"; docs/decisions.md, "IO-brug: tp-shm"). Time Crisis 5 is hiermee speelbaar geworden; Transformers: Shadows Rising niet. De route per game staat in `packaging/io-routes.json`; `scripts/play-halo.sh --profile PROFIEL [--route evdev|tp-shm|unity-tcp]` kiest hem.

- Terminator Salvation via tp-shm (live): start (toets 1 -> Control 0x10), aim (testpunt 0,1/0,1 -> vizier op 0,10/0,10), herladen (video overslaan), trekker (munitie loopt terug) en een herstart van de daemon midden in het spel (de brug hervat en wapent opnieuw; het spel loopt door). Halo en Terminator blijven standaard op evdev; Halo en Terminator via evdev opnieuw gecontroleerd (de bytes die TeknoParrotUi schrijft komen exact overeen met de indeling in `packaging/tp-shm-profiles.json`).
- Time Crisis 5 (route tp-shm, nu de standaard): de kern zit in het gameproces en leest het bestand van de daemon. Gemeten op de tv: het vizier staat op het gevraagde punt, schieten in het menu kiest "1-PLAYER SOLO", de linker pedaal (granaat/middenknop) licht in de tutorial op en de game gaat verder, en de munt geeft "CREDIT(S) 1 / 1" en bij een tweede munt "2 / 1". Belangrijk: de munt van deze game is Shift+T op het toetsenbord (niet het gedeelde geheugen); de route `coin_keys` laat de daemon die toetsen vasthouden zolang de munt (toets 5, Move-knop driehoek) ingedrukt is. Een snelkoppeling is gemaakt (`scripts/make-game-shortcut.sh TC5 "Time Crisis 5"`). Niet apart gecontroleerd: de rechter pedaal en nauwkeurigheid van het vizier over het hele scherm. Op de NAS veranderd: niets nieuws (de game schrijft zijn eigen state; `teknoparrot.ini` bestond al).
- Transformers: Shadows Rising (route tp-shm): nog niet speelbaar. Gemeten dat het niet aan de brug ligt: de bytes zijn goed (ook TeknoParrotUi schrijft in een gewone sessie exact start 0x01, trekker 0x02, hendel 0x10, munt 0x40, aim als complement op offset 12 en 16), en de TeknoParrot-kern zit alleen in `Shell.exe`, niet in `Transformers2.exe`, dat zelf `hid.dll`, `setupapi.dll` en `dinput8.dll` laadt. `ioboard - <datum>.log` meldt in elke sessie "Couldn't find IOBoard device", ook in de twee sessies waarin de start wel doorkwam; X11-toetsen en -klikken doen niets. De eerdere conclusie "de aim komt niet door" was een meetfout: tijdens een testpunt neemt de live Move het na enkele milliseconden weer over (gebruik `test-pattern grid --dwell MS`, dat houdt de punten vast). Twee losse sessies van Transformers stopten met "Unexpect Game termination" kort na de start (de game of de shell viel uit, niet reproduceerbaar). Vervolgstap: een virtueel HID-IO-bord (uhid) nadat VID/PID en rapportindeling zijn gemeten.
- Overige games: geen wijziging (Jurassic Park, Luigi's Mansion, Let's Go Safari blijven geparkeerd). Unity-route (Tomb Raider, Rabbids) draait nu als dialect unity-tcp van de brug: Rabbids Hollywood live gecontroleerd (verbonden, 120 pakketten/s, vizier, munten en start, schoten tellen mee voor de trilling).

# Derde ronde (2026-10-09, avond): resterende games

Samenvatting: geen enkele extra game is speelbaar geworden; Transformers (snelkoppeling weggehaald), Time Crisis 5, Jurassic Park, Luigi's Mansion en Let's Go Safari zijn geparkeerd met de bevindingen hieronder. De profielen zijn teruggezet naar de toestand van voor deze ronde (back-ups: `~/.local/state/ravengun/*.bak-ronde3`).

## Gemeenschappelijk: invoer bij TeknoParrot-Windows-games (Transformers, Time Crisis 5)
- De eigenaar speelde Transformers met de echte Move: beeld goed, maar "lightgun staat boven in de hoek, X-as doet niets". Met `ravengunctl` (testpatroon, toets 1/5, trekker) kreeg ik in de attract-modus van Transformers geen enkele reactie van de game, terwijl de events wel op het virtuele toetsenbord (event256) en de lightgun (event31) staan (uitgelezen met een Python-evdev-lezer). In de game-log van de shell (`Sega/shell/GameStateMachine - <datum>.log`, tijden in UTC) staat `GAME_MSG_START_PRESSED` alleen om 15:58:56 (ronde 2) en 17:54:44 (de eigenaar, 2,5 minuten gespeeld); in alle latere sessies, ook zonder profielwijziging, kwam er geen start door.
- TeknoParrotUI schrijft de invoerstatus in `/dev/shm/TeknoParrot_JvsState` (64 bytes; wordt via pipehelper naar `Z:\dev\shm\...` gespiegeld). Gemeten tijdens Transformers: een teller op offset 0x0c loopt op bij toets 5 en toets 1, maar de knopbits blijven 0 en de drie analoge waarden (offset 0x10/0x14/0x18 = 0x7e/0x7f/0x7f, midden) veranderen niet bij `test-pattern point 0.9 0.1` of 0.1 0.9. Dat past bij de symptomen: de aim komt niet in de game. Waarom is niet vastgesteld; de TeknoParrot-core is gepakt en strace/ptrace op de gameprocessen mag niet ("Operation not permitted"). Niet geprobeerd: de profielbindingen "Player 1 Gun X/Y" (Analog2/Analog0, ongebonden) expliciet aan de gun-assen koppelen, en de assen X=Analog0/Y=Analog2 zoals bij Terminator en TC5 in plaats van de sjabloonwaarden bij Transformers.
- Mijn eerdere meting van het vizier in de attract-demo van Transformers is waardeloos: de demo stuurt zelf twee richtkruizen aan.

## Transformers: Shadows Rising (geparkeerd, snelkoppeling verwijderd)
- Start nog steeds stabiel en beeldvullend (1920x1080, fullscreen, venster `TF2_Gun | Release`), FREE PLAY, attract met "How to play". Laden duurt ongeveer 75 s.
- De Test-/Service-knoppen ontbreken in de sjabloon van dit profiel. Ik voegde `Test` (F2) en `Service 1` (F3) toe (back-up van het profiel zonder die knoppen: `TransformersShadowsRising.xml.bak-ronde3`, variant met knoppen: `...xml.test-service`); de game opende er geen testmenu mee en de start bleef uit, dus weer teruggezet. De game heeft een eigen kalibratiemenu ("CALIBRATION START"/"DEFAULT SETTING" in `Sega/shell/testcalibrate.xml`, "PLAYER 1 CONTROLLER X/Y" in `testinputs.xml`); daar is de oorzaak van een gecomprimeerde X-as te zoeken zodra de test-knop werkt.
- Voor de eigenaar: opnieuw proberen met `scripts/play-halo.sh --profile TransformersShadowsRising`; de snelkoppeling kan terug met `scripts/make-game-shortcut.sh TransformersShadowsRising "Transformers: Shadows Rising"` zodra de besturing werkt.

## Time Crisis 5 (geparkeerd)
- Start direct (`--profile TC5`, plain wine), na ongeveer 60 s het attract-scherm met "INSERT 1 CREDIT(S)"; geen venster-randloos: 1774x998 met titelbalk. De TeknoParrot-core zit in het proces (`TeknoParrot64.dll` in de maps), de game heeft een `GunPeripheral`- en `DongleThread`, maar de JVS-pipe wordt niet geopend ("GAME CONNECTED" ontbreekt).
- Beeldvullend: de eigen sneltoets F11 van de game werkt via het virtuele toetsenbord (`ravengunctl key f11 tap`): het venster wordt 1920x1080 op 0,0 zonder rand. Een gamescope-poging is niet gedaan omdat F11 volstaat; niet in een script gezet omdat het zonder invoer geen zin heeft.
- Invoer: toets 5 (munt) en toets 1 geven geen credit, ook niet met de sjabloonoptie FreePlay=1 (die is in `TC5/Binaries/Win64/teknoparrot.ini` terechtgekomen en door de game genegeerd; profiel teruggezet). De eigen toetsen van de game (F11) werken wel. `README.txt` noemt Shift+H (richtkruis) en Ctrl+Q; Shift+H via xdotool toonde niets. Profielbindingen: Coin=D5, Test=F2, Service=F3, Gun Trigger/Button/Left Pedal = links/rechts/midden, Enter Sw = D1 (P2Button3), assen Analog0/Analog2. DemulShooter doet voor TC5 geheugenschrijven (`Game_Es3Tc5.cs`, standalone-modus), geen route voor ons.
- Op de NAS veranderd: `TC5/Binaries/Win64/teknoparrot.ini` (door TeknoParrot geschreven).

## Jurassic Park (geparkeerd)
- Vergelijking met Terminator (zelfde loader, werkt): `game` van Jurassic Park is een ELF zonder sectieheaders (`file`: "no section header"; Terminator: "stripped" met headers), en de loader meldt alleen bij JP "Warning SHT_DYNSYM not found in section headers / aborting symbol parsing due to invalid name ptr". JP gebruikt SDL 1.2, libudev en libX11 (Terminator: glut, geen SDL); in `ElfLdr2/libs` staat geen `libSDL-1.2.so.0` (wel `SDL.dll`).
- Crash gevangen (`WINEDEBUG=err+all,+seh` via `--env`): `EXCEPTION_ACCESS_VIOLATION` met eip=0x00058000 (leestoegang op 0x58000), met esi=0x550 (1360) en edi=0x300 (768) in de registers: de game roept na het doorgeven van de resolutie 1360x768 een functie aan op een niet-gemapt adres, waarschijnlijk een niet-opgeloste of niet-gealloceerde thunk van de loader (SDL-videopad). Dit gebeurt ~13-30 s na de start, vaak voor er een venster is. De SIGSYS-/single-step-meldingen ervoor zijn de anti-debug van de loader (normaal).
- Geprobeerd in deze ronde (4 starts): (1) Windowed=1 + gamescope; (2) `game_original` (de loader stopt na 1 s; Sentinel-envelope); (3) Windowed=1 + CustomResolution 1360x768 + gamescope; (4) idem zonder gamescope met `SDL_VIDEODRIVER=windib`: het proces leeft ~30 s zonder venster en crasht dan. De JVS-brug verbindt bij elke start ("GAME CONNECTED"). Profiel teruggezet (back-up `JurassicPark.xml.bak-ronde3`).
- `pmuser/gamelog.txt` blijft leeg: de game komt niet tot zijn eerste logregel. Online niets gevonden over deze combinatie (het enige gevonden issue, teknogods/TeknoParrot #83, beschrijft Windows en geen oplossing). Mogelijke vervolgstap voor de eigenaar: een andere dump van Jurassic Park (met sectieheaders) of een nieuwere ElfLdr2-versie met SDL-1.2-ondersteuning onder Wine.

## Luigi's Mansion Arcade (geparkeerd)
- Nieuw: met `TP_POSTSTART_REMOTETHREAD_MS=` leeg en `TP_ENTRYPOINT_REMOTETHREAD_MS=3000` (Proton, `WINEDEBUG=err+all`) laadt de core wel in VACUUM.exe (`teknoparrot64` staat in de modulelijst), maar daarna: "Unhandled illegal instruction at address ...3125 (thread 0198)". Dat adres is exact het entrypoint van VACUUM.exe (RVA 0xF23125, sectie `.capcom1`; de packer van Capcom). Het eerste byte-paar op het entrypoint is `eb 08` (een gewone jmp); de illegal instruction ontstaat dus doordat het geparkeerde entrypoint na de injectie niet netjes wordt hersteld onder Wine, of door de packer zelf. Niet opgelost.
- Online niets over vereisten gevonden (twee zoekopdrachten zonder relevante treffers).

## Let's Go Safari (geparkeerd)
- Onder Proton draait `GameSampR_RingWide.exe` met precies 1 thread, 185 MB, wachtend (kernel `ntsync_schedule`, status S, CPU zakt van 54 % naar 9 % in 90 s) en maakt nooit een venster. Dat is een wachtende hoofdthread, geen rekenwerk (laden). `game.bat` draait eerst `mxGetHwInfo.exe -s mxGetHwInfo.ini` (hardwarecontrole: RingWide, PCI-ids 1002:94cb en 10ec:0883, OS 0045.03.01, 1280x720) en alleen bij exitcode 0 de game; de TeknoParrot-core vervangt dat niet. `firewall.cfg` verwijst naar `GameOrg\GameSampR_RingWide.exe`, een map die niet in de dump zit.
- Niet geprobeerd (ruimte/tijd): `mxGetHwInfo.exe` los draaien om zijn exitcode te zien; een eigen prefix met redistributables.

## Wat er in deze ronde aan scripts is veranderd
- Niets in `scripts/`. Bestanden: `packaging/games-notes.json` (spelersnotities), dit bestand; snelkoppeling Transformers weggehaald.
- Buiten de repo: back-ups `~/.local/state/ravengun/{TC5,JurassicPark,TransformersShadowsRising}.xml.bak-ronde3` en `TransformersShadowsRising.xml.test-service`.
- Op de NAS veranderd door games/TeknoParrot (geaccepteerd): `Sega/shell/*` (logs, `variables/*`), `TC5/Binaries/Win64/teknoparrot.ini`; verder niets.


Testomgeving: SteamOS Desktop Mode, KDE Wayland met Xwayland :0, AMD RX 6950 XT, tv op 1920x1080, TeknoParrotUI 2.0.0.130 (Linux/Avalonia). Games staan op de NAS (`~/mnt/spidernas_emulation/Teknoparrot/<map>`) en zijn niet gekopieerd.

Starten en stoppen (voor elke game hetzelfde):

    cd ~/projects/ravengun
    scripts/play-halo.sh --profile PROFIEL [--no-wait-move] [--gamescope] [--env K=V]...
    scripts/play-halo.sh --stop

Een profiel (re)maken: `python3 -I scripts/tp-add-game.py PROFIEL "PAD/NAAR/EXE" [--wine-runner PAD] [--view] [--gamescope] [--env K=V]... [--force]`. Extra omgeving per profiel staat in `UserProfiles/<PROFIEL>.env`.

## Overzicht

| Game (profiel) | Status | Hoe |
| --- | --- | --- |
| Halo | werkt volledig | `--profile Halo` |
| Terminator Salvation (Terminator) | werkt beeldvullend, richten en schieten goed | `--profile Terminator` (gamescope staat in de .env) |
| Transformers: Shadows Rising (TransformersShadowsRising) | speelbaar via de IO-brug (hid-board): start, trekker en richten werken; kalibratiescherm, hendels en tweede speler niet geprobeerd (zie vijfde ronde) | `--profile TransformersShadowsRising` of de snelkoppeling (prefix-kopie met native DLL's, zie onder) |
| Time Crisis 5 (TC5) | speelbaar via de IO-brug (tp-shm): richten, trekker, pedaal en munt (Shift+T via de daemon) | `--profile TC5` (plain wine) |
| Tomb Raider (TombRaider) | beeld, "IO BOARD ERROR"; geparkeerd (eigen route met de daemon) | |
| Rabbids Hollywood (RabbidsHollywood) | titelscherm, stopt na ongeveer 45 s, knoppen werken niet; geparkeerd (eigen route met de daemon) | |
| Jurassic Park (JurassicPark) | nog niet: zwart/wit beeld; geparkeerd | |
| Luigi's Mansion (LuigisMansion) | geparkeerd: VACUUM.exe stopt na enkele seconden | |
| Let's Go Safari (LGS) | geparkeerd: wacht op een dienst op 127.0.0.1:40100 (PCP); route jvs staat klaar, zie zesde ronde | `--profile LGS` (route jvs) |

## Tweede ronde: wat is veranderd

### Terminator Salvation: schermvullend via TeknoParrotUI-gamescope (werkt)
- Oplossing: `UserProfiles/Terminator.env` met `TP_GAMESCOPE=1` en `TP_GAMESCOPE_PATH=/usr/bin/gamescope` (of `--gamescope` bij `play-halo.sh`). TPUI wrapt dan de wine-aanroep zelf (`gamescope -W 1920 -H 1080 -S fit -f --backend wayland -- wine BudgieLoader.exe ...`) ondanks de tekst "External emulator profile"; het profielveld `FullscreenScalingMode` hoeft niet gezet (de modus wordt dan "AutomaticFit" via de omgevingsvariabele). Windowed blijft 1.
- Resultaat (eerste poging): beeldvullend attract-scherm (FREEPLAY), JVS verbonden ("GAME CONNECTED"), de trekker uit `ravengunctl output-button trigger` start het spel, het crosshair is zichtbaar. TeknoParrotUi zelf heeft de virtuele lightgun (event31) en het toetsenbord (event256) open (de invoer loopt via de JVS-brug); het spelproces BudgieLoader heeft ze in deze opstelling niet zelf open.
- Lightgun-uitlijning (testpatroon, gemeten op schermafdrukken van de tv): hoeken (0,05/0,95) kwamen binnen ongeveer 1 % (bv. doel 0,95/0,05 -> crosshair op 0,944/0,051; doel 0,05/0,95 -> 0,051/0,949); rasterpunt (0,15/0,15) -> 0,148/0,149; (0,85/0,85) -> 0,849/0,849. Daarna heeft de eigenaar enkele minuten met de echte Move gespeeld: "ging goed", richten en schieten werkten.
- Op de NAS veranderd: niets nieuws t.o.v. ronde 1 (de game schrijft zijn eigen state in `T4User`).

### Jurassic Park: nog geen beeld (geparkeerd)
- (a) Wine-virtueel bureaublad via wrapper `scripts/wine-vdesktop.sh` (alleen BudgieLoader krijgt `explorer /desktop=RavenGun,1920x1080`; Unix-pad moet naar `Z:\` voor explorer): het bureaublad komt wel op de tv en de JVS verbindt, maar de game stopt na ongeveer 13 s zonder iets te tonen (zelfde gedrag als de venstermodus: waarschijnlijk dezelfde crash).
- (c) gamescope (`TP_GAMESCOPE=1`, Windowed=0): de game draait (CPU 160 %, JVS verbonden), in gamescope bestaat een venster "SDL_app" van 1368x768, maar het blijft effen wit; er komt geen enkele uitvoer van de game in het log. Zonder gamescope is het beeld zwart (venster van 14x14).
- (b) CustomResolution 1360x768 op volledig scherm zonder gamescope: hetzelfde zwarte beeld als daarvoor.
- Het profiel is teruggezet naar de oorspronkelijke staat (back-up: `~/.local/state/ravengun/JurassicPark.xml.bak-ronde2`; variant met CustomResolution aan: `JurassicPark.xml.ronde2-custom1360`). Volgende stap: de uitvoer van het ELF binnen BudgieLoader zichtbaar maken (WINEDEBUG +loaddll,+x11drv of een GL-trace) om te zien waar na "Starting Game" niets meer gebeurt; geen verband met Proton (plain wine).

### Transformers: Shadows Rising: werkt met native DirectX-DLL's in een prefix-kopie
- De shaderfout ("fx_2_0 ... not yet implemented") is weg met de native `d3dx9_43`, `d3dcompiler_43` en `d3dcompiler_47` (Microsoft DirectX-redistributable Juni 2010 en `d3dcompiler_47`, door winetricks gedownload) in een eigen kopie van de plain-wine-prefix.
- Prefix-kopie: `~/.local/share/TeknoParrotUI/prefixes/ravengun-test-tsr` (kopie van `shared/wine`; de symlink `drive_c/Sega` naar de NAS is mee gekopieerd als symlink). De gedeelde prefix is niet gewijzigd.
- winetricks: het losse script van github.com/Winetricks/winetricks (gecontroleerd als shellscript) is gebruikt met `WINE`/`WINESERVER` van de Kron4ek proton-11.0-2-runner. `cabextract` ontbreekt en kan niet worden geinstalleerd (geen sudo): een kleine vervanger op basis van `7z` staat in `~/.local/share/ravengun/tools/cabextract-shim/cabextract` (alleen voor winetricks, via PATH). Downloads staan in `~/.cache/winetricks`.
- Het profiel gebruikt de wrapper `scripts/wine-prefix.sh` (WineRunnerPath `~/.local/share/ravengun/wrappers/wine-prefix`, een symlink naar het script) die `RG_WINEPREFIX` (staat in `UserProfiles/TransformersShadowsRising.env`) als prefix zet voor zowel de game als pipehelper.exe.
- Resultaat: Game Shell -> Transformers2.exe draait stabiel, attract met "FREE PLAY"; een trekker-tik (`ravengunctl output-button trigger tap`) brengt het spel naar STAGE SELECT. De core zit in het proces en heeft de virtuele gun en het toetsenbord zelf open (zoals bij Halo). De JVS-brug verbindt niet (niet nodig). Niet getest: coin, schieten tijdens het spel, geluid.
- Op de NAS veranderd (door game en shell): logbestanden in `Sega/shell` en `Sega/Transformers2`, `shell/teknoparrot.ini`, `shell/variables/*`.

### Time Crisis 5: de echte game start direct (plain wine) - invoer nog open
- RSLauncher.exe start de echte game en sluit af; direct starten werkt: `GamePath` = `TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe`, `ExecutableName` hetzelfde, `TP_GAME_WORKING_DIRECTORY` = die map (`UserProfiles/TC5.env`, ook `TP_POSTSTART_REMOTETHREAD_MS=3000`).
- Zowel onder het standaard Proton-script als onder plain wine (nu ingesteld, `WineRunnerPath` = Kron4ek proton-11.0-2): venster "TimeCrisis Game (64-bit, DX9)" met de TeknoParrot-core geladen ("Have fun :)"), attract-video en "INSERT 1 CREDIT(S)". COM4 is niet nodig gebleken (de plain-wine-prefix heeft com1-com33, de Proton-prefix alleen com33; beide startten).
- JVS verbindt niet ("GAME CONNECTED" ontbreekt); een toets 5 via xdotool gaf geen credit. Het venster is een gewoon venster (niet schermvullend); mogelijk werkt `--gamescope` (niet geprobeerd).
- Op de NAS veranderd: `teknoparrot.ini` (bestond al), verder niets.

### Windows-games onder Proton: JVS-brug (TeknoParrotUI-issue #1443)
- "Bad server socket 11" staat in geen enkel log. Maar: onder het standaard Proton-script (GE-Proton11-7) is `pipehelper.exe` nooit een lopend proces; het bridge-log meldt alleen "helper launched" (de uitvoer van de helper wordt onder Proton niet gelogd). Een met de hand gestarte `proton run pipehelper.exe ...` (schone omgeving, dezelfde prefix) blijft wel draaien, ook met de echte bridge-poort. De omgeving van TeknoParrotUi bevat geen `WINESERVERSOCKET`. Conclusie: de helper sterft onder Proton bij het starten door TPUI (oorzaak niet vastgesteld, waarschijnlijk #1443-achtig), onder plain wine werkt hij altijd ("pipe created, waiting for game").
- Maar de JVS-pipe is niet de oorzaak van "IO BOARD ERROR" bij Tomb Raider: ook met een met de hand voorgestarte helper (pipe bestond voor de gamestart) blijft de fout. Geen nieuwe Proton-versies gedownload; in `~/.local/share/TeknoParrotUI/proton/` staat alleen GE-Proton11-7-x86_64 (de "Linux Setup" is niet geopend).

### Tomb Raider en Rabbids Hollywood: geparkeerd
- Tomb Raider: er is geen BepInEx; de map heeft Doorstop (`winhttp.dll`) met een eigen `Doorstop.dll` (Harmony-patch `HarmonyPatch_GunControl`) en `Assembly-CSharp.dll` heeft `CheckIOBoard` en gebruikt `libusb`/`ftdi.dll`/`usb.dll` uit `Game_Data/Plugins` voor het I/O-bord ("GetVersionDRV_USB"); het `Gun_*.log` zegt "ERROR: 1001: Base not found" (de lichtgun-basis). De TP-core zit in Game.exe (TeknoParrot64.dll geladen). De fout is dus het eigen USB-I/O-bord van de game, niet de JVS-pipe. Geen Unity-log gevonden; `-logFile` in ExtraParameters breekt de loader ("Please use the following format").
- Rabbids Hollywood: `BepInEx/LogOutput.log` toont alleen een normale start ("RHA Mouse Plugin" geladen, geen fout); de plugin leest toetsen (C/V/B/N = coin, Numpad 1/3/7/9 = start). Een `c` via xdotool (met Num_Lock) gaf "Press START to Play" in plaats van "FREE MODE" (er was een credit), maar de game sloot toch na 45-70 s af, onder Proton en plain wine, met en zonder `TP_POSTSTART_REMOTETHREAD_MS`. Op de tv probeerde de eigenaar de Move-knoppen: geen reactie (past bij ontbrekende JVS; de knoppen worden door de plugin niet uit de JVS gelezen). Beide games komen later via een eigen route met de daemon (rol van DemulShooter).
- Veranderd t.o.v. ronde 1 bij Rabbids: alleen de runner (tijdelijk plain wine) en de credit-toets via xdotool; het profiel is teruggezet.

### Luigi's Mansion en Let's Go Safari: geparkeerd
- Luigi's Mansion: ook met de prefix-kopie met redistributables stopt VACUUM.exe na ongeveer 6 s.
- Let's Go Safari: met de prefix-kopie (en ook met `TP_POSTSTART_REMOTETHREAD_MS=8000`) blijft het proces hangen zonder venster, zoals in ronde 1.
- Beide profielen zijn teruggezet naar het standaard Proton-script.

## Scripts en bestanden (ronde 2)
- `scripts/play-halo.sh`: nieuwe optie `--gamescope`.
- `scripts/tp-add-game.py`: nieuwe optie `--gamescope` (schrijft de .env).
- `scripts/wine-vdesktop.sh`: WineRunnerPath-wrapper voor een Wine-virtueel bureaublad (alleen loader-aanroepen); zie Jurassic Park.
- `scripts/wine-prefix.sh`: WineRunnerPath-wrapper met een eigen prefix via `RG_WINEPREFIX`.
- Lokaal buiten de repo: `~/.local/share/ravengun/wrappers/{wine-vdesktop,wine-prefix}` (symlinks), `~/.local/share/ravengun/tools/cabextract-shim/cabextract`, prefix-kopie `~/.local/share/TeknoParrotUI/prefixes/ravengun-test-tsr` (604 MB), back-ups van profielen `~/.local/state/ravengun/*.bak-ronde2`.
- Profielen: Terminator (.env met gamescope), TC5 (direct de echte exe, plain wine, .env), TransformersShadowsRising (wrapper en .env); overige profielen ongewijzigd.


# Ronde 1 (eerste ronde, ter referentie; waar ze afwijken geldt hierboven)

## Gemeenschappelijke bevindingen

Injectie van de TeknoParrot-loader onder Wine. De standaardmethode van OpenParrotLoader (RemoteThread op een gepauzeerd proces) faalt in alle Windows-games met "Failed to find kernel32.dll in target process ... Failed to Load DLL via RemoteThread! (Error 2b)", zowel onder Proton als onder plain wine: Wine kan de modulelijst van een gepauzeerd proces niet lezen. De loader kent omgevingsvariabelen die dit omzeilen (gevonden in de strings van OpenParrotLoader64.exe):

- `TP_POSTSTART_REMOTETHREAD_MS=3000`: start het proces eerst, injecteert na 3 s. Dit werkt (Tomb Raider, Rabbids Hollywood, Let's Go Safari, Transformers) zolang de game die 3 s overleeft. Games die zichzelf binnen een paar seconden afsluiten zonder core (Luigi's Mansion, Time Crisis 5) halen het niet.
- `TP_ENTRYPOINT_REMOTETHREAD_MS=...`: parkeert het entrypoint en injecteert vóór de start. De injectie lukt, maar daarna crasht de game in Wine op het entrypoint met "Unhandled illegal instruction" (Tomb Raider, Luigi's Mansion; Time Crisis 5 en Let's Go Safari lopen er ook op stuk). Waarschijnlijk wordt de geparkeerde instructie onder Wine niet netjes hersteld.
- `TP_DIRECTHOOK=1` doet niets zichtbaars (valt terug op de standaardmethode).
- `TP_GAME_WORKING_DIRECTORY=Z:\...` zet de werkmap van de game (heeft effect).

`play-halo.sh` leest deze variabelen uit `UserProfiles/<PROFIEL>.env`; `tp-add-game.py` schrijft dat bestand.

Mapnamen met een spatie aan het eind ("Rabbids Hollywood ", "Terminator Salvation ") geven in Wine "Directory name invalid (0x10B)". Oplossing: `--view` (lokale map met alleen symlinks naar de NAS, geen kopie).

Proton-logging: onder Proton staat de console van de loader alleen op het scherm (conhost-venster), niet in het log. Voor diagnose is de plain-wine-runner handiger, want dan staat alles in `~/.local/state/ravengun/logs/tpui-*.log`.

JVS-brug: bij geen enkele Windows-game verbond de JVS-pipe (alleen Terminator Salvation, ElfLdr2, verbond: "GAME CONNECTED"). Bij Tomb Raider en Rabbids Hollywood draait de game wel met de TeknoParrot-core geladen, maar opent hij de pipe niet. Die twee games starten nu zonder TeknoParrot (zie hun secties).

Stoppen: `scripts/play-halo.sh --stop` stopt nu de hele transient unit `ravengun-tpui` (TeknoParrotUi, Proton/Wine, wineserver, de game) en meldt een fout als er nog processen van de test in die cgroup zitten. Een TeknoParrotUi-venster of wineserver dat de speler zelf vanaf het bureaublad startte (andere cgroup) blijft met opzet ongemoeid. Als zo'n wineserver al op de Proton-prefix draait, komen nieuwe wine-processen daarvan buiten de unit terecht; stop die sessie dan eerst.

## Tomb Raider (profiel TombRaider): werkt zonder TeknoParrot (Unity-route)

Status (2026-10-09, tweede ronde): speelbaar zonder TeknoParrot en zonder DemulShooter.exe. De game draait direct onder GE-Proton in een eigen prefix met de BepInEx-plugin van DemulShooter (v17.9); ravengund is de TCP-klant van de plugin (127.0.0.1:33610). Geen "IO BOARD ERROR" meer: de plugin vervangt het I/O-bord. Op de tv gezien: spelmodus kiezen, hoofdstukken, filmpje ("Hold Trigger to Skip"), gameplay met vizier, ammo, score en credits.

Startcommando: `scripts/play-halo.sh --profile TombRaider` (stuurt door naar `scripts/play-unity.sh --game TombRaider`; `--teknoparrot` forceert de oude, niet werkende TeknoParrot-route). Stoppen: `scripts/play-halo.sh --stop`. Procedure: `docs/procedures.md`.

Live gemeten (met `ravengun-aagames-probe` tegen de draaiende, nog niet herstarte daemon; de echte Move leverde de aim):
- a. BepInEx en plugin laden: `BepInEx/LogOutput.log` in de view meldt "Loading [TombRaider_BepInEx_DemulShooter_Plugin 17.0.0.0]", "TCP Server is listening on Port 33610", "Chainloader startup complete".
- b. De plugin luistert op 33610 (`ss -ltnp`), de klant verbindt.
- c. Het vizier staat op de opgegeven plek (u=0,25 v=0,25 geeft het vizier op 480/270 pixels van linksboven; met de echte Move als bron stond het vizier binnen ongeveer 20 pixels van de uitgelezen aim, niet nauwkeuriger bepaald omdat de Move bewoog); de trekker schiet (10 schoten, ammo 10 naar 0, recoil 10) en slaat het filmpje over; Herladen laadt bij.
- d. Munt en start: toets 6 en 7 geven credits voor speler 2 en 3 ("1/4 CREDITS" op de tv, Credits in de uitvoer); speler 1 had al credits en startte.
- e. Uitvoer komt binnen: ammo, leven (float), credits, speelt, recoil per schot; de recoil-koppeling naar de rumble is bij Rabbids op de echte Move gemeten (zie daar); bij Tomb Raider alleen de recoil-vlaggen.
- Niet getest: geluid beluisterd, twee of meer spelers tegelijk, een lange speelsessie, een volledig spel tot het einde.

Runner: GE-Proton11-7 (DXVK), prefix `~/.local/share/ravengun/prefixes/unity-aagames`. Omgeving: `WINEDLLOVERRIDES=winhttp=n,b` (doorstop), `-popupwindow` (randloos), resolutie 1920x1080 via de plugin-ini.

Veranderd op de NAS (map "Tomb Raider"): niets door deze route. De view is een lokale map met links; wat de game schrijft (NVRAM, logs, `ControllerBaseConfig.json`) staat lokaal in `~/.local/share/ravengun/runtime/views-aagames/TombRaider`. (Bestanden van 17:39 op de NAS, `teknoparrot.ini`, `ControllerBaseConfig.json` en `Gun_*.log`, zijn van de eerdere TeknoParrot-ronde.)


## Tomb Raider en Rabbids Hollywood: eigen plugin (route 2)

Status (2026-10-09/10): beide games draaien standaard met onze eigen BepInEx-plugin (`plugins/unity/`, `docs/decisions.md` "Route 2"); DemulShooter is de terugval (`RAVENGUN_UNITY_PLUGIN=demulshooter` of `plugin` in `aagames-games.json`). Het BepInEx-log toont "Loading [RavenGun ...]" en bevat geen DemulShooter. Geen bordfout; de daemon is ongewijzigd (zelfde draadprotocol).

Live gemeten met de eigen plugin, Tomb Raider (op de tv, met `ravengunctl arm`, `key`, `test-pattern`, `output-button`):
- start zonder "IO BOARD ERROR", resolutie 1920x1080 beeldvullend; munt (toets 5 vier keer geeft 4 credits) en start (toets 1) tot en met het spel; trekker slaat de filmpjes over (houd vast) en schiet; ammo loopt terug (10 naar 9 en lager), herladen zet het weer op het volle magazijn; recoil per schot bij de daemon (schoten tellen mee voor de trilling); leven en credits in de uitvoer.
- vizier op de testpunten (raster 3x3): linksboven (0,1/0,1) en bovenaan het midden (0,5/0,1) stonden op de verwachte plek van het scherm, beide assen; Y-oorsprong onderaan.
- vizier verbergen (`Vizier/Modus = Hide`): in het spel geen vizier meer zichtbaar, schieten werkt.
- daemon weg (`iobridge stop`) en terug: de game loopt door, het log meldt "Daemon losgekoppeld" en later "Daemon verbonden" en "Invoer van de daemon actief".
- gevonden en opgelost tijdens het testen: een korte trekkerdruk (korter dan een frame) ging verloren (latch in de server); munitie via `PlayerWeapon.ClipRemaining` in plaats van het HUD.

Rabbids Hollywood met de eigen plugin: start zonder bordfout, 4 munten en start (StartLamp in de uitvoer), de game komt tot het kiezen van de wereld, recoil-vlaggen komen bij de daemon. Zonder `GunFatalError`-onderdrukking meldde de game "Gun not found" en stopte hij; met onderdrukking niet meer. OPEN PUNT (Rabbids, eigen plugin): de game sluit zichzelf ongeveer 50 tot 70 s na de start af. Gemeten met de afsluit-diagnose (`Log/Uitgebreid`): `UnityEngine.Application.Quit` wordt aangeroepen door een coroutine van `SBK.Matrix.MatrixManager` (een verhulde dongle-controle; het log zegt "DONGLE NOT FOUND"), niet door de plugin, de bordcontrole of de geweerfout. Met de DemulShooter-plugin in dezelfde view bleef de game 229 s actief (twee runs, 80 s en 229 s), met de eigen plugin viel hij in vier runs binnen 70 s weg; Tomb Raider heeft dit gedrag met de eigen plugin niet gezien (minuten actief). Waarom het verschilt is niet vastgesteld (de DemulShooter-DLL is bewust niet onderzocht). De dongle-controle omzeilen is een omzeiling van een beveiliging van de game en is niet gedaan; dat vraagt een uitdrukkelijke keuze van de eigenaar (zie `docs/decisions.md`, 2026-10-07: halo.so). Tot dan: Rabbids Hollywood met `RAVENGUN_UNITY_PLUGIN=demulshooter` of `plugin: "demulshooter"` voor deze game in `aagames-games.json`.
Niet beoordeeld op de tv met de eigen plugin: vizier op de testpunten, schieten en trilling in een lopend level van Rabbids (de game viel steeds weg voor een level speelbaar was; de laatste testrondes konden bovendien niet doorgaan omdat een andere agent de tv bezet hield).

Met alleen de terugval (DemulShooter) bevestigd: beide games starten, de plugin luistert op 33610 en de daemon verbindt.

Niet getest: twee of meer spelers, geluid, een lang spel, "treffer"-vlag (Damaged) bij Tomb Raider, de startlamp-koppeling bij Tomb Raider (`IsPlaying` komt uit de speltoestand).

## Luigi's Mansion Arcade (profiel LuigisMansion): geparkeerd

Status: VACUUM.exe sluit zichzelf binnen een paar seconden af (zonder core), en dan faalt de loader met "Failed to find kernel32.dll in target process". Er is nooit beeld geweest.

Startcommando: `scripts/play-halo.sh --profile LuigisMansion`

Wat geprobeerd: Proton en plain wine; `TP_POSTSTART_REMOTETHREAD_MS` 3000 en 600; `TP_ENTRYPOINT_REMOTETHREAD_MS=1000` (injectie lukt, core laadt, daarna "Unhandled illegal instruction" op het entrypoint van VACUUM.exe, RVA 0xF23125); `TP_GAME_WORKING_DIRECTORY` op de gamemap (game.bat gebruikt `x:\exe\x64\VACUUM.exe` met de gamemap als werkmap). Het exe-pad uit de sjabloon (`exe/x64/VACUUM.exe`) klopt.

Laatste foutmelding: "Failed to find kernel32.dll in target process 284 / Failed to Load DLL via RemoteThread! (Error 2b)". Beste vermoeden: de game heeft de TeknoParrot-hooks vanaf de eerste instructie nodig (Nu 1.1: o.a. dongle, `sendFlash COM1`, schijfletters X:/Y:) en sluit af zodra die ontbreken; de geparkeerde-entrypointmethode zou dit oplossen maar crasht onder Wine. Oplossing ligt vermoedelijk bij een nieuwere loader of Wine-versie, of bij een Wine-patch voor de entrypoint-parkering.

Veranderd op de NAS (map "Luigi's Mansion Arcade"): `exe/x64/teknoparrot.ini` (aangemaakt door TeknoParrot, daarna herschreven).

## Let's Go Safari (profiel LGS): geparkeerd

Status: GameSampR_RingWide.exe start (32-bit loader injecteert met de post-start-methode, "Have fun :)"), maar het proces blijft hangen met één thread en maakt nooit een venster. Geen beeld, JVS niet verbonden.

Startcommando: `scripts/play-halo.sh --profile LGS`

Runner: Proton (standaard); plain wine gaf hetzelfde. Met `TP_ENTRYPOINT_REMOTETHREAD_MS=1000` (plain wine) sterft het proces meteen (entrypoint-crash, zie hierboven). De sjabloon heeft Windowed=1 en Input API DirectInput (het script zet RawInput).

Laatste log: de core laadt, daarna alleen enkele EXCEPTION_SINGLE_STEP (de anti-debug-controle van de core) en `fixme:ntdll:enum_firmware_info`; verder niets. Beste vermoeden: de hoofdthread wacht op iets wat de core of de dongle-/hardwarecontrole (`mxGetHwInfo.exe -s mxGetHwInfo.ini` in game.bat) zou moeten leveren; wordt in de gamemap niet door TeknoParrot uitgevoerd. Een tweede poging met een langere vertraging (bv. `TP_POSTSTART_REMOTETHREAD_MS=8000`) en met `mxGetHwInfo` vooraf is nog niet gedaan.

Veranderd op de NAS (map "Let's Go Safari"): `teknoparrot.ini` (nieuw).

## Rabbids Hollywood (profiel RabbidsHollywood): werkt zonder TeknoParrot (Unity-route)

Status (2026-10-09, tweede ronde): speelbaar zonder TeknoParrot en zonder DemulShooter.exe, zelfde route als Tomb Raider. De game sluit niet meer na 45 s af. Op de tv gezien: titelscherm (nu in het Engels), "Insérer/Insert credits" per speler met kwartmunten, na vier munten en Start het laadscherm en het spel met vizier, wapen en levensbalk.

Startcommando: `scripts/play-halo.sh --profile RabbidsHollywood` (naar `scripts/play-unity.sh --game RabbidsHollywood`). De map op de NAS heeft een spatie aan het eind van de naam; de lokale view lost dat op (het pad in `aagames-games.json` bevat die spatie).

Live gemeten (zelfde methode):
- a. `BepInEx/LogOutput.log`: BepInEx 5.4.21 onder Unity 2017.2, "Loading [RabbidsHollywood_BepInEx_DemulShooter_Plugin 17.0.0.0]", "Plugin Loaded".
- b. De plugin luistert op 33610, de klant verbindt.
- c. Het vizier volgt de opgegeven positie (u=0,3 v=0,3 geeft het vizier op 576/324 pixels van linksboven); de trekker schiet (recoil loopt op, tientallen schoten bij ingedrukt houden).
- d. Munt en start: viermaal toets 5 geeft credits 4, toets 1 start; daarna credits 0 en speler 1 speelt (StartLamp).
- e. Uitvoer: ammo, leven, credits, recoil, StartLamp binnen; recoil gaf een rumble-puls (zie Tomb Raider). Let op: bij Rabbids staat StartLamp als laatste veld (andere veldvolgorde dan Tomb Raider).
- Niet getest: geluid beluisterd, meerdere spelers, een volledig spel; Herladen bestaat bij Rabbids niet (geen Reload-byte in het pakket).

Veranderd op de NAS (map "Rabbids Hollywood "): niets door deze route (alleen de eerdere TeknoParrot-ronde schreef daar). De plugin-bestanden en het BepInEx-log staan lokaal in `~/.local/share/ravengun/runtime/views-aagames/RabbidsHollywood`.

## Terminator Salvation (profiel Terminator): werkt (in venster)

Status: de game draait via ElfLdr2 onder plain wine: attract-scherm met "FREEPLAY" in het venster "TeknoBudgie - Terminator Salvation" (ongeveer 1360x768, niet schermvullend). JVS-brug verbonden ("*** GAME CONNECTED ***"). BudgieLoader opent de RavenGun-nodes: `/dev/input/event31` (virtuele lightgun) en `event256` (virtueel toetsenbord). Niet getest: schieten, geluid.

Startcommando: `scripts/play-halo.sh --profile Terminator`

Runner: plain wine (`~/.local/share/ravengun/wine/proton-11.0-2/bin/wine`, zoals Halo en Jurassic Park); Proton is hier niet geprobeerd omdat de JVS-brug-bug bij ElfLdr2-games bekend is. Gebruikt `game` (niet `game_original`) via `--view` (symlink-map, ElfLdr2-link erbij).

Volledig scherm (Windowed=0, 1920x1080) gaf net als bij Jurassic Park geen zichtbaar venster (de game draait en de JVS verbindt, maar er komt geen beeld); daarom blijft het profiel op venstermodus. Het zwarte-beeldprobleem van Jurassic Park (volledig scherm, 32-bit ElfLdr2) is dus niet algemeen: venstermodus werkt hier wel. De Jurassic Park-venstermodus (exit 255) is een ander verhaal; of die ook met deze Terminator-instellingen werkt is niet getest. Het script heeft nu `--fullscreen` om dit snel om te zetten.

Veranderd op de NAS (map "Terminator Salvation "): `GeneralPurposeStateMachine.txt` (herschreven), `T4User/log/syslog_bb3.txt` en `T4User/aud/*.aud` (nieuw/herschreven).

## Time Crisis 5 (profiel TC5): geparkeerd

Status: RSLauncher.exe sluit zichzelf binnen een paar seconden af; de loader meldt "Failed to find kernel32.dll in target process". Nooit beeld.

Startcommando: `scripts/play-halo.sh --profile TC5`

README.txt (in de gamemap) gelezen: de map `TeknoParrot` bevat opslagdata (schijven c:, f:, g:); de README noemt Ctrl+Q (afsluiten), Shift+H (richtkruis), F11 (venster) en lichtgun-gevoeligheid. De sjabloonknoppen zijn nu gebonden: Gun Trigger -> trekker, Gun Button -> rechts, Left Pedal -> midden, Enter Sw -> 1, Service Sw -> F3, Coin -> 5, Test -> F2.

Wat geprobeerd (Proton): standaardinjectie, `TP_ENTRYPOINT_REMOTETHREAD_MS=5000` (de loader meldt "entry point is parked", daarna is het proces weg), `TP_POSTSTART_REMOTETHREAD_MS=3000`, `TP_DIRECTHOOK=1`. Beste vermoeden: RSLauncher is een launcher die de echte game (`TC5/Binaries/...`) start en afsluit; de core moet dus in het launcher-proces en zijn kind, en dat lukt niet met deze loader onder Wine. Zelfde klasse als Luigi's Mansion.

Veranderd op de NAS (map "Time Crisis 5"): `teknoparrot.ini` (nieuw).

## Transformers: Shadows Rising (profiel TransformersShadowsRising): geparkeerd

Status: Shell.exe start, de game-shell toont "Game Shell Version 180605" en start Transformers2.exe, maar die geeft een Windows-foutvenster over het compileren van de shaders. Onder Proton blijft de shell hangen op het splash-scherm met "InitIOBoard: Couldn't find IOBoard device". JVS niet verbonden, core niet in het proces.

Startcommando: `scripts/play-halo.sh --profile TransformersShadowsRising`

GamePath is `Sega/shell/Shell.exe`; de sjabloon heeft geen GamePath2 en de shell start Transformers2.exe zelf (ExecutableName bevat beide namen voor de procesdetectie). De Sega-software verwacht `C:\Sega\Shell` en `C:\Sega\Transformers2`: daarom is in beide prefixes een symlink `drive_c/Sega` naar de NAS-map "Sega" gemaakt (lokaal, geen kopie). Runner nu: plain wine (kwam verder dan Proton).

Laatste foutmelding (venster op de tv): "E5017: Aborting due to not yet implemented feature: Writing fx_2_0 sampler objects initializers is not implemented / Write pass assignments." Dat is de HLSL-effectcompiler van Wine (d3dcompiler_47/vkd3d-shader). De game laadt `d3dx9_43` en `d3dcompiler_43/47` als builtin (overrides op `d3dx9_40` en `d3dcompiler_46` hielpen dus niet; de gamemap bevat alleen `D3DX9_40.dll` en `D3Dcompiler_46.dll`). Beste vermoeden: er zijn native `d3dx9_43.dll` en `d3dcompiler_47.dll` in de prefix nodig (normaal via winetricks; winetricks en cabextract ontbreken op dit systeem, en er is niets gedownload). Daarnaast is de TeknoParrot-core niet geladen (de IO-bordlog van de shell meldt telkens "Couldn't find IOBoard device"), dus ook daar is nog werk.

Veranderd op de NAS (map "Sega"): `shell/teknoparrot.ini` (nieuw), ongeveer 50 logbestanden `shell/<naam> - <datum>.log` (nieuw), `shell/variables/tton`, `tton.bak`, `erlg`, `erlg.bak` (herschreven).

## Wijzigingen aan de scripts

`scripts/play-halo.sh`: `game_running`, `--stop` en de start-controle werken nu voor alle profielen. Alle processen van een test staan in de cgroup van de unit `ravengun-tpui`; een game telt als draaiend als daar een proces in zit met de naam van de loader (BudgieLoader*, OpenParrotLoade*) of een bestandsnaam uit ExecutableName/GamePath/GamePath2 van het profiel (comm, 15 tekens, hoofdletterongevoelig). `--stop` stopt de unit (SIGTERM, na 10 s SIGKILL; `TimeoutStopSec=10`), wacht tot de cgroup leeg is en geeft een foutmelding als er nog iets in staat. Wachttijd voor de start is 120 s (`RAVENGUN_START_WAIT`). Getest met Halo (start, beeld, stop in 0,3 s, geen resterende processen).

`scripts/tp-add-game.py`: `--env K=V` en een standaard `.env` voor Windows-games (`TP_POSTSTART_REMOTETHREAD_MS=3000`); `--fullscreen`; `--view` voegt de ElfLdr2-link alleen toe bij ElfLdr2-profielen; extra bindingen voor "Service Sw", "Credit", "Gun Trigger", "Gun Button", "Left Pedal" en "Enter Sw".

## Opruimen na de tests

Er draait geen game van deze tests meer, de lightgun-uitvoer is ontwapend en de tv staat op het bureaublad. Een TeknoParrotUi-venster dat al draaide voordat de tests begonnen (PID 12262, gestart vanaf het bureaublad om 15:56, met een vastgelopen Luigi's Mansion-sessie) is niet aangeraakt; de vastgelopen Luigi's Mansion-sessie (loader, wineserver) is tussen 16:17 en 16:27 verdwenen zonder dat ik die stopte (mijn `--stop` raakt alleen de eigen unit).

## Achtste ronde (2026-10-10, nacht): Luigi's Mansion

Status: WERKT. Titelscherm, attract met FREE PLAY, start, keuze besturing, stage-select, laden en gameplay (Entrance) staan beeldvullend op de tv (1920x1080, wined3d); richten (virtuele lightgun, tp-shm) en trekker werken: kruisje op (0,8/0,3) en een geraakt spook (score 10, munten).

Oorzaak van het zwarte scherm: de game speelt `Data\Movie\title_*.wmv` (en `kokuchi.wmv`) af via DirectShow (quartz) en `WMCreateReader` (wmvcore) op winegstreamer. Zonder ASF-demuxer en WMV-decoder in GStreamer sluit VACUUM.exe af bij `title_movie.wmv` (echte Movie-map) of blijft het scherm zwart met een lege Movie-map (hoofdthread draait 90 % CPU in de berichtenlus, bijna geen d3d-werk; de game wacht op de film). Op het systeem ontbreken `gst-libav` en `gst-plugins-ugly` (alleen `libgstasfmux`).

Oplossing (geen root, niets systeemwijd): `scripts/fetch-gst-wmv.sh` haalt `gst-libav` en `gst-plugins-ugly` (zelfde versie als de geïnstalleerde gstreamer, 1.28.5-2) uit de SteamOS-pakketrepo en pakt alleen `libgstlibav.so` en `libgstasf.so` uit in `~/.local/share/ravengun/tools/gst-extra/plugins`. In `UserProfiles/LuigisMansion.env` staat `GST_PLUGIN_PATH=<die map>`. De GE-Proton in `~/.local/share/TeknoParrotUI/proton` heeft wel ffmpeg-bibliotheken maar geen GStreamer-libav-plug-in; die is dus niet nodig en niet gebruikt. De film speelt vloeiend (title_advertise00, title_movie e.d.).

Exacte configuratie (profiel `LuigisMansion`):
- `.env`: `TP_REMOTETHREAD=off` (standaard entry-point-route van de loader), `TP_GAME_WORKING_DIRECTORY=Z:\home\deck\.local\share\ravengun\runtime\views\LuigisMansion\exe\x64`, `GST_PLUGIN_PATH=/home/deck/.local/share/ravengun/tools/gst-extra/plugins`. DXVK-regels blijven uitgecommentarieerd (wined3d volstaat).
- Runner: plain wine (Kron4ek proton-11.0-2), gedeelde prefix, direct-modus (`scripts/play-halo.sh --profile LuigisMansion --direct --no-wait-move`).
- View `~/.local/share/ravengun/runtime/views/LuigisMansion`: alle NAS-items als symlink; `Data -> DataGlobal/EN/Data`; `DataGlobal/EN/Data` is een echte map met symlinks naar de NAS-mappen en `Movie` is een symlink naar de NAS-map `DataGlobal/EN/Data/Movie` (de lege `Movie`-map uit de eerdere test is vervangen). Er is niets gekopieerd of getranscodeerd.
- Route: `~/.config/ravengun/io-routes.json` (tijdelijk) `LuigisMansion: route tp-shm, layout LuigisMansion`.
- Opstarttijd: ongeveer 25 s tot de eerste film (veel modellen laden), daarna attract.

Invoer getest: start (key 1) gaat van attract naar de waarschuwing, dan naar de keuze Simple/Normal Controls (23 s timer, kiest zelf Normal), stage-select (kiest zelf Old Clockworks) en tutorial/laden; de trekker (`output-button trigger`) schiet; het kruisje volgt `test-pattern point`.

Zonder dat het nodig bleek: X:/Y:-schijfletters, COM1/`sendFlash` (de game ging door zonder), `oxGetHwInfo`. De game schrijft zelf in de NAS-map `Ram/` (CreditConfig.bin, EEPROM_0x00001100.bin, SRAM_0x00004200.bin gewijzigd 2026-10-10) en `exe/x64/teknoparrot.ini`. In het Wine-log staan alleen onschuldige "not found"-regels (`Ram/Network_eth0.bin`, `config.csv`, enkele `Effects/bishamon`- en `Model/.../*.mattr`-bestanden die niet bestaan).

Scripts: nieuw `scripts/fetch-gst-wmv.sh`.
