# Medaru No Gunman

Sega RingWide (2010), TeknoParrot-profiel `MedaruNoGunman`, `EmulationProfile FarCry`, exe
`exe/gunman_dxF.exe`. Instellingen: `game.ini`.
Bevindingen en meetgeschiedenis: `findings/MedaruNoGunman.md` (op de NAS naast de games, niet in deze
repo).

## Regio/PCB-dongle: nee — en daarom staat hier geen `dongle.json`

De game vraagt **wel** iets aan de hardware, maar dat is geen regio/printplaat-dongle zoals bij de
Sega Ring-games (`LGI`, `LGI3D`, `LGS`, `OG`). Een regiowaarde invullen zou verzinnen zijn: er is
geen waarde om te antwoorden en de waarde is ook niet te meten. Daarom heeft deze game geen
`dongle.json`; de controle `scripts/region.py` meldt hem daardoor als "vraagt een regio-dongle, maar
de beschrijving ontbreekt". Dat is een open punt van de laag, geen fout in deze map (zie onderaan).

### Wat de executable vraagt

`exe/gunman_dxF.exe` (6.823.424 B, het bestand uit het profiel):

* `amDongle Ver.1.03 Build:Oct 14 2009`, `amDongleOpen`, `amDongleOpenEx`, `amDongleSetupKeychip`,
  `amDongleSendAndRecvEx`, `amDongleUpdate`, `amDongleBusy`, `amDongleResponseCheck`;
* de keychip-opdrachten `keychip.appboot.region`, `keychip.appboot.gameid`,
  `keychip.appboot.platformid`, `keychip.appboot.modeltype`, `keychip.appboot.systemflag`,
  `keychip.appboot.dvdflag`, `keychip.appboot.formattype`, `keychip.appboot.networkaddr`, plus
  `keychip.billing.keyid`/`mainid`/`nearfull`/`playcount`/`playlimit`,
  `keychip.tracedata.get`/`put`/`restore`/`sectorerase`, `keychip.ssd.proof`, `keychip.decrypt`,
  `keychip.encrypt`, `keychip.ds.compute`, `keychip.version`;
* de foutteksten `Keychip Not Found.`, `ERR_KEYCHIP`, `ERR_KEYCHIP_DATA`, `Wrong Region.`,
  `Error Response KeychipError !!!`.

Dat is de **keychip** van de Lindbergh/Ring-familie (een sleutel met facturatie en tracelading), niet
de regio/printplaat-dongle. Het verschil is meetbaar in de executable zelf: `LGI_RingW_F_safe.exe` en
`LGI.exe` roepen `amDongleGetRegion` aan en lezen daarnaast `readRegionEeprom` en
`readRegionBuSram`; `gunman_dxF.exe` doet geen van beide.

### Waarom er geen waarde in te vullen is

1. Het profiel `MedaruNoGunman` heeft **geen** `DongleRegion`/`PcbRegion`
   (`GameProfiles/MedaruNoGunman.xml`, `EmulationProfile FarCry`; `packaging/game-options.json` heeft
   voor deze game ook geen regio-optie). Er is dus geen regiowaarde om te antwoorden, en `game.ini`
   heeft er ook geen.
2. De bevindingen hebben geen regiowaarde voor deze game: `findings/data/target_hw_patch.json` (50
   games) en `findings/11_hardware_patches_dongles.md` hebben geen regel voor Medaru No Gunman.
3. De game kiest de dongleregio niet zelf. `gunman_dxF.exe` noemt alleen
   `CheckRegion_EepRom:Dongle(jpn) and Main(jpn):success` en
   `CheckRegion_EepRom:Dongle(gl) and Main(exp):success`, met `failed` voor alle andere combinaties
   (`Dongle(gl)` met `Main(chn)`, `Main(usa)`, `Main(jpn)`). De dongleregio moet dus gelijk zijn aan
   de regio van het hoofdboard, en welke van de twee combinaties hier geldt is niet gemeten. Dezelfde
   drie strings staan in `gunman_dxD.exe` en `gunman_dxR.exe`, dus de bestandsnaam zegt het niet.

De SRAM en het EEPROM van die keychip worden door de TeknoParrot-kern als bestand nagebootst
(`SBTF_sram.bin`, `SBTF_e2prom.bin` in de prefix; zie `findings/MedaruNoGunman.md`). De game start
daarmee en meldt `3001 RAM DATA IS BAD`. Dat is de open kwestie van deze game, niet de regio: de
kern emuleert de sleutel zelf, net als bij `LGI`/`LGI3D`/`LGS`.

### Wat er nodig is om deze game toch een donglebeschrijving te geven

1. Een eigen soort voor de keychip (bijvoorbeeld `keychip`) in de tabel van `docs/dropins.md`, want
   `hasp`, `matrix`, `region`, `sega-scan` en `amdaemon` dekken deze niet.
2. De regiowaarde meten: een start met het testmenu (`TEST`, toets 0) of een log van
   `keychip.appboot.region` in het spelproces.
3. Pas dan `dropin/ravengun-hasp/MedaruNoGunman/dongle.json` schrijven, met een sectie per bestand in
   `dropin/dropin.ini` en `To = .`.

Tot die tijd is de eerlijke stand: deze game vraagt een sleutel, de laag heeft er geen gegevens voor,
en de game start zonder (geparkeerd om de RAM-fout, niet om de regio).
