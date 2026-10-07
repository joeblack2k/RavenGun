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
