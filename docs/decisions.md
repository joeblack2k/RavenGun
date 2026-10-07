# Beslissingen en afgewezen hypotheses

## 2026-10-07 — Uitvoeringsroute Halo

Vastgesteld: Halo is een native Linux-ELF (platform A = Raw Thrills Linux). De TeknoParrot-UI is native Linux (B), maar de loader voor dit profiel, ElfLdr2/BudgieLoader, is een Windows-component die de Linux-UI via Wine/Proton draait (C). De keten wordt dus: Linux-UI, Wine/Proton, BudgieLoader.exe, Linux-ELF van de game.

Gekozen standaardroute: TeknoParrot (de route uit de projectopdracht), met de officiële ElfLdr2- en Proton-componenten die de UI zelf beheert, in een lokale kopie.

Niet als standaard gekozen: de map bevat `halo.so`, een LD_PRELOAD-dongle-emulator (Sentinel HASP) van onbekende herkomst, met een aangepaste `run_game.sh` (`LD_PRELOAD=halo.so game2`). Dit omzeilt de licentiebeveiliging van de game buiten TeknoParrot om en valt onder "omzeil geen licentie/activatie" uit de opdracht. Daarnaast is het onbekende code met hooking-functionaliteit. Deze route wordt alleen heroverwogen als de gebruiker daar uitdrukkelijk voor kiest.

Update 2026-10-07: de gebruiker heeft uitdrukkelijk voor de halo.so-route gekozen. Die is nu de werkroute, zonder verdere patches aan de beveiliging of controles van de game.

Gecontroleerd en afgewezen: ElfLdr2 staat niet in de UI-map, en er is geen native Linux-ElfLdr2 gevonden in de strings van de UI-binary.

## 2026-10-07 — Na de eerste zichtbare start

Hypothese "FileTest faalt door een beschadigde kopie": afgewezen. De log van de game noemt alleen de drie aangepaste bestanden.

Hypothese "START/TEST via het toetsenbord": niet bevestigd. XTEST-toetsen geven geen reactie; een fysieke toetsdruk moet dit nog uitsluiten.

Nieuwe hypothese: de kastinvoer loopt via een FTDI-USB-I/O-bord (libftdi/libusb statisch gelinkt). Mogelijke routes:
- Een eigen I/O-bord-emulatie in RavenGun. Dat is meteen het natuurlijke koppelpunt voor de Move in fase 3, maar vereist onderzoek naar het I/O-protocol (interoperabiliteit, geen DRM) en een manier om een USB-apparaat aan te bieden (bijvoorbeeld USB/IP met vhci-hcd: een kernelmodule en rootrechten, dus toestemming nodig).
- De TeknoParrot-route (Patreon-abonnement), waarin het I/O-bord al geëmuleerd wordt.

## 2026-10-07 — Kern van ravengund (fase 2B/2F)

Uitgewerkt in `docs/architecture.md`. De belangrijkste keuzes:

- Eén daemon is eigenaar van alle hardware. Clients praten alleen via een Unix-socket (0600 in een map met 0700, plus een uid-controle via SO_PEERCRED) met getypeerde JSON-commando's, één per regel. Er is geen HTTP in de live keten.
- De pijplijn heeft één eigenaar (de HID-thread). Beheeropdrachten gaan via een wachtrij naar die thread. De IPC-thread leest alleen een gepubliceerde momentopname. Zo zijn er geen locks in de rekenroute en kan een trage client niets blokkeren.
- Elk HID-rapport wordt afzonderlijk verwerkt, met beide IMU-subsamples (`Frame_FirstHalf` en daarna `Frame_SecondHalf`) en de echte ontvangst-dt, verdeeld over de twee subsamples. Er wordt geen samplefrequentie aangenomen.
- Knopflanken komen per rapport, en daarnaast zijn er per knop cumulatieve tellers. Daardoor gaat bij meerdere of trage afnemers geen druk of loslaten verloren.
- Fusie: een Mahony-filter met alleen een P-term (IMU-only), met een aparte biasschatter die alleen bij stilstand bijwerkt. Gekozen boven Madgwick omdat de correctie eenvoudig te controleren is en de bias dan op één expliciete plek wordt geschat, in plaats van ook nog in een integrator-term. De magnetometer staat uit (ongekalibreerd).
- De aim werkt voorlopig in de oriëntatiemodus: yaw en pitch t.o.v. het midden, lineair naar [0,1]. Een geometrische aim-ray volgt pas met camera en kalibratie.
- Eén filterlaag: 1€ op de aim, uit te schakelen. PSMoveAPI filtert niet (zijn eigen oriëntatie staat uit).
- Een eenmalig automatisch midden per daemon-run is zichtbaar als `center_source: auto_initial`. Daarna komt een nieuw midden alleen van een expliciete `recenter`. Er wordt nooit stil gerecenterd.
- Er is een eigen kleine JSON-parser (ongeveer 250 regels, getest) in plaats van een gevendorde library. Dat is genoeg voor het kleine protocol en voegt geen afhankelijkheid toe.

