# Halo: waar het spel de trekker en de aim leest (lezing van `game`, geen run)

*RavenGun, 2026-10-13, de loader-sessie. Alles hieronder is GELEZEN uit de code van `game` (Game Rev 57529) met `objdump`;
er is niets gestart. Achter elke bewering staat het adres van de code waar het staat. Wat niet gelezen is, staat onderaan.*

## 00. STAND 14:40 — de geweren lopen via het IO-BORD (klasse 0x4); hier staat de slotkaart

Dit vervangt de richting van de secties hieronder. Alles is GELEZEN uit `game`; niets is gedraaid.

**De invoerklassen.** Er is een vaste tabel van acht apparaatklassen op `0x16f8380` (0x80 per klasse: id, drie functies,
gegevens). Klasse `0x8` is de camerageweer-laag (functies `0x623f30`, `0x624430`, `0x624110`), klasse `0x4` is het IO-bord
(`0x61ff70`, `0x6200c0`, `0x6208e0`; gegevens `0x1815d20` = `0x180ae80 + 0xaea0`, de tafel waar de hulp al in schrijft).
De lus per beeld (`0x61cb20`) roept alleen klassen aan waarvan het id in het masker `[0x181d8a0]` staat. Dat masker komt uit
`[0x1766f40]` (gezet op `0x4ac5e4`, doorgegeven op `0x4acbe8`, opgeslagen op `0x61c972`); de enige zetter wordt een keer
aangeroepen (`0x436485`) met een waarde uit `0x800`, `0x4`, `0x100` en `0x20000`, en later wordt alleen `0x100` aan- of
uitgezet (`0x61cd10`, `0x61cd20`). **Bit `0x8` komt er dus nooit in**: de voeding en bijwerking van klasse 8 lopen niet, en
de enige aanroepers van de richtpunt-lezer `0xdd6c60` en de knoplezer `0xdd5e50` zitten in die voeding.

**Wat WEL van de camerageweer-laag gebruikt wordt:** de eigen geweermodule van het spel (`0x415000..0x417f00`) roept die laag
rechtstreeks aan voor aanwezigheid (`0x417880 -> 0xdd4b10`: het apparaatblok) en voor de kalibratie. Die laag is dus niet dood;
alleen het richtpunt en de knoppen komen er in deze build niet uit.

**De bindingen.** Het spel legt met `0x4acdb0(invoer-id, actie-id)` vast welke invoer welke actie voedt; alle 187 aanroepen
staan in `bindingen-ruw.txt` (in het geheugen: paren van 8 bytes vanaf `0x176f850`, aantal op `0x1771850`). Een invoer-id
`0x239..0x298` is een bordvak: bord = (id-0x239)>>5, vak = (id-0x239)&31, adres = `0x1815d20 + bord*0x2200 + vak*0x50`.

| Actie | Betekenis | Bord 0, vak | Adres van het vak |
|---|---|---|---|
| `0x206` | speler 1 richten X (dezelfde actie als het camerageweer-id `0x319`) | 17 | `0x1816270` |
| `0x207` | speler 1 richten Y (`0x31a`) | 18 | `0x18162c0` |
| `0x208`, `0x20a`, `0x209` | de drie geweerknoppen van speler 1 | 21, 22, 23 | `0x18163b0`, `0x1816400`, `0x1816450` |
| `0x212`, `0x213` | speler 2 richten X, Y | 19, 20 | `0x1816310`, `0x1816360` |
| `0x214`, `0x216`, `0x215` | de drie geweerknoppen van speler 2 | 25, 26, 27 | `0x18164f0`, `0x1816540`, `0x1816590` |

Bord 1 heeft dezelfde indeling voor speler 3 en 4. De kastschakelaars zijn vak 0 tot 6 (start, volume, service, test, munt:
zoals de hulp ze al kent) en 8 tot 13.

**Een vak is een schakelaar of een analoog kanaal.** Het soortveld staat op vak+0: 0 = analoog, 1 of 2 = schakelaar (de twee
getters `0x61ee70` en `0x61e890` rekenen hetzelfde adres uit en kijken alleen naar dat veld). Op vak+8 begint de toestand. Voor
een analoog kanaal (zetter `0x61cdc0`): +0x4 = de waarde, +0xc = de ruwe waarde, +0x14 en +0x18 = minimum en maximum, +0x0 =
vlaggen (bit 0 keert om). Gerekend vanaf het vak dus: **waarde op vak+0xc, ruw op vak+0x14.**

