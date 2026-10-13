# Halo: waar het spel de trekker en de aim leest (lezing van `game`, geen run)

*RavenGun, 2026-10-13, de loader-sessie. Alles hieronder is GELEZEN uit de code van `game` (Game Rev 57529) met `objdump`;
er is niets gestart. Achter elke bewering staat het adres van de code waar het staat. Wat niet gelezen is, staat onderaan.*

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
