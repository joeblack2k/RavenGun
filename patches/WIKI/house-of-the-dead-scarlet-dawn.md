# House of the Dead: Scarlet Dawn (Sega ALLS) — publieke digest

Dit is een technisch verslag van wat wij van dit spel hebben gemeten en geleerd, geschreven voor iemand
die dit project **niet** kent en er een eigen front-end of emulator op wil bouwen. Per bewering staat of
hij **GEMETEN** is (met cijfers, tijdstippen, paden, regelnummers) of **AFGELEID**.

Alles is op een Steam Deck (SteamOS) onder plain Wine gemeten. De spelbestanden staan op een NAS en zijn
daar **alleen gelezen**; waar een meting toch moest schrijven, staat dat er expliciet bij. Eén ding staat
bewust **niet** in deze pagina: de details van de hardwarematige beveiligings- en licentielaag van het
platform. Die laag komt verderop alleen als *niet bereikt* ter sprake.

---

## 1. Platform en kast

Het spel is **House of the Dead: Scarlet Dawn** (Sega, 2018) op het **Sega ALLS**-platform, de
UX-variant. Het is een 64-bits Windows-programma in Unreal Engine 4.18, en het is een lichtgeweerspel
voor twee spelers.

| | |
|---|---|
| platform | Sega ALLS UX — `pxGetHwInfo.ini`: `platform-id = ACA0`, en de Japanse toelichting in dat bestand noemt `ACA0` expliciet "ALLS UX" **GEMETEN** |
| engine | Unreal Engine **4.18.1** — `CrashContext.runtime-xml`: `EngineVersion 4.18.1-0+++UE4+Release-4.18`, `BuildConfiguration Shipping`, `PlatformName WindowsNoEditor` **GEMETEN** |
| spelbestand | `Hodzero-Win64-Shipping.exe`, **54.128.128 B**, PE32+ x86-64, GUI-subsysteem, 9 secties, ImageBase `0x140000000`, entry point RVA `0x1c2929c`, tijdstempel `2018-08-21T10:10:19Z`, en **50 geïmporteerde DLL's** **GEMETEN** |
| codenaam | `Hodzero`; `-Win64-Shipping.exe` is de standaardnaam die Unreal aan een uitlevering geeft **GEMETEN** |
| spel-id | `SDET` (`App/Startup/99_StartApp.bat`: `set GAME_ID=SDET`) **GEMETEN** |
| tweede programma | `amdaemon.exe`, **8.272.384 B**, PE32+ x64, **console**-subsysteem, entry point RVA `0x2268a4`, tijdstempel `2018-04-19T14:13:45Z`, versieregel `amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37` **GEMETEN** |
| inhoud | één Unreal-pak van **7.847.593.660 B** plus losse CRIWARE-content **GEMETEN** |

**De kast.** De invoer en het beeld staan in `App/Hodzero/WindowsNoEditor/AMDaemon/config.json`
(3.171 B) **GEMETEN**:

* **twee spelers**, elk met een geweer: per speler `start` (USB-index 7), `gun_select` (index 4) en
  `gun_special` (index 1), alle op USB-node 0;
* kastschakelaars op diezelfde node: `test` (index 0, niet aan een speler gebonden), `service` (index 6),
  `bakuon` (14), `eye0` (10), `proximity_cabinet` (10), `cabinet_kind` (11) en drie betaalterminal-
  schakelaars (`emoney_left/right/center`, index 13/12/11);
* het I/O-bord moet volgens die configuratie vier functies hebben: `coin_input` (slot 2),
  `switch_input` (2 spelers × 8 knoppen), `general_output` (slot 20) en één `unique_io`-commando
  (command 65, parameter `[6]`, alleen uitgang);
* beeld: `video.enable = true`, `resolution 1920 × 1080`, `ignore_unconnected = true`. De officiële
  startketen zet het bureaublad ook echt op 1920 × 1080 (`SetResolution.exe 1920 1080 0`);
* munten: `credit.enable = true`, `max_credit = 24`, `game_cost = [-1,-1,1,1,-1,-1,-1,-1]`;
* foutafhandeling: `error.auto_reset = true` — een herstart na een fout is dus **verwacht gedrag**.

De machine in de logs (Ryzen 7 7800X3D, Radeon RX 6950 XT onder RADV, 31 GB) is **onze testmachine**,
niet de kast **GEMETEN**.

## 2. Wat het spel nodig heeft

### Mappen en paden (**GEMETEN**)

De hoofdmap op de NAS (`…/Teknoparrot/House of the Dead Scarlet Dawn`) bevat **2.081 bestanden,
13.095.511.155 B, en nul lege bestanden**. De boom:

