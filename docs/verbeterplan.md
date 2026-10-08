# Verbeterplan na de eerste Halo-sessie met de Move (2026-10-07)

## Uitgangspunt

- Halo met de Move in de Sharp Shooter werkt: richten, schieten, herladen, granaat, munt, Start en TEST via de daemon. De gebruiker vindt het richten snel en goed.
- Probleem 1: het vizier driftt snel tijdens het spelen; de speler moet steeds met PS centreren.
- Probleem 2: onbekend of de pomp van de Sharp Shooter een eigen signaal geeft.

## Wat de data nu al zegt

- De magnetometer-correctie (mahony_mag) stond in de praktijk uit: 99,8 % van de rapporten werd als "verstoord" afgewezen. De drift is dus pure gyrodrift.
- Oorzaak van die afwijzing: de referentie uit de (marginale) kalibratie klopt niet met het gemeten veld. Gemeten op de tafel: veldsterkte 1,074 keer de referentie (grens 5 %), dip 64,7 graden tegen referentie 60,2 (grens 2 graden). De echte inclinatie in Nederland is ongeveer 67 graden.
- In stilstand is de gyrodrift klein (0,15 graden per minuut). De drift ontstaat dus tijdens bewegen. Kandidaten: verloren BT-rapporten tijdens snelle bewegingen (ongeveer 13 % verlies), schaalfout van de gyro, biasverandering door opwarming.
- Er bestaat nog geen opname van echt spelen; de oefenopname van 600 s was vrijwel stil (max 7 graden per seconde).

## Stappen

1. Meten tijdens spelen (gebruiker, 10 minuten). De daemon neemt op terwijl de gebruiker Halo speelt. Elke PS-druk is een gratis meetpunt: de correctie die de recenter toepast is precies de opgelopen drift sinds de vorige PS-druk. In dezelfde sessie: tien keer pompen en de fire-mode-schakelaar omzetten, om te zien of die signalen geven.
2. Oorzaak van de drift (subagent, offline op de opname). Drift per PS-interval uitzetten tegen gedraaide hoek, maximale draaisnelheid, verloren rapporten, temperatuur en tijd sinds aanzetten. Per hypothese een replay met de verbetering: betere gat-interpolatie, gyroschaal per as, bias bijschatten in elke stille halve seconde.
3. Magnetometer weer laten meetellen. Referentie (veldsterkte en dip) automatisch leren op de speelplek in plaats van uit de kalibratie; zachte weging in plaats van hard afwijzen; nieuwe kalibratie in de Sharp Shooter op de speelplek. Doel: de yaw blijft vastgepind en PS is alleen nog nodig bij het begin.
4. Live A/B met de gebruiker: tien minuten Halo zonder PS te drukken, drift per minuut meten (opname) en gevoel navragen. Alleen houden wat aantoonbaar beter is.
5. Sharp Shooter-knoppen. Als de pomp en schakelaar via de uitbreidingsconnector van de Move binnenkomen: die bytes uitlezen in de daemon en als Move-knoppen in de indeling aanbieden (bijvoorbeeld pomp = herladen).
6. Camera als tweede anker (fase 5): schermkalibratie (vier hoeken aanwijzen) en een geometrische aim-ray vanuit de bolpositie, zodat afstand en zijwaartse verplaatsing kloppen.
7. Gemak: één commando of Steam-snelkoppeling dat de daemon wapent en Halo start; Gaming Mode; de kalibratie-app noemt PS als centreerknop.
8. Later: Navigation Controller, profielen voor MAME, House of the Dead Remake en Dolphin, 250 Hz-uitvoer.

## Stand na de meetsessie van 2026-10-08

- Gemeten in Halo (270 s, 8 PS-intervallen): yaw-drift altijd dezelfde kant op, mediaan 25,8 graden/min, cumulatief -62,6 graden in 164 s.
- Oorzaak: de gyro-bias om de verticale as loopt mee met de opwarming van de Move (ongeveer 0,05 graden/s koud tot 0,85 warm); de oude schatter vond tijdens het spelen bijna geen echte rust en bleef op de koude waarde staan.
- Gebouwd: bias-modus `new` (temperatuurcompensatie, quasi-stille vensters, bewaarde bias per controller). Replay op dezelfde opname: cumulatief -1,0 graad in 164 s (-0,36 graden/min); mediaan per interval 8,0 graden/min, waarvan een groot deel de richtfout van de speler per PS-druk is. Dit is op de opname waarop het is afgesteld; de live test moet het bevestigen. Op de opname stil5min is `new` tijdens beweging slechter dan `legacy` (+13,6 tegen +6,6 graden); nog uitzoeken.
- Sharp Shooter: trekker = fire-bit, pomp = T zonder fire-bit (herladen), knop onder de trekker = Move-knop (centreren).
- Besluit van de gebruiker: voorlopig alleen softwareoplossingen; het tweede lichtpunt op de kolf is geparkeerd.

Vervolg, in deze volgorde:

1. Live test van de bias-modus `new` in Halo, met opname.
2. Camera als bewaker van de gyro: staat de bol stil in beeld, dan is de gemeten draaisnelheid bias. Eerst narekenen op de opname (was de bol in beeld?), daarna inbouwen. Plus een bewaarde bias-tegen-temperatuurcurve per controller, zodat een koude start zonder stilleggen ook klopt.
3. VQF naast Mahony als A/B (leert de magnetometerreferentie zelf), gemeten met `analyze drift`.
4. Voorspelling tegen de Bluetooth-latentie (instelbaar, 20 tot 40 ms).
5. Daarna de eerdere punten: schermkalibratie met de camera, gemak, overige games.
