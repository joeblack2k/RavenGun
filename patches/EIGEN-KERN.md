# Onze eigen kern: welke games herkent hij?

*Gegenereerd door `patches/kern-herkenning.py`; niet met de hand bijwerken.*

De OpenParrot-kern kiest zijn `EmulationProfile` op een **CRC over de eerste `0x400` bytes** van de
hoofdmodule van de game. Dit bestand rekent diezelfde CRC uit voor elke game in `patches/`, met
precies het algoritme uit de bron waarmee onze eigen kern gebouwd is. Het antwoord op de vraag
**"kan deze game van de gesloten `TeknoParrot(64).dll` af?"** begint hier: een game die onze kern
niet herkent, kan er ook niet op draaien zonder eerst een eigen `EmulationProfile` te krijgen.

**Bron:** `GameDetect.cpp` en `Utility/Utils.cpp` uit de OpenParrot-bron, commit
`fc8ba632e1f67c5c340ff88f30efca743e8d38f5` (GPL-3.0). De kern rekent eerst de gewone CRC
(`crcResult`, 95 waarden in de tabel) en alleen als die niet matcht de **genormaliseerde** CRC
(`newCrcResult`, 158 waarden): daarbij worden in de PE-header 8 bytes op `pePTR+0x18` en 4 bytes op
`pePTR+0x36` op nul gezet, zodat een andere `ImageBase` de herkenning niet breekt.

**Uitkomst: 4 van de 36 games worden door onze eigen kern herkend** — precies de vier die er
al op draaien.

