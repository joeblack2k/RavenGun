# Zonder TeknoParrot: bouwstenen per game (onderzoek, 2026-10-09)

Methode: alleen lezen. WebSearch/WebFetch voor de projecten, lokaal read-only voor de dumps onder `~/mnt/spidernas_emulation/Teknoparrot/` (objdump -p, strings, bestandslijsten). Niets gedownload, geïnstalleerd, gestart of gewijzigd. Markering: [bron] = uit een echt geopende pagina of bestand gelezen, [lokaal] = uit de dumps of de eigen machine gelezen, [afgeleid] = afgeleid of onzeker. Een URL staat er alleen als ik hem echt heb geopend; waar een pagina niet te openen was (403/402) of ik alleen een zoekresultaat zag, staat dat erbij.

Kernconclusie: de lijst van de eigenaar bevat maar twee projecten waarvan we de code mogen hergebruiken en die iets met zijn games te maken hebben, en geen van beide dekt een van de negen games. Segatools (Unlicense) dekt Sega Nu/ALLS-ritmegames, maar niet Luigi's Mansion. Lindbergh Loader (niet-commerciële licentie) is voor deze games niet van toepassing. JConfig en RConfig zijn gesloten binaries zonder broncode die ik kon vinden. Micetools is onvolledig en heeft geen vastgestelde licentie. OpenParrot (GPL-3) is geen losse loader. De Raw Thrills-Linuxgames hebben geen open-source route. De winst zit dus niet in "alles erin stoppen", maar in één eigen IO-brug in de daemon plus per game een dunne eigen hook of proxy-DLL. Dat is wat de Unity-route al bewijst.

## 1. Overzicht per game

Platformen volgens de dumps [lokaal]: Luigi's Mansion = Sega Nu (`platform-id = AAV` in `oxGetHwInfo.ini`). Let's Go Safari = Sega RingWide (`board-type = RINGWIDE_RICOH | RINGWIDE_ADVANTECH`, `OSVersion = 0045.03.01` in `mxGetHwInfo.ini`). Transformers: Shadows Rising = Sega RingEdge 2 [bron: tfwiki.duckdns.org, "Sega RingEdge 2", ontwikkelaar Sega of China; DemulShooter-wiki Ringedge-2 noemt het target `ringedge2`]. Time Crisis 5 = Namco ES3. Het is dus geen ALLS/Nu en niet Lindbergh.

1. Halo: Fireteam Raven (Raw Thrills, Linux x86-64). Nu: TeknoParrot ElfLdr2, werkt. Zonder TP: alleen de eigen native route (bubblewrap, `halo.so`, FMOD 1.10 ontbreekt, FTDI-IO-bord is niet geëmuleerd). Bouwstenen: geen open source gevonden; DemulShooter heeft geen Halo-pagina. Licentie: n.v.t. Moeite: hoog (het FTDI-protocol van het IO-bord moet worden achterhaald, de herkomst van `halo.so` is onbekend). Risico: hoog. Advies: voorlopig op TP laten; pas na een succes bij Jurassic Park native hergebruiken.

2. Jurassic Park (Raw Thrills 2015, 32-bit ELF). TP geeft zwart beeld. Beste experiment: native draaien met dezelfde sandbox als Halo, omdat het zwarte beeld volgens ons eerdere onderzoek een Wine/X11-kwestie lijkt (geen GL-context onder Wine). Bouwstenen: Steam Linux Runtime (i386-bibliotheken, zie D), eigen shims. Moeite: hoog tot zeer hoog. Risico: hoog, onbekend gun-protocol. Het is wel de enige route met een kans op beeld.

3. Terminator Salvation (Raw Thrills 2010, 32-bit ELF). Werkt al via TP plus gamescope. Native is dezelfde klus als bij Jurassic Park (zelfde `cmgr`/FTDI/Hardlock-patroon). Geen haast.

4 en 5. Tomb Raider en Rabbids Hollywood: klaar via de eigen route (BepInEx-plugin van DemulShooter v17.9, TCP 33610). Open punt: de licentie van de plugin (zie §2, DemulShooter) en het besluit voor een eigen plugin.

6. Luigi's Mansion Arcade (Sega Nu, `VACUUM.exe` x64, D3D11). Geen open-source loader gevonden die dit dekt. Segatools noemt de game niet. Bronnen zeggen dat het met TeknoParrot werkt en dat JConfigLM crasht. Moeite: zeer hoog (een eigen Nu-platformlaag: `oxGetHwInfo`, I/O-bord `LMAIO.bin` via COM1, drives X: en Y:, keychip, netwerk). Advies: laatste in de rij, bij voorkeur TP onder Wine blijven proberen.

