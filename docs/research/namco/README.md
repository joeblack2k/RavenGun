# Namco- en ALLS-games zonder TeknoParrot-kern: Point Blank X, Mario Kart DX, Time Crisis 5, HOTD Scarlet Dawn

*Stand 2026-10-17. Doel van de eigenaar: elke game start zonder de gesloten `TeknoParrot(64).dll` en zonder bestand uit
`~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64`. Dit stuk gaat over de vier games die op de directe (Wine-)route
nog de gesloten kern laden. **Niets hiervan is op de tv gemeten**: er is niets gestart (geen game, geen Wine); alles is statisch gelezen
(objdump, strings, decompilatie van de Unity-assembly's, de open bronnen van TeknoParrotUI en OpenParrot) en gebouwd in de mingw-container.
Kandidaat-configuraties staan in `docs/research/namco/<Game>.game.ini` en, met hun drop-ins, in `patches/<Game>/game.kandidaat.ini` en `patches/<Game>/dropin.kandidaat.d/`. De meegeleverde
`patches/<Game>/game.ini` en `dropin/` zijn ongewijzigd: elke game blijft op de gesloten kern tot de tv het tegendeel laat zien.**

## Overzicht

| Game | Wat de kern deed | Onze vervanging | Stand |
|---|---|---|---|
| Point Blank X | de USIO-laag (`Plugins/bnusio.dll`: bord via WinUSB, vuurwapen via COM) naar het gedeelde geheugen leiden; schijfletters F:/G: | **`bnusio.dll` van ons** (`tools/rg-usio`, drop-in op de plaats van het echte bestand) + onze kern (generiek, schijfletter-omleiding patch 0004) | **klaar om te testen** |
| Mario Kart DX | JVS: `COM3` -> pipe `TeknoParrot_JVS`; ALL.Net/AMAuthd (COM-server) beantwoorden en naar de TP-gemeenschapsserver wijzen; schijfletters; `teknoparrot.ini` | JVS-over-COM3 in `rg-mfcam.dll` (`tools/rg-mfcam/jvs_com.h`) + onze kern. **ALL.Net/online: niet te vervangen** | **kandidaat, offline** |
| Time Crisis 5 | WinUSB/SetupAPI-emulatie van het USIO-bord in het spelproces, injectie in het kindproces van `RSLauncher.exe`, de HASP-laag | HASP-drop-in bestaat al; USIO-emulatie niet gebouwd | **geblokkeerd** (zie TC5) |
| HOTD Scarlet Dawn | platformgegevens voor `amdaemon.exe`, schijfletters AMFS/D/Y, USB-IO-bord (HID) uit de besturingspipe, twee-exe-start | niets gebouwd | **geblokkeerd + beslissing eigenaar** (zie HOTDSD) |

## Wat een lege Wine-start van deze games mist (de gemeenschappelijke grondslag)

* Onze OpenParrot-kern op de generieke route (`[Loader] Core = ravengun`, patch 0003) laadt elke game zonder per-game hooks. Wat hij wel voor
  iedere game doet (patch 0004): `CreateFileA/W` en `CreateDirectoryA/W` op `<L>:\` of `<L>:/` worden `.\TeknoParrot\<L>\...` (cwd-relatief,
  `C:` en `Z:` nooit). Daarmee is de schijfletterlaag van de gesloten kern gedekt voor Point Blank X (`F:`/`G:` voor `nbamsavdat.dll` en
  `Setting.ini` `MAIN_PATH`/`BACKUP_PATH`) en Mario Kart (`G:/Contents`; de `./F...`-mappen staan al in de exe).
* Per-game hooks horen in de drop-in van de game, niet in de kern (`docs/dropins.md`). De loader laadt **een** preload-DLL per Windows-game
  (`TP_PRELOAD_DLL`); extra reparaties gaan dus in dezelfde DLL (zoals de pHResult-reparatie in `rg-mfcam.dll`).

---

## Point Blank X (EmulationProfile `PointBlankX`, 32-bit Unity, OpenGL)

**Wat het spel van het I/O-bord wil.** `Assembly-CSharp-firstpass.dll` heeft de klasse `BNUsio` met 48 `[DllImport("bnusio")]`-functies
(36 `usio_*`, 12 `gun_*`); `BNUsioController` (Assembly-CSharp.dll) is de enige gebruiker. Het echte `Plugins/bnusio.dll` (21 KB, MSVC 2013)
opent het USIO-bord via SetupAPI + WinUSB en het vuurwapen via een COM-poort (`Setting.ini`: `GUNPORT 2`); zijn exports zijn cdecl en
ongedecoreerd (gemeten: `ret` zonder argument). Wat het spel leest: `usio_GetSwIn()` (start 1P = `0x02000000`, 2P = `0x01000000`, test `0x80`,
service `0x4000`, omhoog `0x2000`, omlaag `0x1000`, enter `0x200`), `usio_GetCoin(0)` + `usio_DecCoin`, `gun_GetPositionX/Y(1|2)` (tienbits
waarde `<< 6`, zo doet het echte bestand het), `gun_IsTriggered`, `gun_IsInRange`, firmwareversie (moet 120..126 zijn). Zonder bord valt het
spel terug op muis/toetsenbord (`Setting.controller`).

**Wat de kern deed.** De gesloten kern haakt die laag in het spelproces en vult hem uit het gedeelde geheugen `TeknoParrot_JvsState`
(TeknoParrotUi `PointBlankPipe`: offset 8 bitveld, offsets 12/16/20/24 de assen van P1/P2). De afbeelding bit -> schakelaar is gesloten
code; wij hebben haar afgeleid uit wat het spel zelf eist (hierboven).

**Vervanging.** `tools/rg-usio/rg_usio.cpp` = `bnusio.dll` met dezelfde 48 exports (`objdump`: namenlijst identiek aan het echte bestand),
gevoed door een eigen pollende draad op het gedeelde geheugen. De logica staat in `tools/rg-usio/usio_model.h` en is getoetst
(`tests/test_usio_model.cpp`, 544 controles): munt/service = rand omhoog -> teller +1 (zoals de muntgever pulst), `DecCoin` niet onder 0,
assen 0..255 -> tienbits `<< 6` (max `0xFFC0`), y omgekeerd (Unity telt y van onderen; de terugval van het spel gebruikt
`Input.mousePosition / Screen`; `RG_USIO_FLIP_Y=0` zet dat uit). Bouwen: `tools/dev/build-usio.sh` (mingw i686 in de gepinde container,
statisch). Plan met de kandidaat: `rg-direct --plan` is `ok`; `core_source: ravengun`; `dropin_files` = `bnusio.dll` in
`PBX100-2-NA-MPR0-A63_Data/Plugins/` (de map wordt lokaal gemaakt, de NAS blijft ongemoeid).

**Niet gedekt / onzeker.** De richting van y is afgeleid, niet gemeten. `gun_IsInRange` is altijd waar (de daemon meldt geen "buiten
beeld"). De ticketautomaat (`TicketIO`, COM) is een ander subsysteem en bleef zoals op de gesloten kern. Terugslag (`gun_StrikeOnce`) doet niets.

**Test op de tv.** (1) kandidaat activeren: `patches/PointBlankX/game.kandidaat.ini` over `game.ini` en `dropin.kandidaat.d/` naar `dropin/`
(of in de gebruikerslaag `~/.local/share/ravengun/patches/PointBlankX/`). (2) dubbelklik. (3) `Plugins/rg-usio.log` in de view: eerste regel
"rg-usio (bnusio.dll) geladen", dan "gedeeld geheugen TeknoParrot_JvsState geopend" en "eerste aanroep: usio_Initialize ... gun_GetPositionX".
Geen log = het spel las het echte bestand. (4) munt (toets 5) -> credit; start; richt op linksboven: het vizier hoort linksboven te staan,
anders `[Env] RG_USIO_FLIP_Y = 0`. (5) 5 minuten spelen. Bewijs van het laden: `/proc/<pid>/maps` van het spelproces bevat
`Plugins/bnusio.dll` met de sha256 uit `dropin.kandidaat.d/`, en bevat **geen** `TeknoParrot.dll`.

---

## Mario Kart Arcade GP DX 1.18 (EmulationProfile `NamcoMkdx`, 32-bit, D3D10/11)

**Wat de kern deed (gemeten aan de exe, niet aan de kern).**

1. **JVS.** `CreateFileA("COM3", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)` op `0x8ac1f2`, daarna
   `GetCommState`/`SetCommState` (115200 8N1, `0x8ac2ab`), `SetCommTimeouts`, `SetupComm`, `PurgeComm`, `GetCommModemStatus` (sense-lijn).
   De kern zet COM3 om naar `\\.\pipe\TeknoParrot_JVS` (zelfde gedrag als `amJvs.cpp` in de OpenParrot-bron, daar voor `hookPort = "COM3"`).
2. **ALL.Net.** `CoCreateInstance(CLSID {4603BB03-058D-43D9-B96F-639BE908C1ED}, NULL, CLSCTX_LOCAL_SERVER, IID {045A5150-D2B3-4590-A38B-
   C1158678E1AC})` op `0x4010c0`: dat is de COM-server **`AMCUS/AMAuthd.exe`** (beide GUID's staan in `AMAuthd.exe`; de IID in het
   proxy/stub-bestand `AMCUS/iauthdll.dll`, dat 32 methodes aangeeft = IUnknown + **29 eigen methodes**). Het spel leest er `auth_state`,
   `daemon_state`, `daemon_error`, `last_auth_error`, de PowerOn-velden (`uri`, `host`, `place_id`, `country`, `region0`, `timezone`) en
   het IP van het spel; mislukt `CoCreateInstance`, dan zet het spel het vlag-byte van het wikkelobject op 0 en gebruikt vaste waarden
   (onder andere toestand 8 en -1, `0x461fc5`): dat is een bedoeld pad, geen crash. `AMAuthd` registreert zelf zijn klasse
   (`CoRegisterClassObject`) en kent `authType` = `ALL.NET` | `OFFLINE` | `NBLINE` in `AMConfig.ini`.
3. **De gemeenschapsserver.** `Network/ServerAddress`/`ServerPort`/`PlayerId`/`Gateway` komen alleen in `teknoparrot.ini`. In een
   geheugenbeeld van de gesloten kern staat het online-protocol van TeknoParrot: ondertekende sessies (`TPAUTH`, `TPSK1`, `TPENC1`,
   HMAC, "the server refused this cabinet", `[Network] OnlineID / OnlineSecret` uit het teknoparrot.com-account). Dat is een
   eigen, geauthenticeerd protocol van een externe dienst.
4. Overig: schijfletters (`G:/Contents`), 16 MiB-stapel (engine, `StackReserve`), `Polygon2D`-ViewLink en d3dx10-pHResult (al van ons).

**Vervanging.**

* JVS: `tools/rg-mfcam` kreeg een derde taak: met `RG_JVS_COMPORT=COM3` (in de drop-in) zet `rg-mfcam.dll` in de importtabel van de exe
  `CreateFileA` (COM3 -> pipe, met wachten tot de brug de pipe heeft) en de zeven comm-aanroepen om (slagen op dat handvat);
  `GetCommModemStatus` geeft 0x10, na adrestoekenning 0x30 (byte 0 van `TeknoParrot_JvsState`, door `rg-jvsbridge` gespiegeld).
  Beslislogica in `tools/rg-mfcam/jvs_com.h`, getoetst in `tests/test_jvs_com.cpp`. Logboek: `rg-mfcam.log`.
* Kern: `[Loader] Core = ravengun` (generiek).
* **Online werkt zonder de gesloten kern niet**, en wij bouwen het niet na: (a) het TPAUTH-protocol is dat van TeknoParrot's dienst en vraagt
  het account van de eigenaar; (b) een eigen `IAuth` (29 methodes, uitvoerstructuren die het spel veldgewijs leest) is zonder proef met het
  echte spel niet betrouwbaar te maken; (c) `AMAuthd.exe` in `OFFLINE`-modus zou de eigen code van het spel gebruiken, maar vraagt een
  tweede-exe-start met COM-registratie (`iauthdll.dll` `DllRegisterServer`, `LocalServer32`) en een `AMConfig.ini`-kopie met
  `amdcfg-authType=OFFLINE`; of het spel die offline-auth accepteert is niet gemeten. De kandidaat start het spel dus **zonder**
  `IAuth`: op het pad dat het spel zelf kent (vlag 0). Of het daarna tot de attract komt of na 2 minuten op de netwerkcontrole wacht
  (`ALL.NetCheck_TIMEOUT`, na 7200 = `0x1c20` ticks) is de vraag voor de tv.

**Test op de tv.** (1) kandidaat activeren (`game.kandidaat.ini` + `dropin.kandidaat.d/`). (2) dubbelklik. (3) `rg-mfcam.log`: "JVS-omleiding
aan: COM3 -> \\.\pipe\TeknoParrot_JVS", "JVS: CreateFileA(COM3) -> pipe ..., handvat ...", plus de bestaande camera- en d3dx10-regels.
(4) `ravengunctl` toont JVS-verkeer; stuur/gas/Item werken zoals voor. (5) wat doet het spel met de ALL.Net-controle: attract binnen ~1 min,
of netwerkfoutscherm? Bij een blokkade: eerst `Data/System/Config/NetConfig.csv` en `AMCUS/AMConfig.ini` bekijken (zie hieronder).
(6) 5 minuten attract/spel, `/proc/<pid>/maps` zonder `TeknoParrot.dll`.

**Als de ALL.Net-controle blokkeert (volgende stappen, niet gedaan).** `Data/System/Config/NetConfig.csv` heeft schakelaars
(`NET_USE_LAN`, `NET_USE_INTERNET`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_DUMMY_SERIAL_NO`, `NET_SECURE_FLAG`; in de exe staan alleen
`NET_GAMESERVER_ADDRESS`, `NET_IGNORE_ALLNET_MAINTENANCE`, `NET_USE_SHAREK`, `NET_INTERNET_PORT`, `NET_GROUP` als sleutelnamen, dus
niet alle regels van dit bestand worden nog gelezen) en `DefaultConfig.csv` heeft `USE_MUCHA`. Dat zijn instellingen van het spel zelf,
in de view te zetten met `ViewCopies` + een bestandspatch.

