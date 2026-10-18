# Invoer zonder TeknoParrot: vier games waar munt of start niet aankwam (2026-10-18)

Stand na de speelproef van 2026-10-18 (muismodus; munt = `ravengunctl key 5`, start = `key 1` 0,3 s, schot = `output-button trigger`).
Niets hier is gestart: alles is statisch gelezen (objdump, ilspy op de Unity-assembly's) of getest met een proefprogramma in een
wegwerpprefix zonder scherm. Kandidaat-instellingen: `docs/research/invoer/patches/<Game>/game.ini`.

| Game | Oorzaak | Wijziging | Stand |
|---|---|---|---|
| FarCryParadiseLost | het virtuele toetsenbord van de daemon ('5') komt via DirectInput8 in de game en is bij Global VR TEST | `tools/rg-farcry/rg_usbio.c`: DirectInput-filter in onze `Usbioextreme.dll` | getest met proefprogramma; tv-proef nodig |
| PointBlankX | `F:`/`G:` bestaan niet in de prefix: Mono gooit DirectoryNotFoundException, `GlobalManager` start niet, `usio_GetCoin` wordt nooit aangeroepen | `[Launch] Drives = F=TeknoParrot/F; G=TeknoParrot/G` + `src/ravengun/launch/direct.cpp` maakt de map onder de view | getest (plan); tv-proef nodig |
| LuigisMansion | de game krijgt zijn invoer van een Capcom-bord aan een COM-poort; onze kern doet niets voor deze game | meetvoorstel hieronder | niet statisch vast te stellen |
| MenInBlack | `MIBCore.Update` zet `DisableInputs = true` zodra Unity een joystick ziet; start/trekker/richten zijn Unity-joystickknoppen | `plugins/bepinex/MenInBlack/MenInBlack.cs` | gebouwd; tv-proef nodig |

## 1. Far Cry: Paradise Lost — de muntknop opent het testmenu

**Bron.** Niet ons bord: het logboek `rg-usbio.log` van de run laat zien dat de DLL bij de muntdruk alleen Control `0x400` (munt) ziet en
geen bedienbit zet (`ctl a=400 b=0 c=1`); de teller loopt naar 1 en 2. Het testmenu komt dus niet uit het record. De daemon drukt voor elke
bordknop ook een toets op zijn virtuele toetsenbord (`src/ravengund/output_service.cpp:222`, de TeknoParrot-afspraak: munt = '5'), en
`Farcry_R.exe` leest naast het bord ook het toetsenbord via DirectInput8 (CryInput; importeert alleen `DINPUT8!DirectInput8Create` en
`GetAsyncKeyState` voor de muisknoppen, VK 1/2/4 op 0x7ac3f4). Bij Global VR is '5' op het toetsenbord TEST: dezelfde conventie als in
Aliens Extermination (`tools/rg-aliens/aliens_logic.h`: '1' '2' start, '5' TEST, '6' SELECT, '9' omhoog, '0' omlaag), waar hetzelfde
gebeurde. Onder de TP-kern zag de game die toets niet (de kern vervangt de invoer van het spel); zonder kern wel.

**Wijziging.** `Usbioextreme.dll` (de game laadt haar toch al uit zijn eigen map) haakt in `DllMain` de importtabel van de exe
(`DirectInput8Create`), daarna `CreateDevice` van de IDirectInput8 (vtable 3) en `GetDeviceState`/`GetDeviceData` (vtable 9/10) van elk
toetsenbordapparaat, en haalt de toetsen van de daemon eruit: DIK 1 2 5 6 0 en F2..F5 (`rg_usbio_dik_blocked` in `rg_usbio_logic.h`).
Muis, joysticks en alle andere toetsen blijven ongemoeid. `RG_USBIO_KEYS=1` (in `[Env]`) laat ze weer door. Het bord krijgt munt, start en
test gewoon uit het bordbeeld.

**Test.** `bash tests/farcry_usbio_test.sh` (eenheidstest van de filter + DLL-bouw), en `tools/rg-farcry/wine-proef.sh` (Wine, wegwerpprefix,
geen scherm): 1. Wine's echte dinput8: de haken geven alles door; 2. een nep-dinput8 (`proef_nepdinput8.c`) dat '5' 'W' '6' vasthoudt:
met `RG_USBIO_KEYS=1` zijn ze er alle drie, zonder zijn '5' en '6' weg en blijft 'W' (state en gebufferd). Zonder scherm bereikt een toets uit
SendInput DirectInput niet (gemeten), daarom het nepapparaat.
**Niet gemeten** (alleen op de tv): of de toets echt via DirectInput binnenkomt en niet via vensterberichten. Het logboek zegt het: na een
munt moet `rg-usbio.log` de regel `toets uit DirectInput onderdrukt (DIK) a=6` hebben; ontbreekt die en opent het menu toch, dan komt de
toets langs een andere weg en is de volgende stap `WINEDEBUG=+dinput,+keyboard` voor een munt. Daarnaast `dinput8: DirectInput8Create omgezet
in de importtabel a=1` (a=0: de importtabel is niet gevonden).

**Tv-recept.** Nieuwe DLL op de plaats van de oude: `tools/rg-farcry/build.sh ~/projects/ravengun-patches/patches/FarCryParadiseLost/dropin/Usbioextreme.dll`
(en `~/.local/share/ravengun/patches/FarCryParadiseLost/dropin/`), dubbelklik, 60 s wachten, `key 5`: credits in plaats van het menu.

## 2. Point Blank X — de munt komt niet aan (INSERT COIN(S) 0/3)

**Bron (gemeten in de logboeken van de run van 08:26).** `rg-usio.log` heeft alle eerste aanroepen van `BNUsioController.update` (`usio_GetSwIn`,
`gun_*`, `usio_GetCoinError`) maar **nooit `usio_GetCoin`**. De game leest de munt alleen in `GlobalManager.Update` (decompilatie met ilspy):
`coinPool = bnUsioController.getCoins()`. `output_log.txt` van de game zegt waarom die klasse niet draait:

```
DirectoryNotFoundException: Could not find a part of the path "F:\version.dat".      (BootSceneController.checkVersion)
DirectoryNotFoundException: Could not find a part of the path "F:\error_log.csv".
DirectoryNotFoundException: Could not find a part of the path "F:\tracking\2026-10-18.csv".
Rethrow as TypeInitializationException: An exception was thrown by the type initializer for Tracking   (GlobalManager.Awake)
```

De kern (patch 0004) leidt `CreateFileA/W` en `CreateDirectoryA/W` op `F:\` om naar `.\TeknoParrot\F\` (daarom werkt `nbamSavdatNewW()` en staat
`TeknoParrot/F/savedata.bin` er), maar Mono's `FileStream` controleert eerst of de map bestaat (`F:\` via een niet omgeleide aanroep) en gooit
`DirectoryNotFoundException`. De TP-kern deed dat blijkbaar wel goed (de route werkte op 2026-10-15). `GlobalManager.Awake` stopt op
`Tracking.newGame()`; `GlobalManager.Update` bleef daarna weg (geen `usio_GetCoin` in het logboek) — dat Unity het script na een uitzondering in
`Awake` niet meer bijwerkt is afgeleid, niet gemeten.

**Wijziging.** `[Launch] Drives = F=TeknoParrot/F; G=TeknoParrot/G` (`docs/research/invoer/patches/PointBlankX/game.ini`): echte schijven F: en G:
voor de duur van de run, naar dezelfde mappen als de omleiding van de kern, dus beide wegen zien dezelfde bestanden. Daarvoor moest
`src/ravengun/launch/direct.cpp` een ontbrekende map ONDER de view bij een start aanmaken (in `--plan` een melding; buiten de view of op de NAS
blijft het een weigering). Test: `tests/test_direct_launch.cpp` ("Een map ONDER de view die er nog niet is").
**Tv-recept.** `game.ini` uit `docs/research/invoer/patches/PointBlankX/` over die van de patches-repo, dubbelklik. Bewijs: in
`PBX100-2-NA-MPR0-A63_Data/output_log.txt` geen `DirectoryNotFoundException` meer; in `Plugins/rg-usio.log` `eerste aanroep: usio_GetCoin`; na
`key 5` staat INSERT COIN(S) op 1/3. Blijft `usio_GetCoin` weg terwijl de uitzonderingen weg zijn: dan zit de oorzaak in een andere scene
(`GlobalManager` staat niet in de boot-scene); meet dan met `[Wine] Debug = +file` of kijk naar `Player.log`.

## 3. Luigi's Mansion — de munt komt niet aan (CREDIT(S) 0)

**Wat statisch vaststaat.** Importtabel van `VACUUM.exe` (23 descriptors, leesbaar in `.capcom1`): `SETUPAPI` (`SetupDiGetClassDevsW`,
`SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailW`, `SetupDiDestroyDeviceInfoList`), `KERNEL32` met `CreateFileA/W`,
`GetCommState`, `SetCommState`, `SetCommTimeouts`, `GetCommProperties`, `SetupComm`, `PurgeComm`, `ClearCommError`, `DeviceIoControl`,
`GetOverlappedResult`, en één `DINPUT8!DirectInput8Create`. `game.bat` flasht `LMAIO.bin` (Capcom-firmware) naar COM1 voor de start. De game zoekt
dus een COM-poort via SetupAPI en praat er een eigen protocol over; de gesloten kern blokkeert de poort ("BLOCKING LUIGI COM PORT") en schrijft
de invoer rechtstreeks in het geheugen van het spel. Onze kern doet voor deze game niets (patches 0003/0005), het bordbeeld
(`TeknoParrot_JvsState`) wordt door niemand in het spelproces gelezen: `rg-pe-hook.dll` doet alleen de 17 `am*`-antwoorden en de ExitProcess-terugkeer.
`docs/goal-90-native.md` noemt RVA `0x17E50` als vermoedelijke plek van de invoer; er bestaat geen beeld van het uitgepakte spel om dat na te
lezen, en een beeld is de enige manier: het adres is statisch niet te vinden (de code staat in een pakker, `.text` is op schijf leeg).

**Meetvoorstel (twee runs, in deze volgorde, alles alleen-lezen voor het spel).**
1. *Wat vraagt de game aan Windows?* `[Wine] Debug = +setupapi,+file,+serial,+dinput,+hid` in `game.ini`, dubbelklik, 90 s attract, stoppen.
   Uit `~/.local/state/ravengun/logs/direct-LuigisMansion-*.log`: de GUID waarmee `SetupDiGetClassDevsW` zoekt (COM-poortklasse
   `86E0D1E0-8089-11D0-9CE4-08003E301F73`, of een HID/eigen klasse), of er daarna een `CreateFileW(\\.\COMx)` komt, en welke `DirectInput8Create`/
   `CreateDevice`-aanroepen volgen. Dat bepaalt welke van de twee wegen (seriële poort of DirectInput) de invoerweg is.
2. *Beeld van het uitgepakte spel.* Zelfde run, na 60 s (`.text` is dan uitgepakt; de pakker klaart op ~10 s): `echo deck | sudo -S python3
   patches/LuigisMansion/b0-geheugen-dump.py <pid van VACUUM.exe> <uitmap> t60` (bestaand instrument; schrijft `image-t60.bin`, 19 MB, plus
   `maps`). Met dat beeld is RVA `0x17E50` en het gebruik van `SetupDiGetClassDevsW`/`CreateFileW` na te lopen met `objdump -b binary -m i386:x86-64
   --adjust-vma=0x140000000`, en is te zien of de invoer een geheugenstruct is of een seriële regel.
3. Pas daarna: óf een schrijver in `rg-pe-hook` die het bordbeeld in die struct zet (vergelijk `plugins/bepinex/shared/Bordbeeld.cs` voor de
   indeling `LuigisMansion`), óf een pty op COM1 (`tools/rg-ptylink`, `tools/wine-comm-shim`) met de bytes van de game gelogd. Zonder stap 1 en 2 is
   elke wijziging gokwerk, en ik lever er daarom geen.

## 4. Men in Black — "PRESS START BUTTON TO JOIN" blijft

**Bron (decompilatie van `Assembly-CSharp.dll`, `globalgamemanagers`).** Op de kast is het I/O-bord een joystick voor Unity: de InputManager heeft
`PlayerStart1` = joystick button 5, `PlayerStart2` = 6, `Player1Trigger1` = 7/8, `Player2Trigger1` = 9/10 en de assen `X Axis0/1`, `Y Axis0/1`.
`CreditsUI.CheckCreditTaken` (start) leest `Input.GetButtonDown("PlayerStart1")` alleen als `!MIBCore.DisableInputs`, en `MIBCore.Update` zet
`DisableInputs = true` zodra `Input.GetJoystickNames()` een niet-lege naam geeft; `MIBFilterInput.GetFilterInput` geeft dan `(0,0)`. Onder Wine zijn
onze virtuele geweren, het virtuele toetsenbord en elk echt gamepad joysticks, dus `DisableInputs` stond altijd aan. Een muntteken hoeft niet:
de run is FREEPLAY.

**Wijziging.** `plugins/bepinex/MenInBlack/MenInBlack.cs` (leest `TeknoParrot_JvsState`, indeling WartranTroopers, via `plugins/bepinex/shared/Bordbeeld.cs`):
1. na elke `MIBCore.Update` `DisableInputs` op onwaar; 2. Harmony-prefix op `Input.GetButton/GetButtonDown/GetButtonUp(string)` voor de vier namen (andere
namen gaan naar Unity); 3. postfix op `MIBFilterInput.GetFilterInput(int)`: schermpunt uit het bordbeeld (oorsprong linksonder, zonder kalibratie van de
shell); 4. munt 1 (rand) = `MIBCore.SetCreditValue(MIBData_Credits(0, n, 0, 99))`, voor als de shell-instellingen geen freeplay zijn.
**Test.** `scripts/build-bepinex-plugin.sh MenInBlack` bouwt (gedaan); `python3 tests/test_bepinex_bordbeeld.py` (tabellen) slaagt. Het gedrag zelf is alleen op de
tv te meten. **Tv-recept.** `build/bepinex-plugin/MenInBlack/RavenGun.MenInBlack.dll` naar `patches/MenInBlack/dropin/` en
`~/.local/share/ravengun/patches/MenInBlack/dropin/` (de `dropin.ini` blijft), dubbelklik; `MIB/BepInEx/LogOutput.log` moet `3 van 3 knopaanroepen aangehaakt` en
(eenmalig) `MIBCore.DisableInputs stond aan ...` zeggen; `key 1` 0,3 s: het scherm gaat van PRESS START BUTTON naar PLAYER 1 READY. Staat `3 van 3` er
niet, dan is `Input.GetButton*` niet te haken (icall): terugval is `MIBCore.Players |= 1` via `PlayerPressedStart`.
