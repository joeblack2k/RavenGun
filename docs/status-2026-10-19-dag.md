# Dagdienst 19 oktober 2026 — stand voor Jeroen

Besluiten van de eigenaar (ochtend): privégegevens blijven in de release-tarball (geen `--public`),
mappen waar een game in schrijft horen in de **schrijflaag** (nooit een koppeling naar de NAS), er
komen geen dongle-dumps of kast-logs (zelf uitzoeken met statische analyse en meten), `iauthdll`
registreren en `PreprocessAmAuth` per prefix automatiseren, en de opruimactie van de oude worktrees,
buildah-mounts en `claude/pass2-*`-takken doet de leiding zelf.

## (A) De schrijflaag — klaar (8819695)

* Nieuwe profielsleutel **`[Launch] ViewWritable`** (komma-gescheiden, relatief aan de **wortel van de
  view** = de werkmap van de game, niet de map van de exe — Robin Hood start uit de view en schrijft
  in `log/` naast `data/`).
* Bij het bouwen van de view wordt zo'n map een **echte map met een kopie** van de NAS-inhoud; staat
  er al een echte map, dan blijft die staan (dat is de stand van de game). Er wordt nooit naar de NAS
  geschreven.
* **Harde controle:** komt een opgegeven schrijfmap na het plaatsen nog op de NAS uit
  (`readlink -f`), dan **weigert de engine de start** met een duidelijke fout. `..` en absolute paden
  worden al bij het lezen van het profiel geweigerd.
* Ingesteld: **Mario Kart `AMCUS`** (schrijft `WritableConfig.ini`) en **Robin Hood `log`** (schrijft
  `PR0/PR1.CFG` en `.CRC`).
* Proef: `ravengun_direct_launch_tests` — 460 controles (echte map, kopie, NAS ongemoeid na schrijven
  in de view, tweede keer niets doen, weigeren als het pad op de NAS uitkomt, weigeren van `../x` en
  `/abs`).
* De bestaande NAS-bestanden (`AMConfig.ini`, `PR0/PR1.CFG`, `.CRC`, `PRbk*.dat`) zijn niet
  verplaatst, gewijzigd of verwijderd. De eerste start na deze wijziging zet de twee mappen om.
* Terloops gevonden en gerepareerd: mijn ronde-9-herschrijving had de **`[Launch]`-kop van Mario
  Kart** weggehaald (de sleutels `Mode`/`StackReserve` stonden onder `[Game]`).

## (B) ctest 94/94 — klaar

De oorzaak van het niet-vinden was diezelfde ontbrekende `[Launch]`-kop: `direct_golden.py` zag het
profiel niet meer als "eigen profiel met `[Launch] Mode = direct`". Met de kop terug vindt de proef
(en `--update`) het profiel weer: **44 profielen**, `tests/golden/direct/RobinHood.json` ververst (in
de proefopstelling wordt Robin Hood nu eerder geweigerd omdat de schrijfmap daar niet in een view
ligt — precies het gedrag dat we willen). **ctest: 94 van 94.**

## (C) De open punten in de RobinHood I/O-kant — klaar

* **Geen spookdata meer:** `ClearCommError` meldt nu **0** zodra het antwoord helemaal gelezen is
  (vlag `g_antwoord_op`), ook bij stand `niks`; `PurgeComm` wist het wachtende antwoord. Zonder dit
  las het spel na een volledig kader opnieuw 7 bytes en begon het rondje opnieuw — een van de
  kandidaten voor de hang.
* **Slot vóór de haken:** `InitializeCriticalSection` gebeurt nu als eerste in `DllMain`.
* **Herprobeer-draad alleen als het echt nodig is:** per import staat nu of hij verplicht is; de
  optionele (PurgeComm, Get/SetCommState, SetCommTimeouts, EscapeCommFunction, SetCommMask,
  WaitCommEvent, GetOverlappedResult) laten de draad niet meer starten en worden als "optioneel"
  gelogd.
* **`OpenBordbeeld`:** herhaalpoging bij elke aanroep, het handvat blijft bewaard (geen lek) en de
  melding komt één keer.
* **Geen `LoadLibrary` in `DllMain`:** de meetdraad van rg-compat wordt nu lazy geladen bij de eerste
  bord-aanraking (`std::call_once`).
* **`RG_RHIO_TOETS`** wordt één keer gelezen, niet per kader.
* **De AMAuth-proefariteit:** de proef gebruikte stdcall met een vaste ariteit, dus de conclusie "het
  spel roept slot 9 aan" is **niet betrouwbaar** en wordt hierbij **ingetrokken**; met de echte
  AMAuthd draait het spel door, dat is de meting die telt.
* De dubbele TC5-noot was al weg.

## Let op: de schrijflaag vuurt (nog) niet op de echte view

De nieuwe `[Launch] ViewWritable` is ingebouwd en de proef bewijst hem (460 controles, via
`apply_direct`), maar op de **echte** view bleef `views/RobinHood/log` een koppeling: met
`rg-direct --plan` staat `view_writable` goed in het plan en de tekst "schrijfmap" zit in de binary,
maar bij `--run` gebeurt er niets. Dat is één open punt met een scherpe volgende stap (de
`--run`-weg van plan naar uitvoering nalopen, of het loggen van de schrijflaag naar de console
zetten). **Stopgap voor vandaag:** de twee mappen zijn met de hand één keer omgezet (een kopie in de
view, de NAS alleen gelezen), dus `views/RobinHood/log` en `views/MarioKartDX/AMCUS` zijn nu echte
mappen en de games schrijven daar lokaal. De NAS-bestanden (PR0/PR1.CFG, .CRC, AMConfig.ini,
PRbk*.dat) zijn niet verplaatst, gewijzigd of verwijderd.

## Nog te doen (op volgorde van de leiding)

* **(C)** de open `rg_rhio`-punten (spookdata in `ClearCommError`, slot vóór de haken,
  herprobeer-draad, `OpenBordbeeld`, geen `LoadLibrary` in `DllMain`, `RG_RHIO_TOETS` één keer lezen,
  de AMAuth-proefariteit en de dubbele TC5-noot).
* **(D)** Robin Hood 90 min: de wachtende hoofddraad volgen (op welk handvat wacht hij, waar wordt dat
  gemaakt, welk pad hoort het te seinen).
* **(E)** Coopers 9 60 min: eerst het RobinHood-kaderprotocol, dan de vroege fout op 0x4fd8aed0.
* **(F)** Mario Kart 90 min: `iauthdll` + `PreprocessAmAuth` idempotent automatiseren in de
  prefix-opzet (in de schrijflaag), dan het zwarte beeld (welke IAuth-methodes roept het spel aan).
* **(G)** de nog niet geteste profielen: HOTD4SP, MenInBlack, SegaGameShell, 3DCosplayMahjong,
  AliensArmageddon, TNXK0O~C — 30 min per stuk, 1 min attract + foto + eigen commit, of parkeren met
  een noot. AfterDark, PigsMightFly en LostLandAdventure alleen als er tijd over is.