**Niet gelezen:** (1) de eenheid van het richtpunt en wat de kalibratie van de vaste geweren (`mountedGun%iCalibration`) ermee
doet; (2) welke van de drie knopacties de trekker is (aanname: `0x208`, vak 21); (3) welke tak van de bindingsfunctie bij ons
kasttype hoort (de lijst bevat alle takken door elkaar; vak 19 en 20 gaan in een andere tak naar speler 4).

## 000. AANVULLING 15:10 — wie de tafel vult, en wat de bordbibliotheek over de ingangen zegt

Gelezen uit `game`, niets gedraaid. Dit beantwoordt "de tafel is leeg: wie hoort hem te vullen?".

- **De voeding `0x6208e0` schrijft het soortveld NIET.** Zij leest het (`mov eax,[rbp-8]` op `0x620b5e`) en kiest: soort 0 ->
  de waarde als kommagetal opslaan met `0x61cdc0` (`0x620b69..0x620b81`: `cvtsi2ss`, dus het GEHELE getal van het bord als
  float); anders de schakelaarafhandeling (`0x620a30`). Zij loopt per bord alle 32 vakken af en vraagt de bordbibliotheek per
  vak om de stand (`0x62e800(bord, vak, &waarde)` op `0x620b55`). Zonder bord (`[0x16f8a58] == 0`) keert zij meteen terug
  (`0x62091d`).
- **Het soortveld wordt geschreven door `0x621150`** (aangeroepen op `0x41d140` en `0x4adb71`): per gevonden bord en per vak
  0..31 roept die `0x62ee20(bord, vak, 1, &vak, 0)` aan, en die schrijft het soort (`mov [rcx],eax` op `0x62ee70`) uit de
  ingangentabel van het bordobject (`byte [bord + vak*8 + 0x2e10]`). De bordindeling komt dus van het AANGESLOTEN BORD, via de
  bibliotheek. Geen bord: niets geschreven, een lege tafel. Er is geen vast bordbeeld in het spel.
- **Wat een ingang KAN zijn staat wel vast**, in een tabel van 78 ingangen in `0x62eed0` (een bitmasker van toegestane
  standen per ingang; de functie geeft 0 als `(1 << stand) & masker` niet nul is):

  | Ingang | Masker | Toegestane standen |
  |---|---|---|
  | 0..3, 6..10, 21..25 | 14 | 1, 2, 3 (niet analoog) |
  | **4, 5, 11..20** | 15 | **0 (analoog)**, 1, 2, 3 |
  | 26..31 | 30 | 1, 2, 3, 4 |
  | 32..41 | 8 | 3 |
  | 42..77 | 16 | 4 |

  **Vak 17, 18, 19 en 20 -- volgens de bindingen het richten van speler 1 en 2 -- horen bij de ingangen die analoog kunnen zijn;
  vak 21 tot 23 (de geweerknoppen) niet.** Dat is een tweede, onafhankelijke aanwijzing voor de slotkaart van sectie 00.
