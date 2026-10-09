# DemulShooter-integratie voor Tomb Raider en Rabbids Hollywood

Onderzoek van 2026-10-09, alleen via WebFetch/WebSearch. Label per bewering: [bron] = uit broncode of pagina gelezen, [afgeleid] = door mij afgeleid of niet rechtstreeks gezien. Basis-URL voor code: https://github.com/argonlefou/DemulShooter (branch master; laatste push volgens de GitHub-API 2026-09-22).

## 1. Licentie

- De GitHub-API geeft voor de repo `license: null`. De rootmap bevat alleen .editorconfig, .gitignore, DemulShooter.sln, README.md en de mappen DemulShooter, DemulShooterX64, DemulShooter_GUI, DsCore, DsDiag, UnityPlugins. Geen LICENSE- of COPYING-bestand. [bron: api.github.com/repos/argonlefou/DemulShooter en .../contents/]
- README en wiki-startpagina bevatten geen licentietekst. Het bronbestand DemulShooter_Plugin.cs van het TRA-plugin heeft geen licentie- of copyrightkop, het .csproj ook niet. [bron]
- Gevolg (juridisch, geen advies): zonder licentie geldt standaard auteursrecht, dus code overnemen of herdistribueren mag niet zonder toestemming van argonlefou. [afgeleid]
- Aanbevolen: clean-room. Het protocol (poort, bytevolgorde, velden) is een feitelijk interface en we implementeren het zelf. Kopieer geen code. Voor de bestaande plugin-DLL's ongewijzigd gebruiken: zonder licentie ook niet meeleveren; laat de gebruiker ze zelf uit de DemulShooter-release halen (zoals de wiki doet) of vraag de auteur schriftelijk om toestemming (BYOAC-forum). [afgeleid]
- Niet nagegaan: de licenties van BepInEx en Harmony (door de plugin gebruikt), en of de Release-zips een eigen licentietekst hebben. [niet gevonden]

## 2. Architectuur voor Tomb Raider (TRA) en Rabbids Hollywood (RHA)

De plugin zit in de repo onder UnityPlugins/UnityPlugin_BepInEx_TRA en UnityPlugin_BepInEx_RHA. Er is geen aparte plugin-repo. [bron: mapindeling]

Wat de plugin in de game doet [bron: UnityPlugin_BepInEx_TRA/DemulShooter_Plugin.cs, Patch/mInputManager.cs, Patch/mKaboomManager.cs, Patch/mKaboomAdrioFxPlusFourFeeders.cs; RHA: Patch/mNewInputManager.cs]:

- BepInEx-plugin (GUID argonlefou.demulshooter.tombraider resp. ...rabbidshollywood, versie 17.0.0.0, .NET 3.5, `harmony.PatchAll()` in `Awake()`).
- `KaboomAdrioFxPlusFourFeeders._Initialize`: prefix die `false` teruggeeft, zodat de game-bibliotheek niet naar IO-apparaten zoekt of schrijft. Dit is vrijwel zeker de plek die onder TeknoParrot als "IO BOARD ERROR" faalt. [afgeleid voor de koppeling met de foutmelding]
- `KaboomManager.Update`: prefix die `false` teruggeeft en zelf toetsen omzet naar game-events: munt (Coin-toets per speler naar `KaboomOnCoinEvent`), start (`BUTTON_PLAYERn_B1_PRESS/RELEASE`), test/menu via `KaboomOnKeyboardEvent`, Escape sluit de game af. Het IO-board wordt dus door toetsenbordinvoer vervangen. Standaardtoetsen: 1 t/m 4 start, 5 t/m 8 munt, 0 test, pijlen en Enter menu (wiki en `PluginController`: Start = keycode 49+ID, Coin = 53+ID).
- `KaboomManager.StartMotorPwm`: prefix die doorloopt en `OutputData.Recoil[speler]=1` zet (trilling/recoil als flag).
- `InputManager.MouseControl` (TRA) resp. `NewInputManager.MouseControl` (RHA): prefix die `false` teruggeeft en de gun-invoer vervangt: `PlayerMoveCrosshair`, `PlayerPressTrigger`, `PlayerShoot`, `PlayerTriggerRelease`, `PlayerReload` (TRA). Positie wordt genormaliseerd met `x / Screen.width` en `y / Screen.height`.
- Zonder verbinding valt de plugin terug op muis (links = trekker, rechts = herladen) en toetsenbord. Pas bij een TCP-verbinding wordt `EnableInputHack` waar. [bron]
- Andere patches in de map (mCrosshairWindow, mLEDManager, mPlayer, mPlayerManager, mCheatsManager, mOperatorMenu, SBK/Encryption, TRA/Timeline/Skip): niet gelezen. [niet gevonden]