Afgewezen:
- De bias continu schatten via de integrator-term van Mahony. Die maakt yaw-bias niet waarneembaar zonder magnetometer en verbergt wat er gebeurt.
- Bij stilstand de integratie stoppen (zero-velocity clamp). Dat zou ook langzame echte rotatie en de random walk maskeren, en geldt dus als verborgen driftcorrectie.
- De rapporttijd vervangen door sequence × nominale periode. Het 4-bitsvolgnummer slaat regelmatig nummers over, en de betekenis daarvan is nog niet bevestigd.

## 2026-10-07 — Wine-runner voor Halo in TeknoParrot

Het Proton-script (GE-Proton11-7) is afgewezen voor Halo. De JVS-brug van TPUI 2.0.0.130 detecteert dan het spelproces niet (zoekt comm "game", terwijl er alleen BudgieLoader_x64 draait; zie ProtonProcessDetector.cs en ProtonLauncher.cs in de branch net8-migration). Gekozen is een plain-wine-runner, alleen voor dit profiel: Kron4ek wine-proton-11.0-2-amd64. Kron4ek vanilla 11.19 is afgewezen: daarmee draaide de game zonder venster. Het upstream-issue moet nog gemeld worden: ExpectedExecutable voor ElfLdr2 hoort de loader te zijn.

## 2026-10-07 — Camera en boltracking (fase 2D)

Uitgewerkt in `docs/architecture.md` (sectie "Camera en boltracking"). De meetgegevens staan in `docs/hardware.md`. Alle metingen zijn gedaan met de Move stil op één plek, op ongeveer 0,5 tot 0,75 m van de camera (geschat uit de bolstraal van 19 px; zonder cameramodel is dat een schatting). De speelafstand en beweging zijn nog niet getest.

### Modus: 640x480@60 YUYV (geïmplementeerd, op echte hardware getest)

Vergeleken met dezelfde stilliggende bol (cyaan, exposure 50, gain 0):
- 640x480@60: 60,02 fps, straal 19,1 px, 100 % geldig, jitter p95 0,040 px (6·10⁻⁵ van de beeldbreedte), verwerking p95 146 µs per frame.
- 320x240@125: 124,9 fps, straal 9,4 px, 100 % geldig, jitter p95 0,039 px (1,2·10⁻⁴ van de breedte), verwerking p95 44 µs.
- 320x240@187: 187,3 fps, straal 8,8 px (bol donkerder door de kortere frametijd), 100 % geldig, jitter p95 0,071 px (2,2·10⁻⁴ van de breedte), verwerking p95 40 µs.

De 320x240-modi tonen hetzelfde beeldveld op halve resolutie (de positie halveert, op ongeveer 3 px na). De straal schaalt ongeveer met 1/afstand. Op een speelafstand van 2 à 3 m verwacht ik bij 640x480 een straal van ongeveer 4 tot 6 px en bij 320x240 ongeveer 2 tot 3 px. Dat laatste zit rond de ondergrens van de tracker (12 px oppervlak). Dit is een extrapolatie en nog te bevestigen. Gekozen is 640x480@60, omdat de detectiemarge op speelafstand zwaarder weegt dan de lagere latentie. De camera is bedoeld als langzame referentie naast de IMU: de snelle richting komt uit de gyro (ongeveer 153 subsamples/s), dus 60 Hz optisch is genoeg. 320x240@125 blijft via `--camera-mode` beschikbaar als de speler dichtbij staat of latentie belangrijker blijkt.

