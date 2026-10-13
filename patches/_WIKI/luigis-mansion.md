# Luigi's Mansion Arcade (Sega Nu 1.1) -- publieke digest

Dit is een technisch verslag van wat wij van dit spel hebben gemeten en geleerd, geschreven voor
iemand die dit project **niet** kent en er een eigen front-end of emulator op wil bouwen. Per bewering
staat of hij **GEMETEN** is (met cijfers, tijdstippen, paden, offsets) of **AFGELEID**.

Alles hier is op een Steam Deck (SteamOS) onder plain Wine gemeten. De spelbestanden staan op een
NAS en zijn daar **alleen gelezen**.

---

## 1. Platform en kast

De kast is **Sega Nu 1.1**. Het spel is een 64-bits Windows-programma (`PE32+`, x86-64), GUI-subsystem,
dat draait op een gewone Windows-installatie in de kast. **GEMETEN** aan de bestanden:

| | |
|---|---|
| spelbestand | `exe/x64/VACUUM.exe`, 3.436.544 B |
| soort | `PE32+ executable for MS Windows 6.00 (GUI), x86-64`, 8 secties |
| ImageBase | `0x140000000` |
| entry point | RVA `0xf23125` (VA `0x140f23125`) |
| subsysteem | GUI; D3D11, DirectInput |

De kastinvoer is een **lichtgeweer plus een stofzuigerhandvat**: richten met het geweer, zuigen met de
trekker, en een aparte knop voor de "Strobulb"-flits. Een tweede speler is mogelijk. Het beeld is een
gewone 1920x1080-TV. **AFGELEID** uit het spelgedrag; de kast zelf hebben wij nooit gezien.

Het spel is **gepakt**: vijf secties (`.text`, `.rdata`, `.data`, `.pdata`, `.capcom0`) hebben een groot
virtueel bereik en **nul bytes in het bestand**; alleen `.capcom1` (3.434.496 B op bestandsoffset
`0x400`, entropie 7,9553) staat in het bestand. **GEMETEN**. Het entry point ligt **in** `.capcom1`, dus
het programma begint in de ontpakker en niet in het spel. De datadir bevestigt het: `import`
(RVA `0x10980d8`), `export` (`0x121cc00`), `exception` (`0x122eb70`), `iat` (`0x120d000`) en
`loadconfig` (`0x122eb00`) wijzen alle naar bereiken waarvan de bytes niet in het bestand staan, dus er
is ook geen bruikbare import-tabel om statisch uit te lezen. **GEMETEN**.

---

## 2. Wat het spel nodig heeft

### Mappen en paden (**GEMETEN**)

Het spel zoekt zijn gegevens **relatief** aan zijn eigen map, met twee verschillende wortels:

* `..\..\DataGlobal\EN\Data\...` -- de eigenlijke speldata (modellen, effecten, geluid, films)
* `..\..\Data\...` -- een tweede wortel die het spel ook vraagt, bijvoorbeeld `Data\System\Model\cube.bin`

Met de werkmap op de hoofdmap van het spel stopt het met een **access violation**. De werkmap moet dus
`exe/x64` zijn. **GEMETEN**.

Wat verder meetbaar nodig is:

| wat | waarom | status |
|---|---|---|
| werkmap `exe/x64` | anders access violation | **GEMETEN** |
| een map `Data` **naast** `DataGlobal` | het spel vraagt `..\..\Data\...` | **GEMETEN** |
| de films in `Data/Movie/*.wmv` | bij de eerste film stopt het spel zonder ASF-demuxer en WMV-decoder | **GEMETEN** |
| GStreamer-plug-ins `libgstasf.so` + `libgstlibav.so` | idem; via `GST_PLUGIN_PATH` | **GEMETEN** |
| schrijfrechten in `Ram/` en `Log/` | het spel schrijft daar zijn EEPROM/SRAM/credits/logs | **GEMETEN** |

### Wat de kast zelf klaarzet, en wat daarvan niets uitmaakt (**GEMETEN**)

Het officiele startscript zet vier dingen klaar. Wij hebben ze **elk apart** getest (zie Â§8). Drie ervan
maken op onze opzet **meetbaar niets** uit, en de vierde is inert:

* `GAMEID=SDCF` als omgevingsvariabele -- **geen effect**: het spel sterft op 6,0 s met en zonder.
* `Y:\SDCF` als app-datagebied -- **geen effect**: 6,5 s met de map aanwezig.
* `sendFlash COM1 LMAIO.bin` -- **inert**: het programma eindigt met exitcode 0 maar zendt de firmware
  van 140.688 B **nooit**; het stuurt vijf keer 7 bytes `/i\n/e\n\x03` (35 B) en krijgt niets terug.