---

## Time Crisis 5 (EmulationProfile `TimeCrisis5`, 64-bit UE3, Namco ES3X)

**Statisch gemeten.** `TimeCrisisGame-Win64-Shipping.exe` importeert `WINUSB.DLL` (8 functies: `Initialize`, `QueryInterfaceSettings`,
`QueryDeviceInformation`, `QueryPipe`, `ReadPipe`, `WritePipe`, `GetOverlappedResult`, `Free`), `SETUPAPI.dll` (`SetupDiGetClassDevsW`,
`EnumDeviceInterfaces`, `GetDeviceInterfaceDetailW`, `DestroyDeviceInfoList`, **en** `CM_Locate_DevNodeW`, `CM_Get_Parent/Child/Sibling`,
`CM_Get_DevNode_Registry_PropertyW`), `DINPUT8`, `XINPUT1_3` en `hasp_windows_x64_105293.dll`; script-natives `GetUSIOCounter`,
`GetUSIOGOut`, `OpenUSIOForUE`, `UpdateUSIOForUE`, `HasConnectedUSIOForUE` en de klasse `CUsioRaw`. `RSLauncher.exe` importeert
`hasp_windows_x64_105293.dll`, `nbamsavdat.dll`, `Nbam_QR_Code.dll`, `nbamMfCapture.dll` en start het spel als kindproces
(`TimeCrisisGame-Win64-Shipping.exe -NOINI -Language=... -playside=1`, `Start.bat`). **Geen van beide exe's importeert `bngrw.dll` of
`iDmacDrv64.dll`**: dat die twee naast de runtime-lader staan is een eis van de gesloten kern, niet van het spel; zonder die kern vervalt
de reden om `Build = teknoparrot` te houden (open punt in `docs/research/loskoppelen-takenlijst.md`).

