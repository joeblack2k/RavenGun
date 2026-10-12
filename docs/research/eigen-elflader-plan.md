# Eigen ELF-lader: het plan (drie fasen)

*Opgesteld in de coördinatieronde op 2026-10-12. Dit is het plan en het overzicht, **niet** de
meting: de getallen staan in de documenten die eronder genoemd staan. Elke fase noemt wie hem doet
en welk artefact eruit komt.*

## Waarom dit project

`ElfLdr2/BudgieLoader.exe` is de laatste gesloten **lader** in de keten. Zeven games in de
verzameling hangen eraan: Halo, JurassicPark, Terminator, GhostBusters, GoGoStrike, GalagaAssault en
PacmanChomp. Wat er aan gemeten is (`patches/ELF-NATIVE.md`):

* **Zes van de zeven komen niet eens voorbij de dynamic linker** — `error while loading shared
  libraries`, en de eerste ontbrekende bibliotheek is precies een die ElfLdr2 in zijn eigen proces
  emuleert (`libGLU`, `libglut`, `libPhysXLoader`, `libtheora`, `libSDL-1.2`, `libesd`, `liballeg`,
  `libfmod`, `libstdc++.so.5`, `libXrandr`).
* **De ene game waarbij alles resolveert stopt ook.** Galaga Assault heeft geen enkele ontbrekende
  bibliotheek, krijgt zijn venster niet, en sterft met een `SIGSEGV` **zonder één regel uitvoer** —
  ook met onze GL-fix als `LD_PRELOAD`. Met `LD_DEBUG=bindings` is te zien dat de linker daar al
  zijn werk afmaakt (alle `DT_NEEDED` geladen, alle `init`-functies aangeroepen) en dat de crash
  daarna komt, in de opstartcode van de game zelf.

Een paar bibliotheken neerzetten is dus niet genoeg: dit vraagt een **eigen lader**.

## Wat er al is (niet opnieuw bouwen)

* **`scripts/play-native.sh`** — start een game native in een bubblewrap-sandbox, zonder Wine en
  zonder BudgieLoader, met per game een profiel uit `packaging/native-games.json`: de NAS-mappen, waar
  de game ze verwacht, de vooraf te laden bibliotheek, losse bibliotheken, parameters en resolutie. De
  NAS blijft lezend; wat de game schrijft komt in een overlay onder
  `~/.local/share/ravengun/native/<profiel>/`. Kan al `--bind-ro`, `--headless`, `--gamescope`,
  `--strace`, `--gdb`, `--snap`, `--seconds` en `--dry-run`.
* **`packaging/native-games.json`** — zeven profielen (Halo, JurassicPark, Terminator, GhostBusters,
  GoGoStrike, GalagaAssault, PacmanChomp), **alle zeven nu `enabled: false` en `status: geparkeerd`**.
* **`tools/rt-glfix/`** — onze GL-brug; die rijdt nu mee op ElfLdr2's eigen resolutie via `LD_AUDIT`.
  Voor een native route met echte Mesa is die brug in principe niet meer nodig, en dat haalt
  tegelijk de stdcall/cdecl-mismatch weg die `docs/rt-glfix.md` meet.
* **`patches/ELF-LADER-SPEC.md`** — wat voor ELF elke game is en wat een eigen lader moet doen.
* **`patches/ELF-NATIVE.md`** met `patches/elf-native.py` — de losse-startmeting.

## De drie fasen

### Fase 1 — de huidige lader uitpakken en begrijpen

Twee analyses, read-only, geen game gestart. Ze staan bewust naast elkaar: A kijkt naar de lader,
B naar het spelbestand.

| | wat | artefact |
|---|---|---|
| **1A** | `BudgieLoader.exe` uitpakken (secties `.brr0`-`.brr4`) en de **exporttabel** groeperen — dat is de API-oppervlakte die onze lader moet leveren | `docs/research/budgieloader-uitgepakt.md` |
| **1B** | hoe ElfLdr2 de **gewiste importnamen** oplost: `hints.dat` (2.994.888 bytes = **249.574** × 12; 64-bits 7.325.952 = 457.872 × 16), de tweede PLT/slottabel, en wat de `0xb750xxxx`-groep werkelijk is | `docs/research/importresolutie-elfldr2.md` |

### Fase 2 — de eigen lader, op de ongepakte klasse

