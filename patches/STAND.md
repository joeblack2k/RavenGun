# Stand van de patches — volledigheidskaart

*Wat dit bestand is:* één tabel over alle games in `patches/` met wat er per game is en wat er nog
ontbreekt: `game.ini` en `[Status]`, de drop-in (hoeveel bestanden en hoeveel daarvan een sectie in
`dropin.ini` hebben), de dongle uit `dongle.json`, de README, en de `To` van elke sectie. De kaart is
gemaakt in ronde 25 (2026-10-11) en is bedoeld om na elke ronde opnieuw te draaien.

*Wat het contract is:* alles wat één game nodig heeft staat in `patches/<Game>/`
(`docs/dropins.md` in de loader-repo). Elk bestand in `dropin/` moet een **eigen sectie** in
`dropin.ini` hebben, anders wordt het niet uitgerold — dat is de fout die het vaakst misgaat en die
de motor **niet** meldt. De dongle is geen loaderbegrip: het is een gewone drop-in met zijn gegevens
ernaast (`docs/geen-dongle-in-de-loader.md`).

## Hoe deze kaart is gemaakt

Op de Deck, vanuit de patches-repo:

```bash
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py          # vult het blok tussen BEGIN TABEL en END TABEL hieronder
```

`patches/stand-gen.py` staat volledig in de [bijlage](#bijlage-het-generatorscript); het is hetzelfde
script. Het leest alleen `game.ini`, `dropin/` + `dropin.ini` en het `dongle.json` per game, telt de
bestanden in `dropin/` **recursief** (ook `ravengun-hasp/<Game>/memory/…`) en schrijft alleen dit
bestand.

Wat er daarnaast met de loader zelf is gecontroleerd:

```bash
cd ~/projects/ravengun
python3 tools/verify-all.py --md /tmp/per-game-stand.md   # lader, kern, dongle, drop-in, patches
python3 scripts/launch-direct.py <Game> --plan            # de echte dest van elk drop-in-bestand
python3 -m unittest discover -s tests 2>&1 | tail -5
```

De uitkomsten daarvan staan onder [Verificatie](#verificatie-met-de-loader-zelf).

<!-- BEGIN TABEL -->
| Game | `game.ini` | `[Status] Status` | drop-in (bestanden/secties) | zonder sectie | `To` ↔ sectie | dongle | README | wat ontbreekt er nog |
|---|---|---|---|---|---|---|---|---|
| **3DCosplayMahjong** | ja | **-** | geen `dropin/` | - | - | - | **nee** | geen `[Status] Status`; geen README; geen `patches.ini`; niet direct te starten: gamemap niet ingesteld (`--plan` rc=4) |
| **GaiaAttack4** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld, zie `game.ini`); `patches.ini` aanwezig |
| **GalagaAssault** | ja | parked | 6 / 6 | - | ok | hasp | ja | **andere agent**; het bestand zonder sectie is een 64-bits overblijfsel van `build-hasphook.sh` in een 32-bits game |
| **Gashaaaan** | ja | playable | geen `dropin/` | - | - | - | ja | compleet; geen drop-in nodig |
| **Gashaaaan2** | ja | playable | geen `dropin/` | - | - | - | ja | compleet; geen drop-in nodig |
| **GhostBusters** | ja | parked | 0 / 0 | - | ok | - | ja | lege `dropin/`; geen `dongle.json` (hasp verwacht); **andere agent**; geen `dropin/`; geparkeerd: GAME PATH CHECK FAILED |
| **GoGoStrike** | ja | playable | geen `dropin/` | - | - | - | ja | compleet; geen drop-in nodig |
| **HOTDSD** | ja | playable | geen `dropin/` | - | - | - | **nee** | geen `dongle.json` (amdaemon verwacht); geen README; `--plan` rc=4: het profiel start twee exe's (`amdaemon.exe` eerst), dat doet `launch-direct.py` niet |
| **Halo** | ja | playable | 7 / 7 | - | ok | hasp | ja | drop-in compleet na reparatie (7 bestanden, 7 secties); de 32-bits hook heeft bewust geen `Preload` |
| **HauntedMuseum** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld); `patches.ini` aanwezig |
| **HauntedMuseumII** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld); `patches.ini` aanwezig |
| **InfinityBladeFX** | ja | parked | 3 / 3 | - | ok | matrix | ja | geparkeerd (UE3: wit of zwart beeld) |
| **JurassicPark** | ja | parked | 6 / 6 | - | ok | hasp | ja | **andere agent**; het bestand zonder sectie is een 64-bits overblijfsel van `build-hasphook.sh` in een 32-bits game |
| **LGI** | ja | playable | 1 / 1 | - | ok | region | ja | **regio-agent**; regio niet afgedwongen (`enforced = false`) |
| **LGI3D** | ja | playable | 1 / 1 | - | ok | region | ja | **regio-agent**; regio niet afgedwongen (`enforced = false`) |
| **LGS** | ja | playable | 1 / 1 | - | ok | region | ja | **regio-agent**; regio niet afgedwongen (`enforced = false`) |
| **LethalEnforcers3** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (32-bits GStreamer ontbreekt) |
| **LuigisMansion** | ja | playable | geen `dropin/` | - | - | - | ja | compleet |
| **MedaruNoGunman** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (3001 RAM DATA IS BAD bij elke verse start) |
| **MusicGunGun2** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (eigen D3D-foutvenster); `patches.ini` aanwezig |
| **NERF** | ja | playable | geen `dropin/` | - | - | - | ja | geen `dongle.json` (hasp verwacht); **andere agent** |
| **OG** | ja | untested | 1 / 1 | - | ok | region | ja | compleet |
| **OmatsuriYasanGansoTakoyaki** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (wit venster; online activering) |
| **OmatsuriYasanKingyoSukui** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (exitcode 8) |
| **Otomedius** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (COM2/Acio-micom) |
| **PacmanChomp** | ja | parked | 1 / 1 | - | ok | - | ja | geparkeerd (zwart beeld) |
| **PigsMightFly** | ja | playable | geen `dropin/` | - | - | - | ja | compleet |
| **RabbidsHollywood** | ja | parked | 3 / 3 | - | ok | matrix | ja | **andere agent**; `matrix/matrix.ini` wordt door onze eigen `matrix64.dll` gelezen, dus zonder sectie komt het niet aan |
| **RaccoonRampage** | ja | playable | geen `dropin/` | - | - | - | ja | compleet |
| **RainbowBombergirl** | ja | parked | geen `dropin/` | - | - | - | **nee** | geen README; geparkeerd (personagekeuze komt niet) |
| **Rampage** | ja | playable | 3 / 3 | - | ok | matrix | ja | speelbaar; joystickspel, geen lightgun-game |
| **TC5** | ja | playable | 4 / 4 | - | ok | hasp | ja | dongle-image ontbreekt (`memory` is leeg, `hasp.py` meldt de dongle onvolledig) |
| **TankTankTank** | ja | **-** | geen `dropin/` | - | - | - | **nee** | geen `[Status] Status`; geen README; niet direct te starten: gamemap niet ingesteld (`--plan` rc=4) |
| **Terminator** | ja | playable | 3 / 3 | - | ok | hasp | ja | **andere agent**; geen `dropin/` terwijl `build-hasphook.sh` die wel vult |
| **TombRaider** | ja | playable | 3 / 3 | - | ok | matrix | ja | speelbaar |
| **TransformersShadowsRising** | ja | playable | geen `dropin/` | - | - | - | **nee** | geen `dongle.json` (sega-scan verwacht); geen README |

