# Architectuur van ravengund (fase 2B/2F-kern, 2D-camera)

Dit document beschrijft wat nu gebouwd is en welke afspraken vastliggen. Statuslabels: "geïmplementeerd" (code bestaat), "automatisch getest" (CTest), "op echte hardware getest" (op deze host met de Move), "nog te bevestigen".

## Processen en eigendom van hardware

Er is één daemon, `ravengund`, en die is de enige eigenaar van de live hardware: de Move (via PSMoveAPI) en de camera (V4L2). Ook het virtuele uitvoerapparaat (uinput-lightgun, zie "Uitvoer") is van de daemon. Clients (`ravengunctl`, later de webinterface en de kalibratieclient) praten alleen via de socket met de daemon en openen zelf nooit hardware. Ze mogen op elk moment stoppen zonder dat de verwerking stopt.

`ravengund` draait op de voorgrond (bedoeld voor een terminal of later een systemd-userdienst). Eén instantie per gebruiker wordt afgedwongen met `flock` op `$XDG_RUNTIME_DIR/ravengun/ravengund.lock`. Het slot vervalt vanzelf als het proces stopt, ook na een crash. Een tweede instantie stopt met exitcode 69. SIGINT en SIGTERM worden in de hoofdthread met `sigwait` afgehandeld: de threads stoppen, de Move wordt gesloten nadat alle knoppen zijn vrijgegeven, en de socket wordt verwijderd.

Geïmplementeerd en automatisch getest (`tests/ipc_smoke.sh`). Op echte hardware getest: starten, verbinden met de Move en stoppen.

## Threads

1. De hoofdthread doet de opstart, het slot en de signalen.
2. De HID-thread (`src/ravengund/hid_thread.cpp`) is de enige code die PSMoveAPI aanroept. Ze bezit de Move-handle en de verwerkingspijplijn. Ze leest elk beschikbaar rapport (`psmove_poll` tot er niets meer is, max. 64 per ronde) en verwerkt elk rapport afzonderlijk. Daarnaast opent ze de hidraw-node van dezelfde ZCM1 een tweede keer, alleen-lezend (de "hidraw-tap", `src/ravengund/hidraw_tap.*`). Die levert per rapport de interne tijdstempel, de temperatuur en de ruwe sensorwaarden, en een descriptor om op te wachten: als er niets nieuws is, wacht de thread met `poll()` (maximaal 2 ms) en wordt hij direct wakker bij een nieuw rapport. Zonder tap slaapt ze, zoals eerder, 1 ms. De fase 0/1/3-uitbreidingen (tap, recorder, magnetometer) staan in `src/ravengund/hid_aux.*`.
3. De IPC-thread (`src/ravengund/ipc_server.cpp`) draait een `poll()`-lus over de luistersocket en alle clients, met niet-blokkerende sockets. Opdrachten die de pijplijn veranderen (recenter, filter, aim-schaal) of die statistiek nodig hebben (status) gaan via een wachtrij naar de HID-thread, met een time-out van 500 ms. Zo heeft de pijplijn één eigenaar en zijn er geen locks in de rekenroute. Live samples leest de IPC-thread uit een momentopname die de HID-thread na elk sample onder een korte mutex publiceert.

4. De capture-thread (`src/ravengund/camera.cpp`) is de enige code die de camera opent (zie "Camera en boltracking").
5. De tracker-thread verwerkt het nieuwste cameraframe en publiceert de optische meting. Camera en tracker staan los van de HID-thread: een trage of haperende camera vertraagt de IMU-verwerking niet.

Een trage client kan de verwerking niet ophouden. Staat er bij een watch-client nog meer dan 16 KiB te wachten, dan wordt dat frame overgeslagen (en geteld). Boven 256 KiB wordt de client verbroken.

## Modules

De library `libravengun` bevat alle logica zonder hardware:

- `core/math.hpp`: vectoren en quaternions.
- `core/fusion.*`: de gyro-biasschatter (stilstand, bewaarde beginwaarde, quasi-stille vensters, temperatuurcompensatie) en het oriëntatiefilter.
- `core/bias_store.*`: het bewaarde bias-bestand per controller en het bewaarbeleid (`BiasSaver`, `BiasPersistence`).
- `core/aim.*`: de voorlopige aim-mapping en recenter.
- `core/one_euro.hpp`: het 1€-filter.
- `core/buttons.*`: knopstatus en -flanken, de virtuele Sharp Shooter-knoppen en de afgeleide pomp (`SharpShooterPump`).
- `core/pipeline.*`: koppelt dit alles. Rapport in, `InputSample` uit, plus staleness en statistiek.
- `core/move_report.*` en `core/move_clock.*`: parsing van het ruwe ZCM1-rapport, het ontrollen van de Move-stempel en de klokkoppeling.
- `core/magnetometer.*`: ellipsoïdefit, dekking, opslag van de kalibratie en de verstoringsdetector.
- `rec/recording.*` en `rec/replay.*`: het opnameformaat en het deterministisch afspelen door de pijplijn.
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

- `t_rx_ns` is de ontvangsttijd. Dit is het moment direct nadat `psmove_poll()` in de HID-thread een nieuw rapport opleverde. Het is GEEN sensor- of belichtingstijd. Het bevat de Bluetooth-overdracht, de kernel/hidraw-buffer en tot ongeveer 1 ms van onze poll-lus. Het rapport bevat ook een eigen tijdteller van de Move (16 bit, 10 µs per tick, gemeten). PSMoveAPI maakt die niet publiek; de daemon leest hem via de hidraw-tap.
- `stamp` (in `RawReport`, niet in het sample) is die interne tijdstempel. De pijplijn ontrolt hem en koppelt hem aan de hostklok (`MoveClock`, ondergrensmethode). Standaard is de integratie-dt van de fusie het stempelverschil (`--dt-source move`); zonder stempel bij dit of het vorige rapport is het de ontvangst-dt. De status toont onder `timebase` de koppeling (skew, restvertraging) en de temperatuur.
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