Een eigen i386-ELF-lader die `PT_LOAD` op zijn `p_vaddr` mapt (het spel is ET_EXEC, niet-PIE, basis
`0x08048000`), `R_386_RELATIVE`/`R_386_GLOB_DAT`/`R_386_JMP_SLOT` toepast met naamresolutie via
`dlsym()` op de host, **TLS opzet** zodat `gs:[0x14]` een geldige en stabiele stackcanarie is,
`DT_INIT` en `DT_INIT_ARRAY` draait en daarna naar `e_entry` springt.

Bewijs: **GoGoStrike** (`bowling_usa`) — ongepakt, en de enige game in de familie die link-compleet
is met alleen `LD_LIBRARY_PATH=<ElfLdr2/libs>`.

Bekende hobbel: de game opent `/dev/w040101sram0` (RW), dat mislukt, en dan geeft een NULL-mmap een
`SIGSEGV` in `uwSramCheckFormatCode`. Recept: een **schrijfbaar SRAM-bestand van minstens `0x10000`
bytes** rw-binden op dat pad (BudgieLoader levert het als `w040101sram`, 65537 B, in de view).
`play-native.sh` kan nu alleen `overlay`/`ro`-mounts van een **map**; daar moet een **rw-file-bind**
bij. De voorbereiding staat klaar in `~/.local/share/ravengun/native/GoGoStrike/`.

### Fase 3 — de gepakte klasse en de API-oppervlakte

Pas te beginnen als 1B antwoord geeft op de naamresolutie. Twee delen:

1. **De gepakte (HASPE-)games**: GalagaAssault, JurassicPark, PacmanChomp en Halo. Deze bestanden
   hebben nul sectiekoppen, PLT-stubs van de vorm `ff 25 <got> 68 ef be ad de e9 <plt0>`, een GOT die
   in het bestand al gevuld is met `0xb7xxxxxx`-adressen uit het oorspronkelijke proces, en 72–82
   procent gewiste importnamen. Zonder de slot-naam-tabel is dit niet te doen; met de tabel wel.
2. **Wat BudgieLoader verder levert**: 173 exports — de volledige freeglut-API plus `GetElfBase`,
   `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface`,
   `AmdPowerXpressRequestHighPerformance`, `NvOptimusEnablement`, `__ctype_b_loc`, `api`, `get_info` —
   en de nagebootste bibliotheken in `libs/`. Voor een native route met echte Mesa valt een deel
   daarvan weg, maar `GetElfBase`/`GetElfSymbol` en de omgevingsafspraken moeten we zelf bieden.

## Waar de moeilijkheid zit (eerlijk)

1. **De gewiste namen.** Van Jurassic Park hebben 472 van de 577 importnamen geen naam meer in het
   bestand. De GOT-waarden zijn echte, 16-byte uitgelijnde functieadressen uit het oorspronkelijke
   proces (573 van 575) in 20 bibliotheek-grote clusters, maar met de bibliotheken die wij hebben zijn
   ze **niet** naar namen te herleiden: 0 van de 93 libGL-offsets landt op een symbool, en over 74
   32-bits bibliotheken is de beste clusterscore 15 van 143 — ruis. Zie `patches/ELF-LADER-SPEC.md`,
   sectie "Zitten de namen écht niet in het bestand?". **Hier hangt fase 3 aan.**
2. **TLS en de canarie.** De stackcanarie wordt aan het begin gelezen en aan het eind vergeleken, dus
   `gs:[0x14]` moet een geldige en stabiele waarde zijn. Dat is precies wat een echte Linux-lader voor
   je regelt, en wat een eigen lader onder Wine niet vanzelf doet.
3. **De ABI.** Wine's `opengl32.dll` exporteert GL als **stdcall** terwijl de games **cdecl** aanroepen
   (`docs/rt-glfix.md`: 20 van 20 gemeten). Met echte Mesa-bibliotheken is dat risico weg — dat is een
   argument vóór de native route, niet ertegen.
4. **Meten met één game tegelijk.** `starttest-batch.sh` doet `pkill -f "$BASIS"`, en zes games in
   deze verzameling hebben de bestandsnaam `game`. Gelijktijdige proeven meten dus elkaar. Wie meet,
   kijkt eerst of er nog een `wine`- of gameproces loopt.

## Stand

| fase | wie | stand |
|---|---|---|
| 1A — BudgieLoader uitpakken | subagent | loopt |
| 1B — importresolutie | subagent | loopt |
| 2 — eigen lader, GoGoStrike | Claude (lader-repo) | loopt |
| 3 — gepakte klasse + API-oppervlakte | nog niet begonnen | wacht op 1B |
