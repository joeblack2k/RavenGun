# Loaders en patches voor zes games

Doel: per game onze **eigen** lader/patch in plaats van de gesloten TeknoParrot-kern. Volgorde van de
eigenaar, één game tegelijk. Dit document is het overzicht; de details staan in de patchkaart van elke
game.

## De lat, letterlijk van de eigenaar

> "je stopt niet voordat de sega/matrix/elf loader werkt en Luigi's Mansion en Rabbids als voorbeelden
> 5 en 10 minuten draaien"

Dus drie dingen moeten staan: **onze eigen lader werkt** (Sega, Matrix, ELF), **Luigi's Mansion draait
5 minuten** en **Rabbids Hollywood draait 10 minuten**. En het doel van het hele project is spelen met een
**PlayStation Move** als geweer; er is een ingebouwde muismodus, dus de Move hoeft voor een test niet aan.

## Waar het op dit moment staat

| # | game | stand | wat er nog moet |
|---|---|---|---|
| 1 | **Luigi's Mansion** | De pakker is niet het probleem en onze kern patcht niets. Onze `InitializeASI` **komt terug**, de entry-bytes worden **2,36 s na het laden van de kern** teruggezet, het spel springt naar zijn entry point en **beëindigt zichzelf 239 ms later** met 32 -- terwijl de gesloten kern vanaf dat punt doorloopt (`.text` vult zich, threads 1 -> 15 -> 21). De vraag is nu wat er in die 239 ms verschilt | de 239 ms doorgronden, dan de dichte tijdlijn en de **5-minutenproef** |
| 2 | **Rabbids Hollywood** | Onze eigen route werkt en het spel leeft (288 s gemeten). De Matrix-laag antwoordt en het spel neemt ons serienummer over. Het **savegame-probleem is GEEN dongleprobleem**: de Encrypt-wrappers zijn dode code en het pad loopt via een andere klasse plus `ServerRequestManager::GetDeviceID` -- als die per run verschilt, is elk eerder savegame onleesbaar | de **10-minutenrun** (staat klaar) en de device-ID-vraag |
| 3 | **Halo Fireteam Raven** | Geen eigen lader nodig (kernel-ld.so). Onze hasphook heeft nu een adres-route en de drop-ins zijn gelijkgesteld; de echte blokker is het **FTDI-gunbord** (0403:6001) dat niet aangesloten is | run 1 zonder bord (staat klaar) |
| 4 | **Terminator Salvation** | Onze eigen `rg-elfloader` **leest nu 1020 gast-symbolen** via `(DT_STRTAB-DT_SYMTAB)/16` in plaats van te falen op het ontbrekende `DT_HASH`, laadt 13 bibliotheken, past **1011 relocaties** toe en laat 2 onopgelost (alleen `__gmon_start__`). Alleen de sprong zelf is nog onbewezen | de laderrun (staat klaar) |
| 5 | **Transformers Shadows Rising** | De eigenaar heeft het zelf in fullscreen gespeeld. De hele IOBoard-handshake is statisch geoogst, het bord is generiek gebouwd met een antwoordtabel als per-game gegeven, en de joystick-id-controle staat als check 0 in het draaiboek | de bevestigingsrun |
| 6 | **Scarlet Dawn** | Zonder keychip speelbaar; de daemon logt `Keychip Not Found` en probeert opnieuw. De keychip-laag is in kaart (een `DeviceIoControl` op `\\.\FddDriver`) en de volledigheid is nagerekend | completeness-werk, geen startblokkade |

## De benchrij (één partij tegelijk, ik geef hem uit)

1. **Laderbouwer**: de 239 ms onder Luigi doorgronden, met een lichtere trace op beide kernen (alleen
   `RG_INIT_TRACE`) zodat de trace de timing niet verstoort.
2. **Rabbids-werker**: de 10-minutenrun (ongeveer 13 minuten) -- de tweede opdracht van de eigenaar.
3. **Luigi-werker**: de dichte tijdlijn (0,5 s stappen van 2,5 tot 7 s) om te zien wanneer de entry-bytes
   precies worden teruggezet, en daarna de 5-minutenproef.