Er zijn twee schatters, te kiezen met `--fusion`, `ravengunctl fusion mahony|mahony_mag`, IPC `set_fusion` (`mode`) of `config.json` (`fusion.mode`; bewaard bij `set_fusion`). Standaard: `mahony`.

- `mahony`: een Mahony-complementair filter met alleen een proportionele term (`kp` = 0,5 per s), IMU-only. De accelerometer corrigeert alleen roll en pitch, en alleen als `| |a| - 1 g |` ≤ 0,15 g is. Bij een sterke versnelling (een schot, een zwaai) wordt dat sample niet gebruikt voor correctie. Er is bewust geen integrator-term. De gyro-bias wordt apart en zichtbaar geschat. Yaw drift met de restbias en met de ruis (random walk).
- `mahony_mag` (fase 4, `src/ravengun/core/mag_yaw.*`): dezelfde Mahony voor de tilt, plus een yaw-correctie met de gekalibreerde magnetometer. Een klein Kalman-filter schat de yaw-fout en de drift om de verticaal uit de kompasrichting van de richtas (accelerometer + veld). De drift gaat in een extra bias om de verticaal (verandering hooguit 0,01°/s per s, |b_v| ≤ 0,3°/s); de resterende hoekfout wordt hooguit met 0,02°/s (stil, |ω| < 3°/s) tot 2 % van de draaisnelheid (max 0,3°/s) weggewerkt. Nooit een sprong; alleen na een onderbroken integratie (gat > 250 ms, herverbinding) glad terug met max 5°/s. Verstoorde samples (de detector: norm, dip, gyro-consistentie; plus een χ²-innovatiepoort) tellen niet. Zonder kalibratie, of als de magnetometer verstoord is, is het gedrag dat van `mahony`. In de modus `mahony` draait dezelfde berekening in de schaduw, zodat de status de opgebouwde drift toont. Ontwerp, motivatie en replay-getallen: `docs/aim-fusion-design.md`, "Metingen en bouw fase 4".

Magnetometer naar B: `m_B = diag(-1,+1,-1) · A (m_ruw - b)` (gemeten, zie het ontwerp; oude kalibratiebestanden zonder `b_map` worden bij het laden omgezet). De pijplijn bezit de verstoringsdetector (de status leest hem uit).

Verloren rapporten: bij een stempel-dt boven 1,5 rapportperiode (11,27 ms) wordt het ontbrekende deel met de gemiddelde hoeksnelheid van vóór en na het gat geïntegreerd (`gaps_interpolated` in de status), daarna elk subsample zijn halve periode.

### Gyro-bias

Gemeten op de Halo-opname van 2026-10-08 (270 s, de speler houdt de Move de hele tijd vast): de gyro-bias is niet constant maar loopt mee met de temperatuur van de Move (ruwe `tc` 1852 bij het aanzetten, 2050 na 4 minuten). Alleen de verticale component (z) beweegt merkbaar: ongeveer +0,05 graden/s bij tc 1860, +0,4 bij 1950 en +0,85 bij 2046; x (+1,5) en y (-2,3) blijven binnen 0,3 graden/s. De stilstandsschatter legde de bias in de eerste 30 s op tafel vast (z = +0,1) en kon daarna, met de gun in de hand, nooit meer bijwerken (in 190 s speeltijd slechts 3 s strikte stilstand). Resultaat: yaw-drift altijd negatief, mediaan 25,8 graden/min. De drift volgde de temperatuur, niet de gedraaide hoek (een replay waarin alleen de z-bias met de temperatuur wordt aangepast, haalt de drift ongeveer weg). `bias_state` is "none" (niets), "restored" (bewaarde waarde, nog niet bevestigd), "estimating" (er is gemeten, maar nog geen 3 s bewijs) of "estimated"; `bias_source` is "restored", "still" of "quasi-still" (waar de laatste wijziging vandaan kwam).

De schatter heeft twee modi (`BiasConfig::mode`, `ravengund --bias-mode new|legacy`, `ravengun-analyze --bias-mode`): `legacy` is de oude stilstandsschatter; `new` (standaard) voegt de onderdelen hieronder toe. In rust (de drie opnames van 2026-10-07) geven beide bit-voor-bit dezelfde oriëntatie.

Stilstand (beide modi): per subsample een venster van 0,5 s. Het venster telt als stil als de standaardafwijking per as ≤ 0,035 rad/s is (2 graden/s; de gemeten ruis van deze ZCM1 is ongeveer 0,5 graden/s op X en Y en 1,1 graden/s op Z), het gemiddelde minder dan 0,10 rad/s van de huidige bias afwijkt en `|a|` overal binnen 0,08 g van 1 g blijft. Tijdens stilstand is de bias het lopende gemiddelde van alle stille vensters, tot er 30 s stilstand is gezien. Daarna is het een exponentieel gemiddelde met een tijdconstante van 30 s. Na 3 s stilstand is de bias "estimated" en accepteert hij alleen nog vensters die hooguit 0,6 graden/s van de bias afwijken (daarvoor 5,7 graden/s; een langzame zwaai werd als stilstand opgenomen). Beperking: een zeer gelijkmatige, langzame rotatie onder die drempels is niet van bias te onderscheiden.

