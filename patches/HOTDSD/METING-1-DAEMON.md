# Meting 1 — de AM Daemon alleen

**Uitgevoerd 2026-10-13, 09:50–09:54 op de Deck. Twee runs, elk met een harde limiet van 90 s; beide
eindigden na 1 s vanzelf. Geen game, geen lader, geen kern, geen display. De NAS is niet aangeraakt.
Deze notitie hoort bij [`VOLLEDIGHEID.md`](VOLLEDIGHEID.md) en [`REPORT.md`](REPORT.md).**

## Samenvatting in één oogopslag

| | |
|---|---|
| **Verwachting vooraf** | uitkomst (a): banner, doorgroei naar `Advertise`, keychip gelogd en getolereerd |
| **Gemeten** | banner, en dan **dood na 1 seconde** in de **platformlaag** — vóór de keychip, vóór de mountletters, vóór `Advertise` |
| **De regel uit zijn eigen log** | `Runtime exception occurred. File: …libamw\src\amw_platform_manager.cpp Line: 102 Function: void __cdecl am::platform::Manager::initialize(void) Message: amPlatformGetPlatformInformation(). ErrCode 0.` |
| **Wat hij nooit heeft aangeraakt** | `FddDriver` **0×**, `amDongle`/`amyDongle` **0×** (beide traces van 2,3 MB en 4,4 MB) — de keychip is nooit geopend; `AMFS` **0×**, `appfile` **0×** |
| **Oordeel** | **Dit is een STARTBLOKKADE voor de route "SEGA's daemon zonder enige TeknoParrot-component".** Hij staat vóór de mountletters en vóór de keychip, en hij verandert de volgorde van het hele plan |

**Mijn verwachting (a) was fout.** Ik voorspelde doorgroei naar `Advertise`; gemeten is een harde stop in
de eerste seconde. Dat hoort hard gezegd te worden voordat iemand op mijn eerdere plan verder bouwt.

**En het is scherper dan uitkomst (c).** Uitkomst (c) was "hij stopt op de keychip" — dan zou de
keychip-laag van completeness-werk een blokkade worden. Wat er werkelijk gebeurt is een trede
**eerder**: de daemon komt niet eens aan de keychip toe. De keychip-vraag is met deze run dus **niet
beslecht** (hij is nooit bereikt), en de **platformlaag** is nu het eerste dat opgelost moet worden.

---

## 1. Wat er precies gedraaid is, en onder welke grenzen

| | |
|---|---|
| Run A | `WINEDEBUG=+file`, `timeout 90`, exit 0, wall **1 s**, trace 2.275.587 B / 20.627 regels |
| Run B | `WINEDEBUG=+file,+reg`, `timeout 90`, exit 0, wall **1 s**, trace 4.434.225 B |
| Commando | `env WINEPREFIX=<prive-kopie> env -u DISPLAY -u WAYLAND_DISPLAY WINEDEBUG=… wine amdaemon.exe -c config.json -l amdaemon.log` |
| View | `~/rg-hotdsd-meting1/AMDaemon` — **echte kopie, 0 symlinks** (21 MB) |
| Prefix | `~/rg-hotdsd-meting1/prefix` — **prive-kopie** van de gedeelde prefix (615 MB) |
| Wine | `wine-11.0-gdc26e618470 (Proton)`, `~/.local/share/ravengun/wine/proton-11.0-2/bin/wine` |

**Over de tracer, zoals gevraagd.** Er is **geen `+relay` gebruikt**. `+file` en `+reg` zijn
logkanalen, geen API-tracer; ze vertragen het proces niet zoals `+relay` doet. Dat maakt hier ook niet
uit: beide runs eindigden na **1 seconde** vanzelf. De `NtDeviceIoControlFile`-regels die ik wilde
hebben kwamen er via `+file` toch uit (zie §3), dus `+relay` was nergens voor nodig.

