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
    README.md                      korte uitleg en verwijzing naar findings/<Game>.md; alleen als er zo'n bestand is
    <extra bestanden>              bv. een .reg voor de eigen prefix; optioneel

~/.local/share/ravengun/patches/   gebruiker ($XDG_DATA_HOME/ravengun/patches), zelfde indeling; wint per sleutel
  <Game>/game.ini
  <Game>/patches.ini
```

`<Game>` is de TeknoParrot-profielnaam (`LGI`, `NERF`, `LuigisMansion`), voor eigen routes de id uit
`native-games.json`/`aagames-games.json`. Een eigen game maak je door in de gebruikersmap een map met een `game.ini` te zetten.

Zoeken van de mappen (zelfde regels in Python, engine en app):

- meegeleverd: `$RAVENGUN_PATCHES_DIR`, anders `<datamap>/patches`. De datamap is de repo bij ontwikkeling en
  `<prefix>/share/ravengun` bij een installatie (dezelfde zoektocht als voor `scripts/` en `packaging/`, `src/ravengun/paths.hpp`).
  Voor scripts is dat de map boven `scripts/`.
- gebruiker: `$RAVENGUN_USER_PATCHES_DIR`, anders `$XDG_DATA_HOME/ravengun/patches`, anders `~/.local/share/ravengun/patches`.

### Samenvoegen

Per sleutel, in deze volgorde (een latere laag wint):

1. `_defaults/<Type>.ini`. Het soort staat in `[Game] Type` (de gebruikerslaag gaat voor); zonder `Type` geldt de hint van de
   aanroeper (de launcher geeft de `EmulatorType` van het TeknoParrot-profiel mee).
2. `patches/<Game>/game.ini` (meegeleverd).
3. `<gebruiker>/<Game>/game.ini`.

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
View =                        ; informatie: symlink-view (bv. Data -> DataGlobal/EN/Data); de launcher leest dit niet

[Loader]
RemoteThread = off            ; off = standaardroute van de lader; 1 = remote thread; leeg = niet gezet (de launcher kiest 1)
PostStartMs =                 ; TP_POSTSTART_REMOTETHREAD_MS
EntryPointMs =                ; TP_ENTRYPOINT_REMOTETHREAD_MS
Build =                       ; leeg/teknoparrot = de lader uit de TeknoParrot-runtime; ravengun = de eigen OpenParrot-lader en (bij Type = OpenParrot) -kern uit scripts/build-openparrot.sh
Core =                        ; alleen de kern apart (teknoparrot | ravengun); leeg volgt Build (bij Type = OpenParrot), een eigen kern bestaat alleen voor Type = OpenParrot
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

[Display]
Gamescope = true              ; TP_GAMESCOPE=1: de game draait schermvullend in gamescope
GamescopePath =               ; TP_GAMESCOPE_PATH
GamescopeBackend =            ; TP_GAMESCOPE_BACKEND
GamescopeResolution =         ; RG_GAMESCOPE_RES
DisplayMode =                 ; informatie: de optie DisplayMode van het profiel (bv. Fullscreen Windowed); de launcher leest dit niet
CustomResolution =            ; informatie: 1920x1080 als het profiel een eigen resolutie aan heeft; de launcher leest dit niet

[Input]
Route = jvs                   ; evdev | tp-shm | unity-tcp | hid-board | jvs
Layout =                      ; tp-shm: indeling uit tp-shm-profiles.json
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
Note = ...                    ; Nederlands (webpagina en engine, hoogstens 300 tekens)
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
2. een lokale view `~/.local/share/ravengun/runtime/views/<Game>/` (door `tp-add-game.py --view` gemaakt) waarin `Path` bestaat;
3. de gamemappen: elke submap van een gamemap (en de gamemap zelf) waarin `Path` bestaat en waarvan de grootte (`Size`) en eventueel de
   sha256 (`Sha256`) kloppen.

`Path` is relatief aan de map van de game. `Size` is de grootte van dat bestand. `Path2` (alleen bij games met twee exe's, HOTDSD) is het pad van de tweede exe
in dezelfde map; het wordt alleen gebruikt om `GamePath2` in de XML voor TeknoParrotUi te zetten. Hebben twee games een identiek hoofdbestand (twee Unity-games met
dezelfde `Game.exe`), dan legt de importer de `Sha256` vast en zo nodig `Folder` (de naam van de map, hoofdletters maken niet uit). Een andere
versie van het bestand (andere grootte) wordt niet herkend; kies dan het pad zelf (`GamePath`) of importeer opnieuw. Python
(`ownprofile.locate_game`), de app (`gamesettings::locate_game`) en de engine gebruiken dezelfde regels; de engine heeft geen hashfunctie en let
dan op pad, grootte en mapnaam.

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

## Dongle per game

Sectie `[Dongle]` in `game.ini`: waar de dongle van een game vandaan komt. Er zijn twee bronnen. **Welke soort** dongle een game nodig heeft staat
niet hier maar in de map van de game: `patches/<Game>/dongle.json` (veld `kind`, plus de gegevens) en `patches/<Game>/dropin/` (de bestanden die de
launcher in de view van de game zet). Het bestaan van `dongle.json` betekent "deze game vraagt een dongle". Het contract daarvoor staat in
`docs/dropins.md`; deze sectie herhaalt het niet.

| Sleutel | Waarden | Standaard | Betekenis |
|---|---|---|---|
| `Enabled` | `true` \| `false` (ook `1/0`, `yes/no`, `on/off`) | `false` (weggelaten) | de eigenaar zet de dongle aan; de launcher controleert dan vóór de start en weigert als de gekozen bron niet bruikbaar is |
| `Source` | `usb-passthrough` \| `drop-in` | `usb-passthrough` | de echte, fysieke dongle van de eigenaar (USB pass-through), of de drop-in uit de map van de game zelf |

```ini
; ~/.local/share/ravengun/patches/LGI/game.ini (gebruikerslaag)
[Dongle]
Enabled = true
Source = drop-in
```

De **meegeleverde** laag laat `Enabled` weg (dus uit) en laat `Source` weg (dus `usb-passthrough`): er verandert niets aan de start van een game tot de
eigenaar er bewust voor kiest. Daarom heeft `patches/<Game>/game.ini` ook geen `[Dongle]`-sectie: de donglekennis van een game staat in `dongle.json`
en `dropin/`, op één plek.

Lezers: `Game.dongle()` in `scripts/gamesettings.py` en `gs::dongle_of()` in `src/ravengun/gamesettings` (dezelfde regels; beide getest tegen
`tests/data/dongle-cases.json`). Het resultaat is (enabled, source, fout). Een `Enabled` die geen true of false is telt als aan met de fout
`[Dongle] Enabled = <waarde> (use true or false)` (wie iets typte wilde er iets mee; de launcher weigert dan). Een onbekende `Source` is een fout
(`[Dongle] Source = <waarde> (known: usb-passthrough, drop-in)`). Bij `Enabled = false` doet de launcher niets, ook niet bij een foute `Source`.

**Open punt (ontwerp).** `Enabled = true` betekent in deze lezer "controleer vóór de start en weiger als er geen bruikbare dongle is". Voor
`usb-passthrough` klopt dat: het is de controle van iets dat de eigenaar zelf in de machine heeft. Voor `drop-in` zou `Enabled` juist moeten betekenen
"schakel de drop-in van deze game in" — dat is iets anders dan weigeren. Zolang die twee betekenissen door elkaar lopen, laat de meegeleverde laag
`Enabled` weg en zet de eigenaar hem pas aan als de drop-in-laag klaar en getest is.

**Detectie** (`scripts/dongle.py`, gegevensbestand `packaging/dongle-vendors.json`; de app roept `python3 -I scripts/dongle.py --json` aan). Dit
hoort bij `Source = usb-passthrough`; de drop-in wordt niet gedetecteerd maar uit de map van de game gelezen:

- Aangesloten USB-apparaten via sysfs (`/sys/bus/usb/devices/*/`): alleen `idVendor`, `idProduct`, `manufacturer` en `product`. Een dongle wordt herkend
  aan het vendor-ID in het gegevensbestand (openbaar bekende ID's): `0529` Aladdin (HASP), `04b9` Rainbow/SafeNet (Sentinel), `064f` Wibu-Systems (CodeMeter).
  `manufacturer` en `product` worden alleen gelezen bij een apparaat dat al als dongle is herkend.
- **Het attribuut `serial` of een andere unieke identificatie wordt nooit gelezen**, en er gaat geen serienummer naar een log, bestand of de UI. Er is
  een vaste lijst gelezen attributen (`ALLOWED_ATTRS`); `tests/test_dongle.py` zet een serienummer in een nep-sysfs en controleert dat dat bestand
  niet wordt geopend en nergens in de uitvoer staat.
- Officiële runtime van de fabrikant: voor HASP en Sentinel de "Sentinel LDK Runtime" van Thales (daemons `aksusbd` en `hasplmd`; de License Manager
  luistert op `127.0.0.1:1947`), voor Wibu de CodeMeter-runtime (proces `CodeMeterLin`, poort `22350`). Alleen kijken of die er is en draait: een
  proces met die naam (alleen `/proc/<pid>/comm`) of een open poort op 127.0.0.1. Status `running`, `not running` (bestanden aanwezig, niet actief),
  `not installed`. RavenGun installeert, levert en downloadt niets; bij `not installed` verwijst de melding naar de officiële installatie van de
  fabrikant (`install_url` in het gegevensbestand). Een fabrikant zonder `runtime` in het gegevensbestand: "dongle gevonden, runtime onbekend" (`unknown`),
  de start gaat door.
- Uitvoer: `Dongle detected: <fabrikant> <product> (vvvv:pppp) - runtime: running|not running|not installed|unknown` of `No dongle found`; met `--json` ook de velden per
  dongle en `ok`/`message`. Voor tests: `RAVENGUN_USB_SYSFS`, `RAVENGUN_PROC_ROOT`, `RAVENGUN_DONGLE_ROOT`, `RAVENGUN_DONGLE_VENDORS`.

**Launcher.** Bij `[Dongle] Enabled = true` controleert `launch-direct.py` (bij `--check` en `--run`, niet bij `--plan`) vóór de start de gekozen bron: bij
`usb-passthrough` de detectie hierboven, bij `drop-in` de drop-in van de game (`patches/<Game>/dongle.json` en `dropin/`, `docs/dropins.md`). Bij
`usb-passthrough` logt hij bij `--run` `dongle (usb-passthrough): Dongle detected: ...` (nooit een serienummer) en weigert met **exitcode 6** en een melding
op stderr (`Dongle: ...`) als er geen dongle is, de runtime niet draait, of `Enabled`/`Source` ongeldig is (zie boven). Geen terugval op TeknoParrotUi. `play-halo.sh` toont de melding (en een
bureaubladmelding) en sluit met dezelfde exitcode 6; bij een start zonder directe route (TeknoParrotUi, native) draait `play-halo.sh` dezelfde detectie
(`gamesettings.py dongle <Game>` en `dongle.py`). Exitcodes van `launch-direct.py`: 0 klaar, 1 fout, 2 gebruik, 3 activering, 4 niet ondersteund, 5 patch of
instelling geweigerd, **6 dongle ontbreekt of runtime niet actief**, anders de exitcode van de lader.

**Permissies.** `packaging/udev/72-ravengun-dongle.rules` geeft `TAG+="uaccess"` op USB-apparaten (`SUBSYSTEM=="usb"`, `DEVTYPE==usb_device`) met precies
die vendor-ID's, zodat een runtime of Wine er als gewone gebruiker bij kan. Installeren gaat via `scripts/install-permissions.sh --apply` (pkexec; ook de
wizard), dat `scripts/udev-ravengun.sh` aanroept; zonder die stap blijft `install-permissions.sh --check` "Niet geïnstalleerd: 72-ravengun-dongle.rules" melden.

**App.** Properties > Options > groep Dongle: vinkje "Use dongle", keuzelijst "Dongle source" ("USB pass-through (physical dongle)" en "Drop-in from
patches/<Game> (dongle.json)"), een statusregel en "Refresh". De keuzelijst, de status en Refresh zijn uitgeschakeld zolang het vinkje uit staat. De
wijziging gaat direct naar de gebruikerslaag en alleen als ze van de standaard afwijkt (uit, usb-passthrough). De statusregel hoort bij
`usb-passthrough`; de drop-in zelf hoort de launcher te controleren (`docs/dropins.md`).

**Wat het project nu wel doet.** De afspraak die hier eerder stond — "geen dongle-emulatie of -nabootsing in welke vorm dan ook", met "geen
dongle-images of `.dgl`/`dongle.json`-lezers" — is achterhaald. RavenGun heeft nu een **drop-in-laag**: `patches/<Game>/dongle.json` noemt de soort en de
gegevens van de dongle, `patches/<Game>/dropin/` bevat bestanden die de launcher in de view van de game zet (`dropin.ini` beschrijft waar ze heen gaan
en welke omgevingsvariabele erbij hoort), en de eigen emulator `hasp/` beantwoordt de tien `hasp_*`-aanroepen. Zie `docs/dropins.md` voor het contract
en `docs/decisions.md` voor de geschiedenis (de regel "Dongle-optie per game: alleen USB pass-through" beschrijft de eerste versie, vóór de
drop-in-laag). Wat blijft staan: geen software van een fabrikant meeleveren of downloaden, geen licentiecodes of serienummers, en geen wijzigingen aan de
games of aan de beveiliging zelf.

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
  Wordt niet toegepast. Nu staan er alleen documentatiepatches van dit soort: tien Type X2-aanpassingen die de OpenParrot-kern zelf al doet (`AppliedBy = core`,
  `Enabled = false`, `Action = jmp-returns-true` of `nop` met `Size`), overgenomen uit de openbare bron `OpenParrot/src/Functions/Games/TypeX2/TypeX2Generic.cpp`
  (GPL-3.0) met het regelnummer in `Source`: Haunted Museum (3), Haunted Museum II (4: versie `2100` en `2101J`, die het profiel niet onderscheidt, dus beide staan er),
  Gaia Attack 4 (2) en Music GunGun 2 (1, zonder commentaar upstream: het doel is niet vastgelegd). De bron noemt geen originele bytes, dus `Find` is bij deze patches leeg.

Patches worden alleen door de directe start (`launch-direct.py`) toegepast; TeknoParrotUi start de lader zelf.

Grenzen (projectafspraak, ook voor meegeleverde patches van anderen): geen patches op dongle-, keychip-, regio-,
integriteits- of abonnementscontroles, en niets op uitgepakte beschermde code. De launcher schrijft nooit naar de NAS.

## Wie leest wat

| Onderdeel | Leest |
|---|---|
| `scripts/launch-direct.py` | `[Game]`, `[Launch]`, `[Loader]`, `[Wine]`, `[Display]`, `[Input] Jvs*`, `[Dongle]`, `[Env]`, `[Identify]`, `[Config*]`, `patches.ini` (via `gamesettings.py` en `ownprofile.py`); zonder eigen profiel de XML |
| `scripts/play-halo.sh` | `[Input]` (route, indeling, bord, poort), `[Launch] Mode`, de omgeving (`gamesettings.py env`) voor de eenheid, de procesnamen (`gamesettings.py comms`) en, voor een start via TeknoParrotUi, `gamesettings.py write-xml` |
| engine (`iobridge/routes.cpp`, `games/games.cpp`) | `[Input]`, bij elke start opnieuw (een keuze in de app geldt zonder herstart); `[Status] Note` voor de web-UI |
| engine (`games/games.cpp`) | `[Game]` (eigen profiel: een game met `Executable` of `ProfileRevision` is een geldig profiel zonder XML en staat in de lijst als hij in een gamemap of view staat), `[Identify]` |
| app (`GameData`, `GameLibrary`, `PropertiesDialog`, `ProfileSync`) | `[Input]`, `[Dongle]` (Options > Dongle; status via `scripts/dongle.py --json`), `[Status]`, `[Game]`, `[Identify]`, `[Config*]`; de bibliotheek vindt games met een eigen profiel in de gamemappen; tab **Patches** (aan/uit per patch schrijft de gebruikerslaag, de reden als tooltip, de effectieve instellingen met hun laag, "Open user folder" en "Open shipped folder"); de routekeuze en alle opties (Options, Display) schrijven alleen verschillen naar de gebruikerslaag |
| `scripts/tp-add-game.py` | schrijft het profiel (uit het sjabloon) en de keuzes in het gebruikers-`game.ini` (en `patches.ini`), plus `UserProfiles/<P>.xml` als uitvoerformaat; leest `[Bindings]` en `[NewGame]` |
| `scripts/import-tp-profiles.py` | maakt de profieldelen van `game.ini` uit de runtime (zie "Importer") |
| `scripts/gen-compatibility.py` | `[Game] Name`, `[Status] Status` en `NoteEN` (alleen de meegeleverde laag) |
| `scripts/migrate-game-settings.py` | maakt de meegeleverde bestanden uit de oude bronnen (zie "Overgang") |

Gedeelde bibliotheken blijven centraal, zoals de controllerprofielen bij Dolphin: tp-shm-indelingen
(`tp-shm-profiles.json`), JVS- en HID-borden (`jvs-boards.json`, `hid-boards.json`). Een game verwijst ernaar met een naam.

De opties in de app (Properties > Options en Display) schrijven voor een game met een eigen profiel naar de gebruikerslaag van `game.ini`, en alleen
als ze van de meegeleverde waarde afwijken: een profielveld naar `[Config]`, een profielveld van het hoogste niveau (`WineRunnerPath`) en een
omgevingsvariabele (`TP_GAMESCOPE`, `TP_POSTSTART_REMOTETHREAD_MS`) naar hun sleutel uit `ENV_MAP` (`[Display] Gamescope`, `[Loader] PostStartMs`),
overige variabelen naar `[Env]`. Een waarde die terug gelijk is aan de meegeleverde haalt de sleutel uit de gebruikerslaag weg; een optie die een
meegeleverde waarde moet opheffen schrijft een lege waarde (`Gamescope =`). `UserProfiles/<P>.xml` en `<P>.env` worden voor zo'n game niet meer
geschreven. Een game zonder eigen profiel gedraagt zich als voorheen.

## Overgang

1. `scripts/migrate-game-settings.py` maakt `patches/<Game>/game.ini` (en `patches.ini`, `README.md`) uit `io-routes.json`,
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
