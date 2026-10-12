# ElfLdr2-familie: wat voor ELF is elke game, en wat kost een eigen lader?

*Met de hand geschreven; de kop van elk blok noemt de commando's om de getallen na te rekenen.
Alles is **statisch gemeten in ronde 44**: er is in de bestanden gelezen, er is **geen game voor
gestart**. `ELF-NATIVE.md` meet of een game-ELF **los start**; dit bestand kijkt ín de bestanden om
te zeggen wat een eigen lader zou moeten doen en of dat haalbaar is.*

## Waarom dit bestand bestaat

`ELF-NATIVE.md` concludeert: 0 van de 7 games starten native, en dat vraagt "een eigen ELF-lader,
geen paar bibliotheken". Die conclusie is juist maar te grof. Uit de bestanden zelf blijkt dat de
familie in **twee klassen** valt, en dat verschil bepaalt of een eigen lader standaardwerk is of een
reverse-engineeringproject.

Twee eerdere aannames moeten hierbij worden gecorrigeerd:

1. De "ongeveer 60 vaste GOT-slots" uit de commentaren in de `game.ini`-bestanden is onjuist: het
   zijn er **357, 577 en 442** per game; die ~60 is het aantal GL-imports daarbinnen.
2. De oorzaak is preciezer dan "de ELF is opgetuigd voor ElfLdr2": bij de gepakte games zijn de
   **PLT-relocatie-indexen vervangen door `0xdeadbeef`** en is **72 tot 82 procent van de
   importnamen gewist**. Daardoor is luie binding onmogelijk en naamresolutie onvolledig. Waar die gewiste namen precies zitten staat in de sectie daaronder.

## De twee klassen

**(a) Ongepakt** — gewone glibc-ELF's met een volledige importtopologie: echte sectiekoppen, echte
`DT_JMPREL`, volledige `dynsym`-namen, en **nul** PLT-stubs die `0xdeadbeef` pushen.

**(b) Gepakt met de HASPE-lader** — de stringtabel bevat letterlijk
`PATCHED_WITH_HASPE_LINUX_LOADER_1.1` en `HASPE_LINUX_LOADER_1.1`. Die bestanden zijn doelbewust
onbruikbaar gemaakt voor een gewone dynamic linker: **nul sectiekoppen** (`SHNUM = 0`), een PLT
waarvan elke stub de vorm `ff 25 <got> 68 ef be ad de e9 <plt0>` heeft, een GOT die in het
**bestand** al gevuld is met absolute adressen uit een vreemd adresbeeld (`0xb7xxxxxx`), en een
`dynsym` waarin een groot deel van de importnamen **gewist** is.

| game | klasse | grootte | entry | SHNUM | NEEDED | structuur-stubs | GOT-slots in het bestand | HASPE |
|---|---|---|---|---|---|---|---|---|
| GalagaAssault | **gepakt** | 7.278.592 | `0x81150a7` | **0** | 10 | **357** | 356 × `0xb7…`, 1 × `0` | **ja** |
| JurassicPark (`game`) | **gepakt** | 17.694.720 | `0x813befc` | **0** | 16 | **577** | 576 × `0xb7…`, 1 × `0x0813a0f6` | **ja** |
| JurassicPark (`game_original`) | **gepakt, ongebonden fase** | 17.690.127 | `0x904a8da` | 2 | 16 | 577 | 577 × `0x08xxxxxx` | ja |
| PacmanChomp (`game`) | **gepakt** | 6.852.608 | `0x8077f73` | **0** | 12 | **442** | 441 × `0xb7…`, 1 × `0` | **ja** |
| PacmanChomp (`game_original`) | **gepakt, ongebonden fase** | 4.144.655 | `0x835f8da` | 2 | 11 | 285 | 285 × `0x08xxxxxx` | ja |
| Halo | **gepakt** (ELF64) | 33.554.432 | `0x414600` | **0** | 13 | 24 (niet geverifieerd) | niet gemeten | **ja** |
| Halo (`game_original`) | gepakt, ongebonden fase | 31.270.111 | `0x1f65240` | 2 | 13 | 2 (niet geverifieerd) | niet gemeten | ja |
| GhostBusters | **ongepakt** | 11.057.832 | `0x804f960` | 30 | 14 | 0 | n.v.t. | nee |
| Terminator | **ongepakt** | 7.764.284 | `0x8057240` | 29 | 13 | 0 | n.v.t. | nee |
| GoGoStrike (`bowling_usa`) | **ongepakt** | 476.176 | `0x8059410` | 26 | 5 | 0 | n.v.t. | nee |

