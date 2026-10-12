# Jurassic Park — actuele stand (2026-10-12, autonome ronde)

Eigenaar wilde JP eerst. Read-only geanalyseerd (geen game gestart). Conclusie: JP is **verder dan de
oude "geparkeerd, wacht op de lader"-stand** — die stand en `decisions.md` ("geen preload-laag") zijn
**achterhaald** door de `rt-glfix`-brug die daarna is gebouwd en nu bedraad is.

## Wat er nu draait (bevestigd via `launch-direct.py JurassicPark --plan`)
- Keten: **BudgieLoader (gesloten, ElfLdr2) + onze `rt-glfix`-brug via `LD_AUDIT`** (`librt-glfix.so` staat in
  `launch_env`/`dropins.audit`). Dit is **geen uitpakwerk**: geen eigen lader, geen resolutietabel geoogst, geen
  import-reconstructie — de brug rijdt mee op ElfLdr2's eigen binding en herstelt alleen de cdecl/stdcall-ABI +
  een `GL_MAX_TEXTURE_SIZE`-vangnet + `dlsym`-interceptie.

## Resultaat
- **plain BudgieLoader:** crash na ~14 s in `VidStart` (`glGetIntegerv(GL_MAX_TEXTURE_SIZE)`), cdecl↔stdcall-mismatch,
  `ret → 0x58000`.
- **met `rt-glfix`:** ~35 s, VidStart-crash weg, cdecl-fout weg. **Resterend:** een tweede, ongediagnosticeerde fout
  (`eip=0x819c4d80`, `ebp=0`), nog geen beeld. De ~85 niet-opgeloste GL-slots zijn **bewezen niet de oorzaak**
  (`docs/rt-glfix.md` §7).

## Waarom nu bench-gated (geen harde blocker)
De volgende stap is de tweede fout diagnosticeren: instructie op `0x819c4d80` + schrijf-watchpoint op het
retouradres (`docs/rt-glfix.md` §6). Dat is een **getracete run**, en er mag maar één game tegelijk draaien — dus
dat kan **niet** tijdens de bench van de eigenaar. Zodra de bench vrij is, is dit de eerste meting (JP = prioriteit).
**Onbekend** blijft of die tweede fout binnen scope (ABI/lader-laag) oplosbaar is of pas na slot-reconstructie
(= uitpakwerk, buiten scope per `decisions.md` 2026-10-11) verdwijnt. Dát is de echte open vraag.

## Voor de eigenaar
- `patches/JurassicPark/game.ini` `[Status] = parked` en `decisions.md` ("geen preload-laag, wacht op de lader")
  zijn achterhaald door de `rt-glfix`-brug. Welke stand geldt, is jouw beslissing; feitelijk is de brug bedraad en
  werkt hij deels.
- HASP: statisch in de game gelinkt; hook/keten met een probe bewezen maar nooit bereikt in het spel (GL blokkeert
  eerder). Dongle-image is **geconstrueerd** (112× 0xFF), niet gedumpt — dongle blijft onaangeroerd.

## Kernbestanden
`docs/rt-glfix.md` §6-7, `findings/games/JurassicPark/CRASH-OPLOSSING.md` (bevat de reconciliatie met het
NAS-document), `patches/ELF-LADER-SPEC.md` ("Zitten de namen écht niet in het bestand?"),
`docs/research/elfldr2-injectie.md` r.23-78.

### Oudere NAS-bronnen (zelfde onderwerp, vóór de brug — read-only; wij schrijven niet op de NAS)
De NAS-mount (`~/mnt/spidernas_emulation`) is een CIFS-mount met `rw`, maar de afspraak is dat we er niet in
schrijven. Twee artefacten van het TeknoParrot-team (2026-10-11) horen bij deze zaak:
- `~/mnt/spidernas_emulation/Teknoparrot/findings/games/JurassicPark/CRASH-OPLOSSING.md` — Johans analyse van
  fase 1 (GL/`VidStart`/canarie → `0x08358000`) en de voorgestelde GOT-herschrijving + `dlsym`.
- `~/mnt/spidernas_emulation/Teknoparrot/findings/games/JurassicPark/PLT-slots.md` (939 regels) — alle 578
  PLT-stubs met stubadres, GOT-slot, bestandswaarde en libGL-offset.

De verzoening (fase-onderscheid, de getallen, en waarom de offset→naam-methode met onze bibliotheken niet lukt)
staat in `findings/games/JurassicPark/CRASH-OPLOSSING.md` van de lader-repo.

## Bench-run 2026-10-12 (autonome ronde) — nieuwe bevinding, geparkeerd

Getracete run (`WINEDEBUG=+seh`, 48 s, BudgieLoader + LD_AUDIT rt-glfix). Resultaat wijkt af van de docs:
- **De rt-glfix-brug laadt niet op het 64-bit niveau:** `ld.so: librt-glfix.so cannot be loaded as audit
  interface: wrong ELF class: ELFCLASS32; ignored`. De brug is 32-bits en wordt via `LD_AUDIT` in de 64-bit
  wine-launcher geduwd, die hem weigert. Of hij alsnog in het 32-bit game-proces laadt is onzeker — de eerdere
  "bedraad en werkt"-conclusie (uit de docs) wordt door de echte run tegengesproken.
