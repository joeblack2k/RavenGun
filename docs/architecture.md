# Architectuur van ravengund (fase 2B/2F-kern)

Dit document beschrijft wat nu gebouwd is en welke afspraken vastliggen. Statuslabels: "geïmplementeerd" (code bestaat), "automatisch getest" (CTest), "op echte hardware getest" (op deze host met de Move), "nog te bevestigen".

## Processen en eigendom van hardware

Er is één daemon, `ravengund`, en die is de enige eigenaar van de live hardware. Nu is dat alleen de Move (via PSMoveAPI). Later komen daar de camera (V4L2) en het virtuele uitvoerapparaat (uinput) bij. Clients (`ravengunctl`, later de webinterface en de kalibratieclient) praten alleen via de socket met de daemon en openen zelf nooit hardware. Ze mogen op elk moment stoppen zonder dat de verwerking stopt.

`ravengund` draait op de voorgrond (bedoeld voor een terminal of later een systemd-userdienst). Eén instantie per gebruiker wordt afgedwongen met `flock` op `$XDG_RUNTIME_DIR/ravengun/ravengund.lock`. Het slot vervalt vanzelf als het proces stopt, ook na een crash. Een tweede instantie stopt met exitcode 69. SIGINT en SIGTERM worden in de hoofdthread met `sigwait` afgehandeld: de threads stoppen, de Move wordt gesloten nadat alle knoppen zijn vrijgegeven, en de socket wordt verwijderd.

Geïmplementeerd en automatisch getest (`tests/ipc_smoke.sh`). Op echte hardware getest: starten, verbinden met de Move en stoppen.

## Threads

1. De hoofdthread doet de opstart, het slot en de signalen.
2. De HID-thread (`src/ravengund/hid_thread.cpp`) is de enige code die PSMoveAPI aanroept. Ze bezit de Move-handle en de verwerkingspijplijn. Ze leest elk beschikbaar rapport (`psmove_poll` tot er niets meer is, max. 64 per ronde) en verwerkt elk rapport afzonderlijk. Als er niets nieuws is, slaapt ze 1 ms. PSMoveAPI geeft geen file descriptor om op te wachten. Daardoor komt er 0 tot ongeveer 1 ms wachttijd bovenop de Bluetooth-latentie.
3. De IPC-thread (`src/ravengund/ipc_server.cpp`) draait een `poll()`-lus over de luistersocket en alle clients, met niet-blokkerende sockets. Opdrachten die de pijplijn veranderen (recenter, filter, aim-schaal) of die statistiek nodig hebben (status) gaan via een wachtrij naar de HID-thread, met een time-out van 500 ms. Zo heeft de pijplijn één eigenaar en zijn er geen locks in de rekenroute. Live samples leest de IPC-thread uit een momentopname die de HID-thread na elk sample onder een korte mutex publiceert.

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
- `json_parse.*`, `ipc_protocol.*` en `ipc_client.*`: het beheerprotocol.

De daemon voegt daar alleen de hardware-, thread- en socketcode aan toe. Tests voeren synthetische rapporten rechtstreeks aan de pijplijn.

Nog niet gebouwd (latere fasen): de camera-acquisitie en boltracking, kalibratie en persistente configuratie, de outputbackend (uinput), Bluetooth-onboarding in de daemon en de webinterface.

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

## Inputmodel (schema-versie 1)

`InputSample` (`src/ravengun/core/input_model.hpp`) heeft in JSON de volgende velden. Een nieuwe of gewijzigde betekenis verhoogt `schema_version`.

