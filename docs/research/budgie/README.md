# Terminator en Crossfire zonder BudgieLoader (onderzoek 2026-10-17)

> **Vervolg 2026-10-18.** De skeletten hieronder zijn opgevolgd door `tools/rg-t4-io/rg_t4_io.c` (Terminator: JAMMA-bordkant uit het
> bordbeeld, richtpunt, glutInit) en `tools/rg-cf-io/rg_cf_io.c` (Crossfire: `/dev/mjg`-records uit het bordbeeld, `/dev/hda`, `/dev/p37c`
> met de iButton), met toetsen `tests/t4_io_test.sh` en `tests/cf_io_test.sh`. Nieuw ten opzichte van de tekst hieronder: (1) Terminators
> richtpunt zit in twee floats per geweer op 0x8b0da00 + geweer*0x30 (game-functie 0x842e36f, 800x600) en is dus zonder de IR-camerageweren
> (cmgr/ttyUSB) te schrijven; (2) Crossfire heeft een iButton (Dallas DS1991) op `/dev/p37c` als opstartcontrole (0x805e7ca; zonder komt de
> game niet voorbij de opstart), `/dev/p37c` is dus geen stom registerbeeld; de irq-teller op record +0x0c moet strikt oplopen (0x804aa40);
> het richtpunt is lichtpuls-samples (x | y<<12) met scherm_x = raw*0.6946-170, scherm_y = raw-25 (0x804a63b). De profielen voor de patches-repo
> staan in `patches-out/<Game>/` (nog `enabled: false`).

**Vraag:** twee games gebruiken op de directe route nog `ElfLdr2/BudgieLoader.exe` uit de TeknoParrot-map (`rg-direct --tp-refs`:
Terminator 5 verwijzingen, CrossfirePaintball 3). Wat is er nodig om ze via de native route (`native.json`, `rg-elfloader`) te starten
zoals JP/NERF/Halo?

**Antwoord in een alinea:** een volledige eigen BudgieLoader is niet nodig. Beide games zijn gewone i386-ELF's zonder packer; Terminator
draaide al 19 minuten attract op `rg-elfloader` (bench 2026-10-13, `patches/Terminator/ATTRACT-RUN.md`), en Crossfire is een klein
programma (161 KB) waarvan alle kastkoppelingen na te bootsen zijn met een LD_PRELOAD-schim. Wat BudgieLoader voor beide doet, is
"de kast nadoen": apparaten (`/dev/ttyS0`, `/dev/sierra2`, `/dev/dsp`, `/dev/js0`, `/dev/p37c`, `/dev/mjg`, `/dev/hda`), de dongle en
de invoer. Die stukken zijn deels al in de repo (ptylink, oss-stub, HASP-haak), deels nu als opstartskelet toegevoegd (hieronder),
en deels nog open (het richten). **Niets hiervan is op de bench of tv gedraaid in de vorm van de kandidaat-profielen**; Terminator
blijft op de oude route `working` en wordt hier niet aangeraakt (de kandidaten staan op `"enabled": false`).

Bestanden:

| pad | wat |
|---|---|
| `patches/Terminator/native.json` | kandidaat-profiel (uit), voor `patches/Terminator/` in de patches-repo |
| `patches/CrossfirePaintball/native.json`, `patches/CrossfirePaintball/data/proc-version` | idem (het tweede is de kernelregel die de game controleert) |
| `src/rg_t4_jamma.c` | Terminator: de bordkant van de seriele JAMMA-lijn + glutInit-herschrijving (skelet) |
| `src/rg_cf_io.c` | Crossfire: `/dev/p37c`, `/dev/mjg`, `/dev/hda`, `iopl` (skelet) |
| `src/build.sh`, `src/test_cf_io.c`, `src/test_t4_jamma.py` | bouwen (kaal `gcc -m32`) en een eigen toets zonder spel: `bash docs/research/budgie/src/build.sh` -> "ALLES GOED" (2x) |

