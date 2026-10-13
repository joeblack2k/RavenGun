# RUN 6 voor Halo — de tafelmeting, en waarom de TEST-pogingen op het verkeerde scherm vielen

*RavenGun, 2026-10-13. Uitgevoerd **10:52:22 - 10:56:2x** (240 s, beeld op de tv, exitcode 0). Geen
crash (`FATAL-regels: 0`). Opgeruimd: niets draait. Geen commit.*

## 1. WAT VOOR INVOER ER IS (de vraag "ziet het spel een geweer")

| apparaat | node | VID:PID |
|---|---|---|
| **RavenGun Virtual Lightgun** | `/dev/input/event13` | **1209:7202** |
| RavenGun Virtual Keyboard | `/dev/input/event29` | 1209:7201 |
| libvirtualhid Keyboard / Mouse | event256/257/258 | 1209:0002 / 1209:0003 |
| Valve Software Steam Controller | event7/14/15/30 | 28de:1304 |
| Cooler Master ARES (toetsenbord) | event2/3/4 | 2516:014d |
| Dell KB216 | event17/18/19 | 413c:2113 |

**En de PlayStation Move: NIET aangesloten.** Er is **geen enkel apparaat met fabrikant `054c`** in
`/sys/class/input`, en in `hidraw` staat geen Sony-apparaat (alleen Cooler Master, Dell, Asus AURA en
de Steam Controller-puck). De Move is eerder als `hidraw4` gemeten (88 rapporten per seconde); die is
er nu niet.

Wat er **wel** staat is een **`RavenGun Virtual Lightgun` (1209:7202)** -- een virtueel apparaat van
onze eigen kant. Dat is belangrijk voor de vraag: het spel *kan* een geweer zien (dit apparaat), maar
dat geweer krijgt zijn beweging en zijn **trekker** van de daemon, en die daemon heeft de Move nodig.
Zonder Move is er dus een geweer zonder trekker.

**Dit is een handeling van de eigenaar:** de Move aanzetten/verbinden (bluetooth). Dat mag ik niet zelf
doen en dat vraag ik hierbij.

## 2. DE TAFELMETING: DE SCHRIJFACTIE KOMT AAN, VAN TWEE KANTEN BEVESTIGD

De hulp is uitgebreid zodat hij de **tabelwaarden die de game leest** logt, en daarnaast las ik
**dezelfde adressen van buiten het proces** mee via `/proc/<pid>/mem`. De hulp en de buitenlezer zijn
het eens:

```
rg_rt_input: tabel bord 0 k 4 NA de indruk:    [0]=1 [3]=1 [4]=0 [5]=1 [6]=30
rg_rt_input: tabel bord 0 k 4 bij LOSLATEN:    [0]=1 [3]=0 [4]=0 [5]=0 [7]=25
BUITEN t=48s  k4=01000000000000000000000001000000     <- [0]=1, [3]=1, [5]=1
BUITEN t=50s  k4=01000000000000000000000000000000     <- losgelaten
```

Daarmee is de vraag "komt mijn schrijfactie in de tabel van het spel aan" **gemeten en niet
aangenomen**: `[0]` (soort = actief), `[3]` (indruk in dit beeld) en `[5]` (vastgehouden) gaan op 1 bij
de indruk, `[6]` telt op, en bij het loslaten gaan `[3]` en `[5]` terug naar 0 en telt `[7]` op. De
buitenlezer ziet exact dezelfde overgangen op exact dezelfde adressen. Ook de zijdelingse stappen
(k=3 en k=5, uit poging 1) zijn van buiten bevestigd.

**En dat geldt ook voor bord 0, k=4**: dat is volgens de indeling van de hulp (k 0 = start, 1/2 =
volume, 3 = service, **4 = diag/test**, 5..8 = munt) de juiste schakelaar voor TEST. **De index klopt
dus, en het pad klopt.**

## 3. WAAROM DE TEST-POGINGEN NIETS DEDEN: ZE VIELEN OP HET FILETEST-SCHERM

