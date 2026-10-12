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
`docs/rt-glfix.md` §6-7, `findings/games/JurassicPark/CRASH-OPLOSSING.md`, `patches/ELF-LADER-SPEC.md` r.37-92,
`docs/research/elfldr2-injectie.md` r.23-78.

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
