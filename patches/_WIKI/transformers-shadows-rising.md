# Transformers: Shadows Rising (Sega RingEdge 2) -- publieke digest

Dit is een technisch verslag van wat wij van dit spel hebben gemeten en geleerd, geschreven voor
iemand die dit project **niet** kent en er een eigen front-end of emulator op wil bouwen. Per bewering
staat of hij **GEMETEN** is (met cijfers, tijdstippen, paden, offsets) of **AFGELEID**.

Alles hier is op een Steam Deck (SteamOS) onder plain Wine gemeten. De spelbestanden staan op een
NAS en zijn daar **alleen gelezen**.

| | |
|---|---|
| Spel | Transformers: Shadows Rising |
| Platform | Sega RingEdge 2, "Game Shell"-opbouw |
| Jaar | 2018 |
| Soort | lightgun-shooter, 2 spelers |
| Map op onze share | `Teknoparrot/Transformers Shadows Rising` (en als kopie `Teknoparrot/Sega`) |
| Bouw | `shell/Shell.exe` (Game Shell 180605) start `Transformers2/Transformers2.exe` |
| Onze stand | **speelbaar met een PlayStation Move als geweer**; de vijf-minutenmaat is gehaald (zie §8) |
| Niet af | het cabinet-IO-bord (lampen, toetsenpaneel, hardware-recoil) — zie §5 en §8 |

**Leeswijzer bij de markeringen.** **GEMETEN** = afgelezen uit een bestand, een logboek of een
procesmeting, met het bewijs erbij. **AFGELEID** = een gevolgtrekking uit die metingen; bruikbaar, maar
geen bewijs. Getallen staan er altijd bij.

---

## 1. Platform en kast

**Het platform (GEMETEN).** Het spel is een Sega RingEdge 2-titel uit 2018 die als **"Game Shell"** is
gebouwd: geen enkel programma, maar een kleine Sega-omgeving (`shell/`) die het eigenlijke spel
(`Transformers2/`) start en daarna in de gaten houdt. Beide zijn 32-bits Windows-programma's (PE32, i386):

| bestand | grootte | rol |
|---|---|---|
| `shell/Shell.exe` | 1.438.720 B | de Game Shell: leest de installatie, start het spel, houdt de toestand bij |
| `Transformers2/Transformers2.exe` | 9.332.736 B | het spel zelf (venstertitel `TF2_Gun \| Release`) |
| `Launcher/SystemLauncher.exe` | 514.048 B | Sega's eigen startprogramma; leest `Launcher/Launcher.ini` (`ShellDirectory=c:\sega\shell`) |

De shell verwacht een vaste installatiemap: `Install.ini` van de shell zegt `Directory=C:\Sega\Shell` en
die van het spel `Directory=C:\Sega\Transformers2` (beide `Version=180605`, `Manufacturer` respectievelijk
"Hollywood Gaming" en "SEGA Amusements"). `shell/Game.ini` zet bovendien `AssetDir=C:\sega\shelldata`.
**GEMETEN** uit die ini-bestanden.

**De twee processen (GEMETEN).** De lader start **één** proces, `Shell.exe`. Dat programma start op zijn
beurt `Transformers2.exe` en blijft daarna als begeleider leven. Er is dus geen "twee exe's starten": het
is een keten van twee. In `Shell.exe` zit de HID- en SetupDi-code (zie hieronder); `Transformers2.exe`
heeft die **niet** — het leest zijn geweer via DirectInput.

**De bordopbouw van de kast (GEMETEN).** De kast heeft twee invoerapparaten die niets met elkaar te maken
hebben, en dat is voor een emulator het belangrijkste om te weten:

1. **Het geweer** — een **DirectInput-joystick**. `Transformers2.exe` opent de eerste joystick van het
   systeem (`CreateDevice(GUID_Joystick)`, zonder filter op fabrikant, product of naam), zet elke as op het
   bereik −1000…1000 en leest een `DIJOYSTATE` van 80 bytes. De horizontale positie komt uit as `lY` en de
   verticale uit `lX` (speler 2: `lRx` horizontaal, `lZ` verticaal); de game rekent om met
   `(1000 − waarde) × 255 / 1000`, dus links/boven is de lage kant. In de linkermap van het spel heet die
   klasse `CSAEBoard` (object `saebd.obj`). **GEMETEN** uit de code en bevestigd op de tv.