Kanaal [bron: DemulShooter_Plugin.cs, DsCore/IPC/DsTcp_Client.cs, DsTcp_TcpPacket.cs, DsTcpData.cs, TcpPacket.cs, TcpData.cs]:

- Geen gedeeld geheugen, geen named pipe, geen WriteProcessMemory voor deze games. Het is TCP op `127.0.0.1:33610`. De plugin is de server (`TcpListener`, een client tegelijk), DemulShooterX64 is de client. De commentaarregel "8052" in de code is verouderd; de code gebruikt 33610.
- Richting plugin naar DemulShooter (outputs): frame = 4 bytes lengte (int32, native little-endian, waarde = payload + 1) + 1 byte header (2 = Outputs) + payload.
- Richting DemulShooter naar plugin (inputs): DemulShooter schrijft `_InputData.ToByteArray()` rechtstreeks, zonder lengte of header (`Game_ArcadepcTra.SendInput` roept `_Tcpclient.SendMessage` aan, `SendMessage` voegt volgens de code niets toe). De plugin leest met een `Read` van max. 1024 bytes en geeft die ongewijzigd aan `_InputData.Update(...)`, zonder hersamenstelling. Eén bericht = één `write` van vaste lengte. Header-waarde 1 (Inputs) bestaat in de enum maar lijkt hier niet gebruikt. [bron, met de conclusie uit twee bestanden samengesteld]
- Payloadserialisatie (`TcpData`): per object alle publieke velden, gesorteerd op veldnaam, zonder padding, arrays zonder lengteprefix (lengte = spelersaantal, 4), little-endian via BinaryWriter. [bron: TcpData.cs; endianness is .NET-standaard, niet in de code vermeld]
- Afgeleide bytelay-out TRA input (42 bytes): Axis_X float[4] (16), Axis_Y float[4] (16), EnableInputsHack byte, HideCrosshairs byte, Reload byte[4], Trigger byte[4]. Velden uit `TcpInputData.cs` van TRA, volgorde afgeleid uit "sorteren op naam". [afgeleid; sorteervolgorde met cultuurgevoelige OrderBy niet geverifieerd, hier is het gelijk aan ASCII]
- RHA input (38 bytes): hetzelfde zonder Reload. [afgeleid: velden uit RHA/TcpInputData.cs en Game_ArcadepcRha.cs]
- Output TRA (60 bytes, plugin naar DemulShooter): Ammo int[4], Credits int[4], Damaged byte[4], IsPlaying byte[4], Life float[4], Recoil byte[4]. RHA heeft StartLamp in plaats van IsPlaying. [afgeleid uit TcpOutputData.cs]
- Waarden: Trigger/Reload 1 = ingedrukt, alles anders = los (`PluginController.SetButton`). Updatefrequentie input: geen vaste klok; DemulShooter stuurt per verwerkt invoerevent (`SendInput`); de plugin past alles bij elke ontvangst toe. Outputs: alleen bij wijziging, gecontroleerd per Unity-frame, plus bij verbinden; `Recoil` en `Damaged` zijn eenmalige flags die na verzenden op 0 gaan. Geen keepalive. [bron]
- Coordinaten: de plugin gebruikt `Axis_X/Axis_Y` als schermpixels van het game-venster en deelt zelf door `Screen.width/height` (0..1). Of Y van onderen (Unity, `Input.mousePosition`) of van boven telt is niet zichtbaar in de gelezen bestanden; omdat de muisterugval `Input.mousePosition` dezelfde route gebruikt is oorsprong linksonder aannemelijk. Dit moet in een test worden vastgesteld. [afgeleid/onzeker]
- Outputs (wat DemulShooter terugkrijgt): Life, Ammo, Recoil, Damaged, Credits per speler; alleen als IsPlaying=1. DemulShooter zet ze om naar MameHooker-outputs. De daemon heeft hier enkel Recoil nodig (trillen). [bron: Game_ArcadepcTra.cs]
- Verdere details uit de wiki: doelnaam is `-target=arcadepc -rom=tra` (en `-nocrosshair`), niet "aagames" zoals de opdracht noemde; de plugin-installatie loopt via DemulShooter_GUI (pagina Unity Plugin Installation); INI `TombRaider_BepInEx_DemulShooter_Plugin.ini` met WIDTH/HEIGHT/FULLSCREEN/FORCE_RESOLUTION en toetsen. [bron: wiki "Arcade PC | Tomb Raider Arcade"]. De sindenwiki noemt extra stappen (game.reg, TombRaider_Operator.exe, geen TeknoParrot trigger-fix, DemulShooter minimaal v10.9.1) [bron: zoekresultaat sindenwiki.org/wiki/Tomb_Raider_Arcade; pagina zelf niet geopend].
- Game__Unity.cs: DemulShooter wacht op proces "Game", vergelijkt de venstertitel (TRA "SquareEnix_TombRaider", RHA "RabbidsShooter") en controleert de MD5 van Assembly-CSharp.dll; alleen bij een bekende dump (clean of gepatcht) verbindt hij. Dat is een DemulShooter-controle, onze daemon hoeft dat niet te doen. [bron]

