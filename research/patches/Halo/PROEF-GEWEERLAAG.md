# Proef geweerlaag Halo: verwachtingstabel per stap (vóór de run opgeschreven)

*RavenGun, 2026-10-13. Dit is de proef die klaarstaat zodra de bench vrij is. De lezing waarop hij
rust staat in `LEZING-GEWEERLAAG.md`; de kandidaat-tabel in `rt-invoer.kandidaat.ini`. **Alles hieronder
is een voorspelling, geen aanname** -- een voorspelling kan weerlegd worden, en dat is de bedoeling.
Niets van dit alles is al gemeten.*

## 0. Het gereedschap: opnieuw gebouwd, en zo gecontroleerd

```
artefact : ~/projects/ravengun/build/librg_rt_input.so
           romein 42.160 bytes, 2026-10-13 13:17:56
           sha256 8199ef9b9aa9518f80047ffbc87afa26cf27970a275a76eea3fe727ac9bb04f3
bron     : tools/rt-input-shim/rg_rt_input.c, 33.551 bytes, 13:10:11, sha256 6403e1adbb359c2b
```

**En de eerste controle die ik deed was fout** -- ik zocht in het artefact naar `soort=f32` en
`RG_MAX_AS`, maar dat is een **samengestelde sleutel** en een **macro**: die staan nooit als tekst in een
binary. De juiste controle is op de **echte teksten in .rodata**, en die zijn er alle drie:
`regel %d: te veel assen (hooguit %d)`, `regel %d: te veel tellers (hooguit %d)`,
`regel %d (zet): 'waarde' ontbreekt of is geen getal ...`, plus de onbekend-woord-melding die
`..., as, teller of zet` opsomt, en de losse sleutelwoorden `soort`, `f32`, `schaal`, `buiten`, `modulo`,
`breedte`, `waarde`, `adres`. **Dus: de drie regelsoorten zitten in het gebouwde bestand**, en het
artefact is nieuwer dan de bron. (Dit is dezelfde les als de `grep` van run 15: een controle moet iets
meten dat kan falen -- een macro zoeken is een controle die per definitie niets vindt.)

**De oproep die ik ga gebruiken** (zo staat het ook in de kop van de kandidaat-tabel):
```
--bind-ro <patches/Halo/rt-invoer.kandidaat.ini> /rg/tabel/rt-invoer.ini \
--env RG_RT_TABLE=/rg/tabel/rt-invoer.ini
```
De tabel heet met opzet **geen** `rt-invoer.ini` in de repo, zodat hij niet vanzelf wordt opgepakt; in de
sandbox krijgt hij die naam wel, en `RG_RT_TABLE` wijst erheen (uit de bron, regel 424: `getenv`, anders
`rt-invoer.ini` naast de bibliotheek, anders de ingebouwde standaard).

**En de camera is inmiddels WEL mogelijk -- via de weg die de laderbouwer gebouwd heeft.** Sinds commit
`5c3ba99` heeft `play-native.sh` **`--dev-bind-try /dev/video0 /dev/video0`** (mijn meting is dus
overgenomen), staat er een **camera-regel in elk runlogboek**, en is er per game een `[Camera]`-blok met
`Use` en `Device`. De eigenlijke weg is niet mijn `--bind-rw` maar de **V4L2-shim in het spel** (de daemon
blijft de enige eigenaar en publiceert elke tweede frame in gedeeld geheugen; `Use = presence` geeft het
spel een virtueel apparaat met een synthetische frame). Voor deze proef laat ik de camera **buiten
beschouwing** -- hij hoort bij de webcam-/QR-vraag, niet bij de geweerlaag -- maar hij kan er in een
volgende run bij, en dan is de verwachting: de webcamregel in het logboek en géén
`Could not ID device` meer.

## 1. De verwachtingstabel

