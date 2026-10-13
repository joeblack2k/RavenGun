# Verificatierun Transformers: Shadows Rising — UITKOMST

Gemeten op de Deck op 2026-10-13, met beeld op de tv. Twee runs, geen enkele derde.
Voorbereiding en de vier vooraf opgeschreven gevallen: `VERIFICATIERUN.md`.

## 0. Samenvatting in één blok

| vraag | uitkomst |
|---|---|
| Start de keten, en komt het **tweede** proces? | **JA**, twee keer. Lader → `Shell.exe` → `Transformers2.exe`, met tijdstippen hieronder. |
| Blijft de keten leven? | **JA**, run 1 ruim 10 minuten; run 2 afgebroken na 2,5 min (bench nodig). |
| **Vijf minuten attract of spel?** | **GEHAALD in run 1**: ± 8,5 minuut spel op het scherm, attract-lus 5× opnieuw gestart. |
| IO-bord gevonden + `Unlocked OK`? | **NEE.** Het bord wordt nu wél gemaakt (bewijs in §3), maar de shell vindt het niet. |
| Op tijd? | Ja, en **sneller dan gedocumenteerd**: attract al na ± 1 min 42 s. |
| Welke launcher? | De **repo**-launcher en de **repo**-daemon, met shas in §5. |

**Twee vervuilingen, eerlijk gemeld:** er liep een tweede run naast (§7) en **run 1 heeft de invoerroute nooit gewapend** (§2) — dat laatste is een fout in mijn eigen draaiboek.

## 1. De vier gevallen, vooraf opgeschreven en nu ingevuld

| # | geval | uitkomst |
|---|---|---|
| **a** | shell start **en** `Transformers2.exe` komt op | **HIT**, twee keer (§2) |
| **b** | shell start, spel niet | niet voorgekomen |
| **c** | IO-bord gevonden en handshake tot `Unlocked OK` | **NIET gehaald** (§3) |
| **c′** | `Couldn't find IOBoard device` terwijl de daemon het bord wél maakt | **DIT IS DE UITKOMST** (§3) |
| **d** | iets anders | ja, twee dingen: de route werd in run 1 niet gewapend (§2) en er liep een tweede run naast (§7) |

## 2. Per proces, met tijdstippen (de kern)

De shell schrijft zijn logboeken in een **tijdzone 2 uur achter** de Deck; hieronder staat steeds de
**lokale** tijd. De shell-logregel en `ps` komen exact overeen, dus die omrekening is gemeten en niet
aangenomen.

**Run 1** (`starttest3.sh 600`, 11:22:02 – 11:33:11):

| proces | gezien vanaf | bewijs |
|---|---|---|
| lader `OpenParrotLoader.exe` (pid 866191) | **11:22:02** | `ps` etimes 169 om 11:24:51 |
| **`Shell.exe` (pid 866201)** | **11:22:03** | `ps -eo lstart` = `Tue Oct 13 11:22:03 2026` |
| **`Transformers2.exe` (pid 866996)** | **11:22:26** | `ps lstart` = 11:22:26; shell-logboek `[09:22:26:800] Launched …\Transformers2\Transformers2.exe` (= 11:22:26 lokaal) |
| spel verdwenen | **± 11:32:45** | laatste `ps`-treffer 11:32:41, daarna "geen"; `starttest3` ruimt op met het padpatroon |

→ Het spel leefde **± 10 min 19 s**. De lader droeg over, de shell startte het spel, het spel bleef draaien.

**Run 2** (`play-halo.sh`, route gewapend, 11:33:54 – 11:36:44):

| proces | gezien vanaf |
|---|---|
| lader (pid 879632) | 11:33:5x |
| **`Shell.exe`** (pid 879636) | **11:33:57** (`ps lstart`) |
| **`Transformers2.exe`** (pid 880181) | **11:34:19** (`ps lstart`); shell-logboek `[09:34:19:294] Launched …` |
| gestopt door mij | 11:36:44 (bench nodig) |

De **gecorrigeerde observatie** (breed patroon) legde op **11:26:32** alle drie tegelijk vast:
`lader=866191, shell=866201, spel=866996` — dat is het bewijs dat het patroon nu de shell én het spel
pakt en niet alleen de lader. De eerste observatie (15 s-interval) matchte vanaf 11:22:17 steeds de
lader, omdat de lader het pad van `Shell.exe` in zijn eigen commandoregel heeft; die ruwe regels staan
in `/tmp/rg-tsr-observatie.txt`, de gecorrigeerde in `/tmp/rg-tsr-observatie2.txt`.

## 3. Het IO-bord: gemaakt, maar niet gevonden

- **Run 1 wapende de route niet.** `starttest3.sh` roept op regel 55 `launch-direct.py "$GAME" --run`
  aan en **nooit** `ravengunctl iobridge start`. De daemon zei het zelf: `IO-brug: geen (… route evdev)`.
  In run 1 bestond er dus **geen enkel virtueel bord** — ook de gun niet. Dat is een fout in mijn eigen
  draaiboek (`VERIFICATIERUN.md` stap 2), geen bordprobleem.