7. Let's Go Safari (RingWide). Geen loader gevonden die het dekt. Micetools mikt op "Ring series" maar is onvolledig. JConfig heeft per-game patches voor verwante titels (Let's Go Island). Moeite: zeer hoog. Aanwijzing: de exe importeert DINPUT8, WS2_32 en SETUPAPI [lokaal], dus invoer loopt via DirectInput en apparaatopsomming. Eerst uitzoeken waarom het proces zonder venster hangt.

8. Time Crisis 5 (Namco ES3). Start, invoer niet. Beste kandidaat voor een eigen IO-DLL: `wajvio.dll` heeft een kleine, bekende API van 18 exports. Alternatief: de muisroute uit de bijgeleverde README. Moeite: laag tot middel. Risico: middel.

9. Transformers: Shadows Rising (RingEdge 2). Werkt, gun-assen niet goed. De game zoekt zijn IO-bord als HID-apparaat (zie §3) en heeft een eigen kalibratiescherm voor de gun-assen. Moeite: laag. Dit is de snelste winst.

## 2. Per project: details, licentie, volwassenheid

Segatools.
- Bron: https://github.com/djhackersdev/segatools (README, geopend). [bron] Loaders en hardware-emulators voor Sega-games op Nu en ALLS, versie v005; genoemde games Chunithm-varianten en Initial D Zero; licentie Unlicense; gearchiveerd 19 okt 2024.
- Bron: https://gitea.tendokyu.moe/Hay1tsme/segatools. [bron] "no longer maintained", gearchiveerd 2024-12-27, Unlicense; mappen onder meer `amex`, `board`, `jvs`, `platform`, `hooklib`, `minihook`, `aimeio`, `iccard`, `pki`, plus per game `*hook` en `*io`. In `jvs/` staan JVS-bus, -framing en -dispatch; in `chuniio/` staan `chuniio.c/h` en config (de inhoud van de headers is niet gelezen). De doc `development.md` noemt als doel: Sega-arcadegames op willekeurige hardware draaien, vereiste software en hardware emuleren, en een API voor eigen IO-interfaces bieden.
- Bron: https://gitea.tendokyu.moe/TeamTofuShop/segatools. [bron] Actief voortgezet door TeamTofuShop (README-datum 2026-04-06). Games: ALL.Net P-ras MULTI v3, Card Maker, CHUNITHM, crossbeats REV., Eiketsu Taisen, Fate/Grand Order Arcade, Project DIVA, Initial D, Kemono Friends 3, maimai DX, Mario & Sonic Tokyo 2020, O.N.G.E.K.I., Sangokushi Taisen, SEGA World Drivers Championship 2019, WACCA. Luigi's Mansion staat er niet bij. Veel forks op https://gitea.tendokyu.moe/explore/repos?q=segatools (geopend).
- Betekenis [afgeleid]: de licentie (Unlicense) staat code overnemen toe zonder voorwaarden. Maar er is geen lightgun-ondersteuning en geen Luigi's Mansion. Waarde voor ons: het architectuurvoorbeeld (een dunne hook-DLL per game plus een losse IO-DLL met een eenvoudige C-API) en de JVS-framing als referentie. Alleen principe en eventueel de JVS-code lenen.

Micetools.
- Bron: https://gitea.tendokyu.moe/Bottersnike/micetools (en de submap `src/micetools/micekeychip`). [bron] Doel: een volledige emulatielaag voor "Ring series"-hardware. Onderdelen genoemd: Micekeychip (keychip-emulator voor operators met een fysieke kast; builds op verzoek van de auteur) en onderzoekstools. "Heavily in-dev", alleen nightlies, laatste getoonde commit 2023-08-30. De README noemt geen licentie (verwijst naar een licentiebestand dat ik niet kon openen, 404). De README van micekeychip zegt letterlijk dat het "the most incomplete keychip emulator" is. Niet genoemd: mxdrivers, JVS, RingWide, spellijst.
- Bron: https://sega.bsnk.me/ringedge/manual/keychip/. [bron] Noemt micekeychip als open-source maar onvolledig; noemt ook `mxbadkey` als goed werkende emulator die je bij de auteur moet opvragen.
- Betekenis [afgeleid]: licentie onbekend, dus niet overnemen. Niet bruikbaar als bouwsteen voor Let's Go Safari. Voor Transformers (RingEdge 2) geen aanwijzing gevonden dat het nodig is.

