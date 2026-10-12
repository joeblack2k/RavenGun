# Hoe ElfLdr2 de gewiste importnamen "oplost" — meting in plaats van vermoeden

*Read-only onderzoek. Geen game gestart, geen `BudgieLoader` gestart, geen Wine gestart, niets op de
NAS geschreven, geen git-mutatie. Alle getallen komen uit de scripts `re/h1…h19`; elk script staat in
de Windows-werkmap `re/` **én** in `/home/deck/`, met de uitvoer als `*.out` ernaast, zodat elk cijfer
na te rekenen is.*

Dataset: `…/Teknoparrot/{Galaga Assault,Jurassic Park,Pacman Chomp-Mania}/game` (+ de
`game_original`-varianten van JP en Pacman), en de runtime
`…/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/` met `hints.dat` (2.994.888 B),
`x64/hints.dat` (7.325.952 B) en `.version` = `TeknoParrotElfLdr2Core_1.0.0.1434`.

---

## 0. De kern in één alinea

**ElfLdr2 lost de gewiste namen niet op — het hoeft niet, en het kan niet.** Voor de imports die de
pakker leesbaar liet (99 / 105 / 199 per game) staat er een echte `DT_JMPREL`-relocatie met een echte
`dynsym`-naam; die worden normaal geresolveerd. Voor de overige imports (258 / 472 / 248 per game)
staat er **niets** in het bestand: geen relocatie, geen dynsymeintrag, en de `push 0xdeadbeef` maakt
luie binding onmogelijk. Het enige dat overblijft is de **al vooraf ingevulde GOT-waarde** uit het
proces van de pakker. ElfLdr2 zet in die slots zijn eigen trampolines — levend gemeten: **570 van 576**
slots wijzen in anonieme `r-xp`-mappings en **180× dezelfde** cdecl-no-op die 0 teruggeeft.
`hints.dat` speelt daarbij geen rol: de kruistoets is **0 van 357 / 577 / 442**.

---

## 1. Wat er werkelijk in de bestanden staat

### 1.1 De twee PLT's (zelf gescand, niet uit de NAS-tabel)

`re/h3_plt_scan.py` zoekt `ff 25 <got32> 68 ef be ad de e9 <plt0>`; `re/h5_dynamic.py` zoekt daarnaast
het PIC-patroon `ff a3 <got-offset> 68 <index> e9 <plt0>`. Alles via de `PT_LOAD`-programmaheaders
(`SHNUM = 0`, dus geen sectiekoppen). Mijn scanner geeft exact dezelfde aantallen als `PLT-slots.md`
(357 / 577 / 442), dus de basis is onafhankelijk nagerekend.

| game | grootte | entry | eerste PLT `push 0xdeadbeef` | `push <echte index>` | tweede PLT (PIC) | GOT `0xb7…` | GOT lui (`stub+6`) |
|---|---|---|---|---|---|---|---|
| Galaga `game` | 7.278.592 | `0x081150a7` | **357** | 0 | 100 stubs @ `0x08658b10` | **356** | 0 |
| JP `game` | 17.694.720 | `0x0813befc` | **577** | 1 | 101 stubs @ `0x09047b10` | **576** | 0 |
| JP `game_original` | 17.690.127 | `0x0904a8da` | **577** | 1 | 101 stubs (identiek) | **0** | **577** |
| Pacman `game` | 6.852.608 | `0x08077f73` | **436** | **6** | 191 stubs @ `0x0837c160` | **441** | 0 |
| Pacman `game_original` | 4.144.655 | `0x0835f8da` | **285** | 0 | 101 stubs @ `0x0835cb10` | **0** | **285** |

De tweede PLT is een klassieke i386-PIC-PLT. De bytes op Galaga `0x08658b10`:

```
ff a3 0c 00 00 00 | 68 00 00 00 00 | e9 e0 ff ff ff   jmp *0xc(%ebx); push 0;   jmp plt0
ff a3 10 00 00 00 | 68 08 00 00 00 | e9 d0 ff ff ff   jmp *0x10(%ebx); push 8; jmp plt0
```

en de tweede slottabel (Galaga `0x086f83f8`) bevat de **luie** ingangen van die stubs:
`0x08658b16`, `0x08658b26`, `0x08658b36`, … = `stub+6`. Standaard onopgeloste PLT-toestand.

### 1.2 De tweede slottabel en `DT_JMPREL` (vraag 2)

| game | `DT_JMPREL` | `DT_PLTRELSZ` | = records van 8 B | allemaal met naam | waar de `r_offset`s heen wijzen |
|---|---|---|---|---|---|
| Galaga `game` | `0x0807f840` | `0x320` = **800** | **100** | **100/100** | alle 100 naar de tweede tabel `0x086f83f8…0x086f8588`; **0** in de eerste GOT |
| JP | `0x081384d8` | `0x328` = **808** | **101** | **101/101** | 1 naar de eerste GOT (`0x08add278`, `fork`), 100 naar `0x090e73f8…0x090e7588` |
| Pacman `game` | `0x083614d0` | `0x618` = **1560** | **195** | **195/195** | 6 naar de eerste GOT (`XUngrabPointer`, `XGrabPointer`, `XDestroyWindow`, `XUnmapWindow`, `XMapWindow`, `fork`), 189 naar `0x0868dd60…0x0868e058` |
| Pacman `game_original` | `0x08072018` | `0x328` = **808** | 101 | 101/101 | 1 naar de eerste GOT (`fork`), 100 naar `0x083fc3f8…` |