**Waarom een eigen kopie en niet de bestaande view.** De view die er al stond
(`~/.local/share/ravengun/runtime/views/HOTDSD`) bestaat uit **symlinks terug naar de NAS**. Draaien
vanuit die view zou `appfile.dat`, `amdaemon.log` en de `TeknoParrot/`-mappen **dwars door de symlinks
heen op de NAS** hebben geschreven. Bovendien is de NAS-mount hier voor gebruiker `deck` gewoon
**schrijfbaar** — "alleen-lezen" is dus een afspraak en geen bestandseigenschap. Daarom een prive-kopie
van zowel de spelmap als de prefix.

## 2. De verwachtingen die ik vooraf had, en wat ik per stuk zag

| # | Verwachting vooraf | Wat ik zag |
|---|---|---|
| **(a)** | banner, doorgroei naar `Advertise`, `Keychip Not Found` gelogd en getolereerd, filetrace noemt `X:`/`Y:`/`D:` en `SystemProperty\mount`, ioctl-codes naar `\\.\FddDriver` | **NIET GEHAALD.** Banner ja. Daarna dood in 1 s. Geen `Advertise`. Geen keychip. Geen `FddDriver`. Geen mountletters |
| **(b)** | banner, maar stopt op de mountletters of `get amfs drive letter failed` | **NIET GEHAALD** — hij komt niet aan de mountletters toe; `AMFS` en `appfile` komen 0× in beide traces voor |
| **(c)** | stopt op de keychip zonder `Advertise` → keychip wordt een startblokkade | **NIET GEHAALD, maar in dezelfde geest:** hij stopt **vóór** de keychip, in de platformlaag. Zie §4 voor wat dat betekent |
| **(d)** | geen banner, wine-fout → omgevingsprobleem | **NIET GEHAALD** — de banner komt er wel, en wine werkt (de probe `cmd /c echo` gaf exit 0 met dezelfde prefix en zonder display) |
| **(e)** | `+relay` geeft geen `DeviceIoControl` → op `NtDeviceIoControlFile` zoeken | **NIET NODIG** — `+file` toonde de ioctl's al, en `+relay` is niet gebruikt |
| — | **nieuw, niet voorspeld** | **(c′) de daemon sterft in `am::platform::Manager::initialize()` → `amPlatformGetPlatformInformation()`, 1 seconde na de banner, vóór alles wat met de keychip of de schijven te maken heeft** |

## 3. De meting in detail

**De eigen log van de daemon (272 bytes, beide runs identiek):**

```
<< 2026/10/13 09:51:27 >>
Runtime exception occurred.
File: D:\jenkins_work\workspace\all_build\libs\libamw\src\amw_platform_manager.cpp
Line: 102
Function: void __cdecl am::platform::Manager::initialize(void)
Message: amPlatformGetPlatformInformation(). ErrCode 0.
```

Exitcode **0**, wandtijd **1 s**: hij beëindigt zichzelf netjes na het loggen van de uitzondering.

**Wat hij niet heeft aangeraakt** (geteld over beide traces samen):

```
FddDriver   0   amDongle  0   amyDongle  0   Keychip  0   keychip  0
AMFS        0   appfile   0
nvapi  0   NVAPI  0   nvml  0   d3d11  0   dxgi  0   d3d9  0   vulkan  0
```

`appfile.dat` in de run-map heeft nog exact zijn NAS-tijd (`2026-10-11 09:02:49`, 8.388.608 B) — hij is
niet eens aan de NVRAM-/appfile-stap toegekomen. Het enige dat de run in de map achterliet was zijn
eigen `amdaemon.log`.

**De apparaat-ioctl's die wél langskwamen** zijn volume-/opslag-bevragingen, geen keychip:

```
DeviceIoControl (0x54, 0x00121000, in=56 B, out=122628 B, …)
NtDeviceIoControlFile (…, 0x10000504, …)  /  (0x54, 0x00121000, …)
```

