# Tussenrapport — de meting per game (`launch-direct.py --plan`)

**Herkomst:** dit is de ruwe uitvoer van één commando per game, uitgevoerd op de Deck op 2026-10-11. Het is
de harde basis onder kolom *Draait nu mee* en *Laderfamilie* van
[loskoppelen-per-game.md](../loskoppelen-per-game.md).

## Het commando

```bash
cd ~/projects/ravengun
for d in patches/*/; do
  g=$(basename "$d")
  [ "$g" = "_defaults" ] && continue
  echo "################ $g"
  timeout 120 python3 -I scripts/launch-direct.py "$g" --plan --json 2>&1
  echo "---- exitcode: $?"
done
```

De volledige uitvoer (2406 regels) staat in `re/scratch/_lp5.out` in de Windows-werkmap en op de Deck in
`/home/deck/_lp5.out`.

**Waarom `--plan` en niet `--check`:** `--plan` bouwt het complete startplan op **zonder bijwerkingen**
(`build_plan()` in `scripts/launch-direct.py`), inclusief de `argv` die de launcher echt zou starten, de
omgeving, de patchlijst, `needs_activation` en de gegenereerde `teknoparrot.ini`. Dat is precies de vraag
"welke TeknoParrot-onderdelen draaien er mee".

---

## De uitkomst per game

