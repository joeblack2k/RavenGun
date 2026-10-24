# RUN 7 voor Halo — de TEST-vraag is beslecht op het JUISTE scherm

*RavenGun, 2026-10-13. Uitgevoerd **10:58:30 - 11:01:xx** (270 s, beeld op de tv, exitcode 0). Geen
crash (`FATAL-regels: 0`). Niets van mij draait nog. Geen commit.*

Deze run herstelt de fout van run 6: daar vielen de TEST-pulsen op het FileTest-scherm. Hier is eerst
de FileTest overgeslagen, daarna is het **kalibratiescherm** op de foto's te zien, en **op dat scherm**
zijn de TEST-pogingen gedaan. De uitkomst is daarmee niet langer onbeslist.

## 1. De tijdstempels

| moment | wat |
|---|---|
| 10:58:30 | start (launcherlogboek) |
| 10:58:31 | `Welcome to Halo v03.11` -- **1 s na de start** (warme cache, zie run 5) |
| **10:58:42** | **START-puls 1** -> in dezelfde seconde `FileTest Reported 1 errd files at startup!` |
| 10:58:44 | `FMOD - Error initializing! Code: 52` (= onze eigen stub, zie run 5) |
| 10:58:55 | vijf shader-fouten (`cov_shield`, `elite_shield`, ...) |
| 10:59:04 | `Failed to load FMOD sound bank: perm (ec 52)` |
| 10:59:06 | `Webcam: Could not ID device for player 0` / `Aborting auto-id` |
| 10:59:09 | `Network initialization completed` + de herstartplanning |
| 10:59:11 - 11:00:50 | **negen TEST-indrukken** (k=3, k=4, k=5, daarna zes keer k=4) |
| t=270 s | netjes beeindigd door de timer |

**De FileTest werd met de EERSTE puls overgeslagen.** De herhaalstrategie was dus niet eens nodig, maar
ze staat klaar en ze verklaart run 4 en run 6 (waar een enkele puls niet aankwam).

## 2. WAT ER OP HET SCHERM STOND

| foto | tijd | omvang | wat het is |
|---|---|---|---|
| `run7-na-start.png` | 10:58:4x | 11,8 kB | vrijwel egaal/zwart: het scherm direct na de FileTest, voordat het beeld staat |
| `run7-voor-test.png` | 10:59:0x | 11,8 kB | idem, nog zwart |
| `run7-test-k3.png` | 10:59:11 | **1.466,8 kB** | **het kalibratiescherm** |
| `run7-test-k4.png` | 10:59:27 | **1.466,6 kB** | **hetzelfde kalibratiescherm** |
| `run7-test-k5.png` | 10:59:43 | **1.466,3 kB** | **hetzelfde kalibratiescherm** |
| `run7-eind.png` | 11:01:0x | **1.467,1 kB** | **hetzelfde kalibratiescherm** |

De laatste foto, in gewone woorden: een donkerrood scherm met een circuitpatroon, linksboven het
Halo-logo, en in witte letters:

> **Player 1 gun (left):**
> Pull gun all the way back and towards the left, then press the trigger.
> You should hit the limits of the gun so it can point to the top left corner of the left screen.
> Press the TEST button to skip calibration for this gun.