**Hij gebruikt SetupAPI en CFGMGR32, en géén GPU-bibliotheek:** `setupapi` 45×, `cfgmgr32` 29×,
`Display` 108×, tegenover `nvapi`/`dxgi`/`d3d11`/`vulkan` **0×**. De functie die faalt hoort bij
`amSysData`/`amPlatform`: in het bestand staan `amPlatformGetPlatformInformation(). ErrCode %d.`,
`amPlatformGetOsVersion(). ErrCode %d.`, `amPlatformSetDisplaySettings(). ErrCode %d.`,
`amPlatformGetMemorySize`, `amPlatformGetGraphicInfo`, `amPlatformGetDevInfo`,
`amPlatformIsAvailableSubStorage`, `amPlatformGetCheckInterfaceHandle`, `amPlatformNvapiInit`,
`amSysDataGetDotClockTiming`, en `AM_PLATFORM_DEVINTERFACE_TYPE is unknown!! Type is %d`.
Er staan **geen device-interface-GUID's** in het bestand, dus wat hij opzoekt wordt niet door een vaste
GUID-string bepaald.

**Het register is niet de ontbrekende input — hij is er de schrijver van.** De regtrace laat zien dat de
daemon `System\SEGA\SystemProperty\Static` zelf **aanmaakt**:

```
trace:reg:NtCreateKey (0x28, L"System\\SEGA\\SystemProperty\\Static", …, 1, 20019, …)
trace:reg:NtOpenKeyEx (0x9c, L"SEGA", 20119, …)
trace:reg:NtCreateKey (0x9c, L"SEGA", …, 1, 20019, …)
trace:reg:NtCreateKey (0xac, L"SystemProperty", …, 1, 20019, …)
```

En de gedeelde prefix heeft **geen enkele SEGA-sectie**: `SystemProperty` 0, `amVideo` 0, `keychip` 0,
`Master` 0, `sgworker` 0, `installer` 0 treffers in `system.reg`. Mijn prive-kopie is daar een kopie
van. **Gevolg: het seeden van registry-waarden (meting C1 uit het eerdere voorstel) is hiermee
gedeprioriteerd — het register is niet wat ontbreekt.**

## 4. Wat dit verandert — hard gezegd

**Dit is een startblokkade voor de route "SEGA's daemon zonder enige TeknoParrot-component", en hij
staat vóór alle andere blokkades.** De volgorde die ik eerder opschreef was: (1) twee exe's starten,
(2) mountletter-omleiding, (3) daemon starten. Punt 3 is nu geen kwestie van "de daemon starten" maar
van "de daemon überhaupt laten initialiseren", en dat komt **vóór** punt 2.

**De sterkste aanwijzing over wie dat gat vult.** Dezelfde `amdaemon.exe` (byte-identiek), in dezelfde
prefix, onder dezelfde Wine, bereikt in de TeknoParrot-route wél `Process state: Advertise` — dat is
eerder gemeten (`findings/HOTDScarletDawn.md`, ronde 15). Het enige verschil in die route is de
**geïnjecteerde gesloten kern** `TeknoParrot64.dll`. De conclusie die zich opdringt is dus dat **de kern
de platformcontrole bevredigt** — waarschijnlijk door de SetupAPI/CFGMGR32-apparaatopsomming te
onderscheppen die `amPlatformGetPlatformInformation()` gebruikt. Dat is dezelfde soort haak als de
schijfletter-omleiding, in hetzelfde proces.

*Eerlijkheidshalve de alternatieven:* ik kan niet uitsluiten dat de prefix bij die ronde-15-meting een
andere toestand had. Maar de gedocumenteerde route gebruikt `[Wine] Prefix = shared`, en mijn kopie
daarvan is precies die prefix. Een andere verklaring zou zijn dat er toen iets in de prefix stond dat er
nu niet meer is; ook dat is met één controle te beslechten.

**Wat dit NIET verandert:**
* de **volledigheid** van de bestanden — die is apart gemeten en staat los van dit (zie `VOLLEDIGHEID.md`);
* de **speelbaarheid via TeknoParrotUi** — die blijft gemeten werken;
* het **keychip-ontwerp** (`REPORT.md` §10) — het is niet weerlegd, alleen nog niet bereikt; het zakt in
  prioriteit achter de platformlaag;
