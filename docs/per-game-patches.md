# Per-game instellingen en patches (`patches/<Game>/`)

Ontwerp, 2026-10-11. Model: Dolphin. Dolphin bewaart alles wat per game anders is in één tekstbestand per game-ID
(`Sys/GameSettings/<ID>.ini`, meegeleverd), met een gebruikerskopie die per sleutel wint (`User/GameSettings/<ID>.ini`).
Patches zijn benoemde blokken (`$Naam`) die de gebruiker in Properties > Patches aan- en uitzet en zelf kan toevoegen.
De emulator zelf bevat geen kennis van één bepaalde game. RavenGun doet hetzelfde.

## Waarom

Tot nu toe stond de kennis per game verspreid: `packaging/io-routes.json` (route, indeling, startwijze),
`packaging/games-notes.json` (status, notities), `UserProfiles/<P>.env` in de TeknoParrot-runtime (lader, Wine,
omgeving), `packaging/jvs-boards.json` (borden), en code in `scripts/launch-direct.py` en `scripts/tp-add-game.py`
(bijvoorbeeld de lader-patch voor Type X2 en de vaste knoppen van Let's Go Safari). Een game toevoegen of een
oplossing aanpassen betekende code wijzigen. Gebruikers konden niets toevoegen zonder in de repo te werken.

## Indeling

```
<datadir>/patches/                 meegeleverd (repo: patches/; geïnstalleerd: share/ravengun/patches/), alleen-lezen
  _defaults/<EmulatorType>.ini     standaard per soort (TeknoParrot, OpenParrot, ElfLdr2, Unity, Native)
  <Game>/
    game.ini                       instellingen (Dolphin: GameSettings)
    patches.ini                    benoemde patches (Dolphin: [OnFrame]/[Patches]); optioneel
    README.md                      korte uitleg en verwijzing naar findings/<Game>.md; optioneel
    <extra bestanden>              bv. een .reg voor de eigen prefix; optioneel

~/.local/share/ravengun/patches/   gebruiker, zelfde indeling; wint per sleutel; eigen games en eigen patches
  <Game>/game.ini
  <Game>/patches.ini
```

`<Game>` is de TeknoParrot-profielnaam (`LGI`, `NERF`, `LuigisMansion`), voor eigen routes de id uit
`native-games.json`/`aagames-games.json`. Samenvoegen: `_defaults/<type>.ini`, dan het meegeleverde `game.ini`,
dan het gebruikers-`game.ini`. Lege waarde in de gebruikerslaag zet een sleutel terug op "niet gezet".

Formaat: INI. Reden: Dolphin gebruikt het, mensen kennen het, Python leest het met `configparser` en de app en engine
met `QSettings(IniFormat)`, zonder extra bibliotheek. Commentaar met `#` of `;`.

## `game.ini`

```ini
[Game]
Name = Let's Go Island
Profile = LGI                 ; TeknoParrot-profiel
Type = TeknoParrot            ; kiest _defaults/<Type>.ini
ExeSha256 =                   ; optioneel: patches gelden alleen voor deze exe-versie

[Launch]
Mode = direct                 ; direct | tpui
WorkingDirectory =            ; relatief aan de gamemap of Windows-pad
View =                        ; symlink-view (bv. Data -> DataGlobal/EN/Data), zie launch-direct.py

[Loader]
RemoteThread = off            ; off = standaardroute van de lader (geparkeerd startpunt)
PostStartMs =
EntryPointMs =

[Wine]
Prefix = own                  ; shared | own (eigen kopie van de gedeelde prefix)
WindowsVersion =              ; winxp | vista | win7 ... (alleen voor de exe van de game, via AppDefaults)
DllOverrides = d3d9=b
VirtualDesktop = 1920x1080
NativeDlls =                  ; bv. d3dx9_40 (bron: Microsoft-redistributable van de gebruiker)
GStreamerPlugins =            ; bv. asf,libav

[Input]
Route = jvs                   ; tp-shm | jvs | evdev | hid | plugin
Layout =                      ; tp-shm-indeling uit tp-shm-profiles.json
JvsBoard = LGI                ; bord uit jvs-boards.json
JvsTransport = pipe

[Env]
; vrije omgevingsvariabelen, als laatste toegepast (ontsnappingsroute; liever een echte sleutel hierboven)

[Status]
Status = playable             ; playable | partial | parked | untested
Note = ...                    ; Nederlands
NoteEN = ...
Findings = findings/LetsGoIsland.md
```

## `patches.ini`

Elke patch is een eigen sectie met een naam, zoals `$Naam` bij Dolphin. De gebruiker zet een patch uit in zijn eigen
`patches.ini` met alleen `Enabled = false` onder dezelfde sectienaam.

```ini
[Patch: OpenParrotLoader zonder CONTEXT_FLOATING_POINT]
Enabled = true
Kind = file                   ; file: op een lokale kopie van een bestand
Target = loader               ; loader | core | <pad relatief aan de runtime>  (nooit een bestand op de NAS)
Find = C7 05 C0 9B 42 00 1F 00 01 00
Replace = C7 05 C0 9B 42 00 17 00 01 00
Count = 1                     ; Find moet precies zo vaak voorkomen, anders weigert de patch
Reason = Wine zet MXCSR op 0 bij SetThreadContext met CONTEXT_FLOATING_POINT; zie findings/GaiaAttack4.md
```

Soorten (`Kind`):

- `file`: zoekt `Find` in een kopie van het doel en vervangt het. De kopie staat in
  `~/.local/share/ravengun/patched/<sha256 van het origineel>-<sha256 van de patchset>/`. Het origineel wordt nooit
  gewijzigd. Komt `Find` niet precies `Count` keer voor, dan start de game niet en zegt de launcher welke patch faalde
  (net als Dolphin's voorwaardelijke patches: alleen op de verwachte bytes).
- `registry`: regels voor de (eigen) prefix, `Key`, `Name`, `Value`.
- `memory`: gereserveerd voor een eigen hook-DLL (later). Formaat zoals Dolphin: `Module`, `Offset`, `Find`, `Replace`.

Grenzen (projectafspraak, ook voor meegeleverde patches van anderen): geen patches op dongle-, keychip-, regio-,
integriteits- of abonnementscontroles, en niets op uitgepakte beschermde code. De launcher schrijft nooit naar de NAS.

## Wie leest wat

| Onderdeel | Leest |
|---|---|
| `scripts/launch-direct.py`, `play-halo.sh` | `[Launch]`, `[Loader]`, `[Wine]`, `[Env]`, `patches.ini` |
| engine (`iobridge/routes.cpp`) | `[Input]` |
| app (`GameData`, `PropertiesDialog`) | alles; tab **Patches** met aan/uit per patch (schrijft de gebruikerslaag) en "Open map" |
| `scripts/tp-add-game.py` | schrijft een nieuw gebruikers-`game.ini` in plaats van `.env` |
| `scripts/gen-compatibility.py` | `[Game]`, `[Status]` |

Gedeelde bibliotheken blijven centraal, zoals de controllerprofielen bij Dolphin: tp-shm-indelingen
(`tp-shm-profiles.json`), JVS- en HID-borden. Een game verwijst ernaar met een naam.

## Overgang

1. `scripts/migrate-game-settings.py` maakt `patches/<Game>/game.ini` (en `patches.ini`) uit `io-routes.json`,
   `games-notes.json` en de `.env`-bestanden. Het script is opnieuw te draaien.
2. Lezers overzetten op de nieuwe bestanden. `UserProfiles/<P>.env` blijft één versie lang gelezen (met een melding
   in het log) en wordt daarna niet meer gebruikt.
3. `io-routes.json` en `games-notes.json` verdwijnen uit `packaging/`; er is één bron per game.