Dus: het scherm **vraagt** om TEST, en na negen TEST-indrukken staat het er **nog precies zo**. Dat is
de tegenstelling met run 6 (waar de foto's op het FileTest-scherm waren) en het maakt de uitkomst hard.

## 3. DE TAFELMETING, IN DEZELFDE RUN EN OP HET JUISTE SCHERM

De buitenlezer (via `/proc/<pid>/mem`, dezelfde adressen als de hulp) zag elke indruk aankomen:

```
BUITEN t=32s  k3 = 01000000 00000000 00000000 01000000   <- [0]=1, [3]=1, [5]=1  (k=3)
BUITEN t=48s  k4 = 01000000 00000000 00000000 01000000   <- idem (k=4)
BUITEN t=64s  k5 = 01000000 00000000 00000000 01000000   <- idem (k=5)
BUITEN t=80s  k4 = 01000000 00000000 00000000 01000000   <- en zes keer herhaald
```

en de hulp zelf meldde hetzelfde patroon (`tabel bord 0 k 4 NA de indruk: [0]=1 [3]=1 [4]=0 [5]=1
[6]=26`, `bij LOSLATEN: [3]=0 [5]=0 [7]++`). **De indrukken kwamen dus aan in de tafel van het spel,
op de juiste index (k=4 = diag/test volgens de indeling van de hulp), terwijl het kalibratiescherm op
het scherm stond -- en het scherm verschoof niet.**

## 4. DE CONCLUSIE

**Dit scherm reageert niet op TEST.** Niet op k=3 (service), niet op k=4 (diag/test, de juiste volgens
de indeling), niet op k=5 (munt 1), en niet op zes herhalingen. De schakelaar komt aan, de tafel
verandert, het beeld staat er goed op (het renderen is in orde) en het scherm blijft staan.

Daarmee blijft er een kandidaat over, en die stond al op het scherm zelf: **de trekker**. Het scherm
zegt letterlijk *"Pull gun all the way back and towards the left, then press the trigger"* -- het wil
een **geweer met een trekker**, en het vraagt zelfs om een **positie** (de uiterste stand linksboven).
Wij sturen geen van beide: er is geen Move aangesloten (geen enkel apparaat met fabrikant `054c`, geen
Sony in `hidraw`) en dus is er wel een virtueel geweer (`RavenGun Virtual Lightgun`, 1209:7202) maar
zonder trekker en zonder beweging.

**Dit is geen muur maar de kern van deze lader.** De eigenaar speelt deze games met een Move als
geweer; de vraag of wij die trekker (en die beweging) kunnen sturen is precies de vraag die dit project
moet beantwoorden. Wat deze run toevoegt is dat het **niet** aan TEST ligt en **niet** aan het
schakelaarpad: dat is nu uitgesloten met een meting, en dat is precies wat we nodig hadden voordat we
de Move-kant in gaan.

## 5. De lat (afspraak met de eigenaar)

**Gehaald = 5 minuten attract- of spelmodus.** Deze run blijft op het kalibratiescherm staan en haalt
de lat dus **niet**. Het is de verst mogelijke stand zónder geweer, en de volgende stap vraagt om de
Move.

## 6. De tap, en wat deze run niet oplevert

De tap zag **6 x `tap start`** en verder niets: geen attribuutregels, geen descriptors, geen
`APPARAAT`. Het FTDI-pad is nog steeds niet bereikt.

Wat deze run **niet** oplevert:
* **niets over het FTDI-protocol** (geen opsomming gezien);
* **niets over aim of trekker** -- die zijn niet aangeraakt; deze run laat alleen zien dat TEST het
  niet doet;
* **geen bewijs dat de trekker de oplossing is.** Dat is een afleiding uit het scherm en uit het feit
  dat TEST is uitgesloten; de meting daarvoor moet met een aangesloten Move gebeuren;
* **niets over de vraag of ook de positie (de uiterste stand) nodig is** naast de trekker -- het scherm
  vraagt om allebei.

## 7. Bewaard

* `patches/Halo/run7-na-start.png`, `run7-voor-test.png`, `run7-test-k3.png`, `run7-test-k4.png`,
  `run7-test-k5.png`, `run7-eind.png` -- het schermverslag, met het kalibratiescherm op de laatste vier.
* `patches/Halo/gamelog-run7.txt`, `patches/Halo/usb-tap-run7.log`.
* `patches/Halo/probe/run7_uitvoeren.sh` -- met de herhaalstrategie voor START en de buitenlezer.
* De invoerhulp met tafellogging: `build/librg_rt_input.so`, sha256 `2640546331b3dd46`.
