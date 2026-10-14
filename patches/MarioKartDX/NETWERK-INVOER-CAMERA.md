# Mario Kart Arcade GP DX — start, invoer, netwerk en camera op onze eigen route

**Datum:** 2026-10-14 · **Stand:** voorbereid en gepland (`rg-direct --plan MarioKartDX` = ok), **nog niet gestart** (de bench
was bezet). Alles hieronder is gelezen of statisch gemeten; wat pas op de tv blijkt staat onder "Open".

## 1. Welke versie

De spelmap op de NAS heet `Mario Kart DX`. Het is **1.18.16**: `Data/DongleData` en `Data/Phys` zijn er en
`AMCUS/WritableConfig.ini` niet; dat is de indeling van 1.18.16 in `findings/mkdx_research.md` (MEASURED: `ls`).
`MK_AGP3_FINAL.exe` is 7.432.704 bytes (sha1 `65aee49c…de55`). Het profiel volgt daarom het sjabloon `MKDX118.xml`
(revisie 12, met het veld `Experimental Webcam Hack`), met de hand overgenomen omdat het sjabloon anders heet dan deze map.

## 2. De start (rg-direct, geen TeknoParrotUi, geen script)

Dezelfde keten als TeknoParrot: onze OpenParrot-lader (`[Loader] Build = ravengun` uit `_defaults/TeknoParrot.ini`) met de
gesloten 32-bits kern `TeknoParrot/TeknoParrot.dll` en `MK_AGP3_FINAL.exe`, in gamescope, gedeelde prefix, plain Wine.
TeknoParrotUi doet voor `NamcoMkdx` alleen één extra stap: oude `MK_AGP3_FINAL`-processen beëindigen
(`GameLaunchArguments.cs`, `ApplyPerGamePreLaunch`; READ). Onze start draait in een eigen systemd-unit die bij elke stop alles
opruimt, dus `NamcoMkdx` staat niet meer op de lijst "niet direct te starten" (`direct.cpp`, `launch-direct.py`).

De game draait uit de **view** `~/.local/share/ravengun/runtime/views/MarioKartDX/` (symlinks naar de NAS; `teknoparrot.ini`
en de drop-in komen lokaal). De game schrijft zelf naar `F:\income`, `F:\playdata` en `G:\Contents` (strings in de exe); de kern
leidt schijfletters om naar een lokale map. Niets komt op de NAS.

## 3. Invoer: JVS zoals TeknoParrot

TeknoParrotUi maakt voor `NamcoMkdx` een **JVS-bord** (`JvsSetup.cs`: ID `NBGI.;NA-JV;Ver6.01;JPN,MK3100-1-NA-APR0-A01`,
versies 0x31, `Namco = true`, 0x18 schakelaars) en zet de Banapass-kaart in het gedeelde geheugen (`PipeFactory.cs`:
`BanapassButton`, bit 0 op offset 8). Wij doen hetzelfde:

- `[Input] Route = jvs`, `Board = MKDX`, `JvsTransport = pipe`: de game praat JVS over `\\.\pipe\TeknoParrot_JVS`, rg-direct
  koppelt die aan de pty van de daemon, de daemon **is** het bord (`packaging/jvs-boards.json`, bord `MKDX`, `namco: true`).
- Nieuw in de JVS-kern: de Namco-commando's (`0x70 <sub>`, `0x78..0x80`, onbekende bytes overslaan) — zonder die stopt een
  gewoon bord met status 2 bij het eerste Namco-commando.
- Analoog: kanaal 0 = stuur (`p1.wheel`), 1 = gas (`p1.gas`), 2 = rem (`p1.brake`).
- Knoppen (TeknoParrot-binding → bit → ons signaal): Item `P1Button5` → byte 1 bit 5 → `trigger`; Mario-knop
  `ExtensionOne2` → byte 2 bit 1 → `reload`; Enter `P1Button1` → byte 0 bit 1 → `grenade`; kaart `P1Button2` → byte 0 bit 0 →
  `aux1`; Menu Up/Down → bit 5/4 → `btn1`/`btn2`; Start, Service, Test, munt 1/2.
