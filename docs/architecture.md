# Architectuur van ravengund (fase 2B/2F-kern, 2D-camera)

Dit document beschrijft wat nu gebouwd is en welke afspraken vastliggen. Statuslabels: "geïmplementeerd" (code bestaat), "automatisch getest" (CTest), "op echte hardware getest" (op deze host met de Move), "nog te bevestigen".

## Processen en eigendom van hardware

Er is één daemon, `ravengund`, en die is de enige eigenaar van de live hardware: de Move (via PSMoveAPI) en de camera (V4L2). Ook het virtuele uitvoerapparaat (uinput-lightgun, zie "Uitvoer") is van de daemon. Clients (`ravengunctl`, later de webinterface en de kalibratieclient) praten alleen via de socket met de daemon en openen zelf nooit hardware. Ze mogen op elk moment stoppen zonder dat de verwerking stopt.

`ravengund` draait op de voorgrond (bedoeld voor een terminal of later een systemd-userdienst). Eén instantie per gebruiker wordt afgedwongen met `flock` op `$XDG_RUNTIME_DIR/ravengun/ravengund.lock`. Het slot vervalt vanzelf als het proces stopt, ook na een crash. Een tweede instantie stopt met exitcode 69. SIGINT en SIGTERM worden in de hoofdthread met `sigwait` afgehandeld: de threads stoppen, de Move wordt gesloten nadat alle knoppen zijn vrijgegeven, en de socket wordt verwijderd.

Geïmplementeerd en automatisch getest (`tests/ipc_smoke.sh`). Op echte hardware getest: starten, verbinden met de Move en stoppen.

## Threads

1. De hoofdthread doet de opstart, het slot en de signalen.
2. De HID-thread (`src/ravengund/hid_thread.cpp`) is de enige code die PSMoveAPI aanroept. Ze bezit de Move-handle en de verwerkingspijplijn. Ze leest elk beschikbaar rapport (`psmove_poll` tot er niets meer is, max. 64 per ronde) en verwerkt elk rapport afzonderlijk. Als er niets nieuws is, slaapt ze 1 ms. PSMoveAPI geeft geen file descriptor om op te wachten. Daardoor komt er 0 tot ongeveer 1 ms wachttijd bovenop de Bluetooth-latentie.
3. De IPC-thread (`src/ravengund/ipc_server.cpp`) draait een `poll()`-lus over de luistersocket en alle clients, met niet-blokkerende sockets. Opdrachten die de pijplijn veranderen (recenter, filter, aim-schaal) of die statistiek nodig hebben (status) gaan via een wachtrij naar de HID-thread, met een time-out van 500 ms. Zo heeft de pijplijn één eigenaar en zijn er geen locks in de rekenroute. Live samples leest de IPC-thread uit een momentopname die de HID-thread na elk sample onder een korte mutex publiceert.

4. De capture-thread (`src/ravengund/camera.cpp`) is de enige code die de camera opent (zie "Camera en boltracking").
5. De tracker-thread verwerkt het nieuwste cameraframe en publiceert de optische meting. Camera en tracker staan los van de HID-thread: een trage of haperende camera vertraagt de IMU-verwerking niet.

Een trage client kan de verwerking niet ophouden. Staat er bij een watch-client nog meer dan 16 KiB te wachten, dan wordt dat frame overgeslagen (en geteld). Boven 256 KiB wordt de client verbroken.

## Modules

De library `libravengun` bevat alle logica zonder hardware:

- `core/math.hpp`: vectoren en quaternions.
- `core/fusion.*`: de gyro-biasschatter en het oriëntatiefilter.
- `core/aim.*`: de voorlopige aim-mapping en recenter.
- `core/one_euro.hpp`: het 1€-filter.
- `core/buttons.*`: knopstatus en -flanken.
- `core/pipeline.*`: koppelt dit alles. Rapport in, `InputSample` uit, plus staleness en statistiek.
- `core/input_model.*`: het inputmodel en de JSON-vorm daarvan.
- `vision/latest_queue.hpp`: de begrensde wachtrij die altijd het nieuwste frame levert.
- `vision/frame_timing.*`: fps, frame-intervallen, gemiste frames en leeftijd.
- `vision/ball_tracker.*`: de boltracker (eigen code, geen OpenCV).
- `output/gun_transform.*`: de enige plek voor inversie, letterboxing, schaal en clamp naar de apparaatassen.
- `output/gun_output.*`: arm/disarm, knoppentoestand en vrijgeven bij disarm/fout/afsluiten, los van uinput (nep-apparaat in tests).
- `output/test_pattern.*`: synthetische patronen (center, corners, grid, sweep, point).
- `output/uinput_gun.*`: het uinput-apparaat zelf.
- `json_parse.*`, `ipc_protocol.*` en `ipc_client.*`: het beheerprotocol.

De daemon voegt daar alleen de hardware-, thread- en socketcode aan toe. Tests voeren synthetische rapporten rechtstreeks aan de pijplijn.

Nog niet gebouwd (latere fasen): het gebruik van de optische meting in de aim (hybride modus), cameramodel en kalibratie, persistente configuratie, het koppelen van de live aim en Move-knoppen aan de uitvoer (de backend zelf staat er, zie "Uitvoer"), Bluetooth-onboarding in de daemon en de webinterface.

## Tijdbases

Alle tijden komen uit `CLOCK_MONOTONIC` (nanoseconden), dezelfde klok als de V4L2-timestamps van de PS3 Eye (die zijn ook monotonic). Ze zijn dus later direct vergelijkbaar.

