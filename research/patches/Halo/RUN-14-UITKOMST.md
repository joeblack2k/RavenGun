# RUN 14 — de service-schakelaar: wél aangekomen, geen menu (op dit scherm)

*RavenGun, 2026-10-13. Uitgevoerd **12:47:46 - 12:51:2x** (200 s, beeld op de tv, exitcode 0). Niets
draaide ernaast; de Move was niet aangesloten; de runtime-kern staat op `f5c7532d6e638485`; opgeruimd
met alleen mijn eigen pids. Geen commit.*

## 1. Wat ik deed en wat er gebeurde

| t | tijd | wat ik stuurde | wat het spel deed | scherm |
|---|---|---|---|---|
| 13 s | 12:47:59 | **START** (bit 6, 1x) | `FileTest Reported 1 errd files at startup!` | opstart |
| 25 s | 12:48:24 | -- | webcam-mislukking, `Network initialization completed` | **kalibratie** (1,47 MB) |
| 39-52 s | 12:48:25-38 | **SERVICE** (bit 1 -> k=3), 4x, puls ~800 ms | **logboek bleef op 580 regels** | **bleef de kalibratie (1,47 MB)** |
| 60-70 s | 12:48:4x | nog 2 service-pulsen | idem | idem |

## 2. De meting: de indruk kwam AAN (van twee kanten)

De hulp meldde vier keer `indruk bit 1 -> bord 0 schakelaar 3`, en de tafelmeting van buiten het proces
laat de teller oplopen:

```
bord 0 k=3 (service): 00000001 ... 00000001 00000001   <- voor de reeks: leeg
bord 0 k=3 (service): 00000001 ... 00000002 00000002   <- na puls 2
bord 0 k=3 (service): 00000001 ... 00000003 00000003   <- na puls 3
bord 0 k=3 (service): 00000001 ... 00000004 00000004   <- na puls 4 (bij LOSLATEN: [0]=1, [7]=4)
```

En ter controle: **test (bit 0) is nooit gezet** (0 indrukken), start (bit 6) 1 keer, service (bit 1)
4 keer. De service-indruk komt dus aan in de tafel van het spel -- precies zoals bij START en TEST eerder
-- maar **het scherm verandert niet**.

## 3. De eerlijke beperking van deze run

**Het spel stond op het kalibratiescherm, niet in de attract-lus.** Dat is te zien aan de foto's: alle
zes na de service-pulsen zijn ~1,466 MB, precies de omvang van het kalibratiescherm in run 11 en 13, en
het logboek kreeg **geen enkele regel** bij (geen `attract`-bank). De opzet was: alleen één START om de
FileTest te passeren en daarna **geen START meer** (zoals gevraagd). Daardoor is de uitkomst van deze run
**"de service-schakelaar doet niets op het kalibratiescherm"** -- en **niet** "er is geen servicemenu".
Dat onderscheid is belangrijk en het is mijn eigen opzetfout om dat niet vooraf te voorzien.

## 4. Wat de volgende run moet doen (kort en gericht)

**Eerst naar de attract-lus, dan pas service.** Concreet: START herhalen tot de kalibratie weg is en de
`attract`-bank in het logboek staat (dat is in run 11/13 in ongeveer 20-40 s gebeurd), en **daarna**
service-pulsen (bit 1 -> k=3, 800 ms, een paar keer) met een foto per stap, en zo nodig de
volumetoetsen (bit 4/5) als menunavigatie. Verwachting vooraf, ongewijzigd: een service-/diagnosescherm
met de geweren, de camera's en de online-inrichting.

**En als het daar ook niets doet**, dan is de volgende kandidaat het **lezen** van de bewaarde
webcam-volgorde en de `AdjValStore`-familie onder `pmuser/adj/save/` (`coin*`, `sound`, `tickets` --
elk met `name`, `version`, `time_set` en een waarde). Dat is geen run.

**En zoals afgesproken:** als ik een item zie als "webcam/gun: none" of "factory setup", dan **zet ik
niets** -- ik meld eerst wat ik zie en wat ik zou willen doen.

## 5. De identiteitsregel (en wat er veranderd is)

```
== play-native Halo 20261013-124747
script: /home/deck/projects/ravengun/scripts/play-native.sh
  de versie uit de REPO (commit 4f6a70a; met wijzigingen in scripts/ die niet in die commit zitten); sha256 c19370b6d9d2ef09…
  gegevens: …/packaging/native-games.json sha256 06db1453b689aaa4…
  hulp: …/scripts/rg-paths.sh sha256 8ca01e47…   hulp: …/scripts/rg-identiteit.sh sha256 be4c411f…
  bibliotheek: …/build/librg_rt_input.so sha256 8ca6f3ba46849f19…
  meegegeven: …/probe/libusb-tap.so sha256 9b63cea4c56fe0d5…
```

Twee dingen vallen op, en dat is precies waar die regel voor is: het script meldt **wijzigingen in
`scripts/` die niet in commit `4f6a70a` zitten**, en zowel de **gegevens** (`06db1453…` tegen
`6ea6aa97…` eerder) als de **invoerbibliotheek** (`8ca6f3ba…` tegen `53bca814…` eerder) zijn veranderd
sinds run 13. Er is dus aan de launcher, de gegevens en de hulp gewerkt tussen die runs -- iets om te
weten als je twee runs vergelijkt.
