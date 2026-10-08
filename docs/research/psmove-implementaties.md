# Onderzoek: hoe bestaande PS Move-implementaties drift oplossen

Datum: 2026-10-08. Labels: [GELEZEN] = broncode of pagina zelf opgehaald; [ZOEK] = alleen uit een zoekresultaat-samenvatting, niet zelf geopend; [AFGELEID] = eigen redenering, niet in een bron gevonden. Veel zoekopdrachten leverden weinig op (zie sectie c).

## (a) Bevindingen per punt

### 1. PSMoveService / PSMoveServiceEx
Bron: https://github.com/cboulay/PSMoveService, map src/psmoveservice/Filter en PSMoveController (gearchiveerd 3 mei 2021).
- [GELEZEN] OrientationFilter.cpp kent: PassThru, MadgwickARG, MadgwickMARG, ComplementaryOpticalARG, ComplementaryMARG. Standaard voor de PSMove (PSMoveController.h, PSMoveControllerConfig): orientation_filter_type "ComplementaryMARG", position_filter_type "LowPassExponential", prediction_time 0, gyro_variance 0,00035, gyro_drift 0,027 rad/s, accelerometer_variance 7,2e-6, magnetometer_variance 0,00059.
- [GELEZEN] ComplementaryMARG: gyro-integratie plus nlerp naar een gravity+magnetometer-uitlijning (eigen_alignment_quaternion_between_vector_frames), gewicht start op 1,0 en daalt naar 0,02 (k_base_earth_frame_align_weight). Geen biasschatting, geen poort tegen verstoring, een vaste referentierichting (magnetometer_calibration_direction). Dit is dezelfde aanpak als PSMoveAPI.
- [GELEZEN] MadgwickMARG is het enige filter met gyro-biasschatting (m_omega_bias_x/y/z, zeta*omega_err, afgeleid van gyro_variance). Bij ontbrekende vector valt het terug op MadgwickARG. Het volgt de referentie niet over tijd (code-opmerking).
- [GELEZEN] ComplementaryOpticalARG: blendt naar een optische oriëntatie met max gewicht 0,005 (k_max_optical_orientation_weight), geschaald met een kwaliteit uit de variantiecurve van het bolprojectiegebied; yaw-only blend staat uit (COMPLEMENTARY_FILTER_YAW_ONLY_BLEND 0). Voor een bol (geen vorm met oriëntatie) komt de optische oriëntatie uit positie plus sphere_offset.z; is die offset 0, dan is het gewicht 0. Dus: met alleen een bol geeft PSMoveService geen optische yaw.
- [GELEZEN] KalmanOrientationFilter.cpp: UKF (SquareRootUnscentedKalmanFilter) op een fout-quaternion (vergelijkbaar met onze geplande ESKF). Gyro-bias wordt NIET geschat: omega = control - gyro_bias met een constante uit config. Meetmodellen: MagGravMeasurementModel (accelerometer + magnetometer, PSMove), optisch (volledige oriëntatie). Lineaire versnelling in het gravity-model staat hard op 0. KalmanPoseFilter.cpp: bias ook constant; de LED-positiemeting die positie aan oriëntatie koppelt is uitgecommentarieerd; geen actief pad waar positie de oriëntatie corrigeert.
- [GELEZEN] PSMoveController.cpp: ZCM1-gyro uit USB-blok 0x00/0x01/0x82, alleen schaalfactor (80 rpm), geen offset of drift (alleen ZCM2 heeft drift-offset uit blob). Magnetometer: ellipsoïde-structuur (getMagnetometerEllipsoid: center, extents, basis, fit_error) en projectie op de ellipsoïdebasis, dus echte soft-iron-aanpak, in tegenstelling tot PSMoveAPI. Temperatuur wordt alleen uitgelezen (getTempCelsius), niet gecorrigeerd.
- PSMoveServiceEx (Timocop): https://github.com/Timocop/PSMoveServiceEx. [GELEZEN] OrientationFilter.cpp in de fork heeft dezelfde filterlijst als het origineel (geen nieuw filter gevonden in dat bestand). De wiki (13 pagina's) heeft niets over filters of yaw-drift; changelog noemt vaag "additional filters/settings" en "improved pose calibration". Geen aanbevolen stabiliteitsinstellingen gevonden.