## Waar de gewiste namen precies zitten (en waarom twee metingen het oneens waren)

Op 2026-10-12 heeft deze spec even een **foutieve correctie** gestaan: de conclusie dat de
importnamen niet gewist zijn. Dat was onjuist. Hier staat wat er werkelijk gemeten is, met de
vindplaats erbij, zodat niemand dezelfde twee lezingen opnieuw hoeft te maken.

**De namen zijn wél gewist, en ze zitten in een aaneengesloten blok aan het EIND van `.dynsym`.**

De verwarring komt van `DT_HASH`. Die tabel heeft twee velden die op een symbolenaantal lijken:
`nbucket` en `nchain`. Voor een gewoon bestand is `nchain` het aantal dynamische symbolen. Voor deze
gepakte games is `nchain` **met opzet verlaagd**, zodat de dynamic linker de gewiste symbolen nooit
ziet. Wie `nchain` als het totale aantal neemt, mist ze en concludeert dat er niets gewist is.

De echte omvang van `.dynsym` volgt uit het verschil tussen `DT_SYMTAB` en `DT_STRTAB`; dat is bij
alle drie precies een veelvoud van 16.

| game | `DT_STRTAB - DT_SYMTAB` | symbolen | `nchain` | verschil | inhoud van het verschil |
|---|---|---|---|---|---|
| Galaga Assault | 79632 | **4977** | 4719 | **258** | 258 × volledig nul-record |
| Jurassic Park | 308528 | **19283** | 18811 | **472** | 472 × volledig nul-record |
| Pacman Chomp-Mania | 67744 | **4234** | 3986 | **248** | 248 × volledig nul-record |
| Big Buck Hunter Pro (ongepakt) | 3008 | **188** | 188 | **0** | niets |

"Volledig nul-record" betekent `st_name = 0`, `st_value = 0`, `st_size = 0`, `st_info = 0` én
`st_shndx = 0` -- en `st_shndx = 0` is precies "undefined", dus het zijn naamloze **imports**.

**Dat maakt het verschil tussen de twee klassen scherp.** Bij de ongepakte Big Buck Hunter Pro is het
verschil **nul**. Het verlagen van `nchain` is dus geen eigenschap van de familie maar van de
**pakker**: dezelfde teller, twee klassen, en alleen de gepakte heeft het blok. Dat is sterker bewijs
dan de getallen alleen.

Daarmee klopt de arithmetiek van deze spec exact: **99** echte imports + **258** naamloos = **357**
= het aantal PLT-stubs bij Galaga, en **105 + 472 = 577** bij Jurassic Park. En `dynsym` is bij deze
games dus geen gewone tabel: de laatste 258 (respectievelijk 472, 248) entries staan er wel, maar
`ld.so` komt er nooit, want `nchain` zegt dat ze niet bestaan.

**Wat dat betekent voor een eigen lader.** De gewiste namen zijn er niet meer, en de GOT-waarden
vervangen ze niet: die zijn wel echte functieadressen (573 van 575 exact 16-byte uitgelijnd, in 20
bibliotheek-grote clusters), maar met de bibliotheken die wij hebben niet naar namen te herleiden
(0 van 93 libGL-offsets; over 74 bibliotheken beste score 15 van 143). Een eigen lader moet die
sloten dus **anders** invullen: ofwel via ElfLdr2's eigen resolutiemechanisme (`hints.dat`, de tweede
slottabel), ofwel door de aanroepen te onderscheppen. Dat is het echte werk van fase 3.

