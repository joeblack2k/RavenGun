# Lost Land Adventure zonder TeknoParrot: wat de kern doet, wat wij vervangen, wat nog ontbreekt

Stand 2026-10-17. Alleen statisch gemeten (objdump, strings, een eigen scanner; NAS alleen gelezen); er is niets gestart.
Status: **geblokkeerd / deels kandidaat.** De seriele JVS-laag (munt, start, service, test, testmenu) is vervangen en gebouwd; drie andere
dingen die de gesloten kern voor deze game doet zijn nog niet vervangen en niet te meten zonder een run (zie "Open").

## Wat de game is en wie wat doet

| Onderdeel | Feit (bron) |
|---|---|
| `RSLauncher.exe` (3,6 MB, PE64, niet ingepakt) | start de game, doet I/O en de Namco-laag. Importeert `wajvio.dll` (8 functies), `hasp_windows_x64_106993.dll` (ordinalen 3, 14, 13, 17 = encrypt, logout, login, write), `nbamMfCapture.dll`, `nbamsavdat.dll`, `Nbam_QR_Code.dll`, `ssleay32/libeay32`, `ws2_32` |
| `LLA/Binaries/Win64/DomeShooterGame-Win64-Shipping.exe` (UE3) | importeert **niet** `wajvio.dll`. Wel `XINPUT1_3.dll`, `DINPUT8.dll`, `WSOCK32`, hasp-ordinalen 2, 15, 13, 14. UnrealScript-natives `UDSMouseInterfacePlayerInput::execGetGunPeripheralRawInputAxis / SetGunPeripheralRawButtonMasks / UpdateMousePositionWithGunPeripheral / UpdateMouseVibrationWithGunPeripheral` (handlers `0x140a2bde0`, `0x140a2ba90`, `0x140a2bd20`): het richten en de trekker komen als "gun peripheral" binnen in de game-exe |
| `wajvio.dll` (123 kB, Namco WinArc, `NBGI.;WinArc;Ver2.1.1;JPN`) | een volledige JVS-master over een seriele poort: `CreateFileA(poort, R/W)`, 115200 baud 8N1, `GetCommModemStatus` (sense), resets en `Tx/Rx Address/Revision/I/O ID/Function command`. `WAJVOpen(const char *poort)`; RSLauncher geeft `"COM3"` (`.rdata` `0x1402bd788`) |
| echte opstartvolgorde (`init*.ps1`, `project.ps1`) | `PreprocessAmAuth.exe` (donglecontrole) -> `regsvr32 AMCUS\iauthdll.dll` -> `AMCUS\AMAuthd.exe` -> `AMCUS\AMUpdater.exe -wait` -> `RSLauncher.exe`. TeknoParrot start alleen `RSLauncher.exe` |

## Wat de gesloten TeknoParrot-kern hier doet (afgeleid, niet gemeten in een draaiend proces)

1. **JVS over COM3 vervangen** (RSLauncher). Het TeknoParrot-profiel `LostLand.xml` heeft Test, Service1, Coin1, Start 1/2 (P1Button3/4),
   Enter (P1Button1), servicepaneel (P1Button2), omhoog/omlaag en pincodetoetsen als gewone JVS-schakelaars: die loopt via de
   JVS-emulatie van de kern (pipe `TeknoParrot_JVS`, `pipehelper`), niet via het gedeelde geheugen.
2. **Het gun peripheral**: TeknoParrot's `LostLandPipe` schrijft alleen de twee trekkers (bits 0, 1 van byte 8) en vier assen (bytes
   12, 16, 20, 24) in het gedeelde geheugen. `RSLauncher.exe` leest in het WAJV-blok **geen enkele as of trekker** (analoog `0x288..0x2a7`
   en speler 1+ ongebruikt; zie hieronder), dus de kern vult die in de game-exe. Via welke API (XInput of DirectInput) is niet gemeten.