## 3. Hoe DemulShooter de gun leest

- RawInput-klasse in DsCore/RawInput levert per speler `Computed_X`, `Computed_Y` en `Computed_Buttons` (flags `OnScreenTriggerDown/Up`, `OffScreenTriggerDown/Up`). Voor TRA gaat `Computed_X/Y` zonder verdere schaling door naar de plugin; het is dus al in clientpixels van het gamevenster. Reload = trigger buiten het scherm. [bron: Game_ArcadepcTra.SendInput]
- De kalibratie (muisresolutie van de gun, schermresolutie, venstergrootte) zit in DsCore/RawInput en Config; die bestanden zijn niet gelezen. [niet gevonden]
- Voor ons niet relevant: onze daemon heeft al absolute coordinaten en eigen kalibratie en weet dus zelf welke pixel de aim is. Alleen de schermresolutie van het gamevenster is nodig. [afgeleid]

## 4. Gevolgen voor Linux/Wine en opties

Omdat het kanaal gewoon TCP op loopback is, valt optie (a) af te leiden als werkbaar: Wine zet winsock-sockets om in echte Linux-sockets, dus de plugin in de game luistert op de Linux-loopback en onze daemon kan met een gewone `connect()` naar 127.0.0.1:33610. [afgeleid; niet getest, zeker te controleren met `ss -ltnp` tijdens de game]