**Hoe je dit narekent:** `re/_dynsym-beslecht.py` in de Windows-werkmap, of
`/home/deck/_dynsym-beslecht.py` op de Deck.

## De kern: de naamloze imports zijn het probleem

Bij Galaga Assault klopt het tot op de byte: **361** UND-symbolen, waarvan **99 met naam** (`malloc`,
`free`, `open`, `connect`, `fileno`, `__errno_location`, …) en **258 met `st_name = 0`**,
aaneengesloten. 99 + 258 = **357**, exact het aantal PLT-stubs en GOT-slots. Bij Jurassic Park
hetzelfde: 105 + 472 = **577** = het aantal stubs. De gewiste namen staan **niet** meer in `.dynstr`:
daar staan nog maar 36 (Galaga) respectievelijk 51 (JP) ongerefereerde strings, en dat zijn
versienamen, `NEEDED`-namen en de HASPE-marker.

En de koppeling slot → symbool is **niet positioneel**. Volgens `docs/rt-glfix.md` zit `dlsym` in het
draaiende spel op slot **437**, terwijl het in de UND-lijst positie **44** heeft.

Daar komt bij dat `DT_JMPREL` bij deze games maar **100** (Galaga) respectievelijk **101**
(Jurassic Park) `R_386_JMP_SLOT`-relocaties bevat, en dat die `r_offset`s (`0x086f83f8+`) niet naar
de PLT-GOT (`0x830d00c+`) wijzen maar naar een **tweede** slottabel met een tweede PLT op
`0x8658b10`. Ook ontbreken `R_386_RELATIVE` en `R_386_GLOB_DAT` volledig — een normaal gelinkte
i386-executable heeft die wel. De tabel die de namen aan de slots koppelt zit dus **niet in het
bestand**.

**Gevolg, eerlijk:** voor de gepakte games is "een eigen lader schrijven" niet hetzelfde als een
ELF-loader schrijven. Het is ElfLdr2's **resolutietabel** reverse-engineeren, en dat kan alleen met
een tabel die uit een **lopende ElfLdr2-run** geoogst wordt. `tools/rt-glfix/analyze.py` doet dat al
gedeeltelijk; het mat eerder de runtime-verdeling **269 nagebootst / 252 `msys-2.0.dll` /
56 `opengl32.dll`**. Uitbreiden daarvan is de weg, niet een lader van nul.

## `game` versus `game_original`: de ongebonden fase helpt niet voor de namen

Van Jurassic Park en Pac-Man Chomp staat naast `game` ook een `game_original`. Dat leek de hoopvolle
variant, want daar is de GOT nog **ongebonden**: in `game_original` van Jurassic Park staan alle 577
slots op hun luie-PLT-beginwaarde `0x08xxxxxx` (`stub+6`), terwijl `game` ze op absolute
`0xb7xxxxxx`-adressen uit een draaiend proces op de originele kast heeft staan. Het aantal
sectiekoppen verschilt ook (2 tegen 0), en de entry verschuift mee.

**Maar de importnamen zijn in beide bestanden even ver weg.** Gemeten met de symbooltabel bij
meerdere bovengrenzen (12.000, 16.000, 20.000, 22.000, 25.000 en 40.000 symbolen), zodat zichtbaar is
waar de tabel eindigt: voor Jurassic Park `game` én `game_original` is de uitkomst bij elke grens
vanaf 20.000 gelijk en precies het aantal stubs — **105 met naam en 472 zonder, samen 577**. Beide
bestanden hebben bovendien dezelfde `DT_SYMTAB` (`0x806e680`) en `DT_STRTAB` (`0x80b9bb0`).

Dus: `game_original` bewaart de **ongebonden GOT**, niet de **namen**. Voor een lader is dat toch
bruikbaar — met een ongebonden GOT hoef je geen vreemde absolute adressen meer terug te draaien, dus
het is de schonere basisafbeelding — maar de 472 slots zonder naam blijven onoplosbaar zonder de
tabel uit een lopende ElfLdr2-run. De twee ontbrekende bibliotheken (`libSDL-1.2.so.0`,
`libglut.so.3`) zijn dus niet de enige en niet de grootste hindernis: zelfs met alle bibliotheken op
hun plaats heeft 82 procent van de imports geen naam om aan te binden.

