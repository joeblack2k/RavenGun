# Onderzoek: het regio/PCB-donglemechanisme van de Sega-titels

*Ronde 6 (2026-10-11). Dit is het ruwe onderzoek achter `docs/region-layer.md`: wat er gezocht is,
wat er gevonden is, en welke sporen doodliepen. De conclusie staat in `docs/region-layer.md`.*

De vraag was: **waar leest de game de regio?** Een bestand, een registerwaarde, een aanroep naar een
DLL, of een waarde in `teknoparrot.ini` die de TeknoParrot-kern naar het spel schrijft?

Het antwoord is geen van de eerste drie en maar half het vierde: de waarde staat wél in
`teknoparrot.ini`, maar **de game leest die ini nooit**. De game vraagt de regio aan hardware
(dongle/keychip, EEPROM, backup-SRAM) die in emulatie ontbreekt, en de kern die deze games draait
beantwoordt die vraag niet. De games spelen omdat de controle niet fataal is.

---

## 1. Wat er in de game-executables staat

Commando (op de Deck, de NAS alleen lezend):

```bash
N="/home/deck/mnt/spidernas_emulation/Teknoparrot"
strings -a -n 4 "$N/Let's Go Island Lost on the Island of Tropics/LGI_RingW_F_safe.exe" \
  | grep -i -E 'region|dongle|pcb|keychip|amDongle'
```

### LGI (`LGI_RingW_F_safe.exe`, PE32, i386) en LGI3D (`LGI.exe`, PE32)

