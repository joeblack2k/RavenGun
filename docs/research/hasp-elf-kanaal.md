# Het kanaal naar het ELF-proces van een ElfLdr2-game

*RavenGun, ronde 12 (2026-10-11). Hoe krijgt de HASP-koppeling code in het proces van een
ElfLdr2-game? Alle cijfers zijn gemeten. Dit vult `docs/research/elfldr2-injectie.md` aan (dat gaat
over de GL-fix en de zoekpaden) met wat de HASP-kant nodig had.*

## De vraag

De koppeling van `tools/rt-hasphook/` moet in het ELF-proces van de game komen om de statisch
gelinkte `hasp_*`-functies te kunnen aanhaken. `docs/research/elfldr2-injectie.md` stelt vast dat
`LD_PRELOAD` niet kan en dat `ElfLdr2/libs/` op het zoekpad staat. Wat is het kanaal dat in de
praktijk werkt?

## Het antwoord: LD_AUDIT, en het proces heet wine-preloader

`LD_PRELOAD` sterft aan Cygwin (gemeten door de HASP-agent, zie `docs/hasp-layer.md` §4). `LD_AUDIT`
kent Cygwin niet en glibc wel — dat is het kanaal dat de GL-fix al gebruikt
(`patches/JurassicPark/dropin/dropin.ini`). Dezelfde weg werkt voor de HASP-koppeling.

Gemeten met een meetbibliotheek die als `LD_AUDIT` meeliep en zijn eigen `/proc/self/exe` en
`/proc/self/cmdline` opschreef:

```
[ravengun-hasphook] === ravengun-hasphook (pid 1154857, wine) ===
```

en in een eerdere meting:

```
/proc/self/exe     = /home/deck/.local/share/ravengun/wine/proton-11.0-2/lib/wine/i386-unix/wine-preloader
/proc/self/cmdline = …/lib/wine/i386-unix/wine
                     …/runtime/TeknoParrotUi-2.0.0.130-linux-x64/pipehelper.exe
                     TeknoParrot_JVS
                     127.0.0.1
                     42817
                     …
```

Het proces waarin de constructor draait is dus de **32-bits `wine-preloader`**, niet BudgieLoader en
niet de game. `file` op die preloader:

```
ELF 32-bit LSB executable, Intel i386, statically linked, stripped
```

## Het spelbeeld staat er al in

Van buitenaf gemeten, met een lus die `/proc/<pid>/maps` van het spelproces volgt vanaf de start:

```
t=2 pid=1168665: 0x08048000 IS afgebeeld:
    08048000-09128000 rwxp 00000000 00:00 0
```

Jurassic Park (17 MB) staat dus al op zijn vaste adres `0x08048000` vanaf het begin van de start, met
`rwxp` — het beeld is **schrijfbaar én uitvoerbaar**. Dat is de reden dat de koppeling de functies
zonder `mprotect` kan aanhaken.

Het is hetzelfde beeld dat de GL-fix-agent in zijn logboek zag:

```
[rt-glfix JurassicPark] spelbeeld gevonden op 0x08048000 (11091968 bytes)
```

De koppeling vindt het op dezelfde manier, maar vergelijkt de ELF-kop in het geheugen met die van het
bestand (klasse, codering, machine, type, ingangspunt, `e_phoff`, `e_phnum`). Alleen naar de
ELF-magic kijken is niet genoeg: in dit proces staan meer 32-bits ELF's, en dat gaf eerst een valse
treffer op een bibliotheek.

## De gevaren van dit kanaal (gemeten)

Een `LD_AUDIT`-bibliotheek komt in **elk** Wine-proces, ook in de 64-bits processen
(`wineserver`, `winedevice.exe`). Daar meldt `ld.so`:

```
ERROR: ld.so: object '…/libravengun-hasphook.so' cannot be loaded as audit interface:
       wrong ELF class: ELFCLASS32; ignored.
```

Dat is onschadelijk: de 64-bits processen negeren de bibliotheek. In het 32-bits proces wordt hij
wél geladen en draait zijn constructor.