- `schema_version`, `seq` (per gepubliceerd sample, oplopend binnen een daemon-run), `t_rx_ns`, `t_eval_ns`, `report_dt_ms` en `hid_seq` (4-bitsvolgnummer uit het rapport).
- `controller_id`: het Bluetooth-adres (stabiel, anders dan `/dev/hidrawN`). Standaard geredigeerd tot `xx:xx:xx:xx:xx:ac`, volledig alleen met `full_ids`.
- `tracking_mode`: nu altijd "orientation". Gereserveerd zijn "optical_diagnostic", "hybrid" en "geometric_ray".
- `link`: "disconnected", "fresh", "stale" of "lost".
- `orientation`: `valid`, `q_wb`, `world_yaw_deg` en `world_pitch_deg` (zonder midden, alleen diagnose), `bias_state`, `still`, `gyro_bias_deg_s` en `yaw_reference`.
- `position`: altijd `valid: false` (nog geen camera).
- `aim`: `valid`, `center_source`, `filter_enabled`, `yaw_rel_deg`, `pitch_rel_deg`, `x_raw`/`y_raw` (ongefilterd, unclamped), `x`/`y` (gefilterd, unclamped), `x_clamped`/`y_clamped` en `off_screen` (unclamped buiten [0,1]).
- `trigger` (0..255; bij de ZCM1 het gemiddelde van twee triggerbytes, zoals PSMoveAPI dat geeft) en `buttons` (`mask`, `down`, `pressed`, `released`).
- `source`: de laatste ruwe, gekalibreerde accelerometerwaarde (g) en gyrowaarde (graden/s, vóór de biascorrectie), om dubbele inversies of verkeerde eenheden te kunnen opsporen.

Er zijn bewust geen confidence-percentages. Geldigheid is per onderdeel een boolean met een gedefinieerde betekenis.

## IPC

De socket is `$XDG_RUNTIME_DIR/ravengun/ravengund.sock` (Unix stream). De map heeft rechten 0700, de socket 0600. Daarnaast weigert de daemon peers met een andere uid (SO_PEERCRED). Er kunnen maximaal 16 clients tegelijk verbonden zijn. Het protocol (`src/ravengun/ipc_protocol.hpp`) gebruikt één JSON-object per regel, maximaal 64 KiB.

Een verzoek heeft de vorm `{"v":1,"cmd":"<commando>","id":<optioneel getal of string>, ...}`. Onbekende velden, verkeerde types en waarden buiten het bereik worden geweigerd. Er bestaat geen commando dat paden of shellopdrachten aanneemt. De commando's zijn:

- `status` (optioneel `full_ids`): de daemon, de controller (inclusief batterij), de rapportstatistiek, de verwerkingstijd, de configuratie en het laatste sample.
- `sample`: het laatste sample.
- `recenter`: legt het midden vast. Zonder actuele oriëntatie geeft het de fout `unavailable`.
- `watch` (`rate_hz` 1..250, standaard 30): daarna stuurt de daemon `{"v":1,"event":"sample","data":{"sample":…,"edges_since_last_frame":{"pressed":{…},"released":{…}},"skipped_frames":n}}`, alleen als er een nieuw sample is.
- `set_filter` (`enabled`, `min_cutoff_hz`, `beta`, `d_cutoff_hz`) en `set_aim` (`h_edge_deg`, `v_edge_deg`): deze gelden alleen voor de lopende run, want persistente configuratie volgt later.

Een antwoord is `{"v":1,"ok":true,"id":…,"result":…}` of `{"v":1,"ok":false,"id":…,"error":{"code":…,"message":…}}`. De foutcodes zijn `parse_error`, `bad_request`, `unsupported_version`, `unknown_command`, `invalid_argument`, `line_too_long`, `unavailable` en `internal`.

## Batterij

PSMoveAPI geeft zes niveaus (`Batt_MIN` minder dan 20 %, en daarna ten minste 20, 40, 60 en 80 % en `Batt_MAX`) plus "laadt" en "vol aan de lader". RavenGun toont precies die niveaus en geen fijnere percentages.

## Bekende beperkingen

- PSMoveAPI schrijft bij het verbinden zelf een waarschuwing met het volledige Bluetooth-adres naar stderr ("Magnetometer in … not yet calibrated"). Die tekst valt buiten de redactie van RavenGun. Logs van `ravengund` die gedeeld worden, moeten daarom gefilterd worden (nog te regelen bij logging naar bestand).
- De ontvangsttijd is geen sensortijd (zie Tijdbases).
- Het 4-bitsvolgnummer meldt regelmatig een overgeslagen nummer (`dropped_by_seq`). Of dat werkelijk verloren rapporten zijn of een eigenschap van de firmware, is nog te bevestigen. De rapportfrequentie zelf wordt los daarvan gemeten.