Voor de volledigheid, en omdat het een grens van de methode is: bij Pac-Man Chomp groeit het aantal
UND-symbolen mee met de bovengrens (647 bij 12.000, 2.386 bij 40.000), dus daar is het einde van de
tabel zó niet gevonden en zijn die getallen onbruikbaar.

## Het verschil tussen Galaga en Jurassic Park zit niet in de pakker

Allebei zijn ze gepakt, met dezelfde kenmerken. Wat de meting verklaart is de **`DT_NEEDED`-lijst**:
Galaga vraagt alleen bibliotheken die SteamOS 32-bits levert, dus `ldd` meldt **niets** ontbrekends
— en toch sterft hij, want zijn 357 GOT-slots staan op `0xb7xxxxxx` (niet op deze machine), de 258
naamloze imports kan geen `ld.so` oplossen, en `push 0xdeadbeef` maakt luie binding onmogelijk.
Jurassic Park vraagt daarnaast `libtheora.so.0`, `libPhysXLoader.so.1`, `libSDL-1.2.so.0`,
`libGLU.so.1` en `libglut.so.3`, die hier ontbreken, dus daar verbergt de **linkerfout** dezelfde
inhoudelijke blokkade.

Let op: **link-compleet is dus geen voorspeller van een geslaagde start.** Galaga en GoGoStrike zijn
allebei link-compleet; de een is gepakt en de ander niet.

## Wat ElfLdr2 zelf is

Uit `~/.../runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/`:

* `.version` = `TeknoParrotElfLdr2Core_1.0.0.1434`.
* `BudgieLoader.exe`: PE32 i386, console, 13 secties, `ImageBase 0x00400000`, 11.152.552 B. Hij
  importeert alleen `ADVAPI32/GDI32/KERNEL32/USER32/VERSION/WINMM` plus `msys-2.0.dll` — geen
  enkele Windows-graphics- of Wine-import. `x64/BudgieLoader_x64.exe` (11.067.048 B, 15 secties)
  heeft er `WS2_32.dll` bij.
* **173** benoemde exports: de **volledige freeglut-API** inclusief alle `*Ucall`-varianten, plus
  `GetElfBase`, `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`,
  `TpRefreshMainInterface`, `AmdPowerXpressRequestHighPerformance`, `NvOptimusEnablement`,
  `__ctype_b_loc`, `__ctype_tolower_loc`, `__ctype_toupper_loc`, `api` en `get_info`. De x64-variant
  heeft er **174** (extra `ElfPreReserveCallback`). **`libglut.so.3` wordt dus niet als bibliotheek
  geleverd maar door de lader zelf**, via zijn eigen exports.
* `libs/` en `x64/libs/` bevatten **echte Linux-ELF's** voor de gast: `libGLU.so.1`,
  `libtheora.so.0`, `libPhysXLoader.so.1`, `liballeg.so.4.0`, `libNL.so.1.6`, `libstdc++.so.5`,
  `libcsv.so`, `libfmod.so.7`; 64-bits ook `libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2`,
  `librt.so.1`, `libgcc_s.so.1`, `libstdc++.so.6.0.25`, `libudev.so.1`, blas/lapack/gfortran.
  **Niet** aanwezig: `libglut.so.3`, `libSDL-1.2.so.0`, `libXrandr.so.2`, `libesd.so.0`,
  `libfmod.so.10`, `libfmodstudio.so.10`, `libiw.so.30`.
* `env/dev/{fd,stdin,stdout,stderr}` zijn nagemaakte `/dev`-nodes.
* `hints.dat` = 2.994.888 bytes = **249.574** records van 12 bytes (8-byte sleutel → in-beeld-adres; de eerdere 235.414 was fout -- 235414 × 12 is 2.824.968, geen 2.994.888), 1518 groepen, **0
  leesbare strings**; `x64/hints.dat` = 457.872 × 16, 720 groepen.
