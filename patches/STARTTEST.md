# Starttest per game

Elke game is met de eigen lader gestart, 30 seconden laten draaien, en daarna opgeruimd. Gemaakt op 2026-10-11 22:34 uit de ruwe uitvoer van `starttest-batch.sh`.

```bash
# één game
bash /home/deck/starttest-batch.sh 30 <Game>
# alle games (elke run schrijft zijn eigen /tmp/rg-starttest-resultaat-<pid>.txt)
bash /home/deck/starttest-batch.sh 30
python3 patches/starttest-gen.py
```

**START** = de lader laadde de game (`Have fun :)` of `Loading game...`) en het proces leefde na 30 seconden nog. **viel-uit** = de game begon te laden maar het proces was eerder weg. **start-niet** = de game is nooit begonnen; de laatste logregel zegt waarom.

| Game | Resultaat | Laatste logregel |
|---|---|---|
| 3DCosplayMahjong | start-niet | Niet direct te starten: game niet gevonden: '' (voeg de map met de gam |
| GaiaAttack4 | START | [launch-direct 22:29:55] lader gestopt (exitcode 0); de game volgen |
| GalagaAssault | start-niet | [launch-direct 22:30:37] klaar (exitcode gestopt) |
| Gashaaaan | START | Have fun :) |
| Gashaaaan2 | START | - |
| GhostBusters | start-niet | [launch-direct 22:32:17] klaar (exitcode gestopt) |
| GoGoStrike | start-niet | [gamescopereaper] [[0;34mInfo[0m]  [0;37mreaper:[0m Parent of game |
| HOTDSD | start-niet | Niet direct te starten: profielen met twee exe's worden niet direct ge |
| Halo | start-niet | Cpmgui end 3472 |
| HauntedMuseum | START | [launch-direct 22:33:28] JVS-pipe: verbinding dicht (44 bytes van de g |
| HauntedMuseumII | START | [launch-direct 22:34:01] JVS-pipe: verbinding dicht (37 bytes van de g |
| InfinityBladeFX | START | Have fun :) |
| JurassicPark | start-niet | [launch-direct 22:17:04] klaar (exitcode -11) |
| LGI | START | [launch-direct 22:17:38] JVS-pipe: verbinding dicht (26 bytes van de g |
| LGI3D | START | [launch-direct 22:18:11] JVS-pipe: verbinding dicht (26 bytes van de g |
| LGS | START | [launch-direct 22:18:44] JVS-pipe: verbinding dicht (26 bytes van de g |
| LethalEnforcers3 | START | Have fun :) |
| LuigisMansion | START | Have fun :) |
| MedaruNoGunman | START | Have fun :) |
| MusicGunGun2 | START | [launch-direct 22:20:56] lader gestopt (exitcode 0); de game volgen |
| NERF | START | Have fun :) |
| OmatsuriYasanGansoTakoyaki | START | Have fun :) |
| OmatsuriYasanKingyoSukui | START | - |
| Otomedius | viel-uit | - |
| PacmanChomp | start-niet | [launch-direct 22:23:36] klaar (exitcode gestopt) |
| PigsMightFly | START | Have fun :) |
| RabbidsHollywood | START | Have fun :) |
| RaccoonRampage | START | - |
| RainbowBombergirl | START | Have fun :) |
| Rampage | START | - |
| TC5 | START | Have fun :) |
| TankTankTank | start-niet | Niet direct te starten: game niet gevonden: '' (voeg de map met de gam |
| Terminator | start-niet | [launch-direct 22:27:34] klaar (exitcode gestopt) |
| TombRaider | START | Have fun :) |
| TransformersShadowsRising | START | Have fun :) |

## Tellingen

| | aantal |
|---|---|
| getest | 35 |
| START | 24 |
| viel-uit | 1 |
| start-niet | 10 |

**START:** GaiaAttack4, Gashaaaan, Gashaaaan2, HauntedMuseum, HauntedMuseumII, InfinityBladeFX, LGI, LGI3D, LGS, LethalEnforcers3, LuigisMansion, MedaruNoGunman, MusicGunGun2, NERF, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, PigsMightFly, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Rampage, TC5, TombRaider, TransformersShadowsRising

**viel-uit:** Otomedius

**start-niet:** 3DCosplayMahjong, GalagaAssault, GhostBusters, GoGoStrike, HOTDSD, Halo, JurassicPark, PacmanChomp, TankTankTank, Terminator

## Wat een meting niet zegt

Dit zegt dat de game **start en blijft draaien**, niet dat hij speelbaar is: of het beeld goed is, of de ingang werkt en of de dongle wordt beantwoord staat er niet in. Voor de games met een dongle is de donglecontrole een aparte meting (`scripts/hasp.py --game <Game> --json` voor de gegevens, en het logboek van de start voor de vraag of de bibliotheek geladen wordt).