| Game | CRC (gewoon) | CRC 32-bits | CRC 64-bits | Onze kern zegt | Profiel in `game.ini` | Lader |
|---|---|---|---|---|---|---|
| 3DCosplayMahjong | — | — | — | geen exe gevonden (niet op de NAS of twee exe's) | `TaitoTypeXGeneric` | TeknoParrot |
| GaiaAttack4 | `0x0f79d088` | `0xad310e21` | `0xce86fe66` | **GaiaAttack4** (tabel 2, 32-bits) | `GaiaAttack4` | OpenParrot |
| GalagaAssault | `0xa68dfc94` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| Gashaaaan | `0x85b7185d` | `0xd0a54b30` | `0xd0a54b30` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| Gashaaaan2 | `0x9314f595` | `0xe412f9e3` | `0xa980aaa7` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| GhostBusters | `0x3ce21a20` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| GoGoStrike | `0x65a70231` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| HOTDSD | — | — | — | geen exe gevonden (niet op de NAS of twee exe's) | `ALLSHOTDSD` | TeknoParrot |
| Halo | `0xc4f85cde` | `0x9774fb66` | `0x38b2211c` | — niet in de tabel — | `WartranTroopers` | ElfLdr2 |
| HauntedMuseum | `0x76af7e50` | `0x3fed1b24` | `0xa27d0bfe` | **HauntedMuseum** (tabel 2, 32-bits) | `HauntedMuseum` | OpenParrot |
| HauntedMuseumII | `0xb26f1207` | `0x5a7fa241` | `0x16ac0890` | **HauntedMuseum2100** (tabel 2, 32-bits — USA 1.00) | `HauntedMuseum2` | OpenParrot |
| InfinityBladeFX | `0x467f37d6` | `0x90d5e6f8` | `0x3ee51761` | — niet in de tabel — | `InfinityBlade` | TeknoParrot |
| JurassicPark | `0x2d5ddf6c` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| LGI | `0x82fed329` | `0xd400a3f5` | `0x7c3e5f6f` | — niet in de tabel — | `SegaJvsLetsGoIsland` | TeknoParrot |
| LGI3D | `0x3f74055e` | `0x2c8b0265` | `0xadd85a43` | — niet in de tabel — | `SegaJvsLetsGoIsland` | TeknoParrot |
| LGS | `0x2f970de5` | `0x9848ba54` | `0x003620a8` | — niet in de tabel — | `LGS` | TeknoParrot |
| LethalEnforcers3 | `0x4dff93a9` | `0xbf789a3d` | `0xaf2664dc` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| LuigisMansion | `0x573d704b` | `0xfc9d6556` | `0xd3dc9603` | — niet in de tabel — | `LuigisMansion` | TeknoParrot |
| MedaruNoGunman | `0x98ad184e` | `0x7f186a03` | `0x34afc796` | — niet in de tabel — | `FarCry` | TeknoParrot |
| MusicGunGun2 | `0xf5d90f01` | `0xf36d354a` | `0xbb355da3` | **MusicGunGun2** (tabel 2, 32-bits) | `MusicGunGun2` | OpenParrot |
| NERF | `0xc8ad67ba` | `0x4391886f` | `0xe3e5ca38` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| OG | `0xf598cc36` | `0x4904b14d` | `0xfa7c005e` | — niet in de tabel — | `SegaJvsGoldenGun` | TeknoParrot |
| OmatsuriYasanGansoTakoyaki | `0x986327aa` | `0x4ea3ebb9` | `0x6daf6828` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| OmatsuriYasanKingyoSukui | `0x199311cd` | `0x6be188a1` | `0xb441defb` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| Otomedius | `0x25cd6d88` | `0x71b8be7e` | `0x1589148b` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| PacmanChomp | `0x3ecb82c6` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| PigsMightFly | `0x5dfb8389` | `0xa9db9d42` | `0x6c641712` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| RabbidsHollywood | `0x82bced43` | `0x3cff0275` | `0x9c8b4022` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| RaccoonRampage | `0x937b2d51` | `0x75a581d3` | `0x71e52ec0` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| RainbowBombergirl | `0xa2adcff2` | `0x1cee20c4` | `0xbc9a6293` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| Rampage | `0x82bced43` | `0x3cff0275` | `0x9c8b4022` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| TC5 | `0x1ba8b580` | `0x2fd81c35` | `0x4c19ac1c` | — niet in de tabel — | `TimeCrisis5` | TeknoParrot |
| TankTankTank | — | — | — | geen exe gevonden (niet op de NAS of twee exe's) | `DeadHeat` | ElfLdr2 |
| Terminator | `0x11dce40c` | — (geen PE-header) | — (geen PE-header) | — niet in de tabel — | `RawThrillsGUN` | ElfLdr2 |
| TombRaider | `0x46cd5495` | `0x80704587` | `0x2d15ab1b` | — niet in de tabel — | `WartranTroopers` | TeknoParrot |
| TransformersShadowsRising | `0xef5fb3ea` | `0x55f66578` | `0xf23ab4ca` | — niet in de tabel — | `TransformersShadowsRising` | TeknoParrot |

**RabbidsHollywood en Rampage hebben dezelfde drie CRC's** (`0x82bced43`): hetzelfde stub-bestand, dus dezelfde
hoofdmodule. Voor de herkenning maakt dat niet uit (geen van beide staat in de tabel),
maar het betekent wel dat een profiel op deze exe beide games zou vangen.

**Wat er per game nodig is.** De kolom *Profiel in `game.ini`* noemt het `EmulationProfile` dat de
game daar heeft — voor de games met de gesloten kern (`Type = TeknoParrot`) is dat het profiel dat
**onze eigen kern zou moeten implementeren**: eerst een nieuwe `case <CRC>` in `GameDetect.cpp` die
op die `GameID` uitkomt, en dan de hooks van dat profiel. Voor de vier games die onze kern al
herkent is de naam in `game.ini` precies de `GameID` uit de CRC-tabel, dus die koppeling is geen
gok. **24 games wachten daar nog op:**

| Profiel dat nog van ons moet komen | Games |
|---|---|
| `WartranTroopers` | 13 — Gashaaaan, Gashaaaan2, LethalEnforcers3, NERF, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, Otomedius, PigsMightFly, RabbidsHollywood, RaccoonRampage, RainbowBombergirl, Rampage, TombRaider |
| `SegaJvsLetsGoIsland` | 2 — LGI, LGI3D |
| `ALLSHOTDSD` | 1 — HOTDSD |
| `FarCry` | 1 — MedaruNoGunman |
| `InfinityBlade` | 1 — InfinityBladeFX |
| `LGS` | 1 — LGS |
| `LuigisMansion` | 1 — LuigisMansion |
| `SegaJvsGoldenGun` | 1 — OG |
| `TaitoTypeXGeneric` | 1 — 3DCosplayMahjong |
| `TimeCrisis5` | 1 — TC5 |
| `TransformersShadowsRising` | 1 — TransformersShadowsRising |

**De 8 games met `Type = ElfLdr2` staan hier wel in maar horen bij een andere vraag.** Hun
`EmulationProfile` (DeadHeat, RawThrillsGUN, WartranTroopers) is van **BudgieLoader**, niet van de OpenParrot-kern; die games zijn ELF's
zonder PE-header, dus de CRC-tabel zegt er niets over. Wat zij nodig hebben is de eigen
`LD_PRELOAD`-kastnabootser voor de 64-bits ElfLdr2-familie, niet een profiel in `GameDetect.cpp`.

**6 games hebben geen PE-header** (GalagaAssault, GhostBusters, GoGoStrike, JurassicPark, PacmanChomp, Terminator). Dat zijn de games die op de Cygwin-lader
(ElfLdr2/BudgieLoader) draaien en dus ELF's zijn; de CRC-tabel van de kern gaat over PE's,
dus over hun herkenning zegt deze meting niets. Ze staan er wel in omdat hun
`EmulationProfile` dezelfde vraag oproept: ook die lader is van TeknoParrot.

**Wat je hieruit leest.** De vier games met `kern van ons` in `tools/verify-all.py` zijn de enige
die de kern zelf thuisbrengt. Voor de andere 32 zou de kern eerst **bij naam** bekend moeten zijn
(een nieuwe `case` in `GameDetect.cpp`), en daarna moeten de hooks van dat `EmulationProfile` nog
geschreven worden — dat tweede staat hier niet in. Dit bestand zegt dus alleen **of de kern de
game herkent**, niet of er al een profiel voor is.

**Eén meting die eruit volgt.** HauntedMuseumII is de **USA 1.00**-build (`0x5a7fa241` ->
`GameID::HauntedMuseum2100`) en niet de JP 1.01 (`0x42d54499` -> `GameID::HauntedMuseum2101J`).
Van de twee versiesets in `patches/HauntedMuseumII/patches.ini` geldt er dus maar één voor onze
dump; de andere staat er als documentatie van een andere build.

## Hoe je dit vernieuwt

```bash
cd ~/projects/ravengun-patches
python3 patches/kern-herkenning.py
```

Het script leest de tabel uit de bron in `~/.cache/ravengun/build/openparrot/<commit>/` en haalt
het pad van elke exe op met `scripts/launch-direct.py <Game> --plan` in de loader-repo. Staat die
build er niet, zet dan `RAVENGUN_REPO` of bouw de kern opnieuw met `scripts/build-openparrot.sh`.