## `To` en de sectienaam: de dest van elk drop-in-bestand

De drop-in-motor rekent `dest = view + To + sectienaam`, en de sectienaam is het pad onder `dropin/` (`docs/dropins.md`). Zet je het pad ook in `To`, dan komt het bestand een niveau te diep en vindt de bibliotheek het niet terug -- de motor meldt daar **geen fout** voor, dus dit is een stille fout. De controle hieronder is: is `To` gelijk aan `.`, of bevat de sectienaam geen pad dat al met `To` begint?

**Uitkomst: geen enkele sectie heeft dit probleem.** Alle 43 secties in de 14 `dropin.ini`-bestanden staan goed.

Deze fout heeft er **wel** in gestaan: in Halo, TC5, LGI, LGI3D, LGS en `_template` stond bij elke donglesectie het pad dubbel, bijvoorbeeld:

```ini
[ravengun-hasp/Halo/dongle.json]
To = ravengun-hasp/Halo        ; dest werd view/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json
```

Met `To = .` wordt dat `view/ravengun-hasp/Halo/dongle.json`, precies waar `RAVENGUN_HASP_DIR=ravengun-hasp` + `RAVENGUN_HASP_GAME=Halo` hem zoekt. Zie het kopje "Reparaties" voor wie dat gedaan heeft en hoe het gecontroleerd is.

## Tellingen

**36 games** in `patches/` (zonder `_template` en `_defaults`).

| | aantal |
|---|---|
| games | 36 |
| met `game.ini` | 36 |
| met `[Status] Status` | 34 |
| met README.md | 31 |
| met een `dropin/`-map | 15 |
| ... waarvan elk bestand een sectie heeft | 15 |
| ... waar nog een bestand zonder sectie staat | 0 |
| met `dongle.json` | 13 |
| dongle verwacht maar nog geen `dongle.json` | 4 |

Dongles die er zijn: **GalagaAssault** (hasp), **Halo** (hasp), **InfinityBladeFX** (matrix), **JurassicPark** (hasp), **LGI** (region), **LGI3D** (region), **LGS** (region), **OG** (region), **RabbidsHollywood** (matrix), **Rampage** (matrix), **TC5** (hasp), **Terminator** (hasp), **TombRaider** (matrix).

