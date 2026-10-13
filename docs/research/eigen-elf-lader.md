# Eigen i386-ELF-lader (rg-elfloader) — stand fase 2

*Bron: `tools/rg-elfloader/rg_elfloader.c`, bouwen met `scripts/build-elfloader.sh`. Dit is de
"eigen native lader" uit `patches/ELF-LADER-SPEC.md` ("Wat een eigen lader moet doen"): een ElfLdr2-game
starten zonder BudgieLoader en zonder Wine.*

## Wat de lader doet (gemeten werkend tot de muur hieronder)
1. Parseert de i386-ELF, mapt de `PT_LOAD`-segmenten op hun vaste `p_vaddr` (ET_EXEC, niet-PIE): één
   anonieme RW-mapping over `[min,max]`, per segment de `p_filesz` erin via `pread` (bss blijft nul),
   daarna `mprotect` naar de einde-rechten. Gemeten op GoGoStrike: beeld `0x8048000..0x8325000` (2932 KB).
2. `dlopen` van de `DT_NEEDED`-libs (uit `ElfLdr2/libs` + `runtime/lib32` via `LD_LIBRARY_PATH`);
   `libc.so.6`/`libm.so.6` zijn al in het proces (geen "twee libc's").
3. Relocaties: `R_386_COPY` (grootte uit `st_size`), `R_386_GLOB_DAT`/`JMP_SLOT` (= opgezocht adres via
   `dlsym`), plus `RELATIVE`/`32`/`PC32`.
4. TLS: erft de eigen glibc-`%gs`. GoGoStrike heeft 0 TLS-relocaties en deelt dezelfde `libc.so.6`, dus
   `gs:[0x14]` is al een geldige, stabiele stackcanarie — geen eigen TLS-opzet nodig.
5. Bouwt een System V-startstack (argc/argv/envp/auxv met AT_PHDR/AT_ENTRY/AT_RANDOM…) en springt naar
   `e_entry` (`_start`); `__libc_start_main` draait daarna zelf de init-array (dus geen dubbele `DT_INIT`).
   Bouwdetail: `-Wl,-z,execstack`, want oude gastlibs (liballeg, 2007) vragen een uitvoerbare stack die
   moderne glibc anders bij `dlopen` weigert.

## De laadorde van de bibliotheken (2026-10-13)
De lader loopt de `DT_NEEDED`-lijst van het spel af met `dlopen(RTLD_NOW | RTLD_GLOBAL)`. Een lib die niet
laadt gaat op een wachtlijst en wordt opnieuw geprobeerd zolang een ronde iets oplevert; pas wat daarna
overblijft is fataal (met de naam en de laatste fout). Dat is nodig omdat de dynamische linker van het systeem
eerst alle libs in de globale zoekruimte zet en dan pas relocaties toepast, en oude libs daarop leunen:
`libGLU.so.1` uit `ElfLdr2/libs` heeft **nul** `DT_NEEDED` en gebruikt `floor` (libm, nr. 9 in de lijst van
Terminator) en `operator delete[]` (libstdc++, nr. 13) terwijl hij zelf nr. 2 is. Een lijst "systeemlibs eerst"
is het niet geworden: welke lib op welke leunt staat nergens, en zo blijft de volgorde voor alles wat meteen
laadt die van het spel. `--ontleed-alleen` en de gewone modus gebruiken dezelfde lus.

Grenzen: de zoekvolgorde van de globale scope is de laadvolgorde, dus een lib uit een latere ronde staat
achteraan (gemeten voor Terminator: `libGLU` en `libtheora` delen geen enkel functie- of datasymbool met de
andere elf, dus daar maakt het niets uit). En de oude libs eisen een uitvoerbare stack (`libGLU.so.1` heeft geen
`PT_GNU_STACK`): de lader wordt met `-Wl,-z,execstack` gebouwd, `tests/elfloader_install_test.sh` controleert
dat de geïnstalleerde lader `GNU_STACK RWE` heeft, en zonder die vlag strandt hij al op zijn eigen shim.

De shim wordt met gcc gebouwd; die 64-bits hulpprocessen krijgen de `LD_PRELOAD`/`LD_AUDIT` van het spel niet
mee (anders zes regels `wrong ELF class: ELFCLASS32` per start). De gast ziet de variabele daarna weer.

