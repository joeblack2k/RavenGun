# De zes sessies, de crash, en de `aimeId`-fout

**Alleen gelezen. Geen bench gebruikt, geen spel gestart, de NAS onaangeroerd.** Dit hoort bij
[`METING-1-DAEMON.md`](METING-1-DAEMON.md) (de daemonmeting) en [`VOLLEDIGHEID.md`](VOLLEDIGHEID.md)
(de volledigheid). Bron: de UE4-logs en de crashmap in de gedeelde Wine-prefix, plus de eigen
launcher-logs van de bench. Ruwe uitvoer: `measurements/sessies-analyse.txt`,
`measurements/aimeid-pak-probe.txt`, `measurements/adx-split-probe.txt`.

## Samenvatting in één oogopslag

| Vraag | Antwoord |
|---|---|
| Hoeveel sessies, en hoe liepen ze af? | **Zes sessies op 2026-10-11.** **Eén** heeft een crash achtergelaten (sessie 5). **Vijf** eindigen zonder crashblok én zonder `Log file closed`, en hun launcher-log stopt op hetzelfde moment → **gestopt, niet gecrasht** |
| Waar crashte sessie 5? | **74 s na de start**, precies op het moment dat `PL_Stage0` klaar was en de match naar `InProgress` ging. Lege foutmelding, geen callstack, **22,7 GB RAM vrij → geen OOM** |
| Is dat punt herhaalbaar? | **Nee** — sessie 6 passeerde **exact hetzelfde punt** en liep daarna nog **105 s** door. Eén crash op zes sessies |
| Is deze game een starter of een omvaller? | **Een starter.** Hij laadt Stage 0, houdt boekhouding bij, en de enige crash is eenmalig en niet reproduceerbaar in dezelfde sessie erna |
| De `aimeId`-fout | **Eén keer per binnenkomst in de `[Login]`-stap**, deterministisch op dezelfde logregel (r416), de game gaat er direct daarna gewoon door. Het is de **enige** fout die de game zelf meldt |
| De map/hoofdletterkwestie | De pak spelt `HodZero_v1`, de schijf `Hodzero_V1`. **577 losse bestanden bestaan nergens anders** — daar zit het risico op een hoofdlettergevoelig bestandssysteem |
| Mijn eigen eerdere claim | **Getest en overeind**, maar met een correctie: ik noemde de kern "de sterkste aanwijzing". Vier alternatieven zijn nu **weerlegd**, twee blijven ongetest (zie §5 van `METING-1-DAEMON.md`) |

---

## 1. De zes sessies, en hoe elk afliep

Alle tijden zijn lokaal (CEST). De logs staan in de prefix op
`%LOCALAPPDATA%\Hodzero\Saved\Logs\`; de launcher-logs in `~/.local/state/ravengun/logs/`.

| # | open | dicht | duur | einde | gestart door |
|---|---|---|---|---|---|
| 1 | 08:41:39 | 08:42:12 | 33 s | ranking-dump, **geen** afsluitmarkering | `direct-20261011-084124.log` (`launch-direct.py`) |
| 2 | 08:44:21 | 08:44:47 | 26 s | ranking-dump, geen afsluitmarkering | `direct-20261011-084406.log` |
| 3 | 08:46:27 | 08:46:54 | 27 s | ranking-dump, geen afsluitmarkering | `direct-20261011-084611.log` |
| 4 | 08:47:52 | 08:51:56 | **4 min 4 s** | `AddFirstDeathLogCount`, geen afsluitmarkering | `tpui-20261011-084737.log` (TPUI) |
| 5 | 08:52:40 | 08:54:54 | 2 min 14 s | **CRASH** (`Critical error` → `Log file closed`) | `tpui-20261011-085226.log` |
| 6 | 08:55:41 | 09:02:44 | **7 min 3 s** | `AddSecondDeathLogCount`, geen afsluitmarkering | `tpui-20261011-085527.log` |

**De launcher-logs sluiten die van de game in tijd:** `direct-…084124` zegt om 08:42:52
`klaar (exitcode gestopt)` en sessie 1 eindigde om 08:42:12; `tpui-…085527` heeft mtime 09:02:36 en
sessie 6 eindigde om 09:02:44. **De launcher stopte op het moment dat de sessie eindigde** — dat is
het patroon van stoppen, niet van een crash.

### Wat géén crashbewijs is (en makkelijk verkeerd gelezen wordt)

`Saved/Config/CrashReportClient/` bevat **zes** mappen, één per sessie, met tijdstempels die exact de
**starttijden** van de sessies zijn (08:41:39, 08:44:21, 08:46:27, 08:47:52, 08:52:40, 08:55:41). Die
maakt UE4 bij het **opstarten** aan, niet bij een crash. Er is **één** echte crashmap
(`Saved/Crashes/UE4CC-Windows-249AC8AF…_0000`, met `CrashContext.runtime-xml`, `UE4Minidump.dmp` van
297.524 B en een kopie van het sessielog). Wie de zes `CrashReportClient`-mappen voor zes crashes
aanziet, concludeert het tegenovergestelde van de waarheid.

## 2. De crash van sessie 5, seconde voor seconde

De laatste normaallogregels (frame 577, 08:53:54.961):

```
06.53.54:770 [576] LogApp:  P2_TopY : 235 / P2_LeftX : 42 / P2_CenterX : 474
                          P2_CenterY : 622 / P2_RightX : 890 / P2_BottomY : 979
