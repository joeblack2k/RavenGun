# De 5-minutenproef voor Luigi's Mansion op ONZE lader

RavenGun, 2026-10-12. Dit is de meetlat die de eigenaar heeft gezet: **onze eigen lader start
Luigi's Mansion en het spel draait 5 MINUTEN.** Stand nu: **288 s met de GESLOTEN kern, 6,3 s met
onze kern.**

Bestanden: `b0-vijfmin.sh` (de proef), `b0-vijfmin-sampler.py` (de bemonstering, root),
`b0-momenten.sh` / `b0-momenten.py` (de korte tijdlijn, voor de kernverificatie).

## Draaiboek, en wat je NIET nodig hebt

* **De PS Move hoeft NIET aan te staan.** De lader heeft een ingebouwde muismodus, dus er is geen
  Move-hardware nodig en er hoeven geen knoppen te worden ingedrukt. Deze proef gaat over het
  **draaien** van het spel, niet over de invoer. **Een run zonder Move is dus geldig** -- denk later
  niet dat de proef mislukt is omdat er geen Move aan hing. Invoer is een aparte vraag en die staat
  al: onze daemon levert de bytes van `shared-luigi-mansion`, en `tp-shm-swap.sh` maakt hem de enige
  schrijver.
* **Wat de blokkade precies is** (aanwijzing van de bouwer, en het maakt de verificatie gericht): de
  entry-point-trampoline in de lader is 69 bytes en doet letterlijk `call InitializeASI`, en **pas
  daarna** zet hij de originele **20** entry-point-bytes terug en springt naar het echte entry point.
  De hele blokkade is dus: **onze `InitializeASI` keert niet terug.** Onze kern sterft op 6,3 s met
  de trampoline nog op zijn plaats.
* Daarom vergelijkt de bemonstering **20 bytes** met het bestand en niet alleen de eerste twee: dat
  is het harde bewijs dat de trampoline is afgelopen, en niet dat de eerste bytes toevallig op
  `eb 08` lijken.

## Draaien

```bash
# onze huidige kern
bash b0-vijfmin.sh

# de nieuwe kern van de laderbouwer (zodra die er is)
RG_B0_KERN_DLL=/home/deck/.local/share/ravengun/b0/core-nieuw/OpenParrotx64/OpenParrot64.dll \
  bash b0-vijfmin.sh

# kort debuggen zonder systemd-unit
RG_B0_NO_SYSTEMD=1 RG_B0_STOP=60 RG_B0_HARD=120 bash b0-vijfmin.sh
```

Het script zet zichzelf in een **systemd-user-unit** (overleeft een ssh-sessie) en zegt hoe je hem
volgt: `journalctl --user -u <unit> -f`. Uitvoer staat in `/tmp/b0-vijfmin-<datum>/`.

## Wat het doet, en waarom zo

* **De gewone werkende route**: `launch-direct.py LuigisMansion --run`. Die zet prefix, werkmap van
  de lader, hulpketen (`rg-jvsbridge`, `TP_LAUNCH_SESSION_ID`) en omgeving goed. Zelf wine aanroepen
  is eerder fout gegaan (exitcode 5, zie `B0-LEGE-DLL.md` Â§4).
* **Headless gamescope**, zodat het scherm van de eigenaar vrij blijft.
* **De kern-DLL wordt verwisseld en gegarandeerd teruggezet** (trap), en daarna gecontroleerd op
  `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`.
* **Per run de kern-sha256 zoals launch-direct hem ZELF in zijn log schrijft**; wijkt die af van wat
  wij bedoelden, dan heet de run ONGELDIG.
* **Elke 30 s game-tijd** (vanaf het eerste verschijnen van het spelproces): game-tijd, leeft het,
  threads, **entry-point-staat** (trampoline `48 B8 <imm64> FF E0` of teruggezet `eb 08`),
  **`.text`-vulling**, en **hoeveel bestanden van het spel openstaan**.
* **Eigen-bestand-meting via `/proc/<pid>/fd`**, bewust NIET via `WINEDEBUG=+file`: die trace
  vertraagt het proces en verandert de levensduur (5-7 s met `-all` tegen 25-30 s met `+file`). Open
  file descriptors naar de `Data`-mappen meten hetzelfde zonder de timing te beinvloeden.