- `Layout = MKDX` (`tp-shm-profiles.json`): de kaartbit op offset 8 in het gedeelde geheugen.
- Gamepad (`[Gamepad]`): linkerstick = stuur, RT = gas, LT = rem, A = Item, B = Mario-knop, X = Enter, Y = kaart,
  LB/RB en d-pad op/neer = Menu Up/Down, Start, Back = munt, RS = Test (F2).

## 4. Netwerk: ALL.Net/AMCUS zoals TeknoParrot

TeknoParrotUi start **geen** `AMAuthd.exe` en geen `muchacd` en heeft geen ALL.Net-code; het enige wat het voor dit spel aan
netwerk doet is de velden `Network/Gateway`, `ServerAddress`, `ServerPort` en `PlayerId` in `teknoparrot.ini` zetten (READ:
`MKDX118.xml`, `GameLaunchArguments.cs`). De **gesloten kern** beantwoordt de ALL.Net-authenticatie in het spelproces
(INFERRED: dezelfde kern doet dat bij de andere Namco-titels in-proces, `docs/research/antwoorden-tp-ronde5-22.md`; de kern
is afgeschermd en laat geen strings zien). Wij schrijven dus exact dezelfde `teknoparrot.ini` (MEASURED: `--plan`):

```
[Network]
Gateway=192.168.1.1
ServerAddress=164.132.246.154
ServerPort=49200
PlayerId=
```

`ServerAddress`/`ServerPort` zijn de gemeenschapsserver van TeknoParrot (online spelen); `PlayerId` leeg = geen online-ID
(TeknoParrotUi vult hem uit `ParrotData.xml` `MarioKartId`, en die is leeg). Wie geen verbinding met die server wil, zet in de
gebruikerslaag `Network/ServerAddress = 127.0.0.1`; of het spel dan nog offline doorstart is NIET GEMETEN.

## 5. Camera: Media Foundation, gevoed door de daemon

Het spel importeert `MF.dll!MFEnumDeviceSources`, `MFReadWrite.dll!MFCreateSourceReaderFromMediaSource` en `MFPlat`
(MEASURED: `objdump -p`), heeft een `IMFSourceReaderCallback` en kent `YUY2` en `NV12`: **Media Foundation, geen DirectShow**.
Wine (proton-11.0-2) heeft daar geen camera's: `MFEnumDeviceSources` zet bij `VIDCAP`
(`{8ac3587a-4ae7-42d8-99e0-0a6013eef90f}`) `*count = 0` en geeft S_OK, `MFCreateDeviceSource` is een stub (MEASURED:
disassemblage van `i386-windows/mf.dll`). Een V4L2-camera aan Wine geven helpt hier dus niet.

Daarom:

- `[Camera] Use = game`: start de IO-brug deze game, dan schrijft de daemon het beeld van **zijn** camera (de PS Eye, 640x480
  YUY2; de tracking loopt door) in `/dev/shm/ravengun_Camera`. Stopt de route, dan is het bestand weg.
- `dropin/rg-mfcam.dll` (`Preload = true`, de lader laadt hem via `TP_PRELOAD_DLL` na de kern) zet `MFEnumDeviceSources` in de
  importtabel van het spel om. Is het bestand er, dan krijgt het spel één camera ("RavenGun camera", YUY2/NV12/RGB32, 30 fps);
  anders gaat de vraag naar het origineel (dan is alles zoals zonder drop-in).
- `Experimental Webcam Hack` (de kern van TeknoParrot) blijft 0; wat die doet is onbekend.

Let op: de PS Eye staat ingesteld voor het volgen van de bol (vaste, korte belichting). De foto wordt dus donker; belichting en gain
staan in de camera-instellingen van de app (IPC `set_camera`). `ravengunctl --json camera` toont `"game_feed": true` zolang de
daemon het beeld voor het spel schrijft. Een tweede camera (niet die van de daemon) wordt nog niet doorgegeven.

## 5b. Stopt na 27 s (bench 2026-10-14 20:10 en 20:13) en de fix

