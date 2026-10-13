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

## DERDE STAND drop-in 2026-10-13: de DLL is geladen, maar het wachten op de hoofdthread blokkeert het ontpakken

Run met ONZE kern (`fe2ecf8c`), de nieuwe drop-in (`rg-pe-hook.dll`, 128.880 B,
sha256 `305b99cca3b696d1330139deb3e13ea53273b5ef9bbb02ee692175eec2cb3bd4`).
Ruwe uitvoer: `/tmp/b0-dropin3/`.

**De drie controles die slaagden:**

1. het plan noemt 2 bestanden en zet de preload op `TP_PRELOAD_DLL=Z:\...\rg-pe-hook.dll` -- de
   launcher-wiring werkt;
2. de sha256 in de view is `305b99cca3b696d1...`, dus de NIEUWE build met de wachtroute, niet de oude;
3. **het logboek van de DLL zelf is er** (5565 B), en dat is het bewijs dat de DLL echt geladen is.

**De vierde controle was door MIJN meetfout onterecht rood.** Mijn proef eiste dat ELK drop-in-bestand
in `/proc/<pid>/maps` staat, maar `dongle-haken.ini` is een GEGEVENSBESTAND dat de DLL leest en dat
nooit gemapt wordt. De proef meldde dus `1 van 2` en "ONGELDIG", terwijl de DLL wel geladen was -- precies
de verkeerde conclusie die vermeden moest worden. **Gerepareerd:** de proef telt nu alleen de bestanden
die bedoeld zijn om geladen te worden (de `Preload`- en `DllOverride`-regels uit de planning), en
gegevensbestanden worden apart behandeld (bestaan controleren; het logboek noemt de adreslijst en het
pad en is het bewijs dat ze gelezen zijn).

**De echte uitkomst, met de levenslijnen:**

| t | entry point | `.text` | drop-in in proces |
|---|---|---|---|
| 0,2-2,0 | teruggezet / `EB FE` | 100 % nul | 0/1 |
| 2,5-10,0 | **TRAMPOLINE -> 0x6ffffd36e410** | **100 % nul** | 1/1 |
| **16,2** | PROCES WEG | -- | -- |

Het logboek zegt precies wat er gebeurde:

```
[rg-pe-hook] wachtroute: 17 pagina('s) nog niet uitgepakt; ik blijf kijken (max 10 s, elke 20 ms)
[rg-pe-hook] wachtroute: 17 pagina('s) na 10 s nog steeds niet uitgepakt; gestopt
[rg-pe-hook] totaal aangehaakt: 0 van 17; afgekeurd: 17; waarvan antwoord TODO: 0
```

Alle 17 zijn afgekeurd met "de prologue op RVA 0x... is nog nul". **` .text` is het HELE leven 100 % nul:
het spel heeft zich nooit uitgepakt.** Het entry point staat van 2,5 s tot 10 s op de trampoline van de
lader. De DLL wordt via `TP_PRELOAD_DLL` geladen **op de hoofdthread, in de aanhechtingsfase, voordat het
entry point van het spel loopt** -- en wacht daar 10 s op pagina's die pas gevuld kunnen worden NADAT de
lader de hoofdthread hervat, en die hervat pas als de DLL terugkeert. **Het wachten op de hoofdthread is
dus een deadlock by construction.**

De langere levensduur is precies dat wachten en niets anders: **16,2 s = 10 s wachten + dezelfde ~6 s**
(onze kern stierf eerder op 4,8-5,96 s). Er is dus niets opgelost; het proces werd alleen 10 s in de
aanhechtingsfase vastgehouden.

**Gevolg:** de am-laag is in deze run **niet** getest -- 0 van 17 aangehaakt. Deze run zegt dus niets over
de vraag of die laag de oorzaak is. Wat hij wel vaststelt: de drop-in-route werkt nu van begin tot eind
(plan -> view -> proces -> logboek), en het wachten moet naar een EIGEN thread, zodat de DLL meteen
terugkeert en de lader de hoofdthread kan hervatten. Dat is aan de heeftphook-specialist.