- `t_rx_ns` is de ontvangsttijd. Dit is het moment direct nadat `psmove_poll()` in de HID-thread een nieuw rapport opleverde. Het is GEEN sensor- of belichtingstijd. Het bevat de Bluetooth-overdracht, de kernel/hidraw-buffer en tot ongeveer 1 ms van onze poll-lus. Het rapport bevat ook een eigen tijdteller van de Move, maar PSMoveAPI (commit 256b629) maakt die niet beschikbaar via de publieke API. Daarom wordt die niet gebruikt (nog te onderzoeken).
- `t_eval_ns` is het moment waarop de toestand is geëvalueerd. Bij een rapport is dat gelijk aan `t_rx_ns`. Bij een toestandswissel zonder rapport (stale, lost, disconnect) is het het moment van die evaluatie.
- `report_dt_ms` is `t_rx` min `t_rx` van het vorige rapport. Dit is de delta-t die de fusie en het filter gebruiken. Per rapport zijn er twee IMU-subsamples (ZCM1). Elk krijgt de helft van die dt, de oudste (`Frame_FirstHalf`) eerst. De som van de dt's is gelijk aan de verstreken tijd, dus bij Bluetooth-bursts (korte en lange intervallen door elkaar) gaat netto geen rotatie verloren. Een kleinere `t_rx` dan de vorige wordt geteld (`non_monotonic`) en krijgt dt 0.
- In de status staat `processing_us`: de tijd van `t_rx` tot het publiceren van het sample (fusie, aim, filter en publicatie). Dat is geen end-to-end-latentie.

Gaten: 50 tot 250 ms wordt geïntegreerd alsof de hoeksnelheid constant was (geteld als `long_dt`). Bij meer dan 250 ms wordt niet geïntegreerd. Dan wordt `yaw_reference` "interrupted", omdat de rotatie in het gat onbekend is.

## Assen, eenheden en quaternionconventie

Sensorwaarden komen uit `psmove_get_accelerometer_frame` en `psmove_get_gyroscope_frame` van PSMoveAPI, met de USB-kalibratie van de controller (`~/.psmoveapi/<adres>.calibration`). De accelerometer geeft g (lineair: `raw*a+b`, ijkpunten ±1 g). In rust meet hij +1 g in de richting "omhoog". De gyroscoop geeft rad/s (`(raw-offset)*factor`, ijking op 80 of 90 rpm). PSMoveAPI past daarbij zelf geen filter toe. Zijn eigen oriëntatiefilter (Madgwick) draait alleen na `psmove_enable_orientation(true)`, en dat roept RavenGun niet aan. Gecontroleerd in `src/psmove.c` en `src/psmove_calibration.c`. In de uitvoer staan de gyrowaarden in graden/s en de hoeken in graden. Intern rekent alles in rad en rad/s.

Het controllerstelsel is dat van PSMoveAPI: +Y wijst van het handvat naar de bol, +Z wijst uit de knoppenkant, +X = Y × Z (rechtshandig). Bron: in `psmove.c` geeft de "upright"-houding (bol omhoog) zwaartekrachtrichting (0,1,0), en de "lying flat"-transformatie zet die op +Z. Dat +Z bij "knoppen omhoog" positief is, is nog op hardware te bevestigen.

Het wereldstelsel is rechtshandig met Z omhoog (tegen de zwaartekracht in). De richting van wereld +Y wordt bij het opstarten willekeurig gekozen (yaw 0 bij de eerste accelerometermeting). Zonder magnetometer heeft yaw geen absolute betekenis, alleen een betekenis ten opzichte van het vastgelegde midden.

De quaternion `q_wb = (w, x, y, z)` is een Hamilton-eenheidsquaternion die een vector uit het controllerstelsel naar het wereldstelsel roteert: `v_w = q v_b q*`. De gyro-integratie is `q ← q ⊗ exp(ω·dt/2)` (hoeksnelheid in het controllerstelsel, rechtsvermenigvuldiging, exacte integratie bij constante ω).

De richtas is controller +Y. Of de fysieke richtas bij de gebruiker (los in de hand of in een houder) precies +Y is, is nog te bevestigen. Een controller-naar-richtas-transformatie volgt bij de kalibratie.

## Oriëntatieschatting

Het algoritme is een Mahony-complementair filter met alleen een proportionele term (`kp` = 0,5 per s), IMU-only. De accelerometer corrigeert alleen roll en pitch, en alleen als `| |a| - 1 g |` ≤ 0,15 g is. Bij een sterke versnelling (een schot, een zwaai) wordt dat sample niet gebruikt voor correctie. Er is bewust geen integrator-term. De gyro-bias wordt apart en zichtbaar geschat. De magnetometer van de ZCM1 wordt niet gebruikt: PSMoveAPI meldt "not yet calibrated", en lokale verstoring is niet onderzocht. Yaw drift dus met de restbias en met de ruis (random walk).

Gyro-biasschatting: per subsample wordt een venster van 0,5 s bijgehouden. Het venster telt als stil als de standaardafwijking per as ≤ 0,035 rad/s is (2 graden/s; de gemeten ruis van deze ZCM1 is ongeveer 0,5 graden/s op X en Y en 1,1 graden/s op Z), het gemiddelde minder dan 0,10 rad/s van de huidige bias afwijkt en `|a|` overal binnen 0,08 g van 1 g blijft. Tijdens stilstand is de bias het lopende gemiddelde van alle stille vensters, tot er 30 s stilstand is gezien. Daarna is het een exponentieel gemiddelde met een tijdconstante van 30 s. `bias_state` is "none" (nog geen stilstand), "estimating" (minder dan 3 s stilstand) of "estimated". Beperking: een zeer gelijkmatige, langzame rotatie (onder de drempels) is niet van bias te onderscheiden en wordt langzaam als bias opgenomen. In de hand is er vaak geen stilstand, en dan blijft de laatste schatting staan. De daemon verbergt geen drift: er is geen automatische recenter en geen extra filter op de oriëntatie.

