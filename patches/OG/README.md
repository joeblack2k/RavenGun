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

`enforced` staat op `false`, en dat is sinds **ronde 34 gemeten** in plaats van aangenomen: de game
start en blijft draaien **zonder enige regiobron**, zowel met de eigen lader (`patches/STARTTEST.md`,
22 s) als met alleen `wine <exe>` (`patches/ZONDER-LADER.md`, 20 s, en in de langere meting nog levend
na 70 s). Er is dus geen regiowaarde nodig om door te starten.

Wat **niet** gemeten is: of de game speelbaar is — hij is nooit gespeeld — en of hij later in het
spel op de regio terugkomt. De `[Status] Note` van `game.ini` zegt dat nu ook, en `enforced_basis` in
`dongle.json` noemt de meting.

Let op het verschil met `LGI`/`LGI3D`/`LGS`: `gs2.exe` heeft géén `DefaultRegion`-terugval zoals
`LGI_RingW_F_safe.exe` (`DefaultRegion`, `Region is JPN./USA./EXP./CHN.` komen er niet in voor), maar
gaat wel door na een mislukte dongle-opening. Waarom dat zo is, is niet uitgezocht.

## Het gereedschap vond de exe niet, de beschrijving klopt

`python3 scripts/region.py --game OG` meldt deze dongle **rood** — *"de executable van OG noemt geen
enkele amDongle- of regioleesroute; klopt dongle.json wel?"* — en dat is misleidend: het gereedschap
heeft de executable **niet gevonden**, dus het heeft nul merktekens gezien.

Waarom niet: `region.py` zoekt de gamemap in `$RAVENGUN_GAME_DIR`, dan `[Game] GamePath` uit
`game.ini`, dan het `GamePath` van het profiel. `LGI`, `LGI3D` en `LGS` hebben een
`UserProfiles/<Game>.xml` met daarin een `GamePath` naar de exe op de NAS; **OG heeft dat
gebruikersprofiel niet** (de game is nooit in TeknoParrotUi geopend) en `GameProfiles/OG.xml` heeft
een leeg `GamePath`. De map op de NAS heet bovendien `OAV6DU~3`, een 8.3-korte naam.

Met de map erbij is hetzelfde gereedschap **groen**, en dat is de meting die de beschrijving
bevestigt:

```bash
RAVENGUN_GAME_DIR=/home/deck/mnt/spidernas_emulation/Teknoparrot/OAV6DU~3 \
  python3 scripts/region.py --game OG --json
# "errors": [], "enforced": false
# merktekens: "amDongle Ver." 1x, "keychip.appboot.region" 1x, "REGION" 5x
```

**Wat er nog te repareren valt** is dus niet deze map maar het gereedschap: `region.py` zou de
gamemap moeten oplossen zoals `launch-direct.py` dat doet (via de view of de gamemappen). Dat is werk
in de loader-repo. Tot dan is `RAVENGUN_GAME_DIR` de werkende route.

## Waarom hier geen bibliotheek staat

Het soort `region` is gegevens zonder binary. De regio- en printplaatwaarde is de software-emulatie
van dongle en PIC die de TeknoParrot-kern zelf doet
(`findings/12_aanpassingen_en_koppelingen.md`, paragraaf 7), en er is geen regio-emulator om in de
view te zetten: de numerieke codering van de regio en het apparaat dat de `amDongle`-laag opent zijn
niet vastgesteld (`docs/region-layer.md`, paragraaf 6). `scripts/region.py` is de controle van de
laag, geen bibliotheek. Daarom staat er in `dropin/dropin.ini` alleen de donglebeschrijving, met
`To = .` en een sectie per bestand.

## Wat er nog moet gebeuren

1. ~~De game opzetten en starten en dan `enforced` meten.~~ **Gedaan in ronde 34:** de game start en
   blijft draaien, met en zonder lader, en `enforced = false` is daarmee gemeten. Wat er nog moet
   gebeuren is de game echt **spelen** (munt, vizier, door het spel heen) om te zien of de regio
   later alsnog opduikt.
2. De route en de invoer vastleggen; het profiel is `SegaJvsGoldenGun` met venster `x 18..229`,
   `y 66..245`, inverted (`findings/05_target_games.md`, paragraaf 7).
3. De dongle-aanroepen echt beantwoorden kan pas als de regiocodering en het apparaat bekend zijn
   (`docs/region-layer.md`, paragraaf 6, punten 1 en 2).