Afgewezen:
- 320x240@187 als standaard: de kleinste bol en de grootste genormaliseerde jitter. De belichting per frame is ook korter, waardoor de bol donkerder wordt.
- GRBG (raw Bayer) met eigen debayering: geen meetbare winst bij een bol die groot genoeg is, en wel extra code. YUYV levert de chroma direct. USB-bandbreedte is geen beperking (0 gemiste frames in alle modi).
- 320x240@120: bestaat niet bij de driver en levert 100 fps (eerder gemeten).

### Controls: AE, AGC en AWB uit, exposure 50, gain 0 (geïmplementeerd, op echte hardware getest)

De ov534 biedt: brightness 0..255 (0), contrast 0..255 (32), saturation 0..255 (64), hue −90..90 (0), white_balance_automatic (bool), exposure 0..255, gain_automatic (bool), gain 0..63, horizontal_flip, vertical_flip, power_line_frequency (uit/50 Hz) en sharpness 0..63 (0), plus auto_exposure (0 = automatisch, 1 = handmatig). Er zijn geen aparte rood- of blauwversterkingen. Na het zetten gelezen: auto_exposure 1, exposure 50, gain_automatic 0, gain 0, white_balance_automatic 0. De rest staat op de standaardwaarden.

Belichtingsreeks met de cyaan bol (gemiddelde Y en chroma van de bol, en de p99 van de Y van de scène):
- exposure 5: niets gevonden. Exposure 10: te klein of niets (alleen de kern is zichtbaar).
- exposure 20: Y 55, chroma 40, scène p99 8.
- exposure 30: Y 75, chroma 53, scène p99 11.
- exposure 50: Y 103, chroma 67, scène p99 16.
- exposure 80: Y 129, chroma 75, scène p99 24.
- exposure 120: Y 155, chroma 80, scène p99 35.
- exposure 200: Y 201, chroma 91, scène p99 56, en een gele lamp verschijnt als chromatisch object.

In geen enkele stand was de bol verzadigd (0 pixels met Y ≥ 245). Met de automatiek aan (de standaard van de driver) was de bol wit verzadigd en dus onbruikbaar (`too_little_color`), en was 4 % van het beeld verzadigd. Gekozen is exposure 50: genoeg chroma (67, ruim boven de drempel van 28), een helderheidsreserve van ongeveer een factor 2,4 tot verzadiging voor een dichterbij gehouden bol, en een vrijwel zwarte scène. Een langere belichting geeft meer bewegingsonscherpte en haalt lampen binnen. De omrekening van de exposure-waarde naar milliseconden is niet gemeten. Gain 0 houdt de ruis laag, en de bol is helder genoeg. Bij een lichtere kamer (overdag) moeten deze waarden opnieuw worden bekeken: dat is nog te bevestigen.

### LED-kleur: cyaan (geïmplementeerd, op echte hardware getest)

Gemeten tint in het camerabeeld (YUYV, AWB uit): cyaan −75°, blauw −31°, groen −145°, rood 119° en magenta 86°. Magenta zit daarmee 25° van de nominale waarde (61°) af, richting rood. De kamer zelf is bij automatische belichting vrijwel kleurloos (minder dan 0,01 % van de pixels heeft chroma ≥ 28). Een conflict met de omgeving was in deze meting dus voor geen enkele kleur zichtbaar. De keuze berust daarom op de kleuren die tijdens het spelen in beeld kunnen komen: huid en hand (oranje, nominaal berekend rond 110–130°, niet met deze camera gemeten), hout en warm lamplicht (oranje-geel, nominaal 120–170°). Cyaan ligt daar het verst vandaan (ongeveer 165° tot de huidtint) en ligt 44° van blauw (daglicht, blauwe leds) en 70° van groen. Met ±30° overlapt de sector met geen van de gemeten LED-kleuren. Cyaan gaf ook de grootste en helderste bol bij dezelfde belichting (straal 18,6 px tegenover 17,4 px voor magenta, chroma 53 tegenover 49).

Afgewezen:
- Magenta: ligt maar ongeveer 33° van de huid- en roodtint, en de gemeten tint wijkt sterk af van de nominale.
- Rood: de hoogste chroma (90), maar valt samen met huid en hout.
- Groen: de laagste chroma (42) en planten. Blauw: de bol is het donkerst en het kleinst (straal 11 px).

Een vals-positieftest met LED uit en met een magenta LED bij een cyaan-instelling gaf 0 kandidaten in 300 frames.

### Tracker: eigen code, geen OpenCV (geïmplementeerd, automatisch getest, op echte hardware getest met een stilliggende bol)

