# Luigi's Mansion: wie beëindigt het spel, en wat doet de gesloten kern anders

*RavenGun, 2026-10-13, op de bench gemeten. Ruwe gegevens: `~/bw/luigi-exittrace/` (per run `exit-trace.log`,
`verslag.txt`, `opzet.txt`, `threads.tsv`, de overzichten; `VOORAF.txt` met de vooraf opgeschreven uitkomsten;
`exitrun.sh` waarmee elke run is gestart; twee beelden van het spel). Instrument: OpenParrot-patch
`third_party/openparrot/patches/0007-exit-trace.patch` en `tools/exit-trace-overzicht.py` Hoofdstuk 9 is de
ELF-kant en is zonder spel gemeten.*

**Kort.** Het spel sterft onder onze kern niet in de pakker en niet in onze init. Het is volledig ontpakt, start
zijn eigen code, en roept 1 ms na de laatste stap van de pakker `ExitProcess` aan vanuit zijn gevirtualiseerde
`WinMain`. Onder de gesloten kern komt die aanroep niet bij onze haak. Het verschil zit **stroomopwaarts**: de
gesloten kern patcht het ontpakte spel op 22 plaatsen voordat het spel begint, onze kern op nul. Zeventien van
die 22 zijn haken op de Sega-bibliotheek `amDongle*`/`amHm*`/`amMaster*` (keychip, hardwaremonitor, master), één
is een haak op een sprong naar de pakker, en vier zijn kleine codepatches.

## 1. Opzet

Elke run via de gewone route (`scripts/launch-direct.py LuigisMansion --run`), headless gamescope, harde
tijdslimiet, met de kern-sha256 uit het logboek van launch-direct vergeleken met de bedoelde. De runtime-kern is
nergens verwisseld: onze kern komt uit een eigen build-map via een tijdelijke gebruikerslaag (`[Loader] Core =
ravengun`, `Path = …-0007exit`); `current` is niet aangeraakt.

| variant | wat | uitkomst |
|---|---|---|
| onze kern + 0007 (zes runs) | de haken zitten in de globale init van de kern | exit 32 na ±6 s, draden 1 (kort 2), elke run geldig |
| gesloten kern kaal (controle) | `TeknoParrot64.dll` f5c7532d, niets erbij | leeft tot de limiet van 45 s, draden 1 → 21 → 92 |
| gesloten kern + `RgExitTrace64.dll` (vijf runs) | dezelfde haakcode als losse DLL via `TP_PRELOAD_DLL` | leeft tot de limiet, draden 1 → 21 → 92/95 |

De haak-DLL verstoort de gesloten kern dus niet meetbaar: zelfde draadverloop met en zonder.

Een lege meting bewijst eerst dat hij gezocht heeft: per haak een installatieregel (`create=MH_OK
enable=MH_OK`, bytes voor en na), en een zelftest (`TerminateProcess` op een handle dat niet bestaat) die in elke
run de regel van `TerminateProcess` en de geneste van `NtTerminateProcess` gaf. Zonder spel getest met een klein
programma dat zich langs elk van de vier wegen beëindigt: alle vier gevangen.

## 2. Onze kern: de aanroep

```
ExitProcess code=0x140415820 (laag32=0x40415820 laag8=32)
terugkeeradres = VACUUM.exe+0xf1374d  sectie .capcom0
bytes ervoor:   f6 e8 48 0c f6 ff 68 fe 49 3f b6 e8 3e 0c f6 ff
bytes erop:     68 be 4a 9f 8c e8 34 0c f6 ff 1e 0e a7 d1 e4 ad
rcx = 0x140415820 -> VACUUM.exe+0x415820 (.rdata): een vtable (pointers naar .text, RTTI-pointer ervoor)
stackwalk: OpenParrot64 (de haak) -> VACUUM.exe+0xf1374d (.capcom0) -> VACUUM.exe+0x30fb00 (.text) -> einde
```

- **De sectie is `.capcom0`, niet `.capcom1`** zoals een eerdere notitie zei (`.capcom1` begint op RVA 0xf15000).
- **Het terugkeeradres is geen beslispunt.** De bytes zijn een tabel van tussenstukken `68 <sleutel> e8 <rel32>`
  (push sleutel, call `0x140e7438b`), om de tien bytes herhaald: het import-mechanisme van de pakker. Het adres
  zegt langs welk tussenstuk `ExitProcess` is bereikt, niet welke voorwaarde daartoe besloot.