* In `TeknoParrotUi.dll` staat letterlijk `ElfLoader 2 cannot handle special characters ... in the
  file path`, `BudgieLoader failed with exit code {0}` en
  `openal32gsevo.dll not loaded from ElfLdr2\libs folder!`, plus twee regels die bewijzen dat de
  lader **Linux-`.so`-bestanden van schijf laadt**.
* De `AndroidLaunchRecipes` noemen `"loaderExecutable": "ElfLdr2\\elfloader.exe"`,
  `"guestArchitecture": "x86"` en `"containerTemplate": "teknoparrot-x86-v1"`; in de runtime staat
  geen `elfloader.exe`, alleen `BudgieLoader.exe`. De `UserProfiles` zetten `WineRunnerPath` op
  `.../wine/proton-11.0-2/bin/wine`.

**Afgeleid (onderbouwd):** ElfLdr2 is een Cygwin-PE die de gast-ELF **in zijn eigen adresruimte** zet
— het spelbeeld staat op `0x08048000` en in de proceskaart staat géén `.so` uit `ElfLdr2/libs`. De
gast-imports worden dus niet via `dlopen` opgelost maar via naam-lookup in de exporttabel van
`BudgieLoader.exe` plus zijn eigen nagebootste implementaties.

## Wat een eigen lader moet doen

**Voor de ongepakte leden** (GhostBusters, Terminator, GoGoStrike, Halo) is het standaardwerk: de
`PT_LOAD`-segmenten op hun `p_vaddr` mappen (niet-PIE, vast), `R_386_JMP_SLOT`/`R_386_GLOB_DAT`/
`R_386_RELATIVE` toepassen met naam-lookup via `dlsym()` op de host, `DT_INIT` en `INIT_ARRAY` draaien
en naar `e_entry` springen. De enige echte kosten zijn de **ontbrekende gastbibliotheken**, en die
liggen grotendeels in `ElfLdr2/libs/`; `libglut.so.3` zou uit de exporttabel van de lader nagebouwd
moeten worden.

**Voor de gepakte leden** (GalagaAssault, JurassicPark, PacmanChomp, en vermoedelijk Halo) is dat
niet genoeg: zonder de slot→symbool-tabel uit een lopende ElfLdr2-run is er geen manier om te weten
welke functie achter een naamloos slot hoort. De PE van de lader zelf openbreken is geen uitweg: die
is zelf gepakt (secties `.brr0`–`.brr4`, gecomprimeerde strings; `libGLU`, `dlopen`, `ELFCLASS` en
`JMP_SLOT` komen er letterlijk niet in voor).

**Eén waarschuwing bij "gewoon de hostbibliotheken op het zoekpad zetten".** Als een gastbibliotheek
op de host geladen wordt, is de gast-ABI niet per definitie goed: `docs/rt-glfix.md` laat zien dat
Wine's `opengl32.dll` GL **stdcall** exporteert terwijl de game **cdecl** aanroept (`ret 0x4/0x8/0x24`,
20 van 20 gemeten). Voor een route met echte Mesa-bibliotheken is dat een open risico.

## De eerstvolgende echte proef

Met alleen `LD_LIBRARY_PATH=<ElfLdr2/libs>` is de linktoestand per game te meten **zonder iets te
starten**:

| game | wat `ldd` dan nog mist |
|---|---|
| **GoGoStrike** (`bowling_usa`) | **niets** |
| GalagaAssault | niets (maar gepakt, dus dat zegt hier weinig) |
| PacmanChomp | alleen `libXrandr.so.2` |
| Terminator | alleen `libglut.so.3` |
| JurassicPark | `libSDL-1.2.so.0` + `libglut.so.3` |
| GhostBusters | `libglut.so.3`, `libesd.so.0`, `/usr/lib/libcsv.so` — een **absoluut** `DT_NEEDED`-pad, dus `LD_LIBRARY_PATH` helpt daar niet |
| Halo (64-bits) | niet zo te meten: `x64/libs/libc.so.6` is een oude glibc die `/bin/bash` sloopt |