### 2. PSMoveAPI (thp)
Bron: https://github.com/thp/psmoveapi, src/psmove_orientation.cpp.
- [GELEZEN] ZCM1: ComplementaryMARG als standaard, ZCM2: Madgwick IMU. Madgwick MARG alleen via psmove_orientation_set_fusion_type. dt = 1/max(freq,120), twee frames per update. Madgwick-constanten gyroMeasError = pi*1,5/180 en gyroMeasDrift = pi*0,9/180 (de commentaren in de code zeggen 5 en 0,2 graden, wat niet klopt). Alleen Madgwick MARG schat bias.
- [ZOEK] Kalibratieblob ZCM1 143 bytes (met magnetometer), gyro 80 rpm; bias-velden in de blob zijn deels giswerk (nitsch/moveonpc wiki en Monado-structs psmv_parsed_calibration_zcm1: https://github.com/nitsch/moveonpc/wiki, niet zelf geopend). Daar staat dat een extra factor (gyro_fact, dicht bij 1,0) vermenigvuldigd met de bias een betere kalibratie gaf.
- Geen gedocumenteerd psmoveapi-issue over ZCM1-gyrobias/temperatuur gevonden. [ZOEK] Wikipedia noemt een interne temperatuursensor voor compensatie; dat is niet geverifieerd in code.

### 3. Sony zelf
- [ZOEK] GDC 2010-verslag (Game Informer, https://gameinformer.com/b/news/archive/2010/03/11/behind-the-scenes-of-the-playstation-move.aspx): camera en inertiële sensoren samen, systeem compenseert bij verdekte bol. Magnetometer corrigeert cumulatieve drift (Wikipedia, secundair). Geen slides of patent met het filterontwerp gevonden.
- [ZOEK] Sharp Shooter/Killzone 3-kalibratie: spelers richten op twee schermhoeken nadat ze eerst naar de camera hebben gericht, in speelhouding; opnieuw kalibreren bij andere kolfpositie of verplaatste tv (Push Square/Sixth Axis/PlayStation Blog, via zoekresultaat). Time Crisis: Razing Storm had bekende crosshair-drift (Push Square, patch van Namco; reviewer zei dat het vaak aan kalibratie lag). Wat de patch deed is onbekend.
- Conclusie [AFGELEID uit bovenstaande]: Sony loste het niet met ruwe IMU-yaw op, maar met een korte richt-kalibratie (schermhoeken) plus een camera die bij elk frame de bolpositie geeft; de aim-richting kwam waarschijnlijk uit IMU-oriëntatie met magnetometer-yaw. Dit is niet uit een Sony-bron bevestigd.

### 4. Andere projecten
- [GELEZEN] RPCS3, rpcs3/Emu/Io/ps_move_data.h/.cpp: update_orientation schrijft de quaternion niet; magnetometer wordt nergens gebruikt; geen drift- of yaw-correctie; er is alleen een orientation_reset_requested (cellGemSetYaw). IMU-versnelling voor snelheid staat uit ("massive drift", use_imu_for_velocity=false). Dus RPCS3 lost drift niet op, het levert recenter-knop.
- [ZOEK] RPCS3-gebruikers (forum) zeggen dat de cursor "WILL drift" en een recenter-knop essentieel is.
- Niet gevonden/bekeken: driver4vr, iPi, SteamVR-PSMove-bridge, Dolphin-code. Geen bron gelezen.

### 5. Yaw uit alleen camerapositie plus accelerometer
- [GELEZEN] PSMoveService doet dit niet: positie corrigeert de oriëntatie in geen enkel actief codepad (zie punt 1). Voor Sony geen bron gevonden. Geen meetgegevens over hoe goed het werkt.
- [AFGELEID] Theorie: yaw is observeerbaar als de bol horizontaal versnelt. Optische baan geeft wereldversnelling a_w (tweede afgeleide, ruis ~ positieruis/dt^2); accelerometer geeft R*(a_w + g). Vergelijking levert yaw via de horizontale component, maar alleen tijdens bewegingen met voldoende versnelling, en de gain is evenredig met de versnelling. Bij 60 fps met positieruis van enkele mm is de versnellingsruis groot (orde meters/s^2); alleen snelle schokken zijn bruikbaar. Realistisch verwachting: een grove, langzame correctie (tientallen seconden), geschikt om een onafhankelijke bias-drift te begrenzen, niet om 0,3 graden vast te houden. Dit moet in simulatie of op opnames getoetst worden. Bekend in de visual-inertial literatuur (yaw/positie zijn onobserveerbaar zonder beweging); niet door mij opgezocht.
- [AFGELEID] Sterker en goedkoper alternatief met dezelfde sensoren: de richtstraal zelf. Elk schot waarvan de speler het scherm raakt (of elke PS-druk) levert een yaw-fout als verschil tussen gerichte en bekende doelpositie.

### 6. Bluetooth-rapportverlies
- [ZOEK] GIMX-blog (https://blog.gimx.fr/bluetooth-latency-issues/, nu 404, alleen via zoeksamenvatting): sinds Linux 3.1 forceerde een kernelwijziging de zender uit sniff mode voor een PS3-controller; ging over latency, niet over verlies.
- [ZOEK] hcitool: lp toont/zet link policy (RSWITCH, HOLD, SNIFF, PARK), lst toont/zet link supervision timeout. Kernelcommit van Marcel Holtmann (04837f64, niet geopend wegens 429): sniff-uitstap kan HID-apparaten storen; via L2CAP-socketoptie te voorkomen. Waarschuwing: hcitool is verouderd, btmgmt/bluetoothctl.
- [ZOEK] Broadcom-patent over sniff en wifi-coexistentie; 2,4 GHz-interferentie is volgens een Ubuntu-bug de gebruikelijke oorzaak bij Intel-chips. Geen directe test van sniff-uit op HID gevonden.
- [GELEZEN] PSMoveAPI-pairingdocs (https://psmoveapi.readthedocs.io/en/latest/pairing.html): niets over verlies, link policy of dongles; alleen ClassicBondedOnly=false in /etc/bluetooth/input.cfg.
- [AFGELEID] Verwachte oorzaken: 2,4 GHz wifi-drukte (zeker als wifi en BT dezelfde chip delen), het Move-rapport wordt pas door de host als geheel gezien met sequentienummer (zie huidige code), host-polling. Oplossing is te testen: wifi naar 5 GHz of ethernet, aparte USB-BT-dongle op verlengkabel (USB3-storing op 2,4 GHz), sniff uit.

## (b) Aanbevelingen, gerangschikt

1. Gyro-biasschatting online, ook tijdens bewegen (Mahony-integral of ESKF-biastoestand, plus stilstandsdetectie over korte vensters, 0,3 s). Effect: grootste; haalt de 2,8 graden/s weg. Moeite laag tot middel. Risico laag. Reden: geen enkele bekeken implementatie doet dit goed (PSMoveService/PSMoveAPI hebben vaste of geen bias; alleen Madgwick MARG schat bias).
2. Richt-gebaseerde yaw-anker: elke PS-druk, en bij voorkeur elk schot met bekend doel of tweepunts-schermkalibratie zoals Sony (schermhoeken). Effect: hoog, absoluut. Moeite middel. Risico laag (spelerbelasting).
3. Magnetometer weer laten meetellen: ellipsoïdekalibratie (zoals PSMoveService-structuur), referentie (sterkte en dip) leren op de speelplek, zachte weging in plaats van harde afwijzing, trage correctie (orde 0,005 per update als bovengrens bij PSMoveService-optisch; 0,02 bij MARG is te snel). Effect: begrenst yaw op enkele graden bij een schone kamer; moeite middel; risico: sterk gestoord in en bij het geweer.
4. BT-verliesonderzoek: wifi/BT-scheiding, dongle op verlengkabel, sniff uit en link policy loggen, en gat-interpolatie op de gyro-subsamples (verlies tijdens beweging geeft integratiefout). Effect: middel (drift tijdens bewegen); moeite laag; risico laag.
5. ESKF op fout-quaternion zoals PSMoveService Kalman, maar met bias als toestand (dat doet PSMoveService niet) en variantiegewicht per meting. Effect: goede basis voor 1 en 3; moeite hoog; risico middel.
6. Camera-versnelling-yaw (punt 5) alleen als experiment op opnames; verwacht beperkt effect. Moeite hoog, onzeker.
7. Per-device gyro-schaal (en eventueel temperatuurmodel) schatten op opnames. Effect laag tot middel; moeite middel.

## (c) Niet gevonden of niet geverifieerd
- Sony's eigen filterontwerp, GDC-slides, patent over camerapositie plus accelerometer: niet gevonden.
- driver4vr, iPi, SteamVR-bridge, Dolphin: niet onderzocht (zoeken leverde niets bruikbaars op).
- PSMoveService-wiki/FAQ over aanbevolen stabiliteitsinstellingen en magnetometerkalibratie-procedure: niet bereikbaar (wiki leeg/zoekresultaten onbruikbaar). Alleen de code is gelezen; de fork-wiki zegt mogelijk dat het origineel verouderd is.
- psmoveapi/BlueZ-specifieke meldingen over 13 % verlies: geen gevonden; alle BT-tips zijn algemene tips of uit zoeksamenvattingen.
- De WebSearch-tool gaf samenvattingen; claims met [ZOEK] zijn niet zelf in de brontekst gecontroleerd.