## De shim: gast-exports in de globale scope (opgelost)
De gastbibliotheken (liballeg.so.4.0) importeren honderden symbolen **uit het spel-executable zelf**
(`_poly_scanline_*`, `_linear_putpixel*`; gemeten: `bowling_usa` definieert `_poly_scanline_gcol8` op
`0x08096344`) — het Allegro-4-patroon: de asm-scanline-routines zitten in de app. In de kernel-route is
dat gratis: de gast **is** het hoofdprogramma, dus zijn dynamische symbolen staan vanzelf in de globale
zoekruimte (`--export-dynamic`). Onze lader is zelf het hoofdprogramma; de gast is losse gemapte
geheugen, dus die symbolen staan er niet.

**Oplossing (bewezen):** de lader bouwt een *shim*-bibliotheek met de gedefinieerde dynamische symbolen
van de gast als **absolute** adressen (`.set naam, 0xADDR`), compileert die (`gcc -m32 -shared -fPIC
-Wl,-z,execstack`) en laadt hem **als eerste** met `dlopen(RTLD_NOW|RTLD_GLOBAL)`. Daarna lossen de
gastbibliotheken hun imports tegen de shim op. De adressen kloppen omdat de gast ET_EXEC op een vast
adres staat; de shim moet dus **per gast** opnieuw gemaakt worden (gebeurt automatisch bij elke start).
Linker-/CRT-symbolen worden overgeslagen (`_init`, `_fini`, `_start`, `_DYNAMIC`, `__bss_start`,
`_edata`, `_end`, `__data_start`, `__init_array_*`, … ) om "multiple definition" te vermijden.

## Gemeten resultaat op GoGoStrike (lader GEVALIDEERD)
Met de shim draait **onze eigen lader** GoGoStrike tot exact hetzelfde punt als de kernel-route:
```
shim geladen: 1385 gast-symbolen in de globale scope
geladen: libstdc++.so.5 / liballeg.so.4.0 / libm.so.6 / libNL.so.1.6 / libc.so.6
DT_REL: 64 COPY, 1 onopgelost (__gmon_start__, weak -> niet fataal)
DT_JMPREL: 132 toegepast, 0 onopgelost
spring naar e_entry 0x8059410  (argc 1, envc 53, 17 auxv)
Shutting down Allegro due to signal #11
```
Backtrace van de crash (gdb): `#0 0x08071915` (= `uwSramCheckFormatCode+5`, in de **gast**) ← `#1 main`
(liballeg) ← `#2/#3 __libc_start_main` (libc) ← `#4 0x08059431` (`_start` van de gast). **Identiek aan de
kernel-route.** De crash zit dus in de game-engine op het bekende sound→SRAM-obstakel (ander agent), niet
in de lader. Daarmee is de lader-pijplijn (mappen → shim → alle libs → relocaties → TLS → SysV-stack →
sprong) bewezen voor de ongepakte klasse.

Twee bugs die deze meting blootlegde en die nu gefixt zijn:
1. **Dangling `b->eh`:** `start_spel` las `e_entry`/`e_phnum`/`e_phentsize` uit het *bestand*, dat vlak
   daarvoor ge-`munmap`t werd (en waar daarna een lib-mapping overheen kwam) → crash in `main+1924`
   (`movzwl 0x2a(%ecx)` = `e_phentsize`). Nu worden die waarden als losse velden bewaard vóór de `munmap`.
2. **`-rdynamic`:** exporteerde de `main` van de lader zelf, waardoor het door-de-gast-geïmporteerde
   `main` (Allegro levert `main`) naar de **lader**-`main` oploste → de lader draaide zijn eigen main
   opnieuw. `-rdynamic` is verwijderd; `main` lost nu naar liballeg op.

Voor de ongepakte klasse is dit vooral een validatie: de kernel-`ld-linux` draagt die games sowieso (de
echte blockers zijn sound/MIT-SHM/rw-SRAM, ander agent). De lader is **nodig voor de gepakte klasse**.

## De echte blocker van GoGoStrike ligt niet bij de lader
Volledige meting van de eigenaar: `docs/research/gogostrike-native-meting.md`. Kort: de crash in
`uwSramCheckFormatCode` (NULL-deref) is een **symptoom**; de eerste oorzaak is **geluid**
(`install_sound` faalt → vroege uitgang die álle device-init overslaat, inclusief de SRAM-open die de
globale zet). Drie obstakels, in deze volgorde: **(1) geluid**, **(2) MIT-SHM** (SysV-shm per
IPC-namespace; `bwrap --unshare-ipc` isoleert het), **(3) een rw-file-bind** voor de SRAM
(`open(O_RDWR)`+`mmap(MAP_SHARED)`). Zelfs een perfecte lader raakt deze drie; ze staan los van de lader.

