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
dezelfde familie als `DllBindFunc.dll`, die is nog niet gelezen), de Konami-I/O-drop-in (`iob.dll` + `gunio.dll`/`ballio.dll`:
`tools/rg-konami/`, `docs/research/konami/`) en de virtuele joystick voor Bug Busters als eigen `dinput8.dll` (`tools/rg-dinput8/`,
zie "Bug Busters"). Volgende onderdelen (niet gebouwd, zie de tabel): een uhid-bord voor het After Dark-bord en een invoerinjector
voor Drakons.

## Per game

| Game | Engine | Wat de kern deed (gemeten in de game) | Vervanger | Stand |
|---|---|---|---|---|
| **MarsSortie** | Unity Mono 64 | antwoorden voor MSTPPlugin (knoppen, richtpunt, resolutie); I/O-bord GMB en SuperDog worden door MSTPPlugin uitgezet | `RavenGun.MarsSortie`: vult `MRCQ.Hardware` (SetInputKey/GetInputAxis) uit het bordbeeld, zet GMB, GameLoader, SuperDog en bordfouten uit | **klaar om te testen** |
| **WildWestShootout** | Unity 5.6 Mono 32 | serieel bord (trekker, munt, testmenu) nagebootst + antwoorden voor WWSTPPlugin (richtpunt); camera MVSDK en SuperDog uitgezet door WWSTPPlugin | `RavenGun.WildWestShootout`: richtpunt (`BaseGun.GetGunPos`), trekker (`BaseGun.OnShoot`), munt (`InCoinAction`), herladen, start, testmenu; camera, bord, dongle, witte flits uit; opstart meteen naar de attract | **klaar om te testen** (meeste risico: de opstart en het testmenu) |
| **AfterDark2** (Night Hunter) | Unity Mono 64 | antwoorden voor NHAD2TPPlugin (muisroute, knoppen, resolutie) en de dongle: SenseLock Sense4 (`Sense4.dll`, `dog_hardware_sense_4`; oud: `DllBindFunc`) | `RavenGun.AfterDark2`: muisroute aan, richtpunt/trekker per speler, munt/start/speciaal/wapen/stuur/testmenu, toetsenbord uit, dongle zegt ja | TV 2026-10-17: "Please insert the dongle" (Sense4 niet afgehandeld); **dongle toegevoegd, opnieuw te testen** |
| **Rampage** | Unity Mono 64 | Kaboom/Adrio-bord (`KaboomAdrioFxPlusFourFeeders64.dll`) | eigen `KaboomAdrioFxPlusFourFeeders64.dll` (rg-adrio) + `rg-adrio.ini` | TV 2026-10-17: beeld zwart, maar de game liep (Unity-logboek: "Kaboom Connected To Serial : 2", attract-scènes wisselen) en exclusief schermvullend zonder gamescope; **nu in gamescope, opnieuw te testen** |
| **DrakonsRealmKeepers** | Unity **IL2CPP** 64 | **gecorrigeerd 2026-10-17 (IL2CPP-dump, sectie "Drakons")**: geen seriele invoerbord. `RS_232_DLL.dll` (`SBK.RS232.RS232DLL`) is alleen de seriele laag onder `PneumaticSeatController`/`SerialProtocol_Leds`/`SBK.KaboomLib.SerialProtocol` (zitbank, ledbesturing); munt en start komen als `ArcadeInput` (toetsenbord) in `CoinInput.keyboardInputs`; aanwijzen/trekker via Unity Input System | `rg-drakons` (`tools/rg-drakons/`): eigen `RS_232_DLL.dll` (geen poorten, zoals nu) met een draad die het bordbeeld leest en de muis van de game voert (aanwijzen + trekker, speler 1); `matrix64.dll` als drop-in; `[Loader] Core = none` | **kandidaat gebouwd 2026-10-18 (nog niet gestart)**: `patches-out/DrakonsRealmKeepers/`; munt/start blijven toetsen van het virtuele toetsenbord (`rg-drakons.ini`: `toetsen = 1` als terugval) |
| **ElevatorActionInvasion** | UE4 64 (PDB aanwezig) | het UNIS-I/O-bord via `ESGame/Plugins/UsbPluginsDll.dll` (Usb_ImportDLL laadt hem; 75 exports `BIND_*`): `BIND_GetESIoData` (16 bytes, start/menu), `BIND_GetGunData` (FPACKET, 92 bytes), munttellers, dongle | `rg-unis` (`tools/rg-unis/`): eigen `UsbPluginsDll.dll` + `rg-unis.ini`, `[Loader] Core = none` | **kandidaat klaar (statisch bewezen, niet gestart)**: `patches/ElevatorActionInvasion/game.kandidaat.ini` + `dropin/`, tv-recept in sectie "UNIS" |
| **RaccoonRampage** | UE4 64 (PDB aanwezig) | idem: `RSGame/Plugins/UsbPluginsDll.dll` (UNIS, 107 exports; TeknoParrot heeft hem op 8 bytes aangepast, het origineel staat ernaast als `.original`): `BIND_GetIoData` = 49 bytes met knoppen, trekkers en de ruwe aanwijspunten van vier geweren | dezelfde `rg-unis` | **kandidaat klaar (statisch bewezen, niet gestart)**: `patches/RaccoonRampage/game.kandidaat.ini` naast de huidige (die op de TP-kern blijft staan) |
| **AfterDark** | Virtools 32 | HID-bord `VID 0471 PID 485E` (HidD_Get/SetFeature uit een dynamisch geladen hid.dll) en een tweede apparaat `ITOKEN2` (QueryDosDevice + CreateFile + DeviceIoControl `0x22000C`/`0x220010`) | hid.dll-drop-in die SetupDi/CreateFile in de IAT van GamePlayer.exe omleidt, plus een ITOKEN2-nabootsing | **geblokkeerd**: twee protocollen, geen van beide uit de aanroepen gelezen (zie "After Dark" hieronder) |
| **LethalEnforcers3** | eigen 32 (Konami) | Konami `iob.dll` (44 exports) + `gunio.dll` (18), beide over een seriele poort | eigen `iob.dll` + `gunio.dll` (`tools/rg-konami/`) + een eigen `SAVE/io.sav` (kalibratie bij onze ruwe waarden); Japanse locale via `[Env]` | **kandidaat, opnieuw te testen** (proef 8: zwart zonder gamescope; nu gamescope + laadregel): zie `docs/research/konami/README.md` |
| **Gashaaaan** | eigen 32 (Konami) | Konami `bin/iob.dll` (F24-familie, 30 exports) + `bin/ballio.dll` (63: bs2_*, ball_io_*) | eigen `iob.dll` + `ballio.dll` (`tools/rg-konami/`): een worp (trekker) wordt een balinslag op het richtpunt | **werkt zonder TeknoParrot tot het titelscherm** (proef 13; eigen `BALLS.INI` zonder schijfletters): zie `docs/research/konami/README.md` |
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

### Drakons: de invoerinjector is gebouwd (2026-10-18, kandidaat, niet gestart)

Wat de kern deed is nu uit het spelproces zelf gemeten: `Player.log` van de laatste run met de kern toont `**** DONGLE FOUND: 69420` (de kern beantwoordde
de originele `matrix64.dll`; nu onze eigen als drop-in in `Game_Data/Plugins/x86_64`), `PneumaticSeat controller not found` en `Could not find a LED controller`
(geen seriele poorten, ook met de kern), en `New input system (experimental) initialized` / `Using XInput`. De dump (`InputsManager`, TypeDefIndex 1724,
RVA 0x3F0100 Update) kent drie modi: `Turret` (zitbank), `Mouse` (`fireAction` + `moveAction`, Input System) en `Gamepad` (speler 2). De muis was dus wat de kern
uit het bordbeeld leverde; speler 1 doet dat nu `tools/rg-drakons/rg_rs232.c` (gebouwd met `tools/dev/build-drakons.sh`): onze `RS_232_DLL.dll` (dezelfde 21 exports als het
origineel, allemaal "geen poort") start bij het laden een draad die `TeknoParrot_JvsState` leest (WartranTroopers: bits op offset 8, `p1.x`/`p1.y` op 12/13) en met
`SetCursorPos` + `SendInput` (absoluut) de muis in het clientgebied van het `UnityWndClass`-venster zet; trekker (bit 7) is de linkerknop. Munt (toets 5) en start (toets 1)
levert het virtuele toetsenbord van RavenGun al aan het systeem; `rg-drakons.ini` heeft `toetsen = 1` als terugval (SendInput) voor het geval dat in Wine niet aankomt.
Beperking: 8 bit per as (256 stappen, ~7,5 px op 1920) en alleen speler 1; speler 2 (gamepad-modus) is niet gedaan.

### RobinHood: waar de HASP-controle in de exe zit (statisch gemeten 2026-10-19)

Op onze eigen lader en kern (`[Loader] Core = ravengun`) komt RobinHood tot het scherm
**ERROR / SECURITY CHECK BAD**; zonder dongle-inhoud is de HASP-reactie niet na te bootsen, dus de
controle in `data/game.exe` (2.671.104 bytes, NAS) moet gepatcht worden. Waar die zit:

* `SECURITY CHECK BAD` staat op bestandsoffset `0x21acac`, sectie `.rdata`, VA `0x61c4ac`.
* Die VA wordt **niet** direct in code gepusht maar staat in een berichtentabel in `.data`
  (o.a. VA `0x68cfe0`, `0x68cfec`, `0x68d088`, `0x68d094`; elk item begint met de stringpointer
  gevolgd door nog een pointer, bijvoorbeeld `ac c4 61 00` -> `57 73 ...`).
* Geen `hasp_login`/`hasp_decrypt`-tekst in de exe; de HASP-API wordt dus niet op naam gezocht
  (de XML-teksten `haspformat`/`haspscope` staan er wel, op `0x22fba4` en verder).

