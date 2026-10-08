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
4. Leg de Move 10 seconden stil op tafel. Dan schat de daemon de gyro-bias ("Gyro-bias: estimated (bron still)" in de status). Is er een bewaarde bias van een eerdere sessie (`~/.local/state/ravengun/gyro-bias/<sleutel>.json`), dan staat de status meteen op "restored" en meldt het opstartlog "gyro-bias hersteld"; de 10 seconden rust vervangen hem daarna door een verse meting. Een koude Move (net aangezet, ruwe temperatuur `tc` rond 1850) heeft een andere z-bias dan een warme (rond 2050): leg hem daarom ook bij een nieuwe sessie even op tafel, en laat hem liefst de eerste minuut stil liggen terwijl hij opwarmt, zodat de daemon de temperatuurhelling kan leren. Tijdens het spelen volgt de daemon de bias verder met die helling en met rustige momenten (quasi-stil); de status toont de bron ("still", "quasi-still" of "restored"), de helling en hoeveel bewijs er is.

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

## Live proef fase 4: magnetometer-yaw en de Move als lightgun (ongeveer 15 minuten)

Voorwaarden: de daemon is na de merge opnieuw gebouwd en gestart (`ravengund`), de Move zit in de Sharp Shooter, de magnetometerkalibratie staat in `~/.config/ravengun/magnetometer/` (oude bestanden worden automatisch omgezet: de dip wordt dan positief, ongeveer +60°). De orchestrator bedient de commando's; de gebruiker richt.

1. Controle (`./build/ravengunctl status`): "Magnetometer: gekalibreerd ..., dip +60 graden (ref +60)" op de speelplek, toestand ok; "Yaw-correctie: mahony, schaduw". Op tafel mag "disturbed" staan (gemeten: het veld bij de tafel is anders).
2. Nulmeting IMU-only (3 minuten): `./build/ravengunctl record --start --tag fase4-mahony --duration 400`. De gebruiker richt 10 s stil op het midden van de tv (label `./build/ravengunctl record --mark stil_midden_1`, na 5 s `--mark stil_midden_1_einde`), richt 30 s vrij, weer 10 s midden (`stil_midden_2`), enzovoort tot 5 keer, en de laatste minuut vrij. Noteer per keer hoe ver de cursor van het midden staat (gevoel/schatting). Daarna `--stop`.
3. Omschakelen: `./build/ravengunctl fusion mahony_mag`. Status: "Yaw-correctie: mahony_mag, wacht op vergrendeling" en na 2 tot 10 s "actief"; `yaw_source` in de SSE/watch wordt `magnetometer`.
4. Zelfde opname als stap 2 met tag `fase4-mahony-mag`. Let op: geen zichtbare sprongen of een vizier dat in stilstand zelf wegglijdt. `correction_rate_deg_s` in stilstand hooguit 0,02 (0,08 vlak boven de bewegingsgrens), `vertical_bias_deg_s` binnen ±0,3.
5. Offline: `./build/ravengun-analyze fusion <opname> --cal ~/.config/ravengun/magnetometer/<sleutel>.json` voor beide opnamen. Winst = kleinere "afwijking van de start" en kleinere helling bij mahony_mag. Pas bij aantoonbare winst `fusion.mode` zo laten (het commando bewaart het al); anders `ravengunctl fusion mahony`.
6. Lightgun (alleen met de gun-binding in TeknoParrot; eerst de daemon, dan de game): `./build/ravengunctl arm`. Dit maakt ook het virtuele toetsenbord `RavenGun Virtual Keyboard` aan; een los draaiend `vinput kbd` moet dan uit. Controle zonder game: `./build/ravengunctl output --json` toont `live.positions` oplopend, `buttons_down` bij het overhalen, `keys_down` bij een toetsknop, en `map` (de actieve indeling). De standaardindeling (Halo):
   - T = trekker (analoog), Move-knop = grenade, Kruis = reload.
   - Driehoek en Select = toets 5 (coin), Vierkant en Start = toets 1 (start P1), Cirkel = niets.
   - PS = altijd recenter (nieuw midden, log "recenter met de PS-knop"), ook als de uitvoer niet gewapend is.
   De indeling staat in `config.json` onder `gun_buttons.map` (zie docs/architecture.md, "Knoppenindeling"); na een wijziging de daemon herstarten (orchestrator). Bol afdekken of Move uitzetten: na 250 ms zijn alle knoppen en toetsen los (`buttons_down` en `keys_down` leeg) en beweegt de cursor niet meer.
   - Sharp Shooter (alleen als EXT verbonden is): de trekker is het fire-bit van de uitbreiding (byte 0, 0x40; T-hysterese is dan niet meer de trekker); de pomp (`pump`) is T zonder fire-bit en doet standaard reload; `ss_rl` (het RL-bit, 0x80) doet ook reload maar is in de praktijk nooit gezien; `ss_fire`, `sel1`, `sel2`, `sel3` = niets. Gemeten in de Halo-opname van 2026-10-08: 131 schoten en 5 pompen, zie `./build/ravengun-analyze buttons <opname>` (schoten, volgorde van T en fire, pompen) en docs/architecture.md, "Uitbreidingsconnector". Controle: `./build/ravengunctl status` toont "Uitbreiding: ext verbonden: ja [..]" en `watch` de ruwe bytes (byte 0: 0x40 fire, 0x01/0x02/0x04 vuurstand) en de knoppen `pump` en `ss_fire`. Een pomp geeft geen schot en een schot geen pomp. Met `sharp_shooter.extension` = `none` wordt de uitbreiding genegeerd en is T met hysterese weer de trekker.
