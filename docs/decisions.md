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

## 2026-10-07 — Fullscreen kalibratie-UI op de tv

Uitgewerkt in `docs/calibration-ui.md`. De keuzes:

- De gebruiker wil geen kalibratie-instructies via de chat of op een telefoon, en heeft geen toetsenbord of muis. De kalibratie is daarom een eigen pagina `/calibrate` van dezelfde webserver (zelfde beveiliging, geen CDN), fullscreen op de tv en alleen bediend met het geweer: trekker of Move-knop volgende, Cirkel terug, Select overslaan, Start afbreken. De pagina stuurt nooit invoer door naar de game of de virtuele lightgun.
- Knopdrukken komen uit cumulatieve flankentellers (`edges` in het SSE-frame), niet uit de flanken van één sample. De stroom is begrensd tot 30 frames per seconde en een korte druk kon daartussen verdwijnen. Debounce zit in de pagina (350 ms na een geaccepteerde actie, de eerste stand na verbinden is alleen een basislijn), als pure JS in `calibrate-core.js`, met unittests. In de stap Knoppen bedient alleen lang indrukken (1,5 s) de pagina, omdat daar elke knop zelf een meting is. Alternatief verworpen: de debounce in de daemon. De daemon kent de stap niet, en dezelfde stroom dient ook de overige pagina's.
- Vast podium van 1920 bij 1080 met letterbox-schaling, zodat de doelen op exacte tv-coördinaten staan (3 bij 3 op 10, 50 en 90 procent) en de layout niet van de browser afhangt. Fase 5 (schermkalibratie) is alleen een plaatshouder met de negen doelen; de berekening volgt later.
- Dekking van de magnetometer wordt getoond met de bestaande 20 richtingen van `SphereCoverage`, getekend als Voronoi-vlakken op een canvas (voor- en achterkant). Geen finere indeling, omdat de getoonde dekking dan niet meer hetzelfde meet als de poort van de daemon. De daemon levert `cells`, `dir`, `spread` en `gyro_dps` in `progress.mag_calibration` en blijft op 5 Hz. Het tempo komt uit de gyro in de 30 Hz-stroom, met vaste grenzen (8, 120 en 220 graden per seconde), geen daemon-oordeel.
- Stil richten legt de metingen vast als labels in een gewone opname (`record mark`): begin `stil_midden_1` enzovoort, einde met `_einde` of `_onrustig`, nieuwe pogingen met `_pogingN`. Zo kan de analyse de segmenten exact terugvinden zonder nieuwe bestandsindeling. De pagina keurt zelf af wie te onrustig is (minstens 85 procent van de samples onder 5 graden per seconde en geen uitschieter boven 20), na een uitzwaaitijd van 1 s.
- De knoppenmapping (fysieke Sharp Shooter-knop naar Move-knop) staat in `config.json` onder `sharp_shooter.buttons` (schema 1 ongewijzigd: een nieuwe optionele sectie), via het IPC-commando `set_buttons`. De rollen zijn een vaste lijst (trekker, pomp, driehoek, cirkel, kruis, vierkant, start, select, move, ps), omdat onbekend is welke knoppen elke Sharp Shooter-uitvoering heeft; een rol zonder druk binnen 15 s wordt overgeslagen.
- Kiosk: Brave als flatpak, met een eigen profiel onder `~/.local/state/ravengun/kiosk`. Sluiten zoekt processen op de exacte `--user-data-dir` van dat profiel en raakt andere Brave-vensters niet. Bewust geen `flatpak kill`, dat alle Brave-instanties zou stoppen. De start gaat door de daemon (`calibrate_ui`), zodat `ravengunctl` en de knop in de web-UI hetzelfde pad gebruiken; de sessievariabelen worden daarbij expliciet gezet. Bij een nieuw profiel worden de meldingsbalken van Brave uitgezet.
- JS-tests lopen in node in een wegwerp-container (`tools/test-js.sh`), als ctest-test die zonder podman of netwerk wordt overgeslagen. Gekozen boven een test in een headless browser, die in de proef bleef hangen op de open SSE-verbinding.

## 2026-10-07 — Fase 4: oriëntatieschatter met magnetometer-yaw en de live lightgun

Uitgewerkt in `docs/aim-fusion-design.md` ("Metingen en bouw fase 4") en `docs/architecture.md`.

- Assen gemeten: gyro en accelerometer zijn consistent (alleen de huidige tekens kloppen); de magnetometer gaat met diag(-1,+1,-1) naar B (Y-flip zoals PSMoveAPI plus een globaal teken uit de dip: met alleen de Y-flip wees het veld omhoog). Oude kalibratiebestanden worden bij het laden omgezet. De afwijkingen van `tools/mag_vs_gyro.py` kwamen door een tekenfout in het script, niet door de assen.
- Gekozen: `mahony_mag` (Mahony-tilt plus een yaw-only Kalman-filter op fout en drift), in plaats van de volledige ESKF. Reden: de tilt is bewezen, het probleem zit alleen in de yaw, yaw-only voorkomt per constructie dat een magnetometerfout de tilt raakt, en de begrenzingen zijn toch niet-Gaussisch. De drift wordt als bias om de verticaal weggeregeld (daarmee ook in stilstand begrensd, waar de hoekcorrectie maar 0,02°/s mag); de hoekcorrectie gaat met 0,02°/s in stilstand en 2 % van de draaisnelheid in beweging (max 0,3°/s).
- Afgewezen: de hoekcorrectie in stilstand sneller maken dan 0,02°/s (zichtbaar wegglijden); de bias via een venster-helling leren (te traag bij veranderende drift, gemeten op ledmag); de yaw-fout meten met de tilt van q (een Mahony-tiltfout werkt 1,7 keer door in de heading).
- Niet standaard: de replay toont winst op het rustige handspoor en het tafelspoor met bevroren bias, maar verlies op de onrustige richttest. `mahony` blijft de standaard tot een langere hand-opname op de speelplek de keuze maakt (procedure in `docs/procedures.md`).
- Ook: detector (gyro-consistentie als helling over 1,5 s, norm en dip na 0,15 s glad, dip alleen bij langzame rotatie), dip in de kalibratie alleen uit langzame samples, interpolatie van de hoeksnelheid over verloren rapporten, en een strengere stilstandsbias na de eerste schatting.
- Live lightgun: de pijplijn-aim per rapport naar de uinput-gun als die gewapend is; trekker uit de analoge T met hysterese, reload/grenade/recenter instelbaar (`gun_buttons`, standaard Kruis/Vierkant/Move). `sharp_shooter.buttons` wordt niet gebruikt (meting fout volgens de gebruiker). Geen 250 Hz-klok met extrapolatie (fase 6).

## 2026-10-08 — VQF en de oorzaak van de Halo-drift

Meetlat: twee opnames van echt Halo-spelen waarin elke centreerdruk een meetpunt is: A (`20261008-193534-halo-drift`, PS-knop, 8 intervallen, 164 s) en B (`20261008-201140-halo-bias-new`, PS of Move-knop, 10 intervallen, 128 s), de maat is de cumulatieve yaw-drift van de eerste naar de laatste druk. A is het spoor waarop de biasschatter is afgesteld (optimistisch), B is de toets. Alle replays hieronder gebruiken de huidige pijplijn (bias-modus `new`).

Oorzaak van de -0,19 graden/s in B (onderzocht met `ravengun-driftlab`, dat `analyze drift` exact reproduceert):

- Het is geen bewegingsafhankelijke drift. Per variant, cumulatief in graden/min voor A / B (Mahony nu: -0,36 / -11,60): kp 0 (pure gyro) +2,97 / -11,76, kp 0,05 tot 2: -0,16 tot -0,38 / -11,59 tot -11,66; accelerometerpoort 0,05 of 0,3 g, extra poort op de versnellingsverandering of op de draaisnelheid: hooguit 0,05 verschil; gatvulling uit / vorige waarde vasthouden / volgende waarde / nul: -1,08 / -11,30, -0,90 / -13,35, +0,17 / -9,85, -0,29 / -10,21; dt uit ontvangsttijd i.p.v. de Move-stempel +1,24 / -11,51; kleine-hoekintegratie i.p.v. de exacte exponent -0,36 / -11,60 (identiek; coning is bij 5,6 ms per sample verwaarloosbaar); schaalfout 2 % per as en kruiskoppeling 2 % tussen de assen: hooguit 0,3 graden/min verschil op A en B; yaw gemeten als twist om Z of als azimut van body-X i.p.v. de richtas: -0,12 / -11,63 en +0,13 / -11,66 (geen meetartefact). PSMoveAPI past de fabrieksschaal per as al toe (`calibration->gx/gy/gz`, ZCM1 op +80 rpm, geen offset). Een rate-asymmetrie (g += alfa·|g|) van 1 % zou 3 tot 6 graden/min geven, maar met een tegengesteld teken nodig in A en B.
- Regressie van de intervaldrift (18 intervallen, dus alleen indicatief): in B past een constante snelheid -0,19 graden/s (reststandaardafwijking 1,07 graden, gelijk aan het richten van de speler); gedraaide hoek, |draaisnelheid| per as, dynamische versnelling of verloren rapporten verbeteren dat niet (rest 1,00) en hun coëfficiënten hebben in A het tegengestelde teken. Er is geen stabiele bewegingsterm.
- Onafhankelijke referentie: de magnetometer (kalibratie 37b2de54e4ef26ba, 83 % van de samples ok in B). Het verschil tussen gyro-yaw en magnetometer-heading loopt in B van 34 tot 165 s met -0,17 graden/s (26 blokken van 5 s, sd 0,16); correlatie met de activiteit 0,08; stille blokken (gemiddelde |ω| < 3 graden/s) -0,195, drukke (> 10 graden/s) -0,152; tussen 20 en 33 s (zelfde houding, de gun rustig in de hand) 0,00. Dus een z-gyrobias die bij het begin van het spelen (rond 33 s) ongeveer 0,19 graden/s boven de schatting kwam en daar bleef, ook tijdens stilhouden in de hand. De camera-controle (`cam_still_bias.py`) kon dit niet uitsluiten: een rotatie van 0,2 graden/s om de verticaal verplaatst de bol maar ongeveer 1 mm/s.
- De z-bias zwerft ook zonder beweging: in de 600 s stille opname (`fase4-mahony-mag`, constante temperatuur) varieert het gemiddelde per 20 s tussen 0,93 en 1,15 graden/s. De schatter krijgt tijdens het spelen weinig stilstand (B: 38 s in totaal, vooral aan het begin en einde) en volgt met tau 30 s; B had een stap van +0,2 die hij niet zag. Een constante extra bias +0,19 op z haalt B naar -0,30 graden/min maar zet A op +10,35: de fout verschilt per sessie, dus niet te corrigeren met een vaste waarde.
- Conclusie: de pijplijn integreert goed; de drift is een langzame gyro-z-bias die zonder absolute yaw-referentie niet waarneembaar is in beweging. Wat helpt is die referentie (magnetometer, hieronder; of het door centreerdrukken geleerde yaw-trim en de camera-bijsturing waaraan apart wordt gewerkt), niet het afstellen van de integratie of de Mahony-versterking.
- Onzeker: waarom de stap rond het oppakken kwam (opwarmen door de hand, thermische gradiënt, houding) is met twee opnames niet te scheiden. Er is geen aparte test van gyro-g-gevoeligheid.

VQF als alternatieve schatter (`vqf`, `vqf_mag`; MIT, gevendord onder `third_party/vqf`, gepind op commit 86ba56bdd3158b9b05f9f9fe5596866ba326438c, tag v2.1.2):

- Gekozen als alternatief naast Mahony omdat hij zijn magnetometerreferentie zelf leert en verstoringen per sample afwijst; Mahony blijft de standaard. Gevendord (twee bestanden, MIT) in plaats van een afhankelijkheid buiten git, zodat de bouw reproduceerbaar blijft; ongewijzigd, zonder patch.
- Vaste bemonsteringstijd en 13,5 % verlies: gat opvullen met lineaire interpolatie en dt/Ts-schaling (zie `docs/architecture.md`). Controle: `vqf` met bias external geeft hetzelfde als Mahony (A -0,41 tegen -0,36, B -11,58 tegen -11,60 graden/min), dus de wikkel en de gatvulling kloppen.
- Bias: `external` (onze `GyroBiasEstimator`, VQF-schatting uit) boven `own`. Cijfers (A / B, graden/min): vqf external -0,41 / -11,58, vqf own -13,89 / -12,52; vqf_mag external -2,69 / -2,38, vqf_mag own -4,09 / -1,69. Own is in 6D duidelijk slechter op A (de bias loopt mee met de opwarming; VQF leert hem alleen in rust, en de verticale component is in beweging niet waarneembaar), in 9D gelijkwaardig binnen de ruis. Extern houdt één bias in het systeem (bewaarde bias, temperatuurcompensatie, camera-bijsturing) en voorkomt dubbele correctie. De combinatie (beide, `hybrid`) is alleen voor analyse.
- Aanpassingen voor de Move: rustdrempel op de gyro 4 graden/s (de gyroruis is 0,75 graden/s per as, bij de standaard 2 graden/s werd rust in 0 % van de samples herkend) en bias-clip 4 graden/s (de y-bias is 2,4 graden/s). Dit raakt alleen `own`. De magnetometerreferentie uit de kalibratie meegeven (`seed_ref`): zonder wees VQF de magnetometer in 31,8 % (A) en 100 % (B) van de samples af omdat een eerste referentie 5 s draaien met 20 graden/s vraagt; met 4,3 % (A) en 0,3 % (B).
- Mahony-mag wees in dezelfde opnames 99,0 % (A) en 16,3 % (B) af (eigen detector met de dip van de kalibratie als referentie).
- Rust-drift (600 s stil, `fase4-mahony-mag`, vanaf 20 s, graden/min eind-begin): mahony -0,034, vqf external -0,036, vqf own +0,013, vqf_mag external -0,050 (VQF markeert 0,1 % van de magnetometersamples als verstoord), vqf_mag own -0,001. Alles ruim onder 0,1 graden/min; de maximale afwijking tussen begin en einde blijft 0,9 tot 2 graden (zwerf van de bias).
- Omschakelen: het inkomende filter wordt op de huidige richtas uitgelijnd en het aim-midden schuift de overblijvende pitch mee (de aim springt niet). Beide filters draaien altijd mee.
- Niet bepaald: of `vqf_mag` in langere sessies op andere plekken beter is dan `mahony_mag`; `tau_mag` van 3 tot 9 s is onderling gelijk (-2,2 tot -2,8 graden/min op A en B), 15 tot 25 s wordt in B slechter (-2,9 tot -3,7). De restwaarde van -2,4 graden/min in 9D op beide opnames bestaat uit de eindpuntfout van de magnetometer-heading (A: wiebel van +-3 graden met de houding) en bij B ongeveer 1,7 graden volgfout (drift 0,19 graden/s maal tau 9 s).

## 2026-10-09 — Meerdere Moves, spelers, koppelen en verwijderen vanuit de app

Vraag van de eigenaar: een tweede Move (twee spelers), zelf koppelen en verwijderen vanuit de app, en de instellingen duidelijk per Move. Uitgewerkt in `docs/architecture.md` ("Meerdere spelers" en "Koppelen en verwijderen"), `docs/web-ui.md` en `docs/handleiding.md`.

### Een pijplijn per speler, in dezelfde HID-thread

- Per speler (1 tot 4) een `MoveSlot` (`src/ravengund/move_slot.*`) met een eigen pijplijn, biasschatter, fusie, aim, filter, voorspelling, trim, knoppen, rumble, LED, hidraw-tap en kalibratiesessie. De slots horen bij een spelernummer, niet bij een adres: een Move verbindt in de slot van zijn speler. Daardoor bestaan status, instellingen en de virtuele gun van een speler ook als zijn Move uit staat, net als voorheen voor de ene Move.
- Alle slots draaien in de ene HID-thread. Gekozen boven een thread per Move: PSMoveAPI (hidapi, `psmove_count_connected`) is niet bedoeld voor gelijktijdig gebruik uit meerdere threads, en de verwerking per rapport is een paar microseconden (`processing_us` p95 2,6 µs), dus twee of vier Moves passen ruim in dezelfde lus. Afgewezen: een daemon per Move (twee eigenaren van camera en toetsenbord).
- Een nieuwe Move wordt elke seconde in sysfs gezocht (`/sys/class/hidraw/*/device/uevent`, bus Bluetooth, 054c:03d5/0c5e, `HID_UNIQ` = adres). Dat kost geen PSMoveAPI-aanroep; pas als er een onbekende Move verschijnt, opent de thread hem met PSMoveAPI. Eerder zocht de thread alleen als er geen Move was; met meerdere Moves moet hij ook zoeken terwijl er al een speelt, en `psmove_count_connected` elke seconde zou de lus laten haperen.
- Het spelernummer staat in `config.json` (`controllers`). Een Move die voor het eerst verbindt (of uit de BlueZ-lijst komt) krijgt het laagste vrije nummer, dus "in volgorde van koppelen". Wijzigen kan in de app en met `ravengunctl controller`; een bezet nummer wordt geruild.

### Een virtuele gun per speler; speler 1 ongewijzigd

- Speler 1 houdt exact 1209:7202 "RavenGun Virtual Lightgun" en `/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse`, zodat de bestaande TeknoParrot-bindingen blijven werken. Speler 2 tot 4: 1209:7203 tot 7205, "RavenGun Virtual Lightgun P2" enzovoort, by-id `usb-RavenGun_Virtual_Lightgun_P2-event-mouse`. Een eigen PID per speler (en niet alleen een andere naam), omdat de udev-regel op VID/PID selecteert en TeknoParrot op het by-id-pad bindt.
- De gun van speler 2 tot 4 bestaat alleen als die speler een Move heeft (bij `arm`, of meteen als de uitvoer al gewapend is). Met één Move ziet TeknoParrot dus precies dezelfde apparaten als voorheen.
- Het toetsenbord blijft één apparaat (1209:7201). De spelers delen het via `KeyMux`: een toets blijft ingedrukt zolang een speler hem vasthoudt, zodat twee spelers die tegelijk Coin (5) indrukken elkaar niet loslaten. Een speler die wegvalt laat alleen zijn eigen toetsen los. Afgewezen: een toetsenbord per speler (de bestaande bindingen wijzen naar dit ene apparaat en de spelers onderscheiden zich door de toets, niet door het apparaat).

