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

#### Correctie en een betere verdachte: de shim is te breed (2026-10-12)

**Mijn `envc 8`-verdachte vervalt.** De sandbox zet met `--clearenv` precies **acht** variabelen --
`HOME`, `PATH`, `DISPLAY`, `XDG_RUNTIME_DIR`, `LANG`, `LD_LIBRARY_PATH`, `LD_PRELOAD`,
`RG_OSS_STUB_LOG` -- te zien aan de bwrap-regel. Onze lader geeft dus door wat er is, en `envc 8` is
**trouw**, geen afgeslankte omgeving. Ik had het aantal moeten natrekken voordat ik het als eerste vraag
doorgaf; nu rechtgezet.

**En er is een betere verdachte, met getallen.** De shim definieert **alle** gedefinieerde dynamische
symbolen van de gast globaal. Die botsen met de bibliotheken die we daarna laden:

| game | symbolen in de gast | botsingen |
|---|---|---|
| GoGoStrike | 1.400 | **71 met `liballeg.so.4.0`** (`_accel_active`, `_accel_driver`, `_linear_blit*`, ...) + 7 met `libNL` |
| Galaga Assault | 4.616 | **66 met `libstdc++.so.5`** (C++ `type_info`-destructors) + 8 met `liballeg` + 3 met `libc` (`stdin`/`stdout`/`stderr`) |

En dat is geen theorie over naamruimte: **DSO-symbolen zijn preemptible.** De shim wordt als **eerste**
met `RTLD_GLOBAL` geladen, dus hij staat vooraan in de opzoekingsscope. Roept `liballeg` intern
`_accel_active` aan, dan kan die aanroep naar **de gast** wijzen in plaats van naar liballeg's eigen
code. Dat is precies het soort fout dat pas na de initialisatie toeslaat -- en de gast sterft hier aan
signaal 11 nadat de geluidsinitialisatie is gelukt.

**Wat de shim hoort te zijn: smal.** Hij is nodig omdat de bibliotheken symbolen **van de gast**
importeren (`liballeg` wil `_poly_scanline_gcol8` uit het hoofdprogramma). Maar het is niet nodig om
alle 1.400 (of 4.616) symbolen globaal te zetten: alleen die welke de geladen bibliotheken werkelijk
**ongedefinieerd** hebben en die geen andere bibliotheek levert. Dan blijft `_poly_scanline_gcol8`
werken en verdwijnt de preemptie van `_accel_active` en de C++-destructors.

Narekenen: `re/_shim-breed2.py`.

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

#### Fase 2 is gehaald: de native route start GoGoStrike (2026-10-12)

De drie bekende hobbelen zijn één voor één weggehaald, met een meting per stap. Verslag:
`docs/research/gogostrike-hobbel2.md`.

| hobbel | oplossing | bewijs |
|---|---|---|
| geluid | eigen i386-OSS-stub in `tools/rg-oss-stub/`, geladen met `LD_PRELOAD` | `install_sound` slaagt; geen `/dev/dsp` meer in de strace |
| MIT-SHM | `--unshare-ipc` **weglaten** in de sandbox | geen enkele X-foutregel meer; de droogloop telt de vlag 1x bij het origineel en 0x bij de kopie |
| SRAM | eigen `--bind-rw S D` op een kopie van de launcher | `openat("/dev/w040101sram0", O_RDWR) = 17`, en in de proceskaart `rwxs ... /dev/w040101sram0` |

**En daarmee zijn de criteria van fase 2 gehaald:**

1. `bowling_usa` start via onze **eigen** route, zonder BudgieLoader en zonder Wine -- ja.
2. Het proces is gezien **en draait nog**: toestand `R`, 776 -> 1178 CPU-ticks in 4 s, en het sterft
   pas op de tijdslimiet (`exit 143` na 30 s). Drie runs lang.
3. Er is een uitspraak over het beeld: een PNG van 1920x1080 met **het eigen ASSERT-scherm van het
   spel** -- `ASSERT FAIL / COND: p100>=0 / FILE: sys/c/ql.c / LINE: 80`.

Dat is nog geen speelbaar spel, maar het is precies wat het criterium vraagt: het spel **loopt** op
onze eigen route, en wat het tegenhoudt is nu een concrete assert in de game zelf in plaats van een
laadprobleem.

**Twee vondsten die het onthouden waard zijn.** De OSS-stub moet `/dev/sequencer` **niet** faken: met
een nagebootste sequencer geeft `install_sound` -1 met `allegro_error = "No support synth type
found"` en komt de oude crash terug. De sequencer met rust laten (`ENOENT`) laat Allegro terugvallen
op digmid, en dan slaagt het wel. En de twee SRAM-inhouden (nul-bestand van 65.536 B en de
view-versie van 65.537 B met de indelingscode op `0xb000`) geven een **identiek** resultaat:
`uwSramCheckFormatCode` kijkt naar offset 0, niet naar `base+0xb000`.

**De vierde hobbel, al gemeten.** De assert komt direct na
`openat("/dev/w040101_ctl", O_RDWR|O_NONBLOCK) = -1 ENOENT`. Met acht rw-gebonden nulbestanden op
`/dev/w040101_{ctl,io0,io1,io2,io101,io104,io105,io106}` wijkt die assert, loopt het spel door tot
`openat("/dev/ttyS0", ...) = -1 ENOENT`, en sterft dan aan SIGSEGV met `si_addr=0x6a238abf`. De
volgende stap is dus de PAOKAI-apparaten op **ioctl**-niveau plus `/dev/ttyS0`.

**En het is nog niet uitgerold.** `scripts/play-native.sh` heeft nog steeds `--unshare-ipc` en geen
rw-bind, en het profiel laadt de stub niet. De proeven zijn op een **kopie** gedaan
(`/home/deck/_gn-noshm.sh`); de exacte zesregelige diff staat in het verslag. Het overzetten is werk
voor de eigenaar van dat bestand.

**Let op: er staan twee stub-mappen.** De werkende is `tools/rg-oss-stub/`
(`rg_oss_stub.c`, `gen.sh`, `librg-oss-stub.so`, header-vrij gebouwd omdat `gnu/stubs-32.h` ontbreekt).
Er is daarnaast een `tools/rg-osstub/` dat hier **niet compileert**; gebruik die niet.

#### "Fase 2 compleet" is half: de launcher kan het, het profiel nog niet (2026-10-12)

Claude heeft de drie fase-2-oplossingen in `scripts/play-native.sh` gezet (commit `2c9083b`):
`--bind-rw`, `--share-ipc` en `LD_PRELOAD` via het profielveld `preload`. **Maar het profiel kan ze
niet aanvragen.** De lezer in dat script kent maar negen sleutels:

```
cwd   exe   home   launcher   note   preload   res   status   title
```