4. **Terminator-werker**: de ELF-laderrun.
5. **Halo-werker**: run 1 zonder FTDI-bord.
6. **Transformers-werker**: de IO-bordbevestiging, check 0 (joystick-id) eerst.

## De PS Move: het doel van dit werk, en wat er gemeten is

* **Verbinding:** Bluetooth op `hidraw4`, **88 rapporten per seconde**, 0,66 % gemist volgens de
  volgnummers; orientatie geldig met een yaw binnen plus/min 0,4 graden bij een minuut stilzitten.
* **Van richtpunt naar de game:** in het gedeelde geheugen met de **Luigi-indeling** geeft een richtpunt
  van 0,8/0,3 de bytes **202 en 79**, precies de verwachte waarden; trekker bit `0x4`, granaat `0x2`,
  test `0x100`.
* **Een echte breuk, gevonden en hersteld:** in de installatie was `share/ravengun/patches` een **dode
  symlink**, waardoor de daemon geen enkele `game.ini` zag, voor elke game evdev koos en de tp-shm-route
  weigerde.
* **Routes per game na die reparatie:** Luigi, Halo en Terminator = `tp-shm`; Rabbids = `unity-tcp`;
  Transformers = `hid-board`; Scarlet Dawn = evdev via TeknoParrotUi.
* **De muismodus** is "muis als lightgun": hij vervangt de Move voor het RICHTEN, niet de toetsen. Voor
  munt en start is `xdotool` de oplossing gebleken: de game draait op gamescope's eigen Xwayland, dus
  `DISPLAY=:N xdotool key 5` komt aan.
* **Wat niet gemeten of af is:** echte knoppen, het kompas (magnetometer afgewezen: dip 74-78 graden tegen
  60 referentie), de camera (ziet de bol niet), USB (een keuze in de code) en de installatie.

## Vaste regels bij dit werk

* **De games staan op de NAS en de lader start ze daar vandaan.** Dat is het ontwerp: het spel schrijft
  zijn eigen saves daar, en dat is normaal. Wat **niet** mag is dat *onze* artifacts (patches, drop-ins,
  onze eigen bestanden) ongevraagd in een speltmap op de NAS landen.
* **Dongle-, HASP-, keychip-, integriteits- en checksumwerk wordt verdeeld, niet verboden.** De bouwer in
  de lader-repo (Claude) blijft erbuiten en draagt het over; **subagents doen het**.
* **Eén game tegelijk op de bench**, en het scherm van de eigenaar blijft vrij (dus headless).
* **Meet in plaats van te redeneren.** Een meting die niets vindt moet eerst bewijzen dat hij gezocht
  heeft, een uitkomst hoort **vooraf** opgeschreven te zijn, en een samenvatting moet niet liegen over
  wat er is afgewezen of mislukt. Vandaag zijn daar drie echte fouten mee gevangen: een modulevergelijking
  die pogingen met geslaagde loads mengde, een "nooit teruggezet" dat uit een bemonsteringsgat kwam, en
  een verdict dat de zwaarste voorwaarde niet eerst testte.

## Bijvangst buiten de zes

* **Jurassic Park: de hasphook werkt daar nu voor het eerst.** De naam-route telde bij een `ET_EXEC` het
  laagste segmentadres dubbel; gemeten op het echte spelbeeld **oud 0 van 11 aangehaakt, nieuw 11 van 11**.
  Kanttekeningen: het JP-recept laadt de hook nog niet, en JP heeft een eigen blokkade voor `main`.
* **Terminator: de symbolenkaart (50 van 58, met graad per treffer) is de adreslijst van de hook.** De
  test haakt 10 van 11 aan; `hasp_free` is een alias-keten zonder prologue en wordt terecht geweigerd.
* **Gereedschap:** `readelf --dyn-syms` geeft op deze ELF-bestanden **niets** terug omdat `game` nul
  sectiekoppen heeft; lees de dynamische symbolen via `PT_DYNAMIC`.

## Waar de dossiers staan

Per game in `patches/<Game>/`. **De mapnaam is de TeknoParrot-profielnaam**, want dat is wat de app
opzoekt (`HOTDSD`, `Terminator`, `LuigisMansion`, `RabbidsHollywood`, `Halo`,
`TransformersShadowsRising`). Een map met een andere naam wordt door geen enkel script gevonden.