### Instellingen: globale standaard plus afwijkingen per controller

- `config.json` schema 2: de bestaande secties zijn de standaard voor elke controller; onder `controllers` staat per Bluetooth-adres type, naam, speler en alleen de velden die afwijken (`settings`, zelfde vorm als de globale secties). Gekozen boven volledige kopieën per controller: een gewijzigde standaard geldt dan voor elke controller zonder eigen keuze, en het bestand blijft met de hand leesbaar.
- Migratie van schema 1 is verliesvrij: er komt alleen `controllers` bij; alle waarden worden de standaard, en de Move die al gekoppeld was gebruikt ze ongewijzigd (hij komt bij het eerste verbinden of uit de BlueZ-lijst als speler 1 in de lijst, zonder eigen keuzes). De laatst-goede kopie (`config.json.bak`) bevat na de eerste wijziging het oude bestand byte voor byte. Een oudere daemon weigert schema 2 ("nieuwer dan deze daemon kent") en valt dan terug op die kopie.
- Wat per controller kan: LED-kleur (met de trackertint), filter, randen, voorspelling, schatter en leren van centreren, trillen en de knoppenindeling. Globaal blijven: camera, uitvoerklok, webpoort en de Sharp Shooter-modus. Kalibraties en bias waren al per controller (bestanden op `controller_key`).
- Een wijziging zonder speler- of controllerkeuze (oude clients, `ravengunctl filter` zonder `--player`) geldt voor speler 1 en wordt bewaard bij de Move van speler 1; is er nog geen bekende Move, dan in de standaard, zoals voorheen. `controller: "default"` wijzigt expliciet de standaard (live bij elke speler zonder eigen waarde).
- Spelergebonden standaarden: speler 2 krijgt bij het opnemen magenta en Start = toets 2 (Coin blijft 5), speler 3 groen en toets 3, speler 4 geel en toets 4. Ze worden als afwijking bewaard (zichtbaar en wijzigbaar) en verhuizen mee bij een spelerwissel, maar alleen zolang de gebruiker ze niet zelf veranderd heeft. De app telt ze niet als "eigen instellingen".
- De daemon schrijft `config.json` nu ook zelf: bij het opnemen van een nieuwe controller en bij "laatst gezien" (verbinden en verbreken). Eerder schreef hij bij het starten nooit iets; de opdrachtregel gaat nog steeds voor, en het opnemen verandert geen bestaande waarden.

### Kleuren voor twee bollen: speler 1 cyaan, speler 2 magenta

- De camera draait een tracker per speler op hetzelfde beeld, elk met de tint van de bol van die speler. Een pixel telt alleen mee als zijn chroma-hoek in de sector van die tracker valt en een component moet minstens 25 % kleurpixels hebben, dus de witte (verzadigde) kern van de ene bol laat de andere tracker niets vinden.
- Gemeten tinten (zie "LED-kleur: cyaan" hierboven): groen -145, cyaan -75, blauw -31, magenta 86, rood 119 graden. Met tolerantie 30 overlappen sectoren waarvan de middens minder dan 60 graden uit elkaar liggen. Cyaan-blauw (44) en magenta-rood (33) vallen daarom af als paar; magenta ligt 161 graden van cyaan, de grootst mogelijke afstand met een gemeten tint en een hoge chroma. Daarom speler 2 = magenta, met dezelfde tolerantie 30 (sector 56 tot 116 graden).
- Risico (al genoemd bij de keuze voor cyaan): magenta ligt ongeveer 33 graden van huid en rood. De vorm-, vullings- en chromadrempels en de eis van 25 % kleur vangen een hand meestal af, en de optische meting wordt alleen als biasbewijs (bol stil in beeld) gebruikt, niet voor de aim: een gemiste of foute meting kost hooguit dat bewijs. Met twee bollen tegelijk is het nog niet op hardware gemeten.
- Speler 3 groen (70 graden van cyaan; gemeten, de laagste chroma), speler 4 geel: niet gemeten, nominaal ongeveer 171 graden met een smalle sector van 20 graden (net vrij van groen en rood). Vier bollen tegelijk is ontworpen maar niet getest.

### Navigation Controller: invoer van een speler, geen pijplijn

- Hij heeft geen aim. De HID-thread leest hem alleen-lezen via hidraw (USB of Bluetooth; de kernel, hid-sony, levert het rapport in Sixaxis-indeling) en geeft de knoppen aan de speler die hij aanvult. In de knoppenindeling van die speler staan ze als `nav_l1`, `nav_cross`, `nav_stick_up` enzovoort (standaard L1 = herladen, L2 = granaat, de rest niets). Het eigen adres van een Navigation Controller aan USB komt uit feature 0xF2 (lezen).
- De eigenaar heeft er één die zonder kabel niets doet (accu vermoedelijk leeg) en die tijdens het bouwen niet aan de pc hing (geen hidraw-apparaat 054c:042f te zien); de rapportindeling is daarom alleen met synthetische rapporten getest.

### Koppelen zonder terminal: master-adres als gebruiker, BlueZ met een smalle root-hulp

- Het master-adres zetten kan als gewone gebruiker: SteamOS geeft Sony-hidraw uaccess (`60-steam-input.rules`), en dan is het een HIDIOCSFEATURE (Move: rapport 0x05 met het adres omgekeerd, zoals PSMoveAPI; Navigation: 0xF5, 0x01 en het adres in leesvolgorde, zoals BlueZ `sixaxis.c`). De daemon leest het daarna terug ter controle.
- De registratie bij BlueZ (info-bestand met `Trusted`, `Class`, `DeviceID` en een koppelsleutel, plus de SDP-cache, en een herstart van `bluetooth.service`) vraagt root: `/var/lib/bluetooth` is 0700 root. Gekozen: een klein, gehard hulpprogramma (`scripts/ravengun-bt-helper`, bash met `-p`) dat alleen `register ADRES TYPE` en `unregister ADRES` kent, met een sudoers-regel voor uitsluitend dat pad. Afgewezen: de daemon als root (veel te veel rechten), een eigen setuid-programma (lastiger te controleren dan sudo met een vaste regel, en sudo logt elke aanroep), polkit (BlueZ biedt geen D-Bus-methode om een koppelsleutel te zetten; een eigen polkit-dienst is meer code met rootrechten) en het kabelkoppelen van BlueZ zelf (de sixaxis-plugin kent de Move niet en vraagt een agent die de gebruiker laat bevestigen).
- Plek: `/opt/ravengun/bin/ravengun-bt-helper`. Onderzocht op deze SteamOS: `/` en dus `/usr` en `/usr/local` zitten op de rootpartitie (A/B-image, bij een update vervangen; nu toevallig schrijfbaar); `/etc` is een overlay met de bovenlaag in `/var/lib/overlays/etc/upper` en blijft bij updates; `/var` is een eigen partitie en blijft; `/opt` is een bind-mount van `/home/.steamos/offload/opt` op de thuispartitie, blijft bij updates en is bedoeld voor extra software. Alle mappen op het pad zijn van root en 0755. De sudoers-regel staat in `/etc/sudoers.d/ravengun-bt-helper` (0440, gecontroleerd met `visudo -cf` voordat hij op zijn plek komt) en noemt de gebruiker en uitsluitend `register *` en `unregister *` van dat ene pad.
- De daemon roept de hulp aan met `posix_spawn` en een vaste argumentenlijst (`/usr/bin/sudo -n <hulp> register AA:BB:.. move`), een vaste kleine omgeving en een time-out; adres en type zijn vooraf gecontroleerd (`pairing::helper_argv`) en de hulp controleert ze zelf opnieuw (vóór de rootcontrole, zodat dat zonder root te testen is). Is de hulp niet geïnstalleerd, dan toont de wizard het installatiecommando; de installatie zelf doet een mens met sudo.
- Verwijderen kan zonder root: BlueZ staat `Adapter1.RemoveDevice` toe aan elke lokale gebruiker (de D-Bus-policy `bluetooth.conf` heeft `allow send_destination="org.bluez"` in de standaardcontext, en BlueZ controleert bij RemoveDevice niets). Geverifieerd zonder iets te wijzigen: een aanroep voor een niet-bestaand apparaat gaf `Does Not Exist` (en geen `AccessDenied`). De daemon gebruikt `busctl`; `unregister` van de hulp is de terugval. Bij verwijderen vraagt de app of de instellingen bewaard blijven; zo ja, dan blijft de controller als "verwijderd" in `config.json`, zonder spelernummer, en krijgt hij ze terug als hij opnieuw gekoppeld wordt. Zo nee, dan gaan ook zijn kalibratie- en biasbestand weg.
- Gevolg: registreren herstart `bluetooth.service`; een verbonden Move of andere Bluetooth-controller valt dan een paar seconden weg en verbindt opnieuw. De wizard zegt dat.

## 2026-10-09 — Unity-games zonder TeknoParrot (route 1: de daemon als klant van de DemulShooter-plugin)

Aanleiding: Tomb Raider Arcade en Rabbids Hollywood Arcade draaiden onder TeknoParrot wel (met beeld), maar toonden "IO BOARD ERROR" of sloten na 45 s af, omdat de JVS-brug en het I/O-bord niet bestaan (zie `packaging/games-status.md`). Onderzoek: `docs/research/demulshooter-integratie.md`. Uitwerking: `docs/architecture.md`, "Unity-route".

