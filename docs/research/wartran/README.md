# De WartranTroopers-familie zonder TeknoParrot

*Stand 2026-10-17. Veertien games die vandaag de gesloten kern (`TeknoParrot(64).dll`) laden via onze OpenParrot-lader, met invoer via
tp-shm (indeling `WartranTroopers`, `packaging/tp-shm-profiles.json`). Doel: per game vervangen wat de kern doet, met zo weinig
mogelijk eigen onderdelen, en de kennis per game als gegevens in `patches/<Game>/`.*

## Wat de kern voor deze familie doet

De kern is ingepakt (geen gamenamen in `strings`, `docs/research/wat-is-de-kern.md`); wat hij per game doet is alleen te zien aan wat
de **game** verwacht. Gelezen in de games zelf (Assembly-CSharp.dll met monodis/ILSpy, exports en imports met objdump):

1. **De TeknoParrot-plugins van de Unity-games** (`BepInEx/plugins/*TPPlugin.dll` in de spelmap op de NAS: AfterDark2, MarsSortie,
   WildWestShootout) praten met de kern via `WriteFile`/`ReadFile` op een **nep-handvat** (`0xC0FFEE`, bij Mars Sortie `0xC0FEE`):
   een verzoek van 8 bytes `FF C0 FF EE <cmd> 0 0 0`, het antwoord in dezelfde buffer. De betekenis van `<cmd>` verschilt per game
   (Mars Sortie: `0x10` test/service/munt 1-4, `0x11` start/trekker 1-4, `0x12` wapen/menu, `0x20..0x23` richtpunt als float, `0x40`
   resolutie, `0x80` opties; Night Hunter: `0x10` test/service/munt/start/trekker, `0x11` speciaal/wapen/stuur, `0x20/0x21` richtpunt in
   pixels; Wild West: `0x00` richtpunt als byte en herladen, `0xFF` service). De kern leest dat uit het bordbeeld.
2. **Een I/O-bord nabootsen** waar de game zelf mee praat: serieel (Wild West: `BaseCom`/`LSerialPort`, pakketten van 8 bytes; Drakons:
   `RS_232_DLL.dll`), een leveranciers-DLL (Rampage: `KaboomAdrioFxPlusFourFeeders64.dll`; Lethal Enforcers 3 en Gashaaaan: Konami
   `iob.dll` + `gunio.dll`/`ballio.dll`), of HID (After Dark: `HID\Vid_0471&Pid_485e` met feature-rapporten; Raccoon Rampage en Elevator
   Action Invasion: `UUsbBPLibrary`, SetupDi + HID).
3. **Dongles**: SuperDog (Mars Sortie `DoCheckKey`, Wild West `CheckDog`), HASP HL statisch gelinkt (Coopers 9, Robin Hood), Dinkey
   (`dk2win32.dll` in de Sega-shell van Pigs Might Fly), Matrix (Rampage, Drakons: niet blokkerend).
4. Venster/resolutie (`teknoparrot.ini`: Windowed, HideCursor, CustomResolution) en, bij een paar games, een Japanse locale
   (`UseLocaleEmulator`: Bug Busters, Gashaaaan, Lethal Enforcers 3).

## Ontwerp: zo weinig mogelijk eigen onderdelen

