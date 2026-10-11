# Het amdaemon-mechanisme van HOTD Scarlet Dawn — metingen

*Ronde 12 (2026-10-11). Alleen lezen. Niets op de NAS geschreven, geen spel gestart, geen git.
De conclusies staan in [`docs/amdaemon-layer.md`](../amdaemon-layer.md); dit is het bewijs eronder.*

## 0. Werkwijze

Alles op de Steam Deck, met de NAS alleen-lezen gemount op
`/home/deck/mnt/spidernas_emulation/Teknoparrot`. De hulpmiddelen: `strings` (ASCII en UTF-16LE met
`-e l`), `objdump -p/-h`, `xxd`, `dd`, en Python 3.14 zonder extra pakketten (geen `pefile`; de
PE-kop en de sectietabel zijn met `struct` gelezen). De langere commando's staan als `.sh` in
`/home/deck/rg-amdaemon/`; de uitvoer is hieronder overgenomen.

De spelmap is `/home/deck/mnt/spidernas_emulation/Teknoparrot/House of the Dead Scarlet Dawn`,
hieronder `$G`. De daemon staat in `$G/App/Hodzero/WindowsNoEditor/AMDaemon`, de game in
`$G/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64`.

**Wat ik niet heb gedaan:** het spel starten, `amdaemon.exe.original` starten, de gesloten kern
`TeknoParrot64.dll` uitpakken, of iets in de spelmap schrijven. Wat dat voor gevolgen heeft voor de
zekerheid van een bewering staat er telkens bij.

## 1. De twee bestanden naast elkaar

| Bestand | Grootte |
|---|---|
| `AMDaemon/amdaemon.exe` | 8.272.384 |
| `AMDaemon/amdaemon.exe.original` | 4.567.040 |
| `Win64/Hodzero-Win64-Shipping.exe` | 54.128.128 |
| `Win64/Hodzero-Win64-Shipping_org.exe` | 51.470.336 |

`python3 /home/deck/rg-amdaemon/secties_nauwkeurig.py` (niet-nul bytes en leesbare reeksen per
sectie):

```
=== GEPATCHT amdaemon.exe (8272384 bytes) ===
  naam           raddr      rsize     niet-nul   percentage   tekst8  tekst40
  .text     0x00000400 0x004aca00      4388485       89.54%     3383       21
  .rdata    0x004ace00 0x00224e00      1328698       59.10%     4430     1582
  .data     0x006d1c00 0x0005da00       313801       81.83%     7687     1559
  .pdata    0x0072f600 0x000ae000       534307       74.97%      149        6
  .rsrc     0x007dd600 0x00001000         4080       99.61%        0        0
  .idata    0x007de600 0x00005400        16753       77.91%      563       74
  overlay na de secties: 0 bytes

=== ORIGINEEL amdaemon.exe.original (4567040 bytes) ===
  GAT 0x338..0x455000: 4541640 bytes, 4517150 niet-nul (99.46%), 1040 leesbare reeksen van 8+
  naam           raddr      rsize     niet-nul   percentage   tekst8  tekst40
  .text     0x00455000 0x00001000         2766       67.53%        3        0
  .rdata    0x00456000 0x00001000          131        3.20%        7        0
  .data     0x00457000 0x00001000            0        0.00%        0        0
  .pdata    0x00458000 0x00001000            0        0.00%        0        0
  .rsrc     0x00459000 0x00001000          398        9.72%       10        4
  .reloc    0x0045a000 0x00001000            1        0.02%        0        0
  overlay na de secties: 0 bytes

=== _org game-exe (51470336 bytes) ===
  GAT 0x440..0x1cd7000: 30239680 bytes, 30095216 niet-nul (99.52%), 6694 leesbare reeksen van 8+
  naam           raddr      rsize     niet-nul   percentage   tekst8  tekst40
  .text     0x01cd7000 0x00001000         2766       67.53%        3        0
  .rdata    0x01cd8000 0x00f1f000         2578        0.02%       73        9
  .data     0x02bf7000 0x00508000            0        0.00%        0        0
  .pdata    0x030ff000 0x00001000            0        0.00%        0        0
  .tls      0x03100000 0x00001000            1        0.02%        0        0
  .gfids    0x03101000 0x00001000            0        0.00%        0        0
  _RDATA    0x03102000 0x00001000            0        0.00%        0        0
  .rsrc     0x03103000 0x00012000        57948       78.60%       43        7
  .reloc    0x03115000 0x00001000           12        0.29%        0        0
  overlay na de secties: 0 bytes
```