```
game.bat  firewall.cfg  pxGetHwinfo.exe  pxGetHwInfo.ini
App/Startup/                 5 .bat  (05_SetVideoPort, 10_SetResolution, 20_SetAudioVolume, 99_StartApp, AppStartup)
App/Tools/                   3 .exe  (SetResolution, SetAudioVolume, CleanupLogDir)
App/Hodzero/StartApp.bat
App/Hodzero/WindowsNoEditor/
   AMDaemon/                  5 bestanden + aime_firm/ (6) + am_resources/ (9)
   Engine/Binaries/ThirdParty/  PhysX (11), Vorbis (2), Ogg (1), NVaftermath (1), Windows/DirectX (1)
   Hodzero/Binaries/Win64/    2 .exe, 2 .png, teknoparrot.ini
   Hodzero/Content/Paks/      1 pak, 7.847.593.660 B
   Hodzero/Content/Hodzero_V1/ADX/  2.014 bestanden (1.890 .uasset + 123 .usm + 1 .acf), 5.024.069.116 B
   Hodzero/Saved/Config/WindowsNoEditor/Game.ini  (274 B)
```

### Is het compleet? Vier structurele controles (**GEMETEN**)

Dit is een **structurele** controle en geen hash-controle: de verwachte bestandshashes van de
integriteitstest die het commerciële hulpmiddel gebruikt zitten niet in de profielbestanden. Wat we
konden controleren:

| Controle | Uitkomst |
|---|---|
| bestaat het uitvoerbare bestand dat het profiel noemt | **ja** — `Hodzero-Win64-Shipping.exe` van 54.128.128 B (precies de maat die het profiel vastlegt) en `amdaemon.exe` |
| lege bestanden (0 bytes) | **0 van de 2.081** |
| is het beeld heel (niet afgekapt) | **32 PE-bestanden, 0 afgekapt** — het einde van de laatste sectie is exact het einde van elk bestand |
| zijn alle bibliotheken vindbaar | **0 onopgelost** — het spel importeert 50 DLL's, de daemon 23, `hwvalue.exe` 7 en `pxGetHwinfo.exe` 5; alle **44 unieke namen** staan in de Wine-prefix (`system32` én `syswow64`, 598 respectievelijk 631 DLL's), inclusief de gebruikelijke pijnpunten `vcruntime140`, `msvcp140`, `ucrtbase`, `mfplat`, `d3d9/d3d11/dxgi`, `xinput1_3`, `X3DAudio1_7` |
| verwijzingen in de eigen scripts die ontbreken | 9 stuks, **alle 9 met een gemeten onschuldige verklaring** (terugval-exe's die niet nodig zijn, ontwikkelaarsmarkeringen waarvan de afwezigheid juist de "cabinet"-modus kiest, een bestandsnaam uit een Japanse commentaarregel) |

**De pak is niet afgekapt**, en dat is te bewijzen zonder het spel te starten: de laatste 44 bytes zijn
een gave Unreal-footer — magic `0x5A6F12E1`, versie 4, `IndexOffset 7.839.093.987`,
`IndexSize 8.499.628` — en `index_offset + index_size` eindigt **exact** waar die footer begint, met de
footer exact op het einde van het bestand. Er is geen ruimte voor ontbrekende inhoud en geen bungelende
offset **GEMETEN**.

### Wat de pak niet bevat (**GEMETEN**)

Dat is de moeite waard voor iedereen die deze titel wil namaken of verplaatsen. De pak bewaart een Unreal-
asset als een paar (`.uasset` + `.uexp`):

| Verzameling | Aantal |
|---|---|
| in de pak onder `HodZero_v1/ADX/` | **2.874** (1.437 + 1.437) |
| los op schijf onder `Content/Hodzero_V1/ADX/` | **2.014** (1.890 `.uasset` + 123 `.usm` + 1 `.acf`) |
| **los, en nergens in de pak** | **577** (453 `.uasset` + 123 `.usm` + 1 `.acf`) |

Die 577 bestanden bestaan dus maar op één plek: **de 123 videobestanden en 453 assets komen niet uit de
pak**. De grootste film is `adver_prime.usm` (167.611.104 B). De engine vraagt die map overigens met een
**hoofdletter Z** (`…/Hodzero/Content/HodZero_v1/Fonts/…`) terwijl de map op schijf `Hodzero_V1` heet;
dat werkt hier omdat de netwerkmount hoofdletterongevoelig is. Zie §9.

### De rol van de daemon (**GEMETEN**)

Dit spel heeft **twee programma's**. `amdaemon.exe` is Sega's AM Daemon, en hij moet **vóór** het spel
draaien: het profiel van het commerciële hulpmiddel zegt het zelf
(`HasTwoExecutables = true`, `LaunchSecondExecutableFirst = true`, `ExecutableName2 = amdaemon.exe`), en
de eigen startketen van het spel wacht er vijf seconden op (§3).

De daemon is de **platformlaag**: hij levert de diensten waar het spel om vraagt — de invoer van het
I/O-bord, de munten, de boekhouding, het bestandssysteem dat het spel als schijf letters ziet, en de
netwerklaag. Het spel is zijn **cliënt**: het spelbestand bevat de clientkant van die koppeling
(`amdaemon::ipc::` komt **506×** voor in de spel-exe, met `ClientRequest` 131×, `DaemonShared`,
`DaemonSharedPart` en `makeDaemonShared`), en die koppeling loopt over **gedeeld geheugen**, niet over
een poort en niet over een named pipe. De daemon heeft de serverkant (`amdaemon::server::`, 378 namen)
plus de dertien mutex-namen die de client niet heeft.

### De eigen configuratie van het spel (**GEMETEN**)

`Hodzero/Saved/Config/WindowsNoEditor/Game.ini` is een echte Unreal-config met een eigen sectie:

```ini
[/Script/Hodzero.AppGameInstance]
DevelopSetting_PlayerGunDevice=0
DebugAMBackup=false
DebugAMCredit=false
DebugAMCreditFreePlay=false
DebugWithoutAMDaemon=false
DevelopSetting_DspEnemyAIWorkingStatus=true
bIsKoreanROMSetting=false
```

Interessant voor wie dit spel wil aansturen: er zit een **schakelaar om zonder de daemon te draaien**
in (`DebugWithoutAMDaemon`), naast een vrij-spel-schakelaar (`DebugAMCreditFreePlay`). De officiële
startketen laat de game deze configuratie ook uit een **gebruikersmap** lezen (`-UserDir=…`), wat een
nette manier is om zulke waarden per spel te zetten zonder de spelbestanden aan te raken **GEMETEN**
(de keten doet het; dat de game die map ook echt gebruikt is **AFGELEID** uit diezelfde keten).

### De hardware-eis doet niets (**GEMETEN**)

`pxGetHwInfo.ini` eist `platform-id ACA0`, `mem-size 16384`, `appdata-size 17000` en
`graphics-board-lane-width 16`, en `pxGetHwinfo.exe` (701.440 B) staat in de spelmap om dat te
controleren. **Niets in de startketen start dat programma**, en het spel is op een machine met andere
waarden gemeten spelend. De eis is in deze kopie dus inert.

## 3. Het officiële startscript en de opstart

**Wij voeren deze scripts nooit uit.** Onze lader **leest** ze en neemt alleen over wat nodig is. Dat is
geen formaliteit: deze keten doet kastbrede dingen — het register van Sega aanpassen, de resolutie van het
bureaublad zetten, en bij elke start `rmdir /S /Q "Y:\"`. Onze lader rapporteert die opdrachten expliciet
als *"kastding (daarom niet uitvoeren)"* en voert ze niet uit **GEMETEN**.

De keten is `game.bat` → `App/Startup/AppStartup.bat` → `05_SetVideoPort.bat`,
`10_SetResolution.bat`, `20_SetAudioVolume.bat`, `99_StartApp.bat` → `App/Hodzero/StartApp.bat`:

| Stap | Wat hij doet |
|---|---|
| `game.bat` | zet `LAUNCH_ON_IMAGE=1` en `IMAGE_ROOT_DIR=X:\`, en start `AppStartup.bat` |
| `AppStartup.bat` | roept de vier volgende scripts in volgorde aan |
| `05_SetVideoPort.bat` | schrijft vijf registerwaarden (`port_1..port_5` = `DVI0, DP0, DP1, DP2, HDMI0`) |
| `10_SetResolution.bat` | `..\Tools\SetResolution.exe 1920 1080 0` |
| `20_SetAudioVolume.bat` | `..\Tools\SetAudioVolume.exe 85 75` |
| `99_StartApp.bat` | `GAME_ID=SDET`, `APP_DIR=Y:\SDET`, `APP_LOG_DIR=Y:\SDET\current_log`; maakt `Y:\SDET` aan, wist `Y:\` als die map ontbreekt, en gaat naar `App/Hodzero` |
| `App/Hodzero/StartApp.bat` | kiest de nieuwste van vier spel-exe's (Shipping eerst), zet `DAEMON_WAIT_SECONDS=5`, start de daemon, wacht, start het spel, wacht, en ruimt op |

De relevante regels uit `StartApp.bat`, letterlijk **GEMETEN** (de bestanden zijn Shift-JIS, de sleutels
zijn ASCII):

```
set DAEMON_WAIT_SECONDS=5
start /min "" "%DAEMON_EXE_PATH%" -c %DAEMON_CONFIG_PATH% -l "%DAEMON_LOG_PATH%"
timeout %DAEMON_WAIT_SECONDS%
start /wait "" "%APP_EXE_PATH%" %APP_EXE_ARGS%
timeout /t 1
taskkill /im "%DAEMON_EXE_FILE_NAME%"
```

met `%DAEMON_CONFIG_PATH% = WindowsNoEditor\AMDaemon\config.json`,
`%APP_EXE_ARGS% = -launch="cabinet" -ABSLOG=… -UserDir=… -NotInstalled -UNATTENDED`, en de werkmap die
de keten opbouwt is `App/Hodzero` (`99_StartApp.bat` doet `pushd ..\Hodzero`).

**De `-UNATTENDED`-vondst.** Een van die argumenten is `-UNATTENDED`, en in het script staat als
commentaar `エラーウィンドウ出さない` — "het foutvenster niet tonen". Dat is niet cosmetisch. In een van de
gemeten sessies sloeg het spel op **74 seconden** na de start fataal af; in het logboek staat de fout
**zonder melding** (`Fatal error!` met een lege regel), daarna probeert Unreal zijn crashrapporteerder te
starten (dat bestand wordt niet meegeleverd), en dan zit er **zestig seconden lang geen enkele regel**
tussen de laatste normale logregel en het afsluiten. Zonder `-UNATTENDED` blijft Unreal bij een fatale
fout op een modaal venster staan voordat het afsluit **GEMETEN** voor het gat van 60 s en het
commentaar; **AFGELEID** voor het mechanisme.

Wij nemen daarom van deze keten over: **de volgorde** (daemon, 5 seconden, spel), **`-UNATTENDED`**, en
niets anders. De argumenten zijn overigens geen voorwaarde om te spelen: het commerciële hulpmiddel geeft
het spel **geen enkel argument** mee en het spel is zo gemeten spelend **GEMETEN**.

## 4. De route die wij gebruiken

Onze lader is `scripts/launch-direct.py` uit de project-repo. Hij leest het spelprofiel plus
`patches/<Game>/game.ini`, bouwt daar een **plan** van en kan dat plan uitvoeren — zonder het
startscript van het spel uit te voeren **GEMETEN**.

```sh
# het plan, zonder iets te starten:
python3 scripts/launch-direct.py HOTDSD --plan
# en de uitvoering:
python3 scripts/launch-direct.py HOTDSD --run
```

Het plan bevat per start de volledige opdrachtregel. Voor dit spel zijn het er **twee**, en dat is
meteen het antwoord op de vraag hoe je een spel met een los hulpprogramma start:

```
start 1/2: amdaemon.exe (tweede exe)
  wine-prefix …/OpenParrotLoader64.exe 'Z:\…\TeknoParrot64' 'Z:\…\AMDaemon\amdaemon.exe'
wachten: 5 s; daarna alleen verder als elke eerdere start nog leeft
start 2/2: Hodzero-Win64-Shipping.exe (het spel)
  wine-prefix …/OpenParrotLoader64.exe 'Z:\…\TeknoParrot64' 'Z:\…\Hodzero-Win64-Shipping.exe'
```

**De argumenten.** Voor de daemon: **geen** (de officiële keten geeft `-c` en `-l` mee, wij niet — het
plan zegt letterlijk *"argumenten voor amdaemon.exe: geen"*). Voor het spel: ook **geen**. Wat wij wél
overnemen is de werkmap-afhandeling: de lader zet de werkmap van het doel op de map van het doel, niet op
die van Wine **GEMETEN**.

**De omgevingsvariabelen** die de lader zet **GEMETEN**:

| Variabele | Waarde | Waarom |
|---|---|---|
| `WINEPREFIX` | `~/.local/share/ravengun/prefixes/shared/wine` | de gedeelde prefix |
| `RG_UNSET_REMOTETHREAD` | `1` | zie hieronder — **de belangrijkste** |
| `OPENSSL_ia32cap` | `:~0x20000000` | projectinstelling voor deze reeks titels |
| `WINEDEBUG` | `-all` | geen ruis in het logboek |
| `TP_LAUNCH_SESSION_ID` | een UUID per run | om de uitvoer van één run te kunnen terugvinden |

**De `TP_REMOTETHREAD`-valkuil.** Het commerciële hulpmiddel zet onder Wine zelf `TP_REMOTETHREAD`, en
dan kiest de lader de *remote-thread*-injectieroute, die hier faalt met *"Failed to find kernel32.dll in
target process"*. Onze wrapper haalt die variabele daarom weg (`RG_UNSET_REMOTETHREAD=1`), waarna de
lader zijn standaardroute neemt: de **entry-point-trampoline**, die zich in het logboek meldt als
`Resolved target kernel32.dll procedure: local=… remote=… rva=0xF81C` gevolgd door `Have fun :)`.
**Dat is de route die werkt** **GEMETEN**.

**Wat de lader identificeert.** Elk logboek begint met het gereedschap dat de run deed, zodat een run met
de verkeerde versie niet meer te verwarren is met een goede **GEMETEN**:

```
launcher: /home/deck/projects/ravengun/scripts/launch-direct.py
  de versie uit de REPO (commit 5cdfaa8; scripts/ gelijk aan die commit);
  sha256 327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0;
  hulpmodules (5) sha256 c882ade50b66ad0cd20260050983968dfc629aba9033c1e5233d92f5f364d656
  de geinstalleerde launcher (…/opt/ravengun/share/ravengun/scripts/launch-direct.py) is dezelfde
