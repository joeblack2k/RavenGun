# Antwoorden TeknoParrot-team, rondes 5 t/m 22: verwerkt tot acties (stand 2026-10-11)

Bron: `findings/antwoorden_ronde5.md` t/m `antwoorden_ronde22.md` op de NAS (Johan, TeknoParrot-team), de vragen in
`findings/vragen.txt` (20, 23 t/m 66) en de per-game-bestanden `findings/<Game>.md`. Afkorting `r9` = `antwoorden_ronde9.md`.

Leeswijzer en betrouwbaarheid:

- Johan antwoordt uit (a) de open broncode (OpenParrot, TeknoParrotUi), (b) niet-afgeschermde onderdelen (`pipehelper.exe`,
  `TPGfx.dll`, profielen, `TeknoParrotUi.Common.dll`) en (c) uitgepakte beelden van de afgeschermde kernen en laders.
  Groep (c) hebben wij niet gecontroleerd en gebruiken we niet verder dan als aanwijzing (zie "Buiten scope").
- Hij heeft zichzelf meermaals gecorrigeerd (pipehelper bestaat wel, r9; LGS heeft wel een dongleveld, r8; vraag 34 eerst
  "ja", dan "nee", r6/r7/r12). Waar een antwoord botst met onze eigen metingen staat dat in de kolom "Actie" en in
  de sectie "Botsingen met onze metingen".
- Status is zoals Johan het zelf zegt (beantwoord / deels / open). "Open (niet beantwoord)" = hij is er nooit inhoudelijk op
  ingegaan.

## 1. Tabel: vraag, antwoord, status, actie