* **Harde limiet 480 s, stopgrens 300 s game-tijd.** Daarna: procesgroep `kill -9`, resten wegvegen.
* **NAS-schrijfcontrole** over het hele venster: een `find -newermt` plus hashes van de bekende
  schrijfdoelen (`exe/x64/teknoparrot.ini`, `Log/play_log.csv`, `Log/play_number.dat`,
  `Ram/CreditConfig.bin`), voor en na.

## Succescriteria, vooraf opgeschreven

| # | criterium | waar het vandaan komt |
|---|---|---|
| 1 | **300 s game-tijd** gehaald, proces nog levend op het stopmoment | de lat van de eigenaar |
| 2 | **entry point teruggezet** (`eb 08`), niet de trampoline | de gesloten kern haalt dit tussen 8 en 10 s; onze kern niet voor hij sterft |
| 3 | **`.text` gevuld** (ruim onder 100 % nul) | gesloten kern: 11,2 % nul op 10 s |
| 4 | **threads ruim boven 1** | gesloten kern: 1 -> 15 op 10 s -> 21 op 12 s; onze kern blijft 1 |
| 5 | **het spel leest zijn eigen databestanden** | gesloten kern: 393-762 eigen paden |
| 6 | **geen onverwachte schrijfactie naar de NAS** | de view is een symlink naar de NAS; onze kern schreef daar op 10:54 `teknoparrot.ini` |

## Met een per-game drop-in erbij

De afspraak in dit project: een game-specifieke patch staat in `patches/<Game>/dropin/` met een
`dropin.ini`, **niet** in de lader (die blijft schoon). De launcher zet die bestanden in de view en
zet de `Env`-sleutels in de omgeving van de game. Voor Luigi bestaat die map nog **niet** -- de route
is dus open, en er is niets dat hem onmogelijk maakt.

Zo draai je de proef met een drop-in:

```bash
RG_B0_DROPIN=$HOME/projects/ravengun/patches/LuigisMansion/dropin bash b0-vijfmin.sh
```

Sleutels per sectie in `dropin.ini` (zie `docs/dropins.md` en `patches/_template/dropin/dropin.ini`):
`To` (doelmap, `.` = de map van de exe), `DllOverride` (`native` -> `<naam>=n`), `Env`
(puntkomma-gescheiden), `Preload` (moet in het proces geladen worden), `Audit` (`false` = niet in
`LD_AUDIT` van de Wine-start). **Elk bestand heeft een eigen sectie nodig**, en de sectienaam is de
bestandsnaam met het pad erin -- een bestand zonder sectie wordt niet gekopieerd. Dat is de fout die
het vaakst misgaat.

**De proef controleert de drop-in op drie manieren, en zonder die controle is de meting niets waard:**

1. **Vooraf, uit het plan**: `launch-direct.py --plan --json` moet de bestanden noemen. Geeft het plan
   0 bestanden terwijl jij een drop-in opgaf, dan **weigert de proef te starten** -- dan wordt er
   niets gekopieerd en zou je een lege meting doen.
2. **Na de run, in de view**: elk doelbestand bestaat en zijn sha256 is gelijk aan de bron.
3. **Tijdens de run, in het PROCES**: elk verwacht bestand moet in `/proc/<pid>/maps` van het
   spelproces staan. Dat is het verschil tussen "het staat in de view" en "het is geladen" -- en dat
   is precies de fout die bij de view-DLL van Rabbids is gemaakt.

De sampler print per monster `drop-in in proces N/M`. **N blijft 0 terwijl M groter is, dan is de
drop-in niet geladen en is de run ongeldig** -- ongeacht wat het spel verder doet.

## Succescriteria met een drop-in erbij

Bovenop de zes criteria hierboven komt:

| # | criterium | waar het vandaan komt |
|---|---|---|
| 7 | het plan noemt de drop-in-bestanden (anders start de proef niet) | `launch-direct.py --plan --json` |
| 8 | elk doelbestand staat in de view met dezelfde sha256 als de bron | na de run |
| 9 | elk verwacht drop-in-bestand staat in `/proc/<pid>/maps` van het spelproces | per monster: `drop-in in proces N/M` met N == M |

## Faalwijzen, elk met een eigen betekenis