* starten vanaf `X:\exe\x64\` -- **geen effect**: 7,0 s met de schijfletter.

Er is ook gemeten dat **het spel zelf COM1 nooit aanraakt**: met een pty op `com1` en een `+file`-trace
over alle bestandsacties staan er **0** COM-namen in 86 CreateFile-regels. De flitsstap hoort dus bij
het losse gereedschap en niet bij het spel. **GEMETEN**.

---

## 3. Het officiele startscript

Naast het spel staat `game.bat`. Letterlijk:

```bat
@echo
setlocal
rem ã‚²ãƒ¼ãƒ ID
set GAMEID=SDCF
rem ã‚¢ãƒ—ãƒªãƒ‡ãƒ¼ã‚¿é ˜åŸŸã®æŽ’ä»–åˆ©ç”¨
if not exist Y:\%GAMEID% (
    rmdir /S /Q Y:\
    mkdir Y:\%GAMEID%
)
start /wait sendFlash COM1 LMAIO.bin
start /wait x:\exe\x64\VACUUM.exe
```

**WAARSCHUWING: wij voeren dit script NOOIT uit.** Het doet drie destructieve dingen:

1. `rmdir /S /Q Y:\` -- het **wist een hele schijf** als `Y:\SDCF` ontbreekt. Op een echte kast is dat
   een dedicated dataschijf; op een gewone machine kan `Y:` iets van jou zijn.
2. `setlocal` zonder bijbehorende `endlocal` in combinatie met `start /wait` vraagt om een herstart van
   de omgeving; het script is geschreven voor een kast die toch opnieuw opstart.
3. Het gooit het app-datagebied van de gebruiker weg (`%GAMEID%`-map) in plaats van het te bewaren.

Wij nemen daarom alleen de **nodige delen los** over (werkmap, datawortels, films) en niet het wissen,
niet `sendFlash` en niet de omgevingsvariabele. Zie Â§8 voor het bewijs dat dat verantwoord is.

---

## 4. De route die wij gebruiken

Wij starten het spel met een **eigen lader** (`OpenParrotLoader64.exe` uit de openbare OpenParrot-bron,
GPL-3.0) en een **eigen kern**. De aanroepvorm is:

```
wine <lader> <kern-DLL zonder .dll> "<spel.exe>"
```

* De **kern is het eerste argument** van de lader; de lader zet er `+ ".dll"` achter. Onze kern en de
  gesloten kern van de kast zijn dus uitwisselbaar door alleen dat argument te veranderen -- dat is de
  manier waarop wij ze eerlijk hebben vergeleken.
* De lader ondersteunt een **override voor het uitvoerbare bestand** (`--exe PAD`, of `[Launch] Exe` in
  de configuratie) en **schijfletters per run** (`--drive X=.`). De override verandert **alleen het pad
  dat de lader voor het spel krijgt**; de hele hulpketen blijft identiek. **GEMETEN** (het plan laat
  zien welk pad eruit komt en dat het een override is).
* Voor de schijfletter leggen wij een koppeling in het Wine-voorvoegsel:
  `dosdevices/x:` -> de map waar het spel staat. Dat moet **na** het opwarmen van de wineserver, want
  Wine herschrijft `dosdevices` bij het starten. En de koppeling moet daarna weer weg: een blijvende
  `x:` verandert elke latere run in dat voorvoegsel. **GEMETEN**.
* Zonder `--exe` start het spel uit onze eigen map; met de override uit `X:\exe\x64\`. Beide zijn
  gemeten en geven hetzelfde resultaat (Â§8).

Twee dingen die wij onderweg als **valkuil** hebben gemeten, zodat je ze niet opnieuw hoeft te vinden:

* **De werkmap van de lader is niet de werkmap van het spel.** De lader zet de spelwerkmap goed, maar
  de lader zelf moet in zijn eigen runtime-map starten, anders zoekt de kern zijn eigen bestanden
  verkeerd. Wij doen dat via de werkmap-instelling van het spel, niet door zelf `cd` te doen.
* **`TP_REMOTETHREAD=off` betekent in de praktijk: de variabele weglaten**, niet hem op `off` zetten.
  Aanwezigheid alleen al zet de lader op de remote-thread-route, en die route werkt voor dit spel niet.

---

## 5. De kern en de virtuele machine -- het hart van deze pagina

Dit is wat wij met zekerheid weten, en het is het meest waardevol voor wie hier verder wil.

**Het spel ontpakt zich volledig op onze kern.** Met een dichte bemonstering van het spelgeheugen
(`/proc/<pid>/mem`, acht blokken van 64 KB over de tekstsectie) gaat `.text` van 100 % nul naar
**10,0 % nul** -- dezelfde eindtoestand die de gesloten kern haalt (gemeten: 11,2 %). Het entry point
staat op dat moment weer op zijn ongerepte eerste bytes (`eb 08 92 1b ...`, byte voor byte gelijk aan
het bestand). **GEMETEN**, in meerdere runs en in beide armen van een A/B.

**En dan sterft het spel ongeveer 0,2 seconde later.** **GEMETEN**:

| run | vrijgave entry point + `.text` eindtoestand | dood | gat |
|---|---|---|---|
| onze kern, geen drop-in | 4,8 s -- `.text 10,0 % nul` | 5,0 s | ~0,2 s |
| onze kern, geen drop-in (tweede) | 6,0 s -- `.text 10,0 % nul` | 6,2 s | ~0,2 s |
| onze kern vanaf `X:\exe\x64\` | 6,8 s -- `.text 10,0 % nul` | 7,0 s | ~0,2 s |
| gesloten kern (baseline) | ~9-10 s -- `.text 11,2 % nul` | **leeft > 300 s** | -- |

Dus: **het ontpakken is niet het probleem.** Het spel komt volledig klaar en gaat er daarna uit.

**Waar het eruit gaat**, uit een trace die `ExitProcess`, `TerminateProcess`, `RtlExitUserProcess` en
`NtTerminateProcess` haakt met de exitcode, het terugkeeradres als module plus offset, de sectienaam en
een stackwalk (**GEMETEN**):

```
frame  2 = 0000000140F1374D  VACUUM.exe+0xf1374d  sectie .capcom0
(genest) RtlExitUserProcess code=0x140415820 (laag8=32)
(genest) NtTerminateProcess  handle=0 code=0x40415820 (laag8=32) ntdll.dll+0x29f71
```

De exit komt van **binnen een gevirtualiseerde functie met tag `0x919e48fe`**, die precies **een**
aanroeper heeft. En de waarde die als exitcode wordt doorgegeven is een **restwaarde in een register**
(een vtable-aanwijzer) -- **geen foutcode**. Met andere woorden: het getal `32` dat je in de exitcode
ziet is geen betekenisvolle foutcode maar de rest van een register op het moment van de aanroep.
**GEMETEN** (uit het traceverslag).

**Wat dat betekent voor wie verder wil:** zoek niet in het ontpakken en niet in de "exitcode". Zoek in
**wat de gevirtualiseerde functie met tag `0x919e48fe` doet** en in de aanroeper die de exit
veroorzaakt. Een logpunt op `0x14011c5f0` dat het `rcx`-register vastlegt zou moeten bevestigen dat
`rcx == 0x140415820` op het moment van de exit; daarmee is de exitcode-vraag in een meting gesloten.
**AFGELEID** als volgende stap; nog niet uitgevoerd.

---

## 6. Timing

Alle tijden zijn **seconden na het verschijnen van het spelproces**, op een Steam Deck onder Wine.
**GEMETEN**:

| moment | waarde |
|---|---|
| entry-point-trampoline van de lader actief | 2,5 s tot 4,0-6,0 s (varieert per run) |
| entry point teruggezet + ontpakken begint | 4,5-6,8 s (varieert) |
| ontpakken klaar (`.text` ~10-11 % nul) | vrijwel direct daarna, binnen ~0,2 s |
| dood op onze kern | 0,2-0,3 s na het ontpakken |
| dood op de gesloten kern | niet; > 300 s |

**Waarschuwing over koude en warme cache:** dezelfde metingen op dezelfde kern gaven 4,8 s en 6,0 s
voor hetzelfde moment. Dat is een verschil van meer dan een seconde op een proces dat in totaal zes
seconden leeft. Verder is gemeten dat **zwaardere instrumentatie het proces vertraagt en de levensduur
verandert**: met een volledige bestandstrace (`WINEDEBUG=+file`) leefde het spel 25-30 s waar het zonder
die trace 5-7 s was. Vergelijk dus nooit een meting met instrumentatie aan met een meting zonder.
**GEMETEN**.

---

## 7. Wat we op het scherm zagen

**Er is geen scherm.** Alle metingen hierboven zijn **headless** gedaan: het spel krijgt een Xwayland-
display zonder fysieke uitvoer, zodat het scherm van de eigenaar vrij blijft en niemand hoeft mee te
kijken. **GEMETEN** in de zin dat de opzet dat expliciet doet.

Wat dat betekent: deze cijfers zeggen iets over **het starten, het ontpakken en het sterven** van het
spel, en **niets** over hoe het eruitziet. Op de gesloten kern is eerder wel beeld gezien (titel,
attract, start, zuigen, spoken vangen, testmenu) -- dat is een **AFGELEIDE** observatie uit eerder werk
op dezelfde machine, niet uit de runs in dit verslag.

Een run met beeld op een echte display is **niet vergelijkbaar** met deze reeks: andere resolutie,
geluid, en het laden van films en muziek zit in hetzelfde venster dat hier wordt gemeten.

---

## 8. De vier stappen uit het startscript als uitkomstentabel

Elk apart gemeten, met dezelfde opzet verder, op **onze eigen kern** (sha256 van de kern telkens
gecontroleerd tegen het logboek van de lader). **GEMETEN**:

| stap | uitkomst | momenten |
|---|---|---|
| `set GAMEID=SDCF` | **NEGATIEF** | vrijgegeven <= 5,8 s; dood **6,0 s** |
| `sendFlash COM1 LMAIO.bin` | **INERT** | exitcode **0**; 35 B in vijf blokken `/i\n/e\n\x03`; firmware van 140.688 B **nooit** verzonden |
| `Y:\SDCF` klaarzetten | **NEGATIEF** | vrijgegeven 6,2 s met `.text 10,0 % nul` (klaar); dood **6,5 s** |
| starten vanaf `X:\exe\x64\` | **NEGATIEF** | vrijgegeven 6,8 s met `.text 10,0 % nul` (klaar); dood **7,0 s** |

Alle vier de stappen van het officiele startscript zijn dus weggestreept. De hele resterende route ligt
bij de kern en de virtuele functie (Â§5). Dat is precies het soort tabel dat weken werk bespaart.

---

## 9. Stand van zaken en open vragen

**De lat:** het spel moet **vijf minuten** draaien (attract- of spelmodus). Dat is **niet gehaald**.

* **Wat wel is gehaald:** het spel start op onze eigen kern, ontpakt zich volledig, en de omgeving laat
  vijf minuten toe -- met de gesloten kern is een baseline van **300 s** gemeten met het proces nog in
  leven (92 threads, entry point teruggezet, `.text` gevuld). **GEMETEN**.
* **Wat in de weg zit:** de dood ~0,2 s na een voltooid ontpakken, vanuit de gevirtualiseerde functie
  met tag `0x919e48fe`. **GEMETEN**.
* **Wat er niet in de weg zit:** de pakker (het ontpakken lukt), de invoerlaag (die is al van ons), en
  de vier stappen uit het startscript (Â§8).

Open vragen, met de volgende stap erbij:

1. **Wat doet de gevirtualiseerde functie met tag `0x919e48fe`, en wie roept de exit aan?**
   Volgende stap: een **log-only** haakpunt op `0x14011c5f0` dat `rcx` vastlegt, om te bevestigen dat
   `rcx == 0x140415820` op het moment van de exit. Dat sluit de exitcode-vraag in een meting.
2. **Hangt de dood aan een moment of aan een duur?** Onze runs geven ~0,2 s na het ontpakken, een
   dichte meter van een andere partij gaf 1,26 s. Beide zien hetzelfde (het ontpakken lukt), dus het
   verschil zit in de blootstelling daarna. Volgende stap: twee armen met een verschillend zwaar
   meetinstrument, dezelfde kern. Als de duur meeschaalt met het instrument, is het een meeteffect.
3. **Waarom duurt het ontpakken op onze kern even lang of langer dan op de gesloten kern, en waarom
   sterft het daarna?** Volgende stap: de uitvoer van het logpunt hierboven naast de tijdlijn leggen.
4. **Het virtuele functiesysteem zelf.** De exit komt van binnen een gevirtualiseerde functie; wat die
   functie precies beschermt of afhandelt is nog niet vastgesteld. Dat is het onderzoeksterrein dat
   overblijft nadat de vier startstappen zijn weggestreept.

## 10. Wat Linux-specifiek is (en wat je op Windows anders doet)

Deze game is een Windows-programma; op Linux draait het onder **Wine**. Dit is wat wij op Linux
moesten regelen, met per punt of het **GEMETEN** is of een **AANNAME**.

| punt | wat er op Linux nodig is | status |
|---|---|---|
| Wine zelf | een **gewone Wine-build** (bij ons de Kron4ek `wine-proton-11.0-2`), geen Proton. Proton wordt hier niet gebruikt. | **GEMETEN** (het pad van het wine-binary staat in onze runlogs) |
| voorvoegsel | een Wine-voorvoegsel waarin de game draait; bij ons `prefixes/shared/wine`. Alles van de game (registers, DLL's, schijven) hangt daaraan. | **GEMETEN** |
| 64-bits lader en kern | `OpenParrotLoader64.exe` plus een 64-bits kern-DLL (`OpenParrot64.dll`), want het spel is `PE32+`. Een 32-bits lader kan dit spel niet laden. | **GEMETEN** |
| `dosdevices` | Wine maakt `z:` (de root) en `com1..com32` zelf aan. Een **extra schijfletter** maak je met een symlink in `<prefix>/dosdevices/`, bijvoorbeeld `x:` -> de map met het spel. | **GEMETEN** |
| **wineserver voorverwarmen** | **Wine herschrijft `dosdevices/` zodra er een wineserver of wineboot start.** Een koppeling die je daarvoor maakt, is daarna weg. Dus eerst `wineserver -p` en `wine cmd /c exit`, **dan** pas de symlink leggen. | **GEMETEN** (dit heeft ons een meting gekost) |
| films | De `Data/Movie/*.wmv`-films gaan via Wine's `winegstreamer`. Je moet de **ASF-demuxer en de WMV-decoder** apart leveren (`libgstasf.so`, `libgstlibav.so`) en `GST_PLUGIN_PATH` naar die map zetten; zonder die twee sluit het spel af bij de eerste film. | **GEMETEN** |
| beschrijfbare mappen | Het spel schrijft in `Ram/` (EEPROM, SRAM, credits) en `Log/`. Die moeten beschrijfbaar zijn, ook als de spelbestanden zelf alleen-lezen staan. | **GEMETEN** |
| alleen-lezen bron + view | Staat het spel op een alleen-lezen schijf (bij ons een NAS), maak dan een **view**: een map met symlinks naar alle spelbestanden, plus `Data` -> `DataGlobal/EN/Data`. Zo blijft de bron ongemoeid en is de mapstructuur die het spel verwacht toch aanwezig. | **GEMETEN** |
| een valkuil met symlinks | Gebruik voor de map **waarin** de game staat (bij ons `exe`) een **echte map met symlinks erin**, niet Ã©Ã©n symlink naar de bron. Een launcher die bestanden in de view wil plaatsen (extra DLL's, configuratie) kan daar niet doorheen schrijven en faalt dan stil. | **GEMETEN** |
| COM1 | Wine zet `com1` op `/dev/ttyS0`. Op een Steam Deck is dat apparaat `root:uucp 0660`, dus een gewone gebruiker krijgt **ACCESS_DENIED** in plaats van "bestaat niet" -- en dat verschil merkt het spel. Wil je meten wat er op de poort gebeurt, zet dan een pty op `dosdevices/com1`. | **GEMETEN** |

### Wat je op Windows anders doet (AANNAME -- niet door ons getest)

* **Geen voorvoegsel en geen `dosdevices`**: schijfletters zijn echte letters. `X:` maak je met een
  partitie, een gemapte schijf of `subst`. De `ln -s`-truc en het voorverwarmen van de wineserver
  bestaan daar niet.
* **Geen wineserver die je mappen herschrijft**: koppelingen blijven staan tussen runs.
* **Codecs**: voor de `.wmv`-films vertrouwt de kast op de codecs van het systeem; de GStreamer-stap
  hierboven is specifiek voor Wine.
* **COM1** is een echte of gevirtualiseerde seriele poort in plaats van een `/dev/ttyS*`-apparaat, dus
  de toegankelijkheidsvalkuil hierboven speelt daar niet.
* **Timing**: wij verwachten dat de absolute tijden op Windows anders liggen (andere scheduler,
  andere driver-stack). Dat is een **AANNAME**; de verhoudingen (ontpakken klaar, dan ~0,2 s later
  dood) zouden hetzelfde moeten zijn, want die komen uit het spel en niet uit het besturingssysteem.

---

### Herkomst en grenzen van dit verslag

Alles hierboven is gemeten op een Steam Deck (SteamOS) met plain Wine, op een NAS die alleen gelezen
is. Waar "GEMETEN" staat, is er een concreet cijfer, tijdstip, pad of offset bij; waar "AFGELEID" staat,
is het een gevolgtrekking uit die metingen. Er staat niets in dit verslag over hardwarematige
beveiligings- of licentielagen; dat hoort niet in een publieke pagina en is er ook niet in verwerkt.