7. In Halo: richten op het midden, overhalen, zwaaien; let op vertraging (1€ met beta 10 is de referentie) en drift. `./build/ravengunctl disarm` aan het eind.

## Driftmeting tijdens het spelen

1. Neem op terwijl de gebruiker Halo speelt: `./build/ravengunctl record --start --tag halo-drift --duration 600`. Elke keer dat het vizier niet meer op het midden staat richt de gebruiker op het midden van het scherm en drukt PS (dat is gratis een meetpunt). Zet eventueel `record --mark pomp` bij het testen van de pomp.
2. Offline: `./build/ravengun-analyze drift <opname> [--mode mahony_mag --cal ~/.config/ravengun/magnetometer/<sleutel>.json]`. Lees per interval de drift in graden per minuut naast gedraaide hoek, maximale snelheid, verloren rapporten en gyro-verzadiging; de samenvatting geeft mediaan, p90, de cumulatieve drift van de eerste tot de laatste PS-druk en de correlaties. De opname gaat door de huidige biasschatter (modus new); vergelijk met `--bias-mode legacy` (oud gedrag, alleen stilstand) en, voor een warme start, `--bias-init X,Y,Z` (graden/s) of `--bias-init warm` (twee ronden, circulair). De eerste regel van de uitvoer ("Gyro-bias: ...") toont modus, start en eindbias. Parameters bijstellen zonder hercompileren: `--bias-set quasi_gate_dps=1.2`, `temp_min_range=25`, `temp_comp=0` enzovoort.
3. Een lage correlatie bij weinig intervallen zegt weinig: doe er minstens 10. De per-interval-mediaan wordt begrensd door het richten van de speler (1 tot 2 graden per PS-druk, bij intervallen van 13 s al 6 tot 8 graden/min); kijk daarom ook naar de cumulatieve drift en wissel de PS-drukken af met intervallen van minstens 30 s.
4. Begin de speelsessie met de Move 10 tot 60 s stil op tafel (de daemon leert de temperatuurhelling en de bias) en druk pas daarna PS; doe je dat niet, noteer het in de tag (bijvoorbeeld `halo-drift-koud`).
5. Controle van de bewaarde bias: `./build/ravengunctl status` ("Gyro-bias: ... bewaard: hersteld bij start (tc ..); N keer bewaard in deze run"). Het bestand staat in `~/.local/state/ravengun/gyro-bias/`; verwijderen mag altijd (de volgende sessie begint dan koud). `ravengund --no-bias-store` zet laden en bewaren uit, `--bias-mode legacy` schakelt de nieuwe schatter uit.