**En de 17 prologue-bytes konden NIET gelezen worden:** de tabel is helemaal nul, correct gemarkeerd als
"nog allemaal nul: op dit moment niet ontpakt". Ze staan op de lijst voor de volgende run, waarin het
wachten op een eigen thread staat en `.text` dus wel gevuld wordt.

## BESLECHTING van de discrepantie 2026-10-13: mijn lezing was niet kort, de twee instrumenten zijn het oneens

De verdenking was dat mijn `.text`-lezing kort terugkwam en dus "100 % nul over een klein stuk" printte.
**Die verdenking is weerlegd:** de telling staat nu in de uitvoer en is op ELK monster
`3496500 van 3496500 B gelezen`. Mijn lezing leest de hele sectie.

Maar daarmee is het meningsverschil niet weg, het is scherper:

| moment | mijn lezing via `/proc/<pid>/mem` | het logboek van de hook, in hetzelfde proces |
|---|---|---|
| t=0,2-5,5 | `.text` 100,0 % nul, volledig gelezen; **alle 17 adressen `00 00 00 00 00`** | `wachtroute: de pagina's zijn uitgepakt na 2421 ms` (~t=3,5 s), en **echte prologues gelezen**: `40 53 48 83`, `48 83 ec 28`, `40 53 48 81`, `48 83 ec 38` |

Beide instrumenten keken naar dezelfde adressen (RVA 0x2b3b70 enz., dus VA 0x1402b3b70) in hetzelfde
proces, op hetzelfde moment, en ze zijn het oneens. Mijn lezer is aantoonbaar levend: op hetzelfde pid
zag ik het entry point wel veranderen (`EB FE` op 1,0 s, TRAMPOLINE op 2,5 s), dus ik lees het juiste
proces. De hook leest echte code.

**Wat elk instrument wel en niet kan:**

| | mijn sampler (`/proc/<pid>/mem`) | het logboek van de hook (in-process) |
|---|---|---|
| kan | de hele sectie in een keer lezen, met byte-telling, zonder het spel aan te raken; entry point, threads, open bestanden | de bytes lezen zoals het spel ze zelf ziet, op het moment dat hij ze nodig heeft |
| kan niet | zeggen WELKE mapping of welk adres het spel zelf gebruikt; als de pakker naar een andere mapping schrijft dan de nominale VA, ziet mijn lezing nullen waar het spel code ziet | geen onafhankelijke controle: hij leest met zijn eigen adresberekening (`beeld=pe, proloog=standaard, basis=beeld`), dus een fout in die berekening is niet te zien |

**Twee kandidaten, en deze run kan ze niet scheiden:**

1. de pakker schrijft de ontpakte code naar een ANDERE mapping dan `ImageBase + sectie-VA`, en de hook
   leest die (via zijn eigen adresberekening) terwijl ik op de nominale VA nullen zie. Dat sluit aan op
   de eerdere verdenking "de pakker legt het elders neer".
2. de hook leest iets anders dan hij denkt (bijvoorbeeld via een verkeerde basis), en de prologues die
   hij print komen niet van de adressen die hij noemt.

**Wat dat zou beslechten, en wat ik daarvoor nodig heb:** laat de hook de **pointer** loggen die hij
werkelijk derefereert bij het lezen van een prologue (niet de RVA, maar het adres dat hij in zijn hand
heeft), plus de modulebasis die hij gebruikte. Dan is in een run te zien of dat 0x1402b3b70 is of iets
anders -- en dan weet ik of mijn lezing of zijn berekening de afwijkende is.