**GoGoStrike is de goedkoopste zinvolle proef in deze familie**: ongepakt én link-compleet, dus één
omgevingsvariabele en geen emulatie meer. Die proef is **nog niet gedaan** en mag niet tegelijk met
de meetronde van de andere games lopen: er mag maar één game tegelijk draaien.

De tegenhanger in de lader-repo is `scripts/play-native.sh` met een drop-in waarin `Audit = false`
staat (zie `docs/elfldr2-route.md` §3).

**Voor de gepakte games** is de kleinste zinvolle stap een offline dumper (geen start) die per game
één JSON schrijft met de drie tabellen die de lader nodig heeft: per PLT-stub het stubadres, het
GOT-slotadres en de waarde in het bestand; `DT_JMPREL` als `(r_offset, symboolindex, naam, type)`;
en `dynsym` per UND-index als `(index, st_name, naam-of-leeg)`. Plus de assertie die hier gemeten is:
`aantal stubs == aantal naamloos + aantal met naam`. Dat maakt de ontbrekende tabel **meetbaar** in
plaats van vermoed, en het is precies de input waarop `tools/rt-glfix/analyze.py` kan aanhaken.

## Zitten de namen écht niet in het bestand? De GOT-waarden wel -- maar ze zijn niet te herleiden

*Gemeten 2026-10-12, read-only, op de NAS-tabel `findings/games/JurassicPark/PLT-slots.md`
(939 regels; `…/Teknoparrot/findings/games/JurassicPark/`). De scripts staan in de Windows-werkmap
als `re/_plt-clusters.py` en `re/_plt-ident.py`; er is **geen game voor gestart** en er is niets op
de NAS geschreven.*

Het bovenstaande blijft staan: van de 577 importstubs van Jurassic Park hebben er 105 een naam en
472 niet, en de tabel die slot aan naam koppelt zit niet in het bestand. Maar er staat iets anders
wél in, en dat was tot nu toe onderschat: **de GOT is in het bestand al gevuld met de echte
functieadressen uit het oorspronkelijke proces.**

**De tabel zelf.** 578 rijen, index 0 t/m 577, geen gaten en geen dubbele indexen. Dat is de
resolverstub (index 0) plus de 577 importstubs. Daarvan hebben er **2 geen waarde** (de
resolverstub en één importstub), wijst er **1 naar het spel zelf** (`0x0813a0f6`) en wijzen er
**575 in het `0xb7`-bereik**. Dit corrigeert de `576 × 0xb7…` in de tabel hierboven naar 575; dat
verschil is precies die ene lege importstub.

| meting | uitkomst |
|---|---|
| exact 16-byte uitgelijnd (zoals een functiebegin) | **573 van 575 (99,7%)** |
| in `0xb7500000`-`0xb750ffff` (de groep die de NAS-tabel libGL noemt) | **93** |
| spreiding van die 93 | `0xb7500000`-`0xb750cb80`, dus **52.096 bytes** |
| de overige 482, gegroepeerd met onderlinge afstand <= 64 KiB | **20 clusters**, waarvan 14 met >= 5 waarden |
| dekking van de 10 grootste clusters | **442 van 482 (91,5%)** |
| grootste cluster | `0xb715a990`-`0xb72077c0`: 708.144 bytes, **143 waarden** |

