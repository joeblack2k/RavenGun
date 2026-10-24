# Stand van de patches — volledigheidskaart

*Wat dit bestand is:* één tabel over alle games in `patches/` met wat er per game is en wat er nog
ontbreekt: `game.ini` en `[Status]`, de drop-in (hoeveel bestanden en hoeveel daarvan een sectie in
`dropin.ini` hebben), de dongle uit `dongle.json`, de README, en de `To` van elke sectie.

*Wat het contract is:* alles wat één game nodig heeft staat in `patches/<Game>/`
(`docs/dropins.md` in de loader-repo). Elk bestand in `dropin/` moet een **eigen sectie** in
`dropin.ini` hebben, anders wordt het niet uitgerold — dat is de fout die het vaakst misgaat en die
de motor **niet** meldt. De dongle is geen loaderbegrip: het is een gewone drop-in met zijn gegevens
ernaast (`docs/geen-dongle-in-de-loader.md`).

**Alles tussen de markers hieronder is gegenereerd; alles erbuiten is met de hand geschreven en moet
je zelf bijhouden.** De kaarten die een ander onderwerp beter weten: `PATCHES-PER-GAME.md` (welke
patch per game nodig is), `REGIO-PER-GAME.md`, `EIGEN-KERN.md`, `ZONDER-LADER.md`, `ELF-NATIVE.md` en
`STARTTEST.md`.

## Hoe deze kaart is gemaakt

```bash
cd ~/projects/ravengun-patches
python3 patches/stand-gen.py          # vult het blok tussen BEGIN TABEL en END TABEL hieronder
```

`patches/stand-gen.py` leest alleen `game.ini`, `dropin/` + `dropin.ini` en het `dongle.json` per
game, telt de bestanden in `dropin/` **recursief** (ook `ravengun-hasp/<Game>/memory/…`) en schrijft
alleen dit bestand. Het script staat in deze map en is **niet** in dit bestand gekopieerd: een kopie
loopt achter zodra iemand het script aanpast.

Wat er daarnaast met de loader zelf te controleren is:

```bash
cd ~/projects/ravengun
python3 tools/verify-all.py --md /tmp/per-game-stand.md   # lader, kern, dongle, drop-in, patches
python3 scripts/launch-direct.py <Game> --plan            # de echte dest van elk drop-in-bestand
python3 -m unittest discover -s tests 2>&1 | tail -5
```

**Let op bij `verify-all.py`:** dat telt de bestanden in `dropin/` alleen op het **bovenste** niveau,
terwijl de motor ook de bestanden in submappen uitrolt. De tabel hieronder telt recursief; die twee
getallen verschillen dus, en dat betekent niet dat er iets ontbreekt. `patches/dropin-aankomst.py`
controleert het echte antwoord: staat er op de bestemming in de view een bestand met dezelfde sha256?

