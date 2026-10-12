# Jurassic Park — crash-diagnose (debugger-ronde 2026-10-12)

Dit bestand hoort bij `docs/research/jurassicpark-stand.md` (r.35 verwees er al naar). Het legt de
**bevestigde diagnose** van de tweede crash vast, uit de debugger-ronde met de eigenaar erbij
(commit `2bb5bf3`). Schone bench, Desktop Mode, één game tegelijk.

Overal onderscheid tussen **gemeten** (uit een echte run/trace/maps) en **afgeleid** (gevolgtrekking
daaruit). Geen serienummers of eigenaar-identificerende gegevens — die komen hier niet voor.

## De crash (gemeten)

Met `WINEDEBUG=+seh` op een schone run:

```
EXCEPTION_ACCESS_VIOLATION (c0000005)  addr=819C524B
  info[0]=00000001   (1 = schrijf-toegang)
  info[1]=00000051   (faultadres = 0x51, dus bijna-nul)
  eip=819c524b  esp=01a9c088  ebp=00000000  eflags=00010296
  eax=ffffffff  ebx=00001f02  ecx=79f0f76c  edx=00000000
  esi=00000000  edi=819c5254
```

- Het is een **schrijf naar een bijna-nul adres** (`0x51`): een schrijf door een basiszpointer 0 + offset 0x51.
- **`eip=0x819c524b` ligt in een gereserveerd blok zonder rechten.** De maps van het levende proces (vlak
  vóór de crash) tonen dat adres in:

  ```
  80080000-98000000 ---p 00000000 00:00 0
  ```

  Dat is Wine's grote **gereserveerde adresblok, `---p` (geen r/w/x)**. Code kán daar niet uitgevoerd worden.
- **`ebp=0`** erbij: er is geen geldig stackframe opgezet.

**Afgeleid:** `ebp=0` + een `eip` middenin een niet-uitvoerbare reservering = een **wilde indirecte sprong/call
door een kapotte functiewijzer**. Dit is geen logische bug in gemapte game-code; de game heeft de controle
doorgegeven aan een adres dat nergens op slaat.

## Wie het opvangt (gemeten)

De SEH-handler die de exception afhandelt zit op **`0x61023800`**. De maps plaatsen dat in:

```
61000000-61001000 r-xp ... /…/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/msys-2.0.dll
61001000-611f8000 r-xp 00000000 00:00 0
```

Dat is **`msys-2.0.dll`**, de MSYS2-runtime van BudgieLoader (de klassieke `0x61000000`-basis). Die handler
unwindt (`RtlUnwind`, handler keert terug met 1) en daarna **stopt de lader met exitcode 255** (gemeten in de
uitvoer van `launch-direct`).

## Waarom de eigen fouthandler zweeg (gemeten + afgeleid)

De brug kreeg deze ronde een opt-in Linux-fouthandler (`RAVENGUN_GLFIX_FAULT=1`, commit `2bb5bf3`). Die **vuurde
niet** bij de crash. Reden: **Wine bezit `SIGSEGV`** — Wine's eigen signaalhandler vangt de Linux-fout en vertaalt
hem naar een Windows-exception (`c0000005`), die vervolgens door de SEH-keten van het spel loopt. Een
Linux-niveau handler (ook de onze) wordt daardoor omzeild. Wine's `+seh`-trace levert in plaats daarvan de
registers hierboven.

- **`winedbg` werkt wél** als debugger: die gebruikt Wine's eigen debug-API, niet Linux-ptrace. (Deze build kent
  overigens géén `--auto`; een gesymboliseerde backtrace vraagt de interactieve modus.)
- **Een kale `gdb`-attach gaf `EPERM`**, ook nadat de brug `prctl(PR_SET_PTRACER_ANY)` had gezet, met
  `ptrace_scope=1`. Ptrace is hier dus niet de weg; winedbg wel.

## Wat de rt-glfix-brug wél dekt (gemeten)

- `librt-glfix.so` is gemapt op **`0xf7f3f000`** in het 32-bit spelproces — de brug laadt dus wél in het
  spelproces (de regel `… ELFCLASS32; ignored` is alleen de 64-bit launcher die klaagt).