3. **De HASP-dongle** (de kern handelt hem af; `patches/TC5/dropin/ravengun-hasp/TC5/dongle.json`, ronde 35: onze shim wordt wel geladen maar
   nooit aangesproken). De fileids en de encrypt/decrypt-sleutel staan nergens in de dump.
4. Mogelijk taal/regio (de tekst is Japans terwijl `Text Language = English`): niet te beslissen zonder run.

## Wat gebouwd is: onze `wajvio.dll`

Route (a) uit de opdracht (vervangende DLL), niet route (b) (pty als COM3 met een JVS-bord in `jvs-boards.json`): (a) heeft geen
seriele poort, geen modemstatus-hulp en geen JVS-timing nodig, en de game-kant is een smalle, volledig gelezen API.
Route (b) werkt in principe ook (Let's Go Island/Safari doen het zo), maar dan moet de daemon het hele JVS-protocol van wajvio's
master goed beantwoorden en de bit-naar-blok-omzetting van wajvio zelf vertrouwen; ze staat hier ook uitgeschreven maar niet getest.

* `tools/rg-lostland/rg_wajv_map.{h,c}`: zuivere logica, gedeeld geheugen -> invoerblok (0x300 bytes). Test: `tests/lostland_wajv_map_test.c`.
* `tools/rg-lostland/wajvio_rg.c` + `build.sh`: de DLL (PE64, alleen kernel32, geen CRT, zelfde 18 exports en ordinalen als het
  origineel). Uitvoer `patches/LostLandAdventure/dropin/wajvio.dll` (sha256 in het rapport) en `dropin.ini`.
* De DLL leest de mapping `TeknoParrot_JvsState` (gemaakt door `rg-jvsbridge.exe`), logt naar `rg-wajvio.log` naast de exe.

### Het WAJV-blok (gemeten in wajvio.dll `0x180001300` en RSLauncher `0x140062ee0`, `0x140063a60..0x140064100`)

`WAJVGetInput(1)` geeft een pointer naar een blok van 0x2ec bytes (knooppunt 1 is het I/O-bord; RSLauncher gebruikt `ebx = 1`).
`WAJVGetStatus() == 0` betekent klaar, `-2` fout. `WAJVGetNodeCount() > 0` is vereist.

| Offset | Betekenis | Opmerking |
|---|---|---|
| `0x183..0x186` | START, SERVICE, OMHOOG, OMLAAG van speler 1 (JVS-bits 7..4 van het eerste spelerbyte) | dubbel met `0x188 + idx` |
| `0x187` | TEST | |
| `0x188 + 0x40*p + idx` | knop `idx` van speler `p` (idx = bit in het woord van twee JVS-bytes: 0 START, 1 SERVICE, 2 OMHOOG, 3 OMLAAG, 4 LINKS, 5 RECHTS, 6..13 PUSH1..8) | RSLauncher leest alleen p = 0: `0x189` service, `0x18a/0x18b` omhoog/omlaag, `0x18e` PUSH1 (Enter), `0x18f` PUSH2 (servicepaneel), `0x190/0x191` PUSH3/4 (start 1/2), `0x192, 0x193, 0x195, 0x196` pincodetoetsen |
| `0x288 + 2*kanaal` | analoge kanalen (u16) | RSLauncher leest ze niet |
| `0x2a8 + 8*sleuf` | u16 muntenteller, u16 0, u32 status (0 = normaal; 1 = muntfout 01-0x) | RSLauncher neemt het verschil met de vorige stand als nieuwe munten |
| `0x2c8 + 8*speler` | u16, u16, u32 (tweede teller van RSLauncher, status op `0x2cc` moet 0 zijn) | blijft 0 |

Wat **niet** zeker is: of "Enter" echt PUSH1 is en "servicepaneel" PUSH2 (dat volgt uit de TeknoParrot-profielnamen P1Button1/2 en de
JVS-indeling, niet uit RSLauncher's gedrag), en of de tweede teller (`0x2c8`) een tweede muntsleuf is. De pincodetoetsen staan nergens in
onze shm-indeling (alleen nodig voor het testmenu van de beheerder).

## Bits in het gedeelde geheugen

De DLL leest bit 2..9 van byte 8 (service, munt, test, start 1, start 2, omhoog, omlaag, Enter). Die staan **nog niet** in
`packaging/tp-shm-profiles.json` (alleen bit 0 en 1). Voorstel (achterwaarts compatibel, de TeknoParrot-kern leest alleen bit 0/1):
`LostLandAdventure.tp-shm-profile.json` in deze map. De JSON-indelingen zitten in de binary, dus bouwen en installeren is nodig.

## Open (blokkeert "zonder TeknoParrot")

1. **Gun peripheral**: hoe de game-exe richten en trekker leest (waarschijnlijk XInput: de game importeert `XINPUT1_3` op de ordinalen
   2 en 3, en de natives noemen "RawInputAxis"/"ButtonMasks"/"Vibration"). Volgende stap: de vtable-methoden achter `0x140a2bd20` /
   `0x140a2bde0` volgen (die roepen virtuele functies op `[obj+0x308]`, `[obj+0x310]`, `[obj+0x198]`), of een run met een `xinput1_3.dll`-waarnemer.
   Mogelijke vervangers: een eigen `xinput1_3.dll` (DllOverride native) die de vier assen en twee trekkers uit dezelfde shm geeft, of een
   hid-board (uinput) dat Wine als XInput-pad ziet.
2. **Dongle**: `RSLauncher` schrijft (`hasp_write`) en versleutelt, de game leest en ontsleutelt. Zonder echte dongle-gegevens kan alleen een
   consistent (symmetrisch) encrypt/decrypt-paar werken, en dat is niet bewezen. De bestaande shim (`patches/TC5/dropin`) wordt in TC5 niet
   aangesproken zolang de gesloten kern erbij zit; zonder kern is dat nu pas te meten. Een probe-run met `RAVENGUN_HASP_VERBOSE=1` en `Core = none` is de volgende meting.
3. **ALL.Net/MUCHA** (`AMCUS/`): de echte opstartvolgorde draait `AMAuthd.exe` en `AMUpdater.exe` voor `RSLauncher.exe`; TeknoParrot doet dat niet.
   `AMCUS/AMConfig.ini` is de online variant (`ALL.NET`, `offlineMode=DISABLE`); `AMConfig_EXDevOffline.ini` (`CHARGE`, offline) bestaat.
4. **Taal**: waarom de tekst Japans is is open (de `Text Language`/`Voice Language`-waarden zijn velden van de kern, niet van de game).

## Kandidaat-test op de tv (alleen een meting, geen verwachting van speelbaarheid)

1. de secties uit `docs/research/losse/LostLandAdventure.dropin.ini` in `patches/LostLandAdventure/dropin/dropin.ini` zetten; de `[Loader]`-sectie uit `LostLandAdventure.game.ini` overnemen
   (`Core = none`); optioneel het voorgestelde shm-profiel in `packaging/tp-shm-profiles.json` zetten, bouwen en installeren.
2. Dubbelklik op Lost Land Adventure; ~2 minuten wachten.
3. Controleren: `rg-wajvio.log` in de view (`.../views/LostLandAdventure/`) bevat `WAJVOpen`, `WAJVGetStatus`, `WAJVGetNodeCount`, `WAJVGetInput`; verder
   waar de game stopt of welke foutcode ("01-0x COIN ERROR", "06-01 GUN CONTROLLER ERROR", dongle) RSLauncher toont. Met de huidige
   kern bereikt de game de attract (DEMO PLAY); gaat dat zonder kern ook, dan is de HASP-laag niet nodig geweest; zo niet, dan is dat de eerste blokkade.
4. Terugdraaien: map terug hernoemen.