06.53.54:772 [576] LogGameState: Match State Changed from WaitingToStart to InProgress
06.53.54:772 [576] LogLoad: Took 5.198148 seconds to LoadMap(/Game/HodZero_v1/Maps/Stage0/PL_Stage0)
06.53.54:832 [577] LogLevel: ActivateLevel /Game/HodZero_v1/Maps/System/SL_TestSW 1 1 0
06.53.54:897 [577] LogSlate: … font '../../../Hodzero/Content/HodZero_v1/Fonts/NotoSansCJKjp-Medium.ufont'
06.53.54:961 [577] LogSlate: … font '../../../Hodzero/Content/HodZero_v1/Fonts/NotoSansCJKjp-Bold.ufont'
```

Daarna **zestig seconden lang niets**, en dan (frame 578, 08:54:54.967):

```
LogWindows: Warning: CreateProc failed: File not found. (0x00000002)
LogWindows: Warning: URL: ../../../Engine/Binaries/Win64/CrashReportClient.exe "C:/users/steamuser/…"
LogWindows: Could not start CrashReportClient.exe
LogMemory: Process Physical Memory: 1189.64 MB used, 1262.16 MB peak
LogMemory: Physical Memory: 8945.98 MB used,  22759.14 MB free, 31705.13 MB total
LogWindows: Error: === Critical error: ===
LogWindows: Error: Fatal error!            <- de melding zelf is LEEG
LogExit: Executing StaticShutdownAfterError
LogWindows: FPlatformMisc::RequestExit(1)
Log file closed, 10/11/26 08:54:54
```

Wat er met zekerheid uit volgt:

* **De fout valt op 74 s** — `CrashContext.runtime-xml` zegt `SecondsSinceStart 74`, en 08:52:40 + 74 s
  = 08:53:54, exact de laatste normaallogregel. De crashmap-kopie van het sessielog eindigt daar ook
  (585 regels) terwijl het volledige log 601 regels heeft; het verschil is precies het crashblok.
* **Het is geen geheugenprobleem.** Op het moment van de crash is er **22.759 MB vrij van 31.705 MB**,
  en het proces gebruikte 1,19 GB. (Let op: de `MemoryStats.*`-velden in `CrashContext.runtime-xml`
  staan **allemaal op 0** — die zijn onbruikbaar; gebruik de `LogMemory`-regels.)
* **Er mislukte niets vlak ervoor.** Geen `Error`, geen `Warning` behalve het crashblok zelf. De laatste
  afwijking was de `aimeId`-fout op 06:53.25 — **29 seconden eerder**, dus niet aangrenzend.
* **De crash is eenmalig op dit punt.** Sessie 6 laadde `PL_Stage0` op 07:00:59 (in 1,98 s), ging
  `InProgress`, en liep daarna nog tot 07:02:44 — **105 seconden voorbij exact hetzelfde punt**.
* **De zestig seconden stilte zijn te verklaren.** `App/Hodzero/StartApp.bat` geeft de game
  `-UNATTENDED` met als commentaar `エラーウィンドウ出さない` = "het foutvenster niet tonen".
  **TeknoParrot geeft géén argumenten mee** (`ExtraParameters` is leeg, en `CrashContext` zegt
  `CommandLine` leeg), dus die vlag ontbreekt in deze route. Zonder `-UNATTENDED` blijft UE4 bij een
  fatale fout op een venster staan voordat het afsluit. *Dat mechanisme is afgeleid uit het
  commentaar plus de gat van 60 s; de aanbeveling is concreet:* **`-UNATTENDED` meegeven**.
  (De mislukte `CrashReportClient.exe` is normaal: dat bestand wordt niet meegeleverd.)

**Antwoord op de vraag "starter of omvaller": een starter.** Zes sessies, één crash, en een latere
sessie passeerde hetzelfde punt ruim. Dat sluit ook aan op de eerdere notitie in
`findings/HOTDScarletDawn.md` ("één keer verdween het spelproces ongeveer 70 s na het kiezen van de
items; een tweede en derde poging ging goed") — die observatie is nu met een log en een minidump
onderbouwd.

## 3. De `aimeId`-fout: waar hij vandaan komt en wat er gevraagd wordt

**De bron is de AMDaemon-plugin van de game zelf, niet de daemon.** De regel wordt gevormd door twee
strings die beide in `Hodzero-Win64-Shipping.exe` staan [gemeten]:

```
Exception caused. (Category:%s  Msg:%s)      <- de generieke UE4-uitzonderinglogger
`aimeId` is invalid.                          <- de eigenlijke melding (veldnaam in backticks)
```

en de fout heeft een **eigen foutcode in de game**: `EAMDaemonAimeErrorId::AimeIdInvalid`, naast
`AimeIdNotFound`, `AimeIdRegisterFail` en `AimeIdRegistered`.

**Waar het gebeurt** — deterministisch op **regel 416** in élke sessie, op de overgang naar de
`[Login]`-stap:

```
r412  LogAppSequence: Display: [Login] BeginPlay
r413  LogAppData: [Login] LockGamePlayMode
r414  LogLevel: ActivateLevel /Game/HodZero_v1/Maps/System/SL_EMoneyUI 1 1 0
r415  LogLoad: Took 0.749435 seconds to LoadMap(/Game/HodZero_v1/Maps/Entry/PL_Entry)
>>r416 LogAMDaemon: Error: Exception caused. (Category:Logic  Msg:`aimeId` is invalid.)
r417  LogAppNet: UPlayLog::InitializePlayLog
r418  LogAppNet: UPlayLog::StartPlayLog PlayCount=1
r419  LogSlate: … font '…/HodZero_v1/Fonts/FOT-NewCinemaBStd-D.ufont'
r421  LogNet: Browse: /Game/HodZero_v1/Maps/StageSelect/PL_StageSelect
```

**Hoe vaak:** **één keer per binnenkomst in de `[Login]`-stap** — niet "twee per sessie" zoals ik
eerder schreef. In de zevenminutensessie staat hij twee keer (r416 om 06:56:27 en r1042 om 07:00:38)
omdat de speler daar twee keer door de Entry/Login-flow ging (`FirstPlay:NORMAL : 0` toen `1`); in de
korte sessies één keer, omdat ze niet terugkwamen bij Entry. De regel op **exact r416** in alle
sessies betekent dat het vastligt in de opstartvolgorde.

**Gaat de game door? Ja, direct.** De regels eronder zijn `UPlayLog::InitializePlayLog`,
`UPlayLog::StartPlayLog PlayCount=1`, dan de fonts, dan `Browse … PL_StageSelect`. Er is **geen**
afgebroken sequence en geen herstart. Het is ook de **enige** fout die de game zelf meldt: de rest in
het log zijn `LogSpawn: Warning: SpawnActor failed because no class was specified` (8×) en
Blueprint-waarschuwingen over een lege `History`-array (StageSelect/GaveOver-UI, een verse save).

**Wat er precies gevraagd wordt.** Aan de daemonkant staan de clientfuncties die de game via de
statisch meegelinkte AM Library aanroept [gemeten in `amdaemon.exe`]:

```
amdaemon::ctrl::AimeCtrl::getAimeId(unsigned __int64)
amdaemon::ctrl::AimeCtrl::getAccessCode(unsigned __int64)
amdaemon::ctrl::AimeCtrl::validResult(unsigned __int64)
amdaemon::ctrl::AimeCtrl::isReaderDetected(unsigned __int64)
amdaemon::ctrl::AimeCtrl::getOfflineId / getHardVersion / getFirmVersion / isSegaIdRegistered
amdaemon::ctrl::SequenceCtrl::getPlayingAimeId(unsigned __int64)
amdaemon::PlayParamHolder::setAimeId(amdaemon::AimeId)
```

en aan de gamekant de bijbehorende laag [gemeten in de game-exe]: `UAMDaemonAime`, `GetAimeId`,
`GetAimeID`, `SetAimeID`, `IsSameAimeID`, `GetPlayingAimeId`, `GetRelatedAimeId(Count)`,
`AMDaemonAimePtr`, `AimeOfflineId`, met logregels als `[Login] Prepare (AimeID:%d)`,
`AM Daemon AimeID is not invalid. (UnitIndex:%d, AimeID:%d)`,
`[Login] LoginForPreviewFromAimeID (UnitIndex:%d)` en `Aime preview login couldn't start.`

