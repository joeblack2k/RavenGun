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
Rampage en Elevator Action Invasion laden hem, Night Hunter heeft dezelfde familie als `DllBindFunc.dll`), een Konami-I/O-drop-in
(`iob.dll` + `gunio.dll`/`ballio.dll`), een uhid-bord voor het After Dark-bord en een `RS_232_DLL.dll`-drop-in voor Drakons. De
virtuele joystick voor Bug Busters is gebouwd, als eigen `dinput8.dll` (`tools/rg-dinput8/`, zie "Bug Busters").

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
| **AfterDark** | Virtools 32 | HID-bord `VID 0471 PID 485E` (HidD_Get/SetFeature uit een dynamisch geladen hid.dll) en een tweede apparaat `ITOKEN2` (QueryDosDevice + CreateFile + DeviceIoControl `0x22000C`/`0x220010`) | hid.dll-drop-in die SetupDi/CreateFile in de IAT van GamePlayer.exe omleidt, plus een ITOKEN2-nabootsing | **geblokkeerd**: twee protocollen, geen van beide uit de aanroepen gelezen (zie "After Dark" hieronder) |
| **LethalEnforcers3** | eigen 32 (Konami) | Konami `iob.dll` (60 exports) + `gunio.dll` (18) | eigen `iob.dll` + `gunio.dll`; Japanse locale via `LC_ALL` | **geblokkeerd**: structuren (`IOB_DIGITAL_IN_W`, `gunio_read_data`) nog niet uit de aanroepen gelezen |
| **Gashaaaan** | eigen 32 (Konami) | Konami `iob.dll` + `ballio.dll` (bs2_*, ball_io_*) | als LE3 (zelfde familie, andere build van iob.dll) | **geblokkeerd**: als LE3 |
| **BugBusters** | eigen 32 (DirectDraw 7) | een DirectInput8-joystick per speler (`c_dfDIJoystick2`, absolute X/Y), munt/start/test (het seriele bord is in deze build dood code), venstermodus en de cd-rom-controle | eigen `dinput8.dll` (rg-dinput8) + `rg-dinput8.ini` + `control.cfg`, `[Loader] Core = none` | **klaar om te testen** (zie "Bug Busters" hieronder) |
| **Coopers9** | eigen 32, geheugendump van een beschermd proces | HASP HL (statisch) + DirectInput8; de importtabel ligt in niet-geïnitialiseerd `.data`, de IAT bevat absolute adressen uit een Windows XP-proces (`0x7C80xxxx` = kernel32) en naar `.protect`-stubs (`0x0313xxxx`; `.protect` is op schijf alleen nullen) | eerst een importtabel herbouwen (welk IAT-slot welke API is), dan HASP en invoer | **geblokkeerd**: zonder de tabel slot -> API start de exe niet (zie hieronder) |
| **RobinHood** | eigen 32 (D3D9, DINPUT `DirectInputCreateA`) | HASP HL (statisch; `\\.\HARDLOCK.VXD`, `\\.\FEnteDev`) + een serieel I/O-bord (`COM%d`, "I/O ERROR 1..4", "Check if any wire disconnection with I/O board.") | het bord via een pty (zoals rg-ptylink) met een eigen bordspreker, plus een HASP-laag op de statische functies | **geblokkeerd**: bordprotocol en HASP-gebruik niet gelezen (zie hieronder) |
| **PigsMightFly** | Sega-shell + PigsMightFly.exe | Sega-shell (I/O-bord) + Dinkey (`dk2win32.dll`) | de route van Men in Black (game zonder shell) + Dinkey-laag | **geblokkeerd**: shell en Dinkey niet nagebouwd |

## Bug Busters (BBPC.exe, 2026-10-17): klaar om te testen

Alles gelezen in `BBPC.exe` (sha256 `12c7b86b…`, alleen gelezen op de NAS; objdump + capstone). Imports: `DINPUT8!DirectInput8Create`,
`DDRAW!DirectDrawCreateEx`, `DSOUND`, `WINMM`, `KERNEL32` (met `CreateFileA`/`SetCommState`/`WaitCommEvent`), `USER32`, `GDI32`,
`ADVAPI32`, `ole32`. Geen dongle.

