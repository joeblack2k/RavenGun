# Games in TeknoParrot: status eerste ronde (2026-10-09)

Testomgeving: SteamOS Desktop Mode, KDE Wayland met Xwayland :0, AMD RX 6950 XT, tv op 1920x1080, TeknoParrotUI 2.0.0.130 (Linux/Avalonia). Games staan op de NAS (`~/mnt/spidernas_emulation/Teknoparrot/<map>`) en zijn niet gekopieerd. Dit is een snelle eerste ronde (maximaal ongeveer 10 minuten per game), geen diepgaand debuggen.

Starten en stoppen (voor elke game hetzelfde):

    cd ~/projects/ravengun
    scripts/play-halo.sh --profile PROFIEL            (wacht op de Move; voeg --no-wait-move toe om dat over te slaan)
    scripts/play-halo.sh --stop

Een profiel (re)maken: `python3 -I scripts/tp-add-game.py PROFIEL "PAD/NAAR/EXE" [--wine-runner PAD] [--view] [--env K=V]... [--force]`. Voor Windows-games schrijft dit script nu ook `UserProfiles/<PROFIEL>.env` met de loader-instelling die onder Wine nodig is (zie hieronder).

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