| Onderdeel | Voor | Waar |
|---|---|---|
| **Bordbeeld in het spelproces** — de Windows-mapping `TeknoParrot_JvsState` bestaat al zonder kern (onze `rg-jvsbridge.exe` maakt hem in de prefix en spiegelt `/dev/shm/ravengun_JvsState` van de daemon) | alle | `plugins/bepinex/shared/Bordbeeld.cs` (C#), `tools/rg-adrio/rg_adrio.c` (C) |
| **Een eigen BepInEx-plugin per Unity-Mono-game** die de game op het niveau van zijn eigen gebeurtenissen bedient (geen bordprotocol nabootsen) en de TeknoParrot-plugin uitzet | MarsSortie, WildWestShootout, AfterDark2 | `plugins/bepinex/<Game>/`, `[Launch] BepInEx = 5.4.21.0` |
| **Een leveranciers-DLL als drop-in**, met de indeling als tabel per game | Rampage (Kaboom/Adrio) | `tools/rg-adrio/`, `patches/Rampage/dropin/rg-adrio.ini` |
| **`[Loader] Core = none`** — geen lader en geen kern; de game draait in de map van zijn exe | de vier hierboven | `src/ravengun/launch/direct.cpp` (werkmap) |

Sinds 2026-10-17 gebouwd, nog niet op de tv: de UNIS-drop-in `UsbPluginsDll.dll` (`tools/rg-unis/`, zie sectie "UNIS"; Night Hunter heeft
dezelfde familie als `DllBindFunc.dll`, die is nog niet gelezen) en de Konami-I/O-drop-in (`iob.dll` + `gunio.dll`/`ballio.dll`:
`tools/rg-konami/`, `docs/research/konami/`). Volgende onderdelen (niet gebouwd, zie de tabel): een uhid-bord voor het After Dark-bord,
een virtuele joystick voor Bug Busters en een invoerinjector voor Drakons.

## Per game

| Game | Engine | Wat de kern deed (gemeten in de game) | Vervanger | Stand |
|---|---|---|---|---|
| **MarsSortie** | Unity Mono 64 | antwoorden voor MSTPPlugin (knoppen, richtpunt, resolutie); I/O-bord GMB en SuperDog worden door MSTPPlugin uitgezet | `RavenGun.MarsSortie`: vult `MRCQ.Hardware` (SetInputKey/GetInputAxis) uit het bordbeeld, zet GMB, GameLoader, SuperDog en bordfouten uit | **klaar om te testen** |
| **WildWestShootout** | Unity 5.6 Mono 32 | serieel bord (trekker, munt, testmenu) nagebootst + antwoorden voor WWSTPPlugin (richtpunt); camera MVSDK en SuperDog uitgezet door WWSTPPlugin | `RavenGun.WildWestShootout`: richtpunt (`BaseGun.GetGunPos`), trekker (`BaseGun.OnShoot`), munt (`InCoinAction`), herladen, start, testmenu; camera, bord, dongle, witte flits uit; opstart meteen naar de attract | **klaar om te testen** (meeste risico: de opstart en het testmenu) |
| **AfterDark2** (Night Hunter) | Unity Mono 64 | antwoorden voor NHAD2TPPlugin (muisroute, knoppen, resolutie) en de dongle: SenseLock Sense4 (`Sense4.dll`, `dog_hardware_sense_4`; oud: `DllBindFunc`) | `RavenGun.AfterDark2`: muisroute aan, richtpunt/trekker per speler, munt/start/speciaal/wapen/stuur/testmenu, toetsenbord uit, dongle zegt ja | TV 2026-10-17: "Please insert the dongle" (Sense4 niet afgehandeld); **dongle toegevoegd, opnieuw te testen** |
| **Rampage** | Unity Mono 64 | Kaboom/Adrio-bord (`KaboomAdrioFxPlusFourFeeders64.dll`) | eigen `KaboomAdrioFxPlusFourFeeders64.dll` (rg-adrio) + `rg-adrio.ini` | TV 2026-10-17: beeld zwart, maar de game liep (Unity-logboek: "Kaboom Connected To Serial : 2", attract-scènes wisselen) en exclusief schermvullend zonder gamescope; **nu in gamescope, opnieuw te testen** |
| **DrakonsRealmKeepers** | Unity **IL2CPP** 64 | **gecorrigeerd 2026-10-17 (IL2CPP-dump, sectie "Drakons")**: geen seriele invoerbord. `RS_232_DLL.dll` (`SBK.RS232.RS232DLL`) is alleen de seriele laag onder `PneumaticSeatController`/`SerialProtocol_Leds`/`SBK.KaboomLib.SerialProtocol` (zitbank, ledbesturing); munt en start komen als `ArcadeInput` (toetsenbord) in `CoinInput.keyboardInputs`; aanwijzen/trekker via Unity Input System | geen bord om na te bouwen: een invoerinjector in het spelproces (muis + toetsen uit het bordbeeld) | **niet gebouwd**: ontwerp en open vragen in sectie "Drakons" |
| **ElevatorActionInvasion** | UE4 64 (PDB aanwezig) | het UNIS-I/O-bord via `ESGame/Plugins/UsbPluginsDll.dll` (Usb_ImportDLL laadt hem; 75 exports `BIND_*`): `BIND_GetESIoData` (16 bytes, start/menu), `BIND_GetGunData` (FPACKET, 92 bytes), munttellers, dongle | `rg-unis` (`tools/rg-unis/`): eigen `UsbPluginsDll.dll` + `rg-unis.ini`, `[Loader] Core = none` | **kandidaat klaar (statisch bewezen, niet gestart)**: `patches/ElevatorActionInvasion/game.kandidaat.ini` + `dropin/`, tv-recept in sectie "UNIS" |
| **RaccoonRampage** | UE4 64 (PDB aanwezig) | idem: `RSGame/Plugins/UsbPluginsDll.dll` (UNIS, 107 exports; TeknoParrot heeft hem op 8 bytes aangepast, het origineel staat ernaast als `.original`): `BIND_GetIoData` = 49 bytes met knoppen, trekkers en de ruwe aanwijspunten van vier geweren | dezelfde `rg-unis` | **kandidaat klaar (statisch bewezen, niet gestart)**: `patches/RaccoonRampage/game.kandidaat.ini` naast de huidige (die op de TP-kern blijft staan) |
| **AfterDark** | Virtools 32 | HID-bord `VID 0471 PID 485E` (HidD_Get/SetFeature) | uhid-bord met feature-rapporten | **geblokkeerd**: protocol in GamePlayer/CoFramework niet gelezen |
| **LethalEnforcers3** | eigen 32 (Konami) | Konami `iob.dll` (44 exports) + `gunio.dll` (18), beide over een seriele poort | eigen `iob.dll` + `gunio.dll` (`tools/rg-konami/`) + een eigen `SAVE/io.sav` (kalibratie bij onze ruwe waarden); Japanse locale via `[Env]` | **kandidaat, klaar om te testen**: structuren gelezen, zie `docs/research/konami/README.md` |
| **Gashaaaan** | eigen 32 (Konami) | Konami `bin/iob.dll` (F24-familie, 30 exports) + `bin/ballio.dll` (63: bs2_*, ball_io_*) | eigen `iob.dll` + `ballio.dll` (`tools/rg-konami/`): een worp (trekker) wordt een balinslag op het richtpunt | **kandidaat, klaar om te testen**: zie `docs/research/konami/README.md` |
| **BugBusters** | eigen 32 | een eigen DirectInput8-laag die alle apparaten opsomt (muis 0x12, toetsenbord 0x13, joystick met `c_dfDIJoystick2`) en een `control.cfg` zoekt (staat niet op de NAS: standaard in de code); de kern levert de invoer in het proces | een virtuele joystick (hid-board, uinput, zoals Transformers) zodra bekend is welke assen/knoppen de game leest | **pad gevonden, niet gebouwd**: welke velden van DIJOYSTATE2 trekker/start/munt zijn |
| **Coopers9** | eigen 32, ingepakt (lege importtabel) | HASP HL (statisch) + DirectInput8 | HASP-laag (bestaat voor ELF) + invoer | **geblokkeerd**: ingepakte exe |
| **RobinHood** | eigen 32 | HASP HL (statisch) + DirectInput | als Coopers 9 | **geblokkeerd** |
| **PigsMightFly** | Sega-shell + PigsMightFly.exe | Sega-shell (I/O-bord) + Dinkey (`dk2win32.dll`) | de route van Men in Black (game zonder shell) + Dinkey-laag | **geblokkeerd**: shell en Dinkey niet nagebouwd |

## Uitrollen en testen

`docs/research/wartran/patches/<Game>/` bevat de klaargezette `game.ini` en `dropin/` voor de vier games die klaar zijn. Het script
`tools/dev/wartran-uitrol.sh` bouwt de plugins en rg-adrio en zet alles in `patches/<Game>/` (alleen als de `game.ini` daar nog de
versie is waarop de klaargezette gebouwd is). Terug naar de kern van TeknoParrot, in ravengun-patches: `git checkout -- <Game>/game.ini`
en voor MarsSortie, WildWestShootout en AfterDark2 `rm -r <Game>/dropin` (die map is nieuw), voor Rampage `git checkout --
Rampage/dropin/dropin.ini` en `rm Rampage/dropin/{KaboomAdrioFxPlusFourFeeders64.dll,rg-adrio.ini,BUILDINFO-kaboom.txt}`. Een plugin
die blijft staan naast de kern van TeknoParrot geeft dubbele invoer (de kern bootst het bord ook na).

Bewijs per run: het logboek van BepInEx (`BepInEx/LogOutput.log` in de view: `RavenGun.<Game>: N van M haken gezet`, `bordbeeld:
TeknoParrot_JvsState geopend`, `TeknoParrot-plugin com.tp.* uitgezet`) en bij Rampage `Game_Data/Plugins/rg-adrio.log` in de view
(`InitLib(poort 2): bord van RavenGun actief`, `tabel: ...`, `bordbeeld TeknoParrot_JvsState geopend`). In `/proc/<pid>/maps` van het
spelproces mag geen `TeknoParrot64.dll`/`TeknoParrot.dll` en geen `OpenParrot` staan; het logboek van rg-direct zegt
`[Loader] Core = none: Wine start de exe zelf` en `start: cwd=<view>`.

## UNIS (`UsbPluginsDll.dll`): Elevator Action Invasion en Raccoon Rampage zonder TeknoParrot

*Stand 2026-10-17, alles statisch (PDB's + disassemblage + eenheidstest); NIETS is gestart. Code: `tools/rg-unis/` (rekenkern `rg_unis_kern.c`, DLL `rg_unis.c`), bouwen:
`tools/dev/build-unis.sh`, test: `tests/unis_test.sh` (ctest `ravengun_unis`), controle van het plan en klaarzetten van de wegwerp-patchmap: `tools/dev/unis-kandidaat.sh`.
De onderzoeksscripts (kleine PDB-lezer, structuurdump, xref, disassemblage met namen) staan in `tools/rg-unis/onderzoek/` (pip: pefile, capstone, numpy).*

### Wat de game doet (bewijs)

Beide games zijn Unreal Engine 4.26; de PDB's (566 en 622 MB) geven namen en typen. Een eigen minimale PDB-lezer (`onderzoek/pdbmin.py`: S_PUB32 naar RVA; `onderzoek/tpi.py`:
LF_STRUCTURE/LF_MEMBER) leverde de volgende feiten.

| Feit | Elevator Action Invasion (`ESGame-Win64-Shipping.exe`) | Raccoon Rampage (`RSGame-Win64-Shipping.exe`) |
|---|---|---|
| De DLL wordt geladen door | `UUsbBPLibrary::Usb_ImportDLL` (RVA 0x7b8290): `FPaths::ProjectPluginsDir() + "UsbPluginsDll.dll"`, `FPlatformProcess::GetDllHandle` (absoluut pad), daarna `GetDllExport("BIND_...")` per functie in de `m_dll*`-pointers | idem (RVA 0x9dc380) |
| Prototypes (uit de gedecoreerde namen van de pointers, bv. `?m_dllGetESIoData@@3P6A_NAEAUFESIoData@@@ZEA`) | `bool InitUnisIo()`, `bool IsIoWorking()`, `bool GetESIoData(FESIoData&)`, `bool OpenESIODevice(int)`, `bool OpenGunDevice(const wchar*, const wchar*, int, int, bool)`, `int GetGunData(int, FPACKET*)`, `int GetCoinNum(int)`, `int DecCoins(int,int)`, dongle: `bool InitDongle()`, `IsDongleExist()`, `IsDongleVerify(uchar[8])` | idem met `bool GetIoData(FCoUnisIoData&)`, zonder gun-functies; `GetCoinNumByPlayer`, `AddKeyboardCoins` |
| Structuren (PDB) | `FESIoData` 16 bytes (Mark, IoData1..14, Crc); `FPACKET` 92 bytes (iX +20, iY +24, iUSB +52, LeftTop +56, Middle +64, RightBotton +72, bOutRange +80, bLeftBtn +81, bIsHandleVerify +89); `FCoUnisIoData` is hier maar 17 bytes en wordt alleen door Blueprint gebruikt (`Usb_IoGetIoData` heeft geen C++-aanroeper) | `FCoUnisIoData` **49 bytes** (UsbIo, Mark, IoData1..46, Crc); `FPACKET` hetzelfde (92) |
| Wie de bytes leest | `ABPGameState::Tick` (0x7b2f90): `IsExistESIODevice` -> `GetESIoData(this+0x2cc)` -> `ParseIOData` (0x792410); met `USettingData::iGunType == 0` (standaard: de constructor zet hem niet, `LoadData` leest hem alleen als hij in de ini staat) per speler `GetGunData(i, &packet[i])` | `ABPGameState::GetIoData` (0x9b0ef0): `IsIoWorking` -> `GetIoData(this+0x2c0)` -> `ParseIOData` (0x9bfd00); `GunDataToScreen` (0x9b3110) rekent de ruwe punten om met het 4x4-rooster van `Setting_Hardware.ini` |
| Start van het bord | `UBPGameInstance::InitDong` (0x76ea10): `InitDongle`, daarna `IsDongleVerify({b2 93 10 12 c0 cf 9b ae})`; `false` geeft het scherm "dongle ontbreekt". `OpenGunDevice(".../Setting_GunCalibrate.ini", reserve, 1080, 1920, true)` in `PostInitializeComponents` (0x799970+0x576): de DLL beheert zelf de geweerkalibratie, het spelbeeld is 1080x1920 | `UBPGameInstance::InitIO` (0x9b5b70): `InitUnisIo` moet `true` geven, anders volgt een foutmelding en een nieuwe poging; daarna `StartCheckDongle`, `SetLockCoin`, licht |

**De bytes** (index = byte in het rapport; bit 7 = hoogste):

* Raccoon, `ParseIOData`: byte 2 (IoData1) bit 2 = Test (`Control_Move_Up`), bit 1 = Service (`Control_Move_Down`), bit 0 = Enter (`Control_Enter`); byte 5 (IoData4) bit 7/6/5/4 = trekker speler 1..4
  (`FPACKET.bLeftBtn`; de opgaande flank roept `ABPPC::StartPlay(speler)` aan: de trekker IS de start), bit 3/2/1 = waterstand laag/midden/hoog (`CheckIsOpenWaterTank`, 0x9ab780); byte 7 wordt door de game
  overgeslagen (de DLL telt de muntingangen zelf); byte 10..25 = ruwe aanwijspunten als 16-bit big-endian X,Y van speler 1..4; byte 1 = 0xAA, byte 48 = ~som(byte 1..47) (zo controleert het origineel zijn eigen
  HID-rapport, `sub_17930` in de originele DLL; de game leest hem niet).
* Elevator Action, `ParseIOData`: byte 2 (IoData2) bit 0 = Enter, bit 1 = omlaag, bit 2 = omhoog, bit 5 = start 1 (`StartPlay(0)`), bit 4 = start 2; byte 3/4 = deursensoren; byte 5..12 = ruwe punten en byte 13
  bit 1/0 = trekkers, alleen in de ES-gewerenmodus (`iGunType == 1`). In de standaardmodus komen punt en trekker uit `GetGunData`: iX/iY zijn beeldpunten (de Tick klemt op 70..1010 en 70..1850), `bLeftBtn` start
  `ABPPC::OnFireDown`.
* Munten: `GetCoinNum(speler)` is een teller die de game elke tick met de vorige waarde vergelijkt (`ParseIOData`); `DecCoins` trekt af (niet onder 0), `AddKeyboardCoins` telt op: de semantiek van het origineel
  (`sub_17700`, `sub_16ed0`, `sub_17250` in de DLL).
* Geweerkalibratie van Raccoon: `Setting_Hardware.ini` van de eigenaar heeft het rooster in ruwe punten `b*257` (4112 = 16*257 ... 61423 = 239*257) en `GunCalibrate_bIsHandleVerify_1P=1`: de kalibratie is dus gemaakt met de
  assen van het bordbeeld als hoog=laag byte. rg-unis levert dezelfde `b*257`, zodat die kalibratie blijft kloppen.

### Wat we bouwen

`rg-unis` = een eigen `UsbPluginsDll.dll` die alle 112 namen van beide originelen exporteert (gecontroleerd met `objdump -p`/pefile tegen de exporttabellen: geen ontbrekende en geen extra naam), de dongle laat slagen, het bord als
aanwezig meldt en `GetIoData`/`GetESIoData`/`GetGunData` bouwt uit `TeknoParrot_JvsState` (de Windows-mapping van `rg-jvsbridge.exe`; 32-bits bitveld op offset 8, acht assen op 12..19), met dezelfde tabel-opzet als rg-adrio
(`rg-unis.ini`). Het draait zonder lader en zonder kern (`[Loader] Core = none`), dus zonder `TeknoParrot(64).dll`.

* Kandidaten: `docs/research/wartran/patches/RaccoonRampage/game.kandidaat.ini` (naast de huidige, die op de TP-kern blijft; die is "Working" en blijft ongemoeid) en
  `docs/research/wartran/patches/ElevatorActionInvasion/game.kandidaat.ini`, elk met `dropin/dropin.ini` en `dropin/rg-unis.ini`. De DLL zelf komt uit `build-unis.sh` (geen binair bestand in git).
* Engine-wijziging: een drop-in mocht niet buiten de map van de exe liggen; UE4 zet de exe in `<Game>/Binaries/Win64` en laadt de DLL uit `<Game>/Plugins`. `To = ../../Plugins` mag nu (nog steeds binnen de view;
  `src/ravengun/launch/direct_files.cpp`, `docs/dropins.md`). Zonder die wijziging zette het plan de DLL op `.../Win64/RSGame/Plugins/` en vond de game hem nooit.
* Controle zonder TeknoParrot: `tools/dev/unis-kandidaat.sh` bouwt de DLL, zet per game een wegwerp-patchmap neer en draait `RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan <Game> --patches <map>` plus
  `--tp-refs --expect-free <Game>`. Gemeten 2026-10-17: beide games `verdict ok`, `core_dll`/`loader` leeg, `tp_files []`, 0 verwijzingen naar de TeknoParrot-map, drop-in-doel
  `views/<Game>/<Game>/Plugins/UsbPluginsDll.dll` (en `rg-unis.ini` ernaast).

### Wat NIET bewezen is (eerlijk gemarkeerd; alles hieronder is GEGOKT of ongemeten)

1. **Dat het werkt.** Er is geen game gestart. Alles hierboven is lezen van code en types plus een eenheidstest van de eigen rekenkern.
2. **`UsbIo` (byte 0 van het Raccoon-rapport) = 1.** `IsGun(speler)` is bij `iGunType == 0` `FPACKET.iUSB > 0`, de constructor zet `iUSB = -1` en in de C++ van Raccoon schrijft niets `iUSB`: het zal Blueprint zijn
   (vermoedelijk uit `UsbIo`). Staat de banner "The guns are not calibrated" er na de proef nog, probeer dan `usbio = 0|2|3` in `rg-unis.ini`. De banner zelf (Blueprint in de pak) is niet gelezen.
3. **Geweerkalibratie van speler 2..4 (Raccoon)**: `Setting_Hardware.ini` heeft alleen speler 1 (`bIsHandleVerify_1P=1`). De game kan voor 2..4 om kalibratie vragen of de banner houden; dan in het testmenu kalibreren
   (de assen zijn 0..255 over het hele scherm) of het rooster van speler 1 kopieren. Niet gemeten of de banner daar van afhangt.
4. **Waterstand "hoog" aan (Raccoon, `water = 1`)**: afgeleid uit `CheckIsOpenWaterTank` (zonder laag/midden/hoog meldt de game een lege tank). Of "aan" echt "vol" betekent is niet bewezen (`Setting_Water.ini`:
   `bEnableWater=true`).
5. **Raccoon: start/munt.** Het TeknoParrot-profiel noemt de startknoppen "Credits P1..P4" en de trekker is de start in het spel; daarom tellen start 1..4 (bits 6/11/16/21) als munt 1..4 en ook de muntknoppen (bit 2/3)
   voor speler 1/2. Enter van het testmenu = volume omhoog (F4), omdat de huidige `game.ini` zegt: "service (F3) = omlaag, F4 = enter"; het profiel zegt `Enter = P1Button5`, een knop die de indeling WartranTroopers niet heeft.
6. **Elevator Action: de CRC van het ES-rapport** (zelfde som als Raccoon: gegokt; de game leest hem niet) en **de deursensoren** (0).
7. **EAI: de plaats van het spelbeeld.** De game is 1080x1920 (staande kast); `aim.mode = column` + `aim.out = 1920x1080` neemt aan dat het beeld als smalle kolom midden in het 16:9-scherm staat (gamescope `-S fit`, zoals de
   bevinding "Staande kast: het beeld staat smal in het midden"). Of de game met `-windowed -ResX=1080 -ResY=1920` ook echt zo opstart en hoe gamescope dat zet is niet gemeten; het kandidaat-`game.ini` geeft die argumenten
   mee (de TP-kern zette de resolutie 1080x1920; zonder kern is de standaard van de game niet bekend). Klopt de aanwijzing niet, pas `aim.out`/`aim.mode` aan in `rg-unis.ini` (geen herbouw nodig).
8. **Gamescope** (`[Display] Gamescope = true`) is overgenomen van Rampage en Mars Sortie (zwart beeld zonder); voor deze UE4-games niet gemeten.
9. **Dewo/OpenCV (Raccoon, webcam/kaartlezer)** is niet aangeraakt; zonder camera hoort de game dat te negeren, niet gemeten.
10. Raccoon draait op de TP-kern en is "Working": deze kandidaat vervangt dat pas na een geslaagde tv-proef.

### Tv-recept UNIS (per game, door wie de bench heeft)

Voorbereiding (geen enkel bestand in `ravengun-patches` wordt aangeraakt): `cmake -S . -B build && cmake --build build -j && tools/dev/unis-kandidaat.sh` zet `build/unis-kandidaat/<Game>/{game.ini,dropin/}` neer.
Start via de directe route met die patchwortel, met de invoerbrug van de daemon erbij: `ravengunctl iobridge start <Game> --route tp-shm` en
`build/rg-direct --plan <Game> --patches build/unis-kandidaat > plan.json && build/rg-direct --run plan.json` (of de map als gebruikerslaag in `~/.local/share/ravengun/patches/<Game>/` zetten; die laag raakt ook de app
van de eigenaar, dus daarna weer weghalen).

Logregels die het bewijzen (logboek van rg-direct en `rg-unis.log` in de view, `views/<Game>/<Game>/Plugins/rg-unis.log`):

| Regel | Bewijst |
|---|---|
| rg-direct: `[Loader] Core = none: Wine start de exe zelf, zonder lader en kern` en `drop-in: .../Plugins/UsbPluginsDll.dll geschreven` | geen lader, onze DLL geplaatst |
| `rg-unis: tabel: .../rg-unis.ini (game eai\|rr, usbio 1, aim ...)` | ONZE DLL is geladen, met zijn tabel (het origineel schrijft dit niet) |
| `rg-unis: bordbeeld TeknoParrot_JvsState geopend` | de brug in de prefix is gevonden; staat er in de telregel `(GEEN bordbeeld)`, dan draait de brug niet |
| `rg-unis: eerste aanroep: BIND_InitUnisIo`, `BIND_IsIoWorking`, `BIND_GetIoData` (Raccoon) of `BIND_IsExistESIODevice`, `BIND_GetESIoData`, `BIND_OpenGunDevice`, `BIND_GetGunData` (EAI), `BIND_InitDongle`, `BIND_IsDongleVerify` | de game vraagt het bord aan onze DLL |
| `rg-unis: telling: GetIoData=N ... bits=0x...` elke 10 s met N stijgend | de game blijft pollen; `bits` verandert als de tester een knop indrukt (TEST = bit 0) |
| `/proc/<pid>/maps` van het spelproces: `.../Plugins/UsbPluginsDll.dll` met de sha256 uit `BUILDINFO-unis.txt`, en GEEN `TeknoParrot`/`OpenParrot` | geen enkel TP-bestand in het proces |

Spelbewijs (in volgorde; elk met een beeld/contactvel van de coordinator): (1) attract zonder de banner "The guns are not calibrated" (Raccoon); (2) TEST indrukken opent het testmenu binnen 1 s (Raccoon: bit 0 = Test;
EAI: bit 0 = Enter); (3) munt: `munt=` in de telregel stijgt en het HUD toont het krediet (Raccoon: start 1 = bit 6, of toets 5 = bit 2); (4) trekker: in Raccoon start de trekker de speler (`StartPlay`), in EAI start 1 = bit 6;
(5) richten en schieten: het kruis volgt de assen (EAI: let op de kolom), de score stijgt; (6) 5 minuten attract of spel (beginscherm/kalibratie telt niet).
Valt iets uit, dan zegt de telregel waar: `GetGunData=0` (EAI) = de game gebruikt de ES-geweermodus of kent het bord niet; geen `eerste aanroep:`-regels = de game laadt een andere DLL (pad van de drop-in) of de DLL kon niet
laden (Wine: ontbrekende import; de DLL importeert alleen kernel32 en de UCRT-doorverwijzers api-ms-win-crt-*, zoals rg-adrio).

## Drakons Realm Keepers: wat de IL2CPP-dump zegt (2026-10-17)

*Il2CppDumper 6.7.46 (net7, in de dotnet-sdk-container, buiten de repo) op `GameAssembly.dll` + `global-metadata.dat` (versie 29): `dump.cs` met klassen, velden en RVA's. Niets gestart, geen bestand gewijzigd.*

* `RS_232_DLL.dll` wordt aangeroepen door `SBK.RS232.RS232DLL` (RVA 0x3449f0..: `RS232_OpenComport/PollComport/SendBuf/CloseComport/GetPortIndexByName`). De gebruikers zijn seriele protocollen met DLE-framing (byte 254) en
  een controlesom: `SBK.KaboomLib.SerialProtocol` (basis van `PneumaticSeatController`: de zitbank/turret van Skyride, met `ControllerTypes.eCMD`, `Button`, `Coin`, `GetInitInfo`, `PanicRawState`) en
  `SBK.LEDSystem.LedControllerLib.SerialProtocol_Leds` (ledbesturing). **Het is dus geen invoerbord voor de aanwijzing**; de aanname in de tabel hierboven ("serieel bord via RS_232_DLL") was te grof.
* Munt en start zijn Unity-invoer: `CoinInput : ArcadeInput` heeft `keyboardInputs` (een lijst `ArcadeInput`, dus toetsenbordacties) en reageert ook op `PneumaticSeatManager.EventCoinState` (de zitbank, als die er is:
  `SeatExist`, `RetryConnecting`); `KaboomBridge` ontvangt `OnCreditReceived(ArcadeInput)`. De `KaboomAdrioFxPlus*64.dll`-bestanden in de map worden niet geimporteerd (alleen `RS_232_DLL` en `matrix64` staan tussen de
  native bibliotheeknamen).
* Gevolg: de TeknoParrot-kern leverde hier toetsenbord en muis in het proces (profiel `Drakons.xml`: `Input API = RawInput`; gemeten op de tv: munt = toets 5, start = toets 1, daarna richten/schieten met de muis). Zonder kern
  leest het spel de echte apparaten van Wine; `[Input] Route = tp-shm` heeft dan geen lezer. Wat nodig is: een **invoerinjector in het spelproces** die `TeknoParrot_JvsState` leest en met `SetCursorPos`/`SendInput` (muis,
  toetsen) invoert, geladen via een bibliotheek die het spel toch al laadt (kandidaat: de `RS_232_DLL.dll` zelf, 18 KB, P/Invoke van `RS232DLL`; niet gecontroleerd waar het spel hem aanroept en of het bij een ontbrekende
  poort doorgaat). Dat is een nieuw onderdeel, zonder tv-proef niet te beoordelen: daarom NIET gebouwd en geen kandidaat gezet.
* Open (nodig voor die stap): welke toetsen/acties het spel bindt (de Input System-acties staan in de levels, `<Keyboard>`-paden zijn in `resources.assets` en `globalgamemanagers` niet in plain text gevonden), of
  `RS232DLL.Open` bij een ontbrekende poort veilig faalt, en of een absolute muispositie (`SetCursorPos` in gamescope) de Unity Input System-positie haalt.