PSMoveAPI is zonder tracker gebouwd en OpenCV staat alleen in de container. De nodige bewerkingen (een opzoektabel, connected components en momenten) zijn klein. Het volledige beeld kost ongeveer 0,6 ms en een ROI ongeveer 0,12 ms per frame. Er is geen confidence-getal: per frame is er een boolean met een reden, plus tellers in de status.

### Voorstel (niet gebouwd): optische positie in een hybride modus

Uitgangspunt (sectie 10 van de opdracht): de bolpositie is een translatie in het beeld en geen meting van de yaw. Een gedraaide controller op dezelfde plek geeft dezelfde bolpositie. Daarom wordt de yaw-drift NIET gecorrigeerd door de bolpositie direct op de aim te zetten.

Voorstel in drie stappen, elk afzonderlijk aan of uit te zetten en elk met een zichtbare status:
1. Translatiecompensatie: de oriëntatie (IMU) blijft de richting bepalen. De verplaatsing van de bol sinds het laatste midden (Δx, Δy in het beeld) corrigeert het trefpunt voor zijwaartse en verticale verplaatsing van de speler. Daarvoor zijn nodig: de intrinsieken van de camera, een schatting van de afstand (bolstraal met een gekalibreerd model en een bolmaat van 46 mm, nog te bevestigen) en de pose van het scherm ten opzichte van de camera (camera bij de tv: grofweg dezelfde vlakte, maar dat moet gekalibreerd worden).
2. Driftbewaking zonder correctie: bij expliciete momenten (recenter, kalibratiedoelen, of stilstand van de bol met stilstand van de gyro) worden IMU-yaw en bolpositie samen vastgelegd. Groeit het verschil tussen de huidige IMU-yaw en de yaw bij dezelfde bolpositie en hetzelfde doel, dan meldt de status "yaw gedegradeerd" en kan de gebruiker gericht recenteren. Dit corrigeert niets heimelijk.
3. Optionele driftcorrectie onder een expliciete bewegingsmodelaanname: aanname "de hand (draaipunt) blijft op zijn plek". Dan verplaatst een yaw-rotatie de bol zijwaarts met ongeveer L·Δψ (L is de afstand van het draaipunt tot de bol, ongeveer 0,1 tot 0,2 m, te kalibreren). Een complementair filter met een zeer lage versterking (tijdconstante van tientallen seconden) trekt de IMU-yaw naar die optische schatting. Zo'n correctie faalt zodra de speler zijwaarts stapt of de arm verplaatst: een translatie wordt dan als yaw gelezen. Daarom moet ze uitgezet worden bij een translatie die de gyro niet verklaart (residu-test) en altijd zichtbaar zijn in de status.

Een betere absolute yaw-referentie dan stap 3 is de magnetometer van de ZCM1 (na kalibratie en een controle op lokale verstoring), of een tweede optisch kenmerk. Een enkele bol maakt de yaw principieel niet waarneembaar.

Benodigde kalibratie (nog niet gebouwd): intrinsieken en vervorming van de camera (dambordpatroon in de container met OpenCV, of een eigen fit) per cameramodus, de fysieke boldiameter, de pose van het scherm ten opzichte van de camera, en de offset van controller naar richtas. Opslag volgens sectie 11: met camera-identiteit (by-id of by-path), modus en exposure-instellingen. Bij een andere modus volstaat het om de intrinsieken te schalen. Een verplaatste camera vraagt een nieuwe extrinsieke kalibratie.

## 2026-10-07 — Webinterface in de daemon (fase 2G)

