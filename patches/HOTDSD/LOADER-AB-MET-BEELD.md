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