| Game | `emulator_type` | `is64` | lader uit `argv` | kern uit `argv` | `needs_activation` | `[Launch] Mode` |
|---|---|---|---|---|---|---|
| 3DCosplayMahjong | — | — | **exit 4: game niet gevonden** | — | — | geen |
| GaiaAttack4 | `OpenParrot` | false | **eigen** `openparrot/current/OpenParrotWin32/OpenParrotLoader.exe` | **eigen** `openparrot/current/OpenParrotWin32/OpenParrot` | false | geen |
| GalagaAssault | `ElfLdr2` | false | runtime `views/GalagaAssault/ElfLdr2/BudgieLoader.exe` (via de view) | — | false | geen |
| Gashaaaan | `TeknoParrot` | false | **eigen** `OpenParrotWin32/OpenParrotLoader.exe` | **gesloten** `runtime/…/TeknoParrot/TeknoParrot` | false | direct |
| Gashaaaan2 | `TeknoParrot` | false | **eigen** | **gesloten** | **true** | direct |
| GhostBusters | `ElfLdr2` | false | runtime `views/GhostBusters/ElfLdr2/BudgieLoader.exe` | — | false | geen |
| GoGoStrike | `ElfLdr2` | false | runtime `views/GoGoStrike/ElfLdr2/BudgieLoader.exe` | — | **true** | direct |
| HOTDSD | — | — | **exit 4: profielen met twee exe's worden niet direct gestart** | — | — | **tpui** |
| Halo | `ElfLdr2` | **true** | runtime `ElfLdr2/x64/BudgieLoader_x64.exe` | — | **true** | direct |
| HauntedMuseum | `OpenParrot` | false | **eigen** | **eigen** `OpenParrot` | false | geen |
| HauntedMuseumII | `OpenParrot` | false | **eigen** | **eigen** `OpenParrot` | false | geen |
| InfinityBladeFX | `TeknoParrot` | false | **eigen** | **gesloten** | false | geen |
| JurassicPark | `ElfLdr2` | false | runtime `views/JurassicPark/ElfLdr2/BudgieLoader.exe` | — | false | geen |
| LGI | `TeknoParrot` | false | **eigen** | **gesloten** | false | direct |
| LGI3D | `TeknoParrot` | false | **eigen** | **gesloten** | false | direct |
| LGS | `TeknoParrot` | false | **eigen** | **gesloten** | false | geen |
| LethalEnforcers3 | `TeknoParrot` | false | **eigen** | **gesloten** | false | geen |
| LuigisMansion | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | direct |
| MedaruNoGunman | `TeknoParrot` | false | **eigen** | **gesloten** | false | direct |
| MusicGunGun2 | `OpenParrot` | false | **eigen** | **eigen** `OpenParrot` | false | geen |
| NERF | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | **true** | direct |
| OmatsuriYasanGansoTakoyaki | `TeknoParrot` | false | **eigen** | **gesloten** | **true** | geen |
| OmatsuriYasanKingyoSukui | `TeknoParrot` | false | **eigen** | **gesloten** | **true** | geen |
| Otomedius | `TeknoParrot` | false | **eigen** | **gesloten** | **true** | geen |
| PacmanChomp | `ElfLdr2` | false | runtime `views/PacmanChomp/ElfLdr2/BudgieLoader.exe` | — | **true** | geen |
| PigsMightFly | `TeknoParrot` | false | **eigen** | **gesloten** | **true** | direct |
| RabbidsHollywood | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | geen |
| RaccoonRampage | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | direct |
| RainbowBombergirl | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | **true** | direct |
| Rampage | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | direct |
| TC5 | `TeknoParrot` | **true** | **runtime** `runtime/…/OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | direct |
| TankTankTank | — | — | **exit 4: game niet gevonden** | — | — | geen |
| Terminator | `ElfLdr2` | false | runtime `views/Terminator/ElfLdr2/BudgieLoader.exe` | — | false | direct |
| TombRaider | `TeknoParrot` | **true** | **eigen** `OpenParrotx64/OpenParrotLoader64.exe` | **gesloten** `TeknoParrot64` | false | geen |
| TransformersShadowsRising | `TeknoParrot` | false | **eigen** `OpenParrotWin32/OpenParrotLoader.exe` | **gesloten** `TeknoParrot` | false | direct |

**Telling:** eigen lader 24 · runtime lader 8 (7 BudgieLoader + TC5) · niet te starten 3.
Eigen kern 4 · gesloten kern 21.

---

## Wat er in het plan verder opvalt

### De Ui als standaardroute

`scripts/play-halo.sh` regel 217-225:

```bash
# Startwijze (A10): direct (scripts/launch-direct.py, zonder TeknoParrotUi) of teknoparrot-ui. Volgorde: --direct/--tpui, anders
# [Launch] Mode in game.ini (gebruikerslaag over de meegeleverde), anders teknoparrot-ui. --teknoparrot (route evdev) betekent de UI.
[[ "$LAUNCH" == direct || "$LAUNCH" == teknoparrot-ui ]] || LAUNCH=teknoparrot-ui
```

**16 games** hebben `[Launch] Mode = direct` in `patches/<Game>/game.ini`: Gashaaaan, Gashaaaan2, GoGoStrike,
Halo, LGI, LGI3D, LuigisMansion, MedaruNoGunman, NERF, PigsMightFly, RaccoonRampage, RainbowBombergirl,
Rampage, TC5, Terminator, TransformersShadowsRising. **18 games** hebben géén `Mode` en starten dus standaard
via TeknoParrotUi; **HOTDSD** heeft `Mode = tpui`. De gebruikerslaag
(`~/.local/share/ravengun/patches/<Game>/game.ini`) zet in **geen enkele** game een `Mode` — de verdeling
hierboven is dus de volledige.

### De unity-route gaat buiten TeknoParrot om

`scripts/play-halo.sh` regel 205-211:

```bash
# Unity-route (zonder TeknoParrot): het profiel staat in packaging/aagames-games.json.
if [[ $USE_TP -eq 0 && $NATIVE -eq 0 && "$ROUTE" != tp-shm ]] && grep -qE "^[[:space:]]{4}\"$PROFILE\":[[:space:]]*\{" "$ROOT/packaging/aagames-games.json" 2>/dev/null; then
  ...
  "$ROOT/scripts/play-unity.sh" "${UARGS[@]}"
