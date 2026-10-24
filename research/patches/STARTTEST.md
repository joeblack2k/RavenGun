# Starttest per game

Elke game is met de eigen lader gestart en daarna opgeruimd. Gemeten op 2026-10-12 01:50 uit de ruwe uitvoer van `starttest3.sh`.

De tabel voegt **alle** ruwe bestanden samen die `starttest3.sh` in `/tmp` heeft achtergelaten (`/tmp/rg-start3-*.txt`); komt een game in meerdere runs voor, dan wint de nieuwste run. Draai voor een schone meting eerst de oude bestanden weg.

```bash
# één game
bash /home/deck/starttest3.sh 22 <Game>
# alle games (elke run schrijft zijn eigen /tmp/rg-start3-<pid>.txt)
bash /home/deck/starttest3.sh 22
python3 patches/starttest-gen.py
```

## Wat er gemeten wordt

| Kolom | Betekenis |
|---|---|
| SPEL | de seconde waarop het SPELPROCES voor het eerst gezien is (0 = nooit) |
| LADER | leefde de launcher nog toen de meting afliep |
| **START** | het spelproces is gezien en leefde aan het eind nog |
| **viel-uit** | het spelproces is gezien maar was daarna weg |
| **lader-levend** | geen spelproces gevonden, maar de launcher liep nog |
| **niet-gestart** | geen van beide |

Het spelproces wordt gezocht op de laatste twee delen van het pad van de exe, in BEIDE
schrijfwijzen: een Wine-proces heeft het Windows-pad in zijn commandoregel
(`Z:\\...\\views\\LGI\\...`), niet het Linux-pad.

### Twee maten die niet werken (en waarom)

1. **Wachten op `Have fun :)` of `Loading game...`.** Die twee regels komen uit de
   OpenParrot-lader en bestaan in de **ElfLdr2-route niet**: 0 treffers in de zeven
   ElfLdr2-game-ELF's, in de vier `BudgieLoader`-bestanden en in `ElfLdr2/TeknoParrot.dll`.
   Elke ElfLdr2-game las daardoor `start-niet` terwijl hij liep.
2. **Opruimen met `pkill -f <bestandsnaam van de exe>`.** Zes games hebben een exe die
   letterlijk `game` heet, dus een starttest van de ene game doodde de start van een andere;
   en met een leeg patroon matcht `pkill -f ""` elk proces. Er wordt nu op de map van de exe
   gematcht, en nooit met een leeg patroon.