**In deze run is de START-indruk niet aangenomen.** Het eigen logboek van de game heeft na de start
maar **vier** nieuwe regels:

```
Tue Oct 13 10:52:23 2026: Welcome to Halo v03.11
Tue Oct 13 10:52:23 2026: Game Rev: 57529, Library Rev: 1412, Country Code: 48
FileTest Failure: '/pm/g7/halo/game'
```

Er staat **geen** `FileTest Reported 1 errd files at startup!` in, en dus is de FileTest **nooit
overgeslagen**: de game stond de hele run op het FileTest-scherm ("Press START or Ding to skip File
Test"). Mijn TEST-pulsen kwamen aan in de tafel -- maar op een scherm dat om **START** vraagt, niet om
TEST. **Poging 1 en poging 2 zijn dus ONBESLIST, niet negatief.** Dat is mijn fout in de opzet: ik nam
aan dat de START van run 5 zich zou herhalen.

**En dat is zelf een vondst, en dezelfde als in run 4.** Dezelfde puls (2000 ms), dezelfde timing
(19 s na `Welcome`), en toch wel in run 3 en 5 en niet in run 4 en 6. Dat patroon past bij een game die
de schakelaartafel **maar af en toe** bekijkt tijdens het scannen van duizenden bestanden: een puls van
2 s heeft dan een kans van ongeveer 2 s gedeeld door de bemonsteringsafstand om gezien te worden. De
fix is niet een langere puls maar **herhalen**: START elke 5 s blijven geven tot de FileTest eindigt.

## 4. WAT ER OP HET SCHERM TE ZIEN WAS

De foto's `run6-poging1-k3.png`, `run6-poging1-k4.png`, `run6-poging1-k5.png` en
`run6-poging2-eind.png` staan bewaard. **Uit het logboek volgt dat het FileTest-scherm er de hele run
stond** (geen `FileTest Reported`, geen webcam- of netwerkregel) -- dat is dus wat er op die foto's
hoort te staan, en dat is de tegenstelling met run 5 (waar op t=70 het kalibratiescherm stond). Ik
controleer er één na als je dat wilt; het logboek is hier de sterkere aanwijzing.

## 5. WAT DIT OPLEVERT, EN WAT DE VOLGENDE RUN MOET DOEN

**Opgeleverd:**
* de invoerinventaris: **geen Move aangesloten** (geen `054c`), wel een virtueel geweer (1209:7202);
* de **tafelmeting van twee kanten**: de schrijfactie komt aan, op de juiste index (k=4), met de
  juiste waarden, en de buitenlezer ziet hetzelfde;
* en de ontdekking dat de START-indruk **marginaal in de tijd** is: hij werkt niet altijd, en dat
  verklaart waarom run 4 en run 6 op het FileTest-scherm bleven staan.

**Volgende run, in deze orde:**
1. **START blijven herhalen** (elke 5 s) tot `FileTest Reported ... at startup!` in het logboek staat.
   Dat is de voorwaarde om uberhaupt op het kalibratiescherm te komen.
2. Pas dan de TEST-pogingen opnieuw: k=3, k=4, k=5 met een foto per poging, en daarna herhaald TEST.
3. En als het scherm dan nog niet verspringt, is de conclusie dat dit scherm de **trekker** wil -- en
   dan is de Move nodig (die de eigenaar moet aansluiten). Dat is geen teleurstelling maar de kern van
   deze lader: de eigenaar speelt deze games met een Move als geweer, en de vraag of wij die trekker
   kunnen sturen is precies de vraag die dit project moet beantwoorden.

## 6. Bewaard

* `patches/Halo/run6-poging1-k3.png`, `run6-poging1-k4.png`, `run6-poging1-k5.png`,
  `run6-poging2-eind.png` -- het scherm van deze run.
* `patches/Halo/gamelog-run6.txt`, `patches/Halo/usb-tap-run6.log`.
* `patches/Halo/probe/run6_uitvoeren.sh` -- met de tafellogging en de buitenlezer erin.
* De invoerhulp met tafellogging: `build/librg_rt_input.so`, sha256 `2640546331b3dd46`.