- **De keten.** `VACUUM.exe+0x30fb00` is het terugkeeradres van de aanroep op `+0x30fafb` in de CRT-start
  (`mov rcx,rdi / xor edx,edx / mov r8,rax / movzx r9d,si / call`, daarna `exit`/`_cexit`): de aanroep van
  `WinMain`. `WinMain` zelf (`+0x11c5f0`) is alleen `jmp 0x140e8189b`, een sprong `.capcom0` in. Dus:
  CRT-start → `WinMain`, gevirtualiseerd door de pakker → `ExitProcess`. De frames binnen de virtuele machine
  geeft de stackwalk niet.
- **De code is geen foutcode.** `rcx` is een achtergebleven pointer naar een vtable; 32 is alleen de lage byte.
- Erna genest: `RtlExitUserProcess` en twee keer `NtTerminateProcess`, zoals Wine dat doet.

**Correctie op de eerdere stand ("`.text` nog nul bij het einde").** Momentopname bij de beëindiging: `.text`
88,8 % niet-nul, `.rdata` 64,9 %, `.capcom0` 92,6 %, met dezelfde beschermingen als onder de gesloten kern. Het
spel is dan volledig ontpakt. De eerdere bemonstering van buitenaf had daar een gat.

## 3. De tijdlijn rond het ontpakken (meekijkende haak op `NtProtectVirtualMemory`)

Onze kern, alle 16 aanroepen, alle met status 0:

```
+0 ms    entry-bytes teruggezet, sprong naar het entry point
+0..8    de pakker maakt .capcom0, .rdata, .text, .pdata schrijfbaar          (begin ontpakken)
+240     de pakker haakt ntdll!DbgUiRemoteBreakin (doel VACUUM.exe+0xd77780)
+240     de pakker zet de eindbeschermingen (.capcom0 RX, .rdata R, .text RX, .pdata R)   (einde ontpakken)
+241     ExitProcess
```

Gesloten kern, 273 aanroepen in 45 s, alle met status 0:

```
dezelfde vijf aanroepen van de pakker aan het begin, dezelfde vijf aan het einde
+6 ms na de laatste: 44 aanroepen op VACUUM.exe .text (36 van 5 bytes, 4 van 4, 4 van 1), op de hoofddraad
                    = 22 plaatsen, elk open en weer dicht; dezelfde 22 RVA's als in hoofdstuk 5
daarna laadt het spel d3d11, dsound, quartz, winegstreamer ... en maakt het draden aan
```

Wine weigert in geen van beide runs een aanroep. Het afvangen zelf maakt geen verschil: met de meekijkende haak
erin eindigt het spel onder onze kern op hetzelfde moment met dezelfde code.

## 4. Wat elke kern in het proces aanraakt

Gemeten met het overzicht van 0007 (elke geëxporteerde functie van elke geladen module, eerste 16 bytes) tegen
het DLL-bestand op schijf. IJkpunt zonder spel: precies de vier eigen haken en verder niets. Vergeleken: 14.769
functies (onze kern, bij de beëindiging) en 14.827 (gesloten kern, bij de vijfde draad, vlak na het ontpakken).

| | functies |
|---|---|
| alleen de gesloten kern | `ntdll!NtProtectVirtualMemory` (= `ZwProtectVirtualMemory`), `kernel32!DeleteFileA/W`, `d3d9!Direct3DCreate9Ex`, `dxgi!CreateDXGIFactory1` |
| alleen onze kern | `kernel32!CreateDirectoryA/W` (onze schijfletter-herleiding, patch 0004) |
| beide | `CreateFileA/W`, de COM-set (`ClearCommError`, `EscapeCommFunction`, `Get/SetCommMask`, `GetCommModemStatus`, `Get/SetCommState`, `Get/SetCommTimeouts`, `PurgeComm`, `SetupComm`), `d3d9!Direct3DCreate9`, `dxgi!CreateDXGIFactory` en `…2`, `ExitProcess`, en `ntdll!DbgUiRemoteBreakin` |

- `ExitProcess` staat bij "beide" om twee verschillende redenen: bij ons is het de haak van 0007, bij de gesloten
  kern een eigen haak die hij na het laden over de onze heen zet (bytes `e9 43 61 fd ff` → `e9 f9 5b d7 fe`).
  **Grens van de meting:** daardoor ligt zijn haak buiten de onze. Dat onze haak onder de gesloten kern geen
  `ExitProcess` ziet, bewijst dus niet dat het spel hem nooit aanroept.