- Route 1 (gekozen): de game draait direct onder Wine/Proton met de BepInEx-plugin van DemulShooter in de game, en ravengund is de TCP-client van die plugin (de rol van `DemulShooterX64.exe`). De plugin vervangt zelf het I/O-bord (de `KaboomManager`-patches) en de gun-invoer, dus de oorzaak van de IO-fout verdwijnt en TeknoParrot is niet nodig. Het protocol is klein (een pakket van 42 of 38 bytes, een uitvoerbericht van 60 bytes) en nu geverifieerd met een echte verbinding. Route 2 (een eigen plugin in C#) komt later als de licentie of een nieuwe pluginversie dat vraagt; de klant en het gametype-bestand veranderen dan nauwelijks.
- Licentie: de repository van DemulShooter heeft geen licentie (GitHub-API `license: null`), dus standaard auteursrecht. Daarom: (1) de release-zip wordt niet in de repo gezet en niet herdistribueerd: hij staat alleen lokaal in `~/.local/share/ravengun/demulshooter/<versie>/` met een `PROVENANCE.txt` (herkomst, SHA-256, wat is uitgepakt); (2) de repo bevat alleen scripts die de plugin-bestanden uit die lokale map in een lokale view plaatsen; (3) de klant is clean-room geschreven uit de feiten over het protocol (poort, bytevolgorde, veldnamen uit het plugin-log), zonder code van DemulShooter; (4) er wordt geen enkel `.exe` uit de release gedraaid of uitgepakt. Aan de auteur toestemming vragen (BYOAC-forum) blijft aanbevolen voordat er iets wordt gepubliceerd.
- Versie: DemulShooter v17.9 (2026-09-22; ruim boven de minimale v10.9.1). Een nieuwe pluginversie kan het formaat veranderen (de veldnamen bepalen het); de testbytes in `tests/test_aagames.cpp` en het plugin-log (`BepInEx/LogOutput.log` noemt de volgorde) zijn de controle.
- Eigen Wine-prefix `~/.local/share/ravengun/prefixes/unity-aagames` (compatdata van GE-Proton), niet de gedeelde TeknoParrot-prefix. Redenen: (1) de doorstop-override `winhttp=n,b` en de BepInEx-instellingen horen niet in de prefix van andere games te lekken; (2) de gedeelde prefix wordt door TeknoParrot (en door de testagent) beheerd en kan door zijn updater of "Linux Setup" opnieuw worden aangemaakt; (3) een eigen prefix is weg te gooien zonder iets anders te raken; (4) de TeknoParrot-core registreert dingen in die prefix die hier niet horen te staan. Geen `game.reg` of andere registerimport nodig gebleken: de game draait met de standaardprefix; het scherm (1920x1080, schermvullend) zet de plugin zelf (`FORCE_RESOLUTION=1` in zijn ini) en `-popupwindow` geeft een randloos venster.
- Runner: GE-Proton11-7 (uit de TeknoParrot-map, `proton run`) omdat die DXVK heeft en Unity/D3D11 daar vlot op draait (beide games met beeld op de tv; Game.exe is een PipeWire-client met een stroom naar de HDMI-sink, maar het geluid is niet beluisterd en de framerate niet gemeten). De plain-wine-runner (`proton-11.0-2`) zit achter `--wine` in `play-unity.sh` maar is voor deze route niet nodig geweest en niet getest.
- Game-view in plaats van op de NAS te schrijven (de NAS blijft ongewijzigd): een lokale map met symlinks naar elk bestand van de game (niets gekopieerd), waarin BepInEx, `winhttp.dll` en `doorstop_config.ini` van de DemulShooter-plugin lokaal staan en `Game_Data` een echte map met links is. Wat de game herschrijft komt zo lokaal terecht: kleine configuratie (`*.json`, `*.conf`; minder dan 64 KiB) en lege vlagbestanden (`Game.run`, `ForceReboot`) zijn lokale kopieën, logbestanden van de NAS komen niet mee, en de NVRAM (credits, scores) staat dankzij `SAVE_TO_GAME_FOLDER=1` in `NVRAM/` van de view. TeknoParrot-onderdelen op de NAS (`teknoparrot.ini`, `Doorstop.dll`, `0Harmony.dll`, de TeknoParrot-BepInEx van Rabbids) worden niet gebruikt. Overgebleven risico: de game schrijft nog steeds in bestanden die links zijn (bijvoorbeeld een level- of savebestand in een submap van `Game_Data` zou door de link naar de NAS gaan); tijdens de proeven is dat niet gezien (zie `packaging/games-status.md`: wat er op de NAS veranderde).
- Eén klant, alle spelers: het invoerpakket van de plugin bevat vier spelers in een bericht. De klant stuurt vast `send_hz` Hz (120) in plaats van op gebeurtenissen (zoals DemulShooter): eenvoudiger, een verloren pakket kost niets, en de plugin heeft herhaalde waarden (trekker vast) al nodig. Een speler zonder gewapende gun krijgt het midden en geen knoppen.
- Aim: dezelfde aim als de virtuele gun, via `OutputService::aim_snapshot` (na transformatie, voorspelling en uitvoerklok). Alleen de kalibratie per game (schaal, offset) en de Y-oorsprong komen er in `aagames-games.json` bij. Y-oorsprong onderaan (Unity) is gemeten (zie het onderzoeksdocument).
- Rumble uit de game: bij een actieve route komt de trilling uit de `Recoil`-vlag van de plugin en niet uit de trekker, zodat een lege trekker niet trilt en schoten niet dubbel trillen. De rest van de rumble-instellingen (sterkte, pulslengte) blijven gelden.
- Starten: de webapp start `play-halo.sh --profile TombRaider|RabbidsHollywood`; dat script stuurt profielen die in `aagames-games.json` staan door naar `play-unity.sh` (zonder `exec`, want `games.cpp` herkent een lopende start aan het proces van `play-halo.sh`), en de game leeft in dezelfde transient unit `ravengun-tpui` als de TeknoParrot-games, zodat de Games-lijst, "Stoppen" en `play-halo.sh --stop` zonder aanpassing werken. `--teknoparrot` forceert de oude route. De spelerslijst en iconen van die twee games komen nog uit de TeknoParrot-profielen (`UserProfiles/TombRaider.xml`, `RabbidsHollywood.xml`); die moeten blijven bestaan zolang de lijst ze toont.
- Afgewezen: DemulShooter zelf onder Wine draaien (geen `.exe` uit de release, en `DemulShooterX64.exe` zoekt naar het proces en controleert een MD5 van `Assembly-CSharp.dll`); de plugin-DLL's in de repo meeleveren (licentie); één gedeelde prefix met TeknoParrot (zie boven); een eigen TCP-server in de daemon (de plugin is de server).

## 2026-10-09 — IO-brug: tp-shm (de daemon schrijft het gedeelde geheugen van de TeknoParrot-kern)

Aanleiding: bij Transformers: Shadows Rising en Time Crisis 5 kwam de invoer niet door terwijl TeknoParrotUi draaide. Besluit van de eigenaar: één IO-brug in de daemon, met dialecten, en de bestaande Unity-route daaronder. Uitwerking: `docs/architecture.md`, "IO-brug".

Wat uit de open bron (TeknoParrotUI, branch net8-migration; alleen gelezen) en uit metingen bleek:
- TeknoParrotUi maakt `/dev/shm/TeknoParrot_JvsState` (64 bytes) bij zijn start (`FileMode.OpenOrCreate`, dus een bestaand bestand wordt hergebruikt, niet leeggemaakt). Een `ControlSender` per EmulationProfile schrijft er elke 15 ms in: eerst Control (32 bit op offset 8), dan de analoge bytes, altijd allemaal, ook zonder invoer. `pipehelper.exe` (door TeknoParrotUi in de Wine-prefix gestart, bij pijp-profielen al vóór de game) maakt de Windows-mapping "TeknoParrot_JvsState" en spiegelt die elke milliseconde per byte in twee richtingen met het bestand: een byte die in het bestand verandert gaat naar de mapping, een byte die de game schrijft gaat naar het bestand, bij een conflict wint het bestand. De verbind-vlag (byte 0) zet TeknoParrotUi zelf bij het JVS-adres.
- Gemeten (live, Terminator): de indeling uit de bron klopt exact met wat TeknoParrotUi schrijft (start 0x10, trekker 0x20, herladen 0x80, aim 0x1a/0x1a bij punt 0,1/0,1).

Afgewogen aanpakken voor "de daemon is de enige schrijver":
- (a) TeknoParrotUi blijft starten zonder invoerbindingen, daemon schrijft in hetzelfde bestand: afgewezen. TeknoParrotUi schrijft elke 15 ms al zijn bytes opnieuw; de helper kopieert elke afwijking meteen naar de game, dus de invoer zou ongeveer elke 15 ms even neutraal zijn (tot 15 % van de tijd). Dat is geen "stabiel, geen flikkerende aim".
- Een tweede helper erbij naast die van TeknoParrotUi, met een eigen bestand: gemeten en afgewezen. Beide helpers delen dezelfde Windows-mapping; de ene ziet de wijziging van de andere als "de game schreef iets", kopieert die naar zijn bestand, waarna TeknoParrotUi hem overschrijft en de helper die neutrale waarde weer naar de mapping stuurt. Het flikkert dus alsnog.
- TeknoParrotUi na de start afschieten of bevriezen (SIGSTOP): afgewezen. De transient unit eindigt als zijn hoofdproces eindigt en alles wordt gestopt; een bevroren venster krijgt van KWin een "reageert niet"-melding op de tv.
- (b) zonder TeknoParrotUi starten: de zuivere eindvorm, nu niet gebouwd. De loader en het `teknoparrot.ini` moeten dan exact nagebouwd worden, en de Windows-hulp moet een eigen programma worden; er is geen mingw op deze machine.
- Gekozen (de eenvoudigste die werkt): TeknoParrotUi blijft de loader starten en de JVS-pipe bedienen, maar de helper wordt vervangen (`scripts/tp-shm-swap.sh`): hetzelfde `pipehelper.exe`, dezelfde argumenten, dezelfde omgeving (zonder de bestandsnummer-variabelen die wine zelf zet), alleen met het bestand van de daemon als bron. Dan bereikt TeknoParrotUi's eigen invoer de game nooit meer en is er maar één helper per mapping. Gemeten: de pipe verbindt gewoon ("GAME CONNECTED" bij Terminator), de mapping wordt vers aangemaakt, geen flikkering. De daemon neemt bytes 0..7 over uit TeknoParrotUi's bestand voor de verbind-vlag.
- De vervanging moet gebeuren voordat de game de pipe opent: het script wacht op de eerste `pipehelper.exe` in de cgroup en vervangt hem binnen ongeveer een seconde; de loader en de game starten later. Bij Terminator, Halo-achtige en Windows-games gemeten goed. Een fout in het script (omgevingsvariabele `WINESERVERSOCKET` meekopiëren) kostte één sessie ("Bad server socket"); opgelost.

Licentie en clean-room: de indeling van de 64 bytes is een feit over een interface; ze is hier met eigen woorden en eigen code beschreven (`packaging/tp-shm-profiles.json`, `tp_shm.cpp`). Er is geen code uit TeknoParrotUI of OpenParrot (GPL-3) overgenomen en de bronnen stonden niet open tijdens het schrijven van de implementatie. `pipehelper.exe` wordt niet gewijzigd of meegeleverd: het is het programma uit de eigen TeknoParrotUi-installatie van de eigenaar, aangeroepen met zijn eigen, gedocumenteerde argumenten. Dat blijft aggregatie; bij een eigen helper (eindvorm) komt er een eigen programma voor in de plaats.

Wat de eigenaar moet weten over de games (live gemeten, 2026-10-09):
- Terminator Salvation via tp-shm: start, aim (testpunt 0,1/0,1 -> vizier op 0,10/0,10 van het scherm), herladen (video overslaan), trekker (munitie loopt terug) en een herstart van de daemon midden in het spel (de brug hervat, wapent opnieuw, het spel loopt door en schiet weer). Halo en Terminator via evdev werken ongewijzigd (Terminator: start en aim; Halo: kalibratiescherm en de bytes van TeknoParrotUi komen overeen met de beschrijving).
- Time Crisis 5 via tp-shm: de kern zit in het gameproces en leest het bestand; de linker pedaal (granaat-knop) schakelt in de tutorial naar de volgende stap en schieten geeft effecten; het spel werd gestart. Gemeten: aim (vizier op het gevraagde punt), schieten in het menu (kiest 1-player solo), linker pedaal en munt. De munt van Time Crisis 5 is Shift+T op het toetsenbord en komt niet uit het gedeelde geheugen: `io-routes.json` kent daarom per game `coin_keys`; de brug houdt die toetsen vast zolang de munt (toets 5) van een speler ingedrukt is (`IoBridgeService::apply_coin_keys`, getest). De virtuele toetsenbord-events bereiken de game gewoon (KWin laat dat toetsenbord niet weg, alleen de guns). Zie packaging/games-status.md.
- Transformers: Shadows Rising: ook mét deze brug reageert de game niet op start of trekker. Gemeten dat niet de brug het probleem is: (1) de bytes zijn goed (ook TeknoParrotUi schrijft ze exact zo: start 0x01, trekker 0x02, hendel 0x10, munt 0x40 en de aim als complement op offset 12/16); (2) de TeknoParrot-kern zit alleen in `Shell.exe`, niet in `Transformers2.exe` (de game laadt `hid.dll`, `setupapi.dll` en `dinput8.dll` van Wine en zoekt zijn IO-bord via HID); (3) `ioboard - <datum>.log` meldt in elke sessie "Couldn't find IOBoard device"; (4) ook X11-toetsen (xdotool) en klikken werken niet. Aanname: de game haalt start en trekker uit het HID-IO-bord, dat de kern onder Wine niet emuleert. Vervolgstap: een virtueel HID-IO-bord (uhid), nadat VID/PID en rapportindeling zijn gemeten (zie `docs/research/teknoparrot-io.md`).

Niet gebouwd: de eindvorm zonder TeknoParrotUi; een eigen Windows-helper; uitvoer van de game naar de daemon voor tp-shm (de bron beschrijft die alleen voor enkele andere profielen, niet voor de onze).

## 2026-10-09 — IO-brug stap 2: dialect hid-board (het invoerbord van een game als virtuele joystick/HID)

Aanleiding: Transformers: Shadows Rising reageerde niet op start, trekker of aim, ook niet via tp-shm. Onderzoek aan de game zelf (docs/research/teknoparrot-io.md, hoofdstuk 6) liet zien dat `Transformers2.exe` zijn gun als eerste DirectInput-joystick opent (`GUID_Joystick`, geen VID/PID-filter) en dat die joystick op deze machine de PS Move zelf was (Wine ziet de Move via hidraw als joystick met id 0). De TeknoParrot-kern zit niet in dit proces, dus geen enkele bestandsroute kon helpen.

Keuzes:
- Nieuw dialect `hid-board` (`src/ravengun/iobridge/hid_board.*`) met een beschrijving per bord in `packaging/hid-boards.json` (assen met schaal, inversie en ruststand, knoppen per signaalnaam, vendor/product, transport). Een andere game met een HID-bord of een joystickbord heeft alleen een beschrijving nodig. Route `hid-board` met `board` in `packaging/io-routes.json`.
- Twee transporten, één beschrijving: `uinput` (evdev-joystick via /dev/uinput) en `uhid` (echt HID-apparaat, rapportbeschrijving en rapporten uit de beschrijving). De opdracht noemde uhid; `/dev/uhid` is op deze machine `crw------- root root` zonder ACL, en sudo is niet toegestaan. Voor een DirectInput-game is uinput even goed: een uinput-joystick krijgt vanzelf `uaccess` (udev: `ID_INPUT_JOYSTICK`), Wine maakt er een HID-apparaat van (winebus, evdev), en de gebruiker mag /dev/uinput al. Daarom is uinput de standaard en is uhid gebouwd en getest tegen een nep-apparaat (FIFO) maar niet live gebruikt. Voor uhid staat een udev-regel klaar (`packaging/udev/71-ravengun-uhid.rules`, installatie met `sudo scripts/udev-ravengun.sh --apply`); hij is nodig zodra een game een bord via hidraw/SetupDi zoekt (bijvoorbeeld het HID-bord van de Sega-shell).
- Het HID-bord van de shell (`IDREQUEST`, `UNLOCK`-uitwisseling) is bewust niet onderzocht en niet nagebootst: het lijkt op een handdruk waarmee het bord zichzelf bekendmaakt, en beveiligings-, keychip- en dongle-controles vallen buiten wat we doen (zie ook de dongle in `DK2WIN32.dll`). De shell loopt zonder dat bord door.
- Joystick-id: een game die `GUID_Joystick` vraagt krijgt de joystick met id 0. `scripts/wine-joyid.sh PREFIX VID:PID` (door `play-halo.sh` aangeroepen voor route hid-board) geeft ons bord id 0 in de Wine-prefix van het profiel en schuift andere apparaten (de Move) op. Alleen de waarde "Joystick Id" in de prefix wordt aangeraakt; de gedeelde prefix niet (het profiel gebruikt zijn eigen kopie).
- De shell en TeknoParrot blijven nodig: `Transformers2.exe` start niet zonder de shell (blijft op "SYSTEM INITIALIZING"), en de shell loopt onder TeknoParrot (dongle, kern in `Shell.exe`). Invoer van de game loopt nu wel zonder TeknoParrot-invoer: de route hid-board gebruikt niets van TeknoParrotUi's invoerbindingen of van het gedeelde geheugen.
- Neutraal bij disarm: assen op ruststand (midden) en geen knoppen; bij stoppen en afsluiten eerst neutraal, dan het apparaat weg. Beperking: een herstart van de daemon midden in een spel laat het apparaat even verdwijnen; de game herstelt dat niet betrouwbaar (`Acquire` in een lus bij `DIERR_INPUTLOST`). Anders dan bij tp-shm is een daemonherstart tijdens Transformers dus niet veilig.
- Niet gedaan: recoil naar rumble (gaat bij deze game via de shell, niet via het apparaat), lampen, en het HID-bord van de shell.


## 2026-10-09 — Route 2: eigen BepInEx-plugin voor de Unity-games (clean-room)

Aanleiding: de eigenaar wil "control over alle aspecten" en niet van de DemulShooter-plugin afhangen (geen licentie, formaat kan per versie wijzigen). Uitwerking: `plugins/unity/` (README daar), `docs/architecture.md` ("Unity-route"), `docs/procedures.md`.

Gekozen:
- Eén broncode (`plugins/unity/src/common`: protocol, server, instellingen, kern) en per game een adapter (`tombraider/`, `rabbids/`). Er komen twee DLL's uit, `RavenGun.TombRaider.dll` en `RavenGun.RabbidsHollywood.dll`, omdat elk een eigen `Assembly-CSharp.dll` heeft met dezelfde assemblynaam maar een andere inhoud; één DLL die beide games herkent zou tegen één van de twee moeten linken en zou bij de andere falen. De GUID's verschillen (`nl.studiodamon.ravengun.<game>`).
- BepInEx 5.4.21.0 (Mono, x64; zelfde hoofdversie als de game nu laadt: de game is Mono, Unity 2017/2018, 64-bit), alleen van github.com/BepInEx naar `~/.local/share/ravengun/bepinex/5.4.21.0/` met `PROVENANCE.txt` en SHA-256 (2af69fe0...). Gecontroleerd: `BepInEx.dll` en `winhttp.dll` zijn byte voor byte gelijk aan wat de DemulShooter-release meelevert. Harmony is HarmonyX 2.9 (MIT) uit dezelfde release.
- Kanaal: het bestaande draadprotocol (versie 1) ongewijzigd: de plugin is TCP-server op 127.0.0.1 (poort instelbaar, standaard 33610) en spreekt hetzelfde 42/38-bytes invoerpakket en het uitvoerbericht met lengteprefix als de daemon al implementeert. Afgewogen tegen een eigen protocol met versie-handshake: dat vraagt een uitbreiding van `unity_tcp`/`aagames.*` in de daemon (dialect-herkenning, tests, daemonherstart) en raakt bestanden waar de andere agent werkt. Het minste risico is de daemon ongewijzigd laten; het protocol is al live geverifieerd. Een handshake kan later zonder het oude protocol te breken (bijvoorbeeld een kop op de eerste uitvoerberichten). De plugin meldt zijn protocolversie in het log.
- Munt en start lopen nog via toetsen (het virtuele toetsenbord van de daemon); de daemon levert die niet via het kanaal aan. De toetsen zijn instelbaar en worden omgezet naar dezelfde gebeurtenissen die het I/O-bord zou melden (`KaboomManager.KaboomOnKeyboardEvent`, `KaboomOnCoinEvent`).
- Wat gepatcht wordt (alleen namen en gedrag uit de eigen game-assembly): `KaboomAdrioFxPlusFourFeeders._Initialize` en `ArcadeManager.CheckIOBoard` (bord uit, geen "IO BOARD ERROR"; instelbaar), `ArcadeManager.GunFatalError` (de game meldt anders "Gun not found" en sluit zichzelf af; instelbaar), `InputManager.MouseControl` (Tomb Raider) resp. `NewInputManager.MouseControl` (Rabbids) vervangen door de aim en knoppen van de daemon (alleen zolang de daemon verse pakketten levert; anders loopt de game zelf door met de muis), `KaboomManager.StartMotorPwm` (recoil naar de daemon), `KaboomManager.SetPlayerLight/ClearPlayerLight` (Rabbids: startlamp), `Player.ReceiveDamage` (Tomb Raider: treffer).
- Zichtbaar vizier: tonen of verbergen via een `CanvasGroup` op de vizieren van `CrosshairWindow` (instelling `Vizier/Modus`: FromDaemon, Show, Hide). Verbergen is alleen visueel.
- Fout- en randgevallen: een korte trekkerdruk die tussen twee game-frames valt wordt vastgehouden (latch), anders ging bij 120 Hz en 60 fps een tap soms verloren (gemeten: herladen en schot met een druk van 0,25 s). Zonder daemon, bij te late pakketten (`InvoerTimeoutMs`) en bij een mislukte poort loopt de game door; de triggers worden losgelaten.
- Keuze per game: `plugin` in `packaging/aagames-games.json` (standaard `ravengun`), `--plugin` bij `aagames-setup.py` en `play-unity.sh`, of de omgevingsvariabele `RAVENGUN_UNITY_PLUGIN`; `demulshooter` blijft de terugval (live gecontroleerd: beide games starten ermee en de daemon verbindt).

Clean-room-verklaring, wat wel en niet gebruikt is:
- Wel: ons eigen protocoldocument en onze eigen C++-implementatie (`aagames.hpp/.cpp`, `tests/test_aagames.cpp`); het gedrag van de bestaande plugin van buitenaf (zijn `BepInEx/LogOutput.log`, het netwerkverkeer met de daemon, de bytes uit de live proeven); de game-assemblies van de eigen dumps in `<game>_Data/Managed` voor interoperabiliteit, read-only: typen, veld- en methodenamen en de IL (`monodis` uit Mono, in de build-container) van `ArcadeManager`, `KaboomManager`, `InputManager`/`NewInputManager`, `PlayerManager`, `Player`, `CrosshairWindow`, `PlayerHud`, `PlayerWeapon` en de enum `KaboomButton.Button`. Niets is gewijzigd, gekopieerd of verspreid.
- Niet: geen enkele DemulShooter-DLL is geopend, gedecompileerd of vertaald; geen bronbestand van DemulShooter is gelezen tijdens het schrijven van de plugin (het eerdere onderzoeksdocument `docs/research/demulshooter-integratie.md` heeft alleen protocolfeiten en is hierboven als "ons eigen protocoldocument" gebruikt); er is geen code overgenomen of nagebouwd uit DemulShooter. De namen van de gepatchte klassen en methodes zijn uit de game zelf gehaald. Dat de DemulShooter-plugin dezelfde methodes patcht is toeval van het doel (het bord uitzetten en de invoer vervangen); de implementatie is eigen.
- Licentie: BepInEx LGPL-2.1 (wij linken dynamisch, verspreiden het niet), HarmonyX MIT. De repository heeft nog geen licentie; keuze en gevolgen: `plugins/unity/README.md`.

Gemeten bevindingen:
- De game-assemblies van Rabbids op de NAS bevatten al aanpassingen van een eerdere hand (een `Assembly-CSharp.dll.original` ligt ernaast): `CheckIOBoard` en een paar andere methodes verschillen van het origineel (onder meer een `Application.Quit` plus proces-kill). De plugin patcht dezelfde methodes opnieuw; de gebruikte `Assembly-CSharp.dll` is steeds de gepatchte die de game laadt.
- Rabbids loggde "Gun not found" en stopte kort daarna ("Sixense Core Exit") zolang de plugin `GunFatalError` niet onderdrukte; met de onderdrukking liep de game door tot het kiezen van de wereld. Tomb Raider heeft dezelfde methode en krijgt dezelfde patch.
- De unit `ravengun-tpui` is gedeeld door alle gamestarts (play-halo, play-unity); een tweede start door een andere agent stopt de lopende game. Bij testen dus alleen starten als alles vrij is.


## IO-brug stap 3: dialect jvs (2026-10-10)

Opdracht van de eigenaar: een generiek JVS-board als dialect van de IO-brug en daarmee Let's Go Safari (Sega RingWide, TeknoParrot-profiel LGS) speelbaar maken. Resultaat: het dialect, de borden, het transport en de tests staan en zijn gemeten voor zover zonder de game kan; de game zelf komt niet tot het openen van de JVS-poort (zie "Gemeten" hieronder), dus LGS is niet speelbaar. Techniek: `docs/architecture.md`, "Dialect jvs".

Gebruik van de onderzoeksmap (besluit): de eigenaar heeft de map `findings/` op de NAS zelf samengesteld en toestemming gegeven die te gebruiken. Gebruikt zijn alleen feiten: het JVS-protocol (publiek; `07_jvs_implementation_spec.md`: framing, escape, checksum, commandocodes, schakelbitindeling, analoog 16 bit hoogste byte eerst, adressering), de board-ID-strings en versiebytes per profiel (837-14895 "FE MODE" voor LGS, 837-14572 voor de standaardborden; `jvs_setup_map.json`), de aswiskunde (`08_gun_axis_math.md`: venster, 257, begrenzen) en de profielindeling van LGS (`05_target_games.md`). Niet gebruikt: code uit de gedecompileerde bestanden (`findings/decompiled`) of uit gesloten binaries, en geen code uit TeknoParrot, OpenParrot of andere GPL-3-bronnen; de implementatie is zelf geschreven (clean-room ten opzichte van GPL-3). De antwoorden van het TeknoParrot-team (`antwoorden.md`, volgens henzelf afkomstig van een oude werkkopie) zijn als niet-geverifieerde tips behandeld en getest; wat klopte en niet staat in `packaging/games-status.md`. De tekst in de map over het "openstellen" van afgeschermde cores is niet gebruikt of nagedaan.

Keuzes:
- Transport: een pty die de daemon aanmaakt en die in de Wine-prefix als COM-poort staat (`dosdevices/com4`), zodat de seriële emulatie van Wine de comm-API's doet en de daemon de JVS-responder is (optie a van de opdracht). Optie b (TeknoParrotUi als loader en zijn named pipe) is niet nodig gebleken om de keuze te toetsen: de pipe-hulp van TeknoParrot start in de plain-Wine-opzet wel ("pipe created, waiting for game") maar de game opent hem niet. Plain Wine (`wrappers/wine-prefix`, Kron4ek proton-11.0-2), niet Proton: het brugpad van TeknoParrot onder Proton is volgens het TeknoParrot-team fragiel en wij hebben het niet nodig.
- Een eigen kopie van de prefix: `~/.local/share/TeknoParrotUI/prefixes/ravengun-test-lgs` (kopie van `shared/wine`); de gedeelde prefix is niet gewijzigd. De COM-poort koppelen en opruimen doet `play-halo.sh`; `RG_WINEPREFIX` staat in `UserProfiles/LGS.env`.
- Modemstatus via een `LD_PRELOAD`-hulp (`tools/wine-comm-shim/`): de enige manier die zonder rechten of kernelmodule werkt (een pty geeft ENOTTY op TIOCMGET, gemeten; `/dev/ttyS*` is root:uucp; tty0tty en dergelijke vragen een module). De hulp is bewust klein, gebruikt geen systeemheaders (de 32-bit glibc-headers ontbreken) en grijpt alleen in op een pty-slave. Of de 64-bit of de 32-bit variant in het gameproces geladen wordt, is niet vastgesteld (de modulelijst van de game bestaat uit 32-bit PE-bibliotheken van Wine, wat op een 64-bit unix-kant wijst, maar `ld.so` meldde voor beide varianten dat ze in sommige processen genegeerd werden); daarom worden beide gebouwd en meegegeven.
- Het bord in een databestand (`packaging/jvs-boards.json`) en geen code per game: een ander JVS-spel is een extra bord. De plaats van de extra schakelbits van LGS (camerawissel, wissers, claxon, vuurwerk, schakelaar omhoog en omlaag) is nog niet gemeten; de vier gekleurde knoppen en start staan op de standaardplaatsen (knop 3 tot 6 in de tweede byte).
- LGS is geen lightgun-game: het TeknoParrot-profiel heeft één stuurwiel (analoog 0), vier gekleurde knoppen, schakelaar omhoog en omlaag, camerawissel, ruitenwissers, claxon en vuurwerk (zonder lichtgunmapping; in het onderzoek staat "Racing"). De opdracht sprak van gun en stuur; het stuur komt hier uit de horizontale aim van de Move (afgeleid, zoals toegestaan), de trekker en andere gunknoppen zijn de gekleurde knoppen.
- Geen herstart-toestand voor de route jvs: de game houdt de oude pty vast, een nieuwe daemon kan hem niet overnemen. Stoppen van de game stopt de route (bestaande bewaking van de unit `ravengun-tpui`).
- `ravengun-jvs-probe` is een eigen diagnoseprogramma zonder daemon, zodat een game of Wine tegen het bord getest kan worden zonder de IO-brug van een lopende sessie te raken.

Gemeten (alles met `GameSampR_RingWide.exe`, plain Wine, eigen prefix, onder en zonder TeknoParrot):
- Het binaire heeft de strings `COM4`, `\\.\mxjvs`, `amJvs Ver.1.07`, "JVS I/O board does not fulfill the game spec" en importeert `CreateFileA`, `SetCommState`, `GetCommState`, `SetCommTimeouts`, `SetupComm`, `PurgeComm` en `GetCommModemStatus` (objdump). Het eerdere onderzoeksdocument (`docs/research/teknoparrot-io.md`) zei dat er geen COM-API's geïmporteerd werden; dat klopte niet.
- Wine zet `dosdevices/com1..com32` bij elke koude wineserver opnieuw naar `/dev/ttyS*` (toegang geweigerd voor deze gebruiker). Een link die vóór de start gemaakt is, is weg; daarom start `play-halo.sh` eerst de wineserver persistent en wineboot en zet daarna de link.
- Met de link op de pty opent een Wine-proces `COM4` en komen zijn bytes bij de daemon aan. De andere kant (lezen met `ReadFile`, `SetCommState`, `GetCommModemStatus` via de hulp) is niet met een echt programma getest: er is geen Windows-compiler op deze machine.
- De hang van de game zit eerder dan COM4: `GameSampR_RingWide.exe` opent na ongeveer 2 s een TCP-verbinding naar 127.0.0.1 poort 40100 (amNetwork/PCP van Sega, vermoedelijk de dienst "mxsegaboot": `mxGetHwInfo.exe` zegt dat hij foutmeldingen "naar mxsegaboot" stuurt), verbindt als daar iets luistert, stuurt niets en wacht daarna zonder reactie (gemeten met een eigen luisteraar die nul tot zestien nulbytes en "PCP\0" stuurde, tot 30 s lang). Luistert er niets, dan herhaalt hij verbinden en wachten. De game komt dus nooit bij COM4 of een venster. Ook met de TeknoParrot-kern in het proces (injectie na 300, 800, 1200 en 3000 ms; `TeknoParrot.dll` staat in de modulelijst) verbindt de game naar die poort: de kern vangt dit niet af. Wat de tip uit `antwoorden.md` zegt (COM4 ontbreekt in de omgeving) is dus niet de eerste oorzaak.
- Wat er nodig is: een server voor het PCP-protocol op 127.0.0.1:40100 (protocol onbekend; een opname van het verkeer op een echte Windows-installatie of een analyse van de `amNetwork`-laag van de game kan het geven), of een manier waarop TeknoParrot dit afhandelt die onder Wine niet werkt. Pas daarna is te meten of de JVS-keten (COM4, modemstatus, identificatie, polling) werkt.

Wat niet is gedaan: de snelkoppeling (`make-game-shortcut.sh`) voor LGS, omdat de game niet speelt. Time Crisis 5 blijft via tp-shm.

## 2026-10-10 — Een gewone muis als bron voor de virtuele lightgun (in de engine)

Aanleiding: de nieuwe Qt-app krijgt per speler de keuze None / Mouse / PS Move. Een muis is handig om games te proberen of te spelen zonder Move. Uitwerking: `docs/architecture.md` ("Muis als lightgun"), `src/ravengund/mouse_service.*`, `src/ravengun/output/mouse_gun.*`, `src/ravengun/sysinfo/mice.*`.

Gekozen:
- In de engine (`ravengund`), niet in de app: de app praat alleen via IPC en opent nooit invoerapparaten (zelfde regel als voor de Move en de camera). De muis loopt zo door dezelfde `OutputService` als een Move (arm/disarm, transformatie, viewport, uitvoerklok, release bij uitval) en werkt ook met de webinterface en `ravengunctl`. De kern (`MouseGun`) is een zuivere toestandsmachine en dus zonder hardware te testen.
- Geen `EVIOCGRAB`: de desktopcursor blijft werken, anders kun je de pc niet meer bedienen zolang de muis aan een speler hangt en blijft de gebruiker bij een fout zonder cursor zitten. Gevolg: de muis beweegt ook de desktopcursor; dat is voor lightgun-games op een eigen scherm of fullscreen acceptabel, en wie dat niet wil gebruikt een tweede muis. Een grab kan later een optie worden.
- Runtime-only toewijzing, niet in `config.json`: wat een muis is verandert bij elke USB-aansluiting (`eventN`, een id kan verdwijnen), en de keuze per speler is een instelling van de app-sessie. Een bewaarde toewijzing aan een muis die er niet meer is, geeft alleen dode configuratie en een foutmelding bij elke start. De app past de keuze opnieuw toe bij het verbinden. Een herstart van de daemon wist ze, zoals een testpatroon of `arm` ook niet bewaard blijft.
- Beveiliging: `set_mouse_gun` neemt alleen een `id` uit de eigen opsomming van de daemon (`mice`), nooit een pad. Het id is een naam uit een vaste lijst tekens (hoogstens 128), wordt vergeleken met de lijst en het pad `/dev/input/<eventN>` wordt door de daemon zelf opgebouwd. Zo kan een client (ook via de webinterface) de daemon niet laten lezen uit een willekeurig bestand of apparaat, en de daemon leest alleen, nooit schrijft hij naar een invoerapparaat. Onze eigen virtuele apparaten (vendor 1209) staan niet in de lijst (geen feedbackloop).
- Eén bron per speler en één speler per muis: een muis bij twee spelers of een muis én een Move bij dezelfde speler zou twee bronnen op één gun zetten. De daemon weigert het eerste; het tweede laat hij aan de app (de Move-kant raken we niet aan).
- Een eigen spelermasker (`set_mouse_players`) naast `set_active_players`, zodat het bestaande gedrag voor Moves onaangeroerd blijft; de gun van speler 2..4 bestaat als de speler een Move of een muis heeft.
- Y-schaal met een aparte beeldverhouding (standaard 16/9) in plaats van een vaste factor: een count is dan horizontaal en verticaal even ver op het scherm.
- App-kant (Controllers-venster): een speler met een Move kan niet op Mouse of None worden gezet (de app meldt: geef de Move eerst een andere speler, of verwijder hem); zo komen er nooit twee bronnen op één gun. De app bewaart de keuze per speler in `~/.config/ravengun/ravengun-app.conf` (`mice/playerN`, `mice/countsN`) en stuurt hem opnieuw na elke (her)verbinding met de engine. Gevolg: zonder lopende app (alleen een bureaubladsnelkoppeling) is er na een engine-herstart geen muis-gun; bewust, zie hierboven.

## 2026-10-10 — De Qt-app: Qt 6.11 uit een container, tegen de systeem-Qt

Aanleiding: spoor A (één app in de stijl van Dolphin, in het Engels). SteamOS 3.9 levert Qt 6.11.1 (`qt6-base`, plus o.a. Svg, Test, Network, DBus) als systeembibliotheek, maar zonder headers en zonder CMake-bestanden; een eigen Qt meeleveren zou honderden MB kosten en buiten de systeemthema's vallen.

Gekozen:
- Bouwen in een rootless podman-container (`packaging/containers/qt-app/Containerfile`, image `localhost/ravengun-qt-build`), niet in de bestaande `localhost/ravengun-build` (die blijft voor PSMoveAPI). De container wordt met `pacman -Syyuu` teruggezet naar het Arch Linux Archive van 2026-07-01: daar is `qt6-base` 6.11.1 (zelfde patchversie als de host) en glibc 2.43 (gelijk aan de host; Arch zit inmiddels op Qt 6.12 en glibc 2.44, waartegen gebouwde binaries op de host niet starten). GCC is 16.1.1, gelijk aan de host, dus ook libstdc++ past.
- De binary (`build-app/ravengun`) linkt dynamisch tegen `/usr/lib/libQt6*.so.6` van de host (gecontroleerd met `ldd` in `scripts/build-app.sh`) en gebruikt de platformplugins (wayland, xcb, offscreen) en het Breeze-thema van het systeem. Qt garandeert binaire compatibiliteit binnen 6.x naar boven; door op exact 6.11.1 te bouwen werkt het ook bij een terugval. Na een SteamOS-update naar Qt 6.12+ blijft de binary werken; bij een update van Qt naar een nieuwere minor kan de image naar een nieuwere momentopname.
- CMake-doel `ravengun_app` met uitvoernaam `ravengun` (de naam `ravengun` is al de bibliotheek in de hoofd-CMakeLists). Alleen gebouwd als `find_package(Qt6 ... Widgets Network)` lukt; op de host (zonder headers) blijft de gewone build met alle ctest-suites ongewijzigd.
- Architectuur: een kern zonder widgets (`ravengun_app_core`: Qt Core/Gui/Network) met engineverbinding, bibliotheek, opties, memory card en launcher, en daarbovenop de widgets. Een latere fullscreen-modus (A7, QML of Widgets) gebruikt dezelfde kern (`rg::AppCore`).

## 2026-10-10 — De Qt-app volgt Dolphin, met overgenomen code

Opdracht van de eigenaar: "Net als de emulator Dolphin ... kan voor 90% overgenomen worden", alles in het Engels. Dolphin is GPL-2.0-or-later, RavenGun GPL-3.0-or-later: overnemen mag, met bronvermelding.

Gekozen:
- Bestudeerd: DolphinQt op commit `e6f3ae17627e4344da95b13424af5baf4c892b08` (MainWindow, ToolBar, MenuBar, GameList met de proxymodellen, SearchBar, Config/SettingsWindow, Config/GamecubeControllersWidget en ControllersPane, Config/PropertiesDialog, Settings/PathPane, QtUtils, Resources en het thema Clean).
- Letterlijk overgenomen en aangepast: StackedSettingsWindow (de navigatielijst links met panelen rechts; Dolphin gebruikt die zelf voor Properties en Settings), WrapInScrollArea, NonDefaultQPushButton, SearchBar, ToolBar, de GameList met ListProxyModel en GridProxyModel, delen van MenuBar en Resources::LoadNamedIcon, en de iconen van het thema Clean. Elk bestand noemt het Dolphin-bestand en de commit; overzicht in `THIRD_PARTY.md`. Die bestanden houden SPDX GPL-2.0-or-later.
- Niet overgenomen: Dolphins Settings/Config-laag, de emulatorkern en de Mapping-dialogen (die gaan over GameCube-pads). Onze modellen zijn eigen code: bibliotheek (`library.json`), `patches/<Game>/options.json`, engine-IPC.
- Properties en Settings gebruiken, net als in de huidige Dolphin, een lijst links in plaats van tabbladen; de instellingen per Move gebruiken tabbladen (zoals Dolphins MappingWindow).

## 2026-10-10 — RavenGun vervangt TeknoParrotUi voor de gebruiker

Productbesluit van de eigenaar: "onze app vervangt TeknoParrot uiteraard." Uitwerking door de orchestrator; zo verwerkt in de app:

- De gebruiker opent TeknoParrotUi nooit meer: bibliotheek, opties per game, invoer en starten/stoppen gebeuren in RavenGun. De TeknoParrot-runtime (de laders BudgieLoader/OpenParrotLoader en de cores, onderdeel van het abonnement van de eigenaar) blijft een afhankelijkheid, zoals een frontend een emulatorcore nodig heeft. De app laat de map kiezen en controleert hem (Settings > Paths > "TeknoParrot runtime": laders, GameProfiles/ en TeknoParrotUi, met een duidelijke melding als iets ontbreekt).
- Starten gaat via een launcher-abstractie in de app-kern (`src/app/core/Launcher.*`) met backends: `teknoparrot-ui` (nu: de engine start `scripts/play-halo.sh`, dat TeknoParrotUi `--profile` headless als lader gebruikt, zoals tot nu toe) en `direct` (later: de engine start de lader zelf met de juiste argumenten en omgeving en levert zelf JVS-pipe en gedeeld geheugen; nu meldt hij "not available yet"). De keuze staat in `library.json` (`launcher`) en in Settings > Paths. De rest van de UI praat alleen met `GameLauncher` en neemt nergens aan dat TeknoParrotUi draait; de voortgang komt uit de engine.
- Opties per game, paden en de keuze van het spel komen uit onze eigen bibliotheek en config: `library.json` (per game `overrides.<id>.options` en `profile_fields`) plus `patches/<Game>/options.json` (sinds 2026-10-16 per game, vroeger `packaging/game-options.json`; docs/game-opties.md). De TeknoParrot-gebruikersprofielen (`UserProfiles/<P>.xml` en `<P>.env`) zijn alleen een uitvoerformaat voor de backend `teknoparrot-ui`: `ProfileSync` schrijft de bewaarde waarden erin bij Apply en vóór elke start. Bestaande profielen zijn de beginwaarden zolang de gebruiker niets gewijzigd heeft. Een nieuw profiel maken gaat (op verzoek) met het bestaande `scripts/tp-add-game.py` ("Set Up Game...").
- Geen "TeknoParrot" in de hoofd-UI: de lijst toont neutrale platformnamen ("Windows PC", "Linux PC", of de naam uit Metadata), het type "Arcade", de route "I/O bridge (shared memory)"; menu's en dialogen spreken van "Set Up Game" en "game profile". Alleen Settings > Paths en het paneel Info noemen TeknoParrot. Uitzondering die blijft: de spelersnotities (`games_en` in `packaging/games-notes.json`, of `status_en` uit game-options.json) kunnen het woord bevatten.

## 2026-10-10 — De app schrijft nooit naar de NAS

- Elke schrijfactie van de app gaat via `rg::checkWritable` (`src/app/core/FileSafety.*`): een pad onder `~/mnt` (of na het volgen van symlinks daar) of op CIFS/SMB/NFS is "read-only (on NAS)". Dat geldt voor opties (INI- en configbestanden in de gamemap), de memory card (wissen) en de bibliotheek zelf.
- Een optie in een bestand dat in een lokale view-map (`~/.local/share/ravengun/runtime/views/<P>`, gemaakt door `tp-add-game.py --view`) een symlink naar de NAS is, krijgt eerst een lokale kopie die de link vervangt (overlay); de NAS blijft ongewijzigd. Ligt de game zonder view direct op de NAS, dan is de optie alleen-lezen met de melding in de UI.
- Memory card: alleen tonen voor NAS-bestanden, ook als options.json ze als lokaal markeert (strenger dan "tenzij expliciet lokaal": een bestand dat werkelijk op de NAS staat, wordt nooit gewist). Items met `safe_to_erase: no` (`protected_data`) zijn ook lokaal niet te wissen; bij `unknown` vraagt de app om eerst een back-up te maken.

## 2026-10-10 — B4: Raw Thrills native op Linux (scripts/play-native.sh)

Opdracht: de Raw Thrills-games zonder Wine en zonder BudgieLoader draaien, met de herschreven `game` en de vooraf te laden bibliotheek van TeknoParrot (antwoorden_ronde2.md vraag 9). Uitkomst na onderzoek en metingen op deze machine: geen enkele game werkt volledig native. Halo komt het verst. Wat er is gebouwd en wat er is gemeten, per punt.

Gebouwd:
- `scripts/play-native.sh --game PROFIEL` met `packaging/native-games.json` (per game: NAS-mounts, bestanden die als origineel worden getoond, losse bibliotheken, preload, argumenten, resolutie, route, status en reden). Bubblewrap zonder root: `/pm/...` als overlay op de NAS-map (lezend; alles wat de game schrijft komt in `~/.local/share/ravengun/native/<PROFIEL>/`), geen netwerk, alleen het X-socket van de sessie, pulse/pipewire (niet bij `--headless` of `--mute`). Er wordt niets van een game gekopieerd; gecontroleerd dat er niets op de NAS is geschreven (geen bestand van minder dan 90 minuten oud in de gamemappen).
- Testmodi: `--headless` (gamescope zonder scherm, de tv blijft vrij; schermafbeelding met `--snap`), `--strace`, `--gdb`, `--dry-run`. i386-bibliotheken: de lokale `lib32` (`--stage-libs`: libglut, libXi, libGLU, libtheora, libXrandr, libXrender uit de Steam Linux Runtime, libSDL-1.2 als sdl12-compat uit de multilib-opslagplaats van SteamOS, uitgepakt in een eigen map) plus losse bestanden uit `ElfLdr2/libs` (PhysX, fmod 7, allegro, libNL). Nooit de hele `ElfLdr2/libs` op het zoekpad: daar staat een eigen `libc.so.6` in.
- `scripts/play-halo.sh --profile P` neemt de native route als `enabled` in `native-games.json` staat (net als de Unity-route), of met `--native`; `--teknoparrot` dwingt TeknoParrot af. De game start in de user-unit `ravengun-tpui` (stoppen via `--stop`, OLED-beschermer herkent hem). Nu staat voor geen enkel profiel `enabled` aan.

Gemeten voor Halo (alles in `--headless`, met de lokale kopie uit fase 1 voor de snelheid en daarna op de NAS):
- `halo.so` is uitsluitend hasp-hulp: `initialize` zet inline-hooks op de hasp_*-aanroepen van de game (vaste adressen in `game`) en leest `/pm/g7/dump`. Er is geen invoercode in (geen shm, evdev, joystick of USB; alleen fopen/fread van `/proc/self/maps` en de dump). De aanname dat het onderdeel `/dev/shm/TeknoParrot_JvsState` leest, klopt dus niet: wat TeknoParrot als invoer levert, zit in BudgieLoader (Windows) en niet in de preload.
- Hoe de native game zijn invoer leest: (1) guns als SDL-joystick (SDL zit statisch in de game): evdev + udev, apparaten met `ID_INPUT_MOUSE` worden overgeslagen, het serienummer en pad komen via `udevadm info` (popen), maximaal vier guns, per gun zes assen en 15 knoppen (`SwitchTestGun<n>Trigger0..9` in het diagnostische menu); de Move zelf (054c:03d5, joystick) wordt zo ook als gun opgepikt; (2) de kastschakelaars (start, test/diag, service, volume, munt 0..3, per bord) van het RIO2-bord (USB; de RIO2-bibliotheek zit in de game, in NERF staat dezelfde bibliotheek los als `libRIO2.so` met een SHA-256-vraag/antwoord met een beveiligingschip); (3) webcams (`Webcam Init`, "Could not ID device for player 0"): de guns worden met webcams geïdentificeerd. Het toetsenbord doet niets: met xdotool elke toets geprobeerd (1..0, F1..F12, letters, pijlen), en alle 15 joystickknoppen van een virtuele gun: geen enkele haalt de game van "Press START or TEST to continue" of "Press START or DIAG to skip File Test".
- De FileTest (sha1sum per bestand, 5941 bestanden) duurt lokaal ongeveer twee minuten en op de NAS naar schatting 25 minuten (gemeten 4 bestanden per seconde); START slaat hem over. Het eindscherm "FileSystem integrity test failed" komt door `game` zelf (de herschreven variant wijkt af) en blijft; dat is de eigen melding van de game, niet iets wat wij overbruggen.
- RIO2 nabouwen doen we niet: de bibliotheek doet een vraag/antwoord met een beveiligingschip (sleutelmateriaal), dat is keychip-afhandeling. Wel gebouwd en bewezen: `tools/rt-input-shim/rg_rt_input.c` (LD_PRELOAD, alleen in het proces `.../game`, alleen voor Game Rev 57529 na een bytecontrole). Zij leest het gedeelde geheugen van de IO-brug (WartranTroopers) en zet start/test/service/munt/volume als korte puls (30 ms) in de schakelaartabel van de game (`0x180ae80 + 0xaea0`, 0x50 per schakelaar, 0x2200 per bord; getter op `0x61e890`). Geen RIO2-bord, geen beveiligingschip, geen controle omzeild: de game ziet nog steeds geen RIO2-bord, alleen de schakelaars komen binnen. Gemeten: een START-bit in het gedeelde geheugen laat de game van het FileTest-scherm naar "Webcam Init" gaan.
- Verder: de FMOD-stub had een fout. Hij leverde alleen de namen `_ZN4FMOD...` en `FMOD_...` uit de dynstr van de game en miste de const-methoden (`_ZNK4FMOD...`, o.a. `Studio::System::getEvent`); halo.so lost die GOT-ingangen niet op, ze bleven 0 en de game crashte op een aanroep naar adres 0 (gdb: rip 0, aanroeper `0x5ba4ea`, GOT-ingang `0x1691880` = 0). Gerepareerd (`tools/fmod-stub/symbols.txt` van 46 naar 60 namen). Daarna loopt Halo native door "Webcam Init" (geen webcam in de sandbox: "Could not ID device for player 0", "Aborting auto-id") naar het gun-kalibratiescherm van de game: "Player 1 gun (left): Pull gun all the way back and towards the left, then press the trigger ... Press the TEST button to skip calibration for this gun" (zelfde plek als in fase 1 via TeknoParrot; schermafbeelding gemaakt in de headless gamescope).
- Verder gekomen met de hulp (alles in `--headless`, schermafbeeldingen met de gst-pipewire-opname): de testknop slaat de kalibratie van gun 1 over als de indruk lang genoeg is (300 ms; 30 ms was te kort), de kalibratie van gun 2 gaat weg met START (testknop op bord 0 t/m 3 hielp daar niet), en daarna komt de game in zijn attract-lus: Microsoft-logo, high scores, "JOIN NOW / Press START to Begin / Cost to play: $1.00". Munt werkt: elke indruk van 14 ms op de munt-schakelaar van bord 0 geeft $0,25 ("Money In: $2.00" na acht indrukken; indrukken die langer duren tellen per beeld meerdere keren). Een keer is de game ook echt gestart: "CHOOSE A GAME MODE", daarna "MISSION 1/ESCAPE" en de introfilm met ondertitels ("Money In: $45.50": toen waren de indrukken nog te lang). Dat is niet reproduceerbaar teruggevonden: met de definitieve hulp blijft START in de attract-lus zonder effect (geprobeerd met 40 en 300 ms, met en zonder loslaat-puls, met alle schakelaars van bord 0 als "aanwezig" gemarkeerd, en met alle 15 knoppen van een virtuele SDL-gun). De waarschuwingen "QR scanners need to be configured!" en "Factory Setup Has Not Been Completed!" staan permanent in beeld (de game mist het QR-lezerbord en de fabrieksinstelling). Aim en trekker zijn niet getest: ze horen bij een spel dat nog niet start. Geluid is er niet: FMOD 1.10 (`libfmod.so.10`, `libfmodstudio.so.10`) is propriëtair en staat nergens op deze machine of de NAS; de stub geeft alleen foutcodes. Native Halo met geluid, aim en trekker is daarmee binnen dit budget niet haalbaar; de route blijft TeknoParrot/BudgieLoader.
  - **CORRECTIE (2026-10-13) op deze bewering:** op deze machine klopt dit niet, van twee kanten gemeten op de native route. (1) Zes TEST-pulsen op alle vier de borden deden niets: het logboek van de game bleef exact op 256 regels staan en alle zeven foto's bleven het kalibratiescherm van 1,47 MB (run 11, `patches/Halo/RUN-11-UITKOMST.md`). (2) Een run met alleen START en geen enkele TEST-puls kwam wel door: `perm` om 11:56:21, `attract` om 11:56:32, `level1` om 11:56:55, `level2` om 11:58:04 (run 13, `patches/Halo/RUN-13-UITKOMST.md`). Dus: TEST doet hier niets, START brengt het spel door de kalibratie en in de attract-lus. De oude regel hierboven (TEST voor gun 1, START voor gun 2) blijft staan als wat er toen is opgeschreven; wie hem volgt stuurt TEST-pulsen die niets doen. Let op het verschil met de TeknoParrot-route in `STATUS.md` ("F2 (Test) slaat de kalibratie over", "F2 tweemaal"): dat is een andere keten (de invoer van BudgieLoader onder Wine) en is met deze metingen niet opnieuw gemeten en niet weerlegd.

Gemeten voor de andere games (headless, native):
- Jurassic Park, Galaga Assault en Pacman Chomp-Mania (herschreven ELF): SIGSEGV voor `main`, een sprong naar een vast adres uit de dump (0xb7227dc0, 0xb758c140, 0xb728fa00). In de gamemappen staat geen vooraf te laden bibliotheek voor deze titels (alleen `libhasp_linux_*.so` van de echte dongle bij Jurassic Park); die laag zit in BudgieLoader. Jurassic Park vraagt bovendien `libSDL-1.2.so.0` (i386), die we uit sdl12-compat hebben gehaald, en gebruikt `run_game.sh`: `game -n -f1360x768` vanuit `/pm`.
- Terminator Salvation: laadt, stopt dan direct met exit 255; de game wil een HASP-dongle (`DongleInitialize() FAILED`). Niet omzeild: geparkeerd. Werkt wel via TeknoParrot+gamescope.
- GhostBusters: het binair verwacht `/usr/lib/libcsv.so` (absoluut pad; in een read-only `/usr` niet te leveren) en `libesd.so.0` (i386, niet beschikbaar), en controleert een dongle. Geparkeerd.
- Go Go Strike (`bowling_usa`, Allegro 4, PAOKAI): laadt zijn beelddata en crasht dan op NULL; het wil ook de kastapparaten `/dev/w040101_*`. Geparkeerd.

Gevolgen: B4 blijft open als onderzoek; native is alleen haalbaar als TeknoParrot een Linux-laag voor ElfLdr2 uitbrengt (vraag aan het team: levert hun Linux-preload ook RIO2-invoer, webcams en FMOD, en bestaat er een vergelijkbare preload voor Jurassic Park, Galaga en Pacman?). De gebruikte TeknoParrot-onderdelen zijn alleen `halo.so` (preload) en de gewone bibliotheken uit `ElfLdr2/libs`; er is niets van de dongle-, keychip- of integriteitslaag nagebouwd of omzeild.

## 2026-10-10 — Invoerroute per game kiezen: een gebruikersbestand over packaging/io-routes.json

Aanleiding: A3, de route moest in Properties te wijzigen zijn in plaats van alleen in de repo.

Gekozen:
- De keuze staat in `~/.config/ravengun/io-routes.json` (`$XDG_CONFIG_HOME`), niet in `packaging/io-routes.json` en niet in `library.json`. De repo blijft de aanbevolen route per game (wat wij getest hebben); de gebruiker wijkt daar per game van af. Engine, startscript en app lezen hetzelfde bestand, dus `library.json` (alleen van de app) was niet genoeg.
- Zelfde vorm als het basisbestand met alleen de afwijkingen; per game vervangen de genoemde velden die van de basis (ondiep), zodat een keuze "evdev" de indeling en munttoetsen van de basis houdt voor als de gebruiker teruggaat. Dezelfde controles als de basis (tp-shm vraagt een indeling, enzovoort). Alles of niets: één fout maakt het hele gebruikersbestand ongeldig (melding in de status en het log), want een half toegepaste keuze is moeilijker te begrijpen dan geen.
- De engine leest het bestand bij elke `iobridge start`, `route` en `status` opnieuw (klein bestand, geen herstart nodig). `play-halo.sh` gebruikt `scripts/io-route.py` met dezelfde regels (getest in `tests/io_route_test.sh`). Een `--route` op de opdrachtregel gaat nog steeds voor alles.
- De app biedt alleen routes aan waarvoor een indeling of bord bestaat; de aanbevolen route kiezen haalt de keuze weg. Unity-games hebben één route (`play-halo.sh` stuurt ze altijd naar de plugin).

## 2026-10-10 — Spelersnotities tweetalig

`packaging/games-notes.json` houdt `games` (Nederlands, max. 300 tekens, voor de webpagina en de engine; formaat ongewijzigd, dus `parse_notes`, `web_smoke.sh` en de webapp blijven werken) en krijgt `games_en` (Engels, voor de app) en `status` (playable, parked; ontbreekt = untested). De app toont `games_en`, anders de Nederlandse tekst (sinds 2026-10-16 geen `status_en` uit een opties-bestand meer; de notities staan in game.ini [Status]). `status` bepaalt welke games de fullscreen-modus standaard toont; het staat bij de notities omdat het dezelfde kennis is ("start dit spel en is het speelbaar?").

## 2026-10-10 — Fullscreen-modus: de lightgun via de engine, niet via het invoerapparaat

Aanleiding: A7, kiezen en starten met de lightgun in Steam Game Mode.

Gekozen:
- De virtuele gun is een absolute muis, maar met `LIBINPUT_IGNORE_DEVICE` bewust onzichtbaar voor de desktop en gamescope (anders beweegt de cursor mee tijdens een game). Het venster leest daarom de aim en de knoppen van speler 1 rechtstreeks uit de engine en tekent een eigen vizier. Daarvoor heeft `watch` een optie `lite` gekregen (compacte frames van ongeveer 200 bytes in plaats van het volledige sample van enkele kilobytes, 60 Hz). Een engine zonder die optie weigert hem (`bad_request`); de app valt dan terug op de volledige frames, zodat de app ook vóór een herstart van de engine werkt. De aim is die van het sample (geklemd, 0..1), niet de uitvoer van de gun: die bestaat alleen als de uitvoer gewapend is.
- Bevestiging: een schot kiest een tegel, een tweede schot op dezelfde tegel binnen 3 s of 1 s de trekker vasthouden start hem. Eén schot zou te makkelijk per ongeluk een game starten (de Move ligt vaak in de hand terwijl je praat); twee schoten passen bij een lightgun en vasthouden is voor wie liever niet twee keer schiet. Een mis wist de keuze; het vizier van de tegel halen breekt het vasthouden af. Muisklik en Enter starten meteen (bewuste handelingen).
- Tijdens de game verbergt het venster zich (zo komt de game onder gamescope in beeld) en stopt de aim-stroom en de gamepadlezer; het komt terug bij `idle` of `failed`. Een trekker die nog ingedrukt is na de game telt pas na loslaten.
- Gamepads: Qt 6 heeft geen gamepadmodule meer en SDL zit niet in de bouwcontainer. De app leest gamepads daarom zelf via evdev, alleen-lezen en zonder `EVIOCGRAB` (een uitzondering op "de app opent geen hardware", die voor de Move en de camera blijft gelden). De eigen virtuele apparaten en de Move worden overgeslagen; de Navigation Controller en de virtuele pad van Steam werken. Met Steam Input als toetsenbord komen pijltjes en Enter gewoon als toetsen binnen.
- OLED: puur zwart, gedempte tekst en lijnen, geen gevulde lichte vlakken; dimmen en langzaam bewegen na 45 s, alleen een klein bewegend tekstje na 5 minuten. Onder gamescope kiest de app Xwayland (`xcb`), tenzij `QT_QPA_PLATFORM` gezet is: de Wayland-socket van gamescope heeft niet alle protocollen die Qt gebruikt. Nog niet onder gamescope zelf geprobeerd.

## 2026-10-10 — Eerste-gebruik-wizard: rechten met één pkexec en een vast script

Aanleiding: A6. De rechten (udev-regels, Bluetooth-hulp met sudoers-regel) vragen root; de gebruiker moet dat met één prompt kunnen doen.

Gekozen:
- Eén nieuw script, `scripts/install-permissions.sh --apply`, dat de twee bestaande installatiescripts uitvoert. De app start het met `pkexec <absoluut pad> --apply`: een vast programma met vaste argumenten via `QProcess` (geen shell, geen tekst van de gebruiker). De gebruiker voor de sudoers-regel komt uit `PKEXEC_UID` (door pkexec gezet), niet uit een argument. Controle zonder root met `--check` (de `--check` van beide scripts); is alles in orde, dan is de stap over te slaan.
- Kanttekening: het script ligt in de repo van de gebruiker, dus een programma met de rechten van de gebruiker zou het kunnen aanpassen vóór de gebruiker zijn wachtwoord geeft. Dat geeft geen extra rechten (de gebruiker geeft zelf beheerdersrechten voor wat er dan staat), maar voor de verpakking (A9) horen de installatiescripts op een plek die alleen root kan wijzigen, met een eigen polkit-actie.
- De wizard verschijnt alleen in Desktop Mode, bij de eerste start of via Tools > Setup Wizard; afbreken telt ook als "gedaan", zodat hij niet blijft terugkomen. Hij voert niets uit zonder klik.

## 2026-10-10 — Steam-integratie: eigen binaire VDF en een vaste shortcut-id

Aanleiding: A8, RavenGun als niet-Steam-game in de bibliotheek, zodat het in Game Mode te starten is.

Gekozen:
- Eigen lezer en schrijver voor binaire VDF (`core/SteamShortcuts.*`, geen code van derden). Alle waarden worden als ruwe bytes bewaard, zodat andere items na het schrijven byte voor byte gelijk zijn (getest op een synthetisch voorbeeld en op een kopie van een echte `shortcuts.vdf` met 29 items). Een bestand met een onbekend type wordt niet aangeraakt.
- Het eigen item wordt herkend aan de naam "RavenGun" of hetzelfde programma en bijgewerkt (geen dubbel item); keuzes die de gebruiker in Steam maakte (bijvoorbeeld de controllerinstelling) blijven staan. Altijd eerst een back-up met tijdstempel; schrijven via `QSaveFile` na een controle dat het resultaat weer te lezen is.
- appid: `crc32("\"<exe>\"" + "RavenGun") | 0x80000000`, de formule van de bekende hulpprogramma's, en in het item geschreven. Nieuwere Steam-versies kiezen zelf een willekeurig appid voor nieuwe items, maar gebruiken een opgeslagen appid; de grid-afbeeldingen heten naar dezelfde waarde, dus die horen er zeker bij.
- Steam overschrijft het bestand bij het afsluiten. Daarom alleen schrijven als er geen proces `steam` is; de app vraagt de gebruiker Steam te sluiten en doet dat alleen na bevestiging, netjes met `steam -shutdown`. `--add-to-steam` (zonder vragen) weigert als Steam draait. `$RAVENGUN_STEAM_ROOT` laat alles op een kopie draaien (dan zonder die controle).
- De afbeeldingen (staand, liggend, hero, logo, icoon) worden getekend: zwart met een rood vizier, passend bij de fullscreen-modus.

## 2026-10-10 — A10: games direct starten zonder TeknoParrotUi

Aanleiding: A10 (de UI roept alleen een lader aan; antwoorden_ronde2.md vraag 8, door het TeknoParrot-team akkoord bevonden). Uitwerking: `docs/architecture.md`, "Directe start zonder TeknoParrotUi"; metingen: `packaging/games-status.md`, achtste ronde.

Gekozen:
- Een Python-script (`scripts/launch-direct.py`, alleen de standaardbibliotheek) als hoofdproces van de unit `ravengun-tpui`, op de plaats van TeknoParrotUi, en niet de engine zelf. Redenen: (1) de engine moet een herstart kunnen hebben zonder dat de game stopt (`safe-restart-daemon.sh`, de tp-shm-route hervat); Wine-processen als kinderen van de engine zouden met elke herstart meegaan; (2) alles van de game blijft in dezelfde unit, dus de OLED-bescherming, `safe-restart-daemon.sh`, de bewaking van de IO-brug, `play-halo.sh --stop` en de status in web en app werken ongewijzigd; (3) het is dezelfde soort lijm als de bestaande startscripts (bash plus Python, `io-route.py`, `tp-add-game.py`) en het startplan is zonder Wine te testen (`--plan`, `tests/launch_direct_test.sh`); (4) de engine blijft vrij van procesbeheer voor Wine.
- De bron van waarheid voor wat TeknoParrotUi doet is de open bron (TeknoParrotUI, branch net8-migration: GameSession, GameLaunchArguments, TeknoParrotIniWriter, ProtonLauncher, ProtonBridgePipe, ProtonHelper; alleen gelezen, niets overgenomen) plus een meting aan een lopende sessie van het oude pad (`/proc/<pid>/cmdline`, `cwd`, `environ` van lader, gamescope en pipehelper). De startvoorschriften in `AndroidLaunchRecipes/` zijn voor Winlator op Android (`elfloader.exe`, `E:\TeknoParrotRuntime`): alleen ter controle gebruikt (lader en argumenten komen overeen).
- `teknoparrot.ini` wordt met dezelfde opbouw gemaakt maar nooit naar de NAS geschreven (besluit "de app schrijft nooit naar de NAS"). Voor alle huidige games is het bestand dat TeknoParrotUi eerder schreef byte voor byte gelijk aan het onze; wijkt het ooit af, dan blijft het staan en meldt het logboek dat. Lokaal (views) wordt het gewoon bijgewerkt.
- De activering (abonnement) wordt niet nagebouwd en ook niet zelf in de prefix gezet: alleen gecontroleerd. Ontbreekt ze, is ze verouderd of ingetrokken, dan valt `play-halo.sh` terug op TeknoParrotUi, dat dat zelf regelt. Dongle, keychip en integriteit blijven in de lader en de kern; daar wordt niets aan veranderd.
- `pipehelper.exe` uit de runtime blijft de Windows-hulp (geen mingw voor een eigen hulp), nu direct met het bestand van de daemon als bron: `tp-shm-swap.sh` is bij de directe start niet nodig. De TCP-kant van de JVS-pipe is klein gehouden: verbind-vlag bij reset/adres (zoals TeknoParrotUi), antwoord op het adres, de rest alleen gelogd. Geen van de werkende games stuurt JVS over de pipe; een volledig JVS-bord achter de pipe (de engine heeft er een, dialect jvs) is pas nodig voor Type X2 en Let's Go Safari en staat open.
- Invoer altijd van de engine: zonder TeknoParrotUi leest niemand evdev. Halo, Terminator en Go Go Strike gaan daarom van route evdev naar tp-shm (via TeknoParrotUi blijft evdev te kiezen: `--tpui --route evdev`). Go Go Strike krijgt een eigen indeling (`GoGoStrike`): het profiel legt de munt op Coin2 en Setup op Coin1, dus de RawThrillsGUN-indeling zou bij elke munt het instelmenu openen.
- Per game een instelling in `packaging/io-routes.json`: `"launch": "direct"` (standaard nu voor Halo, Terminator, Time Crisis 5, Transformers, Go Go Strike en Raccoon Rampage, alle live vergeleken met het oude pad), te overschrijven in het gebruikersbestand en per start met `--direct`/`--tpui`. Zonder `launch` blijft TeknoParrotUi de standaard.
- Engine en app: `game_start` krijgt een optioneel veld `launch`; de app heeft nu de backends `auto` (standaard: het startscript kiest per profiel, met terugval), `direct` en `teknoparrot-ui`. De oude placeholder `direct` is daarmee echt.

Afgewezen:
- De lader vanuit de engine (C++) starten: zie hierboven (herstart, unit, procesbeheer).
- De activering zelf importeren (`wine reg import` van het seedbestand, zoals TeknoParrotUi): technisch eenvoudig, maar het is de licentielaag van de runtime; terugvallen op TeknoParrotUi is even goed en raakt er niet aan.
- Een eigen JVS-antwoord voor alle commando's in het script: dubbel werk naast het dialect jvs in de engine; als het nodig wordt, hoort de pipe aan dat dialect (relais TCP naar de pty).

Bevinding A11: met volledige uitvoer (`--log-full`: `TP_LOGTOFILE=1`, `+debugstr`) schrijft geen enkele kern (BudgieLoader 1.0.0.154, TeknoParrot(64).dll 1.0.0.3761, OpenParrot.dll 1.0.0.787) regels over toegepaste of overgeslagen aanpassingen. Wel zichtbaar: herkenning ("Detected Linux game: Go Go Strike", "RUNNING GoGoStrike"), de versies van lader, kern en exe (OpenParrot-lader) en fouten. Of en hoe de kern die regels laat zien, is een vraag voor het team (`docs/vragen-eigenaar.md`).

## 2026-10-11 — Per-game instellingen en patches in `patches/<Game>/` (model Dolphin)

Op verzoek van de eigenaar: alles wat per game anders is (startwijze, lader, Wine, invoer, status, patches) staat in
één map per game met INI-bestanden, meegeleverd plus een gebruikerslaag die per sleutel wint, zoals Dolphin's
GameSettings. Patches zijn benoemd, aan/uit te zetten, controleren de verwachte bytes en werken alleen op lokale kopieën.
Geen game-kennis meer in de launcher-code of in losse JSON-lijsten. Ontwerp en overgang: `docs/per-game-patches.md`.

## 2026-10-11 — Antwoorden van het TeknoParrot-team: alleen draadformaten gebruiken

Een deel van de antwoorden in rondes 9-19 en 21 is gebaseerd op uitgepakte beelden van de beschermde kernen en van
BudgieLoader. RavenGun gebruikt daaruit alleen draadformaten en gedrag dat nodig is voor uitwisselbaarheid (TPB1 voor
`pipehelper`, ACIO voor Otomedius, JVS), geen code of disassembly. We vragen niet om nieuwe ontledingen van beschermde
code; nieuwe vragen gaan over gedrag, profielen, de open bron (OpenParrot, TeknoParrotUi) en metingen.

## 2026-10-11 — Reactie op `findings/OPDRACHT-voor-de-loader-bouwer.md`

Overgenomen in het patchformaat (na de merge van `patches/`):
- `Version` per patch: de launcher noemt bij een weigering de verwachte gameversie.
- `Category` per patch, met de waarden `graphics`, `input`, `timing`, `audio`, `paths`, `crash`, `network`, `loader`.
  Een categorie voor beveiliging bestaat niet; de launcher weigert patches met een onbekende categorie.
- Logregel per patch in een vast formaat (toegepast/overgeslagen, naam, plek, reden).
- In `game.ini`: `EmulationProfile` (bepaalt bord en kalibratievenster), `DisplayMode`/`CustomResolution` en
  `DongleRegion`/`PcbRegion` als gewone instellingen uit het TeknoParrot-profiel. De regio kiezen uit de keuzes die
  het profiel zelf biedt is een instelling, geen omzeiling (correctie eigenaar, 2026-10-11).

Niet overgenomen:
- Een `dongles/`-map of een andere laag die dongle- of HASP-aanroepen beantwoordt, en het koppelen aan
  `findings/dongles/`. Dat is beschermingsafhandeling en blijft buiten RavenGun, ook als gebruikerslaag.
- Patches die gebruikers zelf voor beschermingscontroles kunnen toevoegen: het formaat biedt daar geen plek voor.
- Patchadressen uit materiaal dat uit beschermde kernen komt.

Aanvulling na de tweede versie van het document: de acht Type X2-adressen komen uit de openbare OpenParrot-bron
(`OpenParrot/src/Functions/Games/TypeX2/TypeX2Generic.cpp`, GPL-3.0); upstream op GitHub nagekeken. Haunted Museum II
heeft daar twee versies (2100 en 2101J) met elk één adres. Ze komen als gegevens in `patches/` met bronvermelding,
uitgeschakeld en gemarkeerd als "past de kern al toe", zodat ze niet dubbel worden toegepast. Een aparte dongle-laag
naast de loader verandert het besluit hierboven niet: RavenGun levert er geen en roept er geen aan. Een eigen
starter bevat geen abonnementscontrole; dat vraagt niets van het formaat.

## 2026-10-11 — Per-game instellingen en patches: uitwerking

Uitwerking van het ontwerp hierboven (`docs/per-game-patches.md`). Gekozen en wat afwijkt van het ontwerp:

- Eén lezer per taal, geen gedeelde bibliotheek over de taalgrens: `scripts/gamesettings.py` (Python, `configparser` met vaste instellingen: alleen `=`, commentaar `#`/`;`, inline `;` na witruimte, hoofdlettergevoelig, geen voortzetting over regels) en `src/ravengun/gamesettings/` (C++, alleen de standaardbibliotheek, want de engine heeft geen Qt; de app compileert dezelfde bron mee). Beide worden getest tegen `tests/data/gamesettings-cases.ini`, zodat de regels niet uit elkaar lopen. Lege waarde = sleutel weg, in elke laag (zo kan een game of de gebruiker een standaard van het soort ongedaan maken).
- Gedeelde patches: definitie één keer in `patches/_defaults/<Type>.patches.ini` (`Enabled = false`), aangezet per game in zijn eigen `patches.ini`. Gekozen boven een eigen kopie per game (vier keer dezelfde bytes) en boven "standaard aan voor het hele soort" (de opdracht was: alleen de vier Type X2-games die het nodig hebben; een vijfde game moet er bewust voor kiezen). `tp-add-game.py` zet voor een nieuwe game de patches aan die `[NewGame] EnablePatches` in `_defaults/<Type>.ini` noemt.
- `TargetName` bij een patch (alleen toepassen als het doel zo heet) is erbij gekomen: de oude code paste de lader-patch alleen toe op `OpenParrotLoader.exe` en liet de 64-bit lader met rust; een patch die niet past weigert nu juist, dus dit filter houdt het gedrag gelijk.
- Een geweigerde patch (Find komt niet precies Count keer voor, andere exe-versie, doel op de NAS) geeft exitcode 5 en `play-halo.sh` valt dan niet terug op TeknoParrotUi, anders zou de game zonder de patch starten. `--plan` en `--check` controleren alles maar maken de kopie niet (het oude plan maakte de lader-kopie wel); alleen `--run` schrijft onder `~/.local/share/ravengun/patched/<sha-orig 16>-<sha-set 16>/`. Soorten `registry` en `memory` worden gelezen en getoond maar nog niet toegepast (de launcher meldt het).
- Sleutels in `[Input]` heten `Board` en `Boards` (niet `JvsBoard`): het bord kan een hid-bord of een jvs-bord zijn. De variabelen die bij de JVS-pipe horen (`RG_JVS_*`) staan als `Jvs*` in `[Input]`; `JvsPipeBoard` (de ingebouwde JVS-antwoorden van `launch-direct.py`) is iets anders dan `Board` (een bord uit `jvs-boards.json`).
- `[Wine] Prefix` is `shared`, een naam onder `~/.local/share/TeknoParrotUI/prefixes/` of een pad, en wordt `RG_WINEPREFIX` (de wrapper `wine-prefix`); `PrefixDirect` is `TP_WINEPREFIX` (gewone runner). Zo is de omzetting naar de oude variabelen exact omkeerbaar en veranderen er geen plannen. De werkmap is relatief aan de map van de game-exe (`.`), paden onder de thuismap staan als `~/...`: geen NAS-pad of `/home/deck` in de repo.
- `UserProfiles/<P>.env` blijft één versie lang gelezen, als laagste laag onder `game.ini` (melding "legacy .env"; `RAVENGUN_NO_LEGACY_ENV=1` slaat het over). De opties van de app (Properties > Options, Display) schrijven nog naar dat `.env`; alleen de gamescope-keuze wordt ook in de gebruikerslaag gezet. Een optie die een meegeleverde waarde moet opheffen werkt pas als ook die naar de gebruikerslaag schrijft (open punt). De oude routekeuze in `~/.config/ravengun/io-routes.json` wordt niet meer gelezen.
- Waarden die gelijk zijn aan de standaard van het soort staan niet in `game.ini`; ontbreekt een variabele die de standaard wel zet (bv. `TP_REMOTETHREAD=off` voor TeknoParrot terwijl de oude `.env` er geen had), dan schrijft de migratie `RemoteThread =` (leeg: niet gezet) zodat de oude uitkomst blijft. De migratie leest de oude JSON-bronnen uit bestanden of uit een revisie (`--rev`) en bewaart handwerk (andere sleutels, secties met commentaar).
- Vergeleken met de oude code: de start-plannen van alle profielen in de runtime zijn gelijk (`tools/compare-launch-plans.py`); het enige verschil is het pad van de gepatchte lader (`patched/<sha>-<sha>/OpenParrotLoader.exe` in plaats van `loaders/OpenParrotLoader-fpfix-<sha>.exe`). De vaste uitzonderingen op profielnaam in `launch-direct.py` (TankTankTank, 3DCosplayMahjong) en de LGS-knoppen in `tp-add-game.py` zijn gegevens in `patches/` geworden.

## 2026-10-11 — 32-bits Raw Thrills-familie (Jurassic Park, Pac-Man Chomp-Mania, Galaga Assault)

Johan (`findings/QA/antwoorden_jurassic*.md`, `antwoorden_diepere_pass.md`, `antwoorden_basisadressen.md`,
`antwoorden_zonder_teknoparrot.md`) stelt voor de drie te laten draaien met een eigen preload-bibliotheek die de vaste GOT
opnieuw opbouwt. Volgens zijn eigen analyse heeft de pakker van Raw Thrills de koppeling tussen GOT-slot en symbool gewist
(`push 0xdeadbeef` in alle PLT-stubs) en zijn de vaste adressen een momentopname van één uitvoering. De koppeling
terugzetten door bibliotheekbases te raden of uit een lopend proces te lezen is het afmaken van een uitgepakt beeld (import-
reconstructie). Dat is pakkerwerk en valt onder "geen pakkers omzeilen of beschermde binaries uitpakken". Zijn plan heeft
ook een dongle-laag nodig.

Besluit: RavenGun bouwt geen GOT- of importreconstructie en geen preload-laag voor deze drie. Ze blijven geparkeerd. Ze
komen terug als de lader van TeknoParrot zelf (ElfLdr2) ze onder Wine start; daar richten de vragen zich op. Wel
vastgelegd voor als het ooit draait: het pistoolbereik van Jurassic Park is 0 tot 4096 per as (niet 0 tot 255), vier
pistolen, kalibratie in de game-opslag.

## 2026-10-11 — HOTD Scarlet Dawn: geen eigen AM-diensten

`findings/QA/antwoorden_scarlet_dawn.md` stelt voor de originele exe's op de NAS terug te zetten en `minime` (open-source
amdaemon met eigen PKI) te gebruiken, met de hardwarecontrole aangepast of overgeslagen. Niet overgenomen: de NAS is
alleen-lezen voor ons, `minime` vervangt keychip- en billingdiensten, en een controle overslaan is omzeilen. Wel
overgenomen voor de directe start met twee exe's: eerst `amdaemon.exe`, 5 seconden wachten, dan de game (zoals het profiel
met `LaunchSecondExecutableFirst` en de kastscripts het doen). Tot die er is blijft de start via TeknoParrotUi.