Kort: **de game vraagt bij de `[Login]`-stap om de AiMe-kaartidentiteit van de lezerseenheid en
valideert die.** Er is geen lezer (de adapter-DLL's `aime_rw_adapterMD.dll`,
`thincatcapclient.dll`, `indicdll.dll` ontbreken; zie `REPORT.md` §9.5), dus het antwoord is ongeldig.
De game kent dat geval: `bIsAimePlay`, `EPlayerPlayMode::Aime`, `MasterAimePlay`, `NormalAimePlay`,
`AddPlayAimeOrGuestCount` — er is dus een **gast-pad** naast het kaart-pad, en deze fout is het
getolereerde spoor daarvan.

**Is dit de eerste patch?** Het is de enige fout die de game zelf meldt, hij is deterministisch en
goedkoop te raken — maar **hij blokkeert het spelen niet**. Een patch hier is dus *niet* nodig om te
spelen; hij is nodig als we het kaart-/loginpad werkend willen hebben (kaart-saves, de Aime-speelmodus).
Wat het wél is: een **klaarliggende indicator** dat onze daemonvervanging de AiMe-dienst goed
beantwoordt — precies het soort controlepunt dat je wilt hebben voordat je aan de keychip-/AiMe-laag
begint (`REPORT.md` §9, §10).

