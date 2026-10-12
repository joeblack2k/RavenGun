# Regio/PCB-dongle per game

*Wat dit bestand is:* één tabel over **alle 36 games** in `patches/` met de vraag of de game een
**regio/printplaat-dongle** vraagt, met het bewijs uit de executable zelf, met het `dongle.json` van
het soort `region` als dat er is, en met wat de game anders wél vraagt.

*Wat "regio/printplaat-dongle" hier betekent:* de **Sega-dongle/keychip** die de regio en de
printplaat draagt — de game roept de `amDongle`-API aan (`amDongleGetRegion`,
`amDongleSetupKeychip`, `keychip.appboot.region`) of leest de regio uit EEPROM/backup-SRAM
(`readRegionEeprom`, `readRegionBuSram`). Dat is de soort `region` in `dongle.json`. **Drie andere
dingen heten in dit ecosysteem ook "regio" en zijn géén dongle** (zie
`docs/region-layer.md` in de loader-repo):

| geen dongle, maar wel "regio" | wat het is | games |
|---|---|---|
| `gun-ini` | de game leest een **REGION-regel uit zijn eigen ini** (Taito Type X2) | GaiaAttack4, HauntedMuseum, HauntedMuseumII |
| `core-region` | het **profielveld `Region`** op de 64-bits kern | RainbowBombergirl |

En er zijn **vier andere donglesoorten** die geen regiodongle zijn: `hasp`, `matrix`, `sega-scan` en
`amdaemon`. Een game kan er daar één van vragen en tóch `nee` zijn op de vraag van deze kaart.

**`[Dongle]` bestaat niet meer** (beslissing van de eigenaar, ronde 12; `docs/geen-dongle-in-de-loader.md`).
Een dongle is een gewone drop-in; deze kaart zet die sectie nergens in.

## Wat hier wel en niet gemeten is

**Wel gemeten.** Voor elke game is de **executable op de NAS** op byteniveau doorzocht op de
beslissende merktekens — hetzelfde als
`strings -a -n 4 <exe> | grep -i -E 'amdongle|keychip|region|dongle'`, maar dan in Python en per game
in een script (`patches/regio-gen.py`). Van de 36 games zijn er **34** zo nagelopen; hun volledige
NAS-map is recursief doorzocht (ook submappen, en door de symlink-koppelingen van de views heen),
dus een dongle-aanroep in een DLL in een submap wordt ook gevonden. Per game is de NAS-map die
doorzocht is die van de game in zijn geheel, niet de smalle submap uit het profiel (bij HOTDSD staat
in het profiel alleen `Binaries/Win64`, terwijl de dongle-aanroepen in `AMDaemon/amdaemon.exe`
zitten — dat is precies waarom).

**Niet gemeten.** De **34** games zijn onderzocht op de merktekens in hun bestanden; er is **geen
enkele game gestart** om te zien of de game het dongle-antwoord ook echt nodig heeft. De kolom
`enforced` in de bestaande `dongle.json`-bestanden blijft daarom `false` (behalve waar een eerdere
ronde het tegendeel mat).

**Drie games staan op `onbekend`**, en dat is met opzet — een eerlijke `onbekend` is meer waard dan
een gok:

* **3DCosplayMahjong** en **TankTankTank**: die staan **niet op de NAS**, dus er is geen executable
  om te onderzoeken. Hun profielen hebben geen `DongleRegion`/`PcbRegion`/`Region`, wat op `nee`
  wijst, maar dat is niet aan de game zelf gemeten.
* **HOTDSD**: hier is wél gemeten, maar de meting geeft geen eenduidig antwoord. De game-exe vraagt
  de regio aan amdaemon; de amdaemon-service die met de game meekomt roept **zelf**
  `amDongleGetRegion` en `amDongleSetupKeychip` aan, en het profiel heeft geen regiowaarde. Zie
  open punt 3.

**Ook niet gemeten.** Of een game een regio/printplaat-dongle vraagt is hier afgeleid uit de
**aanwezigheid van de dongle-aanroepen in de executable** plus het **regioveld in het profiel**.
Of de game zonder dat antwoord doorstart is een aparte vraag, en die is alleen voor LGI, LGI3D en
LGS gemeten (die drie zijn speelbaar met `enforced = false`).

## Hoe deze kaart is gemaakt

Op de Deck, vanuit de patches-repo:

```bash
cd ~/projects/ravengun-patches
python3 patches/regio-gen.py
```

`patches/regio-gen.py` doet de meting en vult het blok tussen `BEGIN TABEL` en `END TABEL`
hieronder. Het leest alleen: de NAS-mappen (`NAS_MAP` in het script), de TeknoParrot-profielen, de
`game.ini` per game en het `dongle.json` in de drop-in. Het schrijft alleen dit bestand.