## Gemeten resultaat op de GEPAKTE klasse (Galaga Assault, eigenaar)
Met dezelfde lader + shim springt ook de gepakte Galaga naar `e_entry` en **de gast draait**:
```
shim geladen: 4607 gast-symbolen in de globale scope
spring naar e_entry 0x81150a7 (argc 1, envc 22, 17 auxv)
SIGSEGV  #0 0xb7508f60  #1 0x080890f9 (gast)  #2 0x0809f0df (gast)  #3 0x081150ca (net na entry)
```
De crash zit in de **gast**, op een adres in het `0xb750xxxx`-bereik: de **voorgevulde GOT** uit het proces
op de originele kast, dat hier niet bestaat. Dat is precies de voorspelde gepakte-klasse-kwestie — en nu een
**runtime**-probleem met een reproduceerbaar adres, geen uitpakwerk. Ter contrast: de kernel-`ld-linux` geeft
op ditzelfde bestand SIGSEGV met **0 bytes** uitvoer. Onze lader is er dus strikt vóór.

## Status
- **Lader gevalideerd** (beide klassen): mapt PT_LOAD, bouwt de shim, laadt alle DT_NEEDED-libs, past de
  relocaties toe, bouwt de SysV-startstack en springt naar `e_entry`; **de gast draait**. De crash is in
  beide gevallen ná de lader, in de gast/engine.
- **Ongepakt** (GoGoStrike): tot `uwSramCheckFormatCode` (sound→SRAM), identiek aan de kernel-route. De echte
  blockers (geluid → MIT-SHM → rw-SRAM-bind) liggen buiten de lader (ander agent). De kernel-`ld-linux` draagt
  de ongepakte games sowieso; de lader is hier vooral de validatie.
- **Gepakt** (Galaga/JP/Pacman): de lader is hier *nodig* (kernel geeft 0 bytes). Er zijn **twee parallelle
  PLT/GOT-systemen** (gemeten, eigenaar): de `DT_JMPREL`-relocaties horen bij het **tweede** systeem, maar de
  stubs die de game aanroept zitten in het **eerste** PLT (absoluut, `jmp *0x0830d2fc`, `push 0xdeadbeef`), met
  GOT-slots die de voorgevulde `0xb7xxxxxx`-waarde houden (bv. `0xb7508f60` = `fopen`).
  **Fase-3-reparatie (geïmplementeerd + gemeten):** de lader wint namen statisch terug — elke JMPREL-entry
  heeft een naam; waar de tweede-tabel-inhoud een stale `0xb7`-adres is, levert dat een map adres→naam; elk
  eerste-PLT-slot met datzelfde stale adres krijgt die naam en wordt met `dlsym` hersteld
  (`bouw_stale_map` + `repareer_eerste_plt`, gated op de map → veilig, no-op op ongepakte games).
  **Gemeten op Galaga:** 100 JMPREL-entries → 33 stale-met-naam → **31 van de 357 eerste-PLT-slots op naam
  hersteld** (`connect`, `getpid`, `realloc`, `memset`, `fopen`, …). De crash verschoof van `0xb7508f60`
  (`fopen`, hersteld) naar `0xb750cef0` — een onbenoembaar slot. **31/357 is het statische plafond** (geen
  afleidbare volgorde-regel; de 258 gewiste imports hebben nergens een resolutietabel; de dump bevestigt dat
  ElfLdr2 de eerste-GOT óók nooit repareert — Galaga crasht ook onder BudgieLoader). Bron-tabel:
  `docs/research/resolutietabel-elfldr2.tsv` (let op: de `doel`-kolom = BudgieLoader-interne adressen; onze
  native lader lost de **namen** op naar de host-libc via `dlsym`, niet die adressen).
  **No-op-trampolines voor de rest** (zoals ElfLdr2): elk onbenoemd stale `0xb7`-slot krijgt een cdecl-no-op
  (`55 8b ec 33 c0 5d c3`, geeft 0). Gemeten Galaga: 31 op naam + **325 no-op** (356/357). De crash verschuift
  daarmee **de gast in** (`0x08089212`, gemapte game-code) i.p.v. een wilde sprong naar niet-gemapte ruimte: de
  game loopt verder tot hij de 0-retour van een no-op als pointer gebruikt. Verder is niet mogelijk zonder de
  originele dump-data voor die imports. (`RG_ELF_NO_TRAMPOLINE=1` zet de trampolines uit voor debuggen.)