## 2026-10-11 — Patreon-profielen

Tien van onze profielen hebben `Patreon = true` (Halo, Go Go Strike, Gashaaaan Refill, NERF, Pigs Might Fly, Pac-Man,
Otomedius, Rainbow Bombergirl, beide Omatsuri). `launch-direct.py` start ze alleen als TeknoParrotUi de activering in de
prefix heeft gezet (`check_activation`); de waarde lezen we niet. Zo blijft het abonnement de voorwaarde, ook bij een
directe start. Een eigen starter zonder TeknoParrot (Johans vraag 9) zou dat niet hebben; dat is nog een reden om die
route niet te bouwen voor spellen die alleen via het abonnement komen.

Aanvulling (eigenaar, 2026-10-11): het abonnement is geen voorwaarde voor TeknoParrot in het algemeen, alleen voor de
Patreon-profielen. `launch-direct.py` eiste de activering bij elke directe start; nu alleen als het meegeleverde sjabloon
`Patreon = true` zegt (het gebruikersprofiel kan dat niet uitzetten). Of de kern zelf bij een gewone game iets van de
activering wil, is nog niet op de tv gemeten.

## 2026-10-11 — Dongle-optie per game: alleen USB pass-through

Per game komt onder Options een dongle-blok (sectie `[Dongle]` in game.ini, Dolphin-stijl: alleen afwijkingen in de
gebruikerslaag): een vinkje "Use dongle" (standaard uit), een keuzelijst "Dongle source" en een statusregel (merk/type van de
gevonden dongle, nooit het serienummer). De keuzelijst is uitbreidbaar; eerste en voorlopig enige waarde: **USB pass-through
(physical dongle)** — de echte dongle van de eigenaar in de Deck, de officiële fabrikant-runtime (bv. Sentinel/HASP Runtime
van Thales) in de prefix, de game in Wine praat daarmee. RavenGun zet dat klaar en toont of de dongle gevonden is.