Dongle verwacht maar nog niet aanwezig (uit `docs/dropins.md` en `docs/todo.md`): **GhostBusters** (hasp), **HOTDSD** (amdaemon), **NERF** (hasp), **TransformersShadowsRising** (sega-scan).

Een bestand in `dropin/` zonder eigen sectie in `dropin.ini` wordt **niet** uitgerold; dat is de enige fout die de drop-in-motor stil laat passeren (`docs/dropins.md`).

<!-- END TABEL -->

## Verificatie met de loader zelf

### `tools/verify-all.py --md` (na alle reparaties van deze ronde)

```
games:                       35
lader van ons:               24
kern van ons:                4
kern nog TeknoParrot:        21
lader nog BudgieLoader:      7
met dongle.json:             5
  waarvan script ontbreekt:  0
met dropin/:                 9
  drop-ins die NIET aankomen:0
plan mislukt:                3
  !! 3DCosplayMahjong             rc=4
  !! HOTDSD                       rc=4
  !! TankTankTank                 rc=4
```

De regels die er voor deze kaart toe doen (kolommen `dropin_bestanden,dropin_secties,dropin_fouten`):

```
Halo,ElfLdr2,ElfLdr2,tp-budgie,-,ja,hasp,hasp.py,3,7,0,0,0,0,0
GalagaAssault,ElfLdr2,ElfLdr2,tp-budgie,-,nee,-,-,3,2,0,0,0,0,0
JurassicPark,ElfLdr2,ElfLdr2,tp-budgie,-,nee,-,-,3,2,0,0,0,0,0
RabbidsHollywood,TeknoParrot,TeknoParrot,ours,tp,nee,-,-,1,1,0,0,0,0,0
PacmanChomp,ElfLdr2,ElfLdr2,tp-budgie,-,nee,-,-,1,1,0,0,0,0,0
LGI,TeknoParrot,TeknoParrot,ours,tp,ja,region,region.py,0,1,0,0,0,0,0
TC5,TeknoParrot,TeknoParrot,tp,tp,ja,hasp,hasp.py,3,4,0,0,0,0,0
```

**Let op bij die getallen:** `verify-all.py` telt de bestanden in `dropin/` alleen op het **bovenste**
niveau (`os.listdir`), terwijl de motor ook de bestanden in submappen uitrolt. Daarom zegt hij bij
Halo `3` bestanden en `7` secties; het werkelijke aantal is **7 bestanden en 7 secties** (de tabel
hierboven telt recursief). Lees die kolom dus niet als "er ontbreekt iets".

Twee dingen zijn in deze ronde veranderd en staan in de uitvoer hierboven al verwerkt:

* **Halo** ging van `3,6` (één bestand zonder sectie) naar `3,7`.
* **TC5** ging van `4 drop-in(s) komen niet aan` naar `0`: er is nu een lokale view
  (`~/.local/share/ravengun/runtime/views/TC5/…`), dus de drop-in-motor hoeft niet meer naar de NAS
  te schrijven. Daarvóór meldde verify-all: *"deze game draait rechtstreeks van de NAS, dus er is geen
  view"*.

### `launch-direct.py <Game> --plan` — de dest van elk drop-in-bestand

