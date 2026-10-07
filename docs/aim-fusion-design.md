# Ontwerp: sensorfusie en richtgeometrie (Move ZCM1 in Sharp Shooter + PS3 Eye)

Status: ontwerp, nog niet gebouwd (7 okt 2026). Dit document beschrijft hoe RavenGun van de Move-IMU, de magnetometer en de optisch gevolgde bol een snelle en nauwkeurige cursor maakt. Het bouwt voort op `docs/architecture.md` (wat er nu draait), `docs/hardware.md` (metingen) en het voorstel "optische positie in een hybride modus" in `docs/decisions.md`. Het volgt sectie 10, 11 en 13 van de opdracht: geen homography als algemene oplossing, een expliciete aim-ray (p, d, s, n), geen 6DoF-claims, geen stille recentering.

Labels: "gemeten" betekent op deze host gemeten (bron erbij). "Uit de broncode" betekent gecontroleerd in de genoemde bron. "Schatting" is een berekening of aanname die nog gemeten moet worden.

## 0. Samenvatting

- De richting komt uit de gyro. Die is snel (ongeveer 153 subsamples/s), ruisarm en kent geen optische vertraging. De gyro bepaalt het gevoel.
- De positie van de richtstraal komt uit de camera: de 3D-positie van de bol, met de bolstraal als diepte. Dat is een gekalibreerd cameramodel met bolmaat 45 mm. Met één bol is er geen yaw-meting.
- Pitch en roll worden absoluut begrensd door de accelerometer.
- De yaw wordt begrensd door de magnetometer van de ZCM1. Die wordt zelf gekalibreerd, de verstoring wordt gedetecteerd en de correctie werkt langzaam en is begrensd in snelheid. Het optische model dient alleen als bewaker, plus een expliciete hercontrole. Een stille recenter is er niet.
- De oriëntatieschatter wordt een error-state Kalman-filter (ESKF) op oriëntatie en gyro-bias. De huidige Mahony-implementatie blijft bestaan als referentie en terugval. De ESKF wordt pas standaard als hij op opgenomen sporen aantoonbaar beter is.
- De schermpose, de schermgrootte, de rotatie tussen camera en wereld en de richtas worden samen gefit uit richtsamples vanaf twee à drie speelposities, met eenvoudige voorinformatie (tv-diagonaal, cameraplaats). De validatie gebeurt op onafhankelijke doelen en met leave-one-position-out.
- De uitvoer gaat op een vaste frequentie (250 Hz, instelbaar) naar uinput. Tussen twee rapporten wordt geëxtrapoleerd met de gyro, en de predictie is begrensd (standaard uit, daarna afgestemd op de gemeten latentie, maximaal 25 ms). Het 1€-filter staat op de schermcoördinaat, aan het eind van de keten.
- De harde waarheid: 1 % van de schermbreedte is op 2,5 m ongeveer 0,28° (bij een tv van 55 inch). Alleen met de IMU drift de yaw nu ongeveer 2°/min. Of de G2-eis (circa 1 % richtfout, drift over 5 min) haalbaar is, hangt af van hoe goed de magnetometer in deze kamer werkt. Dat is het eerste beslispunt in het implementatieplan (fase 3). Als de magnetometer faalt, staat er een escalatiepad klaar (sectie 8).

## 1. Vergelijking van bewezen aanpakken

### 1.1 PSMoveAPI (de gebruikte library, commit 256b629)

Wat de code doet (uit de broncode):

- Sensorschaling (`src/psmove_calibration.c`). De accelerometer is lineair, uit de USB-kalibratieblob met ±1 g-punten per as. Bij de ZCM1 is de gyrofactor `80 rpm · 2π/60 / ruwe waarde bij 80 rpm`, met offset `dx = dy = dz = 0`. PSMoveAPI trekt bij de ZCM1 dus géén nulpunt van de gyro af. De bias van ongeveer 1 tot 2°/s die we meten, is volledig onze verantwoordelijkheid. Kruisgevoeligheid en uitlijnfouten van de assen worden niet gekalibreerd.
- Magnetometer (`src/psmove.c`, `psmove_get_magnetometer_3axisvector`). De ruwe waarden zijn 12-bit signed, en er is één meting per rapport, niet per subsample. De genormaliseerde vector schaalt per as van [min, max] naar [−1, 1] en draait de Y-as om. Min en max worden bij elke aanroep bijgewerkt. Eén uitschieter (een speaker, een stalen tafel) vergroot het bereik dus blijvend en verschuift de nulpunten. Er is geen soft-iron-correctie (geen ellipsoïde, alleen per-as min/max). De tool `magnetometer_calibration.c` meet die extremen en daarna een referentierichting in een rechtopstaande pose.
- Oriëntatie (`src/psmove_orientation.cpp`). Voor de ZCM1 is de standaard `ComplementaryMARG`: een gyrostap plus een nlerp naar de oriëntatie uit zwaartekracht en magneetveld, met gewicht 0,02 per subsample. Bij ongeveer 150 subsamples/s is dat een tijdconstante van ongeveer 0,3 s. Ruis en verstoring van de magnetometer werken dus vrijwel direct door in alle drie de hoeken, ook in pitch en roll. De delta-t is `1/max(gemeten frequentie, 120)` en niet de echte tijd per rapport. Er is geen bias-schatting en geen poort tegen verstoring.
- Positie (`src/tracker/psmove_fusion.cpp`, `psmove_tracker.cpp`). De afstand komt uit de straal via een empirische Pearson-VII-fit (parameters "voor een PS Eye in groothoek met een magenta bol"). De 3D-positie gebruikt een OpenGL-perspectief met een vaste FOV-constante (`PSEYE_FOV_BLUE_DOT 75`) en zonder lensvervorming. De tracker zelf smoothet straal en positie adaptief.

Beoordeling: het principe (positie optisch, oriëntatie uit de IMU, yaw uit de magnetometer) is juist en is ook ons principe. De implementatie is niet geschikt voor een lightgun-vizier van 0,3°. De magnetometercorrectie is te snel en heeft geen poort, het min/max-model is fragiel, de dt is benaderd en het cameramodel is niet gekalibreerd. RavenGun gebruikt PSMoveAPI daarom alleen voor de HID-communicatie, de USB-kalibratie van accelerometer en gyro, en de ruwe magnetometerwaarden (`psmove_get_magnetometer`, al in `RawReport::mag_raw`). De eigen fusie van PSMoveAPI blijft uit, zoals nu.

### 1.2 PSMoveService (gearchiveerd in 2021)

Uit de broncode op GitHub (master, `src/psmoveservice/Filter/*`, `PSMoveController.*`, `psmoveconfigtool/AppStage_MagnetometerCalibration.cpp`):

- Standaard voor de PSMove: `orientation_filter_type = "ComplementaryMARG"` (dezelfde blend met gewicht 0,02 als PSMoveAPI), `position_filter_type = "LowPassExponential"` en `prediction_time = 0`.
- Alternatieven zijn er wel: Madgwick ARG/MARG, `ComplementaryOpticalARG` (schat oriëntatie uit de offset van de bol ten opzichte van de IMU) en een square-root unscented Kalman-filter op oriëntatie en positie (`KalmanOrientationFilter.cpp`, `KalmanPoseFilter.cpp`).
- Magnetometerkalibratie: 200 samples tijdens het ronddraaien, een least-squares-fit van een asgerichte ellipsoïde (of een bounding box), opslag van centrum, assen, fitfout en een gemeten variantie. Dat is een stuk robuuster dan het min/max-model van PSMoveAPI.
- De bolstraal is `PSMOVE_TRACKING_BULB_RADIUS 2.25f` cm (diameter 45 mm). Die waarde nemen we als startwaarde over (nog na te meten met een schuifmaat).

Beoordeling: dit is de meest volledige open implementatie van de Move-fusie. Ze is gebouwd voor VR (SteamVR, kamerschaal), niet voor nauwkeurig richten op een scherm. De ideeën nemen we over (ellipsoïdefit met kwaliteitsmaat, Kalman met expliciete onzekerheid, bekende bolmaat). De code nemen we niet over: gearchiveerd, Windows-georiënteerd, oude OpenCV, en een snelle ComplementaryMARG als standaard. Geen enkele bron laat zien dat PSMoveService op de PSMove een yaw-nauwkeurigheid van 0,3° haalt. Dat is voor ons dus een open meetvraag.

### 1.3 Sinden- en GUN4IR-achtige optische lightguns

- Sinden: een camera in het geweer ziet het scherm, en het spelbeeld krijgt een witte rand. Het trefpunt volgt direct uit de projectie van die rand in het camerabeeld.
- GUN4IR: een IR-camera in het geweer ziet vier IR-ledclusters, één midden op elke rand van de tv.

Met vier of meer bekende punten op het scherm is de pose van het geweer ten opzichte van het scherm volledig bepaald (homography/PnP). Het trefpunt is dan absoluut en driftvrij, ongeacht waar de speler staat. Daarom zijn deze geweren nauwkeurig.

Niet van toepassing: de Sharp Shooter en de Move hebben geen camera die naar het scherm kijkt. Onze camera staat stil bij de tv en kijkt naar de speler. Hij ziet één bol en dus geen oriëntatie. Dezelfde wiskunde (homography) geeft in onze opstelling niets, en dat is precies de valkuil uit sectie 10 van de opdracht. Wel blijft dit de escalatieroute als de yaw-referentie onvoldoende blijkt: een kleine IR-camera in de Sharp Shooter met IR-leds bij de tv (sectie 8).

