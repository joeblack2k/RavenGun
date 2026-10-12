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
- **Gepakt** (Galaga/JP/Pacman): de lader is hier *nodig* (kernel geeft 0 bytes). Volgende taak (fase 3): de
  voorgevulde GOT-slots. **Gemeten door de eigenaar (Galaga):** er zijn **twee parallelle PLT/GOT-systemen**.
  De `DT_JMPREL`-relocaties (die onze lader correct op hun `r_offset` toepast) horen bij het **tweede** systeem
  (tabel vanaf `0x086f83f8`, aangesproken via `%ebx`). De stubs die de **game** aanroept zitten in het
  **eerste** PLT (absoluut, `jmp *0x0830d2fc`, `push 0xdeadbeef` — index vernietigd) en hun GOT-slots houden de
  stale `0xb7xxxxxx`-waarde (bv. `0xb7508f60` = `fopen`). Onze JMPREL-reparatie landt dus in de verkeerde
  tabel. **Fase-3-taak:** het **eerste** PLT repareren — en de naam is daar niet uit JMPREL te lezen. Open
  vraag (eigenaar onderzoekt): de correspondentie eerste-PLT-stub ↔ naam (het tweede PLT pusht een lopende
  waarde `0x0,0x8,0x10,…` die mogelijk een derde tabel indexeert). Zodra die correspondentie vaststaat, is de
  laderfix: per eerste-PLT-stub de echte functie op naam oplossen en in diens **eigen** GOT-slot schrijven.
  Zie het plan (`tools/rt-glfix` doet een verwante PLT-stub-parse op de BudgieLoader-route).
- **Hardening (later, geen blocker):** het gastbeeld staat nu `rwxp`; datasegmenten hoeven geen `x`.