`--share-ipc` en `--bind-rw` zijn dus **alleen commandoregels**, en het GoGoStrike-profiel staat nog op
`enabled: false`, `status: geparkeerd`, `preload: ""` -- met een **achterhaalde note** ("geen bruikbare
grafische modus"), terwijl de meting uitwees dat de eerste oorzaak het **geluid** is.

Wie GoGoStrike nu via de profielroute start, moet dus drie dingen met de hand meegeven. Dat is precies
wat een profiel hoort te voorkomen, en het betekent dat de drie bewezen oplossingen wel in het
**script** zitten maar nog niet in de **game**.

**En dat is inmiddels gedaan en nagerekend** (Claude's commit `7734186`, gecontroleerd in ronde 34):

* de lezer kent de twee velden nu (regels 96, 98 en 174 van `play-native.sh`);
* het GoGoStrike-profiel staat op `enabled: true`, `status: "deels"`, `share_ipc: true` en
  `binds_rw: [["@data/native/GoGoStrike/w040101sram", "/dev/w040101sram0"]]`;
* de OSS-stub zit in de **`libs`-lijst** als `@lib32/librg-oss-stub.so`, en die lijst wordt naar
  `/rg/lib/<basename>` gebonden -- precies het pad dat `preload` noemt. De stub wordt dus echt geladen;
* de note is rechtgezet en noemt nu het geluid als eerste oorzaak, de twee valkuilen
  (`/dev/sequencer` niet nabootsen, `libalsatoss.so.0` onder exact die naam) en de volgende hobbel;
* en de **droogloop** laat zien dat de vlaggen uit het profiel komen:
  `... --bind-rw .../w040101sram /dev/w040101sram0 --share-ipc --headless`.

De drie bewezen oplossingen zitten dus nu in de **game**, niet alleen in het script. Narekenen:
`re/_uitrol-verify.sh` en `re/_stub-binding.sh`.

#### Onze eigen lader draait de ongepakte gast -- fase 2 letterlijk gehaald (2026-10-12)

Fase 2 vraagt letterlijk: *bouw een eigen i386-ELF-lader en bewijs hem einde-tot-einde op de ongepakte
klasse, te beginnen met GoGoStrike*. Tot nu toe was onze lader alleen op de **gepakte** klasse getest
(Galaga Assault); de fase-2-startproef liep via de kernel (`play-native.sh`). Dat gat is nu gedicht.

Onze lader op een **kopie** van `bowling_usa` in `/tmp` (cwd `/tmp`, `DISPLAY` leeg, dus niets naar de
NAS en het scherm van de eigenaar blijft vrij):

```
rg-elfloader: shim geladen: 1385 gast-symbolen in de globale scope
rg-elfloader: LET OP: 1 onopgeloste symbolen in DT_REL
rg-elfloader: geladen; spel ./bowling_usa krijgt argv[0]=./bowling_usa
rg-elfloader: spring naar e_entry 0x8059410 (esp 0xf7870ea0, argc 1, envc 20, 17 auxv)
Fatal error: unable to activate the Allegro system
Unable to find a usable VT
```

**En die laatste twee regels zijn het bewijs.** Ze komen van het **spel zelf** -- het is Allegro's eigen
foutmelding -- en ze zijn **identiek** aan wat de kernelroute op hetzelfde bestand geeft (die gaf
dezelfde twee regels, `rc=1`). Onze lader doet dus alles wat de kernel doet op deze gast:

* `PT_LOAD` mappen op de vaste basis `0x08048000`;
* de relocaties toepassen (1 onopgelost in `DT_REL`);
* de shim genereren en laden -- 1.385 gast-symbolen in de globale scope;
* de `DT_NEEDED`-bibliotheken laden;
* de startstack opbouwen (`argc 1`, `envc 20`, `17 auxv`);
* naar `e_entry 0x8059410` springen, waarna de gast **loopt**.

Het verschil dat overblijft is de omgeving, niet de lader: zonder `DISPLAY` vindt Allegro geen bruikbare
VT. Dat is precies wat `play-native.sh` levert -- en die route laat GoGoStrike 30 seconden leven.

**Wat er daarmee nog ontbreekt, en het is één stap:** de twee zijn nooit **gecombineerd** gemeten. De
startproef van fase 2 loopt via de kernel, en onze lader is los getest. Om fase 2 helemaal dicht te
maken moet `play-native.sh` de optie krijgen om **onze lader** in de keten te zetten in plaats van de
kernel-`ld.so`, en dan moet GoGoStrike daar 30 seconden leven. Dat is werk in de lader-repo.

#### De dragende stukken van de fase-2-keten zijn nagerekend (2026-10-12)

De uitrol zet drie dingen in de keten die stuk kunnen zijn zonder dat je het merkt: een preload die
niet laadt (glibc waarschuwt en gaat door), een bibliotheek die niet op het pad staat dat het profiel
noemt, en een rw-bind waarvan de bron ontbreekt. Alle drie gecontroleerd, los van de bouwer:

| wat | uitkomst |
|---|---|
| `~/.local/share/ravengun/runtime/lib32/librg-oss-stub.so` | bestaat, `ELF 32-bit LSB shared object, Intel i386`, 15.432 B |
| identiek aan de repo-kopie (`tools/rg-oss-stub/`)? | **ja**, zelfde md5 `cb495f5c...` -- dus geen drift tussen repo en werkende bestand |
| laadt hij? | **ja**, `dlopen` slaagt |
| exporteert hij de haken? | `open`, `open64`, `openat`, `openat64`, `ioctl` -- alle vijf aanwezig |
| SRAM-bron van de rw-bind | `native/GoGoStrike/w040101sram`, **65.536 bytes** (= het minimum dat de game eist) |

Het pad klopt ook: het profiel noemt `@lib32/librg-oss-stub.so` en `preload:
/rg/lib/librg-oss-stub.so`, en de `libs`-lijst wordt naar `/rg/lib/<basename>` gebonden -- dus de
preload verwijst naar het bestand dat de sandbox binnenkomt.

Narekenen: `re/_stubverify.sh`.

#### Onze lader zit in de keten en draait de gast -- die crasht daarna (2026-10-12)

De `--loader`-bedrading is ver genoeg om te meten (de vlag werkt; de profielwijziging was nog niet
gecommit, dus de proef is met `--loader` op de commandoregel gedaan). Uit het spellogboek:

```
cmd: /rg/bin/rg-elfloader /pm/bowling_usa
rg-elfloader: shim geladen: 1385 gast-symbolen in de globale scope
rg-elfloader:   onopgelost DT_REL: __gmon_start__
rg-elfloader: LET OP: 1 onopgeloste symbolen in DT_REL
rg-elfloader: geladen; spel /pm/bowling_usa krijgt argv[0]=/pm/bowling_usa
rg-elfloader: spring naar e_entry 0x8059410 (esp 0xf7830ed0, argc 1, envc 8, 17 auxv)
rg-oss-stub: nep-open mixer ret=3
rg-oss-stub: nep-open dsp ret=5
rg-oss-stub: seq doorgelaten (geen nep) seq ret=4294967295
rg-oss-stub: nep-open dsp ret=5
Shutting down Allegro due to signal #11
== exit 139
```

**Wat er goed gaat:** onze lader zit in de keten, bouwt de shim (1.385 gast-symbolen), past de
relocaties toe, meldt netjes **bij naam** welk symbool onopgelost blijft (`__gmon_start__`, een
onschuldige weak-symbool -- precies de verbetering die gevraagd was), bouwt de startstack, springt naar
`e_entry 0x8059410`, en **de gast loopt**: hij bereikt de geluidsinitialisatie en roept onze OSS-stub
aan. En die stub doet exact wat hij moet doen: mixer en dsp worden nagebootst, en `/dev/sequencer`
wordt **met rust gelaten** ("seq doorgelaten (geen nep)") -- precies de valkuil die in de meting stond.

**Wat er misgaat:** daarna sterft de gast aan **signaal 11** (exit 139). De **kernelroute met dezelfde
vlaggen leeft 30 seconden** op hetzelfde bestand. Dat is dus een verschil dat door onze lader wordt
veroorzaakt, en het is een concreet aanknopingspunt.

**En er staat een verdachte in dezelfde regel.** Onze lader bouwt de startstack met **`envc 8`**, terwijl
de losse proef buiten de sandbox `envc 20` gaf. De sandbox draait met `--clearenv` en zet daarna een
handvol variabelen; als de lader alleen doorgeeft wat hij aantreft, krijgt de gast dus een
**afgeslankte omgeving** -- en juist `DISPLAY`, `XDG_RUNTIME_DIR` en `HOME` zijn dingen waar Allegro's
X- en geluidspad op leunen. Dat is de eerste vraag om uit te zoeken: welke acht variabelen komen er
door, en welke mist de gast?

**En een tweede, kleinere observatie:** zes keer staat er
`ERROR: ld.so: object '/rg/lib/librg-oss-stub.so' from LD_PRELOAD cannot be preloaded (wrong ELF class:
ELFCLASS32): ignored.` Dat is een **64-bits** `ld.so` die de 32-bits stub weigert. De stub werkt (zijn
logregels staan er), dus dit is ruis van een 64-bits proces in de keten -- maar het is wel ruis die een
lezer laat denken dat de preload faalt.

#### Bewezen: de shim preempt 64 BSS-globals van liballeg (2026-10-12)

De hypothese uit de vorige sectie is nu **los van onze lader** getoetst, met dezelfde shim en een eigen
32-bits proefprogramma: shim laden met `RTLD_GLOBAL`, dan `liballeg.so.4.0` met `RTLD_GLOBAL`, en dan
kijken waar `dlsym(RTLD_DEFAULT, naam)` uitkomt tegenover `dlsym(liballeg, naam)`.

```
_accel_active         globaal=0x80bc8a8  liballeg-eigen=0xf7cf8d28  shim=0x80bc8a8   <- SHIM WINT
_accel_driver         globaal=0x80bb048  liballeg-eigen=0xf7cf8d24  shim=0x80bb048   <- SHIM WINT
_poly_scanline_gcol8  globaal=0x8096344  liballeg-eigen=(nil)       shim=0x8096344   <- shim, terecht
```

De eerste twee zijn het bewijs: `liballeg` **definieert** die symbolen zelf, maar de globale opzoeking
geeft het adres van de **shim** (dus van de gast). De derde regel laat zien waarvoor de shim bedoeld is:
`_poly_scanline_gcol8` heeft liballeg niet zelf, en daar is de shim de juiste bron.

**En de soort maakt het scherper.** Van de 66 botsingen tussen de gast en liballeg zijn er

| soort | aantal | voorbeelden |
|---|---|---|
| **`B` -- BSS-data** | **64** | `_accel_active`, `_accel_driver`, `_accel_idle`, `_blender_alpha`, `_blender_col_15/16/24/32`, `_blender_func15/15x/16/16x/24` |
| `T` -- code | 2 | |

Dus de shim kaapt **64 van liballeg's eigen veranderlijke globals**. Elke keer dat liballeg intern
`_blender_func16` of `_accel_driver` leest of schrijft, doet hij dat op **de kopie van de gast** -- terwijl
de gast zijn eigen kopie ook gebruikt. Twee versies van Allegro's globale toestand, met liballeg's
inwendige code aan die van de gast vastgeknoopt. Dat is precies het soort fout dat pas toeslaat als het
spel echt werk gaat doen, en niet tijdens de initialisatie.

**De voorspelling is daarmee scherp:** beperk de shim tot wat de geladen bibliotheken werkelijk
**ongedefinieerd** hebben (zoals `_poly_scanline_gcol8`) en laat de 64 BSS-botsingen weg -- dan houdt
liballeg zijn eigen globals, en GoGoStrike hoort door signaal 11 heen te komen via de `--loader`-route.
Als dat niet gebeurt, is de volgende verdachte de TLS- en `%gs`-overerving.

Narekenen: `re/_preemptie.sh` (de toets) en `re/_botsing-soort2.sh` (de soorten).

#### De smalle shim werkt: 1.388 -> 279 symbolen, en liballeg houdt zijn globals (2026-10-12)

De fix is nu ook bewezen, in dezelfde losse opstelling (`re/_smalshim2.sh`). De smalle verzameling is
scherp af te bakenen:

* de gast definieert **1.391** dynamische symbolen;
* de drie geladen bibliotheken samen definieren **4.499** symbolen en hebben **564** ongedefinieerd;
* gast-symbolen die die bibliotheken **nodig** hebben: **278**;
* en daarvan levert **geen enkele** bibliotheek er zelf een -- er valt dus niets meer af te trekken.

Dat geeft een shim van **279** symbolen in plaats van 1.388: een factor vijf kleiner, met dezelfde
dekking. En de toets in dezelfde opstelling als de preemptiemeting:

```
_accel_active         globaal=0xf7c47d28  eigen=0xf7c47d28  shim=(nil)      liballeg houdt hem
_accel_driver         globaal=0xf7c47d24  eigen=0xf7c47d24  shim=(nil)      liballeg houdt hem
_blender_func16       globaal=0xf7c3b774  eigen=0xf7c3b774  shim=(nil)      liballeg houdt hem
_poly_scanline_gcol8  globaal=0x8096344   eigen=(nil)       shim=0x8096344  shim levert (goed)
```

**Alle 64 BSS-preempties zijn weg, en het symbool waarvoor de shim bestaat komt nog steeds uit de
shim.** De voorspelling is daarmee niet alleen scherp maar ook **uitvoerbaar gemaakt**: de regel is
"gast-symbolen die de geladen bibliotheken ongedefinieerd hebben, minus alles wat die bibliotheken zelf
definiëren", en dat levert 278 namen op.

Narekenen: `re/_smalshim2.sh` (bouwt de smalle shim en toetst hem).

#### TLS en DT_INIT uitgesloten -- met een controle die de methode bewijst (2026-10-12)

Na de bewezen shim-preemptie bleven twee verdachten over voor de signaal-11-crash: TLS/`%gs`, en de
`DT_INIT` van de gast (onze lader draait die bewust **niet** zelf, met de redenering dat
`_start -> __libc_start_main -> __libc_csu_init` hem wel draait). Beide zijn nu **uitgesloten**.

**TLS:** `bowling_usa` heeft **geen `PT_TLS`-segment** (alleen twee `LOAD`s) en **nul** TLS-relocaties.
Er valt voor deze gast niets op te zetten, dus TLS kan de crash niet verklaren.

**DT_INIT:** getoetst door in een **kopie** een `int3` (`0xCC`) op het adres te zetten en naar de
exitstatus te kijken. Draait de code, dan trapt het proces (signaal 5, rc 133); draait hij niet, dan
gaat het spel gewoon verder met zijn gebruikelijke afloop.

| patch in de kopie | exit | betekenis |
|---|---|---|
| `int3` op `e_entry` (controle) | **133** (SIGTRAP) | de methode werkt: het entry point wordt uitgevoerd |
| ongepatcht | 1 | de gewone afloop (Allegro's eigen foutmelding) |
| `int3` op `DT_INIT` `0x8058ba8` | **133** (SIGTRAP) | **de `DT_INIT` van de gast loopt wel degelijk** |
| `int3` op `DT_FINI` `0x80a5f64` | 1 | niet geraakt -- logisch, de foutweg loopt niet langs `atexit` |

De controle-regel is hier het belangrijkste: mijn **eerste** poging was een gdb-breekpunt, en dat kon
niet armeren omdat het gastbeeld bij de start nog niet gemapt is (`Cannot access memory at address
0x8058ba8`). Zonder die controle had "NEE, de init loopt niet" als bevinding in dit plan gestaan. Met
de `int3`-methode en een controlepunt op `e_entry` is de uitspraak wel hard.

**Wat dat betekent:** van de drie kandidaten is er nu één over, en dat is degene die bewezen is --
de symboolpreemptie door de te brede shim. TLS en `DT_INIT` hoeven niet meer onderzocht te worden.

Narekenen: `re/_initproef2.sh` (de toets) en `re/_init-check.sh` (de dynamische tags en de TLS-vraag).

#### De smalle-shim-regel voor beide klassen -- en een correctie (2026-10-12)

Dezelfde regel ("gast-symbolen die de geladen bibliotheken ongedefinieerd hebben, min wat die
bibliotheken zelf definiëren") doorgerekend voor beide klassen, met **alleen** de bibliotheken die de
gast zelf in `DT_NEEDED` noemt:

| game | gast-symbolen | daarvan door de libs nodig | **smalle shim** | botsingen die zouden preempten | blijven daarvan over |
|---|---|---|---|---|---|
| GoGoStrike | 1.400 | 279 | **279** | 71 | **0** |
| Galaga Assault | 4.616 | **2** | **1** | 3 | **0** |

**En dat corrigeert een getal dat ik eerder gaf.** Ik schreef voor Galaga "66 botsingen met
`libstdc++.so.5`" en "8 met `liballeg`". Die twee bibliotheken staan **niet** in Galaga's `DT_NEEDED`,
dus op het moment dat de gast draait worden ze niet geladen en kunnen ze niets preempten. Het
relevante aantal voor Galaga is **3** -- en die verdwijnen alle drie in de smalle shim. Voor GoGoStrike
blijft 71 staan; daar is `liballeg` wél een echte `DT_NEEDED`.

**Wat dat per klasse betekent:**

* **GoGoStrike (ongepakt):** de bibliotheken hebben 279 gast-symbolen nodig, dus de shim is echt nodig
  -- en de 71 preempties moeten weg. Dit is waar de diagnose van de signaal-11-crash op rust.
* **Galaga (gepakt):** de bibliotheken hebben **twee** gast-symbolen nodig. De shim kan van 4.616 naar
  **1** symbool, en daarmee verdwijnen alle drie de botsingen. Voor de gepakte klasse is de brede shim
  dus vooral een onnodig risico.

Eén regel dekt beide klassen, en de winst is groot: 5x kleiner voor de ongepakte, 4.600x kleiner voor
de gepakte klasse. Narekenen: `re/_smal-alle.py`.

#### De preemptie is echt, maar het bewijs van schade deugde niet (2026-10-12)

Ik wilde laten zien wat de preemptie **aanricht**: niet alleen dat de shim wint, maar wat er dan in die
globals staat. Die toets is mislukt, en op een leerzame manier.

Met de brede shim **segfaultte mijn eigen proefprogramma** -- maar niet omdat liballeg viel. Het las de
adressen die `dlsym` teruggaf: de kopieen van de gast op `0x080b...`, en in die opstelling is het
gastbeeld **niet gemapt**. Mijn eigen dereference faultte dus. Met de smalle shim liep het wel, en daar
wezen alle drie de symbolen netjes naar liballeg -- met inhoud nul, omdat `allegro_init()` nooit is
aangeroepen. De toets zegt dus **niets** over schade.

**En dat relativeert mijn eigen hypothese.** De preemptie is bewezen, de **schade** niet. In de echte run
is het gastbeeld wel gemapt, dus liballeg's verwijzingen komen uit in de gegevens van de gast -- en omdat
beide naar **hetzelfde object** wijzen, is de toestand **gedeeld** in plaats van gedupliceerd. Wat
liballeg schrijft leest de gast, en omgekeerd. Dat kan best consistent werken. Ik had aangenomen dat het
misgaat omdat er twee kopieen zouden zijn; in de praktijk is het er een.

**Wat dat betekent voor de smalle shim:** het blijft een goede wijziging -- 5x kleiner voor de ongepakte
en 4600x voor de gepakte klasse, en het haalt een onnodige koppeling weg -- maar het is **geen bewezen
oplossing** voor de signaal-11-crash. De ketenproef moet uitwijzen of het helpt. Dat zet ik er liever
eerlijk bij dan dat de wijziging als gegarandeerde kuur in dit plan belandt.

Narekenen: `re/_preemptie-inhoud.sh` (en let op: die toets is dus zelf ongeldig voor de brede shim).

#### De crash is gelokaliseerd: liballeg's textout met een NULL-argument (2026-10-12)

Ik zocht een backtrace en vond hem ergens waar ik niet meer keek: **`coredumpctl` bewaart elke crash.**
Daar staan alle runs in, inclusief de ketenruns van 19:50 en 19:52, en zelfs mijn `int3`-proeven van
19:59 (als SIGTRAP) -- een mooie onafhankelijke bevestiging van de methode uit de vorige sectie.

**En de twee ketenruns geven exact dezelfde plek:**

```
#0  0x00000000f7a1bfc8  n/a (/rg/lib/liballeg.so.4.0 + 0x72fc8)
```

Met de core zelf uitgelezen (die is alleen voor root leesbaar; `coredumpctl dump` schrijft hem
overigens **ongecomprimeerd** ondanks de `.zst`-naam):

| | |
|---|---|
| `eip` | `liballeg.so.4.0 + 0x72fc8` |
| dichtstbijzijnde symbool | **`textout`** (`0x72fb0`, dus +0x18) |
| de instructie | `mov ecx, [eax+8]` |
| `eax` | **0** |
| `ebp` | `0xffffffff` (geen geldige framepointer) |
| `ebx` | `0xf7a61064` |
| `esp` | `0xf7830adc` |
| welke thread | **LWP 2** (niet de hoofdthread) |

**Dus: `liballeg`'s `textout` krijgt een NULL-aanwijzer en derefereert die meteen.** En dat gebeurt op
een **tweede thread**, niet op de hoofdthread.

**En er is een tweede, vreemde aanwijzing.** De lader meldde in diezelfde run dat de gast begon met
`esp 0xf7830ed0`. De gecrashte thread heeft `esp 0xf7830adc` -- ongeveer **1 kB daaronder**. Een echte
pthread krijgt zijn eigen stapel ergens anders (meestal laag in het mmap-gebied). Een tweede LWP die
vlak naast de hoofdstapel loopt is niet wat het hoort te zijn, en dat is nu de meest concrete
verdenking: **de tweede thread deelt de stapel van de hoofdthread.**

**En nog twee dingen die de core laat zien, en die goed nieuws zijn:** de gemapte bestanden van het
proces bevatten `/run/user/1000/rg-elfloader-2/shim.so` (de shim was dus geladen) en
`/dev/w040101sram0` (de rw-bind van het SRAM-apparaat werkte).

**En een aparte bug, gevonden doordat ik het via `--gdb` probeerde:** met `play-native.sh --gdb
--loader` **laadt onze lader `liballeg` niet**:

```
rg-elfloader: FATAAL: dlopen(liballeg.so.4.0) mislukt: /rg/lib/liballeg.so.4.0:
              undefined symbol: _poly_scanline_gcol8
```

Dat is precies het symbool waarvoor de shim bestaat, dus in die route staat de shim niet klaar voordat
`liballeg` geladen wordt. Dat is een echte bug in de volgorde (of in het `--gdb`-pad), los van de crash.

**Wat mijn eerste stapelanalyse betreft: die mislukte, en op de bekende manier.** Ik had de mappings tot
30 regels afgekapt, dus mijn classificator had de bereiken van de gast en van liballeg niet en meldde
**121 van 121** woorden als "?" -- een leeg resultaat dat als uitkomst leest. De volgende stap is
dezelfde scan met de **volledige** mappings, om te zien wie `textout` aanriep.

#### De crashketen in liballeg -- en een correctie op mijn eigen verdenking (2026-10-12)

De stapel van de gecrashte thread bevat terugkeeradressen die in liballeg vallen. Omgerekend naar
offset in de bibliotheek (basis `0xf79a9000`, volgt uit de crash zelf):

| stapelwoord | offset | dichtstbijzijnde symbool |
|---|---|---|
| `0xf7a1c693` | `0x073693` | **`textprintf` +0x63** |
| `0xf7a3ae39` | `0x091e39` | `_xwin_scroll_screen` +0x109 |
| `0xf7a3af7a` | `0x091f7a` | `_xwin_update_screen` +0x7a |
| `0xf7a2e996` | `0x085996` | `_xwin_replace_vtable` +0x156 |
| `0xf7a71da0` | `0x0c8da0` | `_screen_vtable` |
| `0xf79d6dd9` | `0x02ddd9` | `config_is_hooked` +0xf9 |
| `0xf79d6f46` | `0x02df46` | `get_config_string` +0xe6 |

Dus de keten is **`_xwin_update_screen` -> `_xwin_scroll_screen` -> `textprintf` -> `textout`**: Allegro's
**X11-venster/console-pad**, met in `textout` een `eax = 0`. En `config_is_hooked`/`get_config_string`
erbij betekent dat Allegro ook zijn configuratie aan het lezen is.

**En wie die tweede thread maakt, is nu bekend.** De gast importeert **niets** van `clone`,
`pthread_create`, `pthread_self`, `__clone`, `fork` of `vfork` -- nul. Maar `liballeg` heeft ze wel als
verplaatsing:

```
000b8290  00017c07 R_386_JUMP_SLOT   pthread_create@GLIBC_2.1
000b8244  00015207 R_386_JUMP_SLOT   fork@GLIBC_2.0
```

**De tweede LWP wordt dus door liballeg zelf gemaakt**, met `pthread_create`, en de stapel is daarmee
glibc's zaak -- niet die van onze lader.

**En daarmee moet ik mijn eigen verdenking van de vorige sectie afzwakken.** Ik schreef dat een esp van
`0xf7830adc`, ongeveer 1 kB onder de start-esp van de hoofdthread (`0xf7830ed0`), erop wees dat de tweede
thread de hoofdstapel deelt. Maar glibc mmap't threadstapels **vlak onder** de hoofdstapel, dus een
thread die net iets lager begint is ongeveer wat je hoort te zien. Dat is dus geen anomalie. De
kanttekening blijft wel dat de framepointer ongeldig is (`ebp = 0xffffffff`) en dat de
"dichtstbijzijnde symbool"-labels zwak zijn -- `_fini + 0xdbc` komt er ook in voor, wat laat zien dat
zo'n label niets bewijst zonder de functiegrenzen. De namen hierboven zijn dus **indicatief**.

**Wat dit oplevert voor de volgende stap:** de crash zit in Allegro's X11-pad, vlak na de geslaagde
geluidsinitialisatie, met een NULL. De vraag is dus of `set_gfx_mode` in **onze** route eigenlijk wel
wordt bereikt en slaagt -- in de kernelroute slaagt hij (de game tekent op 1920x1080). Dat is een
concrete, afgebakende vraag, en liballeg importeert ook `XInitThreads`, wat bij X11 en threads precies
het soort probleem is dat hier speelt.

#### De crash hoort bij Allegro's XWIN-driver -- en een detail dat veiligheid raakt (2026-10-12)

In de overlay staat het configuratiebestand dat het spel zelf schrijft:

```
~/.local/share/ravengun/native/GoGoStrike/0/upper/.allegrorc
[graphics]
gfx_card  = XWIN
gfx_cardw = XWIN
[system]
keyboard = XWINDOWS
mouse    = XWINDOWS
```

**`XWIN` is Allegro's X11-vensterdriver** -- precies de code uit de crashketen
(`_xwin_update_screen` -> `_xwin_scroll_screen` -> `textprintf` -> `textout`, met `eax = 0`). Daarmee
sluit de crash aan op de driver die het spel zelf kiest, en niet op iets wat wij zouden hebben
veroorzaakt in de mappen of de relocaties.

**En het spel opent zijn eigen logboek en schrijft er niets meer in.** In dezelfde overlay staat
`bowling.log` van 19:52 -- de ketenrun -- met **0 bytes**. Het spel maakt het bestand dus aan bij het
starten en sterft voordat het iets te melden heeft. Dat is zelf informatie: de crash komt vroeg, niet
na een reeks logregels.

**En het eigen logboek van het spel uit de ElfLdr2-route (in de view) zegt bovendien:**

```
Sound    API: Unknown
Graphics API: XWindows
-------------
Starting Game
```

Dus het spel kiest expliciet de **XWindows**-weg, en het haalde onder de gesloten lader de regel
"Starting Game".

**En dan een detail dat de veiligheid raakt, en dat ik expliciet wil vastleggen.**
`runtime/views/GoGoStrike/bowling.log`, `bowling_usa`, `alleg40.dll` en `img` zijn **symlinks naar de
NAS**:

```
bowling.log -> /home/deck/mnt/spidernas_emulation/Teknoparrot/Go Go Strike/bowling.log
```

Het spel **opent dat pad om in te schrijven** bij het starten. Binnen de sandbox is dat een overlay --
bewezen, want de schrijfactie komt uit in `native/GoGoStrike/0/upper/` en niet op de NAS. Maar het
betekent wel dat **dezelfde opdracht buiten de sandbox de NAS zou beschrijven**. Dat is precies de
reden dat alle proeven in dit project in `/tmp` en in de sandbox gebeuren, en het is goed dat hier
hardop te hebben: het is geen theoretisch risico maar een pad dat naar de NAS wijst en door het spel
geopend wordt.

#### De keten is rond: de brede shim laat liballeg een NULL-lettertype lezen (2026-10-12)

In de sectie over de crashketen heb ik gezegd dat de preemptie **echt maar onbewezen** was, en dat de
toestand gedeeld zou zijn in plaats van gedupliceerd. **Dat laatste is nu weerlegd, en de schade is
aangetoond.** Alle schakels komen uit de kern van de crash-dump:

**1. Welk argument is NULL.** `textout` op `0x72fb0`:

```
72fb0: push ebx
72fb1: sub  esp,0xc            ; twee duwen, dus de argumenten schuiven 0x10 op
72fb4: call 0x72fb9            ; get_pc_thunk.bx
72fb9: pop  ebx
72fba: add  ebx,0x450ab        ; PIC-basis
72fc0: mov  eax,[esp+0x18]     ; = oorspronkelijk [esp+8] = ARGUMENT 2
72fc4: mov  edx,[esp+0x24]
72fc8: mov  ecx,[eax+0x8]      ; <-- CRASH: f->vtable
...
72fea: call [ecx+0x10]         ; en dan een virtuele aanroep door die vtable
```

Argument 2 van `textout(BITMAP *bmp, const FONT *f, const char *s, int x, int y, int color)` is dus
**het lettertype**, en `textout` is de **oude** functie: hij controleert niet op NULL, hij pakt meteen
`f->vtable`. In de core staat `eax = 0` en `eip = textout+0x18` -- dus `f` was NULL.

**2. Waar die NULL vandaan komt.** De gast **definieert zelf** `font`:

```
080bb870 B font        <- de gast, soort B = BSS
000ab548 D font        <- liballeg, soort D = met beginwaarde
```

En in de core:

| wat | adres | waarde |
|---|---|---|
| gast-kopie van `font` | `0x080bb870` | **`0x00000000`** |
| liballeg's eigen `font` | `0xf7a54548` | `0xf7a5453c` (= `_default_font`, geldig) |
| `_default_font` | `0xf7a5453c` | `0xf7a54524` |
| `font_vtable_mono` | `0xf7a54534` | `0xf7a544c4` |
| gast gemapt? (`0x08048000`) | | `0x464c457f` = `\x7fELF` -- **ja** |

En `font` staat in de eerder gemeten lijst van **71 botsingen** tussen gast en liballeg.

**3. Het mechanisme.** liballeg's PIC-code leest `font` via de GOT (`[ebx+offset]`). Die GOT-invoer is
een `GLOB_DAT`-verplaatsing en wordt bij het laden gevuld met de uitkomst in de **globale scope** -- en
dat is de shim, dus de **gast-kopie op `0x080bb870`, die nul is**. De statische beginwaarde van
liballeg's eigen `font` blijft intussen gewoon staan (`0xf7a5453c`), en dat is precies waarom het van
buiten lijkt of er niets aan de hand is: het object is er twee keer, de beginwaarde is goed, maar de
**verwijzingen** komen uit op de lege kopie.

**Dus: de shim laat liballeg nul lezen waar het `&_default_font` verwacht, Allegro's tekstpad geeft die
nul door aan de oude `textout`, en die loopt meteen op een NULL-deref.** Dat is de crash -- op de thread
die liballeg zelf maakt, in de XWIN-driver die het spel zelf kiest, op precies de plek die de core
aanwees.

**En dit maakt de smalle shim van risicoreductie tot een bewezen noodzakelijke wijziging.** `font` wordt
door liballeg **gedefinieerd**, staat daarom **niet** in de smalle verzameling van 278, en verdwijnt dus
automatisch uit de shim -- waarna liballeg zijn eigen `font` houdt.

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

#### De lader springt naar de gast -- en de gast sterft op een `0xb750xxxx`-slot (2026-10-12)

Na de correctie van de startstack (bouw 19:17) is de fout **verplaatst van de lader naar de gast**.
Dat is de sprong vooruit waar het hele project op wachtte:

```
rg-elfloader: shim geladen: 4607 gast-symbolen in de globale scope
rg-elfloader: geladen; spel ./game krijgt argv[0]=./game
rg-elfloader: spring naar e_entry 0x81150a7 (esp 0xf7cf0e90, argc 1, envc 22, 17 auxv)

Program received signal SIGSEGV
#0  0xb7508f60 in ?? ()      <- geen geheugen; precies in het 0xb750xxxx-bereik
#1  0x080890f9 in ?? ()      <- in het gastbeeld
#2  0x0809f0df in ?? ()      <- in het gastbeeld
#3  0x081150ca in ?? ()      <- vlak na de entry 0x81150a7
#4  0x00000001
```

Alles wat de lader moet doen, doet hij nu: `PT_LOAD` mappen, de relocaties toepassen, de shim laden,
de `DT_NEEDED`-bibliotheken laden, de startstack opbouwen (argc 1, envc 22, 17 auxv) en naar
`e_entry` springen. **De gast loopt dus**, en sterft bij zijn eerste aanroep door een slot dat in het
bestand al gevuld stond met `0xb7508f60` -- een adres uit het proces op de originele kast dat hier
niet bestaat.

**Daarmee is de voorspelling uit de hele analyse empirisch zichtbaar geworden**, en is dit de eerste
concrete taak van fase 3: die voorgevulde sloten moeten naar **werkende** adressen wijzen. Narekenen:
`re/_packed-gdb.sh` op de Deck.

#### Van crashadres naar symboolnaam: `fopen` -- en waarom de stub ongerepareerd blijft

De gast sterft op `0xb7508f60`. Die waarde staat twee keer in Galaga's bestand, en de keten is nu
rond:

```
0xb7508f60
  -> 0x0830d2fc   de GOT-slot van PLT-stub 0x08080f60
  -> 0x086f84b8   de TWEEDE slottabel (0x086f83f8 + 48 x 4)
  -> JMPREL-entry #48, symboolindex 4668
  -> naam: fopen
```

**En dat is een belangrijke nuance.** Symboolindex 4668 ligt **binnen** `nchain` (4719), dus `fopen`
is een **benoemde** import -- geen van de 258 gewiste. De gast sterft dus niet op een naamloos slot,
maar op een **benoemde** import waarvan de GOT-slot in het bestand al gevuld stond met een adres uit
het oorspronkelijke proces. Het voorgevulde-GOT-probleem raakt dus ook de benoemde imports.

**En dan de mechanische verklaring.** De `DT_JMPREL`-relocaties wijzen naar de **tweede** tabel
(`0x086f84b8`), niet naar de GOT-slot van de stub (`0x0830d2fc`). Past een lader die relocaties
netjes toe op hun `r_offset`, dan schrijft hij het opgeloste adres dus in de **tweede tabel** -- en
blijft de PLT-stub door zijn eigen, verouderde slot springen. Dat verklaart precies waarom de gast na
een verder geslaagde laadbeurt toch op `0xb7508f60` sterft.

*(Te verifiëren in de lader: log waar de `R_386_JMP_SLOT`-relocaties werkelijk geschreven worden.
De meting hierboven is statisch; de conclusie over de lader is daaruit afgeleid.)*

**Wat dat betekent voor fase 3.** Voor elke PLT-stub moet het opgeloste adres ook in **de GOT-slot van
die stub zelf** komen. De **naam** is er namelijk wel: via de tweede tabel en de JMPREL-entry. Wat nog
gezocht moet worden is de **koppeling** tussen een tweede-tabel-entry (`0x086f84b8`) en de
bijbehorende eerste PLT-stub (`0x08080f60`). Twee kandidaten om te toetsen:

* de tweede-tabel-entry wijst naar een stub in de **tweede PLT** (`0x08658b10+`), en die tweede PLT
  zou naar de eerste kunnen doorverwijzen;
* de positie in de tweede tabel loopt parallel aan de PLT-stub-volgorde, maar met een andere stap
  (hier: entry 48 in de tweede tabel tegen 188 sloten verder in de eerste GOT).

Als die koppeling te leggen is, krijgt de lader **namen voor de stubs** -- en dat is de vraag waar
fase 3 op vastzit. Narekenen: `re/_slot-keten.py`.

#### Er zijn twee PLT-systemen, en de JMPREL hoort bij het tweede

Een tweede meting (`re/_tweede-tabel.py`) laat zien waaróm het repareren van de JMPREL-relocaties de
stub niet helpt. Galaga Assault, de bytes letterlijk:

```
DT_PLTGOT=0x830d000   DT_JMPREL=0x807f840   PLTRELSZ=2048 (256 entries)

eerste PLT-stub 0x08080f60:   ff 25 fc d2 30 08    jmp *0x0830d2fc     <- ABSOLUUT GOT-slot
                              68 ef be ad de                          <- push 0xdeadbeef (index weg)
                              e9 20 f4 ff ff
tweede PLT      0x08658b10:   ff a3 0c 00 00 00    jmp *0xc(%ebx)      <- GOT via %ebx
tweede PLT-stub 0x08658b16:   68 00 00 00 00       push 0x0
                              e9 e0 ff ff ff
tweede PLT-stub 0x08658b26:   68 08 00 00 00       push 0x8
tweede PLT-stub 0x08658b36:   68 10 00 00 00       push 0x10
tweede PLT-stub 0x08658b56:   68 20 00 00 00       push 0x20
```

En de **tweede slottabel** (`0x086f83f8+`) bevat een mengeling: waarden als `0x08658b16`,
`0x08658b26`, `0x08658b36` (dus stubs in de tweede PLT) **en** verouderde absolute adressen als
`0xb76e2020` en `0xb755b9a0` -- hetzelfde verschijnsel als in de eerste GOT.

**Dus: er zijn twee parallelle PLT/GOT-systemen.**

| | eerste PLT | tweede PLT |
|---|---|---|
| adressering | absoluut (`jmp *0x0830d2fc`) | via `%ebx` (`jmp *0xc(%ebx)`) |
| wat er gepusht wordt | `0xdeadbeef` (index gewist) | `0x0`, `0x8`, `0x10`, `0x20` (een oplopende waarde) |
| hoort bij | de **aanroepen van de game** | de **`DT_JMPREL`-tabel** |

En dat verklaart de crash precies: onze lader repareert de JMPREL-relocaties, en die horen bij het
**tweede** systeem. De stubs die de game werkelijk aanroept zitten in het **eerste**, en hun
GOT-sloten houden de verouderde waarde. De reparatie is dus niet verkeerd, maar ze raakt de verkeerde
tabel.

**Wat dat betekent.** De lader moet de **eerste** PLT repareren. De naam is per stub niet uit JMPREL
te halen, want die tabel hoort bij het tweede systeem. De open vraag is nu: hoe hangt een eerste
PLT-stub aan een symbool? Twee dingen die we al weten en die de zoekrichting bepalen:

* `fopen` zit op **entry #48** van de tweede tabel, terwijl zijn eerste-PLT-stub `0x08080f60` op
  **index 188** van de eerste GOT staat (`(0x0830d2fc - 0x0830d00c) / 4`). Die twee indexen zijn dus
  niet gelijk, en de tweede PLT pusht `8 x k` terwijl de tweede tabel met stappen van 4 loopt.
* De tweede PLT pusht een **oplopende** waarde (`0x0`, `0x8`, `0x10`, `0x20`) die bij de
  opeenvolgende tabel-entries hoort. Als dat een index in een derde tabel is, ligt daar de koppeling.

Narekenen: `re/_tweede-tabel.py`.

#### 31 van de 357 stubs zijn statisch te benoemen (2026-10-12)

Dit is de eerste keer dat er voor de **gepakte** klasse namen uit het bestand zelf komen. De methode
(`re/_naamterugwinning.py`):

1. De **tweede** slottabel heeft **100 niet-lege entries, en alle 100 hebben een naam** -- via de
   symboolindex in de bijbehorende `DT_JMPREL`-entry.
2. Van die 100 hebben er **33** als inhoud een **verouderd absoluut adres** (`0xb7xxxxxx`) in plaats
   van een tweede-PLT-stub. Dat geeft een map **adres -> naam**.
3. De **eerste** GOT-sloten bevatten dezelfde soort adressen. Elke eerste-PLT-stub waarvan de
   slotwaarde in die map voorkomt, krijgt dus een naam.

**Uitkomst: 31 van de 357 stubs krijgen een naam** (325 geen treffer, 1 leeg). Daaronder precies de
stub die de gast nu laat crashen: `k=48`, `n=188`, **`fopen`**. En verder bijvoorbeeld `connect`,
`getpid`, `syscall`, `realloc`, `recv`, `getenv`, `calloc`, `memset`, `malloc`, `free`, `memcpy`,
`fclose`, `fread`, `socket`, `send`, `poll`, `fcntl`, `dlsym`, `time`, `clock`.

**Wat dat praktisch betekent:** een lader kan die 31 sloten **nu** op naam repareren -- en dat is
genoeg om de eerste crash voorbij te komen, want `fopen` zit erbij. Daarna komt de volgende
ongerepareerde slot, en dan is de vraag of die in de 31 zit.

**En wat het niet oplost.** De relatie tussen de JMPREL-entry-index `k` en het PLT-volgnummer `n` is
**niet lineair** (`k - n` loopt van `-14` tot `-120`), dus er is geen indexformule: de koppeling gaat
via het **adres**. Voor de overige ~326 stubs is er geen adres in de tweede tabel om op te matchen, en
die hebben dus nog steeds de geheugendump nodig. De 31 zijn een begin, niet het einde.

Narekenen: `re/_naamterugwinning.py` en `re/_paren.py`.

#### De naamterugwinning per game: een klein maar echt deel (2026-10-12)

De methode die hierboven op Galaga Assault is bewezen, toegepast op alle gepakte games:

| game | stubs | niet-lege JMPREL-entries | met naam | adres -> naam | **stubs met een naam** |
|---|---|---|---|---|---|
| Galaga Assault | 357 | 100 | 100 | 33 | **31 (8,7%)** |
| Pacman Chomp-Mania | 442 | 653 | 546 | 111 | **70 (15,8%)** |
| Jurassic Park | 577 | 101 | 101 | 39 | **33 (5,7%)** |
| Walking Dead | 807 | 106 | 106 | 12 | **11 (1,4%)** |
| Target Terror Gold | **0** | 656 | 192 | 0 | 0 |

**Wat dit wel is:** een echte, meetbare statische route naar namen, die per game tussen 1,4 en 15,8
procent van de stubs dekt -- en in elke game zit de **eerst falende** stub erbij, dus het is genoeg om
verder te komen dan nu.

**Wat dit niet is:** geen oplossing voor de hele klasse. Het overgrote deel van de stubs blijft
naamloos, en de spreiding is groot (Walking Dead 1,4 procent tegen Pacman Chomp-Mania 15,8). De
geheugendump blijft de hoofdweg.

**En een derde, onafhankelijke bevestiging dat Target Terror Gold níet gepakt is:** nul stubs met het
`0xdeadbeef`-patroon. Dat klopt met de twee eerdere metingen (36 sectiekoppen, en `nchain` gelijk aan
het einde van de tabel).

Narekenen: `re/_naamterugwinning-alle.py`.

#### De 82 teruggewonnen namen lossen allemaal op -- de reparatie is uitvoerbaar (2026-10-12)

De methode levert per game **31 / 70 / 33 / 11** paren van GOT-slot naar naam; over de vier gepakte
games zijn dat **82 unieke namen**. En de voorwaarde voor de reparatie is getoetst: in een **32-bits**
proces is **elke** van die 82 namen met `dlsym` op te lossen -- **82 opgelost, 0 niet gevonden**.

Daarmee is het van een mooie vondst een **uitvoerbare reparatie** geworden: de lader kan voor elk
gevonden slot `dlsym(naam)` schrijven, en dat levert een echt adres op in plaats van een adres uit het
proces op de originele kast. Narekenen: `re/_naamtoets.sh`, dat per game ook een
`paren-<game>.txt` wegschrijft met de sloten en namen.

#### De uitgepakte lader is gedumpt -- met een symbooltabel van duizenden namen (2026-10-12)

Wat statisch aantoonbaar onmogelijk was, is nu binnen: een geheugendump van een **lopende**
BudgieLoader.

Route: `scripts/launch-direct.py GalagaAssault --run --env TP_NO_GAMESCOPE=1`, en terwijl de lader
draait `/proc/<pid>/maps` plus `/proc/<pid>/mem`. Bewijs dat het beeld gevuld is: op `0x401000` staat
na 0,0 s

```
55 89 e5 83 e4 f0 83 ec 10 c7 04 24 40 ed 50 00
push ebp ; mov ebp,esp ; and esp,-16 ; sub esp,16 ; mov [esp],0x50ed40
```

Dat is echte x86-code, precies in de secties waar het bestand `SizeOfRawData = 0` heeft. Opgeleverd in
`~/bw/dump-galagaassault/s1-lader/`:

| regio | grootte | inhoud |
|---|---|---|
| `00400000-00401000` | 4 KB | PE-kop |
| `00401000-00bf6000` | **8.343.552 B** | het beeld dat niet in het bestand stond (`rwxp`) |
| `00bf6000-01695000` | **11.137.024 B** | uitvoerbare regio (`r-xp`) |

**En daar zit de naamgeving in.** In de grootste regio staan **16.558** leesbare namen van 4 tot 40
tekens, in de 8 MB-regio **10.157**. De eerste uit een alfabetische reeks: `__cxa_atexit`, `__errno`,
`__getreent`, `__locale_mb_cur_max`, `__main`, `_bilf`, `_ctype_`, `_exit`, `_impure_ptr`, `abort`,
`accept`, `access`, ... En de eigen API staat er ook: `GetElfBase`, `GetElfSymbol`,
`glutBitmapCharacter`, `glutInit` (7x), `SDL_` (11x), `malloc` (16x), `free` (25x), `fopen`,
`connect`, `getpid`.

Dat is de **Cygwin/msys-exporttabel**: de lader is een Cygwin-PE en lost de gast-imports op tegen zijn
eigen runtime. Daarmee is de vraag verschoven van "waar komen de namen vandaan" naar "**hoe koppelt de
lader een naamloos slot aan een naam**".

**De volgende stap is dus:** zoeken naar een **parallelle tabel** in de dump -- een reeks van ongeveer
258 verwijzingen naar die naamstrings -- die de index van een gastslot aan een naam koppelt. Als die
er is, zijn **alle** gewiste namen te herstellen, niet 31.

**Wat nog niet gelukt is:** de dump van de **gast**. Het proces was weg voordat de gast geladen was
(`OPEN /proc/<pid>/mem MISLUKT: [Errno 3] No such process`), dus de GOT van de gast na resolutie
hebben we nog niet. Dat is de volgende meting.

#### De naamgeving zit in de dump, maar zonder verwijzingen ernaartoe (2026-10-12)

Nadat de dump gelukt was, heb ik er read-only in gezocht (`re/_resolutietabel.py`, `re/_rva-tabel.py`).
De uitkomst is zelf een aanwijzing:

* De naamstrings zitten in **twee gebieden**: in de `rwxp`-regio tussen ongeveer `0x55xxxx` en
  `0x59xxxx` (`malloc` op `0x00561da5`, `free` op `0x00561db9`, `memcpy` op `0x00561dc6`, `SDL_Init`
  op `0x00569f27`, `glutInit` op `0x005747c1`, `glGetIntegerv` op `0x005739e2`), en in de `r-xp`-regio
  (`fopen` op `0x013d95f0`, `getpid` op `0x013ad0e2`, `pthread_mutex_lock` op `0x0157ee76`).
* **Er is geen enkele verwijzing naar die strings** -- niet als absoluut adres en niet als RVA
  (`vaddr - 0x400000`). Nul treffers, voor alle tien getoetste namen. De tabel wijst de namen dus
  **niet** aan: ze staan waarschijnlijk **inline** in records van vaste grootte.
* Op `0x0055e000` staat een **tabel met stap `0x40`** waarvan de waarden in het naamgebied wijzen,
  aflopend: `0x00579be0`, `0x00579ba0`, `0x00579b60`, `0x00579b20`, `0x00579ae0`, `0x00579aa0`.

**De volgende stap, en die is precies:** het recordformaat vaststellen (staat er een naam inline op
`0x00579be0`?), de tabel vinden die die records indexeert, en dan de beslissende toets: loopt die index
**parallel aan de symboolindex van de gast**? Voor Galaga is de gast-`dynsym` 4977 entries en zijn de
gewiste namen de indices **4719..4976** -- 258 stuks, aaneengesloten. Als de tabel 258 records heeft
die met die reeks correleren, is dat de resolutietabel en zijn **alle** gewiste namen te herstellen in
plaats van 31.

Die analyse loopt nu bij de subagent die de dump gemaakt heeft; ik heb hem de gebieden, de adressen en
de tabel met stap `0x40` gegeven zodat hij niet opnieuw hoeft te beginnen.

**En de gast is inmiddels wél gevangen.** Een tweede poging leverde vier snapshots op, met in het
logboek de regel die het verschil maakt:

```
[19:31:59] gastbeeld op 0x08048000 na 8.5 s
```

| snapshot | regio's |
|---|---|
| `s1-lader` | 3 |
| `s2-vroeg` | 310 |
| `s3-gast` | 641 |
| `s4-na` | 657 |

Met een manifest van 1620 regels (per regio: bereik, rechten, soort, pad, bestand, fout). De gast is
een ET_EXEC op de vaste basis `0x08048000`, dus de GOT-sloten zijn direct adresseerbaar.

**Daarmee is de beslissende meting nu mogelijk:** vergelijk de GOT-sloten van de gast tussen de
snapshot direct na het laden en de snapshot daarna. **Elke waarde die verschilt is een schrijfactie
van de lader** -- de letterlijke resolutie, en de enige plek waar die informatie bestaat. Die diff
loopt nu bij de subagent, met de vraag of de gevonden schrijfacties overeenkomen met de 31 statisch
benoemde stubs, en vooral **welke extra sloten** de lader heeft gevuld. Dat laatste zou, met de
naamgevingstabel in de dump erbij, de vraag "hoe koppelt ElfLdr2 een naamloos slot aan een naam" met
een meting beantwoorden in plaats van met een vermoeden.

#### Van BudgieLoaders eigen API vraagt geen enkele game iets (2026-10-12)

De eisenmatrix vond eerder geen gemeten consument; dit toetst het over alle acht games met een
`game`-bestand, geteld met `grep -a -o | wc -l` op de spelbestanden:

| game | GetElfBase | GetElfSymbol | GetMainInterface* | TpRefreshMainInterface | get_info | ElfPreReserveCallback | NvOptimus | AmdPowerXpress |
|---|---|---|---|---|---|---|---|---|
| Galaga Assault | 0 | 0 | 0 | 0 | **5** | 0 | 0 | 0 |
| Pacman Chomp-Mania | 0 | 0 | 0 | 0 | **5** | 0 | 0 | 0 |
| Jurassic Park | 0 | 0 | 0 | 0 | **5** | 0 | 0 | 0 |
| Walking Dead | 0 | 0 | 0 | 0 | **2** | 0 | 0 | 0 |
| Target Terror Gold | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Big Buck Hunter Pro | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GhostBusters | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Terminator Salvation | 0 | 0 | 0 | 0 | **2** | 0 | 0 | 0 |

**En die `get_info`-treffers zijn geen aanroepen.** De context in Galaga Assault is
`rawRects hasp_get_info` (1x -- de dongle) en `req c2v_reply get_info` (2x, een fragment van een
langere reeks). En `api` komt **nul keer** voor als zelfstandig woord, terwijl de ruwe substring 60 tot
129 keer voorkomt -- precies wat de eisenmatrix al vaststelde.

**Conclusie:** van BudgieLoaders **eigen** API -- de 6 laderfuncties, de 3 data-exports en de 3
ctype-exports -- vraagt **geen enkele** game iets. Daarmee is de API-oppervlakte van fase 3 meetbaar
**alleen het glut-deel**, en dat zijn de 39 namen die wij nog moeten maken. De twaalf andere zijn voor
de lader intern of voor gereedschap, niet voor de games.

Narekenen: `re/_eigenapi-check2.sh`.

#### De API-oppervlakte is af: 161 van 161 glut-namen (2026-10-12)

`tools/rg-glutapi/librg-glutapi.so` is gebouwd en daarna **onafhankelijk nagerekend** (niet op grond
van de bouwer zijn woord):

| | aantal |
|---|---|
| `glut*`-namen die BudgieLoader exporteert | 161 |
| `glut*`-namen in `librg-glutapi.so` | **161** |
| gedekt | **161** |
| ontbreekt | **0** |

Het bestand is `ELF 32-bit LSB shared object, Intel i386`, 55.836 B. Het laadt met `dlopen`, en de
gevoeligste namen zitten erin: `glutDisplayFuncUcall` (een van de 31 wikkels), `glutStrokeLengthf`
(de float-variant), `glutSetVertexAttribNormal` (een van de zes nieuwe) en `glutBitmapCharacter`.

Daarmee is het **API-deel van fase 3 klaar**: 122 namen komen uit onze eigen 32-bits libglut, 39 zijn
hier gemaakt (30 mechanische wikkels en 6 kleine nieuwe functies), en van BudgieLoaders eigen API
vraagt geen enkele game iets. Narekenen: `re/_glutverify.sh`.

#### Correctie: `DT_PLTRELSZ` staat in bytes, en ik las hem als hex (2026-10-12)

Hierboven staat dat de pakker `PLTRELSZ` zou **verhogen** (256 tegen ~100 echte). **Dat was mijn eigen
fout.** `readelf -d` print die waarde in **decimalen** met "(bytes)" erachter:

```
0x00000002 (PLTRELSZ)   800 (bytes)
```

Mijn parser las `800` als hexadecimaal, dus als 0x800 = 2048, en liep daarna 156 records **voorbij het
einde van de tabel**. Wat daar staat is andere data -- bij Galaga op index 363 bijvoorbeeld `0x0830d008`,
midden in de eerste GOT. 1B heeft het nagerekend (`re/h17_jmprel_einde.out`):

| game | `PLTRELSZ` | in bytes | records | eerste niet-JMP_SLOT-index |
|---|---|---|---|---|
| Galaga Assault | `0x320` | 800 | **100** | 100 |
| Jurassic Park | `0x328` | 808 | **101** | 101 |
| Pacman Chomp-Mania | `0x618` | 1560 | **195** | 195 |

In alle drie is `PLTRELSZ / 8` **exact** het aantal records met een geldige `r_offset`, `r_info` en
naam. Er is dus geen opgeblazen tabel; er is een verkeerd gelezen getal. De uitkomst van de
naamterugwinning (31/70/33/11 stubs) blijft staan -- die telde alleen records met een geldige
slotwaarde, en de rommel daarvoor had `r_offset = 0` en index 0.

**Les, en dezelfde als eerder in dit plan:** twee tabelgroottes in deze bestanden zijn verraderlijk
(`nchain` is met opzet verlaagd), en ik heb er zelf een derde verzonnen door een decimaal als hex te
lezen. Wie hier iets narekent: kijk naar het **formaat** dat `readelf` print, niet alleen naar het getal.

#### De kernuitkomst van fase 1B: ElfLdr2 lost de gewiste namen NIET op (2026-10-12)

Het rapport staat in `docs/research/importresolutie-elfldr2.md` (32.146 B). Dit is het antwoord op de
vraag waar fase 1 op gericht was, en het is een **negatief** antwoord met harde cijfers:

* **De naamloze records zijn volledig genuld** -- letterlijk `(0,0,0,0,0,0)`, ook `st_value` en
  `st_shndx`. De boekhouding sluit exact: (UND met naam - 3) + staart = **357** en **577**, precies het
  aantal stubs. Voor Galaga 258 staartrecords, JP 472, Pacman 248.
* **`.dynstr` geeft ze niet terug:** ongerefereerd zijn 44/51/49 strings, uitsluitend versienamen,
  bibliotheeknamen en de HASPE-markers. Geen enkele importnaam.
* **`hints.dat` is niet de sleutel.** De beslissende kruistoets met 1B's eigen PLT-scanner (die
  357/577/442 exact reproduceert): stubadres ∩ hint-waarden = **0 van 357, 0 van 577, 0 van 442**;
  GOT-slot ∩ waarden = 0/0/3; stubadres ∩ sleutels = 0. De hintwaarden zijn niet uitgelijnd (7,9%
  op een 16-byte grens) en wijzen naar `.rodata`/DWARF, dus niet naar code.
* **En de `0xb7`-GOT is een GOT-dump van één run.** JP `game_original` heeft **577 van 577** sloten op
  `stub+6` (luie PLT) en **nul** `0xb7`-waarden; JP `game` heeft 576 `0xb7`-waarden. De
  onherleidbaarheid komt door **mmap-ASLR**: de doorsnede van JP en Galaga is **0** van 355/571.

**Wat ElfLdr2 dus doet met de 258/472/248 verborgen imports: niets.** Het laat ze staan, en `rt-glfix`'s
levende meting sluit daar naadloos op aan: 570 van 576 GOT-sloten wijzen naar **anonieme
`r-xp`-trampolines**, en 180 keer is dat dezelfde cdecl-no-op `55 8b ec 33 c0 5d c3` die 0 teruggeeft.
De lader die we vervangen **benoemt die imports dus ook niet** -- hij vangt ze af met een lege stub.

**Wat onze eigen lader daarom moet doen:**

1. De **99/105/195** door relocaties gedekte imports gewoon oplossen via `dynsym` + `DT_JMPREL`
   (`PLTRELSZ` in **bytes**, en het aantal **niet-nul** entries als waarheid).
2. De **258/472/248** verborgen imports: de naam is onherstelbaar, dus zet er een **cdecl-trampoline**
   in de GOT-slot -- precies zoals ElfLdr2. `rt-glfix` MODE=8 kan als teller dienen om te zien welke
   van de ~436 sloten werkelijk worden aangeroepen.
3. **GL: benoem de 93 sloten niet, maar onderschep de naamroute.** De game draagt zijn eigen
   naamruimte: Jurassic Park bevat **1.347 `gl*`-naamstrings** (41% van de GL-naamruimte van 2.950; de
   GLEW-tabel), Galaga 132, Pacman 58 -- plus `dlopen`/`dlsym`. Dat verklaart waarom 22 van de 93 in de
   praktijk toch een echte functie kregen.
4. Valkuilen: Wine's `opengl32` exporteert GL als **stdcall** terwijl de games **cdecl** aanroepen, en
   `gs:[0x14]` moet een stabiele stack-guard hebben.

**En de libGL-groep is geen gedeelde basis.** `0xb7500000` is JP's eigen minimum (Galaga heeft 22
waarden in dat venster op andere offsets, Pacman **nul**). De beste overlap tussen Galaga en JP over
alle verschuivingen 0..0x100000 is **1 van 22**. Tegen de echte 32-bits libGL die er staat: 0 van 93
direct, en de offsetverschillen scoren 331/709 = 46,7% tegenover **69,8% voor willekeurige sets** in
hetzelfde raster (null-model) -- dus ruis. Mijn eerdere "0 van 93" blijft staan, maar nu met een
null-model eronder in plaats van een enkele toets.

#### De resolutietabel is gemeten: 100 slot -> naam -> doel (2026-10-12)

De dump-subagent heeft de tabel waar dit project naar zocht -- niet als vermoeden maar als meting:
`~/bw/resolutietabel-elfldr2.tsv`, **100 rijen** uit de dump van de lopende lader (snapshots
`s3-gast`/`s4-na`). Kolommen: gastslot, symboolindex, naam uit de JMPREL-verplaatsing, oud (in het
bestand), nieuw (in de dump), doel.

En dat **doel** is leesbaar:

```
0x086f83f8  4621  fileno            0x08658b16  0x61101eca  msys-2.0.dll:_fileno
0x086f83fc  4622  __errno_location  0x08658b26  0x0041ee50  lader-intern 0x0041ee50
0x086f8400  4623  open              0x08658b36  0x0041c310  lader-intern 0x0041c310
0x086f8404  4624  connect           0xb76e2020  0x0041ffa0  lader-intern 0x0041ffa0
0x086f8428  4633  syscall           0xb758c140  0x02fa0488  trampoline->0x800c6f98 (syscall)
0x086f842c  4634  realloc           0xb751be90  0x61102ece  msys-2.0.dll:_realloc
0x086f844c  4642  memset            0xb75d1210  0x6118a170  msys-2.0.dll:_memset
```

**Drie dingen die dit vastlegt:**

1. ElfLdr2 lost een benoemde import naar **twee** plekken op: een export van zijn eigen Cygwin-runtime
   (`msys-2.0.dll:_fileno`, `_realloc`, `_memset`, `_getpid`, ...) of **zijn eigen interne
   implementatie** (`lader-intern 0x0041ee50` -- en dat geldt ook voor `open`, `connect`, `recv`,
   `getenv`, `exit` en `dladdr`). De lader is dus geen dun schilmpje: hij implementeert een deel van
   libc/POSIX zelf. Eén entry is een echte trampoline (`syscall`).
2. De tabel dekt **exact de 100 `DT_JMPREL`-entries** -- dus de benoemde helft. Over de 258 gewiste
   namen zegt hij niets.
3. En voor die 258 doet ElfLdr2 **niets**, wat in de sectie hierboven met harde cijfers staat: volledig
   genulde records, `hints.dat` bevat geen enkel stubadres (0 van 357), en de `0xb7`-GOT is een dump
   van één run.

**Daarmee is het beeld compleet voor de benoemde helft.** De open vraag die overblijft is of de lader
het opgeloste adres ook in de **GOT-slot van de eerste PLT-stub** schrijft -- dat is de stub die de
game werkelijk aanroept, en die vraag is met dezelfde dump te beantwoorden door de sloten `0x0830d00c+`
tussen `s3-gast` en `s4-na` te vergelijken.

#### De dump beantwoordt de laatste vragen -- en zet mijn 31 namen in een ander licht (2026-10-12)

Het rapport staat in `docs/research/budgieloader-dump.md`. De uitkomsten, in volgorde van gewicht.

**1. De lader raakt de gast na het laden niet meer aan.** Snapshot `s3-gast` tegen `s4-na`: **0 bytes
verschil**. En het gedumpte gastbeeld tegen het **bestand**: **410 van 7.278.592 bytes**, 103 woorden op
twee pagina's. Toen `PT_DYNAMIC` werd uitgelezen: `DT_JMPREL=0x0807f840`, `DT_PLTRELSZ=0x320` ->
**100 `R_386_JMP_SLOT`-verplaatsingen, en alle 100 hebben een bewaarde naam, nul naamloos**. Precies die
100 `r_offsets` zijn de 100 woorden die de lader vulde: **100 gevuld met naam, 0 gevuld zonder naam, 0
met naam maar niet gevuld.** Controle op de eerste regel: het bestand zegt `0x086f83f8 -> fileno`, en de
lader schreef `0x61101eca` = `msys-2.0.dll`'s `_fileno`.

**2. En mijn 31 paren hebben NUL overlap met wat de lader doet.** Die staan op `0x0830d0xx` -- de
**eerste** GOT -- en zijn in bestand én dump **identiek**: `connect` blijft `0xb76e2020`, `getpid`
blijft `0xb755b9a0`. **ElfLdr2 repareert die verouderde sloten dus niet.** Mijn naamterugwinning
beschrijft daarmee **dode data**: de methode werkt, maar ze reconstrueert iets wat de lader zelf laat
liggen. Dat is een belangrijke nuance op de sectie hierboven.

**En dat is precies de crash die we zagen.** Onze lader springt naar de gast, de gast roept door een
eerste-PLT-stub, en die springt via zijn verouderde `0xb7`-slot in ongemapte ruimte. ElfLdr2 heeft
datzelfde probleem -- en Galaga crasht onder de gesloten lader dan ook (`BudgieLoader.exe.stackdump`
staat in de view). **De eerste GOT repareren is dus iets wat onze lader beter kan doen dan de lader die
we vervangen.** Dat is precies waar dit project om begon.

**3. De 161 glut-implementaties staan er, en het is echte code.** De export-datadir staat op RVA
`0x1090058`, dus in `.brr4` en niet in `.edata` (daarom waren de 173 namen statisch leesbaar). De 161
namen plus 31 `Ucall` staan op RVA `0x308f0`-`0x3e9d0` = `0x4308f0`-`0x43e9d0`, en **134 van de 161
beginnen met een klassieke proloog**; `glutIdleFunc@0x430930` is `55 89 e5 53 83 ec 14`. Daarmee is het
"nagebootste libs"-deel van fase 3 geen aanname meer maar een meting.

**4. En de naam/aderstabel die ik zocht bestaat wél -- op `0x80000000`.** Regio
`0x80000000-0x80110000` (1.114.112 B, `rwxp`, in `s3` en `s4` identiek): **15.742 leesbare reeksen**,
waaronder 902 `_Z`-namen, 527 `SDL_`-namen en 39 `glut`-namen, plus `malloc`, `fopen`, `XFlush`,
`syscall`, `__libc_current_sigrtmin` en `getutent_r`. Er staat een **ELF32-kop op `0x8008877c`**, en 60
van de eerste 60 slotadressen komen erin voor. Voor 37 van de 103 opgeloste adressen staat de naam er
direct bij, en die naam is **exact de JMPREL-naam**. De 5 trampolines pushen een verwijzing naar een
record in die regio en roepen de resolver op `0x004148b0` aan. **Wat nog open is: de exacte
recordstructuur van die regio.**

**5. Twee correcties op mijn eigen aanwijzingen.** De tabel op `0x0055e000` (stap `0x40`) is
**bitmap-fontdata van freeglut** -- 16-bits glyphrijen -- geen resolutietabel; die aanwijzing vervalt.
En mijn conclusie "de lader repareert de verkeerde tabel" klopt in de mechaniek maar niet in de
bedoeling: het is niet de verkeerde tabel, het is de **eerste GOT die de lader bewust ongemoeid laat**.

**6. De laatste vraag is nu beantwoord: er is geen resolutietabel voor de 258 naamloze sloten.** Niet
omdat we hem niet vinden, maar omdat hij **nooit gemaakt wordt**. De 258 zijn geen verplaatsingen; ze
staan wel in de tweede slottabel maar er hoort geen `DT_JMPREL`-invoer bij, en de lader schrijft er
niets in. Wie die namen wil moet bij de **pakker** of bij de **originele bibliotheekversie** zijn, niet
bij de loper. Onze lader hoeft ze dus ook niet te achterhalen: een cdecl-trampoline volstaat.

**7. En de valkuilen voor wie dit herhaalt** (uit §4 van het rapport): `ptrace_scope=1` is geen probleem
zolang het dumpende script de **ouder** van de lader is -- een losse `gdb -p` krijgt `EPERM`,
`/proc/<pid>/mem` als nakomeling werkt. En **systemd-logind doodt bij het sluiten van de ssh-sessie de
hele session-scope**, ook `setsid`/`nohup`-processen (`session-1643.scope: Killing process ...
BudgieLoader.exe with signal SIGTERM`). Dat kostte twee runs; draai de meting als systemd-user-unit
(`re/_dump-start2.sh`). Zonder `DISPLAY` werkt het prima.

#### Geen regelmaat in de koppeling: de volgorde helpt niet (2026-10-12)

Getoetst met `re/_regelmaat.py` op de 31 bekende paren: loopt de eerste-GOT-index mee met de
symboolindex? Antwoord: **nee, niet bruikbaar.** Binnen een paar opeenvolgende symbolen is het verschil
constant -- `n - symidx = -4569` voor `recv`, `getenv` en `calloc` -- maar de sprongen ertussen lopen
uiteen:

```
connect -> getpid    n+2   sym+2
getpid  -> syscall   n+27  sym+7
syscall -> realloc   n+7   sym+1
realloc -> recv      n+13  sym+1
recv    -> getenv    n+1   sym+1
getenv  -> calloc    n+2   sym+2
calloc  -> memset    n+22  sym+4
memset  -> gettimeofday  n+39  sym+9
```

Die sprongen zijn de aantallen **niet-benoemde stubs** die er telkens tussen zitten, en die zijn niet
uit de symbolen af te leiden. Twee paren vallen op: `free` (symboolindex **2555**) en `malloc` (**829**),
ver buiten de band 4621-4718 waar de andere benoemde imports zitten. Die twee komen uit een adres-match
met een ander symbool, dus hun koppeling is minder zeker dan de andere 29.

**Wat dat betekent.** De eerste GOT is per game voor ongeveer **31 van de 357** sloten op naam te
repareren. Dat is genoeg om de eerste crash voorbij te komen -- bij Galaga is dat `fopen` -- maar niet
genoeg om de hele tabel te repareren. Voor de rest geldt hetzelfde als voor ElfLdr2: een trampoline.

**En de vraag die dat kan verkleinen: welke sloten worden werkelijk aangeroepen?** `rt-glfix` heeft
daar een teller voor (MODE=8). Als een game maar een deel van zijn 357 sloten aanraakt, is de reparatie
die wij nodig hebben veel kleiner dan 357 -- en dan dekt de 31 misschien een groot deel van het echte
gebruik. Dat is op dit moment de meting die het meest oplevert.

#### Hoeveel sloten worden werkelijk gebruikt? Allemaal -- en mijn meting daarvoor was eerst fout (2026-10-12)

De vraag was of de reparatie kleiner kan: als een game maar een deel van zijn 357 stubs aanraakt, is
31 repareren misschien genoeg. **Het antwoord is nee, en mijn eerste meting daarvan deugde niet.**

**Mijn fout.** `readelf -l` print de rechten bij deze bestanden als **één token**, `RWE`, en niet als
`R E`. Mijn controle op `d[7] == "E"` matchte daardoor niets, en de scan liep over **nul bytes**. De
uitkomst "0 van 357 stubs wordt aangeroepen" was dus geen bevinding maar een lege lus. Ik heb het
gemerkt doordat dezelfde scan ook "0 van 357 GOT-sloten gebruikt" teruggaf -- dat kon niet kloppen.
Dit is de derde meetfout van mijn hand in dit project, en de les is dezelfde als bij `PLTRELSZ`: laat
een scan **melden dat hij iets gescand heeft**, anders leest een leeg resultaat als een uitkomst.

**Correct gemeten** (beide segmenten zijn `RWE`, dus alles is gescand, 7.278.592 bytes):

| meting | uitkomst |
|---|---|
| stubs met een `e8`/`e9`-verwijzing | **356 van 357** |
| totale aantal verwijzingen naar stubs | **4.241** |
| GOT-sloten waarvan het adres in de code voorkomt | 357 van 357 (714 verwijzingen, waarvan 357 de zelfverwijzing in de stub is) |
| van de 31 benoemde sloten worden gebruikt | **31 van 31** |

**En dat betekent dat er geen kleine deelverzameling is om te repareren.** Het spel roept vrijwel al
zijn 357 stubs aan. De reparatie is dus echt 357 sloten breed, en niet 31.

**En dan de eerlijke ondergrens van deze klasse.** Van die 357 imports hebben er **99 een naam** en
**258 niet** (1B's boekhouding: (UND met naam - 3) + staart = 357). Alleen die 99 zijn ooit op te
lossen -- door ElfLdr2, en door ons. Voor de andere 258 is de naam in het bestand **vernietigd** en
bestaat er **nergens** een tabel: niet in `hints.dat`, niet in `.dynstr`, niet in het geheugen van de
lopende lader. Daar komt een no-op-trampoline, precies zoals ElfLdr2 doet.

**Dus:** geen enkele lader kan deze games volledig repareren. Wat wél kan, en wat onze lader beter doet
dan de lader die we vervangen:

* de **99 benoemde** imports echt oplossen (dat doet ElfLdr2 ook);
* daar bovenop de **31 eerste-GOT-sloten** repareren die we via adres-matching kunnen benoemen -- en
  `fopen` zit daarbij, precies de slot waar onze gast nu op sterft (dat doet ElfLdr2 **niet**);
* voor de 258 verborgen imports een trampoline, net als ElfLdr2.

De winst is dus reëel maar begrensd, en ze zit in de eerste-GOT-reparatie. Belangrijker nog: het maakt
onze lader **onafhankelijk**, en dat was de bedoeling.

#### De nieuwe crash is een NAAMLOZE import -- en dat maakt hem onoplosbaar (2026-10-12)

Onze lader doet nu wat ontworpen is. Claude's commit `8b35c25` meldt bij het laden:

```
rg-elfloader: eerste-PLT: 31 op naam hersteld, 325 no-op-trampoline, 0 naam-niet-gevonden (van 33 stale-namen)
```

En de crash is **verplaatst**: van `0xb7508f60` -- een sprong in ongemapte ruimte -- naar
**`0x08089212`, binnen het gastbeeld**. De reparatie werkt dus.

**En de nieuwe crash is precies te verklaren.** In de 0x300 bytes vóór de crash staan negen aanroepen
naar PLT-stubs. Vier daarvan zijn **benoemd** en dus gerepareerd (`0x08080c60`, `0x08080f60`,
`0x08080e50`, `0x080812d0`); vijf zijn **naamloos**. De aanroep die het fataal maakt is de laatste:

```
0x08089207  call -> stub 0x08080750 (slot 0x0830d0f8)   NAAMLOOS
0x0808920c  mov [ebp-0x4c], eax      ; de trampoline gaf 0 terug
0x0808920f  mov eax, [ebp-0x4c]
0x08089212  mov esi, [eax]           ; NULL-deref, eax = 0
```

**Dus: de gast roept een import aan waarvan de naam in het bestand vernietigd is, onze trampoline geeft
0 terug, en de gast derefereert dat meteen.** Dat is geen fout in onze reparatie maar het gevolg van de
258 gewiste namen. ElfLdr2 doet hier precies hetzelfde -- en daarom crashen deze games ook onder de
gesloten lader.

**En daarmee ligt de ondergrens van de gepakte klasse met een meting vast.** Van de 357 imports hebben
er **99 een naam**; die lossen wij op, plus de **31** eerste-GOT-sloten die ElfLdr2 laat liggen. Maar de
game **heeft** de andere 258 nodig: hij roept ze aan, en hun naam bestaat niet meer. **Geen enkele
lader kan dat oplossen**, want de informatie is vernietigd. Wie deze games werkend wil hebben moet bij de
**originele, onbewerkte bestanden** zijn -- niet bij een lader.

**Eerlijke eindstand van fase 3b:** onze lader doet aantoonbaar meer dan ElfLdr2 (de 31 eerste-GOT-
sloten, waaronder `fopen`), en het plafond ligt bij de pakker. Dat is geen mislukking van dit project
maar de grens van het materiaal.

#### Onze API-bibliotheek is gebouwd maar nergens aangesloten (en dat is nu onschadelijk) (2026-10-12)

`librg-glutapi.so` dekt 161 van 161 glut-namen en is nagerekend. Maar **hij wordt nergens geladen.**
Gemeten:

* `~/.local/share/ravengun/runtime/lib32/` bevat alleen de **echte** `libglut.so.3` (261.140 B, van
  10 oktober) -- er is geen `librg-glutapi.so`.
* Niets in de repo of in de profielen verwijst naar onze bibliotheek of installeert hem; alleen zijn
  eigen `README.md` noemt de handmatige stap (`cp librg-glutapi.so <runtime>/libglut.so.3`).
* Beide hebben **SONAME `libglut.so.3`**, dus wie van de twee er staat, wint de resolutie.

**En dat is vandaag onschadelijk, met een meting eronder.** De echte libglut exporteert **171**
glut-namen en dekt alle **16** namen die een game aantoonbaar vraagt. De 39 die wij erbij bouwden zijn
vooral Windows-specifieke `*Ucall`-varianten, en die vraagt **geen enkele** game. En van de drie games
die `libglut.so.3` in hun `DT_NEEDED` hebben -- **Terminator (15 glut-naamstrings), GhostBusters (1) en
Jurassic Park (2)**; Galaga en Halo niet -- zit er geen enkele naam bij die de echte libglut mist.

**Dus:** de waarde van onze bibliotheek is op dit moment **onafhankelijkheid** (niet leunen op de
freeglut van de host) en volledigheid voor de niet-standaard namen, niet het wegnemen van een zichtbaar
gebrek. Wie hem wil aansluiten doet dat met één kopie, en houdt de echte als `libglut.so.3.real` -- want
122 van de 161 namen worden juist naar de echte doorverwezen. Dat is werk in de lader-repo.

Narekenen: `re/_glutketen.sh`.

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

## Stand (bijgewerkt 2026-10-12, ronde 25)

| fase | stand |
|---|---|
| **1 — de huidige lader uitpakken en begrijpen** | **af.** Statisch uitgepakt (`docs/research/budgieloader-uitgepakt.md`, 785 regels), en daarna ook uit het geheugen gedumpt: 8,3 MB beeld met echte x86-code en duizenden namen erin. |
| **1B — hoe ElfLdr2 de gewiste namen oplost** | **deels beantwoord.** `hints.dat` geeft geen namen (13 sleutels; 1.463 hash-toetsen, 0 treffers) en de `DT_JMPREL`-resten zijn nullen. De naamgeving zit in de gedumpte lader; **hoe** een naamloos slot aan een naam gekoppeld wordt is nog open. |
| **2 — de eigen lader op de ongepakte klasse** | **gehaald.** GoGoStrike start via onze eigen route, zonder Wine en zonder BudgieLoader, draait 30 s en toont zijn eigen assert (`docs/screenshots/gogostrike-native-assert.png`). De lader springt naar de gast met argc 1, envc 22, 17 auxv. |
| **3a — de API-oppervlakte** | **af.** `tools/rg-glutapi/librg-glutapi.so` exporteert **161 van de 161** `glut*`-namen (0 ontbrekend), laadt met `dlopen`, en is onafhankelijk nagerekend. 122 kwamen uit onze eigen libglut, 39 zijn gemaakt. |
| **3b — de gepakte klasse** | **deels.** Onze lader laadt de gast, lost de bibliotheken op en springt naar de entry; de gast sterft op het eerste voorgevulde GOT-slot (`fopen`). **31 van de 357** stubs zijn statisch te benoemen, en alle **82** teruggewonnen namen over vier games lossen op. De rest wacht op de dump-analyse en op de gast-dump. |
| **nog uit te rollen** | `play-native.sh` heeft nog `--unshare-ipc` en geen `--bind-rw`, en het profiel laadt de OSS-stub niet -- de drie bewezen oplossingen van fase 2 zitten dus nog niet in de echte launcher. |