**Gemeten** (logboeken `direct-MarioKartDX-20261014-201029.log` en `-201317.log`): de lader meldt "Have fun :)", gamescope ziet
`MK_AGP3_FINAL.exe` een swapchain maken, en ongeveer 26 s na de start is alles weg ("Primary child shut down", lader exitcode 0). De
JVS-pipe is nooit geopend. Het spel schreef vlak voor het einde zijn back-upbestanden (`Fsystem/`, `FBookkeeping/`, `FTestMode/`,
`FDongleData/` om 20:10:56, `hints.dat` om 20:13:42; einde 20:10:58 en 20:13:44).

**Wat de exitcode 0 waard is: niets.** Elke start in gamescope eindigt in de logboeken met "lader gestopt (exitcode 0)" (tien
gamescope-runs, allemaal 0); zonder gamescope geeft dezelfde lader wel 5 of 1 door (GG, ActionDeka, BlockKing). Gamescope geeft de
code van het spel dus niet door: "stil met 0" kan net zo goed een crash zijn (MEASURED: de logboeken).

**Waar het spel stopt (READ, disassemblage van `MK_AGP3_FINAL.exe`).** De JVS-poort is `COM3` (CreateFileA op 0x8ac1f2) en die
wordt alleen geopend in de eerste stap van `clBootCheck` (0x602310 -> 0x8ab880). `clBootCheck` start pas als `clSequenceBoot` de
eerste laders klaar ziet (0x75eaf0). Geen pipe = het spel kwam niet tot die eerste stap: het stopte tijdens of direct na het laden
op de hoofdthread. Alle stille uitgangen met code 0 in de exe (`exit(0)` na "＜自動的に再起動します＞" in `clTestModeMenuGame`,
`ExitWindowsEx(EWX_REBOOT|EWX_FORCE)` in stap 11 van `clBootCheck`, `PostQuitMessage(0)` in de DXUT-vensterprocedure) liggen
achter die stap of vragen een gesloten venster; de DXUT-foutpaden zetten een exitcode ongelijk aan 0.

**Wat TeknoParrot zelf voor deze game onder Wine doet (READ, TeknoParrotUI-bron in `~/.cache/ravengun/src/TeknoParrotUI`):**
1. `AndroidLaunchRecipes/MKDX.json` en `MKDXUSA106.json`: `compatibilityPreset = parked-entrypoint` (van de 154 recepten alleen
   deze twee), in de Winlator-brug `TP_ENTRYPOINT_REMOTETHREAD_MS=3000` en `TP_LOADER_MANAGED_INIT=1`.
2. `Tools/InputMethodAudit/AndroidManagedImportTest.cs` r. 1496-1517: "Mario Kart DX patches an exact-name sixteen MiB stack
   copy": een kopie van `MK_AGP3_FINAL.exe` met dezelfde naam en `requiredStackReserve = 16 MiB`. De exe zelf heeft 1 MiB
   (`SizeOfStackReserve 00100000`, MEASURED met `objdump -p`), en onder Wine is dat precies de stapel van de hoofdthread.
3. TeknoParrotUi op de desktop (Linux) zet altijd `TP_REMOTETHREAD=1` (`Proton/ProtonLauncher.cs`); voor `NamcoMkdx` verder alleen
   oude `MK_AGP3_FINAL`-processen beëindigen. Geen extra argumenten, register, schijfletters of AMCUS-proces.

Onze route deed geen van die drie: de standaardroute van de lader (`RemoteThread = off`) en de exe met 1 MiB stapel.
**Conclusie (INFERRED, niet gemeten):** de hoofdthread loopt tijdens het laden tegen zijn stapel van 1 MiB aan en het proces sterft;
gamescope maakt daar "exitcode 0" van. Bewijs nog nodig: de benchproef hieronder.

**De fix (in deze map, 2026-10-14):**
- `[Launch] StackReserve = 16M` (nieuwe engine-sleutel, `docs/per-game-patches.md`): de engine maakt van `MK_AGP3_FINAL.exe` in de
  view een lokale kopie met 16 MiB stapel; de koppeling naar de NAS blijft als `MK_AGP3_FINAL.exe.ravengun-orig` staan. Vier bytes
  in de DOS-stub houden de CRC32 van de eerste 0x400 bytes gelijk (0x1ae07e00, ook genormaliseerd 0x6c9038be): daaraan herkent de
  OpenParrot-kern een game (`GameDetect.cpp`), dus de kern ziet dezelfde exe. Op de NAS verandert niets.