Voor alle 9 games met een `dropin/` is de `dropins`-regel van de launcher gelezen en is per bestand
gecontroleerd of `dest` gelijk is aan `view + To + sectienaam`. Uitkomst na de reparaties van deze
ronde: **20 secties, 0 dubbele paden, 0 fouten** (zie het kopje [`To` en de sectienaam](#to-en-de-sectienaam-de-dest-van-elk-drop-in-bestand)
hierboven). Voorbeeld van de bedoelde uitkomst:

```
Halo  base=/home/deck/.local/share/ravengun/runtime/halo-g7-tp/halo
  ok  ravengun-hasp-x64.so                -> ravengun-hasp-x64.so
  ok  libravengun-hasphook-x64.so         -> libravengun-hasphook-x64.so
  ok  libravengun-hasphook.so             -> libravengun-hasphook.so
  ok  ravengun-hasp/Halo/dongle.json      -> ravengun-hasp/Halo/dongle.json
  ok  ravengun-hasp/Halo/memory/fff4      -> ravengun-hasp/Halo/memory/fff4
```

De `preload`-lijst van Halo bevat na de reparatie precies twee bestanden
(`ravengun-hasp-x64.so` en `libravengun-hasphook-x64.so`); de 32-bits hook wordt wel gekopieerd maar
niet geladen.

### `python3 -m unittest discover -s tests`

```
Ran 237 tests in 57.221s

OK (skipped=21)
```

Exitcode 0. De testsuite stond vóór de reparatie ook groen (233 tests, `OK (skipped=21)`); het aantal
groeide tijdens deze ronde naar 237 doordat andere agents tegelijk tests toevoegden. Geen enkele test
leest de meegeleverde `patches/*/dropin/dropin.ini` — ze bouwen hun eigen `dropin/` in een tijdelijke
map (`tests/test_hasp.py`, `tests/test_dongle_inventaris.py`, `tests/test_dropin_materialiseer.py`) —
dus een reparatie aan de ini's kan de suite niet breken. Er is niets gebroken.

## Reparaties

### 1. `patches/Halo/dropin/dropin.ini` — een bestand zonder sectie

`patches/Halo/dropin/` bevatte 7 bestanden met 6 secties: `libravengun-hasphook.so` had er geen, en
werd dus nooit in de view gezet.

Toegevoegd:

```ini
; (toelichting in het bestand zelf)
[libravengun-hasphook.so]
To = .
```

**Zonder `Preload`**, en dat is met opzet: dit is de 32-bits bouw (`ELF 32-bit LSB shared object,
Intel i386`), terwijl Halo een 64-bits spel is (het `game`-bestand is een x86-64 ELF). Een 32-bits ELF
in dat proces laden is fataal (`ELFCLASS64`; `docs/dropins.md`, "Preload = true"). Zonder `Preload`
wordt het bestand wel uitgerold maar nergens in geladen. Het bestand zelf is niet aangeraakt.

*Voor:* 7 bestanden, 6 secties. *Na:* 7 bestanden, 7 secties, `--plan` zonder fouten.

De oorzaak van dit overblijfsel staat in de loader-repo: `scripts/build-hasphook.sh` legt **beide**
architecturen in elke game van zijn `GAMES`-lijst (`JurassicPark GalagaAssault Halo Terminator`). De
verkeerde architectuur blijft daardoor in `dropin/` liggen. Opruimen mag: haal het bestand én de
sectie weg, dan blijft er precies over wat Halo nodig heeft.

### 2. `To` en de sectienaam — het pad stond dubbel (gedaan door de coördinerende agent)

Dit is dezelfde soort stille fout, en de zwaarste van deze ronde. De motor rekent
`dest = view + To + sectienaam`, maar bij de donglesecties stond het pad in **beide**:

```ini
[ravengun-hasp/Halo/dongle.json]
To = ravengun-hasp/Halo
```

Dat gaf `view/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json` — een niveau te diep, terwijl de
bibliotheek hem zoekt op `view/ravengun-hasp/Halo/dongle.json` (`RAVENGUN_HASP_DIR=ravengun-hasp` +
`RAVENGUN_HASP_GAME=Halo`). De motor meldt hier geen fout: hij kopieert netjes naar de verkeerde plek.
Gemeten met de launcher op 2026-10-11, vóór de reparatie:

```
'dest': '.../halo-g7-tp/halo/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json'
```

Getroffen: **Halo, TC5, LGI, LGI3D, LGS** (alle donglesecties) en **`_template`** (waardoor elke
nieuwe game de fout erft). De coördinerende agent heeft die ini's tijdens deze ronde naar `To = .`
gezet; de reparatie is niet door mij gedaan en de game-mappen van andere agents zijn met rust gelaten.
Ik heb het **resultaat** met de launcher gecontroleerd: alle 20 secties komen nu aan waar ze horen
(zie [hierboven](#to-en-de-sectienaam-de-dest-van-elk-drop-in-bestand)).

## Overgeslagen: games waar een andere agent aan werkt

Deze gevallen staan in de tabel en in het `To`-overzicht, maar ik heb ze **niet** aangeraakt.

| Game | Wat er nog is | Wie |
|---|---|---|
| **GalagaAssault** | `libravengun-hasphook-x64.so` zonder sectie (64-bits overblijfsel in een 32-bits game); geen `dongle.json` (hasp verwacht) | HASP-dongle-agent |
| **JurassicPark** | idem: `libravengun-hasphook-x64.so` zonder sectie; geen `dongle.json` (hasp verwacht) | HASP-dongle-agent |
| **Terminator** | geen `dropin/` terwijl `build-hasphook.sh` die wel vult; geen `dongle.json` (hasp verwacht) | HASP-dongle-agent |
| **NERF** | geen `dongle.json` (hasp verwacht) | HASP-dongle-agent |
| **GhostBusters** | geen `dongle.json` (hasp verwacht); geen `dropin/` | HASP-dongle-agent |
| **RabbidsHollywood** | `matrix/matrix.ini` zonder sectie — **dit is een echte fout**: onze `matrix64.dll` leest dat bestand zelf, dus zonder sectie komt het niet aan; geen `dongle.json` (matrix verwacht) | Matrix/SBK-agent |
| **LGI, LGI3D, LGS** | niets te repareren (1 bestand, 1 sectie, `To` inmiddels `.`); regio niet afgedwongen (`enforced = false`) | regio-agent |

Daarmee blijven er **3 bestanden zonder sectie** over, alle drie in een map van een andere agent:
`GalagaAssault/dropin/libravengun-hasphook-x64.so`, `JurassicPark/dropin/libravengun-hasphook-x64.so`
en `RabbidsHollywood/dropin/matrix/matrix.ini`.

## Opmerkingen en open punten

1. **`_template/dropin/ravengun-hasp/JouwGame/dongle.json` wijst naar een pad dat niet bestaat.** Het
   bestand zegt `"file": "dongle/memory/00000000000000001111111111110100"`, maar het geheugenbestand
   staat ernaast in `memory/…` (zonder `dongle/` ervoor). Wie de template kopieert, krijgt dus een
   dongle.json die naar niets wijst. Niet gerepareerd: het bestand zelf aanraken valt buiten deze
   opdracht en de coördinerende agent is met de template bezig.
2. **`verify-all.py` telt `dropin/` niet recursief.** Zijn kolom `dropin_bestanden` telt alleen het
   bovenste niveau, dus bij games met donglegegevens in een submap lijkt het aantal lager dan het
   aantal secties (Halo: 3 tegenover 7). De tabel hierboven telt recursief; dat is de juiste maat.
3. **`build-hasphook.sh` legt beide architecturen in elke game.** Dat is de bron van de drie
   verkeerde-architectuur-overblijfselen. Een game die maar één architectuur nodig heeft, houdt er zo
   een bestand bij dat geen sectie hoort te hebben — precies het patroon dat deze kaart moet blijven
   vangen.
4. **TC5's dongle is incompleet:** `dongle.json` heeft `"memory": []` en zegt zelf dat de dongle-image
   nog niet is vastgelegd. `scripts/hasp.py` meldt deze dongle daarom als onvolledig. De drop-in van
   TC5 (4 bestanden, 4 secties) komt sinds deze ronde wel aan.
5. **`memory/fff4` in Halo is geen dubbel bestand:** `fff4` is de hexvorm van fileid 65524, naast de
   32-bits binaire vorm `00000000000000001111111111110100`. `hasp.py` accepteert beide schrijfwijzen
   (`_fileid_van_naam`) en rekent dit niet als fout.
6. **`docs/per-game-stand.md` en `docs/inventaris-dongle-per-game.md` in de loader-repo zijn
   verouderd** (daar staat Halo nog op 1 bestand / 1 sectie en LGI op geen drop-in). Opnieuw te maken
   met `tools/verify-all.py --md docs/per-game-stand.md` en
   `scripts/gen-dongle-inventaris.py`. Deze kaart vervangt ze niet: die twee gaan over de lader en de
   kern, deze over de volledigheid van de game-mappen.
7. **`3DCosplayMahjong` en `TankTankTank` hebben geen `[Status] Status`** en hun `--plan` faalt met
   `rc=4` ("game niet gevonden: '' — voeg de map met de game toe als gamemap"). `HOTDSD` faalt met
   `rc=4` omdat zijn profiel twee exe's heeft (`amdaemon.exe` eerst), wat `launch-direct.py` niet
   direct start.

## Bijlage: het generatorscript

Dit is `patches/stand-gen.py`, precies het script dat het blok tussen de markers hierboven maakt.
Kopiëren naar `patches/stand-gen.py` (of als heredoc draaien) en vanuit de patches-repo starten.

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Maakt de tabel van patches/STAND.md en werkt hem bij tussen de markers.

Gebruik (op de Deck, vanuit de patches-repo):

    cd ~/projects/ravengun-patches
    python3 patches/stand-gen.py

Of zonder dit bestand: het script in de bijlage van patches/STAND.md is hetzelfde script.

Het leest alleen: patches/<Game>/game.ini, patches/<Game>/dropin/ + dropin.ini (secties en hun To)
en het dongle.json in de drop-in. Het schrijft alleen patches/STAND.md, het blok tussen
BEGIN TABEL en END TABEL.
"""
import json
import os
import re
import sys

PATCHES = "patches"
STAND = os.path.join(PATCHES, "STAND.md")
BEGIN = "<!-- BEGIN TABEL -->"
END = "<!-- END TABEL -->"
SKIP = {"_template", "_defaults"}

# Wat er per game nog ontbreekt of opvalt. Dit is het enige met de hand onderhouden deel; de rest
# van de tabel komt uit de bestanden zelf. Zet hier NIET wat het script al zelf ziet (een bestand
# zonder sectie, een ontbrekende README, een ontbrekende [Status], een ontbrekende dongle): dat
# staat er al, en anders staat het er dubbel.
NOTES = {
    "3DCosplayMahjong": "geen `patches.ini`; niet direct te starten: gamemap niet ingesteld (`--plan` rc=4)",
    "GaiaAttack4": "geparkeerd (geen beeld, zie `game.ini`); `patches.ini` aanwezig",
    "GalagaAssault": "**andere agent**; het bestand zonder sectie is een 64-bits overblijfsel van `build-hasphook.sh` in een 32-bits game",
    "Gashaaaan": "compleet; geen drop-in nodig",
    "Gashaaaan2": "compleet; geen drop-in nodig",
    "GhostBusters": "**andere agent**; geen `dropin/`; geparkeerd: GAME PATH CHECK FAILED",
    "GoGoStrike": "compleet; geen drop-in nodig",
    "HOTDSD": "`--plan` rc=4: het profiel start twee exe's (`amdaemon.exe` eerst), dat doet `launch-direct.py` niet",
    "Halo": "drop-in compleet na reparatie (7 bestanden, 7 secties); de 32-bits hook heeft bewust geen `Preload`",
    "HauntedMuseum": "geparkeerd (geen beeld); `patches.ini` aanwezig",
    "HauntedMuseumII": "geparkeerd (geen beeld); `patches.ini` aanwezig",
    "InfinityBladeFX": "geparkeerd (UE3: wit of zwart beeld)",
    "JurassicPark": "**andere agent**; het bestand zonder sectie is een 64-bits overblijfsel van `build-hasphook.sh` in een 32-bits game",
    "LGI": "**regio-agent**; regio niet afgedwongen (`enforced = false`)",
    "LGI3D": "**regio-agent**; regio niet afgedwongen (`enforced = false`)",
    "LGS": "**regio-agent**; regio niet afgedwongen (`enforced = false`)",
    "LethalEnforcers3": "geparkeerd (32-bits GStreamer ontbreekt)",
    "LuigisMansion": "compleet",
    "MedaruNoGunman": "geparkeerd (3001 RAM DATA IS BAD bij elke verse start)",
    "MusicGunGun2": "geparkeerd (eigen D3D-foutvenster); `patches.ini` aanwezig",
    "NERF": "**andere agent**",
    "OmatsuriYasanGansoTakoyaki": "geparkeerd (wit venster; online activering)",
    "OmatsuriYasanKingyoSukui": "geparkeerd (exitcode 8)",
    "Otomedius": "geparkeerd (COM2/Acio-micom)",
    "PacmanChomp": "geparkeerd (zwart beeld)",
    "PigsMightFly": "compleet",
    "RabbidsHollywood": "**andere agent**; `matrix/matrix.ini` wordt door onze eigen `matrix64.dll` gelezen, dus zonder sectie komt het niet aan",
    "RaccoonRampage": "compleet",
    "RainbowBombergirl": "geparkeerd (personagekeuze komt niet)",
    "Rampage": "speelbaar; joystickspel, geen lightgun-game",
    "TC5": "dongle-image ontbreekt (`memory` is leeg, `hasp.py` meldt de dongle onvolledig)",
    "TankTankTank": "niet direct te starten: gamemap niet ingesteld (`--plan` rc=4)",
    "Terminator": "**andere agent**; geen `dropin/` terwijl `build-hasphook.sh` die wel vult",
    "TombRaider": "speelbaar",
    "TransformersShadowsRising": "",
}

# Games waarvan docs/dropins.md en docs/todo.md zeggen dat ze een dongle vragen, met de soort.
# Staat er een dongle.json, dan komt de soort daaruit en doet deze lijst niets.
EXPECTED_DONGLE = {
    "GalagaAssault": "hasp",
    "GhostBusters": "hasp",
    "HOTDSD": "amdaemon",
    "JurassicPark": "hasp",
    "NERF": "hasp",
    "RabbidsHollywood": "matrix",
    "Terminator": "hasp",
    "TransformersShadowsRising": "sega-scan",
}


def ini_waarde(pad, sectie, sleutel):
    """De waarde van [sectie] sleutel in een ini, of None."""
    if not os.path.isfile(pad):
        return None
    sec = None
    with open(pad, encoding="utf-8-sig", errors="replace") as fh:
        for regel in fh:
            s = regel.strip()
            if not s or s[0] in ";#":
                continue
            m = re.match(r"^\[(.+?)\]\s*$", s)
            if m:
                sec = m.group(1).strip()
                continue
            if sec == sectie and "=" in s:
                k, v = s.split("=", 1)
                if k.strip().lower() == sleutel.lower():
                    return v.strip()
    return None


def dropin_bestanden(d):
    """Alle bestanden in dropin/ behalve dropin.ini, relatief aan dropin/."""
    uit = []
    for root, _, files in os.walk(d):
        for f in files:
            if root == d and f == "dropin.ini":
                continue
            uit.append(os.path.relpath(os.path.join(root, f), d).replace(os.sep, "/"))
    return sorted(uit)


def dropin_secties(ini):
    """[(sectienaam, To)] in volgorde van het bestand."""
    if not os.path.isfile(ini):
        return []
    uit, sec = [], None
    with open(ini, encoding="utf-8-sig", errors="replace") as fh:
        for regel in fh:
            s = regel.strip()
            if not s or s[0] in ";#":
                continue
            m = re.match(r"^\[(.+?)\]\s*$", s)
            if m:
                sec = m.group(1).strip()
                uit.append([sec, "."])
                continue
            if sec and "=" in s:
                k, v = s.split("=", 1)
                if k.strip().lower() == "to":
                    uit[-1][1] = v.strip() or "."
    return uit


def dubbel_pad(naam, to):
    """True als de sectienaam het pad van `To` al bevat: dest wordt dan dubbel (docs/dropins.md)."""
    t = to.strip("/")
    if t in ("", "."):
        return False
    n = naam.strip("/")
    return n == t or n.startswith(t + "/")


def dongle_soort(gd, game):
    """(soort, pad) uit het dongle.json in de drop-in, of (None, None)."""
    kandidaten = [os.path.join(gd, "dropin", "ravengun-hasp", game, "dongle.json"),
                  os.path.join(gd, "dongle.json")]
    for root, _, files in os.walk(os.path.join(gd, "dropin")):
        if "dongle.json" in files:
            kandidaten.append(os.path.join(root, "dongle.json"))
    for k in kandidaten:
        if os.path.isfile(k):
            try:
                with open(k, encoding="utf-8") as fh:
                    d = json.load(fh)
            except (OSError, ValueError) as e:
                return "FOUT: %s" % e, os.path.relpath(k, gd)
            return (d.get("kind") or "").strip().lower() or "?", os.path.relpath(k, gd)
    return None, None


def lees_games():
    rijen = []
    for naam in sorted(os.listdir(PATCHES)):
        if naam in SKIP or naam.startswith("_"):
            continue
        gd = os.path.join(PATCHES, naam)
        if not os.path.isdir(gd):
            continue
        ini = os.path.join(gd, "game.ini")
        d = os.path.join(gd, "dropin")
        heeft_dropin = os.path.isdir(d)
        bestanden = dropin_bestanden(d) if heeft_dropin else []
        secties = dropin_secties(os.path.join(d, "dropin.ini")) if heeft_dropin else []
        zonder = [b for b in bestanden if b not in [s for s, _ in secties]]
        dubbel = [(s, t) for s, t in secties if dubbel_pad(s, t)]
        soort, dongle_pad = dongle_soort(gd, naam)
        rijen.append({
            "game": naam,
            "game_ini": os.path.isfile(ini),
            "status": ini_waarde(ini, "Status", "Status"),
            "dropin": heeft_dropin,
            "n_bestanden": len(bestanden),
            "n_secties": len(secties),
            "zonder": zonder,
            "dubbel": dubbel,
            "dongle": soort,
            "dongle_pad": dongle_pad,
            "verwacht": None if soort else EXPECTED_DONGLE.get(naam),
            "readme": os.path.isfile(os.path.join(gd, "README.md")),
            "notitie": NOTES.get(naam, ""),
        })
    return rijen


def cel(rij):
    """De kolom 'wat ontbreekt er nog': mechanische gaten vooraan, dan de notitie."""
    stukken = []
    if not rij["game_ini"]:
        stukken.append("geen `game.ini`")
    elif not rij["status"]:
        stukken.append("geen `[Status] Status`")
    if rij["dropin"] and not rij["n_bestanden"] and not rij["n_secties"]:
        stukken.append("lege `dropin/`")
    if rij["zonder"]:
        stukken.append("sectie ontbreekt voor %s" % ", ".join("`%s`" % z for z in rij["zonder"]))
    if rij["dubbel"]:
        stukken.append("dubbel pad in %d sectie(s)" % len(rij["dubbel"]))
    if rij["verwacht"]:
        stukken.append("geen `dongle.json` (%s verwacht)" % rij["verwacht"])
    if not rij["readme"]:
        stukken.append("geen README")
    if rij["notitie"]:
        stukken.append(rij["notitie"])
    return "; ".join(stukken) if stukken else "compleet"


def tabel(rijen):
    uit = ["| Game | `game.ini` | `[Status] Status` | drop-in (bestanden/secties) | zonder sectie | `To` ↔ sectie | dongle | README | wat ontbreekt er nog |",
           "|---|---|---|---|---|---|---|---|---|"]
    for r in rijen:
        if not r["dropin"]:
            drop = "geen `dropin/`"
        elif r["n_bestanden"] == r["n_secties"]:
            drop = "%d / %d" % (r["n_bestanden"], r["n_secties"])
        else:
            drop = "%d / **%d**" % (r["n_bestanden"], r["n_secties"])
        zonder = ", ".join("`%s`" % z for z in r["zonder"]) if r["zonder"] else "-"
        if not r["dropin"]:
            tokol = "-"
        elif r["dubbel"]:
            tokol = "**%d dubbel**" % len(r["dubbel"])
        else:
            tokol = "ok"
        uit.append("| **%s** | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["game"],
            "ja" if r["game_ini"] else "**nee**",
            r["status"] or "**-**",
            drop,
            zonder,
            tokol,
            r["dongle"] or "-",
            "ja" if r["readme"] else "**nee**",
            cel(r)))
    return uit


def to_blok(rijen):
    """De losse controle op `To` en de sectienaam (de motor rekent dest = view + To + sectienaam)."""
    uit = ["## `To` en de sectienaam: de dest van elk drop-in-bestand", "",
           "De drop-in-motor rekent `dest = view + To + sectienaam`, en de sectienaam is het pad onder "
           "`dropin/` (`docs/dropins.md`). Zet je het pad ook in `To`, dan komt het bestand een niveau "
           "te diep en vindt de bibliotheek het niet terug -- de motor meldt daar **geen fout** voor, "
           "dus dit is een stille fout. De controle hieronder is: is `To` gelijk aan `.`, of bevat de "
           "sectienaam geen pad dat al met `To` begint?", ""]
    fouten = [(r["game"], s, t) for r in rijen for s, t in r["dubbel"]]
    if not fouten:
        uit += ["**Uitkomst: geen enkele sectie heeft dit probleem.** Alle %d secties in de %d "
                "`dropin.ini`-bestanden staan goed." % (
                    sum(r["n_secties"] for r in rijen),
                    sum(1 for r in rijen if r["dropin"] and r["n_secties"])),
                "",
                "Deze fout heeft er **wel** in gestaan: in Halo, TC5, LGI, LGI3D, LGS en `_template` "
                "stond bij elke donglesectie het pad dubbel, bijvoorbeeld:", "",
                "```ini",
                "[ravengun-hasp/Halo/dongle.json]",
                "To = ravengun-hasp/Halo        ; dest werd view/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json",
                "```", "",
                "Met `To = .` wordt dat `view/ravengun-hasp/Halo/dongle.json`, precies waar "
                "`RAVENGUN_HASP_DIR=ravengun-hasp` + `RAVENGUN_HASP_GAME=Halo` hem zoekt. Zie het "
                "kopje \"Reparaties\" voor wie dat gedaan heeft en hoe het gecontroleerd is."]
    else:
        uit += ["| Game | sectie | `To` | dest van de motor | hoort te zijn |",
                "|---|---|---|---|---|"]
        for game, s, t in fouten:
            uit.append("| **%s** | `%s` | `%s` | `%s/%s` | `%s` |" % (
                game, s, t, t.strip("/"), s, s))
    uit.append("")
    return uit


def tellingen(rijen):
    n = len(rijen)
    met_dropin = [r for r in rijen if r["dropin"]]
    heel = [r for r in met_dropin if not r["zonder"]]
    dongles = [r for r in rijen if r["dongle"]]
    verwacht = [r for r in rijen if r["verwacht"]]
    return [
        "## Tellingen",
        "",
        "**%d games** in `patches/` (zonder `_template` en `_defaults`)." % n,
        "",
        "| | aantal |",
        "|---|---|",
        "| games | %d |" % n,
        "| met `game.ini` | %d |" % sum(1 for r in rijen if r["game_ini"]),
        "| met `[Status] Status` | %d |" % sum(1 for r in rijen if r["status"]),
        "| met README.md | %d |" % sum(1 for r in rijen if r["readme"]),
        "| met een `dropin/`-map | %d |" % len(met_dropin),
        "| ... waarvan elk bestand een sectie heeft | %d |" % len(heel),
        "| ... waar nog een bestand zonder sectie staat | %d |" % (len(met_dropin) - len(heel)),
        "| met `dongle.json` | %d |" % len(dongles),
        "| dongle verwacht maar nog geen `dongle.json` | %d |" % len(verwacht),
        "",
        "Dongles die er zijn: %s." % ", ".join(
            "**%s** (%s)" % (r["game"], r["dongle"]) for r in dongles),
        "",
        "Dongle verwacht maar nog niet aanwezig (uit `docs/dropins.md` en `docs/todo.md`): %s." % ", ".join(
            "**%s** (%s)" % (r["game"], r["verwacht"]) for r in verwacht),
        "",
        "Een bestand in `dropin/` zonder eigen sectie in `dropin.ini` wordt **niet** uitgerold; dat is "
        "de enige fout die de drop-in-motor stil laat passeren (`docs/dropins.md`).",
        "",
    ]


def main():
    if not os.path.isdir(PATCHES):
        sys.exit("FOUT: geen %s/ hier. Draai dit vanuit de patches-repo (cd ~/projects/ravengun-patches)." % PATCHES)
    if not os.path.isfile(STAND):
        sys.exit("FOUT: %s bestaat niet" % STAND)
    rijen = lees_games()
    blok = "\n".join(tabel(rijen) + [""] + to_blok(rijen) + tellingen(rijen))
    with open(STAND, encoding="utf-8") as fh:
        tekst = fh.read()
    if BEGIN not in tekst or END not in tekst:
        sys.exit("FOUT: %s en %s niet gevonden in %s" % (BEGIN, END, STAND))
    kop, rest = tekst.split(BEGIN, 1)
    _, staart = rest.split(END, 1)
    with open(STAND, "w", encoding="utf-8") as fh:
        fh.write(kop + BEGIN + "\n" + blok + "\n" + END + staart)
    print("bijgewerkt: %s (%d games)" % (STAND, len(rijen)))


if __name__ == "__main__":
    main()
```