**Wat de kern deed.** (a) injectie in het kind van `RSLauncher.exe` (`ES3XLauncherHook`-patroon: `CreateProcess` haken, kern laden),
(b) het USIO-bord emuleren op WinUSB/SetupAPI/CM-niveau, gevoed uit `TeknoParrot_JvsState` (TeknoParrotUi `TC5Pipe`: offset 8 bitveld
`0x01` test, `0x04` service, `0x08` munt, `0x10` trekker, `0x20` knop, `0x40`/`0x80` pedalen, `0x100/0x200/0x400` select/enter; offsets
12/16 de assen), (c) de HASP-laag (bestaat al als drop-in `ravengun-hasp`). Op de directe route werken sinds 2026-10-15 pedalen en
trekker niet (`DIRECT-ROUTE-INVOER.md`), de munt (Shift+T) wel.

**Waarom niet gebouwd.** Een eigen vervanging moet in het spelproces `SetupDi*W`, `CM_*`, `CreateFileW` op een nep-apparaatpad en alle 8
`WinUsb_*` nabootsen. Uit het echte `bnusio.dll` (zelfde `CUsioRaw`-bibliotheek, 32-bit) heb ik het transport al gelezen:
apparaat-interface via `SetupDiGetClassDevsA` met een vaste GUID (`bnusio.dll` `0x100044e8`), `CreateFileA` met
`FILE_FLAG_OVERLAPPED`, `WinUsb_QueryPipe` zoekt bulk-IN (type 2, adres met bit 7), bulk-OUT (type 2) en interrupt-IN (type 3);
register lezen = 6 bytes `{0x10|op, 0, addr(16), len(16)}` naar bulk-OUT, dan `len` bytes van bulk-IN; schrijven = 6 bytes
`{0x90|op, ~(addr>>8)&0xF0, addr(16), len(16)}`, dan de gegevens, dan 1 byte ack. De communicatiedraad leest per ronde 0x100 bytes @0x0000,
0x80 bytes @0x1080 (daar lijken de 32-bit schakelaars en 14-bit muntstanden met 2 statusbits te staan) en schrijft 0x20 bytes @0x1420 (uitgangen).
Nog niet gelezen: de rest van de registerindeling (`0x2f90`, `0x31d0` in `bnusio.dll`), wat het TC5-spel met de `CM_*`-boomwandeling
controleert, en of TC5's eigen `CUsioRaw` (nieuwere versie) dezelfde indeling heeft. Zonder die drie is een nep-USIO een gok op een
64-bit spelproces; een tv-proef kan het niet repareren. Aanbevolen vervolg: `rg-usio` een tweede modus geven (WinUSB-nabootser, 64-bit,
registerbeeld uit hetzelfde `usio_model.h`) en eerst de drie onbekenden met een probe-DLL in het spel loggen (`rg-usio` logt al elke
eerste aanroep).

