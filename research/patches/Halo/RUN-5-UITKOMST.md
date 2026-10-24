# RUN 5 voor Halo + de tijdrun — de uitkomst

*RavenGun, 2026-10-13. Run 5: **10:44:36 - 10:47:15** (150 s, beeld op de tv). Tijdrun: **10:47:53 -
10:48:53** (60 s, headless, direct daarna). Opgeruimd: geen game, geen launcher, geen gamescope.
Geen commit.*

## EERST DE LAT (uitspraak van de eigenaar)

**"Gehaald" betekent: 5 minuten attract- of spelmodus.** Het FileTest-scherm, de Webcam-stap en het
kalibratiescherm tellen **niet** -- dat is het beginscherm. Run 3 en run 5 komen dus door de FileTest,
door de webcam-stap en op het kalibratiescherm, en dat is een **grote stap maar geen gehaalde game**.
Niemand moet "hij komt door de FileTest" als "gehaald" lezen.

## 1. Run 5: wat er gebeurde

| moment | wat |
|---|---|
| 10:44:36 | start (launcherlogboek `native-Halo-20261013-104436`) |
| 10:44:37 | eerste nieuwe regel in het logboek van de game: `Welcome to Halo v03.11` |
| 10:44:56 | **START (bit 6, puls 2000 ms)** -> in dezelfde seconde: `FileTest Reported 1 errd files at startup!` |
| 10:45:17 | `Webcam: Could not ID device for player 0` / `Aborting auto-id` |
| 10:45:20 | `Network initialization completed` + de herstartplanning |
| 10:45:32 | **TEST 1 (bit 0, bord 0)** |
| 10:46:03 | **TEST 2 (bit 0, bord 0)** |
| t=145 s | proces nog levend: **35 threads**, 10 libfmod-mappings |
| 10:47:15 | netjes beeindigd door de timer (exitcode 0) |

**Dus: de lange puls van 2000 ms werkt** -- de FileTest werd in dezelfde seconde overgeslagen, precies
zoals in run 3. Dat bevestigt vondst 1 van run 4 (de puls van 300 ms was marginaal).

**En er is NIET gecrasht**: het logboek van deze run eindigt met de netwerkregel en bevat geen `FATAL
signal`. Het spel leefde 150 s met 35 threads.

## 2. WAT ER OP HET SCHERM TE ZIEN WAS (met de tijdstempels van de foto's)

| foto | tijd | wat erop staat |
|---|---|---|
| `run5-t15-voor.png` | t=15 s | egaal/zwart -- het scherm in de eerste seconden, voordat de tekst geschreven is |
| `run5-t40-na-start.png` | t=40 s | nog egaal/zwart: de game is druk met de webcam- en netwerkstap |
| `run5-t70-na-test1.png` | t=70 s | **het kalibratiescherm**: *"Player 1 gun (left): Pull gun all the way back and towards the left, then press the trigger. You should hit the limits of the gun so it can point to the top left corner of the left screen. Press the TEST button to skip calibration for this gun."* |
| `run5-t115-na-test2.png` | t=115 s | **hetzelfde kalibratiescherm, ongewijzigd** |

In gewone woorden: het spel kwam op het kalibratiescherm van speler 1 en **bleef daar staan**. Mijn
twee TEST-pulsen hebben dat scherm niet laten verspringen. De foto's van t=70 en t=115 zijn vrijwel
byte-identiek groot (1.466.744 en 1.466.567 bytes) en tonen dezelfde tekst.

## 3. ZIJN DE PULSEN AANGEKOMEN? JA, alle drie

De invoerhulp meldde het zelf:

```
rg_rt_input: indruk bit 6 -> bord 0 schakelaar 0        <- START
rg_rt_input: indruk bit 0 -> bord 0 schakelaar 4        <- TEST 1 (vier regels: de hulp heeft vier
rg_rt_input: indruk bit 0 -> bord 0 schakelaar 4           tabelregels voor bit 0, en met RG_RT_PROBE
rg_rt_input: indruk bit 0 -> bord 0 schakelaar 4           staan ze alle vier op bord 0)
rg_rt_input: indruk bit 0 -> bord 0 schakelaar 4
... (in totaal 8 regels voor de twee TEST-pulsen)
```

Dus: de pulsen **kwamen aan bij de hulp** en zijn in de schakelaartabel gezet, allemaal op **bord 0**.
En de game reageerde er niet op. Dat is een echt resultaat, geen mislukte proef.

## 4. VONDST 1 VAN RUN 4 IS BEVESTIGD: de lange puls werkt

Run 4's START (300 ms) werd niet aangenomen, run 5's START (2000 ms) wel, bij dezelfde timing. Dat is
precies de marginale puls die ik in run 4 beschreef. **De knop is `RG_RT_PULSE_MS`** en die staat nu op
2000; dat blijft zo.

## 5. DE CRASH: weg, maar ik heb hem niet geisoleerd (en dat zeg ik expliciet)

De crash van run 4 is geanalyseerd en **gerepareerd**, en de reparatie is bench-vrij bewezen:

* **De oorzaak, uit de ontleding van `librg_rt_input.so`:** `+0x1f44` is `movzbl 0x8(%rdi),%edx` -- dat
  is de regel `bits = st[8] | st[9]<<8 | ...`, het lezen van het **gemmapde** schakelaarbeeld. Het
  foutende adres in het crashlogboek was `0x7f035b03c008` = `st+8`. De hulp mapte 64 bytes van mijn
  schakelaarbestand, en **ik schreef dat bestand met `open(...,"wb")` -- dat KAPT AF**. Een afgekapt
  bestand achter een mapping geeft de kernel reden om **SIGBUS** te sturen, en dat is exact wat er op
  10:30:08 gebeurde.