### 1.4 Wii-pointer (Wiimote, sensor bar, MotionPlus)

De Wiimote heeft een IR-camera in de handcontroller (volgens WiiBrew tot vier IR-punten, gerapporteerd in 1024x768 met ongeveer 100 Hz) en ziet de twee ledclusters van de sensor bar. Twee punten geven de richting ten opzichte van de bar, de roll en een grove afstand (uit de afstand tussen de punten). De volledige positie geven ze niet. De cursor is een hoekmapping, net als onze huidige oriëntatiemodus. Stapt de speler opzij, dan verschuift de cursor ten opzichte van het werkelijke trefpunt. MotionPlus voegde een gyro toe voor snelle beweging.

Les voor RavenGun: de combinatie "een absolute maar trage of gevoelige referentie plus een snelle gyro" werkt in de praktijk goed voor het gevoel. Bij de Wii komt de absolute referentie optisch en richting-gevoelig binnen. Bij ons moet ze uit de magnetometer komen, met de camera voor de translatie. De architectuur van de Wii zelf (camera in de hand) hebben we niet.

### 1.5 Oculus Rift DK1/DK2 (magnetometer tegen yaw-drift)

LaValle e.a., "Head tracking for the Oculus Rift" (ICRA 2014), en de Oculus-blog over magnetometers. Daarin: een ellipsoïdefit voor hard- en soft-iron, "referentiepunten" (paren van een magnetometerwaarde en een oriëntatie) die alleen gebruikt worden binnen ongeveer 10° van de oriëntatie waarin ze zijn opgeslagen, en een geleidelijke complementaire correctie, zodat de gebruiker geen sprong ziet. Gemeten tegen motion capture: gemiddeld ongeveer 3,7° yaw-fout en maximaal ongeveer 10° in sessies van 20 minuten, voor een hoofd dat door een kamer beweegt. Daarnaast gyro-predictie tegen latentie.

Les voor RavenGun: dit is de beste publieke aanwijzing voor wat een magnetometer in een woonkamer kan. 3,7° is ruim tien keer te veel voor ons absolute doel. Een lightgun heeft wel twee grote voordelen: hij werkt in een smalle kegel (ongeveer ±25° yaw, ±15° pitch), en de speler staat vrijwel op één plek. Referentiepunten binnen die kegel, gekoppeld aan de schermkalibratie, kunnen de lokale magnetometerfout grotendeels wegkalibreren. Of dat 0,3° haalt, is onbekend en wordt gemeten (fase 3).

### 1.6 Conclusie van de vergelijking

Voor deze hardware is de enige werkbare aanpak "positie optisch, rotatie uit de IMU, yaw begrensd door een gekalibreerde en bewaakte magnetometer". Het optische model mag de yaw alleen bewaken. De optische lightgunaanpakken zijn nauwkeuriger, maar vragen andere hardware.

## 2. Aanbevolen fusie-architectuur

### 2.1 Coördinatenstelsels (expliciet, één keer per overgang)

- B, het controllerstelsel van de Move (PSMoveAPI): +Y van handvat naar bol, +Z uit de knoppenkant, rechtshandig (zie `docs/architecture.md`).
- W, het wereldstelsel: rechtshandig, Z omhoog. De yaw-nul is "magnetisch referentienoord" als de magnetometer actief is, en anders willekeurig per sessie.
- C, het camerastelsel (OpenCV-conventie): x naar rechts in het beeld, y naar beneden, z de kijkrichting in. Eenheid: meter.
- S, het schermstelsel: oorsprong `s0` is de linkerbovenhoek van het actieve beeld. `e_x` wijst naar rechts en `e_y` naar beneden, langs de beeldranden. `n = e_x × e_y` wijst het scherm in (van de speler af). Breedte `W_s` en hoogte `H_s = W_s · 9/16` (16:9 en 1920x1080 actief, gemeten in `STATUS.md`).

De quaternionconventie blijft die van `docs/architecture.md`: Hamilton, `q_WB` roteert van B naar W, en de gyrostap is `q ← q ⊗ exp(ω·dt/2)`.

Vaste of gekalibreerde grootheden:

- `R_CW`: rotatie van W naar C. Dat is 3 vrijheidsgraden, want de PS3 Eye heeft geen accelerometer. Wordt gefit bij de schermkalibratie. Zonder magnetometer moet de yaw-component elke sessie opnieuw worden vastgelegd (sectie 3.5).
- `d_B`: de richtas van het geweer in B. Dat is een eenheidsvector, dus 2 vrijheidsgraden, nominaal (0, 1, 0). Wordt gefit.
- `o_B`: de offset van het bolcentrum naar de oorsprong van de richtstraal in B. Die wordt nominaal opgemeten met een liniaal aan de Sharp Shooter (zie 4.3) en niet gefit (slecht waarneembaar, en weinig invloed).
- `R_bol = 22,5 mm` (PSMoveService) met een schaalfactor `k_r` die bij de schermkalibratie wordt gefit, binnen een prior van ±5 %.

### 2.2 Oriëntatieschatter: keuze en motivatie

Opties:

- Madgwick (gradient descent, Madgwick 2010/2011). Bij de MARG-variant werkt de magnetometer via het gecombineerde gradiënt ook door in pitch en roll. Er is één versterking β voor alles en geen expliciete onzekerheid. Een verstoring is daardoor niet netjes weg te poorten en lekt in de tilt.
- Mahony (niet-lineair complementair filter op SO(3), Mahony e.a. 2008). Het is eenvoudig en goed controleerbaar. Een yaw-only magnetometerterm en een PI-bias zijn mogelijk. Dit is de huidige basis (alleen P, met een aparte stilstands-bias). Nadeel: de versterkingen zijn vast, en een correctie die alleen op de yaw werkt, moet je met de hand afleiden. Een poort tegen verstoring wordt een losse heuristiek zonder voorspelde onzekerheid.
- Error-state Kalman-filter (multiplicatief, MEKF/ESKF; Markley 2003, Solà 2017). De toestand is de nominale `q_WB` plus de gyro-bias `b_g`. De fouttoestand is 6-dimensionaal (δθ, δb_g). De voorspelling gebeurt met elke gyrosubsample en echte dt. De metingen zijn de zwaartekrachtrichting (accelerometer, gepoort) en de magnetische yaw (alleen de horizontale component, zie 2.3).

Keuze: ESKF met 6 fouttoestanden. De redenen:

1. Gyro-bias waarneembaar in alle drie de assen. Met een yaw-referentie (magnetometer) is ook de Z-bias waarneembaar terwijl het geweer beweegt. Dat is precies de bron van de huidige drift van ongeveer 2°/min. De huidige schatter werkt de bias alleen bij stilstand bij, en in de hand is er bijna nooit stilstand.
2. Een principiële poort: de innovatie en haar voorspelde covariantie geven een NIS-test (χ², 1 vrijheidsgraad voor de yaw, 2 voor de tilt). Een verstoorde magnetometer of een versnelde beweging wordt daarmee meetbaar afgewezen in plaats van met een vaste drempel.
3. Onzekerheid als zichtbare grootheid: de yaw-σ uit de covariantie wordt een gedefinieerde degradatie-indicator, dus geen verzonnen confidence-percentage. De status meldt bijvoorbeeld "yaw-σ 0,4°, bron: gyro sinds 45 s".
4. De rekenkosten zijn verwaarloosbaar: 6x6-matrices bij 153 Hz zijn enkele microseconden.

Wat er niet in komt: positie en snelheid. De translatie gaat via een apart optisch filter (2.4), omdat de accelerometer bij deze ruis en tilt-fout geen bruikbare positie geeft boven enkele tientallen milliseconden. Een gekoppeld 15-toestanden-INS-filter voegt complexiteit toe zonder aangetoonde winst. Dat kan later, als een meting dat rechtvaardigt.

Bewijsplicht (sectie 11 van de opdracht: "alleen complexiteit als een vergelijking verbetering toont"): de ESKF komt naast de huidige Mahony-schatter te staan, achter een schakelaar (`fusion: mahony | eskf`). Beide draaien op dezelfde opgenomen sporen (fase 1). De ESKF wordt pas standaard als hij op drift (5 min), stilstandsjitter en dynamische consistentie minstens even goed is en op drift duidelijk beter. Een Mahony-variant met yaw-only magnetometer en PI-bias is het terugvalalternatief als de ESKF dat niet aantoont.

Details die we overnemen of verbeteren:

- Twee subsamples per rapport, oudste eerst. De dt komt uit de tijdbasis van sectie 2.6, niet uit `t_rx`/2 als de Move-tijdstempel bruikbaar blijkt.
- Bias per subsample. De meting toonde op de Z-as een systematisch verschil tussen `Frame_FirstHalf` en `Frame_SecondHalf` (1,73 tegenover 1,25°/s). De ESKF krijgt daarom een vaste, gekalibreerde subsample-offset op Z (uit een stilstandsmeting) bovenop de geschatte bias.
- Temperatuur. De ZCM1 levert een temperatuur (`psmove_get_temperature`). De bias wordt per temperatuur gelogd. Een lineair temperatuurmodel komt er alleen als de opname dat ondersteunt.
- De stilstandsdetector (`GyroBiasEstimator`) blijft bestaan. Hij levert de beginwaarde van de bias en een zero-rate-pseudometing met een kleine variantie bij aantoonbare stilstand (geen klem, gewoon een meting in het filter).
- Accelerometerpoort: `| |a| − 1 g | ≤ 0,15 g` plus de NIS-test. Een afgewezen meting telt mee in de status.

