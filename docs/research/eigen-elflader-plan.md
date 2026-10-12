# Eigen ELF-lader: het plan (drie fasen)

*Opgesteld in de coördinatieronde op 2026-10-12. Dit is het plan en het overzicht, **niet** de
meting: de getallen staan in de documenten die eronder genoemd staan. Elke fase noemt wie hem doet
en welk artefact eruit komt.*

## Waarom dit project

`ElfLdr2/BudgieLoader.exe` is de laatste gesloten **lader** in de keten. Zeven games in de
verzameling hangen eraan: Halo, JurassicPark, Terminator, GhostBusters, GoGoStrike, GalagaAssault en
PacmanChomp. **Sinds 2026-10-12 zijn daar negen ElfLdr2-games bij gekomen** toen de eigenaar een batch games toevoegde (zie `patches/NAS-NIEUW.md`), waarvan er één **gepakt** is: Walking Dead. (Target Terror Gold leek gepakt maar is het niet -- de HASPE-treffer daar was een hardwaresymbool; zie de correctie in `patches/NAS-NIEUW.md`.) Wat er aan gemeten is (`patches/ELF-NATIVE.md`):

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

## Waar een eigen lader echt nodig is, en wanneer fase 2 klaar is

**Eerst een eerlijke constatering die de omvang bepaalt.** Voor de **ongepakte** klasse bestaat "onze
eigen lader" in feite al: dat is de kernel. `ld.so` van Linux mapt de `PT_LOAD`-segmenten, past de
`R_386_RELATIVE`/`GLOB_DAT`/`JMP_SLOT`-relocaties toe, zet de TLS op zodat `gs:[0x14]` een geldige
stackcanarie is, draait `DT_INIT` en `DT_INIT_ARRAY`, en springt naar `e_entry`. Precies de lijst die
in fase 2 staat. En de ongepakte games zijn gewone glibc-ELF's met een volledige importtopologie;
gemeten is dat GoGoStrike **link-compleet** is met alleen `LD_LIBRARY_PATH=<ElfLdr2/libs>` (`ldd`
meldt niets ontbrekends). Wat daar ontbreekt is de **sandbox en de binds**, niet een lader-binary --
en die sandbox bestaat al (`scripts/play-native.sh`).

**Waar een eigen lader wél nodig is:**

1. **De gepakte (HASPE-)klasse.** Zes games: GalagaAssault, JurassicPark, PacmanChomp, Halo,
   Target Terror Gold en Walking Dead. Die bestanden zijn **doelbewust onbruikbaar** gemaakt voor
   `ld.so`: nul sectiekoppen, PLT-stubs die `0xdeadbeef` pushen, een GOT die in het bestand al
   gevuld is met adressen uit een ander proces, en 72-82% gewiste importnamen. Hier kan geen enkele
   systeemlader iets mee; dit is het echte werk.
2. **De API-oppervlakte.** Wat BudgieLoader de games geeft en de kernel niet: de freeglut-API,
   `GetElfBase`/`GetElfSymbol`, `get_info`, en de nagebootste bibliotheken.

**Twee lezingen, en het is een beslissing van de eigenaar:**

* **Smal:** onze lader = de lader voor de gepakte klasse + de API-laag. Voor de ongepakte klasse
  laten we de kernel het werk doen (de sterkste vorm van loskoppelen: hoe minder eigen code, hoe
  minder dat stuk kan gaan). Fase 2 is dan een *proef* dat de native route werkt, niet de bouw van
  een lader.
* **Breed:** één eigen lader voor **alles**, ook voor de ongepakte klasse. Dat geeft één codepad,
  volledige controle over TLS en de canarie, en onafhankelijkheid van de `ld.so` van de host. Het
  kost meer werk en meer risico op fouten die de kernel al voor je oplost.

De eigenaar vroeg letterlijk om "onze eigen elflader", dus de brede lezing is het uitgangspunt; maar
het is de moeite waard om te weten dat de smalle variant voor de ongepakte helft hetzelfde resultaat
geeft met veel minder eigen code. Claude bouwt nu de brede variant; die is voor de gepakte klasse
toch nodig.

**Wanneer is fase 2 klaar?** Niet bij "hij compileert", maar bij een meting:

1. `bowling_usa` start **via onze eigen route** -- zonder BudgieLoader en zonder Wine.
2. Het proces is **gezien en draait nog** na minimaal 20 seconden (dezelfde maat als
   `starttest3.sh` gebruikt, zodat de getallen vergelijkbaar zijn; zie `patches/STARTTEST.md`).