Alle relocaties zijn `R_386_JMP_SLOT` (type 7), `DT_PLTREL = 17`. De symbolen zijn libc-, socket- en
X11-functies: `fileno`, `__errno_location`, `open`, `connect`, `localtime_r`, `getpid`, `strerror`,
`malloc`, `free`, `fork`, `XGrabPointer`, …

**`DT_PLTRELSZ` klopt exact — nameting, want hierover bestond verschil van inzicht.**
`re/h17_jmprel_einde.py` leest de ruwe `DT_PLTRELSZ`, dumpt de tabel ruim uit (4× de opgegeven lengte)
en telt:

```
Galaga   DT_PLTRELSZ = 800  (= 100 records)   eerste niet-JMP_SLOT-record op index 100
JP       DT_PLTRELSZ = 808  (= 101 records)   eerste niet-JMP_SLOT-record op index 101
Pacman   DT_PLTRELSZ = 1560 (= 195 records)   eerste niet-JMP_SLOT-record op index 195
```

In alle drie de gevallen is `DT_PLTRELSZ / 8` **precies** het aantal records dat een geldige
`r_offset` én `r_info` én een bestaande naam heeft, en het eerstvolgende record is precies op die
index geen `JMP_SLOT` meer. De lengte is dus **niet** opgeblazen. Wat er ná de tabel staat is andere
data: bij Galaga staat op index 363 bijvoorbeeld `r_offset = 0x0830d008`, midden in de eerste GOT —
dat is geen relocatietabel. Wie `DT_PLTRELSZ` als *aantal records* leest in plaats van als *bytes*
krijgt ~800 entries waarvan er ~100 kloppen en de rest rommel; dat verklaart de melding "de overige
hebben `r_offset = 0`". Voor de betrouwbare grens maakt het niet uit: **het aantal niet-nul entries is
de juiste maat, en dat is 100 / 101 / 195.** Zie §6, punt 4.

**De verhouding tussen de twee tabellen.** Ze zijn geen twee helften van dezelfde importlijst maar een
tweeledige pakkerstructuur:

* **tabel 2** = de imports die leesbaar mochten blijven: echte relocaties, echte `dynsym`-namen, echte
  PIC-PLT. `ld.so` kan die zonder meer oplossen.
* **tabel 1** = de imports die verborgen moesten worden: per stub `push 0xdeadbeef`, **geen**
  relocatie, **geen** dynsymeintrag, en een GOT-slot dat in het bestand al een absoluut adres bevat.

De 6 Pacman-`r_offset`s die wél in de eerste GOT vallen zijn exact de 6 stubs met een echte index —
de twee structuren zijn dus netjes gescheiden.

### 1.3 Zijn de namen gewist? Ja — en de grens is `DT_STRTAB − DT_SYMTAB`, niet `nchain`

Hier ging een eerdere lezing (en mijn eigen eerste meting) de fout in. **`DT_HASH.nchain` is in deze
bestanden lager dan het werkelijke aantal dynsym-records.** Wie alleen `nchain` langsloopt ziet
"1 naamloos symbool" en concludeert dat er niets gewist is. De werkelijke omvang volgt uit
`DT_STRTAB − DT_SYMTAB`, en dat is deelbaar door 16 (`re/h13_dynsymgrens.py`):

| game | `DT_STRTAB − DT_SYMTAB` | = symbolen | `nchain` | verschil | staart volledig nul | UND met naam | −3 (`_Jv`/`_ITM`) | + staart | PLT-stubs |
|---|---|---|---|---|---|---|---|---|---|
| Galaga `game` | 79.632 | 4.977 | 4.719 | **258** | **258/258** | 102 | 99 | **357** | **357** ✓ |
| JP `game` | 308.528 | 19.283 | 18.811 | **472** | **472/472** | 108 | 105 | **577** | **577** ✓ |
| JP `game_original` | 308.528 | 19.283 | 18.811 | **472** | **472/472** | 108 | 105 | **577** | **577** ✓ |
| Pacman `game` | 67.744 | 4.234 | 3.986 | **248** | **248/248** | 202 | 199 | 447 | 442 |
| Pacman `game_original` | 62.784 | 3.924 | 3.740 | **184** | **184/184** | 103 | 102 | 286 | 285 |

Voor Galaga en Jurassic Park sluit de boekhouding **exact**: `(UND met naam − 3) + staart = aantal
PLT-stubs`. Voor Pacman wijkt het 5 (resp. 1) af; de 6 eerste-PLT-stubs van Pacman met een echte
index zitten immers niet in de 0xdeadbeef-groep. Voor de **ongepakte** game is het verschil nul, dus
het verlagen van `nchain` is een eigenschap van de pakker, niet van de familie.

Alle staart-records zijn **letterlijk `(0,0,0,0,0,0)`**: `st_name`, `st_value`, `st_size`, `st_info`
én `st_shndx` alle nul. Dat is méér dan "de naam is weg": ook de waarde en de sectie-index zijn weg.

