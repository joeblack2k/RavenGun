# De twee-programma-start van HOTD Scarlet Dawn — en waarom dat lader-werk is

*Ronde 12 (2026-10-11). Alleen lezen. Dit is de onderbouwing van blokkade 1 in
[`docs/amdaemon-layer.md`](../amdaemon-layer.md); het mechanisme zelf staat in
[`amdaemon-mechanisme.md`](amdaemon-mechanisme.md).*

## 1. Het bewijs dat het om twee programma's gaat

Uit het TeknoParrot-profiel `GameProfiles/HOTDSD.xml`:

```xml
<ExecutableName>Hodzero-Win64-Shipping.exe</ExecutableName>
<ExecutableName2>amdaemon.exe</ExecutableName2>
<HasTwoExecutables>true</HasTwoExecutables>
<LaunchSecondExecutableFirst>true</LaunchSecondExecutableFirst>
<ExtraParameters></ExtraParameters>
<EmulationProfile>ALLSHOTDSD</EmulationProfile>
```

en uit `GameSetup/HOTDSD.xml`:

```xml
<GameExecutableLocation>App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe</GameExecutableLocation>
<GameExecutableLocation2>App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe</GameExecutableLocation2>
```

`patches/HOTDSD/game.ini` heeft hetzelfde, in de vorm die de launcher leest:
`HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true`, `Executable2 = amdaemon.exe`.

En de game zegt het zelf, in de eigen batchketen. Dat is het sterkste bewijs: de spelmaker heeft de
twee-processen-start ingebouwd, dus het is geen TeknoParrot-uitvinding.

## 2. De keten, bestand voor bestand

Alle vier de bestanden zijn met `iconv -f CP932` gelezen; de commentaren zijn Japans, de sleutels zijn
dat niet. `$G` is de spelmap.

**`$G/game.bat`** (522 bytes)

```bat
set LAUNCH_ON_IMAGE=1
if "%IMAGE_ROOT_DIR%"=="" set IMAGE_ROOT_DIR=X:\
pushd "%IMAGE_ROOT_DIR%\App\Startup"
call AppStartup.bat
popd
```

**`$G/App/Startup/AppStartup.bat`** (1.5 kB) roept vier dingen aan, in deze orde:

```bat
call 05_SetVideoPort.bat      :: reg add HKLM\SYSTEM\SEGA\SystemProperty\amVideo\PortSetting port_1..port_5
call 10_SetResolution.bat     :: ..\Tools\SetResolution.exe 1920 1080 0
call 20_SetAudioVolume.bat    :: ..\Tools\SetAudioVolume.exe 85 75
call 99_StartApp.bat
```

**`$G/App/Startup/99_StartApp.bat`** (2.1 kB) zet de app-gegevens en gaat naar de game:

```bat
set LAUNCH_MODE=target
set GAME_ID=SDET
set APP_DIR=Y:\%GAME_ID%
set APP_LOG_DIR=%APP_DIR%\current_log
set ABAAS_LOG_DIR=%APP_DIR%\log
if "%LAUNCH_ON_IMAGE%"=="1" ( if not exist "%APP_DIR%" ( rmdir /S /Q "Y:\" & mkdir "%APP_DIR%" ) )
mkdir "%APP_LOG_DIR%" >nul 2>&1
pushd ..\Hodzero
call StartApp.bat %LAUNCH_MODE%
popd
```

**`$G/App/Hodzero/StartApp.bat`** (8.1 kB) doet het echte werk. De relevante regels:

```bat
set FILE_PATH_FOR_MINI_CABINET=..\..\Dev\AsMiniCabinet     :: bestaat niet in deze kopie
set FILE_PATH_FOR_TARGET=..\..\Dev\AsTarget                :: bestaat niet in deze kopie
if "%1"=="" ( set LAUNCH_MODE=pc     ... ) else ( set LAUNCH_MODE=target & set DETAIL_LAUNCH_MODE=cabinet )

set ROOT_DIR=WindowsNoEditor
set COMMON_LOG_DIR=%APP_LOG_DIR%\%DATETIME_TEXT%           :: YYMMDD_hhmmss, want LAUNCH_MODE=target

set DAEMON_CHECK_USE_FILE_PATH=..\..\Dev\Tools\CheckUseAMDaemon.bat   :: bestaat niet
set DAEMON_DIR=%ROOT_DIR%\AMDaemon
set DAEMON_EXE_FILE_NAME=amdaemon.exe
set DAEMON_EXE_PATH=%DAEMON_DIR%\%DAEMON_EXE_FILE_NAME%
:: DETAIL_LAUNCH_MODE=cabinet -> de "normale" tak:
set DAEMON_CONFIG_PATH=%DAEMON_DIR%\config.json
set DAEMON_WAIT_SECONDS=5
set DAEMON_LOG_FILE_NAME=amdaemon.log
set DAEMON_LOG_PATH=%COMMON_LOG_DIR%\%DAEMON_LOG_FILE_NAME%

set APP_EXE_DIR=%ROOT_DIR%\Hodzero\Binaries\Win64
:: Shipping -> Test -> Development -> Debug
set APP_EXE_PATH=%APP_EXE_DIR%\Hodzero-Win64-Shipping.exe
set APP_EXE_ARGS=%APP_EXE_ARGS% -launch="%DETAIL_LAUNCH_MODE%"
:: APP_LOG_PATH is gezet (LAUNCH_MODE=target en COMMON_LOG_DIR niet leeg):
set APP_EXE_ARGS=%APP_EXE_ARGS% -ABSLOG="%APP_LOG_PATH%" -UserDir="%COMMON_LOG_DIR%" -NotInstalled -UNATTENDED

:: --- de daemon, dan wachten ---
start /min "" "%DAEMON_EXE_PATH%" -c %DAEMON_CONFIG_PATH% -l "%DAEMON_LOG_PATH%"
timeout %DAEMON_WAIT_SECONDS%

:: --- de game, en wachten tot hij klaar is ---
start /wait "" "%APP_EXE_PATH%" %APP_EXE_ARGS%
timeout /t 1

:: --- en opruimen ---
taskkill /im "%DAEMON_EXE_FILE_NAME%"
```

Drie dingen vallen op:

* **`DETAIL_LAUNCH_MODE=cabinet`**, want `99_StartApp.bat` geeft `target` mee als argument en de twee
  ontwikkel-bestanden `Dev\AsTarget` en `Dev\AsMiniCabinet` bestaan niet. Daardoor wordt
  `config.json` gebruikt en niet `config_target.json` of `config_cabinetMini.json`. **Die twee
  bestanden staan ook niet in de map** — de vraag uit `findings/antwoorden_scarlet_dawn.md` §9 naar
  hun inhoud is dus niet meer relevant voor deze kopie.
* **`CheckUseAMDaemon.bat` bestaat niet**, dus de tak die de daemon zou overslaan wordt nooit
  gekozen: de daemon wordt altijd gestart.
* **De daemon wordt na de game gedood** (`taskkill`). De volgorde is dus: daemon erbij, game
  erbij, game weg, daemon weg.

## 3. Het recept

```
1.  amdaemon.exe                          <- eerst
    argumenten in de eigen keten:  -c WindowsNoEditor\AMDaemon\config.json
                                   -l Y:\SDET\current_log\<YYMMDD_hhmmss>\amdaemon.log
    argumenten onder TeknoParrot:  GEEN (ExtraParameters is leeg; de daemon neemt dan zijn eigen
                                   standaardwaarden en komt toch tot "Process state: Advertise")
2.  5 seconden wachten                    (DAEMON_WAIT_SECONDS=5; de opdracht is `timeout 5`)
3.  Hodzero-Win64-Shipping.exe            <- dan
    argumenten:  -launch="cabinet"
                 -ABSLOG=... -UserDir=... -NotInstalled -UNATTENDED
4.  na het einde van de game: taskkill /im amdaemon.exe
```

De **werkmap** die de keten opbouwt is `App/Hodzero`: `99_StartApp.bat` doet `pushd ..\Hodzero`
vanuit `App/Startup`, en `StartApp.bat` gebruikt daarna relatieve paden (`WindowsNoEditor\AMDaemon`,
`WindowsNoEditor\Hodzero\Binaries\Win64`). Onder TeknoParrot is de werkmap niet vastgesteld — het
profiel start de exe's kaal. **Onbekend.**

Wat verder in het recept hoort: **beide processen krijgen dezelfde lader en dezelfde kern.** De
eerdere ronde heeft gemeten dat TeknoParrotUi beide exe's via de lader met de kern start
(`findings/HOTDScarletDawn.md` §1: "TeknoParrotUi start beide via de lader met de kern"), en dat is
ook nodig: de omleiding van de schijfletters en de invoerpipe komen uit die kern.