- Optie (a): onze daemon wordt de client van de bestaande plugin (het DemulShooter-protocol). Omvang: ongeveer 150 tot 250 regels C++ (verbindmanager met herverbinden, serialisatie van 42 bytes, framing-parser voor 60 bytes aan outputs, recoil naar de rumble). Grootste voordeel: Tomb Raider en Rabbids werken met één kleine toevoeging en geen C#-code. Nadeel: de DLL's van de plugin zijn niet vrij herdistribueerbaar (licentie) en moeten in het BepInEx-formaat al aanwezig zijn; koppeling aan DemulShooter-versies (veldnamen bepalen het formaat, dus een volgende plugin-versie kan het formaat veranderen).
- Optie (b): eigen minimale BepInEx-plugin in C#. Omvang: ongeveer 300 tot 500 regels (listener of bestand, Harmony-prefixes op `KaboomAdrioFxPlusFourFeeders._Initialize`, `KaboomManager.Update`, `StartMotorPwm`, `InputManager.MouseControl`). Vrij van licentieproblemen alleen als het echt zelf geschreven is (clean-room: namen van gepatchte klassen zijn feiten, de code zelf kopiëren niet). Vereist een .NET-3.5-toolchain en de Assembly-CSharp.dll van de game als referentie, en we moeten de interne API van de game zelf uitzoeken. Hoger risico en onderhoud, maar volledige controle (bijvoorbeeld direct een Unix-vriendelijk kanaal zoals een bestand in `Z:\dev\shm`).
- Optie (c): Wine-side brug: niet nodig omdat TCP al doorkomt; alleen relevant bij een named pipe of gedeeld geheugen, wat hier niet speelt. [afgeleid]
- Onder Wine moeten BepInEx en doorstop werken: normaal via een `winhttp.dll`-override (`WINEDLLOVERRIDES="winhttp=n,b"`). Dat is algemene kennis en niet in de gelezen bronnen bevestigd; moet getest worden. [afgeleid/onzeker]
- Open vraag: draait Tomb Raider zelf onder Wine/Proton zonder TeknoParrot, nu de loader-injectie niet nodig is? Bij ons faalde alleen de IO; de Unity-game is door de eigenaar wel gestart. Niet getest in dit onderzoek.

## 5. Dekking voor andere games van de eigenaar

Alleen via bestandsnamen en bronnen van de Game_*.cs; steekproefsgewijs gelezen.

- Tomb Raider en Rabbids Hollywood: BepInEx-plugin met TCP (dit document). [bron]
- Jurassic Park (Raw Thrills, Game_RtJurassicPark.cs, 32-bit, proces BudgieLoader.exe): geen invoerkanaal; het schrijft met WriteProcessMemory/code caves (E9-jmp) vlaggen voor recoil en damage en leest geheugen voor outputs. De aim loopt hier kennelijk via normale invoer. [bron; lees "invoer" als mijn conclusie]
- Terminator Salvation (Game_RtTerminatorSalvation.cs): zelfde BudgieLoader.exe, code cave van 0x800 bytes plus JMP op 0x0811FCF2 voor recoil; "TeknoParrot emuleert het gun-board niet". [bron]
- Luigi's Mansion Arcade (DemulShooterX64/Games/Game_NuLuigiMansion_v2.cs): proces `vacuum`, aim wordt direct in het geheugen van het proces geschreven (WriteProcessMemoryX64) met schaling naar 1920x1080, knoppen via x64-code caves (FF 25-sprong), outputs uit pointerketens. [bron]
- Time Crisis 5 (Game_Es3Tc5.cs): proces `TimeCrisisGame-Win64-Shipping`, code caves plus `WriteProcessMemoryX64`, twee modi (standalone of JVS bij `rslauncher`). [bron]
- Transformers Shadows Rising: er is een `Game_RwTransformers.cs` (Transformers Human Alliance, RingWide, geheugenschrijven + 5-byte-jmp). Welk bestand Shadow Rising dekt, is niet vastgesteld; wiki-indeling noemt Shadow Rising onder RingEdge 2. [onzeker]
- Halo Fireteam Raven en Let's Go Safari: niet gevonden in de wiki-indeling of in de bestandsnamen die ik zag. Let's Go Jungle (Lindbergh) staat er wel, dat is een ander spel. Let's Go Safari mogelijk onder een niet getoond deel van de lijst. [niet gevonden]
- Conclusie: alleen de Unity-games (TRA, RHA en andere "UnityPlugin_BepInEx_*": DCOP, DINOINV, DRK, MARSS, MECHADINO, MECHATOR, MIA, MIB, NHA2, OWR, PBX, PVZ, SKOS, WWS, WZ) delen het TCP-plugin-mechanisme. De andere games van de eigenaar vallen onder geheugenhacks die specifiek zijn voor offsets van een bepaalde exe en dus niet via dit kanaal te dekken zijn. [afgeleid]