- `[Loader] EntryPointMs = 3000` en `[Env] TP_LOADER_MANAGED_INIT = 1`: de parked-entrypoint-route van TeknoParrot voor deze game.

Proeven: zie het rapport van 2026-10-14 (coördinator); de bewijsregels staan hieronder bij "Open".

## 5c. #UD op 0x8C753D met de parked-entrypoint-route (bench 2026-10-14 21:00) en de fix

**Gemeten** (`WINEDEBUG=-all,+debugstr,+seh,err+all`, zonder gamescope): de hoofdthread `0110` van `MK_AGP3_FINAL.exe` krijgt
`EXCEPTION_ILLEGAL_INSTRUCTION` (c000001d) op `0x008C753D`, onafgehandeld; winedbg, lader exitcode 222. Registers: `eax=ebx=003f1000`
(de PEB), `edx=008c6e3a` (het startpunt), `esi=edi=ecx=0`. In het logboek staat de uitzondering tussen de `SINGLE_STEP`-regels van de
remote thread `0164` die de gesloten kern laadt (handler `71A29764` in `teknoparrot`) en `Loading requested compatibility DLL`: het
spel stierf terwijl de kern nog in DllMain zat, vóór rg-mfcam en vóór de trampoline van de lader.

**Wat er op 0x8C753D staat (READ, objdump):** het startpunt is `8c6e3a: e8 b4 06 00 00 call 0x8c74f3` (`__security_init_cookie`),
dan `e9 6b fd ff ff jmp 0x8c6baf`. `0x8C753D` ligt **midden in** `8c753c: 33 f0 xor esi,eax`; vanaf `0x8C753D` leest de CPU
`f0 ff 15 b0 30 94 00` = `lock call [GetCurrentThreadId]`, en een lock-prefix voor een call is altijd #UD. Geen bewuste `ud2`,
geen AVX-512/SSE4a, geen versleutelde code: het spel is in een instructie gesprongen. De registers zeggen ook waar vandaan: `esi`
en `edi` zijn nog 0, dus `__security_init_cookie` heeft geen enkele instructie gedraaid (`8c750a mov edi,0xbb40e64e`); de thread
kwam rechtstreeks van het startpunt.

**De oorzaak (INFERRED, sterk: de rekensom klopt op de byte).** De lader parkeert het startpunt met `EB FE` (spinlus) en laadt de
kern na 3 s in een **remote thread**, terwijl de hoofdthread op die spinlus blijft draaien. De gesloten kern 1.0.0.3761 haakt in
DllMain zelf het startpunt, zoals `Main_SetSafeInit` in de OpenParrot-bron: `ep[0] = 0xE9; memcpy(&ep[1], &rel32, 4);`, byte voor
byte (de kern let dus niet op `TP_LOADER_MANAGED_INIT`; onze eigen kern wel, `dllmain.cpp`). Tussen die twee schrijfacties ziet de
draaiende thread `E9 FE 06 00 00` = `jmp 0x8C6E3F + 0x6FE` = **`0x8C753D`** (en `E8 FE 06 00 00` zou op hetzelfde adres uitkomen).
De `SINGLE_STEP`-uitzonderingen van de beschermde kern maken dat venster microseconden breed. Zonder die race was het ook mis gegaan:
de kern bewaart `EB FE ...` als "originele" code en zet die na zijn init terug, waarna de thread voorgoed spint (tot de lader de
trampoline schrijft, die `InitializeASI` dan een tweede keer aanroept).

**Niet de oorzaak (MEASURED, `cmp -l` origineel tegen kopie):** de StackReserve-kopie verschilt in precies 6 bytes: 4 in de DOS-stub
(bestandsoffset 0x78-0x7B, CRC32-correctie van de kop) en 2 in `SizeOfStackReserve` (0x182-0x183, 0x00100000 -> 0x01000000). Geen
sectie, relocatie, import of code anders; het PE-veld `CheckSum` (0x00726326) is ongewijzigd. Op het startpunt staan in beide
bestanden dezelfde bytes.

