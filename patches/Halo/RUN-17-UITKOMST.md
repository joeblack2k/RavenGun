# RUN 17 — de geweerlaagproef: het blok klopt, de keten breekt één schakel eerder

*RavenGun, 2026-10-13. Uitgevoerd **13:32:03 - 13:35:4x** (200 s, beeld op de tv, exitcode 0). Niets
draaide ernaast, de Move was niet aangesloten, opgeruimd met alleen mijn eigen pids, de daemon ontwapend,
de kern op `f5c7532d6e638485`. Geen commit. **Wat ik las staat hier; ik heb niets herhaald tot het
"werkte".***

## 0. Het gereedschap, direct vóór de run gecontroleerd

```
artefact : build/librg_rt_input.so  sha256 8199ef9b9aa9518f80047ffbc87afa26cf27970a275a76eea3fe727ac9bb04f3
           (precies de hash uit het ijkpunt; bron van 13:10:11)
tabel    : rt-invoer.kandidaat.ini  sha256 82ac96cd4436c5b41b088d406c5b89df8826327328b5a6fdfda527d6243d4bfb
           en de hulp MELDT hem als geldig:
           "tabel /rg/tabel/rt-invoer.ini: 13 schakelaars, 0 bits, 2 assen, 3 tellers, 2 vaste waarden"
```
Dat is de belangrijkste voorcontrole: **dertien schakelaars** betekent dat de ingebouwde kastschakelaars
**behouden** zijn (een tabel zonder `schakelaar`-regels vult aan) en dat mijn zes regels erbij kwamen.
De tabel is dus niet afgekeurd (een afgekeurde tabel zou "er komt GEEN invoer" melden en alles stilzetten).

## 1. De attract-lus kwam er, in DEZE run (met de nieuwe-regels-controle)

```
13:32:18  FileTest Reported 1 errd files at startup!
13:32:43  Network initialization completed
13:32:46  sound bank: ATTRACT        <-- NIEUWE regel, t=44 s, na 12 START-pulsen
13:33:10  sound bank: level1
13:34:30  sound bank: level2
```
De `NIEUW()`-controle deed zijn werk; er is geen oude regel meer gebruikt.

## 2. HET APPARAATBLOK: de vlag blijft staan (VOORSPELLING 4 BEVESTIGD)

Drie keer gelezen, met 10 s ertussen, terwijl het spel in de attract-lus liep:

| adres | stand 1 (t=47 s) | stand 2 (t=57 s) | stand 3 (t=67 s) |
|---|---|---|---|
| `0x1a7cb20` (+0, aanwezig) | **1** | **1** | **1** |
| `0x1a7cb5c` (+0x3c, spelernr) | **0** | **0** | **0** |
| tellers knop 0 / 1 / 3 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| aim x / y (`0x1f392a0/4`) | **-1 / -1** | **-1 / -1** | **-1 / -1** |
| spelersaantal `[0x1f392d0]` | **0** | **0** | **0** |

**Dus: mijn `zet`-regels landen en het spel zet ze NIET terug** -- de voorspelling dat het spel niet naar
`+0`/`+0x3c` terugschrijft, is bevestigd. En de aim staat op `-1` (u32 `3212836864` = `0xBF800000` =
`-1.0f`), precies de waarde die "geen richtpunt" betekent.

**MAAR ÉÉN VOORSPELLING IS WEERLEGD: `[0x1f392d0]` is NUL, niet groter dan nul.** Ik had voorspeld dat de
kast zijn spelersaantal zou hebben. Dat is niet zo, en dat is een serieuze kandidaat voor de verklaring
dat het spel ons blok verder negeert: **een spel met nul spelers leest misschien geen geweerblokken.**

## 3. DE AIM: de keten breekt VÓÓR mijn tabel (GEMETEN)

`ravengunctl arm` + `test-pattern point` deden wat ze horen te doen op de **eigen uitvoer**:
```
Positie: aim 0.500,0.500 -> abs 16384,16384
Positie: aim 0.200,0.200 -> abs  6553,6553
Positie: aim 0.800,0.800 -> abs 13107,13107
```
Maar in het **gedeelde geheugen** dat mijn `as`-regel als bron leest, veranderde er **niets**:
```
shm[12]=0 shm[13]=0     (bij alle drie de posities)
aim x/y in het spel: -1 / -1   (bij alle drie)
```
**Conclusie: de muismodus van de daemon stuurt het virtuele evdev-apparaat (`event13`, abs 0..32767) en
publiceert NIET in de tp-shm-indeling.** Mijn `as`-regel heeft dus niets te lezen gehad. Dat betekent:
* de **eenheid** van de aim is **niet getest** -- die voorspelling is niet weerlegd, hij is **niet aan de
  beurt gekomen**; de keten brak één schakel eerder;
* en de vraag is nu gericht: **moet de daemon de aim in het gedeelde geheugen publiceren** (bytes 12/13),
  of moet de hulp de aim ergens anders vandaan halen? Dat is een vraag voor de laderbouwer, en het is
  precies de koppeling die in de route `tp-shm` hoort te zitten.

## 4. DE DRIE TREKKERKANDIDATEN: geen van drie (en om dezelfde reden)

Drie keer per kandidaat, met een foto per poging:
```
KANDIDAAT knop0: output-button trigger tap x3   -> tellers 0 / 0 / 0
KANDIDAAT knop1: output-button reload  tap x3   -> tellers 0 / 0 / 0
KANDIDAAT knop3: output-button grenade tap x3   -> tellers 0 / 0 / 0
```
En het logboek van de hulp laat zien waarom: er staat **geen enkele** `indruk bit 7`, `bit 8` of `bit 9`
in -- alleen `indruk bit 6` (START). **`output-button` stuurt dus ook het evdev-apparaat en niet het
gedeelde geheugen.** Daarmee is de tellerreeks **niet getest**: welke van de drie knoppen de trekker is,
is nog onbekend, en ook of het overgangstellermodel met `modulo=16` klopt.

## 5. Wat deze run WEL oplevert

1. **Het schrijfpad van de tabel is bewezen voor `zet`**: `+0` en `+0x3c` landen en blijven staan (het
   spel schrijft niet terug).
2. **Het apparaatblok "aanwezig" zetten is onschadelijk**: het spel bleef in de attract-lus, liep door
   naar `level1` en `level2` en crashte niet.
3. **De tabel is geldig en additief**: 13 ingebouwde schakelaars + mijn 2 assen, 3 tellers en 2 vaste
   waarden, en START blijft werken.
4. **Twee gemeten blokkades**, allebei in de keten vóór mijn tabel:
   * de daemon publiceert **geen** aim in het gedeelde geheugen (shm 12/13 blijven 0);
   * de daemon publiceert **geen** geweerknoppen in het gedeelde geheugen (geen bit 7/8/9).
5. **En een onverwachte meting**: `[0x1f392d0]` = **0** (niet > 0), wat kan verklaren waarom het spel het
   blok verder niet gebruikt.

## 6. Wat ik NIET heb kunnen vaststellen

* De **eenheid** van de aim (0..1, -1..1 of pixels) -- de bron bewoog niet.
* **Welke** van de drie knoppen de trekker is -- de tellers kwamen niet in beweging.
* Of het **overgangstellermodel** (`modulo=16`) klopt -- idem.
* Wat de **kalibratie op `0xdccc80`** met de twee kommagetallen doet -- er viel niets te kalibreren.
* Of het spel het blok **gaat lezen** als het spelersaantal groter dan nul is.
* En of de aim **optisch** of via het bord loopt: deze run zegt daar niets over, want de bordkant kreeg
  geen data.