**Gevolg voor de conclusie over het spel:** zolang dit niet beslecht is, is "de pakker kwam niet klaar"
niet te handhaven. Het kan ook zijn dat de pakker wel klaar is en dat mijn lezing naar de verkeerde
mapping kijkt. De run zelf is verder geldig: drop-in 1/1 in het proces, 16 van 17 aangehaakt, spel weg
op 6,0 s.

**En over `0x1402b4270`:** die stond in het logboek op `cc cc cc cc` (int3-opvulling) toen de hook hem
afkeurde, en in mijn lezing op `00 00 00 00 00` op elk monster -- dezelfde tegenstelling. In geen enkel
monster kreeg dat adres een echte prologue. Dat adres klopt dus niet in deze build en hoort uit de
adreslijst, ongeacht welke van de twee lezingen juist is.

## BESLECHTING 2026-10-13 (tweede meting): de hook derefereert HETZELFDE adres als ik, en we zien verschillende bytes

Run met de nieuwe hook (`rg-pe-hook.dll`, 130.341 B, sha256 `4e6759c9474e804c`), ONZE kern, dichte
cadans. Ruwe uitvoer: `/tmp/b0-vergelijk/`. De hook logt nu de aanwijzer die hij werkelijk gebruikt.

**Wat vaststaat:**

| | waarde |
|---|---|
| de hook: aanwijzer/basis | `VA 0x00000001402b3b70` met basis `0x140000000` |
| mijn lezer: hetzelfde adres | `0x1402b3b70` (mijn `AM_ADRESSEN`) |
| de hook zag daar | `bytes 40 53 48 83 ec 20 8b da`, keuring `prologue 40 53 48 83` |
| ik zag daar | `2b3b70=0000000000`, op **alle 12 monsters** (0,2 s t/m 5,5 s) |
| mijn lezing | `.text 100,0 % nul`, **`3496500 van 3496500 B gelezen`** bij elk monster |
| tweede adres 0x1402b4740 | hook: `48 83 ec 28 48 8b 0d 1d`; ik: `0000000000` op alle 12 monsters |

**Dus: geen verkeerde basis, geen kort lezing, geen timingverschil.** Beide instrumenten keken naar
hetzelfde adres, in hetzelfde proces, op hetzelfde moment, en zagen andere bytes.

**En mijn pid is aantoonbaar het juiste proces:** het aantal threads in mijn steekproef ging van **1 naar
2 op het moment dat de drop-in geladen werd** (`drop-in 0/1 -> 1/1`). Dat is de eigen thread van de hook,
die dus in het proces zit dat ik bemonster. Mijn lezer is ook niet bevroren: in hetzelfde proces zag ik
het entry point veranderen (`EB FE` op 1,0 s, TRAMPOLINE op 2,5 s) en dat zit in `.capcom1`.

**Wat overblijft zijn twee mogelijkheden, en deze run kan ze niet scheiden:**

1. **de bytes in het logboek komen niet van het adres dat erbij staat** (een tweede logfout, zoals de
   eerder gevonden fout waarbij de bytes pas NA het patchen werden gelezen);
2. **`/proc/<pid>/mem` laat voor dit Wine-proces van `.text` iets anders zien dan de PE-code zelf**,
   terwijl het voor `.capcom1` wel klopt (daar zie ik de trampoline en `EB FE`).

**Per instrument wat het wel en niet kan:**

| | mijn sampler (`/proc/<pid>/mem`) | het logboek van de hook (in-process) |
|---|---|---|
| kan | de hele sectie lezen met byte-telling, zonder het spel aan te raken; entry point, threads, open bestanden; leeftijd van het proces volgen | de bytes zien zoals het spel ze zelf ziet |
| kan niet | niet zeggen of zijn beeld hetzelfde is als wat de PE-code ziet; als Wine voor `.text` een andere view geeft, ziet hij nullen waar het spel code ziet | geen onafhankelijk bewijs: hij leest met zijn eigen aanwijzer, en een fout in wat hij logt is van buiten niet te zien |