**TeknoParrot (READ, TeknoParrotUI-bron):** de preset `parked-entrypoint` staat alleen in de Android-recepten `MKDX.json` en
`MKDXUSA106.json` (Box64, waar een JIT-blok de spinlus anders uitvoert). Voor **MKDX118** is er geen Android-recept; TeknoParrotUi op
de desktop start met `TP_REMOTETHREAD=1` (`ProtonLauncher.cs`), op Windows met de standaardroute. Geen andere exe, geen IPS of
per-game patch; wel de 16 MiB-stapelkopie (Android).

**De fix:**
- `game.ini`: `EntryPointMs` en `[Env] TP_LOADER_MANAGED_INIT` zijn weg; de game draait op de standaardroute van de lader
  (`RemoteThread = off`, `_defaults/TeknoParrot.ini`) met de 16 MiB-stapel. Daar laadt de hoofdthread zelf de kern (geen tweede thread
  die tegelijk op het startpunt schrijft) en haakt de kern het startpunt over de echte bytes.
- Lader (RavenGun-patch `third_party/openparrot/patches/0008-parked-entry-hold-suspended.patch`): de parked-route zet de hoofdthread na
  de wachttijd stil (`SuspendThread`, controle EIP == startpunt), zet de originele 20 bytes terug en laadt **dan** de kern; met
  `TP_LOADER_MANAGED_INIT` slaat hij de eigen trampoline over als de kern het startpunt zelf al gehaakt heeft. Nieuwe logregels:
  `Primary thread suspended at the entry point; original entry bytes restored before loading the core.` en
  `Core hooked the entry point itself (E9 .. .. .. ..); skipping the loader-managed initializer.`

## 5d. Toegangsfout op 0x835E60 op de standaardroute (bench 2026-10-14 23:24) en de fix

**Gemeten** (`WINEDEBUG=-all,+seh,err+all`, zonder gamescope, standaardroute, 16 MiB stapel): geen `c000001d` meer. De kern laadt,
rg-mfcam laadt, "Have fun :)", en het spel draait zijn eigen code: 13 `OutputDebugString`-meldingen (de laatste twee zijn dezelfde
regel, wide en ANSI) (`*INF* Game Start`,
`*WRN* ShareKSaveErrorInfo load failed.` + `file reset.`, `ContentsInfoCl/OV/Md/Sv: ver:16102602 ...`, `ContentsVer_Client:0`,
`ContentsVer_Server:0`, `ContentsDisableFlag:1`, `ContentsVer_Client_Old:0`, en `プレイデータ保存フォルダ作成：./Fplaydata/1812/`),
dan wined3d die een shader bouwt (de D3D-device bestaat), dan `EXCEPTION_ACCESS_VIOLATION` op `0x00835E60`, lezen van adres 0,
`eax=0`. De handlers van het spel (`_except_handler4`, filter `0x8C6D30` = `_XcptFilter`) laten hem door: onafgehandeld.
De `SINGLE_STEP`-regels vóór "Have fun" zijn de beschermde kern zelf (handler `71A29764` in `teknoparrot`): normaal.

**Wat er op 0x835E60 staat (READ, objdump):** functie `0x835E20` (de 2D-polygoonrenderer, `xelib`): als de D3D10-device (`[0xEABAA8]`)
er is, `call 0x838940` met `ecx = "Polygon2D_D3D10.fx"` en `"fx_4_0"`; de uitkomst gaat naar `[esi+0x4E20]` en meteen
`835e60: mov ecx,[eax]` (de vtable, voor `GetTechniqueByName("Render")`). Geen NULL-controle. `0x838940` zet het pad ervoor
(`0x866E40`, `Data/System/Shader/`) en roept `D3DX10CreateEffectFromFileA(pad, NULL, NULL, "fx_4_0", 0x800 (0x802 bij .cfx), 0, device,
NULL, NULL, &effect, &errors, &hr)` aan (import `0x94377C`); bij `hr < 0` geeft hij NULL terug. De foutmeldingen (`errors`) leest het
spel nooit.

