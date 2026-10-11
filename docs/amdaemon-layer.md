# De amdaemon-laag — wat een Sega ALLS-titel nodig heeft

*Ronde 12 (2026-10-11). Dit gaat over het tweede programma van HOTD Scarlet Dawn: `amdaemon.exe`, de
AM Daemon van Sega. Het mapcontract staat in `docs/dropins.md`; dat de lader sinds
`docs/geen-dongle-in-de-loader.md` geen donglebegrip meer heeft en een software-dongle een gewone
drop-in is, wordt hier niet herhaald.*

## Het korte antwoord

**HOTDSD past niet in het per-game-model.** Er valt vandaag niets in `patches/HOTDSD/dropin/` te
zetten, en er is geen `dongle.json` (dat bestaat niet meer). De reden is niet de dongle maar de
**startvorm**: deze game heeft twee programma's nodig waarvan de een de ander moet voorzijn, en de
launcher start er één.

Twee blokkades, in deze orde:

| # | Wat | Soort | Waarom het niet in een map past |
|---|---|---|---|
| 1 | **De twee-programma-start.** `amdaemon.exe` moet draaien vóór `Hodzero-Win64-Shipping.exe`; het profiel zegt het zelf (`HasTwoExecutables`, `LaunchSecondExecutableFirst`) en de eigen batchketen wacht 5 seconden. | **lader-werk** | De launcher weigert zo'n profiel: `raise Unsupported("profielen met twee exe's worden niet direct gestart")` — gemeten, exitcode 4. Een drop-in plaatst bestanden; hij start geen tweede proces. |
| 2 | **Er is geen daemon.** Ook met twee processen moet er iets zijn dat de daemon *is*: de game praat met hem over zijn eigen gedeelde geheugen en mutexen, hij controleert een keychip via `\??\FddDriver`, en zijn config eist een USB-IO-bord. | **engine-werk** | Een drop-in is een bestand in de view plus een omgevingsvariabele. Daarmee maak je geen proces met een IPC-protocol. |

Punt 1 is klein en generiek: **elke** ALLS-titel heeft deze vorm, dus één keer oplossen in de lader
maakt het daarna voor iedereen een map. Punt 2 is het grote stuk: dat is dezelfde soort klus als de
eigen kern in `docs/community-model.md` §2c, maar dan voor de daemon in plaats van voor de game.

Wat er **wel** is: het volledige mechanisme, met bewijs, in
[`docs/research/amdaemon-mechanisme.md`](research/amdaemon-mechanisme.md) en
[`docs/research/amdaemon-twee-proces-start.md`](research/amdaemon-twee-proces-start.md), en een
hulpmiddel dat de laag controleert: `scripts/amdaemon.py`.

## Wat `amdaemon.exe` moet leveren

Alles hieronder is gelezen uit de bestanden in de spelmap en uit `amdaemon.exe` zelf; de commando's
en de uitvoer staan in `docs/research/amdaemon-mechanisme.md`.

