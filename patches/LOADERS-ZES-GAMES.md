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
