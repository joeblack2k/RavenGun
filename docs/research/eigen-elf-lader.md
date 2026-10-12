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

## De muur voor de ONGEPAKTE games (gemeten op GoGoStrike)
`dlopen(liballeg.so.4.0)` faalt: de lib heeft **477 ongedefinieerde symbolen** (`_poly_scanline_*`,
`_linear_putpixel*`) die **in het spel-executable zelf** staan (gemeten: `bowling_usa` definieert
`_poly_scanline_gcol8` op `0x08096344`). Dat is het klassieke Allegro-4-patroon: de asm-scanline-routines
zitten in de app, en liballeg lost ze op tegen het hoofd-executable.

Een "map-exe + dlopen-libs"-lader kan dat **niet** via `dlopen` oplossen: `dlopen` kan een met de hand
gemapte `ET_EXEC` niet in de globale symboolscope zetten. De systeem-`ld-linux` lukt dit wél omdat die één
link-map bouwt met het executable als wortel. **Twee wegen vooruit:**
- de lader uitbreiden zodat hij de door-het-spel-geleverde symbolen zelf in de GOT's van de gastlibs
  schrijft (ld.so-gedrag nabouwen) — dit is óók nodig voor de gepakte klasse en dus de echte investering;
- of voor de ongepakte games de systeem-`ld-linux` als native route gebruiken (geen BudgieLoader/Wine).
  Gemeten: die route laadt alles en draait de engine tot de echte blocker (zie hieronder).

## De echte blocker van GoGoStrike ligt niet bij de lader
Volledige meting van de eigenaar: `docs/research/gogostrike-native-meting.md`. Kort: de crash in
`uwSramCheckFormatCode` (NULL-deref) is een **symptoom**; de eerste oorzaak is **geluid**
(`install_sound` faalt → vroege uitgang die álle device-init overslaat, inclusief de SRAM-open die de
globale zet). Drie obstakels, in deze volgorde: **(1) geluid**, **(2) MIT-SHM** (SysV-shm per
IPC-namespace; `bwrap --unshare-ipc` isoleert het), **(3) een rw-file-bind** voor de SRAM
(`open(O_RDWR)`+`mmap(MAP_SHARED)`). Zelfs een perfecte lader raakt deze drie; ze staan los van de lader.

## Status
- Lader: werkt t/m mappen+relocaties+libc/libstdc++; muur bij de exe↔lib-symbolen van de ongepakte
  Allegro-games. **Blijft nodig voor de gepakte klasse** (JP/Galaga/Pacman: daar kan `ld-linux` sowieso
  niet, want `push 0xdeadbeef` + verlaagde `nchain`; de lader moet daar op naam resolven).
- GoGoStrike fase 2: de drie obstakels (geluid → MIT-SHM → rw-bind) worden apart aangepakt; de native
  route (systeem-`ld-linux`) is de drager zolang de exe↔lib-uitbreiding niet af is.