- Eigen minimale HTTP-server (`src/ravengun/web/web_core.*`, `src/ravengund/web_server.*`) in plaats van cpp-httplib. Redenen: er komt geen gevendorde code bij (geen licentie- of pinning-last, geen netwerktoegang nodig bij het bouwen); de server is een kleine poll-lus op niet-blokkerende sockets, net als de IPC-thread, waardoor SSE met een begrensde uitvoerbuffer per client en het overslaan van frames bij trage clients eenvoudig en aantoonbaar niet-blokkerend is (cpp-httplib werkt met een threadpool en blokkerende schrijfacties naar de client). De parser is strikt en klein: alleen GET en POST, geen chunked, geen dubbele Host of Content-Length, header max 8 KiB, body max 4 KiB.
- Eén bron van waarheid: de IPC-handler is verplaatst naar `commands.cpp` (`execute_command`). IPC en web roepen dezelfde functie aan. De web-API zet een request om in een IPC-verzoekregel en laat die door dezelfde parser en validatie lopen; er is geen tweede implementatie van filter, aim of recenter.
- Binding: standaard 127.0.0.1:8765. LAN alleen met `--web-lan` én een token (willekeurig, 0600 in `~/.config/ravengun/web-token`, nooit in de log). Lukt het token of de bind niet, dan start alleen de web-interface niet; de daemon draait door.
- Beveiliging per request: Host-controle (tegen DNS-rebinding), voor POST ook Origin (indien aanwezig), Sec-Fetch-Site, `Content-Type: application/json` en een CSRF-token (`X-CSRF-Token`, per proces gegenereerd, in de pagina ingebakken). Nooit CORS-headers; OPTIONS wordt geweigerd. Er is geen shell-endpoint en geen pad- of programma-argument: alleen getypeerde commando's.
- De web-API toont nooit het volledige controller-ID (altijd geredigeerd) en nooit camerabeelden.
- Gevolg: de web-thread wacht bij een commando maximaal 500 ms op de HID-thread (zoals de IPC); de HID-thread wacht nooit op de web-thread.

## 2026-10-07 — Ontwerp sensorfusie en richtgeometrie (nog niet gebouwd)

Uitgewerkt in `docs/aim-fusion-design.md`. De kernkeuze: de richting komt uit de gyro (snel, bepaalt het gevoel). Pitch en roll worden begrensd door de accelerometer. De yaw wordt begrensd door de eigen gekalibreerde magnetometer van de ZCM1: een ellipsoïdefit, alleen een yaw-update, verstoringspoorten op norm, dip, NIS en gyroconsistentie, en een snelheidsbegrensde correctie zonder sprongen. Dat gebeurt in een error-state Kalman-filter op oriëntatie en gyro-bias. Mahony blijft de referentie en terugval tot opgenomen sporen aantonen dat de ESKF beter is. De translatie komt uit de 3D-bolpositie (640x480@60, een gekalibreerd model met bolstraal 22,5 mm en een gefitte schaal), in een geometrische aim-ray naar een schermvlak. Schermpose, schermgrootte, de camera-wereldrotatie en de richtas worden samen gefit uit richtsamples vanaf 2 à 3 posities en gevalideerd op onafhankelijke doelen. De uitvoer gaat op 250 Hz met een begrensde gyro-extrapolatie. Het 1€-filter staat op de schermcoördinaat. Afgewezen: de ComplementaryMARG-fusie van PSMoveAPI/PSMoveService (correctie met τ ≈ 0,3 s, zonder poort, verstoring lekt in de tilt), de min/max-magnetometernormalisatie van PSMoveAPI (een uitschieter verschuift de kalibratie blijvend), het continu corrigeren van de yaw met de bolpositie (één bol meet geen yaw; alleen bewaking), en 320x240 als standaard (de bol zit op 3 m op de detectiegrens). Het beslispunt is fase 3: haalt de magnetometer in de speelkegel geen herhaalbaarheid van ongeveer 0,3°, dan wordt de G2-drifteis niet gehaald zonder extra hardware (een tweede optisch kenmerk of een IR-camera in het geweer).
## 2026-10-07 — Outputbackend: virtuele uinput-lightgun voor TeknoParrot

Uitgewerkt in `docs/architecture.md` (sectie "Uitvoer"), met de TeknoParrot-bronverwijzingen en de meettabel.