**Wat dit beslecht, en dat is goedkoop:** laat de hook de bytes die hij las **naar een bestand schrijven**
(bijvoorbeeld 16 regels met VA + 8 bytes, plus een hash per pagina) in plaats van ze alleen te loggen.
Dat is een gegeven dat ik met een eigen instrument kan natrekken. Klopt dat bestand met zijn logregel, dan
is het meningsverschil over de GEHEUGENVIEW en moet de volgende stap de mapping zijn -- niet een
instrument. Klopt het niet, dan is de logregel de afwijkende.

**Gevolg voor de conclusie:** "de pakker kwam niet klaar" is nog steeds niet te handhaven, en wijst nu
eerder de andere kant op: de hook ziet op hetzelfde adres echte code. Zolang dit niet beslecht is, is de
vraag of de am-laag geraakt wordt niet te beantwoorden.

**Verder in deze run:** 16 van 16 aangehaakt, 0 afgekeurd (het adres `0x2b4270` is uit de lijst, goed);
`.text` 100 % nul in mijn lezing; entry point op de trampoline van 2,5 s tot het einde; **spel weg op
6,0 s**. De 16 prologue-bytes uit het logboek (4 bytes per functie) zijn doorgegeven; mijn eigen 5-byte
lezing geeft nullen, dus de vijfde byte wacht op de beslechting hierboven.

## BESLECHTING 2026-10-13 (derde meting): er is maar EEN mapping, en mijn lezer leest anonieme mappings goed

Meting met `b0-mapping.py` (nieuw) op een lopend spelproces onder ONZE kern, op 4,5 s en 5,5 s.
Ruwe uitvoer: `/tmp/b0-map/`.

**De mappings van het beeldbereik:**

```
140000000-140001000 r--p  .../VACUUM.exe        <- file-backed, de header
140001000-140357000 r-xp  (anoniem)             <- .text
140357000-14049c000 r--p  (anoniem)             <- .rdata
14049c000-140d46000 rw-p  (anoniem)             <- .data
140d46000-140d71000 r--p  (anoniem)             <- .pdata
140d71000-14125c000 r-xp  (anoniem)             <- .capcom0 + .capcom1
14125c000-14125e000 r--p  (anoniem)
```

**Er is dus geen tweede mapping van het beeld.** De hypothese "een file-backed beeld plus een anonieme
kopie waar de pakker in schrijft" is weerlegd: `.text` is EEN anonieme `r-xp`-mapping, en `.capcom1` is
er ook een. En mijn lezer leest anonieme mappings aantoonbaar goed -- op het entry point in `.capcom1`,
in dezelfde anonieme soort mapping, lees ik `eb 08 92 1b 19 00 00 00`, precies de ongerepte eerste sprong
uit het bestand. De file-backed header lees ik ook goed: `4d 5a 90 00` = `MZ`, gelijk aan het bestand.

| adres | dekkende mapping | mijn bytes |
|---|---|---|
| `0x1402b3b70` (am) | `0x140001000-0x140357000` anoniem `r-xp` | `00 00 00 00 00 00 00 00` |
| `0x1402b4740` (am) | idem | `00 00 00 00 00 00 00 00` |
| `0x140f23125` (entry) | `0x140d71000-0x14125c000` anoniem `r-xp` | `eb 08 92 1b 19 00 00 00` |
| `0x140000000` (header) | file-backed | `4d 5a 90 00`, **gelijk aan het bestand** |

**Conclusie: niet mijn lezer is de afwijkende.** Er is geen andere mapping om naar te kijken, en ik lees
anonieme mappings aantoonbaar correct (het entry point in `.capcom1` bewijst het, in dezelfde run en
hetzelfde proces). Wat overblijft is dat **de bytes in het logboek van de hook niet van het adres komen
dat ernaast staat**. Dat is dezelfde soort fout als de eerder gevonden logfout (bytes lezen na het
patchen), en het is de vierde keer dat dit instrument iets logt dat de meting niet ondersteunt.

