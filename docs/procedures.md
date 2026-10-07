# Procedures voor de gebruiker (metingen fase 0 en fase 3)

Dit document beschrijft wat de gebruiker doet bij de opnamen en de magnetometerkalibratie. De commando's draaien op de SteamOS-host. De daemon `ravengund` is de enige eigenaar van de Move en de camera; alle commando's hieronder praten via de socket met die daemon. Opnamen bevatten alleen getallen (HID-rapporten, de positie en straal van de bol, LED-wissels en labels), nooit camerabeelden. Ze staan in `~/.local/state/ravengun/recordings/` (map 0700, bestanden 0600). Het Bluetooth-adres staat er alleen geredigeerd in.

## Voorbereiding (elke sessie, ongeveer 1 minuut)

1. Start de daemon in een terminal: `./build/ravengund`. Draait er al een `ravengund`, dan gebruik je die.
2. Druk op de PS-knop van de Move. Binnen 2 seconden verbindt hij via Bluetooth.
3. Controleer met `./build/ravengunctl status`:
   - "Move: verbonden (bluetooth), ZCM1";
   - "hidraw-tap: open" (dan zijn de interne tijdstempel en de temperatuur beschikbaar);
   - "Tijdbasis: ... gekoppeld" (na ongeveer 5 seconden);
   - "Camera: streaming" en, als de bol in beeld is, "bol geldig".
4. Leg de Move 10 seconden stil op tafel. Dan schat de daemon de gyro-bias ("bias estimated" in de status).

## Opname "richten met de hand" (fase 0; ongeveer 10 minuten)

Doel: een echt richtspoor met beweging, stilstand en snelle stops, waarop de oriëntatieschatters later vergeleken worden (Mahony tegenover ESKF, fase 4). De opname zelf duurt 5 minuten.

1. De speler staat op de gewone speelplek, ongeveer 2,5 m van de tv, met de Move los in de hand (of in de Sharp Shooter, als dat de bedoeling van de sessie is; noem dat dan in de tag, bijvoorbeeld `richten-sharpshooter`). De bol moet zichtbaar zijn voor de camera.
2. Start de opname: `./build/ravengunctl record --tag richten-hand --duration 300`. Het commando toont de voortgang en stopt zelf na 300 seconden. Ctrl-C stopt eerder; het bestand blijft dan geldig.
3. Wat de speler doet, in deze volgorde:
   - 10 seconden stil richten op het midden van het scherm;
   - langzaam naar de vier hoeken en terug naar het midden, op elke hoek 3 seconden stilhouden;
   - een minuut vrij richten zoals in een spel, met snelle zwaaien naar links en rechts en een harde stop;
   - een paar keer snel van richting wisselen (heen en weer);
   - 10 seconden stil op het midden;
   - de rest van de tijd vrij richten, met af en toe de trekker;
   - de laatste 10 seconden de Move stil op tafel leggen (een eindreferentie zonder hand).
4. Labels: wie de opname begeleidt, kan op elk moment een label zetten met `./build/ravengunctl record --mark "midden"` (of "hoeken", "snel", "op tafel"). Labels maken de analyse eenvoudiger, maar zijn niet nodig.
5. Wiebelopname voor de tijdsuitlijning van camera en gyro (1 minuut): `./build/ravengunctl record --tag wiebel --duration 60`. De speler houdt de bol in beeld en draait de pols snel heen en weer (ongeveer 2 keer per seconde, met het draaipunt in de hand), 3 keer 10 seconden, met 5 seconden pauze ertussen.
6. Na afloop toont het commando de bestandsnaam. Controle:
   - `./build/ravengun-analyze replay <bestand>` (deterministisch afspelen; de digest is bij elke herhaling gelijk);
   - `./build/ravengun-analyze timebase <bestand>` (stempel, klokkoppeling, bias per subsample).

## Magnetometerkalibratie in de Sharp Shooter (fase 3; ongeveer 5 minuten)

Doel: de hard- en soft-iron-correctie van de magnetometer, inclusief de vaste metalen delen van de Sharp Shooter. De kalibratie geldt per controller en per houder. Ze wordt alleen opgeslagen als ze de kwaliteitseisen haalt (`~/.config/ravengun/magnetometer/<sleutel>.json`, atomisch geschreven, met versie). De magnetometer wordt nog niet in de fusie gebruikt; dat volgt pas na de beslissing in fase 3.

