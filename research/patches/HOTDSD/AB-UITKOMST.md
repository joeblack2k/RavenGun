# A/B-uitkomst: bevredigt de gesloten kern de platformcontrole?

**Uitgevoerd 2026-10-13, 12:08:52–12:19:35 op de Deck. Daemon-only armen; A1 startte ook het spel
(zoals de echte route doet). De NAS is op één na onaangeroerd gebleven — zie §6, dat is mijn fout en
hij staat er volledig in. Niets van ons draait nog; de runtime-kern staat op `f5c7532d…`.**

Dit hoort bij [`AB-PLATFORMCHECK.md`](AB-PLATFORMCHECK.md) (de opzet en de verwachtingen vooraf) en
[`METING-1-DAEMON.md`](METING-1-DAEMON.md) (de nulmeting).

## Samenvatting: het antwoord, met de nuance die erbij hoort

De vraag was: **bevredigt de gesloten kern `amPlatformGetPlatformInformation()`?**

**Ja — maar niet alleen.** Twee dingen blijken nodig, en dat was niet de verwachting:

1. **De injectie zelf (door de lader) haalt de dodelijke afloop eruit.** Met de lader en *welke DLL dan
   ook* geïnjecteerd leeft de daemon **≥30 s** (A3g, met een volledig andere kern). Rechtstreeks gestart
   sterft hij na 1 s. Dat is een tweede voorwaarde die niemand had opgeschreven.
2. **Alleen met de TeknoParrot-kern komt hij ook echt voorbij de platformfase.** A2g raakt de
   mount-/appfile-laag (**AMFS 95×, appfile 11×** in de trace); A3g met de andere kern raakt die
   **0×**. Iets voorbij `Manager::initialize()` werkt dus alleen met deze kern.

De laderroute werkt bovendien met de **entry-point-trampoline** (`Resolved target kernel32.dll procedure
… Have fun :)`), precies zoals op 10-11, en de kern meldt zich als **`TeknoParrot64.dll (1.0.0.3761)`** —
dezelfde versie als die ochtend.

## 1. Per arm: levensduur, laatste eigen regel, en de vier lagen

De vier lagen zijn `FddDriver` (sleutelchip-apparaat), `amDongle`/`amyDongle` (dongle-API), `AMFS`
(het AM-bestandssysteem) en `appfile` (de app-data). De aantallen komen uit een `WINEDEBUG=+file`-trace
van 1,5 MB per arm; **0 betekent daar "niet aangeraakt"**, behalve waar geen trace is gemaakt.

