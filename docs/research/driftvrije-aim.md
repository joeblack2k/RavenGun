# Onderzoek: driftvrije aim voor RavenGun (Move ZCM1 + PS3 Eye)

Datum: 2026-10-08. Methode: online bronnen gelezen via WebSearch/WebFetch. Elke bewering is gemarkeerd: [BRON] = uit bron gelezen (URL erbij), [AFGELEID] = eigen redenering of onzeker.

## (a) Bevindingen per punt

### 1. Gyro-bias tijdens gebruik; VQF

- [BRON] VQF (Laidig & Seel, Information Fusion 2023) haalt gemiddeld 2,9 graden RMSE over een grote set publieke datasets, tegen 5,3 tot 16,7 graden voor acht literatuurmethoden, met één vaste parameterset. https://arxiv.org/abs/2203.17024 (volledige tekst gelezen via https://arxiv.org/pdf/2203.17024)
- [BRON] Biasschatting is één Kalman-filter met twee soorten updates: in rust (gelaagde rustdetectie, laagdoorlaatgefilterde gyro als meting, klein meetvariantie) en in beweging (alleen uit het verschil tussen gyro-integratie en accelerometer, dus zonder magnetometer). Rust-updates krijgen veel meer vertrouwen; een rustmeting overschrijft een bewegingsschatting binnen enkele seconden.
- [BRON] De bias langs de huidige verticaal is met de accelerometer niet waarneembaar. VQF zet daarvoor een kunstmatige nulmeting met grote variantie (biasVerticalForgettingFactor) zodat die component langzaam vergeet. Zelfde paper, bijlage C.
- [AFGELEID] Gevolg voor ons: een geweer dat vrij vlak gehouden wordt, heeft juist de yaw-bias als "verticale" component. Die schat VQF in beweging dus niet; alleen rustdetectie (rust: 1,5 s, gyro-afwijking onder 2 graden/s, accel onder 0,5 m/s2) of wisselende kanteling (rollen van het geweer) maakt hem waarneembaar. Omdat onze stilstandsdrift maar 0,15 graad/min is, is bias waarschijnlijk niet de hoofdoorzaak; schaalfout, verloren rapporten en tijdstempelfouten zijn dan verdachter. Dit is niet gemeten.
- [BRON] Magnetische verstoring: referentie voor veldsterkte en diphoek (die zichzelf langzaam bijstelt, magRefTau 20 s); veld telt als ongestoord na 0,5 s binnen de drempels (norm 10 %, dip 10 graden); bij verstoring staat de magnetometer-update tot 60 s helemaal uit, daarna loopt hij langzamer; een nieuw homogeen veld wordt als nieuwe referentie geaccepteerd na 20 s bij voldoende beweging (5 s bij de start). Tijdconstanten: tauAcc 3 s, tauMag 9 s. https://vqf.readthedocs.io/en/latest/ref_cpp_params.html
- [BRON] De magnetometer beïnvloedt in VQF nooit de inclinatie of de biasschatting (strikt gescheiden stappen). Paper, sectie 2.
- [BRON] Licentie MIT; C++ is de hoofdimplementatie, ook Python/Cython, Matlab en een onofficiële C-port; 6D (zonder mag) en 9D; verschillende samplefrequenties per sensor. https://github.com/dlaidig/vqf . Volledige variant heeft schakelaars motionBiasEstEnabled, restBiasEstEnabled, magDistRejectionEnabled. https://vqf.readthedocs.io/en/latest/
- [AFGELEID] Geschikt als vervanger van Mahony: ja als referentie en snelle winst (MIT, klein, parameters passen bij ons gedrag: automatische referentie leren lost ons "99,8 % verstoord"-probleem op). Aandachtspunten die ik niet kon verifiëren: omgaan met onregelmatige Ts/verloren rapporten (zelf per subsample met echte dt voeden of gat opvullen), en dat VQF geen positie-input kent (camera-fusie moet er zelf naast).
- [BRON] Over Joy-Con-achtige apps: JoyShockMapper heeft AUTO_CALIBRATE_GYRO (detecteert stilstand/neergelegd en kalibreert). https://github.com/Electronicks/JoyShockMapper/releases/tag/v3.2.0 . GyroWiki waarschuwt dat auto-recalibratie langzame bewuste bewegingen voor stilstand kan aanzien en optioneel moet zijn. https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-1:the-gyro-is-a-mouse
- Niet gevonden: temperatuurmodel van gyrobias voor de ZCM1 (zie c).