## Voorlopige aim-mapping (trackingmodus "orientation")

Dit is uitdrukkelijk GEEN geometrische aim-ray: positie en schermvlak spelen geen rol. Uit `q_wb` volgen yaw = atan2(d_x, d_y) en pitch = atan2(d_z, √(d_x²+d_y²)) van de richtas d in het wereldstelsel. Ten opzichte van het midden: `yaw_rel = wrap(yaw - yaw_midden)`, `pitch_rel = pitch - pitch_midden`. Daarna lineair: `x = 0,5 + yaw_rel / (2·h_edge)` en `y = 0,5 - pitch_rel / (2·v_edge)`. Standaard is h_edge 20 graden en v_edge 11,25 graden. Dit zijn startwaarden, geen kalibratie, en ze zijn instelbaar met `--h-edge-deg`, `--v-edge-deg` en `set_aim`. Rol om de richtas heeft geen invloed. Bij pitch rond ±90 graden is yaw slecht gedefinieerd, en voor een lightgun is dat buiten het werkgebied.

Het midden wordt alleen vastgelegd door `recenter` (center_source "user") of eenmalig per daemon-run bij het eerste rapport (center_source "auto_initial", uit te zetten met `--no-auto-center`). Een andere controller (een ander adres) wist het midden. Een herverbinding van dezelfde controller behoudt het midden, maar zet `yaw_reference` op "interrupted".

## Filtering

Er is precies één filterlaag: het 1€-filter (Casiez e.a. 2012) op de genormaliseerde aim x en y (unclamped, vóór het clampen), met de echte `report_dt` per rapport. Standaard staat het aan met min_cutoff 1,0 Hz, beta 0,5 en d_cutoff 1,0 Hz. Het is uit te zetten met `--no-filter` of `ravengunctl filter --off`. Het filter wordt gereset bij recenter, verlies, (her)verbinden, een gat van meer dan 250 ms en een wijziging van de instellingen. Ongefilterde (`x_raw`, `y_raw`) en gefilterde (`x`, `y`) waarden staan allebei in elk sample. PSMoveAPI filtert niets (zie hierboven). Er is geen voorspelling, deadzone of hysterese.

## Staleness, verlies en disconnect

Een rapport komt normaal elke 13 ms gemiddeld (gemeten, met pieken tot ongeveer 36 ms). De drempels zijn:

1. Na 50 ms zonder rapport wordt `link` "stale": oriëntatie en aim zijn ongeldig, knoppen blijven nog vast.
2. Na 250 ms wordt `link` "lost": alle knoppen worden vrijgegeven, met up-flanken, en de trigger wordt 0.
3. Na 1,5 s sluit de HID-thread de handle (ook met vrijgave van alle knoppen) en zoekt hij elke seconde opnieuw naar een Move via Bluetooth.

De cursor springt nooit naar een hoek. Afnemers krijgen `aim.valid = false` en de aimvelden zijn dan `null`.

## Knopflanken

Elk HID-rapport bevat een volledige knopstatus en wordt afzonderlijk verwerkt. Elke wisseling tussen twee rapporten wordt dus een flank in dat sample (`pressed`, `released`). De gepubliceerde momentopname houdt per knop een cumulatieve teller van down- en up-flanken bij. Iedere afnemer (watch-client, later de outputbackend) onthoudt zijn eigen tellerstand, zodat ook een druk+los tussen twee leesmomenten en meerdere drukken zichtbaar blijven, ongeacht de leessnelheid. De bitposities volgen `enum PSMove_Button` van PSMoveAPI. Een druk die korter is dan één rapportinterval en volledig tussen twee rapporten valt, zit niet in de hardwaredata.

## Inputmodel (schema-versie 2)

`InputSample` (`src/ravengun/core/input_model.hpp`) heeft in JSON de volgende velden. Een nieuwe of gewijzigde betekenis verhoogt `schema_version`.

- `schema_version`, `seq` (per gepubliceerd sample, oplopend binnen een daemon-run), `t_rx_ns`, `t_eval_ns`, `report_dt_ms` en `hid_seq` (4-bitsvolgnummer uit het rapport).
- `controller_id`: het Bluetooth-adres (stabiel, anders dan `/dev/hidrawN`). Standaard geredigeerd tot `xx:xx:xx:xx:xx:ac`, volledig alleen met `full_ids`.
- `tracking_mode`: nu altijd "orientation". Gereserveerd zijn "optical_diagnostic", "hybrid" en "geometric_ray".
- `link`: "disconnected", "fresh", "stale" of "lost".
- `orientation`: `valid`, `q_wb`, `world_yaw_deg` en `world_pitch_deg` (zonder midden, alleen diagnose), `bias_state`, `still`, `gyro_bias_deg_s` en `yaw_reference`.
- `position`: de laatste optische meting (schema 2). `kind` is altijd "image_2d": een 2D-beeldpositie, geen 3D-positie. Velden: `valid`, `reason` ("none" als geldig, anders de reden van de tracker, of "no_camera", "no_frames", "camera_error"), `camera` (disabled, opening, streaming, error), `track_state` (searching, tracking), `frame_seq` (V4L2-volgnummer), `t_frame_ns` (V4L2-timestamp), `t_processed_ns`, `image_w`/`image_h`, `x_px`/`y_px` (zwaartepunt, pixelmiddens op +0,5), `x_norm`/`y_norm` (gedeeld door breedte/hoogte, links-boven 0,0), `radius_px`, `candidates`, en `depth` (altijd null: zonder gekalibreerd cameramodel is diepte onbekend). Is `valid` false, dan zijn de positievelden null. De positie wordt (nog) niet gebruikt voor de aim.
- `aim`: `valid`, `center_source`, `filter_enabled`, `yaw_rel_deg`, `pitch_rel_deg`, `x_raw`/`y_raw` (ongefilterd, unclamped), `x`/`y` (gefilterd, unclamped), `x_clamped`/`y_clamped` en `off_screen` (unclamped buiten [0,1]).
- `trigger` (0..255; bij de ZCM1 het gemiddelde van twee triggerbytes, zoals PSMoveAPI dat geeft) en `buttons` (`mask`, `down`, `pressed`, `released`).
- `source`: de laatste ruwe, gekalibreerde accelerometerwaarde (g) en gyrowaarde (graden/s, vóór de biascorrectie), om dubbele inversies of verkeerde eenheden te kunnen opsporen.