| # | Antwoord (ronde) | Status | Actie RavenGun |
|---|---|---|---|
| 20 | `pipehelper.exe` bestaat wel in de Linux-uitgave (155.648 B, niet afgeschermd); CLI en TPB1-protocol volledig bekend (r9, r13, r22). Raw Thrills-gunbordprotocol: niet besproken in 5-22 | Deels (pipehelper beantwoord; RT-bord open) | Spec in sectie 3.1. Eigen host-kant is klein; onze huidige `JvsPipeServer` gebruikt al de ongeauthenticeerde positionele vorm |
| 23 | Nee: TP levert geen GStreamer mee voor Windows-games. Het ElfLdr2-pakket levert wel `libavcodec.so.51`, `libavformat.so.50`, `libavutil.so.49` voor de Linux-ELF-games (r9) | Beantwoord | Geen code. `scripts/fetch-gst-wmv.sh` blijft; opnemen als gebruikersafhankelijkheid in de handleiding |
| 24 | Wine's eigen wmvcore kan 6.220.800 B (1920x1080x3) niet toewijzen; geen fout van de game; onschadelijk zolang de film loopt (r9) | Beantwoord (of de game er last van heeft: onbekend) | Geen. Bij Luigi's meekijken of films haperen (zie 2.1) |
| 25 | De core werkt met markeringsbestanden `.\ForceRestart`, `.\ForceReboot`, `.\Game.Run` (64-bit core) en blokkeert Windows-shutdown-verzoeken; of Luigi's ze aanmaakt is niet gezien (r11) | Deels | Meten (2.1), dan in `launch-direct.py` een herstartlus op die bestanden |
| 26 | `Select Switch` is een eigen logische knop in `LuigisMansion.xml`; kalibratievenster x 3..252, y 6..250 (r8) | Beantwoord | Geen: aux1 is al gebonden en het venster staat al zo in onze indeling |
| 27 | De runtime schrijft alleen `.\TeknoParrot\tmp\tpeth0.txt` (adapterinfo); `Ram/Network_eth0.bin` is van de game zelf; gevolgen onbekend (r10) | Open (Johan: vraagt een lange speelsessie van ons) | Meting, sectie 4 |
| 28 | De core kent `DataGlobal` niet; `Data` naast `DataGlobal\EN\Data` is gedrag van de game (r11) | Beantwoord | Geen; de symlink-view blijft. Vastleggen in `tp-add-game.py` als Luigi-recept |
| 29 | ElfLdr2 leest alleen `SHT_DYNSYM` uit de sectiekoppen; geen terugval op `PT_DYNAMIC` (0 treffers op `DT_SYMTAB/STRTAB/HASH`), ook niet in 1451 (r5, r9, r12) | Beantwoord | Geen snelle fix. Sectie 2.2 |
| 30 | De lader roept precies een keer `setGfxMode(0)` aan, onvoorwaardelijk (r7) | Beantwoord | Geen |
| 31 | Export `setGfxMode` bestaat (RVA 0xb8800, variabele op +0xf5000), modus 0 = cdecl-wrapper, 1 = `jmp` naar aparte pointer, rest = "Unknown GFX Mode" en `ExitProcess(-1)`; niet nodig (r6) | Beantwoord | Geen; `getGfxMode` (RVA 0xb87f0) is een goede diagnose |
| 32 | GOT-slotwaarde = adres van een TPGfx-wrapper; naam via de exporttabel (2776 exports, 0x70 uit elkaar); geen `hints.dat` nodig (r6) | Beantwoord | Optioneel diagnosescript voor JP (2.2) |
| 33 | Nieuwste ElfLdr2 is 1451 (BudgieLoader 1.0.0.171); lost het sectiekoppenprobleem niet op (r11, r12) | Beantwoord | Geen update nodig |
| 34 | Gecorrigeerd: de modus is goed (0, cdecl); oorzaak is de mislukte symboolparse, zelfde als 29 (r7, r12) | Beantwoord (na correctie) | Onze meting (stapel 8 bytes hoger, `ret` naar 0x58000) blijft het gevolg; zie 2.2 |
| 35 | De 0xDEADC0DE kwam door de injectiemethode, niet door verpakking (r5). Of de core `.m1v`-films levert: niet beantwoord | Deels | LE3: 32-bit GStreamer-pad zelf oplossen (2.4) |
| 36 | Otomedius' ACIO-micom komt uit `ACIODeviceHandler` in de 64-bit core met `CreateFileA`-onderschepping; protocol volledig ontleed (r9, r10, r14 t/m r19) | Beantwoord (protocol); botst met onze meting | Eigen ACIO-responder op een pty als COM2: sectie 3.2 en 2.5 |
| 37 | Wit venster zonder D3D-apparaat is waarschijnlijk geen licentieprobleem; de activeringsaanvraag is de abonnementslaag en moet met rust gelaten worden (r6) | Deels (bewust niet beantwoord) | Geen. Eigen D3D-diagnose (2.6) |
| 38 | De core herleidt elke schijfletter A..Z naar `.\TeknoParrot\<LETTER>\` (hook op `CreateFileA`); `rqrdlist.rqr` en de campagne-bitmaps horen in een van die mappen (r10). Waar de tabel gevonden is: 64-bit core | Beantwoord (geldigheid voor de 32-bit core niet gezien) | Experiment in een view met kopieen, 2.6 |
| 39 | Niets over Kaboom `InitLib` in profiel of core (r9) | Open (niet beantwoord) | Eigen onderzoek, 2.7 |
| 40 | `Binaries\Win32\TPFIX.ini` kan vooraf geplaatst worden (r5, r9) | Beantwoord | `tp-add-game.py` laat het bestand in de view zetten, geen dialoog |
| 41 | Niet inhoudelijk beantwoord: Johan voegt 41 en 42 samen met de standaardroute (r5), maar onze `c00002b5` treedt juist op de standaardroute op | Open (niet beantwoord) | Eigen lader uit de OpenParrot-bron met MXCSR-fix (2.14) |
| 42 | Niet beantwoord (geen oordeel over een lokale lader-kopie) | Open (niet beantwoord) | Doorgaan met `RG_LOADER_FPFIX` tot de bronbuild er is. Zwijgen is geen toestemming |
| 43 | De profielen noemen geen JVS-bord ("jvs" komt er niet in voor); de identiteit zou "hard in de core" zitten, gekozen op `EmulationProfile` (r8) | Beantwoord (negatief) | Zie botsingen: de JVS-node staat waarschijnlijk in de open TeknoParrotUi-bron. Lezen, 2.8 |
| 44 | Niet beantwoord; indirect: de "addressed"-vlag is een toestandslijn (shm-byte 0; seriele route via modemstatus 0x30 = geadresseerd, anders 0x10) (r20) | Open (niet beantwoord), aanwijzing in r20 | Race-experiment, 2.8 |
| 45 | Niet beantwoord | Open (niet beantwoord) | 2.8, stap 3 |
| 46 | Het profiel kiest geen weergavemodus (Windowed=0, RawInput, geen resolutieveld); het is een D3D9-beperking onder Wine; advies: d3d9 niet naar DXVK, Windows-versie XP/Vista (r8) | Beantwoord | MGG2-proef, 2.8 stap 4 |
| 47 | Alleen empirisch te bepalen (r5 en latere rondes) | Open (Johan: onze data) | Meting, sectie 4 |
| 48 | Ja, de standaardroute is ook voor de 64-bit core de bedoelde; `io: 0.0.0` is wat je ziet als de core te laat laadt (r5). Logregel of vlag voor "hooks aan": geen | Beantwoord | Venstertitel `TeknoParrot - <game>` en "Have fun :)" gebruiken als gezondheidscheck in de app |
| 49 | `Player 1 Trigger` start; `Credits P1` is muntinvoer (r8) | Beantwoord | Geen (trekker is al start); controleren dat onze munttoets op de Credits-bit zit |
| 50 | Mogelijk 0..65535, "meetpunt lag er iets naast"; Direct Aim Mode onderdrukt de banner, kalibratie telt waarschijnlijk nog mee (r6) | Open (Johan: vijf metingen) | Eigen hypothese (waarde = shm-byte x 257), meting in 2.9 en sectie 4 |
| 51 | Alleen eenmalige activeringsmeldingen gevonden, sleutel in `HKCU\SOFTWARE\TeknoGods\TeknoParrot`; geen hercontrole gezien, uitsluiten kan hij niet (r5) | Deels | Alleen waarnemen (lange NERF-sessie). Zie "Buiten scope" |
| 52 | `NERFTPPlugin.dll` is een Harmony-postfix op `mIOManager.UpdateInput_Unity`; kent P1 en P2, test, service, schouderknoppen (r11). Waar de plugin zijn waarden vandaan haalt: niet gezegd | Deels (volledig volgens Johan) | P2 en schouderknop testen (2.10) |
| 53 | Ja: de standaardroute is de bedoelde voor alle 32-bit-kern-games; POSTSTART alleen als de core niet voor iets anders mag laden (r5) | Beantwoord | Geen (al doorgevoerd). Overige 32-bit-games volgens 2.x proberen |
| 54 | Nee: TeknoParrotUi kent de Sega-shell niet (0 treffers op `ShellDirectory`, `ShellData`, `Shell.exe`); `C:\Sega` is shellgedrag (r8) | Beantwoord | Geen; eigen prefix met `C:\Sega`-link blijft |
| 55 | "NOT DETECTED" en "STANDARD" zijn verwacht onder TP (invoer loopt via shm, de core meldt geen bord); ticketuitgangen niet bekeken (r9) | Beantwoord (tickets niet) | Geen; ticketuitvoer later |
| 56 | Ja: de core bevat een in-proces amNetwork/amLib-nabootsing; daarom moet de core voor de eerste `amMasterOpen`/`pcpaOpenClient` laden en werkt POSTSTART nooit; er luistert niets op 4010x (r5) | Beantwoord | Geen. Gevolg voor loskoppelen: geen losse dienst mogelijk (sectie 5) |
| 57 | Wiel = `AnalogBytes[0]` op shm-offset 12, knoppen in het 32-bit woord op offset 8; of via de profielschakelaar `Use Keyboard/Button For Axis` (r5, r8) | Beantwoord; botst met onze indelingsanalyse voor de knoppen | LGS via JVS-pipe zoals LGI, of eerst shm-as testen (2.11) |
| 58 | Velden `DisplayMode`, `Use Custom Resolution` (+ `Resolution Width/Height`, namen uit `RaccoonRampage.xml`) zijn toe te voegen aan het LGS-profiel; core-sleutels `CustomResolution`, `RenderResolution`, `ResolutionMode` (r5, r8) | Beantwoord (werking voor RingWide niet bevestigd) | Proef in 2.11 |
| 59 | `CreateWindowExW/A` worden stil en onvoorwaardelijk gehookt; geen schakelaar, geen logregel; `Enable Native/Custom`, `SetCursor`, `ClipCursor` horen bij de cursor. Dat atomen en message-only vensters overgeslagen moeten worden is zijn conclusie, niet vastgesteld (r20, r21) | Open (Johan: kan het niet met zekerheid) | Venstertrace als meting, sectie 4; vdesktop blijft |
| 60 | De vensters zijn profielconstanten (`LGI.xml`: x 27..208, y 35..178, geen NVRAM, geen opgeslagen kalibratie); er is geen kalibratiestap in de core; pas het profiel aan (r20) | Beantwoord | `LGI.xml`: `xAxisMax` 124 en `yAxisMax` 114 (afgeleid, 2.12); direct-route staat al goed |
| 61 | De core doet niets met ViM3D/VisuMotion (0 treffers); de rood-cyaan kalibratie is verwacht spelgedrag (r20) | Beantwoord | Geen |
| 62 | De core onderschept geen schermwissel en heeft geen randloze optie; `Windowed=1` of `DisplayMode=Fullscreen Windowed` (r20) | Beantwoord | Geen; vdesktop blijft (want Windowed=1 crasht op 59) |
| 63 | Core noemt `d3dx9_43.dll`; TP levert geen d3dx9 mee; gebruiker installeert de DirectX-9-herdistribueerbare (r20) | Beantwoord | Naast `d3dx9_40` ook `d3dx9_43` uit dezelfde Steam-herdistribueerbare (2.13) |
| 64 | Core kent `SBTF_sram`, `SBTF_e2prom`, "RAM DATA IS BAD" niet (0 treffers); dus spelgedrag; grootte/inhoud onbekend (r20, r21) | Open (Johan: kan het niet) | Botst met onze meting; meetplan 2.13 en sectie 4 |
| 65 | Start `gunman_dxF.exe`, geen argumenten, `Windowed=0`, assen 0..255; geen kastmodus in het profiel; `EmulationProfile=FarCry` (r20) | Beantwoord | Geen (staat al zo) |
| 66 | Plat JVS in beide richtingen (E0, D0-escape, som); "addressed"-vlag is een toestand die de host zet, het bord antwoordt er niet op; `0x15` niet in zijn lijst (r20) | Beantwoord | Geen. Opmerking: 0x15 is standaard-JVS "master-ID", `0x32` is standaard "uitgang 1"; onze `jvs_answer` kent beide al |

## 2. Per game: wat de antwoorden veranderen, volgorde van proeven, inspanning

Algemene regels voor alle proeven hieronder: niets op de NAS schrijven behalve `findings/*.md`; schrijven gaat in een view
onder `~/.local/share/ravengun/runtime/views/<Game>` of in een eigen prefix. Geen proef raakt dongle, regio, integriteit
of abonnement.

### 2.1 Luigi's Mansion (speelbaar)

Verandert: markeringsbestanden voor herstart (25), select switch bevestigd (26), geen meegeleverde GStreamer (23),
`Network_eth0.bin` is game-eigen (27), `Data`/`DataGlobal` is spelgedrag (28). Het kalibratievenster 3..252/6..250 staat al in onze indeling.

1. Game-over-meting: drie credits, laten aflopen, dan binnen de werkmap `exe\x64` kijken of `ForceRestart`, `ForceReboot` of
   `Game.Run` verschijnen (poll elke 0,5 s) en de exitcode noteren. Eenmaal zonder en eenmaal met "Exit" uit het testmenu. Doel:
   vaststellen of de herstartlus op de markers of op exitcode 1 moet steunen. Inspanning 1 uur (laden 45 s per run).
2. Herstartlus in `scripts/launch-direct.py`: proces beeindigd en marker aanwezig (of exitcode 1 na een bewezen game over) gaat
   opnieuw starten; marker opruimen. 1 uur.
3. Langere sessie (30 min free play) voor 27: `Ram/Network_eth0.bin` verschijnt? Veranderen `Ram/*` of de free-play-instelling? Passief, 30 min kijktijd.
4. Tijdens 1 en 3 films observeren (haperen/overslaan) voor 24.

### 2.2 Jurassic Park, Pacman Chomp-Mania, Galaga Assault (ElfLdr2)

Verandert: de beperking is bevestigd en definitief (ook 1451): ElfLdr2 parseert alleen via sectiekoppen. TPGfx-modus 0 is
correct. Een nieuwe lader of dump lost het niet op. Onze eerdere observatie (stapel 8 bytes hoger na twee-argument-GL-aanroepen,
`ret` naar 0x58000) blijft het zichtbare gevolg; Johan's oorzaak (niet-gehookte GOT-sloten door de mislukte parse) past erbij.
Galaga Assault heeft volgens onze meting een andere blokkade (ontbrekende `zoinks*.tga`/`AlSeana*.tga`); Johan rekent hem bij JP.

1. Goedkoop bevestigen (geen wijziging aan iets): loaderlog van Terminator (werkt) naast JP leggen met dezelfde
   `RG_WINEDEBUG_FULL`-instellingen; zoeken naar `GOT hook '%s' installed`/`skipped` regels. Aanwezig bij Terminator en
   afwezig bij JP bevestigt de oorzaak. 1 uur.
2. Optioneel diagnosescript: GOT-slotwaarden van de JP-crash naar functienamen via de TPGfx-exporttabel (afstand 0x70).
   Alleen voor documentatie. 1 uur.
3. Echte oplossing = eigen ELF-lader die `PT_DYNAMIC` gebruikt: grote klus (dagen), niet gepland. Het toevoegen van sectiekoppen aan
   een kopie van de game-ELF staat onder "Buiten scope": JP heeft een integriteitscontrole.
4. Galaga: eigenaar vragen om een volledige dump (lettertypen). Pacman: wacht op JP.

### 2.3 Gashaaaan, Gashaaaan Refill

Geen wijziging; bevestigt de standaardroute (53). Refill is `Patreon=true` (r9-patroon). Niets te doen.

### 2.4 Lethal Enforcers 3

Verandert: 0xDEADC0DE is een gevolg van onze crash, niet van de core (r5, onze eigen meting klopt). Over `.m1v`-weergave
zegt Johan niets.

1. `ls $WINE/lib/wine/`: is er `i386-unix`? Dan is dit een oude-stijl Wine met 32-bit `winegstreamer.so`, die 32-bit GStreamer eist.
   5 minuten.
2. Als ja: een nieuwe-stijl-WoW64-build proberen (alleen `x86_64-unix`; 32-bit PE-code praat via thunks met de 64-bit
   winegstreamer, dus de 64-bit GStreamer van SteamOS volstaat). Meting: verdwijnt `err:quartz:autoplug` voor `.m1v`? 1 uur
   (download + proef).
3. `HKCU\Software\Wine\AppDefaults\game.exe\Version=winxp` tijdelijk weghalen voor deze game (staat in `LethalEnforcers3.md`).
4. Daarna het volgende obstakel noteren. Als de Wine-keuze alleen voor deze game nodig is: apart Wine per game in de view-config.

### 2.5 Otomedius

Verandert veel: ACIO-protocol compleet beschreven (3.2). Johan's stelling "de core levert de micom" botst met onze meting (de
32-bit `TeknoParrot.dll` heeft geen COM2/ACIO-verwerking, en hij ziet `ACIODeviceHandler` en `COM1..COM4` alleen in de 64-bit
core). Er is dus niets dat een eigen COM2 in de weg zit.

1. Logproef: pty als COM2 (`$WINEPREFIX/dosdevices/com2` naar de pty-slave; 57600 8N1, zoals de game meldt), nog niets
   beantwoorden, alle bytes met tijdstempel loggen. Wat wordt gestuurd, begint elk frame met `AA`, welke commando's komen (ook `0x0001`?),
   klopt de somregel (met of zonder `AA`, 3.2.4). Setting: `RG_JVS_PTY`-achtig mechanisme van de daemon hergebruiken (`ravengunctl
   iobridge`), nieuw bord `ACIO`. Eerdere mislukking ("game opent de pty niet") eerst nalopen met `WINEDEBUG=+file,+comm` om te zien
   of Wine `dosdevices/com2` oppakt. 2 uur.
2. Responder naar spec 3.2 (cmd 2 identificatie, cmd 3/0x100/0xFF bevestiging, rest zwijgen). Meting: verandert de `+debugstr`-melding
   "Node 2, step 1"? 2 tot 4 uur iteratief.
3. Als libacio andere productcodes of nodes verwacht dan de monster-identificatie (HDXS/ICCA hoort bij de handler van Johan's
   game, mogelijk niet bij Otomedius): zoveel nabootsen als nodig voor I/O; e-Amusement-online niet.
4. De profielopties "unlock", "e-Amusement" en "bestandschecksum" niet aanraken. Otomedius is `Patreon=true`.

### 2.6 Omatsuri Yasan (Ganso Takoyaki en Kingyo Sukui)

Verandert: de schijfletterherleiding (38). Zowel `Patreon=true` als de activeringsaanvraag zijn de abonnementslaag (37): niet aanraken.

0. Eerst door de eigenaar: de campagne-bitmaps in `Kingyo Sukui\Media\CAMPAIGN\` terugzetten (zie `OmatsuriYasan.md`, het `cp`-commando daar).
1. View `…/views/OmatsuriKingyo` met symlinks naar de NAS, plus een lokale map `TeknoParrot/D/` met kopieen (geen symlinks, want de game
   verwijdert de bitmaps): `rqrdlist.rqr` en `campaign01.bmp`/`campaign02.bmp` in de wortel van `D`, en dezelfde bestanden onder
   `D/Media/` en `D/Media/CAMPAIGN/` (de zoekplaats is niet eenduidig). Werkmap = de gameview. Meting: `WINEDEBUG=+file` op
   `rqrdlist|campaign|TeknoParrot\\`; verdwijnt exitcode 8? Opent de game paden onder `.\TeknoParrot\<L>\`? 1 tot 2 uur.
2. Zonder effect: dan geldt de herleidingstabel (alleen in de 64-bit core gezien) niet voor de 32-bit core; terugvallen op een
   Wine-schijfletter met kopieen. 1 uur.
3. Ganso Takoyaki (geen `d3d9.dll`, wit venster, 60 % CPU): `WINEDEBUG=+loaddll` welke grafische DLL wordt geladen (ddraw/d3d8/d3d11/
   opengl32); statische importen van `cvgame.exe` met `objdump -p` (alleen lezen; is de exe verpakt, dan stoppen en melden).
   Dan de bijpassende Wine-override. 1 uur.

### 2.7 Infinity Blade FX

Verandert: `TPFIX.ini` is vooraf te plaatsen (40). Kaboom is nog onbekend (39).

1. `tp-add-game.py`: schrijf `Binaries\Win32\TPFIX.ini` in de view met een echt pad naar `TeknoParrot.dll` (geen symlink via `~/Desktop`,
   geen dialoog). 30 minuten.
2. Kaboom: `WINEDEBUG=+loaddll,+reg,err+all` rond "Kaboom: InitLib() failed" (laadt de game `KaboomComDll.dll`? welke CLSID/
   DLL-pad zoekt hij?), `objdump -p KaboomComDll.dll` voor exports (`DllRegisterServer`?), `wine regsvr32` met `timeout 20` en `+reg`.
   Uitkomst bepaalt: gewoon COM-registratieprobleem (oplossen) of kopieerbeveiliging/online-dienst (dan stoppen). 1 tot 2 uur.
3. Grafisch (wined3d zwart met GL-fouten, DXVK wit) is apart; eerst na Kaboom.

### 2.8 Taito Type X2 (Gaia Attack 4, Haunted Museum 1 en 2, Music GunGun 2)

Verandert: geen direct antwoord op 41, 42, 44, 45; wel twee aanwijzingen: de "addressed"-vlag werkt als sense-lijn (r20) en
`pipehelper.exe` spiegelt `shm[0]` met tick-logregels (`pipehelper: shm[0] host->wine 0->1 (tick N)`). Onze eigen documenten zeggen dat de
JVS-node per profiel (`TaitoTypeX*`) in de open TeknoParrotUi-bron staat (`docs/research/teknoparrot-io.md`); Johan's "hard in de core" lijkt onjuist
voor de pipe-route.

1. Race-hypothese voor de adresketen: de game controleert na elke F1 de sense-lijn via `GetCommModemStatus` (0x30 = geadresseerd). Als
   `shm[0]=1` pas na het F1-antwoord bij de game aankomt (pipehelper kopieert per tick), ziet de game "niet geadresseerd" en wijst
   een volgend adres toe: dat verklaart 3..27 adressen per run. Proef: `RG_JVS_DEBUG=true`, in het helperlog de tick van
   `host->wine 0->1` naast het tijdstip van ons F1-antwoord leggen; variant: het F1-antwoord pas sturen nadat die logregel is
   verschenen (of 100 ms vertragen). Meting: aantal F1 per run (nu 3..27, verwacht 1 tot 2) en of 0x20-polling begint. 1 uur.
2. De JVS-node voor Type X2 lezen in de open TeknoParrotUi-bron (zoek `TaitoTypeX` onder `TeknoParrotUi.Common/Jvs`): identificatie, versies,
   functielijst (0x14), uitgangen. Daarmee `packaging/jvs-boards.json` een bord `TaitoTypeX2` geven in plaats van het generieke
   Sega-bord. Als de bron het niet bevat: de echte TeknoParrotUi een Type X2-game laten draaien en het verkeer op de pipehelper-TCP-poort
   meeluisteren (`strace -f -e trace=read,write,sendto,recvfrom` op de UI, of een tussenproxy). 2 uur.
3. Blijft het beeld zwart na de functiecheck: hoofdthread-lus (55 % CPU, `NtGetContextThread` op de eigen thread, adres a88) bemonsteren
   (`winedbg`/`gdb -p`, backtrace, welke API herhaald wordt). Blijkt de lus een bescherming-, regio- of dongle-controle: stoppen
   en onder "Buiten scope" melden. 2 uur.
4. Music GunGun 2 (Direct3D-melding): Windows-versie XP (staat al) en Vista/7 proberen; `Patch\ORI\game.exe` en `HD` naast `4K`;
   de eigen `d3dx9_31.dll` met `d3dx9_31=native,builtin`; `WINEDEBUG=+d3d,err+all` tot de falende `CreateDevice`/
   `GetDeviceCaps`; d3d9 niet naar DXVK (Johan, r8). 1 tot 2 uur.
5. FP-fix: zie 2.14. Het bestand `OpenParrot/*bk1.dat` laat zien dat de game zijn data laadt; dus de blokkade zit na de JVS-functiecheck.

### 2.9 Raccoon Rampage (speelbaar)

Verandert: Q48 bevestigd, Q49 bevestigd (trekker = start; "Credits P1" = munt). Q50 nog open.
Eigen hypothese: `4112 = 0x1010 = 16 x 257`. Onze aim-as is 1 byte (WartranTroopers, offset 12); `round(0,0625 x 255) = 16`; 16-bit-opslag = byte x 257.
Dus de ruimte is 0..65535 met 8-bit resolutie van onze kant (stap 0,4 % scherm, ongeveer 7,5 px op 1920). Johan's 65792 en "meetpunt ernaast" kloppen daarmee niet: het punt lag er exact, de byte rondde af.

1. Kalibratierun A met een bewust vaste aim voor alle 25 schoten, `test-pattern point 0,25 0,75`: verwacht X = 16448 (0x4040), Y = 49087 (0xBFBF).
   Run B op (0,0): verwacht 0. Run C op (1,1): verwacht 65535. Run D op (0,5; 0,5): 32896 (0x8080). Na elke run `Setting_Hardware.ini` lezen
   (`Calibrate_1P_*`), en de shm-bytes uit `ravengunctl iobridge status` ernaast. Elke run is 25 schoten van 0,4 s. 1 uur.
2. Wordt de kalibratie gebruikt met Direct Aim Mode aan? Na run A (bewust scheef) het vizier op vijf testpunten met een schermafdruk
   meten, en herhalen na een juiste kalibratie. Verschuift het vizier, dan telt de kalibratie mee en moeten we hem per
   spelersopstelling goed doen; zo niet, dan kan hij overgeslagen worden. Back-up van `Setting_Hardware.ini` eerst. 30 minuten.
3. Controleren dat onze munttoets op de bit van "Credits P1" zit (is de enige invoer die `CoinNum_1P` ophoogt).

### 2.10 NERF Arcade (speelbaar)

Verandert: de plugin is een Harmony-postfix op `UpdateInput_Unity` en kent P2, test, service en schouderknoppen (52). Daardoor is een eigen vervangende plugin
mogelijk (`AnalogGameInput`, `m_DigitalInputData`, `m_CurrentGunState` zijn de velden), maar niet nodig zolang tp-shm werkt.
De plugin bevat ook dongle- en muntboekhouding via HASP: wij bouwen niets daarvan na.

1. P2: bit `p2.start` (bit 11) en `p2.trigger` (bit 12) via `ravengunctl output-button` testen: verdwijnt "Press Start!" bij speler 2? 20 minuten.
2. Schouderknoppen: volgens `teknoparrot-io.md` is Shoulder = Button3 = onze bit `p1.grenade` (bit 9). In het spel testen wat bit 9 en bit 10
   doen (laser? wisselen?) en een Move-knop eraan koppelen. 20 minuten.
3. Test/service: TEST lang indrukken (1 tot 2 s). De plugin heeft `oldtest`/`oldservice`. 10 minuten.
4. Sessie van 60 minuten free play, waarbij we alleen vastleggen of het proces stopt, of er een venster/melding komt (51). Niets aan de
   abonnementslaag veranderen. 1 uur kijktijd.

### 2.11 Let's Go Safari

Verandert: LGS heeft `DongleRegion`/`PcbRegion = USA` (r8, buiten scope, alleen vermelden). Wiel via shm (offset 12), volledig scherm via profielvelden (58).

1. Snelle proef (TPUI-route): `Use Keyboard/Button For Axis=0`, schrijf een zaagtand in shm-byte 12 en kijk of het stuur reageert. Johan's bewering dat
   de knoppen in het woord op offset 8 zitten staat tegenover onze indelingsanalyse (bits leeg); één bit per keer testen. 30 minuten.
2. Hoofdroute zoals LGI: `RG_JVS_TRANSPORT=pipe` met een nieuw bord `LGS` in `packaging/jvs-boards.json` (identificatie 837-14895 "FE MODE" volgens
   `teknoparrot-io.md`; wiel = Analog0; schakelaars in 0x20). Wielbereik meten met extreme standen (het profiel heeft geen `xAxis`-velden, dus waarschijnlijk 0..255;
   Johan zegt: naar het venster van de game, niet blind). Doel: stuur uit de horizontale Move-aim. 3 uur.
3. Volledig scherm: in `UserProfiles/LGS.xml` toevoegen `DisplayMode`=`Fullscreen Windowed`, `Use Custom Resolution`=1, `Resolution Width`=1920, `Resolution Height`=1080 (namen
   uit `RaccoonRampage.xml`). Meting: venstergrootte (`xwininfo`). Valt niet te forceren: `--vdesktop 1920x1080` zoals LGI, of gamescope met opschalen. 1 uur.

### 2.12 Let's Go Island en Let's Go Island 3D

Verandert: de vensters zijn profielconstanten en `LGI.xml` is een kopie van `LGI3D.xml` (27..208 / 35..178). Er is geen kalibratiestap in het spel of de core (60). Onze gemeten
venster (inverted x 131..228, y 141..220) staat al in `jvs-boards.json`.
Afleiding voor de TPUI-route (door ons): onze waarde = `228 - 97 x` en `220 - 79 y`; het profiel rekent `255 - (min + (max-min) t)`. Dus `xAxisMin` 27 en `yAxisMin` 35
blijven, `xAxisMax` = 27+97 = **124**, `yAxisMax` = 35+79 = **114**.

1. In `UserProfiles/LGI.xml` die twee waarden zetten en de TPUI-route even testen: testpunt 0,2/0,5/0,8 moet 0,20/0,50/0,80 geven (zoals onze direct-route). 30 minuten. Bevestigt beide
   werkwijzen tegelijk.
2. Vraag 59-trace: `WINEDEBUG=err+all` plus `RelayInclude` op `CreateWindowExW` bij `Windowed=1`; alle aanroepen vanuit `TeknoParrot.dll` tabelleren
   (klassenaam als atoom of string, ouder, stijl, titel). Doel: weten of alleen de atoom-klasse faalt. Dit is de meting die Johan nodig heeft (sectie 4). 1 uur.
3. LGI3D: rood-cyaan kalibratie is verwacht; niets doen.

### 2.13 Medaru No Gunman

Verandert: `d3dx9_43` wordt ook genoemd (63); profiel = FarCry, `gunman_dxF.exe` zonder argumenten (65). Johan zegt dat de core `SBTF_sram`/`SBTF_e2prom` niet kent; wij maten dat
de core ze wel maakt (2 MB bij de eerste start; LGI: `SBUH_sram.bin`). De bestandsnaam wordt dus waarschijnlijk uit een spelcode opgebouwd, wat een tekstzoektocht niet vindt.

1. Ook `d3dx9_43.dll` (en `d3dx9_41` t/m `47` indien aanwezig) uit dezelfde Steam-herdistribueerbare (`Jun2010_d3dx9_43_x86.cab`) in `syswow64` van de eigen prefix, `native,builtin`. 20 minuten.
2. Wie zet de grootte: `WINEDEBUG=+file` op een verse prefix-kopie, filter `SBTF_`: open-vlaggen (CREATE_ALWAYS/TRUNCATE?), `SetEndOfFile`, `SetFilePointer`, `FlushFileBuffers`.
   Tegelijk `ls -l` van `…/AppData/Roaming/TeknoParrot/` tijdens het draaien en na een nette afsluiting (misschien gememoriseerd en pas bij afsluiten geflusht). 30 minuten.
3. Vooraf aanmaken en vergelijken (4 runs van 2 minuten): `SBTF_sram.bin` 2.097.152 B met nullen; idem met 0xFF; `SBTF_e2prom.bin` 4096 B met nullen/0xFF. Noteren welke melding (3001/3003/geen) per combinatie.
4. Testmenu: CLEAR BACKUP DATA (en dan EXIT), daarna herstart. Blijft de RAM-fout weg, dan is een eenmalige automatische reset bij de eerste start (per prefix) een uitweg. 30 minuten.
5. Eenmalig: `eep.bin` en `user_backup_*.bin` blijven leeg (Johan kan het niet beoordelen).

### 2.14 Overig: eigen lader en 32-bit games (Q41, 42, 47, 53)

- Johan citeert in r5 de bron `OpenParrotLoader/src/main.cpp`: de trampoline draait `InitializeASI` op de hoofdthread, herstelt de 20 originele bytes en springt terug. Dat is de open GPL-bron. Of de `OpenParrotLoader.exe`
  uit runtime 2.0.0.130 (met `SetThreadContext`, vlaggen 0x1001F) exact die bron is, staat niet vast.
- Actie: de lader uit de OpenParrot-bron bouwen (mingw in een container) met een van twee fixes: `CONTEXT_FLOATING_POINT` weglaten of na `SetThreadContext` `MXCSR` terugzetten naar 0x1F80
  (`ldmxcsr`). Daarmee verdwijnt de binaire byte-aanpassing (`RG_LOADER_FPFIX`) en kunnen we de fix upstream aanbieden. 0,5 tot 1 dag.
- Inventaris voor 47: per 32-bit-game in onze bibliotheek op de standaardroute noteren: lader, `c00002b5` ja/nee, FP-fix nodig ja/nee. Bekend: Gashaaaan, Luigi (64-bit), Pigs Might Fly, LGS, LGI, LGI3D, Medaru: geen probleem;
  Type X2 (4 games): probleem. Nog te doen: LE3, Otomedius, Omatsuri x2, IBFX, Gashaaaan Refill (zonder FPFIX starten en `err+seh` doorzoeken op `c00002b5`). 1 uur totaal.

## 3. Volledig gespecificeerde protocollen

### 3.1 TPB1: de handdruk van `pipehelper.exe` (Windows named pipe naar TCP naar host)

Bron: `r9` (opdrachtregel, meldingen), `r13` (kop en `OKAY`), `r22` (naamveld); meldingen en gebruiksregel door ons bevestigd met `strings` op het meegeleverde `pipehelper.exe`.
Alleen een draadformaat en een opdrachtregel; geen code.

**Aanwijzing over licentie/activering:** niets in r9, r13 of r22 duidt erop dat TPB1 met licentie, activering of abonnement te maken heeft. Het ziet eruit als lokale authenticatie van de
loopback-brug (een geheim per start, meegegeven op de opdrachtregel of via een omgevingsvariabele, zodat een ander lokaal proces de pipe niet kan overnemen). Johan heeft niet gezien hoe TeknoParrotUi sessie en token afleidt;
wij gebruiken uitsluitend eigen, willekeurig gekozen waarden en doen niets met tokens die TP uitgeeft.

#### Opdrachtregel (volgens de gebruiksregel in de exe)

```
pipehelper.exe <pipeName> <host> <port> [shmName shmSize hostShmPath]            (oude, positionele vorm: GEEN TPB1)
pipehelper.exe pipe --name <pipeName> --host 127.0.0.1 --port <port>
               --session <32 hex> (--token <64 hex> | --token-env <naam>)
               [--shared-page <shmName> <shmSize> <hostShmPath>]
               [--ready-file <windowsPad>] [--quiet]
pipehelper.exe shm <shmName> <shmSize> <hostShmPath>                              (alleen gedeeld geheugen)
```

- De positionele vorm gebruikt RavenGun nu (`scripts/launch-direct.py`); onze `JvsPipeServer` stuurt nooit `OKAY` en de game pollt toch: bewijs dat die vorm zonder handdruk werkt.
- `--host`: de gebruiksregel noemt alleen `127.0.0.1`.
- `--session`: 32 hextekens = 16 bytes. `--token`: 64 hextekens = 32 bytes. `--token-env` leest het token uit een omgevingsvariabele (houdt het uit de procesregel).
- `--ready-file`: pipehelper maakt dit bestand aan zodra de named pipe bestaat (melding "pipe %s created, waiting for game..."). `--quiet`: minder logregels (RavenGun leest het helperlog, dus niet gebruiken).
- Foutmeldingen bij ongeldige argumenten: "invalid authenticated pipe arguments", "invalid token environment argument/name", "invalid shared-page size", "cannot reach host %s:%u".

#### Handdruk (pipehelper is de TCP-client, de host de server op 127.0.0.1:<port>)

```
1. host luistert op 127.0.0.1:<port>; pipehelper maakt \\.\pipe\<name> en verbindt met de host.
2. pipehelper -> host, direct na het verbinden:
     off  len  inhoud
     0    4    "TPB1"                       54 50 42 31
     4    4    00 01 00 01                  (versie/type; betekenis onbekend, constant)
     8    16   sessie                       hex-decodering van --session
     24   32   token                        hex-decodering van --token / --token-env
     56   2    naamlengte N, big-endian     0 <= N <= 128 (pipehelper breekt zelf af boven 128)
     58   N    naam                         ASCII, geen afsluitende nul; vrijwel zeker de pijpnaam (niet bevestigd of met \\.\pipe\)
   totale lengte = 58 + N
3. host -> pipehelper: "OKAY"  (4F 4B 41 59)   = geaccepteerd
   alles anders (of verbinding sluiten) = geweigerd; pipehelper meldt "TPB1 authentication rejected for pipe %s"
4. Na "OKAY" melding "TPB1 authenticated ... TCP attached, bridging": dan een kaal bytestroom in beide richtingen, tussen de pipe
   (game-kant) en de socket (host). Geen verdere framing door pipehelper.
```

#### Gedrag van de host (voor een eigen implementatie)

1. Lees minstens 58 bytes (herhaald `recv`, de kop kan gesplitst aankomen), controleer `"TPB1"` en `00 01 00 01` (onbekende versie: weigeren en loggen).
2. Lees N uit de bytes 56..57, weiger bij N > 128, lees N bytes naam.
3. Vergelijk sessie, token en naam met wat de host zelf aan pipehelper meegaf (constant-time vergelijken is voldoende). Naam onbekend: weigeren.
4. Antwoord `OKAY`; weigeren = verbinding sluiten zonder antwoord (wat pipehelper daarna doet: onbekend, het lijkt de cyclus te beeindigen).
5. Daarna de stroom ongewijzigd doorgeven. Voor de JVS-pipe is dat plat JVS (3.3).
6. Na "bridge cycle ended, recycling pipe" (de game sluit de pipe) maakt pipehelper de pipe opnieuw; of hij dan opnieuw verbindt en een nieuwe handdruk doet, is niet vastgesteld. Onze huidige server accepteert slechts een verbinding.
   Meten door de game binnen een sessie te herstarten.
7. Nog onbekend: wat de hoge/lage vlaggen in `00 01 00 01` betekenen (vermoedelijk versie 1.0, type 1.0).

Testvoorbeeld (door ons samengesteld, niet opgenomen): sessie `00112233445566778899aabbccddeeff`, token 32x `a5`, naam `TeknoParrot_JVS` (15 tekens) geeft 73 bytes:
`54504231 00010001 00112233445566778899aabbccddeeff a5a5...a5 (32x) 000f 54656b6e6f506172726f745f4a5653`.

Gedeeld geheugen (positionele vorm en `--shared-page`): pipehelper maakt een benoemd bestandsgeheugen `<shmName>` van `<shmSize>` bytes in de prefix en spiegelt dat met het hostbestand
(`/dev/shm/TeknoParrot_JvsState`, als Windows-pad `Z:\dev\shm\...`). Byte 0 (verbindingsvlag) wordt in beide richtingen gespiegeld en gelogd als
`pipehelper: shm[0] host->wine A->B (tick N)` / `wine->host`; per `docs/research/teknoparrot-io.md` gebeurt de kopie van byte 0 synchroon vlak voor het eerste antwoord. Dit is niet in de antwoorden
van Johan gespecificeerd; het staat hier omdat de vlag voor Type X2 (2.8) relevant is.

### 3.2 ACIO: het micom-protocol van `ACIODeviceHandler` (Otomedius, COM2, 57600 8N1)

Bron: `r14` (klasse), `r15` (kop), `r16` (opbouw, stuffing, som), `r17` (commando's, identificatie), `r18` (antwoordvlag), `r19` (zwijgen bij fouten). Alles komt uit de afgeschermde 64-bit core (zie "Buiten scope");
hier alleen het draadformaat. Draadsnelheid 57600 8N1 en poort COM2 komen uit onze eigen Otomedius-log (libacio 1.12.6), niet van Johan.
Betrouwbaarheid: de structuur is consistent over vijf rondes; de punten die niet vaststaan zijn in 3.2.6 gemarkeerd. Onze lezing (waar die afwijkt van Johan's woorden) staat in 3.2.5.

#### 3.2.1 Kader

```
AA | <kop 5 bytes> | <inhoud: lengte bytes> | <som>        (voor escaping)
```

- `0xAA` = beginmarkering. De som staat achteraan. Beide richtingen gebruiken hetzelfde kader (zie 3.2.5).
- Kop (5 bytes): `b0` byte (Johan: "opdrachtnummer"), `b1 b2` 16-bit waarde, hoogste byte eerst, `b3` byte, `b4` lengte van de inhoud.
- Totaal onbewerkt: `1 + 5 + lengte + 1`. De antwoordbuffer van de handler is 0x104 = 260 bytes: inhoud dus hoogstens 253 bytes.

#### 3.2.2 Byte-stuffing (op de lijn)

Alle bytes na de beginmarkering, ook kop, inhoud en som:

| In het frame | Op de lijn |
|---|---|
| `0xAA` | `0xFF 0x55` |
| `0xFF` | `0xFF 0x00` |
| overig | ongewijzigd |

De ontvanger doet het omgekeerde: na `0xFF` volgt het bitsgewijs omgekeerde van de echte byte. De beginmarkering zelf wordt niet geescaped.

#### 3.2.3 Controlesom

Som van de bytes modulo 256, over het onbewerkte frame, in een byte achter de inhoud. Zie 3.2.6 voor de vraag of `0xAA` meetelt.

#### 3.2.4 Commando's (16-bit, de waarde in `b1 b2`) en antwoorden

Antwoordkop: `b0` van het verzoek met bit 7 gezet (blijft 0 als `b0` 0 is), `b1 b2` en `b3` overgenomen uit het verzoek, `b4` = lengte van de antwoordinhoud, dan inhoud, dan som.

| Commando | Antwoord |
|---|---|
| `0x0002` | apparaatbeschrijving, inhoud 48 bytes (3.2.4.1) |
| `0x0003`, `0x0100`, `0x00FF` | bevestiging: lengte 1, inhoud = 1 "argument"-byte (waarschijnlijk statuswaarde; waarde niet gezien, 0 aanbevolen) |
| `0x0080` | antwoord met inhoud van `n` bytes uit een interne bron (`b4 = n`); inhoud niet bekend |
| al het andere | **geen antwoord** (de handler ruimt op en keert terug; geen foutframe) |

Fouten: een ontvangen frame met een verkeerde som levert ook geen antwoord. Een game die geen antwoord krijgt, wacht; er is dus geen foutframe te verzinnen.

##### 3.2.4.1 Inhoud van het antwoord op `0x0002` (afgeleid uit de offsets in r17; offsets in de inhoud, kopgrootte 5)

| Offset | Lengte | Inhoud |
|---|---|---|
| 0 | 4 | `04 02 00 00` (dword 0x204) |
| 4 | 4 | `00 01 06 00` (dword 0x60100; vermoedelijk firmware 1.06) |
| 8 | 4 | `"HDXS"` |
| 12 | 16 | `"Oct 17 2024"`, nul-aangevuld (`__DATE__`) |
| 28 | 16 | `"00:00:00"`, nul-aangevuld (`__TIME__`) |
| 44 | 4 | vermoedelijk `"ICCA"` (door Johan alleen als tekenreeks naast de klasse gezien, niet in de code) |

`b4` is dan 48 (0x30). Dit is wat de handler in Johan's game antwoordt; Otomedius verwacht mogelijk een ander productnummer.

#### 3.2.5 Onze lezing (afwijking van Johan's woordkeuze)

Het kader (`AA`, adres, 16-bit commando, volgnummer, lengte, inhoud, som, escape `FF`+omgekeerd) komt overeen met het algemeen bekende ACIO-kader uit de Konami-gemeenschap
(`b0` = knoopadres, `b1b2` = commando, `b3` = volgnummer, `b4` = lengte). Dat verklaart "antwoord = `b0` met bit 7" (adres | 0x80, adres 0 = broadcast) en dat `b3` onveranderd wordt teruggegeven. Dit is
onze aanname, niet Johan's tekst en niet uit de repo; verifieer met de logproef (2.5 stap 1) voordat de responder erop gebouwd wordt. Als dat klopt zijn commando 1 (adrestoewijzing) en 0x80 mogelijk onderdeel van de opstart van libacio.

#### 3.2.6 Onzeker of open

- Telt `0xAA` mee in de som? r16 zegt dat de som voor uitgaande frames over de hele buffer loopt, inclusief `0xAA`; de controle in r17 (inkomend) begint bij index 1, dus zonder `0xAA`, en eindigt voor de laatste byte. Beide in een emulator ondersteunen: standaard zonder `0xAA` rekenen, bij herhaalde verzoeken omschakelen.
- Betekenis van `b3` en van het "argument"-byte in het kort antwoord; inhoud bij `0x0080`.
- Of commando `0x0001` bestaat; zo ja is het niet in Johan's lijst.
- Of de handler ook gegevens kan versturen zonder verzoek (niet gezien).
- Tijdgedrag (time-out, minimale pauze) is niet onderzocht.

Referentie-implementatie in pseudocode (onze samenvatting van 3.2.1 t/m 3.2.4):

```
def encode(b0, cmd16, b3, payload, include_aa_in_sum=False):
    body = bytes([b0, cmd16 >> 8, cmd16 & 0xFF, b3, len(payload)]) + payload
    s = (sum(body) + (0xAA if include_aa_in_sum else 0)) & 0xFF
    return b"\xAA" + stuff(body + bytes([s]))
def stuff(b):   # AA -> FF 55, FF -> FF 00
    return b"".join(b"\xFF" + bytes([x ^ 0xFF]) if x in (0xAA, 0xFF) else bytes([x]) for x in b)
def reply(req):      # req: (b0, cmd16, b3, payload) na unstuff en somcontrole
    if cmd16 == 2:                 return encode(b0 | 0x80 if b0 else 0, 2, b3, ID48)
    if cmd16 in (3, 0x100, 0xFF):  return encode(b0 | 0x80 if b0 else 0, cmd16, b3, b"\x00")
    return None                    # zwijgen
```

Testvoorbeeld (samengesteld, niet opgenomen): verzoek `AA 01 00 02 00 00 03` (knoop 1, commando 2, volgnummer 0, lengte 0, som 0x03) hoort `AA 81 00 02 00 30 <48 bytes> <som>` op te leveren.

### 3.3 Overige afspraken die nu vaststaan

| Afspraak | Inhoud | Bron | Zekerheid |
|---|---|---|---|
| JVS over de pipe | Plat JVS in beide richtingen: sync `E0`, escape `D0` (`D0 CF` = `D0`, `D0 DF` = `E0`), som over lengte+status+aantal+rapporten. Opdrachten die de core pollt: 0x10 t/m 0x14, 0x20, 0x21, 0x22 (16-bit, hoogste byte eerst), 0xF1 | r20, `findings/07` | hoog |
| "Addressed"-vlag | Door de host gezet: shm-byte 0 (1 bij F1, 0 bij reset); seriele route: modemstatus 0x30 (geadresseerd) of 0x10 | r20, `teknoparrot-io.md` | hoog voor shm, middel voor modemstatus |
| Schijfletterherleiding | Elke letter A..Z wordt `.\TeknoParrot\<LETTER>\` (hook `CreateFileA`, logregel `[CreateFileA]: Hooked %s`). Vaste bestandsnamen in `F`/`G`: `BookKeeping.bin`, `CreditData.bin`, `ErrorLogData.bin`, `PlayerGameSaveData.bin`, `RankingData.bin`, `TestModeData.bin`, `YouIDatas.bin`, `YouITempData.bin`, `BnpsIdList.bin` en per game voorvoegsel (TC5: `TC5TestModeData.bin`); `.\TeknoParrot\cards\`, `\temp`, `\AppUser\temp` | r10 | middel (64-bit core; werkmap-relatief) |
| Herstartmarkers | `.\ForceRestart`, `.\ForceReboot`, `.\Game.Run` in de werkmap | r11 | laag tot middel (namen uit de core, aanmaker onbekend) |
| TPGfx | Modus 0 = cdecl-wrappers (door de lader gezet), 1 = `pop ebp; jmp [ptr]`, anders fataal | r6, r7 | hoog |
| ElfLdr2-symbolen | Alleen `SHT_DYNSYM` uit sectiekoppen, geen `PT_DYNAMIC`-terugval, ook niet in 1451 | r9, r12 | hoog |
| `HookedWindows.txt` | 31 venstertitels waarmee TP het spelvenster zoekt (uit `pipehelper`-pakket) | r9 | hoog |

## 4. Nog open: wat we precies moeten meten

Johan noemt 27, 47, 50, 59 en 64 als vragen die alleen onze metingen beantwoorden (r19 t/m r22).

- **27, `Ram/Network_eth0.bin`.** Luigi's Mansion 30 tot 60 minuten free play, vanaf een schone start. Vastleggen: ontstaat `Ram/Network_eth0.bin` (of een ander netwerkbestand), verandert de free-play-
  of munt-instelling, melding in `+debugstr`. Daarnaast `ls -l --time-style=full-iso exe/x64/Ram/` voor en na. Er is geen verwachting dat het iets uitmaakt; het resultaat sluit het af.
- **47, welke games de thread-context-aanpak niet verdragen.** Tabel per 32-bit-game: lader, route, Wine-versie, `c00002b5` (STATUS_FLOAT_MULTIPLE_TRAPS) ja/nee, FPFIX nodig ja/nee. Stap: starten zonder FPFIX met `WINEDEBUG=err+seh`, zoeken
  naar `c00002b5`. Voor de games die starten: `MXCSR` printen met een kleine 32-bit probe is niet nodig. Het gewenste eindresultaat is een lijst die we aan Johan kunnen sturen. Zie 2.14.
- **50, Raccoon-kalibratieruimte.** Niet de vijf procentpunten die Johan vraagt (de doelen liggen vast), maar vier runs met een vaste aim voor alle 25 schoten: (0,25/0,75), (0/0), (1/1), (0,5/0,5), telkens met de shm-bytes erbij. Verwachting: opgeslagen waarde = byte x 257
  (4112 = 16 x 257). Dan de vraag of de kalibratie meetelt met Direct Aim Mode (2.9, stap 2). Uitkomst melden in `RaccoonRampage.md`.
- **59, `CreateWindowExW`-hook.** LGI met `Windowed=1`: `WINEDEBUG=err+all` en `RelayInclude=user32.CreateWindowExW`; tabel van alle aanroepen vanuit `TeknoParrot.dll`: ouder, stijl, titel, klasse (atoom of tekenreeks), en
  welke slaagt of faalt. Dezelfde trace voor LGS (`Windowed=1` werkt daar) om het verschil te zien. Doel: bewijs dat de hook atoom-klassen niet afvangt, voor de TeknoParrot-ontwikkelaars.
- **64, `SBTF_sram.bin`/`SBTF_e2prom.bin`.** Het meetplan in 2.13 (stap 2 tot 4). Uit te leveren: grootte, open-vlaggen en schrijver (core of game), en welke beginwaarde (nul of 0xFF) de fout 3001/3003 laat verdwijnen.

Daarnaast vragen die Johan nooit beantwoordde en die wij zelf oplossen: 39 (Kaboom), 41/42 (lader-FP), 44/45 (Type X2 na de functiecheck), plus het Raw Thrills-gunbordprotocol uit vraag 20 (USB-opname nodig).

## 5. Gevolgen voor `loskoppelen-teknoparrot.md` (niet aangepast, voor jou)

Rijen en alinea's van `docs/research/loskoppelen-teknoparrot.md`:

- **Inleiding (alinea met vragen 17 t/m 22).** Voeg toe dat r9 het antwoord op vraag 20 corrigeert: `pipehelper.exe` zit wel in de Linux-uitgave (niet afgeschermd) en het protocol is nu volledig bekend (3.1).
- **Sectie 1, rij 3 (OpenParrotLoader).** Bron-citaat (r5): de trampolineroute is de standaard en bedoeld voor Wine; de lader is open GPL-bron. Aandachtspunt: de runtime-binary schrijft `SetThreadContext` met 0x1001F; bron en binary vergelijken (2.14).
- **Sectie 1, rij 4 (ElfLdr2/BudgieLoader).** Nog steeds afgeschermd; nieuwste 1451 (lader 1.0.0.171) heeft dezelfde sectiekoppenbeperking; `TPGfx.dll` is niet afgeschermd (2776 GL-wrappers, modus 0 = cdecl).
- **Rij 5 (open kern OpenParrot).** Onveranderd. Type X2: de JVS-node staat in de open TeknoParrotUi-bron en moet nog gelezen worden (2.8).
- **Rij 6 (gesloten kern).** Nieuwe bekende taken van de kern: in-proces amNetwork/amLib-nabootsing (geen TCP-dienst), schijfletterherleiding naar `.\TeknoParrot\<L>\`, COM-poortalias met `ACIODeviceHandler` (alleen in de 64-bit core gezien), onvoorwaardelijke
  `CreateWindowExW`-hook (veroorzaakt de LGI-crash bij `Windowed=1`), SRAM/EEPROM-bestanden `<ID>_sram.bin` (door ons waargenomen), herstartmarkers.
- **Rij 7 (`pipehelper.exe`).** Wordt "protocol bekend": TPB1 en de positionele vorm (3.1); een eigen helper is een kleine Windows-exe. Status "Nee" blijft tot die gebouwd is, maar de blokkade (onbekend protocol) is weg.
- **Rij 8 (prefix met activering).** Johan: activering is een eenmalige controle met de sleutel in `HKCU\SOFTWARE\TeknoGods\TeknoParrot`; blijft in de prefix.
- **Rij 10 (formaten).** Voeg toe: TPB1 (3.1), ACIO (3.2), `.\TeknoParrot\<L>\`-indeling.
- **Sectie 2 punt 3 (compatibiliteitspatches).** Nog steeds geen lijst per game; wel de zes soorten uit de rij 6 hierboven, met voorbeelden.
- **Sectie 2 punt 4 (diensten).** Poort 40100 wordt in het proces beantwoord en is geen TCP-dienst: er luistert niets (onze meting en r5 vraag 56). Voor een eigen lader dus geen eigen dienst, maar onderschepping van `amMasterOpen`/`pcpaOpenClient`.
  De sleutel `AmAuthPort` en `TP_INSTANCE`/`TP_BIND_IP` horen bij de netwerk- en authenticatielaag: niet nabouwen.
- **Sectie 3, rij "Diensten (bv. poort 40100)".** Van "eigen dienst die antwoordt" naar "niet als dienst mogelijk; in-proces hook (buiten scope voor het authenticatiedeel)". Status blijft "nee".
- **Rij "Lader (Windows-games)".** Bouw uit de bron wordt nu ook de uitweg voor de MXCSR-fix (Type X2); lader-FP-aanpassing van binary wordt dan overbodig.
- **Rij "Compatibiliteitspatches".** Per-game-lijst nog onbekend; voor Otomedius is de ACIO-kant nu gespecificeerd (3.2).
- **Rij "I/O (JVS)".** Bevestigd: plat JVS; addressed-vlag semantiek (3.3). Type X2 mist nog een passend bord (2.8).
- **Rij "Raw Thrills (Linux-ELF)".** ElfLdr2 levert de FFmpeg-bibliotheken (`libavcodec.so.51`, `libavformat.so.50`, `libavutil.so.49`) voor de video van deze games; gunbordprotocol nog onbekend (vraag 20, tweede deel).
  Voor een eigen ELF-lader: `PT_DYNAMIC` gebruiken is nodig voor JP, Pacman en Galaga.
- **Rij "Unity-games".** NERF: de plugin is een Harmony-postfix op `mIOManager.UpdateInput_Unity`; een eigen plugin kan dezelfde functie patchen (velden bekend). Het HASP-muntgedeelte van de TP-plugin wordt niet nagebouwd.
- **Rij "Wine-prefix en Proton".** Voeg toe: GStreamer wordt niet meegeleverd (Luigi's), `d3dx9` ook niet (Medaru), wine-mono nodig voor Infinity Blade FX.
- **Rij "Bescherming en licentie".** Aanvullen met de profielvelden die Johan noemt: LGS (`DongleRegion`/`PcbRegion` USA, correctie r8), LGI (JAPAN), NERF-plugin (`mDongleControl`, HASP-muntteller); Patreon=true bij NERF, Otomedius, Omatsuri x2, Pigs Might Fly, Gashaaaan Refill (en Go Go Strike).
- **Sectie 4, per-gametabel.** Let's Go Safari: beschermingsveld nu "ja, dongleregio/PCB (USA)" (was "deels (dongle regio/PCB)": klopt, maar noemt nu USA); LGI/LGI3D: JAPAN. Otomedius: ACIO-protocol bekend, nieuwe
  route is eigen responder op COM2. Omatsuri x2: beide `Patreon=true`, schijfletterherleiding als mogelijke oorzaak van exitcode 8. Medaru: RAM-fout, d3dx9_43 erbij. Luigi's: herstartmarkers. Type X2: JVS-node onbekend, sense-lijn-hypothese. JP/Pacman/Galaga: sectiekopbeperking bevestigd in 1451.
- **Sectie 5, volgorde.** Stap 2 (eigen `pipehelper` en eigen lader) is nu volledig gespecificeerd voor de helper; de lader kan uit de open bron, ElfLdr2 niet. Stap 4 (per-game hook-DLL) kan nu voor Otomedius (ACIO) en
  voor de Ring-games (in-proces amLib) concreter. Stap 5 (Raw Thrills native) blijft geblokkeerd op het gunbordprotocol en op de `PT_DYNAMIC`-ondersteuning.

## 6. Botsingen met onze metingen

1. **Otomedius, COM2.** Johan: de core levert de micom en vertaalt de poortnaam. Onze meting: de 32-bit `TeknoParrot.dll` heeft geen COM2/ACIO-verwerking; hij zag de klasse zelf alleen in de 64-bit core. Gevolg: de eigen responder
   (2.5) kan werken; onze eerdere pty-mislukking kwam waarschijnlijk door de Wine-koppeling van `com2` en door het ontbreken van antwoorden.
2. **Medaru, `SBTF_sram.bin`.** Johan: de core kent de naam niet (0 tekstzoeken). Onze meting: de core maakt de 2 MB-bestanden wel (`SBTF_*`, bij LGI `SBUH_*`). Waarschijnlijk wordt de naam samengesteld uit een game-ID; tekstzoeken vindt dat niet.
3. **Vraag 43 en Type X2.** Johan: bordidentiteit "hard in de core". De pipe-route wordt door TeknoParrotUi's JVS-emulatie beantwoord en een profielspecifieke node bestaat in de open bron.
4. **LGS, knoppen in shm.** Johan: knoppen in het woord op offset 8. Onze indelingsanalyse: open bron schrijft alleen het wiel; schakelaars lopen via JVS-pakketten. Test beide (2.11).
5. **Vraag 34.** Johan's omkering ("nee") spreekt onze stapelmeting niet tegen; het is het gevolg van de oorzaak die hij vond.
6. **Vraag 41 en 42.** Johan noemt ze bij de standaardroute; onze FP-fout treedt juist op de standaardroute op. Het antwoord lost onze Type X2-fout dus niet op.
7. **Schijfletterherleiding voor Omatsuri.** De tabel is alleen in de 64-bit core gezien; Omatsuri draait op de 32-bit core.

## 7. Wat dit betekent voor wat we vannacht deden

- `TP_REMOTETHREAD=off` (standaardroute): bevestigd als bedoelde route voor alle 32- en 64-bit-games (r5). Niet tegengesproken.
- Lader-FP-patch (0x1001F naar 0x10017): niet tegengesproken, ook niet goedgekeurd; Johan reageerde er niet inhoudelijk op. Eigen bronbuild is de nette vervolgstap (2.14).
- LGI-aimvenster: consistent met Johan (profielconstanten, pas het profiel aan). Voor de TPUI-route afgeleide waarden `xAxisMax=124`, `yAxisMax=114` (2.12).
- `d3dx9_40`-aanpak (Medaru): consistent met Johan (gebruiker installeert de herdistribueerbare); voeg `d3dx9_43` toe.
- Virtueel bureaublad bij LGI/LGI3D: blijft nodig, want de `CreateWindowExW`-hook is onvoorwaardelijk en `Windowed=1` crasht (59).

## 8. Buiten scope (beschermingsafhandeling)

Alleen gemeld, niet uitgewerkt en niet gebruikt:

- Dongle- en regioafhandeling: LGS `DongleRegion`/`PcbRegion` = USA (r8); LGI = JAPAN (r20); NERF-plugin `mDongleControl` en HASP-lifetime-muntteller (r11); Johan's advies "beantwoord dit in je eigen lader" volgen we niet.
- Abonnement en activering: de activeringsaanvraag bij Omatsuri (37), de eenmalige controle en de sleutel in `HKCU\SOFTWARE\TeknoGods\TeknoParrot` (51), `Patreon=true` bij NERF, Otomedius, Omatsuri x2, Pigs Might Fly, Gashaaaan Refill (r9-patroon).
- Authenticatie en online registratie in de netwerklaag: sleutel `AmAuthPort` (r5) en de bestanden `Rsacnet*`, `first_auth_date_*` (r10); e-Amusement bij Otomedius.
- Integriteit: de optie "bestandschecksum" in het Otomedius-profiel; het toevoegen van sectiekoppen aan een kopie van de JP-game-ELF (JP heeft een integriteitscontrole; geen wijziging zonder akkoord van het team).
- Uitpakken van beschermde binaries: Johan's antwoorden in r5 (deel B), r9 t/m r19 en r21 komen deels uit "opengestelde" beelden van de afgeschermde kernen en van BudgieLoader (ook 1451). Wij herhalen dat niet, vragen er niet om en gebruiken alleen de beschreven draadformaten. Aanbeveling: niet om nieuwe ontledingen van de gesloten kern vragen.
- Mogelijk beschermingsgerelateerd, nog niet beoordeeld: Kaboom `InitLib` bij Infinity Blade FX (2.7) en de uitzonderingslus van Type X2 (2.8 stap 3). Blijkt een van beide beschermend, dan hier toevoegen en stoppen.
- TPB1: geen aanwijzing voor licentie of activering (3.1); token en sessie worden alleen met eigen waarden gebruikt.