Het script **controleert zichzelf**: het waarschuwt als een oordeel tegen de meting in gaat
(bijvoorbeeld `nee` terwijl de executable `amDongleGetRegion` noemt). Het oordeel zelf
(`ja`/`nee`/`onbekend` plus "wat vraagt de game dan wel") is het enige met de hand onderhouden deel
— dat is een beoordeling, geen meting. Het staat in `OORDEEL` boven in het script.

De losse metingen van deze ronde staan in `/home/deck/rgwork/` (`regioscan3.txt` met de merktekens
per bestand, `regiobreed.txt` met de brede controle per NAS-map).

### Wat er met de loader zelf is gecontroleerd

```bash
cd ~/projects/ravengun
python3 scripts/region.py                        # alle games met een dongle.json
python3 scripts/region.py --game <Game> --json    # één game
python3 scripts/hasp.py   --game <Game> --json    # voor een regio-dongle: één verwijzingsregel naar region.py
python3 scripts/launch-direct.py <Game> --plan    # de dropins-regel en de dest van elk bestand
python3 tools/verify-all.py                       # exitcode 0, "drop-ins die NIET aankomen: 0"
cd ~/projects/ravengun-patches && python3 patches/stand-gen.py
```

<!-- BEGIN TABEL -->
| Game | regio/PCB-dongle? | bewijs (bestand · functie/string · regel) | `dongle.json` met `kind: region`? | waar komen de regiowaarden vandaan | wat vraagt de game anders |
|---|---|---|---|---|---|
| **3DCosplayMahjong** | onbekend | **geen bestanden op de NAS om te onderzoeken** (de game staat er niet) | nee | - | De game staat **niet op de NAS**, dus er is geen executable om te onderzoeken. Het profiel `TaitoTypeXGeneric` heeft geen `DongleRegion`/`PcbRegion`/`Region`. Nodig: de spelbestanden. |
| **GaiaAttack4** | nee | `eigen ini`: `game.exe` — MINIGUN.INI<br>profiel `UserProfiles/GaiaAttack4.xml`: `Region = E` | nee | - | een **bestandswaarde**: de REGION-regel in zijn eigen `MINIGUN.INI` (het profielveld `Region`). Geen dongle. |
| **GalagaAssault** | nee | `HASP/Aladdin`: `game` — hasp_login(4), nethasp(10) | nee (wel `kind: hasp`) | - | een **HASP/Aladdin-dongle** (de eigen donglesoort `hasp`). |
| **Gashaaaan** | nee | geen enkel merkteken in 67 bestanden | nee | - | niets: geen dongle-aanroep, geen regioveld, geen ini-regel. |
| **Gashaaaan2** | nee | geen enkel merkteken in 18 bestanden | nee | - | niets. |
| **GhostBusters** | nee | `HASP/Aladdin`: `game` — hasp_login(3), ReInitAladdinDongles(2), DongleInitialize() FAILED, nethasp(6)<br>`HASP/Aladdin`: `utils/rtinit` — hasp_login(3), nethasp(6) | nee | - | een **HASP/Aladdin-dongle** (`game`, `pm/game`, `pm/utils/rtinit`) **en** een **`pm`-map**: de map van de elf moet `pm` heten (`GAME PATH CHECK FAILED`). |
| **GoGoStrike** | nee | geen enkel merkteken in 2 bestanden | nee | - | niets. |
| **HOTDSD** | onbekend | `amDongle-API`: `App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleInit(2), amDongleIsAvailable, amDongleBillingGetKeychipId, amDongle(61)<br>`amDongle-API`: `App/Hodzero/WindowsNoEditor/AMDaemon/hwvalue.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleInit, amDongleIsAvailable, amDongleBillingGetKeychipId, amDongle(59)<br>`amDongle-API`: `pxGetHwinfo.exe` — amDongleInit(2), amDongle(3)<br>`keychip`: `App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe` — Keychip Not Found, GetKeychipId(2)<br>`keychip`: `App/Hodzero/WindowsNoEditor/AMDaemon/hwvalue.exe` — GetKeychipId<br>`keychip`: `App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/Hodzero-Win64-Shipping.exe` — GetKeychipId(3)<br>`amdaemon`: `App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe` — amdaemon(2990)<br>`amdaemon`: `App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64/Hodzero-Win64-Shipping.exe` — EAMDaemonRegionCode(6), amdaemon(1676) | nee | - | De game-exe vraagt de regio aan **amdaemon**, maar de amdaemon-service die met de game meekomt (`AMDaemon/amdaemon.exe`, `AMDaemon/hwvalue.exe`) roept zélf `amDongleGetRegion` en `amDongleSetupKeychip` aan. Het profiel heeft **geen** `DongleRegion`/`PcbRegion`, dus er is geen regiowaarde om te antwoorden. Nodig om te beslissen: de game starten en het amdaemon-logboek lezen, of meten welk antwoord `amDongleGetRegion` in het daemonproces krijgt. De eigenaar noemt dit al een open punt (`scripts/amdaemon.py`: "de dongle/keychip die de daemon zelf controleert"). |
| **Halo** | nee | `HASP/Aladdin`: `g7/halo/Patch` — nethasp(8)<br>`HASP/Aladdin`: `g7/halo/game` — nethasp(14)<br>`HASP/Aladdin`: `g7/halo/game_original` — nethasp(8)<br>`HASP/Aladdin`: `g7/halo/halo.so` — hasp_login(4), nethasp(2)<br>`HASP/Aladdin`: `g7/halo/utils/rtinit` — nethasp(7)<br>`HASP/Aladdin`: `g7/halo/utils/rtinit.so` — hasp_login(4), nethasp(2)<br>`HASP/Aladdin`: `g7/halo/utils/rtinit_original` — nethasp(7) | nee (wel `kind: hasp`) | - | een **HASP-dongle**; die is al vastgelegd als `kind: hasp`. |
| **HauntedMuseum** | nee | `eigen ini`: `game.exe` — MUSEUM.INI<br>profiel `UserProfiles/HauntedMuseum.xml`: `Region = E` | nee | - | een **bestandswaarde**: de REGION-regel in zijn eigen `MUSEUM.ini`. Geen dongle. |
| **HauntedMuseumII** | nee | `eigen ini`: `game.exe` — HAUNTED2.INI<br>`HASP/Aladdin`: `game.exe` — nethasp(7)<br>profiel `UserProfiles/HauntedMuseumII.xml`: `Region = E` | nee | - | een **bestandswaarde**: de REGION-regel in zijn eigen `haunted2.ini`. Daarnaast zit er een HASP/Aladdin-laag in `game.exe`. Geen dongle van het soort region. |
| **InfinityBladeFX** | nee | `matrix/SBK`: `Binaries/Win32/SwordGame.exe` — matrix32<br>`matrix/SBK`: `Binaries/Win32/matrix32.TEKNOPARROT.dll` — matrix32<br>`matrix/SBK`: `Binaries/Win32/matrix32.dll` — matrix32(4) | nee (wel `kind: matrix`) | - | een **matrix/SBK-dongle** (`matrix32.dll`); al vastgelegd als `kind: matrix`. |
| **JurassicPark** | nee | `HASP/Aladdin`: `game` — hasp_login(3), ReInitAladdinDongles(2), DongleInitialize() FAILED, nethasp(10)<br>`HASP/Aladdin`: `game_original` — hasp_login(3), ReInitAladdinDongles(2), DongleInitialize() FAILED, nethasp(10)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_100624.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_112983.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_114576.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_5176.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_93341.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_93898.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_96728.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtdata/libhasp_linux_98111.so` — hasp_login(4), nethasp(5)<br>`HASP/Aladdin`: `utils/pmrtstart` — hasp_login(6), nethasp(5) | nee (wel `kind: hasp`) | - | een **HASP-dongle**; die is al vastgelegd als `kind: hasp`. |
| **LGI** | ja | `amDongle-API`: `LGI_RingW_F_safe.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleOpen(2), amDongleBillingGetKeychipId, amDongle Ver., amDongle(14)<br>`regio-leesroute`: `LGI_RingW_F_safe.exe` — readRegionEeprom(3), readRegionBuSram(4), setupRegionEeprom(3), setupRegionBuSram(4)<br>`terugval`: `LGI_RingW_F_safe.exe` — DefaultRegion, Region is JPN., Region is USA., Region is EXP., Region is CHN.<br>`keychip`: `LGI_RingW_F_safe.exe` — keychip.appboot.region, Keychip Not Found, KeychipError, GetKeychipId<br>profiel `UserProfiles/LGI.xml`: `DongleRegion = JAPAN` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA)<br>profiel `UserProfiles/LGI.xml`: `PcbRegion = JAPAN` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA) | **ja** — regio **JAPAN** / printplaat **JAPAN**, `enforced = false` | profiel `DongleRegion = JAPAN`; game.ini `DongleRegion = JAPAN`; profiel `PcbRegion = JAPAN`; game.ini `PcbRegion = JAPAN` | - |
| **LGI3D** | ja | `amDongle-API`: `LGI.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleOpen(2), amDongleBillingGetKeychipId, amDongle Ver., amDongle(14)<br>`regio-leesroute`: `LGI.exe` — readRegionEeprom(3), readRegionBuSram(4), setupRegionEeprom(3), setupRegionBuSram(4)<br>`terugval`: `LGI.exe` — DefaultRegion, Region is JPN., Region is USA., Region is EXP., Region is CHN.<br>`keychip`: `LGI.exe` — keychip.appboot.region, Keychip Not Found, KeychipError, GetKeychipId<br>profiel `UserProfiles/LGI3D.xml`: `DongleRegion = USA` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA)<br>profiel `UserProfiles/LGI3D.xml`: `PcbRegion = USA` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA) | **ja** — regio **USA** / printplaat **USA**, `enforced = false` | profiel `DongleRegion = USA`; game.ini `DongleRegion = USA`; profiel `PcbRegion = USA`; game.ini `PcbRegion = USA` | - |
| **LGS** | ja | `amDongle-API`: `GameSampR_RingWide.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleOpen(2), amDongleInit(2), amDongleBillingGetKeychipId, amDongle Ver., amDongle(20)<br>`amDongle-API`: `GameTestR_RingWide.exe` — amDongleGetRegion, amDongleSetupKeychip, amDongleOpen(2), amDongleInit(2), amDongleBillingGetKeychipId, amDongle Ver., amDongle(20)<br>`terugval`: `GameSampR_RingWide.exe` — Wrong Region<br>`keychip`: `GameSampR_RingWide.exe` — keychip.appboot.region, Keychip Not Found(2), KeychipError, GetKeychipId<br>`keychip`: `GameTestR_RingWide.exe` — keychip.appboot.region, KeychipError, GetKeychipId<br>profiel `UserProfiles/LGS.xml`: `DongleRegion = USA` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA)<br>profiel `UserProfiles/LGS.xml`: `PcbRegion = USA` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA) | **ja** — regio **USA** / printplaat **USA**, `enforced = false` | profiel `DongleRegion = USA`; game.ini `DongleRegion = USA`; profiel `PcbRegion = USA`; game.ini `PcbRegion = USA` | - |
| **LethalEnforcers3** | nee | geen enkel merkteken in 8 bestanden | nee | - | niets. |
| **LuigisMansion** | nee | `amDongle-API`: `oxGetHwInfo.exe` — amDongleInit(2), amDongle(3)<br>**let op:** wel `amDongle`, maar **géén** `amDongleGetRegion` en **géén** `readRegionEeprom`/`readRegionBuSram` — dus geen regio-leesroute | nee | - | niets. |
| **MedaruNoGunman** | nee | `amDongle-API`: `exe/gunman_dxD.exe` — amDongleSetupKeychip, amDongleOpen(2), amDongleInit, amDongle Ver., amDongle(12)<br>`amDongle-API`: `exe/gunman_dxF.exe` — amDongleSetupKeychip, amDongleOpen(2), amDongle Ver., amDongle(8)<br>`amDongle-API`: `exe/gunman_dxR.exe` — amDongleSetupKeychip, amDongleOpen(2), amDongle Ver., amDongle(8)<br>`terugval`: `exe/gunman_dxD.exe` — Wrong Region<br>`terugval`: `exe/gunman_dxF.exe` — Wrong Region<br>`terugval`: `exe/gunman_dxR.exe` — Wrong Region<br>`keychip`: `exe/gunman_dxD.exe` — keychip.appboot.region, Keychip Not Found, KeychipError<br>`keychip`: `exe/gunman_dxF.exe` — keychip.appboot.region, Keychip Not Found, KeychipError<br>`keychip`: `exe/gunman_dxR.exe` — Keychip Not Found, keychip.appboot.region, KeychipError<br>**let op:** wel `amDongle`, maar **géén** `amDongleGetRegion` en **géén** `readRegionEeprom`/`readRegionBuSram` — dus geen regio-leesroute | nee | - | de **keychip** van de Lindbergh/Ring-familie (facturatie, tracelading, SSD-bewijs) — een eigen laag. Beslissing van de eigenaar, onderbouwd in `patches/MedaruNoGunman/README.md`. |
| **MusicGunGun2** | nee | geen enkel merkteken in 11 bestanden | nee | - | niets. |
| **NERF** | nee | `HASP/Aladdin`: `BepInEx/plugins/NERFTPPlugin.dll` — hasp_login(2)<br>`HASP/Aladdin`: `Nerf_Data/Managed/Assembly-CSharp.dll` — hasp_login<br>`HASP/Aladdin`: `Nerf_Data/Plugins/x86_64_ori/libhasp_linux_23714.so` — hasp_login(4), nethasp(7)<br>`HASP/Aladdin`: `Plugins/x86_64/libhasp_linux_23714.so` — hasp_login(4), nethasp(7)<br>`HASP/Aladdin`: `utils/led_screen_programmer` — hasp_login(4), nethasp(7)<br>`HASP/Aladdin`: `utils/pmrtstart.x86_64` — hasp_login(6) | nee | - | een **HASP-dongle in managed code**: `Nerf_Data/Managed/Assembly-CSharp.dll` (`UDongleControl`) met de BepInEx-plugin `BepInEx/plugins/NERFTPPlugin.dll` als antwoord. Geen HASP-API-oppervlak in het Windows-spel. |
| **OG** | ja | `amDongle-API`: `gs2.exe` — amDongleSetupKeychip, amDongleOpen(2), amDongleInit, amDongleIsAvailable, amDongle Ver., amDongle(11)<br>`amDongle-API`: `gs2.exe.ori` — amDongleSetupKeychip, amDongleOpen(2), amDongleInit, amDongleIsAvailable, amDongle Ver., amDongle(11)<br>`amDongle-API`: `gs2.exe_1080` — amDongleSetupKeychip, amDongleOpen(2), amDongleInit, amDongleIsAvailable, amDongle Ver., amDongle(11)<br>`terugval`: `gs2.exe` — WRONG REGION<br>`terugval`: `gs2.exe.ori` — WRONG REGION<br>`terugval`: `gs2.exe_1080` — WRONG REGION<br>`keychip`: `gs2.exe` — keychip.appboot.region, KEYCHIP ACCESS FAILED, KeychipError, getKeychipID<br>`keychip`: `gs2.exe.ori` — keychip.appboot.region, KEYCHIP ACCESS FAILED, KeychipError, getKeychipID<br>`keychip`: `gs2.exe_1080` — keychip.appboot.region, KEYCHIP ACCESS FAILED, KeychipError, getKeychipID<br>`regionaam`: `gs2.exe` — KeyRegion:, BoardRegion:<br>`regionaam`: `gs2.exe.ori` — KeyRegion:, BoardRegion:<br>`regionaam`: `gs2.exe_1080` — KeyRegion:, BoardRegion:<br>profiel `GameProfiles/OG.xml`: `DongleRegion = JAPAN` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA)<br>profiel `GameProfiles/OG.xml`: `PcbRegion = JAPAN` (keuzes: JAPAN|USA|JAP2N|EXPORT|CHINA)<br>**let op:** leest de regio via `keychip.appboot.region`, niet via `amDongleGetRegion` | **ja** — regio **JAPAN** / printplaat **JAPAN**, `enforced = false` | profiel `DongleRegion = JAPAN`; game.ini `DongleRegion = JAPAN`; profiel `PcbRegion = JAPAN`; game.ini `PcbRegion = JAPAN`; findings | - |
| **OmatsuriYasanGansoTakoyaki** | nee | geen enkel merkteken in 2 bestanden | nee | - | niets. |
| **OmatsuriYasanKingyoSukui** | nee | geen enkel merkteken in 2 bestanden | nee | - | niets. |
| **Otomedius** | nee | geen enkel merkteken in 10 bestanden | nee | - | niets. |
| **PacmanChomp** | nee | `HASP/Aladdin`: `game` — hasp_login(4), nethasp(12)<br>`HASP/Aladdin`: `game_original` — hasp_login(4), nethasp(10)<br>`HASP/Aladdin`: `pmrtstart` — hasp_login(10)<br>`HASP/Aladdin`: `pmrtstart.x86_64` — hasp_login(6) | nee | - | een **HASP/Aladdin-dongle** (`pmrtstart`, `pmrtstart.x86_64`, `game`). |
| **PigsMightFly** | nee | geen enkel merkteken in 13 bestanden | nee | - | niets. |
| **RabbidsHollywood** | nee | `matrix/SBK`: `Game_Data/Managed/Assembly-CSharp.dll` — matrix64<br>`matrix/SBK`: `Game_Data/Managed/Assembly-CSharp.dll.original` — matrix64<br>`matrix/SBK`: `Game_Data/Plugins/matrix32.dll` — matrix32<br>`matrix/SBK`: `Game_Data/Plugins/matrix64.dll` — matrix64<br>`matrix/SBK`: `matrix32.dll` — matrix32<br>`matrix/SBK`: `matrix64.dll` — matrix64 | nee (wel `kind: matrix`) | - | een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`, `Game_Data/Plugins/matrix/matrix.ini`); al vastgelegd als `kind: matrix`. |
| **RaccoonRampage** | nee | geen enkel merkteken in 26 bestanden | nee | - | niets. |
| **RainbowBombergirl** | nee | geen enkel merkteken in 83 bestanden<br>profiel `UserProfiles/RainbowBombergirl.xml`: `Region = EXPORT` (keuzes: JAPAN|EXPORT) | nee | - | het **profielveld `Region`** op de 64-bits kern (de ALLS/amdaemon-route). Geen dongle. |
| **Rampage** | nee | `matrix/SBK`: `Game_Data/Managed/Assembly-CSharp.dll` — matrix64<br>`matrix/SBK`: `Game_Data/Plugins/matrix32.dll` — matrix32<br>`matrix/SBK`: `matrix32.dll` — matrix32<br>`matrix/SBK`: `matrix64.dll` — matrix64 | nee (wel `kind: matrix`) | - | een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`); al vastgelegd als `kind: matrix`. |
| **TC5** | nee | `HASP/Aladdin`: `RSLauncher.exe` — hasp_windows(3)<br>`HASP/Aladdin`: `TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe` — hasp_windows(3)<br>`HASP/Aladdin`: `TC5/Binaries/Win64/hasp_windows_x64_105293.dll` — hasp_login(4), nethasp(6), hasp_windows<br>`HASP/Aladdin`: `TC5/Binaries/Win64/hasp_windows_x64_106993.dll` — hasp_login(4), nethasp(6), hasp_windows<br>`HASP/Aladdin`: `hasp_windows_x64_105293.dll` — hasp_login(4), nethasp(6), hasp_windows | nee (wel `kind: hasp`) | - | een **HASP-USB-dongle** (`hasp_windows_x64_105293.dll`, `hasp_windows_x64_106993.dll`); al vastgelegd als `kind: hasp`. De regio-variabelen in `RSLauncher.exe` (`region_id`, `varBoolRegionJPN/CHN/INT`) zijn profiel-/configuratievlaggen, geen dongle-leesroute. |
| **TankTankTank** | onbekend | **geen bestanden op de NAS om te onderzoeken** (de game staat er niet) | nee | - | De game staat **niet op de NAS** (`n_tank_release`, ElfLdr2, profiel `DeadHeat`), dus er is geen executable om te onderzoeken. Het profiel heeft geen `DongleRegion`/`PcbRegion`/`Region`. Nodig: de spelbestanden. |
| **Terminator** | nee | `HASP/Aladdin`: `ServiceDongleApp/ServiceDongleApp` — DongleInitialize() FAILED, hasp_login(3), ReInitAladdinDongles, nethasp(5)<br>`HASP/Aladdin`: `game` — ReInitAladdinDongles, DongleInitialize() FAILED, nethasp(5)<br>`HASP/Aladdin`: `t4-patch` — hasp_login(3), nethasp(6) | nee (wel `kind: hasp`) | - | een **HASP-dongle** (`ServiceDongleApp`, `t4-patch`); al vastgelegd als `kind: hasp`. |
| **TombRaider** | nee | `matrix/SBK`: `Game_Data/Managed/Assembly-CSharp.dll` — matrix64<br>`matrix/SBK`: `Game_Data/Plugins/matrix32.dll` — matrix32<br>`matrix/SBK`: `matrix32.dll` — matrix32<br>`matrix/SBK`: `matrix64.dll` — matrix64 | nee (wel `kind: matrix`) | - | een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`); al vastgelegd als `kind: matrix`. |
| **TransformersShadowsRising** | nee | `regionaam`: `Transformers2.exe` — REGION_CHN, REGION_EXP, REGION_USA<br>`DK2-donglescan`: `Shell.exe` — MainThreadDongleScan(4), DongleUID | nee | - | een **DK2-sleutel** die getrapt wordt gezocht (`MainThreadDongleScan` in `Shell.exe`) — de eigen donglesoort `sega-scan`, niet `region`. Het spel is speelbaar terwijl de scan faalt (`Cabinet SKU set to UNKNOWN`). |

## Tellingen

| oordeel | aantal | games |
|---|---|---|
| **ja** (vraagt een regio/printplaat-dongle) | 4 | **LGI**, **LGI3D**, **LGS**, **OG** |
| **nee** | 29 | zie de tabel |
| **onbekend** | 3 | **3DCosplayMahjong**, **HOTDSD**, **TankTankTank** |
| games samen | 36 | |

Wat de games met `nee` dan wél vragen, in groepen:

| wat | games |
|---|---|
| een REGION-regel in hun eigen ini (Taito Type X2, geen dongle) | GaiaAttack4, HauntedMuseum, HauntedMuseumII |
| het profielveld `Region` op de 64-bits kern (geen dongle) | RainbowBombergirl |
| een HASP/Aladdin-dongle (`kind: hasp`) | GalagaAssault, Halo, JurassicPark, PacmanChomp, Terminator, GhostBusters, TC5, NERF |
| een matrix/SBK-dongle (`kind: matrix`) | InfinityBladeFX, RabbidsHollywood, Rampage, TombRaider |
| een DK2-sleutel (`kind: sega-scan`) | TransformersShadowsRising |
| de amdaemon-laag (`kind: amdaemon`) | *HOTDSD staat op `onbekend`: de amdaemon-service roept zélf `amDongleGetRegion` aan* |
| de keychip van de Lindbergh/Ring-familie (eigen laag) | MedaruNoGunman |
| niets | Gashaaaan, Gashaaaan2, GoGoStrike, LethalEnforcers3, LuigisMansion, MusicGunGun2, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, Otomedius, PigsMightFly, RaccoonRampage |

<!-- END TABEL -->

## De vier regio-dongles in detail

Alle vier hebben een `dongle.json` met `kind: region` in
`patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json`, met `enforced = false`. Dat laatste is geen
omissie: de 32-bits TeknoParrot-kern kent `DongleRegion` en `PcbRegion` niet, de games vragen de
regio aan hardware die er niet is, en ze gaan verder met hun eigen `DefaultRegion`
(`docs/region-layer.md` §1.3 en §8).

| Game | exe (gemeten) | regio | printplaat | bron van de waarde | afgedwongen? |
|---|---|---|---|---|---|
| LGI | `LGI_RingW_F_safe.exe` | JAPAN (JPN) | JAPAN (JPN) | profiel `UserProfiles/LGI.xml` + `GameProfiles/LGI.xml`, `General/DongleRegion` en `General/PcbRegion` | nee — **gemeten**: de game is speelbaar |
| LGI3D | `LGI.exe` | USA | USA | idem, `UserProfiles/LGI3D.xml` | nee — **gemeten**: speelbaar |
| LGS | `GameSampR_RingWide.exe` | USA | USA | idem, `UserProfiles/LGS.xml` | nee — **gemeten**: speelbaar |
| OG | `gs2.exe` (NAS-map `OAV6DU~3`) | JAPAN (JPN) | JAPAN (JPN) | `GameProfiles/OG.xml`, `General/DongleRegion` en `General/PcbRegion` (beide Dropdown JAPAN\|USA\|JAP2N\|EXPORT\|CHINA) | nee — **aanname, niet gemeten**: de game is nooit gestart (`[Status] = untested`); zie `enforced_basis` in het `dongle.json` |

Er is **geen regiowaarde verzonnen**: elke waarde komt uit het TeknoParrot-profiel van die game, en
`patches/regio-gen.py` leest dat profiel en zet de waarde in de kolom "waar komen de regiowaarden
vandaan". De vier games zijn ook de enige vier met een `DongleRegion`/`PcbRegion`-optie in
`packaging/game-options.json` (`operation_ghost`, `lets_go_island`, `lets_go_island_3d`,
`lets_go_safari`) — dat is een tweede, onafhankelijke bevestiging dat de verzameling precies deze
vier is.

**Geen enkel `dongle.json` toegevoegd in deze ronde**, en dat is de uitkomst van de meting: elke game
waarvan het bewijs een regio/printplaat-dongle aantoont had er al een. Voor de vier bestaande is het
bewijs in deze ronde opnieuw uit de executable op de NAS gehaald en klopt het met wat er in de
bestanden staat. Voor **OG** is de executable in deze ronde voor het eerst bereikt: hij staat in de
NAS-map **`OAV6DU~3`** (een 8.3-korte naam) terwijl het profiel geen `GamePath` heeft.

## Open punten

1. **OG: het gereedschap vindt de executable niet, de gegevens kloppen.** `python3 scripts/region.py
   --game OG --json` meldt *"de executable van OG noemt geen enkele amDongle- of regioleesroute"* en
   geeft `ok: false`. Dat is **geen fout in de gegevens maar een leeg beeld**: `region.py` zoekt de
   gamemap in `$RAVENGUN_GAME_DIR`, dan `[Game] GamePath`, dan het `GamePath` van het profiel. LGI,
   LGI3D en LGS hebben een `UserProfiles/<Game>.xml` met daarin een `GamePath` naar de NAS; **OG heeft
   dat gebruikersprofiel niet** (nooit in TeknoParrotUi geopend) en `GameProfiles/OG.xml` heeft een
   leeg `GamePath`. De map heet bovendien `OAV6DU~3` (8.3-korte naam). Met de map erbij is hetzelfde
   gereedschap **groen**:

   ```bash
   RAVENGUN_GAME_DIR=/home/deck/mnt/spidernas_emulation/Teknoparrot/OAV6DU~3 \
     python3 scripts/region.py --game OG --json
   # "errors": [], "enforced": false
   # merktekens: "amDongle Ver." 1x, "keychip.appboot.region" 1x, "REGION" 5x
   ```

   `scripts/launch-direct.py OG --plan` vindt de game **wel** (via de view: `views/OG/gs2.exe`,
   exitcode 0) en de drop-in komt aan op `views/OG/ravengun-hasp/OG/dongle.json`. **Nodig om dit te
   sluiten:** `region.py` de gamemap laten oplossen zoals `ownprofile.py`/`launch-direct.py` dat doen
   (eerst een override, dan de view). Dat is een wijziging in de loader-repo, en die is van de
   eigenaar.
2. **OG: `enforced` is een aanname.** De exe is nu nagelopen, maar de game is nooit gestart. De
   aanname staat in `enforced_basis` in het `dongle.json`; wie de game start moet die regel vervangen
   door een meting.
3. **HOTDSD staat daarom op `onbekend`: er zit wél een `amDongle`-regioaanroep in de
   amdaemon-service.** De game-exe vraagt de regio aan amdaemon (`EAMDaemonRegionCode`,
   `GetRegionCode`), maar `AMDaemon/amdaemon.exe` en `AMDaemon/hwvalue.exe` roepen
   `amDongleGetRegion` en `amDongleSetupKeychip` aan. Daarmee is het geen zuivere `nee`: er wordt
   in de processen van deze game wel degelijk om een regio gevraagd, alleen één proces verderop.
   Tegelijk is het geen `ja` voor deze kaart: de donglesoort is **`amdaemon`** en niet `region`, en
   het profiel heeft **geen** `DongleRegion`/`PcbRegion`, dus er is geen regiowaarde om in te vullen
   (en een waarde verzinnen mag niet). De eigenaar noemt dit al een open punt: `scripts/amdaemon.py`
   beschrijft het als *"de dongle/keychip die de daemon zelf controleert"* met als *nodig* "een
   antwoord op `amDongle*` binnen het daemonproces". **Nodig om te beslissen:** de game starten en
   het amdaemon-logboek lezen, of meten welk antwoord `amDongleGetRegion` in het daemonproces
   krijgt. Daarna is dit óf `nee` (de daemon valt terug) óf `ja` met soort `amdaemon`.
   `patches/regio-gen.py` meldt dit zelf als tegenstrijdigheid zolang het oordeel `nee` is — dat is
   precies waarom het hier `onbekend` is.
4. **MedaruNoGunman: `region.py` meldt hem rood.** `python3 scripts/region.py --game MedaruNoGunman`
   zegt *"vraagt een regio-dongle, maar de beschrijving ontbreekt"*, omdat de executable `amDongle`
   noemt. De eigenaar heeft met bewijs vastgelegd dat dit de **keychip** is en geen regio/printplaat-
   dongle (`patches/MedaruNoGunman/README.md`), en er is geen regiowaarde om te antwoorden (het
   profiel `FarCry` heeft geen `DongleRegion`/`PcbRegion`). De README noemt dit zelf een open punt van
   de laag, geen fout. **Nodig om te sluiten:** een eigen `kind` voor de keychip in de tabel van
   `docs/dropins.md`, en de regiowaarde meten (testmenu of een log van `keychip.appboot.region`).
5. **3DCosplayMahjong en TankTankTank: geen executable.** Deze twee games staan niet op de NAS, dus
   er is geen bestandsevidence. Hun profielen hebben geen `DongleRegion`/`PcbRegion`/`Region`, wat
   op `nee` wijst, maar dat is niet gemeten aan de game zelf. **Nodig om te beslissen:** de
   spelbestanden op de NAS zetten en `python3 patches/regio-gen.py` opnieuw draaien.
6. **`Transformers.xml` is een ander spel.** In `GameProfiles/` staat een profiel `Transformers.xml`
   met `DongleRegion = USA` en `PcbRegion = USA`; dat hoort bij **Transformers: Human Alliance**
   (`EmulationProfile SegaJvsLetsGoIsland`, exe `TF_Gun_R_Ring_dumped.exe`) en **niet** bij
   Transformers: Shadows Rising uit deze repo (`TransformersShadowsRising.xml`, zonder regioveld).
   Wie de twee verwart, zet een verkeerde regiodongle op deze game.
7. **De `matrix`-treffers in `D3DX9_*.dll` en `UnityPlayer.dll` zijn ruis.** "Matrix" is daar een
   wiskundige term. De echte matrix-dongle is te zien aan `matrix32.dll`/`matrix64.dll` in de
   spelmap. Deze kaart telt alleen die twee, plus `SBK`.
