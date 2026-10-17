# Loskoppelen van TeknoParrot — de per-game kaart (35 games)

**Status:** nieuw overzichtsdocument, 2026-10-11. Dit document verandert geen bestaande bevindingen; het
bundelt wat er per game al gemeten en vastgelegd is, en voegt per game één regel toe: *wat draait er nu
nog mee van TeknoParrot, en wat is de kortste weg naar los*.

**Herkomst van elk feit.** Alles hieronder is herleidbaar tot een bestand of een commando:

| Bewijsbron | Wat het levert |
|---|---|
| `patches/<Game>/game.ini` (repo, `~/projects/ravengun/patches/`) | `[Game] Type`, `EmulationProfile`, `DongleRegion`, `PcbRegion`, `Executable`, `Is64Bit`, `Patreon`, `HasTwoExecutables`, `[Launch] Mode`, `[Loader] Build` |
| `python3 -I scripts/launch-direct.py <Game> --plan --json` | de **echte** argv die de launcher start: lader, kern, omgeving, patches, `needs_activation`, `dongle`, `files` (de gegenereerde `teknoparrot.ini`) |
| `~/.local/share/ravengun/runtime/views/<Game>/` | de view die als werkmap dient (symlinks naar de NAS) |
| `GameProfiles/<Game>.xml` (runtime én de Windows-installatie) | `EmulatorType`, `GameProfileRevision`, `Patreon`, `HasTwoExecutables`, de profielvelden `DongleRegion`, `PcbRegion`, `Region` |
| De NAS-spelmap (`/volume2/emulation/Teknoparrot/<map>`) | de concrete dongle-bestanden (`matrix64.dll`, `hasp/`, `fff4`, `libhasp_linux_*.so`, `dk2win64.dll`, …) |
| `findings/*.md` op de NAS en in deze werkmap | de eerdere metingen en de antwoorden van het TeknoParrot-team |
| `docs/research/loskoppelen-*.md` (nieuw) | de tussenrapporten met de ruwe uitdraaien |

**Narekenen van de kernkolom.** Eén commando per game:

```bash
cd ~/projects/ravengun
python3 -I scripts/launch-direct.py <Game> --plan --json
```

Dat is de methode waarmee kolom *Draait nu mee* is gevuld; de volledige uitvoer van alle 35 games staat in
[loskoppelen-metingen.md](research/loskoppelen-metingen.md).

---

## Samenvatting

**Wat de 35 games nu nog van TeknoParrot gebruiken** (gemeten met `--plan`):