Wat daaruit volgt:

1. **De werkende bestanden zijn normale PE's.** Elke sectie heeft echte inhoud (59–100% niet-nul),
   er is geen overlay, en `amdaemon.exe` heeft een `.idata` van 21.504 bytes met 22 DLL's. `_org` en
   `.original` hebben géén `.idata`; hun enige import is `KeRnEl32.dLl!LoadLibraryA`.
2. **De "originelen" zijn lege hulzen.** In beide bestanden is `.data` **volledig nul** (0 van 4.096
   bytes) en `.pdata` ook. `.rdata` is 3,2% (amdaemon) respectievelijk 0,02% (de game) niet-nul; het
   niet-nulle deel zit aan het eind en bevat precies de importtabel:
   `xxd -s 0x2bf5c00` in `_org` toont een aaneengesloten rij namen
   (`agsDriverExtensionsDX11_*`, `NvOptimusEnablement`, `PrintScriptCallstack`,
   `Hodzero-Win64-Shipping.exe`).
3. **Het `.text`-stuk is in beide bestanden hetzelfde.** 2.766 niet-nul bytes van 4.096, en beide
   beginnen met dezelfde twintig bytes:
   `58 51 52 53 55 56 57 41 50 41 51 41 52 41 53 41 54 41 55 41 56 41 57 48`
   (`pop rax` en dan alle registers pushen). Het entrypoint 0x1b5c valt midden in dit stuk, op een
   rij kleine thunks (`e8 9f f4 ff ff c3` = `call …; ret`). De stub importeert via
   `LoadLibraryA`/`GetProcAddress`; de naamstrings `GetModuleHandleA`, `GetCurrentProcess`,
   `GetCurrentProcessId` staan er ook.
4. **Tussen de sectietabel en de eerste sectie staat een groot blok.** 4.541.640 bytes (amdaemon) en
   30.239.680 bytes (de game), voor 99,5% niet-nul, met 7,9–8,0 bits/byte entropie per 4 KiB. De
   verhouding tot het werkende bestand is in beide gevallen **0,55** — consistent met een
   gecomprimeerde of versleutelde afbeelding van de secties. Het blok is niet uniform: er zitten
   brokken leesbare tekst in, dus het is geen pure cijfertekst.
5. **Geen enkele Sega-string in de originelen.** `strings -n 5` op `amdaemon.exe` geeft
   `amdaemon` 2990, `Dongle` 97, `aime` 51, `Keychip` 22, `amMaster` 15, `amdaemonExe` 1 keer; op
   `amdaemon.exe.original` geeft elk van die namen **0**. Voor de game: `amdaemon` 1473 in de
   werkende exe, 0 in `_org`.
6. **Geen enkele TeknoParrot-string in de werkende bestanden.** `strings -n 5` en
   `strings -e l -n 5` op `amdaemon.exe` én op `Hodzero-Win64-Shipping.exe` geven **0** op
   `TeknoParrot`, `teknoparrot`, `OpenParrot` en `Parrot`.

