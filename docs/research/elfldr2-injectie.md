# Hoe krijg je code in het ELF-proces van een ElfLdr2-game?

*Onderzoek door RavenGun, ronde 11 (2026-10-11). Dit bepaalt of de GL-fix van Jurassic Park en de
HASP-laag voor de Linux-games ooit kunnen aangaan. Alle cijfers zijn gemeten.*

## De vraag

Acht games gebruiken TeknoParrot's **ElfLdr2**-lader (`BudgieLoader.exe` / `BudgieLoader_x64.exe`)
en zijn **Linux-ELF**-programma's: GalagaAssault, GhostBusters, GoGoStrike, Halo, JurassicPark,
PacmanChomp, TankTankTank en Terminator. Twee drop-ins willen code in dat ELF-proces krijgen:

- `librt-glfix.so` — de GOT-herschrijving voor Jurassic Park, PacmanChomp en GalagaAssault;
- `ravengun-hasp*.so` — de HASP-emulator voor Halo, Terminator en Jurassic Park.

## 1. `LD_PRELOAD` kan niet

Gemeten door de HASP-agent, en het is fataal in plaats van hinderlijk:

```
ERROR: ld.so: object '...ravengun-hasp-x64.so' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS64): ignored.
Cygwin WARNING: Couldn't compute FAST_CWD pointer...
BudgieLoader_x64: *** fatal error - error while loading shared libraries: ...ravengun-hasp-x64.so
```

`BudgieLoader` is een **Cygwin**-programma (`msys-2.0.dll`), en Cygwin leest `LD_PRELOAD` en
probeert de opgegeven bibliotheek als **Cygwin-DLL** te laden. Een Linux `.so` laat het programma
sterven. Zonder die variabele start de game volledig (150 s gemeten).

En de lader heeft **geen eigen preload-haak**:

| bestand | `LD_PRELOAD` | `preload` | `LD_LIBRARY_PATH` |
|---|---|---|---|
| `ElfLdr2/x64/BudgieLoader_x64.exe` | 0 | 0 | 0 |
| `ElfLdr2/BudgieLoader.exe` | 0 | 0 | 0 |
| `ElfLdr2/x64/TeknoParrot64.dll` | 0 | 0 | 0 |
| `ElfLdr2/TeknoParrot.dll` | 0 | 0 | 0 |

De launcher zet het pad daarom als `RG_DROPIN_PRELOAD` — maar **niemand leest dat nog**. De drop-in
staat dus wel in de view en is dood.

## 2. Maar `ElfLdr2/libs/` staat wél op het zoekpad

Dat is de uitweg. Bewijs uit de ELF-koppen van de games zelf:

**Jurassic Park** (`game`, 32-bits) vraagt 16 bibliotheken, en **11 daarvan bestaan alleen in
`ElfLdr2/libs/`**:

| gevraagd | in `ElfLdr2/libs/`? |
|---|---|
| `libPhysXLoader.so.1`, `libGLU.so.1`, `libtheora.so.0`, `libogg.so.0`, `libdl.so.2`, `libpthread.so.0`, `librt.so.1`, `libm.so.6`, `libudev.so.1`, `libgcc_s.so.1`, `libc.so.6` | **ja** |
| `libstdc++.so.6`, `libSDL-1.2.so.0`, `libGL.so.1`, `libglut.so.3`, `libX11.so.6` | nee (uit het systeem) |

**Halo** (`game`, 64-bits) vraagt 13 bibliotheken, waarvan **8 in `ElfLdr2/x64/libs/`**:
`libGLU.so.1`, `libpthread.so.0`, `libdl.so.2`, `libudev.so.1`, `liblapack.so.3`, `libm.so.6`,
`libgcc_s.so.1`, `libc.so.6`.

En het beslissende detail: de **RPATH van Jurassic Park is `/usr/lib/PhysX/v2.8.1/`, en die map
bestaat niet**. De game kan `libPhysXLoader.so.1` dus alleen vinden als de lader `ElfLdr2/libs/`
aan het zoekpad toevoegt. Dat doet hij blijkbaar — anders startte de game niet.

> **Gevolg: een bibliotheek in `ElfLdr2/libs/` wordt in het ELF-proces van de game geladen, en zijn
> constructor draait vóór `main`. Dat is precies wat `LD_PRELOAD` zou geven — maar dan zonder
> `LD_PRELOAD`.**

## 3. Wat daar nog aan schort: het is niet per game

De view van een game heeft **geen eigen `ElfLdr2/`**:

```
$ readlink -f ~/.local/share/ravengun/runtime/views/JurassicPark/ElfLdr2
/home/deck/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2
```

Het is een **symlink naar de gedeelde runtime**. Een drop-in met `To = ElfLdr2/libs` zou dus in de
**gedeelde** map schrijven — precies wat `docs/dropins.md` verbiedt ("alles wat één game nodig
heeft staat in `patches/<Game>/`"). En het zou de andere zeven ElfLdr2-games beïnvloeden.

**Om dit per game te houden moet de launcher een view zijn eigen `ElfLdr2/libs/` geven** (een echte
map in de view, of een view-specifieke map vóór de gedeelde op het zoekpad). Dan is een drop-in met
`To = ElfLdr2/libs` weer gewoon een bestand plus één sectie in `dropin.ini`.

## 4. Wat er dan te kiezen valt

| route | kosten | per game? |
|---|---|---|
| **Eigen lader** (werksoort 3 in `docs/loskoppelen-per-game.md`) | groot: `LD_PRELOAD` zelf zetten bij het starten van de ELF | ja |
| **Bibliotheek vervangen in `ElfLdr2/libs/`** | klein: een dunne laag die de echte bibliotheek doorlaadt en de constructor laat werken | pas ja als de launcher de view een eigen `libs/` geeft |
| **`RG_DROPIN_PRELOAD` laten lezen door de eigen lader** | klein, maar werkt alleen samen met de eerste route | ja |

De derde is geen zelfstandige route: het is de afspraak tussen de launcher en de eigen lader.

## 5. De proef die dit beslecht

Nog te doen, en klein: zet een **dunne `libGLU.so.1`** in `ElfLdr2/libs/` waarvan de constructor één
regel naar een logbestand schrijft en die daarna de echte bibliotheek doorlaadt, start Jurassic Park,
en kijk of de regel verschijnt. Verschijnt hij, dan is de route bewezen en is de GL-fix te leveren
zonder eigen lader. Verschijnt hij niet, dan is de lader geen gewone `ld.so`-route (bijvoorbeeld
omdat hij de game in een eigen root start — `ElfLdr2/env/` bevat een `dev/`-map, wat op een
nagemaakte root wijst) en moet de eigen lader het doen.

**Let op bij het doorladen:** de vervangende bibliotheek moet de echte op een **absoluut** pad
vinden (bijvoorbeeld door hem eerst uit de weg te schuiven en `libGLU.so.1.orig` te noemen), anders
laadt hij zichzelf.

## Hoe je dit narekent

```bash
RT=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64
readelf -d "…/Jurassic Park/game" | grep -E 'RPATH|NEEDED'
ls "$RT/ElfLdr2/libs"
strings -a "$RT/ElfLdr2/x64/BudgieLoader_x64.exe" | grep -x -e LD_PRELOAD -e preload   # niets
readlink -f ~/.local/share/ravengun/runtime/views/JurassicPark/ElfLdr2
```
