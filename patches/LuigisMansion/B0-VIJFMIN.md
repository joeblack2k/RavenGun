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