## 4. De map- en hoofdletterkwestie

**De bron van het probleem, gemeten.** De engine vraagt in zijn eigen log om
`../../../Hodzero/Content/HodZero_v1/Fonts/…` (**hoofdletter Z**), terwijl de map op schijf
`Hodzero_V1` heet (**kleine z**). De pak-index spelt het ook met een hoofdletter Z:

| Waar | Spelling | Bewijs |
|---|---|---|
| pak-index | `Hodzero/Content/HodZero_v1/…` | **27.311** indexregels met `HodZero_v1`; **0** met `Hodzero_V1` |
| engine-log | `../../../Hodzero/Content/HodZero_v1/Fonts/*.ufont` | 15× `FOT-NewCinemaBStd-D.ufont`, 9× `NotoSansCJKjp-Bold`, 7× `-Medium`, 6× `-Regular`, 2× `SEGA_KakuGothicDB` |
| schijf | `Content/Hodzero_V1/ADX/` | de mappenlijst uit `VOLLEDIGHEID.md` §5 |

**Waarom het hier werkt.** Twee redenen, en het is belangrijk ze te scheiden:

1. De CIFS-mount is **hoofdletterongevoelig**, dus `HodZero_v1` en `Hodzero_V1` zijn daar dezelfde map.
2. De **fonts komen uit de pak** (18 `.ufont`-regels in de index), en de pak zoekt zijn eigen spelling
   op. De succesvolle fontregels in het log zeggen dus niets over de schijf.