De twee bibliotheken zitten **nog niet in CMake** (de kandidaat-profielen verwijzen naar `@build/lib32/librg-cf-io.so` en
`librg-t4-jamma.so`); verplaatsen naar `tools/` en een regel in `CMakeLists.txt` naast `librg_ptylink.so` is de eerste stap als de
eigenaar de route wil.

## 1. Terminator Salvation

### Wat al bewezen is (uit de patches-repo, bench 2026-10-13, eigen lader in een bwrap-sandbox op de tv)

- ongepakt, `rg-elfloader` laadt hem, hij komt door zijn 49 initstappen en tekent; attract-loop 19 min 5 s gezien (110 opnames).
- de dongle: HASP-client zit statisch in `game`; `patches/Terminator/dropin/` (hook + adreslijst `hasp-haken.ini` + emulator +
  geheugenbestanden voor fileid 0xFFF0/0xFFF2 + de `hasp/`-antwoordtafel) is genoeg voor "Game must be registered" -> weg; attract.
- invoer: START e.d. komen van een seriele "JAMMA I/O"-lijn op `/dev/ttyS0` (frameformaat gemeten, zie `rg_t4_jamma.c`).
- extra's die de run nodig had: `force_glsl_version=120` (shaders), `-display :0.0` in `glutInit` vervangen, `PR_SET_PTRACER_ANY`,
  `/g3` = spelmap, `/T4User` overlay, `/dev/dsp` stil.

### Wat de bench nog "alleen in een testscript" had (ATTRACT-RUN.md §6) en waar het in de kandidaat nu staat

| harness-onderdeel (§6) | in `native.json` | status |
|---|---|---|
| 1 sandbox-indeling (`/g3`, `/T4User`, view) | `mounts`: `/g3` overlay (cwd), `/T4User` overlay | nieuw, **niet gedraaid**; de harness gebruikte een kopie van `game` + symlinks in een view; hier de overlay |
| 2 apparaten: ptys voor `/dev/ttyS0` en `/dev/sierra2`, `/dev/js0` | `librg_ptylink` (`RG_PTYLINK`, twee koppels) en `files`: `/dev/null` -> `/dev/js0` | ptylink is bewezen (Target: Terror Gold); voor Terminator niet gedraaid |
| 3 de seriele I/O-kant (START/munt/TEST als frames) | `librg-t4-jamma.so` | **skelet**, getoetst tegen een proef-pty; leest de lijn leeg, nulmeting, flanken uit een evdev-toetsenbord of `RG_T4_AUTOSTART` |
| 4 `RG_FAKE_DSP` | `librg-oss-stub.so` | bestaat (GoGoStrike, TTG); beantwoordt de SNDCTL-aanroepen die Allegro stuurt; of het precies de set van Terminator dekt (GETBLKSIZE=P4, GETFMTS, GETCAPS, GETOSPACE) is **niet gemeten** |
| 5 `glutInit`-herschrijving | in `rg_t4_jamma.c` (`RG_GLUT_DISPLAY_FROM_ENV`) | overgenomen uit de proefbibliotheek `exit-log.c`; skelet |
| 6 ptracer-permit | env `RG_ELF_PTRACER_ANY=1` (zoals AliensArmageddon) | bestaat in de lader; voor Terminator niet gedraaid |
| 7 `dropin.ini` toepassen op de native route | `files`/`seed`/`libs`/`preload`/`env` (patroon van AliensArmageddon) | nieuw; het hook-pad (`RAVENGUN_HASP_ELF_ADRESLIJST=/rg/hasp-haken.ini`) is van Aliens overgenomen |
| 8 `force_glsl_version=120` | env | direct |
| 9 gamescope/tv | de gewone native route | bestaat |

### Wat open blijft (en dus niet in "werkt")