<!-- BEGIN TABEL -->
| Game | `game.ini` | `[Status] Status` | drop-in (bestanden/secties) | zonder sectie | `To` ↔ sectie | dongle | README | wat ontbreekt er nog |
|---|---|---|---|---|---|---|---|---|
| **3DCosplayMahjong** | ja | untested | geen `dropin/` | - | - | - | ja | geen `patches.ini`; niet direct te starten: het spel staat niet op de NAS (`--plan` rc=4) |
| **GaiaAttack4** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld, zie `game.ini`); `patches.ini` aanwezig |
| **GalagaAssault** | ja | parked | 6 / 6 | - | ok | hasp | ja | het 64-bits `libravengun-hasphook-x64.so` heeft een eigen sectie en wordt dus uitgerold; niets laadt het in deze 32-bits game (zie punt 1 onderaan) |
| **Gashaaaan** | ja | playable | geen `dropin/` | - | - | - | ja | compleet; geen drop-in nodig |
| **Gashaaaan2** | ja | playable | geen `dropin/` | - | - | - | ja | compleet; geen drop-in nodig |
| **GhostBusters** | ja | parked | 0 / 0 | - | ok | - | ja | lege `dropin/`; geen `dongle.json` (hasp verwacht); lege `dropin/`; geen `dongle.json` (hasp verwacht); GAME PATH CHECK FAILED is met `[Identify] Path = pm/game` opgelost, daarna komt de game niet in beeld |
| **GoGoStrike** | ja | playable | geen `dropin/` | - | - | - | ja | geen drop-in; vraagt de eigen `LD_PRELOAD`-kastnabootser voor de ElfLdr2-familie, die nog niet bestaat |
| **HOTDSD** | ja | playable | geen `dropin/` | - | - | - | ja | geen `dongle.json` (amdaemon verwacht); `--plan` rc=4: het profiel start twee exe's (`amdaemon.exe` eerst), dat doet `launch-direct.py` niet |
| **Halo** | ja | playable | 7 / 7 | - | ok | hasp | ja | drop-in compleet na reparatie (7 bestanden, 7 secties); de 32-bits hook heeft bewust geen `Preload` |
| **HauntedMuseum** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld); `patches.ini` aanwezig |
| **HauntedMuseumII** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (geen beeld); `patches.ini` aanwezig |
| **InfinityBladeFX** | ja | parked | 3 / 3 | - | ok | matrix | ja | geparkeerd (UE3: wit of zwart beeld) |
| **JurassicPark** | ja | parked | 6 / 6 | - | ok | hasp | ja | het 64-bits `libravengun-hasphook-x64.so` heeft een eigen sectie en wordt dus uitgerold; niets laadt het in deze 32-bits game (zie punt 1 onderaan) |
| **LGI** | ja | playable | 1 / 1 | - | ok | region | ja | regio niet afgedwongen (`enforced = false`, gemeten: de game speelt) |
| **LGI3D** | ja | playable | 1 / 1 | - | ok | region | ja | regio niet afgedwongen (`enforced = false`, gemeten: de game speelt) |
| **LGS** | ja | playable | 1 / 1 | - | ok | region | ja | regio niet afgedwongen (`enforced = false`, gemeten: de game speelt) |
| **LethalEnforcers3** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (32-bits GStreamer ontbreekt) |
| **LuigisMansion** | ja | playable | geen `dropin/` | - | - | - | ja | map compleet; de game vraagt nog een eigen kern (gepakte exe, COM1-blokkade) |
| **MedaruNoGunman** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (3001 RAM DATA IS BAD bij elke verse start) |
| **MusicGunGun2** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (eigen D3D-foutvenster); `patches.ini` aanwezig |
| **NERF** | ja | playable | geen `dropin/` | - | - | - | ja | geen `dongle.json` nodig: de dongle zit in managed code en TeknoParrot antwoordt daar met de BepInEx-plugin `NERFTPPlugin.dll` |
| **OG** | ja | untested | 1 / 1 | - | ok | region | ja | compleet |
| **OmatsuriYasanGansoTakoyaki** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (wit venster; online activering) |
| **OmatsuriYasanKingyoSukui** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (exitcode 8) |
| **Otomedius** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (COM2/Acio-micom) |
| **PacmanChomp** | ja | parked | 1 / 1 | - | ok | - | ja | geparkeerd (zwart beeld) |
| **PigsMightFly** | ja | playable | geen `dropin/` | - | - | - | ja | compleet |
| **RabbidsHollywood** | ja | parked | 3 / 3 | - | ok | matrix | ja | `matrix/matrix.ini` heeft een eigen sectie; onze `matrix64.dll` leest dat bestand zelf, dus zonder sectie zou het niet aankomen |
| **RaccoonRampage** | ja | playable | geen `dropin/` | - | - | - | ja | compleet |
| **RainbowBombergirl** | ja | parked | geen `dropin/` | - | - | - | ja | geparkeerd (personagekeuze komt niet) |
| **Rampage** | ja | playable | 3 / 3 | - | ok | matrix | ja | speelbaar; joystickspel, geen lightgun-game |
| **TC5** | ja | playable | 4 / 4 | - | ok | hasp | ja | dongle-image ontbreekt (`memory` is leeg, `hasp.py` meldt de dongle onvolledig) |
| **TankTankTank** | ja | untested | geen `dropin/` | - | - | - | ja | niet direct te starten: het spel staat niet op de NAS (`--plan` rc=4) |
| **Terminator** | ja | playable | 3 / 3 | - | ok | hasp | ja | drop-in met 3 bestanden, maar inert: `game` heeft 0 `hasp_`-symbolen in zijn dynsym, dus er valt niets aan te haken |
| **TombRaider** | ja | playable | 3 / 3 | - | ok | matrix | ja | speelbaar |
| **TransformersShadowsRising** | ja | playable | geen `dropin/` | - | - | - | ja | geen `dongle.json` nodig: de DK2-scan faalt in fase 1 en de game speelt door |

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
| met `[Status] Status` | 36 |
| met README.md | 36 |
| met een `dropin/`-map | 15 |
| ... waarvan elk bestand een sectie heeft | 15 |
| ... waar nog een bestand zonder sectie staat | 0 |
| met `dongle.json` | 13 |
| dongle verwacht maar nog geen `dongle.json` | 2 |
| dongle gevraagd, maar geen `dropin/` nodig (met reden) | 2 |

