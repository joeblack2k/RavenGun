# Verificatierun Transformers: Shadows Rising — voorbereiding

Opgesteld 2026-10-13, bijgewerkt nadat de `Boards`-uitbreiding klaar was. **Nog niet gedraaid.** Doel: de
start van deze game met onze eigen lader als **gemeten verificatie** vastleggen, in de vorm van
`patches/STARTTEST.md`, en daarbij voor het eerst het **IO-bord** laten meedoen.

## 1. Wat al gemeten is, en wat niet — lees dit eerst

| vraag | stand |
|---|---|
| Start de **lader** `Shell.exe` en blijft hij leven? | **al gemeten**: `patches/STARTTEST.md` heeft `TransformersShadowsRising \| START \| SPEL 1s \| LADER 1` (2026-10-12 01:50, `starttest3.sh`) |
| Komt het **spel** (`Transformers2.exe`) op? | **niet door die meting gedekt** — zie §2; wel indirect via de eigenaar ("the newest one in fullscreen", zelf gespeeld) |
| Werkt de **gun** (Move als geweer)? | door ons gemeten (vizier binnen 1,5 % op 2026-10-11) |
| Werkt het **IO-bord** (lampen, toetsenpaneel, recoil)? | **nergens gemeten, en in geen enkele sessie ooit gestart**: `ioboard - *.log` zegt in élke sessie op de NAS `Couldn't find IOBoard device`, ook in de gespeelde sessies |

De eigenaar bevestigt dus de **start** en het **geweer**, niet het IO-bord. Het IO-bord is een apart apparaat
dat in de oude configuratie niet eens werd aangevraagd; deze run zet het er voor het eerst bij.

## 2. Wat deze run toevoegt

1. **Het tweede proces.** `starttest3.sh` bepaalt zijn procespatroon uit `launch-direct.py <Game> --plan`'s
   `game:`-regel, en dat is hier `[Identify] Path = shell/Shell.exe`. De bestaande START-regel meet dus
   **`Shell.exe`, niet `Transformers2.exe`**. Dat de shell daarna het spel start, weten we alleen uit
   `Mainthread - <datum>.log` (`Launched …\Transformers2\Transformers2.exe`). Deze run meet dat tweede
   proces expliciet.
2. **Het IO-bord voor het eerst.** Wordt de kaart gevonden, en loopt de handshake tot `Unlocked OK`?
3. **De keten op tijd**: `[Loader] PostStartMs = 3000` en de laadtijd tot attract (3,5–4 min) als geheel.
4. **Het joystick-id van het geweer** met een tweede HID-apparaat ernaast (check 0, §7).

## 3. Opzet — één bestand, dan één commando

De **meegeleverde** `game.ini` blijft ongemoeid (`Board = SaeGunBoard`), zodat het spel van de eigenaar niet
verandert voordat deze run iets bewijst. Het IO-bord komt er via de **gebruikerslaag**, die per sleutel over
de meegeleverde laag heen gaat (`gamesettings::Dirs`, `src/ravengun/gamesettings/gamesettings.hpp:83-90`):

```sh
mkdir -p ~/.local/share/ravengun/patches/TransformersShadowsRising
cat > ~/.local/share/ravengun/patches/TransformersShadowsRising/game.ini <<'EOF'
; Alleen voor de verificatierun: het IO-bord naast de gun. Dit bestand weghalen zet het terug.
[Input]
Boards = SaeGunBoard, SaeIoBoard
EOF

# 1. vooraf: laat de lader zien wat hij gaat doen (welke exe, welke omgeving)
python3 ~/projects/ravengun/scripts/launch-direct.py TransformersShadowsRising --plan

# 2. de run zelf -- LET OP: play-halo.sh, NIET starttest3.sh.
#    Gemeten op 2026-10-13: starttest3.sh roept alleen `launch-direct.py --run` aan en wapent de
#    invoerroute NIET (geen `ravengunctl iobridge start`), dus dan bestaat er geen enkel virtueel bord --
#    de daemon zegt zelf `IO-brug: geen (… route evdev)`. Alleen play-halo.sh doet `iobridge start`.
# 1b. BENCHCONTROLE, altijd eerst: draait er een native game of een tweede gamescope? Wacht dan.
#     De native route van een andere partij draait een proces dat 'game' heet (sandbox als /pm/g7/halo/game)
#     plus zijn eigen gamescope; dat is aan de Wine-kant niet te zien. Gebeurde op 2026-10-13.
pgrep -af 'pm/.*/game$|gamescope' | grep -v grep
bash scripts/play-halo.sh --profile TransformersShadowsRising --no-wait-move
```

Tijdens en na de run:

```sh
# 3. het tweede proces (dit is wat §2.1 toevoegt)
pgrep -af 'Transformers2[/\\]Transformers2.exe'

# 4. check 0: welk joystick-id heeft de gun? LEZEN, niet zetten. (De prefix van deze game is
#    [Wine] Prefix = ravengun-test-tsr; zowel ~/.local/share/TeknoParrotUI/prefixes/ als
#    ~/.local/share/ravengun/prefixes/ verwijzen naar dezelfde prefix -- beide bevatten drive_c/Sega
#    en de native d3dx9_43.dll, nagemeten op 2026-10-13.)
export RG_WINE=~/.local/share/ravengun/wine/proton-11.0-2/bin/wine
WINEPREFIX=~/.local/share/TeknoParrotUI/prefixes/ravengun-test-tsr WINEDEBUG=-all "$RG_WINE" reg query \
  'HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput' /s \
  | grep -E 'Calibration|Joystick Id'
#    verwacht: VID_1209&PID_72F0 (de gun) met Joystick Id 0, VID_1209&PID_72F1 (het IO-bord) op een ander id.
#    Klopt het niet: scripts/wine-joyid.sh <prefix> 1209:72F0 ZET het goed -- dat is de herstelstap, niet de check.

# 5. de logboeken van de shell (op de NAS, alleen lezen)
cd ~/mnt/spidernas_emulation/Teknoparrot/Sega/shell
grep -n 'Launched' "Mainthread - $(date +%y%m%d).log"
grep -n 'IOBOARD\|InitIOBoard\|Unlocked\|Keypad' "ioboard - $(date +%y%m%d).log"

# 6. de daemon: welke borden, wat stuurde de game, welk antwoord is er gezet?
grep -E 'IO-brug|hid-board' ~/.local/share/ravengun/log/*.log | tail -30
```

**Waarom 240 s en niet 22 s:** deze game laadt 3,5–4 min tot attract; met 22 s meet je alleen dat de shell
bestaat, en dat is al bekend. `starttest3.sh` ruimt op met het patroon van de exe, nooit met een leeg patroon.

## 4. De config tegen het dossier: klopt het?

Ja, op alle drie de punten — en één moet je goed lezen:

* **`Executable = shell.exe;Transformers2.exe` is NIET het "twee exe's"-mechanisme van de lader.** Dat
  mechanisme heet `HasTwoExecutables` met `Executable2` (HOTDSD: `amdaemon.exe` naast de game) en
  `launch-direct.py` **weigert** zulke profielen — daarom staat HOTDSD in `STARTTEST.md` als `niet-gestart`
  met "profielen met twee exe's worden". Bij deze game is `Executable` de **namenlijst uit het
  TeknoParrot-profiel** (`ExecutableName`, puntkomma-gescheiden) en start de lader **één** proces:
  `shell/Shell.exe`, gekozen via `[Identify] Path`. Dat `Transformers2.exe` daarna opkomt doet de **shell**
  zelf. Dat verklaart precies waarom deze game wél START en HOTDSD niet — een verschil dat makkelijk te
  verwarren is.
* **`PostStartMs = 3000`** komt overeen met `TP_POSTSTART_REMOTETHREAD_MS=3000`.
* **`Route = hid-board` / `Board = SaeGunBoard`** komt overeen: dat is de gun (DirectInput), de route waarmee
  de game speelbaar werd.

## 5. De `Boards`-uitbreiding is klaar (en waarom het op deze manier moet)

De lader startte bij `Route = hid-board` **exact één bord** (`start_hid_board` gebruikte alleen `r->board`).
Nu accepteert die route net als `jvs` een **lijst**: `Boards = A, B`, elk bord met zijn eigen transport.
Eén bord houdt de oude vorm en de oude statusvorm; een lijst gaat door `HidBoardsDialect`, dat de aanroepen
verdeelt en bij een mislukt bord de al geopende borden weer sluit. Bron:
`src/ravengund/iobridge_service.cpp` (`start_hid_board`), `src/ravengun/iobridge/hid_board.hpp`
(`HidBoardsDialect`), `src/ravengun/iobridge/routes.cpp` (validatie accepteert `Board` **of** `Boards`).
Getest: 122 controles in `ravengun_hid_board_tests` (was 111) en 22289 in `ravengun_iobridge_tests` (was
22287), beide 0 mislukt; de hele suite blijft op 43/44 met alleen de bestaande `ravengun_install`-faler.

**Voorwaarde, nagemeten op deze Deck:** schrijven naar `/dev/uhid` moet mogen. Dat is zo: `/dev/uhid` is
`crw-rw----+ root root` en **schrijfbaar** voor de gebruiker, en
`/etc/udev/rules.d/71-ravengun-uhid.rules` is geïnstalleerd (`scripts/udev-ravengun.sh --check` meldt hem als
actueel). Op een machine zonder die regel faalt het IO-bord en **daarmee de hele route, dus ook de gun** — de
lijst is streng. Daarom staat die controle als vaste voorbereidingsstap in §3.

## 6. Waar het IO-board-antwoord staat — en de bewuste keuze daarover

Het antwoord staat **als data**, niet in de kern, maar het staat in de **lader-repo**:

| wat | waar | bron |
|---|---|---|
| borddefinities (`SaeGunBoard`, `SaeIoBoard`) + de vijf gemeten antwoorden | `~/projects/ravengun/packaging/hid-boards.json` | `src/ravengund/main.cpp:528` via `load_text("hid-boards.json", …)` |
| de per-game keuze | `patches/TransformersShadowsRising/game.ini` (`Board`/`Boards`) | — |
| `dropin/` | is voor **binaire** drop-ins, niet voor data | `docs/dropins.md` |