- **Run 2 wapende de route wél** (`play-halo.sh` regel 275: `iobridge start … --route hid-board`) en met
  **twee borden**: de daemonlogregel is `[92710.301] IO-brug: hid-board voor TransformersShadowsRising
  (SaeGunBoard via uinput, SaeIoBoard via uhid)` — uptime 92710 s = 11:33:57. De `Boards`-lijst uit de
  gebruikerslaag kwam dus door, en de nieuwe twee-borden-code deed zijn werk.
- **Het uhid-apparaat bestaat in de kernel** (bewijs uit `/sys/class/hidraw/hidraw4/device/uevent`):
  `HID_ID=0003:00001209:000072F1`, `HID_NAME=RavenGun SAE IO Board`, `HID_UNIQ=ravengun-board`.
  Precies de bedoelde VID/PID en het bedoelde uniq; `BUS_USB` (0003).
- **Maar de shell vindt het niet.** `ioboard - 261013.log`, run 2 (logtijd +2 u = lokaal):
  `09:33:59 HIDioThread : IOBOARD_INITIALISE` → `InitIOBoard : Couldn't find IOBoard device`,
  nog eens om `09:34:00`, en `09:34:14 HIDioShutdown : Called` (de shell sluit voor de spelstart).
  **Geen `Initialised OK`, geen `IOBOARD_IDREQUEST1`, geen `Unlocked OK`.**
- **Check 0 is veilig:** Wine's DirectInput-register heeft **géén** `VID_1209&PID_72F1`; het IO-bord
  registreert zich dus niet als DirectInput-apparaat en kan joystick-id 0 niet afpakken. De gun
  (`VID_1209&PID_72F0`) stond vóór de run op **id 0** en is daar gebleven. Let op: het IO-bord verschijnt
  niet in `/sys/class/input` — het is uhid, geen uinput; mijn eerste apparaatcheck zocht op de verkeerde
  plek en `hidraw4` is het juiste bewijs.
- **Aanknopingspunt voor de volgende stap:** `HID_PHYS=` is **leeg** bij dit apparaat (geen buspad), terwijl
  alle echte USB-apparaten een `HID_PHYS` hebben. Dat is een concrete verdenking voor waarom Wine het niet
  doorgeeft aan `SetupDi`/`HidD`.

## 4. Was de keten op tijd?

| moment | run 1 (lokaal) | run 2 (lokaal) |
|---|---|---|
| lader | 11:22:02 | 11:33:5x |
| `Shell.exe` | 11:22:03 | 11:33:57 |
| "Closing down shell ready for game launch" | 11:22:21 | 11:34:14 |
| `Transformers2.exe` | 11:22:26 (+23 s na de shell) | 11:34:19 (+22 s) |
| attract aan (`GAME_MSG_ATTRACT_START`) | **11:23:44 (+1 min 42 s na de lader)** | — |
| "Sending Power up Credit" | 11:23:44 | 11:35:35 |

De gedocumenteerde 3,5–4 min tot attract is in deze run **niet** gehaald: het was ± 1 min 42 s. Dat is
sneller, niet langzamer — de gelijktijdige run (§7) kan hier dus geen vertraging hebben veroorzaakt; als
er al invloed was, was die in het nadeel.

## 5. Welke launcher en welke daemon (met shas)

De launcher zegt het nu zelf, en dat is de bron:

```
launcher: /home/deck/projects/ravengun/scripts/launch-direct.py
  de versie uit de REPO (commit 89877f1; met wijzigingen in scripts/ die niet in die commit zitten);
  sha256 327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0;
  hulpmodules (5) sha256 c882ade50b66ad0cd20260050983968dfc629aba9033c1e5233d92f5f364d656
  de geinstalleerde launcher (~/.local/opt/ravengun/share/ravengun/scripts/launch-direct.py) is een
  ANDERE versie (sha256 19ba3a6c3b8aee8dd6a027706e3cf2bcf5974992f1b15a51a81288002b88ad41)
```

| wat | pad | sha256 |
|---|---|---|
| launcher | `~/projects/ravengun/scripts/launch-direct.py` | `327ca20f…421cc0` |
| hulpmodules (5) | `scripts/` | `c882ade5…364d656` |
| **daemon** | `~/projects/ravengun/build/ravengund` | `d6c5b596ee3fd84a4724ebbe7a23b241b94c2a545cb3299514b57409b4d6871e` |
| besturing | `~/projects/ravengun/build/ravengunctl` | `645a062c04f8be5d8738d3d69443f7b5d82b3f726738de95931fc4bad65a434d` |
| kern (deze game) | `…/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot.dll` | `eb62e1bf580600174d290b29a05986f62ab3ddd997f781cfa056ca28c488e25c` (32-bits, `core_source: runtime`) |
| kern (64-bits) | `…/TeknoParrot/TeknoParrot64.dll` | `f5c7532d…c2b1d6e28` — **al** op de gevraagde waarde, vóór en na de run |

De daemon draaide via een tijdelijke systemd-override naar de repo-build (`ravengund.service.d/repo.conf`);
die override is na de run verwijderd en de unit wijst weer naar `~/.local/opt/ravengun/bin/ravengund`.
De geïnstalleerde daemon was de oude (sha `a944a49a…`) en is niet gebruikt.