Volgende stap voor een echte patch: de gebruiker van die berichtentabel vinden (kruisverwijzingen
op de tabel-VA's in `.text`, dan terug omhoog naar de tak die de HASP-uitkomst test), en de
terugkeerwaarde van die tak op "goed" dwingen. Hulpmiddel: `/tmp/pe-stringref.py` (PE-secties,
string-VA en de verwijzingen ernaar; read-only). Dat is een sessie op zich, geen kwestie van een
regel, en zonder dongle-dump blijft het de enige route.

## Vervolgmeting 2026-10-19: de beveiligingscontrole in de exe, en onze eigen geheugen-motor

Het vorige stuk eindigde met "zoek de gebruikers van de berichtentabel en dwing de tak naar goed".
Dat is nu gebeurd, en de motor die dat moet doen is er ook: `memory/src` (`ravengun-patchmem32.dll`,
docs/memory-engine.md) als drop-in van deze game. Wat er precies in de exe staat:

**1. De controle zelf.** `game.exe` (image base 0x400000, 32-bits) heeft de Aladdin-HASP-bibliotheek
statisch in de exe; de apparatuur wordt via `\\.\FEnteDev` en `\\.\HARDLOCK.VXD` geopend
(0x58fe6b e.v., `CreateFileA` + `DeviceIoControl` met 0x9c40244c). De controlefunctie begint op
**0x4913c0**; hij rekent een CRC uit over het antwoord van de dongle en vergelijkt:

```
0x491487  not  %esi
0x49148c  cmp  0x10(%esp),%esi
0x491496  jne  0x4914b1        <- komt niet uit: foutpad (vlag 0x1a6be80 = 3)
0x491498  movsbl 0x21(%esp),%edx ; sub $0x41,%edx ; mov %edx,0x68411c   <- goedpad
```

**2. De uitslag.** De vlag staat op **0x1a6be80**: 0 = goed, 2 = onbekende HASP-uitslag
(0x491521), 3 = CRC klopt niet (0x4914b1), 4 = verkeerd gebied (0x49150d/0x49154e). De
gebiedsstring staat op **0xde7098** en moet met "GW" beginnen (0x491547 vergelijkt met 0x47, 0x49155a
met 0x57); die buffer wordt gevuld door de aanroep 0x57b350, en de status daarvoor komt uit 0x57a9e0
(het spel kijkt of die in 0..0x1f valt). De **enige lezer** van de vlag buiten de controle is
**0x40129b**:

```
0x40129b  mov  0x1a6be80,%eax
0x4012a0  cmp  %ebx,%eax
0x4012a2  je   0x4012b3        <- 0: geen melding
0x4012a4  push %eax ; push $1 ; push $0xc ; call 0x490dc0   <- anders: ERROR-scherm
```

**3. Het scherm.** "ERROR / SECURITY CHECK BAD" is geen MessageBox maar door het spel zelf getekend;
de teksten komen uit een tabel in .data (0x68cfe0: "SECURITY CHECK BAD", 0x68cfec: + "WRONG AREA
CODE") en de tekenaar is 0x490cf0, die de code 0x1a6be74 opzoekt in de tabellen 0x684268/0x6847f8.
Die code wordt in de wikkel op 0x401098 uit 0x68411c (de gebiedscode) gezet, met negatief -> 0.

**4. Wat de geheugen-motor deed.** `patches/RobinHood/dropin/` bevat `ravengun-patchmem32.dll` +
`patchmem.txt`; dat bestand komt in de view naast de exe (de cwd van het spel is de view) en
`RAVENGUN_PATCHMEM_FILE=patchmem.txt` is genoeg omdat de motor een relatief pad tegen de cwd opent.
**Gemeten bewijs dat dit werkt** (eerste keer dat deze motor op een echte game is gebruikt): met het
spel aan is in `/proc/<pid>/mem` op `game.exe+0x91496` (VA 0x491496) `90 90` te lezen waar het
bestand `75 19` heeft, en de md5 van het bestand op de NAS is voor en na gelijk
(`6d5b7bcf5211271559ab63403d5d11f2`). Ons eigen gereedschap, geen schrijfactie op de NAS.

**5. Wat het (nog) niet oplost.** Met alleen de CRC-tak gedempt blijft het scherm staan; de vlag is
dan 2 (de statusaanroep 0x57a9e0 valt buiten 0..0x1f, of de sprongtabel kiest de fouttak). De
uitslag zelf op 0 zetten (de `movl $0x2` op 0x491526) helpt ook niet: het scherm komt terug, dus er
is nog een tweede weg naar de melding. De tak bij 0x4012a2 overslaan haalt de melding weg, maar dan
stopt het spel zelf (gemeten: na ~20 s weg, geen beeld). De volgende stappen zijn dus:
(a) nagaan wie 0xde7008 (de "toon"-vlag van 0x490cf0) zet en welke andere plekken 0x490dc0 met code
0xc aanroepen; (b) de statusaanroep 0x57a9e0 volgen tot wat hij leest (dan is de uitslag misschien
echt te maken in plaats van te dempen); (c) de gebiedsstring: de twee vergelijkingen op 0x491558 en
0x491561 zijn de laatste stap van het goedpad, en met "GW" in 0xde7098 loopt de controle vanzelf
goed af.

Het profiel staat op `Status = parked` met deze meting erbij; het bestand op de NAS is niet
aangeraakt en de dongle-antwoorden hebben we nog steeds niet.
## 2026-10-19 02:45: de controle is om — het spel start door naar zijn eigen BOOTING-scherm

Twee dingen waren nodig:

**1. De patch moet vóór de eerste code van het spel landen.** De motor patchte in een
achtergrondthread (`entry_win.c`, `CreateThread` in `DllMain`). Gemeten: de bytes stonden goed in het
geheugen (`game.exe+0x91496` = `90 90`), maar de vlag van het spel stond al op 2 — de controle was al
geweest. Daarom doet `DllMain` nu **eerst een synchrone ronde op de hoofdmodule**
(`GetModuleHandle(NULL)`; géén `CreateToolhelp32Snapshot`, want die wacht in DllMain op de
loader-lock) en pas daarna de thread voor alle andere modules.

**2. De uitslag moest 0 worden.** De functie zet in elke foutafhandeling dezelfde staart
(0x491517 → 0x491522: `movl $0x2,0x1a6be80`), dus ook met de CRC-tak gedempt bleef de vlag 2. De
gebiedslees (0x57b350) lukt zonder dongle niet → `test %eax,%eax` → foutpad → die staart. Met
0x9150b (`je` → `jmp`, altijd naar de gebiedscontrole) en 0x9159c (`jge` → `jmp`, zodat
`mov %ebx,0x1a6be80` wordt overgeslagen) loopt de functie naar 0x491587 (`movl $0,0x1a6be80`) en geeft
1 terug.

De volledige lijst staat in `patches/RobinHood/dropin/patchmem.txt` (8 regels). **Valkuil van die
avond:** de RVA is het adres *min* de image base 0x400000, dus 0x4913e6 is RVA **0x913e6**; met
0x13e6 klopt de `verwacht`-controle niet en past de motor die regel niet toe (en stopt hij bij de
eerste regel die niet klopt).

**Resultaat** (gemeten; NAS ongemoeid, md5 `6d5b7bcf5211271559ab63403d5d11f2` voor en na): het spel
gaat voorbij `ERROR / SECURITY CHECK BAD` en staat daarna in zijn eigen scherm
`BOOTING . / I/O ... CHECKING`. In het proces: vlag 0x1a6be80 = 0, code 0x1a6be74 = 0, gebied
0x68411c = 0 (gelezen uit `/proc/<pid>/mem`).

**Nieuwe blokkade: het I/O-bord.** Daar blijft het spel staan (13+ minuten, geen time-out, geen
foutmelding). De exe zoekt zijn bord via de Windows-apparatenlijst:
`SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
`SetupDiGetDeviceRegistryPropertyA`, met de apparaatnamen `\\.\USBPDCOM` en (voor de dongle)
`\\.\FEnteDev` / `\\.\HARDLOCK.VXD`. Onze eigen kern vult die opsomming niet, dus het spel vindt geen
bord. **Volgende stap:** de apparaatinterface-GUID uit de exe halen en met een drop-in (zoals
rg-wajvio/rg-mfcam) een bord aanbieden dat op de `DeviceIoControl`-verzoeken antwoordt — of de
opsomming zó beantwoorden dat het spel denkt dat het bord er is. Met de gesloten kern was dit spel
speelbaar, dus die kern deed hier iets.

### I/O-bord: eerste proef (2026-10-19 03:00) en twee vondsten

Voor het I/O-bord is `tools/rg-rhio/rg_rhio.cpp` geschreven: een 32-bits `setupapi.dll` die de
SetupDi-functies exporteert die de exe met `LoadLibraryA` + `GetProcAddress` opzoekt, plus haken op
`kernel32!GetProcAddress`, `CreateFileA` en `DeviceIoControl`, met een logboek (`rg-rhio.log`). Dat
is dezelfde vorm als `tools/rg-winusb` (TC5) en `tools/rg-usio`.

**Geparkeerd, met de reden.** Twee dingen zijn gemeten en nog niet opgelost:
1. Een drop-in met `DllOverride = native` zet `setupapi=n` voor het HELE proces, en dan start het
   spel niet meer (de lader/game sluit direct, exitcode 0, geen rg-rhio.log). De override is er
   daarom weer uit; Wine zoekt bij `LoadLibraryA("SETUPAPI.DLL")` eerst in de map van de exe, dus
   de DLL wordt ook zonder override gevonden.
2. Zonder override start het spel wel, maar rg-rhio.log bleef leeg: de game laadt de DLL in dat
   geval niet (of de opzoeking gaat ergens anders heen). Dat is het eerste dat uitgezocht moet
   worden; daarna kan het bord zelf (de IOCTL-antwoorden) aan bod komen.

**Productvondst onderweg:** een drop-in die je aan een profiel toevoegt, komt pas in de view als de
daemon opnieuw is gestart (`systemctl --user restart ravengund`). Zonder herstart startte het spel
wel, maar stond het nieuwe bestand niet in de view en kwam de override er niet. Dat hoort niet: de
daemon leest de profielen per start.

### I/O-bord: het bord is een SERIELE kaart, en de opdrachten staan in het logboek (2026-10-19 03:10)

De vorige proef (een eigen `setupapi.dll`) is verlaten: Wine's eigen `services.exe` heeft de
ingebouwde setupapi nodig, dus `setupapi=n` laat het spel niet meer starten (gemeten met een klein
proefprogramma: `LoadLibraryA("setupapi.DLL")` geeft zonder override Wine's builtin terug, en mét
override start services.exe niet meer). De SetupDi-opsomming is ook niet de weg die het spel in deze
toestand gebruikt.

**Wat wel werkt:** de geheugen-motor laadt nu een extra haak-DLL in het spelproces
(`RAVENGUN_PATCHMEM_HOOK=rg-rhio.dll`, zie `memory/src/entry_win.c`). Dat mag daar wel
(`LoadLibrary` in de draad, niet in DllMain) en zo zitten onze haken in het spel zonder dat er iets
van Wine vervangen wordt. `tools/rg-rhio/rg_rhio.cpp` zet daarmee `kernel32!GetProcAddress`,
`CreateFileA`, `DeviceIoControl`, `ReadFile`, `WriteFile`, `PurgeComm`, `ClearCommError`,
`Get/SetCommState`, `Get/GetCommTimeouts` en `EscapeCommFunction` om en schrijft alles in
`rg-rhio.log` naast de exe.

**De vondst:** de exe probeert COM1, COM2, COM3 (fout 5) en **COM4 lukte** -- het bord hangt aan een
seriele poort. Daarna stuurt het spel onafgebroken 7-byte kaders en wacht op een antwoord:

```
00 d7 8f 94 22 00 1c
00 1d 90 94 22 00 63
00 63 90 94 22 00 a9
...
```

* byte 0 = 0x00 (opdracht), byte 3 = 0x94, bytes 4-5 = 0x22 0x00 (vast);
* bytes 1-2 zijn een teller die per ronde met 0x46 (70) oploopt;
* byte 6 is de **som van de eerste zes bytes, mod 256** (0xd7+0x8f+0x94+0x22 = 0x21c -> 0x1c) --
  dat is de controlesom, en die is dus na te rekenen;
* het spel blijft dit herhalen: het wacht op een antwoordkader van het bord en komt daarom niet
  verder dan `I/O ... CHECKING`.

**Volgende stap (precies):** het antwoordkader van het bord namaken. Het meest waarschijnlijke
formaat is hetzelfde kader met de schakelaarstand erin (munt/START/geweer), dus: de teller
terugsturen met de IO-bytes uit het bordbeeld van de daemon (`tp-shm`, indeling WartranTroopers) en
dezelfde controlesom. Dat antwoord hoort in `rg_ReadFile` in `tools/rg-rhio/rg_rhio.cpp`; de eerste
proef kan zijn: het ontvangen kader één op één terugsturen (bord aanwezig, geen schakelaars) en
kijken of het spel verder komt.

**Productvondst (nog een keer, nu hard):** een gewijzigde drop-in-**DLL** komt pas in de view als de
daemon opnieuw start. De engine meldde "is al actueel" terwijl het bestand in de view ouder was; pas
na `systemctl --user restart ravengund` werd de nieuwe DLL gebruikt. Tekstbestanden (patchmem.txt)
werden wel meteen bijgewerkt.

### I/O-bord, tweede meting: het spel LEEST de poort nooit (2026-10-19 03:15)

Met de haken van `tools/rg-rhio` in het spel (via `RAVENGUN_PATCHMEM_HOOK`) is het bordverkeer
compleet te zien. Uitkomst van een run van 60 seconden:

* `CreateFileA(COM1..COM3)` -> fout 5, `CreateFileA(COM4)` -> gelukt (nagebootst bordhandvat);
* 288 x `WriteFile(bord, 7 bytes)` met het kader hierboven -- en **geen enkele `ReadFile`**, geen
  `DeviceIoControl` op het bordhandvat, geen `SetCommMask`/`WaitCommEvent`/`GetOverlappedResult`
  (die drie staan niet eens in de importtabel van de exe);
* het spel blijft dus schrijven en wachten: de I/O-controle komt nooit verder dan
  `I/O ... CHECKING`.

**Conclusie:** het antwoord van het bord komt niet van de seriele poort. Het spel stuurt alleen
kaders *naar* het bord (een hartslag/opdracht) en haalt de bordtoestand ergens anders vandaan --
bij deze familie is dat het gedeelde geheugen dat de kern/daemon vult (`tp-shm`, indeling
WartranTroopers), precies zoals bij Time Crisis 5 het bordbeeld in `TeknoParrot_JvsState` staat. De
volgende stap is dus niet het antwoordkader maar de **bordtoestand in het gedeelde geheugen**: welke
naam, welke indeling en of onze daemon die voor RobinHood daadwerkelijk vult (nu waarschijnlijk
niet, want de I/O-controle blijft staan).

Het volledige logboek van deze run: `bench-captures/RobinHood/20261019-bordprotocol.log`.

## Coopers 9: eerste start op onze eigen kern (2026-10-19 03:05-03:25)

Tot nu toe was dit profiel `untested` en het kon ook niet starten: zonder `[Loader]`-regel kiest het
profiel de gesloten TeknoParrot-runtime en die is er niet (`rg-direct --plan` zegt dan letterlijk
"kern ontbreekt: .../TeknoParrotUi-2.0.0.130-linux-x64/Tekno..."). Er staat nu
`[Loader] Build = ravengun` + `Core = ravengun` in het profiel, dus het spel start in elk geval op.

**Wat er gemeten is:**

* **Met onze eigen kern** (`Core = ravengun`): de lader en de kern laden ("Loader: .../OpenParrotLoader.exe",
  "Core: .../OpenParrotWin32/OpenParrot.dll", "Loading core..."), en dan valt het spel om:
  `Unhandled exception: page fault on read access to 0x4fd8aed0 in wow64 32-bit code (0x4fd8aed0)`.
  Het adres zit in **geen enkele module** ("no code accessible"). De stapel is bijna leeg: het
  enige terugkeeradres is `0x4010ed`, dus de aanroep kwam uit de vroege start van de exe zelf:
  `0x4010d0: sub $0x228,%esp; push $0x20; ...; call 0x45c0b0` -> en binnen die aanroep ging het mis.
  `EDI = 0x7a9f0bd3` wijst in OpenParrot.dll, dus de kern is er wel bij betrokken.
* **Zonder kern** (`Core = none`, "Wine start de exe zelf"): het spel start en sluit na ongeveer
  drie seconden met exitcode 0, zonder boodschap en zonder venster (foto: alleen het bureaublad).
  Dat is dus geen oplossing maar wel een aanwijzing: het spel valt niet om aan Wine alleen.

**Over de beveiliging:** de exe is 41 MB (image 0x400000-0x2b2a000) en bevat Aladdin HASP-teksten
(`classic_check_local_port: invalid HASP ID`, `<hasp4/>`, `nethasptype`, `<haspid>%u</haspid>`), maar
**niet** de teksten van Robin Hood ("SECURITY CHECK BAD", "GWRH"). Het is dus een andere (nieuwere)
laag dan bij Robin Hood, en dezelfde negen patches zijn hier niet van toepassing.

**Volgende stap (precies):** uitzoeken welke functie op `0x4010e8` hoort te staan en waarom die
garbage is -- bijvoorbeeld met `WINEDEBUG=+seh` en een breekpunt op `0x4010e8`/`0x45c0b0`, of door
`0x45c0b0` helemaal te ontleden (het begin is nog niet bekeken). Let ook op de tekst op VA 0x512424
die het foutpad van diezelfde functie gebruikt: `load.exe /f /d /q /l z` -- mogelijk hoort het spel
via een eigen lader gestart te worden in plaats van `game.exe` direct.

### I/O-bord, derde meting: het spel LEEST wel, en het bord voedt de beveiligingscontrole (2026-10-19 05:35)

**Correctie op de tweede meting.** "Het spel leest de poort nooit" was een artefact van onze eigen
nagemaakte poort: `ClearCommError` gaf een lege `COMSTAT` terug (cbInQue = 0) en een klassieke
seriele lus leest alleen als er iets in de wachtrij staat. Sinds `rg_ClearCommError` cbInQue meldt
(en `rg_ReadFile` het antwoord opruimt) is de echte volgorde te zien:

```
WriteFile(bord, 7 bytes): 23 17 b1 cb 22 00 d8
ClearCommError(bord) -> cbInQue=7
ReadFile(bord, 1 bytes gevraagd) -> 1 bytes antwoord
ReadFile(bord, 6 bytes gevraagd) -> 6 bytes antwoord      <- samen een kader van 7 bytes
ClearCommError(bord) -> cbInQue=0
```

Het spel leest dus **1 byte en dan 6** (eerst de kop, dan de rest) en vraagt in 30 seconden 462 keer.
Ook gezien: het spel zet de poort zelf op **9600 baud, 8 bits, geen pariteit** (`SetCommState(bord,
9600 baud, 8 bits, pariteit 0)`); de 38400 8O1 in onze `GetCommState` is een aanname en niet wat het
spel instelt.

**Het bord voedt de beveiligingscontrole.** Met een andere antwoordinhoud verandert het scherm:

| antwoord (`RG_RHIO_ANTWOORD`) | scherm na ~25 s |
| --- | --- |
| `echo` (het kader terug, standaard) | `BOOTING . / I/O ... CHECKING` |
| `nul` (alle bytes 0) | `ERROR / SECURITY CHECK BAD` |
| `teller` (de teller in bytes 1-2 eentje hoger) | `ERROR / SECURITY CHECK BAD` |
| `ack` (eerste byte 0) | `BOOTING . / I/O ... CHECKING` |

De dongle-antwoorden komen dus (deels) **via het bord**: met lege antwoorden valt het spel terug op
de beveiligingsfout, ook met onze negen geheugenpatches. Dat verklaart ook waarom de tweede meting op
een dood spoor leek: het bord is geen hartslag maar de weg naar de dongle.

**De "COM1..COM3 gaf fout 5"-regels** in het logboek komen uit een ouder deel van hetzelfde
(bijgevoegde) logboek, van vóór het nagebootste COM-handvat: in het huidige deel staat overal
`CreateFileA(COM1, ...) -> nagebootst bordhandvat`.

**Wat er in `tools/rg-rhio/rg_rhio.cpp` gerepareerd is** (vijf punten van de leiding): ClearCommError
meldt cbInQue en logt; de originelen van de KERNEL32-functies worden vóór het omzetten vastgelegd en
de haken worden **synchroon in DllMain** gezet (een spel-draad mag ons niet voor zijn; `LoadLibrary`
blijft erbuiten); `rg_GetProcAddress` vangt ordinalen af (alles onder 0x10000) voordat hij `strcmp`
aanroept; `LogOpen` probeert het maar een keer (atoomvlag) en `RG_RHIO_STIL` werkt nu echt;
`GetCommState`/`GetCommTimeouts` controleren hun uitvoerpointer. Bouwen kan met
`tools/dev/build-rhio.sh` of `cmake --build build --target rg-rhio` (dat doel staat niet in ALL,
want het vraagt podman).

**Volgende stap:** de *inhoud* van het antwoord. Een echo laat het spel door de beveiligingscontrole
komen, maar de I/O-controle wil meer: waarschijnlijk een toestandskader (schakelaars, bord-id). Zoek
in de exe de lezer van het antwoord (de aanroepen van `ReadFile` zitten in een hulpfunctie; de
IAT-slot van ReadFile is VA 0x1ec0c8) en kijk welke bytes hij vergelijkt.

**Geprobeerd en geparkeerd (2026-10-19 05:40):** dezelfde haken op `CreateFileMappingA`,
`OpenFileMappingA` en `MapViewOfFile` -- om te zien welk gedeeld geheugen de I/O-draad van het spel
leest -- lieten de DLL niet meer laden: het spel startte, maar `rg-rhio.log` bleef leeg (dus geen
DllMain, dus geen haken). Waarom is niet uitgezocht (tijdbox); de bron is teruggezet naar de versie
zonder die drie haken en die werkt weer (gemeten: 109 bordopdrachten in 22 seconden, ClearCommError
omgezet). De vraag welk gedeeld geheugen het bordbeeld levert staat dus nog open, maar met de derde
meting is duidelijk dat het bord zelf de weg naar de dongle is.

### Coopers 9: tweede ronde -- de load.exe-weg is dood, de kern-kant blijft open (2026-10-19 05:55)

Twee dingen uit de opdracht nagekeken:

* **De `load.exe`-weg.** De tekst op VA 0x512424 is `load.exe /f /d /q /l z` en hoort bij het foutpad
  van dezelfde functie die omvalt (`0x4010f9`). Maar de exe importeert **geen** `CreateProcessA`,
  `CreateProcessW`, `WinExec` of `ShellExecuteA` (nul treffers in de importtabel), en er staat ook
  geen `load.exe` in de gamemap. Het spel start dus zelf geen lader: die tekst is geen route.
* **Onze kern met haken uit.** Dat kan niet met wat er in de repo staat: de bron van de kern wordt in
  een container uit de open OpenParrot-bron gebouwd (`third_party/openparrot/` bevat alleen de
  patches en de bouwscripts), en er is geen schakelaar om de haken van de kern stuk voor stuk uit te
  zetten. Die suggestie vraagt dus eerst de bron van de kern in huis, of een debugger-run.

Wat er wel ligt: met `Core = ravengun` laadt alles en valt het spel om in zijn eigen vroege start
(`0x4010d0` -> `call 0x45c0b0` -> page fault op `0x4fd8aed0`, een adres in geen enkele module, met
`EDI` in `OpenParrot.dll`); met `Core = none` start het spel onder Wine en sluit het na ongeveer drie
seconden met exitcode 0, zonder boodschap en zonder venster. Het verschil zit dus in wat de kern in
het proces doet, niet in Wine. Volgende stap: de bron van de kern in huis halen en de haken een voor
een uitzetten, of een debugger op het spel zetten (dan is de aanroep die naar 0x4fd8aed0 springt
direct te zien).

### Het antwoordkader van het bord: de lezer gevonden (2026-10-19 06:55)

De doorbraak kwam van een klein trucje: de haken van `rg_rhio` loggen nu ook **wie er belt**
(`__builtin_return_address(0)`). Daarmee zijn de aanroepen van het spel te vinden zonder de IAT te
hoeven volgen:

```
0x49025b ClearCommError (wachtlus)      0x490324 ClearCommError (peiling)
0x490359 ReadFile                       0x49077c ClearCommError (na de lezing)
```

De bordhulpfunctie van het spel staat dus op **0x4902a0-0x4907xx**. De lezer van het antwoord, na de
`ReadFile` op 0x490357:

```
0x490357  call *%ebp                 ; ReadFile(bord, buf, 1, &gelezen, NULL)   <- eerst 1 byte
0x490359  test %eax,%eax / je ...    ; leesfout -> foutpad
0x490361  cmpl $0x0,0x14(%esp)       ; gelezen == 0 -> foutpad
0x490368  mov 0x30(%esp),%al         ; al = HET EERSTE ANTWOORDBYTE
0x49036f  test %al,%al          -> 0x49049c
0x490377  cmp $0xff,%al         -> 0x49049c
0x49037f  cmp $0x94,%al         -> 0x49049c
0x490387  cmp $0x95,%al         -> 0x49049c
0x49038f  cmp $0x96,%al         -> 0x49049c
0x490397  cmp $0x97,%al         -> 0x49049c
0x49039f  cmp $0x98,%al         -> 0x49049c
0x4903a7  cmp $0x23,%al         -> 0x49049c
0x4903af  cmp $0x24,%al         -> 0x49049c
0x4903b7  cmp $0x29,%al         -> 0x49049c
0x4903bf  cmp $0x30,%al         -> 0x49049c
                                    (anders: het foutpad, en de peiling begint opnieuw)
0x49045d  mov 0x30(%esp),%al         ; de som van de eerste zes bytes ...
0x490463  add 0x34(%esp),%cl         ;   + byte 4
0x490467  add 0x33(%esp),%cl         ;   + byte 3
0x49046b  add %dh,%cl                ;   + byte 2
0x49046d  add %dl,%cl                ;   + byte 1
0x49046f  add %al,%cl                ;   + byte 0
0x490471  cmp 0x36(%esp),%cl         ; ... moet gelijk zijn aan byte 6
0x490475  je 0x49048b                ; klopt: de waarde wordt bewaard
```

Dus: **byte 0 moet een van {0x00, 0xFF, 0x94, 0x95, 0x96, 0x97, 0x98, 0x23, 0x24, 0x29, 0x30}
zijn, en byte 6 is de som van de eerste zes bytes** -- precies dezelfde kaderregel als de
opdrachtkaders van het spel zelf.

**Wat er misging in onze echo (twee fouten, allebei gerepareerd):** de echo gaf het opdrachtkader
terug, dus byte 0 was de *opdrachtcode* (bv. 0x25) en die staat **niet** in de lijst -- meteen het
foutpad. En de tweede lezing gaf byte 0..5 in plaats van byte 1..6, waardoor de som nooit kon
kloppen. `rg_rhio` bouwt het antwoord nu bij de opdracht op (kies `RG_RHIO_EERSTEBYTE`, anders 0x94
als de opdrachtcode geen geldige antwoordcode is) en rekent byte 6 uit; de lezingen komen uit een
leeswijzer, zodat 1+6 samen precies het kader vormen. Elk frame-paar staat in het logboek:

```
frame-paar: opdracht 00 e4 b3 f5 22 00 ae -> antwoord 00 e4 b3 f5 22 00 ae
frame-paar: opdracht 23 ee b3 f5 22 00 db -> antwoord 23 ee b3 f5 22 00 db
frame-paar: opdracht 25 f3 b3 f5 22 00 e2 -> antwoord 94 f3 b3 f5 22 00 51
```

**Protocoltabel tot nu toe** (158 kaders in 26 s):

| opdracht | aantal | kader | antwoord van ons |
| --- | --- | --- | --- |
| 0x00 | 3 | `00 e4 b3 f5 22 00 ae` | echo (geldig) |
| 0x23 | 2 | `23 ee b3 f5 22 00 db` | echo (geldig) |
| 0x25 | 153 | `25 f3 b3 f5 22 00 e2` | `94 f3 b3 f5 22 00 51` |

Byte 1 is een teller (loopt op), bytes 4-5 zijn constant `22 00`, bytes 2-3 veranderen per sessie.
Het spel stuurt 0x00 en 0x23 een keer (bij het opstarten) en **peilt daarna 153 keer opdracht 0x25**
-- dat is de I/O-controle. Alle tien geldige antwoordcodes zijn geprobeerd (`RG_RHIO_EERSTEBYTE=95`,
`96`, `97`, `98`, `00`, `ff`, `23`, `24`, `29`, `30`): het beeld blijft `BOOTING . / I/O ...
CHECKING`. De code is dus niet de laatste hobbel; **de lading (bytes 1-5) moet de bordtoestand
bevatten** die het spel verwacht.

**Volgende stap:** de aanroeper van de hulpfunctie op 0x4902a0 nakijken -- wat doet hij met de
bewaarde waarde (0x49048b bewaart bytes 1-4 en de code) en waarmee vergelijkt hij die? Daaruit volgt
welke bytes van het antwoord de bordtoestand (schakelaars, bord-id, dongle-antwoord) moeten zijn.
Voor de andere Wartran-spellen (Coopers 9!) is dit protocol vermoedelijk hetzelfde: dezelfde
7-byte kaders, dezelfde somregel en dezelfde lijst geldige codes -- dat is de eerste zaak om daar te
proberen.

### Het antwoord draagt nu de bordtoestand van onze eigen daemon (2026-10-19 05:30)

In plaats van een vast echokader vult `rg_rhio` de bytes 1-5 van het antwoord nu uit **ons eigen
gedeelde geheugen**: de mapping `TeknoParrot_JvsState` die de daemon al schrijft (indeling
WartranTroopers uit `packaging/tp-shm-profiles.json` -- besturing als 32-bits bitveld op offset 8,
richtbytes op 12..19). Byte 1 is de besturingsbyte (munt/start/trekker), byte 2 en 3 zijn de richtas
van speler 1 (X en Y), 4 en 5 blijven 0; byte 0 blijft de code en byte 6 de som van de eerste zes
bytes. Gemeten in het logboek:

```
frame-paar: opdracht 00 b8 66 1b 23 00 5c -> antwoord 00 00 80 80 00 00 00
frame-paar: opdracht 25 c7 66 1b 23 00 90 -> antwoord 94 00 80 80 00 00 94
```

`bordbeeld: mapping TeknoParrot_JvsState gekoppeld` staat er meteen bij, en `80 80` is precies het
midden van het scherm zoals de daemon dat schrijft. De somregel klopt (0x94+0x00+0x80+0x80+0x00+0x00
= 0x194 -> 0x94). Met `RG_RHIO_BORD=0` valt hij terug op de echo.

**Het spel blijft nog op `BOOTING . / I/O ... CHECKING` staan.** Belangrijk om te weten: de lezer op
0x490357-0x4904a0 vergelijkt *alleen* byte 0 (de lijst geldige codes) en byte 6 (de som) -- verder
niets. Onze kaders worden dus geaccepteerd; de I/O-controle hangt aan iets anders. Twee sporen voor de
volgende ronde: (a) de aanroeper van de hulpfunctie volgen (het echte functiebegin ligt vóór 0x4902a0;
een `call` naar 0x4902a0 is er niet, dus de proloog staat eerder in het blok) en kijken wat hij met de
bewaarde bytes 1-4 doet; (b) de daemon een munt/start laten aanbieden terwijl het spel op de controle
staat -- misschien wil de controle een toestandsverandering zien in plaats van een vaste waarde.

### Wil de I/O-controle invoer? Nee (gemeten 2026-10-19 05:52)

De controle kan aan een *verandering* hangen in plaats van aan een vaste waarde, dus `rg_rhio` heeft
nu proefstanden (`RG_RHIO_TOETS`) die de besturingsbyte en de richtas van speler 1 zelf zetten
(bits van de WartranTroopers-indeling: munt 1 = bit 2 = 0x04, start van speler 1 = bit 6 = 0x40,
trekker = bit 7 = 0x80; assen op offset 12/13). Vier standen, elk een run van ~30 s met een foto:

| stand | wat het antwoord wordt | foto |
| --- | --- | --- |
| `cyclus` | munt 1 s, start 1 s, trekker 1 s, niets, en rond | 23581 B |
| `nul` | besturingsbyte 0x00 | 23581 B |
| `vol` | besturingsbyte 0xFF | 23408 B |
| `buiten` | richtas 0x20/0xE0 in plaats van het midden | 23205 B |

Elk antwoord heeft de juiste kaderregel (bijvoorbeeld `94 40 80 80 00 00 d4`: 0x94+0x40+0x80+0x80 =
0x1d4 -> 0xd4), en **alle vier de foto's tonen hetzelfde `BOOTING . / I/O ... CHECKING`** (het
scherm is 23 kB; zwart is 10 kB, de beveiligingsfout 17 kB, een spelbeeld is veel groter). De
I/O-controle wil dus **geen invoer en geen toestandsverandering** in dit antwoord. Daarmee blijven de
twee andere sporen over: de aanroeper van de hulpfunctie volgen (wat doet hij met de bewaarde bytes
1-4), en kijken of het spel misschien op een *ander* kanaal wacht (de daemon schrijft de
bordtoestand ook in het gedeelde geheugen; het spel zou die zelf kunnen lezen in plaats van via de
seriele poort).

### De aanroeper van de bordhulp: een peillus met slaap, die niet loopt (2026-10-19 06:00)

De bordhulpfunctie begint op **0x490140** (niet 0x4902a0; daar zit het midden van de code) en wordt
vanaf **0x48f976** aangeroepen. Die aanroeper is een peillus:

```
0x48f976  call 0x490140            ; bordhulp: antwoord lezen
0x48f981  cmpl $0x64,0x1a6be5c     ; teller tegen 100
0x48f988  jle  0x48f9f3            ; nog niet 100: terug
0x48f99b  mov  0x5ec0a4,%edi       ; Sleep
0x48f9a1  push $0x64 / call *%edi  ; Sleep(100)
0x48f9ab  push $0x7d0 / call *%edi ; Sleep(2000)
0x48f9c8  cmp  $0x32,%eax          ; teller tegen 50
```

Dat leek de verklaring voor "het duurt lang": honderd ronden met 2 seconden slaap is ruim drie
minuten. **Gemeten: het is het niet.** Een run van vijf minuten (foto's op 60, 120, 180, 240 en 290 s)
blijft op `BOOTING . / I/O ... CHECKING` staan (23 kB per foto), en in het levende proces staan de
tellers van die lus op **0**: `0x1a6be5c = 0`, `0x1a6bf04 = 0`, `0x1a6bf00 = 0` (en ook
`0x1a6be74`/`0x1a6be80`, de code en de vlag van de beveiligingscontrole, staan op 0). Die peillus
wordt dus **niet uitgevoerd**; het spel wacht ergens anders, terwijl het die tekst al op het scherm
heeft gezet. Volgende stap: uitzoeken welke draad het scherm tekent en waar die op wacht (de
meetdraad van rg-compat kan dat per draad laten zien, zoals bij Mario Kart).

### De meetdraad op Robin Hood: alle draden wachten (2026-10-19 06:02)

`rg-rhio` laadt nu ook de meetdraad van `rg-compat` als `RG_COMPAT_SAMPLE` gezet is (Robin Hood heeft
maar een haak-DLL). Een run van 40 s met `RG_COMPAT_SAMPLE=200` levert dit beeld per draad:

```
draad 248   177x ntdll+0xd410 (wacht)        <- de hoofddraad
draad 348   214x ntdll+0xd410 (wacht)
draad 352   214x ntdll+0xd410 (wacht)
draad 360   208x ntdll+0xe0c0 + winevulkan   <- de beelddraad
draad 364    82x ntdll+0xe0c0
draad 368    71x ntdll+0xe0c0
```

Alles wacht of tekent; **geen enkele draad is aan het peilen**. Dat past bij de meting in het levende
proces: de tellers van de peillus (0x1a6be5c, 0x1a6bf04, 0x1a6bf00) staan op 0, dus die lus loopt
niet. Het spel heeft de tekst `I/O ... CHECKING` dus al getekend en wacht daarna op iets anders -- een
gebeurtenis die niet komt. Volgende stap: de wachtende draad 248 volgen (op welk handvat wacht hij?
`ntdll+0xd410` is een wachtsyscall; met de draad-id uit de prefix kan dat verder uitgezocht worden)
en de vraag stellen wie dat handvat hoort te seinen -- vermoedelijk de I/O-draad die de kaders naar de
poort stuurt.

### Waar de hoofddraad op wacht: een globale mutex, geen gebeurtenis (2026-10-19 08:45)

Met alleen statische analyse (objdump) is de wacht van de hoofddraad te benoemen. De importtabel van
`game.exe` geeft de sleuven (VA = RVA + 0x400000):

| sleuf | functie |
| --- | --- |
| 0x5ec0bc | `WaitForSingleObject` |
| 0x5ec1f8 | `CreateMutexA` |
| 0x5ec15c | `InterlockedCompareExchange` |
| 0x5ec090 | `ReleaseMutex` |
| 0x5ec108 | `SetEvent` (staat in de importtabel maar wordt **nergens** aangeroepen) |
| 0x5ec10c | `CreateEventA` (één aanroep, 0x4a0ec1) |

Van `WaitForSingleObject` zijn er zeven aanroepen (0x490ac3, 0x4a0d5a, 0x57a3dd, 0x580fde, 0x5a53d2,
0x5b7f6b, 0x5eb305). De belangrijkste is **0x580fde**, met dit patroon eromheen:

```
580fa0: mov  eax,[0x6c9530]        ; het globale handvat
580fa7: jne  0x580fd6              ; al gemaakt -> wachten
580faa: push 0 / push 0 / push 0
580fb0: call *0x5ec1f8             ; CreateMutexA(NULL, FALSE, NULL)
580fbb: push 0x6c9530
580fc0: call *0x5ec15c             ; InterlockedCompareExchange(&handvat, mutex, 0)
580fce: call *0x5ec094             ; CloseHandle van de verliezer
580fd6: mov  eax,[0x6c9530]
580fdb: push -1                    ; INFINITE
580fde: call *0x5ec0bc             ; WaitForSingleObject(mutex, INFINITE)
```

Dat is een **lazy gemaakte wereldwijde mutex**: de hoofddraad **vergrendelt** hem met een oneindige
wacht, en de bijbehorende `ReleaseMutex` staat op **0x580ff6** (plus 0x4014e3 in het CRT-einde en
0x5a53e6). Het handvat 0x6c9530 wordt in de hele exe alleen *gelezen* (vier keer, alle in
0x580fa0-0x5810a0); het wordt door `InterlockedCompareExchange` gezet.

**Wat dat betekent:** de hoofddraad wacht niet op een gebeurtenis die iemand moet seinen -- hij wacht
op een **slot** dat een andere draad vasthoudt. `SetEvent` wordt in deze exe nooit aangeroepen, dus
"de I/O-draad moet een event seinen" is een dood spoor. De vraag wordt: **welke draad houdt dat slot
en waarom geeft hij het niet terug?** Het antwoord ligt in onze eigen bordkant: elke haak-aanroep
(`ReadFile`, `WriteFile`, `ClearCommError`, `GetCommTimeouts`, `WaitCommEvent`) moet snel
terugkeren, anders blijft de vergrendeling staan en wacht de hoofddraad voor altijd -- precies wat we
op het scherm zien (`BOOTING . / I/O ... CHECKING`).

Volgende stappen, in deze orde:
1. de aanroepers van 0x580fa0 (vergrendelen) en 0x580ff6 (ontgrendelen) nalopen: welke functie houdt
   het slot tijdens de bord-I/O?
2. in die functie kijken welke van onze haken daar tussen zit en of die ooit blijft hangen (een
   `ReadFile` die 0 teruggeeft terwijl het spel om 7 bytes vraagt is een kandidaat, en een
   `ClearCommError` die blijft liegen over `cbInQue` is er ook een);
3. dezelfde analyse op Coopers 9 (zelfde familie) om te zien of daar hetzelfde slot speelt.

### De meetdraad op Robin Hood aanzetten (voor de volgende ronde)

`rg-rhio` laadt de meetdraad van rg-compat (zie boven). Zet daarvoor in
`patches/RobinHood/dropin/dropin.ini` achter `RAVENGUN_PATCHMEM_HOOK=rg-rhio.dll`:
`;RG_COMPAT_SAMPLE=200`, en zorg dat `rg-compat.dll` in de view staat (naast `rg-rhio.dll`).
Twee valkuilen die vannacht tijd kostten:

1. **De daemon moet een controller hebben.** Zonder `ravengunctl mouse-gun --player 1 --mouse ...`
   blijft hij bij het starten wachten ("Zet de PS Move van speler 1 aan") en wordt er **geen game
   gestart**: geen drop-in in het proces, geen `rg-rhio.log`, geen omgezette schrijfmap. Een
   herstart van `ravengund` zet die instelling niet terug -- dus na elke herstart opnieuw zetten.
2. **`pgrep -f game.exe` matcht je eigen shell** (de opdrachtregel bevat de zoekterm). Gebruik
   `pgrep -f 'Z:\\'` of kijk naar de wine-processen, anders lijkt het of de game draait.

Daarna: sample lezen in `<prefix>/drive_c/users/<user>/AppData/Local/Temp/rg-compat-<pid>.txt`, de
pc's omzetten naar functies, en zoeken naar de draad die in **0x580fa0-0x5810a0** (de lock-helper)
zit of in een van onze haken blijft hangen.

## (D) meten met de meetdraad: wat de draden doen (2026-10-19)

De meetdraad van rg-compat loopt nu echt tijdens I/O CHECKING. Dat kon pas nadat twee dingen waren
opgelost:

1. **De omgeving van de drop-in komt niet door** (alleen `RG_RHIO_LOG` haalde het plan). Daarom kan
   `rg-rhio` de meetdraad nu ook aanzetten met een **markerbestand** naast de DLL: `rg-compat.aan`
   (inhoud = milliseconden, leeg = 200). rg-rhio zet dan zelf `RG_COMPAT_SAMPLE` en
   `RG_COMPAT_SAMPLE_LOG` (naar `<map van de DLL>\rg-compat-sample.txt`) voordat het rg-compat laadt.
2. **De daemon moet een controller hebben.** Zonder `ravengunctl mouse-gun --player 1 --mouse ...`
   blijft hij wachten ("Zet de PS Move van speler 1 aan") en start er geen game: geen drop-in in het
   proces, geen logboek, geen omgezette schrijfmap. Na **elke** herstart van `ravengund` opnieuw
   zetten.

### Uitkomst van de meting (150 rondes, 25 ms, I/O CHECKING)

| draad | wat hij doet |
|---|---|
| 260 (hoofd) | 134/150 in `ntdll+0xd410` = **NtWaitForSingleObject** (syscall 0x35), de rest in `NtReadFile`/`NtWriteFile` (0x12/0x07) en een enkele keer `game.exe+0x58008e` |
| 352, 356 | 150/150 `NtWaitForSingleObject`, geen aanroeper in de exe |
| 368 | 148/150 `ntdll+0xe0c0` (wineserver-aanroep 0x100) met `game.exe+0x34696d`: de presentatie/renderdraad |
| 372 | `ntdll+0xe0c0`, wisselende aanroepers: ook een wachtdraad |

**Geen enkele draad zit in de lock-helper 0x580fa0-0x5810a0.** De hoofddraad wacht dus op een object
(en de syscall is inderdaad NtWaitForSingleObject) terwijl de *houder* van dat object zelf ergens
anders op wacht. De volgende stap is de **handvatwaarde** van die wacht te lezen; dat kan op twee
manieren:

* de mutex-haken in rg_rhio (`WaitForSingleObject`/`ReleaseMutex`/`CreateMutexA`) -- die bestaan nu.
  Het vermoeden dat ze de game laten stoppen is **niet bevestigd**: in de A/B-proef bleven alle tien
  starts draaien (vijf met de haken, vijf zonder) en lazen ze het bord gelijk. Ze staan achter
  `rg-rhio-mutex.aan` en zijn standaard uit; de lange wachtdiensten worden wel apart gemeten;
* of: de argumenten van de wachtdraad uit het geheugen lezen (het handvat staat op de stapel van
  draad 260) en dan kijken wie dat handvat bezit.

De syscallnummers zijn te vinden met `objdump -d --start-address=$((0x7bc00000+RVA))` op
`wine/proton-*/lib/wine/i386-windows/ntdll.dll` (let op: de VMA, niet de RVA -- dat kostte hier een
kwartier).

### Wat niet werkte (en waarom dat nuttig is)

* **De mutex-haken in rg_rhio** (`WaitForSingleObject`/`ReleaseMutex`/`CreateMutexA`): hier stond dat ze
  de game meteen laten stoppen. Dat is **niet bevestigd** -- vijf starts met de haken en vijf zonder
  bleven alle tien draaien, met per variant dezelfde bordlezers (zie de tabel en de weggegooide starts
  hierboven). De oude conclusie kwam uit een bisect met een start per variant. Ze staan achter
  `rg-rhio-mutex.aan`/`RG_RHIO_MUTEX=1` en zijn standaard uit, omdat ze veel logboekruimte kostten, niet
  omdat ze de game stoppen.
* **De meetdraad uitbreiden met de stapelwoorden** (om het handvat van de wacht te lezen) laat de game
  ook stoppen. Die wijziging is teruggedraaid; de meetdraad is precies zoals hij in de repo staat.
* De **start is soms wisselvallig**: met dezelfde DLL's en dezelfde view start de game de ene keer wel
  (foto ~23 kB, I/O CHECKING) en de andere keer niet (de lader stopt met 0 en de game sluit meteen).
  Een keurproef hoort daarom altijd een foto te maken en niet alleen naar het logboek te kijken.

### Concreet vervolg voor de volgende ronde

1. Het handvat van de wacht van draad 260 lezen **zonder** een API-haak: het argument staat op de
   stapel van die draad. Dat kan met een *apart* meetprogramma (buiten rg-compat om), of door in
   rg_rhio op een **eigen** moment (na de eerste bordopdracht) de `Esp` van de andere draden op te
   vragen en alleen te *lezen* -- niet te haken.
2. Met dat handvat: kijken welke draad het bezit (`NtQueryMutant`-achtig, of door de
   `ReleaseMutex`-aanroepen van de game in een *tabel* bij te houden die we uit het bordprotocol
   kennen).
3. Blijkt de bezitter een van onze draden (352/356 wachten met geen enkele exe-aanroep op de stapel),
   dan is dat de kant om te repareren: die draden horen het bord te voeden en het slot daarna vrij te
   geven.

### Stand van de wachthandvat-lezer (einde (D))

`LeesWachthandvatten()` staat in `rg_rhio.cpp` en is gebouwd: het zet elke draad even stil, leest
`Eip`/`Esp` en de vier stapelwoorden, en schrijft `stack: draad <tid> pc=<module+rva> esp=... args=...`
naar het bordlogboek. Aan met `rg-rhio-stack.aan` naast de DLL of `RG_RHIO_STACK=1`. **De game blijft
er goed van draaien** (foto 23 kB, I/O CHECKING) -- dat is de winst ten opzichte van de import-haken.

Alleen: de functie wordt nog **niet bereikt**. De aanroep staat naast `LaadMeetdraad()` in
`rg_CreateFileA`, maar in het logboek staat wel `CreateFileA(COM1, ...) -> nagebootst bordhandvat
52480002` en **geen** `stack:`-regel. De volgende ronde moet dus uitzoeken in welke tak van
`rg_CreateFileA` die twee aanroepen staan (er zijn meer `CreateFileA`-paden: het bord, de pijp, de
gewone bestanden) en ze in de **bordtak** zetten -- precies waar het bordhandvat wordt gemaakt.

### Coopers 9: derde meting (2026-10-19 10:32-10:52) -- bevestigt het parkeren

Opnieuw gemeten op onze eigen lader en kern (`Mode = direct`, `Core = ravengun`):

* de kern laadt ("Loading core...", `openparrot/current/OpenParrotWin32/OpenParrot.dll`), de lader
  stopt met **exitcode 0**, `rg-direct` volgt de game;
* de game **sluit zich binnen ~8 seconden** met exitcode 0, zonder venster, zonder spelstand;
* met een procesvolging (elke 4 s) is te zien dat de processen van de view van 9 naar 3 terugvallen
  en dat de 3 die overblijven de wineserver-achtigen zijn: `game.exe` zelf is weg;
* het drop-in-logboek van die start (`direct-Coopers9-20261019-103208.log`) laat geen crashdump zien,
  en in de logregel staat `[Gamescope WSI] Executable name: explorer.exe` (de kern start eerst
  explorer.exe, zoals bij de andere OpenParrot-titels).

Dat is precies het beeld uit de notitie van 05:55: met de kern valt het spel in zijn eigen vroege start
om, zonder kern sluit het na ~3 s met exitcode 0. **De volgende stap blijft dus dezelfde** en vraagt
nieuwe infrastructuur: de bron van de kern in huis halen om de haken een voor een uit te zetten, of
een debugger op het spel zetten zodat de aanroep naar `0x4fd8aed0` direct te zien is. In deze ronde
was daarvoor geen tijd meer; (E) blijft **geparkeerd** met deze notitie als vertrekpunt.

### Coopers 9: de crash is van het spel zelf, niet van onze kern (2026-10-19 11:35)

WINEDEBUG deed wat de opdracht beloofde. `game.exe` **zonder onze kern**, rechtstreeks onder wine, met
`WINEDEBUG=-all,+seh`:

```
Unhandled page fault on read access to 0x4fd8aed0 in wow64 32-bit code (0x4fd8aed0)
 EIP:4fd8aed0 ESP:02c2fcf0 EBP:7ffd1000 EFLAGS:00010206
Backtrace:
=>0 0x4fd8aed0 (0x7ffd1000)
```

en uit het `+seh`-spoor: `code=c0000005 (EXCEPTION_ACCESS_VIOLATION) addr=4FD8AED0`, met
`eax=fc728db1 ebx=0055e3c8 ecx=0055e3c8 edx=00402480 esi=0055e3c8 edi=00000000`, `ebp=7ffd1000`,
`esp=02c2fcf0`. Het adres `0x4fd8aed0` ligt in **geen enkele module** en de stapel is rommel
(`ebp=0x7ffd1000`), dus het spel **springt door een kapotte functiewijzer**. De aanroeper is niet te
zien (geen geldige frames).

**Dit is de correctie op de notitie van 05:55**: daar stond dat het verschil tussen wel en geen kern
bepaalt of het spel omvalt. Deze meting laat zien dat de crash er **ook zonder onze kern** is -- het is
het eigen vroege startpad van het spel. Dat past bij wat er in dit document over Coopers 9 staat: het
heeft **HASP HL statisch gelinkt** (dat is uit de teksten in de exe te lezen), en **de
veronderstelling** is dat zo'n beveiligingslaag door een tabel springt die pas met een echte dongle
klopt. Dat laatste is niet bewezen: het is afgeleid uit de registers van één `+relay`-start. De volgende stap is dus niet de kern maar dat pad: met `winedbg` (of
`WINEDEBUG=+relay` op een *klein* stuk) de laatste sprong voor `0x4fd8aed0` vinden en kijken welke
tabel daar gelezen wordt -- precies de "dongle"-route die de leiding al als open item heeft.

### Coopers 9: waar de kapotte functiewijzer vandaan komt (2026-10-19 11:47)

Met `WINEDEBUG=-all,+relay` (29 MB, het spel valt na ~1 s om) is de laatste regel voor de crash:

```
Ret  PE DLL (module=kernel32.dll, reason=PROCESS_ATTACH, res=02C2FD24) retval=1
Starting thread proc 00402480 (arg=7FFD1000)
Call KERNEL32.BaseThreadInitThunk(00000000,00402480,7ffd1000) ret=7bf0e277
Call ntdll.NtQueryInformationProcess(...)
Ret  ntdll.NtQueryInformationProcess() retval=00000000
```

Dat verklaart de registers van de crash precies: `edx=00402480` is de **start van de draad** en
`ebp=7ffd1000` is het **argument** dat `BaseThreadInitThunk` kreeg. Het spel start dus heel vroeg een
eigen draad en die valt om.

De code zelf is normaal (objdump op de view-kopie):

* `0x402480` (de draadprocedure) roept `0x4023b0`, `0x401b30`, `0x401fa0` en `0x4014b0` aan -- gewone
  CRT-achtige initialisatie, geen sprong naar een vreemd adres;
* `0x401b30` roept **`0x4010d0`** aan en bevat daarna **`call *%edi`** en **`call *%ebx`** -- indirecte
  aanroepen. `0x4010d0` is precies de functie die de eerdere notitie al aanwees (`0x4010d0 -> call
  0x45c0b0 -> page fault`).

De crash-registers wijzen de tabel aan: `ebx=ecx=esi=0055e3c8` (een adres in de **beschrijfbare data**
van de exe) en de fout is een **lees**fout op `4fd8aed0` met `eip=4fd8aed0`. Met andere woorden: de
code leest een functiewijzer uit de tabel bij `0x55e3c8` en **die wijzer is `0x4fd8aed0`** -- een adres
in geen enkele module -- en springt erheen. Zonder onze kern (deze meting) is `edi=0`, dus dezelfde
route loopt ook daar stuk.

**VERONDERSTELLING**: dit is de beveiligingslaag. Coopers 9 heeft HASP HL statisch gelinkt (zie de
tabel bovenaan dit document), en zo'n laag hoort die tabel bij het opstarten te vullen; zonder dongle
zou er rommel blijven staan. Wat we **zeker** weten: de leesfout op `0x4fd8aed0` (uit een adres in geen
enkele module), `ebp=0x7ffd1000` en `edx=0x402480` uit één `+relay`-start, en dat `0x401b30` de
functie `0x4010d0` aanroept en daarna `call *%edi` / `call *%ebx` bevat. WAT DAT ZOU BEVESTIGEN: de
instructies bij `0x4010d0` en `0x401b30` helemaal uit elkaar halen en laten zien welke tabel-index
gelezen wordt, en dat dezelfde start met een gevulde tabel (echte dongle of een patch) niet omvalt --
vijf starts per variant.
De weg vooruit is dus niet de kern en niet Wine, maar dat pad: ofwel de tabel laten vullen zoals de
echte beveiliging doet (daar is een dongle voor nodig, en de eigenaar heeft gezegd dat die er niet
komt), ofwel de exe op dat punt **patchen** zodat hij de controle overslaat -- dezelfde aanpak als bij
de andere dongle-spellen in dit document.

### Coopers 9: the 10-17 start used the closed core, and the exe is a flat image dump (2026-10-21 07:30)

**The contradiction is resolved without a run.** `git show cadca34:patches/Coopers9/game.ini` (the
profile as it was until 10-19) has **no `[Loader]` section**, so on 2026-10-17 the profile selected the
closed TeknoParrot runtime; commit 7495c3a of 10-17 00:10 lists "Cooper's 9" as starting with
`Windowed = 1`. That start is therefore a TeknoParrot-core start, not ours. There is no start record for
it either: `~/.local/state/ravengun/starts/` only begins on 10-17 in the evening (HOTDSD, Police
Trainer 2). Today's plan (`build/rg-direct --plan Coopers9 --patches patches`) still names our loader and
our core, `tp_files []`, which is what the queue asks for.

**What was actually missing is not in the core.** The exe is a *flat memory image*: the byte at file
offset X belongs at RVA X (the header is 0x1000 bytes, `.text` at 0x1000, `.rdata` at 0x112000, and the
file is 0x272a000 bytes = `SizeOfImage`), but its section table still carries the raw offsets of the
**packed** exe it was dumped from. `.data` even declares `VirtualSize 0x24c3e30` against `SizeOfRawData
0x29000`, which only makes sense for a live image. With that stale table the loader reads `.protect` at
0x160000 instead of 0x25fb000 -- 0x10a000 bytes early -- and the import directory (RVA 0x27297c0, inside
`.protect`) reads as zeros. That is the whole "the import table lies in uninitialized `.data`" story: the
table is there, we were looking 0x10a000 bytes off.

**Measured** with `tools/dev/pe-flat-fix.py` (rewrites `PointerToRawData` to the RVA and
`SizeOfRawData` to the aligned `VirtualSize`, then re-parses the import directory; it never writes the
NAS):

```
section .rsrc      raw 0015f000 -> 025fa000
section .protect   raw 00160000 -> 025fb000
check: 15 descriptors, 15 dlls: KERNEL32.dll, SETUPAPI.dll, KERNEL32.dll, USER32.dll, d3d9.dll,
       DINPUT8.dll, DSOUND.dll, WINMM.dll, KERNEL32.dll, USER32.dll, GDI32.dll, ADVAPI32.dll,
       ole32.dll, OLEAUT32.dll, MSVCR71.dll
```

The hint/name table, the ILT and the IAT are all in the dump, so the names come out of the game itself
(`GetVersion`, `WaitForSingleObject`, `LocalFree`, `CloseHandle`, `DeviceIoControl`, `CreateFileA`, ...).

**Effect (gamescope headless, prefix `prefixes/Coopers9`, `WINEDEBUG=-all,+loaddll`, a copy of the fixed
exe in a throw-away map with links to `data/` and the DXVK dlls):** the old
`EXCEPTION_ACCESS_VIOLATION addr=4FD8AED0` is gone, Wine loads every DLL of the import table plus
`Z:\...\d3d9.dll` (DXVK) and `DINPUT8.dll`, and the process lives ~40 s with **no exception** and then
exits with code 0. So the "unstartable memory dump of a protected process" verdict from 10-19 was based
on the stale section table.

**Reference run on the closed core, for comparison (2026-10-21 07:26).** With `[Loader]` removed from the
*installed* profile (so the plan takes `TeknoParrot/TeknoParrot.dll` from the runtime, `tp_files` = 3) and
`[Display] GamescopeBackend = headless` in the user layer, the **unchanged NAS exe** does reach its own
screen: the headless screenshot shows **PLEASE CALIBRATE THE GUN**. So the closed core is what makes this
dump run; both later reverted (profile restored from a copy, user-layer key removed).

**What our own path lacks, measured:** the game's real import table is *not* the problem -- the thunks the
code uses are `jmp dword ptr [0x512xxx]` into a **second table at 0x512000 in `.rdata`**, and that table is
no descriptor's `FirstThunk` and is not filled by any loader. In the dump its slots hold XP addresses
(`0x7C80xxxx` kernel32, `0x77E4xxxx` user32, `0x77F4xxxx` ntdll) and `0x0313xxxx` values -- stubs of the
protection in memory at 0x3130000 that the dump does **not** contain (the dump was taken on an XP session).
`.text` has 49 such thunks over 47 distinct slots (0x512038..0x5123bc), against 55 named ILT entries in the
descriptors, so the table is the protection's own call table, not the game's IAT. The very first thing the
entry point does after its static init is `call 0x4010d0` -> `call 0x45c0b0` (`jmp [0x5123a0]`), and slot
0x5123a0 holds **0x4FD8AED0** in the dump: that is exactly the address of every crash we measure, with and
without our core and with or without the corrected section table. The closed core supplies that table; we
would have to rebuild it slot by slot from the game's own code (which call sites use which API, as the
document proposes for the other dongle games). That is the open work, and it is the reason this game stays
parked.

**Also open:** (1) how a corrected image would get into the double-click path at all (the engine has to
produce it per launch; no game bytes in the repo); (2) reading the filled table from a reference run is
blocked here by `kernel.yama.ptrace_scope=1` (the game runs as a grandchild of the shell), so the slot ->
API mapping has to come from the game's code.

### Robin Hood: wie er wacht, met de meetdraad (2026-10-19 11:41)

De volgorde-fout in `rg_rhio.cpp` (de meetdraad werd voor `LogOpen()` geladen, dus zijn regels
verdwenen) is hersteld en de meetdraad schrijft nu zijn **eigen** logboek:
`views/RobinHood/data/rg-compat-sample.txt` (de marker `rg-compat.aan` in de drop-in zet
`RG_COMPAT_SAMPLE=200`). Een meting tijdens I/O CHECKING, 675 rondes, per draad de twaalf meest
geziene plekken (`aantal pc | aanroeper 1 | aanroeper 2`):

| draad | wat hij doet |
|---|---|
| **256 (hoofd)** | **607 van 675** in `ntdll.dll+0xd410`, aangeroepen uit **`game.exe+0x9e7bc8`**, die uit **`game.exe+0x8ff9b`**; 38 rondes in `ntdll+0xd410` uit `game.exe+0x8f9b2` (die twee keer, dus een lus); 12 in `ntdll+0xd1e0` uit `game.exe+0x28005c`; verder losse rondes |
| 368 | 657 van 675 in `ntdll+0xe0c0` uit `game.exe+0x34696d`, met een paar rondes in **`winevulkan.dll`** uit `game.exe+0x589680` -- dit is de teken-draad |
| 380 | 666 van 675 in `ntdll+0xe0c0` uit `game.exe+0xa00000` |
| 352, 356, 376, 384, 388-448 | 100% in `ntdll+0xd410` of `+0xe0c0`, **zonder aanroeper** -- gewone wachtdraden |

**De vraag van de leiding is hiermee beantwoord**: er is **geen enkele draad in de sluis-helper
`0x580fa0`** (die komt in het hele logboek niet voor) en **geen enkele draad in een van onze haken**.
De hoofddraad wacht in **de eigen code van het spel** (`game.exe+0x9e7bc8`, terug naar
`game.exe+0x8ff9b`), en alle andere draden wachten in gewone ntdll-wachtdiensten. De vergrendeling
van `0x580fa0` is dus niet wat Robin Hood tegenhoudt; het spel staat in zijn eigen wachtpad op
bordgegevens, precies zoals de leiding vermoedde ("in een lus wachten op bordgegevens"). Dat is één
meting van 675 rondes; **`game.exe+0x9e7bc8` en `0x8ff9b` zijn nog niet opgezocht** (de exe van Robin
Hood staat niet rechtstreeks in de view). WAT DAT ZOU BEVESTIGEN: die twee adressen in de ontleder
opzoeken en de wachtdienst met zijn handvat benoemen, en daarna vijf starts met en vijf zonder
bordantwoord vergelijken.

**Volgende stap**: `game.exe+0x9e7bc8` en `0x8ff9b` in de ontleder opzoeken (de exe van Robin Hood
staat niet rechtstreeks in de view: `data/game.exe` is een hulpje van 71 bytes, het echte spel komt
uit de NAS-map via `sh/` en `start.bat`) en kijken welke wachtdienst daar met welk handvat wordt
aangeroepen. Dat geeft het bordobject waar de game op wacht.

### (D) De twee adressen van de meetdraad staan in geen enkele sectie (2026-10-19 12:27)

De opdracht was: zoek eerst `game.exe+0x9e7bc8` en zijn aanroeper `0x8ff9b` op. Dat kan **niet uit het
bestand**, en dat is zelf een uitkomst:

```
secties van de echte exe (NAS: Robin Hood/data/game.exe, 2.671.104 bytes):
  .text    VA 0x00401000  grootte 0x001ea35c   (eindigt op 0x005eb35c)
  .rdata   VA 0x005ec000  grootte 0x00037974
  .data    VA 0x00624000  grootte 0x00069e00   (eindigt op 0x0068de00)
  .rsrc    VA 0x01ab0000  grootte 0x000001b4
SizeOfImage = 0x016b1000
rva 0x8ff9b  in sectie: GEEN
rva 0x9e7bc8 in sectie: GEEN
```

Beide adressen liggen dus **binnen de image** (waarom de meetdraad `game.exe+...` zegt: die vergelijkt
met `SizeOfImage`, zie rg_compat.c regel 287-289) maar **buiten elke sectie**. Tussen `.data` en
`.rsrc` zit een gat van meer dan 20 MB, en juist daar zit de code waar de hoofddraad op wacht. Die
code wordt dus tijdens het starten aangevuld: de exe is verpakt/beveiligd. In het bestand valt er niets
te ontleden -- mijn eerdere regel "de hoofddraad wacht in de eigen code van het spel
(`game.exe+0x9e7bc8`)" was dus niet te controleren en had als veronderstelling opgeschreven moeten
worden.

**Wat dat zou bevestigen**: de bytes op die twee plekken uit een *lopend* proces halen en die als
32-bits code ontleden. Daarvoor is `tools/dev/rh-dump-adressen.sh` toegevoegd: dat start het spel,
zoekt de image van de exe in `/proc/<pid>/maps`, dumpt 192 bytes op elke gevraagde rva en laat objdump
er 32-bits code van maken. Pas daarna is te zeggen welke wachtdienst daar met welk handvat wordt
aangeroepen.

### De A/B-proef: OLD tegen NEW tegen NOSTRIP (2026-10-19 11:51-12:21)

Aanleiding: mijn bisect van de C-ronde had met **één** start per variant geconcludeerd dat twee
wijzigingen (geen `char bord[8]` maar een omgevingsvariabele, en `-s` bij het bouwen) Robin Hood meteen
lieten stoppen. De leiding vroeg dat te bewijzen met minstens tien starts per variant, om en om, met
een foto per start. De varianten: **OLD** = de DLL die op dat moment geinstalleerd stond
(sha1 `d00986ec`), **NEW** = de DLL van de pass-2-branch (`52193f86`), **NOSTRIP** = dezelfde bron
zonder `-s` (`299d4ac3`). Per start: de DLL naar de engine-drop-in EN de view, `ravengund` herstarten,
het muis-geweer opnieuw zetten, 90 s wachten, foto. De `.aan`-markers stonden voor alle varianten
gelijk (geen). Er zijn 15 geldige starts gedaan (5 per variant; de leiding heeft de proef daarna
gestopt omdat het beeld al duidelijk was).

| variant | start | sha1 | foto (bytes) | voorbij BOOTING | draait nog | logregels | bordlezers |
|---|---|---|---|---|---|---|---|
| OLD | 1 | d00986ec | 23751 | nee | ja | 21143 | 1142 |
| NEW | 1 | 52193f86 | 23408 | nee | ja | 2919 | 1142 |
| NOSTRIP | 1 | 299d4ac3 | 23751 | nee | ja | 2938 | 1149 |
| OLD | 2 | d00986ec | 23751 | nee | ja | 21141 | 1140 |
| NEW | 2 | 52193f86 | 23581 | nee | ja | 2928 | 1145 |
| NOSTRIP | 2 | 299d4ac3 | 23751 | nee | ja | 2928 | 1145 |
| OLD | 3 | d00986ec | 23205 | nee | ja | 21149 | 1148 |
| NEW | 3 | 52193f86 | 23205 | nee | ja | 2937 | 1148 |
| NOSTRIP | 3 | 299d4ac3 | 23408 | nee | ja | 2928 | 1145 |
| OLD | 4 | d00986ec | 23751 | nee | ja | 21146 | 1145 |
| NEW | 4 | 52193f86 | 23408 | nee | ja | 2919 | 1142 |
| OLD | 5 | d00986ec | 23408 | nee | ja | 21142 | 1141 |
| NOSTRIP | 4 | 299d4ac3 | 23751 | nee | ja | 2928 | 1145 |
| NEW | 5 | 52193f86 | 23205 | nee | ja | 2936 | 1148 |
| NOSTRIP | 5 | 299d4ac3 | 23205 | nee | ja | 2935 | 1147 |

**Uitslag binnen deze proef: NEW deed het niet slechter dan OLD -- met de beperkingen hieronder.**
In de 15 getelde starts bleef de game draaien (`draait=ja`) en het aantal bordlezers is per variant
hetzelfde (OLD 1140-1148, NEW 1142-1148, NOSTRIP 1145-1149). De bewering "NEW / geen `bord[8]` / `-s`
laat het spel stoppen" is in deze proef dus **niet bevestigd**: de oude bisect was ruis (één start per
variant, en de start is aantoonbaar wisselvallig). NEW en NOSTRIP ontlopen elkaar ook niet, dus voor
het strippen is er in deze proef evenmin een aanwijzing. Volgens de afspraak ("samenvoegen als NEW
niet slechter is dan OLD") is de branch samengevoegd -- met de aantekening dat deze proef alleen kan
laten zien wat er in de GETELDE starts gebeurde, en dat de weggooi-regel hieronder die grens
opschuift.

**Hoeveel starts er zijn WEGGEGOOID, en waarom -- dit hoort erbij.** De uitslag "alle 15 bleven
draaien" is deels waar **door de opzet**: het proefscript gooit een start weg waarin de game niet
draait (die zegt niets over de DLL) en telt hem niet mee. Wat er in werkelijkheid is weggegooid:

| variant | geteld | weggegooid | waarom |
|---|---|---|---|
| OLD | 5 | 0 | -- |
| NEW | 5 | 0 | -- |
| NOSTRIP | 5 | 2 | (1) om 12:20 een start waarin de game niet liep: het logboek was leeg, er waren 0 bordlezers en de foto was het BUREAUBLAD (1.463.551 bytes, geen spel); (2) eerder een start die samenviel met een tweede instantie van het script: die had het logboekprofiel van OLD (20.407 regels) in plaats van dat van NOSTRIP |

Er zijn dus 17 starts gedaan voor 15 getelde. Dat de weggooi-regel bestaat is op zich goed (een start
waarin de game niet loopt meet niets), maar het betekent wel dat deze proef **niet** kan aantonen dat
een variant de game *niet* laat stoppen in de gevallen die weggegooid zijn. De eerlijke uitspraak is
daarom: in de 15 GETELDE starts liep de game door, met per variant dezelfde bordlezers. Van de 17
pogingen zijn er 2 weggegooid, allebei bij NOSTRIP, en een daarvan is juist een start waarin de game
NIET liep (lege log, 0 bordlezers, een foto van het bureaublad) -- "in 15 van de 17 liep de game door"
zou dus onjuist zijn. De weggegooide starts zijn overgedaan (de tweede keer met de handmatige controle
op het logboekprofiel erbij). Een variant die het spel *soms* laat stoppen zou met deze
opzet deels onzichtbaar blijven; daarvoor is een groter aantal starts nodig, niet een strengere
weggooi-regel.

**Wat de proef NIET zegt.** `voorbij BOOTING` is in 15 van de 15 starts `nee`, **OLD inbegrepen**: geen
enkele variant komt binnen 90 s voorbij het BOOTING-scherm. Dat criterium zegt hier dus niets over de
DLL's -- het is precies het (D)-probleem (Robin Hood blijft op I/O CHECKING staan), niet iets wat een
van de drie DLL's veroorzaakt. Het onderscheid tussen de varianten zit in het logboekprofiel (OLD
~21.100 regels, NEW/NOSTRIP ~2.900: de nieuwe teller telt de ReadFile-regels mee), en dat profiel
diende meteen als controle dat de variantwissel echt werkte: elke run draagt de signatuur van zijn
eigen DLL. Twee runs zijn om die reden afgekeurd en overgedaan (een vervuilde run met het profiel van
OLD, en een run waarin de game niet liep).

De proefscripts staan in `tools/dev/ab-rhio/` (`ab2.sh`, `maak-tabel.py`, `mutex-hermeten.sh`).
