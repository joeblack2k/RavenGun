# Onderzoek: de regio per game (ruwe waarnemingen)

*Ronde 6 (2026-10-11). Het mechanisme staat in `docs/research/region-mechanisme.md`, de conclusie en
de laag in `docs/region-layer.md`. Dit bestand is de naslag per game: wat er in de executable staat,
wat er in de ini's staat, en welke waarde daaruit volgt.*

Alle commando's zijn lezend op de NAS. `N` is
`/home/deck/mnt/spidernas_emulation/Teknoparrot`.

```bash
strings -a -n 4 "<exe>" | grep -i -E 'region|dongle|pcb|keychip'
```

---

## LGI — Let's Go Island: Lost on the Island of Tropics

| | |
|---|---|
| Exe | `Let's Go Island Lost on the Island of Tropics/LGI_RingW_F_safe.exe` (PE32, i386, 6.481.920 B) |
| Profiel | `LGI`, `EmulationProfile = SegaJvsLetsGoIsland`, `EmulatorType = TeknoParrot` |
| Platform | Sega RingWide |
| Regiovelden | `General/DongleRegion = JAPAN`, `General/PcbRegion = JAPAN` (beide `Dropdown`, keuzes `JAPAN|USA|JAP2N|EXPORT|CHINA`) |
| In `patches/LGI/game.ini` | `[Game] DongleRegion = JAPAN`, `PcbRegion = JAPAN` |
| Mechanisme | `dongle` |
| Waarde | regio `JAPAN` (`JPN`), printplaat `JAPAN` (`JPN`) |
| `enforced` | `false` |

Merkers in de exe: `amDongleOpen`, `amDongleSetupKeychip`, `amDongleGetRegion`,
`amDongleGetModelType`, `amDongleGetPlatformId`, `amDongleGetGameId`,
`amDongleBillingGetKeychipId`, `amDongleSendAndRecvEx`, `amDongleGetRegion() is failed.`,
`Dongle::initialize() failed. but reason is unknown...`, `Keychip Not Found`, `DefaultRegion`,
`readRegionEeprom`, `readRegionBuSram`, `setupRegionEeprom`, `setupRegionBuSram`,
`Region is JPN./USA./EXP./CHN.`, `Unknown region!`.

Bijzonderheden: `game.bat` start eerst `mxgethwinfo.exe -s app.ini` (platformcontrole) en pas bij
exitcode 0 de game; het profiel start de exe direct. `settings.tg` (288 B) is hoog-entropisch en
bevat geen leesbare regionaam. `MACtbl/SBUH_Table.dat` is de MAC-tabel van de JVS-ketting, geen regio.

---

## LGI3D — Let's Go Island 3D: Lost on the Island of Tropics

| | |
|---|---|
| Exe | `Let's Go Island 3D Lost on the Island of Tropics/LGI.exe` (PE32, i386, 6.619.648 B) |
| Profiel | `LGI3D`, `EmulationProfile = SegaJvsLetsGoIsland`, `EmulatorType = TeknoParrot` |
| Platform | Sega RingWide |
| Regiovelden | `General/DongleRegion = USA`, `General/PcbRegion = USA` |
| In `patches/LGI3D/game.ini` | `[Game] DongleRegion = USA`, `PcbRegion = USA` |
| Mechanisme | `dongle` |
| Waarde | regio `USA`, printplaat `USA` |
| `enforced` | `false` |

Merkers: identiek aan LGI (zelfde `fwAM`-bouwsel). Extra in de map: `vimlicrt.dll`, `vimasdlib.dll`,
`ViM3DLib.dll` (de 3D-laag), `OutputBlaster.dll`, `ViMCplPage.dll`. De map heeft ook een
`teknoparrot.ini` met `DongleRegion=USA` — geschreven door TeknoParrotUi, door de game zelf nooit
gelezen (de exe noemt `teknoparrot` nul keer).

---

## LGS — Let's Go Safari

| | |
|---|---|
| Exe | `Let's Go Safari/GameSampR_RingWide.exe` (PE32, i386, 2.177.536 B) |
| Profiel | `LGS`, `EmulationProfile = LGS`, `EmulatorType = TeknoParrot` |
| Platform | Sega RingWide |
| Regiovelden | `General/DongleRegion = USA`, `General/PcbRegion = USA` |
| In `patches/LGS/game.ini` | `[Game] DongleRegion = USA`, `PcbRegion = USA` |
| Mechanisme | `dongle` |
| Waarde | regio `USA`, printplaat `USA` |
| `enforced` | `false` |