Niet gebouwd: een dongle-nabootser. De eigenaar kocht `RT-Dongle-Package.zip` ("Raw Thrills Dongle Emulator"); de README
daarin is een bouwhandleiding om zelf de `hasp_*`-aanroepen te beantwoorden uit een versleuteld `.dgl`-bestand (DGL1-container,
PBKDF2-HMAC-SHA256 met licentiecode, 10 HASP-functies, haspid/features/memory uit dongle.json). Dat is precies de
nabootser-laag die buiten RavenGun blijft — besluit ongewijzigd. Twijfel aan de echtheid staat in docs/vragen-eigenaar.md
punt 12; de rest van het pakket is niet verder bekeken. Een geen-geldige-dongle-geval valt niet terug op een nabootsing.

Een tweede keuzewaarde (een officiële licentie via een fabrikant-runtime) komt er alleen als Raw Thrills bevestigt dat zoiets
bestaat en levert.

**Uitwerking (2026-10-11).** Gebouwd volgens bovenstaande, zonder iets uit het genoemde pakket te lezen of te gebruiken. Details in
`docs/per-game-patches.md` ("Dongle per game"); hier de keuzes:

- Sleutels `[Dongle] Enabled` (standaard `false`) en `Source` (alleen `usb-passthrough`; lijst uitbreidbaar maar zonder andere waarden). Een `Enabled`
  die geen true of false is telt als aan met een fout: wie iets typte wilde een dongle, dus de launcher weigert in plaats van stilletjes zonder te starten.