Er zijn bewust geen confidence-percentages. Geldigheid is per onderdeel een boolean met een gedefinieerde betekenis.

## IPC

De socket is `$XDG_RUNTIME_DIR/ravengun/ravengund.sock` (Unix stream). De map heeft rechten 0700, de socket 0600. Daarnaast weigert de daemon peers met een andere uid (SO_PEERCRED). Er kunnen maximaal 16 clients tegelijk verbonden zijn. Het protocol (`src/ravengun/ipc_protocol.hpp`) gebruikt één JSON-object per regel, maximaal 64 KiB.

Een verzoek heeft de vorm `{"v":1,"cmd":"<commando>","id":<optioneel getal of string>, ...}`. Onbekende velden, verkeerde types en waarden buiten het bereik worden geweigerd. Er bestaat geen commando dat paden of shellopdrachten aanneemt. De commando's zijn:

- `status` (optioneel `full_ids`): de daemon, de controller (inclusief batterij en LED), de rapportstatistiek, de verwerkingstijd, de configuratie, het laatste sample en onder `camera` hetzelfde object als het commando `camera`.
- `camera`: de camerastatus: identiteit (by-id, by-path, node, driver, bus), modus, betekenis van de timestamps, frames (ontvangen, fps totaal en over 5 s, gemist volgens sequence, fout- en korte frames), verdelingen van interval, dequeue-vertraging, leeftijd bij verwerking en verwerkingstijd, de wachtrijtellers, alle V4L2-controls (waarde vóór en na, wat ravengund zette), de trackerconfiguratie en -statistiek (geldig, ROI of volledig beeld, redenen, straalverdeling, laatste meting) en scènestatistiek (alleen getallen).
- `sample`: het laatste sample.
- `recenter`: legt het midden vast. Zonder actuele oriëntatie geeft het de fout `unavailable`.
- `watch` (`rate_hz` 1..250, standaard 30): daarna stuurt de daemon `{"v":1,"event":"sample","data":{"sample":…,"edges_since_last_frame":{"pressed":{…},"released":{…}},"skipped_frames":n}}`, alleen als er een nieuw sample of een nieuwe optische meting is (ook zonder verbonden Move).
- `set_filter` (`enabled`, `min_cutoff_hz`, `beta`, `d_cutoff_hz`) en `set_aim` (`h_edge_deg`, `v_edge_deg`): deze gelden alleen voor de lopende run, want persistente configuratie volgt later.
- Uitvoer (zie "Uitvoer"): `arm`, `disarm`, `output` (alleen status), `test_pattern` (`pattern` center|corners|grid|sweep|point|stop, optioneel `dwell_ms`, `duration_s`, `margin`, `x`, `y`, `rate_hz`), `output_button` (`button` trigger|reload|grenade|aux1|aux2, `action` down|up|tap) en `set_output` (`invert_x`, `invert_y`, `scale_x/y`, `offset_x/y`, `viewport_x0/y0/x1/y1` of `screen_aspect` plus `content_aspect`). `status` bevat daarnaast de sleutel `output`. Zonder `arm` weigeren `test_pattern` en `output_button` met `unavailable`.

Een antwoord is `{"v":1,"ok":true,"id":…,"result":…}` of `{"v":1,"ok":false,"id":…,"error":{"code":…,"message":…}}`. De foutcodes zijn `parse_error`, `bad_request`, `unsupported_version`, `unknown_command`, `invalid_argument`, `line_too_long`, `unavailable` en `internal`.

## Uitvoer (virtueel lightgun-apparaat, fase 2A/2H)

Statuslabels: de backend, de transformatie en arm/disarm zijn geïmplementeerd en automatisch getest (`tests/test_output.cpp`, 188 controles, en `tests/ipc_smoke.sh`). Het apparaat is op echte Halo/TeknoParrot gebonden en gemeten (7 okt 2026, 1920x1080 op de tv). Wat niet getest is: de live Move als bron (de backend is nog niet aan de aim gekoppeld), spelen in Gaming Mode en een reboot.

### Wat TeknoParrot als lightgun ziet (uit de broncode, branch net8-migration)

Alle bestandsnamen zijn onder `TeknoParrotUi.Common/InputListening/` (tenzij anders gezegd). Op Linux leest de UI zelf evdev; er is geen RawInput.

