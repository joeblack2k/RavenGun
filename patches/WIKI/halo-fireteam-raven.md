# Halo: Fireteam Raven — publiek digest

*Raw Thrills, 2018. Samengesteld door het RavenGun-project op SteamOS. Per bewering staat **GEMETEN**
(met cijfers, tijdstippen of bestanden) of **AFGELEID**. Waar iets niet bekend is, staat dat er ook.
Dit document gaat over het spel en de lader; het bevat met opzet niets over hardwarematige
beveiligings- of licentielagen.*

---

## 1. Platform en kast

* **GEMETEN:** het spel is een **64-bits Linux-ELF** (`ET_EXEC`, `EM_X86_64`), geen Windows-programma.
  Het draait op een Raw Thrills "PC-based" kast uit 2018 met een eigen Linux-image.
* **GEMETEN:** de spelmap op de kast is `/pm/g7/halo`, met daarin `game` (het spel), `game_original`
  (het origineel van vóór de aanpassing), `halo.so` (een meegeleverde koppeling-bibliotheek), `data/`
  (de speldata), `eFileMaps/` (het integriteitsmanifest), `fff4` en `utils/` (de kastscripts).
* **GEMETEN:** het spel is een **lichtgeweer-spel voor 1 tot 4 spelers**. De kastvarianten verschillen:
  `rtinit -ct` geeft code 9 (dual-screen 4p environmental, 3880x1080), 4 (mounted-gun 2p, 1920x1080),
  3 (super-deluxe 4p tethered, 1920x1080) of 8 (55" dedicated 2p tethered, 1920x1080).
* **GEMETEN (uit het spel zelf):** resolutie, speler en kasttype worden bij de start bepaald; het spel
  meldt zijn versie als `Welcome to Halo v03.11`, `Game Rev: 57529, Library Rev: 1412`.
* **AFGELEID:** de kast heeft per speler een geweer met een **optisch waarneembaar punt** en een
  **webcam**; het spel meet `brightness difference` in het camerabeeld om het geweer te vinden.

## 2. Wat het spel nodig heeft

* **GEMETEN:** 13 `DT_NEEDED`-bibliotheken: `libGL.so.1`, `libGLU.so.1`, `libpthread.so.0`,
  `libdl.so.2`, `libudev.so.1`, `liblapack.so.3`, `libiw.so.30`, **`libfmod.so.10`**,
  **`libfmodstudio.so.10`**, `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`.
  Op een gewone Linux-machine lossen 11 daarvan op; **`libfmod.so.10` en `libfmodstudio.so.10`
  ontbreken** (FMOD 1.10 is propriëtair). Het spel mist dan **alleen geluid**: een stub die de
  FMOD-namen exporteert en foutcode 52 teruggeeft is voldoende om door te starten.
* **GEMETEN:** er zit **geen** `libftdi`/`libusb` in `DT_NEEDED`; die zijn **statisch in `game`**
  gelinkt. Het spel lost zijn GL-/FMOD-/udev-aanroepen bovendien **zelf** op met `dlopen`/`dlsym`/
  `dlvsym` via een interne naamtabel (571 namen, 474 versietags) -- er staan geen `FMOD_*`- of
  `gl*`-symbolen in de eigen `.dynsym`.
* **GEMETEN:** het spel heeft `data/` nodig (o.a. `data/g7/*.g7`, `data/ssl/*.crt`, `data/snd`), het
  manifest `eFileMaps/` (7 kaarten) en schrijfacties in `pmuser/` (zie §7).
* **GEMETEN:** `game` is een **aangepaste** binary: 33.554.432 bytes (exact 32 MiB) tegenover
  `game_original` van 31.270.111 bytes; het startpunt is verlegd en het bestand is opgevuld. De
  integriteitscontrole van het spel meldt dat dan ook: `FileTest Failure: '/pm/g7/halo/game'`.
* **GEMETEN:** die controle is **niet fataal**: het spel meldt `FileTest Reported 1 errd files at
  startup!` en gaat verder zodra de speler START geeft (zie §5).

## 3. Het officiële startscript

* **GEMETEN:** `utils/run_game.sh` (en `utils/go.sh`) start het spel op de kast. De kernregels:
  ```sh
  cd /pm/g7/halo
  LD_PRELOAD=/pm/g7/halo/halo.so ${GAMEDIR}/game2 -f${SCREEN_RES} -n
  ```
  met daarvóór `utils/rtinit -ct` (bepaalt `SCREEN_RES`), `export DISPLAY=:0` en
  `pulseaudio_set_default.sh`; en daarna een `case` op de **exitcode** van het spel (222 = herstart,
  223 = reboot, 224 = aanpassing, 225 = factory reset, 226 = power-cycle).
* **GEMETEN:** `game2` bestaat niet in de map; de officiële start gebruikt hem, wij starten `game`
  (hetzelfde aangepaste beeld).
* **WAARSCHUWING -- voer dit script nooit uit.** Er staat letterlijk in: `rmdir /S /Q Y:\`
  (een schijf leegmaken), `reboot -f`, `sync`, `rm -rf ${USERDIR}` (de gebruikersmap weggooien) en in
  `go.sh` ook `passwd -l`, `swapoff -a` en netwerksetup. **Lees het script en neem alleen de nodige
  delen los over** (zoals de argumenten, de werkmap en een losse probe zoals `rtinit -ct`).

## 4. De route die wij gebruiken (native, zonder de gesloten lader)

* **GEMETEN:** het spel start direct via de **kernel-`ld.so`**, in een **bubblewrap**-sandbox:
  ```sh
  scripts/play-native.sh --game Halo --gamescope --mute --seconds 200 \
    --bind-ro <onze hulp>.so /rg/lib/<onze hulp>.so \
    --bind-ro <schakelaarbeeld> /rg/jvs --bind-rw <logmap> /rg/tap \
    --env LD_PRELOAD=/pm/g7/halo/halo.so:/rg/lib/librg_rt_input.so:<onze hulp>.so \
    --env RG_RT_SHM=/rg/jvs --env RG_RT_LOG=1 --env RG_RT_PULSE_MS=400
  ```
* **GEMETEN:** wat de launcher zelf meegeeft (uit ons logboek): werkmap `/pm/g7/halo`, `HOME`
  `/pm/g7/halo`, argumenten `-f1920x1080 -n`, en de NAS als **alleen-lezen** onderlaag met een
  overlay voor alles wat het spel schrijft.
* **GEMETEN:** de launcher schrijft een **identiteitsregel** bovenaan elk logboek: pad, repo-commit,
  sha256 van het script, van de gegevens, van de hulpen en van alles wat wordt meegegeven -- en of het
  geïnstalleerde script hetzelfde is als het repo-script.
* **GEMETEN:** `halo.so` is **geen** invoerbron (het levert de dongle-kant); onze eigen hulp zet de
  kastschakelaars in de schakelaartafel van het spel.

## 5. Invoer

**Schakelaars (GEMETEN, uit de bron van onze hulp en uit het spel):** het spel leest zijn
kastschakelaars uit een tabel op `0x180ae80 + 0xaea0`, met **0x50 per schakelaar** en **0x2200 per
bord**; de getter in het spel staat op `0x61e890`. De indices: **k 0 = start, 1 = volume op,
2 = volume neer, 3 = service, 4 = diag/test, 5..8 = munt 1..4**. Een schakelaar is een teller van
indrukken: het spel leest `[ingang+4] != 0` als "ingedrukt", dus een indruk is een **korte puls**
(14 ms voor een munt van $0,25; ~300-400 ms voor start; langer telt per beeld meerdere keren).

**Het bordformaat dat de kast gebruikt (`WartranTroopers`, GEMETEN uit het profiel):** een beeld van
64 bytes met op **offset 8** een 32-bits knopveld en op **offset 12..19** de aim:
```
bit 0 test      bit 6 p1.start    bits: p1.trigger = bit 7, p1.reload = 8, p1.grenade = 9, p1.aux1 = 10
bit 1 service   ...               (en per speler: p2.start bit 11, p2.trigger bit 12, ...)
bit 2 munt 1
bit 3 munt 2
bit 4/5 volume
ASSEN: 1 byte per as op offset 12..19 -> p1.x 12, p1.y 13, p2.x 14, p2.y 15, p3.x 16, p3.y 17,
       p4.x 18, p4.y 19; min 0, max 255; links/boven = 0; binnen het scherm blijven de assen op 2..253
       (de kast leest een as <= 1 of >= 254 als "buiten beeld")
```
* **GEMETEN: TEST doet niets.** Zes TEST-pulsen op alle vier de borden lieten het logboek exact op
  hetzelfde aantal regels en zeven identieke schermafbeeldingen achter.
* **GEMETEN: START is voldoende.** In drie runs op een rij (met bit 0 **nooit** gezet) bracht START
  het spel door de integriteitscontrole, door de kalibratie en in de attract-lus.
* **GEMETEN: de munt werkt** (bit 2): `Money In` loopt op; een te lange indruk geeft de waarschuwing
  `Detected more coin pulses than safely allowed`.
* **NOG NIET AANGESLOTEN (GEMETEN dat het ontbreekt): de trekker en de aim.** Onze hulp schrijft de
  knopvelden bit 7..10 en de acht asbytes **niet**. Wat we in plaats daarvan probeerden -- de trekker
  van een virtueel evdev-lichtgeweer -- kwam aan op het apparaat maar niet bij het spel, omdat het spel
  zijn geweer niet via evdev leest.
* **GEMETEN:** het spel houdt tijdens de kalibratie **geen enkel** `/dev/input`-apparaat open en heeft
  **SDL niet geladen** (wel `libudev`). Een extra virtueel invoerapparaat is dus niet de route.

## 6. Timing

| meting | waarde |
|---|---|
| eerste regel in het logboek na de start, **warme** bestandscache | **1 s** (GEMETEN, 4 runs) |
| idem, **koude** cache (eerste run in lange tijd) | **11 s** (GEMETEN) |
| integriteitscontrole afgerond na een START-puls | in dezelfde seconde (GEMETEN, 3 runs) |
| van start tot de attract-bank | ~50-75 s (GEMETEN: 11:56:32 bij start 11:55:51) |

**Waarschuwing:** een tijdvergelijking tussen runs zegt alleen iets als erbij staat of de run **warm
of koud** was. Het verschil van 10 seconden hierboven kwam volledig van de schijfcache, niet van het
beeldpad (GEMETEN: dezelfde opstarttijd met en zonder beeldscherm, zolang de cache warm was).

## 7. Wat we op het scherm zagen

1. **Zwart**, een paar seconden (GEMETEN, foto's van 11,8 kB).
2. **Het integriteitsscherm** van het spel: `Welcome to Halo v03.11`, `Performing Integrity Test.`,
   `Tested: 2232`, `Failed: 1`, `Press START or Ding to skip File Test.` (GEMETEN, foto).
3. **Het geweerkalibratiescherm**: `Player 1 gun (left): Pull gun all the way back and towards the
   left, then press the trigger. ... Press the TEST button to skip calibration for this gun.`
   (GEMETEN, foto).
4. **De attract-lus**: het titelscherm met twee mariniers, **`Press START to Begin`**,
   **`Money In: $16.75`** -- en de meldingen die er nog staan en weg moeten:
   **`QR scanners need to be configured!`**, **`P1 QR Scanner is not detected!`**,
   **`P2 QR Scanner is not detected!`**, **`Factory Setup Has Not Been Completed!`** (GEMETEN, foto).
5. De attract-lus **wisselt van leveldemo** (banken `attract`, `level1` ... `level5`) -- dat is het
   bewijs dat het echt attract is en geen stilstaand beeld (GEMETEN, 9,5 minuut over twee runs).

## 8. Stand van zaken en open vragen

* **GEHAALD (GEMETEN):** de attract-lus draait **6,0 minuten** onafgebroken in één run (en 9,5 minuut
  over twee runs). De lat van dit project is **5 minuten attract- of spelmodus**, dus dat is gehaald --
  met de aantekening dat het de **attract-lus** is en niet het spel zelf.
* **WAT WERKT:** starten zonder de gesloten lader, de integriteitscontrole passeren, de kalibratie
  passeren, de attract-lus, munt, en de identiteitsregel in het logboek. **Geluid niet** (FMOD 1.10
  ontbreekt, stub geeft code 52).
* **OPEN 1 -- de QR- en fabrieksmeldingen** (eis van de eigenaar: die moeten weg). Wat we weten: het
  spel kent de toestandsnamen `ATTRACT_FACTORY_SETUP_INCOMP` en `ActivateCell_FactorySetup_INST`, het
  schrijft/leest QR-beelden als **`<pad>/webcamID/qr<i>.ppm`** en **`/pm/qr<i>.ppm`**, en de melding
  hoort bij een **webcamstap** (`All webcams reported brightness difference of 0`). De meldingsteksten
  zelf staan **niet** als leesbare tekst in `game` en niet in de onderzochte `data`-bestanden.
  **Volgende stappen:** de bron van de meldingstekst vinden (een archief of opgebouwde tekst), en
  uitzoeken of de fabrieksinstallatie in een **servicemenu** zit (de service-schakelaar is k 3) of in
  een vlag die het spel zelf wegschrijft in `pmuser/`.
* **OPEN 2 -- aim en trekker.** De bytes zijn bekend (bit 7 van offset 8, en offset 12..19), maar onze
  hulp schrijft ze niet. Dat is code, geen meting.
* **OPEN 3 -- het spel zelf starten.** In de attract-lus leidt START niet reproduceerbaar tot een
  spelmodus (één keer wel, met moduskeuze en missie 1).
* **OPEN 4 -- het FTDI-/geweerbord.** Het spel heeft libftdi/libusb statisch in zich; wij hebben nooit
  een USB-opsomming van het spel gezien.

---

## 10. Wat is Linux-specifiek voor deze game

*Per punt: **GEMETEN** of aanname, en wat iemand op Windows anders zou doen.*

**1. De native route op Linux. GEMETEN.** Dit spel is een **Linux-ELF** en draait op Linux **native**:
geen Wine, geen Windows-laag. Wij starten de ELF met de **kernel-`ld.so`** en een eigen lader plus de
runtime-kern van dit project (de route van `scripts/play-native.sh`). Het spel lost zijn eigen importen
op (`dlopen`/`dlsym`/`dlvsym`), dus er is geen import-reconstructie nodig.
*Op Windows* zou je dit spel niet direct kunnen starten: je hebt een Linux-omgeving of een ELF-lader
met bibliotheekvertaling nodig. Op Linux is de route juist de eenvoudigste.

**2. De sandbox en het beeldscherm. GEMETEN.** Wij draaien het spel in **bubblewrap**: `/usr`, `/etc` en
`/sys` read-only, `/dev`, `/dev/dri` en `/dev/input` doorgegeven, een eigen netwerk- en PID-namespace,
`/tmp` als tmpfs, en de spelmap als **read-only onderlaag met een overlay** voor alles wat het spel
schrijft (het schrijft in `pmuser/`, zie §2 en §7). Voor beeld gebruiken wij **gamescope**:
`--backend headless` voor proeven zonder scherm, of beeldvullend op de tv. De schermafbeeldingen in §7
zijn gemaakt met de **gst-pipewire-opname** van gamescope (en met `spectacle` op het echte scherm).
*Op Windows* zet je een game in een map en hoop je dat hij zijn paden vindt; op Linux moet je de
werkmap, `HOME` en de schrijfrechten expliciet regelen, anders schrijft het spel in zijn read-only bron.

**3. De invoerhulp. GEMETEN.** De kastschakelaars komen binnen via een eigen hulp die met
**`LD_PRELOAD`** in het **spelproces** wordt geladen (`librg_rt_input.so`). Die hulp leest het
**gedeelde geheugen van de IO-brug** (64 bytes) en schrijft de schakelaars in de **schakelaartafel van
het spel** op `0x180ae80 + 0xaea0`, met **0x50 per schakelaar** en **0x2200 per bord** (de getter staat
op `0x61e890`). Twee waarschuwingen: de hulp laadt **alleen in het proces `.../game`** (niet in de
hulpprogramma's die de game zelf start) en **alleen voor deze spelversie**, na een **bytecontrole** op
die getter -- klopt die niet, dan doet de hulp niets en zegt dat ook.
*Op Windows* bestaat `LD_PRELOAD` niet; daar zou je dezelfde bytes via DLL-injectie of een
geheugen-tool schrijven, en dan mis je de procescontrole die hier gratis is.

**4. De virtuele apparaten. GEMETEN.** Onze lightgun is een **uinput**-apparaat (VID `1209`) en met opzet
een **muisklasse**-apparaat: `udevadm info` geeft `ID_INPUT=1`, **`ID_INPUT_MOUSE=1`** en **geen**
`ID_INPUT_JOYSTICK` (`ABS_X`/`ABS_Y` + `BTN_LEFT`). Dat is nodig voor een front-end die apparaten op hun
kernel-handler selecteert (`mouseN`), maar het betekent ook dat een **native SDL-vraag** dit apparaat
**niet** als joystick ziet -- SDL kijkt op Linux naar `ID_INPUT_JOYSTICK`. **En voor deze game is dat
niet de oplossing:** tijdens de kalibratie opent het spel **geen enkel** `/dev/input`-apparaat en heeft
het **SDL niet geladen** (wel `libudev`). Een extra virtueel invoerapparaat is hier dus geen route; de
invoer loopt via de bordkanalen (§5).
*Op Windows* zou je een virtueel HID-apparaat maken (DirectInput/HID); op Linux is uinput de weg, en
voor games die hun bord via HID zoeken is `uhid` de zwaardere maar trouwere variant.

**5. De muismodus van de daemon. GEMETEN.** Om zonder Move toch een geweer te bedienen heeft dit project
een muismodus: `ravengunctl mice` toont de muizen, `ravengunctl mouse-gun --player N --mouse <id>`
koppelt er een aan speler N, `ravengunctl arm` maakt het virtuele geweer actief, en met
`ravengunctl test-pattern point X Y` plus `ravengunctl output-button trigger tap` stuur je positie en
trekker. De daemon is de enige eigenaar van de hardware; clients praten via een socket.
*Op Windows* zou je een virtuele muis met absolute assen nodig hebben; op Linux doet uinput dat.

**6. De camera. GEMETEN.** Op deze machine bestaat `/dev/video0` (`gspca`, "USB Camera-B4.09.24.1") en
het spel doet daar zijn **helderheidsmeting** (`Baseline %u: %u`, `New lum ... (diff:%u)`) om zijn
webcam(s) en het geweer te vinden. In onze runs meldde het spel `brightness difference of 0`: het zocht
een lichtpunt en vond er geen. *Op Windows* is de camera-API een andere; op Linux is V4L2 de weg en moet
het apparaat in de sandbox doorgegeven worden.

**7. De FMOD-stub. GEMETEN.** `libfmod.so.10` en `libfmodstudio.so.10` zijn propriëtair en staan niet op
deze machine. Wij leveren een **stub** die de FMOD-namen exporteert en **foutcode 52** teruggeeft. Die
52 in het logboek is dus **gereedschap dat werkt**, geen fout: het spel gaat verder zonder geluid. Zonder
stub stopt het spel met een sprong naar adres 0 (eerder gemeten). *Op Windows* is het probleem
hetzelfde, alleen met een DLL in plaats van een `.so`.

**8. Wat er niet nodig was. GEMETEN.** Geen kernelmodules, geen eigen ELF-lader voor deze game, en geen
kernelaanpassing. De enige apparaatrechten die geregeld moesten worden zijn die op `/dev/input/*`
(uinput) en `/dev/dri/*` (beeld).

---

*(Het onderstaande stond eerder in deze sectie en geldt onverkort.)*


* **GEMETEN: het spel is al een Linux-ELF.** Op Linux is er dus **geen** Wine nodig voor het spel
  zelf; je start de ELF direct met de kernel-`ld.so`. (Wine komt alleen in beeld als je de Windows-
  front-end van een emulator wilt gebruiken.)
* **GEMETEN: de kernel-`ld.so` is voldoende.** Het spel lost zijn eigen importen op via `dlopen`/
  `dlsym`/`dlvsym`; er is geen eigen lader nodig.
* **GEMETEN: de sandbox.** Wij draaien in **bubblewrap** met: `/usr` read-only, `/etc` read-only,
  `/sys` read-only, `/dev` en `/dev/dri` doorgegeven, `/dev/input` doorgegeven, een **eigen
  netwerk- en PID-namespace**, `/tmp` als tmpfs, en de NAS-map als read-only onderlaag met een
  overlay voor schrijfacties. De werkmap en `HOME` worden naar de spelmap gezet. Een nieuwkomer moet
  dit regelen of de game in een eigen prefix zetten: **het spel schrijft in zijn eigen map**
  (`pmuser/`, `aud/save/*.xml`, `adj/save/`) en dat moet ergens heen kunnen.
* **GEMETEN: de virtuele apparaten.** Voor invoer gebruiken wij **uinput** (een virtueel
  toetsenbord en een virtuele lightgun, VID `1209`). Let op de valkuil: een apparaat dat voor de
  kast/een emulator als **muis** moet tellen heeft `BTN_LEFT` + `ABS_X`/`ABS_Y` nodig (het krijgt dan
  een `mouseN`-handler); zet je er joystick-knoppen op, dan verdwijnt het uit die lijst. Voor een
  game die zijn geweer via **SDL-joysticks** zoekt moet je juist wél `ID_INPUT_JOYSTICK` halen -- dan
  is een **tweede** apparaat de route, niet het bestaande veranderen.
* **GEMETEN: udev-regels en rechten.** Een `uinput`-apparaat krijgt `/dev/input/eventN` als
  `root:input 0660`; zonder udev-regel met `uaccess` (of groepslidmaatschap) kan een gewone gebruiker
  het niet lezen. Ons project installeert daarvoor een regel per VID/PID.
* **GEMETEN: X/Wayland.** Voor beeld op een tv gebruiken wij `gamescope` (met `--backend headless`
  voor proeven zonder scherm, of beeldvullend op de tv). Op een gewone desktop is een X11/Wayland-
  display met de juiste `DISPLAY`/`XAUTHORITY` voldoende; het spel tekent via GL/GLX.
* **GEMETEN: de camera.** V4L2 (`/dev/video0`) is waar het spel zijn webcam zoekt; in onze sandbox
  was die er wel, maar het spel vond er geen geweer in (`brightness difference of 0`).
* **AFGELEID:** op Windows zou dit spel alleen via een Linux-subsysteem of een ELF-lader met
  Wine-achtige bibliotheek-vertaling kunnen draaien; de route hierboven is de Linux-route en die is
  eenvoudiger, want het spel is al een Linux-programma.
* **AFGELEID:** kernelmodules zijn niet nodig gebleken; de enige apparaatrechten die wij moesten
  regelen waren die op `/dev/input/*` (uinput) en `/dev/dri/*` (beeld).
