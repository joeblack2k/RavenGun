# Compatibiliteit per spel — de eerlijke stand (2026-10-14)

**Waarom dit bestand bestaat.** De opdracht is een Dolphin-achtige launcher: bibliotheek, dubbelklikken,
spelen met de PS Move. Daarvoor is één ding nodig dat we nog niet hebben: een lijst die zegt wat écht
werkt, niet wat start. Dit is die lijst, en hij is strenger dan de vorige omdat de lat nu "speelbaar" is
en niet "vijf minuten attract".

**De belangrijkste eerlijke regel.** Vanavond hebben negen titels vijf minuten attract of spel gehaald op
ONZE EIGEN kern, met een onafhankelijke opname als bewijs. **Geen enkele titel is door een mens met het
echte lichtgeweer gespeeld.** Dat betekent dat er op dit moment geen enkele titel WORKING is in de zin van
de opdracht. De hoogste stand die we eerlijk mogen opschrijven is WACHT-OP-HARDWARE.

## De statussen

* **WORKING** — alle acceptatiecriteria gehaald, inclusief een echte speelsessie met het lichtgeweer.
* **WACHT-OP-HARDWARE** — het spel draait en blijft draaien op onze kern; alleen de geweer- en
  bedieningsproef door een mens ontbreekt.
* **DEELS** — het draait, maar op de verkeerde route (bijvoorbeeld andermans kern), of er is een
  voorwaarde niet vervuld.
* **GEBLOKKEERD** — het start niet of stopt; de reden staat erbij.
* **NIET-BEGONNEN** — nog niets aan gedaan.

## De lijst

| Spel | Stand | Bewijs | Wat ontbreekt voor WORKING |
|---|---|---|---|
| Halo Fireteam Raven | WACHT-OP-HARDWARE | In STATUS.md staat een echte speelsessie op de tv (2026-10-07); vanavond opnieuw attract op onze native kern | Lichtgeweer-aansluiting op de native route; de QR-/fabrieksmeldingen |
| Transformers Shadows Rising | WACHT-OP-HARDWARE | Echt beeld via onze eigen hid-board-route; richtpunten binnen 1,5% op elk rasterpunt | De calibratie in het spel zelf, en een echte speelsessie |
| Luigi's Mansion | WACHT-OP-HARDWARE | ~6 min attract op onze kern, geen melding in beeld | Invoer (vizier en trekker) is nooit aangesloten geweest |
| Terminator Salvation | WACHT-OP-HARDWARE | 13 min 55 s onafgebroken op de tv (onafhankelijk gemeten) | De bestandswaarschuwing bij het starten; invoer alleen synthetisch getoond |
| Rabbids Hollywood | WACHT-OP-HARDWARE | 288 s attract op eigen route (zonder TeknoParrot) | Voorbij de wereldkeuze een level in; de savegame-melding |
| Jurassic Park | WACHT-OP-HARDWARE | 12 min attract, en sinds vanavond de RIO-melding WEG in een frame | Het bord moet ANTWOORDEN, niet alleen aanwezig zijn; daarna vizier en trekker |
| Silent Hill The Arcade | WACHT-OP-HARDWARE | 12 min attract op onze kern | De invoerbrug in onze vervangende bibliotheek levert nog niets |
| Raccoon Rampage | WACHT-OP-HARDWARE | 12 min 52 s attract, onafhankelijk gemeten | De kalibratiemelding weg krijgen, dan vizier en trekker |
| NERF Arcade | **WACHT-OP-HARDWARE (verst)** | **11 minuten GESPEELD op onze eigen kern** via de RIO-brug en het app-pad: start, vizier en trekker uit de daemon, twee rondes, 1.176 trekkerdrukken, en de ronde staat in de eigen topscoretabel van het spel als "8. DART 94,350" (`bewijs-bridge-20261014/`, o.a. `08-highscore-table-our-round-94350.jpg`, `11-mouse-at-075-035-crosshair-follows.jpg`) | De echte arcade-lightgun in een mensenhand -- het meten gebeurde met de daemon en met een muis; en de laatste ~65 s van de run gaf de daemon neutrale invoer omdat de run buiten de app-eenheid startte |
| House of the Dead Scarlet Dawn | DEELS | Alleen op de GESLOTEN kern getoond (TeknoParrotUi, `TeknoParrot64.dll`); onze eigen kern heeft het nooit gedraaid | Eerst op onze eigen kern zien te draaien, dan de rest |
| Men in Black | GEBLOKKEERD | De keten start op onze kern, het spel laadt, toont het logo en stopt na ~4 s; de kast herstart het acht keer in vier minuten | De onbeantwoorde uitdaging van het spel aan de kast (bij mij in behandeling) |
| Metal Gear Arcade | NIET-BEGONNEN | Door de eigenaar uitgesteld | Alles |
| Mario Kart DX | NIET-BEGONNEN | Door de eigenaar uitgesteld; geen lichtgeweerspel, heeft een controller en webcam nodig | Alles |

## Wat dit betekent voor de opdracht

* **Tien titels staan op WACHT-OP-HARDWARE of hoger.** Zij zijn de ontwikkelwachtrij: ze starten op onze
  kern en hebben elk één of twee concrete dingen die nog moeten.
* **Eén titel is DEELS** (Scarlet Dawn) en moet eerst naar onze eigen kern.
* **Eén titel is GEBLOKKEERD** (Men in Black) met een benoemde oorzaak.
* **Twee zijn NIET-BEGONNEN** omdat de eigenaar ze heeft uitgesteld.
* **WORKING is nog voor niemand weggelegd**, en dat is geen formaliteit: de opdracht vraagt om
  dubbelklikken en spelen, en dat is nog bij geen enkele titel aangetoond.

## Wat er nog niet in deze lijst staat

De ongeveer tachtig spellen op de NAS die niet in de dertien van de eigenaar staan. Daarvan is de
bescherming in kaart gebracht (zie de beschermingsinventaris en de familiedossiers); de meesten zijn nog
niet aan de beurt geweest.


## Update 2026-10-14, later die ochtend

**NERF Arcade is de eerste titel die GESPEELD is op onze eigen kern.** Niet "het draait" en niet "het demo't": elf minuten op de native route (`Nerf.x86_64`, geen Wine, geen TeknoParrot-kern, geen BepInEx), met start, vizier en trekker uit onze eigen daemon via de nieuwe RIO-brug (`tools/rio-bridge/`, tabel `patches/NERF/rio-invoer.ini`). Twee rondes gespeeld, punten gescoord, en de ronde is opgenomen in de topscoretabel van het spel zelf. Dezelfde keten liep ook door het opstartpad van de app, waar een muis richtte en vuurde.

**Wat er nog niet is:** de fysieke arcade-lightgun in een mensenhand. Dat blijft de enige reden waarom hier WACHT-OP-HARDWARE staat en niet WORKING, en het is de proef die de eigenaar zelf moet doen.
