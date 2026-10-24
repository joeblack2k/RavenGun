# Zonder lader: welke games starten op zichzelf?

*Gegenereerd door `patches/proef-zonder-lader.py`; niet met de hand bijwerken.*

Elke game is gestart met **alleen `wine <exe>`** — geen `OpenParrotLoader`, geen `BudgieLoader` en
geen `TeknoParrot(64).dll`. De prefix, de werkmap en de omgevingsvariabelen zijn precies die van de
directe start (`scripts/launch-direct.py <Game> --plan`), maar de lader en de kern zijn uit de
opdrachtregel gehaald. Dit is de scherpste vraag die je over **loskoppelen van TeknoParrot** kunt
stellen.

**Let op: dit is niet "zonder patch".** De launcher zet de drop-in van een game bij elke start in
zijn view en haalt hem daarna **niet** weg. Wat een eerdere start heeft achtergelaten, laadt de game
nog steeds — bij Rabbids Hollywood stond onze `matrix64.dll` nog in `Game_Data/Plugins/` en meldde
het spel in deze proef `**** DONGLE FOUND: 69420` uit *onze* laag. De kolom **onze bestanden in de
view** laat per game zien hoeveel bestanden uit `patches/<Game>/dropin/` er nog staan (vergeleken op
sha256). Deze meting zegt dus: **zonder lader en zonder kern**, met wat er al in de view stond.

**Wat een `START` hier betekent.** Het spelproces is verschenen en leefde na **20 seconden** nog.
Dus: deze game heeft de lader niet nodig om te *blijven draaien*.

**Wat een `START` hier níet betekent.** Niet dat de game speelbaar is. Besturing, het JVS-bord, de
dongle, de regio en soms het beeld gaan via de lader die we hier juist weglaten. Een game die hier
START kan in de gewone start alsnog een dongle of een I/O-bord nodig hebben. Andersom is een
`niet gestart` geen bewijs dat de lader onmisbaar is: het kan ook een ontbrekende afhankelijkheid
of een verkeerde werkmap zijn.

**En het venster is kort.** 20 seconden. Een game die een dongle mist kan best starten en pas later
afsluiten: Rabbids Hollywood stopt in de gewone start na ongeveer 57 seconden als de Matrix-dongle
niet antwoordt. Een `START` zegt dus alleen dat het **proces** bleef lopen, niet dat de game zijn
controles gehaald heeft. Kijk daarvoor naar de kolom *onze bestanden in de view* en naar de laatste
logregel; daaronder staat een tweede meting met een venster van 70 seconden.