`load_text` zoekt eerst `packaging/<bestand>` in de datamap en valt anders terug op het **in de daemon
ingebakken asset**; de dropin-`kind`-tabel (`hasp`, `matrix`, `region`, `sega-scan`, `amdaemon`) heeft
**geen bord-soort**. **Besluit (genomen, met reden):** de borddefinities blijven voorlopig in dit gedeelde
register, want een borddefinitie is **generieke** data (assen, knoppen, rapportvormen, antwoorden) en geen
game-specifiek gegeven; de **per-game keuze** staat wél in de patchmap, en dat is de afspraak die telt.
Daarmee is het een bewuste keuze en geen sluipende afwijking — `SaeGunBoard` en `tp-shm-profiles.json` hebben
dezelfde status. **Als er ooit een borddefinitie komt die alleen voor één game geldt**, dan is een nieuw
dropin-`kind` de weg, en dan is dat een beslissing met een reden in plaats van een gewoonte.

## 7. Wat elke uitkomst betekent (vooraf opgeschreven)

**Check 0, vóór alles:** is het geweer nog DirectInput **joystick-id 0**? Het IO-bord is een Generic
Desktop/Game Pad HID-apparaat, dus Wine kan het als joystick opvoeren en de gun van id 0 duwen.
*Symptoom als het misgaat:* vizier in de hoek en de X-as doet niets — precies het gedrag van 2026-10-10, en
niets met het protocol te maken. *Herstel:* `scripts/wine-joyid.sh <prefix> 1209:72F0` opnieuw **nadat** het
IO-bord bestaat (de daemon niet herstarten terwijl de game draait).

| # | uitkomst | wat het betekent |
|---|---|---|
| **a** | shell start **en** `Transformers2.exe` komt op (proces gezien + `Mainthread` zegt `Launched`) | De startketen is bevestigd: lader → shell → spel, met `PostStartMs=3000`. Dit is de verificatie die de opdracht per game vraagt, en het is **meer** dan de bestaande START-regel (die meet alleen de shell). |
| **b** | shell start, `Transformers2.exe` komt niet op | Bekende oorzaken in deze orde: (1) de shell blijft op `SYSTEM INITIALIZING... LOADING..` — dan is het spel niet door de shell gestart; (2) de prefix mist de native `d3dx9_43`/`d3dcompiler_43`/`d3dcompiler_47` → HLSL-fout `E5017 … fx_2_0 sampler objects` (venster op de tv); (3) de `C:\Sega`-symlink of het `Z:`-station ontbreekt. |
| **c** | `InitIOBoard : Initialised OK` **en** de `HIDioThread`-reeks (`IOBOARD_IDREQUEST1` → … → `Unlocked OK`), met in het daemonlogboek een regel per stap (`hid-board: uitvoerrapport 00 74 E0 -> raw[12]=0xB0`) | **Het IO-bord werkt**: de shell vindt onze kaart en onze antwoorden worden geaccepteerd. Dit is voor het eerst gemeten. `Keypad Enabled OK` hoort **niet** te verschijnen (het `ExternalKeypad`-bit staat uit; een beslissing bij de eigenaar, geen fout). |
| **c′** | `Couldn't find IOBoard device` terwijl de daemon wél `hid-board voor … (RavenGun SAE Gun Board via uinput, RavenGun SAE IO Board via uhid)` logt | De kaart wordt aangemaakt maar niet gevonden. De onderscheiden faalwijzen uit het rapport: **nul** geënumereerde apparaten = Wine geeft `/dev/uhid` niet door aan `SetupDi`/`HidD` (of de usage wordt niet als Game Pad gelezen); wél `output_reports` maar geen `_REPLY` = onze inputrapporten komen niet aan. |
| **c″** | het IO-bord kan niet geopend worden (`/dev/uhid`-fout) | De hele route faalt, dus **ook de gun** (de lijst is streng). Herstel: `sudo scripts/udev-ravengun.sh --apply`, of de gebruikerslaag weghalen zodat alleen de gun overblijft. |
| **d** | iets anders | Noteer de laatste logregel en de proceslijst letterlijk. Twee bekende, niet reproduceerbare gevallen: sessies die kort na de start eindigen met `Unexpect Game termination`, en een shaderfoutvenster. |

## 8. Wat ik nodig heb

* De bench, met beeld op de tv, ~10 minuten (4 min laden + attract + nameten).
* Verder niets: `/dev/uhid` is schrijfbaar en de udev-regel staat er al (nagemeten, §5).
* **Na een geslaagde run** is er één beslissing: het IO-bord ook in de **meegeleverde** `game.ini` zetten
  (`Boards = SaeGunBoard, SaeIoBoard`) in plaats van alleen in de gebruikerslaag. Dat is pas verstandig als
  check 0 (joystick-id) én de handshake groen zijn, want anders regresseert het spel van de eigenaar.
* De keypadvraag (`ExternalKeypad` in `shell/Game.ini` op de NAS) ligt bij de eigenaar; zonder dat bit stopt
  de handshake na `Unlocked OK` en dat is geen fout.