1. **Richten.** De geweren zitten niet in het kandidaat-profiel. Het spel zoekt een `ttyUSB*`-poort (IR-camera-bord) via `find /sys/devices ...
   > /g3/irgun/cmgrNNN.txt`; `/dev/js0` houdt alleen assen over. Op de oude route komt het richten via de TeknoParrot-kern (tp-shm,
   RawThrillsGUN). Een native vervanger moet of het cmgr/ttyUSB-protocol nadoen (zoals Halo/JP met de RIO-brug) of de assen op `/dev/js0`
   aanbieden; het eerste is de grote klus. Zonder dit kun je wel navigeren (toetsen) maar niet richten of schieten.
2. Trekker/pomp/grenade horen op de JAMMA-lijn (bytes 2 en 3); `rg_t4_jamma.c` stuurt ze nog niet uit een muis/Move.
3. Netwerk: het spel probeert TCP naar `67.217.33.130:8985` (de sandbox heeft `--unshare-net`; harmless in de bench, maar het kost tijd per poging).
4. Geluid: stil. Echt geluid vraagt een OSS->PipeWire-schim; niet gedaan.
5. De "file check"-waarschuwing (een START per start) blijft; `include/frag_shadmap.gls` op de NAS is gewijzigd (ATTRACT-RUN §4.3).
6. De unit meldt zich als **UK** (landbyte 3 in de dongledump). Of de eigenaar dat wil is zijn keuze.

### tv-testrecept Terminator

Voorwaarde: de coordinator heeft de bibliotheken in CMake gezet en gebouwd (`build/lib32/librg-t4-jamma.so`, `librg-cf-io.so`) en het
profiel (tijdelijk, met `"enabled": true`) als `patches/Terminator/native.json`; zijn `game.ini` zegt nu `Mode = direct`, dus de coordinator
moet voor de proef de native route kiezen en daarna terugzetten: de directe route (dubbelklik werkt nu) blijft de werkende start tot dit slaagt. PhysX: eerst `ravengun --install-component physx-linux` en de
drie profielen uit `../physx/` toepassen (zonder dat mist `@lib32/libPhysX*`). Dan:

1. `rg-direct --tp-refs` -> Terminator moet 0 verwijzingen tonen (nu 5, lader `ElfLdr2/BudgieLoader.exe`).
2. Dubbelklik in de app (of de native route van de daemon). Verwacht in de eerste 30 s: bootteksten, na ~25 s een automatische START
   (`RG_T4_AUTOSTART`) die de waarschuwing "Press Start To Continue" weghaalt, daarna attract (demo-scenes, "FREEPLAY").
3. Controles tijdens de run (op de bench-pc, niet op de tv): `grep -c jamma_msg <syslog>` moet 0 blijven (de lijn wordt leeggelezen);
   `/proc/<pid>/maps` toont de vier PhysX-bibliotheken onder `/rg/lib`; het spellogboek meldt geen `DongleInitialize() FAILED`.
4. Toetsen 1/2 (START), 5/6 (munt), 8 (TEST) werken als `RG_T4_KBD` naar het virtuele toetsenbord van de daemon wijst; kijk of TEST het
   operatormenu opent (zoals in ATTRACT-RUN §2.3).
5. Wat niet te verwachten is: richten en schieten. Slaagt 1-4 niet, laat het profiel uit; de oude route blijft Terminators werkende start.

## 2. Crossfire: Maximum Paintball

Alles hieronder is statisch gemeten aan `med_pball` (ELF32, i386, ET_EXEC, gestript, 161.492 bytes, NEEDED: glut, GLU, GL, pthread, libc, m)
en `mjg.o` (de kernelmodule van de kast, **niet** gestript, met namen) uit de spelmap; er is niets gestart. Het kastscript `run`
doet: `insmod mjg.o`, `/data` mounten, `resdip` (kiest 640x480 of 800x600), dan `xinit med_pball -- -layout MedLayout`.

