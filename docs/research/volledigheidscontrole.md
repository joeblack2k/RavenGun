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


## Verwijderd op verzoek van de eigenaar (2026-10-12)

Vier games zijn **definitief verwijderd**, van de NAS en uit de app: `Galaga Assault` (483M, 373
bestanden), `Pacman Chomp-Mania` (364M, 273), `Go Go Strike` (34M, 14) en `Gashaaaan Refill!!!!`
(539M, 2241). De NAS-map ging van **90 naar 86 items**. Verwijderd met exacte paden en zonder globs,
met een verificatiestap vooraf die per map naam, omvang en bestandsaanstal toonde.

**Gevolg dat vastgelegd moet blijven:** met Go Go Strike is ook de enige game weg waarop onze eigen
lader einde-tot-einde bewezen was (`rg-elfloader` -> `bowling_usa`, exit 143 na 25-30 s, eigen assert op
het scherm). Die proef is **niet meer opnieuw te doen**. De bewijsstukken staan nog in de repo
(`docs/screenshots/gogostrike-loader-assert.png` en `docs/research/eigen-elflader-plan.md`), dus de
geschiedenis klopt, maar herhaalbaar is het niet meer. Wie later denkt dat de test stuk is: dat is dit.

## Antwoord op twee vragen van de eigenaar

**"Er staan meer games in de TeknoParrot-map toch?"** Ja. De NAS heeft **86 top-level items**, waarvan
**circa 82 games** -- de rest zijn geen games: `TeknoParrotUi-2.0.0.130-linux-x64` (de runtime),
`_ia_work` (29 GB werkmap), `findings`, en twee mappen met een **8.3-kortnaam** (`OAV6DU~3`, `TNXK0O~C`)
die ik nog moet identificeren. Daarnaast, los hiervan: de **lokale TeknoParrot-installatie heeft 1.967
gameprofielen** -- er worden dus veel meer games *ondersteund* dan er op de NAS staan.

**"Heb je ook een bestandscontrole gedaan?"** Ja, maar op structuur en niet op hashes:

* wel: bestaat het uitvoerbare bestand dat het profiel noemt; lege bestanden; bij ELF: vallen alle
  `PT_LOAD` binnen het bestand en zijn alle `DT_NEEDED` vindbaar; omvang en bestandsstructuur;
* niet: hash-voor-hash. De profielen noemen de integriteitstest wel
  (`BypassFilesystemIntegrityTest`, `TPCheckAndFixBadDump`) maar bevatten de verwachte hashes niet, en
  ik heb ze nergens anders gevonden. **Een game kan hier dus "compleet" uitkomen terwijl een databestand
  toch andere inhoud heeft.**

## De ijkreeks: de vijf games die de eigenaar zeker werkend heeft gezien

Dit is de belangrijkste controle van allemaal, want hier is de uitkomst **bekend**. Als mijn methode
deze vijf afkeurt, deugt de methode niet.

| game | route | uitvoerbaar bestand | leeg | omvang | oordeel |
|---|---|---|---|---|---|
| Luigi's Mansion Arcade | TeknoParrot | `exe/x64/VACUUM.exe` (profiel heeft een leeg `ExecutableName`) | 12 | 1,6 GB | compleet |
| Rabbids Hollywood | TeknoParrot | `Game.exe` | 0 | 2,0 GB | compleet |
| Halo Fireteam | ElfLdr2 | `g7/halo/game` | 0 | 9,9 GB | compleet |
| Terminator Salvation | ElfLdr2 | `game` | 2 | 5,0 GB | compleet |
| Transformers Shadows Rising | TeknoParrot | `Transformers2/Transformers2.exe` + `shell/Shell.exe` | 2 | 8,5 GB | compleet |
| Transformers Human Alliance | TeknoParrot | `exe/TF_Gun_R_Ring_dumped.exe` | 0 | 4,9 GB | compleet |

**En de lege bestanden blijken geen defecten.** Ze zijn allemaal van een soort dat hoort te leeg zijn:

* Luigi's Mansion: 12x `DataGlobal/EN/Data/Stage/Progress/CAM_st*/<moeilijkheid>/CAM_st*_*.ped` -- dat
  zijn **voortgangsbestanden** van de kast (4 stages x 3 moeilijkheden). Een verse kast heeft ze leeg;
* Terminator: `irgun/cmgr000000.txt` en `irgun/cmgr670719.txt` -- **logbestanden** van het geweer;
* Transformers Shadows Rising: `Transformers2/TF2_Gun_R.ilk` (een **incremental-linkerbestand**, een
  bouwartefact) en `shell/install.crc`.