- **Geen `0x819c4d80`-crash gereproduceerd** in 48 s; alleen rt-glfix-single-steps (benign) en een los 64-bit
  RPC-exception. De game is door mijn timeout gestopt ("exitcode gestopt"), leefde dus nog. Headless geen beeld
  te zien.
- **Contaminatie:** pre-flight vond al wine-`.exe`-orphans; de run is dus niet schoon.

**Conclusie — geparkeerd als echte blocker.** JP vraagt meerdere dingen die niet autonoom/headless gaan:
(1) de rt-glfix-ELFCLASS-kwestie uitzoeken (laadt de 32-bit audit-lib wel in het 32-bit game-proces?),
(2) de tweede fout met een echte debugger (winedbg/gdb) i.p.v. een blinde +seh-run,
(3) de tv om te zien of er beeld is. Alles serieel op een schone bench. Niet verder autonoom; terug naar de
tractabelere taken (zoals de eigenaar vroeg: echte blockers parkeren en door).

## Debugger-ronde 2026-10-12 (eigenaar aanwezig, schone bench) — diagnose bevestigd, correctie op het bovenstaande

Op verzoek van de eigenaar ("je kunt JP toch oppakken, dan doe je een debugger") een echte debugger-sessie
gedraaid (schone bench, Desktop Mode, één game). Drie dingen uit de vorige ronde zijn **onjuist** gebleken en
worden hier rechtgezet:

1. **De brug láádt wél in het spelproces.** `rt-glfix.log` (deze ronde) laat zien dat de brug ~140 GOT-sloten
   herleidt naar echte GL-adressen (`0xf7c2xxxx`), en de maps tonen `librt-glfix.so` gemapt op `0xf7f3f000` in
   het 32-bit proces. De regel `… ELFCLASS32; ignored` is alleen de **64-bit launcher** die klaagt; de 32-bit
   `ld.so` in het kindproces honoreert `LD_AUDIT` wél. De vorige "brug laadt niet"-conclusie verwarde die
   cosmetische regel met de uitkomst.
2. **De tweede crash is reproduceerbaar.** Met `WINEDEBUG=+seh`: `EXCEPTION_ACCESS_VIOLATION (c0000005)`,
   `eip=0x819c524b`, `ebp=0`, schrijf naar bijna-nul (`info[0]=1` write, `info[1]=0x51`). Zelfde
   `0x819cXXXX`/`ebp=0`-familie als het gedocumenteerde `0x819c4d80`. De game loopt met de brug **~112 s** (24
   threads, een levende engine) tegenover ~14 s zonder — de GL-fix werkt dus, de crash zit erna.
3. **Waarom een los signaal-handler niets ving.** Wine bezit `SIGSEGV` en vertaalt de fout naar een Windows-
   exception die de eigen SEH-handler van het spel afhandelt; een Linux-handler (ook onze opt-in in de brug,
   `RAVENGUN_GLFIX_FAULT`) wordt dus omzeild. Wine's `+seh`-trace levert de registers in plaats daarvan.

**Waar de crash zit (maps van het levende proces, vlak vóór de crash):**
- `eip=0x819c524b` ligt in `80080000-98000000 ---p` — Wine's grote **gereserveerde blok, zonder r/w/x**. `ebp=0`
  erbij = een **wilde indirecte sprong/call** door een kapotte functiewijzer, niet een logische bug in gemapte code.
- De SEH-handler die het opvangt (`0x61023800`) ligt in `msys-2.0.dll` (`61000000`, de MSYS2-runtime van
  BudgieLoader). Die unwindt en de lader stopt met 255.

**Oorzaak (sterk onderbouwd) en de grens.** De game geeft de controle door aan een adres in gereserveerde ruimte:
een **niet-GL, onopgelost import/stub**. ElfLdr2 laat onopgeloste sloten op zijn nagebootste stub staan
(§7 van `docs/rt-glfix.md`: 180 GOT-invoeren → `0x71bb3e80`; de gepakte ELF heeft 72-82% van de importnamen
gewist). De brug zet alleen `gl*`-sloten om; een niet-GL-aanroep via een ongevuld slot springt de reservering in.
Dit ligt dus **voorbij de ABI/GL-laag** die de brug dekt — het verdwijnt pas na reconstructie van ElfLdr2's
resolutietabel (= **uitpakwerk, buiten scope** per `decisions.md` 2026-10-11). Een in-scope ingreep lost het niet
op: een no-op-stub voor een functie die de game echt nodig heeft verplaatst alleen de crash. **JP houdt dus
BudgieLoader** en blijft geparkeerd, nu met een precieze, bevestigde diagnose i.p.v. een vage.

**Als de eigenaar toch verder wil (bench + tv):** (a) interactieve `winedbg` i.p.v. `--auto` (dat kent deze
build niet) om een gesymboliseerde backtrace te krijgen; (b) `RAVENGUN_GLFIX_MODE=15` (MODE_STUBLOG) om te
loggen wélke onopgeloste stub geraakt wordt vóór de sprong — puur ter identificatie, verandert de conclusie niet.
De brug heeft nu een opt-in diagnose (`RAVENGUN_GLFIX_FAULT=1`: foutdump + `prctl(PR_SET_PTRACER_ANY)` zodat een
losse gdb kan aanhaken ondanks `ptrace_scope=1`). Let op: een kale `gdb`-attach gaf toch `EPERM`; `winedbg`
(via Wine's debug-API, niet ptrace) is hier de juiste weg.