**Wat dat betekent.** De +3.705.344 en +2.657.792 bytes zijn **uitgepakte Sega-inhoud**, geen
TeknoParrot-runtime. De bewering in `findings/antwoorden_scarlet_dawn.md` §1 ("er is 3,7 MB en
2,66 MB aan code bijgezet — dat is een volledige runtime die in de bestanden is geschoven") is met de
bytes niet te bevestigen: de werkende exe's hebben dezelfde PE-tijdstempel als hun huls, dezelfde
DOS-stub, dezelfde imagebase, en er zit geen TeknoParrot-naam in.

**En de "originelen terugzetten"-route is niet te verifiëren.** Of een protector het blok bij het
starten uitpakt (en het origineel dus gewoon loopt) heb ik niet getest — daar is Wine en een start
voor nodig. Wat vaststaat is dat de code er statisch niet in staat, dat `.data` en `.pdata` leeg
zijn, en dat er geen Sega-naam in voorkomt. Voor de TeknoParrot-route is een *leesbaar* bestand
bovendien een vereiste: gepakte code laat zich niet haken.

## 2. Wat `amdaemon.exe` zelf is

```
grootte 8.272.384, PE32+ x64, imagebase 0x140000000, entry 0x2268a4, subsystem 3 (console)
linkerversie 11.0, tijdstempel 0x5ad8a419 = 2018-04-19T14:13:45Z
secties: .text .rdata .data .pdata .rsrc .idata
```

Versieregel: `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37`.
Bouwserverpaden: `D:\jenkins_work\workspace\all_build\apps\amdaemon\src\...` en
`D:\jenkins_work\workspace\all_build\libs\libamw\src\...`.

Geïmporteerde DLL's (22): `IPHLPAPI.DLL advapi32 bcrypt cfgmgr32 combase crypt32 dbghelp dnsapi gdi32
hid kernel32 mpr msvcp110 msvcr110 setupapi shell32 shlwapi user32 winhttp winmm wlanapi ws2_32`.

### 2a. De diensten voor de game: gedeeld geheugen, geen poort

`strings -e l -n 5` op `amdaemon.exe` geeft 3.794 regels. De naamruimten, geteld op de ASCII-strings:

```
   1339 amdaemon::process::      369 amdaemon::ipc::      189 amdaemon::server::
     71 amdaemon::util::          46 amdaemon::config::    13 amdaemon::arcade::
```

Uit `amdaemon::ipc::`:

```
amdaemon::ipc::SharedMemory::Impl::Impl        amdaemon::ipc::makeDaemonShared
amdaemon::ipc::DaemonSharedPart                amdaemon::ipc::SharedDataList::Impl::browse
amdaemon::ipc::SharedDataList::Impl::getAddress
```

en per dienst een `Client…Request` / `Server…Response`-paar: `abaas`, `aime`, `allnet`, `apm`,
`backup`, `boardio`, `can`, `credit`, `emoney`, `error`, `input`, `jvs`, `network`, `output`,
`process`, `sequence`.

De mutexen die erbij horen:

```
%u:ALIVEHANDLEMUTEX:%x   %u:ALIVEVALUEMUTEX:%x   %u:LOGHANDLEMUTEX:%x   %u:LOGVALUEMUTEX:%x
%u:LEDHANDLEMUTEX:%x     %u:LEDVALUEMUTEX:%x     %u:LIBMUTEX:%x         %u:LPNTDBMUTEX:%x
%u:LPNTNFCMUTEX:%x       %u:COMPMUTEX:%x         %u:NFCBufMutex:%x      %u:R/W MUTEX:%x
%u:DBMUTEX:%x
```

De serverkant, uit `amdaemon::server::` (189 namen):

```
JvsController::loadInput        JvsController::updateNodeInfo      JvsNodeRequestExecutor::doAccept
CreditController::loadData      CreditController::loadAvailable    CreditController::accept
SequenceController::beginPlay   SequenceController::continuePlay   SequenceController::endPlay
BackupRequestExecutor::save     BackupRequestExecutor::load        BackupRequestExecutor::setGameId
AimeUnitRequestExecutor::loadAimeId   EmoneyController::completePay
BoardIoController::accept       ProcessController::accept          NetworkController::loadTestData
RequestExecutor<…>::doAccept    RequestState
```

**Geen named pipe.** `grep -i pipe` op de ASCII-strings geeft twaalf regels en die zijn allemaal
iets anders: `sendToInterruptPipe`, `recvFromInterruptPipe`, `sendToControlPipe`,
`recvFromControlPipe` (de eigen hulpthreads van de daemon) en curl's HTTP-pipelining-tekst. Op de
UTF-16-strings is `pipe`, `Pipe`, `Local\` en `Global\` **0**.

**Geen PCPA.** `pcpa` komt één keer voor, als deel van `pcPathLengthConstraint`; `pcpaOpenClient`,
`pcppGetSockAddr` en `libpcp` komen niet voor. De poortconstanten van de Ring-titels ook niet:
`python3 /home/deck/rg-amdaemon/pcpa_poorten.py` zoekt `40100 40102 40103 40106 40107 40114 40115`
als little-endian én als tekst, in beide werkende bestanden — nul treffers in de `mov eax`-vorm, en
de losse twee-byte-treffers liggen *onder* wat je bij toeval in een bestand van die grootte verwacht
(3–14 waar ~126 verwacht). Zie ook [`amdaemon-minime.md`](amdaemon-minime.md).

**Maar hij kán een server zijn.** De importtabel van `ws2_32.dll` bevat `bind`, `listen` en
`WSAAccept`, naast `connect`, `WSAConnect`, `getaddrinfo`, `select`, `WSARecv` en `WSASend`. Welke
poort, en of de game die gebruikt, is **onbekend**.

### 2b. De ALL.Net-kant

```
amMaster Build:Mar 19 2018 10:42:15 ($Rev: 6911 $)
amMasterInit  amMasterExitA  amMasterExitInternal  amMasterUpdate  amMasterGetResult
amMasterIsDevelop  amMasterGetProcessCount  amMasterOutputLog  amMasterEraseLog
amMasterReconnectUsbDevice  amMasterSetBackupEventLogParam  amMasterGetBackupEventLogParam
Error amMasterInit!!   Error amMasterExit!!
```

Daarnaast de facturatielaag `alpb*` (ALL.Net Payment/Billing), met onder meer
`alpbExStartAccountingReport`, `alpbGetKeychipSerialID`, `alpbExGetKeychipSerialID`,
`alpbSaveSettings`, `alpbZlibCompress`, `ALPB_DONGLE::BillingGetKeychipId`, en de bron
`D:\Jenkins\workspace\allnet_build_for_amx\src\lib\alib\src\alpb_dongle.cpp`. De aanroep naar buiten
is een query op een HTTP-achtige dienst:

```
keychipid=%s&functype=%u&gameid=%s&gamever=%s&boardid=%s&tenpoip=%s&libalibver=%s&datamax=%s&billingtype=%d&protocolver=%s&operatingfix=%d
```

(Dat is de **opmaakregel**, geen waarde; er staan geen gegevens in dit document.)

### 2c. De dongle/keychip

```
am::dongle::Manager::setup / loadInfo   (libamw, amw_dongle_manager.cpp)
amDongleInit  amDongleOpen  amDongleIsAvailable  amDongleSetupKeychip
amDongleGetGameId  amDongleGetModelType  amDongleGetRegion
amDongleGetNetworkAddress  amDongleGetSystemFlag
amDongleReadNvram  amDongleGetGeometryNvram  amDongleEncrypt  amDongleDecrypt
amDongleBillingGetKeychipId  amDongleBillingGetPlayCount  amDongleBillingUpdatePlaylimit
amyDongleDriverStart  amyDongleDriverEnd  amyDongleDriverStartNvram  amyDongleDriverEndNvram
amyDongleDriverStartGetLog  amyDongleDriverEndGetLog
amyDriverEnumDevice_  amyDriverSyncronizeDeviceIoControl
```

Het apparaatpad is `\??\FddDriver`, de foutregels zijn `Keychip Not Found`, `Keychip Access Failed`,
`Error : Invalid Keychip ID : %s`, `Retry Keychip Access.` en `Error : keychip mode is wrong.`.
De `amy*`-laag is dezelfde die in `hwvalue.exe` zit (`amyLogInit`, `amyLogBackupEventlog`).

### 2d. Het USB-IO-bord en de AiMe-lezer

`hid.dll`: `HidD_GetHidGuid`, `HidD_GetProductString`, `HidD_GetManufacturerString`,
`HidD_GetInputReport`, `HidD_SetOutputReport`. `setupapi.dll`: `SetupDiGetClassDevsW/A`,
`SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailW`, `SetupDiOpenDevRegKey`.
`cfgmgr32.dll`: `CM_Get_Device_ID_ExA`. Verder `DeviceIoControl` en de seriële API's
(`GetCommState`, `SetCommState`, `SetCommTimeouts`, `SetupComm`, `PurgeComm`, `ClearCommError`).
De lezer-firmware staat in `AMDaemon/aime_firm/`: `TN32MSEC003S_V12.hex`, `837_15084_1_01.hex`,
`837-15286-P_LPC1112_NFC_RW_LED_BD_0x92.bin`, `update_15396_6728_93.bin`, `update_15396_6728_94.bin`.

### 2e. Het register — hij leest **en schrijft**

```
objdump -p amdaemon.exe | grep -iE 'Reg[A-Za-z]+'
  RegCloseKey  RegCreateKeyExW  RegOpenKeyExW  RegQueryValueExW  RegSetValueExW
  RegQueryValueExA  RegDeleteValueW  RegEnumKeyExW
  (RegisterEventSourceW/A en DeregisterEventSource horen bij de EventLog, niet bij het register)
```

Sleutels die in de strings staan:

```
System\SEGA\SystemProperty\mount                 (met de waarden AMFS en APPDATA)
System\Sega\SystemProperty\amVideo               (port_1..port_5; 05_SetVideoPort.bat schrijft die)
System\Sega\SystemProperty\keychip
System\SEGA\SystemProperty\Static
System\SEGA\SystemProperty\Master / Install / installer / sgworker / wirelessNetwork
System\Sega\SystemProperty\sgsetdisplaysetting\CurrentSetting
SOFTWARE\TFPaymentService\
SYSTEM\CurrentControlSet\services\Tcpip\Parameters\Interfaces\%s
\Registry\Machine\SYSTEM\CurrentControlSet\Control
```

en de functies die dat doen: `amsSystemPropertySetMountAmfs`, `amsSystemPropertyGetMountAmfs`,
`amsSystemPropertySetMountAppdata`, `amsSystemPropertyGetMountAppdata`, `amsSystemPropertyMountInit`,
`amsSystemPropertyStaticInit`, `amsSystemPropertyGetKeychipId`, `amsInstallerGetIcfDriveLetter`,
`amsInstallerGetAmfsFreeSpace`, `getAmfsDriveLetter`, `amfs_share$`.

> **Correctie op mijn eigen tussenstand.** Ik had eerst "hij leest het register maar schrijft het
> nooit" opgeschreven, op grond van een `grep` die alleen naar `RegQueryValue` en `RegOpenKey` keek.
> Dat was fout: `RegSetValueExW` en `RegCreateKeyExW` staan wél in de importtabel. De daemon
> **publiceert de mountpunten zelf**.

### 2f. Wat hij verder nog doet

`SetSystemTime`, `SetLocalTime`, `SetTimeZoneInformation` (de klok), `GlobalMemoryStatusEx`
(geheugen), `CreateProcessW`, `OpenProcess`, `ReadProcessMemory`, `VirtualQueryEx`,
`K32EnumProcessModulesEx` (hij kijkt in andere processen — waarvoor is onbekend),
`QueryDosDeviceW`, `FindFirstVolumeW`, `FindNextVolumeW`, `GetVolumeInformationW` (schijven
opsommen), `LoadLibraryW` (`aime_rw_adapterMD.dll`, `thincatcapclient.dll`, `indicdll.dll`).

En wat hij **niet** doet: de hardware-eis controleren. `pxGetHw` komt **0** keer voor.

## 3. `config.json` en `pxGetHwInfo.ini`

`AMDaemon/config.json` (3.171 bytes) — samengevat, met alleen de velden die de laag raken:

| Veld | Waarde |
|---|---|
| `common.max_player` | 2 |
| `common.language` | english |
| `error.auto_reset` | **true** |
| `video.enable` / `resolution` | true / 1920×1080 |
| `video.ignore_unconnected` | true |
| `credit.enable` / `max_credit` | true / 24 |
| `credit.config.game_cost` | `[-1,-1,1,1,-1,-1,-1,-1]` |
| `usb_io.enable` | true |
| `usb_io.node[0].required_function` | `coin_input` (slot 2), `switch_input` (2 spelers, 8 knoppen), `general_output` (slot 20), `unique_io` (commando 65, param [6]) |
| `input.system` | 9 ingangen (`bakuon`, `eye0`, `proximity_cabinet`, `emoney_left/right/center`, `cabinet_kind`, `test`, `service`) |
| `input.players` | 2 × (`start`, `gun_select`, `gun_special`) |

`error.auto_reset = true` beantwoordt de oude vraag of een herstart verwacht gedrag is: ja.

`pxGetHwInfo.ini` (990 bytes) — de eisen, na het afknippen van de Japanse commentaren:

| Sleutel | Waarde |
|---|---|
| `[Application] appname` | `"game.bat"` |
| `[System] platform-id` | **ACA0** (ALLS UX) |
| `[System] sub-storage` | 0 |
| `[Hardware] mem-size` | 16384 |
| `[Hardware] appdata-size` | 17000 |
| `[Hardware] graphics-board-lane-width` | 16 |

**Wie controleert dat?** `pxGetHwinfo.exe` (701.440 bytes) staat in de spelmap, en de `appname` in
het ini-bestand wijst naar `game.bat`. Maar `game.bat` start `pxGetHwinfo.exe` **niet**, en
`App/Startup/AppStartup.bat` en `99_StartApp.bat` ook niet — die starten
`05_SetVideoPort.bat`, `10_SetResolution.bat`, `20_SetAudioVolume.bat` en `99_StartApp.bat`. In deze
kopie van het spel wordt de hardware-eis dus **nergens afgedwongen**. (In de TeknoParrot-route wordt
`game.bat` zelfs helemaal niet gebruikt.)

## 4. De schijfletters

In `$G/App/Hodzero/WindowsNoEditor/AMDaemon` staat een map die daar niet hoort:

```
TeknoParrot/AMFS/emoney/            (leeg)
TeknoParrot/AMFS/sysfile.dat        24576 bytes
TeknoParrot/D/SDET/appfile.dat      8388608 bytes   (tijdstempel 11 okt 09:02 — door de daemon geschreven)
TeknoParrot/Y/                      (leeg)
```

`find "$G/App/Hodzero/WindowsNoEditor/Hodzero/Binaries/Win64" -iname 'TeknoParrot*'` geeft alleen
`teknoparrot.ini`; de lettermap staat dus **naast `amdaemon.exe`**, niet naast de game-exe.

De werkmap van de batchketen is `App/Hodzero` (via `pushd ..\Hodzero` in `99_StartApp.bat`), terwijl
de lettermap in `WindowsNoEditor/AMDaemon/` staat. De omleiding rekent dus vanaf de **map van de
exe**, niet vanaf de werkmap. Dat past bij de beschrijving van het TeknoParrot-team
(`findings/antwoorden_scarlet_dawn.md` §2: de kern onderschept `CreateFileA` en leidt elke letter
naar `.\TeknoParrot\<LETTER>\`).

**Het is geen Wine-schijfmapping.** Op de Deck zijn alle `dosdevices`-mappen van alle Wine-prefixes
nagelopen (`find /home/deck -maxdepth 6 -type d -name dosdevices`): zeven stuks, plus de prefix van
`hasp-test-prefix`. Geen enkele heeft `x:`, `y:` of `d:`; ze hebben `c:`, `z:` en `com1..com33`.

**En de daemon publiceert de letters zelf** (§2e). Waar hij de *eerste* waarde vandaan haalt als er
nog niets in het register staat is onbekend. Kandidaten die in de strings staan:
`System\SEGA\SystemProperty\Static`, `C:\mount\Option\`, en `hwvalue.exe` (202.240 bytes) dat
`modeltype`, `region` en `dipsw` afdrukt en `SYSTEM\SEGA\SYSTEMPROPERTY` noemt.

## 5. De startketen

De keten is `game.bat` → `App/Startup/AppStartup.bat` → `99_StartApp.bat` → `App/Hodzero/StartApp.bat`
(zie [`amdaemon-twee-proces-start.md`](amdaemon-twee-proces-start.md) voor het volledige recept en de
gevolgen). Twee dingen die daaruit volgen en hier thuishoren:

* `firewall.cfg` noemt `X:\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe` en
  `X:\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero.exe` — de image-root is dus `X:\`
  en de spelboom staat onder `X:\App\...`.
* `Dev\AsTarget`, `Dev\AsMiniCabinet` en `Dev\Tools\CheckUseAMDaemon.bat` bestaan in deze kopie
  **niet**. Daardoor kiest `StartApp.bat` `DETAIL_LAUNCH_MODE=cabinet` (config `config.json`), wordt
  de daemon altijd gestart, en is `config_target.json`/`config_cabinetMini.json` — die de eerdere
  ronde nog miste — hier niet aan de orde: die bestanden staan niet in de map en hun tak wordt niet
  gekozen.

## 6. De kern: wat wel en niet vast te stellen is

* **`TeknoParrot64.dll` is zelf gepakt.** In `cores/x_TeknoParrotPkg/TeknoParrot64.dll` (55.209.640
  bytes) staan de secties `.teknoGo` en `.teknoGo$`, en de DLL bevat **geen** leesbare strings over
  ALLS, amdaemon, AMFS of keychip (`Select-String` op 84.538 geëxtraheerde ASCII-strings: 0 treffers
  op elk van die woorden). Wat de kern voor Scarlet Dawn doet is dus **niet statisch te lezen**. Ik
  heb hem niet uitgepakt.
* **Wat wél vaststaat over de invoer:** `EmulationProfile ALLSHOTDSD` hoort bij `HOTDSDPipe`
  (`re/dispatch_map.md` regel 71, uit
  `TeknoParrotUi.Common.GameLaunch.PipeFactory::CreateControlSender`). En
  `re/pipes_il/per/HOTDSDPipe.Transmit.il` laat zien wat dat wegschrijft: op offset 8 een
  `Control`-veld met bit 0 = P1 `Up` (trekker) en bit 1 = P2 `Up`, en op 12/16/20/24 de bytes
  `AnalogBytes[0,2,4,6]`. **Start en test zitten er niet in** — wat de eerdere ronde in
  `findings/HOTDScarletDawn.md` al meldde.
* **Wat het profiel zegt:** `GameProfiles/HOTDSD.xml` heeft `ExecutableName2 = amdaemon.exe`,
  `HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true` en **`ExtraParameters` leeg**.
  TeknoParrotUi start `amdaemon.exe` dus **zonder argumenten** — geen `-c`, geen `-l`. Dat de daemon
  dan toch tot `Process state: Advertise` komt (gemeten, zie `findings/HOTDScarletDawn.md`) betekent
  dat de daemon zonder config.json start en zijn eigen standaardwaarden neemt.

## 7. Onbekend

1. Wat het blok in de "originelen" precies is, en of het origineel nog start.
2. Waar de daemon de eerste mountwaarde vandaan haalt.
3. Of de game ook over een TCP-poort met de daemon praat (hij importeert `bind`/`listen`/`WSAAccept`,
   maar welke poort is niet te vinden).
4. Waarvoor `amdaemon.exe` in andere processen kijkt (`OpenProcess`, `ReadProcessMemory`,
   `K32EnumProcessModulesEx`).
5. Wat de kern van TeknoParrot voor Scarlet Dawn doet: `TeknoParrot64.dll` is gepakt.
6. Of `DebugWithoutAMDaemon` (een string in de game-exe) een modus is waarin de game zonder daemon
   werkt. De string staat in de game-exe maar ik heb hem nergens anders kunnen plaatsen.
