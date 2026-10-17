# Aliens Extermination zonder TeknoParrot

Stand 2026-10-17. Alleen statische analyse van de exe op de NAS (alleen gelezen) en de bronnen van TeknoParrotUI; er is niets
gestart. Gebouwd en getest: `tools/rg-aliens/` (haak-DLL en logica) en `tests/aliens_test.sh`.

## Wat doet TeknoParrot voor deze game

De kern is gepakt en niet te lezen; dit is dus afgeleid uit de exe zelf en uit wat TeknoParrotUI schrijft
(`AliensExterminationPipe.cs`: een 32-bits besturingswoord op offset 8 en vier asbytes op 12/16/20/24 in `TeknoParrot_JvsState`).

| Onderwerp | TeknoParrot | Bewijs |
|---|---|---|
| Richten en wapenknoppen | zet de gedeelde-geheugenwaarden om in de joystickwaarden die de game vraagt | de exe roept `winmm!joyGetPosEx(0)` en `(1)` aan (0x42ed70, 0x42fcc0); `dwXpos`/`dwYpos` zijn ruw, `dwButtons` bit 0/1/2 geeft de spelmaskers 0x10/0x20/0x40 (0x42ee60), en de testpagina van de game noemt die maskers TRIGGER, SPECIAL en FLAME (tabel op 0x873b08, per speler) |
| Munt, start, test, select, volume | idem, als toets | de game leest ze met `GetAsyncKeyState` uit twee tabellen (0x85edf0 hoofdtoetsen, 0x85ee48 numeriek blok) en `DATA/CONTROLS.txt` zegt: munt L-CTRL/L-SHIFT/L-ALT, start `1`/`2`, service `5`, bevestigen `6`, menu `9`/`0`; de testpagina noemt COIN 1-4, START 1-2, TEST, SELECT, VOLUME UP/DOWN met dezelfde maskers |
| HID-geweren en GVRIO-kaart | niets zichtbaars: het opstartscherm meldt nog steeds "no GVRIO card" en "no Aliens guns" | De game zoekt via SetupDi/HidD VID 04B4 PID 6870 (geweren, max. 2) en VID 04B4/1AB7 PID 7013 (kaart, max. 1) (0x42f1f0, 0x42f110), opent ze met `CreateFileA` en **schrijft** er alleen naar (`WriteFile`, 0x42efc0: lampen, terugslag "kicker", 7-segment). Er is nergens een `ReadFile` of `HidD_GetInputReport` op die apparaten. De tellers voeden alleen het opstartscherm (0x42ed00, 0x42ed10). |
| Dongle | niets nodig van de kern | `aliens dehasped.exe` heeft zijn HASP-emulatie achter `.text` (zie `PeFillPages` in game.ini); de enige lezer van "dongle.tga" is de foutafhandeling van die emulatie |
| Venster en resolutie | `General/CustomResolution` 1920x1080 en `Windowed` | de game vraagt zelf glut-venster 800x600 (0x42ea69: `glutInitWindowSize(0x320, 0x258)`; `-full` in COMMANDLINE.txt gebruikt `"%dx%d:32@60"`) |
| Controle van de exe | de kern rekent een CRC over de kop (gemeten: exitcode 1337 zonder die CRC) | alleen in de gesloten kern |

De community draait dit spel met een AutoHotkey-script dat alleen `aliens dehasped.exe` start (`DATA/Alien Extermination.ahk`): de
game heeft dus geen kern nodig; het enige wat een kern toevoegt is invoer van iets anders dan een joystick en een toetsenbord.

## Wat er nu is (route A, aanbevolen): haak-DLL op de twee importplekken

`tools/rg-aliens/rg_aliens_hook.c` wordt door onze eigen OpenParrot-lader als `TP_PRELOAD_DLL` in het spelproces geladen
(`Build = ravengun`, `Core = ravengun`; de kern herkent de game niet en haakt niets, patch 0003) en zet de importplekken van
`winmm!joyGetPosEx` (VA 0x59892b8) en `user32!GetAsyncKeyState` (VA 0x59892b0) om. Beide plekken zijn aan de echte exe nagemeten
(`tests/aliens_test.sh` laadt de exe in een beeld en zoekt ze met dezelfde functie als de DLL).

- **Richten**: byte 0..255 uit `TeknoParrot_JvsState` wordt omgerekend op de EIGEN kalibratie van de game. De game houdt per speler
  min/max van x en y in het geheugen (VA 0x596af88/0x596af8c voor x, 0x596af98/0x596af9c voor y, stap 0x318 per speler; gevuld uit
  de opties 0xe5..0xe8 van `C:\PC\ALIENS\DATA\ABH.ADJ`, 0x4486xx). De DLL leest die vier floats en zet byte 0 op min en byte 255 op
  max, dus het vizier landt op de randen van het scherm wat de kalibratie ook is. Zijn ze onbruikbaar (nul, NaN) dan geldt 0..65535.
  In `DATA/ABH.ADJ` van de dump is speler 1 `0..65535`.