**Waarom alleen deze (READ):** het spel laadt 24 effecten via `0x838940`; 23 zijn voorgecompileerd (`PostEffect.cfx`, `Bloom.cfx`,
`PositionColor3D_D3D10.cfx`, ...), alleen `Polygon2D_D3D10.fx` is HLSL-bron. Op Windows compileert de native `d3dx10_43` die bron.
Wine's `d3dx10_43` (proton-11.0-2) roept in `create_effect` (0x10009720) `D3DCompile` van `d3dcompiler_47` (vkd3d-shader) aan,
**behalve** als het blok met `DXBC` begint: dan gaat het rechtstreeks naar `D3D10CreateEffectFromMemory` (READ: disassemblage, de
vergelijking op 0x10009820). De bron gebruikt wat een effectcompiler zwaar valt: shaderarrays met een variabele index
(`SetPixelShader(ps[g_texture_type])`, een preshader-expressie), `uniform`-parameters in `CompileShader(ps_4_0, PS(1))`,
samplerarrays met initialisatie, `technique10` en Shift-JIS in commentaar. **INFERRED** dat vkd3d-shader dit niet compileert (zelfde
soort fout als Medaru No Gunman, fx_2_0: "Writing fx_2_0 sampler objects initializers is not implemented"); niet gemeten welke
regel het is. Wine's `d3d10` kent wel indexexpressies in een gecompileerd effect (`Failed to evaluate index expression`, preshader).

**Niet de oorzaak:** de camera (0x835E60 is code van het spel, rg-mfcam staat op 0x711A0000; `rg-mfcam.log` heeft in geen enkele run
een regel `MFEnumDeviceSources #1`, dus het spel heeft nog geen camera gevraagd; MEASURED), AMCUS/ALL.Net, de dongle, de F:-paden, de stuurwielopsomming: de fout ligt in de opbouw van de 2D-renderer.
De JVS-pipe ontbreekt omdat `clBootCheck` (COM3) pas na deze opbouw komt.

**De fix (`game.ini`):** `[Launch] ViewLinks = Data/System/Shader/Polygon2D_D3D10.fx -> Data/System/Shader/Polygon2D_D3D10.cfx`
(nieuwe engine-sleutel, `docs/per-game-patches.md`). In de view worden `Data`, `Data/System` en `Data/System/Shader` echte mappen met
symlinks en wordt `Polygon2D_D3D10.fx` een symlink naar de `.cfx` op de NAS. Die `.cfx` is hetzelfde effect, gecompileerd met
fxc 9.29.952.3111 (de compiler van `d3dx10_43`), met `Render`, `RenderQuad`, `ChainLine`, `g_texture`, `g_textureCube`,
`g_texture3D`, `g_texture_type`, `g_animation`, `cb1`, `ps`, `ps2` (MEASURED: `strings`). Niets gekopieerd, niets op de NAS.
Logregel: `viewlink: .../Data/System/Shader/Polygon2D_D3D10.fx -> .../Polygon2D_D3D10.cfx (alleen in de view)`.

**Terugvaloptie** als Wine's `d3d10` het gecompileerde effect weigert: de native `d3dx10_43.dll` + `d3dcompiler_43.dll` uit de
DirectX-herdistribueerbare (Jun2010) in een eigen prefix, `native,builtin`, zoals bij Medaru No Gunman. Dat is een prefixwijziging en
een Microsoft-bestand: eerst aan de eigenaar vragen.

## 6. Open (alleen op de tv te zien)

0. Komt het spel met de fix hierboven langer dan 27 s en opent het de JVS-pipe? Bewijsregels in het logboek, in deze volgorde:
   `stapel: ... MK_AGP3_FINAL.exe is al actueel (SizeOfStackReserve 16 MiB (16777216) ...)` (of `geschreven: ...`), géén
   `Using parked-entry-point ...`, `Have fun :)`, `JVS-pipe: game verbonden (pipehelper via TCP)`, en het spel draait langer dan
   5 minuten. Geen `c000001d` in een `+seh`-logboek.

1. Start de kern de game en komt hij voorbij het ALL.Net-scherm (netwerk)?
2. Richting en bereik van het stuur (kanaal 0, `invert`), en of gas/rem 0..255 het hele pedaal is.
3. Pakt het spel de camera, en welk formaat kiest het (`rg-mfcam.log`)?
4. Laadt het spel zijn eigen `bngrw.dll` (de echte kaartlezerbibliotheek van Namco, niet die van OpenParrot) zonder fout?