- **Hardening (later, geen blocker):** het gastbeeld staat nu `rwxp`; datasegmenten hoeven geen `x`.

## In de play-native-keten (`--loader`) + smalle shim
`scripts/play-native.sh --loader` (of profielveld `"loader": "rg-elfloader"`) zet onze lader in de keten in
plaats van de kernel-`ld.so`: de lader-binary wordt in de sandbox gebonden (`/rg/bin/rg-elfloader`) en vóór
de game-exe gezet; hij bouwt de shim met de in `/usr` aanwezige `gcc` en schrijft die naar
`XDG_RUNTIME_DIR` (schrijfbaar in de sandbox).

**Smalle shim (bijgesteld):** de shim leest nu uit de lib-BÉstanden welke symbolen de NEEDED-libs zelf
definiëren (`verzamel_lib_defs`) en laat die weg — **behalve** COPY-reloc-doelen (`screen`, `font`,
`_blender_*`, `_accel_*`): die blijven in de shim, want de libs moeten ze met de gast delen
(ld.so-canonicalisatie). Netto verdwijnen de code-preempties, blijven de data-globals gedeeld, en worden
de gast-functies die de libs nodig hebben (`_poly_scanline_gcol8`) geleverd. Gemeten: GoGoStrike-beeld
ongewijzigd t.o.v. de brede shim, Galaga blijft 31 slots herstellen.

**Signal 11 bij GoGoStrike via `--loader` — OPGELOST (R_386_COPY-bron).** De crash zat in Allegro's X11-pad
(`_xwin_update_screen → textprintf → textout`, `eax=0` = NULL-lettertype), op een tweede LWP die liballeg
zelf maakt. Oorzaak (eigenaar via coredumpctl, bevestigd): onze **COPY-relocatie** vulde de gast-kopie van
`font`/`screen` niet. De COPY-bron werd via `dlsym(RTLD_DEFAULT)` gezocht, maar dat vindt — doordat de shim
vooraan de globale scope staat — de **gast-kopie zelf** (de shim), dus `memcpy(waar, waar)` kopieerde nul op
nul. `_cos_tbl` bleef 0 i.p.v. `0x00010000`; `font` bleef 0 i.p.v. `&_default_font`. **Fix:** `R_386_COPY`
haalt de beginwaarde nu uit de **definiërende lib** (`zoek_in_libs`, `dlsym(lib-handle, naam)`), ná diens
eigen relocaties (dlopen `RTLD_NOW`). De COPY-symbolen blijven in de shim (gedeeld), zodat een runtime-schrijf
door de lib (bv. `set_gfx_mode` zet `screen`) de gast-kopie bereikt. **Gemeten: GoGoStrike draait nu 30 s via
`play-native.sh --loader`** (onze rg-elfloader, geen Wine/BudgieLoader; exit 143 = timer). Daarmee is fase 2
óók via onze eigen lader rond, niet alleen via de kernel-route. Bekende losse bug: onder `--gdb --loader`
laadt `liballeg` niet (de shim staat daar niet op tijd klaar) — alleen de debug-route, laag geprioriteerd.

## Noot (2026-10-12): Go Go Strike is uit de app en van de NAS verwijderd
De eigenaar heeft vier games definitief verwijderd (Galaga Assault, Pacman Chomp-Mania, Go Go Strike,
Gashaaaan Refill). Daarmee is **Go Go Strike** weg — de enige game waarop onze eigen lader
**einde-tot-einde** bewezen was (`rg-elfloader` → `bowling_usa`, 25-30 s, exit 143, eigen assertscherm). Die
proef is dus **niet meer herhaalbaar**; de bewijsstukken blijven in de geschiedenis (het plan en, indien
aanwezig, `docs/screenshots/gogostrike-loader-assert.png`). De lader-code en de metingen hierboven blijven
geldig als projectgeschiedenis — als de test ooit "stuk" lijkt: hij is niet stuk, het spel bestaat niet meer.
De gepakte-klasse-waarde van de lader (JurassicPark/Halo/Terminator blijven) staat hier los van.