**Gevolg voor de hoofdvraag:** de koppeling slot → symbool voor de verborgen imports is niet
moeilijk te vinden — **hij bestaat niet meer**. Er is geen naam, geen waarde, geen sectie, geen
versie, en de volgorde van een blok identieke nulrecords draagt geen identiteit.

### 1.4 Zitten de namen nog in `.dynstr`? Nee

`re/h14_namen_terug.py` loopt de hele `.dynstr` langs en markeert elke string waarnaar een
`nchain`-symbool, `DT_NEEDED`, `DT_SONAME`, `DT_RPATH` of `DT_VERSYM` verwijst:

| game | strings in `.dynstr` | gerefereerd | **ongerefereerd** |
|---|---|---|---|
| Galaga | 4.762 | 4.718 | **44** |
| Jurassic Park | 18.861 | 18.810 | **51** |
| Pacman | 4.034 | 3.985 | **49** |

Die 44/51/49 zijn **allemaal** versienamen (`GLIBC_2.0`…`GLIBC_2.15`, `GCC_3.0`, `GLIBCXX_3.4`,
`CXXABI_1.3`, `LIBUDEV_183`), bibliotheeknamen (`libc.so.6`, `libGL.so.1`, `libglut.so.3`,
`libSDL-1.2.so.0`, `libXrandr.so.2`, …), het pad `/usr/lib/PhysX/v2.8.1/` en de pakker-markers
`PATCHED_WITH_HASPE_LINUX_LOADER_1.1` en `HASPE_LINUX_LOADER_1.1`. **Geen enkele importnaam.**
De `DT_HASH`-buckets en -ketens verwijzen ook nergens voorbij `nchain` (max bucketindex 4.710 bij
Galaga), dus de hashtabel is geen achterdeur.

### 1.5 De `0xb7…`-GOT: een GOT-dump van één uitvoering op de kast van de pakker

Dit is een van de duidelijkste uitkomsten. Jurassic Park heeft twee vrijwel identieke bestanden met
één beslissend verschil (`re/h7_original.py`):

| | `game_original` | `game` |
|---|---|---|
| eerste-PLT GOT-slots op `stub+6` (lui) | **577 van 577** | 0 |
| eerste-PLT GOT-slots met `0xb7…` | **0** | **576 van 577** (1 leeg) |
| tweede slottabel met `0xb7…` | 0 van 101 | 39 van 101 |
| `dynsym`, `DT_JMPREL`, `nchain`, staart | identiek (19.283 / 101 / 18.811 / 472) | identiek |
| entry | `0x0904a8da` | `0x0813befc` |

`game_original` is de **eerlijke, onopgeloste** ELF: elke GOT-slot staat nog op de `push` van zijn
eigen stub. In `game` is elke slot vervangen door het adres dat hij in één concrete uitvoering had.
(Pacman is een ander geval: daar is `game` geen nakomeling van `game_original` maar een andere build —
2,7 MB groter, 157 stubs meer, 99 UND-namen meer, een grotere tweede PLT. Voor JP is het paar wél
voor/na.)

Dat verklaart waarom de waarden niet te herleiden zijn en per game verschillen: het zijn
mmap-ASLR-bases van die ene run (`re/h6_clusters.py`):

```
Galaga ∩ Jurassic Park = 0 van 355 / 571
Jurassic Park ∩ Pacman = 0
Galaga ∩ Pacman        = 2
```

Wat ze wél zijn, is meetbaar — echte functieadressen:

| game | `0xb7`-waarden | 16-byte uitgelijnd | clusters (gat ≤ 64 KiB) | grootste cluster |
|---|---|---|---|---|
| Galaga | 356 | **355 (99,7%)** | 10 | `0xb74bea00…0xb7567df0`, 693.232 B |
| Jurassic Park | 576 | **574 (99,7%)** | 19 | `0xb715a990…0xb72077c0`, 708.144 B |
| Pacman | 441 | **440 (99,8%)** | 15 | `0xb74ba190…0xb74fc5c0`, 271.408 B |

99,7% 16-byte uitgelijnd en clusters van honderden KB's: dat zijn bibliotheekmappings. (De eerdere
tabel noemde 573/575 = 99,7%; mijn eigen getal over álle `0xb7`-waarden is 574/576 = 99,7% — zelfde
uitkomst, andere noemer.)

---

## 2. `hints.dat` als kandidaat-sleutel (vraag 1) — **nee**

### 2.1 Recordindeling, uit de bytes zelf

`re/h1_hints_recon.py` en `re/h2_hints_struct.py`:

| | `ElfLdr2/hints.dat` | `ElfLdr2/x64/hints.dat` |
|---|---|---|
| grootte | 2.994.888 B | 7.325.952 B |
| record | **12 B = 8-byte sleutel + 4-byte waarde** | **16 B = 8-byte sleutel + 8-byte waarde** |
| records | **249.574** (precies deelbaar) | **457.872** (precies deelbaar) |
| distinct sleutels | **13** | **15** |
| distinct waarden | 3.593 | 9.539 |
| waardebereik | `0x080780c3 … 0x0896dba6` | `0x0040e709 … 0x01130e7d` |
| uitlijning van de waarden | 7,9% 16-byte, 23,9% 4-byte | 5,9% 16-byte, 27,9% 4-byte |
| leesbare strings | 0 | 0 |