### 2.3 De magnetometer veilig meenemen

1. Ruwe data: `mag_raw` (12-bit, één per rapport) uit `psmove_get_magnetometer`, nooit de genormaliseerde vector van PSMoveAPI (zie 1.1). De Y-as wordt omgedraaid naar het B-stelsel, zoals PSMoveAPI dat ook doet ("y-axis is flipped compared to the accelerometer and gyro"). Dat bevestigen we op hardware met een draaiproef.
2. Kalibratie (procedure in 4.2): een volledige ellipsoïdefit (hard-iron-offset `b_m` en een symmetrische soft-iron-matrix `A`) met regularisatie en een terugval naar asgericht als de dekking onvoldoende is. Daarna is `m_cal = A·(m_raw − b_m)` ongeveer een eenheidsvector. Opgeslagen worden: de veldsterkte (ruw), de inclinatie (dip, de hoek tussen m en g) en de residuen.
3. Het gebruik in de ESKF is alleen yaw. `m_cal` wordt met de huidige tilt naar het horizontale vlak van W geroteerd. De meting is de horizontale hoek ten opzichte van de referentie. Zo kan een magnetometerverstoring nooit pitch of roll veranderen (anders dan bij Madgwick-MARG en ComplementaryMARG).
4. Een lokale correctie in de werkkegel (naar de "referentiepunten" van Oculus): bij de schermkalibratie zijn er tientallen samples waarvoor de echte oriëntatie bekend is (uit de fit). Daaruit volgt een residu "magnetometer-yaw min fit-yaw" als functie van yaw en pitch binnen de kegel, gefit met een constante plus een lineaire term. Daarmee wordt de resterende soft-iron-fout in het gebied waar gespeeld wordt grotendeels verwijderd. Buiten de kalibratiekegel (meer dan 10° voorbij de doelen) valt de magnetometer-update terug op een grotere variantie.
5. Verstoringsdetectie. Elk magnetometersample wordt afgewezen als:
   - `|m_cal|` meer dan 5 % afwijkt van de gekalibreerde 1,0, of
   - de dip meer dan 2° afwijkt van de referentie, of
   - de yaw-innovatie de NIS-poort (99 %) niet haalt, of
   - de magnetometer-yaw over 0,5 s meer dan 2°/s anders draait dan de gyro zegt (een bewegend of inhomogeen veld), of
   - de rumble actief is (motor en stroom in de controller).

   Na meer dan 1 s aaneengesloten afwijzing wordt de toestand "magnetometer verstoord" (zichtbaar). De yaw loopt dan op de gyro, met een groeiende σ. Herstel volgt pas na 2 s consistente samples. Het opgebouwde verschil wordt daarna alleen via de snelheidsbegrensde correctie van 3.2 weggewerkt, nooit met een sprong. De drempels zijn startwaarden en worden in fase 3 op de gemeten ruis afgesteld.

### 2.4 3D-positie uit de bol

Model (pinhole met radiale vervorming k1, eventueel k2): na het corrigeren van de vervorming van het zwaartepunt `(u, v)` en de straal `r` (pixels) volgt:

```
Z = k_r · f · R_bol / r
X = (u − c_x) · Z / f
Y = (v − c_y) · Z / f
```

Dit is een benadering. Voor een bol buiten de optische as is de projectie een ellips, en het zwaartepunt van de schijf ligt dan iets naast de projectie van het middelpunt. Bij een bol van 45 mm op 2 à 3 m is die fout kleiner dan 0,1 px en verwaarloosbaar. Bij de hercontrole wordt dat bevestigd.

Een fout in f verandert vooral Z (schaal), en X en Y nauwelijks: (u − c_x)·(k f R/r)/(k f) = (u − c_x)·R/r. Een onnauwkeurige f wordt dus grotendeels opgevangen door de gefitte schaalfactor `k_r`. Dat maakt een kalibratie zonder schaakbord verantwoord (4.1).

Onzekerheid op speelafstand. Dit is een schatting met f = 520 px bij 640x480. Dat past bij de nominale diagonale FOV van 75° ("blue dot"). Met de gemeten straal van 19,1 px volgt daaruit Z ≈ 0,61 m, en dat strookt met de geschatte 0,5–0,75 m uit `docs/hardware.md`. Bij de lensstand "red dot" (56°) is f ongeveer 750 px en zijn alle stralen ongeveer 1,45x groter. Welke stand de lens heeft, wordt bij kalibratie bepaald.

- Straal bij 640x480: ongeveer 5,9 px op 2 m (oppervlak ongeveer 107 px), 4,7 px op 2,5 m (69 px) en 3,9 px op 3 m (48 px).
- Straal bij 320x240 (f ≈ 260): ongeveer 2,9 px op 2 m (27 px), 2,3 px op 2,5 m (17 px) en 2,0 px op 3 m (12 px). Dat laatste is precies de ondergrens van de tracker (`min_area` 12 px). Daar is detectie niet betrouwbaar.
- De relatieve dieptefout is σ_Z/Z = σ_r/r. In stilstand is gemeten σ_r ≈ 0,015 px bij 19 px. Bij een kleine bol, beweging en bewegingsonscherpte verwacht ik een toevallige σ_r van 0,05 tot 0,2 px en een systematische afwijking door drempel, halo en onscherpte van 0,3 px of meer.
  - 640x480 op 2,5 m: toevallig 1–4 % (3–11 cm), systematisch ongeveer 6 % (16 cm).
  - 320x240 op 2,5 m: toevallig 2–9 % (5–21 cm), systematisch ongeveer 13 % (32 cm).
- De zijwaartse onzekerheid door de pixelruis is klein: σ_X = Z·σ_u/f. Bij 640x480 op 2,5 m is dat 0,2–1 mm, bij 320x240 0,5–2 mm. De meeste zijwaartse fout komt uit de diepte (zie hieronder).

Wat dat betekent voor het trefpunt: een fout δ langs de kijklijn van de camera naar de bol verschuift het trefpunt ongeveer met δ·tan θ. Daarin is θ de hoek tussen die kijklijn en de richtstraal. Richt de speler op de camera, dan heft de fout zichzelf op (θ ≈ 0). Richt hij vanaf 0,5 m opzij op de verste schermrand, dan is tan θ ≈ 0,45. Een systematische fout van 16 cm zou daar 7 cm (ongeveer 6 % van de schermbreedte) kosten. Daarom:

1. wordt de systematische diepteschaal gefit (`k_r`) en gevalideerd op meerdere afstanden;
2. wordt de diepte zwaar gefilterd (tijdconstante ongeveer 0,25 s; de diepte van een staande speler verandert langzaam). Bij een toevallige fout van 3–11 cm blijft er na filtering ongeveer 1–3 cm over, en op het scherm 0,4–1,3 cm (ongeveer 0,3–1 %) in het slechtste geval;
3. blijft 640x480@60 de modus. 320x240@125 halveert de straal, verdubbelt de dieptefout en zit op 3 m op de detectiegrens. De lagere optische latentie levert weinig op, omdat de snelle richting uit de gyro komt.

Optisch positiefilter: per as (in C) een Kalman-filter met constante snelheid. De metingsvariantie hangt af van de diepte: zijwaarts uit σ_u en diepte, langs Z uit σ_r. De procesruis is laag voor Z (traag) en hoger voor X en Y. Uitbijters (een sprong die de tracker al gemarkeerd heeft) worden niet gebruikt. Het filter levert de positie en de snelheid op de belichtingstijd van het frame (2.6). Voor de uitvoer wordt het naar de uitvoertijd voorspeld, met de snelheid en maximaal 50 ms vooruit.