Alleen in modus `new`:

- Bewaarde bias (`restored`). Per controller staat de laatste goede bias in `~/.local/state/ravengun/gyro-bias/<controller_key>.json` (`{format, version, controller_key, bias_deg_s[3], tc, unix_s, source}`; map 0700, bestand 0600, atomisch geschreven; waarden boven 20 graden/s worden als beschadigd geweigerd). De daemon laadt hem bij het verbinden, alleen als de pijplijn nog niets gemeten heeft. De waarde telt als 1,5 s stilstand mee (`restored_weight_s`) en de eerste stille vensters mogen hooguit 1,5 graden/s ervan afwijken (`restored_gate`; gisteren en vandaag verschilde deze Move op dezelfde temperatuur minder dan 0,2 graden/s). Zo verdringen een paar vervuilde vensters hem niet, en een Move die echt stil ligt overtuigt binnen enkele seconden. Staan stille vensters 10 s lang op hetzelfde punt (binnen 0,5 graden/s) verder dan de grens, dan is de bewaarde (of de huidige) waarde fout en herstelt de schatter zich (`reacquire_s`; bewust langer dan een draai van 5 s). Bewaard wordt alleen een eigen meting (stilstand of quasi-stil, nooit een kale herstelde waarde): de eerste keer zodra er 3 s stilstand is gemeten of 20 s na het verbinden, daarna bij 0,15 graden/s verschil (niet vaker dan elke 30 s) of 0,03 graden/s (niet vaker dan elke 300 s), en bij verbreken of afsluiten. `ravengund --bias-dir MAP` kiest de map, `--no-bias-store` zet laden en bewaren uit; `ravengun-analyze` en de tests bewaren nooit.
- Quasi-stille vensters. Niet-overlappende vensters van 0,5 s tellen mee als de standaardafwijking per as ≤ 2,5 graden/s is en `|a|` binnen 0,10 g van 1 g blijft, en alleen als hun gemiddelde binnen 1,2 graden/s van de huidige bias ligt. Een bewuste draai (3 graden/s) valt dus buiten het venster; de trage bias niet. De bias gaat naar het gewogen mediaan (per as) van de vensters van de laatste 90 s (gewicht 1/(1+(sd/0,7 graden/s)²), recentie exp(-leeftijd/30 s)), minimaal 6 vensters, met tijdconstante 10 s en hooguit 0,03 graden/s per s verandering. Zonder eerdere schatting (geen stilstand, niets bewaard) is er een eerste schatting nodig: minstens 16 vensters (8 s) binnen 8 graden/s van nul, waarvan 60 % binnen 0,9 graden/s van hun mediaan; het gemiddelde van die cluster wordt de bias. Op de Halo-opname doen nauwelijks vensters mee (met de hand wordt de gun vrijwel nooit 0,5 s lang zo rustig gehouden: 2 van ongeveer 540 vensters werden gebruikt, de rest viel af op spreiding, accelerometer of de grens van 1,2 graden/s); deze route is het vangnet, de temperatuurcompensatie hieronder doet het meeste werk.
- Temperatuurcompensatie. Uit de stille en quasi-stille vensters van de laatste 600 s (gewicht per recentie exp(-leeftijd/300 s)) wordt per as een helling (rad/s per telling van `tc`) geschat met gewogen kleinste kwadraten. Alleen als de temperatuur in dat bewijs minstens 25 tellingen is verlopen, met minstens 12 punten, en verkleind met `1 - (σ_helling / helling)²` (nul als de helling niet significant is), begrensd op 0,01 graden/s per telling. Bij elke verandering van `tc` (elke ~3 s tijdens het opwarmen) schuift de bias met helling × verandering mee: geleidelijk, nooit met terugwerkende kracht; hooguit 1 graad/s sinds de laatste meting, en een glitch van meer dan 20 tellingen wordt genegeerd. Op de Halo-opname leert dit uit de eerste minuut op tafel een z-helling van ongeveer +0,003 graden/s per telling, genoeg om de bias in de hand mee te laten lopen. Beperking: de helling is niet constant (gisteren, tc 2010 tot 2064, was z vlak op +0,85; de curve is niet lineair). Een bewaarde curve per controller (bias tegen temperatuur) zou ook een koude start zonder rust op tafel dekken en is nog niet gebouwd.
- Samenspel met `mahony_mag`. Die schat zelf een verticale bias via de magnetometer. Zolang de magnetometer de yaw corrigeert (`mag_yaw_active`: vergrendeld, toestand ok, bruikbaar sample < 1 s geleden) staat het quasi-stille bewijs uit en schuift de temperatuurcompensatie niet; echte stilstand blijft meetellen. Zo wordt dezelfde drift niet twee keer gecorrigeerd. Is de magnetometer verstoord (de praktijk: 99,8 % afgewezen), dan werkt het quasi-stille deel gewoon.
- Statuslabel. `ravengunctl status` toont "Gyro-bias: estimated (bron quasi-still), +1,54 -2,34 +0,15 graden/s (x y z); stilstand 24 s, quasi-stil 12 s; temperatuurhelling ...; modus new" en de bewaarstatus (hersteld bij start, aantal keer bewaard, fout). In het sample staan `orientation.bias_state`, `bias_source`, `bias_evidence_s` en `gyro_bias_deg_s`; het statusblok `gyro_bias` voegt de helling per as, de quasi-tellers en de bewaarstatus toe.