Merkers: `amDongle Ver.1.12 Build:Sep  4 2012 00:17:40`, `amDongleOpen`, `amDongleOpenEx`,
`amDongleSetupKeychip`, `amDongleSendAndRecvEx`, `amDongleGetRegion`, `amDongleGetModelType`,
`amDongleGetPlatformId`, `amDongleGetGameId`, `amDongleGetVersion`, `amDongleGetPicVersion`,
`amDongleBillingGetKeychipId`, `keychip.appboot.region`, `keychip.appboot.gameid`,
`keychip.appboot.platformid`, `keychip.appboot.modeltype`, `keychip.appboot.networkaddr`,
`Error Response KeychipError !!!`.

Bijzonderheden: de map heeft `data_english`, `data_japanese`, `data_chinese`, `data_indonesia`,
`data_spain`, `data_thailand` en `dat_otherLanguage`. `GameConfig.ini` kiest de taal met
`[language] left=e right=c` (Engels/Chinees) — dat is de **taalkeuze van de spelbestanden**, niet de
regio; de regio komt uit `keychip.appboot.region`. `gameid.txt` bevat `SDAX`.

---

## GaiaAttack4 — Gaia Attack 4

| | |
|---|---|
| Exe | `Gaia Attack 4/game.exe` (PE32, i386) |
| Profiel | `GaiaAttack4`, `EmulationProfile = GaiaAttack4`, `EmulatorType = OpenParrot` |
| Regioveld | `Region = E`, `FieldType = Text`, `Hint: Valid options: A C E I` |
| Mechanisme | `gun-ini` |
| Waarde | `E` (Export) in `MINIGUN.INI` |
| Dongle? | **nee** |

De gamemap bevat `MINIGUN.INI`:

```
REGION		E
CNFNAME		.\OpenParrot\GA4
RANKFILE	.\OpenParrot\
PRJENABLE   	1
SCREEN_WIDTH	1280
SCREEN_HEIGHT	720
RENDER_WIDTH	1280
RENDER_HEIGHT	720
RENDER_WIDTH3D	1280
RENDER_HEIGHT3D	720
```

De exe noemt `MINIGUN.INI`, `REGION`, `CNFNAME`, `Minigun`, `minigunwin` en
`D:\projects\soft\src\minigun\Release\minigun.pdb`. De game leest de regio dus uit zijn eigen
tekstbestand. `scripts/launch-direct.py` schrijft die regel uit het profielveld `Region`
(`taito_gun_ini`, dezelfde opbouw als TeknoParrotUi).

Geparkeerd om een andere reden dan de regio: geen beeld (de Wine-MXCSR-crash is omzeild, de game loopt
de JVS-ketting door en blijft zwart).

---

## HauntedMuseum — Haunted Museum

| | |
|---|---|
| Exe | `Haunted Museum/Game.exe` (PE32, i386) |
| Profiel | `HauntedMuseum`, `EmulationProfile = HauntedMuseum`, `EmulatorType = OpenParrot` |
| Regioveld | `Region = E`, `FieldType = Text`, `Hint: Valid options: A C E I` |
| Mechanisme | `gun-ini` |
| Waarde | `E` in `MUSEUM.ini` |
| Dongle? | **nee** |

`MUSEUM.ini`:

```
REGION		E
CNFNAME		.\OpenParrot\HM
RANKFILE	.\OpenParrot\
PRJENABLE   	1
SCREEN_WIDTH	1280   SCREEN_HEIGHT 720
RENDER_WIDTH	1280   RENDER_HEIGHT 720
RENDER_WIDTH3D	1280   RENDER_HEIGHT3D 720
```

De exe noemt `MUSEUM.INI`, `REGION`, `Museum`, `museumwin` en
`D:\projects\soft\src\museumjapan\Release\museum.pdb`. Let op: de map heet `MUSEUM.ini` (kleine
letters) in de bestandslijst en `MUSEUM.INI` in de exe — Wine en Windows zijn daar niet
hoofdlettergevoelig, `scripts/region.py` accepteert beide.

Geparkeerd om het zwarte beeld, niet om de regio.

---

## HauntedMuseumII — Haunted Museum II