1. Opsomming: `Mouse/EvdevInterop.cs:199` (`EnumerateMice`) en `:206` (`EnumerateDevices`) lezen `/proc/bus/input/devices` en nemen elk apparaat waarvan de regel `H: Handlers=` het woord `mouse` bevat (`:223`). Er wordt dus niet naar capabilities gekeken, alleen naar de kernel-handler. Die `mouseN`-handler (kernel-mousedev) krijgt een apparaat met `BTN_LEFT` plus `ABS_X`/`ABS_Y` (absolute muis), of `BTN_LEFT` plus `REL_X`/`REL_Y`. Zonder `BTN_LEFT` (de oude vinput-variant met joystick-knoppen) krijgt het apparaat alleen een `jsN`-handler en is het voor TeknoParrot geen muis: dat is waarom "P1 Light Gun" de virtuele gun niet aanbood.
2. Leesbaarheid: `Mouse/EvdevInterop.cs:137` (`CheckAccess`, een `open(O_RDONLY)`) en `Avalonia/Services/RawInputCaptureService.cs:335-345` (`GetMouseDeviceList` filtert op leesbaar). Een onleesbaar apparaat staat niet in de lijst. `InputListenersManager.cs:71-83` valt dan terug op X11 (één muis, geen lightgun). De udev-regel (uaccess) lost de leesbaarheid op; het `VINPUT_JOY`-trucje is niet meer nodig.
3. Naam en pad in de binding: `Avalonia/Services/RawInputCaptureService.cs:355-361` (`GetMouseDevicePathByName`) geeft het `DevicePath` van het eerste apparaat met die naam. `EvdevInterop.cs:293` (`BuildStablePathMap`) kent alleen symlinks onder `/dev/input/by-id`; zonder zo'n link is het pad `/dev/input/eventN`. Een uinput-apparaat heeft `ID_SERIAL=noserial`, dus udev maakt zelf geen by-id-link. De RavenGun-udev-regel maakt `/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse`, en dat pad staat nu in `Halo.xml`/`Halo.json`.
4. Doorgeven aan de game: de UI-listener (`Mouse/EvdevMouseListener.cs`) opent het apparaat (`:263` `DeviceLoop`), leest bij `EV_ABS` ABS_X/ABS_Y en normaliseert met de min/max uit `EVIOCGABS` (`EvdevInterop.cs:90-93`, gebruik in `EvdevMouseListener.cs:300-305`: `(waarde - min) / (max - min)`). Dat gaat naar `UpdateGunPosition` (`:319`) en `Mouse/GunAnalogMath.cs:52` (`ToGameUnits`: `min + factor * (max - min)`, afgerond; Halo: `xAxisMin/Max` en `yAxisMin/Max` 0..255, 8 bit) en `:70` (`Write`). Met `InvertedMouseAxis=true` (Halo-profiel) staan x en y ongewijzigd in de analoge bytes (`GunAnalogMath.cs:100-103`); zonder die vlag wordt `~x` en `~y` geschreven. De listener kent geen venster of letterbox: de factor 0..1 is de volledige apparaatrange. Een apparaat zonder `ABS_X`-bereik (`maximum <= minimum`) wordt als relatieve muis behandeld (`:274-275`; REL_X/REL_Y bewegen dan een positie op een canvas van 1920x1080).
5. Knoppen: `EvdevMouseListener.cs:335-353` (`HandleButtonEvent`). `BTN_LEFT` en `BTN_TOUCH` zijn linkermuisknop, `BTN_RIGHT` rechts, `BTN_MIDDLE` midden, `BTN_SIDE` en `BTN_EXTRA` knop 4 en 5. Een binding in `Halo.xml` heeft `DevicePath` plus `MouseButton`; een ongebonden muis krijgt een standaardmapping links=Button1, rechts=Button2, midden=Button3.
6. Spelerstoewijzing: `EvdevMouseListener.cs:249` (`PlayerForBoundDevice`) en `:126`: het apparaat waarvan `DevicePath` gelijk is aan het pad van de `P1 Light Gun`-binding wordt speler 1. Overige muizen vullen de resterende spelers (max. 4 in totaal) op volgorde.
7. Het apparaat wordt bij de start van de game geopend en nooit opnieuw (`DeviceLoop` leest tot het einde van de sessie, zonder heropenen). Zie "Stabiliteit".

### Apparaatidentiteit

Naam `RavenGun Virtual Lightgun`, bus USB, VID 1209 (pid.codes-testbereik), PID 7202, versie 1. Het toetsenbord is 1209:7201. `EV_ABS` `ABS_X`/`ABS_Y` met bereik 0..32767 op beide assen (`kAbsMin`, `kAbsMax` in `output/gun_transform.hpp`); `EV_KEY` `BTN_LEFT` (trigger), `BTN_RIGHT` (reload), `BTN_MIDDLE` (grenade), `BTN_SIDE` en `BTN_EXTRA` (aux). Geen `EV_REL`, en bewust geen `INPUT_PROP_DIRECT` (dat markeert een touchscreen) en geen `BTN_TOUCH` (dan gedraagt het apparaat zich als tablet). Udev (`packaging/udev/70-ravengun.rules`) geeft de seat-gebruiker leesrecht (uaccess), maakt `/dev/input/ravengun-gun` en `/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse`, en zet `LIBINPUT_IGNORE_DEVICE=1` op de gun. De keuze berust op: de udev-regel selecteert op VID/PID, de TeknoParrot-binding op het by-id-pad, en nergens op `eventN`.

### Arm/disarm en veiligheid