Metingen (`ravengun-analyze drift` op de Halo-opname, 8 PS-intervallen, koude start): legacy mediaan |yaw-drift| 25,8 graden/min (p90 35,3), cumulatief eerste tot laatste PS (164 s) -62,6 graden; new mediaan 8,0 (p90 15,7), cumulatief -1,0 graden (-0,36 graden/min). Bij een warme start (beginbias uit een eerdere ronde van dezelfde opname; circulair en optimistisch) gebeurt hetzelfde, omdat de eerste 10 s van de opname al rust zijn. Het richten van de speler zelf (ongeveer 1 tot 2 graden per PS-druk) zet een ondergrens van ruwweg 6 tot 8 graden/min onder de per-interval-mediaan bij intervallen van 13 s; het cumulatieve cijfer is de eerlijker maat. Zonder rust aan het begin (de eerste 70 s weggeknipt, dus ook geen temperatuurbewijs) hangt de uitkomst af van de beginwaarde: cumulatief +2,4 graden/min met een beginbias z = 0,5 graden/s, maar -27 en +20 graden/min bij z = 0,0 en 0,9 (de werkelijke z loopt in die minuten van 0,3 naar 0,85). Zonder beginwaarde is het eerste "stille" venster in de hand bovendien vervuild door echte beweging (cumulatief +20 graden/min). Dat is de grens van deze aanpak: zonder rust en zonder bewaarde, temperatuurgecorrigeerde curve is de yaw-bias niet waarneembaar met alleen de gyro; de magnetometer (fase 4) of de camera moet dat dan oplossen.

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

## Driftmeting uit een opname (`ravengun-analyze drift`)

`ravengun-analyze drift OPNAME [--mode mahony|mahony_mag] [--cal KALIBRATIE.json] [--bias-mode legacy|new] [--bias-init X,Y,Z|warm] [--bias-set sleutel=waarde]` (code in `src/ravengun/rec/drift.*`) gebruikt elke down-flank van de PS-knop als meetpunt: de speler richt dan op het schermmidden. De opname gaat door de pijplijn (zonder recenter); het yaw- en pitchverschil met de vorige PS-druk is de drift in dat interval. Intervallen korter dan 2 s worden overgeslagen. Per interval: duur, yaw/pitch in graden en graden per minuut, totaal gedraaide hoek, maximale draaisnelheid, verloren rapporten (4-bit seq-gaten; een gat van precies 16 is onzichtbaar), langste gat tussen rapporten (ontvangsttijd), subsamples met een gyro-as op of boven 32000 van 32767 (alleen met ruwe waarden) en de ruwe temperatuur (`tc`) begin en eind. Samenvatting: mediaan en p90 van |drift| per minuut, de cumulatieve yaw-drift van de eerste tot de laatste PS-druk (het richten van de speler telt daar maar tweemaal mee) en Pearson-correlaties van |yaw-drift| met gedraaide hoek, maximale snelheid, verloren rapporten en duur (pas zinvol bij 5 of meer intervallen).

De opname gaat door dezelfde pijplijn als live, dus automatisch met de nieuwe biasschatter. `--bias-mode legacy` geeft het oude gedrag ter vergelijking. `--bias-init X,Y,Z` (graden/s) geeft een beginbias (zoals een bewaard bestand), `--bias-init warm` speelt de opname twee keer af en gebruikt de eindbias van de eerste ronde als beginbias van de tweede (circulair, dus optimistisch). `--bias-set` stelt parameters van `BiasConfig` bij (bijvoorbeeld `quasi_gate_dps=1.2`, `temp_min_range=25`, `temp_comp=0`). Dezelfde opties gelden voor `replay` en `fusion`. De analysetools schrijven nooit een bias-bestand.

`ravengun-analyze buttons OPNAME` telt in een opname met Sharp Shooter-bytes de schoten (fire-bit en de oude T-trekker), de volgorde van T en fire en de afgeleide pompen (zie "Uitbreidingsconnector").

## Knopflanken

Elk HID-rapport bevat een volledige knopstatus en wordt afzonderlijk verwerkt. Elke wisseling tussen twee rapporten wordt dus een flank in dat sample (`pressed`, `released`). De gepubliceerde momentopname houdt per knop een cumulatieve teller van down- en up-flanken bij. Iedere afnemer (watch-client, later de outputbackend) onthoudt zijn eigen tellerstand, zodat ook een druk+los tussen twee leesmomenten en meerdere drukken zichtbaar blijven, ongeacht de leessnelheid. De bitposities volgen `enum PSMove_Button` van PSMoveAPI. Een druk die korter is dan één rapportinterval en volledig tussen twee rapporten valt, zit niet in de hardwaredata.

## Inputmodel (schema-versie 2)

`InputSample` (`src/ravengun/core/input_model.hpp`) heeft in JSON de volgende velden. Een nieuwe of gewijzigde betekenis verhoogt `schema_version`.