| Arm | Wat hij wijzigt | Daemon-levensduur | Laatste eigen regel / bewijs | Fdd | amDongle | amyDongle | AMFS | appfile |
|---|---|---|---|---|---|---|---|---|
| **A1** | de echte route (repo-launcher, beide exe's) | **≥95 s**, afgebroken door mijn limiet (`exitcode -15`); spelproces **88 s** (12:11:12→12:12:40) | **`Process state: Advertise`** | n.v.t.¹ | n.v.t.¹ | n.v.t.¹ | n.v.t.¹ | n.v.t.¹ |
| **A2g** | t.o.v. A1: **de game wordt niet gestart** | **≥30 s, nog levend op de limiet** | stdout verloren (buffering); bewijs is de trace | 0 | 0 | 0 | **95** | **11** |
| **A3g** | t.o.v. A2g: **de kern** (een ándere DLL) | **≥30 s, nog levend op de limiet** | idem | 0 | 0 | 0 | 0 | 0 |
| **A4** | t.o.v. A2g/A3g: **de lader** (direct) | **~1 s, sterft zelf** | alleen de banner — zonder `-l` is de uitzondering onzichtbaar | 0 | 0 | 0 | 0 | 0 |
| **A4b** | t.o.v. A4: **de argumenten** (`-c -l`) | **~1 s** | `amw_platform_manager.cpp` regel 102, `am::platform::Manager::initialize`, `amPlatformGetPlatformInformation(). ErrCode 0.` | 0 | 0 | 0 | 0 | 0 |
| **A5** | t.o.v. A4b: **de mapvorm** (kale AMDaemon-kopie) | **~1 s** | dezelfde regel | 0 | 0 | 0 | 0 | 0 |
| ~~A2c/A3c~~ | *verworpen, zie §5* | 2 s, lader stopt zelf | `Unable to find core DLL!` | — | — | — | — | — |

¹ De launcher forceert `WINEDEBUG=-all`, dus voor A1 is er **geen** trace en zijn de lagen niet te tellen.
Dat is geen "niet aangeraakt" maar "geen bewijs". De daemon-stdout is voor die arm het scherpe instrument,
en die is er: de vier regels hierboven.

**De letterlijke A1-uitvoer per proces** (uit `measurements/ab-A1-launcher.txt` en de per-start-logs):

```
Core:   Z:\…\TeknoParrotUi-2.0.0.130-linux-x64\TeknoParrot\TeknoParrot64.dll (1.0.0.3761)
Game:   Z:\…\views\HOTDSD\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe (0.0.0.0)
Resolved target kernel32.dll procedure: local=00006FFFFFE8F81C remote=00006FFFFFE8F81C rva=0xF81C
Have fun :)
amdaemonExe Ver.2437 Build:Apr 19 2018 23:13:37
Process state: setup Game Mode
Error 0
Process state: Advertise
```

## 2. Wat elke uitkomst betekent

* **A2g bereikt AMFS/appfile.** `Manager::initialize()` loopt vóór de arcade-/mountfase; als de daemon
  `TeknoParrot/AMFS/sysfile.dat` en `appfile.dat` aanraakt, is hij die fase dus **voorbij**. Dat is
  sterker bewijs dan een logregel: het is de daemon die zijn eigen paden opzoekt.
* **A3g overleeft maar vordert niet.** Met een andere kern blijft het proces 30 s leven, maar raakt het
  geen enkele mount. Twee lezingen, en ik kan ze met deze gegevens niet scheiden:
  (i) de andere kern verandert alleen de uitzonderingsafhandeling, waardoor de platformfout niet meer
  fataal is en de daemon blijft hangen; of (ii) de andere kern doet niets en de daemon wacht op iets
  anders. **Wat het zou scheiden:** de daemon met de andere kern en een eigen `-l`-logboek — dat kan niet
  door de lader (die geeft geen argumenten door), dus dat vraagt een instrument in de kern.
* **De sleutelchip is in géén arm aangeraakt** (`FddDriver` 0×, `amDongle`/`amyDongle` 0×), ook niet in
  A2g die de mountfase haalde. De sleutellaag ligt dus ná de mountfase, of wordt volledig overgeslagen.
* **De twee-exe-start werkt.** De launcher plant en start 1/2 `amdaemon.exe`, wacht 5 s, start 2/2 het
  spel — met per start een eigen logboek. De voorwaarde voor meting 2 is daarmee weg.

## 3. Wat NIET gehaald is

**De lat (5 minuten attract- of spelmodus) is niet gehaald en is ook niet gemeten.** In A1 leefde het
spelproces **88 s**, maar:

* **ik heb headless gedraaid** — `DISPLAY` en `WAYLAND_DISPLAY` waren niet gezet, dus er was geen
  beeldserver en er kan niets gerenderd zijn. Op 10-11 draaide het spel met een echte display;
* het UE4-logboek van die sessie (4.727 B) stopt na **0,8 s**, bij `LogInit: Using OS detected locale`,
  **vóór** elke `LogD3D11RHI`-regel en vóór het mounten van de plugins. Op 10-11 stond
  `LogD3D11RHI: Aftermath initialized` er al vroeg in;
* dat kan twee dingen zijn: het logboek is **geblokkeerd gebufferd** en mijn `SIGTERM` heeft de rest
  weggegooid, óf het spel is in de initialisatie blijven steken. **Ik kan die twee niet scheiden**, en
  dus claim ik geen schermwaarneming.

**De volgende run moet dus een display hebben** (gamescope of de Xwayland van de eigenaar) en **netjes
gestopt worden** in plaats van gekilld, anders is het logboek weer afgekapt.

## 4. Eén nieuw verschil met 10-11 dat aandacht verdient

De launcher van vandaag gebruikt een **andere lader** dan de runs van 10-11:

| | pad | grootte | sha256 |
|---|---|---|---|
| 10-11 en de runtime | `…/runtime/TeknoParrotUi-2.0.0.130-linux-x64/OpenParrotx64/OpenParrotLoader64.exe` | 333.824 B | `ad8528b2…` |
| de launcher van vandaag | `…/openparrot/current/OpenParrotx64/OpenParrotLoader64.exe` | 1.227.264 B | `1953d91a…` |

De kern is wél dezelfde (`f5c7532d…`, `1.0.0.3761`). Dit is precies de valkuil die de identiteitsregel
moet vangen, en het is de eerste kandidaat voor "waarom komt het spel van vandaag niet verder dan 0,8 s
terwijl het op 10-11 Stage 0 haalde". **Niet bewezen** — het kan net zo goed de ontbrekende display zijn.

## 5. Mijn eigen fouten, en wat ik daarmee gedaan heb

1. **De eerste A2/A3-reeks was ongeldig.** Ik gaf de lader het kernpad **mét** `.dll`, terwijl de lader
   er zelf `.dll` achter zet. Hij zocht dus `TeknoParrot64.dll.dll`, meldde `Unable to find core DLL!`
   en stopte na 2 s **zonder de daemon te starten**. De launcher geeft het pad zónder `.dll` door — dat
   had ik uit het plan kunnen halen. **Ik heb die armen verworpen in plaats van er iets uit af te leiden**,
   de oorzaak opgezocht, en ze daarna met het juiste pad opnieuw gedaan (A2g/A3g).
2. **Eén reeks heeft de NAS aangeraakt.** De overlay-view was door de eerste harness netjes teruggezet
   naar symlinks, en ik heb daarna de A2/A3-reeks opnieuw gedraaid **vanuit die view** — dus door de
   symlinks heen, op de NAS. Wat er is geschreven:

   | Bestand | Tijdstip | Grootte |
   |---|---|---|
   | `…/AMDaemon/TeknoParrot/D/SDET/appfile.dat` | 12:17:14 | 8.388.608 B (herschreven) |
   | `…/AMDaemon/teknoparrot.ini` | 12:17:22 | 449 → **552 B** (de lader schrijft hem) |
   | `…/AMDaemon/TeknoParrot/` en `…/TeknoParrot/Y` | 12:16:51 | mtime |

   Het zijn de eigen bestanden van de vendeur-daemon en van de lader, geen artifacten van ons, en
   `appfile.dat` wordt sinds 10-11 door elke speelsessie herschreven — maar **het had voorkomen kunnen
   worden** en dat is mijn fout, niet een noodzakelijkheid. De gecorrigeerde ronde (A2g/A3g) is mét
   overlay gedraaid en heeft de NAS **niet** aangeraakt: alle tijdstempels daar zijn van vóór die runs.
3. **Een stale logboekverwijzing in mijn eerste harness.** De regel "nieuwste launcher-logboek" wees
   naar `direct-20261013-113356.log` — een run van **11:33–11:36 die niet van mij was** (hij eindigde op
   `lader gestopt (exitcode -15)`). Mijn A1-bewijs is de opgevangen stdout, niet dat bestand. Er heeft
   dus vóór mijn slot een launcher-run plaatsgevonden; mijn eigen voor/na-controles tonen dat er tijdens
   mijn runs niets anders liep.

## 6. Opgeruimd en gecontroleerd

* view teruggezet naar **5 symlinks, 0 echte bestanden**;
* runtime-kern **`f5c7532d6e63848564a4b9233d30706793066d94d3bbafafe45bf72c2b1d6e28`** — die stond al
  goed (mtime 12:02 vandaag) en is niet gewijzigd;
* **niets van ons nog actief** (per proces de `WINEPREFIX` uit `/proc/<pid>/environ` gelezen);
* de overlay-view van 123 MB blijft staan in `~/rg-ab2` — op te ruimen met `rm -rf ~/rg-ab2`.

## 7. Wat dit betekent voor het plan

* **De route "daemon zonder enige TeknoParrot-component" is niet met één ingreep te halen.** Er zijn
  twee voorwaarden: de injectie (die de fatale afloop voorkomt) én de kern (die de platformfase laat
  slagen). Wie de platformcontrole in onze eigen kern wil nabouwen, moet dus **beide** verklaren.
* **De eerstvolgende vraag is niet de sleutelchip maar de lader en de display.** De sleutelchip is in
  geen enkele arm aangeraakt; het spel komt niet voorbij zijn initialisatie. Een run **met display**,
  **met een langere limiet** en **netjes gestopt** is de logische volgende stap — en die kan meteen
  ook de twee-exe-start als startmeting in `patches/STARTTEST.md` zetten.