**Zonder de gesloten kern kan TC5 nu dus niet worden gestart met invoer.** Geen kandidaat-`game.ini`: een kandidaat zou alleen de lader
wisselen en de invoer kwijtraken.

---

## HOTD Scarlet Dawn (EmulationProfile `ALLSHOTDSD`, 64-bit UE4, twee exe's)

**Gemeten (`patches/HOTDSD/METING-1-DAEMON.md`, `AB-UITKOMST.md`).** `amdaemon.exe` sterft rechtstreeks in 1 s op
`amw_platform_manager.cpp:102`, `amPlatformGetPlatformInformation(). ErrCode 0.` (vóór mountletters en keychip). Met de lader plus
een willekeurige geïnjecteerde DLL leeft hij >= 30 s zonder verder te komen (AMFS 0x); alleen met de gesloten kern raakt hij de
mount-/appfile-laag (AMFS 95x, appfile 11x) en bereikt `Process state: Advertise`. Keychip (`FddDriver`) wordt in geen enkele arm
geraakt. De invoer start/test/service/munt/subtrekker loopt als 64-byte-rapport over `\\.\pipe\TeknoParrotPipe` (`AllsUsbIo`), dat de
kern in het **daemonproces** als USB-IO-bord (HID) presenteert; trekkers en aim lopen via het gedeelde geheugen (`HOTDSDPipe`).

