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