| # | wat ik stuur | wat ik verwacht te zien | hoe een NEGATIEF eruitziet |
|---|---|---|---|
| 1 | START herhalen (puls ~400-500 ms) | `FileTest Reported` na 1 puls; daarna de **nieuwe** `sound bank: attract`-regel binnen ~60 s (run 11: 25 s, run 13: 30-40 s, run 16: 38 s) | geen nieuwe attract-regel binnen 60 s -> **stoppen en melden**; dan is de toestand zelf de uitkomst |
| 2 | `zet adres=0x1a7cb20 breedte=4 waarde=1` + `zet adres=0x1a7cb5c breedte=4 waarde=0` (blok aanwezig, spelernummer 0) | **P1:** het spel leest de blokken uit de tabel (de waarde blijft staan; hij wordt niet teruggezet); **P2:** de aim is niet langer `-1,-1` -- dat is de waarde als het blok ontbreekt | de waarde staat na 5 s weer op 0 (dan **schrijft het spel terug** en is de tabel niet de juiste ingang); of alles blijft onveranderd en de aim blijft `-1,-1` -> dan is het blokadres of de structuur fout |
| 3 | aim via de muismodus: `ravengunctl arm`, `ravengunctl test-pattern point X Y` (een paar punten), as als `soort=f32` | **P3:** het richtpunt in het spel **beweegt mee** met het patroon; eerste poging met de eenheid 0..1 (schaal 1/255) | het richtpunt beweegt niet (eenheid fout), of het springt naar de rand (bereik -1..1 of pixels), of het spel negeert het (dan doet de kalibratie `0xdccc80` meer dan schalen) |
| 4 | trekker: `ravengunctl output-button trigger tap`, drie kandidaten **een voor een** (knop 0, 1, 3), foto per poging | **P4:** precies **een** van de drie doet iets zichtbaars (een schot, of een menu dat reageert). Mijn **aanname** is knop 0 = trekker; 1 en 3 zijn dan herladen/granaat of ongebruikt | geen van de drie doet iets -> het overgangstellermodel of de `modulo=16` is fout (dan is de knop geen teller maar een bit, of een andere breedte) |
| 5 | niets (nulmeting na afloop) | het spel laat het blok met rust en blijft in de attract-lus | het spel crasht of verlaat de attract-lus zodra het blok "aanwezig" is -> dan is "aanwezig" meer dan een vlag |

## 2. De onbekenden, als voorspellingen

1. **De eenheid van de aim.** *Voorspelling:* 0..1 over het scherm (dus `schaal=1/255` werkt); de
   tweede kandidaat is -1..1, de derde pixels. *Weerlegd als:* het punt niet meebeweegt of buiten beeld
   springt.
2. **Wat de kalibratie op `0xdccc80` doet nu START de kalibratie oversloeg.** *Voorspelling:* hij schaalt
   en verschuift de twee kommagetallen naar schermcoordinaten en **blijft dat doen** -- de kalibratie is
   een berekening, geen eenmalige toestandsvlag. *Weerlegd als:* de aim pas meebeweegt nadat de
   kalibratie is doorlopen, of helemaal niet.
3. **Het aantal spelers `[0x1f392d0]`.** *Voorspelling:* het is **groter dan 0** (de kast heeft zijn
   spelersaantal), dus het spel negeert ons blok niet op grond daarvan. *Weerlegd als:* het op 0 staat en
   het spel ons blok daardoor niet leest -- dan moet dat veld ook gezet worden.
4. **Schrijft het spel terug in ons blok?** *Voorspelling:* **nee** voor `+0` en `+0x3c` (dat zijn
   statische kenmerken), **ja** voor de tellers (een overgangsteller wordt door de lezer bijgewerkt).
   *Weerlegd als:* `+0` na een seconde weer 0 is.
5. **Welke knop de trekker is.** *Voorspelling:* knop 0, en dat is de enige **aanname** in de tabel --
   daarom worden alle drie de kandidaten een voor een geprobeerd, met een foto per poging.

## 3. Wat deze proef **niet** doet

* Geen camera (zie §0): de webcam-auto-identificatie en de QR-meldingen blijven buiten deze proef.
* Geen fabrieksinstelling: het DIAG-menu (`TEST`, bit 0 -> k=4) is een **aparte** proef, en het
  installatie-item wordt niet uitgevoerd zonder akkoord.
* Geen service-menu: dat is in de attract-lus al gemeten als "aankomen, niets doen" (run 16).
* En geen uitspraak over spelers 2 tot 4: de kandidaat-tabel doet met opzet **alleen speler 1**.
