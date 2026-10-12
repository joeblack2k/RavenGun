# Volledigheidscontrole van de TeknoParrot-verzameling

**Opdracht (eigenaar):** controleer per game of de bestanden compleet zijn en het spel met TeknoParrot
zou kunnen starten. Volgorde: eerst volledigheid (nog niets wijzigen), alleen US- en EU-titels (geen
Japanse), nieuw naar oud, en Jurassic Park als uitzondering ook. Alleen lezen op de NAS.

## De methode, en wat hij wel en niet dekt

**Orakelbronnen** (allemaal lokaal in de TeknoParrot-installatie, dus zonder de NAS aan te raken):

| bron | wat het geeft |
|---|---|
| `GameProfiles/*.xml` (1.967 stuks) | `EmulatorType` (de route), `ExecutableName` (welk bestand TeknoParrot start), `Is64Bit`, `EmulationProfile` |
| `Metadata/*.json` (1.965 stuks) | `release_year` en `platform` -- hiermee is "nieuw naar oud" een meting in plaats van een gok |
| de NAS zelf | per map: aantal bestanden, omvang, lege bestanden, en de structuur |

**Wat ik NIET heb gevonden:** de bestandshashes die TeknoParrot bij zijn integriteitstest gebruikt. De
profielen noemen de test wel (`BypassFilesystemIntegrityTest`, `TPCheckAndFixBadDump`), maar de
verwachte hashes zitten er niet in. **Deze controle is dus structureel, niet hash-voor-hash.** Een game
kan hier "compleet" uitkomen terwijl een databestand toch andere inhoud heeft dan TeknoParrot verwacht.
Dat onderscheid hoort bij elke uitspraak hieronder te staan.

**Wat er per game gecontroleerd wordt:**

1. bestaat het uitvoerbare bestand dat het profiel noemt (`ExecutableName`);
2. zijn er **lege bestanden** (0 bytes) -- een klassiek teken van een afgebroken kopie;
3. klopt de **ELF-structuur** (alleen bij ElfLdr2-games): vallen alle `PT_LOAD`-segmenten binnen het
   bestand, en zijn alle `DT_NEEDED`-bibliotheken ergens te vinden (spelmap, `ElfLdr2/libs`,
   `runtime/`, of het systeem);
4. is de **omvang en de bestandsstructuur** logisch voor het soort game (Unity heeft `*_Data/Managed`,
   `StreamingAssets` enz.).

**De NAS-inventaris is nagerekend.** Een steekproef (`Halo Fireteam`, `Galaga Assault`, `Go Go Strike`,
`Tomb Raider`) komt exact overeen met `du -sb` en met de som van de bestandsgroottes. 87 spelnamen, 11
met een `game`-bestand -- en die 11 zijn precies de 11 ElfLdr2-games.

## De volgorde (nieuw naar oud, zonder Japanse titels)

**ElfLdr2-games** (de Linux-klasse, met een `game`-bestand):

| game | jaar | omvang | bestanden | gecheckt |
|---|---|---|---|---|
| Halo Fireteam | 2018 | 10,6 GB | 15.932 | **ja** |
| Pacman Chomp-Mania | 2017 | 380 MB | 273 | nee |
| Walking Dead | 2017 | 4,4 GB | 8.143 | nee |
| GhostBusters | 2016 | 2,9 GB | 3.342 | nee |
| Galaga Assault | 2015 | 505 MB | 373 | nee |
| **Jurassic Park** (uitzondering) | 2015 | 5,0 GB | 5.672 | nee |
| Terminator Salvation | 2010 | 5,2 GB | 13.266 | nee |
| Go Go Strike | 2007 | 35 MB | 14 | nee |
| Target Terror Gold | 2004 | 1,5 GB | 3.052 | nee |
| Police Trainer 2 | 2003 | 1,9 GB | 244 | nee |

**Overgeslagen als Japanse titel:** `Gashaaaan Refill!!!!` (2007, ElfLdr2) en `Rainbow Bombergirl`
(2022, Konami). Let op: dit is het enige punt waarop op **titelkenmerken** wordt gewerkt en niet op een
meting -- de mapnamen op de NAS bevatten geen regiomarkering.

**Overige routes** (nieuw naar oud, eerste tien): Raccoon Rampage (2023), NERF Arcade (2023),
Drakons Realm Keepers (2023), Infinity Blade FX (2020), Rampage (2020), Wild West Shootout (2019),
Elevator Action Invasion (2019), Mars Sortie (2018), Tomb Raider (2018), **Rabbids Hollywood (2018)**,
Transformers Shadows Rising (2018).

## Halo Fireteam Raven (2018, ElfLdr2, 64-bit) -- **structureel compleet**

| controle | uitkomst |
|---|---|
| uitvoerbaar bestand | **`g7/halo/game`**, 33.554.432 B. Plus `game_original`, `Patch`, `rtinit` en `rtinit_original` -- er is dus gepatcht |
| lege bestanden | **0 van de 15.932** |
| ELF-kop | `ELF64 EXEC`, entry `0x414600` |
| `PT_LOAD` binnen het bestand | **0 van de segmenten erbuiten** -- niet afgekapt |
| `DT_NEEDED` (13) | alle 13 vindbaar |

De drie die eerst ontbreken leken, komen van elders:

* `libfmod.so.10` en `libfmodstudio.so.10` -> **`runtime/lib-fmodstub/`** (onze eigen stub-route).
  `ElfLdr2/x64/libs` bevat alleen de **Windows**-varianten (`fmod64.dll`, `fmodstudio64.dll`), die bij
  een Windows-build horen en niet bij deze ELF;
* `libiw.so.30` -> **`ldconfig` kent hem**, dus van het systeem.

**Kanttekening:** twee van de dertien bibliotheken komen uit onze stub-map. Of TeknoParrot Halo kan
starten hangt daarmee aan het **pad** (`runtime/lib-fmodstub` moet in de bibliotheekzoektocht zitten) en
niet aan ontbrekende bestanden.

## Rabbids Hollywood (2018, Adrenaline PC Based, profiel: `Game.exe`) -- **structureel compleet**

Het profiel zegt `EmulatorType: TeknoParrot`, `EmulationProfile: WartranTroopers`,
**`ExecutableName: Game.exe`**, `Is64Bit: true`. Dit is dus een **Windows/PC-based** game en geen
ELF-game -- wat klopt met wat er in de map staat (`.exe`, `.dll`, Unity).

| controle | uitkomst |
|---|---|
| uitvoerbaar bestand | **`Game.exe`**, 650.752 B -- precies wat het profiel noemt |
| lege bestanden | **0 van de 1.788** |
| omvang | **2,0 GB** (`Game_Data` alleen al 1,9 GB) |
| Unity-structuur | `Game_Data` met `Managed`, `Plugins`, `Resources`, `StreamingAssets`, `Mono`, `GI`; `UnityPlayer.dll` (23 MB) |
| BepInEx-route | `BepInEx/{core,plugins,config,cache,patchers}`, `doorstop_config.ini`, `.doorstop_version`, `winhttp.dll` |
| operator-gereedschap | `RabbidsHollywood_Operator.exe` + `.conf` aanwezig |

**Kanttekening:** het aagames-profiel noemt Rabbids Hollywood eerder als een Unity-game die **zonder**
TeknoParrot loopt, via de daemon en de BepInEx-plugin. Er zijn dus twee routes voor dezelfde map; deze
controle gaat over de map, niet over de routekeuze.