- `NtProtectVirtualMemory` is bij de gesloten kern al gehaakt bij de eerste draad, als `.text` nog 0 % gevuld is.
  Het sprongdoel is `0x6ffff6fb3910`, binnen `TeknoParrot64.dll` (basis `0x6ffff6b60000`, grootte `0x698d000`).
- `DbgUiRemoteBreakin` wordt onder onze kern na de init gehaakt met doel `VACUUM.exe+0xd77780`: dat doet het spel
  zelf.
- Onze eigen haken: 34 aangeraakte functies direct na de init, dezelfde 34 bij de beëindiging, niets overschreven
  of weggehaald.
- De lijst van de gesloten kern verandert niet meer tussen draad 5 en draad 60.
- De import-adres-tabel van `VACUUM.exe`: 415 items, in beide runs gelijk.
- Niet geverifieerd: het einddoel van de overige haken van de gesloten kern (de sprongen wijzen naar een
  tussenblok buiten elke module; alleen van `NtProtectVirtualMemory` is het einddoel uitgelezen).

## 5. Wat de gesloten kern in het spel zelf verandert

Het beeld van `VACUUM.exe` onder onze kern (bij de beëindiging) tegen dat onder de gesloten kern (vijfde draad).
Kop, `.pdata`, `.capcom1`, `.reloc`, `.rsrc`: byte-gelijk. `.capcom0`: 17 reeksen looptijdtoestand. `.data`:
looptijdtoestand. **`.text`: 22 reeksen, 99 bytes; `.rdata`: 2 reeksen.** Tussen draad 5 en draad 60 verandert
in `.text`, `.rdata` en `.capcom0` niets meer.

| RVA | onze kern | gesloten kern | wat |
|---|---|---|---|
| `0x485a` | `e9 c8 fa e6 00` (`jmp` naar `.capcom0` `+0xe75327`) | `e9 2f c3 fe ff` (naar het haakblok onder het beeld) | een sprong de pakker in, gehaakt zoals de functies hieronder |
| `0x17a9a` | `41 0f 45 c9` (`cmovne ecx,r9d`) | `33 c9 ff c1` (`ecx = 1`) | uitkomst vastgezet (code ernaast drukt HOURS/MINUTES af) |
| `0x4e05f` | `0f b6 58 04` (lees byte) | `88 58 04 90` (schrijf byte) | vlag gezet in plaats van gelezen (naast `../../Ram/`) |
| `0xabcf4` | `74 18` (`je`) | `eb 18` (`jmp`) | tak altijd genomen |
| `0x173e30` | `41 bc 08 00 00 00` | `41 bc 00 00 00 00` | constante 8 → 0 |
| `0x2b3b70 … 0x2b4320` (7) | functiebegin | `e9 …` | `amDongleBillingGetKeychipId`, `GetGameId`, `GetModelType`, `GetNetworkAddress`, `Init`, `IsAvailable`, `SetupKeychip` |
| `0x2b4740` | functiebegin | `e9 …` | niet benoemd (geen string) |
| `0x2b50f0 … 0x2b56f0` (5) | functiebegin | `e9 …` | `amHm*` (Exit, GetCautionFlag, Init, ResetCautionFlag, Update) |
| `0x2b5860 … 0x2b6160` (4) | functiebegin | `e9 …` | `amMaster*` (Exit, Init, SetBackupEventLogParam, Update) |
| `.rdata 0x3d0c20` | leeg | `../../DataGlobal/EN/Data/` | datapad |
| `.rdata 0x3dc9d8` | `Ver.%2u.%02u.%02u` | `Reaver Was Here` | versietekst |

Het sprongdoel van de 18 haken in `.text` (de 17 functies en de sprong op `0x485a`) ligt in een haakblok vlak onder
het beeld (`basis-0xf02c` … `basis-0xf472`), buiten elke module. Het einddoel achter dat blok is niet uitgelezen, dus
in welke module de vervangende code staat is voor deze 18 niet gemeten; alleen dat de aanroepen die ze plaatsten 6 ms
na het ontpakken op de hoofddraad kwamen, en dat ze onder onze kern ontbreken.