Twee dingen die het proces wél laten vallen, en die de koppeling daarom vermijdt:

1. **`dl_iterate_phdr` vanuit de kijker-thread.** De laadvergrendeling van glibc is van de thread die
   de constructor uitvoert; een andere thread die `dl_iterate_phdr` aanroept, wacht daarop en het
   spel start nooit. De koppeling vraagt het laadadres van het hoofdprogramma daarom **één keer in de
   constructor** op.
2. **Een lezing van het spelbeeld die als "leesbaar" in `/proc/self/maps` staat maar het niet is.**
   ElfLdr2 beeldt het beeld in delen af; een pagina die net is toegewezen maar nog niet gevuld is
   geeft `SIGBUS`. De koppeling vangt `SIGSEGV` en `SIGBUS` rond die lezing af
   (`hh_is_spelbeeld_veilig`) en behandelt een fout als "nog niet klaar".

Een derde, niet volledig verklaard: een `LD_AUDIT`-bibliotheek met een constructor die **veel** doet
(de meetprobe liep de GOT van het spel na en las het beeld) laat `wine-preloader` sterven met een
`SIGSEGV` — `coredumpctl` noemt steeds `wine-preloader` als het stervende proces. De koppeling is
terughoudend (alleen de dynsym van schijf lezen en de ingangspunten patchen) en laat het spel gewoon
doorlopen.

## Wat er in de praktijk gebeurde bij een echte start

Jurassic Park, met de koppeling als `LD_AUDIT`:

```
[ravengun-hasphook] === ravengun-hasphook (pid 1178227, wine) ===
[ravengun-hasphook] spel-ELF gelezen uit …/views/JurassicPark/game (cmdline Z:-pad): 17694720 bytes
[ravengun-hasphook] aantal symbolen uit DT_HASH: 18811
[ravengun-hasphook] 26 hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)
[ravengun-hasphook] spelbeeld op 0x08048000
```

De koppeling komt dus in het proces, leest de dynsym en vindt het beeld. Het spel komt tot
`Starting Game` en sterft dan aan zijn eigen OpenGL-start (de bekende blokkade van deze game, zie
`patches/JurassicPark/game.ini` `[Status] Note`) — met en zonder de koppeling, 3 van 3 keer. Het
bereikt zijn HASP-controle nooit, dus een `hasp_login` van deze game is in deze omgeving niet op te
vangen. Zie `docs/hasp-elf-hook.md` §4.3.

## Het zoekpad (voor de volledigheid)

BudgieLoader meldt het zelf bij de start:

```
┝ library path: z:/home/deck/.local/share/ravengun/runtime/views/jurassicpark/elfldr2/libs
```

Die map staat dus op het zoekpad van de game. De view heeft geen eigen `ElfLdr2/`: het is een
symlink naar de gedeelde runtime

```
$ readlink -f ~/.local/share/ravengun/runtime/views/JurassicPark/ElfLdr2
/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2
```

dus een drop-in met `To = ElfLdr2/libs` zou in de **gedeelde** map schrijven en de andere zeven
ElfLdr2-games beïnvloeden. Dat is dezelfde conclusie als in `docs/research/elfldr2-injectie.md` §3.
Voor de HASP-koppeling is dat niet nodig: `LD_AUDIT` is het kanaal, en dat is per start te zetten.

## De afspraak die hieruit volgt

De launcher zet het pad van een `Preload = true`-drop-in als **`RG_DROPIN_PRELOAD`** en niemand leest
dat (`docs/hasp-layer.md` §9, punt 2). De afspraak hoort te zijn:

> Zet `RG_DROPIN_PRELOAD` in **`LD_AUDIT`** van het Wine-proces, niet in `LD_PRELOAD`.

Dan werkt de koppeling zonder dat er iets in de gedeelde `ElfLdr2/libs/` komt, en blijft het per
game. `patches/JurassicPark/dropin/dropin.ini` beschrijft die route al voor `librt-glfix.so`; de
drop-in van deze koppeling volgt dezelfde weg.
