# Hoe krijg je code in het ELF-proces van een ElfLdr2-game?

*Onderzoek door RavenGun, ronde 11 (2026-10-11), **bijgewerkt in ronde 13** nadat de rt-glfix-agent
§2 en §5 met betere metingen heeft weerlegd. Lees §1 en §3; §2 is geschrapt en §5 is beantwoord.*

> **Wat er in ronde 13 is gecorrigeerd.** Ik raadde hieronder de route via `ElfLdr2/libs/` aan.
> **Die is weerlegd**, met harder bewijs dan ik had: een dunne `libGLU.so.1` in die map met een
> constructor die één regel logt, **liep niet**, en de kaart van het spelproces bevat **geen enkele
> `.so` uit die map** — alleen de `.dll`-bestanden. ElfLdr2 bootst die bibliotheken na in zijn eigen
> anonieme uitvoerbare gebied (gemeten: `0x71a41000-0x71eff000`, direct na `TeknoParrot.dll`); een
> constructor draait er nooit. Mijn vermoeden over `env/dev/` (een nagemaakte root) past daarbij.
> **De werkende route staat in §1: `LD_AUDIT`.** Zie `docs/rt-glfix.md` voor de volledige meting.

## De vraag

Acht games gebruiken TeknoParrot's **ElfLdr2**-lader (`BudgieLoader.exe` / `BudgieLoader_x64.exe`)
en zijn **Linux-ELF**-programma's: GalagaAssault, GhostBusters, GoGoStrike, Halo, JurassicPark,
PacmanChomp, TankTankTank en Terminator. Twee drop-ins willen code in dat ELF-proces krijgen:

- `librt-glfix.so` — de GOT-herschrijving voor Jurassic Park, PacmanChomp en GalagaAssault;
- `ravengun-hasp*.so` — de HASP-emulator voor Halo, Terminator en Jurassic Park.

## 1. `LD_PRELOAD` kan niet — maar `LD_AUDIT` wel

Gemeten door de HASP-agent, en het is fataal in plaats van hinderlijk:

```
ERROR: ld.so: object '...ravengun-hasp-x64.so' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS64): ignored.
Cygwin WARNING: Couldn't compute FAST_CWD pointer...
BudgieLoader_x64: *** fatal error - error while loading shared libraries: ...ravengun-hasp-x64.so
```

`BudgieLoader` is een **Cygwin**-programma (`msys-2.0.dll`), en Cygwin leest `LD_PRELOAD` en
probeert de opgegeven bibliotheek als **Cygwin-DLL** te laden. Een Linux `.so` laat het programma
sterven. Zonder die variabele start de game volledig (150 s gemeten).

**Maar er is een tweede kanaal dat Cygwin niet kent en glibc wel: `LD_AUDIT`.** Dat is gemeten in het
echte spelproces door de rt-glfix-agent:

```
[rt-glfix JurassicPark] geladen (pid 999164, mode 7, maxtex 8192, brug op 0xf7c28000)
[rt-glfix JurassicPark] spelbeeld gevonden op 0x08048000 (11091968 bytes)
[rt-glfix JurassicPark] PLT-invoeren: 577
[rt-glfix JurassicPark] ronde 17340: 54 GL-invoeren omgezet (54 gezien)
[rt-glfix JurassicPark] klaar: 55 GL-invoeren omgezet, 577 invoeren bekeken
```

En het BudgieLoader-proces is **32-bits** (`lib/wine/i386-unix/wine-preloader`, ELF 32-bit i386),
dus `gcc -m32` past precies en onze code zit in hetzelfde adresbeeld als het spel. De 64-bits
`wine`-startwrapper klaagt over `wrong ELF class: ELFCLASS32`; dat is onschadelijk.

**De launcher doet dit sinds ronde 13 automatisch**: is de game een Linux-ELF die onder Wine door
ElfLdr2 gestart wordt, dan gaat het pad van een `Preload`-drop-in naar `LD_AUDIT` in plaats van naar
`RG_DROPIN_PRELOAD`. Zie `dropin_plan()` in `scripts/launch-direct.py` en `docs/dropins.md`.

En de lader heeft **geen eigen preload-haak**:

| bestand | `LD_PRELOAD` | `preload` | `LD_LIBRARY_PATH` |
|---|---|---|---|
| `ElfLdr2/x64/BudgieLoader_x64.exe` | 0 | 0 | 0 |
| `ElfLdr2/BudgieLoader.exe` | 0 | 0 | 0 |
| `ElfLdr2/x64/TeknoParrot64.dll` | 0 | 0 | 0 |
| `ElfLdr2/TeknoParrot.dll` | 0 | 0 | 0 |

## 2. ~~Maar `ElfLdr2/libs/` staat wél op het zoekpad~~ — WEERLEGD

Deze sectie is vervallen. Zie de correctie bovenaan: een constructor in die map draait nooit, en de
proceskaart bevat geen enkele `.so` uit die map. De metingen die ik hier had (de ELF-koppen vragen
bibliotheken die alleen daar bestaan, en de RPATH `/usr/lib/PhysX/v2.8.1/` bestaat niet) blijven
juist — maar de conclusie die ik eraan hing niet. **ElfLdr2 leest die bestanden hoogstens voor
symbolen en bootst de rest na.**

## 3. Wat dit betekent voor een eigen lader

De route `LD_AUDIT` werkt **met de gesloten BudgieLoader**. Dat is belangrijk: de acht ElfLdr2-games
hebben dus **geen eigen lader nodig** om een bibliotheek in het spelproces te krijgen. Wat een eigen
lader zou toevoegen is het starten van de ELF zonder BudgieLoader — een aparte vraag, en niet nodig
voor de drop-ins.

## 4. Wat er dan te kiezen valt

| route | kosten | per game? |
|---|---|---|
| **`LD_AUDIT` via de drop-in** | klein, en **bewezen** | ja |
| Eigen lader | groot: `LD_PRELOAD` zelf zetten bij het starten van de ELF | ja |
| ~~Bibliotheek vervangen in `ElfLdr2/libs/`~~ | **weerlegd** | — |

## 5. ~~De proef die dit beslecht~~ — gedaan, en de uitkomst is nee

De proef is uitgevoerd (dunne `libGLU.so.1`, echte opzij als `.orig`, daarna netjes teruggezet) en
het proeflogboek bleef leeg: **de constructor liep niet**. De proceskaart bevestigt het: alleen de
`.dll`-bestanden uit die map zijn gemapt, geen enkele `.so`. De uitweg is dus niet die map maar
`LD_AUDIT` (§1).

## Hoe je dit narekent

```bash
RT=~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64
strings -a "$RT/ElfLdr2/x64/BudgieLoader_x64.exe" | grep -x -e LD_PRELOAD -e preload   # niets
readelf -d "…/Jurassic Park/game" | grep -E 'RPATH|NEEDED'
# en de werkende route:
python3 scripts/launch-direct.py JurassicPark --plan | grep -o 'LD_AUDIT=[^,]*'
```

