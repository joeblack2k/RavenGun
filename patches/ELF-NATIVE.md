# ElfLdr2-games: kan de game-ELF zonder ElfLdr2?

*Gegenereerd door `patches/elf-native.py`; niet met de hand bijwerken.*

De games van de Cygwin-lader (`Type = ElfLdr2`, `BudgieLoader.exe`) zijn **gewone Linux-ELF's**:
32-bits (Halo is 64-bits), met `/lib/ld-linux.so.2` als interpreter en `DT_NEEDED` naar `libGL`,
`libX11`, `libSDL-1.2`, PhysX enzovoort. Dat roept de vraag op die deze kaart meet: **loopt zo'n
game ook als je hem als een gewoon Linux-programma start?** Dan zou de hele Cygwin-lader weg
kunnen — en daarmee Wine én TeknoParrot voor deze familie.

**Wat de meting doet.** Per game (1) welke bibliotheken de dynamic linker op deze machine niet
vindt (`ldd`), en (2) een echte startpoging van de ELF met **10 seconden** venster, in twee standen:
**kaal** en met `librt-glfix.so` uit de drop-in als `LD_PRELOAD` (onze GL-fix). Het spelproces wordt
herkend op het pad van de exe plus een omgevingsvariabele die alleen onze start meekrijgt
(`RG_ELF_NATIVE=<Game>`), en daarna opgeruimd via diezelfde weg.

**Uitkomst: 0 van de 7 games starten native.**

| Game | Wat het is | Ontbrekende bibliotheken | Kaal | Met `librt-glfix.so` | Laatste logregel |
|---|---|---|---|---|---|
| GalagaAssault | ELF32 i386 | — geen — | niet gestart | niet gestart | `—` |
| GhostBusters | ELF32 i386 | `/usr/lib/libcsv.so`, `libGLU.so.1`, `libesd.so.0`, `libglut.so.3` | niet gestart | geen glfix | `/home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm/game: e` |
| GoGoStrike | ELF32 i386 | `libNL.so.1.6`, `liballeg.so.4.0`, `libstdc++.so.5` | niet gestart | geen glfix | `/home/deck/.local/share/ravengun/runtime/views/GoGoStrike/bowling_usa:` |
| Halo | ELF64 x86-64 | `libfmod.so.10`, `libfmodstudio.so.10` | niet gestart | geen glfix | `/home/deck/.local/share/ravengun/runtime/halo-g7-tp/halo/game: error w` |
| JurassicPark | ELF32 i386 | `libGLU.so.1`, `libPhysXLoader.so.1`, `libSDL-1.2.so.0`, `libglut.so.3`, `libtheora.so.0` | niet gestart | niet gestart | `/home/deck/.local/share/ravengun/runtime/views/JurassicPark/game: erro` |
| PacmanChomp | ELF32 i386 | `libXrandr.so.2` | niet gestart | niet gestart | `/home/deck/.local/share/ravengun/runtime/views/PacmanChomp/game: error` |
| Terminator | ELF32 i386 | `libGLU.so.1`, `libPhysXLoader.so.1`, `libglut.so.3`, `libtheora.so.0` | niet gestart | geen glfix | `/home/deck/.local/share/ravengun/runtime/views/Terminator/game: error ` |

## Wat je hieruit leest

* **De ELF's zijn geen Windows-programma's.** Ze zijn voor Linux gebouwd en deze machine heeft de
  32-bits loader. Bij **1 van de 7** games ontbreekt er zelfs geen enkele bibliotheek.

* **Zes van de zeven komen niet eens voorbij de dynamic linker.** Die stopt met
  `error while loading shared libraries: <naam>: cannot open shared object file`, en noemt precies
  de eerste bibliotheek uit de kolom hierboven. Dat zijn de bibliotheken die `ElfLdr2` in zijn eigen
  proces emuleert (`libGLU`, `libglut`, `libPhysXLoader`, `libtheora`, `libSDL-1.2`, `libesd`,
  `liballeg`, `libfmod`, `libstdc++.so.5`, `libXrandr`).

* **De ene game waarbij alles resolveert, stopt toch.** Galaga Assault heeft geen enkele
  ontbrekende bibliotheek, krijgt zijn venster niet, en sterft met een `SIGSEGV` **zonder één regel
  uitvoer** — ook met onze GL-fix als `LD_PRELOAD`. Met `LD_DEBUG=bindings` is te zien dat de
  dynamic linker daar **al zijn werk afmaakt** (alle `DT_NEEDED` geladen, alle `init`-functies
  aangeroepen) en dat de crash daarna komt, in de opstartcode van de game zelf.

* **Wat dat betekent.** `BudgieLoader` is niet weg te strepen met "zet de ontbrekende
  bibliotheken neer": voor de zes zou dat dezelfde muur opleveren als Galaga Assault — al is dat
  voor die zes **niet gemeten**, het is een gevolgtrekking uit de ene game die alles heeft. De
  ELF's zijn gelinkt om door ElfLdr2 in het geheugen te worden opgetuigd (eerdere metingen: vaste
  GOT-sloten uit de dump, PLT-stubs die `0xdeadbeef` pushen). Een eigen route voor deze familie is
  dus een **eigen ELF-loader**, niet een paar bibliotheken — en dat is werk in de loader-repo.

## Hoe je dit vernieuwt

```bash
cd ~/projects/ravengun-patches
python3 patches/elf-native.py
```