1. De Move zit in de Sharp Shooter. Geen USB-kabel aan de Move. De LED staat op de speelkleur (cyaan, de standaard). De speler staat op de speelplek, minstens 1 meter van metalen tafels, radiatoren, speakers en de subwoofer.
2. Neem de ruwe data tegelijk op, zodat de fit later offline herhaald kan worden: `./build/ravengunctl record --start --tag magcal-sharpshooter --duration 600`.
3. Start de kalibratie: `./build/ravengunctl calibrate mag --holder sharp_shooter`. Het commando toont "dekking %" en het aantal samples.
4. Wat de speler doet (60 tot 90 seconden): het geweer langzaam in alle richtingen draaien. Achtjes in de lucht, de kolf omhoog en omlaag, de loop naar het plafond en naar de vloer, en rollen om de loop, ook helemaal ondersteboven. Langzaam draaien, zonder te schudden of te zwaaien, want versnelling verstoort de dipcontrole. Blijf zelf op dezelfde plek staan.
5. Het commando stopt zelf zodra de dekking minstens 80 % is en er minstens 300 samples zijn (anders na 180 seconden). Daarna fit de daemon en toont hij het resultaat:
   - "GESLAAGD, opgeslagen in ...": klaar;
   - "NIET GESLAAGD": de daemon noemt concreet wat er mis is, bijvoorbeeld "te weinig dekking" (meer richtingen, ook ondersteboven) of "veld niet homogeen" (verder van metaal en speakers draaien). Herhaal dan stap 3 en 4.

   Ctrl-C breekt af zonder iets op te slaan. Met `--start`, `--status`, `--stop` en `--abort` is de kalibratie ook in losse stappen te bedienen (bijvoorbeeld vanuit de web-UI; de voortgang staat in `GET /api/magnetometer` en in het veld `progress` van de SSE-stroom `/api/stream`).
6. Veldcheck (ongeveer 1 minuut). De speler richt op het midden van het scherm en houdt het geweer 3 seconden stil, terwijl de begeleider `./build/ravengunctl calibrate mag --check --duration 3` uitvoert. Dat gebeurt op drie plekken:
   - op de speelplek;
   - 0,5 meter naar links;
   - 0,5 meter naar rechts.

   Daarna op de speelplek nog twee keer: met de pomp naar achteren getrokken, en met de trekker ingedrukt. Elke check moet "IN ORDE" melden (de norm binnen 5 % en de dip binnen 2 graden van de kalibratie). "NIET IN ORDE" betekent dat de magnetometer op die plek of in die stand onbetrouwbaar is. Noteer welke.
7. Stop de opname: `./build/ravengunctl record --stop`.
8. Offline controle: `./build/ravengun-analyze magfit <bestand>` (dezelfde fit op de opname) en `./build/ravengun-analyze mag <bestand> --cal ~/.config/ravengun/magnetometer/<sleutel>.json` (de verstoringsdetector over de hele opname).

## Meetsessie magnetometerverstoringen (fase 3; ongeveer 10 minuten, optioneel)

Doel: meten wat de omgeving en de Sharp Shooter met de magnetometer doen. Dit vraagt een geslaagde kalibratie (hierboven). Leg het geweer steeds stil neer, of laat het stil vasthouden, met de loop naar het scherm.

1. `./build/ravengunctl record --start --tag magstoring --duration 900`.
2. Per stap 30 seconden stil, met eerst een label (`./build/ravengunctl record --mark "<label>"`):
   - "referentie": tv aan, geen muziek;
   - "pomp": pomp naar achteren en vasthouden;
   - "trekker": trekker ingedrukt houden;
   - "muziek": muziek of een game met veel bas op normaal volume;
   - "tv uit": tv uit;
   - "dicht bij speaker": het geweer op 50 cm van de speaker of soundbar.
3. `./build/ravengunctl record --stop`, daarna `./build/ravengun-analyze mag <bestand> --cal <kalibratie>`. Het verschil per segment staat in counts en in % van de veldsterkte.

## Door de agent zelf uitgevoerd (ter referentie)

De opnamen "5 minuten stil" (tag `stil5min`) en "LED-reeks bij stilstand" (tag `ledmag`: steeds 20 seconden cyaan, uit, rood, uit, groen, uit, blauw, uit, cyaan, wit, uit en cyaan) vragen geen handeling van de gebruiker. De Move moet alleen verbonden zijn en stil liggen. `ravengunctl led <kleur>` wisselt de LED-kleur. Let op: de tracker blijft de tint van de opstartkleur zoeken, dus de bol is bij een andere kleur niet in beeld.
