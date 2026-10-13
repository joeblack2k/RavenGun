# Welk kanaal gebruikt de AIM, en hoe wordt de kalibratie overgeslagen? (GEMETEN in de documentatie)

*RavenGun, 2026-10-13. Bench-vrij uitgezocht op verzoek van de coördinator, na run 10. Alles is
**lezen** geweest; er is niets gestart. Per bevinding staat GEMETEN of AFGELEID met de bron erbij.*

## 1. HET IS AL EENS GEDAAN, EN HET STAAT OP SCHRIFT (GEMETEN)

`docs/decisions.md`, regel 442 -- de eerdere native Halo-sessie met de gerepareerde FMOD-stub:

> "de testknop slaat de kalibratie van gun 1 over **als de indruk lang genoeg is (300 ms; 30 ms was te
> kort)**, de kalibratie van gun 2 gaat weg met **START** (testknop op bord 0 t/m 3 hielp daar niet), en
> daarna komt de game in zijn **attract-lus**: Microsoft-logo, high scores, 'JOIN NOW / Press START to
> Begin / Cost to play: $1.00'. **Munt werkt**: elke indruk van 14 ms op de munt-schakelaar van bord 0
> geeft $0,25 ('Money In: $2.00' na acht indrukken). Een keer is de game ook echt gestart: 'CHOOSE A GAME
> MODE', daarna 'MISSION 1/ESCAPE' en de introfilm."

Dus de route naar de attract-lus is bekend en hij is **met onze eigen hulp** gelopen:
1. **TEST, lang genoeg (300 ms)** -> kalibratie van **gun 1** weg;
2. **START** -> kalibratie van **gun 2** weg;
3. dan de **attract-lus**; munt 14 ms per $0,25; en een keer is het spel echt gestart.

En ook: *"Aim en trekker zijn niet getest: ze horen bij een spel dat nog niet start."*

## 2. WAAROM HET BIJ ONS NIET LUKTE -- EN DAT IS WAARSCHIJNLIJK MIJN EIGEN INGREEP (AFGELEID)

De werkende configuratie perste TEST op **alle vier de borden**: de tabel van de hulp heeft vier regels
voor bit 0 (`{0,0,4},{0,1,4},{0,2,4},{0,3,4}`). In run 7, 8 en 10 gebruikte ik
**`RG_RT_PROBE=0,0,4`** -- en dat herschrijft die vier regels naar **alleen bord 0**. De enige run
waarin de vier-borden-indruk wel werd gegeven (run 4) crashte op mijn afkappende schrijfactie.

**De hypothese is dus: de TEST-indruk moet op alle vier de borden, en mijn probe heeft dat juist
weggehaald.** Dat is niet bewezen -- de vier-borden-variant is nooit schoon gemeten -- maar het is de
goedkoopste verklaring, en het is een ingreep van mij, niet van de game.

## 3. DE AIM: NIET OPTISCH MAAR VIA HET BORD/tp-shm (GEMETEN)

`packaging/games-status.md`, regel 14 (de **TeknoParrot-route**, nu `tp-shm` in plaats van evdev):

> "Halo: Fireteam Raven (ElfLdr2 64-bit, nu route tp-shm in plaats van evdev): gelijk aan het oude pad.
> Gun-kalibratiescherm speler 1 en 2 (**TEST slaat over**), attract met 'Money In', munt, start
> (modekeuze **met vizier**), **aim (vizier volgt het raster)** en **trekker (kiest een modus)**."

**Daar werkt de aim dus, en de invoerroute is `tp-shm`** -- het gedeelde geheugen van de IO-brug met het
`WartranTroopers`-profiel. En `docs/architecture.md`, regel 106, zegt wat dat profiel schrijft:

> "tp-shm: `packaging/tp-shm-profiles.json` beschrijft per EmulationProfile de indeling: **bits**
> (signaal, offset, bitnummer in het little-endian bitveld) en **assen** (signaal, offset, 1 of 2 bytes,
> min/max, inversie, byte-volgorde, ruststand)."

**Onze hulp schrijft alleen BITS** (de schakelaars, in de schakelaartafel van de game). De **assen** --
waar de aim door loopt -- schrijft hij niet. Dat is het gat: de aim hoort bij het analoge deel van
dezelfde bordstructuur, en `packaging/tp-shm-profiles.json` (profiel `WartranTroopers`) zegt precies op
welke offset, hoeveel bytes, met welk bereik en welke inversie.

`packaging/games-status.md`, regel 82, vult dat aan voor de JVS-kant: het standaard Sega-bord heeft
"8 analoge kanalen: P1 X, P1 Y, P2 X, P2 Y, ...". De aim is dus een **analoog kanaal**, geen camera.

## 4. EN DAARMEE VERVALT MIJN EIGEN OPTISCHE AFLEIDING (correctie op run 10)

In run 10 leidde ik af dat de aim **optisch** zou zijn, op grond van het zoeken naar
`brightness difference` in de webcam-stap. **Die afleiding is nu zwakker, en ik trek hem in als
verklaring voor de aim:** de documentatie laat zien dat de aim via het bord (`tp-shm`, analoge kanalen)
loopt en dat de TeknoParrot-route daarmee werkt -- met een camera die daar ook "brightness difference 0"
meldde. De webcamregel hoort dus bij de **webcamstap van de game** (een eigen functie, en hij faalde op
10 oktober ook terwijl de game toen wél in de attract-lus kwam), niet bij de aim. Wat uit run 10 blijft
staan: de game houdt op het kalibratiescherm **geen enkel invoerapparaat open** (0 `/dev/input`-fds,
geen SDL) -- dus een apparaat bouwen dat hij kan openen, is niet de route; **de bordkanalen zijn de
route**.)

