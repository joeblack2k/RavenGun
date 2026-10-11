# Halo Fireteam Raven — de directe route (ElfLdr2 / BudgieLoader)

*RavenGun, 2026-10-11. Dit is de meetgeschiedenis van de directe start
(`scripts/launch-direct.py Halo --run`) zonder TeknoParrotUi. Instellingen: `game.ini`;
donglegegevens en drop-in: `dropin/`. Algemene bevindingen over de hele ElfLdr2-familie:
`docs/elfldr2-route.md` in de loader-repo.*

## 1. Eindstand van de meting

De game **start en blijft draaien**. Bewijs uit één start (`halo_shot2.sh`, logboek
`/tmp/halo-shot2.log`):

```
Warning SHT_DYNSYM not found in section headers
Warning aborting symbol parsing due to invalid name ptr
Detected Linux game:
┍ SubSystem Detection: for Z:/home/deck/.local/share/ravengun/runtime/halo-g7-tp/halo/game
┝ Sound    API: Unknown
┕ Graphics API: Unknown
Worldspace calib point set to: (400.000000, 300.000000)
cmgr_init
cmgr_thread_main: thread begun
mo_mode_loop_thread: begun
Cpmgui start 2731
Cpmgui end 3354
Gun 0 Cal: (0.000000, 0.000000)
```

en het venster van de game, gemeten met `xdotool` terwijl diezelfde start liep:

```
'TeknoParrot (ElfLoader64) - HALO Fireteam Raven'   X=0 Y=0 WIDTH=1920 HEIGHT=1080
Z:\...\ElfLdr2\x64\BudgieLoader_x64.exe Z:\...\halo-g7-tp\halo\game      (proces leeft)
```

Vijf opeenvolgende startproeven (`halo_retry.sh`) hielden de lader **95 s** in leven en het venster
stond er elke keer; de start werd daarna door de proef zelf afgebroken.

De starttest van dezelfde dag (`bash /home/deck/starttest-batch.sh 30 Halo GoGoStrike PacmanChomp`)
zegt voor Halo `start-niet 1` met als laatste regel `klaar (exitcode gestopt)`: de tweede kolom is
"de launcher leefde nog op 30 s", en `gestopt` betekent dat de lader op dat moment nog liep. Dat is
dezelfde uitkomst als hierboven; alleen het `START`-label komt er niet uit, en waarom staat in §4.

**Wat er nog niet is: het beeld.** Een `ffmpeg -f x11grab`-opname van `:0` tijdens de start geeft een
volledig zwart beeld (1920×1080, 8605 bytes) en de lader meldt `Graphics API: Unknown`. De game
draait dus en zijn eigen code loopt, maar er is nog geen bewezen getekend beeld. Dezelfde stand is
eerder opgeschreven voor de 32-bits zusjes (`docs/rt-glfix.md`: "er komt nog geen beeld").

## 2. `Warning SHT_DYNSYM not found in section headers` — wat het betekent, en dat het onschuldig is

Het `game`-bestand van Halo is een ELF **zonder enige sectiekop**:

```
$ file game
ELF 64-bit LSB executable, x86-64, ... dynamically linked, no section header

$ readelf -h game
  Start of section headers:          0 (bytes into file)
  Number of section headers:         0
  Section header string table index: 0

$ readelf -S game
There are no sections in this file.
```

Er is dus geen sectie van het type `SHT_DYNSYM`, en de symboolontleder van ElfLdr2 zoekt de
dynamische symbolen daar. Hij waarschuwt en gaat verder. **De symbolen zijn er wel**, maar via het
dynamische segment (`DT_HASH`/`DT_SYMTAB`/`DT_STRTAB`):

```
$ readelf -d game
 0x0000000000000005 (STRTAB)   0x405968
 0x0000000000000006 (SYMTAB)   0x401990
 0x0000000000000004 (HASH)     0x1f48d28
```

Dat is precies de route die onze eigen hook gebruikt; die leest 220 dynamische symbolen uit
`DT_HASH` (`hasphook.log`):

```
[ravengun-hasphook] aantal symbolen uit DT_HASH: 220
[ravengun-hasphook] dynsym: 220 symbolen
```

**De waarschuwing is niet de oorzaak en niet nieuw.** Twee metingen:

1. **Een ElfLdr2-game die wél doorloopt geeft dezelfde twee regels.** PacmanChomp-Mania, één start,
   `/tmp/rg-starttest-PacmanChomp.log`:

   ```
   Warning SHT_DYNSYM not found in section headers
   Warning aborting symbol parsing due to invalid name ptr
   Detected Linux game: Pac-Man Chomp
   ┍ SubSystem Detection: ...
   ┕ Graphics API: SDL
   -------------
   Starting Game
   -------------
   [SDL]: SDL initialized. (flags=00100020)
   [SDL]: SetVideoMode initialized: width=1920 height=1080 bpp=24 flags=00000023
   ```

   GalagaAssault geeft dezelfde waarschuwing (`Detected Linux game: Galaga (1.38 US)`,
   `Graphics API: SDL`, `Starting Game`).

2. **In de periode dat Halo volledig speelbaar was, stond de waarschuwing er ook.** Het logbestand
   van de kern in de view (`teknoparrot_log.txt`, 10 oktober 18:07) heeft dezelfde twee regels, en
   daarna `SubSystem Detection`, `cmgr_init`, `cmgr_thread_main: thread begun`,
   `mo_mode_loop_thread: begun`. Zie ook `findings/Halo.md` op de NAS, regel 12: "De kern meldt voor
   Halo geen herkenningsregel, alleen 'Warning SHT_DYNSYM not found in section headers' bij het
   laden van `game`."

Het `game`-bestand is het originele bestand: NAS en view zijn byte-identiek
(`md5sum` = `279f21163f0c3832d4d180b5e2671b58`) en de maat klopt met het eigen profiel
(`[Identify] Size = 33554432`). Er is dus niets aan de ELF veranderd of "gepatcht" geraakt, en er
hoeft ook geen patch op de ELF: de symbolen zijn langs `DT_HASH` te vinden.

## 3. De echte oorzaak van `exitcode 1`: de inline-hook in `LD_AUDIT`

De oorspronkelijke drop-in zette twee bibliotheken op `Preload = true`. De launcher maakt daar
`LD_AUDIT` van (`launch-direct.py`, `dropin_plan()`), en dat is voor een ElfLdr2-game fataal: de
lader is een **Cygwin-programma dat de game-ELF in zijn eigen proces laadt**, dus een
auditbibliotheek komt in het proces van de Cygwin-lader terecht, niet in een schoon spelproces.
Gemeten met losse proeven (`probe2.sh`; elke proef start `ElfLdr2/x64/BudgieLoader_x64.exe` met
precies de omgeving van de launcher, met één variabele erbij):

| proef | omgeving | exitcode | game loopt? |
|---|---|---|---|
| P0 | niets extra | 124 (na 20 s nog levend) | ja |
| P1 | `LD_AUDIT` = emulator + hook | **1** | **nee** |
| P2 | alleen `RG_DROPIN_PRELOAD` | 124 | ja |
| P3 | alleen `RAVENGUN_HASP_*` | 124 | ja |
| P4 | alleen `TP_LAUNCH_SESSION_ID` | 124 | ja |
| P5 | alles (zoals de launcher het deed) | **1** | **nee** |
| P6 | `LD_AUDIT` = **alleen de hook** | **1** | **nee** |
| P7 | `LD_AUDIT` = alleen de emulator | 124 | ja |

De laatste regel van elk sterfgeval is dezelfde; de hook meldt nog dat hij het spelbeeld heeft
gevonden en dan is de lader weg (`hasphook.log`):

```
[ravengun-hasphook] spelbeeld op 0x00400000
```

Daarna volgt in datzelfde proces geen enkele regel meer, en de game komt nooit aan zijn eigen code
toe. **De emulator alleen in `LD_AUDIT` is dus onschadelijk** (P7) — glibc weigert hem sowieso met
`undefined symbol: la_version; ignored` — en `RG_DROPIN_PRELOAD` en de `RAVENGUN_HASP_*`-variabelen
doen niets kwaad.

### De reparatie

De launcher heeft er een sleutel bij gekregen: **`Audit = false`** in een drop-in-sectie houdt dat
bestand uit `LD_AUDIT`, terwijl `Preload = true` het pad in `RG_DROPIN_PRELOAD` laat staan (voor de
native route en een eigen lader). In `dropin/dropin.ini` staat nu:

```ini
[ravengun-hasp-x64.so]
To = .
Preload = true
Audit = false
Env = RAVENGUN_HASP_DIR=ravengun-hasp;RAVENGUN_HASP_GAME=Halo

[libravengun-hasphook-x64.so]
To = .
Preload = true
Audit = false
Env = RAVENGUN_HASP_ELF_LOG=hasphook.log
```

En het plan laat zien dat het auditkanaal leeg is en het preloadkanaal niet:

```
'audit': [], 'geen_audit': ['.../ravengun-hasp-x64.so', '.../libravengun-hasphook-x64.so'],
'env': {..., 'RG_DROPIN_PRELOAD': '.../ravengun-hasp-x64.so:.../libravengun-hasphook-x64.so'}
```

### Waarom dit eerder niet gevonden is

De vorige ronde heeft `LD_AUDIT` "uitgesloten" met de starttest, en dat was een **valse
negatief**: de starttest noemt een game pas `START` bij de regel `Have fun` of `Loading game...`.
Die twee bestaan in de ElfLdr2-route niet — gemeten met `grep -a`:

* `game` van Halo: 0 treffers voor beide;
* alle vier `BudgieLoader`-binaire bestanden en `ElfLdr2/TeknoParrot.dll`: 0 treffers voor beide;
* de logboeken van alle zeven ElfLdr2-games: `Have fun=0 Loading game=0`.

Elke ElfLdr2-game leest dus `start-niet`, ook als de lader na de volle 30 s nog leeft.

## 4. Wat er nog nodig is

1. **Beeld.** De game loopt en de lader meldt `Graphics API: Unknown`; er is nog geen getekend beeld
   gemeten. ElfLdr2 herkent de game niet als spel (`Detected Linux game:` is leeg, terwijl
   GalagaAssault `Galaga (1.38 US)` en Terminator `Terminator (v1.25 US)` melden), en zonder
   herkenning installeert de lader zijn grafische brug niet. Dat is de volgende te meten stap; het is
   geen drop-in die dit oplost.
2. **De dongle in deze route.** De inline-hook die de statisch gelinkte `hasp_*`-aanroepen moet
   aanhaken kan hier niet geladen worden: in `LD_AUDIT` sterft de lader (§3) en `LD_PRELOAD` is
   fataal voor Cygwin (`fatal error - error while loading shared libraries`, gemeten: variant D van
   `probe_loader.sh`). Zolang de game zelf niet om de dongle vraagt is dat geen blokkade — de game
   komt voorbij zijn start en blijft in het kalibratiescherm staan — maar de dongle-emulatie is in
   deze route nog niet aangesloten. Wie dat wil oplossen moet de hook **binnen het spelproces**
   krijgen (een eigen lader, of ElfLdr2 een `DT_NEEDED`-invoer op de game-ELF laten laden), niet via
   de omgeving van de Wine-start.
3. **De starttest.** Zolang `starttest-batch.sh` alleen `Have fun` en `Loading game...` kent, kan
   geen enkele ElfLdr2-game `START` worden. Voor Halo is een bruikbare eigen maatstaf: de regels
   `cmgr_init`, `cmgr_thread_main: thread begun`, `mo_mode_loop_thread: begun` en `Cpmgui start`
   zijn code van de game zelf (ze staan in `game`, niet in de lader: `strings -a game | grep cmgr`),
   plus het venster `TeknoParrot (ElfLoader64) - HALO Fireteam Raven` van 1920×1080.
4. **De JVS-brug bij gelijktijdige proeven.** De pijpnaam `\\.\pipe\TeknoParrot_JVS` is
   procesbreed; bij een tweede start meldt de helper `CreateNamedPipe(...) failed: 231`
   (`ERROR_PIPE_BUSY`) en krijgt de game geen IO. Ook ruimt `starttest-batch.sh` op met
   `pkill -f "$BASIS"`, en `BASIS` is bij deze games `game` — de bestandsnaam van zes games in deze
   verzameling, dus een lopende starttest van een andere game kan deze start doden. Eén van de
   proeven hier is daardoor na 36 s gestopt (`lader gestopt (exitcode 1)`) terwijl de volgende vijf
   95 s bleven lopen.