### 2. Yaw zonder magnetometer via optische positie + accelerometer

- [BRON] Absolute heading is waarneembaar als een absolute positie/snelheidsmeting samengaat met versnelling van de INS ten opzichte van de aarde; bij constante snelheid of stilstand niet (octrooi-samenvatting, via zoekresultaat). Bron: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10502573 (alleen samenvatting gezien).
- [BRON] Snelheid-gesteunde attitudeschatting zonder magnetometer geeft alleen inclinatie, de yaw blijft vrij (abstract; de zin over yaw zelf staat er niet expliciet). https://arxiv.org/abs/2002.10205
- [BRON] Visueel-inertiële EKF's hebben globale yaw als niet-waarneembare richting; naïeve implementaties winnen valse yaw-informatie (FEJ-EKF / observability-constrained EKF zijn de gangbare remedie). Zoekresultaat naar https://arxiv.org/pdf/1712.00036 en https://robotics.usc.edu/publications/611 ; niet zelf diep gelezen.
- [AFGELEID] Bij ons ligt de camera vast in een bekend wereldframe, dus de bolpositie is absoluut en de yaw wordt waarneembaar uit de richting van de horizontale versnelling: yaw-fout psi geeft een richtingsfout psi in de versnelling die aan de optische tweede afgeleide moet voldoen. Schatting: bij 2 m/s2 horizontale versnelling en accelerometerbias 0,1 m/s2 is de resolutie per gebeurtenis enkele graden; middelen over veel bewegingen geeft misschien rond 1 graad. Dat is te grof voor "bijna geen drift" maar goed genoeg als langzame bewaker die de grote yaw-fout begrenst. Vereist: hefboomarm tussen bol en IMU, kloksynchronisatie camera/BT (latentie 30 tot 50 ms is hier cruciaal), kleine kans op valse yaw bij stilstand (updates alleen bij voldoende versnelling).
- [BRON] Praktijk Oculus: yaw heeft een externe referentie nodig (DK1 magnetometer, DK2 camera); zonder camera 1 tot 4 graden/min yaw-drift bij stilstaand hoofd (forumgebruiker, niet gezaghebbend). https://forum.worldviz.com/showthread.php?p=18173 en https://communityforums.atmeta.com/discussions/dev-pcvr/drift-correction-techniques/308493 . Daar draagt het hoofdtelefoon meerdere gepulseerde leds, dus de camera ziet oriëntatie direct; een enkele bol kan dat niet.
- Niet gevonden: hoe PSMoveService/Sony Move.Me of PSVR yaw precies fuseren (geen primaire bron bereikbaar).

### 3. Lightgun-oplossingen en driftcorrectie

- [BRON] Sinden: camera in de loop kijkt naar een witte rand op het scherm; gedraagt zich als muis; manufacturer-cijfers 60 fps (16 ms frame), verwerking 1 tot 5 ms; geen externe sensoren. https://en.wikipedia.org/wiki/Sinden_Light_Gun , https://www.timeextension.com/reviews/sinden-light-gun-old-school-arcade-blasting-on-your-modern-day-tv (leveranciersclaims).
- [BRON] OpenFIRE/Samco: vier IR-punten rond het scherm (lightbars in vierkant of "diamant"), IR-camera in het pistool, pointer-update tot 209 Hz. De README noemt geen IMU. https://raw.githubusercontent.com/TeamOpenFIRE/OpenFIRE-Firmware/OpenFIRE-dev/OpenFIREmain/README.md
- [BRON] Gun4IR: vier leds, "realtime auto-calibration", claim 4 ms latentie; geen IMU genoemd. https://www.gun4ir.com/pages/about-the-gun4ir%C2%AE (leveranciersclaims)
- [AFGELEID] Gemeenschappelijk principe: die systemen hebben geen drift omdat elk frame een absolute meting is (scherm in beeld). Er is niets te "corrigeren". Wij zijn het omgekeerde geval (vaste camera, bewegende bol), dus wij missen de absolute richting van de loop. Dat verschil is de kern van ons probleem.
- [BRON] Wiimote: IR-camera + sensorbalk geeft x, y, z en roll absoluut; MotionPlus voegt gyro toe voor rotatie als de balk niet in beeld is. https://www.ifixit.com/Device/Wii_Motion_Plus en zoekresultaten. Hoe Nintendo de gyro aan de balk her-anchort: niet gevonden. Een Dolphin-voorstel om IMU-aim met de balk te synchroniseren bestaat (https://forums.dolphin-emu.org/Thread-feature-idea-augmented-pointing-with-wii-remote-plus), geen beschrijving van een werkende implementatie.
- [BRON] Dolphin met echte MotionPlus: pointer-drift opgelost door rust-kalibratie en gyro-deadzone; een te lage deadzone (0 graden) verhindert herkalibratie. https://forums.dolphin-emu.org/Thread-real-wiimote-as-emulated-for-wii-motion-plus-pointer-emulation-drifts
- [BRON] GyroWiki: harde minimumsnelheid-cutoffs zijn "het slechtst"; gebruik een zachte "tightening" onder een drempel; gelaagde smoothing tussen twee drempels. https://gyrowiki.jibbsmart.com/blog:good-gyro-controls-part-1:the-gyro-is-a-mouse
- Steam Input/Deck: alleen handmatige gyrokalibratie gevonden, geen in-spel auto-kalibratie bevestigd.
- [AFGELEID] Zachte hercentrering: wie "uit-het-scherm" of loslaten als moment gebruikt (tijdens trekkerloze rust) kan in de aim langzaam naar de schermmidden duwen; dat is de gangbare truc maar is hier niet per bron bevestigd, en je ziet het als "kleven".