**Wat de game leest** (adressen zijn VA's, beeldbasis `0x400000`, geen relocaties):

| Wat | Waar | Bewijs |
|---|---|---|
| DirectInput: `EnumDevices(DI8DEVCLASS_ALL, …, DIEDFL_ATTACHEDONLY)`; een muis (`dwDevType & 0xFF == 0x12`) wordt overgeslagen, toetsenbord (`0x13`) krijgt `c_dfDIKeyboard`, al het andere `c_dfDIJoystick2` (`0x453034`, 164 objecten, 272 bytes) | `0x4102c7`, callback `0x410350` | `0x410365`, `0x410523` |
| per as `SetProperty(DIPROP_RANGE, DIPH_BYID, -1000..1000)` | `0x4106d4` | `lMin = 0xFFFFFC18`, `lMax = 0x3E8` |
| `control.cfg` (werkmap, `"rb"`): per speler een dword modus (0 muis, 1 toetsenbord, 2 joystick absoluut, 3 joystick als richtingtoetsen), bij modus > 1 de 16 bytes `guidInstance`; geen bestand of een fout: speler 1 muis, speler 2 toetsenbord | `0x411696`, standaard `0x41182d` | per speler `0x18` bytes vanaf `0x463490` |
| elke beeldstap: `Poll` + `GetDeviceState(0x110)`; modus 2: `0x4107ee(speler, lX, lY)`; `rgbButtons[0] & 0x80` = sproeien, `rgbButtons[1]` = herladen (tank `+0.25`, max `1.0`, in toestand 5/9) | `0x410d25` (vanuit `0x40ab24`) | `0x41113f`, `0x411386`, `0x4113ba` |
| richtpunt: `x = (lX+1000)*640/1100`, `y = (lY+800)*480/1800`; `lX <= -1000` of `lY <= -800` = buiten beeld; `100 < lX <= 300` wordt 100, `lX > 300` buiten beeld | `0x4107ee`, `0x410c51` | constanten `640.0/1100.0`, `480.0/1800.0` op `0x452330..0x452338` |
| muis (alleen modus 0): `WM_MOUSEMOVE` = richtpunt, `WM_LBUTTONDOWN/UP` = sproeien, `WM_RBUTTONDOWN` = herladen; toetsen: `WM_KEYDOWN` `1`/`2` = start, Enter, Esc, F12 (stoppen) | vensterprocedure `0x40a35f`, toetsen `0x40a749` | |
| het I/O-bord: een seriele leesdraad (`COM%d:%d,n,8,1`, `0x443efa`/`0x43c1fa`) en de verwerker `0x43be99` (`'0'` munt: credits `0x460C1C += 1` + Coin.wav; `'1'` service-credit; `'2'`/`'3'` start 1/2 -> `0x49E880`/`0x49EA84`; `'4'` test -> `0x49E870`; `'5'` -> `0x49732C`; `'6'` uitzetten) worden **nergens aangeroepen** (geen verwijzing naar die adressen; de bordfuncties `0x43c438`/`0x43c43d`/`0x43c447` zijn lege stubs). De vlaggen worden wel elke beeldstap verwerkt: `0x40a872` | `0x43bf9e..0x43c084` | |
| cd-rom-controle: zoekt een schijf met `GetDriveTypeA == 5` en volumelabel `BBPC`, anders een Koreaanse MessageBox met opnieuw/annuleren | `0x409e30`, lus `0x409f32` | `BBPC_standalone.exe` op de NAS verschilt in precies één byte: `0x9F46` `74 -> 75` |
| schermvullend of venster: `0x401145(0, …, 1)`: 1 = `ChangeDisplaySettingsA` 640x480x16 + exclusief; 0 = `DDSCL_NORMAL` met een eigen backbuffer | aanroep `0x40a03e` (`6A 01`) | |
| schrijft `config\buster.cfg`, `config\score.cfg`, `config\color.cfg` (`"wb"`) | `0x411b2e`, `0x411c7f`, `0x411d4f` | |

**De vervanger.** `tools/rg-dinput8/` bouwt een `dinput8.dll` (32-bits, alleen kernel32 en de UCRT-stubs van Wine, zoals rg-compat)
met een eigen `IDirectInput8A`: die somt alleen de geweren op (een joystick per speler, GUID `{52617645-6E47-756E-0000-00000000000n}`),
leest bij elke `GetDeviceState`/`Poll` het bordbeeld (`TeknoParrot_JvsState` van rg-jvsbridge) en geeft X/Y in de eenheden uit
`rg-dinput8.ini` (`as.x = -999 100`, `as.y = -799 1000`, buiten beeld `-1000`/`-800`), knoppen per bit. In dezelfde stap (de draad
van de game) zet hij bij een opgaande flank de bordvlaggen (`vlag.0x…`) en telt hij de munt bij de credits (`teller.0x460C1C`). In
`DllMain` past hij twee bytes van de exe aan (`patch.0x40A03F = 01 -> 00` venster, `patch.0x409F46 = 74 -> 75` geen cd-rom), maar
alleen als **alle** oude bytes kloppen; anders geen patch, geen vlag en geen teller. `dropin/control.cfg` (40 bytes) kiest voor beide
spelers modus 2 met de GUID van geweer 1 en 2. De game draait met `[Loader] Core = none` in de map van de view;
`ViewRealDirs = config` + `ViewCopies` houden de drie configbestanden lokaal. Test: `tests/dinput8_test.sh` (ctest
`ravengun_dinput8`): tabel, schaal en de randen zoals de game ze terugrekent, buiten beeld, flanken, `control.cfg` tegen `rgd_guid`, de
patches tegen de bytes van `BBPC.exe` (als de NAS gekoppeld is), de exports van de DLL. Plan zonder TeknoParrot:
`RAVENGUN_TP_DIR=/nonexistent build/rg-direct --plan BugBusters --patches <kopie met _defaults en BugBusters>` geeft `ok`, `tp_files []`,
geen lader, geen kern, `WINEDLLOVERRIDES dinput8=n`; `rg-direct --tp-refs --expect-free BugBusters`: 0 verwijzingen.

**Gemeten onder Wine, zonder spel** (`tools/rg-dinput8/wine-proef.sh`, Proton 11, eigen wegwerpprefix, geen scherm, 2026-10-17): een
proefprogramma dat `DINPUT8!DirectInput8Create` statisch importeert zoals BBPC.exe krijgt met `dinput8=n` onze DLL uit zijn eigen map
(laadbewijs in het logboek), de patch staat er vóór `main`, `EnumDevices` geeft twee geweren (type `0x214`), `CreateDevice`,
`GetCapabilities` (2 assen, 8 knoppen), `SetDataFormat(c_dfDIJoystick2)`, `EnumObjects` + `DIPROP_RANGE` BYID per as lukken;
`GetDeviceState` geeft voor as-bytes 128/128 `lX -447 lY 104`, na een muntbit is de teller precies één hoger, na de startbit de vlag 1,
en met de trekker buiten beeld `lX -1000 lY -800 knop0 0x80`. Wat dat niet bewijst, staat hieronder.

**Niet bewezen (alleen statisch gelezen; geen run met de game):**
- dat de venstermodus onder gamescope beeld geeft zoals `Windowed = 1` met de kern (aanname: de kern deed hetzelfde; gemeten is alleen
  dat de kern met `Windowed = 1` beeld gaf en met 0 zwart);
- de schaal van het richtpunt op het echte beeld (afgeleid uit `0x410c51`; het testmenu "GUN TEST" laat het zien);
- wat de test-, service- en startvlag in elke toestand doen (gelezen in `0x40a872` en het testmenu `0x43d640`, niet gezien);
- de munt telt mee maar zonder het muntgeluid (dat speelde de dode bordverwerker af); de service-credit van het bord zit er niet in;
- de echte toetsenborden/joysticks zijn via DirectInput weg (modus 1/3 kan niet meer); `WM_KEYDOWN` (Esc, `1`, `2`) blijft werken;
- `InstantReload`, `UseLocaleEmulator` en de gevoeligheid uit `[Config]` doen op deze route niets (dat waren opties van de kern).

**Tv-testrecept** (bench nodig; de hoofdsessie start, niet deze agent):
1. Uitrollen in de echte checkout: `tools/dev/wartran-uitrol.sh BugBusters` (bouwt `dinput8.dll` in `patches/BugBusters/dropin/` en
   kopieert `game.ini`, `dropin.ini`, `rg-dinput8.ini`, `control.cfg`; stopt als `patches/BugBusters/game.ini` niet meer de basis is).
   Dan de app zoals altijd (cmake --build, install-user.sh, daemon herstarten) en de TeknoParrot-map wegzetten voor het bewijs.
2. Dubbelklik Bug Busters in de app. Verwacht: **geen** Koreaanse cd-rom-vraag, beeld in gamescope (venster van de game, geschaald).
3. Bewijs: `~/.local/share/ravengun/runtime/views/BugBusters/rg-dinput8.log` met `geladen in pid … (laadbewijs)`,
   `tabel: …rg-dinput8.ini (2 spelers, 4 vlaggen, 1 tellers, 2 patches)`, `patch 0x0040A03F`, `patch 0x00409F46`,
   `DirectInput8Create(versie 0x0800)`, `CreateDevice: geweer van speler 1` en `2`, `DIPROP_RANGE X+Y = -1000..1000` (of per as),
   `bordbeeld TeknoParrot_JvsState geopend`. In `/proc/<pid>/maps` van BBPC.exe: `views/BugBusters/dinput8.dll`, geen `TeknoParrot`,
   geen `OpenParrot`.
4. Munt (toets 5 / munt van de Move): logboek `teller 0x00460C1C += 1 (nu N)`, credits op het scherm. Start (toets 1): `vlag 0x0049E880
   := 1`, het spel begint. Richten: `ravengunctl test-pattern point` naar de vier hoeken en het midden, vergelijk met het vizier;
   sproeien (trekker) en herladen (knop 2). Speler 2 met `ravengunctl --player 2 …` (start toets 2, `vlag 0x0049EA84`).
5. Test (de testknop van RavenGun, bit 0): naar het testmenu, daar "GUN TEST" voor de schaal; service = kiezen.
6. 5 minuten attract of spel (de lat). Na afloop: `config/` in de view is een echte map, de NAS-bestanden ongewijzigd (mtime).
Terug naar de kern: in ravengun-patches `git checkout -- BugBusters/game.ini` en `rm -r BugBusters/dropin`.

## After Dark (GamePlayer.exe, Virtools): geblokkeerd

Gelezen in `GamePlayer.exe` (sha256 `d4ffc802…`; `GamePlayer.exe.original` verschilt alleen in vier bytes van de PE-kop): `hid.dll`
wordt dynamisch geladen (`0x4368f0`: `LoadLibraryA("hid.dll")`, dan `HidD_GetHidGuid`/`HidD_GetFeature`/`HidD_SetFeature`), SETUPAPI
statisch (`SetupDiGetClassDevsA`, `SetupDiEnumDeviceInfo`, `SetupDiGetDeviceRegistryPropertyA`, `SetupDiDestroyDeviceInfoList`). De
init `0x436140` zoekt twee soorten apparaten (`0x436c20`): het bord op hardware-ID `HID\Vid_0471&Pid_485e` (soort 1, `0x435df0`) en een
apparaat op GUID `{171638F7-1EAD-4873-BA98-C966ABCF0142}` (soort 2). Bij die GUID hoort de naam `ITOKEN2` (`0x436260`); `0x436290`
zoekt hem in `QueryDosDeviceA`, `0x4364d0` opent hem met `CreateFileA`, en `0x436520`/`0x4365d0` praten ermee via `DeviceIoControl`
`0x22000C` (schrijven, pakket van lengte+8) en `0x220010` (lezen, 10 pogingen); `0x436690` bouwt de pakketten. In de Virtools-blokken
heten de gebruikers "UserBB Dongle", "Dongle Auth", "UserBB Coins Gun", "Coin Insert" (`0x40c4d2`, `0x40c72f`, `0x40d7e2`, `0x40d3cc`).
`coin.ini` (NAS) heeft `[IO] IS=1` en een kalibratie per geweer (`Gun1xMin..Gun2yMax`).
**Wat ontbreekt:** de inhoud van de feature-rapporten (welke bytes trekker, munt en richtpunt zijn) en het ITOKEN2-protocol (wat "Dongle
Auth" vraagt en verwacht; mogelijk een uitdaging met een sleutel). Een hid.dll-drop-in kan het bord nabootsen (hij wordt geladen vóór de
eerste opsomming en kan dan SetupDi/CreateFile in de IAT van GamePlayer.exe omleiden), maar zonder het ITOKEN2-antwoord is er geen
bewijs dat de game verder komt. Daarnaast laadt rg-compat (DirectInput met lege HINSTANCE) nu via `Preload` van de OpenParrot-lader: met
`Core = none` is er dat kanaal niet; onze eigen lader (`[Loader] Build = ravengun`, `Core = ravengun`, `third_party/openparrot`, patch
0003) heeft het wel en is geen TeknoParrot. Niets gebouwd.

## Coopers 9 (game.exe): geblokkeerd

`game.exe` (41 MB) is een geheugendump van een beschermd proces: secties `.text`, `CONST`, `.rdata`, `.data` (virtueel `0x24C3E30`, op
schijf `0x29000`), `.rsrc`, `.protect` (`0x12EAC3` bytes, op schijf **alleen nullen**). Het startpunt `0x402480` is een functie met `ret
0x10` (WinMain, geen CRT-start). De importmap (RVA `0x027297C0`, `0x303` bytes) ligt in het niet-geïnitialiseerde deel van `.data`: de
Windows-lader ziet geen enkele import. De IAT aan het begin van `.rdata` (`0x512000`) bevat absolute adressen uit het gedumpte proces:
`0x7C80xxxx` (kernel32 van Windows XP), `0x77E4xxxx`/`0x77F4xxxx` (andere systeem-DLL's van XP) en `0x0313xxxx` (stubs van de bescherming
in geheugen dat niet in de dump zit). Strings van de statische HASP HL-bibliotheek (`enter hasp_login`, `\\.\HARDLOCK.VXD`,
`\\.\FEnteDev`) staan in de code. **Wat ontbreekt:** per IAT-slot de API (de kern van TeknoParrot moet die tabel hebben); de XP-adressen
zijn alleen op te lossen met de exporttabellen van precies die XP-DLL's, de `0x0313xxxx`-slots alleen uit het gebruik in de code. Pas
daarna komen HASP en invoer. Niets gebouwd.

## Robin Hood (data/game.exe): geblokkeerd

`data/game.exe` is een gewone exe (geen dump; startpunt `0x55F3E9`, importmap RVA `0x2224E8`): `d3d9`, `DINPUT!DirectInputCreateA`,
`DSOUND`, `WINMM`, `SHLWAPI`, `KERNEL32` met de seriele API (`CreateFileA`, `SetCommState`, `ClearCommError`, …) en de string
`COM%d` (bestandsoffset `0x21AC3C`). Het I/O-bord is serieel ("I/O ERROR 1..4", "Check if any wire disconnection with I/O board.", `0x61c4e4`/`0x61c524`).
De HASP HL-bibliotheek zit er statisch in (`\\.\HARDLOCK.VXD`, `\\.\FEnteDev`, `\\.\USBPDCOM`, `USB\Hardlock`, HL-RUS-licentie). Met
de kern van TeknoParrot speelbaar in muismodus (2026-10-17), dus de kern bootst het bord en de dongle na. **Wat ontbreekt:** het
bordprotocol (pakketten, baudrate) en welke `hasp_*` de game aanroept en hoe hij de antwoorden controleert. Route als die er zijn: COM1
van de prefix aan een pty (zoals rg-ptylink bij Target: Terror) met een eigen bordspreker uit het bordbeeld, en een HASP-laag op de
statische functies. Niets gebouwd.

## Uitrollen en testen

`docs/research/wartran/patches/<Game>/` bevat de klaargezette `game.ini` en `dropin/` voor de vijf games die klaar zijn (Bug Busters
sinds 2026-10-17; zijn `dinput8.dll` bouwt `tools/rg-dinput8/build.sh`, het script hieronder doet dat zelf). Het script
`tools/dev/wartran-uitrol.sh` bouwt de plugins en rg-adrio en zet alles in `patches/<Game>/` (alleen als de `game.ini` daar nog de
versie is waarop de klaargezette gebouwd is). Terug naar de kern van TeknoParrot, in ravengun-patches: `git checkout -- <Game>/game.ini`
en voor MarsSortie, WildWestShootout, AfterDark2 en BugBusters `rm -r <Game>/dropin` (die map is nieuw), voor Rampage `git checkout --
Rampage/dropin/dropin.ini` en `rm Rampage/dropin/{KaboomAdrioFxPlusFourFeeders64.dll,rg-adrio.ini,BUILDINFO-kaboom.txt}`. Een plugin
die blijft staan naast de kern van TeknoParrot geeft dubbele invoer (de kern bootst het bord ook na).

Bewijs per run: het logboek van BepInEx (`BepInEx/LogOutput.log` in de view: `RavenGun.<Game>: N van M haken gezet`, `bordbeeld:
TeknoParrot_JvsState geopend`, `TeknoParrot-plugin com.tp.* uitgezet`) en bij Rampage `Game_Data/Plugins/rg-adrio.log` in de view
(`InitLib(poort 2): bord van RavenGun actief`, `tabel: ...`, `bordbeeld TeknoParrot_JvsState geopend`). In `/proc/<pid>/maps` van het
spelproces mag geen `TeknoParrot64.dll`/`TeknoParrot.dll` en geen `OpenParrot` staan; het logboek van rg-direct zegt
`[Loader] Core = none: Wine start de exe zelf` en `start: cwd=<view>`.