Beide zijn hetzelfde `fwAM`-bouwsel (Sega's AM-framework). Wat er staat:

```
..\..\src\Fraw\fwRegion.cpp
Region[%s] is not found!
Region num is over! Max%d. Please call function setRegionMax.
Keychip Not Found
fwAM::getSerialID SerialID[%s] KeychipID[%s]
DefaultRegion
fwAM> readRegionEeprom [%s] OK
fwAM> readRegionErprom ERR! %d
fwAM> setupRegionEeprom ERR! %d
fwAM> readRegionBuSram [%s] OK
fwAM> setupRegionBuSram ERR! %d
fwAM::entryRegion [%s] %d addr%x
Unknown region!
Region is CHN.
Region is EXP.
Region is USA.
Region is JPN.
[teaArcade2] Dongle::initialize() failed. but reason is unknown...
  amDongleGetPlatformId() is failed.
  amDongleGetSystemFlag() is failed.
  amDongleGetModelType() is failed.
  amDongleGetRegion() is failed.
  amDongleGetGameId() is failed.
  amDongleBillingGetKeychipId() is failed.
[teaArcade2] JvsManager::initCredit() unknown region, cannot adjust credit setting.
```

Wat dit zegt:

* De regio komt uit **twee** plekken: de **dongle/keychip** (`amDongleGetRegion`) en het bord
  (**EEPROM** via `readRegionEeprom`, **backup-SRAM** via `readRegionBuSram`).
* Er is een **terugval**: `DefaultRegion`. De game heeft dus een eigen standaardwaarde voor als er
  niets te lezen valt.
* De game kent **vier** regionamen: `JPN`, `USA`, `EXP`, `CHN` (`Unknown region!` als het er geen van
  is).

De `am`-familie zit **statisch in de exe** (geen `am*.dll` in de importtabel: LGI importeert
`ADVAPI32, GDI32, KERNEL32, MSVCP90, MSVCR90, SETUPAPI, SHELL32, USER32, WINMM, WS2_32, cg, cgD3D9,
d3d9, d3dx9_40, ole32`). De hardware eronder wordt met `SetupAPI` gezocht en met `DeviceIoControl`
aangestuurd — dezelfde route als in `mxGetHwInfo.exe`:

```
amDipswCreateDeviceFile   SetupDiEnumDeviceInterfaces Error(%ld).
amEepromCreateDeviceFile  SetupDiGetClassDevs Error(%ld).
amDipswReadByteInternal   DeviceIoControl Error(%ld).
```

De `amDongle`-functies hebben zelf **geen** string in de buurt (het zijn dunne wikkels om de driver),
dus de aanroepen zijn alleen indirect te vinden.

### LGS (`GameSampR_RingWide.exe`, PE32, i386)

LGS gebruikt niet `fwAM` maar de `amDongle`-bibliotheek rechtstreeks:

```
amDongle Ver.1.12 Build:Sep  4 2012 00:17:40
amDongleInit  amDongleOpen  amDongleOpenEx  amDongleBusy  amDongleSendAndRecvEx
amDongleSetupKeychip  amDongleGetGameId  amDongleGetModelType  amDongleGetRegion
amDongleGetNetworkAddress  amDongleGetVersion  amDongleGetPicVersion
amDongleBillingGetKeychipId  amDongleResponseCheck  amDongleUpdate
keychip.version  keychip.appboot.region  keychip.appboot.gameid  keychip.appboot.platformid
keychip.appboot.modeltype  keychip.appboot.networkaddr  keychip.appboot.systemflag
keychip.ssd.proof  keychip.ds.compute  keychip.encrypt  keychip.decrypt
Error Response KeychipError !!!
```

De regio zit hier in het **keychip**-veld `keychip.appboot.region`, op te vragen met
`amDongleGetRegion`. LGS importeert ook geen `am*.dll`.

### `mxGetHwInfo.exe` (de platformcontrole uit `game.bat`)

```
gethwinfoInit
ERROR: amSramInit failed.     ERROR: amInstallInit failed.   ERROR: amEepromInit failed.
ERROR: amBackupInit failed.   ERROR: amDipswInit failed.     ERROR: amMasterInit failed.
getRegion
ERROR: Read Save Data Failed. Errno %d.
compareOsVersion  compareMemorySize  compareSramSize  compareAppdataSize  comparePatchSize
compareVertical   compareBoardType  compareGraphicDevice  compareSoundDevice
checkOtherPciDevice  checkUsbDevice
ERROR: Get Region failed.
```

`getRegion` staat direct vóór `ERROR: Read Save Data Failed` — de regio wordt uit **save data**
gelezen (het backup-SRAM), niet uit een ini. De rest van het programma vergelijkt de machine met
`app.ini`/`mxGetHwInfo.ini` (OS-versie, geheugen, SRAM-grootte, bordtype, grafische en geluidschip,
resolutie, PCI- en USB-apparaten).

### De games lezen `teknoparrot.ini` niet

```bash
for f in LGI_RingW_F_safe.exe LGI.exe GameSampR_RingWide.exe mxGetHwInfo.exe; do
  echo -n "$f: "; strings -a -n 4 "$f" | grep -ci teknoparrot
done
# 0 0 0 0        (en ook Rainbow Bombergirls game.exe: 0)
```

Nul treffers. De game kan die ini dus niet lezen, wat de waarde ook is.

---

## 2. De gedecrypteerde kern

De TeknoParrot-kernen zijn gepackt (secties met `rawsize = 0`, code in een hoog-entropische
`teknoGod`-sectie; de stub decryptet bij `LoadLibrary`). De methode staat in `docs/14_unpacking.md`:
een host die de DLL laadt (`re/TpHost32.exe` / `TpHost64.exe`), daarna het procesgeheugen dumpen
(`re/dump_proc.py`).

Beschikbare beelden in `cores/dump/`:

| Beeld | Grootte | Entropie (eerste 4 MB) |
|---|---|---|
| `TpHost32_8296_691b0000_TeknoParrot_dll.bin` | 137.261.056 | 6,86 |
| `BudgieLoader_628_10ff0000_TeknoParrot_dll.bin` | 137.117.696 | 6,86 |
| `TpHost64_7612_7ffbc7d40000_TeknoParrot64_dll.bin` | 109.129.728 | 6,37 |

De dumps zijn leesbaar (entropie 6,4–6,9; de gepackte sectie was 8,00) en bevatten de exports en de
strings. De 32-bits beelden zijn dezelfde DLL (beide 137 MB, dezelfde 88 `TeknoParrot`-strings).

### Tellen

```python
d = open(bin, "rb").read()
d.count(b"DongleRegion")   # 0 in beide
d.count(b"Region")         # 0 in 32-bits, 5 in 64-bits
d.count(b"amDongle")       # 0 in 32-bits, 1 in 64-bits
d.count(b"JAPAN")          # 0 in 32-bits, 1 in 64-bits
```

Uitgebreider, met de vraag of de *stringtabel wel volledig ontsleuteld is* (anders is een 0 niets
waard):

| Tekst | 32-bits | 64-bits |
|---|---|---|
| `teknoparrot.ini` | 3 | ja |
| `FreePlay` | 1 | ja |
| `Windowed` | 1 | ja |
| `HideCursor` | 1 | ja |
| `Enable Outputs` | 1 | ja |
| `FFB Blaster` | 1 | ja |
| `Input API` | 0 | ja |
| `DisableHardcodedKeys` | 0 | ja |
| `SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10` | 4 | – |
| `Region` | **0** | 5 |
| `DongleRegion` / `PcbRegion` | **0 / 0** | **0 / 0** |
| `amDongle` / `amEeprom` / `amSram` / `amJvs` | **0** | `amDongleGetNetworkAddress: %d` |
| `JAPAN` / `USA` / `JAP2N` / `EXPORT` / `CHINA` | **0** | `JAPAN`, `EXPORT` |
| `System_getRegionCode` / `AllnetAuth_getRegionCode` | **0** | ja |
| `System\Sega\SystemProperty\keychip` | **0** | ja |
| unieke strings (≥5 tekens) | 365.120 | 425.756 |

De stringtabel is aantoonbaar ontsleuteld: de 32-bits kern kent `teknoparrot.ini` en precies de
configuratiesleutels die hij nodig heeft (`FreePlay`, `Windowed`, `HideCursor`, `Enable Outputs`,
`FFB Blaster`) en ook de JVS-identiteit van het bord `837-14572`. Dat hij `Input API` en
`DisableHardcodedKeys` niet kent, klopt met het ontwerp: die horen bij de UI (invoer), niet bij de
kern. De afwezigheid van `DongleRegion`, `PcbRegion` en `Region` is dus een echte afwezigheid en geen
ontsleutelingsgat.

**Conclusie:** de 32-bits kern kan `DongleRegion` en `PcbRegion` niet eens bij naam opzoeken, en kent
geen enkele `amDongle`-aanroep. Voor de Ring/Lindbergh-games (32-bits) is het regioveld dood gewicht.

### Waar de regiocode wél zit

In de 64-bits kern, en dat is een andere familie:

```
Region
JAPAN
EXPORT
System_getRegionCode
AllnetAuth_getRegionCode
AllnetAuth_getRegionName
AllnetAuth_getRegionNamePartCount
amDongleGetNetworkAddress: %d
amNetwork emulator: cannot listen on TCP %u (error %d) - another emulator already serves it
System\Sega\SystemProperty\keychip
System_getBoardId
System_getKeychipId
DummyCA1 / DummyPKI0 / -----BEGIN CERTIFICATE-----
BLOCKING LUIGI COM PORT
```

Dit is de **ALLS/amdaemon**-emulatie (Sega's nieuwere platform: HOTD Scarlet Dawn, Transformers
Shadows Rising, en de 64-bits route van Rainbow Bombergirl). `amDongleGetNetworkAddress` hoort bij
die amdaemon-laag, niet bij de RingWide-dongle. Dat de `amNetwork`-emulator "cannot listen on TCP"
meldt, sluit aan op de eerdere meting dat de kern de `amMaster`/`amLib`-aanroepen naar poort 40100
**in het proces** beantwoordt (`findings/LetsGoSafari.md`).

---

## 3. Het profielveld `Region` is iets anders

In `libs/TeknoParrotUi.Common.dll` (UTF-16-strings) staat:

```
JAPAN   USA   EXPORT                       <- naast "Error configuring cxbxr: " en "cxbxr-ldr"
Region="                                   <- naast "MINIGUN.INI", "GA4", "MUSEUM.INI", "HAUNTED2.INI"
Region   Default   --region=               <- bij een opdrachtregel-optie
teknoparrot.ini   [GameInfo] GameVersion=  [GlobalHotkeys] ExitKey=  [GlobalScore]
```

Drie verbruikers van het veld `Region`, geen van drie een dongle:

1. **cxbxr** (Xbox): de UI schrijft `Region="JAPAN"` (of `--region=`) in de cxbxr-configuratie.
2. **De Taito Type X2-geweren**: de UI schrijft een `REGION`-regel in `MINIGUN.INI`, `MUSEUM.INI` of
   `HAUNTED2.INI`. Dat is de lijst die in de UI naast `Region="` staat. `scripts/launch-direct.py`
   doet dat al in `taito_gun_ini()`.
3. **De 64-bits kern** (WartranTroopers/amdaemon-route), o.a. Rainbow Bombergirl.

Bewijs dat het bij de Taito-games echt een **bestand** is:

```
$ cat "/home/deck/mnt/spidernas_emulation/Teknoparrot/Gaia Attack 4/MINIGUN.INI"
REGION		E
CNFNAME		.\OpenParrot\GA4
RANKFILE	.\OpenParrot\
PRJENABLE   	1
...
$ strings -a -n 4 ".../Gaia Attack 4/game.exe" | grep -i -E 'MINIGUN|REGION|CNFNAME'
Minigun
MINIGUN.INI
REGION
CNFNAME
```

En zo ook `Haunted Museum/MUSEUM.ini` (`REGION  E`, `CNFNAME .\OpenParrot\HM`) en
`Haunted Museum II/haunted2.ini` (`REGION  E`, `CNFNAME .\OpenParrot\HM2`). De exe van
HauntedMuseumII noemt zelfs `regione`, `regionc` en `regionj` — de drie regiocode-paden van die game.

`E` = Export. De waarde is dus een letter in een tekstbestand dat de game zelf leest, en er is geen
dongle bij nodig.

---

## 4. Dynamische proeven op de Deck

### 4.1 `mxGetHwInfo.exe` in een eigen prefix

```bash
# view ~/.local/share/ravengun/runtime/views/RgnProbe met mxGetHwInfo.exe + app.ini + mxGetHwInfo.ini
WINEPREFIX=~/.local/share/ravengun/prefixes/rgnprobe \
  ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine ./mxGetHwInfo.exe mxGetHwInfo.ini
```

Uitkomst: alleen `mxgethwinfo Ver.1.03 Build:Sep  3 2009 16:46:23`, daarna **exit 1**. Geen
`ERROR: Get Region failed.` op stdout of stderr — de foutmeldingen van `amiDebug` gaan niet naar de
console.

Met `WINEDEBUG=+file` en met `strace -f -e trace=openat,ioctl,connect`:

* geen enkel bestand met "region" in de naam;
* geen enkel apparaatpad (`\\.\…`) behalve Wine's eigen pipes (`svcctl`, `wine_plugplay`, …);
* wel: `SetupDiGetClassDevsExW`, `SetupDiEnumDeviceInfo`, `SetupDiGetDeviceRegistryPropertyW`,
  `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\DeviceClasses\{…}` en
  `CreateFileW L"\\\\?\\unix/dev/sda1"` … (Wine somt schijven en HID-apparaten op).

De regio wordt dus niet uit een bestand gelezen; het programma zoekt hardware en faalt.

### 4.2 LGI zelf, 45 s met de apparaattrace

```bash
# view ~/.local/share/ravengun/runtime/views/RgnLGI met symbolische links naar de NAS-bestanden
# en een eigen teknoparrot.ini (DongleRegion=JAPAN, PcbRegion=JAPAN)
WINEPREFIX=~/.local/share/TeknoParrotUI/prefixes/ravengun-test-lgi \
WINEDEBUG=+file,+setupapi,+reg \
  ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine ./LGI_RingW_F_safe.exe
```

94.783 regels trace, exitcode 5. Wat er in staat:

* de game leest `data\Fraw\fwDataDB\fwDataDB_Application.txt` (de `fwAM`-databank met
  venster- en rendermaten) — dus de game start `fwAM` echt op;
* de game enumereert HID-apparaten (`{4D1E55B2-F16F-11CF-88CB-001111000030}` 1707×,
  `{745A17A0-74D3-11D0-B6FE-00A0C90F57DA}` 469×);
* **geen** apparaatpad voor een dongle, **geen** `eeprom`/`sram`/`keychip`/`dongle` in de trace, en
  **geen enkele keer "region"**;
* de game opent niets buiten de view en de prefix.

Zonder de kern haalt de game de dongle-aanroep niet (hij stopt op exitcode 5), dus dit bewijst niet
dat de aanroep nooit gebeurt — het laat wel zien dat er in de vroege start geen regiobestand wordt
gelezen. Een volgende meting moet met de kern erbij, of met een drop-in die `CreateFileW` en
`DeviceIoControl` logt.

### 4.3 Wat niet werkte

* `strace` op de Wine-processen geeft vooral Wine-infrastructuur; bruikbaar is alleen
  `WINEDEBUG=+file,+setupapi,+reg`.
* `strings` op `SegaApi.dll` (9,7 MB, in `TeknoParrot/`) en op `TeknoParrot.dll` zelf geeft ruis: die
  zijn gepackt (secties `.tek0`, `.tek1`, `.tek2`). Alleen de gedecrypteerde beelden zijn bruikbaar.
* `cores/OpenSegaAPI_Opensegaapi.zip` → `Opensegaapi.dll` is **niet** de `am*`-API maar de
  Sega-*geluids*API (`SEGAAPI_Init`, `SEGAAPI_Play`, `SEGAAPI_SetSynthParam`, 31 exports). Hetzelfde
  geldt voor `SegaApi.dll`: dat is de geluidsbibliotheek van de Lindbergh-games, geen regiobron.
* `data/Fraw/fwDataDB/fwDataDB_Application.txt` en de rest van `data/` bevatten geen regiowaarden
  (`grep -i 'JPN|USA|EXP|CHN|region'` → niets). De regiodefinitie zit in de exe (`fwRegion.cpp`,
  `entryRegion`).
* De OpenParrot-bron (`third_party/openparrot/`, commit `fc8ba632`) bevat **niets** over regio's: geen
  `DongleRegion`, geen `PcbRegion`, geen `SegaJvs*`-spelmodule (de `Games/`-map heeft APM3, BNA1,
  ES3B, ES3X, Europa-R, Ex-Board, Nesica, Other, TypeX2, TypeX4). De Sega Ring-games zitten daar niet
  in, dus daar is de regio ook niet af te kijken.

---

## 5. Wat dit betekent

* De regiowaarden van LGI/LGI3D/LGS zijn **gegevens**, geen code. Ze horen in
  `patches/<Game>/dongle.json` (`kind: "region"`), met `enforced: false` zolang de kern ze niet
  gebruikt.
* Er is **geen binary** nodig zolang de numerieke regiocodering en het dongle-apparaat onbekend zijn.
* Zodra die twee bekend zijn, is de plek `patches/<Game>/dropin/` met een sectie in `dropin.ini`
  (`Preload = true`, `Env = RAVENGUN_REGION_GAME=<Game>`), zonder codewijziging in de launcher.