- `schema_version`, `seq` (per gepubliceerd sample, oplopend binnen een daemon-run), `t_rx_ns`, `t_eval_ns`, `report_dt_ms` en `hid_seq` (4-bitsvolgnummer uit het rapport).
- `controller_id`: het Bluetooth-adres (stabiel, anders dan `/dev/hidrawN`). Standaard geredigeerd tot `xx:xx:xx:xx:xx:ac`, volledig alleen met `full_ids`.
- `tracking_mode`: nu altijd "orientation". Gereserveerd zijn "optical_diagnostic", "hybrid" en "geometric_ray".
- `link`: "disconnected", "fresh", "stale" of "lost".
- `orientation.fusion` (fase 4, nieuwe velden, verder ongewijzigde betekenis): `mode` (mahony, mahony_mag), `yaw_source` (gyro of magnetometer: mahony_mag, vergrendeld, toestand ok en minder dan 1 s geleden een bruikbaar sample), `magnetometer` (none zonder kalibratie, unknown, ok, disturbed), `mag_locked`, `yaw_pending_deg` (openstaande yaw-fout volgens de magnetometer; bij mahony de opgebouwde drift), `correction_rate_deg_s` en `correction_limit_deg_s` (toegepaste hoekcorrectie en de grens), `correction_total_deg`, `vertical_bias_deg_s` (door de magnetometer geleerde bias) en `mag_unusable_s`. Ook in de SSE-stroom (het sample). De status bevat onder `magnetometer.yaw_correction` daarnaast σ en de geschatte drift.
- `orientation`: `valid`, `q_wb`, `world_yaw_deg` en `world_pitch_deg` (zonder midden, alleen diagnose), `bias_state` (none, restored, estimating, estimated), `still`, `bias_source` (none, restored, still, quasi-still), `bias_evidence_s` (stilstand plus quasi-stil bewijs in seconden), `gyro_bias_deg_s` en `yaw_reference`.
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
- `record` (`action` start, stop, status of mark; `tag`, `label`, `max_mb`, `duration_s`): opnamen (fase 0), zie `docs/procedures.md`.
- `set_led` (`rgb` [r,g,b]): de LED-kleur voor de lopende run (de trackertint verandert niet mee).
- `calib_mag` (`action` start, stop, abort of status; `holder`): de magnetometerkalibratie (fase 3). `stop` fit en slaat alleen bij voldoende kwaliteit op.
- `set_fusion` (`mode` mahony of mahony_mag): de oriëntatieschatter, direct en bewaard in `config.json`. Omschakelen geeft geen sprong; bij inschakelen wordt de huidige stand de referentie.

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

Naam `RavenGun Virtual Lightgun`, bus USB, VID 1209 (pid.codes-testbereik), PID 7202, versie 1. Het toetsenbord is 1209:7201 (zie "Virtueel toetsenbord in de daemon"). `EV_ABS` `ABS_X`/`ABS_Y` met bereik 0..32767 op beide assen (`kAbsMin`, `kAbsMax` in `output/gun_transform.hpp`); `EV_KEY` `BTN_LEFT` (trigger), `BTN_RIGHT` (reload), `BTN_MIDDLE` (grenade), `BTN_SIDE` en `BTN_EXTRA` (aux). Geen `EV_REL`, en bewust geen `INPUT_PROP_DIRECT` (dat markeert een touchscreen) en geen `BTN_TOUCH` (dan gedraagt het apparaat zich als tablet). Udev (`packaging/udev/70-ravengun.rules`) geeft de seat-gebruiker leesrecht (uaccess), maakt `/dev/input/ravengun-gun` en `/dev/input/by-id/usb-RavenGun_Virtual_Lightgun-event-mouse`, en zet `LIBINPUT_IGNORE_DEVICE=1` op de gun. De keuze berust op: de udev-regel selecteert op VID/PID, de TeknoParrot-binding op het by-id-pad, en nergens op `eventN`.

### Live Move als bron (fase 4)

De HID-thread geeft elk gepubliceerd sample ook aan de uitvoer (`src/ravengun/output/live_feed.*`, `OutputService::feed_live`), maar alleen als de uitvoer gewapend is en er geen testpatroon loopt:

- Positie: de gefilterde, ongeclampte aim (`aim.x/y`) van dat rapport, alleen als `aim.valid`. Bij een ongeldige aim (stale, geen midden) stopt het bewegen; de cursor blijft staan, zonder sprong naar een hoek.
- Trekker: zonder uitbreiding de analoge T-waarde met hysterese (in bij ≥ 140, los bij ≤ 100; `gun_buttons.trigger_on/off`); met een actieve Sharp Shooter het fire-bit van de uitbreiding (zie "Uitbreidingsconnector"). De overige knoppen volgen de indeling uit "Knoppenindeling" hieronder. Recenter: een down-flank van de PS-knop (altijd, ook als de uitvoer niet gewapend is, zolang de HID-pijplijn loopt) of van een Move-knop die in de map op `recenter` staat (alleen gewapend); de pijplijn legt dan een nieuw midden vast (`center_source: user`). De mapping `sharp_shooter.buttons` uit de kalibratie-UI wordt bewust niet gebruikt: de eerste meting was fout (de gebruiker drukte in het donker verkeerde knoppen).
- Knoppen volgen de link: bij `stale` (tot 250 ms) blijven ze staan (anders zou een Bluetooth-gat een automatisch dubbelschot geven), bij `lost`, `disconnected` en het afsluiten worden alle gun-knoppen en toetsen vrijgegeven.
- Geen eigen uitvoerklok: één positie per rapport (gemiddeld ~77/s, gaten tot ~50 ms). De 250 Hz-klok met gyro-extrapolatie (ontwerp 5.2) is fase 6; die vraagt een voorspelde oriëntatie per tik en het 1€-filter op de uitvoertik, en is daarom nu niet gebouwd.
- Status: `output.live` (samples, posities, ongeldige aim, vrijgaven, onderdrukt door een patroon).

### Knoppenindeling (`gun_buttons`)

De indeling staat in `config.json` onder `gun_buttons` en geldt voor elke game (eerst Halo in TeknoParrot). Vaste regels, niet configureerbaar:

- T staat niet in de map: zonder uitbreiding is hij de analoge trekker (drempel met hysterese), met een actieve Sharp Shooter is de trekker het fire-bit en is T zonder fire de pomp (knop `pump`).
- PS is altijd recenter (`pipe.recenter()`, log "recenter met de PS-knop"), ook als de uitvoer niet gewapend is. `ps` in de map geeft een validatiefout.

Formaat: `trigger_on`, `trigger_off` en een object `map` van Move-knopnaam naar actie.

- Knoppen: `triangle`, `circle`, `cross`, `square`, `select`, `start`, `move` en de Sharp Shooter-knoppen `pump`, `ss_rl`, `ss_fire`, `sel1`, `sel2`, `sel3` (zie "Uitbreidingsconnector" hieronder).
- Acties: `reload`, `grenade`, `aux1`, `aux2` (de gun-knoppen BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA), `recenter`, `key:<naam>` (toets op het virtuele toetsenbord) of `none` (ook `""`).
- Meerdere Move-knoppen mogen dezelfde actie hebben: de actie is actief zolang minstens een van die knoppen ingedrukt is (OR).
- Een `map` die maar enkele knoppen noemt, laat de defaults voor de andere knoppen staan.
- Standaard (geschikt voor Halo; TeknoParrot-bindingen: Coin = toets 5, Start P1 = toets 1, Reload = BTN_RIGHT, Grenade = BTN_MIDDLE): `move` = grenade, `cross` = reload, `triangle` = key:5, `square` = key:1, `select` = key:5, `start` = key:1, `circle` = none, `pump` = reload, `ss_rl` = reload, `ss_fire`, `sel1`, `sel2` en `sel3` = none.
- Toetsnamen: `esc`, `enter`, `space`, `tab`, `backspace`, `up`, `down`, `left`, `right`, `lshift`, `lctrl`, `lalt`, `f1` tot `f12`, `0` tot `9`, `a` tot `z` (tabel in `output/key_table.hpp`; hoofdletters en een voorvoegsel `key_` worden genegeerd).
- Oud formaat: de sleutels `reload`, `grenade` en `recenter` (waarde = Move-knopnaam) blijven leesbaar en worden vertaald naar map-items die de default voor die knop overschrijven (`recenter: "ps"` wordt genegeerd met een waarschuwing). Bij opslaan wordt alleen het nieuwe formaat geschreven.
- Onbekende knop- of toetsnamen, `ps` en `t` in de map zijn validatiefouten met een duidelijke tekst; het bestand wordt dan niet toegepast.

Voorbeeld:

    "gun_buttons": {
      "trigger_on": 140, "trigger_off": 100,
      "map": {"triangle": "key:5", "circle": "none", "cross": "reload", "square": "key:1",
              "select": "key:5", "start": "key:1", "move": "grenade"}
    }

Zichtbaarheid: `ravengunctl output [--json]` toont `map` (inclusief `ps` = "recenter (vast)"), `keys_down`, `buttons_down` en het blok `keyboard`; het startlog van ravengund toont de indeling ("knoppenindeling: ..."). De web-UI toont `keys_down` in het uitvoerblok; de indeling is daar niet bewerkbaar (bewerk `config.json`; de daemon leest die bij de start).

### Uitbreidingsconnector (Sharp Shooter)

De Move zit in een PlayStation Sharp Shooter, die extra knoppen via de EXT-connector van de Move levert. Bron: PSMoveAPI `src/psmove.c` en `src/utils/test_extension.c` (alleen offsets en bitmaskers; er is geen code overgenomen).

