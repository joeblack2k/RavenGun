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
* **(D)** Robin Hood 90 min (09:50-10:47): de meetdraad **loopt** nu (markerbestand) en de meting
  wijst uit dat de hoofddraad in `NtWaitForSingleObject` wacht terwijl geen enkele draad in de
  lock-helper zit; de volgende stap is de handvatwaarde. Zie `docs/research/wartran/README.md`.
  De mutex-haken zijn gebouwd maar mogen niet aan (ze laten de game stoppen), en ook het uitbreiden
  van de meetdraad met stapelwoorden bleek fataal (teruggedraaid; de meetdraad is weer precies zoals
  in de repo). De volgende stap staat in de wartran-README: het handvat van de wacht lezen zonder te
  haken. Er is nu ook een **wachthandvat-lezer** in rg_rhio (draden even stilzetten, `Eip`/`Esp` en
  de stapelwoorden lezen, geen import-haak): de game blijft er goed van draaien, maar de aanroep wordt
  nog niet bereikt -- zie de wartran-README voor de exacte plek. **Eerlijk: I/O CHECKING is nog niet
  voorbij.**
  **Oudere notitie:** de wachtende hoofddraad -- **deels gedaan**: de mutex is gevonden en
  gedocumenteerd (`docs/research/wartran/README.md`), en de valkuilen bij het meten staan er ook
  (controller voor de daemon, `pgrep` die je eigen shell matcht). De meetdraad zelf moet nog
  lopen: `RG_COMPAT_SAMPLE=200` in de drop-in en dan de draad in 0x580fa0-0x5810a0 zoeken.
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

### OPGELOST: de schrijflaag werkt, en de oorzaak was de controller

De aanleiding was niet de schrijflaag maar de **start**: na een reeks daemon-herstarten was de
muis-geweerinstelling van speler 1 verdwenen, en de daemon bleef bij het starten wachten op een
controller ("Zet de PS Move van speler 1 aan"). Er werd dus **geen game gestart**, en daarom zag ik
geen drop-in in het proces, geen `rg-rhio.log` en geen omgezette map. Met
`ravengunctl mouse-gun --player 1 --mouse ...` er weer in, start de game wel en is de schrijflaag
**bewezen op het echte pad**:

* `views/RobinHood/log` is na een gewone start een **echte map met de volledige kopie**
  (PR0.CFG, PR0.CRC, PR1.CFG, PR1.CRC, PRbk1.dat, PRbk2.dat);
* `ls --full-time` van de NAS-map `Robin Hood/log` is **voor en na identiek**
  (02:09:35 en 10-15, geen enkele regel veranderd);
* de spelstand is daarna teruggezet in de echte map; de NAS is niet aangeraakt.

Daarmee is ook de **runtime-controle van (C)** gedaan: `rg-rhio.log` bestaat weer en de
`cbInQue`-waarden zijn nu 0 (19920 keer, als er niets klaarstaat), 6, 5 en 7 -- precies wat C1 beoogde
(het protocol loopt door in plaats van na frame 1 te sterven).

### (Historisch) de eerste verklaring die niet klopte

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

## (C) De regressies van reviewronde 10 in de RobinHood-bordkant

* **C1 (hoog)**: `BouwAntwoord` zet `g_antwoord_op` weer op `false` als er een nieuw antwoord klaarstaat.
  Zonder dat meldde `ClearCommError` na het eerste volledige kader voor altijd `cbInQue = 0` en ging
  het bordprotocol na frame 1 dood.
* **C2**: bij stand `niks` staat er niets klaar, dus `g_antwoord_op = true` (cbInQue 0) en draait de
  leeslus niet warm.
* **C3**: de oude `InitializeCriticalSection(&g_bord_cs)` onderaan `DllMain` is weg; het slot wordt nu
  één keer gezet, vóór de haken.
* **C4**: `GetOverlappedResult` leest de teller onder het slot; `char bord[8]` is geïnitialiseerd;
  `RG_RHIO_EERSTEBYTE` en `RG_RHIO_BORD` worden één keer gelezen; de kop van het bestand noemt alle
  negen omgevingsvariabelen; `std::call_once` is vervangen door een `InterlockedCompareExchange`-vlag
  (geen winpthreads meer) en `build-rhio.sh` stript nu: de DLL ging van 2,3 MB naar **197 kB**; het
  logboek is begrensd op 20.000 regels met één afsluitende regel.
* **C5**: de conclusie over "slot 9" is in `docs/research/namco/README.md` en
  `docs/status-2026-10-19-nacht.md` ingetrokken (de proef gebruikte stdcall met een vaste ariteit).

**Eerlijk punt:** de *runtime*-controle van C1 kon ik niet doen omdat `rg-rhio.log` niet verschijnt —
het plan bevat `RG_RHIO_LOG` wel (nagekeken in `rg-direct --plan`) en de DLL staat in de view, maar er
komt geen logboek. Dat hoort bij hetzelfde raadsel als de schrijflaag hierboven (de daemon-route), en
het is de eerste vraag voor de volgende ronde.

## Review 11: mijn C-ronde liet Robin Hood meteen stoppen (gevonden en verholpen)

