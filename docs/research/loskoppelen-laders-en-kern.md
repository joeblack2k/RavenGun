# Tussenrapport — de laders, de kern en de koppeling ertussen

**Herkomst:** de laderkeuze uit `scripts/launch-direct.py`, de eigen build (`BUILDINFO.txt` op de Deck), de
exporttabellen met `objdump -p`, de laderbron in `loaders/OpenParrotLoader/src/` en
`cores/x_OpenParrot_src/OpenParrot-master/`. Dit is de basis onder de kolom *Laderfamilie* van
[loskoppelen-per-game.md](../loskoppelen-per-game.md) en onder de sectie over de lader↔kern-koppeling.

---

## 1. De ladertabel

Uit `findings/19_loader_analysis.md` §1 (`GameSession::ResolveLoader [2984]`), nagerekend tegen
`resolve_loader()` in `scripts/launch-direct.py` (regel 315):

| `[Game] Type` / `EmulatorType` | Hulpprogramma | Argumenten | Games in onze lijst |
|---|---|---|---|
| `OpenParrot` (0) | `OpenParrotWin32/OpenParrot` / `OpenParrotx64/OpenParrot64` | (als argument van de lader) | **4** |
| `Lindbergh` (1) | `TeknoParrot/BudgieLoader.exe` | — | 0 |
| **`TeknoParrot` (2, 7, 8, 9)** | `OpenParrotWin32/OpenParrotLoader.exe` / `OpenParrotx64/OpenParrotLoader64.exe` | **`TeknoParrot/TeknoParrot` / `TeknoParrot64`** | **23** |
| `N2` (3) | `N2/BudgieLoader.exe` | — | 0 |
| `OpenParrotKonami` (4) | `OpenParrotWin32/OpenParrotKonamiLoader.exe` | — | 0 |
| `SegaTools` (5) | via de SegaTools-starter (`aimeio.dll`, `idzhook.dll`, `idzio.dll`) | eigen voorbereiding | 0 |
| **`ElfLdr2` (6)** | `ElfLdr2/BudgieLoader.exe` / `x64/BudgieLoader_x64.exe` | — | **8** |
| `TeknoMacaw` (10) | `TeknoParrot/TeknoMacaw.exe` / `TeknoMacaw64.exe` | — | **0** |

**Geen enkele game in de lijst gebruikt TeknoMacaw, Lindbergh, N2 of OpenParrotKonami.** De "drie
laderfamilies" komen in deze 35 games dus neer op twee: **OpenParrotLoader (27 games: 4 OpenParrot + 23
TeknoParrot)** en **BudgieLoader (8 games)**.

De letterlijke functie:

```python
def resolve_loader(p, tp):
    et = p.get("EmulatorType", "")
    is64 = truthy(p.get("Is64Bit", "false"))
    exe = "OpenParrotx64/OpenParrotLoader64.exe" if is64 else "OpenParrotWin32/OpenParrotLoader.exe"
    dll = ""
    if et == "ElfLdr2":
        exe = "ElfLdr2/x64/BudgieLoader_x64.exe" if is64 else "ElfLdr2/BudgieLoader.exe"
    elif et == "Lindbergh":
        exe = "TeknoParrot/BudgieLoader.exe"
    elif et == "N2":
        exe = "N2/BudgieLoader.exe"
    elif et == "TeknoMacaw":
        exe = "TeknoParrot/TeknoMacaw64.exe" if is64 else "TeknoParrot/TeknoMacaw.exe"
    elif et == "OpenParrot":
        dll = "OpenParrotx64/OpenParrot64" if is64 else "OpenParrotWin32/OpenParrot"
    elif et == "OpenParrotKonami":
        exe = "OpenParrotWin32/OpenParrotKonamiLoader.exe"
    elif et == "TeknoParrot":
        dll = "TeknoParrot/TeknoParrot64" if is64 else "TeknoParrot/TeknoParrot"
    else:
        raise Unsupported("EmulatorType %s wordt niet direct gestart ..." % et)
    return exe, dll, is64
```

De **kern** wordt dus alleen als **argument** meegegeven bij `OpenParrot` en `TeknoParrot`; bij `ElfLdr2`,
`Lindbergh`, `N2` en `TeknoMacaw` is er geen apart kern-argument — de lader **is** daar de kern.

---

## 2. Waar de lader vandaan komt: eigen build of runtime

`_defaults/TeknoParrot.ini` en `_defaults/OpenParrot.ini` zetten beide:

```ini
[Loader]
RemoteThread = off
Build = ravengun
```

En `own_build()` (`scripts/launch-direct.py` regel 340) vertaalt dat naar paden:

* `Build = ravengun` mag **alleen** als de lader `OpenParrotLoader(64).exe` is (anders `SettingRefused`).
* `Core = ravengun` mag **alleen** bij `EmulatorType == OpenParrot`.
* Standaardmap: `~/.local/share/ravengun/openparrot/current`.

**De eigen build** (`BUILDINFO.txt` op de Deck):

```
OpenParrot https://github.com/teknogods/OpenParrot.git @ fc8ba632e1f67c5c340ff88f30efca743e8d38f5 (licentie GPL-3.0)
gebouwd: 2026-10-11T14:28:55Z, doelen: loader32 loader64 core32 core64
patches:
  f7961968…  patches/0001-loader-no-context-floating-point.patch
  ee56b3b9…  patches/0002-mingw-w64-port.patch
container: localhost/ravengun-openparrot-build:3141e9a631a5 (mingw-w64-gcc 16.2.0-2, clang 23.1.1-1)
uitvoer:
  cc05247b…  ./OpenParrotWin32/OpenParrot.dll
  e6aa9a73…  ./OpenParrotWin32/OpenParrotLoader.exe
  e1709002…  ./OpenParrotx64/OpenParrot64.dll
  1953d91a…  ./OpenParrotx64/OpenParrotLoader64.exe
```

`scripts/build-openparrot.sh` pint de bron op commit `fc8ba632e1f67c5c340ff88f30efca743e8d38f5`
("2026-09-11, Add APM3 test support") en de container op digest; de bron wordt opgehaald met `git fetch` +
`git archive` en daarna **zonder netwerk** in de container gebouwd (`premake5.exe`, `*.bat` en `appveyor.yml`
worden nooit uitgevoerd). De bron belandt in `~/.cache/ravengun/src/openparrot` (nu niet aanwezig — hij wordt
op verzoek opgehaald).

**De uitzondering: TC5.** `patches/TC5/game.ini`:

```ini
; Lader uit de TeknoParrot-runtime: Time Crisis 5 (Namco ES3X) gebruikt bngrw.dll en iDmacDrv64.dll naast de lader; die zitten
; (nog) niet in de eigen OpenParrot-build (third_party/openparrot/README.md).
[Loader]
Build = teknoparrot
```

TC5 is de **enige** game die de lader uit de runtime neemt. Dat is de enige `Build`-regel in de hele
meegeleverde laag (`grep -H '^Build' patches/*/game.ini` geeft precies één treffer), en de gebruikerslaag zet
er geen enkele.

**In deze checkout staat de bron niet.** `third_party/openparrot/` bevat alleen:

```
third_party/openparrot/
  README.md
  build-in-container.sh
  compile-one.sh
  mingw-compat.h
  openparrot32.def
  shim.sh
  win64init.S
  patches/0001-loader-no-context-floating-point.patch
  patches/0002-mingw-w64-port.patch
```

De bron wordt bij het bouwen gekloond, niet meegeleverd.

---

## 3. De lader↔kern-koppeling — gemeten

### 3.1 Wat de lader opzoekt

De laderbron (`loaders/OpenParrotLoader/src/main.cpp`) zoekt in het normale pad **één** export van de kern op:

```c
// regel 391 (64-bits trampoline) en regel 471 (32-bits)
FindExport(pi.hProcess, pi.dwProcessId, dllLocation, "InitializeASI");
```

En in de aparte testmodus `--apm-test` (`ApmTestLoader.cpp` regel 358):

```c
const uintptr_t initialize = FindExport(pi.hProcess, pi.dwProcessId, data.module, "TP_InitializeApmTest");
```

`InitKonami` komt in de **hele** OpenParrot-bron **nul keer** voor. `PrepareSafeInit` komt alleen voor in
`OpenParrot/src/dllmain.cpp` (regel 237 als commentaar, regel 263 als de export zelf) — de laderbron zoekt
hem **nergens** op naam op. `OpenParrotKonamiLoader` haalt zijn binnenkomstpunten uit het **spelbestand**, niet
uit de kern (`OpenParrotKonamiLoader/src/main.cpp` regel 139-143):

```c
auto gameBinary = LoadLibraryA(argv[1]);
dll_entry_main dllMain = (dll_entry_main)GetProcAddress(gameBinary, "dll_entry_main");
dll_entry_init dllInit = (dll_entry_init)GetProcAddress(gameBinary, "dll_entry_init");
```

### 3.2 De exporttabellen (`objdump -p`)