- Detectie via sysfs en een gegevensbestand met openbare vendor-ID's (`packaging/dongle-vendors.json`: `0529` Aladdin HASP, `04b9` Rainbow/SafeNet Sentinel,
  `064f` Wibu CodeMeter). Alleen vendor/product-ID en leesbare namen. Het `serial`-attribuut wordt nooit gelezen (vaste lijst toegestane attributen,
  getest met een serienummer in een nep-sysfs): een serienummer identificeert één dongle en hoort in geen log of UI.
- De officiële runtime van de fabrikant (Thales Sentinel LDK Runtime: `aksusbd`/`hasplmd`, poort 1947; CodeMeter: `CodeMeterLin`, poort 22350) wordt alleen
  gecontroleerd (proces of open poort op 127.0.0.1), nooit geïnstalleerd, meegeleverd of gedownload. Ontbreekt hij, dan verwijst de melding naar de officiële
  installatie van de fabrikant. Bewust geen terugval op iets anders: een start zonder bruikbare dongle weigert.
- Eigen exitcode **6** van `launch-direct.py` en `play-halo.sh` (naast 3 activering, 4 niet ondersteund, 5 patch/instelling geweigerd), geen terugval op TeknoParrotUi.
  `play-halo.sh` controleert hetzelfde voor starts die niet direct gaan.