> **Correctie met een eigen nameting (`re/h17_jmprel_einde.py`, deel C).** De oude notitie
> "235.414 records" was géén rekenfout: `hints.dat` is **append-only** en is sindsdien gegroeid.
> `249.574 − 235.414 = 14.160` records, en `235.414 × 12 + 14.160 × 12 = 2.824.968 + 169.920 =
> 2.994.888` — exact de bestandsgrootte. Het oude getal was een juiste meting van een eerdere,
> kortere versie van hetzelfde bestand. (Het recordformaat van 12 byte klopte dus ook; alleen het
> aantal was verouderd. De 457.872 van x64 klopte omdat die variant niet gegroeid is.)

**Sleutelgedrag.** De 13 (x86) respectievelijk 15 (x64) sleutels komen **in een vaste cyclus** terug
(periode 13 respectievelijk 15), en binnen elke run staan de waarden **niet-dalend** (0 mengelingen in
1.572 respectievelijk 624 runs). Per sleutel is het dus een gesorteerde adreslijst.

**x64 is exact 48 identieke kopieën:**

```
blokgrootte 9.539 records (152.624 B); cycli 48; runlengtes van alle 48 cycli identiek: 48/48
cycli byte-identiek aan blok 0: 47 van 47
```

Onafhankelijke bevestiging: de waarde `0x0040e709` en de hoogste waarde `0x01130e7d` komen elk
**exact 48×** voor in `x64/hints.dat`.

**x86 is dat níet:** 122 cycli, groottes meestal 2.360 records maar variërend van 344 tot 3.672, en
0 van 121 byte-identiek. De multipliciteit van (sleutel, waarde)-paren is er dan ook gemengd:
1.233 paren komen 8× voor, 3 paren 100×, 1.004 paren 101× en 1.353 paren 102× — terwijl in x64
**alle 9.539 paren exact 48×** voorkomen.

**Afgeleid, niet bewezen:** dat patroon past bij een **append-only log**: de tabel wordt bij elke
verwerking (per module of per invoerbestand) opnieuw weggeschreven. In x64 gebeurde dat 48× met
identieke inhoud; in x86 ruim honderd keer met kleine verschillen (alsof er tussentijds iets
bijkwam of twee schrijvers elkaar kruisten). Wat de 8-byte sleutel *betekent* heb ik niet kunnen
vaststellen; ik heb wel uitgesloten wat hij niet is.

### 2.2 De beslissende kruistoets — negatief

De PLT-stubs zijn **zelf** gescand met `re/h3_plt_scan.py`.

| game | stubs | stubadres ∩ hint-**waarden** | GOT-slot ∩ waarden | `0xb7`-GOT ∩ waarden | stubadres ∩ hint-**sleutels** | GOT-slot ∩ sleutels |
|---|---|---|---|---|---|---|
| Galaga Assault | 357 | **0** | 0 | 0 | **0** | 0 |
| Jurassic Park | 577 | **0** | 0 | 0 | **0** | 0 |
| JP `game_original` | 577 | **0** | 0 | 1 | **0** | 0 |
| Pacman `game` | 442 | **0** | 3 | 0 | **0** | 0 |
| Pacman `game_original` | 285 | **0** | 0 | 0 | **0** | 0 |

**Dekking van de JP-stubs** (de vraag "hoeveel records dekken de 578 JP-stubs"): in het hele
PLT-bereik `0x08139740…0x0813bb50` vallen **104 hint-records met 13 distinct waarden**, en **geen
enkele daarvan is een stubadres (0/577)**. Met een venster van 16 byte ná elke stub:
13/577 voor JP, **0/357** voor Galaga, **0/442** voor Pacman.

De hint-waarden zijn ook geen code: op die adressen staat in Galaga `.rodata`
(`recursive_init_error`, `TreeCrea`, …) en in Jurassic Park DWARF-debugdata. Ze zijn bovendien
**niet uitgelijnd**, terwijl PLT-stubs op 16 byte staan. Waren het stub- of GOT-adressen, dan was dat
uitlijningsbeeld onmogelijk.

Aanvullend (eigen meting, `re/h17_jmprel_einde.py` deel B): de adressen van de tweede PLT en de tweede
slottabel komen **0 keer** voor in `hints.dat` — `0x08658b10`, `0x086f83f8`, `0x09047b10`,
`0x090e73f8`, `0x08139740` en `0x0813b000` alle zes nul keer.

### 2.3 De per-game variant

Op de NAS staat één `hints.dat` die bij een spel hoort in plaats van bij de lader:
`Transformers Human Alliance/exe/hints.dat`, 432 B, mtime `1996-12-24` (het neptijdstempel van de
pakker). Het is **36 × hetzelfde 12-byte record**: sleutel `a9a50f8e0fbe9a03` → waarde `0x00680970`.
Dat spel is een PE (`TestMode.exe`: imagebase `0x00400000`, `.rdata` op vaddr `0x68000`), en
`0x00680970` valt in die `.rdata` — consistent met een import-/IAT-achtig doel, maar dat is
**afgeleid, niet bewezen**. (Ik heb alleen `TestMode.exe` als kandidaat getoetst; de eigenlijke game is
`TF_Gun_R_Ring_dumped.exe`.)