## Aanbevolen ontwerp voor RavenGun

1. Eerst optie (a) als stap 1: een `UnityBridge` in de daemon (loopback-TCP-client op 33610, herverbinden elke 1 s, geen tight loop zoals DemulShooter zelf doet). Per aim-tik (liefst gekoppeld aan de 250 Hz-klok, maar begrensd tot bv. 120 Hz) een inputframe van 42 bytes (TRA) of 38 (RHA) sturen: aim als pixels in het gamevenster, trigger bij B/trekker, reload bij trekker buiten het scherm (of aparte knop). Frames voor outputs parsen en `Recoil[0]` naar de trilling doorgeven. Munt, start en test via het bestaande virtuele toetsenbord (toetsen 5 en 1 voor speler 1; 0 voor test).
2. Licentie: alleen protocolfeiten gebruiken, geen DemulShooter-code. DLL's niet meeleveren; installatie-instructie die de gebruiker de plugin laat plaatsen vanuit zijn eigen DemulShooter-download. Vraag argonlefou via de BYOAC-forumthread om expliciete toestemming (herdistributie en/of protocolgebruik) voordat we iets publiceren.
3. Optie (b) alleen als fallback of als de licentie een probleem blijkt: dan eigen plugin met hetzelfde of een eenvoudiger protocol.
4. Eenvoudigste en meest robuuste route: (a), ongeveer 200 regels C++ en geen C#-toolchain. Risico's: plugin-formaat kan wijzigen (veldnamen bepalen het formaat), de volgorde van de velden is afgeleid en moet met een byte-dump tegen de echte plugin worden gecontroleerd, BepInEx/winhttp onder Wine, Y-oorsprong, DemulShooters MD5-controles gelden alleen voor DemulShooter zelf.
5. Test: start de game met plugin onder Wine, kijk met `ss -ltnp` naar poort 33610, stuur een testframe met `nc`/Python (aim in het midden, trigger 1) en bevestig dat de crosshair beweegt, voordat de C++ wordt gebouwd.

## Wat ik niet kon vinden

- Licentie van de plugin-DLL's in de release-zips en eventuele toestemming van de auteur.
- De RawInput-kalibratiecode (DsCore/RawInput, Config), de overige Harmony-patches en `PluginController.SetAimingValues`-gebruik van de Y-as.
- Exacte bytelay-out gecontroleerd tegen een echte verbinding (alleen afgeleid uit de bronvelden).
- Of BepInEx onder Wine/Proton in deze setup laadt.
- Dekking voor Halo Fireteam Raven, Let's Go Safari en Transformers Shadows Rising.
- De sindenwiki-pagina's voor Rabbids/Tomb Raider (403 of niet geopend).

## Geverifieerd met een echte verbinding (2026-10-09, DemulShooter v17.9, Wine/GE-Proton)

Label: [live] = met de echte plugin in de echte game gemeten, [log] = uit `BepInEx/LogOutput.log` van de plugin zelf.

