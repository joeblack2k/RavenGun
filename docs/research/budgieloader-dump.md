# Geheugendump van de lopende BudgieLoader: route, bestanden en wat erin staat

*RavenGun, 2026-10-12, gemeten op de Deck. Dit is de beslissende stap uit
`docs/research/budgieloader-uitgepakt.md` §9 (weg A) en `docs/research/eigen-elflader-plan.md`
("Fase 1 is af", punt 3): één geheugendump van een draaiende lader, omdat statisch uitpakken
aantoonbaar niet lukt.*

**Kort:** de dump is gelukt, hij staat in `~/bw/dump-galagaassault/` (611 MB), en de
**resolutietabel van de naamloze imports bestaat niet** — maar wel iets veel beters: de lader
heeft **precies de 100 verplaatsingen opgelost die hun naam nog hebben**, en die 100 staan met naam
en al in `~/bw/resolutietabel-elfldr2.tsv`. De overige 258 "naamloze slots" worden door ElfLdr2
**niet aangeraakt**.

---

## 1. De gevolgde route, met de exacte commando's

De bestaande route is `scripts/launch-direct.py` (zie `docs/research/elfldr2-route.md`). Alleen het
starten van de lader loopt daar langs; al het andere is eigen gereedschap in `re\` (Windows-werkmap)
dat via `re\rcput.ps1` en `re\rc.ps1` op de Deck is gezet.

```bash
# 1. voorcontrole (verplicht, één game tegelijk)
pgrep -a wine; pgrep -af 'BudgieLoader|bowling_usa|gamescope|play-native|pipehelper'
#    -> leeg (alleen gamemoded); de bench was vrij

# 2. de meting zelf, als systemd-user-unit (zie §4 waarom)
bash /home/deck/_dump-start2.sh GalagaAssault

