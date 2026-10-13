# De officiele startmanier van Halo, en wat wij anders doen

*RavenGun, 2026-10-13. Gezocht op verzoek van de coördinator, zonder bench. Alles is **lezen** geweest:
de NAS is alleen gelezen, er is niets gestart. Per bevinding staat er **GEMETEN** of **AFGELEID** bij,
met het bestand en de regel erbij.*

## 1. Waar de officiele start staat (GEMETEN)

De spellmap op de NAS is **niet** `Halo Fireteam Raven` maar:

```
/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/
```

Daar staat `utils/run_game.sh` -- het script dat de kast zelf aanroept. De startregel is
(`utils/run_game.sh`, regels 35-36):

```sh
cd /pm/g7/halo
LD_PRELOAD=/pm/g7/halo/halo.so ${GAMEDIR}/game2 -f${SCREEN_RES} -n
```

en in `utils/run_game.sh_original` (regel 35) staat het origineel:

```sh
${GAMEDIR}/game -f${SCREEN_RES} -n
```

`diff run_game.sh_original run_game.sh` geeft **exact** `35c35,36` -- het verschil tussen de officiele
en de originele start is die ene regel, precies zoals het TeknoParrot-document
(`Teknoparrot/findings/16_halo_fireteam.md`, regels 11-25) al beschreef.

## 2. De argumenten vergeleken (GEMETEN)

| | officieel | bij ons |
|---|---|---|
| argumenten | `-f${SCREEN_RES} -n`, met `SCREEN_RES` uit `rtinit -ct`: code 9 -> `3880x1080`, code 4/3/8 -> **`1920x1080`** | `-f1920x1080 -n` (`packaging/native-games.json`, `"args": ["-f{res}", "-n"]`, res 1920x1080) |
| preload | `/pm/g7/halo/halo.so` | `/rg/lib/librg_rt_input.so:/pm/g7/halo/halo.so` (een **uitbreiding**: wij zetten de invoerhulp erbij) |
| binary | `${GAMEDIR}/game2` | `/pm/g7/halo/game` |
| werkmap | `cd /pm/g7/halo` | `--chdir /pm/g7/halo` (uit `"cwd"` in het recept) |
| HOME | niet gezet; `USERDIR="${GAMEDIR}/pmuser"` | `HOME=/pm/g7/halo` (uit `"home"`) |

**Conclusie: er ontbreekt GEEN argument.** De officiele start geeft precies dezelfde twee argumenten,
en de resolutie die eruit volgt is voor onze cabinetcodes (4, 3, 8) 1920x1080 -- dezelfde die wij
geven. De verklaring "het spel wacht op een argument dat de officiele start wel meegeeft en wij niet"
is voor deze game dus **weggenomen**, niet bevestigd.

Ook de werkmap is geen verschil: het recept zet `"cwd": "/pm/g7/halo"` en `play-native.sh` geeft dat
door als `--chdir` (regel 325); het origineel doet `cd /pm/g7/halo`. Dat was mijn eigen beste
kandidaat en hij valt af -- **GEMETEN**.

En `game2`: dat bestand **bestaat niet** in de map (alleen `game` van 33.554.432 B = 32 MiB en
`game_original` van 31.270.111 B). Het TeknoParrot-document zegt dat TeknoParrot `game2` bij het
starten aanmaakt. Wij starten `game` -- hetzelfde gepatchte beeld -- dus dat is in de praktijk
dezelfde binary.

## 3. Wat de officiele start WEL doet en wij niet (GEMETEN)

Uit `utils/run_game.sh` en `utils/go.sh`, en uit `play-native.sh`:

| stap | officieel | bij ons |
|---|---|---|
| **`utils/rtinit -ct`** (dongle/cabinettype-probe, bepaalt de resolutie) | **ja**, voor de game | **nee** |
| `export DISPLAY=:0` | ja | gamescope geeft zijn eigen `DISPLAY` |
| `utils/pulseaudio_set_default.sh` | ja | nee (geen geluid: FMOD 1.10 ontbreekt) |
| omgeving | de omgeving van de kast, inclusief wat `go.sh` zet: `overcommit_memory=1`, cpufreq, `wifi_setup.sh`, `connection_startup.sh &` | `--clearenv` met alleen `HOME`, `PATH`, `DISPLAY`, `XDG_RUNTIME_DIR`, `LANG`, `LD_LIBRARY_PATH`, `LD_PRELOAD` (regels 325-331) |
| uitkomst van de game | de exitcode is een **opdracht**: 222 herstart, 223 reboot, 224 patch, 225 factory reset, 226 power-cycle | de exitcode is de SIGTERM van onze timer (143) |

De twee echte verschillen zijn dus **`rtinit -ct` dat wij niet draaien** en **onze schone, minimale
omgeving** tegenover de volledige kastomgeving.

## 4. Heeft de START-route met de officiele start te maken? (GEMETEN: nee)

