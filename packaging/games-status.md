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

JVS-brug: bij geen enkele Windows-game verbond de JVS-pipe (alleen Terminator Salvation, ElfLdr2, verbond: "GAME CONNECTED"). Bij Tomb Raider en Rabbids Hollywood draait de game wel met de TeknoParrot-core geladen, maar opent hij de pipe niet.

Stoppen: `scripts/play-halo.sh --stop` stopt nu de hele transient unit `ravengun-tpui` (TeknoParrotUi, Proton/Wine, wineserver, de game) en meldt een fout als er nog processen van de test in die cgroup zitten. Een TeknoParrotUi-venster of wineserver dat de speler zelf vanaf het bureaublad startte (andere cgroup) blijft met opzet ongemoeid. Als zo'n wineserver al op de Proton-prefix draait, komen nieuwe wine-processen daarvan buiten de unit terecht; stop die sessie dan eerst.

## Tomb Raider (profiel TombRaider): werkt deels

Status: Game.exe start onder Proton met beeld op de tv, maar toont "WARNING - IO BOARD ERROR - PLEASE SEE ATTENDANT". De JVS-brug verbindt niet.

Startcommando: `scripts/play-halo.sh --profile TombRaider`

Runner: Proton (standaard). Plain wine gaf hetzelfde resultaat. `.env`: `TP_POSTSTART_REMOTETHREAD_MS=3000` en `WINEDLLOVERRIDES=winhttp=n,b` (de Doorstop-proxy `winhttp.dll` in de gamemap wordt dan geladen; de Mono-runtime van de game start).

Log van de game (`Gun_*.log` in de gamemap): "ERROR: 1001: Base not found". Dat is de lichtgun-basis (echte hardware). Beste vermoeden: de Unity-game verwacht via de patch (Doorstop/Harmony) of via de TeknoParrot-core een emulatie van het I/O-bord die hier niet aankomt (de JVS-pipe wordt niet geopend). Volgende stap: de Doorstop-assembly en de Unity-uitvoer bekijken (output_log in `Game_Data`), en nagaan of er een apart emulatieprofiel of een hardware-instelling in de TP-UI nodig is.

Veranderd op de NAS (map "Tomb Raider"): `teknoparrot.ini` (nieuw), `Gun_<datum>_0.log` (nieuw), `Game.run` en `ForceReboot` (nieuwe lege bestanden), `ControllerBaseConfig.json` (herschreven door de game).

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

## Rabbids Hollywood (profiel RabbidsHollywood): werkt deels

Status: titelscherm met animatie ("Rabbids Hollywood - FREE MODE") staat op de tv. De game sluit zichzelf na ongeveer 45 s af (schone exit, "Game session ended"). JVS-pipe niet verbonden; de input loopt via de BepInEx-plugin (RHA Mouse Plugin), de game opende geen RavenGun-event-nodes (alleen TeknoParrotUi heeft ze open). Toetsen (c, KP_1) via xdotool veranderden niets.

Startcommando: `scripts/play-halo.sh --profile RabbidsHollywood`

Runner: Proton (standaard). `--view` is nodig: de mapnaam eindigt op een spatie ("Directory name invalid"). `.env`: `TP_POSTSTART_REMOTETHREAD_MS=3000` en `WINEDLLOVERRIDES=winhttp=n,b` (BepInEx/Doorstop laadt dan: "Chainloader startup complete", "Plugin Loaded"). In `BepInEx/LogOutput.log` staat geen foutmelding.

Open vraag: waarom de game na ongeveer 45 s stopt. Vermoeden: de gun-module (`Gun_*.log`: "ERROR: 1001: Base not found", dezelfde als bij Tomb Raider) of de operator/launcher-flow (GameLauncher.exe, `Game.run`/`ForceReboot`) die onder Wine niet bestaat, of een afsluitsignaal van het I/O-bord. Probeer: de operatorinstellingen (RabbidsHollywood_Operator.exe) en de vensterinstellingen uit de README aanpassen en kijken of de game blijft staan.

Veranderd op de NAS (map "Rabbids Hollywood ", via de symlink-view): `teknoparrot.ini` (nieuw), `ControllerBaseConfig.json` (herschreven), `BepInEx/LogOutput.log`, `BepInEx/config/BepInEx.cfg`, `BepInEx/cache/harmony_interop_cache.dat` en `chainloader_typeloader.dat`. Nieuwe bestanden in de view zelf (bv. `Gun_*.log`) blijven lokaal in `~/.local/share/ravengun/runtime/views/RabbidsHollywood`.

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
