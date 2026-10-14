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

## 6. Open (alleen op de tv te zien)

0. Komt het spel met de fix hierboven langer dan 27 s en opent het de JVS-pipe? Bewijsregels in het logboek, in deze volgorde:
   `stapel: ... geschreven: lokale kopie van ... SizeOfStackReserve 1 MiB (1048576) -> 16 MiB (16777216), CRC32 van de kop gelijk
   (1ae07e00)`, `Using parked-entry-point RemoteThread injection method (3000 ms)...`, `Primary-thread x86 OpenParrot initializer
   installed at ...`, `JVS-pipe: game verbonden (pipehelper via TCP)`, en het spel draait langer dan 5 minuten.

1. Start de kern de game en komt hij voorbij het ALL.Net-scherm (netwerk)?
2. Richting en bereik van het stuur (kanaal 0, `invert`), en of gas/rem 0..255 het hele pedaal is.
3. Pakt het spel de camera, en welk formaat kiest het (`rg-mfcam.log`)?
4. Laadt het spel zijn eigen `bngrw.dll` (de echte kaartlezerbibliotheek van Namco, niet die van OpenParrot) zonder fout?