Lindbergh Loader.
- Bron: https://github.com/lindbergh-loader/lindbergh-loader. [bron] Sega Lindbergh-emulator voor Linux, JVS-doorgave, surround, netwerk; licentie Creative Commons BY-NC-SA 4.0 met de extra regel dat code niet in betaal-, abonnements- of commerciële projecten gebruikt mag worden zonder toestemming. Draait via `./lindbergh` en `lindbergh.conf`/`controls.ini`.
- Betekenis [afgeleid]: geen enkele game van de eigenaar is een Lindbergh-game (Let's Go Safari is RingWide, een Windows-exe; Lindbergh heeft Let's Go Jungle). De licentie is bovendien niet-commercieel en ShareAlike: alleen als los proces voor de eigenaar zelf, niet in onze code.

OpenParrot.
- Bron: https://github.com/teknogods/OpenParrot. [bron] "Open Source version of TeknoParrot", werkt "in collaboration with TeknoParrotUI"; licentie GPL-3.0; mappen OpenBanapass, OpenParrot, OpenParrotKonamiLoader, OpenParrotLoader, deps, iDmacDrv; bouwt met Visual Studio 2019 en premake; nieuwe games vereisen een signatuur in `GameDetect.cpp` en TeknoParrotUi-profielen.
- Betekenis [afgeleid]: niet los te gebruiken. Het bevat niet de gesloten kernen (ElfLdr2, Budgie) die Raw Thrills en de meeste van onze games nodig hebben. GPL-3 is copyleft: code overnemen maakt RavenGun GPL-3; een apart proces is wel mogelijk. Beste waarde: leesvoorbeeld voor de wijze van hooken.

DemulShooter.
- Al onderzocht (`docs/research/demulshooter-integratie.md`). Aanvullend gelezen: de wiki-pagina's Namco-ES3, SEGA-Nu, Raw-Thrill, RingWide, Ringedge-2, Windows-games, Lindbergh en Home, via https://github-wiki-see.page/m/argonlefou/DemulShooter/wiki/ (spiegel), en https://hotr.6bolt.com/pmwiki.php/GameFiles/DemulShooter. [bron] Geen licentie. Voor de TeknoParrot-games (Raw Thrills, Sega Nu, Ringedge 2, RingWide) geeft DemulShooter alleen outputs; TP leest zelf de invoer (RawInput). Targets: `seganu -rom=lma`, `ringedge2 -rom=tsr`, `es3 -rom=tc5`, `rawthrill -rom=jp|ts`. Halo staat er niet in. Let's Go Safari staat niet in de spellijst (alleen Island en Jungle).

JConfig en RConfig.
- Bron: https://github.com/argonlefou/DemulShooter wiki Namco-ES3 en RingWide, https://www.insertmorecoins.es/en/gameloader-all-rh-ultimate-guide/, https://gist.github.com/SeongGino/92c5222d0baaf235332e09d2522e76db. [bron] JConfig is een "control configurator" die per game wordt geleverd als aangepaste bestanden (bij Time Crisis 5 uit onze README: opties als Delay, "CreateThread for Joysticks", DInput of XInput, POV/analoog-assen). RConfig is volgens de gids de opvolger die "Use JConfig" vervangt. Beide worden "per-game patchers" genoemd, "mostly unmaintained", soms met aangepaste executables [bron: Seong-gist]. Voor RingWide kan JConfig de SYSTEM- en TEST-wijzigingen niet opslaan [bron: DemulShooter RingWide]. Bij Luigi's Mansion crasht JConfigLM en werkt TP [gist]. Bij Time Crisis 5 "boot de JConfigTC5-patch met gepatchte RSLauncher niet", maar de Shipping-exe direct starten werkt, met vertraagd muisrichten [gist].
- Broncode: geen repository gevonden. [afgeleid] Gesloten binaries die in verspreidingsbundels zitten; juridisch en technisch geen bouwsteen. Wel nuttig als gedragsreferentie: ze bewijzen dat JVS en IO per game met een kleine DLL te vervangen zijn.

Spice2x/SpiceTools.
- Bron: https://github.com/spice2x/spice2x.github.io. [bron] GPL-3.0, voortzetting van SpiceTools, voor Konami-hardware. Niet relevant voor de negen games; Linux wordt in de README niet genoemd.