Mogelijke verbetering (fase 2, alleen bij meetbare winst): een subpixel-randfit van de cirkel in plaats van de straal uit het oppervlak. Dat geeft een kleinere systematische drempelafhankelijkheid. Verder een langgerektheidscriterium dat rekening houdt met de voorspelde beeldsnelheid. Een snel bewegende bol wordt bij exposure 50 een ellips, en de huidige grens (√(λ1/λ2) ≤ 1,8) kan hem dan als `not_round` afwijzen. Dat is nog niet gemeten (zie risico's).

### 2.5 Geometrische aim-ray naar het schermvlak

Per uitvoermoment t:

```
q   = q_WB(t)                       (ESKF, voorspeld naar t; 2.7)
p_b = p_C(t)                        (optisch positiefilter, voorspeld naar t)
R   = R_CW · R(q)                   (B → C)
p   = p_b + R · o_B                 oorsprong van de richtstraal in C
d   = normalize(R · d_B)            richting in C
t_h = dot(n, s0 − p) / dot(n, d)
hit = p + t_h · d
x   = dot(e_x, hit − s0) / W_s      genormaliseerd, unclamped
y   = dot(e_y, hit − s0) / H_s
```

Ongeldig (met een reden in het sample):

- als `t_h ≤ 0` (wijst van het scherm af);
- als `|dot(n, d)| < 0,17` (meer dan ongeveer 80° schuin, bijna parallel);
- als er geen geldige oriëntatie is (link stale/lost);
- als de schermkalibratie ontbreekt of verouderd is;
- als de positie ouder is dan `max_position_age` (zie 5.3).

`off_screen` en de geclampte waarden blijven zoals in het huidige inputmodel. De mapping van het fysieke scherm naar het spelviewport (letterboxing, profiel) is een aparte, spelspecifieke stap daarna. Die gebeurt precies één keer.

Modi, volgens sectie 10:

- `optical_diagnostic`: de bolpositie als cursor (alleen voor tests).
- `orientation`: de huidige hoekmapping. Blijft beschikbaar als er geen schermkalibratie is.
- `geometric_ray`: zoals hierboven. Dit wordt de standaard nadat de kalibratie is gevalideerd (6.1). Is de kalibratie ongeldig, dan blijft de modus zichtbaar `orientation`.

Een tussenmodus "hybride met hoekmapping plus translatiecompensatie" bouwen we niet apart, want de geometrische straal omvat hem.

### 2.6 Camera en IMU vullen elkaar aan, en de tijdsuitlijning

De verdeling:

- Rotatie: gyro (snel), plus accelerometer (tilt) en magnetometer (yaw) als langzame referenties.
- Translatie: alleen de camera. De accelerometer wordt hooguit gebruikt om de positie tussen frames te voorspellen, en alleen als een meting winst laat zien.
- De camera meet nooit yaw. Het optische model wordt alleen gebruikt als bewaker (3.3).

Tijdbasis. Alles in `CLOCK_MONOTONIC`.

- IMU. `t_rx` is de ontvangsttijd en bevat Bluetooth-planning met een tweetoppige spreiding (intervallen van ongeveer 6 en 20 ms). Voorstel: de interne tijdstempel van de Move gebruiken. Volgens de struct-commentaren van PSMoveAPI staat die bij de ZCM1 als `timehigh` op offset 0x0B en `timelow` aan het eind van het rapport. PSMoveService bouwt hem als `timelow | (timehigh << 8)`. De eenheid en de wrap zijn onbekend en worden in fase 1 gemeten. Met die stempel volgt een klokkoppeling met de ondergrensmethode: een lineaire fit van (t_rx − t_move) over de minimale vertragingen in een glijdend venster. Dat geeft monotone sampletijden met een jitter van ongeveer 1 ms in plaats van ±7 ms, plus de echte subsample-afstand. PSMoveAPI geeft die stempel niet via de publieke API (zie `docs/architecture.md`). Opties: een kleine, gedocumenteerde patch op de gepinde PSMoveAPI-revisie die `timestamp` en `temperature` per rapport doorgeeft, of zelf het rapport uit hidraw parsen. De keuze valt in fase 1. Daar wordt ook bepaald wat het overslaande 4-bits volgnummer betekent (1010 keer in 85 s): als de stempels gelijkmatig doorlopen terwijl het volgnummer springt, gaat er geen data verloren.
- Camera. `t_EOF` is de V4L2-stempel (gemeten: monotonic, bron EOF). De belichtingstijd van de bol ligt eerder: `t_bol = t_EOF − δ_cam − (H − v)/H · T_uitlees`. Daarin is `T_uitlees` de rolling-shutteruitleestijd (vermoedelijk bijna één frameperiode; de sensor van de PS3 Eye is naar verwachting rolling shutter, te bevestigen) en `δ_cam` een vaste rest (belichting/2 en de USB-overdracht van de laatste regels). `δ_cam` wordt gemeten:
  1. Een kruiscorrelatie tussen de gyro en het beeld. Bij een snelle pols-wiebelbeweging (draaipunt in de hand) is de horizontale beeldsnelheid van de bol vrijwel evenredig met de yaw-snelheid. De piek van de kruiscorrelatie geeft de tijdsverschuiving, en een herhaling geeft de spreiding (het principe van Furgale e.a. 2013). Doel: een onzekerheid kleiner dan 2 ms.
  2. Als controle de LED-schakelproef: de daemon wisselt de LED-kleur op een bekend moment. Het eerste frame met de nieuwe kleur geeft een bovengrens voor de keten LED-commando, Bluetooth, belichting en EOF.
- Combineren. De oriëntatie heeft geen vertraagde metingen (de magnetometer zit in hetzelfde rapport). Het optische positiefilter verwerkt elke meting op `t_bol` en voorspelt naar de uitvoertijd. Voor de optische bewaking (3.3) houdt de HID-thread een ringbuffer bij met de oriëntatie van de laatste 250 ms, en vergelijkt hij op `t_bol` (interpolatie tussen subsamples).

Threads: geen nieuwe thread nodig. De tracker-thread publiceert de laatste optische meting (bestaand). De HID-thread neemt die mee in zijn lus. Die lus pollt al elke milliseconde en verzorgt ook de uitvoer op vaste frequentie (5.2).

### 2.7 Latentiecompensatie: begrensde gyro-predictie

- Wat: de oriëntatie wordt naar `t_uit + h` voorgezet met de laatste biasgecorrigeerde hoeksnelheid. Die is licht gemiddeld over de twee subsamples van het laatste rapport, om ruis te dempen. Formule: `q(t_uit + h) = q(t_s) ⊗ exp(ω · (t_uit + h − t_s)/2)`, met `t_s` de tijd van de laatste subsample. De positie gaat met de optische snelheid naar dezelfde tijd.
- Begrenzing:
  - `h` (predictie voorbij nu) staat standaard op 0 en is instelbaar tot 25 ms;
  - de totale extrapolatie `t_uit + h − t_s` is maximaal 40 ms, daarna wordt de laatste waarde vastgehouden (en na 50 ms wordt de link stale, zoals nu);
  - de voorspelde extra hoek is maximaal 1,0°.
- Tegen doorschieten: is de hoeksnelheid tussen de twee laatste subsamples met meer dan 50 % gedaald (remmen of omkeren), dan wordt h evenredig verkleind. Een harde stop geeft zonder die maatregel een overshoot van ongeveer ω·h gedurende één rapportinterval (bij 100°/s en 15 ms is dat 1,5°, en dat is zichtbaar). Met de maatregel is het doel ≤ 0,5 % schermbreedte (6.6).
- Afstemmen: `h` wordt pas ingesteld nadat de latentieketen gemeten is (5.1). `h` compenseert alleen het deel dat we kennen (Bluetooth, uitvoer naar de game) en nooit de hele keten blind.
- Predictie, het 1€-filter en de hysterese zijn elk afzonderlijk aan of uit te zetten (sectie 11).

### 2.8 Waar het 1€-filter komt

Het 1€-filter (Casiez e.a. 2012) komt na de geometrie en de predictie, op de genormaliseerde schermcoördinaat (x, y unclamped) en vóór de hysterese en het clampen. Redenen:

- de parameters van het filter zijn waarnemingsgrootheden (jitter in schermeenheden tegenover vertraging), zoals het filter bedoeld is;
- filteren op een quaternion of op de positie apart zou de fouten van de geometrie juist niet dempen;
- het positiefilter (2.4) is een schatter met een fysisch model en geen cursorfilter. Het staat apart in de status, met eigen schakelaars.

De werkelijke dt per uitvoertik wordt gebruikt. Het filter wordt gereset bij een modus- of kalibratiewissel, bij verlies en bij herverbinden (zoals nu). De startwaarden blijven min_cutoff 1,0 Hz, beta 0,5 en d_cutoff 1,0 Hz. Die worden opnieuw afgesteld op de nieuwe schaal, waarin 1 eenheid de schermbreedte is.

## 3. Driftbeheersing

### 3.1 Wat begrenst de yaw?

- De pitch en de roll worden absoluut begrensd door de zwaartekracht (accelerometer), zoals nu. Gemeten: +0,19° pitchdrift in 60 s.
- De yaw wordt continu begrensd door de magnetometer, als die de kwaliteitstest van fase 3 haalt. Daarnaast schat de ESKF met die referentie de Z-bias van de gyro, waardoor de drift tussen correcties sterk afneemt.
- Het optische model begrenst de yaw niet continu. Het bewaakt alleen (3.3), en het levert een absolute referentie op expliciete momenten: kalibratie, hercontrole en een bewuste uitlijnactie.
- Haalt de magnetometer de test niet, dan is er alleen een gyro met een betere bias, plus de expliciete uitlijnactie. De G2-drifteis wordt dan waarschijnlijk niet gehaald. Dat wordt dan zo gerapporteerd, met de escalatie uit sectie 8.

De begroting (schatting voor een tv van 55 inch, W_s ≈ 1,21 m, op 2,5 m): 1 % breedte ≈ 0,28°. De gyroruis van ongeveer 1°/s per subsample geeft een random walk van ongeveer 0,08°/√s, dus ongeveer 0,6° na 1 minuut en ongeveer 1,4° na 5 minuten zonder referentie. Dat is los van de bias. Alleen de IMU haalt de G2-drifteis dus principieel niet, ook niet met een perfecte bias.

### 3.2 Hoe snel, zonder "verschuivend vizier"

- De effectieve tijdconstante van de magnetometercorrectie in de ESKF is 5 tot 20 s. Die volgt uit de gemeten ruis van de magnetometer-yaw, niet uit een gok. Een ruis van 1° per sample bij 76 Hz en τ = 5 s geeft ongeveer 0,04° restruis. De bias-schatting maakt zo'n lange τ mogelijk zonder dat de drift oploopt.
- Snelheidsbegrenzing van elke yaw-correctie (op de nominale toestand): maximaal 0,02°/s bij een stilstaand geweer (|ω| < 3°/s) en maximaal 0,2°/s tijdens beweging. Correcties vallen zo in de beweging van de speler, waar ze niet opvallen. Bij stilstand zijn ze kleiner dan de hand kan zien (0,02°/s ≈ 0,07 % breedte per s).
- Er komt nooit een sprong. Na een periode van verstoring wordt het verschil met dezelfde begrenzing weggewerkt. De status toont het openstaande verschil.
- Er is geen automatische recenter. Er is ook geen zero-velocity-klem: de stilstandsmeting gaat als meting met een variantie in het filter en zet de integratie niet stil.

### 3.3 Optische bewaking (geen correctie, standaard)

Aanname "draaipunt in de hand": bij een draaiing om de hand beweegt de bol zijwaarts met ongeveer L·Δψ, met L ongeveer 0,1 tot 0,3 m bij een Sharp Shooter met de kolf tegen de schouder. Dat wordt gekalibreerd. Uit de bolverplaatsing en de gyrodraaiing volgt een residu. Een translatie die de gyro niet verklaart, betekent dat de speler zich verplaatst, en dan doet de bewaking niets. Een systematisch residu over tientallen seconden betekent een verdachte yaw. De status meldt dan "yaw verdacht" en adviseert een hercontrole. Een correctie op basis hiervan blijft een optie die standaard uit staat en altijd zichtbaar is (het voorstel in `docs/decisions.md`, stap 3). Die optie komt alleen aan na een meting die laat zien dat ze vaker helpt dan schaadt.

### 3.4 Faalmodi van de magnetometer en wat er dan gebeurt

- Stalen tafel of radiator (statisch, plaatsafhankelijk). Dit is alleen een probleem als de speler zich er dicht bij beweegt. Detectie: veldsterkte en dip wijken af bij de veldcheck (4.2). Gevolg: in die zone "magnetometer verstoord", met gyro-only. De veldcheck in de kalibratie toont dit vooraf.
- Speakers, subwoofer of soundbar (statische magneten, plus wisselvelden bij hoog volume). Dicht bij de speler gevaarlijk, op 2–3 m meestal verwaarloosbaar. Detectie: dip en norm, en een magnetometer-yaw die tegen de gyro in draait. Meten met muziek aan en uit.
- Tv. Een lcd/oled heeft weinig statisch veld, maar luidsprekers en voeding zitten op 2–3 m van de speler. Meten bij aan en uit. Een crt zou wel een probleem zijn, maar die is hier niet aanwezig.
- Eigen veld van de controller. Ledstroom (PWM), rumble-motor en laadstroom. De led-kleur en -helderheid liggen vast (cyaan, elke seconde ververst). Dat de led geen invloed heeft, moet gemeten worden. Tijdens de rumble wordt de magnetometer afgewezen.
- Sharp Shooter. Stalen veren, schroeven en het pompmechaniek bewegen mee met de Move. De vaste delen horen bij de hard- en soft-iron en worden mee gekalibreerd, mits de kalibratie in de Sharp Shooter gebeurt. Bewegende delen (pomp, trekker) kunnen het veld per stand veranderen. Meten in elke stand.
- Laden aan de USB-kabel: niet tijdens het spelen. Als hij laadt, staat de magnetometer uit.

Ieder van deze punten is een meetpunt in fase 3.

### 3.5 Zonder magnetometer: elke sessie bewust uitlijnen

Zonder een bruikbare magnetometer is de wereld-yaw per sessie willekeurig. Dan is de yaw-component van `R_CW` onbekend. De oplossing is een bewuste uitlijning van 3 s: de kalibratieclient toont vóór de gamestart één middendoel, de speler richt en trekt de trekker over, en alleen de yaw-offset wordt gezet. Tijdens het spel is er een expliciete knopcombinatie (bijvoorbeeld de Move-knop 1 s ingedrukt houden, nog te kiezen per profiel), die de status "uitgelijnd door gebruiker" geeft. Dit is geen stille recenter: de gebruiker doet het, en het is zichtbaar.

## 4. Kalibratieprocedure voor de gebruiker

Rolverdeling: de fullscreen-kalibratieclient op de tv toont de doelen en de voortgang, en bij het verzamelen géén cursor (anders lijnt de gebruiker de cursor uit in plaats van het geweer). De web-UI is de bediening: starten, stap kiezen, kwaliteit bekijken, accepteren of verwerpen. De daemon bezit alle hardware, doet het samplen (stilstand plus trekker) en fit. De IPC-commando's daarvoor bestaan nog niet. Voorgesteld zijn getypeerde commando's in de vorm `calib_start`, `calib_sample`, `calib_fit`, `calib_accept` en `calib_abort`. De namen zijn voorlopig.

Opslag volgens sectie 11: atomisch en geversioneerd, met controller-ID, camera-identiteit (by-id), cameramodus, exposure en gain, de lensstand, het fysieke scherm (connector, resolutie) en het tijdstip. Per laag apart: camera-intrinsics, magnetometer (per controller en Sharp Shooter), richtas (per controller en houder), schermgeometrie (per camerapositie en tv) en spelviewport (per profiel). Een gewijzigde laag maakt alleen de lagen die ervan afhangen ongeldig.

### 4.1 Camera-intrinsics (eenmalig per camera en lensstand; ongeveer 4 minuten)

Zonder schaakbord, met een rolmaat en een rechte lijn:

1. Lensstand. De gebruiker kiest in de web-UI de stand van de zoomring van de PS3 Eye ("blue dot" of "red dot") en vergrendelt die met een streepje tape.
2. Brandpuntsafstand met een rolmaat (ongeveer 2 minuten). De Move ligt stil (op een boek of statief) recht voor de camera op 1,00 m, daarna op 2,00 m. Gemeten wordt van de lens tot het bolcentrum. Per stand 2 s samplen. `f·R_bol = r·Z` geeft f, en de twee standen moeten binnen 3 % overeenkomen. Lukt dat niet, dan klopt de meting niet en wordt de stap herhaald.
3. Vervorming met de loodlijnmethode (ongeveer 2 minuten; Devernay en Faugeras 2001, "Straight lines have to be straight"). De gebruiker schuift de Move langzaam langs een rechte rand (tafelrand, deurpost of gespannen touw) van de ene naar de andere beeldrand, en doet dat horizontaal en verticaal, in het bovenste, middelste en onderste deel van het beeld. De baan van de bol moet na correctie recht zijn. Daaruit volgen k1 (en eventueel k2) en het middelpunt (c_x, c_y). Kwaliteitsmaat: de RMS-afwijking van een rechte lijn, kleiner dan 0,3 px.
4. Een alternatief voor gevorderden: een geprint schaakbord met OpenCV in de container. De resultaten moeten overeenkomen met 2 en 3.

Validatie: een derde rolmaatafstand (bijvoorbeeld 2,50 m) wordt voorspeld en gemeten. De dieptefout moet kleiner zijn dan 3 %. Een andere cameramodus (320x240) schaalt de intrinsics en vraagt geen nieuwe meting, maar wel een hercontrole.

### 4.2 Magnetometer (per controller in de Sharp Shooter; ongeveer 2 minuten)

1. De Move zit in de Sharp Shooter, de led staat op de speelkleur, er is geen rumble en geen kabel. De speler staat op de speelplek.
2. Ronddraaien (60–90 s). Het geweer wordt langzaam in alle richtingen gedraaid: achtjes, kolf omhoog en omlaag, roll om de loop. Op de tv staat een bol met vakjes (bijvoorbeeld 20 richtingen) die groen worden zodra ze gedekt zijn. De stap eindigt bij een dekking van minstens 80 % en minstens 300 samples.
3. Fit: een volledige ellipsoïde met regularisatie, met een terugval naar asgericht. Kwaliteit:
   - de RMS van |m_cal| − 1 is kleiner dan 2 %;
   - de spreiding van de dip (hoek tussen m en g) over alle samples is kleiner dan 1,5°.

   Wordt een van beide niet gehaald, dan meldt de web-UI concreet wat er mis is ("veld niet homogeen: verder van de tafel of speaker draaien" of "te weinig dekking").
4. Veldcheck (30 s). Het geweer wordt 3 s stil op de speelrichting gehouden op drie plekken in het speelgebied (midden, 0,5 m links, 0,5 m rechts), en daarna met de pomp en de trekker in elke stand. De norm en de dip moeten binnen de drempels van 2.3 blijven. Anders wordt de magnetometer voor die plek gemarkeerd als "onbetrouwbaar", en blijft de modus gyro-only met uitlijnen (3.5).
5. De lokale yaw-correctie in de werkkegel volgt automatisch uit de schermkalibratie (2.3 punt 4).

### 4.3 Offset van de richtas (onderdeel van de schermkalibratie; ongeveer 1 minuut extra)

- `o_B` (bolcentrum naar de richtlijn): wordt één keer met een liniaal opgemeten aan de Sharp Shooter, als de loodrechte afstand van het bolcentrum tot de lijn waarlangs de speler richt (de bovenkant van de loop). Dat is nominaal een paar centimeter. Het wordt niet gefit: de invloed is klein en vrijwel constant binnen de kegel.
- `d_B` (2 vrijheidsgraden) wordt gefit met de schermdoelen, plus een kanteltest: richt op het middendoel en kantel het geweer langzaam ±30° om de loop. Rond de echte richtas beweegt het trefpunt niet. De as die die beweging minimaliseert, is direct waarneembaar.
- Optioneel hulpmiddel voor objectieve metingen: een goedkope laserpointer (klasse 2, nooit op ogen richten) parallel aan de loop vastgeklemd. De laservlek op het doel is dan de grondwaarheid voor kalibratie en validatie, zonder menselijke richtfout. Aanbevolen voor de acceptatiemetingen (6.1).

### 4.4 Schermpose en schermgrootte (ongeveer 4 minuten), plus een snelle hercontrole (ongeveer 30 s)

Voorinformatie in de web-UI (maakt de fit robuust):

- de tv-diagonaal in inches (van de doos of het typeplaatje), met 16:9;
- de plaats van de camera: boven of onder het scherm, en de afstand van het lensmidden tot de rand en tot het midden van die rand (rolmaat, ongeveer 1 cm nauwkeurig). Zet de camera stevig vast (tape of klem), want een verschoven camera maakt deze laag ongeldig.

Procedure:

1. Positie 1: midden, op de gewone speelafstand (ongeveer 2,5 m). Er verschijnen 9 doelen in een 3x3-raster (10 % van de randen af), één voor één in willekeurige volgorde. Richt, houd stil (de daemon wacht op een stilstand van 300 ms: |ω| < 3°/s en een geldige bol) en trek de trekker over. De daemon middelt het venster van 300 ms vóór de trekker. Ongeveer 4 s per doel.
2. Positie 2: ongeveer 0,5 m naar links en 0,5 m dichterbij, dezelfde 9 doelen.
3. Positie 3: ongeveer 0,5 m naar rechts en 0,5 m verder weg, dezelfde 9 doelen. Twee posities is het minimum. Drie is de standaard, omdat schermafstand en schermgrootte vanaf één plek niet te onderscheiden zijn: een groter scherm verder weg geeft dezelfde hoeken. De parallax tussen de posities maakt ze waarneembaar.
4. Kanteltest (4.3).
5. Fit: niet-lineaire kleinste kwadraten (Levenberg-Marquardt) met een robuuste Huber-kostfunctie, over:
   - `R_CW` (3);
   - `s0` (3);
   - de schermoriëntatie (3, met de prior "de onderrand staat horizontaal" en een zwakke prior "het scherm staat verticaal");
   - `W_s` (1, prior uit de diagonaal ±3 %);
   - `d_B` (2);
   - `k_r` (1, prior 1 ± 5 %).

   Dat zijn 13 parameters tegenover 54 vergelijkingen. Het residu per doel wordt gemeten in % van de schermbreedte. De magnetometer staat tijdens het samplen aan (als die bruikbaar is), zodat de lokale correctie (2.3) mee kan worden gefit.
6. Waarneembaarheid. De daemon rapporteert het conditiegetal en de σ per parameter uit de Jacobiaan. Is een parameter slecht bepaald (bijvoorbeeld een σ van `W_s` boven 2 %), dan vraagt de web-UI om een extra positie in plaats van een slechte fit te accepteren.

Validatie op onafhankelijke punten (ongeveer 1 minuut):

- Vanaf een vierde positie, of vanaf positie 1 met een andere houding, verschijnen 8 nieuwe doelen die niet in het kalibratieraster zaten: 4 tussen de rasterpunten en 4 dicht bij de randen (5 %). Gerapporteerd worden de mediaan, p95 en maximum van de fout in % van de schermbreedte. Dit zijn de getallen die tellen, niet de fitresiduen.
- Leave-one-position-out: fit op twee posities en voorspel de derde. Dat laat zien of de kalibratie generaliseert naar een nieuwe standplaats.
- Acceptatie in de web-UI: mediaan ≤ 1 % en p95 ≤ 2 % (zie 6.1). Anders wordt de kalibratie niet actief, en wordt getoond welk deel slecht is (bijvoorbeeld systematisch links: de schermpose; een fout die groeit met de zijwaartse positie: `k_r` of de vervorming).

Snelle hercontrole (ongeveer 30 s, bijvoorbeeld vóór elke sessie of na een vermoeden):

- 5 doelen (midden en vier diagonale punten op 25 %) vanaf de huidige plek.
- Mediaan ≤ 1,5 %: oké.
- Is de fout een consistente verschuiving, dan wordt alleen een yaw/pitch-offset op `R_CW` aangeboden. De gebruiker bevestigt die expliciet.
- Is de fout een patroon (schaal of draaiing), dan wordt een volledige herkalibratie van de schermlaag geadviseerd. Een verplaatste camera geeft vrijwel altijd zo'n patroon.

Totale tijd van een eerste volledige kalibratie: ongeveer 12 tot 15 minuten. Daarna dagelijks alleen de hercontrole (30 s) en, zonder magnetometer, de uitlijning van 3 s.

## 5. Gevoel en snelheid

### 5.1 Latentiebegroting per schakel

Van beweging tot foton. Waarden zonder "gemeten" zijn schattingen of onbekend.

1. Bemonstering door de sensor: gemiddeld ongeveer 3 ms (subsampleperiode ongeveer 6,5 ms), plus het interne laagdoorlaatfilter van de MEMS (onbekend, typisch 1–5 ms). Niet meetbaar zonder externe referentie.
2. Bundeling in het rapport: de oudste subsample is ongeveer 6,5 ms ouder. Meetbaar met de Move-stempel.
3. Bluetooth tot de host: onbekend. De spreiding is gemeten (intervallen tweetoppig, rond 6 en 20 ms, p95 21 ms, max 37 ms). De absolute waarde wordt geschat met de klokkoppeling uit 2.6 (alleen de variatie) en met de externe meting hieronder.
4. Kernel en hidraw tot de poll-lus: 0–1 ms (de lus slaapt 1 ms). Te verbeteren door het hidraw-fd mee in `poll()` te nemen. Meetbaar met interne stempels.
5. Fusie, straal en filters: gemeten 2–3 µs voor de huidige keten. Verwacht na uitbreiding minder dan 50 µs. Meetbaar.
6. Vertraging van het 1€-filter: hangt af van de snelheid (bij langzame beweging bewust tientallen ms, bij snelle beweging enkele ms). Meetbaar als groepsvertraging op opgenomen sporen.
7. Van uinput tot de game: de kernel is minder dan 1 ms. Hoe vaak Wine/BudgieLoader evdev uitleest is onbekend. Meetbaar met een eigen evdev-lezer naast de game (alleen het kerneldeel).
8. De game: leest de invoer en rendert, typisch 1–2 frames (17–33 ms). Onbekend.
9. Compositor en vsync: 0–17 ms. Onbekend (Desktop Mode met KWin, Gaming Mode met gamescope).
10. Verwerking in de tv: met de spelmodus van de LG typisch 10–20 ms, zonder spelmodus vaak 50 ms of meer. De spelmodus moet aanstaan. Meetbaar met een hogesnelheidscamera.
11. Paneel en scanout: gemiddeld ongeveer 8 ms.

Optisch pad (alleen translatie): belichting (de omrekening van exposure 50 naar ms is onbekend), uitlezen en USB tot EOF (ongeveer één frameperiode, 16,7 ms) en tracking (gemeten p95 146 µs). Een positieleeftijd van ongeveer 20–30 ms, gecompenseerd met de positiesnelheid. Dat raakt alleen de translatie.

Meetmethoden:

- intern: tijdstempels van `t_rx` tot de `write()` naar uinput (G2: p95 onder 10 ms voor de software; ons doel onder 1 ms);
- Bluetooth: de jitter via de Move-stempel;
- camera: de LED-schakelproef en de kruiscorrelatie;
- beweging tot foton: een telefoon met 240 fps filmt tegelijk het geweer (tegen een aanslag gezwaaid) en de tv met een testpatroon van de kalibratieclient (een cursor). Die meting wordt alleen als totaal gerapporteerd, zonder verdeling over schakels die niet gemeten zijn (G2).

### 5.2 Uitvoerfrequentie en interpolatie

- De HID-thread schrijft op een vaste klok van 250 Hz (instelbaar van 60 tot 1000 Hz) naar `/dev/input/ravengun-gun`, plus direct bij elk nieuw rapport. Tussen rapporten wordt de oriëntatie met de laatste ω naar het uitvoermoment gezet (2.7, begrensd tot 40 ms). Dit is extrapolatie en geen nieuwe meting (sectie 11). De status toont de leeftijd van de laatste echte meting.
- Waarom: de rapporten komen onregelmatig binnen (6 en 20 ms door elkaar). Een game die op 60 Hz uitleest, zou zonder extrapolatie waarden zien die 0 tot 20 ms oud zijn, en dus haperende stappen bij een vloeiende zwaai. Met extrapolatie is elke waarde hooguit 4 ms oud en is de beweging gelijkmatig.
- Precisie: de absolute assen krijgen een ruim integerbereik (bijvoorbeeld 0..65535), zodat subpixelbeweging niet weggekwantiseerd wordt. De definitieve keuze hangt af van wat TeknoParrot leest (outputbackend, sectie 13).
- Knoppen: de trekker en de knoppen gaan direct per rapport door (flanken uit de bestaande `EdgeAccumulator`), los van de aimklok. Bij een schot wordt de aim van datzelfde uitvoermoment gebruikt. Er wordt niet gewacht op een cameraframe (sectie 11).

### 5.3 Tijdelijk optisch verlies

- 0 tot 0,5 s zonder geldige bol: de positie loopt verder met een snelheid die exponentieel uitdooft (tijdconstante 100 ms), daarna vastgehouden. De rotatie blijft volledig van de IMU. De aim is geldig, met als kwaliteit `position_held`.
- 0,5 s tot `max_position_age` (standaard 10 s, instelbaar): de aim blijft geldig onder de expliciete aanname "de speler staat stil". Kwaliteit `position_frozen`. De web-UI toont dat, en de kalibratieclient kan in de testmodus een rand of icoon tonen. Een overlay in de game bestaat niet.
- Na `max_position_age`: de aim is ongeldig. De uitvoer stopt met bijwerken (de cursor blijft staan). Er volgt geen sprong naar een hoek en geen kunstmatige off-screen-gebeurtenis (die kan in Halo een reload betekenen). De knoppen blijven werken, zolang de IMU-link vers is.
- Hervinden: zodra de tracker na bevestiging (2 frames, ongeveer 33 ms) weer een geldige bol heeft, wordt de positie in 150–250 ms kritisch gedempt naar de nieuwe meting geleid. Zo is er geen sprong als de speler intussen is bewogen, en ook geen doorschot.
- IMU-verlies: zoals nu (na 50 ms stale, na 250 ms knoppen los). De extrapolatie stopt na 40 ms.

### 5.4 Deadzone en hysterese bij een stilstaand geweer

De gemeten stilstandsjitter (p95 0,07–0,12 % met 1€) ligt ver onder de G2-grens (0,5 % van de korte zijde) en is op 1080p kleiner dan ongeveer 1 px. Een harde deadzone voelt plakkerig en geeft bij loslaten een sprong. Daarom:

- standaard: geen deadzone; alleen het 1€-filter;
- optioneel per profiel een stilstandsvergrendeling met hysterese:
  - aan als |ω| < 1,5°/s gedurende 200 ms én de cursorsnelheid laag is;
  - los als de afwijking van de vastgehouden waarde groter wordt dan 0,15 % of |ω| > 3°/s;
  - bij het loslaten gaat het 1€-filter verder vanaf de vastgehouden waarde, zodat er geen zichtbare stap is (het verschil is ≤ 0,15 %).

  Alleen voor games waar een trillend vizier zichtbaar stoort.
- Handtrilling (8–12 Hz, echt) is geen sensorruis. Die wordt niet weggefilterd, alleen door het 1€-filter bij lage snelheid gedempt.

### 5.5 Geen doorschot

- Bronnen van doorschot: predictie (2.7), een Kalman-positie met een te hoge snelheidsversterking, en een hervindblend. Elk daarvan is begrensd of kritisch gedempt.
- Het 1€-filter zelf is een eerste-orde-laagdoorlaatfilter en schiet bij een stap niet door.
- Een test met een harde stop (geweer tegen een aanslag gezwaaid) en een richtingsomkeer staat in het meetplan, met en zonder predictie.

## 6. Meetplan en acceptatiecriteria (verwijzing naar G2)

De G2-doelen zijn ontwikkeldoelen, geen bewezen eigenschappen. Elke meting vermeldt de methode, de speelpositie, de tv-grootte en de modus (magnetometer aan of uit). Niets wordt bijgesteld om groen te krijgen.

### 6.1 Richtfout op onafhankelijke doelen

- Methode A (instrumenteel): de Sharp Shooter met laser op een statief of zandzak. Elk validatiedoel uit 4.4 wordt met de laservlek geraakt. Op 3 posities, telkens 8 doelen.
- Methode B (gebruiker): dezelfde doelen, uit de hand en zonder zichtbare cursor. Daar zit de menselijke richtfout in, en het wordt apart gerapporteerd.
- Criterium (methode A): mediaan ≤ 1 % en p95 ≤ 2 % van de schermbreedte op de kalibratieposities. Op een nieuwe positie binnen het gevalideerde speelgebied (±0,5 m zijwaarts, 2–3 m diep): mediaan ≤ 1,5 %. Dat speelgebied wordt in het rapport vastgelegd. Daarbuiten wordt niets geclaimd.
- Extra tests (sectie 10): links/rechts stappen, dichterbij en verder, kantelen ±30° (verschuiving ≤ 0,5 %) en draaien zonder verplaatsing van de bol.

### 6.2 Drift over 5 minuten

- Methode: het geweer ligt stil op een statief en richt op het middendoel (de laser bewijst dat het niet bewogen is). Na het opwarmen van de bias wordt 5 minuten opgenomen. Drift is de maximale afwijking van het trefpunt ten opzichte van t = 0, na een glijdend gemiddelde van 1 s, zodat alleen drift en geen jitter overblijft. Daarna dezelfde meting terwijl het geweer met de hand heen en weer wordt bewogen en steeds terugkeert naar de laservlek op het doel (dynamische drift).
- Criterium: ≤ 1 % schermbreedte in 5 minuten met een actieve magnetometer. De IMU-only-waarde wordt ook gerapporteerd (verwacht: ruim boven de grens). Dat is informatie, geen acceptatie.
- Daarnaast een sessie van 15 minuten (G2), met de geschatte yaw-σ en de toestanden van de magnetometer (verstoord, hersteld) in de log.

### 6.3 Stilstandsjitter

- Methode: statief, 60 s, met de huidige definitie (absolute afwijking van een gecentreerd voortschrijdend gemiddelde van 1 s, p95). Nu in schermeenheden van de geometrische modus.
- Criterium: p95 ≤ 0,5 % van de korte viewportzijde (G2). Intern doel ≤ 0,1 %. Gemeten met en zonder 1€, en met en zonder predictie.

### 6.4 Latentie per schakel

- Software van `t_rx` tot uinput-`write`: p95 < 10 ms (G2). Intern doel p95 < 1 ms.
- Ontvangstspreiding van Bluetooth (Move-stempel), de positieleeftijd bij uitvoer (camera) en de uitvoerinterval-verdeling (250 Hz): gemeten en gerapporteerd.
- Beweging tot foton (telefoon met 240 fps, spelmodus van de tv aan): gemeten en gerapporteerd, zonder vooraf vastgelegde norm. Wel een streefwaarde ≤ 60 ms in Gaming Mode. Ook gerapporteerd met de predictie aan.

### 6.5 Hervinden na occlusie

- Methode: de bol 0,2 s, 1 s en 5 s afdekken met de hand, met en zonder verplaatsing van de speler.
- Criteria: een geldige optische meting binnen 3 frames (50 ms) na vrijkomen (p95). Een hervindblend die binnen 250 ms klaar is. Zonder verplaatsing een cursorstap van ≤ 0,5 %. Geen cursor die naar een hoek springt. Geen vastzittende knop.

### 6.6 Doorschot

- Methode: een zwaai tegen een harde aanslag en een snelle omkeer, bij h = 0, 10 en 20 ms.
- Criterium: een doorschot voorbij de eindstand van ≤ 0,5 % schermbreedte met de standaardinstellingen, en 0 met de predictie uit (binnen de jitter).

## 7. Implementatieplan in fasen

Elke fase is klein, krijgt eerst deterministische tests (synthetische sporen) en daarna een hardwaremeting. Een fase is pas af als die meting gerapporteerd is.

Fase 0: opnemen en afspelen

- Bouw: een opnameformaat met alleen getallen (JSONL of binair). Per rapport: `t_rx`, beide subsamples, `mag_raw`, temperatuur, knoppen en `hid_seq`. Per frame: de optische meting met `t_frame` en straal. Verder de LED-gebeurtenissen. Een afspeeltool voert de opnamen deterministisch aan de `Pipeline` (die is al zonder hardware te testen).
- Hergebruik: `RawReport` (uitbreiden met temperatuur en stempel), `Pipeline` en `OpticalSample`.
- Hardware: opnamen van 5 minuten stil, 5 minuten richten met de hand, en een wiebelopname met de camera.

Fase 1: tijdbasis van de Move

- Bouw: de stempel en de temperatuur beschikbaar maken (een PSMoveAPI-patch op de gepinde revisie, of eigen parsing; keuze plus licentie vastleggen). Klokkoppeling, subsample-afstand, de bias per subsample, en het hidraw-fd in `poll()`.
- Hardware: de eenheid en de wrap van de stempel, de relatie tussen overgeslagen volgnummers en de stempels, de spreiding van de sampletijd vóór en na, en de temperatuurdrift van de bias (koud starten, 20 minuten).

Fase 2: cameramodel en 3D-positie

- Bouw: intrinsics (rolmaat plus loodlijn), een vervormingscorrectie per meting (alleen zwaartepunt en straal, geen beeld-remap), het positiefilter, de kruiscorrelatie voor `δ_cam` en de LED-schakelproef. Eventueel een subpixel-randfit en een snelheidsbewust langgerektheidscriterium.
- Hergebruik: `BallTracker`, `FrameTiming` en `LatestQueue`.
- Hardware: de straal en de detectie op 2, 2,5 en 3 m (640x480 en 320x240), de dieptefout ten opzichte van de rolmaat, de detectie tijdens snelle zwaaien (`not_round`-tellingen), `δ_cam` met spreiding, en tv-licht en daglicht.

Fase 3: magnetometer (beslispunt)

- Bouw: de ellipsoïdefit met kwaliteitsmaten, het veldcheck-protocol en de verstoringsdetectoren (offline op opnamen).
- Hardware:
  - de ruis per sample en de veldsterkte in counts;
  - de invloed van led aan/uit en de kleur, de rumble, pomp en trekker;
  - muziek en de tv aan/uit;
  - de veldkaart over het speelgebied;
  - de magnetometer-yaw tegen de kalibratie-yaw in de werkkegel.
- Beslissing: is de herhaalbaarheid in de kegel ≤ 0,3° (na lokale correctie), dan is de magnetometer de yaw-referentie. Anders geldt gyro-only met uitlijnen, plus een escalatie-advies.

Fase 4: oriëntatieschatter

- Bouw: `EskfOrientation` naast `OrientationFilter` (Mahony), met een schakelaar, de NIS-poorten, de snelheidsbegrensde yaw-correctie en de zero-rate-pseudometing.
- Hergebruik: `GyroBiasEstimator` (beginwaarde en stilstandsdetectie) en `math.hpp`.
- Tests: synthetische sporen met bekende bias, magnetometerverstoringen en versnellingen.
- Hardware: afspelen van de opnamen uit fase 0 en 3, en dan een vergelijking Mahony tegen ESKF op drift (5 minuten), jitter en verstoringsgedrag. De ESKF wordt alleen standaard bij aangetoonde winst.

Fase 5: geometrie en schermkalibratie

- Bouw: een `GeometricAim`-module (straal, snijpunt, ongeldigheden), de fit (LM met Huber en priors), de validatie, de opslag per laag en de IPC-commando's voor de kalibratie. De fullscreen-kalibratieclient en de web-UI-stappen worden parallel door het web-UI-spoor gebouwd, tegen dezelfde commando's.
- Hergebruik: `AimMapper` blijft als `orientation`-modus en als terugval. Het inputmodel gaat naar schema 3: `position.kind` "camera_3d", de bron en σ van de yaw, de aimkwaliteit en de kalibratieversie.
- Tests: synthetische scènes met een bekende waarheid (fit herstelt de parameters, ongeldige geometrie geeft de juiste redenen).
- Hardware: de laservalidatie (6.1), leave-one-position-out en de kanteltest.

Fase 6: uitvoer en gevoel

- Bouw: de uitvoerklok van 250 Hz in de HID-lus, de begrensde predictie met remdetectie, het 1€-filter verplaatst naar de schermcoördinaat, de optionele stilstandsvergrendeling, en de uinput-backend (`/dev/input/ravengun-gun`).
- Hergebruik: `OneEuro` en `EdgeAccumulator`.
- Hardware: de latentieketen (5.1), doorschot (6.6), jitter (6.3) en een gevoelstest door de gebruiker in de standalone richttest.

Fase 7: robuustheid en de G2-sessie

- Bouw: het gedrag bij occlusie en hervinden, de degradatiestatus in de web-UI, de driftbewaking (3.3) en de hercontrole.
- Hardware: de occlusietests (6.5), een diagnostische sessie van 15 minuten met alle G2-getallen, en herstart van de daemon met een kalibratie die beschikbaar blijft.

Wat vervangen wordt en wat blijft: niets wordt weggegooid. De Mahony-schatter, de oriëntatiemodus en de huidige filterplek blijven als terugval en als vergelijkingsbasis. Alleen de standaard verschuift, per fase en na een meting.

## 8. Risico's en open vragen

ZCM1:

- De kwaliteit van de magnetometer (resolutie, ruis en eigen veld van led en rumble) is onbekend. Dit is het grootste risico voor de G2-drifteis.
- De gyroschaal komt alleen uit de 80-rpm-ijking, zonder correctie van de asuitlijning. Een schaalfout van 1 % geeft tijdens een zwaai van 20° een fout van 0,2° die pas door de referentie wordt rechtgezet. Dat wordt in fase 4 gemeten (een draai van precies 360° op een draaiplateau, en de magnetometer of de kalibratiedoelen als vergelijking).
- De bias verschilt per subsample (gemeten) en hangt van de temperatuur af (niet gemeten).
- De betekenis van het overslaande volgnummer en de eenheid van de interne tijdstempel zijn onbevestigd.
- De laadtoestand en de batterijspanning kunnen de sensoren beïnvloeden. Niet gemeten.

Camera-afstand en optiek:

- De speelafstand is niet gemeten. Op 3 m is de bol bij 640x480 ongeveer 4 px in straal: werkbaar, maar met een merkbare dieptefout (2.4).
- Bewegingsonscherpte bij exposure 50 (de omrekening naar ms is onbekend) kan een snel bewegende bol als `not_round` laten afwijzen. Ook verschuift de rolling shutter het zwaartepunt.
- Een gestoten of verschoven camera maakt de schermlaag ongeldig. De hercontrole moet dat herkennen.
- De lensstand (blue of red dot) moet vastliggen.

Verlichting:

- Daglicht, lampen en tv-licht op de speler veranderen de achtergrond. De exposure is alleen in een donkere kamer gekozen.
- Glanzende oppervlakken kunnen een reflectie van de bol geven (`multiple_candidates` bij het opnieuw vinden).
- Een helder spelbeeld kan de kamer cyaan kleuren.

Sharp Shooter-geometrie:

- De richtlijn die de speler waarneemt (zonder fysiek vizier) verschilt per persoon. De gefitte `d_B` legt die van de kalibrerende speler vast.
- Metalen delen en het pompmechaniek kunnen de magnetometer per stand beïnvloeden.
- De loop kan de bol vanuit sommige hoeken gedeeltelijk afdekken, waardoor het zwaartepunt verschuift. Dat is te testen bij kanteling.
- Of de extra knoppen van de Sharp Shooter via de EXT-poort van de Move binnenkomen en via PSMoveAPI leesbaar zijn, is niet onderzocht en valt buiten dit ontwerp.

Uitvoerketen:

- Hoe vaak TeknoParrot/BudgieLoader de absolute assen uitleest en of er ergens nog een smoothing of acceleratie bovenop komt (dubbel filteren), is onbekend.
- De invoerlatentie van de tv zonder spelmodus kan elke softwarewinst tenietdoen.

Fundamenteel:

- Met één bol is de yaw optisch niet waarneembaar. Zonder een bruikbare magnetometer blijft de drift alleen begrensd door expliciete uitlijning door de gebruiker.
- Escalatiepad (vereist andere hardware en toestemming van de gebruiker):
  1. een tweede optisch kenmerk op de Sharp Shooter (bijvoorbeeld een tweede lichtbron op bekende afstand langs de loop). Twee punten maken yaw en pitch optisch waarneembaar;
  2. een GUN4IR-achtige IR-camera in de Sharp Shooter met IR-leds bij de tv. Dat geeft een absolute, driftvrije schermpositie, maar is dan geen Move-tracking meer.
- Er wordt geen driftvrije 6DoF-tracking geclaimd.

## Bronnen

- PSMoveAPI, commit 256b629 (lokaal in `~/.cache/ravengun/src/psmoveapi`): `src/psmove.c` (magnetometer en rapportindeling), `src/psmove_calibration.c` (schaling), `src/psmove_orientation.cpp` (ComplementaryMARG en Madgwick), `src/utils/magnetometer_calibration.c`, `src/tracker/psmove_fusion.cpp` en `src/tracker/psmove_tracker.cpp`.
- PSMoveService (gearchiveerd): https://github.com/psmoveservice/PSMoveService. Bestanden: `src/psmoveservice/Filter/OrientationFilter.cpp`, `KalmanOrientationFilter.cpp`, `PositionFilter.cpp`, `src/psmoveservice/PSMoveController/PSMoveController.{h,cpp}` en `src/psmoveconfigtool/AppStage_MagnetometerCalibration.cpp`.
- G. Casiez, N. Roussel, D. Vogel, "1€ Filter: A Simple Speed-based Low-pass Filter for Noisy Input in Interactive Systems", CHI 2012. https://gery.casiez.net/1euro/
- S. Madgwick, "An efficient orientation filter for inertial and inertial/magnetic sensor arrays", rapport 2010. https://www.samba.org/tridge/UAV/madgwick_internal_report.pdf
- R. Mahony, T. Hamel, J.-M. Pflimlin, "Nonlinear Complementary Filters on the Special Orthogonal Group", IEEE TAC 53(5), 2008.
- F. L. Markley, "Attitude Error Representations for Kalman Filtering", JGCD 26(2), 2003.
- J. Solà, "Quaternion kinematics for the error-state Kalman filter", 2017. https://arxiv.org/abs/1711.02508
- S. LaValle, A. Yershova, M. Katsev, M. Antonov, "Head tracking for the Oculus Rift", ICRA 2014, en de Oculus-blog over magnetometers: https://developers.meta.com/horizon/blog/magnetometer/
- P. Furgale, J. Rehder, R. Siegwart, "Unified temporal and spatial calibration for multi-sensor systems", IROS 2013.
- F. Devernay, O. Faugeras, "Straight lines have to be straight", Machine Vision and Applications 13, 2001.
- WiiBrew, Wiimote (IR-camera): https://wiibrew.org/wiki/Wiimote
- GUN4IR en Sinden (werking): https://retrohandhelds.gg/a-brief-look-at-the-gun4ir-light-gun/ en https://dev.to/musique243/light-gun-tech-old-new-kj3