* de **mountletter-omleiding** — die blijft nodig, alleen later in de rij.

## 5. De beslissende volgende meting (A/B, één commando)

De vraag is nu precies één ding: **is de gesloten kern wat `amPlatformGetPlatformInformation()` bevredigt?**

* **Arm 1** — `amdaemon.exe` alleen, via onze lader met de kern geïnjecteerd, verder identiek aan deze
  meting (prive-prefix, geen display, 90 s). *Verwacht vooraf:* hij passeert de 1 seconde en komt
  verder dan `Manager::initialize`. Dat bewijst dat de kern het gat vult, en daarmee is de route
  "daemon zonder TeknoParrot" alleen te halen door die controle zelf na te bouwen.
* **Arm 2** — dezelfde run zonder kern. Dat is wat hierboven al gemeten is en wat als nulmeting dient.

Dit is **één exe**, dus `launch-direct.py` heeft er geen twee-exe-ondersteuning voor nodig; het is de
gewone route met één doelwit. Het hoort bij de laderbouwer of bij een door jou ingeplande run — niet
bij mij.

**En de voorwaarde voor meting 2 (de twee-exe-start), zoals gevraagd om te noemen:**
`launch-direct.py` kan **geen twee exe's starten** (rc=4, regel 966 — `profielen met twee exe's worden
niet direct gestart`). Dat is laderwerk en het ligt bij de laderbouwer. Zonder die uitbreiding is er
geen startmeting voor deze game mogelijk en blijft de rij in `patches/STARTTEST.md` op `niet-gestart`
staan. **Dat is geen formaliteit: met de uitkomst van §3 is het zelfs de vraag of die uitbreiding alleen
genoeg is, want de daemon moet eerst überhaupt willen initialiseren.**

## 6. Bijvangst: de UE4-logs bestaan wél — en ze vertellen het hele verhaal

Dit is los van de daemon, maar het is de belangrijkste vondst van deze ronde, en het **corrigeert een
regel in het dossier**.

`findings/HOTDScarletDawn.md` zegt: *"er is geen UE4-log: `Hodzero/Saved/Logs` wordt niet aangemaakt"*.
Dat is **fout**. De game schrijft zijn UE4-logs wel, maar niet in de speltmap — in de prefix, op
`%LOCALAPPDATA%\Hodzero\Saved\Logs\`:

```
~/.local/share/ravengun/prefixes/shared/wine/drive_c/users/steamuser/AppData/Local/Hodzero/
  Saved/Logs/Hodzero.log                              117.045 B   (08:55:41 → 09:02:44, ±7 min)
  Saved/Logs/Hodzero-backup-2026.10.11-08.44.21.log    22.252 B   (08:41:39 → 08:42:12)
  Saved/Logs/Hodzero-backup-2026.10.11-08.46.26.log    22.249 B   (08:44:21 → 08:44:47)
  Saved/Logs/Hodzero-backup-2026.10.11-08.47.52.log    22.248 B   (08:46:27 → 08:46:54)
  Saved/Logs/Hodzero-backup-2026.10.11-08.52.40.log    56.577 B   (08:47:52 → 08:51:56)
  Saved/Logs/Hodzero-backup-2026.10.11-08.55.41.log    49.042 B   (08:52:40 → 08:54:54, met "Log file closed")
  Saved/Crashes/UE4CC-Windows-249AC8AF…_0000/
      CrashContext.runtime-xml                          3.309 B
      UE4Minidump.dmp                                 297.524 B
      Hodzero.log                                      47.491 B