| Wat | Bewijs in het bestand |
|---|---|
| **De AM-diensten voor de game, over gedeeld geheugen.** Niet over een poort en niet over een named pipe. | `amdaemon::ipc::` (369 namen) met `SharedMemory`, `DaemonSharedPart`, `SharedDataList`, `makeDaemonShared`; diensten `aime`, `allnet`, `apm`, `backup`, `boardio`, `can`, `credit`, `emoney`, `error`, `input`, `jvs`, `network`, `output`, `process`, `sequence`, `abaas`; mutexen `ALIVEHANDLEMUTEX`, `LOGVALUEMUTEX`, `DBMUTEX`, … |
| **Een aanvraag/antwoord-server** per dienst. | `amdaemon::server::` (189 namen): `JvsController::loadInput`, `CreditController::loadData`, `SequenceController::beginPlay`, `BackupRequestExecutor::save`, `RequestExecutor<…>::doAccept`, `RequestState` |
| **De schijfletters publiceren.** De daemon zet de mountpunten in het register. | importeert `RegSetValueExW` én `RegCreateKeyExW`; strings `amsSystemPropertySetMountAmfs`, `amsSystemPropertySetMountAppdata`, `amsSystemPropertyMountInit`, `System\SEGA\SystemProperty\mount` |
| **De hardware-eigenschappen lezen** die de Sega-boot heeft gezet. | `System\SEGA\SystemProperty\{amVideo,keychip,Static,Master,Install,installer,sgworker,wirelessNetwork,sgsetdisplaysetting\CurrentSetting}`, `\Registry\Machine\SYSTEM\CurrentControlSet\Control` |
| **De dongle/keychip controleren.** | `am::dongle::Manager` (`libamw`, `amw_dongle_manager.cpp`), `amDongleOpen`, `amDongleSetupKeychip`, `amDongleBillingGetKeychipId`, `amDongleGetGameId/ModelType/Region/NetworkAddress/SystemFlag`, `amyDongleDriverStart/End/…`, apparaatpad `\??\FddDriver`, foutregels `Keychip Not Found`, `Keychip Access Failed`, `Retry Keychip Access.` |
| **Het USB-IO-bord vinden en bedienen.** | importeert `hid.dll`, `setupapi.dll`, `cfgmgr32.dll`; `HidD_GetHidGuid`, `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailW`, `DeviceIoControl`; seriële poort-API's (`SetCommState`, `SetupComm`, …) |
| **De AiMe-lezer.** | `aime_firm/` met `TN32MSEC003S_V12.hex`, `837_15084_1_01.hex`, `837-15286-P_LPC1112_NFC_RW_LED_BD_0x92.bin` |
| **De ALL.Net-cliënt naar buiten** (`amMaster`, `alpb*` = ALL.Net Payment/Billing). | `amMasterInit`, `amMasterUpdate`, `amMasterGetResult`, `amMasterGetProcessCount`, `amMasterReconnectUsbDevice`; `amMaster Build:Mar 19 2018 10:42:15 ($Rev: 6911 $)`; importeert `winhttp.dll`, `ws2_32.dll`, `crypt32.dll`, `bcrypt.dll`, `wlanapi.dll` |
| **De klok en het systeem.** | `SetSystemTime`, `SetLocalTime`, `SetTimeZoneInformation`, `GlobalMemoryStatusEx` |

En wat hij **niet** doet: de hardware-eis uit `pxGetHwInfo.ini` controleren. Er staat geen
`pxGetHw`-string in `amdaemon.exe`; dat doet `pxGetHwinfo.exe`, en niets in de startketen start dat
programma (zie `docs/research/amdaemon-mechanisme.md` §5).

## Wat de TeknoParrot-kern hier doet

De eerlijke stand van zaken, met wat gemeten is en wat niet:

| Vraag | Antwoord | Bewijs |
|---|---|---|
| Start TeknoParrot alleen `amdaemon.exe`, of vangt hij ook aanroepen af? | **Beide.** Het profiel start twee exe's (`HasTwoExecutables`, `LaunchSecondExecutableFirst`, `ExecutableName2 = amdaemon.exe`), en de bestanden in de spelmap laten een omleiding zien. | `GameProfiles/HOTDSD.xml`, `GameSetup/HOTDSD.xml` |
| Zit er TeknoParrot-code **in** de twee exe's? | **Nee.** | `strings` op `amdaemon.exe` (8.272.384 B) en `Hodzero-Win64-Shipping.exe` (54.128.128 B) geeft **nul** treffers op `TeknoParrot`, `teknoparrot`, `OpenParrot` en `Parrot`, zowel ASCII als UTF-16. De +3,7 MB en +2,66 MB zijn de **uitgepakte** Sega-inhoud, niet een runtime. |
| Wat is dat verschil dan? | De bestanden naast de exe (`amdaemon.exe.original`, `Hodzero-Win64-Shipping_org.exe`) zijn **geen bruikbare originelen**: hun secties zijn lege hulzen (`.data` en `.pdata` helemaal nul, `.text` 2.766 niet-nul bytes), hun enige import is `KeRnEl32.dLl!LoadLibraryA`, en het grootste deel van het bestand is een blok van 7,9–8,0 bits/byte tussen de kop en de secties. Geen enkele Sega-string komt erin voor. | `docs/research/amdaemon-mechanisme.md` §3 |
| Waar komen de schijfletters dan vandaan? | De daemon **schrijft** de mountpunten zelf in `HKLM\SYSTEM\SEGA\SystemProperty\mount`; iets anders moet die letters naar een map laten wijzen. In de spelmap staat daarvoor `AMDaemon/TeknoParrot/{AMFS,D,Y}` — met `D/SDET/appfile.dat` van 8 MB dat de daemon zelf geschreven heeft. In geen enkele Wine-prefix op deze machine staat een `dosdevices` met `x:`, `y:` of `d:`, dus het is geen Wine-schijfmapping. | `docs/research/amdaemon-mechanisme.md` §4 |
| Doet de kern dat met een `CreateFile`-haak? | **Volgens het TeknoParrot-team wel** (`findings/antwoorden_scarlet_dawn.md` §2: "de core onderschept `CreateFileA` en leidt elke letter naar `.\TeknoParrot\<LETTER>\`"). Dat de map naast de **exe** staat en niet in de werkmap van de keten (`App/Hodzero`) past daarbij. **Zelf heb ik dat niet kunnen vaststellen**: de kern `TeknoParrot64.dll` is zelf gepakt (hij heeft een sectie `.teknoGo`) en bevat geen leesbare strings over ALLS of amdaemon. | `docs/research/amdaemon-mechanisme.md` §6 |
| Wat doet de kern voor de invoer? | Alleen de trekkers en de assen. `EmulationProfile ALLSHOTDSD` → `HOTDSDPipe`, en dat schrijft in het gedeelde geheugen op offset 8 een knopveld (bit 0 = P1-trekker, bit 1 = P2-trekker) en op 12/16/20/24 de vier asbytes. Start en test zitten er niet in. | `re/pipes_il/per/HOTDSDPipe.Transmit.il`; `re/dispatch_map.md` regel 71; `findings/HOTDScarletDawn.md` |