```

`packaging/aagames-games.json` bevat **TombRaider** en **RabbidsHollywood**, beide met `"plugin": "ravengun"`
(eigen plugin uit `plugins/unity/`, TCP-poort 33610). Voor die twee is TeknoParrot dus **niet** in het
standaardpad; `--teknoparrot` (`USE_TP=1`) forceert de oude route.

### De dongle-poort staat overal uit

In **elke** `--plan`-uitvoer staat:

```json
"dongle": { "enabled": false, "source": "usb-passthrough", "error": "" }
```

Dat komt doordat `grep '^\[Dongle\]' patches/*/game.ini` **nul** treffers geeft, ook in de gebruikerslaag
(`~/.local/share/ravengun/patches/*/game.ini`). De dongle-afhandeling van `launch-direct.py`
(`dongle_gate()`, exitcode 6) is dus nergens actief; wat er aan dongle gebeurt, gebeurt binnen de gesloten
kern of in de game zelf.

### De gegenereerde `teknoparrot.ini` als regiobewijs

`--plan` toont het bestand dat de launcher naast de game zou schrijven. Daaruit komt het regiobewijs voor
GaiaAttack4, HauntedMuseum, HauntedMuseumII, LGI, LGI3D, LGS en RainbowBombergirl:

| Game | `[General]`-regel in de gegenereerde `teknoparrot.ini` |
|---|---|
| GaiaAttack4 | `Region=E` (plus `MINIGUN.INI`: `REGION\t\tE`) |
| HauntedMuseum | `Region=E` (plus `MUSEUM.INI`: `REGION\t\tE`) |
| HauntedMuseumII | `Region=E` (plus `HAUNTED2.INI`: `REGION\t\tE`) |
| LGI | `DongleRegion=JAPAN`, `PcbRegion=JAPAN` |
| LGI3D | `DongleRegion=USA`, `PcbRegion=USA` |
| LGS | `DongleRegion=USA`, `PcbRegion=USA` |
| RainbowBombergirl | `Region=EXPORT` |

Dat `Region`/`DongleRegion`/`PcbRegion` uit `game.ini` mag komen, wordt door de launcher zelf gecontroleerd:
`apply_profile_choices()` (`scripts/launch-direct.py` regel 560) weigert een waarde die niet in de keuzelijst
van het profiel staat, met `SettingRefused` (exitcode 5). Omdat alle zeven plannen met exitcode 0 doorkomen,
**bestaat het veld in die profielen** — onafhankelijk bewijs naast de `GameProfiles/*.xml`-uitlezing.

### De profielrevisie wordt gecontroleerd

`load_own_profile()` (regel 169) weigert een game als `ProfileRevision` in `game.ini` afwijkt van
`GameProfileRevision` van het sjabloon in de runtime. Dat is de reden dat de profielenset altijd meedraait,
ook bij een volledig directe start.

---

## De runtime-views (12)

```
$ ls -1 ~/.local/share/ravengun/runtime/views/
GalagaAssault  GhostBusters  GoGoStrike  JurassicPark  LuigisMansion  MedaruNoGunman
PacmanChomp  RabbidsHollywood  RaccoonRampage  RainbowBombergirl  T4User  Terminator
```

Een view is een lokale map met **symlinks** naar de NAS-map van de game (plus een `ElfLdr2`-symlink naar de
runtime en soms een `TeknoParrot`-map). `--plan` laat zien welke games hun spelpad via een view krijgen
(`"game_source": "view"`): GalagaAssault, GhostBusters, GoGoStrike, JurassicPark, LuigisMansion,
MedaruNoGunman, PacmanChomp, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Terminator. De rest gebruikt
`"game_source": "folders"` (het pad direct op de NAS).

**Waarom de views bestaan** (`packaging/games-status.md`, ronde 1): mapnamen met een spatie aan het eind
("Rabbids Hollywood ", "Terminator Salvation ") geven in Wine `Directory name invalid (0x10B)`; een view met
symlinks lost dat op zonder de game te kopiëren.