Gekozen:
- Een absolute muis via uinput: `ABS_X`/`ABS_Y` 0..32767, `BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA`, geen `INPUT_PROP_DIRECT`, geen `BTN_TOUCH`. Reden: TeknoParrot (Linux) leest evdev zelf en toont alleen apparaten met een `mouseN`-handler in `/proc/bus/input/devices` (kernel-mousedev). Dat vraagt `BTN_LEFT` bij een absolute as. Dit is het enige apparaattype dat in de lijst "P1 Light Gun" verschijnt, en het is getest: de gun is aangeboden, gebonden en Halo volgt hem.
- Identiteit 1209:7202, naam "RavenGun Virtual Lightgun", plus twee udev-symlinks (`/dev/input/ravengun-gun` en `/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse`). Het by-id-pad is het pad dat TeknoParrot als stabiele binding opslaat (`BuildStablePathMap` kent alleen `/dev/input/by-id`). Zonder die link zou de binding op `/dev/input/eventN` staan, en dat nummer veranderde in de proef (event30 naar event258).
- `LIBINPUT_IGNORE_DEVICE=1` op de gun in de udev-regel: KWin ziet het apparaat dan niet (gemeten: het staat niet in `/org/kde/KWin/InputDevice`, de desktopcursor beweegt niet; een identiek controle-apparaat met een ander PID deed dat wel).
- Standaard disarmed; het apparaat bestaat pas na de eerste `arm` en blijft daarna tot het afsluiten (zodat een binding en een lopende game stabiel zijn); `disarm`, schrijffouten en afsluiten geven knoppen vrij. De daemon leest zijn eigen apparaat nooit.
- Alle geometrie (viewport/letterbox, inversie, schaal, offset, clamp) staat op één plek (`output/gun_transform.cpp`) en is live instelbaar. Meting: voor Halo (16:9, 1920x1080, `InvertedMouseAxis=true`) is de standaard (niets doen) juist; de fout is maximaal 4,5 px (0,23 %), het 8-bits raster van het spel.
- Trigger, reload en grenade gaan via de muisknoppen van de gun (TeknoParrot biedt die aan); start, coin en test blijven op het virtuele toetsenbord.

Afgewezen:
- De `VINPUT_JOY`-route (joystick-capabilities zodat udev het apparaat leesbaar maakt): onnodig door de udev-regel, en zonder `BTN_LEFT` toont TeknoParrot het apparaat niet als muis.
- Auto-arm bij het starten van de daemon: botst met "standaard geen globale injectie".
- De gun automatisch aan de live Move koppelen in deze stap: de opdracht was de uitvoerkant en de testpatronen. De koppeling (aim, trigger, knoppen en `release_all` bij bronuitval) is een kleine volgende stap.

Open (beslissing nodig, zie architectuur "Stabiliteit"): TeknoParrot heropent het apparaat niet, dus de daemon moet blijven draaien zolang de game draait. Voorstel: de uinput-fd door systemd-user laten bewaren (fd store) of een apart apparaathouder-proces. Ook open: `LIBINPUT_IGNORE_DEVICE` voor het virtuele toetsenbord (7201), zodat toetsen niet naar het gefocuste venster gaan.

## 2026-10-07 — Fase 0/1/3 (bouw): opnemen, tijdbasis van de Move, magnetometer

Uitgewerkt in `docs/aim-fusion-design.md` (sectie "Metingen fase 0/1/3") en `docs/procedures.md`.

Tijdstempel en temperatuur: eigen parsing via een tweede hidraw-descriptor, geen patch op PSMoveAPI.
- De kernel levert elk inputrapport aan iedere open hidraw-descriptor. De HID-thread opent de hidraw-node van de verbonden ZCM1 (gevonden via `HID_UNIQ` = Bluetooth-adres en `HID_ID` 0005:054c:03d5 in sysfs) een tweede keer, alleen-lezend en niet-blokkerend. PSMoveAPI blijft de rapporten lezen en kalibreren. De tap levert wat PSMoveAPI niet publiek maakt: de interne 16-bit stempel (byte 0x0B hoog, 0x2B laag), de 12-bit temperatuur en de ruwe accel- en gyrowaarden (`src/ravengun/core/move_report.*`).
- Koppeling per rapport: hetzelfde 4-bit volgnummer en hetzelfde door PSMoveAPI gemiddelde ruwe accelerometerpaar. Lukt dat niet, dan krijgt dat rapport geen stempel en geldt de ontvangsttijd. Tellers (`hidraw.matched/unmatched/discarded`) staan in de status; een verkeerde byte-indeling valt daar direct op.
- Waarom geen patch: een patch vraagt een nieuwe containerbouw en vervangt de gedeelde `libpsmoveapi.so` onder `~/.local/share/ravengun/psmoveapi`, ook voor andere checkouts. De tweede descriptor levert daarnaast een fd voor `poll()`. PSMoveAPI zelf geeft geen fd.
- Licentie: PSMoveAPI is BSD-2-clause (Thomas Perl). Er is geen code overgenomen, alleen de byte-offsets uit de struct-commentaren van `src/psmove.c` op de gepinde revisie 256b6299e6076fe7b113e7277ed67016903f067a. Bij een andere PSMoveAPI-revisie moet de indeling opnieuw gecontroleerd worden (de koppeltellers tonen dat).
- De HID-thread wacht nu met `poll()` (maximaal 2 ms) op de tap-descriptor in plaats van 1 ms te slapen. Zonder tap blijft de oude slaap van 1 ms.

