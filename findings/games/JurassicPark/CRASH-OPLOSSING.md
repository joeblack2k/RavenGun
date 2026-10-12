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

Deze fout verdwijnt pas na **reconstructie van ElfLdr2's resolutietabel** (slot → echte functienaam/-adres). Die
tabel staat **niet in het bestand**; hij is alleen te oogsten uit een **lopende ElfLdr2-run** (zie
`docs/research/loskoppelen-takenlijst.md` §3b en `patches/ELF-LADER-SPEC.md`). Dat is **uitpakwerk** en valt
**buiten scope** volgens `docs/decisions.md` (2026-10-11). Daarom **houdt JP BudgieLoader** en blijft hij
geparkeerd — nu met een precieze, bevestigde diagnose in plaats van een vage.

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

**Gemeten (statisch, uit de patches-repo; aangeleverd door de eigenaar):** JP heeft **577 PLT-stubs, waarvan 105
met naam en 472 gewist**.

**Afgeleid:**
- De runtime-stublogger **ving niets**: de niet-GL-sloten presenteren zich deze run niet als een leesbare
  no-op-stub (hun doel ligt niet in een uitvoerbaar gebied dat de exec-cache ving — passend bij een doel in het
  niet-uitvoerbare gereserveerde blok waar `eip=0x819c524b` lag). Daardoor kon het **exacte eerst-geraakte slot
  niet runtime worden vastgelegd** — een gemeten beperking van deze methode, geen bewijs dat er geen lege sloten
  zijn. (Mogelijk ook een timing-kwestie: ElfLdr2 mapt zijn nagebootste libs later dan het de GOT vult, en de
  exec-cache bevriest vroeg.) Per opdracht géén tweede poging geforceerd.
- **De breedtevraag is wél beantwoord** door de statische getallen samen met de diagnose: **472 van de 577
  importnamen zijn gewist** en 576 niet-GL-invoeren wijzen nergens naar een bruikbaar doel. Dit is dus een
  **brede leegte, geen enkele ontbrekende import.** Een gerichte fix is niet haalbaar; **JP blijft geparkeerd op
  BudgieLoader.** Conclusie ongewijzigd.

> Harde grens: geen HASP/dongle/keychip/FTDI/integriteit/activering en geen checksum-skip. De crash hierboven
> ligt vóór de HASP-controle van de game (die wordt nooit bereikt) en raakt die laag niet. De A2-meting is
> zuiver identificerend en begon géén uitpakwerk.