### 2.4 De sleutels staan nergens in de lader, en "hints" wordt nergens genoemd

`re/h8_laatste.py` zoekt de 13 + 15 sleutelbytes letterlijk in `BudgieLoader.exe`,
`BudgieLoader_x64.exe`, `TeknoParrot.dll`, `TeknoParrot64.dll`, `regal32.dll`, `TPGfx.dll` en
`msys-2.0.dll`: **0 van 13 en 0 van 15, in alle zeven**. `re/h10_pe.py` loopt de hele ElfLdr2-map
langs op de string `hints`: alleen losse treffers in meegelverde bibliotheken
(`af_glyph_hints_dump_edges`, `Renderhints::init`, `GL_PGI_misc_hints`, `buffer_hints`,
`Invalid value for hints` in msys-2.0.dll). **Geen positief bewijs dat de lader `hints.dat` leest** —
de pakkersecties `.brr0`–`.brr4` kunnen dat verbergen.

**Conclusie vraag 1: `hints.dat` is de sleutel niet.** Geen stubadres, geen GOT-slot, geen
`0xb7`-waarde, geen tabeladres; de waarden zijn geen code en niet uitgelijnd; de 13/15 opaque
64-bits sleutels komen in geen enkel laderbestand voor, en er zijn maar 13/15 van — een naamtabel zou
er honderden hebben. Een andere subagent vond onafhankelijk hetzelfde langs een andere weg: **1.463
hash-toetsen over 11 hashfuncties tegen 133 kandidaten** (inclusief de volledige inhoud van alle 96
bibliotheekbestanden in `libs/` en `x64/libs/`) gaven **0 treffers**
(`docs/research/budgieloader-uitgepakt.md`).

---

## 3. Wat ElfLdr2 zelf doet (vraag 3)

### 3.1 De exporttabel is de freeglut-brug, niet een GL-naamtabel

`re/h10_pe.py` parseert de PE-exportdirectory rechtstreeks (de `objdump`-uitvoer is hier misleidend):

| bestand | benoemde exports | glut\* | gl\* | overig |
|---|---|---|---|---|
| `BudgieLoader.exe` | **173** | **161** | **0** | `GetElfBase`, `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface`, `NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`, `__ctype_b_loc/tolower/toupper`, `api`, `get_info` |
| `x64/BudgieLoader_x64.exe` | **174** | 161 | 0 | + `ElfPreReserveCallback` |

Dat bevestigt het eerdere beeld: **`libglut.so.3` wordt niet als bibliotheek geleverd maar door de
lader zelf**, via zijn eigen exports. Voor `libGL` geldt dat **niet**: de bytezoektocht vindt
`glGetIntegerv` **0×** in `BudgieLoader.exe`, en in `ElfLdr2/libs/` staat **geen `libGL.so.1`**
(alleen `libGLU.so.1`). De GL-naamruimte zit in twee Windows-DLL's:

```
libs/regal32.dll   4.657.664 B   2.946 distinct gl*-strings   ("glGetIntegerv" 2×)
libs/TPGfx.dll     1.167.872 B   2.771 distinct gl*-strings   ("glGetIntegerv" 2×)
```

Volgens `docs/rt-glfix.md` is de feitelijke leverancier in de draaiende omgeving Wine's
`opengl32.dll` (`opengl32.dll op 0x79e50000-0x79fa8000`), en die exporteert GL **stdcall** terwijl de
game **cdecl** aanroept (20 van 20 gemeten). Dat is het bekende ABI-risico en de reden dat `rt-glfix`
een eigen cdecl-wacht plaatst.

### 3.2 Wat de lader met de onopgeloste slots doet — levende meting uit `docs/rt-glfix.md` §7

Ik heb geen game gestart (harde regel), maar deze eerder gemeten runtime-cijfers zijn beslissend en
sluiten exact aan op mijn statische vondsten:

```
dwords gelezen in de GOT-streek 0x08add000..0x08add900: 576
   r-xp  (anoniem)          516
   rwxp  (anoniem)           54
   r-xp  librt-glfix.so       2
   r-xp  opengl32.dll         1
meest voorkomende waarde: 0x71bb3e80  180×   =  55 8b ec 33 c0 5d c3
                                                 push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret
Van de 93 GOT-invoeren met een libGL-adres: 22 naar een echte opengl32-export,
71 naar één adres in het nagebootste gebied.
```

**Dit is het antwoord op de hoofdvraag, onafhankelijk van mijn statische analyse: 570 van 576 slots
wijzen in de draaiende ElfLdr2-run naar anonieme uitvoerbare mappings die de lader zelf maakte**, en
180 daarvan naar één en dezelfde cdecl-no-op. Voor de verborgen imports zoekt ElfLdr2 dus **geen
naam**: het maakt een trampoline en zet die in de slot. Waar het wél een echte functie vindt (de ~100
relocatie-gedekte imports en 22 van de GL-slots) komt de naam uit `dynsym` respectievelijk uit de
GL-naamruimte van `opengl32.dll` — niet uit het gamebestand en niet uit `hints.dat`.