## De twee-programma-start, als recept

Voor wie hem in de lader bouwt. De volledige onderbouwing staat in
[`docs/research/amdaemon-twee-proces-start.md`](research/amdaemon-twee-proces-start.md); het
hulpmiddel drukt dit ook uit als `plan` in zijn JSON.

```
1.  amdaemon.exe   (AMDaemon/)          <- eerst
    argumenten in de eigen keten: -c AMDaemon/config.json -l <log>
    argumenten onder TeknoParrot:  geen (ExtraParameters is leeg)
2.  5 seconden wachten                  (DAEMON_WAIT_SECONDS in App/Hodzero/StartApp.bat)
3.  Hodzero-Win64-Shipping.exe          <- dan
    argumenten: -launch="cabinet"       (DETAIL_LAUNCH_MODE=cabinet)
4.  na het einde van de game: taskkill /im amdaemon.exe
```

De werkmap die de keten opbouwt is `App/Hodzero` (via `pushd ..\Hodzero` in `99_StartApp.bat`), en
beide processen krijgen dezelfde lader en kern — anders haakt de kern niet aan.

## Is `minime` een bruikbare vervanging?

**Nee, en het is ook geen vervanging van `amdaemon`.** `minime` is een **ALL.Net/AiMe-server**: het
vervangt de kant waar de daemon *naartoe* belt, niet de daemon zelf. Het draait op Node.js, opent
poorten 22345 (AiMeDB), 80 (ALL.Net) en 8443 (billing, TLS) plus een paar titelpoorten, en heeft een
eigen SQLite-database en een eigen namaak-PKI. Het levert geen schijfletters, geen USB-IO, geen
keychipcontrole, geen gedeeld geheugen en geen JVS. Het kent het spel-id `SDET` van Scarlet Dawn
zelfs niet.

En voor het **starten** is het niet nodig: zonder ALL.Net-server bereikt de daemon gewoon
`Process state: Advertise` en is de game speelbaar. Zie
[`docs/research/amdaemon-minime.md`](research/amdaemon-minime.md) voor het bewijs en voor het
onderscheid tussen `amdaemon`, de ALL.Net-server en het `TFM`-shellprotocol van de Ring-titels.

## Het hulpmiddel: `scripts/amdaemon.py`

```bash
python3 scripts/amdaemon.py --game HOTDSD              # de status in één regel
python3 scripts/amdaemon.py --game HOTDSD --verbose    # met de blokkades en het startplan
python3 scripts/amdaemon.py --game HOTDSD --json       # alles, machineleesbaar
python3 scripts/amdaemon.py --game HOTDSD --view ~/.local/share/ravengun/runtime/views/HOTDSD
```

Het is **geen launcherhaak** — sinds `docs/geen-dongle-in-de-loader.md` is er geen `kind`-dispatch
meer. Het is een controlehulpmiddel dat de laag uitleest en één vraag beantwoordt: *is hier iets te
plaatsen, of vraagt dit lader-werk?*