| Startpad dat de launcher kiest | Games | Welke |
|---|---|---|
| Eigen OpenParrotLoader **+ eigen OpenParrot-kern** (open bron, GPL-3.0) | **4** | GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2 |
| Eigen OpenParrotLoader **+ gesloten TeknoParrot-kern** (`TeknoParrot(64).dll`) | **20** | Gashaaaan, Gashaaaan2, InfinityBladeFX, LGI, LGI3D, LGS, LethalEnforcers3, LuigisMansion, MedaruNoGunman, NERF, Omatsuri ×2, Otomedius, PigsMightFly, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Rampage, TombRaider, TransformersShadowsRising |
| Gesloten **BudgieLoader** (ElfLdr2) uit de runtime | **7** | GalagaAssault, GhostBusters, GoGoStrike, Halo, JurassicPark, PacmanChomp, Terminator |
| **Runtime** OpenParrotLoader64 + gesloten kern (`[Loader] Build = teknoparrot`) | **1** | TC5 |
| Niet met de directe start te starten | **3** | HOTDSD (twee exe's), 3DCosplayMahjong en TankTankTank (staan niet op de NAS) |

*Let op bij rij 2 en 3:* dit is wat `launch-direct.py --plan` zou starten als je die game met de
**TeknoParrot-route** start. Voor **TombRaider** en **RabbidsHollywood** is dat niet de standaardroute: die
twee gaan via de eigen Unity-plugin (zie punt 1 hieronder), en de TeknoParrot-route is daar alleen nog een
terugval met `--teknoparrot`.

**De drie aantallen die de opdracht vraagt:**

1. **Al los van TeknoParrot: 2 games.** Tomb Raider en Rabbids Hollywood starten via hun eigen BepInEx-plugin
   (`patches/<Game>/unity.json`, plugin `ravengun`, TCP 33610) — daar zit geen enkel TeknoParrot-onderdeel
   meer in het startpad (`scripts/play-halo.sh` regel 205-211 stuurt ze naar `scripts/play-unity.sh`).
   **Beide starten via de eigen plugin.** Voor Rabbids Hollywood gold "stopt na ongeveer een minuut op zijn **eigen**
   Matrix-donglecontrole"; dat bleek een uitrol-fout (verouderde plug-in in de view). Hermeting 2026-10-12: de game start en leeft
   288 s in de attract-loop via `scripts/play-unity.sh`. Munt, start en richten zijn niet gemeten (headless); zie `packaging/games-status.md`.
2. **Een donglelaag nodig: 19 games.** HASP 6 (waarvan GhostBusters onzeker), Matrix/SBK 4, regio/PCB 7,
   `amdaemon` 1, Sega-donglescan 1. **Dit is een andere dimensie dan punt 1 en 3:** 2 van die 19 (Tomb Raider
   en Rabbids Hollywood) staan óók in punt 1. De andere **16 games hebben geen enkele dongle-aanwijzing.**
3. **Wachten op iets anders: 14 games.** Dat zijn de games die noch in punt 1 noch in punt 2 zitten en die
   geen dongle nodig hebben.

**En de partitie die wél optelt tot 35** — naar *wat de kortste route nog vraagt*:

| Wat de kortste route vraagt | Games |
|---|---|
| **Kan al los** (alleen nog een drop-in; geen TeknoParrot-onderdeel in het pad) | **2** (TombRaider, RabbidsHollywood) |
| **Alleen per-game werk** — lader én kern zijn al van ons (de 4 Type X2-games) | **4** |
| **Een eigen kern** (werksoort 2, centraal) | **21** |
| **Een eigen lader/preload** (werksoort 3, centraal per bitfamilie) | **8** (de 8 ElfLdr2-games) |
| **Totaal** | **35** |

En de **donglelaag is een extra laag** bovenop die partitie: 19 games hebben er een nodig, 16 niet.

**De belangrijkste conclusie — en die is niet over dongles.** De **lader** is voor 24 van de 35 games al van
ons: `patches/_defaults/TeknoParrot.ini` en `_defaults/OpenParrot.ini` zetten `[Loader] Build = ravengun`, en
`scripts/build-openparrot.sh` bouwt die lader uit de **openbare** OpenParrot-bron
(`github.com/teknogods/OpenParrot`, commit `fc8ba632e1f67c5c340ff88f30efca743e8d38f5`, GPL-3.0; de
`BUILDINFO.txt` van de build staat op de Deck). Maar:

> **De open OpenParrot-bron dekt alleen de vier Taito Type X2-titels.** In
> `cores/x_OpenParrot_src/OpenParrot-master/` komen `WartranTroopers`, `RawThrillsGUN`, `LuigisMansion`,
> `SegaJvsLetsGoIsland`, `TimeCrisis5`, `ALLSHOTDSD`, `FarCry`, `DeadHeat`, `InfinityBlade`,
> `TaitoTypeXGeneric` en `TransformersShadowsRising` **nul keer** voor; `GaiaAttack4` (8 bestanden),
> `HauntedMuseum` (9), `HauntedMuseum2` (8) en `MusicGunGun2` (7) wél. De eigen lader is dus herbouwd, maar de
> **kern** blijft voor 21 games de gesloten `TeknoParrot(64).dll` — en `own_build()` in
> `scripts/launch-direct.py` (regel 340) staat `Core = ravengun` **alleen** toe bij `EmulatorType == OpenParrot`.

**De gesloten kern is dus de resterende hoofdblokkade, niet de dongle en niet de lader.** Voor de **21 games
met een gesloten kern** (20 via de eigen lader + TC5 via de runtime-lader) is er geen open bron die de
WartranTroopers-/RawThrillsGUN-/FarCry-/TimeCrisis5-hooks levert; die moeten of zelf geschreven worden (met de
exacte `RUNNING <naam> <versie>`-controle van de kern als referentie) of per game vervangen worden door een
native route. Alleen dongle- en patchwerk haalt het doel "elke game zonder TeknoParrot" niet.

**En dat is haalbaarder dan het lijkt, om één gemeten reden:** de koppeling tussen de lader en de kern is
**één export, `InitializeASI`** (`OpenParrotLoader/src/main.cpp` regel 391 en 471; met `objdump -p`
nagegaan). De gesloten `TeknoParrot64.dll` is 56 MB, de eigen `OpenParrot64.dll` 1,9 MB — dat verschil is
geen code maar **kennis**. De interface is dus al van ons; wat ontbreekt is de per-game spellogica, en
`patches/<Game>/patches.ini` is daar per game al een begin van (alleen de motor die de patches toepast
ontbreekt nog). Zie de sectie **De lader↔kern-koppeling** hierboven.

**En de eerlijke grens:** voor 16 van de 35 games is er in het profiel, in `game.ini` en in de spelmap **geen
enkele dongle-aanwijzing** gevonden. Dat betekent niet dat ze geen dongle hebben — het betekent dat de
kern het (mogelijk) intern afhandelt en dat wij het niet kunnen zien zonder de gesloten kern te openen.

**En de modulaire afspraak verandert daar niets aan.** `docs/dropins.md` wil dat alles wat één game nodig
heeft in `patches/<Game>/` staat, zodat een game toevoegen één map aanraken is. Dat werkt voor de **patches**,
de **dongle** en de **drop-ins** — en dat is **werksoort 1**, die 6 games alleen nodig hebben. Maar voor de
**21** TeknoParrot-type games is de kern de blokkade, en die kern is **één DLL met de kennis van alle games**
(56 MB tegen 1,9 MB) — die is **niet per game** door een drop-in te vervangen. Dat is **werksoort 2**, en dat
is centrale arbeid. Zie de sectie **De drop-in-afspraak** hierboven.

---

## De lader↔kern-koppeling: waarom een eigen kern architectonisch kan

Dit is het antwoord op de vraag *"is loskoppelen van de 21 `Type = TeknoParrot`-games haalbaar?"*. Het korte
antwoord: **de interface is klein, de kennis is groot.**

**Wat de lader uit de kern opzoekt.** In de laderbron staat precies één aanroep die een export van de kern
opzoekt in het normale pad:

```c
// loaders/OpenParrotLoader/src/main.cpp, regel 391 (64-bits trampoline) en regel 471 (32-bits)
FindExport(pi.hProcess, pi.dwProcessId, dllLocation, "InitializeASI");
```

En verder alleen in de aparte testmodus `--apm-test`:

```c
// cores/x_OpenParrot_src/OpenParrot-master/OpenParrotLoader/src/ApmTestLoader.cpp, regel 358
const uintptr_t initialize = FindExport(pi.hProcess, pi.dwProcessId, data.module, "TP_InitializeApmTest");
```

`InitKonami` komt in de **hele** OpenParrot-bron **nul keer** voor; de lader zoekt het nooit op.
`OpenParrotKonamiLoader` haalt zijn binnenkomstpunten niet uit de kern maar uit het **spelbestand**
(`main.cpp` regel 139-143: `LoadLibraryA(argv[1])` → `GetProcAddress(gameBinary, "dll_entry_main")` /
`"dll_entry_init"`).

**De gemeten exporttabellen** (`objdump -p`, op de Deck):

| Bestand | Grootte | Exports (letterlijk) |
|---|---|---|
| `runtime/…/TeknoParrot/TeknoParrot64.dll` | **56.261.288** | `InitKonami`, `InitializeASI`, `TP_InitializeApmTest` |
| `runtime/…/TeknoParrot/TeknoParrot.dll` (32-bits) | **68.442.280** | `InitKonami`, `InitLinux`, `InitializeASI`, `_TP_InitializeApmTest@4` |
| `openparrot/current/OpenParrotx64/OpenParrot64.dll` | **1.930.752** | `InitializeASI`, `PrepareSafeInit` |
| `openparrot/current/OpenParrotWin32/OpenParrot.dll` | **1.996.288** | `InitLinux`, `InitializeASI`, `PrepareSafeInit@4`, `_PrepareSafeInit@4` |
| `openparrot/current/OpenParrotx64/OpenParrotLoader64.exe` | 1.227.264 | — |
| `openparrot/current/OpenParrotWin32/OpenParrotLoader.exe` | 1.278.464 | — |
| `runtime/…/OpenParrotx64/OpenParrotLoader64.exe` | 333.824 | — |

**Wat daaruit volgt:**

1. **De koppelings-ABI tussen lader en kern is één export: `InitializeASI`.** Dat is wat
   `OpenParrotLoader(64).exe` in het normale pad opzoekt, en het is de enige naam die zowel de gesloten
   `TeknoParrot(64).dll` als de eigen `OpenParrot(64).dll` exporteert.
2. **De gesloten kern is 56 MB, de eigen kern 1,9 MB.** Dat verschil is geen code maar **kennis**: de
   per-game hooks, de profielenset en de patches zitten in die 56 MB. De ABI is klein, de spellogica is groot.
3. **Een eigen kern is dus architectonisch mogelijk** — de lader hoeft niet specifiek TeknoParrot's DLL te
   hebben. Wat ontbreekt is de per-game kennis, niet de interface. `LOSKOPPELEN.md` deel 1 en 2 beschrijft
   precies die zes onderdelen.
4. **`Name` is géén export — en dat is een correctie op een parallel rapport.** `docs/research/kern-abi.md`
   (een parallelle ronde, dezelfde dag) noemt `Name` als gemeenschappelijke export van beide kernen, en
   dezelfde aanname kwam via de tussenrapportage binnen. **Dat klopt niet.** Mijn `objdump -p`-uitvoer geeft
   de exporttabellen letterlijk en volledig:

   ```
   TeknoParrot64.dll  Export Address Table 00000003  -> InitKonami, InitializeASI, TP_InitializeApmTest
   TeknoParrot.dll    Export Address Table 00000004  -> InitKonami, InitLinux, InitializeASI, _TP_InitializeApmTest@4
   OpenParrot64.dll   Export Address Table 00000002  -> InitializeASI, PrepareSafeInit
   OpenParrot.dll     Export Address Table 00000004  -> InitLinux, InitializeASI, PrepareSafeInit@4, _PrepareSafeInit@4
   ```

   **Waar de fout vandaan komt:** de kopregel van de exporttabel in `objdump` is letterlijk
   `Ordinal   Hint Name`. Wie de uitvoer met `sed -n '/Name Pointer/,/^$/p'` of `grep Name` leest, pakt dat
   **kolomlabel** mee en ziet `Name` als een export. Het is geen export. **Een eigen kern hoeft dus géén
   `Name` te exporteren** — alleen `InitializeASI`.
5. **`TP_InitializeApmTest` is óók niet nodig voor de normale start.** `main.cpp` regel 636-637:

   ```c
   if (argc > 1 && _wcsicmp(argv[1], L"--apm-test") == 0)
       return RunApmTestLoader(argc, argv);
   ```

   De opzoeking van `TP_InitializeApmTest` staat in `ApmTestLoader.cpp` (regel 358) en wordt dus **alleen**
   bereikt met `--apm-test` als eerste argument. `kern-abi.md` §3 concludeert dat een eigen kern die naam
   "dus moet leveren"; dat geldt alleen als iemand de APM-testmodus gebruikt. Voor de gewone start is
   `InitializeASI` voldoende — een stub voor `TP_InitializeApmTest` kost niets, maar is niet vereist.
6. **`PrepareSafeInit` blijft onbekend.** Het is een export van **OpenParrot's eigen** kern
   (`OpenParrot/src/dllmain.cpp` regel 263), niet van TeknoParrot's kern, en de laderbron zoekt hem
   **nergens** op naam op. Het commentaar bij regel 237 zegt wel *"must remain inert: the loader invokes
   PrepareSafeInit after LoadLibrary"*, maar in de hele bron staat geen aanroep — **onbekend** hoe hij dan
   aangeroepen wordt (per ordinal? door de APM-testmodus?).

**De minimale exportlijst van een eigen kern:** `InitializeASI`. Meer niet. De andere vier namen
(`TP_InitializeApmTest`, `PrepareSafeInit`, `InitLinux`, `InitKonami`) mogen als lege stub mee, maar geen
ervan is nodig voor een normale start.

**En de eerlijke keerzijde — waar het echte werk zit.** De OpenParrot-bron dekt de `WartranTroopers`- en
`RawThrillsGUN`-profielen **niet**: nul treffers in de hele bron. Voor de 21 `Type = TeknoParrot`-games moet
de per-game kennis dus **opnieuw opgebouwd** worden. `patches/<Game>/patches.ini` is daar al een begin van:
de bytepatches staan er per game in, met `Kind`, `Category`, `Module`, `Offset`, `Action`, `Source` en
`Reason` — alleen de **motor die ze toepast** ontbreekt nog. Die motor is de kern.

**Wat een eigen kern moet implementeren** (uit `LOSKOPPELEN.md` deel 1 en 2, en de kolom *Kortste route*):

| Onderdeel | Wat het is | Status van de kennis |
|---|---|---|
| De invoer uit het gedeelde geheugen | `TeknoParrot_JvsState`, 64 bytes; per bordindeling anders gelezen (RawThrillsGUN: 4 kanalen van 32 bits vanaf 12; WartranTroopers: 26 bits besturing op 8 + acht asbytes op 12-19) | **compleet** (`packaging/tp-shm-profiles.json`, `findings/06`, `07`) |
| De bytepatches per game | de `patches.ini`-regels toepassen, met de verificatie van de originele bytes en een `Version`-veld | **per game**, deels (4 TypeX2-games volledig) |
| De schijfletter-herleiding | `CreateFileA` onderscheppen, elke letter A-Z naar `.\TeknoParrot\<LETTER>\` | **compleet** (`findings/LOSKOPPELEN.md` deel 2.7) |
| De HASP-/Matrix-/regio-laag | de `hasp_*`-, `Dongle_*`- of regiowaarden beantwoorden | **compleet** voor HASP (`dongles/src/dongle.c`, zelftest slaagt) en regio; **raamwerk** voor Matrix en `sega-scan` |
| De JVS-route | het protocol voor de Sega-titels | **compleet** (`findings/07_jvs_implementation_spec.md`) |
| De abonnementscontrole | die zit in **TeknoParrot's** binaries; een eigen kern laat hem weg | **niet nodig** (`OPDRACHT-voor-de-loader-bouwer.md` §6) |

---

## De drop-in-afspraak: `patches/<Game>/` is de enige plek

`docs/dropins.md` (ontwerpafspraak, ronde 6, 2026-10-11) legt vast:

> **Alles wat één game nodig heeft, staat in `patches/<Game>/`. Een game toevoegen of bijwerken is één map
> aanraken — geen gedeeld bestand, geen globale registratie, geen tweede boom.**

De indeling die daar staat:

```
patches/<Game>/
  game.ini        instellingen van de game (bestaand)
  patches.ini     bytepatches, in het bestaande Find/Replace-formaat (bestaand)
  README.md       uitleg: wat deze game nodig heeft en waarom (bestaand)
  dongle.json     WELKE dongle deze game nodig heeft + de gegevens    <- nieuw
  dongle/         de bestanden die dongle.json noemt                  <- nieuw
  dropin/         bestanden die letterlijk in de view van de game komen <- nieuw
    dropin.ini    per bestand: waar het heen gaat en welke omgevingsvariabele erbij hoort
```

En de machineleesbare regel die eruit volgt: **`dongle.json` aanwezig = deze game vraagt een dongle; geen
bestand = geen dongle.** `kind` is één van `hasp`, `matrix`, `region` of `amdaemon`.

### Wat er nu in de mappen staat (gemeten, 2026-10-11 19:42)

`ls` op `patches/*/` (meegeleverde laag) en `~/.local/share/ravengun/patches/*/` (gebruikerslaag):

| Bestand/map | Aantal games | Welke |
|---|---|---|
| `game.ini` | **35 van 35** | alle |
| `README.md` | **27** | alle behalve 3DCosplayMahjong, HOTDSD, RainbowBombergirl, Rampage, TC5, TankTankTank, TombRaider, TransformersShadowsRising |
| `patches.ini` | **4** | GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2 (alle vier Type X2) |
| `dongle.json` | **3** | LGI, LGI3D, LGS — alle drie `"kind": "region"`, aangemaakt door een parallelle ronde **tijdens** dit onderzoek (19:42) |
| `dongle/` | **0** | — (alleen in `patches/_template/dongle/`) |
| `dropin/` | **0** | — (alleen in `patches/_template/dropin/`) |
| `[Dongle]` in `game.ini` | **0** | — (`grep -rl '^\[Dongle\]' patches/ ~/.local/share/ravengun/patches/` geeft niets) |
| `patches/_template/` | **nieuw** | de volledige nieuwe indeling: `game.ini`, `patches.ini`, `README.md`, `dongle.json`, `dongle/`, `dropin/` |

**Let op: dit is een momentopname.** Tijdens dit onderzoek zijn de `dongle.json`-bestanden van LGI, LGI3D en
LGS en de map `_template/` verschenen (een parallelle ronde vult de afspraak in). De telling hierboven is dus
de stand van 19:42; het aantal games **zonder** `dongle.json` loopt terug.

**Wat de nieuwe `dongle.json`-bestanden laten zien** (`patches/LGI/dongle.json`): ze zijn rijker dan de
afspraak minimaal eist — naast `kind`, `game` en `region`/`pcb` bevatten ze de **API die de game aanroept**
(`amDongleOpen`, `amDongleGetRegion`, `amDongleGetModelType`, …), de strings waarmee dat is aangetoond
(`"amDongleGetRegion() is failed."`, `"Region is JPN."`, `"DefaultRegion"`, `"Keychip Not Found"`), en de
cruciale vlag **`"enforced": false`** met de uitleg: *"De game vraagt de regio aan de dongle/keychip en leest
hem daarnaast uit de EEPROM en het backup-SRAM; alle drie de leesroutes falen zonder hardware en de game gaat
verder met zijn eigen DefaultRegion."*

**Dat nuanceert mijn kolom *Dongle* voor de regio-games:** de regio-laag is voor LGI/LGI3D/LGS **niet
blokkerend** — de game valt terug op zijn eigen `DefaultRegion`. Voor GaiaAttack4, HauntedMuseum,
HauntedMuseumII en RainbowBombergirl is die meting nog niet gedaan.

**En het gat blijft:** volgens de afspraak is "geen `dongle.json`" de machineleesbare betekenis van "geen
dongle". Voor de overige **16** games met een dongle-aanwijzing (6 × `hasp` — waarvan GhostBusters onzeker,
4 × `matrix`, 4 × `region`, 1 × `amdaemon`, 1 × `sega-scan`) moet dat bestand nog worden aangemaakt. Tot dat
gebeurt is de machineleesbare inventaris **niet** gelijk aan de bestandswerkelijkheid, en is mijn kolom
*Dongle* (met de bestandsscan als bewijs) de betrouwbaardere bron.

### De drie soorten werk die hieruit volgen

De afspraak maakt het werk **modulair per game** — maar niet ál het werk is per game te doen. Er zijn drie
soorten:

| Soort | Wat het is | Kan per game? | Games |
|---|---|---|---|
| **1. Per game, modulair** | `patches.ini` (bytepatches), `dongle.json` + `dongle/` (de dongle van die game), `dropin/` + `dropin.ini` (onze eigen binaire bestanden voor die game) | **ja** — één map, iedereen kan het onafhankelijk doen | 6 games hebben **alleen** dit nodig |
| **2. Eén keer, centraal: een eigen kern** | één DLL die `InitializeASI` exporteert en de hooks per `EmulationProfile` implementeert (WartranTroopers, RawThrillsGUN, FarCry, TimeCrisis5, ALLSHOTDSD, InfinityBlade, LuigisMansion, SegaJvsLetsGoIsland, TransformersShadowsRising) | **nee** — het is één DLL met de kennis van alle games; dat is precies waarom TeknoParrot's kern 56 MB is en de onze 1,9 MB | **21 games** |
| **3. Eén keer, centraal: een eigen lader/preload** | de `LD_PRELOAD`-kastnabootser voor de Linux-ELF-games (64-bits familie: plthook + Capstone; 32-bits familie: de vaste GOT herschrijven + `dlsym` onderscheppen) | **nee** — één bibliotheek per bitfamilie | **8 games** |

**De eerlijke kernvraag die hierbij hoort.** Voor de 21 TeknoParrot-type games is de **kern** de blokkade, en
die kern is **niet per game te vervangen door een drop-in**: het is één DLL met de kennis van alle games. Dus
de loskoppeling kent twee soorten werk die niet in elkaar te schuiven zijn:

1. **Per game, modulair** (patches, dongle, drop-ins) — dat kan iedereen onafhankelijk doen, en dat is wat
   `docs/dropins.md` wil. Het maakt een game **compleet** zodra de centrale onderdelen er zijn.
2. **Eén keer, centraal** (een eigen kern die `InitializeASI` exporteert en de `WartranTroopers`-/
   `RawThrillsGUN`-hooks implementeert) — dat kan **niet** per game. Zonder dit blijft de map van een
   TeknoParrot-type game een beschrijving van wat er zou moeten gebeuren, niet een werkende game.

**Welke games hebben alleen werksoort 1 nodig (en zijn dus nu al modulair af te maken):**

| Game | Waarom alleen werksoort 1 |
|---|---|
| GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2 | lader **en** kern zijn al van ons (eigen OpenParrot-build); wat rest is de regio-dongle in `dongle.json` en de patches (die er al staan) |
| TombRaider | loopt via de eigen Unity-plugin; alleen een Matrix-`dropin/` ontbreekt (en blokkeert niet) |
| RabbidsHollywood | loopt via de eigen Unity-plugin; alleen een Matrix-`dropin/` ontbreekt — voorheen gemeld als **blokkerend** ("stopt na ~1 min"), maar dat bleek een uitrol-fout; hermeting 2026-10-12: start en leeft 288 s in attract, zie `packaging/games-status.md` |

**En welke ook werksoort 2 of 3 nodig hebben:** de andere **29** games. Daarvan zijn er 2 niet geïnstalleerd
(3DCosplayMahjong, TankTankTank), zodat de verdeling is: **21 × werksoort 2** (eigen kern), **8 × werksoort 3**
(eigen lader/preload), **6 × alleen werksoort 1**.

**En het goede nieuws voor werksoort 2:** de bytepatches die de kern moet toepassen, staan **al per game in de
map** — voor 4 games (de Type X2-titels) zelfs volledig, met `Kind`, `Category`, `Module`, `Offset`, `Action`,
`Source` en `Reason`. Alleen de **motor die ze toepast** ontbreekt. Dat is precies de scheiding die de
afspraak wil: de kennis per game in de map, de motor één keer centraal.

---

## De tabel — alle 35 games

**Kolonnen**

* **Type** — `[Game] Type` uit `patches/<Game>/game.ini`, met het `EmulationProfile` erachter.
* **Laderfamilie** — uit `resolve_loader()` in `scripts/launch-direct.py` (regel 315) en de gemeten `argv`.
  *OpenParrot* = `OpenParrotLoader(64).exe`; *BudgieLoader* = `ElfLdr2/BudgieLoader(_x64).exe`;
  *TeknoMacaw* = `TeknoMacaw(64).exe`. Voor de **eigen** lader staat er `eigen`.
* **Dongle** — `geen`, `HASP`, `Matrix/SBK`, `regio/PCB`, `twee-exe (amdaemon)` of `Sega-donglescan`.
  Tussen haakjes het concrete bewijs.
* **Draait nu mee** — wat er bij een start nog van TeknoParrot meedraait: **lader**, **kern**, **profielenset**
  (`GameProfiles/`/`UserProfiles/`), **Ui** (TeknoParrotUi als startproces), **prefix** (de Wine-prefix met de
  TeknoParrot-activering), **pipehelper**.
* **Kortste route** — de kleinste stap naar los.
* **Blokkade** — wat er nu in de weg staat, met bewijs.

### De tabel

| # | Game | Type (game.ini) | Laderfamilie | Dongle | Draait nu mee | Kortste route | Blokkade |
|---|---|---|---|---|---|---|---|
| 1 | **3DCosplayMahjong** | `TeknoParrot` / `TaitoTypeXGeneric` | OpenParrot + TeknoParrot-kern | geen aanwijzing (profiel heeft geen `DongleRegion`/`PcbRegion`/`Region`-veld) — spel niet geïnstalleerd | n.v.t.: `--plan` exit 4 "game niet gevonden" | onbekend (spel niet aanwezig) | Spel staat niet op de NAS: `ls /volume2/emulation/Teknoparrot/*Cosplay*` geeft niets |
| 2 | **GaiaAttack4** | `OpenParrot` / `GaiaAttack4` | **eigen** OpenParrotLoader | **regio/PCB (E)** — `MINIGUN.INI` `REGION E`; `teknoparrot.ini` `[General] Region=E`; profielveld `Region` (`GameProfiles/GaiaAttack4.xml` regel 52) | **eigen lader + eigen kern**; profielenset (profielrevisie 12); pipehelper; prefix | **kan al los van de gesloten kern**; alleen `pipehelper.exe` en de prefix nog | Zwart beeld na de JVS-featurecheck (`handle_syscall_fault code=c0000005`); zie `findings/GaiaAttack4.md` |
| 3 | **GalagaAssault** | `ElfLdr2` / `RawThrillsGUN` | BudgieLoader (gesloten, uit de view) | geen (geen donglebestand in de spelmap; alleen `SDL2.dll`, `data/`, `game`) | gesloten **lader**; profielenset; prefix; gamescope | eigen `LD_PRELOAD`-bibliotheek (32-bit: vaste GOT + `dlsym`-onderschepping) — Johan raadt Galaga als **startpunt** aan (`antwoorden_zonder_teknoparrot.md`) | BudgieLoader is gesloten; native SIGSEGV vóór `main` (sprong naar een vast adres uit de dump) |
| 4 | **Gashaaaan** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader | geen (geen donglebestand in `Gashaaaan!!!!`) | eigen lader + **gesloten kern**; profielenset; prefix; pipehelper; Ui (geen `[Launch] Mode`) | eigen kern met WartranTroopers-hooks | Gesloten `TeknoParrot.dll`; 32-bit loader-lock / exitcode `0xDEADC0DE` (`packaging/games-status.md`, zesde ronde) |
| 5 | **Gashaaaan2** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader | geen | als #4, **plus de abonnementscontrole** (`needs_activation: true` in `--plan`) | als #4 | Gesloten kern + `Patreon`-activering in de prefix |
| 6 | **GhostBusters** | `ElfLdr2` / `RawThrillsGUN` | BudgieLoader (gesloten, uit de view) | **onbekend / waarschijnlijk HASP** — de lader meldt `GAME PATH CHECK FAILED`; native meldt `Game dongle initialization failed`; de dump mist het bestand dat de lader bij herkende games verwacht (bij Halo `fff4`, bij Terminator `hasp/`); alleen `utils/splash/dongle-1024x768.cfg` (een splash-afbeelding) | gesloten lader; profielenset; prefix; gamescope | donglebestand achterhalen, dan als #3 | BudgieLoader gesloten + `GAME PATH CHECK FAILED` (`findings/GhostBusters.md`) |
| 7 | **GoGoStrike** | `ElfLdr2` / `RawThrillsGUN` (`Patreon = true`) | BudgieLoader (gesloten, uit de view) | geen (geen donglebestand in `Go Go Strike`) | gesloten lader; profielenset; prefix; gamescope; **abonnement** | eigen `LD_PRELOAD` (64-bit familie) | BudgieLoader gesloten + `Patreon` |
| 8 | **HOTDSD** | `TeknoParrot` / `ALLSHOTDSD` (`HasTwoExecutables = true`) | OpenParrotLoader64 + TeknoParrot64 | **twee-exe (amdaemon)** — `Executable2 = amdaemon.exe`, `HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true` in `game.ini`; `App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe` op de NAS | **Ui** (`[Launch] Mode = tpui`), lader, gesloten kern, profielenset, prefix, `pipehelper` + `TeknoParrotPipe` | eigen startketen met twee exe's (`amdaemon` eerst, 5 s wachten) + `minime` als open vervanging van Sega's daemon | `launch-direct.py` weigert twee exe's (exit 4: "profielen met twee exe's worden niet direct gestart"); start en test gaan via de Ui-pipes (`findings/HOTDScarletDawn.md`) |
| 9 | **Halo** | `ElfLdr2` / `WartranTroopers` (`Patreon = true`) | BudgieLoader_x64 (gesloten, uit de runtime) | **HASP** — `Halo Fireteam/g7/halo/fff4` (112 bytes) op de NAS; `dongles/halo-fireteam/{dongle.json,memory/}` in deze werkmap; `~/.local/share/ravengun/hasp/Halo Fireteam/` met de gebouwde `ravengun-hasp*.so` | gesloten **lader**; profielenset; prefix; **abonnement** | eigen `LD_PRELOAD`-kastnabootser (`halo.so` vervangen) + de **al gebouwde** eigen HASP-laag | BudgieLoader gesloten; native vraagt het FTDI-/RIO2-bord en FMOD 1.10 (`packaging/native-games.json`) |
| 10 | **HauntedMuseum** | `OpenParrot` / `HauntedMuseum` | **eigen** OpenParrotLoader | **regio/PCB (E)** — `MUSEUM.INI` `REGION E`; `teknoparrot.ini` `[General] Region=E`; profielveld `Region` | eigen lader + eigen kern; profielenset; pipehelper; prefix | kan al los van de gesloten kern | Zwart beeld na de JVS-featurecheck; dezelfde Wine-MXCSR-crash is met de eigen lader (broncodewijziging) omzeild |
| 11 | **HauntedMuseumII** | `OpenParrot` / `HauntedMuseum2` | **eigen** OpenParrotLoader | **regio/PCB (E)** — `HAUNTED2.INI` `REGION E`; `teknoparrot.ini` `[General] Region=E`; profielveld `Region` | als #10 | als #10 | als #10 |
| 12 | **InfinityBladeFX** | `TeknoParrot` / `InfinityBlade` | eigen OpenParrotLoader | **Matrix/SBK** — `Infinity Blade FX/Binaries/Win32/matrix32.dll` + `matrix32.TEKNOPARROT.dll`; `readme.txt` beschrijft de matrix32/TPFIX-oplossing; `TPFIX.ini` staat er | eigen lader + **gesloten kern**; profielenset; prefix; pipehelper; Ui | eigen kern; daarna de Matrix-laag (27 `Dongle_*`-functies) | Gesloten kern; `Kaboom: InitLib() failed` (`KaboomComDll.dll`), daarna wit of zwart beeld |
| 13 | **JurassicPark** | `ElfLdr2` / `RawThrillsGUN` | BudgieLoader (gesloten, uit de view) | **HASP** — `Jurassic Park/utils/pmrtdata/libhasp_linux_*.so` (8 versies: `5176`, `93341`, `93898`, `96728`, `98111`, `100624`, `112983`, `114576`) | gesloten lader; profielenset; prefix | eigen `LD_PRELOAD` (32-bit: vaste GOT + `dlsym` + eigen HASP-laag) | BudgieLoader gesloten; 32-bit GOT: 58–60 slots met vaste `0xb7xxxxxx`-adressen en de PLT-indexen zijn door de pakker gewist (`docs/research/antwoorden-tp-ronde23-en-jp.md`) |
| 14 | **LGI** | `TeknoParrot` / `SegaJvsLetsGoIsland` (`DongleRegion = JAPAN`, `PcbRegion = JAPAN`) | eigen OpenParrotLoader | **regio/PCB** — `game.ini` `DongleRegion = JAPAN` + `PcbRegion = JAPAN`; `teknoparrot.ini` `[General] DongleRegion=JAPAN` / `PcbRegion=JAPAN`; profielvelden `DongleRegion`/`PcbRegion` | eigen lader + **gesloten kern**; profielenset; pipehelper; eigen prefix (`ravengun-test-lgi`) | eigen kern + eigen regio-laag (`dongles/lets-go-safari/dongle.json` is het voorbeeld) | Gesloten kern; speelt wel al via de directe start |
| 15 | **LGI3D** | `TeknoParrot` / `SegaJvsLetsGoIsland` (`USA`/`USA`) | eigen OpenParrotLoader | **regio/PCB (USA)** — als #14, met `USA` | als #14, prefix `ravengun-test-lgi3d` | als #14 | als #14 |
| 16 | **LGS** | `TeknoParrot` / `LGS` (`USA`/`USA`) | eigen OpenParrotLoader | **regio/PCB (USA)** — als #14, met `USA` | eigen lader + **gesloten kern**; profielenset; pipehelper; prefix `ravengun-test-lgs`; Ui (geen `[Launch] Mode`) | als #14; Let's Go Safari is geen lightgun-game (stuurwiel) | Gesloten kern; ook de JVS-dienst op poort 40100 (`findings/LetsGoSafari.md`) |
| 17 | **LethalEnforcers3** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader | geen (geen donglebestand; `gunio.dll` en `iob.dll` zijn I/O-modules, geen dongle) | eigen lader + **gesloten kern**; profielenset; prefix; Ui | eigen kern met WartranTroopers-hooks | Gesloten kern; de game crasht op een `.m1v`-film — Wine mist 32-bit GStreamer (MPEG-1) |
| 18 | **LuigisMansion** | `TeknoParrot` / `LuigisMansion` | eigen OpenParrotLoader64 | geen (geen donglebestand in `Luigi's Mansion Arcade`) | eigen lader + **gesloten kern**; profielenset; prefix; `TP_GAME_WORKING_DIRECTORY` | eigen kern; de gesloten kern heeft een COM-tak en neemt de I/O over. De COM-blokkade is op onze kern nagebouwd en op 2026-10-12 weer weggehaald: gemeten neutraal, het spel eindigt voordat het een poort opent (`third_party/openparrot/README.md`, patch 0005) | Gesloten kern; Capcom-packer op `VACUUM.exe`; de kern doet `BLOCKING LUIGI COM PORT` (`findings/14_openstellen.md` §4.4) |
| 19 | **MedaruNoGunman** | `TeknoParrot` / `FarCry` | eigen OpenParrotLoader | geen — `keychip/` bevat alleen `SBRZ_Table.dat` en `SBTF_Table.dat` (tabeldata, geen dongle-API) | eigen lader + **gesloten kern**; profielenset; prefix; eigen prefix | eigen kern met FarCry-hooks | Gesloten kern; bij elke verse start `3001 RAM DATA IS BAD` / `3003 ROM HAS CHANGED` (SRAM/EEPROM-grootte) |
| 20 | **MusicGunGun2** | `OpenParrot` / `MusicGunGun2` | **eigen** OpenParrotLoader | geen (geen donglebestand; `SECLIB.dll`/`ewfapi.dll` zijn geen dongle) | eigen lader + eigen kern; profielenset; pipehelper; prefix | kan al los van de gesloten kern | Game-eigen D3D9-probleem: eigen foutvenster "Direct3D-initialisatie mislukt" (ook met DXVK) |
| 21 | **NERF** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader64 | **HASP** — `NERF Arcade/Plugins/x86_64/libhasp_linux_23714.so` (en `Nerf_Data/Plugins/x86_64_ori/libhasp_linux_23714.so`) | eigen lader + **gesloten kern**; profielenset; prefix `ravengun-test-dxvk`; **abonnement** | eigen kern + eigen HASP-laag; de BepInEx-plugin-route (`NERFTPPlugin.dll`) is een alternatief | Gesloten kern; invoer komt niet aan (start en trekker); `Patreon` |
| 22 | **OmatsuriYasanGansoTakoyaki** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader | geen | eigen lader + **gesloten kern**; profielenset; prefix; Ui; **abonnement** | eigen kern met WartranTroopers-hooks | Gesloten kern; wit venster, geen D3D-apparaat; `rqrdlist.rqr` via de schijfletter-herleiding; `Patreon` |
| 23 | **OmatsuriYasanKingyoSukui** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader | geen | als #22 | als #22 | Gesloten kern; exitcode 8 nadat de game alle schijfletters op `rqrdlist.rqr` en `campaign*.bmp` heeft doorzocht; `Patreon` |
| 24 | **Otomedius** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader | geen (`libacio.dll`, `HIdGet.exe`, `HidBoot.id` zijn I/O, geen dongle) | eigen lader + **gesloten kern**; profielenset; prefix; Ui; **abonnement** | eigen kern + een ACIO-responder op een pty als COM2 (`src/ravengun/iobridge/`, protocol in `docs/research/antwoorden-tp-ronde5-22.md`) | Gesloten kern; de game zoekt een ACIO-micom op COM2 ("COM Port 2 Open Error"); `Patreon` |
| 25 | **PacmanChomp** | `ElfLdr2` / `RawThrillsGUN` (`Patreon = true`) | BudgieLoader (gesloten, uit de view) | geen (geen donglebestand in `Pacman Chomp-Mania`) | gesloten lader; profielenset; prefix; gamescope; **abonnement** | eigen `LD_PRELOAD` (32-bit) | BudgieLoader gesloten; zwart beeld, dezelfde klasse als Jurassic Park |
| 26 | **PigsMightFly** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader | geen | eigen lader + **gesloten kern**; profielenset; prefix `ravengun-test-pigs`; **abonnement** | eigen kern; daarna de packer op `Shell.exe` | Gesloten kern; `Shell.exe` is ingepakt (injectie vanaf het startpunt geeft "Unhandled illegal instruction at address 0044A5F4") |
| 27 | **RabbidsHollywood** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader64 | **Matrix/SBK** — `matrix64.dll` en `matrix32.dll` in de spelmap (en in `Game_Data/Plugins/`); `SBK.Matrix.MatrixManager` roept `Dongle_Count(40)` aan en bij 0 volgt `DONGLE NOT FOUND` + `Application.Quit` | **niets van TeknoParrot in het standaardpad**: `play-halo.sh` stuurt het profiel naar `scripts/play-unity.sh` (eigen BepInEx-plugin, TCP 33610). De TeknoParrot-route bestaat nog als terugval | **kan al los** — de eigen plugin draait; alleen de Matrix-laag (27 `Dongle_*`-functies) ontbreekt | Voorheen gemeld: de **eigen** Matrix-donglecontrole van de game, hij stopt na ongeveer een minuut (`findings/MATRIX-dongle-API.md`). Hermeting 2026-10-12: dat was een uitrol-fout (verouderde plug-in in de view); de game leeft 288 s in de attract-loop |
| 28 | **RaccoonRampage** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader64 | geen | eigen lader + **gesloten kern**; profielenset; prefix `ravengun-test-dxvk`; Ui (geen `[Launch] Mode`) | eigen kern met WartranTroopers-hooks | Gesloten kern; de game meldt "guns are not calibrated" en het vizier staat vast linksboven |
| 29 | **RainbowBombergirl** | `TeknoParrot` / `WartranTroopers` (`Patreon = true`) | eigen OpenParrotLoader64 | **regio/PCB (EXPORT)** — `teknoparrot.ini` `[General] Region=EXPORT`; profielveld `Region` (`GameProfiles/RainbowBombergirl.xml` regel 68) | eigen lader + **gesloten kern**; profielenset; prefix `ravengun-test-rainbowbombergirl`; **abonnement** | eigen kern + eigen regio-laag | Gesloten kern; de game komt niet verder dan het begin (aanraakscherm linksonder); `Patreon` |
| 30 | **Rampage** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader64 | **Matrix/SBK** — `matrix32.dll` en `matrix64.dll` in de spelmap en in `Game_Data/Plugins/`; `MATRIX_NO_DONGLE_FOUND` in `Assembly-CSharp.dll`; de game zoekt `C:\windows\SYSTEM\MATRIX.INI` (ontbreekt) | eigen lader + **gesloten kern**; profielenset; prefix `ravengun-test-dxvk` | eigen kern (de Matrix-controle blokkeert hier **niet**: de game speelt door zonder dongle) | Gesloten kern; geen echte blokkade — de game start en speelt; wel een filmprobleem (AAC-decoder, opgelost met `GST_PLUGIN_PATH`) |
| 31 | **TC5** | `TeknoParrot` / `TimeCrisis5` | **runtime** OpenParrotLoader64 (`[Loader] Build = teknoparrot`) | **HASP** — `Time Crisis 5/TC5/Binaries/Win64/hasp_windows_x64_105293.dll` (en `_106993.dll`, plus een kopie in de wortel) | **runtime lader** + gesloten kern; profielenset; prefix; pipehelper; Ui (geen `[Launch] Mode`) | eigen lader (vereist `bngrw.dll` en `iDmacDrv64.dll` in de eigen build) + eigen kern + eigen HASP-laag | `[Loader] Build = teknoparrot` omdat `bngrw.dll`/`iDmacDrv64.dll` nog niet in de eigen OpenParrot-build zitten; gesloten kern; `wajvio.dll` als I/O |
| 32 | **TankTankTank** | `ElfLdr2` / `DeadHeat` | BudgieLoader | geen aanwijzing (profiel heeft geen `DongleRegion`/`PcbRegion`/`Region`-veld) — spel niet geïnstalleerd | n.v.t.: `--plan` exit 4 "game niet gevonden" | onbekend (spel niet aanwezig) | Spel staat niet op de NAS: `ls /volume2/emulation/Teknoparrot/*Tank*` geeft niets |
| 33 | **Terminator** | `ElfLdr2` / `RawThrillsGUN` | BudgieLoader (gesloten, uit de view) | **HASP** — `Terminator Salvation /hasp/` met **5413** bestanden (namen van 32 en 64 hextekens) + `ServiceDongleApp/ServiceDongleApp` | gesloten lader; profielenset; prefix; gamescope | eigen `LD_PRELOAD` + eigen HASP-laag; de `hasp/`-vorm moet nog worden uitgezocht (ander opslagpatroon dan de fileid-naam) | BudgieLoader gesloten; de `hasp/`-map met hash-namen is niet uitgezocht (`dongles/README.md` §5) |
| 34 | **TombRaider** | `TeknoParrot` / `WartranTroopers` | eigen OpenParrotLoader64 | **Matrix/SBK** — `Tomb Raider/matrix64.dll` (en `matrix32.dll`, plus kopieën in `Game_Data/Plugins/`) | **niets van TeknoParrot in het standaardpad**: `play-halo.sh` stuurt het profiel naar `scripts/play-unity.sh` (eigen BepInEx-plugin) | **kan al los** | Geen blokkade meer: lightgun, herladen, munt, start en trillen werken; alleen nog niet met de echte Move-knoppen gespeeld. (`Sixences/Matrix_Files/*.mtx` is de kalibratie van het Sixence-apparaat, **geen** Matrix-dongle) |
| 35 | **TransformersShadowsRising** | `TeknoParrot` / `TransformersShadowsRising` | eigen OpenParrotLoader | **Sega-donglescan (sega-scan)** — `Sega/shell/dk2win64.dll`, `dk2win32.dll` en `dk2inst.dll` (Dinkey Pro) en `Sega/shell/Dongle - 261011.log` met `DONGLE : MainThreadDongleScan: failed stage 1` | eigen lader + **gesloten kern** (alleen in `Shell.exe`); profielenset; prefix `ravengun-test-tsr` | eigen kern + een `sega-scan`-donglelaag (`dongles/README.md` type 3 — nog een raamwerk) | Gesloten kern; het dongle-type `sega-scan` is een raamwerk: alleen bekend dat fase 1 faalt |

---

### De drop-in-kaart per game — wat staat er al, en wat ontbreekt nog

Twee kolommen uit de vorige tabel zijn hier uitgesplitst naar de map: **wat er nu in `patches/<Game>/`
staat** en **wat er nog moet**. `ini` = `game.ini`; `patch` = `patches.ini`; `readme` = `README.md`.
`dongle.json` en `dropin/` staan **nergens** — dat is de hele kolom *Ontbreekt*.

| Game | Map nu | `dongle.json` (`kind`) | `dropin/` nodig | Ontbreekt nog |
|---|---|---|---|---|
| **3DCosplayMahjong** | `ini` | onbekend (spel niet aanwezig) | onbekend | het spel zelf; daarna meten |
| **GaiaAttack4** | `ini` + `patch` + `readme` | `region` (E) | nee | `dongle.json` + `dongle/`; de game-eigen zwarte beeld |
| **GalagaAssault** | `ini` + `readme` | geen | ja: eigen `LD_PRELOAD` (32-bit) | werksoort 3 (eigen lader/preload) |
| **Gashaaaan** | `ini` + `readme` | geen | nee | werksoort 2 (eigen kern) |
| **Gashaaaan2** | `ini` + `readme` | geen | nee | werksoort 2 + de `Patreon`-activering |
| **GhostBusters** | `ini` + `readme` | `hasp` (onbekend welk bestand) | ja: `LD_PRELOAD` (32-bit) + `ravengun-hasp` | het donglebestand achterhalen; werksoort 3 |
| **GoGoStrike** | `ini` + `readme` | geen | ja: eigen `LD_PRELOAD` (64-bit) | werksoort 3 + de `Patreon`-activering |
| **HOTDSD** | `ini` | `amdaemon` | nee | de startketen met twee exe's; werksoort 2 |
| **Halo** | `ini` + `readme` | `hasp` | ja: `LD_PRELOAD` (64-bit) + `ravengun-hasp` | werksoort 3; de HASP-gegevens staan al in `dongles/halo-fireteam/` en `~/.local/share/ravengun/hasp/` |
| **HauntedMuseum** | `ini` + `patch` + `readme` | `region` (E) | nee | `dongle.json` + `dongle/`; de game-eigen zwarte beeld |
| **HauntedMuseumII** | `ini` + `patch` + `readme` | `region` (E) | nee | als HauntedMuseum |
| **InfinityBladeFX** | `ini` + `readme` | `matrix` | ja: eigen `matrix32.dll` | werksoort 2; daarna de Matrix-drop-in |
| **JurassicPark** | `ini` + `readme` | `hasp` | ja: `LD_PRELOAD` (32-bit) + `ravengun-hasp` | werksoort 3; de HASP-gegevens uit `utils/pmrtdata/` |
| **LGI** | `ini` + `readme` + **`dongle.json`** | `region` (JAPAN/JAPAN) — **aanwezig** | nee | werksoort 2; de regio-laag is `enforced: false`, dus niet blokkerend |
| **LGI3D** | `ini` + `readme` + **`dongle.json`** | `region` (USA/USA) — **aanwezig** | nee | als LGI |
| **LGS** | `ini` + `readme` + **`dongle.json`** | `region` (USA/USA) — **aanwezig** | nee | werksoort 2 + de dienst op poort 40100 |
| **LethalEnforcers3** | `ini` + `readme` | geen | nee | werksoort 2 + 32-bit GStreamer (MPEG-1) |
| **LuigisMansion** | `ini` + `readme` | geen | nee | werksoort 2; het COM1-blokkeerpatroon is gemeten neutraal en vervallen (2026-10-12), het vroege einde (`0x40415820`) staat nog open |
| **MedaruNoGunman** | `ini` + `readme` | geen | nee | werksoort 2 + de SRAM-grootte |
| **MusicGunGun2** | `ini` + `patch` + `readme` | geen | nee | alleen de game-eigen D3D9-fout (werksoort 1 is klaar) |
| **NERF** | `ini` + `readme` | `hasp` | ja: `ravengun-hasp` (en de BepInEx-plugin `NERFTPPlugin.dll`) | werksoort 2 + de HASP-gegevens uit `Plugins/x86_64/` |
| **OmatsuriYasanGansoTakoyaki** | `ini` + `readme` | geen | nee | werksoort 2 + D3D + `rqrdlist.rqr` via de schijfletter-herleiding |
| **OmatsuriYasanKingyoSukui** | `ini` + `readme` | geen | nee | werksoort 2 + de schijfletter-herleiding |
| **Otomedius** | `ini` + `readme` | geen | ja: een ACIO-responder op een pty als COM2 | werksoort 2 + de ACIO-responder |
| **PacmanChomp** | `ini` + `readme` | geen | ja: eigen `LD_PRELOAD` (32-bit) | werksoort 3 + de `Patreon`-activering |
| **PigsMightFly** | `ini` + `readme` | geen | nee | werksoort 2 + de packer op `Shell.exe` |
| **RabbidsHollywood** | `ini` + `readme` | `matrix` | **ja: eigen `matrix64.dll`** | **alleen dat** — de game loopt al via de eigen Unity-plugin |
| **RaccoonRampage** | `ini` + `readme` | geen | nee | werksoort 2 + de kalibratie van het vizier |
| **RainbowBombergirl** | `ini` | `region` (EXPORT) | nee | werksoort 2 + het aanraakscherm |
| **Rampage** | `ini` | `matrix` | ja: eigen `matrix64.dll` | werksoort 2 (de Matrix-controle blokkeert hier niet) |
| **TC5** | `ini` | `hasp` | ja: `ravengun-hasp` | werksoort 2 **en** werksoort 3: de eigen lader mist `bngrw.dll` en `iDmacDrv64.dll` |
| **TankTankTank** | `ini` | onbekend (spel niet aanwezig) | onbekend | het spel zelf; daarna meten |
| **Terminator** | `ini` + `readme` | `hasp` | ja: `LD_PRELOAD` (32-bit) + `ravengun-hasp` | werksoort 3 + de `hasp/`-vorm (5413 hash-namen) uitzoeken |
| **TombRaider** | `ini` | `matrix` | ja: eigen `matrix64.dll` | **alleen dat** (en het blokkeert niet) — de game loopt al via de eigen Unity-plugin |
| **TransformersShadowsRising** | `ini` | **`sega-scan` — staat niet in `docs/dropins.md`** | ja: de eigen `Shell.exe`-drop-in | werksoort 2 + een `kind` voor de Sega-donglescan |

**Wat deze tabel laat zien:**

* **`dongle.json` staat er bij 3 van de 19 games** (LGI, LGI3D, LGS — tijdens dit onderzoek aangemaakt).
  Voor de andere **16** moet hij nog worden gemaakt (6 × `hasp` — waarvan GhostBusters onzeker, 4 × `matrix`,
  4 × `region`, 1 × `amdaemon`, 1 × `sega-scan`); voor de resterende 16 games is de afwezigheid correct
  (geen dongle).
* **`dropin/` is nodig bij 15 games**, en bij 2 daarvan (Rabbids Hollywood, Tomb Raider) is het de **enige**
  stap: die twee zijn al los van TeknoParrot en missen alleen onze eigen Matrix-DLL.
* **Bij 21 games is `dropin/` niet de oplossing**: die wachten op werksoort 2 (de eigen kern), en dat is
  centrale arbeid, geen map.
* **`sega-scan` past niet in de afspraak.** `docs/dropins.md` noemt vier `kind`-waarden (`hasp`, `matrix`,
  `region`, `amdaemon`); Transformers heeft een vijfde nodig (`sega-scan`, de Dinkey-donglescan van de
  Sega-shell). Zie open vraag 1.

---

## Per kolom: hoe de waarden zijn bepaald

### 1. Laderfamilie en het `[Game] Type`

De drie laderfamilies uit `19_loader_analysis.md` §1 (`GameSession::ResolveLoader`):

| `[Game] Type` / `EmulatorType` | Hulpprogramma | Games in onze lijst |
|---|---|---|
| `OpenParrot` (0) | `OpenParrotWin32/OpenParrot` als **argument** van `OpenParrotLoader.exe` | 4 (GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2) |
| `TeknoParrot` (2, 7, 8, 9) | `OpenParrotLoader(64).exe` **met de core als argument** | 21 (alle `Type = TeknoParrot`, inclusief TC5) |
| `ElfLdr2` (6) | `ElfLdr2/BudgieLoader.exe` of `x64/BudgieLoader_x64.exe` | 8 (GalagaAssault, GhostBusters, GoGoStrike, Halo, JurassicPark, PacmanChomp, TankTankTank, Terminator) |
| `TeknoMacaw` (10) | `TeknoMacaw(64).exe` | **0** — geen van de 35 games |
| `Lindbergh` (1) / `N2` (3) | `TeknoParrot/BudgieLoader.exe` / `N2/BudgieLoader.exe` | **0** |

**Geen enkele game in de lijst gebruikt TeknoMacaw of de Lindbergh/N2-variant van BudgieLoader.** De
"drie laderfamilies" komen in deze 35 games dus neer op twee: **OpenParrotLoader (25 games)** en
**BudgieLoader (8 games)**; 2 games zijn niet geïnstalleerd.

Waar de lader vandaan komt, is per game af te lezen aan de `argv` uit `--plan`:

* `~/.local/share/ravengun/openparrot/current/...` = **eigen build** (24 games; `_defaults/TeknoParrot.ini`
  en `_defaults/OpenParrot.ini` zetten `[Loader] Build = ravengun`).
* `~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/...` = **uit de runtime** (8 games:
  de 7 BudgieLoader-games en TC5). TC5 wijkt bewust af: `patches/TC5/game.ini` zet
  `[Loader] Build = teknoparrot` omdat `bngrw.dll` en `iDmacDrv64.dll` nog niet in de eigen build zitten.

### 2. Donglesoort — de zes soorten en hun bewijs

Uit `findings/MATRIX-dongle-API.md` §5 en `dongles/README.md` §2:

| Soort | API | Games in onze lijst | Concreet bewijs |
|---|---|---|---|
| **HASP** (`hasp-hl`) | 10 `hasp_*`-functies; bestandsnaam = 32-bits binaire fileid | **6** (waarvan GhostBusters onzeker) | Halo `g7/halo/fff4`; Jurassic Park `utils/pmrtdata/libhasp_linux_*.so` ×8; NERF `Plugins/x86_64/libhasp_linux_23714.so`; TC5 `hasp_windows_x64_105293.dll`; Terminator `hasp/` (5413 bestanden); GhostBusters: het bestand ontbreekt (zie rij 6 van de tabel) |
| **Matrix/SBK** | 27 `Dongle_*`-functies; `Dongle_Count(short)` beslist | **4** | InfinityBladeFX `matrix32.dll`; Rabbids `matrix64.dll`; Rampage `matrix64.dll`; Tomb Raider `matrix64.dll` |
| **regio/PCB** (`region-pcb`) | geen API — vaste waarden | **7** | LGI/LGI3D/LGS `DongleRegion`/`PcbRegion` in `game.ini` + `teknoparrot.ini`; GaiaAttack4/HM/HMII `Region=E` in `teknoparrot.ini` + `MINIGUN.INI`/`MUSEUM.INI`/`HAUNTED2.INI`; RainbowBombergirl `Region=EXPORT` |
| **twee-exe (`amdaemon`)** | Sega's AM Daemon moet **vóór** de game draaien | **1** | HOTDSD `Executable2 = amdaemon.exe`, `HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true`; `AMDaemon/amdaemon.exe` op de NAS |
| **Sega-donglescan** (`sega-scan`) | getrapte scan van de Sega-shell | **1** | Transformers `Sega/shell/dk2win64.dll` + `Dongle - 261011.log`: `MainThreadDongleScan: failed stage 1` |
| **geen aanwijzing** | — | **16** | geen donglebestand in de spelmap, geen dongle-API-string, geen `DongleRegion`/`PcbRegion`/`Region`-veld in het profiel, en geen `[Dongle]`-sectie in enige `game.ini` |

**Twee dingen om te weten bij deze kolom:**

1. **`[Dongle]` staat in géén enkele `game.ini`.** `grep '^\[Dongle\]' patches/*/game.ini` (én de
   gebruikerslaag in `~/.local/share/ravengun/patches/`) geeft nul treffers. In elke `--plan`-uitvoer staat
   daarom `"dongle": {"enabled": false, ...}`. De dongle-poort van `launch-direct.py` (`dongle_gate()`,
   exitcode 6) staat dus **overal uit**; de dongle-afhandeling gebeurt in de praktijk nog binnen de gesloten
   kern of in de game zelf.
2. **`regio/PCB` is geen USB-sleutel maar een configuratiewaarde.** `12_aanpassingen_en_koppelingen.md` §7
   zegt het letterlijk: *"Dit zijn software-dongle/PIC-emulaties: de game leest een regio-/PCB-identifier en
   TeknoParrot geeft de gekozen waarde terug via het I/O-board (JVS-identifier of geheugenoffset). Er is geen
   fysieke dongle nodig."* Voor GaiaAttack4/HM/HMII is het veld bovendien `Region` (een TypeX-waarde `E`), niet
   `DongleRegion`; de inventaris noemt het toch "regio(E)". Ik houd die groepering aan en noem het veld erbij.

### 3. Welke TeknoParrot-onderdelen nu nog meedraaien

`scripts/launch-direct.py` neemt het grootste deel van TeknoParrotUi over (zie de kop van dat bestand,
punten 1-7). Wat er per game nog van TeknoParrot overblijft:

| Onderdeel | Wanneer het nog meedraait | Bewijs |
|---|---|---|
| **De Ui** (TeknoParrotUi, Avalonia) | als `[Launch] Mode` **niet** `direct` is: `play-halo.sh` regel 225 zet `LAUNCH=teknoparrot-ui` als er geen `Mode` staat (volgorde: `--direct`/`--tpui`, dan `[Launch] Mode`, dan de Ui) | **16** games hebben `Mode = direct`; **18** hebben geen `Mode`; HOTDSD heeft `Mode = tpui` |
| **De lader** | altijd (er is geen game zonder lader) — maar voor 24 games is het de **eigen** build | `--plan` `argv[1]` |
| **De kern** | altijd bij `Type = TeknoParrot` en `Type = ElfLdr2`; voor de 4 `OpenParrot`-games is het de **eigen** `OpenParrot(64).dll` | `--plan` `argv[2]`; `own_build()` regel 340 |
| **De profielenset** (`GameProfiles/`, `UserProfiles/`) | altijd: `launch-direct.py` leest het sjabloon `GameProfiles/<P>.xml` voor de revisiecontrole, de `Patreon`-vlag en de velden die `game.ini` niet noemt | `load_own_profile()` regel 169; `patreon_in_layers()`; `--plan` `settings_sources` |
| **De Wine-prefix met de activering** | zolang de gesloten kern nodig is: `check_activation()` regel 573 eist `PatreonSerialKey` in `user.reg` van de prefix voor profielen met `Patreon = true` | `--plan` `needs_activation`; 10 games `true` (Gashaaaan2, GoGoStrike, Halo, NERF, Omatsuri ×2, Otomedius, PacmanChomp, PigsMightFly, RainbowBombergirl) |
| **`pipehelper.exe`** | altijd: `launch-direct.py` start het uit de runtime (`os.path.join(tp, "pipehelper.exe")`, regel 762) en weigert zonder dat bestand | `--plan` `pipehelper`; `antwoorden_ronde2.md`/`_ronde13.md`: de bron zit **niet** in de distributie |
| **De views** | 12 games hebben een view in `~/.local/share/ravengun/runtime/views/`: GalagaAssault, GhostBusters, GoGoStrike, JurassicPark, LuigisMansion, MedaruNoGunman, PacmanChomp, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, T4User, Terminator | `ls -1 ~/.local/share/ravengun/runtime/views/` |

**Samengevat per game in één zin:** bij een start draaien er nu nog **maximaal vier** TeknoParrot-onderdelen
mee — de **lader**, de **kern**, de **profielenset** en (bij **19** games) de **Ui** — plus altijd
**`pipehelper.exe`** en, bij een `Patreon`-profiel, de **activering in de prefix**.

### 4. Wat een losgekoppelde route vraagt — de kortste weg per game

Er zijn vijf soorten kortste weg, in volgorde van hoe ver we al zijn:

| Route | Wat het vraagt | Games |
|---|---|---|
| **"kan al"** | niets — de eigen plugin draait al, geen TeknoParrot-onderdeel in het pad | 2 (TombRaider, RabbidsHollywood) |
| **Eigen kern** | een eigen kern die **`InitializeASI` exporteert** (de enige koppeling met de lader — zie de sectie hierboven) en de hooks van dat `EmulationProfile` implementeert. De open bron levert alleen TypeX2 → voor de rest is dit nieuw werk, maar `patches/<Game>/patches.ini` staat er al. De `RUNNING <naam> <versie>`-controle van de gesloten kern is het referentiepunt (`findings/13_opengestelde_cores.md` §4.3) | 21 |
| **Eigen lader** | een eigen `LD_PRELOAD`-kastnabootser voor de Linux-ELF-games (Raw Thrills). Voor de 64-bits familie werkt de `halo.so`-aanpak (plthook + Capstone); voor de 32-bits familie moet de **vaste GOT** herschreven worden (58–60 slots) en `dlsym` onderschept | 7 (GalagaAssault, GhostBusters, GoGoStrike, Halo, JurassicPark, PacmanChomp, Terminator) |
| **Donglelaag** | een drop-in `dongles/<Game>/dongle.json`; de implementatie (`dongles/src/dongle.c`, 15 KB C99) bestaat al en de zelftest tegen de echte Halo-dongle slaagt | als **extra** laag bij 18 games |
| **Eigen startketen / game-eigen fix** | twee exe's met 5 s wachttijd (HOTDSD); ACIO op COM2 (Otomedius); een Sega-dienst (LGS); schijfletter-herleiding (`.\TeknoParrot\<LETTER>\`) | 4 |

**Twee dingen die de kortste weg voor iedereen verkorten:**

* **`pipehelper.exe` is de enige TeknoParrot-binary die in élk startpad blijft zitten.** De hostkant is
  volledig bekend (de TPB1-handdruk: kop met sessie, token en pijpnaam, antwoord `OKAY`) —
  `docs/research/loskoppelen-teknoparrot.md` §3 noemt het protocol "nu volledig bekend". Een eigen helper is
  dus een kleine, gedeelde winst voor alle 35 games.
* **De schijfletter-herleiding** (`CreateFileA` onderscheppen, elke letter A-Z naar `.\TeknoParrot\<LETTER>\`)
  is een gedeeld stuk dat meerdere games nodig hebben (Omatsuri, Scarlet Dawn met `Y:\SDET`, Luigi's Mansion
  met `X:`/`Y:`).

### 5. Blokkades

De blokkades vallen in vier klassen. **Voor geen enkele game is de dongle de enige blokkade.**

| Klasse | Games | Bewijs |
|---|---|---|
| **De gesloten kern** (`TeknoParrot(64).dll`) | 21 | de open OpenParrot-bron dekt alleen TypeX2 (0 treffers voor WartranTroopers/RawThrillsGUN/FarCry/TimeCrisis5/ALLSHOTDSD/InfinityBlade/LuigisMansion/SegaJvsLetsGoIsland) |
| **De gesloten lader** (`BudgieLoader.exe`) | 7 | `19_loader_analysis.md` §1 en §10: BudgieLoader is afgeschermd (onderdelen op `rawsize = 0`, payload in `.brr4`, dichtheid 7,97); het uittreksel is niet opengesteld |
| **Een game-eigen probleem** | 12 | D3D9-init (MusicGunGun2, InfinityBladeFX, GaiaAttack4, HM, HMII), films/decoders (LethalEnforcers3, Rampage), ACIO op COM2 (Otomedius), SRAM (MedaruNoGunman), packer (PigsMightFly, LuigisMansion), kalibratie (RaccoonRampage) |
| **Het spel of de data ontbreekt** | 2 | 3DCosplayMahjong en TankTankTank staan niet op de NAS |

**En de dongle als blokkade, per game:**

* **Blokkeert echt:** GhostBusters (waarschijnlijk HASP — de
  lader weigert), Terminator (HASP — native exit 255 "de game wil een HASP-dongle"). Rabbids Hollywood (Matrix, voorheen "stopt na ~1 min")
  staat hier niet meer: dat was een uitrol-fout, hermeting 2026-10-12 (288 s in attract; munt/start/richten niet gemeten).
* **Blokkeert niet (meer):** Rampage (Matrix aanwezig, de game speelt door), Transformers (de donglescan
  faalt in fase 1, de game speelt door), GaiaAttack4/HM/HMII (regio `E`, de game komt voorbij de regio-check),
  en **LGI/LGI3D/LGS** — daar is het nu gemeten: `patches/LGI/dongle.json` zet `"enforced": false` met het
  bewijs dat de game alle drie de leesroutes ziet falen en verdergaat met zijn eigen `DefaultRegion`
  (strings: `"amDongleGetRegion() is failed."`, `"DefaultRegion"`, `"Keychip Not Found"`).
* **Onbekend of het blokkeert:** Halo, Jurassic Park, NERF, TC5 (HASP aanwezig; ze zijn speelbaar via
  TeknoParrot, dus de gesloten kern beantwoordt de HASP-aanroepen — of de game ze überhaupt doet, is niet
  apart gemeten).

---

## Centrale afhankelijkheden van de TeknoParrot-map, 2026-10-17

**Doel (eigenaar, 2026-10-17):** elke game start zonder TeknoParrot: geen kern van TeknoParrot en geen bestand uit
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64` (hierna: de TP-map; RAVENGUN_TP_DIR, `--tp-dir` of `tp_dir` in
library.json). Dit hoofdstuk is het centrale deel: wat de directe route, de Unity-route, de daemon en de app zelf uit die map halen,
los van de per-game delen (die per familie geport worden). Besluit: `docs/decisions.md`, "De directe route en de Unity-route zonder de
TeknoParrot-map".

**Gemeten vóór de wijziging** (`RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan <Game> --patches ~/.local/opt/ravengun/share/ravengun/patches`):
alle negen eigen-kern-games (Friction, SilentHill, GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2, LuigisMansion, EADP,
MenInBlack) gaven `fallback` "lader ontbreekt: /nonexistent/OpenParrot.../OpenParrotLoader(64).exe", hoewel hun lader uit
`<data>/openparrot/current` komt. Met de TP-map gaf het plan voor die negen `cwd` en `tp` = de TP-map, en voor vijf ervan een regel
"legacy .env gelezen: <TP-map>/UserProfiles/<P>.env".

### De tabel: elk gebruik van de TP-map, en wat er nu mee gebeurt

Plan = bij het plannen (`plan_direct`, `rg-direct --plan`), start = bij het uitvoeren (`rg-direct --run`, de unit). "Eigen" = een game
met eigen lader en kern of zonder lader (`tp_free_settings`: eigen profiel en `[Loader] Core = none`, of `Build = ravengun` met
`Core = ravengun` of `Type = OpenParrot`).

| # | Wat | Waar in de code | Wanneer | Vóór 2026-10-17 | Nu |
|---|---|---|---|---|---|
| 1 | Lader `OpenParrotWin32/OpenParrotLoader.exe`, `OpenParrotx64/OpenParrotLoader64.exe` | `direct.cpp` (resolve_loader) | plan (bestaat hij?), start (argv) | altijd op bestaan gecontroleerd, ook als `[Loader] Build = ravengun` hem verving: de fout van de meting hierboven | alleen gecontroleerd als het plan hem gebruikt; eigen lader uit `<data>/openparrot/current` (`[Loader] Path`) |
| 2 | Lader `ElfLdr2/BudgieLoader(_x64).exe`, `TeknoParrot/BudgieLoader.exe`, `N2/`, `TeknoParrot/TeknoMacaw(64).exe`, `OpenParrotWin32/OpenParrotKonamiLoader.exe` | `direct.cpp` | plan, start | TP-map (of een `ElfLdr2`-link in de view) | ongewijzigd: geen eigen bouw; per familie (andere agents). Melding: "lader ontbreekt: <pad> (uit de TeknoParrot-map)" |
| 3 | Kern `TeknoParrot/TeknoParrot(64).dll` | `direct.cpp` | plan, start (argv) | TP-map | ongewijzigd voor games zonder `Core = ravengun`; melding "kern ontbreekt: <pad> (de kern van TeknoParrot; een eigen kern: [Loader] Core = ravengun)" |
| 4 | `pipehelper.exe` | `direct.cpp`, `rg_direct.cpp` | plan, start | terugval als `rg-jvsbridge.exe` ontbrak | eigen: alleen `<data>/jvsbridge/rg-jvsbridge.exe` (onderdeel "JVS bridge"), ontbreekt hij dan geen start met het pad; TP-plan: terugval zoals voorheen |
| 5 | Werkmap van de lader (`cwd`, `starts[].cwd`) | `direct.cpp` | start | TP-map (behalve ELF-laders) | eigen: de map van de game. OpenParrotLoader geeft het spel altijd zijn eigen map of `TP_GAME_WORKING_DIRECTORY` (bron `OpenParrotLoader/src/main.cpp`, `CreateProcess(..., gameFolderW, ...)`); de werkmap van de lader telt alleen voor `FFBBlaster/` (TeknoParrot, `[FFB Blaster] Enable`) |
| 6 | Werkmap van rg-direct (`systemd-run --working-directory`), de JVS-brug en `wine reg import` | `games.cpp` (direct_unit_argv), `rg_direct.cpp` | start | altijd de TP-map: een verborgen map = de unit start niet (chdir) | `plan_run_dir`: de TP-map alleen als het plan die gebruikt, anders de werkmap van de lader |
| 7 | `ParrotData.xml` (ExitKey, PauseKey, score, `Elfldr2LogToFile`, `Elfldr2NetworkAdapterName`) | `direct.cpp` | plan (→ `teknoparrot.ini`, ElfLdr2-omgeving) | gelezen | eigen: niet gelezen; `teknoparrot.ini` krijgt de standaard 0x1B/0x13, gelijk aan wat `ParrotData.xml` op deze machine zet (gemeten) |
| 8 | `GameProfiles/<P>.xml` (revisie van het sjabloon) | `direct.cpp` | plan | gelezen; afwijkende `ProfileRevision` = fallback | eigen: niet gelezen (de revisie hoort bij de kern van TeknoParrot) |
| 9 | `UserProfiles/<P>.xml` (hint `EmulatorType`) en `UserProfiles/<P>.env` (oud .env onder game.ini) | `direct_profile.cpp` (load_for_profile) | plan, ook `launch_mode`, `direct_input`, `profile_env` | gelezen | eigen: niet gelezen. Gemeten met `RAVENGUN_NO_LEGACY_ENV=1`: voor GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2 en LuigisMansion is het plan zonder het .env gelijk op de logregel na |
| 10 | `ElfLdr2/` (bibliotheken in de view, `ElfLdr2`-link) | `direct_files.cpp` (plan_view) | plan bij een start | TP-map | ongewijzigd (ElfLdr2-games: Terminator, CrossfirePaintball); in `tp_files` als `ElfLdr2/` |
| 11 | `@tpui/ElfLdr2/...` in `native.json` | `native.cpp` | plan, start | TP-map | ongewijzigd (native route; AAA, AliensArmageddon, Halo, JurassicPark; per familie) |
| 12 | GE-Proton 11-7 van de Unity-route | `ravengund/main.cpp`, `unity.cpp` | start | `~/.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64` (download van TeknoParrotUi) | onderdeel `ge-proton` (`packaging/components.json`, vastgepind op de release van GloriousEggroll met sha256) in `<data>/proton/GE-Proton11-7-x86_64`, dan Steam; de kopie van TeknoParrotUi telt niet (`default_unity_proton`) |
| 13 | De app: starten alleen met een geldige runtime-map | `Launcher.cpp` (`EngineBackend::available`) | elke start | geen loader of `GameProfiles/` in de map = "The game runtime was not found", geen enkele game startte | altijd beschikbaar; de engine meldt per game het ontbrekende bestand. Wizard en Settings > Paths: de map is optioneel |
| 14 | Lijst van games in de daemon (`UserProfiles/*.xml`, `Icons/`) en `available` van de webapp | `games.cpp` (list_games, list_json, read_icon) | lijst | lege `tp_dir` = lege lijst; zonder `UserProfiles/` "niet beschikbaar" | de games met een eigen profiel komen altijd in de lijst; beschikbaar = er is iets te spelen. Iconen uit `Icons/` blijven cosmetisch (zonder: geen icoon) |
| 15 | De app: `UserProfiles/` (games zonder eigen profiel), `GameProfiles/` (sjablonen voor herkenning, naam), `Metadata/`, `Icons/` | `GameLibrary.cpp`, `GameData.cpp`, `CreateProfileDialog`, `PropertiesDialog` | scan, Properties | gelezen | ongewijzigd en niet nodig voor eigen-profiel- en Unity-games (die komen uit game.ini en unity.json); zonder de map: geen icoon, naam uit game.ini, geen herkenning van nieuwe games aan een sjabloon |
| 16 | `teknoparrot.ini`, `MINIGUN.INI`/`MUSEUM.INI`/`HAUNTED2.INI` | `direct_profile.cpp` (build_ini, taito_gun_ini) | plan → view | al uit game.ini (`[Config*]`), behalve de sneltoetsen (#7) | uit game.ini; bij eigen games helemaal |
| 17 | DLL's in `TeknoParrot/` (ffmpeg, FAudio, SDL2, TeknoDraw, …), `FFBBlaster/`, fonts | — | — | alleen geladen door de kern of de laders van TeknoParrot | niet gebruikt door eigen plannen (geen pad in het plan, gecontroleerd) |

### Wat elk plan nog gebruikt: `rg-direct --tp-refs`

Het plan zegt zelf wat het uit de TP-map gebruikt of leest (`tp_files`; leeg = `tp` leeg). `rg-direct --tp-refs [--json] [--patches DIR]
[--tp DIR] [--expect-free A,B,...]` toont per game in `patches/` (route zoals de daemon hem kiest: native, Unity, direct) wat de start nog uit
de TP-map gebruikt: `tp_files` plus elk pad uit die map in het plan (ook de Wine-vorm), de `@tpui/`-verwijzingen van `native.json`, de
Proton van de Unity-route, of bij een plan dat niet lukt het pad in de melding. `.` = de map zelf (de werkmap). Alleen lezen.

Gemeten 2026-10-17 op deze machine (met de echte TP-map; `--patches ~/.local/opt/ravengun/share/ravengun/patches`):

| Groep | Games | Verwijzingen |
|---|---|---|
| Eigen lader en kern (directe route) | EADP, Friction, GaiaAttack4, HauntedMuseum, HauntedMuseumII, LuigisMansion, MenInBlack, MusicGunGun2, SilentHill | **0** (ook met `RAVENGUN_TP_DIR=/nonexistent`: ok en 0) |
| Unity-route | RabbidsHollywood, TombRaider | **0**; verdict `fallback` tot het onderdeel `ge-proton` geïnstalleerd is (`<data>/proton/GE-Proton11-7-x86_64/proton`) |
| Native zonder TP-map | 2Spicy, GSEVO, HOTD4, HOTD4SP, HOTDEX, LGJ, LGJS, NERF, Primevil, Rambo, TargetTerrorGold | 0 |
| Native met `@tpui` | AAA (1), AliensArmageddon (5), Halo (1: `ElfLdr2/x64/libs`), JurassicPark (6) | `ElfLdr2/libs/...` |
| Direct op de kern van TeknoParrot | AfterDark, AfterDark2, AliensExtermination, BugBusters, Coopers9, DrakonsRealmKeepers, ElevatorActionInvasion, FarCryParadiseLost, GG, LostLandAdventure, MarioKartDX, MarsSortie, OG, PullTheTrigger, RobinHood, SDR, Transformers, WildWestShootout | 3-4: `.`, `TeknoParrot/TeknoParrot(64).dll`, `ParrotData.xml`, meestal `GameProfiles/<P>.xml` |
| idem, met oud `UserProfiles/<P>.env`/`.xml` | Gashaaaan, HOTDSD, InfinityBladeFX, LethalEnforcers3, LGI, LGI3D, LGS, PigsMightFly, PointBlankX, RaccoonRampage, Rampage, TransformersShadowsRising | 5-6 |
| Lader van TeknoParrot | TC5 (`OpenParrotx64/OpenParrotLoader64.exe` + kern, 7), SilentScopeBoneEater (`TeknoParrot/TeknoMacaw64.exe`, 4) | |
| ElfLdr2 (direct) | CrossfirePaintball (3), Terminator (5) | `ElfLdr2/`, `ParrotData.xml`, `GameProfiles/` |
| Plan lukt hier niet (game niet gevonden) | Gashaaaan2, GoGoStrike | 0 (onbekend) |

Totaal: 20 games ok zonder enige verwijzing, 38 met, 4 waarvan het plan hier niet lukt (2 Unity zonder het onderdeel, 2 niet gevonden).

### Wat het bewijst (tests)

- `tests/test_direct_launch.cpp`: een game met `Core = ravengun` plant met een "vergiftigde" TP-map (ander sjabloon, andere sneltoetsen,
  een oud .env) precies zoals met een lege en een niet-bestaande map: ok, `tp` en `tp_files` leeg, geen pad uit de map, werkmap = map van
  de game, eigen JVS-brug, `ExitKey=0x1B`. Een game op de kern van TeknoParrot: `tp_files` = sjabloon, `ParrotData.xml` en de kern, werkmap
  = TP-map, en zonder de map "kern ontbreekt: <pad>". `Core = none` en `Core = ravengun` vallen niet terug op `pipehelper.exe`.
- `tests/direct_golden.py` (ctest `ravengun_direct_golden`): de negen eigen-kern-games uit `patches/` in een eigen hermetische opstelling
  (alles in een view, DXVK, Gst32, BepInEx, redistributables, GE-Proton): met een niet-bestaande TP-map ok, hetzelfde plan als met de
  volledige nep-map, `tp`/`tp_files` leeg en geen pad uit de map; daarna `rg-direct --tp-refs --expect-free` met ook TombRaider en
  RabbidsHollywood. De gouden plannen van alle games op de kern van TeknoParrot zijn ongewijzigd; die van EADP, GaiaAttack4,
  HauntedMuseum(II) en LuigisMansion veranderen alleen in `cwd` (de map van de game).
- `tests/test_unity_launch.cpp`: `default_unity_proton` kiest ons onderdeel, dan Steam, nooit de kopie van TeknoParrotUi; de echte
  `unity.json` van TombRaider en RabbidsHollywood plannen zonder één pad met "TeknoParrot".
- `tests/app/test_app_core.cpp`: de backends zijn beschikbaar zonder runtime-map.

### Op de tv na te lopen (de eigenaar)

1. In de app Components: "GE-Proton 11-7" installeren (563 MB); daarna staat `~/.local/share/ravengun/proton/GE-Proton11-7-x86_64/proton`.
2. De TP-map verbergen (bv. `mv` naar een andere naam; ook `~/.local/share/TeknoParrotUI/proton` mag weg), de daemon herstarten.
3. Elk van de negen eigen-kern-games en de twee Unity-games met dubbelklik starten en 5 minuten laten lopen. In het logboek
   (`~/.local/state/ravengun/logs/direct-<P>-*.log`) staat bij `cmd:` `--working-directory=` de map van de game, en de regel
   `pipehelper:` noemt `rg-jvsbridge.exe`. Unity: `unity-<P>-*.log`, `cmd:` begint met `.../ravengun/proton/GE-Proton11-7-x86_64/proton run`.
   De eerste Unity-start met het nieuwe Proton vernieuwt de koppelingen in de prefix `unity-aagames` (Proton zelf, `config_info`).
4. `build/rg-direct --tp-refs` met de verborgen map: de elf games staan op 0 (Unity op `ok` na stap 1).

## Wat er in dit document níet staat (en waarom)

* **Geen dongle-emulatie uitgewerkt.** Dat is een projectafspraak:
  `docs/research/loskoppelen-teknoparrot.md` regel 8 — *"dongle-, keychip-, regio-, integriteits- en
  abonnementsafhandeling bouwt RavenGun niet."* Dit document benoemt alleen **welke** laag een game nodig
  heeft en **met welk bewijs**, zodat de beslissing geïnformeerd genomen kan worden.
* **Geen aanpassingen aan bestaande bevindingen.** `findings/INVENTARIS-dongle-per-game.md` en
  `findings/WAT-ELK-SPEL-NODIG-HEEFT.md` blijven ongewijzigd. Waar mijn meting iets toevoegt dat daar niet
  staat, staat het hier met de bron erbij (zie de open vragen hieronder).
* **Geen per-game-patchadressen voor de gesloten kern.** Die zijn er niet statisch: `12_aanpassingen_en_koppelingen.md`
  §6 zegt dat de byte-adressen voor de 41 games van deze cores niet te extraheren zijn zonder de core eerst
  te openen. Voor vier Type X2-titels staan ze wél in de open bron (HauntedMuseum `0x9E410`/`0x9E3B0`,
  HauntedMuseumII `0x9D970`/`0x9B920`, GaiaAttack4 `0x113E80`, MusicGunGun2 `0x137C70`) — en ik heb
  `TypeX2Generic.cpp` regel 2239 en 2241 nagelezen: dat zijn inderdaad
  `injector::MakeJMP(imageBase + 0x113E80, ReturnsTrue); // Disable Gun Writes` en
  `injector::MakeNOP(imageBase + 0x1156C8, 4); // Volume Write`.

---

## Open vragen

Deze punten heb ik gevonden maar niet kunnen afmaken; ze staan hier zodat een volgende ronde ze kan oppakken.
**Ik heb ze niet in bestaande bevindingen verwerkt.**

1. **Transformers heeft een dongle die in de inventaris niet genoemd wordt.** `findings/INVENTARIS-dongle-per-game.md`
   zet bij TransformersShadowsRising een streepje, maar de spelmap bevat `Sega/shell/dk2win64.dll`,
   `dk2win32.dll` en `dk2inst.dll` (Dinkey Pro) en `Sega/shell/Dongle - 261011.log` zegt
   `MainThreadDongleScan: failed stage 1`. Dat is precies het `sega-scan`-type uit `dongles/README.md` §2.
   **Vraag:** hoort Transformers in de inventaris bij `sega-scan` in plaats van "geen"? En is de
   `Sega/shell/Dongle - *.log` van de **shell** of van het spel?
2. **Terminators `hasp/`-map: 5413 bestanden, niet 10.826.** `dongles/README.md` §5 en
   `findings/LOSKOPPELEN.md` noemen 10.826 bestanden; mijn telling op de NAS is **5413**.
   **Vraag:** welke telling klopt, en is de map intussen veranderd?
3. **GhostBusters: welk donglebestand ontbreekt?** De lader meldt `GAME PATH CHECK FAILED` en native meldt
   `Game dongle initialization failed`, maar de dump heeft alleen een splash-afbeelding
   (`utils/splash/dongle-1024x768.cfg`). **Vraag:** welk bestand zoekt de lader hier — een `fff4`-achtige
   HASP-image, een `hasp/`-map, of iets anders? Zonder dat blijft GhostBusters "onbekend".
4. **Waarom blokkeert Matrix bij Rabbids wél en bij Rampage niet?** `findings/MATRIX-dongle-API.md` §4 stelt
   deze vraag al; ik heb geen nieuw bewijs. Rampage laadt `matrix64.dll` en zoekt `MATRIX.INI`, Rabbids roept
   `Dongle_Count(40)` aan en stopt (voorheen gemeld; het "stopt" bleek een uitrol-fout, zie hermeting 2026-10-12 in `packaging/games-status.md`). **Vraag:** roept Rampage `Dongle_Count` niet aan, of accepteert hij 0?
   (Methode staat in dat document: `dnlib.py` op `Assembly-CSharp.dll`.)
5. **Wat doet `matrix32.TEKNOPARROT.dll` in Infinity Blade FX?** Die DLL staat naast `matrix32.dll` en de
   `readme.txt` beschrijft een "matrix32/TPFIX"-oplossing, maar ik heb niet gelezen wat dat bestand doet of
   wie het plaatst (de speldump of een gebruiker). **Vraag:** is dit een door TeknoParrot meegeleverde
   vervanging van de Matrix-DLL?
6. **Hoeveel games hebben echt een `Patreon`-activering nodig?** `--plan` geeft `needs_activation: true` voor
   10 games, maar dat komt uit het **sjabloon** van de runtime (`patreon_in_layers()`), niet uit een meting van
   wat de kern doet. **Vraag:** blokkeert het ontbreken van de activering een spel werkelijk, en zo ja bij
   welke?
7. **De 19 games zonder `[Launch] Mode` starten standaard via TeknoParrotUi.** Dat is een bewuste keuze
   (`play-halo.sh` regel 217-225), maar het betekent dat "de Ui draait mee" voor die games een
   **configuratie**-afhankelijkheid is, geen technische. **Vraag:** welke van die 19 zijn met `--direct`
   inmiddels wél gemeten? (In `packaging/games-status.md` staat de achtste ronde: zes games standaard direct;
   de negende tot elfde ronde hebben daar LGI, LGI3D, LGS, NERF, Rampage, PigsMightFly, MedaruNoGunman,
   Gashaaaan, Gashaaaan2, LuigisMansion en RaccoonRampage aan toegevoegd.)
8. **3DCosplayMahjong en TankTankTank zijn nooit gemeten.** Ze staan niet op de NAS, dus `--plan` stopt met
   exit 4. Alles wat hier over die twee staat is "onbekend". **Vraag:** komen die twee dumps nog, of horen ze
   uit de lijst?
9. **`sega-scan` past niet in `docs/dropins.md`.** Die afspraak noemt vier `kind`-waarden: `hasp`, `matrix`,
   `region`, `amdaemon`. Transformers Shadows Rising heeft een **vijfde** nodig — de Dinkey-donglescan van de
   Sega-shell (`Sega/shell/dk2win64.dll`; `Dongle - 261011.log`: `MainThreadDongleScan: failed stage 1`).
   **Vraag:** wordt `sega-scan` een vijfde `kind`, of valt Transformers onder `region`?
10. **`dongle.json` ontbreekt bij alle 35 games, terwijl 19 games een dongle hebben.** Volgens de nieuwe
    afspraak is "geen `dongle.json`" de machineleesbare betekenis van "geen dongle" — en dan klopt de
    inventaris niet met de bestandsscan. **Vraag:** wie vult de 19 `dongle.json`-bestanden, en mag de
    bestandsscan (dit document) daarvoor als bron dienen?
11. **De globale `hasp/<Game>/`-boom staat er nog.** `docs/dropins.md` zegt dat die plek vervangen wordt door
    `patches/<Game>/dongle/`, maar op de Deck staat `~/.local/share/ravengun/hasp/Halo Fireteam/{dongle.json,
    memory/*}` nog, plus de niet-game-specifieke `ravengun-hasp.so` / `ravengun-hasp-x64.so`. **Vraag:** moet
    `hasp/Halo Fireteam/` naar `patches/Halo/dongle/` verhuizen, en blijft `hasp/` daarna alleen voor de
    emulator-binary?
12. **`dropin/` heeft nog geen enkele keer een bestand.** Er staat nergens een `dropin/` of `dropin.ini`
    buiten `patches/_template/`, dus de launchercode die `dropin.ini` leest (`To`, `DllOverride`, `Env`,
    `Preload`) is nog niet in bedrijf genomen — althans, niet zichtbaar in de mappen. **Vraag:** is de lezer
    in `launch-direct.py` al gebouwd, en zo ja, waar?
13. **`docs/research/kern-abi.md` noemt `Name` als export van beide kernen; dat is gemeten onjuist.**
    `objdump -p` geeft voor `TeknoParrot64.dll` drie exports (`InitKonami`, `InitializeASI`,
    `TP_InitializeApmTest`) en voor `OpenParrot64.dll` twee (`InitializeASI`, `PrepareSafeInit`). De naam
    `Name` komt uit de **kopregel** van de `objdump`-tabel (`Ordinal   Hint Name`), niet uit de
    exportdirectory. **Vraag:** kan die passage in `kern-abi.md` gecorrigeerd worden? Ik heb het bestand
    zelf niet aangeraakt (het is niet van mij).
14. **De regio-laag is voor LGI/LGI3D/LGS `enforced: false`.** De nieuwe `dongle.json`-bestanden zeggen dat
    de game zonder dongle terugvalt op zijn eigen `DefaultRegion`, en de strings staan erbij. **Vraag:** is
    dat ook voor GaiaAttack4, HauntedMuseum, HauntedMuseumII en RainbowBombergirl gemeten? Als die vier ook
    terugvallen, is de regio-laag voor **alle zeven** regio-games een formaliteit en geen blokkade. Zie ook
    `docs/region-layer.md`.

---

## Bijlagen

* [loskoppelen-metingen.md](research/loskoppelen-metingen.md) — de ruwe `--plan`-uitvoer van alle 35 games
  (lader, kern, argv, omgeving, patches, `needs_activation`, `dongle`, gegenereerde `teknoparrot.ini`).
* [loskoppelen-dongle-bewijs.md](research/loskoppelen-dongle-bewijs.md) — de bestandsscan van de NAS en de
  per-game dongle-bewijsregels.
* [loskoppelen-laders-en-kern.md](research/loskoppelen-laders-en-kern.md) — de ladertabel, de
  `resolve_loader()`-regels, de eigen build (`BUILDINFO.txt`) en de dekking van de open OpenParrot-bron.