**En dat is de eerlijke prijs:** 180 van 576 slots doen niets en geven 0 terug — "geen crash maar
ontbrekende functionaliteit" (`docs/rt-glfix.md` §7).

---

## 4. De `0xb750xxxx`-groep (vraag 4) — geen gedeelde basis, en niet te herleiden

De eerdere conclusie was: "basis(libGL) ≈ `0xb7500000`, dus `glGetIntegerv` = offset `0x120`". Mijn
metingen maken dat onhoudbaar.

**(a) `0xb7500000` is het minimum van Jurassic Park, niet een basis.** De waarde staat letterlijk in
JP's GOT; de "basis" is gekozen als het minimum van de reeks. Dat is circulair. Gemeten
(`re/h6_clusters.py`, `re/h18_libgl3.py`):

| game | waarden in `0xb7500000`–`0xb750ffff` | laagste | hoogste |
|---|---|---|---|
| Jurassic Park | **93** | `0xb7500000` | `0xb750cb80` |
| Galaga Assault | **22** | `0xb75084f0` | `0xb750eaf0` |
| **Pacman Chomp-Mania** | **0** | — | — |

**(b) JP en Galaga delen dat patroon niet.** Niet alleen hebben hun volledige `0xb7`-sets nul
overlap; ook binnen JP's venster liggen Galaga's 22 waarden op andere offsets. De beste
overlap over **alle verschuivingen 0…0x100000 stap 16** is **1 van 22** (bij shift `0xd0`), en voor
elke paginaverschuiving (`0x1000`, `0x2000`, `0x4000`, `0x8000`, `0x10000`) is het **0**. Een
gedeelde bibliotheeklayout zou hier ≥20 van 22 geven. De "`0xb7500000` + offset"-raster is dus
JP-run-specifiek.

**(c) De reeks is geen tabel met vaste stride, maar past wél bij gegenereerde stubs.** Binnen JP's
groep zijn alle 93 offsets een **32-voud** (gcd `0x20`), met 42 van de 92 afstanden gelijk aan 32; de
hoogste offset `0xcb80` is 1.629 slots van 32 byte. Over *alle* `0xb7`-waarden per game is de gcd 4.
Een dispatchtabel zou een strakke stride over de hele groep geven en de waarden zouden in PLT-volgorde
monotoon moeten oplopen — dat doen ze niet: JP heeft **2.150 inversies op 4.278 paren** (51%, dus
ongeveer willekeurig). Het 32-voud-patroon past het beste bij **gegenereerde entry points op een
32-byte raster** (glvnd/glapi-stijl stubs) — dat is **afgeleid**, niet bewezen.

**(d) En te herleiden zijn ze niet met wat we hebben.** Op de Deck staat wél een 32-bits GL-stack
(`/usr/lib32/libGL.so.1.7.0`, ELF32 i386, 435.536 B, libglvnd 2023, 3.470 FUNC-symbolen;
`libGLdispatch.so.0.0.0`; `libGLX.so.0.0.0`; `libGLX_mesa.so.0.0.0`) — maar dat is een andere build
dan die van de pakker:

```
offset direct in de symboolwaarden:            0 van 93   (libGL.so.1.7.0)
onderlinge offsetverschillen op symboolverschil: 331 van 709 = 46,7%
   null-model (200× willekeurig, zelfde aantal, zelfde 32-raster, zelfde bereik):
                                                 185.545 van 265.732 = 69,8%
```

De echte set scoort dus **lager dan willekeurig** — 331/709 is ruis, geen signaal. (Een echte match
zou ~709/709 geven.) Dat is precies de eerder gemelde uitkomst "0 van 93; beste score over 74
bibliotheken 15/143 = ruis", nu met een expliciet null-model eronder.

**(e) De praktische doodsteek.** Zelfs als de offsets klopten, gebruikt ElfLdr2 ze niet om echte
bibliotheken te bereiken: §3.2 laat zien dat 570/576 slots naar eigen anonieme trampolines gaan.

**Conclusie vraag 4:** de 93 waarden zijn echte functieadressen uit één uitvoering op de kast van de
pakker, op een 32-byte raster, niet de offsettabel van een dispatch/trampoline. `0xb7500000` is JP's
eigen mmap-basis van die run — geen gedeelde bibliotheekbasis, en niet bruikbaar als sleutel: Pacman
heeft in dat hele venster nul waarden en JP en Galaga delen nul offsets, ook na elke verschuiving.

---

## 5. Wat een eigen lader moet doen

Dit volgt rechtstreeks uit §1–§4, en het is voor het grootste deel precies wat ElfLdr2 doet — het is
dus een nagebootste route, geen gok.

**1. De imports met een echte relocatie (99 / 105 / 199 per game): normaal oplossen.** Lees
`DT_JMPREL` + `DT_PLTRELSZ`, loop de `R_386_JMP_SLOT`-relocaties langs, haal de naam via `st_name` uit
`dynsym`, `dlsym` die op de host of in een nagebootste bibliotheek, en schrijf het adres in `r_offset`.
Standaardwerk, en de namen zijn er. **Let op de grens:** gebruik `DT_PLTRELSZ` in **bytes**
(`PLTRELSZ/8` records), en neem het aantal **niet-nul** entries als waarheid.

