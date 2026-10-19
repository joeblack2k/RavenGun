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
