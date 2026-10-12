# Invoer-route per game: wie levert de invoer?

*Gegenereerd door `patches/invoer-route.py`; niet met de hand bijwerken.*

Elke game heeft in `patches/<Game>/game.ini` een `[Input] Route`. Die route kiest welk dialect
de **IO-brug van de daemon** gebruikt (`iobridge start <Game>`, zie `docs/architecture.md` in de
lader-repo). Deze kaart zet die route naast het `Type` en het `EmulationProfile` van de game,
want daaruit volgt de vraag die de hele opdracht bepaalt: **zit er een kern in het invoerpad, en
is dat de gesloten kern of onze eigen kern?**

Dat onderscheid is de kern van de zaak. Loopt de invoer buiten elke kern om, dan kan een game
van de gesloten kern af zonder dat er één per-game hook geschreven hoeft te worden. Staat er
wél een kern in het pad met route `tp-shm`, dan leest die kern de bordindeling uit het gedeelde
geheugen en vertaalt hem naar wat de game verwacht (`docs/loskoppelen-per-game.md`, tabel *Wat
een eigen kern moet implementeren*, regel *De invoer uit het gedeelde geheugen*).

**Gemeten over 36 games:**

| vraag | aantal |
|---|---|
| invoer loopt via een kern (`tp-shm`) | **22** |
| waarvan op de **gesloten** TeknoParrot-kern → vraagt een eigen profiel | **15** |
| waarvan al op **onze eigen** OpenParrot-kern | 4 |
| waarvan op de **Cygwin-lader** (ElfLdr2) → vraagt een eigen ELF-lader | 3 |
| invoer loopt buiten elke kern om (de brug doet het zelf) | 8 |
| geen route in `game.ini` gemeten | 6 |

## Alle games

| Game | `Type` | `EmulationProfile` | `Route` | transport | indeling/bord | wie levert de invoer |
|---|---|---|---|---|---|---|
| 3DCosplayMahjong | TeknoParrot | `TaitoTypeXGeneric` | `-` | - | - | — |
| GaiaAttack4 | OpenParrot | `GaiaAttack4` | `tp-shm` | - | MusicGunGun2Pipe | **een kern** |
| GalagaAssault | ElfLdr2 | `RawThrillsGUN` | `-` | - | - | — |
| Gashaaaan | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopersKonami | **een kern** |
| Gashaaaan2 | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopersKonami | **een kern** |
| GhostBusters | ElfLdr2 | `RawThrillsGUN` | `-` | - | - | — |
| GoGoStrike | ElfLdr2 | `RawThrillsGUN` | `tp-shm` | - | GoGoStrike | **een kern** |
| HOTDSD | TeknoParrot | `ALLSHOTDSD` | `evdev` | - | - | de brug |
| Halo | ElfLdr2 | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| HauntedMuseum | OpenParrot | `HauntedMuseum` | `tp-shm` | - | MusicGunGun2Pipe | **een kern** |
| HauntedMuseumII | OpenParrot | `HauntedMuseum2` | `tp-shm` | - | MusicGunGun2Pipe | **een kern** |
| InfinityBladeFX | TeknoParrot | `InfinityBlade` | `tp-shm` | - | TimeCrisis5 | **een kern** |
| JurassicPark | ElfLdr2 | `RawThrillsGUN` | `evdev` | - | RawThrillsGUN | de brug |
| LGI | TeknoParrot | `SegaJvsLetsGoIsland` | `jvs` | pipe | LGI | de brug |
| LGI3D | TeknoParrot | `SegaJvsLetsGoIsland` | `jvs` | pipe | LGI3D | de brug |
| LGS | TeknoParrot | `LGS` | `jvs` | - | LGS | de brug |
| LethalEnforcers3 | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| LuigisMansion | TeknoParrot | `LuigisMansion` | `tp-shm` | - | LuigisMansion | **een kern** |
| MedaruNoGunman | TeknoParrot | `FarCry` | `tp-shm` | - | FarCryPipe | **een kern** |
| MusicGunGun2 | OpenParrot | `MusicGunGun2` | `tp-shm` | - | MusicGunGun2Pipe | **een kern** |
| NERF | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| OG | TeknoParrot | `SegaJvsGoldenGun` | `-` | - | - | — |
| OmatsuriYasanGansoTakoyaki | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| OmatsuriYasanKingyoSukui | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| Otomedius | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| PacmanChomp | ElfLdr2 | `RawThrillsGUN` | `-` | - | - | — |
| PigsMightFly | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| RabbidsHollywood | TeknoParrot | `WartranTroopers` | `unity-tcp` | - | - | de brug |
| RaccoonRampage | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| RainbowBombergirl | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| Rampage | TeknoParrot | `WartranTroopers` | `tp-shm` | - | WartranTroopers | **een kern** |
| TC5 | TeknoParrot | `TimeCrisis5` | `tp-shm` | - | TimeCrisis5 | **een kern** |
| TankTankTank | ElfLdr2 | `DeadHeat` | `-` | - | - | — |
| Terminator | ElfLdr2 | `RawThrillsGUN` | `tp-shm` | - | RawThrillsGUN | **een kern** |
| TombRaider | TeknoParrot | `WartranTroopers` | `unity-tcp` | - | - | de brug |
| TransformersShadowsRising | TeknoParrot | `TransformersShadowsRising` | `hid-board` | - | TransformersShadowsRising | de brug |