**2. De imports zonder relocatie (`push 0xdeadbeef`): de naam is onherstelbaar — stub ze.** Er is geen
relocatie, geen dynsymeintrag, en de 258/472/248 records achter `nchain` zijn volledig nul. Doe wat
ElfLdr2 doet: zet een **cdecl-trampoline** in de GOT-slot. Twee varianten, beide meetbaar:
* de no-op `push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret` (ElfLdr2 gebruikt hem 180×) — de game
  loopt door en krijgt 0;
* een **teller** (`RAVENGUN_GLFIX_MODE=8` in `rt-glfix`) die herkent welke slots echt aangeroepen
  worden en vanaf welk retouradres. Dat is de enige manier om te weten *welke* van de ~436 stubs
  ertoe doen, en dat gereedschap bestaat al.

**3. Voor GL: niet de 93 slots benoemen maar de naamroute onderscheppen.** Dit is de belangrijkste
praktische uitkomst van deze ronde. **De game draagt zijn eigen naamruimte bij zich**
(`re/h15_naamtabel.py`):

| game | leesbare strings | identifiers | **gl\*-identifiers** | waarvan bekend als GL-export | grootste aaneengesloten gl\*-blok |
|---|---|---|---|---|---|
| Galaga Assault | 21.621 | 9.451 | **132** | 114 | 7.074 B |
| Jurassic Park | 67.812 | 32.306 | **1.347** | 1.237 | 34.184 B |
| Pacman Chomp-Mania | 31.347 | 9.649 | **58** | 45 | 8.053 B |
| GhostBusters (ongepakt) | 40.408 | 10.838 | 167 | 137 | 2.143 B |

Jurassic Park bevat **1.347 GL-functienamen als strings** (41% van de volledige GL-naamruimte van
2.950 namen) — een statisch meegeleinkte GLEW-naamtabel. De game haalt zijn GL-functies dus grotendeels
**op naam** op (`glXGetProcAddress`/`dlsym`), en die weg is door de pakker niet geraakt. Ook `dlopen`,
`dlsym`, `malloc`, `free`, `SDL_Init` en `XGrabPointer` staan als strings in het bestand.
Een eigen lader moet daarom `dlsym`/`glXGetProcAddress` onderscheppen en bedienen — precies wat
`rt-glfix` doet, en waarom 22 van de 93 slots in de praktijk wél een echte functie kregen.

**4. De twee valkuilen die overblijven.**
* **ABI:** Wine's `opengl32.dll` exporteert GL als **stdcall**, de game roept **cdecl** aan (20/20
  gemeten). Elke brug moet dat rechtzetten (eigen cdecl-wacht), anders is de stack na de eerste
  GL-aanroep verschoven.
* **TLS/canarie:** `gs:[0x14]` moet een geldige, stabiele stack-guard zijn vóór de eerste
  spel-instructie; anders is `__stack_chk_fail` een aparte fout bovenop het GL-probleem
  (`findings/games/JurassicPark/CRASH-OPLOSSING.md`).

**5. Wat je niet moet bouwen.** Een resolutietabel op basis van `hints.dat` (§2), een
offset→naam-tabel op basis van `0xb7500000` (§4), of het openbreken van `BudgieLoader.exe` voor een
naamtabel — de lader heeft zelf **0 `gl*`-exports** en noemt `hints` nergens. De twee bruikbare
bruggen zijn de **freeglut-exports van de lader zelf** (161 stuks) en de **GL-naamruimte van
`opengl32.dll`/`regal32.dll`/`TPGfx.dll`**.

---

## 6. Wat open blijft

1. **De resolutietabel bestaat alleen in geheugen.** Alle statische routes zijn nu afgesloten:
   `dynsym` (volledig genulde staart), `DT_JMPREL` (alleen de leesbare ~100–200), de GOT-waarden
   (run-specifieke ASLR-adressen), en `hints.dat` (§2). De enige plek waar de koppeling slot → naam
   nog bestaat is het **adresbereik van de lopende lader**. Dat werk loopt elders (een geheugendump
   van BudgieLoader via `/proc/<pid>/mem`); mijn bijdrage daaraan is de tabel in §1.2 en §2.2, die
   precies zegt **welke** slots nog een naam nodig hebben (de 258/472/248, en hoe ze te herkennen:
   `push 0xdeadbeef` + een GOT-slot met een `0xb7…`-waarde).
2. **De semantiek van `hints.dat`.** Vastgesteld: append-only log; 12/16-byte records van
   (8-byte sleutel, in-beeld-achtige waarde); 13/15 sleutels in vaste cyclus; per sleutel gesorteerde
   adressen; x64 = 48 byte-identieke kopieën van 9.539 records; x86 = 122 bijna-kopieën van ~2.360
   records; waarden niet uitgelijnd en niet gelijk aan enig stub-, GOT- of tabeladres van de drie
   gepakte games. **Open:** wat de 8-byte sleutel is en welk beeld de waarden adresseren. Scherpste
   volgende toets: de 432-byte Transformers-variant tegen de IAT van `TF_Gun_R_Ring_dumped.exe`, of
   een `/proc/<pid>/maps` van de machine die het bestand maakte.
