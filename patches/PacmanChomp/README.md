# Pac-Man Chomp-Mania

*RavenGun, 2026-10-11. Directe start (`scripts/launch-direct.py PacmanChomp --run`), gemeten op de
Deck. Algemene bevindingen over deze familie: `docs/elfldr2-route.md` in de loader-repo.*

## Stand

De game **komt ver**: de lader ontleedt de ELF, herkent het spel, zet de grafische brug op en zet de
videomodus. Daarna valt de Cygwin-lader om. Uit `/tmp/rg-starttest-PacmanChomp.log` (start van
11 oktober):

```
Warning SHT_DYNSYM not found in section headers
Warning aborting symbol parsing due to invalid name ptr
Detected Linux game: Pac-Man Chomp
┍ SubSystem Detection: for Z:/.../views/PacmanChomp/game
┝ Sound    API: Unknown
┕ Graphics API: SDL
-------------
Starting Game
-------------
[SDL]: SDL initialized. (flags=00100020)
[SDL]: SetVideoMode initialized: width=1920 height=1080 bpp=24 flags=00000023
      0 [main] BudgieLoader 1019 cygwin_exception::open_stackdumpfile: Dumping stack trace to
      BudgieLoader.exe.stackdump
[launch-direct 22:47:18] lader gestopt (exitcode 0); de game volgen
```

De starttest noemt dat `start-niet` (`start-niet 0`, laatste regel `klaar (exitcode 0)`), maar dat
label is misleidend: de game is tot en met `SetVideoMode initialized` gekomen. Het is dus een
**crash in de lader** na het zetten van de videomodus, niet een game die niet start. De stackdump
staat in de view (`BudgieLoader.exe.stackdump`) en is nog niet uitgelezen — dat is de eerstvolgende
stap.

## De twee waarschuwingen zijn onschuldig

`game` heeft nul sectiekoppen (`readelf -h`: `Number of section headers: 0`), dus de symboolontleder
van ElfLdr2 waarschuwt. Hij gaat daarna gewoon verder: `Detected Linux game: Pac-Man Chomp`,
`Graphics API: SDL`, `Starting Game`. De dynamische symbolen staan in `DT_HASH`. Er hoeft dus geen
patch op de ELF.

## Drop-in

`dropin/dropin.ini` zet `librt-glfix.so` op `Preload = true`. Dat is het werkende kanaal: de launcher
zet het pad in `LD_AUDIT`, en dat is precies wat deze 32-bits game nodig heeft voor de cdecl/stdcall
brug (`docs/rt-glfix.md`). **`Audit` blijft hier dus op zijn standaard `true`** — de nieuwe sleutel
`Audit = false` is voor de inline-hooks die het Cygwin-proces van de lader juist kapotmaken
(`docs/elfldr2-route.md`, §3).

## Zonder ElfLdr2 (gemeten)

Deze game is een **gewone Linux-ELF** (`ELF32 i386`), geen Windows-programma: hij heeft
`/lib/ld-linux.so.2` als interpreter en vraagt dezelfde bibliotheken als elk Linux-spel. Toch
start hij niet als je hem los start — en dat is gemeten, niet aangenomen:

```
/home/deck/.local/share/ravengun/runtime/views/PacmanChomp/game: error while loading shared libraries: libXrandr.so.2: cannot open shared object file: No such file or directory
```

Zonder ElfLdr2 stopt hij dus al bij de dynamic linker: **`libXrandr.so.2`** staat niet op deze
machine.
Dat zijn precies de bibliotheken die `ElfLdr2` in zijn eigen proces emuleert.

De volledige meting over de hele familie staat in `patches/ELF-NATIVE.md`; de generator is
`patches/elf-native.py`.