| wat je ziet | wat het betekent |
|---|---|
| sterft ruim voor 300 s met de **trampoline nog op het entry point** en `.text` nog 100 % nul | de aanhechtingsfase loopt niet af: `InitializeASI` van onze kern komt niet terug. Dit is de faalwijze van 6,3 s, en de reparatie zit in de kern, niet in deze repo. |
| sterft **nadat** het entry point is teruggezet, met `.text` gevuld en threads > 1 | de aanhechting lukt nu, en het spel sterft later aan iets anders. Dan is de volgende vraag wat er NA de pakker misgaat, en dat is nieuw terrein. |
| leeft, maar **threads blijven 1** en `.text` blijft nul | het proces hangt in de trampoline-fase zonder te sterven. Andere faalwijze dan sterven: kijk naar een blokkade in `InitializeASI`. |
| leeft en `.text` is gevuld, maar **geen eigen databestanden** | de pakker loopt, maar het spel komt niet aan zijn data. Dan is de volgende vraag het bestandspad of de werkmap, niet de kern. |
| **de run is ONGELDIG** (kern-sha256 uit launch-direct's log wijkt af) | er is niets gemeten. Opnieuw, en eerst uitzoeken waarom de verwisseling niet aankwam. |
| **NAS-verschil** in de bekende doelen | het spel of de lader heeft op de NAS geschreven. Dat is toegestaan voor wat het spel zelf al deed (`Ram/`, `Log/`), maar `exe/x64/teknoparrot.ini` door ONZE kern is een pad dat dicht moet. |

## BASELINE 2026-10-13: de GESLOTEN kern met ONZE lader haalt 5 minuten

**Dit is een BASELINE en NIET het halen van de lat.** De lat is ONZE eigen kern. Deze run zegt alleen
dat de OMGEVING 5 minuten toelaat -- dat er geen tweede probleem is (geen time-out, geen resourcelek,
geen zelf-stop na een tijdje). Uitgevoerd met
`RG_B0_KERN_DLL=<gesloten TeknoParrot64.dll> RG_B0_STOP=300 RG_B0_HARD=480 bash b0-vijfmin.sh`.
Ruwe uitvoer: `/tmp/b0-baseline-gesloten/`.

| t (game-tijd) | leeft | threads | entry point | `.text` | drop-in |
|---|---|---|---|---|---|
| 0,2 | ja | 1 | teruggezet, 20 bytes identiek aan bestand | 100 % nul | 0/0 |
| 30 | ja | 21 | teruggezet | 11,2 % nul | 0/0 |
| 60 | ja | 89 | teruggezet | 11,2 % | 0/0 |
| 90-270 | ja | 88-92 | teruggezet | 11,2 % | 0/0 |
| **300** | **ja** | 92 | teruggezet | 11,2 % | 0/0 |

`UITKOMST: STOPGRENS gehaald op 300.0 s game-tijd, proces leefde nog`. Kern volgens launch-direct's
eigen log: `f5c7532d...` = de bedoelde. Threads vlakken af op ongeveer 90 en blijven vlak.
NAS over het venster: alleen `Ram/CreditConfig.bin`, `Ram/EEPROM_0x00001100.bin` en
`Ram/SRAM_0x00004200.bin` -- de eigen EEPROM/SRAM/credit-schrijfactoren van het spel, de geaccepteerde
uitzondering. Geen `teknoparrot.ini`, geen `Log/play_log.csv`.

**Conclusie: de enige blokkade voor de lat is onze eigen kern.**

### Twee lessen over het instrument, uit deze run

1. **"Leest zijn eigen databestanden" is met `/proc/<pid>/fd` een VALSE NEGATIEF voor deze game.**
   De run gaf `eigen bestanden open 0` op elk monster, terwijl de `+file`-trace eerder 393-762 eigen
   datapaden liet zien. Het spel opent, leest en SLUIT zijn bestanden meteen, dus op een cadans van
   30 s vangt de fd-telling er bijna nooit een. Criterium 5 is daarom uit deze proef gehaald en
   wordt apart gemeten met `bash b0-run.sh a` (met de eerlijke opmerking dat `+file` het proces
   vertraagt en de levensduur verandert). **Lees een 0 hier nooit als "het spel leest niets".**
2. **De criteriumregels printten eerst grepentellingen in plaats van een oordeel** ("1. 300 s
   game-tijd gehaald : 2"). Dat is nu per criterium JA/NEE met de gemeten waarde ernaast. De
   levenslijntabel blijft de drager van het bewijs.

### En de drop-in-regel die hieruit volgt

Als er een drop-in is opgegeven en die staat NIET in `/proc/<pid>/maps` van het spelproces, dan is de
run **ONGELDIG** -- ook als het spel 5 minuten leeft. Een spel dat 5 minuten leeft ZONDER de drop-in
betekent dat de drop-in niet nodig was, en dat is een eigen uitkomst die gemeld moet worden in plaats
van als succes door te gaan. De proef zegt dat nu met zoveel woorden in het oordeel.

## TUSSENSTAND drop-in 2026-10-13: twee ongeldige runs, de route is nog niet te meten

Beide runs met ONZE kern (`fe2ecf8c`) en de drop-in
(`patches/LuigisMansion/dropin/`: `rg-pe-hook.dll` 127.485 B, `dongle-haken.ini`, `dropin.ini` met
`Preload = true`). Ruwe uitvoer: `/tmp/b0-dropin1/` en `/tmp/b0-dropin2/`.

| | run 1 | run 2 (na de view-fix) |
|---|---|---|
| 1. in het plan | 2 bestanden, `dest` correct, geen fouten | idem |
| 2. in de view, sha256 | **ONTBREEKT** | **OK**, `bron==doel: True` voor beide |
| 3. in `/proc/<pid>/maps` | 0/2 | **0/2** |
| 4. logboek van de drop-in | geen | **geen** |
| spel | PROCES WEG op 5,0 s | PROCES WEG op 4,8 s |
| oordeel | ONGELDIG | **ONGELDIG** |

**Run 1: de bestanden kwamen niet in de view.** launch-direct's eigen log zegt waarom:

```
drop-in: LET OP: de map van het doel .../views/LuigisMansion/exe/x64/rg-pe-hook.dll komt na het
         materialiseren nog buiten de view uit
```

De oorzaak: `views/LuigisMansion/exe` is een **symlink naar de NAS**, en `dropin_materialiseer`
ontpakt wel symlink-mappen ONDER de basis, maar niet de basis zelf -- en die basis is hier
`.../exe/x64`. De map van het doel resolveert dus naar de NAS en de NAS-bescherming weigert. Dit is
een fout in de drop-in-route, niet in de drop-in.

**Run 2: de bestanden staan er wel, maar de DLL komt niet in het proces.** Na het ontpakken van
`views/LuigisMansion/exe` en `exe/x64` (dezelfde bewerking die de materialiseerder voor submappen
doet) schrijft launch-direct beide bestanden weg en kloppen de sha256's. Maar `MAX drop-in in proces
0/2` en er is **geen `rg-pe-hook.log`** -- en dat logboek is het directe bewijs, want de hook schrijft
altijd eerst een PE-regel.

**De wiring-fout, tot op de regel:** launch-direct zet `RG_DROPIN_PRELOAD=<view>/rg-pe-hook.dll`, maar
de lader leest **`TP_PRELOAD_DLL`** (`OpenParrotLoader/src/main.cpp` laadt die DLL na de kern en voor
het spel verdergaat). Er is niets dat het ene in het andere omzet, dus voor een `Type = TeknoParrot`-
game onder Wine bereikt de preload de lader nooit. **Dat is werk voor de laderbouwer** (die zet de
omzetting erin, laat `--plan` zien in welke variabele een preload belandt, en laat een doel buiten de
view LUID falen in plaats van stil door te gaan).

**Wat dit betekent:** de drop-in-route is nog **niet te meten**, en elke conclusie over het spel zou
voorbarig zijn. Het spel stierf in beide runs rond 5 s, en dat is **verwacht**: de drop-in was niet
geladen, dus er was geen enkele reden dat het anders zou gaan. Deze twee runs zeggen dus niets over de
am-laag -- ze zeggen alleen dat de bedrading nog niet klopt.

**En de view-wijziging moet blijven:** `views/LuigisMansion/exe` en `exe/x64` zijn nu echte mappen met
symlinks in plaats van een symlink naar de NAS. Dat is nodig voor elke drop-in met `To = .` op deze
game, en het is dezelfde bewerking die `dropin_materialiseer` voor submappen doet. Het is gevraagd om
dit in `scripts/maak-views.sh` reproduceerbaar te maken, zodat een volgende view-opbouw het niet
ongedaan maakt.

## Klaar voor de derde run

Zodra de launcher `RG_DROPIN_PRELOAD` omzet naar wat de lader leest, is de derde run de beslissende
meting van game 1. De proef controleert dan **vier** dingen, en een run zonder het logboek is
ongeldig:

1. het plan noemt de bestanden (anders start de proef niet);
2. de bestanden staan in de view met dezelfde sha256 als de bron;
3. de bestanden staan in `/proc/<pid>/maps` van het SPELPROCES (`MAX drop-in in proces N/M`);
4. **het logboek dat de drop-in zelf schrijft staat in de werkmap van het spel**, met de PE-regel
   (ImageBase, SizeOfImage) en de samenvatting `totaal aangehaakt: N van 17; afgekeurd: M`. De proef
   leest dat logboek nu zelf en print de eerste regels en de samenvatting.

**Lezen van de uitkomst:** alles boven ongeveer 10 s is een treffer (onze kern haalt nu 5,96 s, de
gesloten kern 300 s). Leeft het spel langer EN staat de DLL in het proces, dan was de am-laag de
oorzaak. Sterft het met de DLL geladen nog rond 6 s, dan is de am-laag **niet** de oorzaak en gaat het
werk terug naar de pakker-fase -- een even goede uitkomst. Leeft het spel zonder geladen drop-in, dan
was de drop-in niet nodig, en dat wordt als eigen uitkomst gemeld en niet als succes.

## Wat de derde run nog meer moet opleveren: de 17 originele prologues

De heeftphook-specialist wil de **originele 5 prologue-bytes van elk van de 17 am-functies**. Die zijn
meetbaar zodra het spel zich uitgepakt heeft (`.text` niet meer volledig nul). Daarmee kan elke regel
in `dongle-haken.ini` een **verwachte waarde** krijgen, en dan weigert de hook een verkeerde spelversie
in plaats van te vertrouwen op "het lijkt een prologue". Zonder die bytes zou een andere build van het
spel stil verkeerd gepatcht kunnen worden.

De sampler leest die 5 bytes nu op elk monster en zet de **laatste** meting in de uitvoer als tabel:

```
ORIGINELE 5 PROLOGUE-BYTES OP DE 17 AM-ADRESSEN (laatste meting voor het einde):
  0x1402b3b70  40 53 48 83 ec
  ...
```

De adressen komen uit `patches/LuigisMansion/dongle-haken.ini` en `AM-LAAG.md`. **De uitkomst hoort in
dit verslag te belanden** als tabel (adres -> 5 bytes), zodat de specialist ze kan overnemen.

**En de bemonstering is nu dicht in de eerste seconden** (elke 0,5 s tot 10 s, daarna 30 s). Dat is een
reparatie van een echt gat: onze kern sterft rond 5-6 s, en met een cadans van 30 s stond er in de
eerste drop-in-runs alleen `t=0,2` en dan `PROCES WEG` -- een spel dat op 6 s sterft werd dus nooit
fatsoenlijk bemonsterd. Met de dichte cadans levert de derde run in ELK van de twee uitkomsten iets op:
sterft het rond 6 s, dan zien we de laatste toestand seconde voor seconde; leeft het, dan zien we de
eerste 10 s fijnmazig en daarna elke 30 s.

**En het beslissende controlepunt blijft:** staat `rg-pe-hook.dll` in `/proc/<pid>/maps` van het
SPELPROCES, en staat `rg-pe-hook.log` in de werkmap met de eerste PE-regel en de samenvatting
`totaal aangehaakt: N van 17; afgekeurd: M`. **Zonder dat logboek is de run ongeldig, ook als het spel
5 minuten leeft.**

## De kernverificatie (onafhankelijk van de laderbouwer)

De laderbouwer instrumenteert en repareert nu `InitializeASI`. Zodra zijn nieuwe kern er is, draait
de onafhankelijke controle met het gereedschap dat er al staat:

```bash
RG_B0_KERN_DLL=<nieuwe kern> RG_B0_UIT=/tmp/b0-mom-nieuw bash b0-momenten.sh
```

Dat geeft dezelfde tijdlijn als in `B0-LEGE-DLL.md` Â§9b (entry-point-staat, `.text`-vulling, threads
op 2/3/3,5/4/5/6/8/10/12 s), maar dan voor zijn kern. **Het bewijs dat zijn reparatie iets doet is
dat de trampoline wordt TERUGGEZET en dat `.text` daarna volloopt** -- niet zijn eigen instrumentatie.
Daarna pas de 5-minutenproef.