Ander.
- OpenJVS (een Linux-JVS-emulator voor echte kasten) en YetAnotherJVSEmu kwamen als zoekresultaat voorbij; niet geopend. [afgeleid] Voor onze games waarschijnlijk niet relevant, omdat wij geen JVS-seriële bus hebben.
- De zoektool weigerde een deel van mijn zoekopdracht over het omzeilen van kopieerbeveiliging (dongles, keychips). Ik heb daarom alleen architectuur en licenties onderzocht en geen details over het omzeilen. Aanbevolen: dongle- of keychip-emulatie niet in de app meeleveren; het eigenaarschap van `halo.so` is onbekend.

## 3. Technisch per game: wat wordt gehookt, hoe komt de gun binnen

Raw Thrills-ELF (Halo, Jurassic Park, Terminator). [lokaal] De binaries tonen `/dev/Hardlock` (dongle), `ftdi_*` en `libusb_*` (in het binary zelf, niet als DT_NEEDED), `cmgr_*` en `pmrt_umc_libusb_*` (Halo: "Did not find exactly one FTDI chip"), `/dev/ttyS0`, `/dev/rtc`, `/dev/dsp`; Terminator ook `/dev/js0` en `/g3/irgun/cmgr%06i.txt`. De dumps bevatten `irgun/cmgr*.txt`, `gunrec_update` en firmwarebestanden, en Jurassic Park bevat een `utils/pmrtdata/libhasp_linux_*.so`-reeks (de echte Sentinel-runtime, geen emulator). Conclusie [afgeleid]: de guns hangen op FTDI-USB-borden en de game praat daar zelf mee. Omdat libusb en ftdi in het binary zitten, werkt een gewone LD_PRELOAD-interpositie daarop niet; je moet functies in het binary inline hooken (zoals `halo.so` met capstone/plthook doet) of USB op OS-niveau virtualiseren. Het protocol is onbekend. Halo leest knoppen daarnaast via X11 (XQueryKeymap), zie STATUS.md. Open source om dit af te kijken: niets gevonden. Er is wel een compleet werkend voorbeeld: ElfLdr2 zelf (zie §4), maar dat is gesloten.

Time Crisis 5. [lokaal] `wajvio.dll` exporteert WAJVOpen, WAJVClose, WAJVGetInput, WAJVSetOutput, WAJVUpdate, WAJVGetNodeCount, WAJVGetStatus, WAJVSioRead, WAJVSioWrite, WAJVSioSetMode, WAJVUseSio, WAJVEnableWatchDogTimer, WAJVSetAsyncComm, WAJVSetFunctionSettings, WAJVSetPLParm, WAJVRestartPL, WAJVGetLibraryVersion, WAJVGetRetryCount; strings tonen JVS-commando's (Reset, Address, Revision, I/O ID, Function) en seriële poort-API's. De Shipping-exe importeert DINPUT8, XINPUT1_3, WINUSB en `hasp_windows_x64`. De bijgeleverde `README.txt` (emulator v1.3) noemt muis-DInput, kruisjes met Shift+H, F11 voor venstermodus, en dat dubbele schoten verdwijnen door de Fire/AltFire-regels in `TC5\Engine\Config\BaseInput.ini` uit te zetten. [bron] DemulShooter-wiki Namco-ES3: "Stand Alone" werkt alleen met muis-achtige apparaten, 1920x1080 volledig scherm, toetsen T/Y pedaal, O wapen, Shift+T munt. Beste inbrengroute [afgeleid]: (a) eerst de muisroute met onze gun, (b) daarna een eigen `wajvio.dll`-vervanger die met de daemon praat (TCP), dezelfde techniek als de plugin bij de Unity-games.

