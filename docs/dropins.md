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
| `To` | doelmap, relatief aan de view van de game (`. ` = de hoofdmap van de game) |

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
`scripts/launch-direct.py <Game> --plan`: bij `dropins` staat per bestand de `dest`.
| `DllOverride` | waarde voor `WINEDLLOVERRIDES`: `native` zet `<naam>=n`, `builtin,native` zet `<naam>=n,b` |
| `Env` | extra omgevingsvariabele(n), puntkomma-gescheiden |
| `Preload` | `true` als dit bestand in het **ELF-proces** van de game geladen moet worden |
| `Audit` | `false` als dit bestand **niet** in `LD_AUDIT` mag (zie hieronder); standaard `true` |

### `Audit = false` — een inline-hook hoort niet in het Cygwin-proces

`LD_AUDIT` wordt gelezen door het proces dat de lader start, en dat is bij ElfLdr2 het
Cygwin-programma BudgieLoader. Voor `librt-glfix` is dat precies goed (de GL-fix moet in dat
proces zitten). Maar een inline-hook die de **game-ELF** wil aanhaken, sterft daar en neemt de
lader mee: gemeten met `libravengun-hasphook` in `LD_AUDIT` gaf Halo exitcode 1 en Jurassic Park
een SIGSEGV. Zo'n bestand zet je op `Audit = false`; het pad blijft dan in `RG_DROPIN_PRELOAD`
staan, dat het spelproces oppakt.

```ini
[libravengun-hasphook.so]
To = .
Preload = true
Audit = false
```

### De grenzen van `LD_AUDIT` (gemeten)

`LD_AUDIT` is de route voor een Linux-ELF onder Wine, maar hij is niet onbeperkt. Gemeten op
2026-10-11 met `launch-direct.py Halo --run`, met de twee drop-in-bibliotheken in `LD_AUDIT`:

```
ERROR: ld.so: object '.../ravengun-hasp-x64.so' cannot be loaded as audit interface:
       undefined symbol: la_version; ignored.
ERROR: ld.so: object '.../libravengun-hasphook-x64.so' cannot be loaded as audit interface:
       wrong ELF class: ELFCLASS64; ignored.
```

Twee dingen om te weten voordat je iets in `LD_AUDIT` zet:

* **Alleen een echte auditbibliotheek werkt.** glibc eist `la_version`; de HASP-emulator is een
  gewone bibliotheek met `hasp_*` en wordt dus geweigerd. Een bibliotheek in `LD_AUDIT` moet
  `la_version` en `la_preinit` hebben (die heeft de inline-hook wel).
* **De klasse moet kloppen.** Wie `LD_AUDIT` leest is het Wine-proces dat de lader start; staat
  daar een 32-bits lezer, dan wordt een 64-bits bibliotheek met "wrong ELF class" genegeerd.
  Let dus op de bitbreedte van het proces dat je wilt inhaken, niet op die van de game-exe.

Een fout hier is stil: de omgeving staat goed, `--plan` meldt geen fout, en de bibliotheek wordt
toch niet geladen. Controleer met de logregels hierboven of met een eigen logbestand
(`RAVENGUN_HASP_ELF_LOG`).

### `Preload = true` — en waarom de launcher géén `LD_PRELOAD` zet

Gemeten op de Deck, en dit is geen theorie: **`LD_PRELOAD` in de startomgeving is fataal voor de
ElfLdr2-games.**

```
[launch-direct] omgeving: LD_PRELOAD=.../ravengun-hasp-x64.so ...
ERROR: ld.so: object '...' from LD_PRELOAD cannot be preloaded (wrong ELF class: ELFCLASS64): ignored.
Cygwin WARNING: Couldn't compute FAST_CWD pointer...
BudgieLoader_x64: *** fatal error - error while loading shared libraries: ...ravengun-hasp-x64.so
```

`BudgieLoader` is een **Cygwin**-programma (`msys-2.0.dll` leest `LD_PRELOAD` en sterft eraan), en
Wine geeft zijn omgeving aan alles door. Zonder die variabele start de game weer volledig.

Daarom geldt:

- De **launcher zet nooit `LD_PRELOAD`** in de omgeving van de start.
- Bij `Preload = true` levert de launcher het pad als **`RG_DROPIN_PRELOAD`** (onze eigen variabele,
  zoals `RG_LD_PRELOAD`), puntkomma-gescheiden bij meerdere bestanden.
- **De native route of de lader zet dat pad zelf in `LD_PRELOAD` van het ELF-proces van de game.**
  Wie een ElfLdr2-vervanging bouwt (of `play-native.sh` aanpast), moet `RG_DROPIN_PRELOAD` lezen en
  in `LD_PRELOAD` zetten — dan werkt de drop-in, en de Cygwin-lader blijft erbuiten.

Dezelfde val geldt voor elke `Preload`-drop-in bij een game die via een Cygwin- of MSYS-lader start.

De launcher leest `dropin.ini` en doet de rest. **Een nieuwe drop-in toevoegen is een bestand in
`dropin/` zetten plus één sectie in `dropin.ini`** — geen codewijziging.

## `game.ini` — `[Dongle]`

De sectie houdt twee dingen:

| Sleutel | Waarden | Standaard | Betekenis |
|---|---|---|---|
| `Enabled` | `true`/`false` | `false` (weggelaten) | de eigenaar zet de dongle aan |
| `Source` | `usb-passthrough` \| `drop-in` | `usb-passthrough` | de fysieke dongle van de eigenaar, of de drop-in uit deze map |

Twee bronnen, niet meer: de keuze is "mijn echte dongle" of "wat hier in de map staat". **Welke
soort** het is staat in `dongle.json`, niet in `game.ini` — één bron van waarheid.

In de **meegeleverde** laag staat `Enabled` weggelaten (dus uit); de eigenaar zet hem aan in zijn
eigen laag. Zo verandert er niets aan de start van een game tot iemand er bewust voor kiest.

## Waarom zo

- **Modulair.** Een game toevoegen is één map kopiëren. Geen gedeelde lijst die je moet bijwerken,
  dus geen merge-conflicten tussen bijdragers.
- **Bij te werken.** Een spelversie verandert? Dan verandert er één map. De rest van de repo blijft
  ongemoeid.
- **Zichtbaar.** Je ziet in één oogopslag wat een game nodig heeft: welke patches, welke dongle,
  welke drop-ins.
- **Terugdraaibaar.** Een drop-in weghalen is het bestand weghalen; de game valt terug op de
  standaardroute.

## Wat dit vervangt

- De globale `hasp/<Game>/`-boom en `~/.local/share/ravengun/hasp/<Game>/` als plek voor
  dongle-gegevens: die gegevens horen in `patches/<Game>/dongle/`. De **emulator-binary** zelf
  (`ravengun-hasp.so`) is niet game-specifiek en blijft waar hij is.
- Vier donglebronnen in `[Dongle] Source`: dat worden er twee, omdat de soort nu in `dongle.json`
  staat.
