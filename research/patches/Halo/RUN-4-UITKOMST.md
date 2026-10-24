# RUN 4 voor Halo — de uitkomst (met twee harde vondsten)

*RavenGun, 2026-10-13. Uitgevoerd **10:28:42 - 10:30:48** (150 s, beeld op de tv, exitcode 0). De
eigenaar keek mee. Opgeruimd: geen game, geen launcher, geen gamescope. Geen commit.*

Deze run was bedoeld als herhaling van run 3 plus twee TEST-pulsen. Hij is **geen schone herhaling
geworden**: de START-indruk werd deze keer niet aangenomen, en de tweede TEST-puls viel samen met een
fatale crash **in ons eigen invoerhulpmiddel**. Beide zijn concrete vondsten en ze wijzen dezelfde kant
op: de pulsen zijn te kort en te breed.

## 1. WAT ER OP HET SCHERM STOND (het extra instrument van deze run)

De foto op **t=80 s** toont het scherm van de game, en dat is het **FileTest-scherm**:

```
Welcome to Halo v03.11
Initializing systems, Please Be Patient.
Performing Integrity Test.
Tested:  2232
Failed:  1
Testing: /pm/g7/halo/data/g7/scarab_inside.g7
Press START or Ding to skip File Test.
```

Wat dat in gewone woorden zegt:

* de FileTest is een **scanner** die de bestandskaarten afloopt en zijn voortgang toont: **2232 bestanden
  getest, 1 mislukt**, en hij was bezig met `/pm/g7/halo/data/g7/scarab_inside.g7`;
* **"Press START or Ding to skip File Test"** staat er letterlijk: de game vraagt zelf om die twee
  schakelaars om de scan over te slaan;
* en op **t=80 s stond dat scherm er nog**, terwijl ik om t=20 s START had gegeven. De indruk is dus
  **niet aangenomen**;
* de foto's op t=15 en t=40 zijn vrijwel egaal (zwart) -- dat is hetzelfde scherm in de eerste
  seconden, toen de tekst nog niet geschreven was. De foto op t=115 (na de crash) is iets voller: het
  scherm na de dood.