Transformers: Shadows Rising. [lokaal] `Shell.exe` zoekt het IO-bord via SetupDi/HID (`InitIOBoard: Couldn't find IOBoard device`, `HidD_GetCaps`, "Error reading Device PID and Version"), heeft `GunP1AxisX/Y` en `GunP2AxisX/Y`, een kalibratiescherm (`testcalibrate.xml`) en een Dinkey-dongle (`dk2win64.dll`). `Transformers2.exe` importeert DINPUT8. De logs in `Sega/shell/ioboard - *.log` melden herhaaldelijk "Couldn't find IOBoard device". [bron] DemulShooter Ringedge2: muis links = trekker, 1/2 start, 5 munt, pijltjes = hendel; ook een aparte "TSR_Loader". [bron: tfwiki] de guns zijn potentiometers (zoekresultaat-samenvatting, niet zelf geopend). Aanpak [afgeleid]: eerst de kalibratie van de game doorlopen met onze gun (hoeken uitsturen, midden, hoeken); de assen worden dan door de game zelf geschaald. Wil je het IO-bord ook zelf bieden, dan kan de daemon via `uhid` een virtueel HID-apparaat aanmaken; Wine ziet dat via udev/hidraw. VID/PID heb ik niet gevonden (niet vastgesteld).

Luigi's Mansion. [lokaal] `game.bat` start `sendFlash COM1 LMAIO.bin` en daarna `x:\exe\x64\VACUUM.exe` (D3D11, DINPUT8, wlanapi, IPHLPAPI, bcrypt); het IO-bord zit dus aan COM1, en de game verwacht drives X: en Y:. [bron] DemulShooter SEGA-Nu: onder TP muis links = trekker, midden = Strobulb; `-target=seganu -rom=lma`. Er is een FFB-plugin in de map (`FFBPlugin.ini`). Kern van het probleem [afgeleid]: een Sega Nu-platformlaag (amdaemon/"ALL.Net"-API's) die geen open project voor deze game levert.

Let's Go Safari. [lokaal] Importeert WS2_32, WINMM, d3d9, d3dx9_40, DINPUT8, DSOUND, SETUPAPI. `mxGetHwInfo.exe` leest `mxGetHwInfo.ini` (RingWide, graphic-device 1002:94cb, sound-device 10ec:0883). Er is een firewall.cfg. [bron] Seong-gist: voor Let's Go Island geldt een game-specifieke JConfig-patch, geen JConfig-muishandler maar DemulShooter (`-target=ringwide -rom=lgi`), en de D3D9-wrapper van JConfig uitzetten. Het hangen zonder venster bij ons is niet verklaard.

## 4. Linux: Wine en native

Wine-ervaringen [bron: Seong-gist]: gebruik Proton via een ondersteunde launcher; lightgunspellen hebben volgens de auteur een X11-sessie nodig en Wayland/XWayland vangt gun-invoer niet goed op; Wine's muisafhandeling beperkt tot één lightgun; BudgieLoader stopt vroeg als Feral Gamemode of MangoHud aan het Proton-proces hangt (relevant voor Steam-Gaming Mode); Terminator werkt met TP; Jurassic Park niet ("venster opent en sluit"); Halo, Tomb Raider en Rabbids staan er niet in. JConfig-games geven foutcode 1280 als een geconfigureerde controller ontbreekt. Op onze machine werkt Wayland met onze uinput-gun toch via TP (evdev), dus die opmerking geldt voor muis-invoer via Wine, niet voor de TP-route.

Native Raw Thrills op een moderne distro [lokaal, afgeleid]:
- Jurassic Park (`game`: ELF 32-bit i386, interpreter `/lib/ld-linux.so.2`, gebouwd in mei 2015, "Motion 1.08") heeft DT_NEEDED: libtheora.so.0, libogg.so.0, libPhysXLoader.so.1, libstdc++.so.6, libdl, libSDL-1.2.so.0, libGL, libGLU, libglut.so.3, libpthread, librt, libm, libudev.so.1, libgcc_s, libc, libX11. Terminator heeft dezelfde lijst zonder SDL en udev.
- Op SteamOS staan in `/usr/lib32` onder meer libGL, libogg, libudev, libasound, libpulse. De Steam Linux Runtime (scout) op deze machine heeft 32-bit freeglut3, libglu1, libsdl1.2debian, libtheora0, libogg0, libusb-1.0, libudev1 (map `~/.local/share/Steam/steamapps/common/SteamLinuxRuntime/steam-runtime/installed`). Dus bijna alles is er al.
- Ontbreekt in de dump: `libPhysXLoader.so.1` (NVIDIA PhysX, gesloten, niet herdistribueren). Het zit wel in de eigen TeknoParrot-installatie: `ElfLdr2/libs/` bevat libPhysXLoader.so.1, libPhysXCore.so, libNxCooking.so, libNxCharacter.so.1, libfmod.so.7, libstdc++ 5/6.0.x en andere oude bibliotheken. Dat is bruikbaar als LD_LIBRARY_PATH, mits we het van de eigen installatie van de gebruiker lezen en niet meeleveren (juridisch niet getoetst). Halo vraagt FMOD 1.10; TP levert versie 7, dus dat helpt Halo niet.
- ElfLdr2 zelf is een Windows-PE-loader (msys-2.0.dll, `TeknoParrot.dll`, `env/dev`) die de Linux-ELF onder Windows/Wine draait. Dat het binary zoveel symbolen voor NVIDIA-extensies noemt (`glCombinerInputNV`) is [afgeleid] een waarschuwing voor AMD/Mesa, maar of het gebruikt wordt is niet vastgesteld.
- Wat dan nog moet [afgeleid]: de dongle (`/dev/Hardlock`, HASP), de FTDI-gunborden, `/dev/ttyS0`, een `/pm`-pad (Halo: `/pm/g7/halo`; Terminator gebruikt `/g3/`), de FileTest-controle. Een bubblewrap-sandbox met bind-mounts zoals `scripts/run-halo.sh` lost paden en netwerk op, maar niet het USB-protocol.
- Conclusie: native draaien is technisch haalbaar tot aan het eerste beeld (Halo bewees het), maar werkend richten en schieten vereist het FTDI-gunprotocol. Geen open-source project gevonden dat dit levert.

