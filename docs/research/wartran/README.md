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

Volgende onderdelen (niet gebouwd, zie de tabel): een UNIS-drop-in `UsbPluginsDll.dll` (de `BIND_*`-bibliotheek van UNIS: Raccoon
Rampage en Elevator Action Invasion laden hem, Night Hunter heeft dezelfde familie als `DllBindFunc.dll`), een uhid-bord voor het
After Dark-bord, een virtuele joystick voor Bug Busters en een `RS_232_DLL.dll`-drop-in voor Drakons. De Konami-I/O-drop-in
(`iob.dll` + `gunio.dll`/`ballio.dll`) is gebouwd: `tools/rg-konami/`, `docs/research/konami/`.

## Per game

| Game | Engine | Wat de kern deed (gemeten in de game) | Vervanger | Stand |
|---|---|---|---|---|
| **MarsSortie** | Unity Mono 64 | antwoorden voor MSTPPlugin (knoppen, richtpunt, resolutie); I/O-bord GMB en SuperDog worden door MSTPPlugin uitgezet | `RavenGun.MarsSortie`: vult `MRCQ.Hardware` (SetInputKey/GetInputAxis) uit het bordbeeld, zet GMB, GameLoader, SuperDog en bordfouten uit | **klaar om te testen** |
| **WildWestShootout** | Unity 5.6 Mono 32 | serieel bord (trekker, munt, testmenu) nagebootst + antwoorden voor WWSTPPlugin (richtpunt); camera MVSDK en SuperDog uitgezet door WWSTPPlugin | `RavenGun.WildWestShootout`: richtpunt (`BaseGun.GetGunPos`), trekker (`BaseGun.OnShoot`), munt (`InCoinAction`), herladen, start, testmenu; camera, bord, dongle, witte flits uit; opstart meteen naar de attract | **klaar om te testen** (meeste risico: de opstart en het testmenu) |
| **AfterDark2** (Night Hunter) | Unity Mono 64 | antwoorden voor NHAD2TPPlugin (muisroute, knoppen, resolutie) en de dongle: SenseLock Sense4 (`Sense4.dll`, `dog_hardware_sense_4`; oud: `DllBindFunc`) | `RavenGun.AfterDark2`: muisroute aan, richtpunt/trekker per speler, munt/start/speciaal/wapen/stuur/testmenu, toetsenbord uit, dongle zegt ja | TV 2026-10-17: "Please insert the dongle" (Sense4 niet afgehandeld); **dongle toegevoegd, opnieuw te testen** |
| **Rampage** | Unity Mono 64 | Kaboom/Adrio-bord (`KaboomAdrioFxPlusFourFeeders64.dll`) | eigen `KaboomAdrioFxPlusFourFeeders64.dll` (rg-adrio) + `rg-adrio.ini` | TV 2026-10-17: beeld zwart, maar de game liep (Unity-logboek: "Kaboom Connected To Serial : 2", attract-scènes wisselen) en exclusief schermvullend zonder gamescope; **nu in gamescope, opnieuw te testen** |
| **DrakonsRealmKeepers** | Unity **IL2CPP** 64 | serieel bord via `RS_232_DLL.dll` (`RS232_OpenComport/PollComport/SendBuf`); Kaboom-DLL's liggen er maar worden niet aangeroepen | eigen `RS_232_DLL.dll` die het bordprotocol spreekt | **geblokkeerd**: protocol staat in GameAssembly.dll (IL2CPP, geen BepInEx 5) |
| **ElevatorActionInvasion** | UE4 64 (PDB aanwezig) | het UNIS-I/O-bord (USB-HID) via `ESGame/Plugins/UsbPluginsDll.dll` (Usb_ImportDLL laadt hem; 78 exports `BIND_*`: InitUnisIo, GetIoData, GetGunData, IsIoWorking, dongle InitDongle/IsDongleExist/IsDongleVerify, munten) | eigen `UsbPluginsDll.dll` (drop-in, zoals rg-adrio): `BIND_GetIoData`/`BIND_GetGunData` uit het bordbeeld, dongle ja, de rest stubs | **pad gevonden, niet gebouwd**: de indeling van de buffers van GetIoData/GetGunData moet uit de DLL (of de PDB van de game) gelezen worden |
| **RaccoonRampage** | UE4 64 (PDB aanwezig) | idem: `RSGame/Plugins/UsbPluginsDll.dll` (UNIS, 110 exports; TeknoParrot heeft hem op 8 bytes aangepast, het origineel staat ernaast als `.original`) + `DewoDll.dll` (camera-scan) | dezelfde `UsbPluginsDll.dll`-drop-in als EAI | **pad gevonden, niet gebouwd** (als EAI) |
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