**Dat verfijnt de methode:** een leeg bestand is pas verdacht als het er **niet** hoort te zijn. Voortgang,
logs, CRCs en bouwartefacten horen leeg te zijn; data, assets en programma's niet. Die indeling moet bij
elke volgende game gemaakt worden, anders levert elke kast met een verse save een valse melding op.

## Drie fouten in mijn eigen checker, en hoe ze aan het licht kwamen

Alle drie gaven ze een **valse "ontbreekt"-melding**, en alle drie zijn ze gerepareerd:

1. **Mapnaam met een spatie aan het eind.** `Terminator Salvation ` heet zo op de NAS; mijn controle
   zocht `Terminator Salvation` en meldde "map bestaat niet". Nu wordt de echte naam opgezocht.
2. **Onvolledig bibliotheekzoekpad.** Ik zocht `DT_NEEDED` alleen in `ElfLdr2/libs`, `x64/libs`,
   `lib32` en het systeem -- en meldde daardoor dat Halo's `libfmod.so.10` en `libfmodstudio.so.10`
   ontbraken, terwijl ze in `runtime/lib-fmodstub/` staan. Nu worden alle `runtime/*`-mappen meegenomen.
3. **Hoofdlettergevoelig zoeken.** `shell.exe` in het profiel tegen `shell/Shell.exe` op schijf: `find`
   is hoofdlettergevoelig, dus die werd gemist. Nu is de vergelijking ongevoelig.

**De les is dezelfde als eerder in dit project:** een controle die niets vindt, moet kunnen aantonen dat
ze iets gezocht heeft. Al deze drie meldden "niets gevonden" terwijl het er wel was.


## House of the Dead: Scarlet Dawn (2018, SEGA ALLS, profiel `HOTDSD`) -- **compleet**

De map heet `House of the Dead Scarlet Dawn` en het profiel heet `HOTDSD`; die twee hebben geen enkele
letter gemeen, dus mijn naamkoppeling vond hem niet. Het profiel zegt `EmulatorType: TeknoParrot`,
`EmulationProfile: ALLSHOTDSD`, **`ExecutableName: Hodzero-Win64-Shipping.exe`**, `Is64Bit: true`, en het
heeft `ExecutableName2`/`GamePath2`/`HasTwoExecutables`/`LaunchSecondExecutableFirst` -- er zijn dus twee
uitvoerbare bestanden. "Hodzero" is de codenaam, `-Win64-Shipping.exe` is de Unreal Engine-naamgeving.

| controle | uitkomst |
|---|---|
| uitvoerbaar bestand | `App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/Hodzero-Win64-Shipping.exe`, 54.128.128 B |
| origineel ernaast | `Hodzero-Win64-Shipping_org.exe`, 51.470.336 B -- het bestand is dus **gepatcht** |
| SEGA-daemon | `AMDaemon/amdaemon.exe` (8.272.384 B) + `amdaemon.exe.original` (4.567.040 B) |
| opstartketen van de kast | `App/Startup/`: `05_SetVideoPort.bat`, `10_SetResolution.bat`, `20_SetAudioVolume.bat`, `99_StartApp.bat`, `AppStartup.bat` |
| lege bestanden | **0 van de 2.081** |
| omvang | 13 GB, met de Unreal-pak van **7,8 GB** (`Content/Paks/Hodzero-WindowsNoEditor.pak`) en de CRIWARE-films (`Content/Hodzero_V1/ADX/*.usm`) |
| UE-structuur | `App/Hodzero/WindowsNoEditor/{Engine,Hodzero,AMDaemon}` |

**Wat dit voor de latere bouw betekent:** dit is een Unreal Engine-game met de **AM Daemon** van SEGA
ALLS ernaast, en de kast start hem via een reeks `.bat`-bestanden. De projectkant heeft de amdaemon-laag
al onderzocht; dat is het aanknopingspunt voor game 6, niet de lader.

## Vierde fout in mijn eigen checker: de dieptelimiet

Ik meldde eerst dat `Hodzero-Win64-Shipping.exe` **ontbrak**. Dat kwam doordat ik zocht met
`find -maxdepth 4`, en het bestand staat op **diepte 6**
(`App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/`). Mijn eigen grens had het buiten beeld gehouden.

Dat is de **vierde** keer in deze controle dat een "niets gevonden"-uitkomst aan mijn eigen zoekopdracht
lag en niet aan de game: eerder waren dat de spatie aan het eind van een mapnaam, het onvolledige
bibliotheekzoekpad en het hoofdlettergevoelige zoeken. **De regel die ik daaruit trek: zoek onbeperkt
diep naar het uitvoerbare bestand, of meld expliciet op welke diepte je gezocht hebt.** Een dieptelimiet
is een aanname, en die hoort in de uitkomst te staan.