## 5. Aanbevolen volgorde en gedeelde componenten

Gedeelde componenten, één keer bouwen:
1. Een generieke IO-brug in de daemon: één intern model (aim, knoppen, outputs) met meerdere transporten. Bestaand: uinput-gun, virtueel toetsenbord, TCP-klant (Unity). Nieuw: (a) TCP/lokale socket-server voor eigen hook-DLL's, (b) `uhid` voor virtuele HID-IO-borden, (c) optioneel een pty voor COM-poorten onder Wine.
2. Eén kleine proxy-DLL-sjabloon (C/C++, kruis-gecompileerd voor Windows) met een vaste API naar de brug; per game een dunne laag: `wajvio.dll` (Time Crisis 5), een DInput8-proxy (Transformers, Let's Go Safari, Luigi's Mansion). Of de toolchain (bijvoorbeeld mingw) op de machine beschikbaar is, heb ik niet gecontroleerd.
3. Een runner- en prefixbeheer per game (plain wine of Proton, aparte prefix, winetricks-set): grotendeels al aanwezig in `play-halo.sh`, `tp-add-game.py` en `wine-prefix.sh`.

Volgorde:
1. Transformers: Shadows Rising: gun-kalibratie in de game doorlopen, dan zo nodig een virtueel HID-IO-bord. Laagste moeite, werkt al grotendeels.
2. Time Crisis 5: eerst de muisroute (let op: onze uinput-gun is voor libinput genegeerd, dus de X-cursor beweegt niet; een muis voor Wine vraagt een aparte route of gamescope), daarna de `wajvio`-vervanger.
3. Jurassic Park native als experiment (zonder dongle- of FTDI-emulatie in de eerste ronde, zoals de eerste Halo-run met de FMOD-stub). Bij succes volgen Terminator en daarna Halo.
4. Tomb Raider en Rabbids: de eigen plugin (besluit van de eigenaar) als de licentie van de DemulShooter-plugin blijft onzeker.
5. Let's Go Safari en Luigi's Mansion: laatst. Eerst per game de importtabel en de logs uitpluizen; verwacht een eigen platformlaag.

## 6. Niet gevonden

- De broncode of een repository van JConfig of RConfig; wie de auteurs zijn; hun licentie.
- De licentie van Micetools (de README verwijst naar een bestand dat ik niet kon openen), en alles over mxdrivers of JVS daarin.
- Een open-source loader voor Luigi's Mansion, Let's Go Safari, Time Crisis 5, Transformers: Shadows Rising of de Raw Thrills-Linuxgames. Segatools, spice2x en Lindbergh Loader dekken geen van deze negen.
- Gedocumenteerde informatie over ElfLdr2/BudgieLoader, het FTDI-gunprotocol van Raw Thrills, en VID/PID van het Transformers-IO-bord.
- Niet te openen (403/402/denied): emulation.gametechwiki.com (alleen zoekresultaat-samenvatting), segaretro.org RingEdge 2, sindenwiki.org Time Crisis 5, jconfig-universe.fandom.com, X/Twitter, Reddit.
- Linux-ervaringsrapporten voor Luigi's Mansion, Let's Go Safari en Transformers: Shadows Rising.