**Wat de kern dus doet voor dit spel.** (1) De platformgegevens die `amPlatformGetPlatformInformation` verwacht (hardware-identiteit:
`pxGetHwInfo.ini` noemt platform-id `ACA0`), (2) schijfletters `X:`/`Y:`/`D:` -> `AMDaemon/TeknoParrot/<L>` (gedekt door patch 0004),
(3) het USB-IO-bord (HID/SetupAPI in `amdaemon.exe`), (4) de injectie in beide processen.

**Waarom niet gebouwd.** Punt 1 is het vervalsen van een hardware-identiteit voor Sega's platformcontrole in `amdaemon.exe`. De
projectafspraak (`docs/decisions.md` 2026-10-11 "HOTD Scarlet Dawn: geen eigen AM-diensten"; `docs/per-game-patches.md` "Grenzen": geen
patches op keychip-, regio-, integriteits- of dongle-controles) laat dat niet zonder beslissing van de eigenaar toe. Dat is een keuze,
geen technisch gat: de functie is statisch te vinden (de uitzonderingsregel noemt bron en regel) en het hardware-oordeel zit in
SetupAPI/CfgMgr-opsommingen (`setupapi` 45x, `cfgmgr32` 29x, `Display` 108x in het spoor). De delen 2 en 4 zijn al of bijna van ons; deel 3
is het grootste bouwwerk (HID-apparaat met `hid.dll`/SetupDi/overlapped `ReadFile`, in een 64-bit daemon).
**Vraag aan de eigenaar:** mag RavenGun de platformgegevens van `amdaemon.exe` nabootsen (de kern deed dat al; het is geen keychip), of
blijft HOTDSD op de gesloten kern tot Sega's `minime`-achtige route er is? Tot dan: geen kandidaat.

---

## Bestanden

| Pad | Wat |
|---|---|
| `tools/rg-usio/usio_model.h`, `rg_usio.cpp` | USIO-model (getoetst) en `bnusio.dll` |
| `tools/dev/build-usio.sh` | bouwt `bnusio.dll` in de mingw-container |
| `tools/rg-mfcam/jvs_com.h`, `rg_mfcam.cpp` | JVS-over-COM-logica (getoetst) en de derde taak van `rg-mfcam.dll` |
| `tools/dev/build-mfcam.sh` | kopieert nu ook `jvs_com.h` |
| `tests/test_usio_model.cpp`, `tests/test_jvs_com.cpp` | ctest `ravengun_usio_model_tests`, `ravengun_jvs_com_tests` |
| `patches/PointBlankX/` en `patches/MarioKartDX/`: `game.kandidaat.ini`, `dropin.kandidaat.d/` | kandidaat-`game.ini` + `dropin/` (ravengun-patches, ongecommit) |
| `docs/research/namco/PointBlankX.game.ini`, `MarioKartDX.game.ini` | dezelfde kandidaat-`game.ini` in deze repo |