## De gesloten kern zit in het invoerpad: 15 games

Dit is de groep die een eigen `EmulationProfile` vraagt. Zonder dat profiel leest niemand de
bordindeling, en krijgt de game geen invoer — ook niet als hij verder start. De profielnaam die
erbij hoort staat in `EIGEN-KERN.md`.

| Game | profiel dat nog van ons moet komen |
|---|---|
| Gashaaaan | `WartranTroopers` |
| Gashaaaan2 | `WartranTroopers` |
| InfinityBladeFX | `InfinityBlade` |
| LethalEnforcers3 | `WartranTroopers` |
| LuigisMansion | `LuigisMansion` |
| MedaruNoGunman | `FarCry` |
| NERF | `WartranTroopers` |
| OmatsuriYasanGansoTakoyaki | `WartranTroopers` |
| OmatsuriYasanKingyoSukui | `WartranTroopers` |
| Otomedius | `WartranTroopers` |
| PigsMightFly | `WartranTroopers` |
| RaccoonRampage | `WartranTroopers` |
| RainbowBombergirl | `WartranTroopers` |
| Rampage | `WartranTroopers` |
| TC5 | `TimeCrisis5` |

## Onze eigen kern zit al in het invoerpad: 4 games

Deze vier draaien al op onze eigen kern en hebben hun invoer dus al van ons. Ze staan hier
alleen ter volledigheid: voor de invoer hoeft hier niets meer bij.

* **GaiaAttack4** — profiel `GaiaAttack4`
* **HauntedMuseum** — profiel `HauntedMuseum`
* **HauntedMuseumII** — profiel `HauntedMuseum2`
* **MusicGunGun2** — profiel `MusicGunGun2`

## De Cygwin-lader zit in het invoerpad: 3 games

Bij deze games is de kern in het invoerpad **BudgieLoader**, niet de OpenParrot-kern. Zij
vragen dus geen profiel in `GameDetect.cpp` maar een eigen ELF-lader; zie `ELF-NATIVE.md`.

* **GoGoStrike** — `Type = ElfLdr2`, profiel `RawThrillsGUN`
* **Halo** — `Type = ElfLdr2`, profiel `WartranTroopers`
* **Terminator** — `Type = ElfLdr2`, profiel `RawThrillsGUN`

## De brug levert de invoer: 8 games

Hier loopt de invoer buiten elke kern om. Voor de **invoer** is de gesloten kern hier niet
nodig; of hij nog ergens anders voor nodig is, is een aparte vraag en staat in `EIGEN-KERN.md`.

| Game | `Type` | route | transport | wat de brug doet |
|---|---|---|---|---|
| HOTDSD | TeknoParrot | `evdev` | - | de brug schrijft rechtstreeks naar evdev |
| JurassicPark | ElfLdr2 | `evdev` | - | de brug schrijft rechtstreeks naar evdev |
| LGI | TeknoParrot | `jvs` | pipe | de game praat zelf JVS; de daemon is het bord aan de andere kant |
| LGI3D | TeknoParrot | `jvs` | pipe | de game praat zelf JVS; de daemon is het bord aan de andere kant |
| LGS | TeknoParrot | `jvs` | - | de game praat zelf JVS; de daemon is het bord aan de andere kant |
| RabbidsHollywood | TeknoParrot | `unity-tcp` | - | de brug praat TCP met de Unity-game |
| TombRaider | TeknoParrot | `unity-tcp` | - | de brug praat TCP met de Unity-game |
| TransformersShadowsRising | TeknoParrot | `hid-board` | - | de brug maakt een eigen HID-bord |

## Geen route in game.ini: 6 games

| Game | `Type` | profiel | wat er gemeten is |
|---|---|---|---|
| 3DCosplayMahjong | TeknoParrot | `TaitoTypeXGeneric` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |
| GalagaAssault | ElfLdr2 | `RawThrillsGUN` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |
| GhostBusters | ElfLdr2 | `RawThrillsGUN` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |
| OG | TeknoParrot | `SegaJvsGoldenGun` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |
| PacmanChomp | ElfLdr2 | `RawThrillsGUN` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |
| TankTankTank | ElfLdr2 | `DeadHeat` | geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten |

## Wat dit verandert aan de opdracht

De opdracht *"elke game zonder TeknoParrot"* valt hiermee in drie stukken uiteen:

1. **8 games waar de invoer buiten elke kern om loopt.** Daar is voor de invoer geen hook
   nodig, ongeacht welk `Type` er in `game.ini` staat.
2. **15 games waar de gesloten kern de invoer doet.** Dit is de groep die om een eigen
   `EmulationProfile` vraagt, en dat is minder dan de 24 profielen die `EIGEN-KERN.md` in
   totaal noemt: de rest zit in groep 1 of 3.
3. **3 games waar de Cygwin-lader de invoer doet plus 6 games zonder gemeten route.** Voor
   groep 3 is nog niets over de invoer gemeten; dat is een open punt, geen conclusie.

Let op: deze kaart gaat **alleen over de invoer**. Of een game ook nog een bytepatch, een
drop-in of een dongle nodig heeft staat in `PATCHES-PER-GAME.md`, `REGIO-PER-GAME.md` en
`STAND.md`.

## Hoe je dit vernieuwt

```bash
cd ~/projects/ravengun-patches
python3 patches/invoer-route.py
```