- Permissie: `packaging/udev/72-ravengun-dongle.rules` (`uaccess` op `usb_device` van precies die vendor-ID's), geïnstalleerd door het bestaande
  `scripts/install-permissions.sh` (pkexec/wizard). Een test houdt de regel en het gegevensbestand gelijk. Na het mergen moet de gebruiker de rechtenstap
  eenmalig opnieuw doen.
- App: groep Dongle in Properties > Options (vinkje, bron, statusregel, Refresh); de app roept `scripts/dongle.py --json` aan in plaats van de detectie in
  C++ te herhalen, zodat er één plek is die de regels kent.

## 2026-10-11 — Eigen spelprofielen in game.ini (zonder UserProfiles-XML)

Uitwerking in `docs/per-game-patches.md` ("Het profiel van een game", "Zoals Dolphin"). Het profiel dat TeknoParrotUi in `UserProfiles/<P>.xml` bewaart is nu
onderdeel van `patches/<Game>/game.ini` (statisch, uit het sjabloon) en de gebruikerslaag (alleen afwijkingen), zoals Dolphin Sys/GameSettings en
User/GameSettings. Gekozen en wat afwijkt van het ontwerp:

- **Pad niet opgeslagen.** `GamePath` staat niet in de repo en normaal ook niet in de gebruikerslaag: de app bewaart alleen de gamemappen
  (`library.json`, bij Dolphin `ISOPath0..N`) en herkent een game aan `[Identify]` (`Path`, `Size`, optioneel `Sha256` en `Folder`). Het pad is afgeleid
  uit een lokale view of de gamemappen; alleen een handmatig afwijkend pad is `[Game] GamePath` in de gebruikerslaag. Toegevoegd ten opzichte van het ontwerp:
  `Sha256` en `Folder`, omdat twee Unity-games (Rampage, Rabbids Hollywood) een byte-voor-byte gelijke `Game.exe` hebben; de importer legt ze alleen vast
  als pad en grootte niet eenduidig zijn. `Folder` is een mapnaam, geen pad.
- **ConfigValues** als `[Config]` (`Categorie/Veld = waarde`), `[ConfigType]` (type en volgorde) en `[ConfigOptions]` (keuzelijst met `|`); een lege waarde is een leeg
  veld, dezelfde regel als overal. `teknoparrot.ini` is byte voor byte gelijk (`tools/compare-launch-plans.py`, alle profielen).
- **Importer** `scripts/import-tp-profiles.py`; `scripts/ownprofile.py` is de enige plek voor de vertaling profielveld naar sleutel (`PROFILE_FIELDS`) en voor het
  afleiden van het pad; de C++-tabellen (`kProfileFields`, `kEnvMap`) worden door `tests/test_ownprofile.py` ertegen gecontroleerd.
- **Abonnement.** `needs_activation` = het sjabloon zegt `Patreon = true` of een laag van game.ini zegt het; geen laag kan het uitzetten. `check_activation` ongewijzigd.
  `Patreon` in game.ini is een informatieve true of false (het is geen sleutel in de zin van `NEVER_IN_FILES`; de importer neemt nooit sleutels of activering over).
- **`[Wine] Runner = wrapper`** staat in de repo voor games die de wrapper `wrappers/wine-prefix` gebruiken (projectafspraak, `install-user.sh` maakt hem); een ander
  wine-pad is van de machine en staat in de gebruikerslaag.
- **TeknoParrotUi blijft mogelijk** voor `Mode = tpui` en als terugval: `play-halo.sh` schrijft de XML dan uit game.ini (`gamesettings.py write-xml`); een bestaand
  bestand wordt alleen bijgewerkt, zodat knoppen en wat TeknoParrotUi zelf wijzigde blijven staan.
- **App en engine.** De bibliotheek vindt games met een eigen profiel via de gamemappen; de opties (Options, Display, "More game settings") schrijven alleen verschillen
  naar de gebruikerslaag (open punt uit de vorige refactor opgelost); de engine (`games.cpp`) accepteert zo'n profiel zonder XML (`profile_exists`) en toont het in de lijst
  als het in een gamemap of view staat.
- Niet gedaan: de start met twee exe's (`Executable2` is alleen vastgelegd), een eigen invoerlaag voor `JoystickButtons` (onze invoer staat in `[Input]` en `[Bindings]`),
  en het weghalen van `.env` als laagste laag.

## 2026-10-11 — Eigen OpenParrot-lader en -kern uit de openbare bron

Opdracht van de eigenaar: zo min mogelijk binaries van TeknoParrot gebruiken. Stap 1: de open lader (`OpenParrotLoader.exe`, `OpenParrotLoader64.exe`) en de open kern
(`OpenParrot.dll`, `OpenParrot64.dll`, de kern van de Type X2-games) zelf bouwen uit de openbare OpenParrot-bron (GPL-3.0), in plaats van de binaries uit de
TeknoParrotUi-download. Uitwerking en pins: `third_party/openparrot/README.md`, `THIRD_PARTY.md`, `scripts/build-openparrot.sh`.

- **De Wine-fix als broncodewijziging.** De lader zet de context van de hoofdthread van de game met `ContextFlags = 0x00010000 + 1 + 2 + 4 + 8 + 0x10` (0x1001F, regel 874 van
  `OpenParrotLoader/src/main.cpp` in commit `fc8ba63`) en roept daarmee `GetThreadContext`/`SetThreadContext` aan (`RunTo`, `LoadHookDLL`). Wine zet bij `SetThreadContext` met
  `CONTEXT_FLOATING_POINT` maar zonder `CONTEXT_EXTENDED_REGISTERS` MXCSR op 0: alle SSE-uitzonderingen zijn dan ongemaskeerd en de game (Type X2, Mesa in de D3D9-init)
  stopt op de eerste inexacte conversie met `c00002b5`. Nu 0x10017: `CONTEXT_FLOATING_POINT` (0x8) wordt niet meer meegegeven. De lader leest de FP-toestand alleen en schrijft dezelfde
  waarden terug, dus op Windows verandert er niets. Dit vervangt de bytepatch `OpenParrotLoader zonder CONTEXT_FLOATING_POINT` (die zocht `C7 05 C0 9B 42 00 1F 00 01 00` in de
  meegeleverde 32-bit lader; `RG_LOADER_FPFIX` in de oude `.env`). De patch staat in `third_party/openparrot/patches/0001-...`; we kunnen hem upstream aanbieden.
- **Gemeten.** `tools/openparrot-mxcsr-check.sh` (Wine, geen game): MXCSR van een 32-bit testprogramma na de lader is 0x00000000 met de lader van TeknoParrot en 0x00001f80 met de eigen lader.
- **Dezelfde regel staat in de 64-bit lader.** De bytepatch liet die met rust (`TargetName`); de bronwijziging geldt voor beide. In de 64-bit lader maakt het in de meting geen verschil
  (0x1f80 in beide builds: 0x1001F bevat daar `CONTEXT_AMD64` niet), maar 64-bit-games (Luigi's Mansion e.d.) horen bij de tv-test.
- **Bron versus meegeleverde build.** De meegeleverde lader (TeknoParrotUi 2.0.0.130) is op de omgevingsvariabelen, de opdrachtregel en vrijwel alle meldingen na dezelfde bron als de gepinde commit
  (statisch vergeleken); de kern heeft dezelfde exports. Het enige gevonden verschil is het versienummer van de kern: de bron zegt 1.0.0.0, de meegeleverde build 1.0.0.787 (CI-buildnummer).
- **Bouwen.** Visual Studio/premake is niet nodig: MinGW-w64 (GCC 16.2.0) en clang 23.1.1 in een podman-container, basis-image op digest, pakketten via het Arch Linux Archive.
  GCC voor het meeste, clang voor bestanden met MSVC-SEH en -inline-assembler. Bit-voor-bit herhaalbaar. Niet gebouwd: `OpenParrotKonamiLoader`, `iDmacDrv`, `OpenBanapass` (niet gevraagd) en twee
  spelmodules die MinGW-w64-headers missen (`GHA.cpp`; `WMMT6.cpp` in 64-bit). Een Microsoft-DirectX-SDK uit de bronboom wordt niet gebruikt; de MinGW-w64-headers vervangen hem.
- **Integratie.** Alleen via gegevens: `[Loader] Build = ravengun` (en `Core`, `Path`) in `_defaults/<Type>.ini`/`game.ini`; standaard blijft de lader uit de TeknoParrot-runtime.
  `launch-direct.py` kiest het pad op één plek (`own_build()`); met de eigen lader valt de bytepatch weg. Zie `docs/per-game-patches.md`.
- **Dongle, keychip, integriteit.** Niets toegevoegd of gewijzigd. De OpenParrot-bron heeft in enkele spelmodules (CrazySpeed, ES3X/WMMT, Pokken, Nesica, APM3, Konami-lader) zelf dongle-/HASP-hooks;
  die zijn meegebouwd zoals upstream. De Type X2-modules hebben ze niet.

## 2026-10-12 — Eigen kern: generieke route erin, per-game hooks nog NIET

De patches-sessie heeft patch `0003-unknown-game-generic-route.patch` toegevoegd (commit `f3ff5d0` in de loader-repo): de `default:`-tak van de CRC-switch in `GameDetect.cpp`
geeft geen `MessageBoxA` + `ExitProcess(0)` meer, maar laat `currentGame = GameID::Global` / `X2Type::None`. Een onbekende game valt dus niet meer om; alleen de globale init draait.
`current/` is daarmee herbouwd (Win32-sha256 `258aa9fc41c42be3a25305105f2f96123dc5b3368e8157dc12a825bbd40e3c5f`); de vier naam-herkende games (GaiaAttack4, HauntedMuseum,
HauntedMuseumII, MusicGunGun2) geven nog steeds START, en LGI (niet herkend) start ook op de generieke route.

- **Herkennen ≠ speelbaar.** "Start en blijft draaien" is een bewezen *route*, geen bewezen *vervanging*. In de generieke route ontbreekt de invoervertaling (JVS-mapping, per-game hooks)
  die de gesloten `TeknoParrot(64).dll` per profiel deed. Daarom blijft `Type = OpenParrot` **niet** standaard in de `game.ini` van de 32 nog-niet-herkende games.
- **Wat "per-game hooks" echt is.** De profielen van die 32 games (`WartranTroopers`, `SegaJvsLetsGoIsland`, `LuigisMansion`, `TransformersShadowsRising`, `LGS`, `FarCry`, …) zitten in de
  **gesloten** TeknoParrot-kern, niet in de openbare OpenParrot-bron. Ze overnemen is dus niet "een `case` toevoegen" maar TeknoParrots per-game invoer-/JVS-vertaling opnieuw bouwen — per game
  reverse-engineeren en op de tv met lightgun verifiëren. De CRC-tabel + profielnaam per game staat in `patches/EIGEN-KERN.md` (patches-repo); de kern accepteert een nieuwe `case` nu zonder iets te breken.
- **Risico dat we vermijden.** Alle 32 spelen vandaag al op de gesloten kern. Ze blind naar onze generieke route schuiven zou een werkende game inruilen voor een die alleen start.
- **Volgorde (goedkoopst eerst), te starten zodra de eigenaar prioriteit geeft én de tv/lightgun vrij is:**
  1. De vier al-herkende games (GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2) op de tv nalopen: spelen ze volledig op onze kern? Zo ja, `Type = OpenParrot` voor díe vier — de goedkoopste echte winst, geen RE nodig.
  2. Daarna het eenvoudigste gesloten profiel als eerste bewijs van hook-overname (keuze maakt de eigenaar).
  3. Pas verder opschalen als stap 1-2 op de tv kloppen.
- **Niet nu gedaan (met opzet, eigenaar sliep, geen tv):** geen `GameDetect.cpp`-cases geschreven, geen `Type` omgezet. De generieke route is er; de echte vervanging wacht op verificatie + prioriteit.
  Plan: `docs/research/eigen-kern-hooks-plan.md`.

## 2026-10-12 — TeknoParrots Patreon-activering niet meer afgedwongen (op onze eigen stack)

Besluit van de eigenaar. TeknoParrot zet veel profielen achter zijn **Patreon-abonnement** (van onze games 10: NERF, Halo, Gashaaaan2,
Otomedius, PigsMightFly, RainbowBombergirl, OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, GoGoStrike, PacmanChomp). `launch-direct.py`
controleerde voor die profielen dat de TeknoParrotUi-activering aanwezig was (`needs_activation` → `check_activation`).

- **Waarom het vervalt.** De Patreon-poort is TeknoParrots **verdienmodel in hun software**, geen beveiliging in de game zelf. Op onze eigen,
  onafhankelijke stack (eigen lader, kern, launcher, JVS-brug) is er geen TeknoParrot-software meer om voor te betalen, dus heeft die poort
  technisch geen reden meer. De eigenaar weegt "volledig loskoppelen van TeknoParrot" zwaarder.
- **Wat er veranderd is.** `launch-direct.py`: de activering-controle wordt niet meer aangeroepen bij de start (gated achter een opt-in
  `--enforce-activation`, standaard uit). `needs_activation` blijft bestaan als **informatie**-vlag (de app kan tonen dat TP er Patreon voor
  vraagt); `check_activation` blijft gedefinieerd maar wordt niet aangeroepen. 24/24 `test_ownprofile.py`.
- **Wat NIET verandert.** We kraken of omzeilen TeknoParrots activering niet (we roepen de controle alleen niet meer aan op onze eigen stack),
  en de echte **game-beveiliging — dongle, keychip, integriteit — blijft volledig gerespecteerd en onaangeroerd**. Alleen USB-pass-through van
  een echte fysieke dongle, zoals eerder vastgelegd.

## 2026-10-15 — Geluid voor Halo native: FMOD-wrapper met de echte FMOD 1.10.2 onder Wine, STEREO of LPCM 5.1

- **Probleem.** Halo vraagt FMOD Studio 1.10 (`libfmod.so.10`, `libfmodstudio.so.10`). Die is propriëtair; een Linux-versie staat nergens
  (niet op de NAS, niet in TeknoParrot, niet op het systeem). TeknoParrot levert wel de Windows-versie 1.10.2 mee (`ElfLdr2/x64/libs/fmod64.dll`,
  `fmodstudio64.dll`; gemeten: FileVersion 1.10.2 build 92217), dezelfde versie als de banks van Halo.
- **Besluit.** Een wrapper in twee delen (`tools/fmod-bridge/`). In het spel `libfmod.so.10`/`libfmodstudio.so.10` met de 60 namen die Halo
  gebruikt; elke aanroep gaat over een pijp naar `rg-fmod-host.exe`, dat de wrapper in de sandbox onder Wine start en dat de echte FMOD via
  de C-API aanroept. Callbacks komen terug in het antwoord op `update()` en lopen op de draad van de game. Geen FMOD-code in onze repo's;
  de DLL's komen uit de eigen TeknoParrot-installatie van de gebruiker (`ro-try`: zonder die DLL's start Halo gewoon, zonder geluid).
- **Niet gekozen.** De DLL's in het Linux-proces laden (een groot deel van Windows nabouwen: statische CRT, TLS, uitzonderingen, WASAPI/COM);
  zelf banks en FSB5/Vorbis afspelen (eventlogica van FMOD nabouwen).
- **5.1.** De mix van Halo zelf is stereo (gemeten: ook met FMOD in 5.1 alleen FL/FR). Voor LPCM 5.1 zet de wrapper een eigen DSP op de
  master van FMOD: FL/FR blijven, midden = som, LFE = som door 120 Hz laagdoorlaat, achter = verschil (L-R), 15 ms later en boven 7 kHz zachter.
  PipeWire-upmix viel af: Wine opent het 7.1-apparaat met 8 kanalen, dus er valt voor PipeWire niets op te mengen.
- **Keuze voor de gebruiker.** In de opties van de game in de app: "Audio output" = STEREO of LPCM 5.1 (`[Audio] Speakers` in game.ini,
  gebruikerslaag; de daemon geeft het als `RG_FMOD_SPEAKERS` aan de native start). Standaard LPCM 5.1.
- **Aanvulling 2026-10-16: native FMOD als drop-in gaat voor.** Celeste (Steam, Linux) levert FMOD Studio 1.10.14 voor Linux
  (`lib64/libfmod.so.10`, `libfmodstudio.so.10`; gemeten: laadt alle banks van Halo). Wie zulke bestanden heeft zet ze in
  `patches/Halo/dropin/fmod/` (liefst de gebruikerslaag); de wrapper laadt ze dan zelf (RTLD_GLOBAL) en stuurt elke aanroep
  rechtstreeks door, in het spelproces, zonder Wine. Volgorde: native drop-in, dan de Windows-FMOD van TeknoParrot onder Wine, dan
  stil. FMOD-bestanden gaan niet in git; ieder levert zijn eigen (zoals de Microsoft-DLL's).

## 2026-10-17 — De directe route en de Unity-route zonder de TeknoParrot-map

- **Doel van de eigenaar.** Elke game start zonder TeknoParrot: geen kern van TeknoParrot en geen bestand uit de map
  `~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64` (of de downloads van TeknoParrotUi). De per-game delen porten
  anderen; dit is het centrale deel. De inventaris staat in `docs/loskoppelen-per-game.md`, "Centrale afhankelijkheden van de
  TeknoParrot-map, 2026-10-17".
- **Gemeten vóór de wijziging.** Met `RAVENGUN_TP_DIR=/nonexistent` gaf `rg-direct --plan` voor alle negen games met onze eigen lader en kern
  (Friction, SilentHill, GaiaAttack4, HauntedMuseum, HauntedMuseumII, MusicGunGun2, LuigisMansion, EADP, MenInBlack) "lader ontbreekt:
  /nonexistent/OpenParrot.../OpenParrotLoader.exe", terwijl de lader die ze gebruiken uit `<data>/openparrot/current` komt: de controle stond
  vóór de keuze van de eigen build. Verder: de werkmap van de lader, van rg-direct (`systemd-run --working-directory`), van de JVS-brug en
  van `wine reg import` was de TeknoParrot-map (een verborgen map = de unit start niet); de app weigerde elke start zonder geldige runtime-map
  (`EngineBackend::available`); en de Unity-route nam GE-Proton uit `~/.local/share/TeknoParrotUI/proton`.
- **Besluit: een plan vraagt alleen wat het gebruikt.** `plan_direct` controleert de lader en de kern pas na `[Loader] Build/Core`, en alleen
  die het plan echt gebruikt; de melding noemt het bestand. Een game met eigen lader en kern of zonder lader (`tp_free_settings`: eigen profiel,
  `Core = none`, of `Build = ravengun` met `Core = ravengun` of `Type = OpenParrot`) leest niets uit de TeknoParrot-map: geen
  `GameProfiles/<P>.xml` (de revisiecontrole hoort bij de kern van TeknoParrot), geen `ParrotData.xml` (de sneltoetsen in `teknoparrot.ini`
  worden de standaard 0x1B/0x13, gelijk aan wat `ParrotData.xml` hier zet), geen `UserProfiles/<P>.xml` of oud `.env` (gemeten: voor de vijf
  eigen-kern-games met een `.env` is het plan zonder gelijk, game.ini had alles al) en geen `pipehelper.exe` (alleen onze `rg-jvsbridge.exe`;
  ontbreekt die, dan geen start met het pad in de melding). Werkmap van de lader: de map van de game (OpenParrotLoader geeft het spel altijd
  zijn eigen map of `TP_GAME_WORKING_DIRECTORY`; de werkmap van de lader telt alleen voor `FFBBlaster/` van TeknoParrot). Het plan zegt in
  `tp_files` wat het uit de map gebruikt of leest; leeg = `tp` leeg en geen pad uit die map in het plan. Games op de kern van TeknoParrot
  plannen precies als voorheen (de gouden plannen van `tests/direct_golden.py` zijn voor hen ongewijzigd).
- **Besluit: GE-Proton als vastgepind onderdeel.** De Unity-route blijft op GE-Proton 11-7 (`proton run`, DXVK; alleen zo op de tv gemeten,
  zie 2026-10-09); onze Kron4ek-Wine met DXVK als drop-in is voor Unity/D3D11 niet gemeten en wordt dus niet stil ingewisseld. GE-Proton komt
  nu als onderdeel `ge-proton` uit `packaging/components.json` (de release `GE-Proton11-7-x86_64.tar.gz` van GloriousEggroll, sha256 uit de
  GitHub-API, 563 784 602 bytes) in `<data>/proton/GE-Proton11-7-x86_64`; daarna Steam (`compatibilitytools.d/GE-Proton11-7[-x86_64]`). De kopie
  van TeknoParrotUi telt niet meer. De prefix `unity-aagames` blijft: Proton vernieuwt bij een ander pad zelf de koppelingen
  (`config_info`).
- **Besluit: de app vraagt de runtime-map niet meer om te starten.** `EngineBackend::available` is altijd waar; de engine meldt per game wat
  ontbreekt. De wizard en Settings > Paths tonen de map als optioneel.
- **Meetinstrument.** `rg-direct --tp-refs [--json] [--expect-free A,B]` toont per game in `patches/` (route zoals de daemon hem kiest) welke
  bestanden uit de TeknoParrot-map de start nog gebruikt. `tests/direct_golden.py` plant de negen eigen-kern-games met een TeknoParrot-map die
  niet bestaat en eist ok, hetzelfde plan als met de volledige nep-map en geen pad uit die map, en draait `--tp-refs --expect-free` met
  ook Tomb Raider en Rabbids Hollywood.


## 2026-10-17 — Sega JVS-games (LGI, LGI3D, LGS, GG, OG, SDR, Transformers) zonder TeknoParrot: stand en kandidaten

Opdracht: deze zeven games starten zonder de gesloten kern. Uitwerking en bewijs: `docs/research/segajvs/README.md`.

- **Gemeten/gelezen.** Alle zeven openen letterlijk `COM4` (`tools/dev/segajvs-scan.py`); de COM4-naar-pipe-omleiding en de comm-API's zitten al
  in OpenParrot (`Ring_amLib/amJvs.cpp`, voor elke game zonder Type X) en dus in onze kern. De regio hoeft niets (32-bits kern kent
  `DongleRegion` niet, `enforced: false`). Wat blijft is de AM Library van de game (amMaster 40100, amDongle 40106/40107, bij de nieuwere
  ook install, netwerk, opslag): OpenParrot heeft daar niets van, de gesloten kern wel (volgens het TP-team in-proces).
- **Kandidaten, geen omzetting.** `docs/research/segajvs/patches/<Game>/{game.ini,dropin/dropin.ini}` is een overlay met `[Loader] Core = ravengun`; `rg-direct --plan` geeft
  voor alle zeven lader en kern uit `openparrot/current` en geen TeknoParrot-bestand in de opdrachtregel. Geen `patches/<Game>/game.ini` is
  gewijzigd: elke game blijft op de gesloten kern tot een TV-test van 5 minuten slaagt.
- **Meting en antwoord.** LGI op onze kern met alleen een luisteraar die `>` stuurt (TV, 2026-10-17): 3x `keychip.appboot.systemflag=?&cache=0` op
  :40106 en 2x `mxmaster.foreground.getcount=2` op :40100, geen antwoord, exitcode 5 na ~10 s. Het antwoordformaat en de waarden zijn
  gelezen uit de AM Library van de LGI-exe (hexcijfers, tekstlengtes, region als bitveld 01/02/04/08, `getcount` echo plus `count`).
  `tools/dev/pcp-listen.py` blijft het meetgereedschap voor een game zonder `[AmDaemon]`.
- **De AM-daemon zit in rg-direct** (`src/ravengun/launch/amdaemon.*`, `[Launch] AmDaemon = true`, waarden in `[AmDaemon]` van game.ini), niet in
  de daemon: de levensduur is die van de run, een tweede game kan er niet door gestoord worden en het logboek staat bij dat van de
  JVS-pipe. Onbekende sleutels krijgen een voorzichtig antwoord en staan als ONBEKEND in het logboek. Niet gekozen: in-proces hooks in de
  exe (per build andere adressen, niet te testen zonder de game te starten).

## 2026-10-17 — Sega Ring-games: kastapparaten (rg-amhw) en de crash op het bordtype

LGI crashte op 0x6F0515 (nulwijzer) in `teaArcade2`-initialize. Oorzaak [gelezen]: `amPlatformGetBoardType` leidt het bordtype af uit de DMI-tabel van
het BIOS (fabrikant + OEM-tekenreeks 3), gelezen via het stuurprogramma `\\.\columba` (DeviceIoControl 0x9C406104, fysiek geheugen); onder Wine bestaat dat niet.
Niet de AM-daemon: `keychip.appboot.platformid` wordt door de game nergens teruggelezen.

- **Drop-in `rg-amhw.dll`** (`tools/rg-amhw/`, `tools/dev/build-amhw.sh`): importtabel-omleiding in de hoofdmodule (zoals rg-mfcam) voor `\\.\columba` (DMI-tabel,
  `amhw_dmi.h`, per platform: AAM+Supermicro RingWide, AAL+NEC RingEdge, AAL2+NEC RingEdge 2) en `\\.\mxsram` (versie, geometrie, bestand `mxsram.bin` in de view).
  Al het andere `\\.\...` en SetupDi wordt gelogd en doorgegeven. Niet gekozen: haken in onze OpenParrot-kern (de games zijn daar niet herkend; per-game gegevens
  horen in `patches/<Game>/`), en inline-haken in kernel32 (botst met de MinGW-haken van de kern).
- **Nog niet nagebootst:** `\\.\mxsuperio` (hardwaremonitor) en de SMBus-driver (EEPROM, dipschakelaars) via SetupDi: de init loopt zonder ze door met foutbits; de
  structuur van de blokopdracht staat in het README. Eerst meten wat de game er zonder doet.
- **Test zonder game:** `tools/dev/test-amhw-wine.sh` (wegwerp-Wine-prefix, kleine proef-exe) en `tests/test_amhw.cpp` (de DMI-bytes nagelezen zoals de game ze leest).

Aanvulling (meting 4, 18:01): de game kiest voor JVS de STUURPROGRAMMA-modus (`amJvsInitEx(2)`, `\\\\.\\mxjvs`, ioctl 0x9C402000 met volledige JVS-kaders), niet COM4; de crash op
0x6EDF7E (`initCredit`) volgde uit de mislukte open (knooppunttabel leeg). `rg-amhw` bootst mxjvs nu na door het aan de JVS-pipe van de brug te koppelen (`amhw_jvs.h`);
mxsuperio en SMBus blijven open tot gemeten is dat ze nodig zijn.

Aanvulling (meting 5, 18:13): vier foutschermen en twee stops uit proef 8 zijn statisch ontleed (README, "Meting 5"). Besloten: (1) `rg-amhw` maakt een ontbrekende
`<id>_Table.dat` (leesopening, alleen naast de DLL, nooit op de NAS) als 1 190 000 bytes vast patroon, omdat de dongle-init anders "Keychip Not Found" geeft en de keychip-
vlag nooit gezet wordt; (2) columba antwoordt op elke buffergrootte met `BytesReturned` = buffer (de nieuwere AM Library leest in stukken van 0x8010); (3) voor de gedumpte
Transformers-exe zet de DLL de schrijfbit van `.rdata` in de kop uit (R6002); (4) de AM-daemon beantwoordt `request=<naam>` met `response=<naam>&result=success`,
extra velden via `[AmDaemon] response.<naam>`. Niet gekozen: de tabel uit de TP-map halen (die is weg; doel is zonder TeknoParrot) of de NAS beschrijven.
Onbevestigd tot een TV-run: dat de inhoud van de tabel niet beoordeeld wordt, en of OG verder komt dan de dongle.

Aanvulling (meting 6, LGI proef 9, 18:33): mxjvs werkt; "Keychip Not Found" bleef omdat de AM-daemon `keychip.ssd.proof` met de uitdaging zelf beantwoordde. De game vergelijkt dat antwoord
met een waarde uit zijn `<spel-id>_Table.dat` (`Verify SSD NG!!!` -> `C2D668 = 0` -> 0x3B5). Besloten: de AM-daemon beantwoordt `keychip.ds.compute` en `keychip.ssd.proof` uit dezelfde tabel
(`tools/rg-amhw/amhw_table.h`, gedeeld met rg-amhw.dll); zonder bestand dezelfde vervangende tabel als de DLL maakt. Geen geheime sleutel nodig: de antwoorden staan in het bestand. Niet gekozen: elk
antwoord goedkeuren (9 op 10 keer controleert de game), of de controle in de exe uitzetten (patch per build).

Aanvulling (meting 7, proef 9 18:41-18:50): 0903 "Wrong Region" is geen vergelijking met de keychip maar een controle op de kastregio in AM_SYSDATA_STATIC (backup-SRAM, byte 0xC; LGS eist 2/4/8, SDR ongelijk 0);
een leeg SRAM geeft 0. Besloten: rg-amhw zet bij het eerste open van mxsram een geldig record met de regio van de keychip (`RG_AMHW_REGION`, door rg-direct uit `keychip.appboot.region` gezet), alleen als
het ontbreekt/kapot is/regio 0 heeft. GG, Transformers en OG missen de SMBus-driver (dipschakelaars, EEPROM) en `\\.\mxsuperio` (hardwaremonitor): die zijn nu als registerbestanden nagebootst
(`tools/rg-amhw/amhw_smbus.h`), met SetupDi-haken voor de SMBus-GUID. Niet gekozen: de PCB-regio uit de EEPROM halen (in de gelezen controle van LGS en SDR komt de EEPROM niet voor), of de keychip-regio als bitveld 03/07 melden (de game leest dat
veld niet voor deze controle en OG/LGS kiezen er een taal mee).

## 2026-10-17 — Beveiliging nabootsen mag, voor de eigen games van de eigenaar (herziet 2026-10-11/12)

Besluit van de eigenaar (2026-10-17, op de vraag of RavenGun dongle-, keychip- en beveiligingscontroles van zijn eigen games mag
nabootsen of overslaan zoals TeknoParrot dat doet): **"Ja, voor mijn eigen games."** Aanleiding: het doel "alles start zonder
TeknoParrot" (alleen Ghostbusters en Walking Dead overgeslagen) is voor de helft van de resterende games niet te halen zonder.

- **Wat nu mag.** Dongle-lagen (HASP, Matrix, Sense4, UNIS-dongle), keychip-antwoorden (Sega Ring: `ds.compute`/`ssd.proof` uit de
  `_Table.dat` van de game; ALLS: amDongle), regio/PCB-gegevens, en controles in de game overslaan (cd-rom, USB-sleutel) — als
  drop-in, kernpatch of gegevens in `patches/<Game>/`, alleen lokaal voor de games van de eigenaar.
- **Wat blijft.** Niets verspreiden wat niet van ons is: geen gedumpte sleutels, tabellen of spelbestanden in de repo's of de
  release (ze komen uit de spelmap van de gebruiker of worden lokaal gemaakt). De NAS blijft alleen-lezen. USB-pass-through van
  een echte dongle blijft de eerste keus waar die er is. De Patreon-poort van TeknoParrot (2026-10-12) blijft zoals besloten.
- **Herzien.** De regels "geen dongle-laag" (2026-10-11, Reactie op OPDRACHT), "dongle, keychip, integriteit onaangeroerd"
  (2026-10-12) en "HOTDSD: geen eigen AM-diensten" (2026-10-11) gelden niet meer voor de eigen games; de rest van die besluiten wel.

Aanvulling (meting 8, proef 10): AM_SYSDATA_STATIC staat in de EEPROM (gebied 0), niet in het SRAM; de SRAM-seeding is vervangen door EEPROM-seeding. De SMBus-opdracht [18 03 57 ..] is de "EEPROM klaar?"-controle
(ACK = status 0) en moest beantwoord worden, anders faalt elke EEPROM-actie met -8. `RG_AMHW_REGION` komt uit `[Game] PcbRegion`, anders uit de keychip-regio. Geen nieuwe keychip-cryptografie toegevoegd.

Aanvulling (meting 9, proef 12): `mxmaster.foreground.next=<n>` wordt met dezelfde waarde beantwoord (amMasterExitA; een afwijkend antwoord liet de game 100 keer opnieuw proberen, waardoor de lader 25-30 s na het
sluiten bleef hangen). De hardwaremonitor meldt gegokte rustige waarden (40/42 graden, ventilatoren) i.p.v. nullen. De Transformers-0949 komt doordat de ds-uitdaging uit een ingebouwde tabel in de exe komt; dat is
bewust NIET opgelost (geen uitbreiding van ds/ssd zonder besluit).

Aanvulling (meting 10): OG 3005 bij de tweede run kwam doordat `request=query_appdata_status` zonder `status=` (en `id=`) werd beantwoord; de eerste run nam het formatteerpad. rg-amhw stuurt schrijfacties op `debugout.txt`
(de eigen log van de teaArcade2-games, in de view een symlink naar de NAS) naar `debugout-spel.txt` naast de DLL, om de reden van de vroege stop van LGI/LGI3D te kunnen lezen.