Tijdbasis (`src/ravengun/core/move_clock.*`):
- De stempel wordt ontrold met de hosttijd (aantal wraps = afgerond (Δt_host − Δstempel)/65536). Dat is eenduidig zolang de Bluetooth-jitter kleiner is dan een halve wrap (0,33 s).
- Klokkoppeling met de ondergrensmethode: het minimum van (t_rx − t_stempel) per bak van 1 s, een rechte door de minima van de laatste 20 s (zonder de lopende bak), daarna omlaag geschoven tot onder alle minima. De gekoppelde tijd is "stempel plus de kleinste transportvertraging", niet het samplemoment van de sensor.
- De pijplijn integreert standaard met de stempel-dt (`--dt-source move`). Dat gebeurt alleen als dit én het vorige rapport een stempel hebben, anders met de ontvangst-dt. `report_dt_ms` in het sample blijft de ontvangst-dt (geen schemawijziging). `--dt-source rx` geeft het oude gedrag.
- Afgewezen: de rapporttijd vervangen door volgnummer × periode. Overgeslagen volgnummers zijn echte gaten in de stempel (zie de metingen), maar de stempel meet die gaten direct, en het volgnummer wrapt al na 16.

Opnameformaat en recorder:
- JSON Lines, één object per regel (header, rapport, optische meting, LED, label, einde). Alleen getallen en korte codes, nooit beelden. Tijden staan relatief t.o.v. t0, en t0 staat als string in de header. Getallen gebruiken de kortste exact terugleesbare notatie (`std::to_chars`). De rondgang is dus bit-exact, en afspelen is aantoonbaar deterministisch (digest over alle samples). Gekozen boven binair: leesbaar met standaardgereedschap, en ongeveer 42 kB/s met camera (12,5 MB per 5 minuten, gemeten) is geen probleem.
- De recorder zit in de daemon, met een eigen schrijfthread en een begrensde wachtrij (8 MiB). De HID- en tracker-thread blokkeren nooit op schijf-I/O. De grootte is strikt begrensd (standaard 64 MiB, max. 1 GiB), net als de duur (standaard 600 s, max. 2 uur). Bestanden komen in `~/.local/state/ravengun/recordings/` (0700/0600, `O_EXCL|O_NOFOLLOW`). De naam komt uit tijdstip en tag (`[a-z0-9_-]`). Er is geen commando dat een pad aanneemt.
- Het controller-ID staat alleen geredigeerd in de opname (`xx:xx:xx:xx:xx:ac`). Een hash is bewust niet gebruikt: met een bekende OUI is een hash van een MAC-adres in seconden terug te rekenen, dus die zou alleen schijnveiligheid geven.

Magnetometer (`src/ravengun/core/magnetometer.*`):
- Ellipsoïdefit als lineaire kleinste kwadraten op genormaliseerde data. Eerst asgericht (6 parameters). Ligt de dekking rond dat middelpunt boven 50 %, dan volgt de volledige fit (9 parameters, met een lichte regularisatie van de kruistermen). Het resultaat is `m_cal = A(m_raw − b)` met een symmetrische A (matrixwortel via Jacobi). Kwaliteit: de RMS van |m_cal| − 1 (≤ 2 %), de dekking van de bol (20 richtingen, minstens 3 samples per richting, ≥ 80 %), het aantal samples (≥ 300), de veldsterkte in counts en de spreiding van de dip (≤ 1,5°, alleen quasi-statische samples). Ter controle van de assen wordt ook de dipspreiding zonder Y-flip gerapporteerd.
- Verzamelen dunt uit (een nieuw sample alleen bij een verschil van minstens 3 % van het bereik), zodat stilhouden de fit niet scheeftrekt.
- Opslag per controller (sleutel FNV-1a-64 van het adres, alleen als bestandsnaam) in `~/.config/ravengun/magnetometer/`. De opslag is atomisch (tijdelijk bestand, fsync, rename, fsync van de map), met 0600, versie 1 en een controle bij het laden (ontbreekt, beschadigd, andere versie). Een kalibratie die de eisen niet haalt, wordt niet opgeslagen en niet geactiveerd.
- Verstoringsdetector: de norm (5 %), de dip (2°, alleen bij |a| binnen 0,1 g van 1 g), de magnetometer-yaw tegen de gyro over 0,5 s (2°/s) en de rumble. "Verstoord" na 1 s aaneengesloten afwijzing, herstel na 2 s. De NIS-poort volgt met de ESKF (fase 4). De detector draait live in de status mee, maar de magnetometer gaat nog niet de fusie in.
- Web: alleen-lezen `GET /api/record` en `GET /api/magnetometer`, plus een veld `progress` in de SSE-frames. Starten en stoppen gaat voorlopig alleen via de IPC (CLI). De web-UI-HTML is niet gewijzigd.
## 2026-10-07 — Instellingen, LED-kleur en camera live via de webinterface