3. **Waarom 48 (x64) en ~122 (x86) append-rondes?** Vermoeden: één ronde per verwerkte module of per
   invoerbestand, met kruisende schrijvers in de x86-versie. Een diff van twee opeenvolgende
   x86-cycli zou dat kunnen beslissen.
4. **`DT_PLTRELSZ` versus het aantal echte entries.** Mijn meting zegt dat `PLTRELSZ` in **bytes**
   exact klopt (100/101/195) en dat de eerste niet-`JMP_SLOT`-entry precies op `PLTRELSZ/8` staat.
   Een andere lezing meldt opgeblazen waarden ("256 tegen ~100", "138 van 684", "156 van 257"). Dat
   verschil is niet opgelost en verdient één gezamenlijke nameting op dezelfde bestanden — de mijne
   staat in `re/h17_jmprel_einde.out`, deel A, met de ruwe `DT_PLTRELSZ`-waarden erbij.
5. **Welke bibliotheek hoort bij welk `0xb7`-cluster?** Zonder de originele kastmappings of de exacte
   2015-bibliotheekversies is dat niet te beslissen, en de levende meting laat zien dat ElfLdr2 het
   ook niet doet (570/576 → eigen trampolines). Enige harde bron: een `maps`-opname van een draaiende
   ElfLdr2-sessie.
6. **De marge van 5 (Pacman `game`) en 1 (Pacman `game_original`)** in de boekhouding
   `(UND met naam − 3) + staart = stubs`. Voor Galaga en Jurassic Park sluit het exact; voor Pacman
   niet, vermoedelijk door imports die alleen via de tweede PLT lopen. Niet uitgezocht.
7. **Of het verlagen van `nchain` de `DT_HASH`-buckets ongemoeid liet.** Gemeten: geen enkele bucket
   of ketenindex wijst voorbij `nchain`. Of dat komt doordat de pakker de hashtabel herbouwde of
   doordat hij toevallig goed bleef, is niet vast te stellen.
8. **De dongle-/HASP-kant, beschreven en gelaten.** `PATCHED_WITH_HASPE_LINUX_LOADER_1.1` en
   `HASPE_LINUX_LOADER_1.1` staan als ongerefereerde strings in alle drie de `.dynstr`s, plus het pad
   `/usr/lib/PhysX/v2.8.1/`. Zoals afgesproken is daar niets aan gedaan.

---

## Bijlage: de scripts en hun uitvoer

Alles staat in de Windows-werkmap `re/` **én** in `/home/deck/`, met de uitvoer als `.out` ernaast.

| script | wat het meet |
|---|---|
| `h1_hints_recon.py` | ruwe kolomstructuur van beide `hints.dat` |
| `h2_hints_struct.py` | runs, paar-multipliciteit, periodiciteit |
| `h3_plt_scan.py` | eigen PLT-scanner + kruistoets hints ↔ stubs/GOT (357/577/442) |
| `h4_struct_semantiek.py` | blokstructuur, opcodes op de hint-adressen, Transformers, hash-batterij |
| `h5_dynamic.py` | `DT_*`, dynsym, `DT_JMPREL`, tweede PLT |
| `h6_clusters.py` | dynsym-census, tweede tabel, `0xb7`-clusters, kruisvergelijking games |
| `h7_original.py` | `game` vs `game_original` (de GOT-dump) |
| `h8_laatste.py` | hints-uitlijning, lader-exports, GL-namen in de runtime |
| `h9_exports.py`, `h9b_exports.py` | exporttabel (pogingen via `objdump`) |
| `h10_pe.py` | echte PE-exportparse + waar "hints" genoemd wordt |
| `h11_libgl.py`, `h16_libgl2.py` | libGL-groep tegen 32-bits kandidaten (niet afgerond; zie `h18`/`h19`) |
| `h12_uitlijning.py` | uitlijning en clustering van de `0xb7`-waarden |
| `h13_dynsymgrens.py` | `DT_STRTAB − DT_SYMTAB` vs `nchain`; de nul-staart |
| `h14_namen_terug.py` | ongerefereerde `.dynstr`-strings; namen elders in het bestand |
| `h15_naamtabel.py` | de GLEW-naamtabel in de gamebestanden |
| `h17_jmprel_einde.py` | grootte van `DT_JMPREL`, hints-bijlage, append-only-controle |
| `h18_libgl3.py` | JP vs Galaga offsetpatroon; de 93 offsets tegen de echte 32-bits GL-stack |
| `h19_nullmodel.py` | null-model voor de libGL-verschiltoets |
| `libgl_waar.sh` | welke 32-bits GL-bibliotheken de machine heeft |
| `pak_deck_scripts.sh`, `provenance.sh`, `elfldr2_ls.sh`, `lees_rtglfix.sh` | omgevingsverkenning |

**Niet gedaan (harde regel):** geen game gestart, geen `BudgieLoader`/Wine gestart, niets op de NAS
geschreven, geen `git commit`/`push`/`checkout`/`stash`, geen dongle-/HASP-/integriteitswerk.