- **Knoppen**: trekker, special, vlam naar `dwButtons` bit 0/1/2 (speler 1: 0x200/0x10/0x20; speler 2: 0x4000/0x04/0x08 in het
  besturingswoord).
- **Toetsen**: een toets staat voor de game "ingedrukt" als de echte toets ingedrukt is OF de bit in het gedeelde geheugen staat
  (munt 1/2: 0x400/0x800, start 1/2: 0x100/0x2000, test 0x40, select 0x80, volume 0x8000/0x10000). Munt 3/4 staan niet in
  `packaging/tp-shm-profiles.json` en dus niet in de vergelijking.
- **Echte joysticks** worden niet doorgelaten: id 0 en 1 komen alleen uit het gedeelde geheugen, ander id geeft `JOYERR_UNPLUGGED`.
- **Opstartscherm**: de twee getters (0x42ed00, 0x42ed10) worden `mov eax,2; ret` en `mov eax,1; ret` als de bytes EXACT kloppen.
  De HID-tellers blijven 0, dus de game schrijft niet naar apparaten die er niet zijn. Uit te zetten met `RG_ALIENS_NO_IOFIX=1`.
- Logboek `RG_ALIENS_LOG` is het laadbewijs: PE-regel, "bekende exe", elke omgezette importplek, eerste `joyGetPosEx` per speler met
  de gelezen kalibratie, eerste keer elke toets. Geen regels = DLL niet in het spelproces geweest.

Waarom niet de HID-apparaten nabouwen (route B in `AliensExtermination.hid-boards.json`)? De game leest er niet uit; de
rapportindeling van de schrijfkant (rapport-ID 2/3/4, lengte uit de `HidP_GetCaps`) is statisch niet vast te stellen en een uinput-joystick
lost de toetsen (munt/start) niet op en geeft speler 2 geen vast joystick-id (`wine_joystick_zero` kent een VID:PID). Route B staat er als
terugval: twee uinput-borden met het echte VID/PID van het geweer, drie knoppen. Gevalideerd met `parse_hid_boards` en `hid_board_frame`.

## Configuratie

- `docs/research/losse/AliensExtermination.game.ini` (kandidaat) + `AliensExtermination.dropin/{dropin.ini,rg-aliens-hook.dll}`.
  Samen verhuizen naar `patches/AliensExtermination/` zodra de proef slaagt. De shipped `game.ini` blijft op de gesloten kern.
- `AimAspect = 4:3`: zonder de kern draait de game zijn eigen 800x600-venster; gamescope zet dat met pillarbox. Is het beeld toch 16:9, weghalen.

## TV-test (de lead)

1. Kandidaat plaatsen (game.ini + dropin/), starten met dubbelklik. Wachten tot het opstartscherm er is.
2. Lees `.../views/AliensExtermination/DATA/rg-aliens-hook.log`: moet "joyGetPosEx: importplek RVA 0x55892b8 omgezet" en
   "GetAsyncKeyState: importplek RVA 0x55892b0 omgezet" en "bekende exe: ja" bevatten. Op het opstartscherm hoort nu
   "2 ALIENS GUNS FOUND" en "GVRIO CARD FOUND" te staan.
3. Move/muis: munt (komt als toets L-CTRL), start (toets `1`), richten op de randen en het midden (vizier moet overeenkomen met de
   Move), trekker, special (rood), vlam (geel) - log noemt de toetsen en de eerste `joyGetPosEx` met de gelezen kalibratie.
4. Draait het vizier omgekeerd: `RG_ALIENS_INVERT_X=1` / `RG_ALIENS_INVERT_Y=1` in `Env` van dropin.ini.
5. Het 5-minutenbewijs pas na (3).

## Open

- Niet gemeten (geen run mogelijk): of de lader de DLL laadt voordat de game zijn toetspollthread start. Die thread (0x42f430) bewaart
  `GetAsyncKeyState` in een register voor zijn munttabel (0x5279f88); wordt de DLL te laat geladen, dan werken de munten niet maar de
  rest wel. Het logboek laat de volgorde zien. Mocht het zo zijn: `[Loader] EntryPointMs` laat de lader bij het startpunt laden.
- De Y-richting en de kalibratie van speler 2 (in de dump `3700..8100`, mogelijk een vergeten fabriekswaarde) zijn alleen op de tv te zien.
- Start/munt voor speler 3/4 en de toetsen `8` (0x10000000) en `/` (numeriek) van de game zijn niet gekoppeld.