- De brug herleidt **~140 GL-GOT-sloten** naar echte `opengl32`-exports (te zien in `rt-glfix.log`).
- Het proces **leeft ~112 s met 24 threads** (een draaiende engine), tegenover **~14 s** zonder de brug. De
  GL/cdecl-fix werkt dus; de crash zit ná de GL-fase.

## Wat de brug niet dekt (afgeleid, met gemeten onderbouwing)

De brug raakt alleen `gl*`-sloten aan. **Niet-GL-sloten** die ElfLdr2 niet oplost blijven op zijn nagebootste
stub staan. Onderbouwing uit `docs/rt-glfix.md` §7 (gemeten op JP): van de 576 GOT-invoeren in de streek wijzen
er **180 naar één stub-adres `0x71bb3e80`** in het nagebootste gebied, en van de 93 GL-invoeren zet ElfLdr2 er
maar 22 om naar een echte export. Roept de game zo'n ongevuld niet-GL-slot aan, dan springt hij de reservering in
→ exact de crash hierboven.

**Waarom een no-op-stub het niet oplost (afgeleid):** als je die ongevulde sloten vervangt door een lege
no-op, springt de game niet meer wild, maar de functie die hij nodig had doet dan niets — de crash verplaatst
zich naar het eerste moment waarop het ontbrekende resultaat telt. De game heeft die functies **echt** nodig; een
no-op maakt hem niet werkend.

## De grens (beslissing eigenaar)

Deze fout verdwijnt pas als de niet-GL-sloten naar werkende functies wijzen. De slot→**naam**-tabel staat niet in
het bestand (105 met naam, 472 gewist). Belangrijke nuance, read-only nagerekend in de coördinatie-ronde (zie de
reconciliatie hieronder en `patches/ELF-LADER-SPEC.md`): de GOT-**waarden** stáán wél in het bestand en zijn echte
functieadressen — maar met de bibliotheken die wij hebben niet naar namen te herleiden. Dat vraagt data die we niet
hebben (de originele 2015-bibliotheken of de proceskaart van de originele kast), en dat valt **buiten scope**
(`docs/decisions.md`, 2026-10-11). Daarom **houdt JP BudgieLoader** en blijft hij geparkeerd — nu met een precieze,
bevestigde diagnose in plaats van een vage.

## Twee opties als de eigenaar tóch verder wil (bench + tv)

1. **Interactieve `winedbg`** voor een gesymboliseerde backtrace van `0x819c524b` (welke call-keten leidt
   ernaartoe). Vraagt een schone bench en de tv.
2. **`RAVENGUN_GLFIX_MODE=15` (MODE_STUBLOG)**: logt wélke onopgeloste stub geraakt wordt vóór de sprong —
   puur ter identificatie (één ontbrekende import vs. een brede leegte). Verandert de conclusie niet en begint
   géén uitpakwerk.

## Identificatie-meting A2 (2026-10-12, schone bench, `RAVENGUN_GLFIX_MODE=15`)

Doel (eigenaar): puur bepalen of dit **één ontbrekende import** is (dan loont een gerichte fix) of een **brede
leegte** (dan blijft het uitpakwerk en blijft JP geparkeerd). Géén uitpakken, géén tabel-reconstructie. Eén run,
schone bench; bij mislukking stoppen zonder tweede poging.

**Gemeten (deze run):**
- Eenmalige `diagnose` (eerste patch-ronde): **576 GOT-invoeren buiten de GL-module; 0 daarvan uitvoerbaar; 0
  met de lege-stub-handtekening** (`55 8b ec 33 c0 5d c3`), met 408 uitvoerbare gebieden in de cache.
- De GL-fix werkte normaal (28 GL-invoeren per ronde omgezet).
- De hele run (~130 s, crash zoals eerder): **0 lege stubs geïnstalleerd en 0 aangeroepen** — de brug logde
  onafgebroken `stubtellers: nog geen enkele lege stub aangeroepen`.

**Gemeten (statisch, uit de patches-repo; aangeleverd vanuit de coördinatie-ronde):** JP heeft **577 PLT-stubs, waarvan 105
met naam en 472 gewist**.

