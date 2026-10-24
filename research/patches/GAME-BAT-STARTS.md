# De officiele startscripts van de games (en wat wij overslaan)

Gemeten op 2026-10-13 in de views (`~/.local/share/ravengun/runtime/views/<profiel>/`), alleen
lezen. Zeven games hebben daar een `game.bat`; de inhoud is allesbehalve triviaal. **Wij starten de
exe direct en slaan dit allemaal over.** Dat is een systemisch verschil tussen onze start en de
start van de kast, en het raakt minstens twee games aantoonbaar (zie de `-UNATTENDED`-vondst bij
Scarlet Dawn) en vermoedelijk meer.

## Wat er in de scripts staat

### LuigisMansion (`Luigi's Mansion Arcade/game.bat`, 259 B, UTF-8 met BOM)

```bat
@echo
setlocal
rem ゲームID
set GAMEID=SDCF
rem アプリデータ領域の排他利用
if not exist Y:\%GAMEID% (
    rmdir /S /Q Y:\
    mkdir Y:\%GAMEID%
)
start /wait sendFlash COM1 LMAIO.bin
start /wait x:\exe\x64\VACUUM.exe
```

* **`GAMEID=SDCF`** wordt als omgevingsvariabele gezet.
* Het app-datagebied **`Y:\SDCF`** wordt klaargezet; bestaat het niet, dan wordt **heel `Y:\` gewist**
  ("exclusief gebruik van het app-datagebied").
* **`sendFlash COM1 LMAIO.bin`** schrijft I/O-board-gegevens via de **seriele poort** en wacht daarop.
* Het spel wordt gestart als **`x:\exe\x64\VACUUM.exe`** -- vanaf een schijfletter, niet vanaf het pad
  dat wij gebruiken, en zonder argumenten.

### HOTDSD (`House of the Dead Scarlet Dawn/game.bat`, 522 B, CP932)

```bat
@echo off
set LAUNCH_ON_IMAGE=1
if "%IMAGE_ROOT_DIR%"=="" set IMAGE_ROOT_DIR=X:\
pushd "%IMAGE_ROOT_DIR%\App\Startup"
call AppStartup.bat
popd
```

* `LAUNCH_ON_IMAGE=1` laat `StartApp.bat` de Y-schijf initialiseren.
* `IMAGE_ROOT_DIR=X:\` is de image-omgeving; de app start vanuit `X:\App\Startup`.
* `AppStartup.bat` zet op zijn beurt `LAUNCH_MODE` en `DETAIL_LAUNCH_MODE` (kast-, doelbord- of
  ontwikkelmodus) en `StartApp.bat` geeft `-UNATTENDED` mee ("foutvenster niet tonen") -- precies het
  argument waarvan is gemeten dat het ontbreken ervan het spel 60 s op een foutvenster laat wachten.

### LGS (`Let's Go Safari/game.bat`, 93 B)

```bat
mxGetHwInfo.exe -s mxGetHwInfo.ini
if %ERRORLEVEL% == 0 start /wait GameSampR_RingWide.exe
```

### OG (`OAV6DU~3/game.bat`, 69 B)

```bat
mxGetHwInfo.exe -s mxRingwide.ini
if %ERRORLEVEL% == 0 gs2.exe.bak
```

Bij beide: **een hardware-info-helper bepaalt OF het spel mag starten**; bij een andere uitkomst dan 0
start het spel helemaal niet. Wij slaan die poort over.

### MedaruNoGunman (`Medaru No Gunman/game.bat`, 266 B)

```bat
@echo off
rem mxGetHwInfo.exe -s mxGetHwInfo.ini
cd exe
start /wait game.exe
```

Hier is de hardware-controle uitgecommentarieerd en draait het spel vanuit de submap `exe`.

### LGI en LGI3D

Hebben ook een `game.bat`; de inhoud is nog niet gelezen (staat op de lijst).

## Waarom dit ertoe doet

1. **Omgevingsvariabelen** (`GAMEID`, `LAUNCH_ON_IMAGE`, `IMAGE_ROOT_DIR`, `LAUNCH_MODE`) die het spel
   verwacht en die wij niet zetten.
2. **Een poort voor het starten**: `mxGetHwInfo.exe` moet 0 teruggeven, anders start het spel niet.
3. **Schijfletters** (`X:`, `Y:`) met een eigen betekenis; de gesloten kern vult die met een omleiding
   naar een map onder zijn eigen installatie.
4. **Een pre-start stap met hardware** (`sendFlash` via de seriele poort bij Luigi's Mansion).
5. **Een andere werkmap** dan wij gebruiken.

## Wat hieruit volgt voor onze lader (en wat NIET)

De lader hoort **generiek** te kunnen: per game argumenten meegeven, per game omgevingsvariabelen
zetten, en per game een officieel startscript kunnen gebruiken. De **inhoud** hoort per game in
`patches/<Game>/` te staan, niet in de lader -- de lader blijft schoon en generiek.

## Status per stuk

| stuk | status |
|---|---|
| `GAMEID=SDCF` bij Luigi's Mansion zetten | test loopt |
| `Y:\SDCF` / schijfletters | te onderzoeken (hoe doet de gesloten kern dat, en hoe wij) |
| `sendFlash COM1 LMAIO.bin` | te testen (`com1` moet naar `/dev/ttyS0`) |
| `mxGetHwInfo.exe` als poort bij LGS/OG | nog niet onderzocht |
| officiele startscripts als route in de lader | bij de laderbouwer |
| Halo's eigen startmanier | uitgezocht door de Halowerker |