* **De reparatie, twee kanten:** de hulp leest het beeld nu met **`pread`** in plaats van `mmap`
  (mmap eruit, pread erin; `build/librg_rt_input.so`, sha256 `079e35823d72a05f`), en **mijn pulsen
  schrijven op hun plaats** (`os.pwrite`) in plaats van met `wb`.
* **Het bewijs, bench-vrij:** een lezer die mmap gebruikt en een lezer die pread gebruikt, met daar
  langs **dezelfde afkappende schrijver**. De mmap-lezer sterft met **`Bus error (core dumped)`** --
  exitcode 135 = SIGBUS, precies de fout van run 4 -- en de pread-lezer blijft lezen zonder een enkele
  fout (hij liep door tot ik hem stopte, want mijn proeflus had een niet-gedeelde stopvlag; dat is een
  fout in mijn proef, niet in de reparatie). En de in-place schrijver liet het bestand nooit kleiner
  worden dan 64 bytes.

**Wat ik NIET mag beweren:** dat de crash van run 4 door de vier borden kwam. In run 5 zijn **drie**
dingen tegelijk veranderd -- de hulp (mmap->pread), mijn schrijfmethode (in-place) en de TEST op alleen
bord 0 -- dus deze run isoleert de bordhypothese **niet**. Wat vaststaat is de afkap-oorzaak (bewezen),
en dat de combinatie van de drie nu geen crash geeft. Wie de bordhypothese wil toetsen, moet de TEST
weer op vier borden zetten met de gerepareerde hulp; dat is een kleine, goedkope proef.

## 6. DE TIJDRUN: de vraag is beslecht, en het is de CACHE

| run | beeld | cache | start | eerste nieuwe regel | verschil |
|---|---|---|---|---|---|
| 2 | headless | koud (eerste run na lange tijd) | 10:04:20 | 10:04:31 | **11 s** |
| 3 | tv | warm | 10:25:44 | 10:25:45 | **1 s** |
| 4 | tv | warm | 10:28:42 | 10:28:43 | **1 s** |
| 5 | tv | warm | 10:44:36 | 10:44:37 | **1 s** |
| **tijdrun** | **headless** | **warm** | **10:47:53** | **10:47:54** | **1 s** |

**De tijdrun is headless en geeft 1 s.** Daarmee is de verklaring niet het beeld maar de
**bestandscache**: run 2 was de eerste run in lange tijd (koud, 11 s), en alles daarna (warm, 1 s) --
ook headless. Mijn vooraf opgeschreven vermoeden dat de gamescope-backend de opstarttijd beinvloedt is
dus **weerlegd**, en de tweede kandidaat (cache) is bevestigd.

**De les voor alles wat we hierna meten:** bij elke tijdvergelijking tussen runs hoort te staan of de
run **warm of koud** was. Een verschil van tien seconden in opstarttijd zegt niets over het spel en
alles over de schijfcache. Dat is precies waarom deze goedkope run het waard was.

## 7. De tap

**7 x `tap start`** en verder niets: geen attribuutregels, geen descriptors, geen `APPARAAT`, geen
bord. Nog steeds uitkomst **(iii)** -- en dat is in deze run extra veelzeggend, want de game kwam
verder dan ooit (webcam- en netwerkstap gehaald) en is nog steeds **niet** bij zijn FTDI-pad geweest.

## 8. Wat run 5 expliciet NIET oplevert

* **Geen gehaalde game**: het spel bleef op het kalibratiescherm staan; de lat is 5 minuten attract- of
  spelmodus.
* **Niets over het FTDI-protocol**: de tap zag geen enkele opsomming.
* **Niets over aim of trekker**: niet aangeraakt.
* **Geen bewijs dat de TEST-puls het juiste schakelaarnummer of moment heeft.** De hulp zette
  schakelaar 4 (diag/test) op bord 0 -- dat is volgens de indeling van de hulp de juiste -- maar de
  game reageerde niet. Of dat aan het moment, de duur, of aan de ONTBREKENDE GUN ligt (het
  kalibratiescherm vraagt om een trekker, en de webcamstap faalde: `All webcams reported brightness
  difference of 0`), is niet te zeggen met deze gegevens. Dat is de volgende vraag.
* **Geen geisoleerd antwoord op de bordhypothese** (zie paragraaf 5).

## 9. Waar het nu op lijkt te stuiten

De game komt tot het kalibratiescherm en vraagt daar om een **geweer** (trekker) of om TEST. Onze TEST
komt aan maar doet niets, en de **webcamstap is mislukt** (`Could not ID device for player 0`). De
eerdere projectmetingen zeggen dat de SDL-joystick als geweer gevonden wordt -- en in onze runs is er
geen controller aangesloten, dus mogelijk vindt de game geen geweer en blijft het scherm staan. Dat is
een concrete volgende vraag, en de tap heeft daar niets mee te maken.

## 10. Bewaard

* `patches/Halo/run5-t15-voor.png`, `run5-t40-na-start.png`, `run5-t70-na-test1.png`,
  `run5-t115-na-test2.png` -- het schermverslag van deze run (het kalibratiescherm op t=70 en t=115).
* `patches/Halo/gamelog-run5.txt` en `patches/Halo/gamelog-tijdrun.txt`.
* `patches/Halo/usb-tap-run5.log`.
* `patches/Halo/probe/run5_uitvoeren.sh`, `probe/tijdrun.sh`, `probe/crash_reparatie.sh`,
  `probe/crash_diagnose.sh` -- alles herhaalbaar.
* De gerepareerde invoerhulp: `~/projects/ravengun/build/librg_rt_input.so`, sha256 `079e35823d72a05f`.
