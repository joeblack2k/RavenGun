# Wat onze hulp moet schrijven voor de TREKKER en de AIM — de analyse vóór de code

*RavenGun, 2026-10-13. Analyse, geen code en geen run. Dit is de vraag die overblijft nu Halo de
attract-lus haalt (6,0 minuten in run 12): de **aim** en de **trekker** komen nooit aan, omdat onze hulp
ze nooit schrijft. Hieronder eerst WAT er geschreven moet worden en WAAROM, pas daarna de code.*

## 1. Wat het bordformaat zegt (GEMETEN, `packaging/tp-shm-profiles.json`, profiel `WartranTroopers`)

```
Control (32 bit) op offset 8, little-endian bitveld:
  bit 0  test          bit 6  p1.start        bit 11 p2.start
  bit 1  service       bit 7  p1.TREKKER      bit 12 p2.trigger
  bit 2  coin1         bit 8  p1.reload       ...
  bit 3  coin2         bit 9  p1.grenade
  bit 4  volume_up     bit 10 p1.aux1
  bit 5  volume_down

ASSEN: 1 byte per as, min 0, max 255, invert False
  p1.x = offset 12    p1.y = offset 13
  p2.x = offset 14    p2.y = offset 15
  p3.x = offset 16    p3.y = offset 17
  p4.x = offset 18    p4.y = offset 19
  en de offscreen-regel: de kern leest een as <= 1 of >= 254 als BUITEN beeld, dus binnen het
  scherm blijven de assen op 2..253.
```

De rekenregel staat in `src/ravengun/iobridge/tp_shm.cpp` (regels ~150-170), en die nemen we letterlijk
over: **eerst afronden** (`round(min + f*(max-min))`), **dan pas inverteren** (`invert` is hier False,
dus dat valt weg), en **dan klemmen** op `2..253` als de speler binnen het scherm is; alleen als de
speler echt buiten het scherm mikt blijft 0 of 255 staan.

## 2. Wat onze hulp NU schrijft (GEMETEN, `tools/rt-input-shim/rg_rt_input.c`)

De hulp leest het schakelaarbeeld (offset 8) en zet een **deel** van die bits in de **schakelaartafel van
de game** op `0x180ae80 + 0xaea0`, met `0x50` per schakelaar en `0x2200` per bord:

| tp-shm-bit | signaal | gaat naar de tafel als |
|---|---|---|
| 0 | test | k = 4 (diag/test) |
| 1 | service | k = 3 |
| 2 | coin1 | k = 5 |
| 4 / 5 | volume op/neer | k = 1 / 2 |
| 6 | p1.start | k = 0 van **bord 0** |

**Wat er NIET geschreven wordt:**
* **bit 7 (p1.trigger), bit 8 (p1.reload), bit 9 (p1.grenade), bit 10 (p1.aux1)** -- de geweerknoppen;
* **de acht asbytes (offset 12..19)** -- de aim.

## 3. WAAROM dat de twee gaten zijn, en waar ze waarschijnlijk horen

De tafelindeling van de hulp loopt tot **k = 12**, en alleen 0..8 zijn benoemd (0 start, 1/2 volume,
3 service, 4 test, 5..8 munt). **k = 9, 10, 11 en 12 zijn onbekend** -- en dat is precies het aantal
geweerknoppen dat ontbreekt (trekker, herladen, granaat, aux). De eerste hypothese is dus:

> **de trekker zit op k = 9** (en herladen/granaat/aux op 10/11/12), in dezelfde tafel als de
> schakelaars.

De tweede hypothese is dat de geweerknoppen en de assen in **hetzelfde bordblok op een andere plek**
staan -- het tp-shm-formaat (offset 8 voor de bits, 12..19 voor de assen) is immers een beschrijving van
de bordstructuur die de game zelf leest, en de hulp heeft daar alleen de *schakelaars* uit gehaald.

**En het is met een lezing te beslissen, zonder bench:** de game heeft een **getter op `0x61e890`**
(dat is de functie waar de hulp zijn versiecontrole op doet) en die getter wordt aangeroepen door de
code die de tafel uitleest. Door **rond die getter en zijn aanroepers** in `game`/`game_original` te
kijken, zie je welke offsets de game werkelijk leest: welke `k` waarden (dus 9..12?) en of er een
**assenblok** naast ligt. Dat is ontleden, geen run -- en het is de goedkoopste manier om te voorkomen
dat we in het wilde weg in de tafel schrijven.

## 4. De twee uitbreidingen, in de vorm die de hulp al heeft

De hulp heeft één functie `entry(board, k)` en een `MAP[]` met per signaal `{bit, board, k, puls-ms}`.
De uitbreiding is dus **in de bestaande vorm**, niet als nieuw mechanisme:

1. **De trekker en de geweerknoppen**: een `MAP`-regel per knop, zodra de lezing zegt welke `k` het is
   (bijvoorbeeld `{7, 0, 9, 0}` voor trekker op bord 0, k=9 -- puls 0, want een trekker is een knop en
   geen puls). De pulsduur van 0 is belangrijk: de trekker moet **ingedrukt blijven** zolang de muis
   ingedrukt is, anders mist de game hem (dat is dezelfde les als bij de START-indruk).
2. **De assen**: een tweede tabel in de hulp, net als de assenlijst in het profiel:
   `{speler, offset-in-het-bordblok, byte}` -- en per as de rekenregel uit §1 (afronden, niet
   inverteren, klemmen op 2..253). De waarde komt uit de bestaande muis-/aimbron van de daemon, dus de
   hulp hoeft alleen *door te geven*, niet te rekenen aan de bron.

**Waarom dit per-game gegevens moeten blijven** (en niet in het gereedschap): welke `k` bij welke knop
hoort en waar het assenblok ligt, is per game/build verschillend. De hulp hoort **generiek** te zijn:
een tabel met `{bit of as, board, k of offset, soort}` in een gegevensbestand naast de game, precies
zoals `hasp-haken.ini` dat voor de HASP-haken doet.

## 5. Wat ik daarvoor nodig heb, en wat ik NIET doe

* **Wel**: het lezen van de getter op `0x61e890` en zijn aanroepers in de spelbinary, om te bepalen
  welke `k` en welke offsets de game leest. Dat is ontleden zonder bench, en dat is mijn analyse.
* **Niet zonder overleg**: de hulp zelf aanpassen. Die is van het project (en Claude gaat over de lader
  en de kern); ik lever de analyse en de exacte bytes, en we spreken af wie het schrijft.
* **En geen run** tot de afspraak er is: de bench is nu van de Terminator-werker.