De namen bij de functiehaken komen uit de foutstrings waar elke functie naar verwijst; het zijn de namen van de
functie zelf of van de buurfunctie, dus de toewijzing kan één plaats verschoven zijn.

## 6. Conclusie en wat niet is vastgesteld

1. **Het verschil zit stroomopwaarts, niet in de voorwaarde op het exitpunt.** De meting die dat onderscheidt:
   onder de gesloten kern staat het spel 6 ms na de laatste stap van de pakker met 22 patches in `.text`, vóór
   zijn eigen code begint; onder onze kern staat op alle 22 plaatsen de oorspronkelijke code en volgt
   `ExitProcess` 1 ms na diezelfde stap.
2. **Het mechanisme van de gesloten kern:** hij haakt `NtProtectVirtualMemory` voor het ontpakken en patcht het
   spel zodra de pakker zijn eindbeschermingen zet. Onze kern heeft voor deze game geen enkele patch
   (`RunFunctions(game=143)` is leeg, patch 0005 is alleen detectie).
3. **Niet vastgesteld:** welke van de 22 patches de `ExitProcess` voorkomt. `WinMain` is gevirtualiseerd, dus de
   voorwaarde is niet uit het beeld te lezen. De kandidaten zijn de gehaakte sprong op `0x485a` en de
   `amDongle`-set.
4. **Dit is geen laderwerk.** Zeventien van de 22 patches vervangen keychip, hardwaremonitor en master van Sega's
   bibliotheek. Dat valt onder `docs/geen-dongle-in-de-loader.md` en is een beslissing van de eigenaar, geen
   generieke infrastructuur. Overgedragen.

## 7. Bijwerkingen en opruimen

- **NAS:** onder onze kern nul gewijzigde bestanden in elke run. Onder de gesloten kern schrijft het spel in
  elke run zijn eigen drie `Ram/`-bestanden (`CreditConfig.bin`, `EEPROM_0x00001100.bin`,
  `SRAM_0x00004200.bin`); dat is het bekende gedrag van het spel op deze route, niets van ons.
- Runtime-kern na elke run `f5c7532d…`; `current` ongemoeid; na elke run nul processen.
- De build met 0007 staat in `~/.local/share/ravengun/openparrot/fc8ba632…-0007exit/`.

## 8. Opnieuw doen

```
scripts/build-openparrot.sh --out ~/.local/share/ravengun/openparrot/<commit>-0007exit \
    --targets "loader32 loader64 core32 core64 exittrace64"
RG_INIT_TRACE=Z:\pad\naar\spoor.log        # aan; zonder dit haakt 0007 niets
RG_EXIT_PROBE=f13737,e7438b                # 32 bytes op deze RVA's van de hoofdmodule in elke momentopname
RG_EXIT_DUMP=1                             # het beeld van de hoofdmodule bij elke momentopname
RG_EXIT_PROTECT=1                          # meekijken op NtProtectVirtualMemory
TP_PRELOAD_DLL=Z:\…\OpenParrotx64\RgExitTrace64.dll   # voor een proces zonder onze kern
tools/exit-trace-overzicht.py verschil <overzicht A> <overzicht B>
```

## 9. Wat dit oplevert voor de ELF-kant (`rg-elfloader --ontleed-alleen`)

*2026-10-13, zonder een spel te starten. Ruwe gegevens: `~/bw/elf-ontleed/` (`VOORAF.txt` met de vooraf
opgeschreven uitkomsten, de twee ontledingen van Terminator Salvation, het testlog, de mutaties). Het spelbestand
op de NAS is alleen gelezen (sha256 `798dec07…`; lijst, grootte en mtime van de map voor en na gelijk). Lader:
sha256 `2190a168…`, gebouwd = geïnstalleerd = uit de huidige bron.*

**Kort.** De ontleed-modus geeft dezelfde soort zekerheid als de trace voor alles tot aan de sprong naar het spel,
en geen enkele voor wat daarna komt. Drie dingen die de trace wel had en de ontleding niet, zitten er nu in.

