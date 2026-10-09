# Games in TeknoParrot: status na de tweede ronde (2026-10-09)

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
| Transformers: Shadows Rising (TransformersShadowsRising) | beeld en attract goed; besturing werkt niet (zie derde ronde); snelkoppeling verwijderd | `--profile TransformersShadowsRising` (prefix-kopie met native DLL's, zie onder) |
| Time Crisis 5 (TC5) | start direct de echte game: attract-scherm "INSERT 1 CREDIT(S)"; invoer nog niet verbonden | `--profile TC5` (plain wine) |
| Tomb Raider (TombRaider) | beeld, "IO BOARD ERROR"; geparkeerd (eigen route met de daemon) | |
| Rabbids Hollywood (RabbidsHollywood) | titelscherm, stopt na ongeveer 45 s, knoppen werken niet; geparkeerd (eigen route met de daemon) | |
| Jurassic Park (JurassicPark) | nog niet: zwart/wit beeld; geparkeerd | |
| Luigi's Mansion (LuigisMansion) | geparkeerd: VACUUM.exe stopt na enkele seconden | |
| Let's Go Safari (LGS) | geparkeerd: proces hangt zonder venster | |

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