De daemon start disarmed: er gaan geen events naar het apparaat en `test_pattern`/`output_button` weigeren. `arm` maakt het apparaat aan (bij de eerste keer) en schakelt events in. `disarm` geeft eerst alle knoppen vrij (up-events) en schakelt dan events uit; het apparaat blijft bestaan zodat een binding en een lopende game niet breken. Alle knoppen worden ook vrijgegeven bij een schrijffout (het apparaat wordt dan verwijderd en de uitvoer disarmed; een nieuwe `arm` maakt het opnieuw aan), bij `release_all` (bronuitval, later de Move) en bij afsluiten (SIGTERM/SIGINT: eerst up-events, dan het apparaat verwijderen). Na een crash (SIGKILL) verwijdert de kernel het apparaat en geeft daarbij de knoppen vrij (getest). De daemon opent zijn eigen apparaat nooit voor lezen; er is in de code geen leesfunctie voor het uitvoerapparaat, dus geen feedbackloop. Het apparaat bestaat pas na de eerste `arm` en blijft tot het afsluiten van de daemon.

### Coördinaatruimtes en transformatie

Aim-ruimte: genormaliseerd fysiek scherm, (0,0) linksboven, y omlaag, ongeclampt en ongespiegeld (zo komt de aim uit de pijplijn). `output/gun_transform.cpp` doet daarna, in deze volgorde en precies één keer: viewport-normalisatie (letterbox/pillarbox, `letterbox_viewport(scherm_aspect, content_aspect)`; standaard het hele scherm), inversie per as, schaal rond het midden met offset (kalibratiecorrectie, standaard 1 en 0), clamp naar [0,1] en afronden op `abs_max`. Niet-eindige invoer geeft geen event. Verder in de keten gebeurt niets meer: uinput en TeknoParrot schalen alleen lineair naar de game-bytes (zie hierboven). De `GunTransform` staat in de daemon (`--out-*` opties) en is live te wijzigen met `set_output` (alleen voor de lopende run).

### Meting in Halo (G1-omgeving, 1920x1080, backend zonder correctie)

Gemeten met `ravengunctl test-pattern point X Y` (aim = het gegeven punt), een screenshot, en een eigen meetscript dat de groene ring met kruis van het spelvizier zoekt (volledige ring: midden van de bounding box; bij een deels buiten beeld staand vizier een cirkelfit met vaste straal 55 px). Halo toont het vizier op de volledige 16:9-tv zonder letterbox, met `byte = round(255·u)` en `pixel = byte·1920/255` (x) of `byte·1080/255` (y). Dat verklaart alle metingen: een lineaire afbeelding zonder inversie, offset of schaalfout, met een raster van 7,5 px (0,39 % van de breedte) door de 8-bits as van het spel. De spelkalibratie (scherm "Pull trigger when ready to calibrate") is overgeslagen met Test; een uitgevoerde spelkalibratie zou dit veranderen.

Fout (gemeten min verwacht, px; x/y; % van de breedte 1920 / hoogte 1080):

| aim | verwacht | gemeten | fout px | fout % |
|---|---|---|---|---|
| (0,5; 0,5) | 960; 540 | 963,5; 542,0 | +3,5; +2,0 | 0,18 / 0,19 |
| (0,1; 0,1) | 192; 108 | 195,5; 110,0 | +3,5; +2,0 | 0,18 / 0,19 |
| (0,5; 0,1) | 960; 108 | 963,5; 110,0 | +3,5; +2,0 | 0,18 / 0,19 |
| (0,9; 0,1) | 1728; 108 | 1723,5; 110,0 | -4,5; +2,0 | 0,23 / 0,19 |
| (0,1; 0,5) | 192; 540 | 195,5; 542,0 | +3,5; +2,0 | 0,18 / 0,19 |
| (0,9; 0,5) | 1728; 540 | 1723,5; 542,0 | -4,5; +2,0 | 0,23 / 0,19 |
| (0,1; 0,9) | 192; 972 | 195,0; 969,5 | +3,0; -2,5 | 0,16 / 0,23 |
| (0,5; 0,9) | 960; 972 | 963,0; 969,0 | +3,0; -3,0 | 0,16 / 0,28 |
| (0,9; 0,9) | 1728; 972 | 1723,5; 969,5 | -4,5; -2,5 | 0,23 / 0,23 |
| hoek (0;0) | 0; 0 | 1,2; 1,9 | +1,2; +1,9 | 0,06 / 0,18 |
| hoek (1;0) | 1920; 0 | 1917,6; 1,9 | -2,4; +1,9 | 0,13 / 0,18 |
| hoek (1;1) | 1920; 1080 | 1917,6; 1077,2 | -2,4; -2,8 | 0,13 / 0,26 |
| hoek (0;1) | 0; 1080 | 1,2; 1077,3 | +1,2; -2,7 | 0,06 / 0,25 |

De hoeken zijn schattingen met een cirkelfit (ongeveer 2 px onzekerheid). De uitkomst is hetzelfde in drie reeksen (twee sessies, waarvan een met een ander eventN). Grootste fout 4,5 px = 0,23 % van de breedte, ruim binnen de eis van 1 %, en gelijk aan de kwantisatie van het spel. Er was dus geen correctie nodig: de standaardtransformatie (geen inversie, viewport = scherm, schaal 1, offset 0) is de juiste. De correctie-instrumenten (`invert_*`, `scale_*`, `offset_*`, viewport) bestaan voor een ander spel, een andere tv-verhouding of een uitgevoerde spelkalibratie.