- **De inrichting van het bord door het spel** (`0x41cd40..0x41d140`): 24 aanroepen van `0x62eca0(bord, ingang, n)` (n = 4
  voor ingang 0..3, 1 voor 4 en 5, 2 voor 6; AFGELEID: een filtertijd, geen stand), paren met `0x635070` ((11,10) en (13,12),
  of (8,7) en (10,9), naar kasttype), en **alleen voor kasttype 4 en 9** (`0x41cf04..0x41cf1b`) twee groepen met `0x635710`:
  groep 0 = ingang 74 en 75, groep 1 = ingang 76 en 77 (ingangen die alleen stand 4 toestaan). De voeding leest groepen met
  `0x634d60` in de blokken op tafel+`0xb8a0` (de id's `0x299..0x318`). **Niet gelezen:** wat die groepen zijn en hoe het spel
  ze gebruikt; geen enkele binding noemt een id uit dat bereik.
- **Pas op bij "vakken die het spel pollt":** de getallen 0x254..0x258, 0x25a, 0x25f, 0x260, 0x288 en 0x28c zijn in de
  bindingslijst ACTIES (het tweede argument van `0x4acdb0`), geen invoer-id's. Als bordvak gelezen worden dat bord 0 vak
  27..31, bord 1 vak 1/6/7 en bord 2 vak 15/19, maar zo zijn ze niet bedoeld: 0x256 is de actie die aan bord 0 vak 0 (START)
  hangt. De getters krijgen de invoer-id's uit de bindingstabel in een register (`0x4ad7fc..0x4ad80c`), niet als vast getal.

## 0. CORRECTIE (13:55, na de eerste run en een tweede lezing): deze laag is voor ONS kasttype waarschijnlijk niet de bron

Wat hieronder staat klopt als lezing, maar de conclusie "schrijf het paar en de knoppen in deze laag" is te vroeg getrokken.

- **Het richtpunt op `0x1f392a0` is een UITVOER van het spel zelf.** De bijwerklus per speler (rond `0x dd58b5`) roept
  `0xdd8350(speler, ..)` aan; levert die niets, dan zet het spel `byte [0x1aa37c0 + speler] = 1` (`0xdd5928`) en schrijft via
  `0xddf850 -> 0xdd5cb0` het paar op **-1,-1**; levert die wel iets, dan `= 0` (`0xdd59c2`) en het berekende punt. Wie het paar
  schrijft terwijl die lus loopt, wordt elke ronde overschreven. De poortbyte wordt bij het opstarten op 0 gezet (`memset` op
  `0xdd525f`); 0 laat de lezer door.
- **Die lus loopt alleen bij geweertype 7 tot 11** (`[0x1e94740]`, controle op `0xdd58a7`), en `0xdd8350` rekent het punt uit
  een lijst beeldpunten per speler (`[0x1f392c0 + 4*speler]` = aantal, `[0x1e94760 + 8*speler]` = wijzer naar records van
  `0x48`). Dit is dus de laag van een geweer met een CAMERA.
- **In onze runs staat het type niet en is het aantal spelers 0** (gemeten door de Halo-werker). Dan loopt de bijwerklus niet en
  loopt de lezer op `0x624120` nul keer: precies "de waarden blijven staan en er verandert niets op het scherm".
- **Afleiding, niet gelezen:** ons kasttype (4) is de variant met VASTE geweren (het kalibratiescherm zegt "pull gun all the way
  back and towards the left"; de binary heeft `mountedGun%iCalibration`), en die krijgt zijn richtpunt langs een andere weg.

**Eerst te lezen, voor er weer een run met `zet`-regels op deze laag komt:** welke code het richtpunt levert bij kasttype 4.
De kandidaat-tabel in `rt-invoer.kandidaat.ini` richt zich op de cameralaag en is daarmee voor dit kasttype waarschijnlijk de
verkeerde plek. De drie regelsoorten in de hulp blijven bruikbaar; de adressen moeten opnieuw.

## 1. De schakelaartafel is niet waar de geweren zitten

De getter op `0x61e890` is een algemene invoerlaag. Hij geeft voor een nummer een knopbeschrijving terug:

- `0x239..0x298`: bord = (n-0x239)>>5, vak = (n-0x239)&31 -> `0x180ae80 + 0xaea0 + bord*0x2200 + vak*0x50`. Hij geeft `vak+8` terug
  als het soortveld (`[vak+0]`) 1 of 2 is. Een bord heeft dus **32 vakken**, niet 13.
- De zustergetter op `0x61ee70` geeft voor hetzelfde vak `vak+8` terug als het soortveld **0** is: een vak is een knop (soort 1/2)
  of een analoog kanaal (soort 0).

De hypothese "de trekker is k = 9..12 in dezelfde tafel" is hiermee **niet bevestigd**: het spel vraagt de geweren niet via
deze getters op.

## 2. De geweren zitten in een eigen laag (code `0xdc8000..0xdda000`)

De functie op `0x624120` vult per beeld de invoer van elke speler. Per speler `p` (0 tot `[0x1f392d0]`, het aantal spelers):

1. `0xdd4b10(p)` -> `0xdd09b0`: zoekt in de **apparaattabel** op `0x1a7cb20` (32 blokken van `0x1348` bytes) het eerste blok
   met `[blok+0x3c] == p` en geeft `[blok+0]` terug. Is dat 0, dan krijgt de speler aim -1,-1 en geen knoppen (`0x624360`).
2. `0xdd6c60(p, &x, &y)`: is `byte [0x1aa37c0 + p] > 3`, dan -1,-1. Anders leest hij **twee kommagetallen op
   `0x1f392a0 + 8*p`** (x op +0, y op +4) en haalt ze door `0xdccc80` (de kalibratie van het geweer).
3. Drie keer `0x624040(.., p, knop, ..)` met knop **0, 1 en 3** -> `0xdd5e50(p, knop)` -> `0xdcf620(p, knop)`: zoekt het blok
   met `[blok+0] != 0` en `[blok+0x3c] == p`, neemt de mutex op `blok+0xf0` en leest **`[blok + 0xc8 + 4*knop]`**.

## 3. Een knop is een overgangsteller

`0x624040` trekt de vorige stand van de nieuwe af met `0x61cd70(vorige, nieuwe, 4)`: het verschil modulo 2^4. Uit dat verschil en
uit bit 0 van de vorige stand rekent hij het aantal indrukken en loslatingen in dit beeld uit. De stand is dus een **teller van
overgangen, 4 bits breed, waarvan bit 0 zegt of de knop nu ingedrukt is**. Een niveau schrijven (0/1) is dus fout: van 1 terug
naar 0 leest het spel als vijftien overgangen.

## 4. Wat de hulp daarvoor kan (gebouwd en getest, 2026-10-13)

Drie nieuwe regelsoorten in de tabel (`docs/per-game-patches.md`):

- `zet adres= breedte= waarde=`: een vaste waarde die vastgehouden wordt (aanwezig-vlag, spelernummer);
- `teller bit= adres= breedte= modulo=`: een bit uit het bordbeeld als overgangsteller;
- `as ... soort=f32 schaal= plus= buiten=`: een as als kommagetal met een eigen bereik, en een vaste waarde buiten beeld.

De kandidaat-tabel staat in `rt-invoer.kandidaat.ini`. Hij wordt **niet** vanzelf gebruikt.

## 5. Wat NIET gelezen is (en wat een run dus moet uitwijzen)

1. **Wat een blok dat wij op "aanwezig" zetten verder aanraakt.** De tabel wordt op tientallen plekken afgelopen. Er zit geen
   echt apparaat achter ons blok; code die naar het geweer wil schrijven kan daarop stuklopen. Dit is het risico van de proef.
2. **De eenheid van de aim** (0..1, -1..1 of pixels) en wat de kalibratie `0xdccc80` ermee doet als de kalibratie met START is
   overgeslagen.
3. **Welke knop de trekker is** (0, 1 of 3).
4. Of `[0x1f392d0]` (aantal spelers; `0xdd4bc0` zet het op 4 of 2 naar het kasttype) in onze runs groter dan 0 is.

## 6. De proef, met de verwachting vooraf (muismodus, geen Move nodig)

De eigenaar: gebruik de muismodus. De daemon stuurt het virtuele geweer aan (`ravengunctl arm`, `ravengunctl test-pattern point
X Y`, `ravengunctl output-button trigger tap`); dat komt in het bordbeeld en de hulp zet het in het spel.

| Stap | Wat | Verwachting |
|---|---|---|
| 0 | Van buiten het proces lezen: `0x1f392d0`, `0x1a7cb20` (+0, +0x3c), `0x1f392a0` | aantal spelers 2 of 4; blok 0 leeg; aim 0 of -1 |
| 1 | De kandidaat-tabel, naar de attract-lus, dan START (moduskeuze "met vizier") | een vizier dat er zonder tabel niet is; of een crash (dan is punt 5.1 het antwoord) |
| 2 | `test-pattern point 0.3 0.3`, dan `0.7 0.7` | het vizier verplaatst; uit de plek volgt de eenheid (5.2) |
| 3 | `output-button trigger tap` | een modus wordt gekozen; zo niet, knop 1 en 3 proberen (5.3) |

Staat er geen vizier en is er geen crash, dan eerst stap 0 herhalen met de tabel actief: staan de waarden er, dan leest het spel
ze niet (en klopt deze lezing niet); staan ze er niet, dan schrijft de hulp niet (tabel afgekeurd: zie stderr van de run).