- Kanaal: onder GE-Proton luistert de plugin op `127.0.0.1:33610` (zichtbaar met `ss -ltnp`, proces wineserver) en een gewone Linux-`connect()` werkt. De plugin is de server, een client tegelijk; hij stuurt bij het verbinden meteen een uitvoerbericht. [live]
- Invoer (client naar plugin), zonder lengte of kop, little-endian: Tomb Raider 42 bytes = Axis_X f32[4] (0..15), Axis_Y f32[4] (16..31), EnableInputsHack u8 (32), HideCrosshairs u8 (33), Reload u8[4] (34..37), Trigger u8[4] (38..41). Rabbids 38 bytes: hetzelfde zonder Reload (Trigger 34..37). De veldvolgorde is alfabetisch en staat ook in het plugin-log (regels "Creating System.Single [4] Axis_X", enz.). EnableInputsHack moet 1 zijn; de plugin reageert dan op de pakketten (anders valt hij terug op muis en toetsenbord). [live, log]
- Schaal: Axis_X en Axis_Y zijn pixels van het gamevenster (0 tot breedte-1 en 0 tot hoogte-1 bij 1920x1080). Gemeten: u=0,25 geeft een vizier op x=480 van 1920 (Tomb Raider) en u=0,3 op x=576 (Rabbids). [live]
- Y-oorsprong: onderaan (Unity: `Input.mousePosition`). Y = (1 - v) * (hoogte-1) met v = 0 bovenaan; v=0,25 geeft het vizier op 270 pixels van de bovenrand (Tomb Raider), v=0,3 op 324 (Rabbids). [live, beide games]
- Trekker: byte 1 is ingedrukt. Tomb Raider: de trekker slaat de filmpjes over ("Hold Trigger to Skip") en schiet (tien schoten uit een magazijn van tien met recoil 10 en ammo 10 naar 0); Reload (byte 34+speler) laadt het magazijn bij (ammo weer 10). Rabbids: de trekker schiet (recoil loopt op bij ingedrukt houden). [live]
- Munt en start: de plugin leest echte toetsen. Toets 5 tot 8 = munt speler 1 tot 4 (Tomb Raider: een munt telt in kwarten, "1/4 CREDITS"; de speler heeft bij een nieuw spel al credits), 1 tot 4 = start. Tomb Raider: toets 6 en 7 gaven Credits[1] en Credits[2] elk +1 in de uitvoer. Rabbids: viermaal toets 5, dan toets 1 start het spel (credits 4, daarna 0, StartLamp van speler 1 aan). De toetsen komen via een gewoon virtueel toetsenbord; Unity onder Wine ziet ze zodra het venster focus heeft. [live]
- Uitvoer (plugin naar client): int32 lengte (61), u8 koptype 2, 60 bytes. De veldvolgorde verschilt per game omdat ze alfabetisch is: Tomb Raider Ammo i32[4], Credits i32[4], Damaged u8[4], IsPlaying u8[4], Life f32[4], Recoil u8[4]; Rabbids Ammo, Credits, Damaged, Life, Recoil, StartLamp (StartLamp achteraan). Eerste bericht bij het verbinden Tomb Raider: ammo 10, life 100,0, credits 6. [live, log]
- Recoil: een bericht met Recoil[p] != 0 is een schot van speler p; ze komen per schot (tien keer bij tien schoten). [live]
- Levensduur: na het sluiten van een probe-verbinding (de probe stopt) accepteert de plugin meteen een nieuwe klant; een nieuw bericht met de actuele stand komt bij elke nieuwe verbinding. Het gedrag als de game zelf sluit (de plugin verdwijnt, `recv` geeft 0 of een fout) is alleen met de nep-plugin in de tests gecontroleerd.
- Het plugin-log zet BepInEx-schrijven standaard uit (`[Logging.Disk] Enabled = false` in de release); `scripts/aagames-setup.py` zet het in de lokale kopie aan.
- Wat de releases bevatten: `Unity/TombRaider` en `Unity/RabbidsHollywood` hebben elk een eigen BepInEx 5.4.21 (core, config, cache), `winhttp.dll` (doorstop) en `doorstop_config.ini` (`targetAssembly=BepInEx\core\BepInEx.Preloader.dll`), plus `<Game>_BepInEx_DemulShooter_Plugin.dll` en `.ini` (WIDTH, HEIGHT, FULLSCREEN, FORCE_RESOLUTION, toetsen, SAVE_TO_GAME_FOLDER). De Tomb Raider-map op de NAS heeft een eigen TeknoParrot-doorstop (`Doorstop.dll`, `0Harmony.dll`) en de Rabbids-map een TeknoParrot-BepInEx met `UnityPlugin_BepInEx_RHA.dll`; die worden niet gebruikt.