Sweep (20 s, 100 stappen/s: horizontaal, verticaal, cirkel): het vizier volgt het pad in alle legs zonder sprongen of stilstand. Met de bestandstijd van het screenshot als klok en de beste vaste meetvertraging (260 ms, een eigenschap van de screenshottool) is de afwijking rms 10,7 px (0,56 %) en max. 35 px (1,8 %, tijdruis van ±50 ms bij 400 px/s). Een filmopname (spectacle -R) was niet beschikbaar zonder interactieve portal-toestemming; de beeldsnelheid van het vizier is dus niet frame voor frame gemeten (schermfoto's ongeveer eens per 0,9 s).

Trigger: `output_button trigger tap` op de kaart "Mission Select" van het modemenu selecteert die kaart. De knoppen zijn gebonden als Trigger P1 = `LeftButton`, Reload P1 = `RightButton` en Grenade P1 = `MiddleButton` op het by-id-pad van de gun (de toetsenbord-bindingen zijn naar het by-id-pad van het virtuele toetsenbord verplaatst).

### Desktopcursor

KWin (libinput) beschouwt een absolute muis met `BTN_LEFT` en `ABS_X`/`ABS_Y` als pointer en beweegt de desktopcursor. Controle-experiment: een identiek apparaat met een ander PID (7299, buiten de udev-regel) staat als `/org/kde/KWin/InputDevice/event258` in KWin en verplaatste de cursor van (1794,900) naar o.a. (703,396); de RavenGun-gun ontbreekt in die lijst en de cursor bleef 120 metingen (7 s sweep plus trigger) op (1794,900). Dat komt door `LIBINPUT_IGNORE_DEVICE=1` in de udev-regel. TeknoParrot leest `/dev/input/eventN` rechtstreeks en merkt dat niet. Het geldt ook als de game niet draait, omdat KWin het apparaat nooit opent. Meetmethode: een tijdelijk KWin-QML-script (`Workspace.cursorPos`, loggen elke 100 ms), daarna weer uitgeladen. Het virtuele toetsenbord (7201) heeft deze regel niet: toetsen als `5`, `1` en F2 gaan ook naar het venster dat focus heeft. Voorstel: ook daar `LIBINPUT_IGNORE_DEVICE=1`, maar dat is niet toegepast omdat de lopende kbd-sessie van de orchestrator er dan pas na herstarten door verandert.

### Stabiliteit

De binding in TeknoParrot gebruikt het by-id-pad, niet `eventN`. Getest: de gun kreeg na een daemon-herstart een ander nummer (event30 eerst, daarna event258 doordat een ander apparaat event30 bezette) en Halo volgde de gun zonder herbinden; de metingen zijn identiek. De daemon is meerdere keren herstart (SIGTERM en SIGKILL) en Halo is drie keer opnieuw gestart.

Beperking (uit de broncode en bevestigd): TeknoParrot opent het apparaat bij de start van de game en heropent het niet. Herstart je de daemon terwijl Halo draait, dan wijst de open handle van de UI naar het verwijderde apparaat (`/proc/<pid>/fd`: `event258 (deleted)`) en volgt de game het nieuwe apparaat niet. Volgorde dus: eerst de daemon met `arm`, dan pas de game (virtueel toetsenbord en gun moeten vóór `LAUNCH GAME` bestaan); en de daemon blijft actief zolang de game draait.

Structurele oplossing (niet gebouwd, vraagt een beslissing): het apparaat moet een daemon-herstart overleven. Systemd-user kan de `/dev/uinput`-filedescriptor bewaren (`FileDescriptorStoreMax=`, `sd_pid_notify_with_fds`) en bij het herstarten teruggeven, waarmee het uinput-apparaat blijft bestaan zolang de service-eenheid bestaat. Alternatief: een minimaal apart "apparaathouder"-proces dat het uinput-apparaat bezit en de events via een socket van de daemon krijgt. Beide horen bij fase 3 (startvolgorde en service). Zonder dat: elk apparaat zo vroeg en in vaste volgorde aanmaken (toetsenbord, dan gun) vóór de game start.

Statuslabels in deze sectie: geïmplementeerd, automatisch getest (CTest, synthetische frames) en op echte hardware getest (PS3 Eye en Move ZCM1 op deze host, 7 okt 2026). Wat bij de hardwaretest niet gevarieerd is (speelafstand, beweging, hand), staat als "nog te bevestigen".

### Acquisitie

Geïmplementeerd en op echte hardware getest. De camera wordt gekozen op stabiele identiteit: met `--camera PAD` (bijvoorbeeld een `/dev/v4l/by-path/...`-link), of automatisch de enige camera met driver `ov534` onder `/dev/v4l/by-id`. Bij nul of meer dan één zo'n camera start de camera niet en staat de reden in de status. `/dev/video0` wordt nergens aangenomen. Het formaat is YUYV (de chroma komt kant-en-klaar van de camera, zonder eigen debayering). De modus is instelbaar met `--camera-mode BxH@FPS`, standaard 640x480@60.

De capture-thread gebruikt V4L2-mmap met 4 driverbuffers. Bij elk `poll()`-signaal haalt hij alle klaarstaande buffers op (DQBUF tot EAGAIN), noteert timestamp, volgnummer en ontvangsttijd, kopieert de inhoud naar een framebuffer in RAM en geeft de driverbuffer direct terug. Zo stapelen de driverbuffers niet op. Het frame gaat in een begrensde wachtrij met capaciteit 2. Is die vol, dan valt het oudste weg (`dropped_full`). De tracker-thread neemt altijd het nieuwste frame en gooit oudere weg (`dropped_stale`). Framebuffers worden hergebruikt. Er is één eigenaar van de camera (de daemon met zijn lockfile; een tweede streamer krijgt EBUSY, en dat staat dan als fout in de status). Valt de stream weg (fout, 2 s geen frame), dan sluit de capture-thread het apparaat en probeert hij het na 1 tot 2 s opnieuw.

Beelden blijven in RAM en worden nergens opgeslagen. Er is geen opname- of previewfunctie. De status bevat alleen getallen.

### Tijd

`t_frame_ns` is de V4L2-buffertimestamp. Op deze camera is het type monotonic (dezelfde klok als `t_rx_ns` van de Move) met bron EOF: het moment waarop het frame volledig binnen was, niet het moment van belichting. Is de timestamp niet monotonic, dan valt de daemon terug op de ontvangsttijd (zichtbaar in de status onder `timestamps`). De status meet per frame het interval tussen timestamps, de gaten in het V4L2-volgnummer (gemiste frames), de dequeue-vertraging (ontvangst min timestamp), de leeftijd bij de start van de verwerking (start min timestamp) en de verwerkingstijd. De belichtings- en overdrachtstijd vóór EOF zit in geen van die getallen.

### Controls

Geïmplementeerd en op echte hardware getest. Bij het openen leest de daemon alle V4L2-controls. Standaard zet hij `auto_exposure` op handmatig (1), `gain_automatic` op 0, `white_balance_automatic` op 0, `exposure` op 50 en `gain` op 0. Na STREAMON leest hij alles opnieuw. In de status staan per control de waarde vóór en na, en wat ravengund zette. Met `--camera-auto` zet de daemon de automatiek juist expliciet aan (de driver onthoudt eerdere handmatige waarden). Een aparte witbalans per kanaal biedt de ov534-driver niet. Bij uitgeschakelde AWB blijft de huidige toestand van de sensor staan. De keuze van de waarden staat in `docs/decisions.md`.

### LED

Geïmplementeerd en op echte hardware getest. De HID-thread stuurt de LED-kleur (standaard cyaan, `--led`) elke 1000 ms opnieuw met `psmove_set_leds` en `psmove_update_leds`. Zonder herhaling dooft de Move de LED na ongeveer 4 tot 5 s. Bij het stoppen zet de daemon de LED uit. De status toont onder `controller.led` de kleur en het aantal geslaagde en mislukte updates.

### Tracker

Geïmplementeerd en automatisch getest. Op echte hardware getest met een stilliggende Move. Het algoritme staat in `src/ravengun/vision/ball_tracker.hpp`:

1. Een pixel telt mee als kleurpixel als zijn chroma (U−128, V−128) binnen de tint-sector valt (standaard −75 ± 30 graden voor cyaan, de gemeten tint), met chroma-sterkte ≥ 28 en Y ≥ 40. Hij telt mee als verzadigde pixel als Y ≥ 245 (een overbelichte kern is wit). De sector staat in een opzoektabel van 64 KiB.
2. Connected components (8-buren, union-find) in een ROI rond de voorspelde positie (vorige positie plus snelheid). Raakt een component de rand van de ROI, of staat er geen geschikte kandidaat binnen de poort, dan wordt in hetzelfde frame het volledige beeld doorzocht.
3. Per component: oppervlak, aandeel kleurpixels, zwaartepunt, tweede momenten, straal (gelijk oppervlak), langgerektheid (√(λ1/λ2) ≤ 1,8) en vulling (0,70 tot 1,35; een schijf is 1). Verworpen worden: te weinig kleur (< 25 %, bijvoorbeeld een witte lamp), te klein (< 12 px), te groot (straal > ¼ beeldhoogte) en niet rond.
4. Keuze: tijdens het volgen de geschikte kandidaat het dichtst bij de voorspelling, binnen een poort van max(24 px × breedte/640, 4 × straal) plus de snelheid. Zonder voorgeschiedenis wordt nooit de grootste gekozen: zijn er meerdere geschikte kandidaten, dan is de meting ongeldig (`multiple_candidates`).
5. Uitbijters: een sprong buiten de poort of een straalverandering van meer dan een factor 1,8 wordt pas geldig als het volgende frame hem bevestigt (`jump_unconfirmed`). Een nieuwe acquisitie vraagt ook een tweede frame (`acquire_pending`). Na 8 frames zonder geaccepteerde meting wordt de toestand `searching`.

Redenen van ongeldigheid: `no_candidate`, `too_small`, `too_large`, `not_round`, `too_little_color`, `multiple_candidates`, `jump_unconfirmed`, `acquire_pending` en `bad_frame`. Er is geen confidence-getal. Bekende beperkingen: bij gedeeltelijke afdekking verschuift het zwaartepunt naar het zichtbare deel, en tot ongeveer een kwart afdekking blijft de meting geldig (getest). Een bol die minder dan ongeveer 12 px beslaat, wordt verworpen. Een reflectie die rond en even helder is als de bol, kan bij de acquisitie `multiple_candidates` geven.

## Batterij

PSMoveAPI geeft zes niveaus (`Batt_MIN` minder dan 20 %, en daarna ten minste 20, 40, 60 en 80 % en `Batt_MAX`) plus "laadt" en "vol aan de lader". RavenGun toont precies die niveaus en geen fijnere percentages.

## Bekende beperkingen

- PSMoveAPI schrijft bij het verbinden zelf een waarschuwing met het volledige Bluetooth-adres naar stderr ("Magnetometer in … not yet calibrated"). Die tekst valt buiten de redactie van RavenGun. Logs van `ravengund` die gedeeld worden, moeten daarom gefilterd worden (nog te regelen bij logging naar bestand).
- De ontvangsttijd is geen sensortijd (zie Tijdbases).
- De optische positie is een 2D-beeldpositie. Diepte, een cameramodel (intrinsieken, lensvervorming) en de koppeling met het schermvlak ontbreken nog.
- Het 4-bitsvolgnummer meldt regelmatig een overgeslagen nummer (`dropped_by_seq`). Of dat werkelijk verloren rapporten zijn of een eigenschap van de firmware, is nog te bevestigen. De rapportfrequentie zelf wordt los daarvan gemeten.
