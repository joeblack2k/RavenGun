# Per-game instellingen en patches (`patches/<Game>/`)

Ontwerp 2026-10-11, uitgewerkt dezelfde dag. Model: Dolphin. Dolphin bewaart alles wat per game anders is in één tekstbestand per
game-ID (`Sys/GameSettings/<ID>.ini`, meegeleverd), met een gebruikerskopie die per sleutel wint (`User/GameSettings/<ID>.ini`).
Patches zijn benoemde blokken (`$Naam`) die de gebruiker in Properties > Patches aan- en uitzet en zelf kan toevoegen.
De emulator zelf bevat geen kennis van één bepaalde game. RavenGun doet hetzelfde.

## Waarom

Voor deze overgang stond de kennis per game verspreid: `packaging/io-routes.json` (route, indeling, startwijze),
`packaging/games-notes.json` (status, notities), `UserProfiles/<P>.env` in de TeknoParrot-runtime (lader, Wine,
omgeving), `packaging/jvs-boards.json` (borden), en code in `scripts/launch-direct.py` en `scripts/tp-add-game.py`
(de lader-patch voor Type X2, de vaste knoppen van Let's Go Safari, uitzonderingen op profielnaam). Een game toevoegen of een
oplossing aanpassen betekende code wijzigen. Gebruikers konden niets toevoegen zonder in de repo te werken.

## Indeling

```
<datadir>/patches/                 meegeleverd (repo: patches/; geïnstalleerd: share/ravengun/patches/), alleen-lezen
  _defaults/<Type>.ini             standaard per soort (TeknoParrot, OpenParrot, ElfLdr2, Unity, Native)
  _defaults/<Type>.patches.ini     gedeelde patchdefinities voor dat soort (zie "patches.ini")
  <Game>/
    game.ini                       instellingen (Dolphin: GameSettings)
    patches.ini                    patches van deze game, of "Enabled = true" bij een gedeelde patch; optioneel
    native.json                    het native profiel (sandbox, exe, bibliotheken; docs/native-profiel.md); optioneel
    options.json                   de opties en memory card die de app toont (docs/game-opties.md); optioneel
    unity.json                     het profiel van een Unity-game (Tomb Raider, Rabbids Hollywood; zie "Unity-games" hieronder); optioneel
    README.md                      korte uitleg en verwijzing naar findings/<Game>.md; alleen als er zo'n bestand is
    seed/                          zaaigoed voor de schrijflaag van de Wine-routes (zie "Zaaigoed" hieronder); optioneel
    <extra bestanden>              bv. een .reg voor de eigen prefix; optioneel

~/.local/share/ravengun/patches/   gebruiker ($XDG_DATA_HOME/ravengun/patches), zelfde indeling; wint per sleutel
  <Game>/game.ini
  <Game>/patches.ini
  <Game>/native.json               vervangt het meegeleverde bestand als geheel (geen samenvoeging); kapot = geen profiel
  <Game>/options.json              idem: vervangt het meegeleverde bestand als geheel; kapot = geen opties
  <Game>/unity.json                idem: vervangt het meegeleverde bestand als geheel; kapot = een melding bij de start, geen terugval
  <Game>/seed/                     wint per bestand van het meegeleverde zaaigoed
```

`<Game>` is de TeknoParrot-profielnaam (`LGI`, `NERF`, `LuigisMansion`), voor eigen routes de naam van de map met
`unity.json`/`native.json`. Een eigen game maak je door in de gebruikersmap een map met een `game.ini` te zetten.

### Zoekvolgorde

Er is **een** plek waar de volgorde staat: `patch_layers()` in `scripts/gamesettings.py` (`patches_roots()` geeft er de paden van).
Alles wat `patches/<Game>/` zoekt (gamesettings, camera, launch-direct, amdaemon, region, matrix, hasp) gaat daarlangs; geen script
leest `RAVENGUN_PATCHES_DIR` of `RAVENGUN_USER_PATCHES_DIR` zelf (`tests/test_patches_roots.py` controleert dat).

| laag | map | rol |
|---|---|---|
| 1, onderaan | de eigen `patches/` van de loader: `<datamap>/patches` (repo bij ontwikkeling, `<prefix>/share/ravengun/patches` bij een installatie) | de basis |
| 2 | de gebruikerslaag: `$RAVENGUN_USER_PATCHES_DIR`, anders `$XDG_DATA_HOME/ravengun/patches`, anders `~/.local/share/ravengun/patches` | afwijkingen van de gebruiker |
| 3, bovenaan | `$RAVENGUN_PATCHES_DIR` (alleen als gezet): een ontwikkel-checkout van de patches | wint van alles |

- **Per sleutel wint de latere laag** (3 boven 2 boven 1); een lege waarde haalt de sleutel weg, in welke laag dan ook. `patches.ini`
  volgt dezelfde lagen.
- **`$RAVENGUN_PATCHES_DIR` komt erbovenop, het vervangt de loader-map niet.** Een game die alleen in `patches/` van de loader staat,
  blijft vindbaar als er een ontwikkel-checkout is. Daardoor kunnen gamemappen van de patches-repo naar de loader verhuizen (of
  terug) zonder dat er iets verandert.
- Een laag die niet bestaat valt weg; de volgorde van de rest blijft. Is dezelfde map (na `realpath`) tweemaal een laag, dan telt hij
  een keer, op de hoogste plek. Op een ontwikkelmachine is `patches/` van de loader een koppeling naar de patches-repo; dat is
  daarmee onschadelijk.
- Een zoektocht die de **eerste treffer** neemt (een bestand, een drop-in) loopt omgekeerd: 3, dan 2, dan 1.
- `_defaults/<Type>.ini` en `.patches.ini` komen uit de meegeleverde lagen (1 en 3), in die volgorde; de gebruikerslaag levert er geen.
- Gevolg om te weten: staat `RAVENGUN_PATCHES_DIR` in de omgeving van de app of de service, dan overrulen de waarden daar de
  gebruikerslaag (laag 2) per sleutel. Zet hem alleen in een ontwikkelshell.
- Tests: `RAVENGUN_LOADER_PATCHES_DIR` verplaatst laag 1 (ook voor een pakket dat de patches elders neerzet). Een test die de echte
  `patches/` buiten de deur wil houden zet hem op een tijdelijke map en laat `RAVENGUN_PATCHES_DIR` weg.
- `--patches DIR` / `--user DIR` van `gamesettings.py` en de parameters `shipped=`/`user=` van de functies vervangen de meegeleverde
  lagen (1 en 3) resp. laag 2; de volgorde blijft meegeleverd, dan gebruiker.
- `gamesettings.py dirs` toont de lagen in deze volgorde. `shipped_dir()` (de bovenste meegeleverde laag: schrijfdoel van de importer)
  en `user_dir()` bestaan nog, afgeleid van dezelfde lijst.
- De C++-lezer (`src/ravengun/gamesettings/`) kent nog het oude of/of (`RAVENGUN_PATCHES_DIR` *of* `<datamap>/patches`) en is bij
  deze wijziging **niet** aangepast; zie "Nog open" onderaan.

### Identiteit: de game vinden op `[Game] Profile`

De map van een game wordt niet alleen op haar naam gezocht (`find_game_dir()` in `gamesettings.py`; hetzelfde in amdaemon, region,
matrix en hasp):

1. Snelle weg: `<wortel>/<profiel>/game.ini`. Staat daar een `[Game] Profile` dat niet `<profiel>` is, dan klopt de naam van de map
   niet met wat erin staat: een luide waarschuwing (in `Game.warnings` en op stderr, "WAARSCHUWING: ... de map heet ...").
2. Scan: een andere map in dezelfde wortel waarvan `[Game] Profile` het gevraagde profiel is. Een hernoemde map werkt dus, met
   dezelfde waarschuwing ("hernoem de map"). Meerdere treffers: de eerste op naam, met een waarschuwing.
3. Niets gevonden maar er is wel een map met de naam van het profiel (een `game.ini` zonder `Profile`, of een map met alleen
   `patches.ini`): die wordt gebruikt. Is er niets, dan is de game "niet gevonden" (`Game.exists = False`, `locate_game_dirs() == []`);
   dat is geen fout en geeft geen waarschuwing.

`list_games()` noemt de games met hun `Profile` (zo zijn ze te laden). Het schrijven naar de gebruikerslaag
(`write_user_settings`) blijft in een bestaande, hernoemde map met dat profiel.

### Indeling van een gamemap: levering en onderzoek

Het schema staat in code (`tools/dev/patchlayout.py`) en wordt getest (`tests/test_patches_layout.py`). Eén regel beslist of een
gamemap van de patches-repo naar de `patches/` van de loader mag verhuizen: **verhuizen neemt alleen de levering mee, nooit het onderzoek.**

| soort | wat | verplicht |
|---|---|---|
| levering | `game.ini` | ja |
| levering | `patches.ini` | nee |
| levering | `native.json` (het native profiel, docs/native-profiel.md; de gebruikerslaag vervangt het als geheel) | nee |
| levering | `options.json` (opties en memory card voor de app, docs/game-opties.md; de gebruikerslaag vervangt het als geheel) | nee |
| levering | `unity.json` (het profiel van een Unity-game, zie "Unity-games"; de gebruikerslaag vervangt het als geheel) | nee |
| levering | `dropin/**` (met `dropin/dropin.ini`) | nee; heeft een game `dropin/`, dan moet `dropin.ini` er zijn en elk bestand dat het noemt |
| levering | `data/**` (runtimegegevens) | nee |
| onderzoek | `*.md` (ook `README.md`, `*.md.voor-correctie-*`), `logs-*/`, `bewijs-*/`, `probe/`, `probes/`, `measurements/`, `*.voor-correctie*`, schermafbeeldingen (`*.png`, `*.jpg`, ...), `*.log`, `gamelog-*`, `usb-tap*` | nee, en nooit in de levering |
| onderzoek, alleen bovenin de map | `*.txt`, `*.csv`, `*.sh`, `*.py`, `*.c`, `*.h`, `*-logs/`, `*-harness/`, `*.draft.*`, `*.kandidaat.*`, `*.schema-fragment.*`, `run*` | nee |
| onbekend | al het andere | wordt gemeld door de test: het hoort of in de levering (en dan in de lijst hierboven), of bij het onderzoek |

Binnen `dropin/` en `data/` zijn `.py`, `.sh`, `.txt` en `.json` gewone leveringsbestanden; daar telt alleen het onderzoek uit de
eerste groep als besmetting.

`ship_set(map)` geeft de levering (relatieve paden), `research_set(map)` het onderzoek op het hoogste niveau,
`unknown_entries(map)` wat er in geen van beide valt. De tests:

- **verplicht**: elke gamemap heeft `game.ini` (`missing-required`);
- **levering compleet**: noemt `dropin/dropin.ini` een bestand dat er niet staat, of staat er `dropin/` zonder `dropin.ini`, dan faalt het
  (`dropin-missing-file`, `dropin-no-ini`, `dropin-bad-ini`);
- **levering zonder onderzoek**: geen enkel pad van `ship_set` is onderzoeksmateriaal (`ship-contaminated`), zodat verhuizen geen dossiers
  of logs meesleept;
- **geen onbekende namen** (`unknown-entry`) en **mapnaam = `[Game] Profile`** (`profile-mismatch`).

`python3 tools/dev/patchlayout.py [wortel ...]` toont dezelfde afwijkingen.

**Een map of twee?** Op een ontwikkelmachine is `patches/` van de loader een koppeling naar `../ravengun-patches/patches`: dezelfde map. De
test stelt dat vast met `st_dev`/`st_ino` (niet met de tekst van het pad), legt die ene map tegen het schema en meldt dat hij geen
vergelijking tussen twee repo's heeft gedaan; hij beweert dus nooit dat "de twee indelingen overeenkomen". Zijn het twee verschillende
mappen (andere machine, of gamemappen zijn naar de loader verhuisd), dan worden beide tegen het schema gelegd en wordt elke game die op
beide plaatsen staat op haar levering (paden en sha256) vergeleken.

### Samenvoegen

Per sleutel, in deze volgorde (een latere laag wint):

1. `_defaults/<Type>.ini`. Het soort staat in `[Game] Type` (de gebruikerslaag gaat voor); zonder `Type` geldt de hint van de
   aanroeper (de launcher geeft de `EmulatorType` van het TeknoParrot-profiel mee).
2. `patches/<Game>/game.ini` (meegeleverd: laag 1 en, als `RAVENGUN_PATCHES_DIR` gezet is, 3).
3. `<gebruiker>/<Game>/game.ini`. (Volgorde van de lagen: zie "Zoekvolgorde"; een ontwikkel-checkout komt daar nog bovenop.)

Een **lege waarde** (`Sleutel =`) haalt de sleutel weg, in welke laag dan ook: zo zet een game of de gebruiker een standaard van het
soort terug op "niet gezet". Een sectie zonder sleutels bestaat niet. Wat de migratie niet kent, blijft staan.

Formaat: INI. Reden: Dolphin gebruikt het, mensen kennen het, Python leest het met `configparser` en de engine en de app met één
kleine eigen lezer (alleen de standaardbibliotheek; de engine heeft geen Qt). Sectienamen en sleutels zijn hoofdlettergevoelig en
blijven zoals ze staan. Commentaar: een regel die met `#` of `;` begint, of een `;` na witruimte achter een waarde (`Sleutel = waarde ;
uitleg`; een `;` zonder witruimte ervoor hoort bij de waarde, zoals in `WINEDLLOVERRIDES`). Geen waarden over meerdere regels,
voorloop-witruimte telt niet. Beide lezers worden getest tegen `tests/data/gamesettings-cases.ini`.

## `game.ini`

```ini
[Game]
Name = Let's Go Island
Profile = LGI                 ; TeknoParrot-profiel
Type = TeknoParrot            ; kiest _defaults/<Type>.ini
EmulationProfile = LGS        ; het TeknoParrot-EmulationProfile (uit GameProfiles/<P>.xml; de migratie vult het); informatie
DongleRegion = JAPAN          ; regiokeuze van het profiel (zie hieronder); leeg = de waarde van het profiel zelf
PcbRegion = JAPAN             ; idem
ExeSha256 =                   ; optioneel: patches gelden alleen voor deze exe-versie (anders weigert de launcher ze)

[Launch]
Mode = direct                 ; direct | tpui (leeg = tpui)
WorkingDirectory = .          ; relatief aan de map van de game-exe, of een Windows-pad (Z:\...)
View =                        ; auto (leeg) | off | N: de lokale view van een game op de NAS (zie "De view"); N = --view-up N
ViewEmptyDirs =               ; mappen die in de view leeg en lokaal zijn (--empty-dir), bv. RSGame/Content/Movies
ViewLinks =                   ; A -> B[, ...]: in de view wordt A een symlink naar het bestand van B (zie "ViewLinks" hieronder)
ViewCopies =                  ; A[, ...]: in de view wordt A een lokale kopie, zodat de game nooit naar de NAS schrijft (zie hieronder)
ViewTtcAsTtf =                ; A.ttc[, ...]: in de view wordt de TrueType-collectie A face 0 als losse font (Wine's GDI+ neemt geen .ttc aan; zie hieronder)
StackReserve =                ; bv. 16M: de exe in de view wordt een lokale kopie met een grotere stapel voor de hoofdthread
PeFillPages =                 ; true: in dezelfde lokale kopie lopen de secties tot hun bladzijde, zoals Windows ze mapt (zie hieronder)
BepInEx =                     ; versie (bv. 5.4.21.0): BepInEx naast de exe in de view, voor een eigen plugin als drop-in (zie hieronder)

[Display]
AimAspect =                   ; bv. 4:3 of 1024x768: het beeld van het spel op de tv; de daemon legt de aim-ruimte van de lightgun
                              ; voor deze game op dat deel van het scherm (pillarbox/letterbox) en zet hem bij stoppen terug

[Loader]
RemoteThread = off            ; off = standaardroute van de lader; 1 = remote thread; leeg = niet gezet (de launcher kiest 1)
PostStartMs =                 ; TP_POSTSTART_REMOTETHREAD_MS
EntryPointMs =                ; TP_ENTRYPOINT_REMOTETHREAD_MS
Build =                       ; leeg/teknoparrot = de lader uit de TeknoParrot-runtime; ravengun = de eigen OpenParrot-lader en (bij Type = OpenParrot) -kern uit scripts/build-openparrot.sh
Core =                        ; alleen de kern apart (teknoparrot | ravengun | none); leeg volgt Build (bij Type = OpenParrot), een eigen kern bestaat alleen voor Type = OpenParrot; none = geen lader en geen kern: Wine start de exe zelf (games zonder TeknoParrot-profiel, zoals Mighty Moto)
Path =                        ; map met OpenParrotWin32/ en OpenParrotx64/ (standaard ~/.local/share/ravengun/openparrot/current)

[Wine]
Prefix = ravengun-test-lgi    ; shared | <naam onder ~/.local/share/TeknoParrotUI/prefixes/> | pad (~ mag); de wrapper wine-prefix
PrefixDirect =                ; idem, voor een gewone wine-runner: de launcher zet zelf WINEPREFIX (TP_WINEPREFIX)
Binary =                      ; eigen wine (TP_WINE)
DllOverrides = d3d9=b         ; WINEDLLOVERRIDES
Debug =                       ; WINEDEBUG
GStreamerPlugins =            ; GST_PLUGIN_PATH, bv. ~/.local/share/ravengun/tools/gst-extra/plugins (asf, libav)
VirtualDesktop = 1920x1080    ; vastgelegd door tp-add-game --vdesktop (het zit in de prefix); de launcher leest dit niet
NativeDlls =                  ; idem: bv. d3dx9_40, bron: de Microsoft-herdistribueerbare van de gebruiker
PhysX =                       ; true: de PhysX-systeemsoftware van NVIDIA moet in de prefix staan (scripts/setup-physx.sh; docs/redist.md)

[Display]
Gamescope = true              ; TP_GAMESCOPE=1: de game draait schermvullend in gamescope
GamescopePath =               ; TP_GAMESCOPE_PATH
GamescopeBackend =            ; TP_GAMESCOPE_BACKEND
GamescopeResolution =         ; RG_GAMESCOPE_RES
GamescopeScale =              ; RG_GAMESCOPE_SCALE: fit (standaard) | stretch | fill | integer | auto (gamescope -S)
DisplayMode =                 ; informatie: de optie DisplayMode van het profiel (bv. Fullscreen Windowed); de launcher leest dit niet
CustomResolution =            ; informatie: 1920x1080 als het profiel een eigen resolutie aan heeft; de launcher leest dit niet

[Input]
Route = jvs                   ; evdev | tp-shm | unity-tcp | hid-board | jvs
Layout =                      ; tp-shm: indeling uit tp-shm-profiles.json
ControlPipe =                 ; tp-shm: ook het rapport voor \\.\pipe\TeknoParrotPipe (indeling uit control-pipe-profiles.json, bv. AllsUsbIo)
Board = LGI                   ; hid-board: bord uit hid-boards.json; jvs: bord uit jvs-boards.json
Boards =                      ; jvs: de keten, gescheiden door komma's
ComPort = COM4                ; jvs: de COM-poort die de game opent
CoinKeys = lshift,t           ; toetsen die de engine ook indrukt bij een munt (games die hun munt uit het toetsenbord lezen)
JvsTransport = pipe           ; RG_JVS_TRANSPORT: pipe = JVS over de pipe TeknoParrot_JVS (Let's Go Island)
JvsPipeBoard =                ; RG_JVS_BOARD: standaard JVS-bord achter de pipe (Type X2)
JvsF1Once =                   ; RG_JVS_F1_ONCE
JvsDebug =                    ; RG_JVS_DEBUG
JvsIdent =                    ; RG_JVS_IDENT (proef)
JvsFeatures =                 ; RG_JVS_FEATURES (proef)
Note =                        ; toelichting bij de route

[Audio]
Channels = stereo             ; stereo (standaard) | 5.1 | native: het geluidsapparaat dat Wine ziet (PULSE_PROP=pulse.fix.position=...).
                              ; Wine's DirectSound mengt niet goed naar 7.1 (geen volume/pan boven 6 kanalen, zoem bij mono-effecten).

[Gamepad]
; optioneel: gamepad naast de lightgun (docs/gamepad.md); per sleutel over de standaardindeling heen, none haalt weg
LeftX = wheel                 ; stuur (zonder gamepad stuurt de aim-x)
RT = gas
LT = brake
LB = btn1                     ; extra kastknop (pN.btn1 in de IO-brug)

[Dongle]
Enabled = false               ; true = de echte, fysieke dongle van de eigenaar gebruiken (USB pass-through); standaard false (zie "Dongle per game")
Source = usb-passthrough      ; enige waarde nu; de lijst is uitbreidbaar. Leeg = usb-passthrough

[Env]
; vrije omgevingsvariabelen, als laatste toegepast (ontsnappingsroute; liever een echte sleutel hierboven)
TP_NUSOUND = 1

[Bindings]
; knoppen van deze game voor tp-add-game: "Knopnaam = kbd:TOETS" of "gun:LeftButton" ("gun:" = het vizier)
Button 1 (red) = kbd:A

[Status]
Status = playable             ; playable | parked | untested
Note = ...                    ; Nederlands (webpagina en engine, hoogstens 300 tekens; langer wordt afgekapt met "…")
NoteEN = ...                  ; Engels (de app)
Findings = findings/LetsGoIsland.md
```

`[Game] DongleRegion` en `PcbRegion` zijn gewone instellingen: de keuze uit de keuzelijst die het TeknoParrot-profiel zelf al biedt
(`ConfigValues` in `GameProfiles/<P>.xml`, bv. Let's Go Island en Let's Go Safari). De migratie neemt de waarde van het gebruikersprofiel over (leeg als
het profiel de optie niet heeft). De directe start zet de waarde in die profieloptie, op dezelfde plek waar de rest van het profiel in `teknoparrot.ini`
terechtkomt (het profielbestand zelf blijft ongemoeid). Alleen waarden uit de keuzelijst van de optie worden aanvaard; een andere waarde, of een profiel zonder die
optie, weigert de start met een melding (exitcode 5, geen terugval op TeknoParrotUi). Dit is het kiezen van een bestaande profieloptie, geen patch: de
regiokeuze hoort bij de regiodongle van de game (`patches/<Game>/dongle.json`, soort `region`; `docs/dropins.md`).

De sleutels van `[Loader]`, `[Wine]`, `[Display]`, `[Launch] WorkingDirectory` en `[Input] Jvs*` worden door
`scripts/gamesettings.py` (`to_env`) omgezet naar de omgevingsvariabelen die TeknoParrot, de lader en `launch-direct.py`
lezen; de tabel `ENV_MAP` daar is de enige plek waar een sleutelnaam aan een variabele hangt. Waarden worden letterlijk
doorgegeven, behalve de booleans (`Gamescope`, `JvsPipeBoard`, ...: `true/yes/on/1` wordt de waarde die de lezer verwacht), `~/` (thuismap)
en de padvormen van `Prefix` en `WorkingDirectory`. `[Env]` wint van de bekende sleutels.

`[Loader] Build`, `Core` en `Path` zijn geen omgevingsvariabelen maar kiezen welke bestanden `scripts/launch-direct.py` start (`own_build()`): standaard
de lader (`OpenParrotLoader(64).exe`) en kern uit de TeknoParrot-runtime; met `Build = ravengun` de eigen build uit de openbare OpenParrot-bron
(`scripts/build-openparrot.sh`, `third_party/openparrot/README.md`). De Wine-fix zit dan in de lader zelf, dus de bytepatch `OpenParrotLoader zonder CONTEXT_FLOATING_POINT`
wordt overgeslagen (`overgeslagen ... [uit]` in het log). Een onbekende waarde, een ontbrekende build (met de hint `scripts/build-openparrot.sh`) of `Build = ravengun` bij een
andere lader dan OpenParrotLoader (BudgieLoader, Konami, Macaw) weigert de start (exitcode 5, geen terugval op TeknoParrotUi).

`[Gamepad]` (optioneel) kiest wat een gamepad naast de lightgun doet: sleutels zijn bedieningen (`A`, `B`, `X`, `Y`, `LB`, `RB`, `LT`, `RT`,
`Start`, `Back`, `Guide`, `LS`, `RS`, `DPadUp/Down/Left/Right`, `LeftX`, `LeftY`, `RightX`, `RightY`), waarden zijn signalen (`wheel`, `gas`, `brake`,
`trigger`, `reload`, `grenade`, `aux1`, `aux2`, `start`, `coin`, `btn1`..`btn8`, `key:<naam>`, `none`). Zonder sectie geldt de standaardindeling
(linkerstick = stuur, RT/LT = gas/rem, A/B/X/Y = trekker/herladen/granaat/aux1, LB/RB = btn1/btn2, Start/Back = start/munt); een sectie gaat per sleutel
over die standaard heen, en de gebruikerslaag wint zoals altijd. De daemon past de indeling toe van de game die de IO-brug start; de app toont hem in
Properties > General > Gamepad, met de herkomst per regel. Volledige beschrijving: `docs/gamepad.md`.

`[Input] Layout` bij de route jvs: de daemon schrijft dan naast het JVS-bord ook die tp-shm-indeling (Let's Go Safari leest het stuur alleen uit het
gedeelde geheugen), rechtstreeks in het bestand dat rg-direct naar de game spiegelt en alleen de eigen bytes (zie `docs/gamepad.md`).

`[Input] ControlPipe` bij de route tp-shm (of evdev, dat de directe start tot tp-shm maakt; bij andere routes is `[Input]` ongeldig): sommige games
lezen een deel van hun invoer niet uit het gedeelde geheugen maar uit de besturingspipe `\\.\pipe\TeknoParrotPipe`, waarin TeknoParrotUi elke
~15 ms een rapport schrijft. De daemon schrijft dat rapport dan zelf (indeling uit `packaging/control-pipe-profiles.json`, bestand
`/dev/shm/ravengun_ControlPipe`) en de directe start stroomt het naar de pipe in de prefix (een tweede `rg-jvsbridge`). Een naam die niet in dat
bestand staat, weigert de directe start ("Instelling geweigerd"). House of the Dead: Scarlet Dawn: `Route = tp-shm`, `Layout = HOTDSDPipe`,
`ControlPipe = AllsUsbIo` (start, service, test, subtrekker, controllerkeuze en munt gaan over de pipe; de trekkers en de aim over het geheugen).

`_defaults/<Type>.ini` heeft dezelfde vorm, plus een sectie `[NewGame]` die alleen `tp-add-game.py` leest:
`EnablePatches = <naam>, <naam>` zet die gedeelde patches aan in de gebruikerslaag van een nieuwe game van dat soort.

## Het profiel van een game

Tot nu toe stond het profiel van een game (de velden die TeknoParrotUi in `UserProfiles/<P>.xml` bewaart: lader, 64-bit, de
`ConfigValues` die in `teknoparrot.ini` komen, het pad naar de game) alleen in dat XML-bestand. RavenGun heeft nu een eigen profiel per game,
als onderdeel van `game.ini`: de meegeleverde laag bevat wat bij de game hoort (uit het sjabloon `GameProfiles/<P>.xml` van de runtime),
de gebruikerslaag alleen wat op deze machine of door deze gebruiker afwijkt. `UserProfiles/<P>.xml` is niet meer nodig om te starten.
De runtime van TeknoParrot (kernen, laders, `pipehelper.exe`, `ParrotData.xml`, de sjablonen in `GameProfiles/`) blijft nodig en wordt
gelezen zoals voorheen.

```ini
[Game]
Name = Let's Go Island: Lost on the Island of Tropics
Type = TeknoParrot                  ; EmulatorType van het profiel; kiest _defaults/<Type>.ini
EmulationProfile = SegaJvsLetsGoIsland
Executable = LGI_RingW_F_safe.exe   ; ExecutableName (en Executable2: ExecutableName2); bepaalt ook de procesnamen die play-halo.sh herkent
ProfileRevision = 21                ; GameProfileRevision van het sjabloon waaruit geïmporteerd is
Is64Bit = true                      ; alleen gezet als waar; zo ook Patreon, HasTwoExecutables, LaunchSecondExecutableFirst, UseRemoteThread

[Wine]
Runner = wrapper                    ; WineRunnerPath: wrapper = ~/.local/share/ravengun/wrappers/wine-prefix, of een pad met ~; leeg = standaard-wine

; Profiel uit TeknoParrot-sjabloon GameProfiles/LGI.xml, revisie 21 (scripts/import-tp-profiles.py; opnieuw importeren in plaats van met de hand wijzigen)
[Identify]
Path = LGI_RingW_F_safe.exe         ; hoofdbestand, relatief aan de map van de game (de submap van een gamemap)
Size = 6481920                      ; grootte in bytes (stat; goedkoop, ook op de NAS)

[Config]                            ; de ConfigValues: Categorie/Veld = waarde
General/Input API = RawInput
General/Windowed = 1
FFB Blaster/Enable = 0

[ConfigType]                        ; type per veld, in de volgorde van teknoparrot.ini
General/Input API = Dropdown
General/Windowed = Bool
FFB Blaster/Enable = Bool

[ConfigOptions]                     ; keuzelijst (alleen bij Dropdown e.d.), gescheiden door |
General/Input API = DirectInput|XInput|RawInput
```

De gebruikerslaag (`~/.local/share/ravengun/patches/LGI/game.ini`) bevat alleen verschillen:

```ini
[Config]
General/Windowed = 0                ; afwijkend van het sjabloon
```

### Sleutels

- `[Game]`: `Executable`, `Executable2` (alleen vastgelegd; de start met twee exe's komt later), `Type` (de bestaande sleutel; de
  `EmulatorType`), `EmulationProfile` (bestond al), `Is64Bit`, `ProfileRevision`, `Patreon` (informatief, overgenomen uit het sjabloon;
  zie "Abonnement"), `MsysType`, `GameVersion`, `HasTwoExecutables`, `LaunchSecondExecutableFirst`.
- `[Launch]`: `UseRemoteThread` (profielveld; iets anders dan `[Loader] RemoteThread`, de omgevingsvariabele), `ExtraParameters`,
  `CustomArguments`. `[Display] GamescopeCompatibility` (`GamescopeGameWindowCompatibility`), `[Wine] PrefixMode` (`WinePrefixMode`),
  `[Wine] Runner` (`WineRunnerPath`).
- De vertaling tussen deze namen en de veldnamen van het TeknoParrot-profiel staat op één plek: `PROFILE_FIELDS` in
  `scripts/ownprofile.py`, met een tabel `kProfileFields` in `src/ravengun/gamesettings/gamesettings.cpp` die `tests/test_ownprofile.py` ertegen
  controleert. Een veld dat het profiel niet of als `false`, `0` of `Default` heeft, staat er niet.
- `[Identify]` (zie "De game vinden"), `[Config]`, `[ConfigType]` en `[ConfigOptions]` (de ConfigValues).

**Argumenten voor het spel (`[Launch] Arguments`).** Standaard geeft de launcher het spel niets mee behalve wat het profiel
zegt (`ExtraParameters`, `CustomArguments`; meestal leeg). Geeft de eigen start van het spel (het `.bat`- of `.sh`-bestand
dat de kast gebruikt) wel iets mee, zet dat dan per game in `game.ini`:

```ini
[Launch]
Arguments = -launch="cabinet" -UserDir="C:\log map\nu" -NotInstalled -UNATTENDED
```

- `Arguments` schrijf je zoals op een opdrachtregel, met aanhalingstekens om een argument met spaties. De standaard is leeg.
- `--plan` laat zien welke argumenten er meegaan (`argumenten voor <exe> (n): [..]  [..]`), uit welke sleutel, en hoe.
- De OpenParrot-lader neemt precies **een** parameter voor het spel (`<DLL> <EXE> <param>`) en plakt die letterlijk achter het
  pad van de exe. Voor die lader gaat daarom alles als een parameter mee, de tekst uit `Arguments` letterlijk; het spel deelt
  hem zelf weer op, en een argument met spaties of aanhalingstekens komt heel aan. Geeft een profiel twee losse argumenten
  (`-a -b` zonder aanhalingstekens eromheen), dan worden die ook een parameter: voorheen kreeg de lader er twee, toonde hij zijn
  gebruiksregel en startte het spel niet. Andere laders (ElfLdr2, TeknoMacaw) krijgen de argumenten los mee.
- Voor de tweede exe geldt hetzelfde met `[Launch] SecondArguments`.

**Omgevingsvariabelen voor het spel (`[Env]`).** Elke regel `NAAM = waarde` onder `[Env]` komt in de omgeving van het
spelproces (en van de tweede exe), met de naam zoals hij geschreven is en de waarde ongewijzigd (`IMAGE_ROOT_DIR = X:\`). De
standaard is geen. `--plan` laat zien welke er meegaan: `omgevingsvariabelen voor het spel ([Env] in game.ini): GAMEID=SDCF`.

**Het eigen startscript van het spel (`[Launch] StartScript`).** Veel spellen hebben een eigen start: `game.bat`, of bij een
Linux-spel `utils/run_game.sh`. Dat script zet omgevingsvariabelen, kiest een werkmap, start soms eerst iets anders (een
hardwarecontrole, een daemon) en geeft het spel argumenten mee. De launcher **leest** dat script en legt het naast zijn eigen
plan; hij voert het **nooit uit**. `--plan` zegt dan wat de kast doet en wij niet:

```
eigen startscript van het spel: game.bat -- gelezen, NIET uitgevoerd
  zet GAMEID=SDCF -- WIJ GEVEN DIE NIET MEE (het script gebruikt hem ook zelf)
  start eerst  sendFlash COM1 LMAIO.bin -- WIJ NIET
  start het spel: VACUUM.exe (zonder argumenten)
    de argumenten zijn gelijk aan de onze
  gebruikt de schijfletters X:, Y:
  kastding (daarom niet uitvoeren): rmdir /S /Q Y:\ (voorwaardelijk)
```

- Zonder sleutel zoekt de launcher `game.bat`, `Start.bat`, `utils/run_game.sh` of `pm/utils/run_game.sh` in de spelmap.
  `StartScript = <pad>` wijst een ander script aan; `StartScript = none` zet het lezen uit.
- Wat het script uit de bestanden beslist (`if exist ...`, `if "%X%"=="y"`), beslist de lezer ook: hij volgt de tak die op deze
  kopie van het spel genomen wordt, ook door aangeroepen scripts heen. Wat van de kast afhangt (een `errorlevel`, een bestand
  op een andere schijf) raadt hij niet; wat in zo'n tak staat heet `(voorwaardelijk)`.
- Het is informatie: de start zelf (argv, omgeving) verandert er niet door. Wat ervan nodig is zet je zelf in `game.ini`:
  de variabelen onder `[Env]`, de argumenten in `[Launch] Arguments`, de werkmap in `[Launch] WorkingDirectory`, en de orde en
  wachttijd van twee exe's (hieronder). Staat het erin, dan zegt `--plan` `-- wij ook` en `de argumenten zijn gelijk aan de onze`.
- Los te gebruiken: `python3 scripts/startscript.py <script> --root <spelmap>` (ook `--json`).

Waarom niet uitvoeren: het script start de exe rechtstreeks (dus zonder onze lader en kern), en het doet kastdingen (`reboot`,
`rm -rf` van de gebruikersmap, `rmdir /S /Q Y:\`, `reg add`, een hardwarecontrole). Een stap die het script VOOR het spel doet
(een hardwarecontrole die exitcode 0 moet geven, een flash naar een COM-poort) doet de launcher niet; `--plan` noemt hem.

**Met welke code draaide een run?** (Stand 2026-10-16: `scripts/launch-direct.py`, `play-native.sh`, `play-halo.sh` en
`scripts/rg-identiteit.sh` zijn weg; de daemon start de game zelf.) De native route zet bovenaan het logboek van elke run een
identiteitsblok (`identity_lines` in `src/ravengun/launch/native.cpp`): het gegevensbestand van het profiel, de eigen ELF-lader,
onze eigen bibliotheken die de sandbox in gaan en de invoertabel, elk met zijn sha256; een bestand dat niet te lezen is staat er als
`(niet te lezen)`, niet overgeslagen.

**Het spel vanaf een ander pad starten (`--exe`, `[Launch] Exe`) en schijfletters (`--drive`, `[Launch] Drives`).** Het eigen
startscript van een spel start het soms vanaf een schijfletter (`x:\exe\x64\spel.exe`). Om dat na te bootsen:

```
scripts/launch-direct.py <Profiel> --plan --exe 'x:\exe\x64\spel.exe' --drive X=.
```

- `--exe PAD` (of `[Launch] Exe`) is het pad dat de lader voor het spel krijgt: een Windows-pad met een schijfletter, een
  Unix-pad, of een pad relatief aan de view. **Alleen dat pad verandert**; de prefix, de werkmap van de start, de omgeving, de
  lader en de kern, de JVS-brug, `teknoparrot.ini` naast het spel en de drop-ins blijven wat ze zonder override zijn. Zo ligt een
  verschil in de uitkomst aan het pad en niet aan de opzet. Het bestand moet bestaan en binnen de view van het spel liggen.
- `--drive X=MAP` (herhaalbaar; of `[Launch] Drives = X=.; Y=~/een/map`) zet een schijfletter in de Wine-prefix **voor de duur
  van de run**: ervoor aangemaakt, erna weggehaald, ook als de run misgaat. `MAP` is relatief aan de view (`.` = de view zelf)
  of een absoluut lokaal pad; een map op de NAS wordt geweigerd, net als de letters C en Z. Staat de letter al in de prefix en
  wijst hij naar dezelfde map, dan blijft hij staan; wijst hij ergens anders heen, dan weigert de launcher. Wat een hard
  gestopte run liet staan ruimt de volgende run op (de launcher houdt bij welke letters van hem zijn).
- `--plan` laat het zien: `spel (OVERRIDE, opgegeven als ...): X:\exe\x64\spel.exe`, of het hetzelfde bestand is als in het
  profiel, en `schijfletter X: -> <map>`. Zonder override staat er `spel: Z:\...` en verandert er niets.
- Wat niet kan (een letter die niet bestaat, een bestand dat er niet is of buiten de view ligt) wordt in het plan geweigerd
  met exitcode 5, voordat er iets start.
- De C++-engine (rg-direct, de route van de daemon) kent beide sleutels ook (2026-10-15): het plan heeft `drives`
  (`letter`, `target`, `link`, `present`) en `exe_override`; rg-direct zet de letters vlak voor de start en haalt ze in
  `run_einde` weer weg. In de engine moet de letter van `Exe` in `Drives` staan. Far Cry Paradise Lost gebruikt
  `Drives = W=.` en `Exe = W:\Farcry_R.exe`, zoals `Runme.bat` van de kast.

**Een grotere stapel voor de hoofdthread (`[Launch] StackReserve = 16M`).** Onder Wine krijgt de hoofdthread van een Windows-game
precies `SizeOfStackReserve` uit de PE-kop van de exe (vaak 1 MiB). Is dat te weinig, dan zet deze sleutel de exe in de **view** om
in een lokale kopie met de opgegeven waarde (64K tot en met 256M; K en M zijn 1024-machten, ook `0x1000000` of een getal in bytes).
Zo doet TeknoParrot het onder Wine voor Mario Kart Arcade GP DX (16 MiB). Wat er gebeurt (engine: `src/ravengun/launch/pe_stack.*`,
`apply_direct`; script: `stack_plan`/`place_stack_copy` in `launch-direct.py`; het plan heeft `stack_reserve`, `stack_src`,
`stack_dest` en `tests/direct_golden.py` vergelijkt ze):

- alleen het veld `SizeOfStackReserve` en vier bytes in de opvulling van de DOS-stub (0x78) veranderen. Die vier bytes houden de
  CRC32 van de eerste 0x400 bytes gelijk aan die van het origineel: daaraan herkent de OpenParrot-kern een game
  (`GameDetect.cpp`, `GetCRC32(moduleBase, 0x400)`), dus de kopie wordt herkend als de exe zelf;
- de koppeling van de view naar het origineel blijft als `<exe>.ravengun-orig` staan; een volgende start gaat van dat origineel uit,
  en schrijft niets als de kopie al klopt. Heeft de exe zelf al minstens zoveel stapel, dan blijft alles zoals het is;
- nooit op de NAS: ligt de game niet in een view (`scripts/tp-add-game.py <Game> <exe> --view`), dan weigert het plan met exitcode 5.
  Terug naar het origineel: de sleutel weghalen en `<exe>` in de view vervangen door `<exe>.ravengun-orig`.

**Een bestand dat de game zelf beschrijft lokaal maken (`[Launch] ViewCopies = A, B`).** Sommige games schrijven records of
instellingen naast de exe. In de view is zo'n bestand een link naar de NAS, dus de game zou daar schrijven. Met `ViewCopies` wordt
de link bij de start één keer een lokale kopie van het origineel (lezen van de NAS mag). Daarna blijft het lokale bestand staan,
want dat is de stand van de game. Paden zijn relatief aan de map van de exe, zonder `..`. De game moet in een view staan, anders
weigert het plan. Mars Sortie (2026-10-15) schreef bij elke start `GameRecords1.dat` en `GameRecords2.dat`. Zonder `ViewCopies`
kregen die bestanden op de NAS een nieuwe mtime; met `ViewCopies` blijft de NAS gelijk. In de engine: `place_view_copies` in
`direct.cpp`, in het plan `view_copies`.

**Een TrueType-collectie als losse font (`[Launch] ViewTtcAsTtf = A.ttc, B.ttc`).** Wine's `GdipPrivateAddMemoryFont` neemt alleen
een losse TrueType- of OpenType-font aan (kop `0x00010000` of `OTTO`); bij een collectie (kop `ttcf`) vindt hij geen naam en blijft
het aantal families 0. Silent Scope: Bone Eater (2026-10-17) stopte daarop met "No font loaded or no font families data in font file"
(`WindowsFont.cpp`; gemeten met `[Wine] Debug = trace+gdiplus`: Fuji Extended laadde, `df_heiseigothic_w5.ttc` niet). Met
`ViewTtcAsTtf` wordt de link in de view bij de start een lokaal bestand met face 0 van de collectie als losse font (`ttc.hpp`); het
origineel op de NAS blijft gelijk en een lokaal bestand dat al een losse font is blijft staan. Paden zijn relatief aan de **wortel
van de view** (zoals `ViewRealDirs`, niet aan de map van de exe), zonder `..`. De map eromheen moet in de view een echte map zijn:
zet hem in `ViewRealDirs`, anders weigert de start (op de NAS wordt nooit geschreven). De naam mag in andere hoofdletters staan dan
het spel vraagt (`data\Font\DF_HeiSeiGothic_W5.ttc`): in de lokale view zoekt Wine zelf hoofdletterongevoelig. In de engine:
`place_view_ttc_fonts` in `direct_apply.cpp`, in het plan `view_ttc_fonts`.

*Waarom niet in de schrijflaag (zoals zaaigoed)?* Gemeten 2026-10-17: de share van de NAS (cifs) is hoofdletterongevoelig. Wine
probeert eerst het pad precies zoals het spel het vraagt, en de overlay vindt `data/Font/...` dan rechtstreeks in de onderlaag op de
NAS. Een bestand in de bovenlaag met de naam zoals hij op de NAS staat (`data/font/...`) wordt dan overgeslagen. Dat geldt ook voor
zaaigoed: een bestand onder `seed/` moet dezelfde hoofdletters hebben als het pad dat het spel opent.

**Secties tot hun bladzijde (`[Launch] PeFillPages = true`).** Windows mapt de laatste bladzijde van een sectie helemaal uit het
bestand; Wine zet alles na `SizeOfRawData` (en `VirtualSize`) op nul. Een exe die daar code heeft, springt onder Wine in nullen. Aliens:
Extermination (`aliens dehasped.exe`, 2026-10-17) heeft zijn HASP-emulatie achter het einde van `.text` (`SizeOfRawData` 0x55DB6, 573
niet-nul bytes tot de grens) en viel met een page fault op 0x456DF1. Met `PeFillPages` wordt de exe in de view een lokale kopie (dezelfde
als bij `StackReserve`, beide in één kopie) waarin elke sectie met data in die staart tot de volgende `FileAlignment`-grens loopt
(`pe_fill_section_pages` in `pe_stack.hpp`; nooit voorbij het bestand, de data van een andere sectie of de bladzijde van `VirtualSize`).
De CRC32 van de eerste 0x400 bytes blijft gelijk (`pe_keep_header_crc`): met een gewijzigde kop zonder die correctie stopte de kern van
TeknoParrot de game met exitcode 1337. Het origineel blijft bereikbaar als `<exe>.ravengun-orig`; de view geldt daarmee nog als de view van
de game, ook als `[Identify]` een `Sha256` heeft.

**BepInEx op de directe route (`[Launch] BepInEx = 5.4.21.0`).** Voor een Unity-game die met een kleine eigen plugin wel draait.
De launcher zet de officiële release (Mono, de bitbreedte van de game) uit `<data>/bepinex/<versie>/<x64|x86>` naast de exe in de view:
`winhttp.dll` (de doorstop, `winhttp=n,b`), `doorstop_config.ini` en `BepInEx/core/`; `RAVENGUN_BEPINEX_ROOT` kiest een andere map.
Ontbreekt de release, dan weigert de start met de opdracht `scripts/fetch-bepinex.sh <versie>` (download van github.com/BepInEx met
sha256-controle; niets in git). De plugin zelf is een gewone drop-in van de game (`dropin/`, `To = BepInEx/plugins`, relatief aan de map
van de exe); de bron staat in `plugins/bepinex/<Game>/` en `scripts/build-bepinex-plugin.sh <Game>` bouwt hem in de Mono-container van
de Unity-route. Een eigen drop-in met dezelfde naam (bv. een andere `winhttp.dll`) wint. Men in Black (2026-10-17): zonder de shell van de
kast stopt de game zichzelf (`Application.Quit`); de plugin `RavenGun.MenInBlack` onderdrukt dat. In de engine: `dropin_plan` in
`direct_files.cpp`, in het plan `bepinex_dir`; het logboek noemt `bepinex: <map>`. Let op: met de gesloten kern van TeknoParrot laadde
BepInEx bij Men in Black niet (gemeten); de game draait daarom op onze kern (`[Loader] Core = ravengun`).

**Een bestand van de game door een ander bestand van de game vervangen (`[Launch] ViewLinks = A -> B`).** In de **view** wordt `A`
een symlink naar het bestand waar `B` naar wijst (meestal op de NAS); er wordt niets gekopieerd en niets op de NAS geschreven.
Paden ten opzichte van de map van de exe, relatief, zonder `..`; meerdere paren gescheiden door komma's. Voorbeeld: Mario Kart
Arcade GP DX laadt `Data/System/Shader/Polygon2D_D3D10.fx` als HLSL-bron (`D3DX10CreateEffectFromFileA`, `fx_4_0`) en die bron
compileert de shadercompiler van Wine niet; de game levert hetzelfde effect voorgecompileerd mee (`Polygon2D_D3D10.cfx`) en
`d3dx10` van Wine neemt een voorgecompileerd effect (`DXBC`) zonder compiler. Wat er gebeurt (engine: `parse_view_links`,
`place_view_links` in `direct.cpp`; script: `view_links_plan`/`place_view_links` in `launch-direct.py`; het plan heeft
`view_links` en `tests/direct_golden.py` vergelijkt ze):

- de mappen op weg naar `A` worden eerst echte lokale mappen met symlinks (zoals bij een drop-in), dan wordt `A` de symlink;
- een volgende start schrijft niets als de link al klopt (`viewlink: ... is al actueel`); een ontbrekend `B`, of een echt (lokaal)
  bestand op de plaats van `A`, is een fout: dan start de game niet;
- ligt de game niet in een view, dan weigert het plan met exitcode 5. Terug naar het origineel: de sleutel weghalen en in de view
  `ln -sfn <gamemap>/A <view>/A`.

**De invoertabel van de native route (`rt-invoer.ini`).** Een native Raw Thrills-spel leest zijn schakelaars, geweerknoppen en
richtpunt uit zijn eigen bordstructuur, niet uit de invoerapparaten van het systeem. De invoerhulp (`librg_rt_input.so`,
`tools/rt-input-shim/`) leest het bordbeeld van de daemon en zet de waarden in het geheugen van het spel. WAT waarheen gaat staat
in een tabel per game, niet in de hulp: `patches/<Game>/rt-invoer.ini`, een regel per ding.

```
build       getter=0x61e890 signatuur=85ff0f88d0050000              # alleen op deze build iets doen
tabel       basis=0x180ae80 eerste=0xaea0 per_bord=0x2200 per_schakelaar=0x50
schakelaar  bit=6 bord=0 k=0 puls=300                              # start speler 1: een puls van 300 ms
schakelaar  bit=7 bord=0 k=9 puls=0                                # een knop: ingedrukt zolang de bit staat
bit         bit=7 adres=0x1234560 breedte=4 doelbit=0               # of: een bit in een woord van het spel
as          bron=12 adres=0x1234570 soort=u8 min=0 max=255 inverteer=nee marge=2
```

- De bits en de assen zijn die van het bordbeeld (`packaging/tp-shm-profiles.json`): het bitveld op offset 8, de asbytes op 12..19.
- Een as wordt omgerekend met de regel van de IO-brug (`src/ravengun/iobridge/tp_shm.cpp`): eerst afronden, dan inverteren, dan
  klemmen tussen `min+marge` en `max-marge` -- behalve als de speler buiten beeld mikt (in het beeld een byte van 0, 1, 254 of
  255): dan blijft de uiterste waarde staan.
- `play-native.sh` geeft de tabel vanzelf mee: staat er een `rt-invoer.ini` in de patchmap van het spel en gebruikt de run de
  hulp, dan gaat hij alleen-lezen de sandbox in (`/rg/tabel/rt-invoer.ini`) en krijgt de hulp het pad (`RG_RT_TABLE`). De
  gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/rt-invoer.ini`) gaat voor de meegeleverde -- als HEEL bestand, niet per
  regel zoals bij `game.ini`. Het logboek van de run noemt de tabel met zijn sha256 (`invoertabel: <pad> sha256 ...`). Zonder
  tabel verandert er niets aan het commando. Een `--env RG_RT_TABLE=<pad>` op de opdrachtregel gaat voor.
- Buiten `play-native.sh` vindt de hulp de tabel via `RG_RT_TABLE=<pad>`, of als `rt-invoer.ini` naast de bibliotheek.
- Drie regelsoorten voor een game die zijn geweren in een eigen laag houdt: `zet adres= breedte= waarde=` (een vaste waarde die
  de hulp vasthoudt, bv. "er is een geweer"), `teller bit= adres= breedte= modulo=` (een knop die de game als overgangsteller
  leest: een telling die omloopt, bit 0 = ingedrukt) en bij een as `schaal=`, `plus=` en `buiten=` (een kommagetal met een eigen
  bereik: na de rekenregel maal schaal plus plus; buiten beeld een vaste waarde).
- Zonder tabel doet de hulp wat hij altijd deed (alleen de kastschakelaars). Een tabel zonder `schakelaar`-regels houdt die
  ingebouwde schakelaars en voegt toe; met `schakelaar`-regels vervangt hij ze.
- Een tabel die niet klopt (een onbekend woord, een adres waar niet geschreven kan worden) wordt in zijn geheel afgekeurd, met de
  reden op stderr, en dan komt er GEEN invoer: liever niets dan schrijven op een verkeerde plek.
- Het volledige voorbeeld met uitleg: `tools/rt-input-shim/rt-invoer.voorbeeld.ini` (dat is precies de ingebouwde standaard).

**De camera (`[Camera]`).** Een spel krijgt alleen een camera als het hem IN het spel gebruikt. Wil het hem alleen voor de
inrichting van de kast, dan krijgt het er geen en hoort het zonder te draaien. Dat is een instelling per game, in `game.ini` (de
gebruikerslaag gaat per sleutel voor):

```ini
[Camera]
Use = game      ; game = gebruikt de camera tijdens het spelen: het beeld van de echte camera
                ; presence = wil alleen dat er een camera IS: een virtuele met een kunstbeeld, NOOIT de echte
                ; setup = alleen voor de inrichting: niets | none = niets (standaard)
Device = auto   ; auto = de camera uit de instellingen van de app | /dev/videoN = precies die | none
```

- Zonder `[Camera]` krijgt een spel geen camera en verandert er niets aan hoe het gestart wordt.
- WELKE camera `auto` is, kiest de gebruiker in de app (Settings, Display, "Camera for games"): automatisch (de eerste camera
  van de machine), een bepaalde camera, of geen. "Geen" daar wint ook van een spel dat erom vraagt. Het staat in
  `~/.config/ravengun/ravengun-app.conf` als `[Games] camera = auto | none | /dev/videoN`.
- Op de native route bindt `play-native.sh` de camera in de sandbox met `--dev-bind-try` (zoals `/dev/dri`). Een gewone `--bind`
  werkt niet: de toegang van de gebruiker komt van een ACL op het apparaat en die overleeft de koppeling niet (gemeten:
  `open()` geeft `Permission denied`). `-try`: een machine zonder die camera geeft geen fout.
- De echte camera kan maar door een proces tegelijk gestreamd worden (gemeten: naast de daemon geeft `VIDIOC_REQBUFS`
  `Device or resource busy`). Met de rol `game` kan een spel hem dus openen maar niet streamen zolang de daemon draait; het
  logboek zegt dat erbij. De virtuele camera (een bibliotheek in het spelproces, gevoed door de daemon) lost dat op en is er nog
  niet; tot die tijd geeft de rol `presence` niets door, en het logboek zegt ook dat.
- De instelling staat voor ELKE route in `game.ini`, ook voor een native spel (`patches/Halo/game.ini` bestaat; een `game.ini` in
  de gebruikerslaag met alleen `[Camera]` is genoeg). `native.json` beschrijft de sandbox, niet de keuzes.
- Een game.ini kan niets anders dan `/dev/videoN` vragen; een andere waarde, of een onbekende rol, stopt de start met een melding.
- Het logboek van elke run zegt wat er gekozen is: `camera: /dev/video0 doorgegeven (rol game: ...)` of `camera: geen (rol ...)`.
- Zelf nakijken: `python3 -I scripts/camera.py list` (de camera's) en `python3 -I scripts/camera.py resolve <Game>` (de keuze).
- Op de Wine-routes is er geen sandbox, maar Wine zelf biedt geen videocamera's aan via Media Foundation (gemeten in `mf.dll`
  van proton-11.0-2: `MFEnumDeviceSources` geeft bij VIDCAP `*count = 0`). Daarom de **virtuele camera van de daemon**: start de
  IO-brug een game met `Use = game` en is de gekozen camera die van de daemon (`auto`, of hetzelfde `/dev/videoN`), dan schrijft de
  daemon het beeld van zijn camera (YUY2, de tracking loopt door) in `/dev/shm/ravengun_Camera`
  (`src/ravengun/vision/camera_feed_format.h`); stopt de route, dan is het bestand weg. In het spelproces biedt de drop-in
  `rg-mfcam.dll` (`tools/rg-mfcam/`, bouwen met `tools/dev/build-mfcam.sh`, `Preload = true`) dat bestand aan als Media
  Foundation-camera ("RavenGun camera"; YUY2, NV12 en RGB32). Eerste gebruiker: Mario Kart Arcade GP DX. Een tweede camera
  (niet die van de daemon) wordt op de Wine-route nog niet doorgegeven; het logboek van de daemon zegt dat
  (`camera voor het spel aan/uit: ...`). `ravengunctl --json camera` toont `game_feed`.

**Twee exe's (`HasTwoExecutables`).** Een profiel met een los hulpprogramma naast het spel (een daemon) wordt door
`launch-direct.py` in een vaste orde gestart: de tweede exe eerst als `LaunchSecondExecutableFirst = true`, anders het spel eerst;
daarna de wachttijd; daarna de andere. Beide krijgen dezelfde wine, dezelfde lader en kern, dezelfde werkmap, dezelfde prefix en
dezelfde omgeving, en `teknoparrot.ini` komt naast beide exe's. De tweede exe draait buiten gamescope. Sleutels:

- het pad van de tweede exe: `[Game] GamePath2` (een eigen pad, zoals `GamePath`), anders `GamePath2` van het XML-profiel, anders
  `[Identify] Path2` in dezelfde spelmap als `Path`. Staat het spel in een view, dan moet de tweede exe in DEZELFDE view staan; een
  pad dat rechtstreeks naar de NAS wijst wordt het pad in de view, en lukt dat niet dan weigert de launcher (exitcode 4);
- `[Launch] SecondWait`: de wachttijd in seconden tussen de twee starts (0 tot 600, standaard 5);
- `[Launch] SecondArguments`: de argumenten van de tweede exe (anders `SecondExecutableArguments` van het XML-profiel).

`--plan` laat beide starts zien met de orde en de wachttijd. Is een start al weg voordat de volgende begint, dan stopt de run met
exitcode 7 en een FOUT-regel: een halve opstelling is geen geldige run. Een lader die eerder stopt dan het programma dat hij
startte telt niet als weg (er wordt ook naar de procesnaam van de exe gekeken). Na het einde van het spel wordt de tweede exe
gestopt. De uitvoer en de exitcode van elke start staan apart in `~/.local/state/ravengun/starts/<Profiel>-<tijd>/`
(`1-<naam>.log`, `2-<naam>.log`, `starts.json`), en in het logboek draagt elke regel de naam van de start die hem schreef.
Een game met een enkele exe gaat de oude weg, ongewijzigd.

**ConfigValues.** De sleutel is `Categorie/Veldnaam` en wordt bij de eerste `/` gesplitst (categorieën bevatten er geen; veldnamen
wel, zoals `Keyboard/Button Axis`). `[ConfigType]` bepaalt welke velden er zijn en in welke volgorde ze in `teknoparrot.ini` komen;
`[Config]` geeft de waarde (afwezig of leeg = leeg veld); `[ConfigOptions]` de keuzelijst. Een sleutel in de gebruikerslaag wint per sleutel;
`Categorie/Veld =` (leeg) maakt het veld leeg. `teknoparrot.ini` uit het eigen profiel is byte voor byte gelijk aan die uit de XML
(`tools/compare-launch-plans.py` controleert dat voor alle profielen). Wat het INI-formaat niet kan dragen (een waarde met ` ;` of een
regeleinde, een `=` in een naam) weigert de importer met een melding; de game blijft dan op de XML terugvallen.

**Revisie.** Is `[Game] ProfileRevision` anders dan `GameProfileRevision` van het sjabloon in de runtime, dan weigert `launch-direct.py`
(exitcode 4, zoals een niet ondersteund profiel) met de melding dat het profiel opnieuw geïmporteerd moet worden:
`scripts/import-tp-profiles.py <Game>`.

### De game vinden: gamemappen en `[Identify]`

Het pad naar de game staat **nergens**: niet in de repo en normaal ook niet in de gebruikerslaag. De app bewaart alleen de gamemappen
(`~/.config/ravengun/library.json`, `folders`; "Add Folder" in Settings > Paths, bij Dolphin `ISOPath0..N`) en herkent een game daarin aan
`[Identify]` (bij Dolphin het game-ID). Het pad wordt afgeleid, in deze volgorde:

1. `[Game] GamePath` in de gebruikerslaag (een afwijkend pad dat de gebruiker zelf koos; de uitzondering, nooit in de repo);
2. een lokale view `~/.local/share/ravengun/runtime/views/<Game>/` (door `tp-add-game.py --view` gemaakt, of vanzelf bij een start: zie "De view") waarin `Path` bestaat;
3. met `Folder`: de map `<gamemap>/<Folder>` in elke gamemap (zonder de andere mappen te bekijken);
4. de map die de app in `library.json` (`games`: `folder`, of de map van `exe`) bij dit profiel heeft;
5. de gamemappen doorzoeken: elke submap van een gamemap (en de gamemap zelf) waarin `Path` bestaat en waarvan de grootte (`Size`) en
   eventueel de sha256 (`Sha256`) kloppen.

Bij 3 en 4 gelden dezelfde controles als bij 5; ze zijn er omdat elke kandidaat op de NAS stat-aanroepen kost en een `Sha256` het hele
hoofdbestand laat lezen. De sha256 wordt onthouden per pad, grootte en mtime: in het geheugen en in
`~/.cache/ravengun/sha256-cache.tsv` (`$XDG_CACHE_HOME`; `RAVENGUN_HASH_CACHE` kiest een ander bestand, `off` = alleen in het geheugen),
gedeeld door de engine, rg-direct en de scripts. Een tweede start hasht dus niets opnieuw; op de NAS wordt niets geschreven.

`Path` is relatief aan de map van de game. `Size` is de grootte van dat bestand. `Path2` (alleen bij games met twee exe's, HOTDSD) is het pad van de tweede exe
in dezelfde map; het zet `GamePath2` in de XML voor TeknoParrotUi en is bij een directe start het pad van de tweede start (zie "Twee exe's" hieronder). Hebben twee games een identiek hoofdbestand (twee Unity-games met
dezelfde `Game.exe`), dan legt de importer de `Sha256` vast en zo nodig `Folder` (de naam van de map, hoofdletters maken niet uit). Een andere
versie van het bestand (andere grootte) wordt niet herkend; kies dan het pad zelf (`GamePath`) of importeer opnieuw. Python
(`ownprofile.locate_game`), de app (`gamesettings::locate_game`) en de engine gebruiken dezelfde regels; de gamelijst van de engine hasht
niet en let dan op pad, grootte en mapnaam (de directe route van de engine en de scripts controleren de sha256 wel, uit de cache; de
bibliotheekscan van de app hasht zelf, zonder de cache).

### De view (`[Launch] View`)

Een game op de NAS (`~/mnt`, of een cifs/smb/nfs/sshfs-koppeling) draait op de directe route vanuit een **view**: een lokale map
`~/.local/share/ravengun/runtime/views/<Game>/` met symlinks naar de gamemap. Wij schrijven nooit op de NAS; zonder view kreeg zo'n game
geen `teknoparrot.ini` (Windowed, FreePlay, resolutie, DongleRegion, netwerk: "ligt op de NAS en ontbreekt; niet geschreven") en konden
drop-ins nergens heen. In een view staan:

- symlinks voor alles van de game (grote bestanden, de eigen saves en instellingen van de game: die woont op de NAS en schrijft daar zelf);
- **echte lokale mappen** voor de map van de exe en elke map erboven tot de wortel van de view (daar komen `teknoparrot.ini` en drop-ins met
  `To = .`), voor `BepInEx/` (onze plugin; lokaal gekopieerd) en voor de mappen uit `ViewEmptyDirs` (leeg);
- bij een 32-bit ElfLdr2-game een link `ElfLdr2` naar de lader van TeknoParrot.

Een start (de app/daemon, `launch-direct.py --check`/`--run`, `rg-direct --plan --make-view`) maakt de view **vanzelf** als die nog niet
bestaat, met precies de opbouw van `scripts/tp-add-game.py <Game> <exe> --view [--view-up N] [--empty-dir REL]` (C++:
`src/ravengun/launch/gameview.*`, Python: `scripts/gameview.py`, dat ook `tp-add-game.py` gebruikt; `tests/test_gameview.py` vergelijkt de
drie bomen op een nep-NAS). Daarna rekent het plan met de exe in de view, en `teknoparrot.ini` wordt lokaal naast de exe geschreven (staat
er een link naar een `teknoparrot.ini` op de NAS, dan gaat alleen die link weg; het bestand op de NAS blijft ongemoeid).

| sleutel | waarde | gelijk aan |
|---|---|---|
| `[Launch] View` | `auto` (leeg, standaard): een view als de game op de NAS staat; de wortel is de map waar `[Identify] Path` begint (zonder `Path` de map van de exe). `off`: de oude route, rechtstreeks van de NAS (geen view, geen `teknoparrot.ini`, drop-ins geweigerd). Een getal `N` (0..99): altijd een view, ook voor een lokale game, met de wortel `N` mappen boven de map van de exe. | `--view`, `--view-up N` |
| `[Launch] ViewEmptyDirs` | paden ten opzichte van de view-wortel, gescheiden door komma of puntkomma; elk wordt een lege lokale map in plaats van een link (bv. `RSGame/Content/Movies` voor Raccoon Rampage). `..` mag niet. | `--empty-dir REL` (herhaalbaar) |
| `[Launch] ViewRealDirs` | idem; elk wordt een **echte** lokale map met links naar de inhoud, zoals de map van de exe. Proton meldt een gelinkte map als map plus reparse point (`GetFileAttributes` = 0x410, gemeten 2026-10-16); een game die op precies `FILE_ATTRIBUTE_DIRECTORY` (0x10) controleert, mist hem dan (Haunted Museum II: `.\data` -> `chdir("..\")`, SOUND ERROR). Een bestaande view wordt bij de volgende start omgezet. | `--real-dir REL` (herhaalbaar) |

Regels:

- **Bestaat de view al** en bevat hij deze game, dan blijft hij zoals hij is (ook een met de hand gemaakte); alleen de links worden
  ververst: in de wortel en in de echte mappen op weg naar de exe komt een link bij voor wat er op de NAS bij is gekomen, en links naar een
  verdwenen bestand in de gamemap gaan weg. Echte bestanden en mappen (lokale `teknoparrot.ini`, drop-ins, lege mappen, `BepInEx/`)
  blijven staan. Een bestaande, niet-lege `views/<Game>/` zonder deze game wordt niet aangeraakt: de start weigert dan ("bestaat al maar
  bevat de game niet"; maak hem opnieuw of zet `View = off`).
- **NAS-veiligheid.** Elke stap (map maken, link zetten of weghalen, bestand kopieren) controleert eerst dat de map waarin hij schrijft
  lokaal is (gevolgd); komt de view-map zelf op de NAS uit, dan weigert de start met een melding. Na het maken controleert de launcher
  onze schrijfpaden (de view, de mappen op weg naar de exe, `BepInEx/`, de lege mappen), zoals `pruef_schrijfpaden` in
  `tools/dev/aagames-setup.py` en `nas_write_errors` in `unity.cpp`.
- **Droog plannen maakt niets.** `launch-direct.py --plan` en `rg-direct --plan` (en dus `tests/direct_golden.py`, ook `--real`) maken
  of verversen geen view: voor een game op de NAS zonder view rekent het plan dan met het pad op de NAS en meldt het "wordt bij de start
  gemaakt". Dat is het enige verschil met een start; `tests/direct_golden.py` plant hermetisch beide kanten nog eens met de views van
  een start (`--make-view`) en vergelijkt plannen en bomen (TC5 op de nep-NAS: droog geweigerd, bij een start een view en gelijk).
- Een ongeldige waarde (`View = soms`) weigert de start ("Instelling geweigerd").

### Importer

`scripts/import-tp-profiles.py [--all | PROFIEL...] [--runtime TP] [--out DIR] [--user DIR] [--game-folder DIR]... [--hash] [--add-to-library] [--dry-run] [--check]`
leest uit de runtime (alleen gelezen) `UserProfiles/<P>.xml` en `GameProfiles/<P>.xml` en schrijft:

- in `--out` (standaard `patches/`): de statische delen uit het sjabloon, met een commentaarregel boven `[Identify]` die het sjabloon en de revisie
  noemt. Handwerk en commentaar in een bestaand `game.ini` blijven staan; alleen de beheerde sleutels en de secties `[Identify]`, `[Config]`,
  `[ConfigType]` en `[ConfigOptions]` worden opnieuw gezet (idempotent: een tweede run verandert niets);
- in `--user` (standaard de gebruikerslaag): alleen wat het gebruikersprofiel anders heeft dan het sjabloon (waarden van de ConfigValues,
  afwijkende profielvelden, een eigen wine-pad) en `[Game] GamePath` als de gamemappen de game niet op hetzelfde bestand terugvinden als het
  huidige `GamePath` van het profiel.

`--game-folder` geeft de gamemappen op (naast die uit `library.json`); ze bepalen de wortel van `[Identify]`. `--add-to-library` voegt ze ook toe aan
`library.json`. Nooit overgenomen: `JoystickButtons` en andere invoertoewijzingen van TeknoParrotUi (onze invoer staat in `[Input]` en
`[Bindings]`), sleutels, serials, tokens of de activering. Een gebruikersprofiel met een andere revisie dan het sjabloon wordt alleen als
sjabloon geïmporteerd (met een melding). Een game zonder sjabloon in de runtime wordt overgeslagen of, bij een expliciete naam, als fout gemeld.

`tp-add-game.py` zet voor een nieuwe game het hele profiel uit het sjabloon in de **gebruikerslaag** (er is geen meegeleverd profiel), of alleen de
keuzes van de aanroeper als er al een meegeleverd profiel is; het schrijft daarnaast `UserProfiles/<P>.xml` als uitvoerformaat (zie hieronder).

### TeknoParrotUi en de XML als uitvoerformaat

Voor een start via TeknoParrotUi (`[Launch] Mode = tpui`, bv. HOTDSD, en de terugval als direct niet kan) schrijft `play-halo.sh`
vóór de start `UserProfiles/<P>.xml` uit `game.ini` (`scripts/gamesettings.py write-xml <P>`): bestaat het bestand, dan worden alleen `GamePath`,
`WineRunnerPath` en de waarden van de ConfigValues bijgewerkt (knoppen en wat TeknoParrotUi zelf wijzigde blijven staan); anders wordt het uit het
sjabloon gemaakt, met de bindingen van RavenGun (`[Bindings]`, `scripts/tpxml.py`). De directe start (`launch-direct.py`) leest de XML niet. Een
game zonder eigen profiel gedraagt zich als voorheen (XML, en het oude `.env` als laagste laag).

### Abonnement

`needs_activation` in `launch-direct.py` is waar zodra het sjabloon in `GameProfiles/` `Patreon = true` zegt, **of** een laag van `game.ini`
(standaard van het soort, meegeleverd of gebruiker) `Patreon = true` zegt. Geen enkele laag kan het uitzetten: `Patreon = false` of een lege
waarde in de gebruikerslaag telt niet. `check_activation` is ongewijzigd (alleen kijken of TeknoParrotUi de activering in de prefix zette); er wordt
geen sleutelwaarde gelezen en geen activering geïmporteerd of nagebootst. `Patreon` in `game.ini` is een informatieve true of false, nooit een sleutel.

## Unity-games (`unity.json`)

Tomb Raider Arcade en Rabbids Hollywood Arcade draaien zonder TeknoParrot: de daemon zet een lokale view klaar (symlinks naar de game
op de NAS, BepInEx en onze plugin lokaal), start `proton run Game.exe` en is de TCP-klant van de plugin in de game
(`src/ravengun/launch/unity.*`, `src/ravengun/games/aagames.*`). Het profiel van zo'n game is `patches/<Game>/unity.json`; een game met
dat bestand gaat over de Unity-route (tenzij `[Input] Route = tp-shm` in `game.ini` staat). Zoals `game.ini` en `native.json`: de
gebruikerslaag eerst, en dat bestand vervangt het meegeleverde als geheel; is het kapot, dan meldt de start dat (met het bestand), zonder
terugval op het meegeleverde bestand. De daemon leest het bij elke start, de app bij het laden van de gamegegevens.

```json
{
  "name": "Tomb Raider Arcade",
  "type": "tomb_raider",
  "folder": "Tomb Raider",
  "exe": "Game.exe",
  "plugin": "ravengun",
  "plugin_dir": "TombRaider",
  "fullscreen": true,
  "width": 1920, "height": 1080,
  "offset_x": 0.0, "offset_y": 0.0, "scale_x": 1.0, "scale_y": 1.0,
  "y_origin": "bottom",
  "send_hz": 120
}
```

| sleutel | betekenis |
|---|---|
| `name` | weergavenaam (standaard de mapnaam van het profiel) |
| `type` | `tomb_raider` of `rabbids_hollywood`: het draadprotocol (42 of 38 bytes invoer, andere uitvoer) |
| `folder` | de **mapnaam** van de game in een gamemap, exact (ook een spatie aan het eind: `"Rabbids Hollywood "`); geen pad |
| `nas_dir` | alleen in de gebruikerslaag: een eigen, absoluut pad (`~/` mag); gaat voor `folder` |
| `exe` | het uitvoerbare bestand in die map (standaard `Game.exe`) |
| `plugin` | `ravengun` (standaard, `plugins/unity/`) of `demulshooter` (terugval); `RAVENGUN_UNITY_PLUGIN` gaat voor |
| `plugin_dir` | map van de DemulShooter-plugin (alleen bij `demulshooter`) |
| `fullscreen`, `width`, `height`, `language` | beginwaarden van het configuratiebestand van de plugin in de view (alleen als dat ontbreekt) en de resolutie van het gamevenster voor de aim |
| `offset_x/_y`, `scale_x/_y`, `y_origin`, `send_hz` | kalibratie van de aim (na de `set_output`-transformatie van de gun), Y-oorsprong en pakketten per seconde |

**De map van de game** staat dus niet als pad in het bestand (dat is per machine anders) maar als mapnaam, en wordt bij elke start
gezocht in de gamemappen van de app (`library.json`, `folders`), met dezelfde regel als `@nas/<Map>` van de native route: eerst
`<gamemap>/<folder>` in elke gamemap, dan een niveau dieper (`<gamemap>/*/<folder>`). Staat hij er niet, dan zegt de start welke map
er gezocht werd en in welke gamemappen. De bibliotheek van de app herkent de game op dezelfde manier.

De instellingen voor alle Unity-games samen (poort van de plugin 33610, versie van BepInEx, versie van DemulShooter voor de terugval) staan
in `packaging/unity-route.json`; ontbreekt dat bestand, dan gelden dezelfde waarden als standaard. Voor de scripts
(`tools/dev/aagames-setup.py`, `scripts/build-unity-plugin.sh`) leest `scripts/unityprofile.py` dezelfde bestanden en dezelfde regel.

## Zaaigoed (`seed/`)

Sommige games bewaren iets in hun eigen map dat ze nodig hebben om goed te spelen, en de fabrieksversie is niet goed genoeg. Gaia
Attack 4 bewaart zijn geweerkalibratie in `OpenParrot/GA40.CFG` en `GA41.CFG` (met een `.CRC` ernaast). De fabriekskalibratie
ligt 20% naast het beeld. `patches/<Game>/seed/` heeft dezelfde mappen als de spelmap. Bij elke directe start
(`src/ravengun/games/seed.hpp`) komt elk bestand daaruit in de schrijflaag van de spelmap, maar alleen als het daar nog niet
staat. Wat de game zelf schrijft gaat dus altijd voor: kalibreert een speler opnieuw in het testmenu, dan blijft die kalibratie
staan. Een versie op de NAS telt niet mee. Die blijft onaangeroerd en het zaaigoed ligt eroverheen. Dat is anders dan `seed` in
`native.json`, waar een bestand op de NAS wint. De links van een view worden gevolgd, en een doel buiten de spelmappen (een
lokale map in de view) krijgt de kopie rechtstreeks. Het logboek noemt elk bestand ("zaaigoed: ... -> ..." of "staat er al").
Zaaigoed maak je door het spel zelf te laten schrijven (het testmenu, met de muismodus van de daemon) en het resultaat uit
`~/.local/share/ravengun/writelayer/<Game>/<spelmap>/upper/` te kopieren. Nooit dongle- of sleutelgegevens.

## Dongle per game

**De loader weet niets van dongles.** Dat is de beslissing van de eigenaar (ronde 12) en die staat in
`docs/geen-dongle-in-de-loader.md`. Er is dus **geen** `[Dongle]`-sectie in `game.ini`: geen
`Enabled`, geen `Source`, geen keuzelijst in de app en geen poort die een start weigert.

Een software-dongle is een **gewone drop-in**. Alles wat een game nodig heeft staat in de map van die
game:

| Wat | Waar |
|---|---|
| de bestanden die in de view gezet worden (de emulator, de inline-hook) | `patches/<Game>/dropin/` + `dropin.ini` |
| de donglebeschrijving en de gegevens | `patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json` + `memory/` |
| of de game de dongle afdwingt | het veld `enforced` in die `dongle.json`, en de notitie in `patches/<Game>/README.md` |

De launcher kopieert die bestanden naar de view van de game en zet de omgevingsvariabelen uit
`dropin.ini` (zoals `RAVENGUN_HASP_DIR` en `RAVENGUN_HASP_GAME`). Meer niet. Zie `docs/dropins.md`
voor het contract en `docs/geen-dongle-in-de-loader.md` voor waarom het zo is.

Controleren of de dongle van een game compleet is:

```bash
cd ~/projects/ravengun && python3 scripts/hasp.py --game <Game> --json    # ok: true = compleet
python3 scripts/launch-direct.py <Game> --plan                            # kijk naar de dropins-regel
```

## Zoals Dolphin

Het model is Dolphin's GameSettings; dit zijn de overeenkomsten en de enige uitzondering:

| Dolphin | RavenGun |
|---|---|
| `Sys/GameSettings/<ID>.ini`, meegeleverd | `patches/<Game>/game.ini` (met `_defaults/<Type>.ini` als de standaard van het soort) |
| `User/GameSettings/<ID>.ini`, wint per sleutel | `~/.local/share/ravengun/patches/<Game>/game.ini`; een lege waarde haalt een sleutel weg |
| de gebruikerslaag bevat alleen niet-standaardwaarden | idem: de app (Properties) en `tp-add-game.py` schrijven alleen verschillen met de meegeleverde laag; een waarde die terug gelijk is, verdwijnt uit de gebruikerslaag |
| `Dolphin.ini [General] ISOPath0..N`: alleen de spelmappen | `library.json` `folders` (Settings > Paths > Game Folders) |
| een game wordt herkend aan zijn ID bij het scannen | aan `[Identify]` (pad, grootte, eventueel sha256) in de gamemappen; het pad wordt afgeleid, niet opgeslagen |
| per game wordt over paden niets opgeslagen | idem; alleen een handmatig gekozen afwijkend pad staat als `[Game] GamePath` in de gebruikerslaag |
| vaste secties per onderwerp (`[Core]`, `[Video_Settings]`) | `[Game]`, `[Launch]`, `[Loader]`, `[Wine]`, `[Display]`, `[Input]`, `[Dongle]`, `[Env]`, `[Bindings]`, `[Status]`, en voor het profiel `[Identify]`, `[Config]`, `[ConfigType]`, `[ConfigOptions]` |
| Properties-dialoog met tabs | Properties: General, Options, Display, Patches, Memory Card, Info; ze schrijven alleen verschillen |

## `patches.ini`

Elke patch is een eigen sectie met een naam, zoals `$Naam` bij Dolphin. Een patch die meer games delen staat **één keer** als
definitie in `_defaults/<Type>.patches.ini` (met `Enabled = false`); een game zet hem aan met alleen de sectienaam en `Enabled = true`
in zijn eigen `patches.ini`. De gebruiker zet een patch uit in zijn eigen `patches.ini` met alleen `Enabled = false` onder dezelfde
sectienaam, of voegt een eigen patch toe. Samenvoegen is per sectie en sleutel, in de volgorde: `_defaults/<Type>.patches.ini`,
meegeleverd `patches.ini`, gebruikers-`patches.ini`. Zonder `Enabled` is een patch uit.

```ini
[Patch: OpenParrotLoader zonder CONTEXT_FLOATING_POINT]
Enabled = false               ; in _defaults/OpenParrot.patches.ini; de vier Type X2-games zetten hem aan
Kind = file                   ; file: op een lokale kopie van een bestand
Category = loader             ; verplicht, zie "Categorie en beleid"
Version = 1.08                ; optioneel: de gameversie waarvoor de patch bedoeld is; staat in de melding als de bytes niet kloppen
Target = loader               ; loader | core | een pad relatief aan de runtime dat een van die twee is (nooit een bestand op de NAS)
TargetName = OpenParrotLoader.exe   ; optioneel: de patch geldt alleen als het doel zo heet (anders overgeslagen)
Find = C7 05 C0 9B 42 00 1F 00 01 00
Replace = C7 05 C0 9B 42 00 17 00 01 00
Count = 1                     ; Find moet precies zo vaak voorkomen (standaard 1), anders weigert de patch
Note = alleen voor de lader van de runtime; onze eigen build heeft de fix in de bron (zie hieronder)
Reason = Wine zet MXCSR op 0 bij SetThreadContext met CONTEXT_FLOATING_POINT ...
```

Overige sleutels voor documentatie: `Source` (herkomst, bv. `OpenParrot TypeX2Generic.cpp (GPL-3.0), regel 2140`), `AppliedBy = core`
(de OpenParrot-kern past dit zelf al toe; RavenGun mag het niet nog eens doen, de patch is alleen documentatie) en `Note` (een losse
opmerking). Met `Note` staat er bijvoorbeeld bij de gedeelde lader-patch dat hij **alleen voor de lader van de runtime** geldt: onze eigen
lader (`[Loader] Build = ravengun`, `scripts/build-openparrot.sh`) heeft die fix in de bron, dus daar past de bytepatch niet op en zet de
launcher hem uit (zie "Het profiel van een game"). Een `Find` die niet precies `Count` keer voorkomt is voor de motor trouwens altijd een
weigering, ook zonder `Note`: de patch is dan voor een andere versie van het bestand.

### Categorie en beleid

Elke patch heeft **precies één** `Category`, uit een vaste lijst: `graphics`, `input`, `timing`, `audio`, `paths`, `crash`, `network`, `loader`, `dongle`,
`region`, `keychip`, `integrity`, `activation`.
Beide lezers (Python en C++) weigeren een patch zonder `Category` of met een onbekende, met een duidelijke melding, in elke laag: de controle gaat over de
samengevoegde patch, dus ook een eigen patch of een gewijzigde `Category` in de gebruikerslaag wordt afgewezen (een `patches.ini` met alleen
`Enabled = ...` bij een gedeelde patch is wel goed, want de definitie levert de rest). Ook een patch zonder `Kind` (een `Enabled` zonder definitie) wordt afgewezen.
Afgewezen patches staan niet in de lijst `patches` maar in `patch_errors` (en als waarschuwing); staat een afgewezen patch aan, dan **weigert de directe start**
(exitcode 5) in plaats van de game stilzwijgend zonder die patch te starten. Properties > Patches toont de afgewezen patches met hun melding.

**Beleid.** De laatste vijf categorieën — `dongle`, `region`, `keychip`, `integrity` en `activation` — horen bij de afhandeling per game in de map van de game
zelf: de dongle-drop-in (`patches/<Game>/dongle.json` en `dropin/`), de regiokeuze van het profiel, en de keychip-, integriteits- en activeringscontroles die
een game zelf doet. Het zijn gewone patches zoals elke andere (dezelfde validatie, dezelfde logregel, dezelfde laagvolgorde) en ze staan per game in
`patches/<Game>/patches.ini`, nooit in een gedeelde lijst. Zie `docs/dropins.md` voor het mapcontract en `docs/community-model.md` voor wat de community
zelf kan toevoegen en wat de engine nog mist. (De regiokeuze hierboven is iets anders: een bestaande profieloptie kiezen, geen patch.)

### Logregel

De directe start logt per patch één regel in vaste vorm, ook voor uitgeschakelde: `<toegepast|overgeslagen> '<naam>' @<plek> [reden]`. Plek: bij een
bestandspatch `<doel>:0x<offset van de eerste Find>` (`loader:0xC`), bij een geheugenpatch `<Module>+<Offset>`. Reden: `uit` voor een uitgeschakelde patch, verder
bv. `niet van toepassing: ...` (TargetName), `soort memory wordt niet door RavenGun toegepast (AppliedBy = core: ...)` of `afgewezen: ...`; bij `toegepast` staat er geen reden.

Soorten (`Kind`):

- `file`: zoekt `Find` in een kopie van het doel en vervangt het door `Replace` (even lang). Meerdere patches op hetzelfde doel gaan
  op volgorde van naam op dezelfde kopie. De kopie staat in
  `~/.local/share/ravengun/patched/<eerste 16 hex van de sha256 van het origineel>-<eerste 16 hex van de sha256 van de patchset>/<bestandsnaam>`.
  De patchset is de lijst van naam, Find, Replace en Count van de ingeschakelde patches op dat doel. Het origineel wordt nooit
  gewijzigd; een doel (of de kopie) op de NAS wordt geweigerd. Komt `Find` niet precies `Count` keer voor (andere versie van het
  bestand), dan **weigert** de launcher: de game start niet, de melding noemt de patch (net als Dolphin's voorwaardelijke patches:
  alleen op de verwachte bytes), exitcode 5, en `play-halo.sh` valt dan niet terug op TeknoParrotUi (die zou de game zonder patch
  starten). Met `[Game] ExeSha256` gezet weigert de launcher ook als de exe van de game een andere versie is. `--plan` en `--check`
  controleren alles maar maken de kopie niet; dat gebeurt bij `--run`.
- `registry`: regels voor de (eigen) prefix, `Key`, `Name`, `Value`. Wordt gelezen en getoond, maar nog niet toegepast (de launcher meldt dat
  hij de patch overslaat).
- `memory`: gereserveerd voor een eigen hook-DLL (later). Formaat zoals Dolphin: `Module` (de exe van de game), `Offset` (ten opzichte van de imagebase), `Find`, `Replace`.
  Wordt niet toegepast. Nu staan er alleen documentatiepatches van dit soort: veertien Type X2-aanpassingen die de OpenParrot-kern zelf al doet (`AppliedBy = core`,
  `Enabled = false`, `Action = jmp-returns-true` of `nop` met `Size`), overgenomen uit de openbare bron `OpenParrot/src/Functions/Games/TypeX2/TypeX2Generic.cpp`
  (GPL-3.0) met het regelnummer in `Source`: Haunted Museum (5), Haunted Museum II (6: versie `2100` en `2101J`, die het profiel niet onderscheidt, dus beide staan er),
  Gaia Attack 4 (2) en Music GunGun 2 (1, zonder commentaar upstream: het doel is niet vastgelegd). De bron noemt geen originele bytes; bij tien van de veertien staat de `Find` er wel, en die is op onze eigen dump gemeten (dezelfde bytes die de motor zelf print). Bij de vier zonder `Find` zou hij niets zeggen: de 2101J-set hoort bij de JP 1.01-build (onze dump is de USA 1.00-build) en de crash-NOP van 0x1391 staat in onze dump al op `0x90`.

Patches worden alleen door de directe start (`launch-direct.py`) toegepast; TeknoParrotUi start de lader zelf.

Grenzen (projectafspraak, ook voor meegeleverde patches van anderen): geen patches op dongle-, keychip-, regio-,
integriteits- of abonnementscontroles, en niets op uitgepakte beschermde code. De launcher schrijft nooit naar de NAS.

## Wie leest wat

| Onderdeel | Leest |
|---|---|
| `scripts/launch-direct.py` | `[Game]`, `[Launch]`, `[Loader]`, `[Wine]`, `[Display]`, `[Input] Jvs*`, `[Dongle]`, `[Env]`, `[Identify]`, `[Config*]`, `patches.ini` (via `gamesettings.py` en `ownprofile.py`); zonder eigen profiel de XML |
| `scripts/play-halo.sh` | `[Input]` (route, indeling, bord, poort), `[Launch] Mode`, de omgeving (`gamesettings.py env`) voor de eenheid, de procesnamen (`gamesettings.py comms`) en, voor een start via TeknoParrotUi, `gamesettings.py write-xml` |
| engine (`iobridge/routes.cpp`, `games/games.cpp`) | `[Input]`, bij elke start opnieuw (een keuze in de app geldt zonder herstart); `[Status] Note` voor de web-UI |
| engine (`launch/unity.cpp`, `games/aagames.cpp`, `games/games.cpp`) | `unity.json` (de Unity-route; gebruikerslaag eerst) en `packaging/unity-route.json`; de gamemappen uit `library.json` |
| engine (`games/games.cpp`) | `[Game]` (eigen profiel: een game met `Executable` of `ProfileRevision` is een geldig profiel zonder XML en staat in de lijst als hij in een gamemap of view staat), `[Identify]` |
| app (`GameData`, `GameLibrary`, `PropertiesDialog`, `ProfileSync`) | `options.json` (opties en memory card, docs/game-opties.md), `unity.json` (de bibliotheek herkent Unity-games aan `folder` in de gamemappen), `[Input]`, `[Dongle]` (Options > Dongle; status via `scripts/dongle.py --json`), `[Status]`, `[Game]`, `[Identify]`, `[Config*]`; de bibliotheek vindt games met een eigen profiel in de gamemappen; tab **Patches** (aan/uit per patch schrijft de gebruikerslaag, de reden als tooltip, de effectieve instellingen met hun laag, "Open user folder" en "Open shipped folder"); de routekeuze en alle opties (Options, Display) schrijven alleen verschillen naar de gebruikerslaag |
| `scripts/tp-add-game.py` | schrijft het profiel (uit het sjabloon) en de keuzes in het gebruikers-`game.ini` (en `patches.ini`), plus `UserProfiles/<P>.xml` als uitvoerformaat; leest `[Bindings]` en `[NewGame]` |
| `scripts/import-tp-profiles.py` | maakt de profieldelen van `game.ini` uit de runtime (zie "Importer") |
| `tools/dev/gen-compatibility.py` | `[Game] Name`, `[Status] Status` en `NoteEN` (alleen de meegeleverde laag) |
| `tools/dev/migrate-game-settings.py` | maakt de meegeleverde bestanden uit de oude bronnen (zie "Overgang") |

Gedeelde bibliotheken blijven centraal, zoals de controllerprofielen bij Dolphin: tp-shm-indelingen
(`tp-shm-profiles.json`), JVS- en HID-borden (`jvs-boards.json`, `hid-boards.json`). Een game verwijst ernaar met een naam.

De opties in de app (Properties > Options en Display) schrijven voor een game met een eigen profiel naar de gebruikerslaag van `game.ini`, en alleen
als ze van de meegeleverde waarde afwijken: een profielveld naar `[Config]`, een profielveld van het hoogste niveau (`WineRunnerPath`) en een
omgevingsvariabele (`TP_GAMESCOPE`, `TP_POSTSTART_REMOTETHREAD_MS`) naar hun sleutel uit `ENV_MAP` (`[Display] Gamescope`, `[Loader] PostStartMs`),
overige variabelen naar `[Env]`. Een waarde die terug gelijk is aan de meegeleverde haalt de sleutel uit de gebruikerslaag weg; een optie die een
meegeleverde waarde moet opheffen schrijft een lege waarde (`Gamescope =`). `UserProfiles/<P>.xml` en `<P>.env` worden voor zo'n game niet meer
geschreven. Een game zonder eigen profiel gedraagt zich als voorheen.

## Overgang

1. `tools/dev/migrate-game-settings.py` maakt `patches/<Game>/game.ini` (en `patches.ini`, `README.md`) uit `io-routes.json`,
   `games-notes.json` en de `.env`-bestanden. Het script is opnieuw te draaien en idempotent: het leidt per bron alleen de sleutels af die
   die bron kent, haalt die eerst weg en zet ze opnieuw; handwerk in andere sleutels en secties blijft staan. Waarden gelijk aan
   de standaard van het soort worden niet herhaald; ontbreekt een variabele die de standaard wel zet, dan komt de oude uitkomst
   expliciet in het bestand (`RemoteThread =`), zodat de game niet verandert. Geheimen en paden naar de NAS komen er nooit in.
   De oude JSON-bestanden staan niet meer in de repo: geef ze op met `--io-routes` en `--notes`, of laat ze uit een revisie lezen met
   `--rev <tak of commit>` (bv. een tak die ze nog wijzigt).
2. Lezers overgezet. `UserProfiles/<P>.env` blijft één versie lang gelezen, als laagste laag onder `game.ini` (met de melding
   "legacy .env" op stderr); `RAVENGUN_NO_LEGACY_ENV=1` slaat het over. Daarna wordt het niet meer gebruikt.
3. `io-routes.json` en `games-notes.json` zijn uit `packaging/` verdwenen; er is één bron per game. De routekeuze van de gebruiker uit
   `~/.config/ravengun/io-routes.json` wordt niet meer gelezen: kies opnieuw in Properties of zet `[Input] Route` in je eigen `game.ini`.
4. Eigen profielen (zie "Het profiel van een game"): `scripts/import-tp-profiles.py --all` heeft de profieldelen van alle games in `patches/` gemaakt uit de
   runtime. Wat daarna van `UserProfiles/` overblijft is een uitvoerformaat voor TeknoParrotUi; de bibliotheek, de engine en de directe start hebben het
   niet meer nodig. De gamemappen die de games bevatten moeten in `library.json` staan (Settings > Paths > Game Folders, of
   `import-tp-profiles.py --game-folder DIR --add-to-library`).

## Nog open

- **De C++-lezers** (`src/ravengun/gamesettings/`, de engine en de instellingenvenster-code) kennen de zoekvolgorde en de identiteit nog
  op de oude manier: een meegeleverde map (`RAVENGUN_PATCHES_DIR` *of* `<datamap>/patches`) en een gebruikersmap, een game op mapnaam.
  Tot ze dezelfde `patch_layers`-volgorde en `find_game_dir`-regels volgen, wijkt de C++-kant af zodra `RAVENGUN_PATCHES_DIR` gezet is of
  een map een andere naam heeft dan zijn `[Game] Profile`. Dit is bewust niet in dezelfde wijziging aangepast en niet gebouwd of getest.
- **`hasp-haken.ini`** staat in `patches/Halo/` en `patches/Terminator/` op het hoogste niveau, maar de hook (`tools/rt-hasphook`) leest hem uit de
  map van zijn eigen bibliotheek (of via `RAVENGUN_HASP_ELF_ADRESLIJST`), dus uit `dropin/`. `tests/test_patches_layout.py` meldt hem als
  onbekende naam. Verplaatsen naar `dropin/` (met een sectie in `dropin.ini`) of als onderzoek aanmerken is een keuze van de eigenaar.