| Game | Resultaat | SPEL (s) | LADER | Laatste logregel |
|---|---|---|---|---|
| 3DCosplayMahjong | niet-gestart | 0 | 0 | Niet direct te starten: game niet gevonden: '' (voeg de |
| GaiaAttack4 | START | 1 | 1 | [launch-direct 01:35:56] klaar (exitcode gestopt) |
| GalagaAssault | START | 1 | 1 | [launch-direct 01:36:21] klaar (exitcode gestopt) |
| Gashaaaan | START | 1 | 1 | [launch-direct 01:36:46] klaar (exitcode gestopt) |
| Gashaaaan2 | START | 1 | 1 | [launch-direct 01:37:11] klaar (exitcode gestopt) |
| GhostBusters | START | 1 | 1 | [launch-direct 01:37:37] klaar (exitcode gestopt) |
| GoGoStrike | START | 1 | 1 | [launch-direct 01:38:02] klaar (exitcode gestopt) |
| HOTDSD | niet-gestart | 0 | 0 | Niet direct te starten: profielen met twee exe's worden |
| Halo | START | 1 | 1 | [launch-direct 01:38:52] klaar (exitcode gestopt) |
| HauntedMuseum | START | 1 | 1 | [launch-direct 01:39:17] klaar (exitcode gestopt) |
| HauntedMuseumII | START | 1 | 1 | [launch-direct 01:39:42] klaar (exitcode gestopt) |
| InfinityBladeFX | START | 1 | 1 | [launch-direct 01:40:08] klaar (exitcode gestopt) |
| JurassicPark | START | 1 | 1 | [launch-direct 01:40:33] klaar (exitcode gestopt) |
| LGI | START | 1 | 1 | [launch-direct 01:40:58] klaar (exitcode gestopt) |
| LGI3D | START | 1 | 1 | [launch-direct 01:41:23] klaar (exitcode gestopt) |
| LGS | START | 1 | 1 | [launch-direct 01:41:49] klaar (exitcode gestopt) |
| LethalEnforcers3 | START | 1 | 1 | [launch-direct 01:42:14] klaar (exitcode gestopt) |
| LuigisMansion | START | 1 | 1 | [launch-direct 01:42:39] klaar (exitcode gestopt) |
| MedaruNoGunman | START | 1 | 1 | [launch-direct 01:43:04] klaar (exitcode gestopt) |
| MusicGunGun2 | START | 1 | 1 | [launch-direct 01:43:29] klaar (exitcode gestopt) |
| NERF | START | 2 | 1 | [launch-direct 01:43:54] klaar (exitcode gestopt) |
| OG | START | 1 | 1 | [launch-direct 01:44:20] klaar (exitcode gestopt) |
| OmatsuriYasanGansoTakoyaki | START | 1 | 1 | [launch-direct 01:44:45] klaar (exitcode gestopt) |
| OmatsuriYasanKingyoSukui | START | 1 | 1 | [launch-direct 01:45:10] klaar (exitcode gestopt) |
| Otomedius | START | 1 | 1 | [launch-direct 01:45:35] klaar (exitcode gestopt) |
| PacmanChomp | START | 1 | 1 | [launch-direct 01:46:01] klaar (exitcode gestopt) |
| PigsMightFly | START | 1 | 1 | [launch-direct 01:46:26] klaar (exitcode gestopt) |
| RabbidsHollywood | START | 1 | 1 | [launch-direct 01:46:51] klaar (exitcode gestopt) |
| RaccoonRampage | START | 2 | 1 | [launch-direct 01:47:16] klaar (exitcode gestopt) |
| RainbowBombergirl | START | 1 | 1 | [launch-direct 01:47:41] klaar (exitcode gestopt) |
| Rampage | START | 1 | 1 | [launch-direct 01:48:06] klaar (exitcode gestopt) |
| TC5 | START | 1 | 1 | [launch-direct 01:48:32] klaar (exitcode gestopt) |
| TankTankTank | niet-gestart | 0 | 0 | Niet direct te starten: game niet gevonden: '' (voeg de |
| Terminator | START | 1 | 1 | [launch-direct 01:49:23] klaar (exitcode gestopt) |
| TombRaider | START | 1 | 1 | [launch-direct 01:49:47] klaar (exitcode gestopt) |
| TransformersShadowsRising | START | 1 | 1 | [launch-direct 01:50:12] klaar (exitcode gestopt) |

## Tellingen

| | aantal |
|---|---|
| getest | 36 |
| START | 33 |
| viel-uit | 0 |
| lader-levend | 0 |
| niet-gestart | 3 |

**START:** GaiaAttack4, GalagaAssault, Gashaaaan, Gashaaaan2, GhostBusters, GoGoStrike, Halo, HauntedMuseum, HauntedMuseumII, InfinityBladeFX, JurassicPark, LGI, LGI3D, LGS, LethalEnforcers3, LuigisMansion, MedaruNoGunman, MusicGunGun2, NERF, OG, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, Otomedius, PacmanChomp, PigsMightFly, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Rampage, TC5, Terminator, TombRaider, TransformersShadowsRising

**niet-gestart:** 3DCosplayMahjong, HOTDSD, TankTankTank

## Wat een meting niet zegt

Dit zegt dat de game **start en blijft draaien**, niet dat hij speelbaar is. Of het beeld
goed is, of de ingang werkt en of de dongle beantwoord wordt staat er niet in. Voor de games
met een dongle is dat een aparte meting (`scripts/hasp.py --game <Game> --json` voor de
gegevens, en het logboek van de start voor de vraag of de bibliotheek geladen wordt).

## Status versus meting

De woordenschat van `[Status] Status` is `playable | parked | untested`. Geen van die drie
betekent "start wel, maar speelbaarheid is niet nagegaan" — en dat is precies wat deze
meting vaststelt. Daarom wordt er niets omgezet; hier staat waar de twee uit elkaar lopen.

**Start en blijft draaien, maar staat niet op `playable`** (17):

* GaiaAttack4 — `parked`
* GalagaAssault — `parked`
* GhostBusters — `parked`
* HauntedMuseum — `parked`
* HauntedMuseumII — `parked`
* InfinityBladeFX — `parked`
* JurassicPark — `parked`
* LethalEnforcers3 — `parked`
* MedaruNoGunman — `parked`
* MusicGunGun2 — `parked`
* OG — `untested`
* OmatsuriYasanGansoTakoyaki — `parked`
* OmatsuriYasanKingyoSukui — `parked`
* Otomedius — `parked`
* PacmanChomp — `parked`
* RabbidsHollywood — `parked`
* RainbowBombergirl — `parked`

Een `playable` zou hier meer claimen dan gemeten is: of het beeld goed is, of de
ingang werkt en of de dongle beantwoord wordt, staat er niet in. Wie dat per game
nakijkt, kan de status met onderbouwing omzetten.

**Staat op `playable` maar start niet** (1): HOTDSD (`playable`)