- Het ZCM1-invoerrapport is 49 bytes. Byte 4 (`buttons4`) bit `0x10` is "ext verbonden" (`psmove_is_ext_connected`). Bytes 44 tot en met 48 zijn de 5 EXT-databytes (`PSMove_ZCM1_Data_Input.extdata`, `PSMOVE_EXT_DATA_BUF_SIZE` = 5). Op de ZCM2 bestaat dit niet.
- Sharp Shooter (device-id 0x8081): byte 0, bits `0x01`, `0x02` en `0x04` = vuurstand 1, 2 en 3 (bits 0 tot 2, `c & 7`), `0x40` = fire, `0x80` = RL. Bytes 1 tot 4 zijn onbekend en worden alleen vastgelegd. Gemeten in de Halo-opname van 2026-10-08 (270 s, 20636 rapporten): byte 0 is 0x04 (vuurstand 3) of 0x44 (met fire); het RL-bit kwam nooit voor en is dus een andere knop dan de pomp.
- `RawReport` en `InputSample` hebben `has_ext`/`ext_known`, `ext_connected` en `ext[5]`. De daemon leest ze via `psmove_is_ext_connected` en `psmove_get_ext_data` (onafhankelijk van de hidraw-tap); `core/move_report.*` parseert dezelfde bytes uit een ruw rapport.
- De pijplijn voegt virtuele knopbits toe aan `buttons` (bits 24 tot 29): `ss_rl` (het RL-bit), `ss_fire`, `sel1`, `sel2`, `sel3` en de afgeleide `pump`. Ze verschijnen als gewone knoppen in `buttons.down`/`pressed`/`released`, in de flankenteller en in `gun_buttons.map`. Ze zijn alleen actief als EXT verbonden is en de modus niet `none` is.
- Trekker en pomp (`SharpShooterPump` in `core/buttons.*`; gemeten, `ravengun-analyze buttons`). De Move ziet de pomp als de trekkerknop T zonder dat het fire-bit staat (PSMoveAPI `src/utils/test_extension.c` doet het ook zo). In de opname: 131 schoten, en het fire-bit valt altijd samen met T (136 T-flanken = 131 schoten + 5 pompen). Bij het indrukken gaan T en fire in hetzelfde rapport omhoog (126 van 131 keer met T = 255; 5 keer is T op dat moment 127 en pas 1 rapport later 255, dus de oude T-trekker met drempel 140 kwam dan 1 rapport te laat). T loopt nooit voor op fire. Bij het loslaten gaat fire 6 keer 1 rapport eerder omlaag dan T (T is dan 1 rapport lang 127). Een pomp is 5 keer gezien: 11 tot 22 rapporten (140 tot 280 ms) met T = 255 zonder fire, met een glooiende T (0, 27, 69, 102, 255 ... 255, 189, 81, 24, 0). Daarom: de trekker (`GunButton::Trigger`) is het fire-bit zelf, dus 0 extra rapporten vertraging en in 5 van 131 gevallen zelfs 1 rapport eerder dan de oude T-trekker; de pomp is T ≥ `trigger_on` (140) zonder fire gedurende 2 opeenvolgende rapporten (1 extra rapport = ongeveer 13 ms vertraging, voor herladen zonder belang), los zodra T ≤ `trigger_off` (100) of fire verschijnt. De scheefloop van 1 rapport (T = 127 < 140, of T 1 rapport vóór fire) geeft zo geen pomp en een pomp geeft nooit een schot, omdat de trekker alleen uit fire komt. Replay op de echte opname: 131 schoten en 5 pompen. Zonder uitbreiding (modus `none` of geen EXT) blijft T met hysterese de trekker.
- Instelling `sharp_shooter.extension` in `config.json`: `auto` (standaard), `sharp_shooter` of `none`. Het device-id (I2C via feature-rapporten, `psmove_get_ext_device_info`) wordt nog niet gelezen: `auto` en `sharp_shooter` nemen aan dat een verbonden EXT-apparaat de Sharp Shooter is. Wordt gelezen bij het starten van de daemon. Nog te bevestigen op echte hardware: de ruwe bytes in de opname zijn het bewijs.
- Opname (`"k":"r"`): extra velden `"xc"` (EXT verbonden, 0 of 1) en `"x"` (array van 5 bytes, 0 tot 255). Alleen aanwezig als de daemon ze kende; oude opnames zonder deze velden blijven leesbaar (`has_ext` is dan onwaar).
- Status-JSON (sample): blok `ext` met `known`, `connected`, `active` en `bytes`. `ravengunctl status` toont "Uitbreiding: ext verbonden: ja [80 00 00 00 00]" en `watch` toont `ext ...` in elke regel.

### Virtueel toetsenbord in de daemon

`output/uinput_kbd.*` (`UinputKeyboard`, achter de abstractie `KeyDevice` zodat tests een nep-apparaat gebruiken) maakt hetzelfde apparaat als het losse hulpprogramma `src/tools/vinput/vinput.c kbd`: naam `RavenGun Virtual Keyboard`, bus USB, VID 1209, PID 7201, versie 1, dezelfde toetsenset. TeknoParrot bindt op `/dev/input/by-id/usb-RavenGun_Virtual_Keyboard-event-kbd` (udev-regel bestaat al). Alles zit zo in een applicatie; `vinput` blijft bestaan voor diagnose maar is niet meer nodig. Twee apparaten met dezelfde identiteit tegelijk (daemon en een lopende `vinput kbd`) geven twee eventN-nodes; stop `vinput kbd` voordat je de daemon wapent.

- Levenscyclus gelijk aan die van de gun: `GunOutput` maakt het toetsenbord aan bij de eerste `arm` (zodra de gun bestaat; TeknoParrot opent evdev-apparaten bij de gamestart) en verwijdert het bij afsluiten of een schrijffout. Kan het toetsenbord niet worden gemaakt, dan werkt de gun wel; de fout staat in het log en in `output.keyboard.error`.
- Toetsen gaan alleen door als de uitvoer gewapend is (`GunOutput::set_key`). Alleen wijzigingen leveren een event op.
- Bij `disarm`, bronuitval (`release_all`: link `lost`/`disconnected`), een schrijffout en afsluiten worden alle ingedrukte toetsen losgelaten.
- De toetsnamentabel staat in `output/key_table.hpp` (C++). `vinput.c` is C en heeft een eigen kopie; `tests/test_output.cpp` (`test_key_table`) controleert de namen, codes en de apparaatidentiteit.

### Arm/disarm en veiligheid

De daemon start disarmed: er gaan geen events naar het apparaat en `test_pattern`/`output_button` weigeren. `arm` maakt het apparaat aan (bij de eerste keer) en schakelt events in. `disarm` geeft eerst alle knoppen vrij (up-events) en schakelt dan events uit; het apparaat blijft bestaan zodat een binding en een lopende game niet breken. Alle knoppen worden ook vrijgegeven bij een schrijffout (het apparaat wordt dan verwijderd en de uitvoer disarmed; een nieuwe `arm` maakt het opnieuw aan), bij `release_all` (bronuitval, ook voor de toetsen van het virtuele toetsenbord) en bij afsluiten (SIGTERM/SIGINT: eerst up-events, dan het apparaat verwijderen). Na een crash (SIGKILL) verwijdert de kernel het apparaat en geeft daarbij de knoppen vrij (getest). De daemon opent zijn eigen apparaat nooit voor lezen; er is in de code geen leesfunctie voor het uitvoerapparaat, dus geen feedbackloop. Het apparaat bestaat pas na de eerste `arm` en blijft tot het afsluiten van de daemon.