**Nog een beperking van de herkenning.** Een `viel uit` kan ook betekenen dat de exe zichzelf
uitpakt en opnieuw start onder een andere naam (Luigi's Mansion en Pigs Might Fly zijn gepakt);
dan verliest het patroon het spoor en lijkt de game gestopt terwijl hij loopt. Wie het zeker wil
weten, kijkt in `/tmp/rg-zonder-<Game>.log` en in de proceslijst op dat moment.

**Hoe het spelproces herkend wordt.** Op de laatste twee delen van het pad van de exe, in beide
schrijfwijzen (`views/LGI/…` en `views\LGI\…`), én op een omgevingsvariabele die alleen onze eigen
start meekrijgt (`RG_PROEF_ZONDER_LADER=<Game>`). Zonder dat merk zou een game die iemand anders tegelijk start
als de onze tellen. Opruimen gebeurt met datzelfde patroon plus onze eigen procesgroep; nooit met
een leeg patroon (dat heeft eerder de SSH-verbinding omgelegd).

**Uitkomst: 19 van de 36 games starten zonder enige lader.**

| Game | Resultaat | Spelproces gezien (s) | Nog levend na 20s | Onze bestanden in de view | Laatste logregel | `Type` | `EmulationProfile` |
|---|---|---|---|---|---|---|---|
| 3DCosplayMahjong | geen plan | — | nee | geen drop-in | `de directe start kan deze game niet plannen` | TeknoParrot | `TaitoTypeXGeneric` |
| GaiaAttack4 | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | OpenParrot | `GaiaAttack4` |
| GalagaAssault | niet gestart | — | nee | 6 van 6 | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| Gashaaaan | viel uit | 1 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| Gashaaaan2 | viel uit | 1 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| GhostBusters | niet gestart | — | nee | geen drop-in | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| GoGoStrike | niet gestart | — | nee | geen drop-in | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| HOTDSD | geen plan | — | nee | geen drop-in | `de directe start kan deze game niet plannen` | TeknoParrot | `ALLSHOTDSD` |
| Halo | niet gestart | — | nee | 0 van 7 | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `WartranTroopers` |
| HauntedMuseum | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | OpenParrot | `HauntedMuseum` |
| HauntedMuseumII | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | OpenParrot | `HauntedMuseum2` |
| InfinityBladeFX | **START** | 1 | ja | 3 van 3 | `ntsync: up and running.` | TeknoParrot | `InfinityBlade` |
| JurassicPark | viel uit | 1 | nee | 6 van 6 | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| LGI | **START** | 1 | ja | 1 van 1 | `ntsync: up and running.` | TeknoParrot | `SegaJvsLetsGoIsland` |
| LGI3D | **START** | 1 | ja | 1 van 1 | `ntsync: up and running.` | TeknoParrot | `SegaJvsLetsGoIsland` |
| LGS | **START** | 1 | ja | 1 van 1 | `ntsync: up and running.` | TeknoParrot | `LGS` |
| LethalEnforcers3 | viel uit | 1 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| LuigisMansion | viel uit | 2 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `LuigisMansion` |
| MedaruNoGunman | viel uit | 1 | nee | geen drop-in | `No TextParam File:../textparam/gamedifficulty_par` | TeknoParrot | `FarCry` |
| MusicGunGun2 | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | OpenParrot | `MusicGunGun2` |
| NERF | **START** | 2 | ja | geen drop-in | `info:      Memory Type[9]: Property Flags = 0xc7` | TeknoParrot | `WartranTroopers` |
| OG | **START** | 1 | ja | 1 van 1 | `ntsync: up and running.` | TeknoParrot | `SegaJvsGoldenGun` |
| OmatsuriYasanGansoTakoyaki | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| OmatsuriYasanKingyoSukui | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| Otomedius | viel uit | 1 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| PacmanChomp | niet gestart | — | nee | 1 van 1 | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| PigsMightFly | viel uit | 1 | nee | geen drop-in | `ntsync: up and running.` | TeknoParrot | `WartranTroopers` |
| RabbidsHollywood | **START** | 1 | ja | 3 van 3 | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` | TeknoParrot | `WartranTroopers` |
| RaccoonRampage | **START** | 1 | ja | geen drop-in | `info:  Setting display mode: 1920x1080@60` | TeknoParrot | `WartranTroopers` |
| RainbowBombergirl | **START** | 1 | ja | geen drop-in | `Total: 21.210700 ms (FindLiveObjects: 1.366500 ms CreateObjectMapping:` | TeknoParrot | `WartranTroopers` |
| Rampage | **START** | 1 | ja | 3 van 3 | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` | TeknoParrot | `WartranTroopers` |
| TC5 | **START** | 1 | ja | 4 van 4 | `ntsync: up and running.` | TeknoParrot | `TimeCrisis5` |
| TankTankTank | geen plan | — | nee | geen drop-in | `de directe start kan deze game niet plannen` | ElfLdr2 | `DeadHeat` |
| Terminator | niet gestart | — | nee | 3 van 3 | `ShellExecuteEx failed: File not found.` | ElfLdr2 | `RawThrillsGUN` |
| TombRaider | **START** | 1 | ja | 3 van 3 | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` | TeknoParrot | `WartranTroopers` |
| TransformersShadowsRising | **START** | 1 | ja | geen drop-in | `ntsync: up and running.` | TeknoParrot | `TransformersShadowsRising` |

## Hoe je dit vernieuwt

```bash
cd ~/projects/ravengun-patches
python3 patches/proef-zonder-lader.py
```

Het script zet de game zelf neer en ruimt hem daarna op. Het raakt de NAS niet aan en schrijft
alleen dit bestand plus `/tmp/rg-zonder-<Game>.log`. Draai het niet terwijl je ergens anders
dezelfde game start: het herkent zijn eigen proces wel, maar twee games tegelijk maken de meting
onnodig rommelig.

---

## Langere meting: blijven ze ook na een dongle-timeout lopen?

De tabel hierboven meet **20 seconden**. Dat is kort: een game die zijn dongle mist kan best
starten en pas later afsluiten (Rabbids Hollywood doet dat na ongeveer 57 seconden, gemeten in
een eerdere ronde). Deze tweede meting gebruikt een **venster van 70 seconden** en dezelfde
methode, op de games die in de eerste meting STARTten.

**Uitkomst: 16 van de 16 blijven ook na 70 seconden lopen.**

| Game | Resultaat | Spelproces gezien (s) | Nog levend na 70s | Laatste logregel |
|---|---|---|---|---|
| InfinityBladeFX | **START** | 1 | ja | `ntsync: up and running.` |
| LGI | **START** | 1 | ja | `ntsync: up and running.` |
| LGI3D | **START** | 1 | ja | `ntsync: up and running.` |
| LGS | **START** | 1 | ja | `ntsync: up and running.` |
| NERF | **START** | 2 | ja | `info:      Memory Type[9]: Property Flags = 0xc7` |
| OG | **START** | 1 | ja | `ntsync: up and running.` |
| OmatsuriYasanGansoTakoyaki | **START** | 1 | ja | `ntsync: up and running.` |
| OmatsuriYasanKingyoSukui | **START** | 1 | ja | `ntsync: up and running.` |
| PigsMightFly | **START** | 1 | ja | `ntsync: up and running.` |
| RabbidsHollywood | **START** | 1 | ja | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` |
| RaccoonRampage | **START** | 1 | ja | `info:  Setting display mode: 1920x1080@60` |
| RainbowBombergirl | **START** | 1 | ja | `Total: 21.322300 ms (FindLiveObjects: 1.071700 ms CreateObjectMapping:` |
| Rampage | **START** | 2 | ja | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` |
| TC5 | **START** | 1 | ja | `ntsync: up and running.` |
| TombRaider | **START** | 1 | ja | `(Filename: C:\buildslave\unity\build\artifacts/generated/common/runtim` |
| TransformersShadowsRising | **START** | 1 | ja | `ntsync: up and running.` |