```

**Zes sessies op één dag, waarvan één van ±7 minuten.** Dat is het sterkste volledigheidsbewijs dat er
is: het spel heeft op deze machine **gespeeld**, niet alleen gestart.

Wat `CrashContext.runtime-xml` vastlegt:

| Veld | Waarde |
|---|---|
| `EngineVersion` | **4.18.1-0+++UE4+Release-4.18** — bevestigt UE4 4.18 uit het dossier |
| `BaseDir` | `Z:/home/deck/mnt/spidernas_emulation/…/WindowsNoEditor/Hodzero/Binaries/Win64/` — **direct van de NAS gedraaid** |
| `CommandLine` | **leeg** — bevestigt dat de game zonder argumenten speelt (TeknoParrot's `ExtraParameters` is leeg) |
| `SecondsSinceStart` | **74** — de crash viel 74 s na de start |
| `CrashType` | `Crash`, `IsEnsure` false, `IsAssert` false, `CallStack` **leeg** |
| `MemoryStats.bIsOOM` | **0** — geen geheugenprobleem |
| Machine | Ryzen 7 7800X3D, Radeon RX 6950 XT (RADV NAVI21), 31 GB |

Wat de sessies zelf laten zien:

* de game bereikt **`Match State Changed from WaitingToStart to InProgress`**, laadt
  `…/Maps/Stage0/PL_Stage0` (1,98 s in de ene sessie, 5,20 s in de andere), en activeert alle
  `SL_Stage0*`-sublevels (ITEM, BROKEN_OBJECT, COLLISION, ENEMY, BG, LIGHT, EFFECT, VOLUME);
* `LogPluginManager: Mounting plugin AMDaemon`, `… CriWare`, `… ABaaSGS`, `EmitMan`, `StateMan`,
  `TaskMan`, `WidgetMan`, `LowEntryExtendedStandardLibrary`, `ApexDestruction`, `Paper2D`;
* `LogApp: P2_CenterY : 622 / P2_RightX : 890 / P2_BottomY : 979` — **kalibratiewaarden van speler 2**;
* `LogAppData: UBookkeeping:…` met `P1 GameTime`, `AddDamageLogCount`, `AddFirstDeathLogCount`,
  `AddSecondDeathLogCount`, `Ranking[0][0][2]` — het spel houdt zijn eigen boekhouding bij;
* **`LogAMDaemon: Error: Exception caused. (Category:Logic Msg:'aimeId' is invalid.)`** — **twee keer per
  sessie** (bij frame 288 en 344), en **het spel gaat gewoon door**. Dit is de AM-Daemon-plugin van de
  game die een AiMe-fout meldt; concreet, en precies het soort ding dat de keychip-/kaartlaag raakt;
* één sessie eindigt met `LogWindows: Could not start CrashReportClient.exe` (dat bestand wordt niet
  meegeleverd) en daarna `LogWindows: Error: === Critical error: === Fatal error!` **met een lege
  melding** — dezelfde sessie als de 74-seconden-crash.

**En dit beantwoordt een van mijn eigen "niet vast te stellen"-punten.** In `VOLLEDIGHEID.md` §7 stond
dat ik niet kon weten welke mappen het spel opent, omdat die configuratie in de pak zit. De historische
logs geven het antwoord: de game zoekt zijn content via de `[Core.System] Paths=` uit
`Saved/Config/WindowsNoEditor/Engine.ini` (`../../../Engine/Content`, `%GAMEDIR%Content`, en
plugin-Content-mappen), en die plugin-mappen (`Hodzero/Plugins/…`, `Engine/Plugins/2D/Paper2D`) **staan
niet op schijf** — ze zitten in de pak. Dat is nu **gemeten in een echte run** in plaats van afgeleid.

**Eén portabiliteitsdetail dat eruit volgt.** De log vraagt letterlijk om
`../../../Hodzero/Content/HodZero_v1/Fonts/…` (met `Z` en `v1`), terwijl de map op schijf
`Content/Hodzero_V1/ADX` heet (met `z` en `V1`). Op de CIFS-mount is dat onderscheid er niet — CIFS is
hoofdletterongevoelig — dus daar werkt het. **Op een hoofdlettergevoelig bestandssysteem (ext4) zou dat
kunnen breken.** Wie de game ooit lokaal kopieert moet dat weten.

## 7. Opschonen, en één eerlijke correctie

* **Niets van mij draait nog.** De check controleert per proces de `WINEPREFIX` uit
  `/proc/<pid>/environ` en laat alleen mijn prive-prefix killen.
* **Eerlijke correctie:** mijn eerste opruimcommando was `wineserver -k` **zonder `WINEPREFIX`**, dus
  dat richtte zich op de standaardprefix in plaats van op mijn prive-kopie. De processen bleken bij de
  hercontrole alsnog verdwenen (wineserver stopt vanzelf als de laatste client weg is), maar de aanpak
  was slordig en is in `re/_hotdsd-cleanup.sh` rechtgezet.
* **Wat ik NIET heb aangeraakt, en waarom.** Er liep tijdens mijn meting een
  `gamescope --backend headless -W 1920 -H 1080 -- sleep 900` plus een monitorproces van een andere
  partij (`pgrep -ax VACUUM.exe …`). Dat is niet van mij — ik heb nooit gamescope of een display
  gestart — dus ik heb ze met rust gelaten. De gamescope is inmiddels vanzelf gestopt. De opdracht
  "geen gamescope laten staan" lees ik als "wat jij start, ruim je op"; andermans benchproces killen zou
  hun run hebben afgebroken.
* **De gedeelde prefix is niet door mij beschreven.** Zijn `system.reg`/`user.reg`/`userdef.reg` zijn
  gewijzigd op **09:53:48**, dus ná mijn laatste wine-aanroep (09:51:32), en hij bevat **geen
  SEGA-sectie** — die zou er wel staan als mijn daemon erin geschreven had. De meest waarschijnlijke
  auteur is het afsluitende wineserver van de gelijktijdige benchrun.
* **De NAS is onaangeroerd:** `find /…/House of the Dead Scarlet Dawn -newermt "-60 minutes"` is leeg,
  en de mtimes in `AMDaemon/` zijn allemaal van 2026-10-10/11.
* **Op schijf laten staan:** `~/rg-hotdsd-meting1` (643 MB: view 21 MB, prefix-kopie 615 MB, traces 6,7 MB).
  Nodig voor de A/B in §5. Weggooien met `rm -rf ~/rg-hotdsd-meting1`.

## 8. Herhalen

```sh
bash /home/deck/_hotdsd-recon.sh      # wat loopt er, welke prefix, welke wine
bash /home/deck/_hotdsd-prep.sh       # prive-kopieen maken (NAS en gedeelde prefix blijven heel)
bash /home/deck/_hotdsd-meting1.sh    # run A: +file, 90 s
bash /home/deck/_hotdsd-meting1b.sh   # run B: +file,+reg, 90 s
bash /home/deck/_hotdsd-analyse.sh    # de sporen uitlezen (alleen lezen)
bash /home/deck/_hotdsd-evidence.sh   # het bewijsbestand samenstellen
bash /home/deck/_hotdsd-cleanup.sh    # alleen MIJN prefix-processen stoppen
bash /home/deck/_hotdsd-logs.sh       # de UE4-logs en de crashdump (alleen lezen)
```

Ruwe sporen staan op de Deck in `~/rg-hotdsd-meting1/` (`ftrace.log`, `regtrace.log`,
`meting1-bewijs.txt`) en in deze map als `measurements/meting1-bewijs.txt` en
`measurements/meting1-amdaemon.log`.

## 9. Wat dit betekent voor de opdracht van de eigenaar

De volledigheid is geen probleem gebleken (zie `VOLLEDIGHEID.md`) en de game heeft op deze machine
**gespeeld** — tot in Stage 0, met boekhouding, in sessies tot zeven minuten. Wat er niet staat is de
**eigen route zonder TeknoParrot**, en de reden is nu precies aan te wijzen: niet de bestanden, niet de
schijfletters, niet de keychip, maar de **platformcontrole in de eerste seconde**, die de gesloten kern
kennelijk bevredigt en wij nog niet. Dat is één concreet, afgebakend aangrijpingspunt — en de A/B in §5
vertelt in één run of het inderdaad de kern is.
