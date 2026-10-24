# Ghostbusters (ICE / Play Mechanix / Raw Thrills, ElfLdr2, profiel RawThrillsGUN)

32-bits Linux-ELF, gestart door de ElfLdr2-lader van TeknoParrot (`ElfLdr2/BudgieLoader.exe` onder
Wine). Deze map bevat `game.ini` en `dropin/`.

**Kort:** de game vraagt geen dongle maar een **`pm`-map**: alle bestanden van de game moeten in een
map met de naam `pm` staan, en het pad naar de elf moet daarbinnen wijzen. Dat is gemeten, niet
afgeleid. De map zelf is **geen drop-in** — de inhoud is de eigen dump van de eigenaar (2,7 GB); de
map komt uit de view. Zie hieronder.

---

## 1. Wat de `pm`-map is

`pm` is de **installatiewortel van de game op de echte kast** (Play Mechanix). De game opent
absolute paden die allemaal met `/pm` beginnen. Bewijs, uit de game zelf:

| Bewijs | Bestand + regel |
|---|---|
| `SPLASH_COMMAND="splash -s -u0 /pm/utils/splash/"` | `GhostBusters/utils/check_usb_drivers.sh:30` |
| `SPLASH_COMMAND="fbv -c -i /pm/utils/splash/"` | `GhostBusters/utils/check_usb_drivers.sh:34` |
| `/pm/g6/ghostb/data`, `/pm/g6/ghostb/pmuser`, `/pm/g6/ghostb/efilemaps`, `/pm/g6/ghostb/pmuser/gamelog.txt`, `/pm/g6/ghostb/data/prod/attract/atorder.txt` | de elf `GhostBusters/game` (strings) |
| `/pm/game` | de elf `GhostBusters/game` (strings) |
| `/pm/utils/reset_usb_bus.sh` | de elf `GhostBusters/game` (strings) |
| `/pm/irgun/firmware/prom_%02x.mcs` | de elf `GhostBusters/game` (strings) |

Narekenen (alleen lezen):

```bash
NAS=/home/deck/mnt/spidernas_emulation/Teknoparrot
strings -a "$NAS/GhostBusters/game" | grep -n "/pm"
nl -ba "$NAS/GhostBusters/utils/check_usb_drivers.sh" | sed -n '28,36p'
```

De dump op de NAS heeft die bestanden **in de wortel** van `GhostBusters/`: `game` (11.057.832
bytes), `g6/` en `utils/` — samen 2,7 GB. Er is geen `pm`-map in de dump.

### Waar de eis vandaan komt (de lader)

De ElfLdr2-kern (`ElfLdr2/TeknoParrot.dll`, 32-bits) bevat de regel
`GAME PATH CHECK FAILED!... path=%s`; de UI-binary (`TeknoParrotUi`) bevat de uitleg die de
eigenaar te zien kreeg:

```
GAME PATH CHECK FAILED!...
This game need all his files to be in a "pm" directory.

Please move every files inside your game/dump folder to the newly created "pm" directory inside it.
Then please re-set the path to the game elf in TeknoparrotUI settings before restarting the game.

Now closing...
```

De padcontrole kijkt naar de **map van de elf**, niet naar de elf zelf. Twee metingen op de Deck
(alleen lezen; de view is lokaal, de NAS is niet beschreven):

```
# 1. de elf in de wortel van de view (de oude situatie) -> AFGEKEURD
game exe    : Z:/home/deck/.local/share/ravengun/runtime/views/GhostBusters/game
game path   : Z:/home/deck/.local/share/ravengun/runtime/views/GhostBusters
Detected Linux game: Ghostbusters (1.17 US)
GAME PATH CHECK FAILED!... path=Z:/home/deck/.local/share/ravengun/runtime/views/GhostBusters

# 2. de elf in pm/ -> GOEDGEKEURD, de lader loopt door
game exe    : Z:/home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm/game
game path   : Z:/home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm
(geen GAME PATH CHECK FAILED)
┍ SubSystem Detection: for .../views/GhostBusters/pm/game
-------------
Starting Game
-------------
```

Meting 2 is herhaalbaar met:

```bash
V=~/.local/share/ravengun/runtime/views/GhostBusters
W=~/.local/share/ravengun/wine/proton-11.0-2/bin/wine
export WINEPREFIX=~/.local/share/TeknoParrotUI/prefixes/shared/wine
export TP_REMOTETHREAD=1 tp_windowed=1 WINEDEBUG=-all TP_LOGTOFILE=1
cd "$V" && timeout 40 "$W" ElfLdr2/BudgieLoader.exe 'Z:\home\deck\.local\share\ravengun\runtime\views\GhostBusters\pm\game'
grep -a "PATH CHECK" "$V/teknoparrot_log.txt"   # niets = goed
```

## 2. Wat er dan in `pm` hoort, en waar het vandaan komt

De hele game: `game`, `g6/`, `utils/` — dus precies de inhoud van de dumpmap `GhostBusters/` op de
NAS. De map moet zo heten omdat de game zelf `/pm/...` opent; de lader weigert de start zolang de
map van de elf niet `pm` heet.

## 3. Waarom dit geen drop-in is (en wat er dan wel moet)

`docs/dropins.md` zegt twee dingen die dit geval beslechten:

1. `dropin/` bevat **wat wij zelf bouwen**, nooit een kopie van bestaande bestanden. De inhoud van
   `pm` is de dump van de eigenaar (2,7 GB) — die hoort niet in deze repo.
2. Een drop-in zet **bestanden** in de view; de motor (`dropin_materialiseer`) maakt mappen aan en
   vervangt een symlink-map door een echte map met symlinks naar de inhoud, maar **maakt zelf geen
   symbolische koppeling**. De `pm`-map kan dus niet door een drop-in ontstaan.