Het script is read-only. Het leest `patches/<Game>/game.ini`, `UserProfiles/<P>.xml`, de
`.bat`-keten, `AMDaemon/config.json`, `pxGetHwInfo.ini` en de **namen en aantallen** in
`TeknoParrot/<LETTER>/`. Het leest **nooit** de inhoud van `appfile.dat`, `sysfile.dat`,
`amdaemon.log` of een dongle-image: daar kunnen serienummers en keychip-identificaties in staan. Van
zulke bestanden meldt het alleen of ze er zijn en hoe groot ze zijn.

Wat het teruggeeft (gemeten op deze machine):

```
$ python3 scripts/amdaemon.py --game HOTDSD --json
  ok=False startbaar=False verdict='dit vraagt lader- en engine-werk'
  blokkades: twee-programma-start(lader-werk), schijfletters(lader-werk),
             daemon-koppeling(engine-werk), keychip-in-de-daemon(engine-werk), usb-io-bord(engine-werk)
  dropin.nodig=False
  keten: GAME_ID='SDET' APP_DIR='Y:\SDET' wachttijd='5' IMAGE_ROOT_DIR='X:\'
  daemon: exe=amdaemon.exe grootte=8272384 origineel=amdaemon.exe.original(4567040)
  config: max_player=2 auto_reset=True 1920x1080 usb_io_functies=coin_input,general_output,switch_input,unique_io
  hardware: platform-id=ACA0 mem-size=16384 appdata-size=17000 lane-width=16
  mounts: AMFS=True D=True Y=True
  plan: [('daemon', 5), ('game', 0)]
```

Exitcode 0 betekent "in orde **en** startbaar"; die is er voor HOTDSD dus niet, en dat is de
bedoeling. Wie `--game` weglaat of een game zonder map opgeeft krijgt exitcode 1 met de reden.

## Wat er bewust **niet** in `patches/HOTDSD/` staat

| Bestand | Waarom niet |
|---|---|
| `dongle.json` | Bestaat niet meer (`docs/geen-dongle-in-de-loader.md`). |
| `dropin/dropin.ini` + bestanden | **Er is niets te plaatsen.** De twee blokkades zijn een tweede proces en een daemonproces; geen van beide is een bestand in de view. Een lege `dropin.ini` zou alleen maar suggereren dat deze game in het per-game-model past, en dat doet hij niet. |
| `dropin/amdaemon/…` | Zodra er een eigen daemonvervanger is, hoort die hier: de bibliotheek plus zijn gegevens, en een sectie in `dropin.ini` die de weg geeft via `Env`. Zie `docs/geen-dongle-in-de-loader.md`. |

## Open punten

1. **Wie schrijft de eerste mountwaarde?** De daemon schrijft `System\SEGA\SystemProperty\mount`,
   maar waar haalt hij de letters vandaan als er nog niets staat? Kandidaten: `hwvalue.exe`,
   `System\SEGA\SystemProperty\Static` (dat de Sega-boot hoort te zetten) en `C:\mount\Option`
   (een pad dat in de strings staat). Onbekend.
2. **Kan het origineel nog starten?** `amdaemon.exe.original` is een huls met een versleuteld blok;
   of een protector dat bij het starten uitpakt heb ik niet getest (daar is Wine en een start voor
   nodig). "De originelen terugzetten" is dus geen route die we kunnen verifiëren — en voor de
   TeknoParrot-route is het eerder een vereiste dat de code *leesbaar* is, want gepakte code laat
   zich niet haken.
3. **Praat de game ook over een poort met de daemon?** `amdaemon.exe` importeert `bind`, `listen` en
   `WSAAccept`, dus hij *kan* een server zijn. Welke poort, en of de game die gebruikt, is onbekend:
   de constanten 40100/40102/40103/40106/40107/40114/40115 die de Ring-titels gebruiken komen in
   Scarlet Dawns bestanden niet voor, en `pcpa*`-namen ook niet.
4. **De `CheckUseAMDaemon.bat`-route.** `App/Hodzero/StartApp.bat` kan de daemon overslaan als
   `Dev\Tools\CheckUseAMDaemon.bat` bestaat en 0 teruggeeft. Dat bestand staat niet in deze kopie,
   dus de daemon wordt altijd gestart. Of er een modus is waarin de game zonder daemon werkt, is
   onbekend; de game-exe bevat wel de naam `DebugWithoutAMDaemon`, maar die string heb ik verder
   nergens kunnen plaatsen.