# 3. de analyse (allemaal read-only op de dump)
python3 /home/deck/_dump_budgie.py   ...   # de dumper (start + dump + opruimen)
python3 /home/deck/_dump_ontleed.py  ...   # laderbeeld, gast tegen bestand, extracten
python3 /home/deck/_dump_namen.py    ...   # exporttabellen van lader en msys-2.0.dll
python3 /home/deck/_dump_thunks.py   ...   # s3 tegen s4, de 45 slots zonder exportnaam
python3 /home/deck/_dump_bron.py     ...   # de 1,1 MB regio op 0x80000000
python3 /home/deck/_dump_bron2.py    ...   # naam bij elk opgelost adres
python3 /home/deck/_dump_jmprel.py   ...   # koppeling met de JMPREL-verplaatsingen van de gast
python3 /home/deck/_dump_tabel.py    ...   # schrijft de resolutietabel weg
```

Wat de dumper precies doet (`re\_dump_budgie.py`):

1. **Voorcontrole** op wine/lader/game/gamescope; is er iets, dan start hij niets (exitcode 3).
2. De lader starten als **eigen kindproces** via `launch-direct.py <profiel> --run
   --env TP_NO_GAMESCOPE=1`, met `DISPLAY` en `WAYLAND_DISPLAY` uit de omgeving verwijderd
   (geen gamescope, geen venster op de tv van de eigenaar).
3. Wachten tot het proces met `BudgieLoader` in de opdrachtregel bestaat **en een nakomeling van
   dit script is** — dat laatste is niet cosmetisch: `/proc/<pid>/mem` lezen is een
   ptrace-aanhechting en `ptrace_scope=1` staat die alleen voor een nakomeling toe.
4. Wachten tot `0x401000` (het begin van `.text`) niet meer leeg is → het uitpakken is begonnen.
5. Vier snapshots, elk met het proces een seconde stilgezet (`SIGSTOP`) voor een samenhangende
   lezing en daarna weer door (`SIGCONT`): `s1-lader` (alleen het laderbeeld), `s2-vroeg` (alles),
   `s3-gast` (2 s nadat de gast op `0x08048000` verscheen) en `s4-na` (8 s later).
6. Opruimen: `SIGTERM` en daarna `SIGKILL` op de eigen procesgroep, plus een expliciete controle
   dat er geen wine-, lader- of gameproces meer is.

### De gebruikte profielgegevens (uit `launch-direct.py --plan`)

| | |
|---|---|
| game | GoGoStrike en GalagaAssault zijn beide even duur; gekozen is **GalagaAssault** (gepakte HASPE-klasse, precies de klasse waar de naamloze imports zitten) |
| lader | `views/GalagaAssault/ElfLdr2/BudgieLoader.exe` (32-bits, ImageBase `0x400000`) |
| Wine | `~/.local/share/ravengun/wine/proton-11.0-2/bin/wine` |
| werkmap | `~/.local/share/ravengun/runtime/views/GalagaAssault` |
| gast | `…/views/GalagaAssault/game`, 7.278.592 B |
| drop-ins | `librt-glfix.so` in `LD_AUDIT` (dat is het werkende kanaal), `libravengun-hasphook.so` met `Audit = false` |
| gamescope | uit (`TP_NO_GAMESCOPE=1`); `DISPLAY` leeg |

---

## 2. De dumpbestanden, met adresbereiken en groottes

Alles onder `~/bw/` op de Deck.

### 2.1 De snapshots (`~/bw/dump-galagaassault/`)

| snapshot | moment | regio's | bytes |
|---|---|---|---|
| `s1-lader` | direct na het begin van het uitpakken | 3 | 19.484.672 |
| `s2-vroeg` | zelfde moment, volledig | 313 | 82.948.096 |
| `s3-gast` | 2 s nadat de gast op `0x08048000` verscheen | 641 | ~257 MB op schijf |
| `s4-na` | 8 s later | 660 | ~257 MB op schijf |

Per snapshot staat er `maps.txt` (de volledige `/proc/<pid>/maps`), `cmdline.txt`, `status.txt` en
per regio een `vma_<begin>-<eind>_<rechten>_<soort>.bin`. De volledige inventaris met adresbereik,
rechten, soort en bestandsnaam staat in `manifest.tsv` (1.620 regels).
`BEVINDINGEN.txt` is het logboek van de meting zelf.

**Het laderbeeld in `s3-gast`** (12 regio's, samen 19.484.672 B = `0x400000`–`0x1695000`):

```
0x00400000-0x00401000 r-xp      4.096   PE-kop
0x00401000-0x0055d000 r-xp  1.425.408   .text
0x0055d000-0x005ea000 rwxp    577.536   .data
0x005ea000-0x005ec000 r-xp      8.192   .edata
0x005ec000-0x005ef000 rwxp     12.288   .idata
0x005ef000-0x00601000 r-xp     73.728   .brr1
0x00601000-0x00603000 rwxp      8.192   .tls
0x00603000-0x0062f000 r-xp    180.224   .brr0
0x0062f000-0x00635000 rwxp     24.576   .brr2 (begin)
0x00635000-0x00bf4000 r-xp  6.025.216   .brr2
0x00bf4000-0x00bf6000 rwxp      8.192
0x00bf6000-0x01695000 r-xp 11.137.024   .brr4
```

**Gemeten en belangrijk:** in `s1-lader` (1 s na de start) is `0x00401000-0x00bf6000` nog **één**
regio met `rwxp`; in `s3-gast` zijn de secties **aparte regio's met de rechten uit de sectietabel**
(`r-xp` voor `.text`, `rwxp` voor `.data`). De lader heeft zijn eigen secties dus na het uitpakken
opnieuw beveiligd — dat is het bewijs dat het uitpakken klaar is en dat `.text`/`.data`/`.brr0`/
`.brr2`/`.brr4` kant-en-klaar in het geheugen staan. De eerste 16 bytes op `0x401000` zijn
`55 89 e5 83 e4 f0 83 ec 10 c7 04 24 40 ed 50 00` = `push ebp; mov ebp,esp; and esp,-16;
sub esp,0x10; mov dword [esp],0x50ed40`: echte i386-code.

**De gast in `s3-gast` en `s4-na`:** één regio `0x08048000-0x08739000 rwxp`, **7.278.592 B** —
exact de bestandsgrootte van `game` uit `game.ini`. Daarmee is een byte-voor-byte vergelijking met
het bestand mogelijk (zie §3.4).

### 2.2 De extracten en de tabel (`~/bw/`)

| bestand | inhoud | grootte |
|---|---|---|
| `lader.img` | het laderbeeld `0x400000`–`0x1695000`, aaneengesloten | 19.484.672 B |
| `gast.img` | het spelbeeld `0x08048000`–`0x08739000` | 7.278.592 B |
| `resolutietabel-elfldr2.tsv` | **de opgeloste imports: slot, symboolindex, naam, oud, nieuw, doel** | 100 regels |
| `resolutietabel.tsv` | de ruwe diff (103 woorden) zonder naamkoppeling | 103 regels |

---

## 3. Wat er in de dump staat

### 3.1 Ja: er staan leesbare symboolnamen in, en veel

* In het laderbeeld alleen al: **61.592 leesbare reeksen van ≥ 5 tekens** (`s1-lader`;
  `.data` 4.414, `.brr4` 43.050, `.brr2` 12.617, `.text` 1.100, `.brr0` 320). Over de hele dump:
  161.370 reeksen van ≥ 4 tekens.
* De namen staan in twee gebieden: het `rwxp`-blok tussen `0x55xxxx` en `0x59xxxx`
  (`malloc` op `0x00561da5`, `memcpy` op `0x00561dc6`, `SDL_Init` op `0x00569f27`, `glutInit` op
  `0x005747c1`) en het `r-xp`-blok (`fopen` op `0x013d95f0`, `getpid` op `0x013ad0e2`,
  `pthread_mutex_lock` op `0x0157ee76`).
* Daar staat de eigen naamlijst van de lader: herhaalde blokken (o.a. op `0x00561d80`, `0x005631c0`,
  `0x00563f80`) met een functielijst — `tolower, sem_post, sem_wait, fwrite, strlen, malloc, ioctl,
  strcmp, free, strncmp, memcpy, ldexp` — gevolgd door een UTF-16-lijst met bibliotheeknamen:
  `libc.so.6, libm.so.6, libdl.so.2, libpthread.so.0, libgcc_s.so.1, ld-linux-x86-64.so.2,
  ld-linux.so.2, libGL.so…`. Dat is de gereedschapskist waarmee de lader zijn eigen hostfuncties
  opzoekt.
* **De 161 glut-implementaties staan na het uitpakken op hun adressen en het is echte code.**
  `export-datadir` RVA `0x1090058` (dus **in `.brr4`, niet in `.edata`** — dat verklaart waarom de
  173 namen statisch al leesbaar waren), 173 namen waarvan **161 `glut*`** en **31 `*Ucall`**.
  Hun adressen: RVA `0x308f0`–`0x3e9d0` = absoluut `0x004308f0`–`0x0043e9d0` (de opdracht noemde
  `0x30930`–`0x3e9d0`; `0x30930` is `glutIdleFunc`, en `0x308f0` is `glutIdleFuncUcall`).
  Op elk van die adressen staat een functieproloog, bijvoorbeeld
  `glutIdleFunc @0x00430930: 55 89 e5 53 83 ec 14 …` en
  `glutSetWindowData @0x0043e9d0: 55 89 e5 83 ec 18 …`. Van de 161 beginnen er 134 met een
  klassieke proloog (`push`/`mov`/`jmp`); de andere 27 beginnen met een andere instructie en zijn
  niet allemaal los nagerekend. **Geen enkele glut-export is leeg of onzin.**

### 3.2 De correctie op de aanwijzing over `0x55e000`

De tabel op `0x0055e000` (stap `0x40`, wijzend naar `0x579be0`, `0x579ba0`, `0x579b60`, …) is
**niet** de resolutietabel. De records van 64 bytes bevatten geen namen maar de
**bitmap-fontgegevens van freeglut** (16-bits glyphrijen: `09 00 00 00 … 3b 00 77 00 63 00 63 00
73 00 3f 00 07 00 63 00 77 00 3e 00`), horend bij `glutBitmapCharacter`. Deze aanwijzing kan
vervallen.

### 3.3 De naamstrings worden niet door een simpele pointertabel geïndexeerd

In het hele laderbeeld zijn er **1.884** 4-byte woorden die naar een leesbare reeks wijzen, bijna
allemaal immediate-waarden in code; de langste aaneengesloten reeksen zijn 22, 19, 18 en 10 woorden.
Er is dus **geen array-van-pointers naar namen** in het laderbeeld. Dat is zelf een meting: de
koppeling naam↔adres zit niet in het laderbeeld maar in de regio op `0x80000000` (§3.5).

### 3.4 De kernmeting: de gast-GOT tegen het bestand

Het gedumpte spelbeeld is byte-voor-byte met het bestand vergeleken. **410 van 7.278.592 bytes
verschillen (0,006 %)**; ze liggen op twee pagina's:

| pagina | bytes |
|---|---|
| `0x086f8000` | 398 |
| `0x08348000` | 12 |

Dat zijn **103 woorden van 4 bytes**, in vier aaneengesloten reeksen:

```
0x083484c0-0x083484c4   2 woorden   (datapunters, oud = 0)
0x083484e0-0x083484e0   1 woord
0x086f83f8-0x086f8574  96 woorden
0x086f857c-0x086f8588   4 woorden
```

**En nu de beslissende stap.** De dynamische sectie van de gast is uitgelezen
(`PT_DYNAMIC`, `DT_JMPREL = 0x0807f840`, `DT_PLTRELSZ = 0x320`, `DT_PLTREL = 17`,
`DT_SYMTAB = 0x08051a0c`, `DT_STRTAB = 0x0806511c`, `DT_PLTGOT = 0x0830d000`):

* Er zijn **100 `R_386_JMP_SLOT`-verplaatsingen** van 8 bytes. **Alle 100 hebben een bewaarde
  naam** (`st_name ≠ 0`); **nul** zijn naamloos.
* **Precies die 100 `r_offset`-adressen zijn de 100 woorden die de lader gevuld heeft.**
  Kruiscontrole: 100 verplaatsingen met naam én gevuld, **0** gevuld én naamloos, **0** met naam
  maar niet gevuld. De overige 3 gevulde woorden (`0x083484c0`, `0x083484c4`, `0x083484e0`) zijn
  geen verplaatsingsdoel maar datapunters.
* Controle op de eerste regel: het bestand zegt `0x086f83f8 → fileno` (ook al statisch gemeten), en
  de lader schreef daar `0x61101eca` = **`msys-2.0.dll`'s `_fileno`**. Dat klopt tot op de letter.

**Wat ElfLdr2 dus doet:** hij lost **alleen de verplaatsingen op waarvan de naam nog in `dynsym`
staat**, tegen zijn eigen exporttabel (`msys-2.0.dll`, de Cygwin-POSIX-laag van de lader) en tegen
zijn eigen code. De "naamloze slots" waar het project achteraan zit, zijn **geen verplaatsingen**:
ze staan wel in de tweede slottabel op `0x086f83f8` en verder (met de oude waarden `0x08658b16`,
`0xb76e2020`, …), maar er hoort geen `DT_JMPREL`-invoer bij en **de lader schrijft er niets in**.
Er bestaat in het geheugen van de loper dus **geen resolutietabel voor die slots** — niet omdat we
hem niet vinden, maar omdat de lader ze nooit resolveert.

**De volledige tabel** staat in `~/bw/resolutietabel-elfldr2.tsv` (kolommen: gastslot,
symboolindex, naam, oud, nieuw, doel). De verdeling van de 100 opgeloste imports:

| doel | aantal |
|---|---|
| export van `msys-2.0.dll` | 58 |
| eigen functie in het laderbeeld (`0x41xxxx`–`0x42xxxx`) | 37 |
| gegenereerde trampoline op `0x2fa04xx` | 5 (zie §3.5) |
| datapunters op `0x083484c0`/`e0` (buiten de verplaatsingen) | 3 |

Voorbeelden: `fileno → msys:_fileno`, `malloc → msys:malloc`, `fopen → msys:fopen`,
`memcpy → msys:memcpy`, `getpid → msys:getpid`, `free → msys:free`, `realloc → msys:realloc`,
`strerror → msys:strerror`, `pthread_mutex_unlock → 0x0042bba0` (lader-intern),
`__errno_location → 0x0041ee50` (lader-intern), `syscall → trampoline 0x02fa0488`.

**Wat de lader níet doet — en dat is de verklaring voor de crashes.** De 31 slots die de andere
agent statisch in de **eerste** GOT op `0x0830d0xx` heeft benoemd (`connect`, `getpid`, `syscall`,
`realloc`, `recv`, `getenv`, `calloc`, `memset`, …) zijn in het bestand én in de dump **identiek**:
0 van die 31 is aangeraakt. Ze houden dus hun oude `0xb7xxxxxx`-waarde uit het proces op de
originele kast, en een sprong daarnaartoe loopt in een ongemapte regio. Dat is precies de crash die
bij Jurassic Park (`eip=0x819c524b`) en Galaga Assault gemeten is.

**`s3-gast` tegen `s4-na`: 0 bytes verschil** in het hele spelbeeld. De lader raakt de gast na het
laden niet meer aan; alles gebeurt vóór het moment dat de gast op `0x08048000` staat.

### 3.5 De naam/aderstabel van de lader: de regio `0x80000000`–`0x80110000`

Er is één regio die er in de proceskaart uitziet als "anoniem", maar die alles bevat wat we zochten:
`0x80000000-0x80110000`, **1.114.112 B, `rwxp`**, en die is in `s3` en `s4` identiek.

* **15.742 leesbare reeksen**, 243.193 tekens; **902 C++-gemangelde namen** (`_Z24plays_per_game_
  menu_initv`), **527 `SDL_*`-namen**, **39 `glut*`-namen**, plus `malloc`, `fopen`, `fileno`,
  `memcpy`, `pthread_mutex_lock`, `XFlush`, `syscall`, `__libc_current_sigrtmin`, `getutent_r`,
  `XCloseDisplay`, en spellogica (`mine_mixPercent`, `jps_infoplayer`, `patrol_entry_wave`).
* Op `0x8008877c` staat een **ELF32-kop** (`7f 45 4c 46 01 01 01 00`, `e_type=2`,
  `e_machine=3`, `e_entry=0x081150a7`, `e_phoff=0x34`) — een door de lader opgebouwd beeld met de
  symbolen erbij.
* **37 van de 103 opgeloste adressen komen letterlijk in deze regio voor, en de naam die er direct
  bij staat is precies de naam uit de JMPREL-verplaatsing**: `0x0041ee50` bij `__errno_location`,
  `0x0041c310` bij `open`, `0x0041ffa0` bij `connect`, `0x0042bba0` bij `pthread_mutex_unlock`,
  `0x00420920` bij `inet_ntoa`, `0x00419840` bij `dlvsym`, `0x004278d0` bij `XOpenDisplay`.
  Bovendien komen **60 van de eerste 60 slotadressen** (`0x086f83f8`, …) in de regio voor.
* **Dit is dus de registratie die de lader zelf bijhoudt** (naam, slotadres, opgelost adres), en
  daarmee is de vraag "hoe koppelt ElfLdr2 een slot aan een naam" met een meting beantwoord: via de
  naam in `dynsym` van de gast, met deze regio als boekhouding.

**De vijf trampolines.** Voor vijf imports die niet rechtstreeks te binden zijn (`syscall`,
`__libc_current_sigrtmin`, `getutent_r`, `XCloseDisplay`, `XFlush`) heeft de lader op
`0x02fa0488`…`0x02fa0508` (stap `0x20`) kleine stubs gegenereerd:

```
0x02fa0488: b8 98 6f 0c 80   mov  eax, 0x800c6f98     ; wijst naar de naam "syscall"
            50               push eax
            b8 b0 48 41 00   mov  eax, 0x004148b0     ; de resolver van de lader
            ff d0            call eax
            83 c4 04         add  esp,4
            c3               ret