Uitgewerkt in `docs/web-ui.md`. De keuzes:

- Persistente instellingen in `~/.config/ravengun/config.json` (schemaversie 1, migratiehaak, strikte validatie, atomisch schrijven met 0600, laatst-goede kopie `.bak`). Een ongeldig bestand wordt nooit half toegepast. De opdrachtregel van `ravengund` gaat altijd voor het bestand en de daemon schrijft bij het starten nooit zelf iets terug. Gekozen boven een eigen binair formaat of een database: het bestand is leesbaar en met de hand te herstellen. De nieuwe logica staat in `src/ravengun/config/` (de bibliotheek, dus los te testen), zodat de HID-thread nauwelijks verandert.
- LED-kleur is een getypeerd commando `set_led` (preset of RGB 0 tot 255). BELANGRIJK: de tracker zoekt op tint, dus een kleurwissel past ook de tracker-tint en de tolerantie aan, in dezelfde stap. Presets gebruiken de gemeten tinten uit de proef van 7 oktober (cyaan -75, blauw -31, groen -145, rood 119, magenta 86 graden; tolerantie 30). Wit heeft geen tint: de LED gaat aan, de tracker-tint blijft staan en er is een waarschuwing dat de tracker dan blind is. Een eigen kleur ("custom") krijgt de nominale tint (BT.601) met tolerantie 45 en een waarschuwing, omdat de gemeten afwijking tussen nominaal en camera tot 25 graden ging (magenta: 61 nominaal, 86 gemeten) en een eigen kleur niet gemeten is. Een bijna grijze kleur of uit krijgt geen tint. Afgewezen: de tint van een eigen kleur stilletjes laten staan (dan is de bol na een kleurwissel weg zonder dat iemand weet waarom) en wit uit de presets halen (de gebruiker vroeg erom; het is nuttig om te zien of de LED brandt).
- Camera-instellingen live (`set_camera`: exposure 0 tot 255, gain 0 tot 63, auto-belichting, auto-gain en AWB, en de modus 640x480@60, 320x240@125 of 320x240@187) worden uitgevoerd in de capture-thread, de enige eigenaar van het apparaat. Daarna worden de werkelijke waarden teruggelezen en getoond; een afwijking is een waarschuwing. Een moduswissel herstart de capture en draait bij een fout terug. Geen apparaatpad als argument. Zonder camera: een nette fout en niets bewaard. Afgewezen: de controls vanuit de web- of IPC-thread op dezelfde fd zetten (twee eigenaars van het apparaat) en een willekeurige resolutie toestaan (alleen de drie gemeten modi).
- Batterij: de tekst en het balkje volgen de werkelijke resolutie van `PSMove_Battery_Level` (psmove.h: 0x00 tot 0x05, 0xEE laden, 0xEF vol aan de lader). Vijf segmenten van elk 20 procent, plus de tekst; geen percentage.
- Webpoort is bewaarbaar (geldt na een herstart); LAN-toegang niet: dat blijft alleen `--web-lan` met token, zodat een cross-origin-aanval of een fout in de pagina het netwerkbereik van de interface nooit kan vergroten.
- JavaScript-syntaxis wordt gecontroleerd met `node --check` in een wegwerp-container (`tools/check-js.sh`). Gekozen boven een eigen mini-parser: een echte engine vangt echte fouten, en de container (al aanwezig) installeert nodejs per run zonder de image te wijzigen. De host heeft geen node of qjs.
