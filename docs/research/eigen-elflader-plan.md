# Eigen ELF-lader: het plan (drie fasen)

*Opgesteld in de coördinatieronde op 2026-10-12. Dit is het plan en het overzicht, **niet** de
meting: de getallen staan in de documenten die eronder genoemd staan. Elke fase noemt wie hem doet
en welk artefact eruit komt.*

## Waarom dit project

`ElfLdr2/BudgieLoader.exe` is de laatste gesloten **lader** in de keten. Zeven games in de
verzameling hangen eraan: Halo, JurassicPark, Terminator, GhostBusters, GoGoStrike, GalagaAssault en
PacmanChomp. **Sinds 2026-10-12 zijn daar negen ElfLdr2-games bij gekomen** toen de eigenaar een batch games toevoegde (zie `patches/NAS-NIEUW.md`), waarvan er twee **gepakt** zijn: Target Terror Gold en Walking Dead. Wat er aan gemeten is (`patches/ELF-NATIVE.md`):

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

### Fase 3 — de gepakte klasse en de API-oppervlakte

Pas te beginnen als 1B antwoord geeft op de naamresolutie. Twee delen:

1. **De gepakte (HASPE-)games**: GalagaAssault, JurassicPark, PacmanChomp en Halo -- en sinds 2026-10-12 ook Target Terror Gold en Walking Dead. Deze bestanden
   hebben nul sectiekoppen, PLT-stubs van de vorm `ff 25 <got> 68 ef be ad de e9 <plt0>`, een GOT die
   in het bestand al gevuld is met `0xb7xxxxxx`-adressen uit het oorspronkelijke proces, en 72–82
   procent gewiste importnamen. Zonder de slot-naam-tabel is dit niet te doen; met de tabel wel.
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