En: **`game.bat` wordt in deze route niet gebruikt.** De launcher start de twee exe's rechtstreeks,
dus `05_SetVideoPort.bat`, `10_SetResolution.bat`, `20_SetAudioVolume.bat`, het logboek-beheer
(`CleanupLogDir.exe`, het verplaatsen van `appfile.dat`) en de `Y:`-initialisatie
(`rmdir /S /Q Y:\`) gebeuren niet. Wat daarvan nodig is, is niet gemeten.

## 4. De blokkade in de lader, gemeten

```
$ cd ~/projects/ravengun && python3 scripts/launch-direct.py HOTDSD --plan
Niet direct te starten: profielen met twee exe's worden niet direct gestart
$ echo $?
4
```

In `scripts/launch-direct.py` staat de weigering op één regel:

```python
if truthy(p.get("HasTwoExecutables", "false")):
    raise Unsupported("profielen met twee exe's worden niet direct gestart")
```

Die regel staat **vóór** alles wat met de lader, de kern, de dongle of de drop-ins te maken heeft.
Voor HOTDSD komt de launcher dus nooit aan de rest toe.

## 5. Waarom dit geen map kan zijn

Een drop-in is, sinds `docs/geen-dongle-in-de-loader.md`, *bestanden in de view plus
omgevingsvariabelen*. Daarmee kun je:

* geen tweede proces starten;
* geen 5 seconden wachten tussen twee processen;
* geen proces doden als het eerste klaar is;
* geen proces in de **juiste volgorde** met dezelfde lader en kern starten.

Dat zijn alle vier laderfuncties. Er is geen bestand dat dit kan overnemen, dus er valt voor deze
game niets in `patches/HOTDSD/dropin/` te zetten. **"Dit kan niet per game" is het antwoord.**

## 6. Wat de lader zou moeten krijgen, en waarom dat generiek is

De uitbreiding is klein en niet Scarlet-Dawn-specifiek:

| Nodig | Waarom generiek |
|---|---|
| Een profielveld dat zegt "twee exe's, deze eerst" — dat bestaat al (`HasTwoExecutables`, `LaunchSecondExecutableFirst`, `ExecutableName2`) en wordt alleen geweigerd | Elke Sega ALLS-titel met een daemon heeft deze vorm. Het profielformaat is er al; alleen de uitvoering ontbreekt. |
| Een wachttijd tussen de twee starts | `DAEMON_WAIT_SECONDS` staat in de eigen batchketen van de game; een profielveld (of een `game.ini`-sleutel) kan hem per game bevatten. |
| Beide processen door dezelfde lader met dezelfde kern | Dat doet de lader al voor één proces; het is een lus, geen nieuwe techniek. |
| Opruimen: de daemon doden als de game klaar is | `StartApp.bat` doet het zelf met `taskkill`; de lader kan hetzelfde doen bij het sluiten van het eerste proces. |

Zodra dit er is, is **de startvorm** voor elke ALLS-titel een map. Wat dan overblijft is blokkade 2
uit `docs/amdaemon-layer.md`: er is geen daemon. Dat is engine-werk, en de omvang daarvan is te
vergelijken met de eigen kern uit `docs/community-model.md` §2c — met dit verschil dat de daemon
minder is dan een gamekern: hij hoeft geen beeld te maken, alleen diensten te leveren.

## 7. Onbekend

1. De **werkmap** die TeknoParrotUi voor de twee processen gebruikt.
2. Of de lader de tweede exe met dezelfde **kern** injecteert of met een eigen exemplaar.
3. Of de game zonder de vier batchstappen (`SetVideoPort`, `SetResolution`, `SetAudioVolume`,
   logboekbeheer) net zo ver komt. In de speelbare route gebeuren ze niet, dus waarschijnlijk wel —
   maar dat is niet gemeten maar afgeleid uit het feit dat de route werkt.
4. Of de `rmdir /S /Q Y:\`-initialisatie nodig is voor een schone eerste start. De map
   `AMDaemon/TeknoParrot/Y/` is in deze kopie leeg en `D/SDET/appfile.dat` is 8 MB en door de daemon
   geschreven, dus de daemon maakt zijn eigen gegevens aan.