| Bestand | Grootte | Exports (letterlijk, in volgorde) |
|---|---|---|
| `runtime/…/TeknoParrot/TeknoParrot64.dll` | **56.261.288** | `InitKonami`, `InitializeASI`, `TP_InitializeApmTest` |
| `runtime/…/TeknoParrot/TeknoParrot.dll` | **68.442.280** | `InitKonami`, `InitLinux`, `InitializeASI`, `_TP_InitializeApmTest@4` |
| `openparrot/current/OpenParrotx64/OpenParrot64.dll` | **1.930.752** | `InitializeASI`, `PrepareSafeInit` |
| `openparrot/current/OpenParrotWin32/OpenParrot.dll` | **1.996.288** | `InitLinux`, `InitializeASI`, `PrepareSafeInit@4`, `_PrepareSafeInit@4` |
| `openparrot/current/OpenParrotx64/OpenParrotLoader64.exe` | 1.227.264 | — |
| `openparrot/current/OpenParrotWin32/OpenParrotLoader.exe` | 1.278.464 | — |
| `runtime/…/OpenParrotx64/OpenParrotLoader64.exe` | 333.824 | — |

**`Name` is géén export.** `objdump -p` toont `Name` in geen van de vier exporttabellen. De tekst `Name`
komt in de binaries wél voor, maar dat is de **kopregel van de exporttabel zelf** — `objdump` schrijft
letterlijk `Ordinal   Hint Name`. Wie de uitvoer met `sed -n '/Name Pointer/,/^$/p'` of `grep Name` leest,
pakt dat kolomlabel mee en ziet `Name` als export. Het is geen export. Een eigen kern hoeft dus **geen**
`Name` te exporteren.

**Deze fout staat ook in `docs/research/kern-abi.md`** (een parallelle ronde, dezelfde dag), dat `Name` als
gemeenschappelijke export van beide kernen noemt. De tabel in §3.2 hierboven is de volledige, letterlijke
`objdump`-uitvoer en is de juiste.

**`TP_InitializeApmTest` is alleen nodig in de testmodus.** `main.cpp` regel 636-637:

```c
if (argc > 1 && _wcsicmp(argv[1], L"--apm-test") == 0)
    return RunApmTestLoader(argc, argv);
```

De opzoeking staat in `ApmTestLoader.cpp` regel 358 en wordt dus **alleen** bereikt met `--apm-test` als
eerste argument. `kern-abi.md` §3 concludeert dat een eigen kern die naam "dus moet leveren"; dat geldt
alleen voor de APM-testmodus, niet voor een gewone start.

### 3.3 Wat daaruit volgt

1. **De koppelings-ABI tussen lader en kern is één export: `InitializeASI`.** Dat is de enige naam die zowel
   de gesloten `TeknoParrot(64).dll` als de eigen `OpenParrot(64).dll` exporteert, en de enige die de lader in
   het normale pad opzoekt.
2. **De gesloten kern is 56 MB, de eigen kern 1,9 MB.** Dat verschil is geen code maar **kennis**: de
   per-game hooks, de profielenset en de patches zitten in die 56 MB.
3. **Een eigen kern is architectonisch mogelijk** — de lader hoeft niet specifiek TeknoParrot's DLL te hebben.
4. **De minimale exportlijst van een eigen kern:** `InitializeASI`. `TP_InitializeApmTest` alleen als iemand
   de `--apm-test`-modus gebruikt; `PrepareSafeInit`/`InitLinux`/`InitKonami` als lege stubs mogen, maar geen
   ervan is nodig voor een normale start.
5. **`PrepareSafeInit` blijft onbekend.** Het commentaar bij `dllmain.cpp` regel 237 zegt *"must remain inert:
   the loader invokes PrepareSafeInit after LoadLibrary"*, maar er staat in de hele bron geen aanroep. Hoe
   hij dan aangeroepen wordt (per ordinal? door de APM-testmodus?) is **niet vastgesteld**.

**Verwante documenten:** `docs/research/kern-abi.md` (parallelle ronde) behandelt dezelfde vraag; let op de
`Name`-fout daar (§3.2 hierboven). `docs/dropins.md` beschrijft waar de per-game kennis hoort te staan.

---

## 4. De dekking van de open OpenParrot-bron

Geteld met `Select-String -List` over alle bestanden in
`cores/x_OpenParrot_src/OpenParrot-master/` (het volledige bronpakket, lokaal aanwezig):