| les uit de trace | wat de ontleding miste | nu |
|---|---|---|
| een lege meting bewijst eerst dat hij gezocht heeft (installatieregel per haak, zelftest) | "0 onopgelost" en "markerbestand ontbreekt" hadden geen tegenproef | de test laat de analyse een onvindbare lib en een ontbrekend symbool melden, en start dezelfde gast in de gewone modus (rc 6, markerbestand aanwezig) |
| een adres zonder module is geen bewijs (terugkeeradres als module+offset; `.capcom0`, niet `.capcom1`) | "toegepast" zei alleen dat er een adres was ingevuld, niet wiens adres | per toegepaste relocatie de module van het doel; de som moet gelijk zijn aan "toegepast" |
| zeg wat de meting niet ziet ("grens van de meting") | "onopgelost" telde zwakke en sterke symbolen bij elkaar, en toonde alleen de eerste tien namen | `N onopgelost (Z zwak, S sterk)`; de sterke namen staan in een eigen lijst |

**Gemeten op Terminator Salvation** (statisch met `readelf` en met de lader; de twee tellingen sluiten):

| | statisch | `--ontleed-alleen` |
|---|---|---|
| dynamische symbolen | 1020, geen `DT_HASH` | 1020 via `(DT_STRTAB-DT_SYMTAB)/16` |
| bibliotheken | 13 `DT_NEEDED` | 13 gevonden en geladen, geen "NIET geladen" |
| relocaties | 517 in `DT_REL` (511 `GLOB_DAT`, 6 `COPY`) + 496 in `DT_JMPREL` = 1013, geen `RELATIVE` | 1011 toegepast, 2 onopgelost (2 zwak, 0 sterk): twee keer `__gmon_start__` |
| lib-defs | | 16.440; shim 515 van de 1020 |

Doelen van de 1011 toegepaste relocaties: 509 de gast zelf (via de shim), 247 `libc`, 161 `libGL`, 21 `libm`,
19 `libtheora`, 15 `libglut`, 14 `libogg`, 13 `libstdc++`, 8 `libGLU`, 3 `libPhysXLoader`, 1 `libgcc_s`. Nul naar
de lader zelf: de fout uit de tijd van `-rdynamic` (het geïmporteerde `main` loste op naar de `main` van de
lader, en telde als toegepast) zou hier als eigen regel staan.

**Het plafond van de lib-defs-lijst.** 16.440 is 56 boven het oude plafond van 16.384. Dezelfde lader met dat
plafond (losse bouw) laat op deze game 56 namen wegvallen; de shim blijft 515, dus het had hier geen gevolg,
maar het gebeurde stil. Nu: 262.144 namen en 8 MiB tekst, en als de lijst toch vol raakt weigert de gewone modus
te starten (`FATAAL`, rc 127) en meldt de analyse het op stderr en in de samenvatting. De test dwingt het af
tegen de echte lader met een lib van plafond + 1000 symbolen.

**Grens van de meting.**
- De analyse stopt vóór de sprong. Wat de trace bij Luigi's Mansion liet zien (volledig geladen, en dan beëindigt
  het spel zichzelf) kan zij niet zien: dezelfde synthetische gast geeft in de analyse rc 0 en in de gewone modus
  rc 6, en de samenvatting zegt daar niets over.
- rc 0 van de analyse betekent niet dat de gewone modus de sprong haalt: een lib die niet laadt is in de analyse
  een regel en in de gewone modus fataal. Lees "NIET geladen" en "sterk".
- "Start de gast niet" is niet "voert niets uit": `dlopen` draait de init-code van de dertien bibliotheken.
- De module van een doel komt uit `dladdr` op het opgezochte adres; "de gast zelf" is een adres binnen het
  gemapte beeld.
- Niet gewijzigd en nog stil: `copyset[512]` (deze game: 6 `COPY`), `rg_libs[64]` en `rg_libinfo[64]` (deze game:
  13), 24 `PT_LOAD`-segmenten per lib, 1024 stale slots.

**De valkuil van de oude lader, een stap eerder.** `tests/elfloader_install_test.sh` vergeleek gebouwd met
geïnstalleerd. Met een gewijzigde bron die niet herbouwd is zijn die twee gelijk en allebei oud, en bleef de test
groen (gemeten). De test herbouwt nu uit de bron (`scripts/build-elfloader.sh --alleen-bouwen`, zelfde vlaggen,
installeert niets) en eist dezelfde sha256; de bouw is reproduceerbaar, ook vanuit een andere map.

```
LD_LIBRARY_PATH=<ElfLdr2/libs>:<runtime/lib32> rg-elfloader --ontleed-alleen <game>
sh tests/elfloader_gnuhash_test.sh      # RG_ELFLOADER_SRC=<andere bron> om een mutatie rood te zien worden
sh tests/elfloader_install_test.sh
```