### Coördinaatruimtes en transformatie

Aim-ruimte: genormaliseerd fysiek scherm, (0,0) linksboven, y omlaag, ongeclampt en ongespiegeld (zo komt de aim uit de pijplijn). `output/gun_transform.cpp` doet daarna, in deze volgorde en precies één keer: viewport-normalisatie (letterbox/pillarbox, `letterbox_viewport(scherm_aspect, content_aspect)`; standaard het hele scherm), inversie per as, schaal rond het midden met offset (kalibratiecorrectie, standaard 1 en 0), clamp naar [0,1] en afronden op `abs_max`. Niet-eindige invoer geeft geen event. Verder in de keten gebeurt niets meer: uinput en TeknoParrot schalen alleen lineair naar de game-bytes (zie hierboven). De `GunTransform` staat in de daemon (`--out-*` opties) en is live te wijzigen met `set_output` (alleen voor de lopende run).

### Meting in Halo (G1-omgeving, 1920x1080, backend zonder correctie)

Gemeten met `ravengunctl test-pattern point X Y` (aim = het gegeven punt), een screenshot, en een eigen meetscript dat de groene ring met kruis van het spelvizier zoekt (volledige ring: midden van de bounding box; bij een deels buiten beeld staand vizier een cirkelfit met vaste straal 55 px). Halo toont het vizier op de volledige 16:9-tv zonder letterbox, met `byte = round(255·u)` en `pixel = byte·1920/255` (x) of `byte·1080/255` (y). Dat verklaart alle metingen: een lineaire afbeelding zonder inversie, offset of schaalfout, met een raster van 7,5 px (0,39 % van de breedte) door de 8-bits as van het spel. De spelkalibratie (scherm "Pull trigger when ready to calibrate") is overgeslagen met Test; een uitgevoerde spelkalibratie zou dit veranderen.

Fout (gemeten min verwacht, px; x/y; % van de breedte 1920 / hoogte 1080):

- (0,5; 0,5): verwacht 960; 540; gemeten 963,5; 542,0; fout px +3,5; +2,0; fout % 0,18 / 0,19
- (0,1; 0,1): verwacht 192; 108; gemeten 195,5; 110,0; fout px +3,5; +2,0; fout % 0,18 / 0,19
- (0,5; 0,1): verwacht 960; 108; gemeten 963,5; 110,0; fout px +3,5; +2,0; fout % 0,18 / 0,19
- (0,9; 0,1): verwacht 1728; 108; gemeten 1723,5; 110,0; fout px -4,5; +2,0; fout % 0,23 / 0,19
- (0,1; 0,5): verwacht 192; 540; gemeten 195,5; 542,0; fout px +3,5; +2,0; fout % 0,18 / 0,19
- (0,9; 0,5): verwacht 1728; 540; gemeten 1723,5; 542,0; fout px -4,5; +2,0; fout % 0,23 / 0,19
- (0,1; 0,9): verwacht 192; 972; gemeten 195,0; 969,5; fout px +3,0; -2,5; fout % 0,16 / 0,23
- (0,5; 0,9): verwacht 960; 972; gemeten 963,0; 969,0; fout px +3,0; -3,0; fout % 0,16 / 0,28
- (0,9; 0,9): verwacht 1728; 972; gemeten 1723,5; 969,5; fout px -4,5; -2,5; fout % 0,23 / 0,23
- hoek (0;0): verwacht 0; 0; gemeten 1,2; 1,9; fout px +1,2; +1,9; fout % 0,06 / 0,18
- hoek (1;0): verwacht 1920; 0; gemeten 1917,6; 1,9; fout px -2,4; +1,9; fout % 0,13 / 0,18
- hoek (1;1): verwacht 1920; 1080; gemeten 1917,6; 1077,2; fout px -2,4; -2,8; fout % 0,13 / 0,26
- hoek (0;1): verwacht 0; 1080; gemeten 1,2; 1077,3; fout px +1,2; -2,7; fout % 0,06 / 0,25

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

- De gyro-bias in de hand is alleen met temperatuurcompensatie en rust-op-tafel in het begin goed te volgen (zie "Gyro-bias"); zonder die rust en zonder bewaarde waarde is de yaw-bias niet waarneembaar. De bewaarde waarde is de laatste goede bias bij een bepaalde temperatuur (`tc`): na een koude start is hij tot ongeveer 1 graad/s te hoog (z), tot de eerste rust of het quasi-stille bewijs hem corrigeert.

- PSMoveAPI schrijft bij het verbinden zelf een waarschuwing met het volledige Bluetooth-adres naar stderr ("Magnetometer in … not yet calibrated"). Die tekst valt buiten de redactie van RavenGun. Logs van `ravengund` die gedeeld worden, moeten daarom gefilterd worden (nog te regelen bij logging naar bestand).
- De ontvangsttijd is geen sensortijd (zie Tijdbases).
- De optische positie is een 2D-beeldpositie. Diepte, een cameramodel (intrinsieken, lensvervorming) en de koppeling met het schermvlak ontbreken nog.
- Het 4-bitsvolgnummer meldt regelmatig een overgeslagen nummer (`dropped_by_seq`). Of dat werkelijk verloren rapporten zijn of een eigenschap van de firmware, is nog te bevestigen. De rapportfrequentie zelf wordt los daarvan gemeten.
