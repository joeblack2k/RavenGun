# RUN 11 — het account: de vier-borden-variant, en wie de kalibratie nu werkelijk wegzet

*RavenGun, 2026-10-13. Run 11 liep **11:38:19 - 11:41:40** (200 s, beeld op de tv, exitcode 0). Ter
vergelijking: **run 12** liep 11:42:44 - 11:49:59 (420 s) en hield de attract-lus **6,0 minuten** vast.
Beide runs schrijven in hetzelfde logboek van de game, dus de gezamenlijke tijdlijn loopt door. Geen
crash in beide runs (`FATAL-regels: 0`). Niets draait. Geen commit.*

## 1. WAT ER PER STAP GEBEURDE (narekenbaar)

| t (run 11) | tijd | wat ik deed | wat het logboek deed | het scherm |
|---|---|---|---|---|
| 13 s | 11:38:31 | **START** (bit 6, puls ~400 ms, 1x) | `FileTest Reported 1 errd files at startup!` | zwart -> opstart |
| 25 s | 11:38:54 | -- | `Network initialization completed` | kalibratie (1,47 MB) |
| 38-75 s | 11:39:1x | **TEST (bit 0) op ALLE VIER de borden, 6x** | **bleef exact op 256 regels** | **bleef de kalibratie, alle 7 foto's 1,47 MB** |
| 82-104 s | 11:39:41-11:40:2x | **START (bit 6), 4x** | **ging groeien** (257, 258 regels) en `attract` om **11:39:41** | **sprong naar 2,7-3,1 MB: andere schermen** |
| 111-129 s | 11:40:1x-11:40:29 | **munt (bit 2), 4x** | `Detected more coin pulses...` en `Money In` | titelscherm |

**En dat corrigeert onze eigen notitie.** Die zei: de testknop slaat de kalibratie van gun 1 over, START
die van gun 2. **Op deze machine is het gemeten andersom:** de zes TEST-pulsen op alle vier de borden
deden **niets** (het logboek bleef exact op 256 regels, alle zeven foto's bleven de kalibratie van
1,47 MB), terwijl de **START-pulsen** het spel verder brachten (het logboek groeide en de foto's
sprongen naar 2,7-3,1 MB). Wat blijft staan is de uitkomst: **de kalibratie ging weg en de game kwam in
de attract-lus.**

**De tijdlijn van de banken (uit het logboek van de game, run 11 en 12 samen):**

```
11:38:49  perm
11:39:41  attract      <- run 11: de lus begint
11:40:05  level1
11:41:12  level2
11:42:24  (run 11 eindigt; run 12 begint om 11:42:44)
11:43:11  perm         <- tweede ronde
11:43:37  attract      <- run 12: de lus begint opnieuw
11:44:00  level1
11:45:07  level2
11:46:32  level3
11:47:49  level4
11:49:08  level5       <- vijf verschillende leveldemo's
```

**9 minuten en 27 seconden attract-lus over de twee runs**, en de lus laadt **elke ronde een andere
leveldemo** (level1..level5) -- dat is het bewijs dat het echt attract is en geen stilstaand scherm.

## 2. DE FOTO'S, MET TIJDSTEMPELS

| bestand | tijd | omvang | wat het is |
|---|---|---|---|
| `run11-a0-kalibratie.png` | 11:38 | 1.466 kB | de kalibratie, vóór de TEST-reeks |
| `run11-a1-na-test.png` ... `a6-na-test.png` | 11:39 | 1.466 kB | **alle zes identiek**: de TEST-pulsen deden niets |
| `run11-b1-na-start.png` | 11:39 | **2.790 kB** | **ander scherm** (na de eerste START) |
| `run11-b2`, `b3` | 11:39-11:40 | 1.351 / 1.261 kB | andere schermen |
| `run11-b4-na-start.png` | 11:40 | **3.115 kB** | ander scherm |
| `run11-c1-na-munt.png` | 11:40 | 2.449 kB | **het titelscherm**: "Press START to Begin" en **"Money In: $16.75"** |
| `run12-lat-1-min.png` ... `lat-6-min.png` | 11:44-11:49 | 500 kB - 3,6 MB | de attract-lus, zes keer, wisselend beeld |

## 3. DE IDENTITEITSREGEL STAAT ER -- en dat is het gereedschap van deze week

De eerste regels van het logboek van run 12 (het bestand dat de launcher zelf schrijft):

```
script: /home/deck/projects/ravengun/scripts/play-native.sh
  de versie uit de REPO (commit 5cdfaa8; scripts/ gelijk aan die commit); sha256 c19370b6d9d2ef09...
  gegevens: .../packaging/native-games.json sha256 6ea6aa97f23801d8...
  hulp: .../scripts/rg-paths.sh sha256 8ca01e47...
  hulp: .../scripts/rg-identiteit.sh sha256 be4c411f...
  bibliotheek: .../build/librg_rt_input.so sha256 53bca814770d681d...
  meegegeven: .../patches/Halo/probe/libusb-tap.so sha256 9b63cea4c56fe0d5...
  meegegeven: /home/deck/.local/share/ravengun/tap/start.jvs sha256 f5a5fd42...
  het geinstalleerde script (/home/deck/.local/opt/ravengun/share/ravengun/scripts/play-native.sh) is hetzelfde
```

Dus: **de identiteitsregel is er**, hij noemt pad, commit, sha256 van het script, de gegevens, de hulpen
en alles wat wij meegeven -- en hij zegt bovendien dat het **geinstalleerde** script hetzelfde is als het
repo-script (de valkuil van deze week, nu automatisch afgedekt).

**Let op één verschil met run 8:** daar was `play-native.sh` sha256 `0868ffd754b7e8a5`, in run 11/12 is
dat `c19370b6d9d2ef09` (repo-commit `5cdfaa8`). De launcher is dus tussen die runs bijgewerkt -- precies
waarom die identiteitsregel er nu is.

## 4. De munt

Vier muntindrukken van ~400 ms gaven **$16.75** en de game waarschuwde:
`WARNING: Detected more coin pulses than safely allowed on cashbox 0 coin 1!` -- een lange indruk telt
per beeld meerdere keren (zie `docs/decisions.md`). Voor $0,25 per stuk is ~14 ms de juiste lengte.

## 5. WAT RUN 11 **NIET** OPLEVERT

* Geen aim en geen trekker: die velden schrijft onze hulp niet (zie `HULP-TREKKER-EN-ASSEN.md`).
* Geen spelmodus: de lat is gehaald met de **attract-lus**, niet met het spel zelf.
* Geen geluid (FMOD 1.10 ontbreekt; de banken falen met code 52 van onze eigen stub).
* Niets over het FTDI-/geweerbord: de tap zag geen opsomming.
* Geen uitspraak over de precieze rolverdeling TEST/START buiten deze machine: wat hier gemeten is, is
  dat START het deed en TEST niet.