## 5. WAT DAT BETEKENT VOOR DE VOLGENDE RUN (geen nieuw apparaat, geen camera)

1. **TEST op ALLE VIER de borden, met een puls van ~300 ms** (dus **zonder** `RG_RT_PROBE`, met de
   standaardtabel van de hulp, of met `RG_RT_PROBE` op alle vier). Een paar keer herhalen, foto per
   poging. Verwachting vooraf: de kalibratie van **gun 1** verdwijnt (dat is eerder gemeten met 300 ms;
   30 ms was te kort).
2. **Dan START** voor de kalibratie van **gun 2** (testknop hielp daar niet, START wel).
3. **Dan de attract-lus** controleren (`Money In`, `JOIN NOW`, `Cost to play`) en **munt** (14 ms per
   $0,25 -- korter dan 14 ms niet, langer telt per beeld meerdere keren).
4. **En de aim pas daarna**: die vraagt om de **assen** van het `WartranTroopers`-profiel. Dat is nieuw
   werk in de hulp (niet in de game): dezelfde bordstructuur, andere offsets -- en
   `packaging/tp-shm-profiles.json` geeft ze precies. Dat is een gerichte uitbreiding van één functie,
   in plaats van een apparaat of een camera.

**Let op bij het meten (uit `packaging/games-status.md`, regel 8):** een verbonden Move zet bij elk
rapport zijn eigen knoppen terug, dus een `down` van `ravengunctl` duurt soms maar enkele
milliseconden -- herhaal de opdracht of gebruik `tap`.

## 5b. DE GEGEVENS VAN HET WartranTroopers-PROFIEL, LETTERLIJK (GEMETEN)

Uit `packaging/tp-shm-profiles.json`, profiel `WartranTroopers` (64 bytes; gebruikt door **Halo**, Tomb
Raider en Rabbids):

```
Control (32 bit) op offset 8, little-endian bitveld:
  bit 0  test          bit 7  p1.trigger      bit 12 p2.trigger      bit 17 p3.trigger
  bit 1  service       bit 8  p1.reload       bit 13 p2.reload       bit 18 p3.reload
  bit 2  coin1         bit 9  p1.grenade      bit 14 p2.grenade      bit 19 p3.grenade
  bit 3  coin2         bit 10 p1.aux1         bit 15 p2.aux1         bit 20 p3.aux1
  bit 4  volume_up     bit 11 p2.start        bit 16 p3.start        bit 21 p4.start
  bit 5  volume_down                                                      ... p4 t/m bit 25
  bit 6  p1.start

AIM: 1 byte per as op offset 12..19 -> P1X(12) P1Y(13) P2X(14) P2Y(15) P3X(16) P3Y(17) P4X(18) P4Y(19)
     links/boven = 0 (InvertedMouseAxis)
     offscreen-regel: de kern leest een as <= 1 of >= 254 als BUITEN beeld;
     binnen het scherm blijven de assen daarom op 2..253 (offscreen_margin)
```

De code die het schrijft: `src/ravengun/iobridge/tp_shm.cpp` (regel ~121: `ta.offset` standaard 12,
`ta.bytes` standaard 1; regel ~153-165: eerst afronden, dan inverteren, en de as tussen
`offscreen_margin` en `255 - offscreen_margin` klemmen).

**TWEE GATEN IN ONZE HULP, niet een.** Onze hulp schrijft alleen **schakelaars in de tafel van de game**
(k 0 = start, 1/2 = volume, 3 = service, 4 = diag/test, 5..8 = munt). Daarmee ontbreken:
1. **de AIM**: de acht asbytes (offset 12..19) komen in de tp-shm-indeling binnen, maar onze hulp zet ze
   nergens in de game;
2. **de TREKKER en de andere geweerknoppen**: `p1.trigger` (bit 7), `p1.reload` (bit 8) en `p1.grenade`
   (bit 9) staan wel in het profiel en **niet** in de tabel van onze hulp. Die tafel loopt tot k=12,
   waarvan 0..8 bekend zijn; de trekker zit daar niet bij. De trekker hoort dus bij dezelfde
   bordstructuur maar op een andere plek dan de schakelaars -- en dat is de tweede vraag voor stap (d).

Dat verklaart ook waarom de trekker "niet getest" is: hij is nooit aangesloten geweest. En het
relativeert mijn run 8: daar stuurde ik de **trekker van het virtuele lichtgeweer** (een evdev-knop
`BTN_LEFT`), terwijl de game zijn trekker uit **zijn eigen bordstructuur** leest. Twee verschillende
werelden, en de mijne was de verkeerde.

## 6. De lat

Onveranderd: **gehaald = 5 minuten attract- of spelmodus.** Wat deze documentatie laat zien is dat de
attract-lus **native al eens gehaald is** (decisions.md regel 442) en dat de route daarheen TEST (300 ms,
alle borden) en START is. Als die route lukt, is de volgende vraag de aim via de assen -- en dat is de
vraag die dit project moet beantwoorden.
