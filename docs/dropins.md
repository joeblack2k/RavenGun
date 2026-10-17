# Drop-ins per game — de map `patches/<Game>/` is de enige plek

*Ontwerpafspraak, ronde 6 (2026-10-11). Dit is het contract voor iedereen die een game toevoegt of
bijwerkt.*

## De regel

**Alles wat één game nodig heeft, staat in `patches/<Game>/`. Een game toevoegen of bijwerken is
één map aanraken — geen gedeeld bestand, geen globale registratie, geen tweede boom.**

Dat is dezelfde lijn als Dolphin's `GameSettings/<ID>.ini`: de meegeleverde laag is per game
compleet, en de gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/`) bevat alleen afwijkingen.

## De indeling

```
patches/<Game>/
  game.ini        instellingen van de game (bestaand)
  patches.ini     bytepatches, in het bestaande Find/Replace-formaat (bestaand)
  README.md       uitleg: wat deze game nodig heeft en waarom (bestaand)
  dongle.json     WELKE dongle deze game nodig heeft + de gegevens    <- nieuw
  dongle/         de bestanden die dongle.json noemt                  <- nieuw
  dropin/         bestanden die letterlijk in de view van de game komen <- nieuw
    dropin.ini    per bestand: waar het heen gaat en welke omgevingsvariabele erbij hoort
```

`_defaults/<Type>.ini` en `_defaults/<Type>.patches.ini` blijven bestaan voor wat **alle** games van
een soort delen. Zodra iets maar voor één game geldt, hoort het in de map van die game.

## `dongle.json` — de dongle van deze game

**Aanwezigheid van dit bestand betekent: deze game vraagt een dongle.** Geen bestand = geen dongle.
Dat is de machineleesbare betekenis waar de inventaris op kan leunen.

```json
{
  "kind": "hasp",
  "game": "Halo Fireteam",
  "memory": [
    {"name": "read/write", "fileid": 65524, "size": 112, "file": "dongle/memory/00000000000000001111111111110100"}
  ]
}
```

`kind` is één van:

| `kind` | waarvoor | wat er in `dongle/` staat |
|---|---|---|
| `hasp` | Halo, Terminator, TC5, NERF, Jurassic Park | `memory/<fileid>` — de geheugenimages |
| `matrix` | Rabbids Hollywood | `memory/<fileid>` als de game data leest; anders niets |
| `region` | de Sega-titels (LGI, LGI3D, LGS, …) | niets — de waarden staan in `dongle.json` |
| `sega-scan` | Transformers: Shadows Rising (`Sega/shell/dk2win64.dll`, log `MainThreadDongleScan: failed stage 1`) | niets — de getrapte scan staat in `dongle.json` |
| `amdaemon` | HOTD Scarlet Dawn | niets — verwijst naar de startketen |

Een nieuw `kind` toevoegen mag: zet het hier in de tabel, maak `scripts/<kind>.py` dat
`--game <Game> --json` aankan, en de launcher pikt het op. **Dat is de uitbreidingspunt voor een
nieuwe donglesoort** — dezelfde gedachte als bij de `Action`-woordenschat van de patches.

De bestandsnamen onder `dongle/` zijn **relatief aan de map van de game**, zodat de map verplaatsbaar
is.

## `dropin/` — binaire drop-ins

Hier staan bestanden die de launcher **letterlijk in de view van de game** zet: onze eigen
`matrix64.dll`, `ravengun-hasp.dll`, een `LD_PRELOAD`-bibliotheek, enzovoort. Nooit een kopie van
TeknoParrot's eigen bestanden — alleen wat wij zelf bouwen.

`dropin.ini` beschrijft per bestand waar het heen gaat:

```ini
[matrix64.dll]
To = .
DllOverride = native
Env = RAVENGUN_MATRIX_GAME=RabbidsHollywood
```

| Sleutel | Betekenis |
|---|---|
| `To` | doelmap, relatief aan de map van de exe van de game (`. ` = die map zelf). Mag omhoog (`../../Plugins`) zolang het doel in de view blijft: Unreal Engine 4 zet de exe in `<Game>/Binaries/Win64` en laadt zijn plug-ins uit `<Game>/Plugins` |

Het doel is **`To` + de sectienaam**, dus het pad staat maar één keer in het bestand. Staat het
pad ook al in de sectienaam, dan komt het bestand dubbel in de view te staan:

```ini
; FOUT: dit zet het bestand op view/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json
[ravengun-hasp/Halo/dongle.json]
To = ravengun-hasp/Halo

