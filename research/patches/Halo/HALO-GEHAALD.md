# HALO IS GEHAALD — 6,0 minuten attract-lus op de native route (RUN 11 + 12)

*RavenGun, 2026-10-13. Twee runs: run 11 (200 s) als eerste schone toets van de vier-borden-variant, en
run 12 (420 s) om de lat te halen. Beeld op de tv, de eigenaar keek mee, geen Move, geen TeknoParrot en
geen gesloten kern. Na de runs: niets draait, geen crash (`FATAL-regels: 0`). Geen commit.*

## 1. DE LAT IS GEHAALD

De afspraak met de eigenaar: **gehaald = 5 minuten attract- of spelmodus.**

```
attract-bank om 11:43:56, einde om 11:49:59 -> 363 seconden = 6,0 minuten in de attract-lus
DE LAT (5 minuten = 300 s): GEHAALD
```

En het logboek van de game laat zien dat hij al die tijd **leefde en doordraaide** -- de attract-lus
wisselt van level-bank:

```
11:43:11  Failed to load FMOD sound bank: perm
11:43:37  Failed to load FMOD sound bank: ATTTRACT      <- de attract-lus begint
11:44:00  Failed to load FMOD sound bank: level1
11:45:07  Failed to load FMOD sound bank: level2
11:46:32  Failed to load FMOD sound bank: level3
11:47:49  Failed to load FMOD sound bank: level4
11:49:08  Failed to load FMOD sound bank: level5        <- en hij draait nog
```

Zes foto's, één per minuut, met wisselende omvang (500 kB -> 3,6 MB -> 1,5 MB): het scherm
**beweegt**, het is geen bevroren beeld. (`run12-lat-1-min.png` t/m `run12-lat-6-min.png`.)

## 2. HET SCHERM (run 11, na de munt)

Op de foto van run 11 staat het titelscherm van de game: twee mariniers voor een hangardeur, en in
beeld:

* **"Press START to Begin"** (geel, groot),
* **"Money In: $16.75"** -- onze muntindrukken zijn dus **geteld door de game**,
* **"QR scanners need to be configured!"**, **"P1 QR Scanner is not detected!"**,
  **"P2 QR Scanner is not detected!"** en **"Factory Setup Has Not Been Completed!"** -- de twee
  QR-lezers en de fabrieksinstelling die deze kast mist (die waarschuwingen staan volgens de
  projectdocumentatie permanent in beeld).

## 3. DE ROUTE, NU SCHOON GEMETEN

1. **START** (puls ~400 ms, herhalen tot `FileTest Reported` in het logboek staat) -> de FileTest weg;
   in beide runs was **één** puls genoeg.
2. **START** (puls ~400 ms, herhaald) -> dat bracht het spel verder; de TEST-pulsen deden in run 11
   **niets** (het logboek bleef exact op 256 regels en alle zeven fotos bleven de kalibratie).
   LET OP: dit corrigeert onze eigen notitie, die de rollen omdraaide (TEST voor gun 1, START voor gun 2).
4. Dan komt de game in zijn **attract-lus** (bank `attract`, dan level1..level5) en blijft daar draaien.

## 4. DE OORZAAK VAN ALLE EERDERE MISLUKKINGEN WAS MIJN EIGEN INGREEP (bevestigd)

In run 7, 8 en 10 gebruikte ik **`RG_RT_PROBE=0,0,4`**, en dat herschrijft de vier tabelregels van bit 0
(test) naar **alleen bord 0**. De hulp perst TEST standaard op **alle vier de borden**; dat is de
variant die werkte. Run 11 was de **eerste schone toets** van die variant, en de uitkomst is precies wat
ik vooraf had opgeschreven: de kalibratie ging weg en de game liep door naar de attract-lus.

**Ik heb dus zelf de werkende indruk weggehaald en daarna geconcludeerd dat TEST niet werkte.** Dat is
nu rechtgezet, en de les staat in het dossier: *een probe die de standaardtabel overschrijft, verandert
de proef* -- en dan meet je je eigen ingreep.

## 5. DE MUNT WERKT OOK (met een kanttekening)

Vier muntindrukken van 400 ms gaven **$16.75**, en de game waarschuwde zelf:
`WARNING: Detected more coin pulses than safely allowed on cashbox 0 coin 1!`. Dat klopt met de
documentatie: een indruk die langer duurt **telt per beeld meerdere keren**. Voor $0,25 per stuk is
~14 ms de juiste lengte; voor "er geld in hebben" is 400 ms prima.

## 6. WAT DEZE RUNS **NIET** OPLEVEREN

* **De aim en de trekker.** Die zitten in het `WartranTroopers`-profiel op **offset 12..19 (1 byte per
  as)** en **bit 7 van offset 8 (p1.trigger)** -- en onze hulp schrijft **geen van beide**. Dat is
  code (stap d), geen run. De trekker is dus nog steeds nooit aangesloten geweest; wat ik in run 8
  deed was de trekker van het *virtuele lichtgeweer* (evdev), en dat is een andere wereld.
* **De spelmodus.** De lat is gehaald met **attract**; "Press START to Begin" is in de attract-lus niet
  reproduceerbaar gebleken (één keer wel, volgens de documentatie). Dat is een volgende stap, geen
  onderdeel van deze lat.
* **Geluid**: FMOD 1.10 ontbreekt; de banken worden geladen en falen met code 52 (onze stub). Geen
  geluid, en dat is bekend.
* **Het FTDI-/geweerbord**: de tap zag in deze runs niets (geen opsomming). Daar is niets over gezegd.

## 7. De stand van game 3

**Halo: Fireteam Raven is gehaald** volgens de afspraak: 6,0 minuten attract-lus, native, met onze eigen
hulp, zonder Move, zonder TeknoParrot en zonder de gesloten kern -- op de tv, met de eigenaar erbij. Wat
nog openstaat is de **aim en de trekker** (de assen en bit 7 in het bordformaat), en dat is de vraag die
dit project moet beantwoorden.