De map `pm` hoort daarom in de **view**, en het pad in het **profiel**:

```bash
# eenmalig: de view van deze game (symlink-boerderij naar de NAS)
cd ~/projects/ravengun
python3 scripts/tp-add-game.py GhostBusters \
    "/home/deck/mnt/spidernas_emulation/Teknoparrot/GhostBusters/game" --view

# en de pm-map: een ECHTE map in de view met koppelingen naar de gamebestanden, zodat alles in pm
# staat en de launcher zijn eigen teknoparrot.ini in pm kan schrijven (dat kan niet op de NAS).
# Dit is precies wat dropin_materialiseer() doet zodra er een drop-in met het voorvoegsel pm/ komt.
V=~/.local/share/ravengun/runtime/views/GhostBusters
G=/home/deck/mnt/spidernas_emulation/Teknoparrot/GhostBusters
mkdir -p "$V/pm"
ln -sfn "$G/game"  "$V/pm/game"
ln -sfn "$G/g6"    "$V/pm/g6"
ln -sfn "$G/utils" "$V/pm/utils"
```

Daarna wijst `[Identify] Path = pm/game` in `game.ini` de lader naar de elf in de `pm`-map. Het plan
laat dat zien:

```
$ python3 scripts/launch-direct.py GhostBusters --plan
game: /home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm/game
cwd:  /home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm
dropins: {'base': '/home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm', 'files': [], ...}
```

`dropins.base` is dus de `pm`-map: **elk bestand dat wij hier later in `dropin/` zetten met het
voorvoegsel `pm/` komt precies in de map van de game zelf.** Vandaag staat er geen bestand: er is
geen dongle nodig (zie §4) en er is nog geen GL-fix voor deze game. De secties staan als vorm in
`dropin/dropin.ini`.

Met `pm` als echte map in de view schrijft de launcher zijn `teknoparrot.ini` gewoon in `pm/`
(`bestand (schrijven) .../views/GhostBusters/pm/teknoparrot.ini`). Zou `pm` een koppeling naar de
gamemap op de NAS zijn, dan weigert hij dat (`bestand (nas) ...`) - de NAS is alleen-lezen.

## 4. Dongle

Deze map heeft **geen `dongle.json`** — en dat is de machineleesbare betekenis van "geen dongle"
(`docs/dropins.md`). Het profiel `GameProfiles/GhostBusters.xml` (revisie 7) heeft ook geen
dongle-velden. Wat de game zelf wél in huis heeft (strings in `game`): `DongleInitialize() FAILED!`,
`Game dongle initialization failed!`, `ReInitAladdinDongles`, `hasp_login`, `nethasp.ini` — de
HASP/Aladdin-laag zit dus in de elf. Of die na de padcontrole alsnog om een dongle vraagt is **niet
gemeten**: de game komt na "Starting Game" niet in beeld (exitcode 255 in een SSH-sessie zonder
gamescope-seat), en dat kan net zo goed aan het beeld of aan de dongle liggen. De eigenaar zegt: geen
dongle. Daarom staat `[Dongle]` hier niet aan.

## 5. Wat niet is vastgesteld, en welke aannames er zijn

* **Niet gemeten:** wat de game ná de padcontrole blokkeert. De lader start door; de game komt in
  deze SSH-sessie niet in beeld. Kandidaten: de HASP/Aladdin-dongle, een werkende OpenGL-context
  (dezelfde klacht als bij Jurassic Park/Galaga Assault/Pac-Man), of het USB-gun-bord.
* **Aanname:** dat de game zijn bestanden in `pm/` vindt. De lader vertaalt de absolute
  `/pm/...`-paden van de game; in de view staan `game`, `g6/` en `utils/` zowel onder
  `views/GhostBusters/` als (als koppeling) onder `views/GhostBusters/pm/`, dus elke vertaling komt
  goed uit. Dat is niet per bestand nagerekend (de game komt daar niet ver genoeg voor).
* **Aanname:** dat `pm` de enige eis van de padcontrole is. Dat is wat de meting laat zien: met
  `pm/game` (en zelfs met alleen de elf in `pm/`, zonder `g6/` en `utils/`) gaat de controle door.
* **Niet vastgesteld:** of de eigenaar de dump op de NAS wil herordenen (alles in een `pm`-map) in
  plaats van de view-koppeling te gebruiken. De koppeling is omkeerbaar en raakt de NAS niet.

## 6. Verifiëren

```bash
cd ~/projects/ravengun
python3 tools/verify-all.py GhostBusters
python3 scripts/launch-direct.py GhostBusters --plan
```

`findings/GhostBusters.md` op de NAS blijft de meetgeschiedenis van de game zelf.

## Zonder ElfLdr2 (gemeten)

Deze game is een **gewone Linux-ELF** (`ELF32 i386`), geen Windows-programma: hij heeft
`/lib/ld-linux.so.2` als interpreter en vraagt dezelfde bibliotheken als elk Linux-spel. Toch
start hij niet als je hem los start — en dat is gemeten, niet aangenomen:

```
/home/deck/.local/share/ravengun/runtime/views/GhostBusters/pm/game: error while loading shared libraries: libGLU.so.1: cannot open shared object file: No such file or directory
```

Zonder ElfLdr2 stopt hij dus al bij de dynamic linker: **`libGLU.so.1`** staat niet op deze
machine. In totaal mist deze machine er 4: `/usr/lib/libcsv.so`, `libGLU.so.1`, `libesd.so.0`, `libglut.so.3`.
Dat zijn precies de bibliotheken die `ElfLdr2` in zijn eigen proces emuleert.

De volledige meting over de hele familie staat in `patches/ELF-NATIVE.md`; de generator is
`patches/elf-native.py`.