**Waar het risico dan wél zit.** De pak en de losse map zijn **niet hetzelfde**:

| Verzameling | Aantal | Detail |
|---|---|---|
| pak, onder `HodZero_v1/ADX/` | **2.874** | **1.437 `.uasset` + 1.437 `.uexp`** (UE4 bewaart een asset als dat paar) |
| los op schijf, `Content/Hodzero_V1/ADX/` | **2.014** | 1.890 `.uasset` + 123 `.usm` + 1 `.acf` |
| **los, en NIET in de pak** | **577** | **453 `.uasset` + 123 `.usm` + 1 `.acf`** |

Die **577 bestanden bestaan nergens anders**. De 123 `.usm`-films zijn samen circa 4,3 GB (de grootste
is `adver_prime.usm` met 167.611.104 B). Zij worden dus via het **bestandssysteem** bereikt, niet via
de pak — en daar kan een gevraagde spelling met hoofdletter Z op een hoofdlettergevoelig
bestandssysteem (ext4) naast een map met kleine z grijpen.

**Wat hiermee ook beantwoord is.** De engine laadt `../../../Engine/Content/EngineFonts/Faces/RobotoBold.ufont`
met succes, terwijl `Engine/Content` **niet op schijf staat** (`VOLLEDIGHEID.md` §5) en de pak
**3.697** regels met `Engine/Content` bevat. Dat is een gemeten bevestiging dat de pak de
Engine-content levert — precies een van de punten die ik eerder als "niet vast te stellen zonder
start" had staan.

**Wat NIET gemeten is.** Met welke spelling CRIWARE de `.usm`-films opent. Het `NewProject.acf` is de
**CRI Atom-configuratietabel** (zie de correctie hieronder) en bevat **geen bestandsnamen**; en in de
logs komt **geen enkele `.usm`-pad** voor (de CRIWARE-plugin logt hier niets). De blootstelling van
juist die 123 films is dus niet vastgesteld. Wat het zou beslechten: één spellrun met
`WINEDEBUG=+file` en dan op `.usm` grepen — dat kan het project al.

**Het is nu geen probleem, en later misschien wel.** Op deze NAS/CIFS speelt het niet: de game heeft
zes sessies gespeeld. Het wordt relevant zodra iemand de game naar een hoofdlettergevoelig
bestandssysteem kopieert, of een view bouwt waarin de losse ADX-bestanden via een pad met een andere
spelling worden opgezocht.

**Correctie op mijn eigen eerdere tekst.** In `VOLLEDIGHEID.md` §5 noemde ik `NewProject.acf` "de index
daarvan" (van de losse content). Dat is onjuist: het is de **CRI Atom-configuratietabel** —
`VoiceLimitGroupTable`, `DspSettingTable`, `BusTable`, `AisacControlNameTable`, `CategoryTable`,
`CueLimitWorkTable`, `GameVariableTable`, `SnapshotBackupWork`, `CharacterEncodingSystem`,
`Md5Hash`, `VersionString` — en er staat **geen enkel bestandspad** in (0 treffers op `usm`, `ADX`,
`Hodzero`). De conclusie dat de losse map er wezenlijk toe doet blijft staan (de 577 bestanden), maar
de onderbouwing "het acf is de index" klopte niet. In §5 is dat rechtgezet.

## 5. Herhalen

```sh
# op de Deck, read-only:
bash /home/deck/_hotdsd-logs.sh            # de log- en crashboardboom
bash /home/deck/_hotdsd-sessions-stop.sh   # CrashReportClient-mappen + de launcher-logs van die ochtend
bash /home/deck/_hotdsd-advertise.sh       # de directe launcher-logs van 2026-10-11, volledig
```
```powershell
# op Windows, read-only via de SMB-share:
& "…\python.exe" probes\session_analyse.py <map-met-de-logs>
& "…\python.exe" probes\aimeid_pak_probe.py
& "…\python.exe" probes\adx_split_probe.py
```

Ruwe uitvoer: `measurements/sessies-analyse.txt`, `measurements/aimeid-pak-probe.txt`,
`measurements/adx-split-probe.txt`.