2. **Het cabinet-IO-bord** — een **HID-apparaat**. `Shell.exe` importeert `HID.DLL`
   (`HidD_GetHidGuid`, `HidD_GetAttributes`, `HidD_GetPreparsedData`, `HidP_GetCaps`) en `SETUPAPI.dll`
   (`SetupDiGetClassDevsA`, `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
   `SetupDiDestroyDeviceInfoList`) en zoekt daarmee een HID-apparaat. Dit bord stuurt de lampen, het
   externe toetsenpaneel en de hardware-recoil. **GEMETEN** uit de importtabellen.

**Hoe de shell dat IO-bord herkent (GEMETEN).** Niet op VID/PID en niet op een naam: de shell wandelt een
ingebouwde tabel af die telkens een paar (HID-usagepagina, HID-usage) op een naam afbeeldt
(`GAMEPAD`, `JOYSTICK`, `SYSTEM`, `TRACKBALL`, `KEYBOARD`) en schrijft zijn rapporten naar het apparaat dat
als **Generic Desktop (0x0001) / Game Pad (0x0005)** doorkomt. Daarna schrijft hij **3 bytes** naar het
bord in de vorm `00 <a> <b>` (rapport-ID 0, daarna twee gegevensbytes); de lengte die hij schrijft is
`HidP_GetCaps().OutputReportByteLength`. Er is ook een letterlijke naam (`POLLING FLAGS`) in die code, maar
die tak is dood: de vergelijking kan nooit waar zijn en de uitkomst wordt nergens gelezen. Wie dit bord
wil namaken moet dus **de usage** kloppend maken, niet een naam of een VID/PID. **GEMETEN** (drie
onafhankelijke bewijzen in ons dossier).

**AFGELEID.** `Transformers2.exe` bevat daarnaast code voor LED-tracking en voor een camera-model
(`LED.obj`, `TF2LED.obj`, `spk_camera_motion_nng20.obj` in de linkermap, en `Sega\libs\HookedWindows.txt`
noemt een "Ring Gun (NNDXG20-WIN)" en "TF Gun (NNDXG20-WIN)"). Dat wijst op een infrarood-camera-geweer
waarvan de ruwe LED-posities naar de pc gaan. In onze opzet wordt die weg niet gebruikt — wij voeden de
DirectInput-joystick — maar wie een echte kast wil nabootsen moet weten dat die code er is.

---

## 2. Wat het spel nodig heeft

**De bestanden (GEMETEN).** De spelmap is compleet. Eén kopie telt **1.748 bestanden**: bovenaan
`GameData`, `Launcher`, `ShellData`, `Transformers2`, `shell` en `temp`. Let op een eigenaardigheid van
onze share: in de map `Transformers Shadows Rising` staat de hele boom **twee keer** — één keer bovenaan
en nog eens onder `Sega/` — waardoor `find` er **3.498 bestanden** en `du` **18 GB** telt. Het is een echte
tweede kopie (verschillende inodes), geen hardlink. Het spel heeft er geen last van, maar wie de cijfers
narekent moet dat weten.

De enige twee lege bestanden in de boom zijn **geen defect**:
`Transformers2/TF2_Gun_R.ilk` (een overblijfsel van de incremental linker) en `shell/install.crc`. De
`Install.ini` van de shell noemt `FilesCount=69`, die van het spel `FilesCount=359` — dat zijn de tellingen
van de installateur zelf, niet van het bestandssysteem.

**De spelladder (GEMETEN).** Wat er in volgorde gebeurt, met de bestanden die dat laten zien:

1. De shell leest zijn `Install.ini`/`Game.ini`, controleert of de map `GameData` bestaat
   (`Mainthread`-logboek: `Validated GameData directory exists`) en zet zijn venster op 1920×1080.
2. De shell sluit zichzelf af met de regel **`Closing down shell ready for game launch`** en start dan
   `Transformers2.exe` vanaf dezelfde share; in het logboek staat letterlijk
   `Launched z:\home\deck\mnt\spidernas_emulation\teknoparrot\sega\\Transformers2\Transformers2.exe`.
3. Het spel laadt, toont eerst een zwart laadscherm en komt daarna in zijn **attract-lus**.
4. De shell blijft daarna leven als begeleider: hij vertaalt de berichten van het spel (`GAME_MSG_*`:
   attract, stage, score, health, `GAME_MSG_TRIGGER_RECOIL`) naar de kast — lampen, paneel, recoil.

**Wat de shell aan het spel doorgeeft (GEMETEN).** Beide kanten gebruiken dezelfde berichtenwoordenschat
(`GAME_MSG_ATTRACT_START`, `GAME_MSG_STAGE_START`, `GAME_MSG_CURRENT_SCORE`, `GAME_MSG_CURRENT_HEALTH`,
`GAME_MSG_TRIGGER_RECOIL`, enzovoort) — dat is het kanaal tussen de twee processen. De shell stuurt ook
`MAINTHREAD : Sending Power up Credit` als het spel eenmaal draait. De namen staan in beide binaries, wat
bevestigt dat het om één protocol tussen twee Sega-processen gaat en niet om iets van ons.

**Wat het spel ZELF nodig heeft (GEMETEN).**

* `Transformers2.exe` **alleen** starten werkt niet: het blijft meer dan 50 seconden op
  `SYSTEM INITIALIZING... LOADING..` staan. De shell is dus verplicht, niet optioneel.
* Onder Wine zijn in de Wine-prefix **native** `d3dx9_43`, `d3dcompiler_43` en `d3dcompiler_47` nodig.
  Zonder die drie stopt het spel met een Windows-foutvenster:
  `E5017: Aborting due to not yet implemented feature: Writing fx_2_0 sampler objects initializers is not
  implemented / Write pass assignments` — de HLSL-effectcompiler van Wine kan dit shader-effect niet
  compileren, de Microsoft-versie wel.
* Het spel is 32-bits en gebruikt dus de **32-bits** TeknoParrot-kern (`TeknoParrot/TeknoParrot.dll`, niet
  de 64-bits variant). **GEMETEN** via `launch-direct.py --plan` (`is64: False`, `core_dll: …`).
* De mapstructuur moet kloppen: de shell werkt met `C:\Sega\shell` en `C:\Sega\Transformers2`. In onze
  prefix is `drive_c/Sega` een symlink naar de map op de share.

---

## 3. Het officiële startscript (of het ontbreken daarvan)

**Er is geen officieel batch- of shellscript (GEMETEN).** In de hele spelmap (3.498 bestanden) staat geen
enkele `.bat`, `.cmd` of `.sh`. De officiële startweg van Sega is een programma, geen script:

```
Launcher/SystemLauncher.exe   (leest Launcher/Launcher.ini: ShellDirectory=c:\sega\shell)
        -> shell/Shell.exe    (Game Shell 180605)
             -> Transformers2/Transformers2.exe
```

Vergelijk dat met de zustertitel *Transformers: Human Alliance* (`Teknoparrot/Transformers Human
Alliance`), waar wél een `game.bat` ligt die eerst een hardwarecontrole doet en daarna het spel start —
zulke scripts bestaan dus wel in deze familie, maar niet voor Shadows Rising.

**Waarschuwing, en die geldt voor elke titel in deze reeks: wij voeren zulke scripts nooit uit.** Een
startscript van een fabrikant is een programma dat we niet kennen; het kan hardware checken, bestanden
aanraken of procescontroles doen die wij niet kunnen overzien. Wij lezen zo'n script alleen om te zien
**wat** het doet (welke exe, welke argumenten, welke hulpbestanden, welke omgevingsvariabelen) en nemen
daarvan alleen de delen over die we zelf nodig hebben. Voor deze titel was dat niets: er is geen script, en
de startweg hierboven is met `Launcher/Launcher.ini` en de `Install.ini`-bestanden volledig te
reconstrueren.

---

## 4. De route die wij gebruiken

Wij starten het spel met een eigen lader en een eigen invoerbrug, zonder de TeknoParrot-UI. De lader is
`scripts/launch-direct.py`; hij leest het TeknoParrot-profiel en start `Shell.exe` met de juiste argumenten
en omgevingsvariabelen. Het profiel (`GameProfiles/TransformersShadowsRising.xml`, revisie 10) zegt:

| veld | waarde | betekenis |
|---|---|---|
| `EmulatorType` | `TeknoParrot` | de lader plus de kern, niet één van de andere emulatortypes |
| `EmulationProfile` | `TransformersShadowsRising` | de naam van het profiel |
| `ExecutableName` | `shell.exe;Transformers2.exe` | de **namenlijst** van het profiel |
| `GamePath` / `[Identify] Path` | `shell/Shell.exe` | wat er werkelijk gestart wordt |

**BELANGRIJK, en dit is een valkuil (GEMETEN).** Die twee namen in `ExecutableName` zijn **niet** het
"twee uitvoerbare bestanden"-mechanisme van de lader. Dat mechanisme heet `HasTwoExecutables` met een
`Executable2`, wordt gebruikt voor een spel met een los hulpprogramma ernaast, en onze lader
**weigert** zulke profielen. Bij deze titel is `ExecutableName` alleen de namenlijst uit het
TeknoParrot-profiel; de lader start **één** proces (`shell/Shell.exe`) en de **shell** start daarna zelf het
spel. Precies daarom start deze titel wel en bijvoorbeeld *HOTD Scarlet Dawn* (dat `HasTwoExecutables`
gebruikt) niet met de directe route.

**De invoerbrug en de borden.** Onze daemon maakt de invoerapparaten na die de game verwacht. De borden
staan als data in `packaging/hid-boards.json` (geen code per spel):

| bord | transport | wat het is |
|---|---|---|
| `SaeGunBoard` | `uinput` (evdev) | het geweer: een virtuele joystick die DirectInput ziet. Assen `X = p1.y`, `Y = p1.x`, `Z = p2.y`, `RX = p2.x`, met een uitvoerbereik van −217…557 binnen −1000…1000 (dat is het deel van het bereik dat de game als het hele scherm ziet) |
| `SaeIoBoard` | `uhid` | het cabinet-IO-bord: een echt HID-apparaat, Generic Desktop / Game Pad, vast inputrapport van 15 bytes en een uitvoerrapport van 3 bytes |

Beide borden kunnen tegelijk, met een **lijst** in de spelinstellingen: `Boards = SaeGunBoard, SaeIoBoard`
(zie §5 voor wat daarvan meetbaar werkt).

**De voorbereidingsstap (GEMETEN, en verplicht).** Het IO-bord gebruikt `/dev/uhid`, en dat is op Linux
standaard alleen voor root beschrijfbaar. Zonder schrijfrecht **faalt dat bord en daarmee de hele route —
ook het geweer**, want een bord dat niet opent laat de route stoppen. Controleer het dus vóór elke run:

```sh
# schrijfrecht op /dev/uhid, en of de udev-regel er staat
bash scripts/udev-ravengun.sh --check          # noemt 71-ravengun-uhid.rules als actueel
ls -l /dev/uhid                                 # moet schrijfbaar zijn voor de gebruiker
sudo bash scripts/udev-ravengun.sh --apply      # eenmalig, als de regel ontbreekt
```

**De gebruikerslaag.** Het meegeleverde profiel van een spel staat in `patches/<Spel>/game.ini`. Een
tweede laag, `~/.local/share/ravengun/patches/<Spel>/game.ini`, gaat daar **per sleutel** overheen: je kunt
één regel toevoegen of veranderen zonder de meegeleverde stand aan te raken, en het bestand weghalen zet
alles terug. Voor deze titel gebruikten wij dat om het IO-bord erbij te zetten:

```ini
; ~/.local/share/ravengun/patches/TransformersShadowsRising/game.ini
[Input]
Boards = SaeGunBoard, SaeIoBoard
```

**De exacte opdrachten (dit is de werkende weg).**

```sh
# 1. wat gaat de lader doen? (exe, omgeving, prefix, welke kern, welke launcher-versie)
python3 scripts/launch-direct.py TransformersShadowsRising --plan

# 2. de run zelf -- LET OP: deze, en NIET een losse starttest
bash scripts/play-halo.sh --profile TransformersShadowsRising --no-wait-move
```

Twee dingen die we hiervan hebben geleerd en die je één meting kunnen kosten:

* **`play-halo.sh` wapent de invoerroute; een losse starttest doet dat niet.** `scripts/starttest3.sh`
  roept alleen `launch-direct.py … --run` aan en nooit `ravengunctl iobridge start`. Met die losse
  starttest bestaat er dus **geen enkel virtueel bord** — de daemon zegt het zelf:
  `IO-brug: geen (… route evdev)`. Het spel start dan wel, maar er is geen geweer en geen IO-bord.
* **Gebruik de lader uit de repo, niet een geïnstalleerde oude kopie.** `launch-direct.py --plan` zegt nu
  zelf welke launcher draait, met pad en sha256, en of de geïnstalleerde kopie een andere versie is. Wij
  gebruikten de repo-versie: `scripts/launch-direct.py`, sha256
  `327ca20f593e39af4c2c324db8232fe81b83734556d60382fffa379542421cc0` (git `89877f1`), met de daemon uit
  de repo-build (`build/ravengund`, sha256
  `d6c5b596ee3fd84a4724ebbe7a23b241b94c2a545cb3299514b57409b4d6871e`). **GEMETEN.**

---

## 5. Invoer

### 5a. Het geweer — dit werkt (GEMETEN)

* De game leest de **eerste joystick van het systeem**. Wie een eigen apparaat maakt moet dus zorgen dat
  dat apparaat joystick-id 0 krijgt; anders pakt de game een ander apparaat (bij ons in eerste instantie
  de PlayStation Move zelf, die Wine ook als joystick ziet) en staat het vizier in de hoek.
* Ons bord heet `RavenGun SAE Gun Board`, heeft vendor/product **0x1209/0x72F0** en komt via `uinput`
  (evdev) naar buiten; Wine maakt daar zelf een HID-apparaat van (`HID\VID_1209&PID_72F0`).
* Wine onthoudt joystick-id's per product in het register onder
  `HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput\VID_xxxx&PID_xxxx\Calibration\N`,
  waarde `Joystick Id`. Een hulpscript (`scripts/wine-joyid.sh`) zet dat goed.
* **De check die je vóór elke run doet** (dit is de eerste vraag, vóór je naar het spel of het IO-bord
  kijkt):

  ```sh
  RG_WINE=~/.local/share/ravengun/wine/proton-11.0-2/bin/wine
  WINEPREFIX=~/.local/share/ravengun/prefixes/ravengun-test-tsr WINEDEBUG=-all "$RG_WINE" reg query \
    'HKCU\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\DirectInput' /s \
    | grep -E 'Calibration|Joystick Id'
  ```

  Verwacht: `VID_1209&PID_72F0` op **0**. Klopt het niet, dan zie je een vizier in de hoek en doet de
  X-as niets — en dat is een joystick-id-probleem, geen bordprobleem. Herstel:
  `scripts/wine-joyid.sh <prefix> 1209:72F0` (zetten, niet lezen).
* Het bereik: de game zet zelf elke as op −1000…1000 en haalt het resultaat daarna door zijn eigen
  kalibratie. Met het volle bereik plakt het vizier tegen de randen; met een uitvoerbereik van −217…557 in
  ons bord zat het vizier op alle rasterpunten **binnen 1,5 % van het scherm**. **GEMETEN** op de tv
  (2026-10-11).

### 5b. Het cabinet-IO-bord — dit werkt NOG NIET, en hier ligt de sleutel (GEMETEN)

Dit is het eerlijkste en het nuttigste stuk van deze pagina, want het is precies waar een volgende
poging moet beginnen.

* **Wat wél lukt:** onze daemon **maakt** het bord correct aan. Het bestaat in de kernel:

  ```
  /sys/class/hidraw/hidraw4/device/uevent
  HID_ID=0003:00001209:000072F1
  HID_NAME=RavenGun SAE IO Board
  HID_UNIQ=ravengun-board
  ```

  (`0003` = BUS_USB, vendor `1209`, product `72F1`.) De daemon logt de route ook met beide borden:
  `IO-brug: hid-board voor TransformersShadowsRising (SaeGunBoard via uinput, SaeIoBoard via uhid)`.
* **Wat NIET lukt:** `Shell.exe` vindt het bord niet. Zijn eigen logboek zegt het letterlijk:

  ```
  [ 09:33:59 ] HIDioThread : IOBOARD_INITIALISE
  [ 09:33:59 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:00 ] HIDioThread : IOBOARD_INITIALISE
  [ 09:34:00 ] InitIOBoard : Couldn't find IOBoard device
  [ 09:34:14 ] HIDioShutdown : Called
  ```

  Hij probeert het twee keer, geeft het dan op, en er komt **geen** `Initialised OK` en geen vervolg van
  de borddialoog. (De shell start deze zoektocht meteen bij het opstarten, vóór het spel begint — het bord
  moet er dus al zijn als de shell begint.)
* **Geen van beide lezers ziet het bord.** Niet de shell via zijn `SetupDi`/`HidD`-weg (hierboven), en ook
  Wine's DirectInput niet: in het register staat **geen** `VID_1209&PID_72F1`. Dat laatste is meteen goed
  nieuws voor §5a: het IO-bord kan joystick-id 0 dus niet afpakken van het geweer.
* **DE AANWIJZING VOOR WIE DIT AFMAAKT:** in `/sys/class/hidraw/hidraw4/device/uevent` is **`HID_PHYS=`
  leeg**. Elk echt USB-HID-apparaat in dezelfde lijst heeft daar een buspad
  (`HID_PHYS=usb-0000:0c:00.0-3/input0`, enzovoort). Een `uhid`-apparaat heeft geen fysieke bus, en dat is
  een concrete verdenking voor waarom Wine het apparaat niet opneemt in zijn HID-verzameling en waarom de
  `SetupDi`-enumeratie van de shell niets oplevert. Wie dit bord werkend wil krijgen, moet dus eerst
  uitzoeken **of Wine apparaten zonder buspad überhaupt doorgeeft** — en zo niet, of dat op te lossen is
  (een apparaat met een gesimuleerd buspad, of een andere transportvorm) voordat er ook maar iets aan het
  protocol hoeft te gebeuren.
* **Wat er daarna klaarstaat:** de bordbeschrijving (usage Game Pad, vast inputrapport van 15 bytes,
  uitvoerrapport van 3 bytes waarvan de vorm `00 <a> <b>` is gemeten) en de antwoorden die de shell per
  stap verwacht. Dat is allemaal uit `Shell.exe` gehaald en staat in ons lokale dossier; de openbare
  kern ervan staat in §1 en §8.
* **Praktisch gevolg in onze opzet:** zolang dit niet werkt, is het IO-bord optioneel en moet het
  **kunnen falen zonder het geweer mee te nemen**. Daarom staat het in de gebruikerslaag (§4) en niet in
  het meegeleverde profiel: één bestand weghalen en de titel is weer zoals hij was.

---

## 6. Timing

**GEMETEN**, met de klok van de machine. Let op: de logboeken van de **shell** schrijven in een tijdzone
**2 uur achter** de Deck; hieronder staat alles in lokale tijd, en de shell-regel en de procesmeting
komen exact overeen (dat is dus gecontroleerd, niet aangenomen).

| moment | run 1 | run 2 |
|---|---|---|
| lader (`OpenParrotLoader.exe`) gezien | 11:22:02 | 11:33:5x |
| `Shell.exe` gezien | **11:22:03** | **11:33:57** |
| "Closing down shell ready for game launch" | 11:22:21 | 11:34:14 |
| `Transformers2.exe` gezien | **11:22:26** (+23 s na de shell) | **11:34:19** (+22 s) |
| attract begint (`GAME_MSG_ATTRACT_START`) | **11:23:44** (+1 min 42 s na de lader) | — |
| "Sending Power up Credit" | 11:23:44 | 11:35:35 |
| spel gestopt (einde meting) | ±11:32:45 → **±10 min 19 s in leven** | 11:36:44 (meting afgebroken) |

Dus: **van shell naar spel is het 22 tot 23 seconden**, en de attract is er **ongeveer 1 minuut 42** na de
lader. De vaak genoemde "3,5 tot 4 minuten laden" hebben wij in deze runs **niet** gezien; het was
duidelijk sneller (vermoedelijk warme cache). Reken voor een koude machine op enkele minuten en meet het
zelf.

---

## 7. Wat we op het scherm zagen

**GEMETEN** met schermafbeeldingen (1920×1080) op vaste tijden, plus de logboeken van de shell. De
bestandsgrootte van zo'n foto is een ruwe maat voor "hoeveel er te zien is": een bijna zwart beeld is
10–130 kB, een echt spelbeeld 1,5–3,5 MB.

| tijd | wat er op de tv stond | fotogrootte |
|---|---|---|
| 11:22:02 | vrijwel zwart — het shell-venster was er nog niet | 11,9 kB |
| **11:22:32** | **de Sega Game Shell-splash**: "Game Shell / Version 180605 / No Updates Found" | 1,6 MB |
| 11:23:03 en 11:23:33 | vrijwel zwart: het laadscherm van het spel | 11,5 kB |
| 11:24:04 → 11:32:41 | **het spel**, afwisselend kleurrijke attract-beelden en zwarte overgangen | 85 kB – 3,5 MB |
| **11:25:04** | het spel **volledig scherm** met attract-kunst en de tekst **"PRESS START BUTTON / FREE PLAY"** | 3,25 MB |
| 11:33:11 | terug naar het bureaublad van de Deck (het spel was gestopt) | 127 kB |

De volgorde is dus: **shell-splash → zwart laadscherm → attract**. Wat je op de foto's ziet wisselt om de
ongeveer 30 seconden tussen een kleurrijk beeld en een bijna zwart tussenbeeld; dat is de attract-lus die
logo's en video's afwisselt.

---

## 8. Stand van zaken en open vragen

**Wat gehaald is (GEMETEN).** De maat die wij per titel aanhouden is **vijf minuten attract- of spelmodus**,
en die is voor deze titel **GEHAALD**: de attract begon om **11:23:44** en het spel stond zichtbaar op het
scherm tot **11:32:41** — ongeveer **8,5 minuut** — terwijl de toestandsmachine van de shell
(`GameStateMachine`) de lus **vijf keer** opnieuw opstartte (11:23:44, 11:25:31, 11:27:04, 11:29:06,
11:30:33).

**Eerlijke kanttekening bij die maat.** Op deze titel staat de tekst "PRESS START BUTTON / FREE PLAY"
**continu** over de attract heen. Aan het beeld alleen is "attract" dus niet te onderscheiden van "het
beginscherm"; het **logboek** toont de echte cyclus (vijf keer opnieuw beginnen, met logo's en video's
ertussen). Wie de maat streng wil, moet dat via het logboek beoordelen en niet via één foto. En de meting
was niet schoon: zie §9.

**Wat nog open is.**

1. **Het cabinet-IO-bord** (§5b). Het bord wordt gemaakt, maar geen van beide lezers ziet het. De vraag is
   of Wine HID-apparaten zonder buspad doorgeeft; `HID_PHYS=` is daar de aanwijzing.
2. **De borddialoog** erna: welke stappen de shell na het vinden zet en welke antwoorden hij verwacht. Dat
   is uit de binary gehaald en voorbereid, maar nooit in het echt gelopen — eenvoudigweg omdat stap 1 nog
   niet lukt.
3. **Lampen, toetsenpaneel en hardware-recoil**: dat is wat dit bord uiteindelijk stuurt, en dat is dus
   automatisch nog onbevestigd. Let op: de bediening van het **toetsenpaneel** hangt bij deze titel
   bovendien af van een instelling in `shell/Game.ini` van het spel zelf; zonder die instelling stopt de
   borddialoog na het vrijgeven en komt het paneel nooit aan de beurt. Dat is geen fout van het bord.
4. **De tweede speler** en het kalibratiescherm van de game zijn met de Move nog niet uitputtend getest.
5. **De procesobservatie in onze eigen hulpmiddelen**: ons eerste observatiescript matchte alleen de
   **lader**, omdat de lader het pad van `Shell.exe` in zijn eigen commandoregel heeft. Het tweede,
   bredere patroon pakt de shell en het spel wel (zie §9).

---

## 9. De les over de twee runs

Wij deden twee runs. Ze verschillen, en dat verschil is leerzaam voor iedereen die dit spel gaat
automatiseren.

**Run 1 deed iets niet.** We gebruikten het eenvoudige starttest-hulpmiddel van het project
(`scripts/starttest3.sh`), in de veronderstelling dat dat hetzelfde deed als onze gewone speelweg. Het
roept echter alleen `launch-direct.py … --run` aan en **nooit** `ravengunctl iobridge start`. Gevolg: de
invoerroute werd nooit gewapend en er bestond **geen enkel virtueel bord — ook het geweer niet**. Het spel
startte wel, en dat is precies waarom dit zo makkelijk te missen is: de daemon zei het zelf
(`IO-brug: geen (… route evdev)`) en het spel kwam gewoon in zijn attract. Alleen wie naar de apparaten
kijkt ziet dat er niets is. **Dat was een fout in ons eigen draaiboek**, geen eigenschap van het spel, en
we hebben het draaiboek er daarna op gecorrigeerd. De les: een spel dat start betekent niet dat de
invoerweg ook bestaat — controleer de weg apart.

**Run 2 gaf de nieuwe data.** Die liep via de gewone speelweg (`play-halo.sh`), met de invoerroute
gewapend en **twee** borden, en leverde precies de dingen die run 1 niet kon laten zien:

* de daemon logt beide borden (`SaeGunBoard via uinput, SaeIoBoard via uhid`);
* het uhid-apparaat bestaat aantoonbaar in de kernel, met de bedoelde identiteit
  (`0003:00001209:000072F1`, `RavenGun SAE IO Board`);
* de joystick-id-check is gedaan (het geweer op 0, en het IO-bord komt niet als joystick opdagen);
* een **verbreed observatiepatroon** legde op 11:26:32 alle drie de processen tegelijk vast — lader,
  `Shell.exe` en `Transformers2.exe` — terwijl het eerste patroon alleen de lader zag;
* en de uitkomst dat het bord wél gemaakt maar niet gevonden wordt (§5b).

Meer tijd in run 1 had niets toegevoegd; de tweede run had een **andere opzet** nodig, niet meer tijd. Dat
is de bruikbaarste les van deze titel: als een meting "niets bijzonders" laat zien, controleer dan eerst of
je meetinstrument wel meet wat je denkt.

---

## 10. Wat is Linux-specifiek voor deze game

Wij draaien alles op **Linux** (SteamOS op een Steam Deck) onder **Wine**. Dat is geen detail: de twee
invoerapparaten van deze kast komen op Linux via twee verschillende kernel-koppelingen naar buiten, en de
spelinstellingen hebben op Linux een eigen laag. Per punt staat erbij of het **GEMETEN** is of een
**aanname**.

**10.1 Het geweer: `uinput` (GEMETEN).** Op Linux maken wij een virtuele joystick via `/dev/uinput`
(evdev). Wine zet die zelf om in een HID-apparaat (`HID\VID_1209&PID_72F0`, via winebus) en DirectInput
ziet een gewone joystick. Er is **geen** speciale toestemming nodig voor `/dev/uinput` en er is geen
udev-regel voor nodig: het apparaat krijgt de eigenschap `ID_INPUT_JOYSTICK` en de sessie krijgt er
automatisch toegang toe. **Windows-verschil:** daar is er geen `uinput`; je zou een virtuele
joystick-driver moeten installeren (zoals ViGEm) en dan hangt het er nog van af of die joystick als
DirectInput-apparaat verschijnt. Het joystick-id-verhaal van §5a is op Windows anders: Windows kent geen
"joystick-id 0" zoals Wine dat in het register bijhoudt, maar nummert de apparaten in de volgorde waarin
ze gevonden worden — dezelfde valkuil, andere plek.

**10.2 Het IO-bord: `uhid` + een udev-regel (GEMETEN).** Het cabinet-IO-bord moet een **echt** HID-apparaat
zijn, want de shell zoekt het via `SetupDi`/`HidD` (§1). Op Linux doen we dat met `/dev/uhid`, waar we de
rapportbeschrijving en de rapporten zelf naartoe schrijven. Twee Linux-eigenaardigheden:

* `/dev/uhid` is standaard `crw------- root root`, dus **alleen root**. Zonder maatregel faalt het bord —
  en omdat een bord dat niet opent de hele route laat stoppen, neemt het **ook het geweer** mee. De
  maatregel is een udev-regel (`packaging/udev/71-ravengun-uhid.rules`, inhoud:
  `KERNEL=="uhid", SUBSYSTEM=="misc", TAG+="uaccess"`), eenmalig te installeren met
  `sudo scripts/udev-ravengun.sh --apply`. Op deze machine is dat gedaan en is `/dev/uhid` schrijfbaar
  (nagemeten 2026-10-13).
* Het apparaat verschijnt als `hidraw` (`/sys/class/hidraw/hidraw4/...`) en **niet** in
  `/sys/class/input` — dat is precies het verschil met het geweer, dat wél een evdev-apparaat is. Wie het
  bord zoekt met `ls /sys/class/input` vindt het niet en concludeert ten onrechte dat het niet bestaat.
  **GEMETEN** (dat overkwam ons).

**10.3 Wat Wine vervolgens niet doet (GEMETEN).** Dit is het Linux-specifieke knelpunt van deze titel: het
`uhid`-apparaat bestaat wel in de kernel, maar Wine neemt het niet op. In het DirectInput-register van de
prefix staat **geen** `VID_1209&PID_72F1`, en de shell vindt het evenmin. Onze aanwijzing is dat
`HID_PHYS=` leeg is terwijl elk echt USB-HID-apparaat een buspad heeft — met andere woorden: Wine lijkt
apparaten zonder fysieke bus te negeren. Wie hier verder wil: dit is de eerste vraag, niet het protocol
(§5b).

**10.4 De gebruikerslaag (GEMETEN).** Op Linux kent onze lader twee instellingenlagen:
`patches/<Spel>/game.ini` (meegeleverd) en `~/.local/share/ravengun/patches/<Spel>/game.ini`
(gebruiker), die **per sleutel** over de eerste heen gaat. Daarmee zet je bijvoorbeeld het IO-bord erbij
zonder het meegeleverde profiel aan te raken, en één bestand weghalen zet het terug. Dit is een
Linux-eigen ontwerp van ons; op Windows zou je hier met kopieën van profielbestanden werken, of met de
gebruikersprofielen van de UI zelf.

**10.5 De paden en de schijfletter (GEMETEN).** De spelbestanden staan op een share en de lader start ze
**daarvandaan** — dat is het ontwerp, niet een omweg. In de Wine-prefix is dat zichtbaar als een
`Z:`-station met het volledige Linux-pad erin (`Launched
z:\home\deck\mnt\spidernas_emulation\teknoparrot\sega\\Transformers2\Transformers2.exe`), en
`drive_c/Sega` is een **symlink** naar de map op de share zodat de shell zijn vaste `C:\Sega\...`-paden
terugvindt. **Windows-verschil:** daar zou je een schijfletter toewijzen (`net use` of een subst) of de
bestanden lokaal zetten; de symlinkstruktuur is de Linux-variant van hetzelfde idee.

**10.6 De prefix en de DirectX-DLL's (GEMETEN).** Wij gebruiken één Wine-prefix per titel, met daarin
**native** `d3dx9_43`, `d3dcompiler_43` en `d3dcompiler_47` (§2). Zonder die drie stopt het spel met de
`E5017`-shaderfout. **Aanname:** dat is een eigenschap van Wine's HLSL-effectcompiler en niet van de
game — op Windows met de Microsoft-DirectX-runtime speelt dit probleem niet, daar zijn die DLL's precies
de versies die het spel verwacht.

**10.7 De launcher-identiteit (GEMETEN).** `launch-direct.py --plan` print welke launcher werkelijk
draait, met pad, sha256, git-commit en of de geïnstalleerde kopie een andere versie is. Dat is op Linux
extra belangrijk omdat dezelfde code op twee plekken kan staan (repo en installatie) en een oude kopie je
metingen kost. Op Windows zou hetzelfde probleem bestaan met een oude installatiemap; de maatregel is
dezelfde: laat het hulpmiddel zelf zeggen welke versie het is.