**Nergens in `run_game.sh` of `go.sh` staat een test-, service- of diagnosemodus, en er is geen
schakelaar in een ini die zoiets aanzet.** De kastschakelaars (START, TEST, munt, service) zijn
*spelinvoer* van het RIO2-bord, geen startmodus: `tools/rt-input-shim` zet ze in de schakelaartabel van
de game, en `packaging/native-games.json` noemt ze als invoer, niet als configuratie.

De START-route uit mijn draaiboek is dus **niet** de officiele manier en verbergt ook geen tweede
verklaring: het is onze eigen, gedocumenteerde omweg om de FileTest over te slaan
(`docs/decisions.md`: *"Gemeten: een START-bit in het gedeelte geheugen laat de game van het
FileTest-scherm naar 'Webcam Init' gaan"*).

## 5. Het manifest van de FileTest (GEMETEN, en de mechaniek blijft open)

`eFileMaps/` in de spellmap is geen bestand maar een **map met zeven kaarten**:
`fileMap000` (555 B) t/m `fileMap006` (137.610 B). De kaarten zijn **binair en ondoorzichtig**: geen
leesbare bestandsnaam, en `game` komt er niet als tekst in voor. Het is dus geen platte bestandslijst
die ik kan lezen; of het formaat namen, groottes of hashes bevat, is daarmee **niet vastgesteld** --
dat laat ik open in plaats van er iets van te maken.

## 6. AFGELEID, en een correctie op mijn eigen verslag van run 2

In run 2 schreef ik: de FileTest is een **voorwaarde**, geen zijspoor, omdat de FTDI-attribuutregels
ontbreken terwijl de FileTest faalt. Wat run 2 werkelijk heeft gemeten is: *de FTDI-opsomming is niet
gebeurd*. Dat de FileTest daar de **oorzaak** van is, is een afleiding -- en er is nu een sterke
aanwijzing dat die afleiding niet klopt:

* de officiele route start **dezelfde gepatchte binary** (`game`/`game2`, 32 MiB) tegen **hetzelfde
  ondoorzichtige manifest** (`eFileMaps/`);
* `Teknoparrot/findings/Halo.md` beschrijft die route als **volledig werkend** (kalibratie, attract,
  munt, start, moduskeuze, richten, trekker, herladen, granaat, trillen) en noemt **nergens** een
  FileTest-melding.

Als de FileTest daar hetzelfde oordeelt, dan is `FileTest Failure` een **gemelde toestand en geen
blokkade**, en dan is de regel in ons logboek het laatste dat de game schreef, niet de reden dat hij
stopte. **AFGELEID, niet gemeten** -- en het is **bench-vrij te controleren**: de werkende route schrijft
zijn eigen `pmuser/gamelog.txt`, alleen op een andere plek (in de Wine-prefix of de userdir van die
route). Wie dat bestand vindt, ziet of de FileTest daar ook faalt. Dat is de eerstvolgende gratis
controle, vóór er weer bench aan te pas komt.

Daarmee blijven er voor "de game komt niet aan zijn FTDI-pad" drie kandidaten over, in deze orde:
1. **de ontbrekende bibliotheken** -- onder TeknoParrot draait de game via ElfLdr2/BudgieLoader, dat
   zijn eigen bibliotheken meelevert (het TeknoParrot-document noemt `ElfLdr2/libs`: PhysX, FMOD 7,
   libav). Dat is precies waarom de native route een **FMOD-stub** nodig heeft om door "Webcam Init"
   te komen -- en dat is de best gedocumenteerde kandidaat;
2. **`rtinit -ct` dat wij niet draaien** (de dongle/cabinettype-probe, die op de kast vóór de game
   loopt);
3. **de FileTest**, die volgens §6 waarschijnlijk een gemelde toestand is en geen blokkade.

## 7. Halo heeft GEEN `game.bat` in de views (GEMETEN)

De coördinator vond dat zeven games een eigen `game.bat` in hun view hebben (HOTDSD, LGI, LGI3D, LGS,
LuigisMansion, MedaruNoGunman, OG) en vroeg of Halo er ook een heeft. **Nee.** In
`~/.local/share/ravengun/runtime/views/` staan **18 views**:

```
GhostBusters  HOTDSD  InfinityBladeFX  JurassicPark  LGI  LGI3D  LGS  LuigisMansion
MedaruNoGunman  OG  RabbidsHollywood  RaccoonRampage  RainbowBombergirl  Rampage
T4User  TC5  Terminator  TombRaider
```

Er is **geen view met de naam Halo** (ook niet onder `Halo Fireteam` of `HaloFireteam`), en dus ook
geen `game.bat`. De zeven `game.bat`-bestanden die er zijn staan allemaal bij **Windows**-games:
`views/{OG,LGS,MedaruNoGunman,LGI,LuigisMansion,LGI3D,HOTDSD}/game.bat`. Dat is geen toeval: de views
zijn de **Wine**-schakel, en Halo is een **Linux-ELF** -- het draait niet onder Wine en heeft dus geen
view en geen `.bat`.

**Halo's equivalent bestaat wel, maar het is geen batchscript in een view: het is
`Halo Fireteam/g7/halo/utils/run_game.sh`** (een `sh`-script in de NAS-spelmap), dat ik in §1 tot en
met §3 volledig heb gelezen en vergeleken. Daar staan alle variabelen, argumenten en hulpmiddelen in:
`DISPLAY`, `PATH`, `GAMEDIR`/`UTILSDIR`/`USERDIR`, `SCREEN_RES` (uit `rtinit -ct`), `cd`,
`LD_PRELOAD=halo.so`, `game2`, en de afhandeling van de exitcode (222-226). Aan hulpmiddelen staan
er twee in: `pulseaudio_set_default.sh` en **`rtinit -ct`**.

## 8. En dat bevestigt het patroon dat de coördinator bij de andere games vond (GEMETEN)

De zeven batchscripts laten een duidelijke vorm zien, en Halo's `sh`-script heeft **dezelfde vorm**:

| game | wat de officiele start doet voor de game begint |
|---|---|
| LGS | `mxGetHwInfo.exe -s mxGetHwInfo.ini`, en **alleen bij `%ERRORLEVEL% == 0`** wordt het spel gestart |
| OG | `mxGetHwInfo.exe -s mxRingwide.ini`, idem met de returncode als poort |
| LGI / LGI3D | `mxgethwinfo.exe -s app.ini`, idem |
| LuigisMansion | `set GAMEID=SDCF`, app-datagebied `Y:\SDCF` klaarzetten, **`sendFlash COM1 LMAIO.bin`** (I/O-board via de seriele poort), dan `start /wait x:\exe\x64\VACUUM.exe` |
| HOTDSD | `LAUNCH_ON_IMAGE=1`, `IMAGE_ROOT_DIR=X:\`, en `call AppStartup.bat` |
| MedaruNoGunman | `cd exe` en `start /wait game.exe` |
| **Halo** | **`utils/rtinit -ct`** -- dezelfde vorm als `mxGetHwInfo.exe`: **een hardwareprobe voor de game**, met de returncode als sleutel |

Het verschil is wat de probe *doet* met zijn uitkomst: bij LGS/OG/LGI is de returncode een **poort**
(alleen 0 start het spel), bij Halo kiest hij de **resolutie** (9 -> 3880x1080; 4, 3 of 8 ->
1920x1080). En dat maakt meteen een subtiel faalpad zichtbaar: geeft `rtinit -ct` een code die niet in
die vier voorkomt, dan blijft `SCREEN_RES` **leeg** en wordt de startregel letterlijk
`game -f -n` -- een leeg argument. (GEMETEN uit `utils/run_game.sh`: `SCREEN_RES` wordt alleen in die
vier takken gezet.)

**Wat wij dus overslaan is precies dezelfde soort stap die de andere games wel nemen:** de
hardwareprobe voor de game. Dat staat in §6 al als kandidaat 2, en deze vondst maakt die kandidaat
sterker -- niet omdat de resolutie ons iets kan schelen (wij geven hem expliciet), maar omdat de probe
op de kast **voor** de game loopt en wij hem nooit draaien. `rtinit.so` is met 880.520 B even groot als
`halo.so`, wat de gedachte ondersteunt dat de dongle-/cabinetkant van dezelfde bibliotheek gebruik
maakt (AFGELEID uit de bestandsgroottes; de werking zelf is niet gemeten).

**Een gratis controle die hieruit volgt:** `rtinit -ct` los draaien met `halo.so` voorgeladen en kijken
welke code eruit komt (3, 4, 8 of 9, of iets anders). Dat is dezelfde hardwareprobe die de officiele
start als eerste doet, en het antwoord zegt of onze native route dezelfde tak zou hebben gekozen. Daar
is geen spelstart voor nodig -- wel een bench-afspraak, want het is een binary uit de spelmap.

## 9. Bronnen

* `/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/utils/run_game.sh` (regels 1-36)
  en `run_game.sh_original` (regel 35); `diff` = `35c35,36`.
* `/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/utils/go.sh` (de kaststart).
* `/home/deck/mnt/spidernas_emulation/Teknoparrot/findings/16_halo_fireteam.md` (regels 11-25: de ene
  gewijzigde regel; de herkomst is het TeknoParrot-team) en `findings/Halo.md` (de werkende route).
* `patches/Halo/game.ini` (`Executable = game`, `Path = game`).
* `packaging/native-games.json`, blok `.games.Halo`: `exe`, `cwd`, `home`, `args`, `preload`, `libs`,
  `extra_ro`, `route`.
* `scripts/play-native.sh`, regels 95 en 325-331 (`--chdir`, `--clearenv`, `HOME`, `LD_PRELOAD`).
* `docs/decisions.md` (de START-bit naar "Webcam Init") en `docs/procedures.md` (de bitindeling van het
  schakelaarbeeld).
* `eFileMaps/fileMap000..006` in de spellmap (binair; geen leesbare namen).