| onderdeel | gemeten (adres in `med_pball`) | native oplossing |
|---|---|---|
| poort-I/O | `geteuid()` (`0x805e7fd`): 0 -> `iopl(3)` + `in`/`out` in twee kleine functies (`0x805e940` inb, `0x805e95f` outb; de enige twee in/out in de hele binary); anders `open("/dev/p37c", O_RDWR)` en 1 byte `read`/`write` per poortactie. Alleen poort `0x37c` wordt geaccepteerd (anders `printf` + `exit(-1)`). | `/dev/p37c` nadoen (`rg_cf_io.c`): byte schrijven = adres/data-nibbles (zoals `mj_set_addr`/`mj_write_reg_now` in `mjg.o`), byte lezen = register. In de sandbox is euid niet 0, dus de apparaatroute geldt. |
| geweren, schakelaars | `open("/dev/mjg", O_RDWR)` (`0x804a5ae`; mislukt = `bail`, de game stopt); `read()` levert een record van `0xb8` bytes of 0 (`mj_read`: ring van 8, 0 = niets nieuws); ioctl `0x40044d02..09` (`_IOW('M', 2..9, int)`) | `/dev/mjg` nadoen: een record per 10 ms, ioctl slaagt |
| record-indeling (uit `stuff_curr_mjd` in `mjg.o`) | `+0x00` 2 bytes (`mj_read_misc_guns`, adres `0x5e`), `+0x04` 4 bytes schakelaars (adres `0x00`), `+0x08/+0x0c` irq-tellers, `+0x10..0x2f` 32 bytes (adres `0x60`: geweerdata), `+0x30/+0x34` muntentellers (stijgende flank van bit `0x200000`/`0x100000` in `+0x04`), `+0x38` = `0x7d`, `+0x3c` volgnummer, `+0x40`/`+0x44` byte van adres `0x5d`, `+0xa8` bit `0x40` van adres `0x81` | het skelet vult `+0x04` (via `RG_CF_SWITCHES`), `+0x38`, `+0x3c`; de rest nul |
| HDD-serienummer | `open("/dev/hda", O_NONBLOCK)` + `ioctl(HDIO_GET_IDENTITY=0x30d)` (`0x8056da9`); het serienummer (offset 0x14, 20 bytes) wordt vergeleken met de eerste `0x15` bytes van `adjust.dat` (`0x8056ec7`; `-` telt niet mee). `adjust.dat` begint met `EM-HYNJ5A194118`. | `rg_cf_io.c` antwoordt met de eerste 20 bytes van `adjust.dat` (of `RG_CF_HDSERIAL`) |
| kernelcontrole | `fopen("/proc/version")` en `strncmp` met `Linux version 2.4.22 (root@midas) (gcc version 3.2.3) #6 Tue Sep 2 17:43:01 PDT 2003` (`0x8056f51`) | `files`: `data/proc-version` -> `/proc/version`; **gemeten** dat bwrap dit toestaat (na `--proc`, `--ro-bind bestand /proc/version`) |
| geluid | OSS `/dev/dsp` (SNDCTL_DSP_SETFRAGMENT/SETFMT/CHANNELS/SPEED) | `librg-oss-stub.so` |
| beeld | GLUT + `glutFullScreen`, 640x480 | zoals nu (4:3 met balken) |
| ELF | NEEDED glut/GLU/GL; geen HASP, geen packer | `rg-elfloader` of gewone ld.so; de profielen met een `loader` kiezen de eerste, de TTG-aanpak (zonder `loader`) is hier ook mogelijk |

Er staat geen kopieerbeveiliging in de dongle-zin (geen HASP); de enige "beveiligingen" zijn het HDD-serienummer en de kernelregel hierboven.
`erocshgih.bin` en `zxcvb` in de spelmap zijn niet onderzocht.

### Wat het skelet kan en niet kan

`rg_cf_io.c` doet alleen wat hierboven staat. Met een stil bord (alles 0) zou de game kunnen opstarten en attract tonen (dat is een verwachting: op de oude route draaien highscores en tutorial zonder invoer, maar op deze route is het niet gemeten), maar **munt, START en geweren doen niets** tot het record
ontcijferd is. Dat is de eigenlijke klus voor Crossfire:

1. de schakelaar-bits van `+0x04` (welke bit is START, trekker, munt 1/2, TEST) uit de verwerkende functie `0x804a862` en haar
   aanroepers;
2. de geweerdata `+0x10..0x2f` (en `+0x00`): functie `0x804a63b` rekent er richtingen mee uit; het zijn waarschijnlijk lichtpuls-tijden van
   de twee geweren, geen x/y. Op de oude route zet TeknoParrot die uit het gedeelde geheugen (tp-shm, RawThrillsGUN) om; een eigen
   vertaling van `(x,y) -> pulstijden` is nodig;
3. de volgorde van de `/dev/p37c`-registers die de game zelf bit-bangt (`0x805df7f`, `0x805e018`, `0x805e0d2`), want het skelet is hier een model.

Police Trainer 2 gebruikt dezelfde motor (overgeslagen door de skip-regel 4:3, maar al het werk aan `rg_cf_io.c` is daar herbruikbaar).

### tv-testrecept Crossfire

Voorwaarde: `librg-cf-io.so` en `librg-oss-stub.so` gebouwd in `build/lib32/` (CMake-regel nodig, zie boven) en het profiel
als `patches/CrossfirePaintball/native.json` (met `data/proc-version`); de directe route blijft de werkende start.

1. Zonder tv, op de bench-pc: `bash docs/research/budgie/src/build.sh` -> beide toetsen "ALLES GOED".
2. Start via de native route (profiel tijdelijk aan). Zet `RG_CF_LOG=1`: in het logboek moeten `open /dev/p37c`, `open /dev/mjg`,
   `open /dev/hda`, `ioctl HDIO_GET_IDENTITY` en `read /dev/mjg (record)` staan. Ontbreken ze, dan opent de game ze op een andere manier
   (bv. `fopen`, niet te onderscheppen) en moet er een andere vorm komen.
3. Verwacht op de tv: de highscores/tutorial/demo-attract (4:3, met balken) en "INSERT COIN". Faalt het met `MegaJamma bail: ...`, dan is
   `/dev/mjg` de oorzaak; `/dev/hda open failed` / `HDIO_GET_IDENTITY failed`: het serienummer; `/dev/p37c must exist or user must be root`: de
   open van `/dev/p37c` is niet onderschept.
4. Munt/START/schieten: **niet te verwachten** (record niet ontcijferd). `RG_CF_SWITCHES=200000` (hex) voor een start-op-bit-test is een gok.

## 3. Wat dit voor de volgorde betekent

- **Terminator** native: de enige grote open klus is het richten (cmgr/ttyUSB of js0); de rest is samengesteld uit bewezen stukken. Daar is
  geen eigen BudgieLoader voor nodig; de omvang van het richtwerk is niet geschat.
- **Crossfire** native: een eigen kastbord-vertaling (MegaJamma-record); kleiner programma, maar de schakelaar- en geweercodering moet
  ontcijferd worden. Beide kunnen op het huidige werkende pad blijven tot die klussen klaar zijn.
- Een **eigen BudgieLoader-vervanger** (alle ElfLdr2-games tegelijk) is veel groter en niet nodig voor deze twee; zie
  `docs/research/eigen-elf-lader.md` en `elfldr2-injectie.md`. De eigen `rg-elfloader` + schimmen per game is al het patroon van JP/NERF/Halo/TTG.
- Zolang `ElfLdr2/BudgieLoader.exe` nodig is, blijft `rg-direct --tp-refs` voor beide games niet-nul (Terminator 5, Crossfire 3 bij een aanwezige TP-map: de lader, `ElfLdr2/`,
  `ParrotData.xml`, `GameProfiles/`; met een verborgen TP-map faalt het plan al op `ElfLdr2/BudgieLoader.exe`).
