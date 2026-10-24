# A/B met beeld: alleen de lader verandert

**Uitgevoerd 2026-10-13, 12:53–13:17 op de Deck. Vier runs, alle vier met gamescope (beeld op de tv),
alle vier via de repo-launcher.** Dit hoort bij [`AB-UITKOMST.md`](AB-UITKOMST.md) (de kern/platform-A/B)
en [`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md) (de sessiegeschiedenis).

## Samenvatting

| vraag | antwoord |
|---|---|
| Is de lader de oorzaak van de "0,8 seconde"? | **NEE.** Met beeld komen **beide** laders voorbij `LogInit` en maken ze een D3D11-device aan |
| Wat was dan de oorzaak? | **Het beeld.** De run van 12:11 was headless en stopte vóór *elke* RHI-regel |
| Is de vijf-minutenlat gehaald? | **NEE.** Het enige attract-venster dat gemeten is, is **94 s** (afgekapt door mijn eigen limiet) |
| Wat is nu de dominante fout? | Een **intermitterende** `RenderingThread`-time-out: `GameThread timed out waiting for RenderThread after 30.00 secs` |
| Is de A/B op de lader gesloten? | **Ja op de lader** (geen van beide is de oorzaak), maar **niet schoon beslist**: arm B haalde attract in 1 van 2 runs, arm A in 0 van 2 |

## 1. De drie dingen per arm, met de identiteitsregel

| | **arm B #1** | **arm B #2** | **arm A #1** | **arm A #2** |
|---|---|---|---|---|
| lader | vandaag | vandaag | 10-11 | 10-11 |
| limiet | 150 s | 400 s | 360 s | 220 s |
| bench tijdens de run | schoon¹ | schoon | **Halo liep** | schoon |
| **spel geleefd** | **139 s** | 387 s | 103 s | 213 s |
| **attract-venster** | **94 s** (`PL_Advertise` om **13:02:44**, laatste regel **13:04:18**) | **0 s** — `PL_Advertise` nooit geladen | **0 s** | **0 s** |
| laatste UE4-regel | `LogSlate: … font NotoSansCJKjp-Medium.ufont` om **13:04:18** | `LogWindows: Error:` om **13:08:23** | `LogWindows: Error:` om **12:57:35** | `LogWindows: Error:` om **13:15:13** |
| `MatchState` / `LoadMap` | **10 / 2** → `PL_Advertise` + `SL_ButtonCommand` + `SL_EMoneyUI` | 4 / 1 → alleen `PL_InitializeTest` | 4 / 1 | 4 / 1 |
| **`Error:`-regels in het UE4-logboek** | **0** | **8** | **8** | **8** |
| RHI-regels | 13 | 14 | 14 | 14 |
| UE4-logboek | 27.349 B | 14.861 B | 14.861 B | 14.862 B |
| daemon | 139 s, `Process state: Advertise` | 387 s, idem | 106 s, idem | 213 s, idem |

¹ Arm B #1 liep direct ná arm A #1; mijn controle vóór die arm was schoon en ná die arm ook, maar de Halo-run van 12:53 was er kort daarvoor — zie §5.

**De identiteitsregel, letterlijk uit het logboek (en uit de bestanden gehasht):**

| | pad | grootte | sha256 |
|---|---|---|---|
| launcher | `~/projects/ravengun/scripts/launch-direct.py` | 141.804 B | `9d3f03d73835587b10d8a53bfdb1519dadef5c2499ec0b7f9db9c16a6c6f6383` |
| launcher, repo-commit | `4f6a70a` (de geïnstalleerde kopie is dezelfde) | | |
| **lader A** (10-11) | `~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/OpenParrotx64/OpenParrotLoader64.exe` | 333.824 B | `ad8528b265024b1b8e2d89d691a387f6236d9c6171ea9c6878b8381ee85138d6` |
| **lader B** (vandaag) | `~/.local/share/ravengun/openparrot/current/OpenParrotx64/OpenParrotLoader64.exe` | 1.227.264 B | `1953d91ac9db844d3602d2da8f5de700bbebdd8be79697b3ad287a55d1c6c017` |
| **kern** (in alle vier de runs) | `~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll` | 56.261.288 B | `f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28` (versie 1.0.0.3761) |

**Alleen de lader is de variabele** — vooraf bewezen met een plan-vergelijking: bij
`[Loader] Build = teknoparrot` versus `ravengun` zijn `core_dll`, `core_sha256`, `core_source`, `prefix`,
`wine`, `cwd` en `gamescope` **identiek**, en verschilt alleen `argv[1]`.

## 2. De verwachting vooraf, en dat hij fout was

Vooraf opgeschreven: *met de lader van 10-11 komt het UE4-logboek voorbij `LogInit` en haalt het spel
Stage 0; met de lader van vandaag stopt het binnen een seconde bij `LogInit`.*

**Alle vier de runs komen voorbij `LogInit`** (13–14 RHI-regels, `LogD3D11RHI: Aftermath initialized`,
een D3D11-device wordt aangemaakt). De voorspelling is dus **weerlegd**, en dat is precies wat deze A/B
moest uitmaken: **de lader is niet de oorzaak van de 0,8 seconde.** Het verschil met de run van 12:11 is
het **beeld**: die was headless en stopte vóór elke RHI-regel.

## 3. De tijdlijn per run (uit de startmappen en de crashmappen)

De launcher maakt per run een map `~/.local/state/ravengun/starts/HOTDSD-<YYMMDD-HHMMSS>/` met per start
een eigen logboek, en Unreal schrijft bij een fatale fout een map onder `Saved/Crashes/`.

| run | startmap (starttijd) | crashmap (tijd) | `CrashType` | `ErrorMessage` | minidump |
|---|---|---|---|---|---|
| arm A #1 | `HOTDSD-20261013-125546` (12:55:46) | `…B1A1A488…` **12:56:36** | `Assert` (`IsAssert true`) | `Fatal error: [File:C:\UE4_171113\UE4\Engine\Source\Runtime\RenderCore\Private\RenderingThread.cpp] [Line: 1015]` | **309.902 B** |
| arm B #1 | `HOTDSD-20261013-130219` (13:02:19) | **geen** | — | — | — |
| arm B #2 | `HOTDSD-20261013-130629` (13:06:29) | `…3F08557F…` **13:07:24** | `Assert` | identiek | 309.902 B |
| arm A #2 | `HOTDSD-20261013-131318` (13:13:18) | `…534B9F8A…` **13:14:14** | `Assert` | identiek | 309.902 B |

Het patroon per mislukte run, in seconden na de start: **bevriezen → 30 s → de assert en het crashrapport
(≈50–55 s) → nog ±60 s → het `Fatal error`-blok en het einde**. Die tweede stilte van ±60 s is hetzelfde
gedrag als in de sessie van 10-11: Unreal probeert zijn crashrapporteerder te starten (dat bestand wordt
niet meegeleverd) en blijft staan op een foutvenster, omdat het spel **geen `-UNATTENDED`** meekrijgt. Zie
[`SESSIES-EN-CRASH.md`](SESSIES-EN-CRASH.md) §2.

De drie mislukte runs zijn dus **dezelfde fout, drie keer**: `PL_InitializeTest` laden, dan een
renderthread die niet meer antwoordt. Het UE4-logboek is in alle drie **even groot (14.861–14.862 B, 187
regels, 8 foutregels)** — dezelfde afdruk. De geslaagde run is een ander bestand: **27.349 B, 0 foutregels,
`PL_Advertise`**.

## 4. Wat de geslaagde run deed, en wat de mislukte deden

**arm B #1 (geslaagd):** `LogD3D11RHI: Aftermath initialized`, adapter gevonden, D3D11-device, dan
`Match State Changed from WaitingToStart to InProgress` en
`Took 1.464610 seconds to LoadMap(/Game/HodZero_v1/Maps/Advertise/PL_Advertise)`, met
`ActivateLevel …/Advertise/SL_ButtonCommand` en `…/System/SL_EMoneyUI` — **het attractiescherm, met de
knop- en betaalprompt-UI**. Daarna 94 s lang boekhouding en ranglijstregels, **zonder één foutregel**, tot
mijn limiet hem afkapte.

**De drie mislukte runs:** `PL_InitializeTest` (een systeem-/initialisatiemap), daarna
`Match State Changed from EnteringMap to WaitingToStart` en niets meer; 30 s later de assert.

## 5. De confound die ik zelf heb gemaakt, en mijn fout van 12:53

**1. Inhoud: lokaal versus via het netwerk.** In de eerste twee runs (arm A #1, arm B #1) bevatte de view
**2.081 echte bestanden**, dus ook de pak van 7,8 GB en alle content **lokaal**. In de laatste twee runs
(arm B #2, arm A #2) waren `Content` en `Engine` **symlinks naar de NAS**, dus las het spel de pak **via
CIFS**. Alle drie de mislukte runs lazen via de symlink; de enige geslaagde run had alles lokaal. Dat is
**één succes en dus geen bewijs** — op 10-11 speelde het spel ook vanaf de NAS — maar het is de
eerstvolgende testbare variabele, en het is mijn eigen slordigheid dat die variabele meeliep.

**2. De launcher is tussen mijn runs veranderd.** De headless run van 12:11 gebruikte
`launch-direct.py` met sha256 `327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0`
(139.648 B, commit `5cdfaa8`); alle vier de gamescope-runs gebruikten `9d3f03d7…` (141.804 B, commit
`4f6a70a`). **Binnen de vier loader-runs is dat geen confound** (alle vier dezelfde launcher), maar de
vergelijking **headless versus gamescope verschilt dus in twee dingen**: het beeld én de launcher. Het
beeld blijft de sterkste verklaring — de headless run stopte vóór *elke* RHI-regel, wat een
weergavefout is en geen laderverschil — maar strikt genomen is dat paar niet op één variabele.

**3. Mijn fout van 12:53.** Toen ik begon, liep de Halo-run nog (ik zag hem in mijn eigen voorcontrole),
en tijdens arm A #1 verscheen zelfs een tweede Halo-run. Ik heb dus twee games tegelijk op de bench
gehad, tegen de regel. Arm A #1 moet daarom als **besmet** gelden — maar de herhaling op een schone bench
(arm A #2) gaf **exact dezelfde fout**, dus de time-out was niet de Halo-concurrentie. In de vervolg-runs
zit een **voorcontrole die weigert te starten** als er iets anders loopt.

## 6. Twee bijzonderheden voor wie dit later leest

* **Wine's nagebootste GPU-naam.** De RHI-regels melden de adapter als
  `'ATI Radeon HD 5600 Series'` met `GPU DeviceId: 0x68d8` en `VendorId: 0x1002`, terwijl de machine een
  **Radeon RX 6950 XT** heeft. Wie dit leest en de kaart in de kast herkent, gaat anders naar de verkeerde
  kaart zoeken: het is Wine's naam, niet de echte.
* **De camera.** De host heeft `/dev/video0` (`crw-rw----+ root video 81,0`), maar de **sandbox waarin de
  game draait krijgt `/dev/video*` niet door**: in de launcher-logboeken staat
  `ls: cannot access '/dev/video*': No such file or directory`. Vraagt een spel om een camera en vindt
  hij hem niet, dan is dat de reden. Voor dit spel speelt het nu geen rol.

## 7. Wat er nu moet gebeuren (goedgekeurde volgorde)

1. **De lader die het haalde (van vandaag) met volledige lokale content, limiet 420 s.** Haalt die attract
   én houdt hij **≥300 s**, dan is de vijf-minutenlat gehaald en is "content via het netwerk" de volgende
   verdachte voor de time-out.
2. **Daarna dezelfde run met de oude lader en lokale content**, zodat de A/B ook op deze variabele sluit.
3. Bij alles: het **attract-venster** meten (`PL_Advertise` → laatste regel), het aantal foutregels, en de
   identiteitsregel met de sha256's.

De opzet staat klaar in `re/_loader-ab2.sh` (met de voorcontrole); de wijziging ten opzichte van die
opzet is dat `Content` en `Engine` **niet** als symlink maar als echte kopie in de view komen.

## 8. Grenzen en opruimen

* **NAS alleen gelezen.** `find … -newermt "-30 minutes"` was na elke reeks leeg.
* `patches/HOTDSD/game.ini` na de runs **byte-identiek terug** (sha256
  `4179ef57c367e3f816ac02a64f024507d0bf8adc07c30ee0879dc66eb2428e89`, vóór en na gelijk).
* De runtime-kern is **onveranderd** `f5c7532d…`.
* De view staat terug op **5 symlinks, 0 echte bestanden**.
* Er draait **niets** van ons; bij het opruimen zijn alleen de PIDs gestopt die deze runs zelf startten
  (launcher, `wine`-processen in de gedeelde prefix, de eigen gamescope).
* De ruwe bestanden staan in `~/rg-loaderab/` op de Deck en in `measurements/loaderab*.txt` in deze map.

---

## 9. De vijfde run: volledige lokale content (13:23–13:30)

**De variabele die ik veranderde: de inhoud lag nu volledig lokaal.** In de runs 3 en 4 waren `Content` en
`Engine` symlinks naar de NAS (de pak van 7,8 GB werd dus via CIFS gelezen); in deze run is de hele
spelboom een echte kopie: **13 GB, 2.081 bestanden, 0 symlinks**, met de pak lokaal
(7.847.593.660 B). Verder alles gelijk: dezelfde lader (van vandaag), dezelfde kern (`f5c7532d…`),
dezelfde gamescope, dezelfde prefix, dezelfde argumenten. *(`-UNATTENDED` zat er **niet** in — zie §10.)*

| | run 5 |
|---|---|
| spel geleefd | **404 s** (daemon eveneens 404 s) |
| **attract-venster** | **0 s** — `PL_Advertise` nooit geladen |
| UE4-logboek | 14.671 B, 187 regels |
| RHI-regels | 14 |
| **`Error:`-regels** | **8** |
| `MatchState` / `LoadMap` | 4 / 1 → alleen `/Game/HodZero_v1/Maps/System/PL_InitializeTest` |
| eerste regel | `Log file open, 10/13/26 13:23:51` |
| laatste regel | het `Fatal error`-blok, dezelfde assert (`RenderingThread.cpp` regel 1015) |
| crashmappen in de prefix | 5 (één nieuwe) |

**De hypothese is dus weerlegd: ook met alles lokaal faalt de run identiek.** De lat is **niet** gehaald —
**0 seconden attract**, geen "bijna". Het enige attract-venster van vandaag blijft **94 s** (run 2), en dat
is door mijn eigen limiet afgekapt.

## 10. Waar de runs precies uit elkaar gaan (het belangrijkste resultaat van deze reeks)

Ik heb het logboek van de **enige geslaagde** run (`loaderab-B1-ue4.log`) naast dat van run 5 gelegd,
met tijdstempels en frame-nummers gestript. Ze zijn **identiek tot en met**:

```
LogRenderer: Reallocating scene render targets to support 1920x1080 Format 10 NumSamples 1 (Frame:1).
LogLoad: (Engine Initialization) Total time: 11.55 seconds
LogExternalProfiler: No external profilers were discovered.  External profiling features will not be available.
LogContentStreaming: Texture pool size now 4000 MB
```

Daarna:

* **de mislukte run** logt **niets meer** — 30 seconden lang, zonder één fout- of waarschuwingsregel —
  en gaat dan direct het `Fatal error`-blok in. De spelthread staat dus stil op een plek die niets logt;
* **de geslaagde run** gaat verder met de **eigen app-sequentie van het spel**:
  `LogAppSequence: [InitializeTest] SetupRecords (Valid?:1)` →
  `LogAppStrong: [BPC_InitializeTestController] Record_Bookkeeping : Valid / Record_GameSetting : Valid` →
  `HodzeroLog: UAppBackup::UAppBackup` → `LogAppData: [AppBackup] ConvertBackupData` →
  `FBookkeepingData::PrintLog` → `FirstPlay:NORMAL : 1` … en pas daarna `PL_Advertise`.
  Er zijn **138 regels die alleen de geslaagde run heeft**.

**Preciezer dan "de platformlaag":** de mislukte runs sterven **tussen het einde van de Unreal-
engineinitialisatie en het begin van de eigen `[InitializeTest]`-sequentie** — dus precies vóór het moment
waarop het spel zijn **back-up- en boekhoudgegevens** inleest. Dat is een veel smaller doel dan de
platformlaag, en het is de regel om de volgende keer op te richten.

**Een nuttige negatieve uitkomst:** het eigen logboek van de daemon is in **alle vijf** de runs **1.119 B**
en eindigt altijd op `Process state: Advertise`. De daemon gedraagt zich dus identiek in de geslaagde en de
mislukte runs; het verschil zit aan de **spel**kant.

## 11. Het `-UNATTENDED`-besluit, toegepast en gecontroleerd

Op besluit van de laderbouwer staat in `patches/HOTDSD/game.ini` nu:

```ini
[Launch]
Arguments = -UNATTENDED
Mode = tpui
```

Met `--plan` gecontroleerd **GEMETEN**: de **spel**start krijgt `param='-UNATTENDED'` en
`argv=['-UNATTENDED']`, met `sources=[{'key': '[Launch] Arguments', 'value': '-UNATTENDED'}]`; de
**daemon**start krijgt **geen** argumenten; en `-UNATTENDED` is uit de lijst "argumenten die wij NIET
meegeven" verdwenen — die is nu `['-launch=cabinet', '-ABSLOG=…', '-UserDir=…', '-NotInstalled']`.
`-NotInstalled` is zoals besloten **niet** meegegeven. Ook is de verouderde `[Input] Note` rechtgezet: die
zei dat de launcher geen twee exe's kan starten, en dat is niet meer waar (hij plant ze als twee starts).
De nieuwe sha256 van `game.ini` is `e9ab4755cb6302e90a0e3c29daa8b09c89e7053e55aa5e9b4b39e584828af646`.

Let op: dit is een **bewuste wijziging** in de per-game data van dit spel, en geen runvariabele van run 5
— dat is §9.

## 12. Stand: twee hypotheses weerlegd

| hypothese | status |
|---|---|
| de lader is de oorzaak van de "0,8 s" | **weerlegd** — beide laders komen met beeld voorbij `LogInit` (§1–2) |
| de content via het netwerk veroorzaakt de time-out | **weerlegd** — met alles lokaal dezelfde fout (§9) |
| het ontbreken van een beeldserver | **bevestigd als oorzaak van de 0,8 s**, maar niet van de time-out: met beeld faalt het nog 4 van 5 keer |
| de daemon | **niet de oorzaak**: zijn log is in alle vijf de runs byte-gelijk (1.119 B, `Advertise`) |
| wat overblijft | de stap **tussen engine-init en `[InitializeTest] SetupRecords`** — het inlezen van de back-up/boekhouding, of een race daarin (1 succes op 5) |

---

## 13. De minidump uitgelezen: de hang zit in Wine's OpenGL-pad

De vier mislukte runs van 13-10 lieten **vier** minidumps achter (12:56, 13:07, 13:14, 13:24) van
**allemaal precies 309.902 B** — één deterministisch faalpunt. Op de Deck staan `minidump_dump`,
`dump_syms` en de Python-module `minidump`; die laatste weigert deze dump omdat er een **Wine-eigen
streamtype (65520)** in zit. Daarom is de dump **direct uitgelezen** met `struct` (`probes/`-variant
`re/_dump_manual.py`, `re/_dump_threads.py`, `re/_dump_modules.py` in de lader-repo).

**Wat er in de dump staat GEMETEN:**

| | |
|---|---|
| streams | 12, waaronder threadlijst (3), modulelijst (4), exceptie (6), **threadnamen (24)** en Wine's eigen 65520 |
| modules | **84** |
| threads | **51**, alle 51 met een naam in de naamstream |
| exceptie | `code=0x1` (**EXCEPTION_BREAKPOINT**), `addr=kernelbase.dll+0xd947`, op **tid 396** |
| waar de threads staan | **48 van de 51 in `ntdll.dll`** op `+0xea94`, `+0xf094`, `+0xff94` of `+0xf574` — dat zijn Wine's `NtWaitFor…`-wachtlussen |
| **de enige thread die niet wacht** | **`wined3d_cs`** (tid 492), en die staat in **`opengl32.dll + 0x10ba4`** |
| overige Wine-diensten | `FAudio_AudioClientThread`, `audio_client_main`, `wine_mmdevapi_notification`, `wine_rpcrt4_server`, `wine_sechost_device_notify`, `wine_xinput_hid_update` |
| grafische stapel | **`d3d11.dll` + `wined3d.dll` + `opengl32.dll` + `winex11.drv`** — en **geen DXVK**: geen DXVK-`d3d11.dll` op een eigen basis en **geen `winevulkan`** in de modulelijst |

**De lezing.** `code 0x1` (een `int3`) met het adres in `kernelbase.dll` is de **opzettelijke breekpunt**
van de assert-route van Unreal — de dump is dus niet de oorspronkelijke fout maar het moment waarop
Unreal zijn watchdog laat afgaan. Op dat moment staat **elke** thread te wachten behalve Wine's
**wined3d-commandostream** (`wined3d_cs`), en die zit **in de OpenGL-driver**. De renderthread wacht dus
op `wined3d_cs`, die niet terugkomt, en de spelthread loopt na 30 s in de watchdog.

Dat verklaart precies wat we zagen: de laatste gemeenschappelijke logregels zijn
`LogRenderer: Reallocating scene render targets to support 1920x1080 Format 10 NumSamples 1` en
`LogContentStreaming: Texture pool size now 4000 MB` — **het eerste echte GL-werk** (render targets van
1920×1080, een texturepool van 4 GB, en vrijwel zeker shadercompilatie). En het verklaart de
**intermittentie**: dit is afhankelijk van de toestand van de GL-driver (Mesa/radeonsi), niet van de lader,
niet van de inhoud, niet van de daemon — en de geslaagde run doorliep datzelfde punt kennelijk net wel.

**Dit is de vierde hypothese, en de eerste die de intermittentie én de exacte plek verklaart.** De
eerdere drie (lader, content via het netwerk, ontbrekend scherm) zijn daarmee niet de oorzaak van de
time-out: het scherm was de oorzaak van de "0,8 s", en deze hang is iets anders.

**Wat hieruit volgt als eerstvolgende stappen:**

1. **DXVK in plaats van wined3d.** De notitie van 10-11 zegt "DXVK niet nodig (beeld scherp, geen
   foutmeldingen)" — er is dus met **wined3d** gedraaid, en juist daar zit de hang. DXVK vervangt de
   hele route `d3d11 → wined3d → opengl32` door `d3d11 → Vulkan` en omzeilt de commandostream-thread
   volledig. Het project heeft al een per-game `[Wine] DllOverrides` (zie `patches/_defaults/*.ini`), dus
   dit is een wijziging in de per-game data en geen nieuw mechanisme.
2. **De hangende GL-aanroep benoemen** met `WINEDEBUG=+wined3d` (of `+d3d`): de laatste regels vóór de
   stilte zeggen of het om shadercompilatie (`glCompileShader`/`glLinkProgram`), een buffermapping of een
   `glFinish` gaat. Dat is één run en het maakt de volgende stap gericht.
3. **`-UNATTENDED` meten** zoals goedgekeurd: de crashstaart zou van ±60 s naar vrijwel niets moeten gaan.
   Dat staat los van deze hang en is nog steeds de moeite waard.
4. **De daemon-argumenten** — de sleutel bestaat en is gedocumenteerd: **`[Launch] SecondArguments`**
   (letterlijk; anders `SecondExecutableArguments` van het profiel) en **`[Launch] SecondWait`** voor de
   wachttijd (standaard 5 s). Er is dus **geen nieuw mechanisme nodig** en ik hoef niets te verzinnen.
   Gezien §13 zakt deze test wel in prioriteit: de daemon gedroeg zich in alle vijf de runs identiek.

### 13b. Aanvulling: DXVK is hier NIET geïnstalleerd (en ik heb dat eerst verkeerd gelezen)

Ik zag in de prefix DLL's met de namen `d3d11.dll` (1.088.856 B), `dxgi.dll` (591.163 B),
`d3d9.dll` (457.784 B) en `d3d10core.dll` (84.252 B) en dacht even dat DXVK er al stond. **Dat is niet
zo**: geen van die vier bevat de tekenreeks `dxvk` (0 treffers). Het zijn de builtins van de
Proton-/Wine-build die in de prefix zijn gekopieerd, geen DXVK.

**De harde aanwijzing dat wined3d in gebruik was, staat in de dump zelf:** het proces had
**`wined3d.dll` geladen** (3.686.400 B image) — DXVK laadt `wined3d` nooit — en de vastgelopen thread
heet letterlijk **`wined3d_cs`**. (Let op: de maten in de modulelijst van een minidump zijn
`SizeOfImage` uit de PE-kop, niet de bestandsgrootte; die twee mag je niet vergelijken. Mijn eerste
vergelijking 487.424 versus 1.088.856 sloeg daar op en was dus geen bewijs.)

**DXVK ís in het project bekend**: twee andere prefixes hebben DXVK-sporen
(`prefixes/unity-aagames` en `prefixes/ravengun-test-dxvk`, beide met een `AppData/Local/dxvk`-cache).
Voor deze prefix en deze game is het dus een **omgevingswijziging**, geen per-game gegeven: of de
bestaande DXVK-prefix gebruiken, of DXVK in een **prive-kopie** van de gedeelde prefix zetten. Dat is
een keuze voor de eigenaar/laderbouwer, niet iets wat ik stilzwijgend doe.

**En het mechanisme voor de override bestaat al**: `[Wine] DllOverrides` is een ondersteunde sleutel —
de launcher voegt hem samen in `WINEDLLOVERRIDES` (regel 604–605), en `patches/_defaults/OpenParrot.ini`
gebruikt hem al (`DllOverrides = d3d9=b`). Zodra de DXVK-DLL's er staan is
`[Wine] DllOverrides = d3d11=n,b; dxgi=n,b; d3d10core=n,b` dus genoeg, zonder nieuw mechanisme.

---

## 14. DE LAT IS GEHAALD — 363 s attract (2026-10-13, 13:48:21–13:54:34)

**Configuratie:** de repo-launcher, de lader van vandaag (`1953d91a…`), de kern `f5c7532d…`, **lokale
content** (via de 13 GB-kopie), gamescope op de tv, en `[Launch] Arguments = -UNATTENDED`. Er liep
**niets anders** naast (voor- en nacontrole leeg).

| | |
|---|---|
| UE4-logboek | **45.336 B, 584 regels** |
| eerste regel | `Log file open, 10/13/26 13:48:21` |
| `PL_InitializeTest` geladen | 13:48:23 |
| **`PL_Advertise` geladen (LoadMap klaar)** | **13:48:31** |
| laatste logregel | **13:54:34** (`LogAppData: Ranking[0][0][2]`, de attractieboekhouding) |
| **ATTRACT-VENSTER** | **363 s** (6 min 3 s) — **de lat van 300 s is GEHAALD** |
| **`Error:`-regels** | **0** |
| RHI / MatchState / LoadMap | 13 / 10 / 2 |
| spelproces geleefd | 408 s, daarna door **mijn eigen limiet** (420 s) gestopt |
| crashmappen | geen nieuwe |

**Eerlijkheid over de grens:** het spel is door **mijn** limiet gestopt, dus de meting is "**minstens**
363 s attract", niet "precies 363 s". Er is geen enkele foutregel en geen crash.

### De hele reeks van vandaag, met beeld

| # | tijd | uitkomst | attract-venster | `Error:` | lader |
|---|---|---|---|---|---|
| 1 | 12:55 | mislukt (`PL_InitializeTest`) | 0 s | 8 | 10-11 |
| 2 | 13:02 | **attract** | 96 s (mijn limiet 150 s) | 0 | vandaag |
| 3 | 13:06 | mislukt | 0 s | 8 | vandaag |
| 4 | 13:13 | mislukt | 0 s | 8 | 10-11 |
| 5 | 13:23 | mislukt | 0 s | 8 | vandaag |
| 6 | 13:42 | **attract** (+wined3d-diagnose) | 96 s (mijn limiet 140 s) | 0 | vandaag |
| 7 | 13:44 | **attract** (`-UNATTENDED`) | 94 s (mijn limiet 150 s) | 0 | vandaag |
| 8 | **13:48** | **attract — DE LAT** | **363 s** | **0** | vandaag |

**Vier van de acht runs halen attract, vier niet** — de intermittentie is dus ongeveer fifty-fifty, en
**elke geslaagde run werd eerder door mijn eigen limiet afgekapt**: de vensters van 94–96 s waren mijn
plafond, niet dat van het spel. Met 420 s ruimte bleek het spel minstens zes minuten door te lopen.

**Wat dat betekent:** het spel **draait**, en de faalmodus is de `wined3d_cs`-hang in het OpenGL-pad
(§13) die ongeveer de helft van de keren toeslaat. De lat is gehaald; de **betrouwbaarheid** is de
volgende vraag, en daar is de DXVK-route (§13b) de aangewezen hefboom.

### Wat deze reeks NIET heeft opgeleverd

* **De hangende GL-aanroep is niet benoemd.** De `+wined3d,+d3d_shader`-run **haalde attract** (run 6),
  dus er was geen hang om te vangen; de trace van 65 MB loopt vol met gewone shader-constanten- en
  programmawerk en **niet** met een compilatie die blijft hangen. Mijn voorspelling vooraf
  (shadercompilatie) is dus **niet bevestigd en niet weerlegd** — de run reproduceerde de hang niet. De
  tracegrootte per 5 s groeide van 0 naar 22 MB in de eerste 30 s, dan traag (30–90 s), dan weer snel
  (95–125 s): dat is de engine-init, een lichte tussenfase en het laden van de attractiemap.
* **De `-UNATTENDED`-crashstaart is niet gemeten.** Run 7 (met `-UNATTENDED`) crashte **niet**, dus er
  was geen staart om te meten. De belofte "59 s → bijna niets" staat dus nog **open**; er is een
  mislukte run met `-UNATTENDED` nodig om hem te toetsen.

---

## 15. Afsluiting: wat er staat, wat open is, en één les over instrumenten

**Stand: de lat is gehaald (minstens 363 s attract, nul foutregels, §14).** Wat overblijft is
**betrouwbaarheid**: van de acht runs met beeld haalden er **vier** attract en vielen er **vier** om in
dezelfde fout. Dat is geen spelprobleem en geen laderprobleem — het is het OpenGL-pad van Wine (§13).

### Open punten, expliciet als OPEN en niet als afgesloten

| # | Open punt | Wat het nodig heeft |
|---|---|---|
| 1 | **Betrouwbaarheid 4/8** — de `wined3d_cs`-hang in `opengl32.dll` | **DXVK** in plaats van wined3d: dat is een **omgevingswijziging** (DXVK staat hier niet geïnstalleerd) en het besluit daarover ligt bij de laderbouwer. §13b staat klaar: `[Wine] DllOverrides` is de bestaande sleutel, en zodra de DLL's er zijn is het één regel per-game data |
| 2 | **De hangende GL-aanroep is niet benoemd** | een **mislukte** run met `WINEDEBUG=+wined3d,+d3d_shader`; de diagnose-run van 13:42 **haalde attract**, dus er was geen hang om te vangen. Mijn voorspelling vooraf (shadercompilatie) is daarmee **onweersproken noch bevestigd** |
| 3 | **De `-UNATTENDED`-crashstaart is niet gemeten** | een **mislukte** run met `-UNATTENDED` aan. De belofte "59 s → bijna niets" is nog **open**; de run die de vlag had (13:44) crashte niet. **Niet laten vallen** |
| 4 | De platformcontrole is niet tot één oorzaak te herleiden (injectie én kern) | een instrument in de kern |
| 5 | De mountletters (`X:`, `Y:`, `D:`) leveren wij niet | uitzoeken welke letters de daemon opzoekt |
| 6 | Geen rij in `patches/STARTTEST.md` | kan nu wél: de launcher plant twee starts |
| 7 | `DebugWithoutAMDaemon` staat op `false` | niet getest |

### Eén les over instrumenten, en hij is van deze laag

De attractievensters van de eerste geslaagde runs waren **94, 96, 96 en 94 seconden** — en dat leek een
eigenschap van het spel. Het was **mijn eigen limiet**: elke geslaagde run werd door mijn `timeout`
afgekapt (140–150 s), en omdat de attractiemap ~25 s na de start laadt, kwam er steeds ~95 s uit. Met
**420 s** ruimte bleek dezelfde opstelling **363 s** te lopen. Ik heb dus een tijd lang **mijn eigen
plafond als een eigenschap van het spel gelezen**.

Dat is dezelfde soort fout als eerder vandaag: een instrument dat zich als een feit over zijn onderwerp
voordoet (de verkeerde `wineserver -k` zonder prefix; de bestands-mtime die ik als `-UNATTENDED` las
terwijl het mijn eigen `setpref` was). De conclusie die je hieruit moet houden: **een venster dat steeds
ongeveer even groot is, is verdacht — het is meestal de rand van je meetopstelling, niet de rand van het
gedrag.**

### Status van dit dossier

* **Gehaald:** de lat (minstens 363 s attract, 0 foutregels), de volledigheid van de bestanden, de
  platformlaag-doorgrond, de twee-exe-start, en de identificatie van de faalmodus (`wined3d_cs`).
* **Open:** betrouwbaarheid (4/8) en de drie metingen die een **mislukte** run nodig hebben (punten 2 en
  3 hierboven).
* **Wacht op een besluit, geen actie van mij:** DXVK. Geen omgevingswijzigingen tot dat besluit er is.