Tijdens (D) bleek de game na de C-ronde niet meer te starten (de lader stopte met 0, de game sloot
zich meteen, geen scherm, geen spelstand). Bisect met een keurscript (`/tmp/rh-keur.sh`: bron bouwen,
naar de drop-in EN de view kopieren, starten, foto) gaf:

* pre-C bron: **draait** (foto 23 kB = I/O CHECKING);
* C1+C2+C3: draait; +C4a: draait; +C4b of C4c: **stopt**; C1+C2+C3+C4a+C4e+C4f: draait.

Twee wijzigingen waren fataal, allebei rond de 8-byte buffer `bord` in `BouwAntwoord`:

1. **C4b** `char bord[8] = {0};` in plaats van `char bord[8];`
2. **C4c** dezelfde buffer via een `static const bool met_bord = []{ char v[8] = {0}; ... }();`

Elke vorm die die buffer anders neerzet laat de game stoppen. Dat wijst op een **latente fout elders
in het bestand** (de framelayout van `BouwAntwoord` verandert en een bestaande overschrijding wordt
fataal); de eerlijke conclusie is dat C4b en C4c hier niet veilig te doen zijn zonder die fout eerst
te vinden. Ze zijn daarom **teruggedraaid** naar de oude vorm, met een waarschuwende opmerking erbij.
De rest van de C-ronde (C1, C2, C3, C4a, C4e, C4f) is gebleven en werkt: het logboek stopt nu bij
20.000 regels (722 kB in plaats van 20 MB) en het protocol loopt door.

Daarnaast bleek het installeren van de **mutex-haken** (WaitForSingleObject/ReleaseMutex/CreateMutexA)
op elke start fataal: die worden door de CRT al heel vroeg aangeroepen. Ze zijn nu **opt-in** met
`rg-rhio-mutex.aan` naast de DLL of `RG_RHIO_MUTEX=1`.

**Les voor het keuren**: de drop-in in `~/.local/opt/ravengun/patches/...` is de bron die de engine
materialiseert; een herbouw van alleen `patches/...` in de repo verandert de start niet. Alle
keurproeven hierboven kopieren naar **beide**.

## (E) Coopers 9 -- eerste minuten (10:32-10:35)

De start gaat via onze eigen route (kern `openparrot/current/OpenParrotWin32/OpenParrot.dll`, de lader
stopt met 0, `rg-direct` volgt de game), maar **de game sluit zich meteen**: geen scherm, geen
spelstand. Dat is hetzelfde beeld als bij Robin Hood toen het misging, dus de volgende stap is
dezelfde: kijken wat de game zelf vindt (dongo-controle, config, `[Config]`-bestanden in de view) en
het drop-in-logboek van die start lezen (`direct-Coopers9-20261019-103208.log`). De box van (E) is
hiermee begonnen maar nog lang niet af; er is geen minuut attract geweest.

**Stand van de lijst op 10:55**: (A) klaar en bewezen, (B) ctest 94/94, (C) klaar en op het echte pad
gecontroleerd, (D) meetdraad loopt en de meting staat er (I/O CHECKING nog niet voorbij, volgende stap
in de wartran-README), (E) is opnieuw gemeten en **geparkeerd** met een precieze notitie (de volgende stap vraagt de bron van
de kern of een debugger; zie de wartran-README), (F) en (G) nog niets.

## (F) Mario Kart: per-prefix IAuth -- recept gevonden en de volgende blokkade benoemd (10:42-10:55)

Wat nu **gemeten en werkend** is (alles in de schrijflaag; de NAS is met `find -newermt` gecontroleerd
en onaangeroerd):

1. `PreprocessAmAuth.exe -wait`, gedraaid **vanuit de view-wortel**, zegt
   `Checking.ErrorCode : 0 / outPath : WritableConfig.ini / Ok!` en schrijft het bestand in
   `views/MarioKartDX/TeknoParrot/E/` (een echte map in de view).
2. Dat bestand moet naar **`views/MarioKartDX/AMCUS/`** (de map van AMAuthd, ook een echte map in de
   schrijflaag): daar zoekt AMAuthd het. Zonder dat bestand zegt hij
   `ERROR: file not found: WritableConfig.ini` en doet niets; met het bestand **blijft hij draaien**
   (direct onder wine gemeten, vier processen, geen fout).
3. De klasse staat in de prefix: `HKLM\Software\Classes\CLSID\{4603BB03-...}` met `LocalServer32`
   (nagekeken in `system.reg`); registreren kan met `wine regsvr32.exe AMCUS/iauthdll.dll`.

Wat **nog blokkeert**: met onze kern erin stopt `AMAuthd.exe` meteen en meldt het laderlogboek
`Failed to Load DLL! (Error 4)`; de loper breekt de start dan af ("start 1/2 stopte voordat start 2/2
begon", exitcode 7) en het spel komt nooit in beeld. De tweede exe is een gewone server die **niet met
onze kern** geladen hoort te worden; de motor heeft geen sleutel om per exe een andere kern (of geen
kern) te kiezen. Dat is de volgende stap -- en die vraagt een motorwijziging, dus die hoort bij de
leiding of in een volgende ronde. Automatiseren van stap 1 en 2 hoort daar meteen bij: de motor kent
geen "een keer per prefix"-haak.