Dongles die er zijn: **GalagaAssault** (hasp), **Halo** (hasp), **InfinityBladeFX** (matrix), **JurassicPark** (hasp), **LGI** (region), **LGI3D** (region), **LGS** (region), **OG** (region), **RabbidsHollywood** (matrix), **Rampage** (matrix), **TC5** (hasp), **Terminator** (hasp), **TombRaider** (matrix).

Dongle verwacht maar nog niet aanwezig (uit `docs/dropins.md` en `docs/todo.md`): **GhostBusters** (hasp), **HOTDSD** (amdaemon).

**Geen `dongle.json` nodig, met een reden:** **NERF** (de dongle zit in managed code (`Assembly-CSharp.dll`, `UDongleControl` roept `hasp_login` aan) en TeknoParrot antwoordt daar met de BepInEx-plugin `BepInEx/plugins/NERFTPPlugin.dll`; er is geen statisch gelinkte HASP-client in het Windows-spel om aan te haken); **TransformersShadowsRising** (de DK2-scan (`MainThreadDongleScan` in `Shell.exe`) faalt in fase 1 en de game speelt door (`Cabinet SKU set to UNKNOWN`), dus er is niets te emuleren; voor het soort `sega-scan` bestaat nog geen laag).

Een bestand in `dropin/` zonder eigen sectie in `dropin.ini` wordt **niet** uitgerold; dat is de enige fout die de drop-in-motor stil laat passeren (`docs/dropins.md`).

<!-- END TABEL -->

## Opmerkingen en open punten

1. **`build-hasphook.sh` legt beide architecturen in elke game.** Daardoor staat
   `libravengun-hasphook-x64.so` ook in de drop-in van de 32-bits games (GalagaAssault, JurassicPark).
   Sinds die bestanden een eigen sectie hebben, worden ze ook uitgerold; kwaad kan dat niet — niets
   laadt ze, ze staan niet in de `Preload`-lijst — maar het is ruis. Wie dat opruimt, doet dat in het
   bouwscript van de loader, niet per game.
2. **`memory/fff4` in Halo is geen dubbel bestand:** `fff4` is de hexvorm van fileid 65524, naast de
   32-bits binaire vorm `00000000000000001111111111110100`. `hasp.py` accepteert beide schrijfwijzen
   (`_fileid_van_naam`) en rekent dit niet als fout.
3. **Drie games zijn niet met de directe start te plannen** (`--plan` rc=4): **3DCosplayMahjong** en
   **TankTankTank** staan niet op de NAS (`[Status] = untested`: geen gamemap, geen bestandsevidence
   en dus geen startmeting), en **HOTDSD** heeft twee exe's (`amdaemon.exe` eerst), wat
   `launch-direct.py` niet doet — die game is via TeknoParrotUi wél speelbaar.
4. **De dongle van TC5 heeft nog geen geheugenimage** (`"memory": []`). Onze DLL komt wél in het
   spelproces (gemeten: het bestand staat in `/proc/<pid>/maps` met de sha256 van onze drop-in), maar
   de game roept hem niet aan — de gesloten kern handelt de dongle af voordat wij aan de beurt komen.
   Zie `patches/TC5/README.md` en `patches/PATCHES-PER-GAME.md`.
5. **Wat een andere kaart beter weet dan deze:** of een game een regio/printplaat-dongle vraagt
   (`REGIO-PER-GAME.md`), welke patch nodig is en wat een start zonder TeknoParrot blokkeert
   (`PATCHES-PER-GAME.md`), of onze eigen kern hem herkent en welk profiel er nog moet komen
   (`EIGEN-KERN.md`), of hij zonder enige lader start (`ZONDER-LADER.md`), en of de ELF van een
   ElfLdr2-game los te starten is (`ELF-NATIVE.md`).