; GOED: het bestand komt op view/ravengun-hasp/Halo/dongle.json
[ravengun-hasp/Halo/dongle.json]
To = .
```

De launcher meldt de foute vorm als fout in plaats van hem stil uit te rollen. Controleer met
`build/rg-direct --plan <Game>`: bij `dropin_files` staat per bestand de `dest`.
| `DllOverride` | waarde voor `WINEDLLOVERRIDES`: `native` zet `<naam>=n`, `builtin,native` zet `<naam>=n,b` |
| `Env` | extra omgevingsvariabele(n), puntkomma-gescheiden |
| `Preload` | `true` als dit bestand in het **proces van de game** geladen moet worden (ELF of Windows-spel; zie "in welke variabele het pad belandt" hieronder) |

| `Audit` | `false` als dit bestand **niet** in `LD_AUDIT` mag (zie hieronder); standaard `true` |

### `Preload = true` — in welke variabele het pad belandt

`Preload = true` betekent "dit bestand moet in het proces van de game geladen worden". Hoe dat gaat hangt
af van hoe de game start, en de launcher kiest de variabele die op die route **werkelijk gelezen wordt**:

| Route | Variabele | Formaat | Wie laadt het |
|---|---|---|---|
| Windows-spel onder Wine via `OpenParrotLoader(64).exe` (`Type = OpenParrot`, `TeknoParrot`) | `TP_PRELOAD_DLL` | **één** Wine-pad, `Z:\...\naam.dll` | de lader, na de kern en voordat het spel verdergaat (`OpenParrotLoader/src/main.cpp`) |
| Linux-ELF onder Wine (`ElfLdr2`, `N2`, Lindbergh) | `LD_AUDIT` | paden met `:` ertussen | glibc, in het proces van de Cygwin-lader |
| echte native start | `LD_PRELOAD` | paden met `:` ertussen | de dynamische linker |

Het pad staat daarnaast altijd in `RG_DROPIN_PRELOAD`, voor de native route en de eigen ELF-lader. Op de
Wine-routes leest **niemand** die variabele: een bestand dat alleen daar staat wordt niet geladen.

`TP_PRELOAD_DLL` neemt precies één DLL, zonder scheidingsteken, en de lader geeft het pad door aan
`LoadLibraryW` in het spelproces; daarom een Wine-pad en geen pad met schuine strepen. Bestaat het
bestand niet of laadt het niet, dan beëindigt de lader het spel en geeft hij zelf 0 terug. Twee
`Preload = true`-bestanden voor één Windows-spel, een lader die de variabele niet leest
(`OpenParrotKonamiLoader.exe`, `TeknoMacaw`), of een `TP_PRELOAD_DLL` die al anders gezet is
(`--env`), zijn daarom **fouten** in het plan.

`--plan` zegt per bestand waar het belandt en of het geladen wordt:

```
drop-in: preload: rg-pe-hook.dll -> TP_PRELOAD_DLL=Z:\home\...\views\LuigisMansion\exe\x64\rg-pe-hook.dll (de OpenParrot-lader (OpenParrotLoader64.exe) laadt deze DLL na de kern in het spelproces)
drop-in: preload: libravengun-hasphook.so -> alleen RG_DROPIN_PRELOAD=...; WORDT NIET GELADEN door NIEMAND op deze route: ...
```

In `--plan --json` staat hetzelfde als `dropins.preload_env`: per bestand `var`, `value`, `loads` en `by`.

*Gemeten op 2026-10-13 (Luigi's Mansion): tot dan zette de launcher het pad op de Wine-route alleen in
`RG_DROPIN_PRELOAD`, en het spelproces had 0 van de 2 drop-in-bestanden in `/proc/<pid>/maps`.*

### Een drop-in die niet geplaatst kan worden is een fout, geen waarschuwing

Een run zonder zijn drop-in is een ongeldige run. Elke fout van de drop-in-motor (bestand ontbreekt in
`dropin/`, doel buiten de view, geen view, geen kanaal voor de preload) staat in `--plan` als
`drop-in: FOUT: ...`, en `--plan`, `--check` en `--run` geven dan exitcode 5 (zoals een geweigerde
patch: niet starten en niet terugvallen op TeknoParrotUi). Lukt het plaatsen tijdens `--run` alsnog
niet, dan stopt de launcher vóór hij iets start.

**De view en de map van de exe.** Een view is een map met symlinks naar de NAS. Loopt de weg van de
wortel van de view naar het doel via een symlink naar een map (ook **boven** de map van de exe:
`views/LuigisMansion/exe` was een symlink en de game staat in `exe/x64`), dan maakt de launcher die
mappen eerst lokaal: een echte map met symlinks naar de inhoud. `--plan` noemt ze
(`dropins.materialise`). `scripts/tp-add-game.py --view` doet hetzelfde al bij het maken van de view
voor de hele weg naar de exe, zodat het niet meer met de hand hoeft. Wat niet kan en dus een fout is:
iets op die weg dat geen map is, of een view die zelf een symlink is. Gevolg om te kennen: een bestand
dat het spel **nieuw** aanmaakt in een lokaal gemaakte map blijft in de view; bestaande bestanden zijn
symlinks en gaan nog naar de NAS.

### `Audit = false` — wanneer een bibliotheek NIET in `LD_AUDIT` mag

*Gemeten op 2026-10-11. Zie `docs/elfldr2-route.md` voor de hele familie en `patches/Halo/README.md`
voor de volledige proef.*

`Preload = true` betekent "dit bestand moet in het proces van de game geladen worden". Voor een
Linux-ELF onder Wine (`ElfLdr2`, `N2`, Lindbergh) zet de launcher het pad in **`LD_AUDIT`**. Dat is
één proces: ElfLdr2 is een Cygwin-programma dat de game-ELF **in zijn eigen adresruimte** laadt, dus
een auditbibliotheek komt in het proces van de Cygwin-lader terecht.

Voor `librt-glfix.so` is dat het werkende kanaal (de 32-bits games komen ermee voorbij hun start).
Voor een **inline-hook die de game wil aanhaken** is het fataal: `libravengun-hasphook` meldt nog
`spelbeeld op 0x00400000` en dan is de lader weg. Gemeten met losse proeven op Halo: alleen de hook
in `LD_AUDIT` geeft `exitcode 1` en de game komt nooit aan zijn eigen code toe; zonder de hook loopt
dezelfde start door naar `SubSystem Detection` en draait de game zijn eigen code. Bij JurassicPark
geeft hetzelfde patroon `exitcode -11` (SIGSEGV).

Daarom is `Audit` een keuze **per bibliotheek**:

| Sleutel | Waarden | Standaard | Betekenis |
|---|---|---|---|
| `Audit` | `true`/`false` | `true` | mag dit bestand in `LD_AUDIT` van de Wine-start? |

Met `Audit = false` blijft het pad in `RG_DROPIN_PRELOAD` staan — het kanaal voor de native route
(`src/ravengun/launch/native.*`) en voor een eigen lader — maar het bestand wordt niet in het proces van de
Cygwin-lader geladen. `--plan` laat de keuze zien:

```ini
[libravengun-hasphook-x64.so]
To = .
Preload = true
Audit = false
Env = RAVENGUN_HASP_ELF_LOG=hasphook.log
```

```
'audit': [], 'geen_audit': ['.../libravengun-hasphook-x64.so']
niet in LD_AUDIT (Audit = false): ... -- deze bibliotheek hoort in het spelproces en niet in het
Cygwin-proces van de lader
```

De standaard is `true`, zodat niets vanzelf verandert en de bestaande werkende drop-ins
(PacmanChomp, GalagaAssault, JurassicPark met `librt-glfix.so`) het kanaal houden.
