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
optie, weigert de start met een melding (exitcode 5, geen terugval op TeknoParrotUi). Dit is alleen het kiezen van een bestaande optie: er is geen
dongle-emulatie of iets anders aan het dongle-beleid veranderd (zie "Beleid" hieronder).

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
Reason = Wine zet MXCSR op 0 bij SetThreadContext met CONTEXT_FLOATING_POINT ...
```

Overige sleutels voor documentatie: `Source` (herkomst, bv. `OpenParrot TypeX2Generic.cpp (GPL-3.0), regel 2140`) en `AppliedBy = core`
(de OpenParrot-kern past dit zelf al toe; RavenGun mag het niet nog eens doen, de patch is alleen documentatie).

### Categorie en beleid

Elke patch heeft **precies één** `Category`, uit een vaste lijst: `graphics`, `input`, `timing`, `audio`, `paths`, `crash`, `network`, `loader`.
Beide lezers (Python en C++) weigeren een patch zonder `Category` of met een onbekende, met een duidelijke melding, in elke laag: de controle gaat over de
samengevoegde patch, dus ook een eigen patch of een gewijzigde `Category` in de gebruikerslaag wordt afgewezen (een `patches.ini` met alleen
`Enabled = ...` bij een gedeelde patch is wel goed, want de definitie levert de rest). Ook een patch zonder `Kind` (een `Enabled` zonder definitie) wordt afgewezen.
Afgewezen patches staan niet in de lijst `patches` maar in `patch_errors` (en als waarschuwing); staat een afgewezen patch aan, dan **weigert de directe start**
(exitcode 5) in plaats van de game stilzwijgend zonder die patch te starten. Properties > Patches toont de afgewezen patches met hun melding.

**Beleid:** er is bewust géén categorie voor beveiliging of bescherming. Patches voor dongle-, keychip-, regio-, integriteits- of abonnementscontroles zijn geen
onderdeel van het formaat en worden niet meegeleverd of gemaakt. (De regiokeuze hierboven is iets anders: een bestaande profieloptie kiezen, geen patch.)

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
| `scripts/launch-direct.py` | `[Launch]`, `[Loader]`, `[Wine]`, `[Display]`, `[Input] Jvs*`, `[Env]`, `patches.ini` (via `gamesettings.py`) |
| `scripts/play-halo.sh` | `[Input]` (route, indeling, bord, poort), `[Launch] Mode`, en de omgeving (`gamesettings.py env`) voor de eenheid |
| engine (`iobridge/routes.cpp`, `games/games.cpp`) | `[Input]`, bij elke start opnieuw (een keuze in de app geldt zonder herstart); `[Status] Note` voor de web-UI |
| app (`GameData`, `PropertiesDialog`) | `[Input]`, `[Status]`, `[Game]`; tab **Patches** (aan/uit per patch schrijft de gebruikerslaag, de reden als tooltip, de effectieve instellingen met hun laag, "Open user folder" en "Open shipped folder"); de routekeuze schrijft `[Input] Route` in de gebruikerslaag |
| `scripts/tp-add-game.py` | schrijft een nieuw gebruikers-`game.ini` (en `patches.ini`) in plaats van een `.env`; leest `[Bindings]` en `[NewGame]` |
| `scripts/gen-compatibility.py` | `[Game] Name`, `[Status] Status` en `NoteEN` (alleen de meegeleverde laag) |
| `scripts/migrate-game-settings.py` | maakt de meegeleverde bestanden uit de oude bronnen (zie "Overgang") |

Gedeelde bibliotheken blijven centraal, zoals de controllerprofielen bij Dolphin: tp-shm-indelingen
(`tp-shm-profiles.json`), JVS- en HID-borden (`jvs-boards.json`, `hid-boards.json`). Een game verwijst ernaar met een naam.

De opties in de app (Properties > Options, Display) schrijven nog naar `UserProfiles/<P>.env` (en naar het profiel); alleen de gamescope-keuze
wordt daarnaast in de gebruikerslaag gezet. Omdat `game.ini` voor het oude `.env` gaat, werkt een optie die een meegeleverde
waarde moet opheffen pas als ook die optie naar de gebruikerslaag schrijft (open punt, zie `docs/todo.md`).

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
