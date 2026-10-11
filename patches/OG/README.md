# Operation G.H.O.S.T.

Sega RingWide (2012), TeknoParrot-profiel `OG`, `EmulationProfile SegaJvsGoldenGun`, exe `gs2.exe`.
De game staat op de NAS in de map `OAV6DU~3`.

Deze map is nieuw: de game vraagt een regio- en printplaat-dongle en had nog geen map in deze repo.
De app kent hem al (`packaging/game-options.json`, id `operation_ghost`) en de repo noemt hem in de
takenlijst bij de Sega Ring/JVS-groep (`docs/todo.md`, B3e). Instellingen: `game.ini`.

## De dongle

`dropin/ravengun-hasp/OG/dongle.json`, soort `region`: regio `JAPAN`, printplaat `JAPAN`.

Waar dat vandaan komt:

* het TeknoParrot-sjabloon `GameProfiles/OG.xml`: `General/DongleRegion = JAPAN` en
  `General/PcbRegion = JAPAN`, beide een `Dropdown` met de keuzes `JAPAN|USA|JAP2N|EXPORT|CHINA`
  (dezelfde waarden staan in `[Config]` van `game.ini`);
* `findings/11_hardware_patches_dongles.md`, sectie "Operation G.H.O.S.T. - OG" (dezelfde twee
  waarden) en `findings/data/target_hw_patch.json`;
* de exe zelf (`gs2.exe`, 2.658.816 B): `amDongle Ver.1.09 Build:May 12 2011`, `amDongleOpen`,
  `amDongleOpenEx`, `amDongleSetupKeychip`, `amDongleSendAndRecvEx`, `amDongleResponseCheck`,
  `amDongleBusy`, `amDongleUpdate`, `getKeychipID`, de keychip-opdrachten `keychip.appboot.region`,
  `keychip.appboot.gameid`, `keychip.appboot.platformid`, `keychip.appboot.modeltype`,
  `keychip.appboot.systemflag` en de foutteksten `Error Response KeychipError !!!`,
  `KEYCHIP ACCESS FAILED`, `Init: amDongleIsAvailable() is false`,
  `Init: Faild to init DongleLib. (amDongleInit() Faild)`, `Faild to get Keychip ID.`;
* de printplaat `837-14572` is het standaard Sega-JVS-gunbord van de `SegaJvs*`-profielen
  (`packaging/jvs-boards.json`, bord `SegaGun2P`).

`enforced` staat op `false`, en dat is een **aanname, geen meting**: de game is nog niet gestart. De
reden om op `false` te beginnen is hetzelfde gedrag als bij `LGI`/`LGI3D`/`LGS`: de 32-bits
TeknoParrot-kern kent `DongleRegion` en `PcbRegion` niet, dus die waarde wordt vastgelegd en niet
afgedwongen (`docs/region-layer.md`, paragraaf 1.3 en 1.4). Let op het verschil: `gs2.exe` heeft
géén `DefaultRegion`-terugval zoals `LGI_RingW_F_safe.exe` (`DefaultRegion`, `Region is JPN./USA./
EXP./CHN.` komen er niet in voor), maar gaat wel door na een mislukte dongle-opening. Wie de game
voor het eerst start, moet `enforced` meten en de regel `enforced_basis` in `dongle.json` vervangen.

## Waarom hier geen bibliotheek staat

Het soort `region` is gegevens zonder binary. De regio- en printplaatwaarde is de software-emulatie
van dongle en PIC die de TeknoParrot-kern zelf doet
(`findings/12_aanpassingen_en_koppelingen.md`, paragraaf 7), en er is geen regio-emulator om in de
view te zetten: de numerieke codering van de regio en het apparaat dat de `amDongle`-laag opent zijn
niet vastgesteld (`docs/region-layer.md`, paragraaf 6). `scripts/region.py` is de controle van de
laag, geen bibliotheek. Daarom staat er in `dropin/dropin.ini` alleen de donglebeschrijving, met
`To = .` en een sectie per bestand.

## Wat er nog moet gebeuren

1. De game opzetten en starten (view/prefix via `scripts/tp-add-game.py OG <exe>`), en dan
   `enforced` meten en hierboven bijwerken.
2. De route en de invoer vastleggen; het profiel is `SegaJvsGoldenGun` met venster `x 18..229`,
   `y 66..245`, inverted (`findings/05_target_games.md`, paragraaf 7).
3. De dongle-aanroepen echt beantwoorden kan pas als de regiocodering en het apparaat bekend zijn
   (`docs/region-layer.md`, paragraaf 6, punten 1 en 2).