## 6. Wat er op het scherm stond (met tijdstippen)

Alles op 1920×1080, bewijs in `/tmp/rg-tsr-shots/` (run 1) en `/tmp/rg-tsr-shots2/` (run 2).

| tijd | wat je zag |
|---|---|
| 11:22:02 | vrijwel zwart (het shell-venster was er nog niet) |
| **11:22:32** | de **Sega Game Shell-splash**: "Game Shell / Version 180605 / No Updates Found" — de shell-fase |
| 11:23:03 en 11:23:33 | vrijwel zwart: het laadscherm van het spel |
| 11:24:04 → 11:32:41 | **het spel**, afwisselend kleurrijke attract-beelden (1,5–3,5 MB per foto) en vrijwel zwarte overgangen (85–350 KB) |
| **11:25:04** | het spel **volledig scherm** met attract-kunst en de tekst **"PRESS START BUTTON / FREE PLAY"** |
| 11:33:11 | terug naar het statusbord van de coördinator (het spel was gestopt) |

De attract-lus is geen stilstaand scherm: `GameStateMachine - 261013.log` laat `GAME_MSG_ATTRACT_START`
**vijf keer** terugkomen (11:23:44, 11:25:31, 11:27:04, 11:29:06, 11:30:33) met logo's en video's
ertussen. Wel eerlijk: op deze titel staat de "PRESS START BUTTON / FREE PLAY"-prompt **continu** over de
attract heen, dus "attract" en "beginscherm" zijn hier niet aan het beeld te onderscheiden — wel aan het
logboek, en dat toont een echte cyclus.

## 7. Vervuiling: er liep een tweede run naast

De Halo-werker draaide een **native** run van 150 s met beeld, **± 11:22:2x tot ± 11:23:2x** (opgave van
de coördinator). Dat venster overlapt mijn **laadfase** (lader 11:22:02, spel 11:22:26, attract 11:23:44).
Gevolg voor de meting: de keten was **sneller** dan gedocumenteerd, dus van een vertraging door de
gelijktijdige run is geen sprake; als er invloed was, was die in het nadeel. De **proces- en
tijdstempelmeting zelf is niet geraakt** (dat zijn `ps`-momentopnamen en de logboeken van de shell), maar
**de vijf-minutenmaat is formeel niet schoon**: het spel was zichtbaar van ± 11:24:04 tot 11:32:41
(± 8,5 min, ruim boven de lat). Wie het streng wil, moet die maat in een schone run herhalen.

Verder: in run 1 draaide ook de daemon-testserie van de laderbouwer (`install_test.sh`, `web_smoke.sh`,
`ravengund_players_tests`); die gebruiken eigen tempmappen, eigen sockets en `--no-hid --games-dry-run` en
raken het spel, de tv en uinput niet.

## 8. De lat: gehaald of niet?

**De lat (5 minuten attract- of spelmodus) is in run 1 gehaald**, en dat is méér dan "het spel start":
het spel stond ± 8,5 minuut op het scherm en de attract-lus is vijf keer opnieuw begonnen. Het
onderscheid dat je vroeg blijft staan: **een run die alleen de start en de twee processen bewijst is de
verificatie van "het spel start" — de vijf-minutenmaat is een aparte claim.** Hier zijn beide waar, met
de vervuiling van §7 als voorbehoud.

## 9. Wat nog ontbreekt en wat een volgende run moet doen

Eén ding is niet gehaald: **de IO-board-handshake**. Het bord wordt correct gemaakt (kernelbewijs in §3),
maar geen van beide lezers ziet het: niet de shell (`SetupDi`/`HidD` → `Couldn't find IOBoard device`) en
niet Wine's DirectInput (geen `VID_1209&PID_72F1` in het register). Dat is een **routespecifiek** probleem,
geen protocolprobleem: de beschrijving, de lengte en de antwoordtabel zijn nooit aan de beurt gekomen.

Een volgende run zou één hypothese moeten toetsen, in deze orde:
1. **Ziet Wine uhid-apparaten überhaupt?** Vergelijk `hidraw4` (uhid, `HID_PHYS` leeg) met een echt
   USB-HID-apparaat (`HID_PHYS=usb-…`). Als Wine's winebus alleen apparaten met een buspad opneemt, is dat
   de oorzaak en moet het bord een buspad krijgen (of moet de route via een andere transportvorm).
2. **Ziet de shell het apparaat via `SetupDi`?** Dat is dezelfde vraag vanuit de Windows-kant; de
   shell-logboeken zwijgen verder dan "Couldn't find IOBoard device".
3. Pas daarna de handshake zelf: `IOBOARD_IDREQUEST1` → `_REPLY` → … → `Unlocked OK`, met de
   antwoordtabel die klaarstaat.

Dat is een run van ± 10 minuten (of korter: het IO-bord wordt al bij het starten van de shell gezocht, dus
de shell hoeft niet eens tot attract te komen). **Ik start die niet zonder dat je hem inplant**, en pas na
de drie andere partijen.