| | |
|---|---|
| Exe | `Haunted Museum II/game.exe` (PE32, i386) |
| Profiel | `HauntedMuseumII`, `EmulationProfile = HauntedMuseum2`, `EmulatorType = OpenParrot` |
| Regioveld | `Region = E`, `FieldType = Text`, `Hint: Valid options: A C E I` |
| Mechanisme | `gun-ini` |
| Waarde | `E` in `haunted2.ini` |
| Dongle? | **nee** |

`haunted2.ini`:

```
REGION		E
CNFNAME		.\OpenParrot\HM2
RANKFILE	.\OpenParrot\
PRJENABLE   	1
SCREEN_WIDTH	1280   SCREEN_HEIGHT 720
RENDER_WIDTH	1280   RENDER_HEIGHT 720
RENDER_WIDTH3D	1280   RENDER_HEIGHT3D 720
```

De exe noemt `HAUNTED2.INI`, `REGION`, `Haunted2`, `haunted2win`, `Haunted Museum II` en — het
duidelijkste bewijs dat de waarde echte code paden kiest — `regione`, `regionc`, `regionj` (Export,
China, Japan). PDB-pad: `d:\projects\game\HM2\haunted2\Release\game.pdb`.

Geparkeerd om het zwarte beeld, niet om de regio.

---

## RainbowBombergirl — Rainbow Bombergirl

| | |
|---|---|
| Exe | `game.exe` (PE32+, x86-64) |
| Profiel | `RainbowBombergirl`, `EmulationProfile = WartranTroopers`, `EmulatorType = TeknoParrot` |
| Regioveld | `Region = EXPORT` (`Dropdown`, keuzes `JAPAN` en `EXPORT`; `Hint: Changes game language.`) |
| Mechanisme | `core-region` |
| Waarde | `EXPORT` |
| Dongle? | **nee** |

De eigen setup-tekst van TeknoParrot in de view zegt het letterlijk:

```
General - Region:   EXPORT
```

en de view-ini bevat `[General] Region=EXPORT`. De game zelf is een Unity-spel met BepInEx; zijn
`game.exe` heeft 468 strings en noemt `teknoparrot` en `region` **nul** keer. De waarde gaat dus via
de 64-bits kern (of de BepInEx-plugin) naar binnen. `Region` staat als tekst in de 64-bits kern en
niet in de 32-bits kern, en de game is x86-64: dat past.

Geparkeerd om de aanraakbediening/twee schermen, niet om de regio.

---

## Overige games met een regio/PCB-veld (niet in deze opdracht)

Voor de volledigheid, geteld in de 932 profielen van de runtime:

* **69 profielen** hebben `DongleRegion`/`PcbRegion` — de Ring/Lindbergh-familie (`SegaJvs*`, `LGS`,
  `Hotd4*`, `Rambo`, `BorderBreak`, `SegaInitialD`, `GuiltyGearRE2`, `SegaSonicAllStarsRacing`,
  `SegaRTuned`, `SegaOlympic2016`, `HummerExtreme`, `PrimevalHunt`, `Harley`, `TooSpicy`, `GSEVO`,
  `2Spicy`, `KODrive`, `SSASR`, `IDZ*`, …).
* **22 profielen** hebben `Region` (zonder `Dongle`/`Pcb`): `CTHR`, `GBOS`, `GaiaAttack4`,
  `GhostSquad`, `HOTD3`, `HauntedMuseum`, `HauntedMuseumII`, `IDTA`, `IDTAS5`, `OllieKing`,
  `RainbowBombergirl`, `SGC05`, `SGC06`, `SilentScopeBoneEater`, `WMMT1`, `WMMT1J`, `WMMT2`, `WMMT2j`,
  `or2`, `or2b`, `or2sp`, `vc3`. Dat zijn dus niet alleen de cxbxr-games: ook de Namco-titels
  (`WMMT*`, `or2*`, `vc3`) en de Taito-geweren zitten erbij. Wat de waarde betekent verschilt per
  familie (cxbxr: de Xbox-regio in de cxbxr-configuratie; Taito-geweren: de `REGION`-letter in hun
  eigen ini; Rainbow Bombergirl: de taal via de 64-bits kern — het profiel zegt zelf
  `Hint: Changes game language.`).

De twee velden zijn niet uitwisselbaar: de eerste hoort bij de Sega-dongle, de tweede bij een bestand
of een kerninstelling.
