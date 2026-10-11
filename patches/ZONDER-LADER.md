# Zonder lader: welke games starten op zichzelf?

*Gegenereerd door `patches/proef-zonder-lader.py`; niet met de hand bijwerken.*

Elke game is gestart met **alleen `wine <exe>`** — geen `OpenParrotLoader`, geen `BudgieLoader`,
geen `TeknoParrot(64).dll`, geen drop-in en geen patch. De prefix, de werkmap en de
omgevingsvariabelen zijn precies die van de directe start (`scripts/launch-direct.py <Game> --plan`),
maar de lader en de kern zijn uit de opdrachtregel gehaald. Dit is de scherpste vraag die je over
**loskoppelen van TeknoParrot** kunt stellen.

**Wat een `START` hier betekent.** Het spelproces is verschenen en leefde na **20 seconden** nog.
Dus: deze game heeft de lader niet nodig om te *blijven draaien*.

**Wat een `START` hier níet betekent.** Niet dat de game speelbaar is. Besturing, het JVS-bord, de
dongle, de regio en soms het beeld gaan via de lader die we hier juist weglaten. Een game die hier
START kan in de gewone start alsnog een dongle of een I/O-bord nodig hebben. Andersom is een
`niet gestart` geen bewijs dat de lader onmisbaar is: het kan ook een ontbrekende afhankelijkheid
of een verkeerde werkmap zijn.

**Hoe het spelproces herkend wordt.** Op de laatste twee delen van het pad van de exe, in beide
schrijfwijzen (`views/LGI/…` en `views\LGI\…`), én op een omgevingsvariabele die alleen onze eigen
start meekrijgt (`RG_PROEF_ZONDER_LADER=<Game>`). Zonder dat merk zou een game die iemand anders tegelijk start
als de onze tellen. Opruimen gebeurt met datzelfde patroon plus onze eigen procesgroep; nooit met
een leeg patroon (dat heeft eerder de SSH-verbinding omgelegd).

**Uitkomst: 20 van de 36 games starten zonder enige lader.**

| Game | Resultaat | Spelproces gezien (s) | Nog levend na 20s | Laatste logregel | `Type` | `EmulationProfile` |
|---|---|---|---|---|---|---|
| 3DCosplayMahjong | geen plan | — | nee | `de directe start kan deze game niet plannen` | TeknoParrot | `TaitoTypeXGeneric` |
| GaiaAttack4 | **START** | 1 | ja | `ntsync: up and running.` | OpenParrot | `GaiaAttack4` |
| GalagaAssault | niet gestart | — | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| Gashaaaan | viel uit | 1 | nee | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| Gashaaaan2 | viel uit | 1 | nee | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| GhostBusters | niet gestart | — | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| GoGoStrike | viel uit | 1 | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| HOTDSD | geen plan | — | nee | `de directe start kan deze game niet plannen` | TeknoParrot | `ALLSHOTDSD` |
| Halo | niet gestart | — | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `WartranTroopers` |
| HauntedMuseum | **START** | 1 | ja | `ntsync: up and running.` | OpenParrot | `HauntedMuseum` |
| HauntedMuseumII | **START** | 1 | ja | `ntsync: up and running.` | OpenParrot | `HauntedMuseum2` |
| InfinityBladeFX | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `InfinityBlade` |
| JurassicPark | viel uit | 1 | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| LGI | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `SegaJvsLetsGoIsland` |
| LGI3D | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `SegaJvsLetsGoIsland` |
| LGS | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `LGS` |
| LethalEnforcers3 | viel uit | 1 | nee | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| LuigisMansion | viel uit | 2 | nee | `ntsync: up and running.` | TeknoParrot | `LuigisMansion` |
| MedaruNoGunman | viel uit | 1 | nee | `No TextParam File:../textparam/gamedifficulty_par` | TeknoParrot | `FarCry` |
| MusicGunGun2 | **START** | 1 | ja | `ntsync: up and running.` | OpenParrot | `MusicGunGun2` |
| NERF | **START** | 1 | ja | `Fallback handler could not load library Z:/home/deck/mnt/spidernas_emu` | TeknoParrot | `WartranTroopers` |
| OG | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `SegaJvsGoldenGun` |
| OmatsuriYasanGansoTakoyaki | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| OmatsuriYasanKingyoSukui | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| Otomedius | viel uit | 1 | nee | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| PacmanChomp | viel uit | 1 | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| PigsMightFly | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| RabbidsHollywood | **START** | 2 | ja | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` | TeknoParrot | `WartranTroopers` |
| RaccoonRampage | **START** | 1 | ja | `info:  Setting display mode: 1920x1080@60` | TeknoParrot | `WartranTroopers` |
| RainbowBombergirl | **START** | 1 | ja | `Total: 23.536100 ms (FindLiveObjects: 1.536800 ms CreateObjectMapping:` | TeknoParrot | `WartranTroopers` |
| Rampage | **START** | 1 | ja | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` | TeknoParrot | `WartranTroopers` |
| TC5 | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `TimeCrisis5` |
| TankTankTank | geen plan | — | nee | `de directe start kan deze game niet plannen` | ElfLdr2 | `DeadHeat` |
| Terminator | viel uit | 1 | nee | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| TombRaider | **START** | 2 | ja | `Total: 3.553600 ms (FindLiveObjects: 0.165400 ms CreateObjectMapping: ` | TeknoParrot | `WartranTroopers` |
| TransformersShadowsRising | **START** | 1 | ja | `ntsync: up and running.` | TeknoParrot | `TransformersShadowsRising` |

## Hoe je dit vernieuwt

```bash
cd ~/projects/ravengun-patches
python3 patches/proef-zonder-lader.py
```

Het script zet de game zelf neer en ruimt hem daarna op. Het raakt de NAS niet aan en schrijft
alleen dit bestand plus `/tmp/rg-zonder-<Game>.log`. Draai het niet terwijl je ergens anders
dezelfde game start: het herkent zijn eigen proces wel, maar twee games tegelijk maken de meting
onnodig rommelig.