kern: …/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll
  sha256 f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28, versie 1.0.0.3761
```

**De view en de NAS.** De lader start het spel uit een **view**:
`~/.local/share/ravengun/runtime/views/HOTDSD`, met daarbinnen dezelfde boom als het spel. Staat die view
vol **symlinks naar de speltmap**, dan schrijven de daemon en de lader dwars door die symlinks heen in de
speltmap op de NAS. Wil je dat niet — en wij willen dat niet — maak dan van alles wat geschreven wordt een
**echte kopie** en houd alleen de alleen-lezen bulk (de map `Content` van 12 GB en `Engine`) als symlink.
Dat is precies wat de lader zelf ook adviseert als een doel op de NAS ligt: *"…ligt op de NAS; niet
geschreven. Maak eerst een lokale view"* **GEMETEN**.

## 5. De daemon en de platformlaag — het hart van deze pagina

Dit is waar het misgaat, en het is de moeite waard om precies te zijn.

**Start je `amdaemon.exe` alleen, dan sterft hij na één seconde.** Hij print eerst nog zijn banner en
sterft dan in de **platformlaag**, vóór alles wat met de schijven, de invoer of de sleutelchip te maken
heeft. De letterlijke regel uit zijn eigen logboek **GEMETEN**:

```
<< 2026/10/13 12:13:02 >>
Runtime exception occurred.
File: D:\jenkins_work\workspace\all_build\libs\libamw\src\amw_platform_manager.cpp
Line: 102
Function: void __cdecl am::platform::Manager::initialize(void)
Message: amPlatformGetPlatformInformation(). ErrCode 0.
```

De afsluitcode is **0** en de wandtijd **1 seconde**: hij beëindigt zichzelf netjes na het loggen van de
uitzondering. De banner die er nog wel staat is
`amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37` **GEMETEN**.

**Welke lagen hij nooit heeft aangeraakt.** Met een bestandstrace (`WINEDEBUG=+file`, een logkanaal en
geen API-tracer) van **1,5 MB** per run geteld **GEMETEN**:

| Laag | Aantal in de trace |
|---|---|
| het AM-bestandssysteem dat het spel als schijf ziet (`AMFS`) | **0×** |
| het app-databestand (`appfile`, normaal 8.388.608 B) | **0×** |
| twee lagen aan de beveiligingskant van het platform | **0×** — de details daarvan staan bewust niet in deze pagina |
| grafische bibliotheken (`nvapi`, `dxgi`, `d3d11`, `vulkan`) | **0×** |
| apparaatopsomming via `setupapi` / `cfgmgr32` | **45×** en **29×** |

Hij sterft dus in de **laatste** van die regels: de platformcontrole vraagt iets op over de apparatuur
van de kast, en op een gewone pc vindt hij dat niet. De functie die faalt hoort bij de systeem- en
platformmodule van het AM-framework; in het bestand staan onder meer `amPlatformGetMemorySize`,
`amPlatformGetGraphicInfo`, `amPlatformGetDevInfo`, `amPlatformIsAvailableSubStorage` en
`amSysDataGetDotClockTiming` **GEMETEN**. Er staan **geen apparaat-interface-GUID's** in het bestand, dus
wat hij precies opzoekt wordt niet door een vaste letterlijke GUID bepaald.

**Wat de A/B beslist.** We hebben de daemon in zes opstellingen gezet, elk met **één** variabele verschil
ten opzichte van de vorige. De vier lagen uit de tabel hierboven zijn per arm geteld:

| Arm | Wat hij wijzigt | Daemon-levensduur | Bewijs | `AMFS` | `appfile` | beveiligingslagen |
|---|---|---|---|---|---|---|
| A1 | de volledige route: lader + kern + **ook het spel** | **≥95 s** (mijn limiet); spelproces **88 s** | de daemon print `Process state: setup Game Mode`, `Error 0`, **`Process state: Advertise`** | niet te tellen¹ | niet te tellen¹ | niet te tellen¹ |
| A2 | t.o.v. A1: het spel wordt **niet** gestart | **≥30 s**, nog levend | **`AMFS` 95×, `appfile` 11×** — hij komt voorbij de platformfase | **95×** | **11×** | 0× |
| A3 | t.o.v. A2: **een andere kern** | **≥30 s**, nog levend | `AMFS` 0× — hij overleeft, maar vordert niet | 0× | 0× | 0× |
| A4 | t.o.v. A2/A3: **de lader weglaten** (direct starten) | **~1 s**, sterft zelf | zoals boven | 0× | 0× | 0× |
| A5 | t.o.v. A4: **de argumenten** toevoegen (`-c`, `-l`) | **~1 s** | dezelfde platformfout, nu in het logboek | 0× | 0× | 0× |
| A6 | t.o.v. A5: **de vorm van de map** (kale kopie) | **~1 s** | dezelfde platformfout | 0× | 0× | 0× |

¹ De launcher zet `WINEDEBUG=-all`, dus voor A1 is er geen trace en zijn de aantallen *niet te tellen* —
dat is iets anders dan "niet aangeraakt".

**Het antwoord, en het is niet het antwoord dat we verwachtten.** De vraag was of de gesloten kern van het
commerciële hulpmiddel die platformcontrole bevredigt. Er blijken **twee** voorwaarden te zijn:

1. **De injectie zelf haalt de dodelijke afloop eruit.** Met de lader en *welke DLL dan ook* geïnjecteerd
   leeft de daemon **≥30 s** (arm A3, met een volledig andere kern). Rechtstreeks gestart sterft hij na
   1 s. Dat was niet voorspeld en het staat in geen enkele eerdere notitie.
2. **Alleen met de eigenlijke kern komt hij ook echt verder.** Arm A2 raakt met de echte kern de
   mount- en app-datalaag (**95×** en **11×**); arm A3 met de andere kern raakt die **0×**. Omdat de
   platformcontrole in `Manager::initialize()` loopt **vóór** de mountfase, bewijst het aanraken van die
   twee lagen dat de daemon de platformstap voorbij is **GEMETEN** voor de aantallen; **AFGELEID** voor de
   causale lezing (de andere kern zou ook alleen de uitzonderingsafhandeling kunnen veranderen, waardoor
   de daemon blijft hangen in plaats van te sterven — die twee zijn met deze gegevens niet te scheiden).

**De sleutelchip-laag is in geen enkele arm aangeraakt** (`0×` in alle traces), ook niet in de arm die de
mountfase haalde. Wat daar ook precies gevraagd wordt: het gebeurt **ná** de platformstap.

## 6. Timing en sessies

**GEMETEN, vandaag (2026-10-13):** de daemon leeft in de launcherroute **≥95 s** (afgebroken door mijn
eigen limiet, afsluitcode `-15`), het spelproces **88 s** (12:11:12 → 12:12:40). Rechtstreeks gestart:
**1 s**. Met de lader maar zonder de eigenlijke kern: **≥30 s** maar zonder enige voortgang.

**GEMETEN, uit de logboeken van 2026-10-11 — zes sessies op één dag:**

| # | open | dicht | duur | hoe het eindigde |
|---|---|---|---|---|
| 1 | 08:41:39 | 08:42:12 | 33 s | gestopt (geen crashblok, geen `Log file closed`) |
| 2 | 08:44:21 | 08:44:47 | 26 s | gestopt |
| 3 | 08:46:27 | 08:46:54 | 27 s | gestopt |
| 4 | 08:47:52 | 08:51:56 | **4 min 4 s** | gestopt |
| 5 | 08:52:40 | 08:54:54 | 2 min 14 s | **crash**, 74 s na de start |
| 6 | 08:55:41 | 09:02:44 | **7 min 3 s** | gestopt |

Dat vijf van de zes geen crashblok hebben is te controleren: hun logboek eindigt midden in de
boekhouding zonder `Log file closed`, en het logboek van de lader stopt op hetzelfde moment. De crash van
sessie 5 valt **exact** op het moment dat het eerste level klaar met laden was (74 s na de start,
`SecondsSinceStart 74` in het crashrapport, en de laatste normale logregel is diezelfde seconde). Het
geheugen was op dat moment **niet** het probleem: **22.759 MB vrij van 31.705 MB**, en het proces gebruikte
1,19 GB. Er is een **minidump van 297.524 B** bewaard. Het is bovendien **niet reproduceerbaar op dat
punt**: sessie 6 laadde hetzelfde level en liep daarna nog **105 seconden** door.

**Wat een meting niet zegt.** Dit zegt dat het spel start, speelt en boekhoudt — niet dat elk beeld goed
is.

## 7. Wat we op het scherm zagen

**Van mijn eigen runs: niets.** Ik heb headless gedraaid (`DISPLAY` en `WAYLAND_DISPLAY` niet gezet), dus
er was geen beeldserver en er kan niets gerenderd zijn. Dat is zichtbaar in het logboek: de sessie van
vandaag stopt na **0,8 s** bij `LogInit: Using OS detected locale`, **vóór** elke `LogD3D11RHI`-regel en
vóór het mounten van de plugins, terwijl op 10-11 `LogD3D11RHI: Aftermath initialized` er al vroeg in
stond. Of dat komt doordat mijn `SIGTERM` het gebufferde logboek heeft weggegooid of doordat het spel in de
initialisatie bleef steken, **kan ik niet scheiden** — dus ik claim geen schermwaarneming **GEMETEN**.

**Wat de logboeken van 10-11 wél laten zien** (dus uit de bestanden, niet van een scherm) **GEMETEN**:

* `Match State Changed from WaitingToStart to InProgress` — het spel komt in de speeltoestand;
* `Took 5.198148 seconds to LoadMap(/Game/HodZero_v1/Maps/Stage0/PL_Stage0)`, en in een andere sessie
  **1,98 s** voor dezelfde map;
* alle sublevels van dat level worden geactiveerd (`SL_Stage0_ITEM`, `…_BROKEN_OBJECT`, `…_COLLISION`,
  `…_ENEMY`, `…_BG`, `…_LIGHT`, `…_EFFECT`, `…_VOLUME`);
* de plugins mounten: `AMDaemon`, `CriWare`, `ABaaSGS`, `EmitMan`, `StateMan`, `TaskMan`, `WidgetMan`,
  `ApexDestruction`, `Paper2D`, `LowEntryExtendedStandardLibrary`;
* de kalibratiewaarden van speler 2 worden uitgeschreven: `P2_CenterX : 474`, `P2_CenterY : 622`,
  `P2_RightX : 890`, `P2_BottomY : 979`, `P2_LeftX : 42`, `P2_TopY : 235`;
* de eigen boekhouding loopt: `UBookkeeping` met `P1 GameTime`, `AddDamageLogCount`,
  `AddFirstDeathLogCount`, `AddSecondDeathLogCount`, en ranglijstregels;
* en het spel meldt één eigen fout, tweemaal in de zevenminutensessie en één keer per sessie, telkens op
  **dezelfde regel in het logboek (regel 416)**, bij het binnengaan van het inlogscherm:
  `LogAMDaemon: Error: Exception caused. (Category:Logic Msg:`aimeId` is invalid.)`. **Het spel gaat er
  direct daarna gewoon door** (`UPlayLog::StartPlayLog PlayCount=1`, dan verder). Dit hoort bij de
  kaartlezer, die in deze kopie ontbreekt; het spel kent een gast-pad naast het kaart-pad **GEMETEN**.

**De lat.** Wij meten een titel als "gehaald" bij **vijf minuten attract- of spelmodus**; een laadscherm of
het beginscherm telt niet. **Die lat is hier niet gehaald en ook niet gemeten** — mijn run was headless en
duurde 88 s.

## 8. Stand van zaken en open vragen

**Stand: het spel start, de daemon bereikt in de volledige route `Process state: Advertise`, en op
10-11 is het spel tot in het eerste level gekomen met boekhouding en kalibratie. Onze eigen route haalt de
lat nog niet.**

| # | Open punt | Volgende stap |
|---|---|---|
| 1 | **Geen schermwaarneming.** Mijn run was headless en het spel logt na 0,8 s niets meer. | Een run **met** een beeldserver (gamescope of de Xwayland van de eigenaar), langere limiet, en **netjes stoppen** in plaats van killen — anders is het logboek weer afgekapt. |
| 2 | **Welke lader?** De route van vandaag gebruikt een lader van 1.227.264 B (`…/openparrot/current/…`, sha256 `1953d91a…`), de runs van 10-11 gebruikten die van 333.824 B (`…/runtime/TeknoParrotUi-2.0.0.130-linux-x64/OpenParrotx64/…`, sha256 `ad8528b2…`). De kern is wel dezelfde (`f5c7532d…`, 1.0.0.3761). | De twee laders met dezelfde opstelling vergelijken; dit is de eerste kandidaat voor "waarom komt het spel van vandaag niet verder dan 0,8 s terwijl het op 10-11 het eerste level haalde" — maar het kan net zo goed punt 1 zijn. |
| 3 | **De twee voorwaarden van de platformcontrole** (injectie én kern) zijn niet tot één oorzaak te herleiden. | Een instrument in de kern dat laat zien welke aanroep de platformcontrole precies doet. |
| 4 | **De beveiligingslaag is nooit bereikt**, in geen enkele arm. | Niet aan de orde zolang het spel niet voorbij de platformstap komt. Details staan bewust niet in deze pagina. |
| 5 | **De mountletters** (`X:`, `Y:`, `D:`) worden door ons niet geleverd. De officiële keten zet ze klaar met `rmdir`/`mkdir` op `Y:\` en wij voeren dat niet uit. | Uitzoeken welke letters de daemon in onze route opzoekt; op 10-11 deed hij dat wél (zijn app-databestand werd geschreven). |
| 6 | **De startmeting** (`patches/STARTTEST.md`) heeft voor dit spel nog geen rij: het kon niet gepland worden zolang de lader geen twee exe's startte. | Dat kan nu wél (§4). Een run met `starttest3.sh HOTDSD` levert de rij. |
| 7 | **`DebugWithoutAMDaemon`** staat op `false` in de configuratie van het spel zelf. | Of het spel met die schakelaar op `true` zonder daemon speelt, is niet getest; zie §2. |

## 9. Wat Linux-specifiek is (en wat je op Windows anders doet)

Wij draaien dit spel op **SteamOS (Arch-gebaseerd) onder plain Wine**, in een gedeelde Wine-prefix, met de
spelbestanden op een netwerkmount. Dat vraagt een aantal dingen die op Windows niet spelen.

**GEMETEN:**

* **Wine-versie en prefix.** `wine-11.0` (een Proton-build) met
  `WINEPREFIX=~/.local/share/ravengun/prefixes/shared/wine`. Die prefix bevat **598 DLL's in
  `system32`** en **631 in `syswow64`**; alle 44 bibliotheken die het spel en de daemon importeren zijn
  daar aanwezig. Zonder die prefix (bijvoorbeeld een verse, lege prefix) mis je er naar verwachting een
  aantal, met name `msvcp110`/`msvcr110` (de daemon importeert die twee) en `vcruntime140`/`msvcp140`.
* **`TP_REMOTETHREAD` moet weg.** De remote-thread-injectieroute faalt onder Wine
  (*"Failed to find kernel32.dll in target process"*); met de variabele verwijderd neemt de lader de
  entry-point-trampoline en werkt het wél. Dit is puur Wine-specifiek.
* **De daemon draait zonder beeldscherm.** Hij is een **console**-programma (subsystem 3) en start met
  `DISPLAY` ongezet; hij print zijn banner en gaat dood aan de platformcontrole. Voor het **spel** is dat
  anders: dat is een D3D11-titel en heeft een beeldserver nodig.
* **Hoofdlettergevoeligheid.** De netwerkmount (CIFS) is hoofdletterongevoelig, waardoor
  `HodZero_v1` (de spelling in de pak en in de engine) en `Hodzero_V1` (de spelling op schijf) op
  dezelfde map uitkomen. **Op een hoofdlettergevoelig bestandssysteem (ext4) kan dat breken**, en dan
  specifiek voor de **577 losse bestanden** uit §2 die nergens in de pak zitten. Dit is nu geen probleem —
  het spel heeft gespeeld — maar het is een echt risico zodra iemand de bestanden naar een lokale schijf
  kopieert.
* **Rechten en de NAS.** De netwerkmount is voor de gebruiker **schrijfbaar**. Onze afspraak is dat wij er
  alleen lezen, en dat is dus een afspraak en geen bestandseigenschap. Wie een spel uit zo'n map wil
  starten, moet er rekening mee houden dat de daemon en de lader hun eigen bestanden naast de exe
  schrijven: de daemon zijn app-databestand van 8,4 MB en de lader een `teknoparrot.ini`. Wij vangen dat op
  met een view van **echte kopieën** plus symlinks voor de alleen-lezen bulk van 12 GB (§4).
* **Snelheid en limieten.** Een koude Wine-prefix kost de eerste seconden; onze armen met een harde
  limiet lieten de daemon daardoor soms niet eens beginnen. Een limiet van 90 s per start is ruim genoeg
  voor de daemon (hij sterft of slaagt binnen 2 s) maar niet voor het spel.

**AFGELEID, en op Windows anders:**

* **Schijfletters.** Wij leveren de letters `X:`, `Y:` en `D:` niet; de officiële keten zet ze klaar met
  `rmdir`/`mkdir` en een image-root van `X:\`. Op Windows is de voor de hand liggende route `subst` of een
  echte partitie; onder Wine zou het een `dosdevices`-koppeling of een haak in de lader moeten zijn. Wij
  hebben dit **niet** gebouwd en niet getest.
* **De resolutie en het volume.** `SetResolution.exe 1920 1080 0` en `SetAudioVolume.exe 85 75` zijn
  Windows-programma's uit de spelmap. Wij voeren ze niet uit; onder Wine is de beeldserver
  (gamescope/Xwayland) degene die de resolutie bepaalt.
* **Wat je op Windows waarschijnlijk het eenvoudigst doet:** de spel-exe's met een eigen lader starten,
  de daemon eerst met vijf seconden wachttijd, `-UNATTENDED` meegeven aan het spel, en de letters via
  `subst` klaarzetten. Dat is **niet door ons getest** — het is de vertaling van wat wij hier hebben
  geleerd.

### Herkomst en grenzen van dit verslag

Alles hier is op één machine gemeten: een **Steam Deck (SteamOS)** met een AMD RX 6950 XT, plain Wine,
en de spelbestanden op een NAS die alleen gelezen is. De sectie-5-metingen komen uit twee runs op
**2026-10-13** (12:08–12:20) en de sessiegeschiedenis uit logboeken van **2026-10-11**. Waar een meting
iets op de NAS heeft geschreven, staat dat in het projectdossier en niet in deze pagina; in deze pagina
is alleen het spel en de lader beschreven. De cijfers, paden en regelnummers zijn letterlijk uit de
bestanden overgenomen; elke **AFGELEID**-markering wijst op precies dat stuk waar wij alleen indirect
bewijs hebben.