| `EmulationProfile` | Bestanden met een treffer |
|---|---|
| `GaiaAttack4` | **8** |
| `HauntedMuseum` | **9** |
| `HauntedMuseum2` | **8** |
| `MusicGunGun2` | **7** |
| `WartranTroopers` | **0** |
| `RawThrillsGUN` | **0** |
| `LuigisMansion` | **0** |
| `SegaJvsLetsGoIsland` | **0** |
| `TimeCrisis5` | **0** |
| `ALLSHOTDSD` | **0** |
| `FarCry` | **0** |
| `DeadHeat` | **0** |
| `InfinityBlade` | **0** |
| `TaitoTypeXGeneric` | **0** |
| `TransformersShadowsRising` | **0** |

**Conclusie: de open bron dekt alleen de vier Taito Type X2-titels.** Voor de andere 21
`Type = TeknoParrot`-games moet de per-game kennis opnieuw opgebouwd worden.

De bronbestanden van de TypeX2-titels:

```
OpenParrot/src/Functions/Games/TypeX2/GaiaAttack4InputMisc.cpp
OpenParrot/src/Functions/Games/TypeX2/TypeX2Generic.cpp
OpenParrot/src/Functions/D3D9Misc.cpp
OpenParrot/src/Functions/Global.cpp
OpenParrot/src/Functions/Types.h
OpenParrot/src/Functions/Ring_amLib/amJvs.cpp
OpenParrot/src/Utility/GameDetect.cpp
OpenParrot/src/Utility/GameID.h
```

En de patches die daaruit komen, nagelezen in `TypeX2Generic.cpp` regel 2239 en 2241:

```c
injector::MakeJMP(imageBase + 0x113E80, ReturnsTrue); // Disable Gun Writes

injector::MakeNOP(imageBase + 0x1156C8, 4); // Volume Write (Uses Analog0 from JVS which we use for inputs)
```

Dat is precies wat `patches/GaiaAttack4/patches.ini` als `Source` noemt:
*"OpenParrot TypeX2Generic.cpp (GPL-3.0), regel 2239"* en *"regel 2241"*. De kennis in `patches.ini` is dus
**herleidbaar tot de open bron** voor deze vier games.

---

## 5. Wat er aan de lader-/kernkant al gebouwd is

| Onderdeel | Status | Bewijs |
|---|---|---|
| Eigen `OpenParrotLoader(64).exe` | **gebouwd** | `~/.local/share/ravengun/openparrot/current/`, `BUILDINFO.txt` |
| Eigen `OpenParrot(64).dll` (kern voor Type X2) | **gebouwd** | idem; 4 games gebruiken hem |
| De Wine-MXCSR-fix als **broncodewijziging** | **in de eigen lader** | `third_party/openparrot/patches/0001-loader-no-context-floating-point.patch`; de bytepatch `patches/_defaults/OpenParrot.patches.ini` staat daarom standaard op `Enabled = false` |
| Eigen kern voor de 21 TeknoParrot-games | **niet gebouwd** | de open bron dekt die profielen niet (nul treffers) |
| Eigen `BudgieLoader`-vervanging (8 ElfLdr2-games) | **niet gebouwd** | `19_loader_analysis.md` §1 en §10: BudgieLoader is afgeschermd (onderdelen op `rawsize = 0`, payload in `.brr4`, dichtheid 7,97) |
| Eigen `pipehelper.exe` | **niet gebouwd** | `docs/research/loskoppelen-teknoparrot.md` §3: het protocol is "nu volledig bekend" (`antwoorden_ronde13.md`, `_ronde22.md`), de implementatie niet |

**En het `patches.ini`-formaat is het begin van werksoort 2.** De bytepatches staan al per game in de map,
met alle velden die een motor nodig heeft:

```ini
[Patch: Gun-schrijfacties uit (0x113E80)]
Enabled = false
Kind = memory
Category = input
AppliedBy = core
Module = game.exe
Offset = 0x113E80
Action = jmp-returns-true
Source = OpenParrot TypeX2Generic.cpp (GPL-3.0), regel 2239
Reason = OpenParrot-commentaar: "Disable Gun Writes". ...
```

Alleen de **motor die ze toepast** ontbreekt nog. `plan_file_patches()` in `scripts/launch-direct.py`
(regel 412) past nu alleen `Kind = file` toe (zoek-en-vervang op een kopie van de lader of de kern);
`Kind = memory` wordt overgeslagen (regel 441-444) met de reden
*"soort memory wordt niet door RavenGun toegepast (AppliedBy = core: de OpenParrot-kern doet dit zelf)"* —
dat is precies het gat dat de eigen kern moet vullen. In de `--plan`-uitvoer van GaiaAttack4, HauntedMuseum,
HauntedMuseumII en MusicGunGun2 staat bij elke memory-patch dan ook `"result": "overgeslagen"`.