---

## Stand 2026-10-13, na de vierde Luigi-poging (overdracht)

Deze sectie is de actuele stand van de lat die de eigenaar heeft gezet. Alles hieronder is gemeten;
waar iets afgeleid is staat het erbij.

### De lat

1. Onze eigen lader werkt in plaats van de gesloten TeknoParrot-kern.
2. De Sega-kant, de Unity-kant en de ELF-kant werken.
3. Luigi's Mansion 5 minuten en Rabbids Hollywood 10 minuten als voorbeelden, op de PS Move.

### Wat gehaald is

* **Rabbids Hollywood: 10 minuten GEAHAALD** (608 s speeltijd, 115 threads, `quit=0`, `notfound=0`,
  alle 20 levenslijnen verbonden met 120 pakketten/s, view == build, NAS onaangeroerd).
* **De ELF-lader WERKT.** Echte run op Terminator Salvation: `shim geladen: 515 gast-symbolen`,
  alle 13 bibliotheken geladen (2 in een latere ronde), `geladen; spel ... krijgt argv[0]=...`,
  en **`spring naar e_entry 0x8057240`** met exitcode 255 van de GAST en nul FATAAL-regels.
  Twee reparaties maakten dat verschil: de herkansings-lus bij het laden van bibliotheken werkt nu in
  BEIDE modi (libGLU heeft nul DT_NEEDED en leunt op libGL, libm en libstdc++ in de globale scope,
  terwijl de DT_NEEDED-volgorde van de gast libGLU op 2 zet en libm op 9), en het plafond van de
  lib-defs-lijst meldt zich nu luid in plaats van stil af te kappen.
  De gast stopt daarna op zijn EIGEN ptrace-controle: zie `patches/Terminator/BESCHERMINGSLAAG-PTRACE.md`.
* **Onze eigen kern start games op**: Luigi's Mansion komt tot het uitpakken en tot zijn am-laag.

### Wat nog open is bij Luigi's Mansion (de enige game op de lat die nog niet gehaald is)

De drop-in-route werkt nu van begin tot eind -- dat is de winst van deze dag:
plan (`TP_PRELOAD_DLL`) -> view (sha256 klopt) -> in het proces -> het logboek dat de DLL zelf schrijft.

De vierde poging (laatste stand):
* `wachtroute: op een eigen thread gezet; de hoofdthread gaat verder`
* `wachtroute: de pagina's zijn uitgepakt na 2421 ms; nu omzetten`
* `totaal aangehaakt: 16 van 17; afgekeurd: 1`
* levenslijn: entry point van 3,5 tot 5,5 s nog op de TRAMPOLINE, `.text` 100 procent nul, **PROCES WEG op 6,0 s**

Twee dingen moeten eerst uitgezocht worden voordat een vijfde run iets kan beslissen:

1. **De discrepantie in de `.text`-lezing.** De hook meldt dat de pagina's na 2421 ms uitgepakt zijn,
   terwijl de bemonstering `.text` op elk moment tot 5,5 s 100 procent nul zag. Of de hook controleert
   iets smallers (de 17 doelpagina's), of de `.text`-lezing van de bemonstering is hier het kapotte
   instrument. Dat laatste is belangrijk, want de verklaring "de pakker kwam niet klaar" rust erop.
2. **Het adres `0x1402b4270`** (`amDongleIsAvailableInternal`) staat in deze build op `cc cc cc cc`.
   Uitzoeken of dat een verkeerd adres is of een functie die de pakker als opvulling laat, en het
   adres dan weghalen zodat een run 17 van 17 met 16 echte hooks meldt in plaats van 16 van 17.

Daarna: 17 van 17 aangehaakt en het spel leeft langer dan ongeveer 10 s betekent dat de am-laag de
oorzaak was; sterft het nog rond 6 s met 17 van 17, dan is die laag NIET de oorzaak en gaat het werk
terug naar de pakker-fase. Beide uitkomsten zijn evenveel waard.

### Harde regels die onderweg zijn afgedwongen

* De NAS blijft alleen-lezen. De view is de plaats waar bestanden komen; een drop-in-bestemming die
  via een symlink buiten de view valt, faalt nu LUID in de planning in plaats van stil door te gaan.
* De lader blijft schoon: game-specifieke adressen en antwoorden staan in `patches/<Game>/`, het
  gereedschap is generiek (`tools/rt-hasphook/rg_pe_hook*.c` bevat geen game-naam of game-adres).
* Een meting zonder bewijs dat het instrument werkte is geen meting. Onderweg zijn zo gevangen: een
  groene ontleed-modus die de gewone run niet bewees, een leeg `tap start`-logboek dat als "niets
  gevonden" werd gelezen, een drop-in-teller die gegevensbestanden als "niet geladen" telde, en een
  `.text`-lezing die mogelijk het kapotte instrument is.

---

## Stand 2026-10-13, tweede update: de officiele startscripts en het exitbesluit

### De vondst van de dag: elke game heeft een eigen startscript dat wij overslaan

Zeven games hebben een `game.bat` naast de exe, en Halo heeft (als ELF) een `run_game.sh`. Ze doen
allemaal hetzelfde soort dingen die wij niet doen: **eerst een hardwareprobe, dan de game**.

| game | wat het startscript doet |
|---|---|
| LuigisMansion | `set GAMEID=SDCF`; `Y:\SDCF` klaarzetten (en `Y:\` wissen als het ontbreekt); **`sendFlash COM1 LMAIO.bin`**; dan `start /wait x:\exe\x64\VACUUM.exe` |
| HOTDSD | `LAUNCH_ON_IMAGE=1`; `IMAGE_ROOT_DIR=X:\`; `call AppStartup.bat` (zet `LAUNCH_MODE`) en `StartApp.bat` geeft **`-UNATTENDED`** mee |
| LGS / OG | `mxGetHwInfo.exe -s <ini>` en **alleen bij `%ERRORLEVEL% == 0`** start het spel |
| MedaruNoGunman | `cd exe` en dan `start /wait game.exe` |
| Halo | `utils/run_game.sh`: eerst **`rtinit -ct`** als hardwareprobe, en die code kiest de resolutie |

Zie `patches/GAME-BAT-STARTS.md` voor de letterlijke inhoud. Dit is een **systemisch** verschil tussen
onze start en de start van de kast, en het verklaart minstens een gemeten geval: Scarlet Dawn bleef 60 s
op een foutvenster wachten omdat TeknoParrot geen argumenten meegeeft terwijl het eigen script
`-UNATTENDED` gebruikt.

### Luigi's Mansion: waar het nu staat

**Wat uitgesloten is, met metingen:**
* De pakker **komt klaar** onder onze kern: `.text` bereikt 11,2 procent nul (dezelfde eindtoestand als
  de gesloten kern) en het spel sterft ongeveer 0,2 s daarna. Dat is in beide armen gemeten (met en
  zonder drop-in) en nog eens bevestigd door de exit-trace: op het moment van beeindigen is `.text`
  3.104.750 van 3.496.500 bytes niet-nul.
* De **am-laag** is niet de oorzaak: de 16 haken kwamen op echte code en de dood verschoof mee met de
  klok in plaats van te verdwijnen.
* **`GAMEID=SDCF`** maakt geen verschil: het spel sterft nog op 6,0 s.
* **`sendFlash COM1 LMAIO.bin`** is op onze bench een **no-op**: hij probeert het bord vijf keer
  (35 bytes, `/i` `/e` met ETX), krijgt niets terug, verzendt de firmware van 140.688 B nooit, en
  eindigt toch met exitcode 0. Zonder bord is die stap inert.

**Nog open:** de schijfletterroute (`x:\exe\x64\VACUUM.exe`) en het klaarzetten van `Y:\SDCF`.

**De exit is hop voor hop ontleed.** De keten: de init-routine op `VACUUM.exe+0x30f998` (stappen met elk
een eigen foutcode 0x1b, 8, 9, en **geen enkele string**) roept met `call 0x14011c5f0` een thunk aan die
naar `0x140e8189b` springt, waar de virtuele functie met tag `0x919e48fe` wordt binnengegaan; van
binnenuit wordt met tag `0xb63f49fe` de exit bereikt op `0xf1374d`. Die virtuele functie heeft in het
hele beeld **precies een aanroeper**.

**En de "exitcode 32" bestaat niet.** De waarde die aan ExitProcess wordt doorgegeven is `0x140415820`:
een tabel met vijf code-aanwijzers in `.rdata` (een vtable/typebeschrijving, met de string
`can not mutex ctor!` er direct achter) met precies een verwijzing in het hele beeld. Het is dus een
**restwaarde in rcx** die de VM bij zijn staartaanroep laat staan, en de 32 is alleen de laagste byte.
Daarmee is een vraag gesloten die vanaf de eerste exitmeting is meegezeuld.

**Nieuw over de pakker:** `.text` bevat **215 `jmp`-thunks naar `.capcom0`**. De pakker heeft code dus
**gesplitst**, niet alleen gevirtualiseerd: `.capcom0` bevat de echte lichamen van 215 `.text`-functies.

**En een open discrepantie die niet wordt weggepoetst:** de blootstelling na het ontpakken is ongeveer
0,2 s in het ene harnas en 1,26 s in het andere -- een factor zes. Als de dood aan een DUUR hangt en niet
aan een moment, dan is wat die duur bepaalt de kern van de zaak.

### Halo Fireteam Raven

* Run 2: uitkomst **(iii) hard**. De aangescherpte tap was aantoonbaar geladen (15 keer `tap start`) en
  zag in 300 s niets anders; het spel is dus nooit bij `ftdi_usb_find_all()` aangekomen en het venster is
  als verklaring uitgesloten. Het spel LEEDE de hele tijd (17 threads, 10 libfmod-mappings).
* De **FileTest is een voorwaarde, geen zijspoor**: de FTDI-regels zijn er niet terwijl die controle
  faalt. START overslaan is dus de route, en dat is een **al gemeten effect** (van het FileTest-scherm
  naar Webcam Init).
* Halo heeft **geen view en geen `game.bat`** (het is een Linux-ELF, geen Wine-spel), maar wel
  `utils/run_game.sh`, en daarin staat dezelfde soort stap als bij de andere games: **`rtinit -ct`**.
  Een onbekende code laat daar een LEEG resolutieargument achter (`game -f -n`).

### Terminator Salvation

* **Onze ELF-lader werkt**: 13 van 13 bibliotheken, `spring naar e_entry 0x8057240`, exitcode 255 van de
  GAST, nul FATAAL-regels. Twee reparaties maakten dat verschil (de herkansings-lus in beide modi en het
  plafond van de lib-defs-lijst dat zich luid meldt).
* De gast stopt daarna op zijn **eigen ptrace-controle**. Die laag is met een A/B **geklaard**: arm A
  bevestigt de bevinding zonder tracer, arm B laat **390 van 390** aanhakingen slagen (uit 392
  kindprocessen) tegen twee regels in arm A. Maar de gast zegt daarna nog niets en exit 0 is een schone
  zelfbeeindiging, geen spelbeeld. Volgende stappen: de 390 kinderen analyseren (werkerpool of lus) en de
  HASP-aanroepmeting, die nu voor het eerst zinvol is.

### House of the Dead Scarlet Dawn

* **Volledigheid gemeten**: 0 van 2.081 bestanden leeg, 32 PE's niet afgekapt, 0 onopgeloste imports, en
  de pak is aantoonbaar niet afgekapt. Wat niet gezegd kan worden staat expliciet in het dossier.
* **Het spel heeft GESPEELD op deze machine**: zes sessies op 11-10, waarvan een van **7 minuten** met
  `LoadMap(PL_Stage0)` en alle sublevels. Een crashte, vijf werden gestopt. De crash valt op het moment
  dat Stage 0 opkomt; de 60 s stilte daarna komt doordat UE4 op een foutvenster wacht, en het eigen
  startscript geeft daar **`-UNATTENDED`** voor mee. Geen OOM (22,7 GB vrij). Het is een **starter, geen
  omvaller**: sessie 6 liep nog 105 s door na dezelfde map.
* De daemon alleen sterft na 1 s in de **platformlaag** (`amPlatformGetPlatformInformation`), voor de
  keychip. Vier concurrerende verklaringen zijn met 2 s-runs weerlegd; de kern blijft als hoofdverdachte.
* `aimeId is invalid` is de enige fout die de game zelf meldt, deterministisch bij elke Login-stap, en
  **blokkeert het spelen niet**.

### De lat

| eis | stand |
|---|---|
| Onze eigen lader werkt | ja voor de ELF-kant (bewezen) en voor de Sega- en Unity-kant; per game in het dossier |
| Rabbids Hollywood 10 minuten | **GEAHAALD** |
| Luigi's Mansion 5 minuten | **niet gehaald**; het spel sterft ongeveer 0,2 s na een voltooid ontpakken, en de vier stappen van zijn eigen startscript worden een voor een getest |

---

## Stand 2026-10-13, derde update: Halo komt DOOR de FileTest

De TV-run (gamescope met een echt beeld in plaats van headless) liet het spel verder gaan dan het
FileTest-oordeel waar het drie runs op bleef staan:

```
10:25:45  FileTest Failure: '/pm/g7/halo/game'
10:26:05  FileTest Reported 1 errd files at startup!
10:26:05  FMOD - Error initializing! Code: 52 - geen uitvoerstuurprogramma   (verwacht: --mute)
10:26:14  Failed to load shader: cov_shield / elite_shield / energy_sword / jackal_shield / spirit_energy
10:26:22  Failed to load FMOD sound bank: perm (ec 52)
10:26:25  Webcam: Could not ID device for player 0 (0, 0, 0, 0)
10:26:25  Webcam: Aborting auto-id. All webcams reported brightness difference of 0.
10:26:27  Network initialization completed.
10:26:27  System set up periodic reboot after 43200 sec with code 223 at 5:00 hour
```

Dus: **de FileTest is passeerbaar** -- de START-indruk uit het schakelaarbeeld zet het spel verder,
precies zoals het project eerder mat ("van het FileTest-scherm naar Webcam Init"). Daarna loopt de
eigen opstart van het spel: shaders, soundbanken, **webcam-herkenning van de spelers**, netwerk, en
zelfs het periodieke herstarten met de kast-exitcode 223.

**De volgende blokkade is hardware**: de webcam voor de automatische spelerherkenning, bovenop het
FTDI-geweerbord -- de tap zag in deze run nog steeds **geen enkele USB-opsomming**.

**En de eigenaar gaf een concrete aanwijzing**: twee keer de **TEST-knop** slaat de kalibratie over.
In de bitindeling van de invoerhulp is bit 0 = test, dus dat is met dezelfde hulp te geven. De volgende
run doet: START op t=20 s om de FileTest te passeren, daarna twee TEST-pulsen om de kalibratie over te
slaan -- en als dat lukt, hoort het spel in de attract-lus te komen.

De vijf shader-fouten (`ec=-9596`) staan als apart punt genoteerd: die kunnen net zo goed aan de
gamescope-omgeving liggen als aan het spel.

### De lat, aangescherpt door de eigenaar

Een game telt pas als **gehaald** wanneer er **5 minuten attract mode of game mode** loopt. Het
beginscherm telt NIET mee, en dat is bij deze games vaak juist het kalibratiescherm. Voor Halo
betekent dat concreet: door de FileTest komen, de Webcam-stap, het netwerk en het kalibratiescherm zijn
**stappen**, geen gehaalde game. Dat staat hier zodat niemand een tussenstand als eindstand leest.

### De tijdvraag bij Halo is beslecht: het is de BESTANDSCACHE, niet het beeld

| run | beeld | cache | eerste nieuwe logregel | verschil |
|---|---|---|---|---|
| 2 | headless | **koud** (eerste run na lange tijd) | 10:04:31 | **11 s** |
| 3, 4, 5 | tv | warm | telkens dezelfde seconde | **1 s** |
| tijdrun | **headless** | **warm** | dezelfde seconde | **1 s** |

De tijdrun is headless en geeft 1 s, dus de verklaring is niet de gamescope-backend maar de schijfcache.
**Les voor elke meting hierna:** bij een tijdvergelijking hoort te staan of de run warm of koud was. Een
verschil van tien seconden in opstarttijd zegt niets over het spel en alles over de cache.

### En de crash van Halo's run 4 is gevonden: een afkappende schrijver achter een mapping

De invoerhulp las het schakelaarbeeld via `mmap`, terwijl hetzelfde bestand met `open(...,"wb")` werd
geschreven -- en dat **kapt af**. Een afgekapt bestand achter een mapping laat de kernel SIGBUS sturen,
en dat is precies wat er gebeurde. Reparatie aan twee kanten: de hulp leest nu met `pread` in plaats van
`mmap`, en de pulsen schrijven op hun plaats met `pwrite` in plaats van `wb`. Bench-vrij bewezen: een
mmap-lezer sterft met `Bus error`, een pread-lezer blijft leven. **En eerlijk vermeld:** run 5 veranderde
drie dingen tegelijk, dus of de crash ook aan de vier borden lag is daarmee NIET bewezen.

---

## Stand 2026-10-13, vierde update: de invoer van Halo en de verificatie van Transformers

### Halo: de aim is zeer waarschijnlijk OPTISCH, en er hangt wel een camera

Wat uitgesloten is, met metingen op het juiste scherm:
* **TEST** slaat de kalibratie niet over: negen indrukken over drie schakelaarindices (run 7), acht
  herhalingen van 3 s (run 10, vervuild maar de tafelmeting is schoon) -- alleen `k=4` verandert, in zijn
  eigen tellers, en de game schrijft **niets** terug.
* **De trekker en de positie** doen het ook niet: de virtuele lightgun zond vijf trekkerindrukken en
  posities van 0 tot 32767 op beide assen uit, inclusief de uiterste linkerbovenhoek die het scherm
  vraagt (run 8, gemeten op het apparaat zelf) -- het scherm verschoof niet.
* **Het is geen joystickkwestie**: `udevadm info` zegt `ID_INPUT_MOUSE=1` en **geen `ID_INPUT_JOYSTICK`**,
  maar de meting die dat beslissend maakt is dat de game **geen enkel invoerapparaat openhoudt en SDL
  niet geladen heeft** (run 9). Een tweede apparaat bouwen zou dus een apparaat zonder consument zijn --
  dat is niet gedaan.
* **De webcam is niet het verschil**: dezelfde fout staat in de sessie van 10 oktober, die 47 s later WEL
  in de attract-lus kwam.

Wat er WEL is, en dit is de leidende verklaring:
* Er hangt **wel** een camera: `/dev/video0`, "USB Camera-B4.09.24.1" (gspca). De game meet een
  **helderheidsverschil** in het beeld -- dat is zoeken naar een lichtpunt -- en vindt niets
  (`All webcams reported brightness difference of 0`). Op het kalibratiescherm houdt hij zelfs geen
  camera open.
* De conclusie is dus: het geweer moet door de camera **gezien** worden, en de trekker komt van de
  kastkaart. Onze virtuele apparaten zijn voor een camera onzichtbaar en kunnen dat principe niet
  vervangen. **STERKE AFLEIDING, geen bewijs** -- het bewijs zou zijn dat het scherm opschuift zodra er
  iets zichtbaars voor de camera staat.
* Een testbaar idee dat daarbij past: een PlayStation Move heeft een **lichtend bolletje**, precies een
  lichtpunt dat een camera kan zien. Voor de invoer is de muismodus genoeg (de eigenaar had daarin
  gelijk), maar voor DEZE game kan de Move als **vizier** nodig zijn in plaats van als invoerapparaat.
* De volgende stap is daarom bench-vrij: lezen **welk kanaal TeknoParrot gebruikt voor de aim van dit
  spel** (`Teknoparrot/findings/16_halo_fireteam.md`, de IO- en JVS-sectie). Is dat het kastbord of de
  tp-shm-route, dan weten we precies welk stuk onze native route mist.

### Transformers: de verificatierun toont de hele keten

Voor het eerst is het TWEEDE proces gemeten. In de verificatierun staan drie processen **tegelijk** in de
proceslijst, ongeveer acht minuten lang:
```
OpenParrotLoader.exe                <- de lader met de TeknoParrot-kern
.../Sega/shell/Shell.exe            <- de shell
.../Transformers2/Transformers2.exe <- HET SPEL zelf
```
Dat is precies wat de opdracht per game vraagt: niet dat de lader start, maar dat het SPEL start. De
eerdere STARTTEST-regel mat alleen de shell -- dat onderscheid is nu gedicht.

### En een les over onze eigen bench

De bench-controles waren per route en zagen elkaar niet: de Wine-kant keek naar gamescope/wine/wineserver/
de lader, de native kant naar gamescope en het `game`-proces. Daardoor liepen op 13-10 twee runs
tegelijk (Halo run 10 en de Transformers-verificatierun), en die eerste is daardoor **vervuild** -- dat
staat als blok boven aan dat verslag, en de conclusie rust op de twee schone runs. Beide partijen
controleren nu ook op de processen van de ANDERE route.

### Drie correcties van mij, voor het archief

1. Ik zei dat er geen camera was; die is er wel.
2. Ik dacht dat een tweede joystickapparaat de route was; de meting laat zien dat de game geen enkel
   invoerapparaat opent.
3. Ik las een lopende run als "voorbij zijn time-out" terwijl hij gewoon nog liep.

---

## Stand 2026-10-13, vijfde update: DRIE VAN DE ZES STAAN OP DE LAT

De lat is, zoals afgesproken: **vijf minuten attract- of spelmodus** -- het beginscherm, het
FileTest-scherm, de webcamstap en het kalibratiescherm tellen NIET mee.

| game | stand | bewijs |
|---|---|---|
| **Rabbids Hollywood** | **GEHAALD** (10 min, 115 threads) | eerdere meting |
| **Transformers Shadows Rising** | **GEHAALD** (~8,5 min attract) | de toestandsmachine laadt de attract-cyclus 5x opnieuw (42 attract-gebeurtenissen: `ATTRACT_START`, dan `ATTRACT_LOGO 1..4`, dan `ATTRACT_VIDEO`), en de foto van 11:25:04 toont het spel volledig in beeld met `PRESS START BUTTON / FREE PLAY` |
| **Halo Fireteam Raven** | **GEHAALD** (8+ min attract) | het bankverloop `attract -> level1 -> level2 -> perm -> attract -> level1 -> level2 -> level3 -> level4` van 11:39:41 tot 11:47:49, dus acht minuten en acht seconden, met vier verschillende leveldemo's |
| Luigi's Mansion | niet gehaald | sterft ~0,3 s na een geslaagde ontpakking; vier stappen uit het eigen startscript zijn doorgemeten |
| Terminator Salvation | niet gehaald | geblokkeerd VOOR de HASP-API (0 aanroepen bij 387 geslaagde attaches); bestandsmeting staat klaar |
| House of the Dead Scarlet Dawn | niet gehaald | structureel compleet; de daemon A/B moet beslissen |

### Halo: wat de doorbraak precies was

* **START brengt het spel verder, TEST doet NIETS.** Zes TEST-pulsen op ALLE VIER de borden lieten het
  logboek exact op 256 regels staan en alle zeven foto's bleven de kalibratie van 1,47 MB; de vier
  START-pulsen lieten het logboek groeien en de foto's naar 2,7 tot 3,1 MB springen (een echt scherm).
  Onze eigen notitie zei het omgekeerde en is hierbij gecorrigeerd.
* **De munt werkt**: na vier muntpulsen staat er `Money In: $16.75` op het scherm, met de foto als bewijs.
* **Wat het spel verder vraagt**: QR scanners (`P1/P2 OR Scanner is not detected`,
  `Factory Setup Has Not Been Completed`). Dat is kasthardware die wij niet nabootsen, en het spel gaat
  er toch door naar de attract.
* **Wat er nog open is voor deze game**: de AIM en de TREKKER. Onze hulp schrijft alleen de SCHAKELAARS;
  `p1.trigger` is bit 7 van offset 8 in het bordprofiel en de acht asbytes staan op offset 12 tot 19, en
  geen van beide wordt geschreven. Dat is twee gerichte uitbreidingen van EEN functie -- geen nieuw
  apparaat. (Een geweer dat perfecte posities en trekkerindrukken uitzond veranderde niets, want het
  sprak een ander kanaal dan de game leest.)