```

De pushwaarde is telkens een **pointer naar het record in de `0x80000000`-regio**, en op elk van
die vijf adressen staat inderdaad de juiste naam (`0x800c6f98` → `syscall`, `0x80088698` →
`__libc_current_sigrtmin`, `0x800b7158` → `getutent_r`, `0x80088510` → `XCloseDisplay`,
`0x800b3610` → `XFlush`). Dit is dezelfde vorm als de "tweede PLT" van de gast (die `0x0`, `0x8`,
`0x10` pusht): een uitgestelde binding via de resolver op `0x004148b0`.

### 3.6 Wat er verder nog opvalt

* De **virtuele adresruimte** van het 32-bits proces: 337 regio's in `s1`, 641 in `s3`, met
  2.020,8 MB aan `---p`-reserveringen, 52,9 MB anoniem leesbaar geheugen en 30,4 MB hostbibliotheken
  (`/usr/lib32/libc.so.6` staat **twee keer** gemapt, op `0xf6fe8000` en `0xf7cd9000`).
* Er is een dongle-/HASP-laag in het beeld: `ravengun-hasp`-drop-ins en `hasp_`-symbolen komen in de
  naamregio voor (`hasp_` is aanwezig). **Het bestaan is hiermee genoemd; er is niets aan geraakt.**

---

## 4. Twee operationele vondsten die het opnieuw doen makkelijk maken

1. **`ptrace_scope=1` is geen probleem als het dumpende script de ouder is.** `/proc/<pid>/mem`
   lezen lukt zonder `sudo` zolang de lader een nakomeling is van het script dat hem startte. Een
   losse `gdb -p` (zoals eerder geprobeerd) krijgt EPERM.
2. **`systemd-logind` doodt de hele sessiescope.** De eerste twee pogingen werden ~4 s na de start
   afgebroken; het journal zegt letterlijk:
   `session-1643.scope: Killing process 313592 (python3) … 313594 (python3) … 313660 (BudgieLoader.ex)
   with signal SIGTERM`. Dat gebeurt bij het sluiten van de ssh-sessie, en `setsid`/`nohup` helpt
   daar niet tegen (die veranderen de sessie, niet de cgroup). De meting moet daarom als
   systemd-user-unit draaien (`systemd-run --user --unit=… --collect`), wat in
   `re\_dump-start2.sh` staat. Dit kostte twee runs en is de belangrijkste valkuil.
3. **Zonder `DISPLAY` werkt het prima.** De lader pakt uit en laadt de gast volledig met `DISPLAY`
   leeg; er is geen venster op de tv verschenen en er is geen gamescope gebruikt.

---

## 5. Opruiming (expliciet gecontroleerd)

```
$ pgrep -a wine                     -> leeg
$ pgrep -af 'Budgie|launch-direct|dump_budgie|gamescope'  -> leeg
$ systemctl --user is-active _dumpbudgie                  -> inactive
```
De dumper logt zelf ook `NA OPRUIMEN: geen wine-, lader- of gameproces meer gevonden`. Er is niets
naar de NAS geschreven, er is geen `git commit/push/checkout/stash` gedaan, en de nieuwe bestanden
in `re\` staan untracked.

---

## 6. Wat open blijft

1. **De namen van de 258 "naamloze" slots zijn niet te vinden — en ze zijn er ook niet.** Gemeten:
   de gast heeft in `DT_JMPREL` 100 verplaatsingen en die hebben allemaal een naam; de lader vult
   precies die 100. De overige slots in de tweede slottabel horen bij geen enkele verplaatsing en
   blijven staan. Wie die 258 imports wil benoemen, moet ze dus **niet** in de loper zoeken.
   De weg vooruit is de andere kant: de oude `0xb7xxxxxx`-waarde in het bestand hoort bij één
   specifieke functie in de ene bibliotheekversie van de originele kast, en de enige hoop is die
   bibliotheekversie zelf (of een tabel uit de pakker), niet de lader.
2. **De 37 adressen in het laderbeeld die geen export zijn** (`0x0041ee50`, `0x0041c310`, …) zijn nog
   niet allemaal met een naam uit een exporttabel bevestigd. Uit de `0x80000000`-regio komt bij elk
   ervan de juiste naam tevoorschijn (zie §3.5), maar de precieze recordstructuur van die regio is
   niet ontleed: het is een hash-achtige tabel met overlappende naamfragmenten, en de velden zijn
   niet één-op-één nagerekend. Dat is het eerstvolgende stuk werk, en het is klein.
3. **De drie datapunters op `0x083484c0`, `0x083484c4`, `0x083484e0`** (nieuwe waarden
   `0x61202090`, `0x61201fb0`, `0x61202020`, in `msys-2.0.dll`'s datagebied) zijn niet benoemd.
   Vermoedelijk de drie data-exports van de lader (`AmdPowerXpressRequestHighPerformance`,
   `NvOptimusEnablement`, `api`), maar dat is **afgeleid**, niet gemeten.
4. **Wat het ELF-beeld op `0x8008877c` precies is** (`e_entry = 0x081150a7`, een gastadres) is niet
   vastgesteld. De programmakoppen op die plek ontleden niet als een normale ELF; vermoedelijk is
   het een door de lader opgebouwde structuur waar de kop toevallig op lijkt, of een half
   overschreven beeld.
5. **Eén game, 32-bits.** De 64-bits lader (`BudgieLoader_x64.exe`, twee TLS-callbacks, `.rdata`/
   `.pdata`/`.xdata`) is niet gedumpt. Zijn TLS-callback 0 op RVA `0xd33198` en callback 1 op
   `0x3330` zijn nu in principe meetbaar met dezelfde methode (`--profiel Halo` of een andere
   64-bits ElfLdr2-game), maar de 64-bits route is een aparte meting.
6. **Of dezelfde 100-van-100 uitkomst voor JurassicPark en PacmanChomp geldt** is niet gemeten; de
   methode is klaar (`re\_dump_budgie.py --game <profiel>`), en per game kost het ~1 minuut
   draaien en ~600 MB schijf.