**Vergelijk dat met run 3**: daar ging de game om 10:26:05, in dezelfde seconde als mijn START-indruk,
verder met `FileTest Reported 1 errd files at startup!` en stond op t=70 s het
**kalibratiescherm** ("Player 1 gun (left): Pull gun all the way back and towards the left, then press
the trigger. ... Press the TEST button to skip calibration for this gun."). Dezelfde knop, hetzelfde
moment, **andere uitkomst**: in run 3 werd de indruk aangenomen, in run 4 niet.

## 2. VONDST 1: de puls is te kort (en run 3 had geluk)

De FileTest **scant duizenden bestanden**. Als de game tijdens die scan maar af en toe naar de
schakelaartabel kijkt, dan hangt het van het toeval af of een puls van **300 ms** precies in zo'n
moment valt. In run 3 viel hij goed, in run 4 niet -- bij dezelfde timing (t=20 s) en dezelfde
werkwijze. Dat is precies wat je verwacht van een marginale puls, en het verklaart waarom run 3
"werkte": geluk, geen zekerheid.

**De knop om dat te verhelpen bestaat al en is gedocumenteerd**: `RG_RT_PULSE_MS` overschrijft de
pulsduur van de invoerhulp (kop van `tools/rt-input-shim/rg_rt_input.c`: *"een indruk wordt daarom als
korte puls gezet (per schakelaar 14 tot 300 ms, `RG_RT_PULSE_MS` overschrijft)"*). De volgende run
dus met een **lange puls** (bijvoorbeeld 2000 ms) en/of de indruk **een paar keer herhaald** -- geen
code, alleen een omgevingsvariabele.

## 3. VONDST 2: de tweede TEST-puls valt samen met een crash IN ONZE EIGEN HULP

Om **10:30:08**, exact op het moment van de tweede TEST-puls, sterft de game met een fatale SIGBUS en
de stack trace noemt **onze eigen bibliotheek**:

```
Tue Oct 13 10:30:08 2026:  Received FATAL signal SIGBUS (7)
Tue Oct 13 10:30:08 2026: si_signo: 7    si_code: SEGV_ACCERR   si_errno: 0
Tue Oct 13 10:30:08 2026: si_addr:   0x7f035b03c008
Tue Oct 13 10:30:08 2026: ############ STACK TRACE ############
Tue Oct 13 10:30:08 2026: /rg/lib/librg_rt_input.so(+0x1f44) [0x7f035b089f44]
Tue Oct 13 10:30:08 2026: /usr/lib/libc.so.6(+0x3e270) [0x7f035943e270]
Tue Oct 13 10:30:08 2026: /rg/lib/librg_rt_input.so(+0x1f44) [0x7f035b089f44]
Tue Oct 13 10:30:08 2026: /usr/lib/libc.so.6(+0x97739) [0x7f0359497739]
```

Wat daar **zeker** aan is: de crash valt exact op de tweede TEST-puls, het foutende **code-adres** zit
in `librg_rt_input.so` (`+0x1f44`, twee keer in de trace), en `si_code: SEGV_ACCERR` betekent een
**toegangsfout op een mapping** -- schrijven naar iets dat niet schrijfbaar is, of buiten een mapping.
Het foutende **adres** (`0x7f035b03c008`) ligt **niet** in het spelbeeld (dat staat op `0x140000000`),
dus het is een andere mapping.

Wat ik daar **niet** uit mag concluderen: welke mapping dat is, en of het de schakelaartabel, het
schakelaarbeeld of iets anders is. Wat ik wel weet en wat een concrete verdenking oplevert:

* de invoerhulp zet voor **TEST** de schakelaar op **vier borden tegelijk** (haar eigen meldingen:
  `indruk bit 0 -> bord 0 schakelaar 4`, en dan bord 1, 2 en 3);
* deze kast is een **tweekopper**-opstelling (de resolutiecodes 4, 3 en 8 uit `run_game.sh` zijn alle
  twee-kopper-kasten), dus de tafels voor bord 2 en 3 bestaan hier mogelijk niet;
* schrijven in tafels die er niet zijn, is precies het soort dingen dat even later als SIGBUS
  terugkomt.

**En ook hier bestaat de knop al**: de hulp heeft een onderzoekspad `RG_RT_PROBE=<bestand>` met inhoud
`bit,bord,k`, dat **één** schakelaar zet in plaats van vier. Daarmee is de verdenking te toetsen
zonder ook maar iets aan de hulp te veranderen: TEST alleen op **bord 0**.

## 4. De uitkomst in het schema

* De tap: **8 x `tap start`** en verder niets -- **geen** attribuutregels, **geen** descriptors, **geen**
  `APPARAAT`, **geen** bord. Dus nog steeds uitkomst **(iii)**: de game is niet bij zijn FTDI-pad
  gekomen. In deze run was dat ook te verwachten: hij is nooit voorbij het FileTest-scherm geraakt.
* **Geval B** uit het draaiboek is van toepassing op run 3 (de indruk werkte, de tap bleef leeg); run 4
  is eerder een **geval C-achtige toestand**: de indruk kwam aan bij de hulp maar niet bij de game, en
  toen ging het mis in de hulp zelf. Dat moet ik zo eerlijk mogelijk zeggen: **run 4 is geen geldige
  herhaling van run 3** en levert dus geen nieuwe uitspraak over het FTDI-pad.
* De **levenscheck** werkte: `game-pid` met 25 threads en 10 libfmod-mappings tijdens de run (in run 3),
  en in run 4 het eigen logboek als autoriteit -- dat is de methode die blijft.

## 5. De tijdstempels, en de vergelijking met run 2 (mijn eigen meetsetup)

| run | beeld | start | eerste nieuwe regel | verschil |
|---|---|---|---|---|
| 2 | headless | 10:04:20 | 10:04:31 | **11 s** |
| 3 | tv | 10:25:44 | 10:25:45 | **1 s** |
| 4 | tv | 10:28:42 | 10:28:43 | **1 s** |

Mijn vooraf opgeschreven verwachting was "het FileTest-tijdstip blijft vrijwel gelijk". Dat klopt
**niet**: met beeld op de tv komt de eerste regel na **1 s**, headless na **11 s** -- een verschil van
tien seconden, en het is **reproduceerbaar** (run 3 en 4 geven allebei 1 s). Zoals ik vooraf heb
vastgelegd is dat dus **een bevinding over onze meetsetup en geen ruis**: de beeldbackend van gamescope
(of iets wat daarmee samenhangt) beïnvloedt de opstarttijd van de game meetbaar. Wie runs met en
zonder beeld vergelijkt, moet dat meenemen.

**Wat ik nog niet weet** is *welke* van de twee kandidaten het is: de gamescope-backend zelf, of de
bestandscache (run 2 was de eerste run na lange tijd, run 3 en 4 kort na elkaar, dus de bestanden
stonden warm). Dat is te scheiden met één run: **headless, direct na een tv-run** (dan is de cache warm
en zie je of headless alsnog 1 s geeft). Dat is een goedkope meting en ik stel hem voor.

## 6. Wat run 4 expliciet NIET oplevert (vooraf opgeschreven, en het klopt)

* **Niets over het FTDI-protocol**: de tap zag geen enkele opsomming, dus geen baud, geen berichten,
  geen CRC, en zelfs niet of er een bord zou worden gevonden.
* **Niets over aim of trekker**: die zijn in deze opstelling niet aangeraakt.
* **Geen uitspraak over de kalibratie**: de game is er in deze run niet eens aan toegekomen.
* **Geen geldige herhaling van run 3**: de START-indruk werd niet aangenomen, dus het verschil tussen
  de twee runs is zelf een bevinding (vondst 1) en geen bevestiging.

## 7. De twee dingen die de volgende run anders moet doen (beide zonder code)

1. **Een lange puls**: `RG_RT_PULSE_MS=2000` (of 3000) voor START, en de indruk desnoods twee keer met
   tussenruimte. Doel: het toeval uitschakelen dat run 3 deed slagen en run 4 niet.
2. **TEST alleen op bord 0**, via `RG_RT_PROBE=<bestand>` met `0,0,4` (bit 0, bord 0, schakelaar 4) --
   dus niet de vier-borden-indruk die met de crash samenvalt. Doel: toetsen of de crash door de
   schakelaars van bord 2 en 3 komt.

En wat er hoe dan ook uit moet blijven: de tap blijft het instrument, en zolang er geen apparaatlijst
langskomt is er over de FTDI-poort niets gezegd.

## 8. Bewaard

* `patches/Halo/run4-t15-voor.png`, `run4-t40-na-start.png`, `run4-t80-na-test1.png`,
  `run4-t115-na-test2.png` -- de schermafbeeldingen van deze run (het FileTest-scherm staat op
  `t80`).
* `patches/Halo/gamelog-run4.txt` -- het eigen logboek van de game, met de crash.
* `patches/Halo/usb-tap-run4.log` -- 8 x `tap start` en verder niets.
* `patches/Halo/probe/run4_uitvoeren.sh` -- het script van deze run, zodat hij herhaalbaar is.