**Afgeleid (bijgesteld — zie de reconciliatie hieronder):**
- De runtime-stublogger **ving niets**, maar dat is een **meetgrens, geen uitkomst**: hij kan alleen een
  *leesbare* no-op-stub omleggen, en een slot dat naar niet-gemapte `0xb7`-ruimte wijst is niet uit te lezen of te
  instrumenteren. "0 aangeroepen" betekent dus **niet** "er zijn geen lege sloten"; het betekent "mijn methode kon
  ze niet uitlezen". (Mogelijk ook timing: ElfLdr2 mapt zijn nagebootste libs later dan het de GOT vult, en de
  exec-cache bevriest vroeg.) Per opdracht géén tweede poging geforceerd.
- Wat de meting **wél** bevestigt: de 576 niet-GL-invoeren hielden runtime geen uitvoerbaar doel — passend bij
  danglende `0xb7`-adressen uit het oorspronkelijke proces. Mijn eerdere formulering ("brede leegte, nergens naar
  een bruikbaar doel") klopt **voor runtime**, maar was te absoluut: de waarden zijn **geen rommel** (zie
  hieronder). De uitkomst (JP geparkeerd) blijft; de **reden** is bijgesteld, zonder een meting om te draaien — de
  ruwe A2-getallen hierboven staan ongewijzigd.

## Reconciliatie met het oudere NAS-document

### 1. Het NAS-document en zijn conclusie
Op de NAS staat een ouder artefact met dezelfde strekking als dit bestand, van vóór de brug:
- `…/Teknoparrot/findings/games/JurassicPark/CRASH-OPLOSSING.md` (2026-10-11, van "Johan — TeknoParrot-team")
- werkblad `…/Teknoparrot/findings/games/JurassicPark/PLT-slots.md` (939 regels): per PLT-stub het stubadres, het
  GOT-slot, de bestandswaarde en voor de libGL-groep de offset t.o.v. `0xb7500000`.

Zijn conclusie (**fase 1**): `VidStart` roept `glGetIntegerv(GL_MAX_TEXTURE_SIZE)` aan via slot `0x8add63c`, dat in
het bestand `0xb7500120` bevat — een adres uit het oorspronkelijke proces dat hier niet bestaat. Antwoord blijft 0,
0 ≤ 2047, dus `SysLockup`; daarna faalt de canariecontrole en valt de uitvoering door naar **`0x08358000`**.
Voorgestelde fix: de 93 libGL-slots herschrijven via basis `0xb7500000` + offset → de symbooltabel van de
originele libGL (Mesa 2015), "de rest hetzelfde, met de bijbehorende bibliotheek", plus `dlsym` onderscheppen.
Toets: één functie die 8192 in `0x8add63c` schrijft en kijken of het spel voorbij `VidStart` komt.
(NB: de NAS-mount is CIFS met `rw`; de afspraak blijft dat wij er niet in schrijven. Alle NAS-metingen read-only.)

### 2. Waar we het eens en oneens zijn — met de getallen
De tabel, read-only nagerekend in de coördinatie-ronde: 578 rijen (0–577, geen gaten) = resolverstub + 577 importstubs;
daarvan **2 zonder waarde**, **1 naar het spel zelf** (`0x0813a0f6`), en **575 in het `0xb7`-bereik** (dit
corrigeert de eerdere "576 × `0xb7`" — het verschil is die ene lege importstub). De NAS-groepsindeling:
93 libGL (`0xb750xxxx`), 25 (`0xb723xxxx`, libc-canarie), 457 overig `0xb7xxxxxx`, 2 leeg, 1 naar het spel.

- **Eens:** **573 van 575** waarden zijn exact 16-byte uitgelijnd (99,7%) en vallen in bibliotheek-grote clusters
  (grootste `0xb715a990`-`0xb72077c0`, 708.144 B, 143 waarden; top-10 dekt 442/482). Dit zijn **echte
  functieadressen** uit het oorspronkelijke proces, geen rommel. Mijn A2-diagnose (576 niet-GL-invoeren, 0
  uitvoerbaar) **bevestigt** dit: die sloten houden danglende `0xb7`-adressen die hier niet gemapt zijn.