3. Er is een uitspraak over het beeld: een schermopname die iets toont, of een met reden
   gedocumenteerd "geen beeld" -- met de trace erbij die zegt waar het stopt.
4. De bestaande fout van GoGoStrike (geen bruikbare grafische modus, zie boven) is ofwel opgelost,
   ofwel precies benoemd met de functie en het adres waar het stopt.

Punt 1 en 2 zijn de harde eis. Punt 3 en 4 mogen "niet gehaald, en hierom" zijn -- een eerlijke
blokkade met een meting eronder is een geldige uitkomst van fase 2.

## Wat nog aan TeknoParrot hangt (nagemeten 2026-10-12)

De native route is bijna los, maar niet helemaal. Twee dingen wijzen nog naar
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/`:

**1. Zes views hebben een `ElfLdr2`-symlink naar de TP-runtime.** Van de zeven ElfLdr2-games gaat het
om GalagaAssault, GhostBusters, GoGoStrike, JurassicPark, PacmanChomp en Terminator (Halo staat er
niet bij). Zolang die link er is, haalt de game zijn bibliotheken uit de map van TeknoParrot.

**2. Tien bibliotheken worden via `@tpui/ElfLdr2/libs/...` in de native-profielen genoemd**, samen
**12 MB**, en **geen enkele** daarvan staat in onze eigen staging
(`~/.local/share/ravengun/runtime/lib32`):

| bibliotheek | genoemd door |
|---|---|
| `libNxCharacter.so.1`, `libNxCooking.so`, `libPhysXCore.so`, `libPhysXLoader.so.1` | JurassicPark, Terminator |
| `libfmod.so.7`, `libfmodstudio.so.7` | JurassicPark |
| `libcsv.so` | GhostBusters |
| `libNL.so.1.6`, `liballeg.so.4.0`, `libstdc++.so.5` | GoGoStrike |

Halo, GalagaAssault en PacmanChomp noemen **geen** enkele `@tpui`-bibliotheek. Het loskoppelen van
de native route is dus klein en concreet: die tien bibliotheken naar onze eigen staging brengen
(12 MB) en de zes symlinks omzetten. Dat hoort bij deze fase, want onze eigen lader mag op den duur
helemaal niet meer in de map van TeknoParrot kijken.

## De drie fasen

### Fase 1 — de huidige lader uitpakken en begrijpen

Twee analyses, read-only, geen game gestart. Ze staan bewust naast elkaar: A kijkt naar de lader,
B naar het spelbestand.

| | wat | artefact |
|---|---|---|
| **1A** | `BudgieLoader.exe` uitpakken (secties `.brr0`-`.brr4`) en de **exporttabel** groeperen — dat is de API-oppervlakte die onze lader moet leveren | `docs/research/budgieloader-uitgepakt.md` |
| **1B** | hoe ElfLdr2 de **gewiste importnamen** oplost: `hints.dat` (2.994.888 bytes = **249.574** × 12; 64-bits 7.325.952 = 457.872 × 16), de tweede PLT/slottabel, en wat de `0xb750xxxx`-groep werkelijk is | `docs/research/importresolutie-elfldr2.md` |

#### Waarom onze lader op GoGoStrike stopt (en dat het geen laderfout is)

*Gemeten 2026-10-12: onze lader `tools/rg-elfloader/rg-elfloader` op een **kopie** van
`bowling_usa` in `/tmp`, dus zonder sandbox, zonder game-data en zonder iets naar de NAS te
schrijven.*

Onze lader mapt de gast, leest zijn dynamische sectie en begint de `DT_NEEDED`-bibliotheken te
laden. Hij stopt op de eerste:

```
rg-elfloader: FATAAL: dlopen(liballeg.so.4.0) mislukt:
.../ElfLdr2/libs/liballeg.so.4.0: undefined symbol: _poly_scanline_gcol8
```


**En let op waarom dat bij de kernelroute níet gebeurt -- dat is de kern van het verschil.** Met de
juiste `LD_LIBRARY_PATH` **draait de kernel de gast wél**: gemeten dat hij zijn eigen code bereikt en
daar stopt op

```
Fatal error: unable to activate the Allegro system
Unable to find a usable VT
```

De kernel lost dus alle imports op, inclusief `_poly_scanline_gcol8`. Dat komt doordat de gast in die
route het **hoofdprogramma** is: de dynamische symbolen van het hoofdprogramma staan automatisch in
de globale opzoekingsscope, en `ld.so` lost de imports van de bibliotheken daartegen op.

Onze lader is het hoofdprogramma, en de gast is dan alleen gemapt geheugen -- dus die symbolen staan
niet in scope. Dat is het hele verschil, en daarom is de shim (of iets dat hetzelfde doet) nodig voor
de lader, maar **niet** voor de kernelroute.

(Mijn eerdere formulering hier -- "de kernel stopt met `libstdc++.so.5`" -- kwam doordat ik de gast
zonder `LD_LIBRARY_PATH` had gestart. Dat was misleidend, en het is rechtgezet.)

Narekenen: `re/_kernproef.sh` in de Windows-werkmap, of `/home/deck/_kernproef.sh` op de Deck.

**Dat is geen fout in de lader maar een eigenschap van deze games.** Wat er gemeten is:

* `bowling_usa` **definieert** `_poly_scanline_gcol8` op `0x08096344` -- een `T` in zijn
  *dynamische* symbooltabel.
* `ElfLdr2/libs/liballeg.so.4.0` heeft hem **ongedefinieerd**: de bibliotheek importeert hem uit het
  hoofdprogramma.
* Die liballeg heeft **476** ongedefinieerde symbolen, waarvan er **280** door geen enkele
  bibliotheek op deze machine geleverd worden -- allemaal Allegro-internen
  (`_accel_bank_switch`, `_i_cx_r`, `_linear_blit16`, ...).

Met andere woorden: deze exe en deze `.so` zijn **twee helften van één Allegro-bouw**. De exe
exporteert de interne symbolen, de `.so` importeert ze. De kernel kan dat niet (hij laadt de `.so` in
zijn eigen scope), en `dlopen` ook niet zolang de symbolen van de gast niet **globaal** zichtbaar
zijn. Dit is precies het omgekeerde van wat BudgieLoader doet: die zet de gast in zijn eigen
adresruimte en lost de imports van de gast op uit zijn eigen exporttabel.

**Wat onze lader dus moet doen:** de dynamische symbolen van de gast in de **globale
opzoekingsscope** zetten vóór de `DT_NEEDED`-bibliotheken geladen worden. Drie wegen, in toenemende
degelijkheid:

1. De gast als `RTLD_GLOBAL`-object registreren. `dlopen` op een ET_EXEC weigert glibc meestal, dus
   dit werkt alleen met een truc.
2. **Een shim-bibliotheek genereren** met de gast-symbolen als absolute adressen
   (`.set naam, 0x08096344`), die met `RTLD_GLOBAL` geladen wordt vóór de rest, en daarna de echte
   bibliotheken. Klein, deterministisch, en het werkt omdat de gast op vaste adressen staat (ET_EXEC
   op `0x08048000`).
3. Zelf alle `DT_NEEDED`-bibliotheken mappen en de symbolen oplossen tegen één gecombineerde tabel
   (gast + alle bibliotheken). Het meeste werk, maar het minst afhankelijk van glibc.

**De rest van de keten blijft staan.** Van de 134 ongedefinieerde symbolen van de gast worden er
maar **44** door liballeg gedekt; de andere 90 zijn libc/C++ (`_ZdaPv`, `_Znaj`, `__cxa_atexit`,
`__errno_location`, `acos`, ...) en komen dus uit `libstdc++.so.5` (uit `ElfLdr2/libs`) en de 32-bits
libc/libm van de host. Buiten de liballeg van ElfLdr2 is er op deze machine **geen** 32-bits
`liballeg` -- de host heeft hem in geen enkele bitbreedte.

#### De oplossing is bewezen: een shim met de symbolen van de gast

Route 2 van hierboven is getest en **werkt**. Het recept, in vier stappen:

1. **De symbolen van de gast ophalen** (alleen de gedefinieerde, met hun adres):
   `nm -D --defined-only bowling_usa | awk '$2 ~ /^[TDBRWV]$/ {print $1, $3}'` geeft **1391**
   symbolen. Let op: `nm` laat de `0x` weg voor het adres, dus die moet je er zelf voor zetten.
2. **Een shim genereren**: voor elk symbool een regel
   `__asm__(".globl NAAM\n.set NAAM, 0xADRES\n");`
   Sla `_init`, `_fini`, `_start`, `__bss_start`, `_edata`, `_end` en de `__init_array_*`/
   `__fini_array_*` over -- die levert de CRT van de shim zelf al, en anders krijg je
   "multiple definition of `_fini`".
3. **Bouwen**: `gcc -m32 -shared -fPIC -Wl,-z,execstack -o shim.so shim.c` geeft 1388 symbolen,
   32-bits, waaronder `_poly_scanline_gcol8`.
4. **Eerst de shim laden met `RTLD_NOW | RTLD_GLOBAL`, dan pas de `DT_NEEDED`-bibliotheken.**

**De meting die het beslist**, met een eigen proefprogramma dus los van de lader:

```
STAP1 shim geladen
STAP2 liballeg GELADEN op 0x57797d50
STAP3 install_sound: 0xf7c16dc0
STAP4 set_gfx_mode:  0xf7becad0
```

Zonder de shim faalt precies dezelfde `dlopen` met `undefined symbol: _poly_scanline_gcol8`. De
aanpak is dus niet alleen theoretisch goed: de bibliotheek laadt, en de twee functies die de
geluids- en de beeldinitialisatie van Allegro aansturen zijn resolveerbaar.

Narekenen: `re/_shimproef4.sh` in de Windows-werkmap, of `/home/deck/_shimproef4.sh` op de Deck.

**Eén voorwaarde om te onthouden:** de shim staat op **absolute adressen** en is dus alleen geldig
zolang de gast op zijn vaste plaats staat (ET_EXEC op `0x08048000`). Dat is precies de aanname waarop
het hele ontwerp rust, maar het betekent wel dat de shim **per gast opnieuw gegenereerd** moet worden.
Dat is één commando, dus dat is geen bezwaar -- het moet alleen niet vergeten worden.

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

**Wat de meting van 2026-10-12 laat zien: de eerste oorzaak is GELUID, niet beeld.**
(Volledige uitwerking: `/home/deck/_gogostrike-native-meting.md`, 480 regels.) De crash is wél de
SRAM-hobbel, maar de game komt daar via een **overgeslagen initialisatie**:

1. `vSystemDeviceInit` roept `install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, argv[0])` aan. Dat
   mislukt: de sandbox heeft geen OSS-apparaten (`/dev/dsp`, `/dev/mixer`, `/dev/sequencer` →
   `ENOENT`), en `allegro_error` wordt `"Insufficient digital voices available"`.
2. Bij een geluidsfout gaat de game naar zijn **eigen vroege uitgang** (`0x805963f` → `jmp 0x8059716`)
   en slaat daarmee de hele rest van de apparaatinitialisatie over: `set_color_depth`,
   `set_gfx_mode(…, 640, 480, …)`, `install_mouse`, `lpSramDeviceOpen`, `vQlDeviceOpen`,
   `vKeyIoDeviceOpen`, `vCoinDeviceInit`, `vPayoutDeviceInit`, `iRS232OpenPort`.
3. `_mangled_main` loopt daarna door naar `uwSramCheckFormatCode()`, dat de globale variabele
   `0x80c2ba8` leest. Die wordt **alleen** in het mmap-succespad van `lpSramDeviceOpen` geschreven en
   is dus `0`. `cmpl $0x69dc065,(%eax)` met `eax = 0` → SIGSEGV op `0x08071915`.

De aantekening in `packaging/native-games.json` ("geen bruikbare grafische modus") beschrijft het
**symptoom** goed en de **eerste oorzaak** verkeerd. En de regel die hier eerder stond -- "de game is
al verder gekomen dan die SRAM-hobbel" -- is **onjuist**: de crash ís die hobbel.

**De drie hobbelen, in de volgorde waarin ze moeten wijken:**

1. **Geluid.** Snelste proef: `LD_PRELOAD` van `libaoss` uit `ElfLdr2/libs` (`libaoss.so.0.0.0` heeft
   `libalsatoss.so.0` nodig, en `/usr/lib32/libasound.so.2.0.0` bestaat). Let op: bind
   `libalsatoss.so.0.0.0` onder de naam `libalsatoss.so.0` in `/rg/lib`, en geef de preload via
   `--env` als je met `--gdb` meet -- `play-native.sh` slaat `LD_PRELOAD` over zodra `--strace` of
   `--gdb` meedoet. Wie het bij het project wil houden kan ook een kleine i386-OSS-stub schrijven die
   `open`/`ioctl` op `/dev/dsp`, `/dev/mixer` en `/dev/sequencer` afvangt en minstens 16 digital
   voices meldt. Een patch op de vroege uitgang (`0x805963f`, of de `je` op `0x805961e`) is de
   kleinste wijziging, maar verbergt de fout.
2. **MIT-SHM.** Met het geluid eenmalig overgeslagen komt de volgende fout: een X-fout `BadAccess` op
   `MIT-SHM X_ShmAttach`, en daarna een SIGSEGV in `set_gfx_mode+270` van `liballeg`. Sterke
   verdachte: `--unshare-ipc` in de sandbox scheidt de SysV-shm-namespace (gemeten: host shmid 61
   tegen binnen de sandbox shmid 0, wederzijds onzichtbaar). Een proefrun **zonder** `--unshare-ipc`
   beslecht het; dat is een wijziging in `play-native.sh`, niet in het profiel.
3. **Het SRAM-apparaat schrijfbaar binden.** `lpSramDeviceOpen` doet `open(O_RDWR)` én
   `mmap(…, PROT_READ|PROT_WRITE, MAP_SHARED, …)`, dus een `--ro-bind` is onvoldoende.
   `play-native.sh` kent alleen `overlay`/`ro` van **mappen** en heeft geen **rw-bestandsbind** -- dat
   is een echte uitbreiding. Inhoudelijk: het voorbereide bestand
   `~/.local/share/ravengun/native/GoGoStrike/w040101sram` (65.536 B) is **volledig nul**, terwijl de
   indelingscode `0x069dc065` in de view-versie (65.537 B) op offset `0xb000` staat -- precies waar
   `uwSramCheckFormatCode` leest (`base+0xb000`). Bind je het nul-bestand, dan meldt de game "niet
   geformatteerd" en neemt hij de `vSramClearAll`-tak. Dat is waarschijnlijk goed voor een eerste
   start, maar het is een bewuste keuze.

**Wat er verder al gestaged is.** `play-native.sh --stage-libs` vult
`~/.local/share/ravengun/runtime/lib32`, en daar staan **9** i386-bibliotheken die `ElfLdr2/libs`
níet levert: `libGLU.so.1`, `libSDL-1.2.so.0`, `libSDL2-2.0.so.0`, `libXi.so.6`, `libXrandr.so.2`,
`libXrender.so.1`, `libesd.so.0`, `libglut.so.3` en `libtheora.so.0`. De "ontbrekende bibliotheken"
uit `patches/ELF-NATIVE.md` zijn dus deels al opgelost -- ze staan alleen niet in `ElfLdr2/libs` maar
in onze eigen staging. Wat nog wél naar de TP-map wijst: het GoGoStrike-profiel haalt zijn drie
bibliotheken (`liballeg.so.4.0`, `libstdc++.so.5`, `libNL.so.1.6`) uit `@tpui/ElfLdr2/libs/...`, dus
voor het loskoppelen moeten die ook naar onze eigen map.

#### Fase 1 is af: wat de uitgepakte lader laat zien (en wat dat voor fase 3 betekent)

Het rapport staat in `docs/research/budgieloader-uitgepakt.md` (785 regels). De uitkomsten die het
plan veranderen:

**1. Statisch uitpakken lukt niet.** Van `BudgieLoader.exe` (PE32 i386, 11.152.552 B) hebben **tien
van de dertien secties `SizeOfRawData = 0`** -- samen 8.317.874 B die niet in het bestand staat. Alleen
`.brr3` (de IAT), `.brr4` (10,9 MB, entropie 7,9714) en `.rsrc` hebben bytes, en `.rsrc` ligt zelfs
*binnen* `.brr4`. De kern van 5,77 MB heeft chi-kwadraat 236 (uniform is 255), entropie 8,0000, en
zlib 1,0003 / lzma 1,0001: **incomprimeerbaar en statistisch identiek aan een stroomcijfer**. Geen
vaste periode, geen interleaving-stap, en dertien omkeerbare bytebewerkingen maken het niet scherper.
Geen enkele bekende pakker matcht.

**De goedkoopste weg is daarom één geheugendump van een lopende lader.** Dat vraagt een bench-run en
het starten van BudgieLoader -- precies wat fase 1 niet mocht. Dat is nu de eerstvolgende stap van
fase 1, en hij is tegelijk de sleutel voor fase 3 (zie punt 3).

**2. `hints.dat` is NIET de sleutel tot de gewiste importnamen.** Het bestand is een levend,
append-only bestand: 32-bits nu **249.574 records** van 12 bytes (de spec zei 235.414; er zijn er
14.160 bijgekomen, mtime vandaag 18:25), 64-bits 457.872 van 16. Het record is (u64 sleutel,
gastadres), maar er zijn maar **13 respectievelijk 15 distincte sleutels** -- een naam-tabel zou er
honderden hebben. **1.463 hash-toetsen** (11 hashfuncties tegen 133 kandidaten, inclusief de volledige
inhoud van alle 96 bibliotheekbestanden) geven **0 treffers**. En de "tweede PLT" `0x08658b10` komt er
**nul keer** in voor. Netto: `hints.dat` groepeert gastadressen naar herkomst, het geeft geen namen.

**3. En de JMPREL-resten zijn nullen, geen namen.** Zelf nagemeten: bij Galaga Assault zegt
`PLTRELSZ` dat er **256** `R_386_JMP_SLOT`-relocaties zijn, maar de eerste ~100 hebben echte slots en
namen (`0x086f83f8` -> `fileno`, `__errno_location`, `open`) en de overige **156** hebben
`r_offset = 0`, symboolindex 0 en `st_name = 0`. Pacman Chomp-Mania: 684 entries waarvan **138** zo,
met r_offsets als `0x296` en `0xb6f` -- aantoonbaar rommel. Jurassic Park: 257 waarvan **156** nul.

Let op het patroon, want het is hetzelfde als bij `nchain`: de pakker **verlaagt** `nchain` (4719 tegen
4977) en **verhoogt** `PLTRELSZ` (256 tegen ~100 echte). Beide tabelgroottes in het bestand zijn dus
niet te vertrouwen; de betrouwbare grenzen zijn `DT_STRTAB - DT_SYMTAB` voor de symbolen, en het
aantal **niet-nul** entries voor JMPREL. Narekenen: `re/_jmprel-naamloos.py`.

**Wat dat samen betekent voor fase 3.** De naamloze imports zijn statisch niet te herleiden: niet uit
`dynsym` (de namen zijn weg), niet uit de GOT-waarden (echte adressen, maar met onze bibliotheken
niet naar namen te herleiden), niet uit `hints.dat` (13 sleutels, geen namen), en niet uit
`DT_JMPREL` (de rest is nul). **De enige weg naar die namen loopt via de lopende lader zelf** -- zijn
resolutietabel bestaat alleen in het geheugen. Dat maakt de geheugendump niet alleen de volgende stap
van fase 1, maar de voorwaarde voor fase 3.

**4. En de API-oppervlakte is nu exact bekend.** De 173 (x64: 174) exports zijn: **6** eigen
laderfuncties (`GetElfBase`, `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`,
`TpRefreshMainInterface`, `get_info`), **3** data-exports, **3** glibc-locale, **161** `glut*` (met 31
`*Ucall`-varianten) en in x64 nog `ElfPreReserveCallback`. Het is **geen freeglut maar een eigen
GLUT-implementatie met freeglut-namen** -- nodig omdat `libglut.so.3` als bestand ontbreekt. En dat is
precies de symbolenset die onze shim globaal moet aanbieden.

#### Eerste meting van onze lader tegen de GEPAKTE klasse (2026-10-12)

Onze lader op een **kopie** van Galaga Assaults `game` in `/tmp` (cwd `/tmp`, `DISPLAY` leeg,
timeout; niets naar de NAS). De vergelijking met de kernelroute is scherp:

| route | uitkomst |
|---|---|
| kernel | **SIGSEGV, 0 bytes uitvoer** -- precies wat `patches/ELF-NATIVE.md` meet |
| onze lader | `shim geladen: 4610 gast-symbolen in de globale scope`, dan `geladen; spel ./game krijgt argv[0]=./game`, dan SIGSEGV |

Onze lader komt dus **voorbij het punt waar de kernel geen enkele instructie van de gast uitvoert**:
hij laadt alle `DT_NEEDED`-bibliotheken, meldt **1 onopgelost symbool in `DT_JMPREL`** (van de
ongeveer 100), bouwt de startstack en geeft de controle aan de gast.

**En dan crasht hij in zijn eigen code, niet in de gast.** Onder gdb:

```
Program received signal SIGSEGV, Segmentation fault.
0x565569c4 in main ()      <- 'start_spel' is static en de bouw is -O2, dus ingelijnd in main
=> 0x565569c4 <main+1924>:  movzwl 0x2a(%ecx),%eax        met ecx = 0xf7600000
eax 0x8048034    esi 0x16    edi 0x0    edx 0xffffdfa9
```

De crash zit dus in het opbouwen van de **startstack/auxv** (`start_spel`), niet in het mappen of het
oplossen van symbolen. Dat is een bug in de lader, en een precieze: `movzwl 0x2a(%ecx)` met `ecx` op
een pagina-grens van een bibliotheekmapping wijst op het lezen van een ELF-kop of een
programma-header via een verkeerd berekende aanwijzer. Relevante bron: `tools/rg-elfloader/rg_elfloader.c`
r.296-330, waar de eigen auxv wordt gelezen (`getauxval(AT_SYSINFO_EHDR)`) en de gast-auxv opgebouwd.

Nog een detail uit de proceskaart dat later aandacht vraagt: het hele gastbeeld staat op
`0x08048000-0x08739000` als **`rwxp`** -- schrijfbaar én uitvoerbaar. Voor het toepassen van de
relocaties is dat te begrijpen, maar een datasegment hoort geen `x` te hebben; dat is een
verhardingspunt voor later, geen blokkade nu.

#### De API-oppervlakte is voor 122 van de 161 namen al in huis (gemeten 2026-10-12)

BudgieLoader exporteert **161** `glut*`-namen. Onze eigen 32-bits
`~/.local/share/ravengun/runtime/lib32/libglut.so.3` exporteert er **171**. De vergelijking:

| | aantal |
|---|---|
| `glut*`-namen van BudgieLoader | 161 |
| `glut*`-namen van onze libglut | 171 |
| **gedekt door onze libglut** | **122** |
| **die wij zelf moeten leveren** | **39** |

Die 39 vallen in twee soorten, en dat maakt het werk overzichtelijk:

* **31 `*Ucall`-varianten** (`glutDisplayFuncUcall`, `glutKeyboardFuncUcall`,
  `glutMultiMotionFuncUcall`, ...). Dat zijn de cdecl-aanroepbare wikkels om de
  callback-registratie: mechanisch werk, één vorm, 31 keer.
* **8 uitbreidingen** die freeglut zelf niet heeft: `glutAppStatusFunc`, `glutInitContextFunc`,
  `glutPositionFunc`, `glutSetVertexAttribCoord3`, `glutSetVertexAttribNormal`,
  `glutSetVertexAttribTexCoord2`, `glutStrokeLengthf`, `glutStrokeWidthf`. Daarvan zijn
  `glutStrokeLengthf`/`Widthf` de float-varianten van de bestaande `glutStrokeLength`/`Width`, en de
  drie `SetVertexAttrib*` zijn kleine setters.

**En er zit een bonus in.** Onze libglut exporteert `glutBitmap8By13` -- precies het gat dat de
eisenmatrix in BudgieLoaders exportlijst vond en dat Jurassic Park vraagt. Door de **echte** libglut
te gebruiken in plaats van een eigen implementatie verdwijnt dat gat vanzelf.

Netto: van de 173 exports hoeven we er dus **39** zelf te maken in plaats van 161. Narekenen:
`re/_glutvergelijk.sh`.

**En die 39 zijn verder te splitsen, zodat het echte werk klein wordt** (tweede meting,
`re/_ucall-check.sh`):

| soort | aantal | wat het is |
|---|---|---|
| `*Ucall`-wikkel om een functie die onze libglut **al heeft** | **28** | mechanisch: één vorm, 28 keer |
| `*Ucall`-wikkel waarvan wij de basis ook niet hebben | **3** | hoort bij de drie nieuwe callbacks hieronder |
| float-variant van een functie die wij hebben (`glutStrokeLengthf`, `glutStrokeWidthf`) | **2** | mechanisch |
| **echt nieuwe callbacks**: `glutAppStatusFunc`, `glutInitContextFunc`, `glutPositionFunc` | **3** | klein, maar nieuw werk -- en elk heeft ook een `*Ucall`-variant |
| **echt nieuwe setters**: `glutSetVertexAttribCoord3`, `Normal`, `TexCoord2` | **3** | klein |

Dus: **30 mechanische wikkels** en **6 kleine nieuwe functies**. Dat is het verschil tussen "161 namen
namaken" en een middag werk.

### Fase 3 — de gepakte klasse en de API-oppervlakte

Pas te beginnen als 1B antwoord geeft op de naamresolutie. Twee delen:

1. **De gepakte (HASPE-)games**: GalagaAssault, JurassicPark, PacmanChomp en Halo -- en sinds 2026-10-12 ook Walking Dead. Deze bestanden
   hebben nul sectiekoppen, PLT-stubs van de vorm `ff 25 <got> 68 ef be ad de e9 <plt0>`, een GOT die
   in het bestand al gevuld is met `0xb7xxxxxx`-adressen uit het oorspronkelijke proces, en 72–82
   procent gewiste importnamen. Zonder de slot-naam-tabel is dit niet te doen; met de tabel wel.

   **En voor ELF64 werkt die beschrijving niet.** Uit de vergelijking van alle tien bestanden
   (`docs/research/packed-elf-vergelijking.md`, 459 regels) blijkt dat Halo weliswaar dezelfde klasse
   is -- `SHNUM = 0`, de marker, een naamloos blok dat **exact achter `DT_HASH.nchain`** begint, en
   `DT_JMPREL` dat naar een tweede slottabel buiten `DT_PLTGOT` wijst -- maar op twee punten
   mechanisch anders:

   * **`0xdeadbeef` is geen 64-bits merker.** Halo heeft er 24, tegen 357/577/442/807 bij 32-bits.
     177 van zijn 202 stubs pushen een **echte** relocatie-index, en alle 178 JUMP_SLOT-relocaties
     hebben een naam: bij Halo is luie binding dus **niet** stukgemaakt.
   * **De structuurstub heeft een andere vorm.** Halo heeft **611 stubs van 8 byte**
     (`ff 25 <rel32>; 66 90`) naar één doorlopend slotbereik met 611 absolute `0x7fd0b...`-adressen;
     32-bits heeft er **nul**. De echte structuurtabel van Halo is dus 611 slots, terwijl de cel
     "24 structuur-stubs" in `patches/ELF-LADER-SPEC.md` alleen de `68 ef be ad de`-telling is --
     die onderschat ELF64 met een factor van ongeveer 25.

   Twee dingen volgen daaruit voor de lader: zoek de 64-bits structuurtabel via het **8-byte-patroon
   met een doorlopend slotbereik**, niet via `deadbeef`; en neem als tabelgrens
   **`(DT_STRTAB - DT_SYMTAB) / DT_SYMENT`** -- `DT_HASH.nchain` is altijd te klein (4719 tegen 4977,
   320 tegen 1021, 220 tegen 681). Dat is nu van drie kanten bevestigd.
2. **Wat BudgieLoader verder levert**: 173 exports — de volledige freeglut-API plus `GetElfBase`,
   `GetElfSymbol`, `GetMainInterfaceIP`, `GetMainInterfaceSubnetMask`, `TpRefreshMainInterface`,
   `AmdPowerXpressRequestHighPerformance`, `NvOptimusEnablement`, `__ctype_b_loc`, `api`, `get_info` —
   en de nagebootste bibliotheken in `libs/`. Voor een native route met echte Mesa valt een deel
   daarvan weg, maar `GetElfBase`/`GetElfSymbol` en de omgevingsafspraken moeten we zelf bieden.

   **Belangrijke nuance uit de eisenmatrix** (`docs/research/budgieloader-api-matrix.md`, 1027 regels):
   de games vragen een veel kleinere API dan die 173. De **werkelijk bij naam gevraagde** functies
   zijn er **16**, en elke naam wordt door precies **één** game gevraagd -- de doorsnede over de zeven
   games is **leeg**. En de conclusie dat de 161 `glut*` van ons moeten komen omdat er geen 32-bits
   `libglut` zou zijn, klopt **niet voor onze eigen route**:
   `~/.local/share/ravengun/runtime/lib32/libglut.so.3` is een echte **32-bits** ELF en exporteert
   **171** `glut*`-namen -- inclusief alle 16 die gemeten zijn én `glutBitmap8By13`, het gat dat de
   matrix in BudgieLoaders exportlijst vond. Voor de native route hoeven we die API dus **niet** na te
   bouwen; we kunnen de echte freeglut gebruiken. Wat wél van ons moet komen zijn de eigen functies
   (`GetElfBase`, `GetElfSymbol`, …).

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
