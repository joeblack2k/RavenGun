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
* **(D)** Robin Hood 90 min: de wachtende hoofddraad -- **eerste uur gedaan**, zie hieronder.
* **(E)** Coopers 9 60 min: eerst het RobinHood-kaderprotocol, dan de vroege fout op 0x4fd8aed0.
* **(F)** Mario Kart 90 min: `iauthdll` + `PreprocessAmAuth` idempotent automatiseren in de
  prefix-opzet (in de schrijflaag), dan het zwarte beeld (welke IAuth-methodes roept het spel aan).
* **(G)** de nog niet geteste profielen: HOTD4SP, MenInBlack, SegaGameShell, 3DCosplayMahjong,
  AliensArmageddon, TNXK0O~C — 30 min per stuk, 1 min attract + foto + eigen commit, of parkeren met
  een noot. AfterDark, PigsMightFly en LostLandAdventure alleen als er tijd over is.

## (D) Robin Hood: waar de hoofddraad op wacht — eerste uur gedaan

Met statische analyse (objdump) is de wacht van de hoofddraad gevonden: hij wacht met
`WaitForSingleObject(mutex, INFINITE)` op een **wereldwijde mutex** die lazy gemaakt wordt
(`CreateMutexA` + `InterlockedCompareExchange`, samen in één helper op 0x580fa0-0x580fde), met de
bijbehorende `ReleaseMutex` op 0x580ff6. **`SetEvent` wordt in de hele exe nooit aangeroepen** -- dus
"de I/O-draad moet een event seinen" is een dood spoor; de hoofddraad wacht op een **slot dat een
andere draad vasthoudt**.

Dat maakt de volgende stap scherp: welke draad houdt dat slot, en welke van onze haak-aanroepen laat
hem wachten? Elke haak (`ReadFile`, `WriteFile`, `ClearCommError`, `GetCommTimeouts`,
`WaitCommEvent`) moet snel terugkeren. De volledige tabel met sleuven, aanroepen en het patroon staat
in `docs/research/wartran/README.md`; de volgende drie stappen staan daar ook.

## (A) hard gemaakt volgens reviewronde 10

* **A1**: de koppeling gaat pas weg als de kopie **volledig** gelukt is. Er wordt eerst naar
  `<pad>.ravengun-tmp` gekopieerd met `directory_iterator` + `increment(ec)` (geen worp in de
  daemon-draad, die zou `std::terminate` zijn), elke fout wordt gemeld, en pas daarna volgt
  `unlink` + `rename`. Een NAS-hapering laat zo nooit een lege of halve map achter die als spelstand
  bewaard blijft.
* **A2**: de uitslag van `unlink` en `rename` wordt gecontroleerd; vóór elke aanraking wordt de
  **ouder** getest (`on_nas(realpath_(dirname(pad)))`) zodat een koppeling in een koppelingsmap niet
  alsnog op de NAS schrijft.
* **A3**: ook als het pad niet bestaat wordt de ouder gecontroleerd, en een **case-variant** die naar
  de NAS wijst (Wine is niet hoofdlettergevoelig) wordt behandeld.
* **A5**: kinderen die naar de NAS wijzen worden **lokaal gemaakt** (bestand kopiëren, koppeling
  weg, echt bestand terug); een kind dat een NAS-**map** is wordt geweigerd met uitleg.
* **A4**: `rg-direct --run PLAN.json` weigert nu ook een schrijfmap die een koppeling is of op de NAS
  uitkomt (dezelfde controle als de daemon-route).
* **A6**: `refresh_game_view` zet **nooit** een link naar de NAS binnen een schrijfmap (nieuw veld
  `writable_dirs` in `GameViewSpec`, gevuld uit `[Launch] ViewWritable`). `InfinityBladeFX`
  (`SwordGame/Logs`), `BugBusters` (`config`) en `HauntedMuseumII` (`data`) hebben de sleutel nu ook.
* **A7**: `on_nas` kiest bij twee regels met hetzelfde koppelpunt (autofs boven cifs op deze Deck) de
  **laatste**; eerst bleef de autofs-regel staan en werkte het alleen door de `~/mnt`-voorwaarde.
* **A8**: vier nieuwe proeven in `ravengun_direct_launch_tests` (mislukte kopie laat de koppeling
  staan en niets halfs achter; ontbrekend pad onder een koppeling naar de NAS wordt geweigerd; een
  kind dat naar een NAS-bestand wijst wordt lokaal; een mislukte stap laat niets halfs achter) --
  **475 controles, 0 fout**. De gouden vergelijking laat nu `view_writable` zien en **RobinHood staat
  weer op `ok`** (het volle plan), doordat de proefopstelling RobinHood en MarioKartDX in een view
  zet. MarioKartDX blijft daar geweigerd op `[Launch] Mod = english` (de fan-vertaling is een
  download die de proefopstelling niet heeft); de tweede exe wordt in de C++-proeven gedekt.
* **Installatiestap (voor de volgende keer)**: de daemon komt uit **`build/`** (niet `build-app/`;
  die heeft geen `ravengund`-doel). Na een bouw: `cmake --install build --prefix
  ~/.local/opt/ravengun` en daarna `systemctl --user restart ravengund`, anders draait de oude
  engine. `build-app` bouwt hier niet (geen `archive.h`); alleen `ravengund`/`ravengunctl` komen uit
  `build/`.

### Nog open: de schrijflaag vuurt niet in de daemon (eerlijk)

De functie is proefondervindelijk goed (475 controles, inclusief de volle `apply_direct`), maar in de
**daemon** wordt `place_view_writable` niet bereikt: met een `fprintf(stderr, ...)` op de eerste regel
(het daemon-stderr gaat naar `logs/ravengund.log`) kwam er bij een gewone start **geen enkele regel**,
terwijl `games.cpp` `L::apply_direct` wel aanroept. Drie routes zijn geprobeerd: `rg-direct --run`
(blijkt volgens de review alleen te *starten*, niet te plaatsen), de daemon met een verse binary (de
geïnstalleerde `ravengund` bevat de nieuwe code, gecontroleerd met `strings`) en instrumentatie in de
functie zelf (geen uitvoer). De volgende stap is dus *vóór* de functie: uitzoeken welke weg de daemon
voor RobinHood werkelijk loopt (`direct_route` → `plan_direct` → `apply_direct`, of de write_layer-
route) en of `pl.view_writable` daar gevuld is. **Stopgap**: `views/RobinHood/log` en
`views/MarioKartDX/AMCUS` zijn met de hand echte mappen met een kopie; de NAS is onaangeroerd
(`ls --full-time` van `Robin Hood/log` is vóór en na de proef identiek: 02:09:35 en 10-15).