Die uitlijning en die clustering zijn het bewijs dat dit **echte functieadressen** zijn en geen
rommel: ze komen uit een proces waarin de bibliotheken op die adressen gemapt stonden. De clusters
zijn bibliotheek-groot (de grootste 708 KB, de meeste tientallen KB's) -- precies wat een gemapte
`.so` met eigen code op een eigen basis oplevert.

**En dan de toets die de methode zou moeten dragen, en die niet slaagt.** Als `waarde - basis` een
offset in een bibliotheek is, dan moet die offset exact op een symboolwaarde van die bibliotheek
landen, want voor een DSO is `st_value` de offset vanaf de laadbasis. Getoetst tegen **74** 32-bits
bibliotheken op deze machine, inclusief `/usr/lib32/libGL.so.1` (3.470 FUNC-symbolen) en alles wat
`ElfLdr2/libs/` meelevert:

* de 93 libGL-offsets met basis `0xb7500000` landen **0 van 93** op een symbool van de libGL van
  deze machine;
* voor de vijf grootste clusters is, over 74 bibliotheken en 17 paginagrenzen, de beste score
  **15 van 143 (10%)** -- ruis, en de "winnaars" spreken elkaar tegen (het ene cluster "libGL", het
  andere "libfmod", een derde "libpng15").

Daar komt een tweede, hardere aanwijzing bij: de libGL-offsets zijn **0x40, 0xe0, 0x120, 0x2a0,
0x360, 0x2a80, 0xc620** -- te klein voor een offset in een echte `.so`. De ELF-kop, `.dynsym` en
`.dynstr` van een bibliotheek met 3.470 functies beslaan samen al veel meer dan `0x120` bytes (het
eerste FUNC-symbool van deze libGL staat pas op `0x2f4c0`). Een functie op 288 byte ná de laadbasis
van libGL kan dus niet.

**Wat dat betekent, eerlijk:** de adressen zijn echte functieadressen, maar de **offsets zijn met de
bibliotheken die wij hebben niet naar namen te herleiden**. De slot-naam-tabel staat nog steeds niet
in het bestand, en de GOT-waarden vervangen die tabel niet: ze zeggen alleen *dat* er ooit een
functie op dat adres stond, niet *welke*. Wie ze toch wil omzetten heeft één van twee dingen nodig
die we niet hebben: de **originele 32-bits bibliotheken uit 2015** zoals ze op de kast stonden, of
de **proceskaart** (`/proc/<pid>/maps`) van het proces waar deze GOT uit komt -- die vertelt welke
bibliotheek op welke basis stond en dus welk adresbereik bij welke bibliotheek hoort.

Dit nuanceert dus **twee** claims die op tafel lagen. De ene, dat je "de rest hetzelfde doet, met de
bijbehorende bibliotheek", is met de huidige bibliotheken **niet gelukt** en dus niet bevestigd. De
andere, dat de GOT-waarden "nergens naar een bruikbaar doel wijzen", klopt voor *runtime* -- maar de
waarden zijn geen rommel, en dat onderscheid is het verschil tussen "er is niets" en "we kunnen er
nog niet bij".

## Hoe je dit narekent

Alles read-only, zonder een game te starten:

```bash
R=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot

# de linktoestand per game (schrijft niets)
LD_LIBRARY_PATH="$R/libs" ldd "$NAS/Go Go Strike/bowling_usa"

# de klasse: sectiekoppen, entry, NEEDED, en de structuur van de PLT
readelf -h -l -d "$NAS/Galaga Assault/game" | grep -E 'Class|Entry|Number of section|NEEDED|JMPREL|PLTGOT'

# de HASPE-marker
strings -a "$NAS/Galaga Assault/game" | grep HASPE

# de exports van de lader en wat hij meelevert
objdump -p "$R/BudgieLoader.exe" | grep -A200 'Export'
ls -la "$R/libs/"
cat "$R/.version"
```

**Wat open blijft:** of ElfLdr2 de ingebakken `0xb7xxxxxx`-GOT-waarden gebruikt of overschrijft; hoe
hij aan de naam van een naamloos slot komt; de semantiek van `hints.dat`; wie "HASPE Linux loader
1.1" is (de marker staat **niet** in `BudgieLoader.exe`); de 64-bits PLT/GOT van Halo; de herkomst en
volgorde van `game` versus `game_original` (de mtime `Dec 24 1996` is een neptijdstempel); of de
gast-ABI op echte hostbibliotheken past; en of deze games zonder lader hun HASP-controle halen — de
marker `HASPE_LINUX_LOADER` suggereert dat de pakker juist díe controle verving, maar dat is niet
bewezen.