- **Oneens / niet bevestigd:** de NAS-**methode** (offset → naam via onze bibliotheken) slaagt niet. Door de
  eigenaar getoetst tegen 74 32-bits bibliotheken incl. `/usr/lib32/libGL.so.1` (3.470 FUNC-symbolen): de 93
  libGL-offsets met basis `0xb7500000` landen **0 van 93** op een symbool; de beste clusterscore is **15/143 (10%)**
  = ruis, met tegensprekende "winnaars" (libGL vs libfmod vs libpng15). Hardere aanwijzing: de libGL-offsets
  (`0x40…0xc620`) zijn **te klein** voor een echte `.so` — het eerste FUNC-symbool van deze libGL staat pas op
  `0x2f4c0`. Dus "de rest hetzelfde, met de bijbehorende bibliotheek" is met **onze** bibliotheken niet gelukt.
- **Meetgrens, geen weerlegging:** mijn stublogger ving 0 sloten omdat hij een danglend, niet-gemapt doel niet kan
  uitlezen (zie boven). Het is dus geen bewijs tégen de NAS-tabel.

Eerlijke gezamenlijke stand (zie `patches/ELF-LADER-SPEC.md`, "Zitten de namen écht niet in het bestand?"): **de
informatie — echte functieadressen — zit in het bestand, maar de vertaalslag naar namen vraagt data die we niet
hebben**: de originele 32-bits bibliotheken uit 2015, of de proceskaart (`/proc/<pid>/maps`) van de originele kast.
Verschil tussen "er is niets" en "we kunnen er nog niet bij".

### 3. Sluiten de twee conclusies elkaar uit? Nee — het zijn fasen
- **Fase 1 (NAS, crash `0x08358000`): vóór de brug.** De GL/VidStart/canarie-crash. `rt-glfix` passeert die nu —
  de game leeft 112 s, voorbij `VidStart` **én** de canarie. Daarmee is de NAS-diagnose van fase 1 **bevestigd**,
  inclusief Johans voorspelling dat de canarie een *gevolg* van `SysLockup` is (we vallen er niet op). `rt-glfix`
  bereikt dit niet via de NAS-offset→naam-methode, maar door op ElfLdr2's eigen GL-resolutie mee te rijden + een
  cdecl-shim + een eigen `glGetIntegerv` (> 2047).
- **Fase 2 (deze diagnose, crash `0x819c524b`): ná de brug.** Een niet-GL-slot dat ElfLdr2 niet oploste, danglend
  `0xb7`. Dit is een **volgende fase**, geen tegenspraak met fase 1.
- Beter onderbouwd: voor fase 1 de NAS-diagnose (nu gemeten bevestigd). Voor de fix-methode weegt de read-only
  toets uit de coördinatie-ronde zwaarder dan beide eerdere claims: met onze libs niet herleidbaar.

### 4. Welke ene meting het beslecht
Geen bench-run beslecht dit — de blokkade is **data, geen analyse**:
- **Fase 1 is al beslecht** door `rt-glfix` (voorbij `VidStart` én de canarie). Johans 10-regeltest (8192 in
  `0x8add63c`) is daar een deelgeval van; `rt-glfix` is er een superset van, dus een losse 10-regeltest zou
  **zwakker** bewijs geven (hij zou waarschijnlijk eerder crashen op een ander ongefixt GL-slot). Daarom **niet
  gedraaid** — de bench was vrij, maar het was niet nodig en "forceren niet" gold.
- **De naamresolutie is al read-only beslecht** in de coördinatie-ronde: met onze bibliotheken lukt het niet. Wat het wél
  zou beslechten is geen meting maar data: de originele 2015-bibliotheken of de proceskaart van de originele kast.
  Tot die er zijn, blijft JP op BudgieLoader.

> Harde grens: geen HASP/dongle/keychip/FTDI/integriteit/activering en geen checksum-skip. De crash hierboven
> ligt vóór de HASP-controle van de game (die wordt nooit bereikt) en raakt die laag niet. De A2-meting is
> zuiver identificerend en begon géén uitpakwerk.