### 4. Goedkope toevoeging: tweede punt voor yaw

- [AFGELEID] Geometrie: twee punten op een basislijn L langs de loop (bol voorop, led achterin de kolf, L ongeveer 25 tot 30 cm). Zij zien vanuit de camera (die achter het scherm kijkt, de loop wijst bijna naar de camera) een zijwaartse offset L·sin(yaw): ongeveer 5 mm per graad. Bij f ongeveer 550 px (PS3 Eye, 640x480, ongeveer 60 graden; kalibreren) en 2,5 m is dat ongeveer 1,1 px per graad. Met sub-pixel-centroid (0,1 tot 0,2 px) is dat orde 0,1 tot 0,3 graden, in pitch/dwarsrichting ook ongeveer hetzelfde. Dit is een rekenvoorbeeld, niet gemeten.
- [AFGELEID] Een basislijn dwars op de loop (twee leds zij aan zij) is slechter: de schijnbare lengte verandert met cos(yaw), dus bijna ongevoelig rond nul.
- [AFGELEID] Een tweede Move-bol of tweede led verschuift het probleem: de camera moet beide punten onderscheiden (kleur of knippercode; Oculus gebruikte knipperpatronen, https://roadtovr.com/reverse-engineering-oculus-rift-dk2-positional-tracking-camera-linux-sdk/). De Move-led van de ZCM1 kan kleur wisselen; een tweede kleur-bol of RGB-led op de kolf is de goedkoopste route. De bol zelf kan door de kolf of de arm van de speler worden afgeschermd (zichtbaarheid niet onderzocht).
- [BRON] ArUco/AprilTag-yaw: op kleine afstand stabiel, op afstand onstabiel; roll stabiel, yaw onbetrouwbaar; afhankelijk van kalibratie en belichting (forumdraden, https://answers.opencv.org/question/196850/aruco-tracking-in-opencv , https://forum.opencv.org/t/camera-pose-estimation-with-aruco-marker-inaccuracy/2034). Eigen schatting: 5 cm tag op 2 m is ongeveer 14 px breed, dus enkele graden fout. Voor ons dus slechter dan twee ver uit elkaar liggende punten, en de loop staat meestal met de punt naar de camera gericht (tag niet zichtbaar). Geen praktijkervaring gevonden met een tag op een lichtgun.

### 5. Latentie en gladheid

- [BRON] Meta/Oculus: voorspellen 20 tot 40 ms vooruit; constante hoekversnelling presteert het best; Savitzky-Golay om snelheid en versnelling te schatten; fout groeit minstens kwadratisch met het interval; bij 40 ms gemiddeld 0,17 graden met constante versnelling tegen 3,36 graden zonder voorspelling (op een HMD bij ongeveer 240 graden/s, één proefpersoon). https://developers.meta.com/horizon/blog/the-latent-power-of-prediction/
- [BRON] 1-euro-filter: snelheidsafhankelijke laagdoorlaat (lage cutoff bij traag, hoge bij snel). Tuning: beta 0, fcmin ongeveer 1 Hz, dan beta verhogen tegen lag en fcmin verlagen tegen jitter. https://hal.archives-ouvertes.fr/hal-00670496 , https://github.com/casiez/OneEuroFilter
- [BRON] Een oude studie (Wu & Ouhyoung 2000) vond de grootste jitter bij Kalman-voorspelling van hoofdbeweging; recente Kalman-voorspellers geven soms grote hoekfouten bij kwaternionen. Geen directe 1-euro-versus-Kalman-vergelijking voor pointers gevonden. https://arxiv.org/pdf/2007.14084
- [AFGELEID] Verloren rapporten (13 %): 88 rapporten/s met 2 subsamples; gaten opvullen met laatste gyro-snelheid (constante snelheid) en bij herstel de echte dt uit de tijdstempels (of rapportteller) gebruiken, nooit vaste dt. Dit is mijn voorstel, niet uit bron.

## (b) Gerangschikte aanbevelingen

1. Eerst diagnose uit speelopname: drift per PS-interval tegen gedraaide hoek, snelheid, verloren rapporten. Effect op drift: bepaalt alles (0,15 graad/min stilstand suggereert schaal of dt, niet bias). Moeite: laag. Risico: geen. Extra hardware: nee.
2. dt per subsample uit rapportteller/tijdstempel halen en gaten correct opvullen, plus gyroschaal per as vastleggen. Effect: mogelijk het grootste deel van de beweging-drift. Moeite: laag tot middel. Risico: laag. Hardware: nee.
3. VQF (MIT) naast Mahony zetten als A/B (6D en 9D), referentie automatisch leren (magRefTau, magNewFirstTime). Effect: zelfde of beter, magnetometer weer bruikbaar, bias in rust betrouwbaar. Moeite: middel (C++ binding, dt-afhandeling). Risico: laag; bias langs verticaal blijft in beweging onschatbaar. Hardware: nee.
4. Rustdetectie op spelmomenten (loslaten, tussen golven): bias bijschatten met zachte weging en nooit hard stoppen (GyroWiki). Effect: laag tot middel. Moeite: laag. Risico: kleven bij langzame bewuste beweging. Hardware: nee.
5. Camera als langzame yaw-bewaker via versnellingsrichting (INS-achtig, ESKF met hefboomarm, update alleen bij horizontale versnelling boven drempel). Effect: begrenst yaw-fout, orde enkele graden tot ongeveer 1 graad; geen absolute oplossing. Moeite: hoog. Risico: middel (latentie-uitlijning, valse yaw). Hardware: nee.
6. Tweede punt op de kolf (kleur-led of tweede bol, basislijn 25 tot 30 cm langs de loop): Effect: absolute yaw per frame, orde 0,1 tot 0,3 graad op 2,5 m (afgeleid), dus echte drift weg. Moeite: middel (tweede blob, identificatie). Risico: zichtbaarheid, wie het punt afschermt. Hardware: ja, een led/bol (euro's).
7. Voorspelling tot de gemeten BT-latentie (constante versnelling, maximaal 25 tot 40 ms), 1-euro op schermcoördinaat. Effect: gevoel, niet drift. Moeite: laag. Risico: overshoot bij plotse stop. Hardware: nee.
8. Zachte hercentrering naar het scherm bij langdurig geen schot (optioneel, uit te zetten). Effect: verbergt drift. Moeite: laag. Risico: kleven; niet "arcade perfection".
9. Alleen als alles faalt: IR-leds rond het scherm plus IR-camera of Sinden/OpenFIRE-hardware. Absoluut per frame, maar andere hardware en nieuw ontwerp.

## (c) Niet gevonden of niet geverifieerd

- Hoe PSMoveService, Sony Move.Me en PSVR yaw fuseren; geen primaire bron bereikbaar.
- Hoe Wii Sports Resort / Nintendo MotionPlus aan de sensorbalk her-anchort.
- Temperatuurmodel van de ZCM1-gyro, en of VQF onregelmatige dt of verloren rapporten ondersteunt (documentatie niet gelezen op dit punt).
- Primaire bronnen over Oculus Constellation-fusie; alleen forumberichten.
- Publicatie met gemeten nauwkeurigheid van twee-punts-yaw of ArUco op 640x480 bij 2 tot 3 m; mijn getallen zijn rekenvoorbeelden.
- Steam Input in-spel auto-kalibratie; alleen handmatig gevonden.
- Directe vergelijking 1-euro versus Kalman voor pointers.
- Details van het octrooi en de VIO-observability-artikelen zijn alleen via zoeksamenvattingen gezien.