**En de scherpe gevolgtrekking voor game 1:** als `.text` werkelijk helemaal nul is op het moment dat de
hook patcht, dan schrijft hij zijn 16 `e9 rel32`-sprongen **in een regio die de pakker later vult** -- en
dan worden ze bij het ontpakken overschreven. Een run die "16 van 16 aangehaakt" meldt zegt dus niets
over de am-laag zolang vaststaat dat `.text` op dat moment nul is. De juiste volgorde is: eerst laten
ontpakken, dan pas haken. Dat verklaart ook waarom de levensduur niet veranderde.

## BESLECHTING 2026-10-13 (vierde meting): de GESLOTEN kern ontpakt in DEZELFDE anonieme regio

De vraag was: schrijft de pakker in dezelfde anonieme regio die ik lees (dan zou ik code moeten zien)
of ergens anders? Antwoord uit de bestaande dump van de GESLOTEN kern op 8 s (`.text` 11,2 % nul, dus
volledig ontpakt), `/tmp/b0-geheugen-b/maps-gesloten.txt`:

```
140000000-140001000 rwxp .../VACUUM.exe     <- file-backed, de kop
140001000-140357000 r-xp 00000000 00:00 0   <- .text            <- DEZELFDE REGIO
140357000-14049c000 r--p 00000000 00:00 0
14049c000-140d46000 rwxp 00000000 00:00 0
140d71000-140da2000 r-xp 00000000 00:00 0
140f15000-14125e000 rwxp 00000000 00:00 0
```

Dekkende mapping per adres, onder de GESLOTEN kern in de ONTPAKTE toestand:

| adres | dekkende mapping |
|---|---|
| `0x1402b3b70` | `0x140001000-0x140357000` anoniem `r-xp` -- **precies dezelfde regio die ik lees** |
| `0x140001000` | idem |
| `0x140f23125` | `0x140f15000-0x14125e000` anoniem |
| `0x140000000` | file-backed `VACUUM.exe` |

**En in diezelfde dump staat `.text` op 11,2 % nul: de regio bevat dan echte code.** Er is geen aparte
mapping waar de pakker naartoe schrijft. **De pakker schrijft in dezelfde anonieme regio die ik lees.**

**Wat dat betekent:** als de pakker klaar is, dan zou IK op `0x1402b3b70` code moeten zien -- en dat is
precies wat er onder de gesloten kern gebeurt. Dat ik onder onze kern daar nullen zie, betekent dus dat
de pakker daar NIET klaar is. En het logboek van de hook dat daar echte prologues meldt, kan daarmee
niet van dat adres komen. **Mijn lezing is niet de afwijkende; die van de hook is het.**

**En de conclusie over de VOLGORDE staat:** haken moet NA het ontpakken. Zolang de hook zijn 16 sprongen
in een nog lege `.text` schrijft, vult de pakker die regio later en verdwijnen ze. Een run die "16 van
16 aangehaakt" meldt zegt dan niets over de am-laag.

**Wat nog niet gelukt is:** de vergelijking met het nieuwe artefact `rg-pe-hook-bytes.txt`. De run werd
door mijn eigen bench-controle geweigerd omdat er nog een `wineserver` (760522) liep uit de vorige
meting; er is dus niets gestart en niets gemeten. Die vergelijking staat nog open: artefact-bytes per
tijdstip naast mijn bytes per tijdstip, op `0x1402b3b70` en `0x1402b4740`.

**Per instrument, kort:** mijn lezer kan de hele sectie met byte-telling lezen en de mappings opvragen,
maar niet zeggen wat de PE-code zelf ziet; de hook ziet wat het spel ziet, maar heeft geen
onafhankelijke controle op zijn eigen bytes -- en op deze vier metingen is dat zijn zwakke punt gebleken.

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

